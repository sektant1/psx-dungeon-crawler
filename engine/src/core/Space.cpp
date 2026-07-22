#include <eng/Space.h>
#include <algorithm>

namespace eng {

Space::~Space() = default;

GameObject* Space::createObject(std::string name) {
    mObjects.push_back(std::make_unique<GameObject>(std::move(name)));
    return mObjects.back().get();
}

void Space::destroyObject(GameObject* obj) {
    if (obj && std::find(mToDestroy.begin(), mToDestroy.end(), obj) == mToDestroy.end())
        mToDestroy.push_back(obj);
}

void Space::flushDestroyed() {
    for (GameObject* dead : mToDestroy) {
        auto it = std::find_if(mObjects.begin(), mObjects.end(),
            [dead](const std::unique_ptr<GameObject>& p) { return p.get() == dead; });
        if (it != mObjects.end()) {
            (*it)->removeAllComponents();
            mObjects.erase(it);
        }
    }
    mToDestroy.clear();
}

void Space::clear() {
    for (auto& o : mObjects) o->removeAllComponents();
    mObjects.clear();
    mToDestroy.clear();
}

} // namespace eng
