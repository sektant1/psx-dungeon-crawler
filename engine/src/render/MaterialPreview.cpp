#include <eng/render/MaterialPreview.h>

#if !defined(ENG_RENDERER_RHI)
#include <OgreMaterial.h>
#include <OgreMaterialManager.h>
#include <OgrePass.h>
#include <OgreTechnique.h>
#include <OgreTextureUnitState.h>
#endif

#include <eng/LightDesc.h>
#include <eng/Log.h>
#include <eng/Primitive.h>
#include <eng/Renderer.h>
#include <eng/assets/AssetRoot.h>

#include <glm/gtc/quaternion.hpp>

#include <cstdio>
#include <fstream>
#include <string>

namespace eng {
namespace {

// The rig's three light colours, in the order buildSphereRig() creates them.
// Named once so setVisible can restore them after switching them off.
constexpr glm::vec3 kKey{1.0f, 0.95f, 0.88f};
constexpr glm::vec3 kFill{0.62f, 0.72f, 0.95f};
constexpr glm::vec3 kRim{0.9f, 0.92f, 1.0f};
constexpr float kKeyPower = 2.2f;
constexpr float kFillPower = 1.0f;
constexpr float kRimPower = 1.6f;

// Quad mode backdrop: a flat neutral grey, mid-dark so a bright emissive icon
// blooms against it and a dark one still has a silhouette. Deliberately not the
// sphere stage's near-black -- that was chosen to serve a rim light this rig
// does not have.
constexpr glm::vec3 kQuadBackdrop{0.20f, 0.205f, 0.215f};

// The quad's world size, chosen so it fills roughly the same share of the frame
// at the stage camera distance as the one-metre sphere it replaces.
constexpr float kQuadSize = 1.6f;
constexpr float kThumbQuadSize = 1.0f;

// Where the swatch rig lives: a large offset rather than the origin, because
// its point lights would otherwise spill onto whatever level is loaded there.
constexpr glm::vec3 kThumbOrigin{0.0f, -1000.0f, 0.0f};
constexpr glm::vec3 kThumbCameraOffset{0.0f, 0.28f, 1.5f};

// Materials whose vertex program reads a per-instance stream. These cannot be
// drawn on a plain mesh at all: particle.vert takes the world position, size,
// colour and flipbook frame as vertex attributes the batch binds per instance
// (uv1/uv2/uv3), so on an ordinary quad uv1.w -- the world size -- arrives as
// zero, every corner collapses to a point, and the "preview" is a one-pixel
// line. That is what the quad rig used to show for every particle material,
// which was two of the three patterns it existed to serve.
bool needsInstanceStream(const std::string& material)
{
#if defined(ENG_RENDERER_RHI)
    (void)material;
    return false;
#else
    Ogre::MaterialPtr mat =
        Ogre::MaterialManager::getSingleton().getByName(material);
    if (!mat || !mat->getTechnique(0) || !mat->getTechnique(0)->getPass(0))
        return false;
    const Ogre::Pass* pass = mat->getTechnique(0)->getPass(0);
    if (!pass->hasVertexProgram())
        return false;
    const std::string& vs = pass->getVertexProgramName();
    return vs == "Particle_VS" || vs == "Particles/SpriteVS" ||
           vs == "Particles/VoxelVS" || vs == "Decals/QuadVS";
#endif
}

// A preview stand-in for one of those: the same texture and blend on the
// engine's plain sprite program, which takes only what a mesh actually has.
// The atlas parameters are carried across, so a flipbook still plays -- the
// preview shows the art animating, which is the whole point of staging an
// icon.
std::string instancedPreviewMaterial(const std::string& material)
{
#if defined(ENG_RENDERER_RHI)
    return material;
#else
    auto& mm = Ogre::MaterialManager::getSingleton();
    const std::string name = "__Preview/Quad/" + material;
    if (mm.getByName(name))
        return name;

    Ogre::MaterialPtr source = mm.getByName(material);
    Ogre::MaterialPtr base = mm.getByName("Engine/Psx/PreviewSprite");
    if (!source || !base)
        return material;
    Ogre::MaterialPtr clone = base->clone(name);
    Ogre::Pass* from = source->getTechnique(0)->getPass(0);
    Ogre::Pass* to = clone->getTechnique(0)->getPass(0);
    if (!from || !to)
        return material;

    if (from->getNumTextureUnitStates() > 0 &&
        to->getNumTextureUnitStates() > 0)
        to->getTextureUnitState(0)->setTextureName(
            from->getTextureUnitState(0)->getTextureName());
    to->setSceneBlending(from->getSourceBlendFactor(),
                         from->getDestBlendFactor());

    // Carry the flipbook across when the source declares one, so an atlas
    // material previews as the animation the game plays rather than as frame 0.
    if (from->hasVertexProgram() && to->hasVertexProgram()) {
        const auto& src = from->getVertexProgramParameters();
        auto dst = to->getVertexProgramParameters();
        const auto carry = [&](const char* fromName, const char* toName) {
            if (!src->_findNamedConstantDefinition(fromName, false))
                return;
            const Ogre::GpuConstantDefinition& def =
                src->getConstantDefinition(fromName);
            const float* v = src->getFloatPointer(def.physicalIndex);
            if (def.elementSize >= 2)
                dst->setNamedConstant(toName, Ogre::Vector2(v[0], v[1]));
            else
                dst->setNamedConstant(toName, v[0]);
        };
        carry("atlasGrid", "spriteGrid");
        carry("atlasFrames", "spriteFrameCount");
        carry("atlasFps", "spriteFps");
    }
    return name;
#endif
}

// A plane primitive lies in XZ with its front face pointing +Y, so a quad that
// faces a camera is that plane rotated until its normal points at the eye.
//
// thickness 0 is load-bearing. The default plane is a slab: two faces and four
// rims, and every one of those carries the full 0..1 UV. On a sprite material
// that drew the whole icon a second time, mirrored and coplanar, plus four
// hairline copies squashed along the borders of the preview -- the "smashed"
// edges around the swatch. A flat plane is one quad.
MeshHandle quadMesh(Renderer& renderer, float size)
{
    PrimitiveMeshDesc quad;
    quad.kind = PrimitiveKind::Plane;
    quad.size = {size, 1.0f, size};
    quad.thickness = 0.0f;
    return renderer.createPrimitiveMesh(quad);
}

glm::quat facing(glm::vec3 subject, glm::vec3 eye)
{
    const glm::vec3 dir = eye - subject;
    const float len = glm::length(dir);
    if (len < 1e-5f)
        return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    const glm::vec3 normal = dir / len;
    // Matching only the normal leaves roll unconstrained. The shortest-arc
    // quaternion then presents an icon as a diamond at the studio camera's
    // three-quarter angle. Build the whole plane basis instead: local +Y faces
    // the eye and local +Z remains screen-up.
    glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f) -
                   normal * glm::dot(glm::vec3(0.0f, 1.0f, 0.0f), normal);
    if (glm::dot(up, up) < 1e-6f)
        up = glm::vec3(0.0f, 0.0f, 1.0f);
    else
        up = glm::normalize(up);
    const glm::vec3 right = glm::normalize(glm::cross(normal, up));
    return glm::normalize(glm::quat_cast(glm::mat3(right, normal, up)));
}

// The preview catalog is an *engine* asset, so the lookup is pack-qualified:
// it must reach past a higher-priority game or editor pack rather than take
// whichever mount happens to have a file by that name. This used to derive the
// path by climbing two directories out of the game's asset dir, which stopped
// being true the moment the editor ran against anything but the source tree.
std::string previewCatalogPath()
{
    return assets::resolve("config/material_preview.toml").string();
}

// A one-key schema does not justify pulling toml++ into the editor target,
// which would mean editing CMakeLists.txt. This reads `quad = ["A", "B/*"]` and
// nothing else, ignoring comments and blank lines.
std::vector<std::string> parseQuadList(std::istream& in)
{
    std::vector<std::string> patterns;
    std::string text;
    std::string line;
    bool inArray = false;
    while (std::getline(in, line)) {
        const std::size_t hash = line.find('#');
        if (hash != std::string::npos)
            line.erase(hash);
        if (!inArray) {
            const std::size_t key = line.find("quad");
            const std::size_t open = line.find('[');
            if (key == std::string::npos || open == std::string::npos)
                continue;
            inArray = true;
            text += line.substr(open + 1);
        }
        else {
            text += line;
        }
        if (text.find(']') != std::string::npos)
            break;
    }
    const std::size_t close = text.find(']');
    if (close != std::string::npos)
        text.erase(close);

    std::size_t i = 0;
    while (i < text.size()) {
        const std::size_t open = text.find('"', i);
        if (open == std::string::npos)
            break;
        const std::size_t end = text.find('"', open + 1);
        if (end == std::string::npos)
            break;
        patterns.push_back(text.substr(open + 1, end - open - 1));
        i = end + 1;
    }
    return patterns;
}

} // namespace

