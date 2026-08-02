#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace ed {

// Picking a file off disk, without typing its path.
//
// The model importer used to be a text field. The one GLB actually in this
// repository lives at
//
//   assets/source/models/dungeon/entire map/Modular Dungeon Free Edition/
//   ModularDungeonFree.glb
//
// -- four directories deep, with spaces in two of them. Typing that correctly
// is the whole feature's difficulty, and getting it wrong reports only "model
// import needs an existing .glb file".
//
// Source art is nested and irregular, so the primary listing is a bounded
// recursive scan of one root rather than a folder-at-a-time walk: the author
// knows the model's name, not which of six directories somebody filed it under.
// Directory navigation is still here for reaching a download outside the tree.
//
// Split from the panel that draws it for the same reason SceneBrowser is: the
// failure mode is silent. A filter matching nothing, a scan that quietly hit
// its cap, and an empty directory all look identical in a dialog.

struct FileEntry {
    std::string path;      // absolute
    std::string label;     // what the author reads: relative to the scan root
    bool directory = false;
};

// Guard rails for the recursive scan. Pointing it at a home directory must not
// hang the editor, and a listing nobody can read is not a listing.
struct ScanLimits {
    std::size_t maxFiles = 400;
    int maxDepth = 8;
};

struct ScanResult {
    std::vector<FileEntry> files;
    // True when the walk stopped early. The dialog says so -- a silently
    // truncated list is one where the author concludes their model is missing.
    bool truncated = false;
};

// True when `path` ends with one of `extensions` (each lowercase, with the
// dot), compared case-insensitively. An empty list accepts everything.
bool hasExtension(const std::string& path,
                  const std::vector<std::string>& extensions);

// Every matching file under `root`, recursively, sorted by label. Unreadable
// directories are skipped rather than failing the scan: a source tree with one
// permission-denied folder in it should still list the other nine.
ScanResult findFiles(const std::string& root,
                     const std::vector<std::string>& extensions,
                     const ScanLimits& limits = {});

// One level of `directory`: sub-directories first, then matching files, each
// group by name. For reaching somewhere the scan root does not cover.
std::vector<FileEntry> listDirectory(const std::string& directory,
                                     const std::vector<std::string>& extensions);

// Case-insensitive substring match on the label. An empty query keeps
// everything. Directories are always kept, so navigation survives a filter.
std::vector<FileEntry> filterFiles(const std::vector<FileEntry>& entries,
                                   const std::string& query);

// The parent of `directory`, or "" when it is already a root.
std::string parentDirectory(const std::string& directory);

} // namespace ed
