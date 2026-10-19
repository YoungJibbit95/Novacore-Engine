#include "SkyEnvironment.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace novacore::render::detail {
namespace {
using math::Vec3;
constexpr float pi = std::numbers::pi_v<float>;
float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3 cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
Vec3 unit(Vec3 v) { return v * (1.0F / std::sqrt(std::max(dot(v, v), 1.0e-20F))); }
Vec3 mix(Vec3 a, Vec3 b, float t) { return a * (1 - t) + b * t; }
Vec3 rgb(const std::array<float, 3>& c) { return {c[0], c[1], c[2]}; }
float smooth(float a, float b, float x) {
    const auto t = std::clamp((x - a) / (b - a), 0.0F, 1.0F);
    return t * t * (3 - 2 * t);
}
float radicalInverse(std::uint32_t bits) {
    bits = (bits << 16U) | (bits >> 16U);
    bits = ((bits & 0x55555555U) << 1U) | ((bits & 0xAAAAAAAAU) >> 1U);
    bits = ((bits & 0x33333333U) << 2U) | ((bits & 0xCCCCCCCCU) >> 2U);
    bits = ((bits & 0x0F0F0F0FU) << 4U) | ((bits & 0xF0F0F0F0U) >> 4U);
    bits = ((bits & 0x00FF00FFU) << 8U) | ((bits & 0xFF00FF00U) >> 8U);
    return static_cast<float>(bits) * 2.3283064365386963e-10F;
}
Vec3 importanceGgx(std::uint32_t i, std::uint32_t samples, float roughness) {
    const auto alpha = roughness * roughness;
    const auto phi = 2 * pi * static_cast<float>(i) / static_cast<float>(samples);
    const auto y = radicalInverse(i);
    const auto cosine = std::sqrt((1 - y) / (1 + (alpha * alpha - 1) * y));
    const auto sine = std::sqrt(std::max(0.0F, 1 - cosine * cosine));
    return {std::cos(phi) * sine, std::sin(phi) * sine, cosine};
}
Vec3 prefilter(const SkyRadianceSnapshot& sky, Vec3 normal, float roughness) {
    if (roughness == 0) {
        return skyRadiance(sky, normal);
    }
    const auto up = std::abs(normal.z) < .999F ? Vec3{0, 0, 1} : Vec3{1, 0, 0};
    const auto tangent = unit(cross(up, normal));
    const auto bitangent = cross(normal, tangent);
    Vec3 sum{};
    float weight = 0;
    for (std::uint32_t i = 0; i < skyPrefilterSamples; ++i) {
        const auto h = importanceGgx(i, skyPrefilterSamples, roughness);
        const auto half = unit(tangent * h.x + bitangent * h.y + normal * h.z);
        const auto light = unit(half * (2 * dot(normal, half)) - normal);
        const auto nl = std::max(dot(normal, light), 0.0F);
        if (nl > 0) {
            sum = sum + skyRadiance(sky, light) * nl;
            weight += nl;
        }
    }
    return weight > 0 ? sum * (1 / weight) : skyRadiance(sky, normal);
}
std::array<float, 2> integrateBrdf(float nv, float roughness) {
    roughness = std::max(.04F, roughness);
    const auto alpha = roughness * roughness;
    const auto a2 = alpha * alpha;
    const Vec3 view{std::sqrt(1 - nv * nv), 0, nv};
    float a = 0, b = 0;
    for (std::uint32_t i = 0; i < skyBrdfSamples; ++i) {
        const auto h = importanceGgx(i, skyBrdfSamples, roughness);
        const auto vh = std::max(dot(view, h), 0.0F);
        const auto light = unit(h * (2 * vh) - view);
        const auto nl = std::max(light.z, 0.0F);
        if (nl <= 0) {
            continue;
        }
        // Height-correlated Smith GGX, as in the opt-in direct-light BRDF.
        // G*VdotH/(NdotH*NdotV), algebraically cancelling NdotV.
        const auto sum = nv * std::sqrt(nl * nl * (1 - a2) + a2) + nl * std::sqrt(nv * nv * (1 - a2) + a2);
        const auto gVis = (2 * nl * vh) / std::max(h.z * sum, 1.0e-12F);
        const auto fc = std::pow(1 - vh, 5.0F);
        a += (1 - fc) * gVis;
        b += fc * gVis;
    }
    return {a / skyBrdfSamples, b / skyBrdfSamples};
}
} // namespace

