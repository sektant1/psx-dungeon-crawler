# Level Editor — Plan 2: Viewport & Interaction Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Turn the `level_editor` executable into a 3D scene editor that authors a `.map` file: a live ECS-backed viewport, click-to-select picking, a translate/rotate/scale gizmo, an Outliner + Inspector, an asset/entity palette, undo/redo, and save/open of the Plan-1 `.map` format, with a Play button that launches the game on the current map.

**Architecture:** The editor owns an `eng::ecs::Scene` (entt registry) rendered live into the RTT viewport by `eng::ecs::SceneSync` over `eng::ecs::RendererSceneBackend`. All editing mutates the registry through `Command` objects on a `CommandStack` (undo/redo). Picking, the gizmo, and the free-fly camera are pure math units, unit-tested headless. The Inspector is an editor-side `InspectorRegistry` (ImGui), kept separate from the Plan-1 GUI-free `mapio::ComponentRegistry`. Save/open reuse `mapio::writeMap`/`readMap` from Plan 1.

**Tech Stack:** C++17, EnTT, GLM, Dear ImGui (Ogre's bundled copy), the `eng` engine API, CMake + CTest. Tests are the repo's `int main()` + `require(cond,msg)` style.

**Design decisions (deviations from the spec, with rationale):**
- **Custom gizmo instead of ImGuizmo.** The spec named ImGuizmo, but vendoring it needs a network fetch unavailable to implementers, and a hand-rolled gizmo (axis handles drawn with `Renderer::DebugLine`, dragged via ray↔axis closest-point math) makes the interaction math unit-testable. Same capability (T/R/S + snap), no new dependency.
- **Inspector GUI is editor-side.** Adding an ImGui `drawInspector` to `mapio::ComponentType` would pull ImGui into the Plan-1 core and break its GUI-free unit tests. Instead an editor-only `InspectorRegistry` maps `stableTypeId → draw fn`. The Plan-1 `ComponentRegistry` is unchanged.

---

## File Structure

New directory `game/src/editor/` (compiled only into `level_editor`):
- `EditorScene.h/.cpp` — owns `eng::ecs::Scene` + `SceneSync` + `RendererSceneBackend`; spawn helpers; `sync()`.
- `Picker.h/.cpp` — screen ray build + ray/AABB pick over entity world bounds. Pure math.
- `Gizmo.h/.cpp` — gizmo state machine + ray↔axis / ray↔plane math + `DebugLine` geometry. Pure math + a draw-data producer.
- `Selection.h` — selected-entity set (header-only, trivial).
- `CommandStack.h/.cpp` + `Commands.h/.cpp` — undo/redo command objects over the registry.
- `InspectorRegistry.h/.cpp` — `stableTypeId → bool draw(registry&,entity)` ImGui inspectors.
- `Palette.h/.cpp` — the create-entity palette (mesh/light/marker/trigger) + asset `.obj` discovery.
- `EditorApp.h/.cpp` — top-level wiring: viewport, input routing, panels, gizmo render, save/open, Play.

Modified:
- `game/src/EditorCamera.h/.cpp` — add free-fly controls (`moveLocal`, `addYawPitch`, `flyPose`).
- `game/src/editor_main.cpp` — reduced to constructing `EditorApp` and running the loop.
- `CMakeLists.txt` — `level_editor` gains the new sources + `engine/src` + `game/src` includes + the Plan-1 `game/src/scene/*` sources; new test targets.

Tests (`game/tests/`): `PickerTests.cpp`, `GizmoMathTests.cpp`, `EditorCommandTests.cpp`, `EditorCameraFlyTests.cpp`, `InspectorRegistryTests.cpp`.

---

## Task 1: EditorCamera free-fly controls

**Files:**
- Modify: `game/src/EditorCamera.h`, `game/src/EditorCamera.cpp`
- Test: `game/tests/EditorCameraFlyTests.cpp`

The current `EditorCamera` is orbit-only (`orbit/dolly/pan/frame/eye/orientation`). Add a free-fly mode: yaw/pitch look and local-space movement, sharing the existing `mYaw/mPitch`. `flyPose` returns eye+orientation from a free position (not orbit-derived).

- [ ] **Step 1: Write the failing test** — create `game/tests/EditorCameraFlyTests.cpp`:

```cpp
#include "EditorCamera.h"

#include <glm/gtc/epsilon.hpp>

#include <cstdlib>
#include <iostream>

static void require(bool c, const char* m)
{
    if (!c) { std::cerr << "EditorCameraFlyTests: " << m << '\n'; std::exit(1); }
}

int main()
{
    EditorCamera cam;
    cam.setFlyPosition(glm::vec3(0, 0, 0));
    cam.setYawPitch(0.0f, 0.0f); // look down -Z by convention

    // Forward at yaw=0,pitch=0 is -Z; moving "forward" 2 units lands at (0,0,-2).
    cam.moveLocal(glm::vec3(0, 0, -2.0f));
    const glm::vec3 p = cam.flyEye();
    require(glm::all(glm::epsilonEqual(p, glm::vec3(0, 0, -2.0f), 1e-4f)),
            "forward move translates along view forward");

    // Yaw 90 deg (to the right, +Y axis rotation) then move right should change X.
    cam.setYawPitch(glm::radians(90.0f), 0.0f);
    const glm::vec3 before = cam.flyEye();
    cam.moveLocal(glm::vec3(1.0f, 0, 0)); // local +X = right
    const glm::vec3 after = cam.flyEye();
    require(glm::abs((after - before).z) > 0.5f,
            "right move after 90deg yaw translates along world Z");

    // Pitch clamps to just under +/- 90 deg to avoid gimbal flip.
    cam.setYawPitch(0.0f, glm::radians(89.0f));
    cam.addYawPitch(0.0f, glm::radians(20.0f));
    require(cam.pitch() < glm::radians(90.0f), "pitch clamps below +90 deg");

    std::cout << "EditorCameraFlyTests OK\n";
    return 0;
}
```

- [ ] **Step 2: Add the CMake target** in `CMakeLists.txt` under `if(BUILD_TESTING)`:

```cmake
  add_executable(editor_camera_fly_tests game/tests/EditorCameraFlyTests.cpp
                                         game/src/EditorCamera.cpp)
  target_include_directories(editor_camera_fly_tests PRIVATE game/src)
  target_link_libraries(editor_camera_fly_tests PRIVATE glm::glm)
  add_test(NAME editor_camera_fly COMMAND editor_camera_fly_tests)
```
Reconfigure: `cmake -S /home/sektant1/psx-dungeon-crawler -B /home/sektant1/psx-dungeon-crawler/build`

- [ ] **Step 3: Confirm it fails** — `cmake --build /home/sektant1/psx-dungeon-crawler/build --target editor_camera_fly_tests` → compile error (new methods missing).

- [ ] **Step 4: Extend the header.** In `game/src/EditorCamera.h`, add to the public section (after `frame(...)`):

```cpp
    // --- free-fly mode (independent of orbit target) ---------------------
    void setFlyPosition(glm::vec3 pos) { mFlyPos = pos; }
    void setYawPitch(float yawRad, float pitchRad);
    void addYawPitch(float dYawRad, float dPitchRad);
    void moveLocal(glm::vec3 localDelta); // +X right, +Y up, -Z forward
    glm::vec3 flyEye() const { return mFlyPos; }
    glm::quat flyOrientation() const;     // from yaw/pitch
    float yaw() const { return mYaw; }
    float pitch() const { return mPitch; }
```
And add to the private section (after `mPitch`):
```cpp
    glm::vec3 mFlyPos{0.0f, 2.0f, 6.0f};
```

- [ ] **Step 5: Implement in `game/src/EditorCamera.cpp`.** Add:

```cpp
void EditorCamera::setYawPitch(float yawRad, float pitchRad)
{
    mYaw = yawRad;
    mPitch = glm::clamp(pitchRad, glm::radians(-89.9f), glm::radians(89.9f));
}

void EditorCamera::addYawPitch(float dYawRad, float dPitchRad)
{
    setYawPitch(mYaw + dYawRad, mPitch + dPitchRad);
}

glm::quat EditorCamera::flyOrientation() const
{
    // Yaw about world +Y, then pitch about local +X. Forward is -Z.
    return glm::angleAxis(mYaw, glm::vec3(0, 1, 0)) *
           glm::angleAxis(mPitch, glm::vec3(1, 0, 0));
}

void EditorCamera::moveLocal(glm::vec3 localDelta)
{
    mFlyPos += flyOrientation() * localDelta;
}
```
Ensure `#include <glm/gtc/quaternion.hpp>` and `#include <glm/common.hpp>` (for `glm::clamp`) are present in the .cpp (add if missing).

- [ ] **Step 6: Confirm pass** — `cmake --build /home/sektant1/psx-dungeon-crawler/build --target editor_camera_fly_tests && ctest --test-dir /home/sektant1/psx-dungeon-crawler/build -R editor_camera_fly --output-on-failure` → `EditorCameraFlyTests OK`.

- [ ] **Step 7: Commit**
```bash
git add game/src/EditorCamera.h game/src/EditorCamera.cpp game/tests/EditorCameraFlyTests.cpp CMakeLists.txt
git commit -m "feat(editor): free-fly controls on EditorCamera"
```

---

## Task 2: Picker (screen ray + ray/AABB pick)

**Files:**
- Create: `game/src/editor/Picker.h`, `game/src/editor/Picker.cpp`
- Test: `game/tests/PickerTests.cpp`

Pure math: build a world-space ray from a normalized viewport coordinate + camera pose + vertical FOV + aspect, and intersect a ray with an axis-aligned box. No engine/entt dependency, so it is trivially testable.

- [ ] **Step 1: Failing test** — create `game/tests/PickerTests.cpp`:

```cpp
#include "Picker.h"

#include <glm/gtc/epsilon.hpp>

#include <cstdlib>
#include <iostream>

using namespace editor;

static void require(bool c, const char* m)
{
    if (!c) { std::cerr << "PickerTests: " << m << '\n'; std::exit(1); }
}

int main()
{
    // Camera at origin looking down -Z, 60deg vfov, square aspect.
    Ray centre = screenRay({0.0f, 0.0f}, glm::vec3(0, 0, 0),
                           glm::quat(1, 0, 0, 0), glm::radians(60.0f), 1.0f);
    require(glm::all(glm::epsilonEqual(centre.origin, glm::vec3(0), 1e-5f)),
            "ray origin is the camera position");
    require(centre.dir.z < -0.9f, "centre ray points down -Z");

    // A unit box centred 5 units ahead is hit by the centre ray.
    float t = 0.0f;
    require(rayAabb(centre, glm::vec3(-1, -1, -6), glm::vec3(1, 1, -4), t),
            "centre ray hits a box straight ahead");
    require(t > 3.5f && t < 4.5f, "hit distance is the near face (~4)");

    // A box off to the far right is missed by the centre ray.
    float t2 = 0.0f;
    require(!rayAabb(centre, glm::vec3(100, -1, -6), glm::vec3(102, 1, -4), t2),
            "centre ray misses an off-axis box");

    std::cout << "PickerTests OK\n";
    return 0;
}
```

- [ ] **Step 2: CMake target** under `if(BUILD_TESTING)`:

```cmake
  add_executable(picker_tests game/tests/PickerTests.cpp
                              game/src/editor/Picker.cpp)
  target_include_directories(picker_tests PRIVATE game/src/editor)
  target_link_libraries(picker_tests PRIVATE glm::glm)
  add_test(NAME picker COMMAND picker_tests)
```
Reconfigure with cmake -S/-B.

- [ ] **Step 3: Confirm fail** — build target, expect missing `Picker.h`.

- [ ] **Step 4: Header** — `game/src/editor/Picker.h`:

```cpp
#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace editor {

struct Ray {
    glm::vec3 origin{0.0f};
    glm::vec3 dir{0.0f, 0.0f, -1.0f}; // normalized
};

// Build a world ray through a normalized viewport point ndc in [-1,1]^2
// (x right, y up), given the camera pose, vertical FOV (radians) and aspect
// (width/height). Camera looks down its local -Z with +Y up.
Ray screenRay(glm::vec2 ndc, glm::vec3 camPos, glm::quat camOrient,
              float vFovRad, float aspect);

// Ray vs axis-aligned box [mn,mx]. On hit, sets tHit to the entry distance
// (>= 0) and returns true. A ray starting inside the box hits at t=0.
bool rayAabb(const Ray& r, glm::vec3 mn, glm::vec3 mx, float& tHit);

} // namespace editor
```

- [ ] **Step 5: Implementation** — `game/src/editor/Picker.cpp`:

```cpp
#include "Picker.h"

#include <algorithm>
#include <cmath>

namespace editor {

Ray screenRay(glm::vec2 ndc, glm::vec3 camPos, glm::quat camOrient,
              float vFovRad, float aspect)
{
    const float tanHalf = std::tan(vFovRad * 0.5f);
    // View-space direction on the near image plane (forward is -Z).
    const glm::vec3 viewDir(ndc.x * tanHalf * aspect, ndc.y * tanHalf, -1.0f);
    Ray r;
    r.origin = camPos;
    r.dir = glm::normalize(camOrient * viewDir);
    return r;
}

bool rayAabb(const Ray& r, glm::vec3 mn, glm::vec3 mx, float& tHit)
{
    float tmin = 0.0f;
    float tmax = 1e30f;
    for (int i = 0; i < 3; ++i) {
        const float o = r.origin[i], d = r.dir[i];
        if (std::fabs(d) < 1e-8f) {
            if (o < mn[i] || o > mx[i]) return false; // parallel & outside
        } else {
            float t1 = (mn[i] - o) / d;
            float t2 = (mx[i] - o) / d;
            if (t1 > t2) std::swap(t1, t2);
            tmin = std::max(tmin, t1);
            tmax = std::min(tmax, t2);
            if (tmin > tmax) return false;
        }
    }
    tHit = tmin;
    return true;
}

} // namespace editor
```

- [ ] **Step 6: Confirm pass** — build + `ctest -R picker` → `PickerTests OK`.

- [ ] **Step 7: Commit**
```bash
git add game/src/editor/Picker.h game/src/editor/Picker.cpp game/tests/PickerTests.cpp CMakeLists.txt
git commit -m "feat(editor): screen-ray + ray/AABB picker math"
```

---

## Task 3: Gizmo math (axis translate / plane drag)

**Files:**
- Create: `game/src/editor/Gizmo.h`, `game/src/editor/Gizmo.cpp`
- Test: `game/tests/GizmoMathTests.cpp`

The gizmo's interaction reduces to two math primitives: the closest point on a translation axis to the pick ray (for axis drag), and the intersection of the pick ray with an axis-aligned plane (for planar/rotation math). This task ships those, plus a `GizmoMode` enum. Rendering + full state machine land in the EditorApp task; here we prove the math.

- [ ] **Step 1: Failing test** — `game/tests/GizmoMathTests.cpp`:

```cpp
#include "Gizmo.h"
#include "Picker.h"

#include <glm/gtc/epsilon.hpp>

#include <cstdlib>
#include <iostream>

using namespace editor;

static void require(bool c, const char* m)
{
    if (!c) { std::cerr << "GizmoMathTests: " << m << '\n'; std::exit(1); }
}

int main()
{
    // Axis along +X through origin; a ray from above pointing down crosses it
    // at approximately x = 3.
    Ray down; down.origin = glm::vec3(3, 5, 0); down.dir = glm::vec3(0, -1, 0);
    float axisT = 0.0f;
    require(closestPointOnAxis(glm::vec3(0), glm::vec3(1, 0, 0), down, axisT),
            "ray not parallel to axis yields a solution");
    require(std::abs(axisT - 3.0f) < 1e-3f, "closest axis param is ~3");

    // Ray straight down hits the y=0 plane (normal +Y) at that x,z.
    Ray d2; d2.origin = glm::vec3(2, 4, -1); d2.dir = glm::vec3(0, -1, 0);
    glm::vec3 hit;
    require(rayPlane(d2, glm::vec3(0), glm::vec3(0, 1, 0), hit),
            "ray hits the ground plane");
    require(glm::all(glm::epsilonEqual(hit, glm::vec3(2, 0, -1), 1e-4f)),
            "plane hit point is directly below the origin");

    // Snap rounds to the nearest step.
    require(std::abs(snap(1.24f, 0.25f) - 1.25f) < 1e-5f, "snap rounds to step");
    require(std::abs(snap(-0.10f, 0.25f) - 0.0f) < 1e-5f, "snap rounds toward zero");

    std::cout << "GizmoMathTests OK\n";
    return 0;
}
```

- [ ] **Step 2: CMake target** under `if(BUILD_TESTING)`:

```cmake
  add_executable(gizmo_math_tests game/tests/GizmoMathTests.cpp
                                  game/src/editor/Gizmo.cpp
                                  game/src/editor/Picker.cpp)
  target_include_directories(gizmo_math_tests PRIVATE game/src/editor)
  target_link_libraries(gizmo_math_tests PRIVATE glm::glm)
  add_test(NAME gizmo_math COMMAND gizmo_math_tests)
```
Reconfigure.

- [ ] **Step 3: Confirm fail.**

- [ ] **Step 4: Header** — `game/src/editor/Gizmo.h`:

```cpp
#pragma once

#include <glm/glm.hpp>

namespace editor {

struct Ray;

enum class GizmoMode { Translate, Rotate, Scale };

// Parameter t along the infinite axis (origin + t*dir, dir need not be unit)
// of the point closest to the ray. Returns false if ray and axis are parallel.
bool closestPointOnAxis(glm::vec3 axisOrigin, glm::vec3 axisDir,
                        const Ray& ray, float& t);

// Intersect ray with the plane through planePoint with the given normal.
// Returns false if the ray is parallel to the plane.
bool rayPlane(const Ray& ray, glm::vec3 planePoint, glm::vec3 normal,
              glm::vec3& hit);

// Round v to the nearest multiple of step (step<=0 returns v unchanged).
float snap(float v, float step);

} // namespace editor
```

- [ ] **Step 5: Implementation** — `game/src/editor/Gizmo.cpp`:

```cpp
#include "Gizmo.h"

#include "Picker.h"

#include <cmath>

namespace editor {

bool closestPointOnAxis(glm::vec3 axisOrigin, glm::vec3 axisDir,
                        const Ray& ray, float& t)
{
    // Closest points between two lines: axis P(t)=axisOrigin+t*u, ray Q(s)=o+s*v.
    const glm::vec3 u = axisDir;
    const glm::vec3 v = ray.dir;
    const glm::vec3 w0 = axisOrigin - ray.origin;
    const float a = glm::dot(u, u);
    const float b = glm::dot(u, v);
    const float c = glm::dot(v, v);
    const float d = glm::dot(u, w0);
    const float e = glm::dot(v, w0);
    const float denom = a * c - b * b;
    if (std::fabs(denom) < 1e-8f) return false; // parallel
    t = (b * e - c * d) / denom;
    return true;
}

bool rayPlane(const Ray& ray, glm::vec3 planePoint, glm::vec3 normal,
              glm::vec3& hit)
{
    const float denom = glm::dot(normal, ray.dir);
    if (std::fabs(denom) < 1e-8f) return false;
    const float t = glm::dot(normal, planePoint - ray.origin) / denom;
    if (t < 0.0f) return false;
    hit = ray.origin + t * ray.dir;
    return true;
}

float snap(float v, float step)
{
    if (step <= 0.0f) return v;
    return std::round(v / step) * step;
}

} // namespace editor
```

- [ ] **Step 6: Confirm pass** — `ctest -R gizmo_math` → `GizmoMathTests OK`.

- [ ] **Step 7: Commit**
```bash
git add game/src/editor/Gizmo.h game/src/editor/Gizmo.cpp game/tests/GizmoMathTests.cpp CMakeLists.txt
git commit -m "feat(editor): gizmo interaction math (axis, plane, snap)"
```

---

## Task 4: EditorScene (registry + SceneSync + spawn helpers)

**Files:**
- Create: `game/src/editor/EditorScene.h`, `game/src/editor/EditorScene.cpp`
- Test: `game/tests/EditorSceneTests.cpp` (uses a mock backend, no renderer)

`EditorScene` owns the `eng::ecs::Scene` and drives a `SceneBackend` via `SceneSync`. In the app it is constructed with a `RendererSceneBackend`; in tests with a recording mock. It exposes spawn helpers that create an entity with the right components + a `MeshSource`, world-bounds lookup for the picker, and `sync()`.

- [ ] **Step 1: Failing test** — `game/tests/EditorSceneTests.cpp`:

```cpp
#include "EditorScene.h"

#include "GameComponents.h"
#include "MeshSource.h"

#include <eng/ecs/Components.h>
#include <eng/ecs/SceneBackend.h>

#include <cstdlib>
#include <iostream>

using namespace editor;

static void require(bool c, const char* m)
{
    if (!c) { std::cerr << "EditorSceneTests: " << m << '\n'; std::exit(1); }
}

// Recording backend: counts node/mesh/light creations so we can assert sync
// happened without a real renderer.
struct MockBackend : eng::ecs::SceneBackend {
    int nodes = 0, meshes = 0, lights = 0;
    uint32_t next = 1;
    eng::NodeHandle createNode(eng::NodeHandle, glm::vec3, const std::string&) override
    { ++nodes; return eng::NodeHandle{next++}; }
    void setPosition(eng::NodeHandle, glm::vec3) override {}
    void setOrientation(eng::NodeHandle, glm::quat) override {}
    void setScale(eng::NodeHandle, glm::vec3) override {}
    void destroyNode(eng::NodeHandle) override {}
    void attachMesh(eng::NodeHandle, eng::MeshHandle, const std::string&, bool) override
    { ++meshes; }
    eng::LightHandle attachLight(eng::NodeHandle, const eng::LightDesc&) override
    { ++lights; return eng::LightHandle{next++}; }
};

int main()
{
    MockBackend backend;
    EditorScene scene(backend);

    entt::entity e = scene.spawnMesh("meshes/tiles/floor.obj", "Game/DungeonTile",
                                     glm::vec3(1, 0, 2));
    require(scene.registry().all_of<eng::ecs::Transform>(e), "mesh entity has a transform");
    require(scene.registry().all_of<mapio::MeshSource>(e), "mesh entity records its source path");
    require(scene.registry().get<mapio::MeshSource>(e).path == "meshes/tiles/floor.obj",
            "source path stored");

    entt::entity l = scene.spawnLight(eng::LightDesc{}, glm::vec3(0, 3, 0));
    require(scene.registry().all_of<eng::ecs::LightRef>(l), "light entity has a light ref");

    scene.sync();
    require(backend.nodes >= 2, "sync created a node per entity");
    require(backend.meshes == 1 && backend.lights == 1, "sync attached mesh + light once");

    // Bounds of the mesh entity fall back to a unit box around its position.
    glm::vec3 mn, mx;
    require(scene.entityBounds(e, mn, mx), "mesh entity has bounds");
    require(mn.x < 1.0f && mx.x > 1.0f, "bounds straddle the entity position x=1");

    std::cout << "EditorSceneTests OK\n";
    return 0;
}
```

- [ ] **Step 2: CMake target** under `if(BUILD_TESTING)`:

```cmake
  add_executable(editor_scene_tests
    game/tests/EditorSceneTests.cpp
    game/src/editor/EditorScene.cpp
    engine/src/ecs/Scene.cpp
    engine/src/ecs/SceneSync.cpp)
  target_include_directories(editor_scene_tests
    PRIVATE game/src/editor game/src/scene engine/include engine/src third_party/imgui)
  target_link_libraries(editor_scene_tests PRIVATE glm::glm EnTT::EnTT)
  add_test(NAME editor_scene COMMAND editor_scene_tests)
```
Reconfigure. (Scene.cpp + SceneSync.cpp are the real engine ECS sources — the same ones the engine's own `scene_sync_tests` compile, so this is a proven, renderer-free combination.)

- [ ] **Step 3: Confirm fail.**

- [ ] **Step 4: Header** — `game/src/editor/EditorScene.h`:

```cpp
#pragma once

#include <eng/ecs/Scene.h>
#include <eng/ecs/SceneSync.h>
#include <eng/LightDesc.h>

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include <string>

namespace eng::ecs { class SceneBackend; }

namespace editor {

// Owns the editor's ECS scene and reconciles it to a SceneBackend each frame.
// Spawn helpers create fully-formed authoring entities. entityBounds feeds the
// picker; for now it is a fixed box around the entity origin (mesh-accurate
// bounds are an EditorApp concern once a renderer handle exists).
class EditorScene {
public:
    explicit EditorScene(eng::ecs::SceneBackend& backend);

    eng::ecs::Scene& scene() { return mScene; }
    entt::registry& registry() { return mScene.registry(); }

    entt::entity spawnMesh(const std::string& objPath, const std::string& material,
                           glm::vec3 pos);
    entt::entity spawnLight(const eng::LightDesc& desc, glm::vec3 pos);
    entt::entity spawnMarker(glm::vec3 pos, const char* name); // empty transform node

    void sync() { mSync.sync(); }

    // Axis-aligned bounds for picking. halfExtent defaults to 0.5 (a 1m box).
    bool entityBounds(entt::entity e, glm::vec3& mn, glm::vec3& mx,
                      float halfExtent = 0.5f) const;

private:
    eng::ecs::Scene mScene;
    eng::ecs::SceneSync mSync;
};

} // namespace editor
```

- [ ] **Step 5: Implementation** — `game/src/editor/EditorScene.cpp`:

```cpp
#include "EditorScene.h"

#include "GameComponents.h"
#include "MeshSource.h"

#include <eng/ecs/Components.h>

namespace editor {

EditorScene::EditorScene(eng::ecs::SceneBackend& backend)
    : mSync(mScene, backend)
{}

entt::entity EditorScene::spawnMesh(const std::string& objPath,
                                    const std::string& material, glm::vec3 pos)
{
    entt::registry& r = registry();
    entt::entity e = mScene.create(objPath);
    eng::ecs::Transform t;
    t.position = pos;
    mScene.setLocalTransform(e, t);
    r.emplace<mapio::MeshSource>(e, mapio::MeshSource{objPath});
    eng::ecs::MeshRenderer mr;
    mr.material = material;
    r.emplace<eng::ecs::MeshRenderer>(e, mr);
    return e;
}

entt::entity EditorScene::spawnLight(const eng::LightDesc& desc, glm::vec3 pos)
{
    entt::registry& r = registry();
    entt::entity e = mScene.create("Light");
    eng::ecs::Transform t;
    t.position = pos;
    mScene.setLocalTransform(e, t);
    r.emplace<eng::ecs::LightRef>(e, eng::ecs::LightRef{desc, {}});
    return e;
}

entt::entity EditorScene::spawnMarker(glm::vec3 pos, const char* name)
{
    entt::entity e = mScene.create(name);
    eng::ecs::Transform t;
    t.position = pos;
    mScene.setLocalTransform(e, t);
    return e;
}

bool EditorScene::entityBounds(entt::entity e, glm::vec3& mn, glm::vec3& mx,
                               float halfExtent) const
{
    const entt::registry& r = mScene.registry();
    if (!r.all_of<eng::ecs::Transform>(e)) return false;
    const glm::vec3 p = r.get<eng::ecs::Transform>(e).position;
    mn = p - glm::vec3(halfExtent);
    mx = p + glm::vec3(halfExtent);
    return true;
}

} // namespace editor
```

Note: confirm `eng::ecs::Scene::create(std::string)` and `setLocalTransform(entt::entity, const Transform&)` signatures against `engine/include/eng/ecs/Scene.h` (Plan-1 exploration confirmed both exist). If `create` returns the entity and assigns a `Name`, the test's later name use is unaffected.

- [ ] **Step 6: Confirm pass** — `ctest -R editor_scene` → `EditorSceneTests OK`.

- [ ] **Step 7: Commit**
```bash
git add game/src/editor/EditorScene.h game/src/editor/EditorScene.cpp game/tests/EditorSceneTests.cpp CMakeLists.txt
git commit -m "feat(editor): EditorScene registry + SceneSync spawn helpers"
```

---

## Task 5: CommandStack + editing commands

**Files:**
- Create: `game/src/editor/CommandStack.h`, `game/src/editor/Commands.h`, `game/src/editor/Commands.cpp`
- Test: `game/tests/EditorCommandTests.cpp`

Every edit is a `Command` with `apply`/`revert` over an `entt::registry`. `CommandStack` runs a command and pushes it, and pops for undo/redo. Commands needed for v1: set-transform, add-component (default via `mapio::coreRegistry`), remove-component, delete-entity (restores all components on undo via a `.map`-style blob), and create-entity.

- [ ] **Step 1: Failing test** — `game/tests/EditorCommandTests.cpp`:

```cpp
#include "CommandStack.h"
#include "Commands.h"

#include <eng/ecs/Components.h>

#include <cstdlib>
#include <iostream>

using namespace editor;

static void require(bool c, const char* m)
{
    if (!c) { std::cerr << "EditorCommandTests: " << m << '\n'; std::exit(1); }
}

int main()
{
    entt::registry reg;
    CommandStack stack;

    // Create an entity via command; undo removes it, redo restores it.
    entt::entity created = entt::null;
    stack.run(makeCreateEntity(reg, "Box", &created));
    require(reg.valid(created), "create command makes a live entity");
    require(reg.all_of<eng::ecs::Transform>(created), "created entity has a transform");

    // Move it, then undo restores the old transform.
    eng::ecs::Transform moved;
    moved.position = glm::vec3(5, 0, 0);
    stack.run(makeSetTransform(reg, created, moved));
    require(reg.get<eng::ecs::Transform>(created).position == glm::vec3(5, 0, 0),
            "set-transform applies");
    require(stack.undo(), "undo available");
    require(reg.get<eng::ecs::Transform>(created).position == glm::vec3(0, 0, 0),
            "undo restores previous transform");
    require(stack.redo(), "redo available");
    require(reg.get<eng::ecs::Transform>(created).position == glm::vec3(5, 0, 0),
            "redo re-applies transform");

    // Delete then undo restores the entity's components.
    stack.run(makeDeleteEntity(reg, created));
    require(!reg.valid(created), "delete removes the entity");
    require(stack.undo(), "undo delete");
    // The restored entity keeps a transform at the last position.
    bool found = false;
    reg.view<eng::ecs::Transform>().each([&](entt::entity, const eng::ecs::Transform& t) {
        if (t.position == glm::vec3(5, 0, 0)) found = true;
    });
    require(found, "undo delete restores the entity with its components");

    std::cout << "EditorCommandTests OK\n";
    return 0;
}
```

- [ ] **Step 2: CMake target** under `if(BUILD_TESTING)`:

```cmake
  add_executable(editor_command_tests
    game/tests/EditorCommandTests.cpp
    game/src/editor/Commands.cpp
    game/src/scene/ByteStream.cpp
    game/src/scene/ComponentRegistry.cpp)
  target_include_directories(editor_command_tests
    PRIVATE game/src/editor game/src/scene engine/include third_party)
  target_link_libraries(editor_command_tests PRIVATE glm::glm EnTT::EnTT)
  add_test(NAME editor_command COMMAND editor_command_tests)
```
Reconfigure. (Delete/undo serializes the entity's components with the Plan-1 `ComponentRegistry` + `ByteStream`, hence those sources.)

- [ ] **Step 3: Confirm fail.**

- [ ] **Step 4: `CommandStack.h`:**

```cpp
#pragma once

#include <functional>
#include <memory>
#include <vector>

namespace editor {

// A reversible edit. apply() performs it; revert() undoes it. Commands capture
// whatever state they need at construction / first apply.
struct Command {
    std::function<void()> apply;
    std::function<void()> revert;
};

class CommandStack {
public:
    void run(Command c) {
        c.apply();
        mUndo.push_back(std::move(c));
        mRedo.clear();
    }
    bool undo() {
        if (mUndo.empty()) return false;
        Command c = std::move(mUndo.back());
        mUndo.pop_back();
        c.revert();
        mRedo.push_back(std::move(c));
        return true;
    }
    bool redo() {
        if (mRedo.empty()) return false;
        Command c = std::move(mRedo.back());
        mRedo.pop_back();
        c.apply();
        mUndo.push_back(std::move(c));
        return true;
    }
    bool canUndo() const { return !mUndo.empty(); }
    bool canRedo() const { return !mRedo.empty(); }
    void clear() { mUndo.clear(); mRedo.clear(); }

private:
    std::vector<Command> mUndo;
    std::vector<Command> mRedo;
};

} // namespace editor
```

- [ ] **Step 5: `Commands.h`:**

```cpp
#pragma once

#include "CommandStack.h"

#include <eng/ecs/Components.h>

#include <entt/entt.hpp>

#include <string>

namespace editor {

// Create an entity with a default Transform (+ Name). If outEntity is non-null,
// it receives the created entity id after apply().
Command makeCreateEntity(entt::registry& reg, std::string name,
                         entt::entity* outEntity);

// Replace the Transform on e; revert restores the prior Transform.
Command makeSetTransform(entt::registry& reg, entt::entity e,
                         eng::ecs::Transform next);

// Destroy e; revert re-creates it with the same components (serialized blob).
Command makeDeleteEntity(entt::registry& reg, entt::entity e);

} // namespace editor
```

- [ ] **Step 6: `Commands.cpp`:**

```cpp
#include "Commands.h"

#include "ByteStream.h"
#include "ComponentRegistry.h"

#include <memory>
#include <vector>

namespace editor {

Command makeCreateEntity(entt::registry& reg, std::string name,
                         entt::entity* outEntity)
{
    // Shared slot so apply/revert refer to the same (recreated) entity.
    auto slot = std::make_shared<entt::entity>(entt::null);
    std::string nm = std::move(name);
    Command c;
    c.apply = [&reg, slot, nm, outEntity] {
        entt::entity e = reg.create();
        reg.emplace<eng::ecs::Name>(e, eng::ecs::Name{nm});
        reg.emplace<eng::ecs::Transform>(e);
        *slot = e;
        if (outEntity) *outEntity = e;
    };
    c.revert = [&reg, slot] {
        if (reg.valid(*slot)) reg.destroy(*slot);
    };
    return c;
}

Command makeSetTransform(entt::registry& reg, entt::entity e,
                         eng::ecs::Transform next)
{
    auto prev = std::make_shared<eng::ecs::Transform>();
    Command c;
    c.apply = [&reg, e, next, prev] {
        if (reg.all_of<eng::ecs::Transform>(e))
            *prev = reg.get<eng::ecs::Transform>(e);
        reg.emplace_or_replace<eng::ecs::Transform>(e, next);
    };
    c.revert = [&reg, e, prev] {
        reg.emplace_or_replace<eng::ecs::Transform>(e, *prev);
    };
    return c;
}

Command makeDeleteEntity(entt::registry& reg, entt::entity e)
{
    // Capture the entity's components as a byte blob so undo can restore them.
    auto blob = std::make_shared<std::vector<uint8_t>>();
    auto pool = std::make_shared<std::vector<std::string>>();
    auto slot = std::make_shared<entt::entity>(e);
    Command c;
    c.apply = [&reg, slot, blob, pool] {
        mapio::ByteWriter w;
        const mapio::ComponentRegistry& types = mapio::coreRegistry();
        std::vector<const mapio::ComponentType*> present;
        for (const mapio::ComponentType& t : types.types())
            if (t.has(reg, *slot)) present.push_back(&t);
        w.u16(uint16_t(present.size()));
        for (const mapio::ComponentType* t : present) {
            w.u16(t->stableTypeId);
            t->serialize(reg, *slot, w);
        }
        *blob = w.bytes();
        *pool = w.pool();
        reg.destroy(*slot);
    };
    c.revert = [&reg, slot, blob, pool] {
        entt::entity e2 = reg.create();
        mapio::ByteReader r(blob->data(), blob->size(), *pool);
        const uint16_t count = r.u16();
        const mapio::ComponentRegistry& types = mapio::coreRegistry();
        for (uint16_t i = 0; i < count && r.ok(); ++i) {
            const uint16_t id = r.u16();
            if (const mapio::ComponentType* t = types.find(id))
                t->deserialize(reg, e2, r);
        }
        *slot = e2;
    };
    return c;
}

} // namespace editor
```

- [ ] **Step 7: Confirm pass** — `ctest -R editor_command` → `EditorCommandTests OK`.

- [ ] **Step 8: Commit**
```bash
git add game/src/editor/CommandStack.h game/src/editor/Commands.h game/src/editor/Commands.cpp game/tests/EditorCommandTests.cpp CMakeLists.txt
git commit -m "feat(editor): CommandStack + create/transform/delete commands"
```

---

## Task 6: InspectorRegistry (per-component ImGui editors)

**Files:**
- Create: `game/src/editor/InspectorRegistry.h`, `game/src/editor/InspectorRegistry.cpp`
- Test: `game/tests/InspectorRegistryTests.cpp` (completeness only — no ImGui context)

Editor-side map from `stableTypeId` to an ImGui draw function. The completeness test asserts every serializable type in `mapio::coreRegistry()` has an inspector registered (so nothing is un-editable), without creating an ImGui frame.

- [ ] **Step 1: Failing test** — `game/tests/InspectorRegistryTests.cpp`:

```cpp
#include "InspectorRegistry.h"

#include "ComponentRegistry.h"

#include <cstdlib>
#include <iostream>

using namespace editor;

static void require(bool c, const char* m)
{
    if (!c) { std::cerr << "InspectorRegistryTests: " << m << '\n'; std::exit(1); }
}

int main()
{
    const InspectorRegistry& insp = inspectorRegistry();
    for (const mapio::ComponentType& t : mapio::coreRegistry().types())
        require(insp.find(t.stableTypeId) != nullptr,
                "every serializable component has an inspector");
    std::cout << "InspectorRegistryTests OK\n";
    return 0;
}
```

- [ ] **Step 2: CMake target** under `if(BUILD_TESTING)`:

```cmake
  add_executable(inspector_registry_tests
    game/tests/InspectorRegistryTests.cpp
    game/src/editor/InspectorRegistry.cpp
    game/src/scene/ComponentRegistry.cpp
    game/src/scene/ByteStream.cpp)
  target_include_directories(inspector_registry_tests
    PRIVATE game/src/editor game/src/scene engine/include third_party third_party/imgui)
  target_link_libraries(inspector_registry_tests PRIVATE glm::glm EnTT::EnTT)
  add_test(NAME inspector_registry COMMAND inspector_registry_tests)
```
Reconfigure. (`third_party/imgui` is on the include path so `InspectorRegistry.cpp` can include `<imgui.h>`; the test itself never opens a frame — it only checks presence.)

- [ ] **Step 3: Confirm fail.**

- [ ] **Step 4: Header** — `game/src/editor/InspectorRegistry.h`:

```cpp
#pragma once

#include <entt/entt.hpp>

#include <cstdint>
#include <vector>

namespace editor {

// stableTypeId -> ImGui inspector. draw() renders editable widgets for the
// component on entity e and returns true if the user changed a value this frame.
struct InspectorEntry {
    uint16_t stableTypeId = 0;
    const char* label = nullptr;
    bool (*draw)(entt::registry&, entt::entity) = nullptr;
};

class InspectorRegistry {
public:
    void add(const InspectorEntry& e) { mEntries.push_back(e); }
    const std::vector<InspectorEntry>& entries() const { return mEntries; }
    const InspectorEntry* find(uint16_t stableTypeId) const;

private:
    std::vector<InspectorEntry> mEntries;
};

const InspectorRegistry& inspectorRegistry();

} // namespace editor
```

- [ ] **Step 5: Implementation** — `game/src/editor/InspectorRegistry.cpp`. Provide a `draw` for every id (1,2,3,4,10,11,12,13,14,15). Widgets edit the live component and return whether `ImGui::IsItemDeactivatedAfterEdit()` fired (so the app can wrap the change in a command). Full file:

```cpp
#include "InspectorRegistry.h"

#include "GameComponents.h"
#include "MeshSource.h"

#include <eng/ecs/Components.h>

#include <imgui.h>

#include <glm/gtc/type_ptr.hpp>

namespace editor {

const InspectorEntry* InspectorRegistry::find(uint16_t id) const
{
    for (const InspectorEntry& e : mEntries)
        if (e.stableTypeId == id) return &e;
    return nullptr;
}

namespace {

bool drawName(entt::registry& r, entt::entity e)
{
    auto& n = r.get<eng::ecs::Name>(e);
    char buf[128];
    std::snprintf(buf, sizeof(buf), "%s", n.value.c_str());
    if (ImGui::InputText("Name", buf, sizeof(buf))) n.value = buf;
    return ImGui::IsItemDeactivatedAfterEdit();
}

bool drawTransform(entt::registry& r, entt::entity e)
{
    auto& t = r.get<eng::ecs::Transform>(e);
    bool changed = false;
    changed |= ImGui::DragFloat3("Position", glm::value_ptr(t.position), 0.05f);
    changed |= ImGui::DragFloat3("Scale", glm::value_ptr(t.scale), 0.05f);
    // Rotation edited as Euler degrees for usability.
    glm::vec3 euler = glm::degrees(glm::eulerAngles(t.rotation));
    if (ImGui::DragFloat3("Rotation", glm::value_ptr(euler), 0.5f)) {
        t.rotation = glm::quat(glm::radians(euler));
        changed = true;
    }
    return changed && ImGui::IsItemDeactivatedAfterEdit();
}

bool drawMesh(entt::registry& r, entt::entity e)
{
    auto& m = r.get<eng::ecs::MeshRenderer>(e);
    ImGui::Text("Mesh: %s", r.all_of<mapio::MeshSource>(e)
                                ? r.get<mapio::MeshSource>(e).path.c_str() : "?");
    char buf[128];
    std::snprintf(buf, sizeof(buf), "%s", m.material.c_str());
    if (ImGui::InputText("Material", buf, sizeof(buf))) m.material = buf;
    bool changed = ImGui::IsItemDeactivatedAfterEdit();
    changed |= ImGui::Checkbox("Cast shadows", &m.castShadows);
    return changed;
}

bool drawLight(entt::registry& r, entt::entity e)
{
    auto& l = r.get<eng::ecs::LightRef>(e).desc;
    bool changed = false;
    int type = int(l.type);
    if (ImGui::Combo("Type", &type, "Directional\0Point\0")) {
        l.type = eng::LightDesc::Type(type); changed = true;
    }
    changed |= ImGui::ColorEdit3("Colour", glm::value_ptr(l.colour));
    changed |= ImGui::DragFloat("Range", &l.range, 0.1f, 0.0f, 200.0f);
    changed |= ImGui::Checkbox("Cast shadows", &l.castShadows);
    return changed;
}

bool drawCollider(entt::registry& r, entt::entity e)
{
    auto& c = r.get<game::Collider>(e);
    bool changed = false;
    changed |= ImGui::DragFloat3("Half extents", glm::value_ptr(c.size), 0.05f);
    return changed;
}

bool drawExit(entt::registry& r, entt::entity e)
{
    auto& x = r.get<game::Exit>(e);
    return ImGui::DragFloat("Yaw (deg)", &x.yawDegrees, 1.0f, -360.0f, 360.0f);
}

bool drawEnemy(entt::registry& r, entt::entity e)
{
    auto& s = r.get<game::EnemySpawn>(e);
    char buf[64]; std::snprintf(buf, sizeof(buf), "%s", s.type.c_str());
    if (ImGui::InputText("Enemy type", buf, sizeof(buf))) s.type = buf;
    return ImGui::IsItemDeactivatedAfterEdit();
}

bool drawPickup(entt::registry& r, entt::entity e)
{
    auto& s = r.get<game::Pickup>(e);
    char buf[64]; std::snprintf(buf, sizeof(buf), "%s", s.type.c_str());
    if (ImGui::InputText("Pickup type", buf, sizeof(buf))) s.type = buf;
    return ImGui::IsItemDeactivatedAfterEdit();
}

bool drawTrigger(entt::registry& r, entt::entity e)
{
    auto& t = r.get<game::Trigger>(e);
    bool changed = false;
    changed |= ImGui::DragFloat3("Half extents", glm::value_ptr(t.size), 0.05f);
    char buf[64]; std::snprintf(buf, sizeof(buf), "%s", t.event.c_str());
    if (ImGui::InputText("Event", buf, sizeof(buf))) t.event = buf;
    changed |= ImGui::IsItemDeactivatedAfterEdit();
    return changed;
}

bool drawTag(entt::registry&, entt::entity) { ImGui::TextDisabled("(no fields)"); return false; }

InspectorRegistry build()
{
    InspectorRegistry reg;
    reg.add({1, "Name", drawName});
    reg.add({2, "Transform", drawTransform});
    reg.add({3, "MeshRenderer", drawMesh});
    reg.add({4, "LightRef", drawLight});
    reg.add({10, "Collider", drawCollider});
    reg.add({11, "PlayerSpawn", drawTag});
    reg.add({12, "Exit", drawExit});
    reg.add({13, "EnemySpawn", drawEnemy});
    reg.add({14, "Pickup", drawPickup});
    reg.add({15, "Trigger", drawTrigger});
    return reg;
}

} // namespace

const InspectorRegistry& inspectorRegistry()
{
    static const InspectorRegistry reg = build();
    return reg;
}

} // namespace editor
```
Add `#include <cstdio>` if `std::snprintf` is unresolved. If `glm::eulerAngles` needs it, include `<glm/gtc/quaternion.hpp>`.

- [ ] **Step 6: Confirm pass** — `ctest -R inspector_registry` → `InspectorRegistryTests OK`.

- [ ] **Step 7: Commit**
```bash
git add game/src/editor/InspectorRegistry.h game/src/editor/InspectorRegistry.cpp game/tests/InspectorRegistryTests.cpp CMakeLists.txt
git commit -m "feat(editor): per-component ImGui InspectorRegistry"
```

---

## Task 7: EditorApp — viewport, gizmo render, panels, save/open, Play

**Files:**
- Create: `game/src/editor/Palette.h`, `game/src/editor/Palette.cpp`
- Create: `game/src/editor/EditorApp.h`, `game/src/editor/EditorApp.cpp`
- Rewrite: `game/src/editor_main.cpp` (thin: build `EditorApp`, run loop)
- Modify: `CMakeLists.txt` — `level_editor` sources + includes
- Verify: build + `PSX_SCREENSHOT` smoke run (manual/CI, not a unit test)

This task wires the tested units into the live app. It is GUI glue, verified by building and a headless screenshot run rather than a unit test. Follow the structure precisely; reuse the exact engine calls named.

- [ ] **Step 1: `Palette.h/.cpp`.** `Palette` discovers `.obj` files under the asset mesh dirs (reuse the `std::filesystem` scan already in the current `editor_main.cpp`) and exposes an ImGui panel of create buttons: each mesh path, plus "Point Light", "Player Spawn", "Exit", "Enemy Spawn", "Pickup", "Trigger". It calls back into `EditorApp` with the chosen creation. Header:

```cpp
#pragma once
#include <functional>
#include <string>
#include <vector>

namespace editor {

class Palette {
public:
    void discover(const std::string& assetDir); // scans meshes/props + meshes/tiles
    // draw() renders the palette panel; on a click it invokes one of the callbacks.
    struct Callbacks {
        std::function<void(const std::string& objPath)> spawnMesh;
        std::function<void()> spawnLight;
        std::function<void()> spawnPlayerSpawn;
        std::function<void()> spawnExit;
        std::function<void()> spawnEnemy;
        std::function<void()> spawnPickup;
        std::function<void()> spawnTrigger;
    };
    void draw(const Callbacks& cb) const;

private:
    std::vector<std::string> mMeshPaths;
};

} // namespace editor
```
Implement `discover` with a `std::filesystem::directory_iterator` over `assetDir + "/meshes/props"` and `"/meshes/tiles"` collecting `.obj` paths (sorted). Implement `draw` with `ImGui::Begin("Palette")`, a button per mesh (label = filename stem) calling `cb.spawnMesh(path)`, then a separator and the six marker buttons. `ImGui::End()`.

- [ ] **Step 2: `EditorApp.h`.** Owns everything and runs a frame:

```cpp
#pragma once

#include "CommandStack.h"
#include "EditorScene.h"
#include "Gizmo.h"
#include "Palette.h"
#include "Selection.h"

#include "../EditorCamera.h"

#include <eng/ecs/RendererSceneBackend.h>

#include <memory>
#include <string>

namespace eng { class Engine; class Renderer; }

namespace editor {

class EditorApp {
public:
    EditorApp(eng::Engine& engine, std::string assetDir);
    void frame(float dt);          // one editor frame: input, gizmo, panels, render
    bool wantsQuit() const { return mQuit; }

private:
    void drawPanels();             // outliner, inspector, palette, toolbar
    void handleViewportInput(float dt);
    void renderOverlays();         // grid, selection AABB, gizmo handles
    void saveMap(const std::string& path);
    void openMap(const std::string& path);
    void launchGame();

    eng::Engine& mEngine;
    eng::Renderer& mRenderer;
    std::string mAssetDir;
    eng::ecs::RendererSceneBackend mBackend;
    EditorScene mScene;
    Selection mSel;
    CommandStack mStack;
    EditorCamera mCam;
    Palette mPalette;
    GizmoMode mGizmoMode = GizmoMode::Translate;
    float mSnapStep = 0.0f;
    std::string mMapPath;
    bool mQuit = false;
    int mVpW = 960, mVpH = 640;
};

} // namespace editor
```
`Selection.h` (create it):
```cpp
#pragma once
#include <entt/entt.hpp>
#include <vector>
namespace editor {
class Selection {
public:
    void set(entt::entity e) { mItems.clear(); if (e != entt::null) mItems.push_back(e); }
    void add(entt::entity e) { if (e != entt::null) mItems.push_back(e); }
    void clear() { mItems.clear(); }
    bool empty() const { return mItems.empty(); }
    entt::entity primary() const { return mItems.empty() ? entt::null : mItems.front(); }
    const std::vector<entt::entity>& items() const { return mItems; }
private:
    std::vector<entt::entity> mItems;
};
}
```

- [ ] **Step 3: `EditorApp.cpp`.** Implement using these exact engine calls (all confirmed to exist):
  - Constructor: `mBackend(mRenderer)`, `mScene(mBackend)`. Call `mRenderer.enableEditorViewport(mVpW, mVpH)`, `engine.debugUi().setMainWindowVisible(false)`, `setVisible(true)`, `engine.input().setMouseGrab(false)`. Discover palette. Seed a floor + key light so the scene isn't empty.
  - `frame(dt)`:
    1. `handleViewportInput(dt)` — free-fly camera when RMB held over the viewport (use `ImGui::GetIO()` mouse like the current editor_main does; drive `mCam.addYawPitch(...)` and `mCam.moveLocal(...)` from WASD via `engine.input()` or `ImGui::IsKeyDown`). Push the pose with `mRenderer.setEditorCameraPose(mCam.flyEye(), mCam.flyOrientation(), 60.0f)`.
    2. On LMB click in the viewport (and not dragging the gizmo): build a `Ray` with `screenRay(ndc, mCam.flyEye(), mCam.flyOrientation(), glm::radians(60.0f), aspect)` where `ndc` is the mouse position mapped into the viewport image rect to [-1,1] (y up). Pick the nearest entity via `mScene.entityBounds` + `rayAabb`; `mSel.set(hit)` (Ctrl → `add`).
    3. Gizmo: if selection non-empty, run the gizmo interaction (drag along `mGizmoMode` axis using `closestPointOnAxis`/`rayPlane`, applying `snap` when `mSnapStep>0`). On drag release, wrap the net transform delta in `makeSetTransform` and `mStack.run(...)`. During drag, mutate the Transform live for feedback.
    4. `mScene.sync()`.
    5. `renderOverlays()` — assemble `std::vector<eng::Renderer::DebugLine>`: a ground grid, an AABB around each selected entity (reuse the wire-box code from the current `editor_main.cpp`), and the gizmo axis handles (three colored lines from the selection centroid). `mRenderer.setDebugLines(lines)`.
    6. `drawPanels()` via `engine.debugUi().addWindow(...)` equivalents (or call directly inside the debug-ui frame as the current editor does).
  - `drawPanels()`:
    - Toolbar window: New / Open / Save (InputText path, default `mMapPath`), gizmo mode radio (`W/E/R`), snap step input, **Play** button → `launchGame()`, Undo/Redo buttons (`mStack.undo()/redo()`), and the viewport image via the same pattern the current editor uses (`ui.draw(r.editorViewportTextureId())` OR draw an `ImGui::Image` of `mRenderer.editorViewportTextureId()` and track its content-rect for picking + resize).
    - Outliner window: iterate `mScene.registry().view<eng::ecs::Name>()` (or all entities) listing selectable rows; clicking sets `mSel`.
    - Inspector window: for `mSel.primary()`, iterate `mapio::coreRegistry().types()`; for each `has(reg,e)`, look up `inspectorRegistry().find(id)` and call `draw(reg,e)`; below, an "Add Component" combo of missing types calling the component's `addDefault`. Wrap edits/adds in commands where practical (a live-edit + command-on-deactivate pattern is acceptable for v1).
    - Palette window: `mPalette.draw(callbacks)` where callbacks call `mScene.spawnMesh/…` (routed through `makeCreateEntity`-style commands or direct spawn + a create command) and set selection to the new entity.
  - `saveMap(path)`: `mapio::writeMap(path, mScene.registry(), mapio::coreRegistry())`; update `mMapPath`.
  - `openMap(path)`: clear the registry (`mScene.registry().clear()`), `mapio::readMap(path, mScene.registry(), mapio::coreRegistry())`, `mStack.clear()`, `mSel.clear()`. (SceneSync will rebuild renderer nodes next `sync()`.)
  - `launchGame()`: reuse the current editor's process-launch pattern (`std::system("SDL_VIDEODRIVER=x11 \"<exeDir>/game\" <mapPath> >/dev/null 2>&1 &")`). Passing the map path to `game` is Plan 3's responsibility; for now launch the game as-is (the argument is harmless if unused).

- [ ] **Step 4: Rewrite `game/src/editor_main.cpp`** to:
```cpp
#include "editor/EditorApp.h"

#include <eng/Engine.h>

#include <cstdlib>
#include <string>

int main(int, char**)
{
    eng::Engine engine;
    const std::string assets = APP_ASSET_DIR;
    if (!engine.init(assets + "/editor.toml", assets))
        return 1;

    editor::EditorApp app(engine, assets);
    while (!engine.shouldClose()) {
        const float dt = engine.tick();
        if (engine.input().wasPressed("quit") || app.wantsQuit())
            engine.requestClose();
        app.frame(dt);
        engine.renderFrame(dt);
    }
    engine.shutdown();
    return 0;
}
```
Keep whatever include/init the current `editor_main.cpp` used for `engine.tick()`/`renderFrame()` (confirm names against `eng/Engine.h`). If the engine drives ImGui inside `renderFrame`, register panel callbacks in the `EditorApp` constructor via `engine.debugUi().addWindow(...)` and have `frame(dt)` only update state (camera/pick/gizmo/overlays), matching how the current editor splits update vs. `addWindow` drawing.

- [ ] **Step 5: Update `CMakeLists.txt` `level_editor` target** — replace its `add_executable`/includes block with:
```cmake
add_executable(
  level_editor
  game/src/editor_main.cpp
  game/src/EditorCamera.cpp
  game/src/editor/EditorScene.cpp
  game/src/editor/Picker.cpp
  game/src/editor/Gizmo.cpp
  game/src/editor/Commands.cpp
  game/src/editor/InspectorRegistry.cpp
  game/src/editor/Palette.cpp
  game/src/editor/EditorApp.cpp
  game/src/scene/ByteStream.cpp
  game/src/scene/ComponentRegistry.cpp
  game/src/scene/MapSerializer.cpp)
target_include_directories(level_editor
  PRIVATE third_party game/src game/src/scene game/src/editor engine/src)
target_link_libraries(level_editor PRIVATE eng)
target_compile_definitions(
  level_editor PRIVATE APP_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/game/assets")
eng_target_hardening(level_editor)
```
(`engine/src` is added so `RendererSceneBackend.h` resolves; its `.cpp` is already inside the linked `eng` library.)

- [ ] **Step 6: Build** — `cmake -S /home/sektant1/psx-dungeon-crawler -B /home/sektant1/psx-dungeon-crawler/build && cmake --build /home/sektant1/psx-dungeon-crawler/build --target level_editor`. Fix compile errors by matching real engine signatures (`engine/include/eng/Engine.h`, `Renderer.h`, `Input.h`, `DebugUi.h`); do NOT change the tested units' interfaces.

- [ ] **Step 7: Headless smoke run** — `PSX_SCREENSHOT=/tmp/editor.png PSX_DEBUG_UI=1 SDL_VIDEODRIVER=x11 ./build/level_editor` (matches the repo's screenshot verification hook). Expect it to render ~90 frames and exit 0, writing `/tmp/editor.png`. Open the PNG (Read tool) and confirm: a 3D viewport with the seeded floor/light and the editor panels visible. If the run hangs or crashes, debug via `ogre.log` and the systematic-debugging skill.

- [ ] **Step 8: Run the full suite** — `ctest --test-dir /home/sektant1/psx-dungeon-crawler/build --output-on-failure`. All prior + new unit tests must pass.

- [ ] **Step 9: Commit**
```bash
git add game/src/editor/Palette.h game/src/editor/Palette.cpp \
        game/src/editor/EditorApp.h game/src/editor/EditorApp.cpp \
        game/src/editor/Selection.h game/src/editor_main.cpp CMakeLists.txt
git commit -m "feat(editor): 3D EditorApp — viewport, gizmo, panels, save/open .map, play"
```

---

## Self-Review

**Spec coverage (against `2026-07-23-level-editor-app-design.md`, Plan-2 slice):**
- Live ECS viewport via SceneSync → Task 4 + Task 7. ✓
- Free-fly camera → Task 1. ✓
- Pick (screen ray + AABB, non-mesh entities via fixed box) → Task 2 + Task 7. ✓
- Gizmo T/R/S + snap (custom, DebugLine handles) → Task 3 + Task 7 (deviation from ImGuizmo documented). ✓
- Overlays (grid, selection AABB, gizmo) via setDebugLines → Task 7. ✓
- Outliner + Inspector (registry-driven) + Asset/entity palette → Task 6 + Task 7. ✓
- Undo/redo command stack → Task 5. ✓
- Save/Open `.map` (Plan-1 writeMap/readMap) → Task 7. ✓
- Play launches game → Task 7. ✓
- Inspector kept out of the GUI-free Plan-1 core → Task 6 (InspectorRegistry, editor-side). ✓
- Deferred to later plans: physics bodies at runtime (Plan 3), generator (Plan 4), mesh-accurate pick bounds (noted in Task 4). ✓

**Type consistency:** `editor::Ray`, `screenRay`, `rayAabb`, `closestPointOnAxis`, `rayPlane`, `snap`, `GizmoMode`, `EditorScene` (spawnMesh/spawnLight/spawnMarker/sync/entityBounds/registry), `Command`, `CommandStack` (run/undo/redo/canUndo/canRedo/clear), `makeCreateEntity/makeSetTransform/makeDeleteEntity`, `Selection`, `InspectorRegistry`/`inspectorRegistry()`, `Palette` are used consistently across tasks. `mapio::` names match Plan 1 exactly (`writeMap`, `readMap`, `coreRegistry`, `ComponentType.has/serialize/deserialize/stableTypeId`, `ByteWriter/ByteReader`, `MeshSource`).

**Placeholder scan:** Tasks 1–6 contain complete code. Task 7 is GUI integration specified as a precise structure with the exact engine calls to use and a screenshot acceptance test, rather than full literal source — appropriate for non-TDD UI glue, and explicitly flagged as build+screenshot-verified. No `TODO`/`TBD` in committed steps.
