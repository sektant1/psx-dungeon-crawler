# 3.24 for $<LINK_LIBRARY:WHOLE_ARCHIVE,...>, used to link the engine layers.
cmake_minimum_required(VERSION 3.24)
project(dungeon_crawler CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE
      Release
      CACHE STRING "Build type" FORCE)
endif()

# --- Build speed: compiler cache -------------------------------------------
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
  set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
  message(STATUS "ccache: ${CCACHE_PROGRAM}")
endif()

# --- Build speed: LTO/IPO for Release (our targets only; Jolt opts out) -----
include(CheckIPOSupported)
check_ipo_supported(RESULT ENG_IPO_SUPPORTED OUTPUT _ipo_err)

# --- Sanitizers: -DENABLE_ASAN=ON for an ASan+UBSan+LeakSan build -----------
option(ENABLE_ASAN "Build our targets with Address/UB/Leak sanitizers" OFF)

# Shared compile/link options applied to first-party targets ONLY (never Jolt or
# OGRE): warnings, optional IPO, optional sanitizers. Call for each target.
function(eng_target_hardening tgt)
  target_compile_options(${tgt} PRIVATE -Wall -Wextra -Wno-unused-parameter)
  if(ENG_IPO_SUPPORTED)
    set_property(TARGET ${tgt} PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
  endif()
  if(ENABLE_ASAN)
    # -fno-sanitize=vptr/function: UBSan's vtable checks need RTTI, but Jolt is
    # built -fno-rtti, so leaving them on makes Physics.cpp reference Jolt
    # typeinfo that doesn't exist (link failure). Everything else in ASan+UBSan+
    # Leak stays on.
    target_compile_options(
      ${tgt} PRIVATE -fsanitize=address,undefined -fno-sanitize=vptr
                     -fno-omit-frame-pointer -g)
    target_link_options(${tgt} PRIVATE -fsanitize=address,undefined
                        -fno-sanitize=vptr)
  endif()
endfunction()

# --- Third-party dependencies ------------------------------------------------
# Every dependency (OGRE, SDL2, glm, Jolt, toml++) is fetched + built from
# source by CPM; see cmake/Dependencies.cmake. No system packages, no vendored
# SDKs. Cache downloads across build trees so a clean build tree is cheap.
set(CPM_SOURCE_CACHE
    "${CMAKE_CURRENT_SOURCE_DIR}/.cache/cpm"
    CACHE PATH "Directory CPM caches fetched dependency sources in")
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/Dependencies.cmake)

# --- Engine layers -----------------------------------------------------------
# The engine is built as one static lib per architectural layer, and each layer
# links only downward:
#
#   eng            facade + app lifetime (Engine)
#   eng_framework  ECS scene, scene sync, controllers
#   eng_systems    renderer, physics, audio, particles  (owns Ogre/Jolt/SDL)
#   eng_platform   window, input, config
#   eng_core       log, io, math/geometry, events, profiling
#
# The split exists to make a layering violation a *link* error rather than a
# review comment: eng_core cannot call the renderer because it does not link it.
# Header-level violations are caught separately by tools/check_layering.py,
# which is what enforces the rule while all public headers still share one
# include root (engine/include). `eng` stays the single target consumers link.

find_package(OpenGL REQUIRED)

# Vendored imgui (docking branch) + its SDL2/OpenGL3 backends and ImGuizmo.
# Built once here so no engine layer compiles third-party sources; the docking
# copy must win over OgreOverlay's bundled imgui, hence the BEFORE include.
add_library(
  eng_imgui STATIC
  third_party/imgui/imgui.cpp
  third_party/imgui/imgui_draw.cpp
  third_party/imgui/imgui_tables.cpp
  third_party/imgui/imgui_widgets.cpp
  third_party/imgui/backends/imgui_impl_sdl2.cpp
  third_party/imgui/backends/imgui_impl_opengl3.cpp
  ${imguizmo_SOURCE_DIR}/src/ImGuizmo.cpp)
