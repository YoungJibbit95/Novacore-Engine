#include "fps/ecs/World.hpp"

#include <memory>
#include <stdexcept>
#include <utility>

namespace riftline::ecs {

EntityId World::createEntity() {
    std::uint32_t index = 0;

    if (!freeList_.empty()) {
        index = freeList_.back();
        freeList_.pop_back();
    } else {
        index = static_cast<std::uint32_t>(slots_.size());
        slots_.push_back(Slot{});
    }

    Slot& slot = slots_[index];
    slot.alive = true;
    ++aliveCount_;

    return EntityId{index, slot.generation};
}

void World::destroyEntity(EntityId entity) {
    if (!isAlive(entity)) {
        return;
    }

    Slot& slot = slots_[entity.index];
    slot.alive = false;
    ++slot.generation;
    freeList_.push_back(entity.index);
    --aliveCount_;

    for (auto& [_, store] : stores_) {
        store->remove(entity);
    }
}

bool World::isAlive(EntityId entity) const {
    if (entity.isNull() || entity.index >= slots_.size()) {
        return false;
    }

    const Slot& slot = slots_[entity.index];
    return slot.alive && slot.generation == entity.generation;
}

std::size_t World::aliveCount() const {
    return aliveCount_;
}

} // namespace riftline::ecs

