#include <eng/telemetry/Telemetry.h>

#include <eng/Log.h>

#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdarg>
#include <cstdio>
#include <deque>
#include <map>
#include <mutex>
#include <thread>

namespace eng::telemetry {
namespace {

// One mutex over the ring, one over the channel table. Separate because the
// hot path (write) touches both and the cold path (listing channels for the
// UI) touches only the second -- sharing one would make the browser's poll
// contend with the game thread.
struct State {
    std::mutex ringMutex;
    std::condition_variable ringSignal;
    std::deque<Record> ring;
    std::size_t capacity = 8192;

    std::mutex channelMutex;
    std::map<std::string, bool, std::less<>> channels;

    std::unique_ptr<Sink> sink;
    std::thread worker;
    std::atomic<bool> running{false};
    std::atomic<bool> connected{false};
    std::atomic<std::uint64_t> frame{0};
    std::atomic<Level> minimumLevel{Level::Trace};
    int flushIntervalMs = 33;

    std::atomic<std::uint64_t> written{0};
    std::atomic<std::uint64_t> published{0};
    std::atomic<std::uint64_t> droppedRingFull{0};
    std::atomic<std::uint64_t> droppedDisabled{0};
    std::atomic<std::uint64_t> publishFailures{0};

    int logSinkToken = -1;

