#pragma once

#include <string>
#include <vector>

namespace ed {

// Bringing scenes authored against this game into a project.
//
// A scene made for the dungeon crawler and a scene made in a project are the
// same file format, so "migrating" is not a conversion in the usual sense --
// nothing is rewritten into a different shape. What differs is the *vocabulary*
// a runtime reads it with: raven_player registers the engine's components and
// whatever the project declares, and skips the rest (see docs/projects.md).
//
// One of those skipped components matters more than all the others: the
// player's spawn. `player_spawn` is one of THIS game's markers, so a migrated
// scene would load with the geometry intact and nowhere for the player to
// stand -- which renders as an empty world seen from the origin, and looks like
// a broken migration rather than a missing component.
//
// So the migration's real job is one transformation: give every scene that
// marks a spawn an equivalent `first_person` rig at the same place.
// SceneRuntime::playerSpawn reads that, the scene contract counts it, and the
// original marker is LEFT IN PLACE -- so the migrated scene plays in the game
// exactly as it did before and in raven_player for the first time.
//
// Everything else this game's scenes carry -- exits, enemy spawns, pickups,
// triggers, markers -- is deliberately not translated. Those are not lost
// detail; they are gameplay this runtime has no systems for, and inventing
// stand-ins would produce a scene that claims to do something it cannot.
// They are reported, per scene, so the loss is stated rather than discovered.

struct MigratedScene {
    std::string source;  // where it came from
    std::string logical; // "scenes/start_hall.scn" in the project
    int entities = 0;
    // True when this migration added a first-person rig, i.e. the scene marked
    // a spawn and had no rig of its own.
    bool addedPlayerRig = false;
    // Authored fields the player's vocabulary does not include, counted by
    // name: "exit", "enemy_spawn", "pickup", "trigger", "marker".
    std::vector<std::pair<std::string, int>> droppedMarkers;
    std::string error; // non-empty when this scene could not be migrated
};

struct MigrateReport {
    bool ok = false;
    std::string error;
    std::vector<MigratedScene> scenes;
    std::string projectDir;
};

struct MigrateOptions {
    // Directory holding the .scn files to bring over.
    std::string sceneDir;
    // Where the project goes. Created if it does not exist; an existing
    // project is added to rather than replaced, so a migration can be re-run
    // after fixing one scene.
    std::string projectDir;
    std::string projectName;
    // Which scene the project opens on. Empty picks the one with the most
    // entities, which for a content tree of demos is the real level rather
    // than a probe.
    std::string mainScene;
};

// Copies every .scn under `sceneDir` into the project and applies the spawn
// transformation. Does not cook: cooking is the exporter's and the editor's
// job, and a migration that also cooked would hide which of the two failed.
MigrateReport migrateScenes(const MigrateOptions& options);

} // namespace ed
