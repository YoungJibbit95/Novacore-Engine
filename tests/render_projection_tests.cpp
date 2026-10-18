#include "../engine/src/render/CameraMath.hpp"

#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string_view>

namespace {
using novacore::math::Vec3;
using novacore::render::RenderCamera3D;
using novacore::render::detail::Mat4;
int failures = 0;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        ++failures;
        std::cerr << "[fail] " << message << '\n';
    }
}

float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3 unit(Vec3 a) { return a * (1.0F / std::sqrt(dot(a, a))); }
bool near(float a, float b) { return std::abs(a - b) < 0.00005F; }
bool near(Vec3 a, Vec3 b) { return near(a.x, b.x) && near(a.y, b.y) && near(a.z, b.z); }

std::array<float, 3> project(const Mat4& matrix, Vec3 point) {
    const std::array<float, 4> input{point.x, point.y, point.z, 1.0F};
    std::array<float, 4> clip{};
    for (std::size_t row = 0; row < 4; ++row) {
        for (std::size_t column = 0; column < 4; ++column) {
            clip[row] += matrix.at(row, column) * input[column];
        }
    }
    return {clip[0] / clip[3], clip[1] / clip[3], clip[2] / clip[3]};
}

void testSkyWorldProjectionAgreement() {
    const std::array<std::array<unsigned, 2>, 4> extents{{{1280, 720}, {720, 1280}, {1920, 1080}, {0, 0}}};
    const std::array<std::array<float, 2>, 5> points{{{0, 0}, {-1, -1}, {1, -1}, {-0.6F, 0.8F}, {1, 1}}};
    for (const auto extent : extents) {
        for (const float fov : {40.0F, 74.0F, 110.0F}) {
            for (const float yaw : {0.0F, 37.0F, 180.0F, -113.0F}) {
                for (const float pitch : {-90.0F, -21.0F, 43.0F, 90.0F}) {
                    for (const float roll : {0.0F, 31.0F, 90.0F}) {
                        RenderCamera3D camera{};
                        camera.yawDegrees = yaw;
                        camera.pitchDegrees = pitch;
                        camera.rollDegrees = roll;
                        camera.verticalFovDegrees = fov;
                        const auto before = novacore::render::detail::skyRayBasis(camera, extent[0], extent[1]);
                        for (const Vec3 position : {Vec3{}, Vec3{12.0F, -7.0F, 31.0F}}) {
                            camera.position = position;
                            const auto rays = novacore::render::detail::skyRayBasis(camera, extent[0], extent[1]);
                            expect(near(before.forward, rays.forward) && near(before.right, rays.right) &&
                                       near(before.down, rays.down),
                                   "sky ray basis is independent of camera translation");
                            const auto vp =
                                novacore::render::detail::cameraViewProjection(camera, extent[0], extent[1]);
                            for (const auto ndc : points) {
                                // Same linear basis consumed by sky.frag, independently projected
                                // through the exact matrix submitted to world mesh/box/line passes.
                                const auto ray = unit(rays.forward + rays.right * ndc[0] + rays.down * ndc[1]);
                                const auto projected = project(vp, position + ray * 20.0F);
                                expect(near(projected[0], ndc[0]) && near(projected[1], ndc[1]),
                                       "sky ray projects to the same framebuffer pixel as world geometry");
                            }
                        }
                    }
                }
            }
        }
    }
}

void testCameraAxesAndDepth() {
    RenderCamera3D camera{};
    camera.position = {};
    auto basis = novacore::render::detail::skyRayBasis(camera, 1280, 720);
    expect(near(basis.forward, {0, 0, 1}), "zero yaw/pitch looks along world +Z");
    expect(basis.right.x < 0, "physical camera right is world -X when a right-handed camera looks along +Z");
    expect(basis.down.y < 0, "positive framebuffer Y points below the world horizon");
    camera.rollDegrees = 90;
    basis = novacore::render::detail::skyRayBasis(camera, 1280, 720);
    expect(basis.right.y > 0 && near(basis.right.x, 0), "positive camera roll rotates right toward world up");
    camera.rollDegrees = 0;
    camera.yawDegrees = 90;
    basis = novacore::render::detail::skyRayBasis(camera, 1280, 720);
    expect(near(basis.forward, {1, 0, 0}), "positive yaw looks along world +X");
    camera.yawDegrees = 0;
    camera.pitchDegrees = 90;
    basis = novacore::render::detail::skyRayBasis(camera, 1280, 720);
    expect(near(basis.forward, {0, 1, 0}), "vertical pitch remains defined and looks up");
    camera.pitchDegrees = 0;
    const auto vp = novacore::render::detail::cameraViewProjection(camera, 1280, 720);
    expect(near(project(vp, {0, 0, camera.nearPlane})[2], 0), "Vulkan near depth is zero");
    expect(near(project(vp, {0, 0, camera.farPlane})[2], 1), "Vulkan far depth is one");
}

