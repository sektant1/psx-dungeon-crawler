#include "ObjLoader.h"

#include <Ogre.h>
#include <glm/glm.hpp>

#include <fstream>
#include <memory>
#include <sstream>
#include <utility>
#include <vector>

namespace ObjLoader {

namespace {

Ogre::Matrix4 toOgre(const glm::mat4& matrix)
{
    Ogre::Matrix4 result;
    for (int row = 0; row < 4; ++row)
        for (int column = 0; column < 4; ++column)
            result[row][column] = matrix[column][row];
    return result;
}

struct Vert {
    Ogre::Vector3 position;
    Ogre::ColourValue colour{1, 1, 1, 1};
};

struct FaceVertex {
    int position = -1;
    int uv = -1;
    int normal = -1;
};

struct Face {
    std::vector<FaceVertex> vertices;
    std::string material;
};

// One source representation is parsed once, then transformed and consumed by
// both Ogre upload and CPU collision capture.
struct ParsedObj {
    std::vector<Vert> positions;
    std::vector<Ogre::Vector2> uvs;
    std::vector<Ogre::Vector3> normals;
    std::vector<Face> faces;
    std::vector<std::string> materialLibraries;
};

struct PartialMeshGuard {
    explicit PartialMeshGuard(std::string resourceName)
        : name(std::move(resourceName))
    {}

    ~PartialMeshGuard()
    {
        if (keep)
            return;
        try {
            Ogre::MeshManager* manager =
                Ogre::MeshManager::getSingletonPtr();
            if (manager && manager->getByName(name))
                manager->remove(name);
        } catch (...) {
            // Cleanup must never replace the original load exception.
        }
    }

