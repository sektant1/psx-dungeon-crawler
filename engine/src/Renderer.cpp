#include <eng/Renderer.h>

#include <eng/Log.h>

#include "ObjLoader.h"
#include "ProceduralMeshes.h"
#include "RenderCore.h"

#include <Ogre.h>
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <vector>

namespace eng {

namespace {

Ogre::Vector3 toOgre(glm::vec3 v) { return {v.x, v.y, v.z}; }
Ogre::Quaternion toOgre(glm::quat q) { return {q.w, q.x, q.y, q.z}; }
Ogre::ColourValue toColour(glm::vec3 c) { return Ogre::ColourValue(c.x, c.y, c.z); }

Ogre::Matrix4 toOgre(const glm::mat4& m) // glm column-major -> Ogre row-major
{
    Ogre::Matrix4 o;
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            o[r][c] = m[c][r];
    return o;
}

} // namespace

struct Renderer::Impl {
    RenderCore core;
    std::vector<Ogre::SceneNode*> nodes; // nodes[id-1]; id 1 == scene root
    std::vector<std::string> meshNames;  // meshNames[id-1]
    std::vector<Ogre::Light*> lights;    // lights[id-1]
    std::vector<Ogre::BillboardSet*> sprites; // sprites[id-1]
    std::vector<std::string> spriteMaterials;
    std::vector<std::string> generatedTextures;
    int nameCounter = 0;
    EnvState env;
    // Original sub-entity materials, saved while the wireframe debug view
    // holds every entity on PSX/DebugWireframe.
    std::unordered_map<Ogre::SubEntity*, std::string> savedMaterials;
    // Post-chain settings stashed by setWireframeDebug(true) and restored
    // on toggle-off (the view bypasses them so lines stay crisp).
    struct {
        int pixelSize = 3;
        bool dither = false, bloom = true, grade = false;
    } preWireframe;

    // Static geometry batches: records kept after build() so the wireframe
    // debug view can rebuild with the wire material and restore.
    struct StaticBatch {
        Ogre::StaticGeometry* sg = nullptr;
        struct Rec {
            MeshHandle mesh;
            std::string material;
            glm::vec3 pos;
            float yawDeg;
        };
        std::vector<Rec> recs;
        bool built = false;
    };
    std::vector<StaticBatch> staticBatches; // staticBatches[id-1]

    // (Re)fills a StaticGeometry from its records. materialOverride empty =
    // each record's own material (normal view); non-empty = forced (wireframe).
    void fillStaticBatch(StaticBatch& b, const std::string& materialOverride)
    {
        Ogre::SceneManager* sm = core.sceneMgr();
        if (b.built)
            b.sg->reset();
        for (const auto& rec : b.recs) {
            const std::string& mat =
                materialOverride.empty() ? rec.material : materialOverride;
            Ogre::Entity* e = sm->createEntity(mesh(rec.mesh, "staticBatch"));
            e->setMaterialName(mat);
            e->setCastShadows(false);
            b.sg->addEntity(e, toOgre(rec.pos),
                            Ogre::Quaternion(Ogre::Degree(rec.yawDeg),
                                             Ogre::Vector3::UNIT_Y));
            sm->destroyEntity(e);
        }
        b.sg->build();
        b.built = true;
    }

