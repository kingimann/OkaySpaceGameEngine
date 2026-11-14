#pragma once
// ---------------------------------------------------------------------------
// okay::ownhttp transports (POSIX): plain TCP, an HTTP CONNECT proxy tunnel, and
// TLS through the system OpenSSL (libssl) loaded at RUN TIME with dlopen.
//
//  - Nothing is linked against OpenSSL and no OpenSSL header is needed. If libssl
//    (1.1.0 or newer) cannot be loaded, https requests are REFUSED (Err::Tls) and
//    the caller falls back to the spawned curl exactly as before. There is no
//    insecure fallback and no option that turns certificate or host name checks off.
//  - TLS 1.2 or newer, SNI, certificate chain verified against the system store or
//    SSL_CERT_FILE / CURL_CA_BUNDLE / SSL_CERT_DIR, host name (or IP) checked with
//    SSL_set1_host / X509_VERIFY_PARAM_set1_ip_asc.
//  - Proxies: HTTPS_PROXY / HTTP_PROXY (and lower case) with CONNECT, NO_PROXY.
//    A proxy URL we cannot honour (https:// proxy, credentials) is an error, never
//    a silent direct connection.
//  - macOS: this header compiles there (TCP, proxy) but TLS reports "not available";
//    see docs/own_http_client.md.
// The pure HTTP logic is in OwnHttp.hpp.
// ---------------------------------------------------------------------------
#include "okay/Platform/OwnHttp.hpp"

#if !defined(_WIN32)
#include <arpa/inet.h>
#include <cerrno>
#include <climits>
#include <functional>
#include <csignal>
#include <cstdlib>
#include <dlfcn.h>
#include <fcntl.h>
#include <map>
#include <mutex>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <pthread.h>
#include <condition_variable>
#include <thread>
#include <ctime>
#include <memory>
#include <type_traits>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

