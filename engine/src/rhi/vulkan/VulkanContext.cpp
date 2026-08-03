#include "VulkanInternal.h"

#include <algorithm>
#include <limits>

namespace eng::rhi::vulkan {

namespace {

void logBootstrapError(const char* operation, const vkb::Error& error)
{
    log::warn("rhi(vulkan): %s failed: %s (VkResult %d)", operation,
              error.type.message().c_str(), static_cast<int>(error.vk_result));
    for (const std::string& reason : error.detailed_failure_reasons)
        log::warn("rhi(vulkan):   %s", reason.c_str());
}

struct AccessScope {
    VkPipelineStageFlags2 stages = VK_PIPELINE_STAGE_2_NONE;
    VkAccessFlags2 access = VK_ACCESS_2_NONE;
};

AccessScope scopeForLayout(VkImageLayout layout)
{
    switch (layout) {
    case VK_IMAGE_LAYOUT_UNDEFINED:
    case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
        return {};
    case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
        return {VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                VK_ACCESS_2_TRANSFER_WRITE_BIT};
    case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
        return {VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                VK_ACCESS_2_TRANSFER_READ_BIT};
    case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
        return {VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT |
                    VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT};
    case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
        return {VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                    VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                    VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT};
    case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
        return {VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                    VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT |
                    VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT |
                    VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                    VK_ACCESS_2_SHADER_SAMPLED_READ_BIT};
    case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
        return {VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT |
                    VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                VK_ACCESS_2_SHADER_SAMPLED_READ_BIT};
    case VK_IMAGE_LAYOUT_GENERAL:
        return {VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT};
    default:
        return {VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT};
    }
}

bool supportsSrgbAttachment(VkPhysicalDevice physicalDevice)
{
    for (VkFormat format : {VK_FORMAT_R8G8B8A8_SRGB, VK_FORMAT_B8G8R8A8_SRGB}) {
        VkFormatProperties properties{};
        vkGetPhysicalDeviceFormatProperties(physicalDevice, format,
                                            &properties);
        if (properties.optimalTilingFeatures &
            VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT)
            return true;
    }
    return false;
}

} // namespace

VKAPI_ATTR VkBool32 VKAPI_CALL
debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
              VkDebugUtilsMessageTypeFlagsEXT type,
              const VkDebugUtilsMessengerCallbackDataEXT* data, void* userData)
{
    if (userData)
        static_cast<VulkanDevice*>(userData)->validationMessage(severity, type,
                                                                data);
    return VK_FALSE;
}

