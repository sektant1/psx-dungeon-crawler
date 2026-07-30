// scene.schema.json is the contract text editors and reviewers read, but
// nothing in the build forces the C++ writer to agree with it -- so this test
// does. It builds a document that exercises EVERY field the writer knows how to
// emit, then checks each emitted key against the schema.
//
// Without this, the schema silently rots into documentation of a format we
// stopped writing, which is worse than having no schema at all.

#include "SceneWriter.h"

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <set>
#include <string>

using namespace game::content;
using Json = nlohmann::json;

static void require(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "SchemaSyncTests: " << message << '\n';
        std::exit(1);
    }
}

// Collects the property names a $defs subschema (or the root) declares.
static std::set<std::string> propertyNames(const Json& schema)
{
    std::set<std::string> names;
    if (schema.contains("properties"))
        for (const auto& entry : schema["properties"].items())
            names.insert(entry.key());
    return names;
}

static void checkObject(const Json& value, const Json& schema,
                        const std::string& where)
{
    const std::set<std::string> allowed = propertyNames(schema);
    for (const auto& entry : value.items()) {
        require(allowed.count(entry.key()) > 0,
                where + " emits '" + entry.key() + "', which the schema does not declare");
    }
}

int main()
{
    std::ifstream input(SCENE_SCHEMA);
    require(bool(input), "the schema file opens");
    const Json schema = Json::parse(input, nullptr, false);
    require(!schema.is_discarded(), "the schema is valid JSON");
    const Json& defs = schema["$defs"];

    // Every optional field set, so nothing the writer can emit is skipped.
    SceneDocument document;
    document.id = "scene.schema_sync";
    Entity entity;
    entity.id = "everything";
    entity.name = "Every Field";
    entity.prefab = "kit.wall";
    entity.castShadows = false;
    entity.transform.position = {1.0f, 2.0f, 3.0f};
    entity.transform.rotationDegrees = {0.0f, 90.0f, 0.0f};
    entity.transform.scale = {2.0f, 2.0f, 2.0f};
    entity.cell = CellPlacement{3, 4, CellPlacement::Edge::North, 2, 1, 4.0f};
    entity.collider = ColliderAuthor{{1.0f, 2.0f, 1.0f}, {0.0f, 1.0f, 0.0f}};
    entity.light = LightAuthor{LightAuthor::Type::Point, {1, 1, 1}, 8.0f, true};
    entity.exitYawDegrees = 90.0f;
    entity.marker = "boss.spawn";
    entity.enemySpawn = "goblin";
    entity.pickup = "potion";
    entity.trigger = TriggerAuthor{{2.0f, 2.0f, 2.0f}, "arena.start"};
    entity.playerSpawn = true;
    document.add(entity);

    const Json written = Json::parse(serializeSceneSource(document));
    checkObject(written, schema, "the document root");
    require(written["entities"].size() == 1, "one entity was written");

    const Json& emitted = written["entities"][0];
    checkObject(emitted, defs["entity"], "an entity");
    checkObject(emitted["transform"], defs["transform"], "a transform");
    checkObject(emitted["cell"], defs["cell"], "a cell");
    checkObject(emitted["collider"], defs["collider"], "a collider");
    checkObject(emitted["light"], defs["light"], "a light");
    checkObject(emitted["trigger"], defs["trigger"], "a trigger");
    checkObject(emitted["exit"], defs["entity"]["properties"]["exit"], "an exit");

    // The version the writer emits has to be one the schema accepts.
    const Json& versions = schema["properties"]["version"]["enum"];
    bool accepted = false;
    for (const Json& version : versions)
        accepted = accepted || version == written["version"];
    require(accepted, "the schema accepts the version the writer emits");

    // Required fields must actually be emitted.
    for (const Json& required : schema["required"])
        require(written.contains(required.get<std::string>()),
                "the root emits required field " + required.get<std::string>());

    std::cout << "SchemaSyncTests: ok\n";
    return 0;
}
