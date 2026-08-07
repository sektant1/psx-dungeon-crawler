#include <eng/ecs/MeshResolve.h>

#include <eng/Primitive.h>
#include <eng/Renderer.h>
#include <eng/ecs/components/MeshRenderer.h>
#include <eng/ecs/components/MeshSource.h>

namespace eng::ecs {
namespace {

// The component's Kind and the renderer's PrimitiveKind are two spellings of
// one enumeration -- the component may not name the renderer's, because it
// lives a layer below it. These asserts are what keeps them one enumeration:
// inserting a kind in the middle of either list stops the build here rather
// than turning every authored cylinder in every level into a cone.
static_assert(int(PrimitiveKind::Box) == PrimitiveMesh::Box);
static_assert(int(PrimitiveKind::BeveledBox) == PrimitiveMesh::BeveledBox);
static_assert(int(PrimitiveKind::Sphere) == PrimitiveMesh::Sphere);
static_assert(int(PrimitiveKind::Capsule) == PrimitiveMesh::Capsule);
static_assert(int(PrimitiveKind::Cylinder) == PrimitiveMesh::Cylinder);
static_assert(int(PrimitiveKind::Cone) == PrimitiveMesh::Cone);
static_assert(int(PrimitiveKind::Plane) == PrimitiveMesh::Plane);
static_assert(int(PrimitiveKind::Disc) == PrimitiveMesh::Disc);

} // namespace

PrimitiveMeshDesc primitiveDescOf(const PrimitiveMesh& c)
{
    PrimitiveMeshDesc desc;
    desc.kind = (c.kind >= 0 && c.kind < PrimitiveMesh::KindCount)
                    ? PrimitiveKind(c.kind)
                    : PrimitiveKind::Box;
    desc.size = c.size;
    desc.radius = c.radius;
    desc.height = c.height;
    desc.bevel = c.bevel;
    desc.thickness = c.thickness;
    desc.rings = c.rings;
    desc.segments = c.segments;
    desc.inwardFacing = c.inwardFacing;
    desc.subdivisions = c.subdivisions;
    return desc;
}

bool samePrimitive(const PrimitiveMesh& a, const PrimitiveMesh& b)
{
    // Exact float comparison on purpose. These are authored numbers that
    // round-trip through the same file, so two entities meant to share geometry
    // carry bit-identical values; an epsilon here would merge a 1.000 m crate
    // with a 1.001 m one and make the difference unauthorable.
    return a.kind == b.kind && a.size == b.size && a.radius == b.radius &&
           a.height == b.height && a.bevel == b.bevel &&
           a.thickness == b.thickness && a.rings == b.rings &&
           a.segments == b.segments && a.subdivisions == b.subdivisions &&
           a.inwardFacing == b.inwardFacing;
}

MeshHandle PrimitiveMeshCache::get(Renderer& renderer, const PrimitiveMesh& c)
{
    for (const Entry& entry : mEntries)
        if (samePrimitive(entry.desc, c))
            return entry.mesh;
    // A linear scan, because the distinct descriptions in one level are a
    // handful and hashing a struct of floats to save four comparisons is how a
    // cache acquires a bug nobody can see.
    const MeshHandle mesh = renderer.createPrimitiveMesh(primitiveDescOf(c));
    mEntries.push_back({c, mesh});
    return mesh;
}

void PrimitiveMeshCache::clear(Renderer& renderer)
{
    for (const Entry& entry : mEntries)
        if (entry.mesh.valid())
            renderer.releaseMesh(entry.mesh);
    mEntries.clear();
}

std::size_t resolvePrimitiveMeshes(entt::registry& registry,
                                   Renderer& renderer,
                                   PrimitiveMeshCache& cache,
                                   bool onlyUnresolved)
{
    std::size_t resolved = 0;
    auto view = registry.view<PrimitiveMesh, MeshRenderer>();
    for (const entt::entity entity : view) {
        if (registry.all_of<MeshSource>(entity))
            continue;
        if (onlyUnresolved && view.get<MeshRenderer>(entity).mesh.valid())
            continue;
        const MeshHandle mesh =
            cache.get(renderer, view.get<PrimitiveMesh>(entity));
        if (!mesh.valid())
            continue;
        view.get<MeshRenderer>(entity).mesh = mesh;
        ++resolved;
    }
    return resolved;
}

} // namespace eng::ecs
