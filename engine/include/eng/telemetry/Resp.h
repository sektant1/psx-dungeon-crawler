#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace eng::telemetry {

// The Redis serialization protocol, just the slice a producer needs.
//
// Hand-written rather than linked against hiredis, for two reasons that are
// about this project rather than about hiredis. The command half of RESP is
// forty lines -- an array of bulk strings, each length-prefixed -- and it is
// frozen: Redis has never broken it. And a vendored C library would have to be
// built for three platforms and kept current for a debug channel that must not
// be able to take the game down. Forty lines we own is the smaller liability.
//
// Only the encoder lives here. Replies are parsed by the sink, which needs to
// recognise exactly two things: "+OK" and an error.

// One command: `*<argc>\r\n` then `$<len>\r\n<arg>\r\n` per argument.
//
// Binary safe by construction -- every argument carries its own length, so a
// log line with a newline, a quote or a NUL in it needs no escaping and cannot
// desynchronise the stream. That matters more than it sounds: the payloads here
// are JSON built from arbitrary game text.
std::string encodeCommand(const std::vector<std::string_view>& args);

// Appends to `out` instead of returning, for the batching path: a flush encodes
// hundreds of commands into one buffer and issues a single write.
void appendCommand(std::string& out, const std::vector<std::string_view>& args);

// What a Redis reply says, reduced to what a fire-and-forget producer acts on.
enum class ReplyKind {
    Incomplete, // not all here yet; read more before deciding
    Ok,         // any non-error reply -- the value is not interesting to us
    Error,      // `-ERR ...`
};

// Classifies the reply at the front of `buffer` and reports how many bytes it
// spans, so the caller can consume exactly one and keep the rest.
//
// Pipelining is the reason this exists: the sink writes N commands and then has
// to account for N replies without a full parser, because leaving them in the
// socket buffer would eventually wedge the connection.
ReplyKind classifyReply(std::string_view buffer, std::size_t& consumed,
                        std::string* error = nullptr);

// A JSON string literal with the six characters JSON forbids escaped, plus
// anything below 0x20 as \u00XX. Not a general JSON writer -- the records here
// are a fixed shape and this is the only field that can contain arbitrary text.
void appendJsonString(std::string& out, std::string_view text);

} // namespace eng::telemetry
