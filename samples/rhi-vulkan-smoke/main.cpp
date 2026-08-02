#include <eng/Log.h>
#include <eng/rhi/Device.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr int kUnsupported = 77;

struct Options {
    int frames = 240;
    bool validation = false;
    bool requireVulkan = false;
    bool exerciseResize = false;
};

bool parsePositive(const char* text, int& value)
{
    errno = 0;
    char* end = nullptr;
    const long parsed = std::strtol(text, &end, 10);
    if (errno != 0 || !end || *end != '\0' || parsed <= 0 || parsed > 1'000'000)
        return false;
    value = static_cast<int>(parsed);
    return true;
}

bool parseOptions(int argc, char** argv, Options& options)
{
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--frames" && i + 1 < argc) {
            if (!parsePositive(argv[++i], options.frames))
                return false;
        }
        else if (arg == "--validation") {
            options.validation = true;
        }
        else if (arg == "--require-vulkan") {
            options.requireVulkan = true;
        }
        else if (arg == "--exercise-resize") {
            options.exerciseResize = true;
        }
        else {
            return false;
        }
    }
    return true;
}

std::vector<uint8_t> readBytes(const char* path)
{
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input)
        return {};
    const std::streamsize size = input.tellg();
    if (size <= 0)
        return {};
    input.seekg(0);
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    if (!input.read(reinterpret_cast<char*>(bytes.data()), size))
        return {};
    return bytes;
}

struct Vertex {
    float position[2];
    float colour[3];
};

eng::rhi::PipelineHandle makePipeline(eng::rhi::Device& device,
                                      eng::rhi::ShaderHandle vertex,
                                      eng::rhi::ShaderHandle fragment,
                                      eng::rhi::Format format, const char* name)
{
    eng::rhi::PipelineDesc desc;
    desc.vertex = vertex;
    desc.fragment = fragment;
    desc.vertexLayout.bindings.push_back({0, sizeof(Vertex), false});
    desc.vertexLayout.attributes.push_back(
        {0, 0, eng::rhi::VertexFormat::Float2, offsetof(Vertex, position)});
    desc.vertexLayout.attributes.push_back(
        {1, 0, eng::rhi::VertexFormat::Float3, offsetof(Vertex, colour)});
    desc.cull = eng::rhi::CullMode::None;
    desc.depth.testEnabled = false;
    desc.depth.writeEnabled = false;
    desc.colourFormats = {format};
    desc.depthFormat = eng::rhi::Format::Unknown;
    desc.debugName = name;
    return device.createPipeline(desc);
}

} // namespace

