#pragma once
#include <eng/Handles.h>
#include <eng/LightDesc.h>
#include <eng/ShaderBlock.h>
#include <eng/ShaderUniforms.h>
#include <eng/particles/DecalSystem.h>
#include <eng/particles/ParticleCollider.h>
#include <eng/particles/ParticleEffectDesc.h>
#include <eng/render/Enchantment.h>
#include <eng/render/ModelImport.h>
#include <eng/render/PrototypeAssets.h>
#include <eng/Sprite.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <memory>
#include <span>
#include <string>
#include <vector>

namespace eng {

class RenderCore; // internal; forward-declared only, no Ogre leak
class Renderer;
class SceneView; // read-only scene-graph facade, defined in Renderer.cpp
struct PrimitiveMeshDesc;

namespace detail {
// Engine-only backdoor to the internal core (defined in Renderer.cpp).
RenderCore& coreOf(Renderer& r);
void registerRoot(Renderer& r);
} // namespace detail

// Last-set environment/camera values, cached so the debug UI can display
// and edit them. Ambient/fog colours are linear; background is raw sRGB
// (matches the setter conventions).
struct EnvState {
    glm::vec3 ambient{0.0f};
    glm::vec3 fogColour{0.0f};
    float fogDensity = 0.0f;
    glm::vec3 background{0.0f};
    float fovDeg = 70.0f;    // RenderCore init defaults
    float nearClip = 0.05f;
    float farClip = 4000.0f;
    bool dither = false;
    int pixelSize = 3;       // PSX/Stylized RT = window / pixelSize
    bool perPixelLighting = true; // fragment vs vertex light evaluation
    // The two GTE artefacts. 512x448 is the grid the snap is expressed
    // against (psx.vert's base_snap_res), so 1.0 is finer than any render
    // target here and reads as "off"; the PS1 profile runs 0.156.
    float precisionMultiplier = 1.0f;
    // 0 = perspective-correct UVs, 1 = the full screen-space swim.
    float affineAmount = 0.0f;
    // Saturation point of the affine/perspective divergence, in UV units.
    // Keeps the warp from tearing on the big near-camera polygons a modern kit
    // draws floors and ceilings out of.
    float affineSoftness = 0.10f;
    float omniAttenuation = 1.0f; // Godot omni falloff exponent (1 = linear)
    float lightSteps = 0.0f; // diffuse posterization bands, 0 = smooth
    float lightStepSoftness = 0.35f; // band seam half-width, 0 = hard edges
    float fogDesatBoost = 0.0f; // distance desat/darken before fog mix
    bool bloom = true;
    float bloomThreshold = 0.85f; // torch-only: flames/embers glow, walls don't
    float bloomIntensity = 0.6f;
    bool wireframe = false;  // debug view: models as light-blue mesh lines
    bool grade = false;             // colour grade in the dither pass
    float gradeDesaturate = 0.15f;
    float gradeContrast = 1.05f;
    glm::vec3 gradeShadowTint{0.82f, 1.0f, 0.86f};
    glm::vec3 gradeMidTint{1.0f, 0.96f, 0.88f};
};

struct NodeTransform {
    glm::vec3 position{0.0f};
    glm::quat orientation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f};
};

