#include "novacore/net/Protocol.hpp"

#include "novacore/net/BitStream.hpp"

#include <algorithm>
#include <limits>

namespace novacore::net {

namespace {

constexpr std::uint32_t kFnvOffset = 2166136261U;
constexpr std::uint32_t kFnvPrime = 16777619U;
constexpr std::uint8_t kKnownPacketFlags =
    static_cast<std::uint8_t>(PacketFlags::HasAcknowledgement) |
    static_cast<std::uint8_t>(PacketFlags::Reliable);

void hashByte(std::uint32_t& hash, std::uint8_t value) {
    hash ^= value;
    hash *= kFnvPrime;
}

template <typename Integer>
void hashInteger(std::uint32_t& hash, Integer value) {
    for (std::size_t shift = 0U; shift < sizeof(Integer) * 8U; shift += 8U) {
        hashByte(hash, static_cast<std::uint8_t>((value >> shift) & static_cast<Integer>(0xFFU)));
    }
}

[[nodiscard]] bool validPacketType(std::uint8_t value) {
    return value >= static_cast<std::uint8_t>(PacketType::InputCommands) &&
        value <= static_cast<std::uint8_t>(PacketType::KeepAlive);
}

[[nodiscard]] std::size_t sentSlot(PacketSequence sequence) {
    return static_cast<std::size_t>(sequence.value) % SequenceChannel::kSentHistoryCapacity;
}

} // namespace

bool sequenceMoreRecent(std::uint32_t lhs, std::uint32_t rhs) {
    if (lhs == rhs) {
        return false;
    }
    return (lhs > rhs && lhs - rhs <= std::numeric_limits<std::uint32_t>::max() / 2U) ||
        (rhs > lhs && rhs - lhs > std::numeric_limits<std::uint32_t>::max() / 2U);
}

std::uint32_t sequenceDistance(std::uint32_t newer, std::uint32_t older) {
    return newer - older;
}

std::uint32_t packetChecksum(
    const PacketHeader& header,
    std::span<const std::uint8_t> payload) {
    std::uint32_t hash = kFnvOffset;
    hashInteger(hash, kProtocolMagic);
    hashInteger(hash, kProtocolVersion);
    hashByte(hash, static_cast<std::uint8_t>(header.type));
    hashByte(hash, static_cast<std::uint8_t>(header.flags));
    hashInteger(hash, header.sequence.value);
    hashInteger(hash, header.acknowledgement.value);
    hashInteger(hash, header.acknowledgementBits);
    hashInteger(hash, header.simulationTick.value);
    hashInteger(hash, header.payloadBytes);
    for (const auto byte : payload) {
        hashByte(hash, byte);
    }
    return hash;
}

std::optional<std::vector<std::uint8_t>> encodePacket(
    PacketHeader header,
    std::span<const std::uint8_t> payload) {
    if (payload.size() > kMaxPacketPayloadBytes ||
        payload.size() > std::numeric_limits<std::uint32_t>::max()) {
        return std::nullopt;
    }
    const auto rawType = static_cast<std::uint8_t>(header.type);
    const auto rawFlags = static_cast<std::uint8_t>(header.flags);
    if (!validPacketType(rawType) || (rawFlags & ~kKnownPacketFlags) != 0U) {
        return std::nullopt;
    }

    header.payloadBytes = static_cast<std::uint32_t>(payload.size());
    PacketWriter writer;
    writer.writeU32(kProtocolMagic);
    writer.writeU16(kProtocolVersion);
    writer.writeU8(rawType);
    writer.writeU8(rawFlags);
    writer.writeU32(header.sequence.value);
    writer.writeU32(header.acknowledgement.value);
    writer.writeU32(header.acknowledgementBits);
    writer.writeU64(header.simulationTick.value);
    writer.writeU32(header.payloadBytes);
    writer.writeU32(packetChecksum(header, payload));
    writer.writeBytes(payload);
    return writer.finish();
}

PacketDecodeResult decodePacket(const std::vector<std::uint8_t>& bytes) {
    PacketDecodeResult result{};
    if (bytes.size() < kPacketHeaderBytes) {
        result.error = PacketDecodeError::Truncated;
        return result;
    }

    PacketReader reader(bytes);
    std::uint32_t magic = 0U;
    std::uint16_t version = 0U;
    std::uint8_t type = 0U;
    std::uint8_t flags = 0U;
    PacketHeader header{};
    std::uint32_t expectedChecksum = 0U;
    if (!reader.readU32(magic) || !reader.readU16(version) ||
        !reader.readU8(type) || !reader.readU8(flags) ||
        !reader.readU32(header.sequence.value) ||
        !reader.readU32(header.acknowledgement.value) ||
        !reader.readU32(header.acknowledgementBits) ||
        !reader.readU64(header.simulationTick.value) ||
        !reader.readU32(header.payloadBytes) ||
        !reader.readU32(expectedChecksum)) {
        result.error = PacketDecodeError::Truncated;
        return result;
    }
    if (magic != kProtocolMagic) {
        result.error = PacketDecodeError::InvalidMagic;
        return result;
    }
    if (version != kProtocolVersion) {
        result.error = PacketDecodeError::UnsupportedVersion;
        return result;
    }
    if (!validPacketType(type)) {
        result.error = PacketDecodeError::InvalidType;
        return result;
    }
    if ((flags & ~kKnownPacketFlags) != 0U) {
        result.error = PacketDecodeError::InvalidFlags;
        return result;
    }
    if (header.payloadBytes > kMaxPacketPayloadBytes) {
        result.error = PacketDecodeError::PayloadTooLarge;
        return result;
    }
    if (reader.remaining() != header.payloadBytes) {
        result.error = PacketDecodeError::SizeMismatch;
        return result;
    }

    header.type = static_cast<PacketType>(type);
    header.flags = static_cast<PacketFlags>(flags);
    const auto payload = reader.readByteSpan(header.payloadBytes);
    if (!payload.has_value() || !reader.consumed()) {
        result.error = PacketDecodeError::Truncated;
        return result;
    }
    if (packetChecksum(header, *payload) != expectedChecksum) {
        result.error = PacketDecodeError::ChecksumMismatch;
        return result;
    }

    DecodedPacket decoded{};
    decoded.header = header;
    decoded.payload.assign(payload->begin(), payload->end());
    result.packet = std::move(decoded);
    return result;
}

bool ReceiveSequenceTracker::observe(PacketSequence sequence) {
    if (!state_.valid) {
        state_.latest = sequence;
        state_.historyBits = 0U;
        state_.valid = true;
        return true;
    }
    if (sequence.value == state_.latest.value) {
        return false;
    }

    if (sequenceMoreRecent(sequence.value, state_.latest.value)) {
        const auto distance = sequenceDistance(sequence.value, state_.latest.value);
        if (distance > kAckHistoryBits) {
            state_.historyBits = 0U;
        } else if (distance == kAckHistoryBits) {
            state_.historyBits = 1U << (kAckHistoryBits - 1U);
        } else {
            state_.historyBits <<= distance;
            state_.historyBits |= 1U << (distance - 1U);
        }
        state_.latest = sequence;
        return true;
    }

    const auto distance = sequenceDistance(state_.latest.value, sequence.value);
    if (distance == 0U || distance > kAckHistoryBits) {
        return false;
    }
    const auto bit = 1U << (distance - 1U);
    if ((state_.historyBits & bit) != 0U) {
        return false;
    }
    state_.historyBits |= bit;
    return true;
}

bool ReceiveSequenceTracker::received(PacketSequence sequence) const {
    return acknowledgementContains(state_, sequence);
}

AcknowledgementState ReceiveSequenceTracker::acknowledgementState() const {
    return state_;
}

void ReceiveSequenceTracker::reset() {
    state_ = {};
}

bool acknowledgementContains(
    const AcknowledgementState& acknowledgement,
    PacketSequence sequence) {
    if (!acknowledgement.valid) {
        return false;
    }
    if (sequence.value == acknowledgement.latest.value) {
        return true;
    }
    if (sequenceMoreRecent(sequence.value, acknowledgement.latest.value)) {
        return false;
    }
    const auto distance = sequenceDistance(acknowledgement.latest.value, sequence.value);
    return distance >= 1U && distance <= kAckHistoryBits &&
        (acknowledgement.historyBits & (1U << (distance - 1U))) != 0U;
}

PacketSequence SequenceChannel::allocateSequence() {
    const auto allocated = nextSequence_;
    nextSequence_ = nextSequence_.next();
    return allocated;
}

std::optional<PacketHeader> SequenceChannel::makeHeader(
    PacketType type,
    SimulationTick tick,
    std::size_t payloadBytes,
    bool reliable) {
    if (payloadBytes > kMaxPacketPayloadBytes) {
        return std::nullopt;
    }
    PacketHeader header{};
    header.type = type;
    header.sequence = allocateSequence();
    header.simulationTick = tick;
    header.payloadBytes = static_cast<std::uint32_t>(payloadBytes);
    const auto acknowledgement = received_.acknowledgementState();
    if (acknowledgement.valid) {
        header.flags = header.flags | PacketFlags::HasAcknowledgement;
        header.acknowledgement = acknowledgement.latest;
        header.acknowledgementBits = acknowledgement.historyBits;
    }
    if (reliable) {
        header.flags = header.flags | PacketFlags::Reliable;
    }
    return header;
}

bool SequenceChannel::trackSent(const PacketHeader& header) {
    if (header.payloadBytes > kMaxPacketPayloadBytes ||
        !validPacketType(static_cast<std::uint8_t>(header.type))) {
        return false;
    }
    auto& slot = sentHistory_[sentSlot(header.sequence)];
    if (!slot.occupied) {
        ++sentHistorySize_;
    }
    slot.record = SentPacketRecord{
        .sequence = header.sequence,
        .simulationTick = header.simulationTick,
        .payloadBytes = header.payloadBytes,
        .reliable = hasFlag(header.flags, PacketFlags::Reliable),
        .acknowledged = false,
    };
    slot.occupied = true;
    return true;
}

bool SequenceChannel::observeReceived(PacketSequence sequence) {
    return received_.observe(sequence);
}

AcknowledgementReport SequenceChannel::processAcknowledgements(
    const AcknowledgementState& acknowledgement) {
    AcknowledgementReport report{};
    if (!acknowledgement.valid) {
        return report;
    }

    auto process = [&](PacketSequence sequence) {
        auto& slot = sentHistory_[sentSlot(sequence)];
        if (!slot.occupied || slot.record.sequence.value != sequence.value) {
            ++report.unknownAcknowledgements;
            return;
        }
        if (slot.record.acknowledged) {
            ++report.duplicateAcknowledgements;
            return;
        }
        slot.record.acknowledged = true;
        report.newlyAcknowledged.push_back(slot.record);
    };

    process(acknowledgement.latest);
    for (std::uint32_t bit = 0U; bit < kAckHistoryBits; ++bit) {
        if ((acknowledgement.historyBits & (1U << bit)) == 0U) {
            continue;
        }
        process(PacketSequence{acknowledgement.latest.value - (bit + 1U)});
    }
    return report;
}

AcknowledgementState SequenceChannel::outgoingAcknowledgement() const {
    return received_.acknowledgementState();
}

const SentPacketRecord* SequenceChannel::sent(PacketSequence sequence) const {
    const auto& slot = sentHistory_[sentSlot(sequence)];
    if (!slot.occupied || slot.record.sequence.value != sequence.value) {
        return nullptr;
    }
    return &slot.record;
}

std::size_t SequenceChannel::sentHistorySize() const {
    return sentHistorySize_;
}

void SequenceChannel::reset(PacketSequence nextSequence) {
    nextSequence_ = nextSequence;
    received_.reset();
    for (auto& slot : sentHistory_) {
        slot = {};
    }
    sentHistorySize_ = 0U;
}

} // namespace novacore::net
