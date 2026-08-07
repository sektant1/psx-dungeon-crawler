// Screen-space UI layout.
//
// The rules asserted here are the ones whose failure is invisible until an
// author is already fighting the editor: a box that does not land where its
// anchors say, a child that resolves against the screen instead of its parent,
// a hidden screen that still swallows clicks, and a picker that returns the
// panel under the label you aimed at.
#include <eng/ecs/Components.h>
#include <eng/ecs/components/UiComponents.h>
#include <eng/ui/UiScene.h>

#include <cstdio>

using namespace eng;
using namespace eng::ecs;

static int failures = 0;
static void check(bool c, const char* m)
{
    if (!c) { std::fprintf(stderr, "FAIL: %s\n", m); ++failures; }
}

namespace {

// The surface every test resolves against: 320x240 virtual pixels.
ui::UiRect surface()
{
    ui::UiRect r;
    r.size = {320, 240};
    return r;
}

const ui::UiSolvedRect* find(const std::vector<ui::UiSolvedRect>& solved,
                             entt::entity e)
{
    for (const ui::UiSolvedRect& s : solved)
        if (s.entity == e)
            return &s;
    return nullptr;
}

// Parents `child` to `owner` the way the ECS does: both halves of the link, so
// the solver's Children walk and its Parent check agree.
void attach(entt::registry& reg, entt::entity owner, entt::entity child)
{
    reg.emplace_or_replace<Parent>(child, Parent{owner});
    reg.get_or_emplace<Children>(owner).value.push_back(child);
}

} // namespace

