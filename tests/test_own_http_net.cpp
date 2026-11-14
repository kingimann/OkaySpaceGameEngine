// Integration tests for okay::ownhttp transports (OwnHttpNet.hpp) against local servers:
//  - a plain HTTP server written here (redirects, chunked, big body, slow, truncated, ...),
//  - a tiny CONNECT / absolute-form proxy written here,
//  - `openssl s_server -HTTP` as the TLS server, with certificates from
//    tests/scripts/gen_http_test_certs.sh (valid, wrong host name, expired, untrusted CA).
// The TLS part is skipped (exit 0, "SKIP") when the openssl program or libssl is missing,
// unless OKAY_REQUIRE_TLS_TESTS=1.
//   test_own_http_net <path to gen_http_test_certs.sh>
#define OKAY_OWN_HTTP 1   // the NativeHttp wiring test below
#include "okay/Platform/NativeHttp.hpp"
#include "okay/Platform/OwnHttpNet.hpp"

#include <atomic>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <sys/wait.h>
#include <thread>

using namespace okay::ownhttp;

static int g_fail = 0, g_checks = 0, g_skips = 0;
#define CHECK(c) do { ++g_checks; if (!(c)) { ++g_fail; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
#define CHECK_ERR(r, want) do { ++g_checks; if ((r).ok || (r).err != (want)) { ++g_fail; std::printf("FAIL %s:%d: expected %s, got ok=%d %s (%s)\n", __FILE__, __LINE__, ErrName(want), (r).ok, ErrName((r).err), (r).message.c_str()); } } while (0)

static void WriteFile(const std::string& p, const std::string& d) { std::ofstream(p, std::ios::binary) << d; }

static void Sh(const std::string& c) { if (std::system(c.c_str()) != 0) { /* best effort cleanup */ } }

static void SendAll(int fd, const std::string& s) {
    size_t off = 0;
    while (off < s.size()) { ssize_t n = ::send(fd, s.data() + off, s.size() - off, MSG_NOSIGNAL); if (n <= 0) return; off += size_t(n); }
}

static int Listen(int& port) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    int one = 1; ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    sockaddr_in a{}; a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK); a.sin_port = 0;
    if (::bind(fd, (sockaddr*)&a, sizeof a) != 0 || ::listen(fd, 16) != 0) { std::perror("bind"); std::exit(2); }
    socklen_t l = sizeof a; ::getsockname(fd, (sockaddr*)&a, &l); port = ntohs(a.sin_port);
    return fd;
}

// Runs `handler(clientFd, requestHead, requestBody)` for each connection on its own thread.
struct Server {
    int fd = -1, port = 0;
    std::atomic<bool> stop{false};
    std::atomic<int> connections{0};
    std::thread acceptor;
    std::mutex mu;
    std::vector<std::thread> workers;
    std::vector<std::string> requests;   // request heads, in arrival order
    std::function<void(int, const std::string&, const std::string&)> handler;

    void Start() {
        fd = Listen(port);
        acceptor = std::thread([this] {
            while (!stop) {
                pollfd p{fd, POLLIN, 0};
                if (::poll(&p, 1, 50) <= 0) continue;
                int c = ::accept(fd, nullptr, nullptr);
                if (c < 0) continue;
                ++connections;
                std::lock_guard<std::mutex> lk(mu);
                workers.emplace_back([this, c] { Serve(c); ::close(c); });
            }
        });
    }
    void Serve(int c) {
        std::string in; char b[4096];
        while (in.find("\r\n\r\n") == std::string::npos && !stop) {
            pollfd p{c, POLLIN, 0}; if (::poll(&p, 1, 100) <= 0) continue;
            ssize_t n = ::recv(c, b, sizeof b, 0); if (n <= 0) return; in.append(b, size_t(n));
        }
        size_t he = in.find("\r\n\r\n"); if (he == std::string::npos) return;
        std::string head = in.substr(0, he + 4), body = in.substr(he + 4);
        size_t cl = 0; size_t pos = head.find("Content-Length: ");
        if (pos != std::string::npos) cl = size_t(std::atoll(head.c_str() + pos + 16));
        while (body.size() < cl && !stop) {
            pollfd p{c, POLLIN, 0}; if (::poll(&p, 1, 100) <= 0) continue;
            ssize_t n = ::recv(c, b, sizeof b, 0); if (n <= 0) break; body.append(b, size_t(n));
        }
        { std::lock_guard<std::mutex> lk(mu); requests.push_back(head); }
        handler(c, head, body);
    }
    void Stop() {
        if (fd < 0) return;
        stop = true;
        if (acceptor.joinable()) acceptor.join();
        for (auto& w : workers) if (w.joinable()) w.join();
        workers.clear();
        if (fd >= 0) ::close(fd);
        fd = -1;
    }
    std::string Request(size_t i) { std::lock_guard<std::mutex> lk(mu); return i < requests.size() ? requests[i] : std::string(); }
    size_t Count() { std::lock_guard<std::mutex> lk(mu); return requests.size(); }
    ~Server() { Stop(); }
};

static std::string PathOf(const std::string& head) {
    size_t a = head.find(' '), b = head.find(' ', a + 1);
    return head.substr(a + 1, b - a - 1);
}
static std::string Ok(const std::string& body) { return "HTTP/1.1 200 OK\r\nContent-Length: " + std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body; }

static NetOptions Opts() {
    NetOptions o;
    o.env = [](const char*) -> const char* { return nullptr; };   // tests never inherit this sandbox's proxy settings
    o.connectMs = 2000; o.ioMs = 3000; o.totalMs = 20000;
    return o;
}

static void TestPlain() {
    Server s;
    s.handler = [&](int c, const std::string& head, const std::string& body) {
        std::string path = PathOf(head);
        const std::string self = "http://127.0.0.1:" + std::to_string(s.port);
        if (path == "/ok") SendAll(c, Ok("hello"));
        else if (path == "/echo") SendAll(c, Ok(head.substr(0, head.find("\r\n")) + "|" + body));
        else if (path == "/hdrs") SendAll(c, Ok(head));
        else if (path == "/chunked") SendAll(c, "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhello\r\n6;x=y\r\n world\r\n0\r\n\r\n");
        else if (path == "/redir") SendAll(c, "HTTP/1.1 302 Found\r\nLocation: /ok\r\nContent-Length: 0\r\n\r\n");
        else if (path == "/redir-abs") SendAll(c, "HTTP/1.1 301 Moved\r\nLocation: " + self + "/ok\r\nContent-Length: 0\r\n\r\n");
        else if (path == "/loop") SendAll(c, "HTTP/1.1 302 Found\r\nLocation: /loop\r\nContent-Length: 0\r\n\r\n");
        else if (path == "/cross") SendAll(c, "HTTP/1.1 302 Found\r\nLocation: http://localhost:" + std::to_string(s.port) + "/hdrs\r\nContent-Length: 0\r\n\r\n");
        else if (path == "/big") { SendAll(c, "HTTP/1.1 200 OK\r\nContent-Length: 3145728\r\n\r\n"); std::string z(65536, 'z'); for (int i = 0; i < 48; ++i) SendAll(c, z); }
        else if (path == "/bigchunked") { SendAll(c, "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"); std::string z(65536, 'z'); for (int i = 0; i < 48; ++i) SendAll(c, "10000\r\n" + z + "\r\n"); SendAll(c, "0\r\n\r\n"); }
        else if (path == "/slow") { std::this_thread::sleep_for(std::chrono::milliseconds(2500)); SendAll(c, Ok("late")); }
        else if (path == "/drip") { SendAll(c, "HTTP/1.1 200 OK\r\nContent-Length: 1000\r\n\r\n"); for (int i = 0; i < 100 && !s.stop; ++i) { SendAll(c, "x"); std::this_thread::sleep_for(std::chrono::milliseconds(50)); } }
        else if (path == "/truncated") SendAll(c, "HTTP/1.1 200 OK\r\nContent-Length: 100\r\n\r\nshort");
        else if (path == "/garbage") SendAll(c, "this is not http\r\n\r\n");
        else if (path == "/gzip") SendAll(c, "HTTP/1.1 200 OK\r\nContent-Encoding: gzip\r\nContent-Length: 2\r\n\r\n\x1f\x8b");
        else if (path == "/404") SendAll(c, "HTTP/1.1 404 Not Found\r\nContent-Length: 4\r\n\r\nnope");
        else if (path == "/hang") { while (!s.stop) std::this_thread::sleep_for(std::chrono::milliseconds(20)); }
        else SendAll(c, "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n");
    };
    s.Start();
    const std::string base = "http://127.0.0.1:" + std::to_string(s.port);
    NetOptions o = Opts();

    FetchResult r = Perform("GET", base + "/ok", {}, "", o);
    CHECK(r.ok && r.response.status == 200 && r.response.body == "hello");
    CHECK(s.Request(0).find("Host: 127.0.0.1:" + std::to_string(s.port) + "\r\n") != std::string::npos);
    r = Perform("POST", base + "/echo", {"Content-Type: application/json", "apikey: k"}, "{\"a\":1}", o);
    CHECK(r.ok && r.response.body == "POST /echo HTTP/1.1|{\"a\":1}");
    r = Perform("GET", base + "/404", {}, "", o);
    CHECK(r.ok && r.response.status == 404 && r.response.body == "nope");
    r = Perform("GET", base + "/chunked", {}, "", o);
    CHECK(r.ok && r.response.body == "hello world");
    r = Perform("GET", base + "/redir", {}, "", o);
    CHECK(r.ok && r.redirects == 1 && r.response.body == "hello");
    r = Perform("GET", base + "/redir-abs", {}, "", o);
    CHECK(r.ok && r.redirects == 1 && r.response.body == "hello");
    r = Perform("GET", base + "/loop", {}, "", o);
    CHECK_ERR(r, Err::TooManyRedirects);
    r = Perform("HEAD", base + "/ok", {}, "", o);
    CHECK(r.ok && r.response.status == 200 && r.response.body.empty());
    r = Perform("GET", base + "/garbage", {}, "", o);
    CHECK_ERR(r, Err::BadResponse);
    r = Perform("GET", base + "/gzip", {}, "", o);
    CHECK_ERR(r, Err::Unsupported);
    r = Perform("GET", base + "/truncated", {}, "", o);
    CHECK_ERR(r, Err::BadResponse);

    // credentials do not follow a redirect to another origin (localhost vs 127.0.0.1), other headers do
    r = Perform("GET", base + "/cross", {"Authorization: Bearer SECRET", "apikey: KEY", "X-Plain: yes"}, "", o);
    CHECK(r.ok && r.redirects == 1);
    CHECK(r.response.body.find("SECRET") == std::string::npos && r.response.body.find("apikey") == std::string::npos);
    CHECK(r.response.body.find("X-Plain: yes") != std::string::npos);

    // size caps: 3 MiB passes with a 4 MiB cap, fails with a 1 MiB cap (Content-Length known up front, and chunked)
    o.limits.maxBodyBytes = 4u << 20;
    r = Perform("GET", base + "/big", {}, "", o);
    CHECK(r.ok && r.response.body.size() == 3145728);
    r = Perform("GET", base + "/bigchunked", {}, "", o);
    CHECK(r.ok && r.response.body.size() == 3145728);
    o.limits.maxBodyBytes = 1u << 20;
    r = Perform("GET", base + "/big", {}, "", o);
    CHECK_ERR(r, Err::TooLarge);
    r = Perform("GET", base + "/bigchunked", {}, "", o);
    CHECK_ERR(r, Err::TooLarge);
    {   // streaming to a callback keeps memory flat and counts bytes
        o.limits.maxBodyBytes = 8u << 20;
        FetchHooks hk; uint64_t n = 0; int heads = 0;
        hk.onHead = [&](const Response& h) { ++heads; return h.status == 200; };
        hk.onData = [&](const char*, size_t k) { n += k; return true; };
        r = Perform("GET", base + "/big", {}, "", o, hk);
        CHECK(r.ok && n == 3145728 && heads == 1 && r.response.body.empty());
    }

    // time limits
    o = Opts(); o.ioMs = 400;
    auto t0 = Clock::now();
    r = Perform("GET", base + "/slow", {}, "", o);
    CHECK_ERR(r, Err::Timeout);
    CHECK(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t0).count() < 2200);
    r = Perform("GET", base + "/hang", {}, "", o);
    CHECK_ERR(r, Err::Timeout);
    o = Opts(); o.totalMs = 700;   // a server that keeps dripping cannot hold us past the total limit
    t0 = Clock::now();
    r = Perform("GET", base + "/drip", {}, "", o);
    CHECK_ERR(r, Err::Timeout);
    CHECK(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t0).count() < 2500);

    // injection is refused before anything is sent
    o = Opts();
    size_t before = s.Count();
    r = Perform("GET", base + "/ok", {"X-A: b\r\nX-Evil: 1"}, "", o);
    CHECK_ERR(r, Err::BadHeader);
    r = Perform("GET", base + "/ok\r\nX-Evil: 1", {}, "", o);
    CHECK_ERR(r, Err::BadUrl);
    r = Perform("GE T", base + "/ok", {}, "", o);
    CHECK_ERR(r, Err::BadHeader);
    CHECK(s.Count() == before);

    // refused connection
    int deadPort; int dfd = Listen(deadPort); ::close(dfd);
    r = Perform("GET", "http://127.0.0.1:" + std::to_string(deadPort) + "/", {}, "", o);
    CHECK_ERR(r, Err::Connect);

    // https against a server that only speaks plain HTTP (it answers the ClientHello with an HTTP response): handshake fails
    {
        NetOptions t = Opts(); std::string why;
        if (TlsAvailable(t, why)) {
            int port; int lfd = Listen(port);
            std::string seen; std::thread th([&] {
                int c = ::accept(lfd, nullptr, nullptr); if (c < 0) return;
                char b[2048]; ssize_t n = ::recv(c, b, sizeof b, 0); if (n > 0) seen.assign(b, size_t(n));
                SendAll(c, "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok"); ::close(c);
            });
            r = Perform("GET", "https://127.0.0.1:" + std::to_string(port) + "/ok", {"Authorization: Bearer SECRET"}, "", t);
            th.join(); ::close(lfd);
            CHECK_ERR(r, Err::Tls);
            CHECK(seen.find("SECRET") == std::string::npos && seen.find("GET ") == std::string::npos);   // only a ClientHello went out
        }
    }
    // libssl missing: https is refused and no connection is even attempted
    {
        NetOptions t = Opts(); t.libsslNames = {"/nonexistent/libssl.so.99"};
        int before2 = s.connections;
        r = Perform("GET", "https://127.0.0.1:" + std::to_string(s.port) + "/ok", {}, "", t);
        CHECK_ERR(r, Err::Tls);
        CHECK(r.message.find("https refused") != std::string::npos);
        CHECK(s.connections == before2);
        std::string why; CHECK(!TlsAvailable(t, why) && !why.empty());
        t.libsslNames = {}; t.env = [](const char* n) -> const char* { return std::string(n) == "OKAY_OWN_HTTP_LIBSSL" ? "/nonexistent/libssl.so" : nullptr; };
        // (the cached default instance may already be loaded in this process; the override only matters on first use)
    }
    s.Stop();
}

