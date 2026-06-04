#include "novacore/core/ConfigRegistry.hpp"

#include <utility>

namespace novacore::core {

ConfigReloadEvent ConfigRegistry::watchJson(std::string name, std::filesystem::path path) {
    Entry entry{};
    entry.name = std::move(name);
    entry.path = std::move(path);

    const auto key = entry.name;
    auto [it, _] = entries_.insert_or_assign(key, std::move(entry));
    tracker_.track(it->second.path);
    return reload(it->second);
}

void ConfigRegistry::unwatch(std::string_view name) {
    auto it = entries_.find(std::string(name));
    if (it == entries_.end()) {
        return;
    }

    tracker_.untrack(it->second.path);
    entries_.erase(it);
}

const ConfigDocument* ConfigRegistry::find(std::string_view name) const {
    auto it = entries_.find(std::string(name));
    if (it == entries_.end() || !it->second.loaded) {
        return nullptr;
    }
    return &it->second.document;
}

ConfigDocument* ConfigRegistry::find(std::string_view name) {
    auto it = entries_.find(std::string(name));
    if (it == entries_.end() || !it->second.loaded) {
        return nullptr;
    }
    return &it->second.document;
}

std::vector<ConfigReloadEvent> ConfigRegistry::pollReloads() {
    std::vector<ConfigReloadEvent> events;
    for (const auto& change : tracker_.pollChanges()) {
        for (auto& [_, entry] : entries_) {
            if (entry.path == change.path) {
                events.push_back(reload(entry));
            }
        }
    }
    return events;
}

std::size_t ConfigRegistry::watchedCount() const {
    return entries_.size();
}

ConfigReloadEvent ConfigRegistry::reload(Entry& entry) {
    ConfigReloadEvent event{};
    event.name = entry.name;
    event.path = entry.path;

    const auto file = io::readTextFile(entry.path);
    if (!file.has_value()) {
        entry.loaded = false;
        event.loaded = false;
        event.errors.push_back(ConfigParseError{"Could not read config file", 0});
        return event;
    }

    ConfigDocument parsed;
    auto result = parseJsonConfig(file->text, parsed);
    if (!result.ok()) {
        entry.loaded = false;
        event.loaded = false;
        event.errors = std::move(result.errors);
        return event;
    }

    entry.document = std::move(parsed);
    entry.loaded = true;
    event.loaded = true;
    return event;
}

} // namespace novacore::core
