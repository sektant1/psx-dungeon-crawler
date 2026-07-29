#pragma once

#include <eng/Handles.h>

#include <cstdint>
#include <string>
#include <vector>

// Render Hardware Interface: the device-level contract every graphics backend
// implements. Nothing in eng::rhi names a graphics API -- no GL, no Vulkan, no
// OGRE type appears in any of these headers, which is the whole point: code
// above the RHI compiles once and runs on whichever backend is plugged in.
//
// The engine currently renders through OGRE and does not go through this
// contract yet. See docs/design/2026-07-29-rhi-and-module-contracts.md.
namespace eng::rhi {

// Resources are generational handles, never pointers: a backend is free to
// move, pool or recreate the objects behind them (Vulkan device loss, GL
// context recreation) without invalidating anything a caller stored.
struct BufferHandle   { uint32_t id = 0; uint32_t gen = 0; bool valid() const { return id != 0; } };
struct TextureHandle  { uint32_t id = 0; uint32_t gen = 0; bool valid() const { return id != 0; } };
struct SamplerHandle  { uint32_t id = 0; uint32_t gen = 0; bool valid() const { return id != 0; } };
struct ShaderHandle   { uint32_t id = 0; uint32_t gen = 0; bool valid() const { return id != 0; } };
struct PipelineHandle { uint32_t id = 0; uint32_t gen = 0; bool valid() const { return id != 0; } };
struct RenderPassHandle { uint32_t id = 0; uint32_t gen = 0; bool valid() const { return id != 0; } };

enum class BackendKind {
    Null,   // records and validates, draws nothing; for headless tests
    OpenGL,
    Vulkan,
};

// Texel formats. Deliberately short: this is the set the PSX pipeline actually
// uses (colour, HDR bloom targets, depth). Add a format when a pass needs it,
// not before -- every entry is one more thing each backend must map.
enum class Format {
    Unknown,
    RGBA8Unorm,
    RGBA8Srgb,
    BGRA8Unorm,
    R8Unorm,
    RG16Float,
    RGBA16Float,
    Depth24Stencil8,
    Depth32Float,
};

enum class BufferUsage : uint32_t {
    Vertex  = 1u << 0,
    Index   = 1u << 1,
    Uniform = 1u << 2,
    Storage = 1u << 3,
    // The backend may keep this in host-visible memory; expect writes every
    // frame and reads only by the GPU.
    Dynamic = 1u << 4,
};
constexpr BufferUsage operator|(BufferUsage a, BufferUsage b)
{
    return BufferUsage(uint32_t(a) | uint32_t(b));
}
constexpr bool any(BufferUsage mask, BufferUsage bit)
{
    return (uint32_t(mask) & uint32_t(bit)) != 0;
}

enum class TextureUsage : uint32_t {
    Sampled      = 1u << 0,
    RenderTarget = 1u << 1,
    DepthStencil = 1u << 2,
};
constexpr TextureUsage operator|(TextureUsage a, TextureUsage b)
{
    return TextureUsage(uint32_t(a) | uint32_t(b));
}
constexpr bool any(TextureUsage mask, TextureUsage bit)
{
    return (uint32_t(mask) & uint32_t(bit)) != 0;
}

enum class ShaderStage { Vertex, Fragment, Compute };
enum class PrimitiveTopology { TriangleList, TriangleStrip, LineList };
enum class CullMode { None, Front, Back };
enum class CompareOp { Never, Less, Equal, LessEqual, Greater, NotEqual, GreaterEqual, Always };
enum class BlendMode { Opaque, AlphaBlend, Additive, Modulate };
// Nearest is not a fallback here: the PSX look depends on unfiltered texels.
enum class FilterMode { Nearest, Linear };
enum class AddressMode { Repeat, ClampToEdge, MirrorRepeat };

enum class VertexFormat { Float1, Float2, Float3, Float4, UByte4Unorm };

struct VertexAttribute {
    uint32_t location = 0;
    uint32_t binding = 0;
    VertexFormat format = VertexFormat::Float3;
    uint32_t offset = 0;
};

struct VertexBinding {
    uint32_t binding = 0;
    uint32_t stride = 0;
    bool perInstance = false;
};

struct VertexLayout {
    std::vector<VertexBinding> bindings;
    std::vector<VertexAttribute> attributes;
};

struct BufferDesc {
    uint64_t size = 0;
    BufferUsage usage = BufferUsage::Vertex;
    const void* initialData = nullptr; // optional; size bytes
    std::string debugName;
};

struct TextureDesc {
    uint32_t width = 1;
    uint32_t height = 1;
    uint32_t depth = 1;
    uint32_t mipLevels = 1;
    Format format = Format::RGBA8Unorm;
    TextureUsage usage = TextureUsage::Sampled;
    const void* initialData = nullptr; // optional; tightly packed mip 0
    std::string debugName;
};

struct SamplerDesc {
    FilterMode minFilter = FilterMode::Nearest;
    FilterMode magFilter = FilterMode::Nearest;
    AddressMode addressU = AddressMode::Repeat;
    AddressMode addressV = AddressMode::Repeat;
    float maxAnisotropy = 1.0f;
};

struct ShaderDesc {
    ShaderStage stage = ShaderStage::Vertex;
    // Source text for backends that compile at load (GL), or SPIR-V bytes for
    // those that do not. A backend that cannot consume what it is given must
    // fail at creation with a clear message rather than at first draw.
    std::vector<uint8_t> code;
    std::string entryPoint = "main";
    std::string debugName;
};

struct DepthState {
    bool testEnabled = true;
    bool writeEnabled = true;
    CompareOp compare = CompareOp::LessEqual;
};

struct PipelineDesc {
    ShaderHandle vertex;
    ShaderHandle fragment;
    VertexLayout vertexLayout;
    PrimitiveTopology topology = PrimitiveTopology::TriangleList;
    CullMode cull = CullMode::Back;
    DepthState depth;
    BlendMode blend = BlendMode::Opaque;
    // Formats the pipeline renders into; must match the pass it is used with.
    std::vector<Format> colourFormats{Format::RGBA8Unorm};
    Format depthFormat = Format::Depth24Stencil8;
    std::string debugName;
};

struct ColourAttachment {
    TextureHandle texture; // invalid = the swapchain's current image
    bool clear = true;
    float clearColour[4] = {0.0f, 0.0f, 0.0f, 1.0f};
};

struct DepthAttachment {
    TextureHandle texture;
    bool clear = true;
    float clearDepth = 1.0f;
    uint8_t clearStencil = 0;
};

// One render pass: what is drawn into and how it starts. Pass objects are
// described per frame rather than cached, because the PSX chain's targets are
// sized from the window and change with it.
struct RenderPassDesc {
    std::vector<ColourAttachment> colour;
    DepthAttachment depth;
    std::string debugName;
};

struct Viewport {
    float x = 0.0f, y = 0.0f;
    float width = 0.0f, height = 0.0f;
    float minDepth = 0.0f, maxDepth = 1.0f;
};

struct Rect {
    int32_t x = 0, y = 0;
    uint32_t width = 0, height = 0;
};

// What a backend can do. Queried rather than assumed: the renderer above has
// to degrade honestly on a device that lacks something, not crash on first use.
struct DeviceCapabilities {
    std::string deviceName;
    std::string backendName;
    uint32_t maxTextureSize = 0;
    uint32_t maxColourAttachments = 1;
    uint32_t maxSimultaneousLights = 0; // psx_lighting.glsl wants 16
    bool supportsCompute = false;
    bool supportsAnisotropicFiltering = false;
    bool supportsSrgbFramebuffer = false;
};

} // namespace eng::rhi
