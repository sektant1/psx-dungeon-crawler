#include "LabelRaster.h"

#include <eng/Log.h>
#include <eng/assets/AssetRoot.h>

#define TOML_EXCEPTIONS 0
#include <tomlplusplus/toml.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string_view>
#include <vector>

namespace eng::rhi_renderer {
namespace {

// The atlas plus the metrics that address it. Loaded once per process: every
// label in a level shares one font, and the atlas is a few hundred kilobytes
// that would otherwise be decoded per label.
struct LabelFont {
    Image atlas;
    int first = 32;
    int columns = 16;
    int cellWidth = 16;
    int cellHeight = 16;
    int lineHeight = 16;
    std::vector<int> advances;

    bool valid() const { return atlas.valid() && !advances.empty(); }

    int advance(char c) const
    {
        const int index = int(static_cast<unsigned char>(c)) - first;
        if (index < 0 || index >= int(advances.size()))
            return cellWidth;
        return advances[size_t(index)];
    }

    int measure(std::string_view line) const
    {
        int width = 0;
        for (char c : line)
            width += advance(c);
        return width;
    }
};

// Same definition the UI font uses (tools/gen_font_atlas.py writes both the
// TOML and the PNG), so a world label and a HUD label are the same typeface.
constexpr const char* kFontDefinition = "fonts/ui_regular.toml";

const LabelFont& labelFont()
{
    static const LabelFont font = [] {
        LabelFont loaded;
        const std::filesystem::path definition =
            assets::resolve(kFontDefinition);
        if (definition.empty()) {
            log::error("Label: '%s' is not in a mounted content pack",
                       kFontDefinition);
            return loaded;
        }
        toml::parse_result parsed = toml::parse_file(definition.string());
        const toml::table* table =
            parsed ? parsed.table()["font"].as_table() : nullptr;
        if (!table) {
            log::error("Label: '%s' has no [font] table", kFontDefinition);
            return loaded;
        }
        loaded.first = int((*table)["first_codepoint"].value_or(32));
        loaded.columns = std::max(1, int((*table)["columns"].value_or(16)));
        loaded.cellWidth = std::max(1, int((*table)["cell_width"].value_or(16)));
        loaded.cellHeight =
            std::max(1, int((*table)["cell_height"].value_or(16)));
        loaded.lineHeight =
            std::max(1, int((*table)["line_height"].value_or(loaded.cellHeight)));
        if (const toml::array* advances = (*table)["advances"].as_array())
            for (const toml::node& node : *advances)
                loaded.advances.push_back(
                    int(node.value_or(int64_t(loaded.cellWidth))));

        const std::string texture =
            (*table)["texture"].value_or(std::string());
        std::filesystem::path atlas = definition.parent_path() / texture;
        if (!std::filesystem::is_regular_file(atlas))
            atlas = assets::resolve("fonts/" + texture);
        if (atlas.empty() || !loadImage(atlas, loaded.atlas))
            log::error("Label: cannot read the font atlas '%s'",
                       texture.c_str());
        return loaded;
    }();
    return font;
}

std::vector<std::string> wrap(const LabelFont& font, const std::string& text,
                              int maxWidth)
{
    std::vector<std::string> lines;
    std::string line;
    std::string word;
    // Greedy, and a word longer than the limit is allowed to overhang rather
    // than being broken: a label is short enough that a mid-word break reads as
    // damage, and the caller's maxWidth is a target, not a clip.
    const auto flushWord = [&] {
        if (word.empty())
            return;
        if (!line.empty() &&
            font.measure(line) + font.advance(' ') + font.measure(word) >
                maxWidth) {
            lines.push_back(line);
            line.clear();
        }
        if (!line.empty())
            line += ' ';
        line += word;
        word.clear();
    };
    for (char c : text) {
        if (c == '\n') {
            flushWord();
            lines.push_back(line);
            line.clear();
        } else if (c == ' ' || c == '\t') {
            flushWord();
        } else {
            word += c;
        }
    }
    flushWord();
    if (!line.empty() || lines.empty())
        lines.push_back(line);
    return lines;
}

uint8_t channel(float value)
{
    return uint8_t(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
}

// Straight "over" in unpremultiplied 8-bit. The plate underneath is opaque
// wherever it matters, so this never has to reconstruct a destination alpha
// more carefully than max().
void blend(uint8_t* pixel, glm::vec4 colour, float coverage)
{
    const float alpha = std::clamp(colour.a * coverage, 0.0f, 1.0f);
    if (alpha <= 0.0f)
        return;
    for (int i = 0; i < 3; ++i)
        pixel[i] = channel((float(pixel[i]) / 255.0f) * (1.0f - alpha) +
                           colour[i] * alpha);
    pixel[3] = channel(std::max(float(pixel[3]) / 255.0f, alpha));
}

void fillRect(Image& image, int x0, int y0, int width, int height,
              glm::vec4 colour)
{
    for (int y = std::max(y0, 0); y < std::min(y0 + height, image.height); ++y)
        for (int x = std::max(x0, 0); x < std::min(x0 + width, image.width);
             ++x)
            blend(&image.rgba[size_t(y * image.width + x) * 4], colour, 1.0f);
}

// The atlas is white ink with a binary alpha mask, so alpha is the glyph's
// coverage and the RGB carries nothing.
void drawGlyph(Image& image, const LabelFont& font, char c, int x0, int y0,
               glm::vec4 colour)
{
    const int index = int(static_cast<unsigned char>(c)) - font.first;
    if (index < 0)
        return;
    const int cellX = (index % font.columns) * font.cellWidth;
    const int cellY = (index / font.columns) * font.cellHeight;
    if (cellY + font.cellHeight > font.atlas.height)
        return;
    for (int row = 0; row < font.cellHeight; ++row) {
        const int y = y0 + row;
        if (y < 0 || y >= image.height)
            continue;
        for (int column = 0; column < font.cellWidth; ++column) {
            const int x = x0 + column;
            if (x < 0 || x >= image.width)
                continue;
            const size_t source =
                (size_t(cellY + row) * size_t(font.atlas.width) +
                 size_t(cellX + column)) *
                4u;
            const float coverage = float(font.atlas.rgba[source + 3]) / 255.0f;
            if (coverage > 0.0f)
                blend(&image.rgba[size_t(y * image.width + x) * 4], colour,
                      coverage);
        }
    }
}

glm::vec4 colourFor(const TextSpriteStyle& style, const std::string& line)
{
    for (const TextSpriteStyle::ColourRule& rule : style.colourRules)
        if (!rule.pattern.empty() &&
            line.find(rule.pattern) != std::string::npos)
            return rule.colour;
    return style.textColour;
}

} // namespace

bool rasterizeLabel(const std::string& text, const TextSpriteStyle& style,
                    Image& out)
{
    const LabelFont& font = labelFont();
    if (!font.valid() || text.empty())
        return false;

    const int maxWidth = std::max(style.maxWidthPixels, font.cellWidth);
    const std::vector<std::string> lines = wrap(font, text, maxWidth);
    const int padding = std::max(style.paddingPixels, 0);
    const int accent = std::max(style.accentWidthPixels, 0);
    const int spacing = std::max(style.lineSpacingPixels, 0);

    int textWidth = 0;
    for (const std::string& line : lines)
        textWidth = std::max(textWidth, font.measure(line));
    // The cell is taller than the line advance, so the last line needs the full
    // cell or its descenders are clipped by the padding.
    const int textHeight = int(lines.size() - 1) * (font.lineHeight + spacing) +
                           font.cellHeight;

    Image image;
    image.width = textWidth + padding * 2 + accent + 2;
    image.height = textHeight + padding * 2 + 2;
    if (image.width <= 0 || image.height <= 0)
        return false;
    image.rgba.assign(size_t(image.width) * size_t(image.height) * 4u, 0);

    fillRect(image, 0, 0, image.width, image.height, style.borderColour);
    fillRect(image, 1, 1, image.width - 2, image.height - 2,
             style.backgroundColour);
    if (accent > 0)
        fillRect(image, 1, 1, accent, image.height - 2, style.accentColour);

    int y = 1 + padding;
    for (const std::string& line : lines) {
        const glm::vec4 colour = colourFor(style, line);
        int x = 1 + accent + padding;
        for (char c : line) {
            drawGlyph(image, font, c, x, y, colour);
            x += font.advance(c);
        }
        y += font.lineHeight + spacing;
    }

    out = std::move(image);
    return true;
}

} // namespace eng::rhi_renderer
