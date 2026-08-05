#include "Image.h"

#include <eng/Log.h>
#include <eng/assets/AssetRoot.h>
#include <eng/content/TextureAsset.h>

// Which decoders exist is decided in engine/src/platform/ImageDecode.cpp, the
// translation unit that compiles stb_image. A STBI_ONLY_* here would select
// nothing and only read as though it did.
#include <stb_image.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <system_error>

namespace eng::rhi_renderer {

namespace {

// The conditioned form, when the Asset Conditioning Pipeline produced one.
// mip 0 only: this uploader takes a single level, and a .rtex carries the whole
// chain for the day it does not. The win here is that stb_image stops decoding
// a PNG per texture at load -- 286 of them in this content tree.
bool loadConditionedImage(const std::filesystem::path& path, Image& out)
{
    const std::filesystem::path conditioned = assets::conditioned(path);
    if (conditioned.empty() ||
        conditioned.extension() != content::kTextureAssetExtension)
        return false;

    content::TextureAsset asset;
    std::string error;
    if (!content::readTextureAsset(conditioned, asset, error)) {
        log::warn("acp: cannot read '%s': %s; decoding the source image",
                  conditioned.string().c_str(), error.c_str());
        return false;
    }
    if (asset.levels.empty())
        return false;

    const content::TextureLevel& level = asset.levels.front();
    out.width = static_cast<int>(level.width);
    out.height = static_cast<int>(level.height);
    out.rgba = level.bytes;
    return out.valid();
}

} // namespace

bool loadImage(const std::filesystem::path& path, Image& out)
{
    out = {};
    if (loadConditionedImage(path, out))
        return true;

    int channels = 0;
    stbi_uc* pixels = stbi_load(path.string().c_str(), &out.width, &out.height,
                                &channels, STBI_rgb_alpha);
    if (!pixels) {
        // Not "cannot decode PNG": the path is whatever it is, and naming the
        // wrong format sent the last reader looking for a corrupt PNG when the
        // file was a perfectly good JPEG.
        log::error("RHI renderer: cannot decode image '%s': %s",
                   path.string().c_str(), stbi_failure_reason());
        return false;
    }
    const size_t size = static_cast<size_t>(out.width) * out.height * 4u;
    out.rgba.assign(pixels, pixels + size);
    stbi_image_free(pixels);
    return out.valid();
}

bool writePng(const std::filesystem::path& path, int width, int height,
              const uint8_t* rgba)
{
    if (!rgba || width <= 0 || height <= 0)
        return false;
    std::error_code error;
    if (!path.parent_path().empty())
        std::filesystem::create_directories(path.parent_path(), error);
    if (error) {
        log::error("RHI renderer: cannot create screenshot directory '%s': %s",
                   path.parent_path().string().c_str(), error.message().c_str());
        return false;
    }
    if (!stbi_write_png(path.string().c_str(), width, height, 4, rgba,
                        width * 4)) {
        log::error("RHI renderer: cannot write screenshot '%s'",
                   path.string().c_str());
        return false;
    }
    return true;
}

} // namespace eng::rhi_renderer
