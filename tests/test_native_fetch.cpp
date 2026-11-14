// The call-site helpers of NativeFetch.hpp (own HTTP client first, curl as the fallback) against loopback servers:
// downloads (success, HTTP errors, retries, truncated and over-long bodies, size caps, cancel, disk errors, redirect loops,
// slow servers), HEAD size probes, link-preview prefix fetches, keep-alive across sequential calls, and a proxy that
// refuses CONNECT. PATH is emptied, so a silent curl fallback would show up as a failure; the helpers never spawn anything.
// Built only with -DOKAY_OWN_HTTP=ON on Linux. Standalone: g++ -std=c++17 -DOKAY_OWN_HTTP=1 -Iengine/include -Ilauncher tests/test_native_fetch.cpp -ldl -lpthread
#include "okay/Platform/NativeFetch.hpp"
#include "NetGuard.hpp"

#include <arpa/inet.h>
#include <atomic>
#include <cstdio>
#include <fstream>
#include <map>
#include <mutex>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

using namespace okay::http;
namespace fs = std::filesystem;
static int g_fail = 0, g_checks = 0, g_skips = 0;
#define CHECK(c) do { ++g_checks; if (!(c)) { ++g_fail; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static void SendAll(int fd, const std::string& s) { size_t o = 0; while (o < s.size()) { ssize_t n = ::send(fd, s.data() + o, s.size() - o, MSG_NOSIGNAL); if (n <= 0) return; o += size_t(n); } }
static std::string Slurp(const std::string& p) { std::ifstream f(p, std::ios::binary); return std::string((std::istreambuf_iterator<char>(f)), {}); }
static double Secs(std::chrono::steady_clock::time_point t0) { return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(); }

struct Srv {
    int fd = -1, port = 0;
    std::atomic<bool> stop{false};
    std::atomic<int> connections{0}, requests{0};
    std::thread acceptor; std::mutex mu; std::vector<std::thread> workers; std::vector<std::string> heads;
    std::function<bool(int, int, const std::string&, int)> handler;   // (conn no, nth on conn, head, fd) -> keep open
    void Start() {
        fd = ::socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in a{}; a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (::bind(fd, (sockaddr*)&a, sizeof a) != 0 || ::listen(fd, 16) != 0) { std::perror("bind"); std::exit(2); }
        socklen_t l = sizeof a; ::getsockname(fd, (sockaddr*)&a, &l); port = ntohs(a.sin_port);
        acceptor = std::thread([this] {
            while (!stop) {
                pollfd p{fd, POLLIN, 0}; if (::poll(&p, 1, 50) <= 0) continue;
                int c = ::accept(fd, nullptr, nullptr); if (c < 0) continue;
                const int no = ++connections; std::lock_guard<std::mutex> lk(mu);
                workers.emplace_back([this, c, no] { Serve(c, no); ::close(c); });
            }
        });
    }
    void Serve(int c, int no) {
        std::string in; char b[4096];
        for (int nth = 0; !stop; ++nth) {
            while (in.find("\r\n\r\n") == std::string::npos) {
                pollfd p{c, POLLIN, 0}; if (::poll(&p, 1, 100) <= 0) { if (stop) return; continue; }
                ssize_t n = ::recv(c, b, sizeof b, 0); if (n <= 0) return; in.append(b, size_t(n));
            }
            size_t he = in.find("\r\n\r\n"); std::string head = in.substr(0, he + 4); in.erase(0, he + 4);
            { std::lock_guard<std::mutex> lk(mu); heads.push_back(head); }
            ++requests;
            if (!handler(no, nth, head, c)) return;
        }
    }
    std::string Head(size_t i) { std::lock_guard<std::mutex> lk(mu); return i < heads.size() ? heads[i] : std::string(); }
    void Stop() { if (fd < 0) return; stop = true; if (acceptor.joinable()) acceptor.join(); for (auto& w : workers) if (w.joinable()) w.join(); workers.clear(); ::close(fd); fd = -1; }
    ~Srv() { Stop(); }
};
static std::string PathOf(const std::string& h) { size_t a = h.find(' '), b = h.find(' ', a + 1); return h.substr(a + 1, b - a - 1); }
static std::string Ok(const std::string& body, const std::string& extra = "") { return "HTTP/1.1 200 OK\r\nContent-Length: " + std::to_string(body.size()) + "\r\n" + extra + "\r\n" + body; }

