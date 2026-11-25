// The call-site helpers of NativeFetch.hpp (own HTTP client first, curl as the fallback) against loopback servers:
// downloads (success, HTTP errors, retries, truncated and over-long bodies, size caps, cancel, disk errors, redirect loops,
// slow servers), HEAD size probes, link-preview prefix fetches, keep-alive across sequential calls, and a proxy that
// refuses CONNECT. PATH is emptied, so a silent curl fallback would show up as a failure; the helpers never spawn anything.
// TestSeam checks the seam calls (FetchToFile, HeadDump, FetchPrefix, Send) with the own client on, then with a stub curl.
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

// The seam itself (FetchToFile, HeadDump, FetchPrefix, Send): our own client first, then the curl fallback (a stub curl
// that records its arguments), never curl for a POST that may have reached the server, and nothing at all with
// OKAY_NO_CURL=1.
static void TestSeam() {
    Srv s;
    const std::string payload(3000, 'q');
    s.handler = [&](int, int, const std::string& h, int c) {
        const std::string path = PathOf(h);
        if (path == "/ok") { SendAll(c, h.rfind("HEAD", 0) == 0 ? "HTTP/1.1 200 OK\r\nContent-Length: 3000\r\n\r\n" : Ok(payload)); return true; }
        if (path == "/page") { SendAll(c, "HTTP/1.1 206 Partial Content\r\nContent-Length: 10\r\nContent-Range: bytes 0-9/3000\r\n\r\n" + payload.substr(0, 10)); return true; }
        if (path == "/echo") {   // the body is still in the server's buffer: answer with its announced length, then hang up
            const size_t p = h.find("Content-Length: ");
            const std::string n = p == std::string::npos ? "0" : std::to_string(std::atoi(h.c_str() + p + 16));
            SendAll(c, "HTTP/1.1 201 Created\r\nContent-Length: " + std::to_string(n.size() + 4) + "\r\nConnection: close\r\n\r\ngot " + n); return false;
        }
        if (path == "/swallow") return false;   // reads the request, then hangs up without an answer
        SendAll(c, "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n"); return true;
    };
    s.Start();
    const std::string base = "http://127.0.0.1:" + std::to_string(s.port);
    char tmpl[] = "/tmp/okay_seam_XXXXXX";
    const std::string dir = ::mkdtemp(tmpl);
    const std::string out = dir + "/file.bin", log = dir + "/curl.log";
    fs::create_directories(dir + "/bin");
    {
        std::ofstream f(dir + "/bin/curl");
        f << "#!/bin/sh\nlog='" << log << "'\necho \"run $*\" >> \"$log\"\no=; D=; K=; w=\n"
             "while [ $# -gt 0 ]; do case \"$1\" in -o) o=\"$2\"; shift;; -D) D=\"$2\"; shift;; -K) K=\"$2\"; shift;; -w) w=\"$2\"; shift;; esac; shift; done\n"
             "if [ -n \"$K\" ]; then while IFS= read -r l; do echo \"cfg $l\" >> \"$log\"; done < \"$K\"; fi\n"
             "if [ -n \"$D\" ]; then printf 'HTTP/1.1 200 OK\\r\\nContent-Length: 77\\r\\n\\r\\n' > \"$D\"; fi\n"
             "if [ -n \"$o\" ] && [ \"$o\" != /dev/null ]; then printf 'curlbody' > \"$o\"; fi\n"
             "if [ -n \"$w\" ]; then printf '201'; fi\nexit 0\n";
    }
    fs::permissions(dir + "/bin/curl", fs::perms::owner_all);
    const std::string savedPath = std::getenv("PATH") ? std::getenv("PATH") : "";
    ::setenv("PATH", (dir + "/bin").c_str(), 1);
    auto runs = [&] { std::ifstream f(log); int n = 0; std::string l; while (std::getline(f, l)) if (l.rfind("run ", 0) == 0) ++n; return n; };

    // 1. Own client on: every answer is its own, curl never runs.
    OwnDownloadSpec sp; sp.userAgent = "SeamTest";
    OwnDownloadResult r = FetchToFile(base + "/ok", out, sp);
    CHECK(r.outcome == OwnOutcome::Ok && !r.viaCurl && Slurp(out) == payload);
    r = FetchToFile(base + "/missing", out, sp);
    CHECK(r.outcome == OwnOutcome::HttpError && r.status == 404 && !fs::exists(out));
    std::string dump, body;
    CHECK(HeadDump(base + "/ok", "SeamTest", dump) && okay::netguard::ParseLastContentLength(dump) == 3000);
    CHECK(FetchPrefix(base + "/page", "SeamTest", 1000, 9, 5000, body) == OwnOutcome::Ok && body == payload.substr(0, 10));
    Result sr = Send("POST", base + "/echo", {"Content-Type: application/json"}, "{\"x\":1}");
    CHECK(sr.reached && sr.status == 201 && sr.body == "got 7");
    sr = Send("POST", base + "/swallow", {}, "once");   // may have arrived: not re-sent through curl
    CHECK(!sr.reached && sr.maybeSent);
    CHECK(runs() == 0);

    // 2. Own client off: the curl fallback (the stub) answers, with the headers in a private config file, not argv.
    ::unsetenv("OKAY_OWN_HTTP");
    r = FetchToFile(base + "/ok", out, sp);
    CHECK(r.outcome == OwnOutcome::Ok && r.viaCurl && Slurp(out) == "curlbody");
    CHECK(HeadDump(base + "/ok", "SeamTest", dump) && okay::netguard::ParseLastContentLength(dump) == 77);
    CHECK(FetchPrefix(base + "/page", "", 1000, 9, 5000, body) == OwnOutcome::Ok && body == "curlbody");
    sr = Send("POST", base + "/echo", {"X-Authorization: secret-ticket"}, "{}");
    CHECK(sr.reached && sr.status == 201 && sr.body == "curlbody");
    {
        const std::string l = Slurp(log);
        CHECK(l.find("cfg header = \"X-Authorization: secret-ticket\"") != std::string::npos);
        CHECK(l.find("run -s -X POST") != std::string::npos);
        size_t runLine = l.rfind("run -s -X POST"); CHECK(l.substr(runLine, l.find('\n', runLine) - runLine).find("secret") == std::string::npos);
    }
    CHECK(runs() == 4);
    {   // the temporary files the fallback used are gone
        int left = 0; for (const auto& e : fs::directory_iterator(fs::temp_directory_path())) if (e.path().filename().string().rfind("okay_http_", 0) == 0) ++left;
        CHECK(left == 0);
    }

    // 3. OKAY_NO_CURL=1: nothing is started, and the calls say so.
    ::setenv("OKAY_NO_CURL", "1", 1);
    fs::remove(out);
    r = FetchToFile(base + "/ok", out, sp);
    CHECK(r.outcome == OwnOutcome::NotHandled && !r.viaCurl && !fs::exists(out));
    CHECK(!HeadDump(base + "/ok", "", dump));
    CHECK(FetchPrefix(base + "/page", "", 1000, 9, 5000, body) == OwnOutcome::NotHandled);
    CHECK(!Send("GET", base + "/ok", {}).reached);
    CHECK(runs() == 4);
    ::unsetenv("OKAY_NO_CURL");
    ::setenv("OKAY_OWN_HTTP", "1", 1);
    ::setenv("PATH", savedPath.c_str(), 1);
    std::error_code ec; fs::remove_all(dir, ec);
}

