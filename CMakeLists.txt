cmake_minimum_required(VERSION 3.16)
project(dungeon_crawler CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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
)
target_include_directories(eng
    PUBLIC engine/include
    PRIVATE third_party
)
target_link_libraries(eng PUBLIC glm::glm OgreOverlay PRIVATE OgreMain SDL2::SDL2)
target_compile_definitions(eng PRIVATE
    ENG_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/engine/assets"
    OGRE_PLUGIN_DIR="${OGRE_PLUGIN_DIR}"
)

add_executable(psx_demo samples/psx-demo/src/main.cpp)
target_link_libraries(psx_demo PRIVATE eng)
target_compile_definitions(psx_demo PRIVATE
    APP_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/samples/psx-demo/assets"
)

add_executable(game
    game/src/main.cpp
    game/src/FpsController.cpp
)
target_link_libraries(game PRIVATE eng)
target_compile_definitions(game PRIVATE
    APP_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/game/assets"
)
