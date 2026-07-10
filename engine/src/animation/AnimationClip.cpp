#include "novacore/animation/AnimationClip.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_set>
#include <utility>

namespace novacore::animation {

namespace {

constexpr float kQuaternionEpsilon = 1.0e-12F;
constexpr float kScaleEpsilon = 1.0e-8F;

template <typename Key>
void validateKeyTimes(std::span<const Key> keys, float duration, std::size_t maxKeys, std::string_view path,
                      ValidationResult& result) {
    if (keys.size() > maxKeys) {
        result.addError(std::string(path), "key count exceeds the configured validation limit");
    }
    float previousTime = -1.0F;
    for (std::size_t keyIndex = 0; keyIndex < keys.size(); ++keyIndex) {
        const float time = keys[keyIndex].time;
        const auto keyPath = std::string(path) + ".keys[" + std::to_string(keyIndex) + "]";
        if (!std::isfinite(time)) {
            result.addError(keyPath + ".time", "key time must be finite");
            continue;
        }
        if (time < 0.0F || time > duration) {
            result.addError(keyPath + ".time", "key time must be inside the clip duration");
        }
        if (keyIndex > 0U && time <= previousTime) {
            result.addError(keyPath + ".time", "key times must be strictly increasing");
        }
        previousTime = time;
    }
}

void validateVec3Track(const Vec3Track& track, float duration, std::size_t maxKeys, std::string_view path,
                       bool scaleTrack, ValidationResult& result) {
    validateKeyTimes<Vec3Key>(track.keys, duration, maxKeys, path, result);
    for (std::size_t keyIndex = 0; keyIndex < track.keys.size(); ++keyIndex) {
        const auto& value = track.keys[keyIndex].value;
        const auto keyPath = std::string(path) + ".keys[" + std::to_string(keyIndex) + "].value";
        if (!isFinite(value)) {
            result.addError(keyPath, "key value must be finite");
        } else if (scaleTrack && (std::abs(value.x) <= kScaleEpsilon || std::abs(value.y) <= kScaleEpsilon ||
                                  std::abs(value.z) <= kScaleEpsilon)) {
            result.addError(keyPath, "scale key components must be non-zero");
        }
    }
}

void validateQuatTrack(const QuatTrack& track, float duration, std::size_t maxKeys, std::string_view path,
                       ValidationResult& result) {
    validateKeyTimes<QuatKey>(track.keys, duration, maxKeys, path, result);
    for (std::size_t keyIndex = 0; keyIndex < track.keys.size(); ++keyIndex) {
        const auto& value = track.keys[keyIndex].value;
        const auto keyPath = std::string(path) + ".keys[" + std::to_string(keyIndex) + "].value";
        if (!isFinite(value)) {
            result.addError(keyPath, "quaternion key must be finite");
            continue;
        }
        const float lengthSquared = quaternionLengthSquared(value);
        if (lengthSquared <= kQuaternionEpsilon) {
            result.addError(keyPath, "quaternion key has zero length");
        } else if (std::abs(lengthSquared - 1.0F) > 1.0e-3F) {
            result.addWarning(keyPath, "quaternion key will be normalized while sampling");
        }
    }
}

template <typename Key> [[nodiscard]] std::size_t upperKeyIndex(std::span<const Key> keys, float time) {
    const auto upper = std::ranges::upper_bound(keys, time, {}, &Key::time);
    return static_cast<std::size_t>(std::distance(keys.begin(), upper));
}

} // namespace

ValidationResult validateClip(const AnimationClip& clip, const Skeleton& skeleton, const ClipValidationLimits& limits) {
    ValidationResult result;
    result.append(validateSkeleton(skeleton), "skeleton");
    if (clip.name.empty()) {
        result.addWarning("name", "clip name is empty");
    }
    if (!std::isfinite(clip.duration) || clip.duration <= 0.0F) {
        result.addError("duration", "clip duration must be finite and greater than zero");
    } else if (clip.duration > limits.maxDurationSeconds) {
        result.addError("duration", "clip duration exceeds the configured validation limit");
    }
    if (clip.tracks.size() > limits.maxTracks) {
        result.addError("tracks", "track count exceeds the configured validation limit");
    }

    std::unordered_set<JointIndex> trackedJoints;
    trackedJoints.reserve(clip.tracks.size());
    for (std::size_t trackIndex = 0; trackIndex < clip.tracks.size(); ++trackIndex) {
        const auto& track = clip.tracks[trackIndex];
        const auto path = "tracks[" + std::to_string(trackIndex) + "]";
        if (static_cast<std::size_t>(track.joint) >= skeleton.joints.size()) {
            result.addError(path + ".joint", "track joint index is outside the skeleton");
        } else if (!trackedJoints.insert(track.joint).second) {
            result.addError(path + ".joint", "only one track per joint is allowed");
        }
        if (track.empty()) {
            result.addError(path, "joint track must contain at least one animated channel");
        }
        validateVec3Track(track.translation, clip.duration, limits.maxKeysPerChannel, path + ".translation", false,
                          result);
        validateQuatTrack(track.rotation, clip.duration, limits.maxKeysPerChannel, path + ".rotation", result);
        validateVec3Track(track.scale, clip.duration, limits.maxKeysPerChannel, path + ".scale", true, result);
    }
    return result;
}

float normalizeSampleTime(float time, float duration, WrapMode wrapMode) {
    if (!std::isfinite(time) || !std::isfinite(duration) || duration <= 0.0F) {
        return 0.0F;
    }
    if (wrapMode == WrapMode::Clamp) {
        return std::clamp(time, 0.0F, duration);
    }
    float wrapped = std::fmod(time, duration);
    if (wrapped < 0.0F) {
        wrapped += duration;
    }
    return wrapped;
}

math::Vec3 sampleVec3Track(const Vec3Track& track, float time, math::Vec3 fallback) {
    if (track.keys.empty()) {
        return fallback;
    }
    if (track.keys.size() == 1U || time <= track.keys.front().time) {
        return track.keys.front().value;
    }
    if (time >= track.keys.back().time) {
        return track.keys.back().value;
    }

    const auto rightIndex = upperKeyIndex<Vec3Key>(track.keys, time);
    const auto& left = track.keys[rightIndex - 1U];
    const auto& right = track.keys[rightIndex];
    if (track.interpolation == InterpolationMode::Step) {
        return left.value;
    }
    const float interval = right.time - left.time;
    const float weight = interval > 0.0F ? (time - left.time) / interval : 0.0F;
    return lerp(left.value, right.value, weight);
}

math::Quat sampleQuatTrack(const QuatTrack& track, float time, math::Quat fallback) {
    if (track.keys.empty()) {
        return normalize(fallback);
    }
    if (track.keys.size() == 1U || time <= track.keys.front().time) {
        return normalize(track.keys.front().value);
    }
    if (time >= track.keys.back().time) {
        return normalize(track.keys.back().value);
    }

    const auto rightIndex = upperKeyIndex<QuatKey>(track.keys, time);
    const auto& left = track.keys[rightIndex - 1U];
    const auto& right = track.keys[rightIndex];
    if (track.interpolation == InterpolationMode::Step) {
        return normalize(left.value);
    }
    const float interval = right.time - left.time;
    const float weight = interval > 0.0F ? (time - left.time) / interval : 0.0F;
    return slerp(left.value, right.value, weight);
}

bool sampleClip(const AnimationClip& clip, const Skeleton& skeleton, float time, WrapMode wrapMode, LocalPose& outPose,
                ValidationResult* validation) {
    auto result = validateClip(clip, skeleton);
    if (!std::isfinite(time)) {
        result.addError("time", "sample time must be finite");
    }
    if (!result.valid()) {
        if (validation != nullptr) {
            *validation = std::move(result);
        }
        return false;
    }

    LocalPose pose = makeBindPose(skeleton);
    const float sampleTime = normalizeSampleTime(time, clip.duration, wrapMode);
    for (const auto& track : clip.tracks) {
        auto& joint = pose.joints[track.joint];
        joint.translation = sampleVec3Track(track.translation, sampleTime, joint.translation);
        joint.rotation = sampleQuatTrack(track.rotation, sampleTime, joint.rotation);
        joint.scale = sampleVec3Track(track.scale, sampleTime, joint.scale);
    }
    outPose = std::move(pose);
    if (validation != nullptr) {
        *validation = std::move(result);
    }
    return true;
}

} // namespace novacore::animation
