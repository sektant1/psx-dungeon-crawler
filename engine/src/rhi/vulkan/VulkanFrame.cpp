#include "VulkanInternal.h"

#include <algorithm>
#include <limits>

namespace eng::rhi::vulkan {

namespace {

bool sameExtent(VkExtent2D extent, uint32_t width, uint32_t height)
{
    return extent.width == width && extent.height == height;
}

} // namespace

bool VulkanDevice::beginFrame()
{
    if (!mInitialized || mFailed)
        return false;
    if (mInFrame) {
        log::error("rhi(vulkan): beginFrame while a frame is open");
        return false;
    }

    int drawableWidth = 0;
    int drawableHeight = 0;
    SDL_Vulkan_GetDrawableSize(static_cast<SDL_Window*>(mDesc.platformWindow),
                               &drawableWidth, &drawableHeight);
    if (drawableWidth <= 0 || drawableHeight <= 0) {
        mSwapchainDirty = true;
        return false;
    }
    if (!sameExtent(mSwapchain.handle.extent,
                    static_cast<uint32_t>(drawableWidth),
                    static_cast<uint32_t>(drawableHeight)))
        mSwapchainDirty = true;
    if (mSwapchainDirty && !recreateSwapchain())
        return false;

    FrameState& frame = mFrames[mFrameIndex];
    VkResult result =
        vkWaitForFences(mContext.device.device, 1, &frame.fence, VK_TRUE,
                        std::numeric_limits<uint64_t>::max());
    if (result != VK_SUCCESS) {
        fatalApiError("vkWaitForFences(frame)", result);
        return false;
    }
    mCompletedSerial = std::max(mCompletedSerial, frame.submittedSerial);
    retireDeletes();

    result = vkAcquireNextImageKHR(
        mContext.device.device, mSwapchain.handle.swapchain,
        std::numeric_limits<uint64_t>::max(), frame.imageAvailable,
        VK_NULL_HANDLE, &mImageIndex);
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        mSwapchainDirty = true;
        recreateSwapchain();
        return false;
    }
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        fatalApiError("vkAcquireNextImageKHR", result);
        return false;
    }
    mAcquiredSuboptimal = result == VK_SUBOPTIMAL_KHR;

    result = vkResetCommandPool(mContext.device.device, frame.commandPool, 0);
    if (result != VK_SUCCESS) {
        fatalApiError("vkResetCommandPool(frame)", result);
        return false;
    }
    result =
        vkResetDescriptorPool(mContext.device.device, frame.descriptorPool, 0);
    if (result != VK_SUCCESS) {
        fatalApiError("vkResetDescriptorPool(frame)", result);
        return false;
    }
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    result = vkBeginCommandBuffer(frame.commandBuffer, &beginInfo);
    if (result != VK_SUCCESS) {
        fatalApiError("vkBeginCommandBuffer(frame)", result);
        return false;
    }
    mInFrame = true;
    return true;
}

