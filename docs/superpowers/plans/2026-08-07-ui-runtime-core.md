# UI Runtime Core (M1–M3) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give the UI-as-scenes stack the three things it needs before a Tarkov-class trader screen is authorable at all: images, clickable interaction, and a repeater with per-item binding scope.

**Architecture:** Four new reflected components (`UiImage` 44, `UiInteract` 45, `UiRepeat` 46, plus session state that is deliberately *not* a component). Images ride the existing `rhi_texture_registry` token that `BitmapFont` already uses as an `ImTextureID`. Interaction is a separate `eng::ui::UiInteraction` object that consumes a solved layout plus POD input and emits events — never a component, so a screen stays reloadable, and headless-testable so the editor can drive it later. The repeater expands inside `solveUiLayout`: one entity appears N times in the solved list with different bounds and an item index, so nothing is cloned and paint/hit-test walk the same flat list they already walk.

**Tech Stack:** C++20, EnTT, glm, Dear ImGui draw lists, the repo's own `check()`-and-counter test style, CMake via `eng_add_test`.

## Global Constraints

- **The rendered world image is frozen.** UI paints on imgui's foreground draw list after the PSX post chain. `make visual-test` must stay green. No shader, material, compositor or render-preset change is in scope.
- **Never clean-build.** Build single targets with `cmake --build build --target <t> -j8`. Never `rm -rf build`. If the tree breaks, `cmake -S . -B build`.
- **Build once at the end**, not after each task — repeated full builds thrash swap and produce spurious `Bus error` ICEs on this machine. Write all code, then one build, then `make test`.
- **`FieldType` is append-only and must not be touched.** `eng::runtime::ProjectComponents` derives declared-component byte layout from its values. Nine-slice borders are therefore two `Vec2` fields, never a new `Vec4`.
- **Layering is enforced by `tools/check_layering.py`** as a ctest: `engine/src/ui/` is the systems layer and **may not include the ECS**. Anything reading an `entt` registry goes in `engine/src/ecs/` with its public header still under `eng/ui/`. `UiScene.cpp` is the precedent.
- **Component ids are permanent.** 44 `UiImage`, 45 `UiInteract`, 46 `UiRepeat`. Do not renumber 38–43.
- **`entt::entity{}` is entity zero, not null.** Every "miss" sentinel is `entt::null`.
- **Tones, not colours.** Every new tone field is an `int` clamped into `eng::ui::UiTone` with an opt-in `useColour`/`colour` escape hatch, matching `UiLabel`.
- **An unanswered binding falls back to the authored value.** This must hold for item-scoped bindings too, or screens stop being previewable in the editor.
- **Font rhythm:** Antiquity's glyph cell is 18px. Label boxes need ~18px, list rows ~19px pitch.
- Commit messages end with `Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>`.

## File Structure

**Create:**
- `engine/include/eng/ui/UiTextureCache.h` — path → `{token, size}`, nearest+clamp, cached. No ECS.
- `engine/src/ui/UiTextureCache.cpp` — in `eng_systems`.
- `engine/include/eng/ui/UiInteraction.h` — `Pointer`, `Keys`, `Event`, `State`, `UiInteraction`. Public header under `eng/ui/`.
- `engine/src/ecs/UiInteraction.cpp` — in `eng_framework`; reads the registry, hence `src/ecs`.
- `engine/tests/UiInteractionTests.cpp` — new headless test binary.

**Modify:**
- `engine/include/eng/ecs/components/UiComponents.h` — add `UiImage`, `UiInteract`, `UiRepeat` + `fieldsOf` declarations.
- `engine/src/ecs/ComponentRegistry.cpp` — three field tables, three `reg.add(...)` at ids 44–46.
- `engine/include/eng/ui/UiCanvas.h` / `engine/src/ui/UiCanvas.cpp` — `image()`, `imageSliced()`, public `clipPush()`/`clipPop()`.
- `engine/include/eng/ui/UiScene.h` — `UiSolvedRect::item`, item-scoped `UiDataSource` methods, `UiHit`, overloads taking interaction state.
- `engine/src/ecs/UiScene.cpp` — repeat expansion in the solver, image + scoped-binding paint, clipping.
- `engine/tests/UiSceneTests.cpp` — repeat and scope tests.
- `cmake/Engine.cmake` — two new source files into the right targets.
- `cmake/Tests.cmake` — register `ui_interaction`.
- `docs/ui-scenes.md`, new `docs/ui-interaction.md`.

---

### Task 1: `UiTextureCache` — a UI texture, once

**Files:**
- Create: `engine/include/eng/ui/UiTextureCache.h`, `engine/src/ui/UiTextureCache.cpp`
- Modify: `cmake/Engine.cmake` (append to `_eng_systems_sources`, beside `engine/src/ui/UiCanvas.cpp`)

**Interfaces:**
- Consumes: `eng::rhi_texture_registry::load(const std::filesystem::path&, rhi::FilterMode, rhi::AddressMode, int& w, int& h) -> uint64_t` from `engine/src/render/rhi/RenderCore.h`, and `eng::assets::resolve` from `eng/assets/AssetRoot.h`.
- Produces: `struct eng::ui::UiTexture { uint64_t token; glm::ivec2 size; bool valid() const; }` and `class eng::ui::UiTextureCache` with `const UiTexture& get(const std::string& path)` and `void clear()`.

- [ ] **Step 1: Write the header**

```cpp
#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <unordered_map>

namespace eng::ui {

// A texture the UI can draw, keyed by the path it was authored as.
//
// The imgui backend takes a `uint64_t` token as its ImTextureID -- this is the
// same road BitmapFont's atlas already travels (see BitmapFont::load), so an
// image primitive is a thin wrapper over machinery that is already proven,
// not new rendering infrastructure.
struct UiTexture {
    uint64_t token = 0;
    glm::ivec2 size{0, 0};
    bool valid() const { return token != 0; }
};

// Path -> texture, loaded once and kept for the process.
//
// Filtering is forced to nearest and addressing to clamp, and neither is a
// parameter: this canvas exists to be crisp at integer magnification, and a
// linear-filtered UI texture shimmers the moment the window is resized. A
// caller that wants a smooth texture wants a different canvas.
class UiTextureCache {
public:
    // A path that will not load yields an invalid UiTexture, cached as such --
    // so a missing file is one log line, not one per frame forever.
    const UiTexture& get(const std::string& path);
    void clear() { mEntries.clear(); }

private:
    std::unordered_map<std::string, UiTexture> mEntries;
};

} // namespace eng::ui
```

- [ ] **Step 2: Write the implementation**

```cpp
#include <eng/ui/UiTextureCache.h>

#include <eng/Log.h>
#include <eng/assets/AssetRoot.h>

#include "render/rhi/RenderCore.h"
#include "render/rhi/Rhi.h"

#include <filesystem>

namespace eng::ui {

const UiTexture& UiTextureCache::get(const std::string& path)
{
    const auto found = mEntries.find(path);
    if (found != mEntries.end())
        return found->second;

    UiTexture tex;
    const std::filesystem::path resolved = eng::assets::resolve(path);
    if (resolved.empty()) {
        eng::log::warn("UI texture: '%s' is not in a mounted pack",
                       path.c_str());
    } else {
        int w = 0, h = 0;
        // Nearest and clamp, deliberately not configurable -- see the header.
        tex.token = rhi_texture_registry::load(
            resolved, rhi::FilterMode::Nearest, rhi::AddressMode::Clamp, w, h);
        tex.size = {w, h};
        if (!tex.valid())
            eng::log::warn("UI texture: '%s' failed to upload", path.c_str());
    }
    // Inserted even when invalid: a failure cached is a failure logged once,
    // and a UI that retries a missing file every frame floods the log and
    // stalls on the filesystem.
    return mEntries.emplace(path, tex).first->second;
}

} // namespace eng::ui
```

- [ ] **Step 3: Confirm the exact filter/address enum spellings**

Run: `grep -n "enum class FilterMode" -A 6 engine/src/render/rhi/Rhi.h; grep -n "enum class AddressMode" -A 6 engine/src/render/rhi/Rhi.h`
Expected: the enumerator names used above. If they differ (e.g. `Point` rather than `Nearest`, `ClampToEdge` rather than `Clamp`), fix the two call-site spellings — do not add a translation layer.

- [ ] **Step 4: Register the source**

In `cmake/Engine.cmake`, inside `set(_eng_systems_sources ...)`, add after `engine/src/ui/UiCanvas.cpp`:

```cmake
  engine/src/ui/UiTextureCache.cpp
```

- [ ] **Step 5: Commit**

```bash
git add engine/include/eng/ui/UiTextureCache.h engine/src/ui/UiTextureCache.cpp cmake/Engine.cmake
git commit -m "feat(ui): a texture cache for the UI canvas

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 2: `UiCanvas::image` and nine-slice

**Files:**
- Modify: `engine/include/eng/ui/UiCanvas.h`, `engine/src/ui/UiCanvas.cpp`
- Test: `engine/tests/UiCanvasTests.cpp`

**Interfaces:**
- Consumes: `eng::ui::UiTexture` from Task 1; `UiCanvas::toScreen(glm::ivec2) -> glm::vec2` and `UiCanvas::list() -> ImDrawList*`, both existing privates.
- Produces:
  - `void UiCanvas::image(glm::ivec2 at, glm::ivec2 size, const UiTexture&, glm::vec2 uvMin, glm::vec2 uvMax, unsigned int tint) const`
  - `void UiCanvas::imageSliced(glm::ivec2 at, glm::ivec2 size, const UiTexture&, glm::ivec2 borderMin, glm::ivec2 borderMax, unsigned int tint) const`
  - `void UiCanvas::clipPush(glm::ivec2 at, glm::ivec2 size) const` / `void UiCanvas::clipPop() const`
  - `static glm::ivec2 eng::ui::fitImage(glm::ivec2 box, glm::ivec2 source, int mode)` — free function in `UiCanvas.h`, so it is testable without a GPU.

- [ ] **Step 1: Write the failing test**

Append to `engine/tests/UiCanvasTests.cpp`, inside `main()` beside the existing checks:

```cpp
    // fitImage: 0 Stretch, 1 Contain, 2 Cover. Contain and Cover are the two
    // that have to be right -- an icon that stretches is an icon that looks
    // broken, and the arithmetic is exactly where an off-by-one hides.
    {
        using eng::ui::fitImage;
        // Stretch always fills the box.
        check(fitImage({100, 50}, {32, 32}, 0) == glm::ivec2(100, 50),
              "fitImage stretch fills the box");
        // Contain: a square source in a wide box is limited by height.
        check(fitImage({100, 50}, {32, 32}, 1) == glm::ivec2(50, 50),
              "fitImage contain fits the short axis");
        // Cover: the same source must overflow the long axis instead.
        check(fitImage({100, 50}, {32, 32}, 2) == glm::ivec2(100, 100),
              "fitImage cover fills the long axis");
        // A source with no size cannot divide; it falls back to the box.
        check(fitImage({100, 50}, {0, 0}, 1) == glm::ivec2(100, 50),
              "fitImage tolerates a zero-sized source");
    }
