#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace eng {

// A single detected change in a watched directory.
struct FileChange {
    enum Kind { Added, Modified, Removed };
    Kind kind;
    std::string path;
};

// Poll-based directory watcher for asset hot-reload. Call poll() from the engine
// loop; it diffs the current directory contents against the previous snapshot
// and returns the changes since the last poll. Non-threaded and deterministic.
// Optionally filtered to a set of extensions (e.g. {".toml",".material"}).
class DirectoryWatcher {
public:
    explicit DirectoryWatcher(std::string dir, std::vector<std::string> extensions = {});
    std::vector<FileChange> poll();

private:
    bool accepts(const std::string& path) const;

    std::string mDir;
    std::vector<std::string> mExtensions;
    std::unordered_map<std::string, std::int64_t> mSnapshot;  // path -> mtime
};

} // namespace eng
