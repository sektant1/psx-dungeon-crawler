# 3.24 for $<LINK_LIBRARY:WHOLE_ARCHIVE,...>, used to link the engine layers.
cmake_minimum_required(VERSION 3.24)
project(raven_engine CXX)

include(CTest)
include(CMakeDependentOption)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# This file is the running order and nothing else. Each module below owns one
# part of the build, in the order the parts depend on each other.
#
# `include()` rather than `add_subdirectory()`, deliberately. include() splices
# a module into *this* scope, so every relative source path stays relative to
# the repository root and every target stays visible to every other module with
# no export plumbing. A subdirectory per component would mean rewriting ~1500
# source paths and giving each one its own variable scope, to express a
# structure the layering lint (tools/check_layering.py) already enforces where
# it counts -- in the includes, not in the build files.
#
# The one rule: a module may use targets defined by a module above it, never
# below. That is the same downward-only dependency the engine layers themselves
# follow (see ARCHITECTURE.md).

# Compiler cache, linker choice, LTO, PCH, unity builds, sanitizers, and the
# feature options every module below reads.
include(BuildOptions)

# --- Third-party dependencies ------------------------------------------------
# Every dependency (OGRE, SDL2, glm, Jolt, toml++) is fetched + built from
# source by CPM; see cmake/Dependencies.cmake. No system packages, no vendored
# SDKs. Cache downloads across build trees so a clean build tree is cheap.
set(CPM_SOURCE_CACHE
    "${CMAKE_CURRENT_SOURCE_DIR}/.cache/cpm"
    CACHE PATH "Directory CPM caches fetched dependency sources in")
include(Dependencies)

# The engine: one static library per architectural layer, plus the PCHs.
include(Engine)

# The samples. Above the engine and below everything else -- a sample links only
# the public facade, which is what makes it a test of the public API.
include(Samples)

# The scene/authoring libraries that the game, the editor and the headless tools
# all compile against, so no consumer lists those sources again.
include(Content)

# The placement editor, the asset conditioning pipeline, and the headless CLIs.
include(Editor)

# The game, its asset-cooking custom targets, and the mapgen/sim harnesses.
include(Game)

# Every ctest. Last, because a test may link anything above it.
include(AddTest)
include(Tests)

# Doxygen, if it is installed.
include(Docs)
