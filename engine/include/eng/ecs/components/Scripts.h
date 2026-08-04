#pragma once
#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace eng::ecs {

// One authored value on a script instance -- the serialised-field equivalent
// from Unity or Godot, and what makes one script reusable across many entities
// instead of one script per door.
//
// A tagged struct rather than a variant: the byte serialiser, the scene writer
// and the inspector all switch on the same enum, and a variant would make each
// of them a visitor for no gain at five types.
struct ScriptProp {
    enum class Type : uint8_t { Bool, Number, String, Vec3, Entity };

    std::string key;
    Type type = Type::Number;
    bool b = false;
    // f32, not double. ByteWriter's vocabulary is f32 and so is every other
    // component's; a Lua number narrows on the way in, which costs nothing for
    // an authored tuning value and keeps the payload the same shape as the
    // rest of the format.
    float n = 0.0f;
    glm::vec3 v{0.0f};
    // String's value, and also Entity's target entity name. Entity is
    // mechanically a string: it exists as its own type so the inspector can
    // offer a picker and the cooker can check the name resolves.
    std::string s;
};

// One script attached to an entity.
struct ScriptRef {
    std::string path;              // logical asset path, "scripts/door.lua"
    std::vector<ScriptProp> props; // authored, per instance
    bool enabled = true;
};

// Every script on this entity. `items` order is author order, which is the
// order their callbacks run in -- so a Health script can be relied on to see a
// frame before the Patrol script that reads it.
//
// Purely authored data: no runtime state lives here. That is what makes hot
// reload trivial (drop ScriptState, rebuild from this) and what keeps sol out
// of eng/ecs.
struct Scripts {
    std::vector<ScriptRef> items;
};

} // namespace eng::ecs
