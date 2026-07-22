#include <eng/Profiler.h>
#include <chrono>

namespace eng {

void Profiler::beginFrame() { mCurrent.clear(); }

void Profiler::sample(const std::string& name, double ms) {
    for (auto& e : mCurrent)
        if (e.name == name) { e.ms += ms; return; }
    mCurrent.push_back({name, ms});
}

void Profiler::endFrame() { mLastFrame = mCurrent; }

Profiler::Scope::Scope(Profiler& p, std::string n)
    : prof(p), name(std::move(n)),
      startNs(std::chrono::duration_cast<std::chrono::nanoseconds>(
                  std::chrono::steady_clock::now().time_since_epoch()).count()) {}

Profiler::Scope::~Scope() {
    long long endNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    prof.sample(name, (endNs - startNs) / 1.0e6);
}

} // namespace eng
