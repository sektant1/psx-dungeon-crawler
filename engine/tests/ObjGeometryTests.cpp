#include "ObjLoader.h"

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

    std::cout << "ObjGeometryTests: OK\n";
    return 0;
}
