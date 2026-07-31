#pragma once
#include "EditorCamera.h"
#include "GridMath.h"
#include "RoomBuilder.h"
#include "KitCatalog.h"
#include "SceneDocument.h"

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

    static constexpr float kSubdivisions[] = {4.0f, 2.0f, 1.0f, 0.5f};
    static constexpr int kSubdivisionCount = 4;

    float step() const { return kSubdivisions[subdivision]; }
    void finer() { subdivision = (subdivision + 1) % kSubdivisionCount; }
    void coarser()
    {
        subdivision = (subdivision + kSubdivisionCount - 1) % kSubdivisionCount;
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
    // The game pack's directory (eng::assets::packDir("game")), resolved once
    // in onLoad. The editor needs a directory, not a logical path: it saves
    // and cooks *into* the tree, and validate() checks prefab meshes against a
    // root. Read-only lookups go through the resolver instead.
    std::string assetRoot;
    bool dirty = false;

    Tool tool = Tool::Select;
    GridState gridState;
    EditorCamera camera;

    std::vector<game::content::AuthorId> selection;
    // The prefab the Place tool will drop next.
    std::string brushPrefab;
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
};

} // namespace ed
