#include <eng/Renderer.h>

#include "MaterialLibrary.h"
#include "RenderCore.h"
#include "render/AssimpLoader.h"
#include "render/PrimitiveGeometry.h"
#include "render/SceneRegistry.h"
#include "render/SkinnedAssimpLoader.h"
#include "particles/ParticleSim.h"
#include "particles/ParticleTextureCatalog.h"

#include <eng/LightDesc.h>
#include <eng/Log.h>
#include <eng/Primitive.h>
#include <eng/SceneView.h>
#include <eng/assets/AssetRoot.h>
#include <eng/particles/ParticlePresets.h>
#include <eng/render/PrototypeAssets.h>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace eng {
namespace {

struct MeshVertex {
    glm::vec3 position{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    glm::vec2 uv{0.0f};
    glm::vec4 colour{1.0f};
};

static_assert(sizeof(MeshVertex) == 48);

constexpr uint32_t kMaxSkinJoints = 256;

struct SkinnedMeshVertex {
    glm::vec3 position{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    glm::vec2 uv{0.0f};
    glm::vec4 colour{1.0f};
    std::array<uint16_t, 4> joints{};
    std::array<float, 4> weights{1.0f, 0.0f, 0.0f, 0.0f};
};

static_assert(sizeof(SkinnedMeshVertex) == 72);

// One billboard corner. Positions are world-space (the CPU already billboarded
// them), so the particle vertex stage is a plain viewProjection transform.
struct ParticleVertex {
    glm::vec3 position{0.0f};
    glm::vec2 uv{0.0f};
    glm::vec4 colour{1.0f};
};

static_assert(sizeof(ParticleVertex) == 36);

// One end of a debug line. World space already -- eng::Renderer::DebugLine
// carries world positions, and there is no model matrix in this pass.
struct DebugLineVertex {
    glm::vec3 position{0.0f};
    glm::vec4 colour{1.0f};
};

static_assert(sizeof(DebugLineVertex) == 28);

// The editor's grid is the heavy caller: a 33x33 patch is ~132 vertices, and
// the collider overlay on a dense room is a few thousand. 64k ends the question
// without being worth streaming.
constexpr uint32_t kMaxDebugLineVertices = 65536;

// Matches ParticleSim's reserve() below; a burst past it drops the tail rather
// than reallocating a GPU buffer mid-frame.
constexpr uint32_t kMaxDrawnParticles = 8192;

// Keep in sync with the mode switch in assets/shaders/vulkan/particle.frag.
// Ogre compiles one fragment_program per look; a runtime mode keeps the RHI
// pipeline cache keyed on blend state alone.
enum class ParticleMode : uint32_t {
    Textured = 0, Atlas, Flame, Smoke, Rain, Block, Mote, Shard, Bubble, Wisp
};

ParticleMode particleModeFor(rhi_renderer::MaterialShader shader)
{
    using S = rhi_renderer::MaterialShader;
    switch (shader) {
        case S::ParticleAtlas:  return ParticleMode::Atlas;
        case S::ParticleFlame:  return ParticleMode::Flame;
        case S::ParticleSmoke:  return ParticleMode::Smoke;
        case S::ParticleRain:   return ParticleMode::Rain;
        case S::ParticleBlock:  return ParticleMode::Block;
        case S::ParticleMote:   return ParticleMode::Mote;
        case S::ParticleShard:  return ParticleMode::Shard;
        case S::ParticleBubble: return ParticleMode::Bubble;
        case S::ParticleWisp:   return ParticleMode::Wisp;
        default:                return ParticleMode::Textured;
    }
}

// The stylised scrolling-surface profiles. Keep in sync with the mode switch in
// assets/shaders/vulkan/surface.frag.
enum class SurfaceMode : uint32_t { None = 0, Liquid, Lava, Portal };

SurfaceMode surfaceModeFor(rhi_renderer::MaterialShader shader)
{
    using S = rhi_renderer::MaterialShader;
    if (shader == S::SurfaceLiquid) return SurfaceMode::Liquid;
    if (shader == S::SurfaceLava)   return SurfaceMode::Lava;
    // The authored (textured flow) and prototype (procedural) portals are one
    // profile: they differ only in where surfaceField comes from, and this
    // backend samples a texture either way.
    if (shader == S::SurfacePortal) return SurfaceMode::Portal;
    return SurfaceMode::None;
}
// The half of a surface profile's parameters that does not fit the push range
// once the model matrix is in it. Per material rather than per draw, uploaded
// once per frame for the one surface being drawn.
// Field order is the shader's std140 block, verbatim. It drifted once already:
// the shader grew paletteA..C while this struct still began at paletteD, so
// every field read one slot early and the liquids came out of the wrong
// colours entirely. Keep the two in the same order or neither is meaningful.
struct alignas(16) SurfaceUniforms {
    glm::vec4 paletteA{0.0f};
    glm::vec4 paletteB{0.0f};
    glm::vec4 paletteC{0.0f};
    glm::vec4 paletteD{0.0f};
    glm::vec4 glowColour{0.0f};
    glm::vec4 flow{0.0f};
    glm::vec4 tuning{8.0f, 32.0f, 0.0f, 0.22f};
    glm::vec4 modeTime{0.0f};
    glm::vec4 present{0.0f, 0.0f, 1.0f, 0.0f}; // texel, dither, bright, glow
    glm::vec4 rims{0.0f, 0.0f, 0.0f, 1.0f};    // glow, flow, mode, threshold
    glm::vec4 motion{0.0f, 0.0f, 0.0f, 1.0f};  // flow, swirl, twist, arms
    glm::vec4 shapeA{0.5f, 1.0f, 0.0f, 0.0f};  // armW, depth, parallax, weight
    glm::vec4 shapeB{0.0f, 0.0f, 0.5f, 0.05f}; // coreR, coreB, rimR, rimW
    glm::vec4 shapeC{0.0f};                    // rimIntensity, edgeFade
};

// Particle draws replace the mesh DrawConstants entirely: the vertex stage
// needs no model matrix (corners are already world-space) and the fragment
// stage only wants its mode and cutoff.
struct ParticleConstants {
    glm::vec4 modeScissor{0.0f};
    float pad[28]{};
};

static_assert(sizeof(ParticleConstants) == 128);

struct alignas(16) SceneUniforms {
    glm::mat4 viewProjection{1.0f};
    glm::mat4 view{1.0f};
    glm::vec4 cameraPositionAndLightCount{0.0f};
    glm::vec4 ambient{0.0f};
    glm::vec4 fogColourDensity{0.0f};
    glm::vec4 clipParams{0.05f, 4000.0f, 0.0f, 0.0f};
    glm::mat4 lightViewProjection{1.0f};
    glm::vec4 shadowParams{0.0f};  // enabled, bias, strength, texel
    std::array<glm::vec4, 16> lightPositionRange{};
    std::array<glm::vec4, 16> lightColourType{};
    // The era knobs, appended last on purpose: every shader that declares this
    // block must agree on the offsets of what it *does* declare, and adding at
    // the end leaves the ones that never look at these (shadow, particle,
    // debug line) correct without touching them.
    //   x precisionMultiplier  vertex snap grid
    //   y lightSteps           0 = smooth, >0 = posterized bands
    //   z lightStepSoftness    band-edge half width
    //   w affineAmount         0 = perspective UVs, 1 = full affine
    glm::vec4 psxParams{1.0f, 0.0f, 0.30f, 0.0f};
    //   x affineSoftness  UV divergence at which the warp saturates
    //   yzw reserved
    glm::vec4 psxParams2{0.10f, 0.0f, 0.0f, 0.0f};
};

struct DrawConstants {
    glm::mat4 model{1.0f};
    glm::vec4 tintOpacity{1.0f};
    glm::vec4 rimColourStrength{0.0f};
    glm::vec4 surfaceParams{0.0f, 3.0f, 0.0f, 0.0f};
    glm::vec4 uvTransform{1.0f, 1.0f, 0.0f, 0.0f};
};

static_assert(sizeof(DrawConstants) == 128);

bool finite(glm::vec3 value)
{
    return std::isfinite(value.x) && std::isfinite(value.y) &&
           std::isfinite(value.z);
}

bool finite(glm::quat value)
{
    return std::isfinite(value.w) && std::isfinite(value.x) &&
           std::isfinite(value.y) && std::isfinite(value.z);
}

bool finite(const glm::mat4& value)
{
    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row)
            if (!std::isfinite(value[column][row]))
                return false;
    return true;
}

std::vector<uint8_t> readBytes(const char* path)
{
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input)
        return {};
    const std::streamsize size = input.tellg();
    if (size <= 0)
        return {};
    input.seekg(0);
    std::vector<uint8_t> result(static_cast<size_t>(size));
    if (!input.read(reinterpret_cast<char*>(result.data()), size))
        return {};
    return result;
}

ParticleEffectDesc sanitizeParticleEffect(ParticleEffectDesc desc)
{
    desc.baseWidth = std::max(0.001f, desc.baseWidth);
    desc.baseHeight = std::max(0.001f, desc.baseHeight);
    desc.quota = std::max(1, desc.quota);
    desc.burstCount = std::max(0.0f, desc.burstCount);
    desc.qualityWeight = std::clamp(desc.qualityWeight, 0.0f, 1.0f);
    desc.drag = std::isfinite(desc.drag) ? std::max(0.0f, desc.drag) : 0.0f;
    desc.restitution = std::clamp(desc.restitution, 0.0f, 1.0f);
    desc.friction = std::clamp(desc.friction, 0.0f, 1.0f);
    for (ParticleEmitterDesc& emitter : desc.emitters) {
        emitter.boxSize = glm::max(emitter.boxSize, glm::vec3(0.001f));
        emitter.angleDegrees = std::clamp(emitter.angleDegrees, 0.0f, 180.0f);
        emitter.emissionRate = std::max(0.0f, emitter.emissionRate);
        emitter.ttlMin = std::max(0.001f, emitter.ttlMin);
        emitter.ttlMax = std::max(0.001f, emitter.ttlMax);
        emitter.velocityMin = std::max(0.0f, emitter.velocityMin);
        emitter.velocityMax = std::max(0.0f, emitter.velocityMax);
        if (emitter.ttlMin > emitter.ttlMax)
            std::swap(emitter.ttlMin, emitter.ttlMax);
        if (emitter.velocityMin > emitter.velocityMax)
            std::swap(emitter.velocityMin, emitter.velocityMax);
        if (glm::dot(emitter.direction, emitter.direction) < 1e-8f)
            emitter.direction = {0.0f, 1.0f, 0.0f};
        else
            emitter.direction = glm::normalize(emitter.direction);
    }
    const auto sortStops = [](auto& stops) {
        for (auto& stop : stops)
            stop.t = std::clamp(stop.t, 0.0f, 1.0f);
        std::stable_sort(stops.begin(), stops.end(),
                         [](const auto& a, const auto& b) {
                             return a.t < b.t;
                         });
    };
    sortStops(desc.colourRamp);
    sortStops(desc.sizeRamp);
    return desc;
}

} // namespace

struct Renderer::Impl {
    struct GpuSubmesh {
        rhi::BufferHandle vertices;
        rhi::BufferHandle indices;
        uint32_t indexCount = 0;
        std::string sourceMaterial;
    };
    struct Mesh {
        bool alive = false;
        bool shared = false;
        std::string name;
        std::vector<GpuSubmesh> submeshes;
        std::vector<glm::vec3> collisionVertices;
        std::vector<uint32_t> collisionIndices;
        MeshBounds bounds;
        ModelImportReport report;
    };
    struct MeshAttachment {
        MeshHandle mesh;
        std::vector<std::string> materials;
        bool castShadows = false;
        bool renderOnTop = false;
    };
    struct SkinnedGpuSubmesh {
        rhi::BufferHandle vertices;
        rhi::BufferHandle indices;
        uint32_t indexCount = 0;
        std::string sourceMaterial;
        std::vector<glm::mat4> inverseBindPoses;
    };
    struct SkinnedMesh {
        bool alive = false;
        std::string name;
        uint32_t jointCount = 0;
        std::vector<SkinnedGpuSubmesh> submeshes;
        MeshBounds bounds;
    };
    struct SkinInstance {
        bool alive = false;
        NodeHandle node{};
        SkinnedMeshHandle mesh{};
        std::vector<std::string> materials;
        std::vector<rhi::BufferHandle> paletteBuffers;
        bool castShadows = false;
        bool renderOnTop = false;
    };
    struct Node {
        bool alive = false;
        NodeHandle parent{};
        std::vector<NodeHandle> children;
        NodeTransform local;
        bool visible = true;
        bool thumbnailOnly = false;
        std::vector<MeshAttachment> meshes;
        ShaderUniforms shader;
        bool hasShader = false;
        std::optional<EnchantmentDesc> enchantment;
        std::unordered_map<std::string, rhi_renderer::MaterialValue> blocks;
    };
    struct Light {
        bool alive = false;
        NodeHandle node{};
        LightDesc desc;
    };
    struct Sprite {
        bool alive = false;
        NodeHandle node{};
        SpriteClip clip;
        bool visible = true;
    };
    struct StaticBatch {
        struct Record {
            MeshHandle mesh;
            std::string material;
            glm::vec3 position{0.0f};
            float yawDeg = 0.0f;
        };
        glm::vec3 regionSize{1.0f};
        std::vector<Record> records;
        bool built = false;
        bool visible = true;
    };
    struct ParticleEffect {
        ParticleEffectDesc desc;
        uint16_t simId = 0;
        // Resolved once on registration: the PNG the stem names, plus the
        // presentation metadata the sheet entry carries.
        RenderCore::TextureBinding texture;
        ParticleBlend blend = ParticleBlend::Alpha;
        FlipbookDesc flipbook;
        ParticleMode mode = ParticleMode::Textured;
        float alphaScissor = 0.0f;
        // The hand-authored atlas materials animate a row of a shared sheet on
        // a wall clock, which is a different thing from the catalogue flipbook
        // above (that one is driven by each particle's own age).
        glm::vec2 atlasGrid{1.0f};
        float atlasRow = 0.0f;
        float atlasFrames = 1.0f;
        float atlasFps = 0.0f;
        bool atlas = false;
        bool resolved = false;
    };
    struct LiveParticles {
        uint32_t instance = ParticleSim::kInvalidInstance;
        NodeHandle parent{};
        glm::mat4 fixedTransform{1.0f};
        bool followsNode = false;
    };

    RenderCore core;
    rhi_renderer::MaterialLibrary materials;
    SceneRegistry sceneRegistry;
    std::vector<Mesh> meshes;
    std::vector<SkinnedMesh> skinnedMeshes;
    std::vector<SkinInstance> skinInstances;
    std::vector<Node> nodes;
    std::vector<Light> lights;
    std::vector<Sprite> sprites;
    std::vector<StaticBatch> staticBatches;
    std::unordered_map<std::string, MeshHandle> prototypeMeshes;
    prototype::PrototypeCatalog prototypes;
    ModelMaterialFallbackWarnings missingMaterialWarnings;
    EnvState env;
    // Modulative darkening applied where the sun is occluded, matching the
    // tone Ogre's stencil pass used. Not in EnvState because nothing else in
    // the engine has a concept of it yet.
    float shadowStrength = 0.55f;
    // Whether the sun casts. The pass itself runs either way (see
    // RenderCore): it is what keeps the depth target in a sampleable layout.
    bool shadowsEnabled = true;
    // Post-chain settings stashed by setWireframeDebug(true), restored on
    // toggle-off (the view bypasses them so the lines stay crisp).
    struct {
        int pixelSize = 3;
        bool dither = false, bloom = true, grade = false;
    } preWireframe;
    NodeHandle cameraNode{};
    int nameCounter = 0;

    rhi::ShaderHandle sceneVertex;
    rhi::ShaderHandle sceneFragment;
    std::unordered_map<uint32_t, rhi::PipelineHandle> pipelines;
    rhi::ShaderHandle skinnedSceneVertex;
    std::unordered_map<uint32_t, rhi::PipelineHandle> skinnedPipelines;
    std::array<rhi::BufferHandle, 4> sceneUniformBuffers{};
    bool gpuShutdown = false;

    rhi::ShaderHandle shadowVertex;
    rhi::ShaderHandle shadowFragment;
    std::unordered_map<uint32_t, rhi::PipelineHandle> shadowPipelines;
    rhi::ShaderHandle skinnedShadowVertex;
    std::unordered_map<uint32_t, rhi::PipelineHandle> skinnedShadowPipelines;
    rhi::ShaderHandle surfaceVertex;
    rhi::ShaderHandle surfaceFragment;
    std::unordered_map<uint32_t, rhi::PipelineHandle> surfacePipelines;
    // One buffer PER MATERIAL, not one shared. updateBuffer writes host memory
    // now but the GPU reads it when the command buffer executes, so a single
    // buffer rewritten before each draw hands every surface in the frame the
    // last material's parameters -- which is exactly how water, slime and lava
    // all came out as one flat two-tone palette.
    std::unordered_map<std::string, rhi::BufferHandle> surfaceUniformBuffers;
    // Advanced alongside the particle clock so the two stay in step.
    float surfaceTime = 0.0f;

    ParticleSim particleSim;
    ParticleTextureCatalog particleTextures;
    // Wall clock for the atlas materials, advanced by updateParticles().
    float particleTime = 0.0f;
    rhi::ShaderHandle particleVertexShader;
    rhi::ShaderHandle particleFragmentShader;
    // Keyed by blend mode: alpha and additive differ only in the blend state.
    std::unordered_map<uint32_t, rhi::PipelineHandle> particlePipelines;
    rhi::BufferHandle particleVertices;
    rhi::BufferHandle particleIndices;
    rhi::ShaderHandle debugLineVertexShader;
    rhi::ShaderHandle debugLineFragmentShader;
    rhi::BufferHandle debugLineVertices;
    std::unordered_map<uint32_t, rhi::PipelineHandle> debugLinePipelines;
    std::vector<DebugLineVertex> debugLineStaging;
    std::vector<ParticleVertex> particleStaging;
    std::vector<uint32_t> particleOrder;
    std::vector<ParticleEffect> particleEffects;
    std::unordered_map<std::string, uint32_t> particleByName;
    std::unordered_map<uint32_t, LiveParticles> liveParticles;
    std::vector<DecalRequest> decalRequests;
    DecalSystem decals;
    IParticleCollider* particleCollider = nullptr;
    uint32_t nextParticleHandle = 1;
    float particleQuality = 1.0f;

    std::vector<DebugLine> debugLines;
    bool debugLinesXray = false;
    std::unordered_set<std::string> warned;

    ~Impl() { core.shutdown(); }

    void warnOnce(const std::string& key, const char* message)
    {
        if (warned.insert(key).second)
            log::warn("RHI renderer: %s not rendered yet", message);
    }

