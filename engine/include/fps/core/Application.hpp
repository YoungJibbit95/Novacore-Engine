#pragma once

#include <chrono>
#include <cstdint>
#include <string>

namespace riftline::core {

struct ApplicationDesc final {
    std::string name = "Riftline";
    double fixedTickHz = 60.0;
    std::uint64_t maxFrames = 0;
};

struct FrameContext final {
    std::uint64_t frameIndex = 0;
    std::uint64_t tickIndex = 0;
    double deltaSeconds = 0.0;
    double fixedDeltaSeconds = 1.0 / 60.0;
    double alpha = 0.0;
};

class IApplicationDelegate {
public:
    virtual ~IApplicationDelegate() = default;
    virtual void onStartup() {}
    virtual void onShutdown() {}
    virtual void onFixedTick(const FrameContext& context) = 0;
    virtual void onFrame(const FrameContext& context) = 0;
    virtual bool shouldQuit() const { return false; }
};

class Application final {
public:
    explicit Application(ApplicationDesc desc);

    void run(IApplicationDelegate& delegate);
    void requestStop();

private:
    ApplicationDesc desc_;
    bool stopRequested_ = false;
};

} // namespace riftline::core

