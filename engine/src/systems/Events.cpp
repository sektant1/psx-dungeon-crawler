#include <eng/Events.h>

namespace eng {

void EventBus::disconnect(void* inst) {
    for (auto& kv : mHandlers) {
        auto& list = kv.second;
        for (auto it = list.begin(); it != list.end();)
            it = (it->observer == inst) ? list.erase(it) : std::next(it);
    }
}

} // namespace eng
