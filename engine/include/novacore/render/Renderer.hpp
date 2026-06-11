#pragma once

#include "novacore/assets/GltfDocument.hpp"
#include "novacore/platform/Window.hpp"
#include "novacore/math/Types.hpp"
#include "novacore/render/VulkanRuntime.hpp"

#include <array>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace novacore::render {

class VulkanBackend;
struct MeshResourceRegistry;

struct MeshResourceHandle final {
    static constexpr std::uint32_t InvalidIndex = std::numeric_limits<std::uint32_t>::max();

    std::uint32_t index = InvalidIndex;
    std::uint32_t generation = 0;

    [[nodiscard]] bool isValid() const {
        return index != InvalidIndex && generation != 0;
    }

    friend bool operator==(MeshResourceHandle lhs, MeshResourceHandle rhs) {
        return lhs.index == rhs.index && lhs.generation == rhs.generation;
    }

    friend bool operator!=(MeshResourceHandle lhs, MeshResourceHandle rhs) {
        return !(lhs == rhs);
    }
};

struct MeshResourceStats final {
    std::size_t registeredResources = 0;
    std::size_t pendingUploadResources = 0;
    std::size_t residentResources = 0;
    std::size_t failedResources = 0;
    std::size_t uploadQueueLength = 0;
    std::size_t deferredDestroyCount = 0;
    std::size_t totalPrimitives = 0;
    std::size_t totalVertices = 0;
    std::size_t totalIndices = 0;
};

struct MeshResourceView final {
    MeshResourceHandle handle{};
    std::string assetId;
    std::shared_ptr<const assets::GltfMeshData> meshData;
};

struct DebugRect final {
    float x = 0.0F;
    float y = 0.0F;
    float width = 0.0F;
    float height = 0.0F;
    std::array<float, 4> color{1.0F, 1.0F, 1.0F, 1.0F};
};

struct DebugLine final {
    float x0 = 0.0F;
    float y0 = 0.0F;
    float x1 = 0.0F;
    float y1 = 0.0F;
    std::array<float, 4> color{1.0F, 1.0F, 1.0F, 1.0F};
};

struct DebugText final {
    float x = 0.0F;
    float y = 0.0F;
    float scale = 2.0F;
    std::array<float, 4> color{1.0F, 1.0F, 1.0F, 1.0F};
    std::string text;
};

struct RendererCreateInfo final {
    std::array<float, 4> clearColor{0.03F, 0.04F, 0.06F, 1.0F};
    bool preferVulkan = false;
    bool requireVulkan = false;
};

struct RenderCamera3D final {
    bool enabled = false;
    math::Vec3 position{0.0F, 1.7F, -8.0F};
    float yawDegrees = 0.0F;
    float pitchDegrees = 0.0F;
    float verticalFovDegrees = 74.0F;
    float nearPlane = 0.03F;
    float farPlane = 1000.0F;
};

struct RenderWorldLighting final {
    math::Vec3 sunDirection{0.35F, 0.85F, 0.28F};
    float ambientIntensity = 0.32F;
};

struct RenderBox3D final {
    math::Vec3 center{};
    math::Vec3 halfExtents{0.5F, 0.5F, 0.5F};
    std::array<float, 4> color{0.72F, 0.82F, 0.86F, 1.0F};
};

struct RenderLine3D final {
    math::Vec3 start{};
    math::Vec3 end{};
    std::array<float, 4> color{0.80F, 0.95F, 1.0F, 1.0F};
};

struct RenderMesh3D final {
    MeshResourceHandle mesh{};
    std::string assetId;
    math::Vec3 position{};
    math::Vec3 scale{1.0F, 1.0F, 1.0F};
    float yawDegrees = 0.0F;
    std::array<float, 4> color{0.70F, 0.78F, 0.80F, 1.0F};
};

struct RenderFrameInfo final {
    std::array<float, 4> clearColor{0.03F, 0.04F, 0.06F, 1.0F};
    RenderCamera3D camera3D{};
    RenderWorldLighting lighting{};
    std::vector<RenderBox3D> worldBoxes;
    std::vector<RenderLine3D> worldLines;
    std::vector<RenderMesh3D> worldMeshes;
    std::vector<DebugRect> debugRects;
    std::vector<DebugLine> debugLines;
    std::vector<DebugText> debugTexts;
};

class Renderer final {
public:
    Renderer();
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    Renderer(Renderer&&) = delete;
    Renderer& operator=(Renderer&&) = delete;

    bool create(platform::Window& window, const RendererCreateInfo& info);
    [[nodiscard]] MeshResourceHandle registerMeshResource(
        std::string assetId,
        const assets::GltfMeshData& meshData);
    void releaseMeshResource(MeshResourceHandle handle);
    [[nodiscard]] MeshResourceHandle findMeshResource(std::string_view assetId) const;
    [[nodiscard]] MeshResourceStats meshResourceStats() const;
    void beginFrame(const RenderFrameInfo& info);
    void endFrame();
    void shutdown();

    [[nodiscard]] std::string_view backendName() const;
    [[nodiscard]] std::string_view vulkanSummary() const;
    [[nodiscard]] bool vulkanRuntimeAvailable() const;
    [[nodiscard]] bool isReady() const;

private:
    bool ready_ = false;
    bool vulkanCapable_ = false;
    VulkanRuntimeInfo vulkanRuntime_;
    std::string vulkanSummary_ = "not probed";
    std::unique_ptr<MeshResourceRegistry> meshResources_;
    std::unique_ptr<VulkanBackend> vulkanBackend_;
    void* sdlRenderer_ = nullptr;
    std::array<float, 4> clearColor_{0.03F, 0.04F, 0.06F, 1.0F};
};

} // namespace novacore::render