target_include_directories(
  eng_imgui BEFORE
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui
         ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui/backends)
target_link_libraries(eng_imgui PUBLIC eng_imguizmo PRIVATE SDL2::SDL2
                                                           OpenGL::GL)

add_library(
  eng_core STATIC
  engine/src/Log.cpp
  engine/src/core/Object.cpp
  engine/src/io/FileSystem.cpp
  engine/src/io/DirectoryWatcher.cpp
  engine/src/content/TextResource.cpp
  engine/src/diagnostics/Trace.cpp
  engine/src/diagnostics/Profiler.cpp
  engine/src/systems/Ease.cpp
  engine/src/systems/Actions.cpp
  engine/src/systems/Events.cpp
  engine/src/StepClock.cpp
  engine/src/render/PrimitiveGeometry.cpp)
# One public include root for the whole engine, exported from the bottom layer
# so every layer and every consumer inherits it.
target_include_directories(eng_core PUBLIC engine/include PRIVATE third_party)
target_link_libraries(eng_core PUBLIC glm::glm)

add_library(eng_platform STATIC engine/src/Platform.cpp engine/src/Input.cpp
                                engine/src/Config.cpp)
target_include_directories(eng_platform PRIVATE third_party engine/src)
target_link_libraries(eng_platform PUBLIC "$<LINK_LIBRARY:WHOLE_ARCHIVE,eng_core>"
                      PRIVATE SDL2::SDL2)

add_library(
  eng_systems STATIC
  engine/src/RenderCore.cpp
  engine/src/Renderer.cpp
  engine/src/RenderPresets.cpp
  engine/src/SceneRegistry.cpp
  engine/src/ObjLoader.cpp
  engine/src/ProceduralMeshes.cpp
  engine/src/render/FrameCapture.cpp
  engine/src/render/Model.cpp
  engine/src/render/Primitive.cpp
  engine/src/render/PrototypeAssets.cpp
  engine/src/particles/Particles.cpp
  engine/src/particles/ParticlePresets.cpp
  engine/src/particles/ParticleLibrary.cpp
  engine/src/Physics.cpp
  engine/src/audio/SoundResource.cpp
  engine/src/audio/SoundInstance.cpp
  engine/src/audio/Audio.cpp)
add_dependencies(eng_systems RenderSystem_GL3Plus Plugin_ParticleFX Codec_STBI)
target_include_directories(eng_systems PRIVATE third_party engine/src
                                               engine/src/audio)
target_link_libraries(
  eng_systems
  PUBLIC "$<LINK_LIBRARY:WHOLE_ARCHIVE,eng_platform>" OgreOverlay eng_imgui
  PRIVATE OgreMain SDL2::SDL2 Jolt miniaudio OpenGL::GL ${CMAKE_DL_LIBS})
target_compile_definitions(
  eng_systems
  PRIVATE ENG_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/engine/assets"
          OGRE_PLUGIN_DIR="${ENG_OGRE_PLUGIN_DIR}"
          OGRE_MEDIA_DIR="${ENG_OGRE_MEDIA_DIR}")

add_library(
  eng_framework STATIC engine/src/ecs/Scene.cpp engine/src/ecs/SceneSync.cpp
                       engine/src/ecs/RendererSceneBackend.cpp
                       engine/src/controllers/FpsController.cpp)
target_include_directories(eng_framework PRIVATE third_party engine/src)
target_link_libraries(eng_framework PUBLIC "$<LINK_LIBRARY:WHOLE_ARCHIVE,eng_systems>"
                                           EnTT::EnTT)

