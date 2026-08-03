#include "render/ObjLoader.h"

#include <eng/render/ModelImport.h>

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {
void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "ObjGeometryTests: " << message << '\n';
        std::exit(1);
    }
}

bool near(glm::vec3 lhs, glm::vec3 rhs, float epsilon = 0.0001f)
{
    return glm::all(glm::lessThanEqual(glm::abs(lhs - rhs),
                                       glm::vec3(epsilon)));
}
}

int main()
{
    const std::string path = "/tmp/obj_geometry_test_quad.obj";
    {
        std::ofstream out(path);
        out << "v 0 0 0\n"
               "v 1 0 0\n"
               "v 1 1 0\n"
               "v 0 1 0\n"
               "f 1 2 3\n"
               "f 1 3 4\n";
    }

    std::vector<glm::vec3> verts;
    std::vector<uint32_t> idx;
    const bool ok = ObjLoader::loadGeometry(path, verts, idx);
    require(ok, "loadGeometry must return true for a readable file");
    require(verts.size() == 4, "unit quad must yield 4 vertices");
    require(idx.size() == 6, "two triangles must yield 6 indices");
    for (uint32_t i : idx)
        require(i < verts.size(), "every index must be in range");

    std::vector<uint32_t> captured;
    ObjLoader::detail::appendTriangleFan({3, 2, 1, 0}, captured);
    require(captured == std::vector<uint32_t>({3, 2, 1, 3, 1, 0}),
            "collision capture must fan-triangulate in render winding order");

    const std::string asymmetricPath =
        "/tmp/obj_geometry_test_asymmetric.obj";
    {
        std::ofstream out(asymmetricPath);
        out << "v 1 -2 3\n"
               "v 5 -2 3\n"
               "v 1 4 9\n"
               "f 1 2 3\n";
    }
    eng::ModelImportOptions options;
    options.metresPerSourceUnit = 2.0f;
    options.sourceOrientation =
        glm::angleAxis(glm::radians(90.0f), glm::vec3(0, 1, 0));
    verts.clear();
    idx.clear();
    require(ObjLoader::loadGeometry(asymmetricPath, verts, idx, options),
            "option-aware geometry load failed");
    require(verts.size() == 3 && idx.size() == 3,
            "option-aware geometry changed topology");
    glm::vec3 min = verts[0];
    glm::vec3 max = verts[0];
    for (glm::vec3 vertex : verts) {
        min = glm::min(min, vertex);
        max = glm::max(max, vertex);
    }
    require(near({(min.x + max.x) * 0.5f, min.y,
                  (min.z + max.z) * 0.5f},
                 {0.0f, 0.0f, 0.0f}),
            "option-aware stream did not apply BottomCenter after transform");
    require(near(verts[0], {-6.0f, 0.0f, 4.0f}) &&
                near(verts[1], {-6.0f, 0.0f, -4.0f}) &&
                near(verts[2], {6.0f, 12.0f, 4.0f}),
            "cached collision positions differ from canonical literals");

    const std::string sparsePath =
        "/tmp/obj_geometry_test_sparse_45.obj";
    {
        std::ofstream out(sparsePath);
        out << "v 0 0 0\n"
               "v 2 0 0\n"
               "v 0 2 1\n"
               "vn 0 1 0\n"
               "vt 0 0\n"
               "usemtl Imported/SlotZero\n"
               "f 1/1/1 2/1/1 3/1/1\n";
    }
    options.metresPerSourceUnit = 1.0f;
    options.sourceOrientation =
        glm::angleAxis(glm::radians(45.0f), glm::vec3(0, 1, 0));
    verts.clear();
    idx.clear();
    require(ObjLoader::loadGeometry(sparsePath, verts, idx, options),
            "single-parse sparse geometry load failed");
    require(near(verts[0], {-0.7071068f, 0.0f, 0.3535534f}) &&
                near(verts[1], {0.7071068f, 0.0f, -1.0606602f}) &&
                near(verts[2], {0.0f, 2.0f, 1.0606602f}),
            "45-degree sparse pivot used source AABB corners");

    // Optional stress fixture for local source assets. CI remains hermetic;
    // developers can send a converted hero mesh through the exact CPU parser.
    if (const char* externalPath = std::getenv("RAVEN_TEST_OBJ")) {
        verts.clear();
        idx.clear();
        require(ObjLoader::loadGeometry(externalPath, verts, idx),
                "external OBJ stress fixture failed to load");
        require(!verts.empty() && !idx.empty() && idx.size() % 3 == 0,
                "external OBJ produced empty or non-triangular geometry");
        for (uint32_t index : idx)
            require(index < verts.size(),
                    "external OBJ produced an out-of-range index");
        for (glm::vec3 vertex : verts)
            require(std::isfinite(vertex.x) && std::isfinite(vertex.y) &&
                        std::isfinite(vertex.z),
                    "external OBJ produced a non-finite vertex");
        std::cout << "external fixture: " << verts.size() << " vertices, "
                  << idx.size() / 3 << " triangles\n";
    }

    std::cout << "ObjGeometryTests: OK\n";
    return 0;
}
