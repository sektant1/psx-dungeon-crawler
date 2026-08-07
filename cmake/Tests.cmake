if(BUILD_TESTING)
  # Architecture + content checks. These are ctests rather than review rules:
  # an upward include and a dangling asset reference both fail quietly at
  # runtime (the second one as a prototype box), so they have to fail here.
  find_package(Python3 COMPONENTS Interpreter)
  if(Python3_Interpreter_FOUND)
    add_test(NAME layering
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_layering.py)
    add_test(NAME assetlint
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tools/assetlint.py)
    # raven_player must contain no game code. The claim the target exists to
    # make, checked against the built binary rather than trusted -- see
    # tools/check_player_purity.py.
    add_test(NAME player_purity
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_player_purity.py
                     $<TARGET_FILE:raven_player>)
  else()
    message(WARNING "Python3 not found: layering + assetlint tests disabled")
  endif()

  eng_add_test(dungeon_layout
    SOURCES game/tests/DungeonLayoutTests.cpp game/src/DungeonGen.cpp
    INCLUDES game/src)

  # The simulation harness doubles as a regression test: its built-in smoke
  # script must pass (exit 0).
  add_test(NAME sim_smoke COMMAND game_sim)

  eng_add_test(level_document
    SOURCES game/tests/LevelDocumentTests.cpp game/src/LevelDocument.cpp game/src/DungeonGen.cpp
    INCLUDES game/src third_party
    LIBS eng_toml)

  eng_add_test(targeting
    SOURCES game/tests/TargetingTests.cpp game/src/Targeting.cpp
    INCLUDES game/src
    LIBS glm::glm)

  eng_add_test(hud_model
    SOURCES game/tests/HudModelTests.cpp game/src/HudModel.cpp
    INCLUDES game/src engine/include third_party
    LIBS glm::glm EnTT::EnTT)

  # The tooltip builder is a pure function of focus + catalog values, so it
  # tests without a renderer, a font atlas or an imgui context.
  eng_add_test(tooltip_builder
    SOURCES game/tests/TooltipBuilderTests.cpp game/src/ui/TooltipBuilder.cpp
    INCLUDES game/src
    LIBS eng_core)

  # The tooltip's pacing is pure logic over its own fields, so it tests with
  # no canvas, no font and no window.
  eng_add_test(tooltip_pace
    SOURCES engine/tests/TooltipPaceTests.cpp engine/src/ui/UiFade.cpp
    INCLUDES engine/include
    LIBS glm::glm)

  # The log backlog is a vector and a mutex: it tests with no renderer, which
  # matters because what it guarantees is that lines logged BEFORE any UI
  # exists are still there when the console opens.
  eng_add_test(log_backlog
    SOURCES engine/tests/LogBacklogTests.cpp engine/src/core/Log.cpp
    INCLUDES engine/include)

  # Compiled from source rather than linked against eng_core, so the test binary
  # gets the operator new overrides whatever the rest of the tree was built
  # with, and -rdynamic for readable stacks. Everything it asserts is a property
  # of those overrides.
  add_executable(memory_profiler_tests engine/tests/MemoryProfilerTests.cpp
                 engine/src/diagnostics/MemoryProfiler.cpp
                 engine/src/core/Log.cpp)
  target_include_directories(memory_profiler_tests PRIVATE engine/include)
  target_compile_definitions(memory_profiler_tests PRIVATE ENG_MEMPROF=1)
  target_link_options(memory_profiler_tests PRIVATE -rdynamic)
  add_test(NAME memory_profiler COMMAND memory_profiler_tests)

  eng_add_test(ui_layout
    SOURCES engine/tests/UiLayoutTests.cpp engine/src/ui/UiLayout.cpp
    INCLUDES engine/include
    LIBS glm::glm)

  # The on-disk formats every exporter writes. A change that silently alters
  # one is a content tree that has to be re-cooked with nothing saying so.
  eng_add_test(asset_format
    SOURCES engine/tests/AssetFormatTests.cpp
    LIBS eng_core)

  # The resource database and the pipeline's incremental behaviour, on a tree
  # the test builds itself. Incrementality only ever fails by being too clever,
  # and the symptom is stale data in a build hours later.
  eng_add_test(acp_pipeline
    SOURCES engine/tests/AcpPipelineTests.cpp
    LIBS eng_acp)

  # The real content tree, conditioned end to end -- the rows that need Assimp
  # and stb, which the unit test above cannot reach. --check, so it also fails
  # when a committed pack has gone stale.
  add_test(NAME acp_content
           COMMAND raven_acp build --out ${CMAKE_BINARY_DIR}/cooked-test --quiet)

  eng_add_test(ui_canvas
    SOURCES engine/tests/UiCanvasTests.cpp
    LIBS eng)

  eng_add_test(game_hud_style
    SOURCES game/tests/GameHudStyleTests.cpp game/src/ui/GameHudStyle.cpp engine/src/ui/UiLayout.cpp
    INCLUDES game/src engine/include
    LIBS glm::glm)

  eng_add_test(debug_console
    SOURCES game/tests/DebugConsoleTests.cpp
    LIBS eng)

  eng_add_test(fps_controller
    SOURCES game/tests/FpsControllerTests.cpp
    LIBS eng)

  eng_add_test(skeletal_animation
    SOURCES engine/tests/SkeletalAnimationTests.cpp
    INCLUDES engine/src
    LIBS eng_systems
    DEFINES PROJECT_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

  # The SHIPPED humanoid clips, not a synthetic rig: does the asset the actors
  # actually wear carry motion, and does a full-weight layer leave the rest
  # pose? Nothing asserted either, so a cook that emitted held poses -- or a
  # blender leaning on rest -- rendered as a mannequin with no test failing.
  eng_add_test(humanoid_clip
    SOURCES engine/tests/HumanoidClipTests.cpp
    INCLUDES engine/src
    LIBS eng_systems
    DEFINES PROJECT_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

  # The camera rigs' framing model: follow smoothing, the pitch clamp, the
  # lock-on blend, the spring arm and the screen fit. No renderer, no window --
  # solve()/boomLength()/fitDistance() are split out of present() precisely so
  # this can be arithmetic.
  eng_add_test(camera_rig
    SOURCES engine/tests/CameraRigTests.cpp
    LIBS eng)

  # Who the lock-on grabs, what it refuses, and what makes it let go.
  eng_add_test(lock_on
    SOURCES game/tests/LockOnTests.cpp game/src/LockOn.cpp
    INCLUDES game/src engine/include
    LIBS glm::glm)

  add_executable(physics_tests game/tests/PhysicsTests.cpp)
  target_include_directories(physics_tests PRIVATE game/src)
  target_link_libraries(physics_tests PRIVATE eng)
  add_test(NAME physics_tests COMMAND physics_tests)

  add_executable(scene_registry_tests engine/tests/SceneRegistryTests.cpp
                                      engine/src/render/SceneRegistry.cpp)
  target_include_directories(
    scene_registry_tests PRIVATE engine/include engine/src third_party/imgui)
  target_link_libraries(scene_registry_tests PRIVATE glm::glm)
  add_test(NAME scene_registry_tests COMMAND scene_registry_tests)

  eng_add_test(component_reflect
    SOURCES engine/tests/ComponentReflectTests.cpp engine/src/ecs/ComponentRegistry.cpp
            engine/src/io/ByteStream.cpp
    INCLUDES engine/include third_party
    LIBS glm::glm EnTT::EnTT)

  # No VM here: this asserts the component's payload format, which is a file
  # format, so it builds the two sources that decide it and nothing else.
  eng_add_test(script_serialize
    SOURCES engine/tests/ScriptSerializeTests.cpp engine/src/ecs/ComponentRegistry.cpp
            engine/src/io/ByteStream.cpp
    INCLUDES engine/include third_party
    LIBS glm::glm EnTT::EnTT)

  eng_add_test(script_error
    SOURCES engine/tests/ScriptErrorTests.cpp
    INCLUDES engine/include engine/src
    LIBS eng_script eng_sol2 glm::glm)

  eng_add_test(script_host
    SOURCES engine/tests/ScriptHostTests.cpp
    INCLUDES engine/include engine/src
    LIBS eng_script glm::glm EnTT::EnTT)

  # Scheduling, and the runtime surface a game is written against.
  eng_add_test(script_timer
    SOURCES engine/tests/ScriptTimerTests.cpp
    INCLUDES engine/include engine/src
    LIBS eng_script glm::glm EnTT::EnTT)

  eng_add_test(script_runtime_api
    SOURCES engine/tests/ScriptRuntimeApiTests.cpp
    INCLUDES engine/include engine/src
    LIBS eng_script glm::glm EnTT::EnTT)

  eng_add_test(script_binding
    SOURCES engine/tests/ScriptBindingTests.cpp
    INCLUDES engine/include engine/src
    LIBS eng_script glm::glm EnTT::EnTT)

  eng_add_test(script_contact
    SOURCES engine/tests/ScriptContactTests.cpp
    INCLUDES engine/include engine/src
    LIBS eng_script glm::glm EnTT::EnTT)

  eng_add_test(script_reload
    SOURCES engine/tests/ScriptReloadTests.cpp
    INCLUDES engine/include engine/src
    LIBS eng_script glm::glm EnTT::EnTT)

  add_executable(scene_tests engine/tests/SceneTests.cpp
                             engine/src/ecs/World.cpp)
  target_include_directories(scene_tests PRIVATE engine/include engine/src
                                                 third_party/imgui)
  target_link_libraries(scene_tests PRIVATE glm::glm EnTT::EnTT)
  add_test(NAME scene_tests COMMAND scene_tests)

  add_executable(
    scene_sync_tests engine/tests/SceneSyncTests.cpp engine/src/ecs/World.cpp
                     engine/src/ecs/SceneSync.cpp
                     # SceneSync pushes shader blocks through their field
                     # tables, which live here.
                     engine/src/ecs/ComponentRegistry.cpp
                     engine/src/io/ByteStream.cpp)
  target_include_directories(scene_sync_tests PRIVATE engine/include engine/src
                                                      third_party/imgui)
  target_link_libraries(scene_sync_tests PRIVATE glm::glm EnTT::EnTT)
  add_test(NAME scene_sync_tests COMMAND scene_sync_tests)

  add_executable(ecs_systems_tests engine/tests/EcsSystemsTests.cpp
                                   engine/src/ecs/World.cpp
                                   engine/src/ecs/Systems.cpp
                                   engine/src/ecs/ClipSystem.cpp)
  target_include_directories(ecs_systems_tests PRIVATE engine/include engine/src
                                                       third_party/imgui)
  # eng_ecs_headless, not the bare glm/EnTT pair the other system tests use:
  # clipSystem resolves a component by name through a ComponentRegistry, so the
  # table (and the ByteStream its codecs are written against) has to be linked.
  target_link_libraries(ecs_systems_tests PRIVATE glm::glm EnTT::EnTT
                        eng_ecs_headless)
  add_test(NAME ecs_systems COMMAND ecs_systems_tests)

  eng_add_test(rhi_contract
    SOURCES engine/tests/RhiContractTests.cpp
    LIBS eng_rhi)

  if(ENG_RHI_VULKAN AND ENG_BUILD_VULKAN_SMOKE)
    add_test(NAME rhi_vulkan_smoke
             COMMAND rhi_vulkan_smoke --frames 120 --require-vulkan --validation
                     --exercise-resize)
    set_tests_properties(
      rhi_vulkan_smoke
      PROPERTIES RUN_SERIAL TRUE RESOURCE_LOCK vulkan_gpu TIMEOUT 180)
  endif()

  eng_add_test(byte_stream
    SOURCES game/tests/ByteStreamTests.cpp engine/src/io/ByteStream.cpp
    INCLUDES engine/include
    LIBS glm::glm)

  eng_add_test(component_registry
    SOURCES game/tests/ComponentRegistryTests.cpp
    INCLUDES game/src game/src/scene engine/include third_party
    LIBS game_content eng_ecs_headless)

  eng_add_test(damage_system
    SOURCES game/tests/DamageSystemTests.cpp game/src/combat/DamageSystem.cpp
    INCLUDES game/src engine/include third_party
    LIBS glm::glm EnTT::EnTT)

  eng_add_test(feel_components
    SOURCES game/tests/FeelComponentsTests.cpp
    INCLUDES game/src engine/include third_party
    LIBS glm::glm EnTT::EnTT)

  eng_add_test(stamina_system
    SOURCES game/tests/StaminaSystemTests.cpp game/src/combat/StaminaSystem.cpp
    INCLUDES game/src engine/include third_party
    LIBS glm::glm EnTT::EnTT)

  eng_add_test(poise_system
    SOURCES game/tests/PoiseSystemTests.cpp game/src/combat/PoiseSystem.cpp
    INCLUDES game/src engine/include third_party
    LIBS glm::glm EnTT::EnTT)

  eng_add_test(action_state
    SOURCES game/tests/ActionStateSystemTests.cpp game/src/combat/ActionStateSystem.cpp
            game/src/combat/StaminaSystem.cpp
    INCLUDES game/src engine/include third_party
    LIBS glm::glm EnTT::EnTT)

  eng_add_test(defense_system
    SOURCES game/tests/DefenseSystemTests.cpp game/src/combat/DefenseSystem.cpp
            game/src/combat/PoiseSystem.cpp game/src/combat/StaminaSystem.cpp
    INCLUDES game/src engine/include third_party
    LIBS glm::glm EnTT::EnTT)

  add_executable(
    damage_types_tests game/tests/DamageTypesTests.cpp
                       game/src/combat/DamageSystem.cpp
                       game/src/combat/CombatVocabulary.cpp)
  target_include_directories(damage_types_tests PRIVATE game/src engine/include
                                                        third_party)
  target_link_libraries(damage_types_tests PRIVATE eng_toml eng_core glm::glm
                                                   EnTT::EnTT)
  add_test(NAME damage_types COMMAND damage_types_tests)

  # Enemies. The three pure layers each test without a world: the table and
  # its archetype inheritance, the brain, and the spawner's pacing.
  add_executable(
    enemy_library_tests game/tests/EnemyLibraryTests.cpp
                        game/src/enemy/EnemyLibrary.cpp
                        game/src/audio/ActorSounds.cpp
                        game/src/combat/BulletPattern.cpp
                        game/src/combat/CombatVocabulary.cpp)
  target_include_directories(enemy_library_tests
                             PRIVATE game/src engine/include third_party)
  target_link_libraries(enemy_library_tests PRIVATE eng_toml eng_core glm::glm
                                                    EnTT::EnTT)
  add_test(NAME enemy_library COMMAND enemy_library_tests)

  eng_add_test(actor_sounds
    SOURCES game/tests/ActorSoundsTests.cpp game/src/audio/ActorSounds.cpp
    INCLUDES game/src)

  eng_add_test(enemy_ai
    SOURCES game/tests/EnemyAITests.cpp game/src/enemy/EnemyAI.cpp
    INCLUDES game/src engine/include third_party
    LIBS glm::glm EnTT::EnTT)

  eng_add_test(enemy_save
    SOURCES game/tests/EnemySaveTests.cpp game/src/enemy/EnemySave.cpp engine/src/io/ByteStream.cpp
    INCLUDES game/src engine/include third_party
    LIBS eng_core glm::glm EnTT::EnTT)

  eng_add_test(enemy_spawner
    SOURCES game/tests/EnemySpawnerTests.cpp game/src/enemy/EnemySpawner.cpp
            game/src/audio/ActorSounds.cpp
    INCLUDES game/src engine/include third_party
    LIBS eng_toml eng_core glm::glm)

  eng_add_test(weapon_timing
    SOURCES game/tests/WeaponTimingTests.cpp game/src/combat/WeaponLibrary.cpp
            game/src/combat/CombatVocabulary.cpp
    INCLUDES game/src engine/include third_party
    LIBS eng_toml eng_core glm::glm EnTT::EnTT)

  add_executable(player_weapon_tests game/tests/PlayerWeaponTests.cpp
                                     game/src/PlayerWeapons.cpp)
  target_include_directories(player_weapon_tests
                             PRIVATE game/src engine/include third_party)
  target_link_libraries(player_weapon_tests PRIVATE eng_toml eng_core glm::glm
                                                    EnTT::EnTT)
  add_test(NAME player_weapon COMMAND player_weapon_tests)

  # Sprite viewmodel layer rules. Header-only validation, so this needs no
  # renderer and no GPU -- which is the point of keeping it inline.
  eng_add_test(sprite_viewmodel
    SOURCES game/tests/SpriteViewmodelTests.cpp
    INCLUDES game/src engine/include third_party
    LIBS glm::glm)

  # Viewmodel placement + procedural motion. Renderer-free by construction, so
  # the layer composition (bob/sway/recoil/landing) is testable at all.
  eng_add_test(viewmodel_rig
    SOURCES game/tests/ViewmodelRigTests.cpp game/src/ViewmodelMotion.cpp game/src/PlayerWeapons.cpp
    INCLUDES game/src engine/include third_party
    LIBS eng_toml eng_core glm::glm EnTT::EnTT)

  # Socket maths and the hands definition that names them. Links the full
  # engine only because ViewmodelSocketSet holds renderer nodes; the functions
  # under test here are free functions over a joint matrix, which is what makes
  # "where does a weapon sit in the hand" answerable without a GPU.
  eng_add_test(viewmodel_socket
    SOURCES game/tests/ViewmodelSocketTests.cpp game/src/ViewmodelSocket.cpp
            game/src/HandsDefinition.cpp
    INCLUDES game/src engine/include third_party
    LIBS eng eng_toml glm::glm)

  # Screen shake and hit-stop. Renderer-free, so the decay curve, the tier
  # ordering and the hit-stop lifetime are testable at all.
  # The RPG layer. Each links only the pure modules it exercises -- no renderer,
  # no physics, no registry -- which is the property that makes the library
  # worth having as a separate target.
  eng_add_test(rpg_progression
    SOURCES game/tests/RpgProgressionTests.cpp game/src/rpg/Skills.cpp game/src/rpg/Stats.cpp
            game/src/rpg/RpgTypes.cpp
    INCLUDES game/src engine/include third_party
    LIBS eng_toml eng_core glm::glm)

  eng_add_test(rpg_inventory
    SOURCES game/tests/RpgInventoryTests.cpp game/src/rpg/Inventory.cpp game/src/rpg/Items.cpp
            game/src/rpg/LossPolicy.cpp game/src/rpg/Trading.cpp
            game/src/rpg/RpgTypes.cpp game/src/combat/CombatVocabulary.cpp
    INCLUDES game/src engine/include third_party
    LIBS eng_toml eng_core glm::glm EnTT::EnTT)

  eng_add_test(rpg_quest
    SOURCES game/tests/RpgQuestTests.cpp game/src/rpg/Quests.cpp game/src/rpg/Dialogue.cpp
            game/src/rpg/RpgTypes.cpp
    INCLUDES game/src engine/include third_party
    LIBS eng_toml eng_core glm::glm)

  eng_add_test(rpg_economy
    SOURCES game/tests/RpgEconomyTests.cpp game/src/rpg/Trading.cpp game/src/rpg/RaidState.cpp
            game/src/rpg/RpgSave.cpp game/src/rpg/Items.cpp game/src/rpg/Inventory.cpp
            game/src/rpg/Quests.cpp game/src/rpg/Skills.cpp game/src/rpg/Stats.cpp
            game/src/rpg/WorldState.cpp game/src/rpg/Hideout.cpp game/src/rpg/RpgTypes.cpp
            game/src/combat/CombatVocabulary.cpp
    INCLUDES game/src engine/include third_party
    LIBS eng_toml eng_core glm::glm EnTT::EnTT)

  eng_add_test(hit_feel
    SOURCES game/tests/HitFeelTests.cpp game/src/HitFeel.cpp
    INCLUDES game/src engine/include third_party
    LIBS eng_toml glm::glm)

  # The debug telemetry pipe, minus the socket: the wire format's binary
  # safety, and that a full ring drops instead of blocking the game thread.
  eng_add_test(telemetry
    SOURCES engine/tests/TelemetryTests.cpp
    LIBS eng_telemetry)

  eng_add_test(bullet_pattern
    SOURCES game/tests/BulletPatternTests.cpp game/src/combat/BulletPattern.cpp
    INCLUDES game/src
    LIBS glm::glm)

  eng_add_test(combat_scenario
    SOURCES game/tests/CombatScenarioTests.cpp game/src/combat/DamageSystem.cpp
            game/src/combat/PoiseSystem.cpp game/src/combat/DefenseSystem.cpp
            game/src/combat/ActionStateSystem.cpp game/src/combat/StaminaSystem.cpp
    INCLUDES game/src engine/include third_party
    LIBS glm::glm EnTT::EnTT)

  eng_add_test(status_effect
    SOURCES game/tests/StatusEffectTests.cpp game/src/combat/StatusEffectSystem.cpp
            game/src/combat/DamageSystem.cpp game/src/combat/CombatVocabulary.cpp
    INCLUDES game/src engine/include third_party
    LIBS eng_toml eng_core glm::glm EnTT::EnTT)

  eng_add_test(map_serializer
    SOURCES game/tests/MapSerializerTests.cpp
    INCLUDES game/src game/src/scene engine/include third_party
    LIBS game_content eng_ecs_headless)

  eng_add_test(render_palette
    SOURCES game/tests/RenderPaletteTests.cpp game/src/RenderPalette.cpp
    INCLUDES game/src third_party engine/include
    LIBS eng_toml glm::glm eng)

  eng_add_test(lobby_dressing
    SOURCES game/tests/LobbyDressingTests.cpp game/src/LobbyDressing.cpp
    INCLUDES game/src third_party engine/include
    LIBS eng_toml glm::glm eng)

  add_executable(showcase_visibility_tests
                 game/tests/ShowcaseVisibilityTests.cpp game/src/DungeonGen.cpp)
  target_include_directories(showcase_visibility_tests PRIVATE game/src
                                                               third_party)
  target_link_libraries(showcase_visibility_tests PRIVATE eng_toml eng_core)
  add_test(NAME showcase_visibility COMMAND showcase_visibility_tests)

  # PhysicsSync lives in eng_framework now, so the test links it rather than
  # recompiling the source (whole-archive linking would double-define it).
  eng_add_test(physics_sync
    SOURCES engine/tests/PhysicsSyncTests.cpp
    INCLUDES engine/include third_party
    LIBS eng EnTT::EnTT glm::glm)

  add_executable(
    level_resource_tests
    game/tests/LevelResourceTests.cpp game/src/LevelResource.cpp
    game/src/LevelDocument.cpp game/src/DungeonGen.cpp)
  target_include_directories(level_resource_tests PRIVATE game/src third_party
                                                          engine/include)
  target_link_libraries(level_resource_tests PRIVATE eng_toml glm::glm eng)
  add_test(NAME level_resource COMMAND level_resource_tests)

  # ObjLoader had a test here that was scoped to the OGRE renderer; it imported
  # geometry through Ogre types, and the renderer now imports through Assimp, so
  # the subject no longer exists.

  eng_add_test(loading
    SOURCES engine/tests/LoadingTests.cpp engine/src/core/Loading.cpp
    INCLUDES engine/include
    LIBS glm::glm)

  eng_add_test(step_clock
    SOURCES engine/tests/StepClockTests.cpp engine/src/core/StepClock.cpp
    INCLUDES engine/include)

  eng_add_test(core_object
    SOURCES engine/tests/CoreObjectTests.cpp engine/src/core/Object.cpp
    INCLUDES engine/include)

  eng_add_test(trace
    SOURCES engine/tests/TraceTests.cpp engine/src/diagnostics/Trace.cpp
    INCLUDES engine/include)

  # MemoryProfiler.cpp because Profiler::push/pop drive the heap tagger: one
  # annotation, both budgets. Built with ENG_MEMPROF=0 so this stays a test of
  # the timing tree and does not replace the test binary's operator new.
  eng_add_test(profiler
    SOURCES engine/tests/ProfilerTests.cpp engine/src/diagnostics/Profiler.cpp
            engine/src/diagnostics/MemoryProfiler.cpp engine/src/core/StringId.cpp
            engine/src/core/Log.cpp
    INCLUDES engine/include
    DEFINES ENG_MEMPROF=0)

  eng_add_test(string_id
    SOURCES engine/tests/StringIdTests.cpp engine/src/core/StringId.cpp engine/src/core/Log.cpp
    INCLUDES engine/include)

  eng_add_test(clock
    SOURCES engine/tests/ClockTests.cpp engine/src/core/Clock.cpp
    INCLUDES engine/include)

  eng_add_test(frame_capture
    SOURCES engine/tests/FrameCaptureTests.cpp engine/src/render/FrameCapture.cpp
    INCLUDES engine/include
    LIBS ${CMAKE_DL_LIBS})

  eng_add_test(gif_recorder
    SOURCES engine/tests/GifRecorderTests.cpp engine/src/render/GifRecorder.cpp
            engine/src/core/Log.cpp
    INCLUDES engine/include)

  eng_add_test(actions
    SOURCES engine/tests/ActionsTests.cpp engine/src/systems/Ease.cpp
            engine/src/systems/Actions.cpp engine/src/core/Object.cpp
    INCLUDES engine/include
    LIBS glm::glm)

  eng_add_test(events
    SOURCES engine/tests/EventsTests.cpp engine/src/systems/Events.cpp
    INCLUDES engine/include)

  eng_add_test(filesystem
    SOURCES engine/tests/FileSystemTests.cpp engine/src/io/FileSystem.cpp
    INCLUDES engine/include)

  # Root discovery, the manifest and mount-order resolution are pure logic over
  # the filesystem, so this builds its own throwaway asset tree in the temp dir
  # rather than asserting against the real one.
  eng_add_test(asset_root
    SOURCES engine/tests/AssetRootTests.cpp
    LIBS eng_core)

  eng_add_test(asset_name
    SOURCES engine/tests/AssetNameTests.cpp engine/src/core/AssetName.cpp
    INCLUDES engine/include)

  # Config flattening is toml++ and two maps: no window, no renderer.
  eng_add_test(config
    SOURCES engine/tests/ConfigTests.cpp engine/src/platform/Config.cpp engine/src/core/Log.cpp
    INCLUDES engine/include third_party)

  eng_add_test(directory_watcher
    SOURCES engine/tests/DirectoryWatcherTests.cpp engine/src/io/DirectoryWatcher.cpp
            engine/src/io/FileSystem.cpp
    INCLUDES engine/include)

  eng_add_test(content
    SOURCES engine/tests/ContentTests.cpp engine/src/content/TextResource.cpp
            engine/src/io/FileSystem.cpp engine/src/core/Object.cpp
    INCLUDES engine/include)

  add_executable(particle_asset_tests engine/tests/ParticleAssetTests.cpp)
  target_compile_definitions(particle_asset_tests
                             PRIVATE PROJECT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  add_test(NAME particle_assets COMMAND particle_asset_tests)

  eng_add_test(particle_options
    SOURCES engine/tests/ParticleOptionsTests.cpp
    INCLUDES engine/include
    LIBS glm::glm)

  # The flipbook window is pure arithmetic over ParticleTypes.h, and the rest of
  # this test reads the three files that have to agree about it as text.
  eng_add_test(particle_flipbook
    SOURCES engine/tests/ParticleFlipbookTests.cpp
    INCLUDES engine/include
    DEFINES PROJECT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

  # The simulation is deliberately renderer-free, so it links only glm and its
  # own two translation units -- no renderer, no physics, no window.
  eng_add_test(particle_sim
    SOURCES engine/tests/ParticleSimTests.cpp engine/src/particles/ParticleSim.cpp
            engine/src/particles/ParticleEmitters.cpp
    INCLUDES engine/include engine/src
    LIBS glm::glm)

  # The walk preview is pure camera maths, so eye height, pitch clamping and
  # the enter/leave round trip are all assertable without a window.
  eng_add_test(editor_camera_eye
    SOURCES editor/tests/EditorCameraEyeTests.cpp editor/src/viewport/EditorCamera.cpp
    INCLUDES editor/include engine/include
    LIBS eng)

  # Whether the player can actually walk from the spawn to the exit. Every other
  # rule in SceneValidate checks that the data is well formed; this one checks
  # that the level is playable, which is the failure that survives a clean cook.
  add_executable(scene_reachability_tests
                 editor/tests/SceneReachabilityTests.cpp)
  target_link_libraries(scene_reachability_tests PRIVATE game_content
                                                         eng_ecs_headless)
  add_test(NAME scene_reachability COMMAND scene_reachability_tests)

  # Portal depth ordering is pure arithmetic, so it is checked here rather than
  # by eye: every failure of this prop so far has been a surface at the wrong Z.
  eng_add_test(portal_geometry
    SOURCES game/tests/PortalGeometryTests.cpp
    INCLUDES game/src
    LIBS eng)

  # DecalSystem runs headlessly: the rebuild path returns early with no device,
  # so ageing, merging and eviction are all testable. The .cpp still pulls in
  # renderer headers, hence the link against eng.
  eng_add_test(decal_system
    SOURCES engine/tests/DecalSystemTests.cpp
    INCLUDES engine/src third_party
    LIBS eng)

  # Parsing blood.toml needs no renderer; the translation unit still references
  # Renderer symbols for the register/spawn half, so it links eng.
  add_executable(blood_system_tests game/tests/BloodSystemTests.cpp
                                    game/src/BloodSystem.cpp)
  target_include_directories(blood_system_tests PRIVATE game/src third_party)
  target_link_libraries(blood_system_tests PRIVATE eng)
  # No PROJECT_SOURCE_DIR: blood.toml is shipped content, so this asks the
  # resolver for it. The engine tests that keep the define read shader and
  # material *source text* -- assertions about the repository, not lookups.
  add_test(NAME blood_system COMMAND blood_system_tests)

  eng_add_test(enchantment
    SOURCES engine/tests/EnchantmentTests.cpp
    INCLUDES engine/include
    LIBS glm::glm
    DEFINES PROJECT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

  add_executable(vfx_shader_asset_tests engine/tests/VfxShaderAssetTests.cpp)
  target_compile_definitions(vfx_shader_asset_tests
                             PRIVATE PROJECT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  add_test(NAME vfx_shader_assets COMMAND vfx_shader_asset_tests)

  eng_add_test(model
    SOURCES engine/tests/ModelTests.cpp
    INCLUDES engine/include engine/src
    LIBS eng glm::glm)

  eng_add_test(primitive
    SOURCES engine/tests/PrimitiveTests.cpp
    INCLUDES engine/include engine/src
    LIBS eng glm::glm
    DEFINES PROJECT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

  eng_add_test(model_import
    SOURCES engine/tests/ModelImportTests.cpp
    INCLUDES engine/include
    LIBS eng glm::glm)

  add_executable(assimp_model_import_tests
                 engine/tests/AssimpModelImportTests.cpp)
  target_link_libraries(assimp_model_import_tests PRIVATE eng_model_import)
  target_compile_definitions(
    assimp_model_import_tests PRIVATE PROJECT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  eng_target_hardening(assimp_model_import_tests)
  add_test(NAME assimp_model_import COMMAND assimp_model_import_tests)

  add_test(NAME model_validate_rejects_infinite_scale
           COMMAND model_validate --scale inf
                   ${CMAKE_SOURCE_DIR}/assets/meshes/box.obj)
  set_tests_properties(model_validate_rejects_infinite_scale
                       PROPERTIES WILL_FAIL TRUE)

  eng_add_test(audio
    SOURCES engine/tests/AudioTests.cpp engine/src/audio/Audio.cpp
            engine/src/audio/SoundInstance.cpp engine/src/audio/SoundResource.cpp
            engine/src/io/FileSystem.cpp engine/src/core/Object.cpp engine/src/core/Log.cpp
    INCLUDES engine/include engine/src/audio
    LIBS miniaudio)

  eng_add_test(audio_scene_sync
    SOURCES engine/tests/AudioSceneSyncTests.cpp
    LIBS eng)

  eng_add_test(game_audio
    SOURCES game/tests/GameAudioTests.cpp game/src/audio/GameAudio.cpp
    INCLUDES game/src engine/include
    LIBS eng eng_toml
    DEFINES PROJECT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

  eng_add_test(map_runtime
    SOURCES game/tests/MapRuntimeTests.cpp game/src/scene/MapRuntime.cpp
    INCLUDES game/src game/src/scene engine/include engine/src third_party third_party/imgui
    LIBS eng eng_runtime game_content EnTT::EnTT glm::glm)

  # --- authoring pipeline (.scn -> IR -> .map) --------------------------------
  # These run against the REAL assets/game/kit.toml and the real shipped scene:
  # the point is to catch content drift, not to test a fixture.
  add_executable(kit_catalog_tests editor/tests/KitCatalogTests.cpp)
  target_link_libraries(kit_catalog_tests PRIVATE game_content eng_ecs_headless)
  add_test(NAME kit_catalog COMMAND kit_catalog_tests)

  eng_add_test(editor_command
    SOURCES editor/tests/EditorCommandTests.cpp editor/src/commands/Commands.cpp
            editor/src/content/SceneDocument.cpp
    INCLUDES editor/include game/src engine/include
    LIBS glm::glm)

  # The .scn geometry blocks: mesh files and generated primitives. Its own
  # binary rather than more of SceneRoundTripTests, because it asserts on
  # documents built in the test and must not fail when a shipped scene is
  # edited.
  eng_add_test(scene_geometry
    SOURCES editor/tests/SceneGeometryTests.cpp
    INCLUDES editor/include game/src engine/include
    LIBS game_content eng_ecs_headless)

  eng_add_test(editor_component
    SOURCES editor/tests/EditorComponentTests.cpp editor/src/scene/EntityComponents.cpp
            editor/src/content/SceneDocument.cpp
    INCLUDES editor/include game/src engine/include
    LIBS glm::glm)

  # Layers are pure document logic -- membership, session visibility, and the
  # per-layer extract/merge -- so all of it is assertable without a window.
  eng_add_test(editor_layer
    SOURCES editor/tests/EditorLayerTests.cpp editor/src/scene/Layers.cpp
            editor/src/content/SceneDocument.cpp
    INCLUDES editor/include game/src engine/include
    LIBS glm::glm)

  # Rubber-band selection and click cycling. Screen-space maths against a
  # view-projection, so both projections are exercised without a window --
  # and cycling in particular is state whose bugs only show up on the second
  # and third click, which is not something anyone tests by hand.
  eng_add_test(editor_selection
    SOURCES editor/tests/EditorSelectionTests.cpp editor/src/scene/SelectionTools.cpp
            editor/src/scene/Picker.cpp editor/src/content/SceneDocument.cpp
    INCLUDES editor/include game/src engine/include
    LIBS glm::glm)

  # Camera bookmarks, the back/forward history and the speed steps.
  eng_add_test(editor_navigation
    SOURCES editor/tests/EditorNavigationTests.cpp editor/src/viewport/CameraNavigation.cpp
            editor/src/viewport/EditorCamera.cpp
    INCLUDES editor/include engine/include
    LIBS eng)

  # Align, distribute and drop-to-surface. Pure arithmetic on world bounds, and
  # the kind of operation whose failure mode is "everything is now in one
  # place" -- which is too late to notice in a screenshot.
  eng_add_test(editor_align
    SOURCES editor/tests/EditorAlignTests.cpp editor/src/scene/AlignTools.cpp
    INCLUDES editor/include game/src engine/include
    LIBS glm::glm)

  # Multi-object editing: which fields fan out across a selection, and which
  # deliberately do not. Pure document logic, and the place a regression would
  # be silent -- a field that stopped fanning out looks exactly like an author
  # having selected one entity.
  eng_add_test(editor_multiedit
    SOURCES editor/tests/EditorMultiEditTests.cpp editor/src/scene/MultiEdit.cpp
            editor/src/content/SceneDocument.cpp
    INCLUDES editor/include game/src engine/include
    LIBS glm::glm)

  eng_add_test(editor_outliner
    SOURCES editor/tests/EditorOutlinerTests.cpp editor/src/scene/OutlinerTree.cpp
            editor/src/scene/EntityComponents.cpp
    INCLUDES editor/include game/src engine/include
    LIBS game_content eng_ecs_headless)

  eng_add_test(editor_outliner_panel
    SOURCES editor/tests/EditorOutlinerPanelTests.cpp editor/src/ui/OutlinerPanel.cpp
            editor/src/ui/EditorIcons.cpp
    INCLUDES editor/include game/src engine/include
    LIBS eng_imgui glm::glm)

  add_executable(editor_material_catalog_tests
                 editor/tests/EditorMaterialCatalogTests.cpp
                 editor/src/assets/MaterialCatalog.cpp)
  target_include_directories(editor_material_catalog_tests PRIVATE editor/include
                             engine/include third_party)
  add_test(NAME editor_material_catalog COMMAND editor_material_catalog_tests
           ${CMAKE_SOURCE_DIR}/assets/materials)

  # The inspector's layout arithmetic, on a headless ImGui context: no window,
  # no backend. Widget sizing is the part that was measured by hand and drifted,
  # and it is checkable without drawing anything.
  # Authoring UI in the 2D viewport: handle hit-testing and the drag maths that
  # turns a grabbed pixel back into the offsets the document stores. No window
  # -- every assertion is arithmetic over a document.
  eng_add_test(editor_ui_scene
    SOURCES editor/tests/UiSceneEditorTests.cpp editor/src/ui/UiSceneEditor.cpp
            editor/src/content/SceneInstancing.cpp
            editor/src/content/SceneDocument.cpp
            editor/src/content/SceneSource.cpp
    INCLUDES editor/include engine/include game/src third_party
    LIBS eng_framework eng game_content glm::glm EnTT::EnTT
         nlohmann_json::nlohmann_json)

  eng_add_test(editor_inspector_layout
    SOURCES editor/tests/EditorInspectorLayoutTests.cpp editor/src/ui/EditorUi.cpp
    INCLUDES editor/include engine/include
    LIBS eng_imgui eng)

  add_executable(editor_vocabulary_tests editor/tests/EditorVocabularyTests.cpp
                                         editor/src/assets/GameVocabulary.cpp)
  target_include_directories(editor_vocabulary_tests PRIVATE editor/include
                             engine/include)
  add_test(NAME editor_vocabulary COMMAND editor_vocabulary_tests
           ${CMAKE_SOURCE_DIR}/assets/config/enemies.toml)

  # Picker.cpp comes along for projectToViewport: the sandbox grid projects
  # world lines to the viewport, which is the same maths picking already owns
  # rather than a second copy of it.
  eng_add_test(editor_viewport_overlay
    SOURCES editor/tests/EditorViewportOverlayTests.cpp editor/src/viewport/ViewportOverlay.cpp
            editor/src/scene/Picker.cpp
    INCLUDES editor/include engine/include
    LIBS eng_imgui)

  eng_add_test(editor_workspace
    SOURCES editor/tests/EditorWorkspaceTests.cpp editor/src/ui/EditorWorkspace.cpp
    INCLUDES editor/include engine/include third_party
    LIBS eng_imgui)

  # The shell's arithmetic: the top bar's three zones and the bottom panel's
  # height. Pure, so it needs neither an imgui context nor a window.
  eng_add_test(editor_shell
    SOURCES editor/tests/EditorShellTests.cpp editor/src/ui/EditorShell.cpp
            editor/src/content/SceneContract.cpp
            editor/src/content/SceneDocument.cpp
    INCLUDES editor/include game/src engine/include
    LIBS glm::glm)

  eng_add_test(editor_scene_tabs
    SOURCES editor/tests/SceneTabsTests.cpp editor/src/app/SceneTabs.cpp
            editor/src/commands/Commands.cpp
            editor/src/content/SceneDocument.cpp
            editor/src/viewport/EditorCamera.cpp
            editor/src/scene/Layers.cpp
    INCLUDES editor/include game/src engine/include
    LIBS glm::glm)

  eng_add_test(editor_scene_browser
    SOURCES editor/tests/EditorSceneBrowserTests.cpp editor/src/project/SceneBrowser.cpp
    INCLUDES editor/include engine/include)

  eng_add_test(editor_settings
    SOURCES editor/tests/EditorSettingsTests.cpp editor/src/project/EditorSettings.cpp
            editor/src/project/RunGame.cpp
    INCLUDES editor/include engine/include)

  eng_add_test(editor_entity_gizmo
    SOURCES editor/tests/EditorEntityGizmoTests.cpp editor/src/viewport/EntityGizmos.cpp
            editor/src/scene/Picker.cpp
    INCLUDES editor/include game/src engine/include
    LIBS game_content eng_imgui glm::glm)

  eng_add_test(editor_document_raycast
    SOURCES editor/tests/EditorDocumentRaycastTests.cpp editor/src/scene/DocumentRaycast.cpp
            editor/src/scene/Picker.cpp
    INCLUDES editor/include engine/include
    LIBS game_content glm::glm)

  eng_add_test(editor_placement
    SOURCES editor/tests/EditorPlacementTests.cpp editor/src/scene/BrushPlacement.cpp
            editor/src/scene/DocumentRaycast.cpp editor/src/scene/Picker.cpp
    INCLUDES editor/include game/src game/tests engine/include
    LIBS game_content eng_ecs_headless glm::glm)

  eng_add_test(editor_attachment
    SOURCES editor/tests/EditorAttachmentTests.cpp editor/src/scene/Attachments.cpp
    INCLUDES editor/include game/src game/tests engine/include
    LIBS game_content eng_ecs_headless glm::glm)

  eng_add_test(editor_model_import
    SOURCES editor/tests/EditorModelImportTests.cpp editor/src/assets/ModelImportPipeline.cpp
            editor/src/assets/FileBrowser.cpp
    INCLUDES editor/include engine/include engine/src
    LIBS eng_model_import eng_core glm::glm
    DEFINES PROJECT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

  eng_add_test(editor_file_browser
    SOURCES editor/tests/EditorFileBrowserTests.cpp editor/src/assets/FileBrowser.cpp
    INCLUDES editor/include)

  eng_add_test(editor_clipboard
    SOURCES editor/tests/EditorClipboardTests.cpp editor/src/commands/Clipboard.cpp
    INCLUDES editor/include game/src engine/include
    LIBS game_content glm::glm)

  eng_add_test(editor_command_palette
    SOURCES editor/tests/EditorCommandPaletteTests.cpp editor/src/commands/CommandPalette.cpp
    INCLUDES editor/include engine/include
    LIBS eng_imgui)

  eng_add_test(editor_viewport_grid
    SOURCES editor/tests/EditorViewportGridTests.cpp editor/src/viewport/ViewportGrid.cpp
    INCLUDES editor/include engine/include)

  eng_add_test(editor_paint_slot
    SOURCES editor/tests/EditorPaintSlotTests.cpp editor/src/scene/PaintSlot.cpp
    INCLUDES editor/include game/src engine/include
    LIBS glm::glm)

  eng_add_test(editor_ui_stage
    SOURCES editor/tests/EditorUiStageTests.cpp editor/src/ui/UiStage.cpp game/src/HudModel.cpp
    INCLUDES editor/include game/src third_party engine/include
    LIBS eng game_content eng_imgui glm::glm)

  eng_add_test(editor_pick_target
    SOURCES editor/tests/EditorPickTargetTests.cpp editor/src/scene/PickTarget.cpp
    INCLUDES editor/include engine/include
    LIBS game_content glm::glm)

  eng_add_test(editor_picker
    SOURCES editor/tests/EditorPickerTests.cpp editor/src/scene/Picker.cpp
    INCLUDES editor/include engine/include
    LIBS glm::glm)

  eng_add_test(editor_camera
    SOURCES editor/tests/EditorCameraTests.cpp editor/src/viewport/EditorCamera.cpp
    INCLUDES editor/include engine/include
    LIBS glm::glm)

  eng_add_test(editor_camera_fly
    SOURCES editor/tests/EditorCameraFlyTests.cpp editor/src/viewport/EditorCamera.cpp
    INCLUDES editor/include engine/include
    LIBS glm::glm)

  add_executable(grid_math_tests editor/tests/GridMathTests.cpp)
  target_link_libraries(grid_math_tests PRIVATE game_content eng_ecs_headless)
  add_test(NAME grid_math COMMAND grid_math_tests)

  add_executable(scene_template_tests editor/tests/SceneTemplateTests.cpp)
  target_link_libraries(scene_template_tests PRIVATE game_content
                                                     eng_ecs_headless)
  add_test(NAME scene_template COMMAND scene_template_tests)

  add_executable(room_builder_tests editor/tests/RoomBuilderTests.cpp)
  target_link_libraries(room_builder_tests PRIVATE game_content eng_ecs_headless)
  add_test(NAME room_builder COMMAND room_builder_tests)

    # editor/include is PRIVATE on game_content, so a consumer that includes an
  # editor header names it too -- same as scene_environment_tests.
  eng_add_test(scene_contract
    SOURCES editor/tests/SceneContractTests.cpp
    INCLUDES editor/include game/src engine/include
    LIBS game_content eng_ecs_headless)

  add_executable(scene_validate_tests editor/tests/SceneValidateTests.cpp)
  target_link_libraries(scene_validate_tests PRIVATE game_content
                                                     eng_ecs_headless)
  add_test(NAME scene_validate COMMAND scene_validate_tests)

  add_executable(scene_environment_tests editor/tests/SceneEnvironmentTests.cpp)
  target_include_directories(scene_environment_tests PRIVATE editor/include
                                                        game/src engine/include)
  target_link_libraries(scene_environment_tests PRIVATE game_content
                                                        eng_ecs_headless)
  add_test(NAME scene_environment COMMAND scene_environment_tests)

  add_executable(scene_hierarchy_tests editor/tests/SceneHierarchyTests.cpp)
  target_link_libraries(scene_hierarchy_tests PRIVATE game_content
                                                      eng_ecs_headless)
  add_test(NAME scene_hierarchy COMMAND scene_hierarchy_tests)

  # Unpacking a compound kit piece must change what is editable and nothing
  # about what is drawn. Reads the shipped kit, so a piece losing its
  # attachments fails the build rather than making the test pass vacuously.
  # The mechanical repairs: cell records never move an entity, and only an
  # exact copy is ever deleted.
  # Exporting a project: the build's layout, and what the exporter refuses.
  # eng_runtime (not eng_ecs_headless) because it reads a Project -- linking
  # both would be a duplicate-symbol error, see Content.cmake.
  eng_add_test(project_export
    SOURCES editor/tests/ProjectExportTests.cpp
            editor/src/project/ProjectExport.cpp
    INCLUDES editor/include engine/include third_party
    LIBS game_content eng_runtime)

  # Making a script, and reading errors back out of a playtest log. The log
  # format is the contract between the runtime and the editor -- two processes,
  # no shared type -- so this test is what keeps them agreeing.
  eng_add_test(script_workshop
    SOURCES editor/tests/ScriptWorkshopTests.cpp
            editor/src/project/ScriptWorkshop.cpp
    INCLUDES editor/include engine/include
    LIBS eng_core)

  # Scene instancing: one .scn placed inside another.
  eng_add_test(scene_instancing
    SOURCES editor/tests/SceneInstancingTests.cpp
    INCLUDES editor/include game/src engine/include third_party
    LIBS game_content eng_ecs_headless)

  eng_add_test(scene_repair
    SOURCES editor/tests/SceneRepairTests.cpp
    INCLUDES game/tests editor/include
    LIBS game_content eng_ecs_headless)

  eng_add_test(scene_unpack
    SOURCES editor/tests/SceneUnpackTests.cpp
    INCLUDES game/tests editor/include
    LIBS game_content eng_ecs_headless)

  add_executable(scene_roundtrip_tests editor/tests/SceneRoundTripTests.cpp)
  target_link_libraries(scene_roundtrip_tests PRIVATE game_content
                                                      eng_ecs_headless)
  add_test(NAME scene_roundtrip COMMAND scene_roundtrip_tests)

  add_executable(schema_sync_tests editor/tests/SchemaSyncTests.cpp)
  target_link_libraries(schema_sync_tests PRIVATE game_content eng_ecs_headless
                                                  nlohmann_json::nlohmann_json)
  add_test(NAME schema_sync COMMAND schema_sync_tests)

  # Runs the scene_cook binary and compares it against the in-process cook, so
  # "the editor and CI cook identical bytes" is a build failure when it breaks.
  add_executable(cook_parity_tests editor/tests/CookParityTests.cpp)
  add_dependencies(cook_parity_tests scene_cook)
  target_link_libraries(cook_parity_tests PRIVATE game_content eng_ecs_headless)
  target_compile_definitions(
    cook_parity_tests
    PRIVATE
      SCENE_COOK_EXE="$<TARGET_FILE:scene_cook>"
      COOK_PARITY_OUT_DIR="${CMAKE_CURRENT_BINARY_DIR}")
  add_test(NAME cook_parity COMMAND cook_parity_tests)

  eng_add_test(layout_to_scene
    SOURCES game/tests/LayoutToSceneTests.cpp game/src/DungeonGen.cpp
    INCLUDES game/src/scene game/src engine/include third_party
    LIBS game_content eng_ecs_headless)

  # --- the project runtime ----------------------------------------------
  # Project is pure data -- TOML in, a struct out -- so it tests against
  # eng_core alone, with no renderer and no window to stand up.
  eng_add_test(project
    SOURCES engine/tests/ProjectTests.cpp engine/src/runtime/Project.cpp
    INCLUDES engine/include third_party
    LIBS eng_core eng_toml)

  # Components a project declares in TOML. Headless: a declared component is
  # registry work plus the .map codec, and neither needs a window.
  eng_add_test(project_components
    SOURCES engine/tests/ProjectComponentTests.cpp
            engine/src/runtime/ProjectComponents.cpp
    INCLUDES engine/include third_party
    LIBS eng_framework eng_toml glm::glm EnTT::EnTT)

  # Screen-space UI layout. Pure registry maths -- anchors, nesting and picking
  # -- so it needs no canvas and no window; the painting is exercised on screen.
  eng_add_test(ui_scene
    SOURCES engine/tests/UiSceneTests.cpp
    INCLUDES engine/include third_party
    LIBS eng_framework glm::glm EnTT::EnTT)

  # SceneRuntime needs a World, so it links eng_framework -- still headless:
  # every assertion here is registry work, and none of it opens a window.
  eng_add_test(scene_runtime
    SOURCES engine/tests/SceneRuntimeTests.cpp engine/src/runtime/SceneRuntime.cpp
    INCLUDES engine/include third_party
    LIBS eng_framework glm::glm EnTT::EnTT)

  # The tests that assert against shipped content share game/tests/TestAssets.h,
  # which mounts the game pack and resolves logical paths. It replaced a
  # per-target macro family (APP_ASSET_DIR, KIT_TOML, ASSET_ROOT, RITUAL_SCN,
  # SCENE_SCHEMA) that baked absolute source paths into fifteen test binaries.
  target_include_directories(damage_types_tests PRIVATE game/tests)
  target_include_directories(enemy_library_tests PRIVATE game/tests)
  target_include_directories(player_weapon_tests PRIVATE game/tests)
  target_include_directories(showcase_visibility_tests PRIVATE game/tests)
  target_include_directories(level_resource_tests PRIVATE game/tests)
  target_include_directories(blood_system_tests PRIVATE game/tests)
  target_include_directories(scene_reachability_tests PRIVATE editor/include
                                                              game/tests)
  target_include_directories(kit_catalog_tests PRIVATE editor/include game/tests)
  target_include_directories(grid_math_tests PRIVATE editor/include game/tests)
  target_include_directories(scene_template_tests PRIVATE editor/include
                                                        game/tests)
  target_include_directories(room_builder_tests PRIVATE editor/include game/tests)
  target_include_directories(scene_validate_tests PRIVATE editor/include
                                                        game/tests)
  target_include_directories(scene_environment_tests PRIVATE editor/include)
  target_include_directories(scene_hierarchy_tests PRIVATE editor/include)
  target_include_directories(scene_roundtrip_tests PRIVATE editor/include
                                                       game/tests engine/include)
  target_include_directories(schema_sync_tests PRIVATE editor/include game/tests)
  target_include_directories(cook_parity_tests PRIVATE editor/include game/tests
                                                      engine/include)
endif()
