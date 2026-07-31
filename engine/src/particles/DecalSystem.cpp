#include <eng/particles/DecalSystem.h>

#include "ParticleSim.h"   // DecalRequest

#include <eng/Log.h>

#include <OgreAxisAlignedBox.h>
#include <OgreCamera.h>
#include <OgreHardwareBufferManager.h>
#include <OgreMaterialManager.h>
#include <OgreSceneManager.h>
#include <OgreSceneNode.h>
#include <OgreSimpleRenderable.h>
#include <OgreTechnique.h>

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <random>
#include <unordered_map>
#include <vector>

namespace eng {
namespace {

// Vertex buffer sources and instance attribute slots, matching ParticleBatch:
// source 0 is the shared unit quad, source 1 the per-instance stream, and the
// instance attributes ride texture coordinate sets because that is the only
// semantic family GL3Plus maps to free indexable attribute slots.
constexpr unsigned short kBaseSource = 0;
constexpr unsigned short kInstanceSource = 1;
constexpr unsigned short kInstPosSizeUv = 1;
constexpr unsigned short kInstColourUv = 2;
constexpr unsigned short kInstTangentUv = 3;
constexpr unsigned short kInstNormalUv = 4;

// The merge grid is uniform, so it cannot be sized from a per-profile radius.
// One metre matches the scale of a room in this game and keeps the neighbour
// search at 27 cells for any sane merge radius.
constexpr float kCellSize = 1.0f;

// Two decals only merge when they lie on what is plausibly the same surface:
// nearly parallel normals *and* nearly coplanar. Without the plane test a pool
// on the floor would swallow a hit on the wall it meets.
constexpr float kMergeNormalDot = 0.94f;   // ~20 degrees
constexpr float kMergePlaneEpsilon = 0.06f;

// Uploaded verbatim, so this layout is part of the vertex declaration below.
// The surface frame is sent as two vec4s rather than a matrix because the
// bitangent is a cross product the vertex shader can afford.
struct DecalInstance {
    glm::vec3 pos;
    float     size;
    glm::vec4 colour;
    glm::vec4 tangent;   // xyz in-plane axis, w unused
    glm::vec4 normal;    // xyz surface normal, w unused
};

void buildQuad(std::vector<float>& verts, std::vector<uint16_t>& indices)
{
    const float p[4][5] = {
        {-0.5f, -0.5f, 0.0f, 0.0f, 1.0f},
        { 0.5f, -0.5f, 0.0f, 1.0f, 1.0f},
        { 0.5f,  0.5f, 0.0f, 1.0f, 0.0f},
        {-0.5f,  0.5f, 0.0f, 0.0f, 0.0f},
    };
    for (const auto& v : p)
        verts.insert(verts.end(), v, v + 5);
    const uint16_t idx[6] = {0, 1, 2, 0, 2, 3};
    indices.assign(idx, idx + 6);
}

// One draw call worth of decals sharing a material. Structurally identical to
// eng::ParticleBatch; it exists separately only because the instance record
// carries a surface frame instead of a screen-space rotation.
class DecalBatch final : public Ogre::SimpleRenderable {
public:
    DecalBatch(Ogre::SceneManager* sm, const std::string& material,
               std::size_t capacity)
        : Ogre::SimpleRenderable(), mSm(sm)
    {
        buildBaseMesh();
        setCapacity(std::max<std::size_t>(capacity, 1));
        applyMaterial(material);
        // Ogre::RENDER_QUEUE_MAIN, same group as particles: blended decals land
        // in its transparent sub-list, after opaque world geometry.
        setRenderQueueGroup(50);
        Ogre::MovableObject::setVisible(false);
        mNode = mSm->getRootSceneNode()->createChildSceneNode();
        mNode->attachObject(this);
    }

    ~DecalBatch() override { destroy(); }

