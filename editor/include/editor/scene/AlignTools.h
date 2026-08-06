#pragma once
#include <editor/content/SceneDocument.h>

#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace ed::align {

// Object placement and alignment aids. Gregory §15.4.1.7: "Many world editors
// provide a host of object placement and alignment aids in addition to the
// basic translation, rotation and scale tools ... Examples include snap to
// grid, snap to terrain, align to object and many more."
//
// Snap to grid this editor already has, in the work plane and the placement
// tools. This file is the rest, and all of it is arithmetic on world positions
// -- which is why it lives apart from the app and is checked by a headless
// test rather than by eye. Every one of these is the kind of operation whose
// failure mode is "everything is now in one place", and noticing that in a
// screenshot after the fact is too late.

enum class Axis { X = 0, Y = 1, Z = 2 };

// Which edge of the selection's spread the entities line up on.
//
// Min/Max work on the entities' BOUNDS rather than their origins, because "line
// these crates up against that wall" is a statement about where their sides
// are, and a crate's origin is wherever the artist left it. Centre works on
// bounds too, for the same reason.
enum class Mode { Min, Centre, Max };

// One entity's world placement, as the caller resolved it. Passed in rather
// than looked up, because bounds come from the kit catalogue and the mesh
// cache, and neither belongs in a file of arithmetic.
struct Placement {
    game::content::AuthorId id;
    glm::vec3 position{0.0f}; // world origin
    glm::vec3 boundsMin{0.0f};
    glm::vec3 boundsMax{0.0f};
};

// A world-space move for one entity. The caller converts it back into an
// authored local transform, which is what keeps a parented entity where it is
// put -- see game::content::localFromWorld.
struct Move {
    game::content::AuthorId id;
    glm::vec3 position{0.0f};
};

// Lines the selection up on one axis. Entities already in place are still
// returned, so the caller can decide whether an empty-effect command is worth
// an undo entry; `changed` on the result says whether anything actually moved.
std::vector<Move> alignTo(const std::vector<Placement>& placements, Axis axis,
                          Mode mode);

// Spreads the selection evenly between its two extremes along `axis`, keeping
// the outermost two where they are. Fewer than three entities is a no-op:
// there is nothing between two things to distribute.
//
// Gaps are equalised between CENTRES, not between facing edges. Centres are
// what an author means by "evenly spaced" for a row of pillars of the same
// size, and edge-gaps for mixed sizes are a different tool (and a rabbit hole
// of what to do when one object is larger than the gap).
std::vector<Move> distribute(const std::vector<Placement>& placements,
                             Axis axis);

// Drops each entity straight down until its underside rests on `floorY`, a
// height the caller found by casting a ray down from it. One entry per
// placement whose floor was found; the caller skips the rest rather than
// dropping them to y=0, because "there was nothing under it" and "there was
// ground at zero" are different answers.
struct Drop {
    game::content::AuthorId id;
    float floorY = 0.0f;
};

std::vector<Move> dropTo(const std::vector<Placement>& placements,
                         const std::vector<Drop>& floors);

// The name the undo entry and the status line get. One table so the menu, the
// palette and the history cannot disagree about what a verb is called.
std::string label(Axis axis, Mode mode);
const char* axisName(Axis axis);

} // namespace ed::align
