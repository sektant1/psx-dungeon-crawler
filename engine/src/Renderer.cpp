#include <eng/Renderer.h>

#include <eng/Log.h>

#include "ObjLoader.h"
#include "ProceduralMeshes.h"
#include "RenderCore.h"

#include <Ogre.h>

#include <algorithm>
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
    int nameCounter = 0;
    EnvState env;
    // Original sub-entity materials, saved while the wireframe debug view
    // holds every entity on PSX/DebugWireframe.
    std::unordered_map<Ogre::SubEntity*, std::string> savedMaterials;
    // Post-chain settings stashed by setWireframeDebug(true) and restored
    // on toggle-off (the view bypasses them so lines stay crisp).
    struct {
        int pixelSize = 3;
        bool stylize = true, dither = false, bloom = true, grade = false;
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
    // The post chain hosts pixelation/outlines/bloom too, so it stays on;
    // "dither off" only bypasses the quantization inside the dither pass.
    mImpl->core.enablePostChain();
    setMaterialParam("PSX/DitherPost", "ditherEnabled", enabled ? 1.0f : 0.0f);
}

void Renderer::setPixelSize(int pixelSize)
{
    mImpl->env.pixelSize = std::clamp(pixelSize, 1, 16);
    mImpl->core.setPixelSize(mImpl->env.pixelSize);
}

void Renderer::setStylizeEnabled(bool enabled)
{
    mImpl->env.stylize = enabled;
    setMaterialParam("PSX/PixelStylize", "stylizeEnabled", enabled ? 1.0f : 0.0f);
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
    // The post chain smears the 1px lines: the pixelation RT averages them
    // away, stylize inks their depth edges, dither speckles the flat colour.
    // Bypass every post effect while the view is up, restore after.
    if (enabled) {
        mImpl->preWireframe = {mImpl->env.pixelSize, mImpl->env.stylize,
                               mImpl->env.dither, mImpl->env.bloom,
                               mImpl->env.grade};
        setPixelSize(1);
        setStylizeEnabled(false);
        setDitherEnabled(false);
        setBloomEnabled(false);
        setGradeEnabled(false);
    } else {
        setPixelSize(mImpl->preWireframe.pixelSize);
        setStylizeEnabled(mImpl->preWireframe.stylize);
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
