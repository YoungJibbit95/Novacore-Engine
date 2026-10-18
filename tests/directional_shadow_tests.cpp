#include "../engine/src/render/DirectionalShadowMath.hpp"

#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string_view>

namespace {
using novacore::math::Vec3;
namespace render = novacore::render;
namespace detail = novacore::render::detail;
int failures = 0;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        ++failures;
        std::cerr << "[fail] " << message << '\n';
    }
}

bool near(float a, float b, float tolerance = 0.00005F) { return std::abs(a - b) < tolerance; }
bool near(Vec3 a, Vec3 b, float tolerance = 0.00005F) {
    return near(a.x, b.x, tolerance) && near(a.y, b.y, tolerance) && near(a.z, b.z, tolerance);
}

void testSettingsAndInvalidInputs() {
    const render::RenderFrameInfo defaults{};
    expect(!defaults.directionalShadows.enabled, "existing frame extraction keeps shadow resources opt-in");
    expect(render::RenderMesh3D{}.castsShadow && render::RenderMesh3D{}.receivesShadow &&
               render::RenderBox3D{}.castsShadow && render::RenderBox3D{}.receivesShadow,
           "ordinary world geometry participates unless the extractor explicitly opts out");
    auto camera = defaults.camera3D;
    expect(detail::validShadowReceiverCamera(camera), "default receiver camera is finite");
    camera.rollDegrees = std::numeric_limits<float>::quiet_NaN();
    expect(!detail::validShadowReceiverCamera(camera), "non-finite camera orientation cannot poison shadow UBOs");
    for (const unsigned resolution : {256U, 512U, 1024U, 2048U}) {
        expect(detail::validDirectionalShadowSettings(resolution, 35, 0.0007F, 0.002F),
               "supported bounded resolution and finite bias/distance are accepted");
    }
    for (const unsigned resolution : {0U, 128U, 513U, 4096U, std::numeric_limits<unsigned>::max()}) {
        expect(!detail::validDirectionalShadowSettings(resolution, 35, 0, 0), "unsupported resolutions are rejected");
    }
    for (const float value :
         {-1.0F, 0.0F, 151.0F, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) {
        expect(!detail::validDirectionalShadowSettings(1024, value, 0, 0),
               "invalid/non-finite receiver distances are rejected");
    }
    expect(!detail::validDirectionalShadowSettings(1024, 35, -0.001F, 0), "negative depth bias is rejected");
    expect(!detail::validDirectionalShadowSettings(1024, 35, 0, 0.051F), "excessive normal bias is rejected");
    expect(!detail::validDirectionalShadowSettings(1024, 35, std::numeric_limits<float>::quiet_NaN(), 0),
           "non-finite depth bias is rejected before uniform upload");
    expect(!detail::directionalShadowCamera({}, {}, 35, 1024), "zero sun direction is rejected");
    expect(!detail::directionalShadowCamera({0, std::numeric_limits<float>::infinity(), 0}, {0, 1, 0}, 35, 1024),
           "non-finite camera position is rejected");
}

void testInstanceBoundsAndIndependentOcclusion() {
    detail::ShadowMeshBounds bounds{};
    bounds.include({-2, -1, -3});
    bounds.include({4, 5, 7});
    // Independently written affine instance: Y +90 degrees, scale (2,.5,-3),
    // translation (10,20,30). Its world center is (-6+10,1+20,-2+30).
    auto instance = detail::identity();
    instance.at(0, 0) = 0;
    instance.at(0, 2) = -3;
    instance.at(1, 1) = 0.5F;
    instance.at(2, 0) = -2;
    instance.at(2, 2) = 0;
    instance.at(0, 3) = 10;
    instance.at(1, 3) = 20;
    instance.at(2, 3) = 30;
    const auto sphere = detail::transformedShadowBounds(bounds, instance);
    expect(sphere && near(sphere->center, {4, 21, 28}) && near(sphere->radius, std::sqrt(263.25F)),
           "caster bounds transform cooked positions once with rotation, signed/nonuniform scale and translation");
    expect(!detail::transformedShadowBounds({}, instance), "empty primitives produce no phantom caster bounds");
    // A moving CPU-skinned surface must move its conservative bound, rather
    // than retaining the bind-pose AABB. The backend updates each safe slot
    // together with the corresponding successful vertex-buffer write.
    detail::ShadowMeshBounds animated{};
    animated.include({-2, -1, -3});
    animated.include({4, 15, 7});
    const auto moved = detail::transformedShadowBounds(animated, instance);
    expect(moved && sphere && moved->center.y > sphere->center.y && moved->radius > sphere->radius,
           "animated current positions expand and move the caster bound");
    instance.at(0, 0) = std::numeric_limits<float>::infinity();
    expect(!detail::transformedShadowBounds(bounds, instance), "invalid affine transforms cannot enter caster culling");

    // Sun at +Y: the horizontal occluder y=4 is closer to the light than the
    // receiver y=0. Verify numeric depth independently of the helper's basis.
    const auto light = detail::directionalShadowCamera({}, {0, 1, 0}, 35, 1024);
    expect(light.has_value(), "independent planar occlusion fixture has a valid light camera");
    if (light) {
        const auto receiver = detail::shadowProjectPoint(light->viewProjection, {0, 0, 0});
        const auto occluder = detail::shadowProjectPoint(light->viewProjection, {0, 4, 0});
        expect(near(receiver.z, 0.5F) && near(occluder.z, 0.5F - 4 / (4 * light->radius)),
               "light depth follows world sun elevation with a known four-metre blocker");
        const auto frontReceiver = detail::shadowProjectPoint(light->viewProjection, {0, 5, 0});
        expect(occluder.z < receiver.z - 0.0007F && frontReceiver.z - 0.0007F <= occluder.z,
               "LESS_OR_EQUAL comparison shadows behind the blocker and keeps the front surface lit");
    }
}

void testWorldLightProjectionAndCoverage() {
    const Vec3 camera{3.123F, 1.71F, -8.975F};
    for (const auto sun : {Vec3{0, 1, 0}, Vec3{0, -1, 0}, Vec3{0, 0, 1}, Vec3{0.397131F, 0.342020F, 0.851651F}}) {
        const auto light = detail::directionalShadowCamera(camera, sun, 35, 1024);
        expect(light.has_value(), "sun light basis is defined at vertical sun angles");
        if (!light) {
            continue;
        }
        expect(near(light->towardSun.lengthSquared(), 1), "world toward-sun direction is normalized");
        expect(near(detail::shadowDot(light->right, light->up), 0) &&
                   near(detail::shadowDot(light->up, light->forward), 0),
               "light frame is orthogonal");
        const auto center = detail::shadowProjectPoint(light->viewProjection, light->center);
        const auto nearSun = detail::shadowProjectPoint(light->viewProjection, light->center + light->towardSun * 5);
        expect(near(center.x, 0) && near(center.y, 0) && near(center.z, 0.5F), "light center projects to mid depth");
        expect(near(nearSun.x, center.x) && near(nearSun.y, center.y) && nearSun.z < center.z,
               "surface toward the world sun occludes a receiver behind it");
        const auto upper = detail::shadowProjectPoint(light->viewProjection, light->center + light->up * 1);
        expect(upper.y < 0, "light projection uses Vulkan's downward framebuffer Y");
        for (int i = 0; i <= 20; ++i) {
            for (int j = 0; j <= 20; ++j) {
                const float theta = static_cast<float>(i) * 3.14159265358979323846F / 20;
                const float phi = static_cast<float>(j) * 6.28318530717958647692F / 20;
                const Vec3 direction{std::sin(theta) * std::cos(phi), std::cos(theta), std::sin(theta) * std::sin(phi)};
                const auto edge = detail::shadowProjectPoint(light->viewProjection, camera + direction * 35);
                expect(std::abs(edge.x) <= 1.00001F && std::abs(edge.y) <= 1.00001F && edge.z >= 0 && edge.z <= 1,
                       "texel-snapped map contains the entire configured camera-centred receiver sphere");
            }
        }
    }
}

void testTexelStabilityAndConservativeCasterCulling() {
    const auto light = detail::directionalShadowCamera({}, {0.4F, 0.3F, 0.85F}, 35, 1024);
    expect(light.has_value(), "light camera initializes for stability tests");
    if (!light) {
        return;
    }
    const auto moved =
        detail::directionalShadowCamera(light->right * (light->texelWorldSize * 0.2F), light->towardSun, 35, 1024);
    expect(moved.has_value(), "sub-texel translated light camera initializes");
    if (moved) {
        for (std::size_t column = 0; column < 4; ++column) {
            expect(near(light->viewProjection.at(0, column), moved->viewProjection.at(0, column)) &&
                       near(light->viewProjection.at(1, column), moved->viewProjection.at(1, column)),
                   "sub-texel camera movement leaves the snapped shadow footprint unchanged");
        }
    }
    expect(detail::shadowCasterSphereVisible(*light, {}, 0), "receiver center is a visible caster candidate");
    expect(detail::shadowCasterSphereVisible(*light, light->towardSun * 50, 1),
           "off-camera caster toward sun remains eligible to shadow the receiver sphere");
    const float radius = 0.5F;
    for (const Vec3 axis : {light->right, light->up}) {
        for (const float sign : {-1.0F, 1.0F}) {
            expect(detail::shadowCasterSphereVisible(*light, axis * ((light->radius + radius - 0.01F) * sign), radius),
                   "caster sphere overlapping a light XY plane is conservatively retained");
            expect(!detail::shadowCasterSphereVisible(*light, axis * ((light->radius + radius + 0.01F) * sign), radius),
                   "caster wholly beyond a light XY plane is culled");
        }
    }
    for (const float sign : {-1.0F, 1.0F}) {
        expect(!detail::shadowCasterSphereVisible(
                   *light, light->forward * ((2 * light->radius + radius + 0.01F) * sign), radius),
               "caster beyond the finite light-depth extrusion is culled");
    }
    expect(!detail::shadowCasterSphereVisible(*light, {}, -1), "negative sphere radius is rejected");
}

void testWorldReconstructionAndUniformContract() {
    for (const float fov : {40.0F, 55.0F, 74.0F, 105.0F, 110.0F}) {
        for (const auto extent : {std::array<unsigned, 2>{1280, 720}, std::array<unsigned, 2>{720, 1280}}) {
            render::RenderCamera3D camera{};
            camera.position = {12, 1.7F, -9};
            camera.yawDegrees = 37;
            camera.pitchDegrees = -21;
            camera.rollDegrees = 15;
            camera.verticalFovDegrees = fov;
            const auto light = detail::directionalShadowCamera(camera.position, {0.4F, 0.3F, 0.85F}, 35, 1024);
            if (!light) {
                expect(false, "light camera initializes for uniform test");
                continue;
            }
            const auto constants =
                detail::directionalShadowUniforms(*light, camera, extent[0], extent[1], 0.0007F, 0.002F, true);
            const auto disabled =
                detail::directionalShadowUniforms(*light, camera, extent[0], extent[1], 0.0007F, 0.002F, false);
            expect(constants.parameters[3] == 1 && disabled.parameters[3] == 0,
                   "per-draw receiver opt-out has an explicit disabled uniform entry");
            expect(near(constants.parameters[2], 1.0F / 1024) && constants.cameraPositionDistance[3] == 35,
                   "PCF texel size and receiver distance describe the real map");
            const auto vp = detail::cameraViewProjection(camera, extent[0], extent[1]);
            for (const Vec3 world : {Vec3{0, 0, 0}, Vec3{33, 4, 21}, Vec3{-17, 2, 120}, Vec3{4, -3, -2}}) {
                const std::array<float, 4> input{world.x, world.y, world.z, 1};
                std::array<float, 4> clip{};
                for (std::size_t row = 0; row < 4; ++row) {
                    for (std::size_t column = 0; column < 4; ++column) {
                        clip[row] += vp.at(row, column) * input[column];
                    }
                }
                const Vec3 reconstructed{
                    camera.position.x + constants.cameraRight[0] * clip[0] + constants.cameraDown[0] * clip[1] +
                        constants.cameraForward[0] * clip[3],
                    camera.position.y + constants.cameraRight[1] * clip[0] + constants.cameraDown[1] * clip[1] +
                        constants.cameraForward[1] * clip[3],
                    camera.position.z + constants.cameraRight[2] * clip[0] + constants.cameraDown[2] * clip[1] +
                        constants.cameraForward[2] * clip[3],
                };
                expect(near(world, reconstructed, 0.0003F),
                       "shadow receiver reconstructs actual world geometry under FOV/aspect/yaw/pitch/roll without "
                       "reapplying a node/model transform");
                expect(near(detail::shadowProjectPoint(light->viewProjection, world),
                            detail::shadowProjectPoint(light->viewProjection, reconstructed)),
                       "caster and receiver use the same light projection");
            }
        }
    }
    detail::ShadowMeshBounds bounds{};
    expect(!bounds.valid, "empty mesh has no caster bounds");
    bounds.include({2, -1, 3});
    bounds.include({-3, 2, 1});
    expect(bounds.valid && near(bounds.minimum, {-3, -1, 1}) && near(bounds.maximum, {2, 2, 3}),
           "caster bounds preserve actual cooked/current skinned vertex positions");
}
} // namespace

int main() {
    testSettingsAndInvalidInputs();
    testWorldLightProjectionAndCoverage();
    testTexelStabilityAndConservativeCasterCulling();
    testWorldReconstructionAndUniformContract();
    testInstanceBoundsAndIndependentOcclusion();
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
