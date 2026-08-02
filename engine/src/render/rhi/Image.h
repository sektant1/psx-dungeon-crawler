#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

namespace eng::rhi_renderer {

struct Image {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgba;

    bool valid() const {
        return width > 0 && height > 0 &&
               rgba.size() == static_cast<size_t>(width * height * 4);
    }
};

bool loadImage(const std::filesystem::path& path, Image& out);
bool writePng(const std::filesystem::path& path, int width, int height,
              const uint8_t* rgba);

} // namespace eng::rhi_renderer
