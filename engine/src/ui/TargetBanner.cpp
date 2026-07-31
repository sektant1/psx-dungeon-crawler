#include <eng/ui/TargetBanner.h>

#include "TooltipDrawShared.h"

#include <algorithm>
#include <string>


namespace eng::ui {

void TargetBanner::update(const TooltipContent& content, float dt) {
    const bool wanted = !content.empty();
    const bool changed =
        wanted && !mContent.id.empty() && content.id != mContent.id;
    if (wanted)
        mContent = content;
    mFade.update(wanted, changed, dt);
    if (!wanted && mFade.closed())
        mContent = {};
}

glm::ivec4 TargetBanner::draw(const UiCanvas& canvas, UiRect safeBounds) const {
    if (!visible())
        return {0, 0, 0, 0};

    const UiPalette& pal = canvas.palette();
    if (safeBounds.empty())
        safeBounds = {{0, 0}, canvas.size()};
    const int pad = mStyle.padding;
    const int line = canvas.lineHeight();

    const int availableWidth = std::max(1, safeBounds.size.x);
    const int minWidth = std::min(mStyle.minWidth, availableWidth);
    const int maxWidth = std::min(mStyle.maxWidth, availableWidth);
    const int width = std::clamp(
        int(float(availableWidth) * mStyle.widthFraction), minWidth, maxWidth);
    const bool hasBar = !mContent.bars.empty();
    bool showBar = hasBar;
    bool showMetadata = !mContent.subtitle.empty() || !mContent.meta.empty();
    // Name row, then the bar, then a metadata row underneath. The category
    // sits below the bar rather than above so the two things that matter in a
    // fight -- the name and the health -- are adjacent.
    const auto measuredHeight = [&] {
        int result = pad + line + pad;
        if (showBar)
            result += mStyle.barHeight + 3;
        if (showMetadata)
            result += line;
        return result;
    };
    int height = measuredHeight();
    // The bottom row carries the category and the distance, so it is needed
    // when either is present, not just the category.
    const int availableHeight = std::max(0, safeBounds.size.y - mStyle.topMargin);
    if (height > availableHeight && showMetadata) {
        showMetadata = false;
        height = measuredHeight();
    }
    if (height > availableHeight && showBar) {
        showBar = false;
        height = measuredHeight();
    }
    if (height > availableHeight)
        return {};

    const int x = safeBounds.position.x + (safeBounds.size.x - width) / 2;
    // Travels *downward* into place: it lives at the top edge, so arriving
    // from above is the direction that reads as it dropping in.
    const int y = safeBounds.position.y + mStyle.topMargin - mFade.travel();

    const float a = mFade.alpha();
    const glm::ivec2 at{x, y};
    const glm::ivec2 box{width, height};

    canvas.panel(at, box, mStyle.chrome, mContent.accent, a);

    const int left = x + pad;
    const int right = x + width - pad;
    int cursor = y + pad;

    // A three-row grid, so nothing has to share a column with anything else.
    // Stacking the health readout and the distance against the same right edge
    // made them read as one two-line block glued to the corner.
    //
    //   name .................... 50/80     <- what you are fighting, and its HP
    //   [==================        ]        <- the bar, full width
    //   category ................. 3.4 m    <- the quieter metadata
    const TooltipContent::Bar* bar =
        mContent.bars.empty() ? nullptr : &mContent.bars.front();

    const int innerWidth = std::max(1, right - left);
    const std::string value = bar
                                  ? canvas.font().ellipsize(bar->value,
                                                            innerWidth / 3)
                                  : std::string{};
    const int valueWidth = !value.empty()
                               ? canvas.measure(value).x + 8
                               : 0;
    const std::string title = canvas.font().ellipsize(
        mContent.title, std::max(1, right - left - valueWidth));
    canvas.text({left, cursor}, title, detail::fade(pal.text, a), Align::Left,
                false);
    if (!value.empty())
        canvas.text({right, cursor}, value, detail::fade(pal.text, a),
                      Align::Right, false);
    cursor += line;

    if (bar && showBar) {
        canvas.bar({left, cursor}, {right - left, mStyle.barHeight}, bar->ratio,
                    detail::fade(canvas.colour(bar->tone), a),
                   detail::fade(pal.inkSoft, a));
        cursor += mStyle.barHeight + 3;
    }

    const std::string meta = canvas.font().ellipsize(mContent.meta,
                                                      innerWidth / 3);
    const int metaWidth = meta.empty() ? 0 : canvas.measure(meta).x + 8;
    if (showMetadata && !mContent.subtitle.empty()) {
        const std::string subtitle = canvas.font().ellipsize(
            mContent.subtitle, std::max(1, right - left - metaWidth));
        canvas.text({left, cursor}, subtitle, detail::fade(pal.textDim, a),
                    Align::Left, false);
    }
    if (showMetadata && !meta.empty())
        canvas.text({right, cursor}, meta, detail::fade(pal.textDim, a),
                      Align::Right, false);

    return {at.x, at.y, width, height};
}

} // namespace eng::ui
