# The samples: small apps that exercise the engine's PUBLIC surface.
#
# They link `eng` and never engine/src, which is the property that makes them
# worth having -- a sample that reaches into private headers stops proving the
# public API is sufficient.

# Shared PSX demo scene, data-driven from assets/game.
add_library(demo_scene STATIC samples/common/DemoScene.cpp)
target_include_directories(
  demo_scene
  PUBLIC samples/common
  PRIVATE third_party)
target_link_libraries(demo_scene PUBLIC eng)
# No DEMO_SCENE_TOML: DemoScene::load has always taken the path as an argument,
# and its one caller (the game) now asks the resolver for "config/demo_scene.toml".
# The file is declared as the `common` pack in assets/assets.toml until the
# file move folds it into `game`.
eng_target_hardening(demo_scene)

add_executable(psx_demo samples/psx-demo/src/main.cpp
                        samples/psx-demo/src/DemoHud.cpp
                        samples/psx-demo/src/ShowcaseScene.cpp)
# eng_imgui, because the demo draws its own tuning tab and its placard: imgui is
# a third-party UI toolkit the engine exposes, not an engine internal.
target_link_libraries(psx_demo PRIVATE demo_scene eng_imgui)
# No engine/src here on purpose: a sample that reaches into the engine's private
# headers is a sample that stops proving the public API is enough.
# No APP_ASSET_DIR: the demo names content by logical path and lets
# eng::assets resolve it against its mount set (demo -> game -> engine).
eng_target_hardening(psx_demo)
