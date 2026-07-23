#include <eng/Actions.h>

namespace eng {

ActionSequence& Actions::sequence() {
    auto set = std::make_unique<ActionSequence>();
    ActionSequence& ref = *set;
    mRoots.push_back(std::move(set));
    return ref;
}

ActionGroup& Actions::group() {
    auto set = std::make_unique<ActionGroup>();
    ActionGroup& ref = *set;
    mRoots.push_back(std::move(set));
    return ref;
}

void Actions::update(float dt) {
    for (auto& s : mRoots) s->update(dt);
    for (auto it = mRoots.begin(); it != mRoots.end();)
        it = (*it)->finished() ? mRoots.erase(it) : std::next(it);
}

} // namespace eng
