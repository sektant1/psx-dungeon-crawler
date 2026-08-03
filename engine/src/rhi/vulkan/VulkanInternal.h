#pragma once

#include <eng/Log.h>
#include <eng/rhi/Device.h>

#include <VkBootstrap.h>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace eng::rhi::vulkan {

constexpr uint32_t kFramesInFlight = 2;
constexpr uint32_t kUniformBindingCount = 8;
constexpr uint32_t kTextureBindingCount = 16;
constexpr uint32_t kPushConstantBytes = 128;
constexpr uint32_t kDescriptorSetsPerType = 4096;

template <typename Handle>
uint64_t vkHandleValue(Handle handle)
{
    if constexpr (std::is_pointer_v<Handle>)
        return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(handle));
    else
        return static_cast<uint64_t>(handle);
}

template <typename Handle, typename Record>
class HandleTable {
public:
    Handle create(Record record)
    {
        uint32_t id = 0;
        if (mFree.empty()) {
            mSlots.push_back({});
            id = static_cast<uint32_t>(mSlots.size());
        }
        else {
            id = mFree.back();
            mFree.pop_back();
        }
        Slot& slot = mSlots[id - 1];
        slot.record = std::move(record);
        slot.alive = true;
        return Handle{id, slot.generation};
    }

    Record* get(Handle handle, const char* kind)
    {
        if (!handle.valid() || handle.id > mSlots.size()) {
            reportInvalid(kind, handle.id, handle.gen, 0, false);
            return nullptr;
        }
        Slot& slot = mSlots[handle.id - 1];
        if (!slot.alive || slot.generation != handle.gen) {
            reportInvalid(kind, handle.id, handle.gen, slot.generation, true);
            return nullptr;
        }
        return &slot.record;
    }

    const Record* get(Handle handle, const char* kind) const
    {
        return const_cast<HandleTable*>(this)->get(handle, kind);
    }

    std::optional<Record> remove(Handle handle, const char* kind)
    {
        Record* record = get(handle, kind);
        if (!record)
            return std::nullopt;
        Slot& slot = mSlots[handle.id - 1];
        std::optional<Record> result(std::move(slot.record));
        slot.record = {};
        slot.alive = false;
        ++slot.generation;
        mFree.push_back(handle.id);
        return result;
    }

    template <typename Fn>
    size_t destroyAll(Fn&& fn)
    {
        size_t count = 0;
        for (Slot& slot : mSlots) {
            if (!slot.alive)
                continue;
            fn(slot.record);
            slot.record = {};
            slot.alive = false;
            ++slot.generation;
            ++count;
        }
        mFree.clear();
        for (uint32_t i = 0; i < mSlots.size(); ++i)
            mFree.push_back(i + 1);
        return count;
    }

    size_t liveCount() const
    {
        size_t count = 0;
        for (const Slot& slot : mSlots)
            count += slot.alive ? 1u : 0u;
        return count;
    }

private:
    static void reportInvalid(const char* kind, uint32_t id, uint32_t given,
                              uint32_t current, bool stale)
    {
        if (stale) {
            log::error(
                "rhi(vulkan): %s handle %u is stale (generation %u, now %u)",
                kind, id, given, current);
        }
        else {
            log::error("rhi(vulkan): %s handle %u is not from this device",
                       kind, id);
        }
    }

    struct Slot {
        Record record{};
        uint32_t generation = 1;
        bool alive = false;
    };
    std::vector<Slot> mSlots;
    std::vector<uint32_t> mFree;
};

struct BufferBacking {
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    void* mapped = nullptr;
};

struct BufferRecord {
    uint64_t size = 0;
    BufferUsage usage = BufferUsage::Vertex;
    bool dynamic = false;
    std::array<BufferBacking, kFramesInFlight> backing{};
    std::vector<std::byte> shadow;
    uint64_t version = 0;
    std::array<uint64_t, kFramesInFlight> frameVersion{};
};

struct TextureRecord {
    VkImage image = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t depth = 1;
    uint32_t mipLevels = 1;
    Format format = Format::Unknown;
    TextureUsage usage = TextureUsage::Sampled;
    VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
};

struct SamplerRecord {
    VkSampler sampler = VK_NULL_HANDLE;
};

struct ShaderRecord {
    VkShaderModule module = VK_NULL_HANDLE;
    ShaderStage stage = ShaderStage::Vertex;
    std::string entryPoint;
};

struct PipelineRecord {
    VkPipeline pipeline = VK_NULL_HANDLE;
    std::vector<VkFormat> colourFormats;
    VkFormat depthFormat = VK_FORMAT_UNDEFINED;
};

struct FrameState {
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    VkSemaphore imageAvailable = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    uint64_t submittedSerial = 0;
};

