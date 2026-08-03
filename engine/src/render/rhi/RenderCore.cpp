#include "RenderCore.h"

#include "Image.h"

#include <eng/Log.h>
#include <eng/assets/AssetRoot.h>
#include <eng/render/ImGuiHint.h>
#include <eng/render/ImGuiLayout.h>
#include <eng/render/ImGuiTheme.h>

#include <ImGuizmo.h>
#include <backends/imgui_impl_sdl2.h>
#include <imgui.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <unordered_map>
#include <utility>
#include <vector>

namespace eng {
namespace {

std::vector<uint8_t> readBytes(const char* path)
{
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input)
        return {};
    const std::streamsize size = input.tellg();
    if (size <= 0)
        return {};
    input.seekg(0);
    std::vector<uint8_t> result(static_cast<size_t>(size));
    if (!input.read(reinterpret_cast<char*>(result.data()), size))
        return {};
    return result;
}

rhi::ShaderHandle loadShader(rhi::Device& device, rhi::ShaderStage stage,
                             const char* path, const char* name)
{
    rhi::ShaderDesc desc;
    desc.stage = stage;
    desc.code = readBytes(path);
    desc.debugName = name;
    if (desc.code.empty()) {
        log::error("RHI renderer: compiled shader '%s' is unreadable", path);
        return {};
    }
    return device.createShader(desc);
}

// The RHI pipeline layout declares one fixed 128-byte push range for every
// pipeline, so the UI/fullscreen passes pad to it: writing the whole declared
// range keeps the BestPractices layer quiet. The shaders only read the prefix.
struct UiConstants {
    glm::vec2 scale{1.0f};
    glm::vec2 translate{0.0f};
    float pad[28]{};
};
static_assert(sizeof(UiConstants) == 128,
              "UI push constants must cover the fixed RHI push range");

// Grade/dither parameters for post.frag. The scale/translate prefix matches
// UiConstants so the same blit plumbing drives either pipeline; everything is
// vec4-aligned so the C++ and std430 push-constant layouts agree by
// construction.
struct PostConstants {
    glm::vec2 scale{1.0f};
    glm::vec2 translate{0.0f};
    glm::vec4 shadowTint{0.0f};
    glm::vec4 midTint{0.0f};
    glm::vec4 vignetteColour{1.0f};
    glm::vec4 gradeA{0.0f};  // desaturate, contrast, saturation, tintStrength
    glm::vec4 gradeB{0.0f};  // blackLift, vignetteStrength, gradeOn, ditherOn
    glm::vec4 ditherA{1.0f}; // colDepth, ditherBanding, ditherDarkFade, -
    glm::vec4 bloom{0.0f};   // intensity, enabled, pixelSnap, -
};
static_assert(sizeof(PostConstants) == 128,
              "post push constants must cover the fixed RHI push range");

// Bright-pass threshold / blur direction. Same scale/translate prefix as the
// other blit push shapes so one code path fills it.
struct BloomConstants {
    glm::vec2 scale{1.0f};
    glm::vec2 translate{0.0f};
    glm::vec4 params{0.0f};
    float pad[24]{};
};
static_assert(sizeof(BloomConstants) == 128,
              "bloom push constants must cover the fixed RHI push range");

// Pixel-stylize edge pass parameters. Too many to fit the 128-byte push range
// alongside the blit prefix, so these ride in a uniform block instead; std140
// vec4 packing keeps the C++ and GLSL layouts aligned by construction.
struct StylizeUniforms {
    glm::vec4 shadowColour{0.0f};
    glm::vec4 highlightColour{0.0f};
    glm::vec4 outlineColour{0.0f};
    glm::vec4 clip{0.05f, 4000.0f, 0.0f, 0.0f}; // near, far, -, -
    glm::vec4 toggles{0.0f};   // stylize, shadows, highlights, outline
    glm::vec4 shadow{0.0f};    // strength, threshold, -, -
    glm::vec4 highlight{0.0f}; // strength, threshold, darkFade, colourOverride
    glm::vec4 outlineA{0.0f};  // opacity, thickness, depthSens, normalSens
    glm::vec4 outlineB{0.0f};  // sharpness, distFade, darkFade, -
    glm::vec4 convex{0.0f};    // convexity, bias, -, -
};

RenderCore* gActiveCore = nullptr;

} // namespace

struct RenderCore::Impl {
    struct TextureEntry {
        TextureBinding binding;
        bool ownsTexture = false;
        bool ownsSampler = false;
    };

    std::unique_ptr<rhi::Device> device;
    SDL_Window* window = nullptr;
    DrawScene drawScene;
    std::function<void()> shutdownCallback;

    uint32_t windowWidth = 1;
    uint32_t windowHeight = 1;
    int pixelSize = 3;
    int targetWidth = 0;
    int targetHeight = 0;
    glm::vec3 background{0.0f};
    glm::vec3 editorBackground{0.10f, 0.11f, 0.13f};

    View editorView{SceneTarget::Editor};
    View thumbnailView{SceneTarget::Thumbnail};
    uint32_t editorWidth = 0;
    uint32_t editorHeight = 0;
    uint32_t thumbnailSize = 0;

    rhi::TextureHandle sceneColour;
    rhi::TextureHandle sceneNormalDepth; // MRT surface 1 for the stylize pass
    rhi::TextureHandle stylizeColour;    // stylizer output, scene resolution
    // Bright-pass and blur ping-pong, at half the scene resolution (the legacy
    // chain's 1/6 of the window against its 1/3 scene target).
    rhi::TextureHandle bloomBright;
    rhi::TextureHandle bloomBlur;
    // One directional shadow map, sized once. 1024 is generous for a hard,
    // short-range shadow at this render resolution and costs one pass.
    rhi::TextureHandle shadowDepth;
    rhi::SamplerHandle shadowSamplerHandle;
    static constexpr uint32_t kShadowSize = 1024;
    bool shadowEnabled = false;
    glm::mat4 shadowViewProjection{1.0f};
    rhi::TextureHandle sceneDepth;
    rhi::TextureHandle finalColour;
    rhi::TextureHandle editorColour;
    rhi::TextureHandle editorDepth;
    rhi::TextureHandle thumbnailColour;
    rhi::TextureHandle thumbnailDepth;
    rhi::SamplerHandle sceneSampler;

    uint64_t editorToken = 0;
    uint64_t thumbnailToken = 0;
    uint64_t nextToken = 1;
    std::unordered_map<uint64_t, TextureEntry> textures;
    std::unordered_map<std::string, uint64_t> textureCache;
    TextureBinding fallback;

    rhi::ShaderHandle fullscreenVertex;
    rhi::ShaderHandle fullscreenFragment;
    rhi::ShaderHandle imguiVertex;
    rhi::ShaderHandle imguiFragment;
    rhi::ShaderHandle postFragment;
    rhi::ShaderHandle stylizeFragment;
    rhi::ShaderHandle bloomBrightFragment;
    rhi::ShaderHandle bloomBlurFragment;
    rhi::PipelineHandle bloomBrightPipeline;
    rhi::PipelineHandle bloomBlurPipeline;
    uint32_t bloomWidth = 1;
    uint32_t bloomHeight = 1;
    // Threshold/intensity/snap as the palette states them; enabled stays false
    // until the game turns bloom on, and then the passes are skipped entirely.
    bool bloomEnabled = false;
    float bloomThreshold = 0.7f;
    rhi::PipelineHandle presentPipeline;
    rhi::PipelineHandle postPipeline;
    rhi::PipelineHandle stylizePipeline;
    rhi::BufferHandle stylizeUniformBuffer;
    StylizeUniforms stylizeUniforms;
    uint32_t sceneWidth = 1;
    uint32_t sceneHeight = 1;
    rhi::PipelineHandle imguiPipeline;
    // Neutral until the game pushes a palette: gradeOn/ditherOn stay 0, so the
    // post pipeline is a pass-through and behaves exactly like compose.
    PostConstants postConstants;
    rhi::BufferHandle imguiVertices;
    rhi::BufferHandle imguiIndices;
    uint64_t imguiVertexCapacity = 0;
    uint64_t imguiIndexCapacity = 0;