void VulkanDevice::endFrame()
{
    if (!mInFrame) {
        log::error("rhi(vulkan): endFrame outside a frame");
        return;
    }
    if (mInPass) {
        log::error("rhi(vulkan): endFrame with pass '%s' still open",
                   mPassName.c_str());
        endPass();
    }

    FrameState& frame = mFrames[mFrameIndex];
    VkCommandBuffer commandBuffer = frame.commandBuffer;
    transitionImage(commandBuffer, mSwapchain.images[mImageIndex],
                    VK_IMAGE_ASPECT_COLOR_BIT, 1,
                    mSwapchain.layouts[mImageIndex],
                    VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
    mSwapchain.layouts[mImageIndex] = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkResult result = vkEndCommandBuffer(commandBuffer);
    if (result != VK_SUCCESS) {
        fatalApiError("vkEndCommandBuffer(frame)", result);
        mInFrame = false;
        return;
    }

    VkSemaphoreSubmitInfo waitInfo{};
    waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    waitInfo.semaphore = frame.imageAvailable;
    waitInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    VkCommandBufferSubmitInfo commandInfo{};
    commandInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    commandInfo.commandBuffer = commandBuffer;
    VkSemaphoreSubmitInfo signalInfo{};
    signalInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    signalInfo.semaphore = mSwapchain.renderFinished[mImageIndex];
    signalInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    VkSubmitInfo2 submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submitInfo.waitSemaphoreInfoCount = 1;
    submitInfo.pWaitSemaphoreInfos = &waitInfo;
    submitInfo.commandBufferInfoCount = 1;
    submitInfo.pCommandBufferInfos = &commandInfo;
    submitInfo.signalSemaphoreInfoCount = 1;
    submitInfo.pSignalSemaphoreInfos = &signalInfo;

    // Every operation that can fail before submission is complete. The fence is
    // reset only now, immediately before the submit that will signal it.
    result = vkResetFences(mContext.device.device, 1, &frame.fence);
    if (result != VK_SUCCESS) {
        fatalApiError("vkResetFences(frame)", result);
        mInFrame = false;
        return;
    }
    result =
        vkQueueSubmit2(mContext.graphicsQueue, 1, &submitInfo, frame.fence);
    if (result != VK_SUCCESS) {
        fatalApiError("vkQueueSubmit2(frame)", result);
        vkDestroyFence(mContext.device.device, frame.fence, nullptr);
        frame.fence = VK_NULL_HANDLE;
        mInFrame = false;
        return;
    }
    frame.submittedSerial = ++mLastSubmittedSerial;

    const VkSemaphore rendered = mSwapchain.renderFinished[mImageIndex];
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &rendered;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &mSwapchain.handle.swapchain;
    presentInfo.pImageIndices = &mImageIndex;
    result = vkQueuePresentKHR(mContext.presentQueue, &presentInfo);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR ||
        mAcquiredSuboptimal) {
        mSwapchainDirty = true;
    }
    else if (result != VK_SUCCESS) {
        fatalApiError("vkQueuePresentKHR", result);
    }

    mInFrame = false;
    mFrameIndex = (mFrameIndex + 1) % kFramesInFlight;
}