void testWorldSunAndLowElevation() {
    // Proposed Dev Range sun: 20-degree elevation, 25-degree azimuth.
    novacore::render::RenderWorldLighting lighting{};
    lighting.sunDirection = {0.397131F, 0.342020F, 0.851651F};
    const Vec3 towardSun = unit(lighting.sunDirection);
    RenderCamera3D camera{};
    camera.position = {};
    camera.yawDegrees = 25;
    const auto vp = novacore::render::detail::cameraViewProjection(camera, 1280, 720);
    const auto sunNdc = project(vp, towardSun * 50.0F);
    expect(near(sunNdc[0], 0) && sunNdc[1] < 0 && sunNdc[1] > -1,
           "low-elevation world sun appears above the horizon in a level 74-degree view");
    auto rays = novacore::render::detail::skyRayBasis(camera, 1280, 720);
    const auto discRay = unit(rays.forward + rays.right * sunNdc[0] + rays.down * sunNdc[1]);
    expect(dot(discRay, towardSun) > 0.99999F, "sun disc ray agrees with the directional light toward-sun vector");
    camera.position = {30, 2, -19};
    const auto movedVp = novacore::render::detail::cameraViewProjection(camera, 1280, 720);
    const auto moved = project(movedVp, camera.position + towardSun * 50);
    expect(near(sunNdc[0], moved[0]) && near(sunNdc[1], moved[1]), "world sun does not move under translation");
    camera.pitchDegrees = 20;
    rays = novacore::render::detail::skyRayBasis(camera, 1280, 720);
    expect(dot(rays.forward, towardSun) > 0.99999F,
           "camera aligned to sun azimuth/elevation centers the real direction");
    camera.yawDegrees = 50;
    const auto rotated =
        project(novacore::render::detail::cameraViewProjection(camera, 1280, 720), camera.position + towardSun * 50);
    expect(rotated[0] > 0.1F,
           "positive logical yaw turns left, moving the lower-azimuth world sun toward physical right");
    camera.yawDegrees = 25;
    camera.pitchDegrees = 0;
    camera.verticalFovDegrees = 40;
    const auto narrow =
        project(novacore::render::detail::cameraViewProjection(camera, 1280, 720), camera.position + towardSun * 50);
    expect(std::abs(narrow[1]) > std::abs(sunNdc[1]), "narrow FOV enlarges the sun's distance from the screen center");
}

Vec3 cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }

void testProperRightHandedCameraRotation() {
    for (const float yaw : {0.0F, 37.0F, 180.0F, -143.0F}) {
        for (const float pitch : {-90.0F, -21.0F, 0.0F, 47.0F, 90.0F}) {
            for (const float roll : {-90.0F, -17.0F, 0.0F, 31.0F, 90.0F}) {
                RenderCamera3D camera{};
                camera.position = {3, 1.7F, -7};
                camera.yawDegrees = yaw;
                camera.pitchDegrees = pitch;
                camera.rollDegrees = roll;
                const auto frame = novacore::render::detail::cameraFrame(camera);
                // A physical glTF camera looks along local -Z. Its actual
                // rotation is (right,up,backward), not (right,up,logical forward).
                expect(near(dot(cross(frame.right, frame.up), frame.forward * -1), 1),
                       "camera view rotation has determinant +1 and preserves right-handed content chirality");
                expect(near(dot(frame.right, frame.right), 1) && near(dot(frame.up, frame.up), 1) &&
                           near(dot(frame.forward, frame.forward), 1),
                       "physical camera basis remains orthonormal at all pitch/roll angles");
                const auto vp = novacore::render::detail::cameraViewProjection(camera, 1280, 720);
                const auto right = project(vp, camera.position + frame.forward * 5 + frame.right);
                const auto up = project(vp, camera.position + frame.forward * 5 + frame.up);
                expect(right[0] > 0 && near(right[1], 0), "physical camera right projects right after yaw/pitch/roll");
                expect(up[1] < 0 && near(up[0], 0), "physical camera up projects upward in Vulkan's framebuffer");
                const auto front = camera.position + frame.forward * 5;
                const float clipW = vp.at(3, 0) * front.x + vp.at(3, 1) * front.y + vp.at(3, 2) * front.z + vp.at(3, 3);
                expect(near(clipW, 5),
                       "standard RH projection gives positive clip W for the unchanged logical look direction");
            }
        }
    }
}