    Node* node(NodeHandle handle, const char* operation)
    {
        if (!handle.valid() || handle.id > nodes.size() ||
            !nodes[handle.id - 1].alive) {
            log::error("RHI renderer: invalid node handle %u in %s", handle.id,
                       operation);
            return nullptr;
        }
        return &nodes[handle.id - 1];
    }
    const Node* node(NodeHandle handle) const
    {
        if (!handle.valid() || handle.id > nodes.size() ||
            !nodes[handle.id - 1].alive)
            return nullptr;
        return &nodes[handle.id - 1];
    }
    Mesh* mesh(MeshHandle handle)
    {
        if (!handle.valid() || handle.id > meshes.size() ||
            !meshes[handle.id - 1].alive)
            return nullptr;
        return &meshes[handle.id - 1];
    }
    const Mesh* mesh(MeshHandle handle) const
    {
        return const_cast<Impl*>(this)->mesh(handle);
    }
    SkinnedMesh* skinnedMesh(SkinnedMeshHandle handle)
    {
        if (!handle.valid() || handle.id > skinnedMeshes.size() ||
            !skinnedMeshes[handle.id - 1].alive)
            return nullptr;
        return &skinnedMeshes[handle.id - 1];
    }
    const SkinnedMesh* skinnedMesh(SkinnedMeshHandle handle) const
    {
        return const_cast<Impl*>(this)->skinnedMesh(handle);
    }
    SkinInstance* skinInstance(SkinInstanceHandle handle)
    {
        if (!handle.valid() || handle.id > skinInstances.size() ||
            !skinInstances[handle.id - 1].alive)
            return nullptr;
        return &skinInstances[handle.id - 1];
    }

    std::string nextName(const char* prefix)
    {
        return std::string(prefix) + std::to_string(++nameCounter);
    }

    NodeTransform worldTransform(NodeHandle handle) const
    {
        const Node* current = node(handle);
        if (!current)
            return {};
        NodeTransform result = current->local;
        NodeHandle parent = current->parent;
        size_t guard = 0;
        while (parent.valid() && guard++ < nodes.size()) {
            const Node* ancestor = node(parent);
            if (!ancestor)
                break;
            result.position = ancestor->local.position +
                              ancestor->local.orientation *
                                  (ancestor->local.scale * result.position);
            result.orientation =
                glm::normalize(ancestor->local.orientation * result.orientation);
            result.scale = ancestor->local.scale * result.scale;
            parent = ancestor->parent;
        }
        return result;
    }

    glm::mat4 worldMatrix(NodeHandle handle) const
    {
        const NodeTransform transform = worldTransform(handle);
        return glm::translate(glm::mat4(1.0f), transform.position) *
               glm::mat4_cast(transform.orientation) *
               glm::scale(glm::mat4(1.0f), transform.scale);
    }

    bool worldVisible(NodeHandle handle) const
    {
        size_t guard = 0;
        while (handle.valid() && guard++ < nodes.size()) {
            const Node* current = node(handle);
            if (!current || !current->visible)
                return false;
            handle = current->parent;
        }
        return true;
    }

    rhi::ShaderHandle loadSceneShader(rhi::ShaderStage stage, const char* path,
                                      const char* name)
    {
        rhi::ShaderDesc desc;
        desc.stage = stage;
        desc.code = readBytes(path);
        desc.debugName = name;
        if (desc.code.empty()) {
            log::error("RHI renderer: scene shader '%s' is unreadable", path);
            return {};
        }
        return core.device()->createShader(desc);
    }

    uint32_t pipelineKey(const rhi_renderer::Material& material,
                         bool renderOnTop, bool withNormalDepth) const
    {
        uint32_t key = uint32_t(material.cull);
        key |= uint32_t(material.blend) << 3u;
        key |= uint32_t(material.depthTest && !renderOnTop) << 6u;
        key |= uint32_t(material.depthWrite && !renderOnTop) << 7u;
        // The editor/thumbnail passes have no MRT metadata surface, and a
        // pipeline's colour attachment count must match the pass it runs in.
        key |= uint32_t(withNormalDepth) << 8u;
        // The wireframe debug view is a whole separate rasterizer state, so it
        // needs its own pipelines rather than mutating the cached ones.
        key |= uint32_t(env.wireframe) << 9u;
        return key;
    }

    rhi::PipelineHandle pipelineFor(const rhi_renderer::Material& material,
                                    bool renderOnTop,
                                    bool withNormalDepth = true)
    {
        const uint32_t key = pipelineKey(material, renderOnTop, withNormalDepth);
        if (const auto found = pipelines.find(key); found != pipelines.end())
            return found->second;
        rhi::PipelineDesc desc;
        desc.vertex = sceneVertex;
        desc.fragment = sceneFragment;
        desc.vertexLayout.bindings.push_back({0, sizeof(MeshVertex), false});
        desc.vertexLayout.attributes.push_back(
            {0, 0, rhi::VertexFormat::Float3, offsetof(MeshVertex, position)});
        desc.vertexLayout.attributes.push_back(
            {1, 0, rhi::VertexFormat::Float3, offsetof(MeshVertex, normal)});
        desc.vertexLayout.attributes.push_back(
            {2, 0, rhi::VertexFormat::Float2, offsetof(MeshVertex, uv)});
        desc.vertexLayout.attributes.push_back(
            {3, 0, rhi::VertexFormat::Float4, offsetof(MeshVertex, colour)});
        // Lines, unculled: a wireframe that back-face culls hides exactly the
        // edges the view exists to show.
        desc.polygonMode = env.wireframe ? rhi::PolygonMode::Line
                                         : rhi::PolygonMode::Fill;
        desc.cull = env.wireframe ? rhi::CullMode::None : material.cull;
        desc.blend = material.blend;
        desc.depth.testEnabled = material.depthTest && !renderOnTop;
        desc.depth.writeEnabled = material.depthWrite && !renderOnTop;
        desc.depth.compare = rhi::CompareOp::LessEqual;
        desc.colourFormats = withNormalDepth
                                 ? std::vector<rhi::Format>{
                                       rhi::Format::RGBA8Unorm,
                                       rhi::Format::RGBA16Float}
                                 : std::vector<rhi::Format>{
                                       rhi::Format::RGBA8Unorm};
        desc.depthFormat = rhi::Format::Depth32Float;
        desc.debugName = "renderer.scene-pipeline-" + std::to_string(key);
        const rhi::PipelineHandle pipeline = core.device()->createPipeline(desc);
        if (pipeline.valid())
            pipelines[key] = pipeline;
        return pipeline;
    }

    void fillSkinnedVertexLayout(rhi::VertexLayout& layout) const
    {
        layout.bindings.push_back({0, sizeof(SkinnedMeshVertex), false});
        layout.attributes.push_back(
            {0, 0, rhi::VertexFormat::Float3,
             offsetof(SkinnedMeshVertex, position)});
        layout.attributes.push_back(
            {1, 0, rhi::VertexFormat::Float3,
             offsetof(SkinnedMeshVertex, normal)});
        layout.attributes.push_back(
            {2, 0, rhi::VertexFormat::Float2, offsetof(SkinnedMeshVertex, uv)});
        layout.attributes.push_back(
            {3, 0, rhi::VertexFormat::Float4,
             offsetof(SkinnedMeshVertex, colour)});
        layout.attributes.push_back(
            {4, 0, rhi::VertexFormat::UShort4Uint,
             offsetof(SkinnedMeshVertex, joints)});
        layout.attributes.push_back(
            {5, 0, rhi::VertexFormat::Float4,
             offsetof(SkinnedMeshVertex, weights)});
    }

    rhi::PipelineHandle
    skinnedPipelineFor(const rhi_renderer::Material& material,
                       bool renderOnTop, bool withNormalDepth = true)
    {
        const uint32_t key = pipelineKey(material, renderOnTop, withNormalDepth);
        if (const auto found = skinnedPipelines.find(key);
            found != skinnedPipelines.end())
            return found->second;
        rhi::PipelineDesc desc;
        desc.vertex = skinnedSceneVertex;
        desc.fragment = sceneFragment;
        fillSkinnedVertexLayout(desc.vertexLayout);
        desc.polygonMode = env.wireframe ? rhi::PolygonMode::Line
                                         : rhi::PolygonMode::Fill;
        desc.cull = env.wireframe ? rhi::CullMode::None : material.cull;
        desc.blend = material.blend;
        desc.depth.testEnabled = material.depthTest && !renderOnTop;
        desc.depth.writeEnabled = material.depthWrite && !renderOnTop;
        desc.depth.compare = rhi::CompareOp::LessEqual;
        desc.colourFormats = withNormalDepth
                                 ? std::vector<rhi::Format>{
                                       rhi::Format::RGBA8Unorm,
                                       rhi::Format::RGBA16Float}
                                 : std::vector<rhi::Format>{
                                       rhi::Format::RGBA8Unorm};
        desc.depthFormat = rhi::Format::Depth32Float;
        desc.debugName = "renderer.skinned-pipeline-" + std::to_string(key);
        const rhi::PipelineHandle pipeline = core.device()->createPipeline(desc);
        if (pipeline.valid())
            skinnedPipelines[key] = pipeline;
        return pipeline;
    }

    bool initializeGpu()
    {
        if (!core.device())
            return false;
        materials.loadAll(core);
        sceneVertex = loadSceneShader(rhi::ShaderStage::Vertex,
                                      ENG_RHI_SCENE_VERT_SPV,
                                      "renderer.scene.vert");
        sceneFragment = loadSceneShader(rhi::ShaderStage::Fragment,
                                        ENG_RHI_SCENE_FRAG_SPV,
                                        "renderer.scene.frag");
        if (!sceneVertex.valid() || !sceneFragment.valid())
            return false;
        skinnedSceneVertex = loadSceneShader(rhi::ShaderStage::Vertex,
                                             ENG_RHI_SKINNED_SCENE_VERT_SPV,
                                             "renderer.skinned-scene.vert");
        if (!skinnedSceneVertex.valid())
            return false;
        for (rhi::BufferHandle& handle : sceneUniformBuffers) {
            rhi::BufferDesc desc;
            desc.size = sizeof(SceneUniforms);
            desc.usage = rhi::BufferUsage::Uniform | rhi::BufferUsage::Dynamic;
            desc.debugName = "renderer.scene-uniforms";
            handle = core.device()->createBuffer(desc);
            if (!handle.valid())
                return false;
        }
        for (const std::string& name : materials.names())
            if (const auto* material = materials.find(name)) {
                pipelineFor(*material, false);
                if (!material->depthTest || !material->depthWrite)
                    pipelineFor(*material, true);
            }
        particleSim.reserve(kMaxDrawnParticles);
        particleSim.setCollider(particleCollider);
        shadowVertex = loadSceneShader(rhi::ShaderStage::Vertex,
                                       ENG_RHI_SHADOW_VERT_SPV,
                                       "renderer.shadow.vert");
        shadowFragment = loadSceneShader(rhi::ShaderStage::Fragment,
                                         ENG_RHI_SHADOW_FRAG_SPV,
                                         "renderer.shadow.frag");
        if (!shadowVertex.valid() || !shadowFragment.valid())
            return false;
        skinnedShadowVertex = loadSceneShader(
            rhi::ShaderStage::Vertex, ENG_RHI_SKINNED_SHADOW_VERT_SPV,
            "renderer.skinned-shadow.vert");
        if (!skinnedShadowVertex.valid())
            return false;
        surfaceVertex = loadSceneShader(rhi::ShaderStage::Vertex,
                                        ENG_RHI_SURFACE_VERT_SPV,
                                        "renderer.surface.vert");
        surfaceFragment = loadSceneShader(rhi::ShaderStage::Fragment,
                                          ENG_RHI_SURFACE_FRAG_SPV,
                                          "renderer.surface.frag");
        if (!surfaceVertex.valid() || !surfaceFragment.valid())
            return false;
        if (!initializeParticleGpu())
            return false;
        core.setDrawScene([this](rhi::CommandList& commands,
                                 const RenderCore::View& view, uint32_t width,
                                 uint32_t height) {
            draw(commands, view, width, height);
        });
        core.setShutdownCallback([this] { shutdownGpu(); });
        gpuShutdown = false;
        return true;
    }

    // Billboards are rebuilt every frame into one dynamic vertex buffer; the
    // index buffer is a fixed quad pattern uploaded once.
    bool initializeParticleGpu()
    {
        particleTextures.load();
        particleVertexShader = loadSceneShader(rhi::ShaderStage::Vertex,
                                               ENG_RHI_PARTICLE_VERT_SPV,
                                               "renderer.particle.vert");
        particleFragmentShader = loadSceneShader(rhi::ShaderStage::Fragment,
                                                 ENG_RHI_PARTICLE_FRAG_SPV,
                                                 "renderer.particle.frag");
        if (!particleVertexShader.valid() || !particleFragmentShader.valid())
            return false;

        rhi::BufferDesc vertices;
        vertices.size = uint64_t(kMaxDrawnParticles) * 4u * sizeof(ParticleVertex);
        vertices.usage = rhi::BufferUsage::Vertex | rhi::BufferUsage::Dynamic;
        vertices.debugName = "renderer.particle-vertices";
        particleVertices = core.device()->createBuffer(vertices);

        // Debug lines ride the same "rebuilt every frame" arrangement: the
        // editor's grid is recentred on the camera every frame anyway.
        debugLineVertexShader = loadSceneShader(rhi::ShaderStage::Vertex,
                                                ENG_RHI_DEBUG_LINE_VERT_SPV,
                                                "renderer.debug_line.vert");
        debugLineFragmentShader = loadSceneShader(rhi::ShaderStage::Fragment,
                                                  ENG_RHI_DEBUG_LINE_FRAG_SPV,
                                                  "renderer.debug_line.frag");
        if (!debugLineVertexShader.valid() || !debugLineFragmentShader.valid())
            return false;
        rhi::BufferDesc lines;
        lines.size = uint64_t(kMaxDebugLineVertices) * sizeof(DebugLineVertex);
        lines.usage = rhi::BufferUsage::Vertex | rhi::BufferUsage::Dynamic;
        lines.debugName = "renderer.debug-line-vertices";
        debugLineVertices = core.device()->createBuffer(lines);

        std::vector<uint32_t> quads(size_t(kMaxDrawnParticles) * 6u);
        for (uint32_t i = 0; i < kMaxDrawnParticles; ++i) {
            const uint32_t v = i * 4u;
            uint32_t* out = &quads[size_t(i) * 6u];
            out[0] = v;     out[1] = v + 1; out[2] = v + 2;
            out[3] = v;     out[4] = v + 2; out[5] = v + 3;
        }
        rhi::BufferDesc indices;
        indices.size = quads.size() * sizeof(uint32_t);
        indices.usage = rhi::BufferUsage::Index;
        indices.debugName = "renderer.particle-indices";
        particleIndices = core.device()->createBuffer(indices);
        if (!particleVertices.valid() || !particleIndices.valid())
            return false;
        core.device()->updateBuffer(particleIndices, quads.data(),
                                    quads.size() * sizeof(uint32_t));
        return true;
    }

    // Same vertex layout and state as a scene draw -- only the fragment stage
    // differs -- so this mirrors pipelineFor() rather than inventing new state.
    rhi::PipelineHandle surfacePipelineFor(const rhi_renderer::Material& material,
                                           bool withNormalDepth)
    {
        const uint32_t key =
            pipelineKey(material, false, withNormalDepth) | (1u << 12u);
        if (const auto found = surfacePipelines.find(key);
            found != surfacePipelines.end())
            return found->second;
        rhi::PipelineDesc desc;
        desc.vertex = surfaceVertex;
        desc.fragment = surfaceFragment;
        desc.vertexLayout.bindings.push_back({0, sizeof(MeshVertex), false});
        desc.vertexLayout.attributes.push_back(
            {0, 0, rhi::VertexFormat::Float3, offsetof(MeshVertex, position)});
        desc.vertexLayout.attributes.push_back(
            {1, 0, rhi::VertexFormat::Float3, offsetof(MeshVertex, normal)});
        desc.vertexLayout.attributes.push_back(
            {2, 0, rhi::VertexFormat::Float2, offsetof(MeshVertex, uv)});
        desc.vertexLayout.attributes.push_back(
            {3, 0, rhi::VertexFormat::Float4, offsetof(MeshVertex, colour)});
        desc.cull = material.cull;
        desc.blend = material.blend;
        desc.depth.testEnabled = material.depthTest;
        desc.depth.writeEnabled = material.depthWrite;
        desc.depth.compare = rhi::CompareOp::LessEqual;
        desc.colourFormats = withNormalDepth
                                 ? std::vector<rhi::Format>{
                                       rhi::Format::RGBA8Unorm,
                                       rhi::Format::RGBA16Float}
                                 : std::vector<rhi::Format>{
                                       rhi::Format::RGBA8Unorm};
        desc.depthFormat = rhi::Format::Depth32Float;
        desc.debugName = "renderer.surface-pipeline-" + std::to_string(key);
        const rhi::PipelineHandle pipeline = core.device()->createPipeline(desc);
        if (pipeline.valid())
            surfacePipelines[key] = pipeline;
        return pipeline;
    }

    // Depth only: no colour attachments, no fragment shader. Front faces are
    // culled instead of back ones, which pushes the depth stored for a caster
    // to its far side and is the cheapest way to keep a surface from
    // shadow-acneing itself.
    rhi::PipelineHandle shadowPipelineFor(const rhi_renderer::Material& material)
    {
        const uint32_t key = uint32_t(material.cull);
        if (const auto found = shadowPipelines.find(key);
            found != shadowPipelines.end())
            return found->second;
        rhi::PipelineDesc desc;
        desc.vertex = shadowVertex;
        desc.fragment = shadowFragment;
        desc.vertexLayout.bindings.push_back({0, sizeof(MeshVertex), false});
        desc.vertexLayout.attributes.push_back(
            {0, 0, rhi::VertexFormat::Float3, offsetof(MeshVertex, position)});
        desc.vertexLayout.attributes.push_back(
            {1, 0, rhi::VertexFormat::Float3, offsetof(MeshVertex, normal)});
        desc.vertexLayout.attributes.push_back(
            {2, 0, rhi::VertexFormat::Float2, offsetof(MeshVertex, uv)});
        desc.vertexLayout.attributes.push_back(
            {3, 0, rhi::VertexFormat::Float4, offsetof(MeshVertex, colour)});
        desc.cull = material.cull == rhi::CullMode::None
                        ? rhi::CullMode::None
                        : rhi::CullMode::Front;
        desc.depth.testEnabled = true;
        desc.depth.writeEnabled = true;
        desc.depth.compare = rhi::CompareOp::LessEqual;
        desc.colourFormats = {};
        desc.depthFormat = rhi::Format::Depth32Float;
        desc.debugName = "renderer.shadow-pipeline-" + std::to_string(key);
        const rhi::PipelineHandle pipeline = core.device()->createPipeline(desc);
        if (pipeline.valid())
            shadowPipelines[key] = pipeline;
        return pipeline;
    }