// Proxy: handles CONNECT (tunnel) and absolute-form GET (forwarded); can be told to refuse.
struct Proxy1 {
    Server srv;
    std::atomic<int> mode{0};   // 0 = forward, 1 = refuse with 403
    void Start() {
        srv.handler = [this](int c, const std::string& head, const std::string&) {
            if (mode == 1) { SendAll(c, "HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\n\r\n"); return; }
            std::string line = head.substr(0, head.find("\r\n"));
            std::string host; int port = 0;
            if (line.rfind("CONNECT ", 0) == 0) {
                std::string hp = line.substr(8, line.find(' ', 8) - 8);
                host = hp.substr(0, hp.rfind(':')); port = std::atoi(hp.c_str() + hp.rfind(':') + 1);
            } else {   // GET http://host:port/path HTTP/1.1
                size_t a = line.find("http://"); size_t b = line.find('/', a + 7);
                std::string hp = line.substr(a + 7, b - a - 7);
                host = hp.substr(0, hp.rfind(':')); port = std::atoi(hp.c_str() + hp.rfind(':') + 1);
            }
            int up = ::socket(AF_INET, SOCK_STREAM, 0);
            sockaddr_in sa{}; sa.sin_family = AF_INET; sa.sin_port = htons(uint16_t(port)); ::inet_pton(AF_INET, host == "localhost" ? "127.0.0.1" : host.c_str(), &sa.sin_addr);
            if (::connect(up, (sockaddr*)&sa, sizeof sa) != 0) { SendAll(c, "HTTP/1.1 502 Bad Gateway\r\nContent-Length: 0\r\n\r\n"); ::close(up); return; }
            if (line.rfind("CONNECT ", 0) == 0) SendAll(c, "HTTP/1.1 200 Connection established\r\n\r\n");
            else {   // rewrite to origin form
                size_t a = line.find("http://"); size_t b = line.find('/', a + 7);
                std::string path = line.substr(b, line.rfind(' ') - b);
                SendAll(up, "GET " + path + " HTTP/1.1" + head.substr(head.find("\r\n")));
            }
            for (int idle = 0; idle < 100 && !srv.stop; ) {   // pipe both ways until either side closes
                pollfd p[2] = {{c, POLLIN, 0}, {up, POLLIN, 0}};
                int r = ::poll(p, 2, 100); if (r <= 0) { ++idle; continue; }
                char b[8192];
                bool done = false;
                for (int i = 0; i < 2 && !done; ++i) if (p[i].revents & (POLLIN | POLLHUP)) {
                    ssize_t n = ::recv(p[i].fd, b, sizeof b, 0);
                    if (n <= 0) { done = true; break; }
                    SendAll(p[i].fd == c ? up : c, std::string(b, size_t(n)));
                }
                if (done) break;
            }
            ::close(up);
        };
        srv.Start();
    }
};

static void TestProxy(const std::string& tlsBase, const std::string& caFile) {
    Server origin;
    origin.handler = [&](int c, const std::string& head, const std::string&) {
        SendAll(c, Ok("via:" + head.substr(0, head.find("\r\n"))));
    };
    origin.Start();
    Proxy1 px; px.Start();
    const std::string purl = "http://127.0.0.1:" + std::to_string(px.srv.port);
    auto envWith = [](std::map<std::string, std::string> m) {
        return EnvFn([m](const char* n) -> const char* { auto it = m.find(n); return it == m.end() ? nullptr : it->second.c_str(); });
    };
    NetOptions o = Opts();
    // plain http through the proxy: absolute-form request line
    o.env = envWith({{"http_proxy", purl}});
    FetchResult r = Perform("GET", "http://127.0.0.1:" + std::to_string(origin.port) + "/p", {}, "", o);
    CHECK(r.ok && r.response.body == "via:GET /p HTTP/1.1");
    CHECK(px.srv.Count() == 1 && px.srv.Request(0).rfind("GET http://127.0.0.1:" + std::to_string(origin.port) + "/p HTTP/1.1", 0) == 0);
    // NO_PROXY bypass
    size_t n = px.srv.Count();
    o.env = envWith({{"http_proxy", purl}, {"NO_PROXY", "localhost,127.0.0.0/8"}});
    r = Perform("GET", "http://127.0.0.1:" + std::to_string(origin.port) + "/p", {}, "", o);
    CHECK(r.ok && px.srv.Count() == n);
    // proxy refuses (https goes through CONNECT, so this also covers the CONNECT 403 path)
    px.mode = 1;
    o.env = envWith({{"HTTPS_PROXY", purl}});
    o.caFile = caFile;
    std::string why;
    if (TlsAvailable(o, why)) {
        r = Perform("GET", "https://localhost:" + std::to_string(origin.port) + "/x", {}, "", o);
        CHECK_ERR(r, Err::Proxy);
        CHECK(r.message.find("403") != std::string::npos);
    }
    px.mode = 0;
    // unusable proxy settings are errors, never a silent direct connection
    size_t on = origin.Count();
    o.env = envWith({{"http_proxy", "https://127.0.0.1:1"}});
    r = Perform("GET", "http://127.0.0.1:" + std::to_string(origin.port) + "/p", {}, "", o);
    CHECK_ERR(r, Err::Proxy);
    o.env = envWith({{"http_proxy", "http://user:pw@127.0.0.1:1"}});
    r = Perform("GET", "http://127.0.0.1:" + std::to_string(origin.port) + "/p", {}, "", o);
    CHECK_ERR(r, Err::Proxy);
    CHECK(origin.Count() == on);
    int deadPort; int dfd = Listen(deadPort); ::close(dfd);
    o.env = envWith({{"http_proxy", "http://127.0.0.1:" + std::to_string(deadPort)}});
    r = Perform("GET", "http://127.0.0.1:" + std::to_string(origin.port) + "/p", {}, "", o);
    CHECK_ERR(r, Err::Connect);
    (void)tlsBase;
    px.srv.Stop(); origin.Stop();
}