static void TestDownload() {
    std::atomic<int> flaky{0};
    Srv s;
    const std::string payload(5000, 'p');
    s.handler = [&](int, int, const std::string& h, int c) {
        const std::string path = PathOf(h);
        if (path == "/ok") { SendAll(c, Ok(payload)); return true; }
        if (path == "/redir") { SendAll(c, "HTTP/1.1 302 Found\r\nLocation: /ok\r\nContent-Length: 0\r\n\r\n"); return true; }
        if (path == "/loop") { SendAll(c, "HTTP/1.1 302 Found\r\nLocation: /loop\r\nContent-Length: 0\r\n\r\n"); return true; }
        if (path == "/missing") { SendAll(c, "HTTP/1.1 404 Not Found\r\nContent-Length: 4\r\n\r\nnope"); return true; }
        if (path == "/flaky") { if (++flaky <= 2) SendAll(c, "HTTP/1.1 503 Busy\r\nContent-Length: 0\r\n\r\n"); else SendAll(c, Ok(payload)); return true; }
        if (path == "/always503") { SendAll(c, "HTTP/1.1 503 Busy\r\nContent-Length: 0\r\n\r\n"); return true; }
        if (path == "/trunc") { SendAll(c, "HTTP/1.1 200 OK\r\nContent-Length: 100000\r\n\r\nshort"); return false; }
        if (path == "/chunked-big") { SendAll(c, "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"); std::string z(1000, 'z'); for (int i = 0; i < 50 && !s.stop; ++i) SendAll(c, "3e8\r\n" + z + "\r\n"); SendAll(c, "0\r\n\r\n"); return true; }
        if (path == "/long-body") { SendAll(c, "HTTP/1.1 200 OK\r\n\r\n" + std::string(50000, 'L')); return false; }   // no length: until close
        if (path == "/hang") { while (!s.stop) std::this_thread::sleep_for(std::chrono::milliseconds(20)); return false; }
        if (path == "/drip") { SendAll(c, "HTTP/1.1 200 OK\r\nContent-Length: 100000\r\n\r\n"); for (int i = 0; i < 400 && !s.stop; ++i) { SendAll(c, std::string(100, 'd')); std::this_thread::sleep_for(std::chrono::milliseconds(25)); } return false; }
        if (path == "/garbage") { SendAll(c, "this is not http\r\n\r\n"); return false; }
        if (path == "/bad-len") { SendAll(c, "HTTP/1.1 200 OK\r\nContent-Length: 12abc\r\n\r\nxx"); return false; }
        if (path == "/dup-len") { SendAll(c, "HTTP/1.1 200 OK\r\nContent-Length: 2\r\nContent-Length: 5\r\n\r\nabcde"); return false; }
        SendAll(c, Ok("?")); return true;
    };
    s.Start();
    const std::string base = "http://127.0.0.1:" + std::to_string(s.port);
    char tmpl[] = "/tmp/okay_nf_XXXXXX"; if (!::mkdtemp(tmpl)) std::exit(2);
    const std::string out = std::string(tmpl) + "/file.bin";
    OwnDownloadSpec sp; sp.userAgent = "OkaySpace-Test"; sp.retryDelayMs = 10; sp.headers = {"Cache-Control: no-cache", "Pragma: no-cache"};
    std::vector<uint64_t> seen; sp.onBytes = [&](uint64_t n) { seen.push_back(n); };

    OwnDownloadResult r = OwnDownload(base + "/ok", out, sp);
    CHECK(r.outcome == OwnOutcome::Ok && r.status == 200 && r.bytes == 5000 && Slurp(out) == payload && !seen.empty() && seen.back() == 5000);
    CHECK(s.Head(0).find("User-Agent: OkaySpace-Test") != std::string::npos && s.Head(0).find("Cache-Control: no-cache") != std::string::npos);
    r = OwnDownload(base + "/redir", out, sp);
    CHECK(r.outcome == OwnOutcome::Ok && Slurp(out) == payload);
    CHECK(s.connections == 1);   // keep-alive: both downloads (and the redirect hop) used one connection
    r = OwnDownload(base + "/missing", out, sp);
    CHECK(r.outcome == OwnOutcome::HttpError && r.status == 404 && !fs::exists(out) && r.attempts == 1);
    {   OwnDownloadSpec rs = sp; rs.retries = 2;                           // 404 is final; 503 is retried
        r = OwnDownload(base + "/missing", out, rs); CHECK(r.outcome == OwnOutcome::HttpError && r.attempts == 1);
        r = OwnDownload(base + "/flaky", out, rs);   CHECK(r.outcome == OwnOutcome::Ok && r.attempts == 3 && Slurp(out) == payload);
        r = OwnDownload(base + "/always503", out, rs); CHECK(r.outcome == OwnOutcome::HttpError && r.status == 503 && r.attempts == 3 && !fs::exists(out));
        r = OwnDownload(base + "/trunc", out, rs);   CHECK(r.outcome == OwnOutcome::NotHandled && r.attempts == 3 && !fs::exists(out)); }
    r = OwnDownload(base + "/trunc", out, sp);                              // the answer died half way: not a clear answer
    CHECK(r.outcome == OwnOutcome::NotHandled && !r.error.empty() && !fs::exists(out) && r.attempts == 1);
    r = OwnDownload(base + "/garbage", out, sp);   CHECK(r.outcome == OwnOutcome::NotHandled && !fs::exists(out));
    r = OwnDownload(base + "/bad-len", out, sp);   CHECK(r.outcome == OwnOutcome::NotHandled && !fs::exists(out));
    r = OwnDownload(base + "/dup-len", out, sp);   CHECK(r.outcome == OwnOutcome::NotHandled && !fs::exists(out));
    r = OwnDownload(base + "/loop", out, sp);      CHECK(r.outcome == OwnOutcome::NotHandled && r.error.find("too-many-redirects") == 0 && !fs::exists(out));
    {   OwnDownloadSpec cap = sp; cap.maxBytes = 1000;                       // announced too large, streamed too large, no length at all
        r = OwnDownload(base + "/ok", out, cap);           CHECK(r.outcome == OwnOutcome::TooBig && !fs::exists(out));
        r = OwnDownload(base + "/chunked-big", out, cap);  CHECK(r.outcome == OwnOutcome::TooBig && !fs::exists(out));
        r = OwnDownload(base + "/long-body", out, cap);    CHECK(r.outcome == OwnOutcome::TooBig && !fs::exists(out)); }
    {   OwnDownloadSpec sl = sp; sl.ioMs = 300; sl.totalMs = 20000;           // a silent server
        auto t0 = std::chrono::steady_clock::now();
        r = OwnDownload(base + "/hang", out, sl);
        CHECK(r.outcome == OwnOutcome::NotHandled && r.error.rfind("timeout", 0) == 0 && Secs(t0) < 5.0); }
    {   OwnDownloadSpec sl = sp; sl.totalMs = 600;                            // a slow trickle hits the total time
        auto t0 = std::chrono::steady_clock::now();
        r = OwnDownload(base + "/drip", out, sl);
        CHECK(r.outcome == OwnOutcome::NotHandled && !fs::exists(out) && Secs(t0) < 5.0); }
    {   OwnDownloadSpec cs = sp; std::atomic<int> polls{0}; cs.cancelled = [&] { return ++polls > 6; };   // cancel while the server is silent
        auto t0 = std::chrono::steady_clock::now();
        r = OwnDownload(base + "/hang", out, cs);
        CHECK(r.outcome == OwnOutcome::Cancelled && !fs::exists(out) && Secs(t0) < 5.0); }
    {   OwnDownloadSpec cs = sp; std::atomic<bool> flag{false}; uint64_t got = 0;   // cancel while data is flowing
        cs.onBytes = [&](uint64_t n) { got = n; if (n > 2000) flag = true; };
        cs.cancelled = [&] { return flag.load(); };
        auto t0 = std::chrono::steady_clock::now();
        r = OwnDownload(base + "/drip", out, cs);
        CHECK(r.outcome == OwnOutcome::Cancelled && !fs::exists(out) && got > 2000 && Secs(t0) < 5.0); }
    {   OwnDownloadSpec rs = sp; rs.retries = 3; rs.retryDelayMs = 100000; std::atomic<int> polls{0};   // a cancel also ends the wait between retries
        rs.cancelled = [&] { return polls.load() > 0; };
        std::thread t([&] { std::this_thread::sleep_for(std::chrono::milliseconds(300)); polls = 1; });
        auto t0 = std::chrono::steady_clock::now();
        r = OwnDownload(base + "/always503", out, rs);
        t.join();
        CHECK(r.outcome == OwnOutcome::Cancelled && Secs(t0) < 5.0); }
    r = OwnDownload(base + "/ok", std::string(tmpl) + "/no/such/dir/file.bin", sp);   // the file cannot be created
    CHECK(r.outcome == OwnOutcome::DiskFull);
    {   // not asked for (OKAY_OWN_HTTP unset): nothing happens, no request is made
        const int before = s.requests;
        ::unsetenv("OKAY_OWN_HTTP");
        r = OwnDownload(base + "/ok", out, sp);
        CHECK(r.outcome == OwnOutcome::NotHandled && s.requests == before && !fs::exists(out));
        ::setenv("OKAY_OWN_HTTP", "1", 1); }
    detail::SleepUnlessCancelled(1, nullptr);
    std::error_code ec; fs::remove_all(tmpl, ec);
    s.Stop();
}

