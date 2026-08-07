#pragma once

#include <eng/rhi/Types.h>

#include <memory>

namespace eng::rhi {

class CommandList;

// Recorded work for one pass. Obtained from Device::beginPass, ended with
// Device::endPass; commands are backend-recorded and submitted at endFrame.
//
// Kept small on purpose. Everything here maps onto something all three
// backends (null, GL, Vulkan) can express without emulation; anything that
// needs emulation on one of them belongs above the RHI, not in it.
class CommandList {
public:
    virtual ~CommandList() = default;

    virtual void bindPipeline(PipelineHandle) = 0;
    virtual void setViewport(const Viewport&) = 0;
    virtual void setScissor(const Rect&) = 0;

    virtual void bindVertexBuffer(uint32_t binding, BufferHandle,
                                  uint64_t offset = 0) = 0;
    virtual void bindIndexBuffer(BufferHandle, uint64_t offset = 0,
                                 IndexType type = IndexType::UInt32) = 0;
    // Fixed descriptor ABI: uniform slot N is set 0/binding N in Vulkan;
    // texture slot N is set 1/binding N. Other backends expose the same slots
    // without exposing backend-specific descriptor types.
    virtual void bindUniformBuffer(uint32_t slot, BufferHandle,
                                   uint64_t offset = 0, uint64_t size = 0) = 0;
    virtual void bindTexture(uint32_t slot, TextureHandle, SamplerHandle) = 0;
    // Small per-draw constants. Backends that lack push constants emulate with
    // a ring-buffered uniform block; keep this well under 128 bytes.
    virtual void pushConstants(const void* data, uint32_t size) = 0;

    virtual void draw(uint32_t vertexCount, uint32_t instanceCount = 1,
                      uint32_t firstVertex = 0, uint32_t firstInstance = 0) = 0;
    virtual void drawIndexed(uint32_t indexCount, uint32_t instanceCount = 1,
                             uint32_t firstIndex = 0, int32_t vertexOffset = 0,
                             uint32_t firstInstance = 0) = 0;

    // Debug marker, surfaced in RenderDoc captures. No-op when unsupported.
    virtual void pushDebugGroup(const char* name) = 0;
    virtual void popDebugGroup() = 0;
};

// A graphics device and its swapchain. One per window.
//
// Ownership rule: every create* returns a handle the caller destroys with the
// matching destroy*. Handles are generational, so using a destroyed one is
// detected and logged rather than being undefined behaviour -- a backend must
// not dereference a stale handle.
//
// Threading rule: unless a backend advertises otherwise, every method is to be
// called from the thread that created the device. Resource creation off-thread
// is a later addition and needs an explicit capability.
class Device {
public:
    virtual ~Device() = default;

    virtual const DeviceCapabilities& capabilities() const = 0;

    // --- frame lifecycle ---
    // beginFrame acquires a swapchain image; false means the frame must be
    // skipped (window minimised, swapchain out of date). endFrame presents.
    virtual bool beginFrame() = 0;
    virtual void endFrame() = 0;
    // Call when the window resizes: recreates swapchain-sized resources.
    virtual void resizeSwapchain(uint32_t width, uint32_t height) = 0;
    virtual Format swapchainFormat() const = 0;

    // --- passes ---
    // The returned list is owned by the device and valid until endPass.
    virtual CommandList& beginPass(const RenderPassDesc&) = 0;
    virtual void endPass() = 0;

    // --- resources ---
    virtual BufferHandle createBuffer(const BufferDesc&) = 0;
    virtual void destroyBuffer(BufferHandle) = 0;
    // Writes `size` bytes at `offset`. For BufferUsage::Dynamic this is the
    // per-frame update path and must not stall. Static-buffer writes are
    // blocking and valid only outside beginFrame/endFrame.
    virtual void updateBuffer(BufferHandle, const void* data, uint64_t size,
                              uint64_t offset = 0) = 0;

    virtual TextureHandle createTexture(const TextureDesc&) = 0;
    virtual void destroyTexture(TextureHandle) = 0;
    // Texture writes are blocking and valid only outside beginFrame/endFrame.
    virtual void updateTexture(TextureHandle, const void* data, uint64_t size,
                               uint32_t mipLevel = 0) = 0;
    // Blocking, tightly packed readback for textures created with Readback.
    // Valid only outside beginFrame/endFrame; swapchain images are not handles
    // and cannot be read through this method.
    virtual void readTexture(TextureHandle, void* destination, uint64_t size,
                             uint32_t mipLevel = 0) = 0;

    virtual SamplerHandle createSampler(const SamplerDesc&) = 0;
    virtual void destroySampler(SamplerHandle) = 0;

    virtual ShaderHandle createShader(const ShaderDesc&) = 0;
    virtual void destroyShader(ShaderHandle) = 0;

    virtual PipelineHandle createPipeline(const PipelineDesc&) = 0;
    virtual void destroyPipeline(PipelineHandle) = 0;

    // Blocks until the GPU is idle. For teardown and for capture tooling; not
    // a frame-loop call.
    virtual void waitIdle() = 0;
};

struct DeviceDesc {
    void* platformWindow = nullptr; // SDL_Window*, the surface is built from it
    uint32_t width = 0;
    uint32_t height = 0;
    bool vsync = true;
    // Validation layers / debug output. Costs performance; off in release.
    bool enableValidation = false;
};

// Creates the device for `kind`. Returns null and logs when the backend is not
// compiled in, or is a skeleton that cannot yet draw -- which is the state the
// GL and Vulkan backends ship in today. Callers must check.
std::unique_ptr<Device> createDevice(BackendKind kind, const DeviceDesc&);

// Parses a backend name ("null", "opengl"/"gl", "vulkan"). Returns false on an
// unknown name so the caller can report it instead of silently defaulting.
bool backendKindFromName(const std::string& name, BackendKind& out);
const char* backendName(BackendKind);

} // namespace eng::rhi
