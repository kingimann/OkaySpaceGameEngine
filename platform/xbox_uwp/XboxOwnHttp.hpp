#pragma once
// Our own HTTP/1.1 client as an OPTIONAL content transport for the Xbox UWP build. EXPERIMENTAL, UNVERIFIED on Windows and on a console.
//
// Written from RFC 9112 (HTTP/1.1) as already implemented by okay::ownhttp (engine/include/okay/Platform/OwnHttp.hpp, our own code) and from
// the Microsoft Winsock documentation (getaddrinfo, socket, connect, send, recv, setsockopt SO_RCVTIMEO / SO_SNDTIMEO, closesocket). Nothing was
// copied from another project. Header only, no <mutex> / <thread> (CLAUDE.md rule 21a).
//
// Choice, made at compile time by the OKAY_XBOX_HTTP_OWN define (CMake -DOKAY_XBOX_HTTP=own, default windows-web-http):
//   windows-web-http (default)  Windows.Web.Http through app/xbox_winhttp.hpp. Does https. The tested, expected path.
//   own                         okay::ownhttp over a Winsock TCP stream. PLAIN http:// ONLY: we have no TLS of our own yet (okay::ownhttp's
//                               TLS is the system OpenSSL loaded at run time, which does not exist in a UWP app), so an https:// URL is
//                               REFUSED with a plain message and never sent in the clear. Good for a LAN or a local test server, not for a CDN.
// The pure parts (the decision, the connector, the Client adapter) are tested on Linux with an in-memory TCP (tests/test_xbox_uwp.cpp);
// the Winsock dialer at the bottom compiles only under _WIN32 and has never been run.
#include "XboxHttp.hpp"
#include "okay/Platform/OwnHttp.hpp"

#include <memory>
#include <string>
#include <utility>

namespace okay::xbox::ownnet {

enum class Backend { WindowsWebHttp, OwnPlain };
inline const char* BackendName(Backend b) { return b == Backend::OwnPlain ? "own okay::ownhttp over Winsock (plain http only)" : "Windows.Web.Http"; }

/// Which transport a request goes through. `ownCompiled` = built with OKAY_XBOX_HTTP_OWN, `webHttpCompiled` = the shell's Windows.Web.Http glue is in.
struct Decision { Backend use = Backend::WindowsWebHttp; bool refuse = false; std::string why; };
inline Decision Decide(bool ownCompiled, bool webHttpCompiled, const std::string& scheme) {
    Decision d;
    if (ownCompiled && scheme == "http") { d.use = Backend::OwnPlain; return d; }                         // our client, plain http
    if (webHttpCompiled) { d.use = Backend::WindowsWebHttp; return d; }                                   // https, or no own client built
    d.refuse = true;
    d.why = scheme == "https" ? "https needs Windows.Web.Http and this build has only the own client, which has no TLS (plain http only)"
                              : "no HTTP client is built into this program";
    return d;
}

/// How a TCP connection is made. The Winsock one is at the bottom; tests use an in-memory one.
class Dialer {
public:
    virtual ~Dialer() = default;
    virtual std::unique_ptr<ownhttp::Stream> Dial(const std::string& host, int port, ownhttp::Clock::time_point deadline, ownhttp::Err& e, std::string& msg) = 0;
};

/// okay::ownhttp::Connector for plain http. An https URL is refused (Err::Unsupported), it is never sent without TLS.
class PlainConnector : public ownhttp::Connector {
public:
    explicit PlainConnector(Dialer& d) : dialer_(d) {}
    std::unique_ptr<ownhttp::Stream> Open(const ownhttp::Url& u, ownhttp::Clock::time_point deadline, ownhttp::Err& e, std::string& msg) override {
        if (u.Https()) { e = ownhttp::Err::Unsupported; msg = "this build has no TLS of its own: https is refused (use the Windows.Web.Http transport)"; return nullptr; }
        return dialer_.Dial(u.host, u.port, deadline, e, msg);
    }
private:
    Dialer& dialer_;
};

/// The pure driver's http::Client over okay::ownhttp. Redirects are followed inside Fetch (never https -> http).
class OwnHttpClient : public http::Client {
public:
    explicit OwnHttpClient(Dialer& d, int totalMs = 60 * 1000) : conn_(d), totalMs_(totalMs) {}
    http::ClientResult Send(const http::Request& r, const http::Sink& s) override {
        http::ClientResult out;
        ownhttp::FetchRequest fr; fr.method = "GET"; fr.url = r.url;
        for (const http::Header& h : r.headers) {
            const std::string n = http::Lower(h.name);
            if (n == "accept-encoding" || n == "host" || n == "connection" || n == "content-length") continue;   // okay::ownhttp sets these itself (it sends Accept-Encoding: identity)
            fr.headerLines.push_back(h.name + ": " + h.value);
        }
        ownhttp::Limits lim; lim.maxBodyBytes = 4ull * 1024 * 1024 * 1024; lim.maxRedirects = 5;
        ownhttp::FetchHooks hooks; bool gotHead = false, stopped = false;
        hooks.onHead = [&](const ownhttp::Response& resp) {
            gotHead = true;
            http::Head h; h.status = resp.status;
            for (const ownhttp::Header& x : resp.headers) h.headers.push_back(http::Header{x.name, x.value});
            if (s.onHead && !s.onHead(h)) { stopped = true; return false; }
            return true;
        };
        hooks.onData = [&](const char* p, size_t n) {
            if (s.onData && !s.onData(reinterpret_cast<const std::uint8_t*>(p), n)) { stopped = true; return false; }
            return true;
        };
        const ownhttp::FetchResult res = ownhttp::Fetch(conn_, fr, lim, totalMs_, hooks);
        out.reached = gotHead || res.ok;
        out.aborted = stopped || res.err == ownhttp::Err::Aborted;
        if (!res.ok && !out.aborted) out.error = res.message.empty() ? ownhttp::ErrName(res.err) : res.message;
        return out;
    }
private:
    PlainConnector conn_; int totalMs_;
};

} // namespace okay::xbox::ownnet