// Public renderer facade. All Ogre types stay inside engine/src.
// Colour convention: shading runs in linear space; callers linearise
// sRGB-picked colours themselves (pow 2.2), as the PSX shaders expect.
class Renderer
{
public:
    // --- meshes -----------------------------------------------------------
    // Format-neutral static-model import. Supported source formats are
    // discovered from the pinned Assimp build; skeletal/deforming data is
    // rejected by this API rather than silently discarded.
    // Default world/static standard: metres, +Y up, -Z forward, X/Z centred,
    // bottom on Y=0. Use explicit options or bake overload for exceptions.
    MeshHandle loadMesh(const std::string& path);
    MeshHandle loadMesh(const std::string& path, const glm::mat4* bake);
    MeshHandle loadMesh(const std::string& path,
                        const ModelImportOptions& options);
    static std::vector<std::string> supportedModelExtensions();
    static bool supportsModelFile(const std::string& path);
    // bake, when given, is multiplied into vertex positions (normals get
    // its inverse-transpose) -- for transforms TRS nodes can't represent.
    // Compatibility spelling; new code should use loadMesh().
    MeshHandle loadObj(const std::string& path, const glm::mat4* bake = nullptr);
    MeshHandle loadObj(const std::string& path,
                       const ModelImportOptions& options);
    // Sole generic primitive entry point. The descriptor's dimensions are
    // baked into the mesh; node scale remains available for placement.
    MeshHandle createPrimitiveMesh(const PrimitiveMeshDesc&);
    // Stand-in for a mesh that failed to load, mirroring the
    // Engine/Psx/PrototypeSurface material fallback: a missing asset should read as
    // an obviously untextured placeholder, not abort the frame. The primitive is
    // chosen from the asset's filename (prototype::meshShapeFor) so a missing
    // wall is wall-shaped; each distinct shape is built once and shared. Pass an
    // empty path for a plain unit box.
    MeshHandle prototypeMesh(const std::string& assetPath = {});
    // Rules for what a missing mesh or material is replaced with. Empty by
    // default: with no rules every miss is a unit box in the checkered
    // prototype material, which is correct but unreadable in a dressed scene.
    // See eng::prototype::PrototypeCatalog for why the application owns these.
    void setPrototypeCatalog(prototype::PrototypeCatalog catalog);
    bool meshBounds(MeshHandle mesh, MeshBounds& out) const;
    size_t meshSubmeshCount(MeshHandle mesh) const;
    bool meshImportReport(MeshHandle mesh, ModelImportReport& out) const;
    // Triangle geometry captured during render-mesh load, never reparsed or
    // read back from Ogre. Returns false for meshes without cached triangles.
    bool meshCollisionGeometry(MeshHandle mesh,
                               std::vector<glm::vec3>& vertices,
                               std::vector<uint32_t>& indices) const;
    // Releases one Renderer-created mesh and its CPU collision cache. Safe for
    // invalid/already-released handles; callers must first destroy attachments
    // that use this uniquely owned mesh.
    bool releaseMesh(MeshHandle mesh);

    // --- skinned meshes ---------------------------------------------------
    // Source geometry remains engine-owned while skeleton/clip sampling is
    // handled by eng::animation. Joint names map mesh bones to stable ozz
    // skeleton order; deforming geometry never enters static collision/batches.
    SkinnedMeshHandle
    loadSkinnedMesh(const std::string& path,
                    const std::vector<std::string>& skeletonJointNames);
    SkinInstanceHandle attachSkinnedMesh(
        NodeHandle node, SkinnedMeshHandle mesh,
        const std::string& materialName, bool castShadows = false,
        bool renderOnTop = false);
    // Model-space matrices in same skeleton order supplied at import. Renderer
    // combines them with each submesh's inverse bind poses and uploads palette.
    bool setSkinningPose(SkinInstanceHandle instance,
                         std::span<const glm::mat4> modelMatrices);
    bool releaseSkinnedMesh(SkinnedMeshHandle mesh);

