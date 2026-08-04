# Lua Scripting Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add Lua 5.4 scripting to the engine so a script can be attached to an entity in the scene editor and receive `start()` / `update(dt)` like Unity or Godot.

**Architecture:** A new static library `eng_script` at the framework layer owns a `sol::state`, a chunk cache, and an instance pool. Entities carry an authored `Scripts` component (a list of `{path, props}`) and a host-owned `ScriptState` component holding live instance handles — the same authored/runtime split the engine already uses for `NodeRef` and `BodyRef`. Bindings are hybrid: hand-written usertypes for the entity handle and the modules that are not fields, plus a reflection-driven proxy over `ComponentRegistry` so every registered component is scriptable with no Lua-side work.

**Tech Stack:** C++20, Lua 5.4.7 (PUC-Rio), sol2 v3.3.0, EnTT, glm, CPM, CMake, CTest.

**Spec:** `docs/superpowers/specs/2026-08-03-lua-scripting-design.md`

## Global Constraints

- **Never clean-build.** OGRE is compiled from source. Build single targets with `cmake --build build --target <t> -j8`. Never `rm -rf build`. If the tree breaks, `cmake -S . -B build` regenerates makefiles without discarding objects.
- **Layer rule:** dependencies point downward only — `eng_core` → `eng_platform` → `eng_systems` → `eng_framework` → `eng`. `eng_script` sits at the *framework* layer. An upward include fails the `layering` ctest.
- **`<sol/sol.hpp>` must never appear in a public `eng/` header.** `ScriptHost` is PIMPL'd for this reason.
- **Stable type ids are a file format.** `Scripts` takes id **33**. Never renumber an existing id.
- **Serialisation is append-only.** New trailing fields only; decoding stops when the payload runs out so older files load with defaults.
- **Tests are plain `int main()` executables** using a local `require(bool, const char*)` helper that prints to `std::cerr` and calls `std::exit(1)`. No test framework. Registered with `add_executable` + `add_test` inside the existing `if(BUILD_TESTING)` block.
- **Lua chunk names are prefixed with `@`** (e.g. `@scripts/door.lua`) — Lua treats a leading `@` as a filename, which is what makes tracebacks read `scripts/door.lua:12:` instead of `[string "..."]:12:`.
- **Log category** comes from a leading `Word:` prefix — `eng::log::error("Script: ...")` lands in the `DebugConsole` under category `Script`.
- **Commit after every task.** End commit messages with `Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>`.

---

## File Structure

| File | Responsibility |
|---|---|
| `cmake/Dependencies.cmake` | Lua 5.4.7 static target + sol2 INTERFACE target |
| `engine/include/eng/ecs/components/Scripts.h` | `ScriptProp`, `ScriptRef`, `Scripts` — authored data only |
| `engine/include/eng/script/ScriptState.h` | Host-owned instance handles; no sol types |
| `engine/include/eng/script/ScriptConfig.h` | `ScriptConfig` — script root, hot-reload flag |
| `engine/include/eng/script/ScriptHost.h` | PIMPL'd public API: construction, ticks, reload, console |
| `engine/src/script/ScriptHost.cpp` | `Impl`, sol state ownership, tick orchestration |
| `engine/src/script/ScriptChunkCache.{h,cpp}` | path → class table, load, reload |
| `engine/src/script/ScriptError.{h,cpp}` | traceback handler, report formatting, quarantine |
| `engine/src/script/ScriptInstance.{h,cpp}` | instance pool, lifecycle dispatch |
| `engine/src/script/bind/BindMath.cpp` | `vec3` usertype |
| `engine/src/script/bind/BindEntity.cpp` | entity handle usertype |
| `engine/src/script/bind/BindComponents.cpp` | reflection proxy |
| `engine/src/script/bind/BindWorld.cpp` | `world`, `log`, `event` |
| `engine/src/script/bind/BindInput.cpp` | `input` |
| `engine/src/script/bind/BindPhysics.cpp` | `physics` |
| `engine/src/script/ScriptContactBridge.{h,cpp}` | `BodyHandle` → entity, contact queue |
| `engine/src/script/ScriptConsole.cpp` | `lua`, `script.*` console commands |

Bindings are split per module so editing one does not recompile all of them — sol2 is template-heavy and this tree never clean-builds.

---

## Task 1: Build plumbing — Lua, sol2, `eng_script`

**Files:**
- Modify: `cmake/Dependencies.cmake` (append at end)
- Modify: `CMakeLists.txt` (after the `eng_framework` block, ~line 372)
- Modify: `tools/check_layering.py:33-50` (`SOURCE_RULES`), `:52+` (`HEADER_RULES`)
- Create: `engine/include/eng/script/ScriptConfig.h`
- Create: `engine/src/script/ScriptHost.cpp` (stub)
- Create: `engine/include/eng/script/ScriptHost.h` (stub)

**Interfaces:**
- Consumes: nothing.
- Produces: CMake targets `lua` (STATIC) and `eng_sol2` (INTERFACE); `eng_script` (STATIC) linking `eng_framework`, `lua`, `eng_sol2`. `eng` links `eng_script` whole-archive. `eng::script::ScriptConfig`.

- [ ] **Step 1: Add Lua and sol2 to `cmake/Dependencies.cmake`**

Append to the end of the file:

```cmake
# --- Lua 5.4 -----------------------------------------------------------------
# PUC-Rio ships no CMake, so the recipe lives here rather than in a stranger's
# fork of the build system. Explicit source list, not a glob: a CMake glob does
# not re-run when a file appears, which turns a version bump into a link error
# nobody can explain.
CPMAddPackage(
    NAME lua
    GITHUB_REPOSITORY lua/lua
    GIT_TAG v5.4.7
    DOWNLOAD_ONLY YES
)
add_library(lua STATIC
    "${lua_SOURCE_DIR}/lapi.c"     "${lua_SOURCE_DIR}/lcode.c"
    "${lua_SOURCE_DIR}/lctype.c"   "${lua_SOURCE_DIR}/ldebug.c"
    "${lua_SOURCE_DIR}/ldo.c"      "${lua_SOURCE_DIR}/ldump.c"
    "${lua_SOURCE_DIR}/lfunc.c"    "${lua_SOURCE_DIR}/lgc.c"
    "${lua_SOURCE_DIR}/llex.c"     "${lua_SOURCE_DIR}/lmem.c"
    "${lua_SOURCE_DIR}/lobject.c"  "${lua_SOURCE_DIR}/lopcodes.c"
    "${lua_SOURCE_DIR}/lparser.c"  "${lua_SOURCE_DIR}/lstate.c"
    "${lua_SOURCE_DIR}/lstring.c"  "${lua_SOURCE_DIR}/ltable.c"
    "${lua_SOURCE_DIR}/ltm.c"      "${lua_SOURCE_DIR}/lundump.c"
    "${lua_SOURCE_DIR}/lvm.c"      "${lua_SOURCE_DIR}/lzio.c"
    "${lua_SOURCE_DIR}/lauxlib.c"  "${lua_SOURCE_DIR}/lbaselib.c"
    "${lua_SOURCE_DIR}/lcorolib.c" "${lua_SOURCE_DIR}/ldblib.c"
    "${lua_SOURCE_DIR}/liolib.c"   "${lua_SOURCE_DIR}/lmathlib.c"
    "${lua_SOURCE_DIR}/loadlib.c"  "${lua_SOURCE_DIR}/loslib.c"
    "${lua_SOURCE_DIR}/lstrlib.c"  "${lua_SOURCE_DIR}/ltablib.c"
    "${lua_SOURCE_DIR}/lutf8lib.c" "${lua_SOURCE_DIR}/linit.c")
# lua.c and luac.c are the standalone interpreter and compiler: both define
# main(), and linking either into a game is a duplicate-symbol error.
target_include_directories(lua PUBLIC "${lua_SOURCE_DIR}")
target_compile_definitions(lua PUBLIC LUA_USE_LINUX)
target_link_libraries(lua PUBLIC ${CMAKE_DL_LIBS} m)
# Third-party C: build it without our warning set.
target_compile_options(lua PRIVATE -w)

# --- sol2 (C++ <-> Lua binding) ----------------------------------------------
# The multi-header distribution, not single/include: the single header is one
# enormous TU that defeats the precompiled header.
CPMAddPackage(
    NAME sol2
    GITHUB_REPOSITORY ThePhD/sol2
    GIT_TAG v3.3.0
    DOWNLOAD_ONLY YES
)
add_library(eng_sol2 INTERFACE)
target_include_directories(eng_sol2 INTERFACE "${sol2_SOURCE_DIR}/include")
target_link_libraries(eng_sol2 INTERFACE lua)
# Safeties trade a little speed for real error messages on a bad call from Lua.
# Worth it while scripts are being written; off in Release.
target_compile_definitions(eng_sol2 INTERFACE
    $<$<CONFIG:Debug>:SOL_ALL_SAFETIES_ON=1>)
```

- [ ] **Step 2: Create the config header**

`engine/include/eng/script/ScriptConfig.h`:

```cpp
#pragma once
#include <string>

namespace eng::script {

// Everything the script host needs decided before it loads anything.
struct ScriptConfig {
    // Logical directory the watcher polls and paths resolve against. Scripts
    // are read by explicit path through eng::assets::resolve, so this is NOT a
    // resource location in assets.toml.
    std::string root = "scripts";
    // Poll the root for changes and swap class tables in place. Development
    // only: a shipped build has nothing to reload from.
    bool hotReload = false;
};

} // namespace eng::script
```

- [ ] **Step 3: Create the host header stub**

`engine/include/eng/script/ScriptHost.h`:

```cpp
#pragma once
#include <eng/script/ScriptConfig.h>

#include <memory>

namespace eng::ecs { class World; }

namespace eng::script {

// Owns the Lua state, the loaded-chunk cache and the live script instances for
// one World.
//
// Not an eng::System. A System's contract is a single update(dt), and this
// host's whole point is that its callbacks land at three different places in
// the frame -- fixed_update before a physics step, contacts after it, update
// with the rest of presentation. A generic update(dt) would hide the one thing
// a reader needs to know.
//
// PIMPL'd because sol2 is template-heavy: <sol/sol.hpp> stays behind this
// header so including it does not cost every consumer a second of compile time.
class ScriptHost {
public:
    ScriptHost(ecs::World& world, const ScriptConfig& config);
    ~ScriptHost();
    ScriptHost(const ScriptHost&) = delete;
    ScriptHost& operator=(const ScriptHost&) = delete;

private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};

} // namespace eng::script
```

- [ ] **Step 4: Create the host source stub**

`engine/src/script/ScriptHost.cpp`:

```cpp
#include <eng/script/ScriptHost.h>

#include <eng/ecs/World.h>

#include <sol/sol.hpp>

namespace eng::script {

struct ScriptHost::Impl {
    Impl(ecs::World& w, const ScriptConfig& c) : world(w), config(c)
    {
        // Deliberately not open_libraries() with no arguments: that opens io,
        // os and package, which let a gameplay script read the filesystem and
        // load native modules. A level script has no business doing either.
        lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::string,
                           sol::lib::table, sol::lib::debug);
    }

    ecs::World& world;
    ScriptConfig config;
    sol::state lua;
};

ScriptHost::ScriptHost(ecs::World& world, const ScriptConfig& config)
    : mImpl(std::make_unique<Impl>(world, config))
{
}

ScriptHost::~ScriptHost() = default;

} // namespace eng::script
```

- [ ] **Step 5: Add the `eng_script` target to `CMakeLists.txt`**

Insert immediately after the `target_link_libraries(eng_framework ...)` line (~line 372), *before* the `# Facade: owns application lifetime` comment:

```cmake
# Lua scripting. Its own target rather than part of eng_framework: that library
# is linked by every engine test and by the editor's preview world, and keeping
# the VM separate makes "who depends on Lua" a link fact instead of a habit.
add_library(eng_script STATIC engine/src/script/ScriptHost.cpp)
target_include_directories(eng_script PRIVATE third_party engine/src)
target_link_libraries(eng_script
  PUBLIC "$<LINK_LIBRARY:WHOLE_ARCHIVE,eng_framework>"
  PRIVATE eng_sol2)
```

Then change the `eng` link line from:

```cmake
target_link_libraries(eng PUBLIC "$<LINK_LIBRARY:WHOLE_ARCHIVE,eng_framework>")
```

to:

```cmake
target_link_libraries(eng PUBLIC "$<LINK_LIBRARY:WHOLE_ARCHIVE,eng_script>")
```

`eng_script` links `eng_framework` whole-archive itself, so `eng` still gets everything it did.

- [ ] **Step 6: Add `eng_script` to the hardening loop and give it a PCH**

Change the hardening `foreach` list to include `eng_script`:

```cmake
foreach(_layer eng_imgui eng_core eng_rhi eng_platform eng_model_import eng_systems
               eng_framework eng_script eng)
  eng_target_hardening(${_layer})
endforeach()
```

In the `if(ENABLE_PCH)` block, after the `eng_framework` PCH line:

```cmake
  # sol2 is the most expensive header in the tree and every binding TU needs it.
  target_precompile_headers(eng_script PRIVATE <sol/sol.hpp> <entt/entt.hpp>
                            <glm/glm.hpp>)
```

- [ ] **Step 7: Add the layering rules**

In `tools/check_layering.py`, add to `SOURCE_RULES` immediately after the `("src/ecs/", "framework")` line:

```python
    ("src/script/", "framework"),
```

And to `HEADER_RULES`, alongside the other directory entries:

```python
    ("script/", "framework"),
```

- [ ] **Step 8: Configure and build**

Run:
```sh
cmake -S . -B build && cmake --build build --target eng_script -j8
```
Expected: CPM fetches lua and sol2, both configure, `eng_script` links. First build of sol2 headers is slow — this is expected and is why the PCH exists.

- [ ] **Step 9: Verify layering still passes**

Run: `python3 tools/check_layering.py`
Expected: exits 0, no violations reported.

- [ ] **Step 10: Verify the game still links**

Run: `cmake --build build --target game -j8`
Expected: links clean. This proves the `eng` → `eng_script` → `eng_framework` re-chaining did not drop a symbol.

- [ ] **Step 11: Commit**

```bash
git add cmake/Dependencies.cmake CMakeLists.txt tools/check_layering.py \
        engine/include/eng/script engine/src/script
git commit -m "build: add Lua 5.4 + sol2 and the eng_script layer

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

## Task 2: The `Scripts` component and its serialisation

**Files:**
- Create: `engine/include/eng/ecs/components/Scripts.h`
- Create: `engine/include/eng/script/ScriptState.h`
- Modify: `engine/include/eng/ecs/Components.h` (add the include)
- Modify: `engine/src/ecs/ComponentRegistry.cpp` (serialiser + registration, ~line 533)
- Create: `engine/tests/ScriptSerializeTests.cpp`
- Modify: `CMakeLists.txt` (test target, next to `component_reflect_tests` ~line 802)

**Interfaces:**
- Consumes: `eng::io::ByteWriter`/`ByteReader` from Task 1's unchanged engine.
- Produces: `eng::ecs::ScriptProp{key, type, b, n, v, s}`, `eng::ecs::ScriptRef{path, props, enabled}`, `eng::ecs::Scripts{items}`, `eng::script::ScriptState{instances}`. Registered as `"Scripts"`, stable id `33`.

- [ ] **Step 1: Write the failing test**

`engine/tests/ScriptSerializeTests.cpp`:

```cpp
#include <eng/ecs/ComponentRegistry.h>
#include <eng/ecs/components/Scripts.h>
#include <eng/io/ByteStream.h>

#include <entt/entt.hpp>

#include <cstdlib>
#include <iostream>

using namespace eng;
using namespace eng::ecs;

static void require(bool c, const char* m)
{
    if (!c) { std::cerr << "ScriptSerializeTests: " << m << '\n'; std::exit(1); }
}

// Round-trips one Scripts component through the registered serialiser.
static Scripts roundTrip(const Scripts& in)
{
    ComponentRegistry reg;
    registerEngineComponents(reg);
    const ComponentType* type = reg.find(33);
    require(type != nullptr, "Scripts is registered under stable id 33");

    entt::registry src;
    const entt::entity a = src.create();
    src.emplace<Scripts>(a, in);

    io::ByteWriter w;
    type->serialize(src, a, w);

    entt::registry dst;
    const entt::entity b = dst.create();
    io::ByteReader r(w.bytes().data(), w.size(), w.pool());
    type->deserialize(dst, b, r, uint32_t(w.size()));
    require(dst.all_of<Scripts>(b), "deserialise emplaced the component");
    return dst.get<Scripts>(b);
}

int main()
{
    // --- every prop type survives the round trip ---------------------------
    {
        Scripts in;
        ScriptRef door;
        door.path = "scripts/door.lua";
        door.enabled = true;
        door.props.push_back({"open", ScriptProp::Type::Bool, true, 0.0f, {}, ""});
        door.props.push_back({"speed", ScriptProp::Type::Number, false, 2.5f, {}, ""});
        door.props.push_back({"tint", ScriptProp::Type::Vec3, false, 0.0f,
                              glm::vec3(0.1f, 0.2f, 0.3f), ""});
        door.props.push_back({"label", ScriptProp::Type::String, false, 0.0f, {},
                              "north gate"});
        door.props.push_back({"target", ScriptProp::Type::Entity, false, 0.0f, {},
                              "lever_a"});
        in.items.push_back(door);

        const Scripts out = roundTrip(in);
        require(out.items.size() == 1, "one script survives");
        const ScriptRef& r = out.items[0];
        require(r.path == "scripts/door.lua", "path survives");
        require(r.enabled, "enabled survives");
        require(r.props.size() == 5, "every prop survives");
        require(r.props[0].key == "open" && r.props[0].b, "bool prop");
        require(r.props[1].n == 2.5f, "number prop is exact as f32");
        require(r.props[2].v.y == 0.2f, "vec3 prop");
        require(r.props[3].s == "north gate", "string prop");
        require(r.props[4].type == ScriptProp::Type::Entity &&
                    r.props[4].s == "lever_a",
                "entity prop keeps its type, not just its text");
    }

    // --- several scripts on one entity keep author order -------------------
    {
        Scripts in;
        in.items.push_back({"scripts/health.lua", {}, true});
        in.items.push_back({"scripts/patrol.lua", {}, false});
        const Scripts out = roundTrip(in);
        require(out.items.size() == 2, "both scripts survive");
        require(out.items[0].path == "scripts/health.lua" &&
                    out.items[1].path == "scripts/patrol.lua",
                "author order is the serialised order -- it decides run order");
        require(!out.items[1].enabled, "a disabled script stays disabled");
    }

    // --- an empty component is legal ---------------------------------------
    {
        const Scripts out = roundTrip(Scripts{});
        require(out.items.empty(), "no scripts round-trips as no scripts");
    }

    // --- a truncated payload decodes to defaults, never out of bounds ------
    {
        ComponentRegistry reg;
        registerEngineComponents(reg);
        const ComponentType* type = reg.find(33);
        Scripts in;
        in.items.push_back({"scripts/door.lua", {}, true});
        entt::registry src;
        const entt::entity a = src.create();
        src.emplace<Scripts>(a, in);
        io::ByteWriter w;
        type->serialize(src, a, w);

        entt::registry dst;
        const entt::entity b = dst.create();
        io::ByteReader r(w.bytes().data(), 1, w.pool()); // one byte of a u16
        type->deserialize(dst, b, r, 1u);
        require(dst.all_of<Scripts>(b),
                "a truncated payload still emplaces, at defaults");
        require(dst.get<Scripts>(b).items.empty(),
                "and reads no garbage items out of it");
    }

    std::cout << "ScriptSerializeTests: ok\n";
    return 0;
}
```

- [ ] **Step 2: Run it to verify it fails**

Add the target first (Step 5 below is the same edit; do it now so the test can run). Then:

Run: `cmake --build build --target script_serialize_tests -j8`
Expected: FAIL to compile — `eng/ecs/components/Scripts.h` does not exist.

- [ ] **Step 3: Write the component header**

`engine/include/eng/ecs/components/Scripts.h`:

```cpp
#pragma once
#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace eng::ecs {

// One authored value on a script instance -- the serialised-field equivalent
// from Unity or Godot, and what makes one script reusable across many entities
// instead of one script per door.
//
// A tagged struct rather than a variant: the byte serialiser, the TOML writer
// and the inspector all switch on the same enum, and a variant would make each
// of them a visitor for no gain at five types.
struct ScriptProp {
    enum class Type : uint8_t { Bool, Number, String, Vec3, Entity };

    std::string key;
    Type type = Type::Number;
    bool b = false;
    // f32, not double. ByteWriter's vocabulary is f32 and so is every other
    // component's; a Lua number narrows on the way in, which costs nothing for
    // an authored tuning value and keeps the payload the same shape as the
    // rest of the format.
    float n = 0.0f;
    glm::vec3 v{0.0f};
    // String's value, and also Entity's target entity name. Entity is
    // mechanically a string: it exists as its own type so the inspector can
    // offer a picker and the cooker can check the name resolves.
    std::string s;
};

// One script attached to an entity.
struct ScriptRef {
    std::string path;              // logical asset path, "scripts/door.lua"
    std::vector<ScriptProp> props; // authored, per instance
    bool enabled = true;
};

// Every script on this entity. `items` order is author order, which is the
// order their callbacks run in -- so a Health script can be guaranteed to see
// a frame before the Patrol script that reads it.
//
// Purely authored data: no runtime state lives here. That is what makes hot
// reload trivial (drop ScriptState, rebuild from this) and what keeps sol out
// of eng/ecs.
struct Scripts {
    std::vector<ScriptRef> items;
};

} // namespace eng::ecs
```

- [ ] **Step 4: Write the runtime-state header**

`engine/include/eng/script/ScriptState.h`:

```cpp
#pragma once
#include <cstdint>
#include <vector>

namespace eng::script {

// The live script instances for this entity's Scripts, owned by ScriptHost.
// Written by the host, never by an author or a caller -- the same contract as
// NodeRef and BodyRef, and stranding one by hand leaks an instance the same way
// writing a BodyRef strands a body.
//
// Holds opaque pool slots rather than sol types, so this header does not drag
// the VM into anything that includes it. Deliberately NOT registered with the
// ComponentRegistry: it is never serialised and never authored.
struct ScriptState {
    std::vector<uint32_t> instances; // slots in ScriptHost's instance pool
};

} // namespace eng::script
```

- [ ] **Step 5: Add the test target to `CMakeLists.txt`**

Immediately after the `add_test(NAME component_reflect ...)` line (~line 809):

```cmake
  add_executable(script_serialize_tests
                 engine/tests/ScriptSerializeTests.cpp
                 engine/src/ecs/ComponentRegistry.cpp
                 engine/src/io/ByteStream.cpp)
  target_include_directories(script_serialize_tests
                             PRIVATE engine/include third_party)
  target_link_libraries(script_serialize_tests PRIVATE glm::glm EnTT::EnTT)
  add_test(NAME script_serialize COMMAND script_serialize_tests)