    rhi::PipelineHandle
    skinnedShadowPipelineFor(const rhi_renderer::Material& material)
    {
        const uint32_t key = uint32_t(material.cull);
        if (const auto found = skinnedShadowPipelines.find(key);
            found != skinnedShadowPipelines.end())
            return found->second;
        rhi::PipelineDesc desc;
        desc.vertex = skinnedShadowVertex;
        desc.fragment = shadowFragment;
        fillSkinnedVertexLayout(desc.vertexLayout);
        desc.cull = material.cull == rhi::CullMode::None
                        ? rhi::CullMode::None
                        : rhi::CullMode::Front;
        desc.depth.testEnabled = true;
        desc.depth.writeEnabled = true;
        desc.depth.compare = rhi::CompareOp::LessEqual;
        desc.colourFormats = {};
        desc.depthFormat = rhi::Format::Depth32Float;
        desc.debugName =
            "renderer.skinned-shadow-pipeline-" + std::to_string(key);
        const rhi::PipelineHandle pipeline = core.device()->createPipeline(desc);
        if (pipeline.valid())
            skinnedShadowPipelines[key] = pipeline;
        return pipeline;
    }

    rhi::PipelineHandle particlePipelineFor(ParticleBlend blend,
                                            bool withNormalDepth)
    {
        const uint32_t key = uint32_t(blend) | (uint32_t(withNormalDepth) << 1u);
        if (const auto found = particlePipelines.find(key);
            found != particlePipelines.end())
            return found->second;
        rhi::PipelineDesc desc;
        desc.vertex = particleVertexShader;
        desc.fragment = particleFragmentShader;
        desc.vertexLayout.bindings.push_back(
            {0, sizeof(ParticleVertex), false});
        desc.vertexLayout.attributes.push_back(
            {0, 0, rhi::VertexFormat::Float3,
             offsetof(ParticleVertex, position)});
        desc.vertexLayout.attributes.push_back(
            {1, 0, rhi::VertexFormat::Float2, offsetof(ParticleVertex, uv)});
        desc.vertexLayout.attributes.push_back(
            {2, 0, rhi::VertexFormat::Float4, offsetof(ParticleVertex, colour)});
        desc.cull = rhi::CullMode::None;
        desc.blend = blend == ParticleBlend::Additive
                         ? rhi::BlendMode::Additive
                         : rhi::BlendMode::AlphaBlend;
        // Depth-tested against the world but never written: sprites must not
        // occlude each other, which is what the sort below is for instead.
        desc.depth.testEnabled = true;
        desc.depth.writeEnabled = false;
        desc.depth.compare = rhi::CompareOp::LessEqual;
        desc.colourFormats = withNormalDepth
                                 ? std::vector<rhi::Format>{
                                       rhi::Format::RGBA8Unorm,
                                       rhi::Format::RGBA16Float}
                                 : std::vector<rhi::Format>{
                                       rhi::Format::RGBA8Unorm};
        desc.depthFormat = rhi::Format::Depth32Float;
        desc.debugName = "renderer.particle-pipeline-" + std::to_string(key);
        const rhi::PipelineHandle pipeline = core.device()->createPipeline(desc);
        if (pipeline.valid())
            particlePipelines[key] = pipeline;
        return pipeline;
    }

    // xray = draw over everything (the editor's grid should not be buried in
    // the floor it describes); otherwise depth-test so collider outlines
    // occlude behind walls the way the wireframe view does.
    rhi::PipelineHandle debugLinePipelineFor(bool withNormalDepth, bool xray)
    {
        const uint32_t key =
            uint32_t(withNormalDepth) | (uint32_t(xray) << 1u);
        if (const auto found = debugLinePipelines.find(key);
            found != debugLinePipelines.end())
            return found->second;
        rhi::PipelineDesc desc;
        desc.vertex = debugLineVertexShader;
        desc.fragment = debugLineFragmentShader;
        desc.topology = rhi::PrimitiveTopology::LineList;
        desc.vertexLayout.bindings.push_back(
            {0, sizeof(DebugLineVertex), false});
        desc.vertexLayout.attributes.push_back(
            {0, 0, rhi::VertexFormat::Float3,
             offsetof(DebugLineVertex, position)});
        desc.vertexLayout.attributes.push_back(
            {1, 0, rhi::VertexFormat::Float4,
             offsetof(DebugLineVertex, colour)});
        desc.cull = rhi::CullMode::None;
        desc.depth.testEnabled = !xray;
        desc.depth.writeEnabled = false; // a line must not occlude geometry
        desc.depth.compare = rhi::CompareOp::LessEqual;
        desc.colourFormats = withNormalDepth
                                 ? std::vector<rhi::Format>{
                                       rhi::Format::RGBA8Unorm,
                                       rhi::Format::RGBA16Float}
                                 : std::vector<rhi::Format>{
                                       rhi::Format::RGBA8Unorm};
        desc.depthFormat = rhi::Format::Depth32Float;
        desc.debugName = "renderer.debug-line-pipeline-" + std::to_string(key);
        const rhi::PipelineHandle pipeline = core.device()->createPipeline(desc);
        if (pipeline.valid())
            debugLinePipelines[key] = pipeline;
        return pipeline;
    }

    void drawDebugLines(rhi::CommandList& commands, size_t& batches,
                        bool withNormalDepth)
    {
        if (debugLines.empty() || !debugLineVertices.valid())
            return;
        debugLineStaging.clear();
        debugLineStaging.reserve(debugLines.size() * 2u);
        for (const DebugLine& line : debugLines) {
            if (debugLineStaging.size() + 2u > kMaxDebugLineVertices)
                break; // silently clamped; the overflow is always the grid
            debugLineStaging.push_back({line.a, glm::vec4(line.colour, 1.0f)});
            debugLineStaging.push_back({line.b, glm::vec4(line.colour, 1.0f)});
        }
        if (debugLineStaging.empty())
            return;
        const rhi::PipelineHandle pipeline =
            debugLinePipelineFor(withNormalDepth, debugLinesXray);
        if (!pipeline.valid())
            return;
        core.device()->updateBuffer(
            debugLineVertices, debugLineStaging.data(),
            debugLineStaging.size() * sizeof(DebugLineVertex));
        commands.bindPipeline(pipeline);
        commands.bindVertexBuffer(0, debugLineVertices);
        commands.draw(uint32_t(debugLineStaging.size()), 1, 0);
        ++batches;
    }

    void destroyMeshGpu(Mesh& resource)
    {
        if (!core.device())
            return;
        for (GpuSubmesh& submesh : resource.submeshes) {
            if (submesh.indices.valid()) core.device()->destroyBuffer(submesh.indices);
            if (submesh.vertices.valid()) core.device()->destroyBuffer(submesh.vertices);
        }
        resource.submeshes.clear();
    }

    void destroySkinnedMeshGpu(SkinnedMesh& resource)
    {
        if (!core.device())
            return;
        for (SkinnedGpuSubmesh& submesh : resource.submeshes) {
            if (submesh.indices.valid())
                core.device()->destroyBuffer(submesh.indices);
            if (submesh.vertices.valid())
                core.device()->destroyBuffer(submesh.vertices);
        }
        resource.submeshes.clear();
    }

    void destroySkinInstanceGpu(SkinInstance& instance)
    {
        if (core.device())
            for (rhi::BufferHandle buffer : instance.paletteBuffers)
                if (buffer.valid())
                    core.device()->destroyBuffer(buffer);
        instance.paletteBuffers.clear();
    }

    void shutdownGpu()
    {
        if (gpuShutdown || !core.device())
            return;
        for (Mesh& resource : meshes)
            if (resource.alive)
                destroyMeshGpu(resource);
        for (SkinInstance& instance : skinInstances)
            if (instance.alive)
                destroySkinInstanceGpu(instance);
        for (SkinnedMesh& resource : skinnedMeshes)
            if (resource.alive)
                destroySkinnedMeshGpu(resource);
        for (auto& [key, pipeline] : pipelines)
            core.device()->destroyPipeline(pipeline);
        pipelines.clear();
        for (auto& [key, pipeline] : skinnedPipelines)
            core.device()->destroyPipeline(pipeline);
        skinnedPipelines.clear();
        for (rhi::BufferHandle& buffer : sceneUniformBuffers) {
            if (buffer.valid()) core.device()->destroyBuffer(buffer);
            buffer = {};
        }
        for (auto& [key, pipeline] : surfacePipelines)
            core.device()->destroyPipeline(pipeline);
        surfacePipelines.clear();
        for (auto& [name, buffer] : surfaceUniformBuffers)
            core.device()->destroyBuffer(buffer);
        surfaceUniformBuffers.clear();
        if (surfaceFragment.valid()) core.device()->destroyShader(surfaceFragment);
        for (auto& [key, pipeline] : shadowPipelines)
            core.device()->destroyPipeline(pipeline);
        shadowPipelines.clear();
        for (auto& [key, pipeline] : skinnedShadowPipelines)
            core.device()->destroyPipeline(pipeline);
        skinnedShadowPipelines.clear();
        if (skinnedShadowVertex.valid())
            core.device()->destroyShader(skinnedShadowVertex);
        skinnedShadowVertex = {};
        if (shadowFragment.valid()) core.device()->destroyShader(shadowFragment);
        if (shadowVertex.valid()) core.device()->destroyShader(shadowVertex);
        shadowFragment = {};
        shadowVertex = {};
        if (surfaceVertex.valid()) core.device()->destroyShader(surfaceVertex);
        surfaceFragment = {};
        surfaceVertex = {};
        for (auto& [key, pipeline] : particlePipelines)
            core.device()->destroyPipeline(pipeline);
        particlePipelines.clear();
        if (particleIndices.valid()) core.device()->destroyBuffer(particleIndices);
        if (particleVertices.valid()) core.device()->destroyBuffer(particleVertices);
        particleIndices = {};
        particleVertices = {};
        if (particleFragmentShader.valid())
            core.device()->destroyShader(particleFragmentShader);
        if (particleVertexShader.valid())
            core.device()->destroyShader(particleVertexShader);
        particleFragmentShader = {};
        particleVertexShader = {};
        for (auto& [key, pipeline] : debugLinePipelines)
            core.device()->destroyPipeline(pipeline);
        debugLinePipelines.clear();
        if (debugLineVertices.valid())
            core.device()->destroyBuffer(debugLineVertices);
        debugLineVertices = {};
        if (debugLineFragmentShader.valid())
            core.device()->destroyShader(debugLineFragmentShader);
        if (debugLineVertexShader.valid())
            core.device()->destroyShader(debugLineVertexShader);
        debugLineFragmentShader = {};
        debugLineVertexShader = {};
        if (sceneFragment.valid()) core.device()->destroyShader(sceneFragment);
        if (skinnedSceneVertex.valid())
            core.device()->destroyShader(skinnedSceneVertex);
        if (sceneVertex.valid()) core.device()->destroyShader(sceneVertex);
        sceneFragment = {};
        sceneVertex = {};
        skinnedSceneVertex = {};
        gpuShutdown = true;
    }

    MeshHandle uploadMesh(std::string name,
                          const std::vector<std::vector<MeshVertex>>& vertices,
                          const std::vector<std::vector<uint32_t>>& indices,
                          const std::vector<std::string>& sourceMaterials,
                          std::vector<glm::vec3> collisionVertices,
                          std::vector<uint32_t> collisionIndices,
                          ModelImportReport report = {}, bool shared = false)
    {
        if (!core.device() || vertices.empty() || vertices.size() != indices.size())
            return {};
        Mesh resource;
        resource.alive = true;
        resource.shared = shared;
        resource.name = std::move(name);
        resource.collisionVertices = std::move(collisionVertices);
        resource.collisionIndices = std::move(collisionIndices);
        resource.report = std::move(report);
        resource.bounds.min = glm::vec3(std::numeric_limits<float>::infinity());
        resource.bounds.max = glm::vec3(-std::numeric_limits<float>::infinity());
        for (size_t i = 0; i < vertices.size(); ++i) {
            if (vertices[i].empty() || indices[i].empty())
                continue;
            GpuSubmesh submesh;
            rhi::BufferDesc vertexDesc;
            vertexDesc.size = vertices[i].size() * sizeof(MeshVertex);
            vertexDesc.usage = rhi::BufferUsage::Vertex;
            vertexDesc.initialData = vertices[i].data();
            vertexDesc.debugName = resource.name + ".vertices";
            submesh.vertices = core.device()->createBuffer(vertexDesc);
            rhi::BufferDesc indexDesc;
            indexDesc.size = indices[i].size() * sizeof(uint32_t);
            indexDesc.usage = rhi::BufferUsage::Index;
            indexDesc.initialData = indices[i].data();
            indexDesc.debugName = resource.name + ".indices";
            submesh.indices = core.device()->createBuffer(indexDesc);
            submesh.indexCount = uint32_t(indices[i].size());
            if (i < sourceMaterials.size())
                submesh.sourceMaterial = sourceMaterials[i];
            if (!submesh.vertices.valid() || !submesh.indices.valid()) {
                if (submesh.indices.valid()) core.device()->destroyBuffer(submesh.indices);
                if (submesh.vertices.valid()) core.device()->destroyBuffer(submesh.vertices);
                destroyMeshGpu(resource);
                return {};
            }
            for (const MeshVertex& vertex : vertices[i]) {
                resource.bounds.min = glm::min(resource.bounds.min, vertex.position);
                resource.bounds.max = glm::max(resource.bounds.max, vertex.position);
            }
            resource.submeshes.push_back(submesh);
        }
        if (resource.submeshes.empty())
            return {};
        meshes.push_back(std::move(resource));
        return MeshHandle{uint32_t(meshes.size())};
    }

    SkinnedMeshHandle uploadSkinnedMesh(
        std::string name, const detail::ImportedSkinnedModel& imported,
        uint32_t jointCount)
    {
        if (!core.device() || imported.submeshes.empty() || jointCount == 0 ||
            jointCount > kMaxSkinJoints)
            return {};
        SkinnedMesh resource;
        resource.alive = true;
        resource.name = std::move(name);
        resource.jointCount = jointCount;
        resource.bounds = imported.bounds;
        for (const detail::ImportedSkinnedSubmesh& source :
             imported.submeshes) {
            if (source.vertices.empty() || source.indices.empty() ||
                source.inverseBindPoses.size() != jointCount) {
                destroySkinnedMeshGpu(resource);
                return {};
            }
            std::vector<SkinnedMeshVertex> vertices(source.vertices.size());
            for (size_t index = 0; index < source.vertices.size(); ++index) {
                const detail::ImportedSkinnedVertex& input =
                    source.vertices[index];
                SkinnedMeshVertex& output = vertices[index];
                output.position = input.position;
                output.normal = input.normal;
                output.uv = input.texcoord;
                output.colour = input.colour;
                output.joints = input.joints;
                output.weights = input.weights;
            }

            SkinnedGpuSubmesh submesh;
            rhi::BufferDesc vertexDesc;
            vertexDesc.size = vertices.size() * sizeof(SkinnedMeshVertex);
            vertexDesc.usage = rhi::BufferUsage::Vertex;
            vertexDesc.initialData = vertices.data();
            vertexDesc.debugName = resource.name + ".skinned-vertices";
            submesh.vertices = core.device()->createBuffer(vertexDesc);
            rhi::BufferDesc indexDesc;
            indexDesc.size = source.indices.size() * sizeof(uint32_t);
            indexDesc.usage = rhi::BufferUsage::Index;
            indexDesc.initialData = source.indices.data();
            indexDesc.debugName = resource.name + ".skinned-indices";
            submesh.indices = core.device()->createBuffer(indexDesc);
            submesh.indexCount = uint32_t(source.indices.size());
            submesh.sourceMaterial = source.sourceMaterial;
            submesh.inverseBindPoses = source.inverseBindPoses;
            if (!submesh.vertices.valid() || !submesh.indices.valid()) {
                if (submesh.indices.valid())
                    core.device()->destroyBuffer(submesh.indices);
                if (submesh.vertices.valid())
                    core.device()->destroyBuffer(submesh.vertices);
                destroySkinnedMeshGpu(resource);
                return {};
            }
            resource.submeshes.push_back(std::move(submesh));
        }
        skinnedMeshes.push_back(std::move(resource));
        return SkinnedMeshHandle{uint32_t(skinnedMeshes.size())};
    }

