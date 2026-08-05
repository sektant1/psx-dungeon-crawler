#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

// Geometry in transit: what an importer produces, what a conditioner writes,
// and what the renderer uploads.
//
// This used to be eng::detail::ImportedModelVertex and friends, private to
// engine/src/render/AssimpLoader.h. It moves down to eng_core so the .rmesh
// writer and reader can name it: a cooked mesh that had to be described by a
// second, identical set of structs would be one struct away from a silent
// field-order bug every time either changed.
//
// This is NOT the GPU vertex layout (eng::MeshVertex, in Renderer.h). That one
// is what the pipeline's vertex shader binds and is the renderer's business;
// this one carries everything an importer knows, including the tangent frame
// and the source material name, and the renderer narrows it on upload.
namespace eng::content {

struct MeshVertex {
    glm::vec3 position{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    glm::vec4 tangent{1.0f, 0.0f, 0.0f, 1.0f};
    glm::vec2 texcoord{0.0f};
    glm::vec4 colour{1.0f};
};

struct MeshSubmesh {
    std::string name;
    std::string sourceMaterial;
    // Base-colour texture as the source file names it: relative to the model,
    // absolute, or "*N" for an embedded one. Empty when the material has none.
    // Never opened by the importer -- it is for converters deciding what to
    // copy, and for the resource database to record as a dependency.
    std::string sourceTexture;
    std::vector<MeshVertex> vertices;
    std::vector<uint32_t> indices;
};

struct MeshData {
    std::vector<MeshSubmesh> submeshes;
    std::vector<glm::vec3> collisionVertices;
    std::vector<uint32_t> collisionIndices;

    uint64_t vertexCount() const
    {
        uint64_t total = 0;
        for (const MeshSubmesh& submesh : submeshes)
            total += submesh.vertices.size();
        return total;
    }

    uint64_t triangleCount() const
    {
        uint64_t total = 0;
        for (const MeshSubmesh& submesh : submeshes)
            total += submesh.indices.size() / 3;
        return total;
    }
};

} // namespace eng::content
