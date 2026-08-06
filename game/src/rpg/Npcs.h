#pragma once
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

// Who lives here.
//
// WHY THIS EXISTS
// A person was described in two files and named in a third. dialogue.toml said
// what they say, traders.toml said what they sell, and quests.toml named them
// as a giver -- and nothing said who they *are*. That was survivable while
// nobody stood in the world: a conversation opened by a debug command needs no
// body. It stopped being survivable the moment the village became a place the
// player walks around, because standing somebody in it needs a name to draw, a
// role to read and a shape to see, and none of those had a home.
//
// So this is the roster: one row per person, naming the conversation they have
// and the shop they keep rather than restating either. An entry here is what
// makes an id placeable in the editor; the other two files remain what that id
// *does*.
namespace game::rpg {

// One authored person.
//
// The appearance fields are deliberately the same three a pickup has (mesh,
// material, scale). A villager is a body in the world with nothing special
// about how it draws, and inventing a second convention for it would mean two
// answers to "how do I give this thing a model".
struct NpcDef {
    std::string id;
    std::string name = "Stranger";
    std::string role;      // "Lodge Physician"; drawn under the name
    // What they say when there is nothing to say -- the line the tooltip shows
    // before a conversation is opened. Not dialogue: a greeting is the label on
    // the door, and putting it in the tree would mean a person with no tree
    // could not have one.
    std::string greeting;

    // The conversation and the shop, by id. `dialogue` defaults to the person's
    // own id, because that is what it is in every case anybody has authored;
    // stating it is for the person who shares a conversation with somebody else
    // (two guards reading from one script).
    std::string dialogue;
    std::string trader;    // empty: talks, does not trade

    // Presentation. An empty mesh gets a capsule stand-in, which is the state
    // the whole village is in until there are character models -- and a
    // stand-in that reads as a person is worth more than a missing-asset box.
    std::string mesh;
    std::string material;
    float scale = 1.0f;
    // How tall the stand-in is, and where the look target sits. Authored rather
    // than derived from the mesh, because the target is aimed at the chest and
    // only the author knows where that is on a model.
    float height = 1.8f;

    bool trades() const { return !trader.empty(); }
    // The conversation id, resolved. Kept a method rather than fixed up at load
    // so a hot-reloaded row that clears the field falls back correctly.
    const std::string& dialogueId() const
    {
        return dialogue.empty() ? id : dialogue;
    }
};

class NpcLibrary {
public:
    bool load(const std::string& tomlPath);
    bool loadFromString(const char* tomlSrc);

    // Shared for the reason ItemLibrary::Ref is: a hot-reload destroys every
    // definition in the file, and a person standing in the world holds on to
    // theirs across frames.
    using Ref = std::shared_ptr<const NpcDef>;

    Ref find(const std::string& id) const;
    std::vector<std::string> ids() const; // sorted, for debug UI and validation
    int size() const { return int(mNpcs.size()); }
    const std::string& sourcePath() const { return mSourcePath; }

private:
    bool parse(const void* tomlTable);

    std::unordered_map<std::string, std::shared_ptr<NpcDef>> mNpcs;
    std::string mSourcePath;
};

} // namespace game::rpg