// PR #1186 review fixes that can be checked on Linux: header lines lose CR/LF before the own client sees them (it used to
// refuse the call, and the seam then ran curl, which strips them); a failed rename is not "disk full"; a cancel that
// comes in after the bytes arrived still wins over the rename (a caller that gave up must never get a late file);
// RequestEx sends If-Range with a resume; the If-Range validator and credential-header rules.
static void TestReviewFixes() {
    Srv s;
    s.handler = [&](int, int, const std::string& h, int c) {
        const std::string path = PathOf(h);
        if (path == "/tail") {
            if (h.find("If-Range: \"v1\"") != std::string::npos) SendAll(c, "HTTP/1.1 206 Partial Content\r\nContent-Length: 3\r\nContent-Range: bytes 2-4/5\r\n\r\ncde");
            else SendAll(c, Ok("abcde"));
            return true;
        }
        SendAll(c, Ok("hi")); return true;
    };
    s.Start();
    const std::string base = "http://127.0.0.1:" + std::to_string(s.port);
    {   // a CR/LF inside a header value is taken out, the call is made (before: refused, then curl, which is not on PATH)
        const Result r = Send("GET", base + "/x", {"X-Test: a\r\nInjected: 1"});
        CHECK(r.reached && r.status == 200);
        const std::string h = s.Head((size_t)s.requests.load() - 1);
        CHECK(h.find("X-Test: aInjected: 1\r\n") != std::string::npos && h.find("\r\nInjected:") == std::string::npos);
    }
    CHECK(CleanHeaderLine(std::string("A: b\r\nC: d\0e", 13)) == "A: bC: de");
    CHECK(CleanHeaderLines({"\r\n", "X: 1"}).size() == 1);
    CHECK(HeaderCarriesSecret("Authorization: Bearer x") && HeaderCarriesSecret("apikey: k") && HeaderCarriesSecret("X-Authorization: t"));
    CHECK(!HeaderCarriesSecret("Content-Type: application/json") && !HeaderCarriesSecret("User-Agent: tokenish"));
    CHECK(fetch_detail::IfRangeValidator(" \"v1\" ", "Mon") == "\"v1\"" && fetch_detail::IfRangeValidator("W/\"v1\"", "Mon, 01 Jan") == "Mon, 01 Jan");
    CHECK(fetch_detail::IfRangeValidator("", "") == "");
    {   // RequestEx: a resume carries If-Range
        const std::string f = "/tmp/okay_nf_tail_" + std::to_string(::getpid());
        { std::ofstream o(f, std::ios::binary); o << "ab"; }
        RequestOptions ro; ro.resumeFrom = 2; ro.ifRange = "\"v1\"";
        const ExResult r = RequestEx("GET", base + "/tail", {}, "", "", f, ro);
        CHECK(r.reached && r.status == 206 && r.resumed && Slurp(f) == "abcde");
        CHECK(s.Head((size_t)s.requests.load() - 1).find("If-Range: \"v1\"\r\n") != std::string::npos);
        std::remove(f.c_str());
    }
    s.Stop();
    {   // FetchToFile, any path: a failure removes an existing `path` too (the own client always did; WinHTTP and curl left
        // the old file there, which the Windows run of winhttp_loopback showed). Here: own client off, curl not on PATH.
        const fs::path old = fs::temp_directory_path() / ("okay_nf_old_" + std::to_string(::getpid()));
        { std::ofstream o(old, std::ios::binary); o << "old"; }
        ::unsetenv("OKAY_OWN_HTTP");
        const OwnDownloadResult r = FetchToFile("http://127.0.0.1:1/x", old, OwnDownloadSpec());
        ::setenv("OKAY_OWN_HTTP", "1", 1);
        CHECK(r.outcome == OwnOutcome::NotHandled);
        CHECK(!fs::exists(old));
        std::error_code ec; fs::remove(old, ec);
    }
    {   // Finish: a rename that fails is NotHandled with its reason, never DiskFull
        const fs::path dir = fs::temp_directory_path() / ("okay_nf_fin_" + std::to_string(::getpid()));
        std::error_code ec; fs::remove_all(dir, ec); fs::create_directories(dir / "target" / "inside", ec);
        const fs::path tmp = dir / "t.part";
        { std::ofstream o(tmp, std::ios::binary); o << "data"; }
        OwnDownloadSpec sp;
        OwnDownloadResult r = fetch_detail::Finish(tmp, dir / "target", sp, OwnDownloadResult());   // a non-empty folder is in the way
        CHECK(r.outcome == OwnOutcome::NotHandled && r.outcome != OwnOutcome::DiskFull && r.error.find("rename failed") == 0);
        CHECK(!fs::exists(tmp));
        // a cancel that came in after the bytes arrived: the file is dropped, the target is not touched
        { std::ofstream o(tmp, std::ios::binary); o << "late"; }
        { std::ofstream o(dir / "out.bin", std::ios::binary); o << "other"; }
        sp.cancelled = [] { return true; };
        r = fetch_detail::Finish(tmp, dir / "out.bin", sp, OwnDownloadResult());
        CHECK(r.outcome == OwnOutcome::Cancelled && !fs::exists(tmp) && Slurp((dir / "out.bin").string()) == "other");
        fs::remove_all(dir, ec);
    }
}

