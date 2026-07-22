#pragma once
#include <eng/Object.h>
#include <memory>

namespace eng {

// Abstract engine subsystem updated once per frame by eng::Engine.
class System : public Object {
public:
    using StrongPtr = std::unique_ptr<System>;

    explicit System(std::string name) : Object(std::move(name)) {}
    ~System() override = default;

    virtual void initialize() {}
    virtual void terminate() {}
    virtual void update(float dt) = 0;
};

} // namespace eng
