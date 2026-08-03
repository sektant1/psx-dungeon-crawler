# 3.24 for $<LINK_LIBRARY:WHOLE_ARCHIVE,...>, used to link the engine layers.
cmake_minimum_required(VERSION 3.24)
project(raven_engine CXX)

include(CTest)
include(CMakeDependentOption)

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
else()
  message(STATUS "ccache: not found (install it; it makes rebuilds near-free)")
endif()

# --- Build speed: linker ----------------------------------------------------
# GNU ld is the slowest part of an incremental build here: every executable
# links the whole-archive engine layers plus Ogre. mold and lld do the same work
# several times faster, so use whichever is installed.
option(ENG_LINKER "Linker to use: auto, mold, lld, bfd" "auto")
if(NOT ENG_LINKER OR ENG_LINKER STREQUAL "auto")
  find_program(ENG_MOLD mold)
  find_program(ENG_LLD ld.lld)
  if(ENG_MOLD)
    set(ENG_LINKER_FLAG "-fuse-ld=mold")
  elseif(ENG_LLD)
    set(ENG_LINKER_FLAG "-fuse-ld=lld")
  endif()
elseif(NOT ENG_LINKER STREQUAL "bfd")
  set(ENG_LINKER_FLAG "-fuse-ld=${ENG_LINKER}")
endif()
if(ENG_LINKER_FLAG)
  add_link_options(${ENG_LINKER_FLAG})
  message(STATUS "linker: ${ENG_LINKER_FLAG}")
endif()

# --- Build speed: LTO/IPO ---------------------------------------------------
# Off by default. LTO defers most of the optimiser to link time, which is paid
# on every incremental relink, and mixing LTO objects across build types has
# produced "lto1: internal compiler error: resolution sub id ... not in object
# file" in this tree. Turn it on for release artifacts, not for development.
option(ENABLE_LTO "Link-time optimisation for release builds" OFF)
set(ENG_IPO_SUPPORTED OFF)
if(ENABLE_LTO)
  include(CheckIPOSupported)
  check_ipo_supported(RESULT ENG_IPO_SUPPORTED OUTPUT _ipo_err)
endif()

# --- Build speed: precompiled headers ---------------------------------------
# Ogre, imgui, EnTT, toml++ and the standard library headers dominate the
# per-file cost of the engine layers: they are large, templated, and included by
# nearly every translation unit. Precompiling them once per target removes that
# from every file in it.
option(ENABLE_PCH "Precompile the heavy third-party headers" ON)

# --- Build speed: unity builds ----------------------------------------------
# Off by default: batching translation units is a large fresh-build win but it
# merges anonymous namespaces, so a name collision between two files in a batch
# becomes a compile error. Turn it on for CI and clean builds.
option(ENABLE_UNITY "Batch translation units per target" OFF)
if(ENABLE_UNITY)
  set(CMAKE_UNITY_BUILD ON)
  set(CMAKE_UNITY_BUILD_BATCH_SIZE 12)
endif()

# --- Sanitizers: -DENABLE_ASAN=ON for an ASan+UBSan+LeakSan build -----------
option(ENABLE_ASAN "Build our targets with Address/UB/Leak sanitizers" OFF)

# The renderer is the Vulkan RHI. There was an ENG_RENDERER switch here while
# the OGRE/GL3Plus backend was still the qualified default; the RHI has since
# reached parity and OGRE is gone, so the switch and its second code path went
# with it. ENG_RENDERER_RHI is still defined for the sources that branch on it.
option(ENG_RHI_VULKAN "Build the Vulkan 1.3 RHI backend" ON)
cmake_dependent_option(
  ENG_BUILD_VULKAN_SMOKE
  "Build Vulkan RHI shader tooling and the windowed smoke executable"
  "${BUILD_TESTING}" "ENG_RHI_VULKAN" OFF)
if(BUILD_TESTING AND ENG_RHI_VULKAN AND NOT ENG_BUILD_VULKAN_SMOKE)
  message(FATAL_ERROR
          "Vulkan-enabled test builds require ENG_BUILD_VULKAN_SMOKE=ON")
endif()

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
#   eng_core       log, io, math/geometry, events, clocks, string ids, profiling
#
# The split exists to make a layering violation a *link* error rather than a
# review comment: eng_core cannot call the renderer because it does not link it.
# Header-level violations are caught separately by tools/check_layering.py,
# which is what enforces the rule while all public headers still share one
# include root (engine/include). `eng` stays the single target consumers link.

# Vendored imgui (docking branch) + SDL2 platform backend and ImGuizmo. The
# renderer backend is selected below; RHI supplies its own renderer backend.
# Built once here so no engine layer compiles third-party sources; the docking
# copy must win over any vendored imgui, hence the BEFORE include.
set(_eng_imgui_sources
  third_party/imgui/imgui.cpp
  third_party/imgui/imgui_draw.cpp
  third_party/imgui/imgui_tables.cpp
  third_party/imgui/imgui_widgets.cpp
  third_party/imgui/backends/imgui_impl_sdl2.cpp
  ${imguizmo_SOURCE_DIR}/src/ImGuizmo.cpp)
add_library(eng_imgui STATIC ${_eng_imgui_sources})
target_include_directories(
  eng_imgui BEFORE
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui
         ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui/backends)
target_link_libraries(eng_imgui PUBLIC eng_imguizmo PRIVATE SDL2::SDL2)

add_library(
  eng_core STATIC
  engine/src/core/Log.cpp
  engine/src/core/Object.cpp
  engine/src/core/StringId.cpp
  engine/src/core/Clock.cpp
  engine/src/core/AssetRoot.cpp
  engine/src/core/AssetName.cpp
  engine/src/io/ByteStream.cpp
  engine/src/io/FileSystem.cpp
  engine/src/io/DirectoryWatcher.cpp
  engine/src/content/TextResource.cpp
  engine/src/diagnostics/Trace.cpp
  engine/src/diagnostics/Profiler.cpp
  engine/src/diagnostics/FrameStats.cpp
  engine/src/systems/Ease.cpp
  engine/src/systems/Actions.cpp
  engine/src/systems/Events.cpp
  engine/src/core/StepClock.cpp
  engine/src/core/Loading.cpp
  engine/src/render/PrimitiveGeometry.cpp)
# One public include root for the whole engine, exported from the bottom layer
# so every layer and every consumer inherits it.
target_include_directories(eng_core PUBLIC engine/include PRIVATE third_party)
# eng_toml is header-only third party, the same category as glm: linking it at
# the bottom layer does not point a dependency upward. AssetRoot.cpp reads the
# content manifest with it.
target_link_libraries(eng_core PUBLIC glm::glm PRIVATE eng_toml)
# Last resort in the root-discovery chain (D5), so `make run` keeps working
# straight out of the build tree. An installed build finds its root through the
# three runtime candidates ahead of this one and never reads it.
target_compile_definitions(
  eng_core PRIVATE RAVEN_ASSET_ROOT_DEV="${CMAKE_CURRENT_SOURCE_DIR}/assets")