    // --- scene graph ------------------------------------------------------
    NodeHandle createNode(NodeHandle parent, glm::vec3 position = glm::vec3(0.0f),
                          const std::string& name = {});
    void setPosition(NodeHandle node, glm::vec3 position);
    void setOrientation(NodeHandle node, glm::quat orientation);
    void setScale(NodeHandle node, glm::vec3 scale);
    // Derived world transform using the same parent orientation/scale
    // inheritance as Ogre. Returns false for invalid or destroyed handles.
    bool nodeWorldTransform(NodeHandle node, NodeTransform& out) const;
    // Live-swap the material on every mesh attached to a node (editor tweaks).
    void setNodeMaterial(NodeHandle node, const std::string& materialName);
    // Adds/removes a scrolling Minecraft-like enchantment pass while
    // preserving each mesh's underlying material.
    void setNodeEnchantment(NodeHandle node, const EnchantmentDesc& desc);
    void setNodeEnchantment(NodeHandle node, const EnchantmentPalette& palette,
                            float strength = 1.0f);
    void clearNodeEnchantment(NodeHandle node);
    // All user-facing material names currently loaded (Ogre parsed every
    // .material at init), sorted, with engine/Ogre internals filtered out. For
    // editor material pickers -- discovered, never hard-coded.
    std::vector<std::string> materialNames() const;
    bool materialAvailable(const std::string& materialName) const;
    // World-space bounds of everything attached under a node (recursive), for
    // editor auto-framing. Returns false if the node has no renderable bounds.
    bool nodeWorldBounds(NodeHandle node, glm::vec3& center, float& radius) const;
    // Show/hide a node and everything attached beneath it (meshes,
    // particles, lights).
    void setNodeVisible(NodeHandle node, bool show);
    // Permanently destroy a node and the lights/entities attached to it, so
    // transient spawns (projectiles, one-shot VFX) don't leak Ogre objects.
    // Pool-owned particle systems are only detached (they recycle themselves).
    void destroyNode(NodeHandle node);

    // --- attachments ------------------------------------------------------
    // castShadows opts the entity into stencil shadow casting; keep it off
    // for open/sliced geometry (walls, floors) -- shadow volumes need
    // closed-ish meshes to extrude cleanly.
    void attachMesh(NodeHandle node, MeshHandle mesh,
                    const std::string& materialName, bool castShadows = false,
                    bool renderOnTop = false);
    void attachMesh(NodeHandle node, MeshHandle mesh,
                    const std::string& materialName,
                    const std::string& fallbackMaterial,
                    bool castShadows = false, bool renderOnTop = false);
    // The same call with a string-literal fallback, and it exists because
    // without it that call silently means something else.
    //
    // `attachMesh(node, mesh, material, "Game/Prototype/Floor", castShadows)`
    // has a `const char*` in the fourth slot. Converting that to `bool` is a
    // standard conversion and converting it to `std::string` is a user-defined
    // one, so overload resolution prefers the overload ABOVE this pair: the
    // literal becomes `castShadows = true` and the caller's castShadows slides
    // into `renderOnTop`. Every enemy in the game was attached that way, and
    // because enemies.toml sets cast_shadows = true they all rendered in the
    // viewmodel pass -- through walls, over particles, over the hands.
    //
    // An exact match for `const char*` outranks both, so this overload takes
    // the call and forwards it where it was always meant to go.
    void attachMesh(NodeHandle node, MeshHandle mesh,
                    const std::string& materialName,
                    const char* fallbackMaterial, bool castShadows = false,
                    bool renderOnTop = false);
    void attachMesh(NodeHandle node, MeshHandle mesh,
                    const ResolvedModelMaterial& material,
                    bool castShadows = false, bool renderOnTop = false);
    void attachMesh(NodeHandle node, MeshHandle mesh,
                    const std::vector<ResolvedModelMaterial>& materials,
                    bool castShadows = false, bool renderOnTop = false);

    // Sprite seam: createSpriteMaterial applies a clip to arbitrary mesh UVs;
    // attachSprite uses the same clip as a camera-facing world billboard.
    std::string createSpriteMaterial(const SpriteClip& clip);
    SpriteHandle attachSprite(NodeHandle node, const SpriteClip& clip);
    SpriteHandle attachTextSprite(NodeHandle node, const std::string& text,
                                  const TextSpriteStyle& style = {});
    void setSpriteVisible(SpriteHandle sprite, bool visible);

