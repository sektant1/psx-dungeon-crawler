#include "ParticleBatch.h"

#include <eng/Log.h>

#include <OgreAxisAlignedBox.h>
#include <OgreHardwareBufferManager.h>
#include <OgreMaterialManager.h>
#include <OgreRoot.h>
#include <OgreSceneManager.h>
#include <OgreSceneNode.h>
#include <OgreCamera.h>
#include <OgreSimpleRenderable.h>

#include <algorithm>
#include <cstddef>
#include <cmath>
#include <cstring>
#include <vector>

namespace eng {
namespace {

// Vertex buffer binding sources. Source 0 is the shared base mesh (one quad or
// one cube); source 1 is the per-instance stream, which carries a divisor of 1
// so every vertex of the base mesh sees the same instance record.
constexpr unsigned short kBaseSource = 0;
constexpr unsigned short kInstanceSource = 1;

// Instance attributes ride on texture coordinate sets 1..3 because that is the
// only semantic family GL3Plus maps to a free, indexable attribute slot
// (VES_TEXTURE_COORDINATES -> attribute 8 + index). They appear in the shaders
// as uv1/uv2/uv3.
constexpr unsigned short kInstPosSizeUv = 1;
constexpr unsigned short kInstColourUv = 2;
constexpr unsigned short kInstRotFrameUv = 3;

// A single quad centred on the origin in the XY plane, half-extent 0.5. The
// vertex shader rotates and billboards it, so this never changes.
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

// A unit cube with split vertices so each face carries its own normal: the
// fragment shader picks one of six fixed tones from that normal, which is what
// gives the voxel batch its flat PSX-era shading for free.
void buildCube(std::vector<float>& verts, std::vector<uint16_t>& indices)
{
    const float n[6][3] = {{0, 0, 1},  {0, 0, -1}, {1, 0, 0},
                           {-1, 0, 0}, {0, 1, 0},  {0, -1, 0}};
    // Face basis: two in-plane axes per normal, chosen so the winding stays
    // counter-clockwise when viewed from outside.
    const float u[6][3] = {{1, 0, 0},  {-1, 0, 0}, {0, 0, -1},
                           {0, 0, 1},  {1, 0, 0},  {1, 0, 0}};
    const float v[6][3] = {{0, 1, 0},  {0, 1, 0},  {0, 1, 0},
                           {0, 1, 0},  {0, 0, -1}, {0, 0, 1}};
    const float corner[4][2] = {{-0.5f, -0.5f}, {0.5f, -0.5f},
                                {0.5f, 0.5f},   {-0.5f, 0.5f}};
    for (int f = 0; f < 6; ++f) {
        for (int c = 0; c < 4; ++c) {
            for (int axis = 0; axis < 3; ++axis)
                verts.push_back(n[f][axis] * 0.5f + u[f][axis] * corner[c][0] +
                                v[f][axis] * corner[c][1]);
            for (int axis = 0; axis < 3; ++axis)
                verts.push_back(n[f][axis]);
        }
        const uint16_t base = static_cast<uint16_t>(f * 4);
        const uint16_t idx[6] = {base,           uint16_t(base + 1), uint16_t(base + 2),
                                 base,           uint16_t(base + 2), uint16_t(base + 3)};
        indices.insert(indices.end(), idx, idx + 6);
    }
}

class ParticleBatch final : public IParticleBatch, public Ogre::SimpleRenderable {
public:
    ParticleBatch(Ogre::SceneManager* sm, const ParticleBatchDesc& desc)
        : Ogre::SimpleRenderable(), mSm(sm), mDesc(desc)
    {
        buildBaseMesh();
        setCapacity(std::max<std::size_t>(desc.capacity, 1));
        applyMaterial(desc.material);
        setRenderQueueGroup(desc.renderQueue);
        // Empty until the first endFrame(); showing an unfilled buffer would
        // draw whatever the driver left in it.
        Ogre::MovableObject::setVisible(false);
        mNode = mSm->getRootSceneNode()->createChildSceneNode();
        mNode->attachObject(this);
    }

    ~ParticleBatch() override { destroy(); }