```

- [ ] **Step 2: Run it and watch it fail**

Run: `cmake --build build --target ui_canvas_tests -j8`
Expected: FAIL to compile — `'fitImage' is not a member of 'eng::ui'`.

- [ ] **Step 3: Add `fitImage` to the header**

In `engine/include/eng/ui/UiCanvas.h`, after the `Align` enum and before `struct PanelPaint`:

```cpp
// How an image fills its box. Mirrors UiImage::fit.
enum class ImageFit { Stretch, Contain, Cover, NineSlice };

// The drawn size of `source` inside `box` under `mode` (an ImageFit as an int,
// because the component carries it as one). Free and header-visible so it can
// be tested without a GPU, a window or an imgui context.
//
// Contain and Cover both preserve aspect; they differ only in which axis wins,
// and getting that backwards is the classic icon bug -- hence the test.
glm::ivec2 fitImage(glm::ivec2 box, glm::ivec2 source, int mode);
```

- [ ] **Step 4: Implement `fitImage`**

In `engine/src/ui/UiCanvas.cpp`, in namespace `eng::ui` at file scope:

```cpp
glm::ivec2 fitImage(glm::ivec2 box, glm::ivec2 source, int mode)
{
    // A source with no size has no aspect to preserve, so every mode is
    // Stretch. Returning the box rather than zero keeps a broken texture
    // visible as a wrongly-filled rectangle instead of silently absent.
    if (source.x <= 0 || source.y <= 0)
        return box;
    const auto fit = ImageFit(std::clamp(mode, 0, int(ImageFit::NineSlice)));
    if (fit == ImageFit::Stretch || fit == ImageFit::NineSlice)
        return box;

    const float sx = float(box.x) / float(source.x);
    const float sy = float(box.y) / float(source.y);
    const float s = fit == ImageFit::Contain ? std::min(sx, sy)
                                             : std::max(sx, sy);
    return {std::max(1, int(std::lround(float(source.x) * s))),
            std::max(1, int(std::lround(float(source.y) * s)))};
}
```

Ensure `<algorithm>` and `<cmath>` are included in that file.

- [ ] **Step 5: Run the test and watch it pass**

Run: `cmake --build build --target ui_canvas_tests -j8 && ./build/ui_canvas_tests`
Expected: PASS, exit 0.

- [ ] **Step 6: Add the drawing primitives to the header**

In `engine/include/eng/ui/UiCanvas.h`, add `#include <eng/ui/UiTextureCache.h>` at the top, and inside the primitives block after `icon(...)`:

```cpp
    // A textured quad. `uvMin`/`uvMax` select a sub-rect, which is atlas
    // support with no atlas format: an item sheet is one PNG and each icon is
    // a rectangle in it.
    //
    // An invalid texture draws a magenta chip rather than nothing. A missing
    // texture that renders as empty space is a missing texture nobody notices
    // until a player does.
    void image(glm::ivec2 at, glm::ivec2 size, const UiTexture& tex,
               glm::vec2 uvMin = {0.0f, 0.0f}, glm::vec2 uvMax = {1.0f, 1.0f},
               unsigned int tint = 0xFFFFFFFFu) const;

    // Nine-slice: corners drawn at source size, edges stretched along one axis,
    // centre along both. `borderMin` is the left/top inset in *source* pixels,
    // `borderMax` the right/bottom.
    //
    // This is the highest-leverage primitive here: every frame, slot, plate and
    // window chrome in an authored screen is a nine-slice, and without it a
    // panel skin needs code.
    void imageSliced(glm::ivec2 at, glm::ivec2 size, const UiTexture& tex,
                     glm::ivec2 borderMin, glm::ivec2 borderMax,
                     unsigned int tint = 0xFFFFFFFFu) const;

    // Clip subsequent drawing to a virtual-pixel box. Public because a
    // scrolling container is a scene component now, and the scene painter lives
    // in another translation unit. Must be balanced.
    void clipPush(glm::ivec2 at, glm::ivec2 size) const;
    void clipPop() const;
```

- [ ] **Step 7: Implement the primitives**

In `engine/src/ui/UiCanvas.cpp`:

```cpp
void UiCanvas::image(glm::ivec2 at, glm::ivec2 size, const UiTexture& tex,
                     glm::vec2 uvMin, glm::vec2 uvMax, unsigned int tint) const
{
    if (size.x <= 0 || size.y <= 0)
        return;
    ImDrawList* draw = list();
    if (!draw)
        return;
    if (!tex.valid()) {
        // Magenta: the traditional "this asset is missing" colour, and one no
        // palette tone will ever produce by accident.
        rect(at, size, 0xFFFF00FFu);
        return;
    }
    const glm::vec2 a = toScreen(at);
    const glm::vec2 b = toScreen(at + size);
    draw->AddImage(ImTextureID(tex.token), ImVec2(a.x, a.y), ImVec2(b.x, b.y),
                   ImVec2(uvMin.x, uvMin.y), ImVec2(uvMax.x, uvMax.y), tint);
}

void UiCanvas::imageSliced(glm::ivec2 at, glm::ivec2 size, const UiTexture& tex,
                           glm::ivec2 borderMin, glm::ivec2 borderMax,
                           unsigned int tint) const
{
    if (!tex.valid() || tex.size.x <= 0 || tex.size.y <= 0) {
        image(at, size, tex, {0.0f, 0.0f}, {1.0f, 1.0f}, tint);
        return;
    }
    // Borders clamped so the two of them can never exceed the box or the
    // source: a nine-slice authored with 16px corners drawn into a 20px box
    // would otherwise produce negative-width middles and inside-out quads.
    const int lx = std::clamp(borderMin.x, 0, std::min(tex.size.x, size.x) / 2);
    const int rx = std::clamp(borderMax.x, 0, std::min(tex.size.x, size.x) / 2);
    const int ty = std::clamp(borderMin.y, 0, std::min(tex.size.y, size.y) / 2);
    const int by = std::clamp(borderMax.y, 0, std::min(tex.size.y, size.y) / 2);

    // Column and row edges, in destination pixels and in source pixels, so the
    // nine quads are one double loop rather than nine hand-written calls.
    const int dx[4] = {at.x, at.x + lx, at.x + size.x - rx, at.x + size.x};
    const int dy[4] = {at.y, at.y + ty, at.y + size.y - by, at.y + size.y};
    const float sx[4] = {0.0f, float(lx) / float(tex.size.x),
                         float(tex.size.x - rx) / float(tex.size.x), 1.0f};
    const float sy[4] = {0.0f, float(ty) / float(tex.size.y),
                         float(tex.size.y - by) / float(tex.size.y), 1.0f};

    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            const glm::ivec2 cellAt{dx[col], dy[row]};
            const glm::ivec2 cellSize{dx[col + 1] - dx[col],
                                      dy[row + 1] - dy[row]};
            if (cellSize.x <= 0 || cellSize.y <= 0)
                continue;
            image(cellAt, cellSize, tex, {sx[col], sy[row]},
                  {sx[col + 1], sy[row + 1]}, tint);
        }
    }
}

void UiCanvas::clipPush(glm::ivec2 at, glm::ivec2 size) const
{
    if (ImDrawList* draw = list()) {
        const glm::vec2 a = toScreen(at);
        const glm::vec2 b = toScreen(at + size);
        draw->PushClipRect(ImVec2(a.x, a.y), ImVec2(b.x, b.y), true);
    }
}

void UiCanvas::clipPop() const
{
    if (ImDrawList* draw = list())
        draw->PopClipRect();
}
```

- [ ] **Step 8: Commit**

```bash
git add engine/include/eng/ui/UiCanvas.h engine/src/ui/UiCanvas.cpp engine/tests/UiCanvasTests.cpp
git commit -m "feat(ui): textured quads, nine-slice and clipping on UiCanvas

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 3: The three components and their field tables

**Files:**
- Modify: `engine/include/eng/ecs/components/UiComponents.h`, `engine/src/ecs/ComponentRegistry.cpp`
- Test: `engine/tests/ComponentReflectTests.cpp`

**Interfaces:**
- Produces: `eng::ecs::UiImage`, `eng::ecs::UiInteract`, `eng::ecs::UiRepeat`, registered at ids 44, 45, 46; `eng::fieldsOf<T>()` for each.

- [ ] **Step 1: Add the component structs**

In `engine/include/eng/ecs/components/UiComponents.h`, after `struct UiList`:

```cpp
// A textured quad: an item icon, a portrait, a rarity frame, a panel skin.
//
// The chip UiIcon draws is a solid colour, which is all a bullet or a status
// dot needs and nothing a trader screen does. This is the component that makes
// an authored screen able to show art.
struct UiImage {
    // Resolved through the asset root, like every other asset path.
    std::string texture;
    // A sub-rect of the texture, which is atlas support with no atlas format:
    // an item sheet is one PNG and each icon is a rectangle in it. A dedicated
    // sprite-atlas asset can arrive later without changing this component.
    glm::vec2 uvMin{0.0f, 0.0f};
    glm::vec2 uvMax{1.0f, 1.0f};
    // eng::ui::ImageFit: 0 Stretch, 1 Contain, 2 Cover, 3 NineSlice.
    int fit = 0;
    // Nine-slice borders in *source* pixels: left/top and right/bottom.
    //
    // Two vec2 rather than one vec4 because FieldType has no Vec4 and adding
    // one renumbers the enum eng::runtime::ProjectComponents derives declared
    // component byte layout from -- the trap that already cost a debugging
    // session when Vec2 was appended. Two vec2s cost nothing and touch no enum.
    glm::vec2 borderMin{0.0f, 0.0f};
    glm::vec2 borderMax{0.0f, 0.0f};
    int tone = 0; // tint, as a palette role
    glm::vec3 colour{1.0f, 1.0f, 1.0f};
    bool useColour = false;
    float opacity = 1.0f;
    // A data key naming a texture path, for a slot whose icon depends on what
    // is in it. Wins over `texture` when it resolves, exactly as
    // UiLabel::binding wins over UiLabel::text.
    std::string binding;
};

// Makes anything clickable.
//
// Separate from UiPanel and UiImage on purpose: a clickable thing is a plate
// *with* an interact, not a special kind of plate. That is what lets an icon,
// a repeat cell and a plain box all become buttons without three variants of
// each.
//
// Note what is NOT here: hovered, pressed, focused, scrolled. Those are session
// state and live in eng::ui::UiInteraction. A component that stored them would
// be a screen that cannot be reloaded without losing the player's place, which
// is the rule this file already states for UiList.
struct UiInteract {
    // What activating this means. The engine never interprets it; it carries
    // the string out to the application, exactly as UiList::action does.
    std::string action;
    // An argument carried with the action: a slot id, an item id, a tab name.
    std::string param;
    bool enabled = true;
    // A data key that can grey it out -- "trade.can_afford". Non-zero enables.
    // Falls back to `enabled` when unanswered.
    std::string enabledBinding;
    bool focusable = true;
    // Hover and press paint as a wash over whatever the entity already draws,
    // so one code path gives feedback to every widget kind.
    int hoverTone = 2;    // Focus
    int pressTone = 2;    // Focus
    int disabledTone = 1; // Muted
    // Drag and drop. Carried here so a screen authored today survives the
    // milestone that implements them; until then both are inert.
    bool draggable = false;
    bool dropTarget = false;
};

