#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace eng {
class Renderer;

// Cached material thumbnail renderer for editor inspectors. It owns an isolated
// Ogre scene, renders only when the material changes, and exposes the GL texture
// id for ImGui::Image.
class MaterialPreview {
public:
    explicit MaterialPreview(Renderer& r, int size = 96);
    ~MaterialPreview();

    void render(const std::string& materialName);
    uint64_t textureId() const;
    int size() const;

private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};
} // namespace eng