# Render Hardware Interface: the graphics-backend contract and its
# implementations. Sits beside eng_platform rather than inside eng_systems
# because it must not see the renderer -- the dependency runs the other way,
# once anything above starts driving it. Today nothing does: the engine still
# renders through OGRE. See engine/src/rhi/README.md.
set(_eng_rhi_sources
    engine/src/rhi/Registry.cpp
    engine/src/rhi/null/NullDevice.cpp
    engine/src/rhi/vulkan/VulkanDevice.cpp)
add_library(eng_rhi STATIC ${_eng_rhi_sources})
target_include_directories(eng_rhi PRIVATE engine/src/rhi)
target_link_libraries(eng_rhi PUBLIC "$<LINK_LIBRARY:WHOLE_ARCHIVE,eng_core>")
  target_compile_definitions(eng_rhi PRIVATE ENG_RENDERER_RHI=1)

if(ENG_RHI_VULKAN)
  target_sources(
    eng_rhi
    PRIVATE engine/src/rhi/vulkan/VulkanContext.cpp
            engine/src/rhi/vulkan/VulkanFormats.cpp
            engine/src/rhi/vulkan/VulkanResources.cpp
            engine/src/rhi/vulkan/VulkanPipeline.cpp
            engine/src/rhi/vulkan/VulkanFrame.cpp
            engine/src/rhi/vulkan/VmaImpl.cpp)
  target_include_directories(
    eng_rhi SYSTEM BEFORE PRIVATE "${VulkanHeaders_SOURCE_DIR}/include"
                                  "${VulkanMemoryAllocator_SOURCE_DIR}/include")
  target_link_libraries(
    eng_rhi PRIVATE vk-bootstrap::vk-bootstrap eng_vma Vulkan::Vulkan SDL2::SDL2)
  target_compile_definitions(eng_rhi PRIVATE ENG_RHI_VULKAN=1)
endif()

if(ENG_BUILD_VULKAN_SMOKE)
  find_program(ENG_GLSLANG_VALIDATOR NAMES glslangValidator REQUIRED)
  find_program(ENG_SPIRV_VAL NAMES spirv-val REQUIRED)
  set(_rhi_vulkan_shader_dir "${CMAKE_CURRENT_BINARY_DIR}/rhi-vulkan-smoke-shaders")
  set(_rhi_vulkan_vert "${_rhi_vulkan_shader_dir}/triangle.vert.spv")
  set(_rhi_vulkan_frag "${_rhi_vulkan_shader_dir}/triangle.frag.spv")

  add_custom_command(
    OUTPUT "${_rhi_vulkan_vert}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_rhi_vulkan_shader_dir}"
    COMMAND ${ENG_GLSLANG_VALIDATOR} -V --target-env vulkan1.3 -S vert
            -o "${_rhi_vulkan_vert}"
            "${CMAKE_CURRENT_SOURCE_DIR}/samples/rhi-vulkan-smoke/triangle.vert"
    COMMAND ${ENG_SPIRV_VAL} --target-env vulkan1.3 "${_rhi_vulkan_vert}"
    DEPENDS samples/rhi-vulkan-smoke/triangle.vert
    VERBATIM)
  add_custom_command(
    OUTPUT "${_rhi_vulkan_frag}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_rhi_vulkan_shader_dir}"
    COMMAND ${ENG_GLSLANG_VALIDATOR} -V --target-env vulkan1.3 -S frag
            -o "${_rhi_vulkan_frag}"
            "${CMAKE_CURRENT_SOURCE_DIR}/samples/rhi-vulkan-smoke/triangle.frag"
    COMMAND ${ENG_SPIRV_VAL} --target-env vulkan1.3 "${_rhi_vulkan_frag}"
    DEPENDS samples/rhi-vulkan-smoke/triangle.frag
    VERBATIM)
  add_custom_target(rhi_vulkan_smoke_shaders
                    DEPENDS "${_rhi_vulkan_vert}" "${_rhi_vulkan_frag}")

  add_executable(rhi_vulkan_smoke samples/rhi-vulkan-smoke/main.cpp)
  add_dependencies(rhi_vulkan_smoke rhi_vulkan_smoke_shaders)
  target_link_libraries(rhi_vulkan_smoke PRIVATE eng_rhi SDL2::SDL2)
  target_compile_definitions(
    rhi_vulkan_smoke
    PRIVATE ENG_SMOKE_VERT_SPV="${_rhi_vulkan_vert}"
            ENG_SMOKE_FRAG_SPV="${_rhi_vulkan_frag}")
  eng_target_hardening(rhi_vulkan_smoke)
endif()

  find_program(ENG_GLSLANG_VALIDATOR NAMES glslangValidator REQUIRED)
  find_program(ENG_SPIRV_VAL NAMES spirv-val REQUIRED)
  set(_eng_renderer_shader_dir
      "${CMAKE_CURRENT_BINARY_DIR}/rhi-renderer-shaders")
  set(_eng_renderer_shader_outputs)
  foreach(_shader scene.vert scene.frag fullscreen.vert fullscreen.frag
                  post.frag stylize.frag bloom_bright.frag bloom_blur.frag surface.vert surface.frag shadow.vert shadow.frag
                  particle.vert particle.frag
                  imgui.vert imgui.frag)
    get_filename_component(_stage "${_shader}" LAST_EXT)
    string(SUBSTRING "${_stage}" 1 -1 _stage)
    set(_source "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/vulkan/${_shader}")
    set(_output "${_eng_renderer_shader_dir}/${_shader}.spv")
    add_custom_command(
      OUTPUT "${_output}"
      COMMAND ${CMAKE_COMMAND} -E make_directory "${_eng_renderer_shader_dir}"
      COMMAND ${ENG_GLSLANG_VALIDATOR} -V --target-env vulkan1.3 -S ${_stage}
              -o "${_output}" "${_source}"
      COMMAND ${ENG_SPIRV_VAL} --target-env vulkan1.3 "${_output}"
      DEPENDS "${_source}"
      VERBATIM)
    list(APPEND _eng_renderer_shader_outputs "${_output}")
    string(REPLACE "." "_" _shader_define "${_shader}")
    string(TOUPPER "${_shader_define}" _shader_define)
    list(APPEND _eng_renderer_shader_definitions
         "ENG_RHI_${_shader_define}_SPV=\"${_output}\"")
  endforeach()
  add_custom_target(rhi_renderer_shaders
                    DEPENDS ${_eng_renderer_shader_outputs})

