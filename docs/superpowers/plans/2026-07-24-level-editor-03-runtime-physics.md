# Level Editor — Plan 3: Runtime & Physics Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let the `game` executable load and play a `.map` file authored in the editor: deserialize into an `entt` registry, build renderer nodes via `SceneSync` and Jolt collision bodies via a new `PhysicsSync`, spawn the player at the `PlayerSpawn` marker, and walk the level. Invoked as `game <path.map>` (the editor's Play button already passes this).

**Architecture:** A new `PhysicsSync` reconciles `game::Collider`/`game::Trigger` components into `eng::Physics` bodies (mirroring how `SceneSync` reconciles render nodes), tagging each entity with a runtime-only `BodyRef`. A `MapRuntime` owns a registry + `SceneSync` + `PhysicsSync` and loads a `.map` (Plan-1 `readMap`). A thin `playMap()` entry, hooked at the top of `main()`, runs a dedicated play loop (input → `FpsController` → `physics.update` → `SceneSync`/`PhysicsSync` → render) — kept separate from the existing procedural `DungeonMap` pipeline so that large, coupled code path is not disturbed.

**Tech Stack:** C++17, EnTT, GLM, Jolt via `eng::Physics`, `eng::ecs::SceneSync`/`RendererSceneBackend`, `FpsController`, Plan-1 `mapio`, CMake + CTest.

**Design notes:**
- `PhysicsSync` is tested against the **real** `eng::Physics` (the repo's `physics_tests` already links `eng` and boots Jolt), asserting `bodyCount()` deltas — no mock needed.
- `MapRuntime` is tested with a **mock** `SceneBackend` (renderer-free) plus the real `eng::Physics`, loading a `.map` written inline in the test.
- Colliders build **static** bodies at the entity's authored `Transform` (flat hierarchy, matching `SceneSync`'s current world-transform handling). Triggers build **sensor** bodies on `BodyLayer::Trigger`.

---

## File Structure

New (in `game/src/scene/`, shared by `game`; `PhysicsSync` also compiles into `level_editor` later if needed):
- `RuntimeComponents.h` — `game::BodyRef { eng::BodyHandle handle; }` (runtime-only, never serialized).
- `PhysicsSync.h/.cpp` — reconciles `Collider`/`Trigger` → Jolt bodies.
- `MapRuntime.h/.cpp` — owns registry + `SceneSync` + `PhysicsSync`; `load()`, `buildAll()`, `step()`, `playerSpawn()`.

New (in `game/src/`):
- `MapPlay.h/.cpp` — `int playMap(eng::Engine&, eng::Physics&, const std::string& assetDir, const std::string& mapPath)`: the dedicated play loop.

Modified:
- `game/src/main.cpp` — near the top of `main()`, if an argv ends in `.map`, call `playMap(...)` and return its result (bypass the procedural boot).
- `CMakeLists.txt` — add the new sources to the `game` target; add `physics_sync_tests` and `map_runtime_tests`.

Tests (`game/tests/`): `PhysicsSyncTests.cpp`, `MapRuntimeTests.cpp`.

---

## Task 1: BodyRef component + PhysicsSync

**Files:**
- Create: `game/src/scene/RuntimeComponents.h`, `game/src/scene/PhysicsSync.h`, `game/src/scene/PhysicsSync.cpp`
- Test: `game/tests/PhysicsSyncTests.cpp`

`PhysicsSync` walks entities with `Collider` (or `Trigger`) and no `BodyRef`, creates a Jolt body from the component + the entity's `Transform`, and stores the handle in `BodyRef`. On a later sync, entities that were destroyed have their bodies removed. It also exposes `bodyOf(entity)` for the play loop's debug draw.

- [ ] **Step 1: Failing test** — `game/tests/PhysicsSyncTests.cpp`:

```cpp
#include "PhysicsSync.h"
#include "RuntimeComponents.h"

#include "GameComponents.h"

#include <eng/Physics.h>
#include <eng/ecs/Components.h>

#include <cstdlib>
#include <iostream>

using namespace game;

static void require(bool c, const char* m)
{
    if (!c) { std::cerr << "PhysicsSyncTests: " << m << '\n'; std::exit(1); }
}

int main()
{
    eng::Physics physics;
    physics.init();

    entt::registry reg;
    PhysicsSync sync(reg, physics);

    // A collider entity gets a static body + a BodyRef on first sync.
    entt::entity wall = reg.create();
    eng::ecs::Transform t;
    t.position = glm::vec3(2, 0, 0);
    reg.emplace<eng::ecs::Transform>(wall, t);
    reg.emplace<Collider>(wall, Collider{eng::ShapeKind::Box, glm::vec3(1, 2, 1),
                                         eng::BodyLayer::Static});

    const int before = physics.bodyCount();
    sync.sync();
    require(physics.bodyCount() == before + 1, "sync creates one body for the collider");
    require(reg.all_of<BodyRef>(wall), "collider entity gets a BodyRef");
    require(reg.get<BodyRef>(wall).handle.valid(), "BodyRef handle is valid");

    // Second sync is idempotent (no duplicate body).
    sync.sync();
    require(physics.bodyCount() == before + 1, "re-sync does not duplicate bodies");

    // A trigger entity builds a sensor body.
    entt::entity trig = reg.create();
    reg.emplace<eng::ecs::Transform>(trig, eng::ecs::Transform{});
    reg.emplace<Trigger>(trig, Trigger{eng::ShapeKind::Box, glm::vec3(1), "door"});
    sync.sync();
    require(reg.all_of<BodyRef>(trig), "trigger entity gets a BodyRef");
    require(physics.bodyCount() == before + 2, "trigger adds one sensor body");

    // Destroying an entity frees its body on the next sync.
    reg.destroy(wall);
    sync.sync();
    require(physics.bodyCount() == before + 1, "destroyed entity's body is removed");

    physics.shutdown();
    std::cout << "PhysicsSyncTests OK\n";
    return 0;
}
```

- [ ] **Step 2: CMake target** under `if(BUILD_TESTING)`:

```cmake
  add_executable(physics_sync_tests
    game/tests/PhysicsSyncTests.cpp
    game/src/scene/PhysicsSync.cpp)
  target_include_directories(physics_sync_tests
    PRIVATE game/src/scene engine/include third_party)
  target_link_libraries(physics_sync_tests PRIVATE eng EnTT::EnTT glm::glm)
  add_test(NAME physics_sync COMMAND physics_sync_tests)
```
Reconfigure: `cmake -S /home/sektant1/psx-dungeon-crawler -B /home/sektant1/psx-dungeon-crawler/build`.
(Links `eng` for real Jolt physics, as `physics_tests` does.)

- [ ] **Step 3: Confirm fail.**

- [ ] **Step 4: `RuntimeComponents.h`:**

```cpp
#pragma once
#include <eng/Handles.h>

namespace game {
// Runtime-only link from an entity to its Jolt body. Populated by PhysicsSync,
// never serialized to .map.
struct BodyRef {
    eng::BodyHandle handle;
};
} // namespace game
```

- [ ] **Step 5: `PhysicsSync.h`:**

```cpp
#pragma once

#include <entt/entt.hpp>

namespace eng { class Physics; }

namespace game {

// Reconciles Collider/Trigger components into eng::Physics bodies, mirroring
// how eng::ecs::SceneSync reconciles render nodes. Static colliders become
// solid static bodies; triggers become sensor bodies on the Trigger layer.
class PhysicsSync {
public:
    PhysicsSync(entt::registry& reg, eng::Physics& physics)
        : mReg(reg), mPhysics(physics) {}

    // Create bodies for Collider/Trigger entities lacking a BodyRef; remove
    // bodies for tracked entities that were destroyed. Idempotent.
    void sync();

private:
    entt::registry& mReg;
    eng::Physics& mPhysics;
    std::vector<std::pair<entt::entity, eng::BodyHandle>> mTracked;
};

} // namespace game
```
Add `#include <eng/Handles.h>`, `#include <utility>`, `#include <vector>` as needed.

- [ ] **Step 6: `PhysicsSync.cpp`:**

