#pragma once

#include "novacore/ecs/Components.hpp"
#include "novacore/ecs/Entity.hpp"

#include <cstddef>
#include <memory>
#include <optional>
#include <stdexcept>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace novacore::ecs {

class World final {
public:
    EntityId createEntity();
    void destroyEntity(EntityId entity);

    [[nodiscard]] bool isAlive(EntityId entity) const;
    [[nodiscard]] std::size_t aliveCount() const;

    template <typename TComponent>
    TComponent& addComponent(EntityId entity, TComponent component);

    template <typename TComponent>
    [[nodiscard]] TComponent* getComponent(EntityId entity);

    template <typename TComponent>
    [[nodiscard]] const TComponent* getComponent(EntityId entity) const;

    template <typename TComponent>
    void removeComponent(EntityId entity);

private:
    struct Slot final {
        std::uint32_t generation = 1;
        bool alive = false;
    };

    struct IComponentStore {
        virtual ~IComponentStore() = default;
        virtual void remove(EntityId entity) = 0;
    };

    template <typename TComponent>
    struct ComponentStore final : IComponentStore {
        std::unordered_map<std::uint64_t, TComponent> values;

        void remove(EntityId entity) override {
            values.erase(entity.packed());
        }
    };

    template <typename TComponent>
    ComponentStore<TComponent>& store();

    template <typename TComponent>
    const ComponentStore<TComponent>* storeIfExists() const;

    std::vector<Slot> slots_;
    std::vector<std::uint32_t> freeList_;
    std::unordered_map<std::type_index, std::unique_ptr<IComponentStore>> stores_;
    std::size_t aliveCount_ = 0;
};

template <typename TComponent>
TComponent& World::addComponent(EntityId entity, TComponent component) {
    if (!isAlive(entity)) {
        throw std::runtime_error("Cannot add component to dead entity");
    }
    auto& componentStore = store<TComponent>();
    auto [it, inserted] = componentStore.values.insert_or_assign(entity.packed(), std::move(component));
    (void)inserted;
    return it->second;
}

template <typename TComponent>
TComponent* World::getComponent(EntityId entity) {
    auto& componentStore = store<TComponent>();
    auto it = componentStore.values.find(entity.packed());
    if (it == componentStore.values.end()) {
        return nullptr;
    }
    return &it->second;
}

template <typename TComponent>
const TComponent* World::getComponent(EntityId entity) const {
    const auto* componentStore = storeIfExists<TComponent>();
    if (componentStore == nullptr) {
        return nullptr;
    }
    auto it = componentStore->values.find(entity.packed());
    if (it == componentStore->values.end()) {
        return nullptr;
    }
    return &it->second;
}

template <typename TComponent>
void World::removeComponent(EntityId entity) {
    auto& componentStore = store<TComponent>();
    componentStore.values.erase(entity.packed());
}

template <typename TComponent>
World::ComponentStore<TComponent>& World::store() {
    const std::type_index key(typeid(TComponent));
    auto it = stores_.find(key);
    if (it == stores_.end()) {
        auto inserted = stores_.emplace(key, std::make_unique<ComponentStore<TComponent>>());
        it = inserted.first;
    }
    return *static_cast<ComponentStore<TComponent>*>(it->second.get());
}

template <typename TComponent>
const World::ComponentStore<TComponent>* World::storeIfExists() const {
    const std::type_index key(typeid(TComponent));
    auto it = stores_.find(key);
    if (it == stores_.end()) {
        return nullptr;
    }
    return static_cast<const ComponentStore<TComponent>*>(it->second.get());
}

} // namespace novacore::ecs






