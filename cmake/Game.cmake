# The game, and the content it cooks at build time.
#
# Also the two headless harnesses that share its sources: `mapgen` (dungeon
# generation to a .map) and `game_sim` (the combat simulation, which doubles as
# the sim_smoke ctest).


add_executable(
  game
  game/src/main.cpp
  game/src/DungeonMap.cpp
  game/src/DungeonGen.cpp
  game/src/PropSystem.cpp
  game/src/CombatSystem.cpp
  game/src/BloodSystem.cpp
  game/src/audio/GameAudio.cpp
  game/src/audio/ActorAudio.cpp
  game/src/ParticleCollider.cpp
  game/src/DebugOverlay.cpp
  game/src/ui/GameHud.cpp
  game/src/ui/GameHudStyle.cpp
  game/src/ui/TooltipBuilder.cpp
  game/src/HudModel.cpp
  game/src/HitFeel.cpp
  game/src/combat/DamageSystem.cpp
  game/src/combat/BulletPattern.cpp
  game/src/combat/StatusEffectSystem.cpp
  game/src/combat/WeaponLibrary.cpp
  game/src/combat/CombatVocabulary.cpp
  game/src/combat/CombatDirector.cpp
  game/src/combat/StaminaSystem.cpp
  game/src/combat/PoiseSystem.cpp
  game/src/combat/ActionStateSystem.cpp
  game/src/combat/DefenseSystem.cpp
  game/src/enemy/EnemyLibrary.cpp
  game/src/enemy/EnemyAI.cpp
  game/src/enemy/EnemySystem.cpp
  game/src/enemy/EnemySpawner.cpp
  game/src/enemy/EnemySpawnerRuntime.cpp
  game/src/enemy/EnemySave.cpp
  game/src/enemy/EnemySaveRuntime.cpp
  # The body every actor wears: the shared humanoid rig, the animation state
  # machine that drives it, and the node/skin pair each actor owns.
  game/src/actor/ActorRig.cpp
  game/src/actor/ActorAnimator.cpp
  game/src/actor/ActorVisual.cpp
  game/src/PlayerWeapons.cpp
  game/src/PlayerSystem.cpp
  game/src/InteractionSystem.cpp
  game/src/ScriptGameplay.cpp
  game/src/ScriptEventBridge.cpp
  # Written, used by main.cpp and DebugOverlay.cpp, and never added here, so
  # every link failed on four undefined LockOnSystem symbols.
  game/src/LockOn.cpp
  game/src/LiveLevel.cpp
  game/src/LevelDocument.cpp
  game/src/LevelResource.cpp
  game/src/Projectiles.cpp
  # The two deliveries that spawn no body: melee sweeps and hitscan rays.
  game/src/WeaponDelivery.cpp
  game/src/LobbyDressing.cpp
  game/src/ParticleEffects.cpp
  game/src/SceneFactory.cpp
  game/src/PrototypeCatalogLoader.cpp
  game/src/RenderPalette.cpp
  game/src/Targeting.cpp
  game/src/Dummy.cpp
  game/src/ViewModel.cpp
  game/src/ViewmodelMotion.cpp
  game/src/FirstPersonHands.cpp
  game/src/ViewmodelSocket.cpp
  game/src/HandsDefinition.cpp
  game/src/WeaponViewmodel.cpp
  # The flat layered first-person presentation, beside the skinned one.
  game/src/SpriteViewmodel.cpp
  game/src/MapPlay.cpp
  game/src/scene/MapRuntime.cpp)
target_include_directories(game PRIVATE third_party samples/common engine/src
                                        game/src game/src/scene)
# eng_runtime, because the game plays its levels through the same ProjectApp
# raven_player does -- the game is what proves that runtime is sufficient.
target_link_libraries(game PRIVATE demo_scene game_content game_rpg eng_runtime)
eng_target_hardening(game)

