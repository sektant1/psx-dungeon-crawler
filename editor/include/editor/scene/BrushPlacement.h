#pragma once
#include <editor/scene/DocumentRaycast.h>
#include <editor/scene/EntityComponents.h>
#include <editor/content/GridMath.h>

namespace ed {

// Everything the placement rule needs, gathered by the caller.
//
// The surface is passed in rather than raycast here so the same hit can drive
// the ghost, the commit and the status readout without three traversals of the
// document -- and so the rule can be tested by handing it a hit, with no scene.
struct PlacementQuery {
    Ray ray;
    float workPlaneLevel = 0.0f;
    bool snapXZ = true;
    float step = 1.0f;
    // Ctrl: ignore geometry entirely and use the work plane. The escape hatch
    // for placing something *inside* the volume of what is already there.
    bool forceWorkPlane = false;
    DocumentHit surface;
    // Where the ghost sits when the cursor points above the horizon and the
    // work plane is behind the camera.
    float fallbackDistance = 12.0f;
    // Metres to lift a brush that carries no kit piece, so the thing rests ON
    // the surface instead of centring its ORIGIN on it.
    //
    // A kit piece states this in kit.toml (`pivot` / KitPiece::yOffsetKit) and
    // has always had it applied. A raw mesh or a primitive has no piece, so it
    // used to get nothing -- and any mesh authored around its centre, which is
    // most of what a modelling package exports, was placed half through the
    // floor. The caller fills this from the mesh's own local bounds; for a mesh
    // already authored with its base at Y=0 it is zero and nothing changes.
    float baseOffset = 0.0f;
};

struct Placement {
    bool valid = false;
    game::content::CellPlacement cell;
    game::content::XformAuthor transform;
    // True when the height came from geometry rather than the work plane.
    // The ghost tints on this, so the author can tell the two apart before
    // committing rather than after.
    bool onSurface = false;
};

// Height comes from what the cursor is over; the work plane is only the answer
// when the cursor is over nothing.
//
// That is the whole point of this function. The work plane used to be the sole
// source of height, which meant a barrel on a table was: raise the plane, guess
// the number, place, check, adjust. Two rules, because the two cases genuinely
// differ:
//
//   architecture (Floor, Wall, Fill, Opening) stacks on the TOP of whatever was
//   pointed at, so pointing anywhere on a wall's face still lands the next wall
//   squarely on top of it rather than partway down the side;
//
//   dressing (Prop, and the meshless gameplay entities) lands exactly where the
//   cursor touched, so a torch goes where you point it on a wall face.
//
// XZ still comes from the grid in both cases -- the surface decides height, not
// footprint, which keeps every kit piece on the cells the runtime can represent.
Placement resolvePlacement(const game::content::GridConfig& grid,
                           const game::content::KitCatalog& catalog,
                           const Brush& brush, const PlacementQuery& query);

} // namespace ed
