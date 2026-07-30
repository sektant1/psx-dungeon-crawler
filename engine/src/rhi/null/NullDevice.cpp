#include "NullDevice.h"

#include <eng/Log.h>

#include <unordered_map>

namespace eng::rhi::null {

namespace {

// Generational handle table. The point of the null backend is to be strict:
// it accepts exactly what a real backend must accept and complains about
// everything else, so a bug is found here rather than as a black screen.
template <typename Handle, typename Record>
class Table {
public:
    Handle create(Record rec)
    {
        uint32_t id;
        if (!mFree.empty()) {
            id = mFree.back();
            mFree.pop_back();
        } else {
            mSlots.push_back({});
            id = uint32_t(mSlots.size()); // ids are 1-based; 0 is the null handle
        }
        Slot& slot = mSlots[id - 1];
        slot.alive = true;
        slot.record = std::move(rec);
        return Handle{id, slot.gen};
    }

    Record* get(Handle h, const char* what)
    {
        if (!h.valid() || h.id > mSlots.size()) {
            log::error("rhi(null): %s handle %u is not from this device", what, h.id);
            return nullptr;
        }
        Slot& slot = mSlots[h.id - 1];
        if (!slot.alive || slot.gen != h.gen) {
            log::error("rhi(null): %s handle %u is stale (generation %u, now %u)",
                       what, h.id, h.gen, slot.gen);
            return nullptr;
        }
        return &slot.record;
    }

    void destroy(Handle h, const char* what)
    {
        if (!get(h, what))
            return;
        Slot& slot = mSlots[h.id - 1];
        slot.alive = false;
        ++slot.gen; // every stale handle is now detectable
        slot.record = {};
        mFree.push_back(h.id);
    }

    size_t liveCount() const
    {
        size_t n = 0;
        for (const Slot& s : mSlots) n += s.alive ? 1 : 0;
        return n;
    }

private:
    struct Slot {
        Record record{};
        uint32_t gen = 1;
        bool alive = false;
    };
    std::vector<Slot> mSlots;
    std::vector<uint32_t> mFree;
};

struct BufferRec { uint64_t size = 0; BufferUsage usage = BufferUsage::Vertex; };
struct TextureRec { uint32_t width = 0, height = 0; Format format = Format::Unknown; };
struct SamplerRec {};
struct ShaderRec { ShaderStage stage = ShaderStage::Vertex; };
struct PipelineRec {};

class NullCommandList final : public CommandList {
public:
    void bindPipeline(PipelineHandle) override {}
    void setViewport(const Viewport&) override {}
    void setScissor(const Rect&) override {}
    void bindVertexBuffer(uint32_t, BufferHandle, uint64_t) override {}
    void bindIndexBuffer(BufferHandle, uint64_t) override {}
    void bindUniformBuffer(uint32_t, BufferHandle, uint64_t, uint64_t) override {}
    void bindTexture(uint32_t, TextureHandle, SamplerHandle) override {}
    void pushConstants(const void*, uint32_t size) override
    {
        // The documented budget. A backend with real push constants would
        // reject this too, just later and less clearly.
        if (size > 128)
            log::error("rhi(null): pushConstants of %u bytes exceeds the "
                       "128-byte budget the contract documents", size);
    }
    void draw(uint32_t, uint32_t, uint32_t, uint32_t) override { ++draws; }
    void drawIndexed(uint32_t, uint32_t, uint32_t, int32_t, uint32_t) override { ++draws; }
    void pushDebugGroup(const char*) override { ++debugDepth; }
    void popDebugGroup() override
    {
        if (debugDepth == 0) {
            log::error("rhi(null): popDebugGroup without a matching push");
            return;
        }
        --debugDepth;
    }

    uint32_t draws = 0;
    uint32_t debugDepth = 0;
};

class NullDevice final : public Device {
public:
    explicit NullDevice(const DeviceDesc& desc) : mDesc(desc)
    {
        mCaps.deviceName = "null";
        mCaps.backendName = "null";
        mCaps.maxTextureSize = 16384;
        mCaps.maxColourAttachments = 8;
        mCaps.maxSimultaneousLights = 16; // what psx_lighting.glsl binds
        mCaps.supportsCompute = true;
        mCaps.supportsAnisotropicFiltering = true;
        mCaps.supportsSrgbFramebuffer = true;
    }

    ~NullDevice() override
    {
        const size_t leaked = mBuffers.liveCount() + mTextures.liveCount() +
                              mSamplers.liveCount() + mShaders.liveCount() +
                              mPipelines.liveCount();
        if (leaked)
            log::error("rhi(null): %zu resources still alive at teardown", leaked);
    }

    const DeviceCapabilities& capabilities() const override { return mCaps; }

    bool beginFrame() override
    {
        if (mInFrame) {
            log::error("rhi(null): beginFrame while a frame is already open");
            return false;
        }
        mInFrame = true;
        return true;
    }

