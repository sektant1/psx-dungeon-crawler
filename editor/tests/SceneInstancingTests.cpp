// Scene instancing: an entity that IS another scene.
//
// File-based, because the feature is about one .scn referring to another and
// the only honest test of that is to write the files and expand them.

#include <editor/content/SceneInstancing.h>
#include <editor/content/SceneSource.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

using namespace game::content;
namespace fs = std::filesystem;

static void require(bool c, const char* m)
{
    if (!c) {
        std::cerr << "SceneInstancingTests: " << m << '\n';
        std::exit(1);
    }
}

static fs::path root()
{
    const fs::path dir = fs::temp_directory_path() / "raven_instancing_tests";
    std::error_code ec;
    fs::create_directories(dir / "scenes", ec);
    return dir;
}

static void write(const std::string& name, const std::string& json)
{
    std::ofstream out(root() / name, std::ios::trunc);
    out << json;
}

static SceneDocument load(const std::string& name)
{
    SceneDocument doc;
    std::string error;
    const bool ok = loadSceneSource((root() / name).string(), doc, error);
    require(ok, error.c_str());
    return doc;
}

static const Entity* find(const SceneDocument& doc, const AuthorId& id)
{
    for (const Entity& e : doc.entities)
        if (e.id == id)
            return &e;
    return nullptr;
}

// A torch: a root with a child, so the test covers both the "inner root hangs
// from the placement" and "inner child keeps its own parent" rules.
static void writeTorch()
{
    write("scenes/torch.scn", R"({
  "format": "raven-scene",
  "version": 2,
  "id": "scene.torch",
  "entities": [
    {
      "id": "body",
      "name": "Torch Body",
      "transform": { "position": [0.0, 0.0, 0.0] },
      "primitive": { "kind": "box", "size": [0.2, 1.0, 0.2] }
    },
    {
      "id": "flame",
      "name": "Flame",
      "parent": "body",
      "transform": { "position": [0.0, 1.0, 0.0] },
      "light": { "type": "point", "colour": [1.0, 0.6, 0.2] }
    }
  ]
}
)");
}

static void testExpandsAndNamespaces()
{
    writeTorch();
    write("scenes/level.scn", R"({
  "format": "raven-scene",
  "version": 2,
  "id": "scene.level",
  "entities": [
    {
      "id": "floor",
      "transform": { "position": [0.0, -0.1, 0.0] },
      "primitive": { "kind": "box", "size": [10.0, 0.2, 10.0] }
    },
    {
      "id": "torch_a",
      "name": "Torch A",
      "layer": "props",
      "transform": { "position": [2.0, 0.0, 0.0] },
      "instance": { "scene": "scenes/torch.scn" }
    },
    {
      "id": "torch_b",
      "transform": { "position": [-2.0, 0.0, 0.0] },
      "instance": { "scene": "scenes/torch.scn" }
    }
  ]
}
)");

    SceneDocument doc = load("scenes/level.scn");
    require(doc.entities.size() == 3, "the document holds what was authored");
    require(find(doc, "torch_a")->instance.has_value(), "instance round-trips");

    std::string error;
    require(expandInstances(doc, root().string(), error), error.c_str());

    // floor + two placements + two entities each.
    require(doc.entities.size() == 7, "both placements expanded");

    // Ids are namespaced, so two placements of one scene cannot collide.
    require(find(doc, "torch_a/body") != nullptr, "the inner root is namespaced");
    require(find(doc, "torch_a/flame") != nullptr, "so is the inner child");
    require(find(doc, "torch_b/body") != nullptr, "and the second placement's");

    // The placement survives as the node the contents hang from, and stops
    // being an instance.
    const Entity* placement = find(doc, "torch_a");
    require(placement != nullptr, "the placement survives");
    require(!placement->instance.has_value(),
            "and is no longer an instance -- it has been expanded");
    require(placement->transform.position.x == 2.0f,
            "keeping its transform, which is what moves the whole thing");

    // The parent links are what make it one object rather than loose pieces.
    require(find(doc, "torch_a/body")->parent == "torch_a",
            "an inner root hangs from the placement");
    require(find(doc, "torch_a/flame")->parent == "torch_a/body",
            "an inner child keeps its own parent, namespaced");

    // The layer comes from the placement: hiding the layer a torch is on
    // should hide its flame.
    require(find(doc, "torch_a/flame")->layer == "props",
            "contents inherit the placement's layer");
    require(find(doc, "torch_b/flame")->layer.empty(),
            "and the other placement's, independently");
}

