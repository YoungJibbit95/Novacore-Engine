#pragma once

#include "novacore/net/SequenceBuffer.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace novacore::net {

enum class ReconciliationStatus {
    MissingPrediction,
    AcceptedPrediction,
    CorrectionRequired,
    HardCorrectionRequired,
};

template <typename Command, typename State>
struct PredictionFrame final {
    std::uint64_t tick = 0U;
    Command command{};
    State predictedState{};
};

template <typename Command, typename State>
struct ReconciliationPlan final {
    ReconciliationStatus status = ReconciliationStatus::MissingPrediction;
    std::uint64_t authoritativeTick = 0U;
    std::optional<State> predictedState;
    State authoritativeState{};
    double error = 0.0;
    std::size_t discardedFrames = 0U;
    std::vector<PredictionFrame<Command, State>> replayFrames;

    [[nodiscard]] bool needsCorrection() const {
        return status == ReconciliationStatus::CorrectionRequired ||
            status == ReconciliationStatus::HardCorrectionRequired;
    }
};

template <typename Command, typename State, std::size_t Capacity>
class PredictionBuffer final {
public:
    using Frame = PredictionFrame<Command, State>;
    using Plan = ReconciliationPlan<Command, State>;

    static_assert(Capacity > 1U, "PredictionBuffer requires at least two frames");

    [[nodiscard]] bool record(std::uint64_t tick, Command command, State predictedState) {
        if (hasNewestTick_ && tick < newestTick_ && frames_.find(tick) == nullptr) {
            return false;
        }
        Frame frame{
            .tick = tick,
            .command = std::move(command),
            .predictedState = std::move(predictedState),
        };
        if (!frames_.store(tick, std::move(frame))) {
            return false;
        }
        if (!hasNewestTick_ || tick > newestTick_) {
            newestTick_ = tick;
            hasNewestTick_ = true;
        }
        return true;
    }

    [[nodiscard]] const Frame* find(std::uint64_t tick) const {
        return frames_.find(tick);
    }

    [[nodiscard]] std::size_t size() const {
        return frames_.size();
    }

    [[nodiscard]] std::optional<std::uint64_t> newestTick() const {
        return hasNewestTick_ ? std::optional<std::uint64_t>{newestTick_} : std::nullopt;
    }

    [[nodiscard]] std::size_t acknowledgeThrough(std::uint64_t tick) {
        const auto erased = frames_.eraseThrough(tick);
        if (!hasAcknowledgedTick_ || tick > acknowledgedTick_) {
            acknowledgedTick_ = tick;
            hasAcknowledgedTick_ = true;
        }
        return erased;
    }

    [[nodiscard]] std::optional<std::uint64_t> acknowledgedTick() const {
        return hasAcknowledgedTick_ ? std::optional<std::uint64_t>{acknowledgedTick_} : std::nullopt;
    }

    template <typename ErrorMeasure>
    [[nodiscard]] Plan reconcile(
        std::uint64_t authoritativeTick,
        State authoritativeState,
        ErrorMeasure&& errorMeasure,
        double correctionThreshold,
        double hardCorrectionThreshold) {
        Plan plan{};
        plan.authoritativeTick = authoritativeTick;
        plan.authoritativeState = std::move(authoritativeState);
        const auto* predicted = frames_.find(authoritativeTick);
        if (predicted == nullptr) {
            plan.discardedFrames = acknowledgeThrough(authoritativeTick);
            collectReplayFrames(authoritativeTick, plan.replayFrames);
            return plan;
        }

        plan.predictedState = predicted->predictedState;
        plan.error = std::max(
            0.0,
            static_cast<double>(errorMeasure(*plan.predictedState, plan.authoritativeState)));
        const auto correction = std::max(0.0, correctionThreshold);
        const auto hardCorrection = std::max(correction, hardCorrectionThreshold);
        if (plan.error > hardCorrection) {
            plan.status = ReconciliationStatus::HardCorrectionRequired;
        } else if (plan.error > correction) {
            plan.status = ReconciliationStatus::CorrectionRequired;
        } else {
            plan.status = ReconciliationStatus::AcceptedPrediction;
        }
        plan.discardedFrames = acknowledgeThrough(authoritativeTick);
        collectReplayFrames(authoritativeTick, plan.replayFrames);
        return plan;
    }

    [[nodiscard]] std::vector<Frame> pendingAfter(std::uint64_t tick) const {
        std::vector<Frame> pending;
        collectReplayFrames(tick, pending);
        return pending;
    }

    void clear() {
        frames_.clear();
        newestTick_ = 0U;
        acknowledgedTick_ = 0U;
        hasNewestTick_ = false;
        hasAcknowledgedTick_ = false;
    }

private:
    void collectReplayFrames(std::uint64_t tick, std::vector<Frame>& output) const {
        frames_.forEach([&](std::uint64_t frameTick, const Frame& frame) {
            if (frameTick > tick) {
                output.push_back(frame);
            }
        });
        std::sort(output.begin(), output.end(), [](const Frame& lhs, const Frame& rhs) {
            return lhs.tick < rhs.tick;
        });
    }

    SequenceBuffer<Frame, Capacity> frames_{};
    std::uint64_t newestTick_ = 0U;
    std::uint64_t acknowledgedTick_ = 0U;
    bool hasNewestTick_ = false;
    bool hasAcknowledgedTick_ = false;
};

struct TickClockSample final {
    std::uint64_t localTick = 0U;
    std::uint64_t serverTick = 0U;
};

class ServerTickEstimator final {
public:
    explicit ServerTickEstimator(double smoothing = 0.1)
        : smoothing_(std::clamp(smoothing, 0.0, 1.0)) {
    }

    void observe(TickClockSample sample) {
        const auto measuredOffset = static_cast<double>(sample.serverTick) -
            static_cast<double>(sample.localTick);
        if (!initialized_) {
            offsetTicks_ = measuredOffset;
            initialized_ = true;
        } else {
            offsetTicks_ += (measuredOffset - offsetTicks_) * smoothing_;
        }
        lastSample_ = sample;
    }

    [[nodiscard]] double estimateServerTick(std::uint64_t localTick) const {
        return static_cast<double>(localTick) + offsetTicks_;
    }

    [[nodiscard]] double renderTick(std::uint64_t localTick, double interpolationDelayTicks) const {
        return estimateServerTick(localTick) - std::max(0.0, interpolationDelayTicks);
    }

    [[nodiscard]] double offsetTicks() const {
        return offsetTicks_;
    }

    [[nodiscard]] bool initialized() const {
        return initialized_;
    }

    [[nodiscard]] std::optional<TickClockSample> lastSample() const {
        return initialized_ ? std::optional<TickClockSample>{lastSample_} : std::nullopt;
    }

    void reset() {
        offsetTicks_ = 0.0;
        initialized_ = false;
        lastSample_ = {};
    }

private:
    double smoothing_ = 0.1;
    double offsetTicks_ = 0.0;
    bool initialized_ = false;
    TickClockSample lastSample_{};
};

} // namespace novacore::net
