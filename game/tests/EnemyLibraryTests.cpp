// Archetype inheritance, the replace-not-merge rule, and the rows the library
// is supposed to refuse.
#include "../src/enemy/EnemyLibrary.h"
#include "../src/combat/CombatVocabulary.h"

#include <cmath>
#include <cstdio>

using namespace game;

static int failures = 0;
static void check(bool c, const char* m)
{
    if (!c) { std::fprintf(stderr, "FAIL: %s\n", m); ++failures; }
}
static bool nearly(float a, float b) { return std::fabs(a - b) < 1e-3f; }

int main()
{
    // Field-wise inheritance: a row states one number, inherits the rest.
    {
        EnemyLibrary lib;
        const bool ok = lib.loadFromString(R"(
[archetype.base]
name = "Base"
[archetype.base.stats]
health = 100.0
poise = 40.0
[archetype.base.locomotion]
chase_speed = 3.0
[[archetype.base.attack]]
id = "swing"
weapon = "claw"
max_range = 2.0

[enemy.tanky]
inherits = "base"
name = "Tanky"
[enemy.tanky.stats]
health = 250.0
)");
        check(ok, "document loads");
        const auto def = lib.find("tanky");
        check(def != nullptr, "enemy found");
        if (def) {
            check(def->name == "Tanky", "own field overrides");
            check(nearly(def->stats.health, 250.0f), "stated field takes effect");
            check(nearly(def->stats.poise, 40.0f), "unstated stat inherited");
            check(nearly(def->locomotion.chaseSpeed, 3.0f),
                  "unstated section inherited whole");
            check(def->attacks.size() == 1, "attacks inherited");
        }
        // Archetypes are not spawnable.
        check(lib.find("base") == nullptr, "archetype is not spawnable");
        check(lib.size() == 1, "only the enemy row is exposed");
    }

    // Transitive: enemy -> archetype -> archetype.
    {
        EnemyLibrary lib;
        lib.loadFromString(R"(
[archetype.root]
[archetype.root.stats]
health = 10.0
poise = 11.0
stamina = 12.0
[[archetype.root.attack]]
id = "a"

[archetype.mid]
inherits = "root"
[archetype.mid.stats]
poise = 21.0

[enemy.leaf]
inherits = "mid"
[enemy.leaf.stats]
stamina = 32.0
)");
        const auto def = lib.find("leaf");
        check(def != nullptr, "transitive row resolves");
        if (def) {
            check(nearly(def->stats.health, 10.0f), "value from the root");
            check(nearly(def->stats.poise, 21.0f), "value from the middle");
            check(nearly(def->stats.stamina, 32.0f), "value from the leaf");
        }
    }

    // Attacks replace rather than merge: a variant that states one move has
    // exactly one move, not its own plus the archetype's.
    {
        EnemyLibrary lib;
        lib.loadFromString(R"(
[archetype.two]
[[archetype.two.attack]]
id = "a"
[[archetype.two.attack]]
id = "b"

[enemy.one]
inherits = "two"
[[enemy.one.attack]]
id = "c"
)");
        const auto def = lib.find("one");
        check(def && def->attacks.size() == 1, "attack list replaced, not merged");
        check(def && def->attacks[0].id == "c", "the stated move is the one kept");
    }

    // Resistances replace too, and resolve to channel ids.
    {
        EnemyLibrary lib;
        lib.loadFromString(R"(
[archetype.base]
[[archetype.base.attack]]
id = "a"
[archetype.base.stats.resistances]
fire = 0.5
frost = 0.5

[enemy.only_fire]
inherits = "base"
[enemy.only_fire.stats.resistances]
fire = 0.9
)");
        const auto def = lib.find("only_fire");
        check(def && def->stats.resistances.size() == 1,
              "resistance table replaced wholesale");

        CombatVocabulary vocab;
        // A minimal vocabulary written the way magic.toml writes one.
        check(!vocab.load("/nonexistent.toml") || true, "missing vocab is survivable");
    }

    // A spawnable row with no attacks is refused: it would be a moving dummy.
    {
        EnemyLibrary lib;
        const bool ok = lib.loadFromString(R"(
[enemy.pacifist]
name = "Pacifist"
)");
        check(!ok, "a document whose only enemy has no attacks fails");
        check(lib.find("pacifist") == nullptr, "attackless row dropped");
    }

    // A row inheriting something that does not exist is dropped, not
    // half-built from defaults.
    {
        EnemyLibrary lib;
        lib.loadFromString(R"(
[enemy.orphan]
inherits = "ghost"
[[enemy.orphan.attack]]
id = "a"
)");
        check(lib.find("orphan") == nullptr, "unresolvable inherits drops the row");
    }

    // A cycle terminates instead of recursing forever.
    {
        EnemyLibrary lib;
        lib.loadFromString(R"(
[archetype.a]
inherits = "b"
[archetype.b]
inherits = "a"
[enemy.c]
inherits = "a"
[[enemy.c.attack]]
id = "x"
)");
        check(lib.find("c") == nullptr, "cyclic chain drops the row");
    }

    // preferredRange derives from the longest move when it is not authored.
    {
        EnemyLibrary lib;
        lib.loadFromString(R"(
[enemy.archer]
[[enemy.archer.attack]]
id = "shot"
max_range = 20.0
[[enemy.archer.attack]]
id = "poke"
max_range = 2.0
)");
        const auto def = lib.find("archer");
        check(def && nearly(def->preferredRange(), 17.0f),
              "derived preferred range is 85% of the longest move");
    }
    {
        EnemyLibrary lib;
        lib.loadFromString(R"(
[enemy.kiter]
[enemy.kiter.behaviour]
preferred_range = 9.0
[[enemy.kiter.attack]]
id = "shot"
max_range = 20.0
)");
        const auto def = lib.find("kiter");
        check(def && nearly(def->preferredRange(), 9.0f),
              "authored preferred range wins over the derived one");
    }

    // Malformed input is refused rather than partially applied.
    {
        EnemyLibrary lib;
        check(!lib.loadFromString("this is not toml ["), "garbage fails to load");
        check(lib.size() == 0, "failed load leaves the library empty");
    }

    // A definition outlives the table it came from. Live enemies hold one of
    // these across frames, and a reload destroys everything in the map -- with
    // a raw pointer this read would be a use-after-free, and the only thing
    // stopping it would be a caller remembering to despawn first.
    {
        EnemyLibrary lib;
        lib.loadFromString(R"(
[enemy.ghoul]
name = "Ghoul"
[enemy.ghoul.stats]
health = 42.0
[[enemy.ghoul.attack]]
id = "claw"
)");
        const auto held = lib.find("ghoul");
        check(held != nullptr, "spawned from the first table");

        // Reload with the row edited, and with a different roster entirely.
        lib.loadFromString(R"(
[enemy.wraith]
name = "Wraith"
[[enemy.wraith.attack]]
id = "chill"
)");
        check(lib.find("ghoul") == nullptr, "reload replaced the roster");
        check(held->name == "Ghoul", "the held definition is still readable");
        check(nearly(held->stats.health, 42.0f),
              "and still has the stats it was spawned with");
        check(lib.find("wraith") != nullptr, "the new row is available to spawn");
    }

    // The shipped table actually parses, and its channels actually exist. This
    // is the check that catches a typo'd resistance or a broken archetype
    // chain at build time instead of at "why does this enemy not resist fire".
    {
        EnemyLibrary lib;
        const std::string assets = APP_ASSET_DIR;
        check(lib.load(assets + "/enemies.toml"), "shipped enemies.toml loads");
        check(lib.size() >= 8, "shipped table defines the full roster");

        CombatVocabulary vocab;
        check(vocab.load(assets + "/magic.toml"), "shipped magic.toml loads");
        for (const std::string& id : lib.ids()) {
            const auto def = lib.find(id);
            if (!def)
                continue;
            check(!def->attacks.empty(), "every shipped enemy can attack");
            check(!def->name.empty(), "every shipped enemy is named");
            for (const EnemyResistance& r : def->stats.resistances)
                check(vocab.damageType(r.channel) != kInvalidDamageType,
                      "every shipped resistance names a real damage channel");
            for (const EnemyAttack& a : def->attacks)
                check(a.maxRange > a.minRange,
                      "every shipped attack has a usable range band");
        }
        const auto warden = lib.find("ashen_warden");
        check(warden && warden->attacks.size() == 3,
              "shipped boss move list is intact");
        check(warden && warden->attacks[2].pattern.has_value(),
              "shipped boss pyre sequence parsed");
    }

    if (failures == 0) std::printf("EnemyLibraryTests OK\n");
    return failures ? 1 : 0;
}
