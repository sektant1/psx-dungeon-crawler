# RHI backends

`eng/rhi/` is the contract; each subdirectory here is one implementation of it.

| Directory | State | Notes |
|---|---|---|
| `null/` | working | Validates and records, draws nothing. Used by headless tests, and as the reference for what a backend must accept. |
| `gl/` | skeleton | To be written by hand. `createDevice(BackendKind::OpenGL, ...)` returns null and logs until it is. |
| `vulkan/` | skeleton | Same. |

The engine does **not** render through the RHI yet: `RenderCore`/`Renderer`
still drive OGRE directly, which is what draws the game today. The RHI exists
so a backend can be written and plugged in without touching anything above it.
See `docs/design/2026-07-29-rhi-and-module-contracts.md` for the staging.

## Writing a backend

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
