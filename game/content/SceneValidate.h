#pragma once
#include "GridMath.h"
#include "KitCatalog.h"
#include "SceneDocument.h"

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
};

struct Issue {
    Severity severity = Severity::Warning;
    // Stable, greppable, and what the tests assert on -- the human message is
    // free to be reworded.
    std::string code;
    std::string message;
    AuthorId entity; // empty when the issue is the document's, not an entity's
    QuickFix fix = QuickFix::None;
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
