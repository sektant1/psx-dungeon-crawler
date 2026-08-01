#include "AssimpLoader.h"

#include <assimp/Importer.hpp>
#include <assimp/config.h>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <glm/gtc/matrix_inverse.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace eng::detail {
namespace {

constexpr uint64_t kAbsoluteVertexLimit = 50'000'000;
constexpr uint64_t kAbsoluteTriangleLimit = 50'000'000;
constexpr uint64_t kAbsoluteSourceByteLimit =
    2ull * 1024ull * 1024ull * 1024ull;
constexpr uint32_t kAbsoluteSubmeshLimit = 16'384;
constexpr uint32_t kAbsoluteMaterialLimit = 16'384;
constexpr uint32_t kAbsoluteNodeLimit = 1'000'000;
constexpr uint32_t kMaxNodeDepth = 1'024;

bool finite(glm::vec2 value)
{
    return std::isfinite(value.x) && std::isfinite(value.y);
}

bool finite(glm::vec4 value)
{
    return std::isfinite(value.x) && std::isfinite(value.y) &&
           std::isfinite(value.z) && std::isfinite(value.w);
}

bool finite(const glm::mat4& value)
{
    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row)
            if (!std::isfinite(value[column][row]))
                return false;
    return true;
}

glm::mat4 toGlm(const aiMatrix4x4& value)
{
    return {{value.a1, value.b1, value.c1, value.d1},
            {value.a2, value.b2, value.c2, value.d2},
            {value.a3, value.b3, value.c3, value.d3},
            {value.a4, value.b4, value.c4, value.d4}};
}

glm::vec3 toGlm(const aiVector3D& value)
{
    return {value.x, value.y, value.z};
}

glm::vec4 toGlm(const aiColor4D& value)
{
    return {value.r, value.g, value.b, value.a};
}

std::string lower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });
    return value;
}

void warn(ModelImportReport& report, std::string message)
{
    if (std::find(report.warnings.begin(), report.warnings.end(), message) ==
        report.warnings.end())
        report.warnings.push_back(std::move(message));
}

glm::vec3 normalized(glm::vec3 value, const char* what)
{
    const float length2 = glm::dot(value, value);
    if (!model_import_detail::finite(value) || !std::isfinite(length2) ||
        length2 <= 1e-20f)
        throw std::runtime_error(std::string("invalid ") + what);
    return value / std::sqrt(length2);
}

glm::vec3 fallbackTangent(glm::vec3 normal)
{
    const glm::vec3 axis = std::abs(normal.y) < 0.999f
                               ? glm::vec3(0.0f, 1.0f, 0.0f)
                               : glm::vec3(1.0f, 0.0f, 0.0f);
    return normalized(glm::cross(axis, normal), "generated tangent");
}

struct FaceKey {
    std::array<uint32_t, 3> vertices{};
    bool operator==(const FaceKey&) const = default;
};

struct FaceKeyHash {
    size_t operator()(const FaceKey& face) const
    {
        size_t hash = face.vertices[0];
        hash = hash * 1'000'003u + face.vertices[1];
        return hash * 1'000'003u + face.vertices[2];
    }
};

FaceKey orientedFaceKey(const std::array<uint32_t, 3>& face)
{
    const std::array<uint32_t, 3> second{face[1], face[2], face[0]};
    const std::array<uint32_t, 3> third{face[2], face[0], face[1]};
    return {std::min({face, second, third})};
}

uint64_t edgeKey(uint32_t first, uint32_t second)
{
    if (first > second)
        std::swap(first, second);
    return (uint64_t(first) << 32u) | uint64_t(second);
}

bool defaultFlipV(const std::filesystem::path& path)
{
    return lower(path.extension().string()) == ".obj";
}

struct ObjColourKey {
    std::array<uint32_t, 6> values{};
    bool operator==(const ObjColourKey&) const = default;
};

struct ObjColourKeyHash {
    size_t operator()(const ObjColourKey& key) const
    {
        size_t hash = 0;
        for (uint32_t value : key.values)
            hash = hash * 1'000'003u + value;
        return hash;
    }
};

