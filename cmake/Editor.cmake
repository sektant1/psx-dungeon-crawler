# The placement editor, the asset conditioning pipeline, and the headless CLIs.
#
# Grouped because they are the *authoring* half of the tree: everything here
# reads or writes content, and none of it ships in the game binary. The editor
# owns UI and commands only -- the scene format, the catalogue and the cooker
# live in game_content (see Content.cmake), so there is no second serializer.

# The placement editor: ImGui + ImGuizmo over the engine's offscreen viewport.
# Owns UI and commands only -- the scene format, the catalogue and the cooker
# compile into game_content, so there is no second serializer.
add_executable(
  scene_editor
  editor/src/app/main.cpp
  editor/src/scene/Attachments.cpp
  editor/src/scene/BrushPlacement.cpp
  editor/src/scene/DocumentRaycast.cpp
  editor/src/app/EditorApp.cpp
  editor/src/ui/EditorWorkspace.cpp
  editor/src/ui/EditorShell.cpp
  editor/src/app/SceneTabs.cpp
  editor/src/viewport/EditorCamera.cpp
  editor/src/ui/EditorIcons.cpp
  editor/src/project/EditorSettings.cpp
  editor/src/viewport/EntityGizmos.cpp
  editor/src/assets/FileBrowser.cpp
  editor/src/assets/GameVocabulary.cpp
  editor/src/assets/MaterialCatalog.cpp
  editor/src/assets/MeshCatalog.cpp
  editor/src/ui/EditorUi.cpp
  editor/src/assets/ModelImportPipeline.cpp
  editor/src/assets/ResourceDbPanel.cpp
  editor/src/content/SceneWorldExporter.cpp
  editor/src/commands/Clipboard.cpp
  editor/src/commands/CommandPalette.cpp
  editor/src/commands/Commands.cpp
  editor/src/ui/ConfirmDialog.cpp
  editor/src/project/SceneBrowser.cpp
  editor/src/viewport/ViewportOverlay.cpp
  editor/src/ui/ComponentInspector.cpp
  editor/src/scene/EntityComponents.cpp
  editor/src/scene/AlignTools.cpp
  editor/src/scene/Layers.cpp
  editor/src/scene/MultiEdit.cpp
  editor/src/scene/SelectionTools.cpp
  editor/src/viewport/CameraNavigation.cpp
  editor/src/ui/LayersPanel.cpp
  editor/src/ui/OutlinerPanel.cpp
  editor/src/scene/OutlinerTree.cpp
  editor/src/scene/PaintSlot.cpp
  editor/src/viewport/ViewportGrid.cpp
  editor/src/scene/Picker.cpp
  editor/src/scene/PickTarget.cpp
  editor/src/viewport/PreviewBridge.cpp
  editor/src/ui/UiStage.cpp
  editor/src/ui/UiSceneEditor.cpp
  # The game's palette loader, shared rather than approximated: the editor's
  # light switch answers a question about the shipped look, and an editor that
  # answers it with its own numbers is an editor that lies about the level.
  game/src/RenderPalette.cpp
  # The game's HUD, likewise: the 2D viewport draws the shipped class against a
  # dialled-in state. A preview that reimplements the HUD tells you about the
  # preview.
  game/src/ui/GameHud.cpp
  game/src/ui/GameHudStyle.cpp
  game/src/HudModel.cpp
  # The first-person rig, shared rather than approximated for the same reason
  # the palette and the HUD are: the viewport must show the weapon where the
  # game will put it, and a second implementation of "where" would not.
  game/src/FirstPersonHands.cpp
  game/src/HandsDefinition.cpp
  game/src/ViewmodelSocket.cpp
  game/src/WeaponViewmodel.cpp
  game/src/SpriteViewmodel.cpp
  game/src/ViewmodelMotion.cpp
  game/src/PlayerWeapons.cpp
  editor/src/project/RunGame.cpp
  editor/src/project/ProjectSession.cpp
  editor/src/project/ScriptWorkshop.cpp
  editor/src/project/ProjectExport.cpp
  editor/src/project/ProjectMigrate.cpp)
target_include_directories(scene_editor PRIVATE editor/include game/src
                                                engine/src third_party
                                                engine/include)
# eng_runtime for eng::runtime::Project: what a project IS has one definition,
# read by the editor that writes it and the player that plays it.
# game_rpg for the condition vocabulary: the inspector offers the kinds the RPG
# layer actually knows rather than a free-text field, because the field is a
# string and a typo is an entity that silently never appears.
target_link_libraries(scene_editor PRIVATE eng eng_runtime game_content game_rpg
                                             eng_imgui
                                             eng_model_import eng_acp
                                             nlohmann_json::nlohmann_json)
