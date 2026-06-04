#pragma once

#include "novacore/net/Types.hpp"

#include <cstdint>
#include <queue>
#include <vector>

namespace novacore::net {

struct Packet final {
    PacketSequence sequence{};
    std::vector<std::uint8_t> payload;
};

class LoopbackChannel final {
public:
    void sendToServer(Packet packet);
    void sendToClient(Packet packet);

    [[nodiscard]] bool tryReceiveForServer(Packet& outPacket);
    [[nodiscard]] bool tryReceiveForClient(Packet& outPacket);

private:
    std::queue<Packet> clientToServer_;
    std::queue<Packet> serverToClient_;
};

} // namespace novacore::net