    void setCapacity(std::size_t maxInstances) override
    {
        maxInstances = std::max<std::size_t>(maxInstances, 1);
        if (maxInstances == mCapacity && mInstanceBuffer)
            return;
        mCapacity = maxInstances;
        mStaging.reserve(mCapacity);
        auto& hbm = Ogre::HardwareBufferManager::getSingleton();
        mInstanceBuffer = hbm.createVertexBuffer(sizeof(ParticleInstance), mCapacity,
                                                 Ogre::HBU_CPU_TO_GPU);
        // The divisor is what turns the stream into instance data; without it
        // GL walks it per vertex and every quad reads the same particle.
        mInstanceBuffer->setIsInstanceData(true);
        mInstanceBuffer->setInstanceDataStepRate(1);
        mRenderOp.vertexData->vertexBufferBinding->setBinding(kInstanceSource,
                                                              mInstanceBuffer);
    }

    std::size_t capacity() const override { return mCapacity; }

    void beginFrame() override { mStaging.clear(); }

    void push(const ParticleInstance& instance) override
    {
        if (mStaging.size() < mCapacity)
            mStaging.push_back(instance);
    }

    void push(const ParticleInstance* instances, std::size_t count) override
    {
        if (!instances || count == 0)
            return;
        count = std::min(count, mCapacity - mStaging.size());
        mStaging.insert(mStaging.end(), instances, instances + count);
    }

    bool needsSorting() const override
    {
        return mDesc.blend == ParticleBlend::Alpha;
    }

    void sort(const glm::vec3& cameraWorldPos) override
    {
        if (!needsSorting())
            return;
        // Farthest first: alpha blending is order-dependent and depth writes
        // are off, so the far particles must lay down their colour first.
        std::sort(mStaging.begin(), mStaging.end(),
                  [&](const ParticleInstance& a, const ParticleInstance& b) {
                      const glm::vec3 da = a.pos - cameraWorldPos;
                      const glm::vec3 db = b.pos - cameraWorldPos;
                      return glm::dot(da, da) > glm::dot(db, db);
                  });
    }

    void endFrame() override
    {
        const std::size_t count = mStaging.size();
        mRenderOp.numberOfInstances = static_cast<uint32_t>(count);
        if (count == 0) {
            Ogre::MovableObject::setVisible(false);
            return;
        }

        void* dst = mInstanceBuffer->lock(0, count * sizeof(ParticleInstance),
                                          Ogre::HardwareBuffer::HBL_DISCARD);
        std::memcpy(dst, mStaging.data(), count * sizeof(ParticleInstance));
        mInstanceBuffer->unlock();

        // The bound has to cover the billboarded quad, not just the centre, or
        // particles pop out at the edge of the frustum. Half the size is the
        // quad's half-extent; the diagonal is covered by the extra half.
        Ogre::AxisAlignedBox box;
        for (const ParticleInstance& p : mStaging) {
            const float r = std::max(p.size, 0.0f);
            box.merge(Ogre::Vector3(p.pos.x - r, p.pos.y - r, p.pos.z - r));
            box.merge(Ogre::Vector3(p.pos.x + r, p.pos.y + r, p.pos.z + r));
        }
        setBoundingBox(box);
        // The radius has to match the box, and it has to be measured from the
        // NODE, not from the box's own centre: Ogre builds the world bounding
        // sphere as (node derived position, getBoundingRadius()). The batch's
        // node sits at the origin and never moves -- instance positions are
        // world-space -- so a fixed 0.71 described a sphere around the world
        // origin. Every batch whose particles were more than 0.71 m from the
        // origin was frustum-culled outright, which is why nothing rendered no
        // matter how correct the instance data was.
        const Ogre::Vector3 lo = box.getMinimum();
        const Ogre::Vector3 hi = box.getMaximum();
        mBoundRadius = std::sqrt(std::max(lo.squaredLength(), hi.squaredLength()));

        // Tell the node its bounds moved. Ogre caches a movable's world AABB
        // and only recomputes it when the parent node is marked dirty -- and
        // this node never moves, because instance positions are world-space.
        // So the cached box stayed at whatever it was when the batch was
        // created (empty), every batch failed the frustum test forever, and
        // _updateRenderQueue was never called on any of them. That is the
        // whole reason particles stopped appearing: the data, the shader and
        // the material were all fine and nothing was ever asked to draw.
        if (mNode)
            mNode->needUpdate();

        Ogre::MovableObject::setVisible(mWantVisible);
    }

    std::size_t count() const override { return mStaging.size(); }


    void setVisible(bool visible) override
    {
        mWantVisible = visible;
        Ogre::MovableObject::setVisible(visible && !mStaging.empty());
    }

