#include <eng/Log.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>

namespace eng::log {

namespace {
void write(const char* level, const char* fmt, va_list ap)
{
    va_list copy;
    va_copy(copy, ap);
    std::fprintf(stderr, "[%s] ", level);
    std::vfprintf(stderr, fmt, copy);
    std::fputc('\n', stderr);
    va_end(copy);
}
} // namespace

#define ENG_LOG_BODY(level)                                                    \
    va_list ap;                                                                \
    va_start(ap, fmt);                                                         \
    write(level, fmt, ap);                                                     \
    va_end(ap)

void info(const char* fmt, ...) { ENG_LOG_BODY("info"); }
void warn(const char* fmt, ...) { ENG_LOG_BODY("warn"); }
void error(const char* fmt, ...) { ENG_LOG_BODY("error"); }
void fatal(const char* fmt, ...)
{
    ENG_LOG_BODY("fatal");
    std::abort();
}

} // namespace eng::log
