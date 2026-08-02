#pragma once
#include <eng/Handles.h>

#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace eng {

class Renderer;

// Moved out of the editor so the game's debug UI and the psx demo can stage a
// material too: the rig is a renderer concern, not an editor one.

// How a material is staged for judging. Two rigs, because the two kinds of
// material in this project are judged on completely different things.
enum class StagePreview {
    // A surface: judged on how it takes light. Sphere, checkerboard, three-point
    // rig. This is the default and the historical behaviour.
    Sphere,
    // A shader that animates: judged on its own motion, the way you would judge
    // an inventory icon. Front-parallel unlit quad, flat backdrop, no lights, no
    // shadow, and a time uniform running so the animation actually plays.
    Quad,
};

// Which materials get which rig. There is no material catalog in this project --
// Renderer::materialNames() enumerates Ogre's material manager directly -- so the
// metadata cannot hang off a catalog entry and lives in its own tiny file,
// assets/engine/material_preview.toml. Anything unlisted is a sphere, which is
// why adding this file cannot change how any of the ~95 existing materials look.
class StagePreviewCatalog
{
public:
    // Reads the TOML. Missing or unreadable file is not an error: every material
    // then stages as a sphere, which is exactly the pre-existing behaviour.
    void load(const std::string& path);
    StagePreview modeFor(const std::string& material) const;

    // Exposed for testing and for the loader; a pattern is either an exact
    // material name or a name ending in '*' matching a prefix.
    static bool matches(const std::string& pattern, const std::string& name);

private:
    std::vector<std::string> mQuadPatterns;
};

// The material staging scene. In the default sphere mode: one sphere on an
// infinite checkerboard under three-point lighting, for judging a material on
// its own before it goes into a level.
//
// One deliberate departure from the usual "material preview" recipe: there is
// NO PBR and NO HDRI. The preview lights the sphere with the game's own PSX
// shaders -- vertex lighting, banded falloff, dither, affine warp. A physically
// based preview would look better and would be a lie: it would show a material
// in a lighting model nothing in the game uses, so a material tuned to look
// right here would look wrong in the dungeon. The preview's job is to predict
// the game, not to flatter the asset.
//
// The quad mode departs from that reasoning on purpose, and the departure is
// consistent with it. A procedural or flipbook shader -- a particle sprite, an
// icon -- has no lighting response worth predicting; it is authored as emissive
// pixels and it is judged on its animation and its silhouette. Staging it on a
// lit sphere hides both: the turntable smears the pattern across curvature and
// the key light recolours it. So the quad rig removes the lighting entirely
// (ambient white, no lights) rather than substituting a different, equally
// wrong, lighting model. Both modes obey the same rule -- show the material the
// way the game will show it.
//
// The rig is transient and lives outside the scene document -- none of it can
// be selected, saved, or cooked into a level.
class MaterialPreview
{
public:
    void build(Renderer& renderer);
    // Studio backdrop: dark and neutral, so the rim light reads against it and
    // nothing in frame competes with the material for attention. Applied on
    // entering the mode and restored on leaving, because these are global
    // renderer settings the level also uses. In quad mode this instead sets a
    // flat neutral backdrop and full ambient, which is what "unlit" means to
    // the PSX shaders.
    void applyEnvironment(Renderer& renderer) const;
    void destroy(Renderer& renderer);
    bool built() const { return mSubject.valid(); }

    // The floor and the lights deliberately keep their own materials: the point
    // of a reference stage is that only the thing under test changes.
    //
    // Setting a material whose catalog mode differs from the live rig tears the
    // whole rig down and rebuilds the other one, then reapplies the environment.
    // Switching is a full teardown rather than a hide, because a half-torn rig
    // is how this stage has broken before: leftover point lights keep lighting
    // whatever level is loaded, and a re-attached mesh silently drops its
    // visibility flags.
    void setMaterial(Renderer& renderer, const std::string& material);
    const std::string& material() const { return mMaterial; }
    StagePreview previewMode() const { return mMode; }

