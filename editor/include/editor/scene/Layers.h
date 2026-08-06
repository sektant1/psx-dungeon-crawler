#pragma once
#include <editor/content/SceneDocument.h>

#include <string>
#include <string_view>
#include <vector>

namespace ed::layers {

// Authoring layers: the organisation Gregory §15.4.1.5 describes, and the
// session state that goes with it.
//
// The document holds what a layer IS and who is in it (game::content::Layer,
// Entity::layer). This file holds what the current session is DOING with them
// -- which are shown, which are locked, which one new entities land in -- and
// the queries every panel needs so that the viewport, the picker, the marquee
// and the outliner cannot disagree about whether an entity is reachable.
//
// The empty id is the default layer. It always exists, it is never in the
// document's list, and it is what every entity authored before layers existed
// belongs to.
inline constexpr std::string_view kDefaultLayer{};

// What the panel draws for the default layer, so the three callers that need a
// label for it do not invent three different ones.
inline constexpr const char* kDefaultLayerName = "Default";

struct LayerSession {
    // Layer ids the author has switched off, and ones they have pinned so a
    // click cannot land on them. Ids rather than indices: a layer removed
    // mid-session must not silently hide a different one.
    std::vector<std::string> hidden;
    std::vector<std::string> locked;
    // While non-empty, ONLY this layer draws. The chapter's clutter argument
    // taken to its end: "show me the lighting and nothing else" is one click,
    // where hiding the other six is six -- and six to put back.
    //
    // Solo is deliberately not the same thing as hiding the rest: leaving solo
    // restores whatever was hidden before it, rather than showing everything.
    std::string solo;
    // Where a newly created entity lands. Empty is the default layer, which is
    // where everything went before this existed.
    std::string active;

    bool soloing() const { return !solo.empty(); }
};

bool isHidden(const LayerSession& session, std::string_view layerId);
bool isLocked(const LayerSession& session, std::string_view layerId);
// Toggle helpers with the same shape EditorState uses for entities, so the two
// lists behave identically under the same gestures.
void setHidden(LayerSession& session, std::string_view layerId, bool on);
void setLocked(LayerSession& session, std::string_view layerId, bool on);
// Enters, leaves or switches solo. Passing the layer that is already soloed
// leaves solo, which is what makes the button a toggle.
void toggleSolo(LayerSession& session, std::string_view layerId);

// Whether the session hides / locks this entity BECAUSE OF ITS LAYER. The
// entity's own hidden and locked lists are separate and are OR-ed in by the
// caller: an entity can be hidden on its own account, by its layer, or both,
// and un-hiding it individually must not resurrect a hidden layer.
bool hidesEntity(const LayerSession& session,
                 const game::content::Entity& entity);
bool locksEntity(const LayerSession& session,
                 const game::content::Entity& entity);

// Every layer id the document uses, default first, then the declared ones in
// author order, then any id an entity names that no layer declares.
//
// The last group is the point: a hand-edited file or a botched merge can leave
// entities pointing at a layer that is gone, and a list that silently omitted
// them would make those entities unreachable from the panel that exists to
// reach them.
std::vector<std::string> layerIds(const game::content::SceneDocument& document);

// Display name for an id, including the two cases the document cannot answer:
// the default layer, and an id nothing declares.
std::string displayName(const game::content::SceneDocument& document,
                        std::string_view layerId);

struct LayerStat {
    std::string id;
    std::string name;
    glm::vec3 colour{0.6f, 0.65f, 0.7f};
    std::size_t entities = 0;
    // True when entities name this id but no Layer declares it.
    bool undeclared = false;
};

// One row per id from layerIds(), with counts. What the Layers panel draws.
std::vector<LayerStat> stats(const game::content::SceneDocument& document);

// The ids of every entity in a layer, in document order.
std::vector<game::content::AuthorId>
membersOf(const game::content::SceneDocument& document,
          std::string_view layerId);

// --- per-layer save and load ------------------------------------------------
//
// The chapter's division-of-labour claim (§15.4.1.5, §15.4.1.9) made real
// without splitting the on-disk format into a file per layer, which would break
// every scene that exists.

// A document holding only `layerId`'s entities, plus the ancestors they hang
// from so the extracted transforms still resolve. Ancestors come across as
// bare transform nodes -- copying a whole parent chain would drag half the
// level into a lighting pass.
game::content::SceneDocument
extractLayer(const game::content::SceneDocument& document,
             std::string_view layerId);

struct MergeReport {
    std::size_t added = 0;
    std::size_t skipped = 0; // ancestor stubs already present, not re-added
    // Ids that collided and were renamed on the way in, old -> new.
    std::vector<std::pair<game::content::AuthorId, game::content::AuthorId>>
        renamed;
};

// Merges `incoming` into `document`, forcing everything it adds into `layerId`.
//
// Colliding ids are renamed rather than overwritten: the merge is somebody
// else's afternoon of work arriving, and silently replacing an entity that
// happens to share a name is the one outcome nobody can undo by hand. Parent
// links inside the incoming set are rewritten to follow the rename.
MergeReport mergeLayer(game::content::SceneDocument& document,
                       const game::content::SceneDocument& incoming,
                       std::string_view layerId);

} // namespace ed::layers
