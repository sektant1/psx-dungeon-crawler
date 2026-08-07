#include <editor/project/ProjectSession.h>

#include <eng/Log.h>

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace ed {
namespace {

// Enough to get back to what you were working on, short enough to read at a
// glance. The same reason the scene list is capped.
constexpr std::size_t kMaxRecents = 10;

} // namespace

bool ProjectSession::open(const std::string& dir)
{
    eng::runtime::Project loaded;
    if (!eng::runtime::loadProject(dir, loaded))
        return false;
    mProject = std::move(loaded);
    noteRecent(mProject->dir.string());
    return true;
}

bool ProjectSession::create(const std::string& dir, const std::string& name)
{
    if (!eng::runtime::createProject(dir, name))
        return false;
    return open(dir);
}

std::string ProjectSession::contentRoot(const std::string& fallback) const
{
    return mProject ? mProject->dir.string() : fallback;
}

std::string ProjectSession::cookTarget(const std::string& scenePath,
                                       const std::string& fallbackRoot) const
{
    const fs::path scene(scenePath.empty() ? fallbackRoot + "/scenes/untitled.scn"
                                           : scenePath);
    if (!mProject) {
        // Beside the .scn, which is where every cooked map in this repository
        // already sits and what the cook_parity test compares against.
        fs::path target = scene;
        target.replace_extension(".map");
        return target.string();
    }
    // Into the project's work directory: cooking is a build product, and a
    // build product in somebody's source tree is a build product they have to
    // gitignore. Created here because a playtest may be the first thing a
    // fresh project does.
    const fs::path dir = mProject->workDir() / "cooked";
    std::error_code ec;
    fs::create_directories(dir, ec);
    return (dir / (scene.stem().string() + ".map")).string();
}

std::string ProjectSession::mainScenePath() const
{
    if (!mProject)
        return {};
    return (mProject->dir / mProject->mainScene).string();
}

void ProjectSession::noteRecent(const std::string& dir)
{
    for (std::size_t i = 0; i < mRecents.size(); ++i) {
        if (mRecents[i] == dir) {
            mRecents.erase(mRecents.begin() + std::ptrdiff_t(i));
            break;
        }
    }
    mRecents.insert(mRecents.begin(), dir);
    if (mRecents.size() > kMaxRecents)
        mRecents.resize(kMaxRecents);
}

void ProjectSession::loadRecents(const std::string& path)
{
    mRecents.clear();
    std::ifstream in(path);
    if (!in)
        return; // a missing file is a first run, not an error
    std::string line;
    while (std::getline(in, line) && mRecents.size() < kMaxRecents) {
        if (line.empty())
            continue;
        // Pruned on load rather than on use: an entry that no longer opens is
        // worse than no entry, and finding that out by clicking it is worse
        // still.
        if (!eng::runtime::isProjectDir(line))
            continue;
        mRecents.push_back(line);
    }
}

void ProjectSession::saveRecents(const std::string& path) const
{
    std::error_code ec;
    fs::create_directories(fs::path(path).parent_path(), ec);
    std::ofstream out(path, std::ios::trunc);
    if (!out) {
        eng::log::warn("project: could not write recents to %s", path.c_str());
        return;
    }
    for (const std::string& entry : mRecents)
        out << entry << '\n';
}

} // namespace ed
