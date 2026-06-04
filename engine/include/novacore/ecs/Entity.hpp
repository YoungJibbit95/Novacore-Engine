#pragma once

#include <cstdint>
#include <limits>

namespace novacore::ecs {

struct EntityId final {
    std::uint32_t index = std::numeric_limits<std::uint32_t>::max();
    std::uint32_t generation = 0;

    [[nodiscard]] bool isNull() const {
        return index == std::numeric_limits<std::uint32_t>::max();
    }

    [[nodiscard]] std::uint64_t packed() const {
        return (static_cast<std::uint64_t>(generation) << 32U) | index;
    }
};

inline bool operator==(EntityId lhs, EntityId rhs) {
    return lhs.index == rhs.index && lhs.generation == rhs.generation;
}

inline bool operator!=(EntityId lhs, EntityId rhs) {
    return !(lhs == rhs);
}

} // namespace novacore::ecs