void testAnatomicalHandAndFingerChiralityAgainstGltfCamera() {
    // Asymmetric physical fixture, independently authored in a RH glTF frame:
    // the character faces -Z, its actual right hand is +X, each thumb lies on
    // the medial side, and index fingertips extend farther toward -Z. These
    // are geometric landmarks, not socket/bone names or a weapon-X assertion.
    const std::array<Vec3, 8> source{
        Vec3{0.30F, -0.25F, -2.00F},  // right wrist
        Vec3{0.32F, -0.22F, -2.20F},  // right index fingertip
        Vec3{0.20F, -0.18F, -2.08F},  // right thumb, toward the body centre
        Vec3{-0.30F, -0.25F, -2.00F}, // left wrist
        Vec3{-0.34F, -0.23F, -2.23F}, // left index fingertip
        Vec3{-0.19F, -0.16F, -2.07F}, // left thumb, toward the body centre
        Vec3{0, 0.20F, -2.60F},       // physical front/nose
        Vec3{0, 0.20F, -2.20F},       // rear landmark
    };
    RenderCamera3D camera{};
    camera.position = {};
    const auto vp = novacore::render::detail::cameraViewProjection(camera, 1280, 720);
    constexpr float radians = 3.14159265358979323846F / 180;
    const float f = 1.0F / std::tan(74.0F * radians * 0.5F);
    const float aspect = 1280.0F / 720;
    std::array<std::array<float, 3>, source.size()> actual{};
    std::array<std::array<float, 2>, source.size()> reference{};
    for (std::size_t i = 0; i < source.size(); ++i) {
        // Existing offline canonical frame is a proper +180-degree Y rotation,
        // determinant +1: source front -Z -> +Z and true right +X -> -X.
        const Vec3 canonical{-source[i].x, source[i].y, -source[i].z};
        actual[i] = project(vp, canonical);
        // Independent standard glTF camera at origin: lens -Z, local +X right;
        // only Vulkan's framebuffer Y convention changes its screen Y sign.
        reference[i] = {(f / aspect) * source[i].x / -source[i].z, -f * source[i].y / -source[i].z};
        expect(near(actual[i][0], reference[i][0]) && near(actual[i][1], reference[i][1]),
               "proper source-to-world rotation preserves anatomical hand/finger/front projection against a standard "
               "glTF camera");
    }
    expect(actual[0][0] > 0 && actual[3][0] < 0,
           "actual right and left wrists retain anatomical screen sides from behind");
    expect(actual[2][0] < actual[0][0] && actual[5][0] > actual[3][0],
           "physical medial thumbs stay toward the body centre rather than appearing on the wrong hand");
    const auto signedArea = [](const auto& a, const auto& b, const auto& c) {
        return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]);
    };
    expect(signedArea(actual[0], actual[1], actual[2]) * signedArea(reference[0], reference[1], reference[2]) > 0 &&
               signedArea(actual[3], actual[4], actual[5]) * signedArea(reference[3], reference[4], reference[5]) > 0,
           "asymmetric wrist/index/thumb triangles preserve image chirality on both hands");
    expect(-source[6].z > -source[7].z, "proper canonical character still faces logical world +Z");

    camera.yawDegrees = 180;
    const auto opposite = novacore::render::detail::cameraViewProjection(camera, 1280, 720);
    const auto oppositeRight = project(opposite, source[0]);
    expect(near(oppositeRight[0], reference[0][0]) && near(oppositeRight[1], reference[0][1]),
           "opposite logical yaw preserves the same actual right-hand image using the original -Z-facing fixture");
}
} // namespace

int main() {
    testSkyWorldProjectionAgreement();
    testCameraAxesAndDepth();
    testWorldSunAndLowElevation();
    testProperRightHandedCameraRotation();
    testAnatomicalHandAndFingerChiralityAgainstGltfCamera();
    if (failures == 0) {
        std::cout << "render projection contracts passed\n";
    }
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
