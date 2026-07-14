#pragma once

// printf-style logging to stderr. fatal() logs and aborts -- used for
// programmer errors (invalid handle, unknown material); no exceptions
// cross the public API boundary.
namespace eng::log {
void info(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void warn(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void error(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
[[noreturn]] void fatal(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
} // namespace eng::log
