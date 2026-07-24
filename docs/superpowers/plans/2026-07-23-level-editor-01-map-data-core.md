# Level Editor — Plan 1: Map Data Core Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the renderer-free, fully unit-tested data core for the `.map` format: a byte stream, gameplay components, a component-type registry, and a serializer that round-trips an `entt` registry to/from a versioned binary `.map` file.

**Architecture:** All scene data lives in an `entt::registry`. A `ComponentRegistry` table lists every serializable component type with function pointers (id, factory, has, remove, serialize, deserialize). `MapSerializer` iterates that table to write/read each entity. A little-endian `ByteWriter`/`ByteReader` pair with an interned string pool does the primitive IO. Nothing here touches Ogre, ImGui, or Jolt — it compiles and tests headless.

**Tech Stack:** C++17, EnTT (`entt::registry`), GLM (vec3/quat), CMake + CTest. Test style matches the repo: a plain `int main()` with a local `require(cond, msg)` that prints and `std::exit(1)` on failure.

**Scope note:** This is Plan 1 of 4. It deliberately excludes the inspector/GUI function pointer (Plan 2), physics bodies (Plan 3), and the generator (Plan 4). The `ComponentType` struct reserves no GUI field yet; Plan 2 adds it.

---

## File Structure

- Create: `game/src/scene/ByteStream.h` — `mapio::ByteWriter` / `mapio::ByteReader` interfaces.
- Create: `game/src/scene/ByteStream.cpp` — their implementation.
- Create: `game/src/scene/GameComponents.h` — gameplay component structs (`Collider`, `PlayerSpawn`, `Exit`, `EnemySpawn`, `Pickup`, `Trigger`).
- Create: `game/src/scene/ComponentRegistry.h` — `mapio::ComponentType`, `mapio::ComponentRegistry`, `mapio::coreRegistry()`.
- Create: `game/src/scene/ComponentRegistry.cpp` — registration of core + gameplay component types.
- Create: `game/src/scene/MapSerializer.h` — `mapio::writeMap` / `mapio::readMap` / `mapio::dumpMap`.
- Create: `game/src/scene/MapSerializer.cpp` — binary write/read/dump implementation.
- Create: `game/tests/ByteStreamTests.cpp`, `game/tests/ComponentRegistryTests.cpp`, `game/tests/MapSerializerTests.cpp`.
- Modify: `CMakeLists.txt` — add the three test executables under `if(BUILD_TESTING)`.

Everything lives in `game/src/scene/` because it is game-owned gameplay data shared later by both the `game` and `level_editor` targets. Namespace `mapio` for the serialization machinery; components sit in namespace `game`.

---

## Task 1: ByteStream (little-endian primitive IO + string pool)

**Files:**
- Create: `game/src/scene/ByteStream.h`
- Create: `game/src/scene/ByteStream.cpp`
- Test: `game/tests/ByteStreamTests.cpp`

- [ ] **Step 1: Write the failing test**

Create `game/tests/ByteStreamTests.cpp`:

```cpp
#include "ByteStream.h"

#include <cstdlib>
#include <iostream>

using namespace mapio;

static void require(bool c, const char* m)
{
    if (!c) { std::cerr << "ByteStreamTests: " << m << '\n'; std::exit(1); }
}

int main()
{
    ByteWriter w;
    w.u8(0x12);
    w.u16(0x3456);
    w.u32(0x789ABCDEu);
    w.f32(1.5f);
    w.vec3({1.0f, -2.0f, 3.0f});
    w.quat(glm::quat(0.0f, 0.0f, 1.0f, 0.0f)); // w,x,y,z
    w.str("floor");
    w.str("wall");
    w.str("floor"); // deduped -> same pool index

    require(w.pool().size() == 2, "string pool dedups repeats");
    require(w.pool()[0] == "floor" && w.pool()[1] == "wall", "pool order preserved");

    ByteReader r(w.bytes().data(), w.bytes().size(), w.pool());
    require(r.u8() == 0x12, "u8 round-trips");
    require(r.u16() == 0x3456, "u16 round-trips");
    require(r.u32() == 0x789ABCDEu, "u32 round-trips");
    require(r.f32() == 1.5f, "f32 round-trips");
    const glm::vec3 v = r.vec3();
    require(v.x == 1.0f && v.y == -2.0f && v.z == 3.0f, "vec3 round-trips");
    const glm::quat q = r.quat();
    require(q.w == 0.0f && q.x == 0.0f && q.y == 1.0f && q.z == 0.0f, "quat round-trips");
    require(r.str() == "floor", "first string round-trips");
    require(r.str() == "wall", "second string round-trips");
    require(r.str() == "floor", "deduped string resolves to same value");
    require(r.ok(), "reader stayed in bounds");
    require(r.remaining() == 0, "reader consumed everything");

    // Overrun sets the error flag instead of reading garbage.
    ByteReader over(w.bytes().data(), 1, w.pool());
    over.u32();
    require(!over.ok(), "reading past the end flags an error");

    std::cout << "ByteStreamTests OK\n";
    return 0;
}
```

- [ ] **Step 2: Run the test to verify it fails**

```bash
cmake --build build --target byte_stream_tests
```
Expected: FAIL to compile — `ByteStream.h` does not exist yet. (The CMake target is added in Step 5; until then the failure is "no such target" / missing header.)

- [ ] **Step 3: Write the header**

Create `game/src/scene/ByteStream.h`:

```cpp
#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace mapio {

// Little-endian primitive writer with an interned, order-preserving string
// pool. Strings are written as a u32 pool index; the pool itself is emitted
// separately (by MapSerializer) so component payloads stay compact.
class ByteWriter {
public:
    void u8(uint8_t v);
    void u16(uint16_t v);
    void u32(uint32_t v);
    void u64(uint64_t v);
    void f32(float v);
    void vec3(const glm::vec3& v);
    void quat(const glm::quat& q);
    void str(const std::string& s); // interns, writes u32 index

    uint32_t intern(const std::string& s); // pool index, appends if new

    const std::vector<uint8_t>& bytes() const { return mBuf; }
    const std::vector<std::string>& pool() const { return mPool; }
    std::size_t size() const { return mBuf.size(); }

private:
    std::vector<uint8_t> mBuf;
    std::vector<std::string> mPool;
};

// Little-endian primitive reader. Never reads out of bounds: once an overrun
// is attempted, ok() returns false and further reads yield zero/defaults.
class ByteReader {
public:
    ByteReader(const uint8_t* data, std::size_t len,
               const std::vector<std::string>& pool);

    uint8_t u8();
    uint16_t u16();
    uint32_t u32();
    uint64_t u64();
    float f32();
    glm::vec3 vec3();
    glm::quat quat();
    const std::string& str(); // reads u32 index into the pool

    void skip(std::size_t n);
    std::size_t remaining() const { return mOk ? std::size_t(mEnd - mCur) : 0; }
    bool ok() const { return mOk; }

private:
    bool take(std::size_t n); // bounds-check n bytes, clear mOk on overrun
    const uint8_t* mCur;
    const uint8_t* mEnd;
    const std::vector<std::string>& mPool;
    bool mOk = true;
    std::string mEmpty;
};

} // namespace mapio
```

