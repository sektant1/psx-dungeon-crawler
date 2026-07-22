#pragma once
#include <eng/Component.h>
#include <vector>

namespace eng {

// A named container of components.
class Entity : public Object {
public:
    explicit Entity(std::string name) : Object(std::move(name)) {}
    ~Entity() override;

    // Takes ownership; sets owner back-ptr; returns a weak pointer.
    Component* addComponent(Component::StrongPtr c);

    template <typename T> T* getComponent() {
        for (auto& c : mComponents)
            if (auto* p = dynamic_cast<T*>(c.get())) return p;
        return nullptr;
    }

    const std::vector<Component::StrongPtr>& components() const { return mComponents; }
    void removeAllComponents();

private:
    std::vector<Component::StrongPtr> mComponents;
};

} // namespace eng
