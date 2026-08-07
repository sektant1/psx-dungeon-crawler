#include "script/bind/Bindings.h"

#include <eng/Log.h>

#include <string>
#include <utility>

namespace eng::script {
namespace {

// Lua -> ScriptValue. Anything the value type cannot hold arrives as
// monostate, which the callee reads as "absent" -- the same thing a missing
// argument produces, and for the same reason: a game function should not have
// to distinguish "you passed a table" from "you passed nothing", it should
// just report that it wanted a number.
ScriptValue fromLua(const sol::object& o)
{
    if (!o.valid() || o.get_type() == sol::type::lua_nil)
        return std::monostate{};
    if (o.is<bool>())
        return o.as<bool>();
    if (o.is<double>())
        return o.as<double>();
    if (o.is<std::string>())
        return o.as<std::string>();
    if (o.is<glm::vec3>())
        return o.as<glm::vec3>();
    return std::monostate{};
}

sol::object toLua(sol::state_view lua, const ScriptValue& v)
{
    return std::visit(
        [&lua](const auto& held) -> sol::object {
            using T = std::decay_t<decltype(held)>;
            if constexpr (std::is_same_v<T, std::monostate>)
                return sol::object(lua, sol::in_place, sol::lua_nil);
            else
                return sol::object(lua, sol::in_place, held);
        },
        v);
}

} // namespace

void bindModule(sol::state& lua, const ScriptModule& module)
{
    if (module.table.empty()) {
        log::error("Script: a module with no table name was ignored");
        return;
    }
    // Extend rather than replace. Two calls naming one table is how a game
    // splits a large surface across the systems that own its parts, and
    // create_named_table would silently discard whatever the first call bound.
    sol::table target = lua[module.table].valid()
                            ? lua[module.table].get<sol::table>()
                            : lua.create_named_table(module.table);

    for (const auto& [name, fn] : module.functions) {
        if (name.empty() || !fn) {
            log::error("Script: %s has an unnamed or null function; skipped",
                       module.table.c_str());
            continue;
        }
        // The name is captured for the error path: a variadic binding has no
        // arity to report, so the one thing a failing call must always be able
        // to say is which function it was.
        const std::string qualified = module.table + "." + name;
        target[name] = [fn, qualified](sol::variadic_args args) {
            ScriptArgs values;
            values.reserve(args.size());
            for (const sol::stack_proxy arg : args)
                values.push_back(fromLua(arg));
            sol::state_view lua = args.lua_state();
            return toLua(lua, fn(values));
        };
    }
}

// --- argument helpers -------------------------------------------------------
//
// Every game module reads its arguments the same three ways, and hand-rolling
// the index-and-type check at each call site is how one of them ends up reading
// argument 0 as argument 1.

std::string argString(const ScriptArgs& args, std::size_t index,
                      const std::string& fallback)
{
    if (index >= args.size())
        return fallback;
    const std::string* s = std::get_if<std::string>(&args[index]);
    return s ? *s : fallback;
}

double argNumber(const ScriptArgs& args, std::size_t index, double fallback)
{
    if (index >= args.size())
        return fallback;
    if (const double* d = std::get_if<double>(&args[index]))
        return *d;
    // A bool is a number in every gameplay call that takes one, and Lua users
    // pass `true` for 1 constantly. Refusing it would be pedantry.
    if (const bool* b = std::get_if<bool>(&args[index]))
        return *b ? 1.0 : 0.0;
    return fallback;
}

bool argBool(const ScriptArgs& args, std::size_t index, bool fallback)
{
    if (index >= args.size())
        return fallback;
    if (const bool* b = std::get_if<bool>(&args[index]))
        return *b;
    if (const double* d = std::get_if<double>(&args[index]))
        return *d != 0.0;
    return fallback;
}

glm::vec3 argVec3(const ScriptArgs& args, std::size_t index, glm::vec3 fallback)
{
    if (index >= args.size())
        return fallback;
    const glm::vec3* v = std::get_if<glm::vec3>(&args[index]);
    return v ? *v : fallback;
}

} // namespace eng::script