    std::string name;
    bool keep = false;
};

// "1/2/3" | "1//3" | "1" -> zero-based indices (OBJ is 1-based, may be
// negative).
FaceVertex parseFaceRef(const std::string& token, int positionCount,
                        int uvCount, int normalCount)
{
    FaceVertex result;
    int part = 0;
    size_t start = 0;
    for (size_t i = 0; i <= token.size(); ++i) {
        if (i != token.size() && token[i] != '/')
            continue;
        if (i > start) {
            int index = std::stoi(token.substr(start, i - start));
            const int count =
                part == 0 ? positionCount :
                (part == 1 ? uvCount : normalCount);
            index = index > 0 ? index - 1 : count + index;
            if (part == 0)
                result.position = index;
            else if (part == 1)
                result.uv = index;
            else
                result.normal = index;
        }
        start = i + 1;
        ++part;
    }
    return result;
}

ParsedObj parse(const std::string& filePath)
{
    std::ifstream input(filePath);
    if (!input)
        OGRE_EXCEPT(Ogre::Exception::ERR_FILE_NOT_FOUND, filePath,
                    "ObjLoader::parse");

    ParsedObj parsed;
    std::string activeMaterial;
    std::string line;
    while (std::getline(input, line)) {
        std::istringstream values(line);
        std::string tag;
        if (!(values >> tag) || tag.empty() || tag[0] == '#')
            continue;

        if (tag == "v") {
            Vert vertex;
            values >> vertex.position.x >> vertex.position.y >>
                vertex.position.z;
            float red, green, blue, alpha;
            if (values >> red >> green >> blue) {
                vertex.colour.r = red;
                vertex.colour.g = green;
                vertex.colour.b = blue;
                if (values >> alpha)
                    vertex.colour.a = alpha;
            }
            parsed.positions.push_back(vertex);
        } else if (tag == "vt") {
            Ogre::Vector2 uv;
            values >> uv.x >> uv.y;
            uv.y = 1.0f - uv.y;
            parsed.uvs.push_back(uv);
        } else if (tag == "vn") {
            Ogre::Vector3 normal;
            values >> normal.x >> normal.y >> normal.z;
            parsed.normals.push_back(normal);
        } else if (tag == "mtllib") {
            std::string library;
            while (values >> library)
                parsed.materialLibraries.push_back(library);
        } else if (tag == "usemtl") {
            std::getline(values >> std::ws, activeMaterial);
        } else if (tag == "f") {
            Face face;
            face.material = activeMaterial;
            std::string token;
            while (values >> token)
                face.vertices.push_back(parseFaceRef(
                    token, int(parsed.positions.size()),
                    int(parsed.uvs.size()), int(parsed.normals.size())));
            parsed.faces.push_back(std::move(face));
        }
    }
    return parsed;
}

std::vector<glm::vec3> sourcePositions(const ParsedObj& parsed)
{
    std::vector<glm::vec3> result;
    result.reserve(parsed.positions.size());
    for (const Vert& vertex : parsed.positions)
        result.push_back({vertex.position.x, vertex.position.y,
                          vertex.position.z});
    return result;
}

void transform(ParsedObj& parsed, const Ogre::Matrix4& bake)
{
    const Ogre::Matrix3 normalMatrix =
        bake.linear().Inverse().Transpose();
    for (Vert& vertex : parsed.positions)
        vertex.position = bake * vertex.position;
    for (Ogre::Vector3& normal : parsed.normals) {
        normal = normalMatrix * normal;
        normal.normalise();
    }
}

ParsedObj parseCanonical(const std::string& filePath,
                         const eng::ModelImportOptions& options)
{
    ParsedObj parsed = parse(filePath);
    const std::vector<glm::vec3> positions =
        sourcePositions(parsed);
    if (positions.empty())
        OGRE_EXCEPT(Ogre::Exception::ERR_INVALIDPARAMS,
                    "OBJ contains no usable positions",
                    "ObjLoader::parseCanonical");
    transform(parsed, toOgre(
        eng::modelImportBakeMatrix(options, positions)));
    return parsed;
}

void captureGeometry(const ParsedObj& parsed,
                     std::vector<glm::vec3>& outVerts,
                     std::vector<uint32_t>& outIndices)
{
    outVerts.clear();
    outIndices.clear();
    outVerts.reserve(parsed.positions.size());
    for (const Vert& vertex : parsed.positions)
        outVerts.push_back({vertex.position.x, vertex.position.y,
                            vertex.position.z});
    for (const Face& face : parsed.faces) {
        std::vector<uint32_t> positions;
        positions.reserve(face.vertices.size());
        for (const FaceVertex& vertex : face.vertices)
            if (vertex.position >= 0 &&
                vertex.position < int(parsed.positions.size()))
                positions.push_back(uint32_t(vertex.position));
        detail::appendTriangleFan(positions, outIndices);
    }
}

void upload(ParsedObj& parsed, const std::string& meshName,
            std::vector<glm::vec3>* outVerts,
            std::vector<uint32_t>* outIndices)
{
    PartialMeshGuard partialMesh(meshName);
    auto manual = std::make_unique<Ogre::ManualObject>(meshName + "_mo");
    // The existing loader intentionally produces one submesh. Parsed material
    // declarations are retained for future indexed remapping, but the current
    // attachment path remains source-compatible.
    manual->begin("BaseWhite", Ogre::RenderOperation::OT_TRIANGLE_LIST);
    Ogre::uint32 nextIndex = 0;

    if (outVerts && outIndices)
        captureGeometry(parsed, *outVerts, *outIndices);
    else {
        if (outVerts) {
            std::vector<uint32_t> ignored;
            captureGeometry(parsed, *outVerts, ignored);
        }
        if (outIndices) {
            std::vector<glm::vec3> ignored;
            captureGeometry(parsed, ignored, *outIndices);
        }
    }

    for (const Face& face : parsed.faces) {
        std::vector<Ogre::uint32> renderFace;
        renderFace.reserve(face.vertices.size());
        for (const FaceVertex& reference : face.vertices) {
            if (reference.position < 0 ||
                reference.position >= int(parsed.positions.size()))
                continue;
            const Vert& vertex = parsed.positions[reference.position];
            manual->position(vertex.position);
            manual->normal(
                reference.normal >= 0 &&
                        reference.normal < int(parsed.normals.size())
                    ? parsed.normals[reference.normal]
                    : Ogre::Vector3::UNIT_Y);
            manual->textureCoord(
                reference.uv >= 0 &&
                        reference.uv < int(parsed.uvs.size())
                    ? parsed.uvs[reference.uv]
                    : Ogre::Vector2::ZERO);
            manual->colour(vertex.colour);
            renderFace.push_back(nextIndex++);
        }
        for (size_t i = 2; i < renderFace.size(); ++i)
            manual->triangle(renderFace[0], renderFace[i - 1],
                             renderFace[i]);
    }
    manual->end();
    manual->convertToMesh(meshName);
    partialMesh.keep = true;
}

} // namespace

namespace detail {

void appendTriangleFan(const std::vector<uint32_t>& face,
                       std::vector<uint32_t>& outIndices)
{
    for (size_t i = 2; i < face.size(); ++i) {
        outIndices.push_back(face[0]);
        outIndices.push_back(face[i - 1]);
        outIndices.push_back(face[i]);
    }
}

} // namespace detail

void load(const std::string& filePath, const std::string& meshName,
          const Ogre::Matrix4& bake,
          std::vector<glm::vec3>* outVerts,
          std::vector<uint32_t>* outIndices)
{
    ParsedObj parsed = parse(filePath);
    transform(parsed, bake);
    upload(parsed, meshName, outVerts, outIndices);
}

void load(const std::string& filePath, const std::string& meshName,
          const eng::ModelImportOptions& options,
          std::vector<glm::vec3>* outVerts,
          std::vector<uint32_t>* outIndices)
{
    ParsedObj parsed = parseCanonical(filePath, options);
    upload(parsed, meshName, outVerts, outIndices);
}

bool loadGeometry(const std::string& filePath,
                  std::vector<glm::vec3>& outVerts,
                  std::vector<uint32_t>& outIndices,
                  const Ogre::Matrix4& bake)
{
    try {
        ParsedObj parsed = parse(filePath);
        transform(parsed, bake);
        captureGeometry(parsed, outVerts, outIndices);
        return true;
    } catch (...) {
        outVerts.clear();
        outIndices.clear();
        return false;
    }
}

bool loadGeometry(const std::string& filePath,
                  std::vector<glm::vec3>& outVerts,
                  std::vector<uint32_t>& outIndices,
    const eng::ModelImportOptions& options)
{
    try {
        ParsedObj parsed = parseCanonical(filePath, options);
        captureGeometry(parsed, outVerts, outIndices);
        return true;
    } catch (...) {
        outVerts.clear();
        outIndices.clear();
        return false;
    }
}

} // namespace ObjLoader
