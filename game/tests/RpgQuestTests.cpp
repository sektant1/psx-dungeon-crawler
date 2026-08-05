// Quests and dialogue.
//
// The quest system's whole claim is that a quest subscribes to a channel while
// it is active and to nothing when it is not. That is what these assert: an
// inactive quest cannot advance, a completed one cannot complete twice, and a
// handler that assigns the next quest in a chain while the channel is
// dispatching does not corrupt the walk.
#include "../src/rpg/Dialogue.h"
#include "../src/rpg/Quests.h"

#include <algorithm>
#include <cstdio>

using namespace game::rpg;

static int failures = 0;
static void check(bool c, const char* m)
{
    if (!c) { std::fprintf(stderr, "FAIL: %s\n", m); ++failures; }
}

static const char* kQuests = R"(
[quest.gather]
type = "collect"
title = "Gather"
target = "stone"
amount = 3
[quest.gather.reward]
xp = 100

[quest.slay]
type = "defeat"
title = "Slay"
target = "rat"
amount = 2

[quest.slay_anything]
type = "defeat"
title = "Slay Anything"
target = ""
amount = 2

[quest.deep]
type = "depth"
title = "Deep"
amount = 3

[quest.deep_and_out]
type = "depth"
title = "Deep And Out"
amount = 2
extra = "extract"

[quest.speak]
type = "talk"
title = "Speak"
target = "smith"
amount = 1

[quest.witness]
type = "flag"
title = "Witness"
target = "saw.it"

[quest.gated]
type = "flag"
title = "Gated"
target = "anything"
[[quest.gated.requirement]]
kind = "flag_set"
subject = "allowed"

[quest.repeat]
type = "defeat"
title = "Repeatable"
target = "rat"
amount = 1
repeatable = true

[quest.chained]
type = "flag"
title = "Chained"
target = "never"
)";