// NativeHttp (the callers' entry point) with the own client: opt-in by environment, same Result semantics.
static void TestNativeHttp() {
    Server s;
    s.handler = [&](int c, const std::string& head, const std::string& body) {
        std::string path = PathOf(head);
        if (path == "/ok") SendAll(c, Ok("hello"));
        else if (path == "/echo") SendAll(c, Ok(body));
        else if (path == "/404") SendAll(c, "HTTP/1.1 404 Not Found\r\nContent-Length: 4\r\n\r\nnope");
        else if (path == "/garbage") SendAll(c, "nonsense\r\n\r\n");
        else SendAll(c, Ok(""));
    };
    s.Start();
    const std::string base = "http://127.0.0.1:" + std::to_string(s.port);
    ::unsetenv("OKAY_OWN_HTTP"); ::unsetenv("OKAY_HTTP_CURL");
    ::unsetenv("HTTP_PROXY"); ::unsetenv("http_proxy"); ::setenv("NO_PROXY", "127.0.0.1", 1);
    CHECK(!okay::http::NativeAvailable());                      // off unless asked for
    ::setenv("OKAY_OWN_HTTP", "1", 1);
    CHECK(okay::http::NativeAvailable());
    okay::http::Result r = okay::http::Request("GET", base + "/ok", {});
    CHECK(r.reached && r.status == 200 && r.body == "hello");
    r = okay::http::Request("POST", base + "/echo", {"Content-Type: text/plain"}, "payload");
    CHECK(r.reached && r.body == "payload");
    char tmpl[] = "/tmp/okay_nh_XXXXXX"; int tfd = ::mkstemp(tmpl); ::close(tfd);
    WriteFile(tmpl, "file body");
    r = okay::http::Request("POST", base + "/echo", {}, "", tmpl);   // body from a file
    CHECK(r.reached && r.body == "file body");
    r = okay::http::Request("GET", base + "/404", {});
    CHECK(r.reached && r.status == 404 && r.body == "nope");
    r = okay::http::Request("GET", base + "/ok", {}, "", "", tmpl);   // download to a file
    CHECK(r.reached && r.status == 200 && r.body.empty());
    { std::ifstream f(tmpl, std::ios::binary); std::string d((std::istreambuf_iterator<char>(f)), {}); CHECK(d == "hello"); }
    r = okay::http::Request("GET", base + "/garbage", {});
    CHECK(!r.reached && !r.error.empty());                       // callers fall back to curl
    r = okay::http::Request("GET", base + "/ok", {"Bad: x\r\nInjected: 1"});
    CHECK(!r.reached);
    r = okay::http::Request("GET", "https://127.0.0.1:" + std::to_string(s.port) + "/ok", {});
    CHECK(!r.reached);                                          // no CA trusts it / not TLS: never "reached"
    ::setenv("OKAY_HTTP_CURL", "1", 1);
    CHECK(!okay::http::NativeAvailable());                      // the existing kill switch still wins
    ::unsetenv("OKAY_HTTP_CURL"); ::unsetenv("OKAY_OWN_HTTP"); ::unsetenv("NO_PROXY");
    ::unlink(tmpl);
    s.Stop();
}


// ---- gzip, Range / resume, progress, cancel, name resolution (loopback) ----------------------------------------
static std::string StoredGzip(const std::string& d) {
    std::string g("\x1f\x8b\x08\x00\x00\x00\x00\x00\x00\x03", 10);
    g.push_back('\x01'); g.push_back(char(d.size() & 255)); g.push_back(char(d.size() >> 8));
    g.push_back(char(~d.size() & 255)); g.push_back(char((~d.size() >> 8) & 255));
    g += d;
    uint32_t c = inflate_detail::Crc32(d), n = uint32_t(d.size());
    for (int i = 0; i < 4; ++i) g.push_back(char(c >> (8 * i)));
    for (int i = 0; i < 4; ++i) g.push_back(char(n >> (8 * i)));
    return g;
}

static void TestDownloadFeatures() {
    std::string content; for (int i = 0; i < 1000; ++i) content += char('a' + i % 26);
    const std::string gz = StoredGzip("compressed hello");
    Server s;
    s.handler = [&](int c, const std::string& head, const std::string&) {
        std::string path = PathOf(head);
        size_t rp = head.find("Range: bytes=");
        uint64_t from = rp == std::string::npos ? 0 : uint64_t(std::atoll(head.c_str() + rp + 13));
        bool hasRange = rp != std::string::npos;
        if (path == "/gz") {
            bool asked = head.find("Accept-Encoding: gzip") != std::string::npos;
            if (asked) SendAll(c, "HTTP/1.1 200 OK\r\nContent-Encoding: gzip\r\nContent-Length: " + std::to_string(gz.size()) + "\r\n\r\n" + gz);
            else SendAll(c, Ok("plain hello"));
        } else if (path == "/gzbad") {
            std::string b = gz; b[b.size() - 9] ^= 1;   // data byte flipped: CRC must catch it
            SendAll(c, "HTTP/1.1 200 OK\r\nContent-Encoding: gzip\r\nContent-Length: " + std::to_string(b.size()) + "\r\n\r\n" + b);
        } else if (path == "/file") {
            if (hasRange && from >= content.size()) SendAll(c, "HTTP/1.1 416 Range Not Satisfiable\r\nContent-Range: bytes */1000\r\nContent-Length: 0\r\n\r\n");
            else if (hasRange) { std::string part = content.substr(size_t(from)); SendAll(c, "HTTP/1.1 206 Partial Content\r\nContent-Range: bytes " + std::to_string(from) + "-999/1000\r\nContent-Length: " + std::to_string(part.size()) + "\r\n\r\n" + part); }
            else SendAll(c, Ok(content));
        } else if (path == "/norange") SendAll(c, Ok(content));                       // ignores Range, sends 200
        else if (path == "/wrongrange") SendAll(c, "HTTP/1.1 206 Partial Content\r\nContent-Range: bytes 0-9/1000\r\nContent-Length: 10\r\n\r\n" + content.substr(0, 10));
        else if (path == "/missing") SendAll(c, "HTTP/1.1 404 Not Found\r\nContent-Length: 4\r\n\r\nnope");
        else if (path == "/slowbody") { SendAll(c, "HTTP/1.1 200 OK\r\nContent-Length: 100000\r\n\r\n"); std::string z(1000, 'z'); for (int i = 0; i < 100 && !s.stop; ++i) { SendAll(c, z); std::this_thread::sleep_for(std::chrono::milliseconds(20)); } }
        else if (path == "/hang") { while (!s.stop) std::this_thread::sleep_for(std::chrono::milliseconds(20)); }
        else SendAll(c, "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n");
    };
    s.Start();
    const std::string base = "http://127.0.0.1:" + std::to_string(s.port);
    NetOptions o = Opts();
    // gzip through Perform
    { o.acceptGzip = true;
      FetchResult r = Perform("GET", base + "/gz", {}, "", o); CHECK(r.ok && r.response.body == "compressed hello");
      CHECK(s.Request(s.Count() - 1).find("Accept-Encoding: gzip, deflate") != std::string::npos);
      r = Perform("GET", base + "/gz", {}, "", o); CHECK(r.ok);
      r = Perform("GET", base + "/gzbad", {}, "", o); CHECK_ERR(r, Err::BadResponse);
      o.acceptGzip = false;
      r = Perform("GET", base + "/gz", {}, "", o); CHECK(r.ok && r.response.body == "plain hello");   // never asked, never sent }
      r = Perform("GET", base + "/gzbad", {}, "", o); CHECK_ERR(r, Err::Unsupported); }
    // Range through Perform
    { FetchResult r = Perform("GET", base + "/file", {"Range: bytes=990-"}, "", o);
      CHECK(r.ok && r.partial && r.rangeStart == 990 && r.rangeTotal == 1000 && r.response.body == content.substr(990));
      r = Perform("GET", base + "/wrongrange", {"Range: bytes=5-"}, "", o); CHECK_ERR(r, Err::BadResponse);
      r = Perform("GET", base + "/file", {"Range: bytes=5000-"}, "", o); CHECK(r.ok && r.response.status == 416 && !r.partial); }
    // RequestEx: resume, restart, 404 left alone, progress
    ::setenv("OKAY_OWN_HTTP", "1", 1); ::unsetenv("OKAY_HTTP_CURL");
    ::setenv("NO_PROXY", "127.0.0.1", 1); ::unsetenv("HTTP_PROXY"); ::unsetenv("http_proxy"); ::unsetenv("HTTPS_PROXY"); ::unsetenv("https_proxy");
    char tmpl[] = "/tmp/okay_dl_XXXXXX"; int tfd = ::mkstemp(tmpl); ::close(tfd);
    auto slurp = [&](const char* p) { std::ifstream f(p, std::ios::binary); return std::string((std::istreambuf_iterator<char>(f)), {}); };
    { okay::http::RequestOptions ro;
      std::vector<std::pair<uint64_t, uint64_t>> seen;
      ro.progress = [&](uint64_t d, uint64_t t) { seen.push_back({d, t}); return true; };
      okay::http::ExResult r = okay::http::RequestEx("GET", base + "/file", {}, "", "", tmpl, ro);   // fresh
      CHECK(r.reached && r.status == 200 && !r.resumed && r.totalSize == 1000 && slurp(tmpl) == content);
      CHECK(!seen.empty() && seen.back().first == 1000 && seen.back().second == 1000);
      WriteFile(tmpl, content.substr(0, 400)); seen.clear(); ro.resumeFrom = 400;                   // resume
      r = okay::http::RequestEx("GET", base + "/file", {}, "", "", tmpl, ro);
      CHECK(r.reached && r.status == 206 && r.resumed && r.totalSize == 1000 && slurp(tmpl) == content);
      CHECK(!seen.empty() && seen.back().first == 1000 && seen.back().second == 1000);              // counts from the offset
      WriteFile(tmpl, "stale partial"); ro.resumeFrom = 13;                                          // server ignores Range: start over
      r = okay::http::RequestEx("GET", base + "/norange", {}, "", "", tmpl, ro);
      CHECK(r.reached && r.status == 200 && !r.resumed && slurp(tmpl) == content);
      WriteFile(tmpl, content); ro.resumeFrom = 1000;                                                // already complete: 416, file untouched
      r = okay::http::RequestEx("GET", base + "/file", {}, "", "", tmpl, ro);
      CHECK(r.reached && r.status == 416 && !r.resumed && slurp(tmpl) == content);
      WriteFile(tmpl, "keep me"); ro.resumeFrom = 0;
      r = okay::http::RequestEx("GET", base + "/missing", {}, "", "", tmpl, ro);                    // 404 never overwrites the file
      CHECK(r.reached && r.status == 404 && slurp(tmpl) == "keep me");
      WriteFile(tmpl, "keep me"); ro.resumeFrom = 5;
      r = okay::http::RequestEx("GET", base + "/wrongrange", {}, "", "", tmpl, ro);                 // wrong slice: refused, file untouched
      CHECK(!r.reached && slurp(tmpl) == "keep me");
      okay::http::RequestOptions mem; mem.acceptGzip = true;                                         // in-memory gzip
      r = okay::http::RequestEx("GET", base + "/gz", {}, "", "", "", mem);
      CHECK(r.reached && r.body == "compressed hello"); }
    // cancel: from the progress callback, and while the server is silent (header never comes)
    { okay::http::RequestOptions ro; int calls = 0;
      ro.progress = [&](uint64_t, uint64_t) { return ++calls < 3; };
      okay::http::ExResult r = okay::http::RequestEx("GET", base + "/slowbody", {}, "", "", tmpl, ro);
      CHECK(!r.reached && r.error.find("aborted") != std::string::npos && calls == 3);
      std::atomic<bool> stop{false};
      ro = okay::http::RequestOptions(); ro.cancel = [&] { return stop.load(); }; ro.ioMs = 20000; ro.totalMs = 60000;
      std::thread t([&] { std::this_thread::sleep_for(std::chrono::milliseconds(400)); stop = true; });
      auto t0 = Clock::now();
      r = okay::http::RequestEx("GET", base + "/hang", {}, "", "", "", ro);
      auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t0).count();
      t.join();
      CHECK(!r.reached && r.error.find("aborted") != std::string::npos && ms < 3000); }
    // Request(..., timeoutMs): a stalled server cannot hold the caller (the account layer's join-time ticket call)
    { auto t0 = Clock::now();
      okay::http::Result r = okay::http::Request("GET", base + "/hang", {}, "", "", "", 600);
      auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t0).count();
      CHECK(!r.reached && r.error.find("timeout") == 0 && ms < 3000); }
    // cancel before connecting at all
    { okay::http::RequestOptions ro; ro.cancel = [] { return true; };
      auto t0 = Clock::now();
      okay::http::ExResult r = okay::http::RequestEx("GET", base + "/hang", {}, "", "", "", ro);
      CHECK(!r.reached && std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t0).count() < 2000); }
    // name resolution: a name that does not exist fails quickly and cleanly; the resolve deadline is honoured
    { NetOptions n = Opts(); n.resolveMs = 3000;
      auto t0 = Clock::now();
      FetchResult r = Perform("GET", "http://no-such-host.invalid/", {}, "", n);
      CHECK(!r.ok && (r.err == Err::Connect || r.err == Err::Timeout));
      CHECK(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t0).count() < 8000);
      n.cancel = [] { return true; };
      r = Perform("GET", "http://no-such-host.invalid/", {}, "", n);
      CHECK(!r.ok); }
    ::unlink(tmpl); ::unsetenv("OKAY_OWN_HTTP"); ::unsetenv("NO_PROXY");
    s.Stop();
}

