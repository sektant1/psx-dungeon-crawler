#include "ProceduralMeshes.h"

#include <Ogre.h>

namespace ProceduralMeshes {

void createInteriorBox(const std::string& meshName, float size, int subdivide)
{
    const int seg = subdivide + 1; // Godot: subdivide = internal subdivisions
    const float half = size * 0.5f;

    auto* mo = new Ogre::ManualObject(meshName + "_mo");
    mo->begin("BaseWhite", Ogre::RenderOperation::OT_TRIANGLE_LIST);

    // Face frame: origin corner, two edge vectors, outward normal.
    struct Face {
        Ogre::Vector3 origin, du, dv, n;
    };
    const Face faces[6] = {
        // +X, -X, +Y, -Y, +Z, -Z
        {{half, -half, half}, {0, 0, -size}, {0, size, 0}, {1, 0, 0}},
        {{-half, -half, -half}, {0, 0, size}, {0, size, 0}, {-1, 0, 0}},
        {{-half, half, half}, {size, 0, 0}, {0, 0, -size}, {0, 1, 0}},
        {{-half, -half, -half}, {size, 0, 0}, {0, 0, size}, {0, -1, 0}},
        {{-half, -half, half}, {size, 0, 0}, {0, size, 0}, {0, 0, 1}},
        {{half, -half, -half}, {-size, 0, 0}, {0, size, 0}, {0, 0, -1}},
    };

    Ogre::uint32 base = 0;
    for (int f = 0; f < 6; ++f) {
        // Godot BoxMesh atlas: each face occupies one cell of a 3x2 UV grid.
        const float u0 = (f % 3) / 3.0f;
        const float v0 = (f / 3) / 2.0f;
        for (int j = 0; j <= seg; ++j) {
            for (int i = 0; i <= seg; ++i) {
                const float fu = (float)i / seg;
                const float fv = (float)j / seg;
                mo->position(faces[f].origin + faces[f].du * fu + faces[f].dv * fv);
                mo->normal(-faces[f].n); // flip_faces: inward normals
                mo->textureCoord(u0 + fu / 3.0f, v0 + fv / 2.0f);
                mo->colour(Ogre::ColourValue::White);
            }
        }
        const int stride = seg + 1;
        for (int j = 0; j < seg; ++j) {
            for (int i = 0; i < seg; ++i) {
                const Ogre::uint32 a = base + j * stride + i;
                const Ogre::uint32 b = a + 1;
                const Ogre::uint32 c = a + stride;
                const Ogre::uint32 d = c + 1;
                // flip_faces: winding reversed relative to an outward box
                mo->triangle(a, c, b);
                mo->triangle(b, c, d);
            }
        }
        base += stride * stride;
    }
    mo->end();
    mo->convertToMesh(meshName);
    delete mo;
}

void createPlane(const std::string& meshName, float size)
{
    const float half = size * 0.5f;
    auto* mo = new Ogre::ManualObject(meshName + "_mo");
    mo->begin("BaseWhite", Ogre::RenderOperation::OT_TRIANGLE_LIST);
    const float xs[2] = {-half, half};
    for (int j = 0; j < 2; ++j) {
        for (int i = 0; i < 2; ++i) {
            mo->position(xs[i], 0.0f, xs[j]);
            mo->normal(0, 1, 0);
            mo->textureCoord((float)i, (float)j);
            mo->colour(Ogre::ColourValue::White);
        }
    }
    // CCW seen from +Y
    mo->triangle(0, 2, 1);
    mo->triangle(1, 2, 3);
    mo->end();
    mo->convertToMesh(meshName);
    delete mo;
}

} // namespace ProceduralMeshes
