#include "PrimitiveGeometry.h"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>

namespace eng::detail {
namespace {

uint32_t vertex(PrimitiveGeometry& geometry, glm::vec3 position,
                glm::vec3 normal, glm::vec2 uv,
                glm::vec4 colour = glm::vec4(1.0f))
{
    geometry.vertices.push_back({position, normal, uv, colour});
    return static_cast<uint32_t>(geometry.vertices.size() - 1);
}

void triangle(PrimitiveGeometry& geometry, uint32_t a, uint32_t b, uint32_t c)
{
    geometry.indices.insert(geometry.indices.end(), {a, b, c});
}

void outwardTriangle(PrimitiveGeometry& geometry, uint32_t a, uint32_t b,
                     uint32_t c)
{
    const PrimitiveVertex& va = geometry.vertices[a];
    const PrimitiveVertex& vb = geometry.vertices[b];
    const PrimitiveVertex& vc = geometry.vertices[c];
    const glm::vec3 face =
        glm::cross(vb.position - va.position, vc.position - va.position);
    const glm::vec3 averageNormal = va.normal + vb.normal + vc.normal;
    if (glm::dot(face, averageNormal) < 0.0f)
        std::swap(b, c);
    triangle(geometry, a, b, c);
}

PrimitiveGeometry boxGeometry(const PrimitiveMeshDesc& desc)
{
    PrimitiveGeometry geometry;
    const glm::vec3 half = desc.size * 0.5f;
    const int segments = desc.subdivisions + 1;
    struct Face {
        glm::vec3 origin;
        glm::vec3 du;
        glm::vec3 dv;
        glm::vec3 normal;
    };
    const Face faces[6] = {
        {{half.x, -half.y, half.z},
         {0, 0, -desc.size.z},
         {0, desc.size.y, 0},
         {1, 0, 0}},
        {{-half.x, -half.y, -half.z},
         {0, 0, desc.size.z},
         {0, desc.size.y, 0},
         {-1, 0, 0}},
        {{-half.x, half.y, half.z},
         {desc.size.x, 0, 0},
         {0, 0, -desc.size.z},
         {0, 1, 0}},
        {{-half.x, -half.y, -half.z},
         {desc.size.x, 0, 0},
         {0, 0, desc.size.z},
         {0, -1, 0}},
        {{-half.x, -half.y, half.z},
         {desc.size.x, 0, 0},
         {0, desc.size.y, 0},
         {0, 0, 1}},
        {{half.x, -half.y, -half.z},
         {-desc.size.x, 0, 0},
         {0, desc.size.y, 0},
         {0, 0, -1}},
    };

    for (size_t faceIndex = 0; faceIndex < 6; ++faceIndex) {
        const Face& face = faces[faceIndex];
        const uint32_t base = static_cast<uint32_t>(geometry.vertices.size());
        const float u0 = float(faceIndex % 3) / 3.0f;
        const float v0 = float(faceIndex / 3) / 2.0f;
        const glm::vec3 normal = desc.inwardFacing ? -face.normal : face.normal;
        for (int y = 0; y <= segments; ++y) {
            for (int x = 0; x <= segments; ++x) {
                const float u = float(x) / float(segments);
                const float v = float(y) / float(segments);
                vertex(geometry, face.origin + face.du * u + face.dv * v,
                       normal, {u0 + u / 3.0f, v0 + v / 2.0f});
            }
        }
        const uint32_t stride = uint32_t(segments + 1);
        for (int y = 0; y < segments; ++y) {
            for (int x = 0; x < segments; ++x) {
                const uint32_t a = base + uint32_t(y) * stride + uint32_t(x);
                const uint32_t b = a + 1;
                const uint32_t c = a + stride;
                const uint32_t d = c + 1;
                if (desc.inwardFacing) {
                    triangle(geometry, a, c, b);
                    triangle(geometry, b, c, d);
                }
                else {
                    triangle(geometry, a, b, c);
                    triangle(geometry, b, d, c);
                }
            }
        }
    }
    return geometry;
}

PrimitiveGeometry beveledBoxGeometry(const PrimitiveMeshDesc& desc)
{
    PrimitiveGeometry geometry;
    const glm::vec3 half = desc.size * 0.5f;
    const glm::vec3 core = half - glm::vec3(desc.bevel);
    const auto axis = [&](int component, int index) {
        const float values[4] = {-half[component], -core[component],
                                 core[component], half[component]};
        return values[index];
    };
    struct Face {
        int fixed, u, v;
        float sign;
    };
    const Face faces[6] = {
        {0, 2, 1, 1},  {0, 1, 2, -1}, {1, 0, 2, 1},
        {1, 2, 0, -1}, {2, 0, 1, 1},  {2, 1, 0, -1},
    };
    for (const Face& face : faces) {
        const uint32_t base = static_cast<uint32_t>(geometry.vertices.size());
        for (int y = 0; y < 4; ++y) {
            for (int x = 0; x < 4; ++x) {
                glm::vec3 p(0.0f);
                p[face.fixed] = face.sign * half[face.fixed];
                p[face.u] = axis(face.u, x);
                p[face.v] = axis(face.v, y);
                const glm::vec3 q = glm::clamp(p, -core, core);
                const glm::vec3 normal = glm::normalize(p - q);
                const float value = 0.82f + 0.06f * float(face.fixed);
                vertex(geometry, q + normal * desc.bevel, normal,
                       {float(x) / 3.0f, 1.0f - float(y) / 3.0f},
                       {value, value * 0.94f, value * 0.86f, 1.0f});
            }
        }
        glm::vec3 u(0.0f), v(0.0f), expected(0.0f);
        u[face.u] = 1.0f;
        v[face.v] = 1.0f;
        expected[face.fixed] = face.sign;
        const bool currentIsOutward =
            glm::dot(glm::cross(v, u), expected) > 0.0f;
        for (int y = 0; y < 3; ++y) {
            for (int x = 0; x < 3; ++x) {
                const uint32_t a = base + uint32_t(y * 4 + x);
                if (currentIsOutward) {
                    triangle(geometry, a, a + 4, a + 1);
                    triangle(geometry, a + 1, a + 4, a + 5);
                }
                else {
                    triangle(geometry, a, a + 1, a + 4);
                    triangle(geometry, a + 1, a + 5, a + 4);
                }
            }
        }
    }
    return geometry;
}

// A slab, not a mathematical plane: two full-size faces separated by
// desc.thickness, with four rims closing the gap.
//
// Zero-thickness quads are what every "paper wall" in this game was made of --
// portal membranes, backing panels, water and lava pools, path strips. Edge on
// they vanish, they z-fight whatever they lie on, and nothing about them reads
// as an object with a side. Giving the primitive a real thickness fixes all of
// those at once, and the faces keep their own 0..1 UVs (a box's atlas-style
// per-face UVs would break the scrolling water/lava and the tiled floor
// materials), so the top surface renders exactly as it did.
//
// thickness == 0 is the one deliberate exception, and it emits a *single* face:
// a billboard, not a slab. Anything drawn flat and camera-facing -- the
// material preview's sprite quad above all -- wants exactly one quad. A slab
// gives it four rim quads that each carry the full 0..1 UV, so the whole sprite
// gets squeezed into a hairline strip along all four borders of the frame, and
// a second coplanar back face that alpha-blends a mirrored copy over the front.
PrimitiveGeometry planeGeometry(const PrimitiveMeshDesc& desc)
{
    PrimitiveGeometry geometry;
    const float halfX = desc.size.x * 0.5f;
    const float halfZ = desc.size.z * 0.5f;
    const float halfY = std::max(desc.thickness, 0.0f) * 0.5f;
    const float xs[2] = {-halfX, halfX};
    const float zs[2] = {-halfZ, halfZ};
    const int faces = halfY > 0.0f ? 2 : 1;
    for (int face = 0; face < faces; ++face) {
        const uint32_t base = static_cast<uint32_t>(geometry.vertices.size());
        const float y = face == 0 ? halfY : -halfY;
        const glm::vec3 normal =
            face == 0 ? glm::vec3(0, 1, 0) : glm::vec3(0, -1, 0);
        for (int v = 0; v < 2; ++v)
            for (int u = 0; u < 2; ++u)
                vertex(geometry, {xs[u], y, zs[v]}, normal,
                       {float(u), float(v)});
        if (face == 0) {
            triangle(geometry, base, base + 2, base + 1);
            triangle(geometry, base + 1, base + 2, base + 3);
        }
        else {
            triangle(geometry, base, base + 1, base + 2);
            triangle(geometry, base + 1, base + 3, base + 2);
        }
    }
    if (halfY <= 0.0f)
        return geometry;

    // Rims. Each is a quad spanning the slab's height, wound outward, with its
    // U running along the edge so a tiling material does not smear across it.
    struct Rim {
        glm::vec3 a, b, normal;
    };
    const Rim rims[4] = {
        {{-halfX, 0.0f, halfZ}, {halfX, 0.0f, halfZ}, {0, 0, 1}},
        {{halfX, 0.0f, -halfZ}, {-halfX, 0.0f, -halfZ}, {0, 0, -1}},
        {{halfX, 0.0f, halfZ}, {halfX, 0.0f, -halfZ}, {1, 0, 0}},
        {{-halfX, 0.0f, -halfZ}, {-halfX, 0.0f, halfZ}, {-1, 0, 0}},
    };
    for (const Rim& rim : rims) {
        const uint32_t base = static_cast<uint32_t>(geometry.vertices.size());
        vertex(geometry, {rim.a.x, halfY, rim.a.z}, rim.normal, {0.0f, 0.0f});
        vertex(geometry, {rim.b.x, halfY, rim.b.z}, rim.normal, {1.0f, 0.0f});
        vertex(geometry, {rim.a.x, -halfY, rim.a.z}, rim.normal, {0.0f, 1.0f});
        vertex(geometry, {rim.b.x, -halfY, rim.b.z}, rim.normal, {1.0f, 1.0f});
        // Wound so that cross(b - a, c - a) points along the rim normal, which
        // is the convention the rest of these generators (and the winding test)
        // hold to.
        triangle(geometry, base, base + 2, base + 1);
        triangle(geometry, base + 1, base + 2, base + 3);
    }
    return geometry;
}

PrimitiveGeometry sphereGeometry(const PrimitiveMeshDesc& desc)
{
    PrimitiveGeometry geometry;
    // Poles are explicit fans. Only non-pole latitudes participate in quad
    // strips, avoiding duplicate pole vertices and ambiguous pole winding.
    const uint32_t ringBase = static_cast<uint32_t>(geometry.vertices.size());
    for (int y = 1; y < desc.rings; ++y) {
        const float v = float(y) / float(desc.rings);
        const float phi = v * glm::pi<float>();
        for (int x = 0; x <= desc.segments; ++x) {
            const float u = float(x) / float(desc.segments);
            const float theta = u * glm::two_pi<float>();
            const glm::vec3 normal(std::sin(phi) * std::cos(theta),
                                   std::cos(phi),
                                   std::sin(phi) * std::sin(theta));
            vertex(geometry, normal * desc.radius, normal, {u, v});
        }
    }
    const uint32_t stride = uint32_t(desc.segments + 1);
    for (int y = 0; y < desc.rings - 2; ++y) {
        for (int x = 0; x < desc.segments; ++x) {
            const uint32_t a = ringBase + uint32_t(y) * stride + uint32_t(x);
            const uint32_t b = a + stride;
            outwardTriangle(geometry, a, a + 1, b);
            outwardTriangle(geometry, a + 1, b + 1, b);
        }
    }

    const uint32_t bottomRing = ringBase + uint32_t(desc.rings - 2) * stride;
    for (int x = 0; x < desc.segments; ++x) {
        const float u0 = float(x) / float(desc.segments);
        const float u1 = float(x + 1) / float(desc.segments);
        const uint32_t topPole = vertex(geometry, {0, desc.radius, 0},
                                        {0, 1, 0}, {(u0 + u1) * 0.5f, 0});
        outwardTriangle(geometry, topPole, ringBase + uint32_t(x),
                        ringBase + uint32_t(x + 1));

        const uint32_t bottomPole = vertex(geometry, {0, -desc.radius, 0},
                                           {0, -1, 0}, {(u0 + u1) * 0.5f, 1});
        outwardTriangle(geometry, bottomPole, bottomRing + uint32_t(x + 1),
                        bottomRing + uint32_t(x));
    }
    return geometry;
}

uint32_t emitHemisphereRings(PrimitiveGeometry& geometry, float radius,
                             float centerY, float angleBegin, float angleEnd,
                             int firstRing, int lastRing, int rings,
                             int segments, float vBegin, float vEnd)
{
    const uint32_t base = static_cast<uint32_t>(geometry.vertices.size());
    for (int y = firstRing; y <= lastRing; ++y) {
        const float t = float(y) / float(rings);
        const float angle = angleBegin + (angleEnd - angleBegin) * t;
        const float v = vBegin + (vEnd - vBegin) * t;
        for (int x = 0; x <= segments; ++x) {
            const float u = float(x) / float(segments);
            const float theta = u * glm::two_pi<float>();
            const glm::vec3 normal(std::cos(angle) * std::cos(theta),
                                   std::sin(angle),
                                   std::cos(angle) * std::sin(theta));
            vertex(geometry, normal * radius + glm::vec3(0, centerY, 0), normal,
                   {u, v});
        }
    }
    const uint32_t stride = uint32_t(segments + 1);
    const int emittedRows = lastRing - firstRing + 1;
    for (int y = 0; y < emittedRows - 1; ++y) {
        for (int x = 0; x < segments; ++x) {
            const uint32_t a = base + uint32_t(y) * stride + uint32_t(x);
            const uint32_t b = a + stride;
            outwardTriangle(geometry, a, a + 1, b);
            outwardTriangle(geometry, a + 1, b + 1, b);
        }
    }
    return base;
}

PrimitiveGeometry capsuleGeometry(const PrimitiveMeshDesc& desc)
{
    PrimitiveGeometry geometry;
    const float halfStraight = desc.height * 0.5f;
    const float capArc = glm::half_pi<float>() * desc.radius;
    const float totalVLength = desc.height + capArc * 2.0f;
    const float bottomV = capArc / totalVLength;
    const float topV = (capArc + desc.height) / totalVLength;
    const int hemisphereRingCount = std::max(2, (desc.rings + 1) / 2);

    // Bottom fan + non-pole rings through the equator.
    const uint32_t bottomHemisphere = emitHemisphereRings(
        geometry, desc.radius, -halfStraight, -glm::half_pi<float>(), 0.0f, 1,
        hemisphereRingCount, hemisphereRingCount, desc.segments, 0.0f, bottomV);
    for (int x = 0; x < desc.segments; ++x) {
        const float u0 = float(x) / float(desc.segments);
        const float u1 = float(x + 1) / float(desc.segments);
        const uint32_t pole =
            vertex(geometry, {0, -halfStraight - desc.radius, 0}, {0, -1, 0},
                   {(u0 + u1) * 0.5f, 0});
        outwardTriangle(geometry, pole, bottomHemisphere + uint32_t(x + 1),
                        bottomHemisphere + uint32_t(x));
    }

    const uint32_t cylinderBase =
        static_cast<uint32_t>(geometry.vertices.size());
    for (int row = 0; row < 2; ++row) {
        const float y = row == 0 ? -halfStraight : halfStraight;
        const float v = row == 0 ? bottomV : topV;
        for (int x = 0; x <= desc.segments; ++x) {
            const float u = float(x) / float(desc.segments);
            const float theta = u * glm::two_pi<float>();
            const glm::vec3 normal(std::cos(theta), 0.0f, std::sin(theta));
            vertex(geometry, normal * desc.radius + glm::vec3(0, y, 0), normal,
                   {u, v});
        }
    }
    const uint32_t stride = uint32_t(desc.segments + 1);
    for (int x = 0; x < desc.segments; ++x) {
        const uint32_t a = cylinderBase + uint32_t(x);
        const uint32_t b = a + stride;
        outwardTriangle(geometry, a, b, a + 1);
        outwardTriangle(geometry, a + 1, b, b + 1);
    }

    // A separately emitted equator gives the hemisphere and straight strip
    // independent topology while retaining identical normals and UVs.
    const uint32_t topHemisphere =
        emitHemisphereRings(geometry, desc.radius, halfStraight, 0.0f,
                            glm::half_pi<float>(), 0, hemisphereRingCount - 1,
                            hemisphereRingCount, desc.segments, topV, 1.0f);
    const uint32_t lastTopRing =
        topHemisphere + uint32_t(hemisphereRingCount - 1) * stride;
    for (int x = 0; x < desc.segments; ++x) {
        const float u0 = float(x) / float(desc.segments);
        const float u1 = float(x + 1) / float(desc.segments);
        const uint32_t pole =
            vertex(geometry, {0, halfStraight + desc.radius, 0}, {0, 1, 0},
                   {(u0 + u1) * 0.5f, 1});
        outwardTriangle(geometry, pole, lastTopRing + uint32_t(x),
                        lastTopRing + uint32_t(x + 1));
    }
    return geometry;
}

PrimitiveGeometry cylinderGeometry(const PrimitiveMeshDesc& desc)
{
    PrimitiveGeometry geometry;
    const float half = desc.height * 0.5f;
    for (int i = 0; i < desc.segments; ++i) {
        const float u0 = float(i) / float(desc.segments);
        const float u1 = float(i + 1) / float(desc.segments);
        const float a = u0 * glm::two_pi<float>();
        const float b = u1 * glm::two_pi<float>();
        const glm::vec3 n0(std::cos(a), 0, std::sin(a));
        const glm::vec3 n1(std::cos(b), 0, std::sin(b));
        const glm::vec3 bottom0 = n0 * desc.radius + glm::vec3(0, -half, 0);
        const glm::vec3 bottom1 = n1 * desc.radius + glm::vec3(0, -half, 0);
        const glm::vec3 top0 = n0 * desc.radius + glm::vec3(0, half, 0);
        const glm::vec3 top1 = n1 * desc.radius + glm::vec3(0, half, 0);

        uint32_t base = static_cast<uint32_t>(geometry.vertices.size());
        vertex(geometry, bottom0, n0, {u0, 1});
        vertex(geometry, bottom1, n1, {u1, 1});
        vertex(geometry, top0, n0, {u0, 0});
        vertex(geometry, top1, n1, {u1, 0});
        triangle(geometry, base, base + 2, base + 1);
        triangle(geometry, base + 1, base + 2, base + 3);

        base = static_cast<uint32_t>(geometry.vertices.size());
        vertex(geometry, {0, half, 0}, {0, 1, 0}, {0.5f, 0.5f});
        vertex(geometry, top1, {0, 1, 0},
               {0.5f + top1.x / (2 * desc.radius),
                0.5f + top1.z / (2 * desc.radius)});
        vertex(geometry, top0, {0, 1, 0},
               {0.5f + top0.x / (2 * desc.radius),
                0.5f + top0.z / (2 * desc.radius)});
        triangle(geometry, base, base + 1, base + 2);

        base = static_cast<uint32_t>(geometry.vertices.size());
        vertex(geometry, {0, -half, 0}, {0, -1, 0}, {0.5f, 0.5f});
        vertex(geometry, bottom0, {0, -1, 0},
               {0.5f + bottom0.x / (2 * desc.radius),
                0.5f + bottom0.z / (2 * desc.radius)});
        vertex(geometry, bottom1, {0, -1, 0},
               {0.5f + bottom1.x / (2 * desc.radius),
                0.5f + bottom1.z / (2 * desc.radius)});
        triangle(geometry, base, base + 1, base + 2);
    }
    return geometry;
}

PrimitiveGeometry coneGeometry(const PrimitiveMeshDesc& desc)
{
    PrimitiveGeometry geometry;
    const float half = desc.height * 0.5f;
    const glm::vec3 tip(0, half, 0);
    for (int i = 0; i < desc.segments; ++i) {
        const float u0 = float(i) / float(desc.segments);
        const float u1 = float(i + 1) / float(desc.segments);
        const float a = u0 * glm::two_pi<float>();
        const float b = u1 * glm::two_pi<float>();
        const glm::vec3 p0(std::cos(a) * desc.radius, -half,
                           std::sin(a) * desc.radius);
        const glm::vec3 p1(std::cos(b) * desc.radius, -half,
                           std::sin(b) * desc.radius);
        const glm::vec3 normal = glm::normalize(glm::cross(tip - p0, p1 - p0));

        uint32_t base = static_cast<uint32_t>(geometry.vertices.size());
        vertex(geometry, p0, normal, {u0, 1});
        vertex(geometry, tip, normal, {(u0 + u1) * 0.5f, 0});
        vertex(geometry, p1, normal, {u1, 1});
        triangle(geometry, base, base + 1, base + 2);

        base = static_cast<uint32_t>(geometry.vertices.size());
        vertex(geometry, {0, -half, 0}, {0, -1, 0}, {0.5f, 0.5f});
        vertex(
            geometry, p0, {0, -1, 0},
            {0.5f + p0.x / (2 * desc.radius), 0.5f + p0.z / (2 * desc.radius)});
        vertex(
            geometry, p1, {0, -1, 0},
            {0.5f + p1.x / (2 * desc.radius), 0.5f + p1.z / (2 * desc.radius)});
        triangle(geometry, base, base + 1, base + 2);
    }
    return geometry;
}

PrimitiveGeometry discGeometry(const PrimitiveMeshDesc& desc)
{
    PrimitiveGeometry geometry;
    for (int face = 0; face < 2; ++face) {
        const glm::vec3 normal =
            face == 0 ? glm::vec3(0, 1, 0) : glm::vec3(0, -1, 0);
        const uint32_t base = static_cast<uint32_t>(geometry.vertices.size());
        vertex(geometry, {0, 0, 0}, normal, {0.5f, 0.5f});
        for (int i = 0; i <= desc.segments; ++i) {
            const float angle =
                float(i) / float(desc.segments) * glm::two_pi<float>();
            const float x = std::cos(angle);
            const float z = std::sin(angle);
            vertex(geometry, {x * desc.radius, 0, z * desc.radius}, normal,
                   {0.5f + x * 0.5f, 0.5f + z * 0.5f});
        }
        for (int i = 0; i < desc.segments; ++i) {
            if (face == 0)
                triangle(geometry, base, base + uint32_t(i + 2),
                         base + uint32_t(i + 1));
            else
                triangle(geometry, base, base + uint32_t(i + 1),
                         base + uint32_t(i + 2));
        }
    }
    return geometry;
}

} // namespace

std::optional<PrimitiveGeometry>
buildPrimitiveGeometry(const PrimitiveMeshDesc& desc)
{
    if (!validPrimitiveMeshDesc(desc))
        return std::nullopt;
    const auto generator = primitiveMeshGenerator(desc.kind);
    if (!generator)
        return std::nullopt;
    switch (*generator) {
    case PrimitiveMeshGenerator::Box:
        return boxGeometry(desc);
    case PrimitiveMeshGenerator::BeveledBox:
        return beveledBoxGeometry(desc);
    case PrimitiveMeshGenerator::Sphere:
        return sphereGeometry(desc);
    case PrimitiveMeshGenerator::Capsule:
        return capsuleGeometry(desc);
    case PrimitiveMeshGenerator::Cylinder:
        return cylinderGeometry(desc);
    case PrimitiveMeshGenerator::Cone:
        return coneGeometry(desc);
    case PrimitiveMeshGenerator::Plane:
        return planeGeometry(desc);
    case PrimitiveMeshGenerator::Disc:
        return discGeometry(desc);
    }
    return std::nullopt;
}

} // namespace eng::detail