    bool imguiInit = false;
    bool imguiFrameStarted = false;
    size_t batches = 0;
    size_t triangles = 0;

    uint64_t addTexture(TextureBinding binding, bool ownsTexture,
                        bool ownsSampler)
    {
        const uint64_t token = nextToken++;
        binding.token = token;
        textures.emplace(token,
                         TextureEntry{binding, ownsTexture, ownsSampler});
        return token;
    }

    void replaceToken(uint64_t token, rhi::TextureHandle texture,
                      uint32_t width, uint32_t height)
    {
        auto found = textures.find(token);
        if (found == textures.end())
            return;
        found->second.binding.texture = texture;
        found->second.binding.width = width;
        found->second.binding.height = height;
    }

    rhi::TextureHandle makeTarget(uint32_t width, uint32_t height,
                                  rhi::Format format, rhi::TextureUsage usage,
                                  const char* name)
    {
        rhi::TextureDesc desc;
        desc.width = std::max(1u, width);
        desc.height = std::max(1u, height);
        desc.format = format;
        desc.usage = usage;
        desc.debugName = name;
        return device->createTexture(desc);
    }

    void destroyMainTargets()
    {
        if (!device)
            return;
        if (finalColour.valid())
            device->destroyTexture(finalColour);
        if (bloomBlur.valid())
            device->destroyTexture(bloomBlur);
        if (bloomBright.valid())
            device->destroyTexture(bloomBright);
        if (stylizeColour.valid())
            device->destroyTexture(stylizeColour);
        if (sceneNormalDepth.valid())
            device->destroyTexture(sceneNormalDepth);
        if (sceneDepth.valid())
            device->destroyTexture(sceneDepth);
        if (sceneColour.valid())
            device->destroyTexture(sceneColour);
        finalColour = {};
        bloomBlur = {};
        bloomBright = {};
        stylizeColour = {};
        sceneNormalDepth = {};
        sceneDepth = {};
        sceneColour = {};
    }

    bool rebuildMainTargets()
    {
        if (!device || windowWidth == 0 || windowHeight == 0)
            return false;
        device->waitIdle();
        destroyMainTargets();
        const uint32_t sceneWidth =
            targetWidth > 0 ? static_cast<uint32_t>(targetWidth)
                            : std::max(1u, windowWidth / uint32_t(pixelSize));
        const uint32_t sceneHeight =
            targetHeight > 0 ? static_cast<uint32_t>(targetHeight)
                             : std::max(1u, windowHeight / uint32_t(pixelSize));
        sceneColour = makeTarget(
            sceneWidth, sceneHeight, rhi::Format::RGBA8Unorm,
            rhi::TextureUsage::RenderTarget | rhi::TextureUsage::Sampled,
            "renderer.scene-colour");
        sceneDepth =
            makeTarget(sceneWidth, sceneHeight, rhi::Format::Depth32Float,
                       rhi::TextureUsage::DepthStencil, "renderer.scene-depth");
        // Float16, like the legacy PF_FLOAT16_RGBA mrt: the alpha channel holds
        // signed linear depth / farClip, which has no headroom in 8 bit.
        sceneNormalDepth = makeTarget(
            sceneWidth, sceneHeight, rhi::Format::RGBA16Float,
            rhi::TextureUsage::RenderTarget | rhi::TextureUsage::Sampled,
            "renderer.scene-normal-depth");
        stylizeColour = makeTarget(
            sceneWidth, sceneHeight, rhi::Format::RGBA8Unorm,
            rhi::TextureUsage::RenderTarget | rhi::TextureUsage::Sampled,
            "renderer.stylize-colour");
        this->sceneWidth = sceneWidth;
        this->sceneHeight = sceneHeight;
        bloomWidth = std::max(1u, sceneWidth / 2u);
        bloomHeight = std::max(1u, sceneHeight / 2u);
        bloomBright = makeTarget(
            bloomWidth, bloomHeight, rhi::Format::RGBA8Unorm,
            rhi::TextureUsage::RenderTarget | rhi::TextureUsage::Sampled,
            "renderer.bloom-bright");
        bloomBlur = makeTarget(bloomWidth, bloomHeight, rhi::Format::RGBA8Unorm,
                               rhi::TextureUsage::RenderTarget |
                                   rhi::TextureUsage::Sampled,
                               "renderer.bloom-blur");
        finalColour = makeTarget(
            windowWidth, windowHeight, rhi::Format::RGBA8Unorm,
            rhi::TextureUsage::RenderTarget | rhi::TextureUsage::Sampled |
                rhi::TextureUsage::Readback,
            "renderer.final-readable");
        return sceneColour.valid() && sceneDepth.valid() && finalColour.valid();
    }

    void destroyEditorTargets()
    {
        if (!device)
            return;
        if (editorToken)
            replaceToken(editorToken, {}, 0, 0);
        if (editorDepth.valid())
            device->destroyTexture(editorDepth);
        if (editorColour.valid())
            device->destroyTexture(editorColour);
        editorDepth = {};
        editorColour = {};
    }

    bool rebuildEditorTargets()
    {
        if (!device || editorWidth == 0 || editorHeight == 0)
            return false;
        device->waitIdle();
        destroyEditorTargets();
        editorColour = makeTarget(
            editorWidth, editorHeight, rhi::Format::RGBA8Unorm,
            rhi::TextureUsage::RenderTarget | rhi::TextureUsage::Sampled,
            "renderer.editor-colour");
        editorDepth = makeTarget(
            editorWidth, editorHeight, rhi::Format::Depth32Float,
            rhi::TextureUsage::DepthStencil, "renderer.editor-depth");
        if (!editorToken) {
            TextureBinding binding{editorColour, sceneSampler, 0, editorWidth,
                                   editorHeight};
            editorToken = addTexture(binding, false, false);
        }
        else {
            replaceToken(editorToken, editorColour, editorWidth, editorHeight);
        }
        return editorColour.valid() && editorDepth.valid();
    }

    void destroyThumbnailTargets()
    {
        if (!device)
            return;
        if (thumbnailToken)
            replaceToken(thumbnailToken, {}, 0, 0);
        if (thumbnailDepth.valid())
            device->destroyTexture(thumbnailDepth);
        if (thumbnailColour.valid())
            device->destroyTexture(thumbnailColour);
        thumbnailDepth = {};
        thumbnailColour = {};
    }

    bool rebuildThumbnailTargets()
    {
        if (!device || thumbnailSize == 0)
            return false;
        device->waitIdle();
        destroyThumbnailTargets();
        thumbnailColour = makeTarget(
            thumbnailSize, thumbnailSize, rhi::Format::RGBA8Unorm,
            rhi::TextureUsage::RenderTarget | rhi::TextureUsage::Sampled,
            "renderer.thumbnail-colour");
        thumbnailDepth = makeTarget(
            thumbnailSize, thumbnailSize, rhi::Format::Depth32Float,
            rhi::TextureUsage::DepthStencil, "renderer.thumbnail-depth");
        if (!thumbnailToken) {
            TextureBinding binding{thumbnailColour, sceneSampler, 0,
                                   thumbnailSize, thumbnailSize};
            thumbnailToken = addTexture(binding, false, false);
        }
        else {
            replaceToken(thumbnailToken, thumbnailColour, thumbnailSize,
                         thumbnailSize);
        }
        return thumbnailColour.valid() && thumbnailDepth.valid();
    }

