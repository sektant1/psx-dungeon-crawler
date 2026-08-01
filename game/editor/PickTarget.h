#pragma once
#include "SceneDocument.h"

#include <vector>

namespace ed {

// Which entity a viewport click *means*, when the thing under the cursor is one
// mesh of a composed object.
//
// A composed object is one thing to the author -- that is the entire reason for
// parenting a chandelier's candles to it -- but the ray hits whichever mesh is
// in front, which is a candle. Selecting the candle and then dragging pulls one
// candle out of the chandelier, and the author only finds out by looking at the
// result. So a click takes the whole object, and going inside is deliberate:
//
//   click                  the outermost ancestor: the object
//   Alt+click              exactly what the ray hit, however deep
//   click again, inside    once the object is selected, clicks drill into it
//
// The third rule is what makes adjusting one candle two clicks rather than a
// modifier nobody remembers, and it is how Unity, Godot and Unreal all behave.
//
// Pure and free-standing so it can be tested without an editor: the rule is
// about a document and a selection, and getting it wrong is invisible until
// somebody drags.
game::content::AuthorId
resolvePickTarget(const game::content::SceneDocument& document,
                  const game::content::AuthorId& hit,
                  const std::vector<game::content::AuthorId>& selection,
                  bool exact);

// The outermost ancestor of `id`, or `id` itself when it has no parent.
// Terminates on a malformed document: a parent cycle is reported by validate(),
// and must not hang the editor that would be used to fix it.
game::content::AuthorId rootOf(const game::content::SceneDocument& document,
                               const game::content::AuthorId& id);

} // namespace ed
