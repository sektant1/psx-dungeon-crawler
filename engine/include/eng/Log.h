#pragma once

#include <cstddef>
#include <functional>

// printf-style logging to stderr. fatal() logs and aborts -- used for
// programmer errors (invalid handle, unknown material); no exceptions
// cross the public API boundary.
namespace eng::log {
void info(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void warn(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void error(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
[[noreturn]] void fatal(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

enum class Level { Info, Warn, Error, Fatal };

// Every log call is mirrored to the registered sinks, formatted but without
// the level prefix. This is how the in-game console shows engine output that
// was written long before any UI existed; stderr always keeps its copy, so a
// crash still leaves a terminal trace.
//
// Sinks run on the calling thread while the sink lock is held: never log from
// inside one. addSink returns a token for removeSink().
//
// A new sink is replayed the backlog (below) before it sees anything live, so
// attaching a console mid-run still shows how the run started.
using Sink = std::function<void(Level, const char*)>;
int addSink(Sink sink);
void removeSink(int token);

// Recent lines, retained whether or not anything is listening.
//
// Without this, every line logged before the first sink attached was gone: a
// console built in onStart could never show window creation, GL capabilities,
// asset-root registration, shader compile failures or the boot warmup -- which
// is precisely the part of a run you open a console to read. The ring is small
// and formatting still only happens once per call.
void setBacklogCapacity(std::size_t lines); // default 512; 0 disables
void replayBacklog(const Sink& sink);       // for a sink attached by hand
} // namespace eng::log