namespace okay::ownhttp {

using EnvFn = std::function<const char*(const char*)>;

struct NetOptions {
    int connectMs = 8000;       ///< per address
    int ioMs      = 30000;      ///< longest silence on a read or write
    int totalMs   = 15 * 60 * 1000;   ///< whole request including redirects
    int resolveMs = 8000;       ///< name resolution (a helper thread; the call gives up after this)
    bool acceptGzip = false;    ///< send Accept-Encoding: gzip, deflate and decode the answer (in memory, capped)
    /// Keep finished connections (this thread only, at most 8, each idle at most 15 s) and reuse them for the next
    /// request to the same scheme, host, port, proxy and CA setting. Off by default for the raw client; the NativeHttp
    /// entry points turn it on. See FetchRequest::keepAlive for the safety rules.
    bool keepAlive = false;
    std::shared_ptr<CookieJar> cookieJar;   ///< null = no cookies (default). The jar sees every hop of the call.
    Limits limits;
    // Test and embedding overrides; empty means "decide from the environment".
    std::string caFile, caDir;
    std::vector<std::string> libsslNames;
    /// Polled about every 200 ms while we wait for the network (connect, TLS, read, write). Return true to stop:
    /// the call ends with Err::Aborted. Lets a UI cancel a download even when the server is silent.
    std::function<bool()> cancel;
    EnvFn env;                  ///< environment lookup (default getenv)
    const char* Env(const char* n) const { const char* v = env ? env(n) : std::getenv(n); return (v && *v) ? v : nullptr; }
};

namespace detail {

inline int MsLeft(Clock::time_point deadline) {
    auto d = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
    return d < 0 ? 0 : (d > INT_MAX ? INT_MAX : int(d));
}

/// Wait for `ev` on fd, bounded by the silence limit and the overall deadline.
inline bool Wait(int fd, short ev, int ioMs, Clock::time_point deadline, Err& e, const std::function<bool()>* cancel = nullptr) {
    int silent = ioMs;   // silence still allowed
    for (;;) {
        if (cancel && *cancel && (*cancel)()) { e = Err::Aborted; return false; }
        int left = MsLeft(deadline);
        if (left <= 0) { e = Err::Timeout; return false; }
        int slice = std::min(left, silent);
        if (cancel && *cancel) slice = std::min(slice, 200);
        struct pollfd p; p.fd = fd; p.events = ev; p.revents = 0;
        int r = ::poll(&p, 1, slice);
        if (r > 0) return true;
        if (r == 0) { silent -= slice; if (silent <= 0) { e = Err::Timeout; return false; } continue; }
        if (errno != EINTR) { e = Err::Io; return false; }
    }
}

inline void SetNonBlocking(int fd) {
    int fl = ::fcntl(fd, F_GETFL, 0);
    if (fl >= 0) ::fcntl(fd, F_SETFL, fl | O_NONBLOCK);
    int fdfl = ::fcntl(fd, F_GETFD, 0);
    if (fdfl >= 0) ::fcntl(fd, F_SETFD, fdfl | FD_CLOEXEC);
}

/// Blocks SIGPIPE on this thread while we write to a socket through a library that
/// uses write(2) (OpenSSL), and swallows a SIGPIPE we caused.
struct SigpipeGuard {
    sigset_t old; bool blocked = false;
    SigpipeGuard() { sigset_t s; sigemptyset(&s); sigaddset(&s, SIGPIPE); blocked = pthread_sigmask(SIG_BLOCK, &s, &old) == 0; }
    ~SigpipeGuard() {
        if (!blocked) return;
#if defined(__linux__)
        sigset_t s; sigemptyset(&s); sigaddset(&s, SIGPIPE);
        if (!sigismember(&old, SIGPIPE)) { struct timespec z = {0, 0}; while (::sigtimedwait(&s, nullptr, &z) > 0) {} }
#endif
        pthread_sigmask(SIG_SETMASK, &old, nullptr);
    }
};

inline bool ParseIPv4(const std::string& s, uint32_t& out) {
    uint32_t v = 0; int parts = 0; size_t i = 0;
    while (i <= s.size()) {
        size_t j = s.find('.', i); if (j == std::string::npos) j = s.size();
        if (j == i || j - i > 3) return false;
        uint32_t n = 0;
        for (size_t k = i; k < j; ++k) { if (s[k] < '0' || s[k] > '9') return false; n = n * 10 + uint32_t(s[k] - '0'); }
        if (n > 255) return false;
        v = (v << 8) | n; ++parts;
        if (j == s.size()) break;
        i = j + 1;
    }
    if (parts != 4) return false;
    out = v; return true;
}

inline bool IsIpLiteral(const std::string& host) {
    unsigned char b[16];
    return ::inet_pton(AF_INET, host.c_str(), b) == 1 || ::inet_pton(AF_INET6, host.c_str(), b) == 1;
}

} // namespace detail

// ----------------------------------------------------------------------------
// TCP
// ----------------------------------------------------------------------------
struct TcpStream : Stream {
    int fd = -1;
    int ioMs = 30000;
    Clock::time_point deadline;
    const std::function<bool()>* cancel = nullptr;   ///< NetOptions::cancel (outlives the stream)
    TcpStream() = default;
    TcpStream(const TcpStream&) = delete;
    TcpStream& operator=(const TcpStream&) = delete;
    ~TcpStream() override { if (fd >= 0) ::close(fd); }
    void Rebind(Clock::time_point d, int io, const std::function<bool()>* c) override { deadline = d; ioMs = io; cancel = c; }
    /// An idle connection must have nothing to read: readable means the peer closed it (EOF), reset it, or sent
    /// something nobody asked for. Any of those means "do not reuse".
    bool StillUsable() override {
        if (fd < 0) return false;
        struct pollfd p; p.fd = fd; p.events = POLLIN; p.revents = 0;
        int r;
        do { r = ::poll(&p, 1, 0); } while (r < 0 && errno == EINTR);
        return r == 0;
    }
    long Read(void* b, size_t n, Err& e) override {
        for (;;) {
            ssize_t r = ::recv(fd, b, n, 0);
            if (r >= 0) return long(r);
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) { if (!detail::Wait(fd, POLLIN, ioMs, deadline, e, cancel)) return -1; continue; }
            e = Err::Io; return -1;
        }
    }
    bool Write(const void* b, size_t n, Err& e) override {
        const char* p = static_cast<const char*>(b);
        while (n) {
            ssize_t r = ::send(fd, p, n, MSG_NOSIGNAL);
            if (r >= 0) { p += r; n -= size_t(r); continue; }
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) { if (!detail::Wait(fd, POLLOUT, ioMs, deadline, e, cancel)) return false; continue; }
            e = Err::Io; return false;
        }
        return true;
    }
};

