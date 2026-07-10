#include "novacore/animation/Skeleton.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace novacore::animation {

namespace {

constexpr float kScaleEpsilon = 1.0e-8F;
constexpr float kQuaternionEpsilon = 1.0e-12F;

[[nodiscard]] bool finiteMatrix(const Mat4& matrix) {
    return std::ranges::all_of(matrix.values, [](float value) { return std::isfinite(value); });
}

void validateTransform(const Transform& transform, std::string path, ValidationResult& result) {
    if (!isFinite(transform)) {
        result.addError(std::move(path), "transform contains a non-finite component");
        return;
    }
    if (quaternionLengthSquared(transform.rotation) <= kQuaternionEpsilon) {
        result.addError(path + ".rotation", "quaternion has zero length");
    }
    if (std::abs(transform.scale.x) <= kScaleEpsilon || std::abs(transform.scale.y) <= kScaleEpsilon ||
        std::abs(transform.scale.z) <= kScaleEpsilon) {
        result.addError(path + ".scale", "scale components must be non-zero");
    }
}

} // namespace

ValidationResult validateSkeleton(const Skeleton& skeleton, const SkeletonValidationLimits& limits) {
    ValidationResult result;
    if (skeleton.joints.empty()) {
        result.addError("joints", "skeleton must contain at least one joint");
        return result;
    }
    if (skeleton.joints.size() > limits.maxJoints) {
        result.addError("joints", "joint count exceeds the configured validation limit");
    }
    if (skeleton.sockets.size() > limits.maxSockets) {
        result.addError("sockets", "socket count exceeds the configured validation limit");
    }

    std::unordered_map<std::string_view, JointIndex> jointNames;
    jointNames.reserve(skeleton.joints.size());
    for (std::size_t index = 0; index < skeleton.joints.size(); ++index) {
        const auto& joint = skeleton.joints[index];
        const auto path = "joints[" + std::to_string(index) + "]";
        if (joint.name.empty()) {
            result.addError(path + ".name", "joint name must not be empty");
        } else if (!jointNames.emplace(joint.name, static_cast<JointIndex>(index)).second) {
            result.addError(path + ".name", "joint name must be unique");
        }
        if (joint.parent < -1 ||
            (joint.parent >= 0 && static_cast<std::size_t>(joint.parent) >= skeleton.joints.size())) {
            result.addError(path + ".parent", "parent index is outside the skeleton");
        }
        if (joint.parent == static_cast<std::int32_t>(index)) {
            result.addError(path + ".parent", "joint cannot parent itself");
        }
        validateTransform(joint.bindLocal, path + ".bindLocal", result);
    }

    bool cycleReported = false;
    bool depthReported = false;
    for (std::size_t start = 0; start < skeleton.joints.size(); ++start) {
        std::unordered_set<std::size_t> path;
        std::size_t cursor = start;
        std::size_t depth = 0U;
        while (cursor < skeleton.joints.size()) {
            if (!path.insert(cursor).second) {
                if (!cycleReported) {
                    result.addError("joints[" + std::to_string(start) + "].parent", "hierarchy contains a cycle");
                    cycleReported = true;
                }
                break;
            }
            if (++depth > limits.maxHierarchyDepth) {
                if (!depthReported) {
                    result.addError("joints", "hierarchy exceeds the configured maximum depth");
                    depthReported = true;
                }
                break;
            }
            const auto parent = skeleton.joints[cursor].parent;
            if (parent < 0 || static_cast<std::size_t>(parent) >= skeleton.joints.size()) {
                break;
            }
            cursor = static_cast<std::size_t>(parent);
        }
    }

    std::unordered_set<std::string_view> socketNames;
    socketNames.reserve(skeleton.sockets.size());
    for (std::size_t index = 0; index < skeleton.sockets.size(); ++index) {
        const auto& socket = skeleton.sockets[index];
        const auto path = "sockets[" + std::to_string(index) + "]";
        if (socket.name.empty()) {
            result.addError(path + ".name", "socket name must not be empty");
        } else if (!socketNames.insert(socket.name).second) {
            result.addError(path + ".name", "socket name must be unique");
        }
        if (static_cast<std::size_t>(socket.joint) >= skeleton.joints.size()) {
            result.addError(path + ".joint", "socket joint index is outside the skeleton");
        }
        validateTransform(socket.local, path + ".local", result);
    }
    return result;
}