    void destroy() override
    {
        if (mNode) {
            mNode->detachObject(this);
            if (mSm)
                mSm->destroySceneNode(mNode);
            mNode = nullptr;
        }
    }

    // SimpleRenderable stores an identity transform and we never move the node,
    // so instance positions are world-space and the auto-bound worldViewProj
    // reduces to viewProj. The vertex shaders rely on that.
    Ogre::Real getBoundingRadius() const override { return mBoundRadius; }

    // Ogre uses this to order transparent renderables against each other. The
    // batch is one renderable covering the whole cloud, so the centre of its
    // bounds is the only meaningful answer; ordering *within* the batch is
    // handled by sort().
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
        const bool voxel = mDesc.mode == ParticleRenderMode::Voxel;
        if (voxel)
            buildCube(verts, indices);
        else
            buildQuad(verts, indices);

        auto& hbm = Ogre::HardwareBufferManager::getSingleton();
        mRenderOp.vertexData = new Ogre::VertexData();
        mRenderOp.indexData = new Ogre::IndexData();
        mRenderOp.operationType = Ogre::RenderOperation::OT_TRIANGLE_LIST;
        mRenderOp.useIndexes = true;
        // Ogre's global instance buffer is a different mechanism; ours is bound
        // explicitly and must not be combined with it.
        mRenderOp.useGlobalInstancing = false;

        Ogre::VertexDeclaration* decl = mRenderOp.vertexData->vertexDeclaration;
        std::size_t offset = 0;
        offset += decl->addElement(kBaseSource, offset, Ogre::VET_FLOAT3,
                                   Ogre::VES_POSITION)
                      .getSize();
        if (voxel)
            offset += decl->addElement(kBaseSource, offset, Ogre::VET_FLOAT3,
                                       Ogre::VES_NORMAL)
                          .getSize();
        else
            offset += decl->addElement(kBaseSource, offset, Ogre::VET_FLOAT2,
                                       Ogre::VES_TEXTURE_COORDINATES, 0)
                          .getSize();
        const std::size_t baseStride = offset;

        decl->addElement(kInstanceSource, offsetof(ParticleInstance, pos),
                         Ogre::VET_FLOAT4, Ogre::VES_TEXTURE_COORDINATES,
                         kInstPosSizeUv);
        decl->addElement(kInstanceSource, offsetof(ParticleInstance, colour),
                         Ogre::VET_FLOAT4, Ogre::VES_TEXTURE_COORDINATES,
                         kInstColourUv);
        decl->addElement(kInstanceSource, offsetof(ParticleInstance, rot),
                         Ogre::VET_FLOAT2, Ogre::VES_TEXTURE_COORDINATES,
                         kInstRotFrameUv);

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

        // A quad's corners reach 0.5 * size from the centre; a cube's reach the
        // half-diagonal. Both are folded into the per-frame AABB, but the
        // radius is what the scene manager culls against first.
        // Only until the first endFrame(), which measures the real extent from
        // the filled box. A base-mesh radius alone is meaningless here: the
        // mesh is a unit quad shared by every instance.
        mBoundRadius = voxel ? 0.87f : 0.71f;
    }

    void applyMaterial(const std::string& name)
    {
        Ogre::MaterialPtr mat =
            Ogre::MaterialManager::getSingleton().getByName(name);
        if (!mat) {
            // A missing particle material is almost always a GLSL compile
            // error reported earlier as "has no supportable Techniques", so say
            // which one rather than letting the batch render as white.
            log::error("particle batch material '%s' not found", name.c_str());
            mat = Ogre::MaterialManager::getSingleton().getByName("BaseWhite");
        }
        if (mat) {
            mat->load();
            setMaterial(mat);
        }
    }

    Ogre::SceneManager* mSm = nullptr;
    Ogre::SceneNode* mNode = nullptr;
    ParticleBatchDesc mDesc;
    Ogre::HardwareVertexBufferSharedPtr mInstanceBuffer;
    std::vector<ParticleInstance> mStaging;
    std::size_t mCapacity = 0;
    float mBoundRadius = 1.0f;
    bool mWantVisible = true;
};

} // namespace

std::unique_ptr<IParticleBatch> createParticleBatch(Ogre::SceneManager* sceneManager,
                                                    const ParticleBatchDesc& desc)
{
    if (!sceneManager)
        return nullptr;
    return std::unique_ptr<IParticleBatch>(new ParticleBatch(sceneManager, desc));
}

} // namespace eng