add_library(eng_platform STATIC engine/src/platform/Platform.cpp
                                engine/src/platform/ImageDecode.cpp
                                engine/src/platform/Input.cpp
                                engine/src/platform/Config.cpp)
target_include_directories(eng_platform PRIVATE third_party engine/src)
target_link_libraries(eng_platform PUBLIC "$<LINK_LIBRARY:WHOLE_ARCHIVE,eng_core>"
                      PRIVATE SDL2::SDL2 eng_stb)

# Renderer-independent source-model import. Kept as its own library so runtime,
# validation CLI, and hermetic tests execute exactly the same Assimp path.
add_library(eng_model_import STATIC engine/src/render/AssimpLoader.cpp)
target_include_directories(eng_model_import PUBLIC engine/include engine/src)
target_link_libraries(eng_model_import PUBLIC glm::glm PRIVATE assimp::assimp)

set(_eng_systems_sources
  engine/src/render/ImGuiTheme.cpp
  engine/src/render/ImGuiHint.cpp
  engine/src/ui/UiCanvas.cpp
  engine/src/ui/UiLayout.cpp
  engine/src/ui/TooltipDraw.cpp
  engine/src/ui/UiFade.cpp
  engine/src/ui/TargetBanner.cpp
  engine/src/ui/LoadingScreen.cpp
  engine/src/render/MaterialPreview.cpp
  engine/src/render/RenderPresets.cpp
  engine/src/render/SceneRegistry.cpp
  engine/src/render/FrameCapture.cpp
  engine/src/render/GifRecorder.cpp
  engine/src/render/Model.cpp
  engine/src/render/Primitive.cpp
  engine/src/render/PrototypeAssets.cpp
  engine/src/particles/ParticlePresets.cpp
  engine/src/particles/ParticleLibrary.cpp
  engine/src/particles/ParticleSim.cpp
  engine/src/particles/ParticleEmitters.cpp
  engine/src/particles/ParticleTextureCatalog.cpp
  engine/src/render/ImGuiLayout.cpp
  engine/src/physics/Physics.cpp
  engine/src/audio/SoundResource.cpp
  engine/src/audio/SoundInstance.cpp
  engine/src/audio/Audio.cpp)

  list(APPEND _eng_systems_sources
       engine/src/render/rhi/RenderCore.cpp
       engine/src/render/rhi/Renderer.cpp
       engine/src/render/rhi/BitmapFont.cpp
       engine/src/render/rhi/Warmup.cpp
       engine/src/render/rhi/Image.cpp
       engine/src/render/rhi/MaterialLibrary.cpp
       engine/src/particles/DecalSystemRhi.cpp)

add_library(eng_systems STATIC ${_eng_systems_sources})
target_include_directories(eng_systems PRIVATE third_party engine/src
                                               engine/src/audio)
target_link_libraries(eng_systems
  PUBLIC "$<LINK_LIBRARY:WHOLE_ARCHIVE,eng_platform>"
         "$<LINK_LIBRARY:WHOLE_ARCHIVE,eng_rhi>" eng_imgui
  PRIVATE SDL2::SDL2 Jolt miniaudio eng_model_import ${CMAKE_DL_LIBS})
  add_dependencies(eng_systems rhi_renderer_shaders)
  target_link_libraries(eng_systems PRIVATE eng_stb)
  target_compile_definitions(eng_systems PRIVATE ENG_RENDERER_RHI=1
                                                ${_eng_renderer_shader_definitions})

add_library(
  eng_framework STATIC engine/src/ecs/World.cpp engine/src/ecs/SceneSync.cpp
                       engine/src/ecs/PhysicsSync.cpp
                       engine/src/ecs/ComponentRegistry.cpp
                       engine/src/ecs/RendererSceneBackend.cpp
                       engine/src/ecs/Systems.cpp
                       engine/src/controllers/FpsController.cpp
                       engine/src/debug/DebugTools.cpp
                       engine/src/debug/SurfacePanels.cpp
                       engine/src/debug/ParticlePanel.cpp
                       engine/src/debug/Console.cpp)
target_include_directories(eng_framework PRIVATE third_party engine/src)
target_link_libraries(eng_framework PUBLIC "$<LINK_LIBRARY:WHOLE_ARCHIVE,eng_systems>"
                                           EnTT::EnTT)

# Facade: owns application lifetime and is the only target consumers name.
add_library(eng STATIC engine/src/app/Engine.cpp engine/src/app/Application.cpp
                       engine/src/app/FpsGameApp.cpp)
target_include_directories(eng PRIVATE third_party engine/src)
target_link_libraries(eng PUBLIC "$<LINK_LIBRARY:WHOLE_ARCHIVE,eng_framework>")
  target_compile_definitions(eng_platform PRIVATE ENG_RENDERER_RHI=1)
  target_compile_definitions(eng PRIVATE ENG_RENDERER_RHI=1)

foreach(_layer eng_imgui eng_core eng_rhi eng_platform eng_model_import eng_systems
               eng_framework eng)
  eng_target_hardening(${_layer})
endforeach()
# imgui is third-party: build it without our warning set.
target_compile_options(eng_imgui PRIVATE -w)

# --- Precompiled headers -----------------------------------------------------
# Only third-party and standard headers go in a PCH. First-party headers are
# edited constantly, and each edit invalidates the PCH and every object built
# against it, which costs more than it saves.
if(ENABLE_PCH)
  target_precompile_headers(
    eng_core
    PRIVATE
    <algorithm>
    <cmath>
    <cstdint>
    <memory>
    <string>
    <unordered_map>
    <vector>
    <glm/glm.hpp>)
    target_precompile_headers(
      eng_systems PRIVATE <imgui.h> <glm/glm.hpp> <glm/gtc/quaternion.hpp>)
  target_precompile_headers(eng_framework PRIVATE <entt/entt.hpp>
                            <glm/glm.hpp>)
  target_precompile_headers(eng PRIVATE <imgui.h> <glm/glm.hpp>)
endif()

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

# --- Shared content libraries ------------------------------------------------
# The scene/authoring code is compiled once here instead of being listed again
# in every consumer (it was in five source lists: game, mapgen, and three test
# targets). Two libraries, because the consumers split cleanly in two:
#
#   eng_ecs_headless  the engine sources a headless tool needs (the component
#                     registry, plus eng_core underneath it), for CLI/test
#                     targets that must NOT drag in Ogre/SDL/Jolt to read a
#                     .map.
#   game_content      the temporarily named authoring/runtime scene bridge:
#                     stable component ids, serializers and scene cooking.
#                     Renderer-free by construction.
#
# NEVER link eng_ecs_headless together with eng: eng already contains those
# objects (whole-archive), so linking both is a duplicate-symbol error. Windowed
# targets link eng; headless ones link eng_ecs_headless.
#
# It links eng_core rather than recompiling ByteStream.cpp of its own. eng_core
# is Ogre/SDL/Jolt-free -- log, io, diagnostics, and eng::assets -- so the "no
# heavy dependencies" property is unchanged, and a headless tool now gets the
# content resolver for free. Listing ByteStream.cpp here *as well* would put two
# definitions in one link.
add_library(eng_ecs_headless STATIC engine/src/ecs/ComponentRegistry.cpp)
target_include_directories(eng_ecs_headless PUBLIC engine/include
                           PRIVATE third_party)
