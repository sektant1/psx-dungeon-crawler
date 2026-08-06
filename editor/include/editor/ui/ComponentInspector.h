#pragma once
#include <functional>
#include <editor/scene/EntityComponents.h>
#include <editor/scene/MultiEdit.h>
#include <editor/content/GridMath.h>
#include <editor/content/KitCatalog.h>
#include <editor/assets/MaterialCatalog.h>
#include <editor/ui/EditorUi.h>

#include <eng/ecs/components/PrimitiveMesh.h>

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
    // Runtime-ready audio clips under the mounted content pack. Free text stays
    // available for a clip generated after the editor started.
    const std::vector<std::string>* audioAssets = nullptr;
    // Cue ids from audio.toml, for the actor sound table. A cue is a semantic
    // event ("enemy.death"), not a clip: the catalog decides which takes it
    // draws from, so this is the vocabulary an actor's action is authored
    // against and a clip path here would bypass every mix decision in it.
    const std::vector<std::string>* audioCues = nullptr;
    // The vocabularies the game itself defines: enemy ids from enemies.toml,
    // pickup ids, and the marker prefixes the runtime looks up.
    //
    // These were free-text fields, which meant the editor let you author
    // "gobling" and told you nothing -- the misspelling surfaced as an enemy
    // that never spawned, in the game, some minutes later. An id you can only
    // get right by having read a TOML is not authorable.
    const std::vector<std::string>* enemyIds = nullptr;
    const std::vector<std::string>* pickupIds = nullptr;
    // The RPG layer's vocabularies, on the same argument. `npcIds` is everyone
    // the content describes -- the union of who has a conversation and who
    // keeps a shop -- because both are ways of being a person, and an author
    // standing a blacksmith in the village should not have to know which file
    // that blacksmith happens to be described in.
    //
    // `traderIds` is the subset who trade, used to *annotate* the pick rather
    // than to filter it: knowing the person you just placed also runs a shop is
    // worth a line, and a silent list of two would have been worse than none.
    const std::vector<std::string>* npcIds = nullptr;
    const std::vector<std::string>* traderIds = nullptr;
    // Quest ids and hideout/village station ids, for the components that name
    // one. Loaded the same way and offered the same way.
    const std::vector<std::string>* questIds = nullptr;
    const std::vector<std::string>* stationIds = nullptr;
    // Player weapon ids from weapons.toml, for the viewmodel preview. Null
    // leaves the field typed, which is survivable but unguided.
    const std::vector<std::string>* weaponIds = nullptr;
    // The .lua files on disk, as the logical paths a scene names them by. Null
    // or empty falls back to a typed path, which is only survivable, not good.
    const std::vector<std::string>* scriptPaths = nullptr;
    // Rescan the above. Offered next to the picker: a script written after the
    // editor started must be attachable without restarting it.
    std::function<void()> rescanScripts;
    // Every author id in the open scene, for a script prop that names another
    // entity. Rebuilt per frame by the panel, because an entity added this
    // frame is a legitimate target and a cached list would not offer it. The
    // cooker fails the build on a name that is not here, so picking beats
    // typing by exactly the margin that check is worth.
    const std::vector<std::string>* sceneEntityIds = nullptr;
    // What the classified catalogue says about the material offered here, and
    // what this entity's mesh can take. Null leaves the combo unfiltered, which
    // is what it always was.
    const std::vector<MaterialInfo>* materials = nullptr;
    MeshKind meshKind = MeshKind::Unknown;

    // --- live preview ------------------------------------------------------
    // The editor renders one shared offscreen swatch (a lit sphere for a
    // material, the running effect for a particle). This panel only asks for a
    // subject and draws the result, so it stays free of any renderer type.
    //
    // `previewTexture` is that swatch's id, 0 when the editor has none yet.
    // The request callbacks are deduplicated by the editor, so calling one
    // every frame while a row is hovered costs nothing.
    uint64_t previewTexture = 0;
    std::function<void(const std::string&)> requestMaterialPreview;
    std::function<void(const std::string&)> requestEffectPreview;
    std::function<void(const game::content::AuthorId&)> requestAudioPreview;
    std::function<void()> stopAudioPreview;
    bool audioPreviewing = false;

    // What the whole selection agrees about, for the rows that can show it.
    // The panel draws ONE entity -- the primary -- so without this a field the
    // selection differs on would be presented as though they all held the
    // primary's value (Gregory §15.4.1.6). Default-constructed means "agrees",
    // which is correct for a selection of one and for any caller that does not
    // set it.
    multiedit::TransformAgreement agreement;

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

// The generated-mesh parameters, as property-grid rows.
//
// Shared by the inspector's Primitive Mesh drawer and the Meshes browser's
// parameter strip, because they are the same question asked in two places: the
// browser sets the size a primitive will be painted at, the inspector corrects
// one that already exists. Two copies of "which fields does a capsule have"
// would disagree the first time a kind gained one.
//
// Only the fields the chosen kind actually reads are drawn -- a sphere has no
// `size` and a box has no `segments`, and showing them anyway presents settings
// that do nothing. `context`, when given, receives the drag/commit tracking the
// inspector needs; the browser passes null and reads the return value.
bool drawPrimitiveFields(eng::ecs::PrimitiveMesh& mesh, ui::PropertyGrid& grid,
                         InspectorContext* context = nullptr);

// id, name and transform: what every entity has, drawn above the components.
void drawEntityIdentity(game::content::Entity& entity,
                        InspectorContext& context);

// One component's fields, with no header -- the caller owns the collapsing
// header and the remove button, so the layout is decided in one place.
void drawComponentBody(const ComponentType& type, game::content::Entity& entity,
                       InspectorContext& context);

} // namespace ed