/// getaddrinfo on a helper thread so a dead resolver cannot stall the caller past `resolveMs`, the overall
/// deadline or a cancel. A resolver that never answers leaves its helper thread behind (it frees its own result).
/// IP literals are resolved directly (no lookup happens).
inline bool Resolve(const std::string& host, int port, const NetOptions& o, Clock::time_point deadline,
                    struct addrinfo*& out, Err& e, std::string& msg) {
    struct addrinfo hints; std::memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC; hints.ai_socktype = SOCK_STREAM; hints.ai_flags = AI_NUMERICSERV;
    out = nullptr;
    const std::string svc = std::to_string(port);
    auto fail = [&](int gai) { e = Err::Connect; msg = std::string("cannot resolve host: ") + ::gai_strerror(gai); return false; };
    if (detail::IsIpLiteral(host)) {
        hints.ai_flags |= AI_NUMERICHOST;
        int gai = ::getaddrinfo(host.c_str(), svc.c_str(), &hints, &out);
        if (gai != 0 || !out) return fail(gai);
        return true;
    }
    struct State { std::mutex mu; bool done = false, abandoned = false; int gai = 0; struct addrinfo* res = nullptr; };
    auto st = std::make_shared<State>();
    try {
        std::thread([st, host, svc, hints] {
            struct addrinfo* r = nullptr;
            int g = ::getaddrinfo(host.c_str(), svc.c_str(), &hints, &r);
            std::lock_guard<std::mutex> lk(st->mu);
            if (st->abandoned) { if (r) ::freeaddrinfo(r); return; }
            st->gai = g; st->res = r; st->done = true;
        }).detach();
    } catch (...) {   // no thread: resolve here, unbounded, as before
        int gai = ::getaddrinfo(host.c_str(), svc.c_str(), &hints, &out);
        if (gai != 0 || !out) return fail(gai);
        return true;
    }
    const Clock::time_point limit = std::min(deadline, Clock::now() + std::chrono::milliseconds(o.resolveMs > 0 ? o.resolveMs : 1 << 30));
    for (;;) {
        {
            std::lock_guard<std::mutex> lk(st->mu);
            if (st->done) {
                if (st->gai != 0 || !st->res) return fail(st->gai != 0 ? st->gai : EAI_NONAME);
                out = st->res; st->res = nullptr; return true;
            }
            const bool cancelled = o.cancel && o.cancel();
            if (cancelled || Clock::now() >= limit) {
                st->abandoned = true;
                e = cancelled ? Err::Aborted : Err::Timeout;
                msg = cancelled ? "cancelled by the caller" : "name resolution timed out";
                return false;
            }
        }
        struct timespec ts = {0, 20 * 1000 * 1000};
        ::nanosleep(&ts, nullptr);
    }
}

inline std::unique_ptr<TcpStream> TcpConnect(const std::string& host, int port, const NetOptions& o,
                                             Clock::time_point deadline, Err& e, std::string& msg) {
    struct addrinfo* res = nullptr;
    if (!Resolve(host, port, o, deadline, res, e, msg)) return nullptr;
    std::string lastMsg = "no address";
    for (struct addrinfo* ai = res; ai; ai = ai->ai_next) {
        if (detail::MsLeft(deadline) <= 0) { e = Err::Timeout; msg = "total time limit reached"; ::freeaddrinfo(res); return nullptr; }
        int fd = ::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0) { lastMsg = "socket failed"; continue; }
        detail::SetNonBlocking(fd);
        int rc = ::connect(fd, ai->ai_addr, ai->ai_addrlen);
        bool ok = rc == 0;
        if (!ok && errno == EINPROGRESS) {
            Err we = Err::None;
            if (detail::Wait(fd, POLLOUT, o.connectMs, deadline, we, &o.cancel)) {
                int soerr = 0; socklen_t sl = sizeof soerr;
                ::getsockopt(fd, SOL_SOCKET, SO_ERROR, &soerr, &sl);
                ok = soerr == 0;
                if (!ok) lastMsg = std::string("connect failed: ") + std::strerror(soerr);
            } else if (we == Err::Aborted) { ::close(fd); ::freeaddrinfo(res); e = Err::Aborted; msg = "cancelled by the caller"; return nullptr; }
            else lastMsg = "connect timed out";
        } else if (!ok) lastMsg = std::string("connect failed: ") + std::strerror(errno);
        if (ok) {
            int one = 1; ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
#ifdef SO_NOSIGPIPE
            ::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
            ::freeaddrinfo(res);
            auto s = std::make_unique<TcpStream>();
            s->fd = fd; s->ioMs = o.ioMs; s->deadline = deadline; s->cancel = &o.cancel;
            return s;
        }
        ::close(fd);
    }
    ::freeaddrinfo(res);
    e = Err::Connect; msg = lastMsg;
    return nullptr;
}

// ----------------------------------------------------------------------------
// Proxy settings
// ----------------------------------------------------------------------------
struct Proxy { bool use = false; std::string host; int port = 0; };