```cpp
#include "PhysicsSync.h"

#include "GameComponents.h"
#include "RuntimeComponents.h"

#include <eng/Physics.h>
#include <eng/ecs/Components.h>

#include <algorithm>

namespace game {

namespace {
glm::vec3 posOf(const entt::registry& r, entt::entity e)
{
    if (const auto* t = r.try_get<eng::ecs::Transform>(e)) return t->position;
    return glm::vec3(0.0f);
}
glm::quat rotOf(const entt::registry& r, entt::entity e)
{
    if (const auto* t = r.try_get<eng::ecs::Transform>(e)) return t->rotation;
    return glm::quat(1, 0, 0, 0);
}
} // namespace

void PhysicsSync::sync()
{
    // Colliders -> solid static bodies.
    for (auto e : mReg.view<Collider>(entt::exclude<BodyRef>)) {
        const Collider& c = mReg.get<Collider>(e);
        eng::BodyDesc d;
        d.kind = c.shape;
        d.halfExtents = c.size;
        d.radius = c.size.x;
        d.halfHeight = c.size.y;
        d.position = posOf(mReg, e);
        d.orientation = rotOf(mReg, e);
        d.layer = c.layer;
        d.dynamic = false;
        const eng::BodyHandle h = mPhysics.createBody(d);
        mReg.emplace<BodyRef>(e, BodyRef{h});
        mTracked.emplace_back(e, h);
    }

    // Triggers -> sensor bodies on the Trigger layer.
    for (auto e : mReg.view<Trigger>(entt::exclude<BodyRef>)) {
        const Trigger& tr = mReg.get<Trigger>(e);
        eng::BodyDesc d;
        d.kind = tr.shape;
        d.halfExtents = tr.size;
        d.radius = tr.size.x;
        d.halfHeight = tr.size.y;
        d.position = posOf(mReg, e);
        d.orientation = rotOf(mReg, e);
        d.layer = eng::BodyLayer::Trigger;
        d.dynamic = false;
        d.sensor = true;
        const eng::BodyHandle h = mPhysics.createBody(d);
        mReg.emplace<BodyRef>(e, BodyRef{h});
        mTracked.emplace_back(e, h);
    }

    // Destroyed entities: free their bodies.
    mTracked.erase(std::remove_if(mTracked.begin(), mTracked.end(),
        [&](auto& pair) {
            if (mReg.valid(pair.first)) return false;
            mPhysics.removeBody(pair.second);
            return true;
        }), mTracked.end());
}

} // namespace game
```

- [ ] **Step 7: Confirm pass** — `cmake --build build --target physics_sync_tests && ctest --test-dir build -R physics_sync --output-on-failure` → `PhysicsSyncTests OK`.
  If `bodyCount()` semantics differ (e.g. it counts only dynamic bodies), switch the assertions to compare against `physics.bodyCount()` before/after consistently; if static bodies are not counted by `bodyCount()`, use the returned `BodyRef.handle.valid()` as the primary assertion and relax the count checks to `>= before`. Inspect `engine/src/Physics*.cpp` to confirm what `bodyCount()` includes before relaxing. Keep the BodyRef + idempotency + removal assertions intact.

- [ ] **Step 8: Commit**
```bash
git add game/src/scene/RuntimeComponents.h game/src/scene/PhysicsSync.h \
        game/src/scene/PhysicsSync.cpp game/tests/PhysicsSyncTests.cpp CMakeLists.txt
git commit -m "feat(map): PhysicsSync reconciles Collider/Trigger to Jolt bodies"
```

---

## Task 2: MapRuntime (load + build + step)

**Files:**
- Create: `game/src/scene/MapRuntime.h`, `game/src/scene/MapRuntime.cpp`
- Test: `game/tests/MapRuntimeTests.cpp`

`MapRuntime` owns the registry, a `SceneSync` (over a caller-supplied `SceneBackend`), and a `PhysicsSync`. `load(path)` reads a `.map`; `resolveMeshes(loadFn)` fills `MeshRenderer.mesh` handles from `MeshSource` paths using a caller callback (so the runtime stays renderer-agnostic and testable); `buildAll()` runs both syncs; `step(dt)` advances physics + re-syncs; `playerSpawn()` returns the `PlayerSpawn` entity's position (or a default).

- [ ] **Step 1: Failing test** — `game/tests/MapRuntimeTests.cpp`:

```cpp
#include "MapRuntime.h"

#include "GameComponents.h"
#include "MeshSource.h"
#include "MapSerializer.h"

#include <eng/Physics.h>
#include <eng/ecs/Components.h>
#include <eng/ecs/SceneBackend.h>

#include <cstdio>
#include <cstdlib>
#include <iostream>

using namespace game;

static void require(bool c, const char* m)
{
    if (!c) { std::cerr << "MapRuntimeTests: " << m << '\n'; std::exit(1); }
}

struct MockBackend : eng::ecs::SceneBackend {
    int nodes = 0;
    uint32_t next = 1;
    eng::NodeHandle createNode(eng::NodeHandle, glm::vec3, const std::string&) override
    { ++nodes; return eng::NodeHandle{next++}; }
    void setPosition(eng::NodeHandle, glm::vec3) override {}
    void setOrientation(eng::NodeHandle, glm::quat) override {}
    void setScale(eng::NodeHandle, glm::vec3) override {}
    void destroyNode(eng::NodeHandle) override {}
    void attachMesh(eng::NodeHandle, eng::MeshHandle, const std::string&, bool) override {}
    eng::LightHandle attachLight(eng::NodeHandle, const eng::LightDesc&) override
    { return eng::LightHandle{next++}; }
};

int main()
{
    const std::string path = "map_runtime_test.map";

    // Author a tiny map: a floor mesh with a collider, plus a player spawn.
    {
        entt::registry reg;
        entt::entity floor = reg.create();
        reg.emplace<eng::ecs::Transform>(floor, eng::ecs::Transform{});
        reg.emplace<mapio::MeshSource>(floor, mapio::MeshSource{"meshes/tiles/floor.obj"});
        reg.emplace<eng::ecs::MeshRenderer>(floor, eng::ecs::MeshRenderer{});
        reg.emplace<Collider>(floor, Collider{eng::ShapeKind::Box, glm::vec3(4, 0.5f, 4),
                                              eng::BodyLayer::Static});
        entt::entity spawn = reg.create();
        eng::ecs::Transform st; st.position = glm::vec3(3, 1, -2);
        reg.emplace<eng::ecs::Transform>(spawn, st);
        reg.emplace<PlayerSpawn>(spawn);
        require(mapio::writeMap(path, reg, mapio::coreRegistry()), "author map");
    }

    eng::Physics physics;
    physics.init();
    MockBackend backend;
    MapRuntime rt(backend, physics);

    require(rt.load(path), "load .map");
    int meshLoads = 0;
    rt.resolveMeshes([&](const std::string&) { ++meshLoads; return eng::MeshHandle{42}; });
    require(meshLoads == 1, "resolveMeshes called once per MeshRenderer");

    const int beforeBodies = physics.bodyCount();
    rt.buildAll();
    require(backend.nodes >= 2, "buildAll created render nodes");
    require(physics.bodyCount() == beforeBodies + 1, "buildAll created the collider body");

    glm::vec3 sp = rt.playerSpawn();
    require(sp == glm::vec3(3, 1, -2), "playerSpawn returns the marker position");

    rt.step(1.0f / 60.0f); // must not crash
    physics.shutdown();
    std::remove(path.c_str());
    std::cout << "MapRuntimeTests OK\n";
    return 0;
}
```

- [ ] **Step 2: CMake target** under `if(BUILD_TESTING)`:

```cmake
  add_executable(map_runtime_tests
    game/tests/MapRuntimeTests.cpp
    game/src/scene/MapRuntime.cpp
    game/src/scene/PhysicsSync.cpp
    game/src/scene/MapSerializer.cpp
    game/src/scene/ComponentRegistry.cpp
    game/src/scene/ByteStream.cpp
    engine/src/ecs/Scene.cpp
    engine/src/ecs/SceneSync.cpp)
  target_include_directories(map_runtime_tests
    PRIVATE game/src/scene engine/include engine/src third_party third_party/imgui)
  target_link_libraries(map_runtime_tests PRIVATE eng EnTT::EnTT glm::glm)
  add_test(NAME map_runtime COMMAND map_runtime_tests)
```
Reconfigure.

- [ ] **Step 3: Confirm fail.**

- [ ] **Step 4: `MapRuntime.h`:**