// Repeats its first child once per item in a data source.
//
// This is the per-row binding scope UiList deliberately lacks. A UiList draws a
// line of text with an optional value and gauge; a trader offer is an icon, a
// name, a price, a condition gauge and a rarity frame, and that is a subtree.
//
// The first child is the template and is not drawn in its own right. Inside it,
// a binding resolves against the item first: a label bound to "name" inside a
// repeat over "trade.stock" asks the source for item i's "name". A binding
// beginning with '/' escapes to the global scope, so a cell can still show the
// player's purse.
struct UiRepeat {
    std::string source;
    int maxItems = 64;
    // 0 Vertical, 1 Horizontal, 2 Grid.
    int direction = 0;
    int columns = 4; // Grid only
    glm::vec2 cell{48.0f, 48.0f};
    glm::vec2 gap{2.0f, 2.0f};
    // Clips cells to this entity's box and lets the wheel scroll them. The
    // offset itself is session state, in UiInteraction, not here.
    bool scrollable = true;
};
```

And extend the `fieldsOf` declaration block at the bottom of the file:

```cpp
template <> FieldSpan fieldsOf<ecs::UiImage>();
template <> FieldSpan fieldsOf<ecs::UiInteract>();
template <> FieldSpan fieldsOf<ecs::UiRepeat>();
```

- [ ] **Step 2: Add the field tables**

In `engine/src/ecs/ComponentRegistry.cpp`, immediately after `fieldsOf<ecs::UiList>()`:

```cpp
template <> FieldSpan fieldsOf<ecs::UiImage>()
{
    using U = ecs::UiImage;
    static const Field f[] = {
        ENG_FIELD(U, texture, FieldType::String),
        ENG_FIELD(U, uvMin, FieldType::Vec2),
        ENG_FIELD(U, uvMax, FieldType::Vec2),
        ENG_FIELD_RANGE(U, fit, FieldType::Int, 0.0f, 3.0f),
        ENG_FIELD(U, borderMin, FieldType::Vec2),
        ENG_FIELD(U, borderMax, FieldType::Vec2),
        ENG_FIELD_RANGE(U, tone, FieldType::Int, 0.0f, 7.0f),
        ENG_FIELD(U, colour, FieldType::Colour),
        ENG_FIELD(U, useColour, FieldType::Bool),
        ENG_FIELD_RANGE(U, opacity, FieldType::Float, 0.0f, 1.0f),
        ENG_FIELD(U, binding, FieldType::String),
    };
    return {f, int(std::size(f))};
}

template <> FieldSpan fieldsOf<ecs::UiInteract>()
{
    using U = ecs::UiInteract;
    static const Field f[] = {
        ENG_FIELD(U, action, FieldType::String),
        ENG_FIELD(U, param, FieldType::String),
        ENG_FIELD(U, enabled, FieldType::Bool),
        ENG_FIELD(U, enabledBinding, FieldType::String),
        ENG_FIELD(U, focusable, FieldType::Bool),
        ENG_FIELD_RANGE(U, hoverTone, FieldType::Int, 0.0f, 7.0f),
        ENG_FIELD_RANGE(U, pressTone, FieldType::Int, 0.0f, 7.0f),
        ENG_FIELD_RANGE(U, disabledTone, FieldType::Int, 0.0f, 7.0f),
        ENG_FIELD(U, draggable, FieldType::Bool),
        ENG_FIELD(U, dropTarget, FieldType::Bool),
    };
    return {f, int(std::size(f))};
}

template <> FieldSpan fieldsOf<ecs::UiRepeat>()
{
    using U = ecs::UiRepeat;
    static const Field f[] = {
        ENG_FIELD(U, source, FieldType::String),
        ENG_FIELD_RANGE(U, maxItems, FieldType::Int, 1.0f, 1024.0f),
        ENG_FIELD_RANGE(U, direction, FieldType::Int, 0.0f, 2.0f),
        ENG_FIELD_RANGE(U, columns, FieldType::Int, 1.0f, 64.0f),
        ENG_FIELD(U, cell, FieldType::Vec2),
        ENG_FIELD(U, gap, FieldType::Vec2),
        ENG_FIELD(U, scrollable, FieldType::Bool),
    };
    return {f, int(std::size(f))};
}
```

- [ ] **Step 3: Register them**

In the same file, after `reg.add(reflectedComponent<UiList>("UiList", 43));`:

```cpp
    // 44-46 continue the UI block. Ids are permanent: a cooked .map names its
    // components by number, so renumbering rewrites the meaning of every file
    // already on disk.
    reg.add(reflectedComponent<UiImage>("UiImage", 44));
    reg.add(reflectedComponent<UiInteract>("UiInteract", 45));
    reg.add(reflectedComponent<UiRepeat>("UiRepeat", 46));
```

- [ ] **Step 4: Assert the ids in a test**

Append inside `main()` in `engine/tests/ComponentReflectTests.cpp`:

```cpp
    // Ids are a file format. A renumbered component silently reinterprets every
    // cooked .map already written, which is the kind of bug that surfaces as
    // "the inventory screen is full of garbage" three weeks later.
    {
        const struct { const char* name; int id; } expect[] = {
            {"UiRect", 38}, {"UiPanel", 39}, {"UiLabel", 40},
            {"UiBar", 41},  {"UiIcon", 42},  {"UiList", 43},
            {"UiImage", 44}, {"UiInteract", 45}, {"UiRepeat", 46},
        };
        for (const auto& e : expect) {
            const eng::ecs::ComponentType* t = registry.find(e.name);
            check(t != nullptr, "UI component is registered");
            if (t)
                check(t->id == e.id, "UI component keeps its id");
        }
    }
```

Adjust `registry` and `find`/`id` to whatever that file's existing checks already use — read the top of the file first and match it exactly rather than inventing an accessor.

- [ ] **Step 5: Commit**

```bash
git add engine/include/eng/ecs/components/UiComponents.h engine/src/ecs/ComponentRegistry.cpp engine/tests/ComponentReflectTests.cpp
git commit -m "feat(ui): UiImage, UiInteract and UiRepeat components (ids 44-46)

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 4: Item-scoped bindings on `UiDataSource`

**Files:**
- Modify: `engine/include/eng/ui/UiScene.h`
- Test: `engine/tests/UiSceneTests.cpp`

**Interfaces:**
- Produces, on `eng::ui::UiDataSource`:
  - `virtual int itemCount(std::string_view source) const`
  - `virtual bool itemText(std::string_view source, int index, std::string_view key, std::string& out) const`
  - `virtual bool itemNumber(std::string_view source, int index, std::string_view key, float& out) const`
  - and the free function `bool eng::ui::resolveScopedText(const UiDataSource*, std::string_view source, int item, std::string_view binding, std::string& out)` plus its `...Number` twin.

- [ ] **Step 1: Write the failing test**

In `engine/tests/UiSceneTests.cpp`, add a scoped data source in the anonymous namespace:

```cpp
// A source with one global key and a two-item list, for the scope tests.
struct ScopedData : ui::UiDataSource {
    bool text(std::string_view key, std::string& out) const override
    {
        if (key != "player.purse")
            return false;
        out = "240";
        return true;
    }
    int itemCount(std::string_view source) const override
    {
        return source == "trade.stock" ? 2 : 0;
    }
    bool itemText(std::string_view source, int index, std::string_view key,
                  std::string& out) const override
    {
        if (source != "trade.stock" || index < 0 || index > 1 || key != "name")
            return false;
        out = index == 0 ? "Iron Sword" : "Oak Shield";
        return true;
    }
    bool itemNumber(std::string_view source, int index, std::string_view key,
                    float& out) const override
    {
        if (source != "trade.stock" || key != "price")
            return false;
        out = index == 0 ? 120.0f : 45.0f;
        return true;
    }
};
```

and in `main()`:

```cpp
    // Binding scope inside a repeat. The three rules that matter: a bare key
    // asks the item, a '/'-prefixed key escapes to the global scope, and an
    // unanswered key leaves the authored value alone -- which is what keeps a
    // screen previewable in an editor with no game behind it.
    {
        const ScopedData data;
        std::string out = "authored";
        check(ui::resolveScopedText(&data, "trade.stock", 1, "name", out),
              "a bare binding resolves against the item");
        check(out == "Oak Shield", "the item scope picks the right row");

        out = "authored";
        check(ui::resolveScopedText(&data, "trade.stock", 0, "/player.purse",
                                    out),
              "a leading slash escapes to the global scope");
        check(out == "240", "the escaped binding reads the global key");

        out = "authored";
        check(!ui::resolveScopedText(&data, "trade.stock", 0, "missing", out),
              "an unanswered scoped binding reports failure");
        check(out == "authored", "and leaves the authored value alone");

        // Outside a repeat (item < 0) a bare key is global, or nothing would
        // resolve on an ordinary screen.
        out = "authored";
        check(ui::resolveScopedText(&data, "", -1, "player.purse", out),
              "outside a repeat a bare binding is global");
        check(out == "240", "and reads the global key");

        float n = -1.0f;
        check(ui::resolveScopedNumber(&data, "trade.stock", 0, "price", n),
              "scoped numbers resolve too");
        check(n == 120.0f, "and pick the right row");
    }
```

- [ ] **Step 2: Run it and watch it fail**

Run: `cmake --build build --target ui_scene_tests -j8`
Expected: FAIL to compile — `resolveScopedText` is not declared, `itemCount` is not a member.

- [ ] **Step 3: Extend `UiDataSource` and declare the resolvers**

In `engine/include/eng/ui/UiScene.h`, inside `class UiDataSource`, after `rows(...)`:

```cpp
    // --- item scope, for UiRepeat ----------------------------------------
    //
    // A repeat asks how many items its source has, then asks each item for the
    // keys its template binds. All three default to "I know nothing", so every
    // UiDataSource written before repeats existed still compiles and still
    // works -- it simply drives no repeaters.
    virtual int itemCount(std::string_view source) const
    {
        (void)source;
        return 0;
    }
    virtual bool itemText(std::string_view source, int index,
                          std::string_view key, std::string& out) const
    {
        (void)source; (void)index; (void)key; (void)out;
        return false;
    }
    virtual bool itemNumber(std::string_view source, int index,
                            std::string_view key, float& out) const
    {
        (void)source; (void)index; (void)key; (void)out;
        return false;
    }
```

and after the class:

```cpp
// Resolve a binding that may be inside a repeat.
//
// `item` < 0 means "not in a repeat", where a bare key is global. Inside one, a
// bare key asks the item and a key beginning with '/' escapes to the global
// scope -- so a cell showing an item's price and the player's purse side by
// side needs no special component.
//
// Free functions rather than members so paint, hit-test and the editor share
// one definition of what a binding means. Two of them rather than a template
// because text and number take different out-parameters and a template would
// only hide that.
bool resolveScopedText(const UiDataSource* data, std::string_view source,
                       int item, std::string_view binding, std::string& out);
bool resolveScopedNumber(const UiDataSource* data, std::string_view source,
                         int item, std::string_view binding, float& out);
```

