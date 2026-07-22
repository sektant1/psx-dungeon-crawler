#pragma once
#include <eng/GameObject.h>
#include <memory>
#include <vector>

namespace eng {

// A container of game objects = one loaded level context.
class Space : public Entity {
public:
    explicit Space(std::string name) : Entity(std::move(name)) {}
    ~Space() override;

    GameObject* createObject(std::string name);
    const std::vector<std::unique_ptr<GameObject>>& objects() const { return mObjects; }

    void destroyObject(GameObject* obj);   // deferred until flushDestroyed()
    void flushDestroyed();
    void clear();

private:
    std::vector<std::unique_ptr<GameObject>> mObjects;
    std::vector<GameObject*> mToDestroy;
};

} // namespace eng