static void TestHeadAndPrefix() {
    Srv s;
    s.handler = [&](int, int, const std::string& h, int c) {
        const std::string path = PathOf(h);
        const bool head = h.rfind("HEAD ", 0) == 0;
        if (path == "/size") { SendAll(c, "HTTP/1.1 200 OK\r\nContent-Length: 123456\r\n\r\n" + std::string(head ? "" : std::string(123456, 'x'))); return true; }
        if (path == "/redir") { SendAll(c, "HTTP/1.1 301 Moved\r\nContent-Length: 5\r\nLocation: /size\r\n\r\n" + std::string(head ? "" : "moved")); return true; }
        if (path == "/chunked") { SendAll(c, "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"); return true; }
        if (path == "/404") { SendAll(c, "HTTP/1.1 404 Not Found\r\nContent-Length: 77\r\n\r\n" + std::string(head ? 0 : 77, 'e')); return true; }
        if (path == "/huge") { SendAll(c, "HTTP/1.1 200 OK\r\nContent-Length: 99999999999999999999\r\n\r\n"); return true; }
        if (path == "/negative") { SendAll(c, "HTTP/1.1 200 OK\r\nContent-Length: -5\r\n\r\n"); return false; }
        if (path == "/loop") { SendAll(c, "HTTP/1.1 302 Found\r\nLocation: /loop\r\nContent-Length: 0\r\n\r\n"); return true; }
        if (path == "/hang") { while (!s.stop) std::this_thread::sleep_for(std::chrono::milliseconds(20)); return false; }
        if (path == "/garbage") { SendAll(c, "no http here\r\n\r\n"); return false; }
        // prefix fetches
        const std::string page(400000, 'h');
        if (path == "/page-range") {
            if (h.find("Range: bytes=0-262143") != std::string::npos) SendAll(c, "HTTP/1.1 206 Partial Content\r\nContent-Range: bytes 0-262143/400000\r\nContent-Length: 262144\r\n\r\n" + page.substr(0, 262144));
            else SendAll(c, Ok(page));
            return true;
        }
        if (path == "/page-noranges") { SendAll(c, Ok(page.substr(0, 100000))); return true; }
        if (path == "/page-huge") { SendAll(c, Ok(page + page + page + page + page + page + page + page)); return true; }   // 3.2 MB
        if (path == "/page-wrong-range") { SendAll(c, "HTTP/1.1 206 Partial Content\r\nContent-Range: bytes 500-999/2000\r\nContent-Length: 500\r\n\r\n" + page.substr(0, 500)); return true; }
        if (path == "/page-416") { SendAll(c, "HTTP/1.1 416 Range Not Satisfiable\r\nContent-Length: 0\r\n\r\n"); return true; }
        SendAll(c, Ok("?")); return true;
    };
    s.Start();
    const std::string base = "http://127.0.0.1:" + std::to_string(s.port);
    std::string dump;
    CHECK(OwnHeadDump(base + "/size", "OkaySpace-Launcher", dump) && okay::netguard::ParseLastContentLength(dump) == 123456);
    CHECK(s.Head(0).rfind("HEAD /size ", 0) == 0 && s.Head(0).find("User-Agent: OkaySpace-Launcher") != std::string::npos);
    CHECK(OwnHeadDump(base + "/redir", "OkaySpace-Launcher", dump) && okay::netguard::ParseLastContentLength(dump) == 123456);   // the last one wins, as with curl -L
    CHECK(OwnHeadDump(base + "/chunked", "", dump) && okay::netguard::ParseLastContentLength(dump) == 0);
    CHECK(OwnHeadDump(base + "/404", "", dump) && dump.find("404") != std::string::npos && okay::netguard::ParseLastContentLength(dump) == 77);   // no -f: an error page's size, like curl -I
    CHECK(OwnHeadDump(base + "/huge", "", dump) && okay::netguard::ParseLastContentLength(dump) == 0);     // implausible: 0, as before
    CHECK(OwnHeadDump(base + "/negative", "", dump) && okay::netguard::ParseLastContentLength(dump) == 0);   // a nonsense length: size 0 (unknown), as before
    CHECK(!OwnHeadDump(base + "/garbage", "", dump) && dump.empty());
    CHECK(!OwnHeadDump(base + "/loop", "", dump));
    {   auto t0 = std::chrono::steady_clock::now();
        CHECK(!OwnHeadDump(base + "/hang", "", dump, 500) && Secs(t0) < 5.0); }
    CHECK(s.connections <= 6);   // the first answers kept their connection; only the failing ones cost a new one
    {   std::string body; long st = 0;
        OwnOutcome o = OwnFetchPrefix(base + "/page-range", "Mozilla/5.0 (compatible; OkaySpace link preview)", 3000000, 262143, 8000, body, &st);
        CHECK(o == OwnOutcome::Ok && st == 206 && body.size() == 262144);
        CHECK(s.Head(s.requests - 1).find("Range: bytes=0-262143") != std::string::npos);
        o = OwnFetchPrefix(base + "/page-noranges", "ua", 3000000, 262143, 8000, body, &st);   // the server ignores the range: the whole body, the caller cuts
        CHECK(o == OwnOutcome::Ok && st == 200 && body.size() == 100000);
        o = OwnFetchPrefix(base + "/page-huge", "ua", 3000000, 262143, 8000, body, &st);
        CHECK(o == OwnOutcome::TooBig && body.empty());
        o = OwnFetchPrefix(base + "/page-wrong-range", "ua", 3000000, 262143, 8000, body, &st);   // a slice we did not ask for is refused: curl runs
        CHECK(o == OwnOutcome::NotHandled);
        o = OwnFetchPrefix(base + "/page-416", "ua", 3000000, 262143, 8000, body, &st);
        CHECK(o == OwnOutcome::HttpError && st == 416);
        o = OwnFetchPrefix(base + "/404", "ua", 3000000, 262143, 8000, body, &st);
        CHECK(o == OwnOutcome::HttpError && st == 404);
        auto t0 = std::chrono::steady_clock::now();
        o = OwnFetchPrefix(base + "/hang", "ua", 3000000, 262143, 700, body, &st);   // -m: the whole call is bounded
        CHECK(o == OwnOutcome::NotHandled && Secs(t0) < 5.0);
        o = OwnFetchPrefix(base + "/garbage", "ua", 3000000, 262143, 2000, body, &st);
        CHECK(o == OwnOutcome::NotHandled); }
    s.Stop();
}

