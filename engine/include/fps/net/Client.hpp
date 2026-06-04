#pragma once

#include <cstdint>
#include <string>

namespace riftline::net {

enum class ClientConnectionState {
    Disconnected,
    Connecting,
    Connected
};

class ClientSession final {
public:
    void connectDirect(std::string host, std::uint16_t port);
    void disconnect();

    [[nodiscard]] ClientConnectionState state() const;
    [[nodiscard]] const std::string& host() const;
    [[nodiscard]] std::uint16_t port() const;

private:
    ClientConnectionState state_ = ClientConnectionState::Disconnected;
    std::string host_;
    std::uint16_t port_ = 0;
};

} // namespace riftline::net