    Ogre::SceneNode* node(NodeHandle h, const char* what)
    {
        if (!h.valid() || h.id > nodes.size())
            log::fatal("Renderer: invalid node handle %u in %s", h.id, what);
        return nodes[h.id - 1];
    }
    const std::string& mesh(MeshHandle h, const char* what)
    {
        if (!h.valid() || h.id > meshNames.size())
            log::fatal("Renderer: invalid mesh handle %u in %s", h.id, what);
        return meshNames[h.id - 1];
    }
    MeshHandle registerMesh(std::string name)
    {
        meshNames.push_back(std::move(name));
        return {static_cast<uint32_t>(meshNames.size())};
    }
    std::string nextName(const char* prefix)
    {
        return std::string(prefix) + std::to_string(++nameCounter);
    }
};

Renderer::Renderer() : mImpl(new Impl) {}
Renderer::~Renderer() = default;

MeshHandle Renderer::loadObj(const std::string& path, const glm::mat4* bake)
{
    const std::string name = mImpl->nextName("mesh");
    try {
        ObjLoader::load(path, name,
                        bake ? toOgre(*bake) : Ogre::Matrix4::IDENTITY);
    } catch (const std::exception& e) {
        log::fatal("Renderer: loadObj('%s') failed: %s", path.c_str(), e.what());
    }
    return mImpl->registerMesh(name);
}

MeshHandle Renderer::createInteriorBox(float size, int subdivide)
{
    const std::string name = mImpl->nextName("mesh");
    ProceduralMeshes::createInteriorBox(name, size, subdivide);
    return mImpl->registerMesh(name);
}

MeshHandle Renderer::createPlane(float size)
{
    const std::string name = mImpl->nextName("mesh");
    ProceduralMeshes::createPlane(name, size);
    return mImpl->registerMesh(name);
}

MeshHandle Renderer::createBeveledBox(float bevel)
{
    const std::string name = mImpl->nextName("beveled_box");
    ProceduralMeshes::createBeveledBox(name, bevel);
    return mImpl->registerMesh(name);
}

MeshHandle Renderer::createCone(float radius, float height, int segments)
{
    const std::string name = mImpl->nextName("cone");
    ProceduralMeshes::createCone(name, radius, height, segments);
    return mImpl->registerMesh(name);
}

MeshHandle Renderer::createPortalRing(float outerRadius, float innerRadius,
                                      float depth, int segments)
{
    const std::string name = mImpl->nextName("portal_ring");
    ProceduralMeshes::createPortalRing(name, outerRadius, innerRadius, depth,
                                       segments);
    return mImpl->registerMesh(name);
}

MeshHandle Renderer::createPortalDisc(float radius, int segments)
{
    const std::string name = mImpl->nextName("portal_disc");
    ProceduralMeshes::createPortalDisc(name, radius, segments);
    return mImpl->registerMesh(name);
}

NodeHandle Renderer::createNode(NodeHandle parent, glm::vec3 position)
{
    Ogre::SceneNode* n =
        mImpl->node(parent, "createNode")->createChildSceneNode(toOgre(position));
    mImpl->nodes.push_back(n);
    return {static_cast<uint32_t>(mImpl->nodes.size())};
}

void Renderer::setPosition(NodeHandle node, glm::vec3 position)
{
    mImpl->node(node, "setPosition")->setPosition(toOgre(position));
}

void Renderer::setOrientation(NodeHandle node, glm::quat orientation)
{
    mImpl->node(node, "setOrientation")->setOrientation(toOgre(orientation));
}

void Renderer::setScale(NodeHandle node, glm::vec3 scale)
{
    mImpl->node(node, "setScale")->setScale(toOgre(scale));
}

void Renderer::setNodeVisible(NodeHandle node, bool show)
{
    mImpl->node(node, "setNodeVisible")->setVisible(show);
}

void Renderer::attachMesh(NodeHandle node, MeshHandle mesh,
                          const std::string& materialName, bool castShadows)
{
    if (!Ogre::MaterialManager::getSingleton().getByName(materialName))
        log::fatal("Renderer: unknown material '%s'", materialName.c_str());
    Ogre::Entity* e =
        mImpl->core.sceneMgr()->createEntity(mImpl->mesh(mesh, "attachMesh"));
    e->setMaterialName(materialName);
    e->setCastShadows(castShadows);
    if (mImpl->env.wireframe) { // debug view active: join it immediately
        for (Ogre::SubEntity* se : e->getSubEntities()) {
            mImpl->savedMaterials[se] = materialName;
            se->setMaterialName("PSX/DebugWireframe");
        }
    }
    mImpl->node(node, "attachMesh")->attachObject(e);
}

std::string Renderer::createSpriteMaterial(const SpriteClip& clip)
{
    const char* base = clip.blend == SpriteBlend::Alpha ? "Sprite/Alpha"
                     : clip.blend == SpriteBlend::Additive ? "Sprite/Additive"
                                                          : "Sprite/Opaque";
    Ogre::MaterialPtr source = Ogre::MaterialManager::getSingleton().getByName(base);
    if (!source)
        log::fatal("Renderer: sprite template '%s' is missing", base);
    const std::string name = mImpl->nextName("sprite_material");
    Ogre::MaterialPtr material = source->clone(name);
    Ogre::Pass* pass = material->getTechnique(0)->getPass(0);
    std::string texture = clip.texture;
    const bool generated = bool(Ogre::TextureManager::getSingleton().getByName(texture));
    if (!generated &&
        !Ogre::ResourceGroupManager::getSingleton().resourceExistsInAnyGroup(texture)) {
        log::error("Sprite: texture '%s' is missing; using PINKY.png",
                   texture.c_str());
        texture = "PINKY.png";
    }
    pass->getTextureUnitState(0)->setTextureName(texture);
    auto vp = pass->getVertexProgramParameters();
    vp->setNamedConstant("spriteGrid",
                         Ogre::Vector2(float(std::max(1, clip.grid.x)),
                                       float(std::max(1, clip.grid.y))));
    vp->setNamedConstant("spriteFrameCount", float(std::max(1, clip.frameCount)));
    vp->setNamedConstant("spriteFps", std::max(0.0f, clip.framesPerSecond));
    vp->setNamedConstant("spriteScroll", Ogre::Vector2(clip.scrollVelocity.x,
                                                        clip.scrollVelocity.y));
    vp->setNamedConstant("spriteUvScale", Ogre::Vector2(clip.uvScale.x,
                                                         clip.uvScale.y));
    vp->setNamedConstant("spritePhase", clip.phaseSeconds);
    auto fp = pass->getFragmentProgramParameters();
    fp->setNamedConstant("spriteTint", Ogre::Vector4(clip.tint.r, clip.tint.g,
                                                      clip.tint.b, clip.tint.a));
    fp->setNamedConstant("spriteAlphaCutoff", clip.alphaCutoff);
    material->load();
    mImpl->spriteMaterials.push_back(name);
    return name;
}

SpriteHandle Renderer::attachSprite(NodeHandle node, const SpriteClip& clip)
{
    Ogre::SceneManager* sm = mImpl->core.sceneMgr();
    Ogre::BillboardSet* set = sm->createBillboardSet(mImpl->nextName("sprite"), 1);
    set->setMaterialName(createSpriteMaterial(clip));
    set->setDefaultDimensions(std::max(0.001f, clip.worldSize.x),
                              std::max(0.001f, clip.worldSize.y));
    set->createBillboard(Ogre::Vector3::ZERO, Ogre::ColourValue::White);
    mImpl->node(node, "attachSprite")->attachObject(set);
    mImpl->sprites.push_back(set);
    return {static_cast<uint32_t>(mImpl->sprites.size())};
}

SpriteHandle Renderer::attachTextSprite(NodeHandle node, const std::string& text,
                                        const TextSpriteStyle& style)
{
    ImFontAtlas* atlas = ImGui::GetIO().Fonts;
    ImFont* font = atlas->Fonts.empty() ? nullptr : atlas->Fonts[0];
    if (!font)
        log::fatal("Renderer: text sprite requested before font atlas exists");

    unsigned char* atlasPixels = nullptr;
    int atlasWidth = 0, atlasHeight = 0;
    atlas->GetTexDataAsRGBA32(&atlasPixels, &atlasWidth, &atlasHeight);
    const int padding = std::max(2, style.paddingPixels);
    float advance = 0.0f;
    for (unsigned char c : text)
        advance += font->FindGlyph(c < 128 ? c : '?')->AdvanceX;
    const int width = std::max(8, int(std::ceil(advance)) + padding * 2);
    const int height = std::max(8, int(std::ceil(font->FontSize)) + padding * 2);
    std::vector<unsigned char> pixels(size_t(width * height * 4), 0);
    const auto byte = [](float v) {
        return static_cast<unsigned char>(glm::clamp(v, 0.0f, 1.0f) * 255.0f);
    };
    const auto put = [&](int x, int y, glm::vec4 colour) {
        if (x < 0 || y < 0 || x >= width || y >= height) return;
        const size_t i = size_t((y * width + x) * 4);
        pixels[i + 0] = byte(colour.r); pixels[i + 1] = byte(colour.g);
        pixels[i + 2] = byte(colour.b); pixels[i + 3] = byte(colour.a);
    };
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            const bool border = x < 2 || y < 2 || x >= width - 2 || y >= height - 2;
            put(x, y, border ? style.borderColour : style.backgroundColour);
        }
    float pen = float(padding);
    for (unsigned char c : text) {
        const ImFontGlyph* glyph = font->FindGlyph(c < 128 ? c : '?');
        const int sx0 = int(std::round(glyph->U0 * atlasWidth));
        const int sy0 = int(std::round(glyph->V0 * atlasHeight));
        const int sx1 = int(std::round(glyph->U1 * atlasWidth));
        const int sy1 = int(std::round(glyph->V1 * atlasHeight));
        const int dx0 = int(std::round(pen + glyph->X0));
        const int dy0 = int(std::round(float(padding) + glyph->Y0));
        for (int sy = sy0; sy < sy1; ++sy)
            for (int sx = sx0; sx < sx1; ++sx) {
                const unsigned char alpha = atlasPixels[(sy * atlasWidth + sx) * 4 + 3];
                if (!alpha) continue;
                glm::vec4 colour = style.textColour;
                colour.a *= float(alpha) / 255.0f;
                put(dx0 + sx - sx0, dy0 + sy - sy0, colour);
            }
        pen += glyph->AdvanceX;
    }