    void setCapacity(std::size_t maxInstances)
    {
        maxInstances = std::max<std::size_t>(maxInstances, 1);
        if (maxInstances == mCapacity && mInstanceBuffer)
            return;
        mCapacity = maxInstances;
        mStaging.reserve(mCapacity);
        auto& hbm = Ogre::HardwareBufferManager::getSingleton();
        mInstanceBuffer = hbm.createVertexBuffer(sizeof(DecalInstance), mCapacity,
                                                 Ogre::HBU_CPU_TO_GPU);
        mInstanceBuffer->setIsInstanceData(true);
        mInstanceBuffer->setInstanceDataStepRate(1);
        mRenderOp.vertexData->vertexBufferBinding->setBinding(kInstanceSource,
                                                              mInstanceBuffer);
    }

    void beginFrame() { mStaging.clear(); }

    void push(const DecalInstance& instance)
    {
        if (mStaging.size() < mCapacity)
            mStaging.push_back(instance);
    }

    void sort(const glm::vec3& cameraWorldPos)
    {
        std::sort(mStaging.begin(), mStaging.end(),
                  [&](const DecalInstance& a, const DecalInstance& b) {
                      const glm::vec3 da = a.pos - cameraWorldPos;
                      const glm::vec3 db = b.pos - cameraWorldPos;
                      return glm::dot(da, da) > glm::dot(db, db);
                  });
    }

    void endFrame()
    {
        const std::size_t count = mStaging.size();
        mRenderOp.numberOfInstances = static_cast<uint32_t>(count);
        if (count == 0) {
            Ogre::MovableObject::setVisible(false);
            return;
        }

        void* dst = mInstanceBuffer->lock(0, count * sizeof(DecalInstance),
                                          Ogre::HardwareBuffer::HBL_DISCARD);
        std::memcpy(dst, mStaging.data(), count * sizeof(DecalInstance));
        mInstanceBuffer->unlock();

        Ogre::AxisAlignedBox box;
        for (const DecalInstance& d : mStaging) {
            const float r = std::max(d.size, 0.0f);
            box.merge(Ogre::Vector3(d.pos.x - r, d.pos.y - r, d.pos.z - r));
            box.merge(Ogre::Vector3(d.pos.x + r, d.pos.y + r, d.pos.z + r));
        }
        setBoundingBox(box);
        Ogre::MovableObject::setVisible(mWantVisible);
    }

    void setVisible(bool visible)
    {
        mWantVisible = visible;
        Ogre::MovableObject::setVisible(visible && !mStaging.empty());
    }

    void destroy()
    {
        if (mNode) {
            mNode->detachObject(this);
            if (mSm)
                mSm->destroySceneNode(mNode);
            mNode = nullptr;
        }
    }

    // The node is never moved, so instance positions are world-space and the
    // auto-bound worldViewProj reduces to viewProj, exactly as the particle
    // batch shaders already assume.
    Ogre::Real getBoundingRadius() const override { return 0.71f; }

