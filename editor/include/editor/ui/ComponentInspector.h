#pragma once
#include <editor/scene/EntityComponents.h>
#include <editor/content/GridMath.h>
#include <editor/content/KitCatalog.h>
#include <editor/assets/MaterialCatalog.h>

#include <string>
#include <vector>

namespace ed {

// The widget half of the component table.
//
// Split from EntityComponents so that half stays ImGui-free and testable: the
// registry answers "what does this entity have, and how do I add or drop one",
// this answers "what does one look like". They are joined by ComponentType::id,
// which means adding a component type is two entries in two tables and no
// change to any panel.
struct InspectorContext {
    const game::content::KitCatalog* catalog = nullptr;
    const game::content::GridConfig* grid = nullptr;
    // Material ids offered as an override on a mesh. Owned by the editor.
    const std::vector<std::string>* materialNames = nullptr;
    // Effect names from the particle library, so the Particles component picks
    // from what exists instead of taking a string nobody validates.
    const std::vector<std::string>* particleEffects = nullptr;
    // The vocabularies the game itself defines: enemy ids from enemies.toml,
    // pickup ids, and the marker prefixes the runtime looks up.
    //
    // These were free-text fields, which meant the editor let you author
    // "gobling" and told you nothing -- the misspelling surfaced as an enemy
    // that never spawned, in the game, some minutes later. An id you can only
    // get right by having read a TOML is not authorable.
    const std::vector<std::string>* enemyIds = nullptr;
    const std::vector<std::string>* pickupIds = nullptr;
    // What the classified catalogue says about the material offered here, and
    // what this entity's mesh can take. Null leaves the combo unfiltered, which
    // is what it always was.
    const std::vector<MaterialInfo>* materials = nullptr;
    MeshKind meshKind = MeshKind::Unknown;

    // Set by the drawers, read by the caller: `edited` means the document must
    // be touched so the preview follows the drag, `closed` means the widget was
    // released and the edit should close as one undo entry.
    bool edited = false;
    bool closed = false;

    void track(bool itemEdited, bool itemClosed)
    {
        edited = edited || itemEdited;
        closed = closed || itemClosed;
    }
};

// id, name and transform: what every entity has, drawn above the components.
void drawEntityIdentity(game::content::Entity& entity,
                        InspectorContext& context);

// One component's fields, with no header -- the caller owns the collapsing
// header and the remove button, so the layout is decided in one place.
void drawComponentBody(const ComponentType& type, game::content::Entity& entity,
                       InspectorContext& context);

} // namespace ed
