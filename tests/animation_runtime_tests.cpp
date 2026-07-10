#include "novacore/animation/AnimationRuntime.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <string_view>
#include <utility>

namespace {

using namespace novacore;
using namespace novacore::animation;

int failures = 0;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        ++failures;
        std::cerr << "[fail] " << message << '\n';
    }
}

void expectNear(float actual, float expected, std::string_view message, float tolerance = 1.0e-4F) {
    expect(std::abs(actual - expected) <= tolerance, message);
}

[[nodiscard]] math::Quat axisAngle(math::Vec3 axis, float radians) {
    const float halfAngle = radians * 0.5F;
    const float sine = std::sin(halfAngle);
    return normalize({axis.x * sine, axis.y * sine, axis.z * sine, std::cos(halfAngle)});
}

[[nodiscard]] Skeleton makeSkeleton() {
    Skeleton skeleton;
    skeleton.name = "runtime_test_rig";
    skeleton.joints = {
        Joint{"root", -1, {}},
        Joint{"spine", 0, Transform{{0.0F, 1.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}},
        Joint{"hand", 1, Transform{{1.0F, 0.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}},
    };
    skeleton.sockets = {
        Socket{"muzzle", 2U, Transform{{0.0F, 0.0F, 2.0F}, {}, {1.0F, 1.0F, 1.0F}}},
    };
    return skeleton;
}

[[nodiscard]] AnimationClip makeWalkClip() {
    AnimationClip clip;
    clip.name = "walk";
    clip.duration = 2.0F;
    JointTrack root;
    root.joint = 0U;
    root.translation.keys = {
        {0.0F, {0.0F, 0.0F, 0.0F}},
        {2.0F, {0.0F, 0.0F, 4.0F}},
    };
    clip.tracks.push_back(std::move(root));
    return clip;
}

[[nodiscard]] AnimationClip makeStandingClip(float rootZ) {
    AnimationClip clip;
    clip.name = "standing";
    clip.duration = 2.0F;
    JointTrack root;
    root.joint = 0U;
    root.translation.keys = {
        {0.0F, {0.0F, 0.0F, rootZ}},
        {2.0F, {0.0F, 0.0F, rootZ}},
    };
    clip.tracks.push_back(std::move(root));
    return clip;
}

[[nodiscard]] AnimationClip makeHandLayerClip() {
    AnimationClip clip;
    clip.name = "hand_override";
    clip.duration = 1.0F;
    JointTrack hand;
    hand.joint = 2U;
    hand.translation.keys = {
        {0.0F, {1.0F, 0.0F, 0.0F}},
        {1.0F, {3.0F, 0.0F, 0.0F}},
    };
    clip.tracks.push_back(std::move(hand));
    return clip;
}

void testMathAndGlobalHierarchy() {
    const auto quarterTurn = axisAngle({0.0F, 1.0F, 0.0F}, 3.1415926535F * 0.5F);
    const auto rotated = rotate(quarterTurn, {0.0F, 0.0F, 1.0F});
    expectNear(rotated.x, 1.0F, "quaternion rotates forward onto positive X");
    expectNear(rotated.z, 0.0F, "quaternion rotation clears original forward axis");

    const auto halfway = slerp({}, axisAngle({0.0F, 1.0F, 0.0F}, 3.1415926535F), 0.5F);
    const auto halfwayForward = rotate(halfway, {0.0F, 0.0F, 1.0F});
    expectNear(std::abs(halfwayForward.x), 1.0F, "slerp uses spherical quaternion interpolation");

    Skeleton scaled;
    scaled.joints = {
        Joint{"root", -1, Transform{{1.0F, 2.0F, 3.0F}, {}, {2.0F, 3.0F, 4.0F}}},
        Joint{"child", 0, Transform{{1.0F, 1.0F, 1.0F}, {}, {1.0F, 1.0F, 1.0F}}},
    };
    auto pose = makeBindPose(scaled);
    GlobalPose global;
    expect(evaluateGlobalPose(scaled, pose, global), "valid hierarchy evaluates globally");
    const auto childPosition = matrixTranslation(global.joints[1]);
    expectNear(childPosition.x, 3.0F, "parent X scale affects child translation");
    expectNear(childPosition.y, 5.0F, "parent Y scale affects child translation");
    expectNear(childPosition.z, 7.0F, "parent Z scale affects child translation");

    Skeleton unordered;
    unordered.joints = {
        Joint{"child", 1, Transform{{0.0F, 2.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}},
        Joint{"root", -1, Transform{{4.0F, 0.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}},
    };
    expect(evaluateGlobalPose(unordered, makeBindPose(unordered), global), "parent may appear after child in storage");
    expectNear(matrixTranslation(global.joints[0]).x, 4.0F, "unordered hierarchy resolves parent transform");
    expectNear(matrixTranslation(global.joints[0]).y, 2.0F, "unordered hierarchy keeps child transform");
}

void testSkeletonValidationAndSockets() {
    auto skeleton = makeSkeleton();
    expect(validateSkeleton(skeleton).valid(), "well-formed skeleton validates");
    expect(findJoint(skeleton, "hand") == 2U, "joint lookup returns stable index");
    expect(!findJoint(skeleton, "missing").has_value(), "missing joint lookup is explicit");

    auto pose = makeBindPose(skeleton);
    pose.joints[0].translation = {5.0F, 0.0F, 0.0F};
    GlobalPose global;
    expect(evaluateGlobalPose(skeleton, pose, global), "socket source pose evaluates");
    Mat4 socketTransform;
    expect(evaluateSocket(skeleton, global, "muzzle", socketTransform), "named socket evaluates");
    const auto socketPosition = matrixTranslation(socketTransform);
    expectNear(socketPosition.x, 6.0F, "socket inherits complete joint hierarchy on X");
    expectNear(socketPosition.y, 1.0F, "socket inherits complete joint hierarchy on Y");
    expectNear(socketPosition.z, 2.0F, "socket applies local attachment offset");
    expect(!evaluateSocket(skeleton, global, "missing", socketTransform), "missing socket fails without fallback");
    global.joints[2].at(0, 0) = std::numeric_limits<float>::quiet_NaN();
    expect(!evaluateSocket(skeleton, global, "muzzle", socketTransform),
           "socket evaluation rejects non-finite global transforms");

    auto cyclic = skeleton;
    cyclic.joints[0].parent = 2;
    expect(!validateSkeleton(cyclic).valid(), "hierarchy cycle is rejected");

    auto invalidParent = skeleton;
    invalidParent.joints[1].parent = 99;
    expect(!validateSkeleton(invalidParent).valid(), "out-of-range parent is rejected");

    auto duplicateNames = skeleton;
    duplicateNames.joints[2].name = "spine";
    duplicateNames.sockets.push_back(duplicateNames.sockets.front());
    expect(validateSkeleton(duplicateNames).errorCount() >= 2U, "duplicate joint and socket names are rejected");

    auto invalidTransforms = skeleton;
    invalidTransforms.joints[0].bindLocal.rotation = {0.0F, 0.0F, 0.0F, 0.0F};
    invalidTransforms.sockets[0].local.scale.y = 0.0F;
    expect(validateSkeleton(invalidTransforms).errorCount() >= 2U, "zero quaternion and zero scale are rejected");

    LocalPose shortPose;
    shortPose.joints.resize(1U);
    expect(!validatePose(skeleton, shortPose).valid(), "pose count mismatch is rejected");
}

void testClipValidationAndSampling() {
    const auto skeleton = makeSkeleton();
    auto walk = makeWalkClip();
    expect(validateClip(walk, skeleton).valid(), "valid sparse clip validates");

    LocalPose pose;
    expect(sampleClip(walk, skeleton, 0.5F, WrapMode::Clamp, pose), "clamped clip samples");
    expectNear(pose.joints[0].translation.z, 1.0F, "Vec3 channel interpolates linearly");
    expectNear(pose.joints[1].translation.y, 1.0F, "missing channel preserves bind pose");

    expect(sampleClip(walk, skeleton, 2.5F, WrapMode::Loop, pose), "looped clip samples");
    expectNear(pose.joints[0].translation.z, 1.0F, "loop sampling wraps by clip duration");
    expect(sampleClip(walk, skeleton, -0.5F, WrapMode::Loop, pose), "negative loop time samples");
    expectNear(pose.joints[0].translation.z, 3.0F, "negative time wraps to positive phase");
    expect(sampleClip(walk, skeleton, 9.0F, WrapMode::Clamp, pose), "late clamp time samples");
    expectNear(pose.joints[0].translation.z, 4.0F, "clamp sampling holds final key");

    Vec3Track step;
    step.interpolation = InterpolationMode::Step;
    step.keys = {{0.0F, {2.0F, 0.0F, 0.0F}}, {1.0F, {8.0F, 0.0F, 0.0F}}};
    expectNear(sampleVec3Track(step, 0.75F, {}).x, 2.0F, "step channel holds the previous key");

    AnimationClip turn;
    turn.name = "turn";
    turn.duration = 1.0F;
    JointTrack rootTurn;
    rootTurn.joint = 0U;
    rootTurn.rotation.keys = {
        {0.0F, {}},
        {1.0F, axisAngle({0.0F, 1.0F, 0.0F}, 3.1415926535F)},
    };
    turn.tracks.push_back(rootTurn);
    expect(sampleClip(turn, skeleton, 0.5F, WrapMode::Clamp, pose), "quaternion clip samples");
    const auto forward = rotate(pose.joints[0].rotation, {0.0F, 0.0F, 1.0F});
    expectNear(std::abs(forward.x), 1.0F, "quaternion channel uses shortest-path slerp");

    auto malformed = walk;
    malformed.duration = 1.0F;
    malformed.tracks.front().translation.keys = {
        {0.75F, {}},
        {0.25F, {}},
    };
    malformed.tracks.push_back(malformed.tracks.front());
    expect(validateClip(malformed, skeleton).errorCount() >= 2U,
           "unsorted keys and duplicate joint tracks are rejected");

    malformed = walk;
    malformed.tracks.front().rotation.keys = {{0.0F, {0.0F, 0.0F, 0.0F, 0.0F}}};
    expect(!validateClip(malformed, skeleton).valid(), "zero-length quaternion key is rejected");
    ValidationResult validation;
    expect(!sampleClip(malformed, skeleton, 0.0F, WrapMode::Clamp, pose, &validation), "invalid clip refuses sampling");
    expect(!validation.valid(), "failed sampling returns detailed validation");
}

void testPoseBlendingAndMasks() {
    const auto skeleton = makeSkeleton();
    auto base = makeBindPose(skeleton);
    auto overlay = base;
    overlay.joints[0].translation = {10.0F, 0.0F, 0.0F};
    overlay.joints[1].translation = {0.0F, 5.0F, 0.0F};
    overlay.joints[2].translation = {5.0F, 0.0F, 0.0F};

    const auto handMask = makeDescendantMask(skeleton, 2U);
    expect(validateLayerMask(handMask, skeleton).valid(), "generated descendant mask validates");
    expectNear(handMask.weight(0U), 0.0F, "descendant mask excludes ancestors");
    expectNear(handMask.weight(2U), 1.0F, "descendant mask includes selected root");

    LocalPose blended;
    expect(blendPoses(base, overlay, 0.5F, blended, &handMask), "masked pose blend succeeds");
    expectNear(blended.joints[0].translation.x, 0.0F, "masked blend preserves excluded root");
    expectNear(blended.joints[1].translation.y, 1.0F, "masked blend preserves excluded spine");
    expectNear(blended.joints[2].translation.x, 3.0F, "masked blend weights included hand");

    LayerMask malformed{{0.0F, 1.0F}};
    expect(!validateLayerMask(malformed, skeleton).valid(), "wrong-sized layer mask is rejected");
    expect(!blendPoses(base, overlay, 1.0F, blended, &malformed), "wrong-sized mask cannot blend poses");

    LayerMask nonFinite{{0.0F, 1.0F, std::numeric_limits<float>::quiet_NaN()}};
    expect(!validateLayerMask(nonFinite, skeleton).valid(), "non-finite layer weight is rejected");
    expect(!blendPoses(base, overlay, 1.0F, blended, &nonFinite), "non-finite layer weight cannot reach blending math");

    auto invalidPose = overlay;
    invalidPose.joints[2].rotation = {0.0F, 0.0F, 0.0F, 0.0F};
    expect(!blendPoses(base, invalidPose, 0.5F, blended), "pose blending rejects zero-length quaternions");
}

void testRootMotion() {
    const auto skeleton = makeSkeleton();
    const auto walk = makeWalkClip();

    const auto simple = extractRootMotionDelta(walk, skeleton, 0U, 0.25F, 0.75F, WrapMode::Clamp);
    expectNear(simple.translation.z, 1.0F, "root motion extracts in-range translation delta");

    const auto loop = extractRootMotionDelta(walk, skeleton, 0U, 1.5F, 2.5F, WrapMode::Loop);
    expectNear(loop.translation.z, 2.0F, "root motion remains continuous over loop boundary");

    const auto multiLoop = extractRootMotionDelta(walk, skeleton, 0U, 0.5F, 4.5F, WrapMode::Loop);
    expectNear(multiLoop.translation.z, 8.0F, "root motion accumulates multiple complete loops");

    const auto reverse = extractRootMotionDelta(walk, skeleton, 0U, 2.5F, 1.5F, WrapMode::Loop);
    expectNear(reverse.translation.z, -2.0F, "root motion supports reverse playback");

    const auto clamped = extractRootMotionDelta(walk, skeleton, 0U, 1.5F, 5.0F, WrapMode::Clamp);
    expectNear(clamped.translation.z, 1.0F, "clamped root motion stops at clip end");

    const auto invalid = extractRootMotionDelta(walk, skeleton, 99U, 0.0F, 1.0F, WrapMode::Loop);
    expectNear(invalid.translation.lengthSquared(), 0.0F, "invalid root joint produces identity delta");

    auto malformed = walk;
    malformed.tracks.front().translation.keys[1].time = -1.0F;
    const auto malformedDelta = extractRootMotionDelta(malformed, skeleton, 0U, 0.0F, 1.0F, WrapMode::Loop);
    expectNear(malformedDelta.translation.lengthSquared(), 0.0F, "invalid root-motion clip produces identity delta");

    AnimationClip turning;
    turning.name = "turning_root_motion";
    turning.duration = 2.0F;
    JointTrack turningRoot{0U};
    turningRoot.translation.keys = {
        {0.0F, {0.0F, 0.0F, 0.0F}},
        {2.0F, {0.0F, 0.0F, 2.0F}},
    };
    turningRoot.rotation.keys = {
        {0.0F, {}},
        {2.0F, axisAngle({0.0F, 1.0F, 0.0F}, 3.1415926535F * 0.5F)},
    };
    turning.tracks.push_back(std::move(turningRoot));
    const auto twoTurningCycles = extractRootMotionDelta(turning, skeleton, 0U, 0.0F, 4.0F, WrapMode::Loop);
    expectNear(twoTurningCycles.translation.x, 2.0F, "root motion composes translation through cycle rotation");
    expectNear(twoTurningCycles.translation.z, 2.0F, "root motion preserves first cycle displacement");
    const auto turnedForward = rotate(twoTurningCycles.rotation, {0.0F, 0.0F, 1.0F});
    expectNear(turnedForward.z, -1.0F, "root motion accumulates rotational cycles");
}

void testAnimationRuntime() {
    const auto skeleton = makeSkeleton();
    const auto walk = makeWalkClip();
    const auto standing = makeStandingClip(10.0F);
    const auto handLayer = makeHandLayerClip();

    AnimationRuntime runtime;
    expect(runtime.setSkeleton(skeleton).valid(), "runtime accepts valid skeleton");
    expect(runtime.play(walk).valid(), "runtime starts validated base clip");
    expect(runtime.update(0.5F), "runtime advances base clip");
    expectNear(runtime.localPose().joints[0].translation.z, 1.0F, "runtime publishes sampled local pose");
    expectNear(runtime.rootMotionDelta().translation.z, 1.0F, "runtime publishes root motion delta");

    expect(runtime.crossFade(standing, 1.0F).valid(), "runtime starts crossfade");
    expect(runtime.transitioning(), "runtime reports active transition");
    expect(runtime.update(0.5F), "runtime advances crossfade");
    expectNear(runtime.localPose().joints[0].translation.z, 6.0F, "crossfade blends source and target poses");
    expect(!runtime.crossFade(walk, 1.0F).valid(), "runtime rejects overlapping crossfade requests");
    expect(runtime.update(0.5F), "runtime completes crossfade");
    expect(!runtime.transitioning(), "completed crossfade promotes target playback");
    expectNear(runtime.localPose().joints[0].translation.z, 10.0F, "completed crossfade reaches target pose");

    AnimationLayerDesc layer;
    layer.clip = &handLayer;
    layer.mask = makeDescendantMask(skeleton, 2U);
    layer.time = 1.0F;
    layer.playbackRate = 0.0F;
    layer.weight = 1.0F;
    layer.wrapMode = WrapMode::Clamp;
    expect(runtime.setLayer(0U, layer).valid(), "runtime installs validated masked layer");
    expectNear(runtime.localPose().joints[2].translation.x, 3.0F, "runtime layer overrides included hand");
    expectNear(runtime.localPose().joints[1].translation.y, 1.0F, "runtime layer preserves excluded spine");

    Mat4 socket;
    expect(runtime.socketTransform("muzzle", socket), "runtime evaluates socket from layered global pose");
    const auto socketPosition = matrixTranslation(socket);
    expectNear(socketPosition.x, 3.0F, "runtime socket inherits layered hand translation");
    expectNear(socketPosition.y, 1.0F, "runtime socket inherits spine translation");
    expectNear(socketPosition.z, 12.0F, "runtime socket inherits base root and local socket offset");

    expect(!runtime.update(-0.1F), "runtime rejects negative frame delta");
    expect(!runtime.lastError().empty(), "runtime exposes operation failure diagnostics");
    expect(!runtime.setRootMotionJoint(99U).valid(), "runtime rejects invalid root motion joint");
    expect(runtime.setRootMotionJoint(std::nullopt).valid(), "runtime can explicitly disable root motion");
    runtime.clearLayers();
    expectNear(runtime.localPose().joints[2].translation.x, 1.0F, "clearing layers restores base pose");
}

} // namespace

int main() {
    testMathAndGlobalHierarchy();
    testSkeletonValidationAndSockets();
    testClipValidationAndSampling();
    testPoseBlendingAndMasks();
    testRootMotion();
    testAnimationRuntime();

    if (failures != 0) {
        std::cerr << failures << " animation runtime test(s) failed\n";
        return 1;
    }
    std::cout << "NovaCore animation runtime tests passed\n";
    return 0;
}
