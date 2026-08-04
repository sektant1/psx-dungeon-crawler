// The two non-kit ways for an entity to be geometry: a mesh file named
// directly, and a mesh the engine generates.
//
// Separate from SceneRoundTripTests because these assert on documents built in
// this file rather than on the shipped scenes -- what is checked here is the
// *format*, and it must not start failing because somebody moved a floor tile.
//
// The property that matters is the fixed point: parse -> serialize -> parse
// gives the same document, and only the fields a kind actually reads are
// written. A .scn is committed to git and reviewed like code; a field that
// survives loading but not saving is how a level silently loses a prop.

#include <editor/content/SceneSource.h>
#include <editor/content/SceneWriter.h>

#include <cstdlib>
#include <iostream>
#include <string>

using namespace game::content;

static void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "SceneGeometryTests: " << message << '\n';
        std::exit(1);
    }
}

static SceneDocument parse(const std::string& json, const char* what)
{
    SceneDocument document;
    std::string error;
    if (!parseSceneSource(json, "<memory>", document, error)) {
        std::cerr << "SceneGeometryTests: " << what << ": " << error << '\n';
        std::exit(1);
    }
    return document;
}

int main()
{
    std::string error;
    SceneDocument bad;

    // --- mesh and primitive round-trip -------------------------------------
    //
    // The two non-kit ways to be geometry. Both go through the same
    // parse/serialize fixed point as everything else, because a .scn is
    // reviewed like code and a field that survives loading but not saving is
    // how a level silently loses a prop.
    const std::string geometryJson = R"({
      "format": "raven-scene", "version": 2, "id": "scene.geometry",
      "entities": [
        {"id": "chair", "mesh": {"path": "meshes/props/Chair.obj",
                                 "import_scale": 0.5},
         "material": "Game/PropTerracotta"},
        {"id": "block", "primitive": {"kind": "capsule", "radius": 0.35,
                                      "height": 1.8, "segments": 12,
                                      "inward_facing": true}},
        {"id": "plain_box", "primitive": {}}
      ]})";
    SceneDocument geometry = parse(geometryJson, "geometry");
    const Entity* chair = geometry.find("chair");
    require(chair && chair->mesh, "a mesh entity parses");
    require(chair->mesh->path == "meshes/props/Chair.obj", "with its path");
    require(chair->mesh->importScale == 0.5f, "and its import scale");
    require(chair->material == "Game/PropTerracotta", "and its material");
    require(chair->prefab.empty() && !chair->primitive,
            "and nothing else claiming to be its geometry");

    const Entity* block = geometry.find("block");
    require(block && block->primitive, "a primitive entity parses");
    require(block->primitive->kind == eng::ecs::PrimitiveMesh::Capsule,
            "with its kind, by name");
    require(block->primitive->radius == 0.35f &&
                block->primitive->height == 1.8f,
            "and its dimensions");
    require(block->primitive->segments == 12, "and its tessellation");
    require(block->primitive->inwardFacing, "and its winding");

    // An empty block is a valid primitive: every field is optional and takes
    // the component's own default, so the shortest thing an author can write
    // that means something is `"primitive": {}`.
    const Entity* plain = geometry.find("plain_box");
    require(plain && plain->primitive &&
                plain->primitive->kind == eng::ecs::PrimitiveMesh::Box,
            "an empty primitive block is the unit box");

    const std::string geometryOnce = serializeSceneSource(geometry);
    require(geometryOnce == serializeSceneSource(parse(geometryOnce, "regeom")),
            "mesh and primitive entities serialize idempotently");
    // Only the fields the kind reads are written: a capsule that spelled out a
    // `size` and a `bevel` would present settings that do nothing.
    require(geometryOnce.find("\"bevel\"") == std::string::npos,
            "a capsule does not write a box's bevel");
    require(geometryOnce.find("\"size\"") == std::string::npos,
            "nor a box's size");
    require(geometryOnce.find("\"capsule\"") != std::string::npos,
            "the kind is written by name, not as a number");

    // --- geometry is exclusive ---------------------------------------------
    require(!parseSceneSource(R"({"format":"raven-scene","version":2,
            "id":"x","entities":[{"id":"a","prefab":"kit.wall",
            "primitive":{"kind":"box"}}]})",
                              "<memory>", bad, error),
            "an entity may not be both a kit piece and a primitive");
    require(!parseSceneSource(R"({"format":"raven-scene","version":2,
            "id":"x","entities":[{"id":"a","mesh":{"path":"meshes/a.obj"},
            "primitive":{"kind":"box"}}]})",
                              "<memory>", bad, error),
            "nor both a mesh file and a primitive");
    require(!parseSceneSource(R"({"format":"raven-scene","version":2,
            "id":"x","entities":[{"id":"a","mesh":{"path":"/abs/a.obj"}}]})",
                              "<memory>", bad, error),
            "an absolute mesh path is refused: a map has to travel");
    require(!parseSceneSource(R"({"format":"raven-scene","version":2,
            "id":"x","entities":[{"id":"a","primitive":{"kind":"sphre"}}]})",
                              "<memory>", bad, error),
            "a misspelled primitive kind is an error, not a silent box");


    std::cout << "SceneGeometryTests: ok\n";
    return 0;
}
