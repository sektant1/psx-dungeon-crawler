#pragma once
#include <eng/Object.h>
#include <memory>

namespace eng {

class Entity;

// Abstract unit of behaviour/data attached to an Entity. Constructed either
// directly or by name through Factory. Owned (unique_ptr) by its Entity;
// passed around as a raw Ptr.
class Component : public Object {
public:
    using Ptr = Component*;
    using StrongPtr = std::unique_ptr<Component>;

    explicit Component(std::string name) : Object(std::move(name)) {}
    ~Component() override = default;

    Entity* owner() const { return mOwner; }
    void setOwner(Entity* e) { mOwner = e; }

    virtual void initialize() {}
    virtual void terminate() {}

private:
    Entity* mOwner = nullptr;
};

} // namespace eng
