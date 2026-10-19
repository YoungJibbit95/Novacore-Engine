#pragma once

#include "novacore/render/Renderer.hpp"
#include <chrono>
#include <optional>
#include <vector>

namespace novacore::render::detail {

// Immutable, pre-display linear radiance. No camera, direct solar disc/glow,
// or display tone-map inputs. Exposure multiplies radiance exactly once.
struct SkyRadianceSnapshot final {
    std::array<float, 3> zenith{}, horizon{}, ground{};
    float horizonHeight{}, gradientPower{}, exposure{}, haze{}, clouds{};
    bool operator==(const SkyRadianceSnapshot&) const = default;
};
[[nodiscard]] std::optional<SkyRadianceSnapshot> skyRadianceSnapshot(const RenderSky& sky);
[[nodiscard]] math::Vec3 skyRadiance(const SkyRadianceSnapshot& sky, math::Vec3 unitDirection);
// Vulkan cube layer order +X,-X,+Y,-Y,+Z,-Z, with texel t increasing downward.
[[nodiscard]] math::Vec3 skyCubeDirection(std::uint32_t face, float s, float t);

inline constexpr std::uint32_t skyCubeSize = 128;
inline constexpr std::uint32_t skyCubeMipLevels = 8;
inline constexpr std::uint32_t skyPrefilterSamples = 64;
inline constexpr std::uint32_t skyBrdfSize = 64;
inline constexpr std::uint32_t skyBrdfSamples = 128;
struct SkyEnvironmentPixels final {
    // Mip-major, face-major, row-major RGBA32F. Each face is tightly packed.
    std::vector<float> cube;
    // Row-major RG32F: x=NdotV, y=perceptual roughness; split-sum A,B.
    std::vector<float> brdf;
};
[[nodiscard]] std::size_t skyCubeFloatCount();
[[nodiscard]] SkyEnvironmentPixels generateSkyEnvironment(const SkyRadianceSnapshot& sky, bool includeBrdf);

// A source must remain equal for two prepares. Failed attempts also consume the
// two-second interval; an animated/toggling source cannot trigger per-frame work.
// Previous maps may only be used when their immutable snapshot matches exactly.
struct SkyEnvironmentGenerationGate final {
    using Clock = std::chrono::steady_clock;
    std::optional<SkyRadianceSnapshot> observed, attempted;
    std::uint32_t stableCount = 0;
    std::optional<Clock::time_point> lastAttempt;
    bool observe(const std::optional<SkyRadianceSnapshot>& source, Clock::time_point now);
};

} // namespace novacore::render::detail
