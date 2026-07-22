#pragma once
#include <eng/Entity.h>

namespace eng {

// An Entity that also carries an archetype tag (the name it was constructed
// from via Factory/Content). Weak Ptr for passing around.
class GameObject : public Entity {
public:
    using Ptr = GameObject*;

    explicit GameObject(std::string name) : Entity(std::move(name)) {}

    const std::string& archetype() const { return mArchetype; }
    void setArchetype(std::string a) { mArchetype = std::move(a); }

private:
    std::string mArchetype;
};

} // namespace eng
