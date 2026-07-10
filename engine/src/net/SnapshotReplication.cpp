#include "novacore/net/SnapshotReplication.hpp"

#include "novacore/net/BitStream.hpp"
#include "novacore/net/Protocol.hpp"

#include <algorithm>
#include <unordered_set>
#include <utility>

namespace novacore::net {

namespace {

constexpr std::uint32_t kSnapshotMagic = 0x4E534653U;
constexpr std::uint32_t kDeltaMagic = 0x4E53444CU;
constexpr std::uint16_t kSnapshotCodecVersion = 1U;
constexpr std::uint8_t kEntityCreatedFlag = 1U << 0U;

template <typename Value, typename Key>
[[nodiscard]] const Value* findSorted(const std::vector<Value>& values, Key key, auto keyOf) {
    const auto it = std::lower_bound(values.begin(), values.end(), key, [&](const Value& value, Key expected) {
        return keyOf(value) < expected;
    });
    if (it == values.end() || keyOf(*it) != key) {
        return nullptr;
    }
    return &*it;
}

[[nodiscard]] const ReplicatedEntity* findEntity(
    const WorldSnapshot& snapshot,
    NetworkEntityId entityId) {
    return findSorted(snapshot.entities, entityId, [](const ReplicatedEntity& entity) {
        return entity.entityId;
    });
}

[[nodiscard]] const ReplicatedComponent* findComponent(
    const ReplicatedEntity& entity,
    ReplicatedComponentId componentId) {
    return findSorted(entity.components, componentId, [](const ReplicatedComponent& component) {
        return component.componentId;
    });
}

[[nodiscard]] SnapshotValidationError validateComponents(
    const std::vector<ReplicatedComponent>& components) {
    if (components.size() > kMaxComponentsPerEntity) {
        return SnapshotValidationError::TooManyComponents;
    }
    std::unordered_set<ReplicatedComponentId> componentIds;
    componentIds.reserve(components.size());
    for (const auto& component : components) {
        if (component.componentId == 0U) {
            return SnapshotValidationError::InvalidComponentId;
        }
        if (!componentIds.insert(component.componentId).second) {
            return SnapshotValidationError::DuplicateComponentId;
        }
        if (component.payload.size() > kMaxComponentPayloadBytes) {
            return SnapshotValidationError::ComponentPayloadTooLarge;
        }
    }
    return SnapshotValidationError::None;
}

void writeComponent(PacketWriter& writer, const ReplicatedComponent& component) {
    writer.writeU16(component.componentId);
    writer.writeU16(component.revision);
    writer.writeU32(static_cast<std::uint32_t>(component.payload.size()));
    writer.writeBytes(std::span<const std::uint8_t>(component.payload));
}

[[nodiscard]] bool readComponent(PacketReader& reader, ReplicatedComponent& component) {
    std::uint32_t payloadBytes = 0U;
    if (!reader.readU16(component.componentId) ||
        !reader.readU16(component.revision) ||
        !reader.readU32(payloadBytes) ||
        component.componentId == 0U ||
        payloadBytes > kMaxComponentPayloadBytes) {
        return false;
    }
    const auto payload = reader.readByteSpan(payloadBytes);
    if (!payload.has_value()) {
        return false;
    }
    component.payload.assign(payload->begin(), payload->end());
    return true;
}

[[nodiscard]] SnapshotDecodeError readCodecHeader(
    PacketReader& reader,
    std::uint32_t expectedMagic) {
    std::uint32_t magic = 0U;
    std::uint16_t version = 0U;
    if (!reader.readU32(magic) || !reader.readU16(version)) {
        return SnapshotDecodeError::Truncated;
    }
    if (magic != expectedMagic) {
        return SnapshotDecodeError::InvalidMagic;
    }
    if (version != kSnapshotCodecVersion) {
        return SnapshotDecodeError::UnsupportedVersion;
    }
    return SnapshotDecodeError::None;
}

} // namespace

SnapshotValidationError validateSnapshot(const WorldSnapshot& snapshot) {
    if (snapshot.entities.size() > kMaxSnapshotEntities) {
        return SnapshotValidationError::TooManyEntities;
    }
    std::unordered_set<NetworkEntityId> entityIds;
    entityIds.reserve(snapshot.entities.size());
    for (const auto& entity : snapshot.entities) {
        if (entity.entityId == 0U) {
            return SnapshotValidationError::InvalidEntityId;
        }
        if (!entityIds.insert(entity.entityId).second) {
            return SnapshotValidationError::DuplicateEntityId;
        }
        if (const auto error = validateComponents(entity.components);
            error != SnapshotValidationError::None) {
            return error;
        }
    }
    return SnapshotValidationError::None;
}

SnapshotValidationError validateSnapshotDelta(const SnapshotDelta& delta) {
    if (delta.removedEntities.size() > kMaxSnapshotEntities ||
        delta.entityDeltas.size() > kMaxSnapshotEntities) {
        return SnapshotValidationError::TooManyEntities;
    }
    std::unordered_set<NetworkEntityId> removedEntityIds;
    removedEntityIds.reserve(delta.removedEntities.size());
    for (const auto entityId : delta.removedEntities) {
        if (entityId == 0U) {
            return SnapshotValidationError::InvalidEntityId;
        }
        if (!removedEntityIds.insert(entityId).second) {
            return SnapshotValidationError::DuplicateRemovedEntity;
        }
    }

    std::unordered_set<NetworkEntityId> changedEntityIds;
    changedEntityIds.reserve(delta.entityDeltas.size());
    for (const auto& entity : delta.entityDeltas) {
        if (entity.entityId == 0U) {
            return SnapshotValidationError::InvalidEntityId;
        }
        if (!changedEntityIds.insert(entity.entityId).second) {
            return SnapshotValidationError::DuplicateEntityId;
        }
        if (removedEntityIds.contains(entity.entityId)) {
            return SnapshotValidationError::InvalidData;
        }
        if (!entity.created && entity.removedComponents.empty() && entity.changedComponents.empty()) {
            return SnapshotValidationError::EmptyEntityDelta;
        }
        if (entity.created && !entity.removedComponents.empty()) {
            return SnapshotValidationError::ConflictingComponentOperation;
        }
        if (entity.removedComponents.size() > kMaxComponentsPerEntity ||
            entity.changedComponents.size() > kMaxComponentsPerEntity) {
            return SnapshotValidationError::TooManyComponents;
        }
        std::unordered_set<ReplicatedComponentId> removedComponentIds;
        removedComponentIds.reserve(entity.removedComponents.size());
        for (const auto componentId : entity.removedComponents) {
            if (componentId == 0U) {
                return SnapshotValidationError::InvalidComponentId;
            }
            if (!removedComponentIds.insert(componentId).second) {
                return SnapshotValidationError::DuplicateRemovedComponent;
            }
            const auto conflicts = std::find_if(
                entity.changedComponents.begin(),
                entity.changedComponents.end(),
                [&](const ReplicatedComponent& component) {
                    return component.componentId == componentId;
                });
            if (conflicts != entity.changedComponents.end()) {
                return SnapshotValidationError::ConflictingComponentOperation;
            }
        }
        if (const auto error = validateComponents(entity.changedComponents);
            error != SnapshotValidationError::None) {
            return error;
        }
    }
    return SnapshotValidationError::None;
}

void canonicalizeSnapshot(WorldSnapshot& snapshot) {
    for (auto& entity : snapshot.entities) {
        std::sort(entity.components.begin(), entity.components.end(), [](const auto& lhs, const auto& rhs) {
            return lhs.componentId < rhs.componentId;
        });
    }
    std::sort(snapshot.entities.begin(), snapshot.entities.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.entityId < rhs.entityId;
    });
}

