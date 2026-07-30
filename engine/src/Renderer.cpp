#include <eng/Renderer.h>

#include <eng/Log.h>
#include <eng/Primitive.h>
#include <eng/render/PrototypeAssets.h>
#include <eng/particles/ParticlePresets.h>
#include <eng/SceneView.h>

#include "ObjLoader.h"
#include "MeshResources.h"
#include "particles/Particles.h"
#include "ProceduralMeshes.h"
#include "render/PrimitiveGeometry.h"
#include "RenderCore.h"
#include "SceneRegistry.h"

#include <Ogre.h>
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <regex>
#include <set>
#include <unordered_map>
#include <vector>

namespace eng {

namespace {

Ogre::Vector3 toOgre(glm::vec3 v) { return {v.x, v.y, v.z}; }
Ogre::Quaternion toOgre(glm::quat q) { return {q.w, q.x, q.y, q.z}; }
Ogre::ColourValue toColour(glm::vec3 c) { return Ogre::ColourValue(c.x, c.y, c.z); }
bool finiteVec3(glm::vec3 v)
{
    return std::isfinite(v.x) && std::isfinite(v.y) &&
           std::isfinite(v.z);
}

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
    Particles particles; // data-driven pooled particle effects
    SceneRegistry mScene; // editor-facing mirror of the scene graph
    std::vector<Ogre::SceneNode*> nodes; // nodes[id-1]; id 1 == scene root
    detail::MeshResources meshes;
    std::vector<Ogre::Light*> lights;    // lights[id-1]
    std::vector<Ogre::BillboardSet*> sprites; // sprites[id-1]
    std::vector<std::string> spriteMaterials;
    std::vector<std::string> generatedTextures;
    // Prototype meshes keyed by prototype::MeshShape::role -- one shared mesh
    // per distinct shape, built on first miss. See prototypeMesh().
    std::unordered_map<std::string, MeshHandle> prototypeMeshes;
    prototype::PrototypeCatalog prototypes;
    int nameCounter = 0;
    EnvState env;
    // Original sub-entity materials, saved while the wireframe debug view
    // holds every entity on PSX/DebugWireframe.
    std::unordered_map<Ogre::SubEntity*, std::string> savedMaterials;
    ModelMaterialFallbackWarnings missingMaterialWarnings;
    EnchantmentBookkeeping<Ogre::SubEntity*, Ogre::SceneNode*> enchantments;

    void restoreEnchantment(Ogre::SubEntity* sub)
    {
        const auto state = enchantments.take(sub);
        if (!state)
            return;
        auto saved = savedMaterials.find(sub);
        if (saved != savedMaterials.end())
            saved->second = state->baseMaterial;
        else
            sub->setMaterialName(state->baseMaterial);
        auto& materials = Ogre::MaterialManager::getSingleton();
        if (materials.getByName(state->generatedMaterial))
            materials.remove(state->generatedMaterial);
    }

    void clearEnchantmentSubtree(Ogre::SceneNode* root)
    {
        const auto children = [](Ogre::SceneNode* node) {
            std::vector<Ogre::SceneNode*> result;
            for (auto* child : node->getChildren())
                if (auto* sceneNode = dynamic_cast<Ogre::SceneNode*>(child))
                    result.push_back(sceneNode);
            return result;
        };
        for (Ogre::SceneNode* node :
             collectEnchantmentTargets(root, true, children))
            for (size_t i = 0; i < node->numAttachedObjects(); ++i)
                if (auto* entity = dynamic_cast<Ogre::Entity*>(
                        node->getAttachedObject(i)))
                    for (Ogre::SubEntity* sub : entity->getSubEntities())
                        restoreEnchantment(sub);
    }