# Facade: owns application lifetime and is the only target consumers name.
add_library(eng STATIC engine/src/Engine.cpp)
target_include_directories(eng PRIVATE third_party engine/src)
target_link_libraries(eng PUBLIC "$<LINK_LIBRARY:WHOLE_ARCHIVE,eng_framework>")
target_compile_definitions(
  eng PRIVATE ENG_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/engine/assets")

foreach(_layer eng_imgui eng_core eng_platform eng_systems eng_framework eng)
  eng_target_hardening(${_layer})
endforeach()
# imgui is third-party: build it without our warning set.
target_compile_options(eng_imgui PRIVATE -w)

# Shared PSX demo scene, data-driven from samples/common/assets.
add_library(demo_scene STATIC samples/common/DemoScene.cpp)
target_include_directories(
  demo_scene
  PUBLIC samples/common
  PRIVATE third_party)
target_link_libraries(demo_scene PUBLIC eng)
target_compile_definitions(
  demo_scene
  PUBLIC
    DEMO_SCENE_TOML="${CMAKE_CURRENT_SOURCE_DIR}/samples/common/assets/demo_scene.toml"
)
eng_target_hardening(demo_scene)

add_executable(psx_demo samples/psx-demo/src/main.cpp)
target_link_libraries(psx_demo PRIVATE demo_scene)
target_include_directories(psx_demo PRIVATE engine/src)
target_compile_definitions(
  psx_demo
  PRIVATE APP_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/samples/psx-demo/assets")
eng_target_hardening(psx_demo)

add_executable(
  game
  game/src/main.cpp
  game/src/DungeonMap.cpp
  game/src/DungeonGen.cpp
  game/src/GameScene.cpp
  game/src/PropSystem.cpp
  game/src/CombatSystem.cpp
  game/src/DebugOverlay.cpp
  game/src/combat/DamageSystem.cpp
  game/src/combat/StatusEffectSystem.cpp
  game/src/combat/WeaponLibrary.cpp
  game/src/combat/CombatDirector.cpp
  game/src/combat/StaminaSystem.cpp
  game/src/combat/PoiseSystem.cpp
  game/src/combat/ActionStateSystem.cpp
  game/src/combat/DefenseSystem.cpp
  game/src/PlayerSystem.cpp
  game/src/InteractionSystem.cpp
  game/src/LiveLevel.cpp
  game/src/LevelDocument.cpp
  game/src/LevelResource.cpp
  game/src/Melee.cpp
  game/src/Projectiles.cpp
  game/src/Spells.cpp
  game/src/CombatConfig.cpp
  game/src/LobbyDressing.cpp
  game/src/ParticleEffects.cpp
  game/src/SceneFactory.cpp
  game/src/PortalGeometry.cpp
  game/src/RenderPalette.cpp
  game/src/Targeting.cpp
  game/src/Dummy.cpp
  game/src/ViewModel.cpp
  game/src/MapPlay.cpp
  game/src/scene/MapRuntime.cpp
  game/src/scene/PhysicsSync.cpp
  game/src/scene/ByteStream.cpp
  game/src/scene/ComponentRegistry.cpp
  game/src/scene/MapSerializer.cpp)
target_include_directories(game PRIVATE third_party samples/common engine/src
                                        game/src game/src/scene)
target_link_libraries(game PRIVATE demo_scene)
target_compile_definitions(
  game
  PRIVATE
    APP_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/game/assets"
    DEMO_SCENE_TOML="${CMAKE_CURRENT_SOURCE_DIR}/samples/common/assets/demo_scene.toml"
)
eng_target_hardening(game)

add_executable(
  mapgen
  game/src/mapgen_main.cpp game/src/scene/LayoutToScene.cpp
  game/src/scene/MapSerializer.cpp game/src/scene/ComponentRegistry.cpp
  game/src/scene/ByteStream.cpp game/src/DungeonGen.cpp)
target_include_directories(mapgen PRIVATE game/src game/src/scene
                                          engine/include third_party)
target_link_libraries(mapgen PRIVATE glm::glm EnTT::EnTT)
target_compile_definitions(
  mapgen PRIVATE APP_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/game/assets")
eng_target_hardening(mapgen)

# Headless action-simulation harness: drives the renderer-free combat + physics
# systems from a text action script (game/sim/), no window. Links eng for
# Physics; combat sources compiled in directly.
add_executable(
  game_sim
  game/sim/sim_main.cpp game/sim/SimWorld.cpp game/src/combat/DamageSystem.cpp
  game/src/combat/StatusEffectSystem.cpp game/src/combat/WeaponLibrary.cpp
  game/src/combat/CombatDirector.cpp)
target_include_directories(game_sim PRIVATE game/src game/sim engine/include
                                            third_party)
target_link_libraries(game_sim PRIVATE eng glm::glm EnTT::EnTT)
target_compile_definitions(
  game_sim PRIVATE APP_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/game/assets")
eng_target_hardening(game_sim)

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
  else()
    message(WARNING "Python3 not found: layering + assetlint tests disabled")
  endif()

  add_executable(dungeon_layout_tests game/tests/DungeonLayoutTests.cpp
                                      game/src/DungeonGen.cpp)
  target_include_directories(dungeon_layout_tests PRIVATE game/src)
  add_test(NAME dungeon_layout COMMAND dungeon_layout_tests)

  # The simulation harness doubles as a regression test: its built-in smoke
  # script must pass (exit 0).
  add_test(NAME sim_smoke COMMAND game_sim)

  add_executable(
    level_document_tests game/tests/LevelDocumentTests.cpp
                         game/src/LevelDocument.cpp game/src/DungeonGen.cpp)
  target_include_directories(level_document_tests PRIVATE game/src third_party)
  target_link_libraries(level_document_tests PRIVATE eng_toml)
  add_test(NAME level_document COMMAND level_document_tests)

  add_executable(targeting_tests game/tests/TargetingTests.cpp
                                 game/src/Targeting.cpp)
  target_include_directories(targeting_tests PRIVATE game/src)
  target_link_libraries(targeting_tests PRIVATE glm::glm)
  add_test(NAME targeting COMMAND targeting_tests)

  add_executable(fps_controller_tests game/tests/FpsControllerTests.cpp)
  target_link_libraries(fps_controller_tests PRIVATE eng)
  add_test(NAME fps_controller COMMAND fps_controller_tests)

  add_executable(physics_tests game/tests/PhysicsTests.cpp)
  target_link_libraries(physics_tests PRIVATE eng)
  add_test(NAME physics_tests COMMAND physics_tests)

  add_executable(scene_registry_tests engine/tests/SceneRegistryTests.cpp
                                      engine/src/SceneRegistry.cpp)
  target_include_directories(
    scene_registry_tests PRIVATE engine/include engine/src third_party/imgui)
  target_link_libraries(scene_registry_tests PRIVATE glm::glm)
  add_test(NAME scene_registry_tests COMMAND scene_registry_tests)

  add_executable(scene_tests engine/tests/SceneTests.cpp
                             engine/src/ecs/Scene.cpp)
  target_include_directories(scene_tests PRIVATE engine/include engine/src
                                                 third_party/imgui)
  target_link_libraries(scene_tests PRIVATE glm::glm EnTT::EnTT)
  add_test(NAME scene_tests COMMAND scene_tests)

  add_executable(
    scene_sync_tests engine/tests/SceneSyncTests.cpp engine/src/ecs/Scene.cpp
                     engine/src/ecs/SceneSync.cpp)
  target_include_directories(scene_sync_tests PRIVATE engine/include engine/src
                                                      third_party/imgui)
  target_link_libraries(scene_sync_tests PRIVATE glm::glm EnTT::EnTT)
  add_test(NAME scene_sync_tests COMMAND scene_sync_tests)

  add_executable(byte_stream_tests game/tests/ByteStreamTests.cpp
                                   game/src/scene/ByteStream.cpp)
  target_include_directories(byte_stream_tests PRIVATE game/src/scene)
  target_link_libraries(byte_stream_tests PRIVATE glm::glm)
  add_test(NAME byte_stream COMMAND byte_stream_tests)

  add_executable(
    component_registry_tests
    game/tests/ComponentRegistryTests.cpp game/src/scene/ByteStream.cpp
    game/src/scene/ComponentRegistry.cpp)
  target_include_directories(component_registry_tests
                             PRIVATE game/src/scene engine/include third_party)
  target_link_libraries(component_registry_tests PRIVATE glm::glm EnTT::EnTT)
  add_test(NAME component_registry COMMAND component_registry_tests)

  add_executable(damage_system_tests game/tests/DamageSystemTests.cpp
                                     game/src/combat/DamageSystem.cpp)
  target_include_directories(damage_system_tests
                             PRIVATE game/src engine/include third_party)
  target_link_libraries(damage_system_tests PRIVATE glm::glm EnTT::EnTT)
  add_test(NAME damage_system COMMAND damage_system_tests)

  add_executable(feel_components_tests game/tests/FeelComponentsTests.cpp)
  target_include_directories(feel_components_tests
                             PRIVATE game/src engine/include third_party)
  target_link_libraries(feel_components_tests PRIVATE glm::glm EnTT::EnTT)
  add_test(NAME feel_components COMMAND feel_components_tests)

  add_executable(stamina_system_tests game/tests/StaminaSystemTests.cpp
                                      game/src/combat/StaminaSystem.cpp)
  target_include_directories(stamina_system_tests
                             PRIVATE game/src engine/include third_party)
  target_link_libraries(stamina_system_tests PRIVATE glm::glm EnTT::EnTT)
  add_test(NAME stamina_system COMMAND stamina_system_tests)

  add_executable(poise_system_tests game/tests/PoiseSystemTests.cpp
                                    game/src/combat/PoiseSystem.cpp)
  target_include_directories(poise_system_tests PRIVATE game/src engine/include
                                                        third_party)
  target_link_libraries(poise_system_tests PRIVATE glm::glm EnTT::EnTT)
  add_test(NAME poise_system COMMAND poise_system_tests)

  add_executable(
    action_state_tests
    game/tests/ActionStateSystemTests.cpp game/src/combat/ActionStateSystem.cpp
    game/src/combat/StaminaSystem.cpp)
  target_include_directories(action_state_tests PRIVATE game/src engine/include
                                                        third_party)
  target_link_libraries(action_state_tests PRIVATE glm::glm EnTT::EnTT)
  add_test(NAME action_state COMMAND action_state_tests)

  add_executable(
    defense_system_tests
    game/tests/DefenseSystemTests.cpp game/src/combat/DefenseSystem.cpp
    game/src/combat/PoiseSystem.cpp game/src/combat/StaminaSystem.cpp)
  target_include_directories(defense_system_tests
                             PRIVATE game/src engine/include third_party)
  target_link_libraries(defense_system_tests PRIVATE glm::glm EnTT::EnTT)
  add_test(NAME defense_system COMMAND defense_system_tests)

  add_executable(damage_types_tests game/tests/DamageTypesTests.cpp
                                    game/src/combat/DamageSystem.cpp)
  target_include_directories(damage_types_tests PRIVATE game/src engine/include
                                                        third_party)
  target_link_libraries(damage_types_tests PRIVATE glm::glm EnTT::EnTT)
  add_test(NAME damage_types COMMAND damage_types_tests)

  add_executable(weapon_timing_tests game/tests/WeaponTimingTests.cpp
                                     game/src/combat/WeaponLibrary.cpp)
  target_include_directories(weapon_timing_tests
                             PRIVATE game/src engine/include third_party)
  target_link_libraries(weapon_timing_tests PRIVATE eng_toml glm::glm
                                                    EnTT::EnTT)
  add_test(NAME weapon_timing COMMAND weapon_timing_tests)

  add_executable(
    combat_scenario_tests
    game/tests/CombatScenarioTests.cpp game/src/combat/DamageSystem.cpp
    game/src/combat/PoiseSystem.cpp game/src/combat/DefenseSystem.cpp
    game/src/combat/ActionStateSystem.cpp game/src/combat/StaminaSystem.cpp)
  target_include_directories(combat_scenario_tests
                             PRIVATE game/src engine/include third_party)
  target_link_libraries(combat_scenario_tests PRIVATE glm::glm EnTT::EnTT)
  add_test(NAME combat_scenario COMMAND combat_scenario_tests)

  add_executable(
    status_effect_tests
    game/tests/StatusEffectTests.cpp game/src/combat/StatusEffectSystem.cpp
    game/src/combat/DamageSystem.cpp)
  target_include_directories(status_effect_tests
                             PRIVATE game/src engine/include third_party)
  target_link_libraries(status_effect_tests PRIVATE glm::glm EnTT::EnTT)
  add_test(NAME status_effect COMMAND status_effect_tests)

  add_executable(
    map_serializer_tests
    game/tests/MapSerializerTests.cpp game/src/scene/ByteStream.cpp
    game/src/scene/ComponentRegistry.cpp game/src/scene/MapSerializer.cpp)
  target_include_directories(map_serializer_tests
                             PRIVATE game/src/scene engine/include third_party)
  target_link_libraries(map_serializer_tests PRIVATE glm::glm EnTT::EnTT)
  add_test(NAME map_serializer COMMAND map_serializer_tests)

  add_executable(render_palette_tests game/tests/RenderPaletteTests.cpp
                                      game/src/RenderPalette.cpp)
  target_include_directories(render_palette_tests PRIVATE game/src third_party
                                                          engine/include)
  target_link_libraries(render_palette_tests PRIVATE eng_toml glm::glm eng)
  add_test(NAME render_palette COMMAND render_palette_tests)

  add_executable(lobby_dressing_tests game/tests/LobbyDressingTests.cpp
                                      game/src/LobbyDressing.cpp)
  target_include_directories(lobby_dressing_tests PRIVATE game/src third_party
                                                          engine/include)
  target_link_libraries(lobby_dressing_tests PRIVATE eng_toml glm::glm eng)
  add_test(NAME lobby_dressing COMMAND lobby_dressing_tests)

  add_executable(showcase_visibility_tests
                 game/tests/ShowcaseVisibilityTests.cpp)
  target_include_directories(showcase_visibility_tests PRIVATE game/src
                                                               third_party)
  target_link_libraries(showcase_visibility_tests PRIVATE eng_toml)
  target_compile_definitions(
    showcase_visibility_tests
    PRIVATE APP_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/game/assets")
  add_test(NAME showcase_visibility COMMAND showcase_visibility_tests)

  add_executable(physics_sync_tests game/tests/PhysicsSyncTests.cpp
                                    game/src/scene/PhysicsSync.cpp)
  target_include_directories(physics_sync_tests
                             PRIVATE game/src/scene engine/include third_party)
  target_link_libraries(physics_sync_tests PRIVATE eng EnTT::EnTT glm::glm)
  add_test(NAME physics_sync COMMAND physics_sync_tests)

  add_executable(
    level_resource_tests
    game/tests/LevelResourceTests.cpp game/src/LevelResource.cpp
    game/src/LevelDocument.cpp game/src/DungeonGen.cpp)
  target_include_directories(level_resource_tests PRIVATE game/src third_party
                                                          engine/include)
  target_link_libraries(level_resource_tests PRIVATE eng_toml glm::glm eng)
  target_compile_definitions(
    level_resource_tests
    PRIVATE APP_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/game/assets")
  add_test(NAME level_resource COMMAND level_resource_tests)

  # ObjLoader comes from the linked engine, not a second compile of the same
  # source: the engine layers link whole-archive, so recompiling it here would
  # be a duplicate definition.
  add_executable(obj_geometry_tests engine/tests/ObjGeometryTests.cpp)
  target_include_directories(obj_geometry_tests PRIVATE engine/src
                                                        engine/include)
  target_link_libraries(obj_geometry_tests PRIVATE eng glm::glm)
  add_test(NAME obj_geometry COMMAND obj_geometry_tests)

  add_executable(step_clock_tests engine/tests/StepClockTests.cpp
                                  engine/src/StepClock.cpp)
  target_include_directories(step_clock_tests PRIVATE engine/include)
  add_test(NAME step_clock COMMAND step_clock_tests)

  add_executable(core_object_tests engine/tests/CoreObjectTests.cpp
                                   engine/src/core/Object.cpp)
  target_include_directories(core_object_tests PRIVATE engine/include)
  add_test(NAME core_object COMMAND core_object_tests)

  add_executable(trace_tests engine/tests/TraceTests.cpp
                             engine/src/diagnostics/Trace.cpp)
  target_include_directories(trace_tests PRIVATE engine/include)
  add_test(NAME trace COMMAND trace_tests)

  add_executable(profiler_tests engine/tests/ProfilerTests.cpp
                                engine/src/diagnostics/Profiler.cpp)
  target_include_directories(profiler_tests PRIVATE engine/include)
  add_test(NAME profiler COMMAND profiler_tests)

  add_executable(frame_capture_tests engine/tests/FrameCaptureTests.cpp
                                     engine/src/render/FrameCapture.cpp)
  target_include_directories(frame_capture_tests PRIVATE engine/include)
  target_link_libraries(frame_capture_tests PRIVATE ${CMAKE_DL_LIBS})
  add_test(NAME frame_capture COMMAND frame_capture_tests)

  add_executable(
    actions_tests engine/tests/ActionsTests.cpp engine/src/systems/Ease.cpp
                  engine/src/systems/Actions.cpp engine/src/core/Object.cpp)
  target_include_directories(actions_tests PRIVATE engine/include)
  target_link_libraries(actions_tests PRIVATE glm::glm)
  add_test(NAME actions COMMAND actions_tests)

  add_executable(events_tests engine/tests/EventsTests.cpp
                              engine/src/systems/Events.cpp)
  target_include_directories(events_tests PRIVATE engine/include)
  add_test(NAME events COMMAND events_tests)

  add_executable(filesystem_tests engine/tests/FileSystemTests.cpp
                                  engine/src/io/FileSystem.cpp)
  target_include_directories(filesystem_tests PRIVATE engine/include)
  add_test(NAME filesystem COMMAND filesystem_tests)

  add_executable(
    directory_watcher_tests
    engine/tests/DirectoryWatcherTests.cpp engine/src/io/DirectoryWatcher.cpp
    engine/src/io/FileSystem.cpp)
  target_include_directories(directory_watcher_tests PRIVATE engine/include)
  add_test(NAME directory_watcher COMMAND directory_watcher_tests)

  add_executable(
    content_tests
    engine/tests/ContentTests.cpp engine/src/content/TextResource.cpp
    engine/src/io/FileSystem.cpp engine/src/core/Object.cpp)
  target_include_directories(content_tests PRIVATE engine/include)
  add_test(NAME content COMMAND content_tests)

  add_executable(particle_asset_tests engine/tests/ParticleAssetTests.cpp)
  target_compile_definitions(particle_asset_tests
                             PRIVATE PROJECT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  add_test(NAME particle_assets COMMAND particle_asset_tests)

  add_executable(particle_options_tests engine/tests/ParticleOptionsTests.cpp)
  target_include_directories(particle_options_tests PRIVATE engine/include)
  target_link_libraries(particle_options_tests PRIVATE glm::glm)
  add_test(NAME particle_options COMMAND particle_options_tests)

  add_executable(enchantment_tests engine/tests/EnchantmentTests.cpp)
  target_include_directories(enchantment_tests PRIVATE engine/include)
  target_link_libraries(enchantment_tests PRIVATE glm::glm)
  target_compile_definitions(enchantment_tests
                             PRIVATE PROJECT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  add_test(NAME enchantment COMMAND enchantment_tests)

  add_executable(vfx_shader_asset_tests engine/tests/VfxShaderAssetTests.cpp)
  target_compile_definitions(vfx_shader_asset_tests
                             PRIVATE PROJECT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  add_test(NAME vfx_shader_assets COMMAND vfx_shader_asset_tests)

  add_executable(portal_geometry_tests game/tests/PortalGeometryTests.cpp
                                       game/src/PortalGeometry.cpp)
  target_include_directories(portal_geometry_tests PRIVATE game/src)
  target_link_libraries(portal_geometry_tests PRIVATE glm::glm)
  add_test(NAME portal_geometry COMMAND portal_geometry_tests)

  add_executable(model_tests engine/tests/ModelTests.cpp)
  target_include_directories(model_tests PRIVATE engine/include engine/src)
  target_link_libraries(model_tests PRIVATE eng glm::glm)
  add_test(NAME model COMMAND model_tests)

  add_executable(primitive_tests engine/tests/PrimitiveTests.cpp)
  target_include_directories(primitive_tests PRIVATE engine/include engine/src)
  target_link_libraries(primitive_tests PRIVATE eng glm::glm)
  target_compile_definitions(primitive_tests
                             PRIVATE PROJECT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  add_test(NAME primitive COMMAND primitive_tests)

  add_executable(model_import_tests engine/tests/ModelImportTests.cpp)
  target_include_directories(model_import_tests PRIVATE engine/include)
  target_link_libraries(model_import_tests PRIVATE eng glm::glm)
  add_test(NAME model_import COMMAND model_import_tests)

  add_executable(
    audio_tests
    engine/tests/AudioTests.cpp engine/src/audio/Audio.cpp
    engine/src/audio/SoundInstance.cpp engine/src/audio/SoundResource.cpp
    engine/src/io/FileSystem.cpp engine/src/core/Object.cpp)
  target_include_directories(audio_tests PRIVATE engine/include
                                                 engine/src/audio)
  target_link_libraries(audio_tests PRIVATE miniaudio)
  add_test(NAME audio COMMAND audio_tests)

  add_executable(
    map_runtime_tests
    game/tests/MapRuntimeTests.cpp
    game/src/scene/MapRuntime.cpp
    game/src/scene/PhysicsSync.cpp
    game/src/scene/MapSerializer.cpp
    game/src/scene/ComponentRegistry.cpp
    game/src/scene/ByteStream.cpp)
  target_include_directories(
    map_runtime_tests PRIVATE game/src/scene engine/include engine/src
                              third_party third_party/imgui)
  target_link_libraries(map_runtime_tests PRIVATE eng EnTT::EnTT glm::glm)
  add_test(NAME map_runtime COMMAND map_runtime_tests)

  add_executable(
    layout_to_scene_tests
    game/tests/LayoutToSceneTests.cpp game/src/scene/LayoutToScene.cpp
    game/src/DungeonGen.cpp)
  target_include_directories(
    layout_to_scene_tests PRIVATE game/src/scene game/src engine/include
                                  third_party)
  target_link_libraries(layout_to_scene_tests PRIVATE glm::glm EnTT::EnTT)
  add_test(NAME layout_to_scene COMMAND layout_to_scene_tests)
endif()

# API documentation is optional so normal builds do not require Doxygen.
find_package(Doxygen QUIET)
if(Doxygen_FOUND)
  set(DOXYGEN_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/docs")
  configure_file("${CMAKE_CURRENT_SOURCE_DIR}/docs/Doxyfile.in"
                 "${CMAKE_CURRENT_BINARY_DIR}/Doxyfile" @ONLY)
  add_custom_target(
    docs
    COMMAND "${DOXYGEN_EXECUTABLE}" "${CMAKE_CURRENT_BINARY_DIR}/Doxyfile"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Generating API documentation with Doxygen"
    VERBATIM)
else()
  message(STATUS "Doxygen not found; the docs target will not be available")
endif()
