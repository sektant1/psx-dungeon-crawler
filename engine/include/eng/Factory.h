#pragma once
#include <eng/Component.h>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace eng {

// Constructs components by their registered name. Component types self-register
// via ENG_REGISTER_COMPONENT. This is the seed of the object Factory; game
// object / space construction is layered on in later phases.
class Factory {
public:
    using Creator = std::function<Component::StrongPtr()>;

    void registerComponent(const std::string& name, Creator creator);
    bool has(const std::string& name) const;
    Component::StrongPtr create(const std::string& name) const;
    std::vector<std::string> registeredNames() const;

private:
    std::unordered_map<std::string, Creator> mCreators;
};

} // namespace eng

// Registers ComponentType under its literal type name.
#define ENG_REGISTER_COMPONENT(factory, ComponentType)                         \
    (factory).registerComponent(#ComponentType,                                \
        [] { return std::make_unique<ComponentType>(); })
