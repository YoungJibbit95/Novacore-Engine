#pragma once

#include "novacore/core/Application.hpp"
#include "novacore/net/Types.hpp"

#include <cstdint>

namespace novacore::net {

struct ServerConfig final {
    double tickHz = 60.0;
    std::uint16_t port = 27015;
    std::uint16_t maxPlayers = 12;
    NetworkRate rate{};
    bool listenServer = false;
};

class ServerWorld final {
public:
    explicit ServerWorld(ServerConfig config);

    void tick(const core::FrameContext& context);
    [[nodiscard]] std::uint64_t tickCount() const;
    [[nodiscard]] const ServerConfig& config() const;

private:
    ServerConfig config_;
    std::uint64_t tickCount_ = 0;
};

} // namespace novacore::net







