#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace ed {

// Opening a scene, and remembering which ones were open.
//
// Until this existed the editor could only open the scene named on the command
// line: the Scene menu had New, Save, Save as and Reload, and no Open. Every
// other way back into a level -- the one saved ten minutes ago, the one another
// author just committed -- meant quitting and relaunching with an argument.
//
// The listing and the recent list are separated from the panel that draws them
// so both are testable without a window, which matters because the failure mode
// here is silent: a filter that quietly matches nothing, or a recent list that
// grows without bound, both look exactly like an empty dialog.

struct SceneEntry {
    std::string path; // as given to loadScene
    std::string name; // file name, what the author reads
};

// Every .scn directly under `directory`, by name, excluding autosave backups.
// A missing or unreadable directory is empty, not an error: a project that has
// not saved a scene yet is the normal first run, and the dialog says so better
// than a message box.
std::vector<SceneEntry> listScenes(const std::string& directory);

// True for the backup files autosavePath() produces. The Open dialog and the
// recent list both hide them: a backup is reached through Recover, not by
// browsing, or a recovery quietly becomes the working file.
bool isAutosavePath(const std::string& path);

// Case-insensitive substring match on the file name. An empty query keeps
// everything.
std::vector<SceneEntry> filterScenes(const std::vector<SceneEntry>& entries,
                                     const std::string& query);

// Most recently opened first, without duplicates, bounded.
//
// Bounded matters: this is written to disk on every open, and an unbounded list
// is a file that grows for the life of the project and a menu nobody can read.
class RecentScenes
{
public:
    static constexpr std::size_t kMax = 8;

    // Moves `path` to the front, whether or not it was already there.
    void touch(std::string path);
    void remove(const std::string& path);
    const std::vector<std::string>& paths() const { return mPaths; }

    // One path per line, oldest last. A missing file leaves the list empty --
    // this is a convenience, and never a reason to fail to start.
    void load(const std::string& file);
    bool save(const std::string& file) const;

private:
    std::vector<std::string> mPaths;
};

// Where the periodic backup of `scenePath` lives.
//
// Beside the scene and named after it, so recovering one is obvious from a file
// listing and so two scenes open in two editors cannot overwrite each other's
// backup. A scene with no file yet still gets one, under `fallbackDir`: an hour
// of blockout that was never saved is exactly the work worth keeping, and it is
// the only work the editor could previously lose outright.
std::string autosavePath(const std::string& scenePath,
                         const std::string& fallbackDir);

// True when an autosave exists and is newer than the scene it belongs to --
// i.e. when the last session ended without saving. Offering recovery in any
// other case trains the author to dismiss the prompt.
bool autosaveIsStale(const std::string& scenePath,
                     const std::string& fallbackDir);

} // namespace ed