static void TestNoProxyList() {
    CHECK(HostBypassesProxy("localhost", "localhost,127.0.0.1"));
    CHECK(HostBypassesProxy("127.0.0.1", "localhost,127.0.0.1"));
    CHECK(HostBypassesProxy("a.svc.cluster.local", "*.svc.cluster.local"));
    CHECK(HostBypassesProxy("a.svc.cluster.local", ".svc.cluster.local"));
    CHECK(HostBypassesProxy("svc.cluster.local", "svc.cluster.local"));
    CHECK(HostBypassesProxy("sub.example.com", "example.com"));
    CHECK(!HostBypassesProxy("notexample.com", "example.com"));
    CHECK(!HostBypassesProxy("example.com", ".example.com"));
    CHECK(HostBypassesProxy("10.1.2.3", "10.0.0.0/8"));
    CHECK(!HostBypassesProxy("11.1.2.3", "10.0.0.0/8"));
    CHECK(HostBypassesProxy("172.20.0.9", "172.16.0.0/12"));
    CHECK(!HostBypassesProxy("172.32.0.9", "172.16.0.0/12"));
    CHECK(HostBypassesProxy("anything", "*"));
    CHECK(HostBypassesProxy("EXAMPLE.com", " Example.COM ,x"));
    CHECK(HostBypassesProxy("h", "h:8080"));
    CHECK(!HostBypassesProxy("h", ""));
    CHECK(!HostBypassesProxy("example.com", "10.0.0.0/33,10.0.0.0/,/8,abc/8"));
    NetOptions o; Proxy p; std::string why; Url u;
    auto env = [&](std::map<std::string, std::string> m) { o.env = [m](const char* n) -> const char* { auto it = m.find(n); return it == m.end() ? nullptr : it->second.c_str(); }; };
    ParseUrl("https://api.example.com/x", u);
    env({{"HTTPS_PROXY", "http://127.0.0.1:42323"}});
    CHECK(ResolveProxy(u, o, p, why) && p.use && p.host == "127.0.0.1" && p.port == 42323);
    env({{"https_proxy", "proxy.local"}});
    CHECK(ResolveProxy(u, o, p, why) && p.use && p.host == "proxy.local" && p.port == 1080);
    env({{"HTTP_PROXY", "http://p:1"}});   // wrong scheme variable for an https URL
    CHECK(ResolveProxy(u, o, p, why) && !p.use);
    env({{"HTTPS_PROXY", "http://p:1"}, {"NO_PROXY", "example.com"}});
    CHECK(ResolveProxy(u, o, p, why) && !p.use);
    env({{"HTTPS_PROXY", "socks5://p:1"}});
    CHECK(!ResolveProxy(u, o, p, why));
    env({{"HTTPS_PROXY", "http://a:b@p:1"}});
    CHECK(!ResolveProxy(u, o, p, why));
    env({{"HTTPS_PROXY", "http://p:1/path"}});
    CHECK(!ResolveProxy(u, o, p, why));
    env({{"HTTPS_PROXY", ""}});
    CHECK(ResolveProxy(u, o, p, why) && !p.use);
}