    rhi::PipelineHandle makeFullscreenPipeline(rhi::Format format,
                                               const char* name,
                                               rhi::ShaderHandle fragment = {})
    {
        rhi::PipelineDesc desc;
        desc.vertex = fullscreenVertex;
        desc.fragment = fragment.valid() ? fragment : fullscreenFragment;
        desc.cull = rhi::CullMode::None;
        desc.depth.testEnabled = false;
        desc.depth.writeEnabled = false;
        desc.colourFormats = {format};
        desc.depthFormat = rhi::Format::Unknown;
        desc.debugName = name;
        return device->createPipeline(desc);
    }

    bool initializePipelines()
    {
        fullscreenVertex =
            loadShader(*device, rhi::ShaderStage::Vertex,
                       ENG_RHI_FULLSCREEN_VERT_SPV, "renderer.fullscreen.vert");
        fullscreenFragment =
            loadShader(*device, rhi::ShaderStage::Fragment,
                       ENG_RHI_FULLSCREEN_FRAG_SPV, "renderer.fullscreen.frag");
        postFragment = loadShader(*device, rhi::ShaderStage::Fragment,
                                  ENG_RHI_POST_FRAG_SPV, "renderer.post.frag");
        stylizeFragment =
            loadShader(*device, rhi::ShaderStage::Fragment,
                       ENG_RHI_STYLIZE_FRAG_SPV, "renderer.stylize.frag");
        bloomBrightFragment = loadShader(*device, rhi::ShaderStage::Fragment,
                                         ENG_RHI_BLOOM_BRIGHT_FRAG_SPV,
                                         "renderer.bloom-bright.frag");
        bloomBlurFragment =
            loadShader(*device, rhi::ShaderStage::Fragment,
                       ENG_RHI_BLOOM_BLUR_FRAG_SPV, "renderer.bloom-blur.frag");
        imguiVertex = loadShader(*device, rhi::ShaderStage::Vertex,
                                 ENG_RHI_IMGUI_VERT_SPV, "renderer.imgui.vert");
        imguiFragment =
            loadShader(*device, rhi::ShaderStage::Fragment,
                       ENG_RHI_IMGUI_FRAG_SPV, "renderer.imgui.frag");
        if (!fullscreenVertex.valid() || !fullscreenFragment.valid() ||
            !postFragment.valid() || !stylizeFragment.valid() ||
            !bloomBrightFragment.valid() || !bloomBlurFragment.valid() ||
            !imguiVertex.valid() || !imguiFragment.valid())
            return false;

        postPipeline = makeFullscreenPipeline(
            rhi::Format::RGBA8Unorm, "renderer.post-pipeline", postFragment);
        stylizePipeline = makeFullscreenPipeline(rhi::Format::RGBA8Unorm,
                                                 "renderer.stylize-pipeline",
                                                 stylizeFragment);
        bloomBrightPipeline = makeFullscreenPipeline(
            rhi::Format::RGBA8Unorm, "renderer.bloom-bright-pipeline",
            bloomBrightFragment);
        bloomBlurPipeline = makeFullscreenPipeline(
            rhi::Format::RGBA8Unorm, "renderer.bloom-blur-pipeline",
            bloomBlurFragment);
        rhi::BufferDesc stylizeDesc;
        stylizeDesc.size = sizeof(StylizeUniforms);
        stylizeDesc.usage =
            rhi::BufferUsage::Uniform | rhi::BufferUsage::Dynamic;
        stylizeDesc.debugName = "renderer.stylize-uniforms";
        stylizeUniformBuffer = device->createBuffer(stylizeDesc);
        presentPipeline = makeFullscreenPipeline(device->swapchainFormat(),
                                                 "renderer.present-pipeline");

        rhi::PipelineDesc ui;
        ui.vertex = imguiVertex;
        ui.fragment = imguiFragment;
        ui.vertexLayout.bindings.push_back({0, sizeof(ImDrawVert), false});
        ui.vertexLayout.attributes.push_back(
            {0, 0, rhi::VertexFormat::Float2, offsetof(ImDrawVert, pos)});
        ui.vertexLayout.attributes.push_back(
            {1, 0, rhi::VertexFormat::Float2, offsetof(ImDrawVert, uv)});
        ui.vertexLayout.attributes.push_back(
            {2, 0, rhi::VertexFormat::UByte4Unorm, offsetof(ImDrawVert, col)});
        ui.cull = rhi::CullMode::None;
        ui.depth.testEnabled = false;
        ui.depth.writeEnabled = false;
        ui.blend = rhi::BlendMode::AlphaBlend;
        ui.colourFormats = {rhi::Format::RGBA8Unorm};
        ui.depthFormat = rhi::Format::Unknown;
        ui.debugName = "renderer.imgui-pipeline";
        imguiPipeline = device->createPipeline(ui);
        return presentPipeline.valid() && postPipeline.valid() &&
               stylizePipeline.valid() && stylizeUniformBuffer.valid() &&
               bloomBrightPipeline.valid() && bloomBlurPipeline.valid() &&
               imguiPipeline.valid();
    }

    bool ensureUiBuffers(const ImDrawData& data)
    {
        const uint64_t vertexBytes =
            uint64_t(std::max(data.TotalVtxCount, 1)) * sizeof(ImDrawVert);
        const uint64_t indexBytes =
            uint64_t(std::max(data.TotalIdxCount, 1)) * sizeof(ImDrawIdx);
        if (vertexBytes > imguiVertexCapacity) {
            if (imguiVertices.valid())
                device->destroyBuffer(imguiVertices);
            imguiVertexCapacity =
                std::max(vertexBytes, imguiVertexCapacity * 2);
            imguiVertexCapacity =
                std::max<uint64_t>(imguiVertexCapacity, 65536);
            rhi::BufferDesc desc;
            desc.size = imguiVertexCapacity;
            desc.usage = rhi::BufferUsage::Vertex | rhi::BufferUsage::Dynamic;
            desc.debugName = "renderer.imgui-vertices";
            imguiVertices = device->createBuffer(desc);
        }
        if (indexBytes > imguiIndexCapacity) {
            if (imguiIndices.valid())
                device->destroyBuffer(imguiIndices);
            imguiIndexCapacity = std::max(indexBytes, imguiIndexCapacity * 2);
            imguiIndexCapacity = std::max<uint64_t>(imguiIndexCapacity, 32768);
            rhi::BufferDesc desc;
            desc.size = imguiIndexCapacity;
            desc.usage = rhi::BufferUsage::Index | rhi::BufferUsage::Dynamic;
            desc.debugName = "renderer.imgui-indices";
            imguiIndices = device->createBuffer(desc);
        }
        if (!imguiVertices.valid() || !imguiIndices.valid())
            return false;

        uint64_t vertexOffset = 0;
        uint64_t indexOffset = 0;
        for (int i = 0; i < data.CmdListsCount; ++i) {
            const ImDrawList* list = data.CmdLists[i];
            const uint64_t vertices =
                uint64_t(list->VtxBuffer.Size) * sizeof(ImDrawVert);
            const uint64_t indices =
                uint64_t(list->IdxBuffer.Size) * sizeof(ImDrawIdx);
            if (vertices)
                device->updateBuffer(imguiVertices, list->VtxBuffer.Data,
                                     vertices, vertexOffset);
            if (indices)
                device->updateBuffer(imguiIndices, list->IdxBuffer.Data,
                                     indices, indexOffset);
            vertexOffset += vertices;
            indexOffset += indices;
        }
        return true;
    }

