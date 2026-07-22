#include "ProceduralMeshes.h"

#include <Ogre.h>

#include <algorithm>
#include <cmath>

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
    // Emit independent top and bottom faces. Pool and portal surfaces remain
    // visible from either side without disabling culling for their material.
    for (int face = 0; face < 2; ++face) {
        const float normalY = face == 0 ? 1.0f : -1.0f;
        for (int j = 0; j < 2; ++j) {
            for (int i = 0; i < 2; ++i) {
                mo->position(xs[i], 0.0f, xs[j]);
                mo->normal(0, normalY, 0);
                mo->textureCoord((float)i, (float)j);
                mo->colour(Ogre::ColourValue::White);
            }
        }
    }
    // Winding agrees with each face normal.
    mo->triangle(0, 2, 1);
    mo->triangle(1, 2, 3);
    mo->triangle(4, 5, 6);
    mo->triangle(5, 7, 6);
    mo->end();
    mo->convertToMesh(meshName);
    delete mo;
}

void createBeveledBox(const std::string& meshName, float bevel)
{
    bevel = std::clamp(bevel, 0.01f, 0.24f);
    const float h = 0.5f, core = h - bevel;
    const float axis[4] = {-h, -core, core, h};
    auto* mo = new Ogre::ManualObject(meshName + "_mo");
    mo->begin("BaseWhite", Ogre::RenderOperation::OT_TRIANGLE_LIST);
    struct Face { int fixed, u, v; float sign; };
    const Face faces[6] = {{0,2,1,1},{0,1,2,-1},{1,0,2,1},
                           {1,2,0,-1},{2,0,1,1},{2,1,0,-1}};
    Ogre::uint32 base = 0;
    for (const Face& f : faces) {
        for (int y = 0; y < 4; ++y) for (int x = 0; x < 4; ++x) {
            Ogre::Vector3 p(Ogre::Vector3::ZERO);
            p[f.fixed] = f.sign * h;
            p[f.u] = axis[x]; p[f.v] = axis[y];
            Ogre::Vector3 q(std::clamp(p.x, -core, core),
                            std::clamp(p.y, -core, core),
                            std::clamp(p.z, -core, core));
            Ogre::Vector3 d = p - q;
            d.normalise();
            const Ogre::Vector3 rounded = q + d * bevel;
            mo->position(rounded); mo->normal(d);
            mo->textureCoord(float(x) / 3.0f, 1.0f - float(y) / 3.0f);
            // Slight warm face tint gives hand-painted value separation even
            // with a plain texture and reinforces the faceted silhouette.
            const float value = 0.82f + 0.06f * float(f.fixed);
            mo->colour(Ogre::ColourValue(value, value * 0.94f, value * 0.86f));
        }
        for (int y = 0; y < 3; ++y) for (int x = 0; x < 3; ++x) {
            const Ogre::uint32 a = base + Ogre::uint32(y * 4 + x);
            Ogre::Vector3 u(Ogre::Vector3::ZERO), v(Ogre::Vector3::ZERO);
            u[f.u] = 1.0f; v[f.v] = 1.0f;
            Ogre::Vector3 expected(Ogre::Vector3::ZERO);
            expected[f.fixed] = f.sign;
            const bool currentIsOutward = v.crossProduct(u).dotProduct(expected) > 0;
            if (currentIsOutward) {
                mo->triangle(a, a + 4, a + 1);
                mo->triangle(a + 1, a + 4, a + 5);
            } else {
                mo->triangle(a, a + 1, a + 4);
                mo->triangle(a + 1, a + 5, a + 4);
            }
        }
        base += 16;
    }
    mo->end(); mo->convertToMesh(meshName); delete mo;
}

