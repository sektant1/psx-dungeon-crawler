#pragma once

#include <glm/vec2.hpp>

#include <cstddef>
#include <span>

namespace eng::ui {

struct Insets {
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
};

// Integer, half-open screen rectangle. Geometry helpers normalize negative
// sizes to zero before operating on them.
struct UiRect {
    glm::ivec2 position{0, 0};
    glm::ivec2 size{0, 0};

    UiRect normalized() const noexcept;
    UiRect inset(Insets insets) const noexcept;
    UiRect intersection(UiRect other) const noexcept;

    // Moves and, when necessary, shrinks `rect` until it fits inside this rect.
    UiRect contain(UiRect rect) const noexcept;

    bool contains(glm::ivec2 point) const noexcept;
    bool contains(UiRect rect) const noexcept;
    bool empty() const noexcept;
};

enum class FlexAxis { Row, Column };
enum class FlexJustify { Start, Centre, End, SpaceBetween };
enum class FlexAlign { Start, Centre, End, Stretch };

struct FlexItem {
    glm::ivec2 preferredSize{0, 0};
    glm::ivec2 minimumSize{0, 0};
    int grow = 0;
    int shrink = 1;
};

struct FlexLayout {
    FlexAxis axis = FlexAxis::Row;
    FlexJustify justify = FlexJustify::Start;
    FlexAlign align = FlexAlign::Start;
    bool wrap = false;
    int gap = 0;
    int lineGap = 0;
};

struct FlexLayoutResult {
    // Size of the line boxes, excluding leading Centre/End justification.
    // Row maps main/cross to x/y; Column maps them to y/x.
    glm::ivec2 usedExtent{0, 0};
    int lineCount = 0;
    std::size_t itemCount = 0;
};

// Pure, allocation-free flex layout. If `output` is shorter than `items`, only
// its capacity is laid out and itemCount reports that truncated count.
[[nodiscard]] FlexLayoutResult
layoutFlex(UiRect bounds, const FlexLayout& layout,
           std::span<const FlexItem> items,
           std::span<UiRect> output) noexcept;

} // namespace eng::ui