int main()
{
    // --- a fixed box pinned to one corner -----------------------------------
    //
    // Equal anchors: the offsets are pixels from that anchor point, which is
    // the case every hand-placed widget uses.
    {
        entt::registry reg;
        const entt::entity e = reg.create();
        UiRect rect;
        rect.anchorMin = rect.anchorMax = {0.0f, 0.0f};
        rect.offsetMin = {10.0f, 20.0f};
        rect.offsetMax = {110.0f, 44.0f};
        reg.emplace<UiRect>(e, rect);

        std::vector<ui::UiSolvedRect> solved;
        ui::solveUiLayout(reg, surface(), solved);
        check(solved.size() == 1, "one box solved");
        const ui::UiSolvedRect* box = find(solved, e);
        check(box && box->bounds.position == glm::ivec2(10, 20),
              "pinned at its offset");
        check(box && box->bounds.size == glm::ivec2(100, 24),
              "sized by the offset span");
    }

    // --- a stretched box ----------------------------------------------------
    //
    // Spread anchors: the offsets are insets from each edge, and a negative max
    // offset is a margin from the far side. This is the case that makes a
    // screen resize with the window, and getting the sign wrong is the single
    // most common RectTransform mistake.
    {
        entt::registry reg;
        const entt::entity e = reg.create();
        UiRect rect;
        rect.anchorMin = {0.0f, 0.0f};
        rect.anchorMax = {1.0f, 1.0f};
        rect.offsetMin = {16.0f, 12.0f};
        rect.offsetMax = {-16.0f, -12.0f};
        reg.emplace<UiRect>(e, rect);

        std::vector<ui::UiSolvedRect> solved;
        ui::solveUiLayout(reg, surface(), solved);
        const ui::UiSolvedRect* box = find(solved, e);
        check(box && box->bounds.position == glm::ivec2(16, 12),
              "inset from the near edges");
        check(box && box->bounds.size == glm::ivec2(320 - 32, 240 - 24),
              "and from the far ones");
    }

    // --- children resolve against their parent ------------------------------
    {
        entt::registry reg;
        const entt::entity parent = reg.create();
        UiRect outer;
        outer.anchorMin = outer.anchorMax = {0.0f, 0.0f};
        outer.offsetMin = {40.0f, 40.0f};
        outer.offsetMax = {240.0f, 140.0f}; // 200x100 at (40,40)
        reg.emplace<UiRect>(parent, outer);

        const entt::entity child = reg.create();
        UiRect inner;
        inner.anchorMin = {0.0f, 0.0f};
        inner.anchorMax = {1.0f, 0.0f};
        inner.offsetMin = {5.0f, 5.0f};
        inner.offsetMax = {-5.0f, 17.0f};
        reg.emplace<UiRect>(child, inner);
        attach(reg, parent, child);

        std::vector<ui::UiSolvedRect> solved;
        ui::solveUiLayout(reg, surface(), solved);
        const ui::UiSolvedRect* box = find(solved, child);
        check(box && box->bounds.position == glm::ivec2(45, 45),
              "child offset is from the parent's origin");
        check(box && box->bounds.size == glm::ivec2(190, 12),
              "and its width stretches with the parent, not the screen");
        check(box && box->depth == 1, "depth records the nesting");
        // Paint order: a parent must be solved before the child that resolves
        // against it, or the child would read a stale box.
        check(solved.size() == 2 && solved[0].entity == parent,
              "parents come first");
    }

    // --- hiding prunes the subtree ------------------------------------------
    //
    // Not "marked hidden": omitted. A caller that only checked the root's flag
    // would still hit-test the children of a closed screen.
    {
        entt::registry reg;
        const entt::entity parent = reg.create();
        UiRect outer;
        outer.visible = false;
        reg.emplace<UiRect>(parent, outer);
        const entt::entity child = reg.create();
        reg.emplace<UiRect>(child, UiRect{});
        attach(reg, parent, child);

        std::vector<ui::UiSolvedRect> solved;
        ui::solveUiLayout(reg, surface(), solved);
        check(solved.empty(), "a hidden screen contributes nothing at all");
    }

    // --- paint order among siblings -----------------------------------------
    {
        entt::registry reg;
        const entt::entity parent = reg.create();
        reg.emplace<UiRect>(parent, UiRect{});
        const entt::entity late = reg.create();
        UiRect lateRect;
        lateRect.order = 10;
        reg.emplace<UiRect>(late, lateRect);
        const entt::entity early = reg.create();
        UiRect earlyRect;
        earlyRect.order = -5;
        reg.emplace<UiRect>(early, earlyRect);
        // Attached in the wrong order on purpose: `order` has to win over the
        // order the author happened to add them in.
        attach(reg, parent, late);
        attach(reg, parent, early);

        std::vector<ui::UiSolvedRect> solved;
        ui::solveUiLayout(reg, surface(), solved);
        check(solved.size() == 3, "all three solved");
        check(solved[1].entity == early && solved[2].entity == late,
              "lower order paints first");
    }

    // --- picking ------------------------------------------------------------
    //
    // Topmost wins, because that is what the user sees. The naive answer --
    // first match in paint order -- returns the backing panel every time and
    // makes the label on top unselectable.
    {
        entt::registry reg;
        const entt::entity panel = reg.create();
        UiRect panelRect;
        panelRect.anchorMin = panelRect.anchorMax = {0.0f, 0.0f};
        panelRect.offsetMin = {0.0f, 0.0f};
        panelRect.offsetMax = {100.0f, 100.0f};
        reg.emplace<UiRect>(panel, panelRect);

        const entt::entity label = reg.create();
        UiRect labelRect;
        labelRect.anchorMin = labelRect.anchorMax = {0.0f, 0.0f};
        labelRect.offsetMin = {10.0f, 10.0f};
        labelRect.offsetMax = {50.0f, 24.0f};
        reg.emplace<UiRect>(label, labelRect);
        attach(reg, panel, label);

        std::vector<ui::UiSolvedRect> solved;
        ui::solveUiLayout(reg, surface(), solved);
        check(ui::pickUi(solved, {20, 15}) == label,
              "the label on top is picked, not the panel under it");
        check(ui::pickUi(solved, {80, 80}) == panel,
              "and the panel where the label is not");
        check(ui::pickUi(solved, {200, 200}) == entt::null,
              "empty space picks nothing");
    }

    // --- row hit-testing ------------------------------------------------------
    //
    // The pitch here must be the one paintUiScene draws with, or a click lands
    // on the row above the one under the cursor -- which looks like the list
    // being off by one and is maddening to track down.
    {
        ui::UiRect list;
        list.position = {10, 20};
        list.size = {100, 80};
        // 17 + 2 = 19px pitch, the value the shipped screens use.
        check(ui::rowAt(list, 17.0f, 2.0f, {50, 20}) == 0, "the first row");
        check(ui::rowAt(list, 17.0f, 2.0f, {50, 38}) == 0, "still the first");
        check(ui::rowAt(list, 17.0f, 2.0f, {50, 39}) == 1, "the second begins");
        check(ui::rowAt(list, 17.0f, 2.0f, {50, 96}) == 4, "the fifth");
        check(ui::rowAt(list, 17.0f, 2.0f, {50, 10}) == -1, "above the list");
        check(ui::rowAt(list, 17.0f, 2.0f, {200, 40}) == -1, "beside the list");
    }

    // Picking a list through the registry: topmost wins, as with entities.
    {
        entt::registry reg;
        const entt::entity plate = reg.create();
        UiRect plateRect;
        plateRect.anchorMin = plateRect.anchorMax = {0.0f, 0.0f};
        plateRect.offsetMin = {0.0f, 0.0f};
        plateRect.offsetMax = {200.0f, 200.0f};
        reg.emplace<UiRect>(plate, plateRect);
        // A panel with no list: a point over it must not report a row.
        reg.emplace<UiPanel>(plate, UiPanel{});

        const entt::entity rows = reg.create();
        UiRect rowsRect;
        rowsRect.anchorMin = rowsRect.anchorMax = {0.0f, 0.0f};
        rowsRect.offsetMin = {10.0f, 10.0f};
        rowsRect.offsetMax = {150.0f, 120.0f};
        reg.emplace<UiRect>(rows, rowsRect);
        UiList list;
        list.rowHeight = 17.0f;
        list.rowGap = 2.0f;
        reg.emplace<UiList>(rows, list);
        attach(reg, plate, rows);

        std::vector<ui::UiSolvedRect> solved;
        ui::solveUiLayout(reg, surface(), solved);
        const ui::UiListHit hit = ui::pickUiRow(reg, solved, {50, 48});
        check(hit.entity == rows, "the list under the point");
        check(hit.row == 2, "and the row within it");
        check(ui::pickUiRow(reg, solved, {180, 180}).entity == entt::null,
              "a panel with no list reports nothing");
    }

    if (failures == 0)
        std::printf("UiSceneTests OK\n");
    return failures == 0 ? 0 : 1;
}
