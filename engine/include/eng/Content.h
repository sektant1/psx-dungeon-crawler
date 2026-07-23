#pragma once
#include <eng/ResourceCache.h>
#include <eng/System.h>
#include <memory>
#include <typeindex>
#include <unordered_map>

namespace eng {

// System owning one ResourceCache per resource type, created on demand. Route
// through cache<T>() or the load<T>()/get<T>() shortcuts. Bridges to concrete
// loaders via each Resource subclass's load().
class Content : public System {
public:
    Content() : System("Content") {}

    template <typename T>
    ResourceCache<T>& cache() {
        const std::type_index key(typeid(T));
        auto it = mCaches.find(key);
        if (it == mCaches.end()) {
            auto holder = std::make_unique<Holder<T>>();
            Holder<T>* raw = holder.get();
            mCaches.emplace(key, std::move(holder));
            return raw->cache;
        }
        return static_cast<Holder<T>*>(it->second.get())->cache;
    }

    template <typename T>
    T* load(const std::string& name, const std::string& path) { return cache<T>().load(name, path); }

    template <typename T>
    T* get(const std::string& name) { return cache<T>().get(name); }

    void update(float /*dt*/) override {}
    void clear() { mCaches.clear(); }

private:
    struct IHolder { virtual ~IHolder() = default; };
    template <typename T> struct Holder : IHolder { ResourceCache<T> cache; };
    std::unordered_map<std::type_index, std::unique_ptr<IHolder>> mCaches;
};

} // namespace eng
