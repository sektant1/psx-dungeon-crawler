#include "MaterialStage.h"

#include <eng/LightDesc.h>
#include <eng/Primitive.h>
#include <eng/Renderer.h>

#include <glm/gtc/quaternion.hpp>

namespace ed {
namespace {

// The rig's three light colours, in the order build() creates them. Named once
// so setVisible can restore them after switching them off.
constexpr glm::vec3 kKey{1.0f, 0.95f, 0.88f};
constexpr glm::vec3 kFill{0.62f, 0.72f, 0.95f};
constexpr glm::vec3 kRim{0.9f, 0.92f, 1.0f};
constexpr float kKeyPower = 2.2f;
constexpr float kFillPower = 1.0f;
constexpr float kRimPower = 1.6f;

} // namespace

void MaterialStage::build(eng::Renderer& renderer)
{
    if (built())
        return;

    // --- the subject -------------------------------------------------------
    // A one-metre sphere at eye height. Dense enough that the silhouette reads
    // as round rather than faceted: a coarse sphere makes every material look
    // like it has shading artefacts it does not have.
    eng::PrimitiveMeshDesc sphere;
    sphere.kind = eng::PrimitiveKind::Sphere;
    sphere.radius = 0.5f;
    sphere.rings = 32;
    sphere.segments = 48;
    mSphere = renderer.createNode(eng::kRootNode, {0.0f, 1.0f, 0.0f},
                                  "material_stage_sphere");
    renderer.attachMesh(mSphere, renderer.createPrimitiveMesh(sphere), mMaterial,
                        true);

    // --- the floor ---------------------------------------------------------
    // Big enough that its edge never enters frame at working distances. The
    // pattern is generated from world position rather than sampled from a
    // texture, so the size costs nothing and the squares stay sharp at any
    // camera distance.
    eng::PrimitiveMeshDesc floor;
    floor.kind = eng::PrimitiveKind::Plane;
    floor.size = {400.0f, 1.0f, 400.0f};
    mFloor = renderer.createNode(eng::kRootNode, {0.0f, 0.0f, 0.0f},
                                 "material_stage_floor");
    renderer.attachMesh(mFloor, renderer.createPrimitiveMesh(floor),
                        mFloorMaterial, false);

    // --- three-point lighting ----------------------------------------------
    // The standard studio rig, and it is standard because it separates the
    // three things you need to see at once: form (key), detail in shadow
    // (fill), and silhouette against the background (rim).
    const auto light = [&](glm::vec3 position, glm::vec3 colour, float range) {
        eng::LightDesc desc;
        desc.type = eng::LightDesc::Type::Point;
        desc.colour = colour;
        desc.range = range;
        const eng::NodeHandle node = renderer.createNode(eng::kRootNode, position);
        mLightNodes.push_back(node);
        mLights.push_back(renderer.attachLight(node, desc));
    };
    light({3.0f, 3.6f, 2.6f}, kKey * kKeyPower, 18.0f);
    light({-3.2f, 1.8f, 2.0f}, kFill * kFillPower, 16.0f);
    light({-1.0f, 2.4f, -3.4f}, kRim * kRimPower, 15.0f);
}

// The swatch: its own sphere, its own lights, its own render target. Lives at a
// large offset rather than at the origin -- its lights are point lights, and at
// the origin they would spill onto whatever level is loaded there.
void MaterialStage::buildThumbnail(eng::Renderer& renderer, int size)
{
    if (thumbnailBuilt())
        return;
    renderer.enableMaterialThumbnail(size);

    const glm::vec3 origin{0.0f, -1000.0f, 0.0f};

    eng::PrimitiveMeshDesc sphere;
    sphere.kind = eng::PrimitiveKind::Sphere;
    sphere.radius = 0.5f;
    sphere.rings = 24;
    sphere.segments = 32;
    mThumbSphere = renderer.createNode(eng::kRootNode, origin,
                                       "material_thumbnail_sphere");
    renderer.attachMesh(mThumbSphere, renderer.createPrimitiveMesh(sphere),
                        mMaterial, false);
    // Visible in the thumbnail target and nowhere else.
    renderer.setNodeThumbnailOnly(mThumbSphere, true);

    // A compact three-point rig, scaled to a subject half a metre across. Same
    // reasoning as the big stage: key describes the form, fill keeps the shadow
    // side readable, rim separates it from the background.
    const auto light = [&](glm::vec3 offset, glm::vec3 colour, float range) {
        eng::LightDesc desc;
        desc.type = eng::LightDesc::Type::Point;
        desc.colour = colour;
        desc.range = range;
        const eng::NodeHandle node = renderer.createNode(eng::kRootNode,
                                                         origin + offset);
        mThumbLightNodes.push_back(node);
        renderer.attachLight(node, desc);
    };
    light({1.3f, 1.5f, 1.6f}, kKey * 1.9f, 6.0f);
    light({-1.5f, 0.6f, 1.2f}, kFill * 0.9f, 6.0f);
    light({-0.6f, 1.0f, -1.6f}, kRim * 1.3f, 5.0f);

    // Three-quarter view, framed so the sphere nearly fills the square.
    renderer.setMaterialThumbnailCamera(origin + glm::vec3(0.0f, 0.28f, 1.5f),
                                        glm::angleAxis(glm::radians(-10.0f),
                                                       glm::vec3(1, 0, 0)),
                                        45.0f);
    mThumbMaterial = mMaterial;
}

void MaterialStage::setThumbnailMaterial(eng::Renderer& renderer,
                                         const std::string& material)
{
    if (material.empty() || !thumbnailBuilt() || material == mThumbMaterial)
        return;
    mThumbMaterial = material;
    renderer.setNodeMaterial(mThumbSphere, material);
    // setNodeMaterial re-attaches, which resets the visibility flags the
    // thumbnail target filters on. Without this the swatch sphere reappears in
    // the level the moment you preview a material.
    renderer.setNodeThumbnailOnly(mThumbSphere, true);
}

void MaterialStage::spinThumbnail(eng::Renderer& renderer, float radians)
{
    mThumbSpin = radians;
    if (mThumbSphere.valid())
        renderer.setOrientation(mThumbSphere,
                                glm::angleAxis(radians, glm::vec3(0, 1, 0)));
}

void MaterialStage::applyEnvironment(eng::Renderer& renderer) const
{
    // Low ambient: the three-point rig should be doing the lighting, not a flat
    // fill that washes out everything the key and rim are there to show.
    renderer.setAmbient({0.16f, 0.17f, 0.20f});
    renderer.setBackground({0.055f, 0.06f, 0.075f});
    renderer.setFog({0.055f, 0.06f, 0.075f}, 0.0f); // fog would tint the subject
    renderer.setEditorViewportBackground({0.055f, 0.06f, 0.075f});
}

void MaterialStage::destroy(eng::Renderer& renderer)
{
    if (mSphere.valid())
        renderer.destroyNode(mSphere);
    if (mThumbSphere.valid())
        renderer.destroyNode(mThumbSphere);
    for (const eng::NodeHandle node : mThumbLightNodes)
        renderer.destroyNode(node);
    mThumbLightNodes.clear();
    mThumbSphere = eng::NodeHandle{};
    if (mFloor.valid())
        renderer.destroyNode(mFloor);
    for (const eng::NodeHandle node : mLightNodes)
        renderer.destroyNode(node);
    mLightNodes.clear();
    mLights.clear();
    mSphere = eng::NodeHandle{};
    mFloor = eng::NodeHandle{};
}

void MaterialStage::setMaterial(eng::Renderer& renderer,
                                const std::string& material)
{
    if (material.empty() || !built())
        return;
    mMaterial = material;
    renderer.setNodeMaterial(mSphere, material);
}

void MaterialStage::setFloorMaterial(eng::Renderer& renderer,
                                     const std::string& material)
{
    if (material.empty() || !mFloor.valid())
        return;
    mFloorMaterial = material;
    renderer.setNodeMaterial(mFloor, material);
}

void MaterialStage::setVisible(eng::Renderer& renderer, bool visible)
{
    if (mSphere.valid())
        renderer.setNodeVisible(mSphere, visible);
    if (mFloor.valid())
        renderer.setNodeVisible(mFloor, visible);

    // Lights are switched by colour rather than by node visibility: an unlit
    // stage would otherwise keep lighting the level behind it.
    const glm::vec3 colours[3] = {kKey * kKeyPower, kFill * kFillPower,
                                  kRim * kRimPower};
    for (std::size_t i = 0; i < mLights.size(); ++i) {
        renderer.setLightColour(mLights[i], visible && i < 3 ? colours[i]
                                                             : glm::vec3(0.0f));
    }
}

void MaterialStage::setSpin(eng::Renderer& renderer, float radians)
{
    mSpin = radians;
    if (mSphere.valid())
        renderer.setOrientation(mSphere,
                                glm::angleAxis(radians, glm::vec3(0, 1, 0)));
}

} // namespace ed
