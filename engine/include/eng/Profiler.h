#pragma once
#include <eng/StringId.h>

#include <string>
#include <vector>

namespace eng {

// In-game hierarchical profiler (Game Engine Architecture, 4th ed., 10.8).
//
// External profilers (`make perf`) answer "which function is hot" better than
// this ever will. What they cannot answer is "which *phase of my frame* is hot,
// right now, on this machine, while I fly the debug camera into the corner that
// stutters". That is what an in-game profiler is for, and why the numbers it
// reports are annotated by hand rather than sampled.
//
// Timings are a tree, because code is a tree: `render` containing `shadows`
// containing `cull` is three numbers with a containment relation, not three
// unrelated bins. Each node carries
//
//   inclusive ms  everything inside the scope, children included
//   self ms       inclusive minus the children -- the work this scope does
//                 itself, which is the number that says whether to look here or
//                 one level down
//   calls         how many times the scope was entered this frame. The book's
//                 point: 2 ms in one call and 2 ms across a thousand are two
//                 completely different bugs.
//
// Usage mirrors the book's game loop:
//
//   prof.beginFrame();
//   { ENG_PROFILE(prof, "simulate");
//     { ENG_PROFILE(prof, "physics"); ... }
//     { ENG_PROFILE(prof, "ai");      ... } }
//   { ENG_PROFILE(prof, "render");    ... }
//   prof.endFrame();
//   for (int i : prof.preorder()) ...      // draw or log the tree
//
// Scope names must be string literals (or otherwise outlive the frame): the
// tree stores the pointer, and identity is the name's StringId, so entering the
// same scope twice accumulates into one node instead of allocating. Nothing in
// here allocates once a frame's shape has stabilised, which is what lets it stay
// enabled while you are chasing the spike.
//
// Single-threaded, and deliberately so: an open scope stack is per-thread state
// and a lock in a profiler measures the lock. One instance per thread if you
// ever need more.
class Profiler
{
public:
    Profiler();

    // --- flat API --------------------------------------------------------
    // Kept from the original profiler: a name plus a duration somebody else
    // measured. A sample lands in the tree as a leaf under whatever scope is
    // open, so the two APIs mix.
    struct Entry
    {
        std::string name;
        double ms;
    };

    // A sample also lands in the tree as a leaf under the open scope. Its
    // duration is the caller's measurement, not this profiler's, so the
    // containment the tree normally guarantees holds only as far as the caller
    // measured work that really did happen inside that scope. selfMs() clamps
    // at zero rather than reporting a negative remainder when it does not.
    void sample(const std::string& name, double ms);
    // Previous frame's samples, flat, in first-seen order.
    const std::vector<Entry>& lastFrame() const { return mLastFrame; }

    // --- frame -----------------------------------------------------------
    // Resets the tree and starts timing the root node. Every scope opened
    // before the next endFrame() belongs to this frame.
    void beginFrame();
    // Closes the root (and defensively any scope left open by an early return),
    // then publishes the frame. Readings below all describe the *previous*
    // frame, so a UI drawing mid-frame sees a complete tree rather than a
    // half-measured one.
    void endFrame();

    // --- scopes ----------------------------------------------------------
    void push(const char* name);
    void pop();

    // RAII form. Prefer the ENG_PROFILE macro, which names the local for you.
    class Timer
    {
    public:
        Timer(Profiler& p, const char* name) : mProf(p) { mProf.push(name); }
        ~Timer() { mProf.pop(); }
        Timer(const Timer&) = delete;
        Timer& operator=(const Timer&) = delete;

    private:
        Profiler& mProf;
    };

    // --- results ---------------------------------------------------------
    struct Node
    {
        StringId id;
        const char* name = "";
        int parent = -1;
        int firstChild = -1;
        int nextSibling = -1;
        double ms = 0.0; // inclusive
        int calls = 0;
        int depth = 0;
    };

    // Previous frame's tree. Index 0 is the root ("frame"), whose inclusive
    // time is the whole measured frame.
    const std::vector<Node>& nodes() const { return mLastNodes; }
    // Indices into nodes(), depth-first in call order: iterate this to draw or
    // print the tree top to bottom.
    const std::vector<int>& preorder() const { return mLastOrder; }
    // Inclusive minus children. Negative never happens; clamped anyway, because
    // a timer that measures itself out of order should not print a minus sign.
    double selfMs(int index) const;
    // Root inclusive time: the whole frame as this profiler measured it.
    double frameMs() const;

    // One indented line per node to the engine log. The no-UI equivalent of the
    // panel, and what RAVEN_PROFILE prints.
    void logTree() const;
    // Every N frames, so a caller does not keep a static counter of its own.
    void logTreeEvery(int everyN);
    // depth,name,calls,inclusive_ms,self_ms -- one row per node, for the
    // spreadsheet pass the book describes in 10.8.2.
    std::string toCsv() const;

    // --- compatibility ---------------------------------------------------
    // The original RAII helper: takes a std::string and feeds sample(). Kept so
    // existing call sites compile; new code should use ENG_PROFILE, which does
    // not allocate and does not lose the hierarchy.
    struct Scope
    {
        Profiler& prof;
        std::string name;
        long long startNs;
        Scope(Profiler& p, std::string n);
        ~Scope();
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;
    };

private:
    // Finds or creates the child of `parent` named `id`.
    int childNode(int parent, StringId id, const char* name);

    std::vector<Node> mNodes;      // tree under construction
    std::vector<int> mOpen;        // indices of currently open scopes
    std::vector<long long> mStart; // ns at push, parallel to mOpen
    std::vector<Entry> mCurrent;

    std::vector<Node> mLastNodes;
    std::vector<int> mLastOrder;
    std::vector<Entry> mLastFrame;
    int mLogTick = 0;
};

} // namespace eng

// Times the enclosing block into `prof` under `name` (a string literal).
#define ENG_PROFILE_CAT_(a, b) a##b
#define ENG_PROFILE_ID_(a, b) ENG_PROFILE_CAT_(a, b)
#define ENG_PROFILE(prof, name)                                                \
    ::eng::Profiler::Timer ENG_PROFILE_ID_(engProfileTimer_, __LINE__)((prof), \
                                                                      (name))
