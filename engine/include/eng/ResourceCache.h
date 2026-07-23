#pragma once
#include <eng/Resource.h>
#include <memory>
#include <string>
#include <type_traits>
#include <unordered_map>

namespace eng {

// Owns a set of resources of one type, keyed by name. load() constructs +
// load()s on first request and returns the cached instance thereafter (a failed
// load() is not cached). Requires T(name, path) and T deriving from Resource.
template <typename T>
class ResourceCache {
    static_assert(std::is_base_of<Resource, T>::value, "T must derive from eng::Resource");
public:
    T* load(const std::string& name, const std::string& path) {
        auto it = mItems.find(name);
        if (it != mItems.end()) return it->second.get();
        auto res = std::make_unique<T>(name, path);
        if (!res->load()) return nullptr;
        T* raw = res.get();
        mItems.emplace(name, std::move(res));
        return raw;
    }

    T* get(const std::string& name) const {
        auto it = mItems.find(name);
        return it == mItems.end() ? nullptr : it->second.get();
    }

    bool has(const std::string& name) const { return mItems.count(name) > 0; }

    bool reload(const std::string& name) {
        auto it = mItems.find(name);
        return it == mItems.end() ? false : it->second->load();
    }

    bool remove(const std::string& name) { return mItems.erase(name) > 0; }
    void clear() { mItems.clear(); }
    std::size_t size() const { return mItems.size(); }

private:
    std::unordered_map<std::string, std::unique_ptr<T>> mItems;
};

} // namespace eng
