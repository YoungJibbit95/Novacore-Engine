#include "fps/core/Application.hpp"

#include "fps/core/Log.hpp"
#include "fps/core/Time.hpp"

#include <algorithm>
#include <thread>
#include <utility>

namespace riftline::core {

Application::Application(ApplicationDesc desc)
    : desc_(std::move(desc)) {
    if (desc_.fixedTickHz <= 0.0) {
        desc_.fixedTickHz = 60.0;
    }
}

void Application::run(IApplicationDelegate& delegate) {
    logInfo("core", "Application startup: " + desc_.name);

    delegate.onStartup();

    const double fixedDelta = 1.0 / desc_.fixedTickHz;
    auto previous = Clock::now();
    double accumulator = 0.0;
    FrameContext context{};
    context.fixedDeltaSeconds = fixedDelta;

    while (!stopRequested_ && !delegate.shouldQuit()) {
        const auto now = Clock::now();
        const double deltaSeconds = std::chrono::duration_cast<Seconds>(now - previous).count();
        previous = now;

        context.deltaSeconds = std::clamp(deltaSeconds, 0.0, 0.25);
        accumulator += context.deltaSeconds;

        while (accumulator >= fixedDelta) {
            delegate.onFixedTick(context);
            accumulator -= fixedDelta;
            ++context.tickIndex;
        }

        context.alpha = accumulator / fixedDelta;
        delegate.onFrame(context);
        ++context.frameIndex;

        if (desc_.maxFrames > 0 && context.frameIndex >= desc_.maxFrames) {
            break;
        }

        std::this_thread::yield();
    }

    delegate.onShutdown();
    logInfo("core", "Application shutdown: " + desc_.name);
}

void Application::requestStop() {
    stopRequested_ = true;
}

} // namespace riftline::core
