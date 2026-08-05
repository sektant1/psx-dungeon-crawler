#include <eng/MemoryProfiler.h>

#include <eng/Log.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

#ifndef ENG_MEMPROF
#    define ENG_MEMPROF 0
#endif

#if ENG_MEMPROF && (defined(__linux__) || defined(__APPLE__))
#    include <cxxabi.h>
#    include <execinfo.h>
#    define ENG_MEMPROF_BACKTRACE 1
#else
#    define ENG_MEMPROF_BACKTRACE 0
#endif

namespace eng::memprof {
namespace {

// Fixed capacities, all of them. The one rule this file cannot break is that
// recording an allocation must not allocate, which rules out every growable
// container and makes the ceilings part of the design rather than a shortcut.
constexpr int kMaxTags = 64;
constexpr int kMaxStacks = 4096; // power of two: the probe masks with it
constexpr int kMaxTagDepth = 32;
constexpr int kMaxStackDepth = 16;
// Always worth a stack, whatever the rate: one 8 MB allocation is not something
// to learn about one time in 128.
constexpr std::uint64_t kAlwaysSampleBytes = 64u * 1024u;

using Counter = std::atomic<std::uint64_t>;

// Namespace scope rather than function-local statics: every member is constant
// -initialised, so these are live before the first dynamic initialiser runs.
// A function-local static would need a guard variable acquired on the
// allocation path, and would not exist yet for allocations made during static
// init -- which is exactly the window where a leak is hardest to find.
struct Totals {
    Counter liveBytes{0}, liveBlocks{0};
    Counter peakBytes{0}, peakBlocks{0};
    Counter totalBytes{0}, totalBlocks{0};
    Counter frameAllocBytes{0}, frameAllocs{0};
    Counter frameFreeBytes{0}, frameFrees{0};
    Counter pubAllocBytes{0}, pubAllocs{0};
    Counter pubFreeBytes{0}, pubFrees{0};
    Counter overhead{0};
    Counter corruptions{0};
};
Totals gTotals;

struct TagSlot {
    std::atomic<const char*> name{nullptr};
    Counter liveBytes{0}, liveBlocks{0};
    Counter frameBytes{0}, frameBlocks{0};
    Counter pubFrameBytes{0}, pubFrameBlocks{0};
    Counter totalBytes{0};
};
TagSlot gTagSlots[kMaxTags];
std::atomic<int> gTagCount{1}; // slot 0 is "untagged" and always exists
std::atomic_flag gTagLock = ATOMIC_FLAG_INIT;

struct StackSlot {
    std::atomic<std::uint64_t> hash{0}; // 0 = empty; claimed by CAS
    std::atomic<int> ready{0};          // frames are readable once this is 1
    void* frames[kMaxStackDepth]{};
    int depth = 0;
    // Two currencies, and conflating them is how a sampled profiler reports
    // more live bytes than the exact counter says exist. A block caught by the
    // 1-in-N sample stands for N blocks and must be scaled up; a block caught
    // because it was large was NOT sampled -- every one of them is seen -- and
    // scaling it multiplies a number that is already right.
    Counter liveSampled{0}, liveSampledBlocks{0};
    Counter liveExact{0}, liveExactBlocks{0};
    Counter totalSampled{0}, totalSampledBlocks{0};
    Counter totalExact{0}, totalExactBlocks{0};
};
StackSlot gStackSlots[kMaxStacks];
std::atomic<std::uint32_t> gStacksTracked{0};
std::atomic<bool> gStackTableFull{false};

// Resolved labels, one per slot, filled the first time a slot is reported.
//
// Kept beside the slots rather than in them because these are touched only by
// the reporter, never by the allocation path -- so a std::string here cannot
// slow down an allocation, and the unused ones cost nothing. A slot's frames
// never change once interned, so a name resolved once stays correct for the
// life of the process (which is why reset() leaves these alone: the stacks they
// describe are still there).
//
// Deliberately NOT atomic, unlike everything else in this file: siteName() is
// reporting, and reporting happens on one thread -- the frame loop's publisher
// or a console command, never a worker and never the allocation path. Guarding
// them would be a lock that only ever has one caller.
std::string gStackNames[kMaxStacks];
bool gStackNamed[kMaxStacks] = {false};

std::atomic<std::uint32_t> gSampleRate{128};

float gHistory[kHistory] = {0.0f};
std::atomic<int> gHistoryHead{0};

// The innermost open tag owns the allocation. Thread-local because an open
// scope is per-thread state: a worker thread that never pushes a tag reports
// "untagged", which is the honest answer rather than whatever the main thread
// happened to be doing.
thread_local std::uint16_t tTagStack[kMaxTagDepth];
thread_local int tTagDepth = 0;

std::uint16_t internTag(const char* name)
{
    if (!name)
        return 0;
    const int count = gTagCount.load(std::memory_order_acquire);
    // Pointer compare first: names are string literals, so the same scope
    // re-entered is the same pointer and this is one load and one branch.
    for (int i = 1; i < count; ++i) {
        const char* have = gTagSlots[i].name.load(std::memory_order_relaxed);
        if (have == name || (have && std::strcmp(have, name) == 0))
            return static_cast<std::uint16_t>(i);
    }
    while (gTagLock.test_and_set(std::memory_order_acquire))
        ;
    std::uint16_t id = 0;
    const int recount = gTagCount.load(std::memory_order_relaxed);
    for (int i = 1; i < recount && id == 0; ++i) {
        const char* have = gTagSlots[i].name.load(std::memory_order_relaxed);
        if (have == name || (have && std::strcmp(have, name) == 0))
            id = static_cast<std::uint16_t>(i);
    }
    if (id == 0 && recount < kMaxTags) {
        gTagSlots[recount].name.store(name, std::memory_order_relaxed);
        gTagCount.store(recount + 1, std::memory_order_release);
        id = static_cast<std::uint16_t>(recount);
    }
    gTagLock.clear(std::memory_order_release);
    return id; // 0 when the table is full: those bytes fall to "untagged"
}

#if ENG_MEMPROF_BACKTRACE
// Characters of leading return type in a demangled name, including the space.
// Zero when there is none (constructors, and anything the ABI left mangled).
std::size_t returnTypeLength(const std::string& name)
{
    int depth = 0;
    for (std::size_t i = 0; i < name.size(); ++i) {
        const char c = name[i];
        if (c == '<' || c == '(' || c == '[')
            ++depth;
        else if (c == '>' || c == ')' || c == ']')
            --depth;
        else if (c == ' ' && depth == 0)
            return i + 1;
        // Past the parameter list there is no return type left to find.
        if (depth > 0 && c == '(')
            break;
    }
    return 0;
}

std::string demangle(const std::string& mangled)
{
    int status = 0;
    // Allocates with malloc, not operator new, so demangling a name does not
    // add to the numbers being reported.
    char* out = abi::__cxa_demangle(mangled.c_str(), nullptr, nullptr, &status);
    std::string result = (status == 0 && out) ? out : mangled;
    std::free(out);
    return result;
}
#endif

#if ENG_MEMPROF

// backtrace(), captureStack(), allocate(), operator new. The first frames are
// this file and are never what you want to read.
constexpr int kSkipFrames = 4;

constexpr std::uint32_t kMagic = 0x5241564EU; // 'RAVN'

std::uint32_t cookieFor(std::uint64_t size)
{
    return kMagic ^ static_cast<std::uint32_t>(size) ^
           static_cast<std::uint32_t>(size >> 32);
}

// Sits immediately before the pointer handed to the caller. 16 bytes exactly,
// so a malloc block that was 16-aligned stays 16-aligned after it -- which is
// the default new alignment on every platform this engine builds for, and the
// reason this is not 24 or 32 bytes of more comfortable fields.
struct Header {
    std::uint64_t size;   // bytes the caller asked for
    std::uint32_t cookie; // validates the header, and catches an underrun
    std::uint16_t tag;    // frame-phase slot
    // Sampled stack id, 1-based; 0 = not sampled. kMaxStacks is 4096, so the
    // id needs 13 bits and the top one is free to record WHY this block was
    // sampled -- which is what keeps the two currencies apart on free.
    std::uint16_t stack;
};
constexpr std::uint16_t kStackExactFlag = 0x8000;
static_assert(sizeof(Header) == 16,
              "the header must preserve 16-byte alignment");

Header* headerOf(void* user)
{
    return reinterpret_cast<Header*>(static_cast<char*>(user) - sizeof(Header));
}

// backtrace() can allocate on its first call (it dlopens the unwinder). Without
// this, that allocation re-enters here and tries to take a backtrace.
thread_local bool tInside = false;
thread_local std::uint32_t tSampleTick = 0;

void bumpPeak(Counter& peak, std::uint64_t value)
{
    std::uint64_t seen = peak.load(std::memory_order_relaxed);
    while (value > seen &&
           !peak.compare_exchange_weak(seen, value, std::memory_order_relaxed))
        ;
}

std::uint16_t currentTag()
{
    return tTagDepth > 0 ? tTagStack[tTagDepth - 1] : std::uint16_t(0);
}

// Interns a call stack into the fixed table and returns its 1-based id, or 0
// when the table is full. Open addressing, claimed with one CAS; a loser of the
// race simply probes on, so two threads that capture the same stack converge on
// one slot without a lock.
std::uint16_t internStack(void* const* frames, int depth)
{
    std::uint64_t hash = 1469598103934665603ULL;
    for (int i = 0; i < depth; ++i) {
        auto bits = reinterpret_cast<std::uintptr_t>(frames[i]);
        for (int b = 0; b < 8; ++b) {
            hash ^= static_cast<std::uint64_t>((bits >> (b * 8)) & 0xFF);
            hash *= 1099511628211ULL;
        }
    }
    if (hash == 0)
        hash = 1; // 0 marks an empty slot

    std::size_t index = static_cast<std::size_t>(hash) & (kMaxStacks - 1);
    for (int probe = 0; probe < 64; ++probe) {
        StackSlot& slot = gStackSlots[index];
        std::uint64_t seen = slot.hash.load(std::memory_order_acquire);
        if (seen == 0) {
            std::uint64_t expected = 0;
            if (slot.hash.compare_exchange_strong(expected, hash,
                                                  std::memory_order_acq_rel)) {
                std::memcpy(slot.frames, frames,
                            sizeof(void*) * static_cast<std::size_t>(depth));
                slot.depth = depth;
                slot.ready.store(1, std::memory_order_release);
                gStacksTracked.fetch_add(1, std::memory_order_relaxed);
                return static_cast<std::uint16_t>(index + 1);
            }
            seen = expected;
        }
        if (seen == hash)
            return static_cast<std::uint16_t>(index + 1);
        index = (index + 1) & (kMaxStacks - 1);
    }
    gStackTableFull.store(true, std::memory_order_relaxed);
    return 0;
}

std::uint16_t captureStack()
{
#    if ENG_MEMPROF_BACKTRACE
    void* frames[kMaxStackDepth + kSkipFrames];
    const int got = ::backtrace(frames, kMaxStackDepth + kSkipFrames);
    if (got <= kSkipFrames)
        return 0;
    return internStack(frames + kSkipFrames, std::min(got - kSkipFrames,
                                                      kMaxStackDepth));
#    else
    return 0;
#    endif
}

void* allocate(std::size_t size, std::size_t front)
{
    void* base = nullptr;
    const std::size_t total = size + front;
    if (front <= sizeof(Header)) {
        base = std::malloc(total);
    } else {
#    if defined(_MSC_VER)
        base = _aligned_malloc(total, front);
#    else
        // aligned_alloc wants a size that is a multiple of the alignment.
        base = std::aligned_alloc(front, ((total + front - 1) / front) * front);
#    endif
    }
    if (!base)
        return nullptr;

    void* user = static_cast<char*>(base) + front;
    Header* h = headerOf(user);
    h->size = size;
    h->cookie = cookieFor(size);
    h->tag = currentTag();
    h->stack = 0;

    // Sampling, the statistical half. A big block is always worth a stack --
    // one 8 MB allocation is not something to learn about one time in 128 --
    // so the rate applies to the small ones that dominate by count.
    const std::uint32_t rate = gSampleRate.load(std::memory_order_relaxed);
    if (rate != 0 && !tInside) {
        const bool large = size >= kAlwaysSampleBytes;
        if (large || (++tSampleTick % rate) == 0) {
            tInside = true;
            h->stack = captureStack();
            tInside = false;
            if (h->stack != 0 && large)
                h->stack |= kStackExactFlag;
        }
    }

    gTotals.liveBytes.fetch_add(size, std::memory_order_relaxed);
    const std::uint64_t live =
        gTotals.liveBytes.load(std::memory_order_relaxed);
    const std::uint64_t blocks =
        gTotals.liveBlocks.fetch_add(1, std::memory_order_relaxed) + 1;
    bumpPeak(gTotals.peakBytes, live);
    bumpPeak(gTotals.peakBlocks, blocks);
    gTotals.totalBytes.fetch_add(size, std::memory_order_relaxed);
    gTotals.totalBlocks.fetch_add(1, std::memory_order_relaxed);
    gTotals.frameAllocBytes.fetch_add(size, std::memory_order_relaxed);
    gTotals.frameAllocs.fetch_add(1, std::memory_order_relaxed);
    gTotals.overhead.fetch_add(front, std::memory_order_relaxed);

    TagSlot& tag = gTagSlots[h->tag];
    tag.liveBytes.fetch_add(size, std::memory_order_relaxed);
    tag.liveBlocks.fetch_add(1, std::memory_order_relaxed);
    tag.frameBytes.fetch_add(size, std::memory_order_relaxed);
    tag.frameBlocks.fetch_add(1, std::memory_order_relaxed);
    tag.totalBytes.fetch_add(size, std::memory_order_relaxed);

    if (const std::uint16_t id = h->stack & ~kStackExactFlag; id != 0) {
        StackSlot& slot = gStackSlots[id - 1];
        const bool exact = (h->stack & kStackExactFlag) != 0;
        (exact ? slot.liveExact : slot.liveSampled)
            .fetch_add(size, std::memory_order_relaxed);
        (exact ? slot.liveExactBlocks : slot.liveSampledBlocks)
            .fetch_add(1, std::memory_order_relaxed);
        (exact ? slot.totalExact : slot.totalSampled)
            .fetch_add(size, std::memory_order_relaxed);
        (exact ? slot.totalExactBlocks : slot.totalSampledBlocks)
            .fetch_add(1, std::memory_order_relaxed);
    }
    return user;
}

void deallocate(void* user, std::size_t front)
{
    if (!user)
        return;
    Header* h = headerOf(user);
    void* base = static_cast<char*>(user) - front;

    if (h->cookie != cookieFor(h->size)) {
        // Something wrote past the end of the block in front of this one (2.4).
        // The block itself is still ours to release, so release it -- leaking
        // on top of a corruption helps nobody -- but say so, once, loudly.
        if (gTotals.corruptions.fetch_add(1, std::memory_order_relaxed) == 0)
            log::error("memprof: heap header at %p is corrupt (buffer underrun "
                       "from the preceding block); accounting is now "
                       "approximate",
                       user);
#    if defined(_MSC_VER)
        if (front > sizeof(Header))
            _aligned_free(base);
        else
            std::free(base);
#    else
        std::free(base);
#    endif
        return;
    }

    const std::uint64_t size = h->size;
    gTotals.liveBytes.fetch_sub(size, std::memory_order_relaxed);
    gTotals.liveBlocks.fetch_sub(1, std::memory_order_relaxed);
    gTotals.frameFreeBytes.fetch_add(size, std::memory_order_relaxed);
    gTotals.frameFrees.fetch_add(1, std::memory_order_relaxed);
    gTotals.overhead.fetch_sub(front, std::memory_order_relaxed);

    TagSlot& tag = gTagSlots[h->tag];
    tag.liveBytes.fetch_sub(size, std::memory_order_relaxed);
    tag.liveBlocks.fetch_sub(1, std::memory_order_relaxed);

    if (const std::uint16_t id = h->stack & ~kStackExactFlag; id != 0) {
        StackSlot& slot = gStackSlots[id - 1];
        const bool exact = (h->stack & kStackExactFlag) != 0;
        (exact ? slot.liveExact : slot.liveSampled)
            .fetch_sub(size, std::memory_order_relaxed);
        (exact ? slot.liveExactBlocks : slot.liveSampledBlocks)
            .fetch_sub(1, std::memory_order_relaxed);
    }

    h->cookie = 0; // a double free now reports as a corruption rather than
                   // silently decrementing the counters twice
#    if defined(_MSC_VER)
    if (front > sizeof(Header))
        _aligned_free(base);
    else
        std::free(base);
#    else
    std::free(base);
#    endif
}

// operator new owes its caller the new_handler protocol, not just a null check.
void* allocateOrThrow(std::size_t size, std::size_t front)
{
    for (;;) {
        if (void* p = allocate(size, front))
            return p;
        std::new_handler handler = std::get_new_handler();
        if (!handler)
            throw std::bad_alloc();
        handler();
    }
}

#endif // ENG_MEMPROF

} // namespace

// --- public ----------------------------------------------------------------

bool enabled()
{
    return ENG_MEMPROF != 0;
}

void pushTag(const char* name)
{
    if (tTagDepth < kMaxTagDepth)
        tTagStack[tTagDepth] = internTag(name);
    // Past the ceiling the depth still counts up so pop() stays balanced; the
    // tag simply does not change.
    ++tTagDepth;
}

void popTag()
{
    if (tTagDepth > 0)
        --tTagDepth;
}

void setSampleRate(std::uint32_t oneInN)
{
    gSampleRate.store(oneInN, std::memory_order_relaxed);
}

void beginFrame()
{
    gTotals.frameAllocBytes.store(0, std::memory_order_relaxed);
    gTotals.frameAllocs.store(0, std::memory_order_relaxed);
    gTotals.frameFreeBytes.store(0, std::memory_order_relaxed);
    gTotals.frameFrees.store(0, std::memory_order_relaxed);
    const int count = gTagCount.load(std::memory_order_acquire);
    for (int i = 0; i < count; ++i) {
        gTagSlots[i].frameBytes.store(0, std::memory_order_relaxed);
        gTagSlots[i].frameBlocks.store(0, std::memory_order_relaxed);
    }
}

void endFrame()
{
    const std::uint64_t bytes =
        gTotals.frameAllocBytes.load(std::memory_order_relaxed);
    gTotals.pubAllocBytes.store(bytes, std::memory_order_relaxed);
    gTotals.pubAllocs.store(gTotals.frameAllocs.load(std::memory_order_relaxed),
                            std::memory_order_relaxed);
    gTotals.pubFreeBytes.store(
        gTotals.frameFreeBytes.load(std::memory_order_relaxed),
        std::memory_order_relaxed);
    gTotals.pubFrees.store(gTotals.frameFrees.load(std::memory_order_relaxed),
                           std::memory_order_relaxed);

    const int count = gTagCount.load(std::memory_order_acquire);
    for (int i = 0; i < count; ++i) {
        gTagSlots[i].pubFrameBytes.store(
            gTagSlots[i].frameBytes.load(std::memory_order_relaxed),
            std::memory_order_relaxed);
        gTagSlots[i].pubFrameBlocks.store(
            gTagSlots[i].frameBlocks.load(std::memory_order_relaxed),
            std::memory_order_relaxed);
    }

    const int head = gHistoryHead.load(std::memory_order_relaxed);
    gHistory[head] = static_cast<float>(bytes) / 1024.0f;
    gHistoryHead.store((head + 1) % kHistory, std::memory_order_relaxed);
}

const float* frameHistoryKb()
{
    return gHistory;
}

int frameHistoryHead()
{
    return gHistoryHead.load(std::memory_order_relaxed);
}

Stats stats()
{
    Stats s;
    s.liveBytes = gTotals.liveBytes.load(std::memory_order_relaxed);
    s.liveBlocks = gTotals.liveBlocks.load(std::memory_order_relaxed);
    s.peakBytes = gTotals.peakBytes.load(std::memory_order_relaxed);
    s.peakBlocks = gTotals.peakBlocks.load(std::memory_order_relaxed);
    s.totalBytes = gTotals.totalBytes.load(std::memory_order_relaxed);
    s.totalBlocks = gTotals.totalBlocks.load(std::memory_order_relaxed);
    s.frameAllocBytes = gTotals.pubAllocBytes.load(std::memory_order_relaxed);
    s.frameAllocs = gTotals.pubAllocs.load(std::memory_order_relaxed);
    s.frameFreeBytes = gTotals.pubFreeBytes.load(std::memory_order_relaxed);
    s.frameFrees = gTotals.pubFrees.load(std::memory_order_relaxed);
    s.overheadBytes = gTotals.overhead.load(std::memory_order_relaxed);
    s.corruptions = gTotals.corruptions.load(std::memory_order_relaxed);
    s.sampleRate = gSampleRate.load(std::memory_order_relaxed);
    s.stacksTracked = gStacksTracked.load(std::memory_order_relaxed);
    s.stackTableFull = gStackTableFull.load(std::memory_order_relaxed);
    return s;
}

int tagCount()
{
    return gTagCount.load(std::memory_order_acquire);
}

bool tagAt(int index, TagStat& out)
{
    if (index < 0 || index >= tagCount())
        return false;
    const TagSlot& slot = gTagSlots[index];
    const char* name = slot.name.load(std::memory_order_relaxed);
    out.name = (index == 0 || !name) ? "untagged" : name;
    out.liveBytes = slot.liveBytes.load(std::memory_order_relaxed);
    out.liveBlocks = slot.liveBlocks.load(std::memory_order_relaxed);
    out.frameBytes = slot.pubFrameBytes.load(std::memory_order_relaxed);
    out.frameBlocks = slot.pubFrameBlocks.load(std::memory_order_relaxed);
    out.totalBytes = slot.totalBytes.load(std::memory_order_relaxed);
    return true;
}

std::vector<TagStat> tags()
{
    const int count = tagCount();
    std::vector<TagStat> out;
    out.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        TagStat t;
        if (tagAt(i, t) && (t.liveBytes != 0 || t.totalBytes != 0))
            out.push_back(t);
    }
    std::sort(out.begin(), out.end(), [](const TagStat& a, const TagStat& b) {
        return a.liveBytes > b.liveBytes;
    });
    return out;
}

