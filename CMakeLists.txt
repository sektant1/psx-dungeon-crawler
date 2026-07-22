cmake_minimum_required(VERSION 3.16)
project(dungeon_crawler CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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
    engine/src/RenderCore.cpp
    engine/src/ObjLoader.cpp
    engine/src/ProceduralMeshes.cpp
    engine/src/Renderer.cpp
    engine/src/Log.cpp
    engine/src/Config.cpp
    engine/src/Input.cpp
    engine/src/Platform.cpp
    engine/src/Engine.cpp
    engine/src/DebugUi.cpp
    engine/src/Physics.cpp
    engine/src/RenderPresets.cpp
)
target_include_directories(eng
    PUBLIC engine/include
    PRIVATE third_party
)
target_link_libraries(eng PUBLIC glm::glm OgreOverlay PRIVATE OgreMain SDL2::SDL2 Jolt)
target_compile_definitions(eng PRIVATE
    ENG_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/engine/assets"
    OGRE_PLUGIN_DIR="${OGRE_PLUGIN_DIR}"
    OGRE_MEDIA_DIR="${OGRE_MEDIA_DIR}"
)

# Shared PSX demo scene, data-driven from samples/common/assets.
add_library(demo_scene STATIC samples/common/DemoScene.cpp)
target_include_directories(demo_scene PUBLIC samples/common PRIVATE third_party)
target_link_libraries(demo_scene PUBLIC eng)
target_compile_definitions(demo_scene PUBLIC
    DEMO_SCENE_TOML="${CMAKE_CURRENT_SOURCE_DIR}/samples/common/assets/demo_scene.toml"
)

add_executable(psx_demo samples/psx-demo/src/main.cpp)
target_link_libraries(psx_demo PRIVATE demo_scene)
target_compile_definitions(psx_demo PRIVATE
    APP_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/samples/psx-demo/assets"
)

add_executable(game
    game/src/main.cpp
    game/src/FpsController.cpp
    game/src/DungeonMap.cpp
    game/src/DungeonGen.cpp
    game/src/LevelDocument.cpp
    game/src/LevelEditor.cpp
    game/src/Melee.cpp
    game/src/Projectiles.cpp
    game/src/SceneFactory.cpp
    game/src/Targeting.cpp
    game/src/Dummy.cpp
    game/src/ViewModel.cpp
)
target_include_directories(game PRIVATE third_party)
target_link_libraries(game PRIVATE demo_scene)
target_compile_definitions(game PRIVATE
    APP_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/game/assets"
)

add_executable(level_editor
    game/src/editor_main.cpp
    game/src/DungeonMap.cpp
    game/src/DungeonGen.cpp
    game/src/LevelDocument.cpp
    game/src/LevelEditor.cpp
)
target_include_directories(level_editor PRIVATE third_party)
target_link_libraries(level_editor PRIVATE eng)
target_compile_definitions(level_editor PRIVATE
    APP_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/game/assets"
)

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
