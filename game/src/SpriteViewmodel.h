#pragma once

#include <eng/Handles.h>

#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace eng { class Renderer; }

namespace game {

// One composited layer of a sprite viewmodel: a camera-facing quad with an
// atlas on it.
//
// This is the classic FPS presentation -- a stack of flat images in front of
// the eye, drawn in depth order -- expressed as data so that "hands, then
// weapon, then muzzle flash" is three rows in a TOML rather than three special
// cases in a renderer.
//
// The layer says nothing about *whose* layer it is. Hand layers come from
// viewmodel_hands.toml and weapon layers from weapons.toml; they are the same
// type and are composited into one list, which is what lets one authored pair
// of hands serve every weapon (the brief's Phase 8).
struct ViewmodelSpriteLayer {
    // Authoring name. Never used to look anything up -- it exists so the debug
    // panel and a warning can say which row is wrong.
    std::string id;
    // A material naming the sprite sheet, defined in a `.mat` file. Each
    // ANIMATED layer needs its own material: frame selection writes uvOffset on
    // the material, and two layers sharing one would fight over the frame.
    std::string material;
    // Camera space, metres, on the sprite plane. +x right, +y up.
    glm::vec2 offset{0.0f, 0.0f};
    // Quad width and height in metres at that plane.
    glm::vec2 size{1.0f, 1.0f};
    // Metres in front of the eye. This is also the LAYER ORDER: layers are
    // sorted farthest-first at build time and drawn in that order with depth
    // testing off, so a smaller distance draws over a larger one. Keeping the
    // ordering geometric rather than an index means a layer cannot claim a
    // priority its position contradicts, and the rig's rotation gives the stack
    // a little honest parallax for free.
    float distance = 0.60f;
    // Atlas layout: columns, rows. {1,1} is a single-frame sprite.
    glm::ivec2 grid{1, 1};
    // Cell index (row-major) held when nothing is happening.
    int idleFrame = 0;
    // The firing run: first cell, how many, and how fast. A count of 1 means
    // the layer does not animate on fire, which is right for a static hand.
    int fireFrame = 0;
    int fireFrameCount = 1;
    float fireFps = 14.0f;
};

// Material and id non-empty, grid at least 1x1, every frame index inside the
// atlas, sizes positive and finite. An invalid row is dropped rather than
// failing the whole viewmodel -- one bad layer must not cost the player their
// hands, the same rule the socket set follows.
//
// Inline on purpose: the weapon parser and the hands parser both validate these
// rows, and both are renderer-free (the editor, the cooker and three tests link
// them without a GPU). Putting it in SpriteViewmodel.cpp -- which needs the
// renderer to build quads -- would have dragged eng_systems into all of them.
inline bool validViewmodelSpriteLayer(const ViewmodelSpriteLayer& layer)
{
    const auto finite2 = [](glm::vec2 v) {
        return std::isfinite(v.x) && std::isfinite(v.y);
    };
    if (layer.id.empty() || layer.material.empty())
        return false;
    if (layer.grid.x < 1 || layer.grid.y < 1)
        return false;
    if (!finite2(layer.offset) || !finite2(layer.size) ||
        !std::isfinite(layer.distance) || !std::isfinite(layer.fireFps))
        return false;
    if (layer.size.x <= 0.0f || layer.size.y <= 0.0f || layer.distance <= 0.0f)
        return false;
    if (layer.fireFrameCount < 1 || layer.fireFps <= 0.0f)
        return false;
    const int cells = layer.grid.x * layer.grid.y;
    // Every cell the layer can ever select must exist. A frame index past the
    // end of the atlas samples a neighbouring cell, which reads as a sprite
    // that flickers into someone else's art rather than as a bad number.
    if (layer.idleFrame < 0 || layer.idleFrame >= cells)
        return false;
    if (layer.fireFrame < 0 || layer.fireFrame + layer.fireFrameCount > cells)
        return false;
    return true;
}

// The sprite half of first-person presentation.
//
// Deliberately NOT a world billboard: eng::Renderer::attachSprite is a
// camera-facing sprite in the world, and it is an unimplemented stub in the RHI
// backend besides. These are quads parented to the first-person rig node, so
// they inherit every procedural layer (bob, sway, recoil, landing) from
// ViewmodelMotion exactly as the skinned hands do -- the motion composer never
// learns which presentation is riding on it.
//
// The muzzle for a sprite weapon is authored in camera space rather than taken
// from a joint, because there is no skeleton here. `aim != muzzle` is
// unaffected: aim is still the camera ray.
class SpriteViewmodel {
public:
    // Builds one quad per valid layer under `parent`, replacing whatever was
    // there. Invalid layers are skipped with a warning naming their id.
    void build(eng::Renderer& renderer, eng::NodeHandle parent,
               const std::vector<ViewmodelSpriteLayer>& layers);
    void clear(eng::Renderer& renderer);

    // Rising edge of a shot: restarts every layer's firing run.
    void triggerFire();
    // Advances the firing runs and writes this frame's cell to each layer's
    // material. Takes the stepped viewmodel channel, like the skinned clips do.
    void update(eng::Renderer& renderer, float dt);
    void setVisible(eng::Renderer& renderer, bool show);

    bool valid() const { return !mLayers.empty(); }
    std::size_t layerCount() const { return mLayers.size(); }
    eng::NodeHandle node() const { return mRoot; }

private:
    struct Layer {
        ViewmodelSpriteLayer def;
        eng::NodeHandle node{};
        float fireTime = -1.0f; // <0 = idle
        int appliedFrame = -1;  // last cell written, so a held frame is free
    };

    void applyFrame(eng::Renderer& renderer, Layer& layer, int frame);

    // One node owning the stack, so hiding or destroying the sprite viewmodel
    // is one call and the quads keep their authored local placement.
    eng::NodeHandle mRoot{};
    eng::MeshHandle mQuad{};
    std::vector<Layer> mLayers;
};

} // namespace game