std::vector<StackStat> stacks(int limit)
{
    std::vector<StackStat> out;
    for (int i = 0; i < kMaxStacks; ++i) {
        StackSlot& slot = gStackSlots[i];
        if (slot.ready.load(std::memory_order_acquire) == 0)
            continue;
        // Combined here, once, so every caller reports the same number: the
        // large blocks are counted as they are, the rate-sampled ones stand for
        // `rate` blocks each. Reporting the raw sampled count instead is what
        // made this claim more live bytes than the exact counter said existed.
        const std::uint64_t rate =
            std::max<std::uint64_t>(gSampleRate.load(std::memory_order_relaxed),
                                    1);
        StackStat s;
        s.liveBytes = slot.liveExact.load(std::memory_order_relaxed) +
                      slot.liveSampled.load(std::memory_order_relaxed) * rate;
        s.liveBlocks =
            slot.liveExactBlocks.load(std::memory_order_relaxed) +
            slot.liveSampledBlocks.load(std::memory_order_relaxed) * rate;
        s.totalBytes = slot.totalExact.load(std::memory_order_relaxed) +
                       slot.totalSampled.load(std::memory_order_relaxed) * rate;
        s.totalBlocks =
            slot.totalExactBlocks.load(std::memory_order_relaxed) +
            slot.totalSampledBlocks.load(std::memory_order_relaxed) * rate;
        s.frames = slot.frames;
        s.depth = slot.depth;
        s.id = i;
        out.push_back(s);
    }
    std::sort(out.begin(), out.end(),
              [](const StackStat& a, const StackStat& b) {
                  if (a.liveBytes != b.liveBytes)
                      return a.liveBytes > b.liveBytes;
                  return a.totalBytes > b.totalBytes;
              });
    if (limit > 0 && out.size() > static_cast<std::size_t>(limit))
        out.resize(static_cast<std::size_t>(limit));
    return out;
}

