# Engine Scaffold Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Restructure ogre-psx-demo into an engine static lib (`eng`, Ogre/SDL fully hidden), a `game` FPS test-room exe, and the demo ported to the public API as `samples/psx-demo`.

**Architecture:** `eng` static lib exposes handle-based public headers in `engine/include/eng/` (GLM math, no Ogre/SDL includes); all Ogre/SDL code lives in `engine/src/`. The existing demo Renderer becomes internal `RenderCore`; public `eng::Renderer` wraps it with node/mesh handle tables; `eng::Engine` facade owns SDL window, event pump, input, config, clock, and the `PSX_SCREENSHOT` hook. Demo is kept building at every task (against internals during transition, ported to the public API at the end).

**Tech Stack:** C++17, CMake ≥ 3.16, system OGRE 14.x (GL3Plus, ParticleFX, STBI), SDL2, GLM (system), toml++ v3.4.0 (vendored header).

**Spec:** `docs/superpowers/specs/2026-07-13-engine-scaffold-design.md`

**Verification model:** No unit-test framework (per spec). Every task ends with a build + run check; renderer regressions checked via the `PSX_SCREENSHOT` hook. The orbit camera is wall-clock driven, so screenshots are compared by eye, not pixel-diff.

**Note on spec's Time module:** folded into `Engine::tick()` (steady_clock dt, clamped 0.1 s) — no separate file, YAGNI.

---

## File structure (end state)

```
CMakeLists.txt, Makefile, .gitignore, README.md
engine/include/eng/   Log.h Config.h Input.h Handles.h Math.h Renderer.h Engine.h
engine/src/           RenderCore.{h,cpp} ObjLoader.{h,cpp} ProceduralMeshes.{h,cpp}
                      Renderer.cpp Log.cpp Config.cpp Input.cpp Platform.{h,cpp} Engine.cpp
engine/assets/        shaders/ programs/ compositors/ materials/psx.material (wrappers+DitherPost)
                      textures/psxdither.png
samples/psx-demo/src/ main.cpp
samples/psx-demo/assets/  demo.toml materials/demo.material meshes/ textures/ particles/
game/src/             main.cpp FpsController.{h,cpp}
game/assets/          game.toml materials/game.material textures/floor.png
third_party/tomlplusplus/toml.hpp
tools/gltf_to_obj.py
```

---

### Task 1: Restructure repo, `eng` lib target, demo keeps building

**Files:**
- Move: everything under `src/` and `assets/` (see commands)
- Modify: `CMakeLists.txt`, `Makefile`, `.gitignore`, `engine/src/RenderCore.{h,cpp}`, `samples/psx-demo/src/main.cpp`
- Create: `engine/assets/materials/psx.material` (trimmed), `samples/psx-demo/assets/materials/demo.material`

- [ ] **Step 1: Capture pre-refactor baseline screenshot**

```bash
make run BUILD_DIR=build 2>/dev/null || make build
cd build && SDL_VIDEODRIVER=x11 PSX_SCREENSHOT=/tmp/psx-baseline.png ./psx_demo && cd ..
ls -l /tmp/psx-baseline.png
```
Expected: PNG exists. Keep it for later eyeball comparison.

- [ ] **Step 2: Move files with git mv**

```bash
mkdir -p engine/src engine/include/eng engine/assets/textures \
         samples/psx-demo/src samples/psx-demo/assets/textures \
         game/src game/assets third_party/tomlplusplus
git mv assets/shaders engine/assets/shaders
git mv assets/programs engine/assets/programs
git mv assets/compositors engine/assets/compositors
git mv assets/materials engine/assets/materials
git mv assets/particles samples/psx-demo/assets/particles
git mv assets/meshes samples/psx-demo/assets/meshes
git mv assets/textures/psxdither.png engine/assets/textures/psxdither.png
git mv assets/textures/floor.png assets/textures/metal-tex.png \
       assets/textures/sparkle.png assets/textures/shadow.png \
       assets/textures/Prototype_orange_32x32px.png \
       samples/psx-demo/assets/textures/
rmdir assets/textures assets
git mv src/Renderer.h engine/src/RenderCore.h
git mv src/Renderer.cpp engine/src/RenderCore.cpp
git mv src/ObjLoader.h src/ObjLoader.cpp engine/src/
git mv src/ProceduralMeshes.h src/ProceduralMeshes.cpp engine/src/
git mv src/main.cpp src/Animation.h samples/psx-demo/src/
rmdir src
```

- [ ] **Step 3: Split psx.material**

Create `samples/psx-demo/assets/materials/demo.material` and MOVE into it these material blocks from `engine/assets/materials/psx.material` (cut verbatim, including their comments): `PSX/Floor`, `PSX/BoxMetal`, `PSX/BoxLit`, `PSX/CrystalSpire`, `PSX/CrystalGround`, `PSX/Sparkle`, `PSX/Shadow`, `PSX/LightShaft` (the whole "Scene materials" section). Keep in `engine/assets/materials/psx.material`: the nine wrapper materials (`PSX/Lit` … `PSX/LightVolume`) and `PSX/DitherPost`. Ogre parses `.program` scripts before `.material` scripts, so the split is parse-order safe; program refs (not inheritance) are used throughout.

- [ ] **Step 4: Rename internal Renderer → RenderCore, extend init**

`engine/src/RenderCore.h` (replaces old Renderer.h):

```cpp
#pragma once
#include <cstdint>
#include <string>

namespace Ogre {
class Root;
class RenderWindow;
class SceneManager;
class Camera;
class Viewport;
} // namespace Ogre

namespace eng {

// Internal: owns Ogre::Root and hides Ogre lifetime rules. Root is the first
// Ogre object created and the last destroyed.
class RenderCore
{
public:
    bool init(uintptr_t nativeWindowHandle, int width, int height,
              const std::string& title, const std::string& appAssetDir);
    void setDitherEnabled(bool enabled);
    void renderFrame(float dt);
    void onResize(int width, int height);
    void writeScreenshot(const std::string& path);
    void shutdown();

    Ogre::SceneManager* sceneMgr() const { return mSceneMgr; }
    Ogre::Camera* camera() const { return mCamera; }
    Ogre::Viewport* viewport() const { return mViewport; }

private:
    Ogre::Root* mRoot = nullptr;
    Ogre::RenderWindow* mWindow = nullptr;
    Ogre::SceneManager* mSceneMgr = nullptr;
    Ogre::Camera* mCamera = nullptr;
    Ogre::Viewport* mViewport = nullptr;
    bool mDitherAdded = false;
};

} // namespace eng
```

`engine/src/RenderCore.cpp` (replaces old Renderer.cpp):

```cpp
#include "RenderCore.h"

#include <Ogre.h>
#include <OgreCompositorManager.h>

#include <filesystem>
#include <string>

namespace eng {

bool RenderCore::init(uintptr_t nativeWindowHandle, int width, int height,
                      const std::string& title, const std::string& appAssetDir)
{
    // Fully programmatic setup: no plugins.cfg / ogre.cfg, no RTSS -- all
    // materials are hand-written GLSL.
    mRoot = new Ogre::Root("", "", "ogre.log");
    mRoot->loadPlugin(std::string(OGRE_PLUGIN_DIR) + "/RenderSystem_GL3Plus");
    mRoot->loadPlugin(std::string(OGRE_PLUGIN_DIR) + "/Plugin_ParticleFX");
    mRoot->loadPlugin(std::string(OGRE_PLUGIN_DIR) + "/Codec_STBI"); // PNG
    mRoot->setRenderSystem(mRoot->getAvailableRenderers().front());
    mRoot->initialise(false); // the engine owns the window

    Ogre::NameValuePairList params;
    params["externalWindowHandle"] = std::to_string(nativeWindowHandle);
    params["vsync"] = "true";
    mWindow = mRoot->createRenderWindow(title, width, height, false, &params);
    mSceneMgr = mRoot->createSceneManager();

    // Resource locations AFTER the render window exists (material parsing
    // needs a live render system). Engine-owned PSX stack + app assets.
    auto& rgm = Ogre::ResourceGroupManager::getSingleton();
    const std::string engBase = ENG_ASSET_DIR;
    for (const char* sub : {"/shaders", "/programs", "/materials",
                            "/compositors", "/textures"})
        rgm.addResourceLocation(engBase + sub, "FileSystem", "General");
    for (const char* sub : {"/materials", "/textures", "/particles"}) {
        const std::string dir = appAssetDir + sub;
        if (std::filesystem::is_directory(dir))
            rgm.addResourceLocation(dir, "FileSystem", "General");
    }
    rgm.initialiseAllResourceGroups();

    mCamera = mSceneMgr->createCamera("MainCamera");
    mCamera->setFOVy(Ogre::Degree(70.0f)); // defaults; app overrides via API
    mCamera->setNearClipDistance(0.05f);
    mCamera->setFarClipDistance(4000.0f);
    mCamera->setAutoAspectRatio(true);

    mViewport = mWindow->addViewport(mCamera);
    mViewport->setBackgroundColour(Ogre::ColourValue::Black);
    return true;
}

void RenderCore::setDitherEnabled(bool enabled)
{
    auto& cm = Ogre::CompositorManager::getSingleton();
    if (enabled && !mDitherAdded) {
        cm.addCompositor(mViewport, "PSX/Dither");
        mDitherAdded = true;
    }
    if (mDitherAdded)
        cm.setCompositorEnabled(mViewport, "PSX/Dither", enabled);
}

void RenderCore::renderFrame(float dt) { mRoot->renderOneFrame(dt); }

void RenderCore::onResize(int width, int height)
{
    if (!mWindow)
        return;
    mWindow->resize(width, height);
    mWindow->windowMovedOrResized();
}

void RenderCore::writeScreenshot(const std::string& path)
{
    if (mWindow)
        mWindow->writeContentsToFile(path);
}

void RenderCore::shutdown()
{
    if (!mRoot)
        return;
    if (mViewport && mWindow)
        Ogre::CompositorManager::getSingleton().removeCompositorChain(mViewport);
    if (mSceneMgr)
        mRoot->destroySceneManager(mSceneMgr);
    delete mRoot; // last: tears down window, render system, resource managers
    mRoot = nullptr;
    mWindow = nullptr;
    mSceneMgr = nullptr;
    mCamera = nullptr;
    mViewport = nullptr;
}

} // namespace eng
```