    const std::string textureName = mImpl->nextName("text_sprite_texture");
    Ogre::TexturePtr texture = Ogre::TextureManager::getSingleton().createManual(
        textureName, Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME,
        Ogre::TEX_TYPE_2D, uint32_t(width), uint32_t(height), 0,
        Ogre::PF_BYTE_RGBA, Ogre::TU_STATIC_WRITE_ONLY);
    const Ogre::PixelBox source(uint32_t(width), uint32_t(height), 1,
                                Ogre::PF_BYTE_RGBA, pixels.data());
    texture->getBuffer()->blitFromMemory(source);
    mImpl->generatedTextures.push_back(textureName);

    SpriteClip clip;
    clip.texture = textureName;
    clip.worldSize = {style.worldHeight * float(width) / float(height),
                      style.worldHeight};
    // Text plaques are deliberately opaque: the PSX compositor consumes a
    // second normal/depth target, and blending that target makes translucent
    // billboards invalidate the full-screen reconstruction pass.
    clip.blend = SpriteBlend::Opaque;
    return attachSprite(node, clip);
}

void Renderer::setSpriteVisible(SpriteHandle sprite, bool visible)
{
    if (!sprite.valid() || sprite.id > mImpl->sprites.size())
        log::fatal("Renderer: invalid sprite handle %u", sprite.id);
    mImpl->sprites[sprite.id - 1]->setVisible(visible);
}

