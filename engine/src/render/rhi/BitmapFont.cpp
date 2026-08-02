#include <eng/ui/BitmapFont.h>

#include "RenderCore.h"

#include <eng/Log.h>
#include <eng/assets/AssetRoot.h>

#include <imgui.h>

#define TOML_EXCEPTIONS 0
#include <tomlplusplus/toml.hpp>

#include <algorithm>
#include <cmath>

namespace eng::ui {

BitmapFont::~BitmapFont() = default;

bool BitmapFont::load(const std::string& definition)
{
    std::filesystem::path definitionPath = assets::resolve(definition);
    if (definitionPath.empty())
        definitionPath = assets::resolve("fonts/" + definition);
    if (definitionPath.empty()) {
        log::error("BitmapFont: '%s' is not in a mounted content pack",
                   definition.c_str());
        return false;
    }
    toml::parse_result parsed = toml::parse_file(definitionPath.string());
    if (!parsed) {
        log::error("BitmapFont: parse failed for '%s'", definition.c_str());
        return false;
    }
    const toml::table* font = parsed.table()["font"].as_table();
    if (!font) {
        log::error("BitmapFont: '%s' has no [font] table", definition.c_str());
        return false;
    }

    mTextureName = (*font)["texture"].value_or(std::string());
    mFirst = int((*font)["first_codepoint"].value_or(32));
    mColumns = std::max(1, int((*font)["columns"].value_or(16)));
    mCellW = std::max(1, int((*font)["cell_width"].value_or(16)));
    mCellH = std::max(1, int((*font)["cell_height"].value_or(16)));
    mAscent = int((*font)["ascent"].value_or(mCellH - 3));
    mLineHeight = int((*font)["line_height"].value_or(mCellH + 1));
    mAdvances.clear();
    if (const toml::array* advances = (*font)["advances"].as_array()) {
        mAdvances.reserve(advances->size());
        for (const toml::node& node : *advances)
            mAdvances.push_back(int(node.value_or(int64_t(mCellW))));
    }
    if (mAdvances.empty() || mTextureName.empty()) {
        log::error("BitmapFont: '%s' has incomplete metrics", definition.c_str());
        return false;
    }

    std::filesystem::path atlas = definitionPath.parent_path() / mTextureName;
    if (!std::filesystem::is_regular_file(atlas))
        atlas = assets::resolve("fonts/" + mTextureName);
    int width = 0;
    int height = 0;
    mTextureId = rhi_texture_registry::load(
        atlas, rhi::FilterMode::Nearest, rhi::AddressMode::ClampToEdge, width,
        height);
    mAtlasW = width;
    mAtlasH = height;
    if (!valid()) {
        log::error("BitmapFont: cannot upload atlas '%s'",
                   atlas.string().c_str());
        return false;
    }
    return true;
}

int BitmapFont::advance(char c) const
{
    const int index = int(static_cast<unsigned char>(c)) - mFirst;
    if (index < 0 || index >= int(mAdvances.size()))
        return mAdvances.empty() ? mCellW : mAdvances[0];
    return mAdvances[size_t(index)] + 1;
}

glm::ivec2 BitmapFont::measure(std::string_view text) const
{
    int widest = 0;
    int width = 0;
    int lines = 1;
    for (char c : text) {
        if (c == '\n') {
            widest = std::max(widest, width);
            width = 0;
            ++lines;
        } else {
            width += advance(c);
        }
    }
    return {std::max(widest, width), lines * mLineHeight};
}

std::vector<std::string> BitmapFont::wrap(std::string_view text,
                                          int maxWidth) const
{
    maxWidth = std::max(1, maxWidth);
    std::vector<std::string> lines;
    std::string line;
    std::string word;
    const auto flushWord = [&] {
        if (word.empty())
            return;
        const std::string candidate = line.empty() ? word : line + " " + word;
        if (measure(candidate).x <= maxWidth) {
            line = candidate;
            word.clear();
            return;
        }
        if (!line.empty()) {
            lines.push_back(line);
            line.clear();
        }
        while (!word.empty() && measure(word).x > maxWidth) {
            size_t count = 1;
            while (count < word.size() &&
                   measure(std::string_view(word).substr(0, count + 1)).x <=
                       maxWidth)
                ++count;
            lines.push_back(word.substr(0, count));
            word.erase(0, count);
        }
        line = std::move(word);
        word.clear();
    };
    for (char c : text) {
        if (c == ' ') {
            flushWord();
        } else if (c == '\n') {
            flushWord();
            lines.push_back(line);
            line.clear();
        } else {
            word.push_back(c);
        }
    }
    flushWord();
    if (!line.empty() || lines.empty())
        lines.push_back(line);
    return lines;
}

std::string BitmapFont::ellipsize(std::string_view text, int maxWidth) const
{
    if (maxWidth <= 0)
        return {};
    if (const size_t newline = text.find('\n'); newline != std::string_view::npos)
        text = text.substr(0, newline);
    if (measure(text).x <= maxWidth)
        return std::string(text);
    constexpr std::string_view suffix = "...";
    if (measure(suffix).x > maxWidth)
        return {};
    std::string result(text);
    while (!result.empty() && measure(result + std::string(suffix)).x > maxWidth)
        result.pop_back();
    return result + std::string(suffix);
}

void BitmapFont::drawPlain(ImDrawList* list, glm::vec2 origin, float scale,
                           std::string_view text, unsigned int colour) const
{
    const float du = 1.0f / float(mAtlasW);
    const float dv = 1.0f / float(mAtlasH);
    float penX = std::floor(origin.x);
    float penY = std::floor(origin.y);
    for (char c : text) {
        if (c == '\n') {
            penX = std::floor(origin.x);
            penY += float(mLineHeight) * scale;
            continue;
        }
        const int index = int(static_cast<unsigned char>(c)) - mFirst;
        if (index >= 0 && index < int(mAdvances.size()) && c != ' ') {
            const int cx = (index % mColumns) * mCellW;
            const int cy = (index / mColumns) * mCellH;
            list->AddImage(ImTextureID(mTextureId), ImVec2(penX, penY),
                           ImVec2(penX + float(mCellW) * scale,
                                  penY + float(mCellH) * scale),
                           ImVec2(float(cx) * du, float(cy) * dv),
                           ImVec2(float(cx + mCellW) * du,
                                  float(cy + mCellH) * dv),
                           colour);
        }
        penX += float(advance(c)) * scale;
    }
}

void BitmapFont::draw(ImDrawList* list, glm::vec2 origin, float scale,
                      std::string_view text, unsigned int colour,
                      unsigned int outline) const
{
    if (!list || !valid() || text.empty())
        return;
    list->PushTextureID(ImTextureID(mTextureId));
    if (outline != 0u) {
        const float step = scale;
        drawPlain(list, {origin.x - step, origin.y}, scale, text, outline);
        drawPlain(list, {origin.x + step, origin.y}, scale, text, outline);
        drawPlain(list, {origin.x, origin.y - step}, scale, text, outline);
        drawPlain(list, {origin.x, origin.y + step}, scale, text, outline);
    }
    drawPlain(list, origin, scale, text, colour);
    list->PopTextureID();
}

} // namespace eng::ui
