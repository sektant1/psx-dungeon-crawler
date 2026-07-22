cmake_minimum_required(VERSION 3.16)
project(dungeon_crawler CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
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

# Shared compile/link options applied to first-party targets ONLY (never Jolt
# or OGRE): warnings, optional IPO, optional sanitizers. Call for each target.
function(eng_target_hardening tgt)
    target_compile_options(${tgt} PRIVATE -Wall -Wextra -Wno-unused-parameter)
    if(ENG_IPO_SUPPORTED)
        set_property(TARGET ${tgt} PROPERTY
                     INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
    endif()
    if(ENABLE_ASAN)
        target_compile_options(${tgt} PRIVATE
            -fsanitize=address,undefined -fno-omit-frame-pointer -g)
        target_link_options(${tgt} PRIVATE -fsanitize=address,undefined)
    endif()
endfunction()

# Prefer the vendored OGRE SDK (vendor/ogre/sdk) over any system install.
list(PREPEND CMAKE_PREFIX_PATH "${CMAKE_CURRENT_SOURCE_DIR}/vendor/ogre/sdk")

find_package(OGRE 14 REQUIRED)
find_package(SDL2 REQUIRED)
find_package(glm REQUIRED)

include(FetchContent)
# Jolt Physics. Pin to a stable release tag; bump deliberately.
FetchContent_Declare(
    JoltPhysics
    GIT_REPOSITORY https://github.com/jrouwe/JoltPhysics.git
    GIT_TAG        v5.6.0
    SOURCE_SUBDIR  Build
)
# Match our determinism/perf posture; disable debug renderer (needs dxc).
set(DOUBLE_PRECISION OFF CACHE BOOL "" FORCE)
set(GENERATE_DEBUG_SYMBOLS ON CACHE BOOL "" FORCE)
set(CROSS_PLATFORM_DETERMINISTIC OFF CACHE BOOL "" FORCE)
set(INTERPROCEDURAL_OPTIMIZATION OFF CACHE BOOL "" FORCE)
set(ENABLE_ALL_WARNINGS OFF CACHE BOOL "" FORCE)
set(DEBUG_RENDERER_IN_DEBUG_AND_RELEASE OFF CACHE BOOL "" FORCE)
set(DEBUG_RENDERER_IN_DISTRIBUTION OFF CACHE BOOL "" FORCE)
# The game uses Jolt's CPU physics only. Vulkan compute pulls in DXC shader
# compilation even with Jolt's debug renderer disabled.
set(JPH_USE_VK OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(JoltPhysics)

add_library(eng STATIC
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
)
target_include_directories(eng
    PUBLIC engine/include
    PRIVATE third_party
)
# Vendored imgui docking branch must win over OgreOverlay's bundled imgui so
# <imgui.h> resolves to the docking copy everywhere in the engine + game.
target_include_directories(eng BEFORE PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui)
target_link_libraries(eng PUBLIC glm::glm OgreOverlay PRIVATE OgreMain SDL2::SDL2 Jolt)
target_compile_definitions(eng PRIVATE
    ENG_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/engine/assets"
    OGRE_PLUGIN_DIR="${OGRE_PLUGIN_DIR}"
    OGRE_MEDIA_DIR="${OGRE_MEDIA_DIR}"
)
eng_target_hardening(eng)

# Shared PSX demo scene, data-driven from samples/common/assets.
add_library(demo_scene STATIC samples/common/DemoScene.cpp)
target_include_directories(demo_scene PUBLIC samples/common PRIVATE third_party)
target_link_libraries(demo_scene PUBLIC eng)
target_compile_definitions(demo_scene PUBLIC
    DEMO_SCENE_TOML="${CMAKE_CURRENT_SOURCE_DIR}/samples/common/assets/demo_scene.toml"
)
eng_target_hardening(demo_scene)

add_executable(psx_demo samples/psx-demo/src/main.cpp)
target_link_libraries(psx_demo PRIVATE demo_scene)
target_compile_definitions(psx_demo PRIVATE
    APP_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/samples/psx-demo/assets"
)
eng_target_hardening(psx_demo)

add_executable(game
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
    game/src/ParticleLibrary.cpp
    game/src/SceneFactory.cpp
    game/src/Targeting.cpp
    game/src/Dummy.cpp
    game/src/ViewModel.cpp
    # Compiled directly (not linked via the demo_scene lib) so building `game`
    # never triggers a separate demo target.
    samples/common/DemoScene.cpp
)
target_include_directories(game PRIVATE third_party samples/common)
target_link_libraries(game PRIVATE eng)
target_compile_definitions(game PRIVATE
    APP_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/game/assets"
    DEMO_SCENE_TOML="${CMAKE_CURRENT_SOURCE_DIR}/samples/common/assets/demo_scene.toml"
)
eng_target_hardening(game)

add_executable(level_editor
    game/src/editor_main.cpp
    game/src/EditorCamera.cpp
    game/src/DungeonMap.cpp
    game/src/DungeonGen.cpp
    game/src/LevelDocument.cpp
    game/src/LevelEditor.cpp
    game/src/ParticleLibrary.cpp
    game/src/JsonSceneLoader.cpp
)
target_include_directories(level_editor PRIVATE third_party)
target_link_libraries(level_editor PRIVATE eng)
target_compile_definitions(level_editor PRIVATE
    APP_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/game/assets"
)
eng_target_hardening(level_editor)

if(BUILD_TESTING)
    add_executable(dungeon_layout_tests
        game/tests/DungeonLayoutTests.cpp
        game/src/DungeonGen.cpp
    )
    target_include_directories(dungeon_layout_tests PRIVATE game/src)
    add_test(NAME dungeon_layout COMMAND dungeon_layout_tests)

    add_executable(level_document_tests
        game/tests/LevelDocumentTests.cpp
        game/src/LevelDocument.cpp
        game/src/DungeonGen.cpp
    )
    target_include_directories(level_document_tests PRIVATE game/src third_party)
    add_test(NAME level_document COMMAND level_document_tests)

    add_executable(targeting_tests
        game/tests/TargetingTests.cpp
        game/src/Targeting.cpp
    )
    target_include_directories(targeting_tests PRIVATE game/src)
    target_link_libraries(targeting_tests PRIVATE glm::glm)
    add_test(NAME targeting COMMAND targeting_tests)

    add_executable(fps_controller_tests
        game/tests/FpsControllerTests.cpp
        game/src/FpsController.cpp
    )
    target_include_directories(fps_controller_tests PRIVATE game/src)
    target_link_libraries(fps_controller_tests PRIVATE eng)
    add_test(NAME fps_controller COMMAND fps_controller_tests)

    add_executable(physics_tests
        game/tests/PhysicsTests.cpp
    )
    target_link_libraries(physics_tests PRIVATE eng)
    add_test(NAME physics_tests COMMAND physics_tests)

    add_executable(scene_registry_tests engine/tests/SceneRegistryTests.cpp engine/src/SceneRegistry.cpp)
    target_include_directories(scene_registry_tests PRIVATE engine/include engine/src third_party/imgui)
    target_link_libraries(scene_registry_tests PRIVATE glm::glm)
    add_test(NAME scene_registry_tests COMMAND scene_registry_tests)

    add_executable(editor_camera_tests game/tests/EditorCameraTests.cpp game/src/EditorCamera.cpp)
    target_include_directories(editor_camera_tests PRIVATE game/src)
    target_link_libraries(editor_camera_tests PRIVATE glm::glm)
    add_test(NAME editor_camera_tests COMMAND editor_camera_tests)
endif()

# API documentation is optional so normal builds do not require Doxygen.
find_package(Doxygen QUIET)
if(Doxygen_FOUND)
    set(DOXYGEN_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/docs")
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/docs/Doxyfile.in"
        "${CMAKE_CURRENT_BINARY_DIR}/Doxyfile"
        @ONLY
    )
    add_custom_target(docs
        COMMAND "${DOXYGEN_EXECUTABLE}" "${CMAKE_CURRENT_BINARY_DIR}/Doxyfile"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "Generating API documentation with Doxygen"
        VERBATIM
    )
else()
    message(STATUS "Doxygen not found; the docs target will not be available")
endif()