/// NO_PROXY: comma separated host names (suffix match, optional leading '.' or "*."),
/// "*", IPv4 CIDR blocks, exact IPs. Ports in entries are ignored.
inline bool HostBypassesProxy(const std::string& hostIn, const std::string& list) {
    const std::string host = detail::Lower(hostIn);
    uint32_t hip = 0; const bool hostIsV4 = detail::ParseIPv4(host, hip);
    size_t i = 0;
    while (i <= list.size()) {
        size_t j = list.find(',', i); if (j == std::string::npos) j = list.size();
        std::string ent = detail::Lower(detail::Trim(list.substr(i, j - i)));
        i = j + 1;
        if (ent.empty()) continue;
        if (ent == "*") return true;
        size_t slash = ent.find('/');
        if (slash != std::string::npos) {
            uint32_t net; int bits = -1;
            std::string b = ent.substr(slash + 1);
            if (!b.empty() && b.size() <= 2 && std::all_of(b.begin(), b.end(), [](char c) { return c >= '0' && c <= '9'; })) bits = std::atoi(b.c_str());
            if (hostIsV4 && bits >= 0 && bits <= 32 && detail::ParseIPv4(ent.substr(0, slash), net)) {
                uint32_t mask = bits == 0 ? 0 : (0xFFFFFFFFu << (32 - bits));
                if ((hip & mask) == (net & mask)) return true;
            }
            continue;
        }
        size_t colon = ent.rfind(':');
        if (colon != std::string::npos && ent.find(':') == colon && ent[0] != '[') ent.erase(colon);   // host:port
        if (ent.compare(0, 2, "*.") == 0) ent.erase(0, 1);
        if (!ent.empty() && ent[0] == '.') {
            if (host.size() > ent.size() && host.compare(host.size() - ent.size(), ent.size(), ent) == 0) return true;
            continue;
        }
        if (host == ent) return true;
        if (!hostIsV4 && host.size() > ent.size() && host[host.size() - ent.size() - 1] == '.' &&
            host.compare(host.size() - ent.size(), ent.size(), ent) == 0) return true;
    }
    return false;
}

/// Decide the proxy for `u` from the environment. false = the environment names a
/// proxy we cannot honour (the request must not go direct).
inline bool ResolveProxy(const Url& u, const NetOptions& o, Proxy& out, std::string& why) {
    out = Proxy();
    const char* val = u.Https() ? (o.Env("HTTPS_PROXY") ? o.Env("HTTPS_PROXY") : o.Env("https_proxy"))
                                : (o.Env("http_proxy") ? o.Env("http_proxy") : o.Env("HTTP_PROXY"));
    if (!val) return true;
    const char* np = o.Env("NO_PROXY") ? o.Env("NO_PROXY") : o.Env("no_proxy");
    if (np && HostBypassesProxy(u.host, np)) return true;
    std::string s = detail::Trim(val);
    if (detail::IPrefix(s, "http://")) s.erase(0, 7);
    else if (s.find("://") != std::string::npos) { why = "proxy scheme not supported (only http:// proxies)"; return false; }
    while (!s.empty() && s.back() == '/') s.pop_back();
    if (s.find('@') != std::string::npos) { why = "proxy credentials are not supported"; return false; }
    if (s.find('/') != std::string::npos) { why = "bad proxy url"; return false; }
    Url pu;
    if (!ParseUrl("http://" + s, pu, &why)) { why = "bad proxy url: " + why; return false; }
    size_t br = s.rfind(']');
    size_t col = s.rfind(':');
    const bool explicitPort = col != std::string::npos && (br == std::string::npos || col > br);
    out.use = true; out.host = pu.host; out.port = explicitPort ? pu.port : 1080;
    return true;
}

// ----------------------------------------------------------------------------
// TLS through the system OpenSSL, loaded at run time
// ----------------------------------------------------------------------------
struct TlsApi {
    // All OpenSSL objects are opaque to us.
    int      (*OPENSSL_init_ssl)(uint64_t, const void*) = nullptr;
    const void* (*TLS_client_method)() = nullptr;
    void*    (*SSL_CTX_new)(const void*) = nullptr;
    void     (*SSL_CTX_free)(void*) = nullptr;
    long     (*SSL_CTX_ctrl)(void*, int, long, void*) = nullptr;
    void     (*SSL_CTX_set_verify)(void*, int, void*) = nullptr;
    int      (*SSL_CTX_load_verify_locations)(void*, const char*, const char*) = nullptr;
    int      (*SSL_CTX_set_default_verify_paths)(void*) = nullptr;
    void*    (*SSL_new)(void*) = nullptr;
    void     (*SSL_free)(void*) = nullptr;
    int      (*SSL_set_fd)(void*, int) = nullptr;
    long     (*SSL_ctrl)(void*, int, long, void*) = nullptr;
    int      (*SSL_set1_host)(void*, const char*) = nullptr;
    void     (*SSL_set_hostflags)(void*, unsigned) = nullptr;
    void*    (*SSL_get0_param)(void*) = nullptr;
    int      (*X509_VERIFY_PARAM_set1_ip_asc)(void*, const char*) = nullptr;
    int      (*SSL_connect)(void*) = nullptr;
    int      (*SSL_read)(void*, void*, int) = nullptr;
    int      (*SSL_write)(void*, const void*, int) = nullptr;
    int      (*SSL_shutdown)(void*) = nullptr;
    int      (*SSL_get_error)(const void*, int) = nullptr;
    long     (*SSL_get_verify_result)(const void*) = nullptr;
    void*    (*SSL_get_peer_certificate)(const void*) = nullptr;
    void     (*X509_free)(void*) = nullptr;
    unsigned long (*ERR_get_error)() = nullptr;
    void     (*ERR_error_string_n)(unsigned long, char*, size_t) = nullptr;
    void     (*ERR_clear_error)() = nullptr;
    const char* (*X509_verify_cert_error_string)(long) = nullptr;
    void*    handle = nullptr;

