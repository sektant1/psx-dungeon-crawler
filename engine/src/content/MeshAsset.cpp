#include <eng/content/CookedMesh.h>

#include <eng/content/CookedFile.h>

#include <limits>

namespace fs = std::filesystem;

namespace eng::content {
namespace {

// Bounds on a single decode, so a truncated or corrupt file fails instead of
// asking for an allocation the machine cannot make. These are an order of
// magnitude above ModelImportLimits, which is the gate that actually enforces
// the content budget at import time; this is only the "do not trust a build
// output that was cut off mid-write" line.
constexpr uint32_t kMaxSubmeshes = 1u << 16;
constexpr uint32_t kMaxVerticesPerSubmesh = 50'000'000;
constexpr uint32_t kMaxIndicesPerSubmesh = 150'000'000;

void writeVec4(io::ByteWriter& out, const glm::vec4& v)
{
    out.f32(v.x);
    out.f32(v.y);
    out.f32(v.z);
    out.f32(v.w);
}

glm::vec4 readVec4(io::ByteReader& in)
{
    glm::vec4 v;
    v.x = in.f32();
    v.y = in.f32();
    v.z = in.f32();
    v.w = in.f32();
    return v;
}

} // namespace

bool writeCookedMesh(const fs::path& path, const MeshData& mesh,
                     const CookedMeshInfo& info, std::string& error)
{
    if (mesh.submeshes.size() > kMaxSubmeshes) {
        error = "mesh has " + std::to_string(mesh.submeshes.size()) +
                " submeshes, over the format's limit";
        return false;
    }

    io::ByteWriter out;

    out.str(info.sourcePath);
    out.str(info.format);
    out.u64(info.sourceBytes);
    out.u32(info.sourceMeshes);
    out.u32(info.materials);
    out.vec3(info.boundsMin);
    out.vec3(info.boundsMax);
    out.f32(info.metresPerSourceUnit);
    out.u8(info.pivot);
    out.u8(info.texcoordV);
    out.u8(info.canonicalPivotStandard ? 1u : 0u);
    out.u8(0); // reserved, keeps the info block 4-byte aligned

    out.u32(static_cast<uint32_t>(mesh.submeshes.size()));
    for (const MeshSubmesh& submesh : mesh.submeshes) {
        out.str(submesh.name);
        out.str(submesh.sourceMaterial);
        out.str(submesh.sourceTexture);
        out.u32(static_cast<uint32_t>(submesh.vertices.size()));
        for (const MeshVertex& vertex : submesh.vertices) {
            out.vec3(vertex.position);
            out.vec3(vertex.normal);
            writeVec4(out, vertex.tangent);
            out.f32(vertex.texcoord.x);
            out.f32(vertex.texcoord.y);
            writeVec4(out, vertex.colour);
        }
        out.u32(static_cast<uint32_t>(submesh.indices.size()));
        for (uint32_t index : submesh.indices)
            out.u32(index);
    }

    out.u32(static_cast<uint32_t>(mesh.collisionVertices.size()));
    for (const glm::vec3& vertex : mesh.collisionVertices)
        out.vec3(vertex);
    out.u32(static_cast<uint32_t>(mesh.collisionIndices.size()));
    for (uint32_t index : mesh.collisionIndices)
        out.u32(index);

    return writeCookedFile(path, kCookedMeshMagic, kCookedMeshVersion, out,
                           error);
}

bool readCookedMesh(const fs::path& path, MeshData& mesh, CookedMeshInfo& info,
                    std::string& error)
{
    mesh = {};
    info = {};

    CookedFileBody body;
    if (!readCookedFile(path, kCookedMeshMagic, body, error))
        return false;
    if (body.version != kCookedMeshVersion) {
        error = "rmesh version " + std::to_string(body.version) +
                ", this build reads " + std::to_string(kCookedMeshVersion);
        return false;
    }

    io::ByteReader in = body.reader();

    info.sourcePath = in.str();
    info.format = in.str();
    info.sourceBytes = in.u64();
    info.sourceMeshes = in.u32();
    info.materials = in.u32();
    info.boundsMin = in.vec3();
    info.boundsMax = in.vec3();
    info.metresPerSourceUnit = in.f32();
    info.pivot = in.u8();
    info.texcoordV = in.u8();
    info.canonicalPivotStandard = in.u8() != 0;
    in.u8();

    const uint32_t submeshCount = in.u32();
    if (!in.ok() || submeshCount > kMaxSubmeshes) {
        error = "bad submesh count";
        return false;
    }
    mesh.submeshes.resize(submeshCount);
    for (MeshSubmesh& submesh : mesh.submeshes) {
        submesh.name = in.str();
        submesh.sourceMaterial = in.str();
        submesh.sourceTexture = in.str();

        const uint32_t vertexCount = in.u32();
        if (!in.ok() || vertexCount > kMaxVerticesPerSubmesh) {
            error = "bad vertex count in submesh '" + submesh.name + "'";
            return false;
        }
        submesh.vertices.resize(vertexCount);
        for (MeshVertex& vertex : submesh.vertices) {
            vertex.position = in.vec3();
            vertex.normal = in.vec3();
            vertex.tangent = readVec4(in);
            vertex.texcoord.x = in.f32();
            vertex.texcoord.y = in.f32();
            vertex.colour = readVec4(in);
        }

        const uint32_t indexCount = in.u32();
        if (!in.ok() || indexCount > kMaxIndicesPerSubmesh) {
            error = "bad index count in submesh '" + submesh.name + "'";
            return false;
        }
        submesh.indices.resize(indexCount);
        for (uint32_t& index : submesh.indices) {
            index = in.u32();
            // A cooked index that points past its own vertex buffer is a file
            // that would crash the upload. Caught here, where the file name is
            // still in hand, instead of in the driver.
            if (index >= vertexCount) {
                error = "submesh '" + submesh.name + "' has an out-of-range index";
                return false;
            }
        }
    }

    const uint32_t collisionVertices = in.u32();
    if (!in.ok() || collisionVertices > kMaxVerticesPerSubmesh) {
        error = "bad collision vertex count";
        return false;
    }
    mesh.collisionVertices.resize(collisionVertices);
    for (glm::vec3& vertex : mesh.collisionVertices)
        vertex = in.vec3();

    const uint32_t collisionIndices = in.u32();
    if (!in.ok() || collisionIndices > kMaxIndicesPerSubmesh) {
        error = "bad collision index count";
        return false;
    }
    mesh.collisionIndices.resize(collisionIndices);
    for (uint32_t& index : mesh.collisionIndices) {
        index = in.u32();
        if (index >= collisionVertices) {
            error = "collision geometry has an out-of-range index";
            return false;
        }
    }

    if (!in.ok()) {
        error = "truncated rmesh payload";
        return false;
    }
    return true;
}

bool isCookedMesh(const fs::path& path)
{
    return cookedFileMatches(path, kCookedMeshMagic);
}

} // namespace eng::content
