#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace eng::telemetry {

// Named debug streams, and a pipe that carries them out of the process.
//
// The model is Naughty Dog's Connector (Game Engine Architecture, 4th ed.,
// 3.4.4): the engine writes text into channels named after the systems that
// produce it -- render, anim, ai, audio, physics, script -- a key-value store
// collects them, and a browser filters the result. What that buys over a single
// log is *selection*: an animation bug is thirty lines an hour buried in ten
// thousand from everything else, and no amount of scrollback makes that
// readable. Channels turn scrolling into a filter.
//
// Three properties this has to have, and they are the design:
//
//   1. It must never be able to take the game down. Nothing here throws, the
//      socket lives on its own thread, and every failure path is "drop the
//      record and try to reconnect later".
//   2. It must cost nothing when nobody is listening. A disabled channel is an
//      atomic load and a branch -- no formatting, no allocation, no lock.
//   3. It must not stall the frame. The game thread writes into a ring buffer
//      and returns; a publisher thread does the encoding and the blocking I/O.
//      A full ring drops the oldest records and counts them, because a debug
//      channel that applies back-pressure to the simulation is a debug channel
//      that changes the bug.

enum class Level : std::uint8_t { Trace, Info, Warn, Error };

// What a record IS, which decides how the browser presents it.
//
// A debug stream is not all one thing. Most of it scrolls; some of it is a
// number over time that wants a graph; and some of it is a *current value* --
// the player's position, the entity count, which state the AI is in -- that
// should update in place rather than scroll past a thousand times a second.
// Putting the third kind in a scrolling log is what makes a log useless.
enum class Kind : std::uint8_t {
    Log,   // a line; scrolls
    Sample,// a number at a moment; graphed
    Watch, // the current value of something named; replaces the last one
    Event, // a marked moment: level load, death, checkpoint. Pinned on the timeline.
};

const char* levelName(Level level);

// One line on one channel.
//
// `frame` and `timeMs` are captured at write time rather than at publish time:
// the whole point is to correlate a line with the frame that produced it, and
// the publisher runs some milliseconds later.
struct Record {
    std::string channel;
    std::string message;
    Level level = Level::Info;
    std::uint64_t frame = 0;
    double timeMs = 0.0;
    // Optional numeric payload, for the streams that are a value over time
    // rather than text -- frame ms, draw calls, entity counts. NaN means "this
    // record is text only", which is the common case.
    double value = 0.0;
    bool hasValue = false;
    Kind kind = Kind::Log;
};

// Where records go once they leave the ring. Implemented by RedisSink; a test
// implements it directly.
//
// `publish` is called on the publisher thread with a whole batch, never per
// record, because the transport cost is per-syscall and a batch of two hundred
// costs the same as one.
class Sink {
public:
    virtual ~Sink() = default;
    virtual bool publish(const std::vector<Record>& batch) = 0;
    // Shown in the debug UI and the log line on connect/disconnect.
    virtual std::string describe() const = 0;
};

struct Config {
    // Records held between flushes. At 60 Hz with a flush every frame this is
    // enormous headroom; it exists for the pathological frame that logs a
    // thousand lines, which is exactly the frame you are debugging.
    std::size_t ringCapacity = 8192;
    // How long the publisher sleeps when the ring is empty. Low enough to feel
    // live in the browser, high enough not to spin a core.
    int flushIntervalMs = 33;
    // Below this, a write is discarded before it is formatted.
    Level minimumLevel = Level::Trace;
};

// Starts the publisher thread and takes ownership of the sink. Calling it twice
// replaces the sink, which is what a `connector.reconnect` console command
// wants. Returns false only if the thread could not start.
bool start(std::unique_ptr<Sink> sink, const Config& config = {});
void stop();
bool running();

// True when anything is listening AND `channel` is enabled AND `level` passes.
// The guard the macro below tests, exposed because a caller that would have to
// build an expensive string should ask first.
bool enabled(std::string_view channel, Level level = Level::Info);

// Write one record. Safe from any thread, safe when nothing is running (it
// becomes a branch), and never blocks on I/O.
void write(std::string_view channel, Level level, std::string_view message);
void writef(std::string_view channel, Level level, const char* fmt, ...)
    __attribute__((format(printf, 3, 4)));
// A numeric sample: the same channel machinery, drawn as a graph rather than a
// line. `name` is the series ("frame_ms", "draw_calls").
void sample(std::string_view channel, std::string_view name, double value);

// The current value of something, shown in a table that updates in place.
//
// This is the one that changes how a debug session feels. "Player position"
// written to a log sixty times a second is noise you filter out; the same
// thing as a watch is a number you glance at. Anything you would have put on
// a debug HUD belongs here instead, where it costs no screen space in the game
// and can be read on a second monitor.
void watch(std::string_view channel, std::string_view name,
           std::string_view value);
void watchf(std::string_view channel, std::string_view name, const char* fmt,
            ...) __attribute__((format(printf, 3, 4)));
void watchValue(std::string_view channel, std::string_view name, double value);

// A moment worth finding again: a level load, a death, a checkpoint, the frame
// a bug reproduced on. Pinned in the browser's timeline so you can jump to it
// instead of scrolling for the log line next to it.
void event(std::string_view channel, std::string_view name);

// The frame number stamped onto records from now on. The app sets this once per
// frame; without it every record reads frame 0 and the correlation that makes
// channels useful is gone.
void setFrame(std::uint64_t frame);

// Channels are discovered rather than declared -- the first write to a name
// registers it -- so adding one is a call site and nothing else. This is what
// the browser lists, and what the console's per-channel switches drive.
std::vector<std::string> channels();
void setChannelEnabled(std::string_view channel, bool enabled);
bool channelEnabled(std::string_view channel);

// What the pipe is doing, for the debug panel and for the test that asserts a
// full ring drops rather than blocks.
struct Stats {
    std::uint64_t written = 0;
    std::uint64_t published = 0;
    std::uint64_t droppedRingFull = 0;
    std::uint64_t droppedDisabled = 0;
    std::uint64_t publishFailures = 0;
    bool connected = false;
};
Stats stats();

// The channel a log line belongs to, derived from its own text.
//
// Nearly every line the engine writes already names its system: "RHI renderer:",
// "EnemySpawner:", "AudioCatalog:", "rhi(vulkan):". That prefix is the channel,
// and reading it is what turns thousands of existing call sites into real
// channels without touching one of them.
//
// Generic first, aliased second: an unrecognised prefix becomes its own channel
// rather than falling into a catch-all, so a system added tomorrow appears in
// the browser by itself. The alias table only exists to merge the handful of
// prefixes that are the same system under different names.
//
// Exposed for the test that pins the mapping against the prefixes the engine
// actually emits.
std::string channelForLogLine(std::string_view text);

// Mirrors every eng::log line into a telemetry channel, so the streams that
// predate this system show up in the browser without touching their call
// sites. Idempotent; the token is for symmetry with log::removeSink.
//
// `fallback` is used only for lines that name no system at all.
void mirrorEngineLog(std::string_view fallback = "log");
void stopMirroringEngineLog();

} // namespace eng::telemetry

// The write guard, as a macro, so an argument list is not evaluated when the
// channel is off. `telemetry::writef` would have to evaluate its arguments to
// be called at all; this way a disabled channel costs the atomic load in
// enabled() and nothing else.
#define ENG_TELEMETRY(channel, level, ...)                                     \
    do {                                                                       \
        if (::eng::telemetry::enabled((channel), (level)))                     \
            ::eng::telemetry::writef((channel), (level), __VA_ARGS__);         \
    } while (false)
