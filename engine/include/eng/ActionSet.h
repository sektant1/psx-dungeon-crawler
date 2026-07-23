#pragma once
#include <eng/Action.h>
#include <vector>

namespace eng {

// A composite action holding children, with chaining builders. Concrete
// subclasses define serial (Sequence) vs parallel (Group) semantics.
class ActionSet : public Action {
public:
    ActionSet& add(ActionPtr a) { mActions.push_back(std::move(a)); return *this; }

    ActionSet& delay(float duration) { return add(std::make_unique<ActionDelay>(duration)); }
    ActionSet& call(std::function<void()> fn) { return add(std::make_unique<ActionCall>(std::move(fn))); }
    template <typename T>
    ActionSet& property(T& prop, T target, float duration, Ease ease) {
        return add(std::make_unique<ActionProperty<T>>(prop, std::move(target), duration, ease));
    }

    bool empty() const { return mActions.empty(); }

protected:
    std::vector<ActionPtr> mActions;
};

// Runs children one after another; instantaneous children chain within a frame.
class ActionSequence : public ActionSet {
public:
    float update(float dt) override {
        float remaining = dt;
        while (!mActions.empty()) {
            float lo = mActions.front()->update(remaining);
            bool done = mActions.front()->finished();
            if (done) {
                mActions.erase(mActions.begin());
                remaining = lo;
            } else {
                break;
            }
        }
        if (mActions.empty()) mFinished = true;
        return mFinished ? remaining : 0.0f;
    }
};

// Runs all children simultaneously; finishes when all children finish.
class ActionGroup : public ActionSet {
public:
    float update(float dt) override {
        for (auto& a : mActions) a->update(dt);
        for (auto it = mActions.begin(); it != mActions.end();)
            it = (*it)->finished() ? mActions.erase(it) : std::next(it);
        if (mActions.empty()) mFinished = true;
        return 0.0f;
    }
};

} // namespace eng
