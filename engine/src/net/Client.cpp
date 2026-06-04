#include "fps/net/Client.hpp"

#include "fps/core/Log.hpp"

#include <utility>

namespace riftline::net {

void ClientSession::connectDirect(std::string host, std::uint16_t port) {
    host_ = std::move(host);
    port_ = port;
    state_ = ClientConnectionState::Connecting;
    core::logInfo("net", "Client direct connect requested");
}

void ClientSession::disconnect() {
    state_ = ClientConnectionState::Disconnected;
    host_.clear();
    port_ = 0;
}

ClientConnectionState ClientSession::state() const {
    return state_;
}

const std::string& ClientSession::host() const {
    return host_;
}

std::uint16_t ClientSession::port() const {
    return port_;
}

} // namespace riftline::net

