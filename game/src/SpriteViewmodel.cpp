#include "SpriteViewmodel.h"

#include <eng/Log.h>
#include <eng/Primitive.h>
#include <eng/Renderer.h>

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>

namespace game {

void SpriteViewmodel::build(eng::Renderer& renderer, eng::NodeHandle parent,
                            const std::vector<ViewmodelSpriteLayer>& layers)
{
    clear(renderer);
    if (!parent.valid())
        return;

    mRoot = renderer.createNode(parent, glm::vec3(0.0f), "sprite_viewmodel");

    // One unit quad, reused by every layer: the node's scale gives each its
    // authored size, so a ten-layer viewmodel is still one mesh.
    //
    // eng::PrimitiveKind::Plane is authored on XZ with its normal along +Y and
    // v running along +z. Pitching it +90 degrees puts the normal on +Z -- the
    // camera looks down -Z, so that faces the eye -- and maps v to -Y, which
    // makes v = 0 the TOP of the image, the convention every sprite sheet is
    // authored in.
    eng::PrimitiveMeshDesc quad;
    quad.kind = eng::PrimitiveKind::Plane;
    quad.size = glm::vec3(1.0f, 1.0f, 1.0f);
    quad.thickness = 0.0f; // single face
    mQuad = renderer.createPrimitiveMesh(quad);

    const glm::quat faceCamera =
        glm::angleAxis(glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f));

    // Painter's order, farthest first.
    //
    // The viewmodel pass runs after the world and reuses its depth buffer, so a
    // depth-testing sprite would be occluded by the wall behind it -- which is
    // the one thing a first-person sprite must never do. The sprite materials
    // therefore switch depth off entirely, and with depth off the ONLY thing
    // that orders these quads is the order they are submitted in. Sorting here
    // is what makes `distance` mean what it says.
    std::vector<ViewmodelSpriteLayer> ordered(layers.begin(), layers.end());
    std::stable_sort(ordered.begin(), ordered.end(),
                     [](const ViewmodelSpriteLayer& a,
                        const ViewmodelSpriteLayer& b) {
                         return a.distance > b.distance;
                     });

    for (const ViewmodelSpriteLayer& def : ordered) {
        if (!validViewmodelSpriteLayer(def)) {
            eng::log::warn(
                "sprite viewmodel: layer '%s' is invalid and was dropped",
                def.id.empty() ? "<unnamed>" : def.id.c_str());
            continue;
        }
        Layer layer;
        layer.def = def;
        // Camera space: +x right, +y up, -z forward.
        layer.node = renderer.createNode(
            mRoot, glm::vec3(def.offset.x, def.offset.y, -def.distance),
            def.id);
        renderer.setOrientation(layer.node, faceCamera);
        renderer.setScale(layer.node,
                          glm::vec3(def.size.x, 1.0f, def.size.y));
        // renderOnTop: a first-person sprite is a presentation element, not a
        // world object, and must not vanish into the wall the player is
        // standing against. This is what puts it in the renderer's viewmodel
        // pass, where the layers then depth-sort against each other.
        renderer.attachMesh(layer.node, mQuad, def.material,
                            /*castShadows=*/false, /*renderOnTop=*/true);
        mLayers.push_back(std::move(layer));
    }

    // Seat every layer on its idle cell now rather than waiting for the first
    // update: a viewmodel that shows cell 0 for one frame before snapping to
    // its idle pose is a visible flicker on every weapon switch.
    for (Layer& layer : mLayers)
        applyFrame(renderer, layer, layer.def.idleFrame);
}

void SpriteViewmodel::clear(eng::Renderer& renderer)
{
    if (mRoot.valid())
        renderer.destroyNode(mRoot);
    mRoot = {};
    mLayers.clear();
    // The quad belongs to the renderer's scene and dies with it on clearScene;
    // holding the handle across a level would hand out a dead mesh.
    mQuad = {};
}

void SpriteViewmodel::triggerFire()
{
    for (Layer& layer : mLayers)
        layer.fireTime = 0.0f;
}

void SpriteViewmodel::applyFrame(eng::Renderer& renderer, Layer& layer,
                                 int frame)
{
    if (frame == layer.appliedFrame)
        return;
    layer.appliedFrame = frame;
    const glm::ivec2 grid = layer.def.grid;
    const glm::vec2 cell(1.0f / float(grid.x), 1.0f / float(grid.y));
    const int column = frame % grid.x;
    const int row = frame / grid.x;
    // uvScale/uvOffset are read off the material every draw by the shared
    // vertex shader, so selecting a cell is two parameter writes and needs no
    // per-frame geometry. It is also why each animated layer needs its own
    // material: these are per-material, not per-instance.
    renderer.setMaterialParam(layer.def.material, "uvScale", cell);
    renderer.setMaterialParam(layer.def.material, "uvOffset",
                              glm::vec2(float(column) * cell.x,
                                        float(row) * cell.y));
}

void SpriteViewmodel::update(eng::Renderer& renderer, float dt)
{
    for (Layer& layer : mLayers) {
        int frame = layer.def.idleFrame;
        if (layer.fireTime >= 0.0f) {
            layer.fireTime += dt;
            const int step = int(layer.fireTime * layer.def.fireFps);
            if (step >= layer.def.fireFrameCount)
                layer.fireTime = -1.0f; // run finished, back to idle
            else
                frame = layer.def.fireFrame + step;
        }
        applyFrame(renderer, layer, frame);
    }
}

void SpriteViewmodel::setVisible(eng::Renderer& renderer, bool show)
{
    if (mRoot.valid())
        renderer.setNodeVisible(mRoot, show);
}

} // namespace game