bool StagePreviewCatalog::matches(const std::string& pattern,
                                  const std::string& name)
{
    if (pattern.empty())
        return false;
    if (pattern.back() != '*')
        return pattern == name;
    const std::string prefix = pattern.substr(0, pattern.size() - 1);
    return name.size() >= prefix.size() &&
           name.compare(0, prefix.size(), prefix) == 0;
}

void StagePreviewCatalog::load(const std::string& path)
{
    mQuadPatterns.clear();
    std::ifstream in(path);
    if (!in) {
        // Not an error. With no file every material stages as a sphere, which
        // is precisely the behaviour that existed before this file did.
        log::info("material stage: no %s, every material previews as a sphere",
                  path.c_str());
        return;
    }
    mQuadPatterns = parseQuadList(in);
}

StagePreview StagePreviewCatalog::modeFor(const std::string& material) const
{
    for (const std::string& pattern : mQuadPatterns) {
        if (matches(pattern, material))
            return StagePreview::Quad;
    }

    // Names are not reliable metadata. Game/Spells/FireballImpact and
    // Engine/Particles/SpriteAdditive use the same instanced shader but only
    // one happens to match the hand-authored prefix list. Inspect the loaded
    // program so newly added animated/instanced materials get a useful preview
    // without another catalog edit.
#if !defined(ENG_RENDERER_RHI)
    Ogre::MaterialPtr mat =
        Ogre::MaterialManager::getSingleton().getByName(material);
    if (mat && mat->getTechnique(0) && mat->getTechnique(0)->getPass(0)) {
        const Ogre::Pass* pass = mat->getTechnique(0)->getPass(0);
        if (pass->hasVertexProgram()) {
            const std::string& vs = pass->getVertexProgramName();
            const auto starts = [&vs](const char* prefix) {
                return vs.rfind(prefix, 0) == 0;
            };
            if (starts("Particle") || starts("Particles/") ||
                starts("Sprite/") || starts("Decals/") ||
                starts("PixelVfx/"))
                return StagePreview::Quad;
        }
    }
#endif
    return StagePreview::Sphere;
}