void canonicalizeSnapshotDelta(SnapshotDelta& delta) {
    std::sort(delta.removedEntities.begin(), delta.removedEntities.end());
    for (auto& entity : delta.entityDeltas) {
        std::sort(entity.removedComponents.begin(), entity.removedComponents.end());
        std::sort(entity.changedComponents.begin(), entity.changedComponents.end(), [](const auto& lhs, const auto& rhs) {
            return lhs.componentId < rhs.componentId;
        });
    }
    std::sort(delta.entityDeltas.begin(), delta.entityDeltas.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.entityId < rhs.entityId;
    });
}

SnapshotDelta buildSnapshotDelta(
    const WorldSnapshot& baselineInput,
    const WorldSnapshot& currentInput) {
    auto baseline = baselineInput;
    auto current = currentInput;
    canonicalizeSnapshot(baseline);
    canonicalizeSnapshot(current);

    SnapshotDelta delta{};
    delta.snapshotId = current.snapshotId;
    delta.baselineId = baseline.snapshotId;
    delta.serverTick = current.serverTick;
    delta.acknowledgedInputTick = current.acknowledgedInputTick;

    for (const auto& oldEntity : baseline.entities) {
        if (findEntity(current, oldEntity.entityId) == nullptr) {
            delta.removedEntities.push_back(oldEntity.entityId);
        }
    }
    for (const auto& currentEntity : current.entities) {
        const auto* oldEntity = findEntity(baseline, currentEntity.entityId);
        EntitySnapshotDelta entityDelta{};
        entityDelta.entityId = currentEntity.entityId;
        entityDelta.generation = currentEntity.generation;
        entityDelta.created = oldEntity == nullptr || oldEntity->generation != currentEntity.generation;
        if (entityDelta.created) {
            entityDelta.changedComponents = currentEntity.components;
            delta.entityDeltas.push_back(std::move(entityDelta));
            continue;
        }

        for (const auto& oldComponent : oldEntity->components) {
            if (findComponent(currentEntity, oldComponent.componentId) == nullptr) {
                entityDelta.removedComponents.push_back(oldComponent.componentId);
            }
        }
        for (const auto& currentComponent : currentEntity.components) {
            const auto* oldComponent = findComponent(*oldEntity, currentComponent.componentId);
            if (oldComponent == nullptr || *oldComponent != currentComponent) {
                entityDelta.changedComponents.push_back(currentComponent);
            }
        }
        if (!entityDelta.removedComponents.empty() || !entityDelta.changedComponents.empty()) {
            delta.entityDeltas.push_back(std::move(entityDelta));
        }
    }
    canonicalizeSnapshotDelta(delta);
    return delta;
}

