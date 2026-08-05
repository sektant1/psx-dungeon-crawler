#include "Dialogue.h"

#include "Quests.h" // readConditionArray / readEffectArray

#include <eng/Log.h>

#define TOML_EXCEPTIONS 0
#include <tomlplusplus/toml.hpp>

#include <algorithm>

namespace game::rpg {

bool DialogueLibrary::parse(const void* tomlTable)
{
    const toml::table& root = *static_cast<const toml::table*>(tomlTable);
    const toml::table* group = root["dialogue"].as_table();
    if (!group) {
        eng::log::error("DialogueLibrary: document defines no [dialogue.*] trees");
        return false;
    }

    mTrees.clear();
    for (auto&& [key, node] : *group) {
        const toml::table* t = node.as_table();
        if (!t)
            continue;
        DialogueTree tree;
        tree.id = std::string(key.str());
        tree.speaker = (*t)["speaker"].value_or(tree.id);

        if (const toml::array* entries = (*t)["entry"].as_array()) {
            for (const toml::node& e : *entries) {
                const toml::table* et = e.as_table();
                if (!et)
                    continue;
                DialogueEntry entry;
                entry.node = (*et)["node"].value_or(std::string());
                if (entry.node.empty()) {
                    eng::log::error("DialogueLibrary: '%s' has an entry with "
                                    "no node", tree.id.c_str());
                    continue;
                }
                entry.conditions = readConditionArray(et, "condition", tree.id);
                tree.entries.push_back(std::move(entry));
            }
        }

        if (const toml::table* nodes = (*t)["node"].as_table()) {
            for (auto&& [nodeKey, nodeVal] : *nodes) {
                const toml::table* nt = nodeVal.as_table();
                if (!nt)
                    continue;
                DialogueNode n;
                n.id = std::string(nodeKey.str());
                n.speaker = (*nt)["speaker"].value_or(std::string());
                n.text = (*nt)["text"].value_or(std::string());
                n.onEnter = readEffectArray(nt, "effect", tree.id + "." + n.id);
                if (const toml::array* choices = (*nt)["choice"].as_array()) {
                    for (const toml::node& c : *choices) {
                        const toml::table* ct = c.as_table();
                        if (!ct)
                            continue;
                        DialogueChoice choice;
                        choice.text = (*ct)["text"].value_or(std::string());
                        choice.next = (*ct)["next"].value_or(std::string());
                        choice.showWhenLocked =
                            (*ct)["show_when_locked"].value_or(false);
                        choice.conditions =
                            readConditionArray(ct, "condition",
                                               tree.id + "." + n.id);
                        choice.effects = readEffectArray(ct, "effect",
                                                         tree.id + "." + n.id);
                        n.choices.push_back(std::move(choice));
                    }
                }
                tree.nodes[n.id] = std::move(n);
            }
        }

        if (tree.entries.empty() || tree.nodes.empty()) {
            eng::log::error("DialogueLibrary: '%s' has %d entries and %d "
                            "nodes; a tree needs at least one of each. Dropped.",
                            tree.id.c_str(), int(tree.entries.size()),
                            int(tree.nodes.size()));
            continue;
        }
        // Dangling `next` is the failure that turns into "the conversation
        // ends for no reason", three characters into a branch nobody tested.
        // Report it at load, where the file is in front of the author.
        for (const auto& [id, n] : tree.nodes)
            for (const DialogueChoice& c : n.choices)
                if (!c.next.empty() && !tree.nodes.count(c.next))
                    eng::log::error("DialogueLibrary: '%s.%s' offers a choice "
                                    "leading to node '%s', which does not "
                                    "exist", tree.id.c_str(), id.c_str(),
                                    c.next.c_str());
        for (const DialogueEntry& e : tree.entries)
            if (!tree.nodes.count(e.node))
                eng::log::error("DialogueLibrary: '%s' enters at node '%s', "
                                "which does not exist", tree.id.c_str(),
                                e.node.c_str());

        mTrees[tree.id] = std::move(tree);
    }

    eng::log::info("DialogueLibrary: %d conversations", int(mTrees.size()));
    return !mTrees.empty();
}

bool DialogueLibrary::load(const std::string& tomlPath)
{
    toml::parse_result parsed = toml::parse_file(tomlPath);
    if (!parsed) {
        eng::log::error("DialogueLibrary: %s: %s", tomlPath.c_str(),
                        std::string(parsed.error().description()).c_str());
        mTrees.clear();
        return false;
    }
    return parse(&parsed.table());
}

bool DialogueLibrary::loadFromString(const char* tomlSrc)
{
    toml::parse_result parsed = toml::parse(tomlSrc);
    if (!parsed) {
        eng::log::error("DialogueLibrary: %s",
                        std::string(parsed.error().description()).c_str());
        mTrees.clear();
        return false;
    }
    return parse(&parsed.table());
}

const DialogueTree* DialogueLibrary::find(const std::string& npc) const
{
    const auto it = mTrees.find(npc);
    return it == mTrees.end() ? nullptr : &it->second;
}

std::vector<std::string> DialogueLibrary::ids() const
{
    std::vector<std::string> out;
    out.reserve(mTrees.size());
    for (const auto& [id, t] : mTrees)
        out.push_back(id);
    std::sort(out.begin(), out.end());
    return out;
}

// ---------------------------------------------------------------------------
// DialogueRunner
// ---------------------------------------------------------------------------

namespace {

bool allPass(const std::vector<Condition>& conditions,
             const DialogueRunner::ConditionFn& evaluate)
{
    if (!evaluate)
        return true;
    for (const Condition& c : conditions)
        if (!evaluate(c))
            return false;
    return true;
}

} // namespace

bool DialogueRunner::begin(const DialogueLibrary& library,
                           const std::string& npc, const ConditionFn& evaluate,
                           const EffectFn& fire)
{
    end();
    const DialogueTree* tree = library.find(npc);
    if (!tree) {
        eng::log::error("Dialogue: '%s' has nothing to say (no tree)",
                        npc.c_str());
        return false;
    }
    mTree = tree;
    // First entry whose conditions pass, in authored order. Authors put the
    // most specific greeting first and the unconditional one last, which is
    // the only ordering rule this system has.
    for (const DialogueEntry& e : tree->entries) {
        if (!allPass(e.conditions, evaluate))
            continue;
        enter(e.node, evaluate, fire);
        if (mNode)
            return true;
    }
    eng::log::error("Dialogue: '%s' has no entry whose conditions pass; the "
                    "tree needs an unconditional fallback", npc.c_str());
    mTree = nullptr;
    return false;
}

void DialogueRunner::enter(const std::string& nodeId,
                           const ConditionFn& evaluate, const EffectFn& fire)
{
    mNode = nullptr;
    mNodeId.clear();
    mOffered.clear();
    if (!mTree || nodeId.empty())
        return;
    const auto it = mTree->nodes.find(nodeId);
    if (it == mTree->nodes.end()) {
        eng::log::error("Dialogue: '%s' has no node '%s'", mTree->id.c_str(),
                        nodeId.c_str());
        return;
    }
    mNode = &it->second;
    mNodeId = nodeId;
    if (fire)
        for (const Effect& e : mNode->onEnter)
            fire(e);
    refreshChoices(evaluate);
}

void DialogueRunner::refreshChoices(const ConditionFn& evaluate)
{
    mOffered.clear();
    if (!mNode)
        return;
    for (std::size_t i = 0; i < mNode->choices.size(); ++i) {
        const DialogueChoice& c = mNode->choices[i];
        const bool ok = allPass(c.conditions, evaluate);
        if (!ok && !c.showWhenLocked)
            continue;
        mOffered.push_back({&c, int(i), !ok});
    }
}

bool DialogueRunner::choose(int offeredIndex, const ConditionFn& evaluate,
                            const EffectFn& fire)
{
    if (offeredIndex < 0 || offeredIndex >= int(mOffered.size()))
        return false;
    const Offered picked = mOffered[std::size_t(offeredIndex)];
    if (picked.locked || !picked.choice)
        return false;

    // Effects fire before the move, so a choice that gives an item and leads to
    // a node gated on having it works. Copy the target first: firing effects
    // cannot invalidate the tree (it is const content), but the node pointer is
    // about to be replaced either way.
    const std::string next = picked.choice->next;
    if (fire)
        for (const Effect& e : picked.choice->effects)
            fire(e);

    if (next.empty()) {
        end();
        return true;
    }
    enter(next, evaluate, fire);
    // A choice pointing at a node the library rejected ends the conversation
    // rather than leaving the runner active with nothing to show.
    if (!mNode)
        end();
    return true;
}

void DialogueRunner::end()
{
    mTree = nullptr;
    mNode = nullptr;
    mNodeId.clear();
    mOffered.clear();
}

const std::string& DialogueRunner::speaker() const
{
    static const std::string kEmpty;
    if (!mTree)
        return kEmpty;
    if (mNode && !mNode->speaker.empty())
        return mNode->speaker;
    return mTree->speaker;
}

const std::string& DialogueRunner::text() const
{
    static const std::string kEmpty;
    return mNode ? mNode->text : kEmpty;
}

} // namespace game::rpg
