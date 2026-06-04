#include "novacore/core/Config.hpp"

#include <utility>

namespace novacore::core {

void Config::set(std::string key, std::string value) {
    values_.insert_or_assign(std::move(key), std::move(value));
}

std::optional<std::string> Config::get(std::string_view key) const {
    auto it = values_.find(std::string(key));
    if (it == values_.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::string Config::getOr(std::string_view key, std::string fallback) const {
    auto value = get(key);
    if (!value.has_value()) {
        return fallback;
    }
    return *value;
}

} // namespace novacore::core






