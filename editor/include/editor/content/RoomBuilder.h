#pragma once
#include <editor/content/GridMath.h>
#include <editor/content/KitCatalog.h>
#include <editor/content/SceneDocument.h>

#include <string>
#include <vector>

namespace game::content {

// Builds a whole room from a rectangle of cells: floor, perimeter walls, and
// the corner posts that make the walls actually meet.
//
// This exists because placing a room one piece at a time is both slow and
// wrong. A wall is a cell wide and sits entirely outside the boundary it faces,
// so two perpendicular runs stop against each other and leave a hole the width
// of the wall at every convex corner -- every time, in every room, unless
// something fills it. Making the room the unit of authoring means the corner is
// handled once, here, instead of being a mistake each author gets to rediscover.
struct RoomSpec {
    int col0 = 0, row0 = 0; // one corner cell, inclusive
    int col1 = 0, row1 = 0; // the opposite corner cell, inclusive
    float level = 0.0f;     // floor height in metres

    bool floor = true;
    bool walls = true;
    bool corners = true; // the posts that close the wall joins
    // The kit has no ceiling piece, so a ceiling is the floor slab again at
    // wall height in a two-sided material -- the same trick the procedural
    // generator uses (see LayoutToScene). On by default: a room without one is
    // open to a black sky, which is what every cooked scene looked like.
    bool ceiling = true;

    // Which pieces to use. Defaults are the plain kit; an author who wants a
    // skull-walled crypt changes these rather than editing every wall after.
    std::string floorPrefab = "kit.floor";
    std::string wallPrefab = "kit.wall";
    std::string cornerPrefab = "kit.pillar";
    // The ceiling is the floor mesh seen from below, so it needs the two-sided
    // variant of the atlas or it renders as nothing at all.
    std::string ceilingMaterial = "Game/Kit/DungeonTwoSided";

    // Cell range, normalised so col0 <= col1 regardless of drag direction.
    int minCol() const { return col0 < col1 ? col0 : col1; }
    int maxCol() const { return col0 < col1 ? col1 : col0; }
    int minRow() const { return row0 < row1 ? row0 : row1; }
    int maxRow() const { return row0 < row1 ? row1 : row0; }
    int width() const { return maxCol() - minCol() + 1; }
    int depth() const { return maxRow() - minRow() + 1; }
};

// The entities a RoomSpec expands to, in a stable order (floors, ceilings,
// walls, then corners). Ids are allocated against `document` but nothing is added to
// it: the caller wraps them in one undoable command.
std::vector<Entity> buildRoom(const GridConfig& grid, const KitCatalog& catalog,
                              const RoomSpec& spec, const SceneDocument& document,
                              std::string& error);

// Where a corner post goes for a room's outside corner: on the outer corner of
// the wall bands, so it covers the notch between the two runs it joins.
glm::vec3 cornerPostPosition(const GridConfig& grid, int col, int row,
                             bool east, bool south);

// The Y scale that makes a piece exactly `metres` tall, for pieces whose
// authored height is not the wall height (the pillar is half again as tall).
// Returns 1 when the piece already matches or has no height.
float scaleToHeight(const KitPiece& piece, float scale, float metres);

} // namespace game::content
