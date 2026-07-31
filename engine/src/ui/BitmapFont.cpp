#include <eng/ui/BitmapFont.h>

#include <eng/Log.h>

#include <OgreDataStream.h>
#include <OgreException.h>
#include <OgreHardwarePixelBuffer.h>
#include <OgreImage.h>
#include <OgreResourceGroupManager.h>
#include <OgreTextureManager.h>

#include <GL/gl.h>

#include <imgui.h>

#define TOML_EXCEPTIONS 0
#include <tomlplusplus/toml.hpp>

#include <algorithm>

namespace {

constexpr const char* kGroup = "General";

// A texture that ImGui samples directly gets no Ogre texture-unit state, so
// its filtering is whatever the object itself carries. Ogre uploads with
// trilinear defaults, which turns a magnified pixel font into mush.
void forceNearest(unsigned int glId) {
    GLint previous = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous);
    glBindTexture(GL_TEXTURE_2D, GLuint(glId));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, GLuint(previous));
}

} // namespace

namespace eng::ui {

BitmapFont::~BitmapFont() = default;

bool BitmapFont::load(const std::string& definition) {
    Ogre::ResourceGroupManager& rgm = Ogre::ResourceGroupManager::getSingleton();
    if (!rgm.resourceExists(kGroup, definition)) {
        log::error("BitmapFont: '%s' not found in resource group",
                   definition.c_str());
        return false;
    }

    std::string source;
    {
        Ogre::DataStreamPtr stream = rgm.openResource(definition, kGroup);
        source = stream->getAsString();
    }
    toml::parse_result parsed = toml::parse(source);
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
    if (mAdvances.empty()) {
        log::error("BitmapFont: '%s' has no advances", definition.c_str());
        return false;
    }

    // The atlas is shared, not owned. More than one BitmapFont can name the
    // same atlas -- the loading screen's canvas and the game's HUD both do --
    // and Ogre's TextureManager throws on a duplicate name, so creating it
    // unconditionally aborted startup for the second one. Reuse whatever is
    // already there; the atlas is immutable once uploaded, so sharing it is
    // free and there is nothing to keep in sync.
    const std::string textureName = "eng/ui/font/" + mTextureName;
    Ogre::TextureManager& textures = Ogre::TextureManager::getSingleton();
    Ogre::TexturePtr texture =
        Ogre::static_pointer_cast<Ogre::Texture>(textures.getByName(textureName,
                                                                   kGroup));
    if (!texture) {
        // Group-scoped lookup misses a texture Ogre filed under a different
        // group than the one it was asked for, and the miss is only visible as
        // the duplicate-name throw below. Search every group before creating.
        texture = Ogre::static_pointer_cast<Ogre::Texture>(textures.getByName(
            textureName,
            Ogre::ResourceGroupManager::AUTODETECT_RESOURCE_GROUP_NAME));
    }
    if (!texture) {
        try {
            // Loaded manually rather than through TextureManager::load so the
            // atlas keeps exactly one mip level; mips bleed neighbouring glyphs.
            Ogre::Image image;
            image.load(mTextureName, kGroup);
            texture = textures.createManual(
                textureName, kGroup, Ogre::TEX_TYPE_2D, image.getWidth(),
                image.getHeight(), 0, Ogre::PF_BYTE_RGBA,
                Ogre::TU_STATIC_WRITE_ONLY);
            texture->getBuffer()->blitFromMemory(image.getPixelBox());
        } catch (const Ogre::Exception& error) {
            log::error("BitmapFont: cannot load atlas '%s': %s",
                       mTextureName.c_str(),
                       error.getFullDescription().c_str());
            mTextureId = 0;
            return false;
        }
    }

    unsigned int glId = 0;
    texture->getCustomAttribute("GLID", &glId);
    if (glId == 0) {
        log::error("BitmapFont: '%s' has no GL id", mTextureName.c_str());
        return false;
    }
    forceNearest(glId);

    mAtlasW = int(texture->getWidth());
    mAtlasH = int(texture->getHeight());
    mTextureId = glId;
    return true;
}

int BitmapFont::advance(char c) const {
    const int index = int(static_cast<unsigned char>(c)) - mFirst;
    if (index < 0 || index >= int(mAdvances.size()))
        return mAdvances.empty() ? mCellW : mAdvances[0];
    return mAdvances[size_t(index)] + 1; // one column of tracking
}

glm::ivec2 BitmapFont::measure(std::string_view text) const {
    int widest = 0;
    int width = 0;
    int lines = 1;
    for (char c : text) {
        if (c == '\n') {
            widest = std::max(widest, width);
            width = 0;
            ++lines;
            continue;
        }
        width += advance(c);
    }
    widest = std::max(widest, width);
    return {widest, lines * mLineHeight};
}

std::vector<std::string> BitmapFont::wrap(std::string_view text,
                                           int maxWidth) const {
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
            std::size_t count = 1;
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

std::string BitmapFont::ellipsize(std::string_view text, int maxWidth) const {
    if (maxWidth <= 0)
        return {};
    if (const std::size_t newline = text.find('\n');
        newline != std::string_view::npos)
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
                           std::string_view text, unsigned int colour) const {
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
            const ImVec2 uv0(float(cx) * du, float(cy) * dv);
            const ImVec2 uv1(float(cx + mCellW) * du, float(cy + mCellH) * dv);
            list->AddImage(ImTextureID(mTextureId), ImVec2(penX, penY),
                           ImVec2(penX + float(mCellW) * scale,
                                  penY + float(mCellH) * scale),
                           uv0, uv1, colour);
        }
        penX += float(advance(c)) * scale;
    }
}

void BitmapFont::draw(ImDrawList* list, glm::vec2 origin, float scale,
                      std::string_view text, unsigned int colour,
                      unsigned int outline) const {
    if (!list || !valid() || text.empty())
        return;

    list->PushTextureID(ImTextureID(mTextureId));
    if (outline != 0u) {
        // A one-pixel drop shadow in the four cardinal directions: the cheap
        // trick that keeps light HUD text readable over a bright wall torch.
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
