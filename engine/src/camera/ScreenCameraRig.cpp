#include <eng/camera/ScreenCameraRig.h>

#include <eng/Renderer.h>

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>

namespace eng {

float ScreenCameraRig::fitDistance(const ecs::ScreenCamera& page, float aspect)
{
    const float fov = glm::radians(std::clamp(page.fovDegrees, 1.0f, 179.0f));
    const float halfTan = std::tan(fov * 0.5f);
    if (halfTan <= 1e-4f)
        return 1.0f;
    // The vertical field is the one the projection is built from, so fitting
    // the page's height is exact at any window size and needs no aspect at all.
    const float byHeight = (std::max(page.pageHeight, 1.0f) * 0.5f) / halfTan;
    if (page.fit != ecs::ScreenCamera::Contain)
        return byHeight;
    // Contain: the horizontal half-angle is the vertical one times the aspect,
    // so a narrow window needs the camera further back. Taking the larger of
    // the two is what guarantees nothing authored is ever cropped.
    const float byWidth = (std::max(page.pageWidth, 1.0f) * 0.5f) /
                          (halfTan * std::max(aspect, 0.01f));
    return std::max(byHeight, byWidth);
}

glm::vec3 ScreenCameraRig::pagePoint(glm::vec2 pixel, int layer) const
{
    const float z = -float(layer) * mTuning.layerSpacing;
    if (mTuning.origin == ecs::ScreenCamera::TopLeft) {
        // Screen convention: y grows downward from the page's top-left corner.
        // Converted once, here, so a layout copied off a mock-up is typed in
        // the coordinates the mock-up used.
        return {pixel.x - mTuning.pageWidth * 0.5f,
                mTuning.pageHeight * 0.5f - pixel.y, z};
    }
    return {pixel.x, pixel.y, z};
}

void ScreenCameraRig::attach(Renderer& r)
{
    mPageRoot = r.createNode(kRootNode);
    // The eye is a sibling of the page, not a child: a transition that slides
    // or scales the page must not take the camera with it.
    mEye = r.createNode(kRootNode);
    r.attachCamera(mEye);
}

void ScreenCameraRig::detach(Renderer& r)
{
    if (mPageRoot.valid())
        r.destroyNode(mPageRoot);
    if (mEye.valid())
        r.destroyNode(mEye);
    mPageRoot = {};
    mEye = {};
}

void ScreenCameraRig::present(Renderer& r, const CameraPose& pose, float dt)
{
    (void)pose; // a screen does not follow anybody
    (void)dt;   // and nothing here eases

    const float distance = fitDistance(mTuning, mAspect);
    mEyeWorld = glm::vec3(0.0f, 0.0f, distance);
    r.setPosition(mEye, mEyeWorld + mShake.offset);
    // Square on to the page, and no rotation of its own: the identity
    // orientation already looks down -Z, which is the page's normal.
    const glm::vec3 shake = glm::radians(mShake.rotationDegrees);
    r.setOrientation(mEye, glm::angleAxis(shake.x, glm::vec3(1, 0, 0)) *
                               glm::angleAxis(shake.y, glm::vec3(0, 1, 0)) *
                               glm::angleAxis(shake.z, glm::vec3(0, 0, 1)));

    // The lens is part of the fit -- the distance above was derived from this
    // exact FOV, and letting anything else set it would silently rescale the
    // page.
    if (std::abs(r.envState().fovDeg - mTuning.fovDegrees) > 0.001f)
        r.setCameraFov(mTuning.fovDegrees);
}

} // namespace eng
