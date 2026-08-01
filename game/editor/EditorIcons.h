#pragma once

struct ImDrawList;
struct ImVec2;

namespace ed {

// The editor's icons, drawn rather than typed.
//
// The panels labelled everything in words: a row in the outliner spent seven
// characters on "prop" before it got to the entity's name, and the toolbar was
// a line of text buttons. That is what a debug panel looks like, not what a
// level editor looks like -- and at the widths these panels dock to, the words
// were eating the names they were describing.
//
// Drawn with ImDrawList instead of a glyph font because the alternative is
// shipping an icon font and an atlas rebuild for a dozen shapes. These are
// primitives -- circles, boxes, a couple of polylines -- so they stay sharp at
// any size, tint per row, and add no asset the pipeline has to know about.
enum class Icon {
    Cube,        // geometry
    Light,       // point light
    Sun,         // directional light
    Camera,      // a point of view
    Spawn,       // player start
    Exit,        // level exit
    Enemy,
    Pickup,
    Trigger,     // event volume
    Marker,
    Group,       // an empty node other things hang from
    Collider,
    Eye,         // visible
    EyeClosed,   // hidden
    Lock,
    Unlock,
    Missing,     // an unresolved prefab
};

// Draws `icon` centred in a `size` box whose top-left is `at`. `rgba` is
// ImGui's packed colour.
void drawIcon(ImDrawList* list, Icon icon, ImVec2 at, float size, unsigned rgba);

// The icon a kind tag names. Falls back to Cube, which is what an entity with
// a mesh and nothing else is.
Icon iconForKind(const char* kind);

// An icon-only button, `size` square, with `tooltip` on hover. Returns true on
// click. `active` draws it lit rather than dimmed, which is how a toggle says
// what it currently is.
bool iconButton(Icon icon, const char* id, const char* tooltip,
                bool active = true, float size = 0.0f);

// The same, drawn flat with no frame: for the per-row toggles in a list, where
// a button frame on every line is heavier than the list itself.
bool iconToggle(Icon icon, const char* id, bool active, const char* tooltip,
                float size = 0.0f);

} // namespace ed
