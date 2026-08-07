# RHI backends

`eng/rhi/` is the contract; each subdirectory here is one implementation of it.

| Directory | State | Notes |
|---|---|---|
| `null/` | working | Validates and records, draws nothing. Used by headless tests, and as the reference for what a backend must accept. |
| `gl/` | skeleton | To be written by hand. `createDevice(BackendKind::OpenGL, ...)` returns null and logs until it is. |
| `vulkan/` | shipping | Vulkan 1.3, built by default (`ENG_RHI_VULKAN=ON`). **This is what draws the game.** |

The engine renders **through** the RHI: `RenderCore` owns an `rhi::Device` and
every pass above it records into an `rhi::CommandList`. There is no longer a
second, non-RHI path — OGRE was removed, and `cmake/BuildOptions.cmake` keeps
`ENG_RENDERER_RHI` defined only for the sources that still branch on it.
See `docs/design/2026-07-29-rhi-and-module-contracts.md` for how it was staged.

## Writing a backend

For Vulkan specifically, `docs/vulkan-impl-survival-kit/index.html` is the
staged plan, the reading list, and the register of things in this engine that
will bite — read it before step 1.

1. Implement `eng::rhi::Device` and `eng::rhi::CommandList` in your directory.
2. Fill in `capabilities()` honestly -- the renderer above degrades on what it
   reports, and `maxSimultaneousLights` in particular must be at least 16 or
   the PSX lighting path silently truncates its light list.
3. Add your source files to the `eng_rhi` target in `CMakeLists.txt` and return
   your device from `createDevice` in `Registry.cpp`.
4. Handles are generational. A destroyed handle that comes back must be
   detected and logged, never dereferenced.
5. `rhi_contract_tests` runs the same sequence against every backend that
   reports itself as creatable. Make it pass before wiring anything above.

## Fixed binding ABI

The public slots have a fixed Vulkan mapping: `bindUniformBuffer(N, ...)` writes
set 0/binding N and `bindTexture(N, ...)` writes set 1/binding N. Callers must
explicitly bind every set their shader uses in each pass; descriptor state is
not inherited as part of the RHI pass contract. The limits are reported through
`DeviceCapabilities` without exposing Vulkan types.
