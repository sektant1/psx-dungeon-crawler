#include <editor/scene/SelectionTools.h>

#include <algorithm>
#include <cmath>

namespace ed::selection {
namespace {

using game::content::AuthorId;

} // namespace

ScreenRect ScreenRect::fromCorners(glm::vec2 a, glm::vec2 b)
{
    ScreenRect rect;
    rect.min = {std::min(a.x, b.x), std::min(a.y, b.y)};
    rect.max = {std::max(a.x, b.x), std::max(a.y, b.y)};
    return rect;
}

bool ScreenRect::contains(glm::vec2 point) const
{
    return point.x >= min.x && point.x <= max.x && point.y >= min.y &&
           point.y <= max.y;
}

bool ScreenRect::degenerate(float minimumPixels) const
{
    return (max.x - min.x) < minimumPixels && (max.y - min.y) < minimumPixels;
}

std::vector<AuthorId> marquee(const std::vector<Candidate>& candidates,
                              const glm::mat4& viewProjection,
                              glm::vec2 viewportOrigin, glm::vec2 viewportSize,
                              const ScreenRect& rect, Fit fit)
{
    std::vector<AuthorId> hits;
    for (const Candidate& candidate : candidates) {
        // The eight corners, projected. A world AABB does not stay axis-aligned
        // on screen, so projecting only min and max would miss most of it.
        bool any = false;
        glm::vec2 low{0.0f};
        glm::vec2 high{0.0f};
        for (int corner = 0; corner < 8; ++corner) {
            const glm::vec3 world = {
                (corner & 1) ? candidate.boundsMax.x : candidate.boundsMin.x,
                (corner & 2) ? candidate.boundsMax.y : candidate.boundsMin.y,
                (corner & 4) ? candidate.boundsMax.z : candidate.boundsMin.z};
            glm::vec2 screen{0.0f};
            if (!projectToViewport(world, viewProjection, viewportOrigin,
                                   viewportSize, screen))
                continue; // behind the camera
            if (!any) {
                low = screen;
                high = screen;
                any = true;
            } else {
                low = {std::min(low.x, screen.x), std::min(low.y, screen.y)};
                high = {std::max(high.x, screen.x), std::max(high.y, screen.y)};
            }
        }
        if (!any)
            continue;

        // Enclose demands the whole footprint; Touch takes any overlap. An
        // entity partly behind the camera has an under-reported footprint,
        // which makes Enclose conservative there -- the safe direction, since
        // Enclose exists to avoid catching things.
        const bool inside = fit == Fit::Enclose
                                ? rect.contains(low) && rect.contains(high)
                                : !(high.x < rect.min.x || low.x > rect.max.x ||
                                    high.y < rect.min.y || low.y > rect.max.y);
        if (inside)
            hits.push_back(candidate.id);
    }
    return hits;
}

AuthorId PickCycle::next(const std::vector<AuthorId>& hits, glm::vec2 point,
                         float radius)
{
    if (hits.empty()) {
        reset();
        return {};
    }

    // Same spot AND same stack. Either changing means the author is picking
    // something new: the camera may have moved under a stationary cursor, and
    // continuing to count into a different list would select an unrelated
    // entity.
    const bool sameSpot =
        mHasPoint && std::abs(point.x - mPoint.x) <= radius &&
        std::abs(point.y - mPoint.y) <= radius;
    const bool sameStack = hits == mHits;

    if (sameSpot && sameStack)
        mDepth = (mDepth + 1) % hits.size();
    else
        mDepth = 0;

    mHits = hits;
    mPoint = point;
    mHasPoint = true;
    return hits[mDepth];
}

void PickCycle::reset()
{
    mHits.clear();
    mDepth = 0;
    mHasPoint = false;
}

void save(std::vector<SelectionSet>& sets, const std::string& name,
          const std::vector<AuthorId>& members)
{
    if (name.empty() || members.empty())
        return;
    for (SelectionSet& set : sets) {
        if (set.name != name)
            continue;
        set.members = members;
        return;
    }
    sets.push_back(SelectionSet{name, members});
}

std::vector<AuthorId> restore(const std::vector<SelectionSet>& sets,
                              const std::string& name,
                              const game::content::SceneDocument& document)
{
    std::vector<AuthorId> members;
    for (const SelectionSet& set : sets) {
        if (set.name != name)
            continue;
        for (const AuthorId& id : set.members)
            if (document.contains(id))
                members.push_back(id);
        break;
    }
    return members;
}

void remove(std::vector<SelectionSet>& sets, const std::string& name)
{
    for (std::size_t i = 0; i < sets.size(); ++i) {
        if (sets[i].name != name)
            continue;
        sets.erase(sets.begin() + std::ptrdiff_t(i));
        return;
    }
}

} // namespace ed::selection
