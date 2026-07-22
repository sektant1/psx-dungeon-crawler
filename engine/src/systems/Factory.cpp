#include <eng/Factory.h>

namespace eng {

void Factory::registerComponent(const std::string& name, Creator creator) {
    mCreators[name] = std::move(creator);
}

bool Factory::has(const std::string& name) const {
    return mCreators.find(name) != mCreators.end();
}

Component::StrongPtr Factory::create(const std::string& name) const {
    auto it = mCreators.find(name);
    return it == mCreators.end() ? nullptr : it->second();
}

std::vector<std::string> Factory::registeredNames() const {
    std::vector<std::string> names;
    names.reserve(mCreators.size());
    for (const auto& kv : mCreators) names.push_back(kv.first);
    return names;
}

} // namespace eng
