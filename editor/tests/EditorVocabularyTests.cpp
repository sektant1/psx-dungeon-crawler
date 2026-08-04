// The ids the editor offers in its drop-downs, read out of the game's tables.
//
// This knows one line of enemies.toml's shape -- `[enemy.<id>]` -- so that the
// editor does not have to link the enemy library to fill a combo box. The test
// runs against the shipped file, which is what turns "the format changed" from
// a silently empty drop-down into a failing build.

#include <editor/assets/GameVocabulary.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

using namespace ed;

static void require(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "EditorVocabularyTests: " << message << '\n';
        std::exit(1);
    }
}

static bool has(const std::vector<std::string>& ids, const std::string& id)
{
    for (const std::string& candidate : ids)
        if (candidate == id)
            return true;
    return false;
}

static std::string writeToml(const std::filesystem::path& path,
                             const std::string& body)
{
    std::ofstream out(path);
    out << body;
    return path.string();
}

int main(int argc, char** argv)
{
    // --- the shipped table --------------------------------------------------
    if (argc > 1) {
        const std::vector<std::string> ids = enemyIdsFromToml(argv[1]);
        require(!ids.empty(),
                "the shipped enemies.toml yields ids -- an empty list here is "
                "an editor whose enemy field silently went back to free text");
        require(has(ids, "hollow"),
                "including the one every level starts with");
        for (const std::string& id : ids) {
            require(id.find('.') == std::string::npos,
                    "and none of them is a sub-table: '" + id + "'");
            require(id.find(']') == std::string::npos &&
                        id.find('[') == std::string::npos,
                    "or a bracket that leaked out of the header: '" + id + "'");
        }
    }

    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "raven_editor_vocabulary";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);

    // --- what counts as an id ------------------------------------------------
    {
        const std::string path = writeToml(root / "enemies.toml", R"(
# a comment mentioning [enemy.commented] which is not a table
[archetype.trash]
health = 10

[enemy.hollow]
inherits = "trash"

[enemy.hollow.stats]
health = 40

[enemy.hollow.stats.resistances]
physical = 0.2

  [enemy.indented]
  inherits = "trash"

[[enemy.array]]
name = "not a table header"

[weapon.sword]
damage = 5
)");
        const std::vector<std::string> ids = enemyIdsFromToml(path);
        require(has(ids, "hollow"), "a plain table is an enemy");
        require(has(ids, "indented"),
                "and so is an indented one -- TOML does not care and neither "
                "should this");
        require(!has(ids, "hollow.stats"),
                "a sub-table is not a second enemy");
        require(!has(ids, "trash"),
                "an archetype never spawns, so it is never offered");
        require(!has(ids, "array"),
                "an array-of-tables is not a table header");
        require(!has(ids, "commented"),
                "and a bracket inside a comment is not one either");
        require(!has(ids, "sword"), "nor is another section's table");
        require(ids.size() == 2, "exactly the two real enemies");
    }

    // --- sorted, deduplicated ------------------------------------------------
    {
        const std::string path = writeToml(root / "dupes.toml", R"(
[enemy.zebra]
[enemy.aardvark]
[enemy.zebra]
)");
        const std::vector<std::string> ids = enemyIdsFromToml(path);
        require(ids.size() == 2, "a repeated table is one id");
        require(ids[0] == "aardvark" && ids[1] == "zebra",
                "and the list is sorted, so the drop-down does not reorder "
                "itself when the file is edited");
    }

    // --- the general form ----------------------------------------------------
    {
        const std::string path = writeToml(root / "mixed.toml", R"(
[pickup.potion]
[pickup.key]
[enemy.hollow]
)");
        const std::vector<std::string> pickups = tomlSectionIds(path, "pickup");
        require(pickups.size() == 2, "one section is read at a time");
        require(has(pickups, "potion") && has(pickups, "key"),
                "and it reads that section's ids");
        require(tomlSectionIds(path, "nothing").empty(),
                "a section that is not there is empty, not an error");
    }

    // --- degenerate ----------------------------------------------------------
    {
        require(enemyIdsFromToml((root / "missing.toml").string()).empty(),
                "a missing file leaves the field free text rather than "
                "refusing to open the editor");
        const std::string empty = writeToml(root / "empty.toml", "");
        require(enemyIdsFromToml(empty).empty(), "and so does an empty one");
        const std::string junk =
            writeToml(root / "junk.toml", "[enemy.\n[enemy.]\n[\n[]\n[enemy.]");
        require(enemyIdsFromToml(junk).empty(),
                "an unterminated or empty header yields nothing rather than a "
                "garbage entry");
    }

    // --- lua script paths ----------------------------------------------------
    {
        const std::filesystem::path scripts = root / "scripts";
        std::filesystem::create_directories(scripts / "traps", ec);
        std::ofstream(scripts / "door.lua") << "return {}\n";
        std::ofstream(scripts / "lever.lua") << "return {}\n";
        std::ofstream(scripts / "traps" / "spike.lua") << "return {}\n";
        std::ofstream(scripts / "notes.txt") << "not a script\n";

        const std::vector<std::string> found =
            luaScriptPaths(scripts.string(), "scripts");
        require(found.size() == 3, "only .lua files are offered");
        require(found[0] == "scripts/door.lua" && found[1] == "scripts/lever.lua",
                "sorted, and named by the logical path a scene stores -- not "
                "the filesystem path of the machine that authored it");
        require(has(found, "scripts/traps/spike.lua"),
                "subdirectories are included: scripts group as a level grows, "
                "and a picker that hid them would hide half the list");

        require(luaScriptPaths((root / "no_such_dir").string(), "scripts").empty(),
                "a missing directory leaves the field free text rather than "
                "refusing to open the editor");
    }

    std::filesystem::remove_all(root, ec);
    std::cout << "EditorVocabularyTests: ok\n";
    return 0;
}
