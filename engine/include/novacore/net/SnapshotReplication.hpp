#pragma once

#include "novacore/net/Types.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace novacore::net {

constexpr std::size_t kMaxSnapshotEntities = 4096U;
constexpr std::size_t kMaxComponentsPerEntity = 64U;
constexpr std::size_t kMaxComponentPayloadBytes = 64U * 1024U;
constexpr std::size_t kMaxSnapshotPayloadBytes = 4U * 1024U * 1024U;

using SnapshotId = std::uint32_t;
using NetworkEntityId = std::uint32_t;
using ReplicatedComponentId = std::uint16_t;

struct ReplicatedComponent final {
    ReplicatedComponentId componentId = 0U;
    std::uint16_t revision = 0U;
    std::vector<std::uint8_t> payload;

    [[nodiscard]] bool operator==(const ReplicatedComponent&) const = default;
};

struct ReplicatedEntity final {
    NetworkEntityId entityId = 0U;
    std::uint16_t generation = 0U;
    std::vector<ReplicatedComponent> components;

    [[nodiscard]] bool operator==(const ReplicatedEntity&) const = default;
};

struct WorldSnapshot final {
    SnapshotId snapshotId = 0U;
    SimulationTick serverTick{};
    SimulationTick acknowledgedInputTick{};
    std::vector<ReplicatedEntity> entities;

    [[nodiscard]] bool operator==(const WorldSnapshot&) const = default;
};

struct EntitySnapshotDelta final {
    NetworkEntityId entityId = 0U;
    std::uint16_t generation = 0U;
    bool created = false;
    std::vector<ReplicatedComponentId> removedComponents;
    std::vector<ReplicatedComponent> changedComponents;

    [[nodiscard]] bool operator==(const EntitySnapshotDelta&) const = default;
};

struct SnapshotDelta final {
    SnapshotId snapshotId = 0U;
    SnapshotId baselineId = 0U;
    SimulationTick serverTick{};
    SimulationTick acknowledgedInputTick{};
    std::vector<NetworkEntityId> removedEntities;
    std::vector<EntitySnapshotDelta> entityDeltas;

    [[nodiscard]] bool operator==(const SnapshotDelta&) const = default;
};

enum class SnapshotValidationError {
    None,
    TooManyEntities,
    InvalidEntityId,
    DuplicateEntityId,
    TooManyComponents,
    InvalidComponentId,
    DuplicateComponentId,
    ComponentPayloadTooLarge,
    DuplicateRemovedEntity,
    DuplicateRemovedComponent,
    ConflictingComponentOperation,
    EmptyEntityDelta,
    BaselineMismatch,
    InvalidData,
};

enum class SnapshotDecodeError {
    None,
    Truncated,
    InvalidMagic,
    UnsupportedVersion,
    PayloadTooLarge,
    CountLimitExceeded,
    InvalidData,
    TrailingData,
};

template <typename Value>
struct SnapshotDecodeResult final {
    std::optional<Value> value;
    SnapshotDecodeError error = SnapshotDecodeError::None;

    [[nodiscard]] explicit operator bool() const {
        return value.has_value();
    }
};

[[nodiscard]] SnapshotValidationError validateSnapshot(const WorldSnapshot& snapshot);
[[nodiscard]] SnapshotValidationError validateSnapshotDelta(const SnapshotDelta& delta);
void canonicalizeSnapshot(WorldSnapshot& snapshot);
void canonicalizeSnapshotDelta(SnapshotDelta& delta);

[[nodiscard]] SnapshotDelta buildSnapshotDelta(
    const WorldSnapshot& baseline,
    const WorldSnapshot& current);
[[nodiscard]] std::optional<WorldSnapshot> applySnapshotDelta(
    const WorldSnapshot& baseline,
    const SnapshotDelta& delta);

[[nodiscard]] std::optional<std::vector<std::uint8_t>> encodeSnapshot(
    const WorldSnapshot& snapshot);
[[nodiscard]] SnapshotDecodeResult<WorldSnapshot> decodeSnapshot(
    const std::vector<std::uint8_t>& bytes);
[[nodiscard]] std::optional<std::vector<std::uint8_t>> encodeSnapshotDelta(
    const SnapshotDelta& delta);
[[nodiscard]] SnapshotDecodeResult<SnapshotDelta> decodeSnapshotDelta(
    const std::vector<std::uint8_t>& bytes);

class SnapshotBaselineStore final {
public:
    explicit SnapshotBaselineStore(std::size_t capacity = 32U);

    [[nodiscard]] bool store(WorldSnapshot snapshot);
    [[nodiscard]] const WorldSnapshot* find(SnapshotId snapshotId) const;
    [[nodiscard]] bool erase(SnapshotId snapshotId);
    [[nodiscard]] std::size_t acknowledgeThrough(SnapshotId snapshotId);
    [[nodiscard]] std::optional<SnapshotId> newestSnapshotId() const;
    [[nodiscard]] std::size_t size() const;
    [[nodiscard]] std::size_t capacity() const;
    void clear();

private:
    std::size_t capacity_ = 0U;
    std::vector<WorldSnapshot> snapshots_;
};

} // namespace novacore::net
