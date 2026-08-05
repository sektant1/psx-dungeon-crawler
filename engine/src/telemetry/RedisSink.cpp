#include <eng/telemetry/RedisSink.h>

#include <eng/Log.h>
#include <eng/telemetry/Resp.h>
#include <eng/telemetry/Socket.h>

#include <chrono>
#include <cstdio>
#include <set>
#include <vector>

namespace eng::telemetry {
namespace {

void appendNumber(std::string& out, double value)
{
    char buffer[40];
    std::snprintf(buffer, sizeof(buffer), "%.4f", value);
    out.append(buffer);
}

class RedisSink final : public Sink {
public:
    explicit RedisSink(const RedisConfig& config) : mConfig(config)
    {
        Socket::platformInit();
    }
    ~RedisSink() override
    {
        mSocket.close();
        Socket::platformShutdown();
    }

    std::string describe() const override
    {
        return "redis " + mConfig.host + ":" + std::to_string(mConfig.port) +
               " (prefix '" + mConfig.prefix + "')";
    }

    bool publish(const std::vector<Record>& batch) override
    {
        if (!ensureConnected())
            return false;

        mOut.clear();
        mKeys.clear();
        mKeys.reserve(batch.size() * 2);
        mPayloads.clear();
        mPayloads.reserve(batch.size());

        // Strings are built into stable vectors first: appendCommand takes
        // string_views, and a view into a vector that reallocates mid-loop is
        // the classic way to publish garbage.
        for (const Record& record : batch) {
            mPayloads.push_back(recordJson(record));
            mKeys.push_back(mConfig.prefix + ":ch:" + record.channel);
            mKeys.push_back(mConfig.prefix + ":log:" + record.channel);
        }

        int commands = 0;
        for (std::size_t i = 0; i < batch.size(); ++i) {
            const std::string& live = mKeys[i * 2];
            const std::string& history = mKeys[i * 2 + 1];
            const std::string& payload = mPayloads[i];
            appendCommand(mOut, {"PUBLISH", live, payload});
            appendCommand(mOut, {"LPUSH", history, payload});
            ++commands;
            ++commands;
        }
        // One trim per flush rather than per record: LTRIM on a list that is
        // already short is cheap, and doing it per record doubled the command
        // count for no benefit.
        const std::string cap = std::to_string(mConfig.historyPerChannel - 1);
        mTrimmed.clear();
        for (std::size_t i = 0; i < batch.size(); ++i) {
            const std::string& history = mKeys[i * 2 + 1];
            if (mTrimmed.insert(history).second) {
                appendCommand(mOut, {"LTRIM", history, "0", cap});
                ++commands;
            }
        }
        // A registry of live channels, so the browser can list them before any
        // record on a given channel scrolls past.
        for (const Record& record : batch) {
            if (mAnnounced.insert(record.channel).second) {
                appendCommand(mOut,
                              {"SADD", mChannelSet, record.channel});
                ++commands;
            }
        }

        std::string error;
        if (!mSocket.sendAll(mOut, &error)) {
            log::warn("Telemetry: redis write failed (%s); reconnecting",
                      error.c_str());
            drop();
            return false;
        }
        return drainReplies(commands);
    }

private:
    bool ensureConnected()
    {
        if (mSocket.valid())
            return true;
        const auto now = std::chrono::steady_clock::now();
        if (now < mNextAttempt)
            return false; // still backing off; records are dropped meanwhile

        std::string error;
        if (!mSocket.connect(mConfig.host, mConfig.port,
                             mConfig.connectTimeoutMs, &error)) {
            mNextAttempt =
                now + std::chrono::milliseconds(mConfig.reconnectMs);
            if (!mWarned) {
                // Once, not per attempt: a session with no Connector running is
                // the normal case and must not fill the log with it.
                log::info("Telemetry: no collector at %s:%u (%s); retrying "
                          "quietly",
                          mConfig.host.c_str(), unsigned(mConfig.port),
                          error.c_str());
                mWarned = true;
            }
            return false;
        }
        mChannelSet = mConfig.prefix + ":channels";
        mAnnounced.clear(); // a fresh collector has to be told again
        mWarned = false;
        log::info("Telemetry: connected to %s", describe().c_str());
        return true;
    }

