#include <editor/viewport/CameraNavigation.h>

#include <cmath>

namespace ed::nav {
namespace {

// A centimetre and a tenth of a degree. Below this the camera has not gone
// anywhere an author would call somewhere else.
constexpr float kPositionEpsilon = 0.01f;
constexpr float kAngleEpsilon = 0.002f; // radians, about 0.1 degrees

bool sameVec(const glm::vec3& a, const glm::vec3& b)
{
    return std::abs(a.x - b.x) < kPositionEpsilon &&
           std::abs(a.y - b.y) < kPositionEpsilon &&
           std::abs(a.z - b.z) < kPositionEpsilon;
}

} // namespace

Pose poseOf(const EditorCamera& camera)
{
    Pose pose;
    pose.flyPosition = camera.flyEye();
    pose.yaw = camera.yaw();
    pose.pitch = camera.pitch();
    pose.target = camera.target();
    pose.distance = camera.distance();
    return pose;
}

void applyPose(EditorCamera& camera, const Pose& pose)
{
    // Orbit state first, then the fly position: frame() rewrites the target and
    // distance, and setting the fly eye afterwards is what makes a bookmark
    // restore the exact viewpoint rather than an orbit approximation of it.
    camera.frame(pose.target, pose.distance);
    camera.setYawPitch(pose.yaw, pose.pitch);
    camera.setFlyPosition(pose.flyPosition);
}

bool nearlySame(const Pose& a, const Pose& b)
{
    return sameVec(a.flyPosition, b.flyPosition) &&
           std::abs(a.yaw - b.yaw) < kAngleEpsilon &&
           std::abs(a.pitch - b.pitch) < kAngleEpsilon &&
           sameVec(a.target, b.target) &&
           std::abs(a.distance - b.distance) < kPositionEpsilon;
}

Bookmarks::Bookmarks()
    : mSlots(kBookmarkSlots), mSet(kBookmarkSlots, false)
{
}

void Bookmarks::set(std::size_t slot, const Pose& pose, const std::string& name)
{
    if (slot >= kBookmarkSlots)
        return;
    mSlots[slot].pose = pose;
    // A slot keeps whatever name it had unless a new one is given, so
    // re-recording "boss arena" from a better angle does not blank its label.
    if (!name.empty() || mSlots[slot].name.empty())
        mSlots[slot].name = name.empty() ? "view " + std::to_string(slot + 1)
                                         : name;
    mSet[slot] = true;
}

void Bookmarks::clear(std::size_t slot)
{
    if (slot >= kBookmarkSlots)
        return;
    mSlots[slot] = Bookmark{};
    mSet[slot] = false;
}

bool Bookmarks::has(std::size_t slot) const
{
    return slot < kBookmarkSlots && mSet[slot];
}

const Bookmark& Bookmarks::at(std::size_t slot) const
{
    static const Bookmark kEmpty;
    return slot < kBookmarkSlots ? mSlots[slot] : kEmpty;
}

void History::push(const Pose& from)
{
    // Standing still is not a jump. Without this, framing a selection that is
    // already framed would fill the stack with identical entries and make Back
    // do nothing several times before it did something.
    if (!mBack.empty() && nearlySame(mBack.back(), from))
        return;
    mBack.push_back(from);
    if (mBack.size() > kMaxDepth)
        mBack.erase(mBack.begin());
    mForward.clear(); // following a link discards the forward stack
}

bool History::back(const Pose& current, Pose& out)
{
    if (mBack.empty())
        return false;
    mForward.push_back(current);
    out = mBack.back();
    mBack.pop_back();
    return true;
}

bool History::forward(const Pose& current, Pose& out)
{
    if (mForward.empty())
        return false;
    mBack.push_back(current);
    out = mForward.back();
    mForward.pop_back();
    return true;
}

void History::clear()
{
    mBack.clear();
    mForward.clear();
}

float speedScale(Speed speed)
{
    switch (speed) {
    case Speed::Fine:   return 0.25f;
    case Speed::Coarse: return 4.0f;
    case Speed::Normal: break;
    }
    return 1.0f;
}

const char* speedName(Speed speed)
{
    switch (speed) {
    case Speed::Fine:   return "fine";
    case Speed::Coarse: return "fast";
    case Speed::Normal: break;
    }
    return "normal";
}

} // namespace ed::nav
