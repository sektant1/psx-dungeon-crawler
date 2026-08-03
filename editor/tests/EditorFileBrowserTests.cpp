// The model picker's listing.
//
// The failure mode here is silent: a filter that matches nothing, a scan that
// hit its cap, and an empty directory all look identical in a dialog, so every
// one of those is checked explicitly rather than by eye.

#include <editor/assets/FileBrowser.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace fs = std::filesystem;
using namespace ed;

static void require(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "EditorFileBrowserTests: " << message << '\n';
        std::exit(1);
    }
}

static void touch(const fs::path& path)
{
    fs::create_directories(path.parent_path());
    std::ofstream(path) << "x";
}

static bool has(const std::vector<FileEntry>& entries, const std::string& label)
{
    for (const FileEntry& entry : entries)
        if (entry.label == label)
            return true;
    return false;
}

int main()
{
    std::error_code ec;
    const fs::path root = fs::temp_directory_path() / "raven_file_browser_tests";
    fs::remove_all(root, ec);

    // A shape like the real source tree: models filed several directories deep,
    // with spaces, beside files of other kinds.
    touch(root / "top.glb");
    touch(root / "models" / "a.glb");
    touch(root / "models" / "notes.txt");
    touch(root / "models" / "entire map" / "Free Edition" / "deep.glb");
    touch(root / "models" / "old.fbx");

    const std::vector<std::string> glb = {".glb"};

    // --- extensions --------------------------------------------------------
    require(hasExtension("a.glb", glb), "matches by extension");
    require(hasExtension("A.GLB", glb), "case-insensitively");
    require(!hasExtension("a.fbx", glb), "and rejects others");
    require(hasExtension("a.fbx", {}), "an empty list accepts everything");

    // --- the recursive scan ------------------------------------------------
    {
        const ScanResult scan = findFiles(root.string(), glb);
        require(!scan.truncated, "a small tree is not truncated");
        require(scan.files.size() == 3,
                "every .glb at any depth is found, and nothing else");
        require(has(scan.files, "top.glb"), "including one at the root");
        // The whole point: the author knows the name, not the six directories
        // somebody filed it under.
        require(has(scan.files,
                    fs::path("models/entire map/Free Edition/deep.glb")
                        .make_preferred()
                        .string()),
                "and one four directories down, labelled by its path");
    }
    {
        const ScanResult scan = findFiles(root.string(), {});
        require(scan.files.size() == 5, "an empty filter lists every file");
    }
    {
        ScanLimits limits;
        limits.maxFiles = 2;
        const ScanResult scan = findFiles(root.string(), glb, limits);
        require(scan.files.size() == 2, "the cap is honoured");
        require(scan.truncated,
                "and reported -- a silently short list is one where the author "
                "concludes their model is missing");
    }
    {
        ScanLimits limits;
        limits.maxDepth = 1;
        const ScanResult scan = findFiles(root.string(), glb, limits);
        require(!has(scan.files,
                     fs::path("models/entire map/Free Edition/deep.glb")
                         .make_preferred()
                         .string()),
                "the depth cap stops the walk");
    }
    {
        const ScanResult scan = findFiles((root / "nope").string(), glb);
        require(scan.files.empty() && !scan.truncated,
                "a missing directory is empty, not an error");
    }

    // --- one level ---------------------------------------------------------
    {
        const std::vector<FileEntry> entries =
            listDirectory((root / "models").string(), glb);
        require(has(entries, "a.glb"), "matching files are listed");
        require(!has(entries, "notes.txt"), "others are not");
        require(has(entries, "entire map"), "sub-directories always are");
        require(entries.front().directory,
                "and come first, so navigating is one predictable region");
    }

    // --- filtering ---------------------------------------------------------
    {
        const std::vector<FileEntry> entries =
            listDirectory((root / "models").string(), glb);
        const std::vector<FileEntry> kept = filterFiles(entries, "A.GL");
        require(has(kept, "a.glb"), "the filter is case-insensitive");
        require(has(kept, "entire map"),
                "and never strands the author in a folder they cannot leave");
        require(filterFiles(entries, "").size() == entries.size(),
                "an empty query keeps everything");
        require(filterFiles(entries, "zzz").size() == 1,
                "a query matching no file still leaves the directories");
    }

    // --- parents -----------------------------------------------------------
    require(parentDirectory((root / "models").string()) == root.string(),
            "the parent is one level up");
    require(parentDirectory("/").empty(), "and empty at a root");

    fs::remove_all(root, ec);
    std::cout << "EditorFileBrowserTests: ok\n";
    return 0;
}
