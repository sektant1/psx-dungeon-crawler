#include <eng/ui/UiScene.h>

#include <eng/ecs/Components.h>
#include <eng/ecs/components/UiComponents.h>
#include <eng/ui/UiCanvas.h>

#include <entt/entt.hpp>

#include <algorithm>
#include <cmath>

namespace eng::ui {
namespace {

using ecs::UiBar;
using ecs::UiIcon;
using ecs::UiLabel;
using ecs::UiList;
using ecs::UiPanel;

// Tones and styles arrive as ints from the field table, so every one of them is
// clamped on the way in. An out-of-range value is an authoring mistake, and the
// useful response is the nearest legal look rather than reading past an enum.
UiTone toneOf(int value)
{
    return UiTone(std::clamp(value, 0, int(UiTone::Edge)));
}
PanelStyle panelStyleOf(int value)
{
    return PanelStyle(std::clamp(value, 0, int(PanelStyle::Sunken)));
}
RailEdge railOf(int value)
{
    return RailEdge(std::clamp(value, 0, int(RailEdge::Bottom)));
}
Align alignOf(int value)
{
    return Align(std::clamp(value, 0, int(Align::Right)));
}

} // namespace

// The one place row pitch is decided. Public-by-header (rowAt) and used by the
// paint below, so a hit test and the pixels it tests against cannot disagree.
int rowPitch(float rowHeight, float rowGap)
{
    return std::max(1, int(std::lround(rowHeight + rowGap)));
}

namespace {

// An authored vec3 as the packed colour imgui wants. Alpha is always opaque:
// a UI element fades by its panel's opacity or not at all, and a per-element
// alpha would be a second, quieter way to hide something that the visibility
// flag already does honestly.
unsigned int packed(glm::vec3 c)
{
    const auto byte = [](float v) {
        return (unsigned int)std::clamp(int(std::lround(v * 255.0f)), 0, 255);
    };
    return 0xFF000000u | (byte(c.b) << 16) | (byte(c.g) << 8) | byte(c.r);
}

glm::ivec2 roundTo(glm::vec2 v)
{
    return {int(std::lround(v.x)), int(std::lround(v.y))};
}

// One child's box from its parent's. Kept a free function so the editor's
// gizmo maths -- which has to invert it to turn a dragged pixel back into an
// offset -- reads against the same four lines the paint path uses.
UiRect resolve(const ecs::UiRect& rect, const UiRect& parent)
{
    const glm::vec2 origin(parent.position);
    const glm::vec2 size(parent.size);
    const glm::vec2 min = origin + size * rect.anchorMin + rect.offsetMin;
    const glm::vec2 max = origin + size * rect.anchorMax + rect.offsetMax;
    UiRect out;
    out.position = roundTo(min);
    // Rounded from the corners rather than from the size, so a box's edges land
    // on the same pixel as a neighbour that shares them. Rounding a size
    // instead is what makes two abutting panels overlap by one pixel at some
    // scales and gap by one at others.
    out.size = roundTo(max) - out.position;
    return out.normalized();
}

// Depth-first walk in paint order, so a parent is always solved before the
// children that resolve against it.
void solveInto(const entt::basic_registry<entt::entity>& registry,
               entt::entity entity, const UiRect& parentBounds, int depth,
               std::vector<UiSolvedRect>& out)
{
    const ecs::UiRect* rect = registry.try_get<ecs::UiRect>(entity);
    if (!rect || !rect->visible)
        return;

    const UiRect bounds = resolve(*rect, parentBounds);
    out.push_back({entity, bounds, depth});

    const ecs::Children* children = registry.try_get<ecs::Children>(entity);
    if (!children)
        return;
    // Sorted by `order`, stable so scene order breaks ties. Copied rather than
    // sorted in place: the component belongs to the scene and a paint must not
    // reorder it.
    std::vector<entt::entity> sorted(children->value.begin(),
                                     children->value.end());
    std::stable_sort(sorted.begin(), sorted.end(),
                     [&registry](entt::entity a, entt::entity b) {
                         const ecs::UiRect* ra = registry.try_get<ecs::UiRect>(a);
                         const ecs::UiRect* rb = registry.try_get<ecs::UiRect>(b);
                         return (ra ? ra->order : 0) < (rb ? rb->order : 0);
                     });
    for (const entt::entity child : sorted)
        if (registry.valid(child))
            solveInto(registry, child, bounds, depth + 1, out);
}

} // namespace

void solveUiLayout(const entt::basic_registry<entt::entity>& registry,
                   UiRect surface, std::vector<UiSolvedRect>& out)
{
    out.clear();

    // Roots first: a UI entity whose parent is not itself a UI entity anchors
    // against the surface. That covers both a top-level screen and one parented
    // to a plain grouping entity, which is how the outliner keeps a scene tidy.
    std::vector<entt::entity> roots;
    for (const entt::entity e : registry.view<const ecs::UiRect>()) {
        const ecs::Parent* parent = registry.try_get<ecs::Parent>(e);
        const bool parented = parent && parent->value != entt::null &&
                              registry.valid(parent->value) &&
                              registry.all_of<ecs::UiRect>(parent->value);
        if (!parented)
            roots.push_back(e);
    }
    std::stable_sort(roots.begin(), roots.end(),
                     [&registry](entt::entity a, entt::entity b) {
                         return registry.get<ecs::UiRect>(a).order <
                                registry.get<ecs::UiRect>(b).order;
                     });
    for (const entt::entity root : roots)
        solveInto(registry, root, surface, 0, out);
}

void paintUiScene(UiCanvas& canvas,
                  const entt::basic_registry<entt::entity>& registry,
                  const std::vector<UiSolvedRect>& solved,
                  const UiDataSource* data)
{
    const UiPalette& palette = canvas.palette();

    for (const UiSolvedRect& item : solved) {
        const entt::entity e = item.entity;
        const glm::ivec2 at = item.bounds.position;
        const glm::ivec2 size = item.bounds.size;
        if (size.x <= 0 || size.y <= 0)
            continue;

        if (const UiPanel* panel = registry.try_get<UiPanel>(e)) {
            PanelPaint paint = canvas.style().panel;
            paint.style = panelStyleOf(panel->style);
            paint.rail = railOf(panel->rail);
            paint.railTone = toneOf(panel->railTone);
            canvas.panel(at, size, paint, toneOf(panel->railTone),
                         std::clamp(panel->opacity, 0.0f, 1.0f));
        }

        if (const UiIcon* icon = registry.try_get<UiIcon>(e))
            canvas.icon(at, size,
                        icon->useColour ? packed(icon->colour)
                                        : canvas.colour(toneOf(icon->tone)),
                        icon->inset);

        if (const UiBar* bar = registry.try_get<UiBar>(e)) {
            float ratio = bar->ratio;
            if (data && !bar->binding.empty())
                data->number(bar->binding, ratio);
            canvas.bar(at, size, std::clamp(ratio, 0.0f, 1.0f),
                       bar->useFillColour
                           ? packed(bar->fillColour)
                           : canvas.colour(toneOf(bar->fillTone)),
                       canvas.colour(toneOf(bar->trackTone)));
        }

        if (const UiLabel* label = registry.try_get<UiLabel>(e)) {
            std::string value = label->text;
            if (data && !label->binding.empty())
                data->text(label->binding, value);
            const Align align = alignOf(label->align);
            // Anchored to the edge the alignment names, so a right-aligned
            // label sits against the right of its own box rather than of the
            // screen -- which is what an author means by putting it in a box.
            const int x = align == Align::Left     ? at.x
                          : align == Align::Centre ? at.x + size.x / 2
                                                   : at.x + size.x;
            const int textScale = std::clamp(label->textScale, 1, 8);
            // Fitted in the face and size it will actually be drawn at: a label
            // ellipsized against the default font then drawn at 2x in another
            // one overflows its box by however much the two differ.
            const std::string fitted =
                canvas.fontFor(label->font).ellipsize(value,
                                                      size.x / textScale);
            canvas.text({x, at.y}, fitted,
                        label->useColour ? packed(label->colour)
                                         : canvas.colour(toneOf(label->tone)),
                        align, label->shadow, label->font, textScale);
        }

        if (const UiList* list = registry.try_get<UiList>(e)) {
            std::vector<UiDataSource::Row> rows;
            const int wanted = std::max(1, list->maxRows);
            if (data && !list->source.empty())
                data->rows(list->source, wanted, rows);
            const int pitch = rowPitch(list->rowHeight, list->rowGap);
            const int fits = std::max(0, size.y / pitch);
            const int count =
                std::min({int(rows.size()), wanted, fits});
            for (int i = 0; i < count; ++i) {
                const UiDataSource::Row& row = rows[std::size_t(i)];
                const int y = at.y + i * pitch;
                if (row.selected)
                    canvas.rect({at.x, y - 1}, {size.x, pitch},
                                canvas.colour(toneOf(list->selectedTone)) &
                                    0x40FFFFFFu);
                // Dim wins over selected, and the plate behind the row is what
                // shows the cursor. The other order reads as "this became
                // available when I moved onto it": in a shop, dim means you
                // cannot afford it, and that does not stop being true because
                // the cursor is there.
                const UiTone tone = row.dim ? UiTone::Muted
                                    : row.selected ? toneOf(list->selectedTone)
                                                   : toneOf(list->tone);
                // The value column is measured and reserved before the label is
                // fitted, so a long name shortens instead of running under the
                // number beside it.
                const int rowScale = std::clamp(list->textScale, 1, 8);
                const int valueWidth =
                    list->showValues && !row.value.empty()
                        ? canvas.measureIn(row.value, list->font, rowScale).x + 4
                        : 0;
                canvas.text({at.x + 2, y},
                            canvas.fontFor(list->font).ellipsize(
                                row.label, (size.x - 4 - valueWidth) / rowScale),
                            canvas.colour(tone), Align::Left, false, list->font,
                            rowScale);
                if (valueWidth > 0)
                    canvas.text({at.x + size.x - 2, y}, row.value,
                                canvas.colour(tone), Align::Right, false,
                                list->font, rowScale);
                if (row.ratio >= 0.0f) {
                    const int barY = y + int(list->rowHeight) - 1;
                    canvas.rect({at.x + 2, barY}, {size.x - 4, 1},
                                palette.inkSoft);
                    canvas.rect({at.x + 2, barY},
                                {int(float(size.x - 4) *
                                     std::clamp(row.ratio, 0.0f, 1.0f)),
                                 1},
                                canvas.colour(tone));
                }
            }
        }
    }
}

void drawUiScene(UiCanvas& canvas,
                 const entt::basic_registry<entt::entity>& registry,
                 const UiDataSource* data)
{
    UiRect surface;
    surface.size = canvas.size();
    std::vector<UiSolvedRect> solved;
    solveUiLayout(registry, surface, solved);
    paintUiScene(canvas, registry, solved, data);
}

int rowAt(const UiRect& listBounds, float rowHeight, float rowGap,
          glm::ivec2 point)
{
    if (!listBounds.contains(point))
        return -1;
    const int pitch = rowPitch(rowHeight, rowGap);
    const int row = (point.y - listBounds.position.y) / pitch;
    // The caller clamps against how many rows the data source actually
    // produced; this only says which slot was hit, because the geometry is all
    // it knows.
    return row >= 0 ? row : -1;
}

UiListHit pickUiRow(const entt::basic_registry<entt::entity>& registry,
                    const std::vector<UiSolvedRect>& solved, glm::ivec2 point)
{
    for (auto it = solved.rbegin(); it != solved.rend(); ++it) {
        const UiList* list = registry.try_get<UiList>(it->entity);
        if (!list)
            continue;
        const int row = rowAt(it->bounds, list->rowHeight, list->rowGap, point);
        if (row >= 0)
            return {it->entity, row};
    }
    return {};
}

entt::entity pickUi(const std::vector<UiSolvedRect>& solved, glm::ivec2 point)
{
    // Backwards: the list is in paint order, so the last box containing the
    // point is the one drawn on top and therefore the one the user clicked.
    for (auto it = solved.rbegin(); it != solved.rend(); ++it)
        if (it->bounds.contains(point))
            return it->entity;
    return entt::null;
}

} // namespace eng::ui