StaticBatchHandle Renderer::createStaticBatch(glm::vec3 regionSize)
{
    Impl::StaticBatch b;
    b.sg = mImpl->core.sceneMgr()->createStaticGeometry(
        mImpl->nextName("staticbatch"));
    b.sg->setRegionDimensions(toOgre(regionSize));
    b.sg->setCastShadows(false); // tiles receive shadows, never cast
    mImpl->staticBatches.push_back(std::move(b));
    return {static_cast<uint32_t>(mImpl->staticBatches.size())};
}

void Renderer::addToStaticBatch(StaticBatchHandle batch, MeshHandle mesh,
                                const std::string& materialName,
                                glm::vec3 pos, float yawDeg)
{
    if (!batch.valid() || batch.id > mImpl->staticBatches.size())
        log::fatal("Renderer: invalid batch handle %u in addToStaticBatch",
                   batch.id);
    if (!Ogre::MaterialManager::getSingleton().getByName(materialName))
        log::fatal("Renderer: unknown material '%s'", materialName.c_str());
    mImpl->staticBatches[batch.id - 1].recs.push_back(
        {mesh, materialName, pos, yawDeg});
}

void Renderer::buildStaticBatch(StaticBatchHandle batch)
{
    if (!batch.valid() || batch.id > mImpl->staticBatches.size())
        log::fatal("Renderer: invalid batch handle %u in buildStaticBatch",
                   batch.id);
    // Respect an already-active wireframe view (PSX_WIREFRAME startup).
    mImpl->fillStaticBatch(mImpl->staticBatches[batch.id - 1],
                           mImpl->env.wireframe ? "PSX/DebugWireframe" : "");
}