    // Not const: the main view hands the fitted light matrix back to the core,
    // which is what tells the shadow pass whether to run at all.
    void fillSceneUniforms(SceneUniforms& uniforms,
                           const RenderCore::View& requested, uint32_t width,
                           uint32_t height)
    {
        glm::vec3 cameraPosition = requested.position;
        glm::quat cameraOrientation = requested.orientation;
        float fov = requested.fovDeg;
        float nearClip = requested.nearClip;
        float farClip = requested.farClip;
        if (requested.target == RenderCore::SceneTarget::Main ||
            requested.target == RenderCore::SceneTarget::Viewmodel) {
            const NodeTransform camera = worldTransform(cameraNode);
            cameraPosition = camera.position;
            cameraOrientation = camera.orientation;
            fov = env.fovDeg;
            nearClip = env.nearClip;
            farClip = env.farClip;
        }
        const glm::mat4 cameraWorld =
            glm::translate(glm::mat4(1.0f), cameraPosition) *
            glm::mat4_cast(cameraOrientation);
        const glm::mat4 view = glm::inverse(cameraWorld);
        const float aspect = float(std::max(width, 1u)) /
                             float(std::max(height, 1u));
        // No clip-space Y flip here: that would reverse screen-space winding
        // and get solid geometry back-face culled. The scene target is righted
        // by the upscale blit instead (see RenderCore's flipV).
        const glm::mat4 projection = glm::perspectiveRH_ZO(
            glm::radians(std::clamp(fov, 1.0f, 179.0f)), aspect,
            std::max(nearClip, 0.001f), std::max(farClip, nearClip + 0.01f));
        uniforms.viewProjection = projection * view;
        uniforms.view = view;
        // z is the vertex-lighting switch: the PS1 and N64 presets ask for it
        // (RenderPresets, perPixel = false) and the scene shaders read it in
        // both stages. Not a separate uniform because clipParams already had
        // two unused lanes and a new binding is a pipeline-layout change.
        uniforms.clipParams = {std::max(nearClip, 0.001f),
                               std::max(farClip, nearClip + 0.01f),
                               env.perPixelLighting ? 0.0f : 1.0f, 0.0f};
        uniforms.psxParams = {std::max(env.precisionMultiplier, 0.001f),
                              std::max(env.lightSteps, 0.0f),
                              std::clamp(env.lightStepSoftness, 0.0f, 0.5f),
                              std::clamp(env.affineAmount, 0.0f, 1.0f)};
        uniforms.psxParams2 = {std::max(env.affineSoftness, 1e-4f), 0.0f, 0.0f,
                               0.0f};
        // w carries fogDesatBoost: the scene shader needs it alongside fog.
        uniforms.ambient = glm::vec4(env.ambient, std::max(env.fogDesatBoost, 0.0f));
        uniforms.fogColourDensity = glm::vec4(env.fogColour, env.fogDensity);

        uint32_t count = 0;
        for (const Light& light : lights) {
            if (!light.alive || count >= 16 || !worldVisible(light.node))
                continue;
            const Node* owner = node(light.node);
            if (!owner)
                continue;
            if (requested.target == RenderCore::SceneTarget::Thumbnail &&
                !owner->thumbnailOnly)
                continue;
            if (requested.target != RenderCore::SceneTarget::Thumbnail &&
                owner->thumbnailOnly)
                continue;
            const NodeTransform transform = worldTransform(light.node);
            if (light.desc.type == LightDesc::Type::Point) {
                uniforms.lightPositionRange[count] =
                    glm::vec4(transform.position, std::max(light.desc.range, 0.001f));
                uniforms.lightColourType[count] =
                    glm::vec4(light.desc.colour, 1.0f);
            } else {
                const glm::vec3 direction =
                    transform.orientation * glm::vec3(0.0f, 0.0f, -1.0f);
                uniforms.lightPositionRange[count] = glm::vec4(direction, 0.0f);
                uniforms.lightColourType[count] =
                    glm::vec4(light.desc.colour, 0.0f);
            }
            ++count;
        }
        uniforms.cameraPositionAndLightCount =
            glm::vec4(cameraPosition, float(count));

        // One orthographic shadow view, fitted to a box around the camera.
        // Ogre capped stencil shadows at 10 m; the same budget spent on a map
        // this size keeps the texel density high enough for a hard edge.
        constexpr float kShadowRange = 14.0f;
        glm::vec3 sunDirection{0.0f};
        for (uint32_t i = 0; i < count; ++i)
            if (uniforms.lightColourType[i].w < 0.5f) {
                // Directional entries store the direction the light TRAVELS --
                // the node's forward axis, as written above and as scene.frag
                // reads it (it negates to get the vector toward the light).
                //
                // This used to take the stored value as "the direction to the
                // light" and place the shadow eye along it, which put the
                // shadow camera on the far side of the scene looking back: for
                // a sun overhead the eye ended up UNDER the floor, and every
                // shadow landed on the ceiling instead of the ground.
                sunDirection = -glm::vec3(uniforms.lightPositionRange[i]);
                break;
            }
        const bool haveSun = glm::dot(sunDirection, sunDirection) > 1e-6f &&
                             shadowsEnabled &&
                             requested.target == RenderCore::SceneTarget::Main;
        if (haveSun) {
            const glm::vec3 direction = glm::normalize(sunDirection);
            // Snap the centre to shadow texels: without it the whole map
            // crawls as the camera moves and every edge shimmers.
            const float texelWorld = 2.0f * kShadowRange / 1024.0f;
            glm::vec3 centre = cameraPosition;
            centre = glm::floor(centre / texelWorld) * texelWorld;
            const glm::vec3 eye = centre + direction * kShadowRange;
            const glm::vec3 up =
                std::abs(direction.y) > 0.95f ? glm::vec3(0.0f, 0.0f, 1.0f)
                                              : glm::vec3(0.0f, 1.0f, 0.0f);
            const glm::mat4 lightView = glm::lookAt(eye, centre, up);
            const glm::mat4 lightProjection =
                glm::orthoRH_ZO(-kShadowRange, kShadowRange, -kShadowRange,
                                kShadowRange, 0.05f, kShadowRange * 2.5f);
            uniforms.lightViewProjection = lightProjection * lightView;
            uniforms.shadowParams = {1.0f, 0.0015f, shadowStrength,
                                     1.0f / 1024.0f};
        } else {
            uniforms.shadowParams = glm::vec4(0.0f);
        }
        if (requested.target == RenderCore::SceneTarget::Main)
            core.setShadowView(haveSun, uniforms.lightViewProjection);
    }

    DrawConstants drawConstants(const Node& nodeRecord,
                                const rhi_renderer::Material& material,
                                const glm::mat4& model) const
    {
        DrawConstants constants;
        constants.model = model;
        const glm::vec4 materialTint = material.modulate();
        constants.tintOpacity = materialTint;

        // The material's own fragment_program_ref defaults come first: they are
        // the authored look, and a node's ShaderParams or an enchantment then
        // overrides them. This used to be skipped entirely, so a material that
        // states `param_named rimColour ...` -- which is how every rim-lit
        // material in the game states it, the crystals included -- rendered
        // with no rim at all.
        const auto materialValue =
            [&](const char* name) -> const rhi_renderer::MaterialValue* {
            const auto found = material.params.find(name);
            return found == material.params.end() ? nullptr : &found->second;
        };
        if (const auto* value = materialValue("rimColour"))
            if (const glm::vec4* rim = std::get_if<glm::vec4>(value))
                constants.rimColourStrength = *rim;
        if (const auto* value = materialValue("rimPower"))
            if (const float* rimPower = std::get_if<float>(value))
                constants.surfaceParams.y = *rimPower;
        if (const auto* value = materialValue("alphaScissor"))
            if (const float* alpha = std::get_if<float>(value))
                constants.surfaceParams.x = *alpha;

        if (nodeRecord.hasShader) {
            constants.tintOpacity *= glm::vec4(nodeRecord.shader.tint,
                                                nodeRecord.shader.opacity);
            constants.rimColourStrength =
                glm::vec4(nodeRecord.shader.rimColour,
                          nodeRecord.shader.rimStrength);
            constants.surfaceParams.x = nodeRecord.shader.alphaScissor;
            constants.surfaceParams.y = nodeRecord.shader.rimPower;
        }
        if (nodeRecord.enchantment) {
            const EnchantmentDesc& enchantment = *nodeRecord.enchantment;
            constants.rimColourStrength =
                glm::vec4(glm::vec3(enchantment.palette.colour),
                          enchantment.palette.colour.a * enchantment.strength);
        }
        const auto blockValue = [&](const char* name) -> const rhi_renderer::MaterialValue* {
            const auto found = nodeRecord.blocks.find(name);
            return found == nodeRecord.blocks.end() ? nullptr : &found->second;
        };
        if (const auto* value = blockValue("modulateColor"))
            if (const glm::vec4* tint = std::get_if<glm::vec4>(value))
                constants.tintOpacity *= *tint;
        if (const auto* value = blockValue("rimColour"))
            if (const glm::vec4* rim = std::get_if<glm::vec4>(value))
                constants.rimColourStrength = *rim;
        if (const auto* value = blockValue("rimPower"))
            if (const float* rimPower = std::get_if<float>(value))
                constants.surfaceParams.y = *rimPower;
        if (const auto* value = blockValue("alphaScissor"))
            if (const float* alpha = std::get_if<float>(value))
                constants.surfaceParams.x = *alpha;
        const glm::vec2 uvScale = material.uvScale();
        const glm::vec2 uvOffset = material.uvOffset();
        constants.uvTransform = {uvScale.x, uvScale.y, uvOffset.x, uvOffset.y};
        return constants;
    }

    void drawMesh(rhi::CommandList& commands, const Node& nodeRecord,
                   const MeshAttachment& attachment, const glm::mat4& model,
                  size_t& batches, size_t& triangles, bool withNormalDepth,
                  bool viewmodelPass = false)
    {
        const Mesh* resource = mesh(attachment.mesh);
        if (!resource)
            return;
        for (size_t index = 0; index < resource->submeshes.size(); ++index) {
            const std::string requested =
                attachment.materials.empty()
                    ? prototype::kSurfaceMaterial
                    : attachment.materials[std::min(index,
                                                    attachment.materials.size() - 1)];
            // Keep the real material: the wireframe view only changes the
            // rasterizer state and the fragment colour. Substituting the
            // prototype surface here (as this used to) drew every line in the
            // placeholder's magenta rather than the flat wire tint.
            const rhi_renderer::Material& material =
                materials.resolve(requested, prototypes.materialFor(requested));
            // A stylised surface (water, slime, lava) is the same geometry
            // through a different fragment stage; everything else about the
            // draw -- vertex layout, blend, depth, culling -- is unchanged.
            const SurfaceMode surfaceMode =
                surfaceModeFor(material.shader);
            const bool isSurface = surfaceMode != SurfaceMode::None;
            const rhi::PipelineHandle pipeline =
                isSurface
                    ? surfacePipelineFor(material, withNormalDepth)
                    : pipelineFor(material,
                                  attachment.renderOnTop && !viewmodelPass,
                                  withNormalDepth);
            if (!pipeline.valid() || !material.texture.valid())
                continue;
            if (isSurface) {
                SurfaceUniforms uniforms;
                const auto number = [&](const char* name, float fallback) {
                    const auto found = material.params.find(name);
                    if (found == material.params.end())
                        return fallback;
                    const float* value = std::get_if<float>(&found->second);
                    return value ? *value : fallback;
                };
                const auto vec2Of = [&](const char* name, glm::vec2 fallback) {
                    const auto found = material.params.find(name);
                    if (found == material.params.end())
                        return fallback;
                    const glm::vec2* value =
                        std::get_if<glm::vec2>(&found->second);
                    return value ? *value : fallback;
                };
                const auto palette = [&](const char* name, glm::vec4 fallback) {
                    const auto found = material.params.find(name);
                    if (found == material.params.end())
                        return fallback;
                    const glm::vec4* value =
                        std::get_if<glm::vec4>(&found->second);
                    return value ? *value : fallback;
                };
                if (surfaceMode == SurfaceMode::Lava) {
                    uniforms.paletteA =
                        palette("lavaDark", {0.12f, 0.004f, 0.001f, 1.0f});
                    uniforms.paletteB =
                        palette("lavaCrust", {0.46f, 0.025f, 0.004f, 1.0f});
                    uniforms.paletteC =
                        palette("lavaHot", {1.65f, 0.36f, 0.018f, 1.0f});
                    uniforms.paletteD =
                        palette("lavaCore", {2.20f, 1.10f, 0.12f, 1.0f});
                    uniforms.tuning = {number("lavaStepFps", 10.0f),
                                       number("lavaPixelGrid", 40.0f), 0.0f,
                                       number("lavaFlowSpeed", 0.22f)};
                } else if (surfaceMode == SurfaceMode::Portal) {
                    uniforms.paletteA =
                        palette("surfaceDark", {0.076f, 0.0f, 0.535f, 1.0f});
                    uniforms.paletteB =
                        palette("surfaceMid", {0.858f, 0.0f, 1.0f, 1.0f});
                    uniforms.paletteC =
                        palette("surfaceBright", {0.540f, 0.0f, 1.0f, 1.0f});
                    uniforms.paletteD =
                        palette("surfaceCore", {0.566f, 0.0f, 1.0f, 1.0f});
                    uniforms.glowColour =
                        palette("surfaceGlowColour", {1.0f, 1.0f, 1.0f, 1.0f});
                    uniforms.tuning = {number("surfaceStepFps", 11.0f),
                                       number("surfacePixelGrid", 39.0f), 0.0f,
                                       0.0f};
                    uniforms.present = {number("surfaceTexelSize", 0.0f),
                                        number("surfaceDither", 0.0f),
                                        number("surfaceBrightness", 1.0f),
                                        number("surfaceGlowStrength", 0.0f)};
                    uniforms.rims = {number("surfaceEdgeGlow", 0.0f),
                                     number("surfaceEdgeFlow", 0.0f),
                                     number("surfaceEdgeMode", 0.0f),
                                     number("surfaceGlowThreshold", 1.0f)};
                    uniforms.motion = {number("portalFlowSpeed", 0.0f),
                                       number("portalSwirlSpeed", 0.0f),
                                       number("portalTwist", 0.0f),
                                       number("portalArms", 1.0f)};
                    uniforms.shapeA = {number("portalArmWidth", 0.5f),
                                       number("portalDepthScale", 1.0f),
                                       number("portalParallax", 0.0f),
                                       number("portalFieldWeight", 0.0f)};
                    uniforms.shapeB = {number("portalCoreRadius", 0.0f),
                                       number("portalCoreBoost", 0.0f),
                                       number("portalRimRadius", 0.5f),
                                       number("portalRimWidth", 0.05f)};
                    uniforms.shapeC = {number("portalRimIntensity", 0.0f),
                                       number("portalEdgeFade", 0.0f), 0.0f,
                                       0.0f};
                } else {
                    uniforms.paletteA =
                        palette("liquidDark", {0.02f, 0.08f, 0.16f, 1.0f});
                    uniforms.paletteB =
                        palette("liquidMid", {0.05f, 0.42f, 0.78f, 1.0f});
                    uniforms.paletteC =
                        palette("liquidBright", {0.28f, 0.90f, 1.0f, 1.0f});
                    const glm::vec2 flowA =
                        vec2Of("liquidFlowA", {0.07f, 0.035f});
                    const glm::vec2 flowB =
                        vec2Of("liquidFlowB", {-0.035f, 0.055f});
                    uniforms.flow = {flowA.x, flowA.y, flowB.x, flowB.y};
                    uniforms.tuning = {number("liquidStepFps", 8.0f),
                                       number("liquidPixelGrid", 32.0f),
                                       number("liquidEmission", 0.0f), 0.0f};
                }
                uniforms.modeTime = {float(surfaceMode), surfaceTime, 0.0f,
                                     0.0f};
                rhi::BufferHandle& buffer =
                    surfaceUniformBuffers[material.name];
                if (!buffer.valid()) {
                    rhi::BufferDesc desc;
                    desc.size = sizeof(SurfaceUniforms);
                    desc.usage = rhi::BufferUsage::Uniform |
                                 rhi::BufferUsage::Dynamic;
                    desc.debugName = "renderer.surface-uniforms";
                    buffer = core.device()->createBuffer(desc);
                }
                if (!buffer.valid())
                    continue;
                core.device()->updateBuffer(buffer, &uniforms,
                                            sizeof(uniforms));
                commands.bindUniformBuffer(1, buffer);
            }
            const GpuSubmesh& submesh = resource->submeshes[index];
            DrawConstants constants = drawConstants(nodeRecord, material, model);
            // Stone keeps its outlines but refuses the highlight wash; the
            // scene shader encodes that as a negative MRT depth.
            constants.surfaceParams.z = material.noHighlight ? 1.0f : 0.0f;
            constants.surfaceParams.w = env.wireframe ? 1.0f : 0.0f;
            commands.bindPipeline(pipeline);
            commands.bindVertexBuffer(0, submesh.vertices);
            commands.bindIndexBuffer(submesh.indices, 0, rhi::IndexType::UInt32);
            commands.bindTexture(0, material.texture.texture,
                                 material.texture.sampler);
            // Slot 1 is the shadow map. It must be bound on every scene draw,
            // not just the shadowed ones: an unbound sampler reads zero, which
            // the depth comparison takes as "fully occluded" and drops the
            // whole frame into shadow.
            // Only once the shadow pass has actually written and released it:
            // the texture exists from init, but until the pass runs it is in
            // an attachment layout that cannot be sampled.
            if (core.shadowTexture().valid())
                commands.bindTexture(1, core.shadowTexture(),
                                     core.shadowSampler());
            commands.pushConstants(&constants, sizeof(constants));
            commands.drawIndexed(submesh.indexCount);
            ++batches;
            triangles += submesh.indexCount / 3;
        }
    }

    void drawSkinnedMesh(rhi::CommandList& commands, const Node& nodeRecord,
                         const SkinInstance& instance, const glm::mat4& model,
                         size_t& batches, size_t& triangles,
                         bool withNormalDepth, bool viewmodelPass = false)
    {
        const SkinnedMesh* resource = skinnedMesh(instance.mesh);
        if (!resource)
            return;
        for (size_t index = 0; index < resource->submeshes.size(); ++index) {
            if (index >= instance.paletteBuffers.size() ||
                !instance.paletteBuffers[index].valid())
                continue;
            const std::string requested =
                instance.materials.empty()
                    ? prototype::kSurfaceMaterial
                    : instance.materials[std::min(
                          index, instance.materials.size() - 1)];
            const rhi_renderer::Material& material = materials.resolve(
                requested, prototypes.materialFor(requested));
            const rhi::PipelineHandle pipeline = skinnedPipelineFor(
                material, instance.renderOnTop && !viewmodelPass,
                withNormalDepth);
            if (!pipeline.valid() || !material.texture.valid())
                continue;
            const SkinnedGpuSubmesh& submesh = resource->submeshes[index];
            DrawConstants constants = drawConstants(nodeRecord, material, model);
            constants.surfaceParams.z = material.noHighlight ? 1.0f : 0.0f;
            constants.surfaceParams.w = env.wireframe ? 1.0f : 0.0f;
            commands.bindPipeline(pipeline);
            commands.bindUniformBuffer(2, instance.paletteBuffers[index]);
            commands.bindVertexBuffer(0, submesh.vertices);
            commands.bindIndexBuffer(submesh.indices, 0,
                                     rhi::IndexType::UInt32);
            commands.bindTexture(0, material.texture.texture,
                                 material.texture.sampler);
            if (core.shadowTexture().valid())
                commands.bindTexture(1, core.shadowTexture(),
                                     core.shadowSampler());
            commands.pushConstants(&constants, sizeof(constants));
            commands.drawIndexed(submesh.indexCount);
            ++batches;
            triangles += submesh.indexCount / 3;
        }
    }