ObjColourKey objColourKey(glm::vec3 position, glm::vec3 colour)
{
    return {{model_import_detail::floatBits(position.x),
             model_import_detail::floatBits(position.y),
             model_import_detail::floatBits(position.z),
             model_import_detail::floatBits(colour.r),
             model_import_detail::floatBits(colour.g),
             model_import_detail::floatBits(colour.b)}};
}

using ObjAlphaMap =
    std::unordered_map<ObjColourKey, float, ObjColourKeyHash>;

// Assimp's OBJ importer accepts RGB vertex colours but rejects the common
// Blender/tool extension with an additional alpha component. Normalize that
// one spelling in memory; source files stay untouched and standard OBJ files
// still use Assimp's filesystem path (including any MTL sidecar).
std::string normalizedExtendedObj(const std::filesystem::path& path,
                                  bool& changed, bool& ambiguousAlpha,
                                  ObjAlphaMap& alphaByVertex)
{
    changed = false;
    ambiguousAlpha = false;
    alphaByVertex.clear();
    std::ifstream input(path);
    if (!input)
        return {};
    std::ostringstream output;
    std::string line;
    while (std::getline(input, line)) {
        std::istringstream values(line);
        std::string tag;
        if (!(values >> tag) || tag != "v") {
            output << line << '\n';
            continue;
        }
        std::vector<std::string> fields;
        std::string field;
        while (values >> field)
            fields.push_back(std::move(field));
        if (fields.size() != 7) {
            output << line << '\n';
            continue;
        }
        changed = true;
        try {
            const glm::vec3 position{std::stof(fields[0]),
                                     std::stof(fields[1]),
                                     std::stof(fields[2])};
            const glm::vec3 colour{std::stof(fields[3]),
                                   std::stof(fields[4]),
                                   std::stof(fields[5])};
            const float alpha = std::stof(fields[6]);
            auto [found, inserted] =
                alphaByVertex.emplace(objColourKey(position, colour), alpha);
            if (!inserted && std::abs(found->second - alpha) > 0.0001f)
                ambiguousAlpha = true;
        } catch (...) {
            // Assimp will produce the authoritative malformed-number error.
        }
        output << "v";
        for (size_t index = 0; index < 6; ++index)
            output << ' ' << fields[index];
        output << '\n';
    }
    return changed ? output.str() : std::string{};
}

std::string materialName(const aiScene& scene, unsigned index)
{
    if (index >= scene.mNumMaterials)
        return {};
    aiString name;
    if (scene.mMaterials[index]->Get(AI_MATKEY_NAME, name) == AI_SUCCESS &&
        name.length > 0)
        return name.C_Str();
    return "material_" + std::to_string(index);
}

struct ImportContext {
    const aiScene& scene;
    const ModelImportOptions& options;
    bool flipV = false;
    const ObjAlphaMap* objAlphaByVertex = nullptr;
    ImportedModelData& model;
    ModelImportReport& report;
    uint64_t rawVertices = 0;
    uint64_t rawTriangles = 0;
    uint32_t rawSubmeshes = 0;
    uint32_t nodes = 0;
};

void checkBudget(const ImportContext& context, uint64_t vertices,
                 uint64_t triangles, uint32_t submeshes)
{
    const ModelImportLimits& limits = context.options.limits;
    const uint64_t vertexLimit =
        std::min(limits.maxVertices, kAbsoluteVertexLimit);
    const uint64_t triangleLimit =
        std::min(limits.maxTriangles, kAbsoluteTriangleLimit);
    const uint32_t submeshLimit =
        std::min(limits.maxSubmeshes, kAbsoluteSubmeshLimit);
    if (vertices > vertexLimit)
        throw std::runtime_error("vertex budget exceeded (" +
                                 std::to_string(vertices) + " > " +
                                 std::to_string(vertexLimit) + ")");
    if (triangles > triangleLimit)
        throw std::runtime_error("triangle budget exceeded (" +
                                 std::to_string(triangles) + " > " +
                                 std::to_string(triangleLimit) + ")");
    if (submeshes > submeshLimit)
        throw std::runtime_error("submesh budget exceeded (" +
                                 std::to_string(submeshes) + " > " +
                                 std::to_string(submeshLimit) + ")");
}

