#pragma once
#include <editor/content/GridMath.h>
#include <editor/content/KitCatalog.h>
#include <editor/content/SceneDocument.h>

#include <editor/content/SceneValidate.h>

#include <cstddef>
#include <string>

namespace game::content {

// Mechanical repairs to a scene's grid bookkeeping.
//
// A `cell` is a *record* of grid intent, and the transform is derived from it.
// Move a piece freely -- with the gizmo, or by hand in the file -- and the two
// disagree: the transform is now the truth and the cell is a lie about where
// the piece was meant to be. The validator reports that (`cell.transform_drift`),
// and the drifted records then poison two further checks, because both address
// entities by their recorded cell rather than by where they are:
//
//   cell.overlap      two pieces claim one slot they are not actually both in
//   cell.wall_orphan  a wall's recorded cell has no floor, though it stands on
//                     one
//
// So one root cause shows up as three kinds of warning and, on a scene that has
// been hand-edited for a while, as a hundred of them. Clicking Snap-to-cell a
// hundred times is not the fix: that moves the geometry to match the lie, and
// where two records collide it stacks pieces on top of each other.
//
// This goes the other way. The position is kept exactly; only the record is
// rewritten to describe where the piece actually is.
struct CellRepairReport {
    std::size_t rebased = 0;  // cell recomputed from the transform
    std::size_t detached = 0; // genuinely off-grid: the record was dropped
    std::size_t untouched = 0;

    std::size_t changed() const { return rebased + detached; }
};

// Rewrites every drifted `cell` to match its entity's transform.
//
// Per entity, in order:
//   * the record already agrees with the transform  -> untouched
//   * transformToPlacement finds a cell that derives back to this transform
//     (within `tolerance` metres)                   -> rebased
//   * nothing on the grid describes where it is     -> detached, `cell` cleared
//
// **No entity's transform is modified.** That is the property that makes this
// safe to run on a shipped level without looking at it afterwards, and
// `scene_repair_tests` asserts it.
CellRepairReport repairCellRecords(SceneDocument& document,
                                   const KitCatalog& catalog,
                                   const GridConfig& grid,
                                   float tolerance = 0.01f);

// Which quick fixes are safe to apply without a human looking at the result.
//
// The validator's fixes are not equal in kind. Some restore bookkeeping or add
// the thing that is missing; others delete an entity or move one. Applying the
// second sort in bulk is how a "fix all" button eats a level -- `wall_orphan`
// alone would have deleted thirty-six walls out of cozy_lair, every one of them
// a wall the author put there on purpose, reported only because a stale cell
// record pointed at the wrong slot.
//
// So the split is explicit and conservative:
//
//   safe    FillCornerGap        adds a pillar into a visible hole
//           ClearParent          drops a link that is already broken
//           AddPortalComponent   adds the component the kit piece declares
//           SetDefaultRange      fills in a number that must be positive
//           SetDefaultHalfExtents
//
//   not     RemoveEntity         deletes authored geometry
//           SnapToCell           moves a piece to match a record
//           ResetTransform       discards an authored placement
//           AddPlayerSpawn       invents gameplay content
//
// The unsafe ones stay one-click-at-a-time in the Issues panel, where the
// author can see what each is about to do.
bool safeToApplyInBulk(QuickFix fix);

struct SafeFixReport {
    std::size_t applied = 0;
    std::size_t skipped = 0; // fixable, but not safe in bulk
    std::size_t remaining = 0;
};

// Applies every safe fix the validator reports, re-validating between passes
// because one fix can change what the next sees (filling a corner adds an
// entity the overlap check then reads). Bounded, so a fix that fails to settle
// cannot spin.
SafeFixReport applySafeQuickFixes(SceneDocument& document,
                                  const KitCatalog& catalog,
                                  const std::string& assetRoot = {},
                                  int maxPasses = 8);

} // namespace game::content