- [ ] **Step 4: Write the implementation**

Create `game/src/scene/ByteStream.cpp`:

```cpp
#include "ByteStream.h"

#include <cstring>

namespace mapio {

void ByteWriter::u8(uint8_t v) { mBuf.push_back(v); }

void ByteWriter::u16(uint16_t v)
{
    mBuf.push_back(uint8_t(v & 0xFF));
    mBuf.push_back(uint8_t((v >> 8) & 0xFF));
}

void ByteWriter::u32(uint32_t v)
{
    for (int i = 0; i < 4; ++i) mBuf.push_back(uint8_t((v >> (8 * i)) & 0xFF));
}

void ByteWriter::u64(uint64_t v)
{
    for (int i = 0; i < 8; ++i) mBuf.push_back(uint8_t((v >> (8 * i)) & 0xFF));
}

void ByteWriter::f32(float v)
{
    uint32_t bits;
    std::memcpy(&bits, &v, 4);
    u32(bits);
}

void ByteWriter::vec3(const glm::vec3& v) { f32(v.x); f32(v.y); f32(v.z); }

void ByteWriter::quat(const glm::quat& q) { f32(q.w); f32(q.x); f32(q.y); f32(q.z); }

uint32_t ByteWriter::intern(const std::string& s)
{
    for (uint32_t i = 0; i < mPool.size(); ++i)
        if (mPool[i] == s) return i;
    mPool.push_back(s);
    return uint32_t(mPool.size() - 1);
}

void ByteWriter::str(const std::string& s) { u32(intern(s)); }

ByteReader::ByteReader(const uint8_t* data, std::size_t len,
                       const std::vector<std::string>& pool)
    : mCur(data), mEnd(data + len), mPool(pool)
{}

bool ByteReader::take(std::size_t n)
{
    if (!mOk || std::size_t(mEnd - mCur) < n) { mOk = false; return false; }
    return true;
}

uint8_t ByteReader::u8()
{
    if (!take(1)) return 0;
    return *mCur++;
}

uint16_t ByteReader::u16()
{
    if (!take(2)) return 0;
    uint16_t v = uint16_t(mCur[0]) | (uint16_t(mCur[1]) << 8);
    mCur += 2;
    return v;
}

uint32_t ByteReader::u32()
{
    if (!take(4)) return 0;
    uint32_t v = 0;
    for (int i = 0; i < 4; ++i) v |= uint32_t(mCur[i]) << (8 * i);
    mCur += 4;
    return v;
}

uint64_t ByteReader::u64()
{
    if (!take(8)) return 0;
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v |= uint64_t(mCur[i]) << (8 * i);
    mCur += 8;
    return v;
}

float ByteReader::f32()
{
    uint32_t bits = u32();
    float v;
    std::memcpy(&v, &bits, 4);
    return v;
}

glm::vec3 ByteReader::vec3()
{
    float x = f32(), y = f32(), z = f32();
    return {x, y, z};
}

glm::quat ByteReader::quat()
{
    float w = f32(), x = f32(), y = f32(), z = f32();
    return glm::quat(w, x, y, z);
}

const std::string& ByteReader::str()
{
    uint32_t idx = u32();
    if (!mOk || idx >= mPool.size()) { mOk = false; return mEmpty; }
    return mPool[idx];
}

void ByteReader::skip(std::size_t n)
{
    if (take(n)) mCur += n;
}

} // namespace mapio
```

- [ ] **Step 5: Add the CMake test target**

In `CMakeLists.txt`, inside the `if(BUILD_TESTING)` block (after the existing `editor_camera_tests` block), add:

```cmake
  add_executable(byte_stream_tests game/tests/ByteStreamTests.cpp
                                   game/src/scene/ByteStream.cpp)
  target_include_directories(byte_stream_tests PRIVATE game/src/scene)
  target_link_libraries(byte_stream_tests PRIVATE glm::glm)
  add_test(NAME byte_stream COMMAND byte_stream_tests)
```

- [ ] **Step 6: Run the test to verify it passes**

```bash
cmake --build build --target byte_stream_tests && ctest --test-dir build -R byte_stream --output-on-failure
```
Expected: `ByteStreamTests OK` and CTest reports `1 passed`.

- [ ] **Step 7: Commit**

```bash
git add game/src/scene/ByteStream.h game/src/scene/ByteStream.cpp \
        game/tests/ByteStreamTests.cpp CMakeLists.txt
git commit -m "feat(map): little-endian ByteStream with interned string pool"
```

---

## Task 2: Gameplay components

**Files:**
- Create: `game/src/scene/GameComponents.h`

No test of its own — these are plain structs exercised by Task 3/4. Committed with Task 3.

- [ ] **Step 1: Write the header**

Create `game/src/scene/GameComponents.h`:

```cpp
#pragma once

#include <eng/Physics.h> // eng::ShapeKind, eng::BodyLayer

#include <glm/glm.hpp>

#include <string>

// Gameplay components authored in the editor and read by the runtime. Core
// scene components (Name, Transform, MeshRenderer, LightRef) live in
// engine/include/eng/ecs/Components.h and are registered alongside these.
namespace game {

// Static collision volume. Emitted as a Jolt body by PhysicsSync (Plan 3).
struct Collider {
    eng::ShapeKind shape = eng::ShapeKind::Box;
    glm::vec3 size{0.5f}; // half-extents (box) / radius in x (sphere)
    eng::BodyLayer layer = eng::BodyLayer::Static;
};

// Unique player start.
struct PlayerSpawn {};

// Level exit / down-portal. yawDegrees orients the arrival facing.
struct Exit {
    float yawDegrees = 0.0f;
};

// Enemy placement; type keys into the enemy factory.
struct EnemySpawn {
    std::string type;
};

// Loot / item placement; type keys into the pickup factory.
struct Pickup {
    std::string type;
};

// Event volume (WC3-style region). event keys into the trigger dispatch.
struct Trigger {
    eng::ShapeKind shape = eng::ShapeKind::Box;
    glm::vec3 size{1.0f};
    std::string event;
};

} // namespace game
```

- [ ] **Step 2: Verify it compiles standalone**

