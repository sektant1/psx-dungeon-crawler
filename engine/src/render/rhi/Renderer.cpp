#include <eng/Renderer.h>

#include "MaterialLibrary.h"
#include "RenderCore.h"
#include "render/AssimpLoader.h"
#include "render/PrimitiveGeometry.h"
#include "render/SceneRegistry.h"
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

// One billboard corner. Positions are world-space (the CPU already billboarded
// them), so the particle vertex stage is a plain viewProjection transform.
struct ParticleVertex {
    glm::vec3 position{0.0f};
    glm::vec2 uv{0.0f};
    glm::vec4 colour{1.0f};
};

static_assert(sizeof(ParticleVertex) == 36);

// Matches ParticleSim's reserve() below; a burst past it drops the tail rather
// than reallocating a GPU buffer mid-frame.
constexpr uint32_t kMaxDrawnParticles = 8192;

struct alignas(16) SceneUniforms {
    glm::mat4 viewProjection{1.0f};
    glm::mat4 view{1.0f};
    glm::vec4 cameraPositionAndLightCount{0.0f};
    glm::vec4 ambient{0.0f};
    glm::vec4 fogColourDensity{0.0f};
    glm::vec4 clipParams{0.05f, 4000.0f, 0.0f, 0.0f};
    std::array<glm::vec4, 16> lightPositionRange{};
    std::array<glm::vec4, 16> lightColourType{};
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
    std::vector<Node> nodes;
    std::vector<Light> lights;
    std::vector<Sprite> sprites;
    std::vector<StaticBatch> staticBatches;
    std::unordered_map<std::string, MeshHandle> prototypeMeshes;
    prototype::PrototypeCatalog prototypes;
    ModelMaterialFallbackWarnings missingMaterialWarnings;
    EnvState env;
    NodeHandle cameraNode{};
    int nameCounter = 0;

    rhi::ShaderHandle sceneVertex;
    rhi::ShaderHandle sceneFragment;
    std::unordered_map<uint32_t, rhi::PipelineHandle> pipelines;
    std::array<rhi::BufferHandle, 3> sceneUniformBuffers{};
    bool gpuShutdown = false;

    ParticleSim particleSim;
    ParticleTextureCatalog particleTextures;
    rhi::ShaderHandle particleVertexShader;
    rhi::ShaderHandle particleFragmentShader;
    // Keyed by blend mode: alpha and additive differ only in the blend state.
    std::unordered_map<uint32_t, rhi::PipelineHandle> particlePipelines;
    rhi::BufferHandle particleVertices;
    rhi::BufferHandle particleIndices;
    std::vector<ParticleVertex> particleStaging;
    std::vector<uint32_t> particleOrder;
    std::vector<ParticleEffect> particleEffects;
    std::unordered_map<std::string, uint32_t> particleByName;
    std::unordered_map<uint32_t, LiveParticles> liveParticles;
    std::vector<DecalRequest> decalRequests;
    std::vector<ParticleTextureDesc> particleTextureDescs;
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
        desc.cull = material.cull;
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

