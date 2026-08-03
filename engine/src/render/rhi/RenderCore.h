#pragma once

#include <eng/rhi/Device.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>

namespace eng {

class RenderCore {
public:
    // Shadow is a depth-only pass from the sun's point of view, drawn before
    // Main so the scene can sample it. The renderer treats it as another view
    // so caster selection stays in the one place that knows what a mesh is.
    enum class SceneTarget { Main, Editor, Thumbnail, Shadow };

    struct View {
        SceneTarget target = SceneTarget::Main;
        glm::vec3 position{0.0f};
        glm::quat orientation{1.0f, 0.0f, 0.0f, 0.0f};
        float fovDeg = 70.0f;
        float nearClip = 0.05f;
        float farClip = 4000.0f;
    };

    struct TextureBinding {
        rhi::TextureHandle texture{};
        rhi::SamplerHandle sampler{};
        uint64_t token = 0;
        uint32_t width = 0;
        uint32_t height = 0;

        bool valid() const {
            return texture.valid() && sampler.valid() && token != 0;
        }
    };

    // Colour grade / vignette / ordered dither, applied by the upscale pass.
    // Mirrors the legacy Engine/Psx/DitherPost compositor material; defaults
    // are neutral so an unset palette leaves the pass a plain blit.
    struct PostParams {
        bool grade = false;
        float gradeDesaturate = 0.0f;
        float gradeContrast = 1.0f;
        float gradeSaturation = 1.0f;
        float gradeTintStrength = 0.0f;
        float gradeBlackLift = 0.0f;
        glm::vec3 gradeShadowTint{0.0f};
        glm::vec3 gradeMidTint{0.0f};
        float vignetteStrength = 0.0f;
        glm::vec3 vignetteColour{1.0f};
        bool dither = false;
        float colourDepth = 255.0f;
        float ditherBanding = 0.0f;
        float ditherDarkFade = 0.0f;
        bool bloom = false;
        float bloomThreshold = 0.7f;
        float bloomIntensity = 0.0f;
        // 1 = sample the glow at scene-texel centres, keeping it on the pixel
        // grid; 0 = smooth bilinear, which the PS2/GameCube profiles want.
        float bloomPixelSnap = 0.0f;
    };

    // Pixel-art edge pass, mirroring Engine/Psx/PixelStylize. Defaults are
    // inert (stylize off), so an unset palette leaves the pass a pass-through.
    struct StylizeParams {
        bool stylize = false;
        bool shadows = false;
        bool highlights = false;
        bool outlines = false;
        float shadowStrength = 0.0f;
        float shadowThreshold = 0.0f;
        glm::vec3 shadowColour{0.0f};
        float highlightStrength = 0.0f;
        float highlightThreshold = 0.0f;
        float highlightDarkFade = 0.0f;
        float highlightColourOverride = 0.0f;
        glm::vec3 highlightColour{1.0f};
        float outlineOpacity = 0.0f;
        float outlineThickness = 1.0f;
        float outlineDepthSens = 0.0f;
        float outlineNormalSens = 0.0f;
        float outlineSharpness = 0.0f;
        float outlineDistFade = 0.0f;
        float outlineDarkFade = 0.0f;
        glm::vec3 outlineColour{0.0f};
        float edgeConvexity = 0.0f;
        float edgeConvexBias = 0.0f;
        float nearClip = 0.05f;
        float farClip = 4000.0f;
    };

    using DrawScene = std::function<void(rhi::CommandList&, const View&,
                                         uint32_t, uint32_t)>;

    RenderCore();
    ~RenderCore();

    bool init(uintptr_t nativeWindowHandle, void* sdlWindow, int width,
              int height, const std::string& title, bool vsync);
    void shutdown();
    void renderFrame(float dt);
    void onResize(int width, int height);
    void beginImGuiFrame(float dt);
    bool imguiReady() const;
    void writeScreenshot(const std::string& path);
    void frameStats(size_t& batches, size_t& triangles) const;

    void setDrawScene(DrawScene draw);
    void setShutdownCallback(std::function<void()> callback);
    void addFrameStats(size_t batches, size_t triangles);

    void setPixelSize(int pixelSize);
    void setRenderResolution(int width, int height);
    void setBackground(glm::vec3 colour);
    void setPostParams(const PostParams& params);
    // The sun's view-projection for the shadow pass, and whether to run it.
    void setShadowView(bool enabled, const glm::mat4& lightViewProjection);
    rhi::TextureHandle shadowTexture() const;
    rhi::SamplerHandle shadowSampler() const;
    void setStylizeParams(const StylizeParams& params);

    void enableOffscreenViewport(int width, int height);
    void resizeOffscreenViewport(int width, int height);
    uint64_t viewportTextureId() const;
    void setOffscreenBackground(float r, float g, float b);
    void setEditorCameraPose(float px, float py, float pz, float qw, float qx,
                             float qy, float qz, float fovDeg);

    void enableThumbnailViewport(int size);
    uint64_t thumbnailTextureId() const;
    void setThumbnailCameraPose(float px, float py, float pz, float qw,
                                float qx, float qy, float qz, float fovDeg);

    TextureBinding loadTexture(const std::filesystem::path& path,
                               rhi::FilterMode filter,
                               rhi::AddressMode address);
    TextureBinding createTexture(const std::string& name, uint32_t width,
                                 uint32_t height, const void* rgba,
                                 rhi::FilterMode filter,
                                 rhi::AddressMode address);
    TextureBinding fallbackTexture();
    bool textureForToken(uint64_t token, TextureBinding& out) const;

    rhi::Device* device();
    const rhi::Device* device() const;

private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};

namespace rhi_texture_registry {
uint64_t load(const std::filesystem::path& path, rhi::FilterMode filter,
              rhi::AddressMode address, int& width, int& height);
}

} // namespace eng