void Renderer::setStaticBatchVisible(StaticBatchHandle batch, bool visible)
{
    if (!batch.valid() || batch.id > mImpl->staticBatches.size())
        log::fatal("Renderer: invalid batch handle %u in setStaticBatchVisible",
                   batch.id);
    mImpl->staticBatches[batch.id - 1].sg->setVisible(visible);
}

void Renderer::clearScene()
{
    Ogre::SceneManager* sm = mImpl->core.sceneMgr();
    // Detach + destroy every SceneNode under the root, then free the objects
    // those nodes referenced (Ogre owns them; removing nodes alone leaks).
    sm->getRootSceneNode()->removeAndDestroyAllChildren();
    sm->destroyAllStaticGeometry();
    sm->destroyAllParticleSystems();
    sm->destroyAllBillboardSets();
    sm->destroyAllEntities();
    sm->destroyAllManualObjects();
    sm->destroyAllLights();
    auto& materials = Ogre::MaterialManager::getSingleton();
    for (const std::string& name : mImpl->spriteMaterials)
        if (materials.getByName(name)) materials.remove(name);
    auto& textures = Ogre::TextureManager::getSingleton();
    for (const std::string& name : mImpl->generatedTextures)
        if (textures.getByName(name)) textures.remove(name);
    // Entities are gone; now free their Ogre::Mesh resources (otherwise the
    // next level's loadObj collides on the same resource name). Only the
    // meshes this Renderer created are in meshNames.
    auto& mm = Ogre::MeshManager::getSingleton();
    for (const std::string& name : mImpl->meshNames)
        if (mm.getByName(name))
            mm.remove(name);
    // Reset handle bookkeeping; re-register the root as kRootNode (id 1), the
    // same way detail::registerRoot does at startup. nameCounter stays
    // MONOTONIC across levels so freshly created Ogre objects can never reuse
    // a name that a lingering resource still holds.
    mImpl->nodes.clear();
    mImpl->lights.clear();
    mImpl->sprites.clear();
    mImpl->spriteMaterials.clear();
    mImpl->generatedTextures.clear();
    mImpl->staticBatches.clear();
    mImpl->savedMaterials.clear();
    mImpl->meshNames.clear();
    mImpl->nodes.push_back(sm->getRootSceneNode());
}

void Renderer::attachParticles(NodeHandle node, const std::string& templateName)
{
    try {
        Ogre::ParticleSystem* ps = mImpl->core.sceneMgr()->createParticleSystem(
            mImpl->nextName("particles"), templateName);
        mImpl->node(node, "attachParticles")->attachObject(ps);
    } catch (const std::exception& e) {
        log::fatal("Renderer: attachParticles('%s') failed: %s",
                   templateName.c_str(), e.what());
    }
}

void Renderer::attachCamera(NodeHandle node)
{
    Ogre::Camera* cam = mImpl->core.camera();
    if (cam->getParentSceneNode())
        cam->detachFromParent();
    mImpl->node(node, "attachCamera")->attachObject(cam);
}