- [ ] **Step 4: Implement the resolvers**

In `engine/src/ecs/UiScene.cpp`, in namespace `eng::ui` at file scope:

```cpp
bool resolveScopedText(const UiDataSource* data, std::string_view source,
                       int item, std::string_view binding, std::string& out)
{
    if (!data || binding.empty())
        return false;
    if (binding.front() == '/')
        return data->text(binding.substr(1), out);
    if (item < 0 || source.empty())
        return data->text(binding, out);
    return data->itemText(source, item, binding, out);
}

bool resolveScopedNumber(const UiDataSource* data, std::string_view source,
                         int item, std::string_view binding, float& out)
{
    if (!data || binding.empty())
        return false;
    if (binding.front() == '/')
        return data->number(binding.substr(1), out);
    if (item < 0 || source.empty())
        return data->number(binding, out);
    return data->itemNumber(source, item, binding, out);
}
```

- [ ] **Step 5: Run the test and watch it pass**

Run: `cmake --build build --target ui_scene_tests -j8 && ./build/ui_scene_tests`
Expected: PASS, exit 0.

- [ ] **Step 6: Commit**

```bash
git add engine/include/eng/ui/UiScene.h engine/src/ecs/UiScene.cpp engine/tests/UiSceneTests.cpp
git commit -m "feat(ui): item-scoped bindings, with a '/' escape to global

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 5: Repeat expansion in the solver

**Files:**
- Modify: `engine/include/eng/ui/UiScene.h`, `engine/src/ecs/UiScene.cpp`
- Test: `engine/tests/UiSceneTests.cpp`

**Interfaces:**
- Consumes: `UiDataSource::itemCount` (Task 4); the existing file-local `solveInto` and `resolve`.
- Produces:
  - `int UiSolvedRect::item` (default `-1`), and `std::string_view UiSolvedRect::scope` is **not** added — the source string is looked up from the owning `UiRepeat` at paint time instead. Paint carries the repeat source down its own recursion.
  - `void solveUiLayout(const registry&, UiRect surface, const UiDataSource* data, std::vector<UiSolvedRect>& out)` — new overload. The existing three-argument form stays and forwards with `nullptr`.
  - `glm::ivec2 repeatCellOrigin(const ecs::UiRepeat&, int index)` — free, header-visible, for the tests and for hit-testing.

- [ ] **Step 1: Write the failing test**

In `engine/tests/UiSceneTests.cpp`, in `main()`:

```cpp
    // Cell geometry. Vertical stacks down, horizontal runs across, and grid
    // wraps at `columns` -- authored in virtual pixels, so the arithmetic is
    // exact and worth pinning.
    {
        UiRepeat r;
        r.cell = {40.0f, 20.0f};
        r.gap = {4.0f, 2.0f};

        r.direction = 0; // Vertical
        check(ui::repeatCellOrigin(r, 0) == glm::ivec2(0, 0),
              "vertical: item 0 at the origin");
        check(ui::repeatCellOrigin(r, 2) == glm::ivec2(0, 44),
              "vertical: item 2 is two pitches down");

        r.direction = 1; // Horizontal
        check(ui::repeatCellOrigin(r, 2) == glm::ivec2(88, 0),
              "horizontal: item 2 is two pitches across");

        r.direction = 2; // Grid
        r.columns = 3;
        check(ui::repeatCellOrigin(r, 4) == glm::ivec2(44, 22),
              "grid: item 4 wraps to row 1, column 1");
    }

    // Expansion. A repeat over a two-item source emits its template subtree
    // twice, tagged with the item index, and does not emit the template on its
    // own -- a template drawn as itself is a phantom card at the top of every
    // list.
    {
        entt::registry reg;
        const entt::entity list = reg.create();
        UiRect listRect;
        listRect.anchorMin = {0.0f, 0.0f};
        listRect.anchorMax = {0.0f, 0.0f};
        listRect.offsetMin = {10.0f, 10.0f};
        listRect.offsetMax = {110.0f, 210.0f};
        reg.emplace<UiRect>(list, listRect);
        UiRepeat rep;
        rep.source = "trade.stock";
        rep.direction = 0;
        rep.cell = {100.0f, 20.0f};
        rep.gap = {0.0f, 0.0f};
        rep.scrollable = false;
        reg.emplace<UiRepeat>(list, rep);

        // The template: a card filling its cell, with a label inside it.
        const entt::entity card = reg.create();
        UiRect cardRect;
        cardRect.anchorMin = {0.0f, 0.0f};
        cardRect.anchorMax = {1.0f, 1.0f};
        cardRect.offsetMin = {0.0f, 0.0f};
        cardRect.offsetMax = {0.0f, 0.0f};
        reg.emplace<UiRect>(card, cardRect);
        attach(reg, list, card);

        const entt::entity name = reg.create();
        UiRect nameRect;
        nameRect.anchorMin = {0.0f, 0.0f};
        nameRect.anchorMax = {1.0f, 1.0f};
        nameRect.offsetMin = {2.0f, 2.0f};
        nameRect.offsetMax = {-2.0f, -2.0f};
        reg.emplace<UiRect>(name, nameRect);
        reg.emplace<UiLabel>(name, UiLabel{});
        attach(reg, list, name); // deliberately a second child, see below

        const ScopedData data;
        std::vector<ui::UiSolvedRect> solved;
        ui::solveUiLayout(reg, surface(), &data, solved);

        // Only the FIRST child is the template. The second child is a sibling
        // of the template and is drawn once, unrepeated -- that is what lets a
        // repeat carry a header or an empty-state message beside its cells.
        int cardInstances = 0, nameInstances = 0;
        for (const ui::UiSolvedRect& s : solved) {
            if (s.entity == card) ++cardInstances;
            if (s.entity == name) ++nameInstances;
        }
        check(cardInstances == 2, "the template is emitted once per item");
        check(nameInstances == 1, "a non-template child is emitted once");

        // Item indices, and cell placement inside the repeat's own box.
        const ui::UiSolvedRect* first = nullptr;
        const ui::UiSolvedRect* second = nullptr;
        for (const ui::UiSolvedRect& s : solved) {
            if (s.entity != card) continue;
            if (s.item == 0) first = &s;
            if (s.item == 1) second = &s;
        }
        check(first && second, "each instance carries its item index");
        if (first && second) {
            check(first->bounds.position == glm::ivec2(10, 10),
                  "item 0 sits at the repeat's origin");
            check(second->bounds.position == glm::ivec2(10, 30),
                  "item 1 is one cell pitch down");
            check(first->bounds.size == glm::ivec2(100, 20),
                  "a stretched template fills its cell");
        }

        // A source nobody answers yields no instances at all, not one.
        UiRepeat empty = rep;
        empty.source = "nothing.at.all";
        reg.replace<UiRepeat>(list, empty);
        ui::solveUiLayout(reg, surface(), &data, solved);
        int none = 0;
        for (const ui::UiSolvedRect& s : solved)
            if (s.entity == card) ++none;
        check(none == 0, "an unanswered source repeats nothing");
    }
```

- [ ] **Step 2: Run it and watch it fail**

Run: `cmake --build build --target ui_scene_tests -j8`
Expected: FAIL to compile — `repeatCellOrigin` undeclared, no four-argument `solveUiLayout`, `UiSolvedRect` has no `item`.

- [ ] **Step 3: Extend the header**

In `engine/include/eng/ui/UiScene.h`, add to `struct UiSolvedRect`:

```cpp
    // Which item of an enclosing UiRepeat this box belongs to, or -1 outside
    // one. The same entity appears once per item with different bounds, which
    // is how a repeat costs no entities: nothing is cloned, the flat list
    // simply grows.
    int item = -1;
```

and after `solveUiLayout`:

```cpp
// The same, with a data source, so UiRepeat can ask how many items to emit.
// A repeat met without a source emits nothing.
void solveUiLayout(const entt::basic_registry<entt::entity>& registry,
                   UiRect surface, const UiDataSource* data,
                   std::vector<UiSolvedRect>& out);

// Where item `index` sits, relative to the repeat's own top-left. Free and
// header-visible so the solver, the hit test and the editor's handles all
// place a cell the same way -- the failure mode of two copies is clicking one
// cell and selecting its neighbour.
glm::ivec2 repeatCellOrigin(const ecs::UiRepeat& repeat, int index);
```

Add `#include <eng/ecs/components/UiComponents.h>` if the header does not already have it.

- [ ] **Step 4: Implement cell placement and expansion**

In `engine/src/ecs/UiScene.cpp`, add `using ecs::UiImage; using ecs::UiInteract; using ecs::UiRepeat;` to the existing `using` block, then at file scope in `eng::ui`:

```cpp
glm::ivec2 repeatCellOrigin(const ecs::UiRepeat& repeat, int index)
{
    const int pitchX = int(std::lround(repeat.cell.x + repeat.gap.x));
    const int pitchY = int(std::lround(repeat.cell.y + repeat.gap.y));
    const int columns = std::max(1, repeat.columns);
    switch (std::clamp(repeat.direction, 0, 2)) {
    case 1: return {pitchX * index, 0};
    case 2: return {pitchX * (index % columns), pitchY * (index / columns)};
    default: return {0, pitchY * index};
    }
}
```

Then replace the body of the anonymous-namespace `solveInto` with a version that carries the data source and an item scope. The full replacement:

