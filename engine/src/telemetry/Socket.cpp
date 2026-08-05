#include <eng/telemetry/Socket.h>

#include <cerrno>
#include <cstring>
#include <mutex>
#include <string>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
using SocketHandle = SOCKET;
static constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
#else
#  include <arpa/inet.h>
#  include <fcntl.h>
#  include <netdb.h>
#  include <netinet/in.h>
#  include <netinet/tcp.h>
#  include <sys/select.h>
#  include <sys/socket.h>
#  include <sys/types.h>
#  include <unistd.h>
using SocketHandle = int;
static constexpr SocketHandle kInvalidSocket = -1;
#endif

namespace eng::telemetry {
namespace {

SocketHandle asHandle(long long value)
{
    return value < 0 ? kInvalidSocket : static_cast<SocketHandle>(value);
}

std::string lastError()
{
#if defined(_WIN32)
    const int code = WSAGetLastError();
    char* text = nullptr;
    FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                       FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, DWORD(code), 0, reinterpret_cast<char*>(&text), 0,
                   nullptr);
    std::string message = text ? text : "winsock error";
    if (text)
        LocalFree(text);
    while (!message.empty() &&
           (message.back() == '\n' || message.back() == '\r'))
        message.pop_back();
    return message + " (" + std::to_string(code) + ")";
#else
    return std::strerror(errno);
#endif
}

bool wouldBlock()
{
#if defined(_WIN32)
    const int code = WSAGetLastError();
    return code == WSAEWOULDBLOCK || code == WSAEINPROGRESS;
#else
    return errno == EWOULDBLOCK || errno == EAGAIN || errno == EINPROGRESS;
#endif
}

void closeHandle(SocketHandle handle)
{
    if (handle == kInvalidSocket)
        return;
#if defined(_WIN32)
    ::closesocket(handle);
#else
    ::close(handle);
#endif
}

bool setBlocking(SocketHandle handle, bool blocking)
{
#if defined(_WIN32)
    u_long mode = blocking ? 0u : 1u;
    return ::ioctlsocket(handle, FIONBIO, &mode) == 0;
#else
    const int flags = ::fcntl(handle, F_GETFL, 0);
    if (flags < 0)
        return false;
    const int wanted = blocking ? (flags & ~O_NONBLOCK) : (flags | O_NONBLOCK);
    return ::fcntl(handle, F_SETFL, wanted) == 0;
#endif
}

// Winsock's startup/shutdown is process-wide and reference counted, so several
// sinks (or a sink and a test) can each call this without stepping on the
// other. A no-op everywhere else, which is why callers never branch on it.
std::mutex& platformMutex()
{
    static std::mutex mutex;
    return mutex;
}
int& platformRefs()
{
    static int refs = 0;
    return refs;
}

} // namespace

bool Socket::platformInit(std::string* error)
{
#if defined(_WIN32)
    const std::lock_guard<std::mutex> lock(platformMutex());
    if (platformRefs()++ > 0)
        return true;
    WSADATA data{};
    const int result = ::WSAStartup(MAKEWORD(2, 2), &data);
    if (result != 0) {
        platformRefs()--;
        if (error)
            *error = "WSAStartup failed (" + std::to_string(result) + ")";
        return false;
    }
    return true;
#else
    (void)error;
    const std::lock_guard<std::mutex> lock(platformMutex());
    ++platformRefs();
    return true;
#endif
}

void Socket::platformShutdown()
{
    const std::lock_guard<std::mutex> lock(platformMutex());
    if (platformRefs() > 0 && --platformRefs() == 0) {
#if defined(_WIN32)
        ::WSACleanup();
#endif
    }
}

Socket::~Socket() { close(); }

Socket::Socket(Socket&& other) noexcept : mHandle(other.mHandle)
{
    other.mHandle = -1;
}

Socket& Socket::operator=(Socket&& other) noexcept
{
    if (this != &other) {
        close();
        mHandle = other.mHandle;
        other.mHandle = -1;
    }
    return *this;
}

bool Socket::valid() const { return mHandle >= 0; }

void Socket::close()
{
    closeHandle(asHandle(mHandle));
    mHandle = -1;
}

