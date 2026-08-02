#pragma once
#include <editor/content/GridMath.h>
#include <editor/content/KitCatalog.h>
#include <editor/content/SceneDocument.h>

#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace game::content {

enum class Severity { Error, Warning, Info };

// What the editor offers to do about an issue. The editor wraps the fix in an
// undoable command; the fix itself lives here so the CLI could apply it too.
enum class QuickFix {
    None,
    RemoveEntity,
    AddPlayerSpawn,
    SetDefaultRange,
    SetDefaultHalfExtents,
    SnapToCell,
    ResetTransform,
    FillCornerGap,
    // Detaches an entity whose parent is missing, itself, or above it in a
    // loop. Deleting it would be the other option and it is the wrong one: the
    // entity is fine, only the link is broken.
    ClearParent,
    // Gives an entity the component its kit piece says it needs. The scene
    // already has the right mesh in the right place; only the component that
    // drives it is missing, so replacing the entity would be the wrong fix.
    AddPortalComponent,
};

struct Issue {
    Severity severity = Severity::Warning;
    // Stable, greppable, and what the tests assert on -- the human message is
    // free to be reworded.
    std::string code;
    std::string message;
    AuthorId entity; // empty when the issue is the document's, not an entity's
    QuickFix fix = QuickFix::None;
    // Where the problem is in the world, for fixes that need a location: a
    // corner gap is *between* two entities, not on either of them.
    glm::vec3 position{0.0f};
};

// Everything wrong with a scene that needs the kit or the rest of the document
// to notice. Structural JSON problems are the loader's job and already failed
// the load; these are the ones a scene can *have* while still opening, which is
// the point: a broken scene must be fixable in the editor, not just rejected.
//
// `assetRoot` enables the on-disk mesh check; pass empty to skip it.
std::vector<Issue> validate(const SceneDocument& document,
                            const KitCatalog& catalog,
                            const std::string& assetRoot = {});

// True when any issue is an Error. The cooker refuses these: shipping a level
// with an unresolved prefab is the silent-hole failure the pipeline design
// explicitly forbids.
bool blocksCook(const std::vector<Issue>& issues);

// Applies a quick fix in place. Returns false when the fix no longer applies
// (the entity was deleted, someone already fixed it).
bool applyQuickFix(SceneDocument& document, const KitCatalog& catalog,
                   const Issue& issue);

const char* severityName(Severity severity);

} // namespace game::content
