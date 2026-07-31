#include <eng/Loading.h>

#include <eng/Log.h>

#include <chrono>
#include <utility>

namespace eng {
namespace {

double steadyMs()
{
    using namespace std::chrono;
    return duration<double, std::milli>(steady_clock::now().time_since_epoch())
        .count();
}

} // namespace

void LoadPlan::add(std::string label, std::function<void()> work, float weight)
{
    addResumable(std::move(label),
                 [w = std::move(work)]() {
                     if (w)
                         w();
                     return true;
                 },
                 weight);
}

void LoadPlan::addResumable(std::string label, std::function<bool()> work,
                            float weight)
{
    LoadStep step;
    step.label = std::move(label);
    step.weight = weight > 0.0f ? weight : 0.0f;
    step.tick = std::move(work);
    mSteps.push_back(std::move(step));
}

float LoadPlan::totalWeight() const
{
    float total = 0.0f;
    for (const LoadStep& s : mSteps)
        total += s.weight;
    return total;
}

LoadRunner::LoadRunner(LoadPlan plan, Clock clock)
    : mPlan(std::move(plan)), mClock(clock ? std::move(clock) : Clock(steadyMs))
{
    mTotalWeight = mPlan.totalWeight();
    if (!mPlan.steps().empty())
        mLabel = mPlan.steps().front().label;
}

void LoadRunner::slice(float budgetMs)
{
    if (done())
        return;

    const double sliceStart = mClock();
    do {
        const LoadStep& step = mPlan.steps()[mIndex];
        if (!mStepStarted) {
            mLabel = step.label;
            mStepStartMs = float(mClock());
            mStepStarted = true;
        }
        const bool finished = !step.tick || step.tick();
        if (finished) {
            const float ms = float(mClock()) - mStepStartMs;
            mTimings.push_back({step.label, ms});
            mDoneWeight += step.weight;
            mStepStarted = false;
            ++mIndex;
        }
        // The budget is checked after at least one tick so a step heavier than
        // the whole budget still advances; without that the loop would spin
        // forever drawing the same frame.
    } while (!done() && float(mClock() - sliceStart) < budgetMs);

    mElapsedMs += float(mClock() - sliceStart);
}

void LoadRunner::runAll()
{
    while (!done())
        slice(0.0f);
}

float LoadRunner::progress() const
{
    if (mTotalWeight <= 0.0f)
        return done() ? 1.0f : 0.0f;
    return mDoneWeight / mTotalWeight;
}

} // namespace eng