std::optional<WorldSnapshot> applySnapshotDelta(
    const WorldSnapshot& baselineInput,
    const SnapshotDelta& deltaInput) {
    auto baseline = baselineInput;
    auto delta = deltaInput;
    canonicalizeSnapshot(baseline);
    canonicalizeSnapshotDelta(delta);
    if (validateSnapshot(baseline) != SnapshotValidationError::None ||
        validateSnapshotDelta(delta) != SnapshotValidationError::None ||
        baseline.snapshotId != delta.baselineId) {
        return std::nullopt;
    }

    WorldSnapshot result = baseline;
    result.snapshotId = delta.snapshotId;
    result.serverTick = delta.serverTick;
    result.acknowledgedInputTick = delta.acknowledgedInputTick;
    for (const auto removed : delta.removedEntities) {
        std::erase_if(result.entities, [&](const ReplicatedEntity& entity) {
            return entity.entityId == removed;
        });
    }
    for (const auto& change : delta.entityDeltas) {
        auto entityIt = std::lower_bound(result.entities.begin(), result.entities.end(), change.entityId,
            [](const ReplicatedEntity& entity, NetworkEntityId id) {
                return entity.entityId < id;
            });
        if (change.created) {
            ReplicatedEntity replacement{change.entityId, change.generation, change.changedComponents};
            if (entityIt != result.entities.end() && entityIt->entityId == change.entityId) {
                *entityIt = std::move(replacement);
            } else {
                result.entities.insert(entityIt, std::move(replacement));
            }
            continue;
        }
        if (entityIt == result.entities.end() || entityIt->entityId != change.entityId ||
            entityIt->generation != change.generation) {
            return std::nullopt;
        }
        for (const auto removed : change.removedComponents) {
            std::erase_if(entityIt->components, [&](const ReplicatedComponent& component) {
                return component.componentId == removed;
            });
        }
        for (const auto& component : change.changedComponents) {
            auto componentIt = std::lower_bound(entityIt->components.begin(), entityIt->components.end(),
                component.componentId, [](const ReplicatedComponent& value, ReplicatedComponentId id) {
                    return value.componentId < id;
                });
            if (componentIt != entityIt->components.end() && componentIt->componentId == component.componentId) {
                *componentIt = component;
            } else {
                entityIt->components.insert(componentIt, component);
            }
        }
    }
    canonicalizeSnapshot(result);
    if (validateSnapshot(result) != SnapshotValidationError::None) {
        return std::nullopt;
    }
    return result;
}

