// The heap counters, minus a game.
//
// What is worth pinning here is not "does it count" but the properties that
// make it safe to leave switched on: a block round-trips through every operator
// new/delete pair without losing bytes, an over-aligned allocation still comes
// back aligned once a 16-byte header has been slipped in front of it, and the
// attribution follows the tag stack rather than the call order.
//
// The test allocates constantly on its own account (std::string, std::vector),
// so every assertion is on a *delta* measured around one specific allocation.
// Absolute numbers here would be a test of the test harness.

#include <eng/MemoryProfiler.h>

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <new>
#include <string>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "MemoryProfilerTests: " << message << '\n';
        std::exit(1);
    }
}

std::uint64_t live()
{
    return eng::memprof::stats().liveBytes;
}

// C++ lets an implementation elide a new/delete pair whose object is never
// observed (N3664), and at -O2 GCC does exactly that -- which turns a test of
// the allocator into a test of nothing, silently. Pushing the pointer through a
// volatile sink makes the allocation observable and stops the optimiser.
void* volatile gSink = nullptr;
void escape(void* p)
{
    gSink = p;
}

// --- counting --------------------------------------------------------------
void anAllocationIsCountedAndGivenBack()
{
    const std::uint64_t before = live();
    auto* block = new char[4096];
    escape(block);
    const std::uint64_t during = live();
    require(during >= before + 4096,
            "live bytes must rise by at least the requested size");

    // Touch it, so a compiler that can see the whole function cannot elide the
    // allocation and quietly turn this into a test of nothing.
    for (int i = 0; i < 4096; ++i)
        block[i] = static_cast<char>(i);
    require(block[4095] == static_cast<char>(4095), "the block must be usable");

    delete[] block;
    require(live() == before, "freeing must return the counter exactly");
}

void everyOverloadRoundTrips()
{
    const std::uint64_t before = live();

    void* plain = ::operator new(300);
    escape(plain);
    ::operator delete(plain);

    void* sized = ::operator new(300);
    escape(sized);
    ::operator delete(sized, 300);

    void* array = ::operator new[](300);
    escape(array);
    ::operator delete[](array);

    void* nothrown = ::operator new(300, std::nothrow);
    escape(nothrown);
    require(nothrown != nullptr, "nothrow new must succeed for 300 bytes");
    ::operator delete(nothrown, std::nothrow);

    require(live() == before,
            "every new/delete pairing must leave the counter where it started");
}

void overAlignedBlocksStayAligned()
{
    const std::uint64_t before = live();
    constexpr std::size_t kAlign = 64; // past the 16-byte header

    struct alignas(kAlign) Wide {
        char bytes[kAlign * 2];
    };
    auto* wide = new Wide;
    escape(wide);
    const auto address = reinterpret_cast<std::uintptr_t>(wide);
    require(address % kAlign == 0,
            "an over-aligned allocation must still honour its alignment with a "
            "header in front of it");
    require(live() >= before + sizeof(Wide),
            "an over-aligned allocation must be counted like any other");
    delete wide;
    require(live() == before, "an over-aligned free must return the counter");
}

void peakRemembersTheHighWaterMark()
{
    auto* big = new char[512 * 1024];
    escape(big);
    big[0] = 1;
    const std::uint64_t peak = eng::memprof::stats().peakBytes;
    const std::uint64_t atPeak = live();
    delete[] big;
    require(peak >= atPeak,
            "the peak must be at least the live figure at the moment it was "
            "taken");
    require(eng::memprof::stats().peakBytes == peak,
            "freeing must not lower the peak -- that is the whole point of it");
}

// --- attribution -----------------------------------------------------------
std::uint64_t liveUnder(const char* name)
{
    for (const eng::memprof::TagStat& t : eng::memprof::tags())
        if (std::string(t.name) == name)
            return t.liveBytes;
    return 0;
}

void allocationsAreChargedToTheOpenTag()
{
    const std::uint64_t before = liveUnder("test-phase");
    char* block = nullptr;
    {
        ENG_MEM_TAG("test-phase");
        block = new char[8192];
        escape(block);
        block[0] = 1;
    }
    require(liveUnder("test-phase") >= before + 8192,
            "the innermost open tag must own the allocation");

    // Freeing outside the scope must still credit the tag the block was
    // allocated under, not whatever happens to be open now: the tag travels in
    // the block's header, not in the caller.
    delete[] block;
    require(liveUnder("test-phase") == before,
            "a free must return the bytes to the tag that allocated them");
}