    static std::vector<std::string> DefaultNames() {
#if defined(__APPLE__)
        return {};   // not wired on macOS (docs/own_http_client.md)
#else
        return {"libssl.so.3", "libssl.so.1.1", "libssl.so"};
#endif
    }

    /// Load libssl from the first name that opens. Never falls back to anything insecure.
    static const TlsApi* Load(const std::vector<std::string>& names, std::string& why) {
        if (names.empty()) { why = "no system TLS library is wired on this platform"; return nullptr; }
        void* h = nullptr;
        for (const std::string& n : names) { h = ::dlopen(n.c_str(), RTLD_NOW | RTLD_LOCAL); if (h) break; }
        if (!h) { why = "libssl could not be loaded (" + names[0] + (names.size() > 1 ? ", ..." : "") + ")"; return nullptr; }
        TlsApi* a = new TlsApi();
        a->handle = h;
        const char* missing = nullptr;
        auto sym = [&](auto& fp, const char* name, bool required = true) {
            void* p = ::dlsym(h, name);
            if (!p && required && !missing) missing = name;
            fp = reinterpret_cast<std::remove_reference_t<decltype(fp)>>(p);
        };
        sym(a->OPENSSL_init_ssl, "OPENSSL_init_ssl");
        sym(a->TLS_client_method, "TLS_client_method");
        sym(a->SSL_CTX_new, "SSL_CTX_new"); sym(a->SSL_CTX_free, "SSL_CTX_free");
        sym(a->SSL_CTX_ctrl, "SSL_CTX_ctrl"); sym(a->SSL_CTX_set_verify, "SSL_CTX_set_verify");
        sym(a->SSL_CTX_load_verify_locations, "SSL_CTX_load_verify_locations");
        sym(a->SSL_CTX_set_default_verify_paths, "SSL_CTX_set_default_verify_paths");
        sym(a->SSL_new, "SSL_new"); sym(a->SSL_free, "SSL_free"); sym(a->SSL_set_fd, "SSL_set_fd");
        sym(a->SSL_ctrl, "SSL_ctrl"); sym(a->SSL_set1_host, "SSL_set1_host"); sym(a->SSL_set_hostflags, "SSL_set_hostflags");
        sym(a->SSL_get0_param, "SSL_get0_param"); sym(a->X509_VERIFY_PARAM_set1_ip_asc, "X509_VERIFY_PARAM_set1_ip_asc");
        sym(a->SSL_connect, "SSL_connect"); sym(a->SSL_read, "SSL_read"); sym(a->SSL_write, "SSL_write");
        sym(a->SSL_shutdown, "SSL_shutdown"); sym(a->SSL_get_error, "SSL_get_error");
        sym(a->SSL_get_verify_result, "SSL_get_verify_result");
        sym(a->SSL_get_peer_certificate, "SSL_get1_peer_certificate", false);   // 3.x name
        if (!a->SSL_get_peer_certificate) sym(a->SSL_get_peer_certificate, "SSL_get_peer_certificate");   // 1.1.x name
        sym(a->X509_free, "X509_free"); sym(a->ERR_get_error, "ERR_get_error");
        sym(a->ERR_error_string_n, "ERR_error_string_n"); sym(a->ERR_clear_error, "ERR_clear_error");
        sym(a->X509_verify_cert_error_string, "X509_verify_cert_error_string");
        if (missing) { why = std::string("libssl is too old or incomplete (missing ") + missing + "; needs 1.1.0 or newer)"; delete a; ::dlclose(h); return nullptr; }
        if (a->OPENSSL_init_ssl(0, nullptr) != 1) { why = "libssl failed to initialise"; delete a; ::dlclose(h); return nullptr; }
        return a;
    }

    /// The process-wide instance (cached). OKAY_OWN_HTTP_LIBSSL=<path> replaces the search list.
    static const TlsApi* Get(const NetOptions& o, std::string& why) {
        if (!o.libsslNames.empty()) return Load(o.libsslNames, why);   // explicit override, not cached
        static std::mutex mu;
        static const TlsApi* api = nullptr;
        static std::string lastWhy;
        static bool tried = false;
        std::lock_guard<std::mutex> lk(mu);
        if (!tried) {
            tried = true;
            std::vector<std::string> names = DefaultNames();
            if (const char* ov = o.Env("OKAY_OWN_HTTP_LIBSSL")) names = {ov};
            api = Load(names, lastWhy);
        }
        if (!api) why = lastWhy;
        return api;
    }
};