```

- [ ] **Step 6: Run it to verify it now fails on the missing registration**

Run: `cmake -S . -B build && cmake --build build --target script_serialize_tests -j8 && ./build/script_serialize_tests`
Expected: compiles, then FAILS at runtime with `Scripts is registered under stable id 33`.

- [ ] **Step 7: Write the serialiser and register the component**

In `engine/src/ecs/ComponentRegistry.cpp`, add near the other hand-written serialisers (after `deName`, ~line 61):

```cpp
void serScripts(const entt::registry& r, entt::entity e, ByteWriter& w)
{
    const auto& s = r.get<Scripts>(e);
    w.u16(uint16_t(s.items.size()));
    for (const ScriptRef& item : s.items) {
        w.str(item.path);
        w.u8(item.enabled ? 1u : 0u);
        w.u16(uint16_t(item.props.size()));
        for (const ScriptProp& p : item.props) {
            w.str(p.key);
            w.u8(uint8_t(p.type));
            switch (p.type) {
            case ScriptProp::Type::Bool:   w.u8(p.b ? 1u : 0u); break;
            case ScriptProp::Type::Number: w.f32(p.n); break;
            case ScriptProp::Type::Vec3:   w.vec3(p.v); break;
            case ScriptProp::Type::String:
            case ScriptProp::Type::Entity: w.str(p.s); break;
            }
        }
    }
}

void deScripts(entt::registry& r, entt::entity e, ByteReader& b, uint32_t bytes)
{
    Scripts s;
    // The reader is already bounded to this component and yields zeros past
    // the end, so a truncated payload simply produces fewer items rather than
    // reading into the next component's bytes.
    if (bytes >= 2) {
        const uint16_t count = b.u16();
        for (uint16_t i = 0; i < count && b.ok(); ++i) {
            ScriptRef item;
            item.path = b.str();
            item.enabled = b.u8() != 0;
            const uint16_t propCount = b.u16();
            for (uint16_t j = 0; j < propCount && b.ok(); ++j) {
                ScriptProp p;
                p.key = b.str();
                p.type = ScriptProp::Type(b.u8());
                switch (p.type) {
                case ScriptProp::Type::Bool:   p.b = b.u8() != 0; break;
                case ScriptProp::Type::Number: p.n = b.f32(); break;
                case ScriptProp::Type::Vec3:   p.v = b.vec3(); break;
                case ScriptProp::Type::String:
                case ScriptProp::Type::Entity: p.s = b.str(); break;
                default:
                    // An id from a newer build. The payload is no longer
                    // parseable from here, so stop rather than guess.
                    b.invalidate();
                    break;
                }
                if (b.ok()) item.props.push_back(std::move(p));
            }
            if (b.ok()) s.items.push_back(std::move(item));
        }
    }
    r.emplace_or_replace<Scripts>(e, std::move(s));
}
```

At the end of `registerEngineComponents`, replace the trailing comment and add the registration:

```cpp
    reg.add(reflectedComponent<PrimitiveMesh>("PrimitiveMesh", 32));
    // Not a reflected component: Field{type, offset} describes a fixed layout,
    // and this is a variable-length list of heterogeneous values. So it hand
    // -writes its serialiser, like the five above it.
    reg.add({"Scripts", 33, addDefault<Scripts>, has<Scripts>, remove<Scripts>,
             serScripts, deScripts});
```

And fix the stale sentence just above it — the header's `kFirstApplicationTypeId` is 64, not 33:

```cpp
    // renumbering the game's three would reinterpret every .map on disk. The
    // engine therefore continues above them; applications start at
    // kFirstApplicationTypeId (64), which is where the two blocks stop being
    // able to meet at all.
```

- [ ] **Step 8: Add the include to `Components.h`**

In `engine/include/eng/ecs/Components.h`, add alongside the other component includes, in alphabetical position:

```cpp
#include <eng/ecs/components/Scripts.h>
```

- [ ] **Step 9: Run the test to verify it passes**

Run: `cmake --build build --target script_serialize_tests -j8 && ./build/script_serialize_tests`
Expected: `ScriptSerializeTests: ok`

- [ ] **Step 10: Verify no existing map format regressed**

Run: `cmake --build build --target component_reflect_tests scene_tests -j8 && ctest --test-dir build -R "component_reflect|scene_tests" --output-on-failure`
Expected: both PASS. Adding id 33 must not disturb ids 1–32.

- [ ] **Step 11: Commit**

```bash
git add engine/include/eng/ecs/components/Scripts.h \
        engine/include/eng/script/ScriptState.h \
        engine/include/eng/ecs/Components.h \
        engine/src/ecs/ComponentRegistry.cpp \
        engine/tests/ScriptSerializeTests.cpp CMakeLists.txt
git commit -m "feat(ecs): Scripts component, stable id 33

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

## Task 3: Chunk cache and load-time errors with tracebacks

**Files:**
- Create: `engine/src/script/ScriptError.h`, `engine/src/script/ScriptError.cpp`
- Create: `engine/src/script/ScriptChunkCache.h`, `engine/src/script/ScriptChunkCache.cpp`
- Modify: `CMakeLists.txt` (add both sources to `eng_script`)
- Create: `engine/tests/ScriptErrorTests.cpp`
- Modify: `CMakeLists.txt` (test target)

**Interfaces:**
- Consumes: `ScriptConfig` (Task 1).
- Produces:
  - `eng::script::ScriptError::report(const std::string& path, const std::string& where, const std::string& message, const std::string& entityLabel)` → formats and logs.
  - `eng::script::installTracebackHandler(sol::state&)` → stores `debug.traceback` in the registry and returns a `sol::protected_function` to use as an error handler.
  - `class ScriptChunkCache { explicit ScriptChunkCache(sol::state&); sol::table* classFor(const std::string& logicalPath); bool reload(const std::string& logicalPath); void clear(); }` — `classFor` returns `nullptr` on any failure, having already reported it.

- [ ] **Step 1: Write the failing test**

`engine/tests/ScriptErrorTests.cpp`:

```cpp
#include "script/ScriptChunkCache.h"
#include "script/ScriptError.h"

#include <sol/sol.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

using namespace eng::script;

static void require(bool c, const char* m)
{
    if (!c) { std::cerr << "ScriptErrorTests: " << m << '\n'; std::exit(1); }
}

static std::filesystem::path writeScript(const std::string& name,
                                         const std::string& body)
{
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "eng_script_error_tests";
    std::filesystem::create_directories(dir);
    const std::filesystem::path file = dir / name;
    std::ofstream(file) << body;
    return file;
}

int main()
{
    sol::state lua;
    lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::string,
                       sol::lib::table, sol::lib::debug);

    // --- a valid chunk returning a table becomes a class -------------------
    {
        const auto file = writeScript("good.lua",
                                      "local M = {}\n"
                                      "function M:update(dt) end\n"
                                      "return M\n");
        ScriptChunkCache cache(lua);
        sol::table* cls = cache.classFor(file.string());
        require(cls != nullptr, "a chunk returning a table loads");
        require((*cls)["update"].valid(), "its methods are reachable");
        require(cache.classFor(file.string()) == cls,
                "a second request is served from the cache, not re-run");
    }

    // --- a syntax error is reported at load, and yields no class -----------
    {
        const auto file = writeScript("broken.lua", "function M:update( end\n");
        ScriptChunkCache cache(lua);
        require(cache.classFor(file.string()) == nullptr,
                "a chunk that does not parse produces no class");
        require(cache.classFor(file.string()) == nullptr,
                "and asking twice does not crash on the cached failure");
    }

    // --- a chunk that does not return a table is a load error --------------
    {
        const auto file = writeScript("noreturn.lua", "local M = {}\n");
        ScriptChunkCache cache(lua);
        require(cache.classFor(file.string()) == nullptr,
                "a script must return its class table");
    }

    // --- a chunk that throws while loading is caught, not propagated -------
    {
        const auto file = writeScript("throws.lua",
                                      "error('boom at load')\n"
                                      "return {}\n");
        ScriptChunkCache cache(lua);
        require(cache.classFor(file.string()) == nullptr,
                "an error raised while the chunk runs is a load failure");
    }

    // --- the traceback handler produces a multi-frame trace ----------------
    {
        installTracebackHandler(lua);
        const auto file = writeScript("deep.lua",
                                      "local M = {}\n"
                                      "local function inner() error('deep') end\n"
                                      "local function outer() inner() end\n"
                                      "function M:update(dt) outer() end\n"
                                      "return M\n");
        ScriptChunkCache cache(lua);
        sol::table* cls = cache.classFor(file.string());
        require(cls != nullptr, "the deep script loads");

        sol::protected_function fn = (*cls)["update"];
        fn.error_handler = tracebackHandler(lua);
        const sol::protected_function_result r = fn(*cls, 0.016f);
        require(!r.valid(), "the call fails");
        const std::string msg = r.get<std::string>();
        require(msg.find("stack traceback") != std::string::npos,
                "the failure carries a traceback, not just the top frame");
        require(msg.find("deep.lua") != std::string::npos,
                "and the traceback names the chunk by its path");
    }

    std::cout << "ScriptErrorTests: ok\n";
    return 0;
}
```

- [ ] **Step 2: Add the test target and run it to verify it fails**

In `CMakeLists.txt`, after the `script_serialize` test:

```cmake
  add_executable(script_error_tests engine/tests/ScriptErrorTests.cpp)
  target_include_directories(script_error_tests PRIVATE engine/include engine/src)
  target_link_libraries(script_error_tests PRIVATE eng_script eng_sol2 glm::glm)
  add_test(NAME script_error COMMAND script_error_tests)
```

Run: `cmake -S . -B build && cmake --build build --target script_error_tests -j8`
Expected: FAIL — `script/ScriptChunkCache.h` and `script/ScriptError.h` do not exist.

- [ ] **Step 3: Write `ScriptError.h`**

`engine/src/script/ScriptError.h`:

```cpp
#pragma once
#include <sol/sol.hpp>

#include <string>

namespace eng::script {

// Stores debug.traceback in the Lua registry so every protected call can use it
// as its message handler. Call once per state, before anything is loaded.
//
// Without it a failure reports only the line that raised -- which for a script
// that calls a helper that calls a binding tells you nothing about how it got
// there. With it the report carries the whole Lua call stack.
void installTracebackHandler(sol::state& lua);

// The handler installed above. Assign to protected_function::error_handler.
sol::protected_function tracebackHandler(sol::state& lua);

// One error, one report. `where` is the callback or phase ("update", "load");
// `subject` names the entity when there is one ("entity 'iron_door' #42") and
// is empty at load time, when no entity is involved yet.
//
// Logged through eng::log::error with a "Script:" prefix, which is what puts it
// under the DebugConsole's `Script` category -- the console derives a category
// from a leading Word: prefix.
void reportScriptError(const std::string& path, const std::string& where,
                       const std::string& subject, const std::string& message);

} // namespace eng::script
```

- [ ] **Step 4: Write `ScriptError.cpp`**

`engine/src/script/ScriptError.cpp`:

```cpp
#include "script/ScriptError.h"

#include <eng/Log.h>

namespace eng::script {
namespace {
// A registry key of our own, so we are not fighting anything Lua or sol2 keeps
// there under a name of theirs.
constexpr const char* kTracebackKey = "eng_script_traceback";
} // namespace

void installTracebackHandler(sol::state& lua)
{
    lua.registry()[kTracebackKey] = lua["debug"]["traceback"];
}

sol::protected_function tracebackHandler(sol::state& lua)
{
    return lua.registry()[kTracebackKey];
}

void reportScriptError(const std::string& path, const std::string& where,
                       const std::string& subject, const std::string& message)
{
    // One block per error rather than one line: the traceback is multi-line and
    // splitting it across log calls interleaves it with whatever else is
    // logging that frame.
    if (subject.empty())
        log::error("Script: %s in %s():\n  %s", path.c_str(), where.c_str(),
                   message.c_str());
    else
        log::error("Script: %s on %s in %s():\n  %s", path.c_str(),
                   subject.c_str(), where.c_str(), message.c_str());
}

} // namespace eng::script
```

- [ ] **Step 5: Write `ScriptChunkCache.h`**

`engine/src/script/ScriptChunkCache.h`:

```cpp
#pragma once
#include <sol/sol.hpp>

#include <string>
#include <unordered_map>

namespace eng::script {

// Logical script path -> the class table the chunk returned.
//
// A .lua file is a chunk that returns a table: its *class*. It is run exactly
// once per path, no matter how many entities carry it, and every instance
// reaches its methods through __index. That is what makes attaching the same
// script to two hundred entities cost two hundred small tables and one chunk.
class ScriptChunkCache {
public:
    explicit ScriptChunkCache(sol::state& lua) : mLua(lua) {}

    // The class table for `path`, loading it on first request. Returns nullptr
    // on any failure -- unreadable, does not parse, raises while running, or
    // does not return a table -- having already reported it. A failure is
    // remembered, so a broken script reports once rather than once per entity
    // per frame.
    //
    // The returned pointer is stable until reload() or clear(): the map holds
    // the table by value and nothing rehashes it in between.
    sol::table* classFor(const std::string& path);

    // Re-runs the chunk and replaces the class table in place. Returns false
    // and KEEPS the previous class if the new source fails -- a half-typed save
    // must not kill a running level.
    bool reload(const std::string& path);

    void clear();

private:
    // Reads the file and runs it. Reports and returns nullopt on failure.
    std::optional<sol::table> load(const std::string& path);

    sol::state& mLua;
    std::unordered_map<std::string, sol::table> mClasses;
    // Paths whose last load failed. Kept so classFor() does not retry (and
    // re-report) every frame; reload() clears the entry.
    std::unordered_map<std::string, bool> mFailed;
};

} // namespace eng::script
```

- [ ] **Step 6: Write `ScriptChunkCache.cpp`**

`engine/src/script/ScriptChunkCache.cpp`:

```cpp
#include "script/ScriptChunkCache.h"

#include "script/ScriptError.h"

#include <fstream>
#include <sstream>

namespace eng::script {

std::optional<sol::table> ScriptChunkCache::load(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        reportScriptError(path, "load", {}, "cannot open the file");
        return std::nullopt;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    const std::string source = ss.str();

    // The '@' prefix is what makes a traceback read "scripts/door.lua:12:"
    // instead of '[string "local M = {}..."]:12:'. Lua treats a chunk name
    // starting with @ as a filename; without it every frame quotes source.
    const std::string chunkName = "@" + path;

    sol::load_result chunk = mLua.load(source, chunkName);
    if (!chunk.valid()) {
        const sol::error err = chunk;
        reportScriptError(path, "load", {}, err.what());
        return std::nullopt;
    }

    sol::protected_function fn = chunk;
    fn.error_handler = tracebackHandler(mLua);
    const sol::protected_function_result result = fn();
    if (!result.valid()) {
        const sol::error err = result;
        reportScriptError(path, "load", {}, err.what());
        return std::nullopt;
    }
    if (result.get_type() != sol::type::table) {
        reportScriptError(path, "load", {},
                          "the chunk must return its class table "
                          "(add 'return M' at the end)");
        return std::nullopt;
    }
    return result.get<sol::table>();
}

sol::table* ScriptChunkCache::classFor(const std::string& path)
{
    if (const auto it = mClasses.find(path); it != mClasses.end())
        return &it->second;
    if (mFailed.count(path))
        return nullptr;

    std::optional<sol::table> cls = load(path);
    if (!cls) {
        mFailed[path] = true;
        return nullptr;
    }
    return &mClasses.emplace(path, std::move(*cls)).first->second;
}

bool ScriptChunkCache::reload(const std::string& path)
{
    std::optional<sol::table> cls = load(path);
    if (!cls)
        return false; // the previous class, if any, stays live
    mFailed.erase(path);
    mClasses.insert_or_assign(path, std::move(*cls));
    return true;
}

void ScriptChunkCache::clear()
{
    mClasses.clear();
    mFailed.clear();
}

} // namespace eng::script
```

- [ ] **Step 7: Add both sources to the `eng_script` target**

In `CMakeLists.txt`:

```cmake
add_library(eng_script STATIC engine/src/script/ScriptHost.cpp
                              engine/src/script/ScriptError.cpp
                              engine/src/script/ScriptChunkCache.cpp)
```

- [ ] **Step 8: Run the test to verify it passes**

Run: `cmake -S . -B build && cmake --build build --target script_error_tests -j8 && ./build/script_error_tests`
Expected: `ScriptErrorTests: ok`

- [ ] **Step 9: Commit**

```bash
git add engine/src/script/ScriptError.h engine/src/script/ScriptError.cpp \
        engine/src/script/ScriptChunkCache.h engine/src/script/ScriptChunkCache.cpp \
        engine/tests/ScriptErrorTests.cpp CMakeLists.txt
git commit -m "feat(script): chunk cache with traceback-carrying load errors

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

## Task 4: Instance pool, `start` and `update`

**Files:**
- Create: `engine/src/script/ScriptInstance.h`, `engine/src/script/ScriptInstance.cpp`
- Modify: `engine/include/eng/script/ScriptHost.h` (add `tick`)
- Modify: `engine/src/script/ScriptHost.cpp` (wire the pool)
- Modify: `CMakeLists.txt` (source + test target)
- Create: `engine/tests/ScriptHostTests.cpp`

**Interfaces:**
- Consumes: `ScriptChunkCache` (Task 3), `Scripts`/`ScriptState` (Task 2).
- Produces:
  - `struct ScriptInstance { entt::entity entity; std::string path; sol::table self; bool started; bool quarantined; }`
  - `class ScriptInstancePool { uint32_t create(...); ScriptInstance* get(uint32_t); void release(uint32_t); }` — slot reuse via a free list.
  - `void ScriptHost::tick(float dt)` — creates instances for entities whose `Scripts` has no `ScriptState`, runs `start()` on unstarted instances, then `update(dt)` on started ones.
  - `ScriptHost::instanceCount() const` and `ScriptHost::isQuarantined(entt::entity, const std::string& path) const` — test seams only, both `const`.

- [ ] **Step 1: Write the failing test**

`engine/tests/ScriptHostTests.cpp`:

```cpp
#include <eng/ecs/World.h>
#include <eng/ecs/components/Scripts.h>
#include <eng/script/ScriptConfig.h>
#include <eng/script/ScriptHost.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

using namespace eng;
using namespace eng::ecs;
using namespace eng::script;

static void require(bool c, const char* m)
{
    if (!c) { std::cerr << "ScriptHostTests: " << m << '\n'; std::exit(1); }
}

static std::filesystem::path gDir;

static std::string writeScript(const std::string& name, const std::string& body)
{
    std::filesystem::create_directories(gDir);
    const std::filesystem::path file = gDir / name;
    std::ofstream(file) << body;
    return file.string();
}

// Attaches one script with no props.
static void attach(World& w, entt::entity e, const std::string& path,
                   bool enabled = true)
{
    auto& s = w.registry().get_or_emplace<Scripts>(e);
    s.items.push_back({path, {}, enabled});
}

int main()
{
    gDir = std::filesystem::temp_directory_path() / "eng_script_host_tests";

    // A script that records its callbacks into a global counter table, so the
    // test observes behaviour through Lua rather than through host internals.
    const std::string counter =
        "local M = {}\n"
        "function M:start() calls = (calls or 0) + 1; started = true end\n"
        "function M:update(dt) ticks = (ticks or 0) + 1; last_dt = dt end\n"
        "return M\n";

    // --- start runs once, on the tick AFTER attach -------------------------
    {
        World world;
        ScriptHost host(world, ScriptConfig{});
        const std::string path = writeScript("counter.lua", counter);
        const entt::entity e = world.create("thing");
        attach(world, e, path);

        require(!host.luaGlobalBool("started"),
                "start has not run merely because the component exists");
        host.tick(0.016f);
        require(host.luaGlobalBool("started"), "start runs on the first tick");
        require(host.luaGlobalNumber("calls") == 1.0, "start ran once");
        require(host.luaGlobalNumber("ticks") == 1.0,
                "update runs on the same tick, after start");

        host.tick(0.032f);
        require(host.luaGlobalNumber("calls") == 1.0, "start does not run again");
        require(host.luaGlobalNumber("ticks") == 2.0, "update runs every tick");
        require(host.luaGlobalNumber("last_dt") > 0.03,
                "update receives the frame's dt");
    }

    // --- self is per entity; the class is shared ---------------------------
    {
        World world;
        ScriptHost host(world, ScriptConfig{});
        const std::string path = writeScript(
            "perentity.lua",
            "local M = {}\n"
            "function M:start() self_count = (self_count or 0) + 1\n"
            "  self.mine = 0 end\n"
            "function M:update(dt) self.mine = self.mine + 1\n"
            "  total = (total or 0) + self.mine end\n"
            "return M\n");
        attach(world, world.create("a"), path);
        attach(world, world.create("b"), path);

        host.tick(0.016f);
        require(host.luaGlobalNumber("self_count") == 2.0,
                "each entity gets its own instance");
        require(host.luaGlobalNumber("total") == 2.0,
                "and its own state -- 1 + 1, not 1 + 2 from a shared table");
        require(host.instanceCount() == 2, "two live instances");
    }

    // --- several scripts on one entity run in author order -----------------
    {
        World world;
        ScriptHost host(world, ScriptConfig{});
        const std::string first = writeScript(
            "first.lua",
            "local M = {}\n"
            "function M:update(dt) order = (order or '') .. 'A' end\n"
            "return M\n");
        const std::string second = writeScript(
            "second.lua",
            "local M = {}\n"
            "function M:update(dt) order = (order or '') .. 'B' end\n"
            "return M\n");
        const entt::entity e = world.create("both");
        attach(world, e, first);
        attach(world, e, second);
        host.tick(0.016f);
        require(host.luaGlobalString("order") == "AB",
                "author order is run order");
    }

    // --- a disabled script never instantiates ------------------------------
    {
        World world;
        ScriptHost host(world, ScriptConfig{});
        const std::string path = writeScript("counter2.lua", counter);
        attach(world, world.create("off"), path, /*enabled=*/false);
        host.tick(0.016f);
        require(host.instanceCount() == 0, "a disabled script is not created");
        require(!host.luaGlobalBool("started"), "and never starts");
    }

    // --- a script missing update is legal ----------------------------------
    {
        World world;
        ScriptHost host(world, ScriptConfig{});
        const std::string path = writeScript(
            "startonly.lua",
            "local M = {}\n"
            "function M:start() ok = true end\n"
            "return M\n");
        attach(world, world.create("s"), path);
        host.tick(0.016f);
        host.tick(0.016f);
        require(host.luaGlobalBool("ok"),
                "a script may define only the callbacks it needs");
    }

    // --- a script that fails to load leaves the entity alone ---------------
    {
        World world;
        ScriptHost host(world, ScriptConfig{});
        attach(world, world.create("bad"), writeScript("bad.lua", "return 5\n"));
        host.tick(0.016f);
        require(host.instanceCount() == 0,
                "a chunk that returns a non-table creates no instance");
        host.tick(0.016f); // must not crash or re-report forever
    }

    std::cout << "ScriptHostTests: ok\n";
    return 0;
}
```

- [ ] **Step 2: Add the test target and run it to verify it fails**

In `CMakeLists.txt`, after the `script_error` test:

```cmake
  add_executable(script_host_tests engine/tests/ScriptHostTests.cpp)
  target_include_directories(script_host_tests PRIVATE engine/include engine/src)
  target_link_libraries(script_host_tests PRIVATE eng_script glm::glm EnTT::EnTT)
  add_test(NAME script_host COMMAND script_host_tests)