std::string symbolize(const StackStat& stack, int maxFrames)
{
    std::string out;
    const int count = std::min(stack.depth, maxFrames);
#if ENG_MEMPROF_BACKTRACE
    // backtrace_symbols goes through malloc, not operator new, so resolving a
    // stack does not perturb the numbers being reported.
    char** names = ::backtrace_symbols(
        const_cast<void* const*>(stack.frames), count);
    for (int i = 0; i < count; ++i) {
        out += "      ";
        out += names ? names[i] : "?";
        out += '\n';
    }
    std::free(names);
#else
    char buf[32];
    for (int i = 0; i < count; ++i) {
        std::snprintf(buf, sizeof(buf), "      %p\n", stack.frames[i]);
        out += buf;
    }
#endif
    return out;
}

namespace {
// The uncached resolve. Internal: everything outside goes through the memo.
std::string resolveSiteName(const StackStat& stack);
} // namespace

std::string siteName(const StackStat& stack, int* budget)
{
    // The memo, and the reason it exists: resolving is a backtrace_symbols plus
    // a demangle per frame, far too expensive to repeat every time a tile is
    // drawn. A cached hit never touches the budget.
    const bool cacheable = stack.id >= 0 && stack.id < kMaxStacks;
    if (cacheable && gStackNamed[stack.id])
        return gStackNames[stack.id];
    // Out of budget: the caller gets nothing this time and asks again later,
    // rather than resolving a burst of new stacks on one frame.
    if (budget && *budget <= 0)
        return {};
    if (budget)
        --*budget;

    std::string resolved = resolveSiteName(stack);
    if (cacheable) {
        gStackNames[stack.id] = resolved;
        gStackNamed[stack.id] = true;
    }
    return resolved;
}

