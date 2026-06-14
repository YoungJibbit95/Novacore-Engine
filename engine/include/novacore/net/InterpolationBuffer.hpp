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

private:
    SequenceBuffer<Value, Capacity> buffer_;
};

} // namespace novacore::net