```

Run: `cmake -S . -B build && cmake --build build --target script_host_tests -j8`
Expected: FAIL — `ScriptHost` has no `tick`, `instanceCount`, or `luaGlobal*`.

- [ ] **Step 3: Write `ScriptInstance.h`**

`engine/src/script/ScriptInstance.h`:

```cpp
#pragma once
#include <sol/sol.hpp>

#include <entt/entt.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace eng::script {

// One live script on one entity.
struct ScriptInstance {
    entt::entity entity = entt::null;
    std::string path;
    sol::table self;          // per-entity state; __index -> the class table
    bool started = false;     // start() has run
    bool quarantined = false; // errored once; skipped until revived
    bool alive = false;       // slot is in use
};

// Slot-allocated instances with a free list.
//
// A pool rather than a map keyed by entity: ScriptState stores plain uint32_t
// slots, which is what keeps the VM out of a component header. Slots are reused
// and there is no generation counter, because a ScriptState is destroyed with
// its entity and nothing outside the host holds a slot across that.
class ScriptInstancePool {
public:
    uint32_t create(entt::entity e, std::string path, sol::table self);
    // nullptr when the slot is free -- a caller iterating a stale ScriptState
    // must not resurrect one.
    ScriptInstance* get(uint32_t slot);
    const ScriptInstance* get(uint32_t slot) const;
    void release(uint32_t slot);
    void clear();
    std::size_t liveCount() const { return mLive; }

    // Every live slot, for the console listing and reload.
    template <typename Fn> void forEach(Fn&& fn)
    {
        for (uint32_t i = 0; i < mSlots.size(); ++i)
            if (mSlots[i].alive) fn(i, mSlots[i]);
    }

private:
    std::vector<ScriptInstance> mSlots;
    std::vector<uint32_t> mFree;
    std::size_t mLive = 0;
};

} // namespace eng::script
```

- [ ] **Step 4: Write `ScriptInstance.cpp`**

`engine/src/script/ScriptInstance.cpp`:

```cpp
#include "script/ScriptInstance.h"

namespace eng::script {

uint32_t ScriptInstancePool::create(entt::entity e, std::string path,
                                    sol::table self)
{
    uint32_t slot;
    if (!mFree.empty()) {
        slot = mFree.back();
        mFree.pop_back();
    } else {
        slot = uint32_t(mSlots.size());
        mSlots.emplace_back();
    }
    ScriptInstance& inst = mSlots[slot];
    inst.entity = e;
    inst.path = std::move(path);
    inst.self = std::move(self);
    inst.started = false;
    inst.quarantined = false;
    inst.alive = true;
    ++mLive;
    return slot;
}

ScriptInstance* ScriptInstancePool::get(uint32_t slot)
{
    if (slot >= mSlots.size() || !mSlots[slot].alive) return nullptr;
    return &mSlots[slot];
}

const ScriptInstance* ScriptInstancePool::get(uint32_t slot) const
{
    if (slot >= mSlots.size() || !mSlots[slot].alive) return nullptr;
    return &mSlots[slot];
}

void ScriptInstancePool::release(uint32_t slot)
{
    if (slot >= mSlots.size() || !mSlots[slot].alive) return;
    ScriptInstance& inst = mSlots[slot];
    inst.alive = false;
    inst.started = false;
    // Drop the Lua reference explicitly: leaving it would keep the instance
    // table (and everything it captured) alive until the slot is reused, which
    // is a leak with an unbounded lifetime on a level that never reloads.
    inst.self = sol::lua_nil;
    inst.path.clear();
    inst.entity = entt::null;
    mFree.push_back(slot);
    --mLive;
}

void ScriptInstancePool::clear()
{
    mSlots.clear();
    mFree.clear();
    mLive = 0;
}

} // namespace eng::script
```

- [ ] **Step 5: Extend the public header**

Replace the `private:` section of `engine/include/eng/script/ScriptHost.h` with:

```cpp
    // --- frame -----------------------------------------------------------
    // Creates instances for entities whose Scripts have none, runs start() on
    // any that have not started, then update(dt) on the rest.
    //
    // Call once per frame after gameplay has mutated components and BEFORE
    // World::sync(), the same slot tickComponentSystems() occupies.
    void tick(float dt);

    // --- test and tooling seams ------------------------------------------
    std::size_t instanceCount() const;
    bool luaGlobalBool(const char* name) const;
    double luaGlobalNumber(const char* name) const;
    std::string luaGlobalString(const char* name) const;

private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
```

Add `#include <cstddef>` and `#include <string>` to the header's include block.

- [ ] **Step 6: Implement `tick` in `ScriptHost.cpp`**

Replace `engine/src/script/ScriptHost.cpp` with:

```cpp
#include <eng/script/ScriptHost.h>

#include "script/ScriptChunkCache.h"
#include "script/ScriptError.h"
#include "script/ScriptInstance.h"

#include <eng/ecs/World.h>
#include <eng/ecs/components/Name.h>
#include <eng/ecs/components/Scripts.h>
#include <eng/script/ScriptState.h>

#include <sol/sol.hpp>

namespace eng::script {

struct ScriptHost::Impl {
    Impl(ecs::World& w, const ScriptConfig& c) : world(w), config(c), chunks(lua)
    {
        // Deliberately not open_libraries() with no arguments: that opens io,
        // os and package, which let a gameplay script read the filesystem and
        // load native modules. A level script has no business doing either.
        lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::string,
                           sol::lib::table, sol::lib::debug);
        installTracebackHandler(lua);
    }

    // How an entity reads in an error report. Built on demand: an error is
    // rare and a name lookup per instance per frame is not.
    std::string subject(entt::entity e) const
    {
        const auto* name = world.registry().try_get<ecs::Name>(e);
        std::string label = "entity ";
        if (name && !name->value.empty()) label += "'" + name->value + "' ";
        label += "#" + std::to_string(uint32_t(entt::to_integral(e)));
        return label;
    }

    // Calls one callback on one instance, protected, with a traceback. Returns
    // false when it failed -- in which case the instance is already quarantined
    // and reported.
    template <typename... Args>
    bool call(ScriptInstance& inst, const char* name, Args&&... args)
    {
        if (inst.quarantined) return false;
        const sol::object fn = inst.self[name];
        if (!fn.valid() || fn.get_type() != sol::type::function)
            return true; // a script defines only the callbacks it needs

        sol::protected_function pf = fn.as<sol::protected_function>();
        pf.error_handler = tracebackHandler(lua);
        const sol::protected_function_result r =
            pf(inst.self, std::forward<Args>(args)...);
        if (r.valid()) return true;

        const sol::error err = r;
        reportScriptError(inst.path, name, subject(inst.entity), err.what());
        // Quarantine rather than retry: the same traceback sixty times a second
        // buries every other line in the console, and a script that failed once
        // in update will fail again next frame for the same reason.
        inst.quarantined = true;
        return false;
    }

    // Builds instances for any entity carrying Scripts but no ScriptState.
    void instantiateNew()
    {
        auto& reg = world.registry();
        // Snapshot: creating a ScriptState inside the view's own pool would
        // invalidate the iteration.
        std::vector<entt::entity> pending;
        for (const entt::entity e : reg.view<ecs::Scripts>())
            if (!reg.all_of<ScriptState>(e)) pending.push_back(e);

        for (const entt::entity e : pending) {
            const ecs::Scripts& scripts = reg.get<ecs::Scripts>(e);
            ScriptState state;
            for (const ecs::ScriptRef& ref : scripts.items) {
                if (!ref.enabled) continue;
                sol::table* cls = chunks.classFor(ref.path);
                if (!cls) continue; // already reported

                sol::table self = lua.create_table();
                sol::table mt = lua.create_table();
                mt["__index"] = *cls;
                self[sol::metatable_key] = mt;
                state.instances.push_back(
                    instances.create(e, ref.path, std::move(self)));
            }
            reg.emplace<ScriptState>(e, std::move(state));
        }
    }

    ecs::World& world;
    ScriptConfig config;
    sol::state lua;
    ScriptChunkCache chunks;
    ScriptInstancePool instances;
};

ScriptHost::ScriptHost(ecs::World& world, const ScriptConfig& config)
    : mImpl(std::make_unique<Impl>(world, config))
{
}

ScriptHost::~ScriptHost() = default;

void ScriptHost::tick(float dt)
{
    mImpl->instantiateNew();

    // Snapshot the slots before dispatching: a script may spawn an entity (and
    // therefore create instances) from inside start() or update(), and the pool
    // must not be reallocated under the loop.
    std::vector<uint32_t> slots;
    slots.reserve(mImpl->instances.liveCount());
    mImpl->instances.forEach([&](uint32_t slot, ScriptInstance&) {
        slots.push_back(slot);
    });

    // start() before any update(), across all instances rather than per
    // instance: a script's start must be able to see every other script's
    // entity already built, which is only true if no update has moved anything.
    for (const uint32_t slot : slots) {
        ScriptInstance* inst = mImpl->instances.get(slot);
        if (!inst || inst->started || inst->quarantined) continue;
        inst->started = true; // set first: a failing start must not retry
        mImpl->call(*inst, "start");
    }

    for (const uint32_t slot : slots) {
        ScriptInstance* inst = mImpl->instances.get(slot);
        if (!inst || !inst->started) continue;
        mImpl->call(*inst, "update", dt);
    }
}

std::size_t ScriptHost::instanceCount() const
{
    return mImpl->instances.liveCount();
}

bool ScriptHost::luaGlobalBool(const char* name) const
{
    const sol::object o = mImpl->lua[name];
    return o.valid() && o.is<bool>() && o.as<bool>();
}

double ScriptHost::luaGlobalNumber(const char* name) const
{
    const sol::object o = mImpl->lua[name];
    return (o.valid() && o.is<double>()) ? o.as<double>() : 0.0;
}

std::string ScriptHost::luaGlobalString(const char* name) const
{
    const sol::object o = mImpl->lua[name];
    return (o.valid() && o.is<std::string>()) ? o.as<std::string>()
                                             : std::string();
}

} // namespace eng::script
```

- [ ] **Step 7: Add `ScriptInstance.cpp` to the target**

```cmake
add_library(eng_script STATIC engine/src/script/ScriptHost.cpp
                              engine/src/script/ScriptError.cpp
                              engine/src/script/ScriptChunkCache.cpp
                              engine/src/script/ScriptInstance.cpp)
```

- [ ] **Step 8: Run the test to verify it passes**

Run: `cmake -S . -B build && cmake --build build --target script_host_tests -j8 && ./build/script_host_tests`
Expected: `ScriptHostTests: ok`

- [ ] **Step 9: Commit**

```bash
git add engine/src/script/ScriptInstance.h engine/src/script/ScriptInstance.cpp \
        engine/src/script/ScriptHost.cpp engine/include/eng/script/ScriptHost.h \
        engine/tests/ScriptHostTests.cpp CMakeLists.txt
git commit -m "feat(script): instance pool with start/update lifecycle

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

## Task 5: `fixed_update`, `on_destroy`, and quarantine behaviour

**Files:**
- Modify: `engine/include/eng/script/ScriptHost.h` (add `fixedTick`, `revive`)
- Modify: `engine/src/script/ScriptHost.cpp`
- Modify: `engine/tests/ScriptHostTests.cpp` (append cases)
- Modify: `engine/tests/ScriptErrorTests.cpp` (append runtime-error cases)

**Interfaces:**
- Consumes: Task 4's pool and `Impl::call`.
- Produces:
  - `void ScriptHost::fixedTick(float dt)` — `fixed_update(dt)` on started instances.
  - `std::size_t ScriptHost::revive()` — un-quarantines every instance, returns how many.
  - `bool ScriptHost::isQuarantined(entt::entity, const std::string& path) const`.
  - `on_destroy` dispatched from an entt `on_destroy<ScriptState>` hook.

- [ ] **Step 1: Append the failing cases to `ScriptHostTests.cpp`**

Insert before the final `std::cout` line:

```cpp
    // --- fixed_update is separate from update ------------------------------
    {
        World world;
        ScriptHost host(world, ScriptConfig{});
        const std::string path = writeScript(
            "fixed.lua",
            "local M = {}\n"
            "function M:update(dt) u = (u or 0) + 1 end\n"
            "function M:fixed_update(dt) f = (f or 0) + 1; fdt = dt end\n"
            "return M\n");
        attach(world, world.create("f"), path);

        // A fixed step before the first tick has no started instance to run on:
        // fixed_update must not fire before start.
        host.fixedTick(0.008f);
        require(host.luaGlobalNumber("f") == 0.0,
                "fixed_update does not run before start");

        host.tick(0.016f);
        host.fixedTick(0.008f);
        host.fixedTick(0.008f);
        require(host.luaGlobalNumber("u") == 1.0, "update ran once");
        require(host.luaGlobalNumber("f") == 2.0,
                "fixed_update runs once per physics step, not per frame");
        require(host.luaGlobalNumber("fdt") < 0.01,
                "and receives the fixed delta, not the frame delta");
    }

    // --- on_destroy fires, and the entity is still readable ----------------
    {
        World world;
        ScriptHost host(world, ScriptConfig{});
        const std::string path = writeScript(
            "bye.lua",
            "local M = {}\n"
            "function M:start() end\n"
            "function M:on_destroy() gone = true end\n"
            "return M\n");
        const entt::entity e = world.create("doomed");
        attach(world, e, path);
        host.tick(0.016f);
        require(host.instanceCount() == 1, "instance exists");

        world.destroyHierarchy(e);
        require(host.luaGlobalBool("gone"), "on_destroy fired");
        require(host.instanceCount() == 0,
                "and the slot was released -- no leak per destroyed entity");
    }

    // --- destroying a parent takes the child's scripts with it -------------
    {
        World world;
        ScriptHost host(world, ScriptConfig{});
        const std::string path = writeScript(
            "count_destroy.lua",
            "local M = {}\n"
            "function M:on_destroy() destroyed = (destroyed or 0) + 1 end\n"
            "return M\n");
        const entt::entity parent = world.create("rig");
        const entt::entity child = world.create("attachment");
        world.setParent(child, parent);
        attach(world, parent, path);
        attach(world, child, path);
        host.tick(0.016f);
        world.destroyHierarchy(parent);
        require(host.luaGlobalNumber("destroyed") == 2.0,
                "both halves of a rig get on_destroy");
        require(host.instanceCount() == 0, "and both slots are released");
    }
```

- [ ] **Step 2: Append the failing quarantine cases to `ScriptErrorTests.cpp`**

These need a `World` and a host, so add the includes at the top of the file:

```cpp
#include <eng/ecs/World.h>
#include <eng/ecs/components/Scripts.h>
#include <eng/script/ScriptConfig.h>
#include <eng/script/ScriptHost.h>
```

And insert before the final `std::cout` line:

```cpp
    // --- a runtime error quarantines exactly one instance ------------------
    {
        eng::ecs::World world;
        ScriptHost host(world, eng::script::ScriptConfig{});
        const auto file = writeScript("boom.lua",
                                      "local M = {}\n"
                                      "function M:update(dt)\n"
                                      "  ticks = (ticks or 0) + 1\n"
                                      "  error('kaboom')\n"
                                      "end\n"
                                      "return M\n");
        const std::string path = file.string();

        const entt::entity a = world.create("a");
        const entt::entity b = world.create("b");
        for (const entt::entity e : {a, b})
            world.registry().get_or_emplace<eng::ecs::Scripts>(e).items.push_back(
                {path, {}, true});

        host.tick(0.016f);
        require(host.luaGlobalNumber("ticks") == 2.0,
                "both instances ran and both failed");
        require(host.isQuarantined(a, path) && host.isQuarantined(b, path),
                "each failing instance is quarantined on its own");

        host.tick(0.016f);
        require(host.luaGlobalNumber("ticks") == 2.0,
                "a quarantined instance does not run again -- no per-frame spam");

        require(host.revive() == 2, "revive returns how many it restored");
        host.tick(0.016f);
        require(host.luaGlobalNumber("ticks") == 4.0, "and they run again");
    }

    // --- one broken instance does not stop its siblings --------------------
    {
        eng::ecs::World world;
        ScriptHost host(world, eng::script::ScriptConfig{});
        const std::string bad =
            writeScript("bad_sibling.lua",
                        "local M = {}\n"
                        "function M:update(dt) error('no') end\n"
                        "return M\n")
                .string();
        const std::string good =
            writeScript("good_sibling.lua",
                        "local M = {}\n"
                        "function M:update(dt) fine = (fine or 0) + 1 end\n"
                        "return M\n")
                .string();
        const entt::entity e = world.create("mixed");
        auto& s = world.registry().get_or_emplace<eng::ecs::Scripts>(e);
        s.items.push_back({bad, {}, true});
        s.items.push_back({good, {}, true});

        host.tick(0.016f);
        host.tick(0.016f);
        require(host.luaGlobalNumber("fine") == 2.0,
                "the healthy script keeps ticking after its neighbour died");
    }

    // --- a failing start does not retry every frame ------------------------
    {
        eng::ecs::World world;
        ScriptHost host(world, eng::script::ScriptConfig{});
        const std::string path =
            writeScript("badstart.lua",
                        "local M = {}\n"
                        "function M:start() starts = (starts or 0) + 1\n"
                        "  error('bad start') end\n"
                        "return M\n")
                .string();
        world.registry()
            .get_or_emplace<eng::ecs::Scripts>(world.create("s"))
            .items.push_back({path, {}, true});
        host.tick(0.016f);
        host.tick(0.016f);
        require(host.luaGlobalNumber("starts") == 1.0,
                "start is attempted once, even when it throws");
    }
```

- [ ] **Step 3: Run both tests to verify they fail**

Run: `cmake --build build --target script_host_tests script_error_tests -j8`
Expected: FAIL to compile — no `fixedTick`, `revive`, or `isQuarantined`.

- [ ] **Step 4: Extend the public header**

Add to `engine/include/eng/script/ScriptHost.h`, before the test seams, and add `#include <entt/fwd.hpp>` plus `namespace entt { enum class entity : std::uint32_t; }` is *not* needed — include `<entt/entity/fwd.hpp>`:

```cpp
    // Runs fixed_update(dt) on every started instance.
    //
    // Defined as "immediately before a physics step", not "on the fixed clock".
    // That keeps the contract true in a mode with no fixed loop (MapPlay steps
    // physics from onPresent), where the caller simply calls this first.
    void fixedTick(float dt);

    // Un-quarantines every instance that errored. Returns how many. Called by
    // the console and by a successful hot reload.
    std::size_t revive();

    // Whether this entity's instance of `path` is currently quarantined.
    bool isQuarantined(entt::entity e, const std::string& path) const;
```

- [ ] **Step 5: Implement them in `ScriptHost.cpp`**

Add to `Impl`, at the end of the constructor body:

```cpp
        // on_destroy fires here rather than from World::destroy, because the
        // World must not know scripting exists. entt calls this while the
        // entity is still valid, so a script can read its own components one
        // last time.
        world.registry().on_destroy<ScriptState>()
            .template connect<&Impl::onStateDestroyed>(this);
```

Add to `Impl`:

```cpp
    void onStateDestroyed(entt::registry& reg, entt::entity e)
    {
        const ScriptState& state = reg.get<ScriptState>(e);
        // Teardown: a script that tries to spawn from on_destroy is spawning
        // into a registry that is mid-mutation. The flag is read by the world
        // bindings (Task 8), which refuse and log instead.
        const bool wasTearingDown = tearingDown;
        tearingDown = true;
        for (const uint32_t slot : state.instances) {
            if (ScriptInstance* inst = instances.get(slot)) {
                call(*inst, "on_destroy");
                instances.release(slot);
            }
        }
        tearingDown = wasTearingDown;
    }

    bool tearingDown = false;
```

And add a destructor to `Impl` so the hook does not outlive the host:

```cpp
    ~Impl()
    {
        // The registry outlives this host in the editor's preview world, and a
        // dangling listener would call through freed memory on the next level
        // teardown.
        world.registry().on_destroy<ScriptState>()
            .template disconnect<&Impl::onStateDestroyed>(this);
        instances.clear();
        chunks.clear();
    }
```

Then the three public methods:

```cpp
void ScriptHost::fixedTick(float dt)
{
    std::vector<uint32_t> slots;
    slots.reserve(mImpl->instances.liveCount());
    mImpl->instances.forEach([&](uint32_t slot, ScriptInstance&) {
        slots.push_back(slot);
    });
    for (const uint32_t slot : slots) {
        ScriptInstance* inst = mImpl->instances.get(slot);
        // Not started yet means start() has not run: a fixed step that beat the
        // first frame must not call fixed_update on an uninitialised self.
        if (!inst || !inst->started) continue;
        mImpl->call(*inst, "fixed_update", dt);
    }
}

std::size_t ScriptHost::revive()
{
    std::size_t revived = 0;
    mImpl->instances.forEach([&](uint32_t, ScriptInstance& inst) {
        if (inst.quarantined) { inst.quarantined = false; ++revived; }
    });
    return revived;
}

bool ScriptHost::isQuarantined(entt::entity e, const std::string& path) const
{
    const auto& reg = mImpl->world.registry();
    if (!reg.valid(e) || !reg.all_of<ScriptState>(e)) return false;
    for (const uint32_t slot : reg.get<ScriptState>(e).instances) {
        const ScriptInstance* inst = mImpl->instances.get(slot);
        if (inst && inst->path == path) return inst->quarantined;
    }
    return false;
}
```

- [ ] **Step 6: Run both tests to verify they pass**

Run: `cmake --build build --target script_host_tests script_error_tests -j8 && ./build/script_host_tests && ./build/script_error_tests`
Expected: `ScriptHostTests: ok` and `ScriptErrorTests: ok`

- [ ] **Step 7: Commit**

```bash
git add engine/include/eng/script/ScriptHost.h engine/src/script/ScriptHost.cpp \
        engine/tests/ScriptHostTests.cpp engine/tests/ScriptErrorTests.cpp
git commit -m "feat(script): fixed_update, on_destroy, per-instance quarantine

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

## Task 6: `vec3` and the entity handle

**Files:**
- Create: `engine/src/script/bind/Bindings.h` (shared declarations)
- Create: `engine/src/script/bind/BindMath.cpp`
- Create: `engine/src/script/bind/BindEntity.cpp`
- Modify: `engine/src/script/ScriptHost.cpp` (call the binders, pass `self.entity`)
- Modify: `CMakeLists.txt` (two sources)
- Create: `engine/tests/ScriptBindingTests.cpp`

**Interfaces:**
- Consumes: Task 4's `Impl`.
- Produces:
  - `struct LuaEntity { eng::ecs::World* world = nullptr; entt::entity e = entt::null; }` in `Bindings.h`.
  - `void bindMath(sol::state&)`, `void bindEntity(sol::state&, eng::ecs::World&)`.
  - Lua: `vec3(x,y,z)` with `x/y/z`, `+ - *`, `:length()`, `:normalized()`, `:dot(o)`, `:cross(o)`.
  - Lua entity: `.valid`, `.name`, `.position`, `.rotation` (a `vec3` of Euler degrees), `.scale`, `.world_position` (read-only), `:set_parent(o)`.
  - `self.entity` is a `LuaEntity` for the owning entity.

- [ ] **Step 1: Write the failing test**

`engine/tests/ScriptBindingTests.cpp`:

```cpp
#include <eng/ecs/World.h>
#include <eng/ecs/components/Name.h>
#include <eng/ecs/components/Scripts.h>
#include <eng/ecs/components/Transform.h>
#include <eng/ecs/components/WorldTransform.h>
#include <eng/script/ScriptConfig.h>
#include <eng/script/ScriptHost.h>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

