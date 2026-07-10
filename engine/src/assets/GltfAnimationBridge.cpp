#include "novacore/assets/GltfAnimationBridge.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <unordered_map>
#include <utility>

namespace novacore::assets {

namespace {

[[nodiscard]] animation::Transform toTransform(const GltfNodeTransform& source) {
    return animation::Transform{source.translation, source.rotation, source.scale};
}

[[nodiscard]] animation::Mat4 toAnimationMatrix(const GltfMatrix4& source) {
    animation::Mat4 result{};
    for (std::size_t row = 0; row < 4; ++row) {
        for (std::size_t column = 0; column < 4; ++column) {
            result.at(row, column) = source.values[(column * 4U) + row];
        }
    }
    return result;
}

[[nodiscard]] animation::InterpolationMode toInterpolation(GltfAnimationInterpolation interpolation) {
    return interpolation == GltfAnimationInterpolation::Step
        ? animation::InterpolationMode::Step
        : animation::InterpolationMode::Linear;
}

[[nodiscard]] std::string lowercase(std::string_view value) {
    std::string result(value);
    std::ranges::transform(result, result.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return result;
}

[[nodiscard]] animation::JointIndex findJointByHints(
    const animation::Skeleton& skeleton,
    std::initializer_list<std::string_view> hints) {
    for (animation::JointIndex index = 0; index < skeleton.joints.size(); ++index) {
        const auto name = lowercase(skeleton.joints[index].name);
        for (const auto hint : hints) {
            if (name.find(lowercase(hint)) != std::string::npos) {
                return index;
            }
        }
    }
    return animation::invalidJointIndex;
}

[[nodiscard]] animation::JointIndex socketJoint(
    const animation::Skeleton& skeleton,
    std::string_view socketName) {
    const auto name = lowercase(socketName);
    if (name.find("hand_r") != std::string::npos || name.find("weapon") != std::string::npos) {
        return findJointByHints(skeleton, {"hand.r", "hand_r", "r_hand", "righthand"});
    }
    if (name.find("hand_l") != std::string::npos) {
        return findJointByHints(skeleton, {"hand.l", "hand_l", "l_hand", "lefthand"});
    }
    if (name.find("camera") != std::string::npos || name.find("head") != std::string::npos) {
        return findJointByHints(skeleton, {"head", "neck"});
    }
    if (name.find("vfx") != std::string::npos) {
        return findJointByHints(skeleton, {"chest", "spine.002", "spine_02", "spine"});
    }
    return skeleton.joints.empty() ? animation::invalidJointIndex : 0U;
}

[[nodiscard]] float lengthSquared(math::Vec3 value) {
    return (value.x * value.x) + (value.y * value.y) + (value.z * value.z);
}

[[nodiscard]] math::Vec3 normalized(math::Vec3 value, math::Vec3 fallback) {
    const float squared = lengthSquared(value);
    if (!std::isfinite(squared) || squared <= 0.0000001F) {
        return fallback;
    }
    const float inverseLength = 1.0F / std::sqrt(squared);
    return value * inverseLength;
}

void appendValidation(
    GltfAnimationBridgeResult& result,
    const animation::ValidationResult& validation,
    std::string_view prefix) {
    for (const auto& issue : validation.issues) {
        std::string message;
        if (!prefix.empty()) {
            message.append(prefix);
            message.append(": ");
        }
        if (!issue.path.empty()) {
            message.append(issue.path);
            message.append(": ");
        }
        message.append(issue.message);
        if (issue.severity == animation::ValidationSeverity::Error) {
            result.errors.push_back(std::move(message));
        } else {
            result.warnings.push_back(std::move(message));
        }
    }
}

[[nodiscard]] animation::JointTrack& trackForJoint(
    animation::AnimationClip& clip,
    animation::JointIndex joint) {
    const auto existing = std::ranges::find_if(
        clip.tracks,
        [joint](const animation::JointTrack& track) {
            return track.joint == joint;
        });
    if (existing != clip.tracks.end()) {
        return *existing;
    }
    clip.tracks.push_back(animation::JointTrack{joint});
    return clip.tracks.back();
}

void importChannel(
    const GltfAnimationChannelData& source,
    animation::JointTrack& target,
    GltfAnimationBridgeResult& result,
    std::string_view clipName) {
    const auto interpolation = toInterpolation(source.interpolation);
    if (source.interpolation == GltfAnimationInterpolation::CubicSpline) {
        result.warnings.push_back(
            "Animation clip '" + std::string(clipName) +
            "' uses cubic-spline interpolation; runtime currently samples its values linearly");
    }

    switch (source.targetPath) {
    case GltfAnimationTargetPath::Translation:
        target.translation.interpolation = interpolation;
        target.translation.keys.clear();
        target.translation.keys.reserve(source.vectorKeyframes.size());
        for (const auto& key : source.vectorKeyframes) {
            target.translation.keys.push_back(animation::Vec3Key{key.timeSeconds, key.value});
        }
        break;
    case GltfAnimationTargetPath::Rotation:
        target.rotation.interpolation = interpolation;
        target.rotation.keys.clear();
        target.rotation.keys.reserve(source.rotationKeyframes.size());
        for (const auto& key : source.rotationKeyframes) {
            target.rotation.keys.push_back(animation::QuatKey{key.timeSeconds, key.value});
        }
        break;
    case GltfAnimationTargetPath::Scale:
        target.scale.interpolation = interpolation;
        target.scale.keys.clear();
        target.scale.keys.reserve(source.vectorKeyframes.size());
        for (const auto& key : source.vectorKeyframes) {
            target.scale.keys.push_back(animation::Vec3Key{key.timeSeconds, key.value});
        }
        break;
    }
}

} // namespace

const animation::AnimationClip* GltfAnimationAsset::findClip(std::string_view name) const {
    const auto found = std::ranges::find_if(
        clips,
        [name](const animation::AnimationClip& clip) {
            return clip.name == name;
        });
    return found == clips.end() ? nullptr : &*found;
}

GltfAnimationBridgeResult buildGltfAnimationAsset(
    const GltfAnimationData& source,
    std::size_t skinIndex,
    GltfAnimationAsset& outAsset) {
    outAsset = {};
    GltfAnimationBridgeResult result{};
    if (skinIndex >= source.skins.size()) {
        result.errors.push_back("Requested glTF skin index is out of range");
        return result;
    }

    const auto& skin = source.skins[skinIndex];
    if (skin.joints.empty()) {
        result.errors.push_back("Requested glTF skin has no joints");
        return result;
    }

    outAsset.sourcePath = source.path;
    outAsset.skinIndex = skin.skinIndex;
    outAsset.skeleton.name = skin.name.empty()
        ? "gltf_skin_" + std::to_string(skin.skinIndex)
        : skin.name;
    outAsset.skeleton.joints.reserve(skin.joints.size());
    outAsset.inverseBindMatrices.reserve(skin.joints.size());
    outAsset.jointNodeIndices.reserve(skin.joints.size());

    std::unordered_map<std::size_t, animation::JointIndex> jointByNode;
    jointByNode.reserve(skin.joints.size());
    for (std::size_t index = 0; index < skin.joints.size(); ++index) {
        jointByNode.emplace(skin.joints[index].nodeIndex, static_cast<animation::JointIndex>(index));
    }

    for (std::size_t index = 0; index < skin.joints.size(); ++index) {
        const auto& joint = skin.joints[index];
        outAsset.skeleton.joints.push_back(animation::Joint{
            joint.name.empty() ? "joint_" + std::to_string(index) : joint.name,
            joint.parentJointIndex,
            toTransform(joint.localTransform),
        });
        outAsset.inverseBindMatrices.push_back(toAnimationMatrix(joint.inverseBindMatrix));
        outAsset.jointNodeIndices.push_back(joint.nodeIndex);
    }

    for (const auto& node : source.nodes) {
        if (!lowercase(node.name).starts_with("socket_")) {
            continue;
        }
        const auto joint = socketJoint(outAsset.skeleton, node.name);
        if (joint == animation::invalidJointIndex) {
            result.warnings.push_back("Could not bind glTF socket node '" + node.name + "' to a joint");
            continue;
        }
        outAsset.skeleton.sockets.push_back(animation::Socket{
            node.name,
            joint,
            {},
        });
    }

    appendValidation(result, animation::validateSkeleton(outAsset.skeleton), "skeleton");
    if (!result.errors.empty()) {
        return result;
    }

    outAsset.clips.reserve(source.animations.size());
    for (const auto& sourceClip : source.animations) {
        animation::AnimationClip clip{};
        clip.name = sourceClip.name.empty()
            ? "animation_" + std::to_string(sourceClip.animationIndex)
            : sourceClip.name;
        clip.duration = sourceClip.durationSeconds;

        for (const auto& channel : sourceClip.channels) {
            const auto joint = jointByNode.find(channel.targetNodeIndex);
            if (joint == jointByNode.end()) {
                continue;
            }
            auto& track = trackForJoint(clip, joint->second);
            importChannel(channel, track, result, clip.name);
        }

        std::ranges::sort(
            clip.tracks,
            [](const animation::JointTrack& lhs, const animation::JointTrack& rhs) {
                return lhs.joint < rhs.joint;
            });
        const auto validation = animation::validateClip(clip, outAsset.skeleton);
        if (!validation.valid()) {
            for (const auto& issue : validation.issues) {
                result.warnings.push_back(
                    "Skipped clip '" + clip.name + "': " + issue.path + ": " + issue.message);
            }
            continue;
        }
        appendValidation(result, validation, "clip '" + clip.name + "'");
        outAsset.clips.push_back(std::move(clip));
    }

    if (outAsset.clips.empty() && !source.animations.empty()) {
        result.errors.push_back("No animation clip targets joints in the selected skin");
    }
    return result;
}

GltfAnimationBridgeResult skinGltfMesh(
    const GltfMeshData& source,
    const GltfAnimationAsset& animationAsset,
    const animation::GlobalPose& pose,
    GltfMeshData& outMesh) {
    GltfAnimationBridgeResult result{};
    if (animationAsset.inverseBindMatrices.size() != animationAsset.skeleton.joints.size() ||
        pose.joints.size() != animationAsset.skeleton.joints.size()) {
        result.errors.push_back("Skinning pose, skeleton, and inverse-bind matrix counts do not match");
        return result;
    }

    std::vector<animation::Mat4> palette(animationAsset.skeleton.joints.size());
    for (std::size_t index = 0; index < palette.size(); ++index) {
        palette[index] = animation::multiply(pose.joints[index], animationAsset.inverseBindMatrices[index]);
    }

    bool reusable = outMesh.primitives.size() == source.primitives.size();
    if (reusable) {
        for (std::size_t index = 0; index < source.primitives.size(); ++index) {
            if (outMesh.primitives[index].positions.size() != source.primitives[index].positions.size() ||
                outMesh.primitives[index].normals.size() != source.primitives[index].normals.size() ||
                outMesh.primitives[index].indices.size() != source.primitives[index].indices.size()) {
                reusable = false;
                break;
            }
        }
    }
    if (!reusable) {
        outMesh = source;
    }
    std::size_t skinnedPrimitiveCount = 0;
    for (std::size_t primitiveIndex = 0; primitiveIndex < outMesh.primitives.size(); ++primitiveIndex) {
        auto& primitive = outMesh.primitives[primitiveIndex];
        if (primitive.skinIndex < 0 ||
            static_cast<std::size_t>(primitive.skinIndex) != animationAsset.skinIndex) {
            continue;
        }
        if (primitive.joints.size() != primitive.positions.size() ||
            primitive.weights.size() != primitive.positions.size()) {
            result.errors.push_back(
                "Skinned primitive " + std::to_string(primitive.primitiveIndex) +
                " has incomplete JOINTS_0 or WEIGHTS_0 data");
            continue;
        }

        const auto& sourcePrimitive = source.primitives[primitiveIndex];
        for (std::size_t vertexIndex = 0; vertexIndex < primitive.positions.size(); ++vertexIndex) {
            const auto sourcePosition = sourcePrimitive.positions[vertexIndex];
            const auto sourceNormal = vertexIndex < sourcePrimitive.normals.size()
                ? sourcePrimitive.normals[vertexIndex]
                : math::Vec3{0.0F, 1.0F, 0.0F};
            math::Vec3 position{};
            math::Vec3 normal{};
            float weightSum = 0.0F;

            for (std::size_t influence = 0; influence < 4; ++influence) {
                const float weight = primitive.weights[vertexIndex][influence];
                const auto joint = primitive.joints[vertexIndex][influence];
                if (!std::isfinite(weight) || weight <= 0.0F) {
                    continue;
                }
                if (joint >= palette.size()) {
                    result.errors.push_back(
                        "Skinned primitive references joint " + std::to_string(joint) +
                        " outside the selected skin");
                    continue;
                }
                position = position + (animation::transformPoint(palette[joint], sourcePosition) * weight);
                normal = normal + (animation::transformDirection(palette[joint], sourceNormal) * weight);
                weightSum += weight;
            }

            if (weightSum > 0.00001F) {
                primitive.positions[vertexIndex] = position * (1.0F / weightSum);
                if (vertexIndex < primitive.normals.size()) {
                    primitive.normals[vertexIndex] = normalized(normal, sourceNormal);
                }
            }
        }
        ++skinnedPrimitiveCount;
    }

    if (skinnedPrimitiveCount == 0) {
        result.errors.push_back("Mesh has no primitives bound to the selected glTF skin");
    }
    return result;
}

} // namespace novacore::assets
