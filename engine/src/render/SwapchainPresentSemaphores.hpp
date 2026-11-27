#pragma once

#include <cassert>
#include <cstddef>
#include <vector>

namespace novacore::render::detail {

// The acquire wait orders reuse for the same image. A graphics frame-slot
// fence does not track completion of the presentation engine's wait.
template <typename Handle>
class SwapchainPresentSemaphores final {
public:
    template <typename Create, typename Destroy>
    [[nodiscard]] bool create(std::size_t imageCount, Create&& create, Destroy&& destroy) {
        assert(semaphores_.empty());
        if (imageCount == 0) {
            return false;
        }
        semaphores_.resize(imageCount);
        for (auto& semaphore : semaphores_) {
            semaphore = create();
            if (semaphore == Handle{}) {
                clear(destroy);
                return false;
            }
        }
        return true;
    }

    template <typename Destroy>
    void clear(Destroy&& destroy) noexcept {
        for (const auto semaphore : semaphores_) {
            if (semaphore != Handle{}) {
                destroy(semaphore);
            }
        }
        semaphores_.clear();
    }

    [[nodiscard]] Handle forImage(std::size_t acquiredImageIndex) const noexcept {
        assert(acquiredImageIndex < semaphores_.size());
        return semaphores_[acquiredImageIndex];
    }

private:
    std::vector<Handle> semaphores_;
};

} // namespace novacore::render::detail