class TlsStream : public Stream {
public:
    TlsStream(const TlsApi* a, std::unique_ptr<TcpStream> t) : api_(a), tcp_(std::move(t)) {}
    void Rebind(Clock::time_point d, int io, const std::function<bool()>* c) override { tcp_->Rebind(d, io, c); }
    bool StillUsable() override { return tcp_->StillUsable(); }
    ~TlsStream() override {
        if (ssl_) {
            detail::SigpipeGuard g;
            api_->SSL_shutdown(ssl_);   // best effort close_notify; the socket is non-blocking
            api_->SSL_free(ssl_);
        }
    }

    static const char* Fail(const TlsApi* a, std::string& msg, const char* what, long verify) {
        char buf[256] = {0};
        unsigned long err = a->ERR_get_error();
        if (err) a->ERR_error_string_n(err, buf, sizeof buf);
        msg = what;
        if (buf[0]) msg += std::string(": ") + buf;
        if (verify != 0) msg += std::string(" (certificate verification: ") + a->X509_verify_cert_error_string(verify) + ")";
        return nullptr;
    }

    /// TLS client handshake on the connected socket, with certificate and host name verification.
    bool Handshake(void* ctx, const std::string& host, Err& e, std::string& msg) {
        const TlsApi* a = api_;
        a->ERR_clear_error();
        ssl_ = a->SSL_new(ctx);
        if (!ssl_) { e = Err::Tls; Fail(a, msg, "SSL_new failed", 0); return false; }
        if (a->SSL_set_fd(ssl_, tcp_->fd) != 1) { e = Err::Tls; Fail(a, msg, "SSL_set_fd failed", 0); return false; }
        if (detail::IsIpLiteral(host)) {
            if (a->X509_VERIFY_PARAM_set1_ip_asc(a->SSL_get0_param(ssl_), host.c_str()) != 1) { e = Err::Tls; msg = "cannot set the expected IP address"; return false; }
        } else {
            if (a->SSL_set1_host(ssl_, host.c_str()) != 1) { e = Err::Tls; msg = "cannot set the expected host name"; return false; }
            a->SSL_set_hostflags(ssl_, 0x4);   // X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS
            a->SSL_ctrl(ssl_, 55, 0, const_cast<char*>(host.c_str()));   // SSL_set_tlsext_host_name (SNI)
        }
        for (;;) {
            a->ERR_clear_error();
            int r = a->SSL_connect(ssl_);
            if (r == 1) break;
            int er = a->SSL_get_error(ssl_, r);
            if (er == 2 || er == 3) {
                if (!detail::Wait(tcp_->fd, er == 2 ? POLLIN : POLLOUT, tcp_->ioMs, tcp_->deadline, e, tcp_->cancel)) { msg = "TLS handshake timed out"; return false; }
                continue;
            }
            e = Err::Tls;
            Fail(a, msg, "TLS handshake failed", a->SSL_get_verify_result(ssl_));
            return false;
        }
        // Belt and braces: the handshake must also have produced a verified peer certificate.
        long vr = a->SSL_get_verify_result(ssl_);
        void* cert = a->SSL_get_peer_certificate(ssl_);
        if (!cert || vr != 0) { if (cert) a->X509_free(cert); e = Err::Tls; Fail(a, msg, "certificate not verified", vr ? vr : 0); if (!vr) msg = "no peer certificate"; return false; }
        a->X509_free(cert);
        return true;
    }

    long Read(void* b, size_t n, Err& e) override {
        const TlsApi* a = api_;
        for (;;) {
            a->ERR_clear_error();
            int r = a->SSL_read(ssl_, b, int(std::min<size_t>(n, INT_MAX)));
            if (r > 0) return r;
            int er = a->SSL_get_error(ssl_, r);
            if (er == 6) return 0;   // close_notify: clean end of stream
            if (er == 2 || er == 3) { if (!detail::Wait(tcp_->fd, er == 2 ? POLLIN : POLLOUT, tcp_->ioMs, tcp_->deadline, e, tcp_->cancel)) return -1; continue; }
            e = (er == 5) ? Err::Io : Err::Tls;   // includes a close without close_notify (truncation)
            return -1;
        }
    }
    bool Write(const void* b, size_t n, Err& e) override {
        const TlsApi* a = api_;
        detail::SigpipeGuard g;
        const char* p = static_cast<const char*>(b);
        while (n) {
            a->ERR_clear_error();
            int r = a->SSL_write(ssl_, p, int(std::min<size_t>(n, INT_MAX)));
            if (r > 0) { p += r; n -= size_t(r); continue; }
            int er = a->SSL_get_error(ssl_, r);
            if (er == 2 || er == 3) { if (!detail::Wait(tcp_->fd, er == 2 ? POLLIN : POLLOUT, tcp_->ioMs, tcp_->deadline, e, tcp_->cancel)) return false; continue; }
            e = (er == 5) ? Err::Io : Err::Tls;
            return false;
        }
        return true;
    }
private:
    const TlsApi* api_;
    std::unique_ptr<TcpStream> tcp_;
    void* ssl_ = nullptr;
};

