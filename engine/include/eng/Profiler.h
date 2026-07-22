#pragma once
#include <string>
#include <vector>

namespace eng {

// Accumulates per-name timing samples within a frame and exposes the completed
// previous frame's totals for display in DebugUi. A Scope RAII helper times a
// block and feeds sample() automatically.
class Profiler {
public:
    struct Entry { std::string name; double ms; };

    void beginFrame();
    void sample(const std::string& name, double ms);  // accumulates by name
    void endFrame();

    const std::vector<Entry>& lastFrame() const { return mLastFrame; }

    struct Scope {
        Profiler& prof;
        std::string name;
        long long startNs;
        Scope(Profiler& p, std::string n);
        ~Scope();
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;
    };

private:
    std::vector<Entry> mCurrent;
    std::vector<Entry> mLastFrame;
};

} // namespace eng
