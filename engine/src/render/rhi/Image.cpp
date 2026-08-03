#include "Image.h"

#include <eng/Log.h>

#define STBI_ONLY_PNG
#include <stb_image.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <system_error>

namespace eng::rhi_renderer {

bool loadImage(const std::filesystem::path& path, Image& out)
{
    out = {};
    int channels = 0;
    stbi_uc* pixels = stbi_load(path.string().c_str(), &out.width, &out.height,
                                &channels, STBI_rgb_alpha);
    if (!pixels) {
        log::error("RHI renderer: cannot decode PNG '%s': %s",
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
