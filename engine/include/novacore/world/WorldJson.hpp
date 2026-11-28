#pragma once

#include "novacore/world/WorldDocument.hpp"

namespace novacore::world {

inline constexpr std::size_t kWorldJsonByteLimit = 64U * 1024U * 1024U;

// Schema 1, UTF-8 JSON. Ordered fields and authored object order; no implicit
// coercion, unknown fields or duplicate keys. On error the output is unchanged.
[[nodiscard]] std::optional<std::string> serializeWorldJson(const WorldDocument& document, std::string& out);
[[nodiscard]] std::optional<std::string> deserializeWorldJson(std::string_view json, WorldDocument& out);

} // namespace novacore::world
