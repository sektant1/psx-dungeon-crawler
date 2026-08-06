#pragma once
#include <editor/viewport/EditorCamera.h>

#include <glm/glm.hpp>

#include <cstddef>
#include <string>
#include <vector>

namespace ed::nav {

// Navigation conveniences, Gregory §15.4.1.3: "the ability to save various
// relevant camera locations and then jump between them, various camera movement
// speed modes for coarse navigation and fine camera control, a Web-browser-like
// navigation history that can be used to jump around the game world".
//
// All three are session state -- where somebody likes to stand while working is
// not something the other people editing this chunk should inherit -- and all
// three are pure enough to check without a window.

// Everything needed to put the viewport back exactly where it was. Stored as
// the camera's own numbers rather than as a matrix: a matrix would have to be
// decomposed back into yaw and pitch, and that is lossy at the poles, which is
// precisely where a top-down bookmark sits.
struct Pose {
    glm::vec3 flyPosition{0.0f};
    float yaw = 0.0f;
    float pitch = 0.0f;
    glm::vec3 target{0.0f};
    float distance = 12.0f;
};

Pose poseOf(const EditorCamera& camera);
void applyPose(EditorCamera& camera, const Pose& pose);

// True when two poses are close enough to be the same place. What stops the
// history from recording a new entry every frame the camera drifts by a
// millimetre.
bool nearlySame(const Pose& a, const Pose& b);

// --- bookmarks --------------------------------------------------------------

struct Bookmark {
    std::string name;
    Pose pose;
};

// Ten numbered slots. A fixed set rather than a growable list because the point
// is a keybind -- Ctrl+1 to jump, Ctrl+Shift+1 to set -- and a bookmark you
// have to find in a menu is one you stop using.
inline constexpr std::size_t kBookmarkSlots = 10;

class Bookmarks
{
public:
    Bookmarks();

    void set(std::size_t slot, const Pose& pose, const std::string& name = {});
    void clear(std::size_t slot);
    bool has(std::size_t slot) const;
    const Bookmark& at(std::size_t slot) const;
    std::size_t size() const { return kBookmarkSlots; }

private:
    std::vector<Bookmark> mSlots;
    std::vector<bool> mSet;
};

// --- history ----------------------------------------------------------------

// A browser's back/forward stack over camera poses.
//
// Pushed on *jumps* only -- framing a selection, entering isolation, following
// a bookmark, revealing an issue -- never on ordinary flying. Recording every
// mouse-look frame would make Back mean "undo the last twitch", which is not
// what anybody presses it for.
class History
{
public:
    // Records a jump. `from` is where the camera was before it moved; the
    // forward stack is discarded, exactly like a browser following a link.
    void push(const Pose& from);

    // Steps back or forward. `current` is where the camera is now, so the
    // opposite stack can be given somewhere to return to. False when there is
    // nowhere to go, in which case `out` is untouched.
    bool back(const Pose& current, Pose& out);
    bool forward(const Pose& current, Pose& out);

    bool canGoBack() const { return !mBack.empty(); }
    bool canGoForward() const { return !mForward.empty(); }
    void clear();

    // Deep enough to get out of trouble, shallow enough that it is not a
    // second undo stack somebody expects to be complete.
    static constexpr std::size_t kMaxDepth = 32;

private:
    std::vector<Pose> mBack;
    std::vector<Pose> mForward;
};

// --- movement speed ---------------------------------------------------------

// Coarse and fine, plus the normal pace between them. Three named steps rather
// than a slider: the gesture is "hold Shift to cross the level, hold Alt to
// place something precisely", and a number nobody can reach mid-drag is not a
// speed control.
enum class Speed { Fine, Normal, Coarse };

// Metres per second the fly camera moves at, and the multiplier a scroll-wheel
// dolly uses. `base` is the editor's configured pace.
float speedScale(Speed speed);
const char* speedName(Speed speed);

} // namespace ed::nav