bool VulkanDevice::initializeContext()
{
    auto* window = static_cast<SDL_Window*>(mDesc.platformWindow);
    unsigned int extensionCount = 0;
    if (!SDL_Vulkan_GetInstanceExtensions(window, &extensionCount, nullptr)) {
        log::warn("rhi(vulkan): SDL could not enumerate Vulkan instance "
                  "extensions: %s",
                  SDL_GetError());
        return false;
    }
    std::vector<const char*> extensions(extensionCount);
    if (!SDL_Vulkan_GetInstanceExtensions(window, &extensionCount,
                                          extensions.data())) {
        log::warn(
            "rhi(vulkan): SDL could not read Vulkan instance extensions: %s",
            SDL_GetError());
        return false;
    }

    const auto systemResult = vkb::SystemInfo::get_system_info();
    if (!systemResult) {
        logBootstrapError("loading the Vulkan loader",
                          systemResult.full_error());
        return false;
    }
    const vkb::SystemInfo& system = systemResult.value();
    const bool validationAvailable =
        system.is_layer_available("VK_LAYER_KHRONOS_validation");
    if (mDesc.enableValidation && !validationAvailable) {
        log::warn("rhi(vulkan): validation requested, but "
                  "VK_LAYER_KHRONOS_validation is unavailable");
        return false;
    }

    vkb::InstanceBuilder instanceBuilder;
    instanceBuilder.set_app_name("Raven Engine")
        .set_engine_name("Raven Engine")
        .require_api_version(1, 3, 0)
        .enable_extensions(extensions);

    if (system.is_extension_available(VK_EXT_DEBUG_UTILS_EXTENSION_NAME))
        instanceBuilder.enable_extension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    if (mDesc.enableValidation && validationAvailable) {
        instanceBuilder.enable_validation_layers()
            .set_debug_callback(debugCallback)
            .set_debug_callback_user_data_pointer(this)
            .set_debug_messenger_severity(
                VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
            .add_validation_feature_enable(
                VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT)
            .add_validation_feature_enable(
                VK_VALIDATION_FEATURE_ENABLE_BEST_PRACTICES_EXT);
    }

    auto instanceResult = instanceBuilder.build();
    if (!instanceResult) {
        logBootstrapError("creating the Vulkan instance",
                          instanceResult.full_error());
        return false;
    }
    mContext.instance = std::move(instanceResult.value());

    if (!SDL_Vulkan_CreateSurface(window, mContext.instance.instance,
                                  &mContext.surface)) {
        log::warn("rhi(vulkan): SDL could not create the Vulkan surface: %s",
                  SDL_GetError());
        return false;
    }

    VkPhysicalDeviceVulkan13Features required13{};
    required13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    required13.dynamicRendering = VK_TRUE;
    required13.synchronization2 = VK_TRUE;
    // glslang lowers `discard` to OpDemoteToHelperInvocation when targeting
    // vulkan1.3, so any shader with alpha scissor needs this enabled or
    // vkCreateShaderModule trips VUID-VkShaderModuleCreateInfo-pCode-08740.
    // Core in 1.3 and required of every conformant 1.3 device, so asking for it
    // does not narrow selection.
    required13.shaderDemoteToHelperInvocation = VK_TRUE;

    vkb::PhysicalDeviceSelector selector(mContext.instance, mContext.surface);
    auto physicalResult = selector.set_minimum_version(1, 3)
                              .require_present()
                              .set_required_features_13(required13)
                              .select();
    if (!physicalResult) {
        logBootstrapError("selecting a Vulkan 1.3 device with dynamic "
                          "rendering and synchronization2",
                          physicalResult.full_error());
        return false;
    }
    mContext.physical = std::move(physicalResult.value());

    VkPhysicalDeviceFeatures optionalFeatures{};
    optionalFeatures.samplerAnisotropy = VK_TRUE;
    mContext.samplerAnisotropy =
        mContext.physical.enable_features_if_present(optionalFeatures);

    // Optional, and asked for separately so a device without it still selects:
    // only the wireframe debug view needs line-filled polygons.
    VkPhysicalDeviceFeatures fillModeFeature{};
    fillModeFeature.fillModeNonSolid = VK_TRUE;
    mContext.fillModeNonSolid =
        mContext.physical.enable_features_if_present(fillModeFeature);
    if (!mContext.fillModeNonSolid)
        log::warn("rhi(vulkan): no fillModeNonSolid; the wireframe debug view "
                  "will draw solid");

    auto deviceResult = vkb::DeviceBuilder(mContext.physical).build();
    if (!deviceResult) {
        logBootstrapError("creating the Vulkan logical device",
                          deviceResult.full_error());
        return false;
    }
    mContext.device = std::move(deviceResult.value());

    auto graphicsQueue = mContext.device.get_queue(vkb::QueueType::graphics);
    auto graphicsFamily =
        mContext.device.get_queue_index(vkb::QueueType::graphics);
    auto presentQueue = mContext.device.get_queue(vkb::QueueType::present);
    auto presentFamily =
        mContext.device.get_queue_index(vkb::QueueType::present);
    if (!graphicsQueue || !graphicsFamily || !presentQueue || !presentFamily) {
        log::warn("rhi(vulkan): vk-bootstrap did not expose required "
                  "graphics/present queues");
        return false;
    }
    mContext.graphicsQueue = graphicsQueue.value();
    mContext.graphicsFamily = graphicsFamily.value();
    mContext.presentQueue = presentQueue.value();
    mContext.presentFamily = presentFamily.value();

    VmaAllocatorCreateInfo allocatorInfo{};
    allocatorInfo.physicalDevice = mContext.physical.physical_device;
    allocatorInfo.device = mContext.device.device;
    allocatorInfo.instance = mContext.instance.instance;
    allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_3;
    const VkResult allocatorResult =
        vmaCreateAllocator(&allocatorInfo, &mContext.allocator);
    if (allocatorResult != VK_SUCCESS) {
        apiError("vmaCreateAllocator", allocatorResult);
        return false;
    }

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT |
                     VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    poolInfo.queueFamilyIndex = mContext.graphicsFamily;
    VkResult result = vkCreateCommandPool(mContext.device.device, &poolInfo,
                                          nullptr, &mContext.uploadPool);
    if (result != VK_SUCCESS) {
        apiError("vkCreateCommandPool(upload)", result);
        return false;
    }

    VkCommandBufferAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocateInfo.commandPool = mContext.uploadPool;
    allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocateInfo.commandBufferCount = 1;
    result = vkAllocateCommandBuffers(mContext.device.device, &allocateInfo,
                                      &mContext.uploadCommandBuffer);
    if (result != VK_SUCCESS) {
        apiError("vkAllocateCommandBuffers(upload)", result);
        return false;
    }

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    result = vkCreateFence(mContext.device.device, &fenceInfo, nullptr,
                           &mContext.uploadFence);
    if (result != VK_SUCCESS) {
        apiError("vkCreateFence(upload)", result);
        return false;
    }

    mContext.setObjectName =
        reinterpret_cast<PFN_vkSetDebugUtilsObjectNameEXT>(vkGetDeviceProcAddr(
            mContext.device.device, "vkSetDebugUtilsObjectNameEXT"));
    mContext.beginLabel =
        reinterpret_cast<PFN_vkCmdBeginDebugUtilsLabelEXT>(vkGetDeviceProcAddr(
            mContext.device.device, "vkCmdBeginDebugUtilsLabelEXT"));
    mContext.endLabel =
        reinterpret_cast<PFN_vkCmdEndDebugUtilsLabelEXT>(vkGetDeviceProcAddr(
            mContext.device.device, "vkCmdEndDebugUtilsLabelEXT"));

    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(mContext.physical.physical_device,
                                  &properties);
    mCaps.deviceName = properties.deviceName;
    mCaps.backendName = "vulkan";
    mCaps.maxTextureSize = properties.limits.maxImageDimension2D;
    mCaps.maxColourAttachments = properties.limits.maxColorAttachments;
    mCaps.maxSimultaneousLights = 16;
    mCaps.maxUniformBufferBindings = kUniformBindingCount;
    mCaps.maxTextureBindings = kTextureBindingCount;
    mCaps.uniformBufferOffsetAlignment =
        properties.limits.minUniformBufferOffsetAlignment;
    mCaps.maxUniformBufferRange = properties.limits.maxUniformBufferRange;
    mCaps.supportsCompute = false;
    mCaps.supportsAnisotropicFiltering = mContext.samplerAnisotropy;
    mCaps.supportsSrgbFramebuffer =
        supportsSrgbAttachment(mContext.physical.physical_device);
    return true;
}

bool VulkanDevice::initializeFrames()
{
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = mContext.graphicsFamily;

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    const std::array<VkDescriptorPoolSize, 2> sizes{{
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
         kDescriptorSetsPerType * kUniformBindingCount},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
         kDescriptorSetsPerType * kTextureBindingCount},
    }};
    VkDescriptorPoolCreateInfo descriptorInfo{};
    descriptorInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    descriptorInfo.maxSets = kDescriptorSetsPerType * 2;
    descriptorInfo.poolSizeCount = static_cast<uint32_t>(sizes.size());
    descriptorInfo.pPoolSizes = sizes.data();

    for (uint32_t i = 0; i < kFramesInFlight; ++i) {
        FrameState& frame = mFrames[i];
        VkResult result = vkCreateCommandPool(mContext.device.device, &poolInfo,
                                              nullptr, &frame.commandPool);
        if (result != VK_SUCCESS) {
            apiError("vkCreateCommandPool(frame)", result);
            return false;
        }
        VkCommandBufferAllocateInfo allocateInfo{};
        allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocateInfo.commandPool = frame.commandPool;
        allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocateInfo.commandBufferCount = 1;
        result = vkAllocateCommandBuffers(mContext.device.device, &allocateInfo,
                                          &frame.commandBuffer);
        if (result != VK_SUCCESS) {
            apiError("vkAllocateCommandBuffers(frame)", result);
            return false;
        }
        result = vkCreateSemaphore(mContext.device.device, &semaphoreInfo,
                                   nullptr, &frame.imageAvailable);
        if (result != VK_SUCCESS) {
            apiError("vkCreateSemaphore(image available)", result);
            return false;
        }
        result = vkCreateFence(mContext.device.device, &fenceInfo, nullptr,
                               &frame.fence);
        if (result != VK_SUCCESS) {
            apiError("vkCreateFence(frame)", result);
            return false;
        }
        result = vkCreateDescriptorPool(mContext.device.device, &descriptorInfo,
                                        nullptr, &frame.descriptorPool);
        if (result != VK_SUCCESS) {
            apiError("vkCreateDescriptorPool(frame)", result);
            return false;
        }
    }
    return true;
}