    void scenePass(rhi::TextureHandle colour, rhi::TextureHandle depth,
                    uint32_t width, uint32_t height, glm::vec3 clear,
                    const View& view, rhi::TextureHandle normalDepth = {},
                    bool clearColour = true, bool clearDepth = true)
    {
        if (!colour.valid() || !depth.valid())
            return;
        rhi::RenderPassDesc pass;
        rhi::ColourAttachment target;
        target.texture = colour;
        target.clear = clearColour;
        target.clearColour[0] = clear.r;
        target.clearColour[1] = clear.g;
        target.clearColour[2] = clear.b;
        pass.colour.push_back(target);
        // MRT surface 1: view-space normal + signed linear depth, the metadata
        // the stylize pass edge-detects on. Cleared to zero, which the stylizer
        // reads as "no geometry here" and passes through untouched.
        if (normalDepth.valid()) {
            rhi::ColourAttachment metadata;
            metadata.texture = normalDepth;
            metadata.clear = clearColour;
            metadata.clearColour[0] = 0.0f;
            metadata.clearColour[1] = 0.0f;
            metadata.clearColour[2] = 0.0f;
            metadata.clearColour[3] = 0.0f;
            pass.colour.push_back(metadata);
        }
        pass.depth.texture = depth;
        pass.depth.clear = clearDepth;
        pass.debugName =
            view.target == SceneTarget::Main     ? "renderer.scene-pass"
            : view.target == SceneTarget::Editor ? "renderer.editor-pass"
            : view.target == SceneTarget::Thumbnail
                ? "renderer.thumbnail-pass"
                : "renderer.viewmodel-pass";
        rhi::CommandList& commands = device->beginPass(pass);
        commands.setViewport({0, 0, float(width), float(height), 0, 1});
        commands.setScissor({0, 0, width, height});
        if (drawScene)
            drawScene(commands, view, width, height);
        device->endPass();
    }

    // Fullscreen-triangle pass. Grew enough knobs (second sampler, off-window
    // target sizes, a uniform block, two push-constant shapes) that positional
    // arguments stopped being readable.
    // Which push-constant shape the bound pipeline declares. All three share a
    // scale/translate prefix, so the blit fills that part identically.
    enum class PushShape { Ui, Post, Bloom };

    struct BlitPass {
        rhi::TextureHandle output; // invalid = swapchain
        rhi::Format outputFormat = rhi::Format::RGBA8Unorm;
        rhi::PipelineHandle pipeline;
        rhi::TextureHandle source;
        rhi::TextureHandle source2; // optional texture binding 1
        rhi::BufferHandle uniforms; // optional uniform binding 0
        bool clear = true;
        const char* name = "renderer.blit";
        bool flipV = false;
        PushShape push = PushShape::Ui;
        glm::vec4 bloomParams{0.0f};
        uint32_t width = 0; // 0 = window size
        uint32_t height = 0;
    };

    void fullscreenPass(const BlitPass& blit)
    {
        rhi::RenderPassDesc pass;
        rhi::ColourAttachment colour;
        colour.texture = blit.output;
        colour.clear = blit.clear;
        pass.colour.push_back(colour);
        pass.debugName = blit.name;
        const uint32_t passWidth = blit.width ? blit.width : windowWidth;
        const uint32_t passHeight = blit.height ? blit.height : windowHeight;
        rhi::CommandList& commands = device->beginPass(pass);
        commands.bindPipeline(blit.pipeline);
        commands.setViewport({0, 0, float(passWidth), float(passHeight), 0, 1});
        commands.setScissor({0, 0, passWidth, passHeight});
        commands.bindTexture(0, blit.source, sceneSampler);
        if (blit.source2.valid())
            commands.bindTexture(1, blit.source2, sceneSampler);
        if (blit.uniforms.valid())
            commands.bindUniformBuffer(0, blit.uniforms);
        const bool flipV = blit.flipV;
        // uvScale/uvOffset select a straight or vertically mirrored sample.
        // Padded to the fixed push range so no declared byte is left unset.
        const glm::vec2 scale =
            flipV ? glm::vec2(1.0f, -1.0f) : glm::vec2(1.0f, 1.0f);
        const glm::vec2 translate =
            flipV ? glm::vec2(0.0f, 1.0f) : glm::vec2(0.0f, 0.0f);
        switch (blit.push) {
        case PushShape::Post: {
            PostConstants post = postConstants;
            post.scale = scale;
            post.translate = translate;
            commands.pushConstants(&post, sizeof(post));
            break;
        }
        case PushShape::Bloom: {
            BloomConstants bloom;
            bloom.scale = scale;
            bloom.translate = translate;
            bloom.params = blit.bloomParams;
            commands.pushConstants(&bloom, sizeof(bloom));
            break;
        }
        default: {
            UiConstants ui;
            ui.scale = scale;
            ui.translate = translate;
            commands.pushConstants(&ui, sizeof(ui));
            break;
        }
        }
        commands.draw(3);
        device->endPass();
        ++batches;
        ++triangles;
    }

    void drawImGui(const ImDrawData& data)
    {
        if (data.TotalVtxCount <= 0 || data.DisplaySize.x <= 0.0f ||
            data.DisplaySize.y <= 0.0f)
            return;
        rhi::RenderPassDesc pass;
        rhi::ColourAttachment colour;
        colour.texture = finalColour;
        colour.clear = false;
        pass.colour.push_back(colour);
        pass.debugName = "renderer.imgui-pass";
        rhi::CommandList& commands = device->beginPass(pass);
        commands.bindPipeline(imguiPipeline);
        commands.setViewport(
            {0, 0, float(windowWidth), float(windowHeight), 0, 1});
        commands.bindVertexBuffer(0, imguiVertices);
        commands.bindIndexBuffer(imguiIndices, 0,
                                 sizeof(ImDrawIdx) == 2
                                     ? rhi::IndexType::UInt16
                                     : rhi::IndexType::UInt32);

        UiConstants constants;
        constants.scale = {2.0f / data.DisplaySize.x,
                           -2.0f / data.DisplaySize.y};
        constants.translate = {-1.0f - data.DisplayPos.x * constants.scale.x,
                               1.0f - data.DisplayPos.y * constants.scale.y};
        commands.pushConstants(&constants, sizeof(constants));

        int globalVertexOffset = 0;
        uint32_t globalIndexOffset = 0;
        const ImVec2 clipOffset = data.DisplayPos;
        const ImVec2 clipScale = data.FramebufferScale;
        for (int listIndex = 0; listIndex < data.CmdListsCount; ++listIndex) {
            const ImDrawList* list = data.CmdLists[listIndex];
            for (const ImDrawCmd& draw : list->CmdBuffer) {
                if (draw.UserCallback) {
                    if (draw.UserCallback != ImDrawCallback_ResetRenderState)
                        draw.UserCallback(list, &draw);
                    continue;
                }
                const float clipMinX =
                    (draw.ClipRect.x - clipOffset.x) * clipScale.x;
                const float clipMinY =
                    (draw.ClipRect.y - clipOffset.y) * clipScale.y;
                const float clipMaxX =
                    (draw.ClipRect.z - clipOffset.x) * clipScale.x;
                const float clipMaxY =
                    (draw.ClipRect.w - clipOffset.y) * clipScale.y;
                if (clipMaxX <= clipMinX || clipMaxY <= clipMinY)
                    continue;
                const int32_t left =
                    std::max(0, static_cast<int32_t>(clipMinX));
                const int32_t bottom = std::max(
                    0, static_cast<int32_t>(float(windowHeight) - clipMaxY));
                const uint32_t width = static_cast<uint32_t>(std::max(
                    0.0f, std::min(float(windowWidth), clipMaxX) - left));
                const uint32_t height = static_cast<uint32_t>(
                    std::max(0.0f, std::min(float(windowHeight), clipMaxY) -
                                       std::max(0.0f, clipMinY)));
                if (!width || !height)
                    continue;
                commands.setScissor({left, bottom, width, height});
                auto texture = textures.find(uint64_t(draw.GetTexID()));
                if (texture == textures.end() ||
                    !texture->second.binding.valid()) {
                    texture = textures.find(fallback.token);
                    if (texture == textures.end())
                        continue;
                }
                commands.bindTexture(0, texture->second.binding.texture,
                                     texture->second.binding.sampler);
                commands.drawIndexed(
                    draw.ElemCount, 1, globalIndexOffset + draw.IdxOffset,
                    globalVertexOffset + int(draw.VtxOffset), 0);
                ++batches;
                triangles += draw.ElemCount / 3;
            }
            globalIndexOffset += uint32_t(list->IdxBuffer.Size);
            globalVertexOffset += list->VtxBuffer.Size;
        }
        device->endPass();
    }
};

