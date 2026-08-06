#include <editor/scene/AlignTools.h>

#include <algorithm>
#include <cmath>

namespace ed::align {
namespace {

int index(Axis axis)
{
    return int(axis);
}

// The value being lined up, for one entity on one axis. Bounds rather than the
// origin, for the reason Mode documents: a crate's origin is wherever the
// artist left it, and "against that wall" is a statement about its side.
float edgeOf(const Placement& placement, Axis axis, Mode mode)
{
    const int i = index(axis);
    switch (mode) {
    case Mode::Min:
        return placement.boundsMin[i];
    case Mode::Max:
        return placement.boundsMax[i];
    case Mode::Centre:
        break;
    }
    return (placement.boundsMin[i] + placement.boundsMax[i]) * 0.5f;
}

} // namespace

const char* axisName(Axis axis)
{
    switch (axis) {
    case Axis::X: return "X";
    case Axis::Y: return "Y";
    case Axis::Z: return "Z";
    }
    return "?";
}

std::string label(Axis axis, Mode mode)
{
    const char* which = "centre";
    switch (mode) {
    case Mode::Min: which = "min"; break;
    case Mode::Max: which = "max"; break;
    case Mode::Centre: break;
    }
    return std::string("align ") + which + " " + axisName(axis);
}

std::vector<Move> alignTo(const std::vector<Placement>& placements, Axis axis,
                          Mode mode)
{
    std::vector<Move> moves;
    if (placements.size() < 2)
        return moves; // one entity is already aligned with itself

    const int i = index(axis);

    // The line everything moves to. Min aligns to the lowest edge in the
    // selection, Max to the highest, Centre to the midpoint of the whole
    // spread -- so the operation is idempotent and never depends on which
    // entity happens to be primary.
    float target = edgeOf(placements.front(), axis, mode);
    if (mode == Mode::Centre) {
        float low = placements.front().boundsMin[i];
        float high = placements.front().boundsMax[i];
        for (const Placement& placement : placements) {
            low = std::min(low, placement.boundsMin[i]);
            high = std::max(high, placement.boundsMax[i]);
        }
        target = (low + high) * 0.5f;
    } else {
        for (const Placement& placement : placements) {
            const float edge = edgeOf(placement, axis, mode);
            target = mode == Mode::Min ? std::min(target, edge)
                                       : std::max(target, edge);
        }
    }

    for (const Placement& placement : placements) {
        // Moved by the delta between its own edge and the target, not set to
        // the target outright: the entity keeps the offset between its origin
        // and its bounds, which is what stops an aligned row of props all
        // jumping to sit on their pivots.
        const float delta = target - edgeOf(placement, axis, mode);
        Move move;
        move.id = placement.id;
        move.position = placement.position;
        move.position[i] += delta;
        moves.push_back(move);
    }
    return moves;
}

std::vector<Move> distribute(const std::vector<Placement>& placements, Axis axis)
{
    std::vector<Move> moves;
    if (placements.size() < 3)
        return moves; // nothing lies between two things

    const int i = index(axis);

    // Sorted by centre, so the result does not depend on selection order --
    // an author who rubber-banded a row and one who ctrl-clicked it in a
    // different order must get the same spacing.
    std::vector<const Placement*> sorted;
    sorted.reserve(placements.size());
    for (const Placement& placement : placements)
        sorted.push_back(&placement);
    std::sort(sorted.begin(), sorted.end(),
              [i](const Placement* a, const Placement* b) {
                  const float ca = (a->boundsMin[i] + a->boundsMax[i]) * 0.5f;
                  const float cb = (b->boundsMin[i] + b->boundsMax[i]) * 0.5f;
                  if (ca != cb)
                      return ca < cb;
                  return a->id < b->id; // stable for coincident entities
              });

    const auto centreOf = [i](const Placement& placement) {
        return (placement.boundsMin[i] + placement.boundsMax[i]) * 0.5f;
    };
    const float first = centreOf(*sorted.front());
    const float last = centreOf(*sorted.back());
    const float step =
        (last - first) / float(sorted.size() - 1); // the two ends stay put

    for (std::size_t at = 0; at < sorted.size(); ++at) {
        const Placement& placement = *sorted[at];
        const float wanted = first + step * float(at);
        Move move;
        move.id = placement.id;
        move.position = placement.position;
        move.position[i] += wanted - centreOf(placement);
        moves.push_back(move);
    }
    return moves;
}

std::vector<Move> dropTo(const std::vector<Placement>& placements,
                         const std::vector<Drop>& floors)
{
    std::vector<Move> moves;
    for (const Placement& placement : placements) {
        const Drop* floor = nullptr;
        for (const Drop& candidate : floors)
            if (candidate.id == placement.id)
                floor = &candidate;
        if (!floor)
            continue; // nothing was under it; leave it where it is

        // The underside goes to the floor, and the origin follows by the same
        // delta -- so a prop whose pivot is at its base and one whose pivot is
        // at its centre both end up resting on the surface.
        Move move;
        move.id = placement.id;
        move.position = placement.position;
        move.position.y += floor->floorY - placement.boundsMin.y;
        moves.push_back(move);
    }
    return moves;
}

} // namespace ed::align