std::optional<std::vector<std::uint8_t>> encodeSnapshot(const WorldSnapshot& input) {
    auto snapshot = input;
    canonicalizeSnapshot(snapshot);
    if (validateSnapshot(snapshot) != SnapshotValidationError::None) {
        return std::nullopt;
    }
    PacketWriter writer;
    writer.writeU32(kSnapshotMagic);
    writer.writeU16(kSnapshotCodecVersion);
    writer.writeU32(snapshot.snapshotId);
    writer.writeU64(snapshot.serverTick.value);
    writer.writeU64(snapshot.acknowledgedInputTick.value);
    writer.writeU16(static_cast<std::uint16_t>(snapshot.entities.size()));
    for (const auto& entity : snapshot.entities) {
        writer.writeU32(entity.entityId);
        writer.writeU16(entity.generation);
        writer.writeU16(static_cast<std::uint16_t>(entity.components.size()));
        for (const auto& component : entity.components) {
            writeComponent(writer, component);
        }
    }
    if (writer.size() > kMaxSnapshotPayloadBytes) {
        return std::nullopt;
    }
    return writer.finish();
}

SnapshotDecodeResult<WorldSnapshot> decodeSnapshot(const std::vector<std::uint8_t>& bytes) {
    SnapshotDecodeResult<WorldSnapshot> result{};
    if (bytes.size() > kMaxSnapshotPayloadBytes) {
        result.error = SnapshotDecodeError::PayloadTooLarge;
        return result;
    }
    PacketReader reader(bytes);
    if (const auto error = readCodecHeader(reader, kSnapshotMagic); error != SnapshotDecodeError::None) {
        result.error = error;
        return result;
    }
    WorldSnapshot snapshot{};
    std::uint16_t entityCount = 0U;
    if (!reader.readU32(snapshot.snapshotId) ||
        !reader.readU64(snapshot.serverTick.value) ||
        !reader.readU64(snapshot.acknowledgedInputTick.value) ||
        !reader.readU16(entityCount)) {
        result.error = SnapshotDecodeError::Truncated;
        return result;
    }
    if (entityCount > kMaxSnapshotEntities) {
        result.error = SnapshotDecodeError::CountLimitExceeded;
        return result;
    }
    snapshot.entities.reserve(entityCount);
    for (std::uint16_t entityIndex = 0U; entityIndex < entityCount; ++entityIndex) {
        ReplicatedEntity entity{};
        std::uint16_t componentCount = 0U;
        if (!reader.readU32(entity.entityId) || !reader.readU16(entity.generation) ||
            !reader.readU16(componentCount)) {
            result.error = SnapshotDecodeError::Truncated;
            return result;
        }
        if (componentCount > kMaxComponentsPerEntity) {
            result.error = SnapshotDecodeError::CountLimitExceeded;
            return result;
        }
        entity.components.reserve(componentCount);
        for (std::uint16_t componentIndex = 0U; componentIndex < componentCount; ++componentIndex) {
            ReplicatedComponent component{};
            if (!readComponent(reader, component)) {
                result.error = SnapshotDecodeError::Truncated;
                return result;
            }
            entity.components.push_back(std::move(component));
        }
        snapshot.entities.push_back(std::move(entity));
    }
    if (!reader.consumed()) {
        result.error = SnapshotDecodeError::TrailingData;
        return result;
    }
    canonicalizeSnapshot(snapshot);
    if (validateSnapshot(snapshot) != SnapshotValidationError::None) {
        result.error = SnapshotDecodeError::InvalidData;
        return result;
    }
    result.value = std::move(snapshot);
    return result;
}

