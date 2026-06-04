#include "novacore/core/FixedStep.hpp"

#include <algorithm>

namespace novacore::core {

FixedStepAccumulator::FixedStepAccumulator(FixedStepConfig config) {
    if (config.tickHz <= 0.0) {
        config.tickHz = 60.0;
    }
    if (config.maxFrameSeconds <= 0.0) {
        config.maxFrameSeconds = 0.25;
    }

    fixedDeltaSeconds_ = 1.0 / config.tickHz;
    maxFrameSeconds_ = config.maxFrameSeconds;
}

void FixedStepAccumulator::advance(double deltaSeconds) {
    accumulatedSeconds_ += std::clamp(deltaSeconds, 0.0, maxFrameSeconds_);
}

bool FixedStepAccumulator::shouldStep() const {
    return accumulatedSeconds_ >= fixedDeltaSeconds_;
}

void FixedStepAccumulator::consumeStep() {
    if (!shouldStep()) {
        return;
    }

    accumulatedSeconds_ -= fixedDeltaSeconds_;
    ++consumedSteps_;
}

double FixedStepAccumulator::alpha() const {
    if (fixedDeltaSeconds_ <= 0.0) {
        return 0.0;
    }
    return accumulatedSeconds_ / fixedDeltaSeconds_;
}

double FixedStepAccumulator::fixedDeltaSeconds() const {
    return fixedDeltaSeconds_;
}

std::uint64_t FixedStepAccumulator::consumedSteps() const {
    return consumedSteps_;
}

} // namespace novacore::core

