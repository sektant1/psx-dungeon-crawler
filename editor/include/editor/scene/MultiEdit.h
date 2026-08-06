#pragma once
#include <editor/content/SceneDocument.h>

#include <optional>
#include <string>
#include <vector>

namespace ed::multiedit {

// Editing a multi-object selection through a one-object property grid.
// Gregory §15.4.1.6.
//
// The chapter describes a grid that shows an amalgam of the selection and
// writes an edited value back to all of it. The inspector here draws ONE
// entity -- the primary -- because a hundred hand-written component drawers
// cannot each learn to be N-valued. What makes that equivalent is this file:
// when the edit closes, whatever field the author actually moved is applied to
// every other selected entity that can take it, and every field they did not
// touch is left alone.
//
// Field-level, never component-level. Copying the whole component across would
// mean dragging one light's range also stamped the primary's colour on the
// other thirty-nine -- an edit nobody asked for, from a panel that looked like
// it was editing one number.

// The subset of an entity this fans out. Deliberately not "everything": the
// fields here are the ones that are meaningful to hold in common across a
// heterogeneous selection, which is exactly the chapter's test. An id, a name
// or a parent is per-entity by nature and never fans out.
//
// Reported so the caller can label the undo entry and say what it did.
struct Change {
    // Author-facing, for the status line and the undo label: "position.x",
    // "light range".
    std::string field;
    // How many other entities it reached. Zero means the selection had nobody
    // else it applied to, which is not a failure -- a heterogeneous selection
    // is expected to absorb only some edits.
    std::size_t applied = 0;
};

// Applies to `target` whatever differs between `before` and `after`, field by
// field, and returns true if anything moved.
//
// `target` is another selected entity, NOT the one that was edited: the primary
// already holds `after`. A field only lands where it is meaningful -- a
// material needs a surface to override, a light field needs the target to be a
// light -- so a mixed selection of walls and lights takes the transform edit
// and quietly ignores the rest.
bool applyDelta(const game::content::Entity& before,
                const game::content::Entity& after,
                game::content::Entity& target);

// What changed between the two, as author-facing names. Used for the undo
// label and the status line; empty when the edit touched nothing that fans out
// (a rename, a parent change), in which case the caller leaves the edit alone.
std::vector<std::string> changedFields(const game::content::Entity& before,
                                       const game::content::Entity& after);

// --- mixed values -----------------------------------------------------------
//
// The other half of the chapter's property grid: "If a particular attribute has
// the same value across all objects in the selection, the value is shown as-is
// ... If the attribute's value differs from object to object, the property grid
// typically shows no value at all."
//
// A one-entity drawer cannot render that on its own, so the panel asks these
// what the selection agrees about and dims or blanks the rows that disagree.

// Per-axis agreement across a set of entities, for the three transform rows.
// Each flag is true when every entity in the set holds the same value on that
// axis. A selection of one always agrees with itself.
struct TransformAgreement {
    bool position[3] = {true, true, true};
    bool rotation[3] = {true, true, true};
    bool scale[3] = {true, true, true};

    bool allAgree() const;
};

TransformAgreement
agreementOf(const std::vector<const game::content::Entity*>& entities);

} // namespace ed::multiedit
