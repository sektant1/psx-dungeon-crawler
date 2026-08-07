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
  # The asset pipeline's runtime-readable half: the format table, the resource
  # database, and the readers for every intermediate the exporters write. In
  # eng_core because the game, the editor, the pipeline and the tests must all
  # read one definition of what a .rmesh is.
  engine/src/content/AssetType.cpp
  engine/src/content/ContentHash.cpp
  engine/src/content/ResourceDb.cpp
  engine/src/content/AssetFile.cpp
  engine/src/content/MeshAsset.cpp
  engine/src/content/TextureAsset.cpp
  engine/src/content/DataAsset.cpp
  engine/src/content/PackManifest.cpp
  engine/src/diagnostics/Trace.cpp
  engine/src/diagnostics/Profiler.cpp
  engine/src/diagnostics/FrameStats.cpp
  engine/src/diagnostics/MemoryProfiler.cpp
  engine/src/diagnostics/CodeSize.cpp
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

# Heap accounting (engine/src/diagnostics/MemoryProfiler.cpp): replaces global
# operator new with a counting one. Two relaxed atomic adds and a 16-byte header
# per allocation, which is cheap enough to leave on for development and not
# something to hand to a player -- so this is the switch to throw for a shipping
# build, and it is deliberately NOT tied to CMAKE_BUILD_TYPE: this repo builds
# Release by default, and a profiler that silently disables itself in the only
# configuration anyone runs is worse than no profiler.
#
# PUBLIC, because the header's enabled() and the operator new overrides have to
# agree in every translation unit that links this. A TU compiled with 0 next to
# one compiled with 1 means a block allocated with a header and freed without
# one, which frees the wrong pointer.
# A development build turns the development tools on without being asked:
# Connector attaches by default (RAVEN_CONNECTOR=0 opts out). Nothing here
# changes what is rendered -- the sink drops records when nobody is collecting
# them, so a dev run with no Connector open costs one idle thread and a socket
# that keeps failing to connect.
#
# Both Debug and RelWithDebInfo count: RelWithDebInfo is this tree's default and
# is what anyone actually plays while developing, so tying the debug channels to
# Debug alone would mean they were off in the build where the bug showed up.
if(CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
  target_compile_definitions(eng_core PUBLIC ENG_CONNECTOR_DEFAULT=1)
else()
  target_compile_definitions(eng_core PUBLIC ENG_CONNECTOR_DEFAULT=0)
endif()

option(ENG_MEMPROF "instrument global operator new for heap profiling" ON)
if(ENG_MEMPROF)
  target_compile_definitions(eng_core PUBLIC ENG_MEMPROF=1)
  # backtrace() resolves names out of the dynamic symbol table; without this the
  # sampled call stacks are addresses and nothing else.
  target_link_options(eng_core INTERFACE -rdynamic)
else()
  target_compile_definitions(eng_core PUBLIC ENG_MEMPROF=0)
endif()
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
  foreach(_shader scene.vert scene.frag skinned_scene.vert skinned_shadow.vert fullscreen.vert fullscreen.frag
                  post.frag stylize.frag bloom_bright.frag bloom_blur.frag surface.vert surface.frag shadow.vert shadow.frag
                  particle.vert particle.frag
                  debug_line.vert debug_line.frag
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

# The single translation unit that compiles stb_image, as its own target: the
# renderer decodes source images at load and the pipeline's Compression row
# decodes them at build time, and the editor links both. One definition.
add_library(eng_image_decode STATIC engine/src/platform/ImageDecode.cpp)
target_link_libraries(eng_image_decode PUBLIC eng_stb)
eng_target_hardening(eng_image_decode)

# Debug telemetry: named channels out of the engine and into a collector.
# Its own library because it is the only thing here that opens a socket, and
# because the RESP encoder is worth testing without linking a renderer.
add_library(eng_telemetry STATIC
            engine/src/telemetry/Resp.cpp
            engine/src/telemetry/Socket.cpp
            engine/src/telemetry/Telemetry.cpp
            engine/src/telemetry/RedisSink.cpp)
target_include_directories(eng_telemetry PUBLIC engine/include)
target_link_libraries(eng_telemetry PUBLIC eng_core)
if(WIN32)
  # Winsock is not linked by default, and getaddrinfo lives in ws2_32 too.
  target_link_libraries(eng_telemetry PRIVATE ws2_32)
endif()
find_package(Threads REQUIRED)
target_link_libraries(eng_telemetry PRIVATE Threads::Threads)
eng_target_hardening(eng_telemetry)

add_library(eng_platform STATIC engine/src/platform/Platform.cpp
                                engine/src/platform/Input.cpp
                                engine/src/platform/Config.cpp)
target_include_directories(eng_platform PRIVATE third_party engine/src)
target_link_libraries(eng_platform PUBLIC "$<LINK_LIBRARY:WHOLE_ARCHIVE,eng_core>"
                      PRIVATE SDL2::SDL2 eng_stb eng_image_decode)

# Renderer-independent source-model import. Kept as its own library so runtime,
# validation CLI, and hermetic tests execute exactly the same Assimp path.
add_library(eng_model_import STATIC engine/src/render/AssimpLoader.cpp
                                    engine/src/render/SkinnedAssimpLoader.cpp
                                    # The seam the conditioning pipeline plugs
                                    # into: read the .rmesh when the mounted
                                    # pack has one, run Assimp when it does not.
                                    engine/src/render/ConditionedModel.cpp)
target_include_directories(eng_model_import PUBLIC engine/include engine/src)
target_link_libraries(eng_model_import PUBLIC glm::glm eng_core
                      PRIVATE assimp::assimp)

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
  engine/src/animation/SkeletalAnimation.cpp
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
       engine/src/render/rhi/LabelRaster.cpp
       engine/src/render/rhi/MaterialLibrary.cpp
       engine/src/particles/DecalSystemRhi.cpp)

add_library(eng_systems STATIC ${_eng_systems_sources})
target_include_directories(eng_systems PRIVATE third_party engine/src
                                               engine/src/audio)
target_link_libraries(eng_systems
  PUBLIC "$<LINK_LIBRARY:WHOLE_ARCHIVE,eng_platform>"
         "$<LINK_LIBRARY:WHOLE_ARCHIVE,eng_rhi>" eng_imgui
  PRIVATE SDL2::SDL2 Jolt miniaudio eng_model_import ozz_animation
          ${CMAKE_DL_LIBS})
  add_dependencies(eng_systems rhi_renderer_shaders)
  target_link_libraries(eng_systems PRIVATE eng_stb)
  target_compile_definitions(eng_systems PRIVATE ENG_RENDERER_RHI=1
                                                ${_eng_renderer_shader_definitions})

add_library(
  eng_framework STATIC engine/src/ecs/World.cpp engine/src/ecs/SceneSync.cpp
                        engine/src/ecs/AudioSync.cpp
                       engine/src/ecs/MeshResolve.cpp
                       engine/src/ecs/PhysicsSync.cpp
                       engine/src/ecs/ComponentRegistry.cpp
                       # Also in eng_ecs_headless, like ComponentRegistry above
                       # and for the same reason: a headless tool links that,
                       # a windowed app links this, and nothing links both.
                       engine/src/ecs/MapSerializer.cpp
                       engine/src/ecs/RendererSceneBackend.cpp
                       engine/src/ecs/Systems.cpp
                       engine/src/ecs/ClipSystem.cpp
                       # Screen-space UI as entities. In src/ecs rather than
                       # beside the other ui/ sources because it reads the
                       # registry: src/ui is the systems layer, which sits below
                       # the ECS and may not include it (tools/check_layering.py).
                       # The public header stays eng/ui/UiScene.h -- it is a UI
                       # API; only the implementation is ECS work.
                       engine/src/ecs/UiScene.cpp
                       engine/src/controllers/FpsController.cpp
                       engine/src/camera/FirstPersonCameraRig.cpp
                       engine/src/camera/ThirdPersonCameraRig.cpp
                       engine/src/camera/ScreenCameraRig.cpp
                       engine/src/debug/DebugTools.cpp
                       engine/src/debug/SurfacePanels.cpp
                       engine/src/debug/ParticlePanel.cpp
                       engine/src/debug/ClipPanel.cpp
                       engine/src/debug/Console.cpp)
target_include_directories(eng_framework PRIVATE third_party engine/src)
target_link_libraries(eng_framework PUBLIC "$<LINK_LIBRARY:WHOLE_ARCHIVE,eng_systems>"
                                           EnTT::EnTT)

# Lua scripting. Its own target rather than part of eng_framework: that library
# is linked by every engine test and by the editor's preview world, and keeping
# the VM in a separate target makes "who depends on Lua" a link fact instead of
# a habit. It sits AT the framework layer -- check_layering.py maps
# engine/src/script to "framework" -- so its bindings may reach World, Physics
# and Input, and an upward include is still a lint failure.
add_library(eng_script STATIC engine/src/script/ScriptHost.cpp
                              engine/src/script/ScriptError.cpp
                              engine/src/script/ScriptChunkCache.cpp
                              engine/src/script/ScriptInstance.cpp
                              engine/src/script/bind/BindMath.cpp
                              engine/src/script/bind/BindEntity.cpp
                              engine/src/script/bind/BindComponents.cpp
                              engine/src/script/bind/BindWorld.cpp
                              engine/src/script/bind/BindInput.cpp
                              engine/src/script/bind/BindAudio.cpp
                              engine/src/script/bind/BindRuntime.cpp
                              engine/src/script/bind/BindModule.cpp
                              engine/src/script/bind/BindSave.cpp
                              engine/src/script/bind/BindTimer.cpp
                              engine/src/script/ScriptTimers.cpp
                              engine/src/script/bind/BindPhysics.cpp
                              engine/src/script/ScriptContactBridge.cpp
                              engine/src/script/ScriptConsole.cpp)
target_include_directories(eng_script PRIVATE third_party engine/src)
target_link_libraries(eng_script
  PUBLIC "$<LINK_LIBRARY:WHOLE_ARCHIVE,eng_framework>"
  PRIVATE eng_sol2)

# Facade: owns application lifetime and is the only target consumers name.
add_library(eng STATIC engine/src/app/Engine.cpp engine/src/app/Application.cpp
                       engine/src/app/FpsGameApp.cpp)
target_include_directories(eng PRIVATE third_party engine/src)
target_link_libraries(eng PUBLIC "$<LINK_LIBRARY:WHOLE_ARCHIVE,eng_script>"
                      PUBLIC eng_telemetry)
  target_compile_definitions(eng_platform PRIVATE ENG_RENDERER_RHI=1)
  target_compile_definitions(eng PRIVATE ENG_RENDERER_RHI=1)

# The project runtime: reads a project.toml, boots its scene into a World and
# runs the script host over it.
#
# Above `eng` rather than inside it, for the reason eng_script is its own
# target: only raven_player and the game link this, and "who plays a project"
# should be a link fact rather than a habit. It is also what keeps the engine
# facade free of a scene-boot policy -- a sample that just wants a window and a
# renderer still links `eng` and gets none of this.
add_library(eng_runtime STATIC engine/src/runtime/Project.cpp
                               engine/src/runtime/ProjectComponents.cpp
                               engine/src/runtime/SceneRuntime.cpp
                               engine/src/runtime/ProjectApp.cpp)
target_include_directories(eng_runtime PRIVATE third_party engine/src)
# eng_toml for project.toml, PRIVATE: reading a project is this library's job,
# not something its consumers should inherit a TOML parser to do.
target_link_libraries(eng_runtime PUBLIC "$<LINK_LIBRARY:WHOLE_ARCHIVE,eng>"
                      PRIVATE eng_toml)

foreach(_layer eng_imgui eng_core eng_rhi eng_platform eng_model_import eng_systems
               eng_framework eng_script eng eng_runtime)
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
  # sol2 is the most expensive header in the tree and every binding TU needs it.
  target_precompile_headers(eng_script PRIVATE <sol/sol.hpp> <entt/entt.hpp>
                            <glm/glm.hpp>)
  target_precompile_headers(eng PRIVATE <imgui.h> <glm/glm.hpp>)
endif()