    // The legacy grade/dither knobs are split across two owners: the engine-level
    // EnvState toggles, and the Engine/Psx/DitherPost compositor material that
    // RenderPalette drives through setMaterialParam. Fold both into the post
    // pass inputs. Cheap enough to refresh per frame, which keeps hot-reloaded
    // palette edits live without a change-notification path.
    void syncPostParams()
    {
        const rhi_renderer::Material* post =
            materials.find("Engine/Psx/DitherPost");
        const auto param = [&](const char* name, float fallback) {
            if (!post)
                return fallback;
            const auto found = post->params.find(name);
            if (found == post->params.end())
                return fallback;
            const float* value = std::get_if<float>(&found->second);
            return value ? *value : fallback;
        };
        const auto colourParam = [&](const char* name, glm::vec3 fallback) {
            if (!post)
                return fallback;
            const auto found = post->params.find(name);
            if (found == post->params.end())
                return fallback;
            if (const glm::vec3* v = std::get_if<glm::vec3>(&found->second))
                return *v;
            if (const glm::vec4* v = std::get_if<glm::vec4>(&found->second))
                return glm::vec3(*v);
            return fallback;
        };

        RenderCore::PostParams params;
        params.grade = env.grade;
        params.gradeDesaturate = env.gradeDesaturate;
        params.gradeContrast = env.gradeContrast;
        params.gradeShadowTint = env.gradeShadowTint;
        params.gradeMidTint = env.gradeMidTint;
        params.gradeSaturation = param("gradeSaturation", 1.0f);
        params.gradeTintStrength = param("gradeTintStrength", 0.0f);
        params.gradeBlackLift = param("gradeBlackLift", 0.0f);
        params.vignetteStrength = param("vignetteStrength", 0.0f);
        params.vignetteColour = colourParam("vignetteColor", glm::vec3(1.0f));
        params.dither = env.dither && param("ditherEnabled", 1.0f) > 0.5f;
        params.bloom = env.bloom;
        params.bloomThreshold = env.bloomThreshold;
        params.bloomIntensity = env.bloomIntensity;
        // The one bloom knob the palette drives through a material rather than
        // through EnvState, like the grade/dither block above it.
        params.bloomPixelSnap =
            materials.find("Engine/Psx/BloomComposite")
                ? [&] {
                      const rhi_renderer::Material* composite =
                          materials.find("Engine/Psx/BloomComposite");
                      const auto found =
                          composite->params.find("bloomPixelSnap");
                      if (found == composite->params.end())
                          return 0.0f;
                      const float* value = std::get_if<float>(&found->second);
                      return value ? *value : 0.0f;
                  }()
                : 0.0f;
        params.colourDepth = std::max(param("colDepth", 255.0f), 1.0f);
        params.ditherBanding = param("ditherBanding", 0.0f);
        params.ditherDarkFade = param("ditherDarkFade", 0.0f);
        core.setPostParams(params);
    }

    // Engine/Psx/PixelStylize is driven entirely through setMaterialParam by
    // RenderPresets, so the stylize pass reads its knobs back off that material
    // rather than duplicating them in EnvState.
    void syncStylizeParams()
    {
        const rhi_renderer::Material* stylize =
            materials.find("Engine/Psx/PixelStylize");
        const auto number = [&](const char* name, float fallback) {
            if (!stylize)
                return fallback;
            const auto found = stylize->params.find(name);
            if (found == stylize->params.end())
                return fallback;
            const float* value = std::get_if<float>(&found->second);
            return value ? *value : fallback;
        };
        const auto colour = [&](const char* name, glm::vec3 fallback) {
            if (!stylize)
                return fallback;
            const auto found = stylize->params.find(name);
            if (found == stylize->params.end())
                return fallback;
            if (const glm::vec3* v = std::get_if<glm::vec3>(&found->second))
                return *v;
            if (const glm::vec4* v = std::get_if<glm::vec4>(&found->second))
                return glm::vec3(*v);
            return fallback;
        };

        RenderCore::StylizeParams params;
        params.stylize = number("stylizeEnabled", 0.0f) > 0.5f;
        params.shadows = number("shadowsEnabled", 0.0f) > 0.5f;
        params.highlights = number("highlightsEnabled", 0.0f) > 0.5f;
        params.outlines = number("outlineEnabled", 0.0f) > 0.5f;
        params.shadowStrength = number("shadowStrength", 0.0f);
        params.shadowThreshold = number("shadowThreshold", 0.0f);
        params.shadowColour = colour("shadowColor", glm::vec3(0.0f));
        params.highlightStrength = number("highlightStrength", 0.0f);
        params.highlightThreshold = number("highlightThreshold", 0.0f);
        params.highlightDarkFade = number("highlightDarkFade", 0.0f);
        params.highlightColourOverride =
            number("highlightColorOverride", 0.0f);
        params.highlightColour = colour("highlightColor", glm::vec3(1.0f));
        params.outlineOpacity = number("outlineOpacity", 0.0f);
        params.outlineThickness = number("outlineThickness", 1.0f);
        params.outlineDepthSens = number("outlineDepthSens", 0.0f);
        params.outlineNormalSens = number("outlineNormalSens", 0.0f);
        params.outlineSharpness = number("outlineSharpness", 0.0f);
        params.outlineDistFade = number("outlineDistFade", 0.0f);
        params.outlineDarkFade = number("outlineDarkFade", 0.0f);
        params.outlineColour = colour("outlineColor", glm::vec3(0.0f));
        params.edgeConvexity = number("edgeConvexity", 0.0f);
        params.edgeConvexBias = number("edgeConvexBias", 0.0f);
        params.nearClip = std::max(env.nearClip, 0.001f);
        params.farClip = std::max(env.farClip, env.nearClip + 0.01f);
        core.setStylizeParams(params);
    }

    // Effects name a PNG *stem*; the catalogue turns that into the file plus the
    // blend/flipbook metadata a sheet entry may override. Resolved on first use
    // so a pack of several hundred strips stays off the boot path.
    void resolveParticleEffect(ParticleEffect& effect)
    {
        if (effect.resolved)
            return;
        effect.resolved = true;
        const std::string& stem = effect.desc.texture;
        if (stem.empty()) {
            // No stem: the effect names a hand-authored material instead, which
            // is the older spelling and still what most library effects use
            // ("Engine/Psx/Fire"). The material library already owns its texture
            // and blend, so take both from there rather than making the effect
            // author restate them.
            if (effect.desc.material.empty())
                return;
            const rhi_renderer::Material* material =
                materials.find(effect.desc.material);
            if (!material) {
                warnOnce("particle-material:" + effect.desc.material,
                         "a particle effect names an unknown material");
                return;
            }
            effect.mode = particleModeFor(material->shader);
            effect.blend = material->blend == rhi::BlendMode::Additive
                               ? ParticleBlend::Additive
                               : ParticleBlend::Alpha;
            const auto number = [&](const char* name, float fallback) {
                const auto found = material->params.find(name);
                if (found == material->params.end())
                    return fallback;
                const float* value = std::get_if<float>(&found->second);
                return value ? *value : fallback;
            };
            effect.alphaScissor = number("alphaScissor", 0.0f);
            if (const auto found = material->params.find("atlasGrid");
                found != material->params.end())
                if (const glm::vec2* grid = std::get_if<glm::vec2>(&found->second))
                    effect.atlasGrid = glm::max(*grid, glm::vec2(1.0f));
            effect.atlasRow = number("atlasRow", 0.0f);
            effect.atlasFrames = number("atlasFrames", 1.0f);
            effect.atlasFps = number("atlasFps", 0.0f);
            effect.atlas = effect.mode == ParticleMode::Atlas;
            // The procedural variants generate their whole look in the fragment
            // stage and bind no texture at all, so a missing one is expected;
            // the descriptor still needs something, hence the fallback bind.
            effect.texture = material->texture.valid() ? material->texture
                                                       : core.fallbackTexture();
            if (!effect.texture.valid())
                warnOnce("particle-material:" + effect.desc.material,
                         "a particle material has no bindable texture");
            return;
        }
        const ParticleTextureDesc* desc = particleTextures.find(stem);
        if (!desc) {
            warnOnce("particle-texture:" + stem,
                     "a particle effect names an undeclared texture");
            return;
        }
        effect.blend = desc->blend;
        effect.flipbook = desc->flipbook;
        const std::string path = particleTextures.pathFor(*desc);
        if (path.empty()) {
            warnOnce("particle-file:" + stem,
                     "a particle texture names a PNG that does not exist");
            return;
        }
        effect.texture = core.loadTexture(
            path,
            desc->nearest ? rhi::FilterMode::Nearest : rhi::FilterMode::Linear,
            rhi::AddressMode::ClampToEdge);
    }

    // One draw per effect, matching how the Ogre path batches per material.
    // Alpha effects are sorted back-to-front within their own batch; additive
    // output is order-independent, so sorting it would be pure cost.
    void drawParticles(rhi::CommandList& commands, const RenderCore::View& view,
                       size_t& batches, size_t& triangles, bool withNormalDepth)
    {
        if (particleSim.liveCount() == 0 || particleEffects.empty())
            return;
        glm::quat cameraOrientation = view.orientation;
        glm::vec3 cameraPosition = view.position;
        if (view.target == RenderCore::SceneTarget::Main) {
            const NodeTransform camera = worldTransform(cameraNode);
            cameraPosition = camera.position;
            cameraOrientation = camera.orientation;
        }
        // The camera's world axes billboard the quad without a per-particle
        // matrix, exactly as the legacy vertex program reads them out of the
        // view matrix columns.
        const glm::vec3 right = cameraOrientation * glm::vec3(1.0f, 0.0f, 0.0f);
        const glm::vec3 up = cameraOrientation * glm::vec3(0.0f, 1.0f, 0.0f);

        struct Group {
            RenderCore::TextureBinding texture;
            ParticleBlend blend = ParticleBlend::Alpha;
            ParticleMode mode = ParticleMode::Textured;
            float alphaScissor = 0.0f;
            uint32_t firstQuad = 0;
            uint32_t quads = 0;
        };
        std::vector<Group> groups;

        const ParticlePool& pool = particleSim.pool();
        particleStaging.clear();
        uint32_t quadCount = 0;

        static const glm::vec2 kCorners[4] = {
            {-1.0f, -1.0f}, {1.0f, -1.0f}, {1.0f, 1.0f}, {-1.0f, 1.0f}};
        // v is flipped against the corner sign: image row 0 is the top of the
        // sprite, which is +y in world space.
        static const glm::vec2 kUvs[4] = {
            {0.0f, 1.0f}, {1.0f, 1.0f}, {1.0f, 0.0f}, {0.0f, 0.0f}};

        for (ParticleEffect& effect : particleEffects) {
            resolveParticleEffect(effect);
            const std::vector<uint32_t>& indices =
                particleSim.indicesForEffect(effect.simId);
            if (!effect.texture.valid() || indices.empty())
                continue;

            particleOrder.assign(indices.begin(), indices.end());
            if (effect.blend == ParticleBlend::Alpha) {
                std::sort(particleOrder.begin(), particleOrder.end(),
                          [&](uint32_t a, uint32_t b) {
                              const glm::vec3 da = pool.pos[a] - cameraPosition;
                              const glm::vec3 db = pool.pos[b] - cameraPosition;
                              return glm::dot(da, da) > glm::dot(db, db);
                          });
            }

            Group group;
            group.texture = effect.texture;
            group.blend = effect.blend;
            group.mode = effect.mode;
            group.alphaScissor = effect.alphaScissor;
            group.firstQuad = quadCount;

            const FlipbookDesc& flipbook = effect.flipbook;
            const bool animated = flipbook.active();
            const float frameCount = float(flipbook.frameCount());
            const float perRow = std::max(float(flipbook.framesPerRow()), 1.0f);
            glm::vec2 cell(flipbook.cellU(), flipbook.cellV());
            glm::vec2 origin(flipbook.originU(), flipbook.originV());

            // Atlas materials pick their cell off a wall clock shared by every
            // particle, rather than off each particle's own age. Expressed in
            // the same origin/cell/window form so the corner loop stays common:
            // (corner + window) / grid.
            glm::vec2 atlasWindow(0.0f);
            if (effect.atlas) {
                cell = 1.0f / effect.atlasGrid;
                origin = glm::vec2(0.0f);
                const float count =
                    std::clamp(effect.atlasFrames, 1.0f, effect.atlasGrid.x);
                const float frame =
                    effect.atlasFps > 0.0f
                        ? std::fmod(std::floor(particleTime * effect.atlasFps),
                                    count)
                        : 0.0f;
                atlasWindow = {frame,
                               std::clamp(effect.atlasRow, 0.0f,
                                          effect.atlasGrid.y - 1.0f)};
            }

            for (uint32_t index : particleOrder) {
                if (quadCount >= kMaxDrawnParticles)
                    break;
                // Wall-clock cadence, not normalised life, so a flipbook keeps
                // its authored speed however long the particle lives.
                float frame = 0.0f;
                if (animated) {
                    const float f = pool.age[index] * flipbook.fps;
                    frame = flipbook.loop ? std::fmod(f, frameCount)
                                          : std::min(f, frameCount - 1.0f);
                }
                const float cellIndex = std::floor(std::max(frame, 0.0f));
                const glm::vec2 window =
                    effect.atlas ? atlasWindow
                                 : glm::vec2(std::fmod(cellIndex, perRow),
                                             std::floor(cellIndex / perRow));

                const float angle = pool.rot[index];
                const float sinAngle = std::sin(angle);
                const float cosAngle = std::cos(angle);
                const float half = pool.size[index] * 0.5f;
                const glm::vec3 centre = pool.pos[index];
                const glm::vec4 colour = pool.colour[index];

                // Corner order matches the quad index pattern uploaded once in
                // initializeParticleGpu(): 0-1-2, 0-2-3.
                for (int corner = 0; corner < 4; ++corner) {
                    const glm::vec2 c = kCorners[corner];
                    const glm::vec2 spun(c.x * cosAngle - c.y * sinAngle,
                                         c.x * sinAngle + c.y * cosAngle);
                    ParticleVertex vertex;
                    vertex.position =
                        centre + (right * spun.x + up * spun.y) * half;
                    vertex.uv = origin + (kUvs[corner] + window) * cell;
                    vertex.colour = colour;
                    particleStaging.push_back(vertex);
                }
                ++quadCount;
                ++group.quads;
            }
            if (group.quads)
                groups.push_back(group);
        }

        if (particleStaging.empty())
            return;
        core.device()->updateBuffer(
            particleVertices, particleStaging.data(),
            particleStaging.size() * sizeof(ParticleVertex));

        for (const Group& group : groups) {
            const rhi::PipelineHandle pipeline =
                particlePipelineFor(group.blend, withNormalDepth);
            if (!pipeline.valid())
                continue;
            commands.bindPipeline(pipeline);
            commands.bindVertexBuffer(0, particleVertices);
            commands.bindIndexBuffer(particleIndices, 0, rhi::IndexType::UInt32);
            commands.bindTexture(0, group.texture.texture,
                                 group.texture.sampler);
            ParticleConstants constants;
            constants.modeScissor = {float(group.mode), group.alphaScissor,
                                     0.0f, 0.0f};
            commands.pushConstants(&constants, sizeof(constants));
            commands.drawIndexed(group.quads * 6u, 1, group.firstQuad * 6u);
            ++batches;
            triangles += group.quads * 2u;
        }
    }

    void drawShadowCasters(rhi::CommandList& commands, uint32_t width,
                           uint32_t height)
    {
        SceneUniforms uniforms;
        RenderCore::View mainView;
        fillSceneUniforms(uniforms, mainView, width, height);
        // The pass transforms by lightViewProjection, which fillSceneUniforms
        // has already computed for the main view.
        core.device()->updateBuffer(sceneUniformBuffers[0], &uniforms,
                                    sizeof(uniforms));
        commands.bindUniformBuffer(0, sceneUniformBuffers[0]);

        const auto drawCaster = [&](const Node& nodeRecord,
                                    const MeshAttachment& attachment,
                                    const glm::mat4& model) {
            if (!attachment.castShadows)
                return;
            const Mesh* resource = mesh(attachment.mesh);
            if (!resource)
                return;
            for (size_t i = 0; i < resource->submeshes.size(); ++i) {
                const std::string requested =
                    attachment.materials.empty()
                        ? prototype::kSurfaceMaterial
                        : attachment.materials[std::min(
                              i, attachment.materials.size() - 1)];
                const rhi_renderer::Material& material = materials.resolve(
                    requested, prototypes.materialFor(requested));
                const rhi::PipelineHandle pipeline =
                    shadowPipelineFor(material);
                if (!pipeline.valid())
                    continue;
                const GpuSubmesh& submesh = resource->submeshes[i];
                DrawConstants constants;
                constants.model = model;
                commands.bindPipeline(pipeline);
                commands.bindVertexBuffer(0, submesh.vertices);
                commands.bindIndexBuffer(submesh.indices, 0,
                                         rhi::IndexType::UInt32);
                commands.pushConstants(&constants, sizeof(constants));
                commands.drawIndexed(submesh.indexCount);
            }
        };

        for (size_t index = 0; index < nodes.size(); ++index) {
            const Node& nodeRecord = nodes[index];
            const NodeHandle handle{uint32_t(index + 1)};
            if (!nodeRecord.alive || nodeRecord.thumbnailOnly ||
                !worldVisible(handle))
                continue;
            const glm::mat4 model = worldMatrix(handle);
            for (const MeshAttachment& attachment : nodeRecord.meshes) {
                drawCaster(nodeRecord, attachment, model);
            }
        }
        for (const SkinInstance& instance : skinInstances) {
            if (!instance.alive || !instance.castShadows ||
                !worldVisible(instance.node))
                continue;
            const Node* nodeRecord = node(instance.node);
            const SkinnedMesh* resource = skinnedMesh(instance.mesh);
            if (!nodeRecord || !resource || nodeRecord->thumbnailOnly)
                continue;
            const glm::mat4 model = worldMatrix(instance.node);
            for (size_t i = 0; i < resource->submeshes.size(); ++i) {
                if (i >= instance.paletteBuffers.size() ||
                    !instance.paletteBuffers[i].valid())
                    continue;
                const std::string requested =
                    instance.materials.empty()
                        ? prototype::kSurfaceMaterial
                        : instance.materials[std::min(
                              i, instance.materials.size() - 1)];
                const rhi_renderer::Material& material = materials.resolve(
                    requested, prototypes.materialFor(requested));
                const rhi::PipelineHandle pipeline =
                    skinnedShadowPipelineFor(material);
                if (!pipeline.valid())
                    continue;
                const SkinnedGpuSubmesh& submesh = resource->submeshes[i];
                DrawConstants constants;
                constants.model = model;
                commands.bindPipeline(pipeline);
                commands.bindUniformBuffer(2, instance.paletteBuffers[i]);
                commands.bindVertexBuffer(0, submesh.vertices);
                commands.bindIndexBuffer(submesh.indices, 0,
                                         rhi::IndexType::UInt32);
                commands.pushConstants(&constants, sizeof(constants));
                commands.drawIndexed(submesh.indexCount);
            }
        }
    }

