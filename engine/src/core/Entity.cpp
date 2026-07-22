#include <eng/Entity.h>

namespace eng {

Entity::~Entity() = default;

Component* Entity::addComponent(Component::StrongPtr c) {
    c->setOwner(this);
    Component* raw = c.get();
    mComponents.push_back(std::move(c));
    return raw;
}

void Entity::removeAllComponents() {
    for (auto& c : mComponents) c->terminate();
    mComponents.clear();
}

} // namespace eng
