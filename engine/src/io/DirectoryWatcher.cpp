#include <eng/DirectoryWatcher.h>
#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;

namespace eng {

DirectoryWatcher::DirectoryWatcher(std::string dir, std::vector<std::string> extensions)
    : mDir(std::move(dir)), mExtensions(std::move(extensions)) {}

bool DirectoryWatcher::accepts(const std::string& path) const {
    if (mExtensions.empty()) return true;
    const std::string ext = fs::path(path).extension().string();
    return std::find(mExtensions.begin(), mExtensions.end(), ext) != mExtensions.end();
}

std::vector<FileChange> DirectoryWatcher::poll() {
    std::vector<FileChange> changes;
    std::unordered_map<std::string, std::int64_t> current;

    std::error_code ec;
    for (const auto& e : fs::directory_iterator(mDir, ec)) {
        if (!e.is_regular_file()) continue;
        const std::string path = e.path().string();
        if (!accepts(path)) continue;
        auto mtime = fs::last_write_time(e, ec).time_since_epoch().count();
        current[path] = mtime;

        auto prev = mSnapshot.find(path);
        if (prev == mSnapshot.end())
            changes.push_back({FileChange::Added, path});
        else if (prev->second != mtime)
            changes.push_back({FileChange::Modified, path});
    }

    for (const auto& kv : mSnapshot)
        if (current.find(kv.first) == current.end())
            changes.push_back({FileChange::Removed, kv.first});

    mSnapshot.swap(current);
    return changes;
}

} // namespace eng
