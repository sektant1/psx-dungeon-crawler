// The telemetry pipe, minus the socket.
//
// Two things are worth pinning here and they are both properties rather than
// behaviours: the wire format is binary-safe (a log line is arbitrary text and
// must not be able to desynchronise the stream), and the pipe drops rather than
// blocks (a debug channel that applies back-pressure to the simulation changes
// the bug it was opened to find).

#include <eng/telemetry/RedisSink.h>
#include <eng/telemetry/Resp.h>
#include <eng/telemetry/Telemetry.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <memory>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "TelemetryTests: " << message << '\n';
        std::exit(1);
    }
}

// --- RESP ------------------------------------------------------------------
void commandsAreLengthPrefixed()
{
    const std::string encoded =
        eng::telemetry::encodeCommand({"PUBLISH", "raven:ch:ai", "hi"});
    require(encoded ==
                "*3\r\n$7\r\nPUBLISH\r\n$11\r\nraven:ch:ai\r\n$2\r\nhi\r\n",
            "the command encoding must match RESP exactly");
}

void payloadsAreBinarySafe()
{
    // The reason RESP is length-prefixed, and the reason this is a test: a log
    // line legitimately contains newlines, quotes and CRLFs, and none of them
    // may be able to end an argument early.
    const std::string nasty = "line\r\nnext\r\n$5\r\nEVIL\r\n\"quoted\"";
    const std::string encoded =
        eng::telemetry::encodeCommand({"PUBLISH", "ch", nasty});
    const std::string header = "$" + std::to_string(nasty.size()) + "\r\n";
    require(encoded.find(header) != std::string::npos,
            "the payload's own length must prefix it verbatim");
    require(encoded.find(nasty) != std::string::npos,
            "and the payload must be carried unescaped");
    require(encoded.size() ==
                std::string("*3\r\n$7\r\nPUBLISH\r\n$2\r\nch\r\n").size() +
                    header.size() + nasty.size() + 2,
            "nothing may be added to or removed from the payload");
}

void repliesAreClassifiedAndConsumed()
{
    std::size_t consumed = 0;
    std::string error;

    require(eng::telemetry::classifyReply("+OK\r\n", consumed) ==
                eng::telemetry::ReplyKind::Ok,
            "a simple string is a success");
    require(consumed == 5, "and spans exactly its own bytes");

    require(eng::telemetry::classifyReply(":42\r\n", consumed) ==
                eng::telemetry::ReplyKind::Ok,
            "an integer reply is a success");

    require(eng::telemetry::classifyReply("-ERR nope\r\n", consumed, &error) ==
                eng::telemetry::ReplyKind::Error,
            "an error reply is an error");
    require(error == "ERR nope", "and carries its text");

    // Bulk strings are why counting replies needs a parser at all: the payload
    // can contain anything, including what looks like another reply.
    // Payload is the 6 bytes `+OK\r\nx` -- deliberately containing something
    // that looks like a complete reply, which is the case a length-blind
    // scanner gets wrong.
    require(eng::telemetry::classifyReply("$6\r\n+OK\r\nx\r\n", consumed) ==
                eng::telemetry::ReplyKind::Ok,
            "a bulk string is a success");
    require(consumed == 12,
            "and consumes its whole payload, not the reply that looks like it");

    // A partial reply must not be mistaken for a complete one; otherwise the
    // sink desynchronises the moment a batch spans two TCP segments.
    require(eng::telemetry::classifyReply("+O", consumed) ==
                eng::telemetry::ReplyKind::Incomplete,
            "a half-arrived reply is incomplete");
    require(eng::telemetry::classifyReply("$7\r\n+OK\r\n", consumed) ==
                eng::telemetry::ReplyKind::Incomplete,
            "a bulk string missing its tail is incomplete");
}

void jsonEscapesWhatItMust()
{
    std::string out;
    eng::telemetry::appendJsonString(out, "a\"b\\c\nd\te");
    require(out == "\"a\\\"b\\\\c\\nd\\te\"", "the six JSON escapes");
    out.clear();
    eng::telemetry::appendJsonString(out, std::string("x\x01y"));
    require(out == "\"x\\u0001y\"", "and control bytes as \\u00XX");
}

