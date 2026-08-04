#include "WeaponViewmodel.h"

#include <eng/Log.h>
#include <eng/Primitive.h>
#include <eng/Renderer.h>
#include <eng/assets/AssetRoot.h>

#include <glm/gtc/quaternion.hpp>

#include <filesystem>

namespace game {
namespace {

glm::quat degreesOrientation(glm::vec3 degrees)
{
    // Same convention the rest of the viewmodel uses (yaw, then roll, then
    // pitch); a weapon authored against the panel must not need a second one.
    const glm::quat pitch =
        glm::angleAxis(glm::radians(degrees.x), glm::vec3(1, 0, 0));
    const glm::quat yaw =
        glm::angleAxis(glm::radians(degrees.y), glm::vec3(0, 1, 0));
    const glm::quat roll =
        glm::angleAxis(glm::radians(degrees.z), glm::vec3(0, 0, 1));
    return yaw * roll * pitch;
}

eng::PrimitiveKind primitiveKind(WeaponPrimitive primitive)
{
    switch (primitive) {
    case WeaponPrimitive::Box: return eng::PrimitiveKind::Box;
    case WeaponPrimitive::BeveledBox: return eng::PrimitiveKind::BeveledBox;
    case WeaponPrimitive::Sphere: return eng::PrimitiveKind::Sphere;
    case WeaponPrimitive::Capsule: return eng::PrimitiveKind::Capsule;
    case WeaponPrimitive::Cylinder: return eng::PrimitiveKind::Cylinder;
    case WeaponPrimitive::Cone: return eng::PrimitiveKind::Cone;
    case WeaponPrimitive::Disc: return eng::PrimitiveKind::Disc;
    }
    return eng::PrimitiveKind::Box;
}

} // namespace

const char* weaponPresentationName(WeaponViewmodel::Presentation presentation)
{
    switch (presentation) {
    case WeaponViewmodel::Presentation::Model: return "model";
    case WeaponViewmodel::Presentation::Primitives: return "primitives";
    case WeaponViewmodel::Presentation::None: break;
    }
    return "none";
}

void WeaponViewmodel::build(eng::Renderer& renderer,
                            eng::NodeHandle socketNode,
                            const WeaponViewmodelDef& definition,
                            const std::optional<eng::EnchantmentDesc>& glow)
{
    clear(renderer);
    if (!socketNode.valid())
        return;

    mNode = renderer.createNode(socketNode, glm::vec3(0.0f), "weapon-viewmodel");
    if (!mNode.valid())
        return;
    applyAttachment(renderer, definition);

    // renderOnTop, like the hands themselves: a first-person weapon is a
    // presentation element, not a world object, and it must not vanish into a
    // wall the player is standing against.
    constexpr bool kCastShadows = false;
    constexpr bool kRenderOnTop = true;

    if (!definition.model.empty()) {
        const std::filesystem::path resolved =
            eng::assets::resolve(definition.model);
        if (resolved.empty()) {
            eng::log::warn("Weapon viewmodel: model '%s' was not found",
                           definition.model.c_str());
        } else {
            mMesh = renderer.loadMesh(resolved.string());
            if (mMesh.valid()) {
                renderer.attachMesh(mNode, mMesh, definition.modelMaterial,
                                    kCastShadows, kRenderOnTop);
                mGlowNodes.push_back(mNode);
                mPresentation = Presentation::Model;
                setEnchantment(renderer, glow);
                return;
            }
            eng::log::warn("Weapon viewmodel: model '%s' failed to load",
                           definition.model.c_str());
        }
        // Falls through to the primitives, which is the point of keeping them:
        // a missing or broken model leaves a placeholder in the hand rather
        // than an empty one, and the console says which happened.
    }

    for (const WeaponViewmodelPart& part : definition.parts) {
        eng::PrimitiveMeshDesc meshDesc;
        meshDesc.kind = primitiveKind(part.primitive);
        meshDesc.bevel = 0.08f;
        meshDesc.rings = 8;
        meshDesc.segments = 10;
        const eng::MeshHandle mesh = renderer.createPrimitiveMesh(meshDesc);
        if (!mesh.valid())
            continue;
        const eng::NodeHandle node = renderer.createNode(mNode, part.position);
        if (!node.valid())
            continue;
        renderer.setOrientation(node, degreesOrientation(part.rotationDegrees));
        renderer.setScale(node, part.scale);
        renderer.attachMesh(node, mesh, part.material, kCastShadows,
                            kRenderOnTop);
        if (part.enchanted)
            mGlowNodes.push_back(node);
    }
    mPresentation =
        definition.parts.empty() ? Presentation::None : Presentation::Primitives;
    setEnchantment(renderer, glow);
}

void WeaponViewmodel::clear(eng::Renderer& renderer)
{
    if (mNode.valid())
        renderer.destroyNode(mNode); // takes the part nodes with it
    mNode = {};
    mGlowNodes.clear();
    if (mMesh.valid())
        renderer.releaseMesh(mMesh);
    mMesh = {};
    mPresentation = Presentation::None;
}

void WeaponViewmodel::applyAttachment(eng::Renderer& renderer,
                                      const WeaponViewmodelDef& definition)
{
    if (!mNode.valid())
        return;
    renderer.setPosition(mNode, definition.attachOffset);
    renderer.setOrientation(mNode,
                            degreesOrientation(definition.attachRotationDegrees));
    renderer.setScale(mNode, glm::vec3(definition.attachScale));
}

void WeaponViewmodel::setVisible(eng::Renderer& renderer, bool show)
{
    if (mNode.valid())
        renderer.setNodeVisible(mNode, show);
}

void WeaponViewmodel::setEnchantment(
    eng::Renderer& renderer, const std::optional<eng::EnchantmentDesc>& glow)
{
    for (eng::NodeHandle node : mGlowNodes) {
        if (!node.valid())
            continue;
        if (glow)
            renderer.setNodeEnchantment(node, *glow);
        else
            renderer.clearNodeEnchantment(node);
    }
}

} // namespace game