    // Immediate-mode debug line overlay (physics collider wireframes etc.)
    // Created lazily on first use; recreated if clearScene destroyed it.
    Ogre::ManualObject* debugLines = nullptr;
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
        const std::string* name = meshes.name(h);
        if (!name)
            log::fatal("Renderer: invalid mesh handle %u in %s", h.id, what);
        return *name;
    }
    MeshHandle registerMesh(std::string name,
                            detail::MeshGeometry geometry = {},
                            std::string importIdentity = {})
    {
        return meshes.add(std::move(name), std::move(geometry),
                          std::move(importIdentity));
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
    detail::MeshGeometry geometry;
    const auto removePartialMesh = [&] {
        auto& manager = Ogre::MeshManager::getSingleton();
        if (manager.getByName(name))
            manager.remove(name);
    };
    try {
        ObjLoader::load(path, name,
                        bake ? toOgre(*bake) : Ogre::Matrix4::IDENTITY,
                        &geometry.vertices, &geometry.indices);
    } catch (const std::exception& e) {
        removePartialMesh();
        log::error("Renderer: loadObj('%s') failed: %s; using prototype mesh",
                   path.c_str(), e.what());
        return prototypeMesh(path);
    } catch (...) {
        removePartialMesh();
        log::error("Renderer: loadObj('%s') failed with an unknown error; "
                   "using prototype mesh",
                   path.c_str());
        return prototypeMesh(path);
    }
    return mImpl->registerMesh(name, std::move(geometry));
}

MeshHandle Renderer::loadObj(const std::string& path,
                             const ModelImportOptions& options)
{
    const ModelImportOptions sanitized =
        sanitizeModelImportOptions(options);
    const std::string identity =
        modelImportCacheKey(path, sanitized);
    const std::string name = mImpl->nextName("model");
    detail::MeshGeometry geometry;
    const auto removePartialMesh = [&] {
        auto& manager = Ogre::MeshManager::getSingleton();
        if (manager.getByName(name))
            manager.remove(name);
    };
    try {
        ObjLoader::load(path, name, sanitized, &geometry.vertices,
                        &geometry.indices);
    } catch (const std::exception& e) {
        removePartialMesh();
        log::error("Renderer: loadObj('%s') failed: %s; using prototype mesh",
                   path.c_str(), e.what());
        return prototypeMesh(path);
    } catch (...) {
        removePartialMesh();
        log::error("Renderer: loadObj('%s') failed with an unknown error; "
                   "using prototype mesh",
                   path.c_str());
        return prototypeMesh(path);
    }
    // Identity metadata is retained, but every load still owns a distinct Ogre
    // resource/handle so destroying one ModelInstance cannot unload another.
    return mImpl->registerMesh(name, std::move(geometry), identity);
}

MeshHandle Renderer::createPrimitiveMesh(const PrimitiveMeshDesc& desc)
{
    const auto primitive = detail::buildPrimitiveGeometry(desc);
    if (!primitive) {
        log::error("Renderer: invalid primitive mesh descriptor");
        return {};
    }

    const std::string name = mImpl->nextName("primitive");
    ProceduralMeshes::upload(name, *primitive);
    detail::MeshGeometry cached;
    cached.vertices.reserve(primitive->vertices.size());
    for (const detail::PrimitiveVertex& vertex : primitive->vertices)
        cached.vertices.push_back(vertex.position);
    cached.indices = primitive->indices;
    return mImpl->registerMesh(name, std::move(cached));
}

void Renderer::setPrototypeCatalog(prototype::PrototypeCatalog catalog)
{
    mImpl->prototypes = std::move(catalog);
    // Cached meshes were built from the old rules; the next miss rebuilds.
    mImpl->prototypeMeshes.clear();
}

MeshHandle Renderer::prototypeMesh(const std::string& assetPath)
{
    const prototype::MeshShape shape = mImpl->prototypes.meshFor(assetPath);
    MeshHandle& cached = mImpl->prototypeMeshes[shape.role];
    if (!cached.valid())
        cached = createPrimitiveMesh(shape.desc);
    return cached;
}

bool Renderer::meshBounds(MeshHandle mesh, MeshBounds& out) const
{
    const std::string* name = mImpl->meshes.name(mesh);
    if (!name)
        return false;
    const Ogre::MeshPtr resource =
        Ogre::MeshManager::getSingleton().getByName(
            *name);
    if (!resource)
        return false;
    const Ogre::AxisAlignedBox& bounds = resource->getBounds();
    if (bounds.isNull() || bounds.isInfinite())
        return false;
    const Ogre::Vector3 min = bounds.getMinimum();
    const Ogre::Vector3 max = bounds.getMaximum();
    out.min = {min.x, min.y, min.z};
    out.max = {max.x, max.y, max.z};
    return finiteVec3(out.min) && finiteVec3(out.max);
}

bool Renderer::meshCollisionGeometry(
    MeshHandle mesh, std::vector<glm::vec3>& vertices,
    std::vector<uint32_t>& indices) const
{
    const detail::MeshGeometry* geometry =
        mImpl->meshes.geometry(mesh);
    if (!geometry || geometry->vertices.empty() ||
        geometry->indices.empty())
        return false;
    vertices = geometry->vertices;
    indices = geometry->indices;
    return true;
}

bool Renderer::releaseMesh(MeshHandle mesh)
{
    const std::optional<std::string> name =
        mImpl->meshes.release(mesh);
    if (!name)
        return false;
    auto& manager = Ogre::MeshManager::getSingleton();
    if (manager.getByName(*name))
        manager.remove(*name);
    return true;
}

NodeHandle Renderer::createNode(NodeHandle parent, glm::vec3 position,
                                const std::string& name)
{
    Ogre::SceneNode* n =
        mImpl->node(parent, "createNode")->createChildSceneNode(toOgre(position));
    mImpl->nodes.push_back(n);
    NodeHandle h{static_cast<uint32_t>(mImpl->nodes.size())};
    mImpl->mScene.addNode(h, parent,
                          name.empty() ? mImpl->mScene.autoName(h) : name);
    return h;
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

bool Renderer::nodeWorldTransform(NodeHandle node,
                                  NodeTransform& out) const
{
    if (!node.valid() || node.id > mImpl->nodes.size())
        return false;
    Ogre::SceneNode* sceneNode = mImpl->nodes[node.id - 1];
    if (!sceneNode)
        return false;
    const Ogre::Vector3 position = sceneNode->_getDerivedPosition();
    const Ogre::Quaternion orientation =
        sceneNode->_getDerivedOrientation();
    const Ogre::Vector3 scale = sceneNode->_getDerivedScale();
    out.position = {position.x, position.y, position.z};
    out.orientation = {orientation.w, orientation.x, orientation.y,
                       orientation.z};
    out.scale = {scale.x, scale.y, scale.z};
    return finiteVec3(out.position) && finiteVec3(out.scale) &&
           std::isfinite(out.orientation.w) &&
           std::isfinite(out.orientation.x) &&
           std::isfinite(out.orientation.y) &&
           std::isfinite(out.orientation.z);
}

void Renderer::setNodeVisible(NodeHandle node, bool show)
{
    mImpl->node(node, "setNodeVisible")->setVisible(show);
}

void Renderer::setNodeMaterial(NodeHandle node, const std::string& materialName)
{
    std::string resolved = materialName;
    if (!Ogre::MaterialManager::getSingleton().getByName(resolved)) {
        const std::string fallback =
            mImpl->prototypes.materialFor(materialName);
        log::error("Renderer: material '%s' is missing; using '%s'",
                   materialName.c_str(), fallback.c_str());
        resolved = fallback;
    }
    Ogre::SceneNode* n = mImpl->node(node, "setNodeMaterial");
    for (size_t i = 0; i < n->numAttachedObjects(); ++i)
        if (auto* e = dynamic_cast<Ogre::Entity*>(n->getAttachedObject(i))) {
            for (Ogre::SubEntity* sub : e->getSubEntities()) {
                // A material swap defines a new base state, so discard any
                // enchant clone before applying it.
                mImpl->restoreEnchantment(sub);
                auto saved = mImpl->savedMaterials.find(sub);
                if (saved != mImpl->savedMaterials.end())
                    saved->second = resolved;
                else
                    sub->setMaterialName(resolved);
            }
        }
    // Reflect the change in the editor's scene mirror (first mesh attachment).
    mImpl->mScene.setMeshMaterial(node, resolved);
}

void Renderer::clearNodeEnchantment(NodeHandle node)
{
    Ogre::SceneNode* root = mImpl->node(node, "clearNodeEnchantment");
    const auto children = [](Ogre::SceneNode* current) {
        std::vector<Ogre::SceneNode*> result;
        for (auto* child : current->getChildren())
            if (auto* sceneNode = dynamic_cast<Ogre::SceneNode*>(child))
                result.push_back(sceneNode);
        return result;
    };
    for (Ogre::SceneNode* current :
         collectEnchantmentTargets(root, true, children))
        for (size_t i = 0; i < current->numAttachedObjects(); ++i)
            if (auto* entity = dynamic_cast<Ogre::Entity*>(
                    current->getAttachedObject(i)))
                for (Ogre::SubEntity* sub : entity->getSubEntities()) {
                    if (mImpl->enchantments.shouldClear(
                            sub, current, root))
                        mImpl->restoreEnchantment(sub);
                }
}

void Renderer::setNodeEnchantment(NodeHandle node,
                                  const EnchantmentDesc& desc)
{
    clearNodeEnchantment(node);
    const EnchantmentDesc clean = sanitizeEnchantmentDesc(desc);
    if (clean.strength <= 0.0f)
        return;

    const glm::vec3 scroll = clean.scroll * clean.palette.scrollDirection;
    Ogre::SceneNode* root = mImpl->node(node, "setNodeEnchantment");
    const auto children = [](Ogre::SceneNode* current) {
        std::vector<Ogre::SceneNode*> result;
        for (auto* child : current->getChildren())
            if (auto* sceneNode = dynamic_cast<Ogre::SceneNode*>(child))
                result.push_back(sceneNode);
        return result;
    };
    for (Ogre::SceneNode* current :
         collectEnchantmentTargets(root, clean.recursive, children))
        for (size_t i = 0; i < current->numAttachedObjects(); ++i)
            if (auto* entity = dynamic_cast<Ogre::Entity*>(
                    current->getAttachedObject(i))) {
                for (Ogre::SubEntity* sub : entity->getSubEntities()) {
                    // An independently enchanted descendant may already own
                    // this submesh. Restore it before cloning so passes never
                    // stack.
                    mImpl->restoreEnchantment(sub);
                    auto saved = mImpl->savedMaterials.find(sub);
                    const std::string baseName =
                        saved != mImpl->savedMaterials.end()
                            ? saved->second : sub->getMaterialName();
                    Ogre::MaterialPtr base =
                        Ogre::MaterialManager::getSingleton().getByName(
                            baseName);
                    if (!base)
                        continue;
                    const std::string cloneName =
                        mImpl->nextName("enchantment");
                    Ogre::MaterialPtr enchanted = base->clone(cloneName);
                    Ogre::Pass* pass =
                        enchanted->getTechnique(0)->createPass();
                    pass->setSceneBlending(Ogre::SBT_ADD);
                    pass->setDepthWriteEnabled(false);
                    pass->setDepthFunction(Ogre::CMPF_LESS_EQUAL);
                    pass->setCullingMode(Ogre::CULL_NONE);
                    pass->setLightingEnabled(false);
                    pass->setVertexProgram("Enchantment/VS");
                    pass->setFragmentProgram("Enchantment/FS");
                    Ogre::GpuProgramParametersSharedPtr params =
                        pass->getFragmentProgramParameters();
                    params->setNamedConstant(
                        "enchantColour",
                        Ogre::Vector4(clean.palette.colour.r,
                                      clean.palette.colour.g,
                                      clean.palette.colour.b,
                                      clean.palette.colour.a));
                    params->setNamedConstant("enchantStrength",
                                             clean.strength);
                    params->setNamedConstant("enchantRuneScale",
                                             clean.runeScale);
                    params->setNamedConstant(
                        "enchantScroll",
                        Ogre::Vector3(scroll.x, scroll.y, scroll.z));
                    params->setNamedConstant("enchantPulseSpeed",
                                             clean.pulseSpeed);
                    params->setNamedConstant("enchantPulseDepth",
                                             clean.pulseDepth);
                    params->setNamedConstant("enchantEdgeIntensity",
                                             clean.edgeIntensity);
                    params->setNamedConstant("enchantBandCount",
                                             clean.bandCount);
                    params->setNamedConstant("enchantPixelScale",
                                             clean.pixelScale);
                    params->setNamedConstant("enchantCoreBoost",
                                             clean.coreBoost);
                    enchanted->load();
                    mImpl->enchantments.replace(
                        sub, {baseName, cloneName, root});
                    if (saved != mImpl->savedMaterials.end())
                        saved->second = cloneName;
                    else
                        sub->setMaterialName(cloneName);
                }
            }
}

void Renderer::setNodeEnchantment(NodeHandle node,
                                  const EnchantmentPalette& palette,
                                  float strength)
{
    EnchantmentDesc desc;
    desc.palette = palette;
    desc.strength = strength;
    setNodeEnchantment(node, desc);
}

std::vector<std::string> Renderer::materialNames() const
{
    std::vector<std::string> out;
    auto& mm = Ogre::MaterialManager::getSingleton();
    auto it = mm.getResourceIterator();
    while (it.hasMoreElements()) {
        const std::string& n = it.getNext()->getName();
        if (n.empty()) continue;
        // Filter engine/Ogre internals + generated helper materials.
        if (n.rfind("Ogre/", 0) == 0) continue;
        if (n.rfind("__", 0) == 0) continue;                 // preview/internal
        if (n.rfind("BaseWhite", 0) == 0) continue;
        if (n.rfind("Sprite/", 0) == 0) continue;            // per-clip generated
        if (n.find("DebugWireframe") != std::string::npos) continue;
        if (mImpl->enchantments.containsGeneratedMaterial(n)) continue;
        out.push_back(n);
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

bool Renderer::materialAvailable(const std::string& materialName) const
{
    return !materialName.empty() &&
           bool(Ogre::MaterialManager::getSingleton().getByName(
               materialName));
}

bool Renderer::nodeWorldBounds(NodeHandle node, glm::vec3& center,
                               float& radius) const
{
    if (!node.valid() || node.id > mImpl->nodes.size())
        return false;
    Ogre::SceneNode* n = mImpl->nodes[node.id - 1];
    if (!n)
        return false;
    n->_updateBounds();
    Ogre::AxisAlignedBox box;
    box.setNull();
    std::function<void(Ogre::SceneNode*)> merge = [&](Ogre::SceneNode* sn) {
        for (size_t i = 0; i < sn->numAttachedObjects(); ++i)
            if (auto* e = dynamic_cast<Ogre::Entity*>(sn->getAttachedObject(i)))
                box.merge(e->getWorldBoundingBox(true));
        for (auto* child : sn->getChildren())
            if (auto* cs = dynamic_cast<Ogre::SceneNode*>(child))
                merge(cs);
    };
    merge(n);
    if (box.isNull())
        return false;
    const Ogre::Vector3 c = box.getCenter();
    const Ogre::Vector3 h = box.getHalfSize();
    center = glm::vec3(c.x, c.y, c.z);
    radius = std::max(0.05f, h.length());
    return true;
}

void Renderer::destroyNode(NodeHandle node)
{
    // Reclaim particle systems throughout the subtree before the Ogre nodes
    // disappear. Merely detaching them would leave looping systems alive in
    // Particles::mLive forever and make their handles impossible to stop.
    std::vector<ParticlesHandle> particleHandles;
    std::function<void(NodeHandle)> gatherParticles = [&](NodeHandle current) {
        const NodeRecord* record = mImpl->mScene.find(current);
        if (!record) return;
        for (const AttachRecord& attachment : record->attachments)
            if (attachment.kind == NodeAttachKind::Particles)
                particleHandles.push_back(
                    ParticlesHandle{static_cast<uint32_t>(attachment.handle)});
        for (NodeHandle child : record->children)
            gatherParticles(child);
    };
    gatherParticles(node);
    for (ParticlesHandle particles : particleHandles)
        despawnParticles(particles);

    mImpl->mScene.removeNode(node);
    if (!node.valid() || node.id > mImpl->nodes.size()) return;
    Ogre::SceneNode* n = mImpl->nodes[node.id - 1];
    if (!n) return;
    Ogre::SceneManager* sm = mImpl->core.sceneMgr();
    mImpl->clearEnchantmentSubtree(n);

    // Snapshot attached objects (can't mutate the node's map while iterating).
    std::vector<Ogre::MovableObject*> objs;
    objs.reserve(n->numAttachedObjects());
    for (size_t i = 0; i < n->numAttachedObjects(); ++i)
        objs.push_back(n->getAttachedObject(i));
    for (Ogre::MovableObject* o : objs) {
        n->detachObject(o);
        // Pool-owned particle systems recycle themselves; only detach them.
        if (o->getMovableType() == "ParticleSystem") continue;
        if (o->getMovableType() == "Light")
            for (auto& lp : mImpl->lights) if (lp == o) lp = nullptr;
        sm->destroyMovableObject(o); // handles Light/Entity/etc.
    }
    n->removeAndDestroyAllChildren();
    if (n->getParentSceneNode()) n->getParentSceneNode()->removeChild(n);
    sm->destroySceneNode(n);
    mImpl->nodes[node.id - 1] = nullptr; // stale slot; handle never reused
}

void Renderer::attachMesh(NodeHandle node, MeshHandle mesh,
                          const std::string& materialName, bool castShadows,
                          bool renderOnTop)
{
    attachMesh(node, mesh, materialName,
               mImpl->prototypes.materialFor(materialName), castShadows,
               renderOnTop);
}

void Renderer::attachMesh(NodeHandle node, MeshHandle mesh,
                          const std::string& materialName,
                          const std::string& fallbackMaterial,
                          bool castShadows, bool renderOnTop)
{
    std::string resolved = materialName;
    if (!Ogre::MaterialManager::getSingleton().getByName(resolved)) {
        if (mImpl->missingMaterialWarnings.shouldLog(materialName, true))
            log::error("Renderer: material '%s' is missing; using '%s'",
                       materialName.c_str(), fallbackMaterial.c_str());
        resolved = fallbackMaterial;
        if (!Ogre::MaterialManager::getSingleton().getByName(resolved)) {
            if (mImpl->missingMaterialWarnings.shouldLog(
                    "fallback:" + fallbackMaterial, true))
                log::error(
                    "Renderer: fallback material '%s' is missing; using '%s'",
                    fallbackMaterial.c_str(),
                    prototype::kSurfaceMaterial);
            resolved = prototype::kSurfaceMaterial;
        }
    }
    Ogre::Entity* e =
        mImpl->core.sceneMgr()->createEntity(mImpl->mesh(mesh, "attachMesh"));
    e->setMaterialName(resolved);
    e->setCastShadows(castShadows);
    if (renderOnTop)
        e->setRenderQueueGroup(Ogre::RENDER_QUEUE_8);
    if (mImpl->env.wireframe) { // debug view active: join it immediately
        for (Ogre::SubEntity* se : e->getSubEntities()) {
            mImpl->savedMaterials[se] = resolved;
            se->setMaterialName("PSX/DebugWireframe");
        }
    }
    mImpl->node(node, "attachMesh")->attachObject(e);
    mImpl->mScene.addAttachment(node, {NodeAttachKind::Mesh, 0, resolved});
}

void Renderer::attachMesh(NodeHandle node, MeshHandle mesh,
                          const ResolvedModelMaterial& material,
                          bool castShadows, bool renderOnTop)
{
    if (material.usedFallback &&
        mImpl->missingMaterialWarnings.shouldLog(material.requested, true))
        log::error("Renderer: material '%s' is missing; using '%s'",
                   material.requested.c_str(), material.material.c_str());
    // Resolve against what was originally asked for, not the already-substituted
    // name, so a missing portal still lands on the portal prototype.
    attachMesh(node, mesh, material.material,
               mImpl->prototypes.materialFor(material.requested), castShadows,
               renderOnTop);
}

std::string Renderer::createSpriteMaterial(const SpriteClip& clip)
{
    const char* base = clip.blend == SpriteBlend::Alpha ? "Sprite/Alpha"
                     : clip.blend == SpriteBlend::Additive ? "Sprite/Additive"
                     : clip.blend == SpriteBlend::Overlay ? "Sprite/Overlay"
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
        log::error("Sprite: texture '%s' is missing; using %s",
                   texture.c_str(), prototype::kSpriteTexture);
        texture = prototype::kSpriteTexture;
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
    SpriteHandle sh{static_cast<uint32_t>(mImpl->sprites.size())};
    mImpl->mScene.addAttachment(node, {NodeAttachKind::Sprite, sh.id, ""});
    return sh;
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
    const auto textWidth = [&](const std::string& value) {
        float width = 0.0f;
        for (unsigned char c : value)
            width += font->FindGlyph(c)->AdvanceX;
        return width;
    };
    // The current UI font is ASCII. Collapse UTF-8 punctuation to one dash
    // instead of producing one '?' for every continuation byte.
    std::string printable;
    for (size_t i = 0; i < text.size();) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (c < 128) {
            printable.push_back(char(c));
            ++i;
        } else {
            printable.push_back('-');
            ++i;
            while (i < text.size() &&
                   (static_cast<unsigned char>(text[i]) & 0xc0) == 0x80)
                ++i;
        }
    }
    std::vector<std::string> lines(1);
    std::string word;
    const auto appendWord = [&]() {
        if (word.empty()) return;
        std::string candidate = lines.back().empty()
            ? word : lines.back() + " " + word;
        if (!lines.back().empty() &&
            textWidth(candidate) > float(std::max(32, style.maxWidthPixels))) {
            lines.push_back(word);
        } else {
            lines.back() = std::move(candidate);
        }
        word.clear();
    };
    for (char c : printable) {
        if (c == '\n') {
            appendWord();
            if (!lines.back().empty()) lines.emplace_back();
        } else if (c == ' ' || c == '\t') {
            appendWord();
        } else {
            word.push_back(c);
        }
    }
    appendWord();
    if (lines.back().empty() && lines.size() > 1) lines.pop_back();
    std::vector<float> lineWidths;
    float widest = 0.0f;
    for (const std::string& line : lines) {
        const float lineWidth = textWidth(line);
        lineWidths.push_back(lineWidth);
        widest = std::max(widest, lineWidth);
    }
    const int lineHeight = int(std::ceil(font->FontSize));
    const int lineSpacing = std::max(0, style.lineSpacingPixels);
    const int accentWidth = std::max(0, style.accentWidthPixels);
    const int accentGutter = accentWidth > 0 ? accentWidth + 2 : 0;
    const int width = std::max(
        8, int(std::ceil(widest)) + padding * 2 + accentGutter);
    const int height = std::max(
        8, lineHeight * int(lines.size()) +
               lineSpacing * std::max(0, int(lines.size()) - 1) + padding * 2);
    std::vector<unsigned char> pixels(size_t(width * height * 4), 0);
    std::vector<std::pair<std::regex, glm::vec4>> colourRules;
    colourRules.reserve(style.colourRules.size());
    for (const auto& rule : style.colourRules) {
        try {
            colourRules.emplace_back(
                std::regex(rule.pattern, std::regex::icase), rule.colour);
        } catch (const std::regex_error&) {
            log::error("TextSprite: invalid colour regex '%s'",
                       rule.pattern.c_str());
        }
    }
    const auto byte = [](float v) {
        return static_cast<unsigned char>(glm::clamp(v, 0.0f, 1.0f) * 255.0f);
    };
    const auto put = [&](int x, int y, glm::vec4 colour) {
        if (x < 0 || y < 0 || x >= width || y >= height) return;
        const size_t i = size_t((y * width + x) * 4);
        pixels[i + 0] = byte(colour.r); pixels[i + 1] = byte(colour.g);
        pixels[i + 2] = byte(colour.b); pixels[i + 3] = byte(colour.a);
    };
    // Overlay plaques are intentionally opaque, so antialiased glyph alpha
    // must be resolved into the plaque here rather than left for GPU blend.
    const auto blendText = [&](int x, int y, glm::vec4 colour, float coverage) {
        if (x < 0 || y < 0 || x >= width || y >= height) return;
        const size_t i = size_t((y * width + x) * 4);
        const float a = glm::clamp(coverage * colour.a, 0.0f, 1.0f);
        pixels[i + 0] = byte(glm::mix(float(pixels[i + 0]) / 255.0f, colour.r, a));
        pixels[i + 1] = byte(glm::mix(float(pixels[i + 1]) / 255.0f, colour.g, a));
        pixels[i + 2] = byte(glm::mix(float(pixels[i + 2]) / 255.0f, colour.b, a));
        pixels[i + 3] = 255;
    };
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            const bool border = x < 2 || y < 2 || x >= width - 2 || y >= height - 2;
            put(x, y, border ? style.borderColour : style.backgroundColour);
        }
    for (int y = 2; y < height - 2; ++y)
        for (int x = 2; x < 2 + accentWidth; ++x)
            put(x, y, style.accentColour);
    for (size_t lineIndex = 0; lineIndex < lines.size(); ++lineIndex) {
      float pen = float(accentGutter) +
          (float(width - accentGutter) - lineWidths[lineIndex]) * 0.5f;
      const float lineTop = float(padding + int(lineIndex) *
                                  (lineHeight + lineSpacing));
      const std::string& line = lines[lineIndex];
      for (size_t charIndex = 0; charIndex < line.size(); ++charIndex) {
        const unsigned char c = static_cast<unsigned char>(line[charIndex]);
        const ImFontGlyph* glyph = font->FindGlyph(c);
        glm::vec4 colour = style.textColour;
        if (c != ' ') {
            size_t begin = charIndex, end = charIndex + 1;
            while (begin > 0 && line[begin - 1] != ' ') --begin;
            while (end < line.size() && line[end] != ' ') ++end;
            const std::string currentWord = line.substr(begin, end - begin);
            for (const auto& rule : colourRules)
                if (std::regex_search(currentWord, rule.first)) {
                    colour = rule.second;
                    break;
                }
        }
        const int sx0 = int(std::round(glyph->U0 * atlasWidth));
        const int sy0 = int(std::round(glyph->V0 * atlasHeight));
        const int sx1 = int(std::round(glyph->U1 * atlasWidth));
        const int sy1 = int(std::round(glyph->V1 * atlasHeight));
        const int dx0 = int(std::round(pen + glyph->X0));
        const int dy0 = int(std::round(lineTop + glyph->Y0));
        for (int sy = sy0; sy < sy1; ++sy)
            for (int sx = sx0; sx < sx1; ++sx) {
                const unsigned char alpha = atlasPixels[(sy * atlasWidth + sx) * 4 + 3];
                if (!alpha) continue;
                blendText(dx0 + sx - sx0, dy0 + sy - sy0, colour,
                          float(alpha) / 255.0f);
            }
        pen += glyph->AdvanceX;
      }
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
    const float pixelToWorld = style.worldHeight /
        float(lineHeight + padding * 2);
    clip.worldSize = {float(width) * pixelToWorld,
                      float(height) * pixelToWorld};
    // Depth-independent and opaque: glyph antialiasing was already resolved
    // into the plaque above, so nothing here needs GPU blending.
    clip.blend = SpriteBlend::Overlay;
    const SpriteHandle sprite = attachSprite(node, clip);
    // Text is UI, so it must not be resampled through the pixelated buffer:
    // PSX/Stylized draws these plaques itself, at native window resolution,
    // in an extra render_scene pass on target_output (after stylize/bloom/
    // dither). Two things put them there and only there:
    //   - queue 100 (OVERLAY), which Ogre's implicit original-scene pass
    //     (queues 0..95) never touches, and which the compositor chain's
    //     render-queue listener refuses to skip on any target;
    //   - the reserved visibility bit, which `target mrt` masks off -- that
    //     is what actually keeps the plaque out of the low-res pass, since
    //     queue 100 alone would still be drawn into it.
    Ogre::BillboardSet* set = mImpl->sprites[sprite.id - 1];
    set->setRenderQueueGroup(Ogre::RENDER_QUEUE_OVERLAY);
    set->setVisibilityFlags(kFullResUiVisibilityFlag);
    set->setCastShadows(false); // world UI, never a stencil-shadow caster
    return sprite;
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
    std::string resolved = materialName;
    if (!Ogre::MaterialManager::getSingleton().getByName(resolved)) {
        const std::string fallback =
            mImpl->prototypes.materialFor(materialName);
        log::error("Renderer: material '%s' is missing; using '%s'",
                   materialName.c_str(), fallback.c_str());
        resolved = fallback;
    }
    mImpl->staticBatches[batch.id - 1].recs.push_back(
        {mesh, resolved, pos, yawDeg});
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
    mImpl->particles.clear(); // drop pooled-system bookkeeping before Ogre frees them
    mImpl->clearEnchantmentSubtree(sm->getRootSceneNode());
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
    // meshes this Renderer created are in the mesh resource registry.
    auto& mm = Ogre::MeshManager::getSingleton();
    for (const std::string& name : mImpl->meshes.takeAll())
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
    for (const auto& state : mImpl->enchantments.takeAll())
        if (materials.getByName(state.generatedMaterial))
            materials.remove(state.generatedMaterial);
    mImpl->debugLines = nullptr; // destroyAllManualObjects freed it
    mImpl->mScene.clear();
    mImpl->nodes.push_back(sm->getRootSceneNode());
}

ParticleEffectId Renderer::registerParticleEffect(const ParticleEffectDesc& desc)
{
    return mImpl->particles.registerEffect(desc);
}

ParticleEffectId Renderer::particleEffectId(const std::string& name)
{
    return mImpl->particles.find(name);
}

ParticlesHandle Renderer::spawnParticles(const std::string& name, NodeHandle parent,
                                         glm::vec3 localPos)
{
    return spawnParticles(mImpl->particles.find(name), parent, localPos);
}

ParticlesHandle Renderer::spawnParticles(
    const std::string& name, NodeHandle parent,
    const ParticleSpawnOptions& options)
{
    return spawnParticles(mImpl->particles.find(name), parent, options);
}

ParticlesHandle Renderer::spawnParticles(
    const std::string& name, NodeHandle parent, glm::vec3 localPos,
    const ParticleSpawnOptions& options)
{
    return spawnParticles(mImpl->particles.find(name), parent, localPos,
                          options);
}

ParticlesHandle Renderer::spawnParticles(const std::string& name, glm::vec3 worldPos)
{
    return spawnParticles(mImpl->particles.find(name), worldPos);
}

ParticlesHandle Renderer::spawnParticles(
    const std::string& name, glm::vec3 worldPos,
    const ParticleSpawnOptions& options)
{
    return spawnParticles(mImpl->particles.find(name), worldPos, options);
}

ParticlesHandle Renderer::spawnParticles(ParticleEffectId fx, NodeHandle parent,
                                         glm::vec3 localPos)
{
    return spawnParticles(fx, parent, localPos, ParticleSpawnOptions{});
}

ParticlesHandle Renderer::spawnParticles(
    ParticleEffectId fx, NodeHandle parent,
    const ParticleSpawnOptions& options)
{
    return spawnParticles(fx, parent, glm::vec3(0.0f), options);
}

ParticlesHandle Renderer::spawnParticles(
    ParticleEffectId fx, NodeHandle parent, glm::vec3 localPos,
    const ParticleSpawnOptions& options)
{
    Ogre::SceneNode* n = mImpl->node(parent, "spawnParticles");
    // A non-zero local offset gets its own child node so the particle inherits
    // an offset transform without disturbing the parent (the particle system
    // itself has no per-attachment offset in Ogre).
    const bool ownsNode = glm::dot(localPos, localPos) > 1e-8f;
    if (ownsNode)
        n = n->createChildSceneNode(toOgre(localPos));
    ParticlesHandle ph =
        mImpl->particles.spawn(fx, n, glm::vec3(0.0f), options, ownsNode);
    if (ph.valid())
        mImpl->mScene.addAttachment(parent, {NodeAttachKind::Particles, ph.id, ""});
    else if (ownsNode)
        mImpl->core.sceneMgr()->destroySceneNode(n);
    return ph;
}

ParticlesHandle Renderer::spawnParticles(ParticleEffectId fx, glm::vec3 worldPos)
{
    return spawnParticles(fx, worldPos, ParticleSpawnOptions{});
}

ParticlesHandle Renderer::spawnParticles(
    ParticleEffectId fx, glm::vec3 worldPos,
    const ParticleSpawnOptions& options)
{
    Ogre::SceneNode* n =
        mImpl->core.sceneMgr()->getRootSceneNode()->createChildSceneNode(toOgre(worldPos));
    ParticlesHandle ph =
        mImpl->particles.spawn(fx, n, glm::vec3(0.0f), options, true);
    if (!ph.valid())
        mImpl->core.sceneMgr()->destroySceneNode(n);
    return ph;
}

void Renderer::stopParticles(ParticlesHandle h) { mImpl->particles.stop(h); }
void Renderer::despawnParticles(ParticlesHandle h) {
    mImpl->particles.despawn(h);
    mImpl->mScene.removeAttachment(NodeAttachKind::Particles, h.id);
}
void Renderer::setParticleQuality(float q) { mImpl->particles.setQuality(q); }
void Renderer::updateParticles(float dt) {
    for (uint32_t id : mImpl->particles.update(dt))
        mImpl->mScene.removeAttachment(NodeAttachKind::Particles, id);
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
    LightHandle lh{static_cast<uint32_t>(mImpl->lights.size())};
    mImpl->mScene.addAttachment(node, {NodeAttachKind::Light, lh.id, ""});
    return lh;
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
    if (!mat) {
        // The mesh using this material already drew with the prototype
        // fallback; there is simply nothing here to parameterise. Tuning a
        // missing material is not worth killing the frame over, and we must not
        // write the params onto the shared prototype -- every other missing
        // material would inherit them. Warn once per name and move on.
        static std::set<std::string> warned;
        if (warned.insert(materialName).second)
            log::error("Renderer: cannot set '%s' on missing material '%s'; "
                       "it is drawing with the prototype fallback",
                       paramName.c_str(), materialName.c_str());
        return;
    }
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

void Renderer::setRenderResolution(int width, int height)
{
    mImpl->core.setRenderResolution(width, height);
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

void Renderer::enableEditorViewport(int w, int h)
{
    mImpl->core.enableOffscreenViewport(w, h);
}

void Renderer::resizeEditorViewport(int w, int h)
{
    mImpl->core.resizeOffscreenViewport(w, h);
}

uint64_t Renderer::editorViewportTextureId() const
{
    return mImpl->core.viewportTextureId();
}

void Renderer::setEditorCameraPose(const glm::vec3& pos, const glm::quat& orient,
                                   float fovDeg)
{
    mImpl->core.setEditorCameraPose(pos.x, pos.y, pos.z, orient.w, orient.x,
                                    orient.y, orient.z, fovDeg);
}

void Renderer::setEditorViewportBackground(const glm::vec3& colour)
{
    mImpl->core.setOffscreenBackground(colour.r, colour.g, colour.b);
}

void Renderer::enableMaterialThumbnail(int size)
{
    mImpl->core.enableThumbnailViewport(size);
}

void Renderer::setMaterialThumbnailCamera(const glm::vec3& position,
                                          const glm::quat& orientation,
                                          float fovDeg)
{
    mImpl->core.setThumbnailCameraPose(position.x, position.y, position.z,
                                       orientation.w, orientation.x,
                                       orientation.y, orientation.z, fovDeg);
}

uint64_t Renderer::materialThumbnailTextureId() const
{
    return mImpl->core.thumbnailTextureId();
}

void Renderer::setNodeThumbnailOnly(NodeHandle node, bool thumbnailOnly)
{
    Ogre::SceneNode* scene = mImpl->node(node, "setNodeThumbnailOnly");
    // The flag rides on the attached movables, not the node: Ogre filters
    // visibility per renderable, and a SceneNode has no flags of its own.
    for (Ogre::MovableObject* object : scene->getAttachedObjects()) {
        object->setVisibilityFlags(thumbnailOnly ? kThumbnailVisibilityFlag
                                                 : kWorldVisibilityMask);
    }
}

void Renderer::setDebugLines(const std::vector<DebugLine>& lines)
{
    Ogre::SceneManager* sm = mImpl->core.sceneMgr();
    // Lazy creation (or re-creation after clearScene).
    if (!mImpl->debugLines) {
        mImpl->debugLines = sm->createManualObject("__eng_debug_lines");
        mImpl->debugLines->setDynamic(true);
        sm->getRootSceneNode()->createChildSceneNode()->attachObject(
            mImpl->debugLines);
    }
    mImpl->debugLines->clear();
    if (lines.empty())
        return;
    // PSX/DebugLines: unlit, per-vertex colour, depth_write off (declared in
    // engine/assets/materials/psx.material + debug_lines.frag).
    const std::string matName = "PSX/DebugLines";
    mImpl->debugLines->begin(matName, Ogre::RenderOperation::OT_LINE_LIST);
    for (const auto& l : lines) {
        mImpl->debugLines->position(l.a.x, l.a.y, l.a.z);
        mImpl->debugLines->colour(Ogre::ColourValue(l.colour.r, l.colour.g, l.colour.b, 1.0f));
        mImpl->debugLines->position(l.b.x, l.b.y, l.b.z);
        mImpl->debugLines->colour(Ogre::ColourValue(l.colour.r, l.colour.g, l.colour.b, 1.0f));
    }
    mImpl->debugLines->end();
}

void Renderer::frameStats(size_t& batches, size_t& triangles) const
{
    mImpl->core.frameStats(batches, triangles);
}

glm::mat4 Renderer::cameraViewProj() const
{
    Ogre::Camera* cam = mImpl->core.camera();
    if (!cam)
        return glm::mat4(1.0f);
    // Ogre matrices are row-major; glm is column-major -> transpose on copy.
    const Ogre::Matrix4 vp = cam->getProjectionMatrix() * cam->getViewMatrix();
    glm::mat4 g;
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            g[c][r] = float(vp[r][c]);
    return g;
}

void Renderer::setDebugLinesXray(bool xray)
{
    // Flip depth-check on the PSX/DebugLines pass. Off (xray) => lines pass the
    // depth test everywhere and draw over all geometry; on => normal occlusion.
    Ogre::MaterialPtr m = Ogre::MaterialManager::getSingleton().getByName(
        "PSX/DebugLines");
    if (!m || m->getTechniques().empty())
        return;
    Ogre::Technique* t = m->getTechnique(0);
    if (t->getPasses().empty())
        return;
    t->getPass(0)->setDepthCheckEnabled(!xray);
}

void Renderer::setLightRange(LightHandle light, float range)
{
    // Mirror attachLight: Ogre's attenuation range is pinned huge so the light
    // stays registered against the frustum; the real falloff range rides in the
    // constant-attenuation slot (arg 2), which the PSX shader reads as
    // lightAtten.y (see psx_lighting.glsl).
    if (light.valid() && light.id <= mImpl->lights.size())
        mImpl->lights[light.id - 1]->setAttenuation(1000.0f, range, 0.0f, 0.0f);
}

SceneView Renderer::scene() const { return SceneView(*this); }

std::vector<NodeHandle> SceneView::roots() const
{
    return mRenderer->mImpl->mScene.roots();
}

std::vector<NodeHandle> SceneView::childrenOf(NodeHandle n) const
{
    const NodeRecord* r = mRenderer->mImpl->mScene.find(n);
    return r ? r->children : std::vector<NodeHandle>{};
}

bool SceneView::info(NodeHandle n, NodeInfo& out) const
{
    auto& impl = *mRenderer->mImpl;
    const NodeRecord* rec = impl.mScene.find(n);
    if (!rec) return false;
    out.handle = rec->handle; out.parent = rec->parent; out.name = rec->name;
    out.attachments.clear();
    for (const AttachRecord& a : rec->attachments)
        out.attachments.push_back({a.kind, a.handle, a.label});
    if (n.valid() && n.id <= impl.nodes.size()) {
        Ogre::SceneNode* sn = impl.nodes[n.id - 1];
        const Ogre::Vector3 p = sn->getPosition();
        const Ogre::Quaternion q = sn->getOrientation();
        const Ogre::Vector3 s = sn->getScale();
        out.position = glm::vec3(p.x, p.y, p.z);
        out.orientation = glm::quat(q.w, q.x, q.y, q.z);
        out.scale = glm::vec3(s.x, s.y, s.z);
    }
    out.visible = true; // MVP: visibility read-back not tracked
    return true;
}

bool SceneView::lightInfo(LightHandle l, LightDesc& out) const
{
    auto& impl = *mRenderer->mImpl;
    if (!l.valid() || l.id > impl.lights.size()) return false;
    Ogre::Light* lt = impl.lights[l.id - 1];
    const Ogre::ColourValue c = lt->getDiffuseColour();
    out.colour = glm::vec3(c.r, c.g, c.b);
    // Real falloff range lives in the constant-attenuation slot (see
    // setLightRange / attachLight), not Ogre's attenuation range.
    out.range = lt->getAttenuationConstant();
    out.type = (lt->getType() == Ogre::Light::LT_DIRECTIONAL)
                   ? LightDesc::Type::Directional : LightDesc::Type::Point;
    out.castShadows = lt->getCastShadows();
    return true;
}

namespace detail {

RenderCore& coreOf(Renderer& r) { return r.mImpl->core; }

void registerRoot(Renderer& r)
{
    r.mImpl->nodes.push_back(r.mImpl->core.sceneMgr()->getRootSceneNode());
    r.mImpl->particles.init(r.mImpl->core.sceneMgr());
    particle_presets::registerDefaults(r);
}

} // namespace detail

} // namespace eng
