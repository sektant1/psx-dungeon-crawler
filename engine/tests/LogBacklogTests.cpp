// The log backlog: lines are retained whether or not anything is listening,
// and replayed to a sink when it attaches.
//
// This is the whole reason a console opened in onStart can show how the run
// started. Before it existed, every line logged during window creation, asset
// registration, shader compilation and the boot warmup was dropped on the
// floor -- so the one failure a console is opened to read ("material has no
// supportable Techniques") was never in it.
//
// Pure logic over a vector and a mutex: no window, no imgui, no renderer.
#include <eng/Log.h>

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "LogBacklogTests: " << message << '\n';
        std::exit(1);
    }
}

struct Capture {
    std::vector<std::string> lines;
    std::vector<eng::log::Level> levels;

    eng::log::Sink sink()
    {
        return [this](eng::log::Level level, const char* text) {
            levels.push_back(level);
            lines.emplace_back(text);
        };
    }
};

} // namespace

int main()
{
    // stderr is noise for a test; the lines under test are the ones a sink
    // sees, and stderr keeps its own copy either way.
    std::freopen("/dev/null", "w", stderr);

    // --- a sink attached late still sees what came before it ---------------
    eng::log::setBacklogCapacity(64);
    eng::log::info("boot: window created");
    eng::log::warn("boot: falling back to %s", "vertex lighting");
    eng::log::error("boot: material %d unsupported", 3);

    Capture late;
    const int token = eng::log::addSink(late.sink());
    require(late.lines.size() == 3, "backlog was not replayed to a new sink");
    require(late.lines[0] == "boot: window created", "replay lost line 0");
    require(late.lines[1] == "boot: falling back to vertex lighting",
            "replay lost the formatted argument");
    require(late.lines[2] == "boot: material 3 unsupported", "replay lost line 2");
    require(late.levels[1] == eng::log::Level::Warn, "replay lost the level");

    // --- and keeps receiving live lines, in order, with no duplicate -------
    eng::log::info("frame: live line");
    require(late.lines.size() == 4, "live line did not reach the sink");
    require(late.lines[3] == "frame: live line", "live line arrived out of order");
    eng::log::removeSink(token);

    // --- a second sink sees the same history, including the live line ------
    Capture second;
    const int secondToken = eng::log::addSink(second.sink());
    require(second.lines.size() == 4, "second sink saw a different history");
    require(second.lines[3] == "frame: live line",
            "a line logged while a sink was attached was not retained");
    eng::log::removeSink(secondToken);

    // --- the ring drops the oldest, never the newest -----------------------
    eng::log::setBacklogCapacity(3);
    Capture trimmed;
    const int trimmedToken = eng::log::addSink(trimmed.sink());
    require(trimmed.lines.size() == 3, "shrinking the capacity did not trim");
    require(trimmed.lines.back() == "frame: live line",
            "trimming dropped the newest line instead of the oldest");
    eng::log::removeSink(trimmedToken);

    for (int i = 0; i < 5; ++i)
        eng::log::info("overflow %d", i);
    Capture overflowed;
    const int overflowToken = eng::log::addSink(overflowed.sink());
    require(overflowed.lines.size() == 3, "the ring grew past its capacity");
    require(overflowed.lines[0] == "overflow 2", "the ring kept a stale line");
    require(overflowed.lines[2] == "overflow 4", "the ring lost the newest line");
    eng::log::removeSink(overflowToken);

    // --- capacity 0 restores the old free path ------------------------------
    eng::log::setBacklogCapacity(0);
    eng::log::info("dropped on the floor");
    Capture none;
    const int noneToken = eng::log::addSink(none.sink());
    require(none.lines.empty(), "capacity 0 still retained lines");
    // A sink attached with the backlog off must still receive live lines: the
    // early-out in fanout() covers both conditions, and getting it wrong makes
    // the console silent instead of merely historyless.
    eng::log::info("still live");
    require(none.lines.size() == 1 && none.lines[0] == "still live",
            "capacity 0 also silenced live output");
    eng::log::removeSink(noneToken);

    std::cout << "LogBacklogTests OK\n";
    return 0;
}
