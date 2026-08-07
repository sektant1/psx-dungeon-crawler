// The tooltip's layout and painting.
//
// The pacing itself lives in UiFade, which links and tests on its own:
// everything below reaches into UiCanvas and the bitmap font, which drag in
// the renderer and ImGui, and none of that is needed to decide *when* a panel should
// appear.
#include <eng/ui/Tooltip.h>

#include "TooltipDrawShared.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>


namespace eng::ui {

void TooltipView::update(const TooltipContent& content, float dt) {
    const bool wanted = !content.empty();
    const bool changed =
        wanted && !mContent.id.empty() && content.id != mContent.id;
    if (wanted) {
        // Swapping targets replaces the text immediately but keeps the panel
        // on screen: a flicker to zero alpha every time the crosshair grazes
        // two props reads as a bug, not as feedback.
        mContent = content;
    }
    mFade.update(wanted, changed, dt);
    if (!wanted && mFade.closed())
        mContent = {};
}

glm::ivec4 TooltipView::draw(const UiCanvas& canvas,
                              const TooltipAnchor& anchor,
                              UiRect safeBounds) const {
    if (!visible())
        return {0, 0, 0, 0};

    const UiPalette& pal = canvas.palette();
    const int pad = mStyle.padding;
    const int line = canvas.lineHeight();
    // The accent rail owns the left edge; text starts inside it.
    constexpr int kRail = 3;
    const int textX = pad + kRail;
    if (safeBounds.empty())
        safeBounds = UiRect{{0, 0}, canvas.size()}.inset(
            {mStyle.safeMargin, mStyle.safeMargin,
             mStyle.safeMargin, mStyle.safeMargin});
    const int maxWidth = std::max(1, std::min(mStyle.maxWidth,
                                              safeBounds.size.x));
    const int minWidth = std::min(mStyle.minWidth, maxWidth);
    const int inner = std::max(1, maxWidth - textX - pad);
    const auto continued = [&](std::string text, int width) {
        const std::string suffix = "...";
        const int suffixWidth = canvas.measure(suffix).x;
        if (suffixWidth > width)
            return std::string{};
        while (!text.empty() &&
               canvas.measure(text).x + suffixWidth > width)
            text.pop_back();
        return text + suffix;
    };

    // --- measure ---------------------------------------------------------
    // The header is title + a right-aligned meta value on the same baseline,
    // so the title wraps against the space the meta actually leaves.
    const std::string meta = canvas.font().ellipsize(mContent.meta, inner / 2);
    const int metaW = meta.empty() ? 0 : canvas.measure(meta).x + 8;
    std::vector<std::string> title =
        mContent.title.empty()
            ? std::vector<std::string>{}
            : canvas.font().wrap(mContent.title, std::max(24, inner - metaW));
    if (title.size() > 2) {
        title.resize(2);
        title.back() = continued(title.back(), inner);
    }
    std::vector<std::string> subtitle =
        mContent.subtitle.empty()
            ? std::vector<std::string>{}
            : canvas.font().wrap(mContent.subtitle, inner);
    if (subtitle.size() > 1) {
        subtitle.resize(1);
        subtitle.front() = canvas.font().ellipsize(subtitle.front(), inner);
    }
    struct WrappedLine {
        std::string text;
        UiTone tone = UiTone::Muted;
    };
    std::vector<WrappedLine> body;
    for (const TooltipContent::Line& l : mContent.lines)
        for (std::string& wrapped : canvas.font().wrap(l.text, inner))
            body.push_back({std::move(wrapped), l.tone});
    if (mStyle.maxBodyLines >= 0 &&
        int(body.size()) > mStyle.maxBodyLines) {
        body.resize(size_t(mStyle.maxBodyLines));
        if (!body.empty())
            body.back().text = continued(body.back().text, inner);
    }
    const std::string actionKey =
        canvas.font().ellipsize(mContent.actionKey, inner / 3);
    const int actionKeyWidth =
        actionKey.empty() ? 0 : canvas.keyCapWidth(actionKey);

    // Bindings: a cap column plus descriptions, measured together so every
    // label starts on the same x no matter how wide its own cap is.
    struct HintRow {
        std::string key;
        std::string label;
    };
    std::vector<HintRow> hints;
    const std::size_t hintBudget =
        mStyle.maxHints < 0
            ? mContent.hints.size()
            : std::min(mContent.hints.size(), std::size_t(mStyle.maxHints));
    int hintCapColumn = 0;
    for (std::size_t i = 0; i < hintBudget; ++i) {
        const TooltipContent::Hint& hint = mContent.hints[i];
        if (hint.key.empty() && hint.label.empty())
            continue;
        const std::string key = canvas.font().ellipsize(hint.key, inner / 2);
        hintCapColumn = std::max(hintCapColumn, canvas.keyCapWidth(key));
        hints.push_back({key, hint.label});
    }
    const int hintLabelX = hintCapColumn + 6;
    for (HintRow& row : hints)
        row.label = canvas.font().ellipsize(row.label,
                                            std::max(1, inner - hintLabelX));
    const std::string action = canvas.font().ellipsize(
        mContent.action, std::max(1, inner - actionKeyWidth - 13));
    const std::size_t barCount = std::min<std::size_t>(mContent.bars.size(), 2);

    int width = 0;
    for (size_t i = 0; i < title.size(); ++i)
        width = std::max(width, canvas.measure(title[i]).x +
                                    (i == 0 ? metaW : 0));
    for (const std::string& l : subtitle)
        width = std::max(width, canvas.measure(l).x);
    for (const WrappedLine& l : body)
        width = std::max(width, canvas.measure(l.text).x);
    for (std::size_t i = 0; i < barCount; ++i) {
        const TooltipContent::Bar& b = mContent.bars[i];
        // label + gap + a bar worth looking at + the readout's own column.
        const int valueW =
            b.value.empty() ? 0 : canvas.measure(b.value).x + 4;
        width = std::max(width, canvas.measure(b.label).x + 5 + 52 + valueW);
    }
    if (!mContent.action.empty())
        width = std::max(width, actionKeyWidth + canvas.measure(action).x + 13);
    for (const HintRow& row : hints)
        width = std::max(width, hintLabelX + canvas.measure(row.label).x);
    width = std::clamp(width + textX + pad, minWidth, maxWidth);

    // Vertical: each block contributes its lines plus the rule above it.
    const int barH = line - 3;
    int height = pad;
    height += int(title.size()) * line;
    height += int(subtitle.size()) * line;
    if (barCount > 0)
        height += 4 + int(barCount) * (barH + 2);
    // Cap rows are taller than text rows: the plate is sized from the glyph
    // cell, and a column of them needs a gap between plates.
    const int capRow = canvas.keyCapRow();
    if (!body.empty())
        height += 5 + int(body.size()) * line;
    if (!hints.empty())
        height += 5 + int(hints.size()) * capRow;
    if (!mContent.action.empty())
        height += 6 + capRow;
    height += pad;

    // Bindings survive longer than body copy when room runs out: a panel that
    // has to choose keeps the thing the player acts on, not the prose about it.
    bool verticallyClamped = false;
    while (height > safeBounds.size.y && !body.empty()) {
        body.pop_back();
        height -= line;
        verticallyClamped = true;
    }
    if (verticallyClamped && body.empty())
        height -= 5;
    if (verticallyClamped && !body.empty())
        body.back().text = continued(body.back().text, inner);
    while (height > safeBounds.size.y && hints.size() > 1) {
        hints.pop_back();
        height -= capRow;
    }
    while (height > safeBounds.size.y && !subtitle.empty()) {
        subtitle.pop_back();
        height -= line;
    }
    while (height > safeBounds.size.y && title.size() > 1) {
        title.pop_back();
        height -= line;
    }
    // At pathological window heights, hiding beats painting over combat HUD.
    if (height > safeBounds.size.y)
        return {};

    // --- place -----------------------------------------------------------
    glm::ivec2 at;
    if (anchor.mode == TooltipAnchor::Mode::Crosshair) {
        const glm::ivec2 centre = canvas.size() / 2;
        at = {centre.x + mStyle.gap, centre.y + mStyle.gap};
        if (at.x + width > safeBounds.position.x + safeBounds.size.x)
            at.x = centre.x - mStyle.gap - width;
        if (at.y + height > safeBounds.position.y + safeBounds.size.y)
            at.y = centre.y - mStyle.gap - height;
    } else {
        at = {anchor.point.x + mStyle.gap, anchor.point.y + mStyle.gap};
        if (at.x + width > safeBounds.position.x + safeBounds.size.x)
            at.x = anchor.point.x - mStyle.gap - width;
        if (at.y + height > safeBounds.position.y + safeBounds.size.y)
            at.y = anchor.point.y - mStyle.gap - height;
    }
    at.y += mFade.travel();
    at = safeBounds.contain({at, {width, height}}).position;

    // --- draw ------------------------------------------------------------
    const float a = mFade.alpha();
    const unsigned int accent = canvas.colour(mContent.accent);
    const glm::ivec2 box{width, height};

    canvas.panel(at, box, mStyle.chrome, mContent.accent, a);

    const int x = at.x + textX;
    const int right = at.x + width - pad;
    int y = at.y + pad;

    for (size_t i = 0; i < title.size(); ++i) {
        canvas.text({x, y}, title[i], detail::fade(pal.text, a), Align::Left,
                    false);
        if (i == 0 && !meta.empty())
            canvas.text({right, y}, meta, detail::fade(pal.textDim, a),
                        Align::Right, false);
        y += line;
    }
    for (const std::string& l : subtitle) {
        canvas.text({x, y}, l, detail::fade(pal.textDim, a), Align::Left,
                    false);
        y += line;
    }

    // Rules separate blocks. Drawn in the accent at low weight so the panel
    // has one colour story rather than two.
    auto rule = [&](int atY) {
        canvas.rect({x, atY}, {right - x, 1}, detail::fade(pal.edge, a * 0.9f));
    };

    if (!mContent.action.empty()) {
        y += 2;
        rule(y);
        y += 3;
        const int capW = canvas.keyCap({x, y}, actionKey, a);
        canvas.text({x + capW + 6, y}, action,
                    detail::fade(accent, a), Align::Left, false);
        y += capRow;
    }

    if (barCount > 0) {
        y += 2;
        rule(y);
        y += 2;
        for (std::size_t i = 0; i < barCount; ++i) {
            const TooltipContent::Bar& b = mContent.bars[i];
            const int rowWidth = std::max(1, right - x);
            const std::string value = canvas.font().ellipsize(
                b.value, std::max(1, rowWidth / 3));
            const int valueW = value.empty() ? 0 : canvas.measure(value).x + 4;
            const int labelBudget = std::max(
                1, std::min(rowWidth / 3, rowWidth - valueW - 14));
            const std::string label =
                canvas.font().ellipsize(b.label, labelBudget);
            canvas.text({x, y - 1}, label, detail::fade(pal.textDim, a),
                        Align::Left, false);
            const int barX = x + canvas.measure(label).x + 5;
            // The readout gets its own column rather than being painted over
            // the bar: on a full bar the digits sat on the fill and the bar's
            // own border cut through them.
            const int barW = right - barX - valueW;
            if (barW > 8) {
                canvas.bar({barX, y}, {barW, barH}, b.ratio,
                           detail::fade(canvas.colour(b.tone), a),
                           detail::fade(pal.inkSoft, a));
            }
            if (!value.empty())
                canvas.text({right, y - 1}, value, detail::fade(pal.text, a),
                              Align::Right, false);
            y += barH + 2;
        }
    }

    if (!hints.empty()) {
        y += 2;
        rule(y);
        y += 3;
        for (const HintRow& row : hints) {
            canvas.keyCap({x, y}, row.key, a);
            canvas.text({x + hintLabelX, y}, row.label,
                        detail::fade(pal.textDim, a), Align::Left, false);
            y += capRow;
        }
    }

    if (!body.empty()) {
        y += 2;
        rule(y);
        y += 3;
        for (const WrappedLine& l : body) {
            canvas.text({x, y}, l.text,
                        detail::fade(canvas.colour(l.tone), a), Align::Left,
                        false);
            y += line;
        }
    }

    return {at.x, at.y, width, height};
}

} // namespace eng::ui