bool VulkanDevice::recreateSwapchain()
{
    int drawableWidth = 0;
    int drawableHeight = 0;
    SDL_Vulkan_GetDrawableSize(static_cast<SDL_Window*>(mDesc.platformWindow),
                               &drawableWidth, &drawableHeight);
    if (drawableWidth <= 0 || drawableHeight <= 0) {
        mSwapchainDirty = true;
        return false;
    }

    if (mSwapchain.handle.swapchain != VK_NULL_HANDLE && !waitForIdle())
        return false;

    vkb::SwapchainBuilder builder(
        mContext.physical.physical_device, mContext.device.device,
        mContext.surface, mContext.graphicsFamily, mContext.presentFamily);
    // UNORM, deliberately. This renderer carries sRGB-*encoded* 8-bit values
    // end to end: the scene shader finishes on toSrgb(), the stylize/grade/
    // dither passes are ports of compositor shaders that operate on that
    // encoded colour, and ImGui writes sRGB vertex colours. An _SRGB swapchain
    // treats a fragment output as linear and encodes it again on write, so the
    // whole composite -- world *and* HUD -- reached the screen a second gamma
    // too bright. With UNORM the present blit is the plain copy it is meant to
    // be. (The offscreen readback behind writeScreenshot samples finalColour,
    // before this step, which is why screenshots never showed the difference.)
    const VkFormat preferredFormat = mSwapchain.rhiFormat == Format::Unknown
                                         ? VK_FORMAT_B8G8R8A8_UNORM
                                         : toVkFormat(mSwapchain.rhiFormat);
    builder
        .set_desired_extent(static_cast<uint32_t>(drawableWidth),
                            static_cast<uint32_t>(drawableHeight))
        .set_desired_format(
            {preferredFormat, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR})
        .add_fallback_format(
            {VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR})
        .add_fallback_format(
            {VK_FORMAT_R8G8B8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR})
        .add_fallback_format(
            {VK_FORMAT_B8G8R8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR})
        .add_fallback_format(
            {VK_FORMAT_R8G8B8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR})
        .set_desired_min_image_count(3)
        .set_image_usage_flags(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
    if (mDesc.vsync) {
        builder.set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR);
    }
    else {
        builder.set_desired_present_mode(VK_PRESENT_MODE_IMMEDIATE_KHR)
            .add_fallback_present_mode(VK_PRESENT_MODE_MAILBOX_KHR)
            .add_fallback_present_mode(VK_PRESENT_MODE_FIFO_KHR);
    }
    if (mSwapchain.handle.swapchain != VK_NULL_HANDLE)
        builder.set_old_swapchain(mSwapchain.handle);

    auto swapchainResult = builder.build();
    if (!swapchainResult) {
        logBootstrapError("creating the Vulkan swapchain",
                          swapchainResult.full_error());
        mSwapchainDirty = true;
        return false;
    }
    vkb::Swapchain newHandle = std::move(swapchainResult.value());
    const Format newFormat = fromVkFormat(newHandle.image_format);
    if (newFormat == Format::Unknown) {
        log::error("rhi(vulkan): swapchain selected unsupported VkFormat %d",
                   static_cast<int>(newHandle.image_format));
        vkb::destroy_swapchain(newHandle);
        return false;
    }
    if (newFormat == Format::RGBA8Srgb || newFormat == Format::BGRA8Srgb)
        log::warn("rhi(vulkan): swapchain fell back to an sRGB format; the "
                  "present blit carries already-encoded colour, so the frame "
                  "will reach the screen one gamma too bright");
    if (mSwapchain.rhiFormat != Format::Unknown &&
        mSwapchain.rhiFormat != newFormat) {
        log::error("rhi(vulkan): swapchain format changed during resize; "
                   "existing pipelines cannot be reused safely");
        vkb::destroy_swapchain(newHandle);
        return false;
    }

    auto imagesResult = newHandle.get_images();
    if (!imagesResult) {
        log::error("rhi(vulkan): could not retrieve swapchain images");
        vkb::destroy_swapchain(newHandle);
        return false;
    }
    std::vector<VkImage> newImages = std::move(imagesResult.value());
    auto viewsResult = newHandle.get_image_views();
    if (!viewsResult) {
        log::error("rhi(vulkan): could not create swapchain image views");
        vkb::destroy_swapchain(newHandle);
        return false;
    }
    std::vector<VkImageView> newViews = std::move(viewsResult.value());
    std::vector<VkSemaphore> newRenderFinished(newImages.size(),
                                               VK_NULL_HANDLE);
    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    for (VkSemaphore& semaphore : newRenderFinished) {
        const VkResult result = vkCreateSemaphore(
            mContext.device.device, &semaphoreInfo, nullptr, &semaphore);
        if (result != VK_SUCCESS) {
            for (VkSemaphore created : newRenderFinished)
                if (created)
                    vkDestroySemaphore(mContext.device.device, created,
                                       nullptr);
            newHandle.destroy_image_views(newViews);
            vkb::destroy_swapchain(newHandle);
            apiError("vkCreateSemaphore(render finished)", result);
            return false;
        }
    }

    destroySwapchain();
    mSwapchain.handle = std::move(newHandle);
    mSwapchain.images = std::move(newImages);
    mSwapchain.views = std::move(newViews);
    mSwapchain.renderFinished = std::move(newRenderFinished);
    mSwapchain.layouts.assign(mSwapchain.images.size(),
                              VK_IMAGE_LAYOUT_UNDEFINED);
    mSwapchain.rhiFormat = newFormat;
    mSwapchainDirty = false;
    mAcquiredSuboptimal = false;
    return true;
}

