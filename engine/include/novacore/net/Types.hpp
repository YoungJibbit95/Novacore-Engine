#pragma once

#include <cstdint>

namespace novacore::net {

struct SimulationTick final {
    std::uint64_t value = 0;
};

struct PacketSequence final {
    std::uint32_t value = 0;

    [[nodiscard]] constexpr PacketSequence next() const {
        return PacketSequence{value + 1U};
    }
};

struct NetworkRate final {
    std::uint16_t simulationHz = 60;
    std::uint16_t snapshotHz = 30;
};

inline bool operator==(SimulationTick lhs, SimulationTick rhs) {
    return lhs.value == rhs.value;
}

inline bool operator==(PacketSequence lhs, PacketSequence rhs) {
    return lhs.value == rhs.value;
}

} // namespace novacore::net

