<div align="center">

<img src="docs/media/branding/logo.png" alt="PSX Game Engine" width="900">


# PSX Game Engine

**A Simple Vulkan Engine with PSX/Retro Aesthetics**

</div>

## Core

- Vulkan 1.3 renderer
- Low-resolution internal rendering
- Vertex snapping and affine-style texture warping
- Dithering, color quantization, fog and palette control
- ECS scene and gameplay layer
- Jolt physics
- Seeded generation and reproducible captures
- Scene editor, material tools and immediate playtesting
- Debug UI, profiling and RenderDoc capture support

## Footage

<p align="center"><img src="assets/preview.gif" alt="Portal room running in the engine" width="900"></p>

<p align="center"><img src="docs/media/engine-demo.gif" alt="Engine rendering demo" width="900"></p>

<p align="center"><img src="docs/media/engine-demo-debug-ui.gif" alt="Engine debug UI" width="900"></p>

<p align="center"><img src="docs/media/scene-editor.gif" alt="Scene editor" width="900"></p>

<p align="center"><img src="docs/media/model-viewer-turntable.gif" alt="Model viewer turntable" width="600"></p>

<p align="center"><img src="docs/media/ac_vexian.gif" alt="Model viewer turntable" width="600"></p>


## Build

```sh
make deps
make run
```

The first build compiles the dependencies, so it will take longer than later builds.

```sh
make editor SCENE=path/to/scene.scn  # edit, then press F5 to playtest
make editor                          # run default scene
make scene SCENE=path/to/scene.scn   # cook and run a scene
make prefab-viewer PREFAB=prefab     # inspect a model
make demo                            # run the renderer demo
make test                            # run the test suite
make help                            # show every target and option
```

## Different Profiles:

```sh
make run PRESET=ps1
make run PRESET=n64
make run PRESET=pixel-3d
make run PRESET=modern-ps1
```

## Content Pipeline

```text
  .scn    source
    |
    x     scene cooker
    |
    v
  .map    runtime data
    |
    v
  game
```

## License

Released under the [GNU GPL v3](LICENSE).