std::optional<JointIndex> findJoint(const Skeleton& skeleton, std::string_view name) {
    for (std::size_t index = 0; index < skeleton.joints.size(); ++index) {
        if (skeleton.joints[index].name == name) {
            return static_cast<JointIndex>(index);
        }
    }
    return std::nullopt;
}

std::optional<std::size_t> findSocket(const Skeleton& skeleton, std::string_view name) {
    for (std::size_t index = 0; index < skeleton.sockets.size(); ++index) {
        if (skeleton.sockets[index].name == name) {
            return index;
        }
    }
    return std::nullopt;
}

LocalPose makeBindPose(const Skeleton& skeleton) {
    LocalPose pose;
    pose.joints.reserve(skeleton.joints.size());
    for (const auto& joint : skeleton.joints) {
        pose.joints.push_back(joint.bindLocal);
    }
    return pose;
}

ValidationResult validatePose(const Skeleton& skeleton, const LocalPose& pose) {
    ValidationResult result;
    if (pose.joints.size() != skeleton.joints.size()) {
        result.addError("joints", "pose joint count does not match the skeleton");
        return result;
    }
    for (std::size_t index = 0; index < pose.joints.size(); ++index) {
        validateTransform(pose.joints[index], "joints[" + std::to_string(index) + "]", result);
    }
    return result;
}

bool evaluateGlobalPose(const Skeleton& skeleton, const LocalPose& localPose, GlobalPose& outGlobalPose,
                        ValidationResult* validation) {
    ValidationResult result = validateSkeleton(skeleton);
    result.append(validatePose(skeleton, localPose), "pose");
    if (!result.valid()) {
        if (validation != nullptr) {
            *validation = std::move(result);
        }
        return false;
    }

    GlobalPose evaluated;
    evaluated.joints.resize(skeleton.joints.size());
    std::vector<std::uint8_t> state(skeleton.joints.size(), 0U);
    std::function<bool(std::size_t)> evaluateJoint = [&](std::size_t index) {
        if (state[index] == 2U) {
            return true;
        }
        if (state[index] == 1U) {
            return false;
        }
        state[index] = 1U;
        const auto local = matrixFromTransform(localPose.joints[index]);
        const auto parent = skeleton.joints[index].parent;
        if (parent >= 0) {
            const auto parentIndex = static_cast<std::size_t>(parent);
            if (!evaluateJoint(parentIndex)) {
                return false;
            }
            evaluated.joints[index] = multiply(evaluated.joints[parentIndex], local);
        } else {
            evaluated.joints[index] = local;
        }
        state[index] = 2U;
        return true;
    };

    for (std::size_t index = 0; index < skeleton.joints.size(); ++index) {
        if (!evaluateJoint(index)) {
            result.addError("joints", "hierarchy could not be evaluated");
            if (validation != nullptr) {
                *validation = std::move(result);
            }
            return false;
        }
    }
    outGlobalPose = std::move(evaluated);
    if (validation != nullptr) {
        *validation = std::move(result);
    }
    return true;
}

bool evaluateSocket(const Skeleton& skeleton, const GlobalPose& globalPose, std::size_t socketIndex,
                    Mat4& outTransform) {
    if (socketIndex >= skeleton.sockets.size() || globalPose.joints.size() != skeleton.joints.size()) {
        return false;
    }
    const auto& socket = skeleton.sockets[socketIndex];
    if (static_cast<std::size_t>(socket.joint) >= globalPose.joints.size() || !isFinite(socket.local) ||
        quaternionLengthSquared(socket.local.rotation) <= kQuaternionEpsilon ||
        std::abs(socket.local.scale.x) <= kScaleEpsilon || std::abs(socket.local.scale.y) <= kScaleEpsilon ||
        std::abs(socket.local.scale.z) <= kScaleEpsilon || !finiteMatrix(globalPose.joints[socket.joint])) {
        return false;
    }
    const auto evaluated = multiply(globalPose.joints[socket.joint], matrixFromTransform(socket.local));
    if (!finiteMatrix(evaluated)) {
        return false;
    }
    outTransform = evaluated;
    return true;
}

bool evaluateSocket(const Skeleton& skeleton, const GlobalPose& globalPose, std::string_view socketName,
                    Mat4& outTransform) {
    const auto index = findSocket(skeleton, socketName);
    return index.has_value() && evaluateSocket(skeleton, globalPose, *index, outTransform);
}

} // namespace novacore::animation
