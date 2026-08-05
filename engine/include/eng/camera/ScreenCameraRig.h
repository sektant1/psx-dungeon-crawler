#pragma once
#include <eng/camera/CameraRig.h>
#include <eng/ecs/components/ScreenCamera.h>

#include <glm/vec2.hpp>

namespace eng {

// The camera for a 2D screen scene: menus, HUD plates, dialogue, inventory
// pages. It stands square-on to the XY plane at exactly the distance where the
// authored page fills the view, so a scene laid out in virtual pixels renders
// pixel-for-pixel at any window size.
//
// It is a CameraRig like the other two on purpose. A screen is a *kind of
// scene*, and the thing that decides which kind you are in is which camera
// component the scene carries -- not a separate application, a separate loop or
// a flag threaded through the game. Which also means a screen can be previewed
// in the editor viewport, cooked into a .map and played by the same runner that
// plays a dungeon.
//
// It ignores the character pose entirely: a menu does not follow anybody. The
// pose still arrives, because the contract is the contract, and a game that
// pauses into a menu therefore does not have to stop feeding the camera.
class ScreenCameraRig final : public CameraRig {
public:
    void setPage(const ecs::ScreenCamera& page) { mTuning = page; }
    ecs::ScreenCamera& page() { return mTuning; }
    const ecs::ScreenCamera& page() const { return mTuning; }

    // The window's aspect (width / height). Supplied rather than queried: the
    // renderer derives it from the render target, which the rig cannot see, and
    // only Fit::Contain needs it at all.
    void setViewportAspect(float aspect) { mAspect = aspect > 0.0f ? aspect : mAspect; }
    float viewportAspect() const { return mAspect; }

    void attach(Renderer& renderer) override;
    void detach(Renderer& renderer) override;
    void forgetNodes() override
    {
        mPageRoot = {};
        mEye = {};
    }
    void present(Renderer& renderer, const CameraPose& pose, float dt) override;

    NodeHandle eyeNode() const override { return mEye; }
    // The page root: every authored screen entity belongs under this node, and
    // parenting to it is what puts a layout in page coordinates.
    NodeHandle characterNode() const override { return mPageRoot; }
    glm::vec3 eyePosition() const override { return mEyeWorld; }
    glm::vec3 forward() const override { return {0.0f, 0.0f, -1.0f}; }
    bool firstPerson() const override { return false; }
    // A screen has no neck to bend. Zero-width limits are how the controller
    // learns that: look input still arrives, and it cannot go anywhere.
    glm::vec2 pitchLimitsRadians() const override { return {0.0f, 0.0f}; }

    // How far back the camera has to stand for the page to fill the view.
    // Pure, and the whole model: everything else here is bookkeeping.
    static float fitDistance(const ecs::ScreenCamera& page, float aspect);

    // Virtual-pixel coordinates -> a position in the page's own space. `pixel`
    // is measured from the page origin the component names (top-left with y
    // down, or the centre with y up), so a layout written against a mock-up
    // does not have to be flipped by hand. `layer` is an integer depth, spaced
    // by ScreenCamera::layerSpacing.
    glm::vec3 pagePoint(glm::vec2 pixel, int layer = 0) const;

private:
    ecs::ScreenCamera mTuning;
    float mAspect = 16.0f / 9.0f;
    NodeHandle mPageRoot{};
    NodeHandle mEye{};
    glm::vec3 mEyeWorld{0.0f};
};

} // namespace eng
