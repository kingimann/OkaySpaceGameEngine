// Integration tests for okay::ownhttp transports (OwnHttpNet.hpp) against local servers:
//  - a plain HTTP server written here (redirects, chunked, big body, slow, truncated, ...),
//  - a tiny CONNECT / absolute-form proxy written here,
//  - `openssl s_server -HTTP` as the TLS server, with certificates from
//    tests/scripts/gen_http_test_certs.sh (valid, wrong host name, expired, untrusted CA).
// The TLS part is skipped (exit 0, "SKIP") when the openssl program or libssl is missing,
// unless OKAY_REQUIRE_TLS_TESTS=1.
//   test_own_http_net <path to gen_http_test_certs.sh>
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

static void WriteFile(const std::string& p, const std::string& d) { std::ofstream(p, std::ios::binary) << d; }

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
    std::system(("mkdir -p '" + docs + "'").c_str());

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
    std::system(("rm -rf '" + dir + "'").c_str());
}

int main(int argc, char** argv) {
    std::signal(SIGPIPE, SIG_IGN);   // the transports must not depend on this, but test servers write to closed sockets
    std::string script = argc > 1 ? argv[1] : "tests/scripts/gen_http_test_certs.sh";
    TestNoProxyList();
    TestPlain();
    {   // proxy tests that do not need TLS
        NetOptions p = Opts(); std::string why;
        if (!TlsAvailable(p, why)) TestProxy("", "");
    }
    TestTls(script);
    std::printf("own_http_net: %d checks, %d failed, %d skipped sections\n", g_checks, g_fail, g_skips);
    return g_fail ? 1 : 0;
}
