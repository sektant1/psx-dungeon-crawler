#include <editor/ui/UiSceneEditor.h>

#include <editor/content/SceneInstancing.h>

#include <eng/ecs/Components.h>
#include <eng/ui/UiCanvas.h>

#include <algorithm>
#include <unordered_map>

namespace ed {
namespace {

using game::content::AuthorId;
using game::content::Entity;
using game::content::SceneDocument;

} // namespace

void UiSceneEditor::rebuild(const SceneDocument& source, glm::ivec2 virtualSize,
                            const std::string& assetRoot)
{
    mRegistry.clear();
    mSolved.clear();
    mByAuthor.clear();

    // Expanded into a copy: the caller's document is what the author is
    // editing, and an expanded one would show the atom's insides in the
    // outliner as if they were this scene's to edit.
    //
    // A failure leaves the document unexpanded rather than empty -- the
    // placements still draw as their own boxes, which is more useful than a
    // blank viewport while the author fixes the path.
    SceneDocument document = source;
    std::string error;
    if (!game::content::expandInstances(document, assetRoot, error))
        document = source;

    // Two passes: create every UI entity first, then link parents. One pass
    // would have to handle a child authored above its parent in the file, which
    // the format permits and the outliner produces routinely.
    std::unordered_map<std::string, entt::entity> byId;
    for (const Entity& authored : document.entities) {
        if (!authored.ui)
            continue;
        const entt::entity e = mRegistry.create();
        mRegistry.emplace<eng::ecs::UiRect>(e, authored.ui->rect);
        if (authored.ui->panel)
            mRegistry.emplace<eng::ecs::UiPanel>(e, *authored.ui->panel);
        if (authored.ui->label)
            mRegistry.emplace<eng::ecs::UiLabel>(e, *authored.ui->label);
        if (authored.ui->bar)
            mRegistry.emplace<eng::ecs::UiBar>(e, *authored.ui->bar);
        if (authored.ui->icon)
            mRegistry.emplace<eng::ecs::UiIcon>(e, *authored.ui->icon);
        if (authored.ui->list)
            mRegistry.emplace<eng::ecs::UiList>(e, *authored.ui->list);
        byId.emplace(authored.id, e);
    }
    for (const Entity& authored : document.entities) {
        if (!authored.ui || authored.parent.empty())
            continue;
        const auto self = byId.find(authored.id);
        const auto parent = byId.find(authored.parent);
        // A UI entity parented to a *non-UI* entity (a plain group, which is
        // how the outliner tidies a scene) anchors against the surface instead.
        // That is the solver's own rule, so simply not linking it is correct.
        if (self == byId.end() || parent == byId.end())
            continue;
        mRegistry.emplace_or_replace<eng::ecs::Parent>(
            self->second, eng::ecs::Parent{parent->second});
        mRegistry.get_or_emplace<eng::ecs::Children>(parent->second)
            .value.push_back(self->second);
    }

    eng::ui::UiRect surface;
    surface.size = virtualSize;
    eng::ui::solveUiLayout(mRegistry, surface, mSolved);

    // The reverse map, for boundsOf and pick. Built from the solved list rather
    // than from the document so a hidden entity is absent from both -- the
    // editor must not offer a handle on something the author cannot see.
    std::unordered_map<entt::entity, const Entity*> back;
    for (const auto& [id, e] : byId) {
        (void)id;
        back.emplace(e, nullptr);
    }
    for (const Entity& authored : document.entities) {
        if (!authored.ui)
            continue;
        const auto found = byId.find(authored.id);
        if (found != byId.end())
            back[found->second] = &authored;
    }
    mByAuthor.reserve(mSolved.size());
    for (const eng::ui::UiSolvedRect& s : mSolved) {
        const auto found = back.find(s.entity);
        if (found != back.end() && found->second)
            mByAuthor.push_back({found->second->id, s.bounds});
    }
}

void UiSceneEditor::paint(eng::ui::UiCanvas& canvas,
                          const eng::ui::UiDataSource* data) const
{
    eng::ui::paintUiScene(canvas, mRegistry, mSolved, data);
}

AuthorId UiSceneEditor::pick(glm::ivec2 point) const
{
    // Backwards, for the reason eng::ui::pickUi goes backwards: the list is in
    // paint order and the topmost box is the one the author sees under the
    // cursor.
    for (auto it = mByAuthor.rbegin(); it != mByAuthor.rend(); ++it)
        if (it->bounds.contains(point))
            return it->id;
    return {};
}

bool UiSceneEditor::boundsOf(const AuthorId& id, eng::ui::UiRect& out) const
{
    for (const Solved& s : mByAuthor) {
        if (s.id != id)
            continue;
        out = s.bounds;
        return true;
    }
    return false;
}

UiSceneEditor::Handle UiSceneEditor::handleAt(const eng::ui::UiRect& bounds,
                                              glm::ivec2 point, int grabPixels)
{
    const int grab = std::max(1, grabPixels);
    const glm::ivec2 min = bounds.position;
    const glm::ivec2 max = bounds.position + bounds.size;
    // Generous outward, tight inward: a handle you can only grab from *inside*
    // the box is unusable on a box a few pixels tall, which describes every
    // label in a UI.
    if (point.x < min.x - grab || point.x > max.x + grab ||
        point.y < min.y - grab || point.y > max.y + grab)
        return Handle::None;

    const bool left = std::abs(point.x - min.x) <= grab;
    const bool right = std::abs(point.x - max.x) <= grab;
    const bool top = std::abs(point.y - min.y) <= grab;
    const bool bottom = std::abs(point.y - max.y) <= grab;

    // Corners before edges: at a corner both tests pass, and resizing one axis
    // when the author grabbed a corner is the wrong answer every time.
    if (left && top)     return Handle::TopLeft;
    if (right && top)    return Handle::TopRight;
    if (left && bottom)  return Handle::BottomLeft;
    if (right && bottom) return Handle::BottomRight;
    if (left)   return Handle::Left;
    if (right)  return Handle::Right;
    if (top)    return Handle::Top;
    if (bottom) return Handle::Bottom;
    return bounds.contains(point) ? Handle::Body : Handle::None;
}

bool UiSceneEditor::applyDrag(Entity& entity, Handle handle,
                              glm::ivec2 deltaPixels, int minimumSize)
{
    if (!entity.ui || handle == Handle::None ||
        (deltaPixels.x == 0 && deltaPixels.y == 0))
        return false;

    eng::ecs::UiRect& rect = entity.ui->rect;
    const glm::vec2 before[2] = {rect.offsetMin, rect.offsetMax};
    const glm::vec2 delta(float(deltaPixels.x), float(deltaPixels.y));

    switch (handle) {
    case Handle::Body:
        rect.offsetMin += delta;
        rect.offsetMax += delta;
        break;
    case Handle::Left:        rect.offsetMin.x += delta.x; break;
    case Handle::Right:       rect.offsetMax.x += delta.x; break;
    case Handle::Top:         rect.offsetMin.y += delta.y; break;
    case Handle::Bottom:      rect.offsetMax.y += delta.y; break;
    case Handle::TopLeft:     rect.offsetMin += delta; break;
    case Handle::BottomRight: rect.offsetMax += delta; break;
    case Handle::TopRight:
        rect.offsetMax.x += delta.x;
        rect.offsetMin.y += delta.y;
        break;
    case Handle::BottomLeft:
        rect.offsetMin.x += delta.x;
        rect.offsetMax.y += delta.y;
        break;
    case Handle::None:
        return false;
    }

    // A resize may not turn the box inside out. Clamped rather than refused, so
    // dragging past the opposite edge parks the box at its minimum instead of
    // stopping dead -- which reads as the editor having frozen.
    //
    // Only meaningful when the anchors do not span: a stretched box's size also
    // depends on the parent, and clamping its offsets against each other would
    // fight a parent that is simply narrow.
    if (handle != Handle::Body) {
        const float minSize = float(std::max(1, minimumSize));
        if (rect.anchorMin.x == rect.anchorMax.x &&
            rect.offsetMax.x - rect.offsetMin.x < minSize) {
            if (handle == Handle::Left || handle == Handle::TopLeft ||
                handle == Handle::BottomLeft)
                rect.offsetMin.x = rect.offsetMax.x - minSize;
            else
                rect.offsetMax.x = rect.offsetMin.x + minSize;
        }
        if (rect.anchorMin.y == rect.anchorMax.y &&
            rect.offsetMax.y - rect.offsetMin.y < minSize) {
            if (handle == Handle::Top || handle == Handle::TopLeft ||
                handle == Handle::TopRight)
                rect.offsetMin.y = rect.offsetMax.y - minSize;
            else
                rect.offsetMax.y = rect.offsetMin.y + minSize;
        }
    }

    return rect.offsetMin != before[0] || rect.offsetMax != before[1];
}

} // namespace ed
