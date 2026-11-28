#include "novacore/world/WorldDocument.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <unordered_set>

namespace novacore::world {
namespace {

bool validId(std::string_view id) {
    return !id.empty() && id.size() <= 128 && std::all_of(id.begin(), id.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
    }) && id != "." && id != "..";
}
bool validName(std::string_view name) {
    return !name.empty() && name.size() <= 256 && std::none_of(name.begin(), name.end(), [](unsigned char c) {
        return c < 32 || c == 127;
    });
}
std::array<double, 3> components(math::Vec3 value) { return {value.x, value.y, value.z}; }
bool finite(math::Vec3 value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}
bool inBounds(math::Vec3 value) {
    return finite(value) && std::abs(value.x) <= kWorldCoordinateLimit &&
        std::abs(value.y) <= kWorldCoordinateLimit && std::abs(value.z) <= kWorldCoordinateLimit;
}
bool validBox(math::Vec3 center, math::Vec3 halfExtents) {
    const auto p = components(center);
    const auto h = components(halfExtents);
    for (std::size_t i = 0; i < 3; ++i) {
        if (!std::isfinite(h[i]) || h[i] < 0.001 || std::abs(p[i]) + h[i] > kWorldCoordinateLimit) {
            return false;
        }
    }
    return true;
}
} // namespace

std::optional<std::string> validateWorldDocument(const WorldDocument& document) {
    if (document.schemaVersion != kWorldSchemaVersion) return "Unsupported world schema version";
    if (!validId(document.id)) return "Invalid world ID";
    if (document.objects.size() > kWorldObjectLimit) return "World object limit exceeded";
    std::unordered_set<std::string_view> ids;
    ids.reserve(document.objects.size());
    for (const auto& object : document.objects) {
        if (!validId(object.id)) return "Invalid object ID";
        if (!ids.insert(object.id).second) return "Duplicate object ID: " + object.id;
        if (!validName(object.name)) return "Invalid name: " + object.id;
        if (!inBounds(object.position)) return "Invalid position: " + object.id;
        if (const auto* box = std::get_if<WorldBox>(&object.data)) {
            if (!validBox(object.position, box->halfExtents)) return "Invalid box bounds: " + object.id;
        } else if (const auto* asset = std::get_if<WorldAssetReference>(&object.data)) {
            if (!validId(asset->assetId)) return "Invalid asset ID: " + object.id;
        } else if (const auto* marker = std::get_if<WorldMarker>(&object.data)) {
            if (!validId(marker->type)) return "Invalid marker type: " + object.id;
        } else {
            return "Invalid object data: " + object.id;
        }
    }
    return std::nullopt;
}

const WorldObject* findWorldObject(const WorldDocument& document, std::string_view id) {
    const auto found = std::find_if(document.objects.begin(), document.objects.end(),
        [id](const WorldObject& object) { return object.id == id; });
    return found == document.objects.end() ? nullptr : &*found;
}

std::optional<WorldBoxHit> pickWorldBox(const WorldDocument& document,
    math::Vec3 origin, math::Vec3 direction, double maxDistance) {
    if (!finite(origin) || !finite(direction) || !std::isfinite(maxDistance) || maxDistance < 0.0) return std::nullopt;
    const auto o = components(origin);
    auto d = components(direction);
    const double length = std::hypot(d[0], d[1], d[2]);
    if (length == 0.0) return std::nullopt;
    for (auto& axis : d) axis /= length;
    std::optional<WorldBoxHit> best;
    for (const auto& object : document.objects) {
        const auto* box = std::get_if<WorldBox>(&object.data);
        if (!box || !inBounds(object.position) || !validBox(object.position, box->halfExtents)) continue;
        const auto p = components(object.position);
        const auto h = components(box->halfExtents);
        double enter = 0.0;
        double leave = best ? best->distance : maxDistance;
        bool hit = true;
        for (std::size_t i = 0; i < 3; ++i) {
            const double low = p[i] - h[i];
            const double high = p[i] + h[i];
            if (d[i] == 0.0) {
                if (o[i] < low || o[i] > high) { hit = false; break; }
                continue;
            }
            double a = (low - o[i]) / d[i];
            double b = (high - o[i]) / d[i];
            if (a > b) std::swap(a, b);
            enter = std::max(enter, a);
            leave = std::min(leave, b);
            if (enter > leave) { hit = false; break; }
        }
        if (hit && (!best || enter < best->distance ||
                (enter == best->distance && object.id < best->objectId))) {
            best = WorldBoxHit{object.id, enter};
        }
    }
    return best;
}
} // namespace novacore::world