RenderCore::RenderCore() : mImpl(std::make_unique<Impl>()) {}
RenderCore::~RenderCore()
{
    shutdown();
}

bool RenderCore::init(uintptr_t nativeWindowHandle, void* sdlWindow, int width,
                      int height, const std::string&, bool vsync)
{
    mImpl->window = static_cast<SDL_Window*>(sdlWindow);
    if (!mImpl->window) {
        log::error("RHI renderer: SDL window is null");
        return false;
    }
    int drawableWidth = width;
    int drawableHeight = height;
    SDL_Vulkan_GetDrawableSize(mImpl->window, &drawableWidth, &drawableHeight);
    mImpl->windowWidth = uint32_t(std::max(drawableWidth, 1));
    mImpl->windowHeight = uint32_t(std::max(drawableHeight, 1));

    rhi::DeviceDesc desc;
    desc.nativeWindowHandle = nativeWindowHandle;
    desc.platformWindow = sdlWindow;
    desc.width = mImpl->windowWidth;
    desc.height = mImpl->windowHeight;
    desc.vsync = vsync;
    desc.enableValidation = std::getenv("RAVEN_VULKAN_VALIDATION") != nullptr;
    mImpl->device = rhi::createDevice(rhi::BackendKind::Vulkan, desc);
    if (!mImpl->device)
        return false;

    rhi::SamplerDesc sampler;
    sampler.minFilter = rhi::FilterMode::Nearest;
    sampler.magFilter = rhi::FilterMode::Nearest;
    sampler.addressU = rhi::AddressMode::ClampToEdge;
    sampler.addressV = rhi::AddressMode::ClampToEdge;
    mImpl->sceneSampler = mImpl->device->createSampler(sampler);
    // Linear on the shadow map: the comparison is still a hard threshold, but
    // filtering the depth lets the four taps in the scene shader soften the
    // stair-step along a silhouette without turning the shadow soft.
    rhi::SamplerDesc shadowSampler;
    shadowSampler.minFilter = rhi::FilterMode::Linear;
    shadowSampler.magFilter = rhi::FilterMode::Linear;
    shadowSampler.addressU = rhi::AddressMode::ClampToEdge;
    shadowSampler.addressV = rhi::AddressMode::ClampToEdge;
    mImpl->shadowSamplerHandle = mImpl->device->createSampler(shadowSampler);
    mImpl->shadowDepth = mImpl->makeTarget(
        Impl::kShadowSize, Impl::kShadowSize, rhi::Format::Depth32Float,
        rhi::TextureUsage::DepthStencil | rhi::TextureUsage::Sampled,
        "renderer.shadow-depth");
    if (!mImpl->shadowSamplerHandle.valid() || !mImpl->shadowDepth.valid() ||
        !mImpl->sceneSampler.valid() || !mImpl->initializePipelines() ||
        !mImpl->rebuildMainTargets()) {
        shutdown();
        return false;
    }

    fallbackTexture();
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(float(width), float(height));
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable |
                      ImGuiConfigFlags_NavEnableKeyboard |
                      ImGuiConfigFlags_NavEnableGamepad;
    imgui_layout::install();
    io.Fonts->AddFontDefault();
    ImFontConfig toolFont;
    toolFont.OversampleH = 2;
    toolFont.OversampleV = 2;
    const std::filesystem::path mono =
        assets::resolve("fonts/DejaVuSansMono.ttf");
    if (!mono.empty()) {
        if (ImFont* font = io.Fonts->AddFontFromFileTTF(mono.string().c_str(),
                                                        15.0f, &toolFont))
            io.FontDefault = font;
    }
    const char* theme = std::getenv("RAVEN_IMGUI_THEME");
    if (!theme || !imguitheme::apply(theme))
        imguitheme::apply("raven_editor");
    if (!ImGui_ImplSDL2_InitForVulkan(mImpl->window)) {
        log::error("RHI renderer: ImGui SDL2 Vulkan initialization failed");
        shutdown();
        return false;
    }
    unsigned char* atlas = nullptr;
    int atlasWidth = 0;
    int atlasHeight = 0;
    io.Fonts->GetTexDataAsRGBA32(&atlas, &atlasWidth, &atlasHeight);
    const TextureBinding font = createTexture(
        "renderer.imgui-font", uint32_t(atlasWidth), uint32_t(atlasHeight),
        atlas, rhi::FilterMode::Linear, rhi::AddressMode::ClampToEdge);
    if (!font.valid()) {
        shutdown();
        return false;
    }
    io.Fonts->SetTexID(ImTextureID(font.token));
    imguihint::load(assets::resolve("ui/hints.toml").string());
    mImpl->imguiInit = true;
    gActiveCore = this;

    const rhi::DeviceCapabilities& caps = mImpl->device->capabilities();
    log::info("RHI renderer: %s on %s", caps.backendName.c_str(),
              caps.deviceName.c_str());
    log::info("Window: %ux%u, Vulkan, vsync %s", mImpl->windowWidth,
              mImpl->windowHeight, vsync ? "on" : "off");
    return true;
}