using namespace eng;
using namespace eng::ecs;
using namespace eng::script;

static void require(bool c, const char* m)
{
    if (!c) { std::cerr << "ScriptBindingTests: " << m << '\n'; std::exit(1); }
}

static std::string writeScript(const std::string& name, const std::string& body)
{
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "eng_script_binding_tests";
    std::filesystem::create_directories(dir);
    const std::filesystem::path file = dir / name;
    std::ofstream(file) << body;
    return file.string();
}

static entt::entity scripted(World& w, const std::string& name,
                             const std::string& path)
{
    const entt::entity e = w.create(name);
    w.registry().get_or_emplace<Scripts>(e).items.push_back({path, {}, true});
    return e;
}

int main()
{
    // --- vec3 arithmetic ---------------------------------------------------
    {
        World world;
        ScriptHost host(world, ScriptConfig{});
        const std::string path = writeScript(
            "vec.lua",
            "local M = {}\n"
            "function M:start()\n"
            "  local a = vec3(1, 2, 3)\n"
            "  local b = vec3(0, 1, 0)\n"
            "  sum_y = (a + b).y\n"
            "  diff_x = (a - b).x\n"
            "  scaled = (a * 2).z\n"
            "  len = vec3(3, 4, 0):length()\n"
            "  norm = vec3(0, 5, 0):normalized().y\n"
            "  dotted = a:dot(b)\n"
            "  crossed = vec3(1,0,0):cross(vec3(0,1,0)).z\n"
            "end\n"
            "return M\n");
        scripted(world, "v", path);
        host.tick(0.016f);
        require(host.luaGlobalNumber("sum_y") == 3.0, "vec3 addition");
        require(host.luaGlobalNumber("diff_x") == 1.0, "vec3 subtraction");
        require(host.luaGlobalNumber("scaled") == 6.0, "vec3 scalar multiply");
        require(host.luaGlobalNumber("len") == 5.0, "vec3 length");
        require(host.luaGlobalNumber("norm") == 1.0, "vec3 normalized");
        require(host.luaGlobalNumber("dotted") == 2.0, "vec3 dot");
        require(host.luaGlobalNumber("crossed") == 1.0, "vec3 cross");
    }

    // --- self.entity reads and writes the local Transform ------------------
    {
        World world;
        ScriptHost host(world, ScriptConfig{});
        const std::string path = writeScript(
            "move.lua",
            "local M = {}\n"
            "function M:start()\n"
            "  read_y = self.entity.position.y\n"
            "  self.entity.position = vec3(1, 5, 2)\n"
            "  who = self.entity.name\n"
            "  ok = self.entity.valid\n"
            "end\n"
            "return M\n");
        const entt::entity e = scripted(world, "mover", path);
        world.setLocalTransform(e, Transform{glm::vec3(0.0f, 7.0f, 0.0f)});
        host.tick(0.016f);

        require(host.luaGlobalNumber("read_y") == 7.0,
                "position reads the authored local transform");
        require(host.luaGlobalString("who") == "mover", "name reads Name");
        require(host.luaGlobalBool("ok"), "a live entity is valid");
        require(world.registry().get<Transform>(e).position.y == 5.0f,
                "writing position writes the local Transform");
        require(world.registry().all_of<Dirty>(e),
                "and marks the subtree dirty -- a write that skipped this "
                "would draw at the old pose until something else moved it");
    }

    // --- world_position is derived and read-only ---------------------------
    {
        World world;
        ScriptHost host(world, ScriptConfig{});
        const std::string path = writeScript(
            "wp.lua",
            "local M = {}\n"
            "function M:start()\n"
            "  wp = self.entity.world_position.y\n"
            "  local ok, err = pcall(function()\n"
            "    self.entity.world_position = vec3(0, 0, 0)\n"
            "  end)\n"
            "  refused = not ok\n"
            "end\n"
            "return M\n");
        const entt::entity parent = world.create("rig");
        world.setLocalTransform(parent, Transform{glm::vec3(0.0f, 10.0f, 0.0f)});
        const entt::entity e = scripted(world, "child", path);
        world.setParent(e, parent);
        world.setLocalTransform(e, Transform{glm::vec3(0.0f, 2.0f, 0.0f)});
        world.updateWorldTransforms();
        host.tick(0.016f);

        require(std::abs(host.luaGlobalNumber("wp") - 12.0) < 1e-4,
                "world_position is the composed pose, not the local one");
        require(host.luaGlobalBool("refused"),
                "assigning it is an error -- WorldTransform is derived");
    }

    // --- a destroyed entity reports invalid rather than crashing -----------
    {
        World world;
        ScriptHost host(world, ScriptConfig{});
        const std::string path = writeScript(
            "hold.lua",
            "local M = {}\n"
            "function M:start() held = self.entity end\n"
            "return M\n");
        const entt::entity e = scripted(world, "temp", path);
        host.tick(0.016f);
        world.destroyHierarchy(e);

        const std::string probe = writeScript(
            "probe.lua",
            "local M = {}\n"
            "function M:start()\n"
            "  stale_valid = held.valid\n"
            "  stale_name = held.name\n"
            "end\n"
            "return M\n");
        scripted(world, "prober", probe);
        host.tick(0.016f);
        require(!host.luaGlobalBool("stale_valid"),
                "a handle to a destroyed entity reports invalid");
        require(host.luaGlobalString("stale_name").empty(),
                "and reading through it yields a default, not a crash");
    }

    // --- set_parent composes transforms ------------------------------------
    {
        World world;
        ScriptHost host(world, ScriptConfig{});
        const std::string path = writeScript(
            "parent.lua",
            "local M = {}\n"
            "function M:start()\n"
            "  self.entity:set_parent(world.find('anchor'))\n"
            "end\n"
            "return M\n");
        const entt::entity anchor = world.create("anchor");
        world.setLocalTransform(anchor, Transform{glm::vec3(4.0f, 0.0f, 0.0f)});
        const entt::entity e = scripted(world, "hanger", path);
        host.tick(0.016f);
        world.updateWorldTransforms();
        require(world.registry().get<WorldTransform>(e).position.x == 4.0f,
                "set_parent puts the entity in its parent's frame");
    }

    std::cout << "ScriptBindingTests: ok\n";
    return 0;
}
```

Note: the `set_parent` case uses `world.find`, delivered in Task 8. Comment that block out with `#if 0` until Task 8, and re-enable it there — Task 8's step list says so explicitly.

- [ ] **Step 2: Add the test target and run it to verify it fails**

```cmake
  add_executable(script_binding_tests engine/tests/ScriptBindingTests.cpp)
  target_include_directories(script_binding_tests PRIVATE engine/include engine/src)
  target_link_libraries(script_binding_tests PRIVATE eng_script glm::glm EnTT::EnTT)
  add_test(NAME script_binding COMMAND script_binding_tests)
```

Run: `cmake -S . -B build && cmake --build build --target script_binding_tests -j8 && ./build/script_binding_tests`
Expected: compiles, FAILS at runtime — `vec3` is nil, so the script errors and no global is set.

- [ ] **Step 3: Write `Bindings.h`**

`engine/src/script/bind/Bindings.h`:

```cpp
#pragma once
#include <sol/sol.hpp>

#include <entt/entt.hpp>

namespace eng { class Input; class Physics; }
namespace eng::ecs { class World; }

namespace eng::script {

// An entity, as Lua holds it.
//
// A World pointer and an id, never a component pointer and never a bare id:
//   - a component pointer dies on the next emplace, because any emplace can
//     move a pool;
//   - a bare id cannot be validated, so a script holding one across a destroy
//     would read a recycled entity's components and silently act on the wrong
//     object.
// Every accessor re-checks registry().valid() before it touches anything.
struct LuaEntity {
    ecs::World* world = nullptr;
    entt::entity e = entt::null;

    bool valid() const;
};

void bindMath(sol::state& lua);
void bindEntity(sol::state& lua, ecs::World& world);

// Tasks 7, 8 and 9 each add their own declarations to this file. Do not
// pre-declare them here: bindEntity's return type and bindWorld's parameter
// list both change when those tasks land, and a stale declaration would be a
// link error blamed on the wrong file.

} // namespace eng::script
```

- [ ] **Step 4: Write `BindMath.cpp`**

`engine/src/script/bind/BindMath.cpp`:

```cpp
#include "script/bind/Bindings.h"

#include <glm/glm.hpp>
#include <glm/gtx/norm.hpp>

namespace eng::script {

void bindMath(sol::state& lua)
{
    // glm::vec3 directly rather than a wrapper: every binding that takes or
    // returns a position already speaks it, and a wrapper would mean a
    // conversion at each boundary for no expressive gain.
    lua.new_usertype<glm::vec3>(
        "vec3",
        sol::constructors<glm::vec3(), glm::vec3(float),
                          glm::vec3(float, float, float)>(),
        "x", &glm::vec3::x,
        "y", &glm::vec3::y,
        "z", &glm::vec3::z,
        sol::meta_function::addition,
        [](const glm::vec3& a, const glm::vec3& b) { return a + b; },
        sol::meta_function::subtraction,
        [](const glm::vec3& a, const glm::vec3& b) { return a - b; },
        sol::meta_function::multiplication,
        sol::overload([](const glm::vec3& a, float s) { return a * s; },
                      [](float s, const glm::vec3& a) { return a * s; },
                      [](const glm::vec3& a, const glm::vec3& b) { return a * b; }),
        sol::meta_function::unary_minus,
        [](const glm::vec3& a) { return -a; },
        sol::meta_function::equal_to,
        [](const glm::vec3& a, const glm::vec3& b) { return a == b; },
        sol::meta_function::to_string,
        [](const glm::vec3& a) {
            return "vec3(" + std::to_string(a.x) + ", " + std::to_string(a.y) +
                   ", " + std::to_string(a.z) + ")";
        },
        "length", [](const glm::vec3& a) { return glm::length(a); },
        // Guarded: normalising a zero vector in glm is a division by zero and
        // yields NaN, which then propagates silently into a transform and puts
        // the entity nowhere visible. Returning zero is the answer a script
        // author can actually debug.
        "normalized", [](const glm::vec3& a) {
            const float len2 = glm::length2(a);
            return len2 > 1e-12f ? a / std::sqrt(len2) : glm::vec3(0.0f);
        },
        "dot", [](const glm::vec3& a, const glm::vec3& b) { return glm::dot(a, b); },
        "cross", [](const glm::vec3& a, const glm::vec3& b) {
            return glm::cross(a, b);
        });
}

} // namespace eng::script
```

- [ ] **Step 5: Write `BindEntity.cpp`**

`engine/src/script/bind/BindEntity.cpp`:

```cpp
#include "script/bind/Bindings.h"

#include <eng/ecs/World.h>
#include <eng/ecs/components/Name.h>
#include <eng/ecs/components/Transform.h>
#include <eng/ecs/components/WorldTransform.h>

#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/euler_angles.hpp>

namespace eng::script {

bool LuaEntity::valid() const
{
    return world && world->registry().valid(e);
}

namespace {

// Reads the local Transform, or a default when the entity is gone. A stale
// handle yields zeros rather than throwing: a script probing an optional
// collaborator should get "nothing there", not an error it has to pcall.
const ecs::Transform& localOrDefault(const LuaEntity& h)
{
    static const ecs::Transform kDefault{};
    if (!h.valid()) return kDefault;
    const auto* t = h.world->registry().try_get<ecs::Transform>(h.e);
    return t ? *t : kDefault;
}

// Writes one field of the local Transform through World::setLocalTransform --
// never by mutating the component directly. That call is what marks the
// subtree Dirty, and a write that skipped it would draw at the old pose until
// something unrelated happened to move the entity.
template <typename Fn>
void editLocal(LuaEntity& h, Fn&& edit)
{
    if (!h.valid()) return;
    ecs::Transform t = localOrDefault(h);
    edit(t);
    h.world->setLocalTransform(h.e, t);
}

} // namespace

void bindEntity(sol::state& lua, ecs::World& world)
{
    (void)world; // handles carry their own World pointer

    lua.new_usertype<LuaEntity>(
        "Entity",
        // No constructor: entities come from world.spawn/find or self.entity.
        // Letting Lua fabricate one would produce a handle to an id nobody
        // allocated.
        sol::no_constructor,

        "valid", sol::property(&LuaEntity::valid),

        "name", sol::property([](const LuaEntity& h) -> std::string {
            if (!h.valid()) return {};
            const auto* n = h.world->registry().try_get<ecs::Name>(h.e);
            return n ? n->value : std::string{};
        }),

        "position", sol::property(
            [](const LuaEntity& h) { return localOrDefault(h).position; },
            [](LuaEntity& h, const glm::vec3& v) {
                editLocal(h, [&](ecs::Transform& t) { t.position = v; });
            }),

        "scale", sol::property(
            [](const LuaEntity& h) { return localOrDefault(h).scale; },
            [](LuaEntity& h, const glm::vec3& v) {
                editLocal(h, [&](ecs::Transform& t) { t.scale = v; });
            }),

        // Euler degrees, not a quaternion. A script author writing a door or a
        // patrol wants "turn 90 degrees about Y"; quaternions are the right
        // storage and the wrong authoring surface, and the conversion is two
        // glm calls.
        "rotation", sol::property(
            [](const LuaEntity& h) {
                return glm::degrees(glm::eulerAngles(localOrDefault(h).rotation));
            },
            [](LuaEntity& h, const glm::vec3& deg) {
                editLocal(h, [&](ecs::Transform& t) {
                    t.rotation = glm::quat(glm::radians(deg));
                });
            }),

        // Read-only: WorldTransform is derived by the hierarchy resolve, and a
        // write here would be silently overwritten on the next update. Refusing
        // loudly is the only honest option.
        "world_position", sol::property(
            [](const LuaEntity& h) {
                if (!h.valid()) return glm::vec3(0.0f);
                const auto* wt =
                    h.world->registry().try_get<ecs::WorldTransform>(h.e);
                return wt ? wt->position : localOrDefault(h).position;
            },
            [](LuaEntity&, const sol::object&) {
                throw sol::error("world_position is derived and read-only; "
                                 "set position instead");
            }),

        "set_parent", [](LuaEntity& h, const LuaEntity& parent) {
            if (!h.valid()) return;
            h.world->setParent(h.e, parent.valid() ? parent.e : entt::null);
        });
}

} // namespace eng::script
```

- [ ] **Step 6: Wire the binders into `ScriptHost::Impl`**

Add to `ScriptHost.cpp`'s includes:

```cpp
#include "script/bind/Bindings.h"
```

At the end of `Impl`'s constructor body, after `installTracebackHandler(lua)`:

```cpp
        bindMath(lua);
        bindEntity(lua, world);
```

And in `instantiateNew()`, immediately after the metatable is set:

```cpp
                self["entity"] = LuaEntity{&world, e};
```

- [ ] **Step 7: Add the two sources to the target**

```cmake
add_library(eng_script STATIC engine/src/script/ScriptHost.cpp
                              engine/src/script/ScriptError.cpp
                              engine/src/script/ScriptChunkCache.cpp
                              engine/src/script/ScriptInstance.cpp
                              engine/src/script/bind/BindMath.cpp
                              engine/src/script/bind/BindEntity.cpp)
```

- [ ] **Step 8: Run the test to verify it passes**