namespace {
std::string resolveSiteName(const StackStat& stack)
{
#if ENG_MEMPROF_BACKTRACE
    char** names =
        ::backtrace_symbols(const_cast<void* const*>(stack.frames), stack.depth);
    if (!names)
        return "?";
    std::string best;
    for (int i = 0; i < stack.depth; ++i) {
        // backtrace_symbols gives "path/bin(_ZN3eng6Thing2doEv+0x1c) [0xaddr]".
        const char* line = names[i];
        const char* open = std::strchr(line, '(');
        const char* plus = open ? std::strchr(open, '+') : nullptr;
        if (!open || !plus || plus == open + 1)
            continue;
        std::string mangled(open + 1, static_cast<std::size_t>(plus - open - 1));
        std::string readable = demangle(mangled);
        // A demangled template function leads with its return type -- `void
        // std::__cxx11::basic_string<...>::_M_construct(...)` -- so matching on
        // the start of the string lets the whole standard library through under
        // a `void ` prefix. That is how "std::basic_string" ended up the single
        // largest allocation site in the first real capture: it is true, and it
        // is not an answer.
        //
        // The split has to be the first space at depth ZERO. Template arguments
        // are full of spaces (`std::_Vector_base<float, std::allocator<float> >`)
        // and cutting at one of those leaves a fragment like "float, " as the
        // name of an allocation site.
        readable.erase(0, returnTypeLength(readable));
        // Skip the plumbing. These frames are always true and never the answer:
        // the question is which of *our* functions asked for the memory.
        if (readable.rfind("std::", 0) == 0 ||
            readable.rfind("__gnu_cxx::", 0) == 0 ||
            readable.rfind("__cxx", 0) == 0 ||
            readable.rfind("operator new", 0) == 0 ||
            readable.rfind("void*", 0) == 0 ||
            readable.rfind("eng::memprof::", 0) == 0)
            continue;
        best = std::move(readable);
        break;
    }
    if (best.empty())
        best = names[0] ? names[0] : "?";
    std::free(names);
    // Template arguments make every name 400 characters and identical for the
    // first 300. The signature is not what identifies a tile; the name is.
    const std::size_t angle = best.find('<');
    if (angle != std::string::npos)
        best.resize(angle);
    const std::size_t paren = best.find('(');
    if (paren != std::string::npos)
        best.resize(paren);
    if (best.size() > 48)
        best.resize(48);
    return best.empty() ? std::string("?") : best;
#else
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%p",
                  stack.depth > 0 ? stack.frames[0] : nullptr);
    return buf;
#endif
}
} // namespace

