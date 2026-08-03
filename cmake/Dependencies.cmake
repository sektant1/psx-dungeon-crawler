# =============================================================================
# Dependencies.cmake — single source of truth for every third-party library.
#
# All deps are fetched + built from source by CPM.cmake (a thin, cached layer
# over FetchContent). A bare `git clone` of this repo builds with ZERO system
# packages and no committed binary SDKs: no apt/vcpkg/conan, no vendor/ blobs.
#
# Downloads are cached across build trees when CPM_SOURCE_CACHE is set (see the
# top-level CMakeLists). Bump a version by editing the pinned tag here and
# nowhere else. Every pin is exact and deliberate — never track a branch.
#
# Exposed targets after include(): glm::glm                 (header-only math)
# SDL2::SDL2               (windowing/input) Jolt                     (physics)
# tomlplusplus::tomlplusplus + <tomlplusplus/toml.hpp> compat include
#
# The renderer is not here: it is the Vulkan RHI in engine/src/rhi, which needs
# only the Vulkan loader and the SPIR-V toolchain the top-level file finds.
# =============================================================================

include(${CMAKE_CURRENT_LIST_DIR}/CPM.cmake)

# --- Vulkan RHI (strictly opt-in) --------------------------------------------
# The loader remains a runtime/system discovery; only headers and helper
# libraries are pinned and fetched. Defining Vulkan::Headers before
# find_package(Vulkan) makes vk-bootstrap compile against the exact header pin
# without replacing or vendoring the loader.
if(ENG_RHI_VULKAN)
    CPMAddPackage(
        NAME VulkanHeaders
        GITHUB_REPOSITORY KhronosGroup/Vulkan-Headers
        GIT_TAG v1.4.357
        DOWNLOAD_ONLY YES
    )
    add_library(eng_vulkan_headers INTERFACE)
    add_library(Vulkan::Headers ALIAS eng_vulkan_headers)
    target_include_directories(
        eng_vulkan_headers INTERFACE "${VulkanHeaders_SOURCE_DIR}/include")

    find_package(Vulkan 1.3 REQUIRED)

    CPMAddPackage(
        NAME vk-bootstrap
        GITHUB_REPOSITORY charles-lunarg/vk-bootstrap
        GIT_TAG v1.4.357
        OPTIONS "VK_BOOTSTRAP_TEST OFF" "VK_BOOTSTRAP_INSTALL OFF"
    )
    CPMAddPackage(
        NAME VulkanMemoryAllocator
        GITHUB_REPOSITORY GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator
        GIT_TAG v3.4.0
        DOWNLOAD_ONLY YES
    )
    add_library(eng_vma INTERFACE)
    target_include_directories(
        eng_vma INTERFACE "${VulkanMemoryAllocator_SOURCE_DIR}/include")
endif()

# --- glm ---------------------------------------------------------------------
cpmaddpackage(NAME glm GITHUB_REPOSITORY g-truc/glm GIT_TAG 1.0.1)

# --- EnTT (header-only ECS) --------------------------------------------------
# Scene-data source of truth. Header-only: just an include target, no build.
CPMAddPackage(
    NAME EnTT
    GITHUB_REPOSITORY skypjack/entt
    GIT_TAG v3.13.2
)

# --- stb image I/O -----------------------------------------------------------
# Header-only PNG decoder/writer for the engine-owned renderer. This pin is
# the engine's own image decoding, independent of any renderer package.
CPMAddPackage(
    NAME stb
    GITHUB_REPOSITORY nothings/stb
    GIT_TAG 2c980bb59875b0d32144a71867fbdebb2f77cd20
    DOWNLOAD_ONLY YES
)
add_library(eng_stb INTERFACE)
target_include_directories(eng_stb INTERFACE "${stb_SOURCE_DIR}")

