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

// Formats into a stack buffer and only heap-allocates for the rare long line.
// Nothing is formatted at all while no sink is attached, so logging costs the
// same as before the console existed.
void fanout(Level level, const char* fmt, va_list ap)
{
    std::lock_guard<std::mutex> lock(sinkMutex());
    if (sinks().empty())
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
    sinks().push_back({token, std::move(sink)});
    return token;
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
