#pragma once

#include "novacore/core/ConfigDocument.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace novacore::assets {

enum class GltfContainerKind {
    Text,
    BinaryGlb
};

struct GltfSceneInfo final {
    std::filesystem::path path;
    GltfContainerKind container = GltfContainerKind::Text;
    std::uint32_t glbVersion = 0;
    std::uint64_t declaredLengthBytes = 0;
    std::uint64_t jsonBytes = 0;
    std::uint64_t binaryBytes = 0;
    std::size_t meshCount = 0;
    std::size_t nodeCount = 0;
    std::size_t materialCount = 0;
    std::size_t accessorCount = 0;
    std::size_t bufferViewCount = 0;
    std::size_t bufferCount = 0;
    std::size_t imageCount = 0;
    std::size_t animationCount = 0;
    std::size_t skinCount = 0;
};

struct GltfSceneInfoLoadResult final {
    std::vector<std::string> errors;

    [[nodiscard]] bool ok() const {
        return errors.empty();
    }
};

[[nodiscard]] std::string_view gltfContainerKindName(GltfContainerKind kind);

[[nodiscard]] GltfSceneInfoLoadResult parseGltfSceneInfoFromJson(
    std::string_view json,
    GltfSceneInfo& outInfo);

[[nodiscard]] GltfSceneInfoLoadResult loadGltfSceneInfo(
    const std::filesystem::path& path,
    GltfSceneInfo& outInfo);

} // namespace novacore::assets
