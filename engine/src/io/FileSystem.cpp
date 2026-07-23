#include <eng/FileSystem.h>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace eng {

bool FileSystem::exists(const Path& path) {
    std::error_code ec;
    return fs::exists(path, ec);
}

std::time_t FileSystem::lastModified(const Path& path) {
    std::error_code ec;
    auto ft = fs::last_write_time(path, ec);
    if (ec) return 0;
    auto sys = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        ft - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
    return std::chrono::system_clock::to_time_t(sys);
}

bool FileSystem::remove(const Path& path) {
    std::error_code ec;
    return fs::remove(path, ec);
}

bool FileSystem::fileRead(const Path& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
}

bool FileSystem::fileWrite(const Path& path, const std::string& data, bool append) {
    std::ofstream o(path, std::ios::binary | (append ? std::ios::app : std::ios::trunc));
    if (!o) return false;
    o << data;
    return true;
}

FileSystem::Path FileSystem::extension(const Path& path) { return fs::path(path).extension().string(); }
FileSystem::Path FileSystem::stem(const Path& path)      { return fs::path(path).stem().string(); }
FileSystem::Path FileSystem::filename(const Path& path)  { return fs::path(path).filename().string(); }

FileSystem::Path FileSystem::directoryCurrent() {
    std::error_code ec;
    return fs::current_path(ec).string();
}

bool FileSystem::directoryCreate(const Path& path) {
    std::error_code ec;
    fs::create_directories(path, ec);
    return !ec;
}

FileSystem::PathList FileSystem::directoryListFiles(const Path& path) {
    PathList files;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(path, ec))
        if (e.is_regular_file()) files.push_back(e.path().string());
    return files;
}

FileSystem::PathList FileSystem::directoryListFiles(const Path& path, const std::string& extension) {
    PathList files;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(path, ec))
        if (e.is_regular_file() && e.path().extension() == extension)
            files.push_back(e.path().string());
    return files;
}

} // namespace eng
