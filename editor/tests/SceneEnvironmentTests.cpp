// The look a level asks to be lit and graded with.
//
// A palette is a level design decision -- a crypt and a cathedral are not the
// same room with different props -- and it was the one such decision the scene
// format could not carry. The properties worth pinning are that it survives the
// round trip, that it reaches the runtime, and that a scene which does not use
// it cooks to exactly what it cooked to before this existed.

#include <editor/content/KitCatalog.h>
#include <editor/content/SceneCook.h>
#include <editor/content/SceneDocument.h>
#include <editor/content/SceneSource.h>
#include <editor/content/SceneWriter.h>
#include "scene/GameComponents.h"

#include <entt/entt.hpp>

#include <cstdlib>
#include <iostream>
#include <string>

using namespace game::content;

static void require(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "SceneEnvironmentTests: " << message << '\n';
        std::exit(1);
    }
}

static SceneDocument minimalScene()
{
    SceneDocument doc;
    doc.id = "scene.test.environment";
    Entity spawn;
    spawn.id = "player_spawn_0001";
    spawn.playerSpawn = true;
    doc.add(spawn);
    return doc;
}

static int environmentCount(const entt::registry& registry)
{
    int count = 0;
    for (const auto entity : registry.view<const game::SceneEnvironment>()) {
        (void)entity;
        ++count;
    }
    return count;
}

int main()
{
    const KitCatalog catalog;

    // --- a scene that names no palette is untouched -------------------------
    {
        const SceneDocument doc = minimalScene();
        const std::string json = serializeSceneSource(doc);
        require(json.find("palette") == std::string::npos,
                "the field is omitted entirely, so every scene authored before "
                "palettes existed still writes byte for byte");

        entt::registry registry;
        std::string error;
        require(buildRegistry(doc, catalog, registry, error), error);
        require(environmentCount(registry) == 0,
                "and cooks to no extra entity -- which is what keeps the "
                "shipped .map files identical");
    }

    // --- a scene that names one carries it through --------------------------
    {
        SceneDocument doc = minimalScene();
        doc.palette = "showroom";

        const std::string json = serializeSceneSource(doc);
        require(json.find("\"palette\": \"showroom\"") != std::string::npos,
                "the palette is written");

        SceneDocument read;
        std::string error;
        require(parseSceneSource(json, "<test>", read, error), error);
        require(read.palette == "showroom",
                "and read back -- a look that survives one save and not the "
                "next is worse than none");

        entt::registry registry;
        require(buildRegistry(read, catalog, registry, error), error);
        require(environmentCount(registry) == 1,
                "the cook puts it on exactly one entity: a .map is a flat list "
                "and a fact about the whole level needs a carrier");

        for (const auto entity : registry.view<const game::SceneEnvironment>()) {
            require(registry.get<const game::SceneEnvironment>(entity).palette ==
                        "showroom",
                    "carrying the name the scene asked for");
        }
    }

    // --- a palette that is not a string is a format error -------------------
    {
        SceneDocument read;
        std::string error;
        const std::string json =
            R"({"format":"psx-dungeon-scene","version":2,)"
            R"("id":"scene.x","palette":7,"entities":[]})";
        require(!parseSceneSource(json, "<test>", read, error),
                "a non-string palette is rejected rather than silently "
                "becoming empty");
        require(error.find("palette") != std::string::npos,
                "and the error names the field");
    }

    // --- a name the palettes file does not have still opens -----------------
    {
        SceneDocument read;
        std::string error;
        const std::string json =
            R"({"format":"psx-dungeon-scene","version":2,)"
            R"("id":"scene.x","palette":"renamed_away","entities":[]})";
        require(parseSceneSource(json, "<test>", read, error), error);
        require(read.palette == "renamed_away",
                "an unknown palette is kept, not dropped: a scene whose look "
                "was renamed out from under it has to be openable, because "
                "opening it is how the name gets fixed");
    }

    std::cout << "SceneEnvironmentTests: ok\n";
    return 0;
}
