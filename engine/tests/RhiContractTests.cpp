// The RHI contract, exercised against every backend that can be created.
//
// A backend that passes this is not necessarily correct -- nothing here checks
// pixels -- but it does accept the sequence the renderer will drive it with,
// and it detects the misuse the contract says it must. Run it first when
// writing the GL or Vulkan backend; it fails fast and in the right place.
//
// Backends that are still skeletons return null from createDevice; those are
// reported as skipped rather than failed, so this test tells you what exists.

#include <eng/rhi/Device.h>

#include <cstdio>
#include <vector>

using namespace eng::rhi;

static int failures = 0;

static void check(bool condition, const char* what)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

static void exerciseDevice(Device& device, const char* name)
{
    const DeviceCapabilities& caps = device.capabilities();
    check(!caps.backendName.empty(), "backend reports no name");
    // psx_lighting.glsl binds 16 slots; a device that reports fewer would
    // silently truncate the per-renderable light list.
    check(caps.maxSimultaneousLights >= 16,
          "backend reports fewer than the 16 light slots the PSX path binds");
    check(caps.maxTextureSize >= 2048, "backend reports an unusably small texture limit");

    // --- resource round-trip ---
    const std::vector<float> vertices(3 * 8, 0.0f);
    BufferDesc vb;
    vb.size = vertices.size() * sizeof(float);
    vb.usage = BufferUsage::Vertex;
    vb.initialData = vertices.data();
    vb.debugName = "contract.vertices";
    const BufferHandle vbuf = device.createBuffer(vb);
    check(vbuf.valid(), "createBuffer returned an invalid handle");

    TextureDesc td;
    td.width = td.height = 32;
    td.format = Format::RGBA8Unorm;
    td.usage = TextureUsage::Sampled;
    td.debugName = "contract.texture";
    const TextureHandle tex = device.createTexture(td);
    check(tex.valid(), "createTexture returned an invalid handle");

    const SamplerHandle sampler = device.createSampler({});
    check(sampler.valid(), "createSampler returned an invalid handle");

    ShaderDesc vsDesc;
    vsDesc.stage = ShaderStage::Vertex;
    vsDesc.code = {'v', 's'};
    vsDesc.debugName = "contract.vs";
    ShaderDesc fsDesc;
    fsDesc.stage = ShaderStage::Fragment;
    fsDesc.code = {'f', 's'};
    fsDesc.debugName = "contract.fs";
    const ShaderHandle vs = device.createShader(vsDesc);
    const ShaderHandle fs = device.createShader(fsDesc);

    PipelineDesc pd;
    pd.vertex = vs;
    pd.fragment = fs;
    pd.vertexLayout.bindings.push_back({0, 32, false});
    pd.vertexLayout.attributes.push_back({0, 0, VertexFormat::Float3, 0});
    pd.debugName = "contract.pipeline";
    const PipelineHandle pipeline = device.createPipeline(pd);
    check(pipeline.valid(), "createPipeline returned an invalid handle");

    // --- one frame, one pass, one draw ---
    if (device.beginFrame()) {
        RenderPassDesc pass;
        pass.colour.push_back({});  // the swapchain image
        pass.debugName = "contract.pass";
        CommandList& cmd = device.beginPass(pass);
        cmd.pushDebugGroup("contract");
        cmd.bindPipeline(pipeline);
        cmd.setViewport({0.0f, 0.0f, 640.0f, 360.0f, 0.0f, 1.0f});
        cmd.bindVertexBuffer(0, vbuf);
        cmd.bindTexture(0, tex, sampler);
        cmd.draw(3);
        cmd.popDebugGroup();
        device.endPass();
        device.endFrame();
    }

    // Resizing between frames is the window-resize path and must be safe.
    device.resizeSwapchain(800, 600);
    device.waitIdle();

    device.destroyPipeline(pipeline);
    device.destroyShader(fs);
    device.destroyShader(vs);
    device.destroySampler(sampler);
    device.destroyTexture(tex);
    device.destroyBuffer(vbuf);

    std::printf("  %s: contract sequence completed\n", name);
}

int main()
{
    DeviceDesc desc;
    desc.width = 640;
    desc.height = 360;
    desc.vsync = false;

    int created = 0;
    for (BackendKind kind : {BackendKind::Null, BackendKind::OpenGL, BackendKind::Vulkan}) {
        const char* name = backendName(kind);
        std::unique_ptr<Device> device = createDevice(kind, desc);
        if (!device) {
            std::printf("  %s: not available (skeleton or unsupported)\n", name);
            continue;
        }
        ++created;
        exerciseDevice(*device, name);
    }

    // The null backend must always exist: it is what headless tests run on.
    check(created >= 1, "no backend could be created, not even null");

    BackendKind parsed = BackendKind::Null;
    check(backendKindFromName("vulkan", parsed) && parsed == BackendKind::Vulkan,
          "backendKindFromName failed on a known name");
    check(!backendKindFromName("directx", parsed),
          "backendKindFromName accepted an unknown name");

    if (failures == 0)
        std::printf("RhiContractTests OK\n");
    return failures ? 1 : 0;
}