// A proxy that refuses CONNECT: the helpers must report NotHandled (so curl runs) and never reach the target directly.
static void TestProxyRefusal() {
    okay::ownhttp::NetOptions probe; std::string why;
    if (!okay::ownhttp::TlsAvailable(probe, why)) { ++g_skips; std::printf("SKIP proxy refusal: %s\n", why.c_str()); return; }
    Srv origin; origin.handler = [&](int, int, const std::string&, int c) { SendAll(c, Ok("direct")); return true; }; origin.Start();
    Srv px; std::atomic<int> mode{0};
    px.handler = [&](int, int, const std::string&, int c) {
        if (mode == 0) SendAll(c, "HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\n\r\n");
        else if (mode == 1) SendAll(c, "HTTP/1.1 407 Proxy Authentication Required\r\nContent-Length: 0\r\n\r\n");
        return false;
    };
    px.Start();
    ::setenv("HTTPS_PROXY", ("http://127.0.0.1:" + std::to_string(px.port)).c_str(), 1);
    ::unsetenv("https_proxy"); ::unsetenv("NO_PROXY"); ::unsetenv("no_proxy");
    char tmpl[] = "/tmp/okay_nfp_XXXXXX"; if (!::mkdtemp(tmpl)) std::exit(2);
    const std::string out = std::string(tmpl) + "/f";
    const std::string target = "https://127.0.0.1:" + std::to_string(origin.port) + "/x";   // the origin speaks plain http: a direct attempt would be visible
    for (int m = 0; m < 3; ++m) {
        mode = m;
        OwnDownloadSpec sp; sp.totalMs = 4000;
        OwnDownloadResult r = OwnDownload(target, out, sp);
        CHECK(r.outcome == OwnOutcome::NotHandled && r.error.rfind("proxy", 0) == 0 && !fs::exists(out));
        std::string dump; CHECK(!OwnHeadDump(target, "", dump, 3000));
        std::string body; CHECK(OwnFetchPrefix(target, "", 1000, 100, 3000, body) == OwnOutcome::NotHandled);
    }
    CHECK(origin.requests == 0 && origin.connections == 0);   // never went around the proxy
    ::unsetenv("HTTPS_PROXY");
    std::error_code ec; fs::remove_all(tmpl, ec);
    px.Stop(); origin.Stop();
}

int main() {
    std::signal(SIGPIPE, SIG_IGN);
    ::setenv("OKAY_OWN_HTTP", "1", 1); ::unsetenv("OKAY_HTTP_CURL");
    ::setenv("NO_PROXY", "127.0.0.1", 1); ::unsetenv("HTTP_PROXY"); ::unsetenv("http_proxy"); ::unsetenv("HTTPS_PROXY"); ::unsetenv("https_proxy");
    ::setenv("PATH", "/nonexistent", 1);   // no curl and no shell tools: any fallback is a visible failure
    TestDownload();
    TestHeadAndPrefix();
    TestProxyRefusal();
    std::printf("native_fetch: %d checks, %d failed, %d skipped\n", g_checks, g_fail, g_skips);
    return g_fail ? 1 : 0;
}
