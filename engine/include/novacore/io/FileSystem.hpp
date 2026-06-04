#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace novacore::io {

struct TextFile final {
    std::filesystem::path path;
    std::string text;
};

struct FileSnapshot final {
    std::filesystem::path path;
    bool exists = false;
    std::uintmax_t sizeBytes = 0;
    std::filesystem::file_time_type lastWriteTime{};
};

struct FileChange final {
    std::filesystem::path path;
    bool existedBefore = false;
    bool existsNow = false;
    bool contentMayHaveChanged = false;
};

[[nodiscard]] std::optional<TextFile> readTextFile(const std::filesystem::path& path);
[[nodiscard]] FileSnapshot snapshotFile(const std::filesystem::path& path);

class FileChangeTracker final {
public:
    void track(const std::filesystem::path& path);
    void untrack(const std::filesystem::path& path);
    [[nodiscard]] bool isTracked(const std::filesystem::path& path) const;
    [[nodiscard]] std::vector<FileChange> pollChanges();
    [[nodiscard]] std::size_t trackedCount() const;

private:
    std::unordered_map<std::string, FileSnapshot> snapshots_;
};

} // namespace novacore::io
