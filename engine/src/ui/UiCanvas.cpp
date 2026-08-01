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

void UiCanvas::begin(glm::vec2 displayPixels, glm::ivec2 preferred,
                     glm::vec2 framebufferScale) {
    mDisplay = displayPixels;
    mFramebufferScale = glm::max(framebufferScale, glm::vec2(1e-3f));
    const glm::vec2 physical = displayPixels * mFramebufferScale;
    const int byWidth = int(physical.x) / std::max(1, preferred.x);
    const int byHeight = int(physical.y) / std::max(1, preferred.y);
    mScale = std::clamp(std::min(byWidth, byHeight), 1, 8);
    // The virtual surface covers the whole window: layouts anchor to real
    // corners instead of living inside a letterboxed box.
    mVirtual = {
        std::max(1, int(std::ceil(physical.x / float(mScale)))),
        std::max(1, int(std::ceil(physical.y / float(mScale))))};
    mOrigin = {0.0f, 0.0f};
    mTarget = nullptr;
    mClipToTarget = false;
}

void UiCanvas::beginTarget(glm::vec2 originPixels, glm::ivec2 virtualSize,
                           int scale, ImDrawList* target) {
    mScale = std::clamp(scale, 1, 8);
    mVirtual = {std::max(virtualSize.x, 1), std::max(virtualSize.y, 1)};
    mDisplay = {float(mVirtual.x * mScale), float(mVirtual.y * mScale)};
    mOrigin = originPixels;
    mFramebufferScale = {1.0f, 1.0f};
    mTarget = target;
    mClipToTarget = true;
}

ImDrawList* UiCanvas::list() const {
    return mTarget ? mTarget : ImGui::GetForegroundDrawList();
}

void UiCanvas::pushClip(ImDrawList* draw) const {
    if (!mClipToTarget)
        return;
    draw->PushClipRect(ImVec2(mOrigin.x, mOrigin.y),
                       ImVec2(mOrigin.x + mDisplay.x,
                              mOrigin.y + mDisplay.y),
                       true);
}

void UiCanvas::popClip(ImDrawList* draw) const {
    if (mClipToTarget)
        draw->PopClipRect();
}

glm::vec2 UiCanvas::toScreen(glm::ivec2 at) const {
    return {mOrigin.x + float(at.x * mScale) / mFramebufferScale.x,
            mOrigin.y + float(at.y * mScale) / mFramebufferScale.y};
}

void UiCanvas::rect(glm::ivec2 at, glm::ivec2 size, unsigned int colour) const {
    if (size.x <= 0 || size.y <= 0)
        return;
    const glm::vec2 a = toScreen(at);
    const glm::vec2 b = toScreen(at + size);
    ImDrawList* draw = list();
    pushClip(draw);
    draw->AddRectFilled(ImVec2(a.x, a.y), ImVec2(b.x, b.y), colour);
    popClip(draw);
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
    ImDrawList* draw = list();
    pushClip(draw);
    mFont.draw(draw, toScreen(pos), float(mScale) / mFramebufferScale.x,
               value, colour,
               shadow ? mStyle.palette.shadow : 0u);
    popClip(draw);
}

int UiCanvas::keyCapWidth(std::string_view label) const {
    if (label.empty())
        return 0;
    // Padding either side of the glyphs, plus a floor so a one-character cap
    // (`W`, `.`) is still a cap and not a tight box around a comma.
    return std::max(mFont.measure(label).x + 8, 13);
}

int UiCanvas::keyCap(glm::ivec2 textAt, std::string_view label,
                     float alpha) const {
    if (label.empty())
        return 0;
    const int width = keyCapWidth(label);
    const int height = keyCapHeight();
    // The plate is placed from the text, not the other way round: the glyph
    // cell carries blank rows above the ink (cellHeight - ascent - 1), so a box
    // drawn at the text origin sits a couple of pixels too high and cuts
    // through the baseline -- which is how the first version of this looked.
    const int inkTop = mFont.cellHeight() - mFont.ascent() - 1;
    const glm::ivec2 at{textAt.x, textAt.y + inkTop - 2};
    const auto tint = [alpha](unsigned int colour) {
        const unsigned int a = (colour >> 24) & 0xFFu;
        const unsigned int scaled = (unsigned int)std::lround(
            float(a) * std::clamp(alpha, 0.0f, 1.0f));
        return (colour & 0x00FFFFFFu) | (scaled << 24);
    };
    rect(at, {width, height}, tint(mStyle.palette.inkSoft));
    border(at, {width, height}, tint(mStyle.palette.edge));
    // The label is centred in the plate rather than left-padded by a constant:
    // caps hold anything from "." to "WHEEL", and a fixed inset leaves the
    // short ones visibly off-centre in a column of wider ones.
    text({textAt.x + width / 2, textAt.y}, label, tint(mStyle.palette.text),
         Align::Centre, false);
    return width;
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
