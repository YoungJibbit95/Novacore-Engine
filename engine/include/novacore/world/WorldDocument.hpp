#pragma once

#include "novacore/math/Types.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace novacore::world {

inline constexpr unsigned kWorldSchemaVersion = 1;
inline constexpr std::size_t kWorldObjectLimit = 100000;
inline constexpr float kWorldCoordinateLimit = 10000.0F;

// First slice: axis-aligned boxes, catalog references and generic markers.
// IDs belong to the authored document, never to an ECS entity or GPU handle.
struct WorldBox final {
    math::Vec3 halfExtents{0.5F, 0.5F, 0.5F};
    bool blocksMovement = true;
};
struct WorldAssetReference final { std::string assetId; };
struct WorldMarker final { std::string type; };
using WorldObjectData = std::variant<WorldBox, WorldAssetReference, WorldMarker>;

struct WorldObject final {
    std::string id;
    std::string name;
    math::Vec3 position{};
    WorldObjectData data{WorldBox{}};
};
struct WorldDocument final {
    unsigned schemaVersion = kWorldSchemaVersion;
    std::string id;
    std::vector<WorldObject> objects;
};

// Pure structural validation. Asset resolution and marker semantics belong to
// the project adapter; this does not certify a document as playable.
[[nodiscard]] std::optional<std::string> validateWorldDocument(const WorldDocument& document);
[[nodiscard]] const WorldObject* findWorldObject(const WorldDocument& document, std::string_view id);

struct WorldBoxHit final {
    std::string objectId;
    double distance = 0.0; // metres, independent of input direction length
};
// For structurally validated documents. Only boxes participate. Nearest hit
// wins; equal distances use lexicographic persistent ID order.
[[nodiscard]] std::optional<WorldBoxHit> pickWorldBox(const WorldDocument& document,
    math::Vec3 origin, math::Vec3 direction, double maxDistance);

} // namespace novacore::world