    void endFrame() override
    {
        if (!mInFrame) {
            log::error("rhi(null): endFrame outside a frame");
            return;
        }
        if (mInPass)
            log::error("rhi(null): endFrame with pass '%s' still open",
                       mPassName.c_str());
        mInFrame = false;
    }

    void resizeSwapchain(uint32_t width, uint32_t height) override
    {
        mDesc.width = width;
        mDesc.height = height;
    }

    Format swapchainFormat() const override { return Format::BGRA8Unorm; }

    CommandList& beginPass(const RenderPassDesc& desc) override
    {
        if (!mInFrame)
            log::error("rhi(null): beginPass outside a frame");
        if (mInPass)
            log::error("rhi(null): beginPass '%s' while '%s' is open",
                       desc.debugName.c_str(), mPassName.c_str());
        if (desc.colour.size() > mCaps.maxColourAttachments)
            log::error("rhi(null): pass '%s' wants %zu colour attachments, "
                       "the device supports %u",
                       desc.debugName.c_str(), desc.colour.size(),
                       mCaps.maxColourAttachments);
        mInPass = true;
        mPassName = desc.debugName;
        mList = NullCommandList{};
        return mList;
    }

    void endPass() override
    {
        if (!mInPass) {
            log::error("rhi(null): endPass without a pass");
            return;
        }
        if (mList.debugDepth != 0)
            log::error("rhi(null): pass '%s' ended with %u debug groups open",
                       mPassName.c_str(), mList.debugDepth);
        mInPass = false;
    }

    BufferHandle createBuffer(const BufferDesc& d) override
    {
        if (d.size == 0)
            log::error("rhi(null): zero-sized buffer '%s'", d.debugName.c_str());
        return mBuffers.create({d.size, d.usage});
    }
    void destroyBuffer(BufferHandle h) override { mBuffers.destroy(h, "buffer"); }
    void updateBuffer(BufferHandle h, const void*, uint64_t size, uint64_t offset) override
    {
        if (const BufferRec* rec = mBuffers.get(h, "buffer");
            rec && offset + size > rec->size)
            log::error("rhi(null): updateBuffer writes %llu bytes at %llu, "
                       "past the buffer's %llu",
                       (unsigned long long)size, (unsigned long long)offset,
                       (unsigned long long)rec->size);
    }

    TextureHandle createTexture(const TextureDesc& d) override
    {
        if (d.width > mCaps.maxTextureSize || d.height > mCaps.maxTextureSize)
            log::error("rhi(null): texture '%s' is %ux%u, past the %u limit",
                       d.debugName.c_str(), d.width, d.height, mCaps.maxTextureSize);
        return mTextures.create({d.width, d.height, d.format});
    }
    void destroyTexture(TextureHandle h) override { mTextures.destroy(h, "texture"); }
    void updateTexture(TextureHandle h, const void*, uint64_t, uint32_t) override
    {
        mTextures.get(h, "texture");
    }

    SamplerHandle createSampler(const SamplerDesc&) override { return mSamplers.create({}); }
    void destroySampler(SamplerHandle h) override { mSamplers.destroy(h, "sampler"); }

    ShaderHandle createShader(const ShaderDesc& d) override
    {
        if (d.code.empty())
            log::error("rhi(null): shader '%s' has no code", d.debugName.c_str());
        return mShaders.create({d.stage});
    }
    void destroyShader(ShaderHandle h) override { mShaders.destroy(h, "shader"); }

    PipelineHandle createPipeline(const PipelineDesc& d) override
    {
        const ShaderRec* vs = mShaders.get(d.vertex, "vertex shader");
        const ShaderRec* fs = mShaders.get(d.fragment, "fragment shader");
        if (vs && vs->stage != ShaderStage::Vertex)
            log::error("rhi(null): pipeline '%s' binds a non-vertex shader as "
                       "its vertex stage", d.debugName.c_str());
        if (fs && fs->stage != ShaderStage::Fragment)
            log::error("rhi(null): pipeline '%s' binds a non-fragment shader as "
                       "its fragment stage", d.debugName.c_str());
        if (d.vertexLayout.attributes.empty())
            log::error("rhi(null): pipeline '%s' has no vertex attributes",
                       d.debugName.c_str());
        return mPipelines.create({});
    }
    void destroyPipeline(PipelineHandle h) override { mPipelines.destroy(h, "pipeline"); }

    void waitIdle() override {}

private:
    DeviceDesc mDesc;
    DeviceCapabilities mCaps;
    bool mInFrame = false;
    bool mInPass = false;
    std::string mPassName;
    NullCommandList mList;

    Table<BufferHandle, BufferRec> mBuffers;
    Table<TextureHandle, TextureRec> mTextures;
    Table<SamplerHandle, SamplerRec> mSamplers;
    Table<ShaderHandle, ShaderRec> mShaders;
    Table<PipelineHandle, PipelineRec> mPipelines;
};

} // namespace

std::unique_ptr<Device> createDevice(const DeviceDesc& desc)
{
    return std::make_unique<NullDevice>(desc);
}

} // namespace eng::rhi::null
