#include <eng/ui/UiLayout.h>

#include <algorithm>
#include <cstdint>
#include <limits>

namespace {

using Wide = std::int64_t;

int nonNegative(int value) noexcept { return std::max(0, value); }

int narrow(Wide value) noexcept
{
    return static_cast<int>(std::clamp(
        value, Wide(std::numeric_limits<int>::min()),
        Wide(std::numeric_limits<int>::max())));
}

Wide endOf(int position, int size) noexcept
{
    return Wide(position) + Wide(nonNegative(size));
}

int mainOf(glm::ivec2 value, eng::ui::FlexAxis axis) noexcept
{
    return axis == eng::ui::FlexAxis::Row ? value.x : value.y;
}

int crossOf(glm::ivec2 value, eng::ui::FlexAxis axis) noexcept
{
    return axis == eng::ui::FlexAxis::Row ? value.y : value.x;
}

glm::ivec2 axes(int main, int cross, eng::ui::FlexAxis axis) noexcept
{
    return axis == eng::ui::FlexAxis::Row ? glm::ivec2{main, cross}
                                          : glm::ivec2{cross, main};
}

void setMain(glm::ivec2& value, eng::ui::FlexAxis axis, int main) noexcept
{
    if (axis == eng::ui::FlexAxis::Row)
        value.x = main;
    else
        value.y = main;
}

int preferredMain(const eng::ui::FlexItem& item,
                  eng::ui::FlexAxis axis) noexcept
{
    return std::max(nonNegative(mainOf(item.preferredSize, axis)),
                    nonNegative(mainOf(item.minimumSize, axis)));
}

int preferredCross(const eng::ui::FlexItem& item,
                   eng::ui::FlexAxis axis) noexcept
{
    return std::max(nonNegative(crossOf(item.preferredSize, axis)),
                    nonNegative(crossOf(item.minimumSize, axis)));
}

int minimumMain(const eng::ui::FlexItem& item,
                eng::ui::FlexAxis axis) noexcept
{
    return nonNegative(mainOf(item.minimumSize, axis));
}

struct Line {
    std::size_t end = 0;
    Wide itemMain = 0;
    int naturalCross = 0;
};

Line measureLine(std::size_t begin, std::size_t count, int availableMain,
                 int gap, bool wrap, eng::ui::FlexAxis axis,
                 std::span<const eng::ui::FlexItem> items) noexcept
{
    Line line{begin, 0, 0};
    for (std::size_t i = begin; i < count; ++i) {
        const int itemMain = preferredMain(items[i], axis);
        const Wide candidate = line.itemMain + itemMain +
                               (line.end == begin ? 0 : Wide(gap));
        if (wrap && line.end != begin && candidate > availableMain)
            break;

        line.itemMain = candidate;
        line.naturalCross =
            std::max(line.naturalCross, preferredCross(items[i], axis));
        line.end = i + 1;
    }
    return line;
}

void growLine(std::size_t begin, std::size_t end, Wide free,
              eng::ui::FlexAxis axis,
              std::span<const eng::ui::FlexItem> items,
              std::span<eng::ui::UiRect> output) noexcept
{
    Wide totalWeight = 0;
    for (std::size_t i = begin; i < end; ++i)
        totalWeight += nonNegative(items[i].grow);
    if (free <= 0 || totalWeight <= 0)
        return;

    Wide distributed = 0;
    for (std::size_t i = begin; i < end; ++i) {
        const int weight = nonNegative(items[i].grow);
        const Wide share = free * weight / totalWeight;
        setMain(output[i].size, axis,
                narrow(Wide(mainOf(output[i].size, axis)) + share));
        distributed += share;
    }

    // Weighted division floors each share. Earliest eligible items receive the
    // remaining pixels, giving identical input a stable result on every run.
    Wide remainder = free - distributed;
    for (std::size_t i = begin; i < end && remainder > 0; ++i) {
        if (items[i].grow <= 0)
            continue;
        setMain(output[i].size, axis, mainOf(output[i].size, axis) + 1);
        --remainder;
    }
}

void shrinkLine(std::size_t begin, std::size_t end, Wide deficit,
                eng::ui::FlexAxis axis,
                std::span<const eng::ui::FlexItem> items,
                std::span<eng::ui::UiRect> output) noexcept
{
    while (deficit > 0) {
        Wide totalWeight = 0;
        Wide totalCapacity = 0;
        for (std::size_t i = begin; i < end; ++i) {
            const int capacity =
                mainOf(output[i].size, axis) - minimumMain(items[i], axis);
            if (capacity > 0 && items[i].shrink > 0) {
                totalCapacity += capacity;
                totalWeight += items[i].shrink;
            }
        }
        if (totalCapacity <= 0 || totalWeight <= 0)
            return;

        const Wide round = std::min(deficit, totalCapacity);
        Wide removed = 0;
        for (std::size_t i = begin; i < end; ++i) {
            const int capacity =
                mainOf(output[i].size, axis) - minimumMain(items[i], axis);
            if (capacity <= 0 || items[i].shrink <= 0)
                continue;
            const Wide share = std::min(
                Wide(capacity), round * Wide(items[i].shrink) / totalWeight);
            setMain(output[i].size, axis,
                    mainOf(output[i].size, axis) - static_cast<int>(share));
            removed += share;
        }

        Wide remainder = round - removed;
        for (std::size_t i = begin; i < end && remainder > 0; ++i) {
            const int capacity =
                mainOf(output[i].size, axis) - minimumMain(items[i], axis);
            if (capacity <= 0 || items[i].shrink <= 0)
                continue;
            setMain(output[i].size, axis,
                    mainOf(output[i].size, axis) - 1);
            --remainder;
            ++removed;
        }

        if (removed <= 0)
            return;
        deficit -= removed;
    }
}

} // namespace