    void shutdownGpu()
    {
        if (gpuShutdown || !core.device())
            return;
        for (Mesh& resource : meshes)
            if (resource.alive)
                destroyMeshGpu(resource);
        for (auto& [key, pipeline] : pipelines)
            core.device()->destroyPipeline(pipeline);
        pipelines.clear();
        for (rhi::BufferHandle& buffer : sceneUniformBuffers) {
            if (buffer.valid()) core.device()->destroyBuffer(buffer);
            buffer = {};
        }
        if (sceneFragment.valid()) core.device()->destroyShader(sceneFragment);
        if (sceneVertex.valid()) core.device()->destroyShader(sceneVertex);
        sceneFragment = {};
        sceneVertex = {};
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

    void fillSceneUniforms(SceneUniforms& uniforms,
                           const RenderCore::View& requested, uint32_t width,
                           uint32_t height) const
    {
        glm::vec3 cameraPosition = requested.position;
        glm::quat cameraOrientation = requested.orientation;
        float fov = requested.fovDeg;
        float nearClip = requested.nearClip;
        float farClip = requested.farClip;
        if (requested.target == RenderCore::SceneTarget::Main) {
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
        uniforms.clipParams = {std::max(nearClip, 0.001f),
                               std::max(farClip, nearClip + 0.01f), 0.0f, 0.0f};
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
    }

    DrawConstants drawConstants(const Node& nodeRecord,
                                const rhi_renderer::Material& material,
                                const glm::mat4& model) const
    {
        DrawConstants constants;
        constants.model = model;
        const glm::vec4 materialTint = material.modulate();
        constants.tintOpacity = materialTint;
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
                  size_t& batches, size_t& triangles, bool withNormalDepth)
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
            const rhi_renderer::Material& material = materials.resolve(
                env.wireframe ? prototype::kSurfaceMaterial : requested,
                prototypes.materialFor(requested));
            const rhi::PipelineHandle pipeline =
                pipelineFor(material, attachment.renderOnTop, withNormalDepth);
            if (!pipeline.valid() || !material.texture.valid())
                continue;
            const GpuSubmesh& submesh = resource->submeshes[index];
            DrawConstants constants = drawConstants(nodeRecord, material, model);
            // Stone keeps its outlines but refuses the highlight wash; the
            // scene shader encodes that as a negative MRT depth.
            constants.surfaceParams.z = material.noHighlight ? 1.0f : 0.0f;
            commands.bindPipeline(pipeline);
            commands.bindVertexBuffer(0, submesh.vertices);
            commands.bindIndexBuffer(submesh.indices, 0, rhi::IndexType::UInt32);
            commands.bindTexture(0, material.texture.texture,
                                 material.texture.sampler);
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

    void draw(rhi::CommandList& commands, const RenderCore::View& view,
              uint32_t width, uint32_t height)
    {
        // Only the main pass carries the MRT metadata surface the stylizer
        // reads; the editor/thumbnail viewports render colour only.
        const bool withNormalDepth =
            view.target == RenderCore::SceneTarget::Main;
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
        // renderOnTop is Ogre's RENDER_QUEUE_8: drawn after everything else so
        // it lands on top. Here it only clears depth test/write, which is not
        // enough on its own -- a viewmodel node is created early, so in node
        // order it drew *before* the dungeon and, writing no depth, was then
        // painted over by the static batches below. Defer those attachments to
        // the end of the pass to restore the queue semantics.
        struct DeferredDraw {
            const Node* node;
            const MeshAttachment* attachment;
            glm::mat4 model;
        };
        std::vector<DeferredDraw> onTop;
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
                if (attachment.renderOnTop)
                    onTop.push_back({&nodeRecord, &attachment, model});
                else
                    drawMesh(commands, nodeRecord, attachment, model, batches,
                             triangles, withNormalDepth);
            }
        }
        if (view.target != RenderCore::SceneTarget::Thumbnail) {
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
        for (const DeferredDraw& deferred : onTop)
            drawMesh(commands, *deferred.node, *deferred.attachment,
                     deferred.model, batches, triangles, withNormalDepth);
        core.addFrameStats(batches, triangles);
        if (!debugLines.empty())
            warnOnce("debug-lines", "debug lines");
        if (particleSim.liveCount() > 0)
            warnOnce("particles", "particle batches");
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
    if (!detail::importStaticModel(path, options, imported, report)) {
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
    if (!detail::importStaticModel(path, options, imported, report)) {
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
    if (resolved != materialName &&
        mImpl->missingMaterialWarnings.shouldLog(materialName, true))
        log::error("RHI renderer: material '%s' is missing; using '%s'",
                   materialName.c_str(), resolved.c_str());
    for (Impl::MeshAttachment& attachment : record->meshes)
        attachment.materials.assign(
            std::max<size_t>(1, mImpl->mesh(attachment.mesh)
                                    ? mImpl->mesh(attachment.mesh)->submeshes.size()
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
    if (material.name != materialName &&
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
    if (castShadows)
        mImpl->warnOnce("shadows", "mesh shadows");
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
    if (castShadows)
        mImpl->warnOnce("shadows", "mesh shadows");
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
    mImpl->meshes.clear();
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
        effect.desc = std::move(desc);
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
    mImpl->warnOnce("particles", "particle batches");
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
    mImpl->warnOnce("particles", "particle batches");
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
    return mImpl->particleTextureDescs;
}
bool Renderer::reloadParticleTextures()
{
    mImpl->warnOnce("particle-textures", "particle texture hot reload");
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
        mImpl->warnOnce("shadows", "light shadows");
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
glm::mat4 Renderer::cameraViewProj() const
{
    const NodeTransform camera = mImpl->worldTransform(mImpl->cameraNode);
    const glm::mat4 view = glm::inverse(
        glm::translate(glm::mat4(1), camera.position) *
        glm::mat4_cast(camera.orientation));
    return glm::perspectiveRH_ZO(glm::radians(mImpl->env.fovDeg), 4.0f / 3.0f,
                                 mImpl->env.nearClip, mImpl->env.farClip) *
           view;
}

bool Renderer::loadMaterialScript(const std::string& path)
{
    return mImpl->materials.loadScript(mImpl->core, path);
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
    if (!enabled)
        mImpl->warnOnce("vertex-lighting", "vertex-lighting mode (per-pixel remains active)");
}
void Renderer::setOmniAttenuation(float exponent)
{
    mImpl->env.omniAttenuation = exponent;
}
void Renderer::setLightSteps(float steps)
{
    mImpl->env.lightSteps = steps;
    if (steps > 0) mImpl->warnOnce("light-steps", "posterized light steps");
}
void Renderer::setLightStepSoftness(float softness)
{
    mImpl->env.lightStepSoftness = softness;
}
void Renderer::setFogDesatBoost(float boost)
{
    mImpl->env.fogDesatBoost = boost;
}
void Renderer::setBloomEnabled(bool enabled)
{
    mImpl->env.bloom = enabled;
    if (enabled) mImpl->warnOnce("bloom", "bloom post-processing");
}
void Renderer::setBloomParams(float threshold, float intensity)
{
    mImpl->env.bloomThreshold = threshold;
    mImpl->env.bloomIntensity = intensity;
}
void Renderer::setWireframeDebug(bool enabled)
{
    mImpl->env.wireframe = enabled;
    if (enabled) mImpl->warnOnce("wireframe", "wireframe rasterization");
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