    // Stops the worker if a caller never did. Without this, exiting while the
    // pipe is up -- an abort, a std::exit, a test that fails -- destroys a
    // joinable std::thread and terminates the process, turning a clean failure
    // into a crash that hides it.
    ~State()
    {
        running.store(false, std::memory_order_release);
        ringSignal.notify_all();
        if (worker.joinable())
            worker.join();
    }
};

State& state()
{
    static State instance;
    return instance;
}

double nowMs()
{
    using clock = std::chrono::steady_clock;
    static const clock::time_point start = clock::now();
    return std::chrono::duration<double, std::milli>(clock::now() - start)
        .count();
}

// Registers the channel on first sight and reports whether it is on. Channels
// default to enabled: a stream nobody asked to silence should be visible, and
// discovering one by seeing its output is the point.
bool touchChannel(State& s, std::string_view channel)
{
    const std::lock_guard<std::mutex> lock(s.channelMutex);
    const auto found = s.channels.find(channel);
    if (found != s.channels.end())
        return found->second;
    s.channels.emplace(std::string(channel), true);
    return true;
}

void enqueue(State& s, Record&& record)
{
    {
        const std::lock_guard<std::mutex> lock(s.ringMutex);
        // Drop the OLDEST, not the newest. When a frame floods the ring the
        // interesting records are the ones at the end -- the ones nearest
        // whatever went wrong -- and dropping the newest would throw away
        // exactly those.
        while (s.ring.size() >= s.capacity) {
            s.ring.pop_front();
            s.droppedRingFull.fetch_add(1, std::memory_order_relaxed);
        }
        s.ring.push_back(std::move(record));
    }
    s.ringSignal.notify_one();
    s.written.fetch_add(1, std::memory_order_relaxed);
}

void workerMain()
{
    State& s = state();
    std::vector<Record> batch;
    while (s.running.load(std::memory_order_acquire)) {
        {
            std::unique_lock<std::mutex> lock(s.ringMutex);
            if (s.ring.empty()) {
                s.ringSignal.wait_for(
                    lock, std::chrono::milliseconds(s.flushIntervalMs));
            }
            batch.assign(std::make_move_iterator(s.ring.begin()),
                         std::make_move_iterator(s.ring.end()));
            s.ring.clear();
        }
        if (batch.empty())
            continue;

        if (s.sink && s.sink->publish(batch)) {
            s.published.fetch_add(batch.size(), std::memory_order_relaxed);
            s.connected.store(true, std::memory_order_relaxed);
        } else {
            s.publishFailures.fetch_add(1, std::memory_order_relaxed);
            s.connected.store(false, std::memory_order_relaxed);
        }
        batch.clear();
    }

    // Last drain, so the records from the frame that called stop() -- often the
    // ones explaining why it is shutting down -- are not lost.
    std::vector<Record> tail;
    {
        const std::lock_guard<std::mutex> lock(s.ringMutex);
        tail.assign(std::make_move_iterator(s.ring.begin()),
                    std::make_move_iterator(s.ring.end()));
        s.ring.clear();
    }
    if (!tail.empty() && s.sink)
        s.sink->publish(tail);
}

} // namespace

const char* levelName(Level level)
{
    switch (level) {
    case Level::Trace: return "trace";
    case Level::Info: return "info";
    case Level::Warn: return "warn";
    case Level::Error: return "error";
    }
    return "info";
}

bool start(std::unique_ptr<Sink> sink, const Config& config)
{
    stop();
    State& s = state();
    s.capacity = config.ringCapacity ? config.ringCapacity : 1;
    s.flushIntervalMs = config.flushIntervalMs > 0 ? config.flushIntervalMs : 1;
    s.minimumLevel.store(config.minimumLevel, std::memory_order_relaxed);
    s.sink = std::move(sink);
    s.running.store(true, std::memory_order_release);
    try {
        s.worker = std::thread(workerMain);
    } catch (...) {
        // A debug channel that cannot start its thread is a debug channel that
        // does nothing, not a reason the game fails to boot.
        s.running.store(false, std::memory_order_release);
        s.sink.reset();
        log::warn("Telemetry: publisher thread could not start");
        return false;
    }
    log::info("Telemetry: publishing to %s",
              s.sink ? s.sink->describe().c_str() : "nothing");
    return true;
}

void stop()
{
    State& s = state();
    if (!s.running.exchange(false, std::memory_order_acq_rel)) {
        s.sink.reset();
        return;
    }
    s.ringSignal.notify_all();
    if (s.worker.joinable())
        s.worker.join();
    s.sink.reset();
    s.connected.store(false, std::memory_order_relaxed);
}

bool running() { return state().running.load(std::memory_order_acquire); }

bool enabled(std::string_view channel, Level level)
{
    State& s = state();
    if (!s.running.load(std::memory_order_acquire))
        return false;
    if (level < s.minimumLevel.load(std::memory_order_relaxed))
        return false;
    return touchChannel(s, channel);
}

void write(std::string_view channel, Level level, std::string_view message)
{
    State& s = state();
    if (!s.running.load(std::memory_order_acquire))
        return;
    if (level < s.minimumLevel.load(std::memory_order_relaxed) ||
        !touchChannel(s, channel)) {
        s.droppedDisabled.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    Record record;
    record.channel.assign(channel);
    record.message.assign(message);
    record.level = level;
    record.frame = s.frame.load(std::memory_order_relaxed);
    record.timeMs = nowMs();
    enqueue(s, std::move(record));
}

void writef(std::string_view channel, Level level, const char* fmt, ...)
{
    char buffer[1024];
    va_list args;
    va_start(args, fmt);
    const int length = std::vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    if (length < 0)
        return;
    // Truncated rather than heap-allocated for a longer one: a debug line past
    // a kilobyte is a dump, and a dump belongs in a file.
    write(channel, level,
          std::string_view(buffer, std::size_t(length) < sizeof(buffer)
                                       ? std::size_t(length)
                                       : sizeof(buffer) - 1));
}

namespace {
// The three non-log kinds differ only in what the browser does with them, so
// they share one builder rather than three near-copies that drift.
void emit(std::string_view channel, std::string_view name, Kind kind,
          Level level, std::string_view text, double value, bool hasValue)
{
    State& s = state();
    if (!s.running.load(std::memory_order_acquire) || !touchChannel(s, channel))
        return;
    Record record;
    record.channel.assign(channel);
    record.message.assign(name);
    record.level = level;
    record.kind = kind;
    record.frame = s.frame.load(std::memory_order_relaxed);
    record.timeMs = nowMs();
    record.value = value;
    record.hasValue = hasValue;
    if (!text.empty())
        record.message.append("\x1f").append(text); // unit separator: name\x1fvalue
    enqueue(s, std::move(record));
}
} // namespace

void sample(std::string_view channel, std::string_view name, double value)
{
    emit(channel, name, Kind::Sample, Level::Trace, {}, value, true);
}

void watch(std::string_view channel, std::string_view name,
           std::string_view value)
{
    emit(channel, name, Kind::Watch, Level::Trace, value, 0.0, false);
}

void watchf(std::string_view channel, std::string_view name, const char* fmt,
            ...)
{
    char buffer[512];
    va_list args;
    va_start(args, fmt);
    const int length = std::vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    if (length < 0)
        return;
    watch(channel, name,
          std::string_view(buffer, std::size_t(length) < sizeof(buffer)
                                       ? std::size_t(length)
                                       : sizeof(buffer) - 1));
}

void watchValue(std::string_view channel, std::string_view name, double value)
{
    // Both a watch (current value, in place) and a series, because a number
    // worth watching is almost always a number worth seeing the shape of.
    emit(channel, name, Kind::Watch, Level::Trace, {}, value, true);
}

void event(std::string_view channel, std::string_view name)
{
    emit(channel, name, Kind::Event, Level::Info, {}, 0.0, false);
}

void setFrame(std::uint64_t frame)
{
    state().frame.store(frame, std::memory_order_relaxed);
}

std::vector<std::string> channels()
{
    State& s = state();
    const std::lock_guard<std::mutex> lock(s.channelMutex);
    std::vector<std::string> names;
    names.reserve(s.channels.size());
    for (const auto& [name, on] : s.channels)
        names.push_back(name);
    return names;
}

void setChannelEnabled(std::string_view channel, bool on)
{
    State& s = state();
    const std::lock_guard<std::mutex> lock(s.channelMutex);
    const auto found = s.channels.find(channel);
    if (found == s.channels.end())
        s.channels.emplace(std::string(channel), on);
    else
        found->second = on;
}

bool channelEnabled(std::string_view channel)
{
    State& s = state();
    const std::lock_guard<std::mutex> lock(s.channelMutex);
    const auto found = s.channels.find(channel);
    return found == s.channels.end() ? true : found->second;
}

Stats stats()
{
    State& s = state();
    Stats out;
    out.written = s.written.load(std::memory_order_relaxed);
    out.published = s.published.load(std::memory_order_relaxed);
    out.droppedRingFull = s.droppedRingFull.load(std::memory_order_relaxed);
    out.droppedDisabled = s.droppedDisabled.load(std::memory_order_relaxed);
    out.publishFailures = s.publishFailures.load(std::memory_order_relaxed);
    out.connected = s.connected.load(std::memory_order_relaxed);
    return out;
}

std::string channelForLogLine(std::string_view text)
{
    // The prefix is what precedes the first ": ", and only when it is short
    // enough and free of sentence punctuation to plausibly BE a prefix -- a
    // line like "warning: the level is broken: see below" must not become a
    // channel called "warning" for its first clause and something else later.
    const std::size_t colon = text.find(':');
    if (colon == std::string_view::npos || colon == 0 || colon > 24)
        return {};
    std::string_view prefix = text.substr(0, colon);
    // A prefix is one token, possibly with a parenthesised qualifier
    // ("rhi(vulkan)") or a single space ("RHI renderer"). More than one space
    // is a sentence -- "could not open x: no such file" must not become a
    // channel called "could" -- and any other punctuation is prose too.
    int spaces = 0;
    for (const char c : prefix) {
        if (c == ' ' && ++spaces > 1)
            return {};
        const bool ok = std::isalnum(static_cast<unsigned char>(c)) || c == ' ' ||
                        c == '(' || c == ')' || c == '_' || c == '-';
        if (!ok)
            return {};
    }

    // Normalise: lowercase, and keep only the first word (so "RHI renderer"
    // and "rhi(vulkan)" both reduce to "rhi").
    std::string key;
    for (const char c : prefix) {
        if (c == ' ' || c == '(')
            break;
        key.push_back(static_cast<char>(
            std::tolower(static_cast<unsigned char>(c))));
    }
    if (key.empty())
        return {};

    // The only hand-written part: prefixes that are the same system under
    // different names. Everything else keeps its own channel.
    struct Alias { const char* from; const char* to; };
    static constexpr Alias kAliases[] = {
        {"rhi", "render"},          {"warmup", "render"},
        {"renderer", "render"},     {"particletextures", "particles"},
        {"particlelibrary", "particles"},
        {"enemyspawner", "enemy"},  {"enemylibrary", "enemy"},
        {"combatvocabulary", "combat"}, {"bloodsystem", "combat"},
        {"dungeonmap", "world"},    {"scene", "world"},
        {"audiocatalog", "audio"},  {"gameaudio", "audio"},
        {"imguilayout", "ui"},      {"window", "engine"},
        {"prototypes", "assets"},   {"telemetry", "engine"},
    };
    for (const Alias& alias : kAliases)
        if (key == alias.from)
            return alias.to;
    return key;
}

void mirrorEngineLog(std::string_view fallback)
{
    State& s = state();
    if (s.logSinkToken >= 0)
        return;
    const std::string name(fallback);
    s.logSinkToken = log::addSink([name](log::Level level, const char* text) {
        // Deliberately not routed back through eng::log on failure: a sink that
        // logs is a sink that recurses through the lock it is already holding.
        Level mapped = Level::Info;
        switch (level) {
        case log::Level::Info: mapped = Level::Info; break;
        case log::Level::Warn: mapped = Level::Warn; break;
        case log::Level::Error:
        case log::Level::Fatal: mapped = Level::Error; break;
        }
        const std::string_view line = text ? text : "";
        // The system that wrote it, not one bucket for all of them: a channel
        // list of one is a channel list nobody can filter with.
        std::string channel = channelForLogLine(line);
        write(channel.empty() ? name : channel, mapped, line);
    });
}

void stopMirroringEngineLog()
{
    State& s = state();
    if (s.logSinkToken >= 0) {
        log::removeSink(s.logSinkToken);
        s.logSinkToken = -1;
    }
}

} // namespace eng::telemetry
