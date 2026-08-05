#pragma once
#include <cstdint>
#include <string>
#include <vector>

// Heap accounting for the engine's own allocations (Game Engine Architecture,
// 4th ed., 2.3-2.4).
//
// The book splits profilers into *statistical* (sample periodically, stay fast
// enough to keep playing) and *instrumenting* (hook everything, get exact
// numbers, watch the game slow to a crawl). A memory profiler for a game wants
// both halves, and they answer different questions, so this is deliberately two
// mechanisms in one file:
//
//   instrumenting  Every allocation goes through operator new, so every
//                  allocation is counted exactly: live bytes, live blocks,
//                  peak, and -- the number that actually matters in a game --
//                  how many bytes were allocated *this frame*. A steady-state
//                  frame in a shipped engine should allocate approximately
//                  nothing; watching that number is how you find out it does
//                  not. Cost is two atomic adds and a 16-byte header.
//
//   statistical    One allocation in N also captures a call stack, and the
//                  stacks are aggregated into a fixed table keyed by their
//                  hash. This is the "which 20% of the code" question from
//                  2.3, asked about bytes instead of milliseconds. Sampling is
//                  what keeps it affordable: backtrace() costs microseconds,
//                  which is ruinous per allocation and free at 1-in-128.
//
// The third aggregation is the one a general-purpose tool (heaptrack, Massif,
// `make valgrind`) cannot do, and it is the reason this exists in-engine at
// all: allocations are attributed to the **frame phase** that made them.
// eng::Profiler::push already brackets every phase of the frame by name, so
// entering a profiled scope also tags the heap, and "physics allocated 400 KB
// this frame" falls out with no extra annotation at any call site. A call stack
// tells you which function; a phase tells you which *part of the frame*, which
// is what you can actually schedule, budget, and pool away.
//
// Three properties, matching the telemetry pipe next door:
//
//   1. It cannot take the game down. Nothing throws (beyond the std::bad_alloc
//      operator new owes its caller), no lock is held on the allocation path,
//      and the tables are fixed-capacity -- a profiler that allocates to record
//      an allocation is a profiler that recurses.
//   2. It is compiled out of Release. ENG_MEMPROF gates the operator new
//      overrides entirely; with it off, everything below still compiles and
//      links, and reports zeros. Call sites never need an #ifdef.
//   3. It reports rather than draws. The numbers go out over the telemetry
//      channel "mem" and to the console, not onto the screen -- this renderer
//      draws the world into a third-of-the-window framebuffer, and pixels spent
//      on a debug overlay are pixels taken from the thing being debugged.
//
// Usage:
//
//   memprof::beginFrame();                    // once, top of the frame
//   { ENG_PROFILE(prof, "physics"); ... }     // allocations tagged "physics"
//   memprof::endFrame();
//   log::info("%s", memprof::report(10).c_str());
//
// RAVEN_MEMPROF=N sets the sampling rate (1-in-N; 0 disables call-stack capture
// and leaves the exact counters running).

namespace eng::memprof {

// True when the operator new overrides are compiled in. Everything below is
// callable either way; when this is false the numbers are zero.
bool enabled();

// --- totals ----------------------------------------------------------------
struct Stats {
    std::uint64_t liveBytes = 0;   // requested bytes currently outstanding
    std::uint64_t liveBlocks = 0;
    std::uint64_t peakBytes = 0;   // high-water mark since start (or reset)
    std::uint64_t peakBlocks = 0;
    std::uint64_t totalBytes = 0;  // ever allocated, the churn denominator
    std::uint64_t totalBlocks = 0;

    // The previous frame, which is the number to put on a graph. A frame that
    // allocates is a frame that can hitch; a frame that allocates the same
    // amount every time is a pooling opportunity with a name attached.
    std::uint64_t frameAllocBytes = 0;
    std::uint64_t frameAllocs = 0;
    std::uint64_t frameFreeBytes = 0;
    std::uint64_t frameFrees = 0;

    std::uint64_t overheadBytes = 0; // what the headers themselves cost
    // Headers whose cookie did not validate on free: something wrote off the
    // end of the block in front of this one (2.4). Non-zero is a real bug.
    std::uint64_t corruptions = 0;

