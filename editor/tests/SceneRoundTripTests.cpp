// .scn is committed to git and reviewed like code, so the writer has to be
// canonical: same document in, same bytes out, and a one-field edit produces a
// one-line diff. These tests are what make that a build failure instead of a
// review comment.

#include <editor/content/SceneSource.h>
#include <editor/content/SceneWriter.h>
#include "TestAssets.h"

#include <cstdlib>
#include <iostream>
#include <string>

using namespace game::content;

static void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "SceneRoundTripTests: " << message << '\n';
        std::exit(1);
    }
}

static SceneDocument parse(const std::string& json, const char* what)
{
    SceneDocument document;
    std::string error;
    if (!parseSceneSource(json, "<memory>", document, error)) {
        std::cerr << "SceneRoundTripTests: " << what << ": " << error << '\n';
        std::exit(1);
    }
    return document;
}

static int countLines(const std::string& text)
{
    int lines = 1;
    for (char c : text)
        if (c == '\n') ++lines;
    return lines;
}

int main()
{
    game::test::mountGameAssets();
    // --- the real shipped scene -------------------------------------------
    SceneDocument shipped;
    std::string error;
    require(loadSceneSource(game::test::asset("scenes/ritual_boss_showroom.scn"), shipped, error),
            ("the shipped scene loads: " + error).c_str());
    require(shipped.id == "scene.test.ritual_boss_showroom", "scene id");
    require(shipped.entities.size() > 20, "it has its entities");

    // Idempotence: serialize -> parse -> serialize is a fixed point. (Not
    // byte-equality with the hand-written file, which is formatted by a human.)
    const std::string once = serializeSceneSource(shipped);
    const std::string twice = serializeSceneSource(parse(once, "reparse"));
    require(once == twice, "serialize is idempotent from the second write on");

    // The author's data survives the trip.
    const SceneDocument reparsed = parse(once, "reparse");
    require(reparsed.entities.size() == shipped.entities.size(),
            "no entity is lost");
    const Entity* spawn = reparsed.find("player_spawn");
    require(spawn && spawn->playerSpawn, "player_spawn survives");
    require(spawn->transform.position.z == 26.0f, "its position survives");
    const Entity* boss = reparsed.find("boss_spawn");
    require(boss && boss->marker && *boss->marker == "boss.spawn",
            "markers survive");
    const Entity* exit = reparsed.find("descent_exit");
    require(exit && exit->exitYawDegrees, "the exit survives");

    // PreviewBridge only draws prefab-backed meshes. Keep both showcase
    // subjects explicit: a bare Exit gets generated portal art in play mode,
    // but remains only a marker in the editor viewport.
    SceneDocument spinPortal;
    require(loadSceneSource(game::test::asset("scenes/spin_portal.scn"),
                            spinPortal, error),
            ("the spin portal scene loads: " + error).c_str());
    const Entity* raccoon = spinPortal.find("prop_raccoon");
    require(raccoon && raccoon->prefab == "kit.prop_raccoon_head",
            "the raccoon head has an editor-preview mesh");
    const Entity* portal = spinPortal.find("portal_membrane_0001");
    require(portal && portal->prefab == "kit.portal_membrane" &&
                portal->portal,
            "the portal membrane has an editor-preview mesh and shader params");

    SceneDocument cozyLair;
    require(loadSceneSource(game::test::asset("scenes/cozy_lair.scn"),
                            cozyLair, error),
            ("the cozy lair scene loads: " + error).c_str());
    int floors = 0;
    for (const Entity& entity : cozyLair.entities)
        if (entity.prefab == "kit.floor")
            ++floors;
    require(floors == 4, "the cozy lair is a compact 2x2 stage");
    const Entity* subjectPivot = cozyLair.find("subject_pivot");
    require(subjectPivot && subjectPivot->spin &&
                subjectPivot->spin->degreesPerSecond == 12.0f,
            "the centred inspection pivot turns in place");
    const Entity* subject = cozyLair.find("subject");
    require(subject && subject->prefab == "kit.prop_boss_placeholder" &&
                subject->parent == "subject_pivot" && !subject->spin &&
                subject->shader && subject->transform.position.y > 0.0f,
            "the imported boss reference is centred and grounded on the pivot");
    require(!cozyLair.find("subject_child"),
            "prefab attachments do not leak into authored scene entities");
    int stageLights = 0;
    for (const Entity& entity : cozyLair.entities)
        if (entity.id.starts_with("stage_") && entity.light)
            ++stageLights;
    require(stageLights == 3, "the compact stage has key, fill, and rim lights");
    const Entity* camera = cozyLair.find("camera_main");
    require(camera && camera->camera && !camera->spin && !camera->orbit,
            "the cozy lair camera is static");
    const Entity* lairPortal = cozyLair.find("portal");
    require(lairPortal && lairPortal->portal &&
                lairPortal->exitYawDegrees &&
                lairPortal->prefab == "kit.portal_membrane",
            "the lair portal is visible, tunable, and usable as its exit");

    // --- diff friendliness -------------------------------------------------
    SceneDocument nudged = reparsed;
    nudged.find("boss_spawn")->transform.position.x = 1.5f;
    const std::string after = serializeSceneSource(nudged);
    require(after != once, "a real edit changes the file");
    int differing = 0;
    {
        std::size_t a = 0, b = 0;
        while (a < once.size() || b < after.size()) {
            const std::size_t ea = once.find('\n', a);
            const std::size_t eb = after.find('\n', b);
            const std::string la = once.substr(a, ea - a);
            const std::string lb = after.substr(b, eb - b);
            if (la != lb) ++differing;
            if (ea == std::string::npos || eb == std::string::npos) break;
            a = ea + 1;
            b = eb + 1;
        }
    }
    require(countLines(once) == countLines(after),
            "moving a thing does not reflow the file");
    require(differing <= 2, "and touches at most a couple of lines");

    // --- canonical form ----------------------------------------------------
    SceneDocument tiny;
    tiny.id = "scene.tiny";
    Entity noisy;
    noisy.id = "b_thing";
    noisy.name = "b_thing";                    // same as id: omitted
    noisy.transform.scale = glm::vec3(1.0f);   // default: omitted
    noisy.transform.position = {2.00000012f, 0.0f, -0.0f}; // rounds, kills -0
    tiny.add(noisy);
    Entity first;
    first.id = "a_thing";
    first.marker = "test.marker";
    tiny.add(first);

    const std::string text = serializeSceneSource(tiny);
    require(text.find("\"scale\"") == std::string::npos,
            "a default scale is not written");
    require(text.find("\"name\"") == std::string::npos,
            "a name equal to the id is not written");
    require(text.find("-0.0") == std::string::npos, "negative zero is normalised");
    require(text.find("2.0000001") == std::string::npos, "floats are rounded");
    require(text.find("\"a_thing\"") < text.find("\"b_thing\""),
            "entities are sorted by id, not by creation order");
    require(text.find("\"version\": 2") != std::string::npos, "writes version 2");
    require(text.back() == '\n', "ends with a newline");

    // --- rejections --------------------------------------------------------
    SceneDocument bad;
    require(!parseSceneSource("{}", "<memory>", bad, error), "empty object fails");
    require(!parseSceneSource(R"({"format":"psx-dungeon-scene","version":9,
            "id":"x","entities":[]})",
                              "<memory>", bad, error),
            "an unknown version fails");
    require(!parseSceneSource(R"({"format":"psx-dungeon-scene","version":2,
            "id":"x","entities":[{"id":"a"},{"id":"a"}]})",
                              "<memory>", bad, error),
            "duplicate author ids fail");
    require(error.find("unique") != std::string::npos, "and the error says why");

    // --- allocateId --------------------------------------------------------
    require(tiny.allocateId("kit.wall") == "wall_0001",
            "ids drop the prefab namespace and start at 1");
    Entity taken;
    taken.id = "wall_0001";
    tiny.add(taken);
    require(tiny.allocateId("kit.wall") == "wall_0002",
            "and take the lowest free index, not a counter");

    std::cout << "SceneRoundTripTests: ok\n";
    return 0;
}