LightHandle Renderer::attachLight(NodeHandle node, const LightDesc& desc)
{
    Ogre::Light* l = mImpl->core.sceneMgr()->createLight();
    l->setType(desc.type == LightDesc::Type::Directional
                   ? Ogre::Light::LT_DIRECTIONAL
                   : Ogre::Light::LT_POINT);
    l->setDiffuseColour(toColour(desc.colour));
    l->setSpecularColour(Ogre::ColourValue::Black); // PSX: specular_disabled
    // Ogre culls lights against the camera frustum using the attenuation
    // range (findLightsAffectingFrustum); a tight range makes wall lights
    // pop off the moment the source leaves the view. Keep Ogre's range
    // huge so lights stay registered, and pass the real falloff range
    // through the constant-attenuation slot -- the PSX shader reads
    // lightAtten.y (see psx_lighting.glsl) and ignores the rest.
    if (desc.type == LightDesc::Type::Point)
        l->setAttenuation(1000.0f, desc.range, 0.0f, 0.0f);
    l->setCastShadows(desc.castShadows);
    // The huge Ogre-side attenuation range would also become the stencil
    // volume extrusion distance; cap it to something scene-sized.
    if (desc.castShadows)
        l->setShadowFarDistance(desc.range * 2.0f);
    mImpl->node(node, "attachLight")->attachObject(l);
    mImpl->lights.push_back(l);
    return {static_cast<uint32_t>(mImpl->lights.size())};
}

void Renderer::setLightColour(LightHandle light, glm::vec3 colour)
{
    if (!light.valid() || light.id > mImpl->lights.size())
        log::fatal("Renderer: invalid light handle %u in setLightColour",
                   light.id);
    mImpl->lights[light.id - 1]->setDiffuseColour(toColour(colour));
}

void Renderer::setCameraFov(float degrees)
{
    mImpl->env.fovDeg = degrees;
    mImpl->core.camera()->setFOVy(Ogre::Degree(degrees));
}

void Renderer::setCameraClip(float nearDist, float farDist)
{
    nearDist = std::clamp(nearDist, 0.01f, 10.0f);
    farDist = std::max(farDist, nearDist + 1.0f);
    mImpl->env.nearClip = nearDist;
    mImpl->env.farClip = farDist;
    mImpl->core.camera()->setNearClipDistance(nearDist);
    mImpl->core.camera()->setFarClipDistance(farDist);
}

namespace {
// Applies `set` to every VS/FS param set that defines paramName.
template <typename SetFn>
void applyMaterialParam(const std::string& materialName,
                        const std::string& paramName, SetFn&& set)
{
    Ogre::MaterialPtr mat =
        Ogre::MaterialManager::getSingleton().getByName(materialName);
    if (!mat)
        log::fatal("Renderer: unknown material '%s'", materialName.c_str());
    bool found = false;
    for (Ogre::Technique* tech : mat->getTechniques()) {
        for (Ogre::Pass* pass : tech->getPasses()) {
            Ogre::GpuProgramParametersSharedPtr sets[2];
            if (pass->hasVertexProgram())
                sets[0] = pass->getVertexProgramParameters();
            if (pass->hasFragmentProgram())
                sets[1] = pass->getFragmentProgramParameters();
            for (auto& params : sets) {
                if (params && params->_findNamedConstantDefinition(paramName, false)) {
                    set(params);
                    found = true;
                }
            }
        }
    }
    if (!found)
        log::fatal("Renderer: material '%s' has no param '%s'",
                   materialName.c_str(), paramName.c_str());
}
} // namespace

void Renderer::setMaterialParam(const std::string& m, const std::string& p, float v)
{
    applyMaterialParam(m, p, [&](auto& params) { params->setNamedConstant(p, v); });
    mImpl->core.markPostChainDirty();
}
void Renderer::setMaterialParam(const std::string& m, const std::string& p, glm::vec2 v)
{
    applyMaterialParam(m, p, [&](auto& params) {
        params->setNamedConstant(p, Ogre::Vector2(v.x, v.y));
    });
    mImpl->core.markPostChainDirty();
}
void Renderer::setMaterialParam(const std::string& m, const std::string& p, glm::vec3 v)
{
    applyMaterialParam(m, p, [&](auto& params) {
        params->setNamedConstant(p, Ogre::Vector3(v.x, v.y, v.z));
    });
    mImpl->core.markPostChainDirty();
}
void Renderer::setMaterialParam(const std::string& m, const std::string& p, glm::vec4 v)
{
    applyMaterialParam(m, p, [&](auto& params) {
        params->setNamedConstant(p, Ogre::Vector4(v.x, v.y, v.z, v.w));
    });
    mImpl->core.markPostChainDirty();
}