# No asset defines: the editor mounts the `editor` set and resolves logical
# paths like every other app. Where it needs a directory rather than a file --
# the default save location for a new scene -- it asks for the game pack's dir
# by name (eng::assets::packDir), and the playtest log goes next to the project
# (eng::assets::project) instead of climbing "/../.." out of an asset path.
eng_target_hardening(scene_editor)

# The Asset Conditioning Pipeline. Above the engine, not part of it: it links
# eng_core for the format definitions and eng_model_import for the DCC readers,
# and nothing links it back. No renderer, no window -- CI conditions content
# without a GPU, and the editor calls the same functions in-process.
add_library(eng_acp STATIC
            engine/src/acp/Pipeline.cpp
            engine/src/acp/Registry.cpp
            engine/src/acp/MeshExporter.cpp
            engine/src/acp/TextureExporter.cpp
            engine/src/acp/DataExporter.cpp
            engine/src/acp/PassthroughExporter.cpp)
target_include_directories(eng_acp PUBLIC engine/include
                           PRIVATE engine/src third_party)
target_link_libraries(eng_acp PUBLIC eng_core PRIVATE eng_model_import eng_toml
                                     eng_stb eng_image_decode)
eng_target_hardening(eng_acp)

# raven_acp: the pipeline and the resource database, from the shell. It also
# registers the World row, whose cooker lives in game_content -- which is why
# the executable and not eng_acp is where the two meet.
add_executable(raven_acp engine/tools/acp/main.cpp
                         editor/src/content/SceneWorldExporter.cpp)
target_include_directories(raven_acp PRIVATE editor/include)
target_link_libraries(raven_acp PRIVATE eng_acp game_content eng_ecs_headless)
eng_target_hardening(raven_acp)

# The single .scn -> .map cooker, as a CLI. Headless: CI cooks content without a
# GPU, and the editor calls the same function in-process.
add_executable(scene_cook editor/tools/scene_cook/main.cpp)
target_include_directories(scene_cook PRIVATE editor/include)
target_link_libraries(scene_cook PRIVATE game_content eng_ecs_headless)
eng_target_hardening(scene_cook)

# raven_player: plays a project.
#
# The one target in the tree that must NOT link anything under game/. That is
# the whole claim it exists to make -- a game authored in the editor and
# scripted in Lua runs on this, and the dungeon crawler's combat, dungeon
# generation and enemy library are not part of what it means to play a project.
# The claim is enforced rather than asserted: player_purity_test reads this
# binary's symbol table and fails on any game:: symbol. Before adding a library
# here, check it is not a way in.
add_executable(raven_player engine/tools/player/main.cpp)
target_link_libraries(raven_player PRIVATE eng_runtime)
eng_target_hardening(raven_player)

# raven_export: a project -> a directory somebody else can run.
#
# Links the authoring half because it has to cook, which is exactly why it is a
# tool and not part of the player: the binary it SHIPS still has no game or
# editor code in it, and player_purity checks that independently.
add_executable(raven_export editor/tools/project_export/main.cpp
                            editor/src/project/ProjectExport.cpp
  editor/src/project/ProjectMigrate.cpp)
target_include_directories(raven_export PRIVATE editor/include)
# eng_runtime brings `eng`, which already contains the component registry and
# the .map codec whole-archive -- so eng_ecs_headless must NOT be here as well.
# Linking both is a duplicate-symbol error; see the note in Content.cmake.
target_link_libraries(raven_export PRIVATE game_content eng_runtime)
eng_target_hardening(raven_export)

# raven_migrate: this game's scenes -> a project. Same library split as the
# exporter: it links the authoring half because it reads and writes .scn.
add_executable(raven_migrate editor/tools/project_migrate/main.cpp
                             editor/src/project/ProjectMigrate.cpp)
target_include_directories(raven_migrate PRIVATE editor/include)
target_link_libraries(raven_migrate PRIVATE game_content eng_runtime)
eng_target_hardening(raven_migrate)

# Static-model production gate. Emits Markdown suitable for build artifacts and
# production briefs; no renderer/window required.
add_executable(model_validate engine/tools/model_validate_main.cpp)
target_link_libraries(model_validate PRIVATE eng_model_import)
eng_target_hardening(model_validate)