    Ogre::Real getSquaredViewDepth(const Ogre::Camera* cam) const override
    {
        return getBoundingBox().getCenter().squaredDistance(
            cam->getDerivedPosition());
    }

private:
    void buildBaseMesh()
    {
        std::vector<float> verts;
        std::vector<uint16_t> indices;
        buildQuad(verts, indices);

        auto& hbm = Ogre::HardwareBufferManager::getSingleton();
        mRenderOp.vertexData = new Ogre::VertexData();
        mRenderOp.indexData = new Ogre::IndexData();
        mRenderOp.operationType = Ogre::RenderOperation::OT_TRIANGLE_LIST;
        mRenderOp.useIndexes = true;
        mRenderOp.useGlobalInstancing = false;

        Ogre::VertexDeclaration* decl = mRenderOp.vertexData->vertexDeclaration;
        std::size_t offset = 0;
        offset += decl->addElement(kBaseSource, offset, Ogre::VET_FLOAT3,
                                   Ogre::VES_POSITION)
                      .getSize();
        offset += decl->addElement(kBaseSource, offset, Ogre::VET_FLOAT2,
                                   Ogre::VES_TEXTURE_COORDINATES, 0)
                      .getSize();
        const std::size_t baseStride = offset;

        decl->addElement(kInstanceSource, offsetof(DecalInstance, pos),
                         Ogre::VET_FLOAT4, Ogre::VES_TEXTURE_COORDINATES,
                         kInstPosSizeUv);
        decl->addElement(kInstanceSource, offsetof(DecalInstance, colour),
                         Ogre::VET_FLOAT4, Ogre::VES_TEXTURE_COORDINATES,
                         kInstColourUv);
        decl->addElement(kInstanceSource, offsetof(DecalInstance, tangent),
                         Ogre::VET_FLOAT4, Ogre::VES_TEXTURE_COORDINATES,
                         kInstTangentUv);
        decl->addElement(kInstanceSource, offsetof(DecalInstance, normal),
                         Ogre::VET_FLOAT4, Ogre::VES_TEXTURE_COORDINATES,
                         kInstNormalUv);

        const std::size_t vertexCount = verts.size() / (baseStride / sizeof(float));
        auto base = hbm.createVertexBuffer(baseStride, vertexCount,
                                           Ogre::HBU_GPU_ONLY);
        base->writeData(0, base->getSizeInBytes(), verts.data(), true);
        mRenderOp.vertexData->vertexStart = 0;
        mRenderOp.vertexData->vertexCount = vertexCount;
        mRenderOp.vertexData->vertexBufferBinding->setBinding(kBaseSource, base);

        auto ib = hbm.createIndexBuffer(Ogre::HardwareIndexBuffer::IT_16BIT,
                                        indices.size(), Ogre::HBU_GPU_ONLY);
        ib->writeData(0, ib->getSizeInBytes(), indices.data(), true);
        mRenderOp.indexData->indexBuffer = ib;
        mRenderOp.indexData->indexStart = 0;
        mRenderOp.indexData->indexCount = indices.size();
    }

    void applyMaterial(const std::string& name)
    {
        Ogre::MaterialPtr mat =
            Ogre::MaterialManager::getSingleton().getByName(name);
        if (!mat) {
            // A missing decal material is nearly always a GLSL compile error
            // reported earlier as "has no supportable Techniques", so name it
            // instead of silently drawing white quads on every wall.
            log::error("decal batch material '%s' not found", name.c_str());
            mat = Ogre::MaterialManager::getSingleton().getByName("BaseWhite");
        }
        if (mat) {
            mat->load();
            setMaterial(mat);
        }
    }