void reset()
{
    gTotals.totalBytes.store(0, std::memory_order_relaxed);
    gTotals.totalBlocks.store(0, std::memory_order_relaxed);
    gTotals.peakBytes.store(gTotals.liveBytes.load(std::memory_order_relaxed),
                            std::memory_order_relaxed);
    gTotals.peakBlocks.store(gTotals.liveBlocks.load(std::memory_order_relaxed),
                             std::memory_order_relaxed);
    gTotals.corruptions.store(0, std::memory_order_relaxed);
    const int count = gTagCount.load(std::memory_order_acquire);
    for (int i = 0; i < count; ++i)
        gTagSlots[i].totalBytes.store(0, std::memory_order_relaxed);
    // Live counters -- global, per tag and per stack -- are deliberately left
    // alone. Those blocks still exist and will still be freed, and a counter
    // zeroed underneath them would underflow on the next free and report a
    // nonsense number for the rest of the session.
    for (int i = 0; i < kMaxStacks; ++i) {
        gStackSlots[i].totalSampled.store(0, std::memory_order_relaxed);
        gStackSlots[i].totalSampledBlocks.store(0, std::memory_order_relaxed);
        gStackSlots[i].totalExact.store(0, std::memory_order_relaxed);
        gStackSlots[i].totalExactBlocks.store(0, std::memory_order_relaxed);
    }
}

