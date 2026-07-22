#include "ObjLoader.h"

#include <Ogre.h>
#include <glm/glm.hpp>

#include <fstream>
#include <sstream>
#include <vector>

namespace ObjLoader {

namespace {
struct Vert {
    Ogre::Vector3 p;
    Ogre::ColourValue c{1, 1, 1, 1};
};

// "1/2/3" | "1//3" | "1" -> zero-based indices (OBJ is 1-based, may be negative)
void parseFaceRef(const std::string& tok, int nv, int nt, int nn,
                  int& vi, int& ti, int& ni)
{
    vi = ti = ni = -1;
    int part = 0;
    size_t start = 0;
    for (size_t i = 0; i <= tok.size(); ++i) {
        if (i == tok.size() || tok[i] == '/') {
            if (i > start) {
                int idx = std::stoi(tok.substr(start, i - start));
                int count = part == 0 ? nv : (part == 1 ? nt : nn);
                idx = idx > 0 ? idx - 1 : count + idx;
                (part == 0 ? vi : part == 1 ? ti : ni) = idx;
            }
            start = i + 1;
            ++part;
        }
    }
}
} // namespace

void load(const std::string& filePath, const std::string& meshName,
          const Ogre::Matrix4& bake)
{
    std::ifstream in(filePath);
    if (!in)
        OGRE_EXCEPT(Ogre::Exception::ERR_FILE_NOT_FOUND, filePath, "ObjLoader::load");

    const Ogre::Matrix3 nrmMat =
        bake.linear().Inverse().Transpose(); // normals: inverse transpose

    std::vector<Vert> positions;
    std::vector<Ogre::Vector2> uvs;
    std::vector<Ogre::Vector3> normals;

    auto* mo = new Ogre::ManualObject(meshName + "_mo");
    mo->begin("BaseWhite", Ogre::RenderOperation::OT_TRIANGLE_LIST);
    Ogre::uint32 nextIndex = 0;

    std::string line;
    while (std::getline(in, line)) {
        std::istringstream ls(line);
        std::string tag;
        if (!(ls >> tag) || tag.empty() || tag[0] == '#')
            continue;
        if (tag == "v") {
            Vert v;
            ls >> v.p.x >> v.p.y >> v.p.z;
            float r, g, b, a;
            if (ls >> r >> g >> b) {
                v.c.r = r; v.c.g = g; v.c.b = b;
                if (ls >> a) v.c.a = a;
            }
            v.p = bake * v.p;
            positions.push_back(v);
        } else if (tag == "vt") {
            Ogre::Vector2 t;
            ls >> t.x >> t.y;
            t.y = 1.0f - t.y; // Godot OBJ importer convention
            uvs.push_back(t);
        } else if (tag == "vn") {
            Ogre::Vector3 n;
            ls >> n.x >> n.y >> n.z;
            n = nrmMat * n;
            n.normalise();
            normals.push_back(n);
        } else if (tag == "f") {
            std::vector<Ogre::uint32> face;
            std::string tok;
            while (ls >> tok) {
                int vi, ti, ni;
                parseFaceRef(tok, (int)positions.size(), (int)uvs.size(),
                             (int)normals.size(), vi, ti, ni);
                const Vert& v = positions[vi];
                mo->position(v.p);
                mo->normal(ni >= 0 ? normals[ni] : Ogre::Vector3::UNIT_Y);
                mo->textureCoord(ti >= 0 ? uvs[ti] : Ogre::Vector2::ZERO);
                mo->colour(v.c);
                face.push_back(nextIndex++);
            }
            for (size_t i = 2; i < face.size(); ++i) // fan-triangulate polygons
                mo->triangle(face[0], face[i - 1], face[i]);
        }
    }
    mo->end();
    mo->convertToMesh(meshName);
    delete mo;
}

bool loadGeometry(const std::string& filePath,
                  std::vector<glm::vec3>& outVerts,
                  std::vector<uint32_t>& outIndices,
                  const Ogre::Matrix4& bake)
{
    std::ifstream file(filePath);
    if (!file) return false;
    std::vector<Ogre::Vector3> positions;
    std::string line;
    while (std::getline(file, line)) {
        std::istringstream ss(line);
        std::string tag;
        ss >> tag;
        if (tag == "v") {
            float x, y, z;
            ss >> x >> y >> z;
            Ogre::Vector3 p = bake * Ogre::Vector3(x, y, z);
            positions.push_back(p);
        } else if (tag == "f") {
            std::vector<int> vs;
            std::string tok;
            while (ss >> tok) {
                int vi, ti, ni;
                parseFaceRef(tok, int(positions.size()), 0, 0, vi, ti, ni);
                if (vi >= 0) vs.push_back(vi);
            }
            for (size_t i = 2; i < vs.size(); ++i) {
                outIndices.push_back(uint32_t(vs[0]));
                outIndices.push_back(uint32_t(vs[i - 1]));
                outIndices.push_back(uint32_t(vs[i]));
            }
        }
    }
    outVerts.reserve(positions.size());
    for (const Ogre::Vector3& p : positions)
        outVerts.push_back(glm::vec3(p.x, p.y, p.z));
    return true;
}

} // namespace ObjLoader
