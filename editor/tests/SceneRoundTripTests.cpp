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

    // The spin-portal scene used to be pinned here, for the property that
    // PreviewBridge only draws prefab-backed meshes. It is gone: two thirds of
    // its placements named prototype-era models that were deleted from the
    // tree, so the scene was pruned along with the catalogue entries (see
    // tools/prune_dead_content.py).
    //
    // Nothing is lost that this file is for. The round trip is the property
    // under test, and `ritual_boss_showroom` above already covers "a shipped
    // scene loads and re-serializes" -- including a prefab-backed mesh entity.
    // Re-pinning the same property against a second scene was never what made
    // it true.

    // The cozy lair used to be pinned here as well -- its floor count, its
    // pivot's spin rate, where its subject stands. Those assertions were about
    // a scene somebody is still authoring, not about the format: every edit to
    // the level broke a test that had nothing to say about serialization, and
    // the only way to keep it green was to re-pin numbers nobody was reading.
    //
    // What this file is for is the round trip. `ritual_boss_showroom` above
    // covers "a shipped scene loads and re-serializes", which is the property
    // that matters and does not care which cell a floor tile is in.

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
    require(!parseSceneSource(R"({"format":"raven-scene","version":9,
            "id":"x","entities":[]})",
                              "<memory>", bad, error),
            "an unknown version fails");
    require(!parseSceneSource(R"({"format":"raven-scene","version":2,
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

    // --- scripts round-trip, including every prop type ---------------------
    {
        using game::content::ScriptAuthor;
        using game::content::ScriptPropAuthor;
        using Prop = ScriptPropAuthor::Type;

        const char* source = R"({"format":"raven-scene","version":2,
          "id":"scene.test.scripts","entities":[
            {"id":"lever_a","name":"Lever"},
            {"id":"iron_door","name":"Door","scripts":[
              {"path":"scripts/door.lua","props":{
                 "speed":2.5,"open":true,"label":"north",
                 "tint":[0.0,0.5,0.0],"target":{"entity":"lever_a"}}},
              {"path":"scripts/creak.lua","enabled":false}
            ]}
          ]})";

        SceneDocument doc = parse(source, "<scripts>");
        const Entity* door = doc.find("iron_door");
        require(door != nullptr, "the scripted entity parses");
        require(door->scripts.size() == 2, "both scripts parse");
        require(door->scripts[0].path == "scripts/door.lua", "path parses");
        require(door->scripts[0].enabled, "enabled defaults to true");
        require(!door->scripts[1].enabled, "and an authored false is kept");
        require(door->scripts[0].props.size() == 5, "every prop parses");

        // The writer must round-trip the TYPE, not just the text: a bare string
        // for an entity reference would read back as a String prop, and the
        // inspector and cooker would both stop treating it as a reference.
        const SceneDocument again =
            parse(serializeSceneSource(doc), "<scripts-reparse>");
        const Entity* door2 = again.find("iron_door");
        require(door2 != nullptr && door2->scripts.size() == 2,
                "scripts survive the write");
        require(!door2->scripts[1].enabled, "and so does a disabled one");

        int seen = 0;
        for (const ScriptPropAuthor& p : door2->scripts[0].props) {
            if (p.key == "speed") {
                require(p.type == Prop::Number && p.numberValue == 2.5f,
                        "number prop");
                ++seen;
            } else if (p.key == "open") {
                require(p.type == Prop::Bool && p.boolValue, "bool prop");
                ++seen;
            } else if (p.key == "label") {
                require(p.type == Prop::String && p.stringValue == "north",
                        "string prop");
                ++seen;
            } else if (p.key == "tint") {
                require(p.type == Prop::Vec3 && p.vecValue.y == 0.5f,
                        "vec3 prop");
                ++seen;
            } else if (p.key == "target") {
                require(p.type == Prop::Entity && p.stringValue == "lever_a",
                        "an entity reference keeps its type across the write");
                ++seen;
            }
        }
        require(seen == 5, "all five prop types survive");

        // And serialisation stays a fixed point with scripts in the document.
        const std::string first = serializeSceneSource(doc);
        require(first == serializeSceneSource(parse(first, "<again>")),
                "serialize is still idempotent with scripts present");
    }

    // --- clips -------------------------------------------------------------
    // A clip's JSON is hand-written on both sides (a list of tracks of keys is
    // not a field table), so it is exactly the shape that rots without a test.
    {
        const std::string json = R"({
          "format": "raven-scene", "version": 2, "id": "scene.clip",
          "entities": [
            { "id": "door",
              "clip": {
                "duration": 0.8, "mode": "pingpong", "speed": 1.5,
                "autoplay": false,
                "tracks": [
                  { "target": "lid", "component": "Transform",
                    "field": "position", "ease": "easeout",
                    "keys": [ {"t": 0.0, "v": [0,0,0]},
                              {"t": 0.8, "v": [0,3.2,0]} ] },
                  { "component": "ShaderParams", "field": "tint",
                    "keys": [ {"t": 0.0, "v": 0.25} ] }
                ]
              } } ] })";

        SceneDocument doc = parse(json, "<clip>");
        const Entity* door = doc.find("door");
        require(door && door->clip, "the clip parses");
        require(door->clip->duration == 0.8f, "duration");
        require(door->clip->mode == eng::ecs::ClipMode::PingPong, "mode");
        require(door->clip->speed == 1.5f, "speed");
        require(!door->clip->autoplay, "autoplay=false survives -- it is the "
                                       "non-default, and a writer that dropped "
                                       "it would silently start every clip");
        require(door->clip->tracks.size() == 2, "both tracks");
        require(door->clip->tracks[0].target == "lid", "the descendant target");
        require(door->clip->tracks[0].ease == eng::ecs::ClipEase::EaseOut, "ease");
        require(door->clip->tracks[0].keys.size() == 2, "two keys");
        require(door->clip->tracks[0].keys[1].value.y == 3.2f, "the key value");
        // A scalar `v` is the shorthand a Float track wants; it lands in x.
        require(door->clip->tracks[1].keys[0].value.x == 0.25f,
                "a bare number is accepted for a scalar field");

        // Runtime state is never read back, even from a hand-edited file: a
        // saved playhead would reload mid-swing.
        require(door->clip->time == 0.0f && !door->clip->playing,
                "the playhead starts at zero regardless of the file");

        const std::string first = serializeSceneSource(doc);
        require(first == serializeSceneSource(parse(first, "<clip again>")),
                "serialize is idempotent with a clip present");
    }

    // A default clip writes only what it must, so adding the component to an
    // entity is a small diff rather than a page of restated defaults.
    {
        SceneDocument doc;
        doc.id = "scene.default_clip";
        Entity entity;
        entity.id = "thing";
        entity.clip = ClipAuthor{};
        doc.add(std::move(entity));

        const std::string text = serializeSceneSource(doc);
        require(text.find("\"speed\"") == std::string::npos,
                "a default speed is omitted");
        require(text.find("\"autoplay\"") == std::string::npos,
                "so is a default autoplay");
        require(text.find("\"mode\"") == std::string::npos,
                "and the default mode");
        require(text.find("\"tracks\"") == std::string::npos,
                "and an empty track list, which every freshly added clip has");
        require(text.find("\"duration\"") != std::string::npos,
                "but the duration is always written -- it is what a clip IS, "
                "and a file that omitted it would read as zero-length");
    }

    // Every mode and every ease survives a write and a read.
    //
    // The failure this catches is the one the shared id tables were introduced
    // for: the reader's accepted set and the writer's emitted set used to be
    // two hand-written lists, and a value in one but not the other is a scene
    // that saves and then refuses to load. Looping over the enums rather than
    // naming three cases means a mode added later is covered without anyone
    // remembering to extend this.
    {
        for (int m = 0; m < eng::ecs::kClipModeCount; ++m) {
            for (int e = 0; e < eng::ecs::kClipEaseCount; ++e) {
                SceneDocument doc;
                doc.id = "scene.enum_sweep";
                Entity entity;
                entity.id = "thing";
                ClipAuthor clip;
                clip.mode = eng::ecs::ClipMode(m);
                eng::ecs::ClipTrack track;
                track.component = "Transform";
                track.field = "position";
                track.ease = eng::ecs::ClipEase(e);
                track.keys = {{0.0f, {0.0f, 0.0f, 0.0f}}};
                clip.tracks.push_back(std::move(track));
                entity.clip = std::move(clip);
                doc.add(std::move(entity));

                const std::string text = serializeSceneSource(doc);
                const SceneDocument back = parse(text, "<enum sweep>");
                const Entity* thing = back.find("thing");
                require(thing && thing->clip, "the clip survives");
                require(int(thing->clip->mode) == m, "the mode survives");
                require(thing->clip->tracks.size() == 1, "the track survives");
                require(int(thing->clip->tracks[0].ease) == e,
                        "and so does the ease");
            }
        }
    }

    std::cout << "SceneRoundTripTests: ok\n";
    return 0;
}
