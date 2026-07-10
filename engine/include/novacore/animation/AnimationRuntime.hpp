#pragma once

#include "novacore/animation/AnimationClip.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace novacore::animation {

struct LayerMask final {
    std::vector<float> weights;

    [[nodiscard]] float weight(JointIndex joint) const;
};

struct RootMotionDelta final {
    math::Vec3 translation{};
    math::Quat rotation{};
};

[[nodiscard]] ValidationResult validateLayerMask(const LayerMask& mask, const Skeleton& skeleton);
[[nodiscard]] LayerMask makeDescendantMask(const Skeleton& skeleton, JointIndex root, float includedWeight = 1.0F,
                                           float excludedWeight = 0.0F);
[[nodiscard]] bool blendPoses(const LocalPose& from, const LocalPose& to, float weight, LocalPose& outPose,
                              const LayerMask* mask = nullptr);
[[nodiscard]] RootMotionDelta extractRootMotionDelta(const AnimationClip& clip, const Skeleton& skeleton,
                                                     JointIndex rootJoint, float previousTime, float currentTime,
                                                     WrapMode wrapMode);

struct AnimationLayerDesc final {
    const AnimationClip* clip = nullptr;
    LayerMask mask;
    float time = 0.0F;
    float playbackRate = 1.0F;
    float weight = 1.0F;
    WrapMode wrapMode = WrapMode::Loop;
};

class AnimationRuntime final {
  public:
    [[nodiscard]] ValidationResult setSkeleton(const Skeleton& skeleton);
    [[nodiscard]] ValidationResult play(const AnimationClip& clip, WrapMode wrapMode = WrapMode::Loop,
                                        float startTime = 0.0F, float playbackRate = 1.0F);
    [[nodiscard]] ValidationResult crossFade(const AnimationClip& clip, float durationSeconds,
                                             WrapMode wrapMode = WrapMode::Loop, float startTime = 0.0F,
                                             float playbackRate = 1.0F);
    [[nodiscard]] ValidationResult setLayer(std::size_t slot, const AnimationLayerDesc& layer);
    void clearLayer(std::size_t slot);
    void clearLayers();
    [[nodiscard]] ValidationResult setRootMotionJoint(std::optional<JointIndex> joint);

    [[nodiscard]] bool update(float deltaSeconds);
    [[nodiscard]] const LocalPose& localPose() const { return localPose_; }
    [[nodiscard]] const GlobalPose& globalPose() const { return globalPose_; }
    [[nodiscard]] const RootMotionDelta& rootMotionDelta() const { return rootMotionDelta_; }
    [[nodiscard]] bool socketTransform(std::string_view name, Mat4& outTransform) const;
    [[nodiscard]] bool transitioning() const { return transition_.has_value(); }
    [[nodiscard]] std::string_view lastError() const { return lastError_; }

  private:
    struct Playback final {
        const AnimationClip* clip = nullptr;
        float time = 0.0F;
        float playbackRate = 1.0F;
        WrapMode wrapMode = WrapMode::Loop;
    };

    struct Transition final {
        Playback from;
        Playback to;
        float elapsed = 0.0F;
        float duration = 0.0F;
    };

    [[nodiscard]] bool rebuildPose();

    const Skeleton* skeleton_ = nullptr;
    Playback base_{};
    std::optional<Transition> transition_;
    std::vector<std::optional<AnimationLayerDesc>> layers_;
    std::optional<JointIndex> rootMotionJoint_;
    LocalPose localPose_;
    GlobalPose globalPose_;
    RootMotionDelta rootMotionDelta_{};
    std::string lastError_;
};

} // namespace novacore::animation