    Ogre::SceneManager* mSm = nullptr;
    Ogre::SceneNode* mNode = nullptr;
    Ogre::HardwareVertexBufferSharedPtr mInstanceBuffer;
    std::vector<DecalInstance> mStaging;
    std::size_t mCapacity = 0;
    bool mWantVisible = true;
};

// Base materials live in engine/assets/materials/decals.material. One clone per
// texture stem carries the texture, so adding a decal texture is a PNG plus a
// TOML profile and never a material edit.
const char* baseMaterialFor(ParticleBlend blend)
{
    return blend == ParticleBlend::Additive ? "Decals/Additive" : "Decals/Alpha";
}

std::string autoMaterialName(const std::string& stem, ParticleBlend blend)
{
    return std::string("Decals/Auto/") +
           (blend == ParticleBlend::Additive ? "Add/" : "Alpha/") + stem;
}

// Creates the per-stem clone on first use. Returns the base material name when
// the stem is empty, which is what an untextured debug profile wants.
std::string resolveMaterial(const std::string& stem, ParticleBlend blend)
{
    const char* base = baseMaterialFor(blend);
    if (stem.empty())
        return base;

    const std::string name = autoMaterialName(stem, blend);
    auto& mm = Ogre::MaterialManager::getSingleton();
    if (mm.getByName(name))
        return name;

    Ogre::MaterialPtr baseMat = mm.getByName(base);
    if (!baseMat) {
        log::error("decal base material '%s' not found", base);
        return base;
    }
    Ogre::MaterialPtr clone = baseMat->clone(name);
    if (clone->getTechnique(0) && clone->getTechnique(0)->getPass(0) &&
        clone->getTechnique(0)->getPass(0)->getNumTextureUnitStates() > 0) {
        clone->getTechnique(0)->getPass(0)->getTextureUnitState(0)->setTextureName(
            stem + ".png");
    }
    return name;
}

// An in-plane axis for the quad. Crossing the normal with whichever cardinal
// axis it is least aligned to avoids the degenerate case; the caller then spins
// the result about the normal so repeated hits are not stamped identically.
glm::vec3 planeTangent(const glm::vec3& n, float angle)
{
    const glm::vec3 axis = (std::fabs(n.y) < 0.9f) ? glm::vec3(0.0f, 1.0f, 0.0f)
                                                   : glm::vec3(1.0f, 0.0f, 0.0f);
    glm::vec3 t = glm::normalize(glm::cross(axis, n));
    // Rodrigues rotation about the normal. t is already perpendicular to n, so
    // the (n . t) term vanishes and this reduces to the two-term form.
    const float c = std::cos(angle);
    const float s = std::sin(angle);
    return glm::normalize(t * c + glm::cross(n, t) * s);
}

} // namespace

struct DecalSystem::Impl {
    struct Decal {
        uint16_t  profile = 0;
        glm::vec3 pos{0.0f};
        glm::vec3 normal{0.0f, 1.0f, 0.0f};
        glm::vec3 tangent{1.0f, 0.0f, 0.0f};
        glm::vec4 colour{1.0f};
        float     size = 0.0f;
        float     targetSize = 0.0f;
        float     age = 0.0f;
        uint64_t  seq = 0;      // spawn order; the LRU key
        int64_t   cell = 0;     // merge-grid cell, cached so removal can patch it
    };

    Ogre::SceneManager* sm = nullptr;
    std::vector<DecalProfileDesc> profiles;
    std::unordered_map<std::string, uint16_t> profileIds;
    std::vector<Decal> decals;
    std::unordered_map<int64_t, std::vector<uint32_t>> grid;
    std::unordered_map<std::string, std::unique_ptr<DecalBatch>> batches;
    std::size_t budget = 256;
    uint64_t nextSeq = 1;
    bool visible = true;
    std::mt19937 rng{0x5eed1234u};

    float rand01() { return std::uniform_real_distribution<float>(0.0f, 1.0f)(rng); }
    float randSym() { return std::uniform_real_distribution<float>(-1.0f, 1.0f)(rng); }

    static int64_t cellKey(const glm::vec3& p)
    {
        // Quantise to a uniform lattice and pack. 21 bits per axis covers
        // +/- 1 million cells, far past any level this engine will load.
        const auto q = [](float v) {
            return static_cast<int64_t>(std::floor(v / kCellSize));
        };
        return ((q(p.x) & 0x1FFFFF) << 42) | ((q(p.y) & 0x1FFFFF) << 21) |
               (q(p.z) & 0x1FFFFF);
    }

    static int64_t cellKeyFromCoords(int64_t x, int64_t y, int64_t z)
    {
        return ((x & 0x1FFFFF) << 42) | ((y & 0x1FFFFF) << 21) | (z & 0x1FFFFF);
    }

    void gridInsert(uint32_t index)
    {
        grid[decals[index].cell].push_back(index);
    }

    void gridRemove(uint32_t index)
    {
        auto it = grid.find(decals[index].cell);
        if (it == grid.end())
            return;
        auto& bucket = it->second;
        bucket.erase(std::remove(bucket.begin(), bucket.end(), index),
                     bucket.end());
        if (bucket.empty())
            grid.erase(it);
    }

    void gridRename(uint32_t from, uint32_t to)
    {
        auto it = grid.find(decals[to].cell);
        if (it == grid.end())
            return;
        for (uint32_t& v : it->second)
            if (v == from)
                v = to;
    }