int main(int argc, char** argv)
{
    Options options;
    if (!parseOptions(argc, argv, options)) {
        std::cerr << "usage: rhi_vulkan_smoke [--frames N] [--validation] "
                     "[--require-vulkan] [--exercise-resize]\n";
        return 2;
    }

    std::atomic<int> errorCount{0};
    std::atomic<int> validationFailureCount{0};
    const int sink = eng::log::addSink([&](eng::log::Level level,
                                           const char* message) {
        if (level == eng::log::Level::Error || level == eng::log::Level::Fatal)
            ++errorCount;
        if (options.validation && message &&
            std::string_view(message).find("vulkan validation [") !=
                std::string_view::npos &&
            (level == eng::log::Level::Warn ||
             level == eng::log::Level::Error ||
             level == eng::log::Level::Fatal))
            ++validationFailureCount;
    });

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::cerr << "Vulkan smoke unsupported: SDL video init failed: "
                  << SDL_GetError() << '\n';
        eng::log::removeSink(sink);
        return options.requireVulkan || options.validation ? 1 : kUnsupported;
    }
    SDL_Window* window = SDL_CreateWindow(
        "RHI Vulkan smoke", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 640,
        360,
        SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!window) {
        std::cerr << "Vulkan smoke unsupported: SDL Vulkan window failed: "
                  << SDL_GetError() << '\n';
        SDL_Quit();
        eng::log::removeSink(sink);
        return options.requireVulkan || options.validation ? 1 : kUnsupported;
    }

    int drawableWidth = 0;
    int drawableHeight = 0;
    SDL_Vulkan_GetDrawableSize(window, &drawableWidth, &drawableHeight);
    eng::rhi::DeviceDesc deviceDesc;
    deviceDesc.platformWindow = window;
    deviceDesc.width = static_cast<uint32_t>(std::max(drawableWidth, 1));
    deviceDesc.height = static_cast<uint32_t>(std::max(drawableHeight, 1));
    deviceDesc.vsync = false;
    deviceDesc.enableValidation = options.validation;
    std::unique_ptr<eng::rhi::Device> device =
        eng::rhi::createDevice(eng::rhi::BackendKind::Vulkan, deviceDesc);
    if (!device) {
        std::cerr << "Vulkan smoke unsupported: loader, device, or surface "
                     "initialization failed\n";
        SDL_DestroyWindow(window);
        SDL_Quit();
        eng::log::removeSink(sink);
        return options.requireVulkan || options.validation ? 1 : kUnsupported;
    }

    const std::array<Vertex, 3> vertices{{
        {{-0.72f, -0.62f}, {1.00f, 0.22f, 0.12f}},
        {{0.72f, -0.62f}, {0.12f, 0.85f, 0.38f}},
        {{0.0f, 0.72f}, {0.20f, 0.38f, 1.00f}},
    }};
    const std::array<uint16_t, 3> indices{0, 1, 2};
    std::array<float, 4> transform{0.0f, 0.0f, 1.0f, 1.0f};

    eng::rhi::BufferDesc vertexDesc;
    vertexDesc.size = sizeof(vertices);
    vertexDesc.usage = eng::rhi::BufferUsage::Vertex;
    vertexDesc.initialData = vertices.data();
    vertexDesc.debugName = "smoke.vertices";
    const eng::rhi::BufferHandle vertexBuffer =
        device->createBuffer(vertexDesc);
    eng::rhi::BufferDesc indexDesc;
    indexDesc.size = sizeof(indices);
    indexDesc.usage = eng::rhi::BufferUsage::Index;
    indexDesc.initialData = indices.data();
    indexDesc.debugName = "smoke.indices";
    const eng::rhi::BufferHandle indexBuffer = device->createBuffer(indexDesc);
    eng::rhi::BufferDesc uniformDesc;
    uniformDesc.size = sizeof(transform);
    uniformDesc.usage =
        eng::rhi::BufferUsage::Uniform | eng::rhi::BufferUsage::Dynamic;
    uniformDesc.initialData = transform.data();
    uniformDesc.debugName = "smoke.transform";
    const eng::rhi::BufferHandle uniformBuffer =
        device->createBuffer(uniformDesc);

    std::array<uint8_t, 16> checker{255, 255, 255, 255, 90,  45,  140, 255,
                                    90,  45,  140, 255, 255, 255, 255, 255};
    eng::rhi::TextureDesc checkerDesc;
    checkerDesc.width = 2;
    checkerDesc.height = 2;
    checkerDesc.format = eng::rhi::Format::RGBA8Unorm;
    checkerDesc.usage =
        eng::rhi::TextureUsage::Sampled | eng::rhi::TextureUsage::Readback;
    checkerDesc.initialData = checker.data();
    checkerDesc.debugName = "smoke.checker";
    const eng::rhi::TextureHandle checkerTexture =
        device->createTexture(checkerDesc);
    std::array<uint8_t, 16> checkerReadback{};
    device->readTexture(checkerTexture, checkerReadback.data(),
                        checkerReadback.size());
    const bool checkerReadbackValid = checkerReadback == checker;
    std::swap(checker[0], checker[4]);
    device->updateTexture(checkerTexture, checker.data(), checker.size());

    eng::rhi::TextureDesc offscreenDesc;
    offscreenDesc.width = 128;
    offscreenDesc.height = 128;
    offscreenDesc.format = eng::rhi::Format::RGBA8Unorm;
    offscreenDesc.usage = eng::rhi::TextureUsage::RenderTarget |
                          eng::rhi::TextureUsage::Sampled |
                          eng::rhi::TextureUsage::Readback;
    offscreenDesc.debugName = "smoke.offscreen";
    const eng::rhi::TextureHandle offscreen =
        device->createTexture(offscreenDesc);
    const eng::rhi::SamplerHandle sampler = device->createSampler({});

    eng::rhi::ShaderDesc vertexShaderDesc;
    vertexShaderDesc.stage = eng::rhi::ShaderStage::Vertex;
    vertexShaderDesc.code = readBytes(ENG_SMOKE_VERT_SPV);
    vertexShaderDesc.debugName = "smoke.triangle.vert";
    eng::rhi::ShaderDesc fragmentShaderDesc;
    fragmentShaderDesc.stage = eng::rhi::ShaderStage::Fragment;
    fragmentShaderDesc.code = readBytes(ENG_SMOKE_FRAG_SPV);
    fragmentShaderDesc.debugName = "smoke.triangle.frag";
    const eng::rhi::ShaderHandle vertexShader =
        device->createShader(vertexShaderDesc);
    const eng::rhi::ShaderHandle fragmentShader =
        device->createShader(fragmentShaderDesc);
    const eng::rhi::PipelineHandle offscreenPipeline =
        makePipeline(*device, vertexShader, fragmentShader,
                     eng::rhi::Format::RGBA8Unorm, "smoke.offscreen-pipeline");
    const eng::rhi::PipelineHandle swapchainPipeline =
        makePipeline(*device, vertexShader, fragmentShader,
                     device->swapchainFormat(), "smoke.swapchain-pipeline");

    const bool resourcesValid =
        vertexBuffer.valid() && indexBuffer.valid() && uniformBuffer.valid() &&
        checkerTexture.valid() && offscreen.valid() && sampler.valid() &&
        vertexShader.valid() && fragmentShader.valid() &&
        offscreenPipeline.valid() && swapchainPipeline.valid() &&
        checkerReadbackValid;

    int presented = 0;
    int attempts = 0;
    bool quit = false;
    bool churnValid = true;
    while (resourcesValid && !quit && presented < options.frames &&
           attempts < options.frames * 100) {
        ++attempts;
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT)
                quit = true;
            if (event.type == SDL_WINDOWEVENT &&
                event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
                device->resizeSwapchain(event.window.data1, event.window.data2);
        }
        if (options.exerciseResize && presented == options.frames / 3) {
            SDL_SetWindowSize(window, 704, 396);
            device->resizeSwapchain(704, 396);
        }
        if (options.exerciseResize && presented == (options.frames * 2) / 3) {
            SDL_SetWindowSize(window, 640, 360);
            device->resizeSwapchain(640, 360);
        }

        const std::array<float, 2> offset{(presented & 1) ? 0.0025f : -0.0025f,
                                          0.0f};
        device->updateBuffer(uniformBuffer, offset.data(), sizeof(offset), 0);

        eng::rhi::BufferHandle churnBuffer;
        eng::rhi::TextureHandle churnTexture;
        if ((presented % 17) == 0) {
            eng::rhi::BufferDesc churnDesc;
            churnDesc.size = 64;
            churnDesc.usage =
                eng::rhi::BufferUsage::Uniform | eng::rhi::BufferUsage::Dynamic;
            churnDesc.debugName = "smoke.churn-buffer";
            churnBuffer = device->createBuffer(churnDesc);
            churnValid = churnValid && churnBuffer.valid();
        }
        if ((presented % 61) == 0) {
            eng::rhi::TextureDesc churnDesc;
            churnDesc.width = churnDesc.height = 4;
            churnDesc.format = eng::rhi::Format::BGRA8Srgb;
            churnDesc.usage = eng::rhi::TextureUsage::Sampled;
            churnDesc.debugName = "smoke.churn-texture";
            churnTexture = device->createTexture(churnDesc);
            churnValid = churnValid && churnTexture.valid();
        }
        if (!device->beginFrame()) {
            if (churnTexture.valid())
                device->destroyTexture(churnTexture);
            if (churnBuffer.valid())
                device->destroyBuffer(churnBuffer);
            SDL_Delay(1);
            continue;
        }
        if (churnTexture.valid())
            device->destroyTexture(churnTexture);
        if (churnBuffer.valid())
            device->destroyBuffer(churnBuffer);

        const std::array<float, 2> scale{1.0f + ((presented % 3) * 0.0005f),
                                         1.0f};
        device->updateBuffer(uniformBuffer, scale.data(), sizeof(scale),
                             2 * sizeof(float));

        std::array<float, 32> drawConstants{};
        drawConstants[0] = 1.0f;
        drawConstants[1] = 0.92f;
        drawConstants[2] = 0.82f;
        drawConstants[3] = 1.0f;
        eng::rhi::RenderPassDesc offscreenPass;
        eng::rhi::ColourAttachment offscreenColour;
        offscreenColour.texture = offscreen;
        offscreenColour.clearColour[0] = 0.16f;
        offscreenColour.clearColour[1] = 0.04f;
        offscreenColour.clearColour[2] = 0.22f;
        offscreenPass.colour.push_back(offscreenColour);
        offscreenPass.debugName = "smoke.offscreen-pass";
        eng::rhi::CommandList& offscreenCommands =
            device->beginPass(offscreenPass);
        offscreenCommands.pushDebugGroup("triangle-to-offscreen");
        offscreenCommands.bindPipeline(offscreenPipeline);
        offscreenCommands.setViewport({0, 0, 128, 128, 0, 1});
        offscreenCommands.setScissor({0, 0, 128, 128});
        offscreenCommands.bindVertexBuffer(0, vertexBuffer);
        offscreenCommands.bindIndexBuffer(indexBuffer, 0,
                                          eng::rhi::IndexType::UInt16);
        offscreenCommands.bindUniformBuffer(0, uniformBuffer);
        offscreenCommands.bindUniformBuffer(0, uniformBuffer);
        offscreenCommands.bindTexture(0, checkerTexture, sampler);
        offscreenCommands.bindTexture(0, checkerTexture, sampler);
        offscreenCommands.pushConstants(drawConstants.data(),
                                        sizeof(drawConstants));
        offscreenCommands.drawIndexed(3);
        offscreenCommands.popDebugGroup();
        device->endPass();

        SDL_Vulkan_GetDrawableSize(window, &drawableWidth, &drawableHeight);
        eng::rhi::RenderPassDesc swapchainPass;
        eng::rhi::ColourAttachment swapchainColour;
        swapchainColour.clearColour[0] = 0.015f;
        swapchainColour.clearColour[1] = 0.02f;
        swapchainColour.clearColour[2] = 0.035f;
        swapchainPass.colour.push_back(swapchainColour);
        swapchainPass.debugName = "smoke.swapchain-pass";
        eng::rhi::CommandList& swapchainCommands =
            device->beginPass(swapchainPass);
        swapchainCommands.pushDebugGroup("deterministic-triangle");
        swapchainCommands.bindPipeline(swapchainPipeline);
        swapchainCommands.setViewport({0, 0, static_cast<float>(drawableWidth),
                                       static_cast<float>(drawableHeight), 0,
                                       1});
        swapchainCommands.setScissor({0, 0,
                                      static_cast<uint32_t>(drawableWidth),
                                      static_cast<uint32_t>(drawableHeight)});
        swapchainCommands.bindVertexBuffer(0, vertexBuffer);
        swapchainCommands.bindUniformBuffer(0, uniformBuffer);
        swapchainCommands.bindTexture(0, offscreen, sampler);
        swapchainCommands.pushConstants(drawConstants.data(),
                                        sizeof(drawConstants));
        swapchainCommands.draw(3);
        swapchainCommands.popDebugGroup();
        device->endPass();
        device->endFrame();
        ++presented;
    }

    device->waitIdle();
    std::vector<uint8_t> offscreenReadback(128u * 128u * 4u);
    bool offscreenReadbackValid = false;
    if (offscreen.valid() && presented > 0) {
        device->readTexture(offscreen, offscreenReadback.data(),
                            offscreenReadback.size());
        const auto nearByte = [](uint8_t actual, uint8_t expected) {
            return std::abs(int(actual) - int(expected)) <= 2;
        };
        offscreenReadbackValid = nearByte(offscreenReadback[0], 41) &&
                                 nearByte(offscreenReadback[1], 10) &&
                                 nearByte(offscreenReadback[2], 56) &&
                                 nearByte(offscreenReadback[3], 255);
    }
    device->destroyPipeline(swapchainPipeline);
    device->destroyPipeline(offscreenPipeline);
    device->destroyShader(fragmentShader);
    device->destroyShader(vertexShader);
    device->destroySampler(sampler);
    device->destroyTexture(offscreen);
    device->destroyTexture(checkerTexture);
    device->destroyBuffer(uniformBuffer);
    device->destroyBuffer(indexBuffer);
    device->destroyBuffer(vertexBuffer);
    device.reset();

    SDL_DestroyWindow(window);
    SDL_Quit();
    const int finalErrors = errorCount.load();
    const int validationFailures = validationFailureCount.load();
    eng::log::removeSink(sink);

    if (!resourcesValid || presented != options.frames || !churnValid ||
        !offscreenReadbackValid || finalErrors != 0 ||
        (options.validation && validationFailures != 0)) {
        std::cerr << "Vulkan smoke failed: resources=" << resourcesValid
                  << ", frames=" << presented << '/' << options.frames
                  << ", churn=" << churnValid
                  << ", render-target-readback=" << offscreenReadbackValid
                  << ", errors=" << finalErrors
                  << ", validation-warnings/errors=" << validationFailures
                  << '\n';
        return 1;
    }
    std::cout << "rhi_vulkan_smoke OK: " << presented << " frames\n";
    return 0;
}
