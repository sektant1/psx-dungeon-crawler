#include "MaterialStage.h"

#include <eng/LightDesc.h>
#include <eng/Log.h>
#include <eng/Primitive.h>
#include <eng/Renderer.h>

#include <glm/gtc/quaternion.hpp>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace ed {
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

// Where the swatch rig lives: a large offset rather than the origin, because its
// point lights would otherwise spill onto whatever level is loaded there.
constexpr glm::vec3 kThumbOrigin{0.0f, -1000.0f, 0.0f};
constexpr glm::vec3 kThumbCameraOffset{0.0f, 0.28f, 1.5f};

// Rotation taking direction `from` onto direction `to`, both assumed unit
// length. Written out rather than pulled from glm's gtx extensions so this file
// keeps to the same glm surface the rest of the editor uses.
glm::quat rotationBetween(glm::vec3 from, glm::vec3 to)
{
    const float d = glm::dot(from, to);
    if (d > 0.99999f)
        return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    if (d < -0.99999f) {
        // Antiparallel: any axis perpendicular to `from` is a valid 180 turn.
        glm::vec3 axis = glm::cross(glm::vec3(0, 0, 1), from);
        if (glm::dot(axis, axis) < 1e-6f)
            axis = glm::cross(glm::vec3(1, 0, 0), from);
        return glm::angleAxis(3.14159265f, glm::normalize(axis));
    }
    const glm::vec3 axis = glm::cross(from, to);
    return glm::normalize(glm::quat(1.0f + d, axis.x, axis.y, axis.z));
}

// A plane primitive lies in XZ with its front face pointing +Y, so a quad that
// faces a camera is that plane rotated until its normal points at the eye.
eng::MeshHandle quadMesh(eng::Renderer& renderer, float size)
{
    eng::PrimitiveMeshDesc quad;
    quad.kind = eng::PrimitiveKind::Plane;
    quad.size = {size, 1.0f, size};
    return renderer.createPrimitiveMesh(quad);
}

glm::quat facing(glm::vec3 subject, glm::vec3 eye)
{
    const glm::vec3 dir = eye - subject;
    const float len = glm::length(dir);
    if (len < 1e-5f)
        return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    return rotationBetween(glm::vec3(0, 1, 0), dir / len);
}

// Locates engine/assets/material_preview.toml. APP_ASSET_DIR is the *game*
// asset directory (the editor's own define); the engine's assets sit beside it
// two levels up in the source tree, and the engine asset dir is not exported to
// this target as a define. The working-directory fallback covers running the
// editor from the repository root.
std::string previewCatalogPath()
{
    namespace fs = std::filesystem;
    std::error_code ec;
#ifdef ENG_ASSET_DIR
    // The catalog is an engine asset, so ask for it directly when the build
    // told us where the engine assets live.
    const fs::path direct = fs::path(ENG_ASSET_DIR) / "material_preview.toml";
    if (fs::exists(direct, ec))
        return direct.string();
#endif
#ifdef APP_ASSET_DIR
    const fs::path fromAssets = fs::path(APP_ASSET_DIR) / ".." / ".." /
                                "engine" / "assets" / "material_preview.toml";
    const fs::path resolved = fs::weakly_canonical(fromAssets, ec);
    const fs::path candidate = ec ? fromAssets : resolved;
    if (fs::exists(candidate, ec))
        return candidate.string();
#endif
    return "engine/assets/material_preview.toml";
}

// A one-key schema does not justify pulling toml++ into the editor target, which
// would mean editing CMakeLists.txt. This reads `quad = ["A", "B/*"]` and
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
        } else {
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
        // Not an error. With no file every material stages as a sphere, which is
        // precisely the behaviour that existed before this file did.
        eng::log::info("material stage: no %s, every material previews as a sphere",
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
    return StagePreview::Sphere;
}

void MaterialStage::build(eng::Renderer& renderer)
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

void MaterialStage::buildSphereRig(eng::Renderer& renderer)
{
    // --- the subject -------------------------------------------------------
    // A one-metre sphere at eye height. Dense enough that the silhouette reads
    // as round rather than faceted: a coarse sphere makes every material look
    // like it has shading artefacts it does not have.
    eng::PrimitiveMeshDesc sphere;
    sphere.kind = eng::PrimitiveKind::Sphere;
    sphere.radius = 0.5f;
    sphere.rings = 32;
    sphere.segments = 48;
    mSubject = renderer.createNode(eng::kRootNode, {0.0f, 1.0f, 0.0f},
                                   "material_stage_sphere");
    renderer.attachMesh(mSubject, renderer.createPrimitiveMesh(sphere), mMaterial,
                        true);
    renderer.setOrientation(mSubject,
                            glm::angleAxis(mSpin, glm::vec3(0, 1, 0)));

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

    if (!mVisible)
        setVisible(renderer, false);
}

// The quad rig: subject only. No floor, because a checkerboard behind a
// transparent icon reads as part of the icon; no lights, because an animated
// shader is emissive and a key light would just recolour it; no shadow, because
// there is nothing for it to fall on.
void MaterialStage::buildQuadRig(eng::Renderer& renderer)
{
    mSubject = renderer.createNode(eng::kRootNode, focusPoint(),
                                   "material_stage_quad");
    renderer.attachMesh(mSubject, quadMesh(renderer, kQuadSize), mMaterial,
                        false);
    renderer.setOrientation(mSubject, facing(focusPoint(), cameraPosition()));
    if (!mVisible)
        setVisible(renderer, false);
}

// The swatch rig, in whichever mode the hovered material asks for.
void MaterialStage::buildThumbnailRig(eng::Renderer& renderer)
{
    if (mThumbMode == StagePreview::Quad) {
        mThumbSubject = renderer.createNode(eng::kRootNode, kThumbOrigin,
                                            "material_thumbnail_quad");
        renderer.attachMesh(mThumbSubject, quadMesh(renderer, kThumbQuadSize),
                            mThumbMaterial, false);
        renderer.setOrientation(
            mThumbSubject, facing(kThumbOrigin, kThumbOrigin + kThumbCameraOffset));
        renderer.setNodeThumbnailOnly(mThumbSubject, true);
        return;
    }

    eng::PrimitiveMeshDesc sphere;
    sphere.kind = eng::PrimitiveKind::Sphere;
    sphere.radius = 0.5f;
    sphere.rings = 24;
    sphere.segments = 32;
    mThumbSubject = renderer.createNode(eng::kRootNode, kThumbOrigin,
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
        eng::LightDesc desc;
        desc.type = eng::LightDesc::Type::Point;
        desc.colour = colour;
        desc.range = range;
        const eng::NodeHandle node = renderer.createNode(eng::kRootNode,
                                                         kThumbOrigin + offset);
        mThumbLightNodes.push_back(node);
        renderer.attachLight(node, desc);
    };
    light({1.3f, 1.5f, 1.6f}, kKey * 1.9f, 6.0f);
    light({-1.5f, 0.6f, 1.2f}, kFill * 0.9f, 6.0f);
    light({-0.6f, 1.0f, -1.6f}, kRim * 1.3f, 5.0f);
}

void MaterialStage::destroyThumbnailRig(eng::Renderer& renderer)
{
    if (mThumbSubject.valid())
        renderer.destroyNode(mThumbSubject);
    mThumbSubject = eng::NodeHandle{};
    for (const eng::NodeHandle node : mThumbLightNodes)
        renderer.destroyNode(node);
    mThumbLightNodes.clear();
}

void MaterialStage::buildThumbnail(eng::Renderer& renderer, int size)
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
    renderer.setMaterialThumbnailCamera(kThumbOrigin + kThumbCameraOffset,
                                        glm::angleAxis(glm::radians(-10.0f),
                                                       glm::vec3(1, 0, 0)),
                                        45.0f);
}

