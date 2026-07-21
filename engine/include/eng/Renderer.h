#pragma once
#include <eng/Handles.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <memory>
#include <string>

namespace eng {

class RenderCore; // internal; forward-declared only, no Ogre leak
class Renderer;

namespace detail {
// Engine-only backdoor to the internal core (defined in Renderer.cpp).
RenderCore& coreOf(Renderer& r);
void registerRoot(Renderer& r);
} // namespace detail

struct LightDesc {
    enum class Type { Directional, Point };
    Type type = Type::Point;
    glm::vec3 colour{1.0f}; // linear, energy pre-multiplied by the caller
    float range = 3.0f;     // point lights only
    bool castShadows = false; // stencil shadows from opted-in casters
};

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
    bool stylize = true;     // outline/highlight pass active
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
    MeshHandle createInteriorBox(float size, int subdivide);
    MeshHandle createPlane(float size);

    // --- scene graph ------------------------------------------------------
    NodeHandle createNode(NodeHandle parent, glm::vec3 position = glm::vec3(0.0f));
    void setPosition(NodeHandle node, glm::vec3 position);
    void setOrientation(NodeHandle node, glm::quat orientation);
    void setScale(NodeHandle node, glm::vec3 scale);
    // Show/hide a node and everything attached beneath it (meshes,
    // particles, lights).
    void setNodeVisible(NodeHandle node, bool show);

    // --- attachments ------------------------------------------------------
    // castShadows opts the entity into stencil shadow casting; keep it off
    // for open/sliced geometry (walls, floors) -- shadow volumes need
    // closed-ish meshes to extrude cleanly.
    void attachMesh(NodeHandle node, MeshHandle mesh,
                    const std::string& materialName, bool castShadows = false);

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
    void attachParticles(NodeHandle node, const std::string& templateName);
    void attachCamera(NodeHandle node); // moves the single camera to this node
    LightHandle attachLight(NodeHandle node, const LightDesc& desc);
    // Retint an existing light (linear, energy pre-multiplied) -- cheap,
    // intended for per-frame effects like torch flicker.
    void setLightColour(LightHandle light, glm::vec3 colour);

    // --- camera -----------------------------------------------------------
    void setCameraFov(float degrees); // vertical FOV
    void setCameraClip(float nearDist, float farDist);

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
    void setStylizeEnabled(bool enabled);  // off = pass-through (PSX look only)
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

private:
    friend class Engine; // Engine constructs, initialises, and drives it
    friend RenderCore& detail::coreOf(Renderer&);
    friend void detail::registerRoot(Renderer&);
    Renderer();
    ~Renderer();
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};

} // namespace eng