void MaterialPreview::build(Renderer& renderer)
{
    if (built())
        return;
    if (!mCatalogLoaded) {
        mCatalog.load(previewCatalogPath());
        mCatalogLoaded = true;
    }
    mMode = mCatalog.modeFor(mMaterial);
    if (mMode == StagePreview::Quad)
        buildQuadRig(renderer);
    else
        buildSphereRig(renderer);
}

void MaterialPreview::buildSphereRig(Renderer& renderer)
{
    // --- the subject -------------------------------------------------------
    // A one-metre sphere at eye height. Dense enough that the silhouette reads
    // as round rather than faceted: a coarse sphere makes every material look
    // like it has shading artefacts it does not have.
    PrimitiveMeshDesc sphere;
    sphere.kind = PrimitiveKind::Sphere;
    sphere.radius = 0.5f;
    sphere.rings = 32;
    sphere.segments = 48;
    mSubject = renderer.createNode(kRootNode, {0.0f, 1.0f, 0.0f},
                                   "material_stage_sphere");
    renderer.attachMesh(mSubject, renderer.createPrimitiveMesh(sphere),
                        mMaterial, true);
    renderer.setOrientation(mSubject,
                            glm::angleAxis(mSpin, glm::vec3(0, 1, 0)));

    // --- the floor ---------------------------------------------------------
    // Big enough that its edge never enters frame at working distances. The
    // pattern is generated from world position rather than sampled from a
    // texture, so the size costs nothing and the squares stay sharp at any
    // camera distance.
    PrimitiveMeshDesc floor;
    floor.kind = PrimitiveKind::Plane;
    floor.size = {400.0f, 1.0f, 400.0f};
    mFloor = renderer.createNode(kRootNode, {0.0f, 0.0f, 0.0f},
                                 "material_stage_floor");
    renderer.attachMesh(mFloor, renderer.createPrimitiveMesh(floor),
                        mFloorMaterial, false);

    // --- three-point lighting ----------------------------------------------
    // The standard studio rig, and it is standard because it separates the
    // three things you need to see at once: form (key), detail in shadow
    // (fill), and silhouette against the background (rim).
    const auto light = [&](glm::vec3 position, glm::vec3 colour, float range) {
        LightDesc desc;
        desc.type = LightDesc::Type::Point;
        desc.colour = colour;
        desc.range = range;
        const NodeHandle node = renderer.createNode(kRootNode, position);
        mLightNodes.push_back(node);
        mLights.push_back(renderer.attachLight(node, desc));
    };
    light({3.0f, 3.6f, 2.6f}, kKey * kKeyPower, 18.0f);
    light({-3.2f, 1.8f, 2.0f}, kFill * kFillPower, 16.0f);
    light({-1.0f, 2.4f, -3.4f}, kRim * kRimPower, 15.0f);

    if (!mVisible)
        setVisible(renderer, false);
}

