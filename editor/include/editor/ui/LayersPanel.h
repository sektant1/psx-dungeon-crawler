#pragma once
#include <editor/scene/Layers.h>

#include <functional>
#include <string>
#include <vector>

namespace ed {

// The Layers panel: one row per layer, with the eye, the padlock, the solo and
// the count. Gregory §15.4.1.5.
//
// Split from EditorApp for the reason OutlinerPanel is: the row is where the
// interesting bugs live -- which widget a click is attributed to, whether the
// row itself is still clickable once four icon buttons sit on it -- and those
// are only catchable by a headless test that presses the mouse over a row.
//
// The panel owns no state and no document. It is handed rows and callbacks, so
// the same drawing code serves the real editor and a test with a fake session.

// What the panel asks the editor to do. Every one may be null; a null callback
// leaves its control out rather than drawing a dead one.
struct LayerActions {
    // Visibility, lock and solo, straight through to the session.
    std::function<bool(const std::string&)> isHidden;
    std::function<void(const std::string&, bool)> setHidden;
    std::function<bool(const std::string&)> isLocked;
    std::function<void(const std::string&, bool)> setLocked;
    std::function<bool(const std::string&)> isSolo;
    std::function<void(const std::string&)> toggleSolo;

    // Which layer new entities land in. Exactly one row is active, and clicking
    // a row makes it so -- the same gesture as picking a paint layer in an
    // image editor, which is where the muscle memory comes from.
    std::function<bool(const std::string&)> isActive;
    std::function<void(const std::string&)> setActive;

    // Select everything in the layer. The panel's answer to "what IS in here",
    // and the fastest route to a multi-selection an author actually means.
    std::function<void(const std::string&)> selectMembers;
    // Move the current selection into this layer, as one undo entry.
    std::function<void(const std::string&)> assignSelection;
    // Rename and recolour, applied on commit rather than per keystroke so the
    // undo stack gets one entry per edit.
    std::function<void(const std::string&, const std::string& name)> rename;
    std::function<void(const std::string&, const glm::vec3&)> recolour;
    // Remove the layer itself. Its entities are not deleted -- they fall back
    // to the default layer -- because a layer is an organisation of a level and
    // deleting the organisation must never delete the level.
    std::function<void(const std::string&)> removeLayer;
    std::function<void()> addLayer;

    // Per-layer save and load, the chapter's division-of-labour story.
    std::function<void(const std::string&)> exportLayer;
    std::function<void(const std::string&)> importLayer;

    // How many entities the current selection has, so the assign control can
    // say what it will move rather than being a verb with no object.
    std::size_t selectionCount = 0;
};

// Draws the rows into the current window. `rows` is what ed::layers::stats
// returned this frame.
void drawLayerRows(const std::vector<layers::LayerStat>& rows,
                   const LayerActions& actions);

} // namespace ed