    // Swap-remove, keeping the grid consistent. Order in `decals` carries no
    // meaning: the LRU key is the spawn sequence, not the slot.
    void removeAt(uint32_t index)
    {
        gridRemove(index);
        const uint32_t last = static_cast<uint32_t>(decals.size() - 1);
        if (index != last) {
            decals[index] = decals[last];
            gridRename(last, index);
        }
        decals.pop_back();
    }

    // Eviction is a linear scan for the smallest spawn sequence. At the budgets
    // this system is for (hundreds) that is cheaper and far simpler than an
    // intrusive ordered list, and it only runs on a spawn that is already over
    // budget.
    void evictOldest()
    {
        if (decals.empty())
            return;
        uint32_t oldest = 0;
        for (uint32_t i = 1; i < decals.size(); ++i)
            if (decals[i].seq < decals[oldest].seq)
                oldest = i;
        removeAt(oldest);
    }

    void enforceBudget()
    {
        while (decals.size() > budget)
            evictOldest();
    }

    // Nearest pool decal of the same profile on the same plane, or -1. Searches
    // the 27 cells around the point, which covers any merge radius up to the
    // cell size; larger radii are clamped rather than widening the search,
    // because a pool that spans metres is a content bug, not a feature.
    int32_t findMergeTarget(uint16_t profileIndex, const glm::vec3& pos,
                            const glm::vec3& normal, float radius) const
    {
        const int64_t bx = static_cast<int64_t>(std::floor(pos.x / kCellSize));
        const int64_t by = static_cast<int64_t>(std::floor(pos.y / kCellSize));
        const int64_t bz = static_cast<int64_t>(std::floor(pos.z / kCellSize));
        const float r2 = radius * radius;
        int32_t best = -1;
        float bestDist = r2;
        for (int64_t dx = -1; dx <= 1; ++dx)
            for (int64_t dy = -1; dy <= 1; ++dy)
                for (int64_t dz = -1; dz <= 1; ++dz) {
                    auto it = grid.find(cellKeyFromCoords(bx + dx, by + dy, bz + dz));
                    if (it == grid.end())
                        continue;
                    for (uint32_t idx : it->second) {
                        const Decal& d = decals[idx];
                        if (d.profile != profileIndex)
                            continue;
                        if (glm::dot(d.normal, normal) < kMergeNormalDot)
                            continue;
                        const glm::vec3 delta = pos - d.pos;
                        if (std::fabs(glm::dot(delta, normal)) > kMergePlaneEpsilon)
                            continue;
                        const float dist = glm::dot(delta, delta);
                        if (dist < bestDist) {
                            bestDist = dist;
                            best = static_cast<int32_t>(idx);
                        }
                    }
                }
        return best;
    }

    DecalBatch* batchFor(const std::string& material)
    {
        if (!sm)
            return nullptr;
        auto it = batches.find(material);
        if (it != batches.end()) {
            it->second->setCapacity(std::max<std::size_t>(budget, 1));
            return it->second.get();
        }
        auto batch = std::make_unique<DecalBatch>(sm, material,
                                                  std::max<std::size_t>(budget, 1));
        DecalBatch* raw = batch.get();
        batches.emplace(material, std::move(batch));
        return raw;
    }