target_link_libraries(eng_ecs_headless PUBLIC glm::glm EnTT::EnTT eng_core)
eng_target_hardening(eng_ecs_headless)

add_library(
  game_content STATIC
  game/src/scene/ComponentRegistry.cpp
  game/src/scene/MapSerializer.cpp
  game/src/scene/LayoutToScene.cpp
  editor/src/content/KitCatalog.cpp
  editor/src/content/SceneDocument.cpp
  editor/src/content/SceneSource.cpp
  editor/src/content/SceneWriter.cpp
  editor/src/content/SceneCook.cpp
  editor/src/content/GridMath.cpp
  editor/src/content/SceneValidate.cpp
  editor/src/content/RoomBuilder.cpp
  editor/src/content/SceneTemplates.cpp)
target_include_directories(game_content PUBLIC game/src game/src/scene
                                               engine/include
                            PRIVATE editor/include third_party)
target_link_libraries(game_content PUBLIC glm::glm EnTT::EnTT
                      PRIVATE eng_toml nlohmann_json::nlohmann_json)
eng_target_hardening(game_content)

# The placement editor: ImGui + ImGuizmo over the engine's offscreen viewport.
# Owns UI and commands only -- the scene format, the catalogue and the cooker
# compile into game_content, so there is no second serializer.
add_executable(
  scene_editor
  editor/src/app/main.cpp
  editor/src/scene/BrushPlacement.cpp
  editor/src/scene/DocumentRaycast.cpp
  editor/src/app/EditorApp.cpp
  editor/src/ui/EditorWorkspace.cpp
  editor/src/viewport/EditorCamera.cpp
  editor/src/ui/EditorIcons.cpp
  editor/src/project/EditorSettings.cpp
  editor/src/viewport/EntityGizmos.cpp
  editor/src/assets/FileBrowser.cpp
  editor/src/assets/GameVocabulary.cpp
  editor/src/assets/MaterialCatalog.cpp
  editor/src/assets/ModelImportPipeline.cpp
  editor/src/commands/Clipboard.cpp
  editor/src/commands/CommandPalette.cpp
  editor/src/commands/Commands.cpp
  editor/src/ui/ConfirmDialog.cpp
  editor/src/project/SceneBrowser.cpp
  editor/src/viewport/ViewportOverlay.cpp
  editor/src/ui/ComponentInspector.cpp
  editor/src/scene/EntityComponents.cpp
  editor/src/ui/OutlinerPanel.cpp
  editor/src/scene/OutlinerTree.cpp
  editor/src/scene/PaintSlot.cpp
  editor/src/viewport/ViewportGrid.cpp
  editor/src/scene/Picker.cpp
  editor/src/scene/PickTarget.cpp
  editor/src/viewport/PreviewBridge.cpp
  editor/src/ui/UiStage.cpp
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
  editor/src/project/RunGame.cpp)
target_include_directories(scene_editor PRIVATE editor/include game/src
                                                engine/src third_party
                                                engine/include)
target_link_libraries(scene_editor PRIVATE eng game_content eng_imgui
                                             eng_model_import
                                             nlohmann_json::nlohmann_json)
# No asset defines: the editor mounts the `editor` set and resolves logical
# paths like every other app. Where it needs a directory rather than a file --
# the default save location for a new scene -- it asks for the game pack's dir
# by name (eng::assets::packDir), and the playtest log goes next to the project
# (eng::assets::project) instead of climbing "/../.." out of an asset path.
eng_target_hardening(scene_editor)

# The single .scn -> .map cooker, as a CLI. Headless: CI cooks content without a
# GPU, and the editor calls the same function in-process.
add_executable(scene_cook editor/tools/scene_cook/main.cpp)
target_include_directories(scene_cook PRIVATE editor/include)
target_link_libraries(scene_cook PRIVATE game_content eng_ecs_headless)
eng_target_hardening(scene_cook)

# Static-model production gate. Emits Markdown suitable for build artifacts and
# production briefs; no renderer/window required.
add_executable(model_validate engine/tools/model_validate_main.cpp)
target_link_libraries(model_validate PRIVATE eng_model_import)
eng_target_hardening(model_validate)

add_executable(
  game
  game/src/main.cpp
  game/src/DungeonMap.cpp
  game/src/DungeonGen.cpp
  game/src/PropSystem.cpp
  game/src/CombatSystem.cpp
  game/src/BloodSystem.cpp
  game/src/ParticleCollider.cpp
  game/src/DebugOverlay.cpp
  game/src/ui/GameHud.cpp
  game/src/ui/GameHudStyle.cpp
  game/src/ui/TooltipBuilder.cpp
  game/src/HudModel.cpp
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
  game/src/PlayerWeapons.cpp
  game/src/PlayerSystem.cpp
  game/src/InteractionSystem.cpp
  game/src/LiveLevel.cpp
  game/src/LevelDocument.cpp
  game/src/LevelResource.cpp
  game/src/Projectiles.cpp
  game/src/LobbyDressing.cpp
  game/src/ParticleEffects.cpp
  game/src/SceneFactory.cpp
  game/src/PrototypeCatalogLoader.cpp
  game/src/RenderPalette.cpp
  game/src/Targeting.cpp
  game/src/Dummy.cpp
  game/src/ViewModel.cpp
  game/src/MapPlay.cpp
  game/src/scene/MapRuntime.cpp)
target_include_directories(game PRIVATE third_party samples/common engine/src
                                        game/src game/src/scene)
