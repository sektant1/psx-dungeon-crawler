#pragma once
#include <eng/ActionSet.h>
#include <eng/System.h>
#include <memory>
#include <vector>

namespace eng {

// System that owns root action sets and advances them each frame, pruning
// finished sets. Build via sequence()/group() and chain on the returned set.
class Actions : public System {
public:
    Actions() : System("Actions") {}

    ActionSequence& sequence();
    ActionGroup& group();

    void update(float dt) override;
    std::size_t activeCount() const { return mRoots.size(); }
    void clear() { mRoots.clear(); }

private:
    std::vector<std::unique_ptr<ActionSet>> mRoots;
};

} // namespace eng