```cpp
// Depth-first walk in paint order, so a parent is always solved before the
// children that resolve against it.
//
// `item` is the enclosing repeat's index, inherited by the whole subtree: a
// label three levels inside a card still belongs to that card's item.
void solveInto(const entt::basic_registry<entt::entity>& registry,
               entt::entity entity, const UiRect& parentBounds, int depth,
               const UiDataSource* data, int item,
               std::vector<UiSolvedRect>& out);

// The children of `entity`, sorted by `order` with scene order breaking ties.
// Lifted out of solveInto because the repeat path needs the same list to find
// its template child, and two copies of this sort would drift.
std::vector<entt::entity> sortedChildren(
    const entt::basic_registry<entt::entity>& registry, entt::entity entity)
{
    const ecs::Children* children = registry.try_get<ecs::Children>(entity);
    if (!children)
        return {};
    std::vector<entt::entity> sorted(children->value.begin(),
                                     children->value.end());
    std::stable_sort(sorted.begin(), sorted.end(),
                     [&registry](entt::entity a, entt::entity b) {
                         const ecs::UiRect* ra = registry.try_get<ecs::UiRect>(a);
                         const ecs::UiRect* rb = registry.try_get<ecs::UiRect>(b);
                         return (ra ? ra->order : 0) < (rb ? rb->order : 0);
                     });
    return sorted;
}

void solveInto(const entt::basic_registry<entt::entity>& registry,
               entt::entity entity, const UiRect& parentBounds, int depth,
               const UiDataSource* data, int item,
               std::vector<UiSolvedRect>& out)
{
    const ecs::UiRect* rect = registry.try_get<ecs::UiRect>(entity);
    if (!rect || !rect->visible)
        return;

    const UiRect bounds = resolve(*rect, parentBounds);
    out.push_back({entity, bounds, depth, item});

    const std::vector<entt::entity> sorted = sortedChildren(registry, entity);
    if (sorted.empty())
        return;

    const UiRepeat* repeat = registry.try_get<UiRepeat>(entity);
    if (!repeat) {
        for (const entt::entity child : sorted)
            if (registry.valid(child))
                solveInto(registry, child, bounds, depth + 1, data, item, out);
        return;
    }

    // A repeat's FIRST child is the template. Later children are ordinary and
    // drawn once, which is what lets a repeat carry a header or an empty-state
    // message beside its cells.
    const entt::entity templ = sorted.front();
    const int count = data ? std::clamp(data->itemCount(repeat->source), 0,
                                        std::max(0, repeat->maxItems))
                           : 0;
    if (registry.valid(templ)) {
        for (int i = 0; i < count; ++i) {
            // The cell is a box inside the repeat's own bounds; the template
            // resolves against it exactly as it would against any parent, so a
            // stretched template fills its cell and a pinned one sits in a
            // corner of it -- no special anchoring rule for repeats.
            UiRect cell;
            cell.position = bounds.position + repeatCellOrigin(*repeat, i);
            cell.size = {int(std::lround(repeat->cell.x)),
                         int(std::lround(repeat->cell.y))};
            solveInto(registry, templ, cell, depth + 1, data, i, out);
        }
    }
    for (std::size_t i = 1; i < sorted.size(); ++i)
        if (registry.valid(sorted[i]))
            solveInto(registry, sorted[i], bounds, depth + 1, data, item, out);
}
```

Finally, replace `solveUiLayout` with the pair:

```cpp
void solveUiLayout(const entt::basic_registry<entt::entity>& registry,
                   UiRect surface, std::vector<UiSolvedRect>& out)
{
    solveUiLayout(registry, surface, nullptr, out);
}

void solveUiLayout(const entt::basic_registry<entt::entity>& registry,
                   UiRect surface, const UiDataSource* data,
                   std::vector<UiSolvedRect>& out)
{
    out.clear();

    std::vector<entt::entity> roots;
    for (const entt::entity e : registry.view<const ecs::UiRect>()) {
        const ecs::Parent* parent = registry.try_get<ecs::Parent>(e);
        const bool parented = parent && parent->value != entt::null &&
                              registry.valid(parent->value) &&
                              registry.all_of<ecs::UiRect>(parent->value);
        if (!parented)
            roots.push_back(e);
    }
    std::stable_sort(roots.begin(), roots.end(),
                     [&registry](entt::entity a, entt::entity b) {
                         return registry.get<ecs::UiRect>(a).order <
                                registry.get<ecs::UiRect>(b).order;
                     });
    for (const entt::entity root : roots)
        solveInto(registry, root, surface, 0, data, -1, out);
}
```

- [ ] **Step 5: Run the test and watch it pass**

Run: `cmake --build build --target ui_scene_tests -j8 && ./build/ui_scene_tests`
Expected: PASS, exit 0. The pre-existing tests in that file must still pass — the three-argument `solveUiLayout` they call now forwards.

- [ ] **Step 6: Commit**

```bash
git add engine/include/eng/ui/UiScene.h engine/src/ecs/UiScene.cpp engine/tests/UiSceneTests.cpp
git commit -m "feat(ui): UiRepeat expands its template once per item in the solver

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 6: Painting images, scoped bindings and clipped repeats

**Files:**
- Modify: `engine/src/ecs/UiScene.cpp`, `engine/include/eng/ui/UiScene.h`

**Interfaces:**
- Consumes: `UiCanvas::image`/`imageSliced`/`clipPush`/`clipPop` (Task 2), `UiTextureCache` (Task 1), `resolveScopedText`/`resolveScopedNumber` (Task 4), `UiSolvedRect::item` (Task 5).
- Produces: `void paintUiScene(UiCanvas&, const registry&, const std::vector<UiSolvedRect>&, const UiDataSource*, UiTextureCache*)` — a new overload; the four-argument form stays and forwards with `nullptr`.

- [ ] **Step 1: Declare the overload**

In `engine/include/eng/ui/UiScene.h`, add `#include <eng/ui/UiTextureCache.h>` and, beside the existing `paintUiScene`:

```cpp
// The same, with a texture cache, so UiImage can draw. Painting without one
// draws every image as its missing-texture chip rather than crashing: a caller
// that forgot the cache should see that it forgot, immediately and on screen.
void paintUiScene(UiCanvas& canvas,
                  const entt::basic_registry<entt::entity>& registry,
                  const std::vector<UiSolvedRect>& solved,
                  const UiDataSource* data, UiTextureCache* textures);
```

- [ ] **Step 2: Route existing bindings through the scoped resolvers**

In `paintUiScene` in `engine/src/ecs/UiScene.cpp`, the loop needs the enclosing repeat's source for each solved rect. Add, before the loop:

```cpp
    // Which source each solved box's item index belongs to. Built once here
    // rather than searched per binding: a box inside a repeat needs its
    // repeat's source name to ask the data source for the item's keys, and
    // walking up the Parent chain per label would be quadratic on a full grid.
    const auto scopeOf = [&registry](entt::entity e) -> std::string_view {
        for (entt::entity walk = e; walk != entt::null;) {
            const ecs::Parent* p = registry.try_get<ecs::Parent>(walk);
            if (!p || p->value == entt::null || !registry.valid(p->value))
                return {};
            if (const UiRepeat* r = registry.try_get<UiRepeat>(p->value))
                return r->source;
            walk = p->value;
        }
        return {};
    };
```

Then replace the two binding lookups. `UiBar`:

```cpp
        if (const UiBar* bar = registry.try_get<UiBar>(e)) {
            float ratio = bar->ratio;
            resolveScopedNumber(data, scopeOf(e), item.item, bar->binding,
                                ratio);
            canvas.bar(at, size, std::clamp(ratio, 0.0f, 1.0f),
                       bar->useFillColour
                           ? packed(bar->fillColour)
                           : canvas.colour(toneOf(bar->fillTone)),
                       canvas.colour(toneOf(bar->trackTone)));
        }
```

`UiLabel`, replacing only its first two lines:

```cpp
            std::string value = label->text;
            resolveScopedText(data, scopeOf(e), item.item, label->binding,
                              value);
```

- [ ] **Step 3: Paint `UiImage`**

In the same loop, immediately **before** the `UiPanel` block — an image is chrome and other widgets draw over it:

```cpp
        if (const UiImage* img = registry.try_get<UiImage>(e)) {
            std::string path = img->texture;
            resolveScopedText(data, scopeOf(e), item.item, img->binding, path);
            const UiTexture& tex =
                textures && !path.empty() ? textures->get(path) : UiTexture{};
            const unsigned int base =
                img->useColour ? packed(img->colour)
                               : canvas.colour(toneOf(img->tone));
            // Opacity multiplies into the tint's alpha rather than being a
            // separate concept: one number reaching the draw call means a
            // half-transparent image cannot also be fully opaque.
            const unsigned int alpha = (unsigned int)std::clamp(
                int(std::lround(std::clamp(img->opacity, 0.0f, 1.0f) * 255.0f)),
                0, 255);
            const unsigned int tint = (base & 0x00FFFFFFu) | (alpha << 24);

            if (img->fit == int(ImageFit::NineSlice)) {
                canvas.imageSliced(at, size, tex, roundTo(img->borderMin),
                                   roundTo(img->borderMax), tint);
            } else {
                // Contain and Cover centre the fitted quad in the box, which
                // is the only placement that reads as intentional.
                const glm::ivec2 drawn = fitImage(size, tex.size, img->fit);
                const glm::ivec2 origin = at + (size - drawn) / 2;
                canvas.image(origin, drawn, tex, img->uvMin, img->uvMax, tint);
            }
        }
```

- [ ] **Step 4: Clip a scrollable repeat**

Still inside the loop, after the `UiImage` block:

```cpp
        // A scrollable repeat clips its cells to its own box. Pushed here and
        // popped when the last box belonging to it has been drawn -- tracked
        // with a small stack rather than recursion, because paint walks a flat
        // list and has no call stack to hang a scope on.
        if (const UiRepeat* rep = registry.try_get<UiRepeat>(e)) {
            if (rep->scrollable) {
                canvas.clipPush(at, size);
                clips.push_back({e, item.depth});
            }
        }
```

and before the loop:

```cpp
    // Open clip scopes: the repeat that pushed one, and the depth it sits at.
    // A box at or above that depth that is not a descendant ends the scope.
    struct Clip { entt::entity owner; int depth; };
    std::vector<Clip> clips;
```

and as the **first** statement inside the loop:

```cpp
        while (!clips.empty() && item.depth <= clips.back().depth) {
            canvas.clipPop();
            clips.pop_back();
        }
```

and after the loop:

```cpp
    while (!clips.empty()) {
        canvas.clipPop();
        clips.pop_back();
    }
```

- [ ] **Step 5: Split the function into the two overloads**

Rename the existing definition's parameter list to take `UiTextureCache* textures`, and add above it:

```cpp
void paintUiScene(UiCanvas& canvas,
                  const entt::basic_registry<entt::entity>& registry,
                  const std::vector<UiSolvedRect>& solved,
                  const UiDataSource* data)
{
    paintUiScene(canvas, registry, solved, data, nullptr);
}
```

Also update `drawUiScene` to solve with the data source it already has, so a repeat draws from the one call a game makes:

```cpp
void drawUiScene(UiCanvas& canvas,
                 const entt::basic_registry<entt::entity>& registry,
                 const UiDataSource* data)
{
    UiRect surface;
    surface.size = canvas.size();
    std::vector<UiSolvedRect> solved;
    solveUiLayout(registry, surface, data, solved);
    paintUiScene(canvas, registry, solved, data, nullptr);
}
```

Read the existing `drawUiScene` before replacing it and keep whatever it already does that is not shown here.

- [ ] **Step 6: Build and run every UI test**

Run: `cmake --build build --target ui_scene_tests ui_canvas_tests -j8 && ./build/ui_scene_tests && ./build/ui_canvas_tests`
Expected: both PASS, exit 0.

- [ ] **Step 7: Commit**

```bash
git add engine/include/eng/ui/UiScene.h engine/src/ecs/UiScene.cpp
git commit -m "feat(ui): paint UiImage, scoped bindings and clipped repeats

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 7: `UiInteraction` — hover, press, activate

**Files:**
- Create: `engine/include/eng/ui/UiInteraction.h`, `engine/src/ecs/UiInteraction.cpp`, `engine/tests/UiInteractionTests.cpp`
- Modify: `cmake/Engine.cmake`, `cmake/Tests.cmake`

**Interfaces:**
- Consumes: `UiSolvedRect` (with `item`), `UiDataSource`, `ecs::UiInteract`.
- Produces: `eng::ui::Pointer`, `eng::ui::Keys`, `eng::ui::UiInteraction` with `update(...) -> bool`, `events()`, `takeEvents()`, `state()`.

- [ ] **Step 1: Write the header**

```cpp
#pragma once
#include <eng/ui/UiScene.h>

