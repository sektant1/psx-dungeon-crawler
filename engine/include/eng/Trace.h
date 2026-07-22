#pragma once
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace eng {

// Emits Chrome-trace ("chrome://tracing") JSON: one complete ("X") event per
// scope with a start timestamp and duration in microseconds. RAII Scope wraps a
// begin/end pair. Complements PSX_BENCH frame timing.
class Trace {
public:
    void beginSession(const std::string& path);
    void endSession();

    void begin(const std::string& name);   // pushes an open scope
    void end();                             // closes most-recent open scope

    struct Scope {
        Trace& trace;
        Scope(Trace& t, const std::string& name) : trace(t) { trace.begin(name); }
        ~Scope() { trace.end(); }
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;
    };

private:
    struct Open { std::string name; int64_t startUs; };
    struct Event { std::string name; int64_t startUs; int64_t durUs; };

    std::vector<Open> mOpen;
    std::vector<Event> mEvents;
    std::ofstream mOut;
    bool mActive = false;
    int64_t mSessionStartUs = 0;
};

} // namespace eng
