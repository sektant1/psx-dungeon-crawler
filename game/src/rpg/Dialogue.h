#pragma once
#include "RpgTypes.h"

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

// Conversations.
//
// A node is a line somebody says plus the replies available to it. A reply is
// gated by Conditions and fires Effects -- the same Condition and Effect the
// quest system uses, so "you need three doses of tincture" is written once and
// means one thing whether it gates a quest or a sentence.
//
// This is data, not a VM. The engine has Lua (eng_script) and it is the right
// home for a scripted *encounter*; a conversation tree is not one, and putting
// every villager's small talk in a hot-reloadable script file would buy nothing
// and cost a file per character. When a line genuinely needs logic, the seam is
// an effect that publishes a typed event -- which the script host already
// listens to.
namespace game::rpg {

struct DialogueChoice {
    std::string text;
    std::string next;               // node id; empty ends the conversation
    std::vector<Condition> conditions; // all must pass for the choice to show
    std::vector<Effect> effects;       // fired when the choice is taken
    // Hidden rather than greyed out when its conditions fail. Default is
    // hidden: a greyed-out "[Bring me the ledger]" is a quest marker in
    // disguise, and §20 asks for minimal markers.
    bool showWhenLocked = false;
};

struct DialogueNode {
    std::string id;
    std::string speaker;   // display name, or empty to inherit the tree's
    std::string text;
    std::vector<Effect> onEnter; // fired the first time the node is shown
    std::vector<DialogueChoice> choices;
};

// One character's conversation. `entry` picks the opening node by condition, so
// the same NPC greets a stranger, a client and a debtor differently without a
// branch at the top of every node.
struct DialogueEntry {
    std::string node;
    std::vector<Condition> conditions;
};

struct DialogueTree {
    std::string id;        // npc id
    std::string speaker;   // display name
    std::vector<DialogueEntry> entries;
    std::unordered_map<std::string, DialogueNode> nodes;
};

class DialogueLibrary {
public:
    bool load(const std::string& tomlPath);
    bool loadFromString(const char* tomlSrc);

    const DialogueTree* find(const std::string& npc) const;
    std::vector<std::string> ids() const;
    int size() const { return int(mTrees.size()); }

private:
    bool parse(const void* tomlTable);
    std::unordered_map<std::string, DialogueTree> mTrees;
};

// A conversation in progress.
//
// The runner owns no game state: it is handed an evaluator for conditions and a
// sink for effects, both of which RpgRuntime supplies. That is what lets the
// dialogue tests run with no world at all -- a lambda that returns true is a
// complete condition evaluator.
class DialogueRunner {
public:
    using ConditionFn = std::function<bool(const Condition&)>;
    using EffectFn = std::function<void(const Effect&)>;

    // Open `npc`'s tree at the first entry whose conditions pass. Returns false
    // when the npc has no tree or no entry is available (which is a content
    // bug worth logging, not a silent no-op).
    bool begin(const DialogueLibrary&, const std::string& npc,
               const ConditionFn&, const EffectFn&);
    void end();
    bool active() const { return mTree != nullptr; }

    const std::string& speaker() const;
    const std::string& text() const;
    const std::string& nodeId() const { return mNodeId; }

    // The choices actually offered, in authored order. Locked choices appear
    // only when they asked to.
    struct Offered {
        const DialogueChoice* choice = nullptr;
        int index = 0;   // index into the node's choice list
        bool locked = false;
    };
    const std::vector<Offered>& choices() const { return mOffered; }

    // Take the nth *offered* choice (not the nth authored one). Fires its
    // effects, advances, and re-evaluates. Returns false for an out-of-range or
    // locked choice; the conversation is unchanged.
    bool choose(int offeredIndex, const ConditionFn&, const EffectFn&);

private:
    void enter(const std::string& nodeId, const ConditionFn&, const EffectFn&);
    void refreshChoices(const ConditionFn&);

    const DialogueTree* mTree = nullptr;
    const DialogueNode* mNode = nullptr;
    std::string mNodeId;
    std::vector<Offered> mOffered;
};

} // namespace game::rpg