#include <string>
#include <vector>

namespace eng::ui {

// Where the pointer is and what it is doing, as plain values.
//
// Values rather than an eng::Input& on purpose: the editor has to drive this
// same model from an imgui panel with no game input system behind it, and a
// headless test has neither. Anything that can fill in four fields can drive
// the whole interaction layer.
struct Pointer {
    glm::ivec2 position{-1, -1}; // virtual pixels; off-surface is fine
    bool down = false;           // held this frame
    bool pressed = false;        // went down this frame
    bool released = false;       // came up this frame
    float wheel = 0.0f;
};

// Directional and confirm input, already resolved from whatever bindings the
// application uses. Same reasoning as Pointer.
struct Keys {
    bool up = false, down = false, left = false, right = false;
    bool accept = false, back = false;
};

// Interaction state for one screen: what is hovered, held and focused.
//
// Deliberately NOT components. Which widget is focused is session state, and a
// screen that stored it could not be reloaded without losing the player's
// place -- the rule UiComponents.h already states for UiList.
class UiInteraction {
public:
    struct Event {
        enum class Kind { Activate, Drop, Change };
        Kind kind = Kind::Activate;
        entt::entity entity = entt::null;
        std::string action;
        std::string param;
        int item = -1;
        // Drop only: where the drag began.
        entt::entity source = entt::null;
        std::string sourceParam;
        // Change only: the committed value.
        std::string value;
    };

    struct State {
        // entt::null, never a default-constructed entity: entt::entity{} is
        // entity zero, which is a perfectly valid one, and a "nothing hovered"
        // that meant entity 0 would light up whatever that happens to be.
        entt::entity hovered = entt::null;
        entt::entity pressed = entt::null;
        entt::entity focused = entt::null;
        int hoveredItem = -1;
        int pressedItem = -1;
        int focusedItem = -1;
    };

    // Fold one frame of input into the state and emit events. Returns true
    // when the input was over the screen at all -- which is what stops a click
    // on a panel from also firing a weapon.
    bool update(const entt::basic_registry<entt::entity>& registry,
                const std::vector<UiSolvedRect>& solved,
                const UiDataSource* data, const Pointer& pointer,
                const Keys& keys);

    const State& state() const { return mState; }
    // Drains. A caller that reads events twice would act on each click twice,
    // and the second time is always the surprising one.
    std::vector<Event> takeEvents();
    void reset() { mState = {}; mEvents.clear(); }

private:
    State mState;
    std::vector<Event> mEvents;
};

// Is this entity interactive right now? Free, because paint needs the same
// answer to grey a disabled widget and must not duplicate the binding rule.
bool uiInteractEnabled(const ecs::UiInteract& interact, const UiDataSource* data,
                       std::string_view scope, int item);

// The topmost interactive box under a point, with its item index. entt::null
// when there is none.
struct UiInteractHit {
    entt::entity entity = entt::null;
    int item = -1;
};
UiInteractHit pickInteractive(const entt::basic_registry<entt::entity>& registry,
                              const std::vector<UiSolvedRect>& solved,
                              glm::ivec2 point);

} // namespace eng::ui
```

- [ ] **Step 2: Write the failing test**

`engine/tests/UiInteractionTests.cpp`:

```cpp
// The interaction model, headless.
//
// What is asserted here is the ordering nobody notices until it is wrong: a
// press that activates on release rather than on press, a release outside the
// pressed widget that activates nothing, and a disabled widget that swallows
// the click without acting on it.
#include <eng/ecs/Components.h>
#include <eng/ecs/components/UiComponents.h>
#include <eng/ui/UiInteraction.h>

#include <cstdio>

using namespace eng;
using namespace eng::ecs;

static int failures = 0;
static void check(bool c, const char* m)
{
    if (!c) { std::fprintf(stderr, "FAIL: %s\n", m); ++failures; }
}

namespace {

ui::UiRect surface()
{
    ui::UiRect r;
    r.size = {320, 240};
    return r;
}

// A box at (x,y) size (w,h) carrying an action.
entt::entity button(entt::registry& reg, int x, int y, int w, int h,
                    const char* action, bool enabled = true)
{
    const entt::entity e = reg.create();
    UiRect rect;
    rect.anchorMin = {0.0f, 0.0f};
    rect.anchorMax = {0.0f, 0.0f};
    rect.offsetMin = {float(x), float(y)};
    rect.offsetMax = {float(x + w), float(y + h)};
    reg.emplace<UiRect>(e, rect);
    UiInteract in;
    in.action = action;
    in.param = action;
    in.enabled = enabled;
    reg.emplace<UiInteract>(e, in);
    return e;
}

} // namespace

int main()
{
    // A click activates on RELEASE, inside the widget it was pressed on.
    {
        entt::registry reg;
        const entt::entity ok = button(reg, 10, 10, 40, 20, "ok");
        std::vector<ui::UiSolvedRect> solved;
        ui::solveUiLayout(reg, surface(), nullptr, solved);

        ui::UiInteraction in;
        ui::Pointer p;
        p.position = {20, 15};

        // Hover only.
        check(in.update(reg, solved, nullptr, p, {}), "the pointer is over it");
        check(in.state().hovered == ok, "hover is reported");
        check(in.takeEvents().empty(), "hovering activates nothing");

        // Press: held, still nothing fired.
        p.pressed = true; p.down = true;
        in.update(reg, solved, nullptr, p, {});
        check(in.state().pressed == ok, "press is held");
        check(in.takeEvents().empty(),
              "a press alone activates nothing -- release does");

        // Release inside: one Activate.
        p.pressed = false; p.released = true; p.down = false;
        in.update(reg, solved, nullptr, p, {});
        const std::vector<ui::UiInteraction::Event> ev = in.takeEvents();
        check(ev.size() == 1, "release inside activates exactly once");
        if (ev.size() == 1) {
            check(ev[0].kind == ui::UiInteraction::Event::Kind::Activate,
                  "and it is an Activate");
            check(ev[0].action == "ok", "carrying the authored action");
        }
        check(in.state().pressed == entt::null, "and the press is cleared");
    }

    // A release OUTSIDE the pressed widget activates nothing. This is the rule
    // that lets a player change their mind after pressing, and every UI has it.
    {
        entt::registry reg;
        button(reg, 10, 10, 40, 20, "ok");
        std::vector<ui::UiSolvedRect> solved;
        ui::solveUiLayout(reg, surface(), nullptr, solved);

        ui::UiInteraction in;
        ui::Pointer p;
        p.position = {20, 15};
        p.pressed = true; p.down = true;
        in.update(reg, solved, nullptr, p, {});
        in.takeEvents();

        p.position = {200, 200};   // dragged off
        p.pressed = false; p.released = true; p.down = false;
        in.update(reg, solved, nullptr, p, {});
        check(in.takeEvents().empty(), "release outside activates nothing");
    }

    // A disabled widget consumes the pointer but never fires.
    {
        entt::registry reg;
        button(reg, 10, 10, 40, 20, "locked", false);
        std::vector<ui::UiSolvedRect> solved;
        ui::solveUiLayout(reg, surface(), nullptr, solved);

        ui::UiInteraction in;
        ui::Pointer p;
        p.position = {20, 15};
        p.pressed = true; p.down = true;
        in.update(reg, solved, nullptr, p, {});
        p.pressed = false; p.released = true; p.down = false;
        const bool over = in.update(reg, solved, nullptr, p, {});
        check(over, "a disabled widget still consumes the pointer");
        check(in.takeEvents().empty(), "but activates nothing");
        check(in.state().hovered == entt::null,
              "and does not report itself hovered");
    }

    // Spatial focus navigation. A 2x2 grid: right from top-left must reach
    // top-right, not the entity that happens to be next in creation order.
    {
        entt::registry reg;
        const entt::entity tl = button(reg, 0, 0, 20, 20, "tl");
        const entt::entity bl = button(reg, 0, 40, 20, 20, "bl");
        const entt::entity tr = button(reg, 60, 0, 20, 20, "tr");
        std::vector<ui::UiSolvedRect> solved;
        ui::solveUiLayout(reg, surface(), nullptr, solved);

        ui::UiInteraction in;
        ui::Pointer p; // pointer parked off-surface
        ui::Keys k;

        // First directional press takes focus rather than moving it: a screen
        // opened with nothing focused must not skip its first widget.
        k.down = true;
        in.update(reg, solved, nullptr, p, k);
        check(in.state().focused == tl, "the first key focuses the first widget");

        k = {}; k.right = true;
        in.update(reg, solved, nullptr, p, k);
        check(in.state().focused == tr, "right reaches the box to the right");

        k = {}; k.down = true;
        in.update(reg, solved, nullptr, p, k);
        check(in.state().focused == tr,
              "down from the top-right finds nothing below and stays put");

        // Accept fires the focused widget.
        in.takeEvents();
        k = {}; k.accept = true;
        in.update(reg, solved, nullptr, p, k);
        const std::vector<ui::UiInteraction::Event> ev = in.takeEvents();
        check(ev.size() == 1 && ev[0].action == "tr",
              "accept activates the focused widget");
        (void)bl;
    }

    if (failures == 0)
        std::printf("all interaction tests passed\n");
    return failures == 0 ? 0 : 1;
}
```

- [ ] **Step 3: Register the test and the source**

In `cmake/Engine.cmake`, in the `eng_framework` source list beside `engine/src/ecs/UiScene.cpp`:

```cmake
                       # Session state for an authored screen: hover, press,
                       # focus. In src/ecs for the same reason UiScene.cpp is
                       # -- it reads the registry.
                       engine/src/ecs/UiInteraction.cpp
```

In `cmake/Tests.cmake`, after the `ui_scene` block:

```cmake
  eng_add_test(ui_interaction
    SOURCES engine/tests/UiInteractionTests.cpp
    INCLUDES engine/include third_party
    LIBS eng_framework glm::glm EnTT::EnTT)
```

- [ ] **Step 4: Run it and watch it fail**

Run: `cmake -S . -B build && cmake --build build --target ui_interaction_tests -j8`
Expected: FAIL to link — `eng::ui::UiInteraction::update` is undefined.

- [ ] **Step 5: Implement**

`engine/src/ecs/UiInteraction.cpp`:

```cpp
#include <eng/ui/UiInteraction.h>

#include <eng/ecs/Components.h>
#include <eng/ecs/components/UiComponents.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace eng::ui {
namespace {

using ecs::UiInteract;
using ecs::UiRepeat;

// The repeat source enclosing an entity, for scoped enabledBinding lookups.
std::string_view scopeOf(const entt::basic_registry<entt::entity>& registry,
                         entt::entity e)
{
    for (entt::entity walk = e; walk != entt::null;) {
        const ecs::Parent* p = registry.try_get<ecs::Parent>(walk);
        if (!p || p->value == entt::null || !registry.valid(p->value))
            return {};
        if (const UiRepeat* r = registry.try_get<UiRepeat>(p->value))
            return r->source;
        walk = p->value;
    }
    return {};
}

} // namespace

