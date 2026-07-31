#pragma once
#include "EnemyDef.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace game {

class CombatVocabulary;

// The enemy table, loaded from assets/enemies.toml.
//
// Two kinds of row live in one file. `[archetype.<id>]` rows are templates that
// never spawn; `[enemy.<id>]` rows spawn and may name an archetype (or another
// enemy) in `inherits`. Inheritance is field-wise and transitive, so an
// archetype supplies every value a row does not state, and a row that only
// wants a different health writes exactly one line.
//
// That is the whole extensibility story: adding an enemy type is adding a
// table. No registration call, no factory, no C++.
class EnemyLibrary {
public:
    // Replace the contents from a TOML file. Returns false (and leaves the
    // library empty) if the file is missing or malformed; the caller decides
    // whether that is fatal. Rows with an unresolvable `inherits`, a cyclic
    // chain, or no attacks are logged and dropped rather than half-built.
    bool load(const std::string& tomlPath);
    // Same, from an in-memory document. For tests and hot-reload previews.
    bool loadFromString(const char* tomlSrc);

    // Turn every authored resistance channel name into an id. Call after load;
    // until then EnemyDef::resistanceById is all zeroes.
    void resolve(const CombatVocabulary& vocabulary);

    // A definition, kept alive independently of the table it came from.
    //
    // This is shared rather than raw because reloading the file destroys every
    // definition in it, and live enemies hold on to theirs across frames -- a
    // raw pointer would dangle on the next hot-reload, and the only thing
    // stopping it would be a convention ("clear the enemies first") that the
    // type does not enforce. Sharing gives hot-reload the semantics you
    // actually want: enemies already on the floor keep fighting with the stats
    // they were spawned with, and the next spawn picks up the edited row.
    using Ref = std::shared_ptr<const EnemyDef>;

    // Null when the id is not a spawnable enemy (archetypes are not spawnable).
    Ref find(const std::string& id) const;
    // Every spawnable id, sorted, for debug UI and spawner validation.
    std::vector<std::string> ids() const;
    int size() const { return int(mEnemies.size()); }

    // For the debug panel's live sliders. Edits are seen immediately by every
    // enemy already holding this definition, which is the point.
    EnemyDef* mutableDef(const std::string& id);

    // The path the last successful load() came from, so the debug panel's
    // reload button needs no state of its own. Empty after loadFromString.
    const std::string& sourcePath() const { return mSourcePath; }

private:
    // Parse one document into `archetypes`/`enemies` staging maps, then flatten
    // inheritance. Shared by both load entry points.
    bool parse(const void* tomlTable);

    std::unordered_map<std::string, std::shared_ptr<EnemyDef>> mEnemies;
    std::string mSourcePath;
};

} // namespace game
