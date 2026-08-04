#pragma once
#include <eng/Handles.h>
#include <eng/ecs/components/PrimitiveMesh.h>

#include <entt/entt.hpp>

#include <cstddef>
#include <vector>

namespace eng {

class Renderer;
struct PrimitiveMeshDesc;

namespace ecs {

// Turning a MeshRenderer's *description* of a mesh into a handle.
//
// A MeshRenderer holds a handle and nothing else, deliberately: resolving an
// asset is a load-time job and the hot path must not carry a path. That leaves
// somebody to do the resolving, and there are three somebodies -- the game's
// map loader, the editor's preview bridge and the demo -- which is exactly the
// shape that grows three subtly different answers. The path half already had
// that problem (MapRuntime::resolveMeshes takes a callback because the game and
// the editor disagree about absolute paths); the generated half starts here so
// it never gets one.

// The description a component carries, as the renderer's own descriptor.
// Out-of-range kinds clamp to Box rather than being rejected: a damaged .map
// should draw a wrong mesh, not no mesh, and validPrimitiveMeshDesc still gets
// the last word on the numbers.
PrimitiveMeshDesc primitiveDescOf(const PrimitiveMesh&);

// Generated meshes, shared by description.
//
// A greybox level is a hundred boxes with four distinct sizes between them, and
// createPrimitiveMesh uploads a fresh vertex buffer every call. Keyed by the
// component's bytes because that IS the identity of a generated mesh: two
// entities with equal parameters cannot tell each other's geometry apart.
//
// Not a global. One cache per resolver -- the level's, the editor preview's --
// so tearing a level down releases exactly the meshes that level generated.
class PrimitiveMeshCache
{
public:
    MeshHandle get(Renderer&, const PrimitiveMesh&);
    // Releases every generated mesh. Call before the renderer goes away.
    void clear(Renderer&);
    std::size_t size() const { return mEntries.size(); }

private:
    struct Entry {
        PrimitiveMesh desc;
        MeshHandle mesh;
    };
    std::vector<Entry> mEntries;
};

// True when the two descriptions would generate the same geometry.
bool samePrimitive(const PrimitiveMesh&, const PrimitiveMesh&);

// Fills MeshRenderer::mesh for every entity carrying a PrimitiveMesh, and
// returns how many it resolved.
//
// Skips entities that also carry a MeshSource: a file is the more specific
// statement, and an entity with both is one an author is mid-way through
// converting. Skips entities with no MeshRenderer as well -- what material a
// generated mesh wears is the MeshRenderer's business, and inventing one here
// would put an untextured pink box in a level nobody asked for.
std::size_t resolvePrimitiveMeshes(entt::registry&, Renderer&,
                                   PrimitiveMeshCache&);

} // namespace ecs
} // namespace eng
