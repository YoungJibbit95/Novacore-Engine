#include "novacore/render/UiDrawData.hpp"
#include <cmath>

namespace novacore::render {
bool UiDrawData::valid() const {
    if (!std::isfinite(width) || !std::isfinite(height) || width <= 0 || height <= 0 ||
        vertices.size() > 1000000 || indices.size() > 3000000 || commands.size() > 10000) return false;
    for (const auto& vertex : vertices) {
        for (float v : vertex.position) if (!std::isfinite(v)) return false;
        for (float v : vertex.texcoord) if (!std::isfinite(v)) return false;
    }
    for (auto index : indices) if (index >= vertices.size()) return false;
    for (const auto& draw : commands) {
        if (draw.firstIndex > indices.size() || draw.indexCount > indices.size() - draw.firstIndex ||
            draw.indexCount % 3 != 0 || (draw.texture && !draw.texture->valid())) return false;
        for (float v : draw.clip) if (!std::isfinite(v)) return false;
        if (draw.clip[2] < 0 || draw.clip[3] < 0) return false;
    }
    return true;
}
} // namespace novacore::render