    // Static world geometry, baked into region-batched buffers (one draw
    // per material per region, whole regions frustum-culled). add* records
    // are retained so the batch can be rebuilt -- the wireframe debug view
    // rebuilds every batch with the wire material and back.
    StaticBatchHandle createStaticBatch(glm::vec3 regionSize);
    void addToStaticBatch(StaticBatchHandle batch, MeshHandle mesh,
                          const std::string& materialName, glm::vec3 pos,
                          float yawDeg = 0.0f);
    void buildStaticBatch(StaticBatchHandle batch); // bake the records
    // Show/hide a whole static batch (room-level occlusion culling).
    void setStaticBatchVisible(StaticBatchHandle batch, bool visible);
    // Destroy all scene content (nodes, meshes, lights, particles, static
    // batches, entities) except the camera, resetting handle allocation so
    // fresh handles start over. Used for level transitions.
    void clearScene();
    // --- particles (data-driven, pooled) ----------------------------------
    ParticleEffectId registerParticleEffect(const ParticleEffectDesc& desc);
    ParticleEffectId particleEffectId(const std::string& name); // by desc.name
    ParticlesHandle  spawnParticles(ParticleEffectId fx, NodeHandle parent,
                                    glm::vec3 localPos = glm::vec3(0.0f));
    ParticlesHandle  spawnParticles(ParticleEffectId fx, NodeHandle parent,
                                    const ParticleSpawnOptions& options);
    ParticlesHandle  spawnParticles(ParticleEffectId fx, NodeHandle parent,
                                    glm::vec3 localPos,
                                    const ParticleSpawnOptions& options);
    ParticlesHandle  spawnParticles(ParticleEffectId fx, glm::vec3 worldPos);
    ParticlesHandle  spawnParticles(ParticleEffectId fx, glm::vec3 worldPos,
                                    const ParticleSpawnOptions& options);
    // Convenience: resolve the effect by name and spawn (invalid name = no-op).
    ParticlesHandle  spawnParticles(const std::string& name, NodeHandle parent,
                                    glm::vec3 localPos = glm::vec3(0.0f));
    ParticlesHandle  spawnParticles(const std::string& name, NodeHandle parent,
                                    const ParticleSpawnOptions& options);
    ParticlesHandle  spawnParticles(const std::string& name, NodeHandle parent,
                                    glm::vec3 localPos,
                                    const ParticleSpawnOptions& options);
    ParticlesHandle  spawnParticles(const std::string& name, glm::vec3 worldPos);
    ParticlesHandle  spawnParticles(const std::string& name, glm::vec3 worldPos,
                                    const ParticleSpawnOptions& options);
    void stopParticles(ParticlesHandle h);
    void despawnParticles(ParticlesHandle h);
    void setParticleQuality(float q);
    // Every texture the particle import declared, in stem order. This is what a
    // tuning panel lists; nothing in gameplay reads it. The descs carry the
    // flipbook window too, so a panel can show a strip's frame count and rate
    // without re-reading the TOML.
    const std::vector<ParticleTextureDesc>& particleTextures() const;
    // Re-scan the particle texture import (new PNGs, edited *.toml). Materials
    // already in use are updated in place, so live effects keep drawing.
    bool reloadParticleTextures();
    uint32_t liveParticleCount() const;
    // Install the world the particle simulation traces against. The renderer
    // never links physics, so the application owns the adapter and its
    // lifetime: it must outlive the renderer or be cleared with nullptr.
    // Without one, effects that ask to collide simply pass through everything.
    // Drop every particle batch and decal while Ogre is still alive. Engine
    // calls this immediately before tearing the render core down; the
    // destructor cannot do it, because by then the SceneManager is gone.
    void shutdownParticles();
    void setParticleCollider(IParticleCollider* collider);
    void setParticleRayBudget(uint32_t raysPerFrame);
    // Decal profiles are authored per game, so the runtime holds them but does
    // not parse them. Registering the same id twice replaces the profile.
    void registerDecalProfile(const std::string& id, const DecalProfileDesc&);
    // Marks a surface directly, for the caller that already has a hit and does
    // not want a particle to carry it there -- a bullet hole under a hitscan,
    // say. Returns false when the profile is unknown.
    bool spawnDecal(const std::string& profile, glm::vec3 position,
                    glm::vec3 normal);
    void updateParticles(float dt);
    void attachCamera(NodeHandle node); // moves the single camera to this node
    LightHandle attachLight(NodeHandle node, const LightDesc& desc);
    // Retint an existing light (linear, energy pre-multiplied) -- cheap,
    // intended for per-frame effects like torch flicker.
    void setLightColour(LightHandle light, glm::vec3 colour);
    // Set a point light's falloff range (linear attenuation range).
    void setLightRange(LightHandle light, float range);

