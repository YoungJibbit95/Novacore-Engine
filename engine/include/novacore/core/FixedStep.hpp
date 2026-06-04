#pragma once

#include <cstdint>

namespace novacore::core {

struct FixedStepConfig final {
    double tickHz = 60.0;
    double maxFrameSeconds = 0.25;
};

class FixedStepAccumulator final {
public:
    explicit FixedStepAccumulator(FixedStepConfig config);

    void advance(double deltaSeconds);
    [[nodiscard]] bool shouldStep() const;
    void consumeStep();

    [[nodiscard]] double alpha() const;
    [[nodiscard]] double fixedDeltaSeconds() const;
    [[nodiscard]] std::uint64_t consumedSteps() const;

private:
    double fixedDeltaSeconds_ = 1.0 / 60.0;
    double maxFrameSeconds_ = 0.25;
    double accumulatedSeconds_ = 0.0;
    std::uint64_t consumedSteps_ = 0;
};

} // namespace novacore::core