std::string report(int topStacks)
{
    char buf[512];
    std::string out;

    if (!enabled()) {
        return "memprof: compiled out (build with -DENG_MEMPROF=ON)\n";
    }

    const Stats s = stats();
    std::snprintf(buf, sizeof(buf),
                  "live %.2f MB in %llu blocks   peak %.2f MB   header "
                  "overhead %.2f MB\n",
                  double(s.liveBytes) / (1024.0 * 1024.0),
                  static_cast<unsigned long long>(s.liveBlocks),
                  double(s.peakBytes) / (1024.0 * 1024.0),
                  double(s.overheadBytes) / (1024.0 * 1024.0));
    out += buf;
    std::snprintf(buf, sizeof(buf),
                  "last frame: +%.1f KB in %llu allocs, -%.1f KB in %llu "
                  "frees   (churn since reset %.1f MB)\n",
                  double(s.frameAllocBytes) / 1024.0,
                  static_cast<unsigned long long>(s.frameAllocs),
                  double(s.frameFreeBytes) / 1024.0,
                  static_cast<unsigned long long>(s.frameFrees),
                  double(s.totalBytes) / (1024.0 * 1024.0));
    out += buf;
    if (s.corruptions != 0) {
        std::snprintf(buf, sizeof(buf),
                      "*** %llu corrupt headers seen -- something is writing "
                      "past the end of a heap block ***\n",
                      static_cast<unsigned long long>(s.corruptions));
        out += buf;
    }

    out += "\n  by frame phase          live        last frame       total\n";
    for (const TagStat& t : tags()) {
        std::snprintf(buf, sizeof(buf),
                      "  %-20s %8.2f MB   %8.1f KB   %8.2f MB\n", t.name,
                      double(t.liveBytes) / (1024.0 * 1024.0),
                      double(t.frameBytes) / 1024.0,
                      double(t.totalBytes) / (1024.0 * 1024.0));
        out += buf;
    }

    if (s.sampleRate == 0) {
        out += "\n  call stacks: capture off (RAVEN_MEMPROF=N, or mem.sample "
               "N)\n";
        return out;
    }

    std::snprintf(buf, sizeof(buf),
                  "\n  top allocation sites (1-in-%u sample of %u distinct "
                  "stacks%s)\n",
                  s.sampleRate, s.stacksTracked,
                  s.stackTableFull ? ", TABLE FULL" : "");
    out += buf;
    for (const StackStat& stack : stacks(topStacks)) {
        if (stack.liveBytes == 0 && stack.totalBytes == 0)
            continue;
        // Scaled: the sample saw one allocation in N, so the estimate of the
        // real figure is N times what it counted. Reported as an estimate
        // because that is what it is.
        std::snprintf(buf, sizeof(buf), "   ~%.2f MB live in ~%llu blocks\n",
                      double(stack.liveBytes) / (1024.0 * 1024.0),
                      static_cast<unsigned long long>(stack.liveBlocks));
        out += buf;
        out += symbolize(stack, 6);
    }
    return out;
}

