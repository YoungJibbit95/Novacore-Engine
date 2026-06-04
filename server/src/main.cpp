#include "novacore/core/Application.hpp"
#include "novacore/core/Log.hpp"
#include "novacore/net/Server.hpp"

#include <string_view>

namespace {

class DedicatedServerApp final : public novacore::core::IApplicationDelegate {
public:
    DedicatedServerApp()
        : server_(novacore::net::ServerConfig{}) {
    }

    void onStartup() override {
        novacore::core::logInfo("server", "Dedicated server startup");
    }

    void onShutdown() override {
        novacore::core::logInfo("server", "Dedicated server shutdown");
    }

    void onFixedTick(const novacore::core::FrameContext& context) override {
        server_.tick(context);
    }

    void onFrame(const novacore::core::FrameContext& context) override {
        (void)context;
    }

private:
    novacore::net::ServerWorld server_;
};

} // namespace

int main(int argc, char** argv) {
    bool runForever = false;
    for (int i = 1; i < argc; ++i) {
        if (std::string_view(argv[i]) == "--forever") {
            runForever = true;
        }
    }

    DedicatedServerApp server;

    novacore::core::ApplicationDesc desc{};
    desc.name = "novacore_server";
    desc.fixedTickHz = 60.0;
    desc.maxFrames = runForever ? 0 : 120;

    novacore::core::Application app(desc);
    app.run(server);
    return 0;
}