```cpp
#pragma once

#include "PhysicsSync.h"

#include <eng/ecs/Scene.h>
#include <eng/ecs/SceneSync.h>
#include <eng/Handles.h>

#include <entt/entt.hpp>
#include <functional>
#include <glm/glm.hpp>
#include <string>

namespace eng { class Physics; }
namespace eng::ecs { class SceneBackend; }

namespace game {

// Loads a .map into a registry and drives it: SceneSync builds render nodes,
// PhysicsSync builds Jolt bodies. Renderer-agnostic — mesh handles are resolved
// through a caller callback so the runtime is unit-testable headless.
class MapRuntime {
public:
    MapRuntime(eng::ecs::SceneBackend& backend, eng::Physics& physics);

    bool load(const std::string& path);        // readMap into the registry
    // Fill MeshRenderer.mesh from each entity's MeshSource path via loadFn.
    using LoadMeshFn = std::function<eng::MeshHandle(const std::string& path)>;
    void resolveMeshes(const LoadMeshFn& loadFn);
    void buildAll();                            // SceneSync + PhysicsSync once
    void step(float dt);                        // physics.update + re-sync
    glm::vec3 playerSpawn() const;              // PlayerSpawn pos or (0,1,0)

    entt::registry& registry() { return mScene.registry(); }

private:
    eng::ecs::Scene mScene;
    eng::ecs::SceneSync mSceneSync;
    PhysicsSync mPhysicsSync;
    eng::Physics& mPhysics;
};

} // namespace game
```

- [ ] **Step 5: `MapRuntime.cpp`:**

```cpp
#include "MapRuntime.h"

#include "MapSerializer.h"
#include "MeshSource.h"
#include "GameComponents.h"

#include <eng/Physics.h>
#include <eng/ecs/Components.h>

namespace game {

MapRuntime::MapRuntime(eng::ecs::SceneBackend& backend, eng::Physics& physics)
    : mSceneSync(mScene, backend), mPhysicsSync(mScene.registry(), physics),
      mPhysics(physics)
{}

bool MapRuntime::load(const std::string& path)
{
    entt::registry& reg = mScene.registry();
    reg.clear();
    if (!mapio::readMap(path, reg, mapio::coreRegistry())) return false;
    // readMap creates entities directly; tag them Dirty so SceneSync pushes an
    // initial transform, and they already lack NodeRef so nodes get built.
    for (auto e : reg.view<eng::ecs::Transform>())
        reg.emplace_or_replace<eng::ecs::Dirty>(e);
    return true;
}

void MapRuntime::resolveMeshes(const LoadMeshFn& loadFn)
{
    entt::registry& reg = mScene.registry();
    for (auto e : reg.view<eng::ecs::MeshRenderer, mapio::MeshSource>()) {
        auto& mr = reg.get<eng::ecs::MeshRenderer>(e);
        mr.mesh = loadFn(reg.get<mapio::MeshSource>(e).path);
    }
}

void MapRuntime::buildAll()
{
    mSceneSync.sync();
    mPhysicsSync.sync();
}

void MapRuntime::step(float dt)
{
    mPhysics.update(dt);
    mSceneSync.sync();
    mPhysicsSync.sync();
}

glm::vec3 MapRuntime::playerSpawn() const
{
    const entt::registry& reg = mScene.registry();
    for (auto e : reg.view<const PlayerSpawn, const eng::ecs::Transform>())
        return reg.get<const eng::ecs::Transform>(e).position;
    return glm::vec3(0.0f, 1.0f, 0.0f);
}

} // namespace game
```
Note: confirm `entt`'s multi-component `view<A, B>()` iteration compiles in this version (Plan 1 used `storage<entt::entity>()` because `view<entt::entity>` was unavailable, but typed multi-views are the normal, supported API). If `view<const PlayerSpawn, const eng::ecs::Transform>` errors, use `reg.view<PlayerSpawn>()` then `try_get<eng::ecs::Transform>`.

- [ ] **Step 6: Confirm pass** — `cmake --build build --target map_runtime_tests && ctest --test-dir build -R map_runtime --output-on-failure` → `MapRuntimeTests OK`.

- [ ] **Step 7: Commit**
```bash
git add game/src/scene/MapRuntime.h game/src/scene/MapRuntime.cpp \
        game/tests/MapRuntimeTests.cpp CMakeLists.txt
git commit -m "feat(map): MapRuntime loads + builds + steps a .map scene"
```

---

## Task 3: `playMap()` + main.cpp hook (walk a .map)

**Files:**
- Create: `game/src/MapPlay.h`, `game/src/MapPlay.cpp`
- Modify: `game/src/main.cpp` (top-of-`main` dispatch), `CMakeLists.txt` (`game` sources)
- Verify: build `game` + headless screenshot run on a `.map`

This is the integration + play loop, verified by build + a `PSX_SCREENSHOT` run rather than a unit test.