// Which material the quad rig actually attaches: the real one, or a stand-in
// when the real one cannot be drawn on a mesh.
std::string MaterialPreview::quadMaterial(const std::string& material)
{
    return needsInstanceStream(material) ? instancedPreviewMaterial(material)
                                         : material;
}

// The quad rig: subject only. No floor, because a checkerboard behind a
// transparent icon reads as part of the icon; no lights, because an animated
// shader is emissive and a key light would just recolour it; no shadow, because
// there is nothing for it to fall on.
void MaterialPreview::buildQuadRig(Renderer& renderer)
{
    mSubject =
        renderer.createNode(kRootNode, focusPoint(), "material_stage_quad");
    renderer.attachMesh(mSubject, quadMesh(renderer, kQuadSize),
                        quadMaterial(mMaterial), false);
    renderer.setOrientation(mSubject, facing(focusPoint(), cameraPosition()));
    if (!mVisible)
        setVisible(renderer, false);
}

// The swatch rig, in whichever mode the hovered material asks for.
void MaterialPreview::buildThumbnailRig(Renderer& renderer)
{
    if (mThumbMode == StagePreview::Quad) {
        mThumbSubject = renderer.createNode(kRootNode, kThumbOrigin,
                                            "material_thumbnail_quad");
        renderer.attachMesh(mThumbSubject, quadMesh(renderer, kThumbQuadSize),
                            quadMaterial(mThumbMaterial), false);
        renderer.setOrientation(
            mThumbSubject,
            facing(kThumbOrigin, kThumbOrigin + kThumbCameraOffset));
        renderer.setNodeThumbnailOnly(mThumbSubject, true);
        return;
    }

    PrimitiveMeshDesc sphere;
    sphere.kind = PrimitiveKind::Sphere;
    sphere.radius = 0.5f;
    sphere.rings = 24;
    sphere.segments = 32;
    mThumbSubject = renderer.createNode(kRootNode, kThumbOrigin,
                                        "material_thumbnail_sphere");
    renderer.attachMesh(mThumbSubject, renderer.createPrimitiveMesh(sphere),
                        mThumbMaterial, false);
    // Visible in the thumbnail target and nowhere else.
    renderer.setNodeThumbnailOnly(mThumbSubject, true);
    renderer.setOrientation(mThumbSubject,
                            glm::angleAxis(mThumbSpin, glm::vec3(0, 1, 0)));

    // A compact three-point rig, scaled to a subject half a metre across. Same
    // reasoning as the big stage: key describes the form, fill keeps the shadow
    // side readable, rim separates it from the background.
    const auto light = [&](glm::vec3 offset, glm::vec3 colour, float range) {
        LightDesc desc;
        desc.type = LightDesc::Type::Point;
        desc.colour = colour;
        desc.range = range;
        const NodeHandle node =
            renderer.createNode(kRootNode, kThumbOrigin + offset);
        mThumbLightNodes.push_back(node);
        renderer.attachLight(node, desc);
    };
    light({1.3f, 1.5f, 1.6f}, kKey * 1.9f, 6.0f);
    light({-1.5f, 0.6f, 1.2f}, kFill * 0.9f, 6.0f);
    light({-0.6f, 1.0f, -1.6f}, kRim * 1.3f, 5.0f);
}

