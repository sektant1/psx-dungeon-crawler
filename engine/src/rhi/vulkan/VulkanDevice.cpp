#include "VulkanDevice.h"

#include <eng/Log.h>

#if defined(ENG_RHI_VULKAN)
#include "VulkanInternal.h"

#include <algorithm>

namespace eng::rhi::vulkan {

VulkanDevice::VulkanDevice(const DeviceDesc& desc)
    : mDesc(desc), mCommandList(*this)
{
}

VulkanDevice::~VulkanDevice()
{
    shutdown();
}

bool VulkanDevice::initialize()
{
    if (!initializeContext() || !initializeLayouts() || !initializeFrames() ||
        !recreateSwapchain())
        return false;
    if (mValidationFailures.load() != 0) {
        log::error("rhi(vulkan): initialization produced %u validation "
                   "warnings/errors",
                   mValidationFailures.load());
        return false;
    }
    mInitialized = true;
    log::info("rhi(vulkan): initialized %s (%ux%u, %zu swapchain images)",
              mCaps.deviceName.c_str(), mSwapchain.handle.extent.width,
              mSwapchain.handle.extent.height, mSwapchain.images.size());
    return true;
}

void VulkanDevice::resizeSwapchain(uint32_t width, uint32_t height)
{
    mDesc.width = width;
    mDesc.height = height;
    mSwapchainDirty = true;
    if (!mInFrame && width > 0 && height > 0)
        recreateSwapchain();
}

void VulkanDevice::waitIdle()
{
    waitForIdle();
}

bool VulkanDevice::waitForIdle()
{
    if (mContext.device.device == VK_NULL_HANDLE)
        return true;
    if (mInFrame) {
        log::error(
            "rhi(vulkan): waitIdle cannot be called while a frame is open");
        return false;
    }
    const VkResult result = vkDeviceWaitIdle(mContext.device.device);
    if (result != VK_SUCCESS) {
        apiError("vkDeviceWaitIdle", result);
        return false;
    }
    mCompletedSerial = mLastSubmittedSerial;
    drainDeletes();
    return true;
}

void VulkanDevice::scheduleDelete(std::function<void()> destroy)
{
    if (!destroy)
        return;
    const uint64_t retireAfter = mLastSubmittedSerial + (mInFrame ? 1u : 0u);
    if (retireAfter <= mCompletedSerial && !mInFrame) {
        destroy();
        return;
    }
    mDeferred.push_back({retireAfter, std::move(destroy)});
}

void VulkanDevice::retireDeletes()
{
    auto firstAlive = std::remove_if(
        mDeferred.begin(), mDeferred.end(), [&](DeferredDelete& pending) {
            if (pending.retireAfter > mCompletedSerial)
                return false;
            pending.destroy();
            return true;
        });
    mDeferred.erase(firstAlive, mDeferred.end());
}

void VulkanDevice::drainDeletes()
{
    for (DeferredDelete& pending : mDeferred)
        pending.destroy();
    mDeferred.clear();
}

void VulkanDevice::logLeaks()
{
    const size_t leaked = mBuffers.liveCount() + mTextures.liveCount() +
                          mSamplers.liveCount() + mShaders.liveCount() +
                          mPipelines.liveCount();
    if (leaked)
        log::error("rhi(vulkan): %zu resources still alive at teardown",
                   leaked);
}

void VulkanDevice::shutdown()
{
    if (mContext.instance.instance == VK_NULL_HANDLE)
        return;

    // An abandoned command buffer was never submitted and owns no GPU work.
    // Drop its recording state so teardown can still wait and drain safely.
    mInPass = false;
    mInFrame = false;
    waitIdle();
    logLeaks();

    mPipelines.destroyAll([&](PipelineRecord& record) {
        if (record.pipeline)
            vkDestroyPipeline(mContext.device.device, record.pipeline, nullptr);
    });
    mShaders.destroyAll([&](ShaderRecord& record) {
        if (record.module)
            vkDestroyShaderModule(mContext.device.device, record.module,
                                  nullptr);
    });
    mSamplers.destroyAll([&](SamplerRecord& record) {
        if (record.sampler)
            vkDestroySampler(mContext.device.device, record.sampler, nullptr);
    });
    mTextures.destroyAll([&](TextureRecord& record) {
        if (record.view)
            vkDestroyImageView(mContext.device.device, record.view, nullptr);
        if (record.image)
            vmaDestroyImage(mContext.allocator, record.image,
                            record.allocation);
    });
    mBuffers.destroyAll([&](BufferRecord& record) {
        const uint32_t count = record.dynamic ? kFramesInFlight : 1;
        for (uint32_t i = 0; i < count; ++i) {
            if (record.backing[i].buffer)
                vmaDestroyBuffer(mContext.allocator, record.backing[i].buffer,
                                 record.backing[i].allocation);
        }
    });
    drainDeletes();

    destroySwapchain();
    for (FrameState& frame : mFrames) {
        if (frame.descriptorPool)
            vkDestroyDescriptorPool(mContext.device.device,
                                    frame.descriptorPool, nullptr);
        if (frame.fence)
            vkDestroyFence(mContext.device.device, frame.fence, nullptr);
        if (frame.imageAvailable)
            vkDestroySemaphore(mContext.device.device, frame.imageAvailable,
                               nullptr);
        if (frame.commandPool)
            vkDestroyCommandPool(mContext.device.device, frame.commandPool,
                                 nullptr);
        frame = {};
    }
    if (mPipelineLayout)
        vkDestroyPipelineLayout(mContext.device.device, mPipelineLayout,
                                nullptr);
    if (mTextureSetLayout)
        vkDestroyDescriptorSetLayout(mContext.device.device, mTextureSetLayout,
                                     nullptr);
    if (mUniformSetLayout)
        vkDestroyDescriptorSetLayout(mContext.device.device, mUniformSetLayout,
                                     nullptr);
    mPipelineLayout = VK_NULL_HANDLE;
    mTextureSetLayout = VK_NULL_HANDLE;
    mUniformSetLayout = VK_NULL_HANDLE;

    if (mContext.uploadFence)
        vkDestroyFence(mContext.device.device, mContext.uploadFence, nullptr);
    if (mContext.uploadPool)
        vkDestroyCommandPool(mContext.device.device, mContext.uploadPool,
                             nullptr);
    if (mContext.allocator)
        vmaDestroyAllocator(mContext.allocator);
    if (mContext.device.device)
        vkb::destroy_device(mContext.device);
    if (mContext.surface)
        vkb::destroy_surface(mContext.instance, mContext.surface);
    vkb::destroy_instance(mContext.instance);
    mContext = {};
    mInitialized = false;
}

std::unique_ptr<Device> createDevice(const DeviceDesc& desc)
{
    if (!desc.platformWindow) {
        log::warn("rhi(vulkan): DeviceDesc has no SDL window; Vulkan requires "
                  "an SDL_WINDOW_VULKAN surface");
        return nullptr;
    }
    auto* window = static_cast<SDL_Window*>(desc.platformWindow);
    if ((SDL_GetWindowFlags(window) & SDL_WINDOW_VULKAN) == 0) {
        log::warn(
            "rhi(vulkan): SDL window was not created with SDL_WINDOW_VULKAN");
        return nullptr;
    }
    auto device = std::make_unique<VulkanDevice>(desc);
    if (!device->initialize())
        return nullptr;
    return device;
}

} // namespace eng::rhi::vulkan

#else

namespace eng::rhi::vulkan {

std::unique_ptr<Device> createDevice(const DeviceDesc&)
{
    log::warn("rhi: the Vulkan backend is not compiled (configure with "
              "ENG_RHI_VULKAN=ON)");
    return nullptr;
}

} // namespace eng::rhi::vulkan

#endif
