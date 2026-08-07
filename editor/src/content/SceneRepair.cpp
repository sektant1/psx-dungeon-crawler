#include <editor/content/SceneRepair.h>

#include <glm/glm.hpp>

#include <ios>
#include <map>
#include <set>
#include <sstream>
#include <utility>

namespace game::content {

CellRepairReport repairCellRecords(SceneDocument& document,
                                   const KitCatalog& catalog,
                                   const GridConfig& grid, float tolerance)
{
    CellRepairReport report;
    for (Entity& entity : document.entities) {
        if (!entity.cell || entity.prefab.empty()) {
            continue; // nothing recorded, so nothing to disagree with
        }
        const KitPiece* piece = catalog.find(entity.prefab);
        if (!piece) {
            // An unresolved prefab is a different problem with a different
            // fix, and guessing a cell for a piece whose footprint is unknown
            // would be inventing data.
            ++report.untouched;
            continue;
        }

        const XformAuthor derived =
            placementToTransform(grid, catalog, *piece, *entity.cell);
        if (glm::length(derived.position - entity.transform.position) <=
            tolerance) {
            ++report.untouched;
            continue;
        }

        // The record disagrees. Ask the grid where this piece actually is, and
        // only believe the answer if it derives back to where the piece stands
        // -- transformToPlacement is documented as best-effort and ambiguous,
        // so a cell that does not round-trip is a guess, not a repair.
        CellPlacement rebased;
        if (transformToPlacement(grid, catalog, *piece, entity.transform,
                                 rebased)) {
            const XformAuthor check =
                placementToTransform(grid, catalog, *piece, rebased);
            if (glm::length(check.position - entity.transform.position) <=
                tolerance) {
                entity.cell = rebased;
                ++report.rebased;
                continue;
            }
        }

        // Nothing on the grid describes where this piece is: it was placed
        // freely and the record is stale. Dropping it is the honest outcome --
        // the piece keeps its position, and the checks that address entities by
        // cell stop making claims about one it is not in.
        entity.cell.reset();
        ++report.detached;
    }
    if (report.changed() != 0)
        document.touch();
    return report;
}

DuplicateReport removeDuplicatePlacements(SceneDocument& document)
{
    DuplicateReport report;

    // Anything with children stays, whatever else is true of it: the child
    // names its parent by id, and keeping a different member of the group would
    // leave that child pointing at an entity that no longer exists.
    std::set<AuthorId> hasChildren;
    for (const Entity& entity : document.entities)
        if (!entity.parent.empty())
            hasChildren.insert(entity.parent);

    const auto key = [](const Entity& e) {
        std::ostringstream out;
        out.setf(std::ios::fixed);
        out.precision(4);
        out << e.parent << '|' << e.prefab << '|' << e.material << '|'
            << e.transform.position.x << ',' << e.transform.position.y << ','
            << e.transform.position.z << '|' << e.transform.rotationDegrees.x
            << ',' << e.transform.rotationDegrees.y << ','
            << e.transform.rotationDegrees.z << '|' << e.transform.scale.x
            << ',' << e.transform.scale.y << ',' << e.transform.scale.z;
        return out.str();
    };

    std::map<std::string, int> seen;
    std::vector<Entity> kept;
    kept.reserve(document.entities.size());
    for (const Entity& entity : document.entities) {
        // Only prefab-backed scenery is deduplicated. A marker, a light or a
        // spawn carries meaning beyond its mesh, and two of them in one place
        // is a question for an author rather than a redundancy.
        const bool dedupable =
            !entity.prefab.empty() && hasChildren.count(entity.id) == 0 &&
            !entity.playerSpawn && !entity.marker && !entity.enemySpawn &&
            !entity.pickup && !entity.light && !entity.camera &&
            !entity.trigger && !entity.portal && !entity.exitYawDegrees &&
            entity.scripts.empty();
        if (!dedupable) {
            kept.push_back(entity);
            continue;
        }
        const int count = ++seen[key(entity)];
        if (count == 1) {
            kept.push_back(entity);
            continue;
        }
        if (count == 2)
            ++report.groups;
        ++report.removed;
    }

    if (report.removed != 0) {
        document.entities = std::move(kept);
        document.touch();
    }
    return report;
}

bool safeToApplyInBulk(QuickFix fix)
{
    switch (fix) {
    case QuickFix::FillCornerGap:
    case QuickFix::ClearParent:
    case QuickFix::AddPortalComponent:
    case QuickFix::SetDefaultRange:
    case QuickFix::SetDefaultHalfExtents:
        return true;
    // Named rather than defaulted, so a fix added later has to be classified
    // here before it can be applied in bulk. A new destructive fix silently
    // inheriting "safe" is the one failure mode this split exists to prevent.
    case QuickFix::RemoveEntity:
    case QuickFix::SnapToCell:
    case QuickFix::ResetTransform:
    case QuickFix::AddPlayerSpawn:
    // The scene contract's fixes. Each one decides what kind of scene this is
    // -- what you look through, what you hear from, what lights it -- and the
    // view fixes *swap* an existing camera rather than adding to it. That is an
    // authoring choice made one at a time from the contract panel, never
    // something a bulk repair pass should make on someone's behalf.
    case QuickFix::AddFirstPersonView:
    case QuickFix::AddThirdPersonView:
    case QuickFix::AddShotCamera:
    case QuickFix::AddAudioListener:
    case QuickFix::AddKeyLight:
    case QuickFix::None:
        return false;
    }
    return false;
}

SafeFixReport applySafeQuickFixes(SceneDocument& document,
                                  const KitCatalog& catalog,
                                  const std::string& assetRoot, int maxPasses)
{
    SafeFixReport report;
    for (int pass = 0; pass < maxPasses; ++pass) {
        const std::vector<Issue> issues =
            validate(document, catalog, assetRoot);
        std::size_t appliedThisPass = 0;
        for (const Issue& issue : issues) {
            if (!safeToApplyInBulk(issue.fix))
                continue;
            // Re-validated every pass, so an issue whose fix no longer applies
            // -- because an earlier fix in this same pass already covered it --
            // simply returns false and is picked up again next round if it is
            // still real.
            if (applyQuickFix(document, catalog, issue)) {
                ++report.applied;
                ++appliedThisPass;
            }
        }
        if (appliedThisPass == 0)
            break; // settled
    }

    const std::vector<Issue> left = validate(document, catalog, assetRoot);
    for (const Issue& issue : left) {
        if (safeToApplyInBulk(issue.fix))
            ++report.skipped; // did not settle; reported rather than retried
        else
            ++report.remaining;
    }
    return report;
}

} // namespace game::content
