#include "../engine/src/render/MaterialTextureMath.hpp"
#include "../engine/src/render/SkyEnvironment.hpp"
#include <cmath>
#include <iostream>
#include <limits>

namespace {
namespace detail = novacore::render::detail;
using novacore::math::Vec3;
int failures = 0;
void expect(bool value, const char* message) {
    if (!value) {
        ++failures;
        std::cerr << "[fail] " << message << '\n';
    }
}
bool near(float a, float b, float tolerance = .00001F) { return std::abs(a - b) < tolerance; }
bool near(Vec3 a, Vec3 b) { return near(a.x, b.x) && near(a.y, b.y) && near(a.z, b.z); }
} // namespace
int main() {
    using namespace novacore::render;
    expect(!RenderFrameInfo{}.skyEnvironmentReflectionsEnabled, "specular sky IBL is opt-in");
    expect(!RenderBackendFrameStats{}.skyEnvironmentReflectionsReady &&
               RenderBackendFrameStats{}.lastSkyEnvironmentDrawCount == 0,
           "no requested flag claims an actual draw");
    RenderSky sky{};
    expect(!detail::skyRadianceSnapshot(sky), "disabled sky has no environment source");
    sky.enabled = true;
    sky.zenithColor = {1, 0, 0, 1};
    sky.horizonColor = {0, 1, 0, 1};
    sky.groundColor = {0, 0, 1, 1};
    sky.horizonHeight = .5F;
    sky.gradientPower = 1;
    sky.hazeStrength = 0;
    sky.cloudStrength = 0;
    sky.exposure = 2;
    const auto source = *detail::skyRadianceSnapshot(sky);
    expect(near(detail::skyRadiance(source, {0, 1, 0}), {2, 0, 0}) &&
               near(detail::skyRadiance(source, {0, -1, 0}), {0, 0, 2}) &&
               near(detail::skyRadiance(source, {1, 0, 0}), {0, 2, 0}),
           "actual pre-tone linear sky poles/horizon and exposure are preserved");
    sky.sunIntensity = 8;
    sky.sunAngularRadiusDegrees = 12;
    expect(detail::skyRadianceSnapshot(sky) == source, "direct solar disc/glow do not enter reflected radiance");
    sky.zenithColor[0] = std::numeric_limits<float>::quiet_NaN();
    expect(!detail::skyRadianceSnapshot(sky), "non-finite source falls back explicitly");
    sky.zenithColor[0] = 65;
    expect(!detail::skyRadianceSnapshot(sky), "bounded HDR radiance rejects sources exceeding the slice limit");
    sky.zenithColor[0] = 1;
    sky.exposure = std::numeric_limits<float>::infinity();
    expect(!detail::skyRadianceSnapshot(sky), "non-finite exposure is not silently ignored");
    const std::array<Vec3, 6> directions{{{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}}};
    for (std::uint32_t face = 0; face < 6; ++face) {
        expect(near(detail::skyCubeDirection(face, 0, 0), directions[face]),
               "cube layer centers use Vulkan world axes");
    }
    expect(near(detail::skyCubeDirection(0, -1, .3F), detail::skyCubeDirection(4, 1, .3F)) &&
               near(detail::skyCubeDirection(2, 1, .3F), detail::skyCubeDirection(0, -.3F, -1)),
           "adjacent cube edges share world directions without camera mirroring");
    detail::SkyEnvironmentGenerationGate gate;
    const auto now = detail::SkyEnvironmentGenerationGate::Clock::time_point{};
    expect(!gate.observe(source, now) && gate.observe(source, now),
           "generation requires two consecutive equal snapshots");
    expect(!gate.observe(source, now + std::chrono::seconds(10)), "unchanged source is not regenerated each frame");
    auto changed = source;
    changed.exposure = 1;
    expect(!gate.observe(changed, now + std::chrono::milliseconds(10)) &&
               !gate.observe(changed, now + std::chrono::milliseconds(20)),
           "rapid source changes cannot generate every other frame");
    expect(gate.observe(changed, now + std::chrono::seconds(2)),
           "stable changed source becomes eligible after bounded cooldown");
    expect(!gate.observe(std::nullopt, now + std::chrono::seconds(3)), "inactive source is never generated");
    auto constant = source;
    constant.zenith = constant.horizon = constant.ground = {.25F, .5F, 1};
    constant.exposure = 1;
    const auto pixels = detail::generateSkyEnvironment(constant, true);
    expect(pixels.cube.size() == detail::skyCubeFloatCount() &&
               pixels.brdf.size() == detail::skyBrdfSize * detail::skyBrdfSize * 2,
           "generation produces exact fixed mip/layer/LUT extents");
    bool conserved = true;
    for (std::size_t i = 0; i < pixels.cube.size(); i += 4) {
        conserved = conserved && near(pixels.cube[i], .25F, .0001F) && near(pixels.cube[i + 1], .5F, .0001F) &&
                    near(pixels.cube[i + 2], 1, .0001F) && pixels.cube[i + 3] == 1;
    }
    expect(conserved, "GGX convolution conserves a constant radiance field at every face/mip");
    bool finite = true;
    for (const auto value : pixels.brdf) {
        finite = finite && std::isfinite(value) && value >= 0 && value < 2;
    }
    expect(finite, "actual generated LUT remains finite and bounded at grazing/roughness limits");
    const auto smoothNormal = (detail::skyBrdfSize - 1) * 2;
    expect(pixels.brdf[smoothNormal] > .97F && pixels.brdf[smoothNormal + 1] < .0001F,
           "smooth normal-incidence BRDF integrates to nearly unit Fresnel scale with negligible bias");
    const auto roughNormal = ((detail::skyBrdfSize - 1) * detail::skyBrdfSize + detail::skyBrdfSize - 1) * 2;
    expect(pixels.brdf[roughNormal] > .28F && pixels.brdf[roughNormal] < .36F,
           "fully rough GGX has the expected normal-incidence directional albedo");
    const auto varied = detail::generateSkyEnvironment(source, false);
    expect(varied.brdf.empty(), "cached LUT can be reused without regeneration");
    // Source has a strong directional color split; fully rough convolution must
    // reduce, rather than retain, the sharp mirror cube's directional contrast.
    const auto baseFace = static_cast<std::size_t>(detail::skyCubeSize) * detail::skyCubeSize * 4;
    const auto top = (baseFace * 2) + (detail::skyCubeSize / 2 * detail::skyCubeSize + detail::skyCubeSize / 2) * 4;
    const auto bottom = top + baseFace;
    const auto mirrorContrast = std::abs(varied.cube[top] - varied.cube[bottom]);
    const auto last = varied.cube.size() - 24;
    const auto roughContrast = std::abs(varied.cube[last + 8] - varied.cube[last + 12]);
    expect(roughContrast < mirrorContrast * .8F, "roughness broadens the reflected sky lobe across direction changes");
    const auto frame = detail::materialFrameUniforms({}, 1280, 720, false);
    expect(sizeof(frame) == 80 && frame.parameters[1] == 0 && frame.parameters[2] == 0,
           "existing material frame prefix/offsets and disabled environment controls remain intact");
    std::cout << (failures ? "FAIL" : "PASS")
              << " sky environment CPU generation/contracts; GPU shading not executed\n";
    return failures ? 1 : 0;
}