void VulkanDevice::destroySwapchain()
{
    if (mContext.device.device == VK_NULL_HANDLE)
        return;
    for (VkSemaphore semaphore : mSwapchain.renderFinished)
        if (semaphore)
            vkDestroySemaphore(mContext.device.device, semaphore, nullptr);
    mSwapchain.renderFinished.clear();
    if (mSwapchain.handle.swapchain != VK_NULL_HANDLE) {
        mSwapchain.handle.destroy_image_views(mSwapchain.views);
        vkb::destroy_swapchain(mSwapchain.handle);
    }
    mSwapchain = {};
}

bool VulkanDevice::immediateSubmit(
    const std::function<void(VkCommandBuffer)>& record)
{
    if (mFailed)
        return false;
    if (mContext.uploadFence == VK_NULL_HANDLE) {
        log::error("rhi(vulkan): upload queue is unavailable");
        return false;
    }
    VkResult result =
        vkWaitForFences(mContext.device.device, 1, &mContext.uploadFence,
                        VK_TRUE, std::numeric_limits<uint64_t>::max());
    if (result != VK_SUCCESS) {
        apiError("vkWaitForFences(upload)", result);
        return false;
    }
    result = vkResetCommandPool(mContext.device.device, mContext.uploadPool, 0);
    if (result != VK_SUCCESS) {
        apiError("vkResetCommandPool(upload)", result);
        return false;
    }
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    result = vkBeginCommandBuffer(mContext.uploadCommandBuffer, &beginInfo);
    if (result != VK_SUCCESS) {
        apiError("vkBeginCommandBuffer(upload)", result);
        return false;
    }
    record(mContext.uploadCommandBuffer);
    result = vkEndCommandBuffer(mContext.uploadCommandBuffer);
    if (result != VK_SUCCESS) {
        apiError("vkEndCommandBuffer(upload)", result);
        return false;
    }

    VkCommandBufferSubmitInfo commandInfo{};
    commandInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    commandInfo.commandBuffer = mContext.uploadCommandBuffer;
    VkSubmitInfo2 submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submitInfo.commandBufferInfoCount = 1;
    submitInfo.pCommandBufferInfos = &commandInfo;

    result = vkResetFences(mContext.device.device, 1, &mContext.uploadFence);
    if (result != VK_SUCCESS) {
        apiError("vkResetFences(upload)", result);
        return false;
    }
    result = vkQueueSubmit2(mContext.graphicsQueue, 1, &submitInfo,
                            mContext.uploadFence);
    if (result != VK_SUCCESS) {
        apiError("vkQueueSubmit2(upload)", result);
        vkDestroyFence(mContext.device.device, mContext.uploadFence, nullptr);
        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        mContext.uploadFence = VK_NULL_HANDLE;
        const VkResult fenceResult = vkCreateFence(
            mContext.device.device, &fenceInfo, nullptr, &mContext.uploadFence);
        if (fenceResult != VK_SUCCESS)
            apiError("vkCreateFence(upload recovery)", fenceResult);
        return false;
    }
    result = vkWaitForFences(mContext.device.device, 1, &mContext.uploadFence,
                             VK_TRUE, std::numeric_limits<uint64_t>::max());
    if (result != VK_SUCCESS) {
        apiError("vkWaitForFences(upload completion)", result);
        return false;
    }
    return true;
}