bool uiInteractEnabled(const UiInteract& interact, const UiDataSource* data,
                       std::string_view scope, int item)
{
    if (interact.enabledBinding.empty())
        return interact.enabled;
    float value = interact.enabled ? 1.0f : 0.0f;
    // Unanswered leaves the authored flag standing, like every other binding.
    resolveScopedNumber(data, scope, item, interact.enabledBinding, value);
    return value != 0.0f;
}

UiInteractHit pickInteractive(const entt::basic_registry<entt::entity>& registry,
                              const std::vector<UiSolvedRect>& solved,
                              glm::ivec2 point)
{
    // Backwards: the list is in paint order and the topmost box is the one the
    // player sees under the cursor.
    for (auto it = solved.rbegin(); it != solved.rend(); ++it) {
        if (!registry.valid(it->entity) ||
            !registry.all_of<UiInteract>(it->entity))
            continue;
        if (it->bounds.contains(point))
            return {it->entity, it->item};
    }
    return {};
}

bool UiInteraction::update(const entt::basic_registry<entt::entity>& registry,
                           const std::vector<UiSolvedRect>& solved,
                           const UiDataSource* data, const Pointer& pointer,
                           const Keys& keys)
{
    const auto enabledAt = [&](entt::entity e, int item) {
        const UiInteract* in = registry.try_get<UiInteract>(e);
        return in && uiInteractEnabled(*in, data, scopeOf(registry, e), item);
    };
    const auto emit = [&](entt::entity e, int item) {
        const UiInteract* in = registry.try_get<UiInteract>(e);
        if (!in || in->action.empty())
            return;
        Event ev;
        ev.kind = Event::Kind::Activate;
        ev.entity = e;
        ev.action = in->action;
        ev.param = in->param;
        ev.item = item;
        mEvents.push_back(std::move(ev));
    };

    // --- pointer ---------------------------------------------------------
    const UiInteractHit hit =
        pickInteractive(registry, solved, pointer.position);
    const bool overAnything =
        hit.entity != entt::null ||
        pickUi(solved, pointer.position) != entt::null;

    const bool hitEnabled =
        hit.entity != entt::null && enabledAt(hit.entity, hit.item);
    mState.hovered = hitEnabled ? hit.entity : entt::null;
    mState.hoveredItem = hitEnabled ? hit.item : -1;

    if (pointer.pressed && hitEnabled) {
        mState.pressed = hit.entity;
        mState.pressedItem = hit.item;
        // Clicking also focuses, so the keyboard picks up where the mouse
        // left off rather than jumping back to wherever it was.
        mState.focused = hit.entity;
        mState.focusedItem = hit.item;
    }
    if (pointer.released) {
        // Only a release *on the widget that was pressed* activates. That is
        // what lets a player press, think better of it, drag off and let go.
        if (mState.pressed != entt::null && mState.pressed == hit.entity &&
            mState.pressedItem == hit.item && hitEnabled)
            emit(hit.entity, hit.item);
        mState.pressed = entt::null;
        mState.pressedItem = -1;
    }
    if (!pointer.down && !pointer.released) {
        mState.pressed = entt::null;
        mState.pressedItem = -1;
    }

    // --- keyboard --------------------------------------------------------
    const bool moving = keys.up || keys.down || keys.left || keys.right;
    if (moving) {
        // Focusable boxes, with their centres. Rebuilt per press rather than
        // cached: the solved list changes whenever a repeat's data does, and a
        // cached ring would navigate to cells that no longer exist.
        struct Candidate { entt::entity e; int item; glm::ivec2 centre; };
        std::vector<Candidate> candidates;
        for (const UiSolvedRect& s : solved) {
            const UiInteract* in = registry.try_get<UiInteract>(s.entity);
            if (!in || !in->focusable ||
                !uiInteractEnabled(*in, data, scopeOf(registry, s.entity),
                                   s.item))
                continue;
            candidates.push_back(
                {s.entity, s.item, s.bounds.position + s.bounds.size / 2});
        }

        if (!candidates.empty()) {
            const auto current = std::find_if(
                candidates.begin(), candidates.end(), [&](const Candidate& c) {
                    return c.e == mState.focused && c.item == mState.focusedItem;
                });
            if (current == candidates.end()) {
                // Nothing focused yet: the first directional press takes focus
                // rather than moving it, or a screen would skip its first
                // widget every time it opened.
                mState.focused = candidates.front().e;
                mState.focusedItem = candidates.front().item;
            } else {
                const glm::ivec2 from = current->centre;
                const glm::ivec2 dir{keys.right ? 1 : keys.left ? -1 : 0,
                                     keys.down ? 1 : keys.up ? -1 : 0};
                // Nearest in the pressed direction, scored so that along-axis
                // distance dominates and off-axis drift only breaks ties.
                // Index order is useless here: a 6x8 grid navigated by index
                // wraps rows on every press.
                long long best = std::numeric_limits<long long>::max();
                const Candidate* pick = nullptr;
                for (const Candidate& c : candidates) {
                    if (c.e == current->e && c.item == current->item)
                        continue;
                    const glm::ivec2 d = c.centre - from;
                    const int along = d.x * dir.x + d.y * dir.y;
                    if (along <= 0)
                        continue; // behind, or perpendicular
                    const int off = dir.x != 0 ? std::abs(d.y) : std::abs(d.x);
                    const long long score =
                        (long long)along + 4LL * (long long)off;
                    if (score < best) {
                        best = score;
                        pick = &c;
                    }
                }
                if (pick) {
                    mState.focused = pick->e;
                    mState.focusedItem = pick->item;
                }
            }
        }
    }

    if (keys.accept && mState.focused != entt::null &&
        enabledAt(mState.focused, mState.focusedItem))
        emit(mState.focused, mState.focusedItem);

    return overAnything || moving || keys.accept;
}

std::vector<UiInteraction::Event> UiInteraction::takeEvents()
{
    std::vector<Event> out;
    out.swap(mEvents);
    return out;
}

} // namespace eng::ui
```

- [ ] **Step 6: Run the test and watch it pass**

Run: `cmake --build build --target ui_interaction_tests -j8 && ./build/ui_interaction_tests`
Expected: `all interaction tests passed`, exit 0.

- [ ] **Step 7: Commit**

```bash
git add engine/include/eng/ui/UiInteraction.h engine/src/ecs/UiInteraction.cpp engine/tests/UiInteractionTests.cpp cmake/Engine.cmake cmake/Tests.cmake
git commit -m "feat(ui): UiInteraction -- hover, press, activate, spatial focus

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 8: Paint the interaction state

**Files:**
- Modify: `engine/include/eng/ui/UiScene.h`, `engine/src/ecs/UiScene.cpp`

**Interfaces:**
- Consumes: `UiInteraction::State` (Task 7).
- Produces: `void paintUiScene(UiCanvas&, const registry&, const std::vector<UiSolvedRect>&, const UiDataSource*, UiTextureCache*, const UiInteraction::State*)`. The four- and five-argument forms stay and forward with `nullptr`.

- [ ] **Step 1: Declare it**

In `engine/include/eng/ui/UiScene.h`, forward-declare rather than include (`UiInteraction.h` includes this header, so including it back is a cycle):

```cpp
class UiInteraction;
```

and beside the other overloads:

```cpp
// The same, drawing hover, press and disabled feedback.
//
// A wash over whatever the entity already paints, rather than a per-widget
// variant: that is what makes a plate, an icon and a repeat cell all give the
// same feedback with one code path, and what stops "add a hover state" being a
// renderer change every time a new widget appears.
void paintUiScene(UiCanvas& canvas,
                  const entt::basic_registry<entt::entity>& registry,
                  const std::vector<UiSolvedRect>& solved,
                  const UiDataSource* data, UiTextureCache* textures,
                  const void* interactionState);
```

`const void*` avoids the include cycle at the cost of a cast in one place. Prefer moving `State` into its own tiny header (`eng/ui/UiInteractionState.h`) if that reads better once you see both files — the cast is the thing to avoid, not the include.

- [ ] **Step 2: Paint the wash**

In the paint loop, as the last thing drawn for each box:

```cpp
        if (const UiInteract* in = registry.try_get<UiInteract>(e)) {
            const bool live =
                uiInteractEnabled(*in, data, scopeOf(e), item.item);
            unsigned int wash = 0;
            if (!live) {
                wash = (canvas.colour(toneOf(in->disabledTone)) & 0x00FFFFFFu) |
                       0x60000000u;
            } else if (st && st->pressed == e && st->pressedItem == item.item) {
                wash = (canvas.colour(toneOf(in->pressTone)) & 0x00FFFFFFu) |
                       0x70000000u;
            } else if (st && st->hovered == e && st->hoveredItem == item.item) {
                wash = (canvas.colour(toneOf(in->hoverTone)) & 0x00FFFFFFu) |
                       0x38000000u;
            }
            if (wash)
                canvas.rect(at, size, wash);
            // Focus is a border, not a wash: a focused widget the pointer is
            // also over must still read as focused, and two washes would just
            // add up into one brighter one that says neither.
            if (st && st->focused == e && st->focusedItem == item.item)
                canvas.border(at, size, canvas.colour(UiTone::Focus));
        }
```

with `const UiInteraction::State* st = static_cast<const UiInteraction::State*>(interactionState);` before the loop.

- [ ] **Step 3: Add the forwarding overloads**

```cpp
void paintUiScene(UiCanvas& canvas,
                  const entt::basic_registry<entt::entity>& registry,
                  const std::vector<UiSolvedRect>& solved,
                  const UiDataSource* data, UiTextureCache* textures)
{
    paintUiScene(canvas, registry, solved, data, textures, nullptr);
}
```

and keep the existing four-argument one forwarding to this.

- [ ] **Step 4: Build**

Run: `cmake --build build --target ui_scene_tests -j8 && ./build/ui_scene_tests`
Expected: PASS. No new test here — the wash is pixels, and Task 12's screenshot is what checks it.

- [ ] **Step 5: Commit**