void Renderer::setAmbient(glm::vec3 colour)
{
    mImpl->env.ambient = colour;
    mImpl->core.sceneMgr()->setAmbientLight(toColour(colour));
}

void Renderer::setFog(glm::vec3 colour, float expDensity)
{
    mImpl->env.fogColour = colour;
    mImpl->env.fogDensity = expDensity;
    mImpl->core.sceneMgr()->setFog(Ogre::FOG_EXP, toColour(colour), expDensity);
}

void Renderer::setBackground(glm::vec3 colour)
{
    mImpl->env.background = colour;
    mImpl->core.viewport()->setBackgroundColour(toColour(colour));
}

void Renderer::setDitherEnabled(bool enabled)
{
    mImpl->env.dither = enabled;
    // The post chain hosts pixelation/bloom too, so it stays on;
    // "dither off" only bypasses the quantization inside the dither pass.
    mImpl->core.enablePostChain();
    setMaterialParam("PSX/DitherPost", "ditherEnabled", enabled ? 1.0f : 0.0f);
}

void Renderer::setPixelSize(int pixelSize)
{
    mImpl->env.pixelSize = std::clamp(pixelSize, 1, 16);
    mImpl->core.setPixelSize(mImpl->env.pixelSize);
}

void Renderer::setPerPixelLightingEnabled(bool enabled)
{
    mImpl->env.perPixelLighting = enabled;
    const float value = enabled ? 1.0f : 0.0f;
    auto it = Ogre::MaterialManager::getSingleton().getResourceIterator();
    while (it.hasMoreElements()) {
        auto mat = Ogre::static_pointer_cast<Ogre::Material>(it.getNext());
        if (!mat || !mat->isLoaded())
            continue; // unloaded materials keep the program default (on)
        for (Ogre::Technique* tech : mat->getTechniques()) {
            for (Ogre::Pass* pass : tech->getPasses()) {
                // perPixelLighting is an FS-only branch; vertex programs
                // never declare it, so skip them entirely.
                if (!pass->hasFragmentProgram())
                    continue;
                // Each loaded pass owns a clone of the program's default
                // params, so mutating here never leaks across materials.
                auto params = pass->getFragmentProgramParameters();
                if (params &&
                    params->_findNamedConstantDefinition("perPixelLighting", false))
                    params->setNamedConstant("perPixelLighting", value);
            }
        }
    }
}

void Renderer::setGlobalMaterialParam(const std::string& paramName, float value)
{
    auto it = Ogre::MaterialManager::getSingleton().getResourceIterator();
    while (it.hasMoreElements()) {
        auto mat = Ogre::static_pointer_cast<Ogre::Material>(it.getNext());
        if (!mat || !mat->isLoaded())
            continue;
        for (Ogre::Technique* tech : mat->getTechniques()) {
            for (Ogre::Pass* pass : tech->getPasses()) {
                Ogre::GpuProgramParametersSharedPtr sets[2];
                if (pass->hasVertexProgram())
                    sets[0] = pass->getVertexProgramParameters();
                if (pass->hasFragmentProgram())
                    sets[1] = pass->getFragmentProgramParameters();
                for (auto& params : sets)
                    if (params &&
                        params->_findNamedConstantDefinition(paramName, false))
                        params->setNamedConstant(paramName, value);
            }
        }
    }
    mImpl->core.markPostChainDirty();
}

void Renderer::setOmniAttenuation(float exponent)
{
    mImpl->env.omniAttenuation = exponent;
    setGlobalMaterialParam("omniAttenuation", exponent);
}

void Renderer::setLightSteps(float steps)
{
    mImpl->env.lightSteps = steps;
    setGlobalMaterialParam("lightSteps", steps);
}

void Renderer::setLightStepSoftness(float softness)
{
    mImpl->env.lightStepSoftness = softness;
    setGlobalMaterialParam("lightStepSoftness", softness);
}

void Renderer::setFogDesatBoost(float boost)
{
    mImpl->env.fogDesatBoost = boost;
    setGlobalMaterialParam("fogDesatBoost", boost);
}