std::optional<SkyRadianceSnapshot> skyRadianceSnapshot(const RenderSky& sky) {
    if (!sky.enabled) {
        return std::nullopt;
    }
    SkyRadianceSnapshot result{};
    for (std::size_t i = 0; i < 3; ++i) {
        for (const auto color : {sky.zenithColor[i], sky.horizonColor[i], sky.groundColor[i]}) {
            if (!std::isfinite(color) || color < 0 || color > 64) {
                return std::nullopt;
            }
        }
        result.zenith[i] = sky.zenithColor[i];
        result.horizon[i] = sky.horizonColor[i];
        result.ground[i] = sky.groundColor[i];
    }
    for (const auto value : {sky.horizonHeight, sky.gradientPower, sky.exposure, sky.hazeStrength, sky.cloudStrength}) {
        if (!std::isfinite(value)) {
            return std::nullopt;
        }
    }
    result.horizonHeight = std::clamp(sky.horizonHeight, .05F, .95F);
    result.gradientPower = std::clamp(sky.gradientPower, .2F, 5.0F);
    result.exposure = std::clamp(sky.exposure, 0.0F, 4.0F);
    result.haze = std::clamp(sky.hazeStrength, 0.0F, 1.0F);
    result.clouds = std::clamp(sky.cloudStrength, 0.0F, 1.0F);
    return result;
}
Vec3 skyRadiance(const SkyRadianceSnapshot& sky, Vec3 ray) {
    const auto vertical = std::clamp(ray.y * .5F + .5F, 0.0F, 1.0F);
    const auto h = sky.horizonHeight;
    const auto color = vertical >= h
                           ? mix(rgb(sky.horizon), rgb(sky.zenith),
                                 std::pow(std::clamp((vertical - h) / (1 - h), 0.0F, 1.0F), sky.gradientPower))
                           : mix(rgb(sky.horizon), rgb(sky.ground),
                                 std::pow(std::clamp(1 - vertical / h, 0.0F, 1.0F), sky.gradientPower));
    const auto haze = std::pow(1 - std::abs(ray.y), 5.0F) * sky.haze;
    const auto hazed = mix(color, rgb(sky.horizon) * 1.12F, haze);
    const auto noise = std::sin(ray.x * 31 + ray.z * 23) * std::sin(ray.x * 13 - ray.z * 17);
    const auto cloud = smooth(.28F, .84F, noise * .5F + .5F) * smooth(-.02F, .35F, ray.y) * sky.clouds * .32F;
    const auto radiance = mix(hazed, {.88F, .92F, .96F}, cloud) * sky.exposure;
    return {std::max(0.0F, radiance.x), std::max(0.0F, radiance.y), std::max(0.0F, radiance.z)};
}
Vec3 skyCubeDirection(std::uint32_t face, float s, float t) {
    switch (face) {
    case 0:
        return unit({1, -t, -s});
    case 1:
        return unit({-1, -t, s});
    case 2:
        return unit({s, 1, t});
    case 3:
        return unit({s, -1, -t});
    case 4:
        return unit({s, -t, 1});
    default:
        return unit({-s, -t, -1});
    }
}
std::size_t skyCubeFloatCount() {
    std::size_t count = 0;
    for (auto size = skyCubeSize; size; size /= 2) {
        count += static_cast<std::size_t>(size) * size * 6 * 4;
    }
    return count;
}
SkyEnvironmentPixels generateSkyEnvironment(const SkyRadianceSnapshot& sky, bool includeBrdf) {
    SkyEnvironmentPixels result;
    result.cube.reserve(skyCubeFloatCount());
    for (std::uint32_t level = 0, size = skyCubeSize; level < skyCubeMipLevels;
         ++level, size = std::max(1U, size / 2)) {
        const auto roughness = static_cast<float>(level) / static_cast<float>(skyCubeMipLevels - 1);
        for (std::uint32_t face = 0; face < 6; ++face) {
            for (std::uint32_t y = 0; y < size; ++y) {
                for (std::uint32_t x = 0; x < size; ++x) {
                    const auto ray =
                        skyCubeDirection(face, 2 * (static_cast<float>(x) + .5F) / static_cast<float>(size) - 1,
                                         2 * (static_cast<float>(y) + .5F) / static_cast<float>(size) - 1);
                    const auto color = prefilter(sky, ray, roughness);
                    result.cube.insert(result.cube.end(), {color.x, color.y, color.z, 1});
                }
            }
        }
    }
    if (includeBrdf) {
        result.brdf.reserve(skyBrdfSize * skyBrdfSize * 2);
        for (std::uint32_t y = 0; y < skyBrdfSize; ++y) {
            for (std::uint32_t x = 0; x < skyBrdfSize; ++x) {
                const auto value = integrateBrdf((static_cast<float>(x) + .5F) / skyBrdfSize,
                                                 (static_cast<float>(y) + .5F) / skyBrdfSize);
                result.brdf.insert(result.brdf.end(), value.begin(), value.end());
            }
        }
    }
    return result;
}
bool SkyEnvironmentGenerationGate::observe(const std::optional<SkyRadianceSnapshot>& source, Clock::time_point now) {
    if (source != observed) {
        observed = source;
        stableCount = source ? 1U : 0U;
    } else if (source) {
        stableCount = std::min(2U, stableCount + 1);
    }
    if (!source || stableCount < 2 || source == attempted ||
        (lastAttempt && now - *lastAttempt < std::chrono::seconds(2))) {
        return false;
    }
    attempted = source;
    lastAttempt = now;
    return true;
}
} // namespace novacore::render::detail