```bash
git add engine/include/eng/ui/UiScene.h engine/src/ecs/UiScene.cpp
git commit -m "feat(ui): draw hover, press, disabled and focus feedback

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 9: Editor — the 2D viewport shows the new components

**Files:**
- Modify: `editor/src/ui/UiSceneEditor.cpp`, `editor/include/editor/ui/UiSceneEditor.h`, `editor/src/app/EditorApp.cpp` (around `6211`–`6320`)
- Test: `editor/tests/UiSceneEditorTests.cpp`

**Interfaces:**
- Consumes: everything above.
- Produces: `UiSceneEditor::rebuild` copies `UiImage`/`UiInteract`/`UiRepeat` into its preview registry; `UiSceneEditor::setMockData(const UiDataSource*)`; handles are offered only on repeat item 0.

- [ ] **Step 1: Write the failing test**

In `editor/tests/UiSceneEditorTests.cpp`:

```cpp
    // A repeat's template resolves to many boxes and exactly one of them may
    // carry handles. Dragging instance 3 must edit the card, not instance 3 --
    // there is no such thing as instance 3 in the document.
    {
        SceneDocument doc;
        Entity list;
        list.id = "shelf";
        list.ui.emplace();
        list.ui->rect.anchorMin = {0.0f, 0.0f};
        list.ui->rect.anchorMax = {0.0f, 0.0f};
        list.ui->rect.offsetMin = {0.0f, 0.0f};
        list.ui->rect.offsetMax = {100.0f, 200.0f};
        list.ui->repeat.emplace();
        list.ui->repeat->source = "trade.stock";
        list.ui->repeat->direction = 0;
        list.ui->repeat->cell = {100.0f, 20.0f};
        list.ui->repeat->gap = {0.0f, 0.0f};
        doc.entities.push_back(list);

        Entity card;
        card.id = "card";
        card.parent = "shelf";
        card.ui.emplace();
        card.ui->rect.anchorMin = {0.0f, 0.0f};
        card.ui->rect.anchorMax = {1.0f, 1.0f};
        doc.entities.push_back(card);

        ed::UiSceneEditor editor;
        TwoItemMock mock;
        editor.setMockData(&mock);
        editor.rebuild(doc, {320, 240}, "");

        // Item 0's box is pickable and names the template.
        check(editor.pick({50, 10}) == AuthorId{"card"},
              "picking a repeat cell selects the template");
        // Item 1's box picks the template too -- same author id.
        check(editor.pick({50, 30}) == AuthorId{"card"},
              "every cell picks the same template");

        // But handles are offered only on item 0, so a drag has one meaning.
        eng::ui::UiRect bounds;
        check(editor.boundsOf(AuthorId{"card"}, bounds),
              "the template has bounds");
        check(bounds.position == glm::ivec2(0, 0),
              "and they are item 0's, not the last cell's");
    }
```

Add a `TwoItemMock : eng::ui::UiDataSource` in that file's anonymous namespace returning `itemCount("trade.stock") == 2`.

Read `editor/include/editor/content/SceneDocument.h` first and match the real field names for the `ui` sub-struct — `list.ui->repeat` is the name this task adds and the others must match what is already there.

- [ ] **Step 2: Add the three components to the authored `ui` struct**

In `editor/include/editor/content/SceneDocument.h`, beside the existing `std::optional<eng::ecs::UiList> list;`:

```cpp
    std::optional<eng::ecs::UiImage> image;
    std::optional<eng::ecs::UiInteract> interact;
    std::optional<eng::ecs::UiRepeat> repeat;
```

and teach the JSON reader/writer the three new keys, following exactly what `list` already does in `editor/src/content/SceneSource.cpp`. Read that file's `list` handling and mirror it field for field — the six existing components are the template.

- [ ] **Step 3: Copy them in `rebuild`**

In `editor/src/ui/UiSceneEditor.cpp`, in the first loop beside the other `emplace` calls:

```cpp
        if (authored.ui->image)
            mRegistry.emplace<eng::ecs::UiImage>(e, *authored.ui->image);
        if (authored.ui->interact)
            mRegistry.emplace<eng::ecs::UiInteract>(e, *authored.ui->interact);
        if (authored.ui->repeat)
            mRegistry.emplace<eng::ecs::UiRepeat>(e, *authored.ui->repeat);
```

and pass the mock source to the solver:

```cpp
    eng::ui::solveUiLayout(mRegistry, surface, mMock, mSolved);
```

with a `const eng::ui::UiDataSource* mMock = nullptr;` member and `void setMockData(const eng::ui::UiDataSource* d) { mMock = d; }`.

- [ ] **Step 4: Offer handles only on item 0**

In the `mByAuthor` build loop:

```cpp
    for (const eng::ui::UiSolvedRect& s : mSolved) {
        // Instances past the first are pickable but carry no handles: they are
        // the same authored entity, and a drag on instance 3 must edit the
        // template. `item <= 0` covers both "not in a repeat" (-1) and "the
        // first cell" (0).
        const auto found = back.find(s.entity);
        if (found != back.end() && found->second)
            mByAuthor.push_back({found->second->id, s.bounds, s.item});
    }
```

with `int item = -1;` added to `struct Solved`, `boundsOf` returning the first entry whose `item <= 0`, and `pick` unchanged (any instance selects the template).

- [ ] **Step 5: Run the test**

Run: `cmake --build build --target editor_ui_scene_tests -j8 && ./build/editor_ui_scene_tests`
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add editor/ engine/
git commit -m "feat(editor): author UiImage, UiInteract and UiRepeat in the 2D viewport

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 10: The cooker carries the new components

**Files:**
- Modify: whichever file implements `SceneCook`'s `ui` grouping (find with `grep -rn "uiChild\|authored.ui" editor/src game/src tools`)
- Test: the existing cook/asset tests

**Interfaces:**
- Produces: a `.scn` with `ui.image` / `ui.interact` / `ui.repeat` cooks to a `.map` carrying components 44–46, with the parent link preserved by the existing `uiChild` exemption.

- [ ] **Step 1: Find every place the six UI components are enumerated**

Run: `grep -rn "UiList" --include=*.cpp --include=*.h editor game engine tools | grep -v build`
Expected: a handful of sites — the field tables (done), the editor document (done), the cooker, and the inspector grouping. Each one that lists all six needs all nine.

- [ ] **Step 2: Extend each site**

For every site the grep found that enumerates the six, add the three. Do not invent a loop over the registry to replace the enumeration — the sites differ in what they do with each component, and a loop would hide that.

- [ ] **Step 3: Cook an existing screen and diff**

Run: `make cook SCENE=assets/scenes/ui/inventory.scn && git diff --stat assets/scenes/ui/inventory.map`
Expected: no change. The three new components are optional and absent from that scene, so a cook that alters it means the enumeration change broke something.

- [ ] **Step 4: Commit**

```bash
git add -A
git commit -m "feat(content): cook UiImage, UiInteract and UiRepeat

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 11: One build, one test run

This is the single build. Per the repo's own rule, do not build between the earlier tasks.

- [ ] **Step 1: Regenerate and build**

Run: `cmake -S . -B build && cmake --build build -j8`
Expected: success. If `internal compiler error: Bus error` appears, that is memory pressure, not a corrupt tree — rebuild the failing target with `-j1`.

- [ ] **Step 2: Run the suite**

Run: `make test`
Expected: green, **except** `fps_controller` and `scene_template`, which already fail at HEAD and are not yours. Confirm that by checking them out at the merge base if in doubt.

- [ ] **Step 3: Check layering held**

Run: `ctest --test-dir build -R layering --output-on-failure`
Expected: PASS. `engine/src/ui/UiTextureCache.cpp` must not have pulled in the ECS.

- [ ] **Step 4: Confirm the world image is untouched**

Run: `make visual-test`
Expected: green. Nothing in this plan touches a shader, material or the compositor; a failure here means something reached the world render path and must be reverted, not rebaselined.

---

### Task 12: A screen that proves it, on screen

The repo's rule is that a change which compiles is not a change that works, and every milestone here is visual.

**Files:**
- Create: `assets/scenes/ui/interaction_probe.scn`, a mock data source behind `RAVEN_OPEN_SCREEN=probe`

- [ ] **Step 1: Author the probe screen**

A `.scn` with: a nine-sliced `UiImage` panel background, a `UiRepeat` over a source with six items laid out as a 3-column grid, each cell a card with a `UiImage` icon bound to `icon`, a `UiLabel` bound to `name`, a `UiBar` bound to `condition` and a `UiInteract` with action `probe`; plus one disabled `UiInteract` button so the disabled wash is visible.

- [ ] **Step 2: Answer its bindings**

Add a temporary source in `game/src/ui/GameUiData.cpp` returning six fixed items, so the probe needs no RPG state. Mark it clearly as a probe fixture.

- [ ] **Step 3: Capture and read the PNG**

Run: `RAVEN_OPEN_SCREEN=probe make screenshot SHOT=/tmp/ui-probe.png FRAME=200`
Then **read `/tmp/ui-probe.png`**. Check, specifically: six cards in a 3×2 grid (not one, not stacked at the corner), icons drawn at the right aspect, the nine-slice corners unstretched, and the disabled button visibly greyed.

A black frame is usually an unfocused window, not a regression — confirm against a known-good binary before chasing it.

- [ ] **Step 4: Commit**

```bash
git add assets/scenes/ui/interaction_probe.scn game/src/ui/GameUiData.cpp
git commit -m "test(ui): a probe screen exercising images, repeats and interaction

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 13: Documentation

**Files:**
- Modify: `docs/ui-scenes.md`
- Create: `docs/ui-interaction.md`

- [ ] **Step 1: Update `docs/ui-scenes.md`**

Extend the component table with `UiImage`, `UiInteract` and `UiRepeat`. Add a **Repeat scope** section stating the three rules verbatim: the first child is the template, a bare binding asks the item, a `/`-prefixed binding is global. Add a nine-slice example. Correct the "What this deliberately is not" section — `UiList` is still a list widget, but the sentence claiming no repeater exists is now false and must say that `UiRepeat` is the repeater and `UiList` is kept for plain text lists.

- [ ] **Step 2: Write `docs/ui-interaction.md`**

Cover: why interaction state is not a component; `Pointer`/`Keys` as values and what that buys (editor and tests drive the same model); activate-on-release-inside; the disabled rule; spatial focus navigation and why not index order; the event kinds; and what the engine deliberately does not know (what an action means).

- [ ] **Step 3: Commit**

```bash
git add docs/ui-scenes.md docs/ui-interaction.md
git commit -m "docs: images, repeats and the interaction model

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

## Self-Review

**Spec coverage (M1–M3 only; M4–M7 are out of scope for this plan):**

| Spec item | Task |
|---|---|
| `UiTextureCache`, nearest+clamp, cached failures | 1 |
| `UiCanvas::image`, `imageSliced`, clipping | 2 |
| `UiImage` component with fit modes and two-Vec2 borders | 3, 6 |
| `UiInteract` component | 3 |
| `UiRepeat` component | 3 |
| Item-scoped `UiDataSource` + `/` escape | 4 |
| `UiSolvedRect::item`, repeat expansion, grid wrap | 5 |
| Image paint, scoped binding paint, scroll clipping | 6 |
| `UiInteraction`, events, spatial focus | 7 |
| Hover/press/disabled/focus paint | 8 |
| Editor authoring + handles on item 0 | 9 |
| Cooker | 10 |
| Tests, layering, visual-test | 11 |
| Screenshot verification | 12 |
| Docs | 13 |

**Deliberately deferred to later plans:** `UiScreenStack` and modals, the Lua `ui` module and `ProjectApp` integration, drag-and-drop, `UiTextField`, scroll *input* (Task 6 clips; the wheel offset needs the screen stack to own it), and all M6 editor tooling. The spec's `UiScreenStack` is listed under M2 but depends on nothing in this plan and everything in M4, so it moves there.

**Known gap this plan creates and the next one must close:** `UiRepeat::scrollable` clips but nothing scrolls yet, because the scroll offset is session state and this plan ships no owner for it. `UiInteract::draggable`/`dropTarget` are likewise inert. Both are carried in the component now so screens authored against this plan survive the next one unchanged.
