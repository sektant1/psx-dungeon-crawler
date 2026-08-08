#pragma once

#include <eng/content/MeshData.h>

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace eng {

// Non-flat ground.
//
// Everything this engine drew until now stood on a grid of flat kit pieces, and
// the level generator, the physics and the editor all quietly assumed it: a
// floor is a cell, a cell is at y=0, and a placement is two numbers. That is
// the right shape for a dungeon and the wrong one for the outdoor content the
// built-in library now ships -- a forest of 35 m trees on a flat plane reads as
// a diorama.
//
// A terrain is a heightfield: a square grid of heights, sampled bilinearly, in
// its own local space with the origin at the CENTRE of the patch. It knows how
// to produce its own geometry and nothing about how that geometry is drawn or
// collided with -- Renderer::createMesh takes the one, Physics::createMeshBody
// takes the other, and both already existed.
//
// WHY A MESH BODY RATHER THAN A JOLT HEIGHTFIELD. Jolt has
// HeightFieldShapeSettings and it is the better shape for this: smaller, and it
// can be updated in place while sculpting. `Physics::createMeshBody` is what
// the engine already exposes, is already tested, and costs nothing to use. The
// heightfield shape is worth adding when sculpting needs to re-collide a patch
// per brush stroke rather than per stroke-end; until then a second collision
// path would be a second thing to keep correct.

struct TerrainDesc {
    // Vertices per side. 129 rather than 128: a heightfield wants an odd count
    // so that halving the resolution keeps a vertex on every existing one,
    // which is what makes a coarser LOD or a smaller export line up.
    int resolution = 129;
    // Metres per side. Independent of `resolution`, so detail and extent are
    // separate decisions rather than one number doing both jobs badly.
    float size = 128.0f;
    // Vertical exaggeration applied to the sampled heightmap, in metres. A
    // heightmap is 0..1 by construction; this is what it means.
    float heightScale = 8.0f;
    // Logical path to a greyscale PNG. Empty runs the procedural fallback
    // below, which is what a scene gets before anyone has painted terrain.
    std::string heightmap;

    // Procedural fallback: layered value noise. Deliberately small and
    // deterministic rather than good. It exists so a terrain always has SOME
    // shape -- a flat default reads as a bug in the terrain system, and a
    // programmer looking at rolling ground can tell at a glance that sampling,
    // normals and collision all work.
    uint32_t seed = 1337;
    int octaves = 4;
    float frequency = 2.5f;   // cycles across the whole patch at octave 0
    float roughness = 0.5f;   // amplitude falloff per octave

    // Texture repeats across the whole patch. The built-in ground textures are
    // 64-128 px tiles, so one repeat per patch is a smear and this is not a
    // number anyone should have to discover.
    float uvScale = 24.0f;

    // Flattened to exactly `y` within `radius` metres of these points, easing
    // out over `radius * 2`. This is how a scene gets a level place to stand --
    // a spawn, a plinth, a doorway -- without an editor and without the level
    // author hand-editing a heightmap.
    struct FlatSpot {
        glm::vec2 centre{0.0f};
        float radius = 4.0f;
        float y = 0.0f;
    };
    std::vector<FlatSpot> flatten;
};

class Terrain {
public:
    // False for a malformed descriptor, or a heightmap that names a file which
    // is not there -- NOT for one that is simply absent, which is the
    // procedural case.
    bool build(const TerrainDesc& desc, std::string* error = nullptr);

    bool valid() const { return mResolution > 1; }
    const TerrainDesc& desc() const { return mDesc; }
    int resolution() const { return mResolution; }
    float size() const { return mDesc.size; }
    // Corner-to-corner extent in local space: the patch spans [-half, +half].
    float half() const { return mDesc.size * 0.5f; }

    // Height in metres at a LOCAL x/z, bilinearly interpolated. Outside the
    // patch it clamps to the edge rather than returning zero: a player who
    // walks off the terrain should keep standing on the last thing it knew
    // about, not drop through the world.
    float heightAt(float x, float z) const;
    // The surface normal there, from the same interpolated field, so a slope's
    // shading and the direction a character slides agree.
    glm::vec3 normalAt(float x, float z) const;

    // Raw grid access, row-major, `resolution * resolution` samples. The editor
    // sculpts through these; everything else should use heightAt().
    const std::vector<float>& heights() const { return mHeights; }
    float sample(int column, int row) const;
    void setSample(int column, int row, float height);

    // Raise or lower a disc of the field, with a smooth falloff, and return
    // whether anything changed. The whole of a sculpt brush: strength is metres
    // at the centre, and a negative one digs.
    bool raise(glm::vec2 centre, float radius, float strength);
    // Pull the disc toward the average height under it. Separate from raise()
    // because "make this flatter" is not "add zero".
    bool smooth(glm::vec2 centre, float radius, float strength);

    // Triangles, with normals, UVs and a collision copy. Rebuilt on demand
    // rather than cached: a sculpt invalidates it, and a stale terrain mesh is
    // ground the player can see through.
    content::MeshData geometry(const std::string& material = {}) const;

    // Lowest and highest sample, for a bounding volume and for the editor's
    // "where did my terrain go" answer.
    void heightRange(float& low, float& high) const;

private:
    float noiseAt(float u, float v) const;
    void applyFlatten();

    TerrainDesc mDesc;
    int mResolution = 0;
    std::vector<float> mHeights;
};

// A heightmap PNG, decoded to 0..1 samples on a `resolution` grid. Exposed
// because the editor's terrain import wants it without building a Terrain, and
// because a failure here has to be reportable rather than silently flat.
bool loadHeightmap(const std::string& path, int resolution,
                   std::vector<float>& out, std::string* error = nullptr);

} // namespace eng
