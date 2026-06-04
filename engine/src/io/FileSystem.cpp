#include "novacore/io/FileSystem.hpp"

#include <fstream>
#include <utility>

namespace novacore::io {

namespace {

std::string normalizedPathKey(const std::filesystem::path& path) {
    return path.lexically_normal().generic_string();
}

} // namespace

std::optional<TextFile> readTextFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::in | std::ios::binary);
    if (!file) {
        return std::nullopt;
    }

    std::string text;
    file.seekg(0, std::ios::end);
    text.resize(static_cast<std::size_t>(file.tellg()));
    file.seekg(0, std::ios::beg);
    file.read(text.data(), static_cast<std::streamsize>(text.size()));

    return TextFile{path, std::move(text)};
}

FileSnapshot snapshotFile(const std::filesystem::path& path) {
    FileSnapshot snapshot{};
    snapshot.path = path;

    std::error_code error;
    snapshot.exists = std::filesystem::exists(path, error);
    if (error || !snapshot.exists) {
        return snapshot;
    }

    snapshot.sizeBytes = std::filesystem::file_size(path, error);
    if (error) {
        snapshot.sizeBytes = 0;
    }

    snapshot.lastWriteTime = std::filesystem::last_write_time(path, error);
    if (error) {
        snapshot.lastWriteTime = {};
    }

    return snapshot;
}

void FileChangeTracker::track(const std::filesystem::path& path) {
    snapshots_.insert_or_assign(normalizedPathKey(path), snapshotFile(path));
}

void FileChangeTracker::untrack(const std::filesystem::path& path) {
    snapshots_.erase(normalizedPathKey(path));
}

bool FileChangeTracker::isTracked(const std::filesystem::path& path) const {
    return snapshots_.contains(normalizedPathKey(path));
}

std::vector<FileChange> FileChangeTracker::pollChanges() {
    std::vector<FileChange> changes;

    for (auto& [_, previous] : snapshots_) {
        auto current = snapshotFile(previous.path);
        const bool changed = previous.exists != current.exists ||
            previous.sizeBytes != current.sizeBytes ||
            previous.lastWriteTime != current.lastWriteTime;

        if (changed) {
            changes.push_back(FileChange{
                previous.path,
                previous.exists,
                current.exists,
                true,
            });
            previous = current;
        }
    }

    return changes;
}

std::size_t FileChangeTracker::trackedCount() const {
    return snapshots_.size();
}

} // namespace novacore::io

