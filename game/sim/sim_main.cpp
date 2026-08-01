// Headless action-simulation runner. Executes a text script of combat actions
// against a game::sim::World and reports assertion pass/fail. No window, no
// renderer -- exercises the real damage/resistance/crowd-control model.
//
// Script grammar (one statement per line; '#' starts a comment):
//   weapons <path.toml>                     load weapon table (optional)
//   combatant <name> hp=<f> faction=<f> [<type>=<resist> ...]
//       faction: player | enemy | neutral
//       type:    physical fire frost lightning poison arcane   (resist in -1..0.9)
//   hit <target> <weapon> <source>          land a weapon hit
//   wait <seconds>                          advance the model (i-frames + DoT)
//   assert <name>.hp <op> <value>           op: < > <= >= == !=
//   assert <name>.effects <op> <int>
//   assert <name>.dead | <name>.alive
//
// Usage: game_sim [script.txt]   (no arg runs the built-in smoke script)
#include "SimWorld.h"

#include <eng/assets/AssetRoot.h>

#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace game;

namespace {

const char* kSmokeScript = R"SIM(
# Built-in smoke test: an armored, flammable dummy takes a sword hit, then a
# fireball whose burn finishes it off.
combatant dummy hp=60 faction=enemy physical=0.35 fire=-0.5
combatant hero  hp=100 faction=player
# Sword: 22 base, 35% physical resist -> ~14 (crit may raise it). Not lethal.
hit dummy sword hero
assert dummy.alive
assert dummy.hp < 60
# Fireball: fire damage (dummy is +50% vulnerable) + a 3s burn.
hit dummy fireball hero
assert dummy.effects > 0
# Let the burn tick out; it should finish the dummy.
wait 4.0
assert dummy.dead
)SIM";

std::vector<std::string> tokens(const std::string& line)
{
    std::vector<std::string> out;
    std::istringstream is(line);
    for (std::string t; is >> t;) out.push_back(t);
    return out;
}

Faction parseFaction(const std::string& s)
{
    if (s == "player") return Faction::Player;
    if (s == "enemy") return Faction::Enemy;
    return Faction::Neutral;
}

// "channel=value" -> writes into resist; returns false when magic.toml does
// not define that channel. Scripts therefore reach every authored channel and
// no others, without this parser knowing any of their names.
bool parseResist(const std::string& kv, Resistances& r,
                 const CombatVocabulary& vocabulary)
{
    const auto eq = kv.find('=');
    if (eq == std::string::npos) return false;
    const DamageTypeId type = vocabulary.damageType(kv.substr(0, eq));
    if (type == kInvalidDamageType)
        return false;
    r[type] = std::stof(kv.substr(eq + 1));
    return true;
}

bool cmp(float a, const std::string& op, float b)
{
    if (op == "<") return a < b;
    if (op == ">") return a > b;
    if (op == "<=") return a <= b;
    if (op == ">=") return a >= b;
    if (op == "==") return std::fabs(a - b) < 1e-3f;
    if (op == "!=") return std::fabs(a - b) >= 1e-3f;
    return false;
}

struct Runner {
    sim::World world;
    int passed = 0, failed = 0, line = 0;

    void fail(const std::string& msg) {
        std::fprintf(stderr, "line %d: FAIL %s\n", line, msg.c_str());
        ++failed;
    }

    void doAssert(const std::vector<std::string>& t) {
        // assert <subject> [op value]
        const std::string& subj = t[1];
        const auto dot = subj.find('.');
        const std::string name = subj.substr(0, dot);
        const std::string field = subj.substr(dot + 1);

        if (field == "dead") {
            if (world.alive(name)) fail(name + " expected dead, is alive");
            else ++passed;
            return;
        }
        if (field == "alive") {
            if (!world.alive(name)) fail(name + " expected alive, is dead");
            else ++passed;
            return;
        }
        if (t.size() < 4) { fail("assert needs 'op value'"); return; }
        const std::string& op = t[2];
        const float rhs = std::stof(t[3]);
        const float lhs = (field == "hp") ? world.hp(name)
                        : (field == "effects") ? float(world.activeEffects(name))
                        : 0.0f;
        if (cmp(lhs, op, rhs)) ++passed;
        else fail(subj + " is " + std::to_string(lhs) + ", not " + op + " " +
                  t[3]);
    }

    void exec(const std::string& raw) {
        ++line;
        std::string s = raw;
        if (const auto h = s.find('#'); h != std::string::npos) s = s.substr(0, h);
        const auto t = tokens(s);
        if (t.empty()) return;

        const std::string& cmd = t[0];
        if (cmd == "weapons" && t.size() >= 2) {
            world.loadWeapons(t[1]);
        } else if (cmd == "combatant" && t.size() >= 4) {
            float hp = 100.0f;
            Faction fac = Faction::Neutral;
            Resistances resist;
            for (size_t i = 2; i < t.size(); ++i) {
                if (t[i].rfind("hp=", 0) == 0) hp = std::stof(t[i].substr(3));
                else if (t[i].rfind("faction=", 0) == 0) fac = parseFaction(t[i].substr(8));
                else parseResist(t[i], resist, world.vocabulary());
            }
            if (!world.addCombatant(t[1], hp, fac, resist))
                fail("duplicate combatant " + t[1]);
        } else if (cmd == "hit" && t.size() >= 4) {
            if (!world.hit(t[1], t[2], t[3]))
                fail("hit on unknown combatant " + t[1]);
        } else if (cmd == "wait" && t.size() >= 2) {
            world.advance(std::stof(t[1]));
        } else if (cmd == "assert" && t.size() >= 2) {
            doAssert(t);
        } else {
            fail("unknown statement '" + cmd + "'");
        }
    }
};

} // namespace

int main(int argc, char** argv)
{
    // No window and no eng::Engine here, so nothing else mounts the content
    // set: the harness does it itself, before the World that reads magic.toml
    // and weapons.toml exists.
    if (!eng::assets::init() || !eng::assets::mount("game")) {
        std::fprintf(stderr, "game_sim: no content root\n");
        return 2;
    }

    std::string text;
    if (argc >= 2) {
        std::ifstream f(argv[1]);
        if (!f) {
            std::fprintf(stderr, "game_sim: cannot open script '%s'\n", argv[1]);
            return 2;
        }
        std::stringstream ss;
        ss << f.rdbuf();
        text = ss.str();
        std::printf("game_sim: running %s\n", argv[1]);
    } else {
        text = kSmokeScript;
        std::printf("game_sim: running built-in smoke script\n");
    }

    Runner run;
    std::istringstream lines(text);
    for (std::string line; std::getline(lines, line);)
        run.exec(line);

    std::printf("game_sim: %d passed, %d failed\n", run.passed, run.failed);
    return run.failed == 0 ? 0 : 1;
}