- [ ] **Step 5: Patch demo main.cpp for the transition (still raw Ogre)**

In `samples/psx-demo/src/main.cpp`:
- `#include "Renderer.h"` → `#include "RenderCore.h"`
- `Renderer renderer;` → `eng::RenderCore renderer;`
- `renderer.init(handle, 960, 720)` → `renderer.init(handle, 960, 720, "ogre-psx-demo", APP_ASSET_DIR)`
- every `ASSET_DIR` → `APP_ASSET_DIR`
- after init, restore the demo camera/background (defaults changed):

```cpp
    renderer.camera()->setFOVy(Ogre::Degree(68.1243f));
    renderer.viewport()->setBackgroundColour(
        Ogre::ColourValue(0.670588f, 0.760784f, 1.0f));
```

- `renderer.enableDitherCompositor()` → `renderer.setDitherEnabled(true)`

- [ ] **Step 6: New CMakeLists.txt and Makefile**

`CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.16)
project(dungeon_crawler CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OGRE 14 REQUIRED)
find_package(SDL2 REQUIRED)

add_library(eng STATIC
    engine/src/RenderCore.cpp
    engine/src/ObjLoader.cpp
    engine/src/ProceduralMeshes.cpp
)
target_link_libraries(eng PRIVATE OgreMain SDL2::SDL2)
target_compile_definitions(eng PRIVATE
    ENG_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/engine/assets"
    OGRE_PLUGIN_DIR="${OGRE_PLUGIN_DIR}"
)

add_executable(psx_demo samples/psx-demo/src/main.cpp)
# Transitional: demo uses engine internals until ported to the public API.
target_include_directories(psx_demo PRIVATE engine/src)
target_link_libraries(psx_demo PRIVATE eng OgreMain SDL2::SDL2)
target_compile_definitions(psx_demo PRIVATE
    APP_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/samples/psx-demo/assets"
)
```

`Makefile`:

```make
# Convenience wrapper around the CMake build.
#   make            - configure (if needed) + build all targets
#   make run-demo   - build then run the PSX sample (forces X11 under Wayland)
#   make run-game   - build then run the game
#   make debug      - Debug-type build in build-debug/
#   make clean      - remove build directories

BUILD_DIR   ?= build
BUILD_TYPE  ?= Release
JOBS        ?= $(shell nproc)

.PHONY: all build run-demo run-game debug clean

all: build

build:
	cmake -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE)
	cmake --build $(BUILD_DIR) -j$(JOBS)

run-demo: build
	cd $(BUILD_DIR) && SDL_VIDEODRIVER=x11 ./psx_demo

run-game: build
	cd $(BUILD_DIR) && SDL_VIDEODRIVER=x11 ./game

debug:
	$(MAKE) build BUILD_DIR=build-debug BUILD_TYPE=Debug

clean:
	rm -rf $(BUILD_DIR) build-debug
```

(`run-game` will fail until Task 7 — fine.)

Append `build-debug/` to `.gitignore`.

- [ ] **Step 7: Build and verify demo unchanged**

```bash
rm -rf build && make build
cd build && SDL_VIDEODRIVER=x11 PSX_SCREENSHOT=/tmp/psx-task1.png ./psx_demo && cd ..
```
Expected: builds clean; `/tmp/psx-task1.png` visually matches `/tmp/psx-baseline.png`.

- [ ] **Step 8: Commit**

```bash
git add -A
git commit -m "refactor: restructure into engine lib + samples layout

Renderer becomes internal eng::RenderCore; assets split into
engine-owned PSX stack and demo-owned scene assets. Demo still
builds against engine internals until the public API lands."
```

---

### Task 2: Vendor toml++, Log, Config

**Files:**
- Create: `third_party/tomlplusplus/toml.hpp`, `engine/include/eng/Log.h`, `engine/src/Log.cpp`, `engine/include/eng/Config.h`, `engine/src/Config.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Vendor toml++ v3.4.0**

```bash
curl -fsSL -o third_party/tomlplusplus/toml.hpp \
  https://raw.githubusercontent.com/marzer/tomlplusplus/v3.4.0/toml.hpp
head -5 third_party/tomlplusplus/toml.hpp
```
Expected: header with toml++ banner comment.

- [ ] **Step 2: Log**

`engine/include/eng/Log.h`:

```cpp
#pragma once

