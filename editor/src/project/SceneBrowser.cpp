#include <editor/project/SceneBrowser.h>

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace ed {
namespace {

std::string lower(std::string text)
{
    for (char& c : text)
        if (c >= 'A' && c <= 'Z')
            c = char(c - 'A' + 'a');
    return text;
}

} // namespace

bool isAutosavePath(const std::string& path)
{
    static const std::string kSuffix = ".autosave.scn";
    return path.size() > kSuffix.size() &&
           path.compare(path.size() - kSuffix.size(), kSuffix.size(),
                        kSuffix) == 0;
}

std::vector<SceneEntry> listScenes(const std::string& directory)
{
    std::vector<SceneEntry> entries;
    std::error_code ec;
    if (!std::filesystem::is_directory(directory, ec))
        return entries;

    for (const std::filesystem::directory_entry& file :
         std::filesystem::directory_iterator(directory, ec)) {
        if (ec)
            break;
        if (!file.is_regular_file() || file.path().extension() != ".scn")
            continue;
        // Backups are scenes by extension and not by intent. Listing them
        // beside the real ones doubles the dialog and invites opening one by
        // accident, which is how a recovery quietly becomes the working file.
        // `Scene > Recover autosave` is the one door to them.
        const std::string name = file.path().filename().string();
        if (isAutosavePath(name))
            continue;
        entries.push_back({file.path().string(), name});
    }
    // By name, because that is the order the author's eye expects and the only
    // one that is stable across machines -- directory iteration order is not.
    std::sort(entries.begin(), entries.end(),
              [](const SceneEntry& a, const SceneEntry& b) {
                  return a.name < b.name;
              });
    return entries;
}

std::vector<SceneEntry> filterScenes(const std::vector<SceneEntry>& entries,
                                     const std::string& query)
{
    if (query.empty())
        return entries;
    const std::string needle = lower(query);
    std::vector<SceneEntry> kept;
    for (const SceneEntry& entry : entries)
        if (lower(entry.name).find(needle) != std::string::npos)
            kept.push_back(entry);
    return kept;
}

void RecentScenes::touch(std::string path)
{
    if (path.empty())
        return;
    remove(path);
    mPaths.insert(mPaths.begin(), std::move(path));
    if (mPaths.size() > kMax)
        mPaths.resize(kMax);
}

void RecentScenes::remove(const std::string& path)
{
    mPaths.erase(std::remove(mPaths.begin(), mPaths.end(), path), mPaths.end());
}

void RecentScenes::load(const std::string& file)
{
    mPaths.clear();
    std::ifstream in(file);
    if (!in)
        return;
    std::string line;
    while (std::getline(in, line) && mPaths.size() < kMax) {
        if (!line.empty())
            mPaths.push_back(line);
    }
}

bool RecentScenes::save(const std::string& file) const
{
    std::error_code ec;
    std::filesystem::create_directories(
        std::filesystem::path(file).parent_path(), ec);
    std::ofstream out(file, std::ios::trunc);
    if (!out)
        return false;
    for (const std::string& path : mPaths)
        out << path << '\n';
    return bool(out);
}

std::string autosavePath(const std::string& scenePath,
                         const std::string& fallbackDir)
{
    if (scenePath.empty()) {
        if (fallbackDir.empty())
            return {};
        return (std::filesystem::path(fallbackDir) / "untitled.autosave.scn")
            .string();
    }
    std::filesystem::path path(scenePath);
    // ".autosave.scn", not ".scn.autosave": it stays a scene file, so the
    // recovery path is "open it" and every tool that reads scenes can.
    path.replace_extension();
    return path.string() + ".autosave.scn";
}

bool autosaveIsStale(const std::string& scenePath,
                     const std::string& fallbackDir)
{
    const std::string backup = autosavePath(scenePath, fallbackDir);
    std::error_code ec;
    if (backup.empty() || !std::filesystem::exists(backup, ec))
        return false;
    // An unsaved scene has nothing to compare against: the backup is the only
    // copy, so it is always worth offering.
    if (scenePath.empty() || !std::filesystem::exists(scenePath, ec))
        return true;
    const auto backupTime = std::filesystem::last_write_time(backup, ec);
    if (ec)
        return false;
    const auto sceneTime = std::filesystem::last_write_time(scenePath, ec);
    if (ec)
        return false;
    return backupTime > sceneTime;
}

} // namespace ed
