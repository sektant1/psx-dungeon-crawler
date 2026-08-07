# --- Shared content libraries ------------------------------------------------
# The scene/authoring code is compiled once here instead of being listed again
# in every consumer (it was in five source lists: game, mapgen, and three test
# targets). Two libraries, because the consumers split cleanly in two:
#
#   eng_ecs_headless  the engine sources a headless tool needs (the component
#                     registry, plus eng_core underneath it), for CLI/test
#                     targets that must NOT drag in Ogre/SDL/Jolt to read a
#                     .map.
#   game_content      the temporarily named authoring/runtime scene bridge:
#                     stable component ids, serializers and scene cooking.
#                     Renderer-free by construction.
#
# NEVER link eng_ecs_headless together with eng: eng already contains those
# objects (whole-archive), so linking both is a duplicate-symbol error. Windowed
# targets link eng; headless ones link eng_ecs_headless.
#
# It links eng_core rather than recompiling ByteStream.cpp of its own. eng_core
# is Ogre/SDL/Jolt-free -- log, io, diagnostics, and eng::assets -- so the "no
# heavy dependencies" property is unchanged, and a headless tool now gets the
# content resolver for free. Listing ByteStream.cpp here *as well* would put two
# definitions in one link.
# MapSerializer sits beside the registry it walks, and is listed here AND in
# eng_framework for the same reason ComponentRegistry.cpp is: a headless tool
# links one, a windowed app links the other, and neither links both.
#
# It moved out of game_content because it never belonged there. writeMap and
# readMap are handed a `const eng::ecs::ComponentRegistry&`, so the codec has
# never known which components exist -- only how to walk a table of them. The
# game's table was simply the only one anybody passed it. raven_player passes
# the engine's, which is what lets a project play a scene without linking the
# game's component vocabulary, its actor sounds and its cooker.
#
# The cooker deliberately did NOT move with it: SceneCook emits game::Exit,
# game::EnemySpawn, game::Pickup and the rest by name, because which authored
# field becomes which component is this game's model. Making that pluggable is
# a later milestone; playing a cooked map does not need it.
add_library(eng_ecs_headless STATIC engine/src/ecs/ComponentRegistry.cpp
                                    engine/src/ecs/MapSerializer.cpp)
target_include_directories(eng_ecs_headless PUBLIC engine/include
                           PRIVATE third_party)
target_link_libraries(eng_ecs_headless PUBLIC glm::glm EnTT::EnTT eng_core)
eng_target_hardening(eng_ecs_headless)

add_library(
  game_content STATIC
  game/src/scene/ComponentRegistry.cpp
  # The actor/action vocabulary. In the content library rather than beside the
  # audio system because the editor and the headless cooker need it and neither
  # links miniaudio: what an enemy's actions ARE is content, and which clip
  # plays for one is runtime.
  game/src/audio/ActorSounds.cpp
  game/src/scene/LayoutToScene.cpp
  editor/src/content/KitCatalog.cpp
  editor/src/content/SceneDocument.cpp
  editor/src/content/SceneSource.cpp
  editor/src/content/SceneWriter.cpp
  editor/src/content/SceneCook.cpp
  editor/src/content/SceneInstancing.cpp
  editor/src/content/GridMath.cpp
  editor/src/content/SceneValidate.cpp
  editor/src/content/SceneContract.cpp
  editor/src/content/SceneRepair.cpp
  editor/src/content/RoomBuilder.cpp
  editor/src/content/SceneTemplates.cpp)
target_include_directories(game_content PUBLIC game/src game/src/scene
                                               engine/include
                            PRIVATE editor/include third_party)
# lua, not eng_script: SceneValidate only *compiles* authored scripts to check
# their syntax. It never runs one, so it needs the parser and none of the host.
target_link_libraries(game_content PUBLIC glm::glm EnTT::EnTT
                      PRIVATE eng_toml nlohmann_json::nlohmann_json lua)
eng_target_hardening(game_content)

# The RPG layer: skills, items, inventory, loot, quests, dialogue, trading, the
# safehouse, the raid state machine and the save codec.
#
# A static library rather than sources compiled into `game`, for the same reason
# game_content is one: the pure half must compile with no renderer, no physics
# and no registry so the tests can exercise it exhaustively and cheaply, and a
# link error is a better guard on that than a convention. PickupSystem and
# RpgRuntime are the two files that need a world, and they are here because the
# alternative was a second library for two files.
add_library(
  game_rpg STATIC
  game/src/rpg/RpgTypes.cpp
  game/src/rpg/Skills.cpp
  game/src/rpg/Stats.cpp
  game/src/rpg/Items.cpp
  game/src/rpg/Inventory.cpp
  game/src/rpg/LossPolicy.cpp
  game/src/ui/GameUiData.cpp
  game/src/ui/UiScreens.cpp
  game/src/rpg/WorldState.cpp
  game/src/rpg/Quests.cpp
  game/src/rpg/Dialogue.cpp
  game/src/rpg/Trading.cpp
  game/src/rpg/Hideout.cpp
  game/src/rpg/RaidState.cpp
  game/src/rpg/RpgSave.cpp
  game/src/rpg/PickupSystem.cpp
  game/src/rpg/Npcs.cpp
  game/src/rpg/NpcSystem.cpp
  game/src/rpg/RpgRuntime.cpp)
target_include_directories(game_rpg PUBLIC game/src engine/include
                           PRIVATE third_party)
target_link_libraries(game_rpg PUBLIC glm::glm EnTT::EnTT eng
                      PRIVATE eng_toml)
eng_target_hardening(game_rpg)

