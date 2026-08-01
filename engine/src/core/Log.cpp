#include <eng/Log.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace eng::log {

namespace {

struct SinkEntry {
    int token;
    Sink fn;
};

std::mutex& sinkMutex()
{
    static std::mutex m;
    return m;
}

std::vector<SinkEntry>& sinks()
{
    static std::vector<SinkEntry> s;
    return s;
}

struct BacklogLine {
    Level level;
    std::string text;
};

// Guarded by sinkMutex, like the sink list: a line is appended and fanned out
// under one lock, so a sink attaching mid-run cannot see a line twice or miss
// one between the replay and its first live call.
std::vector<BacklogLine>& backlog()
{
    static std::vector<BacklogLine> b;
    return b;
}

std::size_t& backlogCapacity()
{
    static std::size_t capacity = 512;
    return capacity;
}

// Formats into a stack buffer and only heap-allocates for the rare long line.
void fanout(Level level, const char* fmt, va_list ap)
{
    std::lock_guard<std::mutex> lock(sinkMutex());
    // Formatting still happens when nothing is listening, because the backlog
    // IS the listener that has not arrived yet. Disabling it (capacity 0) with
    // no sinks attached restores the old free path.
    if (sinks().empty() && backlogCapacity() == 0)
        return;
    char stackBuf[1024];
    va_list copy;
    va_copy(copy, ap);
    const int n = std::vsnprintf(stackBuf, sizeof(stackBuf), fmt, copy);
    va_end(copy);
    if (n < 0)
        return;
    const char* text = stackBuf;
    std::string heap;
    if (std::size_t(n) >= sizeof(stackBuf)) {
        heap.resize(std::size_t(n) + 1);
        va_copy(copy, ap);
        std::vsnprintf(heap.data(), heap.size(), fmt, copy);
        va_end(copy);
        text = heap.c_str();
    }
    if (const std::size_t capacity = backlogCapacity(); capacity > 0) {
        std::vector<BacklogLine>& lines = backlog();
        // Ring by erase-from-front: the cap is small and a line is appended
        // once per log call, so the memmove is cheaper than the indirection a
        // real ring buffer would add to every reader.
        if (lines.size() >= capacity)
            lines.erase(lines.begin(),
                        lines.begin() +
                            std::ptrdiff_t(lines.size() - capacity + 1));
        lines.push_back({level, text});
    }

    for (const SinkEntry& s : sinks())
        if (s.fn)
            s.fn(level, text);
}

void write(Level level, const char* levelName, const char* fmt, va_list ap)
{
    va_list copy;
    va_copy(copy, ap);
    std::fprintf(stderr, "[%s] ", levelName);
    std::vfprintf(stderr, fmt, copy);
    std::fputc('\n', stderr);
    va_end(copy);
    fanout(level, fmt, ap);
}

} // namespace

#define ENG_LOG_BODY(level, name)                                              \
    va_list ap;                                                                \
    va_start(ap, fmt);                                                         \
    write(level, name, fmt, ap);                                               \
    va_end(ap)

void info(const char* fmt, ...) { ENG_LOG_BODY(Level::Info, "info"); }
void warn(const char* fmt, ...) { ENG_LOG_BODY(Level::Warn, "warn"); }
void error(const char* fmt, ...) { ENG_LOG_BODY(Level::Error, "error"); }
void fatal(const char* fmt, ...)
{
    ENG_LOG_BODY(Level::Fatal, "fatal");
    std::abort();
}

int addSink(Sink sink)
{
    static int nextToken = 1;
    std::lock_guard<std::mutex> lock(sinkMutex());
    const int token = nextToken++;
    // Replayed before the sink is registered, and under the same lock, so the
    // sink sees the run in order with no gap and no duplicate at the seam.
    if (sink)
        for (const BacklogLine& line : backlog())
            sink(line.level, line.text.c_str());
    sinks().push_back({token, std::move(sink)});
    return token;
}

void setBacklogCapacity(std::size_t lines)
{
    std::lock_guard<std::mutex> lock(sinkMutex());
    backlogCapacity() = lines;
    std::vector<BacklogLine>& kept = backlog();
    if (lines == 0)
        kept.clear();
    else if (kept.size() > lines)
        kept.erase(kept.begin(),
                   kept.begin() + std::ptrdiff_t(kept.size() - lines));
}

void replayBacklog(const Sink& sink)
{
    if (!sink)
        return;
    std::lock_guard<std::mutex> lock(sinkMutex());
    for (const BacklogLine& line : backlog())
        sink(line.level, line.text.c_str());
}

void removeSink(int token)
{
    std::lock_guard<std::mutex> lock(sinkMutex());
    auto& s = sinks();
    for (auto it = s.begin(); it != s.end(); ++it)
        if (it->token == token) {
            s.erase(it);
            return;
        }
}

} // namespace eng::log
