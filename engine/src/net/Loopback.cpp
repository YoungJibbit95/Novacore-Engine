#include "novacore/net/Loopback.hpp"

#include <utility>

namespace novacore::net {

void LoopbackChannel::sendToServer(Packet packet) {
    clientToServer_.push(std::move(packet));
}

void LoopbackChannel::sendToClient(Packet packet) {
    serverToClient_.push(std::move(packet));
}

bool LoopbackChannel::tryReceiveForServer(Packet& outPacket) {
    if (clientToServer_.empty()) {
        return false;
    }
    outPacket = std::move(clientToServer_.front());
    clientToServer_.pop();
    return true;
}

bool LoopbackChannel::tryReceiveForClient(Packet& outPacket) {
    if (serverToClient_.empty()) {
        return false;
    }
    outPacket = std::move(serverToClient_.front());
    serverToClient_.pop();
    return true;
}

} // namespace novacore::net







