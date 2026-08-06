#pragma once
#include <editor/content/SceneDocument.h>
#include <editor/scene/Picker.h>

#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace ed::selection {

// Selection, past "click the nearest thing". Gregory §15.4.1.4.
//
// The chapter names three problems a densely populated world creates, and this
// file is the answer to all three:
//
//   "Objects might be selected via a rubber-band box in the orthographic view"
//       -> marquee, below. Screen-rectangle against projected bounds, so it
//          works in the perspective view too.
//
//   "the editor might allow the user to cycle through all of the objects that
//    the ray is currently intersecting rather than always selecting the
//    nearest one"
//       -> cycling, below. The alternative the chapter offers -- hide the
//          thing in the way and try again -- is a loop this replaces.
//
//   "Some world editors also allow selections to be named and saved for later
//    retrieval"
//       -> SelectionSet, below.

// --- rubber band ------------------------------------------------------------

// A screen rectangle, in the same viewport pixel space the picker uses. Built
// from the drag's two corners in either order, so dragging up-left works.
struct ScreenRect {
    glm::vec2 min{0.0f};
    glm::vec2 max{0.0f};

    static ScreenRect fromCorners(glm::vec2 a, glm::vec2 b);
    bool contains(glm::vec2 point) const;
    // Drags shorter than a few pixels are clicks with a shaky hand, not
    // marquees. The caller uses this to decide which gesture just happened.
    bool degenerate(float minimumPixels = 4.0f) const;
};

// One candidate for the rubber band: an entity and its world bounds, resolved
// by the caller (bounds come from the kit catalogue, which does not belong in
// a file of screen-space maths).
struct Candidate {
    game::content::AuthorId id;
    glm::vec3 boundsMin{0.0f};
    glm::vec3 boundsMax{0.0f};
};

// How much of an entity has to be inside the rectangle.
//
// Touch is what an author expects from a quick sweep across a row of props;
// Enclose is what they expect when they carefully box one room and do not want
// the corridor behind it. Both exist in every 3D tool, usually bound to the
// drag direction; here the caller chooses.
enum class Fit {
    Touch,   // any part of the projected bounds overlaps
    Enclose, // the whole projected footprint is inside
};

// Every candidate whose projected bounds meet the rectangle, in candidate
// order. Entities entirely behind the camera never match.
//
// The bounds are projected as their eight corners and reduced to a screen-space
// box. That over-estimates a rotated box's footprint, which is the right error
// to make: a marquee that misses something the author clearly dragged across is
// far more annoying than one that catches a neighbour, and the outliner shows
// what was caught.
std::vector<game::content::AuthorId>
marquee(const std::vector<Candidate>& candidates, const glm::mat4& viewProjection,
        glm::vec2 viewportOrigin, glm::vec2 viewportSize, const ScreenRect& rect,
        Fit fit = Fit::Touch);

// --- click cycling ----------------------------------------------------------

// Remembers where the last pick happened and how deep into the stack it went,
// so a repeated click at the same spot walks down through what is under the
// cursor instead of selecting the nearest thing forever.
//
// State rather than a pure function because "the same spot" is a fact about the
// previous click, and depth has to survive between them.
class PickCycle
{
public:
    // The id to select for a click at `point` given everything the ray hit,
    // nearest first. Empty when nothing was hit.
    //
    // A click that lands more than `radius` pixels from the last one, or on a
    // different set of hits, starts again at the nearest -- so cycling never
    // surprises somebody who has moved on to a different part of the level.
    game::content::AuthorId next(const std::vector<game::content::AuthorId>& hits,
                                 glm::vec2 point, float radius = 6.0f);

    // Forget the cycle. Called when the selection changes from anywhere else,
    // so the next viewport click starts from the top.
    void reset();

    // How deep the last click went, for the status line: telling the author
    // "2 of 4 under the cursor" is what makes the gesture discoverable.
    std::size_t depth() const { return mDepth; }
    std::size_t count() const { return mHits.size(); }

private:
    std::vector<game::content::AuthorId> mHits;
    glm::vec2 mPoint{0.0f};
    std::size_t mDepth = 0;
    bool mHasPoint = false;
};

// --- named selections -------------------------------------------------------

// A selection somebody kept. Session state, in the sidecar: the members are
// entity ids, which are document data, but "the six lights I keep coming back
// to" is one author's working set and not something the other two people
// editing this chunk should inherit.
struct SelectionSet {
    std::string name;
    std::vector<game::content::AuthorId> members;
};

// Saves under `name`, replacing any set with that name. An empty selection is
// not saved: a named set that restores nothing is a trap.
void save(std::vector<SelectionSet>& sets, const std::string& name,
          const std::vector<game::content::AuthorId>& members);

// The members of `name`, minus any entity the document no longer has -- a saved
// set outlives the entities in it, and restoring a ghost would select nothing
// while claiming to have selected six things.
std::vector<game::content::AuthorId>
restore(const std::vector<SelectionSet>& sets, const std::string& name,
        const game::content::SceneDocument& document);

void remove(std::vector<SelectionSet>& sets, const std::string& name);

} // namespace ed::selection
