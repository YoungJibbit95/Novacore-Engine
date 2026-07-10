#pragma once

#include "novacore/animation/AnimationTypes.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace novacore::animation {

struct Joint final {
    std::string name;
    std::int32_t parent = -1;
    Transform bindLocal{};
};

struct Socket final {
    std::string name;
    JointIndex joint = invalidJointIndex;
    Transform local{};
};

struct Skeleton final {
    std::string name;
    std::vector<Joint> joints;
    std::vector<Socket> sockets;
};

struct SkeletonValidationLimits final {
    std::size_t maxJoints = 4096U;
    std::size_t maxSockets = 1024U;
    std::size_t maxHierarchyDepth = 1024U;
};

[[nodiscard]] ValidationResult validateSkeleton(const Skeleton& skeleton, const SkeletonValidationLimits& limits = {});

[[nodiscard]] std::optional<JointIndex> findJoint(const Skeleton& skeleton, std::string_view name);
[[nodiscard]] std::optional<std::size_t> findSocket(const Skeleton& skeleton, std::string_view name);
[[nodiscard]] LocalPose makeBindPose(const Skeleton& skeleton);
[[nodiscard]] ValidationResult validatePose(const Skeleton& skeleton, const LocalPose& pose);
[[nodiscard]] bool evaluateGlobalPose(const Skeleton& skeleton, const LocalPose& localPose, GlobalPose& outGlobalPose,
                                      ValidationResult* validation = nullptr);
[[nodiscard]] bool evaluateSocket(const Skeleton& skeleton, const GlobalPose& globalPose, std::size_t socketIndex,
                                  Mat4& outTransform);
[[nodiscard]] bool evaluateSocket(const Skeleton& skeleton, const GlobalPose& globalPose, std::string_view socketName,
                                  Mat4& outTransform);

} // namespace novacore::animation