void RenderCore::shutdown()
{
    if (!mImpl->device)
        return;
    mImpl->device->waitIdle();
    if (mImpl->shutdownCallback)
        mImpl->shutdownCallback();
    mImpl->shutdownCallback = {};
    mImpl->drawScene = {};
    if (gActiveCore == this)
        gActiveCore = nullptr;

    if (mImpl->imguiInit) {
        imgui_layout::save();
        ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext();
        mImpl->imguiInit = false;
    }
    if (mImpl->imguiIndices.valid())
        mImpl->device->destroyBuffer(mImpl->imguiIndices);
    if (mImpl->imguiVertices.valid())
        mImpl->device->destroyBuffer(mImpl->imguiVertices);
    if (mImpl->imguiPipeline.valid())
        mImpl->device->destroyPipeline(mImpl->imguiPipeline);
    if (mImpl->presentPipeline.valid())
        mImpl->device->destroyPipeline(mImpl->presentPipeline);
    if (mImpl->shadowDepth.valid())
        mImpl->device->destroyTexture(mImpl->shadowDepth);
    if (mImpl->shadowSamplerHandle.valid())
        mImpl->device->destroySampler(mImpl->shadowSamplerHandle);
    if (mImpl->stylizeUniformBuffer.valid())
        mImpl->device->destroyBuffer(mImpl->stylizeUniformBuffer);
    if (mImpl->bloomBlurPipeline.valid())
        mImpl->device->destroyPipeline(mImpl->bloomBlurPipeline);
    if (mImpl->bloomBrightPipeline.valid())
        mImpl->device->destroyPipeline(mImpl->bloomBrightPipeline);
    if (mImpl->stylizePipeline.valid())
        mImpl->device->destroyPipeline(mImpl->stylizePipeline);
    if (mImpl->postPipeline.valid())
        mImpl->device->destroyPipeline(mImpl->postPipeline);
    if (mImpl->imguiFragment.valid())
        mImpl->device->destroyShader(mImpl->imguiFragment);
    if (mImpl->imguiVertex.valid())
        mImpl->device->destroyShader(mImpl->imguiVertex);
    if (mImpl->bloomBlurFragment.valid())
        mImpl->device->destroyShader(mImpl->bloomBlurFragment);
    if (mImpl->bloomBrightFragment.valid())
        mImpl->device->destroyShader(mImpl->bloomBrightFragment);
    if (mImpl->stylizeFragment.valid())
        mImpl->device->destroyShader(mImpl->stylizeFragment);
    if (mImpl->postFragment.valid())
        mImpl->device->destroyShader(mImpl->postFragment);
    if (mImpl->fullscreenFragment.valid())
        mImpl->device->destroyShader(mImpl->fullscreenFragment);
    if (mImpl->fullscreenVertex.valid())
        mImpl->device->destroyShader(mImpl->fullscreenVertex);

    mImpl->destroyThumbnailTargets();
    mImpl->destroyEditorTargets();
    mImpl->destroyMainTargets();
    for (auto& [token, entry] : mImpl->textures) {
        if (entry.ownsTexture && entry.binding.texture.valid())
            mImpl->device->destroyTexture(entry.binding.texture);
        if (entry.ownsSampler && entry.binding.sampler.valid())
            mImpl->device->destroySampler(entry.binding.sampler);
    }
    mImpl->textures.clear();
    mImpl->textureCache.clear();
    if (mImpl->sceneSampler.valid())
        mImpl->device->destroySampler(mImpl->sceneSampler);
    mImpl->device->waitIdle();
    mImpl->device.reset();
    mImpl->window = nullptr;
}

void RenderCore::beginImGuiFrame(float)
{
    if (!mImpl->imguiInit)
        return;
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
    ImGuizmo::BeginFrame();
    mImpl->imguiFrameStarted = true;
}

bool RenderCore::imguiReady() const
{
    return mImpl->imguiInit;
}

void RenderCore::renderFrame(float)
{
    if (!mImpl->device)
        return;
    ImDrawData* drawData = nullptr;
    if (mImpl->imguiFrameStarted) {
        ImGui::Render();
        drawData = ImGui::GetDrawData();
        if (drawData && !mImpl->ensureUiBuffers(*drawData))
            drawData = nullptr;
    }

    int drawableWidth = 0;
    int drawableHeight = 0;
    SDL_Vulkan_GetDrawableSize(mImpl->window, &drawableWidth, &drawableHeight);
    if (drawableWidth > 0 && drawableHeight > 0 &&
        (uint32_t(drawableWidth) != mImpl->windowWidth ||
         uint32_t(drawableHeight) != mImpl->windowHeight)) {
        mImpl->windowWidth = uint32_t(drawableWidth);
        mImpl->windowHeight = uint32_t(drawableHeight);
        mImpl->device->resizeSwapchain(mImpl->windowWidth, mImpl->windowHeight);
        mImpl->rebuildMainTargets();
    }

    mImpl->batches = 0;
    mImpl->triangles = 0;
    if (!mImpl->device->beginFrame()) {
        mImpl->imguiFrameStarted = false;
        return;
    }
    if (mImpl->editorColour.valid())
        mImpl->scenePass(mImpl->editorColour, mImpl->editorDepth,
                         mImpl->editorWidth, mImpl->editorHeight,
                         mImpl->editorBackground, mImpl->editorView);
    if (mImpl->thumbnailColour.valid())
        mImpl->scenePass(mImpl->thumbnailColour, mImpl->thumbnailDepth,
                         mImpl->thumbnailSize, mImpl->thumbnailSize,
                         {0.13f, 0.14f, 0.16f}, mImpl->thumbnailView);

    const uint32_t sceneWidth =
        mImpl->targetWidth > 0
            ? uint32_t(mImpl->targetWidth)
            : std::max(1u, mImpl->windowWidth / uint32_t(mImpl->pixelSize));
    const uint32_t sceneHeight =
        mImpl->targetHeight > 0
            ? uint32_t(mImpl->targetHeight)
            : std::max(1u, mImpl->windowHeight / uint32_t(mImpl->pixelSize));
    View mainView;
    // Legacy chain (assets/compositors/psx.compositor):
    //   scene MRT -> PixelStylize -> bloom -> dither -> window.
    // Depth only, from the sun, before anything samples it.
    //
    // Run UNCONDITIONALLY when the target exists, even with shadows off. The
    // pass is what leaves the texture in a sampleable layout, and the scene
    // binds it on every draw; skipping the pass left it in the attachment
    // layout it was created in and every draw of the first frame failed
    // validation. Disabled, this is a clear with no draws -- and the shader
    // ignores the result because shadowParams.x is zero.
    if (mImpl->shadowDepth.valid()) {
        rhi::RenderPassDesc pass;
        pass.depth.texture = mImpl->shadowDepth;
        pass.debugName = "renderer.shadow-pass";
        rhi::CommandList& commands = mImpl->device->beginPass(pass);
        commands.setViewport(
            {0, 0, float(Impl::kShadowSize), float(Impl::kShadowSize), 0, 1});
        commands.setScissor({0, 0, Impl::kShadowSize, Impl::kShadowSize});
        if (mImpl->drawScene) {
            View shadowView;
            shadowView.target = SceneTarget::Shadow;
            mImpl->drawScene(commands, shadowView, Impl::kShadowSize,
                             Impl::kShadowSize);
        }
        mImpl->device->endPass();
    }
    mImpl->scenePass(mImpl->sceneColour, mImpl->sceneDepth, sceneWidth,
                      sceneHeight, mImpl->background, mainView,
                      mImpl->sceneNormalDepth);
    // First-person geometry needs world-independent depth, not disabled depth.
    // Preserve scene/MRT colour, clear only depth, then draw viewmodels with
    // normal depth testing so hands and weapons self-occlude correctly.
    View viewmodelView;
    viewmodelView.target = SceneTarget::Viewmodel;
    mImpl->scenePass(mImpl->sceneColour, mImpl->sceneDepth, sceneWidth,
                     sceneHeight, mImpl->background, viewmodelView,
                     mImpl->sceneNormalDepth, false, true);
    // Every blit below wants flipV. The scene pass renders through the
    // negative-height viewport, which is the maintenance1 y-inversion: NDC +1
    // lands on row 0, so the world is already upright in sceneColour, and
    // flipV is the *straight* copy -- the fullscreen triangle puts v=0 on the
    // bottom row under that same inverted viewport, and flipV cancels it.
    // Omitting it mirrored the world, and because ImGui composes into
    // finalColour below (after the upscale, so the HUD is not dragged along)
    // the mirror hit the world alone: the dungeon rendered upside down under an
    // upright HUD, which also read as inverted mouse pitch.
    //
    // The stylizer runs at scene resolution so its 4-tap cross lands on whole
    // render pixels -- that is what keeps every ink line exactly one pixel
    // thick. flipV like every other blit here: it is the orientation-preserving
    // copy, and it also puts uv v=0 on the top row growing downward, which is
    // the convention the tap directions below assume (screen up = view +y).
    device()->updateBuffer(mImpl->stylizeUniformBuffer, &mImpl->stylizeUniforms,
                           sizeof(mImpl->stylizeUniforms));
    Impl::BlitPass stylize;
    stylize.output = mImpl->stylizeColour;
    stylize.pipeline = mImpl->stylizePipeline;
    stylize.source = mImpl->sceneColour;
    stylize.source2 = mImpl->sceneNormalDepth;
    stylize.uniforms = mImpl->stylizeUniformBuffer;
    stylize.name = "renderer.stylize-pass";
    stylize.flipV = true;
    stylize.width = mImpl->sceneWidth;
    stylize.height = mImpl->sceneHeight;
    mImpl->fullscreenPass(stylize);

    // Bloom: bright-pass then a separable blur, all at half the scene
    // resolution. The composite is folded into the upscale below rather than
    // given its own target, which keeps it before the grade and dither exactly
    // as the legacy chain has it. Skipped entirely when the palette turns bloom
    // off -- the post shader's own toggle then reads an unwritten target, so
    // the passes and the flag have to agree.
    if (mImpl->bloomEnabled) {
        Impl::BlitPass bright;
        bright.output = mImpl->bloomBright;
        bright.pipeline = mImpl->bloomBrightPipeline;
        bright.source = mImpl->stylizeColour;
        bright.name = "renderer.bloom-bright-pass";
        bright.flipV = true;
        bright.push = Impl::PushShape::Bloom;
        bright.bloomParams = {mImpl->bloomThreshold, 0.0f, 0.0f, 0.0f};
        bright.width = mImpl->bloomWidth;
        bright.height = mImpl->bloomHeight;
        mImpl->fullscreenPass(bright);

        Impl::BlitPass blurH;
        blurH.output = mImpl->bloomBlur;
        blurH.pipeline = mImpl->bloomBlurPipeline;
        blurH.source = mImpl->bloomBright;
        blurH.name = "renderer.bloom-blur-h";
        blurH.flipV = true;
        blurH.push = Impl::PushShape::Bloom;
        blurH.bloomParams = {1.0f, 0.0f, 0.0f, 0.0f};
        blurH.width = mImpl->bloomWidth;
        blurH.height = mImpl->bloomHeight;
        mImpl->fullscreenPass(blurH);

        Impl::BlitPass blurV;
        blurV.output = mImpl->bloomBright;
        blurV.pipeline = mImpl->bloomBlurPipeline;
        blurV.source = mImpl->bloomBlur;
        blurV.name = "renderer.bloom-blur-v";
        blurV.flipV = true;
        blurV.push = Impl::PushShape::Bloom;
        blurV.bloomParams = {0.0f, 1.0f, 0.0f, 0.0f};
        blurV.width = mImpl->bloomWidth;
        blurV.height = mImpl->bloomHeight;
        mImpl->fullscreenPass(blurV);
    }

    // The upscale doubles as the legacy dither/grade compositor pass: it is the
    // one place that sees the finished scene colour at scene resolution while
    // still writing window pixels, so grading, vignette and the ordered dither
    // land before the HUD composes on top (the HUD must not be graded).
    Impl::BlitPass upscale;
    upscale.output = mImpl->finalColour;
    upscale.pipeline = mImpl->postPipeline;
    upscale.source = mImpl->stylizeColour;
    // Always bound: a descriptor set with a hole is invalid even when the
    // shader's own toggle would never sample it.
    upscale.source2 =
        mImpl->bloomEnabled ? mImpl->bloomBright : mImpl->stylizeColour;
    upscale.name = "renderer.upscale-pass";
    upscale.flipV = true;
    upscale.push = Impl::PushShape::Post;
    mImpl->fullscreenPass(upscale);

    if (drawData)
        mImpl->drawImGui(*drawData);

    Impl::BlitPass present;
    present.outputFormat = mImpl->device->swapchainFormat();
    present.pipeline = mImpl->presentPipeline;
    present.source = mImpl->finalColour;
    present.name = "renderer.present-pass";
    present.flipV = true;
    mImpl->fullscreenPass(present);
    mImpl->device->endFrame();
    mImpl->imguiFrameStarted = false;
}

