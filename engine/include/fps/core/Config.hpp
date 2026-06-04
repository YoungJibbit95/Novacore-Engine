#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace riftline::core {

class Config final {
public:
    void set(std::string key, std::string value);
    [[nodiscard]] std::optional<std::string> get(std::string_view key) const;
    [[nodiscard]] std::string getOr(std::string_view key, std::string fallback) const;

private:
    std::unordered_map<std::string, std::string> values_;
};

} // namespace riftline::core