// ---- TLS -------------------------------------------------------------------------
struct SServer {
    pid_t pid = -1; int port = 0;
    bool Start(const std::string& dir, const std::string& cert, const std::string& key, const std::vector<std::string>& extra = {}) {
        int fd = Listen(port); ::close(fd);
        pid = ::fork();
        if (pid == 0) {
            if (::chdir(dir.c_str()) != 0) _exit(126);
            int nul = ::open("/dev/null", O_RDWR); ::dup2(nul, 0); ::dup2(nul, 1); ::dup2(nul, 2);
            std::vector<std::string> a = {"openssl", "s_server", "-accept", std::to_string(port), "-cert", cert, "-key", key, "-HTTP", "-quiet"};
            for (auto& e : extra) a.push_back(e);
            std::vector<char*> av; for (auto& s : a) av.push_back(const_cast<char*>(s.c_str())); av.push_back(nullptr);
            ::execvp("openssl", av.data()); _exit(127);
        }
        for (int i = 0; i < 100; ++i) {   // wait until it accepts
            int s = ::socket(AF_INET, SOCK_STREAM, 0); sockaddr_in a{}; a.sin_family = AF_INET; a.sin_port = htons(uint16_t(port)); ::inet_pton(AF_INET, "127.0.0.1", &a.sin_addr);
            bool ok = ::connect(s, (sockaddr*)&a, sizeof a) == 0; ::close(s);
            if (ok) { std::this_thread::sleep_for(std::chrono::milliseconds(50)); return true; }
            int st; if (::waitpid(pid, &st, WNOHANG) == pid) { pid = -1; return false; }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        return false;
    }
    void Stop() { if (pid > 0) { ::kill(pid, SIGTERM); int st; ::waitpid(pid, &st, 0); pid = -1; } }
    ~SServer() { Stop(); }
    std::string Url(const std::string& host, const std::string& file) const { return "https://" + host + ":" + std::to_string(port) + "/" + file; }
};


static void TestTls(const std::string& scriptPath) {
    std::string why;
    NetOptions probe = Opts();
    bool required = std::getenv("OKAY_REQUIRE_TLS_TESTS") != nullptr;
    auto skip = [&](const std::string& m) { std::printf("SKIP TLS tests: %s\n", m.c_str()); ++g_skips; if (required) { ++g_fail; std::printf("FAIL: OKAY_REQUIRE_TLS_TESTS is set\n"); } };
    if (!TlsAvailable(probe, why)) { skip(why); return; }
    if (std::system("openssl version >/dev/null 2>&1") != 0) { skip("the openssl program is not installed"); return; }
    char tmpl[] = "/tmp/okay_http_tls_XXXXXX";
    if (!::mkdtemp(tmpl)) { skip("no temp directory"); return; }
    const std::string dir = tmpl, certs = dir + "/certs", docs = dir + "/docs";
    if (std::system(("sh '" + scriptPath + "' '" + certs + "' >/dev/null").c_str()) != 0) { skip("certificate script failed"); return; }
    Sh("mkdir -p '" + docs + "'");

    const std::string ca = certs + "/ca.pem";
    WriteFile(docs + "/ok.http", "HTTP/1.1 200 OK\r\nContent-Length: 5\r\nConnection: close\r\n\r\nhello");
    WriteFile(docs + "/chunked.http", "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n5\r\nhello\r\n6\r\n world\r\n0\r\n\r\n");
    WriteFile(docs + "/redir.http", "HTTP/1.1 302 Found\r\nLocation: /ok.http\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
    WriteFile(docs + "/big.http", "HTTP/1.1 200 OK\r\nContent-Length: 3145728\r\nConnection: close\r\n\r\n" + std::string(3145728, 'z'));
    WriteFile(docs + "/headers.http", "HTTP/1.1 200 OK\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");

    NetOptions o = Opts(); o.caFile = ca;
    {   // valid certificate: host name and IP address both match
        SServer s; CHECK(s.Start(docs, certs + "/good.crt", certs + "/good.key"));
        FetchResult r = Perform("GET", s.Url("localhost", "ok.http"), {}, "", o);
        CHECK(r.ok && r.response.body == "hello");
        r = Perform("GET", s.Url("127.0.0.1", "ok.http"), {}, "", o);
        CHECK(r.ok && r.response.body == "hello");
        r = Perform("GET", s.Url("localhost", "chunked.http"), {}, "", o);
        CHECK(r.ok && r.response.body == "hello world");
        r = Perform("GET", s.Url("localhost", "redir.http"), {}, "", o);
        CHECK(r.ok && r.redirects == 1 && r.response.body == "hello");
        o.limits.maxBodyBytes = 4u << 20;
        r = Perform("GET", s.Url("localhost", "big.http"), {}, "", o);
        CHECK(r.ok && r.response.body.size() == 3145728);
        o.limits.maxBodyBytes = 1u << 20;
        r = Perform("GET", s.Url("localhost", "big.http"), {}, "", o);
        CHECK_ERR(r, Err::TooLarge);
        o.limits.maxBodyBytes = 64u << 20;
        // a different name than the certificate carries: refused (the server answers on the same port)
        r = Perform("GET", "https://127.0.0.2:" + std::to_string(s.port) + "/ok.http", {}, "", o);
        CHECK_ERR(r, Err::Tls);
        // trusting a different CA: refused
        NetOptions o2 = o; o2.caFile = certs + "/ca2.pem";
        r = Perform("GET", s.Url("localhost", "ok.http"), {}, "", o2);
        CHECK_ERR(r, Err::Tls);
        // a CA file that does not exist: refused, no fallback to the system store
        o2.caFile = certs + "/does-not-exist.pem";
        r = Perform("GET", s.Url("localhost", "ok.http"), {}, "", o2);
        CHECK_ERR(r, Err::Tls); CHECK(r.message.find("CA file") != std::string::npos);
        // CA from the environment (SSL_CERT_FILE, then CURL_CA_BUNDLE)
        NetOptions o3 = Opts();
        o3.env = [&](const char* n) -> const char* { static std::string c = ca; return std::string(n) == "SSL_CERT_FILE" ? c.c_str() : nullptr; };
        r = Perform("GET", s.Url("localhost", "ok.http"), {}, "", o3);
        CHECK(r.ok && r.response.body == "hello");
        o3.env = [&](const char* n) -> const char* { static std::string c = ca; return std::string(n) == "CURL_CA_BUNDLE" ? c.c_str() : nullptr; };
        r = Perform("GET", s.Url("localhost", "ok.http"), {}, "", o3);
        CHECK(r.ok && r.response.body == "hello");
        // no CA configured at all: the system store does not know the test CA, so this is refused
        o3.env = [](const char*) -> const char* { return nullptr; };
        r = Perform("GET", s.Url("localhost", "ok.http"), {}, "", o3);
        CHECK_ERR(r, Err::Tls);
        // credentials are never sent when verification fails: the TLS server can not have answered
        r = Perform("GET", s.Url("localhost", "ok.http"), {"Authorization: Bearer SECRET"}, "", o2);
        CHECK(!r.ok);
    }
    {   // wrong host name in the certificate
        SServer s; CHECK(s.Start(docs, certs + "/wronghost.crt", certs + "/wronghost.key"));
        FetchResult r = Perform("GET", s.Url("localhost", "ok.http"), {}, "", o);
        CHECK_ERR(r, Err::Tls);
        CHECK(r.message.find("verif") != std::string::npos);
        // no environment variable of any common tool turns verification off
        NetOptions e = o;
        e.env = [&](const char* n) -> const char* {
            static const char* off[] = {"OKAY_OWN_HTTP_INSECURE", "OKAY_HTTP_INSECURE", "OKAY_INSECURE", "OKAY_TLS_INSECURE", "OKAY_NO_VERIFY",
                                        "CURL_INSECURE", "SSL_NO_VERIFY", "SSL_VERIFY", "NODE_TLS_REJECT_UNAUTHORIZED", "PYTHONHTTPSVERIFY", "GIT_SSL_NO_VERIFY"};
            for (const char* k : off) if (std::string(n) == k) return std::string(n) == "SSL_VERIFY" || std::string(n) == "PYTHONHTTPSVERIFY" || std::string(n) == "NODE_TLS_REJECT_UNAUTHORIZED" ? "0" : "1";
            return nullptr;
        };
        r = Perform("GET", s.Url("localhost", "ok.http"), {}, "", e);
        CHECK_ERR(r, Err::Tls);
    }
    {   // expired
        SServer s; CHECK(s.Start(docs, certs + "/expired.crt", certs + "/expired.key"));
        FetchResult r = Perform("GET", s.Url("localhost", "ok.http"), {}, "", o);
        CHECK_ERR(r, Err::Tls);
        CHECK(r.message.find("expired") != std::string::npos);
    }
    {   // signed by a CA the client does not trust
        SServer s; CHECK(s.Start(docs, certs + "/untrusted.crt", certs + "/untrusted.key"));
        FetchResult r = Perform("GET", s.Url("localhost", "ok.http"), {}, "", o);
        CHECK_ERR(r, Err::Tls);
    }
    {   // SNI is sent: the default certificate is wrong, the "localhost" one is right
        SServer s; CHECK(s.Start(docs, certs + "/wronghost.crt", certs + "/wronghost.key",
                                 {"-servername", "localhost", "-cert2", certs + "/good.crt", "-key2", certs + "/good.key"}));
        FetchResult r = Perform("GET", s.Url("localhost", "ok.http"), {}, "", o);
        CHECK(r.ok && r.response.body == "hello");
        r = Perform("GET", s.Url("127.0.0.1", "ok.http"), {}, "", o);   // IP: no SNI, default (wrong) certificate
        CHECK_ERR(r, Err::Tls);
    }
    {   // a server limited to TLS 1.1 is refused (we require 1.2 or newer)
        SServer s;
        if (s.Start(docs, certs + "/good.crt", certs + "/good.key", {"-tls1_1", "-cipher", "ALL:@SECLEVEL=0"})) {
            FetchResult r = Perform("GET", s.Url("localhost", "ok.http"), {}, "", o);
            CHECK_ERR(r, Err::Tls);
        } else std::printf("note: this openssl cannot serve TLS 1.1; that case is not exercised\n");
    }
    {   // https -> http redirect is refused
        Server plain; plain.handler = [&](int c, const std::string&, const std::string&) { SendAll(c, Ok("plain")); };
        plain.Start();
        WriteFile(docs + "/down.http", "HTTP/1.1 302 Found\r\nLocation: http://127.0.0.1:" + std::to_string(plain.port) + "/x\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
        SServer s; CHECK(s.Start(docs, certs + "/good.crt", certs + "/good.key"));
        FetchResult r = Perform("GET", s.Url("localhost", "down.http"), {}, "", o);
        CHECK_ERR(r, Err::DowngradeRefused);
        CHECK(plain.Count() == 0);
        plain.Stop();
    }
    {   // handshake that never completes: timeout, not a hang
        Server hang; hang.handler = [&](int, const std::string&, const std::string&) {};
        // the plain Server waits for "\r\n\r\n"; a ClientHello never contains it, so it just sits there
        hang.Start();
        NetOptions t = o; t.ioMs = 400;
        auto t0 = Clock::now();
        FetchResult r = Perform("GET", "https://127.0.0.1:" + std::to_string(hang.port) + "/x", {}, "", t);
        CHECK(!r.ok && (r.err == Err::Timeout || r.err == Err::Tls));
        CHECK(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t0).count() < 3000);
        hang.Stop();
    }
    {   // https through a CONNECT proxy, certificate still verified end to end
        SServer s; CHECK(s.Start(docs, certs + "/good.crt", certs + "/good.key"));
        Proxy1 px; px.Start();
        NetOptions t = o;
        const std::string purl = "http://127.0.0.1:" + std::to_string(px.srv.port);
        t.env = [purl](const char* n) -> const char* { return std::string(n) == "HTTPS_PROXY" ? purl.c_str() : nullptr; };
        FetchResult r = Perform("GET", s.Url("localhost", "ok.http"), {}, "", t);
        CHECK(r.ok && r.response.body == "hello");
        CHECK(px.srv.Count() == 1 && px.srv.Request(0).rfind("CONNECT localhost:" + std::to_string(s.port) + " HTTP/1.1", 0) == 0);
        // proxy path with a bad certificate still refused
        SServer bad; CHECK(bad.Start(docs, certs + "/expired.crt", certs + "/expired.key"));
        r = Perform("GET", bad.Url("localhost", "ok.http"), {}, "", t);
        CHECK_ERR(r, Err::Tls);
        px.srv.Stop();
    }
    {   // proxy tests that need TLS
        SServer s; CHECK(s.Start(docs, certs + "/good.crt", certs + "/good.key"));
        TestProxy(s.Url("localhost", "ok.http"), ca);
    }
    Sh("rm -rf '" + dir + "'");
}

// Hostile servers over real sockets: every cap and limit must hold, fast, and without the client hanging.
static void TestHostile() {
    Server s;
    std::atomic<int> hops{0};
    s.handler = [&](int c, const std::string& head, const std::string&) {
        std::string path = PathOf(head);
        if (path == "/headline") {   // one header line that never ends (no newline), far past the head cap
            SendAll(c, "HTTP/1.1 200 OK\r\nX-Long: ");
            std::string z(8192, 'a'); for (int i = 0; i < 64 && !s.stop; ++i) SendAll(c, z);
        } else if (path == "/headmany") {
            std::string h = "HTTP/1.1 200 OK\r\n"; for (int i = 0; i < 150; ++i) h += "X-" + std::to_string(i) + ": v\r\n";
            SendAll(c, h + "Content-Length: 0\r\n\r\n");
        } else if (path == "/headbytes") {   // 90 headers of ~1 KiB: under the count cap, over the byte cap
            std::string h = "HTTP/1.1 200 OK\r\n"; for (int i = 0; i < 90; ++i) h += "X-" + std::to_string(i) + ": " + std::string(1000, 'v') + "\r\n";
            SendAll(c, h + "Content-Length: 0\r\n\r\n");
        } else if (path == "/status") SendAll(c, "HTTP/1.1 200 " + std::string(6000, 'R') + "\r\nContent-Length: 0\r\n\r\n");
        else if (path == "/interim") { for (int i = 0; i < 40; ++i) SendAll(c, "HTTP/1.1 102 Processing\r\n\r\n"); SendAll(c, Ok("late")); }
        else if (path == "/trailers") {
            std::string t = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n1\r\nx\r\n0\r\n";
            for (int i = 0; i < 400; ++i) t += "X-T" + std::to_string(i) + ": " + std::string(60, 't') + "\r\n";
            SendAll(c, t + "\r\n");
        } else if (path == "/endless") {   // no length, never stops: the body cap must end it
            SendAll(c, "HTTP/1.1 200 OK\r\n\r\n"); std::string z(65536, 'e'); for (int i = 0; i < 2000 && !s.stop; ++i) SendAll(c, z);
        } else if (path == "/endlesschunks") {   // many tiny chunks, never the last one
            SendAll(c, "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n");
            for (int i = 0; i < 200000 && !s.stop; ++i) { std::string b; for (int k = 0; k < 64; ++k) b += "1\r\nx\r\n"; SendAll(c, b); }
        } else if (path == "/hugeloc") SendAll(c, "HTTP/1.1 302 Found\r\nLocation: /" + std::string(20000, 'a') + "\r\nContent-Length: 0\r\n\r\n");
        else if (path == "/hop") { ++hops; SendAll(c, "HTTP/1.1 302 Found\r\nLocation: /hop\r\nContent-Length: 0\r\n\r\n"); }
        else if (path == "/redirbody") {   // a redirect that carries an enormous body
            SendAll(c, "HTTP/1.1 302 Found\r\nLocation: /ok\r\nContent-Length: 104857600\r\n\r\n"); std::string z(65536, 'r'); for (int i = 0; i < 1600 && !s.stop; ++i) SendAll(c, z);
        } else if (path == "/ok") SendAll(c, Ok("hello"));
        else if (path == "/stall") { SendAll(c, "HTTP/1.1 200 OK\r\nContent-Length: 100\r\n\r\nhello"); while (!s.stop) std::this_thread::sleep_for(std::chrono::milliseconds(20)); }
        else if (path == "/stallhead") { SendAll(c, "HTTP/1.1 200 OK\r\nX-A: b\r\n"); while (!s.stop) std::this_thread::sleep_for(std::chrono::milliseconds(20)); }
        else SendAll(c, "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n");
    };
    s.Start();
    const std::string base = "http://127.0.0.1:" + std::to_string(s.port);
    auto ms = [](Clock::time_point t0) { return (long)std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t0).count(); };
    NetOptions o = Opts(); FetchResult r; Clock::time_point t0;

    // response head limits
    t0 = Clock::now(); r = Perform("GET", base + "/headline", {}, "", o);
    CHECK(!r.ok && (r.err == Err::TooLarge || r.err == Err::BadResponse)); CHECK(ms(t0) < 2500);
    r = Perform("GET", base + "/headmany", {}, "", o);  CHECK_ERR(r, Err::TooLarge);
    r = Perform("GET", base + "/headbytes", {}, "", o); CHECK_ERR(r, Err::TooLarge);
    r = Perform("GET", base + "/status", {}, "", o);    CHECK_ERR(r, Err::BadResponse);
    r = Perform("GET", base + "/interim", {}, "", o);   CHECK_ERR(r, Err::BadResponse);
    r = Perform("GET", base + "/trailers", {}, "", o);  CHECK_ERR(r, Err::TooLarge);
    {   // the limits are the caller's to tighten
        NetOptions t = Opts(); t.limits.maxHeaders = 3;
        r = Perform("GET", base + "/headmany", {}, "", t); CHECK_ERR(r, Err::TooLarge);
    }

    // body caps hold for unframed and chunked bodies, and the data handed to a streaming caller never passes the cap
    {
        NetOptions t = Opts(); t.limits.maxBodyBytes = 1u << 20;
        t0 = Clock::now(); r = Perform("GET", base + "/endless", {}, "", t);
        CHECK_ERR(r, Err::TooLarge); CHECK(ms(t0) < 4000);
        uint64_t got = 0; FetchHooks h; h.onData = [&](const char*, size_t n) { got += n; return true; };
        r = Perform("GET", base + "/endless", {}, "", t, h);
        CHECK_ERR(r, Err::TooLarge); CHECK(got <= t.limits.maxBodyBytes);
        t0 = Clock::now(); r = Perform("GET", base + "/endlesschunks", {}, "", t);
        CHECK_ERR(r, Err::TooLarge); CHECK(ms(t0) < 8000);
    }
    {   // the caller can abort a download at any point
        FetchHooks h; uint64_t got = 0; h.onData = [&](const char*, size_t n) { got += n; return got < 100000; };
        NetOptions t = Opts();
        r = Perform("GET", base + "/endless", {}, "", t, h);
        CHECK_ERR(r, Err::Aborted); CHECK(got < 100000 + 65536 * 2);
    }

    // redirects: a hop cap that can be tightened, a huge Location, an enormous body behind a redirect
    {
        NetOptions t = Opts(); t.limits.maxRedirects = 3; hops = 0;
        r = Perform("GET", base + "/hop", {}, "", t);
        CHECK_ERR(r, Err::TooManyRedirects); CHECK(hops == 4);   // the first request plus exactly three followed hops
        hops = 0; r = Perform("GET", base + "/hop", {}, "", Opts());
        CHECK_ERR(r, Err::TooManyRedirects); CHECK(hops == 11);
    }
    r = Perform("GET", base + "/hugeloc", {}, "", o); CHECK(!r.ok);
    t0 = Clock::now(); r = Perform("GET", base + "/redirbody", {}, "", o);
    CHECK(ms(t0) < 4000);   // the redirect's own body is capped (1 MiB), whatever the outcome

    // timeouts: silence in the middle of the head, in the middle of the body, and the total limit
    {
        NetOptions t = Opts(); t.ioMs = 400;
        t0 = Clock::now(); r = Perform("GET", base + "/stallhead", {}, "", t); CHECK_ERR(r, Err::Timeout); CHECK(ms(t0) < 2000);
        t0 = Clock::now(); r = Perform("GET", base + "/stall", {}, "", t);     CHECK_ERR(r, Err::Timeout); CHECK(ms(t0) < 2000);
        t = Opts(); t.totalMs = 500; t.ioMs = 20000;
        t0 = Clock::now(); r = Perform("GET", base + "/stall", {}, "", t);     CHECK_ERR(r, Err::Timeout); CHECK(ms(t0) < 2000);
    }
    r = Perform("GET", base + "/ok", {}, "", o); CHECK(r.ok && r.response.body == "hello");   // the server is still fine afterwards
    s.Stop();
}

// Certificate verification can not be switched off: not by options, not by any environment variable we or other
// tools know of, and the source never asks OpenSSL for SSL_VERIFY_NONE.
static void TestNoVerifyBypass(const std::string& srcDir) {
    std::ifstream f(srcDir + "/engine/include/okay/Platform/OwnHttpNet.hpp", std::ios::binary);
    std::stringstream ss; ss << f.rdbuf(); std::string src = ss.str();
    CHECK(!src.empty());
    if (!src.empty()) {
        CHECK(src.find("VERIFY_NONE") == std::string::npos);
        CHECK(src.find("SSL_set_verify") == std::string::npos);
        CHECK(src.find("set_cert_verify_callback") == std::string::npos);
        CHECK(src.find("SSL_CTX_set_verify(ctx, 1 /* SSL_VERIFY_PEER */") != std::string::npos);
        size_t n = 0, p = 0; while ((p = src.find("SSL_CTX_set_verify(", p)) != std::string::npos) { ++n; p += 5; }
        CHECK(n == 1);   // exactly one call, with SSL_VERIFY_PEER
    }
}


// ---- keep-alive, HEAD, conditional requests, cookies, hostile servers and proxies (loopback) -------------------
// A server that answers several requests per connection. handler(connNo, nth request on this connection, head, body, fd)
// sends the reply itself and returns false to close the connection afterwards.
struct KaServer {
    int fd = -1, port = 0;
    std::atomic<bool> stop{false};
    std::atomic<int> connections{0}, requests{0};
    std::thread acceptor;
    std::mutex mu;
    std::vector<std::thread> workers;
    std::vector<std::string> heads;
    std::function<bool(int, int, const std::string&, const std::string&, int)> handler;
    void Start() {
        fd = Listen(port);
        acceptor = std::thread([this] {
            while (!stop) {
                pollfd p{fd, POLLIN, 0};
                if (::poll(&p, 1, 50) <= 0) continue;
                int c = ::accept(fd, nullptr, nullptr);
                if (c < 0) continue;
                const int no = ++connections;
                std::lock_guard<std::mutex> lk(mu);
                workers.emplace_back([this, c, no] { Serve(c, no); ::close(c); });
            }
        });
    }
    void Serve(int c, int no) {
        std::string in; char b[4096];
        for (int nth = 0; !stop; ++nth) {
            while (in.find("\r\n\r\n") == std::string::npos) {
                pollfd p{c, POLLIN, 0};
                if (::poll(&p, 1, 100) <= 0) { if (stop) return; continue; }
                ssize_t n = ::recv(c, b, sizeof b, 0); if (n <= 0) return; in.append(b, size_t(n));
            }
            size_t he = in.find("\r\n\r\n");
            std::string head = in.substr(0, he + 4); in.erase(0, he + 4);
            size_t cl = 0, pos = head.find("Content-Length: ");
            if (pos != std::string::npos) cl = size_t(std::atoll(head.c_str() + pos + 16));
            while (in.size() < cl) {
                pollfd p{c, POLLIN, 0};
                if (::poll(&p, 1, 100) <= 0) { if (stop) return; continue; }
                ssize_t n = ::recv(c, b, sizeof b, 0); if (n <= 0) return; in.append(b, size_t(n));
            }
            std::string body = in.substr(0, cl); in.erase(0, cl);
            { std::lock_guard<std::mutex> lk(mu); heads.push_back(head); }
            ++requests;
            if (!handler(no, nth, head, body, c)) return;
        }
    }
    std::string Head(size_t i) { std::lock_guard<std::mutex> lk(mu); return i < heads.size() ? heads[i] : std::string(); }
    void Stop() {
        if (fd < 0) return;
        stop = true;
        if (acceptor.joinable()) acceptor.join();
        for (auto& w : workers) if (w.joinable()) w.join();
        workers.clear(); ::close(fd); fd = -1;
    }
    ~KaServer() { Stop(); }
};
static std::string KaOk(const std::string& body, const std::string& extra = "") {
    return "HTTP/1.1 200 OK\r\nContent-Length: " + std::to_string(body.size()) + "\r\n" + extra + "\r\n" + body;
}

static void TestKeepAliveNet() {
    NetOptions o = Opts(); o.keepAlive = true;
    detail::DropIdleConnections();
    {   // sequential requests share one TCP connection
        KaServer s; s.handler = [](int, int, const std::string& h, const std::string&, int c) { SendAll(c, KaOk("r:" + PathOf(h))); return true; };
        s.Start();
        const std::string base = "http://127.0.0.1:" + std::to_string(s.port);
        for (int i = 0; i < 5; ++i) {
            FetchResult r = Perform("GET", base + "/p" + std::to_string(i), {}, "", o);
            CHECK(r.ok && r.response.body == "r:/p" + std::to_string(i) && r.reusedConnections == (i ? 1 : 0));
        }
        CHECK(s.connections == 1 && s.requests == 5 && detail::IdleCount() == 1);
        CHECK(s.Head(1).find("Connection: close") == std::string::npos);
        // another port is another connection; keepAlive off never reuses and says close
        KaServer t; t.handler = s.handler; t.Start();
        FetchResult r = Perform("GET", "http://127.0.0.1:" + std::to_string(t.port) + "/x", {}, "", o);
        CHECK(r.ok && t.connections == 1 && s.connections == 1);
        NetOptions off = Opts();
        r = Perform("GET", base + "/off", {}, "", off);
        CHECK(r.ok && s.connections == 2 && s.Head(5).find("Connection: close") != std::string::npos);
        detail::DropIdleConnections();
        t.Stop(); s.Stop();
    }
    {   // the server closes the idle connection: it is noticed (or the retry covers it); the call works either way
        KaServer s; s.handler = [](int, int, const std::string&, const std::string&, int c) { SendAll(c, KaOk("ok")); return false; };
        s.Start();
        const std::string base = "http://127.0.0.1:" + std::to_string(s.port);
        FetchResult a = Perform("GET", base + "/a", {}, "", o);
        std::this_thread::sleep_for(std::chrono::milliseconds(150));   // let the FIN arrive
        FetchResult b = Perform("GET", base + "/b", {}, "", o);
        CHECK(a.ok && b.ok && b.response.body == "ok" && s.connections == 2 && b.reusedConnections == 0);
        detail::DropIdleConnections(); s.Stop();
    }
    {   // the close races our request: the server swallows the second request on a connection and hangs up. GET is retried once.
        KaServer s;
        s.handler = [](int, int nth, const std::string&, const std::string&, int c) { if (nth == 1) return false; SendAll(c, KaOk("ok")); return true; };
        s.Start();
        const std::string base = "http://127.0.0.1:" + std::to_string(s.port);
        FetchResult a = Perform("GET", base + "/a", {}, "", o);
        FetchResult b = Perform("GET", base + "/b", {}, "", o);
        CHECK(a.ok && b.ok && b.response.body == "ok" && b.staleRetries == 1 && s.connections == 2);
        // POST does not ride on a kept connection at all, so it is sent once and never replayed
        FetchResult p = Perform("POST", base + "/post", {}, "data", o);
        CHECK(p.ok && p.reusedConnections == 0 && s.connections == 3);
        detail::DropIdleConnections(); s.Stop();
    }
    {   // a POST that hits a server which hangs up is an error after ONE attempt
        KaServer s; s.handler = [](int, int, const std::string&, const std::string&, int) { return false; };
        s.Start();
        FetchResult p = Perform("POST", "http://127.0.0.1:" + std::to_string(s.port) + "/post", {}, "data", o);
        CHECK(!p.ok && s.requests == 1 && s.connections == 1);
        s.Stop();
    }
    {   // a new deadline per call: the first call's 300 ms total is long over when the second call uses the idle connection
        KaServer s; s.handler = [](int, int, const std::string&, const std::string&, int c) { SendAll(c, KaOk("ok")); return true; };
        s.Start();
        const std::string base = "http://127.0.0.1:" + std::to_string(s.port);
        NetOptions q = o; q.totalMs = 300;
        FetchResult a = Perform("GET", base + "/a", {}, "", q);
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
        FetchResult b = Perform("GET", base + "/b", {}, "", o);
        CHECK(a.ok && b.ok && b.reusedConnections == 1 && s.connections == 1);
        detail::DropIdleConnections(); s.Stop();
    }
    {   // cancel and silence limits apply on a reused connection too
        KaServer s;
        s.handler = [&](int, int nth, const std::string&, const std::string&, int c) {
            if (nth == 0) { SendAll(c, KaOk("ok")); return true; }
            while (!s.stop) std::this_thread::sleep_for(std::chrono::milliseconds(20));   // never answers
            return false;
        };
        s.Start();
        const std::string base = "http://127.0.0.1:" + std::to_string(s.port);
        Perform("GET", base + "/a", {}, "", o);
        NetOptions q = o; std::atomic<int> polls{0}; q.cancel = [&] { return ++polls > 4; };
        auto t0 = std::chrono::steady_clock::now();
        FetchResult b = Perform("GET", base + "/b", {}, "", q);
        CHECK_ERR(b, Err::Aborted);
        CHECK(std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() < 5.0);
        detail::DropIdleConnections();
        Perform("GET", base + "/a2", {}, "", o);   // new connection (the aborted one was dropped)
        NetOptions w = o; w.ioMs = 400;
        t0 = std::chrono::steady_clock::now();
        FetchResult c2 = Perform("GET", base + "/b2", {}, "", w);
        CHECK_ERR(c2, Err::Timeout);
        CHECK(std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() < 5.0);
        detail::DropIdleConnections(); s.Stop();
    }
    {   // idle connections belong to the thread; when it ends they are closed (the server sees the end of the connection)
        KaServer s; std::atomic<int> ended{0};
        s.handler = [&](int, int, const std::string&, const std::string&, int c) {
            SendAll(c, KaOk("ok"));
            for (int i = 0; i < 100 && !s.stop; ++i) { pollfd p{c, POLLIN, 0}; if (::poll(&p, 1, 50) > 0) { char b[8]; if (::recv(c, b, sizeof b, 0) <= 0) { ++ended; return false; } } }
            return false;
        };
        s.Start();
        std::thread([&] { NetOptions q = Opts(); q.keepAlive = true; Perform("GET", "http://127.0.0.1:" + std::to_string(s.port) + "/a", {}, "", q); }).join();
        for (int i = 0; i < 40 && ended == 0; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(50));
        CHECK(ended == 1);
        s.Stop();
    }
    {   // the pool holds at most 8 idle connections
        std::vector<std::unique_ptr<KaServer>> servers;
        for (int i = 0; i < 10; ++i) {
            servers.push_back(std::make_unique<KaServer>());
            servers.back()->handler = [](int, int, const std::string&, const std::string&, int c) { SendAll(c, KaOk("ok")); return true; };
            servers.back()->Start();
            FetchResult r = Perform("GET", "http://127.0.0.1:" + std::to_string(servers.back()->port) + "/", {}, "", o);
            CHECK(r.ok);
        }
        CHECK(detail::IdleCount() == detail::kMaxIdleConnections);
        detail::DropIdleConnections();
        CHECK(detail::IdleCount() == 0);
        for (auto& sv : servers) sv->Stop();
    }
}

static void TestHeadConditionalCookies() {
    KaServer s;
    s.handler = [&](int, int, const std::string& h, const std::string&, int c) {
        const std::string path = PathOf(h);
        const bool head = h.rfind("HEAD ", 0) == 0;
        if (path == "/res") {
            if (h.find("If-None-Match: \"v7\"") != std::string::npos) { SendAll(c, "HTTP/1.1 304 Not Modified\r\nETag: \"v7\"\r\nLast-Modified: Wed, 01 Jan 2025 00:00:00 GMT\r\n\r\n"); return true; }
            if (h.find("If-Modified-Since: Wed, 01 Jan 2025 00:00:00 GMT") != std::string::npos) { SendAll(c, "HTTP/1.1 304 Not Modified\r\n\r\n"); return true; }
            SendAll(c, "HTTP/1.1 200 OK\r\nETag: \"v7\"\r\nLast-Modified: Wed, 01 Jan 2025 00:00:00 GMT\r\nContent-Length: 11\r\n\r\n" + std::string(head ? "" : "fresh bytes"));
        } else if (path == "/head-gz") SendAll(c, "HTTP/1.1 200 OK\r\nContent-Encoding: gzip\r\nContent-Length: 999\r\n\r\n");
        else if (path == "/set") SendAll(c, "HTTP/1.1 302 Found\r\nSet-Cookie: sid=abc; Path=/\r\nSet-Cookie: gone=1; Max-Age=0\r\nLocation: /need\r\nContent-Length: 0\r\n\r\n");
        else if (path == "/need") { const size_t p = h.find("Cookie: "); SendAll(c, KaOk(p == std::string::npos ? "none" : h.substr(p + 8, h.find("\r\n", p) - p - 8))); }
        else SendAll(c, KaOk("?"));
        return true;
    };
    s.Start();
    const std::string base = "http://127.0.0.1:" + std::to_string(s.port);
    ::setenv("OKAY_OWN_HTTP", "1", 1); ::unsetenv("OKAY_HTTP_CURL"); ::setenv("NO_PROXY", "127.0.0.1", 1); ::unsetenv("HTTP_PROXY"); ::unsetenv("http_proxy");
    // HEAD: the length comes from the headers, no body is read, and a following GET on the same connection is not confused
    okay::http::RequestOptions ro;
    okay::http::ExResult r = okay::http::RequestEx("HEAD", base + "/res", {}, "", "", "", ro);
    CHECK(r.reached && r.status == 200 && r.body.empty() && r.totalSize == 11 && r.etag == "\"v7\"" && r.lastModified == "Wed, 01 Jan 2025 00:00:00 GMT");
    r = okay::http::RequestEx("GET", base + "/res", {}, "", "", "", ro);
    CHECK(r.reached && r.status == 200 && r.body == "fresh bytes" && s.connections == 1 && r.reusedConnections == 1);
    // HEAD with an output file never creates or truncates it
    char tmpl[] = "/tmp/okay_cond_XXXXXX"; int tfd = ::mkstemp(tmpl); ::close(tfd);
    WriteFile(tmpl, "keep me");
    r = okay::http::RequestEx("HEAD", base + "/res", {}, "", "", tmpl, ro);
    CHECK(r.reached && r.status == 200);
    { std::ifstream f(tmpl, std::ios::binary); std::string d((std::istreambuf_iterator<char>(f)), {}); CHECK(d == "keep me"); }
    // a HEAD answer that claims gzip and a length is still just headers
    r = okay::http::RequestEx("HEAD", base + "/head-gz", {}, "", "", "", ro);
    CHECK(r.reached && r.status == 200 && r.body.empty());
    // conditional: ETag and date validators produce 304, the file is left alone, and the validators come back
    okay::http::RequestOptions c1; c1.ifNoneMatch = "\"v7\"";
    r = okay::http::RequestEx("GET", base + "/res", {}, "", "", tmpl, c1);
    CHECK(r.reached && r.status == 304 && r.notModified && r.body.empty() && r.etag == "\"v7\"");
    okay::http::RequestOptions c2; c2.ifModifiedSince = "Wed, 01 Jan 2025 00:00:00 GMT";
    r = okay::http::RequestEx("GET", base + "/res", {}, "", "", tmpl, c2);
    CHECK(r.reached && r.status == 304 && r.notModified);
    { std::ifstream f(tmpl, std::ios::binary); std::string d((std::istreambuf_iterator<char>(f)), {}); CHECK(d == "keep me"); }
    okay::http::RequestOptions c3 = c1; c3.fileOnSuccessOnly = false;   // even when error bodies are wanted, a 304 never truncates
    r = okay::http::RequestEx("GET", base + "/res", {}, "", "", tmpl, c3);
    CHECK(r.reached && r.notModified);
    { std::ifstream f(tmpl, std::ios::binary); std::string d((std::istreambuf_iterator<char>(f)), {}); CHECK(d == "keep me"); }
    // a stale validator gets the full answer and the file is replaced
    okay::http::RequestOptions c4; c4.ifNoneMatch = "\"old\"";
    r = okay::http::RequestEx("GET", base + "/res", {}, "", "", tmpl, c4);
    CHECK(r.reached && r.status == 200 && !r.notModified);
    { std::ifstream f(tmpl, std::ios::binary); std::string d((std::istreambuf_iterator<char>(f)), {}); CHECK(d == "fresh bytes"); }
    // a validator with a line break is refused before any traffic
    okay::http::RequestOptions bad; bad.ifNoneMatch = "x\r\nX-Evil: 1";
    size_t before = size_t(s.requests.load());
    r = okay::http::RequestEx("GET", base + "/res", {}, "", "", "", bad);
    CHECK(!r.reached && size_t(s.requests.load()) == before);
    // cookies: off by default, on with the option; the deleted one is not sent; the jar content is reported
    r = okay::http::RequestEx("GET", base + "/set", {}, "", "", "", ro);
    CHECK(r.reached && r.body == "none" && r.cookies.empty());
    okay::http::RequestOptions ck; ck.cookies = true;
    r = okay::http::RequestEx("GET", base + "/set", {}, "", "", "", ck);
    CHECK(r.reached && r.body == "sid=abc" && r.cookies.size() == 1 && r.cookies[0] == "sid=abc");
    // cookies do not leak into the next call
    r = okay::http::RequestEx("GET", base + "/need", {}, "", "", "", ck);
    CHECK(r.reached && r.body == "none");
    detail::DropIdleConnections();
    ::unlink(tmpl); ::unsetenv("OKAY_OWN_HTTP"); ::unsetenv("NO_PROXY");
    s.Stop();
}

static void TestHostileNet() {
    NetOptions o = Opts(); o.keepAlive = true;
    detail::DropIdleConnections();
    KaServer s;
    s.handler = [&](int, int, const std::string& h, const std::string&, int c) {
        const std::string path = PathOf(h);
        if (path == "/ok") { SendAll(c, KaOk("ok")); return true; }
        if (path == "/short") { SendAll(c, "HTTP/1.1 200 OK\r\nContent-Length: 100\r\n\r\nshort"); return false; }          // wrong (too large) length, then close
        if (path == "/long") { SendAll(c, "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nabcdefghij"); return true; }        // body longer than stated
        if (path == "/trunc-chunk") { SendAll(c, "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n8\r\nabc"); return false; }
        if (path == "/glued") { SendAll(c, KaOk("one") + KaOk("TWO")); return true; }                                          // pipelined leftover
        if (path == "/loop") { SendAll(c, "HTTP/1.1 302 Found\r\nLocation: /loop\r\nContent-Length: 0\r\n\r\n"); return true; }
        if (path == "/pingpong-a") { SendAll(c, "HTTP/1.1 302 Found\r\nLocation: /pingpong-b\r\nContent-Length: 0\r\n\r\n"); return true; }
        if (path == "/pingpong-b") { SendAll(c, "HTTP/1.1 302 Found\r\nLocation: /pingpong-a\r\nContent-Length: 0\r\n\r\n"); return true; }
        if (path == "/dribble") {   // a head that never ends
            std::string head = "HTTP/1.1 200 OK\r\nX-Pad: ";
            for (char ch : head) { SendAll(c, std::string(1, ch)); std::this_thread::sleep_for(std::chrono::milliseconds(20)); }
            for (int i = 0; i < 100 && !s.stop; ++i) { SendAll(c, "a"); std::this_thread::sleep_for(std::chrono::milliseconds(150)); }
            return false;
        }
        if (path == "/silent") { while (!s.stop) std::this_thread::sleep_for(std::chrono::milliseconds(20)); return false; }
        if (path == "/cl-conflict") { SendAll(c, "HTTP/1.1 200 OK\r\nContent-Length: 2\r\nContent-Length: 3\r\n\r\nabc"); return false; }
        SendAll(c, KaOk("?")); return true;
    };
    s.Start();
    const std::string base = "http://127.0.0.1:" + std::to_string(s.port);
    FetchResult r = Perform("GET", base + "/short", {}, "", o);
    CHECK(!r.ok && detail::IdleCount() == 0);                                      // truncated: error, nothing pooled
    r = Perform("GET", base + "/trunc-chunk", {}, "", o);
    CHECK(!r.ok && detail::IdleCount() == 0);
    r = Perform("GET", base + "/cl-conflict", {}, "", o);
    CHECK_ERR(r, Err::BadResponse);
    r = Perform("GET", base + "/long", {}, "", o);                                 // longer than stated: the extra bytes poison the connection
    CHECK(r.ok && r.response.body == "abc");
    r = Perform("GET", base + "/ok", {}, "", o);
    CHECK(r.ok && r.response.body == "ok" && r.reusedConnections == 0);            // so the next call used a clean new connection
    r = Perform("GET", base + "/glued", {}, "", o);
    CHECK(r.ok && r.response.body == "one");
    r = Perform("GET", base + "/ok", {}, "", o);
    CHECK(r.ok && r.response.body == "ok" && r.reusedConnections == 0);            // never read "TWO" as an answer
    NetOptions lim = o; lim.limits.maxRedirects = 4;
    r = Perform("GET", base + "/loop", {}, "", lim);
    CHECK_ERR(r, Err::TooManyRedirects);
    r = Perform("GET", base + "/pingpong-a", {}, "", lim);
    CHECK_ERR(r, Err::TooManyRedirects);
    {   NetOptions w = o; w.totalMs = 800;                                            // a slow head hits the total time
        auto t0 = std::chrono::steady_clock::now();
        r = Perform("GET", base + "/dribble", {}, "", w);
        CHECK(!r.ok && (r.err == Err::Timeout || r.err == Err::TooLarge));
        CHECK(std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() < 5.0); }
    {   NetOptions w = o; w.ioMs = 300;
        r = Perform("GET", base + "/silent", {}, "", w);
        CHECK_ERR(r, Err::Timeout); }
    detail::DropIdleConnections();
    s.Stop();
}

// A proxy that misbehaves in a chosen way, for the CONNECT path (https needs libssl to get that far; skipped otherwise).
static void TestProxyHostile() {
    std::string why; NetOptions probe = Opts();
    if (!TlsAvailable(probe, why)) { ++g_skips; std::printf("SKIP proxy CONNECT cases: %s\n", why.c_str()); return; }
    std::atomic<int> mode{0};
    Server px;
    px.handler = [&](int c, const std::string&, const std::string&) {
        switch (mode.load()) {
        case 0: SendAll(c, "HTTP/1.1 407 Proxy Authentication Required\r\nProxy-Authenticate: Basic realm=x\r\nContent-Length: 0\r\n\r\n"); break;
        case 1: SendAll(c, "HTTP/1.1 502 Bad Gateway\r\nContent-Length: 0\r\n\r\n"); break;
        case 2: break;                                                             // hangs up without an answer
        case 3: SendAll(c, "garbage that is not http\r\n\r\n"); break;
        case 4: SendAll(c, "HTTP/1.1 200 Connection established\r\n\r\nHELLO-BEFORE-TLS"); break;   // data ahead of the handshake
        case 5: SendAll(c, "HTTP/1.1 301 Moved\r\nLocation: http://127.0.0.1:1/\r\nContent-Length: 0\r\n\r\n"); break;   // a redirect is not a tunnel
        case 6: SendAll(c, "HTTP/1.1 200 Connection established\r\n\r\n"); for (int i = 0; i < 100 && !px.stop; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(20)); break;   // tunnel that never speaks TLS
        }
    };
    px.Start();
    const std::string purl = "http://127.0.0.1:" + std::to_string(px.port);
    auto envWith = [&](std::map<std::string, std::string> m) {
        return EnvFn([m](const char* n) -> const char* { auto it = m.find(n); return it == m.end() ? nullptr : it->second.c_str(); });
    };
    NetOptions o = Opts(); o.keepAlive = true;
    o.env = envWith({{"HTTPS_PROXY", purl}});
    const std::string target = "https://example.invalid/x";
    const int want[] = {407, 502};
    for (int m = 0; m < 2; ++m) {
        mode = m;
        FetchResult r = Perform("GET", target, {}, "", o);
        CHECK_ERR(r, Err::Proxy);
        CHECK(r.message.find(std::to_string(want[m])) != std::string::npos);
    }
    for (int m = 2; m <= 5; ++m) {
        mode = m;
        FetchResult r = Perform("GET", target, {}, "", o);
        CHECK_ERR(r, Err::Proxy);
    }
    mode = 4; { FetchResult r = Perform("GET", target, {}, "", o); CHECK(r.message.find("before the TLS handshake") != std::string::npos); }
    mode = 6; { NetOptions w = o; w.ioMs = 400; w.totalMs = 3000; FetchResult r = Perform("GET", target, {}, "", w); CHECK(!r.ok && (r.err == Err::Timeout || r.err == Err::Tls)); }
    CHECK(detail::IdleCount() == 0);   // nothing from a refused tunnel was kept
    px.Stop();
}

int main(int argc, char** argv) {
    std::signal(SIGPIPE, SIG_IGN);   // the transports must not depend on this, but test servers write to closed sockets
    std::string script = argc > 1 ? argv[1] : "tests/scripts/gen_http_test_certs.sh";
    TestNoProxyList();
    TestPlain();
    TestHostile();
    TestNativeHttp();
    TestDownloadFeatures();
    TestKeepAliveNet();
    TestHeadConditionalCookies();
    TestHostileNet();
    TestProxyHostile();
    {   // proxy tests that do not need TLS
        NetOptions p = Opts(); std::string why;
        if (!TlsAvailable(p, why)) TestProxy("", "");
    }
    TestTls(script);
    TestNoVerifyBypass(argc > 2 ? argv[2] : ".");
    std::printf("own_http_net: %d checks, %d failed, %d skipped sections\n", g_checks, g_fail, g_skips);
    return g_fail ? 1 : 0;
}
