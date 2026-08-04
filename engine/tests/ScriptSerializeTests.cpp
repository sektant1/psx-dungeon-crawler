#include <eng/ecs/ComponentRegistry.h>
#include <eng/ecs/components/Scripts.h>
#include <eng/io/ByteStream.h>

#include <entt/entt.hpp>

#include <cstdlib>
#include <iostream>

using namespace eng;
using namespace eng::ecs;

static void require(bool c, const char* m)
{
    if (!c) { std::cerr << "ScriptSerializeTests: " << m << '\n'; std::exit(1); }
}

static const ComponentType& scriptsType(const ComponentRegistry& reg)
{
    const ComponentType* type = reg.find(33);
    require(type != nullptr, "Scripts is registered under stable id 33");
    return *type;
}

// Round-trips one Scripts component through the registered serialiser.
static Scripts roundTrip(const Scripts& in)
{
    ComponentRegistry reg;
    registerEngineComponents(reg);
    const ComponentType& type = scriptsType(reg);

    entt::registry src;
    const entt::entity a = src.create();
    src.emplace<Scripts>(a, in);

    io::ByteWriter w;
    type.serialize(src, a, w);

    entt::registry dst;
    const entt::entity b = dst.create();
    io::ByteReader r(w.bytes().data(), w.size(), w.pool());
    type.deserialize(dst, b, r, uint32_t(w.size()));
    require(dst.all_of<Scripts>(b), "deserialise emplaced the component");
    return dst.get<Scripts>(b);
}

int main()
{
    // --- every prop type survives the round trip ---------------------------
    {
        Scripts in;
        ScriptRef door;
        door.path = "scripts/door.lua";
        door.enabled = true;
        door.props.push_back({"open", ScriptProp::Type::Bool, true, 0.0f, {}, ""});
        door.props.push_back(
            {"speed", ScriptProp::Type::Number, false, 2.5f, {}, ""});
        door.props.push_back({"tint", ScriptProp::Type::Vec3, false, 0.0f,
                              glm::vec3(0.1f, 0.2f, 0.3f), ""});
        door.props.push_back({"label", ScriptProp::Type::String, false, 0.0f, {},
                              "north gate"});
        door.props.push_back({"target", ScriptProp::Type::Entity, false, 0.0f, {},
                              "lever_a"});
        in.items.push_back(door);

        const Scripts out = roundTrip(in);
        require(out.items.size() == 1, "one script survives");
        const ScriptRef& r = out.items[0];
        require(r.path == "scripts/door.lua", "path survives");
        require(r.enabled, "enabled survives");
        require(r.props.size() == 5, "every prop survives");
        require(r.props[0].key == "open" && r.props[0].b, "bool prop");
        require(r.props[1].n == 2.5f, "number prop is exact as f32");
        require(r.props[2].v.y == 0.2f, "vec3 prop");
        require(r.props[3].s == "north gate", "string prop");
        require(r.props[4].type == ScriptProp::Type::Entity &&
                    r.props[4].s == "lever_a",
                "entity prop keeps its type, not just its text");
    }

    // --- several scripts on one entity keep author order -------------------
    {
        Scripts in;
        in.items.push_back({"scripts/health.lua", {}, true});
        in.items.push_back({"scripts/patrol.lua", {}, false});
        const Scripts out = roundTrip(in);
        require(out.items.size() == 2, "both scripts survive");
        require(out.items[0].path == "scripts/health.lua" &&
                    out.items[1].path == "scripts/patrol.lua",
                "author order is the serialised order -- it decides run order");
        require(!out.items[1].enabled, "a disabled script stays disabled");
    }

    // --- an empty component is legal ---------------------------------------
    {
        const Scripts out = roundTrip(Scripts{});
        require(out.items.empty(), "no scripts round-trips as no scripts");
    }

    // --- a truncated payload decodes to defaults, never out of bounds ------
    {
        ComponentRegistry reg;
        registerEngineComponents(reg);
        const ComponentType& type = scriptsType(reg);
        Scripts in;
        in.items.push_back({"scripts/door.lua", {}, true});
        entt::registry src;
        const entt::entity a = src.create();
        src.emplace<Scripts>(a, in);
        io::ByteWriter w;
        type.serialize(src, a, w);

        entt::registry dst;
        const entt::entity b = dst.create();
        io::ByteReader r(w.bytes().data(), 1, w.pool()); // one byte of a u16
        type.deserialize(dst, b, r, 1u);
        require(dst.all_of<Scripts>(b),
                "a truncated payload still emplaces, at defaults");
        require(dst.get<Scripts>(b).items.empty(),
                "and reads no garbage items out of it");
    }

    // --- the add-component menu can offer it -------------------------------
    {
        ComponentRegistry reg;
        registerEngineComponents(reg);
        const ComponentType& type = scriptsType(reg);
        require(type.authorable, "Scripts is authorable -- it is the whole point");

        entt::registry r;
        const entt::entity e = r.create();
        require(!type.has(r, e), "absent to begin with");
        type.addDefault(r, e);
        require(type.has(r, e), "addDefault emplaces an empty list");
        require(r.get<Scripts>(e).items.empty(),
                "and a fresh Scripts carries no scripts");
        type.remove(r, e);
        require(!type.has(r, e), "remove takes it away again");
    }

    std::cout << "ScriptSerializeTests: ok\n";
    return 0;
}