std::string toCsv()
{
    std::string out = "tag,live_bytes,live_blocks,frame_bytes,frame_blocks,"
                      "total_bytes\n";
    char buf[256];
    for (const TagStat& t : tags()) {
        std::snprintf(buf, sizeof(buf), "%s,%llu,%llu,%llu,%llu,%llu\n", t.name,
                      static_cast<unsigned long long>(t.liveBytes),
                      static_cast<unsigned long long>(t.liveBlocks),
                      static_cast<unsigned long long>(t.frameBytes),
                      static_cast<unsigned long long>(t.frameBlocks),
                      static_cast<unsigned long long>(t.totalBytes));
        out += buf;
    }
    return out;
}

} // namespace eng::memprof

// --- the overrides ---------------------------------------------------------
//
// All eighteen of them, because the set has to be consistent: a block that
// picks up a header from one overload and is released through an overload that
// does not know about the header frees the wrong pointer. There is no partial
// version of this that is safe.

#if ENG_MEMPROF

namespace {
constexpr std::size_t kFront = sizeof(eng::memprof::Header);
} // namespace

void* operator new(std::size_t size)
{
    return eng::memprof::allocateOrThrow(size, kFront);
}
void* operator new[](std::size_t size)
{
    return eng::memprof::allocateOrThrow(size, kFront);
}
void* operator new(std::size_t size, const std::nothrow_t&) noexcept
{
    return eng::memprof::allocate(size, kFront);
}
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept
{
    return eng::memprof::allocate(size, kFront);
}
void* operator new(std::size_t size, std::align_val_t align)
{
    return eng::memprof::allocateOrThrow(
        size, std::max(kFront, static_cast<std::size_t>(align)));
}
void* operator new[](std::size_t size, std::align_val_t align)
{
    return eng::memprof::allocateOrThrow(
        size, std::max(kFront, static_cast<std::size_t>(align)));
}
void* operator new(std::size_t size, std::align_val_t align,
                   const std::nothrow_t&) noexcept
{
    return eng::memprof::allocate(
        size, std::max(kFront, static_cast<std::size_t>(align)));
}
void* operator new[](std::size_t size, std::align_val_t align,
                     const std::nothrow_t&) noexcept
{
    return eng::memprof::allocate(
        size, std::max(kFront, static_cast<std::size_t>(align)));
}

