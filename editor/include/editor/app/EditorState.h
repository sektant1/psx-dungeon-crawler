#pragma once
#include <editor/viewport/EditorCamera.h>
#include <editor/scene/EntityComponents.h>
#include <editor/scene/Layers.h>
#include <editor/content/GridMath.h>
#include <editor/content/RoomBuilder.h>
#include <editor/content/KitCatalog.h>
#include <editor/content/SceneDocument.h>

#include <cstdint>
#include <string>
#include <vector>

namespace ed {

// Which tool the pointer is driving. TrenchBroom's model: one modal tool at a
// time, switched with a single key, rather than a mode buried in a menu.
enum class Tool { Select, Place, Room };

// The grid the editor snaps to. Kit pieces always land on a whole cell or a
// cell edge; the subdivision below only applies to free objects (props, lights,
// markers), which is what keeps a half-cell wall -- something the runtime
// cannot represent -- from ever being authored.
struct GridState {
    // Index into kSubdivisions: how the cell is divided for free placement.
    int subdivision = 0;
    bool snap = true;
    float level = 0.0f; // work-plane height in metres
    // Whether the viewport hides everything above the work plane. Off by
    // default: `level` is a placement/snap control, and having it silently
    // decide which storeys are VISIBLE meant a multi-storey scene could only
    // ever be seen one floor at a time. The cut still exists because a ceiling
    // becomes a lid over a top-down view, but it is now something the author
    // asks for rather than a side effect of choosing a work plane.
    bool cutAboveLevel = false;

    static constexpr float kSubdivisions[] = {4.0f, 2.0f, 1.0f, 0.5f};
    static constexpr int kSubdivisionCount = 4;

    float step() const { return kSubdivisions[subdivision]; }
    // Clamped, not wrapped. These used to be modular, so pressing "finer" at
    // the finest step silently jumped back to the coarsest -- the author's next
    // placement then landed metres from where the last one did, and nothing on
    // screen had visibly changed except a number they were not looking at.
    void finer()
    {
        if (subdivision + 1 < kSubdivisionCount)
            ++subdivision;
    }
    void coarser()
    {
        if (subdivision > 0)
            --subdivision;
    }
};

// What the 3D view is showing. Three answers, so it is an enum rather than the
// two independent booleans it would otherwise become -- and two booleans
// deciding what the viewport draws is how they end up both true.
//
//   Level     the whole scene, which is every session's starting state
//   Isolate   one entity and its descendants, alone on the grid
//   Material  the shipped material stage: one sphere, one material
enum class ViewportMode { Level, Isolate, Material };

// Which entity the viewport is isolated on, and what to put back on leaving.
//
// A view of the open document, never a copy of it: the entity is not extracted
// and no second file is opened, so undo, save and cook are unaffected and an
// edit made here is the same edit made in the level.
struct IsolationState {
    // Empty when not isolated. The mode enum is the authority; this is which.
    game::content::AuthorId root;
    // The root and every descendant, rebuilt when the document revision moves.
    // Cached because it is asked once per entity per frame by the visibility
    // filter, and walking the parent chain there would be quadratic.
    std::vector<game::content::AuthorId> members;
    uint64_t membersRevision = ~uint64_t(0);

    // Restored on leaving. The author was somewhere when they entered, and a
    // mode that returns them to the origin is a mode they stop using.
    EditorCamera cameraBefore;
    float gridLevelBefore = 0.0f;
    bool gridCutBefore = false;

    bool contains(const game::content::AuthorId& id) const
    {
        for (const game::content::AuthorId& member : members)
            if (member == id)
                return true;
        return false;
    }
};

// Everything the editor session holds that is not the document itself. Kept
// apart so the document stays exactly what the .scn says -- panel layout,
// selection and camera bookmarks are presentation state and belong in the
// sidecar, never in the scene file.
struct EditorState {
    game::content::SceneDocument document;
    game::content::KitCatalog catalog;
    game::content::GridConfig grid;

    std::string scenePath;    // "" until saved
    std::string kitPath;
    // Content pack directory (eng::assets::packDir("content")), resolved once
    // in onLoad. The editor needs a directory, not a logical path: it saves
    // and cooks *into* the tree, and validate() checks prefab meshes against a
    // root. Read-only lookups go through the resolver instead.
    std::string assetRoot;
    bool dirty = false;