void createCone(const std::string& meshName, float radius, float height,
                int segments)
{
    segments = std::max(3, segments);
    auto* mo = new Ogre::ManualObject(meshName + "_mo");
    mo->begin("BaseWhite", Ogre::RenderOperation::OT_TRIANGLE_LIST);
    const float half = height * 0.5f;
    for (int i = 0; i < segments; ++i) {
        const float a = float(i) / float(segments) * Ogre::Math::TWO_PI;
        const float b = float(i + 1) / float(segments) * Ogre::Math::TWO_PI;
        const Ogre::Vector3 p0(std::cos(a) * radius, -half,
                              std::sin(a) * radius);
        const Ogre::Vector3 p1(std::cos(b) * radius, -half,
                              std::sin(b) * radius);
        const Ogre::Vector3 tip(0, half, 0);
        Ogre::Vector3 n = (tip - p0).crossProduct(p1 - p0).normalisedCopy();
        Ogre::uint32 k = Ogre::uint32(i * 6);
        for (const auto& v : {p0, p1, tip}) {
            mo->position(v); mo->normal(n); mo->textureCoord(v.y > 0 ? 0.5f : 0.0f,
                                                               v.y > 0 ? 0.0f : 1.0f);
            mo->colour(Ogre::ColourValue::White);
        }
        mo->triangle(k, k + 2, k + 1);
        for (const auto& v : {Ogre::Vector3::ZERO + Ogre::Vector3(0,-half,0), p1, p0}) {
            mo->position(v); mo->normal(0,-1,0); mo->textureCoord(0.5f,0.5f);
            mo->colour(Ogre::ColourValue::White);
        }
        mo->triangle(k + 3, k + 5, k + 4);
    }
    mo->end(); mo->convertToMesh(meshName); delete mo;
}

void createPortalRing(const std::string& meshName, float outerRadius,
                      float innerRadius, float depth, int segments)
{
    segments = std::max(8, segments);
    innerRadius = std::min(innerRadius, outerRadius * 0.9f);
    const float halfDepth = depth * 0.5f;
    auto* mo = new Ogre::ManualObject(meshName + "_mo");
    mo->begin("BaseWhite", Ogre::RenderOperation::OT_TRIANGLE_LIST);
    // Four vertices per angle: front/back outer, front/back inner. The four
    // quad strips form a closed, shadow-safe low-poly stone/magic arch.
    for (int i = 0; i <= segments; ++i) {
        const float u = float(i) / float(segments);
        const float a = u * Ogre::Math::TWO_PI;
        const float x = std::cos(a), y = std::sin(a);
        for (const auto& v : {Ogre::Vector3(x * outerRadius, y * outerRadius, halfDepth),
                              Ogre::Vector3(x * outerRadius, y * outerRadius, -halfDepth),
                              Ogre::Vector3(x * innerRadius, y * innerRadius, halfDepth),
                              Ogre::Vector3(x * innerRadius, y * innerRadius, -halfDepth)}) {
            mo->position(v);
            mo->normal(x, y, 0.0f);
            mo->textureCoord(u, (v.length() - innerRadius) /
                                    std::max(outerRadius - innerRadius, 0.001f));
            mo->colour(Ogre::ColourValue::White);
        }
    }
    for (int i = 0; i < segments; ++i) {
        const Ogre::uint32 a = Ogre::uint32(i * 4), b = a + 4;
        const auto quad = [&](Ogre::uint32 i0, Ogre::uint32 i1,
                              Ogre::uint32 i2, Ogre::uint32 i3) {
            mo->triangle(i0, i1, i2); mo->triangle(i2, i1, i3);
        };
        quad(a, b, a + 2, b + 2);         // front face
        quad(a + 1, a + 3, b + 1, b + 3); // back face
        quad(a, a + 1, b, b + 1);         // outer wall
        quad(a + 2, b + 2, a + 3, b + 3); // inner wall
    }
    mo->end();
    mo->convertToMesh(meshName);
    delete mo;
}

void createPortalDisc(const std::string& meshName, float radius, int segments)
{
    segments = std::max(8, segments);
    auto* mo = new Ogre::ManualObject(meshName + "_mo");
    mo->begin("BaseWhite", Ogre::RenderOperation::OT_TRIANGLE_LIST);
    mo->position(0, 0, 0); mo->normal(0, 0, 1); mo->textureCoord(0.5f, 0.5f);
    mo->colour(Ogre::ColourValue::White);
    for (int i = 0; i <= segments; ++i) {
        const float a = float(i) / float(segments) * Ogre::Math::TWO_PI;
        mo->position(std::cos(a) * radius, std::sin(a) * radius, 0);
        mo->normal(0, 0, 1);
        mo->textureCoord(0.5f + std::cos(a) * 0.5f,
                         0.5f + std::sin(a) * 0.5f);
        mo->colour(Ogre::ColourValue::White);
    }
    for (int i = 0; i < segments; ++i)
        mo->triangle(0, Ogre::uint32(i + 1), Ogre::uint32(i + 2));
    mo->end();
    mo->convertToMesh(meshName);
    delete mo;
}

} // namespace ProceduralMeshes
