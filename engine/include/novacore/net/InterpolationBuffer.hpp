#pragma once

#include "novacore/net/SequenceBuffer.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>

namespace novacore::net {

template <typename Value, std::size_t Capacity>
class InterpolationBuffer final {
public:
    struct Sample final {
        std::uint64_t targetSequence = 0;
        std::uint64_t fromSequence = 0;
        std::uint64_t toSequence = 0;
        const Value* from = nullptr;
        const Value* to = nullptr;
        float alpha = 0.0F;
        bool exact = false;
        bool clampedToOldest = false;
        bool clampedToNewest = false;
        bool extrapolating = false;
        double targetPosition = 0.0;
        double extrapolationDistance = 0.0;

        [[nodiscard]] bool valid() const {
            return from != nullptr && to != nullptr;
        }

        [[nodiscard]] bool interpolating() const {
            return valid() && from != to && !exact;
        }
    };

    [[nodiscard]] bool store(std::uint64_t sequence, Value value) {
        return buffer_.store(sequence, std::move(value));
    }

    [[nodiscard]] std::size_t eraseThrough(std::uint64_t sequence) {
        return buffer_.eraseThrough(sequence);
    }

    [[nodiscard]] std::size_t eraseAfter(std::uint64_t sequence) {
        return buffer_.eraseAfter(sequence);
    }

    void clear() {
        buffer_.clear();
    }

    [[nodiscard]] std::size_t size() const {
        return buffer_.size();
    }

    [[nodiscard]] constexpr std::size_t capacity() const {
        return Capacity;
    }

    [[nodiscard]] bool empty() const {
        return buffer_.empty();
    }

    [[nodiscard]] const Value* find(std::uint64_t sequence) const {
        return buffer_.find(sequence);
    }

    [[nodiscard]] std::optional<std::uint64_t> newestSequence() const {
        return buffer_.newestSequence();
    }

    [[nodiscard]] std::optional<std::uint64_t> oldestSequence() const {
        return buffer_.oldestSequence();
    }

    [[nodiscard]] Sample sample(std::uint64_t targetSequence) const {
        Sample result{};
        result.targetSequence = targetSequence;
        result.targetPosition = static_cast<double>(targetSequence);

        if (const auto* exact = buffer_.find(targetSequence); exact != nullptr) {
            result.fromSequence = targetSequence;
            result.toSequence = targetSequence;
            result.from = exact;
            result.to = exact;
            result.exact = true;
            return result;
        }

        const Value* lower = nullptr;
        const Value* upper = nullptr;
        std::uint64_t lowerSequence = 0;
        std::uint64_t upperSequence = 0;
        bool hasLower = false;
        bool hasUpper = false;

        buffer_.forEach([&](std::uint64_t sequence, const Value& value) {
            if (sequence < targetSequence && (!hasLower || sequence > lowerSequence)) {
                lower = &value;
                lowerSequence = sequence;
                hasLower = true;
            }
            if (sequence > targetSequence && (!hasUpper || sequence < upperSequence)) {
                upper = &value;
                upperSequence = sequence;
                hasUpper = true;
            }
        });

        if (hasLower && hasUpper) {
            result.fromSequence = lowerSequence;
            result.toSequence = upperSequence;
            result.from = lower;
            result.to = upper;
            const auto span = static_cast<float>(upperSequence - lowerSequence);
            result.alpha = span > 0.0F
                ? std::clamp(static_cast<float>(targetSequence - lowerSequence) / span, 0.0F, 1.0F)
                : 0.0F;
            return result;
        }

        if (hasLower) {
            result.fromSequence = lowerSequence;
            result.toSequence = lowerSequence;
            result.from = lower;
            result.to = lower;
            result.clampedToNewest = true;
            return result;
        }

        if (hasUpper) {
            result.fromSequence = upperSequence;
            result.toSequence = upperSequence;
            result.from = upper;
            result.to = upper;
            result.clampedToOldest = true;
            return result;
        }

        return result;
    }

    [[nodiscard]] Sample sampleFractional(
        double targetPosition,
        double maxExtrapolationDistance = 0.0) const {
        Sample result{};
        result.targetPosition = targetPosition;
        result.targetSequence = targetPosition > 0.0
            ? static_cast<std::uint64_t>(targetPosition)
            : 0U;
        if (buffer_.empty()) {
            return result;
        }

        const Value* lower = nullptr;
        const Value* upper = nullptr;
        const Value* secondNewest = nullptr;
        const Value* newest = nullptr;
        std::uint64_t lowerSequence = 0U;
        std::uint64_t upperSequence = 0U;
        std::uint64_t newestSequence = 0U;
        std::uint64_t secondNewestSequence = 0U;
        bool hasLower = false;
        bool hasUpper = false;
        bool hasNewest = false;
        bool hasSecondNewest = false;

        buffer_.forEach([&](std::uint64_t sequence, const Value& value) {
            const auto position = static_cast<double>(sequence);
            if (position <= targetPosition && (!hasLower || sequence > lowerSequence)) {
                lower = &value;
                lowerSequence = sequence;
                hasLower = true;
            }
            if (position >= targetPosition && (!hasUpper || sequence < upperSequence)) {
                upper = &value;
                upperSequence = sequence;
                hasUpper = true;
            }
            if (!hasNewest || sequence > newestSequence) {
                secondNewest = hasNewest ? newest : secondNewest;
                secondNewestSequence = hasNewest ? newestSequence : secondNewestSequence;
                hasSecondNewest = hasNewest;
                newest = &value;
                newestSequence = sequence;
                hasNewest = true;
            } else if ((!hasSecondNewest || sequence > secondNewestSequence) && sequence < newestSequence) {
                secondNewest = &value;
                secondNewestSequence = sequence;
                hasSecondNewest = true;
            }
        });

        if (hasLower && hasUpper) {
            result.fromSequence = lowerSequence;
            result.toSequence = upperSequence;
            result.from = lower;
            result.to = upper;
            if (lowerSequence == upperSequence) {
                result.exact = targetPosition == static_cast<double>(lowerSequence);
                return result;
            }
            const auto span = static_cast<double>(upperSequence - lowerSequence);
            result.alpha = static_cast<float>(std::clamp(
                (targetPosition - static_cast<double>(lowerSequence)) / span,
                0.0,
                1.0));
            return result;
        }

        if (hasLower) {
            const auto distance = targetPosition - static_cast<double>(lowerSequence);
            if (distance > 0.0 && distance <= maxExtrapolationDistance && hasSecondNewest &&
                secondNewestSequence < lowerSequence) {
                result.fromSequence = secondNewestSequence;
                result.toSequence = lowerSequence;
                result.from = secondNewest;
                result.to = lower;
                result.alpha = static_cast<float>(
                    (targetPosition - static_cast<double>(secondNewestSequence)) /
                    static_cast<double>(lowerSequence - secondNewestSequence));
                result.extrapolating = true;
                result.extrapolationDistance = distance;
                return result;
            }
            result.fromSequence = lowerSequence;
            result.toSequence = lowerSequence;
            result.from = lower;
            result.to = lower;
            result.clampedToNewest = true;
            return result;
        }

        if (hasUpper) {
            result.fromSequence = upperSequence;
            result.toSequence = upperSequence;
            result.from = upper;
            result.to = upper;
            result.clampedToOldest = true;
        }
        return result;
    }

private:
    SequenceBuffer<Value, Capacity> buffer_;
};

} // namespace novacore::net