    // Decals persist, but the instance stream is still rebuilt each update: the
    // stream is bounded by the budget, alpha and size change every frame while
    // anything is fading or growing, and one memcpy of a few hundred records is
    // not worth the bookkeeping a partial-update path would need.
    void rebuild(const glm::vec3* cameraWorldPos)
    {
        if (!sm)
            return;
        for (auto& [name, batch] : batches)
            batch->beginFrame();

        for (const Decal& d : decals) {
            const DecalProfileDesc& desc = profiles[d.profile];
            DecalBatch* batch = batchFor(resolveMaterial(desc.texture, desc.blend));
            if (!batch)
                continue;
            DecalInstance inst{};
            // The quad is lifted along the normal here rather than at spawn so
            // that retuning normalOffset in the debug panel moves live decals.
            inst.pos = d.pos + d.normal * desc.normalOffset;
            inst.size = d.size;
            inst.colour = d.colour;
            inst.colour.a *= fadeAlpha(d, desc);
            inst.tangent = glm::vec4(d.tangent, 0.0f);
            inst.normal = glm::vec4(d.normal, 0.0f);
            batch->push(inst);
        }

        for (auto& [name, batch] : batches) {
            if (cameraWorldPos)
                batch->sort(*cameraWorldPos);
            batch->endFrame();
            batch->setVisible(visible);
        }
    }

