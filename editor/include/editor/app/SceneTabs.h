#pragma once

#include <editor/commands/Commands.h>
#include <editor/content/SceneDocument.h>
#include <editor/scene/Layers.h>
#include <editor/viewport/EditorCamera.h>

#include <cstddef>
#include <string>
#include <vector>

namespace ed {

// Open scenes, as tabs.
//
// The editor held exactly one document, which is why *opening* a scene was a
// destructive act: it went through the same save/discard/cancel prompt as
// quitting, because it threw the open one away. An author comparing two rooms,
// or copying a light rig from one level into another, had to choose which of
// them they were allowed to have.
//
// A tab owns everything that is *about one scene*: the document itself, where
// it came from, whether it has unsaved edits, its undo history, what is
// selected in it, where the camera is, and which of its entities the author has
// hidden or locked. Everything else in EditorState -- the tool, the grid, the
// brush -- is about the author rather than about the scene, stays shared, and
// deliberately does not switch with the tab. Nobody wants the grid step to
// change because they looked at another level.
//
// Undo is per tab and not shared. A single stack across documents can produce a
// command whose target no longer exists in the document it is being undone
// against, and the fact that commands address entities by AuthorId (see
// Commands.h) makes that failure silent rather than loud.
struct SceneTab {
    // Identity that survives its neighbours being closed.
    //
    // The tab bar keys its per-tab imgui state on this rather than on the
    // index, because closing a tab shifts every index after it and the
    // survivors would inherit each other's state -- the scroll position and
    // selection of the tab that used to be there.
    unsigned long long uid = 0;

    // Where it lives on disk, or empty for a scene that has never been saved.
    std::string path;
    game::content::SceneDocument document;
    bool dirty = false;

    CommandStack commands;
    std::vector<game::content::AuthorId> selection;
    EditorCamera camera;
    std::vector<game::content::AuthorId> hidden;
    std::vector<game::content::AuthorId> locked;
    layers::LayerSession layerSession;

    // The cook status line, per scene: "cooked 12:04" belongs to the scene it
    // describes, and carrying one tab's across to another said the wrong thing
    // about both.
    std::string cookStatus = "not cooked";
};

// What the tab is labelled: the file's name, or "[unsaved]" for one that has
// never been written. Free of the dirty mark, which the caller draws separately
// so it cannot be mistaken for part of the filename.
std::string sceneTabName(const SceneTab& tab);

// The tooltip: the full path, or a note that there is not one yet.
std::string sceneTabTooltip(const SceneTab& tab);

class SceneTabs
{
public:
    // Always at least one tab. An editor with no open scene has a state its own
    // panels cannot describe -- the outliner of nothing, the inspector of
    // nothing -- and every path through this class maintains the invariant
    // rather than every panel testing for it.
    SceneTabs();

    std::size_t size() const { return mTabs.size(); }
    const SceneTab& at(std::size_t index) const { return mTabs.at(index); }
    SceneTab& at(std::size_t index) { return mTabs.at(index); }

    std::size_t active() const { return mActive; }
    const SceneTab& current() const { return mTabs.at(mActive); }
    SceneTab& current() { return mTabs.at(mActive); }

    // Which tab holds this file, or size() for none. Open-if-not-open is the
    // whole reason a tab set beats a document: clicking a scene twice must
    // raise it, not load a second copy that can then disagree with the first.
    std::size_t indexOfPath(const std::string& path) const;

    // Appends a tab and makes it current. Returns its index.
    std::size_t open(SceneTab tab);
    // Makes `index` current. Out-of-range is ignored rather than clamped: a
    // stale index is a bug in the caller, and silently activating a neighbour
    // hides it behind the author's document changing under them.
    void activate(std::size_t index);

    // Removes a tab. When it was the last one, the set is left holding a single
    // fresh untitled tab rather than none -- closing the only scene is how an
    // author says "start again", not "leave me with nothing".
    //
    // Returns true when the closed tab was replaced by a blank one, which the
    // caller needs to know because the document it must now mirror is a
    // different object either way.
    bool close(std::size_t index);

    // True when any tab has unsaved edits. What quitting asks.
    bool anyDirty() const;

private:
    // Never reused, never reset. A counter rather than a hash of the path,
    // because two tabs can legitimately hold no path at all.
    unsigned long long nextUid() { return ++mNextUid; }

    std::vector<SceneTab> mTabs;
    std::size_t mActive = 0;
    unsigned long long mNextUid = 0;
};

} // namespace ed
