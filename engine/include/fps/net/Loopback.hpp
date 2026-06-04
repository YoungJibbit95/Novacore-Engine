#pragma once

#include <cstdint>
#include <queue>
#include <span>
#include <vector>

namespace novacore::net {

struct Packet final {
    std::uint32_t sequence = 0;
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