namespace eng::ui {

UiRect UiRect::normalized() const noexcept
{
    return {position, {nonNegative(size.x), nonNegative(size.y)}};
}

UiRect UiRect::inset(Insets insets) const noexcept
{
    const UiRect base = normalized();
    const int left = std::min(nonNegative(insets.left), base.size.x);
    const int top = std::min(nonNegative(insets.top), base.size.y);
    const int right =
        std::min(nonNegative(insets.right), base.size.x - left);
    const int bottom =
        std::min(nonNegative(insets.bottom), base.size.y - top);
    return {{narrow(Wide(base.position.x) + left),
             narrow(Wide(base.position.y) + top)},
            {base.size.x - left - right, base.size.y - top - bottom}};
}

UiRect UiRect::intersection(UiRect other) const noexcept
{
    const UiRect a = normalized();
    const UiRect b = other.normalized();
    const Wide left = std::max(Wide(a.position.x), Wide(b.position.x));
    const Wide top = std::max(Wide(a.position.y), Wide(b.position.y));
    const Wide right = std::min(endOf(a.position.x, a.size.x),
                                endOf(b.position.x, b.size.x));
    const Wide bottom = std::min(endOf(a.position.y, a.size.y),
                                 endOf(b.position.y, b.size.y));
    return {{narrow(left), narrow(top)},
            {narrow(std::max(Wide(0), right - left)),
             narrow(std::max(Wide(0), bottom - top))}};
}

UiRect UiRect::contain(UiRect rect) const noexcept
{
    const UiRect outer = normalized();
    UiRect inner = rect.normalized();
    inner.size.x = std::min(inner.size.x, outer.size.x);
    inner.size.y = std::min(inner.size.y, outer.size.y);

    const Wide maxX = endOf(outer.position.x, outer.size.x) - inner.size.x;
    const Wide maxY = endOf(outer.position.y, outer.size.y) - inner.size.y;
    inner.position.x = narrow(std::clamp(Wide(inner.position.x),
                                         Wide(outer.position.x), maxX));
    inner.position.y = narrow(std::clamp(Wide(inner.position.y),
                                         Wide(outer.position.y), maxY));
    return inner;
}

bool UiRect::contains(glm::ivec2 point) const noexcept
{
    const UiRect rect = normalized();
    return point.x >= rect.position.x && point.y >= rect.position.y &&
           Wide(point.x) < endOf(rect.position.x, rect.size.x) &&
           Wide(point.y) < endOf(rect.position.y, rect.size.y);
}

bool UiRect::contains(UiRect rect) const noexcept
{
    const UiRect outer = normalized();
    const UiRect inner = rect.normalized();
    return inner.position.x >= outer.position.x &&
           inner.position.y >= outer.position.y &&
           endOf(inner.position.x, inner.size.x) <=
               endOf(outer.position.x, outer.size.x) &&
           endOf(inner.position.y, inner.size.y) <=
               endOf(outer.position.y, outer.size.y);
}

bool UiRect::empty() const noexcept
{
    return size.x <= 0 || size.y <= 0;
}

FlexLayoutResult layoutFlex(UiRect bounds, const FlexLayout& layout,
                            std::span<const FlexItem> items,
                            std::span<UiRect> output) noexcept
{
    bounds = bounds.normalized();
    const std::size_t count = std::min(items.size(), output.size());
    if (count == 0)
        return {};

    const FlexAxis axis = layout.axis;
    const int availableMain = mainOf(bounds.size, axis);
    const int availableCross = crossOf(bounds.size, axis);
    const int boundMain = mainOf(bounds.position, axis);
    const int boundCross = crossOf(bounds.position, axis);
    const int gap = nonNegative(layout.gap);
    const int lineGap = nonNegative(layout.lineGap);

    Wide usedMain = 0;
    Wide usedCross = 0;
    int lineCount = 0;
    std::size_t begin = 0;
    while (begin < count) {
        const Line line = measureLine(begin, count, availableMain, gap,
                                      layout.wrap, axis, items);
        const std::size_t lineItems = line.end - begin;
        const Wide gaps = Wide(gap) * Wide(lineItems - 1);

        Wide baseItems = 0;
        for (std::size_t i = begin; i < line.end; ++i) {
            const int itemMain = preferredMain(items[i], axis);
            const int itemCross = preferredCross(items[i], axis);
            output[i] = {{0, 0}, axes(itemMain, itemCross, axis)};
            baseItems += itemMain;
        }

        const Wide free = Wide(availableMain) - baseItems - gaps;
        if (free > 0)
            growLine(begin, line.end, free, axis, items, output);
        else if (free < 0)
            shrinkLine(begin, line.end, -free, axis, items, output);

        Wide itemMain = 0;
        for (std::size_t i = begin; i < line.end; ++i)
            itemMain += mainOf(output[i].size, axis);

        const Wide contentMain = itemMain + gaps;
        const Wide remaining =
            std::max(Wide(0), Wide(availableMain) - contentMain);
        Wide leading = 0;
        Wide extraGap = 0;
        Wide gapRemainder = 0;
        switch (layout.justify) {
        case FlexJustify::Start: break;
        case FlexJustify::Centre: leading = remaining / 2; break;
        case FlexJustify::End: leading = remaining; break;
        case FlexJustify::SpaceBetween:
            if (lineItems > 1) {
                extraGap = remaining / Wide(lineItems - 1);
                gapRemainder = remaining % Wide(lineItems - 1);
            }
            break;
        }

        // An unwrapped line owns the full cross axis. Wrapped lines use their
        // natural cross size and stack from the cross-axis start.
        const int lineCross = layout.wrap ? line.naturalCross : availableCross;
        Wide cursor = Wide(boundMain) + leading;
        for (std::size_t i = begin; i < line.end; ++i) {
            const int naturalCross = preferredCross(items[i], axis);
            int itemCross = naturalCross;
            int crossDelta = 0;
            switch (layout.align) {
            case FlexAlign::Start: break;
            case FlexAlign::Centre:
                crossDelta = std::max(0, lineCross - itemCross) / 2;
                break;
            case FlexAlign::End:
                crossDelta = std::max(0, lineCross - itemCross);
                break;
            case FlexAlign::Stretch:
                itemCross = std::max(
                    lineCross, nonNegative(crossOf(items[i].minimumSize, axis)));
                break;
            }

            output[i].position = axes(
                narrow(cursor),
                narrow(Wide(boundCross) + usedCross + crossDelta), axis);
            output[i].size =
                axes(mainOf(output[i].size, axis), itemCross, axis);
            cursor += mainOf(output[i].size, axis);
            if (i + 1 < line.end) {
                const Wide gapIndex = Wide(i - begin);
                cursor += gap + extraGap + (gapIndex < gapRemainder ? 1 : 0);
            }
        }

        const Wide occupiedMain =
            layout.justify == FlexJustify::SpaceBetween && lineItems > 1
                ? contentMain + remaining
                : contentMain;
        usedMain = std::max(usedMain, occupiedMain);
        usedCross += lineCross;
        ++lineCount;
        begin = line.end;
        if (begin < count)
            usedCross += lineGap;
    }

    return {axes(narrow(usedMain), narrow(usedCross), axis), lineCount, count};
}

} // namespace eng::ui
