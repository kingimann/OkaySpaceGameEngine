#include "okay/Net/ServerQuery.hpp"
#include "okay/Net/Packet.hpp"
#include "okay/Net/UdpSocket.hpp"

#include <chrono>
#include <cstdlib>

#if defined(_WIN32)
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  include <windows.h>
#else
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  include <unistd.h>
#endif

namespace okay {

namespace {
    constexpr std::uint8_t kMsgQuery = 17, kMsgQueryInfo = 18;   // NetworkManager's ids
    constexpr std::uint32_t kLanNonce = 0x80000000u;

    void NapMs(int ms) {
#if defined(_WIN32)
        Sleep((DWORD)ms);
#else
        usleep((useconds_t)ms * 1000);
#endif
    }

    std::vector<std::uint8_t> BuildQuery(std::uint32_t nonce) {
        net::Packet p(kMsgQuery);
        p.Write(std::string(net::kQueryMagic));
        p.Write(nonce);
        std::vector<std::uint8_t> out(p.Data(), p.Data() + p.Size());
        if (out.size() < net::kQueryMinSize) out.resize(net::kQueryMinSize, 0);
        return out;
    }

    std::string Dotted(std::uint32_t a) {
        return std::to_string((a >> 24) & 0xFF) + "." + std::to_string((a >> 16) & 0xFF) + "." +
               std::to_string((a >> 8) & 0xFF) + "." + std::to_string(a & 0xFF);
    }
}

bool SplitHostPort(const std::string& text, std::string& host, std::uint16_t& port, std::uint16_t defaultPort) {
    std::string t = text;
    while (!t.empty() && (t.back() == ' ' || t.back() == '\t' || t.back() == '/')) t.pop_back();
    std::size_t b = 0; while (b < t.size() && (t[b] == ' ' || t[b] == '\t')) ++b;
    t = t.substr(b);
    if (t.empty()) return false;
    const std::size_t colon = t.rfind(':');
    if (colon == std::string::npos) { host = t; port = defaultPort; return true; }
    host = t.substr(0, colon);
    const std::string ps = t.substr(colon + 1);
    if (host.empty() || ps.empty() || ps.size() > 5) return false;
    for (char c : ps) if (c < '0' || c > '9') return false;
    const long v = std::atol(ps.c_str());
    if (v <= 0 || v > 65535) return false;
    port = (std::uint16_t)v;
    return true;
}

std::vector<ServerInfo> QueryServers(const std::vector<QueryTarget>& targets, int timeoutMs, bool lan) {
    using Clock = std::chrono::steady_clock;
    std::vector<ServerInfo> out(targets.size());
    std::vector<net::Endpoint> eps(targets.size());
    std::vector<bool> resolved(targets.size(), false);
    std::vector<Clock::time_point> sentAt(targets.size());
    for (std::size_t i = 0; i < targets.size(); ++i) { out[i].host = targets[i].host; out[i].port = targets[i].port; }
    if (!net::Startup()) return out;
    {
        net::UdpSocket sock;
        if (!sock.Open()) { net::Shutdown(); return out; }
        if (lan) sock.EnableBroadcast();
        for (std::size_t i = 0; i < targets.size(); ++i)
            resolved[i] = !targets[i].host.empty() && targets[i].port &&
                          net::UdpSocket::Resolve(targets[i].host, targets[i].port, eps[i]);

        Clock::time_point lanSent{};
        auto sendAll = [&](bool onlyUnanswered) {
            for (std::size_t i = 0; i < targets.size(); ++i) {
                if (!resolved[i] || (onlyUnanswered && out[i].responded)) continue;
                auto q = BuildQuery((std::uint32_t)i + 1);
                sentAt[i] = Clock::now();
                sock.SendTo(eps[i], q.data(), q.size());
            }
            if (lan && !onlyUnanswered) {
                auto q = BuildQuery(kLanNonce);
                lanSent = Clock::now();
                for (std::uint16_t p = net::kLanPortFirst; p <= net::kLanPortLast; ++p) {
                    sock.SendTo(net::Endpoint{0xFFFFFFFFu, p}, q.data(), q.size());   // the LAN
                    sock.SendTo(net::Endpoint{0x7F000001u, p}, q.data(), q.size());   // this machine
                }
            }
        };
        sendAll(false);
        const Clock::time_point start = Clock::now();
        bool resent = false;
        std::vector<std::uint8_t> buf(net::kMaxDatagram);
        for (;;) {
            const int elapsed = (int)std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
            if (elapsed >= timeoutMs) break;
            if (!resent && elapsed >= timeoutMs / 3) { sendAll(true); resent = true; }   // a lost packet gets a second chance
            bool allIn = !lan;
            for (std::size_t i = 0; i < targets.size() && allIn; ++i) if (resolved[i] && !out[i].responded) allIn = false;
            if (allIn) break;

            net::Endpoint from; int n;
            bool any = false;
            while ((n = sock.RecvFrom(buf.data(), buf.size(), from)) != 0) {
                if (n < 0) break;   // e.g. Windows reports "port unreachable" from a dead server here
                any = true;
                net::Packet p(buf.data(), (std::size_t)n);
                if (p.ReadU8() != kMsgQueryInfo || p.ReadString() != net::kQueryMagic) continue;
                const std::uint32_t nonce = p.ReadU32();
                ServerInfo s;
                if (p.ReadU8() < 1) continue;   // protocol
                s.name = p.ReadString(); s.game = p.ReadString(); s.map = p.ReadString();
                s.players = (int)p.ReadU32(); s.maxPlayers = (int)p.ReadU32();
                const std::uint8_t f = p.ReadU8();
                if (!p.Ok()) continue;
                s.password = (f & net::kQueryPassword) != 0; s.dedicated = (f & net::kQueryDedicated) != 0;
                s.auth = (f & net::kQueryAuth) != 0;         s.encrypted = (f & net::kQueryEncrypted) != 0;
                s.responded = true;
                const Clock::time_point now = Clock::now();
                if (nonce == kLanNonce) {
                    s.lan = true; s.host = Dotted(from.address); s.port = from.port;
                    s.pingMs = (int)std::chrono::duration_cast<std::chrono::milliseconds>(now - lanSent).count();
                    // One entry per server: a local server answers both the broadcast and 127.0.0.1.
                    bool dup = false;
                    for (std::size_t k = targets.size(); k < out.size(); ++k)
                        if (out[k].port == s.port && out[k].name == s.name && out[k].game == s.game) {
                            dup = true;
                            if (out[k].host == "127.0.0.1" && s.host != "127.0.0.1") out[k].host = s.host;   // prefer the LAN address
                            break;
                        }
                    if (!dup) out.push_back(s);
                } else if (nonce >= 1 && nonce <= targets.size()) {
                    ServerInfo& t = out[nonce - 1];
                    if (t.responded) continue;
                    s.host = t.host; s.port = t.port;
                    s.pingMs = (int)std::chrono::duration_cast<std::chrono::milliseconds>(now - sentAt[nonce - 1]).count();
                    t = s;
                }
            }
            if (!any) NapMs(2);
        }
    }
    net::Shutdown();
    return out;
}

std::string LocalNetworkAddress() {
    if (!net::Startup()) return {};
    std::string out;
#if defined(_WIN32)
    SOCKET s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    const bool ok = s != INVALID_SOCKET;
#else
    int s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    const bool ok = s >= 0;
#endif
    if (ok) {
        // "Connecting" a UDP socket picks the outgoing interface without sending anything.
        sockaddr_in to{}; to.sin_family = AF_INET; to.sin_port = htons(53);
        to.sin_addr.s_addr = htonl(0x08080808u);
        if (::connect(s, reinterpret_cast<sockaddr*>(&to), sizeof(to)) == 0) {
            sockaddr_in me{};
#if defined(_WIN32)
            int len = sizeof(me);
#else
            socklen_t len = sizeof(me);
#endif
            if (::getsockname(s, reinterpret_cast<sockaddr*>(&me), &len) == 0 && me.sin_addr.s_addr != 0)
                out = Dotted(ntohl(me.sin_addr.s_addr));
        }
#if defined(_WIN32)
        closesocket(s);
#else
        ::close(s);
#endif
    }
    net::Shutdown();
    return out;
}

} // namespace okay
