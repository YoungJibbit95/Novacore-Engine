#include "../engine/src/render/MeshLighting.hpp"

#include <algorithm>
#include <array>
#include <cfenv>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string_view>

namespace {
using novacore::math::Vec3;
using novacore::render::RenderMesh3D;
int failures = 0;
float maximumDirectionErrorDegrees = 0;
float maximumScalarError = 0;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        ++failures;
        std::cerr << "[fail] " << message << '\n';
    }
}

float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3 cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
Vec3 unit(Vec3 a) { return a * (1.0F / std::sqrt(dot(a, a))); }
bool near(Vec3 a, Vec3 b) { return (a - b).lengthSquared() < 0.0000000001F; }

// Independent explicit axis rotations in the renderer's documented Z-X-Y order.
Vec3 rotate(Vec3 value, const RenderMesh3D& mesh) {
    constexpr float radians = 3.14159265358979323846F / 180.0F;
    const float z = mesh.rollDegrees * radians;
    value = {std::cos(z) * value.x - std::sin(z) * value.y, std::sin(z) * value.x + std::cos(z) * value.y, value.z};
    const float x = mesh.pitchDegrees * radians;
    value = {value.x, std::cos(x) * value.y - std::sin(x) * value.z, std::sin(x) * value.y + std::cos(x) * value.z};
    const float y = mesh.yawDegrees * radians;
    return {std::cos(y) * value.x + std::sin(y) * value.z, value.y, -std::sin(y) * value.x + std::cos(y) * value.z};
}

// Evaluate the uploaded quaternion through a rotation matrix, independently of
// the vertex shader's cross-product quaternion implementation.
Vec3 transformNormal(Vec3 normal, const novacore::render::detail::MeshNormalTransform& transform) {
    normal = {normal.x * transform.inverseScale[0], normal.y * transform.inverseScale[1],
              normal.z * transform.inverseScale[2]};
    const auto q = transform.rotation;
    return unit({
        (1 - 2 * (q[1] * q[1] + q[2] * q[2])) * normal.x + 2 * (q[0] * q[1] - q[2] * q[3]) * normal.y +
            2 * (q[0] * q[2] + q[1] * q[3]) * normal.z,
        2 * (q[0] * q[1] + q[2] * q[3]) * normal.x + (1 - 2 * (q[0] * q[0] + q[2] * q[2])) * normal.y +
            2 * (q[1] * q[2] - q[0] * q[3]) * normal.z,
        2 * (q[0] * q[2] - q[1] * q[3]) * normal.x + 2 * (q[1] * q[2] + q[0] * q[3]) * normal.y +
            (1 - 2 * (q[0] * q[0] + q[1] * q[1])) * normal.z,
    });
}

void testInverseTransposeInstanceNormals() {
    for (const Vec3 scale : {Vec3{1, 1, 1}, Vec3{2, 0.5F, 3}, Vec3{0.001F, 4, 0.1F}, Vec3{-2, 0.5F, 3}}) {
        for (const float yaw : {0.0F, 90.0F, 180.0F, 37.0F}) {
            for (const float pitch : {0.0F, 21.0F, -48.0F}) {
                for (const float roll : {0.0F, 33.0F, 89.0F}) {
                    RenderMesh3D mesh{};
                    mesh.scale = scale;
                    mesh.yawDegrees = yaw;
                    mesh.pitchDegrees = pitch;
                    mesh.rollDegrees = roll;
                    const auto transform = novacore::render::detail::meshNormalTransform(mesh);
                    expect(transform.has_value(), "finite nonsingular instance normal transform is accepted");
                    if (!transform) {
                        continue;
                    }
                    for (const Vec3 source : {Vec3{1, 2, 3}, Vec3{-2, 1, -1}, Vec3{0, 1, 0}}) {
                        const auto normal = unit(source);
                        const auto tangent = unit(cross(normal, {1, 0, 0}));
                        const auto bitangent = cross(normal, tangent);
                        const auto instanceTangent =
                            rotate({tangent.x * scale.x, tangent.y * scale.y, tangent.z * scale.z}, mesh);
                        const auto instanceBitangent =
                            rotate({bitangent.x * scale.x, bitangent.y * scale.y, bitangent.z * scale.z}, mesh);
                        const auto actual = transformNormal(normal, *transform);
                        const auto expected =
                            unit(rotate({normal.x / scale.x, normal.y / scale.y, normal.z / scale.z}, mesh));
                        expect(near(actual, expected),
                               "uploaded rotation/inverse-scale equals world inverse transpose");
                        expect(std::abs(dot(actual, unit(instanceTangent))) < 0.00002F &&
                                   std::abs(dot(actual, unit(instanceBitangent))) < 0.00002F,
                               "world normal remains perpendicular to both transformed geometric tangents");
                        mesh.position = {71, -12, 5};
                        const auto translated = novacore::render::detail::meshNormalTransform(mesh);
                        expect(translated && near(actual, transformNormal(normal, *translated)),
                               "translation cannot change instance normal lighting");
                    }
                }
            }
        }
    }
}

