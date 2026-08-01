#pragma once

#include <string>
#include <vector>

namespace ed {

// The ids the game defines, for the editor's drop-downs.
//
// An enemy spawn is a *name*: "hollow_soldier". The editor used to take that as
// free text, so authoring one meant having read enemies.toml, and a typo became
// an enemy that never appeared -- in the game, some minutes later, with nothing
// pointing at the cause.
//
// Read by scanning the file's table headers rather than through EnemyLibrary:
// the library resolves inheritance, attacks and weapons, and pulls the combat
// vocabulary in with it. The editor needs the list of names, not the enemies,
// and a scene editor that cannot start because a weapons table is malformed is
// a scene editor that has taken on a dependency it did not need.
//
// The cost of that choice is that this knows the file's shape. It is one line
// of shape -- `[enemy.<id>]` -- and `game_vocabulary_tests` pins it against the
// shipped file, so a format change fails a test rather than silently emptying
// a drop-down.

// Every spawnable enemy id in `enemiesToml`, sorted, without duplicates.
//
// `[enemy.hollow]` yields "hollow". Sub-tables (`[enemy.hollow.stats]`) and
// archetypes (`[archetype.trash]`, which never spawn) are skipped.
std::vector<std::string> enemyIdsFromToml(const std::string& enemiesToml);

// The ids named by top-level `[<section>.<id>]` tables. The general form of the
// above, so a second vocabulary does not need a second parser.
std::vector<std::string> tomlSectionIds(const std::string& path,
                                        const std::string& section);

} // namespace ed