target_link_libraries(game PRIVATE demo_scene game_content)
eng_target_hardening(game)
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

  add_executable(hud_model_tests game/tests/HudModelTests.cpp
                                 game/src/HudModel.cpp)
  target_include_directories(hud_model_tests PRIVATE game/src engine/include
                                                     third_party)
  target_link_libraries(hud_model_tests PRIVATE glm::glm EnTT::EnTT)
  add_test(NAME hud_model COMMAND hud_model_tests)

  # The tooltip builder is a pure function of focus + catalog values, so it
  # tests without a renderer, a font atlas or an imgui context.
  add_executable(tooltip_builder_tests game/tests/TooltipBuilderTests.cpp
                                       game/src/ui/TooltipBuilder.cpp)
  target_include_directories(tooltip_builder_tests PRIVATE game/src)
  target_link_libraries(tooltip_builder_tests PRIVATE eng_core)
  add_test(NAME tooltip_builder COMMAND tooltip_builder_tests)

  # The tooltip's pacing is pure logic over its own fields, so it tests with
  # no canvas, no font and no window.
  add_executable(tooltip_pace_tests engine/tests/TooltipPaceTests.cpp
                                    engine/src/ui/UiFade.cpp)
  target_include_directories(tooltip_pace_tests PRIVATE engine/include)
  target_link_libraries(tooltip_pace_tests PRIVATE glm::glm)
  add_test(NAME tooltip_pace COMMAND tooltip_pace_tests)

  # The log backlog is a vector and a mutex: it tests with no renderer, which
  # matters because what it guarantees is that lines logged BEFORE any UI
  # exists are still there when the console opens.
  add_executable(log_backlog_tests engine/tests/LogBacklogTests.cpp
                                   engine/src/core/Log.cpp)
  target_include_directories(log_backlog_tests PRIVATE engine/include)
  add_test(NAME log_backlog COMMAND log_backlog_tests)

  add_executable(ui_layout_tests engine/tests/UiLayoutTests.cpp
                                 engine/src/ui/UiLayout.cpp)
  target_include_directories(ui_layout_tests PRIVATE engine/include)
  target_link_libraries(ui_layout_tests PRIVATE glm::glm)
  add_test(NAME ui_layout COMMAND ui_layout_tests)

  add_executable(ui_canvas_tests engine/tests/UiCanvasTests.cpp)
  target_link_libraries(ui_canvas_tests PRIVATE eng)
  add_test(NAME ui_canvas COMMAND ui_canvas_tests)

  add_executable(game_hud_style_tests game/tests/GameHudStyleTests.cpp
                                      game/src/ui/GameHudStyle.cpp
                                      engine/src/ui/UiLayout.cpp)
  target_include_directories(game_hud_style_tests
                             PRIVATE game/src engine/include)
  target_link_libraries(game_hud_style_tests PRIVATE glm::glm)
  add_test(NAME game_hud_style COMMAND game_hud_style_tests)

  add_executable(debug_console_tests game/tests/DebugConsoleTests.cpp)
  target_link_libraries(debug_console_tests PRIVATE eng)
  add_test(NAME debug_console COMMAND debug_console_tests)

  add_executable(fps_controller_tests game/tests/FpsControllerTests.cpp)
  target_link_libraries(fps_controller_tests PRIVATE eng)
  add_test(NAME fps_controller COMMAND fps_controller_tests)

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

  add_executable(component_reflect_tests
                 engine/tests/ComponentReflectTests.cpp
                 engine/src/ecs/ComponentRegistry.cpp
                 engine/src/io/ByteStream.cpp)
  target_include_directories(component_reflect_tests
                             PRIVATE engine/include third_party)
  target_link_libraries(component_reflect_tests PRIVATE glm::glm EnTT::EnTT)
  add_test(NAME component_reflect COMMAND component_reflect_tests)

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
                                   engine/src/ecs/Systems.cpp)
  target_include_directories(ecs_systems_tests PRIVATE engine/include engine/src
                                                       third_party/imgui)
  target_link_libraries(ecs_systems_tests PRIVATE glm::glm EnTT::EnTT)
  add_test(NAME ecs_systems COMMAND ecs_systems_tests)

  add_executable(rhi_contract_tests engine/tests/RhiContractTests.cpp)
  target_link_libraries(rhi_contract_tests PRIVATE eng_rhi)
  add_test(NAME rhi_contract COMMAND rhi_contract_tests)

  if(ENG_RHI_VULKAN AND ENG_BUILD_VULKAN_SMOKE)
    add_test(NAME rhi_vulkan_smoke
             COMMAND rhi_vulkan_smoke --frames 120 --require-vulkan --validation
                     --exercise-resize)
    set_tests_properties(
      rhi_vulkan_smoke
      PROPERTIES RUN_SERIAL TRUE RESOURCE_LOCK vulkan_gpu TIMEOUT 180)
  endif()

  add_executable(byte_stream_tests game/tests/ByteStreamTests.cpp
                                   engine/src/io/ByteStream.cpp)
  target_include_directories(byte_stream_tests PRIVATE engine/include)
  target_link_libraries(byte_stream_tests PRIVATE glm::glm)
  add_test(NAME byte_stream COMMAND byte_stream_tests)

  add_executable(
    component_registry_tests
    game/tests/ComponentRegistryTests.cpp)
  target_include_directories(
    component_registry_tests PRIVATE game/src game/src/scene engine/include
                                     third_party)
  target_link_libraries(component_registry_tests PRIVATE game_content
                                                        eng_ecs_headless)
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
                        game/src/combat/BulletPattern.cpp
                        game/src/combat/CombatVocabulary.cpp)
  target_include_directories(enemy_library_tests
                             PRIVATE game/src engine/include third_party)
  target_link_libraries(enemy_library_tests PRIVATE eng_toml eng_core glm::glm
                                                    EnTT::EnTT)
  add_test(NAME enemy_library COMMAND enemy_library_tests)

  add_executable(enemy_ai_tests game/tests/EnemyAITests.cpp
                                game/src/enemy/EnemyAI.cpp)
  target_include_directories(enemy_ai_tests PRIVATE game/src engine/include
                                                    third_party)
  target_link_libraries(enemy_ai_tests PRIVATE glm::glm EnTT::EnTT)
  add_test(NAME enemy_ai COMMAND enemy_ai_tests)

  add_executable(enemy_save_tests game/tests/EnemySaveTests.cpp
                                  game/src/enemy/EnemySave.cpp
                                  engine/src/io/ByteStream.cpp)
  target_include_directories(enemy_save_tests PRIVATE game/src engine/include
                                                      third_party)
  target_link_libraries(enemy_save_tests PRIVATE eng_core glm::glm EnTT::EnTT)
  add_test(NAME enemy_save COMMAND enemy_save_tests)

  add_executable(enemy_spawner_tests game/tests/EnemySpawnerTests.cpp
                                     game/src/enemy/EnemySpawner.cpp)
  target_include_directories(enemy_spawner_tests
                             PRIVATE game/src engine/include third_party)
  target_link_libraries(enemy_spawner_tests PRIVATE eng_toml eng_core glm::glm)
  add_test(NAME enemy_spawner COMMAND enemy_spawner_tests)

  add_executable(
    weapon_timing_tests game/tests/WeaponTimingTests.cpp
                        game/src/combat/WeaponLibrary.cpp
                        game/src/combat/CombatVocabulary.cpp)
  target_include_directories(weapon_timing_tests
                             PRIVATE game/src engine/include third_party)
  target_link_libraries(weapon_timing_tests PRIVATE eng_toml eng_core glm::glm
                                                    EnTT::EnTT)
  add_test(NAME weapon_timing COMMAND weapon_timing_tests)

  add_executable(player_weapon_tests game/tests/PlayerWeaponTests.cpp
                                     game/src/PlayerWeapons.cpp)
  target_include_directories(player_weapon_tests
                             PRIVATE game/src engine/include third_party)
  target_link_libraries(player_weapon_tests PRIVATE eng_toml eng_core glm::glm
                                                    EnTT::EnTT)
  add_test(NAME player_weapon COMMAND player_weapon_tests)

  add_executable(bullet_pattern_tests game/tests/BulletPatternTests.cpp
                                      game/src/combat/BulletPattern.cpp)
  target_include_directories(bullet_pattern_tests PRIVATE game/src)
  target_link_libraries(bullet_pattern_tests PRIVATE glm::glm)
  add_test(NAME bullet_pattern COMMAND bullet_pattern_tests)

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
    game/src/combat/DamageSystem.cpp game/src/combat/CombatVocabulary.cpp)
  target_include_directories(status_effect_tests
                             PRIVATE game/src engine/include third_party)
  target_link_libraries(status_effect_tests PRIVATE eng_toml eng_core
                                                   glm::glm EnTT::EnTT)
  add_test(NAME status_effect COMMAND status_effect_tests)

  add_executable(
    map_serializer_tests
    game/tests/MapSerializerTests.cpp)
  target_include_directories(
    map_serializer_tests PRIVATE game/src game/src/scene engine/include
                                 third_party)
  target_link_libraries(map_serializer_tests PRIVATE game_content
                                                  eng_ecs_headless)
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
                 game/tests/ShowcaseVisibilityTests.cpp game/src/DungeonGen.cpp)
  target_include_directories(showcase_visibility_tests PRIVATE game/src
                                                               third_party)
  target_link_libraries(showcase_visibility_tests PRIVATE eng_toml eng_core)
  add_test(NAME showcase_visibility COMMAND showcase_visibility_tests)

  # PhysicsSync lives in eng_framework now, so the test links it rather than
  # recompiling the source (whole-archive linking would double-define it).
  add_executable(physics_sync_tests engine/tests/PhysicsSyncTests.cpp)
  target_include_directories(
    physics_sync_tests PRIVATE engine/include third_party)
  target_link_libraries(physics_sync_tests PRIVATE eng EnTT::EnTT glm::glm)
  add_test(NAME physics_sync COMMAND physics_sync_tests)

  add_executable(
    level_resource_tests
    game/tests/LevelResourceTests.cpp game/src/LevelResource.cpp
    game/src/LevelDocument.cpp game/src/DungeonGen.cpp)
  target_include_directories(level_resource_tests PRIVATE game/src third_party
                                                          engine/include)
  target_link_libraries(level_resource_tests PRIVATE eng_toml glm::glm eng)
  add_test(NAME level_resource COMMAND level_resource_tests)

  # ObjLoader comes from the linked engine, not a second compile of the same
  # source: the engine layers link whole-archive, so recompiling it here would
  # be a duplicate definition.
  # Scoped to the OGRE renderer: ObjLoader is an Ogre-typed source that only the
  # OGRE path compiles (the RHI renderer imports geometry through Assimp), so
  # its subject does not exist to test in an RHI build.

  add_executable(loading_tests engine/tests/LoadingTests.cpp
                               engine/src/core/Loading.cpp)
  target_include_directories(loading_tests PRIVATE engine/include)
  target_link_libraries(loading_tests PRIVATE glm::glm)
  add_test(NAME loading COMMAND loading_tests)

  add_executable(step_clock_tests engine/tests/StepClockTests.cpp
                                  engine/src/core/StepClock.cpp)
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
                                engine/src/diagnostics/Profiler.cpp
                                engine/src/core/StringId.cpp
                                engine/src/core/Log.cpp)
  target_include_directories(profiler_tests PRIVATE engine/include)
  add_test(NAME profiler COMMAND profiler_tests)

  add_executable(string_id_tests engine/tests/StringIdTests.cpp
                                 engine/src/core/StringId.cpp
                                 engine/src/core/Log.cpp)
  target_include_directories(string_id_tests PRIVATE engine/include)
  add_test(NAME string_id COMMAND string_id_tests)

  add_executable(clock_tests engine/tests/ClockTests.cpp
                             engine/src/core/Clock.cpp)
  target_include_directories(clock_tests PRIVATE engine/include)
  add_test(NAME clock COMMAND clock_tests)

  add_executable(frame_capture_tests engine/tests/FrameCaptureTests.cpp
                                     engine/src/render/FrameCapture.cpp)
  target_include_directories(frame_capture_tests PRIVATE engine/include)
  target_link_libraries(frame_capture_tests PRIVATE ${CMAKE_DL_LIBS})
  add_test(NAME frame_capture COMMAND frame_capture_tests)

  add_executable(gif_recorder_tests engine/tests/GifRecorderTests.cpp
                                    engine/src/render/GifRecorder.cpp
                                    engine/src/core/Log.cpp)
  target_include_directories(gif_recorder_tests PRIVATE engine/include)
  add_test(NAME gif_recorder COMMAND gif_recorder_tests)

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

  # Root discovery, the manifest and mount-order resolution are pure logic over
  # the filesystem, so this builds its own throwaway asset tree in the temp dir
  # rather than asserting against the real one.
  add_executable(asset_root_tests engine/tests/AssetRootTests.cpp)
  target_link_libraries(asset_root_tests PRIVATE eng_core)
  add_test(NAME asset_root COMMAND asset_root_tests)

  add_executable(asset_name_tests engine/tests/AssetNameTests.cpp
                                  engine/src/core/AssetName.cpp)
  target_include_directories(asset_name_tests PRIVATE engine/include)
  add_test(NAME asset_name COMMAND asset_name_tests)

  # Config flattening is toml++ and two maps: no window, no renderer.
  add_executable(config_tests engine/tests/ConfigTests.cpp
                              engine/src/platform/Config.cpp
                              engine/src/core/Log.cpp)
  target_include_directories(config_tests PRIVATE engine/include third_party)
  add_test(NAME config COMMAND config_tests)

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

  # The flipbook window is pure arithmetic over ParticleTypes.h, and the rest of
  # this test reads the three files that have to agree about it as text.
  add_executable(particle_flipbook_tests engine/tests/ParticleFlipbookTests.cpp)
  target_include_directories(particle_flipbook_tests PRIVATE engine/include)
  target_compile_definitions(particle_flipbook_tests
                             PRIVATE PROJECT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  add_test(NAME particle_flipbook COMMAND particle_flipbook_tests)

  # The simulation is deliberately Ogre-free, so it links only glm and its own
  # two translation units -- no renderer, no physics, no window.
  add_executable(particle_sim_tests engine/tests/ParticleSimTests.cpp
                                    engine/src/particles/ParticleSim.cpp
                                    engine/src/particles/ParticleEmitters.cpp)
  target_include_directories(particle_sim_tests PRIVATE engine/include
                                                        engine/src)
  target_link_libraries(particle_sim_tests PRIVATE glm::glm)
  add_test(NAME particle_sim COMMAND particle_sim_tests)

  # The walk preview is pure camera maths, so eye height, pitch clamping and
  # the enter/leave round trip are all assertable without a window.
  add_executable(editor_camera_eye_tests editor/tests/EditorCameraEyeTests.cpp
                                         editor/src/viewport/EditorCamera.cpp)
  target_include_directories(editor_camera_eye_tests PRIVATE editor/include
                             engine/include)
  target_link_libraries(editor_camera_eye_tests PRIVATE eng)
  add_test(NAME editor_camera_eye COMMAND editor_camera_eye_tests)

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
  add_executable(portal_geometry_tests game/tests/PortalGeometryTests.cpp)
  target_include_directories(portal_geometry_tests PRIVATE game/src)
  target_link_libraries(portal_geometry_tests PRIVATE eng)
  add_test(NAME portal_geometry COMMAND portal_geometry_tests)

  # DecalSystem runs headlessly: attach() is optional and the rebuild path
  # returns early with no scene manager, so ageing, merging and eviction are all
  # testable. The .cpp still includes Ogre headers, hence the link against eng.
  add_executable(decal_system_tests engine/tests/DecalSystemTests.cpp)
  target_include_directories(decal_system_tests PRIVATE engine/src third_party)
  target_link_libraries(decal_system_tests PRIVATE eng)
  add_test(NAME decal_system COMMAND decal_system_tests)

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
    game/src/scene/MapRuntime.cpp)
  target_include_directories(
    map_runtime_tests PRIVATE game/src game/src/scene engine/include engine/src
                              third_party third_party/imgui)
  target_link_libraries(map_runtime_tests PRIVATE eng game_content EnTT::EnTT
                                               glm::glm)
  add_test(NAME map_runtime COMMAND map_runtime_tests)

  # --- authoring pipeline (.scn -> IR -> .map) --------------------------------
  # These run against the REAL assets/game/kit.toml and the real shipped scene:
  # the point is to catch content drift, not to test a fixture.
  add_executable(kit_catalog_tests editor/tests/KitCatalogTests.cpp)
  target_link_libraries(kit_catalog_tests PRIVATE game_content eng_ecs_headless)
  add_test(NAME kit_catalog COMMAND kit_catalog_tests)

  add_executable(editor_command_tests editor/tests/EditorCommandTests.cpp
                                      editor/src/commands/Commands.cpp
                                      editor/src/content/SceneDocument.cpp)
  target_include_directories(editor_command_tests PRIVATE editor/include
                                                          engine/include)
  target_link_libraries(editor_command_tests PRIVATE glm::glm)
  add_test(NAME editor_command COMMAND editor_command_tests)

  add_executable(editor_component_tests editor/tests/EditorComponentTests.cpp
                                        editor/src/scene/EntityComponents.cpp
                                        editor/src/content/SceneDocument.cpp)
  target_include_directories(editor_component_tests PRIVATE editor/include
                                                            engine/include)
  target_link_libraries(editor_component_tests PRIVATE glm::glm)
  add_test(NAME editor_component COMMAND editor_component_tests)

  add_executable(editor_outliner_tests editor/tests/EditorOutlinerTests.cpp
                                       editor/src/scene/OutlinerTree.cpp
                                       editor/src/scene/EntityComponents.cpp)
  target_include_directories(editor_outliner_tests PRIVATE editor/include
                                             engine/include)
  target_link_libraries(editor_outliner_tests PRIVATE game_content
                                                      eng_ecs_headless)
  add_test(NAME editor_outliner COMMAND editor_outliner_tests)

  add_executable(
    editor_outliner_panel_tests editor/tests/EditorOutlinerPanelTests.cpp
                                editor/src/ui/OutlinerPanel.cpp
                                editor/src/ui/EditorIcons.cpp)
  target_include_directories(editor_outliner_panel_tests
                             PRIVATE editor/include
                             engine/include)
  target_link_libraries(editor_outliner_panel_tests PRIVATE eng_imgui glm::glm)
  add_test(NAME editor_outliner_panel COMMAND editor_outliner_panel_tests)

  add_executable(editor_material_catalog_tests
                 editor/tests/EditorMaterialCatalogTests.cpp
                 editor/src/assets/MaterialCatalog.cpp)
  target_include_directories(editor_material_catalog_tests PRIVATE editor/include
                             engine/include third_party)
  add_test(NAME editor_material_catalog COMMAND editor_material_catalog_tests
           ${CMAKE_SOURCE_DIR}/assets/materials)

  add_executable(editor_vocabulary_tests editor/tests/EditorVocabularyTests.cpp
                                         editor/src/assets/GameVocabulary.cpp)
  target_include_directories(editor_vocabulary_tests PRIVATE editor/include
                             engine/include)
  add_test(NAME editor_vocabulary COMMAND editor_vocabulary_tests
           ${CMAKE_SOURCE_DIR}/assets/config/enemies.toml)

  add_executable(editor_viewport_overlay_tests
                 editor/tests/EditorViewportOverlayTests.cpp
                 editor/src/viewport/ViewportOverlay.cpp)
  target_include_directories(editor_viewport_overlay_tests PRIVATE editor/include
                             engine/include)
  target_link_libraries(editor_viewport_overlay_tests PRIVATE eng_imgui)
  add_test(NAME editor_viewport_overlay COMMAND editor_viewport_overlay_tests)

  add_executable(editor_workspace_tests
                 editor/tests/EditorWorkspaceTests.cpp
                 editor/src/ui/EditorWorkspace.cpp)
  target_include_directories(editor_workspace_tests PRIVATE editor/include
                                                           engine/include
                                                           third_party)
  target_link_libraries(editor_workspace_tests PRIVATE eng_imgui)
  add_test(NAME editor_workspace COMMAND editor_workspace_tests)

  add_executable(editor_scene_browser_tests
                 editor/tests/EditorSceneBrowserTests.cpp
                 editor/src/project/SceneBrowser.cpp)
  target_include_directories(editor_scene_browser_tests PRIVATE editor/include
                                             engine/include)
  add_test(NAME editor_scene_browser COMMAND editor_scene_browser_tests)

  add_executable(editor_settings_tests editor/tests/EditorSettingsTests.cpp
                                       editor/src/project/EditorSettings.cpp
                                       editor/src/project/RunGame.cpp)
  target_include_directories(editor_settings_tests PRIVATE editor/include
                             engine/include)
  add_test(NAME editor_settings COMMAND editor_settings_tests)

  add_executable(
    editor_entity_gizmo_tests editor/tests/EditorEntityGizmoTests.cpp
                              editor/src/viewport/EntityGizmos.cpp
                              editor/src/scene/Picker.cpp)
  target_include_directories(editor_entity_gizmo_tests PRIVATE editor/include
                             engine/include)
  target_link_libraries(editor_entity_gizmo_tests PRIVATE game_content eng_imgui
                                                          glm::glm)
  add_test(NAME editor_entity_gizmo COMMAND editor_entity_gizmo_tests)

  add_executable(
    editor_document_raycast_tests editor/tests/EditorDocumentRaycastTests.cpp
                                  editor/src/scene/DocumentRaycast.cpp
                                  editor/src/scene/Picker.cpp)
  target_include_directories(editor_document_raycast_tests
                             PRIVATE editor/include engine/include)
  target_link_libraries(editor_document_raycast_tests PRIVATE game_content
                                                              glm::glm)
  add_test(NAME editor_document_raycast COMMAND editor_document_raycast_tests)

  add_executable(
    editor_placement_tests editor/tests/EditorPlacementTests.cpp
                           editor/src/scene/BrushPlacement.cpp
                           editor/src/scene/DocumentRaycast.cpp
                           editor/src/scene/Picker.cpp)
  target_include_directories(editor_placement_tests PRIVATE editor/include
                                                            game/tests
                             engine/include)
  target_link_libraries(editor_placement_tests PRIVATE game_content
                                                       eng_ecs_headless
                                                       glm::glm)
  add_test(NAME editor_placement COMMAND editor_placement_tests)

  add_executable(
    editor_model_import_tests editor/tests/EditorModelImportTests.cpp
                              editor/src/assets/ModelImportPipeline.cpp
                              editor/src/assets/FileBrowser.cpp)
  target_include_directories(editor_model_import_tests PRIVATE editor/include
                             engine/include engine/src)
  target_link_libraries(editor_model_import_tests PRIVATE eng_model_import
                                                          glm::glm)
  target_compile_definitions(
    editor_model_import_tests PRIVATE PROJECT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  add_test(NAME editor_model_import COMMAND editor_model_import_tests)

  add_executable(editor_file_browser_tests
                 editor/tests/EditorFileBrowserTests.cpp
                 editor/src/assets/FileBrowser.cpp)
  target_include_directories(editor_file_browser_tests PRIVATE editor/include)
  add_test(NAME editor_file_browser COMMAND editor_file_browser_tests)

  add_executable(editor_clipboard_tests editor/tests/EditorClipboardTests.cpp
                                        editor/src/commands/Clipboard.cpp)
  target_include_directories(editor_clipboard_tests PRIVATE editor/include
                                                            engine/include)
  target_link_libraries(editor_clipboard_tests PRIVATE game_content glm::glm)
  add_test(NAME editor_clipboard COMMAND editor_clipboard_tests)

  add_executable(
    editor_command_palette_tests editor/tests/EditorCommandPaletteTests.cpp
                                 editor/src/commands/CommandPalette.cpp)
  target_include_directories(editor_command_palette_tests PRIVATE editor/include
                             engine/include)
  target_link_libraries(editor_command_palette_tests PRIVATE eng_imgui)
  add_test(NAME editor_command_palette COMMAND editor_command_palette_tests)

  add_executable(editor_viewport_grid_tests
                 editor/tests/EditorViewportGridTests.cpp
                 editor/src/viewport/ViewportGrid.cpp)
  target_include_directories(editor_viewport_grid_tests PRIVATE editor/include
                             engine/include)
  add_test(NAME editor_viewport_grid COMMAND editor_viewport_grid_tests)

  add_executable(editor_paint_slot_tests editor/tests/EditorPaintSlotTests.cpp
                                         editor/src/scene/PaintSlot.cpp)
  target_include_directories(editor_paint_slot_tests PRIVATE editor/include
                             engine/include)
  target_link_libraries(editor_paint_slot_tests PRIVATE glm::glm)
  add_test(NAME editor_paint_slot COMMAND editor_paint_slot_tests)

  add_executable(editor_ui_stage_tests
                 editor/tests/EditorUiStageTests.cpp
                 editor/src/ui/UiStage.cpp
                 game/src/HudModel.cpp)
  target_include_directories(editor_ui_stage_tests
                             PRIVATE editor/include game/src third_party
                             engine/include)
  target_link_libraries(editor_ui_stage_tests PRIVATE eng game_content
                                                      eng_imgui glm::glm)
  add_test(NAME editor_ui_stage COMMAND editor_ui_stage_tests)

  add_executable(editor_pick_target_tests
                 editor/tests/EditorPickTargetTests.cpp
                 editor/src/scene/PickTarget.cpp)
  target_include_directories(editor_pick_target_tests PRIVATE editor/include
                                                              engine/include)
  target_link_libraries(editor_pick_target_tests PRIVATE game_content glm::glm)
  add_test(NAME editor_pick_target COMMAND editor_pick_target_tests)

  add_executable(editor_picker_tests editor/tests/EditorPickerTests.cpp
                                     editor/src/scene/Picker.cpp)
  target_include_directories(editor_picker_tests PRIVATE editor/include
                             engine/include)
  target_link_libraries(editor_picker_tests PRIVATE glm::glm)
  add_test(NAME editor_picker COMMAND editor_picker_tests)

  add_executable(editor_camera_tests editor/tests/EditorCameraTests.cpp
                                     editor/src/viewport/EditorCamera.cpp)
  target_include_directories(editor_camera_tests PRIVATE editor/include
                             engine/include)
  target_link_libraries(editor_camera_tests PRIVATE glm::glm)
  add_test(NAME editor_camera COMMAND editor_camera_tests)

  add_executable(editor_camera_fly_tests editor/tests/EditorCameraFlyTests.cpp
                                         editor/src/viewport/EditorCamera.cpp)
  target_include_directories(editor_camera_fly_tests PRIVATE editor/include
                             engine/include)
  target_link_libraries(editor_camera_fly_tests PRIVATE glm::glm)
  add_test(NAME editor_camera_fly COMMAND editor_camera_fly_tests)

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

  add_executable(
    layout_to_scene_tests
    game/tests/LayoutToSceneTests.cpp game/src/DungeonGen.cpp)
  target_include_directories(
    layout_to_scene_tests PRIVATE game/src/scene game/src engine/include
                                  third_party)
  target_link_libraries(layout_to_scene_tests PRIVATE game_content
                                                   eng_ecs_headless)
  add_test(NAME layout_to_scene COMMAND layout_to_scene_tests)

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
