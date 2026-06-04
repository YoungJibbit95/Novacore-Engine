#include "fps/net/Server.hpp"

#include "fps/core/Log.hpp"

namespace riftline::net {

ServerWorld::ServerWorld(ServerConfig config)
    : config_(config) {
    core::logInfo("net", "Server world created");
}

void ServerWorld::tick(const core::FrameContext& context) {
    (void)context;
    ++tickCount_;
}

std::uint64_t ServerWorld::tickCount() const {
    return tickCount_;
}

const ServerConfig& ServerWorld::config() const {
    return config_;
}

} // namespace riftline::net

