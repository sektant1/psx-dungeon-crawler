#include <eng/ui/UiLayout.h>

#include <array>
#include <cstdlib>
#include <iostream>

using namespace eng::ui;

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "UiLayoutTests: " << message << '\n';
        std::exit(1);
    }
}

bool equal(glm::ivec2 a, glm::ivec2 b) { return a.x == b.x && a.y == b.y; }

bool equal(UiRect a, UiRect b)
{
    return equal(a.position, b.position) && equal(a.size, b.size);
}

FlexItem item(int width, int height)
{
    FlexItem value;
    value.preferredSize = {width, height};
    value.shrink = 0;
    return value;
}

} // namespace

int main()
{
    // Row justification keeps item order and distributes only free pixels.
    {
        const std::array items{item(4, 2), item(4, 2)};
        std::array<UiRect, 2> output{};
        FlexLayout layout;
        const UiRect bounds{{0, 0}, {20, 10}};

        layout.justify = FlexJustify::Start;
        (void)layoutFlex(bounds, layout, items, output);
        require(equal(output[0].position, {0, 0}) &&
                    equal(output[1].position, {4, 0}),
                "row Start must pack against the leading edge");

        layout.justify = FlexJustify::Centre;
        (void)layoutFlex(bounds, layout, items, output);
        require(equal(output[0].position, {6, 0}) &&
                    equal(output[1].position, {10, 0}),
                "row Centre must split leading and trailing space");

        layout.justify = FlexJustify::End;
        (void)layoutFlex(bounds, layout, items, output);
        require(equal(output[0].position, {12, 0}) &&
                    equal(output[1].position, {16, 0}),
                "row End must pack against the trailing edge");

        layout.justify = FlexJustify::SpaceBetween;
        const FlexLayoutResult result = layoutFlex(bounds, layout, items, output);
        require(equal(output[0].position, {0, 0}) &&
                    equal(output[1].position, {16, 0}),
                "SpaceBetween must place free space between items");
        require(equal(result.usedExtent, {20, 10}) && result.lineCount == 1,
                "result must report occupied line extent and count");
    }

    // Five growth pixels divide 2,2,1: remainder follows stable item order.
    {
        std::array<FlexItem, 3> items{item(2, 1), item(2, 1), item(2, 1)};
        for (FlexItem& value : items)
            value.grow = 1;
        std::array<UiRect, 3> output{};
        const FlexLayoutResult result =
            layoutFlex({{0, 0}, {11, 3}}, {}, items, output);
        require(output[0].size.x == 4 && output[1].size.x == 4 &&
                    output[2].size.x == 3,
                "grow remainder must favor earliest eligible items");
        require(output[0].position.x == 0 && output[1].position.x == 4 &&
                    output[2].position.x == 8,
                "grown items must remain contiguous");
        require(result.itemCount == 3, "result must report laid-out item count");
    }

    // Shrink redistributes pressure after an item reaches its minimum.
    {
        std::array<FlexItem, 2> items{};
        items[0].preferredSize = {10, 2};
        items[0].minimumSize = {8, 1};
        items[0].shrink = 1;
        items[1].preferredSize = {10, 2};
        items[1].minimumSize = {2, 1};
        items[1].shrink = 1;
        std::array<UiRect, 2> output{};
        (void)layoutFlex({{0, 0}, {12, 4}}, {}, items, output);
        require(output[0].size.x == 8 && output[1].size.x == 4,
                "shrink must respect minimums and redistribute deficit");
        require(output[1].position.x == 8,
                "shrunk items must use final preceding size");
    }

    // Cross-axis alignment uses the full unwrapped line box.
    {
        const std::array items{item(4, 4)};
        std::array<UiRect, 1> output{};
        FlexLayout layout;
        const UiRect bounds{{2, 3}, {20, 10}};

        layout.align = FlexAlign::Start;
        (void)layoutFlex(bounds, layout, items, output);
        require(output[0].position.y == 3 && output[0].size.y == 4,
                "Start alignment must preserve cross size");
        layout.align = FlexAlign::Centre;
        (void)layoutFlex(bounds, layout, items, output);
        require(output[0].position.y == 6 && output[0].size.y == 4,
                "Centre alignment must center on cross axis");
        layout.align = FlexAlign::End;
        (void)layoutFlex(bounds, layout, items, output);
        require(output[0].position.y == 9 && output[0].size.y == 4,
                "End alignment must use trailing cross edge");
        layout.align = FlexAlign::Stretch;
        (void)layoutFlex(bounds, layout, items, output);
        require(output[0].position.y == 3 && output[0].size.y == 10,
                "Stretch must fill cross axis");
    }

    // Wrapping breaks before overflow and stacks natural-height lines.
    {
        const std::array items{item(4, 2), item(4, 2), item(4, 2)};
        std::array<UiRect, 3> output{};
        FlexLayout layout;
        layout.wrap = true;
        const FlexLayoutResult result =
            layoutFlex({{0, 0}, {8, 20}}, layout, items, output);
        require(equal(output[0].position, {0, 0}) &&
                    equal(output[1].position, {4, 0}) &&
                    equal(output[2].position, {0, 2}),
                "wrapping must preserve order across lines");
        require(result.lineCount == 2 && equal(result.usedExtent, {8, 4}),
                "wrapped result must report natural line extent");
    }

    // Column swaps main/cross calculations without changing semantics.
    {
        const std::array items{item(3, 4), item(3, 4)};
        std::array<UiRect, 2> output{};
        FlexLayout layout;
        layout.axis = FlexAxis::Column;
        layout.justify = FlexJustify::Centre;
        layout.align = FlexAlign::End;
        const FlexLayoutResult result =
            layoutFlex({{0, 0}, {10, 20}}, layout, items, output);
        require(equal(output[0].position, {7, 6}) &&
                    equal(output[1].position, {7, 10}),
                "column must justify vertically and align horizontally");
        require(equal(result.usedExtent, {10, 8}),
                "column result must map cross/main back to x/y");
    }

    // Rect helpers use half-open containment and never return negative sizes.
    {
        const UiRect outer{{10, 20}, {20, 10}};
        require(outer.contains({10, 20}) && !outer.contains({30, 30}),
                "point containment must use half-open edges");
        require(outer.contains(UiRect{{12, 22}, {4, 4}}),
                "rect containment must accept inner rects");
        require(equal(outer.inset({2, 3, 4, 5}),
                      UiRect{{12, 23}, {14, 2}}),
                "inset must consume named edges");
        require(equal(outer.intersection({{25, 15}, {10, 10}}),
                      UiRect{{25, 20}, {5, 5}}),
                "intersection must return shared area");
        require(equal(outer.contain({{25, 15}, {10, 20}}),
                      UiRect{{20, 20}, {10, 10}}),
                "contain must resize and reposition into outer rect");
        require(equal(UiRect{{1, 2}, {-3, -4}}.normalized(),
                      UiRect{{1, 2}, {0, 0}}),
                "normalization must clamp negative size");
    }

    // Negative item and bound sizes collapse cleanly; no output dimension can
    // become negative through layout arithmetic.
    {
        const std::array items{item(-8, -3)};
        std::array<UiRect, 1> output{};
        const FlexLayoutResult result =
            layoutFlex({{5, 7}, {-10, -20}}, {}, items, output);
        require(equal(output[0].size, {0, 0}) &&
                    equal(result.usedExtent, {0, 0}),
                "negative bounds and preferred sizes must clamp to zero");
    }

    // Same values repeatedly produce byte-for-byte-equivalent geometry.
    {
        std::array<FlexItem, 5> items{item(3, 2), item(5, 3), item(2, 1),
                                      item(4, 2), item(1, 4)};
        for (std::size_t i = 0; i < items.size(); ++i)
            items[i].grow = static_cast<int>(i % 3) + 1;
        FlexLayout layout;
        layout.wrap = true;
        layout.gap = 1;
        layout.lineGap = 2;
        layout.justify = FlexJustify::SpaceBetween;
        layout.align = FlexAlign::Centre;

        std::array<UiRect, 5> expected{};
        const FlexLayoutResult expectedResult =
            layoutFlex({{-3, 7}, {13, 30}}, layout, items, expected);
        for (int repeat = 0; repeat < 32; ++repeat) {
            std::array<UiRect, 5> actual{};
            const FlexLayoutResult actualResult =
                layoutFlex({{-3, 7}, {13, 30}}, layout, items, actual);
            require(equal(actualResult.usedExtent, expectedResult.usedExtent) &&
                        actualResult.lineCount == expectedResult.lineCount &&
                        actualResult.itemCount == expectedResult.itemCount,
                    "repeated result metadata must be deterministic");
            for (std::size_t i = 0; i < actual.size(); ++i)
                require(equal(actual[i], expected[i]),
                        "repeated item geometry must be deterministic");
        }
    }

    std::cout << "UiLayoutTests: OK\n";
    return 0;
}
