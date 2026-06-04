#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace novacore::core {

struct ConfigParseError final {
    std::string message;
    std::size_t offset = 0;
};

struct ConfigParseResult final {
    std::vector<ConfigParseError> errors;

    [[nodiscard]] bool ok() const {
        return errors.empty();
    }
};

class ConfigDocument final {
public:
    void set(std::string key, std::string value);
    void clear();

    [[nodiscard]] bool contains(std::string_view key) const;
    [[nodiscard]] std::optional<std::string> stringValue(std::string_view key) const;
    [[nodiscard]] std::string stringOr(std::string_view key, std::string fallback) const;
    [[nodiscard]] std::optional<double> numberValue(std::string_view key) const;
    [[nodiscard]] double numberOr(std::string_view key, double fallback) const;
    [[nodiscard]] std::optional<int> intValue(std::string_view key) const;
    [[nodiscard]] int intOr(std::string_view key, int fallback) const;
    [[nodiscard]] std::optional<bool> boolValue(std::string_view key) const;
    [[nodiscard]] bool boolOr(std::string_view key, bool fallback) const;
    [[nodiscard]] std::size_t valueCount() const;

    [[nodiscard]] const std::unordered_map<std::string, std::string>& values() const;

private:
    std::unordered_map<std::string, std::string> values_;
};

[[nodiscard]] ConfigParseResult parseJsonConfig(std::string_view text, ConfigDocument& outDocument);

} // namespace novacore::core
