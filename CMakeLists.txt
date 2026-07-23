cmake_minimum_required(VERSION 3.16)
project(dungeon_crawler CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 17)
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
    target_compile_options(${tgt} PRIVATE -fsanitize=address,undefined
                                          -fno-omit-frame-pointer -g)
    target_link_options(${tgt} PRIVATE -fsanitize=address,undefined)
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

add_library(
  eng STATIC
  third_party/imgui/imgui.cpp
  third_party/imgui/imgui_draw.cpp
  third_party/imgui/imgui_tables.cpp
  third_party/imgui/imgui_widgets.cpp
  engine/src/RenderCore.cpp
  engine/src/ObjLoader.cpp
  engine/src/ProceduralMeshes.cpp
  engine/src/Renderer.cpp
  engine/src/MaterialPreview.cpp
  engine/src/LoadingScreen.cpp
  engine/src/Particles.cpp
  engine/src/Log.cpp
  engine/src/Config.cpp
  engine/src/Input.cpp
  engine/src/Platform.cpp
  engine/src/Engine.cpp
  engine/src/EditorUi.cpp
  engine/src/DebugUi.cpp
  engine/src/UiContext.cpp
  engine/src/panels/RenderTuningPanel.cpp
  engine/src/panels/ScenePanel.cpp
  engine/src/panels/InspectorPanel.cpp
  engine/src/Physics.cpp
  engine/src/RenderPresets.cpp
  engine/src/SceneRegistry.cpp
  engine/src/core/Object.cpp
  engine/src/core/Entity.cpp
  engine/src/core/Space.cpp
  engine/src/core/GameSession.cpp
  engine/src/systems/Factory.cpp
  engine/src/diagnostics/Trace.cpp
  engine/src/diagnostics/Profiler.cpp
  engine/src/systems/Ease.cpp
  engine/src/systems/Actions.cpp
  engine/src/systems/Events.cpp
  engine/src/io/FileSystem.cpp
  engine/src/io/DirectoryWatcher.cpp)
target_include_directories(
  eng
  PUBLIC engine/include
  PRIVATE third_party)
# Vendored imgui docking branch must win over OgreOverlay's bundled imgui so
# <imgui.h> resolves to the docking copy everywhere in the engine + game.
target_include_directories(eng BEFORE
                           PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui)
target_link_libraries(
  eng
  PUBLIC glm::glm OgreOverlay
  PRIVATE OgreMain SDL2::SDL2 Jolt)
target_compile_definitions(
  eng
  PRIVATE ENG_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/engine/assets"
          OGRE_PLUGIN_DIR="${ENG_OGRE_PLUGIN_DIR}"
          OGRE_MEDIA_DIR="${ENG_OGRE_MEDIA_DIR}")
