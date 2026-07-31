#include <eng/ui/UiCanvas.h>

#include <imgui.h>

#include <algorithm>
#include <cmath>

namespace eng::ui {
namespace {

unsigned int withOpacity(unsigned int colour, float opacity)
{
    const unsigned int alpha = (colour >> 24) & 0xFFu;
    const unsigned int resolved = static_cast<unsigned int>(std::lround(
        float(alpha) * std::clamp(opacity, 0.0f, 1.0f)));
    return (colour & 0x00FFFFFFu) | (resolved << 24);
}

} // namespace

unsigned int UiCanvas::colour(UiTone tone) const {
    const UiPalette& palette = mStyle.palette;
    switch (tone) {
    case UiTone::Text: return palette.text;
    case UiTone::Muted: return palette.textDim;
    case UiTone::Focus: return palette.accent;
    case UiTone::Positive: return palette.good;
    case UiTone::Warning: return palette.warn;
    case UiTone::Danger: return palette.bad;
    case UiTone::Mystic: return palette.mystic;
    case UiTone::Edge: return palette.edgeBright;
    }
    return palette.text;
}

bool UiCanvas::initialise(const std::string& fontDefinition) {
    return mFont.load(fontDefinition);
}

void UiCanvas::begin(glm::vec2 displayPixels, glm::ivec2 preferred) {
    mDisplay = displayPixels;
    const int byWidth = int(displayPixels.x) / std::max(1, preferred.x);
    const int byHeight = int(displayPixels.y) / std::max(1, preferred.y);
    mScale = std::clamp(std::min(byWidth, byHeight), 1, 8);
    // The virtual surface covers the whole window: layouts anchor to real
    // corners instead of living inside a letterboxed box.
    mVirtual = {int(displayPixels.x) / mScale, int(displayPixels.y) / mScale};
}

ImDrawList* UiCanvas::list() const { return ImGui::GetForegroundDrawList(); }

glm::vec2 UiCanvas::toScreen(glm::ivec2 at) const {
    return {float(at.x * mScale), float(at.y * mScale)};
}

void UiCanvas::rect(glm::ivec2 at, glm::ivec2 size, unsigned int colour) const {
    if (size.x <= 0 || size.y <= 0)
        return;
    const glm::vec2 a = toScreen(at);
    const glm::vec2 b = toScreen(at + size);
    list()->AddRectFilled(ImVec2(a.x, a.y), ImVec2(b.x, b.y), colour);
}

void UiCanvas::border(glm::ivec2 at, glm::ivec2 size,
                      unsigned int colour) const {
    // Drawn as four filled one-virtual-pixel bars rather than AddRect, whose
    // stroke straddles the edge and lands on half device pixels.
    rect(at, {size.x, 1}, colour);
    rect({at.x, at.y + size.y - 1}, {size.x, 1}, colour);
    rect({at.x, at.y + 1}, {1, size.y - 2}, colour);
    rect({at.x + size.x - 1, at.y + 1}, {1, size.y - 2}, colour);
}

void UiCanvas::panel(glm::ivec2 at, glm::ivec2 size, PanelStyle style) const {
    PanelPaint paint;
    paint.style = style;
    paint.brokenFrame = false;
    paint.pins = false;
    paint.highlight = false;
    panel(at, size, paint, paint.railTone);
}

void UiCanvas::panel(glm::ivec2 at, glm::ivec2 size,
                     const PanelPaint& paint, UiTone railTone,
                     float opacity) const {
    if (size.x <= 0 || size.y <= 0)
        return;
    const UiPalette& palette = mStyle.palette;
    switch (paint.style) {
    case PanelStyle::Solid:
        rect(at + glm::ivec2(2, 2), size,
             withOpacity(palette.shadow, opacity));
        rect(at, size, withOpacity(palette.ink, opacity));
        break;
    case PanelStyle::Frame:
        break;
    case PanelStyle::Sunken:
        rect(at, size, withOpacity(palette.inkSoft, opacity));
        rect(at + glm::ivec2(1, 1), {size.x - 2, 1},
             withOpacity(palette.shadow, opacity));
        break;
    }

    if (paint.brokenFrame) {
        const int corner = std::min(5, std::max(1, std::min(size.x, size.y) / 2));
        const unsigned int edge = withOpacity(palette.edge, opacity);
        rect(at, {corner, 1}, edge);
        rect({at.x + size.x - corner, at.y}, {corner, 1}, edge);
        rect({at.x, at.y + size.y - 1}, {corner, 1}, edge);
        rect({at.x + size.x - corner, at.y + size.y - 1}, {corner, 1},
             edge);
        rect(at, {1, corner}, edge);
        rect({at.x + size.x - 1, at.y}, {1, corner}, edge);
        rect({at.x, at.y + size.y - corner}, {1, corner}, edge);
        rect({at.x + size.x - 1, at.y + size.y - corner}, {1, corner},
             edge);
    } else {
        border(at, size, withOpacity(palette.edge, opacity));
    }

    if (paint.highlight && size.x > 2)
        rect(at + glm::ivec2(1, 1), {size.x - 2, 1},
             withOpacity(palette.edgeBright, opacity));

    const unsigned int rail = withOpacity(colour(railTone), opacity);
    switch (paint.rail) {
    case RailEdge::None: break;
    case RailEdge::Left:
        rect(at + glm::ivec2(1, 2), {2, std::max(0, size.y - 4)}, rail);
        break;
    case RailEdge::Right:
        rect({at.x + size.x - 3, at.y + 2}, {2, std::max(0, size.y - 4)}, rail);
        break;
    case RailEdge::Bottom:
        rect({at.x + 2, at.y + size.y - 3}, {std::max(0, size.x - 4), 2}, rail);
        break;
    }

    if (paint.pins && paint.rail != RailEdge::None) {
        if (paint.rail == RailEdge::Left || paint.rail == RailEdge::Right) {
            const int x = paint.rail == RailEdge::Left ? at.x + 1
                                                       : at.x + size.x - 3;
            rect({x, at.y + 1}, {2, 2}, rail);
            rect({x, at.y + size.y - 3}, {2, 2}, rail);
        } else {
            rect({at.x + 1, at.y + size.y - 3}, {2, 2}, rail);
            rect({at.x + size.x - 3, at.y + size.y - 3}, {2, 2}, rail);
        }
    }
}

void UiCanvas::text(glm::ivec2 at, std::string_view value, unsigned int colour,
                    Align align, bool shadow) const {
    if (value.empty())
        return;
    glm::ivec2 pos = at;
    if (align != Align::Left) {
        const int width = mFont.measure(value).x;
        pos.x -= align == Align::Centre ? width / 2 : width;
    }
    mFont.draw(list(), toScreen(pos), float(mScale), value, colour,
               shadow ? mStyle.palette.shadow : 0u);
}

void UiCanvas::bar(glm::ivec2 at, glm::ivec2 size, float ratio,
                   unsigned int fill, unsigned int track) const {
    rect(at, size, track);
    const int filled =
        int(std::lround(std::clamp(ratio, 0.0f, 1.0f) * float(size.x - 2)));
    if (filled > 0)
        rect(at + glm::ivec2(1, 1), {filled, size.y - 2}, fill);
    border(at, size, mStyle.palette.edge);
}

void UiCanvas::icon(glm::ivec2 at, glm::ivec2 size, unsigned int colour,
                    int inset) const {
    rect(at + glm::ivec2(inset, inset),
         {size.x - inset * 2, size.y - inset * 2}, colour);
}

} // namespace eng::ui