    // Read-only scene-graph introspection facade for editor tooling.
    SceneView scene() const;

    // --- camera -----------------------------------------------------------
    void setCameraFov(float degrees); // vertical FOV
    void setCameraClip(float nearDist, float farDist);
    // Current camera view-projection (world -> clip). For projecting debug
    // overlays (collider gizmos) to screen space at full window resolution.
    glm::mat4 cameraViewProj() const;
    // The same pair, separately. ImGuizmo takes view and projection as two
    // matrices, and multiplying them back apart is not possible -- so a tool
    // that manipulates a transform in the game's own view (the viewmodel
    // gizmo) needs these rather than the product above.
    glm::mat4 cameraView() const;
    glm::mat4 cameraProjection() const;

    // --- materials --------------------------------------------------------
    // Parse one generated Ogre material script at runtime. Editor imports use
    // this after writing a GLB's base-colour texture material so new geometry
    // does not require an editor restart before it renders correctly.
    bool loadMaterialScript(const std::string& path);
    // Re-index the mounted resource directories.
    //
    // Ogre builds a FileSystem archive's file list once, at
    // initialiseAllResourceGroups(), so a file written *after* start-up does
    // not exist as far as a texture unit is concerned. Nothing reports this:
    // the material parses, the texture silently resolves to nothing, and the
    // model renders untextured.
    //
    // That is exactly what an in-editor model import does -- copy a PNG in,
    // then load a material naming it -- so it must re-index between the two.
    // Already-loaded resources are untouched; this only refreshes what the
    // archives know is on disk.
    void refreshAssetIndex();
    void setMaterialParam(const std::string& materialName,
                          const std::string& paramName, float value);
    void setMaterialParam(const std::string& materialName,
                          const std::string& paramName, glm::vec2 value);
    void setMaterialParam(const std::string& materialName,
                          const std::string& paramName, glm::vec3 value);
    void setMaterialParam(const std::string& materialName,
                          const std::string& paramName, glm::vec4 value);

    // Per-node shader uniforms: give this node's meshes their own copy of the
    // material they wear, and set the PSX family's per-entity uniforms on it
    // (see eng::ecs::ShaderParams for what each one drives).
    //
    // A named material is shared -- `Game/Kit/Dungeon` is one object a hundred
    // and sixty walls point at -- so setMaterialParam below tints all of them.
    // This clones instead, which costs a material and breaks this node out of
    // its batch. That is the right trade for the handful of hero objects that
    // want it and the wrong one for a level, which is why it is opt-in per
    // node and never applied by default.
    //
    // The clone is made once per subentity and reused, so calling this every
    // frame with an animated value costs constant sets, not clones. Reverted by
    // clearNodeShaderParams(), which puts the shared material back.
    void setNodeShaderParams(NodeHandle node, const ShaderUniforms& params);
    void clearNodeShaderParams(NodeHandle node);

    // The general form: push an arbitrary block of uniforms onto this node's
    // private material, taking each uniform's name, type and address from the
    // block's own field table (see eng/ShaderBlock.h).
    //
    // This is what lets a shader have a component without the renderer knowing
    // the shader exists. `setNodeShaderParams` above is the same mechanism with
    // the PSX family's six knobs hard-coded, kept because those six are
    // universal and worth a typed call.
    //
    // Shares the clone with setNodeShaderParams -- one private material per
    // subentity, however many blocks are pushed onto it -- so an entity may
    // carry both a portal block and the universal tint.
    void setNodeShaderBlock(NodeHandle node, const ShaderBlock& block);

    // Sets a float param on EVERY loaded material that declares it, in both
    // vertex and fragment program params (emulates a Godot global uniform).
    void setGlobalMaterialParam(const std::string& paramName, float value);