void RenderCore::onResize(int width, int height)
{
    if (!mImpl->device || width <= 0 || height <= 0)
        return;
    int drawableWidth = width;
    int drawableHeight = height;
    SDL_Vulkan_GetDrawableSize(mImpl->window, &drawableWidth, &drawableHeight);
    mImpl->windowWidth = uint32_t(std::max(drawableWidth, 1));
    mImpl->windowHeight = uint32_t(std::max(drawableHeight, 1));
    mImpl->device->resizeSwapchain(mImpl->windowWidth, mImpl->windowHeight);
    mImpl->rebuildMainTargets();
}

void RenderCore::writeScreenshot(const std::string& path)
{
    if (!mImpl->device || !mImpl->finalColour.valid())
        return;
    std::vector<uint8_t> pixels(size_t(mImpl->windowWidth) *
                                mImpl->windowHeight * 4u);
    mImpl->device->readTexture(mImpl->finalColour, pixels.data(),
                               pixels.size());
    if (rhi_renderer::writePng(path, int(mImpl->windowWidth),
                               int(mImpl->windowHeight), pixels.data()))
        log::info("RHI renderer: screenshot wrote %s", path.c_str());
}

void RenderCore::frameStats(size_t& batches, size_t& triangles) const
{
    batches = mImpl->batches;
    triangles = mImpl->triangles;
}

void RenderCore::setDrawScene(DrawScene draw)
{
    mImpl->drawScene = std::move(draw);
}
void RenderCore::setShutdownCallback(std::function<void()> callback)
{
    mImpl->shutdownCallback = std::move(callback);
}
void RenderCore::addFrameStats(size_t batches, size_t triangles)
{
    mImpl->batches += batches;
    mImpl->triangles += triangles;
}

void RenderCore::setPixelSize(int pixelSize)
{
    mImpl->pixelSize = std::clamp(pixelSize, 1, 16);
    mImpl->targetWidth = 0;
    mImpl->targetHeight = 0;
    mImpl->rebuildMainTargets();
}

void RenderCore::setRenderResolution(int width, int height)
{
    if (width <= 0 || height <= 0) {
        mImpl->targetWidth = 0;
        mImpl->targetHeight = 0;
    }
    else {
        mImpl->targetWidth = std::clamp(width, 64, 4096);
        mImpl->targetHeight = std::clamp(height, 64, 4096);
    }
    mImpl->rebuildMainTargets();
}

void RenderCore::setBackground(glm::vec3 colour)
{
    mImpl->background = colour;
}

void RenderCore::setShadowView(bool enabled,
                               const glm::mat4& lightViewProjection)
{
    mImpl->shadowEnabled = enabled;
    mImpl->shadowViewProjection = lightViewProjection;
}

rhi::TextureHandle RenderCore::shadowTexture() const
{
    return mImpl->shadowDepth;
}

rhi::SamplerHandle RenderCore::shadowSampler() const
{
    return mImpl->shadowSamplerHandle;
}

void RenderCore::setStylizeParams(const StylizeParams& p)
{
    StylizeUniforms& u = mImpl->stylizeUniforms;
    u.shadowColour = glm::vec4(p.shadowColour, 0.0f);
    u.highlightColour = glm::vec4(p.highlightColour, 0.0f);
    u.outlineColour = glm::vec4(p.outlineColour, 0.0f);
    u.clip = {p.nearClip, p.farClip, 0.0f, 0.0f};
    u.toggles = {p.stylize ? 1.0f : 0.0f, p.shadows ? 1.0f : 0.0f,
                 p.highlights ? 1.0f : 0.0f, p.outlines ? 1.0f : 0.0f};
    u.shadow = {p.shadowStrength, p.shadowThreshold, 0.0f, 0.0f};
    u.highlight = {p.highlightStrength, p.highlightThreshold,
                   p.highlightDarkFade, p.highlightColourOverride};
    u.outlineA = {p.outlineOpacity, p.outlineThickness, p.outlineDepthSens,
                  p.outlineNormalSens};
    u.outlineB = {p.outlineSharpness, p.outlineDistFade, p.outlineDarkFade,
                  0.0f};
    u.convex = {p.edgeConvexity, p.edgeConvexBias, 0.0f, 0.0f};
}

