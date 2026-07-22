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
# Exposed targets after include():
#   glm::glm                 (header-only math)
#   SDL2::SDL2               (windowing/input)
#   Jolt                     (physics)
#   tomlplusplus::tomlplusplus + <tomlplusplus/toml.hpp> compat include
#   OgreMain / OgreOverlay   (renderer; plugins built as shared libs)
#   eng_ogre_plugins         (INTERFACE: forces the runtime plugin .so builds)
#
# OGRE-owned locations, resolved for the source build (were SDK paths before):
#   ENG_OGRE_PLUGIN_DIR      generator-expr dir holding RenderSystem_GL3Plus etc.
#   ENG_OGRE_MEDIA_DIR       OGRE's stock Media/ (shadow-extrude programs, fonts)
# =============================================================================

include(${CMAKE_CURRENT_LIST_DIR}/CPM.cmake)

# --- glm ---------------------------------------------------------------------
CPMAddPackage(
    NAME glm
    GITHUB_REPOSITORY g-truc/glm
    GIT_TAG 1.0.1
)

# --- SDL2 --------------------------------------------------------------------
# Shared by default so we don't statically pull SDL's system backends into our
# binaries; the build tree ships the .so next to the executables.
CPMAddPackage(
    NAME SDL2
    GITHUB_REPOSITORY libsdl-org/SDL
    GIT_TAG release-2.30.11
    OPTIONS
        "SDL2_DISABLE_INSTALL ON"
        "SDL_TEST OFF"
        "SDL_SHARED ON"
        "SDL_STATIC OFF"
        # SDL 2.30.11's pipewire backend does not compile against newer system
        # PipeWire headers (pw_node_enum_params signature drift). We only need
        # SDL for windowing/input; audio still negotiates ALSA/PulseAudio.
        "SDL_PIPEWIRE OFF"
)

# --- Jolt Physics ------------------------------------------------------------
# CMake lives in the Build/ subdir. Options mirror our determinism/perf posture;
# Vulkan compute (debug renderer) pulls in DXC, so it stays off.
CPMAddPackage(
    NAME JoltPhysics
    GITHUB_REPOSITORY jrouwe/JoltPhysics
    GIT_TAG v5.6.0
    SOURCE_SUBDIR Build
    OPTIONS
        "DOUBLE_PRECISION OFF"
        "GENERATE_DEBUG_SYMBOLS ON"
        "CROSS_PLATFORM_DETERMINISTIC OFF"
        "INTERPROCEDURAL_OPTIMIZATION OFF"
        "ENABLE_ALL_WARNINGS OFF"
        "DEBUG_RENDERER_IN_DEBUG_AND_RELEASE OFF"
        "DEBUG_RENDERER_IN_DISTRIBUTION OFF"
        "JPH_USE_VK OFF"
)

# --- toml++ ------------------------------------------------------------------
# Code includes it as <tomlplusplus/toml.hpp> (this repo's historic spelling).
# Upstream ships <toml++/toml.hpp>, so generate a forwarding header into the
# build tree instead of editing every call site.
CPMAddPackage(
    NAME tomlplusplus
    GITHUB_REPOSITORY marzer/tomlplusplus
    GIT_TAG v3.4.0
)
set(_toml_compat "${CMAKE_BINARY_DIR}/compat-include")
file(WRITE "${_toml_compat}/tomlplusplus/toml.hpp"
     "#pragma once\n#include <toml++/toml.hpp>\n")
add_library(eng_toml INTERFACE)
target_link_libraries(eng_toml INTERFACE tomlplusplus::tomlplusplus)
target_include_directories(eng_toml INTERFACE "${_toml_compat}")

# --- OGRE 14 (renderer, built from source) -----------------------------------
# The single heavy dependency. First configure fetches + builds OGRE and its
# bundled deps (freetype/zlib/zziplib/pugixml) — minutes, then cached. We build
# ONLY what the engine loads at runtime: GL3Plus RS, ParticleFX, STBI codec,
# and the imgui-enabled Overlay. Everything else (samples, tools, other RS,
# RTSS, terrain/paging/bullet/assimp) is off to keep the tree lean.
CPMAddPackage(
    NAME OGRE
    GITHUB_REPOSITORY OGRECave/ogre
    GIT_TAG v14.4.1
    OPTIONS
        "OGRE_BUILD_DEPENDENCIES ON"
        "OGRE_STATIC OFF"
        "OGRE_BUILD_SAMPLES OFF"
        "OGRE_BUILD_TOOLS OFF"
        "OGRE_BUILD_TESTS OFF"
        "OGRE_INSTALL_SAMPLES OFF"
        "OGRE_INSTALL_TOOLS OFF"
        "OGRE_INSTALL_DOCS OFF"
        "OGRE_BUILD_RENDERSYSTEM_GL3PLUS ON"
        "OGRE_BUILD_RENDERSYSTEM_GL OFF"
        "OGRE_BUILD_RENDERSYSTEM_GLES2 OFF"
        "OGRE_BUILD_RENDERSYSTEM_VULKAN OFF"
        "OGRE_BUILD_PLUGIN_PFX ON"
        "OGRE_BUILD_PLUGIN_STBI ON"
        "OGRE_BUILD_PLUGIN_DOT_SCENE OFF"
        "OGRE_BUILD_PLUGIN_ASSIMP OFF"
        "OGRE_BUILD_PLUGIN_FREEIMAGE OFF"
        "OGRE_BUILD_COMPONENT_OVERLAY ON"
        "OGRE_BUILD_COMPONENT_OVERLAY_IMGUI ON"
        # Bites is the SDL2-based app framework; the engine drives Ogre::Root
        # programmatically. Building it drags our CPM SDL2 into Ogre's install
        # export set, which breaks generation ("SDL2 not in any export set").
        "OGRE_BUILD_COMPONENT_BITES OFF"
        # Consumed as a subproject, never installed — skip Ogre's install rules.
        "OGRE_INSTALL_PDB OFF"
        "OGRE_BUILD_COMPONENT_RTSHADERSYSTEM OFF"
        "OGRE_BUILD_COMPONENT_TERRAIN OFF"
        "OGRE_BUILD_COMPONENT_PAGING OFF"
        "OGRE_BUILD_COMPONENT_VOLUME OFF"
        "OGRE_BUILD_COMPONENT_MESHLODGENERATOR OFF"
        "OGRE_BUILD_COMPONENT_PROPERTY OFF"
        "OGRE_BUILD_COMPONENT_BULLET OFF"
        "OGRE_BUILD_COMPONENT_PYTHON OFF"
        "OGRE_BUILD_COMPONENT_JAVA OFF"
        "OGRE_BUILD_COMPONENT_CSHARP OFF"
)

# The runtime loads these plugins by path (see RenderCore). They are separate
# CMake targets, not linked into libeng, so nothing would build them otherwise:
# gather them behind one INTERFACE target the engine depends on, and expose
# their output directory (all land together) via a generator expression.
add_library(eng_ogre_plugins INTERFACE)
add_dependencies(eng_ogre_plugins
    RenderSystem_GL3Plus Plugin_ParticleFX Codec_STBI)
set(ENG_OGRE_PLUGIN_DIR "$<TARGET_FILE_DIR:RenderSystem_GL3Plus>")
set(ENG_OGRE_MEDIA_DIR "${OGRE_SOURCE_DIR}/Media")
