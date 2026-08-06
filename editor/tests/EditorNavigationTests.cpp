// Camera bookmarks, the browser-style history and the speed steps.
// Gregory §15.4.1.3.
//
// The history is the part worth asserting: a back stack that records ordinary
// mouse-look, or one that does not discard its forward entries on a new jump,
// behaves exactly like a browser that has lost your place.

#include <editor/viewport/CameraNavigation.h>

#include <iostream>
#include <string>

namespace nav = ed::nav;

static int gFailures = 0;

static void check(bool condition, const std::string& what)
{
    if (!condition) {
        std::cerr << "EditorNavigationTests: " << what << '\n';
        ++gFailures;
    }
}

static nav::Pose poseAt(float x)
{
    nav::Pose pose;
    pose.flyPosition = {x, 2.0f, 6.0f};
    pose.target = {x, 0.0f, 0.0f};
    pose.yaw = 0.5f;
    pose.pitch = -0.3f;
    pose.distance = 12.0f;
    return pose;
}

// A bookmark has to restore the exact viewpoint, not an orbit approximation of
// it: the whole reason to save one is that the framing was right.
static void testPoseRoundTrip()
{
    EditorCamera camera;
    camera.setFlyPosition({12.0f, 4.0f, -3.0f});
    camera.setYawPitch(1.1f, -0.4f);
    camera.frame({5.0f, 1.0f, 2.0f}, 20.0f);
    // frame() rewrites the fly position's orbit context, so set it last -- the
    // same order applyPose uses, and the reason it uses it.
    camera.setFlyPosition({12.0f, 4.0f, -3.0f});

    const nav::Pose saved = nav::poseOf(camera);

    EditorCamera other;
    nav::applyPose(other, saved);
    check(nav::nearlySame(nav::poseOf(other), saved),
          "a pose survives a save and a restore");
}

static void testBookmarks()
{
    nav::Bookmarks bookmarks;
    check(bookmarks.size() == nav::kBookmarkSlots, "ten slots");
    for (std::size_t slot = 0; slot < bookmarks.size(); ++slot)
        check(!bookmarks.has(slot), "every slot starts empty");

    bookmarks.set(3, poseAt(7.0f));
    check(bookmarks.has(3), "the slot is set");
    check(bookmarks.at(3).name == "view 4", "it got a default name");
    check(nav::nearlySame(bookmarks.at(3).pose, poseAt(7.0f)),
          "it holds the pose");

    bookmarks.set(3, poseAt(9.0f), "boss arena");
    check(bookmarks.at(3).name == "boss arena", "a given name is kept");
    // Re-recording from a better angle must not blank the label.
    bookmarks.set(3, poseAt(11.0f));
    check(bookmarks.at(3).name == "boss arena",
          "re-recording kept the name it had");

    bookmarks.clear(3);
    check(!bookmarks.has(3), "cleared");

    // Out of range is a no-op rather than a crash: the slot comes from a
    // keybind, and a keybind table is a thing people edit.
    bookmarks.set(99, poseAt(1.0f));
    check(!bookmarks.has(99), "an out-of-range slot is refused");
}

static void testHistoryIsABrowser()
{
    nav::History history;
    check(!history.canGoBack() && !history.canGoForward(), "starts empty");

    nav::Pose out;
    check(!history.back(poseAt(0.0f), out), "back with nowhere to go fails");

    // Jump A -> B -> C, recording where we left each time.
    history.push(poseAt(0.0f)); // was at A, now at B
    history.push(poseAt(1.0f)); // was at B, now at C
    check(history.canGoBack(), "there is somewhere to go back to");

    check(history.back(poseAt(2.0f), out), "back from C");
    check(nav::nearlySame(out, poseAt(1.0f)), "landed at B");
    check(history.canGoForward(), "and C is now forward");

    check(history.back(out, out), "back from B");
    check(nav::nearlySame(out, poseAt(0.0f)), "landed at A");
    check(!history.canGoBack(), "nothing further back");

    check(history.forward(out, out), "forward from A");
    check(nav::nearlySame(out, poseAt(1.0f)), "back at B");

    // Following a new jump discards the forward stack, exactly like a browser.
    history.push(poseAt(1.0f));
    check(!history.canGoForward(),
          "a new jump discarded the forward entries");
}

// Framing a selection that is already framed must not fill the stack with
// identical entries -- Back would then do nothing several times before it did
// something, which reads as broken.
static void testHistoryIgnoresStandingStill()
{
    nav::History history;
    history.push(poseAt(4.0f));
    history.push(poseAt(4.0f));
    history.push(poseAt(4.0f));

    nav::Pose out;
    check(history.back(poseAt(9.0f), out), "one entry went in");
    check(!history.canGoBack(), "and only one");
}

static void testHistoryIsBounded()
{
    nav::History history;
    for (int i = 0; i < int(nav::History::kMaxDepth) + 20; ++i)
        history.push(poseAt(float(i)));

    int steps = 0;
    nav::Pose out = poseAt(1000.0f);
    while (history.back(out, out) && steps < 1000)
        ++steps;
    check(steps == int(nav::History::kMaxDepth),
          "the stack is capped at its stated depth");
}

static void testSpeeds()
{
    check(nav::speedScale(nav::Speed::Fine) <
              nav::speedScale(nav::Speed::Normal),
          "fine is slower than normal");
    check(nav::speedScale(nav::Speed::Normal) <
              nav::speedScale(nav::Speed::Coarse),
          "coarse is faster than normal");
    check(nav::speedScale(nav::Speed::Normal) == 1.0f, "normal is the unit");
    check(std::string(nav::speedName(nav::Speed::Fine)) == "fine",
          "the names are stable");
}

// The elevations: entering one must frame where the author already was, and
// leaving must put the perspective camera back untouched.
static void testProjectionSwitchKeepsTheFraming()
{
    EditorCamera camera;
    camera.setFlyPosition({20.0f, 3.0f, -8.0f});
    camera.setYawPitch(0.9f, -0.2f);
    const glm::vec3 before = camera.flyEye();
    const float yawBefore = camera.yaw();

    camera.setProjection(EditorCamera::Projection::Top);
    check(camera.orthographic(), "the elevation is orthographic");
    check(camera.activeOrthoHeight() > 0.0f,
          "and reports a span for the renderer");
    check(std::abs(camera.orthoFocus().x - before.x) < 1e-3f &&
              std::abs(camera.orthoFocus().z - before.z) < 1e-3f,
          "it framed where the author already was");

    camera.zoomOrtho(0.5f);
    check(camera.orthoHeight() < 24.0f, "zooming in narrows the span");

    camera.setProjection(EditorCamera::Projection::Perspective);
    check(!camera.orthographic(), "back to perspective");
    check(camera.activeOrthoHeight() == 0.0f,
          "and the renderer is told to use the perspective path");
    check(camera.flyEye() == before && camera.yaw() == yawBefore,
          "the perspective framing was untouched throughout");
}

int main()
{
    testPoseRoundTrip();
    testBookmarks();
    testHistoryIsABrowser();
    testHistoryIgnoresStandingStill();
    testHistoryIsBounded();
    testSpeeds();
    testProjectionSwitchKeepsTheFraming();

    if (gFailures != 0) {
        std::cerr << "EditorNavigationTests: " << gFailures << " failure(s)\n";
        return 1;
    }
    std::cout << "EditorNavigationTests: ok\n";
    return 0;
}
