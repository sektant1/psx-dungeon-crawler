#pragma once
#include "EntityComponents.h"
#include "KitCatalog.h"

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
    // Material ids offered as an override on a mesh. Owned by the editor.
    const std::vector<std::string>* materialNames = nullptr;

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
