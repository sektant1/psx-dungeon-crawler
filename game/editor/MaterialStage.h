#pragma once
#include <eng/Handles.h>

#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace eng {
class Renderer;
}

namespace ed {

// The material staging scene: one sphere on an infinite checkerboard under
// three-point lighting, for judging a material on its own before it goes into a
// level.
//
// One deliberate departure from the usual "material preview" recipe: there is
// NO PBR and NO HDRI. The preview lights the sphere with the game's own PSX
// shaders -- vertex lighting, banded falloff, dither, affine warp. A physically
// based preview would look better and would be a lie: it would show a material
// in a lighting model nothing in the game uses, so a material tuned to look
// right here would look wrong in the dungeon. The preview's job is to predict
// the game, not to flatter the asset.
//
// The rig is transient and lives outside the scene document -- none of it can
// be selected, saved, or cooked into a level.
class MaterialStage
{
public:
    void build(eng::Renderer& renderer);
    // Studio backdrop: dark and neutral, so the rim light reads against it and
    // nothing in frame competes with the material for attention. Applied on
    // entering the mode and restored on leaving, because these are global
    // renderer settings the level also uses.
    void applyEnvironment(eng::Renderer& renderer) const;
    void destroy(eng::Renderer& renderer);
    bool built() const { return mSphere.valid(); }

    // The floor and the lights deliberately keep their own materials: the point
    // of a reference stage is that only the thing under test changes.
    void setMaterial(eng::Renderer& renderer, const std::string& material);
    const std::string& material() const { return mMaterial; }

    void setVisible(eng::Renderer& renderer, bool visible);
    void setFloorMaterial(eng::Renderer& renderer, const std::string& material);
    // Turntable: rotating the sphere is how you see a material's angular
    // behaviour without flying the camera around it.
    void setSpin(eng::Renderer& renderer, float radians);
    float spin() const { return mSpin; }

    // The only node the gizmo is allowed to touch. Floor and lights stay put --
    // a preview whose reference frame can be dragged out of alignment stops
    // being a reference.
    eng::NodeHandle gizmoTarget() const { return mSphere; }

    // --- thumbnail swatch --------------------------------------------------
    // A second, private sphere rendered into its own small square target: the
    // material swatch beside the list, the way every engine editor shows one.
    //
    // It is a separate object from the staging sphere on purpose. The swatch has
    // to render whatever the cursor is hovering in the list *without* disturbing
    // what the big viewport is showing, so previewing a material cannot lose the
    // one you were actually working on.
    void buildThumbnail(eng::Renderer& renderer, int size);
    void setThumbnailMaterial(eng::Renderer& renderer, const std::string& material);
    const std::string& thumbnailMaterial() const { return mThumbMaterial; }
    void spinThumbnail(eng::Renderer& renderer, float radians);
    float thumbnailSpin() const { return mThumbSpin; }
    bool thumbnailBuilt() const { return mThumbSphere.valid(); }

    // Where a camera should sit to frame the subject. A studio three-quarter
    // view, not the steep top-down a dungeon wants: a material is judged from
    // roughly eye level, which is where the player will actually see it.
    glm::vec3 focusPoint() const { return {0.0f, 1.0f, 0.0f}; }
    glm::vec3 cameraPosition() const { return {2.2f, 1.7f, 3.4f}; }
    float cameraPitch() const { return -0.18f; }
    float cameraYaw() const { return 0.58f; }

private:
    eng::NodeHandle mSphere;
    eng::NodeHandle mFloor;
    std::vector<eng::NodeHandle> mLightNodes;
    std::vector<eng::LightHandle> mLights;
    std::string mMaterial = "Kit/Dungeon";
    std::string mFloorMaterial = "Editor/Checkerboard";
    float mSpin = 0.0f;

    eng::NodeHandle mThumbSphere;
    std::vector<eng::NodeHandle> mThumbLightNodes;
    std::string mThumbMaterial;
    float mThumbSpin = 0.0f;
};

} // namespace ed