void MaterialStage::setThumbnailMaterial(eng::Renderer& renderer,
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

    renderer.setNodeMaterial(mThumbSubject, material);
    // setNodeMaterial re-attaches, which resets the visibility flags the
    // thumbnail target filters on. Without this the swatch subject reappears in
    // the level the moment you preview a material.
    renderer.setNodeThumbnailOnly(mThumbSubject, true);
}

void MaterialStage::spinThumbnail(eng::Renderer& renderer, float radians)
{
    mThumbSpin = radians;
    // A quad turned on the turntable would present its edge and then its back.
    // The swatch keeps facing the camera; the animation is the motion here.
    if (mThumbSubject.valid() && mThumbMode == StagePreview::Sphere)
        renderer.setOrientation(mThumbSubject,
                                glm::angleAxis(radians, glm::vec3(0, 1, 0)));
}

void MaterialStage::applyEnvironment(eng::Renderer& renderer) const
{
    if (mMode == StagePreview::Quad) {
        // Full ambient with no lights is how you say "unlit" to the PSX shaders:
        // the lighting term collapses to the albedo, so a procedural shader
        // shows exactly the colours it computes. The backdrop is flat and
        // neutral so nothing behind the icon reads as part of it.
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
    renderer.setFog({0.055f, 0.06f, 0.075f}, 0.0f); // fog would tint the subject
    renderer.setEditorViewportBackground({0.055f, 0.06f, 0.075f});
}

void MaterialStage::destroyStage(eng::Renderer& renderer)
{
    if (mSubject.valid())
        renderer.destroyNode(mSubject);
    mSubject = eng::NodeHandle{};
    if (mFloor.valid())
        renderer.destroyNode(mFloor);
    mFloor = eng::NodeHandle{};
    for (const eng::NodeHandle node : mLightNodes)
        renderer.destroyNode(node);
    mLightNodes.clear();
    mLights.clear();
}

void MaterialStage::destroy(eng::Renderer& renderer)
{
    destroyThumbnailRig(renderer);
    destroyStage(renderer);
}

void MaterialStage::setMaterial(eng::Renderer& renderer,
                                const std::string& material)
{
    if (material.empty() || !built())
        return;
    mMaterial = material;

    const StagePreview mode = mCatalog.modeFor(material);
    if (mode != mMode) {
        // Full teardown and rebuild. The two rigs differ in geometry, in whether
        // a floor exists, and in whether lights exist, so there is no subset of
        // state worth preserving -- and leaving a point light behind would light
        // the level the editor returns to.
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

void MaterialStage::setFloorMaterial(eng::Renderer& renderer,
                                     const std::string& material)
{
    if (material.empty())
        return;
    // Remembered even in quad mode, which has no floor, so the choice survives a
    // round trip through an animated material.
    mFloorMaterial = material;
    if (mFloor.valid())
        renderer.setNodeMaterial(mFloor, material);
}

void MaterialStage::setVisible(eng::Renderer& renderer, bool visible)
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

void MaterialStage::setSpin(eng::Renderer& renderer, float radians)
{
    mSpin = radians;
    // See spinThumbnail: the turntable is a sphere affordance. The value is
    // still stored so the gizmo's rotation readout stays continuous.
    if (mSubject.valid() && mMode == StagePreview::Sphere)
        renderer.setOrientation(mSubject,
                                glm::angleAxis(radians, glm::vec3(0, 1, 0)));
}

} // namespace ed
