#include "novacore/animation/AnimationRuntime.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>

namespace novacore::animation {

namespace {

constexpr std::size_t kMaxRuntimeLayers = 64U;
constexpr float kQuaternionEpsilon = 1.0e-12F;

[[nodiscard]] float sanitizedMaskWeight(float value, float fallback) {
    return std::isfinite(value) ? std::clamp(value, 0.0F, 1.0F) : fallback;
}

[[nodiscard]] bool blendable(const Transform& transform) {
    return isFinite(transform) && quaternionLengthSquared(transform.rotation) > kQuaternionEpsilon;
}

[[nodiscard]] RootMotionDelta composeRootMotion(const RootMotionDelta& lhs, const RootMotionDelta& rhs) {
    return {
        lhs.translation + rotate(lhs.rotation, rhs.translation),
        multiply(lhs.rotation, rhs.rotation),
    };
}

[[nodiscard]] RootMotionDelta inverseRootMotion(const RootMotionDelta& value) {
    const auto inverseRotation = conjugate(normalize(value.rotation));
    return {
        rotate(inverseRotation, value.translation * -1.0F),
        inverseRotation,
    };
}

[[nodiscard]] RootMotionDelta relativeRootMotion(const RootMotionDelta& from, const RootMotionDelta& to) {
    return composeRootMotion(inverseRootMotion(from), to);
}

[[nodiscard]] RootMotionDelta rootMotionPower(RootMotionDelta value, std::int64_t exponent) {
    if (exponent < 0) {
        value = inverseRootMotion(value);
    }
    std::uint64_t remaining =
        exponent < 0 ? static_cast<std::uint64_t>(-(exponent + 1)) + 1U : static_cast<std::uint64_t>(exponent);
    RootMotionDelta result{};
    while (remaining > 0U) {
        if ((remaining & 1U) != 0U) {
            result = composeRootMotion(result, value);
        }
        value = composeRootMotion(value, value);
        remaining >>= 1U;
    }
    return result;
}

[[nodiscard]] Transform sampleRootTransform(const AnimationClip& clip, const Skeleton& skeleton, JointIndex rootJoint,
                                            float time) {
    Transform root = skeleton.joints[rootJoint].bindLocal;
    for (const auto& track : clip.tracks) {
        if (track.joint != rootJoint) {
            continue;
        }
        root.translation = sampleVec3Track(track.translation, time, root.translation);
        root.rotation = sampleQuatTrack(track.rotation, time, root.rotation);
        break;
    }
    root.rotation = normalize(root.rotation);
    return root;
}

[[nodiscard]] RootMotionDelta asRootMotion(const Transform& transform) {
    return {transform.translation, normalize(transform.rotation)};
}

[[nodiscard]] RootMotionDelta extendedRootMotion(const AnimationClip& clip, const Skeleton& skeleton,
                                                 JointIndex rootJoint, float time) {
    const auto start = asRootMotion(sampleRootTransform(clip, skeleton, rootJoint, 0.0F));
    const auto end = asRootMotion(sampleRootTransform(clip, skeleton, rootJoint, clip.duration));
    const auto cycle = relativeRootMotion(start, end);

    const double duration = static_cast<double>(clip.duration);
    const double inputTime = static_cast<double>(time);
    double cyclesAsDouble = std::floor(inputTime / duration);
    constexpr double kCycleLimit = 1'000'000'000.0;
    cyclesAsDouble = std::clamp(cyclesAsDouble, -kCycleLimit, kCycleLimit);
    const auto cycles = static_cast<std::int64_t>(cyclesAsDouble);
    double phase = inputTime - (static_cast<double>(cycles) * duration);
    phase = std::clamp(phase, 0.0, duration);

    const auto phaseTransform = asRootMotion(sampleRootTransform(clip, skeleton, rootJoint, static_cast<float>(phase)));
    return composeRootMotion(rootMotionPower(cycle, cycles), relativeRootMotion(start, phaseTransform));
}

[[nodiscard]] RootMotionDelta extractValidatedRootMotionDelta(const AnimationClip& clip, const Skeleton& skeleton,
                                                              JointIndex rootJoint, float previousTime,
                                                              float currentTime, WrapMode wrapMode) {
    RootMotionDelta previous;
    RootMotionDelta current;
    if (wrapMode == WrapMode::Loop) {
        previous = extendedRootMotion(clip, skeleton, rootJoint, previousTime);
        current = extendedRootMotion(clip, skeleton, rootJoint, currentTime);
    } else {
        const float previousSample = normalizeSampleTime(previousTime, clip.duration, WrapMode::Clamp);
        const float currentSample = normalizeSampleTime(currentTime, clip.duration, WrapMode::Clamp);
        previous = asRootMotion(sampleRootTransform(clip, skeleton, rootJoint, previousSample));
        current = asRootMotion(sampleRootTransform(clip, skeleton, rootJoint, currentSample));
    }
    return relativeRootMotion(previous, current);
}

[[nodiscard]] bool sampleValidatedPlayback(const Skeleton& skeleton, const AnimationClip* clip, float time,
                                           WrapMode wrapMode, LocalPose& outPose) {
    if (clip == nullptr) {
        return false;
    }
    outPose = makeBindPose(skeleton);
    const float sampleTime = normalizeSampleTime(time, clip->duration, wrapMode);
    for (const auto& track : clip->tracks) {
        auto& joint = outPose.joints[track.joint];
        joint.translation = sampleVec3Track(track.translation, sampleTime, joint.translation);
        joint.rotation = sampleQuatTrack(track.rotation, sampleTime, joint.rotation);
        joint.scale = sampleVec3Track(track.scale, sampleTime, joint.scale);
    }
    return true;
}

void compactPlaybackTime(const AnimationClip* clip, float& time, WrapMode wrapMode) {
    if (clip == nullptr) {
        return;
    }
    time = normalizeSampleTime(time, clip->duration, wrapMode);
}

[[nodiscard]] std::string firstError(const ValidationResult& validation) {
    for (const auto& issue : validation.issues) {
        if (issue.severity == ValidationSeverity::Error) {
            return issue.path.empty() ? issue.message : issue.path + ": " + issue.message;
        }
    }
    return {};
}

} // namespace

float LayerMask::weight(JointIndex joint) const {
    if (weights.empty()) {
        return 1.0F;
    }
    return static_cast<std::size_t>(joint) < weights.size() ? weights[joint] : 0.0F;
}

ValidationResult validateLayerMask(const LayerMask& mask, const Skeleton& skeleton) {
    ValidationResult result;
    if (!mask.weights.empty() && mask.weights.size() != skeleton.joints.size()) {
        result.addError("weights", "layer mask must be empty or contain one weight per joint");
        return result;
    }
    for (std::size_t index = 0; index < mask.weights.size(); ++index) {
        if (!std::isfinite(mask.weights[index]) || mask.weights[index] < 0.0F || mask.weights[index] > 1.0F) {
            result.addError("weights[" + std::to_string(index) + "]", "layer weight must be finite and inside [0, 1]");
        }
    }
    return result;
}

LayerMask makeDescendantMask(const Skeleton& skeleton, JointIndex root, float includedWeight, float excludedWeight) {
    LayerMask mask;
    mask.weights.assign(skeleton.joints.size(), sanitizedMaskWeight(excludedWeight, 0.0F));
    if (static_cast<std::size_t>(root) >= skeleton.joints.size()) {
        return mask;
    }
    includedWeight = sanitizedMaskWeight(includedWeight, 1.0F);
    mask.weights[root] = includedWeight;
    for (std::size_t index = 0; index < skeleton.joints.size(); ++index) {
        std::int32_t parent = static_cast<std::int32_t>(index);
        std::size_t traversed = 0U;
        while (parent >= 0 && traversed++ <= skeleton.joints.size()) {
            if (static_cast<JointIndex>(parent) == root) {
                mask.weights[index] = includedWeight;
                break;
            }
            if (static_cast<std::size_t>(parent) >= skeleton.joints.size()) {
                break;
            }
            parent = skeleton.joints[static_cast<std::size_t>(parent)].parent;
        }
    }
    return mask;
}

bool blendPoses(const LocalPose& from, const LocalPose& to, float weight, LocalPose& outPose, const LayerMask* mask) {
    if (from.joints.size() != to.joints.size() || !std::isfinite(weight)) {
        return false;
    }
    if (mask != nullptr && !mask->weights.empty() && mask->weights.size() != from.joints.size()) {
        return false;
    }

    for (std::size_t index = 0; index < from.joints.size(); ++index) {
        if (!blendable(from.joints[index]) || !blendable(to.joints[index])) {
            return false;
        }
        if (mask != nullptr && !mask->weights.empty()) {
            const float maskWeight = mask->weights[index];
            if (!std::isfinite(maskWeight) || maskWeight < 0.0F || maskWeight > 1.0F) {
                return false;
            }
        }
    }

    LocalPose blended;
    blended.joints.resize(from.joints.size());
    const float baseWeight = std::clamp(weight, 0.0F, 1.0F);
    for (std::size_t index = 0; index < from.joints.size(); ++index) {
        const float jointWeight =
            mask == nullptr ? baseWeight : baseWeight * mask->weight(static_cast<JointIndex>(index));
        blended.joints[index] = blend(from.joints[index], to.joints[index], jointWeight);
    }
    outPose = std::move(blended);
    return true;
}

RootMotionDelta extractRootMotionDelta(const AnimationClip& clip, const Skeleton& skeleton, JointIndex rootJoint,
                                       float previousTime, float currentTime, WrapMode wrapMode) {
    if (static_cast<std::size_t>(rootJoint) >= skeleton.joints.size() || !std::isfinite(previousTime) ||
        !std::isfinite(currentTime) || !std::isfinite(clip.duration) || clip.duration <= 0.0F) {
        return {};
    }
    if (!validateClip(clip, skeleton).valid()) {
        return {};
    }
    return extractValidatedRootMotionDelta(clip, skeleton, rootJoint, previousTime, currentTime, wrapMode);
}

ValidationResult AnimationRuntime::setSkeleton(const Skeleton& skeleton) {
    auto result = validateSkeleton(skeleton);
    if (!result.valid()) {
        lastError_ = firstError(result);
        return result;
    }

    skeleton_ = &skeleton;
    base_ = {};
    transition_.reset();
    layers_.clear();
    rootMotionJoint_.reset();
    for (std::size_t index = 0; index < skeleton.joints.size(); ++index) {
        if (skeleton.joints[index].parent < 0) {
            rootMotionJoint_ = static_cast<JointIndex>(index);
            break;
        }
    }
    localPose_ = makeBindPose(skeleton);
    if (!evaluateGlobalPose(skeleton, localPose_, globalPose_)) {
        result.addError("skeleton", "bind pose could not be evaluated");
        lastError_ = firstError(result);
        skeleton_ = nullptr;
        return result;
    }
    rootMotionDelta_ = {};
    lastError_.clear();
    return result;
}

ValidationResult AnimationRuntime::play(const AnimationClip& clip, WrapMode wrapMode, float startTime,
                                        float playbackRate) {
    ValidationResult result;
    if (skeleton_ == nullptr) {
        result.addError("skeleton", "runtime requires a valid skeleton before playing a clip");
    } else {
        result.append(validateClip(clip, *skeleton_), "clip");
    }
    if (!std::isfinite(startTime)) {
        result.addError("startTime", "start time must be finite");
    }
    if (!std::isfinite(playbackRate)) {
        result.addError("playbackRate", "playback rate must be finite");
    }
    if (!result.valid()) {
        lastError_ = firstError(result);
        return result;
    }

    base_ = {&clip, startTime, playbackRate, wrapMode};
    transition_.reset();
    rootMotionDelta_ = {};
    if (!rebuildPose()) {
        result.addError("clip", lastError_);
        return result;
    }
    lastError_.clear();
    return result;
}

ValidationResult AnimationRuntime::crossFade(const AnimationClip& clip, float durationSeconds, WrapMode wrapMode,
                                             float startTime, float playbackRate) {
    ValidationResult result;
    if (skeleton_ == nullptr || base_.clip == nullptr) {
        result.addError("runtime", "crossfade requires an active skeleton and base clip");
    } else {
        result.append(validateClip(clip, *skeleton_), "clip");
    }
    if (transition_.has_value()) {
        result.addError("transition", "an active crossfade must finish before starting another");
    }
    if (!std::isfinite(durationSeconds) || durationSeconds <= 0.0F) {
        result.addError("durationSeconds", "crossfade duration must be finite and greater than zero");
    }
    if (!std::isfinite(startTime)) {
        result.addError("startTime", "start time must be finite");
    }
    if (!std::isfinite(playbackRate)) {
        result.addError("playbackRate", "playback rate must be finite");
    }
    if (!result.valid()) {
        lastError_ = firstError(result);
        return result;
    }

    transition_ = Transition{
        base_,
        Playback{&clip, startTime, playbackRate, wrapMode},
        0.0F,
        durationSeconds,
    };
    rootMotionDelta_ = {};
    lastError_.clear();
    return result;
}

ValidationResult AnimationRuntime::setLayer(std::size_t slot, const AnimationLayerDesc& layer) {
    ValidationResult result;
    if (skeleton_ == nullptr) {
        result.addError("skeleton", "runtime requires a valid skeleton before adding a layer");
    } else {
        result.append(validateLayerMask(layer.mask, *skeleton_), "mask");
        if (layer.clip == nullptr) {
            result.addError("clip", "layer clip must not be null");
        } else {
            result.append(validateClip(*layer.clip, *skeleton_), "clip");
        }
    }
    if (slot >= kMaxRuntimeLayers) {
        result.addError("slot", "layer slot exceeds the runtime layer limit");
    }
    if (!std::isfinite(layer.time)) {
        result.addError("time", "layer time must be finite");
    }
    if (!std::isfinite(layer.playbackRate)) {
        result.addError("playbackRate", "layer playback rate must be finite");
    }
    if (!std::isfinite(layer.weight) || layer.weight < 0.0F || layer.weight > 1.0F) {
        result.addError("weight", "layer weight must be finite and inside [0, 1]");
    }
    if (!result.valid()) {
        lastError_ = firstError(result);
        return result;
    }

    if (layers_.size() <= slot) {
        layers_.resize(slot + 1U);
    }
    layers_[slot] = layer;
    if (base_.clip != nullptr && !rebuildPose()) {
        result.addError("layer", lastError_);
        return result;
    }
    lastError_.clear();
    return result;
}

void AnimationRuntime::clearLayer(std::size_t slot) {
    if (slot < layers_.size()) {
        layers_[slot].reset();
        if (base_.clip != nullptr) {
            static_cast<void>(rebuildPose());
        }
    }
}

void AnimationRuntime::clearLayers() {
    layers_.clear();
    if (base_.clip != nullptr) {
        static_cast<void>(rebuildPose());
    }
}

ValidationResult AnimationRuntime::setRootMotionJoint(std::optional<JointIndex> joint) {
    ValidationResult result;
    if (joint.has_value() && (skeleton_ == nullptr || static_cast<std::size_t>(*joint) >= skeleton_->joints.size())) {
        result.addError("joint", "root motion joint index is outside the active skeleton");
        lastError_ = firstError(result);
        return result;
    }
    rootMotionJoint_ = joint;
    rootMotionDelta_ = {};
    lastError_.clear();
    return result;
}

bool AnimationRuntime::update(float deltaSeconds) {
    if (skeleton_ == nullptr || base_.clip == nullptr) {
        lastError_ = "runtime has no active skeleton and clip";
        return false;
    }
    if (!std::isfinite(deltaSeconds) || deltaSeconds < 0.0F) {
        lastError_ = "delta seconds must be finite and non-negative";
        return false;
    }

    rootMotionDelta_ = {};
    if (transition_.has_value()) {
        auto& transition = *transition_;
        const float previousFromTime = transition.from.time;
        const float previousToTime = transition.to.time;
        transition.from.time += deltaSeconds * transition.from.playbackRate;
        transition.to.time += deltaSeconds * transition.to.playbackRate;
        transition.elapsed = std::min(transition.elapsed + deltaSeconds, transition.duration);
        const float transitionWeight = transition.elapsed / transition.duration;
        if (rootMotionJoint_.has_value()) {
            const auto fromDelta =
                extractValidatedRootMotionDelta(*transition.from.clip, *skeleton_, *rootMotionJoint_, previousFromTime,
                                                transition.from.time, transition.from.wrapMode);
            const auto toDelta =
                extractValidatedRootMotionDelta(*transition.to.clip, *skeleton_, *rootMotionJoint_, previousToTime,
                                                transition.to.time, transition.to.wrapMode);
            rootMotionDelta_ = {
                lerp(fromDelta.translation, toDelta.translation, transitionWeight),
                slerp(fromDelta.rotation, toDelta.rotation, transitionWeight),
            };
        }
    } else {
        const float previousTime = base_.time;
        base_.time += deltaSeconds * base_.playbackRate;
        if (rootMotionJoint_.has_value()) {
            rootMotionDelta_ = extractValidatedRootMotionDelta(*base_.clip, *skeleton_, *rootMotionJoint_, previousTime,
                                                               base_.time, base_.wrapMode);
        }
    }

    for (auto& layer : layers_) {
        if (layer.has_value()) {
            layer->time += deltaSeconds * layer->playbackRate;
        }
    }

    if (!rebuildPose()) {
        return false;
    }

    if (transition_.has_value() && transition_->elapsed >= transition_->duration) {
        base_ = transition_->to;
        transition_.reset();
    }
    compactPlaybackTime(base_.clip, base_.time, base_.wrapMode);
    if (transition_.has_value()) {
        compactPlaybackTime(transition_->from.clip, transition_->from.time, transition_->from.wrapMode);
        compactPlaybackTime(transition_->to.clip, transition_->to.time, transition_->to.wrapMode);
    }
    for (auto& layer : layers_) {
        if (layer.has_value() && layer->clip != nullptr) {
            layer->time = normalizeSampleTime(layer->time, layer->clip->duration, layer->wrapMode);
        }
    }
    lastError_.clear();
    return true;
}

bool AnimationRuntime::socketTransform(std::string_view name, Mat4& outTransform) const {
    return skeleton_ != nullptr && evaluateSocket(*skeleton_, globalPose_, name, outTransform);
}

bool AnimationRuntime::rebuildPose() {
    if (skeleton_ == nullptr || base_.clip == nullptr) {
        lastError_ = "runtime has no active skeleton and clip";
        return false;
    }

    LocalPose evaluated;
    if (transition_.has_value()) {
        LocalPose fromPose;
        LocalPose toPose;
        if (!sampleValidatedPlayback(*skeleton_, transition_->from.clip, transition_->from.time,
                                     transition_->from.wrapMode, fromPose) ||
            !sampleValidatedPlayback(*skeleton_, transition_->to.clip, transition_->to.time, transition_->to.wrapMode,
                                     toPose)) {
            lastError_ = "crossfade clip could not be sampled";
            return false;
        }
        const float transitionWeight = std::clamp(transition_->elapsed / transition_->duration, 0.0F, 1.0F);
        if (!blendPoses(fromPose, toPose, transitionWeight, evaluated)) {
            lastError_ = "crossfade poses are incompatible";
            return false;
        }
    } else if (!sampleValidatedPlayback(*skeleton_, base_.clip, base_.time, base_.wrapMode, evaluated)) {
        lastError_ = "base clip could not be sampled";
        return false;
    }

    for (const auto& layer : layers_) {
        if (!layer.has_value() || layer->clip == nullptr || layer->weight <= 0.0F) {
            continue;
        }
        const Playback playback{layer->clip, layer->time, layer->playbackRate, layer->wrapMode};
        LocalPose layerPose;
        LocalPose blended;
        if (!sampleValidatedPlayback(*skeleton_, playback.clip, playback.time, playback.wrapMode, layerPose) ||
            !blendPoses(evaluated, layerPose, layer->weight, blended, &layer->mask)) {
            lastError_ = "animation layer could not be evaluated";
            return false;
        }
        evaluated = std::move(blended);
    }

    GlobalPose global;
    ValidationResult validation;
    if (!evaluateGlobalPose(*skeleton_, evaluated, global, &validation)) {
        lastError_ = firstError(validation);
        return false;
    }
    localPose_ = std::move(evaluated);
    globalPose_ = std::move(global);
    return true;
}

} // namespace novacore::animation