    void draw(rhi::CommandList& commands, const RenderCore::View& view,
              uint32_t width, uint32_t height)
    {
        // Only the main pass carries the MRT metadata surface the stylizer
        // reads; the editor/thumbnail viewports render colour only.
        // Depth-only caster pass: draw the opted-in meshes from the sun and
        // stop. Everything below -- lighting, particles, the on-top set --
        // has nothing to contribute to a depth buffer.
        if (view.target == RenderCore::SceneTarget::Shadow) {
            // Disabled: the pass still runs to clear and to leave the texture
            // sampleable, but there is nothing to put in it.
            if (shadowsEnabled)
                drawShadowCasters(commands, width, height);
            return;
        }
        const bool viewmodelPass =
            view.target == RenderCore::SceneTarget::Viewmodel;
        const bool withNormalDepth =
            view.target == RenderCore::SceneTarget::Main || viewmodelPass;
        if (withNormalDepth) {
            syncPostParams();
            syncStylizeParams();
        }
        SceneUniforms uniforms;
        fillSceneUniforms(uniforms, view, width, height);
        const size_t viewIndex = size_t(view.target);
        core.device()->updateBuffer(sceneUniformBuffers[viewIndex], &uniforms,
                                    sizeof(uniforms));
        commands.bindUniformBuffer(0, sceneUniformBuffers[viewIndex]);

        size_t batches = 0;
        size_t triangles = 0;
        for (size_t index = 0; index < nodes.size(); ++index) {
            const Node& nodeRecord = nodes[index];
            if (!nodeRecord.alive || !worldVisible(NodeHandle{uint32_t(index + 1)}))
                continue;
            if (view.target == RenderCore::SceneTarget::Thumbnail) {
                if (!nodeRecord.thumbnailOnly)
                    continue;
            } else if (nodeRecord.thumbnailOnly) {
                continue;
            }
            const glm::mat4 model = worldMatrix(NodeHandle{uint32_t(index + 1)});
            for (const MeshAttachment& attachment : nodeRecord.meshes) {
                if (attachment.renderOnTop != viewmodelPass)
                    continue;
                drawMesh(commands, nodeRecord, attachment, model, batches,
                         triangles, withNormalDepth, viewmodelPass);
            }
        }
        for (const SkinInstance& instance : skinInstances) {
            if (!instance.alive || !worldVisible(instance.node))
                continue;
            const Node* nodeRecord = node(instance.node);
            if (!nodeRecord)
                continue;
            if (view.target == RenderCore::SceneTarget::Thumbnail) {
                if (!nodeRecord->thumbnailOnly)
                    continue;
            } else if (nodeRecord->thumbnailOnly) {
                continue;
            }
            const glm::mat4 model = worldMatrix(instance.node);
            if (instance.renderOnTop != viewmodelPass)
                continue;
            drawSkinnedMesh(commands, *nodeRecord, instance, model, batches,
                            triangles, withNormalDepth, viewmodelPass);
        }
        if (!viewmodelPass &&
            view.target != RenderCore::SceneTarget::Thumbnail) {
            Node neutralNode;
            neutralNode.alive = true;
            for (const StaticBatch& batch : staticBatches) {
                if (!batch.built || !batch.visible)
                    continue;
                for (const StaticBatch::Record& record : batch.records) {
                    const glm::mat4 model =
                        glm::translate(glm::mat4(1.0f), record.position) *
                        glm::rotate(glm::mat4(1.0f), glm::radians(record.yawDeg),
                                    glm::vec3(0, 1, 0));
                    MeshAttachment attachment;
                    attachment.mesh = record.mesh;
                    attachment.materials = {record.material};
                    drawMesh(commands, neutralNode, attachment, model, batches,
                             triangles, withNormalDepth);
                }
            }
        }
        // Between world geometry and the renderOnTop set, which is where the
        // legacy queues put them: particles are RENDER_QUEUE_MAIN (50), the
        // viewmodel is RENDER_QUEUE_8 (80).
        if (!viewmodelPass)
            drawParticles(commands, view, batches, triangles, withNormalDepth);
        // After the world and the particles, before the viewmodel pass: a
        // diagnostic overlay belongs over the level it describes and under the
        // hands, which is where the legacy queue put it.
        if (!viewmodelPass)
            drawDebugLines(commands, batches, withNormalDepth);
        core.addFrameStats(batches, triangles);
    }
};

Renderer::Renderer() : mImpl(std::make_unique<Impl>()) {}
Renderer::~Renderer() = default;

MeshHandle Renderer::loadMesh(const std::string& path)
{
    return loadMesh(path, ModelImportOptions{});
}

MeshHandle Renderer::loadMesh(const std::string& path, const glm::mat4* bake)
{
    ModelImportOptions options;
    options.pivot = PivotMode::Source;
    detail::ImportedModelData imported;
    ModelImportReport report;
    if (!detail::loadStaticModel(path, options, imported, report)) {
        log::error("RHI renderer: model '%s' failed: %s; using prototype mesh",
                   path.c_str(), report.error.c_str());
        return prototypeMesh(path);
    }
    if (bake) {
        std::string error;
        if (!detail::transformImportedModel(imported, *bake, error)) {
            log::error("RHI renderer: model '%s' transform failed: %s; using prototype mesh",
                       path.c_str(), error.c_str());
            return prototypeMesh(path);
        }
        report.finalBounds = detail::importedModelBounds(imported);
        report.canonicalPivotStandard = false;
    }
    std::vector<std::vector<MeshVertex>> vertices;
    std::vector<std::vector<uint32_t>> indices;
    std::vector<std::string> sourceMaterials;
    for (const detail::ImportedModelSubmesh& source : imported.submeshes) {
        std::vector<MeshVertex> converted;
        converted.reserve(source.vertices.size());
        for (const detail::ImportedModelVertex& vertex : source.vertices)
            converted.push_back({vertex.position, vertex.normal, vertex.texcoord,
                                 vertex.colour});
        vertices.push_back(std::move(converted));
        indices.push_back(source.indices);
        sourceMaterials.push_back(source.sourceMaterial);
    }
    return mImpl->uploadMesh(mImpl->nextName("mesh"), vertices, indices,
                             sourceMaterials,
                             std::move(imported.collisionVertices),
                             std::move(imported.collisionIndices),
                             std::move(report));
}

MeshHandle Renderer::loadMesh(const std::string& path,
                              const ModelImportOptions& rawOptions)
{
    const ModelImportOptions options = sanitizeModelImportOptions(rawOptions);
    detail::ImportedModelData imported;
    ModelImportReport report;
    if (!detail::loadStaticModel(path, options, imported, report)) {
        log::error("RHI renderer: model '%s' failed: %s; using prototype mesh",
                   path.c_str(), report.error.c_str());
        return prototypeMesh(path);
    }
    for (const std::string& warning : report.warnings)
        log::warn("RHI renderer: model '%s': %s", path.c_str(), warning.c_str());
    std::vector<std::vector<MeshVertex>> vertices;
    std::vector<std::vector<uint32_t>> indices;
    std::vector<std::string> sourceMaterials;
    for (const detail::ImportedModelSubmesh& source : imported.submeshes) {
        std::vector<MeshVertex> converted;
        converted.reserve(source.vertices.size());
        for (const detail::ImportedModelVertex& vertex : source.vertices)
            converted.push_back({vertex.position, vertex.normal, vertex.texcoord,
                                 vertex.colour});
        vertices.push_back(std::move(converted));
        indices.push_back(source.indices);
        sourceMaterials.push_back(source.sourceMaterial);
    }
    return mImpl->uploadMesh(mImpl->nextName("model"), vertices, indices,
                             sourceMaterials,
                             std::move(imported.collisionVertices),
                             std::move(imported.collisionIndices),
                             std::move(report));
}

MeshHandle Renderer::loadObj(const std::string& path, const glm::mat4* bake)
{
    return loadMesh(path, bake);
}
MeshHandle Renderer::loadObj(const std::string& path,
                             const ModelImportOptions& options)
{
    return loadMesh(path, options);
}
std::vector<std::string> Renderer::supportedModelExtensions()
{
    return detail::supportedAssimpModelExtensions();
}
bool Renderer::supportsModelFile(const std::string& path)
{
    return detail::assimpSupportsModelFile(path);
}

MeshHandle Renderer::createPrimitiveMesh(const PrimitiveMeshDesc& desc)
{
    const auto geometry = detail::buildPrimitiveGeometry(desc);
    if (!geometry) {
        log::error("RHI renderer: invalid primitive mesh descriptor");
        return {};
    }
    std::vector<MeshVertex> vertices;
    std::vector<glm::vec3> collision;
    vertices.reserve(geometry->vertices.size());
    collision.reserve(geometry->vertices.size());
    for (const detail::PrimitiveVertex& vertex : geometry->vertices) {
        vertices.push_back(
            {vertex.position, vertex.normal, vertex.uv, vertex.colour});
        collision.push_back(vertex.position);
    }
    return mImpl->uploadMesh(
        mImpl->nextName("primitive"), {vertices}, {geometry->indices}, {{}},
        std::move(collision), geometry->indices);
}

void Renderer::setPrototypeCatalog(prototype::PrototypeCatalog catalog)
{
    mImpl->prototypes = std::move(catalog);
    mImpl->prototypeMeshes.clear();
}

MeshHandle Renderer::prototypeMesh(const std::string& assetPath)
{
    const prototype::MeshShape shape = mImpl->prototypes.meshFor(assetPath);
    MeshHandle& handle = mImpl->prototypeMeshes[shape.role];
    if (!handle.valid()) {
        handle = createPrimitiveMesh(shape.desc);
        if (Impl::Mesh* resource = mImpl->mesh(handle))
            resource->shared = true;
    }
    return handle;
}

bool Renderer::meshBounds(MeshHandle handle, MeshBounds& out) const
{
    const Impl::Mesh* resource = mImpl->mesh(handle);
    if (!resource)
        return false;
    out = resource->bounds;
    return finite(out.min) && finite(out.max);
}
size_t Renderer::meshSubmeshCount(MeshHandle handle) const
{
    const Impl::Mesh* resource = mImpl->mesh(handle);
    return resource ? resource->submeshes.size() : 0;
}
bool Renderer::meshImportReport(MeshHandle handle, ModelImportReport& out) const
{
    const Impl::Mesh* resource = mImpl->mesh(handle);
    if (!resource || !resource->report.succeeded())
        return false;
    out = resource->report;
    return true;
}
bool Renderer::meshCollisionGeometry(MeshHandle handle,
                                     std::vector<glm::vec3>& vertices,
                                     std::vector<uint32_t>& indices) const
{
    const Impl::Mesh* resource = mImpl->mesh(handle);
    if (!resource || resource->collisionVertices.empty() ||
        resource->collisionIndices.empty())
        return false;
    vertices = resource->collisionVertices;
    indices = resource->collisionIndices;
    return true;
}
bool Renderer::releaseMesh(MeshHandle handle)
{
    Impl::Mesh* resource = mImpl->mesh(handle);
    if (!resource || resource->shared)
        return false;
    mImpl->destroyMeshGpu(*resource);
    resource->alive = false;
    return true;
}

SkinnedMeshHandle Renderer::loadSkinnedMesh(
    const std::string& path,
    const std::vector<std::string>& skeletonJointNames)
{
    if (skeletonJointNames.size() > kMaxSkinJoints) {
        log::error("RHI renderer: skinned model '%s' has %zu joints; GPU limit is %u",
                   path.c_str(), skeletonJointNames.size(), kMaxSkinJoints);
        return {};
    }
    detail::ImportedSkinnedModel imported;
    std::string error;
    if (!detail::importSkinnedModel(path, skeletonJointNames, imported, error)) {
        log::error("RHI renderer: skinned model '%s' failed: %s", path.c_str(),
                   error.c_str());
        return {};
    }
    return mImpl->uploadSkinnedMesh(mImpl->nextName("skinned-model"), imported,
                                    uint32_t(skeletonJointNames.size()));
}

SkinInstanceHandle Renderer::attachSkinnedMesh(
    NodeHandle node, SkinnedMeshHandle mesh, const std::string& materialName,
    bool castShadows, bool renderOnTop)
{
    Impl::Node* owner = mImpl->node(node, "attachSkinnedMesh");
    const Impl::SkinnedMesh* resource = mImpl->skinnedMesh(mesh);
    if (!owner || !resource || !mImpl->core.device()) {
        log::error("RHI renderer: invalid skinned mesh attachment");
        return {};
    }
    const rhi_renderer::Material& material = mImpl->materials.resolve(
        materialName, mImpl->prototypes.materialFor(materialName));
    // A blank name is not a missing material: on these string overloads it is
    // the caller saying it has no preference, which the scene format documents
    // as legal ("an empty one leaves the entity wearing the renderer's
    // default"). Reporting the documented default as an error is how a console
    // becomes something people scroll past. The submesh overload below still
    // warns on a blank, because there it means an imported mesh named no
    // material, which is worth knowing.
    if (!materialName.empty() && material.name != materialName &&
        mImpl->missingMaterialWarnings.shouldLog(materialName, true))
        log::error("RHI renderer: material '%s' is missing; using '%s'",
                   materialName.c_str(), material.name.c_str());

    const uint64_t paletteBytes =
        uint64_t(kMaxSkinJoints) * sizeof(glm::mat4);
    if (paletteBytes >
        mImpl->core.device()->capabilities().maxUniformBufferRange) {
        log::error("RHI renderer: device uniform range cannot hold skin palette");
        return {};
    }
    std::array<glm::mat4, kMaxSkinJoints> identities;
    identities.fill(glm::mat4(1.0f));
    Impl::SkinInstance instance;
    instance.alive = true;
    instance.node = node;
    instance.mesh = mesh;
    instance.materials.assign(resource->submeshes.size(), material.name);
    instance.castShadows = castShadows;
    instance.renderOnTop = renderOnTop;
    instance.paletteBuffers.reserve(resource->submeshes.size());
    for (size_t index = 0; index < resource->submeshes.size(); ++index) {
        rhi::BufferDesc desc;
        desc.size = paletteBytes;
        desc.usage = rhi::BufferUsage::Uniform | rhi::BufferUsage::Dynamic;
        desc.initialData = identities.data();
        desc.debugName = "renderer.skin-palette";
        const rhi::BufferHandle buffer = mImpl->core.device()->createBuffer(desc);
        if (!buffer.valid()) {
            mImpl->destroySkinInstanceGpu(instance);
            return {};
        }
        instance.paletteBuffers.push_back(buffer);
    }
    mImpl->skinInstances.push_back(std::move(instance));
    mImpl->skinnedPipelineFor(material, renderOnTop);
    return SkinInstanceHandle{uint32_t(mImpl->skinInstances.size())};
}

bool Renderer::setSkinningPose(SkinInstanceHandle handle,
                               std::span<const glm::mat4> modelMatrices)
{
    Impl::SkinInstance* instance = mImpl->skinInstance(handle);
    if (!instance || !mImpl->core.device())
        return false;
    const Impl::SkinnedMesh* resource = mImpl->skinnedMesh(instance->mesh);
    if (!resource || modelMatrices.size() != resource->jointCount ||
        instance->paletteBuffers.size() != resource->submeshes.size())
        return false;
    std::array<glm::mat4, kMaxSkinJoints> palette;
    for (size_t submeshIndex = 0;
         submeshIndex < resource->submeshes.size(); ++submeshIndex) {
        const Impl::SkinnedGpuSubmesh& submesh =
            resource->submeshes[submeshIndex];
        if (submesh.inverseBindPoses.size() != modelMatrices.size())
            return false;
        for (size_t joint = 0; joint < modelMatrices.size(); ++joint) {
            if (!finite(modelMatrices[joint]))
                return false;
            palette[joint] =
                modelMatrices[joint] * submesh.inverseBindPoses[joint];
            if (!finite(palette[joint]))
                return false;
        }
        mImpl->core.device()->updateBuffer(
            instance->paletteBuffers[submeshIndex], palette.data(),
            modelMatrices.size() * sizeof(glm::mat4));
    }
    return true;
}

bool Renderer::releaseSkinnedMesh(SkinnedMeshHandle handle)
{
    Impl::SkinnedMesh* resource = mImpl->skinnedMesh(handle);
    if (!resource)
        return false;
    for (const Impl::SkinInstance& instance : mImpl->skinInstances)
        if (instance.alive && instance.mesh.id == handle.id)
            return false;
    mImpl->destroySkinnedMeshGpu(*resource);
    resource->alive = false;
    return true;
}

NodeHandle Renderer::createNode(NodeHandle parent, glm::vec3 position,
                                const std::string& name)
{
    if (parent.valid() && !mImpl->node(parent, "createNode"))
        return {};
    Impl::Node record;
    record.alive = true;
    record.parent = parent;
    record.local.position = finite(position) ? position : glm::vec3(0.0f);
    mImpl->nodes.push_back(std::move(record));
    const NodeHandle handle{uint32_t(mImpl->nodes.size())};
    if (Impl::Node* owner = mImpl->node(parent, "createNode parent"))
        owner->children.push_back(handle);
    mImpl->sceneRegistry.addNode(
        handle, parent, name.empty() ? mImpl->sceneRegistry.autoName(handle) : name);
    return handle;
}
void Renderer::setPosition(NodeHandle handle, glm::vec3 position)
{
    if (Impl::Node* record = mImpl->node(handle, "setPosition"))
        record->local.position = finite(position) ? position : glm::vec3(0.0f);
}
void Renderer::setOrientation(NodeHandle handle, glm::quat orientation)
{
    if (Impl::Node* record = mImpl->node(handle, "setOrientation")) {
        const float length = glm::dot(orientation, orientation);
        record->local.orientation = finite(orientation) && length > 1e-8f
                                        ? glm::normalize(orientation)
                                        : glm::quat(1, 0, 0, 0);
    }
}
void Renderer::setScale(NodeHandle handle, glm::vec3 scale)
{
    if (Impl::Node* record = mImpl->node(handle, "setScale"))
        record->local.scale = finite(scale) ? scale : glm::vec3(1.0f);
}
bool Renderer::nodeWorldTransform(NodeHandle handle, NodeTransform& out) const
{
    if (!mImpl->node(handle))
        return false;
    out = mImpl->worldTransform(handle);
    return finite(out.position) && finite(out.scale) && finite(out.orientation);
}
void Renderer::setNodeVisible(NodeHandle handle, bool show)
{
    if (Impl::Node* record = mImpl->node(handle, "setNodeVisible"))
        record->visible = show;
}

void Renderer::setNodeMaterial(NodeHandle handle,
                               const std::string& materialName)
{
    Impl::Node* record = mImpl->node(handle, "setNodeMaterial");
    if (!record)
        return;
    const std::string fallback = mImpl->prototypes.materialFor(materialName);
    const std::string resolved = mImpl->materials.find(materialName)
                                     ? materialName
                                     : mImpl->materials.find(fallback)
                                           ? fallback
                                           : prototype::kSurfaceMaterial;
    if (!materialName.empty() && resolved != materialName &&
        mImpl->missingMaterialWarnings.shouldLog(materialName, true))
        log::error("RHI renderer: material '%s' is missing; using '%s'",
                   materialName.c_str(), resolved.c_str());
    for (Impl::MeshAttachment& attachment : record->meshes)
        attachment.materials.assign(
            std::max<size_t>(1, mImpl->mesh(attachment.mesh)
                                    ? mImpl->mesh(attachment.mesh)->submeshes.size()
                                    : 1),
            resolved);
    for (Impl::SkinInstance& instance : mImpl->skinInstances)
        if (instance.alive && instance.node.id == handle.id)
            instance.materials.assign(
                std::max<size_t>(
                    1, mImpl->skinnedMesh(instance.mesh)
                           ? mImpl->skinnedMesh(instance.mesh)->submeshes.size()
                           : 1),
                resolved);
    mImpl->sceneRegistry.setMeshMaterial(handle, resolved);
}