namespace eng {

bool validPrimitiveMeshDesc(const PrimitiveMeshDesc& desc)
{
    const bool finiteSize = std::isfinite(desc.size.x) &&
                            std::isfinite(desc.size.y) &&
                            std::isfinite(desc.size.z);
    // A flat plane is a legal primitive -- planeGeometry emits one quad for it.
    // Every other kind still needs a real thickness: a zero-thickness disc is a
    // degenerate cylinder, not a billboard.
    const float minThickness = desc.kind == PrimitiveKind::Plane ? 0.0f : 1e-6f;
    if (!finiteSize ||
        glm::any(glm::lessThanEqual(desc.size, glm::vec3(0.0f))) ||
        !std::isfinite(desc.radius) || desc.radius <= 0.0f ||
        !std::isfinite(desc.height) || desc.height <= 0.0f ||
        !std::isfinite(desc.bevel) || desc.bevel <= 0.0f ||
        !std::isfinite(desc.thickness) || desc.thickness < minThickness ||
        desc.rings < 3 || desc.segments < 3 || desc.subdivisions < 0)
        return false;

    if ((desc.inwardFacing || desc.subdivisions != 0) &&
        desc.kind != PrimitiveKind::Box)
        return false;

    if (desc.kind == PrimitiveKind::BeveledBox) {
        const float halfSmallest =
            0.5f * std::min({desc.size.x, desc.size.y, desc.size.z});
        if (desc.bevel >= halfSmallest)
            return false;
    }

    switch (desc.kind) {
    case PrimitiveKind::Box:
    case PrimitiveKind::BeveledBox:
    case PrimitiveKind::Sphere:
    case PrimitiveKind::Capsule:
    case PrimitiveKind::Cylinder:
    case PrimitiveKind::Cone:
    case PrimitiveKind::Plane:
    case PrimitiveKind::Disc:
        return true;
    }
    return false;
}

namespace detail {

std::optional<PrimitiveMeshGenerator> primitiveMeshGenerator(PrimitiveKind kind)
{
    switch (kind) {
    case PrimitiveKind::Box:
        return PrimitiveMeshGenerator::Box;
    case PrimitiveKind::BeveledBox:
        return PrimitiveMeshGenerator::BeveledBox;
    case PrimitiveKind::Sphere:
        return PrimitiveMeshGenerator::Sphere;
    case PrimitiveKind::Capsule:
        return PrimitiveMeshGenerator::Capsule;
    case PrimitiveKind::Cylinder:
        return PrimitiveMeshGenerator::Cylinder;
    case PrimitiveKind::Cone:
        return PrimitiveMeshGenerator::Cone;
    case PrimitiveKind::Plane:
        return PrimitiveMeshGenerator::Plane;
    case PrimitiveKind::Disc:
        return PrimitiveMeshGenerator::Disc;
    }
    return std::nullopt;
}

} // namespace detail

} // namespace eng