bool Socket::connect(const std::string& host, unsigned short port,
                     int timeoutMs, std::string* error)
{
    close();

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC; // v4 or v6, whichever the host resolves to
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    const std::string service = std::to_string(port);
    addrinfo* results = nullptr;
    const int resolved =
        ::getaddrinfo(host.c_str(), service.c_str(), &hints, &results);
    if (resolved != 0 || !results) {
        if (error)
            *error = "cannot resolve '" + host + "'";
        return false;
    }

    bool connected = false;
    std::string failure;
    for (addrinfo* at = results; at && !connected; at = at->ai_next) {
        const SocketHandle handle =
            ::socket(at->ai_family, at->ai_socktype, at->ai_protocol);
        if (handle == kInvalidSocket) {
            failure = lastError();
            continue;
        }

        // Telemetry is many small writes; Nagle would hold each one back
        // waiting for company and put a debug line 40 ms behind the frame that
        // produced it.
        int one = 1;
        ::setsockopt(handle, IPPROTO_TCP, TCP_NODELAY,
                     reinterpret_cast<const char*>(&one), sizeof(one));
#if defined(SO_NOSIGPIPE)
        // macOS/BSD: without this a write to a closed peer raises SIGPIPE and
        // kills the process. Linux passes MSG_NOSIGNAL per-send instead, and
        // Windows has no such signal.
        ::setsockopt(handle, SOL_SOCKET, SO_NOSIGPIPE,
                     reinterpret_cast<const char*>(&one), sizeof(one));
#endif

        // Non-blocking for the connect alone, so the timeout is ours rather
        // than the platform's minute-plus default, then back to blocking.
        setBlocking(handle, false);
        const int result =
            ::connect(handle, at->ai_addr, static_cast<int>(at->ai_addrlen));
        if (result == 0) {
            connected = true;
        } else if (wouldBlock()) {
            fd_set writable;
            FD_ZERO(&writable);
            FD_SET(handle, &writable);
            timeval timeout{};
            timeout.tv_sec = timeoutMs / 1000;
            timeout.tv_usec = (timeoutMs % 1000) * 1000;
            const int ready = ::select(static_cast<int>(handle) + 1, nullptr,
                                       &writable, nullptr, &timeout);
            if (ready > 0) {
                // select() says writable for both success and refusal; the
                // pending error is what distinguishes them.
                int pending = 0;
#if defined(_WIN32)
                int length = sizeof(pending);
#else
                socklen_t length = sizeof(pending);
#endif
                if (::getsockopt(handle, SOL_SOCKET, SO_ERROR,
                                 reinterpret_cast<char*>(&pending), &length) ==
                        0 &&
                    pending == 0)
                    connected = true;
                else
                    failure = "connection refused";
            } else {
                failure = ready == 0 ? "connection timed out" : lastError();
            }
        } else {
            failure = lastError();
        }

        if (connected) {
            setBlocking(handle, true);
            mHandle = static_cast<long long>(handle);
        } else {
            closeHandle(handle);
        }
    }
    ::freeaddrinfo(results);

    if (!connected && error)
        *error = failure.empty() ? "connect failed" : failure;
    return connected;
}

bool Socket::sendAll(std::string_view data, std::string* error)
{
    if (!valid()) {
        if (error)
            *error = "socket is not connected";
        return false;
    }
    const SocketHandle handle = asHandle(mHandle);
    std::size_t sent = 0;
    while (sent < data.size()) {
        int flags = 0;
#if defined(MSG_NOSIGNAL)
        flags |= MSG_NOSIGNAL; // Linux: the SO_NOSIGPIPE equivalent, per send
#endif
        const auto written =
            ::send(handle, data.data() + sent,
#if defined(_WIN32)
                   static_cast<int>(data.size() - sent),
#else
                   data.size() - sent,
#endif
                   flags);
        if (written > 0) {
            sent += static_cast<std::size_t>(written);
            continue;
        }
        if (written < 0 && wouldBlock())
            continue; // blocking socket, so this is a spurious wakeup
        if (error)
            *error = written == 0 ? "peer closed the connection" : lastError();
        return false;
    }
    return true;
}

bool Socket::receiveSome(std::string& out, int timeoutMs, std::string* error)
{
    if (!valid()) {
        if (error)
            *error = "socket is not connected";
        return false;
    }
    const SocketHandle handle = asHandle(mHandle);

    fd_set readable;
    FD_ZERO(&readable);
    FD_SET(handle, &readable);
    timeval timeout{};
    timeout.tv_sec = timeoutMs / 1000;
    timeout.tv_usec = (timeoutMs % 1000) * 1000;
    const int ready =
        ::select(static_cast<int>(handle) + 1, &readable, nullptr, nullptr,
                 &timeout);
    if (ready == 0)
        return true; // nothing waiting; the normal case, and not an error
    if (ready < 0) {
        if (error)
            *error = lastError();
        return false;
    }

    char buffer[4096];
    const auto read = ::recv(handle, buffer, sizeof(buffer), 0);
    if (read > 0) {
        out.append(buffer, static_cast<std::size_t>(read));
        return true;
    }
    if (read == 0) {
        if (error)
            *error = "peer closed the connection";
        return false;
    }
    if (wouldBlock())
        return true;
    if (error)
        *error = lastError();
    return false;
}

} // namespace eng::telemetry