void Renderer::setNodeEnchantment(NodeHandle handle,
                                  const EnchantmentDesc& desc)
{
    const EnchantmentDesc clean = sanitizeEnchantmentDesc(desc);
    Impl::Node* root = mImpl->node(handle, "setNodeEnchantment");
    if (!root)
        return;
    std::vector<NodeHandle> targets{handle};
    if (clean.recursive)
        for (size_t i = 0; i < targets.size(); ++i)
            if (const Impl::Node* current = mImpl->node(targets[i]))
                targets.insert(targets.end(), current->children.begin(),
                               current->children.end());
    for (NodeHandle target : targets)
        if (Impl::Node* current = mImpl->node(target, "setNodeEnchantment"))
            current->enchantment = clean.strength > 0.0f
                                       ? std::optional<EnchantmentDesc>(clean)
                                       : std::nullopt;
    mImpl->warnOnce("enchantment-detail", "enchantment rune overlay (rim tint is applied)");
}
void Renderer::setNodeEnchantment(NodeHandle handle,
                                  const EnchantmentPalette& palette,
                                  float strength)
{
    EnchantmentDesc desc;
    desc.palette = palette;
    desc.strength = strength;
    setNodeEnchantment(handle, desc);
}
void Renderer::clearNodeEnchantment(NodeHandle handle)
{
    Impl::Node* root = mImpl->node(handle, "clearNodeEnchantment");
    if (!root)
        return;
    std::vector<NodeHandle> targets{handle};
    for (size_t i = 0; i < targets.size(); ++i)
        if (const Impl::Node* current = mImpl->node(targets[i]))
            targets.insert(targets.end(), current->children.begin(),
                           current->children.end());
    for (NodeHandle target : targets)
        if (Impl::Node* current = mImpl->node(target, "clearNodeEnchantment"))
            current->enchantment.reset();
}

std::vector<std::string> Renderer::materialNames() const
{
    return mImpl->materials.names();
}
bool Renderer::materialAvailable(const std::string& name) const
{
    return !name.empty() && mImpl->materials.find(name);
}

bool Renderer::nodeWorldBounds(NodeHandle handle, glm::vec3& center,
                               float& radius) const
{
    if (!mImpl->node(handle))
        return false;
    glm::vec3 minimum(std::numeric_limits<float>::infinity());
    glm::vec3 maximum(-std::numeric_limits<float>::infinity());
    bool found = false;
    std::vector<NodeHandle> targets{handle};
    for (size_t i = 0; i < targets.size(); ++i) {
        const Impl::Node* node = mImpl->node(targets[i]);
        if (!node)
            continue;
        targets.insert(targets.end(), node->children.begin(), node->children.end());
        const glm::mat4 world = mImpl->worldMatrix(targets[i]);
        for (const Impl::MeshAttachment& attachment : node->meshes) {
            const Impl::Mesh* mesh = mImpl->mesh(attachment.mesh);
            if (!mesh)
                continue;
            for (int x = 0; x < 2; ++x)
                for (int y = 0; y < 2; ++y)
                    for (int z = 0; z < 2; ++z) {
                        const glm::vec3 corner{
                            x ? mesh->bounds.max.x : mesh->bounds.min.x,
                            y ? mesh->bounds.max.y : mesh->bounds.min.y,
                            z ? mesh->bounds.max.z : mesh->bounds.min.z};
                        const glm::vec3 point = glm::vec3(world * glm::vec4(corner, 1));
                        minimum = glm::min(minimum, point);
                        maximum = glm::max(maximum, point);
                        found = true;
                    }
        }
        for (const Impl::SkinInstance& instance : mImpl->skinInstances) {
            if (!instance.alive || instance.node.id != targets[i].id)
                continue;
            const Impl::SkinnedMesh* mesh = mImpl->skinnedMesh(instance.mesh);
            if (!mesh)
                continue;
            for (int x = 0; x < 2; ++x)
                for (int y = 0; y < 2; ++y)
                    for (int z = 0; z < 2; ++z) {
                        const glm::vec3 corner{
                            x ? mesh->bounds.max.x : mesh->bounds.min.x,
                            y ? mesh->bounds.max.y : mesh->bounds.min.y,
                            z ? mesh->bounds.max.z : mesh->bounds.min.z};
                        const glm::vec3 point =
                            glm::vec3(world * glm::vec4(corner, 1.0f));
                        minimum = glm::min(minimum, point);
                        maximum = glm::max(maximum, point);
                        found = true;
                    }
        }
    }
    if (!found)
        return false;
    center = (minimum + maximum) * 0.5f;
    radius = std::max(0.05f, glm::length(maximum - center));
    return true;
}

void Renderer::destroyNode(NodeHandle handle)
{
    Impl::Node* root = mImpl->node(handle, "destroyNode");
    if (!root || handle.id == kRootNode.id)
        return;
    // Unlink from the parent BEFORE walking the subtree.
    //
    // Without this the parent kept a handle to a node it no longer owns, and
    // destroying the parent later walked into that dead entry and reported an
    // invalid handle -- once per orphaned child, which on a level teardown is a
    // screenful of errors describing nothing a caller can act on. Worse, it
    // trained everyone to ignore the one channel that reports real handle
    // misuse.
    //
    // SceneRegistry::removeNode has always unlinked its own copy of the graph;
    // this is the renderer's node vector catching up with it.
    // const_cast rather than the logging overload: a node whose parent is
    // already gone is the ordinary case when a subtree unwinds top-down, and
    // reporting it would reintroduce exactly the noise this removes.
    if (auto* owner = const_cast<Impl::Node*>(
            static_cast<const Impl*>(mImpl.get())->node(root->parent))) {
        auto& kids = owner->children;
        kids.erase(std::remove_if(kids.begin(), kids.end(),
                                  [&](NodeHandle h) { return h.id == handle.id; }),
                   kids.end());
    }
    std::vector<NodeHandle> targets{handle};
    for (size_t i = 0; i < targets.size(); ++i)
        if (const Impl::Node* current = mImpl->node(targets[i]))
            targets.insert(targets.end(), current->children.begin(),
                           current->children.end());
    for (auto it = targets.rbegin(); it != targets.rend(); ++it) {
        Impl::Node* current = mImpl->node(*it, "destroyNode subtree");
        if (!current)
            continue;
        for (auto live = mImpl->liveParticles.begin();
             live != mImpl->liveParticles.end();) {
            if (live->second.followsNode && live->second.parent.id == it->id) {
                mImpl->particleSim.killInstance(live->second.instance);
                live = mImpl->liveParticles.erase(live);
            } else {
                ++live;
            }
        }
        for (Impl::Light& light : mImpl->lights)
            if (light.alive && light.node.id == it->id)
                light.alive = false;
        for (Impl::SkinInstance& instance : mImpl->skinInstances)
            if (instance.alive && instance.node.id == it->id) {
                mImpl->destroySkinInstanceGpu(instance);
                instance.alive = false;
            }
        current->alive = false;
        current->meshes.clear();
        current->children.clear();
    }
    mImpl->sceneRegistry.removeNode(handle);
    if (mImpl->cameraNode.id == handle.id)
        mImpl->cameraNode = kRootNode;
}

void Renderer::attachMesh(NodeHandle node, MeshHandle mesh,
                          const std::string& materialName, bool castShadows,
                          bool renderOnTop)
{
    attachMesh(node, mesh, materialName,
               mImpl->prototypes.materialFor(materialName), castShadows,
               renderOnTop);
}
void Renderer::attachMesh(NodeHandle node, MeshHandle mesh,
                          const std::string& materialName,
                          const std::string& fallbackMaterial,
                          bool castShadows, bool renderOnTop)
{
    Impl::Node* owner = mImpl->node(node, "attachMesh");
    const Impl::Mesh* resource = mImpl->mesh(mesh);
    if (!owner || !resource) {
        log::error("RHI renderer: invalid mesh handle %u in attachMesh", mesh.id);
        return;
    }
    const rhi_renderer::Material& material =
        mImpl->materials.resolve(materialName, fallbackMaterial);
    // A blank name is not a missing material: on these string overloads it is
    // the caller saying it has no preference, which the scene format documents
    // as legal ("an empty one leaves the entity wearing the renderer's
    // default"). Reporting the documented default as an error is how a console
    // becomes something people scroll past. The submesh overload below still
    // warns on a blank, because there it means an imported mesh named no
    // material, which is worth knowing.
    if (!materialName.empty() && material.name != materialName &&
        mImpl->missingMaterialWarnings.shouldLog(materialName, true))
        log::error("RHI renderer: material '%s' is missing; using '%s'",
                   materialName.c_str(), material.name.c_str());
    Impl::MeshAttachment attachment;
    attachment.mesh = mesh;
    attachment.materials.assign(resource->submeshes.size(), material.name);
    attachment.castShadows = castShadows;
    attachment.renderOnTop = renderOnTop;
    owner->meshes.push_back(std::move(attachment));
    mImpl->pipelineFor(material, renderOnTop);
    mImpl->sceneRegistry.addAttachment(
        node, {NodeAttachKind::Mesh, 0, material.name});
}
void Renderer::attachMesh(NodeHandle node, MeshHandle mesh,
                          const ResolvedModelMaterial& material,
                          bool castShadows, bool renderOnTop)
{
    attachMesh(node, mesh, std::vector<ResolvedModelMaterial>{material},
               castShadows, renderOnTop);
}
void Renderer::attachMesh(NodeHandle node, MeshHandle mesh,
                          const std::vector<ResolvedModelMaterial>& supplied,
                          bool castShadows, bool renderOnTop)
{
    Impl::Node* owner = mImpl->node(node, "attachMesh submeshes");
    const Impl::Mesh* resource = mImpl->mesh(mesh);
    if (!owner || !resource || supplied.empty()) {
        log::error("RHI renderer: invalid submesh attachment");
        return;
    }
    Impl::MeshAttachment attachment;
    attachment.mesh = mesh;
    attachment.castShadows = castShadows;
    attachment.renderOnTop = renderOnTop;
    std::string label;
    for (size_t i = 0; i < resource->submeshes.size(); ++i) {
        const ResolvedModelMaterial& requested =
            supplied[std::min(i, supplied.size() - 1)];
        const rhi_renderer::Material& material = mImpl->materials.resolve(
            requested.material, mImpl->prototypes.materialFor(requested.requested));
        if (material.name != requested.material &&
            mImpl->missingMaterialWarnings.shouldLog(requested.requested, true))
            log::error("RHI renderer: material '%s' is missing; using '%s'",
                       requested.requested.c_str(), material.name.c_str());
        attachment.materials.push_back(material.name);
        mImpl->pipelineFor(material, renderOnTop);
        if (i == 0)
            label = material.name;
        else if (label != material.name)
            label = "<mixed>";
    }
    owner->meshes.push_back(std::move(attachment));
    mImpl->sceneRegistry.addAttachment(node, {NodeAttachKind::Mesh, 0, label});
}

std::string Renderer::createSpriteMaterial(const SpriteClip&)
{
    mImpl->warnOnce("sprites", "world sprites");
    return mImpl->nextName("rhi_sprite_material");
}
SpriteHandle Renderer::attachSprite(NodeHandle node, const SpriteClip& clip)
{
    if (!mImpl->node(node, "attachSprite"))
        return {};
    mImpl->sprites.push_back({true, node, clip, true});
    const SpriteHandle handle{uint32_t(mImpl->sprites.size())};
    mImpl->sceneRegistry.addAttachment(
        node, {NodeAttachKind::Sprite, handle.id, clip.texture});
    mImpl->warnOnce("sprites", "world sprites");
    return handle;
}
SpriteHandle Renderer::attachTextSprite(NodeHandle node, const std::string&,
                                        const TextSpriteStyle&)
{
    SpriteClip clip;
    return attachSprite(node, clip);
}
void Renderer::setSpriteVisible(SpriteHandle handle, bool visible)
{
    if (!handle.valid() || handle.id > mImpl->sprites.size() ||
        !mImpl->sprites[handle.id - 1].alive) {
        log::error("RHI renderer: invalid sprite handle %u", handle.id);
        return;
    }
    mImpl->sprites[handle.id - 1].visible = visible;
}

StaticBatchHandle Renderer::createStaticBatch(glm::vec3 regionSize)
{
    Impl::StaticBatch batch;
    batch.regionSize = regionSize;
    mImpl->staticBatches.push_back(std::move(batch));
    return {uint32_t(mImpl->staticBatches.size())};
}
void Renderer::addToStaticBatch(StaticBatchHandle handle, MeshHandle mesh,
                                const std::string& materialName, glm::vec3 pos,
                                float yawDeg)
{
    if (!handle.valid() || handle.id > mImpl->staticBatches.size() ||
        !mImpl->mesh(mesh)) {
        log::error("RHI renderer: invalid static batch record");
        return;
    }
    const rhi_renderer::Material& material = mImpl->materials.resolve(
        materialName, mImpl->prototypes.materialFor(materialName));
    mImpl->staticBatches[handle.id - 1].records.push_back(
        {mesh, material.name, pos, yawDeg});
}
void Renderer::buildStaticBatch(StaticBatchHandle handle)
{
    if (!handle.valid() || handle.id > mImpl->staticBatches.size()) {
        log::error("RHI renderer: invalid static batch handle %u", handle.id);
        return;
    }
    mImpl->staticBatches[handle.id - 1].built = true;
}
void Renderer::setStaticBatchVisible(StaticBatchHandle handle, bool visible)
{
    if (!handle.valid() || handle.id > mImpl->staticBatches.size()) {
        log::error("RHI renderer: invalid static batch handle %u", handle.id);
        return;
    }
    mImpl->staticBatches[handle.id - 1].visible = visible;
}

void Renderer::clearScene()
{
    const size_t oldNodes = mImpl->nodes.size() > 0 ? mImpl->nodes.size() - 1 : 0;
    for (Impl::Mesh& mesh : mImpl->meshes)
        if (mesh.alive)
            mImpl->destroyMeshGpu(mesh);
    for (Impl::SkinInstance& instance : mImpl->skinInstances)
        if (instance.alive)
            mImpl->destroySkinInstanceGpu(instance);
    for (Impl::SkinnedMesh& mesh : mImpl->skinnedMeshes)
        if (mesh.alive)
            mImpl->destroySkinnedMeshGpu(mesh);
    mImpl->meshes.clear();
    mImpl->skinInstances.clear();
    mImpl->skinnedMeshes.clear();
    mImpl->nodes.clear();
    mImpl->lights.clear();
    mImpl->sprites.clear();
    mImpl->staticBatches.clear();
    mImpl->prototypeMeshes.clear();
    mImpl->sceneRegistry.clear();
    mImpl->particleSim.clear();
    mImpl->liveParticles.clear();
    mImpl->decals.clear();
    Impl::Node root;
    root.alive = true;
    mImpl->nodes.push_back(root);
    mImpl->cameraNode = kRootNode;
    log::info("Scene: cleared %zu nodes", oldNodes);
}

ParticleEffectId Renderer::registerParticleEffect(const ParticleEffectDesc& raw)
{
    if (raw.name.empty() || raw.emitters.empty()) {
        log::error("RHI renderer: particle effect requires a name and emitter");
        return {};
    }
    ParticleEffectDesc desc = sanitizeParticleEffect(raw);
    if (const auto found = mImpl->particleByName.find(desc.name);
        found != mImpl->particleByName.end()) {
        Impl::ParticleEffect& effect =
            mImpl->particleEffects[found->second - 1];
        mImpl->particleSim.updateEffect(effect.simId, desc);
        // Hot reload may have repointed the effect at a different texture, so
        // drop the cached binding and let the next frame resolve it again.
        const bool textureChanged = effect.desc.texture != desc.texture;
        effect.desc = std::move(desc);
        if (textureChanged) {
            effect.resolved = false;
            effect.texture = {};
        }
        return ParticleEffectId{found->second};
    }
    const uint16_t simId = mImpl->particleSim.registerEffect(desc);
    mImpl->particleEffects.push_back({std::move(desc), simId});
    const uint32_t id = uint32_t(mImpl->particleEffects.size());
    mImpl->particleByName[mImpl->particleEffects.back().desc.name] = id;
    return ParticleEffectId{id};
}
ParticleEffectId Renderer::particleEffectId(const std::string& name)
{
    const auto found = mImpl->particleByName.find(name);
    if (found == mImpl->particleByName.end()) {
        if (mImpl->warned.insert("particle-name:" + name).second)
            log::warn("RHI renderer: no particle effect named '%s'", name.c_str());
        return {};
    }
    return {found->second};
}