namespace detail {
/// One SSL_CTX per (library, CA configuration), kept for the life of the process.
inline void* TlsContext(const TlsApi* a, const NetOptions& o, std::string& why) {
    static std::mutex mu;
    static std::map<std::string, void*> cache;
    std::vector<std::string> files; std::string dir;
    if (!o.caFile.empty() || !o.caDir.empty()) { if (!o.caFile.empty()) files.push_back(o.caFile); dir = o.caDir; }
    else {
        if (const char* f = o.Env("SSL_CERT_FILE")) files.push_back(f);
        if (const char* f = o.Env("CURL_CA_BUNDLE")) files.push_back(f);
        if (const char* d = o.Env("SSL_CERT_DIR")) dir = d;
    }
    std::string key = std::to_string(reinterpret_cast<uintptr_t>(a)) + "|" + dir;
    for (const std::string& f : files) key += "|" + f;
    std::lock_guard<std::mutex> lk(mu);
    auto it = cache.find(key);
    if (it != cache.end()) return it->second;
    void* ctx = a->SSL_CTX_new(a->TLS_client_method());
    if (!ctx) { why = "SSL_CTX_new failed"; return nullptr; }
    // TLS 1.2 or newer (SSL_CTRL_SET_MIN_PROTO_VERSION = 123, TLS1_2_VERSION = 0x0303).
    if (a->SSL_CTX_ctrl(ctx, 123, 0x0303, nullptr) != 1) { why = "cannot require TLS 1.2"; a->SSL_CTX_free(ctx); return nullptr; }
    a->SSL_CTX_set_verify(ctx, 1 /* SSL_VERIFY_PEER */, nullptr);
    if (files.empty() && dir.empty()) {
        if (a->SSL_CTX_set_default_verify_paths(ctx) != 1) { why = "cannot load the system CA store"; a->SSL_CTX_free(ctx); return nullptr; }
    } else {
        for (const std::string& f : files) {
            struct stat st;
            if (::stat(f.c_str(), &st) != 0 || !S_ISREG(st.st_mode) || a->SSL_CTX_load_verify_locations(ctx, f.c_str(), nullptr) != 1) {
                why = "CA file cannot be loaded: " + f; a->SSL_CTX_free(ctx); return nullptr;   // no silent fallback to something else
            }
        }
        if (!dir.empty() && a->SSL_CTX_load_verify_locations(ctx, nullptr, dir.c_str()) != 1) {
            why = "CA directory cannot be used: " + dir; a->SSL_CTX_free(ctx); return nullptr;
        }
    }
    cache[key] = ctx;
    return ctx;
}
} // namespace detail

// ----------------------------------------------------------------------------
// Connector
// ----------------------------------------------------------------------------
namespace detail {
/// Idle connections kept for reuse, per thread (so no locking and no sharing of a stream between threads). A thread that
/// ends closes its idle connections. Small on purpose: the launcher talks to a handful of hosts.
struct IdleConn { std::string key; std::unique_ptr<Stream> stream; Clock::time_point since; };
inline std::vector<IdleConn>& IdlePool() { thread_local std::vector<IdleConn> pool; return pool; }
constexpr size_t kMaxIdleConnections = 8;
constexpr int kIdleMaxMs = 15000;
/// Number of connections waiting in this thread's pool (tests).
inline size_t IdleCount() { return IdlePool().size(); }
/// Close every idle connection of this thread now.
inline void DropIdleConnections() { IdlePool().clear(); }
} // namespace detail

class SystemConnector : public Connector {
public:
    explicit SystemConnector(NetOptions o) : o_(std::move(o)) {}

    std::unique_ptr<Stream> Open(const Url& u, Clock::time_point deadline, Err& e, std::string& msg) override {
        Proxy px;
        if (!ResolveProxy(u, o_, px, msg)) { e = Err::Proxy; return nullptr; }
        const TlsApi* api = nullptr; void* ctx = nullptr;
        if (u.Https()) {   // fail before any network traffic when TLS is unavailable
            api = TlsApi::Get(o_, msg);
            if (!api) { e = Err::Tls; msg = "https refused: " + msg; return nullptr; }
            ctx = detail::TlsContext(api, o_, msg);
            if (!ctx) { e = Err::Tls; return nullptr; }
        }
        std::unique_ptr<TcpStream> tcp = px.use ? TcpConnect(px.host, px.port, o_, deadline, e, msg)
                                                : TcpConnect(u.host, u.port, o_, deadline, e, msg);
        if (!tcp) return nullptr;
        if (!u.Https()) return tcp;
        if (px.use && !Tunnel(*tcp, u, e, msg)) return nullptr;
        auto tls = std::make_unique<TlsStream>(api, std::move(tcp));
        if (!tls->Handshake(ctx, u.host, e, msg)) return nullptr;
        return tls;
    }

