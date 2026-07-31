#include "eng/Loading.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using eng::LoadPlan;
using eng::LoadRunner;

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "LoadingTests: " << message << '\n';
        std::exit(1);
    }
}

bool near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) < eps; }

// A hand-cranked clock: tests advance it explicitly so slicing behaviour does
// not depend on how fast the machine running them happens to be.
struct FakeClock {
    double now = 0.0;
    double operator()() const { return now; }
};

void runsEveryStepInOrder()
{
    std::vector<std::string> order;
    LoadPlan plan;
    plan.add("first", [&] { order.push_back("first"); });
    plan.add("second", [&] { order.push_back("second"); });
    plan.add("third", [&] { order.push_back("third"); });

    LoadRunner runner(std::move(plan));
    runner.runAll();

    require(runner.done(), "plan did not finish");
    require(order.size() == 3, "wrong step count");
    require(order[0] == "first" && order[1] == "second" && order[2] == "third",
            "steps ran out of order");
    require(near(runner.progress(), 1.0f), "progress did not reach 1");
    require(runner.completed() == 3 && runner.count() == 3, "bad counters");
}

void progressFollowsWeight()
{
    LoadPlan plan;
    plan.add("cheap", [] {}, 1.0f);
    plan.add("expensive", [] {}, 3.0f);

    auto clock = std::make_shared<FakeClock>();
    // Zero budget: one step per slice, so progress can be sampled between them.
    LoadRunner runner(std::move(plan), [clock] { return clock->now; });
    require(near(runner.progress(), 0.0f), "progress should start at 0");
    runner.slice(0.0f);
    require(near(runner.progress(), 0.25f), "weight 1 of 4 should be 25%");
    runner.slice(0.0f);
    require(near(runner.progress(), 1.0f), "progress should end at 1");
}

void resumableStepSpansSlices()
{
    int ticks = 0;
    LoadPlan plan;
    plan.addResumable("chunked", [&] { return ++ticks >= 3; });

    auto clock = std::make_shared<FakeClock>();
    LoadRunner runner(std::move(plan), [clock] { return clock->now; });

    runner.slice(0.0f);
    require(ticks == 1, "zero budget should buy exactly one tick");
    require(!runner.done(), "unfinished step must not complete the plan");
    require(near(runner.progress(), 0.0f),
            "a half-done step must not count toward progress");
    runner.slice(0.0f);
    runner.slice(0.0f);
    require(runner.done(), "third tick should finish the step");
    require(ticks == 3, "step ticked too many times");
}

void budgetLimitsWorkPerSlice()
{
    // Each tick costs 4 ms of fake time; a 10 ms budget buys three of them
    // (the budget is checked after a tick, so the one that crosses it still
    // runs -- that is the guarantee that oversized steps make progress).
    auto clock = std::make_shared<FakeClock>();
    int ran = 0;
    LoadPlan plan;
    for (int i = 0; i < 8; ++i)
        plan.add("step", [&, clock] {
            ++ran;
            clock->now += 4.0;
        });

    LoadRunner runner(std::move(plan), [clock] { return clock->now; });
    runner.slice(10.0f);
    require(ran == 3, "budget did not stop the slice where expected");
    require(!runner.done(), "slice consumed the whole plan");
}

void oversizedStepStillCompletes()
{
    auto clock = std::make_shared<FakeClock>();
    bool ran = false;
    LoadPlan plan;
    plan.add("one very slow step", [&, clock] {
        ran = true;
        clock->now += 5000.0; // five seconds inside an 8 ms budget
    });

    LoadRunner runner(std::move(plan), [clock] { return clock->now; });
    runner.slice(8.0f);
    require(ran, "a step heavier than the budget never ran");
    require(runner.done(), "loader would have spun forever on this step");
}

void reportsPerStepTimings()
{
    auto clock = std::make_shared<FakeClock>();
    LoadPlan plan;
    plan.add("fast", [clock] { clock->now += 2.0; });
    plan.add("slow", [clock] { clock->now += 20.0; });

    LoadRunner runner(std::move(plan), [clock] { return clock->now; });
    runner.runAll();

    const auto& t = runner.timings();
    require(t.size() == 2, "missing timings");
    require(t[0].label == "fast" && near(t[0].ms, 2.0f), "bad first timing");
    require(t[1].label == "slow" && near(t[1].ms, 20.0f), "bad second timing");
    require(near(runner.elapsedMs(), 22.0f), "bad total elapsed");
}

void emptyPlanIsDone()
{
    LoadRunner runner{LoadPlan{}};
    require(runner.done(), "empty plan should start done");
    require(near(runner.progress(), 1.0f), "empty plan should read as complete");
    runner.slice(8.0f); // must not crash
}

void labelTracksCurrentStep()
{
    LoadPlan plan;
    plan.add("cooking scene", [] {});
    plan.add("compiling materials", [] {});

    auto clock = std::make_shared<FakeClock>();
    LoadRunner runner(std::move(plan), [clock] { return clock->now; });
    require(runner.label() == "cooking scene", "label should start on step 1");
    runner.slice(0.0f);
    require(runner.label() == "cooking scene",
            "label must name the step that just ran, not the next one");
    runner.slice(0.0f);
    require(runner.label() == "compiling materials", "label did not advance");
}

} // namespace

int main()
{
    runsEveryStepInOrder();
    progressFollowsWeight();
    resumableStepSpansSlices();
    budgetLimitsWorkPerSlice();
    oversizedStepStillCompletes();
    reportsPerStepTimings();
    emptyPlanIsDone();
    labelTracksCurrentStep();
    std::cout << "LoadingTests: ok\n";
    return 0;
}
