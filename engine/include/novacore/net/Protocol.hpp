#pragma once

#include "novacore/net/Types.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace novacore::net {

constexpr std::uint32_t kProtocolMagic = 0x4E564E43U;
constexpr std::uint16_t kProtocolVersion = 1U;
constexpr std::size_t kPacketHeaderBytes = 36U;
constexpr std::size_t kMaxPacketPayloadBytes = 64U * 1024U;
constexpr std::size_t kAckHistoryBits = 32U;

enum class PacketType : std::uint8_t {
    InputCommands = 1,
    Snapshot = 2,
    ReliableEvent = 3,
    Handshake = 4,
    KeepAlive = 5,
};

enum class PacketFlags : std::uint8_t {
    None = 0,
    HasAcknowledgement = 1U << 0U,
    Reliable = 1U << 1U,
};

[[nodiscard]] constexpr PacketFlags operator|(PacketFlags lhs, PacketFlags rhs) {
    return static_cast<PacketFlags>(
        static_cast<std::uint8_t>(lhs) | static_cast<std::uint8_t>(rhs));
}

[[nodiscard]] constexpr bool hasFlag(PacketFlags value, PacketFlags flag) {
    return (static_cast<std::uint8_t>(value) & static_cast<std::uint8_t>(flag)) != 0U;
}

struct PacketHeader final {
    PacketType type = PacketType::KeepAlive;
    PacketFlags flags = PacketFlags::None;
    PacketSequence sequence{};
    PacketSequence acknowledgement{};
    std::uint32_t acknowledgementBits = 0U;
    SimulationTick simulationTick{};
    std::uint32_t payloadBytes = 0U;
};

struct DecodedPacket final {
    PacketHeader header{};
    std::vector<std::uint8_t> payload;
};

enum class PacketDecodeError {
    None,
    Truncated,
    InvalidMagic,
    UnsupportedVersion,
    InvalidType,
    InvalidFlags,
    PayloadTooLarge,
    SizeMismatch,
    ChecksumMismatch,
};

struct PacketDecodeResult final {
    std::optional<DecodedPacket> packet;
    PacketDecodeError error = PacketDecodeError::None;

    [[nodiscard]] explicit operator bool() const {
        return packet.has_value();
    }
};

[[nodiscard]] bool sequenceMoreRecent(std::uint32_t lhs, std::uint32_t rhs);
[[nodiscard]] std::uint32_t sequenceDistance(std::uint32_t newer, std::uint32_t older);
[[nodiscard]] std::uint32_t packetChecksum(
    const PacketHeader& header,
    std::span<const std::uint8_t> payload);
[[nodiscard]] std::optional<std::vector<std::uint8_t>> encodePacket(
    PacketHeader header,
    std::span<const std::uint8_t> payload);
[[nodiscard]] PacketDecodeResult decodePacket(const std::vector<std::uint8_t>& bytes);

struct AcknowledgementState final {
    PacketSequence latest{};
    std::uint32_t historyBits = 0U;
    bool valid = false;
};

class ReceiveSequenceTracker final {
public:
    [[nodiscard]] bool observe(PacketSequence sequence);
    [[nodiscard]] bool received(PacketSequence sequence) const;
    [[nodiscard]] AcknowledgementState acknowledgementState() const;
    void reset();

private:
    AcknowledgementState state_{};
};

[[nodiscard]] bool acknowledgementContains(
    const AcknowledgementState& acknowledgement,
    PacketSequence sequence);

struct SentPacketRecord final {
    PacketSequence sequence{};
    SimulationTick simulationTick{};
    std::uint32_t payloadBytes = 0U;
    bool reliable = false;
    bool acknowledged = false;
};

struct AcknowledgementReport final {
    std::vector<SentPacketRecord> newlyAcknowledged;
    std::size_t duplicateAcknowledgements = 0U;
    std::size_t unknownAcknowledgements = 0U;
};

class SequenceChannel final {
public:
    static constexpr std::size_t kSentHistoryCapacity = 1024U;

    [[nodiscard]] PacketSequence allocateSequence();
    [[nodiscard]] std::optional<PacketHeader> makeHeader(
        PacketType type,
        SimulationTick tick,
        std::size_t payloadBytes,
        bool reliable = false);
    [[nodiscard]] bool trackSent(const PacketHeader& header);
    [[nodiscard]] bool observeReceived(PacketSequence sequence);
    [[nodiscard]] AcknowledgementReport processAcknowledgements(
        const AcknowledgementState& acknowledgement);
    [[nodiscard]] AcknowledgementState outgoingAcknowledgement() const;
    [[nodiscard]] const SentPacketRecord* sent(PacketSequence sequence) const;
    [[nodiscard]] std::size_t sentHistorySize() const;
    void reset(PacketSequence nextSequence = {});

private:
    struct SentSlot final {
        SentPacketRecord record{};
        bool occupied = false;
    };

    PacketSequence nextSequence_{};
    ReceiveSequenceTracker received_{};
    std::vector<SentSlot> sentHistory_{kSentHistoryCapacity};
    std::size_t sentHistorySize_ = 0U;
};

} // namespace novacore::net
