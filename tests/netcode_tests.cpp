#include "novacore/net/BitStream.hpp"
#include "novacore/net/InterpolationBuffer.hpp"
#include "novacore/net/Protocol.hpp"
#include "novacore/net/Reconciliation.hpp"
#include "novacore/net/SnapshotReplication.hpp"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string_view>
#include <vector>

namespace {

int failures = 0;

void expect(bool condition, std::string_view message) {
    if (condition) {
        return;
    }
    ++failures;
    std::cerr << "[fail] " << message << '\n';
}

void expectNear(double actual, double expected, double tolerance, std::string_view message) {
    expect(std::abs(actual - expected) <= tolerance, message);
}

novacore::net::ReplicatedComponent component(
    novacore::net::ReplicatedComponentId id,
    std::uint16_t revision,
    std::initializer_list<std::uint8_t> payload) {
    return {id, revision, std::vector<std::uint8_t>(payload)};
}

novacore::net::ReplicatedEntity entity(
    novacore::net::NetworkEntityId id,
    std::uint16_t generation,
    std::initializer_list<novacore::net::ReplicatedComponent> components) {
    return {id, generation, std::vector<novacore::net::ReplicatedComponent>(components)};
}

novacore::net::WorldSnapshot baselineSnapshot() {
    novacore::net::WorldSnapshot snapshot{};
    snapshot.snapshotId = 40U;
    snapshot.serverTick = {100U};
    snapshot.acknowledgedInputTick = {96U};
    snapshot.entities = {
        entity(10U, 2U, {component(1U, 3U, {1U, 2U}), component(4U, 1U, {9U})}),
        entity(20U, 1U, {component(1U, 7U, {4U, 5U, 6U})}),
        entity(30U, 1U, {component(2U, 1U, {8U})}),
    };
    return snapshot;
}

void testStrictBitStreamSpanReads() {
    novacore::net::PacketWriter writer;
    const std::vector<std::uint8_t> payload{9U, 8U, 7U};
    writer.writeU16(3U);
    writer.writeBytes(std::span<const std::uint8_t>(payload));

    const auto bytes = writer.finish();
    novacore::net::PacketReader reader(bytes);
    std::uint16_t count = 0U;
    expect(reader.readU16(count) && count == 3U, "bit stream reads span length");
    const auto readPayload = reader.readByteSpan(count);
    expect(readPayload.has_value() && (*readPayload)[1] == 8U, "bit stream returns immutable byte span");
    expect(reader.consumed(), "bit stream consumes exact payload");
    expect(!reader.readByteSpan(1U).has_value(), "bit stream rejects span overread");
}

void testPacketEnvelopeRoundTripAndStrictFailures() {
    novacore::net::PacketHeader header{};
    header.type = novacore::net::PacketType::Snapshot;
    header.flags = novacore::net::PacketFlags::HasAcknowledgement;
    header.sequence = {77U};
    header.acknowledgement = {73U};
    header.acknowledgementBits = 0b1011U;
    header.simulationTick = {9001U};
    const std::vector<std::uint8_t> payload{1U, 3U, 5U, 7U, 9U};

    const auto encoded = novacore::net::encodePacket(header, payload);
    expect(encoded.has_value(), "packet envelope encodes valid packet");
    expect(encoded.has_value() && encoded->size() == novacore::net::kPacketHeaderBytes + payload.size(),
        "packet envelope uses fixed header size");
    const auto decoded = novacore::net::decodePacket(*encoded);
    expect(decoded && decoded.packet->header.sequence.value == 77U, "packet envelope preserves sequence");
    expect(decoded && decoded.packet->header.acknowledgementBits == 0b1011U,
        "packet envelope preserves acknowledgement history");
    expect(decoded && decoded.packet->payload == payload, "packet envelope preserves payload");

    auto corrupt = *encoded;
    corrupt.back() ^= 0x80U;
    expect(novacore::net::decodePacket(corrupt).error == novacore::net::PacketDecodeError::ChecksumMismatch,
        "packet envelope rejects checksum mismatch");
    auto trailing = *encoded;
    trailing.push_back(0U);
    expect(novacore::net::decodePacket(trailing).error == novacore::net::PacketDecodeError::SizeMismatch,
        "packet envelope rejects trailing bytes");
    auto truncated = *encoded;
    truncated.resize(12U);
    expect(novacore::net::decodePacket(truncated).error == novacore::net::PacketDecodeError::Truncated,
        "packet envelope rejects truncated header");
    auto invalidType = *encoded;
    invalidType[6] = 255U;
    expect(novacore::net::decodePacket(invalidType).error == novacore::net::PacketDecodeError::InvalidType,
        "packet envelope validates packet type before checksum");
}

void testSequenceComparisonAcrossWrap() {
    constexpr auto maximum = std::numeric_limits<std::uint32_t>::max();
    expect(novacore::net::sequenceMoreRecent(1U, maximum), "wrapped sequence is newer than maximum");
    expect(novacore::net::sequenceMoreRecent(0U, maximum - 1U), "zero is newer across wrap");
    expect(!novacore::net::sequenceMoreRecent(maximum, 1U), "maximum is older after wrap");
    expect(novacore::net::sequenceDistance(1U, maximum) == 2U, "sequence distance wraps naturally");
}

void testReceiveAcknowledgementWindow() {
    novacore::net::ReceiveSequenceTracker tracker;
    expect(tracker.observe({100U}), "receive tracker accepts first packet");
    expect(tracker.observe({102U}), "receive tracker accepts packet with gap");
    expect(tracker.observe({101U}), "receive tracker accepts reordered packet inside window");
    expect(!tracker.observe({101U}), "receive tracker rejects duplicate reordered packet");
    expect(!tracker.observe({60U}), "receive tracker rejects packet outside history window");

    const auto acknowledgement = tracker.acknowledgementState();
    expect(acknowledgement.valid && acknowledgement.latest.value == 102U,
        "receive tracker reports newest acknowledgement");
    expect(novacore::net::acknowledgementContains(acknowledgement, {102U}),
        "acknowledgement includes latest packet");
    expect(novacore::net::acknowledgementContains(acknowledgement, {101U}),
        "acknowledgement includes reordered packet");
    expect(novacore::net::acknowledgementContains(acknowledgement, {100U}),
        "acknowledgement includes prior latest packet");
    expect(!novacore::net::acknowledgementContains(acknowledgement, {99U}),
        "acknowledgement leaves missing packet clear");

    tracker.reset();
    constexpr auto maximum = std::numeric_limits<std::uint32_t>::max();
    expect(tracker.observe({maximum}), "receive tracker starts at wrap boundary");
    expect(tracker.observe({0U}), "receive tracker advances across wrap boundary");
    expect(tracker.received({maximum}), "receive tracker remembers pre-wrap packet");

    tracker.reset();
    expect(tracker.observe({200U}), "receive tracker starts exact-window test");
    expect(tracker.observe({232U}), "receive tracker advances exactly acknowledgement window");
    expect(tracker.received({200U}), "receive tracker keeps packet exactly 32 sequences old");
}

void testSequenceChannelTracksSentAndAcknowledgedPackets() {
    novacore::net::SequenceChannel channel;
    channel.reset({500U});
    std::vector<novacore::net::PacketHeader> headers;
    for (std::uint64_t tick = 10U; tick < 14U; ++tick) {
        auto header = channel.makeHeader(
            novacore::net::PacketType::InputCommands,
            {tick},
            24U,
            tick == 10U);
        expect(header.has_value(), "sequence channel creates bounded packet header");
        expect(header.has_value() && channel.trackSent(*header), "sequence channel tracks sent packet");
        headers.push_back(*header);
    }
    expect(headers.front().sequence.value == 500U && headers.back().sequence.value == 503U,
        "sequence channel allocates consecutive packet sequences");
    expect(channel.sentHistorySize() == 4U, "sequence channel counts sent history");

    novacore::net::AcknowledgementState acknowledgement{};
    acknowledgement.valid = true;
    acknowledgement.latest = {503U};
    acknowledgement.historyBits = (1U << 0U) | (1U << 2U);
    const auto report = channel.processAcknowledgements(acknowledgement);
    expect(report.newlyAcknowledged.size() == 3U, "sequence channel resolves selective acknowledgements");
    expect(channel.sent({503U})->acknowledged, "latest sent packet is acknowledged");
    expect(channel.sent({502U})->acknowledged, "ack history bit zero acknowledges previous packet");
    expect(!channel.sent({501U})->acknowledged, "ack history preserves packet loss gap");
    expect(channel.sent({500U})->acknowledged, "ack history acknowledges older selected packet");
    const auto duplicate = channel.processAcknowledgements(acknowledgement);
    expect(duplicate.newlyAcknowledged.empty() && duplicate.duplicateAcknowledgements == 3U,
        "sequence channel does not report duplicate acknowledgements twice");

    expect(channel.observeReceived({88U}), "sequence channel observes remote packet");
    const auto outgoing = channel.makeHeader(novacore::net::PacketType::KeepAlive, {20U}, 0U);
    expect(outgoing.has_value() &&
            novacore::net::hasFlag(outgoing->flags, novacore::net::PacketFlags::HasAcknowledgement),
        "sequence channel puts receive acknowledgement on outgoing packet");
    expect(outgoing.has_value() && outgoing->acknowledgement.value == 88U,
        "outgoing packet carries newest remote sequence");
    expect(!channel.makeHeader(
            novacore::net::PacketType::Snapshot,
            {20U},
            novacore::net::kMaxPacketPayloadBytes + 1U).has_value(),
        "sequence channel rejects oversized packet header");
}

void testFullSnapshotRoundTripAndCanonicalization() {
    auto snapshot = baselineSnapshot();
    std::reverse(snapshot.entities.begin(), snapshot.entities.end());
    std::reverse(snapshot.entities.front().components.begin(), snapshot.entities.front().components.end());
    const auto encoded = novacore::net::encodeSnapshot(snapshot);
    expect(encoded.has_value(), "full snapshot encodes");
    const auto decoded = novacore::net::decodeSnapshot(*encoded);
    expect(decoded && decoded.value->snapshotId == 40U, "full snapshot keeps snapshot id");
    expect(decoded && decoded.value->entities.size() == 3U, "full snapshot keeps entities");
    expect(decoded && decoded.value->entities.front().entityId == 10U,
        "full snapshot decoder returns canonical entity order");
    expect(decoded && decoded.value->entities.front().components.front().componentId == 1U,
        "full snapshot decoder returns canonical component order");

    auto trailing = *encoded;
    trailing.push_back(0xCCU);
    expect(novacore::net::decodeSnapshot(trailing).error == novacore::net::SnapshotDecodeError::TrailingData,
        "full snapshot rejects trailing bytes");
    auto truncated = *encoded;
    truncated.pop_back();
    expect(!novacore::net::decodeSnapshot(truncated), "full snapshot rejects truncated component payload");
}

void testSnapshotDeltaBuildEncodeApply() {
    const auto baseline = baselineSnapshot();
    auto current = baseline;
    current.snapshotId = 41U;
    current.serverTick = {102U};
    current.acknowledgedInputTick = {100U};
    current.entities.erase(current.entities.begin() + 2);
    current.entities[0].components[0] = component(1U, 4U, {1U, 2U, 3U});
    current.entities[0].components.erase(current.entities[0].components.begin() + 1);
    current.entities[1] = entity(20U, 2U, {component(5U, 1U, {42U})});
    current.entities.push_back(entity(40U, 1U, {component(1U, 1U, {7U}), component(8U, 1U, {8U})}));

    const auto delta = novacore::net::buildSnapshotDelta(baseline, current);
    expect(delta.baselineId == baseline.snapshotId && delta.snapshotId == current.snapshotId,
        "snapshot delta names baseline and target");
    expect(delta.removedEntities == std::vector<novacore::net::NetworkEntityId>{30U},
        "snapshot delta contains removed entity");
    expect(delta.entityDeltas.size() == 3U, "snapshot delta includes changed recreated and new entities");
    expect(!delta.entityDeltas[0].created && delta.entityDeltas[0].removedComponents.size() == 1U,
        "snapshot delta removes individual components");
    expect(delta.entityDeltas[1].created && delta.entityDeltas[1].generation == 2U,
        "snapshot delta recreates generation changes");
    expect(delta.entityDeltas[2].created, "snapshot delta creates new entity");

    const auto encoded = novacore::net::encodeSnapshotDelta(delta);
    expect(encoded.has_value(), "snapshot delta encodes");
    const auto decoded = novacore::net::decodeSnapshotDelta(*encoded);
    expect(decoded && decoded.value->entityDeltas == delta.entityDeltas,
        "snapshot delta round trip preserves component operations");
    const auto applied = novacore::net::applySnapshotDelta(baseline, *decoded.value);
    expect(applied.has_value(), "snapshot delta applies to matching baseline");
    novacore::net::canonicalizeSnapshot(current);
    expect(applied == current, "snapshot delta reconstructs authoritative snapshot exactly");

    auto wrongBaseline = baseline;
    wrongBaseline.snapshotId = 39U;
    expect(!novacore::net::applySnapshotDelta(wrongBaseline, delta).has_value(),
        "snapshot delta rejects mismatched baseline");
}

void testSnapshotDeltaRejectsMalformedData() {
    novacore::net::SnapshotDelta invalid{};
    invalid.snapshotId = 2U;
    invalid.baselineId = 1U;
    invalid.removedEntities = {4U, 4U};
    novacore::net::canonicalizeSnapshotDelta(invalid);
    expect(novacore::net::validateSnapshotDelta(invalid) ==
            novacore::net::SnapshotValidationError::DuplicateRemovedEntity,
        "snapshot delta validation rejects duplicate entity removal");
    expect(!novacore::net::encodeSnapshotDelta(invalid).has_value(),
        "snapshot delta encoder rejects invalid operations");

    invalid.removedEntities.clear();
    novacore::net::EntitySnapshotDelta conflicting{};
    conflicting.entityId = 5U;
    conflicting.generation = 1U;
    conflicting.removedComponents = {3U};
    conflicting.changedComponents = {component(3U, 2U, {1U})};
    invalid.entityDeltas = {conflicting};
    novacore::net::canonicalizeSnapshotDelta(invalid);
    expect(novacore::net::validateSnapshotDelta(invalid) ==
            novacore::net::SnapshotValidationError::ConflictingComponentOperation,
        "snapshot delta rejects remove and change for same component");
}

void testSnapshotBaselineStoreRotation() {
    novacore::net::SnapshotBaselineStore store{3U};
    for (std::uint32_t id = 10U; id < 14U; ++id) {
        auto snapshot = baselineSnapshot();
        snapshot.snapshotId = id;
        expect(store.store(snapshot), "baseline store accepts valid snapshot");
    }
    expect(store.size() == 3U, "baseline store enforces capacity");
    expect(store.find(10U) == nullptr && store.find(13U) != nullptr,
        "baseline store rotates oldest snapshot");
    expect(store.newestSnapshotId() == 13U, "baseline store tracks newest snapshot");
    expect(store.acknowledgeThrough(12U) == 1U, "baseline store prunes snapshots older than ack");
    expect(store.find(12U) != nullptr, "baseline store retains acknowledged baseline itself");
}

void testFractionalInterpolationAndBoundedExtrapolation() {
    novacore::net::InterpolationBuffer<float, 8U> buffer;
    expect(buffer.store(100U, 10.0F), "interpolation buffer stores first state");
    expect(buffer.store(103U, 18.0F), "interpolation buffer stores second state");
    expect(buffer.store(106U, 26.0F), "interpolation buffer stores third state");

    const auto fractional = buffer.sampleFractional(101.5);
    expect(fractional.valid() && fractional.interpolating(), "fractional sample brackets snapshots");
    expectNear(fractional.alpha, 0.5, 0.0001, "fractional sample computes sub-tick alpha");
    const auto interpolated = *fractional.from + ((*fractional.to - *fractional.from) * fractional.alpha);
    expectNear(interpolated, 14.0, 0.001, "fractional sample supports state interpolation");

    const auto extrapolated = buffer.sampleFractional(107.5, 2.0);
    expect(extrapolated.valid() && extrapolated.extrapolating,
        "interpolation buffer allows bounded short extrapolation");
    expectNear(extrapolated.alpha, 1.5, 0.0001, "extrapolation alpha extends newest segment");
    const auto clamped = buffer.sampleFractional(110.0, 2.0);
    expect(clamped.valid() && clamped.clampedToNewest && !clamped.extrapolating,
        "interpolation buffer clamps excessive extrapolation");
    const auto oldest = buffer.sampleFractional(90.0);
    expect(oldest.valid() && oldest.clampedToOldest, "interpolation buffer clamps before history");
}

struct TestCommand final {
    float move = 0.0F;
};

struct TestState final {
    float position = 0.0F;
    float velocity = 0.0F;
};

double stateError(const TestState& predicted, const TestState& authoritative) {
    return std::abs(static_cast<double>(predicted.position - authoritative.position));
}

void testPredictionReconciliationPlan() {
    novacore::net::PredictionBuffer<TestCommand, TestState, 16U> predictions;
    for (std::uint64_t tick = 20U; tick <= 25U; ++tick) {
        expect(predictions.record(
            tick,
            TestCommand{1.0F},
            TestState{static_cast<float>(tick), 1.0F}),
            "prediction buffer records deterministic frame");
    }
    const auto correction = predictions.reconcile(
        22U,
        TestState{22.4F, 1.0F},
        stateError,
        0.1,
        1.0);
    expect(correction.status == novacore::net::ReconciliationStatus::CorrectionRequired,
        "reconciliation flags correction above tolerance");
    expect(correction.predictedState.has_value(), "reconciliation includes matching predicted state");
    expectNear(correction.error, 0.4, 0.001, "reconciliation measures authoritative error");
    expect(correction.discardedFrames == 3U, "reconciliation discards acknowledged prediction history");
    expect(correction.replayFrames.size() == 3U, "reconciliation returns pending commands for replay");
    expect(correction.replayFrames.front().tick == 23U && correction.replayFrames.back().tick == 25U,
        "reconciliation replay frames are ordered by tick");

    const auto hard = predictions.reconcile(
        24U,
        TestState{30.0F, 0.0F},
        stateError,
        0.1,
        1.0);
    expect(hard.status == novacore::net::ReconciliationStatus::HardCorrectionRequired,
        "reconciliation distinguishes hard correction");
    expect(hard.replayFrames.size() == 1U && hard.replayFrames.front().tick == 25U,
        "hard reconciliation preserves only commands newer than authoritative tick");

    const auto missing = predictions.reconcile(
        99U,
        TestState{},
        stateError,
        0.1,
        1.0);
    expect(missing.status == novacore::net::ReconciliationStatus::MissingPrediction,
        "reconciliation reports missing prediction explicitly");
    expect(predictions.size() == 0U, "missing authoritative tick acknowledges stale history");
}

void testServerTickEstimator() {
    novacore::net::ServerTickEstimator estimator{0.5};
    estimator.observe({100U, 106U});
    expectNear(estimator.offsetTicks(), 6.0, 0.001, "tick estimator initializes measured offset");
    estimator.observe({102U, 110U});
    expectNear(estimator.offsetTicks(), 7.0, 0.001, "tick estimator smooths offset changes");
    expectNear(estimator.estimateServerTick(120U), 127.0, 0.001, "tick estimator predicts server timeline");
    expectNear(estimator.renderTick(120U, 4.5), 122.5, 0.001,
        "tick estimator produces delayed interpolation timeline");
}

} // namespace

int main() {
    testStrictBitStreamSpanReads();
    testPacketEnvelopeRoundTripAndStrictFailures();
    testSequenceComparisonAcrossWrap();
    testReceiveAcknowledgementWindow();
    testSequenceChannelTracksSentAndAcknowledgedPackets();
    testFullSnapshotRoundTripAndCanonicalization();
    testSnapshotDeltaBuildEncodeApply();
    testSnapshotDeltaRejectsMalformedData();
    testSnapshotBaselineStoreRotation();
    testFractionalInterpolationAndBoundedExtrapolation();
    testPredictionReconciliationPlan();
    testServerTickEstimator();

    if (failures > 0) {
        std::cerr << failures << " NovaCore netcode test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "NovaCore netcode tests passed\n";
    return EXIT_SUCCESS;
}
