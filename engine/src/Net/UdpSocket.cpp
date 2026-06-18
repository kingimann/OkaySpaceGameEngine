#include "okay/Net/UdpSocket.hpp"

#include <cstdio>

#if defined(_WIN32)
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  if defined(_MSC_VER)
#    pragma comment(lib, "ws2_32.lib")
#  endif
   using socklen_t = int;
#else
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  include <netdb.h>
#  include <fcntl.h>
#  include <unistd.h>
#  include <errno.h>
#endif

namespace okay::net {

namespace {
    int g_initCount = 0;

    bool WouldBlock() {
#if defined(_WIN32)
        return WSAGetLastError() == WSAEWOULDBLOCK;
#else
        return errno == EWOULDBLOCK || errno == EAGAIN;
#endif
    }
}

std::string Endpoint::ToString() const {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%u.%u.%u.%u:%u",
                  (address >> 24) & 0xFF, (address >> 16) & 0xFF,
                  (address >> 8) & 0xFF, address & 0xFF, port);
    return buf;
}

bool Startup() {
#if defined(_WIN32)
    if (g_initCount++ == 0) {
        WSADATA d;
        if (WSAStartup(MAKEWORD(2, 2), &d) != 0) { g_initCount = 0; return false; }
    }
#else
    ++g_initCount;
#endif
    return true;
}

void Shutdown() {
#if defined(_WIN32)
    if (g_initCount > 0 && --g_initCount == 0) WSACleanup();
#else
    if (g_initCount > 0) --g_initCount;
#endif
}

UdpSocket::~UdpSocket() { Close(); }

UdpSocket& UdpSocket::operator=(UdpSocket&& o) noexcept {
    if (this != &o) { Close(); m_handle = o.m_handle; o.m_handle = kInvalid; }
    return *this;
}

bool UdpSocket::Open() {
    Close();
    m_handle = static_cast<Handle>(::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP));
    if (m_handle == kInvalid) return false;

#if defined(_WIN32)
    u_long mode = 1;
    if (ioctlsocket(static_cast<SOCKET>(m_handle), FIONBIO, &mode) != 0) { Close(); return false; }
#else
    int flags = fcntl(m_handle, F_GETFL, 0);
    if (flags < 0 || fcntl(m_handle, F_SETFL, flags | O_NONBLOCK) < 0) { Close(); return false; }
#endif
    return true;
}

bool UdpSocket::Bind(std::uint16_t port) {
    if (!IsOpen()) return false;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    return ::bind(static_cast<int>(m_handle),
                  reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0;
}

void UdpSocket::Close() {
    if (m_handle == kInvalid) return;
#if defined(_WIN32)
    closesocket(static_cast<SOCKET>(m_handle));
#else
    ::close(m_handle);
#endif
    m_handle = kInvalid;
}

std::uint16_t UdpSocket::LocalPort() const {
    if (!IsOpen()) return 0;
    sockaddr_in addr{};
    socklen_t len = sizeof(addr);
    if (::getsockname(static_cast<int>(m_handle),
                      reinterpret_cast<sockaddr*>(&addr), &len) != 0) return 0;
    return ntohs(addr.sin_port);
}

bool UdpSocket::SendTo(const Endpoint& to, const void* data, std::size_t size) {
    if (!IsOpen()) return false;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(to.address);
    addr.sin_port = htons(to.port);
    int sent = ::sendto(static_cast<int>(m_handle),
                        reinterpret_cast<const char*>(data),
                        static_cast<int>(size), 0,
                        reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    return sent == static_cast<int>(size);
}

int UdpSocket::RecvFrom(void* buffer, std::size_t capacity, Endpoint& from) {
    if (!IsOpen()) return -1;
    sockaddr_in addr{};
    socklen_t len = sizeof(addr);
    int got = ::recvfrom(static_cast<int>(m_handle),
                         reinterpret_cast<char*>(buffer),
                         static_cast<int>(capacity), 0,
                         reinterpret_cast<sockaddr*>(&addr), &len);
    if (got < 0) return WouldBlock() ? 0 : -1;
    from.address = ntohl(addr.sin_addr.s_addr);
    from.port    = ntohs(addr.sin_port);
    return got;
}

bool UdpSocket::Resolve(const std::string& host, std::uint16_t port, Endpoint& out) {
    addrinfo hints{};
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    addrinfo* res = nullptr;
    if (::getaddrinfo(host.c_str(), nullptr, &hints, &res) != 0 || !res) return false;
    auto* a = reinterpret_cast<sockaddr_in*>(res->ai_addr);
    out.address = ntohl(a->sin_addr.s_addr);
    out.port    = port;
    ::freeaddrinfo(res);
    return true;
}

} // namespace okay::net