# Explicit source-asset cook. Normal builds consume checked-in cooked content
# and never write into source tree; enable tool only on an asset-authoring build
# and invoke `cook_first_person_hands` deliberately after changing source rig.
set(_hands_source_dir
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/source/fps-animated-hand-assets")
set(_hands_animation_dir
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/animations/viewmodels/arms")
set(_hands_animation_stage
    "${CMAKE_CURRENT_BINARY_DIR}/cooked-assets/first-person-hands")
set(_hands_mesh_dir "${CMAKE_CURRENT_SOURCE_DIR}/assets/meshes/viewmodels")
set(_hands_texture_dir "${CMAKE_CURRENT_SOURCE_DIR}/assets/textures/viewmodels")
if(ENG_BUILD_ANIMATION_TOOLS AND TARGET gltf2ozz AND
   EXISTS "${_hands_source_dir}/arms_rig.glb")
  add_custom_target(
    cook_first_person_hands
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${_hands_animation_stage}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_hands_animation_stage}"
    COMMAND ${CMAKE_COMMAND} -E chdir "${_hands_animation_stage}"
            $<TARGET_FILE:gltf2ozz>
            --file=${_hands_source_dir}/arms_rig.glb
            --config_file=${CMAKE_CURRENT_SOURCE_DIR}/assets/config/arms_rig.ozz.json
            --endian=little
    # Converter succeeded: only now replace destination, preventing stale clips
    # and preserving last good cook on conversion failure.
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${_hands_animation_dir}"
    COMMAND ${CMAKE_COMMAND} -E rename "${_hands_animation_stage}"
            "${_hands_animation_dir}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_hands_mesh_dir}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_hands_texture_dir}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${_hands_source_dir}/arms_rig.glb"
            "${_hands_mesh_dir}/arms_rig.glb"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${_hands_source_dir}/arms_01.png"
            "${_hands_texture_dir}/arms_01.png"
    DEPENDS gltf2ozz "${_hands_source_dir}/arms_rig.glb"
            "${_hands_source_dir}/arms_01.png"
            assets/config/arms_rig.ozz.json
    COMMENT "Cooking checked-in first-person hands runtime assets"
    VERBATIM)
endif()

# The actor humanoid, on the same terms and for the same reason.
#
# One extra step in front of the converter: unlike the hands, whose .glb was
# downloaded already rigged, this rig does not exist until Blender builds it.
# tools/author_humanoid_rig.py turns the kit's own mannequin mesh into a rigged,
# animated .glb; gltf2ozz then cooks that into the runtime skeleton and clips.
# Both halves are checked in, so a normal build and a fresh checkout consume the
# result and never need Blender.
set(_actor_source "${CMAKE_CURRENT_SOURCE_DIR}/assets/source/models/actors")
set(_actor_animation_dir
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/animations/actors/humanoid")
set(_actor_animation_stage
    "${CMAKE_CURRENT_BINARY_DIR}/cooked-assets/actor-humanoid")
set(_actor_mesh_dir "${CMAKE_CURRENT_SOURCE_DIR}/assets/meshes/actors")
if(ENG_BUILD_ANIMATION_TOOLS AND TARGET gltf2ozz)
  add_custom_target(
    cook_actor_humanoid
    # Author: Humanoid.blend -> a rigged, animated .glb.
    COMMAND ${CMAKE_COMMAND} -E env
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/author_humanoid_rig.py"
            --out "${_actor_source}"
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${_actor_animation_stage}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_actor_animation_stage}"
    # Condition: .glb -> skeleton + clips.
    COMMAND ${CMAKE_COMMAND} -E chdir "${_actor_animation_stage}"
            $<TARGET_FILE:gltf2ozz>
            --file=${_actor_source}/humanoid_rig.glb
            --config_file=${CMAKE_CURRENT_SOURCE_DIR}/assets/config/humanoid_rig.ozz.json
            --endian=little
    # Converter succeeded: only now replace the destination, so a failed cook
    # leaves the last good clips in place rather than an empty directory that
    # makes every actor fall back to a capsule.
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${_actor_animation_dir}"
    COMMAND ${CMAKE_COMMAND} -E rename "${_actor_animation_stage}"
            "${_actor_animation_dir}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_actor_mesh_dir}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${_actor_source}/humanoid_rig.glb"
            "${_actor_mesh_dir}/humanoid_rig.glb"
    DEPENDS gltf2ozz tools/author_humanoid_rig.py
            assets/config/humanoid_rig.ozz.json
            assets/source/models/base_player_mesh/Humanoid.blend
    COMMENT "Authoring and cooking the shared actor humanoid rig"
    VERBATIM)
endif()
if(ENABLE_PCH)
  target_precompile_headers(game PRIVATE <entt/entt.hpp> <glm/glm.hpp>
                            <string> <vector> <unordered_map>)
endif()

add_executable(mapgen game/src/mapgen_main.cpp game/src/DungeonGen.cpp)
target_include_directories(mapgen PRIVATE game/src game/src/scene
                                          engine/include third_party)
target_link_libraries(mapgen PRIVATE game_content eng_ecs_headless)
eng_target_hardening(mapgen)

# Headless action-simulation harness: drives the renderer-free combat + physics
# systems from a text action script (game/sim/), no window. Links eng for
# Physics; combat sources compiled in directly.
add_executable(
  game_sim
  game/sim/sim_main.cpp game/sim/SimWorld.cpp game/src/combat/DamageSystem.cpp
  game/src/combat/StatusEffectSystem.cpp game/src/combat/WeaponLibrary.cpp
  game/src/combat/CombatDirector.cpp game/src/combat/CombatVocabulary.cpp)
target_include_directories(game_sim PRIVATE game/src game/sim engine/include
                                            third_party)
target_link_libraries(game_sim PRIVATE eng eng_toml glm::glm EnTT::EnTT)
eng_target_hardening(game_sim)