void MaterialPreview::destroyThumbnailRig(Renderer& renderer)
{
    if (mThumbSubject.valid())
        renderer.destroyNode(mThumbSubject);
    mThumbSubject = NodeHandle{};
    for (const NodeHandle node : mThumbLightNodes)
        renderer.destroyNode(node);
    mThumbLightNodes.clear();
}

void MaterialPreview::buildThumbnail(Renderer& renderer, int size)
{
    if (thumbnailBuilt())
        return;
    if (!mCatalogLoaded) {
        mCatalog.load(previewCatalogPath());
        mCatalogLoaded = true;
    }
    mThumbSize = size;
    renderer.enableMaterialThumbnail(size);

    mThumbMaterial = mMaterial;
    mThumbMode = mCatalog.modeFor(mThumbMaterial);
    buildThumbnailRig(renderer);

    // Three-quarter view, framed so the subject nearly fills the square. The
    // same pose serves both modes: the quad is built facing it.
    renderer.setMaterialThumbnailCamera(
        kThumbOrigin + kThumbCameraOffset,
        glm::angleAxis(glm::radians(-10.0f), glm::vec3(1, 0, 0)), 45.0f);
}

void MaterialPreview::setThumbnailMaterial(Renderer& renderer,
                                           const std::string& material)
{
    if (material.empty() || !thumbnailBuilt() || material == mThumbMaterial)
        return;
    mThumbMaterial = material;

    const StagePreview mode = mCatalog.modeFor(material);
    if (mode != mThumbMode) {
        // A mode change swaps the geometry and the lights, so the swatch rig is
        // destroyed outright rather than patched. Patching is how the swatch
        // sphere has previously leaked into the level: half the state gets
        // updated and the rest keeps the old rig's assumptions.
        mThumbMode = mode;
        destroyThumbnailRig(renderer);
        buildThumbnailRig(renderer);
        return;
    }

    renderer.setNodeMaterial(mThumbSubject, mThumbMode == StagePreview::Quad
                                                ? quadMaterial(material)
                                                : material);
    // setNodeMaterial re-attaches, which resets the visibility flags the
    // thumbnail target filters on. Without this the swatch subject reappears in
    // the level the moment you preview a material.
    renderer.setNodeThumbnailOnly(mThumbSubject, true);
}

void MaterialPreview::spinThumbnail(Renderer& renderer, float radians)
{
    mThumbSpin = radians;
    // A quad turned on the turntable would present its edge and then its back.
    // The swatch keeps facing the camera; the animation is the motion here.
    if (mThumbSubject.valid() && mThumbMode == StagePreview::Sphere)
        renderer.setOrientation(mThumbSubject,
                                glm::angleAxis(radians, glm::vec3(0, 1, 0)));
}