void VulkanDevice::transitionImage(VkCommandBuffer commandBuffer, VkImage image,
                                   VkImageAspectFlags aspect,
                                   uint32_t mipLevels, VkImageLayout oldLayout,
                                   VkImageLayout newLayout,
                                   VkPipelineStageFlags2 sourceStageOverride)
{
    if (oldLayout == newLayout)
        return;
    AccessScope source = scopeForLayout(oldLayout);
    if (sourceStageOverride != 0)
        source.stages = sourceStageOverride;
    const AccessScope destination = scopeForLayout(newLayout);
    VkImageMemoryBarrier2 barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barrier.srcStageMask = source.stages;
    barrier.srcAccessMask = source.access;
    barrier.dstStageMask = destination.stages;
    barrier.dstAccessMask = destination.access;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = aspect;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = mipLevels;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    VkDependencyInfo dependency{};
    dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(commandBuffer, &dependency);
}

void VulkanDevice::transitionTexture(VkCommandBuffer commandBuffer,
                                     TextureRecord& texture,
                                     VkImageLayout newLayout)
{
    transitionImage(commandBuffer, texture.image, texture.aspect,
                    texture.mipLevels, texture.layout, newLayout);
    texture.layout = newLayout;
}

void VulkanDevice::setDebugName(VkObjectType type, uint64_t handle,
                                const std::string& name)
{
    if (!mContext.setObjectName || handle == 0 || name.empty())
        return;
    VkDebugUtilsObjectNameInfoEXT info{};
    info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
    info.objectType = type;
    info.objectHandle = handle;
    info.pObjectName = name.c_str();
    const VkResult result =
        mContext.setObjectName(mContext.device.device, &info);
    if (result != VK_SUCCESS)
        apiError("vkSetDebugUtilsObjectNameEXT", result);
}

void VulkanDevice::apiError(const char* operation, VkResult result)
{
    log::error("rhi(vulkan): %s failed with VkResult %d", operation,
               static_cast<int>(result));
    if (result == VK_ERROR_DEVICE_LOST)
        mFailed = true;
}

void VulkanDevice::fatalApiError(const char* operation, VkResult result)
{
    apiError(operation, result);
    mFailed = true;
}

void VulkanDevice::validationMessage(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT type,
    const VkDebugUtilsMessengerCallbackDataEXT* data)
{
    const char* id =
        data && data->pMessageIdName ? data->pMessageIdName : "unknown";
    const char* message =
        data && data->pMessage ? data->pMessage : "(no message)";
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        ++mValidationFailures;
        log::error("vulkan validation [%s, type=0x%x]: %s", id,
                   static_cast<unsigned int>(type), message);
    }
    else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        ++mValidationFailures;
        log::warn("vulkan validation [%s, type=0x%x]: %s", id,
                  static_cast<unsigned int>(type), message);
    }
    else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) {
        log::info("vulkan validation [%s]: %s", id, message);
    }
}

} // namespace eng::rhi::vulkan
