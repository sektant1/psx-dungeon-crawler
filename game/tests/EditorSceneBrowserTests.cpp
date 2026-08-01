// Listing scenes to open, and the recent list.
//
// The failure mode this guards is silent: a filter that matches nothing and a
// directory that could not be read look identical on screen -- an empty dialog
// -- so the behaviour has to be pinned somewhere other than the eye.

#include "SceneBrowser.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

using namespace ed;

static void require(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "EditorSceneBrowserTests: " << message << '\n';
        std::exit(1);
    }
}

static void write(const std::filesystem::path& path)
{
    std::ofstream out(path);
    out << "# scene\n";
}

int main()
{
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "psx_editor_scene_browser";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root / "scenes", ec);

    // --- listing -------------------------------------------------------------
    {
        write(root / "scenes" / "crypt.scn");
        write(root / "scenes" / "Atrium.scn");
        write(root / "scenes" / "notes.txt");
        write(root / "scenes" / "crypt.map");
        std::filesystem::create_directories(root / "scenes" / "sub", ec);
        write(root / "scenes" / "sub" / "nested.scn");

        // A backup sitting beside the scene it backs up. It is a .scn by
        // extension, and it must not appear in a list of scenes to open.
        // Named for Atrium rather than crypt: the recovery case below asserts
        // that crypt has no backup yet.
        write(root / "scenes" / "Atrium.autosave.scn");

        const std::vector<SceneEntry> entries =
            listScenes((root / "scenes").string());
        require(entries.size() == 2,
                "only .scn files, only at the top level, and no autosaves -- "
                "browsing to a backup is how a recovery quietly becomes the "
                "working file");
        require(isAutosavePath("/x/crypt.autosave.scn"),
                "a backup is recognised by its name");
        require(!isAutosavePath("/x/crypt.scn"), "and a real scene is not");
        require(entries[0].name == "Atrium.scn" && entries[1].name == "crypt.scn",
                "sorted by name, not by whatever order the filesystem hands "
                "back");
        require(std::filesystem::exists(entries[0].path),
                "the path is openable as given");
    }

    // --- a project with no scenes yet is empty, not a failure ----------------
    {
        require(listScenes((root / "does_not_exist").string()).empty(),
                "a missing directory lists nothing");
        require(listScenes("").empty(), "and so does an empty path");
    }

    // --- filtering -----------------------------------------------------------
    {
        const std::vector<SceneEntry> all = listScenes((root / "scenes").string());
        require(filterScenes(all, "").size() == all.size(),
                "an empty query keeps everything");
        require(filterScenes(all, "CRYPT").size() == 1,
                "matching ignores case, so shift-typing still finds the file");
        require(filterScenes(all, "ryp").front().name == "crypt.scn",
                "and matches inside the name, not only at the start");
        require(filterScenes(all, "zzz").empty(), "no match is no rows");
    }

    // --- recent list ---------------------------------------------------------
    {
        RecentScenes recent;
        recent.touch("a.scn");
        recent.touch("b.scn");
        recent.touch("a.scn");
        require(recent.paths().size() == 2, "reopening a scene is not a new row");
        require(recent.paths().front() == "a.scn",
                "the most recently opened is first");

        for (int i = 0; i < 40; ++i)
            recent.touch("scene" + std::to_string(i) + ".scn");
        require(recent.paths().size() == RecentScenes::kMax,
                "the list is bounded -- it is written to disk on every open");
        require(recent.paths().front() == "scene39.scn",
                "and the newest survives the truncation, not the oldest");

        recent.remove(recent.paths().front());
        require(recent.paths().front() == "scene38.scn",
                "a scene that no longer exists can be dropped");
        recent.touch("");
        require(recent.paths().front() == "scene38.scn",
                "an empty path is not a recent scene");
    }

    // --- round trip through disk --------------------------------------------
    {
        RecentScenes recent;
        recent.touch("one.scn");
        recent.touch("two.scn");
        const std::string file = (root / "state" / "recent.txt").string();
        require(recent.save(file), "saving creates the directory it needs");

        RecentScenes loaded;
        loaded.load(file);
        require(loaded.paths() == recent.paths(),
                "the order survives the round trip");

        RecentScenes missing;
        missing.load((root / "nope.txt").string());
        require(missing.paths().empty(),
                "a first run has no file, and that is not an error");
    }

    // --- autosave paths ------------------------------------------------------
    {
        const std::string scene = (root / "scenes" / "crypt.scn").string();
        const std::string backup = autosavePath(scene, "/fallback");
        require(backup.find("crypt.autosave.scn") != std::string::npos,
                "the backup is named after the scene, beside it");
        require(backup.rfind(".scn") == backup.size() - 4,
                "and stays a scene file, so recovering it is just opening it");
        require(autosavePath("", "/fallback") ==
                    std::string("/fallback/untitled.autosave.scn"),
                "a scene with no file still gets a backup -- that is the work "
                "the editor could actually lose");
        require(autosavePath("", "").empty(),
                "with nowhere to put it, there is no backup path");
    }

    // --- recovery is offered only when there is something to recover ---------
    {
        const std::string scene = (root / "scenes" / "crypt.scn").string();
        require(!autosaveIsStale(scene, root.string()),
                "no backup, nothing to offer");

        const std::string backup = autosavePath(scene, root.string());
        write(backup);
        // The backup was written after the scene, which is what an editor that
        // died with unsaved work leaves behind.
        std::filesystem::last_write_time(
            scene, std::filesystem::file_time_type::clock::now() -
                       std::chrono::hours(1), ec);
        require(autosaveIsStale(scene, root.string()),
                "a backup newer than the scene means unsaved work survived");

        std::filesystem::last_write_time(
            scene, std::filesystem::file_time_type::clock::now() +
                       std::chrono::hours(1), ec);
        require(!autosaveIsStale(scene, root.string()),
                "a scene saved since is not a recovery prompt -- offering one "
                "anyway teaches the author to dismiss it");
    }

    std::filesystem::remove_all(root, ec);
    std::cout << "EditorSceneBrowserTests: ok\n";
    return 0;
}
