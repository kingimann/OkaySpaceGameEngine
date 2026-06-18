#pragma once
#include <cstdint>
#include <string>

namespace okay::net {

/// Identifies a remote host (resolved address + port). Compared by value so it
/// can key the server's table of connected clients.
struct Endpoint {
    std::uint32_t address = 0; // IPv4, host byte order
    std::uint16_t port    = 0;

    bool operator==(const Endpoint& o) const {
        return address == o.address && port == o.port;
    }
    std::string ToString() const;
};

struct EndpointHash {
    std::size_t operator()(const Endpoint& e) const {
        return (std::size_t(e.address) << 16) ^ e.port;
    }
};

/// Initialize/teardown the platform networking stack (WSAStartup on Windows,
/// a no-op elsewhere). Safe to nest; reference-counted.
bool Startup();
void Shutdown();

/// A non-blocking UDP socket that works on both POSIX and Windows.
class UdpSocket {
public:
    UdpSocket() = default;
    ~UdpSocket();
    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;
    UdpSocket(UdpSocket&& o) noexcept { *this = std::move(o); }
    UdpSocket& operator=(UdpSocket&& o) noexcept;

    /// Create the socket and switch it to non-blocking mode.
    bool Open();
    /// Bind to a local port (server side). Port 0 picks an ephemeral port.
    bool Bind(std::uint16_t port);
    void Close();
    bool IsOpen() const { return m_handle != kInvalid; }
    /// The local port actually bound (useful after Bind(0) picks one), or 0.
    std::uint16_t LocalPort() const;

    /// Send to a destination; returns true if the datagram was queued.
    bool SendTo(const Endpoint& to, const void* data, std::size_t size);
    /// Receive one datagram. Returns bytes read, 0 if none pending, -1 on error.
    int  RecvFrom(void* buffer, std::size_t capacity, Endpoint& from);

    /// Resolve "host:port" / "host" into an Endpoint (host may be a dotted IP).
    static bool Resolve(const std::string& host, std::uint16_t port, Endpoint& out);

private:
#if defined(_WIN32)
    using Handle = std::uintptr_t;
    static const Handle kInvalid = static_cast<Handle>(~0ull);
#else
    using Handle = int;
    static const Handle kInvalid = -1;
#endif
    Handle m_handle = kInvalid;
};

} // namespace okay::net