    std::uint32_t sampleRate = 0;    // 1-in-N call-stack capture; 0 = off
    std::uint32_t stacksTracked = 0;
    bool stackTableFull = false;     // aggregation is lossy past this point
};
Stats stats();

// --- frame ----------------------------------------------------------------
// Zeroes the per-frame churn counters and publishes the previous frame's, so a
// reader mid-frame sees a complete number rather than a partial one.
void beginFrame();
void endFrame();

// Ring of per-frame allocated kilobytes, oldest-to-newest with a wrapping head
// -- the same shape as eng::FrameStatsView, so anything that can plot one can
// plot this. Valid for the life of the process.
constexpr int kHistory = 120;
const float* frameHistoryKb();
int frameHistoryHead();

// --- frame-phase tags ------------------------------------------------------
// Attribution is a thread-local stack of names; the innermost open tag owns
// whatever is allocated. eng::Profiler::push/pop drive this, so every
// ENG_PROFILE scope is already a tag and this is only needed for work that is
// not inside one. Names must be string literals (the table stores the pointer).
void pushTag(const char* name);
void popTag();

class TagScope {
public:
    explicit TagScope(const char* name) { pushTag(name); }
    ~TagScope() { popTag(); }
    TagScope(const TagScope&) = delete;
    TagScope& operator=(const TagScope&) = delete;
};

struct TagStat {
    const char* name = "";
    std::uint64_t liveBytes = 0;
    std::uint64_t liveBlocks = 0;
    std::uint64_t frameBytes = 0; // allocated under this tag last frame
    std::uint64_t frameBlocks = 0;
    std::uint64_t totalBytes = 0;
};
// Every registered tag, live bytes first. Allocates; call it from a console
// command, not from the frame.
std::vector<TagStat> tags();

// The same data without allocating, in registration order, for the per-frame
// publisher -- a reporter that allocates to report allocations adds itself to
// its own numbers. `tagAt` returns false past the end.
int tagCount();
bool tagAt(int index, TagStat& out);

// --- sampled call stacks ---------------------------------------------------
struct StackStat {
    // Already scaled: blocks caught by the 1-in-N sample stand for N each,
    // blocks caught for being large are counted whole. Estimates -- but
    // estimates of the heap, not of the sample.
    std::uint64_t liveBytes = 0;
    std::uint64_t liveBlocks = 0;
    std::uint64_t totalBytes = 0;
    std::uint64_t totalBlocks = 0;
    const void* const* frames = nullptr;
    int depth = 0;
    int id = -1; // slot index; what siteName() memoises against
};
// Aggregated stacks, live bytes first, capped at `limit` (0 = all).
std::vector<StackStat> stacks(int limit = 0);
// Frame addresses resolved to names where the platform can, one per line.
std::string symbolize(const StackStat& stack, int maxFrames = 8);

// The single most useful frame of a stack, demangled and trimmed: the first one
// that is not engine allocator plumbing or a std:: container growing itself.
// `std::vector<Foo>::push_back` is true and useless -- what you want to know is
// which of your functions was doing the pushing. This is what labels a treemap
// tile, so it has to fit in one.
//
// Memoised per stack, because a stack's frames never change once interned and
// resolving one is expensive: a backtrace_symbols plus a demangle per frame.
// Re-resolving sixteen of them once a second was a 100 ms hitch once a second
// -- the profiler causing precisely the stutter it exists to find.
//
// `budget`, when given, is a counter this DECREMENTS each time it resolves a
// name that was not already cached, and refuses to resolve once it hits zero
// (returning empty). Pass the same int across a loop and it caps how many new
// stacks that loop may resolve, so the first publish after a burst of them does
// not pay for all of them on one frame. A cached name costs nothing and never
// touches the budget.
std::string siteName(const StackStat& stack, int* budget = nullptr);

// --- control ---------------------------------------------------------------
// 1-in-N allocations capture a stack; 0 turns capture off and leaves the exact
// counters running. Also read from RAVEN_MEMPROF at startup.
void setSampleRate(std::uint32_t oneInN);

// Clears peaks, totals and the stack aggregation. Live counters are left alone
// -- those blocks still exist, and zeroing them would make every subsequent
// free underflow. Use it to isolate one level load or one action.
void reset();

// --- output ----------------------------------------------------------------
// Totals, then the top tags, then the top sampled stacks. What the console's
// `mem` command prints.
std::string report(int topStacks = 8);
// tag,live_bytes,live_blocks,frame_bytes,total_bytes -- one row per tag, for
// the spreadsheet pass.
std::string toCsv();

} // namespace eng::memprof

// Tags the enclosing block for heap attribution. Only needed outside an
// ENG_PROFILE scope, which already tags itself.
#define ENG_MEM_TAG_CAT_(a, b) a##b
#define ENG_MEM_TAG_ID_(a, b) ENG_MEM_TAG_CAT_(a, b)
#define ENG_MEM_TAG(name) \
    ::eng::memprof::TagScope ENG_MEM_TAG_ID_(engMemTag_, __LINE__)((name))