    bool AbsoluteForm(const Url& u) override {
        Proxy px; std::string why;
        return !u.Https() && ResolveProxy(u, o_, px, why) && px.use;
    }

    std::unique_ptr<Stream> TakeIdle(const Url& u, Clock::time_point deadline) override {
        if (!o_.keepAlive) return nullptr;
        auto& pool = detail::IdlePool();
        const std::string key = PoolKey(u);
        const Clock::time_point now = Clock::now();
        for (size_t i = 0; i < pool.size(); ) {
            const bool tooOld = std::chrono::duration_cast<std::chrono::milliseconds>(now - pool[i].since).count() > detail::kIdleMaxMs;
            if (tooOld || (pool[i].key == key && !pool[i].stream->StillUsable())) { pool.erase(pool.begin() + long(i)); continue; }
            if (pool[i].key == key) {
                std::unique_ptr<Stream> s = std::move(pool[i].stream);
                pool.erase(pool.begin() + long(i));
                s->Rebind(deadline, o_.ioMs, &o_.cancel);
                return s;
            }
            ++i;
        }
        return nullptr;
    }

    void GiveIdle(const Url& u, std::unique_ptr<Stream> s) override {
        if (!o_.keepAlive || !s) return;
        s->Rebind(Clock::time_point(), o_.ioMs, nullptr);   // nothing of this call may stay referenced
        auto& pool = detail::IdlePool();
        const std::string key = PoolKey(u);
        for (size_t i = 0; i < pool.size(); ) { if (pool[i].key == key) pool.erase(pool.begin() + long(i)); else ++i; }
        pool.push_back(detail::IdleConn{key, std::move(s), Clock::now()});
        while (pool.size() > detail::kMaxIdleConnections) pool.erase(pool.begin());
    }

private:
    /// What makes two connections interchangeable: scheme, host, port, the proxy in use and the CA configuration.
    std::string PoolKey(const Url& u) const {
        Proxy px; std::string why;
        std::string k = u.scheme + "|" + u.host + "|" + std::to_string(u.port) + "|";
        if (ResolveProxy(u, o_, px, why) && px.use) k += px.host + ":" + std::to_string(px.port);
        k += "|" + o_.caFile + "|" + o_.caDir;
        if (u.Https()) for (const char* n : {"SSL_CERT_FILE", "CURL_CA_BUNDLE", "SSL_CERT_DIR"}) { const char* v = o_.Env(n); k += "|"; if (v) k += v; }
        return k;
    }
    static bool Tunnel(TcpStream& tcp, const Url& u, Err& e, std::string& msg) {
        std::string hp = (u.IsV6() ? "[" + u.host + "]" : u.host) + ":" + std::to_string(u.port);
        std::string req = "CONNECT " + hp + " HTTP/1.1\r\nHost: " + hp + "\r\nUser-Agent: OkaySpace/1.0\r\n\r\n";
        if (!tcp.Write(req.data(), req.size(), e)) { e = Err::Proxy; msg = "proxy write failed"; return false; }
        Reader rd(tcp); Response head; Err he = Err::None; std::string hm;
        if (!ReadHead(rd, Limits(), head, he, &hm)) { e = Err::Proxy; msg = "bad proxy answer to CONNECT: " + hm; return false; }
        if (head.status < 200 || head.status > 299) { e = Err::Proxy; msg = "proxy refused CONNECT (HTTP " + std::to_string(head.status) + ")"; return false; }
        if (rd.Avail() != 0) { e = Err::Proxy; msg = "proxy sent data before the TLS handshake"; return false; }
        return true;
    }
    NetOptions o_;
};

/// True when https can be used (libssl loads); `why` says why not.
inline bool TlsAvailable(const NetOptions& o, std::string& why) { return TlsApi::Get(o, why) != nullptr; }

/// One request over TCP/TLS with redirects, proxies and limits.
inline FetchResult Perform(const std::string& method, const std::string& url,
                           const std::vector<std::string>& headerLines, const std::string& body,
                           const NetOptions& o, const FetchHooks& hooks = FetchHooks()) {
    SystemConnector c(o);
    FetchRequest rq; rq.method = method; rq.url = url; rq.headerLines = headerLines; rq.body = body; rq.acceptGzip = o.acceptGzip; rq.keepAlive = o.keepAlive; rq.jar = o.cookieJar;
    return Fetch(c, rq, o.limits, o.totalMs, hooks);
}

} // namespace okay::ownhttp
#endif // !_WIN32