std::optional<std::vector<std::uint8_t>> encodeSnapshotDelta(const SnapshotDelta& input) {
    auto delta = input;
    canonicalizeSnapshotDelta(delta);
    if (validateSnapshotDelta(delta) != SnapshotValidationError::None) {
        return std::nullopt;
    }
    PacketWriter writer;
    writer.writeU32(kDeltaMagic);
    writer.writeU16(kSnapshotCodecVersion);
    writer.writeU32(delta.snapshotId);
    writer.writeU32(delta.baselineId);
    writer.writeU64(delta.serverTick.value);
    writer.writeU64(delta.acknowledgedInputTick.value);
    writer.writeU16(static_cast<std::uint16_t>(delta.removedEntities.size()));
    writer.writeU16(static_cast<std::uint16_t>(delta.entityDeltas.size()));
    for (const auto removed : delta.removedEntities) {
        writer.writeU32(removed);
    }
    for (const auto& entity : delta.entityDeltas) {
        writer.writeU32(entity.entityId);
        writer.writeU16(entity.generation);
        writer.writeU8(entity.created ? kEntityCreatedFlag : 0U);
        writer.writeU8(static_cast<std::uint8_t>(entity.removedComponents.size()));
        writer.writeU8(static_cast<std::uint8_t>(entity.changedComponents.size()));
        for (const auto removed : entity.removedComponents) {
            writer.writeU16(removed);
        }
        for (const auto& component : entity.changedComponents) {
            writeComponent(writer, component);
        }
    }
    if (writer.size() > kMaxSnapshotPayloadBytes) {
        return std::nullopt;
    }
    return writer.finish();
}

SnapshotDecodeResult<SnapshotDelta> decodeSnapshotDelta(const std::vector<std::uint8_t>& bytes) {
    SnapshotDecodeResult<SnapshotDelta> result{};
    if (bytes.size() > kMaxSnapshotPayloadBytes) {
        result.error = SnapshotDecodeError::PayloadTooLarge;
        return result;
    }
    PacketReader reader(bytes);
    if (const auto error = readCodecHeader(reader, kDeltaMagic); error != SnapshotDecodeError::None) {
        result.error = error;
        return result;
    }
    SnapshotDelta delta{};
    std::uint16_t removedEntityCount = 0U;
    std::uint16_t entityDeltaCount = 0U;
    if (!reader.readU32(delta.snapshotId) || !reader.readU32(delta.baselineId) ||
        !reader.readU64(delta.serverTick.value) ||
        !reader.readU64(delta.acknowledgedInputTick.value) ||
        !reader.readU16(removedEntityCount) || !reader.readU16(entityDeltaCount)) {
        result.error = SnapshotDecodeError::Truncated;
        return result;
    }
    if (removedEntityCount > kMaxSnapshotEntities || entityDeltaCount > kMaxSnapshotEntities) {
        result.error = SnapshotDecodeError::CountLimitExceeded;
        return result;
    }
    delta.removedEntities.reserve(removedEntityCount);
    for (std::uint16_t i = 0U; i < removedEntityCount; ++i) {
        NetworkEntityId entityId = 0U;
        if (!reader.readU32(entityId)) {
            result.error = SnapshotDecodeError::Truncated;
            return result;
        }
        delta.removedEntities.push_back(entityId);
    }
    delta.entityDeltas.reserve(entityDeltaCount);
    for (std::uint16_t entityIndex = 0U; entityIndex < entityDeltaCount; ++entityIndex) {
        EntitySnapshotDelta entity{};
        std::uint8_t flags = 0U;
        std::uint8_t removedCount = 0U;
        std::uint8_t changedCount = 0U;
        if (!reader.readU32(entity.entityId) || !reader.readU16(entity.generation) ||
            !reader.readU8(flags) || !reader.readU8(removedCount) || !reader.readU8(changedCount)) {
            result.error = SnapshotDecodeError::Truncated;
            return result;
        }
        if ((flags & ~kEntityCreatedFlag) != 0U ||
            removedCount > kMaxComponentsPerEntity || changedCount > kMaxComponentsPerEntity) {
            result.error = SnapshotDecodeError::InvalidData;
            return result;
        }
        entity.created = (flags & kEntityCreatedFlag) != 0U;
        entity.removedComponents.reserve(removedCount);
        for (std::uint8_t i = 0U; i < removedCount; ++i) {
            ReplicatedComponentId componentId = 0U;
            if (!reader.readU16(componentId)) {
                result.error = SnapshotDecodeError::Truncated;
                return result;
            }
            entity.removedComponents.push_back(componentId);
        }
        entity.changedComponents.reserve(changedCount);
        for (std::uint8_t i = 0U; i < changedCount; ++i) {
            ReplicatedComponent component{};
            if (!readComponent(reader, component)) {
                result.error = SnapshotDecodeError::Truncated;
                return result;
            }
            entity.changedComponents.push_back(std::move(component));
        }
        delta.entityDeltas.push_back(std::move(entity));
    }
    if (!reader.consumed()) {
        result.error = SnapshotDecodeError::TrailingData;
        return result;
    }
    canonicalizeSnapshotDelta(delta);
    if (validateSnapshotDelta(delta) != SnapshotValidationError::None) {
        result.error = SnapshotDecodeError::InvalidData;
        return result;
    }
    result.value = std::move(delta);
    return result;
}

