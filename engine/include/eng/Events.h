#pragma once
#include <functional>
#include <string>
#include <typeindex>
#include <unordered_map>
#include <vector>

namespace eng {

// Base of all events. Concrete events derive and add payload fields.
class Event {
public:
    explicit Event(std::string name) : mName(std::move(name)) {}
    virtual ~Event() = default;
    const std::string& name() const { return mName; }
private:
    std::string mName;
};

// Type-indexed publish/subscribe bus. Member functions of the form
// void(EventType&) connect against an owning instance; dispatch routes by the
// concrete event type. disconnect() drops every handler owned by an instance.
class EventBus {
public:
    template <typename E, typename Obj>
    void connect(Obj* inst, void (Obj::*fn)(E&)) {
        mHandlers[std::type_index(typeid(E))].push_back(
            Handler{ static_cast<void*>(inst),
                     [inst, fn](Event& e) { (inst->*fn)(static_cast<E&>(e)); } });
    }

    template <typename E>
    void dispatch(E& event) {
        auto it = mHandlers.find(std::type_index(typeid(E)));
        if (it == mHandlers.end()) return;
        for (auto& h : it->second) h.call(event);
    }

    void disconnect(void* inst);

private:
    struct Handler {
        void* observer;
        std::function<void(Event&)> call;
    };
    std::unordered_map<std::type_index, std::vector<Handler>> mHandlers;
};

} // namespace eng