    Tool tool = Tool::Select;
    GridState gridState;
    EditorCamera camera;
    ViewportMode mode = ViewportMode::Level;
    IsolationState isolation;

    bool isolating() const { return mode == ViewportMode::Isolate; }

    std::vector<game::content::AuthorId> selection;
    // What the Place tool will drop next, and which way up.
    Brush brush;
    // What the Room tool builds. Held here rather than passed around so the
    // toolbar can offer a crypt-walled room as easily as a plain one.
    game::content::RoomSpec roomSpec;

    bool isSelected(const game::content::AuthorId& id) const
    {
        for (const auto& selected : selection)
            if (selected == id) return true;
        return false;
    }
    void select(const game::content::AuthorId& id)
    {
        selection.assign(1, id);
    }
    void toggleSelected(const game::content::AuthorId& id)
    {
        for (std::size_t i = 0; i < selection.size(); ++i) {
            if (selection[i] == id) {
                selection.erase(selection.begin() + std::ptrdiff_t(i));
                return;
            }
        }
        selection.push_back(id);
    }
    const game::content::AuthorId* primary() const
    {
        return selection.empty() ? nullptr : &selection.front();
    }

    // --- per-entity session state -------------------------------------------
    // Hidden and locked are the editor's, not the document's: they are how an
    // author gets a ceiling out of the way or stops clicking the floor while
    // dressing a room, and none of it belongs in a file two people share.
    //
    // Hidden entities are not drawn and not pickable. Locked ones are drawn and
    // not pickable -- which is the whole point, since the thing you keep
    // catching by accident is usually the thing you most need to see.
    std::vector<game::content::AuthorId> hidden;
    std::vector<game::content::AuthorId> locked;

    static bool listed(const std::vector<game::content::AuthorId>& list,
                       const game::content::AuthorId& id)
    {
        for (const auto& entry : list)
            if (entry == id) return true;
        return false;
    }
    static void toggleIn(std::vector<game::content::AuthorId>& list,
                         const game::content::AuthorId& id, bool on)
    {
        for (std::size_t i = 0; i < list.size(); ++i) {
            if (list[i] != id) continue;
            if (!on) list.erase(list.begin() + std::ptrdiff_t(i));
            return;
        }
        if (on) list.push_back(id);
    }

    // Which layers are switched off, locked or soloed, and where new entities
    // land. Session state for the same reason `hidden` above is: what a layer
    // IS and who is in it is shared and lives in the document, but whether it
    // is on screen right now is one person's ten minutes.
    layers::LayerSession layerSession;

    // --- effective visibility ------------------------------------------------
    // An entity is out of the way if it was hidden on its own account OR its
    // layer is off. Every consumer -- the viewport filter, the picker, the
    // marquee, the outliner -- must ask these two rather than the raw lists, or
    // a click lands on something that is not on screen.
    //
    // The layer half needs the entity, not just the id, so the two overloads
    // exist: callers holding a document look the entity up, callers already
    // holding one pass it straight in.
    bool isHidden(const game::content::AuthorId& id) const
    {
        if (listed(hidden, id))
            return true;
        const game::content::Entity* entity = document.find(id);
        return entity != nullptr && layers::hidesEntity(layerSession, *entity);
    }
    bool isLocked(const game::content::AuthorId& id) const
    {
        if (listed(locked, id))
            return true;
        const game::content::Entity* entity = document.find(id);
        return entity != nullptr && layers::locksEntity(layerSession, *entity);
    }
    // Whether the entity itself was hidden/locked, ignoring its layer. What the
    // outliner's own eye and padlock toggles read and write, so clicking the
    // eye on a row inside a hidden layer does not silently do nothing.
    bool isHiddenAlone(const game::content::AuthorId& id) const
    {
        return listed(hidden, id);
    }
    bool isLockedAlone(const game::content::AuthorId& id) const
    {
        return listed(locked, id);
    }
    void setHidden(const game::content::AuthorId& id, bool on)
    {
        toggleIn(hidden, id, on);
    }
    void setLocked(const game::content::AuthorId& id, bool on)
    {
        toggleIn(locked, id, on);
    }
};

} // namespace ed
