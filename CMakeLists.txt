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
)
target_include_directories(eng
    PUBLIC engine/include
    PRIVATE third_party
)
target_link_libraries(eng PUBLIC glm::glm OgreOverlay PRIVATE OgreMain SDL2::SDL2)
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
    game/src/Targeting.cpp
)
target_include_directories(game PRIVATE third_party)
target_link_libraries(game PRIVATE demo_scene)
target_compile_definitions(game PRIVATE
    APP_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/game/assets"
)

if(BUILD_TESTING)
    add_executable(dungeon_layout_tests
        game/tests/DungeonLayoutTests.cpp
        game/src/DungeonGen.cpp
    )
    target_include_directories(dungeon_layout_tests PRIVATE game/src)
    add_test(NAME dungeon_layout COMMAND dungeon_layout_tests)

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
endif()