void appendMeshInstance(ImportContext& context, const aiMesh& source,
                        const glm::mat4& world, const std::string& nodeName,
                        unsigned sourceMeshIndex)
{
    if (source.HasBones())
        throw std::runtime_error("skinned mesh '" + std::string(source.mName.C_Str()) +
                                 "' requires skeletal import");
    if (source.mNumAnimMeshes > 0)
        throw std::runtime_error("morph targets require deforming-mesh import");
    if (source.mNumFaces == 0 || source.mNumVertices == 0) {
        warn(context.report, "ignored empty mesh " +
                                 std::to_string(sourceMeshIndex));
        return;
    }
    if (!source.HasNormals())
        throw std::runtime_error("Assimp did not produce vertex normals");
    context.rawVertices += uint64_t(source.mNumVertices);
    context.rawTriangles += uint64_t(source.mNumFaces);
    ++context.rawSubmeshes;
    checkBudget(context, context.rawVertices, context.rawTriangles,
                context.rawSubmeshes);
    if (!finite(world))
        throw std::runtime_error("node transform contains non-finite values");

    const glm::mat3 linear(world);
    const float determinant = glm::determinant(linear);
    if (!std::isfinite(determinant) || std::abs(determinant) <= 1e-12f)
        throw std::runtime_error("node transform is singular");
    const glm::mat3 normalMatrix = glm::transpose(glm::inverse(linear));
    const glm::mat3 sourceToEngine = glm::mat3(
        glm::mat4_cast(context.options.sourceOrientation) *
        glm::scale(glm::mat4(1.0f),
                   glm::vec3(context.options.metresPerSourceUnit)));
    const bool mirrored = determinant < 0.0f;

    std::vector<ImportedModelVertex> expanded(source.mNumVertices);
    const bool hasTangents = source.HasTangentsAndBitangents();
    for (unsigned index = 0; index < source.mNumVertices; ++index) {
        ImportedModelVertex& vertex = expanded[index];
        vertex.position = glm::vec3(world * glm::vec4(toGlm(source.mVertices[index]),
                                                       1.0f));
        vertex.normal = normalized(normalMatrix * toGlm(source.mNormals[index]),
                                   "normal");

        glm::vec3 tangent;
        glm::vec3 bitangent;
        if (hasTangents) {
            tangent = linear * toGlm(source.mTangents[index]);
            tangent -= vertex.normal * glm::dot(vertex.normal, tangent);
            tangent = normalized(tangent, "tangent");
            bitangent = normalized(linear * toGlm(source.mBitangents[index]),
                                   "bitangent");
        } else {
            tangent = fallbackTangent(vertex.normal);
            bitangent = glm::cross(vertex.normal, tangent);
        }
        float tangentSign =
            glm::dot(glm::cross(vertex.normal, tangent), bitangent) < 0.0f
                ? -1.0f
                : 1.0f;
        if (context.flipV)
            tangentSign = -tangentSign;
        vertex.tangent = {tangent, tangentSign};

        if (source.HasTextureCoords(0)) {
            vertex.texcoord = {source.mTextureCoords[0][index].x,
                               source.mTextureCoords[0][index].y};
            if (context.flipV)
                vertex.texcoord.y = 1.0f - vertex.texcoord.y;
        }
        if (source.HasVertexColors(0))
            vertex.colour = toGlm(source.mColors[0][index]);
        if (context.objAlphaByVertex && source.HasVertexColors(0)) {
            const auto alpha = context.objAlphaByVertex->find(objColourKey(
                toGlm(source.mVertices[index]), glm::vec3(vertex.colour)));
            if (alpha != context.objAlphaByVertex->end())
                vertex.colour.a = alpha->second;
        }

        if (!model_import_detail::finite(vertex.position) ||
            !model_import_detail::finite(vertex.normal) ||
            !finite(vertex.tangent) || !finite(vertex.texcoord) ||
            !finite(vertex.colour))
            throw std::runtime_error("mesh contains non-finite vertex data");
    }

    if (source.GetNumUVChannels() > 1)
        warn(context.report, "only UV channel 0 is imported");
    if (source.GetNumColorChannels() > 1)
        warn(context.report, "only vertex colour channel 0 is imported");
    if (source.HasTextureCoords(0) && source.mNumUVComponents[0] > 2)
        warn(context.report, "3D texture coordinates are reduced to UV");

    std::vector<bool> used(source.mNumVertices, false);
    std::vector<std::array<uint32_t, 3>> faces;
    faces.reserve(source.mNumFaces);
    std::unordered_set<FaceKey, FaceKeyHash> uniqueFaces;
    std::unordered_map<uint64_t, uint8_t> edgeUse;
    for (unsigned faceIndex = 0; faceIndex < source.mNumFaces; ++faceIndex) {
        const aiFace& sourceFace = source.mFaces[faceIndex];
        if (sourceFace.mNumIndices != 3)
            throw std::runtime_error("post-process produced a non-triangle face");
        std::array<uint32_t, 3> face{
            sourceFace.mIndices[0], sourceFace.mIndices[1],
            sourceFace.mIndices[2]};
        for (uint32_t index : face) {
            if (index >= source.mNumVertices)
                throw std::runtime_error("face index is outside vertex stream");
        }
        if (face[0] == face[1] || face[1] == face[2] || face[2] == face[0]) {
            ++context.report.removedDegenerateTriangles;
            continue;
        }
        const glm::vec3 first = sourceToEngine *
                                (expanded[face[1]].position -
                                 expanded[face[0]].position);
        const glm::vec3 second = sourceToEngine *
                                 (expanded[face[2]].position -
                                  expanded[face[0]].position);
        const float area2 = glm::dot(glm::cross(first, second),
                                     glm::cross(first, second));
        if (!std::isfinite(area2))
            throw std::runtime_error("triangle area is non-finite");
        if (area2 <= 1e-20f) {
            ++context.report.removedDegenerateTriangles;
            continue;
        }

        const FaceKey duplicate = orientedFaceKey(face);
        if (!uniqueFaces.insert(duplicate).second) {
            ++context.report.removedDuplicateTriangles;
            continue;
        }
        for (int edge = 0; edge < 3; ++edge) {
            uint8_t& count = edgeUse[edgeKey(face[edge], face[(edge + 1) % 3])];
            if (++count > 2)
                throw std::runtime_error("mesh contains a non-manifold edge");
        }
        for (uint32_t index : face)
            used[index] = true;
        if (mirrored)
            std::swap(face[1], face[2]);
        faces.push_back(face);
    }

    if (faces.empty()) {
        context.report.removedLooseVertices += source.mNumVertices;
        warn(context.report, "ignored mesh containing no usable triangles");
        return;
    }

    std::vector<uint32_t> remap(source.mNumVertices,
                                std::numeric_limits<uint32_t>::max());
    ImportedModelSubmesh submesh;
    submesh.name = nodeName.empty() ? source.mName.C_Str() : nodeName;
    if (submesh.name.empty())
        submesh.name = "mesh_" + std::to_string(sourceMeshIndex);
    submesh.sourceMaterial = materialName(context.scene, source.mMaterialIndex);
    submesh.vertices.reserve(source.mNumVertices);
    for (unsigned index = 0; index < source.mNumVertices; ++index) {
        if (!used[index]) {
            ++context.report.removedLooseVertices;
            continue;
        }
        remap[index] = uint32_t(submesh.vertices.size());
        submesh.vertices.push_back(expanded[index]);
    }
    submesh.indices.reserve(faces.size() * 3u);
    for (const auto& face : faces)
        for (uint32_t index : face)
            submesh.indices.push_back(remap[index]);

    const uint64_t totalVertices =
        context.report.vertices + submesh.vertices.size();
    const uint64_t totalTriangles =
        context.report.triangles + submesh.indices.size() / 3u;
    const uint32_t totalSubmeshes = context.report.submeshes + 1u;
    checkBudget(context, totalVertices, totalTriangles, totalSubmeshes);
    context.report.vertices = totalVertices;
    context.report.triangles = totalTriangles;
    context.report.submeshes = totalSubmeshes;
    context.model.submeshes.push_back(std::move(submesh));
}

