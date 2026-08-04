#pragma once
#include <glm/glm.hpp>

#include <string_view>

namespace eng::ecs {

// A mesh the engine generates from parameters instead of loading from a file.
//
// The renderer has been able to build these since `eng::PrimitiveMeshDesc`
// existed, but only from C++: a box in a level meant a .obj on disk, and the
// eight generators were reachable from gameplay code and from nowhere an author
// could see. This is the same description as a component, so a crate, a
// collision blocker or a greybox pillar is authored, saved, cooked and inspected
// like any other mesh.
//
// It sits *beside* MeshRenderer rather than replacing it: MeshRenderer still
// says what material to wear and whether to cast a shadow, and still holds the
// resolved handle. What differs is only where the handle comes from -- a
// MeshSource resolves a path, a PrimitiveMesh generates geometry (see
// eng/ecs/MeshResolve.h). An entity carrying both is a mesh file; the path wins,
// because a file is the more specific statement.
//
// Every field is POD and the kind is an int, following Orbit::facing: the
// reflection layer's field types are the ones a byte stream and an ImGui widget
// both understand, and an enum that serialises as an int cannot acquire a value
// the reader has never heard of.
struct PrimitiveMesh {
    // Mirrors eng::PrimitiveKind value for value -- MeshResolve.cpp static_asserts
    // that they agree, so the two cannot drift apart silently. Held here as
    // well as there because a component may not name a renderer type: this
    // header is in the framework layer and eng/Primitive.h is in systems.
    enum Kind : int {
        Box = 0,
        BeveledBox = 1,
        Sphere = 2,
        Capsule = 3,
        Cylinder = 4,
        Cone = 5,
        Plane = 6,
        Disc = 7,
        KindCount = 8,
    };

    int kind = Box;

    // Which fields matter depends on the kind, and the generators ignore the
    // rest. Defaults are eng::PrimitiveMeshDesc's, so a component added with no
    // edits produces the unit box the renderer has always produced.
    glm::vec3 size{1.0f}; // Box, BeveledBox, Plane
    float radius = 0.5f;  // Sphere, Capsule, Cylinder, Cone, Disc
    float height = 1.0f;  // Capsule, Cylinder, Cone
    float bevel = 0.12f;  // BeveledBox
    float thickness = 0.05f;
    int rings = 12;    // latitude bands on a sphere/capsule cap
    int segments = 16; // radial segments on anything round
    int subdivisions = 0;
    // Flips the winding and the normals, for a box used as a room rather than
    // as a crate. The one field that changes what a kind *means* rather than
    // how big it is.
    bool inwardFacing = false;
};

// The author-facing name of a kind, and its inverse. One table, so the
// inspector combo, the .scn field and the validator cannot disagree about what
// "beveled_box" is. An unknown name yields PrimitiveMesh::Box, and an
// out-of-range kind yields "box": a damaged file stays openable.
//
// Inline because the callers span three link targets -- the renderer-free
// content library, the headless cooker and the editor -- and a one-line table
// is not worth a fourth translation unit in each of them.
inline const char* const* primitiveKindNames()
{
    static const char* const kNames[PrimitiveMesh::KindCount] = {
        "box",      "beveled_box", "sphere", "capsule",
        "cylinder", "cone",        "plane",  "disc",
    };
    return kNames;
}

inline const char* primitiveKindName(int kind)
{
    if (kind < 0 || kind >= PrimitiveMesh::KindCount)
        return "box";
    return primitiveKindNames()[kind];
}

inline int primitiveKindFromName(std::string_view name)
{
    for (int i = 0; i < PrimitiveMesh::KindCount; ++i)
        if (name == primitiveKindNames()[i])
            return i;
    return PrimitiveMesh::Box;
}

} // namespace eng::ecs