    void setVisible(Renderer& renderer, bool visible);
    void setFloorMaterial(Renderer& renderer, const std::string& material);
    // Turntable: rotating the sphere is how you see a material's angular
    // behaviour without flying the camera around it. Ignored in quad mode --
    // a flat icon turned edge-on shows nothing, so the quad stays facing the
    // camera no matter what the auto-spin or the gizmo asks for.
    void setSpin(Renderer& renderer, float radians);
    float spin() const { return mSpin; }

    // The only node the gizmo is allowed to touch. Floor and lights stay put --
    // a preview whose reference frame can be dragged out of alignment stops
    // being a reference.
    NodeHandle gizmoTarget() const { return mSubject; }

    // --- thumbnail swatch --------------------------------------------------
    // A second, private subject rendered into its own small square target: the
    // material swatch beside the list, the way every engine editor shows one.
    //
    // It is a separate object from the staging subject on purpose. The swatch has
    // to render whatever the cursor is hovering in the list *without* disturbing
    // what the big viewport is showing, so previewing a material cannot lose the
    // one you were actually working on.
    //
    // It follows the same catalog mode as the big stage, so hovering an animated
    // shader shows the animated square rather than a confusing lit sphere.
    void buildThumbnail(Renderer& renderer, int size);
    void setThumbnailMaterial(Renderer& renderer, const std::string& material);
    const std::string& thumbnailMaterial() const { return mThumbMaterial; }
    StagePreview thumbnailPreviewMode() const { return mThumbMode; }
    // The thumbnail target is shared by editor asset tabs. Particles hide the
    // material subject and draw a thumbnail-only emitter into the same square.
    void setThumbnailVisible(Renderer& renderer, bool visible);
    void spinThumbnail(Renderer& renderer, float radians);
    float thumbnailSpin() const { return mThumbSpin; }
    bool thumbnailBuilt() const { return mThumbSubject.valid(); }

    // Where a camera should sit to frame the subject. A studio three-quarter
    // view, not the steep top-down a dungeon wants: a material is judged from
    // roughly eye level, which is where the player will actually see it.
    //
    // These are deliberately identical in both modes. The editor samples them
    // only when entering the stage or when the user resets the view, never on a
    // material change, so a mode-dependent camera would leave the quad framed by
    // whichever pose happened to be current. Instead the quad orients itself to
    // face this fixed pose. A plane parallel to the image plane projects with a
    // uniform scale, so that front-parallel view is pixel-for-pixel what an
    // orthographic camera would give -- without an ortho path in the renderer.
    glm::vec3 focusPoint() const { return {0.0f, 1.0f, 0.0f}; }
    glm::vec3 cameraPosition() const { return {2.2f, 1.7f, 3.4f}; }
    float cameraPitch() const { return -0.18f; }
    float cameraYaw() const { return 0.58f; }

private:
    // Substitutes a drawable stand-in for a material that needs a per-instance
    // vertex stream; returns the material itself otherwise.
    static std::string quadMaterial(const std::string& material);


    void buildSphereRig(Renderer& renderer);
    void buildQuadRig(Renderer& renderer);
    void destroyStage(Renderer& renderer);   // big viewport rig only
    void buildThumbnailRig(Renderer& renderer);
    void destroyThumbnailRig(Renderer& renderer);

    StagePreviewCatalog mCatalog;
    bool mCatalogLoaded = false;

    StagePreview mMode = StagePreview::Sphere;
    NodeHandle mSubject;
    NodeHandle mFloor;
    std::vector<NodeHandle> mLightNodes;
    std::vector<LightHandle> mLights;
    std::string mMaterial = "Game/Kit/Dungeon";
    std::string mFloorMaterial = "Editor/Checkerboard";
    float mSpin = 0.0f;
    bool mVisible = true;

    StagePreview mThumbMode = StagePreview::Sphere;
    int mThumbSize = 256;
    NodeHandle mThumbSubject;
    std::vector<NodeHandle> mThumbLightNodes;
    std::string mThumbMaterial;
    float mThumbSpin = 0.0f;
};

} // namespace eng