void visitNode(ImportContext& context, const aiNode& node,
               const glm::mat4& parent, uint32_t depth)
{
    if (depth > kMaxNodeDepth)
        throw std::runtime_error("node hierarchy exceeds depth limit");
    const uint32_t nodeLimit =
        std::min(context.options.limits.maxNodes, kAbsoluteNodeLimit);
    if (++context.nodes > nodeLimit)
        throw std::runtime_error("node budget exceeded (" +
                                 std::to_string(context.nodes) + " > " +
                                 std::to_string(nodeLimit) + ")");
    const glm::mat4 world = parent * toGlm(node.mTransformation);
    for (unsigned index = 0; index < node.mNumMeshes; ++index) {
        const unsigned meshIndex = node.mMeshes[index];
        if (meshIndex >= context.scene.mNumMeshes)
            throw std::runtime_error("node references an invalid mesh");
        appendMeshInstance(context, *context.scene.mMeshes[meshIndex], world,
                           node.mName.C_Str(), meshIndex);
    }
    for (unsigned index = 0; index < node.mNumChildren; ++index) {
        if (!node.mChildren[index])
            throw std::runtime_error("node hierarchy contains a null child");
        visitNode(context, *node.mChildren[index], world, depth + 1u);
    }
}

void buildCollisionGeometry(ImportedModelData& model)
{
    model.collisionVertices.clear();
    model.collisionIndices.clear();
    for (const ImportedModelSubmesh& submesh : model.submeshes) {
        if (model.collisionVertices.size() + submesh.vertices.size() >
            std::numeric_limits<uint32_t>::max())
            throw std::runtime_error("collision vertex indices exceed uint32");
        const uint32_t base = uint32_t(model.collisionVertices.size());
        for (const ImportedModelVertex& vertex : submesh.vertices)
            model.collisionVertices.push_back(vertex.position);
        for (uint32_t index : submesh.indices)
            model.collisionIndices.push_back(base + index);
    }
}

} // namespace

