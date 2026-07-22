#include <eng/Trace.h>
#include <chrono>

namespace eng {
namespace {
int64_t nowUs() {
    using namespace std::chrono;
    return duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count();
}
} // namespace

void Trace::beginSession(const std::string& path) {
    mOut.open(path);
    mEvents.clear();
    mOpen.clear();
    mActive = mOut.is_open();
    mSessionStartUs = nowUs();
}

void Trace::begin(const std::string& name) {
    if (mActive) mOpen.push_back({name, nowUs()});
}

void Trace::end() {
    if (!mActive || mOpen.empty()) return;
    Open o = mOpen.back();
    mOpen.pop_back();
    mEvents.push_back({o.name, o.startUs - mSessionStartUs, nowUs() - o.startUs});
}

void Trace::endSession() {
    if (!mActive) return;
    mOut << "{\"traceEvents\":[";
    for (size_t i = 0; i < mEvents.size(); ++i) {
        const Event& e = mEvents[i];
        if (i) mOut << ',';
        mOut << "{\"name\":\"" << e.name << "\",\"ph\":\"X\",\"pid\":0,\"tid\":0"
             << ",\"ts\":" << e.startUs << ",\"dur\":" << e.durUs << "}";
    }
    mOut << "]}";
    mOut.close();
    mActive = false;
}

} // namespace eng