void Renderer::setBloomEnabled(bool enabled)
{
    mImpl->env.bloom = enabled;
    setMaterialParam("PSX/BloomComposite", "bloomEnabled",
                     enabled ? 1.0f : 0.0f);
}

void Renderer::setBloomParams(float threshold, float intensity)
{
    mImpl->env.bloomThreshold = threshold;
    mImpl->env.bloomIntensity = intensity;
    setMaterialParam("PSX/BloomBright", "bloomThreshold", threshold);
    setMaterialParam("PSX/BloomComposite", "bloomIntensity", intensity);
}

void Renderer::setWireframeDebug(bool enabled)
{
    if (mImpl->env.wireframe == enabled)
        return;
    mImpl->env.wireframe = enabled;
    for (const auto& [name, mo] : mImpl->core.sceneMgr()->getMovableObjects("Entity")) {
        auto* e = static_cast<Ogre::Entity*>(mo);
        for (Ogre::SubEntity* se : e->getSubEntities()) {
            if (enabled) {
                mImpl->savedMaterials[se] = se->getMaterial()->getName();
                se->setMaterialName("PSX/DebugWireframe");
            } else {
                auto found = mImpl->savedMaterials.find(se);
                if (found != mImpl->savedMaterials.end())
                    se->setMaterialName(found->second);
            }
        }
    }
    if (!enabled)
        mImpl->savedMaterials.clear();
    // Static batches have no per-entity material swap: rebuild each built
    // batch from its records with the wire material (on) or the recorded
    // originals (off).
    for (auto& b : mImpl->staticBatches)
        if (b.built)
            mImpl->fillStaticBatch(b, enabled ? "PSX/DebugWireframe" : "");
    // The post chain smears the 1px lines, so bypass every post effect while
    // the view is up and restore it afterwards.
    // Bypass every post effect while the view is up, restore after.
    if (enabled) {
        mImpl->preWireframe = {mImpl->env.pixelSize, mImpl->env.dither,
                               mImpl->env.bloom,
                               mImpl->env.grade};
        setPixelSize(1);
        // Wireframe is a diagnostic view: no ink/highlight pass should alter
        // its lines or introduce false contour noise.
        setMaterialParam("PSX/PixelStylize", "stylizeEnabled", 0.0f);
        setDitherEnabled(false);
        setBloomEnabled(false);
        setGradeEnabled(false);
    } else {
        setPixelSize(mImpl->preWireframe.pixelSize);
        setMaterialParam("PSX/PixelStylize", "stylizeEnabled", 1.0f);
        setDitherEnabled(mImpl->preWireframe.dither);
        setBloomEnabled(mImpl->preWireframe.bloom);
        setGradeEnabled(mImpl->preWireframe.grade);
    }
}

void Renderer::setGradeEnabled(bool enabled)
{
    mImpl->env.grade = enabled;
    setMaterialParam("PSX/DitherPost", "gradeEnabled", enabled ? 1.0f : 0.0f);
}

void Renderer::setGradeParams(float desaturate, float contrast,
                              glm::vec3 shadowTint, glm::vec3 midTint)
{
    mImpl->env.gradeDesaturate = desaturate;
    mImpl->env.gradeContrast = contrast;
    mImpl->env.gradeShadowTint = shadowTint;
    mImpl->env.gradeMidTint = midTint;
    setMaterialParam("PSX/DitherPost", "gradeDesaturate", desaturate);
    setMaterialParam("PSX/DitherPost", "gradeContrast", contrast);
    setMaterialParam("PSX/DitherPost", "gradeShadowTint", shadowTint);
    setMaterialParam("PSX/DitherPost", "gradeMidTint", midTint);
}

const EnvState& Renderer::envState() const { return mImpl->env; }

void Renderer::writeScreenshot(const std::string& path)
{
    mImpl->core.writeScreenshot(path);
}

namespace detail {

RenderCore& coreOf(Renderer& r) { return r.mImpl->core; }

void registerRoot(Renderer& r)
{
    r.mImpl->nodes.push_back(r.mImpl->core.sceneMgr()->getRootSceneNode());
}

} // namespace detail

} // namespace eng