eng_target_hardening(eng)

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
target_compile_definitions(
  psx_demo
  PRIVATE APP_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/samples/psx-demo/assets")
eng_target_hardening(psx_demo)

add_executable(
  game
  game/src/main.cpp
  game/src/FpsController.cpp
  game/src/DungeonMap.cpp
  game/src/DungeonGen.cpp
  game/src/LevelDocument.cpp
  game/src/LevelEditor.cpp
  game/src/Melee.cpp
  game/src/Projectiles.cpp
  game/src/Spells.cpp
  game/src/CombatConfig.cpp
  game/src/LobbyDressing.cpp
  game/src/ParticleLibrary.cpp
  game/src/SceneFactory.cpp
  game/src/RenderPalette.cpp
  game/src/Targeting.cpp
  game/src/Dummy.cpp
  game/src/ViewModel.cpp
  # Compiled directly (not linked via the demo_scene lib) so building `game`
  # never triggers a separate demo target.
  samples/common/DemoScene.cpp)
target_include_directories(game PRIVATE third_party samples/common engine/src)
target_link_libraries(game PRIVATE eng)
target_compile_definitions(
  game
  PRIVATE
    APP_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/game/assets"
    DEMO_SCENE_TOML="${CMAKE_CURRENT_SOURCE_DIR}/samples/common/assets/demo_scene.toml"
)
eng_target_hardening(game)

add_executable(
  level_editor
  game/src/editor_main.cpp
  game/src/EditorCamera.cpp)
target_include_directories(level_editor PRIVATE third_party)
target_link_libraries(level_editor PRIVATE eng)
target_compile_definitions(
  level_editor PRIVATE APP_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/game/assets")
eng_target_hardening(level_editor)

if(BUILD_TESTING)
  add_executable(dungeon_layout_tests game/tests/DungeonLayoutTests.cpp
                                      game/src/DungeonGen.cpp)
  target_include_directories(dungeon_layout_tests PRIVATE game/src)
  add_test(NAME dungeon_layout COMMAND dungeon_layout_tests)

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

  add_executable(fps_controller_tests game/tests/FpsControllerTests.cpp
                                      game/src/FpsController.cpp)
  target_include_directories(fps_controller_tests PRIVATE game/src)
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

  add_executable(editor_camera_tests game/tests/EditorCameraTests.cpp
                                     game/src/EditorCamera.cpp)
  target_include_directories(editor_camera_tests PRIVATE game/src)
  target_link_libraries(editor_camera_tests PRIVATE glm::glm)
  add_test(NAME editor_camera_tests COMMAND editor_camera_tests)

  add_executable(render_palette_tests game/tests/RenderPaletteTests.cpp
                                      game/src/RenderPalette.cpp)
  target_include_directories(render_palette_tests PRIVATE game/src third_party engine/include)
  target_link_libraries(render_palette_tests PRIVATE eng_toml glm::glm eng)
  add_test(NAME render_palette COMMAND render_palette_tests)

  add_executable(lobby_dressing_tests game/tests/LobbyDressingTests.cpp
                                       game/src/LobbyDressing.cpp)
  target_include_directories(lobby_dressing_tests PRIVATE game/src third_party engine/include)
  target_link_libraries(lobby_dressing_tests PRIVATE eng_toml glm::glm eng)
  add_test(NAME lobby_dressing COMMAND lobby_dressing_tests)

  add_executable(obj_geometry_tests engine/tests/ObjGeometryTests.cpp
                                     engine/src/ObjLoader.cpp)
  target_include_directories(obj_geometry_tests PRIVATE engine/src engine/include)
  target_link_libraries(obj_geometry_tests PRIVATE eng glm::glm)
  add_test(NAME obj_geometry COMMAND obj_geometry_tests)

  add_executable(core_object_tests engine/tests/CoreObjectTests.cpp
                                   engine/src/core/Object.cpp
                                   engine/src/core/Entity.cpp
                                   engine/src/core/Space.cpp
                                   engine/src/core/GameSession.cpp)
  target_include_directories(core_object_tests PRIVATE engine/include)
  add_test(NAME core_object COMMAND core_object_tests)

  add_executable(factory_tests engine/tests/FactoryTests.cpp
                               engine/src/systems/Factory.cpp
                               engine/src/core/Object.cpp)
  target_include_directories(factory_tests PRIVATE engine/include)
  add_test(NAME factory COMMAND factory_tests)

  add_executable(trace_tests engine/tests/TraceTests.cpp
                             engine/src/diagnostics/Trace.cpp)
  target_include_directories(trace_tests PRIVATE engine/include)
  add_test(NAME trace COMMAND trace_tests)

  add_executable(profiler_tests engine/tests/ProfilerTests.cpp
                                engine/src/diagnostics/Profiler.cpp)
  target_include_directories(profiler_tests PRIVATE engine/include)
  add_test(NAME profiler COMMAND profiler_tests)

  add_executable(actions_tests engine/tests/ActionsTests.cpp
                               engine/src/systems/Ease.cpp
                               engine/src/systems/Actions.cpp
                               engine/src/core/Object.cpp)
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

  add_executable(directory_watcher_tests engine/tests/DirectoryWatcherTests.cpp
                                         engine/src/io/DirectoryWatcher.cpp
                                         engine/src/io/FileSystem.cpp)
  target_include_directories(directory_watcher_tests PRIVATE engine/include)
  add_test(NAME directory_watcher COMMAND directory_watcher_tests)
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
