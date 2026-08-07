# Build configuration: how this tree is compiled, not what it compiles.
#
# Everything here is either a speed knob (ccache, linker, LTO, PCH, unity), a
# correctness knob (sanitizers), or a feature switch the modules below read.
# Included first, because every target created afterwards inherits these.


if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE
      Release
      CACHE STRING "Build type" FORCE)
endif()

# --- Build speed: compiler cache -------------------------------------------
# CCACHE_SLOPPINESS is not optional here. ccache refuses to cache *any*
# compilation that uses a precompiled header unless it is told to be sloppy
# about the PCH's #defines and about __DATE__/__TIME__. Every expensive target
# below has a PCH, so without this the cache is bypassed for exactly the files
# that dominate the build: ccache reports them as "Could not use precompiled
# header" and hands them straight to the compiler, recording neither a hit nor
# a miss. Measured on this tree it was 12947 of 21608 uncacheable calls (60%),
# and a no-op rebuild of `game` still cost 326s. No source here uses __DATE__
# or __TIME__ (checked), so time_macros costs nothing.
#
# This is also the *only* place ccache is configured. A dependency that turns
# ccache on for itself can prepend a second one to every rule in the project and
# disable caching wholesale -- see the RULE_LAUNCH_COMPILE backstop at the end of
# Dependencies.cmake.
#
# Verify with `make doctor`, or by hand with `ccache -s -v`: "Could not use
# precompiled header" and "Multiple source files" should both be 0.
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
  set(_eng_ccache "${CMAKE_COMMAND}" -E env
      CCACHE_SLOPPINESS=pch_defines,time_macros "${CCACHE_PROGRAM}")
  set(CMAKE_CXX_COMPILER_LAUNCHER ${_eng_ccache})
  set(CMAKE_C_COMPILER_LAUNCHER ${_eng_ccache})
  message(STATUS "ccache: ${CCACHE_PROGRAM} (sloppiness=pch_defines,time_macros)")
else()
  message(STATUS "ccache: not found (install it; it makes rebuilds near-free)")
endif()

# --- Build speed: linker ----------------------------------------------------
# GNU ld is the slowest part of an incremental build here: every executable
# links the whole-archive engine layers. mold and lld do the same work
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
# imgui, EnTT, toml++ and the standard library headers dominate the
# per-file cost of the engine layers: they are large, templated, and included by
# nearly every translation unit. Precompiling them once per target removes that
# from every file in it.
option(ENABLE_PCH "Precompile the heavy third-party headers" ON)
option(ENG_BUILD_ANIMATION_TOOLS
       "Build offline ozz tools and explicit source-asset cook targets" OFF)

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

# The renderer is the Vulkan RHI, and it is the only renderer. There was an
# ENG_RENDERER switch here while the OGRE/GL3Plus backend was still the
# qualified default; the RHI reached parity, OGRE was removed, and the last
# dead branches behind ENG_RENDERER_RHI went with it. Nothing is conditional
# on a renderer choice any more -- if a second backend is ever added, it goes
# behind eng::rhi::Device, not behind a preprocessor macro.
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
# third-party code): warnings, optional IPO, optional sanitizers. Call for each target.
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

