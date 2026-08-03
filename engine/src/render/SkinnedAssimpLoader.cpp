#include "SkinnedAssimpLoader.h"

#include <assimp/Importer.hpp>
#include <assimp/config.h>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <unordered_map>

namespace eng::detail {
namespace {

glm::vec3 toGlm(const aiVector3D& value)
{
    return {value.x, value.y, value.z};
}

glm::vec4 toGlm(const aiColor4D& value)
{
    return {value.r, value.g, value.b, value.a};
}

glm::mat4 toGlm(const aiMatrix4x4& value)
{
    // Assimp names rows; GLM indexes columns.
    return {{value.a1, value.b1, value.c1, value.d1},
            {value.a2, value.b2, value.c2, value.d2},
            {value.a3, value.b3, value.c3, value.d3},
            {value.a4, value.b4, value.c4, value.d4}};
}

bool finite(glm::vec3 value)
{
    return std::isfinite(value.x) && std::isfinite(value.y) &&
           std::isfinite(value.z);
}

bool finite(const glm::mat4& value)
{
    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row)
            if (!std::isfinite(value[column][row]))
                return false;
    return true;
}

std::string materialName(const aiScene& scene, unsigned index)
{
    if (index >= scene.mNumMaterials || !scene.mMaterials[index])
        return {};
    aiString name;
    return scene.mMaterials[index]->Get(AI_MATKEY_NAME, name) == AI_SUCCESS
               ? std::string(name.C_Str())
               : std::string{};
}

struct Influence {
    uint16_t joint = 0;
    float weight = 0.0f;
};

} // namespace

