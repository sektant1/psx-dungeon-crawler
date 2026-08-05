#include <eng/telemetry/Resp.h>

#include <charconv>
#include <cstdio>

namespace eng::telemetry {
namespace {

void appendUnsigned(std::string& out, std::size_t value)
{
    char digits[24];
    const auto result =
        std::to_chars(digits, digits + sizeof(digits), value);
    out.append(digits, std::size_t(result.ptr - digits));
}

// Where the CRLF ending the line at `from` is, or npos when the line has not
// fully arrived. Deliberately looks for the pair rather than a bare \n: a bulk
// payload may legitimately contain \n, and only the framing uses \r\n.
std::size_t lineEnd(std::string_view buffer, std::size_t from)
{
    const std::size_t at = buffer.find("\r\n", from);
    return at;
}

} // namespace

void appendCommand(std::string& out, const std::vector<std::string_view>& args)
{
    out.push_back('*');
    appendUnsigned(out, args.size());
    out.append("\r\n");
    for (const std::string_view arg : args) {
        out.push_back('$');
        appendUnsigned(out, arg.size());
        out.append("\r\n");
        out.append(arg);
        out.append("\r\n");
    }
}

std::string encodeCommand(const std::vector<std::string_view>& args)
{
    std::string out;
    appendCommand(out, args);
    return out;
}

ReplyKind classifyReply(std::string_view buffer, std::size_t& consumed,
                        std::string* error)
{
    consumed = 0;
    if (buffer.empty())
        return ReplyKind::Incomplete;

    const char tag = buffer.front();
    const std::size_t end = lineEnd(buffer, 0);
    if (end == std::string_view::npos)
        return ReplyKind::Incomplete;
    const std::string_view line = buffer.substr(1, end - 1);

    switch (tag) {
    case '+': // simple string
    case ':': // integer
        consumed = end + 2;
        return ReplyKind::Ok;
    case '-': // error
        consumed = end + 2;
        if (error)
            error->assign(line);
        return ReplyKind::Error;
    case '$': { // bulk string: a length line, then that many bytes, then CRLF
        long long length = -1;
        std::from_chars(line.data(), line.data() + line.size(), length);
        if (length < 0) { // null bulk string, no payload follows
            consumed = end + 2;
            return ReplyKind::Ok;
        }
        const std::size_t total = end + 2 + std::size_t(length) + 2;
        if (buffer.size() < total)
            return ReplyKind::Incomplete;
        consumed = total;
        return ReplyKind::Ok;
    }
    case '*': { // array: a count, then that many replies
        long long count = -1;
        std::from_chars(line.data(), line.data() + line.size(), count);
        std::size_t at = end + 2;
        for (long long i = 0; i < count; ++i) {
            std::size_t inner = 0;
            const ReplyKind kind =
                classifyReply(buffer.substr(at), inner, error);
            if (kind == ReplyKind::Incomplete)
                return ReplyKind::Incomplete;
            at += inner;
            if (kind == ReplyKind::Error) {
                consumed = at;
                return ReplyKind::Error;
            }
        }
        consumed = at;
        return ReplyKind::Ok;
    }
    default:
        break;
    }
    // An unrecognised tag means the stream is out of sync, and there is no way
    // back from that on a byte stream. Reported as an error so the sink drops
    // the connection and reconnects rather than reading garbage forever.
    consumed = buffer.size();
    if (error)
        error->assign("unrecognised RESP reply tag");
    return ReplyKind::Error;
}

void appendJsonString(std::string& out, std::string_view text)
{
    out.push_back('"');
    for (const char c : text) {
        switch (c) {
        case '"': out.append("\\\""); break;
        case '\\': out.append("\\\\"); break;
        case '\n': out.append("\\n"); break;
        case '\r': out.append("\\r"); break;
        case '\t': out.append("\\t"); break;
        case '\b': out.append("\\b"); break;
        case '\f': out.append("\\f"); break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                char escape[8];
                std::snprintf(escape, sizeof(escape), "\\u%04x",
                              static_cast<unsigned char>(c));
                out.append(escape);
            } else {
                out.push_back(c);
            }
            break;
        }
    }
    out.push_back('"');
}

} // namespace eng::telemetry