struct SwapchainState {
    vkb::Swapchain handle{};
    std::vector<VkImage> images;
    std::vector<VkImageView> views;
    std::vector<VkSemaphore> renderFinished;
    std::vector<VkImageLayout> layouts;
    Format rhiFormat = Format::Unknown;
};

struct VulkanContext {
    vkb::Instance instance{};
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    vkb::PhysicalDevice physical{};
    vkb::Device device{};
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    VkQueue presentQueue = VK_NULL_HANDLE;
    uint32_t graphicsFamily = 0;
    uint32_t presentFamily = 0;
    VmaAllocator allocator = VK_NULL_HANDLE;
    VkCommandPool uploadPool = VK_NULL_HANDLE;
    VkCommandBuffer uploadCommandBuffer = VK_NULL_HANDLE;
    VkFence uploadFence = VK_NULL_HANDLE;
    PFN_vkSetDebugUtilsObjectNameEXT setObjectName = nullptr;
    PFN_vkCmdBeginDebugUtilsLabelEXT beginLabel = nullptr;
    PFN_vkCmdEndDebugUtilsLabelEXT endLabel = nullptr;
    bool samplerAnisotropy = false;
    bool fillModeNonSolid = false;
};

struct DeferredDelete {
    uint64_t retireAfter = 0;
    std::function<void()> destroy;
};

class VulkanDevice;

class VulkanCommandList final : public CommandList {
public:
    explicit VulkanCommandList(VulkanDevice& device) : mDevice(device) {}

    void reset(VkCommandBuffer commandBuffer, VkExtent2D extent,
               std::vector<VkFormat> colourFormats, VkFormat depthFormat);

    void bindPipeline(PipelineHandle) override;
    void setViewport(const Viewport&) override;
    void setScissor(const Rect&) override;
    void bindVertexBuffer(uint32_t binding, BufferHandle,
                          uint64_t offset) override;
    void bindIndexBuffer(BufferHandle, uint64_t offset,
                         IndexType type) override;
    void bindUniformBuffer(uint32_t slot, BufferHandle, uint64_t offset,
                           uint64_t size) override;
    void bindTexture(uint32_t slot, TextureHandle, SamplerHandle) override;
    void pushConstants(const void* data, uint32_t size) override;
    void draw(uint32_t vertexCount, uint32_t instanceCount,
              uint32_t firstVertex, uint32_t firstInstance) override;
    void drawIndexed(uint32_t indexCount, uint32_t instanceCount,
                     uint32_t firstIndex, int32_t vertexOffset,
                     uint32_t firstInstance) override;
    void pushDebugGroup(const char* name) override;
    void popDebugGroup() override;

    uint32_t debugDepth() const { return mDebugDepth; }

private:
    struct UniformBinding {
        BufferHandle handle{};
        uint64_t offset = 0;
        uint64_t size = 0;
    };
    struct TextureBinding {
        TextureHandle texture{};
        SamplerHandle sampler{};
    };

    bool prepareDraw();
    bool flushDescriptors();

    VulkanDevice& mDevice;
    VkCommandBuffer mCommandBuffer = VK_NULL_HANDLE;
    VkExtent2D mExtent{};
    std::vector<VkFormat> mColourFormats;
    VkFormat mDepthFormat = VK_FORMAT_UNDEFINED;
    PipelineHandle mPipeline{};
    std::array<UniformBinding, kUniformBindingCount> mUniforms{};
    std::array<TextureBinding, kTextureBindingCount> mTextures{};
    VkDescriptorSet mUniformSet = VK_NULL_HANDLE;
    VkDescriptorSet mTextureSet = VK_NULL_HANDLE;
    bool mUniformsDirty = false;
    bool mTexturesDirty = false;
    bool mUniformsUsed = false;
    bool mTexturesUsed = false;
    bool mIndexBound = false;
    uint32_t mDebugDepth = 0;
};

class VulkanDevice final : public Device {
public:
    explicit VulkanDevice(const DeviceDesc& desc);
    ~VulkanDevice() override;

    bool initialize();

    const DeviceCapabilities& capabilities() const override { return mCaps; }
    bool beginFrame() override;
    void endFrame() override;
    void resizeSwapchain(uint32_t width, uint32_t height) override;
    Format swapchainFormat() const override { return mSwapchain.rhiFormat; }
    CommandList& beginPass(const RenderPassDesc&) override;
    void endPass() override;