bool importSkinnedModel(const std::filesystem::path& path,
                        const std::vector<std::string>& skeletonJointNames,
                        ImportedSkinnedModel& output, std::string& error)
{
    output = {};
    error.clear();
    try {
        if (!std::filesystem::is_regular_file(path))
            throw std::runtime_error("model file does not exist");
        if (skeletonJointNames.empty() ||
            skeletonJointNames.size() > std::numeric_limits<uint16_t>::max())
            throw std::runtime_error("invalid skeleton joint count");

        std::unordered_map<std::string, uint16_t> jointByName;
        jointByName.reserve(skeletonJointNames.size());
        for (size_t index = 0; index < skeletonJointNames.size(); ++index) {
            if (skeletonJointNames[index].empty() ||
                !jointByName
                     .emplace(skeletonJointNames[index], uint16_t(index))
                     .second)
                throw std::runtime_error(
                    "skeleton contains empty or duplicate joint names");
        }

        Assimp::Importer importer;
        importer.SetPropertyInteger(AI_CONFIG_PP_LBW_MAX_WEIGHTS, 4);
        const unsigned flags =
            aiProcess_CalcTangentSpace | aiProcess_JoinIdenticalVertices |
            aiProcess_Triangulate | aiProcess_GenSmoothNormals |
            aiProcess_ValidateDataStructure | aiProcess_ImproveCacheLocality |
            aiProcess_SortByPType | aiProcess_FindInvalidData |
            aiProcess_LimitBoneWeights;
        const aiScene* scene = importer.ReadFile(path.string(), flags);
        if (!scene)
            throw std::runtime_error(importer.GetErrorString());
        if (!scene->mRootNode || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE))
            throw std::runtime_error("model scene is incomplete");

        output.bounds.min =
            glm::vec3(std::numeric_limits<float>::infinity());
        output.bounds.max =
            glm::vec3(-std::numeric_limits<float>::infinity());
        uint64_t totalVertices = 0;
        uint64_t totalTriangles = 0;
        for (unsigned meshIndex = 0; meshIndex < scene->mNumMeshes;
             ++meshIndex) {
            const aiMesh* source = scene->mMeshes[meshIndex];
            if (!source || !source->HasBones())
                continue; // Cameras/reference primitives are not part of skin.
            if (source->mNumAnimMeshes > 0)
                throw std::runtime_error("morph targets are not supported");
            if (!source->HasPositions() || !source->HasNormals() ||
                source->mNumFaces == 0)
                throw std::runtime_error("skinned mesh has incomplete geometry");
            totalVertices += source->mNumVertices;
            totalTriangles += source->mNumFaces;
            if (totalVertices > 5'000'000 || totalTriangles > 5'000'000)
                throw std::runtime_error("skinned model exceeds import budget");

            ImportedSkinnedSubmesh submesh;
            submesh.name = source->mName.C_Str();
            submesh.sourceMaterial =
                materialName(*scene, source->mMaterialIndex);
            submesh.vertices.resize(source->mNumVertices);
            submesh.inverseBindPoses.assign(skeletonJointNames.size(),
                                            glm::mat4(1.0f));
            std::vector<std::vector<Influence>> influences(source->mNumVertices);

            for (unsigned boneIndex = 0; boneIndex < source->mNumBones;
                 ++boneIndex) {
                const aiBone* bone = source->mBones[boneIndex];
                if (!bone)
                    throw std::runtime_error("skinned mesh contains null bone");
                const auto joint = jointByName.find(bone->mName.C_Str());
                if (joint == jointByName.end())
                    throw std::runtime_error("mesh bone '" +
                                             std::string(bone->mName.C_Str()) +
                                             "' is absent from ozz skeleton");
                const glm::mat4 inverseBind = toGlm(bone->mOffsetMatrix);
                if (!finite(inverseBind))
                    throw std::runtime_error(
                        "mesh contains non-finite inverse bind pose");
                submesh.inverseBindPoses[joint->second] = inverseBind;
                for (unsigned weightIndex = 0; weightIndex < bone->mNumWeights;
                     ++weightIndex) {
                    const aiVertexWeight& weight = bone->mWeights[weightIndex];
                    if (weight.mVertexId >= source->mNumVertices)
                        throw std::runtime_error(
                            "bone weight references invalid vertex");
                    if (std::isfinite(weight.mWeight) && weight.mWeight > 0.0f)
                        influences[weight.mVertexId].push_back(
                            {joint->second, weight.mWeight});
                }
            }

            for (unsigned vertexIndex = 0; vertexIndex < source->mNumVertices;
                 ++vertexIndex) {
                ImportedSkinnedVertex& vertex = submesh.vertices[vertexIndex];
                vertex.position = toGlm(source->mVertices[vertexIndex]);
                vertex.normal = glm::normalize(toGlm(source->mNormals[vertexIndex]));
                if (source->HasTextureCoords(0))
                    vertex.texcoord = {
                        source->mTextureCoords[0][vertexIndex].x,
                        source->mTextureCoords[0][vertexIndex].y};
                if (source->HasVertexColors(0))
                    vertex.colour = toGlm(source->mColors[0][vertexIndex]);
                if (!finite(vertex.position) || !finite(vertex.normal))
                    throw std::runtime_error(
                        "skinned mesh contains non-finite vertex data");

                auto& vertexInfluences = influences[vertexIndex];
                std::sort(vertexInfluences.begin(), vertexInfluences.end(),
                          [](const Influence& lhs, const Influence& rhs) {
                              return lhs.weight > rhs.weight;
                          });
                if (vertexInfluences.size() > 4)
                    vertexInfluences.resize(4);
                float sum = 0.0f;
                for (const Influence& influence : vertexInfluences)
                    sum += influence.weight;
                if (!(sum > 1e-6f) || !std::isfinite(sum))
                    throw std::runtime_error(
                        "skinned mesh contains an unweighted vertex");
                vertex.weights.fill(0.0f);
                for (size_t influenceIndex = 0;
                     influenceIndex < vertexInfluences.size();
                     ++influenceIndex) {
                    vertex.joints[influenceIndex] =
                        vertexInfluences[influenceIndex].joint;
                    vertex.weights[influenceIndex] =
                        vertexInfluences[influenceIndex].weight / sum;
                }
                output.bounds.min = glm::min(output.bounds.min, vertex.position);
                output.bounds.max = glm::max(output.bounds.max, vertex.position);
            }

            submesh.indices.reserve(size_t(source->mNumFaces) * 3u);
            for (unsigned faceIndex = 0; faceIndex < source->mNumFaces;
                 ++faceIndex) {
                const aiFace& face = source->mFaces[faceIndex];
                if (face.mNumIndices != 3)
                    throw std::runtime_error(
                        "post-process produced non-triangle face");
                for (unsigned corner = 0; corner < 3; ++corner) {
                    if (face.mIndices[corner] >= source->mNumVertices)
                        throw std::runtime_error("face index is out of bounds");
                    submesh.indices.push_back(face.mIndices[corner]);
                }
            }
            output.submeshes.push_back(std::move(submesh));
        }
        if (output.submeshes.empty())
            throw std::runtime_error("model contains no skinned triangle mesh");
        return true;
    } catch (const std::exception& exception) {
        output = {};
        error = exception.what();
        return false;
    }
}

} // namespace eng::detail