void tagsNestAndUnwind()
{
    const std::uint64_t outerBefore = liveUnder("outer");
    const std::uint64_t innerBefore = liveUnder("inner");
    char* outerBlock = nullptr;
    char* innerBlock = nullptr;
    {
        ENG_MEM_TAG("outer");
        {
            ENG_MEM_TAG("inner");
            innerBlock = new char[4096];
            escape(innerBlock);
            innerBlock[0] = 1;
        }
        // Back to "outer" now that the inner scope has closed.
        outerBlock = new char[4096];
        escape(outerBlock);
        outerBlock[0] = 1;
    }
    require(liveUnder("inner") >= innerBefore + 4096,
            "the inner scope must own what it allocated");
    require(liveUnder("outer") >= outerBefore + 4096,
            "closing the inner scope must restore the outer tag");
    delete[] innerBlock;
    delete[] outerBlock;
    require(liveUnder("inner") == innerBefore && liveUnder("outer") == outerBefore,
            "both tags must unwind to where they started");
}

// --- frames ----------------------------------------------------------------
void frameChurnCoversExactlyOneFrame()
{
    eng::memprof::beginFrame();
    auto* block = new char[64 * 1024];
    escape(block);
    block[0] = 1;
    eng::memprof::endFrame();
    const eng::memprof::Stats busy = eng::memprof::stats();
    require(busy.frameAllocBytes >= 64 * 1024,
            "the frame's churn must include what the frame allocated");

    eng::memprof::beginFrame();
    eng::memprof::endFrame();
    require(eng::memprof::stats().frameAllocBytes < busy.frameAllocBytes,
            "a quiet frame must not inherit the previous frame's churn");

    delete[] block;
}

void resetKeepsLiveBytesIntact()
{
    auto* block = new char[32 * 1024];
    escape(block);
    block[0] = 1;
    const std::uint64_t before = live();
    const std::uint64_t churnBefore = eng::memprof::stats().totalBytes;
    require(churnBefore > 0, "the run so far must have allocated something");

    eng::memprof::reset();
    require(live() == before,
            "reset must not touch live bytes: those blocks still exist, and a "
            "counter zeroed underneath them underflows on the next free");
    require(eng::memprof::stats().totalBytes == 0,
            "reset must zero the churn total");

    delete[] block;
    require(live() == before - 32 * 1024,
            "a free after a reset must still subtract exactly, which it only "
            "does because reset left the live counters alone");
}

// --- reporting -------------------------------------------------------------
void theReportMentionsTheTagsItWasGiven()
{
    char* block = nullptr;
    {
        ENG_MEM_TAG("reported-phase");
        block = new char[128 * 1024];
        escape(block);
        block[0] = 1;
    }
    eng::memprof::beginFrame();
    eng::memprof::endFrame();
    const std::string text = eng::memprof::report(4);
    require(text.find("reported-phase") != std::string::npos,
            "a tag holding 128 KB must appear in the report");
    require(!eng::memprof::toCsv().empty(), "the csv must have rows");
    delete[] block;
}

void nothingCorruptedItself()
{
    require(eng::memprof::stats().corruptions == 0,
            "no header may have failed its cookie check during this run");
}

} // namespace

int main()
{
    if (!eng::memprof::enabled()) {
        // Not a failure: a build with ENG_MEMPROF=OFF is a supported build, and
        // the assertions below all measure the overrides that are not there.
        std::cout << "MemoryProfilerTests: memprof compiled out, skipping\n";
        return 0;
    }

    anAllocationIsCountedAndGivenBack();
    everyOverloadRoundTrips();
    overAlignedBlocksStayAligned();
    peakRemembersTheHighWaterMark();
    allocationsAreChargedToTheOpenTag();
    tagsNestAndUnwind();
    frameChurnCoversExactlyOneFrame();
    resetKeepsLiveBytesIntact();
    theReportMentionsTheTagsItWasGiven();
    nothingCorruptedItself();

    std::cout << "MemoryProfilerTests: ok\n";
    return 0;
}
