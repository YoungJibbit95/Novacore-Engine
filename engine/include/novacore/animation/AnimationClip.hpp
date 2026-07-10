#pragma once

#include "novacore/animation/Skeleton.hpp"

#include <span>
#include <string>
#include <vector>

namespace novacore::animation {

enum class InterpolationMode {
    Step,
    Linear,
};

enum class WrapMode {
    Clamp,
    Loop,
};

struct Vec3Key final {
    float time = 0.0F;
    math::Vec3 value{};
};

struct QuatKey final {
    float time = 0.0F;
    math::Quat value{};
};

struct Vec3Track final {
    InterpolationMode interpolation = InterpolationMode::Linear;
    std::vector<Vec3Key> keys;
};

struct QuatTrack final {
    InterpolationMode interpolation = InterpolationMode::Linear;
    std::vector<QuatKey> keys;
};

struct JointTrack final {
    JointIndex joint = invalidJointIndex;
    Vec3Track translation;
    QuatTrack rotation;
    Vec3Track scale;

    JointTrack() = default;
    explicit JointTrack(JointIndex jointIndex) : joint(jointIndex) {}

    [[nodiscard]] bool empty() const { return translation.keys.empty() && rotation.keys.empty() && scale.keys.empty(); }
};

struct AnimationClip final {
    std::string name;
    float duration = 0.0F;
    std::vector<JointTrack> tracks;
};

struct ClipValidationLimits final {
    std::size_t maxTracks = 4096U;
    std::size_t maxKeysPerChannel = 1'000'000U;
    float maxDurationSeconds = 86'400.0F;
};

[[nodiscard]] ValidationResult validateClip(const AnimationClip& clip, const Skeleton& skeleton,
                                            const ClipValidationLimits& limits = {});
[[nodiscard]] float normalizeSampleTime(float time, float duration, WrapMode wrapMode);
[[nodiscard]] math::Vec3 sampleVec3Track(const Vec3Track& track, float time, math::Vec3 fallback);
[[nodiscard]] math::Quat sampleQuatTrack(const QuatTrack& track, float time, math::Quat fallback);
[[nodiscard]] bool sampleClip(const AnimationClip& clip, const Skeleton& skeleton, float time, WrapMode wrapMode,
                              LocalPose& outPose, ValidationResult* validation = nullptr);

} // namespace novacore::animation
