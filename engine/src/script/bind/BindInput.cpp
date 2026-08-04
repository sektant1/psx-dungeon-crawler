#include "script/bind/Bindings.h"

#include <eng/Input.h>

#include <string>
#include <tuple>

namespace eng::script {

void bindInput(sol::state& lua, Input& input)
{
    sol::table t = lua.create_named_table("input");

    // Action names, not key codes. eng::Input is already action-based and the
    // bindings live in the TOML config, so a script never names a physical key
    // and rebinding does not touch a line of Lua.
    t["down"] = [&input](const std::string& action) {
        return input.isDown(action);
    };
    t["pressed"] = [&input](const std::string& action) {
        return input.wasPressed(action);
    };

    // Two numbers rather than a vec3: mouseDelta is a vec2, and widening it
    // would invent a z that means nothing.
    t["mouse_delta"] = [&input]() {
        const glm::vec2 d = input.mouseDelta();
        return std::make_tuple(d.x, d.y);
    };
}

} // namespace eng::script