int main()
{
    QuestLibrary library;
    check(library.loadFromString(kQuests), "the quest table loads");
    check(library.size() == 10, "ten quests");

    // A quest whose `type` names no subclass is refused at load. It would sit
    // Active forever, which reads to a player as a broken game.
    {
        QuestLibrary bad;
        bad.loadFromString(R"(
[quest.nonsense]
type = "no_such_kind"
title = "Nonsense"
)");
        check(bad.size() == 0, "an unknown quest type is refused at load");
    }

    GameChannels channels;
    QuestBook book;
    book.bind(channels);

    // --- an inactive quest is not subscribed --------------------------------
    check(!book.isStarted("slay"), "unstarted quests hold no state at all");
    channels.combat.raise(eng::intern("rat"), 1);
    check(!book.isStarted("slay"), "and an event does not create one");

    // --- collect tests the holding, not the acquisitions --------------------
    {
        check(book.assignUnchecked(library, "gather"), "gather assigned");
        check(book.isActive("gather"), "and is active");
        // The inventory channel carries the new *total*, so a quest asking for
        // three cannot be satisfied by picking one up three times.
        channels.inventory.raise(eng::intern("stone"), 1, 1);
        check(book.isActive("gather"), "one is not three");
        channels.inventory.raise(eng::intern("stone"), 1, 1);
        check(book.isActive("gather"), "still not three");
        channels.inventory.raise(eng::intern("stone"), 3, 2);
        check(book.isFinished("gather"), "three is three");
        check(book.find("gather")->progress() == 3, "and progress says so");

        // A completed quest is off the channels: it cannot complete twice.
        //
        // The subscription is cancelled at the end of the block. A handler
        // capturing a local by reference outlives that local otherwise, which
        // is exactly the leak ASan caught the first time this was written --
        // and it is the reason subscribe() hands back an id at all.
        int completions = 0;
        const SubscriptionId watch =
            channels.quests.completed.subscribe([&](Quest&) { ++completions; });
        channels.inventory.raise(eng::intern("stone"), 9, 6);
        check(completions == 0, "a finished quest does not fire again");
        channels.quests.completed.unsubscribe(watch);

        // Turning in moves it on and cannot happen twice either.
        check(book.turnIn("gather") != nullptr, "turned in");
        check(book.stateOf("gather") == QuestState::TurnedIn, "and stays that way");
        check(book.turnIn("gather") == nullptr, "a second turn-in is refused");
    }

    // --- defeat counts events -----------------------------------------------
    {
        book.assignUnchecked(library, "slay");
        channels.combat.raise(eng::intern("wolf"), 1);
        check(book.isActive("slay"), "a wolf is not a rat");
        channels.combat.raise(eng::intern("rat"), 1);
        channels.combat.raise(eng::intern("rat"), 1);
        check(book.isFinished("slay"), "two rats is two rats");

        // An empty target is a wildcard: "survive ten of them" needs no list.
        book.assignUnchecked(library, "slay_anything");
        channels.combat.raise(eng::intern("wolf"), 1);
        channels.combat.raise(eng::intern("bear"), 1);
        check(book.isFinished("slay_anything"), "anything counts");
    }

    // --- depth, and the extraction variant -----------------------------------
    {
        book.assignUnchecked(library, "deep");
        channels.depth.raise(2, false);
        check(book.isActive("deep"), "two is not three");
        channels.depth.raise(3, false);
        check(book.isFinished("deep"), "reaching three is enough");

        book.assignUnchecked(library, "deep_and_out");
        channels.depth.raise(5, /*extracted=*/false);
        check(book.isActive("deep_and_out"),
              "going deep is not the same as coming back");
        channels.depth.raise(2, /*extracted=*/true);
        check(book.isFinished("deep_and_out"), "getting out is what counts");
    }

    // --- talk and flag --------------------------------------------------------
    {
        book.assignUnchecked(library, "speak");
        channels.npcs.raise(eng::intern("baker"));
        check(book.isActive("speak"), "the wrong person does not count");
        channels.npcs.raise(eng::intern("smith"));
        check(book.isFinished("speak"), "the right one does");

        book.assignUnchecked(library, "witness");
        channels.flags.raise(eng::intern("something.else"));
        check(book.isActive("witness"), "the wrong flag does not count");
        channels.flags.raise(eng::intern("saw.it"));
        check(book.isFinished("witness"), "the right one does");
    }

    // --- requirements ---------------------------------------------------------
    {
        bool allowed = false;
        const auto evaluate = [&](const Condition& c) {
            return c.kind == ConditionKind::FlagSet ? allowed : true;
        };
        check(!book.assign(library, "gated", evaluate),
              "an unmet requirement refuses the assignment");
        check(!book.isStarted("gated"), "and nothing was created");
        allowed = true;
        check(book.assign(library, "gated", evaluate),
              "a met requirement allows it");
        check(book.isActive("gated"), "and it is active");
    }

    // --- repeatable ------------------------------------------------------------
    {
        book.assignUnchecked(library, "repeat");
        channels.combat.raise(eng::intern("rat"), 1);
        check(book.isFinished("repeat"), "done once");
        book.turnIn("repeat");
        check(book.assignUnchecked(library, "repeat"), "and can be taken again");
        check(book.isActive("repeat"), "with its progress reset");
        check(book.find("repeat")->progress() == 0, "from zero");

        // A non-repeatable quest cannot be re-assigned.
        check(!book.assignUnchecked(library, "slay"),
              "a finished one-shot cannot be retaken");
    }

    // --- re-entrancy ------------------------------------------------------------
    //
    // The normal case IS re-entrant: a quest completes inside a handler, which
    // unsubscribes it from the channel currently dispatching, and the handler
    // may assign the next quest in a chain while that happens.
    {
        QuestBook chain;
        chain.bind(channels);
        int assigned = 0;
        const SubscriptionId onComplete =
            channels.quests.completed.subscribe([&](Quest& q) {
                if (q.id() == "slay_anything")
                    chain.assignUnchecked(library, "chained");
            });
        const SubscriptionId onAssign =
            channels.quests.assigned.subscribe([&](Quest&) { ++assigned; });
        chain.assignUnchecked(library, "slay_anything");
        channels.combat.raise(eng::intern("anything"), 1);
        channels.combat.raise(eng::intern("anything"), 1);
        check(chain.isFinished("slay_anything"), "it completed");
        check(chain.isActive("chained"), "and started the next one from inside "
                                         "the dispatch");
        check(assigned == 2, "both assignments were announced");
        channels.quests.completed.unsubscribe(onComplete);
        channels.quests.assigned.unsubscribe(onAssign);
        // The book goes out of scope here, and its quests unsubscribe in their
        // destructor -- which is what keeps the outer `channels` usable below.
    }

    // --- forceComplete and fail -------------------------------------------------
    {
        QuestBook other;
        other.bind(channels);
        check(other.forceComplete(library, "deep"),
              "a conversation can resolve a quest that was never started");
        check(other.isFinished("deep"), "and it is complete");

        other.assignUnchecked(library, "witness");
        check(other.fail("witness"), "a quest can fail");
        check(other.stateOf("witness") == QuestState::Failed, "and stays failed");
        channels.flags.raise(eng::intern("saw.it"));
        check(other.stateOf("witness") == QuestState::Failed,
              "a failed quest is off the channels");
    }

    // --- rebind across a content reload -------------------------------------------
    {
        QuestBook live;
        live.bind(channels);
        live.assignUnchecked(library, "slay");
        channels.combat.raise(eng::intern("rat"), 1);
        check(live.find("slay")->progress() == 1, "one rat so far");

        const std::vector<std::string> dropped = live.rebind(library);
        check(dropped.empty(), "nothing was dropped by a no-op reload");
        check(live.find("slay") != nullptr, "the quest survived");
        check(live.find("slay")->progress() == 1, "with its progress");
        // And it is still subscribed after the rebuild.
        channels.combat.raise(eng::intern("rat"), 1);
        check(live.isFinished("slay"), "and still counting");

        // A quest whose row disappears is reported rather than silently lost.
        QuestLibrary shrunk;
        shrunk.loadFromString(R"(
[quest.gather]
type = "collect"
title = "Gather"
target = "stone"
amount = 3
)");
        QuestBook orphaned;
        orphaned.bind(channels);
        orphaned.assignUnchecked(library, "speak");
        const std::vector<std::string> lost = orphaned.rebind(shrunk);
        check(lost.size() == 1 && lost[0] == "speak",
              "a quest whose row is gone is reported");
    }

    // =====================================================================
    // Dialogue
    // =====================================================================

    DialogueLibrary trees;
    check(trees.loadFromString(R"(
[dialogue.smith]
speaker = "The Smith"

[[dialogue.smith.entry]]
node = "known"
[[dialogue.smith.entry.condition]]
kind = "flag_set"
subject = "met.smith"

[[dialogue.smith.entry]]
node = "greeting"

[dialogue.smith.node.greeting]
text = "You are new."
[[dialogue.smith.node.greeting.effect]]
kind = "set_flag"
subject = "met.smith"
[[dialogue.smith.node.greeting.choice]]
text = "Show me the good steel."
next = "steel"
[[dialogue.smith.node.greeting.choice.condition]]
kind = "level_at_least"
value = 5
[[dialogue.smith.node.greeting.choice]]
text = "Nothing today."
next = ""

[dialogue.smith.node.steel]
text = "It is not for sale."
[[dialogue.smith.node.steel.choice]]
text = "[Leave]"
next = ""

[dialogue.smith.node.known]
text = "Back again."
[[dialogue.smith.node.known.choice]]
text = "[Leave]"
next = ""
)"),
          "the dialogue tree loads");

    {
        int level = 1;
        std::vector<std::string> fired;
        const auto evaluate = [&](const Condition& c) {
            switch (c.kind) {
                case ConditionKind::LevelAtLeast: return level >= c.value;
                case ConditionKind::FlagSet:
                    return std::find(fired.begin(), fired.end(), c.subject) !=
                           fired.end();
                default: return true;
            }
        };
        const auto effect = [&](const Effect& e) { fired.push_back(e.subject); };

        DialogueRunner runner;
        check(runner.begin(trees, "smith", evaluate, effect), "the tree opens");
        check(runner.active(), "and is running");
        check(runner.speaker() == "The Smith", "the speaker is named");
        check(runner.text() == "You are new.", "at the unconditional entry");
        // onEnter effects fire once, when the node is shown.
        check(fired.size() == 1 && fired[0] == "met.smith",
              "the node's effect fired on entry");

        // A locked choice is hidden by default, because a greyed-out
        // "[Bring me the ledger]" is a quest marker in disguise.
        check(runner.choices().size() == 1,
              "the level-gated choice is hidden, not greyed out");
        check(runner.choices()[0].choice->text == "Nothing today.",
              "only the open one is offered");

        // Choosing the last offered index ends the conversation.
        check(runner.choose(0, evaluate, effect), "the choice is taken");
        check(!runner.active(), "an empty `next` ends it");

        // Out-of-range choices are refused rather than indexing off the end.
        check(!runner.choose(0, evaluate, effect), "no choice while inactive");

        // With the requirement met, the gated choice appears and leads on.
        level = 5;
        check(runner.begin(trees, "smith", evaluate, effect), "reopened");
        // met.smith is set now, so the *first* entry whose conditions pass is
        // the returning-visitor one.
        check(runner.text() == "Back again.", "a later visit enters elsewhere");
        check(runner.choices().size() == 1, "one way out");
        runner.choose(0, evaluate, effect);
        check(!runner.active(), "and it closed");

        // A tree nobody authored refuses to open rather than half-running.
        DialogueRunner missing;
        check(!missing.begin(trees, "nobody", evaluate, effect),
              "an npc with no tree cannot be talked to");
        check(!missing.active(), "and the runner stays closed");
    }

    // A tree with no unconditional entry is a content bug: it refuses to open,
    // loudly, rather than leaving the player facing a silent NPC.
    {
        DialogueLibrary broken;
        broken.loadFromString(R"(
[dialogue.ghost]
speaker = "A Ghost"
[[dialogue.ghost.entry]]
node = "only"
[[dialogue.ghost.entry.condition]]
kind = "flag_set"
subject = "impossible"
[dialogue.ghost.node.only]
text = "..."
)");
        DialogueRunner runner;
        const auto never = [](const Condition&) { return false; };
        check(!runner.begin(broken, "ghost", never, {}),
              "a tree with no reachable entry refuses to open");
    }

    if (failures == 0)
        std::printf("RpgQuestTests OK\n");
    return failures == 0 ? 0 : 1;
}
