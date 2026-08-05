#pragma once

#include <string>
#include <string_view>

namespace eng::telemetry {

// A blocking TCP client socket, on the three platforms this ships to.
//
// The whole platform difference is here and nowhere else: Winsock needs
// WSAStartup, uses SOCKET/closesocket/WSAGetLastError and has its own error
// numbering; BSD sockets use int/close/errno. macOS additionally lacks
// MSG_NOSIGNAL and needs SO_NOSIGPIPE instead, which is the detail that
// silently kills a process on a broken pipe if you miss it.
//
// Deliberately blocking, with timeouts, and only ever touched from the
// telemetry publisher thread. A non-blocking state machine would be the right
// answer for a game's netcode; for a debug channel that writes a batch once
// every few frames, it is complexity with nothing to buy.
class Socket {
public:
    Socket() = default;
    ~Socket();
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    Socket(Socket&& other) noexcept;
    Socket& operator=(Socket&& other) noexcept;

    // Resolves `host` and connects, giving up after `timeoutMs`. A timeout is
    // not optional here: the default TCP connect timeout is over a minute on
    // every platform, and a debug sink that hangs a thread for a minute at
    // startup because nothing is listening is worse than one that never
    // connects.
    bool connect(const std::string& host, unsigned short port,
                 int timeoutMs = 500, std::string* error = nullptr);
    void close();
    bool valid() const;

    // Writes the whole buffer, looping over partial sends. False on any error;
    // the caller's contract is to close and retry later, never to retry a
    // partial write, because it cannot know how much of the last command went.
    bool sendAll(std::string_view data, std::string* error = nullptr);

    // Appends whatever has arrived to `out`. Returns false only on a real
    // error or a closed peer -- a timeout with nothing to read is success with
    // nothing appended, because that is the normal state of a socket whose
    // replies we do not wait for.
    bool receiveSome(std::string& out, int timeoutMs = 0,
                     std::string* error = nullptr);

    // Winsock needs a process-wide startup/shutdown pair. Reference counted and
    // thread safe, so a caller never has to know which platform it is on.
    static bool platformInit(std::string* error = nullptr);
    static void platformShutdown();

private:
    // Deliberately not the platform type: SOCKET is a 64-bit unsigned handle on
    // Windows and a small int elsewhere, and putting either in this header
    // would drag <winsock2.h> into everything that includes it. -1 is invalid
    // on both.
    long long mHandle = -1;
};

} // namespace eng::telemetry