# --- SDL2 --------------------------------------------------------------------
# Shared by default so we don't statically pull SDL's system backends into our
# binaries; the build tree ships the .so next to the executables.
set(_eng_sdl_options
    "SDL2_DISABLE_INSTALL ON"
    "SDL_TEST OFF"
    "SDL_SHARED ON"
    "SDL_STATIC OFF"
    # SDL 2.30.11's pipewire backend does not compile against newer system
    # PipeWire headers (pw_node_enum_params signature drift). We only need
    # SDL for windowing/input; audio still negotiates ALSA/PulseAudio.
    "SDL_PIPEWIRE OFF")
    # RHI uses SDL only for events, a native Vulkan surface and drawable-size
    # queries. These optional modules otherwise pull GL/EGL implementation
    # sources into the platform library; KMSDRM and offscreen depend on EGL.
    list(APPEND _eng_sdl_options
         "SDL_RENDER OFF"
         "SDL_OPENGL OFF"
         "SDL_OPENGLES OFF"
         "SDL_KMSDRM OFF"
         "SDL_OFFSCREEN OFF")
CPMAddPackage(
    NAME SDL2
    GITHUB_REPOSITORY libsdl-org/SDL
    GIT_TAG release-2.30.11
    OPTIONS ${_eng_sdl_options}
)

# --- Jolt Physics ------------------------------------------------------------
# CMake lives in the Build/ subdir. Options mirror our determinism/perf posture;
# Vulkan compute (debug renderer) pulls in DXC, so it stays off.
cpmaddpackage(
  NAME
  JoltPhysics
  GITHUB_REPOSITORY
  jrouwe/JoltPhysics
  GIT_TAG
  v5.6.0
  SOURCE_SUBDIR
  Build
  OPTIONS
  "DOUBLE_PRECISION OFF"
  "GENERATE_DEBUG_SYMBOLS ON"
  "CROSS_PLATFORM_DETERMINISTIC OFF"
  "INTERPROCEDURAL_OPTIMIZATION OFF"
  "ENABLE_ALL_WARNINGS OFF"
  "DEBUG_RENDERER_IN_DEBUG_AND_RELEASE OFF"
  "DEBUG_RENDERER_IN_DISTRIBUTION OFF"
  "JPH_USE_VK OFF")

# --- toml++ ------------------------------------------------------------------
# Code includes it as <tomlplusplus/toml.hpp> (this repo's historic spelling).
# Upstream ships <toml++/toml.hpp>, so generate a forwarding header into the
# build tree instead of editing every call site.
cpmaddpackage(NAME tomlplusplus GITHUB_REPOSITORY marzer/tomlplusplus GIT_TAG
              v3.4.0)
set(_toml_compat "${CMAKE_BINARY_DIR}/compat-include")
file(WRITE "${_toml_compat}/tomlplusplus/toml.hpp"
     "#pragma once\n#include <toml++/toml.hpp>\n")
add_library(eng_toml INTERFACE)
target_link_libraries(eng_toml INTERFACE tomlplusplus::tomlplusplus)
target_include_directories(eng_toml INTERFACE "${_toml_compat}")

# --- miniaudio (audio playback, single-header) -------------------------------
# Header-only C library; DOWNLOAD_ONLY (it ships no CMake target we want). One
# TU (engine/src/audio/Audio.cpp) defines MINIAUDIO_IMPLEMENTATION.
CPMAddPackage(
    NAME miniaudio
    GITHUB_REPOSITORY mackron/miniaudio
    GIT_TAG 0.11.21
    DOWNLOAD_ONLY YES
)
add_library(miniaudio INTERFACE)
target_include_directories(miniaudio INTERFACE "${miniaudio_SOURCE_DIR}")
# miniaudio's Linux backends need libm/libdl/pthread at link time.
if(UNIX AND NOT APPLE)
    target_link_libraries(miniaudio INTERFACE m dl pthread)
endif()

# --- nlohmann/json (authoring format) ----------------------------------------
# JSON is the source-of-truth format for authored scenes (.scn): human-diffable
# and reviewable, unlike the cooked .map. Header-only, so the build cost is a
# fetch. Tests/install off — we only consume the header.
CPMAddPackage(
    NAME nlohmann_json
    GITHUB_REPOSITORY nlohmann/json
    GIT_TAG v3.11.3
    OPTIONS "JSON_BuildTests OFF" "JSON_Install OFF"
)

# --- Assimp (static model source import) -------------------------------------
# Import-only and deliberately limited to formats used by production DCC and
# interchange workflows. Runtime code copies Assimp's scene into engine-owned
# buffers immediately; no Assimp type crosses the engine API boundary.
#
# BUILD_SHARED_LIBS is a generic Assimp option. Keep it scoped here so Assimp is
# self-contained without changing the SDL linkage selected below.
if(DEFINED BUILD_SHARED_LIBS)
    set(_eng_saved_build_shared_libs "${BUILD_SHARED_LIBS}")
    set(_eng_had_build_shared_libs ON)