    static float fadeAlpha(const Decal& d, const DecalProfileDesc& desc)
    {
        if (desc.lifetime <= 0.0f)
            return 1.0f;
        const float fade = std::min(std::max(desc.fadeTime, 0.0f), desc.lifetime);
        if (fade <= 0.0f)
            return 1.0f;
        const float remaining = desc.lifetime - d.age;
        return std::min(std::max(remaining / fade, 0.0f), 1.0f);
    }
};

DecalSystem::DecalSystem() : mImpl(std::make_unique<Impl>()) {}

DecalSystem::~DecalSystem() { shutdown(); }

void DecalSystem::attach(Ogre::SceneManager* sceneManager)
{
    if (mImpl->sm == sceneManager)
        return;
    // Batches belong to one scene manager; moving means rebuilding them, and
    // the decals themselves are world-space data that survives the move.
    mImpl->batches.clear();
    mImpl->sm = sceneManager;
}

void DecalSystem::shutdown()
{
    mImpl->batches.clear();
    mImpl->decals.clear();
    mImpl->grid.clear();
    mImpl->sm = nullptr;
}

void DecalSystem::registerProfile(const std::string& id, const DecalProfileDesc& desc)
{
    auto it = mImpl->profileIds.find(id);
    if (it != mImpl->profileIds.end()) {
        mImpl->profiles[it->second] = desc;
        return;
    }
    const uint16_t index = static_cast<uint16_t>(mImpl->profiles.size());
    mImpl->profiles.push_back(desc);
    mImpl->profileIds.emplace(id, index);
}

const DecalProfileDesc* DecalSystem::profile(const std::string& id) const
{
    auto it = mImpl->profileIds.find(id);
    return it == mImpl->profileIds.end() ? nullptr : &mImpl->profiles[it->second];
}

bool DecalSystem::spawn(const DecalRequest& request)
{
    auto idIt = mImpl->profileIds.find(request.profile);
    if (idIt == mImpl->profileIds.end())
        return false;
    const uint16_t profileIndex = idIt->second;
    const DecalProfileDesc& desc = mImpl->profiles[profileIndex];

    const float normalLen = glm::length(request.normal);
    if (!(normalLen > 1e-4f))
        return false;   // a degenerate hit normal has no plane to lie on
    const glm::vec3 normal = request.normal / normalLen;

    const float lo = std::min(desc.sizeMin, desc.sizeMax);
    const float hi = std::max(desc.sizeMin, desc.sizeMax);
    const float size = lo + (hi - lo) * mImpl->rand01();

    // Pools absorb nearby hits instead of adding to the count. This is the only
    // thing that keeps sustained fire on one spot inside the budget.
    if (desc.pool && desc.mergeRadius > 0.0f) {
        const float radius = std::min(desc.mergeRadius, kCellSize);
        const int32_t target =
            mImpl->findMergeTarget(profileIndex, request.position, normal, radius);
        if (target >= 0) {
            Impl::Decal& d = mImpl->decals[static_cast<uint32_t>(target)];
            // Half the incoming size, because a pool absorbing a splash spreads
            // by area rather than by diameter; the ceiling is the profile's.
            d.targetSize = std::min(d.targetSize + size * 0.5f, desc.maxSize);
            // A fed pool is fresh again, otherwise a puddle under continuous
            // bleeding would fade out while it is still being fed.
            d.age = 0.0f;
            d.seq = mImpl->nextSeq++;
            return true;
        }
    }

    Impl::Decal d;
    d.profile = profileIndex;
    d.pos = request.position;
    d.normal = normal;
    d.tangent = planeTangent(
        normal, desc.randomRotation
                    ? mImpl->rand01() * glm::two_pi<float>()
                    : 0.0f);
    d.colour = desc.colour;
    d.colour.r = std::clamp(d.colour.r + desc.colourJitter.r * mImpl->randSym(), 0.0f, 1.0f);
    d.colour.g = std::clamp(d.colour.g + desc.colourJitter.g * mImpl->randSym(), 0.0f, 1.0f);
    d.colour.b = std::clamp(d.colour.b + desc.colourJitter.b * mImpl->randSym(), 0.0f, 1.0f);
    d.colour.a = std::clamp(d.colour.a + desc.alphaJitter * mImpl->randSym(), 0.0f, 1.0f);
    d.size = size;
    // A pool starts small and spreads to its ceiling; everything else is born
    // at its final size and never changes shape.
    d.targetSize = desc.pool ? std::max(size, desc.maxSize) : size;
    d.age = 0.0f;
    d.seq = mImpl->nextSeq++;
    d.cell = Impl::cellKey(d.pos);

    mImpl->decals.push_back(d);
    mImpl->gridInsert(static_cast<uint32_t>(mImpl->decals.size() - 1));
    mImpl->enforceBudget();
    return true;
}

void DecalSystem::update(float dt)
{
    update(dt, glm::vec3(0.0f));
    // The no-camera overload still needs a sorted-enough result, so it reuses
    // the sorting path with the origin; coplanar decals are order-independent
    // anyway, which is the common case.
}

void DecalSystem::update(float dt, const glm::vec3& cameraWorldPos)
{
    if (dt < 0.0f)
        dt = 0.0f;

    for (uint32_t i = 0; i < mImpl->decals.size();) {
        Impl::Decal& d = mImpl->decals[i];
        const DecalProfileDesc& desc = mImpl->profiles[d.profile];
        d.age += dt;
        if (desc.lifetime > 0.0f && d.age >= desc.lifetime) {
            mImpl->removeAt(i);
            continue;   // slot now holds the decal swapped in from the end
        }
        if (desc.pool && d.size < d.targetSize) {
            d.size = desc.growthRate > 0.0f
                         ? std::min(d.size + desc.growthRate * dt, d.targetSize)
                         : d.targetSize;
        }
        ++i;
    }

    mImpl->enforceBudget();
    mImpl->rebuild(&cameraWorldPos);
}

void DecalSystem::clear()
{
    mImpl->decals.clear();
    mImpl->grid.clear();
    // Destroy the batches rather than just emptying them. The only caller is
    // Renderer::clearScene, which destroys the whole scene graph on the very
    // next line: a batch left alive here keeps a pointer to a SceneNode that is
    // about to be freed, and Ogre does not notify an attached MovableObject
    // when its node goes away. The dangling pointer then survives until
    // shutdown, where destroy() detaches from and re-destroys freed memory.
    // Particles::clear() destroys its batches for exactly this reason.
    for (auto& [name, batch] : mImpl->batches)
        batch->destroy();
    mImpl->batches.clear();
}

void DecalSystem::setBudget(std::size_t maxDecals)
{
    mImpl->budget = std::max<std::size_t>(maxDecals, 1);
    mImpl->enforceBudget();
}

std::size_t DecalSystem::budget() const { return mImpl->budget; }

std::size_t DecalSystem::count() const { return mImpl->decals.size(); }

void DecalSystem::setVisible(bool visible)
{
    mImpl->visible = visible;
    for (auto& [name, batch] : mImpl->batches)
        batch->setVisible(visible);
}

} // namespace eng