// A scene instanced by a scene that is itself instanced.
static void testNesting()
{
    writeTorch();
    write("scenes/sconce.scn", R"({
  "format": "raven-scene",
  "version": 2,
  "id": "scene.sconce",
  "entities": [
    {
      "id": "bracket",
      "transform": { "position": [0.0, 1.5, 0.0] },
      "primitive": { "kind": "box", "size": [0.3, 0.1, 0.3] }
    },
    {
      "id": "torch",
      "parent": "bracket",
      "transform": { "position": [0.0, 0.1, 0.0] },
      "instance": { "scene": "scenes/torch.scn" }
    }
  ]
}
)");
    write("scenes/hall.scn", R"({
  "format": "raven-scene",
  "version": 2,
  "id": "scene.hall",
  "entities": [
    {
      "id": "wall_light",
      "transform": { "position": [5.0, 0.0, 0.0] },
      "instance": { "scene": "scenes/sconce.scn" }
    }
  ]
}
)");

    SceneDocument doc = load("scenes/hall.scn");
    std::string error;
    require(expandInstances(doc, root().string(), error), error.c_str());

    // placement + bracket + torch placement + body + flame.
    require(doc.entities.size() == 5, "nesting expands all the way down");
    require(find(doc, "wall_light/torch/body") != nullptr,
            "ids namespace once per level of nesting");
    require(find(doc, "wall_light/torch/flame")->parent ==
                "wall_light/torch/body",
            "and parents follow them down");
    require(find(doc, "wall_light/bracket")->parent == "wall_light",
            "the outer root still hangs from its placement");
}

// A scene that instances itself, directly or through another.
static void testCycleIsRefused()
{
    write("scenes/loop_a.scn", R"({
  "format": "raven-scene", "version": 2, "id": "scene.loop_a",
  "entities": [
    { "id": "inner", "instance": { "scene": "scenes/loop_b.scn" } }
  ]
}
)");
    write("scenes/loop_b.scn", R"({
  "format": "raven-scene", "version": 2, "id": "scene.loop_b",
  "entities": [
    { "id": "back", "instance": { "scene": "scenes/loop_a.scn" } }
  ]
}
)");

    SceneDocument doc = load("scenes/loop_a.scn");
    const std::size_t before = doc.entities.size();
    std::string error;
    require(!expandInstances(doc, root().string(), error),
            "a cycle must be refused, not expanded until it runs out of stack");
    require(doc.entities.size() == before,
            "and must leave the document untouched");
    // The message has to name the path: "cycle detected" alone is something an
    // author has to reproduce before they can act on it.
    require(error.find("loop_a") != std::string::npos &&
                error.find("loop_b") != std::string::npos,
            "the error names the scenes in the cycle");
}

static void testMissingSceneIsRefused()
{
    write("scenes/broken.scn", R"({
  "format": "raven-scene", "version": 2, "id": "scene.broken",
  "entities": [
    { "id": "ghost", "instance": { "scene": "scenes/nope.scn" } }
  ]
}
)");

    SceneDocument doc = load("scenes/broken.scn");
    std::string error;
    require(!expandInstances(doc, root().string(), error),
            "a missing scene is refused");
    require(error.find("ghost") != std::string::npos,
            "the error names the entity an author can find");
    require(error.find("nope.scn") != std::string::npos,
            "and the file it was looking for");
}

// A document with no instances is left exactly as it was.
static void testNoInstancesIsUntouched()
{
    write("scenes/plain.scn", R"({
  "format": "raven-scene", "version": 2, "id": "scene.plain",
  "entities": [
    { "id": "floor", "primitive": { "kind": "box", "size": [4.0, 0.2, 4.0] } }
  ]
}
)");

    SceneDocument doc = load("scenes/plain.scn");
    std::string error;
    require(expandInstances(doc, root().string(), error), error.c_str());
    require(doc.entities.size() == 1, "nothing added");
    require(doc.entities[0].id == "floor", "and nothing renamed");
}

// What a scene depends on, for the editor to show and a watcher to use.
static void testInstancedScenes()
{
    writeTorch();
    const std::vector<std::string> deps =
        instancedScenes(load("scenes/hall.scn"), root().string());
    require(std::find(deps.begin(), deps.end(), "scenes/sconce.scn") !=
                deps.end(),
            "direct dependencies are reported");
    require(std::find(deps.begin(), deps.end(), "scenes/torch.scn") !=
                deps.end(),
            "and transitive ones");
}

int main()
{
    testExpandsAndNamespaces();
    testNesting();
    testCycleIsRefused();
    testMissingSceneIsRefused();
    testNoInstancesIsUntouched();
    testInstancedScenes();
    std::puts("SceneInstancingTests: ok");
    return 0;
}
