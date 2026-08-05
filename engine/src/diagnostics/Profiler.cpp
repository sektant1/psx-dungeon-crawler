#include <eng/Profiler.h>

#include <eng/Log.h>
#include <eng/MemoryProfiler.h>

#include <algorithm>
#include <chrono>
#include <cstdio>

namespace eng {
namespace {

long long nowNs()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

constexpr const char* kRootName = "frame";

} // namespace

Profiler::Profiler() { beginFrame(); }

int Profiler::childNode(int parent, StringId id, const char* name)
{
    // Linear walk of one node's children. A profiled frame has a handful of
    // scopes per level, so a map here would cost more than it saves and would
    // allocate on the path being measured.
    int prev = -1;
    for (int c = mNodes[std::size_t(parent)].firstChild; c != -1;
         c = mNodes[std::size_t(c)].nextSibling) {
        if (mNodes[std::size_t(c)].id == id)
            return c;
        prev = c;
    }

    const int index = int(mNodes.size());
    Node n;
    n.id = id;
    n.name = name;
    n.parent = parent;
    n.depth = mNodes[std::size_t(parent)].depth + 1;
    mNodes.push_back(n);
    if (prev == -1)
        mNodes[std::size_t(parent)].firstChild = index;
    else
        mNodes[std::size_t(prev)].nextSibling = index;
    return index;
}

void Profiler::beginFrame()
{
    mCurrent.clear();
    mNodes.clear();
    mOpen.clear();
    mStart.clear();

    Node root;
    root.id = intern(kRootName);
    root.name = kRootName;
    root.calls = 1;
    mNodes.push_back(root);
    mOpen.push_back(0);
    mStart.push_back(nowNs());
}

void Profiler::push(const char* name)
{
    // Timing a phase and attributing the heap to it are the same annotation,
    // so one call site drives both: every ENG_PROFILE scope in the engine is
    // already a memory tag, and "which phase of the frame allocated this"
    // needs no further markup anywhere. Compiled to nothing when the memory
    // profiler is off.
    memprof::pushTag(name);
    const int parent = mOpen.empty() ? 0 : mOpen.back();
    const int index = childNode(parent, StringId(hashString(name)), name);
    ++mNodes[std::size_t(index)].calls;
    mOpen.push_back(index);
    mStart.push_back(nowNs());
}

void Profiler::pop()
{
    // Before the guard below, so this stays one-for-one with push() even on the
    // unbalanced path -- a tag stack that drifts mis-attributes every later
    // allocation in the frame, not just the one scope.
    memprof::popTag();
    // The root is only closed by endFrame; an unbalanced pop() would otherwise
    // leave the frame with no open scope at all and send every later sample to
    // a node that is already finished.
    if (mOpen.size() <= 1)
        return;
    const int index = mOpen.back();
    const long long start = mStart.back();
    mOpen.pop_back();
    mStart.pop_back();
    mNodes[std::size_t(index)].ms += double(nowNs() - start) / 1.0e6;
}

void Profiler::sample(const std::string& name, double ms)
{
    bool merged = false;
    for (auto& e : mCurrent) {
        if (e.name == name) {
            e.ms += ms;
            merged = true;
            break;
        }
    }
    if (!merged)
        mCurrent.push_back({name, ms});

    // Also a leaf in the tree, under whatever scope is open, so code that hands
    // in a duration it measured itself still shows up in the hierarchy.
    if (!mNodes.empty()) {
        const int parent = mOpen.empty() ? 0 : mOpen.back();
        // intern(), not a raw hash: the name is a std::string whose storage the
        // caller may free, and the tree keeps only a pointer. The intern table
        // owns a copy that outlives the frame.
        const StringId id = intern(name);
        const int index = childNode(parent, id, id.c_str());
        mNodes[std::size_t(index)].ms += ms;
        ++mNodes[std::size_t(index)].calls;
    }
}

void Profiler::endFrame()
{
    // Close anything an early return left open, innermost first, so its time is
    // attributed rather than silently dropped.
    while (mOpen.size() > 1)
        pop();
    if (!mOpen.empty()) {
        mNodes[0].ms += double(nowNs() - mStart.back()) / 1.0e6;
        mOpen.clear();
        mStart.clear();
    }

    mLastFrame = mCurrent;
    mLastNodes = mNodes;

    // Pre-order flattening, done once here rather than by every consumer. Node
    // creation order is not pre-order: re-entering a scope appends its new
    // children after later siblings.
    mLastOrder.clear();
    mLastOrder.reserve(mLastNodes.size());
    std::vector<int> stack{0};
    while (!stack.empty()) {
        const int i = stack.back();
        stack.pop_back();
        mLastOrder.push_back(i);
        // Children pushed in reverse so they pop in call order.
        std::vector<int> kids;
        for (int c = mLastNodes[std::size_t(i)].firstChild; c != -1;
             c = mLastNodes[std::size_t(c)].nextSibling)
            kids.push_back(c);
        for (auto it = kids.rbegin(); it != kids.rend(); ++it)
            stack.push_back(*it);
    }
}

double Profiler::selfMs(int index) const
{
    if (index < 0 || std::size_t(index) >= mLastNodes.size())
        return 0.0;
    double children = 0.0;
    for (int c = mLastNodes[std::size_t(index)].firstChild; c != -1;
         c = mLastNodes[std::size_t(c)].nextSibling)
        children += mLastNodes[std::size_t(c)].ms;
    return std::max(mLastNodes[std::size_t(index)].ms - children, 0.0);
}

double Profiler::frameMs() const
{
    return mLastNodes.empty() ? 0.0 : mLastNodes[0].ms;
}

void Profiler::logTree() const
{
    const double total = frameMs();
    for (int i : mLastOrder) {
        const Node& n = mLastNodes[std::size_t(i)];
        const double pct = total > 0.0 ? n.ms / total * 100.0 : 0.0;
        log::info("Profile: %*s%-*s %7.3f ms  self %7.3f  %5.1f%%  x%d",
                  n.depth * 2, "", std::max(28 - n.depth * 2, 1), n.name, n.ms,
                  selfMs(i), pct, n.calls);
    }
}

void Profiler::logTreeEvery(int everyN)
{
    if (everyN <= 0)
        return;
    if (++mLogTick < everyN)
        return;
    mLogTick = 0;
    logTree();
}

std::string Profiler::toCsv() const
{
    std::string out = "depth,name,calls,inclusive_ms,self_ms\n";
    char line[256];
    for (int i : mLastOrder) {
        const Node& n = mLastNodes[std::size_t(i)];
        std::snprintf(line, sizeof(line), "%d,%s,%d,%.4f,%.4f\n", n.depth,
                      n.name, n.calls, n.ms, selfMs(i));
        out += line;
    }
    return out;
}

Profiler::Scope::Scope(Profiler& p, std::string n)
    : prof(p), name(std::move(n)), startNs(nowNs())
{
}

Profiler::Scope::~Scope() { prof.sample(name, double(nowNs() - startNs) / 1.0e6); }

} // namespace eng