void MaterialPreview::setThumbnailVisible(Renderer& renderer, bool visible)
{
    if (mThumbSubject.valid())
        renderer.setNodeVisible(mThumbSubject, visible);
    for (const NodeHandle node : mThumbLightNodes)
        renderer.setNodeVisible(node, visible);
}

void MaterialPreview::applyEnvironment(Renderer& renderer) const
{
    if (mMode == StagePreview::Quad) {
        // Full ambient with no lights is how you say "unlit" to the PSX
        // shaders: the lighting term collapses to the albedo, so a procedural
        // shader shows exactly the colours it computes. The backdrop is flat
        // and neutral so nothing behind the icon reads as part of it.
        renderer.setAmbient({1.0f, 1.0f, 1.0f});
        renderer.setBackground(kQuadBackdrop);
        renderer.setFog(kQuadBackdrop, 0.0f);
        renderer.setEditorViewportBackground(kQuadBackdrop);
        return;
    }
    // Low ambient: the three-point rig should be doing the lighting, not a flat
    // fill that washes out everything the key and rim are there to show.
    renderer.setAmbient({0.16f, 0.17f, 0.20f});
    renderer.setBackground({0.055f, 0.06f, 0.075f});
    renderer.setFog({0.055f, 0.06f, 0.075f},
                    0.0f); // fog would tint the subject
    renderer.setEditorViewportBackground({0.055f, 0.06f, 0.075f});
}

void MaterialPreview::destroyStage(Renderer& renderer)
{
    if (mSubject.valid())
        renderer.destroyNode(mSubject);
    mSubject = NodeHandle{};
    if (mFloor.valid())
        renderer.destroyNode(mFloor);
    mFloor = NodeHandle{};
    for (const NodeHandle node : mLightNodes)
        renderer.destroyNode(node);
    mLightNodes.clear();
    mLights.clear();
}

void MaterialPreview::destroy(Renderer& renderer)
{
    destroyThumbnailRig(renderer);
    destroyStage(renderer);
}

void MaterialPreview::setMaterial(Renderer& renderer,
                                  const std::string& material)
{
    if (material.empty() || !built())
        return;
    mMaterial = material;

    const StagePreview mode = mCatalog.modeFor(material);
    if (mode != mMode) {
        // Full teardown and rebuild. The two rigs differ in geometry, in
        // whether a floor exists, and in whether lights exist, so there is no
        // subset of state worth preserving -- and leaving a point light behind
        // would light the level the editor returns to.
        mMode = mode;
        destroyStage(renderer);
        if (mMode == StagePreview::Quad)
            buildQuadRig(renderer);
        else
            buildSphereRig(renderer);
        applyEnvironment(renderer);
        return;
    }

    renderer.setNodeMaterial(mSubject, material);
}

void MaterialPreview::setFloorMaterial(Renderer& renderer,
                                       const std::string& material)
{
    if (material.empty())
        return;
    // Remembered even in quad mode, which has no floor, so the choice survives
    // a round trip through an animated material.
    mFloorMaterial = material;
    if (mFloor.valid())
        renderer.setNodeMaterial(mFloor, material);
}

void MaterialPreview::setVisible(Renderer& renderer, bool visible)
{
    mVisible = visible;
    if (mSubject.valid())
        renderer.setNodeVisible(mSubject, visible);
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

void MaterialPreview::setSpin(Renderer& renderer, float radians)
{
    mSpin = radians;
    // See spinThumbnail: the turntable is a sphere affordance. The value is
    // still stored so the gizmo's rotation readout stays continuous.
    if (mSubject.valid() && mMode == StagePreview::Sphere)
        renderer.setOrientation(mSubject,
                                glm::angleAxis(radians, glm::vec3(0, 1, 0)));
}

} // namespace eng