    BufferHandle createBuffer(const BufferDesc&) override;
    void destroyBuffer(BufferHandle) override;
    void updateBuffer(BufferHandle, const void* data, uint64_t size,
                      uint64_t offset) override;
    TextureHandle createTexture(const TextureDesc&) override;
    void destroyTexture(TextureHandle) override;
    void updateTexture(TextureHandle, const void* data, uint64_t size,
                       uint32_t mipLevel) override;
    void readTexture(TextureHandle, void* destination, uint64_t size,
                     uint32_t mipLevel) override;
    SamplerHandle createSampler(const SamplerDesc&) override;
    void destroySampler(SamplerHandle) override;
    ShaderHandle createShader(const ShaderDesc&) override;
    void destroyShader(ShaderHandle) override;
    PipelineHandle createPipeline(const PipelineDesc&) override;
    void destroyPipeline(PipelineHandle) override;
    void waitIdle() override;

    void validationMessage(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                           VkDebugUtilsMessageTypeFlagsEXT type,
                           const VkDebugUtilsMessengerCallbackDataEXT* data);

private:
    friend class VulkanCommandList;

    bool initializeContext();
    bool initializeLayouts();
    bool initializeFrames();
    bool recreateSwapchain();
    void destroySwapchain();
    void shutdown();
    bool waitForIdle();

    bool immediateSubmit(const std::function<void(VkCommandBuffer)>& record);
    void transitionImage(VkCommandBuffer commandBuffer, VkImage image,
                         VkImageAspectFlags aspect, uint32_t mipLevels,
                         VkImageLayout oldLayout, VkImageLayout newLayout,
                         VkPipelineStageFlags2 sourceStageOverride = 0);
    void transitionTexture(VkCommandBuffer commandBuffer,
                           TextureRecord& texture, VkImageLayout newLayout);
    VkBuffer bufferForFrame(BufferRecord& buffer);
    bool uploadToBuffer(BufferRecord& buffer, const void* data, uint64_t size,
                        uint64_t offset);
    bool uploadToTexture(TextureRecord& texture, const void* data,
                         uint64_t size, uint32_t mipLevel);

    void scheduleDelete(std::function<void()> destroy);
    void retireDeletes();
    void drainDeletes();
    void setDebugName(VkObjectType type, uint64_t handle,
                      const std::string& name);
    void apiError(const char* operation, VkResult result);
    void fatalApiError(const char* operation, VkResult result);
    void logLeaks();

    bool allocateDescriptorSet(VkDescriptorSetLayout layout,
                               VkDescriptorSet& set);

    DeviceDesc mDesc;
    DeviceCapabilities mCaps;
    VulkanContext mContext;
    SwapchainState mSwapchain;
    std::array<FrameState, kFramesInFlight> mFrames{};
    uint32_t mFrameIndex = 0;
    uint32_t mImageIndex = 0;
    uint64_t mLastSubmittedSerial = 0;
    uint64_t mCompletedSerial = 0;
    std::vector<DeferredDelete> mDeferred;

    VkDescriptorSetLayout mUniformSetLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout mTextureSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout mPipelineLayout = VK_NULL_HANDLE;

    bool mInitialized = false;
    bool mInFrame = false;
    bool mInPass = false;
    bool mSwapchainDirty = false;
    bool mAcquiredSuboptimal = false;
    bool mFailed = false;
    std::atomic<uint32_t> mValidationFailures{0};
    std::string mPassName;
    std::vector<TextureHandle> mPassColourTextures;
    TextureHandle mPassDepthTexture{};
    VulkanCommandList mCommandList;

    HandleTable<BufferHandle, BufferRecord> mBuffers;
    HandleTable<TextureHandle, TextureRecord> mTextures;
    HandleTable<SamplerHandle, SamplerRecord> mSamplers;
    HandleTable<ShaderHandle, ShaderRecord> mShaders;
    HandleTable<PipelineHandle, PipelineRecord> mPipelines;
};

VkFormat toVkFormat(Format format);
Format fromVkFormat(VkFormat format);
VkImageAspectFlags aspectForFormat(Format format);
uint32_t bytesPerTexel(Format format);
VkBufferUsageFlags toVkBufferUsage(BufferUsage usage);
VkImageUsageFlags toVkImageUsage(TextureUsage usage);
VkShaderStageFlagBits toVkShaderStage(ShaderStage stage);
VkPrimitiveTopology toVkTopology(PrimitiveTopology topology);
VkCullModeFlags toVkCullMode(CullMode mode);
VkCompareOp toVkCompareOp(CompareOp op);
VkFilter toVkFilter(FilterMode mode);
VkSamplerAddressMode toVkAddressMode(AddressMode mode);
VkFormat toVkVertexFormat(VertexFormat format);
VkPipelineColorBlendAttachmentState toVkBlendState(BlendMode mode);
VkImageLayout defaultTextureLayout(TextureUsage usage);

VKAPI_ATTR VkBool32 VKAPI_CALL
debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
              VkDebugUtilsMessageTypeFlagsEXT type,
              const VkDebugUtilsMessengerCallbackDataEXT* data, void* userData);

} // namespace eng::rhi::vulkan
