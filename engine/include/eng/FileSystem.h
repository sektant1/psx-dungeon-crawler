#pragma once
#include <ctime>
#include <string>
#include <vector>

namespace eng {

// Thin static wrapper over std::filesystem for the operations the engine needs.
// (The sample's shell Execute and native file dialogs are intentionally omitted.)
class FileSystem {
public:
    using Path = std::string;
    using PathList = std::vector<Path>;

    // Paths
    static bool exists(const Path& path);
    static std::time_t lastModified(const Path& path);   // 0 if missing
    static bool remove(const Path& path);

    // Files
    static bool fileRead(const Path& path, std::string& out);
    static bool fileWrite(const Path& path, const std::string& data, bool append = false);
    static Path extension(const Path& path);   // includes leading dot
    static Path stem(const Path& path);        // filename without extension
    static Path filename(const Path& path);    // name + extension

    // Directories
    static Path directoryCurrent();
    static bool directoryCreate(const Path& path);
    static PathList directoryListFiles(const Path& path);
    static PathList directoryListFiles(const Path& path, const std::string& extension);
};

} // namespace eng
