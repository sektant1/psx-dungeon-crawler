#include "VulkanInternal.h"

#include <algorithm>
#include <cstring>
#include <unordered_set>

namespace eng::rhi::vulkan {

bool VulkanDevice::initializeLayouts()
{
    std::array<VkDescriptorSetLayoutBinding, kUniformBindingCount>
        uniformBindings{};
    for (uint32_t i = 0; i < kUniformBindingCount; ++i) {
        uniformBindings[i].binding = i;
        uniformBindings[i].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        uniformBindings[i].descriptorCount = 1;
        uniformBindings[i].stageFlags =
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(uniformBindings.size());
    layoutInfo.pBindings = uniformBindings.data();
    VkResult result = vkCreateDescriptorSetLayout(
        mContext.device.device, &layoutInfo, nullptr, &mUniformSetLayout);
    if (result != VK_SUCCESS) {
        apiError("vkCreateDescriptorSetLayout(uniform ABI)", result);
        return false;
    }

    std::array<VkDescriptorSetLayoutBinding, kTextureBindingCount>
        textureBindings{};
    for (uint32_t i = 0; i < kTextureBindingCount; ++i) {
        textureBindings[i].binding = i;
        textureBindings[i].descriptorType =
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        textureBindings[i].descriptorCount = 1;
        textureBindings[i].stageFlags =
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    layoutInfo.bindingCount = static_cast<uint32_t>(textureBindings.size());
    layoutInfo.pBindings = textureBindings.data();
    result = vkCreateDescriptorSetLayout(mContext.device.device, &layoutInfo,
                                         nullptr, &mTextureSetLayout);
    if (result != VK_SUCCESS) {
        apiError("vkCreateDescriptorSetLayout(texture ABI)", result);
        return false;
    }

    const std::array<VkDescriptorSetLayout, 2> setLayouts{
        mUniformSetLayout, mTextureSetLayout};
    VkPushConstantRange pushRange{};
    pushRange.stageFlags =
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.offset = 0;
    pushRange.size = kPushConstantBytes;
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType =
        VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount =
        static_cast<uint32_t>(setLayouts.size());
    pipelineLayoutInfo.pSetLayouts = setLayouts.data();
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushRange;
    result = vkCreatePipelineLayout(mContext.device.device,
                                    &pipelineLayoutInfo, nullptr,
                                    &mPipelineLayout);
    if (result != VK_SUCCESS) {
        apiError("vkCreatePipelineLayout(fixed ABI v1)", result);
        return false;
    }
    setDebugName(VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT,
                 vkHandleValue(mUniformSetLayout), "rhi.uniform-set-v1");
    setDebugName(VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT,
                 vkHandleValue(mTextureSetLayout), "rhi.texture-set-v1");
    setDebugName(VK_OBJECT_TYPE_PIPELINE_LAYOUT,
                 vkHandleValue(mPipelineLayout), "rhi.pipeline-layout-v1");
    return true;
}

ShaderHandle VulkanDevice::createShader(const ShaderDesc& desc)
{
    if (desc.code.size() < 5 * sizeof(uint32_t) ||
        (desc.code.size() % sizeof(uint32_t)) != 0) {
        log::error("rhi(vulkan): shader '%s' is not word-aligned SPIR-V",
                   desc.debugName.c_str());
        return {};
    }
    std::vector<uint32_t> words(desc.code.size() / sizeof(uint32_t));
    std::memcpy(words.data(), desc.code.data(), desc.code.size());
    if (words.front() != 0x07230203u) {
        log::error("rhi(vulkan): shader '%s' has no SPIR-V magic number",
                   desc.debugName.c_str());
        return {};
    }
    if (desc.entryPoint.empty()) {
        log::error("rhi(vulkan): shader '%s' has an empty entry point",
                   desc.debugName.c_str());
        return {};
    }

    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = desc.code.size();
    createInfo.pCode = words.data();
    VkShaderModule module = VK_NULL_HANDLE;
    const VkResult result = vkCreateShaderModule(
        mContext.device.device, &createInfo, nullptr, &module);
    if (result != VK_SUCCESS) {
        apiError("vkCreateShaderModule", result);
        return {};
    }
    setDebugName(VK_OBJECT_TYPE_SHADER_MODULE, vkHandleValue(module),
                 desc.debugName);
    return mShaders.create({module, desc.stage, desc.entryPoint});
}

void VulkanDevice::destroyShader(ShaderHandle handle)
{
    auto record = mShaders.remove(handle, "shader");
    if (!record)
        return;
    const VkDevice device = mContext.device.device;
    const VkShaderModule module = record->module;
    scheduleDelete([device, module] {
        vkDestroyShaderModule(device, module, nullptr);
    });
}

PipelineHandle VulkanDevice::createPipeline(const PipelineDesc& desc)
{
    ShaderRecord* vertex = mShaders.get(desc.vertex, "vertex shader");
    ShaderRecord* fragment = mShaders.get(desc.fragment, "fragment shader");
    if (!vertex || !fragment)
        return {};
    if (vertex->stage != ShaderStage::Vertex ||
        fragment->stage != ShaderStage::Fragment) {
        log::error("rhi(vulkan): pipeline '%s' has incorrect shader stages",
                   desc.debugName.c_str());
        return {};
    }
    if (desc.colourFormats.size() > mCaps.maxColourAttachments) {
        log::error("rhi(vulkan): pipeline '%s' requests %zu colour attachments, limit is %u",
                   desc.debugName.c_str(), desc.colourFormats.size(),
                   mCaps.maxColourAttachments);
        return {};
    }
    if (desc.depthFormat == Format::Unknown &&
        (desc.depth.testEnabled || desc.depth.writeEnabled)) {
        log::error("rhi(vulkan): pipeline '%s' enables depth with no depth format",
                   desc.debugName.c_str());
        return {};
    }

    std::vector<VkFormat> colourFormats;
    colourFormats.reserve(desc.colourFormats.size());
    for (Format format : desc.colourFormats) {
        const VkFormat vkFormat = toVkFormat(format);
        if (vkFormat == VK_FORMAT_UNDEFINED ||
            (aspectForFormat(format) & VK_IMAGE_ASPECT_COLOR_BIT) == 0) {
            log::error("rhi(vulkan): pipeline '%s' has an invalid colour format",
                       desc.debugName.c_str());
            return {};
        }
        colourFormats.push_back(vkFormat);
    }
    const VkFormat depthFormat = toVkFormat(desc.depthFormat);
    if (desc.depthFormat != Format::Unknown &&
        (aspectForFormat(desc.depthFormat) & VK_IMAGE_ASPECT_DEPTH_BIT) == 0) {
        log::error("rhi(vulkan): pipeline '%s' has a non-depth depth format",
                   desc.debugName.c_str());
        return {};
    }

    std::vector<VkVertexInputBindingDescription> bindings;
    bindings.reserve(desc.vertexLayout.bindings.size());
    std::unordered_set<uint32_t> bindingNumbers;
    for (const VertexBinding& binding : desc.vertexLayout.bindings) {
        if (binding.stride == 0 || !bindingNumbers.insert(binding.binding).second) {
            log::error("rhi(vulkan): pipeline '%s' has an invalid or duplicate vertex binding %u",
                       desc.debugName.c_str(), binding.binding);
            return {};
        }
        bindings.push_back({binding.binding, binding.stride,
                            binding.perInstance
                                ? VK_VERTEX_INPUT_RATE_INSTANCE
                                : VK_VERTEX_INPUT_RATE_VERTEX});
    }
    std::vector<VkVertexInputAttributeDescription> attributes;
    attributes.reserve(desc.vertexLayout.attributes.size());
    std::unordered_set<uint32_t> locations;
    for (const VertexAttribute& attribute : desc.vertexLayout.attributes) {
        if (!bindingNumbers.contains(attribute.binding) ||
            !locations.insert(attribute.location).second) {
            log::error("rhi(vulkan): pipeline '%s' has an invalid vertex attribute",
                       desc.debugName.c_str());
            return {};
        }
        attributes.push_back({attribute.location, attribute.binding,
                              toVkVertexFormat(attribute.format),
                              attribute.offset});
    }

    const std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages{{
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
         VK_SHADER_STAGE_VERTEX_BIT, vertex->module,
         vertex->entryPoint.c_str(), nullptr},
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
         VK_SHADER_STAGE_FRAGMENT_BIT, fragment->module,
         fragment->entryPoint.c_str(), nullptr},
    }};
    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType =
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount =
        static_cast<uint32_t>(bindings.size());
    vertexInput.pVertexBindingDescriptions = bindings.data();
    vertexInput.vertexAttributeDescriptionCount =
        static_cast<uint32_t>(attributes.size());
    vertexInput.pVertexAttributeDescriptions = attributes.data();
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType =
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = toVkTopology(desc.topology);
    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType =
        VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rasterization{};
    rasterization.sType =
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterization.polygonMode = VK_POLYGON_MODE_FILL;
    rasterization.cullMode = toVkCullMode(desc.cull);
    // Negative-height viewports preserve GL orientation; this winding matches it.
    rasterization.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rasterization.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType =
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType =
        VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = desc.depth.testEnabled;
    depthStencil.depthWriteEnable = desc.depth.writeEnabled;
    depthStencil.depthCompareOp = toVkCompareOp(desc.depth.compare);

    std::vector<VkPipelineColorBlendAttachmentState> blendAttachments(
        colourFormats.size(), toVkBlendState(desc.blend));
    VkPipelineColorBlendStateCreateInfo blendState{};
    blendState.sType =
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blendState.attachmentCount =
        static_cast<uint32_t>(blendAttachments.size());
    blendState.pAttachments = blendAttachments.data();
    const std::array<VkDynamicState, 2> dynamicStates{
        VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount =
        static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    VkPipelineRenderingCreateInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    renderingInfo.colorAttachmentCount =
        static_cast<uint32_t>(colourFormats.size());
    renderingInfo.pColorAttachmentFormats = colourFormats.data();
    renderingInfo.depthAttachmentFormat = depthFormat;
    if (desc.depthFormat == Format::Depth24Stencil8)
        renderingInfo.stencilAttachmentFormat = depthFormat;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.pNext = &renderingInfo;
    pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
    pipelineInfo.pStages = shaderStages.data();
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterization;
    pipelineInfo.pMultisampleState = &multisample;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &blendState;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = mPipelineLayout;

    VkPipeline pipeline = VK_NULL_HANDLE;
    const VkResult result = vkCreateGraphicsPipelines(
        mContext.device.device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr,
        &pipeline);
    if (result != VK_SUCCESS) {
        apiError("vkCreateGraphicsPipelines", result);
        return {};
    }
    setDebugName(VK_OBJECT_TYPE_PIPELINE, vkHandleValue(pipeline),
                 desc.debugName);
    return mPipelines.create(
        {pipeline, std::move(colourFormats), depthFormat});
}

void VulkanDevice::destroyPipeline(PipelineHandle handle)
{
    auto record = mPipelines.remove(handle, "pipeline");
    if (!record)
        return;
    const VkDevice device = mContext.device.device;
    const VkPipeline pipeline = record->pipeline;
    scheduleDelete([device, pipeline] {
        vkDestroyPipeline(device, pipeline, nullptr);
    });
}

} // namespace eng::rhi::vulkan