void RenderCore::setPostParams(const PostParams& params)
{
    PostConstants& post = mImpl->postConstants;
    post.shadowTint = glm::vec4(params.gradeShadowTint, 0.0f);
    post.midTint = glm::vec4(params.gradeMidTint, 0.0f);
    post.vignetteColour = glm::vec4(params.vignetteColour, 0.0f);
    post.gradeA = {params.gradeDesaturate, params.gradeContrast,
                   params.gradeSaturation, params.gradeTintStrength};
    post.gradeB = {params.gradeBlackLift, params.vignetteStrength,
                   params.grade ? 1.0f : 0.0f, params.dither ? 1.0f : 0.0f};
    post.ditherA = {params.colourDepth, params.ditherBanding,
                    params.ditherDarkFade, 0.0f};
    post.bloom = {params.bloomIntensity, params.bloom ? 1.0f : 0.0f,
                  params.bloomPixelSnap, 0.0f};
    mImpl->bloomEnabled = params.bloom && params.bloomIntensity > 0.0f;
    mImpl->bloomThreshold = std::clamp(params.bloomThreshold, 0.0f, 0.999f);
}

void RenderCore::enableOffscreenViewport(int width, int height)
{
    mImpl->editorWidth = uint32_t(std::max(width, 1));
    mImpl->editorHeight = uint32_t(std::max(height, 1));
    mImpl->rebuildEditorTargets();
}
void RenderCore::resizeOffscreenViewport(int width, int height)
{
    const uint32_t w = uint32_t(std::max(width, 1));
    const uint32_t h = uint32_t(std::max(height, 1));
    if (w == mImpl->editorWidth && h == mImpl->editorHeight)
        return;
    mImpl->editorWidth = w;
    mImpl->editorHeight = h;
    mImpl->rebuildEditorTargets();
}
uint64_t RenderCore::viewportTextureId() const
{
    return mImpl->editorToken;
}
void RenderCore::setOffscreenBackground(float r, float g, float b)
{
    mImpl->editorBackground = {r, g, b};
}
void RenderCore::setEditorCameraPose(float px, float py, float pz, float qw,
                                     float qx, float qy, float qz, float fovDeg)
{
    mImpl->editorView.position = {px, py, pz};
    mImpl->editorView.orientation = glm::normalize(glm::quat(qw, qx, qy, qz));
    mImpl->editorView.fovDeg = std::clamp(fovDeg, 1.0f, 179.0f);
}

void RenderCore::enableThumbnailViewport(int size)
{
    const uint32_t wanted = uint32_t(std::max(size, 32));
    if (wanted == mImpl->thumbnailSize && mImpl->thumbnailColour.valid())
        return;
    mImpl->thumbnailSize = wanted;
    mImpl->thumbnailView.farClip = 100.0f;
    mImpl->rebuildThumbnailTargets();
}
uint64_t RenderCore::thumbnailTextureId() const
{
    return mImpl->thumbnailToken;
}
void RenderCore::setThumbnailCameraPose(float px, float py, float pz, float qw,
                                        float qx, float qy, float qz,
                                        float fovDeg)
{
    mImpl->thumbnailView.position = {px, py, pz};
    mImpl->thumbnailView.orientation =
        glm::normalize(glm::quat(qw, qx, qy, qz));
    mImpl->thumbnailView.fovDeg = std::clamp(fovDeg, 1.0f, 179.0f);
}

RenderCore::TextureBinding
RenderCore::createTexture(const std::string& name, uint32_t width,
                          uint32_t height, const void* rgba,
                          rhi::FilterMode filter, rhi::AddressMode address)
{
    TextureBinding binding;
    if (!mImpl->device || !rgba || width == 0 || height == 0)
        return binding;
    rhi::TextureDesc texture;
    texture.width = width;
    texture.height = height;
    texture.format = rhi::Format::RGBA8Unorm;
    texture.usage = rhi::TextureUsage::Sampled;
    texture.initialData = rgba;
    texture.debugName = name;
    binding.texture = mImpl->device->createTexture(texture);
    rhi::SamplerDesc sampler;
    sampler.minFilter = filter;
    sampler.magFilter = filter;
    sampler.addressU = address;
    sampler.addressV = address;
    binding.sampler = mImpl->device->createSampler(sampler);
    binding.width = width;
    binding.height = height;
    if (!binding.texture.valid() || !binding.sampler.valid()) {
        if (binding.sampler.valid())
            mImpl->device->destroySampler(binding.sampler);
        if (binding.texture.valid())
            mImpl->device->destroyTexture(binding.texture);
        return {};
    }
    binding.token = mImpl->addTexture(binding, true, true);
    return binding;
}

RenderCore::TextureBinding
RenderCore::loadTexture(const std::filesystem::path& path,
                        rhi::FilterMode filter, rhi::AddressMode address)
{
    const std::string key = path.lexically_normal().string() + "|" +
                            std::to_string(int(filter)) + "|" +
                            std::to_string(int(address));
    if (const auto found = mImpl->textureCache.find(key);
        found != mImpl->textureCache.end()) {
        auto entry = mImpl->textures.find(found->second);
        return entry == mImpl->textures.end() ? TextureBinding{}
                                              : entry->second.binding;
    }
    rhi_renderer::Image image;
    if (!rhi_renderer::loadImage(path, image))
        return {};
    TextureBinding binding = createTexture(
        path.filename().string(), uint32_t(image.width), uint32_t(image.height),
        image.rgba.data(), filter, address);
    if (binding.valid())
        mImpl->textureCache[key] = binding.token;
    return binding;
}

RenderCore::TextureBinding RenderCore::fallbackTexture()
{
    if (mImpl->fallback.valid())
        return mImpl->fallback;
    const std::filesystem::path checker =
        assets::resolve("textures/EnginePrototypeSurface.png");
    if (!checker.empty())
        mImpl->fallback = loadTexture(checker, rhi::FilterMode::Nearest,
                                      rhi::AddressMode::Repeat);
    if (!mImpl->fallback.valid()) {
        constexpr std::array<uint8_t, 16> pixels{255, 0,   255, 255, 16, 16,
                                                 16,  255, 16,  16,  16, 255,
                                                 255, 0,   255, 255};
        mImpl->fallback =
            createTexture("renderer.prototype-checker", 2, 2, pixels.data(),
                          rhi::FilterMode::Nearest, rhi::AddressMode::Repeat);
    }
    return mImpl->fallback;
}

bool RenderCore::textureForToken(uint64_t token, TextureBinding& out) const
{
    const auto found = mImpl->textures.find(token);
    if (found == mImpl->textures.end() || !found->second.binding.valid())
        return false;
    out = found->second.binding;
    return true;
}

rhi::Device* RenderCore::device()
{
    return mImpl->device.get();
}
const rhi::Device* RenderCore::device() const
{
    return mImpl->device.get();
}

namespace rhi_texture_registry {
uint64_t load(const std::filesystem::path& path, rhi::FilterMode filter,
              rhi::AddressMode address, int& width, int& height)
{
    width = 0;
    height = 0;
    if (!gActiveCore)
        return 0;
    const RenderCore::TextureBinding binding =
        gActiveCore->loadTexture(path, filter, address);
    width = int(binding.width);
    height = int(binding.height);
    return binding.token;
}
} // namespace rhi_texture_registry

} // namespace eng