void recordJsonCarriesTheBrowsersFields()
{
    // The browser reads ch/lvl/frame/t/msg (and v on a sample). Renaming one
    // silently empties a column, which is exactly the kind of break a test
    // should catch rather than a person.
    eng::telemetry::Record record;
    record.channel = "ai";
    record.message = "path failed";
    record.level = eng::telemetry::Level::Warn;
    record.frame = 12;
    record.timeMs = 1.5;
    const std::string json = eng::telemetry::recordJson(record);
    for (const char* field : {"\"ch\":", "\"lvl\":", "\"frame\":", "\"t\":",
                              "\"msg\":"})
        require(json.find(field) != std::string::npos,
                "a field the browser reads is missing from the record JSON");
    require(json.find("\"v\":") == std::string::npos,
            "a text record must not claim a numeric value");

    record.hasValue = true;
    record.value = 3.5;
    require(eng::telemetry::recordJson(record).find("\"v\":") !=
                std::string::npos,
            "a sample must carry its value");
}

// --- the pipe --------------------------------------------------------------
class CountingSink final : public eng::telemetry::Sink {
public:
    std::atomic<int> batches{0};
    std::atomic<int> records{0};
    std::atomic<bool> fail{false};

    bool publish(const std::vector<eng::telemetry::Record>& batch) override
    {
        batches.fetch_add(1);
        records.fetch_add(int(batch.size()));
        return !fail.load();
    }
    std::string describe() const override { return "counting"; }
};

// Waits for a predicate rather than sleeping a fixed time: the publisher runs
// on its own thread and a fixed sleep is how a test becomes flaky on a loaded
// machine.
template <typename Predicate>
bool waitFor(Predicate ready, int timeoutMs = 3000)
{
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        if (ready())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return ready();
}

void recordsReachTheSink()
{
    auto sink = std::make_unique<CountingSink>();
    CountingSink* observer = sink.get();
    eng::telemetry::Config config;
    config.flushIntervalMs = 2;
    require(eng::telemetry::start(std::move(sink), config), "the pipe starts");

    for (int i = 0; i < 50; ++i)
        eng::telemetry::write("test", eng::telemetry::Level::Info, "hello");
    require(waitFor([&] { return observer->records.load() >= 50; }),
            "every written record should reach the sink");
    // Batched, not one publish per record: the transport cost is per syscall.
    require(observer->batches.load() < 50,
            "records should be batched rather than published one at a time");
    eng::telemetry::stop();
}

void aDisabledChannelCostsNothing()
{
    auto sink = std::make_unique<CountingSink>();
    CountingSink* observer = sink.get();
    eng::telemetry::Config config;
    config.flushIntervalMs = 2;
    eng::telemetry::start(std::move(sink), config);

    eng::telemetry::setChannelEnabled("noisy", false);
    require(!eng::telemetry::enabled("noisy"),
            "a channel switched off reports itself off");
    for (int i = 0; i < 20; ++i)
        eng::telemetry::write("noisy", eng::telemetry::Level::Info, "x");
    eng::telemetry::write("quiet", eng::telemetry::Level::Info, "x");
    require(waitFor([&] { return observer->records.load() >= 1; }),
            "the enabled channel still publishes");
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    require(observer->records.load() == 1,
            "a disabled channel must not publish anything at all");
    eng::telemetry::stop();
}

void aFullRingDropsRatherThanBlocks()
{
    // The property that keeps this safe to leave on: the game thread must never
    // wait for the collector. A sink that never succeeds and a tiny ring is the
    // worst case, and it has to stay bounded and keep running.
    auto sink = std::make_unique<CountingSink>();
    CountingSink* observer = sink.get();
    observer->fail.store(true);
    eng::telemetry::Config config;
    config.ringCapacity = 16;
    config.flushIntervalMs = 1000; // effectively never drains during the test
    eng::telemetry::start(std::move(sink), config);

    // The counters are process-wide and earlier tests have already written, so
    // this measures the delta rather than an absolute.
    const eng::telemetry::Stats before = eng::telemetry::stats();
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 20000; ++i)
        eng::telemetry::write("flood", eng::telemetry::Level::Info, "x");
    const auto elapsed = std::chrono::steady_clock::now() - start;

    require(elapsed < std::chrono::seconds(2),
            "writing into a full ring must not block the calling thread");
    const eng::telemetry::Stats after = eng::telemetry::stats();
    require(after.droppedRingFull > before.droppedRingFull,
            "a full ring should report what it dropped rather than hide it");
    require(after.written - before.written == 20000,
            "and still count everything written");
    eng::telemetry::stop();
}

