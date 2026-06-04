#pragma once

#include "novacore/core/ConfigDocument.hpp"
#include "novacore/io/FileSystem.hpp"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace novacore::core {

struct ConfigReloadEvent final {
    std::string name;
    std::filesystem::path path;
    bool loaded = false;
    std::vector<ConfigParseError> errors;
};

class ConfigRegistry final {
public:
    [[nodiscard]] ConfigReloadEvent watchJson(std::string name, std::filesystem::path path);
    void unwatch(std::string_view name);

    [[nodiscard]] const ConfigDocument* find(std::string_view name) const;
    [[nodiscard]] ConfigDocument* find(std::string_view name);
    [[nodiscard]] std::vector<ConfigReloadEvent> pollReloads();
    [[nodiscard]] std::size_t watchedCount() const;

private:
    struct Entry final {
        std::string name;
        std::filesystem::path path;
        ConfigDocument document;
        bool loaded = false;
    };

    [[nodiscard]] ConfigReloadEvent reload(Entry& entry);

    io::FileChangeTracker tracker_;
    std::unordered_map<std::string, Entry> entries_;
};

} // namespace novacore::core