ParticlesHandle Renderer::spawnParticles(const std::string& name,
                                         NodeHandle parent, glm::vec3 localPos)
{
    return spawnParticles(particleEffectId(name), parent, localPos, {});
}
ParticlesHandle Renderer::spawnParticles(const std::string& name,
                                         NodeHandle parent,
                                         const ParticleSpawnOptions& options)
{
    return spawnParticles(particleEffectId(name), parent, glm::vec3(0), options);
}
ParticlesHandle Renderer::spawnParticles(const std::string& name,
                                         NodeHandle parent, glm::vec3 localPos,
                                         const ParticleSpawnOptions& options)
{
    return spawnParticles(particleEffectId(name), parent, localPos, options);
}
ParticlesHandle Renderer::spawnParticles(const std::string& name,
                                         glm::vec3 worldPos)
{
    return spawnParticles(particleEffectId(name), worldPos, {});
}
ParticlesHandle Renderer::spawnParticles(const std::string& name,
                                         glm::vec3 worldPos,
                                         const ParticleSpawnOptions& options)
{
    return spawnParticles(particleEffectId(name), worldPos, options);
}
ParticlesHandle Renderer::spawnParticles(ParticleEffectId effect,
                                         NodeHandle parent, glm::vec3 localPos)
{
    return spawnParticles(effect, parent, localPos, {});
}
ParticlesHandle Renderer::spawnParticles(ParticleEffectId effect,
                                         NodeHandle parent,
                                         const ParticleSpawnOptions& options)
{
    return spawnParticles(effect, parent, glm::vec3(0), options);
}
ParticlesHandle Renderer::spawnParticles(ParticleEffectId effect,
                                         NodeHandle parent, glm::vec3 localPos,
                                         const ParticleSpawnOptions& options)
{
    if (!effect.valid() || effect.id > mImpl->particleEffects.size() ||
        !mImpl->node(parent, "spawnParticles"))
        return {};
    const Impl::ParticleEffect& registered =
        mImpl->particleEffects[effect.id - 1];
    const float quality = particleQualityScale(
        registered.desc.visualRole, registered.desc.qualityWeight,
        mImpl->particleQuality);
    const ResolvedParticleSpawn resolved = resolveParticleSpawn(
        registered.desc, options, localPos, quality);
    const uint32_t handle = mImpl->nextParticleHandle++;
    const uint32_t instance = mImpl->particleSim.addInstance(
        registered.simId, resolved, mImpl->worldMatrix(parent),
        handle * 2654435761u + 1u);
    if (instance == ParticleSim::kInvalidInstance)
        return {};
    mImpl->liveParticles[handle] = {instance, parent, glm::mat4(1), true};
    mImpl->sceneRegistry.addAttachment(
        parent, {NodeAttachKind::Particles, handle, registered.desc.name});
    return {handle};
}
ParticlesHandle Renderer::spawnParticles(ParticleEffectId effect,
                                         glm::vec3 worldPos)
{
    return spawnParticles(effect, worldPos, {});
}
ParticlesHandle Renderer::spawnParticles(ParticleEffectId effect,
                                         glm::vec3 worldPos,
                                         const ParticleSpawnOptions& options)
{
    if (!effect.valid() || effect.id > mImpl->particleEffects.size())
        return {};
    const Impl::ParticleEffect& registered =
        mImpl->particleEffects[effect.id - 1];
    const float quality = particleQualityScale(
        registered.desc.visualRole, registered.desc.qualityWeight,
        mImpl->particleQuality);
    const ResolvedParticleSpawn resolved = resolveParticleSpawn(
        registered.desc, options, glm::vec3(0), quality);
    const uint32_t handle = mImpl->nextParticleHandle++;
    const glm::mat4 transform = glm::translate(glm::mat4(1), worldPos);
    const uint32_t instance = mImpl->particleSim.addInstance(
        registered.simId, resolved, transform, handle * 2654435761u + 1u);
    if (instance == ParticleSim::kInvalidInstance)
        return {};
    mImpl->liveParticles[handle] = {instance, {}, transform, false};
    return {handle};
}
void Renderer::stopParticles(ParticlesHandle handle)
{
    const auto found = mImpl->liveParticles.find(handle.id);
    if (found != mImpl->liveParticles.end())
        mImpl->particleSim.stopInstance(found->second.instance);
}
void Renderer::despawnParticles(ParticlesHandle handle)
{
    const auto found = mImpl->liveParticles.find(handle.id);
    if (found == mImpl->liveParticles.end())
        return;
    mImpl->particleSim.killInstance(found->second.instance);
    mImpl->liveParticles.erase(found);
    mImpl->sceneRegistry.removeAttachment(NodeAttachKind::Particles, handle.id);
}
void Renderer::setParticleQuality(float quality)
{
    mImpl->particleQuality = std::clamp(quality, 0.25f, 1.0f);
}
const std::vector<ParticleTextureDesc>& Renderer::particleTextures() const
{
    // Was a permanently empty vector, so the editor's texture list showed
    // nothing; the catalogue is the real, stably-sorted answer.
    return mImpl->particleTextures.all();
}
bool Renderer::reloadParticleTextures()
{
    if (!mImpl->particleTextures.reload())
        return false;
    // Re-resolve every effect against the rescanned catalogue: a blend mode or
    // flipbook window may have changed even when the file did not. The texture
    // cache is keyed by path, so unchanged PNGs are not re-uploaded.
    for (Impl::ParticleEffect& effect : mImpl->particleEffects) {
        effect.resolved = false;
        effect.texture = {};
    }
    return true;
}
uint32_t Renderer::liveParticleCount() const
{
    return mImpl->particleSim.liveCount();
}
void Renderer::shutdownParticles()
{
    mImpl->particleSim.clear();
    mImpl->liveParticles.clear();
    mImpl->decals.shutdown();
}
void Renderer::setParticleCollider(IParticleCollider* collider)
{
    mImpl->particleCollider = collider;
    mImpl->particleSim.setCollider(collider);
}
void Renderer::setParticleRayBudget(uint32_t budget)
{
    mImpl->particleSim.setRayBudget(budget);
}
void Renderer::registerDecalProfile(const std::string& id,
                                    const DecalProfileDesc& desc)
{
    mImpl->decals.registerProfile(id, desc);
}
bool Renderer::spawnDecal(const std::string& profile, glm::vec3 position,
                          glm::vec3 normal)
{
    const bool spawned =
        mImpl->decals.spawn(DecalRequest{profile, position, normal});
    if (spawned)
        mImpl->warnOnce("decals", "decal batches");
    return spawned;
}
void Renderer::updateParticles(float dt)
{
    for (auto& [handle, live] : mImpl->liveParticles) {
        if (live.followsNode && mImpl->node(live.parent))
            mImpl->particleSim.setInstanceTransform(
                live.instance, mImpl->worldMatrix(live.parent));
    }
    mImpl->particleTime += dt;
    mImpl->surfaceTime = mImpl->particleTime;
    mImpl->decalRequests.clear();
    mImpl->particleSim.update(dt, mImpl->decalRequests);
    for (const DecalRequest& request : mImpl->decalRequests)
        mImpl->decals.spawn(request);
    mImpl->decals.update(dt);
    std::vector<uint32_t> retired;
    for (const auto& [handle, live] : mImpl->liveParticles)
        if (!mImpl->particleSim.instanceActive(live.instance))
            retired.push_back(handle);
    for (uint32_t handle : retired) {
        mImpl->liveParticles.erase(handle);
        mImpl->sceneRegistry.removeAttachment(NodeAttachKind::Particles, handle);
    }
}

void Renderer::attachCamera(NodeHandle node)
{
    if (mImpl->node(node, "attachCamera"))
        mImpl->cameraNode = node;
}
LightHandle Renderer::attachLight(NodeHandle node, const LightDesc& desc)
{
    if (!mImpl->node(node, "attachLight"))
        return {};
    mImpl->lights.push_back({true, node, desc});
    const LightHandle handle{uint32_t(mImpl->lights.size())};
    mImpl->sceneRegistry.addAttachment(
        node, {NodeAttachKind::Light, handle.id, ""});
    if (desc.castShadows)

    return handle;
}
void Renderer::setLightColour(LightHandle handle, glm::vec3 colour)
{
    if (!handle.valid() || handle.id > mImpl->lights.size() ||
        !mImpl->lights[handle.id - 1].alive) {
        log::error("RHI renderer: invalid light handle %u", handle.id);
        return;
    }
    mImpl->lights[handle.id - 1].desc.colour = colour;
}
void Renderer::setLightRange(LightHandle handle, float range)
{
    if (handle.valid() && handle.id <= mImpl->lights.size() &&
        mImpl->lights[handle.id - 1].alive)
        mImpl->lights[handle.id - 1].desc.range = std::max(range, 0.001f);
}

void Renderer::setCameraFov(float degrees)
{
    mImpl->env.fovDeg = std::clamp(degrees, 1.0f, 179.0f);
}
void Renderer::setCameraClip(float nearDist, float farDist)
{
    mImpl->env.nearClip = std::clamp(nearDist, 0.001f, 10.0f);
    mImpl->env.farClip = std::max(farDist, mImpl->env.nearClip + 0.01f);
}
glm::mat4 Renderer::cameraView() const
{
    const NodeTransform camera = mImpl->worldTransform(mImpl->cameraNode);
    return glm::inverse(glm::translate(glm::mat4(1), camera.position) *
                        glm::mat4_cast(camera.orientation));
}

glm::mat4 Renderer::cameraProjection() const
{
    return glm::perspectiveRH_ZO(glm::radians(mImpl->env.fovDeg), 4.0f / 3.0f,
                                 mImpl->env.nearClip, mImpl->env.farClip);
}

glm::mat4 Renderer::cameraViewProj() const
{
    return cameraProjection() * cameraView();
}

bool Renderer::loadMaterialScript(const std::string& path)
{
    return mImpl->materials.loadFile(mImpl->core, path);
}
void Renderer::refreshAssetIndex() { mImpl->materials.refreshTextures(mImpl->core); }
void Renderer::setMaterialParam(const std::string& material,
                                const std::string& parameter, float value)
{
    if (!mImpl->materials.set(material, parameter, value) &&
        mImpl->warned.insert("material:" + material).second)
        log::warn("RHI renderer: material param '%s' ignored for missing material '%s'",
                  parameter.c_str(), material.c_str());
}
void Renderer::setMaterialParam(const std::string& material,
                                const std::string& parameter, glm::vec2 value)
{
    mImpl->materials.set(material, parameter, value);
}
void Renderer::setMaterialParam(const std::string& material,
                                const std::string& parameter, glm::vec3 value)
{
    mImpl->materials.set(material, parameter, value);
}
void Renderer::setMaterialParam(const std::string& material,
                                const std::string& parameter, glm::vec4 value)
{
    mImpl->materials.set(material, parameter, value);
}

void Renderer::setNodeShaderParams(NodeHandle node, const ShaderUniforms& params)
{
    if (Impl::Node* record = mImpl->node(node, "setNodeShaderParams")) {
        record->shader = params;
        record->hasShader = true;
    }
}
void Renderer::clearNodeShaderParams(NodeHandle node)
{
    if (Impl::Node* record = mImpl->node(node, "clearNodeShaderParams")) {
        record->shader = {};
        record->hasShader = false;
        record->blocks.clear();
    }
}
void Renderer::setNodeShaderBlock(NodeHandle node, const ShaderBlock& block)
{
    Impl::Node* record = mImpl->node(node, "setNodeShaderBlock");
    if (!record || !block.valid())
        return;
    for (int i = 0; i < block.fields.count; ++i) {
        const Field& field = block.fields.data[i];
        if (!field.name)
            continue;
        const void* value = fieldPtr(block.instance, field);
        bool supported = false;
        switch (field.type) {
        case FieldType::Bool:
            record->blocks[field.name] =
                *static_cast<const bool*>(value) ? 1.0f : 0.0f;
            break;
        case FieldType::Int:
            record->blocks[field.name] =
                float(*static_cast<const int*>(value));
            break;
        case FieldType::Float:
            record->blocks[field.name] = *static_cast<const float*>(value);
            supported = std::strcmp(field.name, "rimPower") == 0 ||
                        std::strcmp(field.name, "alphaScissor") == 0;
            break;
        case FieldType::Vec3:
        case FieldType::Colour:
            record->blocks[field.name] =
                *static_cast<const glm::vec3*>(value);
            break;
        case FieldType::Quat: {
            const glm::quat quaternion = *static_cast<const glm::quat*>(value);
            record->blocks[field.name] =
                glm::vec4(quaternion.x, quaternion.y, quaternion.z,
                          quaternion.w);
            break;
        }
        case FieldType::String:
            break;
        }
        const std::string name = field.name ? field.name : "<unnamed>";
        supported = supported || name == "modulateColor" ||
                    name == "rimColour";
        if (!supported && mImpl->warned.insert("shader-block:" + name).second)
            log::warn("RHI renderer: shader block field '%s' preserved but not rendered yet",
                      name.c_str());
    }
}
void Renderer::setGlobalMaterialParam(const std::string& parameter, float value)
{
    // Two of these are scene-wide GTE artefacts rather than per-material
    // settings, and the scene shaders read them out of the uniform block. They
    // still reach the material library as well: the parameter is what the
    // render presets and the debug panel already speak, and intercepting it
    // here is what makes the existing "Affine warp" slider drive this backend
    // without either of them learning a new call.
    if (parameter == "precisionMultiplier")
        mImpl->env.precisionMultiplier = value;
    else if (parameter == "affineAmount")
        mImpl->env.affineAmount = value;
    else if (parameter == "affineSoftness")
        mImpl->env.affineSoftness = value;
    for (const std::string& material : mImpl->materials.names())
        mImpl->materials.set(material, parameter, value);
}

void Renderer::setAmbient(glm::vec3 colour) { mImpl->env.ambient = colour; }
void Renderer::setFog(glm::vec3 colour, float density)
{
    mImpl->env.fogColour = colour;
    mImpl->env.fogDensity = std::max(density, 0.0f);
}
void Renderer::setBackground(glm::vec3 colour)
{
    mImpl->env.background = colour;
    mImpl->core.setBackground(colour);
}
const EnvState& Renderer::envState() const { return mImpl->env; }
void Renderer::setDitherEnabled(bool enabled)
{
    mImpl->env.dither = enabled;
}
void Renderer::setPixelSize(int pixelSize)
{
    mImpl->env.pixelSize = std::clamp(pixelSize, 1, 16);
    mImpl->core.setPixelSize(mImpl->env.pixelSize);
}
void Renderer::setRenderResolution(int width, int height)
{
    mImpl->core.setRenderResolution(width, height);
}
void Renderer::setPerPixelLightingEnabled(bool enabled)
{
    mImpl->env.perPixelLighting = enabled;
}
void Renderer::setOmniAttenuation(float exponent)
{
    mImpl->env.omniAttenuation = exponent;
}
void Renderer::setLightSteps(float steps) { mImpl->env.lightSteps = steps; }
void Renderer::setLightStepSoftness(float softness)
{
    mImpl->env.lightStepSoftness = softness;
}
void Renderer::setFogDesatBoost(float boost)
{
    mImpl->env.fogDesatBoost = boost;
}
void Renderer::setBloomEnabled(bool enabled) { mImpl->env.bloom = enabled; }
void Renderer::setBloomParams(float threshold, float intensity)
{
    mImpl->env.bloomThreshold = threshold;
    mImpl->env.bloomIntensity = intensity;
}
void Renderer::setWireframeDebug(bool enabled)
{
    if (mImpl->env.wireframe == enabled)
        return;
    mImpl->env.wireframe = enabled;
    // The post chain smears one-pixel lines, so the whole of it is bypassed
    // while the view is up and restored afterwards -- the same trade the Ogre
    // path makes. Stylize in particular would ink every triangle edge a second
    // time and turn the mesh into noise.
    if (enabled) {
        mImpl->preWireframe = {mImpl->env.pixelSize, mImpl->env.dither,
                               mImpl->env.bloom, mImpl->env.grade};
        setPixelSize(1);
        setMaterialParam("Engine/Psx/PixelStylize", "stylizeEnabled", 0.0f);
        setDitherEnabled(false);
        setBloomEnabled(false);
        setGradeEnabled(false);
    } else {
        setPixelSize(mImpl->preWireframe.pixelSize);
        setMaterialParam("Engine/Psx/PixelStylize", "stylizeEnabled", 1.0f);
        setDitherEnabled(mImpl->preWireframe.dither);
        setBloomEnabled(mImpl->preWireframe.bloom);
        setGradeEnabled(mImpl->preWireframe.grade);
    }
}
void Renderer::setGradeEnabled(bool enabled)
{
    mImpl->env.grade = enabled;
}
void Renderer::setGradeParams(float desaturate, float contrast,
                              glm::vec3 shadowTint, glm::vec3 midTint)
{
    mImpl->env.gradeDesaturate = desaturate;
    mImpl->env.gradeContrast = contrast;
    mImpl->env.gradeShadowTint = shadowTint;
    mImpl->env.gradeMidTint = midTint;
}
void Renderer::writeScreenshot(const std::string& path)
{
    mImpl->core.writeScreenshot(path);
}
void Renderer::frameStats(size_t& batches, size_t& triangles) const
{
    mImpl->core.frameStats(batches, triangles);
}

void Renderer::enableEditorViewport(int width, int height)
{
    mImpl->core.enableOffscreenViewport(width, height);
}
void Renderer::resizeEditorViewport(int width, int height)
{
    mImpl->core.resizeOffscreenViewport(width, height);
}
uint64_t Renderer::editorViewportTextureId() const
{
    return mImpl->core.viewportTextureId();
}
void Renderer::setEditorViewportBackground(const glm::vec3& colour)
{
    mImpl->core.setOffscreenBackground(colour.r, colour.g, colour.b);
}
void Renderer::setEditorCameraPose(const glm::vec3& position,
                                   const glm::quat& orientation, float fovDeg)
{
    mImpl->core.setEditorCameraPose(
        position.x, position.y, position.z, orientation.w, orientation.x,
        orientation.y, orientation.z, fovDeg);
}
void Renderer::enableMaterialThumbnail(int size)
{
    mImpl->core.enableThumbnailViewport(size);
}
void Renderer::setMaterialThumbnailCamera(const glm::vec3& position,
                                          const glm::quat& orientation,
                                          float fovDeg)
{
    mImpl->core.setThumbnailCameraPose(
        position.x, position.y, position.z, orientation.w, orientation.x,
        orientation.y, orientation.z, fovDeg);
}
uint64_t Renderer::materialThumbnailTextureId() const
{
    return mImpl->core.thumbnailTextureId();
}
void Renderer::setNodeThumbnailOnly(NodeHandle node, bool thumbnailOnly)
{
    if (Impl::Node* record = mImpl->node(node, "setNodeThumbnailOnly"))
        record->thumbnailOnly = thumbnailOnly;
}
void Renderer::setDebugLines(const std::vector<DebugLine>& lines)
{
    mImpl->debugLines = lines;
}
void Renderer::setDebugLinesXray(bool xray) { mImpl->debugLinesXray = xray; }

SceneView Renderer::scene() const { return SceneView(*this); }
std::vector<NodeHandle> SceneView::roots() const
{
    return mRenderer->mImpl->sceneRegistry.roots();
}
std::vector<NodeHandle> SceneView::childrenOf(NodeHandle node) const
{
    const NodeRecord* record = mRenderer->mImpl->sceneRegistry.find(node);
    return record ? record->children : std::vector<NodeHandle>{};
}
bool SceneView::info(NodeHandle handle, NodeInfo& out) const
{
    const Renderer::Impl& impl = *mRenderer->mImpl;
    const NodeRecord* sceneRecord = impl.sceneRegistry.find(handle);
    const Renderer::Impl::Node* node = impl.node(handle);
    if (!sceneRecord || !node)
        return false;
    out.handle = sceneRecord->handle;
    out.parent = sceneRecord->parent;
    out.name = sceneRecord->name;
    out.position = node->local.position;
    out.orientation = node->local.orientation;
    out.scale = node->local.scale;
    out.visible = node->visible;
    out.attachments.clear();
    for (const AttachRecord& attachment : sceneRecord->attachments)
        out.attachments.push_back(
            {attachment.kind, attachment.handle, attachment.label});
    return true;
}
bool SceneView::lightInfo(LightHandle handle, LightDesc& out) const
{
    const Renderer::Impl& impl = *mRenderer->mImpl;
    if (!handle.valid() || handle.id > impl.lights.size() ||
        !impl.lights[handle.id - 1].alive)
        return false;
    out = impl.lights[handle.id - 1].desc;
    return true;
}

namespace detail {
RenderCore& coreOf(Renderer& renderer) { return renderer.mImpl->core; }
void registerRoot(Renderer& renderer)
{
    Renderer::Impl::Node root;
    root.alive = true;
    renderer.mImpl->nodes.push_back(root);
    renderer.mImpl->cameraNode = kRootNode;
    if (!renderer.mImpl->initializeGpu())
        log::fatal("RHI renderer: application renderer initialization failed");
    particle_presets::registerDefaults(renderer);
}
} // namespace detail

} // namespace eng