```bash
cd /home/sektant1/psx-dungeon-crawler
g++ -std=c++17 -fsyntax-only -Iengine/include \
    $(pkg-config --cflags glm 2>/dev/null) game/src/scene/GameComponents.h
```
Expected: no output (clean syntax check). If `glm` is not a pkg-config package, drop that substitution — GLM headers resolve via `engine/include` dependencies during the real build; this check is best-effort.

- [ ] **Step 3: (Commit happens in Task 3.)**

---

## Task 3: ComponentRegistry (type table + serialize/deserialize per type)

**Files:**
- Create: `game/src/scene/ComponentRegistry.h`
- Create: `game/src/scene/ComponentRegistry.cpp`
- Test: `game/tests/ComponentRegistryTests.cpp`

The registry is a list of `ComponentType` entries. Each entry knows how to test/add/remove/serialize/deserialize one component on an entity. `coreRegistry()` returns the singleton list with every type registered once. `stableTypeId` values are assigned here and MUST never change or be reused.

Stable type id assignments (fixed forever):
`Name=1, Transform=2, MeshRenderer=3, LightRef=4, Collider=10, PlayerSpawn=11, Exit=12, EnemySpawn=13, Pickup=14, Trigger=15`.
(`Parent` is not a component entry — parent links are stored per-entity in the file header, see Task 4. `WorldTransform`, `Dirty`, `NodeRef`, `BodyRef` are runtime-derived and never registered.)

- [ ] **Step 1: Write the failing test**

Create `game/tests/ComponentRegistryTests.cpp`:

```cpp
#include "ByteStream.h"
#include "ComponentRegistry.h"
#include "GameComponents.h"

#include <eng/ecs/Components.h>

#include <cstdlib>
#include <iostream>
#include <set>

using namespace mapio;

static void require(bool c, const char* m)
{
    if (!c) { std::cerr << "ComponentRegistryTests: " << m << '\n'; std::exit(1); }
}

int main()
{
    const ComponentRegistry& reg = coreRegistry();

    // Completeness: every entry has all function pointers and a unique id.
    std::set<uint16_t> ids;
    for (const ComponentType& t : reg.types()) {
        require(t.name != nullptr, "type has a name");
        require(t.addDefault && t.has && t.remove && t.serialize && t.deserialize,
                "type has all function pointers");
        require(ids.insert(t.stableTypeId).second, "stableTypeId is unique");
    }

    // Round-trip one component through its serialize/deserialize pair.
    const ComponentType* transform = reg.find(2 /* Transform */);
    require(transform != nullptr, "Transform is registered under id 2");

    entt::registry src;
    entt::entity e = src.create();
    src.emplace<eng::ecs::Transform>(e, glm::vec3(1, 2, 3),
                                     glm::quat(1, 0, 0, 0), glm::vec3(2, 2, 2));
    require(transform->has(src, e), "has() sees the emplaced component");

    ByteWriter w;
    transform->serialize(src, e, w);

    entt::registry dst;
    entt::entity d = dst.create();
    ByteReader r(w.bytes().data(), w.bytes().size(), w.pool());
    transform->deserialize(dst, d, r);
    require(r.ok(), "deserialize stayed in bounds");

    const auto& t = dst.get<eng::ecs::Transform>(d);
    require(t.position == glm::vec3(1, 2, 3), "position survives round-trip");
    require(t.scale == glm::vec3(2, 2, 2), "scale survives round-trip");

    // remove() clears it.
    transform->remove(dst, d);
    require(!transform->has(dst, d), "remove() drops the component");

    std::cout << "ComponentRegistryTests OK\n";
    return 0;
}
```

- [ ] **Step 2: Run to verify it fails**

```bash
cmake --build build --target component_registry_tests
```
Expected: FAIL — `ComponentRegistry.h` missing.

- [ ] **Step 3: Write the header**

Create `game/src/scene/ComponentRegistry.h`:

```cpp
#pragma once

#include <entt/entt.hpp>

#include <cstdint>
#include <vector>

namespace mapio {

class ByteWriter;
class ByteReader;

// One serializable/editable component type. Function pointers keep the table
// POD and free of virtual dispatch; the inspector hook is added in Plan 2.
struct ComponentType {
    const char* name = nullptr;
    uint16_t stableTypeId = 0; // persisted in .map, never reused

    void (*addDefault)(entt::registry&, entt::entity) = nullptr;
    bool (*has)(const entt::registry&, entt::entity) = nullptr;
    void (*remove)(entt::registry&, entt::entity) = nullptr;
    void (*serialize)(const entt::registry&, entt::entity, ByteWriter&) = nullptr;
    void (*deserialize)(entt::registry&, entt::entity, ByteReader&) = nullptr;
};

// Ordered list of component types. Serializer, inspector (Plan 2), and the
// add-component menu all iterate this.
class ComponentRegistry {
public:
    void add(const ComponentType& t) { mTypes.push_back(t); }
    const std::vector<ComponentType>& types() const { return mTypes; }
    const ComponentType* find(uint16_t stableTypeId) const;

private:
    std::vector<ComponentType> mTypes;
};

// The process-wide registry with all core + gameplay types registered once.
const ComponentRegistry& coreRegistry();

} // namespace mapio
```

- [ ] **Step 4: Write the implementation**

Create `game/src/scene/ComponentRegistry.cpp`:

```cpp
#include "ComponentRegistry.h"

#include "ByteStream.h"
#include "GameComponents.h"

#include <eng/ecs/Components.h>

namespace mapio {

const ComponentType* ComponentRegistry::find(uint16_t id) const
{
    for (const ComponentType& t : mTypes)
        if (t.stableTypeId == id) return &t;
    return nullptr;
}

namespace {

// Generic add/has/remove built from the component type.
template <typename T>
void addDefault(entt::registry& r, entt::entity e) { r.emplace_or_replace<T>(e); }
template <typename T>
bool has(const entt::registry& r, entt::entity e) { return r.all_of<T>(e); }
template <typename T>
void remove(entt::registry& r, entt::entity e) { r.remove<T>(e); }

// ---- per-type serialize / deserialize --------------------------------------

void serName(const entt::registry& r, entt::entity e, ByteWriter& w)
{ w.str(r.get<eng::ecs::Name>(e).value); }
void deName(entt::registry& r, entt::entity e, ByteReader& b)
{ r.emplace_or_replace<eng::ecs::Name>(e, eng::ecs::Name{b.str()}); }

void serTransform(const entt::registry& r, entt::entity e, ByteWriter& w)
{
    const auto& t = r.get<eng::ecs::Transform>(e);
    w.vec3(t.position); w.quat(t.rotation); w.vec3(t.scale);
}
void deTransform(entt::registry& r, entt::entity e, ByteReader& b)
{
    eng::ecs::Transform t;
    t.position = b.vec3(); t.rotation = b.quat(); t.scale = b.vec3();
    r.emplace_or_replace<eng::ecs::Transform>(e, t);
}

// MeshRenderer stores the mesh ASSET PATH (not the session MeshHandle) plus
// material and shadow flag. The runtime MeshHandle is resolved on load (Plan 3),
// so here we persist the path in a companion component the loader reads.
// To keep MeshRenderer self-describing in the file we serialize path+material+
// shadow and reconstruct a MeshRenderer with an empty handle; the loader fills
// the handle. The path is carried in the string pool.
void serMesh(const entt::registry& r, entt::entity e, ByteWriter& w)
{
    const auto& m = r.get<eng::ecs::MeshRenderer>(e);
    // meshPath lives in a parallel MeshSource component (see below).
    const auto* src = r.try_get<eng::ecs::MeshRenderer>(e);
    (void)src;
    w.str(r.get<MeshSource>(e).path);
    w.str(m.material);
    w.u8(m.castShadows ? 1 : 0);
}
void deMesh(entt::registry& r, entt::entity e, ByteReader& b)
{
    const std::string path = b.str();
    const std::string material = b.str();
    const bool shadows = b.u8() != 0;
    r.emplace_or_replace<MeshSource>(e, MeshSource{path});
    eng::ecs::MeshRenderer m;
    m.material = material;
    m.castShadows = shadows;
    r.emplace_or_replace<eng::ecs::MeshRenderer>(e, m); // handle filled on load
}

void serLight(const entt::registry& r, entt::entity e, ByteWriter& w)
{
    const auto& l = r.get<eng::ecs::LightRef>(e).desc;
    w.u8(uint8_t(l.type));
    w.vec3(l.colour);
    w.f32(l.range);
    w.u8(l.castShadows ? 1 : 0);
}
void deLight(entt::registry& r, entt::entity e, ByteReader& b)
{
    eng::LightDesc d;
    d.type = eng::LightDesc::Type(b.u8());
    d.colour = b.vec3();
    d.range = b.f32();
    d.castShadows = b.u8() != 0;
    r.emplace_or_replace<eng::ecs::LightRef>(e, eng::ecs::LightRef{d, {}});
}

void serCollider(const entt::registry& r, entt::entity e, ByteWriter& w)
{
    const auto& c = r.get<game::Collider>(e);
    w.u8(uint8_t(c.shape)); w.vec3(c.size); w.u8(uint8_t(c.layer));
}
void deCollider(entt::registry& r, entt::entity e, ByteReader& b)
{
    game::Collider c;
    c.shape = eng::ShapeKind(b.u8()); c.size = b.vec3();
    c.layer = eng::BodyLayer(b.u8());
    r.emplace_or_replace<game::Collider>(e, c);
}

void serExit(const entt::registry& r, entt::entity e, ByteWriter& w)
{ w.f32(r.get<game::Exit>(e).yawDegrees); }
void deExit(entt::registry& r, entt::entity e, ByteReader& b)
{ r.emplace_or_replace<game::Exit>(e, game::Exit{b.f32()}); }

void serEnemy(const entt::registry& r, entt::entity e, ByteWriter& w)
{ w.str(r.get<game::EnemySpawn>(e).type); }
void deEnemy(entt::registry& r, entt::entity e, ByteReader& b)
{ r.emplace_or_replace<game::EnemySpawn>(e, game::EnemySpawn{b.str()}); }

void serPickup(const entt::registry& r, entt::entity e, ByteWriter& w)
{ w.str(r.get<game::Pickup>(e).type); }
void dePickup(entt::registry& r, entt::entity e, ByteReader& b)
{ r.emplace_or_replace<game::Pickup>(e, game::Pickup{b.str()}); }

void serTrigger(const entt::registry& r, entt::entity e, ByteWriter& w)
{
    const auto& t = r.get<game::Trigger>(e);
    w.u8(uint8_t(t.shape)); w.vec3(t.size); w.str(t.event);
}
void deTrigger(entt::registry& r, entt::entity e, ByteReader& b)
{
    game::Trigger t;
    t.shape = eng::ShapeKind(b.u8()); t.size = b.vec3(); t.event = b.str();
    r.emplace_or_replace<game::Trigger>(e, t);
}

// Tag components (PlayerSpawn) serialize no payload.
void serEmpty(const entt::registry&, entt::entity, ByteWriter&) {}
void dePlayerSpawn(entt::registry& r, entt::entity e, ByteReader&)
{ r.emplace_or_replace<game::PlayerSpawn>(e); }

ComponentRegistry buildCore()
{
    ComponentRegistry reg;
    using eng::ecs::Name; using eng::ecs::Transform;
    using eng::ecs::MeshRenderer; using eng::ecs::LightRef;

    reg.add({"Name", 1, addDefault<Name>, has<Name>, remove<Name>, serName, deName});
    reg.add({"Transform", 2, addDefault<Transform>, has<Transform>,
             remove<Transform>, serTransform, deTransform});
    reg.add({"MeshRenderer", 3, addDefault<MeshRenderer>, has<MeshRenderer>,
             remove<MeshRenderer>, serMesh, deMesh});
    reg.add({"LightRef", 4, addDefault<LightRef>, has<LightRef>,
             remove<LightRef>, serLight, deLight});

    reg.add({"Collider", 10, addDefault<game::Collider>, has<game::Collider>,
             remove<game::Collider>, serCollider, deCollider});
    reg.add({"PlayerSpawn", 11, addDefault<game::PlayerSpawn>,
             has<game::PlayerSpawn>, remove<game::PlayerSpawn>,
             serEmpty, dePlayerSpawn});
    reg.add({"Exit", 12, addDefault<game::Exit>, has<game::Exit>,
             remove<game::Exit>, serExit, deExit});
    reg.add({"EnemySpawn", 13, addDefault<game::EnemySpawn>,
             has<game::EnemySpawn>, remove<game::EnemySpawn>, serEnemy, deEnemy});
    reg.add({"Pickup", 14, addDefault<game::Pickup>, has<game::Pickup>,
             remove<game::Pickup>, serPickup, dePickup});
    reg.add({"Trigger", 15, addDefault<game::Trigger>, has<game::Trigger>,
             remove<game::Trigger>, serTrigger, deTrigger});
    return reg;
}

} // namespace

const ComponentRegistry& coreRegistry()
{
    static const ComponentRegistry reg = buildCore();
    return reg;
}

} // namespace mapio
```

- [ ] **Step 5: Add the `MeshSource` component**

`MeshRenderer` carries a runtime `MeshHandle`, not a path, so the serializer needs the source path stored somewhere. Add a small component next to the gameplay ones. In `game/src/scene/GameComponents.h`, inside `namespace game`... no — it is referenced unqualified as `MeshSource` in `ComponentRegistry.cpp`. Put it in `namespace mapio`. Create `game/src/scene/MeshSource.h`:

```cpp
#pragma once
#include <string>

namespace mapio {
// The asset path a MeshRenderer was built from. The editor sets this when it
// spawns a mesh entity; the serializer persists it; the loader (Plan 3) uses
// it to resolve a runtime MeshHandle. Kept separate so eng::ecs::MeshRenderer
// stays renderer-facing (handle only).
struct MeshSource {
    std::string path;
};
} // namespace mapio
```

Then in `ComponentRegistry.cpp`, add `#include "MeshSource.h"` and change the two `MeshSource` references from unqualified to `mapio::MeshSource` is unnecessary (already in `namespace mapio`). Remove the dead `try_get` lines in `serMesh` so it reads:

```cpp
void serMesh(const entt::registry& r, entt::entity e, ByteWriter& w)
{
    const auto& m = r.get<eng::ecs::MeshRenderer>(e);
    w.str(r.get<MeshSource>(e).path);
    w.str(m.material);
    w.u8(m.castShadows ? 1 : 0);
}
```

- [ ] **Step 6: Add the CMake test target**

In `CMakeLists.txt` under `if(BUILD_TESTING)` add:

```cmake
  add_executable(component_registry_tests
    game/tests/ComponentRegistryTests.cpp
    game/src/scene/ByteStream.cpp
    game/src/scene/ComponentRegistry.cpp)
  target_include_directories(component_registry_tests
    PRIVATE game/src/scene engine/include third_party)
  target_link_libraries(component_registry_tests PRIVATE glm::glm EnTT::EnTT)
  add_test(NAME component_registry COMMAND component_registry_tests)
```

- [ ] **Step 7: Run to verify it passes**

```bash
cmake --build build --target component_registry_tests && \
  ctest --test-dir build -R component_registry --output-on-failure
```
Expected: `ComponentRegistryTests OK`, `1 passed`.

- [ ] **Step 8: Commit**

```bash
git add game/src/scene/GameComponents.h game/src/scene/MeshSource.h \
        game/src/scene/ComponentRegistry.h game/src/scene/ComponentRegistry.cpp \
        game/tests/ComponentRegistryTests.cpp CMakeLists.txt
git commit -m "feat(map): component registry with core + gameplay serializers"
```

---

## Task 4: MapSerializer (binary write/read round-trip, skip-unknown)

**Files:**
- Create: `game/src/scene/MapSerializer.h`
- Create: `game/src/scene/MapSerializer.cpp`
- Test: `game/tests/MapSerializerTests.cpp`

File layout (matches the spec):

```
"PSXMAP\0" (8 bytes)   magic
u16 version            = 1
u16 flags              = 0
u32 poolCount          then [u32 len, bytes] * poolCount
u32 entityCount        then per entity:
    u32 localId
    u32 parentLocalId  (0xFFFFFFFF = root)
    u16 componentCount
    per component: u16 stableTypeId, u32 byteLen, bytes[byteLen]
```

Because component payloads use the writer's string pool, the whole body is
serialized to one `ByteWriter` first (so the pool is complete), then the header
+ pool + body are written to the file. The `byteLen` prefix lets a reader skip a
component whose `stableTypeId` is unknown.

- [ ] **Step 1: Write the failing test**

Create `game/tests/MapSerializerTests.cpp`:

```cpp
#include "GameComponents.h"
#include "MapSerializer.h"
#include "MeshSource.h"

#include <eng/ecs/Components.h>

#include <cstdlib>
#include <cstdio>
#include <iostream>
#include <string>

static void require(bool c, const char* m)
{
    if (!c) { std::cerr << "MapSerializerTests: " << m << '\n'; std::exit(1); }
}

int main()
{
    const std::string path = "map_serializer_test.map";

    entt::registry src;
    // A parent mesh entity with a child light.
    entt::entity room = src.create();
    src.emplace<eng::ecs::Name>(room, eng::ecs::Name{"Room"});
    src.emplace<eng::ecs::Transform>(room, glm::vec3(4, 0, 0),
                                     glm::quat(1, 0, 0, 0), glm::vec3(1));
    src.emplace<mapio::MeshSource>(room, mapio::MeshSource{"meshes/tiles/floor.obj"});
    eng::ecs::MeshRenderer mr; mr.material = "Game/DungeonTile"; mr.castShadows = true;
    src.emplace<eng::ecs::MeshRenderer>(room, mr);
    src.emplace<game::Collider>(room, game::Collider{});

    entt::entity torch = src.create();
    src.emplace<eng::ecs::Name>(torch, eng::ecs::Name{"Torch"});
    src.emplace<eng::ecs::Transform>(torch, glm::vec3(0, 2, 0),
                                     glm::quat(1, 0, 0, 0), glm::vec3(1));
    eng::LightDesc ld; ld.type = eng::LightDesc::Type::Point;
    ld.colour = glm::vec3(1, 0.6f, 0.3f); ld.range = 6.5f;
    src.emplace<eng::ecs::LightRef>(torch, eng::ecs::LightRef{ld, {}});
    src.emplace<eng::ecs::Parent>(torch, eng::ecs::Parent{room});

    require(mapio::writeMap(path, src, mapio::coreRegistry()), "write succeeds");

    entt::registry dst;
    require(mapio::readMap(path, dst, mapio::coreRegistry()), "read succeeds");

    // Same entity count.
    int srcCount = 0, dstCount = 0;
    src.view<eng::ecs::Transform>().each([&](auto...) { ++srcCount; });
    dst.view<eng::ecs::Transform>().each([&](auto...) { ++dstCount; });
    require(srcCount == dstCount && dstCount == 2, "entity count round-trips");

    // Find the room again by name and verify data + parent link.
    entt::entity dRoom = entt::null, dTorch = entt::null;
    dst.view<eng::ecs::Name>().each([&](entt::entity e, const eng::ecs::Name& n) {
        if (n.value == "Room") dRoom = e;
        if (n.value == "Torch") dTorch = e;
    });
    require(dRoom != entt::null && dTorch != entt::null, "both entities present");
    require(dst.get<eng::ecs::Transform>(dRoom).position == glm::vec3(4, 0, 0),
            "room transform round-trips");
    require(dst.get<mapio::MeshSource>(dRoom).path == "meshes/tiles/floor.obj",
            "mesh path round-trips");
    require(dst.get<eng::ecs::MeshRenderer>(dRoom).material == "Game/DungeonTile",
            "material round-trips");
    require(dst.all_of<game::Collider>(dRoom), "collider round-trips");
    require(dst.get<eng::ecs::LightRef>(dTorch).desc.range == 6.5f,
            "light range round-trips");
    require(dst.get<eng::ecs::Parent>(dTorch).value == dRoom,
            "parent link is remapped correctly");

    // Unknown-component skip: a registry that only knows Transform must still
    // load every entity, silently dropping the components it cannot resolve.
    mapio::ComponentRegistry tiny;
    for (const mapio::ComponentType& t : mapio::coreRegistry().types())
        if (t.stableTypeId == 2) tiny.add(t); // Transform only
    entt::registry partial;
    require(mapio::readMap(path, partial, tiny), "read with tiny registry succeeds");
    int partialCount = 0;
    partial.view<eng::ecs::Transform>().each([&](auto...) { ++partialCount; });
    require(partialCount == 2, "all entities load even with unknown components");

    std::remove(path.c_str());
    std::cout << "MapSerializerTests OK\n";
    return 0;
}
```

