#pragma once

#include <eng/telemetry/Telemetry.h>

#include <memory>
#include <string>

namespace eng::telemetry {

struct RedisConfig {
    std::string host = "127.0.0.1";
    unsigned short port = 6379;
    // Namespace for every key and channel this writes, so a Redis shared with
    // anything else stays legible and one `DEL raven:*` cleans up.
    std::string prefix = "raven";
    // Scrollback per channel. The browser asks for this on connect, which is
    // what makes opening Connector mid-session useful rather than showing an
    // empty pane until the next line arrives.
    int historyPerChannel = 2000;
    // How long to wait between reconnect attempts once the connection drops.
    // The game is not waiting on this, so it is generous.
    int reconnectMs = 2000;
    int connectTimeoutMs = 500;
};

// Publishes records to Redis (or to anything speaking its wire protocol -- the
// Connector server does, so no Redis is required to use this).
//
// Two writes per record, which is the shape the book's Connector implies and
// what the browser needs:
//
//   PUBLISH <prefix>:ch:<channel> <json>    live tail for anyone subscribed
//   LPUSH   <prefix>:log:<channel> <json>   scrollback, LTRIM'd to a cap
//
// Pipelined: a batch becomes one buffer and one write, and the replies are
// counted rather than parsed. A debug channel that costs a round trip per line
// would be a debug channel that changes the timing it is there to report.
std::unique_ptr<Sink> makeRedisSink(const RedisConfig& config);

// Builds the JSON one record becomes. Exposed for the test, which is the only
// way to pin the field names the browser depends on without a running Redis.
std::string recordJson(const Record& record);

} // namespace eng::telemetry