    void drop()
    {
        mSocket.close();
        mNextAttempt = std::chrono::steady_clock::now() +
                       std::chrono::milliseconds(mConfig.reconnectMs);
    }

    // Replies are counted, not interpreted. They still have to be consumed --
    // an unread socket buffer eventually stalls the writer -- but the only
    // outcome that changes behaviour is an error, which drops the connection.
    bool drainReplies(int expected)
    {
        std::string error;
        int seen = 0;
        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::milliseconds(50);
        while (seen < expected) {
            std::size_t consumed = 0;
            std::string replyError;
            const ReplyKind kind =
                classifyReply(mIn, consumed, &replyError);
            if (kind == ReplyKind::Incomplete) {
                if (std::chrono::steady_clock::now() > deadline)
                    break; // the collector is slow, not broken; try next flush
                if (!mSocket.receiveSome(mIn, 5, &error)) {
                    log::warn("Telemetry: redis read failed (%s)",
                              error.c_str());
                    drop();
                    return false;
                }
                continue;
            }
            mIn.erase(0, consumed);
            ++seen;
            if (kind == ReplyKind::Error) {
                log::warn("Telemetry: redis rejected a command (%s)",
                          replyError.c_str());
                drop();
                return false;
            }
        }
        return true;
    }

    RedisConfig mConfig;
    Socket mSocket;
    std::string mOut;
    std::string mIn;
    std::string mChannelSet;
    std::vector<std::string> mKeys;
    std::vector<std::string> mPayloads;
    std::set<std::string> mTrimmed;
    std::set<std::string> mAnnounced;
    std::chrono::steady_clock::time_point mNextAttempt{};
    bool mWarned = false;
};

} // namespace

std::string recordJson(const Record& record)
{
    // Hand-built rather than through a JSON library: the shape is fixed, it is
    // built a few thousand times a second, and the only field that can contain
    // arbitrary text goes through appendJsonString.
    std::string out;
    out.reserve(record.message.size() + 128);
    out.append("{\"ch\":");
    appendJsonString(out, record.channel);
    out.append(",\"lvl\":");
    appendJsonString(out, levelName(record.level));
    out.append(",\"frame\":");
    out.append(std::to_string(record.frame));
    out.append(",\"t\":");
    appendNumber(out, record.timeMs);
    out.append(",\"kind\":");
    switch (record.kind) {
    case Kind::Sample: out.append("\"sample\""); break;
    case Kind::Watch: out.append("\"watch\""); break;
    case Kind::Event: out.append("\"event\""); break;
    case Kind::Log: out.append("\"log\""); break;
    }
    // A watch carries name and value in one field separated by \x1f, so the
    // record shape stays fixed. Split here rather than in the browser: the
    // separator is a wire detail and should not leave this file.
    if (const std::size_t split = record.message.find('\x1f');
        split != std::string::npos) {
        out.append(",\"name\":");
        appendJsonString(out, std::string_view(record.message).substr(0, split));
        out.append(",\"msg\":");
        appendJsonString(out, std::string_view(record.message).substr(split + 1));
    } else {
        out.append(",\"name\":");
        appendJsonString(out, record.message);
        out.append(",\"msg\":");
        appendJsonString(out, record.message);
    }
    if (record.hasValue) {
        out.append(",\"v\":");
        appendNumber(out, record.value);
    }
    out.push_back('}');
    return out;
}

std::unique_ptr<Sink> makeRedisSink(const RedisConfig& config)
{
    return std::make_unique<RedisSink>(config);
}

} // namespace eng::telemetry
