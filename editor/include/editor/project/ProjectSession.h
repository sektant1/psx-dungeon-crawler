#pragma once

#include <eng/runtime/Project.h>

#include <optional>
#include <string>
#include <vector>

namespace ed {

// The project the editor has open, if any.
//
// "If any" is load-bearing. This editor was built to author one game -- the
// content tree it ships beside -- and that mode still works exactly as it did:
// no project open means the content pack is the tree, which is what every
// existing scene, kit and test assumes. Opening a project points the same
// editor at somebody else's tree instead.
//
// Deliberately thin. It holds what the editor cannot derive (which project,
// which ones were open before) and derives everything else from
// eng::runtime::Project, so there is one definition of what a project is and
// the editor cannot drift from what the player reads.
class ProjectSession {
public:
    // Open an existing project. Returns false and leaves the session untouched
    // if the directory has no readable project.toml -- a failed open must not
    // close what was already open.
    bool open(const std::string& dir);

    // Create a project and open it. Fails on a directory that is already one.
    bool create(const std::string& dir, const std::string& name);

    void close() { mProject.reset(); }
    bool isOpen() const { return mProject.has_value(); }

    // Only valid while isOpen(). Callers that might not have one ask through
    // the accessors below instead.
    const eng::runtime::Project& project() const { return *mProject; }

    // The directory the editor saves, cooks and validates against: the
    // project's when one is open, and the fallback -- the shipped content pack
    // -- when none is. This is the single question the rest of the editor asks
    // this class, which is why it takes the fallback rather than knowing it.
    std::string contentRoot(const std::string& fallback) const;

    // Where a cooked map goes for a playtest. Inside the project's work
    // directory when one is open, so cooking never writes into somebody's
    // source tree; beside the .scn otherwise, which is where this editor has
    // always put it.
    std::string cookTarget(const std::string& scenePath,
                           const std::string& fallbackRoot) const;

    // The scene to open when a project is opened: its main scene, absolute.
    std::string mainScenePath() const;

    // --- recents ---------------------------------------------------------
    // Most recent first, capped. Stored as directories, and pruned on load of
    // anything that is no longer a project -- a list whose entries do not open
    // is worse than no list.
    const std::vector<std::string>& recents() const { return mRecents; }
    void loadRecents(const std::string& path);
    void saveRecents(const std::string& path) const;

private:
    void noteRecent(const std::string& dir);

    std::optional<eng::runtime::Project> mProject;
    std::vector<std::string> mRecents;
};

} // namespace ed
