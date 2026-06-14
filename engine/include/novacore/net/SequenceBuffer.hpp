#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <type_traits>
#include <utility>

namespace novacore::net {

template <typename Value, std::size_t Capacity>
class SequenceBuffer final {
    static_assert(Capacity > 0U, "SequenceBuffer capacity must be greater than zero");

public:
    struct Entry final {
        std::uint64_t sequence = 0;
        Value value{};
        bool occupied = false;
    };

    [[nodiscard]] constexpr std::size_t capacity() const {
        return Capacity;
    }

    [[nodiscard]] std::size_t size() const {
        return size_;
    }

    [[nodiscard]] bool empty() const {
        return size_ == 0U;
    }

    [[nodiscard]] std::optional<std::uint64_t> newestSequence() const {
        if (!hasNewest_) {
            return std::nullopt;
        }
        return newestSequence_;
    }

    [[nodiscard]] std::optional<std::uint64_t> oldestSequence() const {
        if (size_ == 0U) {
            return std::nullopt;
        }

        std::uint64_t oldest = 0;
        bool hasOldest = false;
        for (const auto& entry : entries_) {
            if (!entry.occupied) {
                continue;
            }
            if (!hasOldest || entry.sequence < oldest) {
                oldest = entry.sequence;
                hasOldest = true;
            }
        }
        return hasOldest ? std::optional<std::uint64_t>{oldest} : std::nullopt;
    }

    [[nodiscard]] bool tooOld(std::uint64_t sequence) const {
        return hasNewest_ &&
            newestSequence_ >= static_cast<std::uint64_t>(Capacity) &&
            sequence <= newestSequence_ - static_cast<std::uint64_t>(Capacity);
    }

    [[nodiscard]] bool contains(std::uint64_t sequence) const {
        const auto& entry = entries_[slotFor(sequence)];
        return entry.occupied && entry.sequence == sequence;
    }

    [[nodiscard]] const Value* find(std::uint64_t sequence) const {
        const auto& entry = entries_[slotFor(sequence)];
        if (!entry.occupied || entry.sequence != sequence) {
            return nullptr;
        }
        return &entry.value;
    }

    [[nodiscard]] Value* find(std::uint64_t sequence) {
        auto& entry = entries_[slotFor(sequence)];
        if (!entry.occupied || entry.sequence != sequence) {
            return nullptr;
        }
        return &entry.value;
    }

    [[nodiscard]] bool store(std::uint64_t sequence, Value value) {
        if (tooOld(sequence)) {
            return false;
        }

        if (!hasNewest_ || sequence > newestSequence_) {
            newestSequence_ = sequence;
            hasNewest_ = true;
            expireBefore(windowFloor());
        }

        auto& entry = entries_[slotFor(sequence)];
        if (!entry.occupied) {
            ++size_;
        }
        entry.sequence = sequence;
        entry.value = std::move(value);
        entry.occupied = true;
        return true;
    }

    [[nodiscard]] bool erase(std::uint64_t sequence) {
        auto& entry = entries_[slotFor(sequence)];
        if (!entry.occupied || entry.sequence != sequence) {
            return false;
        }
        entry.occupied = false;
        --size_;
        return true;
    }

    [[nodiscard]] std::size_t eraseThrough(std::uint64_t sequence) {
        std::size_t erased = 0;
        for (auto& entry : entries_) {
            if (!entry.occupied || entry.sequence > sequence) {
                continue;
            }
            entry.occupied = false;
            ++erased;
        }
        size_ -= erased;
        return erased;
    }

    [[nodiscard]] std::size_t eraseAfter(std::uint64_t sequence) {
        std::size_t erased = 0;
        for (auto& entry : entries_) {
            if (!entry.occupied || entry.sequence <= sequence) {
                continue;
            }
            entry.occupied = false;
            ++erased;
        }
        size_ -= erased;
        if (erased > 0U) {
            recomputeNewest();
        }
        return erased;
    }

    template <typename Visitor>
    void forEach(Visitor&& visitor) const {
        for (const auto& entry : entries_) {
            if (!entry.occupied) {
                continue;
            }
            if constexpr (std::is_invocable_v<Visitor, std::uint64_t, const Value&>) {
                visitor(entry.sequence, entry.value);
            } else {
                visitor(entry);
            }
        }
    }

    void clear() {
        for (auto& entry : entries_) {
            entry.occupied = false;
        }
        size_ = 0;
        newestSequence_ = 0;
        hasNewest_ = false;
    }

private:
    [[nodiscard]] static constexpr std::size_t slotFor(std::uint64_t sequence) {
        return static_cast<std::size_t>(sequence % static_cast<std::uint64_t>(Capacity));
    }

    [[nodiscard]] std::uint64_t windowFloor() const {
        if (!hasNewest_ || newestSequence_ + 1U <= static_cast<std::uint64_t>(Capacity)) {
            return 0;
        }
        return newestSequence_ + 1U - static_cast<std::uint64_t>(Capacity);
    }

    void expireBefore(std::uint64_t sequence) {
        for (auto& entry : entries_) {
            if (!entry.occupied || entry.sequence >= sequence) {
                continue;
            }
            entry.occupied = false;
            --size_;
        }
    }

    void recomputeNewest() {
        std::uint64_t newest = 0;
        bool hasNewest = false;
        for (const auto& entry : entries_) {
            if (!entry.occupied) {
                continue;
            }
            if (!hasNewest || entry.sequence > newest) {
                newest = entry.sequence;
                hasNewest = true;
            }
        }
        newestSequence_ = newest;
        hasNewest_ = hasNewest;
    }

    std::array<Entry, Capacity> entries_{};
    std::size_t size_ = 0;
    std::uint64_t newestSequence_ = 0;
    bool hasNewest_ = false;
};

} // namespace novacore::net