- [ ] **Step 2: Run to verify it fails**

```bash
cmake --build build --target map_serializer_tests
```
Expected: FAIL — `MapSerializer.h` missing.

- [ ] **Step 3: Write the header**

Create `game/src/scene/MapSerializer.h`:

```cpp
#pragma once

#include <entt/entt.hpp>

#include <string>

namespace mapio {

class ComponentRegistry;

// Serialize every entity in `reg` (their registered components + parent links)
// to a binary .map file. Returns false on file-open failure.
bool writeMap(const std::string& path, const entt::registry& reg,
              const ComponentRegistry& types);

// Load a .map file into `out` (which should be empty). Components with an
// unknown stableTypeId are skipped. Returns false on open / bad-magic /
// version-too-new / truncated file.
bool readMap(const std::string& path, entt::registry& out,
             const ComponentRegistry& types);

// Human-readable dump of a .map file to stdout (recovers inspect/diff for the
// binary format). Returns false if the file cannot be read.
bool dumpMap(const std::string& path, const ComponentRegistry& types);

} // namespace mapio
```

- [ ] **Step 4: Write the implementation**

Create `game/src/scene/MapSerializer.cpp`:

```cpp
#include "MapSerializer.h"

#include "ByteStream.h"
#include "ComponentRegistry.h"

#include <eng/ecs/Components.h> // eng::ecs::Parent

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <unordered_map>
#include <vector>

namespace mapio {

namespace {
constexpr char kMagic[8] = {'P', 'S', 'X', 'M', 'A', 'P', '\0', 0};
constexpr uint16_t kVersion = 1;
constexpr uint32_t kNoParent = 0xFFFFFFFFu;

void putRaw32(std::vector<uint8_t>& b, uint32_t v)
{ for (int i = 0; i < 4; ++i) b.push_back(uint8_t((v >> (8 * i)) & 0xFF)); }
void putRaw16(std::vector<uint8_t>& b, uint16_t v)
{ b.push_back(uint8_t(v & 0xFF)); b.push_back(uint8_t((v >> 8) & 0xFF)); }
} // namespace

bool writeMap(const std::string& path, const entt::registry& reg,
              const ComponentRegistry& types)
{
    // Assign a dense localId to every live entity.
    std::unordered_map<entt::entity, uint32_t> localId;
    std::vector<entt::entity> order;
    reg.view<entt::entity>().each([&](entt::entity e) {
        localId.emplace(e, uint32_t(order.size()));
        order.push_back(e);
    });

    // Serialize the body into one writer so its string pool is complete.
    ByteWriter body;
    putRaw32(const_cast<std::vector<uint8_t>&>(body.bytes()), 0); // placeholder? no
    // NOTE: body.bytes() is const; write entity count via body.u32 instead.

    ByteWriter w;
    w.u32(uint32_t(order.size()));
    for (entt::entity e : order) {
        w.u32(localId[e]);
        uint32_t parent = kNoParent;
        if (const auto* p = reg.try_get<eng::ecs::Parent>(e))
            if (p->value != entt::null) {
                auto it = localId.find(p->value);
                if (it != localId.end()) parent = it->second;
            }
        w.u32(parent);

        // Count then write each present component with a byteLen prefix.
        std::vector<const ComponentType*> present;
        for (const ComponentType& t : types.types())
            if (t.has(reg, e)) present.push_back(&t);
        w.u16(uint16_t(present.size()));
        for (const ComponentType* t : present) {
            w.u16(t->stableTypeId);
            // Serialize into a scratch writer to measure byteLen. The scratch
            // shares the main pool by interning through w for strings: to keep
            // pool indices consistent, serialize directly into w and record the
            // span length.
            const std::size_t before = w.size();
            w.u32(0); // byteLen placeholder (4 bytes)
            const std::size_t payloadStart = w.size();
            t->serialize(reg, e, w);
            const std::size_t payloadLen = w.size() - payloadStart;
            // Patch the placeholder. bytes() is not mutable via the public API,
            // so we cannot overwrite in place; instead we require serialize to
            // be deterministic and re-emit. Simpler: expose a patch method.
            (void)before; (void)payloadLen;
        }
    }
    (void)body;

    // The in-place patch above needs a mutable byte accessor. Add it to
    // ByteWriter (see Step 5) and use w.patchU32(offset, value).
    return false; // replaced in Step 5
}

bool readMap(const std::string&, entt::registry&, const ComponentRegistry&)
{ return false; } // implemented in Step 5

bool dumpMap(const std::string&, const ComponentRegistry&)
{ return false; } // implemented in Step 6

} // namespace mapio
```

> The Step 4 sketch exposes a real problem: writing a `byteLen` prefix before a
> payload of unknown length needs either a two-pass measure or an in-place patch.
> Step 5 resolves it cleanly by adding `patchU32` to `ByteWriter` and finishing
> the implementation. Do not commit Step 4 on its own.

- [ ] **Step 5: Add `patchU32` to ByteWriter and finish write/read**

In `game/src/scene/ByteStream.h`, add to the public section of `ByteWriter`:

```cpp
    // Overwrite 4 bytes previously written at byte offset `at` (little-endian).
    // Used to backfill a length prefix once the payload size is known.
    void patchU32(std::size_t at, uint32_t v);
```

In `game/src/scene/ByteStream.cpp`, add:

```cpp
void ByteWriter::patchU32(std::size_t at, uint32_t v)
{
    for (int i = 0; i < 4; ++i) mBuf[at + std::size_t(i)] = uint8_t((v >> (8 * i)) & 0xFF);
}
```

Now replace the entire body of `game/src/scene/MapSerializer.cpp` with the
correct implementation:

```cpp
#include "MapSerializer.h"

#include "ByteStream.h"
#include "ComponentRegistry.h"

#include <eng/ecs/Components.h> // eng::ecs::Parent

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <unordered_map>
#include <vector>

namespace mapio {

namespace {
constexpr char kMagic[8] = {'P', 'S', 'X', 'M', 'A', 'P', '\0', 0};
constexpr uint16_t kVersion = 1;
constexpr uint32_t kNoParent = 0xFFFFFFFFu;
} // namespace

bool writeMap(const std::string& path, const entt::registry& reg,
              const ComponentRegistry& types)
{
    std::unordered_map<entt::entity, uint32_t> localId;
    std::vector<entt::entity> order;
    reg.view<entt::entity>().each([&](entt::entity e) {
        localId.emplace(e, uint32_t(order.size()));
        order.push_back(e);
    });

    // Body (entities) first, so the pool is fully populated before we emit it.
    ByteWriter w;
    w.u32(uint32_t(order.size()));
    for (entt::entity e : order) {
        w.u32(localId[e]);
        uint32_t parent = kNoParent;
        if (const auto* p = reg.try_get<eng::ecs::Parent>(e))
            if (p->value != entt::null) {
                auto it = localId.find(p->value);
                if (it != localId.end()) parent = it->second;
            }
        w.u32(parent);

        std::vector<const ComponentType*> present;
        for (const ComponentType& t : types.types())
            if (t.has(reg, e)) present.push_back(&t);
        w.u16(uint16_t(present.size()));
        for (const ComponentType* t : present) {
            w.u16(t->stableTypeId);
            const std::size_t lenAt = w.size();
            w.u32(0); // byteLen placeholder
            const std::size_t start = w.size();
            t->serialize(reg, e, w);
            w.patchU32(lenAt, uint32_t(w.size() - start));
        }
    }

    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    out.write(kMagic, 8);
    const uint16_t ver = kVersion, flags = 0;
    out.write(reinterpret_cast<const char*>(&ver), 2);   // host LE assumed
    out.write(reinterpret_cast<const char*>(&flags), 2);

    // String pool.
    const uint32_t poolCount = uint32_t(w.pool().size());
    out.write(reinterpret_cast<const char*>(&poolCount), 4);
    for (const std::string& s : w.pool()) {
        const uint32_t len = uint32_t(s.size());
        out.write(reinterpret_cast<const char*>(&len), 4);
        out.write(s.data(), std::streamsize(len));
    }

    // Body.
    out.write(reinterpret_cast<const char*>(w.bytes().data()),
              std::streamsize(w.bytes().size()));
    return bool(out);
}

bool readMap(const std::string& path, entt::registry& outReg,
             const ComponentRegistry& types)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::vector<uint8_t> file((std::istreambuf_iterator<char>(in)),
                              std::istreambuf_iterator<char>());
    if (file.size() < 12) return false;
    if (std::memcmp(file.data(), kMagic, 8) != 0) return false;
    uint16_t ver;
    std::memcpy(&ver, file.data() + 8, 2);
    if (ver > kVersion) return false; // too new to understand

    const uint8_t* p = file.data() + 12;
    const uint8_t* end = file.data() + file.size();

    // String pool (raw, not via ByteReader — the pool is what ByteReader needs).
    auto rd32 = [&](const uint8_t*& q) -> uint32_t {
        uint32_t v = 0;
        for (int i = 0; i < 4; ++i) v |= uint32_t(q[i]) << (8 * i);
        q += 4; return v;
    };
    if (end - p < 4) return false;
    const uint32_t poolCount = rd32(p);
    std::vector<std::string> pool;
    pool.reserve(poolCount);
    for (uint32_t i = 0; i < poolCount; ++i) {
        if (end - p < 4) return false;
        const uint32_t len = rd32(p);
        if (std::size_t(end - p) < len) return false;
        pool.emplace_back(reinterpret_cast<const char*>(p), len);
        p += len;
    }

    ByteReader r(p, std::size_t(end - p), pool);
    const uint32_t count = r.u32();

    // First pass: create entities and remember parent local ids.
    std::vector<entt::entity> byLocal(count, entt::null);
    std::vector<uint32_t> parentOf(count, kNoParent);
    for (uint32_t i = 0; i < count && r.ok(); ++i) {
        const uint32_t local = r.u32();
        const uint32_t parent = r.u32();
        if (local >= count) return false;
        entt::entity e = outReg.create();
        byLocal[local] = e;
        parentOf[local] = parent;
        const uint16_t comps = r.u16();
        for (uint16_t c = 0; c < comps && r.ok(); ++c) {
            const uint16_t typeId = r.u16();
            const uint32_t len = r.u32();
            const std::size_t before = r.remaining();
            const ComponentType* t = types.find(typeId);
            if (t) {
                t->deserialize(outReg, e, r);
                // Guard against a deserializer that read the wrong length.
                const std::size_t consumed = before - r.remaining();
                if (consumed < len) r.skip(len - consumed);
            } else {
                r.skip(len); // unknown component: skip its payload
            }
        }
    }
    if (!r.ok()) return false;

    // Second pass: wire parent links now that all entities exist.
    for (uint32_t local = 0; local < count; ++local) {
        if (parentOf[local] == kNoParent) continue;
        if (parentOf[local] >= count) return false;
        outReg.emplace_or_replace<eng::ecs::Parent>(
            byLocal[local], eng::ecs::Parent{byLocal[parentOf[local]]});
    }
    return true;
}

bool dumpMap(const std::string& path, const ComponentRegistry& types)
{
    // Implemented in Task 5.
    (void)path; (void)types;
    return false;
}

} // namespace mapio
```

Add `#include <cstring>` to the includes (used by `std::memcmp`/`std::memcpy`).

- [ ] **Step 6: Add the CMake test target**

In `CMakeLists.txt` under `if(BUILD_TESTING)` add:

```cmake
  add_executable(map_serializer_tests
    game/tests/MapSerializerTests.cpp
    game/src/scene/ByteStream.cpp
    game/src/scene/ComponentRegistry.cpp
    game/src/scene/MapSerializer.cpp)
  target_include_directories(map_serializer_tests
    PRIVATE game/src/scene engine/include third_party)
  target_link_libraries(map_serializer_tests PRIVATE glm::glm EnTT::EnTT)
  add_test(NAME map_serializer COMMAND map_serializer_tests)
```

- [ ] **Step 7: Run to verify it passes**

```bash
cmake --build build --target map_serializer_tests && \
  ctest --test-dir build -R map_serializer --output-on-failure
```
Expected: `MapSerializerTests OK`, `1 passed`.

- [ ] **Step 8: Commit**

```bash
git add game/src/scene/ByteStream.h game/src/scene/ByteStream.cpp \
        game/src/scene/MapSerializer.h game/src/scene/MapSerializer.cpp \
        game/tests/MapSerializerTests.cpp CMakeLists.txt
git commit -m "feat(map): versioned binary .map serializer with skip-unknown"
```

---

## Task 5: `dumpMap` text dump (`--dump` recovery tool)