CommandList& VulkanDevice::beginPass(const RenderPassDesc& desc)
{
    if (!mInFrame) {
        log::error("rhi(vulkan): beginPass outside a frame");
        return mCommandList;
    }
    if (mInPass) {
        log::error("rhi(vulkan): beginPass '%s' while '%s' is open",
                   desc.debugName.c_str(), mPassName.c_str());
        return mCommandList;
    }
    if (desc.colour.empty() && !desc.depth.texture.valid()) {
        log::error("rhi(vulkan): pass '%s' has no attachments",
                   desc.debugName.c_str());
        return mCommandList;
    }
    if (desc.colour.size() > mCaps.maxColourAttachments) {
        log::error("rhi(vulkan): pass '%s' exceeds colour attachment limit",
                   desc.debugName.c_str());
        return mCommandList;
    }

    VkExtent2D extent{};
    bool extentSet = false;
    bool hasSwapchainAttachment = false;
    std::vector<VkRenderingAttachmentInfo> colours;
    std::vector<VkFormat> colourFormats;
    colours.reserve(desc.colour.size());
    colourFormats.reserve(desc.colour.size());
    mPassColourTextures.clear();

    VkCommandBuffer commandBuffer = mFrames[mFrameIndex].commandBuffer;
    for (const ColourAttachment& attachment : desc.colour) {
        VkImage image = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkFormat format = VK_FORMAT_UNDEFINED;
        VkExtent2D attachmentExtent{};
        if (!attachment.texture.valid()) {
            if (hasSwapchainAttachment) {
                log::error(
                    "rhi(vulkan): pass '%s' names the swapchain more than once",
                    desc.debugName.c_str());
                return mCommandList;
            }
            hasSwapchainAttachment = true;
            image = mSwapchain.images[mImageIndex];
            view = mSwapchain.views[mImageIndex];
            format = mSwapchain.handle.image_format;
            attachmentExtent = mSwapchain.handle.extent;
            transitionImage(commandBuffer, image, VK_IMAGE_ASPECT_COLOR_BIT, 1,
                            mSwapchain.layouts[mImageIndex],
                            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                            VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT);
            mSwapchain.layouts[mImageIndex] =
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        }
        else {
            TextureRecord* texture =
                mTextures.get(attachment.texture, "colour attachment");
            if (!texture)
                return mCommandList;
            if (!any(texture->usage, TextureUsage::RenderTarget)) {
                log::error("rhi(vulkan): texture %u is not a render target",
                           attachment.texture.id);
                return mCommandList;
            }
            image = texture->image;
            view = texture->view;
            format = toVkFormat(texture->format);
            attachmentExtent = {texture->width, texture->height};
            transitionTexture(commandBuffer, *texture,
                              VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        }
        if (!extentSet) {
            extent = attachmentExtent;
            extentSet = true;
        }
        else if (!sameExtent(extent, attachmentExtent.width,
                             attachmentExtent.height)) {
            log::error("rhi(vulkan): pass '%s' attachment extents do not match",
                       desc.debugName.c_str());
            return mCommandList;
        }

        VkRenderingAttachmentInfo renderingAttachment{};
        renderingAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        renderingAttachment.imageView = view;
        renderingAttachment.imageLayout =
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        renderingAttachment.loadOp = attachment.clear
                                         ? VK_ATTACHMENT_LOAD_OP_CLEAR
                                         : VK_ATTACHMENT_LOAD_OP_LOAD;
        renderingAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        std::copy(std::begin(attachment.clearColour),
                  std::end(attachment.clearColour),
                  renderingAttachment.clearValue.color.float32);
        colours.push_back(renderingAttachment);
        colourFormats.push_back(format);
        mPassColourTextures.push_back(attachment.texture);
    }

    VkRenderingAttachmentInfo depthAttachment{};
    VkFormat depthFormat = VK_FORMAT_UNDEFINED;
    mPassDepthTexture = {};
    if (desc.depth.texture.valid()) {
        TextureRecord* texture =
            mTextures.get(desc.depth.texture, "depth attachment");
        if (!texture)
            return mCommandList;
        if (!any(texture->usage, TextureUsage::DepthStencil)) {
            log::error("rhi(vulkan): texture %u is not a depth attachment",
                       desc.depth.texture.id);
            return mCommandList;
        }
        if (!extentSet) {
            extent = {texture->width, texture->height};
            extentSet = true;
        }
        else if (!sameExtent(extent, texture->width, texture->height)) {
            log::error("rhi(vulkan): pass '%s' depth extent does not match",
                       desc.debugName.c_str());
            return mCommandList;
        }
        transitionTexture(commandBuffer, *texture,
                          VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
        depthFormat = toVkFormat(texture->format);
        depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        depthAttachment.imageView = texture->view;
        depthAttachment.imageLayout =
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depthAttachment.loadOp = desc.depth.clear ? VK_ATTACHMENT_LOAD_OP_CLEAR
                                                  : VK_ATTACHMENT_LOAD_OP_LOAD;
        depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        depthAttachment.clearValue.depthStencil = {desc.depth.clearDepth,
                                                   desc.depth.clearStencil};
        mPassDepthTexture = desc.depth.texture;
    }

    VkRenderingInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea.extent = extent;
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = static_cast<uint32_t>(colours.size());
    renderingInfo.pColorAttachments = colours.data();
    if (desc.depth.texture.valid()) {
        renderingInfo.pDepthAttachment = &depthAttachment;
        if (desc.depth.texture.valid()) {
            const TextureRecord* depth =
                mTextures.get(desc.depth.texture, "depth attachment");
            if (depth && (depth->aspect & VK_IMAGE_ASPECT_STENCIL_BIT))
                renderingInfo.pStencilAttachment = &depthAttachment;
        }
    }
    vkCmdBeginRendering(commandBuffer, &renderingInfo);

    mInPass = true;
    mPassName = desc.debugName;
    mCommandList.reset(commandBuffer, extent, std::move(colourFormats),
                       depthFormat);
    return mCommandList;
}

void VulkanDevice::endPass()
{
    if (!mInPass) {
        log::error("rhi(vulkan): endPass without a pass");
        return;
    }
    while (mCommandList.debugDepth() != 0) {
        log::error("rhi(vulkan): pass '%s' ended with a debug group open",
                   mPassName.c_str());
        mCommandList.popDebugGroup();
    }
    VkCommandBuffer commandBuffer = mFrames[mFrameIndex].commandBuffer;
    vkCmdEndRendering(commandBuffer);
    for (TextureHandle handle : mPassColourTextures) {
        if (!handle.valid())
            continue;
        TextureRecord* texture = mTextures.get(handle, "colour attachment");
        if (texture && any(texture->usage, TextureUsage::Sampled))
            transitionTexture(commandBuffer, *texture,
                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
    if (mPassDepthTexture.valid()) {
        TextureRecord* depth =
            mTextures.get(mPassDepthTexture, "depth attachment");
        if (depth && any(depth->usage, TextureUsage::Sampled))
            transitionTexture(commandBuffer, *depth,
                              VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
    }
    mInPass = false;
    mPassName.clear();
    mPassColourTextures.clear();
    mPassDepthTexture = {};
}

bool VulkanDevice::allocateDescriptorSet(VkDescriptorSetLayout layout,
                                         VkDescriptorSet& set)
{
    VkDescriptorSetAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocateInfo.descriptorPool = mFrames[mFrameIndex].descriptorPool;
    allocateInfo.descriptorSetCount = 1;
    allocateInfo.pSetLayouts = &layout;
    const VkResult result =
        vkAllocateDescriptorSets(mContext.device.device, &allocateInfo, &set);
    if (result != VK_SUCCESS) {
        if (result == VK_ERROR_OUT_OF_POOL_MEMORY ||
            result == VK_ERROR_FRAGMENTED_POOL) {
            log::error("rhi(vulkan): descriptor pool exhausted in frame %u; "
                       "active draw skipped",
                       mFrameIndex);
        }
        else {
            apiError("vkAllocateDescriptorSets", result);
        }
        return false;
    }
    return true;
}

void VulkanCommandList::reset(VkCommandBuffer commandBuffer, VkExtent2D extent,
                              std::vector<VkFormat> colourFormats,
                              VkFormat depthFormat)
{
    mCommandBuffer = commandBuffer;
    mExtent = extent;
    mColourFormats = std::move(colourFormats);
    mDepthFormat = depthFormat;
    mPipeline = {};
    mUniforms = {};
    mTextures = {};
    mUniformSet = VK_NULL_HANDLE;
    mTextureSet = VK_NULL_HANDLE;
    mUniformsDirty = false;
    mTexturesDirty = false;
    mUniformsUsed = false;
    mTexturesUsed = false;
    mIndexBound = false;
    mDebugDepth = 0;
}

void VulkanCommandList::bindPipeline(PipelineHandle handle)
{
    PipelineRecord* pipeline = mDevice.mPipelines.get(handle, "pipeline");
    if (!pipeline)
        return;
    if (pipeline->colourFormats != mColourFormats ||
        pipeline->depthFormat != mDepthFormat) {
        log::error("rhi(vulkan): pipeline %u formats do not match active pass",
                   handle.id);
        return;
    }
    vkCmdBindPipeline(mCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      pipeline->pipeline);
    mPipeline = handle;
}

void VulkanCommandList::setViewport(const Viewport& viewport)
{
    if (viewport.width <= 0.0f || viewport.height <= 0.0f ||
        viewport.minDepth < 0.0f || viewport.maxDepth > 1.0f ||
        viewport.minDepth > viewport.maxDepth) {
        log::error("rhi(vulkan): invalid viewport");
        return;
    }
    VkViewport vkViewport{};
    vkViewport.x = viewport.x;
    vkViewport.y = viewport.y + viewport.height;
    vkViewport.width = viewport.width;
    vkViewport.height = -viewport.height;
    vkViewport.minDepth = viewport.minDepth;
    vkViewport.maxDepth = viewport.maxDepth;
    vkCmdSetViewport(mCommandBuffer, 0, 1, &vkViewport);
}

void VulkanCommandList::setScissor(const Rect& rect)
{
    const int64_t left = std::clamp<int64_t>(rect.x, 0, mExtent.width);
    const int64_t right =
        std::clamp<int64_t>(int64_t(rect.x) + rect.width, 0, mExtent.width);
    const int64_t bottom = std::clamp<int64_t>(rect.y, 0, mExtent.height);
    const int64_t topFromBottom =
        std::clamp<int64_t>(int64_t(rect.y) + rect.height, 0, mExtent.height);
    VkRect2D scissor{};
    scissor.offset.x = static_cast<int32_t>(left);
    scissor.offset.y =
        static_cast<int32_t>(int64_t(mExtent.height) - topFromBottom);
    scissor.extent.width =
        static_cast<uint32_t>(std::max<int64_t>(0, right - left));
    scissor.extent.height =
        static_cast<uint32_t>(std::max<int64_t>(0, topFromBottom - bottom));
    vkCmdSetScissor(mCommandBuffer, 0, 1, &scissor);
}

void VulkanCommandList::bindVertexBuffer(uint32_t binding, BufferHandle handle,
                                         uint64_t offset)
{
    BufferRecord* buffer = mDevice.mBuffers.get(handle, "vertex buffer");
    if (!buffer)
        return;
    if (!any(buffer->usage, BufferUsage::Vertex) || offset >= buffer->size) {
        log::error("rhi(vulkan): invalid vertex-buffer bind");
        return;
    }
    const VkBuffer vkBuffer = mDevice.bufferForFrame(*buffer);
    const VkDeviceSize vkOffset = offset;
    if (vkBuffer)
        vkCmdBindVertexBuffers(mCommandBuffer, binding, 1, &vkBuffer,
                               &vkOffset);
}

void VulkanCommandList::bindIndexBuffer(BufferHandle handle, uint64_t offset,
                                        IndexType type)
{
    BufferRecord* buffer = mDevice.mBuffers.get(handle, "index buffer");
    if (!buffer)
        return;
    const uint64_t alignment = type == IndexType::UInt16 ? 2u : 4u;
    if (!any(buffer->usage, BufferUsage::Index) || offset >= buffer->size ||
        offset % alignment != 0) {
        log::error("rhi(vulkan): invalid index-buffer bind");
        return;
    }
    const VkBuffer vkBuffer = mDevice.bufferForFrame(*buffer);
    if (vkBuffer) {
        vkCmdBindIndexBuffer(mCommandBuffer, vkBuffer, offset,
                             type == IndexType::UInt16 ? VK_INDEX_TYPE_UINT16
                                                       : VK_INDEX_TYPE_UINT32);
        mIndexBound = true;
    }
}

void VulkanCommandList::bindUniformBuffer(uint32_t slot, BufferHandle handle,
                                          uint64_t offset, uint64_t size)
{
    if (slot >= kUniformBindingCount) {
        log::error("rhi(vulkan): uniform slot %u exceeds ABI limit %u", slot,
                   kUniformBindingCount);
        return;
    }
    BufferRecord* buffer = mDevice.mBuffers.get(handle, "uniform buffer");
    if (!buffer || !any(buffer->usage, BufferUsage::Uniform)) {
        log::error(
            "rhi(vulkan): buffer for uniform slot %u lacks Uniform usage",
            slot);
        return;
    }
    if (size == 0 && offset < buffer->size)
        size = buffer->size - offset;
    if (offset % mDevice.mCaps.uniformBufferOffsetAlignment != 0 ||
        offset > buffer->size || size == 0 || size > buffer->size - offset ||
        size > mDevice.mCaps.maxUniformBufferRange) {
        log::error("rhi(vulkan): invalid range for uniform slot %u", slot);
        return;
    }
    const UniformBinding next{handle, offset, size};
    const UniformBinding& current = mUniforms[slot];
    if (mUniformsUsed && current.handle.id == next.handle.id &&
        current.handle.gen == next.handle.gen &&
        current.offset == next.offset && current.size == next.size)
        return;
    mUniforms[slot] = next;
    mUniformsUsed = true;
    mUniformsDirty = true;
}

void VulkanCommandList::bindTexture(uint32_t slot, TextureHandle texture,
                                    SamplerHandle sampler)
{
    if (slot >= kTextureBindingCount) {
        log::error("rhi(vulkan): texture slot %u exceeds ABI limit %u", slot,
                   kTextureBindingCount);
        return;
    }
    TextureRecord* textureRecord = mDevice.mTextures.get(texture, "texture");
    SamplerRecord* samplerRecord = mDevice.mSamplers.get(sampler, "sampler");
    if (!textureRecord || !samplerRecord)
        return;
    if (!any(textureRecord->usage, TextureUsage::Sampled)) {
        log::error("rhi(vulkan): texture %u lacks Sampled usage", texture.id);
        return;
    }
    const TextureBinding next{texture, sampler};
    const TextureBinding& current = mTextures[slot];
    if (mTexturesUsed && current.texture.id == next.texture.id &&
        current.texture.gen == next.texture.gen &&
        current.sampler.id == next.sampler.id &&
        current.sampler.gen == next.sampler.gen)
        return;
    mTextures[slot] = next;
    mTexturesUsed = true;
    mTexturesDirty = true;
}

bool VulkanCommandList::flushDescriptors()
{
    if (mUniformsUsed && (mUniformsDirty || mUniformSet == VK_NULL_HANDLE)) {
        VkDescriptorSet set = VK_NULL_HANDLE;
        if (!mDevice.allocateDescriptorSet(mDevice.mUniformSetLayout, set))
            return false;
        std::array<VkDescriptorBufferInfo, kUniformBindingCount> infos{};
        std::array<VkWriteDescriptorSet, kUniformBindingCount> writes{};
        uint32_t writeCount = 0;
        for (uint32_t slot = 0; slot < kUniformBindingCount; ++slot) {
            if (!mUniforms[slot].handle.valid())
                continue;
            BufferRecord* buffer =
                mDevice.mBuffers.get(mUniforms[slot].handle, "uniform buffer");
            if (!buffer)
                return false;
            const VkBuffer vkBuffer = mDevice.bufferForFrame(*buffer);
            if (!vkBuffer)
                return false;
            infos[writeCount] = {vkBuffer, mUniforms[slot].offset,
                                 mUniforms[slot].size};
            writes[writeCount].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[writeCount].dstSet = set;
            writes[writeCount].dstBinding = slot;
            writes[writeCount].descriptorCount = 1;
            writes[writeCount].descriptorType =
                VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            writes[writeCount].pBufferInfo = &infos[writeCount];
            ++writeCount;
        }
        vkUpdateDescriptorSets(mDevice.mContext.device.device, writeCount,
                               writes.data(), 0, nullptr);
        vkCmdBindDescriptorSets(mCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                mDevice.mPipelineLayout, 0, 1, &set, 0,
                                nullptr);
        mUniformSet = set;
        mUniformsDirty = false;
    }
    if (mTexturesUsed && (mTexturesDirty || mTextureSet == VK_NULL_HANDLE)) {
        VkDescriptorSet set = VK_NULL_HANDLE;
        if (!mDevice.allocateDescriptorSet(mDevice.mTextureSetLayout, set))
            return false;
        std::array<VkDescriptorImageInfo, kTextureBindingCount> infos{};
        std::array<VkWriteDescriptorSet, kTextureBindingCount> writes{};
        uint32_t writeCount = 0;
        for (uint32_t slot = 0; slot < kTextureBindingCount; ++slot) {
            if (!mTextures[slot].texture.valid())
                continue;
            TextureRecord* texture =
                mDevice.mTextures.get(mTextures[slot].texture, "texture");
            SamplerRecord* sampler =
                mDevice.mSamplers.get(mTextures[slot].sampler, "sampler");
            if (!texture || !sampler)
                return false;
            const VkImageLayout expected =
                (texture->aspect & VK_IMAGE_ASPECT_DEPTH_BIT)
                    ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
                    : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            if (texture->layout != expected) {
                log::error("rhi(vulkan): texture %u is bound for sampling in "
                           "layout %d",
                           mTextures[slot].texture.id,
                           static_cast<int>(texture->layout));
                return false;
            }
            infos[writeCount] = {sampler->sampler, texture->view, expected};
            writes[writeCount].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[writeCount].dstSet = set;
            writes[writeCount].dstBinding = slot;
            writes[writeCount].descriptorCount = 1;
            writes[writeCount].descriptorType =
                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[writeCount].pImageInfo = &infos[writeCount];
            ++writeCount;
        }
        vkUpdateDescriptorSets(mDevice.mContext.device.device, writeCount,
                               writes.data(), 0, nullptr);
        vkCmdBindDescriptorSets(mCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                mDevice.mPipelineLayout, 1, 1, &set, 0,
                                nullptr);
        mTextureSet = set;
        mTexturesDirty = false;
    }
    return true;
}

bool VulkanCommandList::prepareDraw()
{
    if (!mPipeline.valid()) {
        log::error("rhi(vulkan): draw without a pipeline");
        return false;
    }
    return flushDescriptors();
}

void VulkanCommandList::pushConstants(const void* data, uint32_t size)
{
    if (size == 0)
        return;
    if (!data || size > kPushConstantBytes || (size & 3u) != 0) {
        log::error("rhi(vulkan): invalid push constant payload of %u bytes",
                   size);
        return;
    }
    vkCmdPushConstants(mCommandBuffer, mDevice.mPipelineLayout,
                       VK_SHADER_STAGE_VERTEX_BIT |
                           VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, size, data);
}

void VulkanCommandList::draw(uint32_t vertexCount, uint32_t instanceCount,
                             uint32_t firstVertex, uint32_t firstInstance)
{
    if (vertexCount == 0 || instanceCount == 0 || !prepareDraw())
        return;
    vkCmdDraw(mCommandBuffer, vertexCount, instanceCount, firstVertex,
              firstInstance);
}

void VulkanCommandList::drawIndexed(uint32_t indexCount, uint32_t instanceCount,
                                    uint32_t firstIndex, int32_t vertexOffset,
                                    uint32_t firstInstance)
{
    if (!mIndexBound) {
        log::error("rhi(vulkan): drawIndexed without an index buffer");
        return;
    }
    if (indexCount == 0 || instanceCount == 0 || !prepareDraw())
        return;
    vkCmdDrawIndexed(mCommandBuffer, indexCount, instanceCount, firstIndex,
                     vertexOffset, firstInstance);
}

void VulkanCommandList::pushDebugGroup(const char* name)
{
    if (!name) {
        log::error("rhi(vulkan): null debug group name");
        return;
    }
    if (mDevice.mContext.beginLabel) {
        VkDebugUtilsLabelEXT label{};
        label.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
        label.pLabelName = name;
        label.color[0] = 0.45f;
        label.color[1] = 0.12f;
        label.color[2] = 0.65f;
        label.color[3] = 1.0f;
        mDevice.mContext.beginLabel(mCommandBuffer, &label);
    }
    ++mDebugDepth;
}

void VulkanCommandList::popDebugGroup()
{
    if (mDebugDepth == 0) {
        log::error("rhi(vulkan): popDebugGroup without a matching push");
        return;
    }
    if (mDevice.mContext.endLabel)
        mDevice.mContext.endLabel(mCommandBuffer);
    --mDebugDepth;
}

} // namespace eng::rhi::vulkan
