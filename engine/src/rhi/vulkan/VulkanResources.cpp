#include "VulkanInternal.h"

#include <algorithm>
#include <cstring>
#include <limits>

namespace eng::rhi::vulkan {

namespace {

VmaAllocationCreateInfo hostAllocationInfo()
{
    VmaAllocationCreateInfo info{};
    info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                 VMA_ALLOCATION_CREATE_MAPPED_BIT;
    info.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
    return info;
}

VmaAllocationCreateInfo hostReadbackAllocationInfo()
{
    VmaAllocationCreateInfo info{};
    info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT |
                 VMA_ALLOCATION_CREATE_MAPPED_BIT;
    info.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
    return info;
}

bool rangeFits(uint64_t capacity, uint64_t offset, uint64_t size)
{
    return offset <= capacity && size <= capacity - offset;
}

uint32_t mipDimension(uint32_t value, uint32_t mip)
{
    return std::max(1u, value >> mip);
}

} // namespace

BufferHandle VulkanDevice::createBuffer(const BufferDesc& desc)
{
    if (desc.size == 0) {
        log::error("rhi(vulkan): zero-sized buffer '%s'",
                   desc.debugName.c_str());
        return {};
    }
    BufferRecord record;
    record.size = desc.size;
    record.usage = desc.usage;
    record.dynamic = any(desc.usage, BufferUsage::Dynamic);

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = desc.size;
    bufferInfo.usage = toVkBufferUsage(desc.usage);
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    const uint32_t backingCount = record.dynamic ? kFramesInFlight : 1;
    for (uint32_t i = 0; i < backingCount; ++i) {
        VmaAllocationCreateInfo allocationInfo{};
        if (record.dynamic) {
            allocationInfo = hostAllocationInfo();
        }
        else {
            allocationInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        }
        VmaAllocationInfo resultInfo{};
        const VkResult result =
            vmaCreateBuffer(mContext.allocator, &bufferInfo, &allocationInfo,
                            &record.backing[i].buffer,
                            &record.backing[i].allocation, &resultInfo);
        if (result != VK_SUCCESS) {
            for (uint32_t j = 0; j < i; ++j)
                vmaDestroyBuffer(mContext.allocator, record.backing[j].buffer,
                                 record.backing[j].allocation);
            apiError("vmaCreateBuffer", result);
            return {};
        }
        record.backing[i].mapped = resultInfo.pMappedData;
        if (record.dynamic && !record.backing[i].mapped) {
            for (uint32_t j = 0; j <= i; ++j)
                vmaDestroyBuffer(mContext.allocator, record.backing[j].buffer,
                                 record.backing[j].allocation);
            log::error("rhi(vulkan): VMA did not map dynamic buffer '%s'",
                       desc.debugName.c_str());
            return {};
        }
        const std::string name =
            record.dynamic ? desc.debugName + ".frame" + std::to_string(i)
                           : desc.debugName;
        vmaSetAllocationName(mContext.allocator, record.backing[i].allocation,
                             name.c_str());
        setDebugName(VK_OBJECT_TYPE_BUFFER,
                     vkHandleValue(record.backing[i].buffer), name);
    }

    if (record.dynamic) {
        record.shadow.resize(static_cast<size_t>(desc.size));
        if (desc.initialData)
            std::memcpy(record.shadow.data(), desc.initialData,
                        static_cast<size_t>(desc.size));
        record.version = 1;
        for (uint32_t i = 0; i < kFramesInFlight; ++i) {
            std::memcpy(record.backing[i].mapped, record.shadow.data(),
                        record.shadow.size());
            const VkResult flushResult = vmaFlushAllocation(
                mContext.allocator, record.backing[i].allocation, 0, desc.size);
            if (flushResult != VK_SUCCESS) {
                apiError("vmaFlushAllocation(dynamic initialization)",
                         flushResult);
                for (uint32_t j = 0; j < kFramesInFlight; ++j)
                    vmaDestroyBuffer(mContext.allocator,
                                     record.backing[j].buffer,
                                     record.backing[j].allocation);
                return {};
            }
            record.frameVersion[i] = record.version;
        }
    }

    BufferHandle handle = mBuffers.create(std::move(record));
    if (!any(desc.usage, BufferUsage::Dynamic) && desc.initialData) {
        BufferRecord* created = mBuffers.get(handle, "buffer");
        if (!created ||
            !uploadToBuffer(*created, desc.initialData, desc.size, 0)) {
            destroyBuffer(handle);
            return {};
        }
    }
    return handle;
}

bool VulkanDevice::uploadToBuffer(BufferRecord& destination, const void* data,
                                  uint64_t size, uint64_t offset)
{
    VkBufferCreateInfo stagingInfo{};
    stagingInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    stagingInfo.size = size;
    stagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    stagingInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VmaAllocationCreateInfo allocationInfo = hostAllocationInfo();
    VkBuffer staging = VK_NULL_HANDLE;
    VmaAllocation stagingAllocation = VK_NULL_HANDLE;
    VmaAllocationInfo mapped{};
    VkResult result =
        vmaCreateBuffer(mContext.allocator, &stagingInfo, &allocationInfo,
                        &staging, &stagingAllocation, &mapped);
    if (result != VK_SUCCESS) {
        apiError("vmaCreateBuffer(staging)", result);
        return false;
    }
    std::memcpy(mapped.pMappedData, data, static_cast<size_t>(size));
    result = vmaFlushAllocation(mContext.allocator, stagingAllocation, 0, size);
    if (result != VK_SUCCESS) {
        apiError("vmaFlushAllocation(buffer staging)", result);
        vmaDestroyBuffer(mContext.allocator, staging, stagingAllocation);
        return false;
    }

    const bool submitted = immediateSubmit([&](VkCommandBuffer commandBuffer) {
        VkBufferCopy copy{};
        copy.dstOffset = offset;
        copy.size = size;
        vkCmdCopyBuffer(commandBuffer, staging, destination.backing[0].buffer,
                        1, &copy);

        VkBufferMemoryBarrier2 barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT |
                               VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT |
                               VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        barrier.dstAccessMask =
            VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_2_INDEX_READ_BIT |
            VK_ACCESS_2_UNIFORM_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.buffer = destination.backing[0].buffer;
        barrier.offset = offset;
        barrier.size = size;
        VkDependencyInfo dependency{};
        dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dependency.bufferMemoryBarrierCount = 1;
        dependency.pBufferMemoryBarriers = &barrier;
        vkCmdPipelineBarrier2(commandBuffer, &dependency);
    });
    vmaDestroyBuffer(mContext.allocator, staging, stagingAllocation);
    return submitted;
}

void VulkanDevice::destroyBuffer(BufferHandle handle)
{
    auto record = mBuffers.remove(handle, "buffer");
    if (!record)
        return;
    const VmaAllocator allocator = mContext.allocator;
    const bool dynamic = record->dynamic;
    const auto backing = record->backing;
    scheduleDelete([allocator, dynamic, backing] {
        const uint32_t count = dynamic ? kFramesInFlight : 1;
        for (uint32_t i = 0; i < count; ++i)
            vmaDestroyBuffer(allocator, backing[i].buffer,
                             backing[i].allocation);
    });
}

void VulkanDevice::updateBuffer(BufferHandle handle, const void* data,
                                uint64_t size, uint64_t offset)
{
    BufferRecord* record = mBuffers.get(handle, "buffer");
    if (!record)
        return;
    if (!data || size == 0 || !rangeFits(record->size, offset, size)) {
        log::error("rhi(vulkan): invalid update of buffer %u (%llu bytes at "
                   "%llu, capacity %llu)",
                   handle.id, static_cast<unsigned long long>(size),
                   static_cast<unsigned long long>(offset),
                   static_cast<unsigned long long>(record->size));
        return;
    }
    if (record->dynamic) {
        std::memcpy(record->shadow.data() + offset, data,
                    static_cast<size_t>(size));
        ++record->version;
        if (record->version == 0) {
            record->version = 1;
            record->frameVersion.fill(0);
        }
        if (mInFrame) {
            // The shadow is authoritative. A frame backing can be more than
            // one version stale, so publishing a partial write would otherwise
            // preserve unrelated bytes from an older frame.
            std::memcpy(record->backing[mFrameIndex].mapped,
                        record->shadow.data(), record->shadow.size());
            const VkResult result = vmaFlushAllocation(
                mContext.allocator, record->backing[mFrameIndex].allocation, 0,
                record->size);
            if (result != VK_SUCCESS) {
                apiError("vmaFlushAllocation(dynamic update)", result);
                return;
            }
            record->frameVersion[mFrameIndex] = record->version;
        }
        return;
    }
    if (mInFrame) {
        log::error(
            "rhi(vulkan): static updateBuffer is only valid outside a frame");
        return;
    }
    if (!waitForIdle())
        return;
    uploadToBuffer(*record, data, size, offset);
}

VkBuffer VulkanDevice::bufferForFrame(BufferRecord& buffer)
{
    if (!buffer.dynamic)
        return buffer.backing[0].buffer;
    if (!mInFrame) {
        log::error("rhi(vulkan): dynamic buffer bound outside a frame");
        return VK_NULL_HANDLE;
    }
    if (buffer.frameVersion[mFrameIndex] != buffer.version) {
        std::memcpy(buffer.backing[mFrameIndex].mapped, buffer.shadow.data(),
                    buffer.shadow.size());
        const VkResult result = vmaFlushAllocation(
            mContext.allocator, buffer.backing[mFrameIndex].allocation, 0,
            buffer.size);
        if (result != VK_SUCCESS) {
            apiError("vmaFlushAllocation(dynamic buffer)", result);
            return VK_NULL_HANDLE;
        }
        buffer.frameVersion[mFrameIndex] = buffer.version;
    }
    return buffer.backing[mFrameIndex].buffer;
}

TextureHandle VulkanDevice::createTexture(const TextureDesc& desc)
{
    if (desc.width == 0 || desc.height == 0 || desc.depth == 0 ||
        desc.mipLevels == 0 || desc.width > mCaps.maxTextureSize ||
        desc.height > mCaps.maxTextureSize) {
        log::error("rhi(vulkan): invalid dimensions for texture '%s'",
                   desc.debugName.c_str());
        return {};
    }
    if (any(desc.usage, TextureUsage::RenderTarget) &&
        any(desc.usage, TextureUsage::DepthStencil)) {
        log::error("rhi(vulkan): texture '%s' cannot be both colour and depth",
                   desc.debugName.c_str());
        return {};
    }
    const VkFormat format = toVkFormat(desc.format);
    if (format == VK_FORMAT_UNDEFINED || bytesPerTexel(desc.format) == 0) {
        log::error("rhi(vulkan): texture '%s' has an unknown format",
                   desc.debugName.c_str());
        return {};
    }
    const bool depthFormat =
        (aspectForFormat(desc.format) & VK_IMAGE_ASPECT_DEPTH_BIT) != 0;
    if (depthFormat != any(desc.usage, TextureUsage::DepthStencil)) {
        log::error("rhi(vulkan): texture '%s' format and depth usage disagree",
                   desc.debugName.c_str());
        return {};
    }
    if (depthFormat && any(desc.usage, TextureUsage::Readback)) {
        log::error("rhi(vulkan): depth texture readback is not supported");
        return {};
    }

    VkFormatProperties formatProperties{};
    vkGetPhysicalDeviceFormatProperties(mContext.physical.physical_device,
                                        format, &formatProperties);
    VkFormatFeatureFlags required = VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
    if (any(desc.usage, TextureUsage::Sampled))
        required |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
    if (any(desc.usage, TextureUsage::RenderTarget))
        required |= VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
    if (any(desc.usage, TextureUsage::DepthStencil))
        required |= VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;
    if (any(desc.usage, TextureUsage::Readback))
        required |= VK_FORMAT_FEATURE_TRANSFER_SRC_BIT;
    if ((formatProperties.optimalTilingFeatures & required) != required) {
        log::error(
            "rhi(vulkan): texture '%s' format does not support requested usage",
            desc.debugName.c_str());
        return {};
    }

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = desc.depth > 1 ? VK_IMAGE_TYPE_3D : VK_IMAGE_TYPE_2D;
    imageInfo.format = format;
    imageInfo.extent = {desc.width, desc.height, desc.depth};
    imageInfo.mipLevels = desc.mipLevels;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = toVkImageUsage(desc.usage);
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VmaAllocationCreateInfo allocationInfo{};
    allocationInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

    TextureRecord record;
    record.width = desc.width;
    record.height = desc.height;
    record.depth = desc.depth;
    record.mipLevels = desc.mipLevels;
    record.format = desc.format;
    record.usage = desc.usage;
    record.aspect = aspectForFormat(desc.format);
    VkResult result =
        vmaCreateImage(mContext.allocator, &imageInfo, &allocationInfo,
                       &record.image, &record.allocation, nullptr);
    if (result != VK_SUCCESS) {
        apiError("vmaCreateImage", result);
        return {};
    }
    vmaSetAllocationName(mContext.allocator, record.allocation,
                         desc.debugName.c_str());

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = record.image;
    viewInfo.viewType =
        desc.depth > 1 ? VK_IMAGE_VIEW_TYPE_3D : VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = record.aspect;
    viewInfo.subresourceRange.levelCount = desc.mipLevels;
    viewInfo.subresourceRange.layerCount = 1;
    result = vkCreateImageView(mContext.device.device, &viewInfo, nullptr,
                               &record.view);
    if (result != VK_SUCCESS) {
        vmaDestroyImage(mContext.allocator, record.image, record.allocation);
        apiError("vkCreateImageView", result);
        return {};
    }
    setDebugName(VK_OBJECT_TYPE_IMAGE, vkHandleValue(record.image),
                 desc.debugName);
    setDebugName(VK_OBJECT_TYPE_IMAGE_VIEW, vkHandleValue(record.view),
                 desc.debugName + ".view");

    TextureHandle handle = mTextures.create(std::move(record));
    TextureRecord* created = mTextures.get(handle, "texture");
    const uint64_t initialSize = uint64_t(desc.width) * desc.height *
                                 desc.depth * bytesPerTexel(desc.format);
    bool initialized = false;
    if (created && desc.initialData) {
        initialized =
            uploadToTexture(*created, desc.initialData, initialSize, 0);
    }
    else if (created) {
        initialized = immediateSubmit([&](VkCommandBuffer commandBuffer) {
            VkImageLayout initialLayout = defaultTextureLayout(created->usage);
            if (any(created->usage, TextureUsage::DepthStencil))
                initialLayout =
                    VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            else if (any(created->usage, TextureUsage::RenderTarget))
                initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            if (any(created->usage, TextureUsage::Sampled) &&
                created->aspect == VK_IMAGE_ASPECT_COLOR_BIT &&
                !any(created->usage, TextureUsage::RenderTarget)) {
                transitionTexture(commandBuffer, *created,
                                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
                const VkClearColorValue clear{};
                VkImageSubresourceRange range{};
                range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                range.levelCount = created->mipLevels;
                range.layerCount = 1;
                vkCmdClearColorImage(commandBuffer, created->image,
                                     VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                     &clear, 1, &range);
            }
            transitionTexture(commandBuffer, *created, initialLayout);
        });
    }
    if (!initialized) {
        destroyTexture(handle);
        return {};
    }
    return handle;
}

bool VulkanDevice::uploadToTexture(TextureRecord& texture, const void* data,
                                   uint64_t size, uint32_t mipLevel)
{
    if ((texture.aspect & VK_IMAGE_ASPECT_STENCIL_BIT) != 0) {
        log::error("rhi(vulkan): packed depth/stencil texture uploads are not "
                   "supported");
        return false;
    }
    const uint64_t requiredSize =
        uint64_t(mipDimension(texture.width, mipLevel)) *
        mipDimension(texture.height, mipLevel) *
        mipDimension(texture.depth, mipLevel) * bytesPerTexel(texture.format);
    if (!data || size < requiredSize) {
        log::error(
            "rhi(vulkan): texture upload is %llu bytes, expected at least %llu",
            static_cast<unsigned long long>(size),
            static_cast<unsigned long long>(requiredSize));
        return false;
    }

    VkBufferCreateInfo stagingInfo{};
    stagingInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    stagingInfo.size = requiredSize;
    stagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    stagingInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VmaAllocationCreateInfo allocationInfo = hostAllocationInfo();
    VkBuffer staging = VK_NULL_HANDLE;
    VmaAllocation stagingAllocation = VK_NULL_HANDLE;
    VmaAllocationInfo mapped{};
    VkResult result =
        vmaCreateBuffer(mContext.allocator, &stagingInfo, &allocationInfo,
                        &staging, &stagingAllocation, &mapped);
    if (result != VK_SUCCESS) {
        apiError("vmaCreateBuffer(texture staging)", result);
        return false;
    }
    std::memcpy(mapped.pMappedData, data, static_cast<size_t>(requiredSize));
    result = vmaFlushAllocation(mContext.allocator, stagingAllocation, 0,
                                requiredSize);
    if (result != VK_SUCCESS) {
        apiError("vmaFlushAllocation(texture staging)", result);
        vmaDestroyBuffer(mContext.allocator, staging, stagingAllocation);
        return false;
    }

    const VkImageLayout finalLayout = defaultTextureLayout(texture.usage);
    const bool submitted = immediateSubmit([&](VkCommandBuffer commandBuffer) {
        transitionTexture(commandBuffer, texture,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        VkBufferImageCopy copy{};
        copy.imageSubresource.aspectMask = texture.aspect;
        copy.imageSubresource.mipLevel = mipLevel;
        copy.imageSubresource.layerCount = 1;
        copy.imageExtent = {mipDimension(texture.width, mipLevel),
                            mipDimension(texture.height, mipLevel),
                            mipDimension(texture.depth, mipLevel)};
        vkCmdCopyBufferToImage(commandBuffer, staging, texture.image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
        transitionTexture(commandBuffer, texture, finalLayout);
    });
    vmaDestroyBuffer(mContext.allocator, staging, stagingAllocation);
    return submitted;
}

void VulkanDevice::destroyTexture(TextureHandle handle)
{
    auto record = mTextures.remove(handle, "texture");
    if (!record)
        return;
    const VkDevice device = mContext.device.device;
    const VmaAllocator allocator = mContext.allocator;
    const VkImage image = record->image;
    const VmaAllocation allocation = record->allocation;
    const VkImageView view = record->view;
    scheduleDelete([device, allocator, image, allocation, view] {
        vkDestroyImageView(device, view, nullptr);
        vmaDestroyImage(allocator, image, allocation);
    });
}

void VulkanDevice::updateTexture(TextureHandle handle, const void* data,
                                 uint64_t size, uint32_t mipLevel)
{
    TextureRecord* record = mTextures.get(handle, "texture");
    if (!record)
        return;
    if (mInFrame) {
        log::error("rhi(vulkan): updateTexture is only valid outside a frame");
        return;
    }
    if (mipLevel >= record->mipLevels) {
        log::error("rhi(vulkan): texture %u has no mip %u", handle.id,
                   mipLevel);
        return;
    }
    if (!waitForIdle())
        return;
    uploadToTexture(*record, data, size, mipLevel);
}

void VulkanDevice::readTexture(TextureHandle handle, void* destination,
                               uint64_t size, uint32_t mipLevel)
{
    TextureRecord* texture = mTextures.get(handle, "texture");
    if (!texture)
        return;
    if (mInFrame) {
        log::error("rhi(vulkan): readTexture is only valid outside a frame");
        return;
    }
    if (!any(texture->usage, TextureUsage::Readback)) {
        log::error("rhi(vulkan): texture %u lacks Readback usage", handle.id);
        return;
    }
    if (!destination || mipLevel >= texture->mipLevels ||
        texture->aspect != VK_IMAGE_ASPECT_COLOR_BIT) {
        log::error("rhi(vulkan): invalid texture readback for texture %u",
                   handle.id);
        return;
    }
    const uint64_t requiredSize =
        uint64_t(mipDimension(texture->width, mipLevel)) *
        mipDimension(texture->height, mipLevel) *
        mipDimension(texture->depth, mipLevel) * bytesPerTexel(texture->format);
    if (size < requiredSize) {
        log::error("rhi(vulkan): texture readback destination is %llu bytes, "
                   "expected at least %llu",
                   static_cast<unsigned long long>(size),
                   static_cast<unsigned long long>(requiredSize));
        return;
    }
    if (!waitForIdle())
        return;

    VkBufferCreateInfo stagingInfo{};
    stagingInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    stagingInfo.size = requiredSize;
    stagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    stagingInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VmaAllocationCreateInfo allocationInfo = hostReadbackAllocationInfo();
    VkBuffer staging = VK_NULL_HANDLE;
    VmaAllocation stagingAllocation = VK_NULL_HANDLE;
    VmaAllocationInfo mapped{};
    VkResult result =
        vmaCreateBuffer(mContext.allocator, &stagingInfo, &allocationInfo,
                        &staging, &stagingAllocation, &mapped);
    if (result != VK_SUCCESS) {
        apiError("vmaCreateBuffer(readback staging)", result);
        return;
    }

    const VkImageLayout priorLayout = texture->layout;
    const bool submitted = immediateSubmit([&](VkCommandBuffer commandBuffer) {
        transitionTexture(commandBuffer, *texture,
                          VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        VkBufferImageCopy copy{};
        copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copy.imageSubresource.mipLevel = mipLevel;
        copy.imageSubresource.layerCount = 1;
        copy.imageExtent = {mipDimension(texture->width, mipLevel),
                            mipDimension(texture->height, mipLevel),
                            mipDimension(texture->depth, mipLevel)};
        vkCmdCopyImageToBuffer(commandBuffer, texture->image,
                               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging, 1,
                               &copy);
        transitionTexture(commandBuffer, *texture, priorLayout);
    });
    if (submitted) {
        result = vmaInvalidateAllocation(mContext.allocator, stagingAllocation,
                                         0, requiredSize);
        if (result != VK_SUCCESS) {
            apiError("vmaInvalidateAllocation(texture readback)", result);
        }
        else {
            std::memcpy(destination, mapped.pMappedData,
                        static_cast<size_t>(requiredSize));
        }
    }
    vmaDestroyBuffer(mContext.allocator, staging, stagingAllocation);
}

SamplerHandle VulkanDevice::createSampler(const SamplerDesc& desc)
{
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(mContext.physical.physical_device,
                                  &properties);
    const bool anisotropy =
        mContext.samplerAnisotropy && desc.maxAnisotropy > 1.0f;
    VkSamplerCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    createInfo.magFilter = toVkFilter(desc.magFilter);
    createInfo.minFilter = toVkFilter(desc.minFilter);
    createInfo.mipmapMode = desc.minFilter == FilterMode::Linear
                                ? VK_SAMPLER_MIPMAP_MODE_LINEAR
                                : VK_SAMPLER_MIPMAP_MODE_NEAREST;
    createInfo.addressModeU = toVkAddressMode(desc.addressU);
    createInfo.addressModeV = toVkAddressMode(desc.addressV);
    createInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    createInfo.anisotropyEnable = anisotropy;
    createInfo.maxAnisotropy =
        anisotropy ? std::clamp(desc.maxAnisotropy, 1.0f,
                                properties.limits.maxSamplerAnisotropy)
                   : 1.0f;
    createInfo.minLod = 0.0f;
    createInfo.maxLod = VK_LOD_CLAMP_NONE;
    VkSampler sampler = VK_NULL_HANDLE;
    const VkResult result =
        vkCreateSampler(mContext.device.device, &createInfo, nullptr, &sampler);
    if (result != VK_SUCCESS) {
        apiError("vkCreateSampler", result);
        return {};
    }
    return mSamplers.create({sampler});
}

void VulkanDevice::destroySampler(SamplerHandle handle)
{
    auto record = mSamplers.remove(handle, "sampler");
    if (!record)
        return;
    const VkDevice device = mContext.device.device;
    const VkSampler sampler = record->sampler;
    scheduleDelete(
        [device, sampler] { vkDestroySampler(device, sampler, nullptr); });
}

} // namespace eng::rhi::vulkan