**Files:**
- Modify: `game/src/scene/MapSerializer.cpp` (replace the stub `dumpMap`).
- Test: extend `game/tests/MapSerializerTests.cpp`.

- [ ] **Step 1: Add a failing assertion to the existing test**

In `game/tests/MapSerializerTests.cpp`, just before `std::remove(path.c_str());`, add:

```cpp
    // dumpMap prints without crashing and reports success on a valid file.
    require(mapio::dumpMap(path, mapio::coreRegistry()), "dumpMap succeeds on a valid file");
    require(!mapio::dumpMap("does_not_exist.map", mapio::coreRegistry()),
            "dumpMap fails on a missing file");
```

- [ ] **Step 2: Run to verify it fails**

```bash
cmake --build build --target map_serializer_tests && \
  ctest --test-dir build -R map_serializer --output-on-failure
```
Expected: FAIL — `dumpMap` returns false on the valid file (still a stub).

- [ ] **Step 3: Implement `dumpMap`**

Replace the stub `dumpMap` in `game/src/scene/MapSerializer.cpp` with:

```cpp
bool dumpMap(const std::string& path, const ComponentRegistry& types)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::vector<uint8_t> file((std::istreambuf_iterator<char>(in)),
                              std::istreambuf_iterator<char>());
    if (file.size() < 12 || std::memcmp(file.data(), kMagic, 8) != 0) return false;
    uint16_t ver;
    std::memcpy(&ver, file.data() + 8, 2);

    const uint8_t* p = file.data() + 12;
    const uint8_t* end = file.data() + file.size();
    auto rd32 = [&](const uint8_t*& q) -> uint32_t {
        uint32_t v = 0; for (int i = 0; i < 4; ++i) v |= uint32_t(q[i]) << (8 * i);
        q += 4; return v;
    };
    std::printf("PSXMAP version %u\n", unsigned(ver));
    if (end - p < 4) return false;
    const uint32_t poolCount = rd32(p);
    std::printf("string pool (%u):\n", poolCount);
    std::vector<std::string> pool;
    for (uint32_t i = 0; i < poolCount; ++i) {
        if (end - p < 4) return false;
        const uint32_t len = rd32(p);
        if (std::size_t(end - p) < len) return false;
        pool.emplace_back(reinterpret_cast<const char*>(p), len);
        std::printf("  [%u] \"%s\"\n", i, pool.back().c_str());
        p += len;
    }

    ByteReader r(p, std::size_t(end - p), pool);
    const uint32_t count = r.u32();
    std::printf("entities (%u):\n", count);
    for (uint32_t i = 0; i < count && r.ok(); ++i) {
        const uint32_t local = r.u32();
        const uint32_t parent = r.u32();
        const uint16_t comps = r.u16();
        std::printf("  entity %u parent=%d components=%u\n", local,
                    parent == kNoParent ? -1 : int(parent), unsigned(comps));
        for (uint16_t c = 0; c < comps && r.ok(); ++c) {
            const uint16_t typeId = r.u16();
            const uint32_t len = r.u32();
            const ComponentType* t = types.find(typeId);
            std::printf("    - %s (id %u, %u bytes)\n",
                        t ? t->name : "<unknown>", unsigned(typeId), len);
            r.skip(len);
        }
    }
    return r.ok();
}
```

- [ ] **Step 4: Run to verify it passes**

```bash
cmake --build build --target map_serializer_tests && \
  ctest --test-dir build -R map_serializer --output-on-failure
```
Expected: `MapSerializerTests OK`, `1 passed`. The dump prints to stdout during the run.

- [ ] **Step 5: Commit**

```bash
git add game/src/scene/MapSerializer.cpp game/tests/MapSerializerTests.cpp
git commit -m "feat(map): dumpMap text inspector for binary .map files"
```

---

## Task 6: Full-suite green + plan handoff

- [ ] **Step 1: Build and run every test**

```bash
cmake --build build && ctest --test-dir build --output-on-failure
```
Expected: all tests pass, including the new `byte_stream`, `component_registry`,
and `map_serializer`.

- [ ] **Step 2: Confirm the data core is self-contained**

Verify none of `game/src/scene/*` includes Ogre, ImGui, SDL, or Jolt headers:

```bash
grep -rEn "Ogre|imgui|SDL|Jolt" game/src/scene/ || echo "clean: no renderer/GUI/physics deps"
```
Expected: `clean: no renderer/GUI/physics deps`. (`eng/Physics.h` is included for
the `ShapeKind`/`BodyLayer` enums only — that is a header of plain enums, not a
Jolt dependency, and the grep above targets `Jolt` specifically.)

- [ ] **Step 3: Commit any final fixups (if needed), then stop.**

Plan 1 is complete: the `.map` format reads and writes an `entt` registry with
full round-trip, forward-compatible skipping, and a text dump — all headless and
unit-tested. Plan 2 (editor viewport, picking, gizmos, panels, undo/redo) builds
on `coreRegistry()` and `writeMap`/`readMap` from here.

---

## Self-Review

**Spec coverage (against `2026-07-23-level-editor-app-design.md`):**
- `.map` binary header/pool/entity/component layout → Task 4. ✓
- Skip-unknown via `byteLen` → Task 4 test + reader. ✓
- Versioning + reject-too-new → Task 4 (`ver > kVersion`). ✓
- `MeshRenderer` persists asset path not handle → `MeshSource` + Task 3/4. ✓
- Runtime-derived components not serialized (`NodeRef`/`BodyRef`/`WorldTransform`/`Dirty`) → not registered. ✓
- ComponentRegistry keystone (name/id/factory/has/remove/serialize/deserialize) → Task 3. ✓
- Gameplay components (Collider/PlayerSpawn/Exit/EnemySpawn/Pickup/Trigger) → Task 2/3. ✓
- Parent links survive via local-id remap → Task 4 two-pass. ✓
- `--dump` text recovery → Task 5. ✓
- Round-trip / skip / completeness / primitive tests → Tasks 1,3,4. ✓
- Deferred (inspector GUI, physics bodies, generator) → explicitly out of scope. ✓

**Type consistency:** `mapio::ByteWriter`/`ByteReader`, `mapio::ComponentType`,
`mapio::ComponentRegistry`, `mapio::coreRegistry()`, `mapio::MeshSource`,
`mapio::writeMap`/`readMap`/`dumpMap`, `game::Collider/PlayerSpawn/Exit/EnemySpawn/Pickup/Trigger`
are used identically across tasks. Stable ids `1,2,3,4,10,11,12,13,14,15` are unique.

**Placeholder scan:** The only intentionally-incomplete code is the Step-4 sketch
in Task 4, which is explicitly flagged "do not commit on its own" and fully
replaced in Step 5. No `TODO`/`TBD` remain in committed steps.