Run: `cmake -S . -B build && cmake --build build --target script_binding_tests -j8 && ./build/script_binding_tests`
Expected: `ScriptBindingTests: ok` (with the `set_parent` block still `#if 0`'d).

- [ ] **Step 9: Commit**

```bash
git add engine/src/script/bind engine/src/script/ScriptHost.cpp \
        engine/tests/ScriptBindingTests.cpp CMakeLists.txt
git commit -m "feat(script): vec3 and the entity handle

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

## Task 7: The reflection component proxy

**Files:**
- Create: `engine/src/script/bind/BindComponents.cpp`
- Modify: `engine/src/script/ScriptHost.cpp` (own a `ComponentRegistry`, call `bindComponents`)
- Modify: `engine/include/eng/script/ScriptHost.h` (accept a registry)
- Modify: `engine/tests/ScriptBindingTests.cpp` (append cases)
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `LuaEntity` (Task 6), `eng::ecs::ComponentRegistry` / `ComponentType` / `Field`.
- Produces:
  - `struct LuaComponent { ecs::World* world; entt::entity e; const ecs::ComponentType* type; }` — resolves `type->instance()` on **every** access.
  - Lua: `e:get(name) -> LuaComponent|nil`, `e:set(name, table)`, `e:has(name)`, `e:add(name)`, `e:remove(name)`.
  - `ScriptHost` constructor gains a third parameter: `const ecs::ComponentRegistry& registry`.

- [ ] **Step 1: Append the failing cases to `ScriptBindingTests.cpp`**

Add `#include <eng/ecs/ComponentRegistry.h>` and `#include <eng/ecs/components/Spin.h>` at the top, and a registry helper next to `writeScript`:

```cpp
static const ComponentRegistry& engineRegistry()
{
    static ComponentRegistry reg = [] {
        ComponentRegistry r;
        registerEngineComponents(r);
        return r;
    }();
    return reg;
}
```

The constructor gains a third parameter in this task, so **every existing
construction in all three test files written so far** must be updated:

- `engine/tests/ScriptHostTests.cpp` — every `ScriptHost host(world, ScriptConfig{});`
- `engine/tests/ScriptErrorTests.cpp` — every `ScriptHost host(world, eng::script::ScriptConfig{});`
- `engine/tests/ScriptBindingTests.cpp` — every `ScriptHost host(world, ScriptConfig{});`

each becoming `ScriptHost host(world, ScriptConfig{}, engineRegistry());` with
the `engineRegistry()` helper above copied into each file that needs it. Then
insert before the final `std::cout`:

```cpp
    // --- the proxy reads and writes any reflected component ----------------
    {
        World world;
        ScriptHost host(world, ScriptConfig{}, engineRegistry());
        const std::string path = writeScript(
            "reflect.lua",
            "local M = {}\n"
            "function M:start()\n"
            "  has_before = self.entity:has('Spin')\n"
            "  self.entity:add('Spin')\n"
            "  has_after = self.entity:has('Spin')\n"
            "  local s = self.entity:get('Spin')\n"
            "  read_default = s.degrees_per_second\n"
            "  s.degrees_per_second = 45\n"
            "  self.entity:set('Spin', { degrees_per_second = 180 })\n"
            "  missing = self.entity:get('Orbit')\n"
            "end\n"
            "return M\n");
        const entt::entity e = scripted(world, "spinner", path);
        host.tick(0.016f);

        require(!host.luaGlobalBool("has_before"), "has() is false before add");
        require(host.luaGlobalBool("has_after"), "add() emplaces the component");
        require(host.luaGlobalNumber("read_default") == 90.0,
                "a field reads the component's own default");
        require(world.registry().get<Spin>(e).degreesPerSecond == 180.0f,
                "set() with a table writes named fields");
        require(host.luaGlobalNil("missing"),
                "get() on an absent component is nil, not an error -- a script "
                "should be able to probe");
    }

    // --- remove, and the proxy after it ------------------------------------
    {
        World world;
        ScriptHost host(world, ScriptConfig{}, engineRegistry());
        const std::string path = writeScript(
            "remove.lua",
            "local M = {}\n"
            "function M:start()\n"
            "  self.entity:add('Spin')\n"
            "  held = self.entity:get('Spin')\n"
            "  self.entity:remove('Spin')\n"
            "  gone = self.entity:has('Spin')\n"
            "  after = held.degrees_per_second\n"
            "end\n"
            "return M\n");
        scripted(world, "r", path);
        host.tick(0.016f);
        require(!host.luaGlobalBool("gone"), "remove() removes it");
        require(host.luaGlobalNumber("after") == 0.0,
                "a proxy to a removed component reads a default rather than "
                "through a dangling pointer");
    }

    // --- THE invalidation case: a proxy held across a pool-moving emplace ---
    {
        World world;
        ScriptHost host(world, ScriptConfig{}, engineRegistry());
        const std::string path = writeScript(
            "invalidate.lua",
            "local M = {}\n"
            "function M:start()\n"
            "  self.entity:add('Spin')\n"
            "  local s = self.entity:get('Spin')\n"
            "  s.degrees_per_second = 10\n"
            "  -- Emplacing Spin on 512 other entities reallocates the pool.\n"
            "  -- A proxy caching a component pointer would now be dangling.\n"
            "  for i = 1, 512 do world.spawn('filler'):add('Spin') end\n"
            "  s.degrees_per_second = 20\n"
            "  readback = s.degrees_per_second\n"
            "end\n"
            "return M\n");
        const entt::entity e = scripted(world, "survivor", path);
        host.tick(0.016f);
        require(host.luaGlobalNumber("readback") == 20.0,
                "the proxy re-resolved after the pool moved");
        require(world.registry().get<Spin>(e).degreesPerSecond == 20.0f,
                "and it wrote the RIGHT entity's component, not whatever "
                "occupies the old address");
    }

    // --- an unknown component name is an error, not a silent no-op ---------
    {
        World world;
        ScriptHost host(world, ScriptConfig{}, engineRegistry());
        const std::string path = writeScript(
            "typo.lua",
            "local M = {}\n"
            "function M:start()\n"
            "  local ok = pcall(function() self.entity:add('Spinn') end)\n"
            "  refused = not ok\n"
            "end\n"
            "return M\n");
        scripted(world, "t", path);
        host.tick(0.016f);
        require(host.luaGlobalBool("refused"),
                "a misspelled component name fails loudly -- silently doing "
                "nothing is how a typo becomes an afternoon");
    }
```

This test uses `world.spawn` from Task 8. Guard the invalidation block with
`#if 0` and re-enable it in Task 8, which says so.

Also add a `luaGlobalNil` seam to `ScriptHost`:

```cpp
    bool luaGlobalNil(const char* name) const;
```

implemented as:

```cpp
bool ScriptHost::luaGlobalNil(const char* name) const
{
    const sol::object o = mImpl->lua[name];
    return !o.valid() || o.get_type() == sol::type::lua_nil;
}
```

- [ ] **Step 2: Run it to verify it fails**

Run: `cmake --build build --target script_binding_tests -j8`
Expected: FAIL to compile — `ScriptHost` takes two arguments, not three.

- [ ] **Step 3: Write `BindComponents.cpp`**

`engine/src/script/bind/BindComponents.cpp`:

```cpp
#include "script/bind/Bindings.h"

#include <eng/ecs/ComponentRegistry.h>
#include <eng/ecs/World.h>

#include <cstring>

namespace eng::script {
namespace {

// A component, as Lua holds it.
//
// {entity, type} and NEVER a component pointer. Every read and every write
// calls type->instance() again, because any emplace can move a pool and a
// cached pointer would then be writing into whatever moved in behind it. That
// is a documented invariant of this ECS, and ScriptBindingTests asserts it with
// 512 emplaces between two writes through the same proxy.
struct LuaComponent {
    ecs::World* world = nullptr;
    entt::entity e = entt::null;
    const ecs::ComponentType* type = nullptr;

    void* live() const
    {
        if (!world || !type || !type->instance || !world->registry().valid(e))
            return nullptr;
        return type->instance(world->registry(), e);
    }
};

const ecs::Field* findField(const ecs::ComponentType& t, const char* name)
{
    for (int i = 0; i < t.fieldCount; ++i)
        if (t.fields[i].name && std::strcmp(t.fields[i].name, name) == 0)
            return &t.fields[i];
    return nullptr;
}

sol::object readField(sol::state_view lua, void* base, const ecs::Field& f)
{
    const void* p = ecs::fieldPtr(base, f);
    switch (f.type) {
    case ecs::FieldType::Bool:
        return sol::make_object(lua, *static_cast<const bool*>(p));
    case ecs::FieldType::Int:
        return sol::make_object(lua, *static_cast<const int*>(p));
    case ecs::FieldType::Float:
        return sol::make_object(lua, *static_cast<const float*>(p));
    case ecs::FieldType::Vec3:
    case ecs::FieldType::Colour:
        return sol::make_object(lua, *static_cast<const glm::vec3*>(p));
    case ecs::FieldType::Quat:
        // Deliberately not exposed: a script author wants Euler degrees, which
        // is what the entity handle's `rotation` gives. A raw quaternion here
        // would be a footgun with no use case behind it.
        return sol::lua_nil;
    case ecs::FieldType::String:
        return sol::make_object(lua, *static_cast<const std::string*>(p));
    }
    return sol::lua_nil;
}

void writeField(void* base, const ecs::Field& f, const sol::object& v)
{
    void* p = ecs::fieldPtr(base, f);
    switch (f.type) {
    case ecs::FieldType::Bool:
        if (v.is<bool>()) *static_cast<bool*>(p) = v.as<bool>();
        break;
    case ecs::FieldType::Int:
        if (v.is<int>()) *static_cast<int*>(p) = v.as<int>();
        break;
    case ecs::FieldType::Float:
        if (v.is<float>()) *static_cast<float*>(p) = v.as<float>();
        break;
    case ecs::FieldType::Vec3:
    case ecs::FieldType::Colour:
        if (v.is<glm::vec3>()) *static_cast<glm::vec3*>(p) = v.as<glm::vec3>();
        break;
    case ecs::FieldType::Quat:
        break; // see readField
    case ecs::FieldType::String:
        if (v.is<std::string>())
            *static_cast<std::string*>(p) = v.as<std::string>();
        break;
    }
}

const ecs::ComponentRegistry* gRegistry = nullptr;

const ecs::ComponentType* findType(const std::string& name)
{
    if (!gRegistry) return nullptr;
    for (const ecs::ComponentType& t : gRegistry->types())
        if (t.name && name == t.name) return &t;
    return nullptr;
}

// Throws rather than returning nil: a misspelled component name is a bug in the
// script, and the only way the author finds out is if it says so.
const ecs::ComponentType& requireType(const std::string& name)
{
    const ecs::ComponentType* t = findType(name);
    if (!t)
        throw sol::error("no component named '" + name +
                         "' is registered -- check the spelling against the "
                         "add-component menu");
    return *t;
}

} // namespace

void bindComponents(sol::state& lua)
{
    lua.new_usertype<LuaComponent>(
        "Component", sol::no_constructor,
        sol::meta_function::index,
        [](const LuaComponent& c, const std::string& field, sol::this_state ts) {
            void* base = c.live();
            if (!base || !c.type) return sol::object(sol::lua_nil);
            const ecs::Field* f = findField(*c.type, field.c_str());
            if (!f) return sol::object(sol::lua_nil);
            return readField(sol::state_view(ts), base, *f);
        },
        sol::meta_function::new_index,
        [](const LuaComponent& c, const std::string& field,
           const sol::object& value) {
            void* base = c.live();
            if (!base || !c.type) return;
            if (const ecs::Field* f = findField(*c.type, field.c_str()))
                writeField(base, *f, value);
        });
}

// Registered onto the Entity usertype from BindEntity, which owns the type.
void bindComponentAccessors(sol::usertype<LuaEntity>& entity)
{
    entity["has"] = [](const LuaEntity& h, const std::string& name) {
        if (!h.valid()) return false;
        const ecs::ComponentType& t = requireType(name);
        return t.has && t.has(h.world->registry(), h.e);
    };
    entity["add"] = [](LuaEntity& h, const std::string& name) {
        if (!h.valid()) return;
        const ecs::ComponentType& t = requireType(name);
        if (t.addDefault) t.addDefault(h.world->registry(), h.e);
    };
    entity["remove"] = [](LuaEntity& h, const std::string& name) {
        if (!h.valid()) return;
        const ecs::ComponentType& t = requireType(name);
        if (t.remove && t.has && t.has(h.world->registry(), h.e))
            t.remove(h.world->registry(), h.e);
    };
    entity["get"] = [](const LuaEntity& h, const std::string& name,
                       sol::this_state ts) -> sol::object {
        if (!h.valid()) return sol::lua_nil;
        const ecs::ComponentType& t = requireType(name);
        if (!t.has || !t.has(h.world->registry(), h.e)) return sol::lua_nil;
        return sol::make_object(sol::state_view(ts),
                                LuaComponent{h.world, h.e, &t});
    };
    entity["set"] = [](LuaEntity& h, const std::string& name,
                       const sol::table& values) {
        if (!h.valid()) return;
        const ecs::ComponentType& t = requireType(name);
        if (t.has && !t.has(h.world->registry(), h.e) && t.addDefault)
            t.addDefault(h.world->registry(), h.e);
        // Resolved once here and not held: nothing between this line and the
        // loop's end can emplace, because the loop only writes fields.
        void* base = t.instance ? t.instance(h.world->registry(), h.e) : nullptr;
        if (!base) return;
        for (const auto& kv : values) {
            if (!kv.first.is<std::string>()) continue;
            const std::string key = kv.first.as<std::string>();
            if (const ecs::Field* f = findField(t, key.c_str()))
                writeField(base, *f, kv.second);
        }
    };
}

void setComponentRegistry(const ecs::ComponentRegistry* reg) { gRegistry = reg; }

} // namespace eng::script
```

Add the two new declarations to `Bindings.h`:

```cpp
void bindComponentAccessors(sol::usertype<LuaEntity>& entity);
void setComponentRegistry(const ecs::ComponentRegistry* reg);
```

- [ ] **Step 4: Have `BindEntity` return its usertype so the accessors attach**

Change `bindEntity`'s signature in `Bindings.h` and `BindEntity.cpp` to return
`sol::usertype<LuaEntity>`, `return` the result of `new_usertype`, and add
`#include <eng/ecs/ComponentRegistry.h>` where needed. In `BindEntity.cpp`:

```cpp
sol::usertype<LuaEntity> bindEntity(sol::state& lua, ecs::World& world)
{
    (void)world;
    sol::usertype<LuaEntity> entity = lua.new_usertype<LuaEntity>(
        "Entity", sol::no_constructor,
        /* ... every property from Task 6, unchanged ... */);
    return entity;
}
```

- [ ] **Step 5: Take a registry in `ScriptHost`**

In `engine/include/eng/script/ScriptHost.h`, forward-declare and extend the
constructor:

```cpp
namespace eng::ecs { class World; class ComponentRegistry; }
...
    // `registry` must outlive the host. It is what the reflection bindings walk,
    // so an application that registers its own components gets them in Lua for
    // free -- which is the whole reason the fallback exists.
    ScriptHost(ecs::World& world, const ScriptConfig& config,
               const ecs::ComponentRegistry& registry);
```

In `ScriptHost.cpp`, store it on `Impl`, and in the constructor body after
`bindMath`:

```cpp
        setComponentRegistry(&registry);
        sol::usertype<LuaEntity> entity = bindEntity(lua, world);
        bindComponents(lua);
        bindComponentAccessors(entity);
```

- [ ] **Step 6: Add the source, run the test**

```cmake
                              engine/src/script/bind/BindComponents.cpp
```

Run: `cmake -S . -B build && cmake --build build --target script_binding_tests -j8 && ./build/script_binding_tests`
Expected: `ScriptBindingTests: ok` (invalidation block still `#if 0`'d).

- [ ] **Step 7: Commit**

```bash
git add engine/src/script/bind engine/src/script/ScriptHost.cpp \
        engine/include/eng/script/ScriptHost.h \
        engine/tests/ScriptBindingTests.cpp CMakeLists.txt
git commit -m "feat(script): reflection-driven component access

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

## Task 8: `world`, `log`, `event`, and authored props

**Files:**
- Create: `engine/src/script/bind/BindWorld.cpp`
- Modify: `engine/src/script/ScriptHost.cpp` (props, deferred destroys, event routing)
- Modify: `engine/tests/ScriptBindingTests.cpp` (re-enable two blocks, append cases)
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `LuaEntity` (Task 6), `ScriptInstancePool` (Task 4).
- Produces:
  - Lua `world.spawn(name) -> Entity`, `world.find(name) -> Entity|nil`, `world.destroy(e)`, `world.destroy_hierarchy(e)`.
  - Lua `log.info/warn/error(msg)`.
  - Lua `event.send(entity, name, data)`, `event.broadcast(name, data)`; `Entity:send(name, data)`.
  - `self.props` populated before `start()`, with `Entity` props resolved to `LuaEntity` (or `nil` plus a warning).
  - `ScriptHost::Impl::pendingDestroy` — a queue flushed after each dispatch loop.

- [ ] **Step 1: Re-enable the two guarded blocks and append the failing cases**

In `engine/tests/ScriptBindingTests.cpp`, delete the `#if 0` / `#endif` around
the `set_parent` block (Task 6 Step 1) and the invalidation block (Task 7
Step 1). Then insert before the final `std::cout`:

```cpp
    // --- props arrive typed, before start ----------------------------------
    {
        World world;
        ScriptHost host(world, ScriptConfig{}, engineRegistry());
        const std::string path = writeScript(
            "props.lua",
            "local M = {}\n"
            "function M:start()\n"
            "  p_bool = self.props.open\n"
            "  p_num = self.props.speed\n"
            "  p_str = self.props.label\n"
            "  p_vec = self.props.tint.y\n"
            "  p_ent_name = self.props.target.name\n"
            "  p_missing = self.props.nope\n"
            "end\n"
            "return M\n");
        world.create("lever_a");
        const entt::entity e = world.create("door");
        auto& s = world.registry().get_or_emplace<Scripts>(e);
        ScriptRef ref;
        ref.path = path;
        ref.props.push_back({"open", ScriptProp::Type::Bool, true, 0.0f, {}, ""});
        ref.props.push_back({"speed", ScriptProp::Type::Number, false, 2.5f, {}, ""});
        ref.props.push_back({"label", ScriptProp::Type::String, false, 0.0f, {},
                             "north"});
        ref.props.push_back({"tint", ScriptProp::Type::Vec3, false, 0.0f,
                             glm::vec3(0.0f, 0.5f, 0.0f), ""});
        ref.props.push_back({"target", ScriptProp::Type::Entity, false, 0.0f, {},
                             "lever_a"});
        s.items.push_back(ref);

        host.tick(0.016f);
        require(host.luaGlobalBool("p_bool"), "bool prop");
        require(host.luaGlobalNumber("p_num") == 2.5, "number prop");
        require(host.luaGlobalString("p_str") == "north", "string prop");
        require(host.luaGlobalNumber("p_vec") == 0.5, "vec3 prop");
        require(host.luaGlobalString("p_ent_name") == "lever_a",
                "an Entity prop arrives already resolved to a handle");
        require(host.luaGlobalNil("p_missing"),
                "an unauthored prop is nil, not an error");
    }

    // --- an Entity prop naming nothing is nil, not a crash -----------------
    {
        World world;
        ScriptHost host(world, ScriptConfig{}, engineRegistry());
        const std::string path = writeScript(
            "dangling.lua",
            "local M = {}\n"
            "function M:start() dangled = (self.props.target == nil) end\n"
            "return M\n");
        const entt::entity e = world.create("orphan");
        auto& s = world.registry().get_or_emplace<Scripts>(e);
        ScriptRef ref;
        ref.path = path;
        ref.props.push_back({"target", ScriptProp::Type::Entity, false, 0.0f, {},
                             "no_such_entity"});
        s.items.push_back(ref);
        host.tick(0.016f);
        require(host.luaGlobalBool("dangled"),
                "an unresolvable Entity prop is nil -- a level may legitimately "
                "ship without the collaborator");
    }

    // --- world.spawn / find / destroy --------------------------------------
    {
        World world;
        ScriptHost host(world, ScriptConfig{}, engineRegistry());
        const std::string path = writeScript(
            "worldapi.lua",
            "local M = {}\n"
            "function M:start()\n"
            "  local made = world.spawn('spawned')\n"
            "  made.position = vec3(1, 1, 1)\n"
            "  spawned_ok = made.valid\n"
            "  found_ok = world.find('spawned').valid\n"
            "  missing_nil = (world.find('nothing_here') == nil)\n"
            "  world.destroy(made)\n"
            "  -- still valid THIS frame: destroys are queued so the loop we\n"
            "  -- are inside cannot have its views invalidated under it.\n"
            "  immediate = made.valid\n"
            "end\n"
            "return M\n");
        scripted(world, "spawner", path);
        host.tick(0.016f);
        require(host.luaGlobalBool("spawned_ok"), "spawn returns a live handle");
        require(host.luaGlobalBool("found_ok"), "find locates it by name");
        require(host.luaGlobalBool("missing_nil"), "find returns nil when absent");
        require(host.luaGlobalBool("immediate"),
                "destroy is deferred within the tick");
        require(!world.registry().valid(
                    world.registry().view<Name>().front()) ||
                    world.registry().view<Name>().size() >= 1,
                "the world is still consistent");

        host.tick(0.016f); // the queued destroy has been flushed by now
        const std::string probe = writeScript(
            "probe2.lua",
            "local M = {}\n"
            "function M:start() gone = (world.find('spawned') == nil) end\n"
            "return M\n");
        scripted(world, "probe", probe);
        host.tick(0.016f);
        require(host.luaGlobalBool("gone"), "and the destroy did happen");
    }

    // --- events reach another entity's script ------------------------------
    {
        World world;
        ScriptHost host(world, ScriptConfig{}, engineRegistry());
        const std::string listener = writeScript(
            "listener.lua",
            "local M = {}\n"
            "function M:on_event(name, data)\n"
            "  heard = name\n"
            "  payload = data and data.amount or 0\n"
            "end\n"
            "return M\n");
        const std::string sender = writeScript(
            "sender.lua",
            "local M = {}\n"
            "function M:start()\n"
            "  world.find('ear'):send('open', { amount = 7 })\n"
            "end\n"
            "return M\n");
        scripted(world, "ear", listener);
        scripted(world, "mouth", sender);
        host.tick(0.016f);
        require(host.luaGlobalString("heard") == "open",
                "send reaches the target's on_event");
        require(host.luaGlobalNumber("payload") == 7.0,
                "and carries its data table");
    }

    // --- e:script() reaches another entity's instance ----------------------
    {
        World world;
        ScriptHost host(world, ScriptConfig{}, engineRegistry());
        const std::string doorPath = writeScript(
            "door_api.lua",
            "local M = {}\n"
            "function M:start() self.open = false end\n"
            "function M:toggle() self.open = not self.open; door_open = self.open end\n"
            "return M\n");
        const std::string leverPath = writeScript(
            "lever_api.lua",
            "local M = {}\n"
            "function M:update(dt)\n"
            "  local d = world.find('door'):script('" + doorPath + "')\n"
            "  if d and not pulled then d:toggle(); pulled = true end\n"
            "end\n"
            "return M\n");
        scripted(world, "door", doorPath);
        scripted(world, "lever", leverPath);
        host.tick(0.016f);
        require(host.luaGlobalBool("door_open"),
                "a lever can call a method on the door's instance directly");
    }
```

- [ ] **Step 2: Run it to verify it fails**

Run: `cmake --build build --target script_binding_tests -j8 && ./build/script_binding_tests`
Expected: FAILS — `world` is nil.

- [ ] **Step 3: Write `BindWorld.cpp`**

`engine/src/script/bind/BindWorld.cpp`:

```cpp
#include "script/bind/Bindings.h"

#include <eng/Log.h>
#include <eng/ecs/World.h>
#include <eng/ecs/components/Name.h>

namespace eng::script {

entt::entity findByName(ecs::World& world, const std::string& name)
{
    // A linear scan of the Name view. Deliberately not an index: a name is not
    // unique and not immutable, so a cache would need invalidating on every
    // Name write, and the documented guidance is to resolve once in start()
    // and keep the handle on self -- which Entity props already do for you.
    for (const entt::entity e : world.registry().view<ecs::Name>())
        if (world.registry().get<ecs::Name>(e).value == name) return e;
    return entt::null;
}

void bindWorld(sol::state& lua, ecs::World& world, const WorldCallbacks& cb)
{
    sol::table w = lua.create_named_table("world");

    w["spawn"] = [&world](const std::string& name) {
        return LuaEntity{&world, world.create(name)};
    };

    w["find"] = [&world](const std::string& name,
                         sol::this_state ts) -> sol::object {
        const entt::entity e = findByName(world, name);
        if (e == entt::null) return sol::lua_nil;
        return sol::make_object(sol::state_view(ts), LuaEntity{&world, e});
    };

    // Queued, not immediate. A script calling destroy from inside update is
    // inside the host's dispatch loop, and destroying there would invalidate
    // the very views the loop is walking. The queue is flushed after dispatch.
    w["destroy"] = [cb](const LuaEntity& h) {
        if (h.valid()) cb.queueDestroy(h.e, false);
    };
    w["destroy_hierarchy"] = [cb](const LuaEntity& h) {
        if (h.valid()) cb.queueDestroy(h.e, true);
    };

    sol::table l = lua.create_named_table("log");
    // "Script:" is what puts these under the DebugConsole's `Script` category:
    // the console derives a category from a leading Word: prefix.
    l["info"] = [](const std::string& m) { log::info("Script: %s", m.c_str()); };
    l["warn"] = [](const std::string& m) { log::warn("Script: %s", m.c_str()); };
    l["error"] = [](const std::string& m) { log::error("Script: %s", m.c_str()); };

    sol::table ev = lua.create_named_table("event");
    ev["send"] = [cb](const LuaEntity& h, const std::string& name,
                      sol::object data) {
        if (h.valid()) cb.sendEvent(h.e, name, std::move(data));
    };
    ev["broadcast"] = [cb](const std::string& name, sol::object data) {
        cb.broadcastEvent(name, std::move(data));
    };
}

} // namespace eng::script
```

Add to `Bindings.h`:

```cpp
// What the world bindings need from the host, without BindWorld.cpp having to
// see ScriptHost::Impl. std::function rather than a pointer to Impl: this file
// is the boundary between "what Lua can ask for" and "how the host does it",
// and one of those should not be able to reach into the other.
struct WorldCallbacks {
    std::function<void(entt::entity, bool hierarchy)> queueDestroy;
    std::function<void(entt::entity, const std::string&, sol::object)> sendEvent;
    std::function<void(const std::string&, sol::object)> broadcastEvent;
};

entt::entity findByName(ecs::World& world, const std::string& name);
void bindWorld(sol::state& lua, ecs::World& world, const WorldCallbacks& cb);
```

and `#include <functional>` and `#include <string>`.

- [ ] **Step 4: Add props, deferred destroys and event routing to `ScriptHost.cpp`**

Add to `Impl`:

```cpp
    // Deferred structural changes. A script may call world.destroy from inside
    // update; performing it there would invalidate the loop's own views.
    struct PendingDestroy { entt::entity e; bool hierarchy; };
    std::vector<PendingDestroy> pendingDestroy;

    void flushDestroys()
    {
        // Swapped out first: an on_destroy handler may itself queue a destroy,
        // and appending to the vector we are iterating would invalidate it.
        std::vector<PendingDestroy> batch;
        batch.swap(pendingDestroy);
        for (const PendingDestroy& d : batch) {
            if (!world.registry().valid(d.e)) continue;
            if (d.hierarchy) world.destroyHierarchy(d.e);
            else world.destroy(d.e);
        }
    }

    // Builds self.props for one script instance.
    sol::table buildProps(entt::entity owner, const ecs::ScriptRef& ref)
    {
        sol::table props = lua.create_table();
        for (const ecs::ScriptProp& p : ref.props) {
            switch (p.type) {
            case ecs::ScriptProp::Type::Bool:   props[p.key] = p.b; break;
            case ecs::ScriptProp::Type::Number: props[p.key] = p.n; break;
            case ecs::ScriptProp::Type::String: props[p.key] = p.s; break;
            case ecs::ScriptProp::Type::Vec3:   props[p.key] = p.v; break;
            case ecs::ScriptProp::Type::Entity: {
                const entt::entity target = findByName(world, p.s);
                if (target == entt::null) {
                    // A warning, not an error: a level may legitimately ship
                    // without the collaborator, and the cooker already fails
                    // the build on a name absent from the authored scene.
                    log::warn("Script: %s on %s: prop '%s' names entity '%s', "
                              "which does not exist",
                              ref.path.c_str(), subject(owner).c_str(),
                              p.key.c_str(), p.s.c_str());
                    props[p.key] = sol::lua_nil;
                } else {
                    props[p.key] = LuaEntity{&world, target};
                }
                break;
            }
            }
        }
        return props;
    }

    void sendEvent(entt::entity target, const std::string& name, sol::object data)
    {
        auto& reg = world.registry();
        if (!reg.valid(target) || !reg.all_of<ScriptState>(target)) return;
        // Copied: on_event may attach a script, which reallocates the vector.
        const std::vector<uint32_t> slots = reg.get<ScriptState>(target).instances;
        for (const uint32_t slot : slots)
            if (ScriptInstance* inst = instances.get(slot))
                call(*inst, "on_event", name, data);
    }

    void broadcastEvent(const std::string& name, sol::object data)
    {
        std::vector<uint32_t> slots;
        slots.reserve(instances.liveCount());
        instances.forEach([&](uint32_t slot, ScriptInstance&) {
            slots.push_back(slot);
        });
        for (const uint32_t slot : slots)
            if (ScriptInstance* inst = instances.get(slot))
                call(*inst, "on_event", name, data);
    }

    // The first instance of `path` on `e`, or nil. Used by Entity:script().
    sol::object instanceTable(entt::entity e, const std::string& path)
    {
        auto& reg = world.registry();
        if (!reg.valid(e) || !reg.all_of<ScriptState>(e)) return sol::lua_nil;
        for (const uint32_t slot : reg.get<ScriptState>(e).instances) {
            const ScriptInstance* inst = instances.get(slot);
            if (inst && (path.empty() || inst->path == path))
                return sol::object(inst->self);
        }
        return sol::lua_nil;
    }
```

In `instantiateNew()`, after `self["entity"] = ...`:

```cpp
                self["props"] = buildProps(e, ref);
```

In the constructor body, after `bindComponentAccessors(entity)`:

```cpp
        WorldCallbacks cb;
        cb.queueDestroy = [this](entt::entity e, bool hierarchy) {
            pendingDestroy.push_back({e, hierarchy});
        };
        cb.sendEvent = [this](entt::entity e, const std::string& n,
                              sol::object d) { sendEvent(e, n, std::move(d)); };
        cb.broadcastEvent = [this](const std::string& n, sol::object d) {
            broadcastEvent(n, std::move(d));
        };
        bindWorld(lua, world, cb);

        entity["send"] = [this](const LuaEntity& h, const std::string& name,
                                sol::object data) {
            if (h.valid()) sendEvent(h.e, name, std::move(data));
        };
        entity["script"] = [this](const LuaEntity& h, sol::optional<std::string> p) {
            if (!h.valid()) return sol::object(sol::lua_nil);
            return instanceTable(h.e, p.value_or(std::string{}));
        };
```

Keep `entity` alive as an `Impl` member (`sol::usertype<LuaEntity> entityType;`)
so the lambdas above can be registered after `bindWorld`.

At the end of both `ScriptHost::tick` and `ScriptHost::fixedTick`:

```cpp
    mImpl->flushDestroys();
```

Add `#include <eng/Log.h>` to `ScriptHost.cpp`.

- [ ] **Step 5: Add the source and run the test**

```cmake
                              engine/src/script/bind/BindWorld.cpp
```

Run: `cmake -S . -B build && cmake --build build --target script_binding_tests -j8 && ./build/script_binding_tests`
Expected: `ScriptBindingTests: ok`, with **all** blocks enabled — including the pool-invalidation case, which is the point of the proxy design.

- [ ] **Step 6: Run every script test together**

Run: `ctest --test-dir build -R "script_" --output-on-failure`
Expected: `script_serialize`, `script_error`, `script_host`, `script_binding` all PASS.

- [ ] **Step 7: Commit**

```bash
git add engine/src/script engine/tests/ScriptBindingTests.cpp CMakeLists.txt
git commit -m "feat(script): world/log/event bindings and authored props

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

## Task 9: `input` and `physics.raycast`

**Files:**
- Create: `engine/src/script/bind/BindInput.cpp`, `engine/src/script/bind/BindPhysics.cpp`
- Modify: `engine/include/eng/script/ScriptHost.h` (`bindInput`, `bindPhysics`)
- Modify: `engine/src/script/ScriptHost.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `eng::Input` (`isDown`, `wasPressed`, `mouseDelta`), `eng::Physics` (`rayCast`, `RayHit`), `eng::ecs::BodyRef`.
- Produces:
  - `void ScriptHost::bindInput(Input&)`, `void ScriptHost::bindPhysics(Physics&)` — both optional; a host given neither still runs World-only scripts, which is what keeps the headless tests real.
  - Lua `input.down(action)`, `input.pressed(action)`, `input.mouse_delta() -> dx, dy`.
  - Lua `physics.raycast(from, dir, dist [, mask]) -> {entity, point, normal, fraction}|nil`.

- [ ] **Step 1: Write `BindInput.cpp`**

```cpp
#include "script/bind/Bindings.h"

#include <eng/Input.h>

namespace eng::script {

void bindInput(sol::state& lua, Input& input)
{
    sol::table t = lua.create_named_table("input");
    // Action names, not key codes. eng::Input is already action-based and the
    // bindings live in the TOML config, so a script never names a physical key
    // and rebinding does not touch a line of Lua.
    t["down"] = [&input](const std::string& action) {
        return input.isDown(action);
    };
    t["pressed"] = [&input](const std::string& action) {
        return input.wasPressed(action);
    };
    // Two numbers rather than a vec3: mouseDelta is a vec2, and widening it
    // would invent a z that means nothing.
    t["mouse_delta"] = [&input]() {
        const glm::vec2 d = input.mouseDelta();
        return std::make_tuple(d.x, d.y);
    };
}

} // namespace eng::script
```

- [ ] **Step 2: Write `BindPhysics.cpp`**

```cpp
#include "script/bind/Bindings.h"

#include <eng/Physics.h>
#include <eng/ecs/World.h>
#include <eng/ecs/components/BodyRef.h>

namespace eng::script {

entt::entity entityForBody(ecs::World& world, BodyHandle body)
{
    // A scan of the BodyRef view rather than a maintained map: BodyRef is
    // written by PhysicsSync, so a map here would need invalidating on every
    // body create and destroy, and this runs once per raycast and once per
    // contact -- not per body per frame.
    for (const entt::entity e : world.registry().view<ecs::BodyRef>())
        if (world.registry().get<ecs::BodyRef>(e).handle == body) return e;
    return entt::null;
}

void bindPhysics(sol::state& lua, Physics& physics, ecs::World& world)
{
    sol::table t = lua.create_named_table("physics");
    t["raycast"] = [&physics, &world](const glm::vec3& from, const glm::vec3& dir,
                                      float dist, sol::optional<uint32_t> mask,
                                      sol::this_state ts) -> sol::object {
        RayHit hit;
        if (!physics.rayCast(from, dir, dist, hit,
                             mask.value_or(uint32_t(kAllLayers))))
            return sol::lua_nil;

        sol::state_view lv(ts);
        sol::table out = lv.create_table();
        const entt::entity e = entityForBody(world, hit.body);
        // Nil rather than an invalid handle when the body has no entity: the
        // level's batched static geometry is one body per region and not an
        // entity at all, and a script must be able to tell "I hit the world"
        // from "I hit a thing".
        if (e != entt::null) out["entity"] = LuaEntity{&world, e};
        out["point"] = hit.point;
        out["normal"] = hit.normal;
        out["fraction"] = hit.fraction;
        return out;
    };
}

} // namespace eng::script
```

Add `entt::entity entityForBody(ecs::World&, BodyHandle);` to `Bindings.h`,
with `#include <eng/Handles.h>`.

- [ ] **Step 3: Expose the two optional bindings on `ScriptHost`**

In the public header:

```cpp
    // Optional subsystems. A host given neither still runs every script that
    // only touches the World -- which is exactly what makes the headless tests
    // real, and what lets a combat sim run scripted behaviour with no window.
    // Both references must outlive the host.
    void bindInput(Input& input);
    void bindPhysics(Physics& physics);
```

with `namespace eng { class Input; class Physics; }` forward declarations.

In `ScriptHost.cpp`:

```cpp
void ScriptHost::bindInput(Input& input)
{
    eng::script::bindInput(mImpl->lua, input);
}

void ScriptHost::bindPhysics(Physics& physics)
{
    eng::script::bindPhysics(mImpl->lua, physics, mImpl->world);
}
```

- [ ] **Step 4: Add both sources and build**

```cmake
                              engine/src/script/bind/BindInput.cpp
                              engine/src/script/bind/BindPhysics.cpp
```

Run: `cmake -S . -B build && cmake --build build --target eng_script -j8`
Expected: links clean.

- [ ] **Step 5: Verify the headless tests still pass without either binding**

Run: `ctest --test-dir build -R "script_" --output-on-failure`
Expected: all PASS. None of them calls `bindInput`/`bindPhysics`, which proves
the optionality is real rather than assumed.

- [ ] **Step 6: Commit**

```bash
git add engine/src/script engine/include/eng/script/ScriptHost.h CMakeLists.txt
git commit -m "feat(script): input and physics.raycast bindings

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

## Task 10: Multi-subscriber contact callbacks

**Files:**
- Modify: `engine/include/eng/Physics.h:221` (replace `setContactCallback`)
- Modify: `engine/src/physics/Physics.cpp:41,96,113,280,321-329,808`
- Modify: `game/src/main.cpp:394`
- Modify: `game/tests/PhysicsTests.cpp:252`

**Interfaces:**
- Consumes: nothing new.
- Produces:
  - `using ContactToken = uint32_t;`
  - `ContactToken Physics::addContactCallback(HitCallback)` — returns a non-zero token.
  - `void Physics::removeContactCallback(ContactToken)`.
  - `setContactCallback` is **deleted**, not deprecated. There are exactly two call sites; leaving a wrapper would preserve a single-slot API that the next caller would reach for and silently unregister the script bridge with.

- [ ] **Step 1: Write the failing test**

Append to `game/tests/PhysicsTests.cpp`, before its final success print:

```cpp
    // --- several subscribers each see every contact ------------------------
    {
        eng::Physics phys;
        phys.init();
        int first = 0, second = 0;
        const eng::ContactToken t1 =
            phys.addContactCallback([&](const eng::HitEvent&) { ++first; });
        const eng::ContactToken t2 =
            phys.addContactCallback([&](const eng::HitEvent&) { ++second; });
        require(t1 != 0 && t2 != 0 && t1 != t2,
                "every subscription gets its own non-zero token");

        // A dynamic box dropped onto a static floor: one contact, two callbacks.
        eng::BodyDesc floor;
        floor.shape = eng::ShapeKind::Box;
        floor.size = glm::vec3(10.0f, 0.5f, 10.0f);
        floor.position = glm::vec3(0.0f, -0.5f, 0.0f);
        floor.dynamic = false;
        phys.createBody(floor);

        eng::BodyDesc box;
        box.shape = eng::ShapeKind::Box;
        box.size = glm::vec3(0.5f);
        box.position = glm::vec3(0.0f, 2.0f, 0.0f);
        box.dynamic = true;
        phys.createBody(box);

        for (int i = 0; i < 120 && first == 0; ++i) phys.update(1.0f / 60.0f);
        require(first > 0, "the first subscriber saw the landing");
        require(second == first,
                "and the second saw exactly the same contacts -- a subscriber "
                "must not be able to starve another");

        const int before = first;
        phys.removeContactCallback(t1);
        for (int i = 0; i < 60; ++i) phys.update(1.0f / 60.0f);
        require(first == before, "a removed subscriber stops receiving");
        phys.shutdown();
    }
```

Match the surrounding file's existing `BodyDesc` field usage — read
`game/tests/PhysicsTests.cpp` first and copy how it builds bodies, rather than
assuming the field names above.

- [ ] **Step 2: Migrate the existing call site in the same test file**

Change line 252 from:

```cpp
    phys.setContactCallback([&](const eng::HitEvent&){ contacts++; });
```

to:

```cpp
    phys.addContactCallback([&](const eng::HitEvent&){ contacts++; });
```

- [ ] **Step 3: Run it to verify it fails**

Run: `cmake --build build --target physics_tests -j8`
Expected: FAIL to compile — no `addContactCallback`, no `ContactToken`.

- [ ] **Step 4: Change the public API**

In `engine/include/eng/Physics.h`, replace:

```cpp
    using HitCallback = std::function<void(const HitEvent&)>;
    void setContactCallback(HitCallback);
```

with:

```cpp
    using HitCallback = std::function<void(const HitEvent&)>;
    // A subscription token. Zero is never issued, so it is a usable "none".
    using ContactToken = uint32_t;

    // Every subscriber sees every contact, in subscription order.
    //
    // Multi-subscriber rather than one slot because the slot had two claimants
    // the moment anything else wanted contacts: the game's combat system and
    // the script host's trigger bridge. A setter would have let whichever ran
    // second silently unregister the first.
    ContactToken addContactCallback(HitCallback);
    void removeContactCallback(ContactToken);
```

`ContactToken` must be declared at namespace scope too, since the test names
`eng::ContactToken` — add above the class:

```cpp
using ContactToken = uint32_t;
```

and inside the class use `using ContactToken = eng::ContactToken;`.

- [ ] **Step 5: Change the implementation**

In `engine/src/physics/Physics.cpp`:

At line ~41, in `ContactSharedData`, replace the callback pointer with a gate
the job threads can read safely:

```cpp
    // Read from Jolt's job threads inside OnContactAdded. An atomic bool rather
    // than a pointer to the callback list: the listener only needs to know
    // whether collecting is worth it, and reading a std::vector concurrently
    // with a subscribe would be a race.
    const std::atomic<bool>*                  contactsWanted = nullptr;
```

At line ~96, in `Impl`, replace `Physics::HitCallback contactCb;` with:

```cpp
    struct ContactSub { ContactToken token; Physics::HitCallback fn; };
    std::vector<ContactSub> contactSubs;
    ContactToken nextContactToken = 1; // 0 is reserved for "none"
    std::atomic<bool> contactsWanted{false};
```

At line ~113, in `OnContactAdded`, replace the early-out:

```cpp
        if (!mShared->contactsWanted ||
            !mShared->contactsWanted->load(std::memory_order_relaxed))
            return;
```

At line ~280, replace the wiring:

```cpp
    mImpl->contactShared.contactsWanted = &mImpl->contactsWanted;
```

At lines ~321-329, replace the flush:

```cpp
    // Flush deferred contact events collected during Update (called from job
    // threads). Hold the lock just long enough to swap out the vector, then
    // call the subscribers from the main thread with no lock held.
    if (mImpl->contactsWanted.load(std::memory_order_relaxed)) {
        std::vector<HitEvent> batch;
        {
            std::lock_guard<std::mutex> lock(mImpl->contactMtx);
            batch.swap(mImpl->pendingContacts);
        }
        // Copied: a subscriber may subscribe or unsubscribe from inside its own
        // callback (a script that destroys the entity it just hit does exactly
        // that), and that would reallocate the vector under this loop.
        const std::vector<Impl::ContactSub> subs = mImpl->contactSubs;
        for (const HitEvent& ev : batch)
            for (const Impl::ContactSub& sub : subs)
                if (sub.fn) sub.fn(ev);
    }
```

At line ~808, replace the setter:

```cpp
ContactToken Physics::addContactCallback(HitCallback cb) {
    if (!cb) return 0;
    const ContactToken token = mImpl->nextContactToken++;
    mImpl->contactSubs.push_back({token, std::move(cb)});
    mImpl->contactsWanted.store(true, std::memory_order_relaxed);
    return token;
}

void Physics::removeContactCallback(ContactToken token) {
    auto& subs = mImpl->contactSubs;
    subs.erase(std::remove_if(subs.begin(), subs.end(),
                              [token](const Impl::ContactSub& s) {
                                  return s.token == token;
                              }),
               subs.end());
    mImpl->contactsWanted.store(!subs.empty(), std::memory_order_relaxed);
}
```

Add `#include <atomic>` and `#include <algorithm>` if not already present.

- [ ] **Step 6: Migrate the game's call site**

In `game/src/main.cpp:394`, change:

```cpp
    physics().setContactCallback(
        [this](const eng::HitEvent& e) { mCombat.onContact(*mCtx, e); });
```

to:

```cpp
    physics().addContactCallback(
        [this](const eng::HitEvent& e) { mCombat.onContact(*mCtx, e); });
```

The token is discarded deliberately: this subscription lives as long as the
physics world does.

- [ ] **Step 7: Run the tests to verify they pass**

Run: `cmake --build build --target physics_tests -j8 && ./build/physics_tests`
Expected: PASS.

- [ ] **Step 8: Verify the game still builds and behaves**

Run: `cmake --build build --target game -j8`
Expected: links clean.

Run: `make screenshot SHOT=/tmp/contacts.png FRAME=200`
Then **read `/tmp/contacts.png`**. Expected: the dungeon renders normally.
Combat contacts now route through the new list; a mistake here would show as
projectiles passing through walls, which the compiler cannot catch.

- [ ] **Step 9: Commit**

```bash
git add engine/include/eng/Physics.h engine/src/physics/Physics.cpp \
        game/src/main.cpp game/tests/PhysicsTests.cpp
git commit -m "refactor(physics): multi-subscriber contact callbacks

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

## Task 11: The contact bridge — `on_collision` and `on_trigger`

**Files:**
- Create: `engine/src/script/ScriptContactBridge.h`, `engine/src/script/ScriptContactBridge.cpp`
- Modify: `engine/include/eng/script/ScriptHost.h` (`drainContacts`)
- Modify: `engine/src/script/ScriptHost.cpp`
- Create: `engine/tests/ScriptContactTests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `Physics::addContactCallback` (Task 10), `entityForBody` (Task 9), `eng::ecs::Collider::sensor`.
- Produces:
  - `class ScriptContactBridge { ScriptContactBridge(Physics&, ecs::World&); ~ScriptContactBridge(); std::vector<Contact> drain(); }` where `struct Contact { entt::entity self, other; glm::vec3 point, normal; float impulse; bool sensor; }`.
  - `void ScriptHost::drainContacts()` — dispatches `on_trigger(other)` for sensor contacts, `on_collision(other, hit)` otherwise. Call **after** `Physics::update()`.

- [ ] **Step 1: Write the failing test**

`engine/tests/ScriptContactTests.cpp`:

```cpp
#include <eng/Physics.h>
#include <eng/ecs/ComponentRegistry.h>
#include <eng/ecs/World.h>
#include <eng/ecs/components/Collider.h>
#include <eng/ecs/components/RigidBody.h>
#include <eng/ecs/components/Scripts.h>
#include <eng/ecs/components/Transform.h>
#include <eng/script/ScriptConfig.h>
#include <eng/script/ScriptHost.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

using namespace eng;
using namespace eng::ecs;
using namespace eng::script;

static void require(bool c, const char* m)
{
    if (!c) { std::cerr << "ScriptContactTests: " << m << '\n'; std::exit(1); }
}

static std::string writeScript(const std::string& name, const std::string& body)
{
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "eng_script_contact_tests";
    std::filesystem::create_directories(dir);
    const std::filesystem::path file = dir / name;
    std::ofstream(file) << body;
    return file.string();
}

static const ComponentRegistry& engineRegistry()
{
    static ComponentRegistry reg = [] {
        ComponentRegistry r; registerEngineComponents(r); return r;
    }();
    return reg;
}

int main()
{
    // Read engine/tests/PhysicsSyncTests.cpp first and copy exactly how it
    // builds a Collider + RigidBody, and how it drives World::sync() against a
    // Physics world. The shapes below follow that file's conventions.

    // --- a solid contact calls on_collision --------------------------------
    {
        Physics physics;
        physics.init();
        World world;
        world.attachPhysics(physics);
        ScriptHost host(world, ScriptConfig{}, engineRegistry());
        host.bindPhysics(physics);

        const std::string path = writeScript(
            "hit.lua",
            "local M = {}\n"
            "function M:on_collision(other, hit)\n"
            "  hits = (hits or 0) + 1\n"
            "  other_name = other and other.name or ''\n"
            "  has_point = hit.point ~= nil\n"
            "end\n"
            "return M\n");

        const entt::entity floor = world.create("floor");
        world.setLocalTransform(floor, Transform{glm::vec3(0.0f, -0.5f, 0.0f)});
        world.registry().emplace<Collider>(
            floor, Collider{ShapeKind::Box, glm::vec3(10.0f, 0.5f, 10.0f)});

        const entt::entity box = world.create("box");
        world.setLocalTransform(box, Transform{glm::vec3(0.0f, 2.0f, 0.0f)});
        world.registry().emplace<Collider>(box,
                                           Collider{ShapeKind::Box, glm::vec3(0.5f)});
        world.registry().emplace<RigidBody>(box);
        world.registry().get_or_emplace<Scripts>(box).items.push_back(
            {path, {}, true});

        world.sync();
        host.tick(0.016f);
        for (int i = 0; i < 180 && host.luaGlobalNumber("hits") == 0.0; ++i) {
            host.fixedTick(1.0f / 60.0f);
            physics.update(1.0f / 60.0f);
            host.drainContacts();
            world.sync();
            host.tick(1.0f / 60.0f);
        }

        require(host.luaGlobalNumber("hits") > 0.0, "the box landed and reported");
        require(host.luaGlobalString("other_name") == "floor",
                "and named the entity it hit, not a body handle");
        require(host.luaGlobalBool("has_point"),
                "the hit carries its contact point");

        world.detachAll();
        physics.shutdown();
    }

    // --- a sensor collider calls on_trigger instead -------------------------
    {
        Physics physics;
        physics.init();
        World world;
        world.attachPhysics(physics);
        ScriptHost host(world, ScriptConfig{}, engineRegistry());
        host.bindPhysics(physics);

        const std::string path = writeScript(
            "volume.lua",
            "local M = {}\n"
            "function M:on_trigger(other) triggered = true end\n"
            "function M:on_collision(other, hit) collided = true end\n"
            "return M\n");

        const entt::entity volume = world.create("volume");
        world.setLocalTransform(volume, Transform{glm::vec3(0.0f, 0.0f, 0.0f)});
        Collider sensor{ShapeKind::Box, glm::vec3(2.0f)};
        sensor.sensor = true;
        world.registry().emplace<Collider>(volume, sensor);
        world.registry().get_or_emplace<Scripts>(volume).items.push_back(
            {path, {}, true});

        const entt::entity faller = world.create("faller");
        world.setLocalTransform(faller, Transform{glm::vec3(0.0f, 4.0f, 0.0f)});
        world.registry().emplace<Collider>(
            faller, Collider{ShapeKind::Box, glm::vec3(0.4f)});
        world.registry().emplace<RigidBody>(faller);

        world.sync();
        host.tick(0.016f);
        for (int i = 0; i < 240 && !host.luaGlobalBool("triggered"); ++i) {
            host.fixedTick(1.0f / 60.0f);
            physics.update(1.0f / 60.0f);
            host.drainContacts();
            world.sync();
            host.tick(1.0f / 60.0f);
        }

        require(host.luaGlobalBool("triggered"),
                "a sensor collider reports through on_trigger");
        require(!host.luaGlobalBool("collided"),
                "and NOT through on_collision -- the split is what lets a "
                "trigger volume and a wall use the same component");

        world.detachAll();
        physics.shutdown();
    }

    std::cout << "ScriptContactTests: ok\n";
    return 0;
}
```

- [ ] **Step 2: Add the test target and run it to verify it fails**

```cmake
  add_executable(script_contact_tests engine/tests/ScriptContactTests.cpp)
  target_include_directories(script_contact_tests PRIVATE engine/include engine/src)
  target_link_libraries(script_contact_tests PRIVATE eng_script glm::glm EnTT::EnTT)
  add_test(NAME script_contact COMMAND script_contact_tests)
```

Run: `cmake -S . -B build && cmake --build build --target script_contact_tests -j8`
Expected: FAIL — no `drainContacts`.

- [ ] **Step 3: Write `ScriptContactBridge.h`**

```cpp
#pragma once
#include <entt/entt.hpp>

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace eng { class Physics; }
namespace eng::ecs { class World; }

namespace eng::script {

// One contact, in the vocabulary a script speaks: entities, not body handles.
struct ScriptContact {
    entt::entity self = entt::null;
    entt::entity other = entt::null;
    glm::vec3 point{0.0f};
    glm::vec3 normal{0.0f};
    float impulse = 0.0f;
    bool sensor = false; // self's Collider is a sensor -> on_trigger
};

// Turns Physics contacts into script contacts.
//
// Queues rather than dispatching inline. Physics already flushes contacts on
// the main thread (it collects them on Jolt's job threads and drains them
// itself), so this is NOT for thread safety -- it is so Lua never runs inside
// Physics::update(), where a script destroying the entity it just hit would be
// mutating the registry mid-step.
class ScriptContactBridge {
public:
    ScriptContactBridge(Physics& physics, ecs::World& world);
    ~ScriptContactBridge();
    ScriptContactBridge(const ScriptContactBridge&) = delete;
    ScriptContactBridge& operator=(const ScriptContactBridge&) = delete;

    // Everything queued since the last call, cleared.
    std::vector<ScriptContact> drain();

private:
    Physics& mPhysics;
    ecs::World& mWorld;
    uint32_t mToken = 0;
    std::vector<ScriptContact> mQueue;
};

} // namespace eng::script
```

- [ ] **Step 4: Write `ScriptContactBridge.cpp`**

```cpp
#include "script/ScriptContactBridge.h"

#include "script/bind/Bindings.h"

#include <eng/Physics.h>
#include <eng/ecs/World.h>
#include <eng/ecs/components/Collider.h>

namespace eng::script {

ScriptContactBridge::ScriptContactBridge(Physics& physics, ecs::World& world)
    : mPhysics(physics), mWorld(world)
{
    mToken = mPhysics.addContactCallback([this](const HitEvent& ev) {
        const entt::entity self = entityForBody(mWorld, ev.self);
        const entt::entity other = entityForBody(mWorld, ev.other);
        // A contact where neither side is an entity is the level's batched
        // static geometry touching itself -- nothing scripted can be listening.
        if (self == entt::null && other == entt::null) return;

        auto push = [&](entt::entity a, entt::entity b) {
            if (a == entt::null) return;
            const auto* col = mWorld.registry().try_get<ecs::Collider>(a);
            mQueue.push_back({a, b, ev.point, ev.normal, ev.impulse,
                              col && col->sensor});
        };
        // Both directions: each side hears about the other, which is what lets
        // a trigger volume react to the player without the player's script
        // knowing the volume exists.
        push(self, other);
        push(other, self);
    });
}

ScriptContactBridge::~ScriptContactBridge()
{
    // Before the host's Lua state dies. A live subscription calling into a
    // freed queue is the failure this destructor exists to prevent.
    if (mToken) mPhysics.removeContactCallback(mToken);
}

std::vector<ScriptContact> ScriptContactBridge::drain()
{
    std::vector<ScriptContact> out;
    out.swap(mQueue);
    return out;
}

} // namespace eng::script
```

- [ ] **Step 5: Wire it into `ScriptHost`**

Public header:

```cpp
    // Dispatches on_collision / on_trigger for contacts collected by the last
    // physics step. Call immediately AFTER Physics::update(), before tick().
    // A no-op unless bindPhysics() was called.
    void drainContacts();
```

In `Impl`, add `std::unique_ptr<ScriptContactBridge> contacts;`. In
`ScriptHost::bindPhysics`, after the binding:

```cpp
    mImpl->contacts =
        std::make_unique<ScriptContactBridge>(physics, mImpl->world);
```

And:

```cpp
void ScriptHost::drainContacts()
{
    if (!mImpl->contacts) return;
    for (const ScriptContact& c : mImpl->contacts->drain()) {
        auto& reg = mImpl->world.registry();
        if (!reg.valid(c.self) || !reg.all_of<ScriptState>(c.self)) continue;
        // Copied: a handler may destroy an entity, which releases slots.
        const std::vector<uint32_t> slots = reg.get<ScriptState>(c.self).instances;
        for (const uint32_t slot : slots) {
            ScriptInstance* inst = mImpl->instances.get(slot);
            if (!inst || !inst->started) continue;
            const LuaEntity other{&mImpl->world, c.other};
            if (c.sensor) {
                mImpl->call(*inst, "on_trigger", other);
            } else {
                sol::table hit = mImpl->lua.create_table();
                hit["point"] = c.point;
                hit["normal"] = c.normal;
                hit["impulse"] = c.impulse;
                mImpl->call(*inst, "on_collision", other, hit);
            }
        }
    }
    mImpl->flushDestroys();
}
```

Add `#include "script/ScriptContactBridge.h"` to `ScriptHost.cpp`.

- [ ] **Step 6: Add the source and run the test**

```cmake
                              engine/src/script/ScriptContactBridge.cpp
```

Run: `cmake -S . -B build && cmake --build build --target script_contact_tests -j8 && ./build/script_contact_tests`
Expected: `ScriptContactTests: ok`

- [ ] **Step 7: Commit**

```bash
git add engine/src/script engine/include/eng/script/ScriptHost.h \
        engine/tests/ScriptContactTests.cpp CMakeLists.txt
git commit -m "feat(script): on_collision and on_trigger via a contact bridge

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

## Task 12: Hot reload and console commands

**Files:**
- Create: `engine/src/script/ScriptConsole.cpp`
- Modify: `engine/include/eng/script/ScriptHost.h` (`pollReload`, `reload`, `registerConsole`)
- Modify: `engine/src/script/ScriptHost.cpp`
- Create: `engine/tests/ScriptReloadTests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `eng::DirectoryWatcher`, `eng::DebugConsole`, `ScriptChunkCache::reload` (Task 3).
- Produces:
  - `void ScriptHost::pollReload()` — no-op unless `ScriptConfig::hotReload`.
  - `bool ScriptHost::reload(const std::string& path)` — reload one; empty path reloads all.
  - `void ScriptHost::registerConsole(DebugConsole&)` — `lua`, `script.list`, `script.reload`, `script.revive`.
  - Lua `on_reload()` called on each instance whose class was swapped.

- [ ] **Step 1: Write the failing test**

`engine/tests/ScriptReloadTests.cpp`:

```cpp
#include <eng/ecs/ComponentRegistry.h>
#include <eng/ecs/World.h>
#include <eng/ecs/components/Scripts.h>
#include <eng/script/ScriptConfig.h>
#include <eng/script/ScriptHost.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

using namespace eng;
using namespace eng::ecs;
using namespace eng::script;

static void require(bool c, const char* m)
{
    if (!c) { std::cerr << "ScriptReloadTests: " << m << '\n'; std::exit(1); }
}

static std::filesystem::path gDir;

static std::string write(const std::string& name, const std::string& body)
{
    std::filesystem::create_directories(gDir);
    const std::filesystem::path file = gDir / name;
    std::ofstream(file) << body;
    return file.string();
}

static const ComponentRegistry& engineRegistry()
{
    static ComponentRegistry reg = [] {
        ComponentRegistry r; registerEngineComponents(r); return r;
    }();
    return reg;
}

int main()
{
    gDir = std::filesystem::temp_directory_path() / "eng_script_reload_tests";
    std::filesystem::remove_all(gDir);

    // --- reload swaps behaviour and KEEPS instance state -------------------
    {
        World world;
        ScriptHost host(world, ScriptConfig{}, engineRegistry());
        const std::string path = write("v.lua",
                                       "local M = {}\n"
                                       "function M:start() self.n = 100 end\n"
                                       "function M:update(dt) result = self.n + 1 end\n"
                                       "return M\n");
        const entt::entity e = world.create("subject");
        world.registry().get_or_emplace<Scripts>(e).items.push_back(
            {path, {}, true});
        host.tick(0.016f);
        require(host.luaGlobalNumber("result") == 101.0, "original behaviour");

        write("v.lua", "local M = {}\n"
                       "function M:start() self.n = 999 end\n"
                       "function M:update(dt) result = self.n + 2 end\n"
                       "function M:on_reload() reloaded = true end\n"
                       "return M\n");
        require(host.reload(path), "reload succeeds");
        host.tick(0.016f);

        require(host.luaGlobalNumber("result") == 102.0,
                "the new update() runs -- 100 + 2, so the method swapped");
        require(host.luaGlobalNumber("result") != 1001.0,
                "and start() did NOT re-run: re-running it would wipe the "
                "state an iteration loop is trying to preserve");
        require(host.luaGlobalBool("reloaded"), "on_reload fired");
    }

    // --- a reload that does not parse keeps the previous class -------------
    {
        World world;
        ScriptHost host(world, ScriptConfig{}, engineRegistry());
        const std::string path = write("keep.lua",
                                       "local M = {}\n"
                                       "function M:update(dt) alive = true end\n"
                                       "return M\n");
        world.registry()
            .get_or_emplace<Scripts>(world.create("keeper"))
            .items.push_back({path, {}, true});
        host.tick(0.016f);
        require(host.luaGlobalBool("alive"), "loaded");

        write("keep.lua", "function M:update( end\n"); // half-typed save
        require(!host.reload(path), "a broken reload reports failure");

        host.luaSetGlobalNil("alive");
        host.tick(0.016f);
        require(host.luaGlobalBool("alive"),
                "and the running level keeps working on the previous version");
    }

    // --- a successful reload revives a quarantined instance ----------------
    {
        World world;
        ScriptHost host(world, ScriptConfig{}, engineRegistry());
        const std::string path = write("fixme.lua",
                                       "local M = {}\n"
                                       "function M:update(dt) error('broken') end\n"
                                       "return M\n");
        const entt::entity e = world.create("patient");
        world.registry().get_or_emplace<Scripts>(e).items.push_back(
            {path, {}, true});
        host.tick(0.016f);
        require(host.isQuarantined(e, path), "it errored and was quarantined");

        write("fixme.lua", "local M = {}\n"
                           "function M:update(dt) healed = true end\n"
                           "return M\n");
        require(host.reload(path), "the fixed version loads");
        require(!host.isQuarantined(e, path),
                "a successful reload revives it -- fixing the file is exactly "
                "how an author expects to un-break a script");
        host.tick(0.016f);
        require(host.luaGlobalBool("healed"), "and it runs again");
    }

    std::cout << "ScriptReloadTests: ok\n";
    return 0;
}
```

- [ ] **Step 2: Add the test target and run it to verify it fails**

```cmake
  add_executable(script_reload_tests engine/tests/ScriptReloadTests.cpp)
  target_include_directories(script_reload_tests PRIVATE engine/include engine/src)
  target_link_libraries(script_reload_tests PRIVATE eng_script glm::glm EnTT::EnTT)
  add_test(NAME script_reload COMMAND script_reload_tests)
```

Run: `cmake -S . -B build && cmake --build build --target script_reload_tests -j8`
Expected: FAIL — no `reload`, no `luaSetGlobalNil`.

- [ ] **Step 3: Implement reload on the host**

Public header:

```cpp
    // Re-runs `path`'s chunk and swaps the class table under every live
    // instance of it. Instance state (everything on self) survives, start() is
    // NOT re-run, on_reload() fires if defined, and any quarantined instance of
    // that script is revived.
    //
    // Returns false and changes nothing if the new source fails to load -- a
    // half-typed save must not kill a running level.
    // An empty path reloads every loaded script.
    bool reload(const std::string& path = {});

    // Polls the script root for changes and reloads what moved. A no-op unless
    // ScriptConfig::hotReload. Call once per frame.
    void pollReload();

    void luaSetGlobalNil(const char* name); // test seam
```

In `ScriptHost.cpp`:

```cpp
bool ScriptHost::reload(const std::string& path)
{
    std::vector<std::string> paths;
    if (path.empty()) {
        mImpl->instances.forEach([&](uint32_t, ScriptInstance& inst) {
            if (std::find(paths.begin(), paths.end(), inst.path) == paths.end())
                paths.push_back(inst.path);
        });
    } else {
        paths.push_back(path);
    }

    bool allOk = true;
    for (const std::string& p : paths) {
        if (!mImpl->chunks.reload(p)) { allOk = false; continue; }
        sol::table* cls = mImpl->chunks.classFor(p);
        if (!cls) { allOk = false; continue; }

        std::vector<uint32_t> slots;
        mImpl->instances.forEach([&](uint32_t slot, ScriptInstance& inst) {
            if (inst.path == p) slots.push_back(slot);
        });
        for (const uint32_t slot : slots) {
            ScriptInstance* inst = mImpl->instances.get(slot);
            if (!inst) continue;
            // Swap the metatable's __index, not `self`: everything the script
            // stored on self stays exactly where it was, and only the methods
            // change. That is the whole difference between a reload and a
            // restart.
            sol::table mt = inst->self[sol::metatable_key];
            mt["__index"] = *cls;
            // A fixed file is how an author expects to un-break a script.
            inst->quarantined = false;
            mImpl->call(*inst, "on_reload");
        }
    }
    return allOk;
}

void ScriptHost::luaSetGlobalNil(const char* name)
{
    mImpl->lua[name] = sol::lua_nil;
}
```

Add `#include <algorithm>`.

- [ ] **Step 4: Implement `pollReload` with the watcher**

In `Impl`, add:

```cpp
    // Only constructed when hot reload is on: a shipped build has nothing to
    // reload from, and polling a directory every frame for nothing is waste.
    std::optional<DirectoryWatcher> watcher;
```

In the constructor:

```cpp
        if (config.hotReload) {
            const std::filesystem::path dir = assets::resolve(config.root);
            if (!dir.empty())
                watcher.emplace(dir.string(), std::vector<std::string>{".lua"});
            else
                log::warn("Script: hot reload is on but '%s' does not resolve",
                          config.root.c_str());
        }
```

And:

```cpp
void ScriptHost::pollReload()
{
    if (!mImpl->watcher) return;
    for (const FileChange& change : mImpl->watcher->poll()) {
        if (change.kind == FileChange::Removed) continue;
        // Only reload what something is actually running: a watcher fires for
        // every file in the tree, and loading a script no entity carries would
        // execute its chunk for nothing.
        bool inUse = false;
        mImpl->instances.forEach([&](uint32_t, ScriptInstance& inst) {
            if (inst.path == change.path) inUse = true;
        });
        if (!inUse) continue;
        if (reload(change.path))
            log::info("Script: reloaded %s", change.path.c_str());
    }
}
```

Add includes: `<eng/DirectoryWatcher.h>`, `<eng/assets/AssetRoot.h>`,
`<filesystem>`, `<optional>`.

- [ ] **Step 5: Write `ScriptConsole.cpp`**

```cpp
#include <eng/script/ScriptHost.h>

#include <eng/debug/Console.h>

namespace eng::script {

void registerScriptCommands(DebugConsole& console, ScriptHost& host)
{
    console.registerCommand(
        "lua", "evaluate a Lua expression in the script state",
        [&host](const DebugConsole::Args& args) {
            if (args.size() < 2) return;
            std::string line;
            for (std::size_t i = 1; i < args.size(); ++i) {
                if (i > 1) line += ' ';
                line += args[i];
            }
            std::string out;
            host.executeConsole(line, out);
        });

    console.registerCommand(
        "script.list", "every live script instance",
        [&host](const DebugConsole::Args&) { host.listInstances(); });

    console.registerCommand(
        "script.reload", "reload one script by path, or all of them",
        [&host](const DebugConsole::Args& args) {
            host.reload(args.size() > 1 ? args[1] : std::string{});
        });

    console.registerCommand(
        "script.revive", "un-quarantine every errored instance",
        [&host](const DebugConsole::Args&) { host.revive(); });
}

} // namespace eng::script
```

Declare in the public header:

```cpp
    // Evaluates one line in the script state, through the same traceback
    // handler every callback uses. Returns false on failure; `out` carries the
    // result or the error either way.
    bool executeConsole(const std::string& line, std::string& out);
    // Logs every live instance: entity, path, and whether it is quarantined.
    void listInstances() const;
```

and, outside the class:

```cpp
// Registers `lua`, `script.list`, `script.reload` and `script.revive`.
//
// A free function taking both, not a method: gameplay must not depend on
// ImGui, and DebugConsole lives in the debug layer. This is the one file that
// knows about both, and a build without a console simply does not link it.
void registerScriptCommands(DebugConsole& console, ScriptHost& host);
```

with `namespace eng { class DebugConsole; }`.

Implement the two host methods in `ScriptHost.cpp`:

```cpp
bool ScriptHost::executeConsole(const std::string& line, std::string& out)
{
    // Wrapped in `return (...)` first so `lua 1+1` prints 2 rather than being
    // a syntax error. Falls back to running it as a statement, so `lua x = 5`
    // works too.
    sol::load_result chunk = mImpl->lua.load("return (" + line + ")", "@console");
    if (!chunk.valid()) chunk = mImpl->lua.load(line, "@console");
    if (!chunk.valid()) {
        const sol::error err = chunk;
        out = err.what();
        log::error("Script: console: %s", out.c_str());
        return false;
    }
    sol::protected_function fn = chunk;
    fn.error_handler = tracebackHandler(mImpl->lua);
    const sol::protected_function_result r = fn();
    if (!r.valid()) {
        const sol::error err = r;
        out = err.what();
        log::error("Script: console: %s", out.c_str());
        return false;
    }
    out = r.get_type() == sol::type::lua_nil
              ? std::string("nil")
              : mImpl->lua["tostring"](r.get<sol::object>()).get<std::string>();
    log::info("Script: %s", out.c_str());
    return true;
}

void ScriptHost::listInstances() const
{
    std::size_t n = 0;
    const_cast<ScriptInstancePool&>(mImpl->instances)
        .forEach([&](uint32_t slot, ScriptInstance& inst) {
            log::info("Script: [%u] %s on %s%s", slot, inst.path.c_str(),
                      mImpl->subject(inst.entity).c_str(),
                      inst.quarantined ? "  (QUARANTINED)" : "");
            ++n;
        });
    log::info("Script: %zu live instance(s)", n);
}
```

- [ ] **Step 6: Add the source, run the test**

```cmake
                              engine/src/script/ScriptConsole.cpp
```

Run: `cmake -S . -B build && cmake --build build --target script_reload_tests -j8 && ./build/script_reload_tests`
Expected: `ScriptReloadTests: ok`

- [ ] **Step 7: Run every script test**

Run: `ctest --test-dir build -R "script_" --output-on-failure`
Expected: all six PASS.

- [ ] **Step 8: Commit**

```bash
git add engine/src/script engine/include/eng/script/ScriptHost.h \
        engine/tests/ScriptReloadTests.cpp CMakeLists.txt
git commit -m "feat(script): hot reload and console commands

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

## Task 13: Game integration and sample scripts

**Files:**
- Modify: `game/src/MapPlay.cpp:~265` (`onPresent`) and its member block
- Modify: `game/src/LiveLevel.cpp:~282`
- Create: `assets/scripts/spin.lua`, `assets/scripts/lever.lua`, `assets/scripts/door.lua`, `assets/scripts/trigger_volume.lua`
- Modify: `assets/assets.toml` (directory-map comment)

**Interfaces:**
- Consumes: everything from Tasks 1–12.
- Produces: a running game in which a scripted entity in a scene moves.

- [ ] **Step 1: Document the scripts directory**

In `assets/assets.toml`, add to the directory-map comment block, in place:

```
#   scripts/      Lua entity and level scripts, read by explicit path
```

Do **not** add `scripts` to the `resources` list. That list is Ogre's flat
resource group; the manifest's own rule is that anything read by explicit path
stays out of it, and scripts are resolved through `eng::assets::resolve`.

- [ ] **Step 2: Write the sample scripts**

`assets/scripts/spin.lua`:

```lua
-- The minimum useful script: turn, forever.
--
-- Props:
--   degrees_per_second  number   how fast (default 90)
--   axis                vec3     which way (default Y)
--
-- The engine already has a Spin component that does this without any Lua. This
-- exists as the smallest complete example of the shape every script takes.
local Spin = {}

function Spin:start()
  self.speed = self.props.degrees_per_second or 90
  self.axis = self.props.axis or vec3(0, 1, 0)
  self.angle = 0
end

function Spin:update(dt)
  self.angle = (self.angle + self.speed * dt) % 360
  self.entity.rotation = self.axis * self.angle
end

return Spin
```

`assets/scripts/door.lua`:

```lua
-- A door that slides up when told to, and back down when told again.
--
-- Props:
--   height  number  how far it opens, in metres (default 3)
--   speed   number  how fast it moves, in metres per second (default 4)
--
-- It exposes toggle(), open() and close() as methods, which is what lets a
-- lever drive it directly via entity:script(). It also answers the "toggle"
-- event, so anything can drive it without holding a reference.
local Door = {}

function Door:start()
  self.closed_y = self.entity.position.y
  self.height = self.props.height or 3
  self.speed = self.props.speed or 4
  self.open_state = false
end

function Door:open()   self.open_state = true  end
function Door:close()  self.open_state = false end
function Door:toggle() self.open_state = not self.open_state end

function Door:on_event(name)
  if name == "toggle" then self:toggle() end
end

function Door:update(dt)
  local target = self.closed_y + (self.open_state and self.height or 0)
  local p = self.entity.position
  local delta = target - p.y
  local step = self.speed * dt
  -- Clamped rather than lerped: a lerp never arrives, and a door that is
  -- 0.001 short of open is a door that never stops writing its transform.
  if math.abs(delta) <= step then
    p.y = target
  else
    p.y = p.y + (delta > 0 and step or -step)
  end
  self.entity.position = p
end

return Door
```

`assets/scripts/lever.lua`:

```lua
-- Pulls a door when the player presses the interact action while nearby.
--
-- Props:
--   target  entity  the door to drive
--   range   number  how close the player must be, in metres (default 2.5)
--
-- Demonstrates the two ways to reach another entity: a direct method call
-- through :script(), and a message through :send(). The direct call is used
-- here because the lever genuinely knows it is driving a door.
local Lever = {}

function Lever:start()
  self.target = self.props.target
  self.range = self.props.range or 2.5
  if not self.target then
    log.warn("lever at " .. tostring(self.entity.name) .. " has no target")
  end
end

function Lever:update(dt)
  if not (self.target and self.target.valid) then return end
  if not input.pressed("interact") then return end

  local player = world.find("player")
  if not player then return end
  local to = player.world_position - self.entity.world_position
  if to:length() > self.range then return end

  local door = self.target:script("scripts/door.lua")
  if door then door:toggle() end
end

return Lever
```

`assets/scripts/trigger_volume.lua`:

```lua
-- Broadcasts an event the first time something enters this volume.
--
-- Put it on an entity whose Collider has `sensor` set -- that is what makes
-- the engine deliver on_trigger instead of on_collision.
--
-- Props:
--   event  string  what to broadcast (default "trigger_entered")
--   once   bool    fire only the first time (default true)
local Trigger = {}

function Trigger:start()
  self.event = self.props.event or "trigger_entered"
  self.once = self.props.once
  if self.once == nil then self.once = true end
  self.fired = false
end

function Trigger:on_trigger(other)
  if self.once and self.fired then return end
  self.fired = true
  event.broadcast(self.event, { who = other, from = self.entity })
end

return Trigger
```

- [ ] **Step 3: Wire the host into `MapPlay`**

In `game/src/MapPlay.cpp`, add to the includes:

```cpp
#include <eng/script/ScriptHost.h>
```

Add to the member block, after `eng::ecs::World mWorld;`:

```cpp
    // Constructed in onStartGame, after the world and physics exist. optional
    // rather than a member: a ScriptHost binds to a World for its whole life.
    std::optional<eng::script::ScriptHost> mScripts;
```

In `onStartGame`, after the world is built and physics is up:

```cpp
        eng::script::ScriptConfig scriptConfig;
        scriptConfig.hotReload = true; // development mode; see docs/scripting.md
        mScripts.emplace(mWorld, scriptConfig, sceneComponentRegistry());
        mScripts->bindInput(engine.input());
        mScripts->bindPhysics(physics());
        eng::script::registerScriptCommands(console(), *mScripts);
```

Use whatever accessor this file already has for the component registry it
passes to the map loader — read the file and reuse it rather than building a
second registry, or the two would drift.

Replace `onPresent` with:

```cpp
    void onPresent(const eng::FrameContext& f) override
    {
        eng::Renderer& r = f.engine.renderer();
        if (!mCinematic)
            mPlayer.update(f.engine.input(), r, f.dt);

        // The tick order, and the reason for it:
        //   fixed_update immediately before the step it is about to influence;
        //   contacts drained right after, so a script reacts to the collision
        //     in the same frame it happened;
        //   update with the rest of presentation;
        //   component systems and sync last, so everything a script wrote this
        //     frame is what gets pushed at the renderer.
        if (mScripts) mScripts->fixedTick(f.dt);
        physics().update(f.dt);
        if (mScripts) mScripts->drainContacts();
        if (mScripts) {
            mScripts->pollReload();
            mScripts->tick(f.dt);
        }

        eng::ecs::tickComponentSystems(mWorld, f.dt);
        mWorld.sync();
        if (!mCinematic)
            mPlayer.present(r);
    }
```

In `onStopGame`, **before** `mWorld.detachAll()`:

```cpp
        // Before the world and physics die: the host holds a contact
        // subscription on one and an on_destroy hook on the other.
        mScripts.reset();
```

- [ ] **Step 4: Wire the host into `LiveLevel` the same way**

`game/src/LiveLevel.cpp:282` already calls `tickComponentSystems(*world, dt)`.
Read the surrounding function and apply the identical ordering: `fixedTick`
before the physics step this file performs, `drainContacts` after it,
`pollReload` + `tick` before `tickComponentSystems`, and reset the host before
the world is torn down.

- [ ] **Step 5: Build and run the whole test suite**

Run: `cmake -S . -B build && cmake --build build --target game -j8 && ctest --test-dir build --output-on-failure`
Expected: everything PASSES, including `layering` and `assetlint`.

- [ ] **Step 6: Verify on screen — a scripted entity actually moves**

Add a `spin.lua` script to one entity in `assets/scenes/cozy_lair.scn` by hand
(the JSON shape is in Task 14; a `"scripts"` array on one entity), cook it, and
capture two frames far enough apart to show motion:

```sh
make cook SCENE=cozy_lair.scn
RAVEN_SCREENSHOT=/tmp/script_a.png RAVEN_SCREENSHOT_FRAME=60 timeout 120 ./build/game
RAVEN_SCREENSHOT=/tmp/script_b.png RAVEN_SCREENSHOT_FRAME=240 timeout 120 ./build/game
```

**Read both PNGs.** Expected: the scripted entity is at a visibly different
orientation between them, and nothing else in the scene has changed. A change
that compiles is not a change that works — three real bugs in this repository
were invisible to the compiler and obvious in a screenshot.

If the screenshot is black, the window was unfocused or offscreen; confirm
against a known-good binary before chasing it.

- [ ] **Step 7: Commit**

```bash
git add game/src/MapPlay.cpp game/src/LiveLevel.cpp assets/scripts assets/assets.toml
git commit -m "feat(game): run scripts in MapPlay and LiveLevel

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

## Task 14: Editor authoring — inspector, scene I/O, schema

**Files:**
- Modify: `editor/src/ui/ComponentInspector.cpp` (bespoke `Scripts` block)
- Modify: `editor/src/content/SceneSource.cpp` (read `"scripts"`)
- Modify: `editor/src/content/SceneWriter.cpp` (write `"scripts"`)
- Modify: `editor/include/editor/scene/EntityComponents.h`, `editor/src/scene/EntityComponents.cpp` (carry the authored value)
- Modify: `assets/schemas/scene.schema.json`

**Interfaces:**
- Consumes: `eng::ecs::Scripts` (Task 2).
- Produces: `.scn` round-trips scripts and props; the inspector edits them.

- [ ] **Step 1: Read the three files first**

Run:
```sh
sed -n '1,80p' editor/include/editor/scene/EntityComponents.h
grep -n "portal_params\|first_person\|viewmodel" editor/src/content/SceneSource.cpp | head
grep -n "portal_params\|first_person\|viewmodel" editor/src/content/SceneWriter.cpp | head
```

Pick the most recently added component (the one with the fewest special cases)
and follow its path through all three files exactly. Do not invent a fourth
convention.

- [ ] **Step 2: Extend the JSON schema**

In `assets/schemas/scene.schema.json`, add to the entity object's `properties`:

```json
"scripts": {
  "type": "array",
  "description": "Lua scripts attached to this entity, in run order.",
  "items": {
    "type": "object",
    "required": ["path"],
    "additionalProperties": false,
    "properties": {
      "path": {
        "type": "string",
        "description": "Logical asset path, e.g. scripts/door.lua",
        "pattern": "\\.lua$"
      },
      "enabled": { "type": "boolean", "default": true },
      "props": {
        "type": "object",
        "description": "Per-instance authored values, reachable as self.props.",
        "additionalProperties": {
          "oneOf": [
            { "type": "boolean" },
            { "type": "number" },
            { "type": "string" },
            {
              "type": "array",
              "items": { "type": "number" },
              "minItems": 3, "maxItems": 3,
              "description": "a vec3"
            },
            {
              "type": "object",
              "required": ["entity"],
              "additionalProperties": false,
              "properties": { "entity": { "type": "string" } },
              "description": "a reference to another entity, by name"
            }
          ]
        }
      }
    }
  }
}
```

- [ ] **Step 3: Read the `"scripts"` array in `SceneSource.cpp`**

Add next to the other component parsers:

```cpp
// Scripts cannot go through parseFields: that helper is driven by a FieldSpan,
// which describes a fixed layout, and this is a variable-length list of
// heterogeneous values. So it is parsed by hand, and the JSON schema is what
// keeps the accepted shape documented.
bool parseScripts(const Json& entity, eng::ecs::Scripts& out,
                  const std::string& location, std::string& error)
{
    if (!entity.contains("scripts")) return true;
    const Json& list = entity["scripts"];
    if (!list.is_array()) {
        error = location + "/scripts must be an array";
        return false;
    }
    for (std::size_t i = 0; i < list.size(); ++i) {
        const Json& node = list[i];
        const std::string where = location + "/scripts/" + std::to_string(i);
        if (!node.is_object() || !node.contains("path") ||
            !node["path"].is_string()) {
            error = where + " needs a string 'path'";
            return false;
        }
        eng::ecs::ScriptRef ref;
        ref.path = node["path"].get<std::string>();
        ref.enabled = node.value("enabled", true);

        if (node.contains("props")) {
            const Json& props = node["props"];
            if (!props.is_object()) {
                error = where + "/props must be an object";
                return false;
            }
            for (auto it = props.begin(); it != props.end(); ++it) {
                eng::ecs::ScriptProp p;
                p.key = it.key();
                const Json& v = it.value();
                if (v.is_boolean()) {
                    p.type = eng::ecs::ScriptProp::Type::Bool;
                    p.b = v.get<bool>();
                } else if (v.is_number()) {
                    p.type = eng::ecs::ScriptProp::Type::Number;
                    p.n = v.get<float>();
                } else if (v.is_string()) {
                    p.type = eng::ecs::ScriptProp::Type::String;
                    p.s = v.get<std::string>();
                } else if (v.is_array()) {
                    if (!readVec3(v, p.v)) {
                        error = where + "/props/" + it.key() +
                                " must be three finite numbers";
                        return false;
                    }
                    p.type = eng::ecs::ScriptProp::Type::Vec3;
                } else if (v.is_object() && v.contains("entity") &&
                           v["entity"].is_string()) {
                    p.type = eng::ecs::ScriptProp::Type::Entity;
                    p.s = v["entity"].get<std::string>();
                } else {
                    error = where + "/props/" + it.key() +
                            " must be a boolean, number, string, 3 numbers, or "
                            "{ \"entity\": \"name\" }";
                    return false;
                }
                ref.props.push_back(std::move(p));
            }
        }
        out.items.push_back(std::move(ref));
    }
    return true;
}
```

Call it wherever the other per-entity component parsers are called, and store
the result on the editor's authored entity struct (`EntityComponents.h`) the
way the neighbouring components do.

- [ ] **Step 4: Write the `"scripts"` array in `SceneWriter.cpp`**

```cpp
// Written in full rather than as a diff against defaults, unlike reflectedNode:
// there is no meaningful "default script list", and a half-written entry would
// be a scene that silently loses a prop.
Json scriptsNode(const eng::ecs::Scripts& scripts)
{
    Json list = Json::array();
    for (const eng::ecs::ScriptRef& ref : scripts.items) {
        Json node = Json::object();
        node["path"] = ref.path;
        if (!ref.enabled) node["enabled"] = false; // true is the default
        if (!ref.props.empty()) {
            Json props = Json::object();
            for (const eng::ecs::ScriptProp& p : ref.props) {
                switch (p.type) {
                case eng::ecs::ScriptProp::Type::Bool:   props[p.key] = p.b; break;
                case eng::ecs::ScriptProp::Type::Number:
                    props[p.key] = canonical(p.n);
                    break;
                case eng::ecs::ScriptProp::Type::String: props[p.key] = p.s; break;
                case eng::ecs::ScriptProp::Type::Vec3:
                    props[p.key] = vec3(p.v);
                    break;
                case eng::ecs::ScriptProp::Type::Entity: {
                    Json ref_node = Json::object();
                    ref_node["entity"] = p.s;
                    props[p.key] = ref_node;
                    break;
                }
                }
            }
            node["props"] = std::move(props);
        }
        list.push_back(std::move(node));
    }
    return list;
}
```

Emit it on the entity only when the list is non-empty, next to where the other
components are emitted.

- [ ] **Step 5: Add the inspector block in `ComponentInspector.cpp`**

Next to the generic field loop (~line 1089), add a bespoke case for `Scripts`:

```cpp
// Scripts get a hand-written block for the same reason they hand-write their
// serialiser: the generic field walker renders a fixed set of typed rows, and
// this is a reorderable list whose rows each carry a variable table.
void drawScriptsComponent(eng::ecs::Scripts& scripts, const EntityNames& names,
                          bool& changed)
{
    int removeAt = -1, moveFrom = -1, moveTo = -1;
    for (int i = 0; i < int(scripts.items.size()); ++i) {
        eng::ecs::ScriptRef& ref = scripts.items[i];
        ImGui::PushID(i);

        changed |= ImGui::Checkbox("##enabled", &ref.enabled);
        ImGui::SameLine();
        // The order is the run order, so it is worth being able to change.
        ImGui::TextDisabled("%d.", i + 1);
        ImGui::SameLine();
        changed |= drawScriptPathPicker("##path", ref.path);

        if (ImGui::ArrowButton("##up", ImGuiDir_Up) && i > 0) {
            moveFrom = i; moveTo = i - 1;
        }
        ImGui::SameLine();
        if (ImGui::ArrowButton("##down", ImGuiDir_Down) &&
            i + 1 < int(scripts.items.size())) {
            moveFrom = i; moveTo = i + 1;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("remove")) removeAt = i;

        changed |= drawScriptProps(ref.props, names);
        ImGui::PopID();
        ImGui::Separator();
    }

    // Applied after the loop: mutating the vector inside it would invalidate
    // the reference the current row is still holding.
    if (removeAt >= 0) {
        scripts.items.erase(scripts.items.begin() + removeAt);
        changed = true;
    } else if (moveFrom >= 0) {
        std::swap(scripts.items[moveFrom], scripts.items[moveTo]);
        changed = true;
    }

    if (ImGui::Button("add script")) {
        scripts.items.push_back({});
        changed = true;
    }
}
```

And the two helpers it calls:

```cpp
// A combo over the .lua files under the resolved scripts root, plus a free-text
// field. The free text matters: a script may legitimately not exist on disk yet
// when the entity is being laid out, and a picker that refused to accept that
// would force the author to write the file first.
bool drawScriptPathPicker(const char* id, std::string& path)
{
    bool changed = false;
    char buffer[256];
    std::snprintf(buffer, sizeof(buffer), "%s", path.c_str());
    if (ImGui::InputText(id, buffer, sizeof(buffer))) {
        path = buffer;
        changed = true;
    }
    ImGui::SameLine();
    if (ImGui::BeginCombo("##pick", "...", ImGuiComboFlags_NoPreview)) {
        for (const std::string& candidate : scriptFilesUnderRoot())
            if (ImGui::Selectable(candidate.c_str(), candidate == path)) {
                path = candidate;
                changed = true;
            }
        ImGui::EndCombo();
    }
    return changed;
}

// One row per prop: key, type, value. The type combo is what makes the five
// prop types authorable without a schema per script -- the editor cannot know
// what a given .lua expects, so the author declares it here.
bool drawScriptProps(std::vector<eng::ecs::ScriptProp>& props,
                     const EntityNames& names)
{
    using Type = eng::ecs::ScriptProp::Type;
    static const char* kTypeNames[] = {"bool", "number", "string", "vec3",
                                       "entity"};
    bool changed = false;
    int removeAt = -1;

    for (int i = 0; i < int(props.size()); ++i) {
        eng::ecs::ScriptProp& p = props[i];
        ImGui::PushID(i);

        char key[128];
        std::snprintf(key, sizeof(key), "%s", p.key.c_str());
        ImGui::SetNextItemWidth(120.0f);
        if (ImGui::InputText("##key", key, sizeof(key))) {
            p.key = key;
            changed = true;
        }

        ImGui::SameLine();
        ImGui::SetNextItemWidth(80.0f);
        int typeIndex = int(p.type);
        if (ImGui::Combo("##type", &typeIndex, kTypeNames,
                         IM_ARRAYSIZE(kTypeNames))) {
            p.type = Type(typeIndex);
            changed = true;
        }

        ImGui::SameLine();
        ImGui::SetNextItemWidth(160.0f);
        switch (p.type) {
        case Type::Bool:
            changed |= ImGui::Checkbox("##v", &p.b);
            break;
        case Type::Number:
            changed |= ImGui::DragFloat("##v", &p.n, 0.01f);
            break;
        case Type::Vec3:
            changed |= ImGui::DragFloat3("##v", &p.v.x, 0.01f);
            break;
        case Type::String: {
            char value[256];
            std::snprintf(value, sizeof(value), "%s", p.s.c_str());
            if (ImGui::InputText("##v", value, sizeof(value))) {
                p.s = value;
                changed = true;
            }
            break;
        }
        case Type::Entity:
            // A combo over the scene's entity names rather than free text: an
            // Entity prop that names nothing is a warning at runtime and a cook
            // failure, so it is worth making the wrong value hard to type.
            if (ImGui::BeginCombo("##v", p.s.c_str())) {
                for (const std::string& name : names.all)
                    if (ImGui::Selectable(name.c_str(), name == p.s)) {
                        p.s = name;
                        changed = true;
                    }
                ImGui::EndCombo();
            }
            break;
        }

        ImGui::SameLine();
        if (ImGui::SmallButton("x")) removeAt = i;
        ImGui::PopID();
    }

    // After the loop, for the same reason as the script list above: erasing
    // inside it would invalidate the reference the current row still holds.
    if (removeAt >= 0) {
        props.erase(props.begin() + removeAt);
        changed = true;
    }
    if (ImGui::SmallButton("add prop")) {
        props.push_back({});
        changed = true;
    }
    return changed;
}
```

`scriptFilesUnderRoot()` lists `*.lua` under `eng::assets::resolve("scripts")`,
cached for the session. `EntityNames` is whatever this file already uses to
offer entity references — read it and reuse it rather than adding a second one.

- [ ] **Step 6: Verify the round trip**

Run:
```sh
cmake --build build --target scene_editor -j8
make cook SCENE=cozy_lair.scn VALIDATE=1
```
Expected: validation passes with the hand-added `"scripts"` array from Task 13.

Then open the editor, add a script to an entity through the inspector, save,
and `git diff assets/scenes/cozy_lair.scn`. Expected: only the `"scripts"` array
appears — no reordering or reformatting of anything else, which would mean the
writer is losing or re-emitting unrelated data.

- [ ] **Step 7: Commit**

```bash
git add editor/ assets/schemas/scene.schema.json
git commit -m "feat(editor): author scripts and props in the inspector

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

## Task 15: Cook validation, assetlint, and documentation

**Files:**
- Modify: `editor/src/content/SceneValidate.cpp`
- Modify: `tools/assetlint.py`
- Create: `docs/scripting.md`
- Modify: `docs/ecs.md`, `ARCHITECTURE.md`, `docs/scene-editor-entities.md`

**Interfaces:**
- Consumes: everything.
- Produces: a dangling script path or a syntax error fails the build; the architecture is documented.

- [ ] **Step 1: Add cook-time validation**

In `editor/src/content/SceneValidate.cpp`, following how it validates mesh and
material references:

```cpp
// Three checks, in the order they can fail:
//   1. the path resolves -- a typo must fail the build, not produce an entity
//      that silently does nothing at runtime;
//   2. the file PARSES -- luaL_loadbuffer only, never executed. Validating a
//      script must not require a world, a renderer or a physics system;
//   3. every Entity prop names an entity this scene actually contains.
```

For (2), link the editor against `eng_script` and use Lua directly:

```cpp
bool luaParses(const std::string& source, const std::string& chunkName,
               std::string& error)
{
    lua_State* L = luaL_newstate();
    // No libraries opened: the chunk is compiled, never run, so it needs none,
    // and a cooker that can open io is a cooker that can be made to write.
    const int status = luaL_loadbuffer(L, source.data(), source.size(),
                                       ("@" + chunkName).c_str());
    if (status != LUA_OK) error = lua_tostring(L, -1);
    lua_close(L);
    return status == LUA_OK;
}
```

For (3), collect every entity `name`/`id` in the scene first, then check each
`Entity` prop against that set, reporting the entity, script and prop key.

Link the editor target against `eng_script` in `CMakeLists.txt` if it does not
already inherit it through `eng`.

- [ ] **Step 2: Add the assetlint pass**

In `tools/assetlint.py`, extend `collect()` to gather `*.lua` under each root,
and add to `lint()` — mirroring the mesh check at line ~161:

```python
    # Every script a scene names must exist. A dangling script path is exactly
    # as broken as a dangling mesh, and the runtime cannot tell you: an entity
    # whose script failed to load simply sits there.
    for doc in sorted(root.rglob("*.scn")):
        data = json.loads(doc.read_text())
        for entity in data.get("entities", []):
            for script in entity.get("scripts", []):
                path = script.get("path", "")
                if path and Path(path).name not in scripts:
                    errors.append(
                        f"{doc.relative_to(ROOT)}: script '{path}' not found")
```

Read the file first — `collect()` already returns a `scripts` name in its
tuple (line ~116), so check whether it is already populated and follow that.

- [ ] **Step 3: Run both checks**

Run: `python3 tools/assetlint.py && make cook SCENE=cozy_lair.scn VALIDATE=1`
Expected: both pass.

Then deliberately break one: change a script path in the `.scn` to
`scripts/nope.lua` and re-run. Expected: **assetlint fails** with the dangling
path. Restore it. A check that has never failed is a check nobody has tested.

- [ ] **Step 4: Write `docs/scripting.md`**

Match the voice of `docs/ecs.md` and `docs/particles.md` — explain *why* a
thing is shaped the way it is, not just what it does. Sections:

1. **The object model** — a script is a table; an instance is `self`; the class
   is shared and the state is not.
2. **Lifecycle** — every callback, and the exact tick order table from the
   spec, with the reason `fixed_update` is defined as "before a physics step"
   rather than "on the fixed clock".
3. **Props** — the five types, how they reach `self.props`, why `Entity` props
   resolve at `start()` and arrive as `nil` with a warning when they cannot.
4. **API reference** — `world`, `log`, `input`, `physics`, `event`, `vec3`, the
   entity handle, and the reflection fallback. Note that `world.find` is a
   linear scan and handles should be cached in `start()`.
5. **Errors** — what a report looks like, what quarantine means, how to revive.
6. **Hot reload** — what survives a reload and what does not, and why `start()`
   is not re-run.
7. **Console** — the four commands.
8. **HOW TO ADD A SCRIPT** — the concrete tutorial:
   1. write `assets/scripts/my_thing.lua` returning a table;
   2. add a `"scripts"` entry on an entity in the `.scn` (show the JSON);
   3. declare any props;
   4. `make cook SCENE=<x>.scn` — a bad path or a syntax error fails here;
   5. `make scene SCENE=<x>.scn`;
   6. edit the `.lua` while it runs and watch it reload.
9. **Limits** — no coroutines, no `io`/`os`/`package`, numbers are `f32` once
   authored as props, `Quat` fields are not exposed through the reflection
   proxy (use the entity handle's `rotation`, in Euler degrees).

- [ ] **Step 5: Update the three existing documents**

- `docs/ecs.md`: add `Scripts` to the component table ("Lua behaviours attached
  to this entity") and `ScriptState` to the runtime-handle row. Add a paragraph
  under "Behavioural components and their systems" stating that `ScriptHost` is
  the deliberate exception to the free-function `(World&, dt)` shape, because
  its callbacks land at three different points in the frame and a generic
  `update(dt)` would hide the ordering that is the whole point.
- `ARCHITECTURE.md`: add `eng_script` to the layer table between
  `eng_framework` and `eng` — "Lua scripting: the VM, the bindings, the script
  host", may link `eng_framework`.
- `docs/scene-editor-entities.md`: how to attach a script and edit props in the
  inspector, and what the entity picker on an `Entity` prop is for.

- [ ] **Step 6: Full verification**

Run:
```sh
cmake --build build -j8
ctest --test-dir build --output-on-failure
```
Expected: **everything** passes, including `layering`, `assetlint`, and all six
script tests.

Then the on-screen check one final time:
```sh
make screenshot SHOT=/tmp/final.png FRAME=240
```
**Read the PNG.** Expected: the scene renders as it did before scripting
existed, plus the scripted entity's motion. The image is frozen — no refactor
may change the rendered image, and `make visual-test` is how that is proven:

```sh
make visual-test
```
Expected: PASS.

- [ ] **Step 7: Commit**

```bash
git add docs/ tools/assetlint.py editor/src/content/SceneValidate.cpp CMakeLists.txt
git commit -m "docs: Lua scripting architecture and tutorial

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

## Verification checklist

Run before considering the feature done:

```sh
cmake --build build -j8                      # everything compiles
ctest --test-dir build --output-on-failure   # incl. layering + assetlint
python3 tools/check_layering.py              # eng_script sits at framework
python3 tools/assetlint.py                   # no dangling script paths
make cook SCENE=cozy_lair.scn VALIDATE=1     # scripts parse at cook time
make visual-test                             # the image did not change
make screenshot SHOT=/tmp/final.png FRAME=240 # and READ the png
```

Against the spec's own definition of done:

- [ ] `Scripts` component authored in the editor, several per entity, with props
- [ ] `start`, `update`, `fixed_update`, `on_destroy`, `on_event`,
      `on_collision`, `on_trigger`, `on_reload` all dispatched
- [ ] errors carry a Lua traceback naming the script path and line
- [ ] a failing instance is quarantined, not respawned every frame
- [ ] hot reload swaps behaviour and keeps instance state
- [ ] every registered component is reachable from Lua without new C++
- [ ] the field proxy survives a pool-moving `emplace` (asserted, not assumed)
- [ ] contacts are multi-subscriber and both original call sites migrated
- [ ] a dangling script path fails the build
- [ ] `docs/scripting.md` explains the architecture and carries the tutorial