else()
    set(_eng_had_build_shared_libs OFF)
endif()
set(BUILD_SHARED_LIBS OFF)
CPMAddPackage(
    NAME assimp
    GITHUB_REPOSITORY assimp/assimp
    GIT_TAG v6.0.5
    OPTIONS
        "ASSIMP_BUILD_TESTS OFF"
        "ASSIMP_BUILD_ASSIMP_TOOLS OFF"
        "ASSIMP_BUILD_SAMPLES OFF"
        "ASSIMP_BUILD_DOCS OFF"
        "ASSIMP_INSTALL OFF"
        "ASSIMP_NO_EXPORT ON"
        "ASSIMP_WARNINGS_AS_ERRORS OFF"
        "ASSIMP_IGNORE_GIT_HASH ON"
        "ASSIMP_BUILD_ZLIB ON"
        "ASSIMP_BUILD_MINIZIP ON"
        "ASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT OFF"
        "ASSIMP_BUILD_GLTF_IMPORTER ON"
        "ASSIMP_BUILD_FBX_IMPORTER ON"
        "ASSIMP_BUILD_OBJ_IMPORTER ON"
        "ASSIMP_BUILD_COLLADA_IMPORTER ON"
        "ASSIMP_BUILD_STL_IMPORTER ON"
        "ASSIMP_BUILD_PLY_IMPORTER ON"
        "ASSIMP_BUILD_3DS_IMPORTER ON"
        "ASSIMP_BUILD_M3D_IMPORTER OFF"
        "ASSIMP_BUILD_USD_IMPORTER OFF"
        "ASSIMP_BUILD_VRML_IMPORTER OFF"
        "ASSIMP_BUILD_DRACO OFF"
)
if(_eng_had_build_shared_libs)
    set(BUILD_SHARED_LIBS "${_eng_saved_build_shared_libs}")
else()
    unset(BUILD_SHARED_LIBS)
endif()
unset(_eng_saved_build_shared_libs)
unset(_eng_had_build_shared_libs)

# --- ozz-animation (skeletal animation runtime + offline cooker) ------------
# 0.16.0 is latest stable release compatible with project's CMake 3.24 floor.
# Runtime ships only ozz_base + ozz_animation. gltf2ozz remains host-side build
# tool used to cook source rigs into compact, version-matched runtime archives.
CPMAddPackage(
    NAME ozz_animation_source
    GITHUB_REPOSITORY guillaumeblanc/ozz-animation
    GIT_TAG 0.16.0
    OPTIONS
        "ozz_build_tools ${ENG_BUILD_ANIMATION_TOOLS}"
        "ozz_build_fbx OFF"
        "ozz_build_gltf ${ENG_BUILD_ANIMATION_TOOLS}"
        "ozz_build_data OFF"
        "ozz_build_samples OFF"
        "ozz_build_howtos OFF"
        "ozz_build_tests OFF"
        "ozz_build_simd_ref OFF"
        "ozz_build_postfix OFF"
)

# --- ImGuizmo (ImGui gizmo widget) -------------------------------------------
# DOWNLOAD_ONLY: fetch the source + expose its include dir. eng compiles
# ${imguizmo_SOURCE_DIR}/src/ImGuizmo.cpp (see CMakeLists) so it draws through
# the engine's own ImGui frame (RenderCore's SDL2/OpenGL3 backend).
CPMAddPackage(
    NAME imguizmo
    GITHUB_REPOSITORY cedricguillemet/ImGuizmo
    GIT_TAG dc25afb98bc3ebe00dfc9a23ba7235fead2ccb1d
    DOWNLOAD_ONLY YES
)
add_library(eng_imguizmo INTERFACE)
target_include_directories(eng_imguizmo INTERFACE "${imguizmo_SOURCE_DIR}/src")

# OGRE is gone. It was the fallback renderer, built from source here, and it
# was by far the heaviest dependency in the tree -- its fetch and build
# dominated a first configure. The Vulkan RHI replaced it; the engine now
# talks to Vulkan through eng_rhi and needs no renderer package at all.