- [ ] **Step 1: `MapPlay.h`:**

```cpp
#pragma once
#include <string>

namespace eng { class Engine; class Physics; }

namespace game {
// Dedicated play loop for an authored .map: builds the scene via MapRuntime and
// walks it with the FpsController. Returns a process exit code. Separate from
// the procedural DungeonMap boot in main().
int playMap(eng::Engine& engine, eng::Physics& physics,
            const std::string& assetDir, const std::string& mapPath);
}
```

- [ ] **Step 2: `MapPlay.cpp`.** Implement using the confirmed APIs (`eng::Engine::tick/renderFrame/shouldClose/requestClose/input/renderer/debugUi`, `eng::Renderer::loadObj`, `eng::ecs::RendererSceneBackend`, `FpsController::init/update/present/eyePosition`, `MapRuntime`). Structure:
  1. Construct `eng::ecs::RendererSceneBackend backend(engine.renderer())` and `MapRuntime rt(backend, physics)`.
  2. `if (!rt.load(mapPath)) { eng::log::error(...); return 1; }`
  3. `rt.resolveMeshes([&](const std::string& p){ return engine.renderer().loadObj(assetDir + "/" + p); });` — mesh paths are stored relative to the asset dir (the editor scanned `assetDir + "/meshes/..."`; strip or prefix consistently — verify by inspecting a saved path and matching what `loadObj` expects; the editor's `spawnMesh` stored the full path it scanned, so if paths are already absolute, pass them through unchanged).
  4. `rt.buildAll();`
  5. A light + ambient so the scene is lit (reuse the lighting setup from `editor_main.cpp`: a directional key light through the renderer, `setAmbient`, `setBackground`) — only if the map has no `LightRef` entities.
  6. `FpsController player; player.init(engine.renderer(), physics, rt.playerSpawn(), /*speed*/6.0f, /*sens*/0.0025f, roomMin, roomMax);` with generous room bounds (e.g. ±500).
  7. `engine.input().setMouseGrab(true);` Loop: `while(!engine.shouldClose()){ dt=engine.tick(); if(input.wasPressed("quit")) engine.requestClose(); player.update(engine.input(), engine.renderer(), dt); rt.step(dt); player.present(engine.renderer()); engine.renderFrame(dt);}` then `return 0;`. (`rt.step` already calls `physics.update`; do NOT also step physics elsewhere. Confirm `FpsController::update` expects physics to be stepped by the caller — inspect `FpsController.cpp`; if it steps the character internally via `physics.characterUpdate`, order `player.update` before `rt.step` as shown so the character move is applied then the world advances. If FpsController needs `physics.update` called before reading character state, adjust the order to match how `main.cpp` sequences `physics.update` vs `player.update` — READ main.cpp's main loop and mirror its ordering exactly.)

- [ ] **Step 3: Hook `main.cpp`.** Read `game/src/main.cpp`'s `int main(int argc, char** argv)` signature and the point right after `engine.init(...)` and `eng::Physics physics; physics.init();`. Insert, as early as possible after physics init and asset dir resolution:

```cpp
    // Authored-map play path: `game <file.map>` bypasses the procedural boot.
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg.size() > 4 && arg.substr(arg.size() - 4) == ".map") {
            const int rc = game::playMap(engine, physics, assets, arg);
            engine.shutdown();
            return rc;
        }
    }
```
Place it so `engine`, `physics`, and the asset-dir string (named `assets` or similar — match the real variable) are already constructed. Add `#include "MapPlay.h"` to main.cpp's includes. If `main` currently has no `argc/argv` parameters, change its signature to `int main(int argc, char** argv)`.

- [ ] **Step 4: CMake.** Add `game/src/MapPlay.cpp`, `game/src/scene/PhysicsSync.cpp`, `game/src/scene/MapRuntime.cpp`, and the Plan-1 `game/src/scene/{ByteStream,ComponentRegistry,MapSerializer}.cpp` to the `game` target's `add_executable` list (if not already compiled there). Ensure the `game` target's `target_include_directories` includes `game/src/scene` and `engine/src` (for `RendererSceneBackend.h`). Check whether `game` already lists any `game/src/scene/*` — the Plan-1/Plan-2 work may not have added them to `game`; add whatever is missing so the new code links.

- [ ] **Step 5: Build** — `cmake -S ... -B build && cmake --build build --target game`. Fix compile/link errors by matching real signatures (FpsController init params, log API). Do not alter tested-unit headers.

- [ ] **Step 6: Author a test map, then play it headless.** Generate a `.map` with the editor's serializer via a tiny throwaway, OR reuse the editor: run `PSX_DEBUG_UI=1 SDL_VIDEODRIVER=x11 ./build/level_editor` is interactive — instead, write a 20-line temporary program is overkill. Simplest: the `map_runtime_tests` binary already writes `map_runtime_test.map` during its run in the build dir; copy that, OR add a `--emit <path>` isn't in scope. Use this concrete approach: from the repo root run the existing test to produce a map file is unreliable (it deletes it). Instead, in this step create the map by scripting the editor is not headless. **Do this:** write the map inline using the serializer through a one-off `printf`-free helper is unnecessary — reuse `MapRuntimeTests`' authored map by temporarily NOT deleting it:
  - Run: `cd build && ./map_runtime_tests` — then immediately `ls map_runtime_test.map` (it is removed at exit). Since it is deleted, instead copy the authoring block: create `/tmp/make_map.cpp` that includes the scene headers and writes `/tmp/smoke.map` with one floor+collider+PlayerSpawn (same as the Task-2 test authoring block), compile it against the scene sources, run it. Then:
  - `PSX_SCREENSHOT=/tmp/play.png PSX_DEBUG_UI=1 SDL_VIDEODRIVER=x11 ./build/game /tmp/smoke.map` — expect exit 0 and `/tmp/play.png` written showing the floor from the player's eye. Read the PNG to confirm. Debug via `ogre.log` on failure.

  (If producing the smoke map is awkward, an acceptable alternative acceptance is: `./build/game /tmp/smoke.map` loads without crashing and writes the screenshot; the visual need only show the lit floor + HUD, proving load→build→render→physics all ran.)

- [ ] **Step 7: Full suite** — `ctest --test-dir build --output-on-failure`. All pass.

- [ ] **Step 8: Commit**
```bash
git add game/src/MapPlay.h game/src/MapPlay.cpp game/src/main.cpp CMakeLists.txt
git commit -m "feat(game): play authored .map via MapRuntime (game <file.map>)"
```

---

## Self-Review

**Spec coverage (against `2026-07-23-level-editor-app-design.md`, runtime slice):**
- `.map` → registry at runtime (`readMap`) → Task 2 `MapRuntime::load`. ✓
- `SceneSync` builds render nodes → Task 2 `buildAll`/`step`. ✓
- `PhysicsSync` builds Jolt bodies for `Collider`/`Trigger` → Task 1. ✓
- `BodyRef` runtime component, not serialized → Task 1. ✓
- Player spawns at `PlayerSpawn` → Task 2 `playerSpawn` + Task 3 `FpsController`. ✓
- Replaces `DungeonMap` for hand-authored levels (as a parallel path, procedural untouched) → Task 3. ✓
- Editor Play launches `game <map>` (Plan 2) now actually plays → Task 3 hook. ✓
- Deferred: trigger event dispatch/gameplay reactions, enemy/pickup spawning behavior (markers load but spawning enemies/loot is gameplay wiring for a later pass); mesh-accurate collider from mesh bounds (uses authored Collider). Noted.

**Type consistency:** `game::BodyRef`, `game::PhysicsSync(reg,physics).sync()`, `game::MapRuntime(backend,physics)` with `load/resolveMeshes/buildAll/step/playerSpawn/registry`, `game::playMap(engine,physics,assetDir,mapPath)`. `mapio::readMap/writeMap/coreRegistry`, `mapio::MeshSource`, `eng::ecs::{Transform,MeshRenderer,Dirty}`, `eng::Physics::{createBody,removeBody,update,bodyCount,init,shutdown}`, `eng::BodyDesc` fields, `eng::ShapeKind`/`eng::BodyLayer` used consistently and matched to the real headers read during planning.

**Placeholder scan:** Tasks 1–2 are complete code. Task 3 is integration specified with exact APIs + a screenshot acceptance (the smoke-map authoring is described concretely with a fallback). No `TODO`/`TBD` in committed steps. The only latitude is Task-3's physics-vs-controller ordering, which is explicitly instructed to mirror `main.cpp`'s existing loop after reading it.
```
