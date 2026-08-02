#include "FileBrowser.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <system_error>

namespace ed {
namespace {

std::string lowered(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return char(std::tolower(c));
    });
    return value;
}

// Labels sort before the paths they came from, so "a/z.glb" and "b/a.glb"
// order the way the eye reads them rather than by directory depth.
void sortByLabel(std::vector<FileEntry>& entries)
{
    std::sort(entries.begin(), entries.end(),
              [](const FileEntry& a, const FileEntry& b) {
                  return lowered(a.label) < lowered(b.label);
              });
}

} // namespace

bool hasExtension(const std::string& path,
                  const std::vector<std::string>& extensions)
{
    if (extensions.empty())
        return true;
    const std::string ext =
        lowered(std::filesystem::path(path).extension().string());
    for (const std::string& wanted : extensions)
        if (ext == wanted)
            return true;
    return false;
}

ScanResult findFiles(const std::string& root,
                     const std::vector<std::string>& extensions,
                     const ScanLimits& limits)
{
    ScanResult result;
    std::error_code ec;
    if (!std::filesystem::is_directory(root, ec))
        return result;

    // recursive_directory_iterator with an error_code never throws; it reports
    // and continues, which is what lets one unreadable folder be skipped rather
    // than aborting the whole listing.
    auto options = std::filesystem::directory_options::skip_permission_denied;
    std::filesystem::recursive_directory_iterator it(root, options, ec), end;
    if (ec)
        return result;

    const std::filesystem::path rootPath = std::filesystem::path(root);
    for (; it != end; it.increment(ec)) {
        if (ec) {
            ec.clear();
            continue;
        }
        if (it.depth() >= limits.maxDepth) {
            it.disable_recursion_pending();
            continue;
        }
        if (!it->is_regular_file(ec) || ec) {
            ec.clear();
            continue;
        }
        const std::string path = it->path().string();
        if (!hasExtension(path, extensions))
            continue;
        if (result.files.size() >= limits.maxFiles) {
            result.truncated = true;
            break;
        }
        // Shown relative to the root: an author recognises
        // "dungeon/entire map/.../ModularDungeonFree.glb", not an absolute path
        // whose first sixty characters are the same on every row.
        std::error_code relEc;
        const std::filesystem::path relative =
            std::filesystem::relative(it->path(), rootPath, relEc);
        result.files.push_back(
            {path, relEc ? it->path().filename().string() : relative.string(),
             false});
    }
    sortByLabel(result.files);
    return result;
}

std::vector<FileEntry> listDirectory(const std::string& directory,
                                     const std::vector<std::string>& extensions)
{
    std::vector<FileEntry> directories;
    std::vector<FileEntry> files;
    std::error_code ec;
    if (!std::filesystem::is_directory(directory, ec))
        return files;

    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator(
             directory, std::filesystem::directory_options::skip_permission_denied,
             ec)) {
        if (ec)
            break;
        const std::string name = entry.path().filename().string();
        // Dotfiles are noise here: nobody keeps source art in .git.
        if (!name.empty() && name[0] == '.')
            continue;
        std::error_code kindEc;
        if (entry.is_directory(kindEc) && !kindEc) {
            directories.push_back({entry.path().string(), name, true});
        }
        else if (entry.is_regular_file(kindEc) && !kindEc &&
                 hasExtension(entry.path().string(), extensions)) {
            files.push_back({entry.path().string(), name, false});
        }
    }
    sortByLabel(directories);
    sortByLabel(files);
    // Directories first, so navigating is one predictable region of the list
    // rather than something to hunt for among the files.
    directories.insert(directories.end(), files.begin(), files.end());
    return directories;
}

std::vector<FileEntry> filterFiles(const std::vector<FileEntry>& entries,
                                   const std::string& query)
{
    if (query.empty())
        return entries;
    const std::string needle = lowered(query);
    std::vector<FileEntry> kept;
    for (const FileEntry& entry : entries) {
        // A filter must never strand the author in a folder they cannot leave.
        if (entry.directory ||
            lowered(entry.label).find(needle) != std::string::npos)
            kept.push_back(entry);
    }
    return kept;
}

std::string parentDirectory(const std::string& directory)
{
    const std::filesystem::path path(directory);
    const std::filesystem::path parent = path.parent_path();
    if (parent.empty() || parent == path)
        return std::string();
    return parent.string();
}

} // namespace ed