void writesAreSafeWhenNothingIsRunning()
{
    // Every ENG_TELEMETRY in the engine is compiled in unconditionally, so the
    // no-collector case is the common one and must be a branch, not a crash.
    eng::telemetry::stop();
    require(!eng::telemetry::running(), "nothing is running");
    require(!eng::telemetry::enabled("anything"),
            "no channel is enabled when the pipe is down");
    eng::telemetry::write("anything", eng::telemetry::Level::Error, "safe");
    eng::telemetry::sample("anything", "x", 1.0);
    ENG_TELEMETRY("anything", eng::telemetry::Level::Info, "%d", 42);
}

void logLinesRouteToTheirSystem()
{
    // The fix for "Connector only ever shows one channel": nearly every line
    // the engine writes already names its system, and that prefix is the
    // channel. Pinned against prefixes the engine actually emits, taken from a
    // real run.
    struct Case { const char* line; const char* channel; };
    const Case cases[] = {
        {"RHI renderer: 228 materials", "render"},
        {"rhi(vulkan): initialized Intel(R) UHD Graphics", "render"},
        {"Warmup: shaders were validated", "render"},
        {"EnemySpawner: 6 spawn points", "enemy"},
        {"EnemyLibrary: 10 enemies from 20 rows", "enemy"},
        {"AudioCatalog: 39 cues", "audio"},
        {"ParticleLibrary: loaded 36 effects", "particles"},
        {"DungeonMap: 21 rows, 1 rooms", "world"},
        {"CombatVocabulary: 10 damage types", "combat"},
        {"ImGuiLayout: panel layout restored", "ui"},
        {"assets: root /x (dev), 1 packs", "assets"},
        // Not aliased, and deliberately so: an unrecognised system gets its
        // own channel rather than a catch-all, so one added tomorrow shows up
        // by itself with no table edit.
        {"Load: Preparing systems", "load"},
        {"Physics: 3 bodies", "physics"},
    };
    for (const Case& c : cases) {
        const std::string got = eng::telemetry::channelForLogLine(c.line);
        require(got == c.channel,
                ("a log line routed to the wrong channel: " +
                 std::string(c.line) + " -> '" + got + "'")
                    .c_str());
    }

    // Prose that merely contains a colon is not a channel name.
    require(eng::telemetry::channelForLogLine("could not open x: no such file")
                .empty(),
            "a sentence with a colon must not become a channel");
    require(eng::telemetry::channelForLogLine("no colon here at all").empty(),
            "a line naming no system has no channel of its own");
    require(eng::telemetry::channelForLogLine(
                "a very long prefix indeed that goes on: x").empty(),
            "an implausibly long prefix is prose, not a system name");
}

void channelsAreDiscovered()
{
    auto sink = std::make_unique<CountingSink>();
    eng::telemetry::Config config;
    config.flushIntervalMs = 2;
    eng::telemetry::start(std::move(sink), config);
    eng::telemetry::write("render", eng::telemetry::Level::Info, "x");
    eng::telemetry::write("anim", eng::telemetry::Level::Info, "x");
    const std::vector<std::string> names = eng::telemetry::channels();
    require(names.size() >= 2, "writing to a channel registers it");
    require(std::find(names.begin(), names.end(), "render") != names.end() &&
                std::find(names.begin(), names.end(), "anim") != names.end(),
            "both channels should be listed, sorted, without being declared");
    eng::telemetry::stop();
}

} // namespace

int main()
{
    commandsAreLengthPrefixed();
    payloadsAreBinarySafe();
    repliesAreClassifiedAndConsumed();
    jsonEscapesWhatItMust();
    recordJsonCarriesTheBrowsersFields();

    recordsReachTheSink();
    aDisabledChannelCostsNothing();
    aFullRingDropsRatherThanBlocks();
    writesAreSafeWhenNothingIsRunning();
    logLinesRouteToTheirSystem();
    channelsAreDiscovered();

    std::cout << "TelemetryTests: ok\n";
    return 0;
}
