#pragma once

#include <glm/glm.hpp>

#include <string>
#include <string_view>
#include <vector>

struct ImDrawList;

namespace eng::ui {

// A 1-bit pixel font baked into a texture grid by tools/gen_font_atlas.py.
//
// The engine already had text-from-the-imgui-atlas (Renderer::attachTextSprite),
// but that atlas is baked at one size and blurs when a HUD asks for 24px. A
// shipped bitmap atlas magnified by an integer factor with nearest filtering
// stays crisp at any scale, which is the whole point of the retro UI.
//
// Metrics are asset data, not face data: measurement is identical on every
// machine and does not depend on a TTF being installed.
class BitmapFont {
public:
    ~BitmapFont();

    // `definition` is the TOML filename inside engine/assets/fonts, e.g.
    // "ui_regular.toml". Both it and the PNG resolve through Ogre's resource
    // group, so no path handling is needed at the call site.
    bool load(const std::string& definition);
    bool valid() const { return mTextureId != 0; }

    int lineHeight() const { return mLineHeight; }
    int ascent() const { return mAscent; }
    int advance(char c) const;

    // Width/height in font pixels. Handles '\n'.
    glm::ivec2 measure(std::string_view text) const;

    // Greedy word wrap to `maxWidth` font pixels. Returns the wrapped lines.
    std::vector<std::string> wrap(std::string_view text, int maxWidth) const;
    // Fits one line and appends "..." when truncation is necessary.
    std::string ellipsize(std::string_view text, int maxWidth) const;

    // `origin` is the top-left in screen pixels; every glyph quad is placed on
    // an integer screen pixel so the magnified texels never shimmer.
    void draw(ImDrawList* list, glm::vec2 origin, float scale,
              std::string_view text, unsigned int colour,
              unsigned int outline = 0u) const;

private:
    void drawPlain(ImDrawList* list, glm::vec2 origin, float scale,
                   std::string_view text, unsigned int colour) const;

    std::string mTextureName;
    unsigned long long mTextureId = 0; // GL name; ImTextureID on our backend
    int mFirst = 32;
    int mColumns = 16;
    int mCellW = 16;
    int mCellH = 16;
    int mAscent = 12;
    int mLineHeight = 16;
    int mAtlasW = 0;
    int mAtlasH = 0;
    std::vector<int> mAdvances;
};

} // namespace eng::ui