void operator delete(void* p) noexcept
{
    eng::memprof::deallocate(p, kFront);
}
void operator delete[](void* p) noexcept
{
    eng::memprof::deallocate(p, kFront);
}
void operator delete(void* p, std::size_t) noexcept
{
    eng::memprof::deallocate(p, kFront);
}
void operator delete[](void* p, std::size_t) noexcept
{
    eng::memprof::deallocate(p, kFront);
}
void operator delete(void* p, const std::nothrow_t&) noexcept
{
    eng::memprof::deallocate(p, kFront);
}
void operator delete[](void* p, const std::nothrow_t&) noexcept
{
    eng::memprof::deallocate(p, kFront);
}
void operator delete(void* p, std::align_val_t align) noexcept
{
    eng::memprof::deallocate(p,
                             std::max(kFront, static_cast<std::size_t>(align)));
}
void operator delete[](void* p, std::align_val_t align) noexcept
{
    eng::memprof::deallocate(p,
                             std::max(kFront, static_cast<std::size_t>(align)));
}
void operator delete(void* p, std::size_t, std::align_val_t align) noexcept
{
    eng::memprof::deallocate(p,
                             std::max(kFront, static_cast<std::size_t>(align)));
}
void operator delete[](void* p, std::size_t, std::align_val_t align) noexcept
{
    eng::memprof::deallocate(p,
                             std::max(kFront, static_cast<std::size_t>(align)));
}
void operator delete(void* p, std::align_val_t align,
                     const std::nothrow_t&) noexcept
{
    eng::memprof::deallocate(p,
                             std::max(kFront, static_cast<std::size_t>(align)));
}
void operator delete[](void* p, std::align_val_t align,
                       const std::nothrow_t&) noexcept
{
    eng::memprof::deallocate(p,
                             std::max(kFront, static_cast<std::size_t>(align)));
}

#endif // ENG_MEMPROF