// ---- Winsock dialer: UWP / Windows only, never compiled on Linux, never run ----
#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
namespace okay::xbox::ownnet {

class WinsockStream : public ownhttp::Stream {
public:
    WinsockStream(SOCKET s, ownhttp::Clock::time_point deadline) : s_(s), deadline_(deadline) {}
    ~WinsockStream() override { if (s_ != INVALID_SOCKET) closesocket(s_); }
    long Read(void* buf, size_t n, ownhttp::Err& e) override {
        if (ownhttp::Clock::now() >= deadline_) { e = ownhttp::Err::Timeout; return -1; }
        const int r = recv(s_, static_cast<char*>(buf), static_cast<int>(n > 65536 ? 65536 : n), 0);
        if (r < 0) { e = WSAGetLastError() == WSAETIMEDOUT ? ownhttp::Err::Timeout : ownhttp::Err::Io; return -1; }
        return r;
    }
    bool Write(const void* buf, size_t n, ownhttp::Err& e) override {
        const char* p = static_cast<const char*>(buf);
        while (n > 0) {
            const int w = send(s_, p, static_cast<int>(n > 65536 ? 65536 : n), 0);
            if (w <= 0) { e = ownhttp::Err::Io; return false; }
            p += w; n -= static_cast<size_t>(w);
        }
        return true;
    }
private:
    SOCKET s_; ownhttp::Clock::time_point deadline_;
};

class WinsockDialer : public Dialer {
public:
    std::unique_ptr<ownhttp::Stream> Dial(const std::string& host, int port, ownhttp::Clock::time_point deadline, ownhttp::Err& e, std::string& msg) override {
        WSADATA wd; if (WSAStartup(MAKEWORD(2, 2), &wd) != 0) { e = ownhttp::Err::Connect; msg = "WSAStartup failed"; return nullptr; }
        addrinfo hints = {}; hints.ai_family = AF_UNSPEC; hints.ai_socktype = SOCK_STREAM; hints.ai_protocol = IPPROTO_TCP;
        addrinfo* list = nullptr;
        if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &list) != 0 || !list) { e = ownhttp::Err::Connect; msg = "could not find " + host; return nullptr; }
        SOCKET s = INVALID_SOCKET;
        for (addrinfo* a = list; a; a = a->ai_next) {
            s = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
            if (s == INVALID_SOCKET) continue;
            const DWORD ms = 15000; setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&ms), sizeof ms); setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&ms), sizeof ms);
            if (connect(s, a->ai_addr, static_cast<int>(a->ai_addrlen)) == 0) break;
            closesocket(s); s = INVALID_SOCKET;
        }
        freeaddrinfo(list);
        if (s == INVALID_SOCKET) { e = ownhttp::Err::Connect; msg = "could not connect to " + host; return nullptr; }
        return std::make_unique<WinsockStream>(s, deadline);
    }
};

} // namespace okay::xbox::ownnet
#endif