void testRotationLightingAndBakedVertices() {
    RenderMesh3D mesh{};
    const auto original = novacore::render::detail::meshNormalTransform(mesh);
    expect(original && dot(transformNormal({1, 0, 0}, *original), {1, 0, 0}) > 0.9999F,
           "surface facing the world key light receives key diffuse");
    mesh.yawDegrees = 180;
    const auto opposite = novacore::render::detail::meshNormalTransform(mesh);
    expect(opposite && std::max(dot(transformNormal({1, 0, 0}, *opposite), {1, 0, 0}), 0.0F) == 0.0F,
           "180-degree instance rotation turns a surface away from the same world light");
    mesh.yawDegrees = 0;
    const Vec3 bakedNodeNormal = unit({2, -3, 1});
    const auto identity = novacore::render::detail::meshNormalTransform(mesh);
    expect(identity && near(transformNormal(bakedNodeNormal, *identity), bakedNodeNormal),
           "identity render instance preserves normals already transformed by the glTF importer or skinning");
    for (const Vec3 invalid : {Vec3{0, 1, 1}, Vec3{1, std::numeric_limits<float>::quiet_NaN(), 1},
                               Vec3{1, 1, std::numeric_limits<float>::infinity()}}) {
        mesh.scale = invalid;
        expect(!novacore::render::detail::meshNormalTransform(mesh),
               "singular or non-finite normal transforms are rejected");
    }
    mesh.scale = {0.000000001F, 1, 1};
    expect(novacore::render::detail::meshNormalTransform(mesh).has_value(),
           "small finite nonsingular scale is not rejected by an arbitrary epsilon");
}

Vec3 decodeDirection(std::uint32_t packed) {
    const float x = static_cast<float>(packed & 65535U) / 65535.0F * 2 - 1;
    const float y = static_cast<float>(packed >> 16U) / 65535.0F * 2 - 1;
    Vec3 decoded{x, y, 1 - std::abs(x) - std::abs(y)};
    const float t = std::clamp(-decoded.z, 0.0F, 1.0F);
    decoded.x += decoded.x >= 0 ? -t : t;
    decoded.y += decoded.y >= 0 ? -t : t;
    return unit(decoded);
}

void testBoundedPushPacking() {
    for (int x = -10; x <= 10; ++x) {
        for (int y = -10; y <= 10; ++y) {
            for (int z = -10; z <= 10; ++z) {
                if (x == 0 && y == 0 && z == 0) {
                    continue;
                }
                const Vec3 direction{static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)};
                const auto decoded = decodeDirection(novacore::render::detail::packLightDirection(direction));
                const double chord = std::sqrt(static_cast<double>((decoded - unit(direction)).lengthSquared()));
                const float angle = static_cast<float>(2 * std::asin(chord * 0.5) * 180 / 3.14159265358979323846);
                maximumDirectionErrorDegrees = std::max(maximumDirectionErrorDegrees, angle);
                expect(angle < 0.01F, "packed world light direction stays within 0.01 degree in both hemispheres");
            }
        }
    }
    for (int i = 0; i <= 10000; ++i) {
        const float first = static_cast<float>(i) / 10000.0F;
        const float second = 1 - first;
        const auto packed = novacore::render::detail::packUnitPair(first, second);
        const float a = static_cast<float>(packed & 65535U) / 65535.0F;
        const float b = static_cast<float>(packed >> 16U) / 65535.0F;
        maximumScalarError = std::max({maximumScalarError, std::abs(a - first), std::abs(b - second)});
        expect(std::abs(a - first) <= 0.0000077F && std::abs(b - second) <= 0.0000077F,
               "packed light/material factors retain unorm16 precision");
    }
    expect(novacore::render::detail::packUnitPair(-1, 2) == 0xffff0000U, "packing clamps scalar endpoints");
    std::feclearexcept(FE_ALL_EXCEPT);
    expect(novacore::render::detail::packUnitPair(std::numeric_limits<float>::quiet_NaN(),
                                                  std::numeric_limits<float>::infinity()) == 0U,
           "non-finite scalar factors use a deterministic zero before integer conversion");
    expect((std::fetestexcept(FE_INVALID) & FE_INVALID) == 0,
           "non-finite factor handling does not perform an invalid float-to-integer conversion");
    expect(novacore::render::detail::packUnitPair(-std::numeric_limits<float>::infinity(), 0.5F) == 0x80000000U,
           "non-finite first factor does not affect the independent valid second factor");
    expect((decodeDirection(novacore::render::detail::packLightDirection({})) - Vec3{0, 0, 1}).lengthSquared() <
               0.00000001F,
           "zero light direction retains the renderer's +Z fallback within octahedral precision");
}
} // namespace

int main() {
    testInverseTransposeInstanceNormals();
    testRotationLightingAndBakedVertices();
    testBoundedPushPacking();
    std::cout << "maximum packed direction error: " << maximumDirectionErrorDegrees
              << " degrees; scalar error: " << maximumScalarError << '\n';
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