// printf-style logging to stderr. fatal() logs and aborts -- used for
// programmer errors (invalid handle, unknown material); no exceptions
// cross the public API boundary.
namespace eng::log {
void info(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void warn(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void error(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
[[noreturn]] void fatal(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
} // namespace eng::log
```

`engine/src/Log.cpp`:

```cpp
#include <eng/Log.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>

namespace eng::log {

namespace {
void write(const char* level, const char* fmt, va_list ap)
{
    std::fprintf(stderr, "[%s] ", level);
    std::vfprintf(stderr, fmt, ap);
    std::fputc('\n', stderr);
}
} // namespace

#define ENG_LOG_BODY(level)                                                    \
    va_list ap;                                                                \
    va_start(ap, fmt);                                                         \
    write(level, fmt, ap);                                                     \
    va_end(ap)

void info(const char* fmt, ...) { ENG_LOG_BODY("info"); }
void warn(const char* fmt, ...) { ENG_LOG_BODY("warn"); }
void error(const char* fmt, ...) { ENG_LOG_BODY("error"); }
void fatal(const char* fmt, ...)
{
    ENG_LOG_BODY("fatal");
    std::abort();
}

} // namespace eng::log
```

- [ ] **Step 3: Config**

`engine/include/eng/Config.h`:

```cpp
#pragma once
#include <map>
#include <string>
#include <vector>

namespace eng {

// TOML config, flattened to dotted "section.key" leaves. The [bindings]
// table is kept separately: action -> list of SDL key names (a binding
// value may be a string or an array of strings).
class Config
{
public:
    bool load(const std::string& path);

    std::string getString(const std::string& key, const std::string& def = {}) const;
    double getNumber(const std::string& key, double def = 0.0) const;
    bool getBool(const std::string& key, bool def = false) const;

    const std::map<std::string, std::vector<std::string>>& bindings() const
    {
        return mBindings;
    }

private:
    std::map<std::string, std::string> mStrings;
    std::map<std::string, double> mNumbers;
    std::map<std::string, bool> mBools;
    std::map<std::string, std::vector<std::string>> mBindings;
};

} // namespace eng
```

`engine/src/Config.cpp`:

```cpp
#include <eng/Config.h>
#include <eng/Log.h>

#define TOML_EXCEPTIONS 0
#include <tomlplusplus/toml.hpp>

namespace eng {

bool Config::load(const std::string& path)
{
    toml::parse_result result = toml::parse_file(path);
    if (!result) {
        log::error("Config: failed to parse %s: %s", path.c_str(),
                   std::string(result.error().description()).c_str());
        return false;
    }

    auto storeLeaf = [this](const std::string& key, const toml::node& n) {
        if (auto s = n.as_string())
            mStrings[key] = s->get();
        else if (auto f = n.as_floating_point())
            mNumbers[key] = f->get();
        else if (auto i = n.as_integer())
            mNumbers[key] = static_cast<double>(i->get());
        else if (auto b = n.as_boolean())
            mBools[key] = b->get();
        else
            log::warn("Config: unsupported value type for key '%s'", key.c_str());
    };

    for (auto&& [k, v] : result.table()) {
        const std::string key(k.str());
        if (key == "bindings") {
            const toml::table* tbl = v.as_table();
            if (!tbl)
                continue;
            for (auto&& [bk, bv] : *tbl) {
                std::vector<std::string>& keys = mBindings[std::string(bk.str())];
                if (auto s = bv.as_string())
                    keys.push_back(s->get());
                else if (auto arr = bv.as_array())
                    for (auto&& e : *arr)
                        if (auto es = e.as_string())
                            keys.push_back(es->get());
            }
        } else if (const toml::table* tbl = v.as_table()) {
            for (auto&& [sk, sv] : *tbl)
                storeLeaf(key + "." + std::string(sk.str()), sv);
        } else {
            storeLeaf(key, v);
        }
    }
    return true;
}

std::string Config::getString(const std::string& key, const std::string& def) const
{
    auto it = mStrings.find(key);
    return it != mStrings.end() ? it->second : def;
}

double Config::getNumber(const std::string& key, double def) const
{
    auto it = mNumbers.find(key);
    return it != mNumbers.end() ? it->second : def;
}

bool Config::getBool(const std::string& key, bool def) const
{
    auto it = mBools.find(key);
    return it != mBools.end() ? it->second : def;
}

} // namespace eng
```

- [ ] **Step 4: Wire into CMake**

In `CMakeLists.txt` add `find_package(glm REQUIRED)` after SDL2, add the new sources, and export the public include dir:

```cmake
add_library(eng STATIC
    engine/src/RenderCore.cpp
    engine/src/ObjLoader.cpp
    engine/src/ProceduralMeshes.cpp
    engine/src/Log.cpp
    engine/src/Config.cpp
)
target_include_directories(eng
    PUBLIC engine/include
    PRIVATE third_party
)
target_link_libraries(eng PUBLIC glm::glm PRIVATE OgreMain SDL2::SDL2)
```

- [ ] **Step 5: Build + commit**

```bash
make build
```
Expected: compiles (Config.cpp is the slow one — toml++ is header-heavy).

```bash
git add -A
git commit -m "feat(engine): Log and TOML Config modules, vendor toml++ 3.4.0"
```

---

### Task 3: Public renderer API (Handles, Math, eng::Renderer)

**Files:**
- Create: `engine/include/eng/Handles.h`, `engine/include/eng/Math.h`, `engine/include/eng/Renderer.h`, `engine/src/Renderer.cpp`
- Modify: `CMakeLists.txt` (add `engine/src/Renderer.cpp`)

- [ ] **Step 1: Handles and Math headers**

`engine/include/eng/Handles.h`:

```cpp
#pragma once
#include <cstdint>

namespace eng {

struct MeshHandle {
    uint32_t id = 0;
    bool valid() const { return id != 0; }
};

struct NodeHandle {
    uint32_t id = 0;
    bool valid() const { return id != 0; }
};

// The scene root, valid after Engine::init.
inline constexpr NodeHandle kRootNode{1};

} // namespace eng
```

`engine/include/eng/Math.h`:

```cpp
#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace eng {

// a..i are the ROWS of a pure-rotation basis, e.g. the Godot .tscn
// Transform3D value order. glm::mat3 takes columns, hence the transpose.
inline glm::quat quatFromBasisRows(float a, float b, float c, float d, float e,
                                   float f, float g, float h, float i)
{
    return glm::quat_cast(glm::mat3(a, d, g, b, e, h, c, f, i));
}

} // namespace eng
```

- [ ] **Step 2: Renderer public header**

`engine/include/eng/Renderer.h`:

```cpp
#pragma once
#include <eng/Handles.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <memory>
#include <string>

namespace eng {

class RenderCore; // internal; forward-declared only, no Ogre leak
class Renderer;

namespace detail {
// Engine-only backdoor to the internal core (defined in Renderer.cpp).
RenderCore& coreOf(Renderer& r);
void registerRoot(Renderer& r);
} // namespace detail

struct LightDesc {
    enum class Type { Directional, Point };
    Type type = Type::Point;
    glm::vec3 colour{1.0f}; // linear, energy pre-multiplied by the caller
    float range = 3.0f;     // point lights only
};

// Public renderer facade. All Ogre types stay inside engine/src.
// Colour convention: shading runs in linear space; callers linearise
// sRGB-picked colours themselves (pow 2.2), as the PSX shaders expect.
class Renderer
{
public:
    // --- meshes -----------------------------------------------------------
    // bake, when given, is multiplied into vertex positions (normals get
    // its inverse-transpose) -- for transforms TRS nodes can't represent.
    MeshHandle loadObj(const std::string& path, const glm::mat4* bake = nullptr);
    MeshHandle createInteriorBox(float size, int subdivide);
    MeshHandle createPlane(float size);

    // --- scene graph ------------------------------------------------------
    NodeHandle createNode(NodeHandle parent, glm::vec3 position = glm::vec3(0.0f));
    void setPosition(NodeHandle node, glm::vec3 position);
    void setOrientation(NodeHandle node, glm::quat orientation);
    void setScale(NodeHandle node, glm::vec3 scale);

    // --- attachments ------------------------------------------------------
    void attachMesh(NodeHandle node, MeshHandle mesh, const std::string& materialName);
    void attachParticles(NodeHandle node, const std::string& templateName);
    void attachCamera(NodeHandle node); // moves the single camera to this node
    void attachLight(NodeHandle node, const LightDesc& desc);

    // --- camera -----------------------------------------------------------
    void setCameraFov(float degrees); // vertical FOV
    void setCameraClip(float nearDist, float farDist);

    // --- materials --------------------------------------------------------
    void setMaterialParam(const std::string& materialName,
                          const std::string& paramName, float value);
    void setMaterialParam(const std::string& materialName,
                          const std::string& paramName, glm::vec2 value);
    void setMaterialParam(const std::string& materialName,
                          const std::string& paramName, glm::vec3 value);
    void setMaterialParam(const std::string& materialName,
                          const std::string& paramName, glm::vec4 value);

    // --- environment ------------------------------------------------------
    void setAmbient(glm::vec3 colour);
    void setFog(glm::vec3 colour, float expDensity);
    void setBackground(glm::vec3 colour);

    // --- post + verification ---------------------------------------------
    void setDitherEnabled(bool enabled);
    void writeScreenshot(const std::string& path);

private:
    friend class Engine; // Engine constructs, initialises, and drives it
    friend RenderCore& detail::coreOf(Renderer&);
    friend void detail::registerRoot(Renderer&);
    Renderer();
    ~Renderer();
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};

} // namespace eng
```

- [ ] **Step 3: Renderer implementation**

`engine/src/Renderer.cpp`:

```cpp
#include <eng/Renderer.h>

#include <eng/Log.h>

#include "ObjLoader.h"
#include "ProceduralMeshes.h"
#include "RenderCore.h"

#include <Ogre.h>

#include <vector>

namespace eng {

namespace {

Ogre::Vector3 toOgre(glm::vec3 v) { return {v.x, v.y, v.z}; }
Ogre::Quaternion toOgre(glm::quat q) { return {q.w, q.x, q.y, q.z}; }
Ogre::ColourValue toColour(glm::vec3 c) { return {c.x, c.y, c.z}; }

Ogre::Matrix4 toOgre(const glm::mat4& m) // glm column-major -> Ogre row-major
{
    Ogre::Matrix4 o;
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            o[r][c] = m[c][r];
    return o;
}

} // namespace

struct Renderer::Impl {
    RenderCore core;
    std::vector<Ogre::SceneNode*> nodes; // nodes[id-1]; id 1 == scene root
    std::vector<std::string> meshNames;  // meshNames[id-1]
    int nameCounter = 0;

    Ogre::SceneNode* node(NodeHandle h, const char* what)
    {
        if (!h.valid() || h.id > nodes.size())
            log::fatal("Renderer: invalid node handle %u in %s", h.id, what);
        return nodes[h.id - 1];
    }
    const std::string& mesh(MeshHandle h, const char* what)
    {
        if (!h.valid() || h.id > meshNames.size())
            log::fatal("Renderer: invalid mesh handle %u in %s", h.id, what);
        return meshNames[h.id - 1];
    }
    MeshHandle registerMesh(std::string name)
    {
        meshNames.push_back(std::move(name));
        return {static_cast<uint32_t>(meshNames.size())};
    }
    std::string nextName(const char* prefix)
    {
        return std::string(prefix) + std::to_string(++nameCounter);
    }
};

Renderer::Renderer() : mImpl(new Impl) {}
Renderer::~Renderer() = default;

MeshHandle Renderer::loadObj(const std::string& path, const glm::mat4* bake)
{
    const std::string name = mImpl->nextName("mesh");
    try {
        ObjLoader::load(path, name,
                        bake ? toOgre(*bake) : Ogre::Matrix4::IDENTITY);
    } catch (const std::exception& e) {
        log::fatal("Renderer: loadObj('%s') failed: %s", path.c_str(), e.what());
    }
    return mImpl->registerMesh(name);
}

MeshHandle Renderer::createInteriorBox(float size, int subdivide)
{
    const std::string name = mImpl->nextName("mesh");
    ProceduralMeshes::createInteriorBox(name, size, subdivide);
    return mImpl->registerMesh(name);
}

MeshHandle Renderer::createPlane(float size)
{
    const std::string name = mImpl->nextName("mesh");
    ProceduralMeshes::createPlane(name, size);
    return mImpl->registerMesh(name);
}

NodeHandle Renderer::createNode(NodeHandle parent, glm::vec3 position)
{
    Ogre::SceneNode* n =
        mImpl->node(parent, "createNode")->createChildSceneNode(toOgre(position));
    mImpl->nodes.push_back(n);
    return {static_cast<uint32_t>(mImpl->nodes.size())};
}

void Renderer::setPosition(NodeHandle node, glm::vec3 position)
{
    mImpl->node(node, "setPosition")->setPosition(toOgre(position));
}

void Renderer::setOrientation(NodeHandle node, glm::quat orientation)
{
    mImpl->node(node, "setOrientation")->setOrientation(toOgre(orientation));
}

void Renderer::setScale(NodeHandle node, glm::vec3 scale)
{
    mImpl->node(node, "setScale")->setScale(toOgre(scale));
}

void Renderer::attachMesh(NodeHandle node, MeshHandle mesh,
                          const std::string& materialName)
{
    if (!Ogre::MaterialManager::getSingleton().getByName(materialName))
        log::fatal("Renderer: unknown material '%s'", materialName.c_str());
    Ogre::Entity* e =
        mImpl->core.sceneMgr()->createEntity(mImpl->mesh(mesh, "attachMesh"));
    e->setMaterialName(materialName);
    mImpl->node(node, "attachMesh")->attachObject(e);
}

void Renderer::attachParticles(NodeHandle node, const std::string& templateName)
{
    try {
        Ogre::ParticleSystem* ps = mImpl->core.sceneMgr()->createParticleSystem(
            mImpl->nextName("particles"), templateName);
        mImpl->node(node, "attachParticles")->attachObject(ps);
    } catch (const std::exception& e) {
        log::fatal("Renderer: attachParticles('%s') failed: %s",
                   templateName.c_str(), e.what());
    }
}

void Renderer::attachCamera(NodeHandle node)
{
    Ogre::Camera* cam = mImpl->core.camera();
    if (cam->getParentSceneNode())
        cam->detachFromParent();
    mImpl->node(node, "attachCamera")->attachObject(cam);
}

void Renderer::attachLight(NodeHandle node, const LightDesc& desc)
{
    Ogre::Light* l = mImpl->core.sceneMgr()->createLight();
    l->setType(desc.type == LightDesc::Type::Directional
                   ? Ogre::Light::LT_DIRECTIONAL
                   : Ogre::Light::LT_POINT);
    l->setDiffuseColour(toColour(desc.colour));
    l->setSpecularColour(Ogre::ColourValue::Black); // PSX: specular_disabled
    if (desc.type == LightDesc::Type::Point)
        l->setAttenuation(desc.range, 1.0f, 0.0f, 0.0f); // shader reads range only
    mImpl->node(node, "attachLight")->attachObject(l);
}

void Renderer::setCameraFov(float degrees)
{
    mImpl->core.camera()->setFOVy(Ogre::Degree(degrees));
}

void Renderer::setCameraClip(float nearDist, float farDist)
{
    mImpl->core.camera()->setNearClipDistance(nearDist);
    mImpl->core.camera()->setFarClipDistance(farDist);
}

namespace {
// Applies `set` to every VS/FS param set that defines paramName.
template <typename SetFn>
void applyMaterialParam(const std::string& materialName,
                        const std::string& paramName, SetFn&& set)
{
    Ogre::MaterialPtr mat =
        Ogre::MaterialManager::getSingleton().getByName(materialName);
    if (!mat)
        log::fatal("Renderer: unknown material '%s'", materialName.c_str());
    bool found = false;
    for (Ogre::Technique* tech : mat->getTechniques()) {
        for (Ogre::Pass* pass : tech->getPasses()) {
            Ogre::GpuProgramParametersSharedPtr sets[2];
            if (pass->hasVertexProgram())
                sets[0] = pass->getVertexProgramParameters();
            if (pass->hasFragmentProgram())
                sets[1] = pass->getFragmentProgramParameters();
            for (auto& params : sets) {
                if (params && params->_findNamedConstantDefinition(paramName, false)) {
                    set(params);
                    found = true;
                }
            }
        }
    }
    if (!found)
        log::fatal("Renderer: material '%s' has no param '%s'",
                   materialName.c_str(), paramName.c_str());
}
} // namespace

void Renderer::setMaterialParam(const std::string& m, const std::string& p, float v)
{
    applyMaterialParam(m, p, [&](auto& params) { params->setNamedConstant(p, v); });
}
void Renderer::setMaterialParam(const std::string& m, const std::string& p, glm::vec2 v)
{
    applyMaterialParam(m, p, [&](auto& params) {
        params->setNamedConstant(p, Ogre::Vector2(v.x, v.y));
    });
}
void Renderer::setMaterialParam(const std::string& m, const std::string& p, glm::vec3 v)
{
    applyMaterialParam(m, p, [&](auto& params) {
        params->setNamedConstant(p, Ogre::Vector3(v.x, v.y, v.z));
    });
}
void Renderer::setMaterialParam(const std::string& m, const std::string& p, glm::vec4 v)
{
    applyMaterialParam(m, p, [&](auto& params) {
        params->setNamedConstant(p, Ogre::Vector4(v.x, v.y, v.z, v.w));
    });
}

void Renderer::setAmbient(glm::vec3 colour)
{
    mImpl->core.sceneMgr()->setAmbientLight(toColour(colour));
}

void Renderer::setFog(glm::vec3 colour, float expDensity)
{
    mImpl->core.sceneMgr()->setFog(Ogre::FOG_EXP, toColour(colour), expDensity);
}

void Renderer::setBackground(glm::vec3 colour)
{
    mImpl->core.viewport()->setBackgroundColour(toColour(colour));
}

void Renderer::setDitherEnabled(bool enabled)
{
    mImpl->core.setDitherEnabled(enabled);
}

void Renderer::writeScreenshot(const std::string& path)
{
    mImpl->core.writeScreenshot(path);
}

} // namespace eng
```

- [ ] **Step 4: Add to CMake, build, commit**

Add `engine/src/Renderer.cpp` to the `eng` sources.

```bash
make build
git add -A
git commit -m "feat(engine): public handle-based renderer API over RenderCore"
```

---

### Task 4: Input module

**Files:**
- Create: `engine/include/eng/Input.h`, `engine/src/Input.cpp`
- Modify: `CMakeLists.txt` (add `engine/src/Input.cpp`)

- [ ] **Step 1: Public header**

`engine/include/eng/Input.h`:

```cpp
#pragma once
#include <glm/vec2.hpp>

#include <memory>
#include <string>

namespace eng {

class Config;

// Action-mapped input. Bindings come from the [bindings] TOML table:
// action name -> SDL key name string (or array of them).
class Input
{
public:
    Input();
    ~Input();

    bool loadBindings(const Config& cfg); // false on unknown key name

    bool isDown(const std::string& action) const;
    bool wasPressed(const std::string& action) const; // edge, cleared each tick
    bool wasMouseClicked() const;                     // left-button edge
    glm::vec2 mouseDelta() const;                     // relative, this tick

    void setMouseGrab(bool grab); // relative mouse mode + cursor capture
    bool mouseGrabbed() const;

private:
    friend class Engine; // feeds SDL events, begins ticks
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};

} // namespace eng
```

- [ ] **Step 2: Implementation**

`engine/src/Input.cpp`:

```cpp
#include <eng/Input.h>

#include <eng/Config.h>
#include <eng/Log.h>

#include <SDL2/SDL.h>

#include <map>
#include <set>
#include <vector>

namespace eng {

struct Input::Impl {
    std::map<std::string, std::vector<SDL_Keycode>> bindings;
    std::set<SDL_Keycode> down;
    std::set<SDL_Keycode> pressed;
    bool mouseClicked = false;
    bool grabbed = false;
    glm::vec2 delta{0.0f};

    void beginTick()
    {
        pressed.clear();
        mouseClicked = false;
        delta = glm::vec2(0.0f);
    }

    void onEvent(const SDL_Event& e)
    {
        switch (e.type) {
        case SDL_KEYDOWN:
            if (e.key.repeat == 0) {
                down.insert(e.key.keysym.sym);
                pressed.insert(e.key.keysym.sym);
            }
            break;
        case SDL_KEYUP:
            down.erase(e.key.keysym.sym);
            break;
        case SDL_MOUSEMOTION:
            delta += glm::vec2(float(e.motion.xrel), float(e.motion.yrel));
            break;
        case SDL_MOUSEBUTTONDOWN:
            if (e.button.button == SDL_BUTTON_LEFT)
                mouseClicked = true;
            break;
        }
    }

    const std::vector<SDL_Keycode>* find(const std::string& action) const
    {
        auto it = bindings.find(action);
        if (it == bindings.end())
            log::fatal("Input: unbound action '%s'", action.c_str());
        return &it->second;
    }
};

Input::Input() : mImpl(new Impl) {}
Input::~Input() = default;

bool Input::loadBindings(const Config& cfg)
{
    for (auto&& [action, keyNames] : cfg.bindings()) {
        for (const std::string& keyName : keyNames) {
            SDL_Keycode kc = SDL_GetKeyFromName(keyName.c_str());
            if (kc == SDLK_UNKNOWN) {
                log::error("Input: unknown key name '%s' for action '%s' "
                           "(use SDL key names, e.g. \"W\", \"Space\", "
                           "\"Return\", \"Escape\", \"Left Shift\")",
                           keyName.c_str(), action.c_str());
                return false;
            }
            mImpl->bindings[action].push_back(kc);
        }
    }
    return true;
}

bool Input::isDown(const std::string& action) const
{
    for (SDL_Keycode kc : *mImpl->find(action))
        if (mImpl->down.count(kc))
            return true;
    return false;
}

bool Input::wasPressed(const std::string& action) const
{
    for (SDL_Keycode kc : *mImpl->find(action))
        if (mImpl->pressed.count(kc))
            return true;
    return false;
}

bool Input::wasMouseClicked() const { return mImpl->mouseClicked; }
glm::vec2 Input::mouseDelta() const { return mImpl->delta; }

void Input::setMouseGrab(bool grab)
{
    SDL_SetRelativeMouseMode(grab ? SDL_TRUE : SDL_FALSE);
    mImpl->grabbed = grab;
}

bool Input::mouseGrabbed() const { return mImpl->grabbed; }

} // namespace eng
```

- [ ] **Step 3: Add to CMake, build, commit**

```bash
make build
git add -A
git commit -m "feat(engine): action-mapped input module"
```

---

### Task 5: Platform + Engine facade + screenshot hook

**Files:**
- Create: `engine/src/Platform.h`, `engine/src/Platform.cpp`, `engine/include/eng/Engine.h`, `engine/src/Engine.cpp`, `engine/src/InputImpl.h`
- Modify: `CMakeLists.txt` (add `Platform.cpp`, `Engine.cpp`), `engine/src/Input.cpp` (Impl moves out)

- [ ] **Step 1: Platform (internal)**

`engine/src/Platform.h`:

```cpp
#pragma once
#include <SDL2/SDL.h>

#include <cstdint>
#include <string>

namespace eng {

// Internal SDL window wrapper. No SDL_WINDOW_OPENGL: Ogre GL3Plus creates
// its own GL context against the raw native handle.
class Platform
{
public:
    bool init(const std::string& title, int width, int height);
    void shutdown(); // call AFTER RenderCore::shutdown (Ogre holds the handle)
    uintptr_t nativeHandle() const { return mNativeHandle; }

private:
    SDL_Window* mWindow = nullptr;
    uintptr_t mNativeHandle = 0;
};

} // namespace eng
```

`engine/src/Platform.cpp`:

```cpp
#include "Platform.h"

#include <eng/Log.h>

#include <SDL2/SDL_syswm.h>

namespace eng {

bool Platform::init(const std::string& title, int width, int height)
{
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        log::error("Platform: SDL_Init failed: %s", SDL_GetError());
        return false;
    }
    mWindow = SDL_CreateWindow(title.c_str(), SDL_WINDOWPOS_CENTERED,
                               SDL_WINDOWPOS_CENTERED, width, height,
                               SDL_WINDOW_RESIZABLE);
    if (!mWindow) {
        log::error("Platform: SDL_CreateWindow failed: %s", SDL_GetError());
        return false;
    }
    SDL_SysWMinfo wmInfo;
    SDL_VERSION(&wmInfo.version);
    SDL_GetWindowWMInfo(mWindow, &wmInfo);
#if defined(SDL_VIDEO_DRIVER_X11)
    mNativeHandle = static_cast<uintptr_t>(wmInfo.info.x11.window);
#endif
    if (!mNativeHandle) {
        log::error("Platform: no X11 window handle (run with SDL_VIDEODRIVER=x11)");
        return false;
    }
    return true;
}

void Platform::shutdown()
{
    if (mWindow) {
        SDL_DestroyWindow(mWindow);
        mWindow = nullptr;
    }
    SDL_Quit();
}

} // namespace eng
```

- [ ] **Step 2: Engine facade**

`engine/include/eng/Engine.h`:

```cpp
#pragma once
#include <eng/Config.h>
#include <eng/Input.h>
#include <eng/Renderer.h>

#include <memory>
#include <string>

namespace eng {

// Owns lifetime and ordering: SDL window -> Ogre Root -> (frames) ->
// Ogre Root down -> SDL window down. Also owns the frame clock and the
// PSX_SCREENSHOT verification hook (render 90 frames, save PNG, close).
class Engine
{
public:
    Engine();
    ~Engine();

    // Loads TOML config (window.title/width/height + [bindings]), creates
    // the window, brings up the renderer with engine + app asset roots.
    bool init(const std::string& configPath, const std::string& appAssetDir);

    float tick(); // pump events, update input; returns dt clamped to 0.1 s
    bool shouldClose() const { return mClose; }
    void requestClose() { mClose = true; }
    void renderFrame(float dt);
    void shutdown();

    Renderer& renderer() { return mRenderer; }
    Input& input() { return mInput; }
    Config& config() { return mConfig; }

private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
    Config mConfig;
    Input mInput;
    Renderer mRenderer;
    bool mClose = false;
};

} // namespace eng
```

`engine/src/Engine.cpp`:

```cpp
#include <eng/Engine.h>

#include <eng/Log.h>

#include "InputImpl.h"
#include "Platform.h"
#include "RenderCore.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>

// detail::coreOf / detail::registerRoot come from eng/Renderer.h (via
// Engine.h); their definitions live in Renderer.cpp next to Renderer::Impl.
namespace eng {

struct Engine::Impl {
    Platform platform;
    std::chrono::steady_clock::time_point prev;
    std::string screenshotPath;
    int frameCount = 0;
};

Engine::Engine() : mImpl(new Impl) {}
Engine::~Engine() = default;

bool Engine::init(const std::string& configPath, const std::string& appAssetDir)
{
    if (!mConfig.load(configPath))
        return false;
    const std::string title = mConfig.getString("window.title", "eng");
    const int width = static_cast<int>(mConfig.getNumber("window.width", 960));
    const int height = static_cast<int>(mConfig.getNumber("window.height", 720));

    if (!mImpl->platform.init(title, width, height))
        return false;
    if (!detail::coreOf(mRenderer).init(mImpl->platform.nativeHandle(), width,
                                        height, title, appAssetDir))
        return false;
    detail::registerRoot(mRenderer);
    if (!mInput.loadBindings(mConfig))
        return false;

    const char* shot = std::getenv("PSX_SCREENSHOT");
    if (shot)
        mImpl->screenshotPath = shot;
    mImpl->prev = std::chrono::steady_clock::now();
    return true;
}

float Engine::tick()
{
    mInput.mImpl->beginTick();
    for (SDL_Event e; SDL_PollEvent(&e);) {
        if (e.type == SDL_QUIT)
            mClose = true;
        else if (e.type == SDL_WINDOWEVENT &&
                 e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
            detail::coreOf(mRenderer).onResize(e.window.data1, e.window.data2);
        else
            mInput.mImpl->onEvent(e);
    }
    auto now = std::chrono::steady_clock::now();
    float dt = std::chrono::duration<float>(now - mImpl->prev).count();
    mImpl->prev = now;
    return std::min(dt, 0.1f);
}

void Engine::renderFrame(float dt)
{
    detail::coreOf(mRenderer).renderFrame(dt);
    if (!mImpl->screenshotPath.empty() && ++mImpl->frameCount == 90) {
        detail::coreOf(mRenderer).writeScreenshot(mImpl->screenshotPath);
        mClose = true;
    }
}

void Engine::shutdown()
{
    detail::coreOf(mRenderer).shutdown(); // Ogre first
    mImpl->platform.shutdown();           // native window after
}

} // namespace eng
```

`Engine::tick` calls `mInput.mImpl->beginTick()` / `onEvent()`, so the
`Input::Impl` definition must be visible here. Create `engine/src/InputImpl.h`
with this exact content and cut the identical `struct Input::Impl { ... };`
block out of `Input.cpp` (which then does `#include "InputImpl.h"` in place
of it — keep its other includes):

```cpp
#pragma once
#include <eng/Input.h>
#include <eng/Log.h>

#include <SDL2/SDL.h>

#include <map>
#include <set>
#include <vector>

namespace eng {

struct Input::Impl {
    std::map<std::string, std::vector<SDL_Keycode>> bindings;
    std::set<SDL_Keycode> down;
    std::set<SDL_Keycode> pressed;
    bool mouseClicked = false;
    bool grabbed = false;
    glm::vec2 delta{0.0f};

    void beginTick()
    {
        pressed.clear();
        mouseClicked = false;
        delta = glm::vec2(0.0f);
    }

    void onEvent(const SDL_Event& e)
    {
        switch (e.type) {
        case SDL_KEYDOWN:
            if (e.key.repeat == 0) {
                down.insert(e.key.keysym.sym);
                pressed.insert(e.key.keysym.sym);
            }
            break;
        case SDL_KEYUP:
            down.erase(e.key.keysym.sym);
            break;
        case SDL_MOUSEMOTION:
            delta += glm::vec2(float(e.motion.xrel), float(e.motion.yrel));
            break;
        case SDL_MOUSEBUTTONDOWN:
            if (e.button.button == SDL_BUTTON_LEFT)
                mouseClicked = true;
            break;
        }
    }

    const std::vector<SDL_Keycode>* find(const std::string& action) const
    {
        auto it = bindings.find(action);
        if (it == bindings.end())
            log::fatal("Input: unbound action '%s'", action.c_str());
        return &it->second;
    }
};

} // namespace eng
```

- [ ] **Step 3: detail accessors in Renderer.cpp**

The `detail::coreOf` / `detail::registerRoot` declarations and friend
statements already exist in `engine/include/eng/Renderer.h` (Task 3).
Append their definitions to `engine/src/Renderer.cpp`, at the end of
`namespace eng`:

```cpp
namespace detail {

RenderCore& coreOf(Renderer& r) { return r.mImpl->core; }

void registerRoot(Renderer& r)
{
    r.mImpl->nodes.push_back(r.mImpl->core.sceneMgr()->getRootSceneNode());
}

} // namespace detail
```

- [ ] **Step 4: Add Platform.cpp + Engine.cpp to CMake, build, commit**

```bash
make build
git add -A
git commit -m "feat(engine): Engine facade with SDL platform, frame clock, screenshot hook"
```

---

### Task 6: Port psx-demo to the public API

**Files:**
- Create: `samples/psx-demo/assets/demo.toml`
- Rewrite: `samples/psx-demo/src/main.cpp`
- Delete: `samples/psx-demo/src/Animation.h`
- Modify: `CMakeLists.txt` (drop demo's internal include + Ogre/SDL links)

- [ ] **Step 1: demo.toml**

`samples/psx-demo/assets/demo.toml`:

```toml
[window]
title = "ogre-psx-demo"
width = 960
height = 720

[bindings]
pause = ["Space", "Return"]
restart = "R"
quit = "Escape"
```

- [ ] **Step 2: Rewrite main.cpp against the public API**

`samples/psx-demo/src/main.cpp` (full replacement):

```cpp
// ogre-psx-demo -- port of MenacingMecha's godot-psx-style-demo, driven
// through the eng public API (no Ogre/SDL includes here).

#include <eng/Engine.h>
#include <eng/Math.h>

#include <glm/gtc/matrix_transform.hpp> // glm::scale
#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <string>
#include <vector>

namespace {

// Godot linear-space shading: sRGB editor colours linearised, energy
// multiplied after.
float lin(float srgb) { return std::pow(srgb, 2.2f); }

// R(quat) * S(scale), zero translation -- crystal spire bakes.
glm::mat4 trsBake(glm::quat q, glm::vec3 s)
{
    return glm::mat4_cast(q) *
           glm::scale(glm::mat4(1.0f), s);
}

// Basis given as rows a..i (tscn order), zero translation.
glm::mat4 rowsBake(float a, float b, float c, float d, float e, float f,
                   float g, float h, float i)
{
    return glm::mat4(a, d, g, 0, b, e, h, 0, c, f, i, 0, 0, 0, 0, 1);
}

// world/orbit_camera.gd: rotation.y = base + t
struct OrbitCamera {
    eng::NodeHandle node;
    float baseYaw = 0.0f;
    void update(eng::Renderer& r, float t) const
    {
        r.setOrientation(node,
                         glm::angleAxis(baseYaw + t, glm::vec3(0, 1, 0)));
    }
};

// world/spatial_sin_pan.gd: offset = T(0,sin(t)*dir,0) * Euler-YXZ(t,t,t)
struct SinPan {
    eng::NodeHandle node;
    bool reverse = false;
    void update(eng::Renderer& r, float t) const
    {
        const float dir = reverse ? -1.0f : 1.0f;
        r.setPosition(node, {0.0f, std::sin(t) * dir, 0.0f});
        r.setOrientation(node, glm::angleAxis(t, glm::vec3(0, 1, 0)) *
                                   glm::angleAxis(t, glm::vec3(1, 0, 0)) *
                                   glm::angleAxis(t, glm::vec3(0, 0, 1)));
    }
};

// world/shadow/shadow.gd: scale = 0.775 + sin(t) * 0.125 * dir
struct ShadowScale {
    eng::NodeHandle node;
    bool reverse = false;
    void update(eng::Renderer& r, float t) const
    {
        const float dir = reverse ? 1.0f : -1.0f;
        const float s = 0.775f + std::sin(t) * 0.125f * dir;
        r.setScale(node, {s, s, s});
    }
};

} // namespace

int main(int, char**)
{
    eng::Engine engine;
    const std::string assets = APP_ASSET_DIR;
    if (!engine.init(assets + "/demo.toml", assets))
        return 1;
    eng::Renderer& r = engine.renderer();

    // Camera3D: fov 68.1243 vertical, Godot default clips.
    r.setCameraFov(68.1243f);
    r.setCameraClip(0.05f, 4000.0f);

    // world_env.tres
    r.setAmbient({lin(1.0f) * 0.15f, lin(0.67451f) * 0.15f,
                  lin(0.988235f) * 0.15f});
    r.setFog({lin(0.670588f), lin(0.760784f), lin(1.0f)}, 0.05f);
    r.setBackground({0.670588f, 0.760784f, 1.0f});

    // ------------------------------------------------------------ meshes ---
    const std::string mdir = assets + "/meshes/";
    eng::MeshHandle boxMesh = r.loadObj(mdir + "box.obj");
    eng::MeshHandle bevelBoxMesh = r.loadObj(mdir + "bevel-box.obj");
    eng::MeshHandle lightShaftMesh = r.loadObj(mdir + "light_shaft.obj");
    eng::MeshHandle crystalGround = r.loadObj(mdir + "crystal_ground.obj");

    // Spire transforms (rotation * non-uniform scale) baked into vertices.
    const glm::mat4 bake1 =
        trsBake(glm::quat(0.99098921f, 0.0f, 0.0f, -0.13394181f),
                {0.96659988f, 1.97307432f, 0.96659982f});
    const glm::mat4 bake2 =
        rowsBake(-0.223364f, -0.132712f, 0.884556f, -0.242743f, 1.90228f,
                 -1.36255e-08f, -0.852817f, -0.5067f, -0.231677f);
    const glm::mat4 bake3 =
        rowsBake(-0.689533f, -0.463782f, -0.375407f, -0.214429f, 1.90228f,
                 3.00904e-09f, 0.361937f, 0.24344f, -0.715194f);
    const glm::mat4 bake4 =
        trsBake(glm::quat(0.78186893f, 0.08229667f, -0.60888463f, -0.10567717f),
                {0.61454207f, 1.97307408f, 0.61454219f});
    const eng::MeshHandle spires[4] = {
        r.loadObj(mdir + "crystal_spire1.obj", &bake1),
        r.loadObj(mdir + "crystal_spire2.obj", &bake2),
        r.loadObj(mdir + "crystal_spire3.obj", &bake3),
        r.loadObj(mdir + "crystal_spire4.obj", &bake4),
    };

    eng::MeshHandle bgMesh = r.createInteriorBox(40.0f, 25);
    eng::MeshHandle shadowPlane = r.createPlane(2.0f);

    // -------------------------------------------------- OrbitPoint branch ---
    OrbitCamera orbit;
    orbit.node = r.createNode(eng::kRootNode);
    orbit.baseYaw = std::atan2(-0.556238f, 0.831023f);

    eng::NodeHandle camNode =
        r.createNode(orbit.node, {0.0f, 2.147f, 4.48151f});
    r.setOrientation(camNode,
                     eng::quatFromBasisRows(1, 0, 0, 0, 0.989078f, 0.147395f,
                                            0, -0.147395f, 0.989078f));
    r.attachCamera(camNode);

    eng::NodeHandle dirLightNode =
        r.createNode(orbit.node, {5.08833f, 2.79045f, -0.311581f});
    r.setOrientation(dirLightNode,
                     eng::quatFromBasisRows(0.999229f, -0.0247207f, 0.0305003f,
                                            0.0f, 0.776871f, 0.629659f,
                                            -0.0392604f, -0.629174f, 0.776272f));
    eng::LightDesc dirLight;
    dirLight.type = eng::LightDesc::Type::Directional;
    dirLight.colour = {1.5f, 1.5f, 1.5f}; // light_energy 1.5, white
    r.attachLight(dirLightNode, dirLight);

    // --------------------------------------------------------- OmniLights ---
    for (float x : {-4.0f, 4.0f}) {
        eng::LightDesc omni;
        omni.colour = glm::vec3(lin(0.909804f), lin(0.803922f),
                                lin(0.666667f)) *
                      4.75f;
        omni.range = 3.0f;
        r.attachLight(r.createNode(eng::kRootNode, {x, 0.784f, 0.0f}), omni);
    }

    // --------------------------------------------------------- Background ---
    r.attachMesh(r.createNode(eng::kRootNode, {0.0f, 20.0f, 0.0f}), bgMesh,
                 "PSX/Floor");

    // --------------------------------------------------------- LightShaft ---
    r.attachMesh(
        r.createNode(eng::kRootNode, {0.0109267f, 2.05731f, 0.0147681f}),
        lightShaftMesh, "PSX/LightShaft");

    // ------------------------------------------------- BoxMetal + sparkles ---
    std::vector<SinPan> sinPans;
    std::vector<ShadowScale> shadowScales;
    {
        eng::NodeHandle base =
            r.createNode(eng::kRootNode, {-1.0f, 2.236f, 0.0f});
        r.setScale(base, glm::vec3(0.613118f));
        eng::NodeHandle anim = r.createNode(base);
        r.attachMesh(anim, bevelBoxMesh, "PSX/BoxMetal");
        r.attachParticles(anim, "PSX/Sparkles");
        sinPans.push_back({anim, /*reverse=*/false});
    }

    // -------------------------------------------------------------- BoxLit ---
    {
        eng::NodeHandle base =
            r.createNode(eng::kRootNode, {1.0f, 2.236f, 0.0f});
        r.setScale(base, glm::vec3(0.613118f));
        eng::NodeHandle anim = r.createNode(base);
        r.attachMesh(anim, boxMesh, "PSX/BoxLit");
        sinPans.push_back({anim, /*reverse=*/true});
    }

    // -------------------------------------------------------- blob shadows ---
    for (bool reverse : {false, true}) {
        eng::NodeHandle base = r.createNode(
            eng::kRootNode, {reverse ? 1.0f : -1.0f, 0.0f, 0.0f});
        eng::NodeHandle mesh = r.createNode(base, {0.0f, 0.05f, 0.0f});
        r.attachMesh(mesh, shadowPlane, "PSX/Shadow");
        shadowScales.push_back({base, reverse});
        shadowScales.back().update(r, 0.0f);
    }

    // ------------------------------------------------------------ crystals ---
    struct CrystalXf {
        float a, b, c, d, e, f, g, h, i, ox, oy, oz;
    };
    const CrystalXf crystals[5] = {
        {0.719146f, 0, 0.694859f, 0, 1, 0, -0.694859f, 0, 0.719146f, 1.93081f, 0, 1.34372f},
        {0.826285f, 0, -0.563252f, 0, 1, 0, 0.563252f, 0, 0.826285f, -1.32247f, 0, 1.77809f},
        {0.864371f, 0, 0.502854f, 0, 1, 0, -0.502854f, 0, 0.864371f, -2.32825f, 0, -0.999177f},
        {-0.632935f, 0, 0.774205f, 0, 1, 0, -0.774205f, 0, -0.632935f, 2.30476f, 0, -1.16371f},
        {-0.90227f, 0, -0.431172f, 0, 1, 0, 0.431172f, 0, -0.90227f, -0.00271803f, 0, -1.98459f},
    };
    for (const CrystalXf& x : crystals) {
        eng::NodeHandle root = r.createNode(eng::kRootNode, {x.ox, x.oy, x.oz});
        r.setOrientation(root, eng::quatFromBasisRows(x.a, x.b, x.c, x.d, x.e,
                                                      x.f, x.g, x.h, x.i));
        eng::NodeHandle ground =
            r.createNode(root, {-0.02274385f, 0.0f, 0.01232092f});
        r.setScale(ground, glm::vec3(0.31058314f));
        r.attachMesh(ground, crystalGround, "PSX/CrystalGround");
        for (const eng::MeshHandle& spire : spires)
            r.attachMesh(ground, spire, "PSX/CrystalSpire");
    }

    r.setDitherEnabled(true);

    // ---------------------------------------------------------------- loop ---
    bool paused = false;
    float animTime = 0.0f;
    orbit.update(r, 0.0f);
    for (SinPan& p : sinPans)
        p.update(r, 0.0f);

    while (!engine.shouldClose()) {
        const float dt = engine.tick();
        eng::Input& in = engine.input();
        if (in.wasPressed("quit"))
            engine.requestClose();
        if (in.wasPressed("pause"))
            paused = !paused;
        if (in.wasPressed("restart"))
            animTime = 0.0f;

        if (!paused)
            animTime += dt;
        orbit.update(r, animTime);
        for (SinPan& p : sinPans)
            p.update(r, animTime);
        for (ShadowScale& s : shadowScales)
            s.update(r, animTime);

        engine.renderFrame(dt);
    }
    engine.shutdown();
    return 0;
}
```

```bash
git rm samples/psx-demo/src/Animation.h
```

- [ ] **Step 3: Tighten CMake**

```cmake
add_executable(psx_demo samples/psx-demo/src/main.cpp)
target_link_libraries(psx_demo PRIVATE eng)
target_compile_definitions(psx_demo PRIVATE
    APP_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/samples/psx-demo/assets"
)
```
(no more `engine/src` include, no OgreMain/SDL2 link.)

- [ ] **Step 4: Verify against baseline**

```bash
make build
cd build && SDL_VIDEODRIVER=x11 PSX_SCREENSHOT=/tmp/psx-task6.png ./psx_demo && cd ..
```
Expected: `/tmp/psx-task6.png` visually matches `/tmp/psx-baseline.png` (orbit
phase may differ slightly — wall-clock timing). Also run `make run-demo`
interactively: Space pauses, R restarts, Esc quits.

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "feat(demo): port psx-demo to the eng public API

No Ogre/SDL includes left in sample code; demo doubles as the
renderer API regression test."
```

---

### Task 7: Game executable (FPS test room)

**Files:**
- Create: `game/assets/game.toml`, `game/assets/materials/game.material`, `game/assets/textures/floor.png` (copy), `game/src/FpsController.h`, `game/src/FpsController.cpp`, `game/src/main.cpp`
- Modify: `CMakeLists.txt` (add `game` target)

- [ ] **Step 1: Assets**

```bash
mkdir -p game/assets/materials game/assets/textures
cp samples/psx-demo/assets/textures/floor.png game/assets/textures/
```

`game/assets/game.toml`:

```toml
[window]
title = "dungeon-crawler"
width = 960
height = 720

[player]
speed = 3.0
mouse_sensitivity = 0.002

[bindings]
move_forward = "W"
move_back = "S"
move_left = "A"
move_right = "D"
quit = "Escape"
```

`game/assets/materials/game.material`:

```
// Test-room material: PSX lit shader pair, tiled floor texture.
material Game/Room
{
    technique { pass {
        vertex_program_ref PSX_VS_Lit
        {
            param_named uvScale float2 6.0 6.0
        }
        fragment_program_ref PSX_FS_Lit { }
        texture_unit
        {
            texture floor.png
            filtering none
            tex_address_mode wrap
        }
    } }
}
```

- [ ] **Step 2: FpsController**

`game/src/FpsController.h`:

```cpp
#pragma once
#include <eng/Handles.h>

#include <glm/glm.hpp>

namespace eng {
class Input;
class Renderer;
} // namespace eng

// First-person controller: yaw on a body node, pitch on a head node
// (camera attached at eye height). Movement on the ground plane, clamped
// to a room AABB -- no collision system in the scaffold.
class FpsController
{
public:
    void init(eng::Renderer& r, glm::vec3 startPos, float speed,
              float sensitivity, glm::vec3 roomMin, glm::vec3 roomMax);
    void update(eng::Input& in, eng::Renderer& r, float dt);

private:
    eng::NodeHandle mBody{};
    eng::NodeHandle mHead{};
    glm::vec3 mPos{0.0f};
    glm::vec3 mMin{0.0f};
    glm::vec3 mMax{0.0f};
    float mYaw = 0.0f;
    float mPitch = 0.0f;
    float mSpeed = 3.0f;
    float mSens = 0.002f;
};
```

`game/src/FpsController.cpp`:

```cpp
#include "FpsController.h"

#include <eng/Input.h>
#include <eng/Renderer.h>

#include <cmath>

namespace {
constexpr float kEyeHeight = 1.7f;
constexpr float kMaxPitch = glm::radians(89.0f);
} // namespace

void FpsController::init(eng::Renderer& r, glm::vec3 startPos, float speed,
                         float sensitivity, glm::vec3 roomMin, glm::vec3 roomMax)
{
    mPos = startPos;
    mSpeed = speed;
    mSens = sensitivity;
    mMin = roomMin;
    mMax = roomMax;
    mBody = r.createNode(eng::kRootNode, mPos);
    mHead = r.createNode(mBody, {0.0f, kEyeHeight, 0.0f});
    r.attachCamera(mHead);
}

void FpsController::update(eng::Input& in, eng::Renderer& r, float dt)
{
    if (in.mouseGrabbed()) {
        const glm::vec2 d = in.mouseDelta();
        mYaw -= d.x * mSens;
        mPitch = glm::clamp(mPitch - d.y * mSens, -kMaxPitch, kMaxPitch);
    }

    // Camera looks down -Z at yaw 0; forward/right on the ground plane.
    const glm::vec3 fwd(-std::sin(mYaw), 0.0f, -std::cos(mYaw));
    const glm::vec3 right(std::cos(mYaw), 0.0f, -std::sin(mYaw));
    glm::vec3 move(0.0f);
    if (in.isDown("move_forward"))
        move += fwd;
    if (in.isDown("move_back"))
        move -= fwd;
    if (in.isDown("move_right"))
        move += right;
    if (in.isDown("move_left"))
        move -= right;
    if (glm::length(move) > 0.0f)
        mPos += glm::normalize(move) * mSpeed * dt;
    mPos = glm::clamp(mPos, mMin, mMax);

    r.setPosition(mBody, mPos);
    r.setOrientation(mBody, glm::angleAxis(mYaw, glm::vec3(0, 1, 0)));
    r.setOrientation(mHead, glm::angleAxis(mPitch, glm::vec3(1, 0, 0)));
}
```

- [ ] **Step 3: Game main**

`game/src/main.cpp`:

```cpp
// dungeon-crawler scaffold: FPS walk in a PSX-shaded test room.

#include "FpsController.h"

#include <eng/Engine.h>

#include <cmath>
#include <string>

namespace {
float lin(float srgb) { return std::pow(srgb, 2.2f); }
} // namespace

int main(int, char**)
{
    eng::Engine engine;
    const std::string assets = APP_ASSET_DIR;
    if (!engine.init(assets + "/game.toml", assets))
        return 1;
    eng::Renderer& r = engine.renderer();

    r.setCameraFov(70.0f);
    r.setCameraClip(0.05f, 100.0f);
    r.setAmbient(glm::vec3(0.12f));
    r.setFog({lin(0.05f), lin(0.05f), lin(0.08f)}, 0.08f);
    r.setBackground({0.02f, 0.02f, 0.03f});

    // Room: 10m interior cube centred at y=5 -> floor at y=0.
    const float roomSize = 10.0f;
    eng::MeshHandle room = r.createInteriorBox(roomSize, 1);
    r.attachMesh(r.createNode(eng::kRootNode, {0.0f, roomSize / 2.0f, 0.0f}),
                 room, "Game/Room");

    eng::LightDesc sun;
    sun.type = eng::LightDesc::Type::Directional;
    sun.colour = {0.9f, 0.9f, 1.0f};
    eng::NodeHandle sunNode = r.createNode(eng::kRootNode);
    r.setOrientation(sunNode,
                     glm::angleAxis(glm::radians(-60.0f), glm::vec3(1, 0, 0)));
    r.attachLight(sunNode, sun);

    for (float x : {-3.0f, 3.0f}) {
        eng::LightDesc lamp;
        lamp.colour =
            glm::vec3(lin(0.909804f), lin(0.803922f), lin(0.666667f)) * 4.75f;
        lamp.range = 5.0f;
        r.attachLight(r.createNode(eng::kRootNode, {x, 1.5f, 0.0f}), lamp);
    }

    r.setDitherEnabled(true);

    FpsController player;
    const float margin = roomSize / 2.0f - 0.5f;
    player.init(r, {0.0f, 0.0f, 3.0f},
                float(engine.config().getNumber("player.speed", 3.0)),
                float(engine.config().getNumber("player.mouse_sensitivity", 0.002)),
                {-margin, 0.0f, -margin}, {margin, 0.0f, margin});
    engine.input().setMouseGrab(true);

    while (!engine.shouldClose()) {
        const float dt = engine.tick();
        eng::Input& in = engine.input();
        // First Esc releases the mouse, second quits; click re-grabs.
        if (in.wasPressed("quit")) {
            if (in.mouseGrabbed())
                in.setMouseGrab(false);
            else
                engine.requestClose();
        }
        if (!in.mouseGrabbed() && in.wasMouseClicked())
            in.setMouseGrab(true);

        player.update(in, r, dt);
        engine.renderFrame(dt);
    }
    engine.shutdown();
    return 0;
}
```

- [ ] **Step 4: CMake target**

```cmake
add_executable(game
    game/src/main.cpp
    game/src/FpsController.cpp
)
target_link_libraries(game PRIVATE eng)
target_compile_definitions(game PRIVATE
    APP_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/game/assets"
)
```

- [ ] **Step 5: Verify**

```bash
make build
cd build && SDL_VIDEODRIVER=x11 PSX_SCREENSHOT=/tmp/game-task7.png ./game && cd ..
```
Expected: screenshot shows dithered 320x240-upscaled room interior with tiled
floor texture. Then `make run-game` interactively: mouse-look works, WASD
moves, walls stop you (AABB clamp), Esc releases mouse, Esc again quits.

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "feat(game): FPS test room with PSX shaders and dither post"
```

---

### Task 8: Boundary check + README

**Files:**
- Modify: `README.md`

- [ ] **Step 1: Verify no Ogre/SDL leaks outside engine/src**

```bash
grep -rnE '#include [<"](Ogre|SDL)' game/ samples/ engine/include/ && echo LEAK || echo CLEAN
```
Expected: `CLEAN`.

- [ ] **Step 2: Rewrite README.md**

```markdown
# dungeon-crawler

C++17 engine scaffold for a PSX-style single-player FPS RPG dungeon crawler.
OGRE 14.x is the renderer (hand-written GLSL PSX shaders, no RTSS), SDL2 owns
the window and loop — both fully hidden behind the `eng` public API
(`engine/include/eng/`, GLM math, handle-based scene graph).

## Build & run

```sh
make run-game    # FPS test room: WASD + mouse-look, Esc releases/quits
make run-demo    # PSX shader demo (port of MenacingMecha's godot-psx-style)
```

Requires system OGRE 14.x (plugins `RenderSystem_GL3Plus`, `Plugin_ParticleFX`,
`Codec_STBI`), SDL2, GLM, CMake >= 3.16. On Wayland the Makefile forces
`SDL_VIDEODRIVER=x11` (XWayland). `PSX_SCREENSHOT=/path.png` renders 90 frames,
saves a screenshot, and exits (verification hook, both targets).

## Layout

```
engine/include/eng/   public API (Engine, Renderer, Input, Config, Log, Math)
engine/src/           Ogre + SDL implementation (RenderCore, Platform, ...)
engine/assets/        PSX shader stack (GLSL, programs, materials, compositor)
game/                 the game: FPS test room scaffold + game.toml
samples/psx-demo/     renderer regression sample (godot-psx-style port)
docs/superpowers/     design specs and plans
```

Design docs: `docs/superpowers/specs/2026-07-13-engine-scaffold-design.md`.
Deviation notes for the original 1:1 Godot port live in git history
(`README.md` before the scaffold restructure).
```

- [ ] **Step 3: Final commit**

```bash
git add -A
git commit -m "docs: scaffold README, boundary check"
```
