#include "VulkanInternal.h"

namespace eng::rhi::vulkan {

VkFormat toVkFormat(Format format)
{
    switch (format) {
    case Format::Unknown:
        return VK_FORMAT_UNDEFINED;
    case Format::RGBA8Unorm:
        return VK_FORMAT_R8G8B8A8_UNORM;
    case Format::RGBA8Srgb:
        return VK_FORMAT_R8G8B8A8_SRGB;
    case Format::BGRA8Unorm:
        return VK_FORMAT_B8G8R8A8_UNORM;
    case Format::BGRA8Srgb:
        return VK_FORMAT_B8G8R8A8_SRGB;
    case Format::R8Unorm:
        return VK_FORMAT_R8_UNORM;
    case Format::RG16Float:
        return VK_FORMAT_R16G16_SFLOAT;
    case Format::RGBA16Float:
        return VK_FORMAT_R16G16B16A16_SFLOAT;
    case Format::Depth24Stencil8:
        return VK_FORMAT_D24_UNORM_S8_UINT;
    case Format::Depth32Float:
        return VK_FORMAT_D32_SFLOAT;
    }
    return VK_FORMAT_UNDEFINED;
}

Format fromVkFormat(VkFormat format)
{
    switch (format) {
    case VK_FORMAT_R8G8B8A8_UNORM:
        return Format::RGBA8Unorm;
    case VK_FORMAT_R8G8B8A8_SRGB:
        return Format::RGBA8Srgb;
    case VK_FORMAT_B8G8R8A8_UNORM:
        return Format::BGRA8Unorm;
    case VK_FORMAT_B8G8R8A8_SRGB:
        return Format::BGRA8Srgb;
    case VK_FORMAT_R8_UNORM:
        return Format::R8Unorm;
    case VK_FORMAT_R16G16_SFLOAT:
        return Format::RG16Float;
    case VK_FORMAT_R16G16B16A16_SFLOAT:
        return Format::RGBA16Float;
    case VK_FORMAT_D24_UNORM_S8_UINT:
        return Format::Depth24Stencil8;
    case VK_FORMAT_D32_SFLOAT:
        return Format::Depth32Float;
    default:
        return Format::Unknown;
    }
}

VkImageAspectFlags aspectForFormat(Format format)
{
    switch (format) {
    case Format::Depth24Stencil8:
        return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    case Format::Depth32Float:
        return VK_IMAGE_ASPECT_DEPTH_BIT;
    default:
        return VK_IMAGE_ASPECT_COLOR_BIT;
    }
}

uint32_t bytesPerTexel(Format format)
{
    switch (format) {
    case Format::R8Unorm:
        return 1;
    case Format::RG16Float:
        return 4;
    case Format::RGBA8Unorm:
    case Format::RGBA8Srgb:
    case Format::BGRA8Unorm:
    case Format::BGRA8Srgb:
    case Format::Depth24Stencil8:
    case Format::Depth32Float:
        return 4;
    case Format::RGBA16Float:
        return 8;
    case Format::Unknown:
        return 0;
    }
    return 0;
}

VkBufferUsageFlags toVkBufferUsage(BufferUsage usage)
{
    VkBufferUsageFlags flags = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    if (any(usage, BufferUsage::Vertex))
        flags |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    if (any(usage, BufferUsage::Index))
        flags |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    if (any(usage, BufferUsage::Uniform))
        flags |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    if (any(usage, BufferUsage::Storage))
        flags |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    return flags;
}

VkImageUsageFlags toVkImageUsage(TextureUsage usage)
{
    VkImageUsageFlags flags = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (any(usage, TextureUsage::Sampled))
        flags |= VK_IMAGE_USAGE_SAMPLED_BIT;
    if (any(usage, TextureUsage::RenderTarget))
        flags |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (any(usage, TextureUsage::DepthStencil))
        flags |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    if (any(usage, TextureUsage::Readback))
        flags |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    return flags;
}

VkShaderStageFlagBits toVkShaderStage(ShaderStage stage)
{
    switch (stage) {
    case ShaderStage::Vertex:
        return VK_SHADER_STAGE_VERTEX_BIT;
    case ShaderStage::Fragment:
        return VK_SHADER_STAGE_FRAGMENT_BIT;
    case ShaderStage::Compute:
        return VK_SHADER_STAGE_COMPUTE_BIT;
    }
    return VK_SHADER_STAGE_VERTEX_BIT;
}

VkPrimitiveTopology toVkTopology(PrimitiveTopology topology)
{
    switch (topology) {
    case PrimitiveTopology::TriangleList:
        return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    case PrimitiveTopology::TriangleStrip:
        return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    case PrimitiveTopology::LineList:
        return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    }
    return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
}

VkCullModeFlags toVkCullMode(CullMode mode)
{
    switch (mode) {
    case CullMode::None:
        return VK_CULL_MODE_NONE;
    case CullMode::Front:
        return VK_CULL_MODE_FRONT_BIT;
    case CullMode::Back:
        return VK_CULL_MODE_BACK_BIT;
    }
    return VK_CULL_MODE_NONE;
}

VkCompareOp toVkCompareOp(CompareOp op)
{
    switch (op) {
    case CompareOp::Never:
        return VK_COMPARE_OP_NEVER;
    case CompareOp::Less:
        return VK_COMPARE_OP_LESS;
    case CompareOp::Equal:
        return VK_COMPARE_OP_EQUAL;
    case CompareOp::LessEqual:
        return VK_COMPARE_OP_LESS_OR_EQUAL;
    case CompareOp::Greater:
        return VK_COMPARE_OP_GREATER;
    case CompareOp::NotEqual:
        return VK_COMPARE_OP_NOT_EQUAL;
    case CompareOp::GreaterEqual:
        return VK_COMPARE_OP_GREATER_OR_EQUAL;
    case CompareOp::Always:
        return VK_COMPARE_OP_ALWAYS;
    }
    return VK_COMPARE_OP_ALWAYS;
}

VkFilter toVkFilter(FilterMode mode)
{
    return mode == FilterMode::Linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
}

VkSamplerAddressMode toVkAddressMode(AddressMode mode)
{
    switch (mode) {
    case AddressMode::Repeat:
        return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    case AddressMode::ClampToEdge:
        return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    case AddressMode::MirrorRepeat:
        return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    }
    return VK_SAMPLER_ADDRESS_MODE_REPEAT;
}

VkFormat toVkVertexFormat(VertexFormat format)
{
    switch (format) {
    case VertexFormat::Float1:
        return VK_FORMAT_R32_SFLOAT;
    case VertexFormat::Float2:
        return VK_FORMAT_R32G32_SFLOAT;
    case VertexFormat::Float3:
        return VK_FORMAT_R32G32B32_SFLOAT;
    case VertexFormat::Float4:
        return VK_FORMAT_R32G32B32A32_SFLOAT;
    case VertexFormat::UByte4Unorm:
        return VK_FORMAT_R8G8B8A8_UNORM;
    }
    return VK_FORMAT_UNDEFINED;
}

VkPipelineColorBlendAttachmentState toVkBlendState(BlendMode mode)
{
    VkPipelineColorBlendAttachmentState result{};
    result.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                            VK_COLOR_COMPONENT_G_BIT |
                            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    result.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    result.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
    result.colorBlendOp = VK_BLEND_OP_ADD;
    result.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    result.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    result.alphaBlendOp = VK_BLEND_OP_ADD;

    switch (mode) {
    case BlendMode::Opaque:
        result.blendEnable = VK_FALSE;
        break;
    case BlendMode::AlphaBlend:
        result.blendEnable = VK_TRUE;
        result.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        result.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        result.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        result.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        break;
    case BlendMode::Additive:
        result.blendEnable = VK_TRUE;
        result.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        result.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
        result.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        result.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        break;
    case BlendMode::Modulate:
        result.blendEnable = VK_TRUE;
        result.srcColorBlendFactor = VK_BLEND_FACTOR_DST_COLOR;
        result.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
        result.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        result.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        break;
    }
    return result;
}

VkImageLayout defaultTextureLayout(TextureUsage usage)
{
    if (any(usage, TextureUsage::Sampled)) {
        return any(usage, TextureUsage::DepthStencil)
                   ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
                   : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }
    if (any(usage, TextureUsage::DepthStencil))
        return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    if (any(usage, TextureUsage::RenderTarget))
        return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    return VK_IMAGE_LAYOUT_GENERAL;
}

} // namespace eng::rhi::vulkan