    // --- environment ------------------------------------------------------
    void setAmbient(glm::vec3 colour);
    void setFog(glm::vec3 colour, float expDensity);
    void setBackground(glm::vec3 colour);
    const EnvState& envState() const;

    // --- post + verification ---------------------------------------------
    void setDitherEnabled(bool enabled);
    void setPixelSize(int pixelSize);      // 1..16, rebuilds the post chain
    // Absolute post-chain resolution, held across window resizes, for profiles
    // that emulate a specific console framebuffer (PS2 640x448, GameCube
    // 640x480). Overrides setPixelSize; 0 in either axis reverts to it.
    void setRenderResolution(int width, int height);
    void setPerPixelLightingEnabled(bool enabled); // off = authentic vertex-lit
    void setOmniAttenuation(float exponent); // omni falloff curve, 1 = linear
    void setLightSteps(float steps); // posterized diffuse bands, 0 = smooth
    void setLightStepSoftness(float softness); // band seam half-width, 0 = hard
    void setFogDesatBoost(float boost); // 0 = plain fog lerp
    void setBloomEnabled(bool enabled);    // off = pass-through composite
    void setBloomParams(float threshold, float intensity);
    // Debug view: swaps every entity's materials for light-blue wireframe
    // lines (Engine/Psx/DebugWireframe); off restores the original materials.
    void setWireframeDebug(bool enabled);
    void setGradeEnabled(bool enabled); // colour grade before quantization
    void setGradeParams(float desaturate, float contrast,
                        glm::vec3 shadowTint, glm::vec3 midTint);
    void writeScreenshot(const std::string& path);
    // Whole-frame draw-call (batch) + triangle counts across the window and the
    // live PSX post-chain targets. For the on-screen performance HUD.
    void frameStats(size_t& batches, size_t& triangles) const;

    // --- editor offscreen viewport ---------------------------------------
    void enableEditorViewport(int w, int h);
    void resizeEditorViewport(int w, int h);
    uint64_t editorViewportTextureId() const;
    // Drive the editor viewport's dedicated free-fly camera (decoupled from the
    // game MainCamera). Call every frame from the editor's EditorCamera.
    // The offscreen viewport clears to its own colour; setBackground() only
    // reaches the window's viewport, so an editor backdrop has to be set here.
    void setEditorViewportBackground(const glm::vec3& colour);

    // --- material thumbnails ---------------------------------------------
    // A small square offscreen target holding one object on its own, for the
    // material-swatch grid every engine editor has. Separate from the main
    // editor viewport: different size, different camera, different subject.
    //
    // Nodes marked setNodeThumbnailOnly() appear in this target and NOWHERE
    // else, so the preview sphere can sit at the world origin of a loaded level
    // without ever showing up in it.
    void enableMaterialThumbnail(int size);
    void setMaterialThumbnailCamera(const glm::vec3& position,
                                    const glm::quat& orientation, float fovDeg);
    uint64_t materialThumbnailTextureId() const;
    void setNodeThumbnailOnly(NodeHandle node, bool thumbnailOnly);
    void setEditorCameraPose(const glm::vec3& pos, const glm::quat& orient,
                             float fovDeg);

    // --- debug line overlay -----------------------------------------------
    struct DebugLine { glm::vec3 a{0}; glm::vec3 b{0}; glm::vec3 colour{1,1,1}; };
    // Replace the debug line set for this frame. Pass empty to clear. Rebuilt
    // into a single unlit line-list; call once per frame when the overlay is on.
    void setDebugLines(const std::vector<DebugLine>& lines);
    // X-ray toggle for the debug lines: on = ignore depth (draw over all
    // geometry), off = depth-tested so they occlude behind nearer surfaces.
    void setDebugLinesXray(bool xray);

private:
    friend class Engine; // Engine constructs, initialises, and drives it
    friend RenderCore& detail::coreOf(Renderer&);
    friend void detail::registerRoot(Renderer&);
    friend class SceneView; // reads mImpl to walk the scene graph read-only
    Renderer();
    ~Renderer();
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};

} // namespace eng
