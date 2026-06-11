#include "novacore/render/Renderer.hpp"

#include "VulkanBackend.hpp"

#include "novacore/core/Log.hpp"

#if NOVACORE_HAS_SDL3
#include <SDL3/SDL.h>
#endif

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace novacore::render {

namespace {

[[nodiscard]] std::uint8_t colorByte(float value) {
    return static_cast<std::uint8_t>(std::clamp(value, 0.0F, 1.0F) * 255.0F);
}

#if NOVACORE_HAS_SDL3
void setDrawColor(SDL_Renderer* renderer, std::array<float, 4> color) {
    SDL_SetRenderDrawColor(
        renderer,
        colorByte(color[0]),
        colorByte(color[1]),
        colorByte(color[2]),
        colorByte(color[3]));
}

[[nodiscard]] std::array<std::uint8_t, 7> glyphRows(char raw) {
    const char c = static_cast<char>(std::toupper(static_cast<unsigned char>(raw)));
    switch (c) {
    case 'A': return {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11};
    case 'B': return {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E};
    case 'C': return {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E};
    case 'D': return {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E};
    case 'E': return {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F};
    case 'F': return {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10};
    case 'G': return {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F};
    case 'H': return {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11};
    case 'I': return {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E};
    case 'J': return {0x07, 0x02, 0x02, 0x02, 0x12, 0x12, 0x0C};
    case 'K': return {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11};
    case 'L': return {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F};
    case 'M': return {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11};
    case 'N': return {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11};
    case 'O': return {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E};
    case 'P': return {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10};
    case 'Q': return {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D};
    case 'R': return {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11};
    case 'S': return {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E};
    case 'T': return {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04};
    case 'U': return {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E};
    case 'V': return {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04};
    case 'W': return {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A};
    case 'X': return {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11};
    case 'Y': return {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04};
    case 'Z': return {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F};
    case '0': return {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E};
    case '1': return {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E};
    case '2': return {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F};
    case '3': return {0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E};
    case '4': return {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02};
    case '5': return {0x1F, 0x10, 0x10, 0x1E, 0x01, 0x01, 0x1E};
    case '6': return {0x0E, 0x10, 0x10, 0x1E, 0x11, 0x11, 0x0E};
    case '7': return {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08};
    case '8': return {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E};
    case '9': return {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x01, 0x0E};
    case ':': return {0x00, 0x04, 0x04, 0x00, 0x04, 0x04, 0x00};
    case '.': return {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C};
    case '-': return {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00};
    case '/': return {0x01, 0x01, 0x02, 0x04, 0x08, 0x10, 0x10};
    case '>': return {0x10, 0x08, 0x04, 0x02, 0x04, 0x08, 0x10};
    case '<': return {0x01, 0x02, 0x04, 0x08, 0x04, 0x02, 0x01};
    default: return {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    }
}

void fillRect(SDL_Renderer* renderer, float x, float y, float width, float height) {
    const SDL_FRect rect{x, y, width, height};
    SDL_RenderFillRect(renderer, &rect);
}

void drawText(SDL_Renderer* renderer, const DebugText& text) {
    setDrawColor(renderer, text.color);
    float cursorX = text.x;
    const float pixel = std::max(1.0F, text.scale);
    const float step = pixel * 6.0F;

    for (const char c : text.text) {
        if (c == ' ') {
            cursorX += step;
            continue;
        }

        const auto rows = glyphRows(c);
        for (std::size_t row = 0; row < rows.size(); ++row) {
            for (std::uint8_t col = 0; col < 5; ++col) {
                const auto bit = static_cast<std::uint8_t>(1U << (4U - col));
                if ((rows[row] & bit) == 0) {
                    continue;
                }
                fillRect(
                    renderer,
                    cursorX + static_cast<float>(col) * pixel,
                    text.y + static_cast<float>(row) * pixel,
                    pixel,
                    pixel);
            }
        }
        cursorX += step;
    }
}
#endif

struct MeshResourceSlot final {
    MeshResourceHandle handle{};
    std::string assetId;
    std::shared_ptr<const assets::GltfMeshData> meshData;
    bool alive = false;
};

} // namespace

struct MeshResourceRegistry final {
    [[nodiscard]] MeshResourceHandle registerResource(
        std::string assetId,
        const assets::GltfMeshData& meshData) {
        if (assetId.empty() || meshData.primitives.empty()) {
            return {};
        }

        if (const auto existing = find(assetId); existing.isValid()) {
            return existing;
        }

        std::uint32_t slotIndex = 0;
        if (!freeList.empty()) {
            slotIndex = freeList.back();
            freeList.pop_back();
        } else {
            slotIndex = static_cast<std::uint32_t>(slots.size());
            slots.push_back({});
        }

        auto& slot = slots[slotIndex];
        const std::uint32_t nextGeneration = slot.handle.generation == 0 ? 1 : slot.handle.generation + 1;
        slot.handle = MeshResourceHandle{slotIndex, nextGeneration};
        slot.assetId = std::move(assetId);
        slot.meshData = std::make_shared<assets::GltfMeshData>(meshData);
        slot.alive = true;
        handlesByAssetId.emplace(slot.assetId, slot.handle);
        return slot.handle;
    }

    void release(MeshResourceHandle handle) {
        auto* slot = mutableSlot(handle);
        if (slot == nullptr) {
            return;
        }

        handlesByAssetId.erase(slot->assetId);
        slot->assetId.clear();
        slot->meshData.reset();
        slot->alive = false;
        freeList.push_back(handle.index);
    }

    [[nodiscard]] MeshResourceHandle find(std::string_view assetId) const {
        const auto it = handlesByAssetId.find(std::string(assetId));
        if (it == handlesByAssetId.end()) {
            return {};
        }
        return view(it->second).has_value() ? it->second : MeshResourceHandle{};
    }

    [[nodiscard]] std::optional<MeshResourceView> view(MeshResourceHandle handle) const {
        const auto* slot = constSlot(handle);
        if (slot == nullptr || slot->meshData == nullptr) {
            return std::nullopt;
        }
        return MeshResourceView{slot->handle, slot->assetId, slot->meshData};
    }

    [[nodiscard]] std::vector<MeshResourceView> views() const {
        std::vector<MeshResourceView> result;
        result.reserve(slots.size());
        for (const auto& slot : slots) {
            if (!slot.alive || slot.meshData == nullptr) {
                continue;
            }
            result.push_back(MeshResourceView{slot.handle, slot.assetId, slot.meshData});
        }
        return result;
    }

    [[nodiscard]] MeshResourceStats stats() const {
        MeshResourceStats result{};
        for (const auto& slot : slots) {
            if (!slot.alive || slot.meshData == nullptr) {
                continue;
            }

            ++result.registeredResources;
            result.totalPrimitives += slot.meshData->primitiveCount();
            result.totalVertices += slot.meshData->vertexCount();
            result.totalIndices += slot.meshData->indexCount();
        }
        return result;
    }

private:
    [[nodiscard]] const MeshResourceSlot* constSlot(MeshResourceHandle handle) const {
        if (!handle.isValid() || handle.index >= slots.size()) {
            return nullptr;
        }
        const auto& slot = slots[handle.index];
        if (!slot.alive || slot.handle != handle) {
            return nullptr;
        }
        return &slot;
    }

    [[nodiscard]] MeshResourceSlot* mutableSlot(MeshResourceHandle handle) {
        if (!handle.isValid() || handle.index >= slots.size()) {
            return nullptr;
        }
        auto& slot = slots[handle.index];
        if (!slot.alive || slot.handle != handle) {
            return nullptr;
        }
        return &slot;
    }

    std::vector<MeshResourceSlot> slots;
    std::vector<std::uint32_t> freeList;
    std::unordered_map<std::string, MeshResourceHandle> handlesByAssetId;
};

Renderer::Renderer()
    : meshResources_(std::make_unique<MeshResourceRegistry>()) {}

Renderer::~Renderer() {
    shutdown();
}

bool Renderer::create(platform::Window& window, const RendererCreateInfo& info) {
    clearColor_ = info.clearColor;
    vulkanRuntime_ = probeVulkanRuntime();
    vulkanSummary_ = vulkanRuntimeSummary(vulkanRuntime_);

    if (vulkanRuntime_.usable()) {
        core::logInfo("render", "Vulkan runtime detected: " + vulkanSummary_);
    } else {
        core::logWarning("render", "Vulkan runtime unavailable: " + vulkanSummary_);
        for (const auto& error : vulkanRuntime_.errors) {
            core::logWarning("render", "Vulkan probe: " + error);
        }
    }

#if NOVACORE_HAS_VULKAN && NOVACORE_HAS_SDL3
    if (info.preferVulkan && !window.isHeadless()) {
        vulkanBackend_ = std::make_unique<VulkanBackend>();
        if (vulkanBackend_->create(window, clearColor_)) {
            for (const auto& resource : meshResources_->views()) {
                vulkanBackend_->registerMeshResource(resource);
            }
            ready_ = true;
            vulkanCapable_ = true;
            core::logInfo("render", "Vulkan backend active: " + std::string(vulkanBackend_->deviceName()));
            return true;
        }
        vulkanBackend_.reset();
        if (info.requireVulkan) {
            core::logWarning("render", "Vulkan backend failed and Vulkan is required; SDL debug fallback is disabled");
            ready_ = true;
            vulkanCapable_ = false;
            return true;
        }
        core::logWarning("render", "Vulkan backend failed; falling back to SDL debug/null renderer");
    }
#endif

    if (info.requireVulkan && !window.isHeadless()) {
        core::logWarning("render", "Vulkan backend required but unavailable in this build/runtime; SDL debug fallback is disabled");
        ready_ = true;
        vulkanCapable_ = false;
        return true;
    }

#if NOVACORE_HAS_SDL3
    if (!window.isHeadless()) {
        sdlRenderer_ = SDL_CreateRenderer(static_cast<SDL_Window*>(window.nativeHandle()), nullptr);
        if (sdlRenderer_ != nullptr) {
            ready_ = true;
            vulkanCapable_ = false;
            core::logInfo("render", "SDL debug renderer created");
            return true;
        }
        core::logWarning("render", "SDL debug renderer unavailable: " + std::string(SDL_GetError()));
    }
#endif

    (void)window;

    ready_ = true;
    vulkanCapable_ = false;
    core::logWarning("render", "Using null renderer fallback");
    return true;
}

MeshResourceHandle Renderer::registerMeshResource(
    std::string assetId,
    const assets::GltfMeshData& meshData) {
    const auto handle = meshResources_->registerResource(std::move(assetId), meshData);
    if (!handle.isValid()) {
        core::logWarning("render", "Mesh resource registration rejected invalid mesh data");
        return {};
    }

    if (vulkanBackend_ != nullptr && vulkanBackend_->ready()) {
        const auto resource = meshResources_->view(handle);
        if (resource.has_value()) {
            vulkanBackend_->registerMeshResource(*resource);
        }
    }
    return handle;
}

void Renderer::releaseMeshResource(MeshResourceHandle handle) {
    if (!handle.isValid()) {
        return;
    }

    if (vulkanBackend_ != nullptr && vulkanBackend_->ready()) {
        vulkanBackend_->releaseMeshResource(handle);
    }
    meshResources_->release(handle);
}

MeshResourceHandle Renderer::findMeshResource(std::string_view assetId) const {
    return meshResources_->find(assetId);
}

MeshResourceStats Renderer::meshResourceStats() const {
    auto stats = meshResources_->stats();
    if (vulkanBackend_ != nullptr && vulkanBackend_->ready()) {
        const auto gpuStats = vulkanBackend_->meshResourceStats();
        stats.pendingUploadResources = gpuStats.pendingUploadResources;
        stats.residentResources = gpuStats.residentResources;
        stats.failedResources = gpuStats.failedResources;
        stats.uploadQueueLength = gpuStats.uploadQueueLength;
        stats.deferredDestroyCount = gpuStats.deferredDestroyCount;
    }
    return stats;
}

void Renderer::beginFrame(const RenderFrameInfo& info) {
    clearColor_ = info.clearColor;

    if (vulkanBackend_ != nullptr && vulkanBackend_->ready()) {
        vulkanBackend_->beginFrame(info);
        return;
    }

#if NOVACORE_HAS_SDL3
    if (sdlRenderer_ != nullptr) {
        auto* renderer = static_cast<SDL_Renderer*>(sdlRenderer_);
        setDrawColor(renderer, clearColor_);
        SDL_RenderClear(renderer);

        for (const auto& rect : info.debugRects) {
            setDrawColor(renderer, rect.color);
            fillRect(renderer, rect.x, rect.y, rect.width, rect.height);
        }

        for (const auto& line : info.debugLines) {
            setDrawColor(renderer, line.color);
            SDL_RenderLine(renderer, line.x0, line.y0, line.x1, line.y1);
        }

        for (const auto& text : info.debugTexts) {
            drawText(renderer, text);
        }
    }
#else
    (void)info;
#endif
}

void Renderer::endFrame() {
    if (vulkanBackend_ != nullptr && vulkanBackend_->ready()) {
        vulkanBackend_->endFrame();
        return;
    }

#if NOVACORE_HAS_SDL3
    if (sdlRenderer_ != nullptr) {
        SDL_RenderPresent(static_cast<SDL_Renderer*>(sdlRenderer_));
    }
#endif
}

void Renderer::shutdown() {
    if (vulkanBackend_ != nullptr) {
        vulkanBackend_->shutdown();
        vulkanBackend_.reset();
    }

#if NOVACORE_HAS_SDL3
    if (sdlRenderer_ != nullptr) {
        SDL_DestroyRenderer(static_cast<SDL_Renderer*>(sdlRenderer_));
        sdlRenderer_ = nullptr;
    }
#endif

    if (ready_) {
        core::logInfo("render", "Renderer shutdown");
    }
    ready_ = false;
    vulkanCapable_ = false;
    vulkanSummary_ = "not probed";
    vulkanRuntime_ = {};
}

std::string_view Renderer::backendName() const {
    if (vulkanBackend_ != nullptr && vulkanBackend_->ready()) {
        return "vulkan-world";
    }
    if (sdlRenderer_ != nullptr) {
        return "sdl-debug";
    }
    return vulkanCapable_ ? "vulkan-unavailable" : "null";
}

std::string_view Renderer::vulkanSummary() const {
    return vulkanSummary_;
}

bool Renderer::vulkanRuntimeAvailable() const {
    return vulkanRuntime_.usable();
}

bool Renderer::isReady() const {
    return ready_;
}

} // namespace novacore::render