// PR #1186 review follow-up: a download with an expected SHA-256 but no validator (no ETag, no Last-Modified) is dropped,
// and the resume (sent without If-Range, since there is nothing to send) gets the tail of a file that changed on the
// server. The glued file fails the checksum; the download must then fetch the file once more from byte 0 instead of
// ending as ChecksumMismatch. A server whose file really does not match still ends as ChecksumMismatch, after that one
// extra try (and a download that was never resumed is not tried again).
static void TestChecksumAfterResume() {
    const std::string a(6000, 'a'), b(6000, 'b');
    std::atomic<int> fulls{0}, ranges{0};
    Srv s;
    s.handler = [&](int, int, const std::string& h, int c) {
        const std::string path = PathOf(h);
        const std::string& now = path == "/bad" ? a : (fulls.load() == 0 ? a : b);   // /changes: A first, then B
        const size_t rp = h.find("Range: bytes=");
        if (rp != std::string::npos) {
            ++ranges;
            const size_t from = (size_t)std::strtoull(h.c_str() + rp + 13, nullptr, 10);
            const std::string& tail = path == "/bad" ? a : b;
            SendAll(c, "HTTP/1.1 206 Partial Content\r\nContent-Length: " + std::to_string(tail.size() - from) + "\r\nContent-Range: bytes " +
                       std::to_string(from) + "-" + std::to_string(tail.size() - 1) + "/" + std::to_string(tail.size()) + "\r\n\r\n" + tail.substr(from));
            return true;
        }
        const int n = fulls++;
        if (n == 0) {   // the first answer is cut in the middle: the client resumes with Range on its next try
            SendAll(c, "HTTP/1.1 200 OK\r\nContent-Length: " + std::to_string(now.size()) + "\r\n\r\n" + now.substr(0, 2500));
            return false;
        }
        SendAll(c, Ok(now));
        return true;
    };
    s.Start();
    const std::string base = "http://127.0.0.1:" + std::to_string(s.port);
    const fs::path dir = fs::temp_directory_path() / ("okay_nf_sha_" + std::to_string(::getpid()));
    std::error_code ec; fs::remove_all(dir, ec); fs::create_directories(dir, ec);
    const fs::path want = dir / "want.bin";
    { std::ofstream o(want, std::ios::binary); o << b; }
    const std::string shaB = fetch_detail::Sha256File(want);
    {
        OwnDownloadSpec sp; sp.retries = 1; sp.retryDelayMs = 10; sp.expectedSha256 = shaB;
        const OwnDownloadResult r = FetchToFile(base + "/changes", dir / "out.bin", sp);
        CHECK(r.outcome == OwnOutcome::Ok);
        CHECK(ranges.load() == 1 && fulls.load() == 2);   // cut, resumed onto the new file, then fetched again from byte 0
        CHECK(Slurp((dir / "out.bin").string()) == b);
        if (r.outcome != OwnOutcome::Ok) std::printf("  outcome %d, attempts %d, error %s\n", (int)r.outcome, r.attempts, r.error.c_str());
    }
    {   // the server's file never matches: still ChecksumMismatch (one extra try from byte 0, not a loop), nothing kept
        fulls = 0; ranges = 0;
        OwnDownloadSpec sp; sp.retries = 1; sp.retryDelayMs = 10; sp.expectedSha256 = shaB;
        const OwnDownloadResult r = FetchToFile(base + "/bad", dir / "bad.bin", sp);
        CHECK(r.outcome == OwnOutcome::ChecksumMismatch);
        CHECK(ranges.load() == 1 && fulls.load() == 2);
        CHECK(!fs::exists(dir / "bad.bin"));
    }
    {   // never resumed (the first answer is whole): a mismatch is final at once
        fulls = 1; ranges = 0;
        OwnDownloadSpec sp; sp.retries = 1; sp.retryDelayMs = 10; sp.expectedSha256 = shaB;
        const OwnDownloadResult r = FetchToFile(base + "/bad", dir / "bad.bin", sp);
        CHECK(r.outcome == OwnOutcome::ChecksumMismatch && fulls.load() == 2 && ranges.load() == 0);
    }
    s.Stop();
    fs::remove_all(dir, ec);
}

int main() {
    std::signal(SIGPIPE, SIG_IGN);
    ::setenv("OKAY_OWN_HTTP", "1", 1); ::unsetenv("OKAY_HTTP_CURL");
    ::setenv("NO_PROXY", "127.0.0.1", 1); ::unsetenv("HTTP_PROXY"); ::unsetenv("http_proxy"); ::unsetenv("HTTPS_PROXY"); ::unsetenv("https_proxy");
    ::setenv("PATH", "/nonexistent", 1);   // no curl and no shell tools: any fallback is a visible failure
    TestDownload();
    TestHeadAndPrefix();
    TestProxyRefusal();
    TestSeam();
    TestReviewFixes();
    TestChecksumAfterResume();
    std::printf("native_fetch: %d checks, %d failed, %d skipped\n", g_checks, g_fail, g_skips);
    return g_fail ? 1 : 0;
}
