#pragma once

#include "PlayerWeapons.h"

#include <eng/Handles.h>
#include <eng/render/Enchantment.h>

#include <optional>
#include <vector>

namespace eng { class Renderer; }

namespace game {

// What is actually in the player's hand: the weapon's visual, parented to a
// socket node on the first-person rig (see ViewmodelSocket.h).
//
// Two presentations, chosen by data rather than by type:
//
//   model = "meshes/viewmodels/weapons/x.glb"  -> that mesh
//   model = "" (and `part` rows authored)      -> generated primitives
//
// The primitive path is the placeholder every weapon uses until it has a model.
// It was authored in weapons.toml and validated as required all along, but the
// only code that built it was never called, so weapons rendered as empty hands;
// this is where those rows finally become geometry.
//
// The seam the brief asks for in Phase 7 is exactly this: WeaponController and
// ViewmodelMotion never learn which branch was taken, so dropping a .glb beside
// a TOML line is the whole of "give weapon #4 a model".
class WeaponViewmodel {
public:
    enum class Presentation { None, Model, Primitives };

    // Builds under `socketNode`, replacing whatever was there. An invalid
    // socket (a weapon naming one the rig does not define) leaves the object
    // empty rather than parenting the weapon to the world.
    void build(eng::Renderer& renderer, eng::NodeHandle socketNode,
               const WeaponViewmodelDef& definition,
               const std::optional<eng::EnchantmentDesc>& glow = std::nullopt);
    void clear(eng::Renderer& renderer);

    // Re-applies attach offset/rotation/scale without rebuilding the geometry.
    // The gizmo drags these every frame; reloading a mesh per frame would not
    // survive the first drag.
    void applyAttachment(eng::Renderer& renderer,
                         const WeaponViewmodelDef& definition);
    void setVisible(eng::Renderer& renderer, bool show);
    void setEnchantment(eng::Renderer& renderer,
                        const std::optional<eng::EnchantmentDesc>& glow);

    eng::NodeHandle node() const { return mNode; }
    Presentation presentation() const { return mPresentation; }
    bool valid() const { return mNode.valid(); }

private:
    // The attach node: one transform under the socket, so the whole weapon
    // moves as a unit and the parts keep their authored local placement.
    eng::NodeHandle mNode{};
    // Owned only on the model path; primitive meshes belong to the renderer's
    // primitive cache and are not released per weapon.
    eng::MeshHandle mMesh{};
    std::vector<eng::NodeHandle> mGlowNodes;
    Presentation mPresentation = Presentation::None;
};

const char* weaponPresentationName(WeaponViewmodel::Presentation presentation);

} // namespace game