bool transformImportedModel(ImportedModelData& model,
                            const glm::mat4& transform, std::string& error)
{
    error.clear();
    if (!finite(transform)) {
        error = "vertex bake contains non-finite values";
        return false;
    }
    const glm::mat3 linear(transform);
    const float determinant = glm::determinant(linear);
    if (!std::isfinite(determinant) || std::abs(determinant) <= 1e-12f) {
        error = "vertex bake is singular";
        return false;
    }
    const glm::mat3 normalMatrix = glm::transpose(glm::inverse(linear));
    const bool mirrored = determinant < 0.0f;
    try {
        for (ImportedModelSubmesh& submesh : model.submeshes) {
            for (ImportedModelVertex& vertex : submesh.vertices) {
                vertex.position = glm::vec3(
                    transform * glm::vec4(vertex.position, 1.0f));
                vertex.normal = normalized(normalMatrix * vertex.normal,
                                           "transformed normal");
                glm::vec3 tangent = linear * glm::vec3(vertex.tangent);
                tangent -= vertex.normal * glm::dot(vertex.normal, tangent);
                tangent = normalized(tangent, "transformed tangent");
                vertex.tangent = {tangent,
                                  mirrored ? -vertex.tangent.w
                                           : vertex.tangent.w};
                if (!model_import_detail::finite(vertex.position))
                    throw std::runtime_error(
                        "vertex bake produced a non-finite position");
            }
            if (mirrored)
                for (size_t index = 0; index + 2 < submesh.indices.size();
                     index += 3)
                    std::swap(submesh.indices[index + 1],
                              submesh.indices[index + 2]);
        }
        for (glm::vec3& vertex : model.collisionVertices) {
            vertex = glm::vec3(transform * glm::vec4(vertex, 1.0f));
            if (!model_import_detail::finite(vertex))
                throw std::runtime_error(
                    "collision bake produced a non-finite position");
        }
        if (mirrored)
            for (size_t index = 0; index + 2 < model.collisionIndices.size();
                 index += 3)
                std::swap(model.collisionIndices[index + 1],
                          model.collisionIndices[index + 2]);
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
    return true;
}

MeshBounds importedModelBounds(const ImportedModelData& model)
{
    MeshBounds bounds{
        glm::vec3(std::numeric_limits<float>::infinity()),
        glm::vec3(-std::numeric_limits<float>::infinity())};
    for (const ImportedModelSubmesh& submesh : model.submeshes)
        for (const ImportedModelVertex& vertex : submesh.vertices) {
            bounds.min = glm::min(bounds.min, vertex.position);
            bounds.max = glm::max(bounds.max, vertex.position);
        }
    return bounds;
}

bool importStaticModel(const std::filesystem::path& path,
                       const ModelImportOptions& input,
                       ImportedModelData& out, ModelImportReport& report)
{
    ImportedModelData model;
    report = {};
    report.sourcePath = path.string();
    report.format = lower(path.extension().string());
    report.sourceUnitsAssumed =
        report.format != ".glb" && report.format != ".gltf";
    try {
        if (!std::filesystem::is_regular_file(path))
            throw std::runtime_error("model file does not exist");

        const ModelImportOptions options = sanitizeModelImportOptions(input);
        const uint64_t sourceByteLimit =
            std::min(options.limits.maxSourceBytes,
                     kAbsoluteSourceByteLimit);
        const uint64_t sourceBytes = std::filesystem::file_size(path);
        report.sourceBytes = sourceBytes;
        if (sourceBytes > sourceByteLimit)
            throw std::runtime_error("source file budget exceeded (" +
                                     std::to_string(sourceBytes) + " > " +
                                     std::to_string(sourceByteLimit) + ")");

        Assimp::Importer importer;
        if (!importer.IsExtensionSupported(report.format))
            throw std::runtime_error("unsupported model format '" +
                                     report.format + "'");
        importer.SetPropertyInteger(
            AI_CONFIG_PP_SBP_REMOVE,
            aiPrimitiveType_POINT | aiPrimitiveType_LINE);
        importer.SetPropertyFloat(AI_CONFIG_PP_GSN_MAX_SMOOTHING_ANGLE, 80.0f);
        const unsigned flags =
            aiProcess_CalcTangentSpace | aiProcess_JoinIdenticalVertices |
            aiProcess_Triangulate | aiProcess_GenSmoothNormals |
            aiProcess_ValidateDataStructure | aiProcess_ImproveCacheLocality |
            aiProcess_SortByPType | aiProcess_FindInvalidData;
        const aiScene* scene = nullptr;
        bool normalizedObj = false;
        bool ambiguousObjAlpha = false;
        ObjAlphaMap objAlphaByVertex;
        std::string objSource;
        if (report.format == ".obj")
            objSource = normalizedExtendedObj(path, normalizedObj,
                                              ambiguousObjAlpha,
                                              objAlphaByVertex);
        if (normalizedObj) {
            scene = importer.ReadFileFromMemory(objSource.data(),
                                                objSource.size(), flags, "obj");
            warn(report,
                 "normalized nonstandard OBJ RGBA vertices for Assimp");
            if (ambiguousObjAlpha)
                warn(report,
                     "ambiguous OBJ vertex alpha could not be preserved; use glTF");
        } else {
            scene = importer.ReadFile(path.string(), flags);
        }
        if (!scene)
            throw std::runtime_error(importer.GetErrorString());
        if (!scene->mRootNode || scene->mNumMeshes == 0 ||
            (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) != 0)
            throw std::runtime_error("model scene is incomplete or has no meshes");
        if (scene->HasAnimations())
            throw std::runtime_error(
                "animations require skeletal/deforming model import");

        const uint32_t materialLimit =
            std::min(options.limits.maxMaterials, kAbsoluteMaterialLimit);
        if (scene->mNumMaterials > materialLimit)
            throw std::runtime_error("material budget exceeded (" +
                                     std::to_string(scene->mNumMaterials) +
                                     " > " +
                                     std::to_string(materialLimit) +
                                     ")");
        report.sourceMeshes = scene->mNumMeshes;
        report.materials = scene->mNumMaterials;
        report.embeddedTextures = scene->mNumTextures;
        if (scene->mNumTextures > 0)
            warn(report,
                 "embedded textures are metadata only; bind engine materials explicitly");
        if (scene->mNumCameras > 0 || scene->mNumLights > 0)
            warn(report, "model cameras and lights are ignored");

        bool flipV = options.texcoordV == TexcoordVMode::Flip;
        if (options.texcoordV == TexcoordVMode::FormatDefault)
            flipV = defaultFlipV(path);
        ImportContext context{*scene, options, flipV,
                              normalizedObj ? &objAlphaByVertex : nullptr,
                              model, report};
        visitNode(context, *scene->mRootNode, glm::mat4(1.0f), 0u);
        if (model.submeshes.empty())
            throw std::runtime_error("model contains no triangle mesh data");

        std::vector<glm::vec3> positions;
        positions.reserve(size_t(report.vertices));
        for (const ImportedModelSubmesh& submesh : model.submeshes)
            for (const ImportedModelVertex& vertex : submesh.vertices)
                positions.push_back(vertex.position);
        report.sourceBounds = modelImportBounds(positions, glm::mat4(1.0f));
        report.appliedPivot = options.pivot;
        report.appliedMetresPerSourceUnit = options.metresPerSourceUnit;
        report.appliedSourceOrientation = options.sourceOrientation;
        const glm::mat4 bake = modelImportBakeMatrix(options, positions);
        std::string transformError;
        if (!transformImportedModel(model, bake, transformError))
            throw std::runtime_error(transformError);
        report.finalBounds = importedModelBounds(model);
        report.canonicalPivotStandard =
            options.pivot == PivotMode::BottomCenter;
        if (report.canonicalPivotStandard &&
            !modelImportMeetsSpatialStandard(report.finalBounds))
            throw std::runtime_error(
                "canonical spatial standard failed after import");
        report.sourceMaterials.clear();
        report.sourceMaterials.reserve(model.submeshes.size());
        std::unordered_set<std::string> usedMaterials;
        for (const ImportedModelSubmesh& submesh : model.submeshes) {
            report.sourceMaterials.push_back(submesh.sourceMaterial);
            if (!submesh.sourceMaterial.empty())
                usedMaterials.insert(submesh.sourceMaterial);
        }
        report.materials = uint32_t(usedMaterials.size());
        buildCollisionGeometry(model);
        if (report.removedLooseVertices > 0)
            warn(report, "removed " +
                             std::to_string(report.removedLooseVertices) +
                             " loose vertices");
        if (report.removedDegenerateTriangles > 0)
            warn(report, "removed " +
                             std::to_string(report.removedDegenerateTriangles) +
                             " degenerate triangles");
        if (report.removedDuplicateTriangles > 0)
            warn(report, "removed " +
                             std::to_string(report.removedDuplicateTriangles) +
                             " duplicate triangles");
        out = std::move(model);
        return true;
    } catch (const std::exception& exception) {
        report.error = exception.what();
    } catch (...) {
        report.error = "unknown model import failure";
    }
    out = {};
    return false;
}

std::vector<std::string> supportedAssimpModelExtensions()
{
    Assimp::Importer importer;
    aiString raw;
    importer.GetExtensionList(raw);
    std::vector<std::string> result;
    std::istringstream stream(raw.C_Str());
    std::string extension;
    while (std::getline(stream, extension, ';')) {
        if (extension.starts_with('*'))
            extension.erase(extension.begin());
        extension = lower(extension);
        if (!extension.empty())
            result.push_back(std::move(extension));
    }
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}

bool assimpSupportsModelFile(const std::filesystem::path& path)
{
    const std::string extension = lower(path.extension().string());
    Assimp::Importer importer;
    return !extension.empty() && importer.IsExtensionSupported(extension);
}

} // namespace eng::detail
