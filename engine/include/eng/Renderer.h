#pragma once
#include <eng/Handles.h>
#include <eng/LightDesc.h>
#include <eng/particles/ParticleEffectDesc.h>
#include <eng/render/Enchantment.h>
#include <eng/render/ModelImport.h>
#include <eng/Sprite.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <memory>
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
    // bake, when given, is multiplied into vertex positions (normals get
    // its inverse-transpose) -- for transforms TRS nodes can't represent.
    MeshHandle loadObj(const std::string& path, const glm::mat4* bake = nullptr);
    MeshHandle loadObj(const std::string& path,
                       const ModelImportOptions& options);
    // Sole generic primitive entry point. The descriptor's dimensions are
    // baked into the mesh; node scale remains available for placement.
    MeshHandle createPrimitiveMesh(const PrimitiveMeshDesc&);
    bool meshBounds(MeshHandle mesh, MeshBounds& out) const;
    // OBJ geometry captured during the render-mesh load, never reparsed or
    // read back from Ogre. Returns false for meshes without cached triangles.
    bool meshCollisionGeometry(MeshHandle mesh,
                               std::vector<glm::vec3>& vertices,
                               std::vector<uint32_t>& indices) const;
    // Releases one Renderer-created mesh and its CPU collision cache. Safe for
    // invalid/already-released handles; callers must first destroy attachments
    // that use this uniquely owned mesh.
    bool releaseMesh(MeshHandle mesh);

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
    void setNodeEnchantment(NodeHandle node, EnchantmentStyle style,
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
    void attachMesh(NodeHandle node, MeshHandle mesh,
                    const ResolvedModelMaterial& material,
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

    // --- materials --------------------------------------------------------
    void setMaterialParam(const std::string& materialName,
                          const std::string& paramName, float value);
    void setMaterialParam(const std::string& materialName,
                          const std::string& paramName, glm::vec2 value);
    void setMaterialParam(const std::string& materialName,
                          const std::string& paramName, glm::vec3 value);
    void setMaterialParam(const std::string& materialName,
                          const std::string& paramName, glm::vec4 value);

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
    // lines (PSX/DebugWireframe); off restores the original materials.
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