SnapshotBaselineStore::SnapshotBaselineStore(std::size_t capacity)
    : capacity_(std::max<std::size_t>(1U, capacity)) {
    snapshots_.reserve(capacity_);
}

bool SnapshotBaselineStore::store(WorldSnapshot snapshot) {
    canonicalizeSnapshot(snapshot);
    if (validateSnapshot(snapshot) != SnapshotValidationError::None) {
        return false;
    }
    const auto existing = std::find_if(snapshots_.begin(), snapshots_.end(), [&](const WorldSnapshot& value) {
        return value.snapshotId == snapshot.snapshotId;
    });
    if (existing != snapshots_.end()) {
        *existing = std::move(snapshot);
    } else {
        snapshots_.push_back(std::move(snapshot));
    }
    std::sort(snapshots_.begin(), snapshots_.end(), [](const WorldSnapshot& lhs, const WorldSnapshot& rhs) {
        return sequenceMoreRecent(rhs.snapshotId, lhs.snapshotId);
    });
    while (snapshots_.size() > capacity_) {
        snapshots_.erase(snapshots_.begin());
    }
    return true;
}

const WorldSnapshot* SnapshotBaselineStore::find(SnapshotId snapshotId) const {
    const auto it = std::find_if(snapshots_.begin(), snapshots_.end(), [&](const WorldSnapshot& snapshot) {
        return snapshot.snapshotId == snapshotId;
    });
    return it == snapshots_.end() ? nullptr : &*it;
}

bool SnapshotBaselineStore::erase(SnapshotId snapshotId) {
    const auto oldSize = snapshots_.size();
    std::erase_if(snapshots_, [&](const WorldSnapshot& snapshot) {
        return snapshot.snapshotId == snapshotId;
    });
    return snapshots_.size() != oldSize;
}

std::size_t SnapshotBaselineStore::acknowledgeThrough(SnapshotId snapshotId) {
    const auto oldSize = snapshots_.size();
    std::erase_if(snapshots_, [&](const WorldSnapshot& snapshot) {
        return snapshot.snapshotId != snapshotId && !sequenceMoreRecent(snapshot.snapshotId, snapshotId);
    });
    return oldSize - snapshots_.size();
}

std::optional<SnapshotId> SnapshotBaselineStore::newestSnapshotId() const {
    if (snapshots_.empty()) {
        return std::nullopt;
    }
    return snapshots_.back().snapshotId;
}

std::size_t SnapshotBaselineStore::size() const {
    return snapshots_.size();
}

std::size_t SnapshotBaselineStore::capacity() const {
    return capacity_;
}

void SnapshotBaselineStore::clear() {
    snapshots_.clear();
}

} // namespace novacore::net
