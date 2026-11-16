// Tests for okay::ownhttp retries and downloads to a file (engine/include/okay/Platform/OwnHttpDownload.hpp), against
// a local HTTP server written here on plain sockets (no outside host is ever contacted):
//   redirects 301/302/307/308 and a loop, chunked bodies, gzip (in memory), Range and resume after dropped and stalled
//   connections, a server that ignores Range, a file that changes between tries (If-Range), a wrong Content-Range,
//   slow trickles, 5xx and 429 then success with Retry-After (seconds and an HTTP date), no retry for POST, a wrong
//   checksum (fails, nothing is left behind), a large file (200 MB by default) streamed with flat memory, a plain
//   HTTP proxy and a CONNECT proxy (https to `openssl s_server`, with test certificates from
//   tests/scripts/gen_http_test_certs.sh; that part prints SKIP when libssl or the openssl program is missing,
//   unless OKAY_REQUIRE_TLS_TESTS=1).
//
// Usage:
//   test_own_http_download [gen_http_test_certs.sh]            the tests (OKAY_HTTP_BIG_MB sets the large file size)
//   test_own_http_download --serve                             a server for scripts/own_http_bench.py: prints the port,
//                                                              serves /file?size=N until stdin closes
//   test_own_http_download --get URL OUT [nosync,nohash]       download URL to OUT with DownloadToFile, print the SHA-256
//                                                              (nosync: no fsync before the rename, as curl; nohash: no SHA-256)
//   test_own_http_download --sha FILE                          SHA-256 of FILE with the engine's own hash
//
// Written for this engine; the server, proxy and checks are our own code.
#include "okay/Platform/OwnHttpDownload.hpp"

#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <mutex>
#include <sys/wait.h>
#include <thread>

using namespace okay::ownhttp;

static int g_fail = 0, g_checks = 0, g_skips = 0;
#define CHECK(c) do { ++g_checks; if (!(c)) { ++g_fail; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

// ---- content -----------------------------------------------------------------------
/// The byte at `i` of test file version `v`: cheap, deterministic, not periodic over small spans.
static inline uint8_t Gen(uint64_t i, int v) { return uint8_t((i * 7u) + ((i >> 9) * 131u) + (i >> 17)) ^ uint8_t(v == 2 ? 0x5a : 0); }
static void GenFill(char* p, uint64_t off, size_t n, int v) { for (size_t k = 0; k < n; ++k) p[k] = char(Gen(off + k, v)); }
static std::string GenStr(uint64_t off, size_t n, int v = 1) { std::string s(n, '\0'); GenFill(&s[0], off, n, v); return s; }
static std::string GenSha(uint64_t size, int v = 1) {
    okay::crypto::Sha256 h; std::vector<char> buf(1 << 16);
    for (uint64_t off = 0; off < size;) { size_t n = size_t(std::min<uint64_t>(buf.size(), size - off)); GenFill(buf.data(), off, n, v); h.Update(reinterpret_cast<const uint8_t*>(buf.data()), n); off += n; }
    uint8_t d[32]; h.Final(d);
    static const char* k = "0123456789abcdef"; std::string s;
    for (uint8_t b : d) { s += k[b >> 4]; s += k[b & 15]; }
    return s;
}
static std::string Slurp(const std::string& p) { std::ifstream f(p, std::ios::binary); return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>()); }
static bool Exists(const std::string& p) { struct stat st; return ::stat(p.c_str(), &st) == 0; }

// gzip of `s` with stored (uncompressed) deflate blocks, our own writer (RFC 1951 3.2.4, RFC 1952): enough to test the
// Content-Encoding path end to end. CRC-32 from the reflected polynomial 0xEDB88320, bit by bit.
static uint32_t Crc32(const std::string& s) {
    uint32_t c = 0xffffffffu;
    for (unsigned char b : s) { c ^= b; for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1u))); }
    return c ^ 0xffffffffu;
}
static std::string GzipStored(const std::string& s) {
    std::string g = std::string("\x1f\x8b\x08\x00\x00\x00\x00\x00\x00\xff", 10);
    size_t off = 0;
    do {
        const size_t n = std::min<size_t>(65535, s.size() - off);
        const bool last = off + n == s.size();
        g += char(last ? 1 : 0);
        g += char(n & 0xff); g += char(n >> 8); g += char(~n & 0xff); g += char((~n >> 8) & 0xff);
        g.append(s, off, n); off += n;
    } while (off < s.size());
    auto le32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) g += char((v >> (8 * i)) & 0xff); };
    le32(Crc32(s)); le32(uint32_t(s.size()));
    return g;
}

// ---- server --------------------------------------------------------------------------
static void SendAll(int fd, const char* p, size_t n) {
    while (n > 0) { ssize_t w = ::send(fd, p, n, MSG_NOSIGNAL); if (w <= 0) return; p += w; n -= size_t(w); }
}
static void SendAll(int fd, const std::string& s) { SendAll(fd, s.data(), s.size()); }

static int Listen(int& port) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    int one = 1; ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    sockaddr_in a{}; a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK); a.sin_port = 0;
    if (::bind(fd, (sockaddr*)&a, sizeof a) != 0 || ::listen(fd, 64) != 0) { std::perror("bind"); std::exit(2); }
    socklen_t l = sizeof a; ::getsockname(fd, (sockaddr*)&a, &l); port = ntohs(a.sin_port);
    return fd;
}

static int ConnectLocal(int port) {
    int s = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in a{}; a.sin_family = AF_INET; a.sin_port = htons(uint16_t(port)); a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::connect(s, (sockaddr*)&a, sizeof a) != 0) { ::close(s); return -1; }
    return s;
}

struct Req { std::string method, path, query, head; std::map<std::string, std::string> q; };
static std::string HeaderValue(const std::string& head, const std::string& name) {   // case-insensitive, first match
    std::string lh = head; for (char& c : lh) c = char(std::tolower((unsigned char)c));
    std::string ln = "\r\n" + name + ":"; for (char& c : ln) c = char(std::tolower((unsigned char)c));
    size_t p = lh.find(ln); if (p == std::string::npos) return std::string();
    p += ln.size(); size_t e = head.find("\r\n", p);
    std::string v = head.substr(p, e - p);
    while (!v.empty() && v[0] == ' ') v.erase(0, 1);
    return v;
}

/// A threaded HTTP/1.1 server; one request per connection unless the handler returns true (keep the connection).
struct Server {
    int fd = -1, port = 0;
    std::atomic<bool> stop{false};
    std::thread acceptor;
    std::mutex mu;
    std::vector<std::thread> workers;
    std::map<std::string, int> hits;          // by path
    std::vector<std::string> heads;
    std::function<bool(int, Req&)> handler;

    void Start() {
        fd = Listen(port);
        acceptor = std::thread([this] {
            while (!stop) {
                pollfd p{fd, POLLIN, 0};
                if (::poll(&p, 1, 50) <= 0) continue;
                int c = ::accept(fd, nullptr, nullptr);
                if (c < 0) continue;
                std::lock_guard<std::mutex> lk(mu);
                workers.emplace_back([this, c] { Serve(c); ::close(c); });
            }
        });
    }
    void Serve(int c) {
        std::string in; char b[8192];
        for (;;) {
            while (in.find("\r\n\r\n") == std::string::npos && !stop) {
                pollfd p{c, POLLIN, 0}; if (::poll(&p, 1, 100) <= 0) continue;
                ssize_t n = ::recv(c, b, sizeof b, 0); if (n <= 0) return; in.append(b, size_t(n));
            }
            const size_t he = in.find("\r\n\r\n"); if (he == std::string::npos) return;
            Req r; r.head = in.substr(0, he + 4); in.erase(0, he + 4);
            const std::string cl = HeaderValue(r.head, "content-length");
            size_t want = cl.empty() ? 0 : size_t(std::atoll(cl.c_str()));
            while (in.size() < want && !stop) { pollfd p{c, POLLIN, 0}; if (::poll(&p, 1, 100) <= 0) continue; ssize_t n = ::recv(c, b, sizeof b, 0); if (n <= 0) return; in.append(b, size_t(n)); }
            in.erase(0, std::min(want, in.size()));
            size_t a = r.head.find(' '), e = r.head.find(' ', a + 1);
            r.method = r.head.substr(0, a);
            std::string target = r.head.substr(a + 1, e - a - 1);
            size_t qm = target.find('?');
            r.path = target.substr(0, qm);
            if (qm != std::string::npos) {
                r.query = target.substr(qm + 1);
                size_t s = 0;
                while (s <= r.query.size()) {
                    size_t amp = r.query.find('&', s); if (amp == std::string::npos) amp = r.query.size();
                    std::string kv = r.query.substr(s, amp - s); size_t eq = kv.find('=');
                    if (eq != std::string::npos) r.q[kv.substr(0, eq)] = kv.substr(eq + 1);
                    s = amp + 1;
                }
            }
            { std::lock_guard<std::mutex> lk(mu); ++hits[r.path]; heads.push_back(r.head); }
            if (!handler(c, r)) return;
        }
    }
    void Reset(const std::string& p) { std::lock_guard<std::mutex> lk(mu); hits.erase(p); }
    int Hits(const std::string& p) { std::lock_guard<std::mutex> lk(mu); auto it = hits.find(p); return it == hits.end() ? 0 : it->second; }
    std::string LastHead() { std::lock_guard<std::mutex> lk(mu); return heads.empty() ? std::string() : heads.back(); }
    std::vector<std::string> Heads() { std::lock_guard<std::mutex> lk(mu); return heads; }
    void Stop() {
        if (fd < 0) return;
        stop = true;
        if (acceptor.joinable()) acceptor.join();
        std::vector<std::thread> w;
        { std::lock_guard<std::mutex> lk(mu); w.swap(workers); }
        for (auto& t : w) if (t.joinable()) t.join();
        ::close(fd); fd = -1;
    }
    ~Server() { Stop(); }
};

static uint64_t U(const Req& r, const char* k, uint64_t d) { auto it = r.q.find(k); return it == r.q.end() ? d : uint64_t(std::strtoull(it->second.c_str(), nullptr, 10)); }

/// "bytes=N-" from the request, or -1.
static long long RangeFrom(const std::string& head) {
    const std::string v = HeaderValue(head, "range");
    if (v.rfind("bytes=", 0) != 0) return -1;
    return std::atoll(v.c_str() + 6);
}

/// Send version `v` of a `size` byte file from `from`, as 200 (from = 0) or 206. `cut` > 0 stops after that many body
/// bytes (then the connection closes, or stays silent with `stall`). `pieceDelayMs` sends 4 KiB pieces with a pause.
static void SendFile(Server& s, int c, uint64_t size, uint64_t from, int v, const std::string& etag, uint64_t cut = 0,
                     bool stall = false, int pieceDelayMs = 0, bool chunked = false) {
    std::string h;
    if (from == 0) h = "HTTP/1.1 200 OK\r\n";
    else h = "HTTP/1.1 206 Partial Content\r\nContent-Range: bytes " + std::to_string(from) + "-" + std::to_string(size - 1) + "/" + std::to_string(size) + "\r\n";
    if (chunked) h += "Transfer-Encoding: chunked\r\n"; else h += "Content-Length: " + std::to_string(size - from) + "\r\n";
    if (!etag.empty()) h += "ETag: " + etag + "\r\n";
    h += "Last-Modified: Sun, 06 Nov 1994 08:49:37 GMT\r\nAccept-Ranges: bytes\r\n\r\n";
    SendAll(c, h);
    std::vector<char> buf(pieceDelayMs ? 4096 : (1 << 16));
    uint64_t sent = 0, off = from;
    while (off < size && !s.stop) {
        size_t n = size_t(std::min<uint64_t>(buf.size(), size - off));
        if (chunked) n = std::min<size_t>(n, 1000 + size_t(off % 777));   // odd chunk sizes
        if (cut && sent + n > cut) n = size_t(cut - sent);
        GenFill(buf.data(), off, n, v);
        if (chunked) { char line[32]; std::snprintf(line, sizeof line, "%zx\r\n", n); SendAll(c, line); }
        SendAll(c, buf.data(), n);
        if (chunked) SendAll(c, "\r\n");
        off += n; sent += n;
        if (cut && sent >= cut) {
            if (stall) while (!s.stop) std::this_thread::sleep_for(std::chrono::milliseconds(20));
            return;   // the connection closes when the handler returns
        }
        if (pieceDelayMs) std::this_thread::sleep_for(std::chrono::milliseconds(pieceDelayMs));
    }
    if (chunked) SendAll(c, "0\r\n\r\n");
}

/// The test routes. Returns true to keep the connection.
static bool Route(Server& s, int c, Req& r) {
    const std::string& p = r.path;
    const uint64_t size = U(r, "size", 100000);
    const long long rf = RangeFrom(r.head);
    const uint64_t from = rf > 0 ? uint64_t(rf) : 0;
    const int hit = s.Hits(p);
    if (p == "/file") { SendFile(s, c, size, from, 1, "\"v1\""); return false; }
    if (p == "/chunked") { SendFile(s, c, size, 0, 1, "", 0, false, 0, true); return false; }
    if (p == "/slow") { SendFile(s, c, size, from, 1, "\"v1\"", 0, false, 3); return false; }
    if (p == "/drop" || p == "/stall") {   // the first `times` answers die after `cut` bytes
        const bool bad = hit <= int(U(r, "times", 1));
        SendFile(s, c, size, from, 1, "\"v1\"", bad ? U(r, "cut", 30000) : 0, bad && p == "/stall");
        return false;
    }
    if (p == "/drop-chunked") {   // chunked, cut on the first try; the resume is a plain 206
        if (hit == 1) SendFile(s, c, size, 0, 1, "\"v1\"", U(r, "cut", 30000), false, 0, true);
        else SendFile(s, c, size, from, 1, "\"v1\"");
        return false;
    }
    if (p == "/norange") {   // never honours Range; the first answer is cut
        SendFile(s, c, size, 0, 1, "", hit == 1 ? U(r, "cut", 30000) : 0);
        return false;
    }
    if (p == "/changed") {   // v1 is cut; by the resume the file is v2, so If-Range "v1" gets the whole v2 (200)
        if (hit == 1) { SendFile(s, c, size, 0, 1, "\"v1\"", U(r, "cut", 30000)); return false; }
        const std::string ir = HeaderValue(r.head, "if-range");
        if (ir == "\"v2\"") SendFile(s, c, size, from, 2, "\"v2\"");
        else SendFile(s, c, size, 0, 2, "\"v2\"");
        return false;
    }
    if (p == "/badrange") {   // the resume answer starts at the wrong byte
        if (hit == 1) { SendFile(s, c, size, 0, 1, "\"v1\"", U(r, "cut", 30000)); return false; }
        SendFile(s, c, size, from > 0 ? from - 1 : 0, 1, "\"v1\"");
        return false;
    }
    if (p == "/r301") { SendAll(c, "HTTP/1.1 301 Moved\r\nLocation: /r302?" + r.query + "\r\nContent-Length: 0\r\n\r\n"); return false; }
    if (p == "/r302") { SendAll(c, "HTTP/1.1 302 Found\r\nLocation: http://127.0.0.1:" + std::to_string(s.port) + "/r307?" + r.query + "\r\nContent-Length: 0\r\n\r\n"); return false; }
    if (p == "/r307") { SendAll(c, "HTTP/1.1 307 Temporary\r\nLocation: r308?" + r.query + "\r\nContent-Length: 5\r\n\r\nmoved"); return false; }
    if (p == "/r308") { SendAll(c, "HTTP/1.1 308 Permanent\r\nLocation: /file?" + r.query + "\r\nContent-Length: 0\r\n\r\n"); return false; }
    if (p == "/rloop") { SendAll(c, "HTTP/1.1 302 Found\r\nLocation: /rloop\r\nContent-Length: 0\r\n\r\n"); return false; }
    if (p == "/flaky") {   // `n` answers of `code` (Retry-After `ra` when given), then the file
        if (hit <= int(U(r, "n", 2))) {
            std::string h = "HTTP/1.1 " + std::to_string(U(r, "code", 503)) + " Busy\r\nContent-Length: 4\r\n";
            if (r.q.count("ra")) h += "Retry-After: " + r.q["ra"] + "\r\n";
            SendAll(c, h + "\r\nbusy");
            return false;
        }
        SendFile(s, c, size, from, 1, "\"v1\"");
        return false;
    }
    if (p == "/429date") {   // Retry-After as an HTTP date one second ahead
        if (hit == 1) {
            char d[64]; std::time_t t = std::time(nullptr) + 2; std::tm g{}; ::gmtime_r(&t, &g);
            std::strftime(d, sizeof d, "%a, %d %b %Y %H:%M:%S GMT", &g);
            SendAll(c, std::string("HTTP/1.1 429 Too Many\r\nRetry-After: ") + d + "\r\nContent-Length: 0\r\n\r\n");
            return false;
        }
        SendFile(s, c, size, from, 1, "\"v1\""); return false;
    }
    if (p == "/longwait") { SendAll(c, "HTTP/1.1 503 Busy\r\nRetry-After: 3600\r\nContent-Length: 0\r\n\r\n"); return false; }
    if (p == "/always503") { SendAll(c, "HTTP/1.1 503 Busy\r\nContent-Length: 0\r\n\r\n"); return false; }
    if (p == "/404") { SendAll(c, "HTTP/1.1 404 Not Found\r\nContent-Length: 4\r\n\r\nnope"); return false; }
    if (p == "/post") { SendAll(c, "HTTP/1.1 503 Busy\r\nContent-Length: 0\r\n\r\n"); return false; }
    if (p == "/gzip") {
        const std::string body = GenStr(0, size);
        const std::string gz = GzipStored(body);
        SendAll(c, "HTTP/1.1 200 OK\r\nContent-Encoding: gzip\r\nContent-Length: " + std::to_string(gz.size()) + "\r\n\r\n" + gz);
        return false;
    }
    if (p == "/gzip-flaky") {   // 502 first, then a gzip body
        if (hit == 1) { SendAll(c, "HTTP/1.1 502 Bad Gateway\r\nContent-Length: 0\r\n\r\n"); return false; }
        const std::string gz = GzipStored(GenStr(0, size));
        SendAll(c, "HTTP/1.1 200 OK\r\nContent-Encoding: gzip\r\nContent-Length: " + std::to_string(gz.size()) + "\r\n\r\n" + gz);
        return false;
    }
    if (p == "/refuse-after") { return false; }   // closes at once (no answer)
    SendAll(c, "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n");
    return false;
}

// ---- proxy (plain HTTP absolute-form and CONNECT tunnels) -------------------------------
static void Pump(int a, int b, std::atomic<bool>& stop) {
    char buf[1 << 15];
    for (;;) {
        if (stop) return;
        pollfd p[2] = {{a, POLLIN, 0}, {b, POLLIN, 0}};
        if (::poll(p, 2, 100) <= 0) continue;
        for (int i = 0; i < 2; ++i) {
            if (!(p[i].revents & (POLLIN | POLLHUP | POLLERR))) continue;
            ssize_t n = ::recv(p[i].fd, buf, sizeof buf, 0);
            if (n <= 0) return;
            SendAll(i == 0 ? b : a, buf, size_t(n));
        }
    }
}
struct TestProxy {
    Server srv;
    std::atomic<int> connects{0}, forwards{0};
    void Start() {
        srv.handler = [this](int c, Req& r) {
            // CONNECT host:port, or GET http://host:port/path (absolute form)
            std::string target = r.head.substr(r.head.find(' ') + 1); target = target.substr(0, target.find(' '));
            if (r.method == "CONNECT") {
                ++connects;
                const int port = std::atoi(target.substr(target.rfind(':') + 1).c_str());
                if (target.rfind("127.0.0.1:", 0) != 0 && target.rfind("localhost:", 0) != 0) { SendAll(c, "HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\n\r\n"); return false; }
                int up = ConnectLocal(port);
                if (up < 0) { SendAll(c, "HTTP/1.1 502 Bad Gateway\r\nContent-Length: 0\r\n\r\n"); return false; }
                SendAll(c, "HTTP/1.1 200 Connection established\r\n\r\n");
                Pump(c, up, srv.stop); ::close(up);
                return false;
            }
            if (target.rfind("http://127.0.0.1:", 0) != 0) { SendAll(c, "HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\n\r\n"); return false; }
            ++forwards;
            const std::string rest = target.substr(17);
            const int port = std::atoi(rest.c_str());
            const std::string path = rest.substr(rest.find('/'));
            int up = ConnectLocal(port);
            if (up < 0) { SendAll(c, "HTTP/1.1 502 Bad Gateway\r\nContent-Length: 0\r\n\r\n"); return false; }
            std::string head = r.method + " " + path + " HTTP/1.1" + r.head.substr(r.head.find("\r\n"));
            SendAll(up, head);
            Pump(c, up, srv.stop); ::close(up);
            return false;
        };
        srv.Start();
    }
};

// ---- helpers ----------------------------------------------------------------------------
static NetOptions Opts() {
    NetOptions o;
    o.env = [](const char*) -> const char* { return nullptr; };   // never inherit this machine's proxy settings
    o.connectMs = 2000; o.ioMs = 3000; o.totalMs = 60000;
    return o;
}
static RetryPolicy Fast(int attempts) { RetryPolicy p; p.maxAttempts = attempts; p.baseDelayMs = 20; p.maxDelayMs = 200; p.seed = 42; return p; }
static double Secs(std::chrono::steady_clock::time_point t0) { return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(); }
static long RssKb() {
    std::ifstream f("/proc/self/status"); std::string line;
    while (std::getline(f, line)) if (line.rfind("VmRSS:", 0) == 0) return std::atol(line.c_str() + 6);
    return -1;
}
static std::string TmpBase() { const char* t = std::getenv("TMPDIR"); return (t && *t) ? t : "/tmp"; }

// ---- pure parts ---------------------------------------------------------------------------
static void TestPure() {
    long long e = 0;
    CHECK(ParseHttpDate("Sun, 06 Nov 1994 08:49:37 GMT", e) && e == 784111777);
    CHECK(ParseHttpDate("Sunday, 06-Nov-94 08:49:37 GMT", e, 1700000000) && e == 784111777);
    CHECK(ParseHttpDate("Sun Nov  6 08:49:37 1994", e) && e == 784111777);
    CHECK(ParseHttpDate("Thu, 01 Jan 1970 00:00:00 GMT", e) && e == 0);
    CHECK(ParseHttpDate("Tue, 29 Feb 2000 12:00:00 GMT", e) && e == 951825600);   // leap day of a /400 year
    CHECK(!ParseHttpDate("Thu, 29 Feb 2001 12:00:00 GMT", e));                     // not a leap year
    CHECK(!ParseHttpDate("Sun, 06 Nov 1994 08:49:37 UTC", e));
    CHECK(!ParseHttpDate("Sun, 6 Nov 1994 08:49:37 GMT", e));
    CHECK(!ParseHttpDate("Sun, 06 Foo 1994 08:49:37 GMT", e));
    CHECK(!ParseHttpDate("Sun, 06 Nov 1994 25:49:37 GMT", e));
    CHECK(!ParseHttpDate("", e) && !ParseHttpDate("garbage", e));
    // Two-digit years: at most 50 years ahead of now, else the century before.
    CHECK(ParseHttpDate("Monday, 01-Jan-70 00:00:00 GMT", e, 1700000000) && e == 3155760000LL);   // 2070
    CHECK(ParseHttpDate("Monday, 01-Jan-80 00:00:00 GMT", e, 1700000000) && e == 315532800);   // 1980 (2080 is too far ahead)
    CHECK(ParseRetryAfterMs("120") == 120000);
    CHECK(ParseRetryAfterMs(" 0 ") == 0);
    CHECK(ParseRetryAfterMs("-1") == -1 && ParseRetryAfterMs("1.5") == -1 && ParseRetryAfterMs("") == -1);
    CHECK(ParseRetryAfterMs("Sun, 06 Nov 1994 08:49:47 GMT", 784111777) == 10000);
    CHECK(ParseRetryAfterMs("Sun, 06 Nov 1994 08:49:37 GMT", 784111800) == 0);   // in the past: retry now
    CHECK(IsIdempotentMethod("GET") && IsIdempotentMethod("PUT") && IsIdempotentMethod("DELETE") && IsIdempotentMethod("HEAD"));
    CHECK(!IsIdempotentMethod("POST") && !IsIdempotentMethod("PATCH") && !IsIdempotentMethod("CONNECT"));
    CHECK(RetryableStatus(503) && RetryableStatus(429) && RetryableStatus(408) && !RetryableStatus(501) && !RetryableStatus(404) && !RetryableStatus(200));
    {   // backoff: doubles, capped, jitter stays inside [d*(1-j), d)
        RetryPolicy p; p.baseDelayMs = 100; p.maxDelayMs = 1000; p.jitter = 0;
        std::mt19937_64 rng(1);
        CHECK(BackoffDelayMs(p, 2, rng) == 100 && BackoffDelayMs(p, 3, rng) == 200 && BackoffDelayMs(p, 4, rng) == 400);
        CHECK(BackoffDelayMs(p, 5, rng) == 800 && BackoffDelayMs(p, 6, rng) == 1000 && BackoffDelayMs(p, 40, rng) == 1000);
        p.jitter = 0.5; bool spread = false; int first = -1; bool inside = true;
        for (int i = 0; i < 200; ++i) {
            const int d = BackoffDelayMs(p, 3, rng);
            if (d < 100 || d > 200) inside = false;
            if (first < 0) first = d; else if (d != first) spread = true;
        }
        CHECK(inside && spread);
        p.jitter = 1.0; inside = true;
        for (int i = 0; i < 200; ++i) { const int d = BackoffDelayMs(p, 2, rng); if (d < 0 || d > 100) inside = false; }
        CHECK(inside);
        std::mt19937_64 a(7), b(7); p.jitter = 0.5;
        CHECK(BackoffDelayMs(p, 4, a) == BackoffDelayMs(p, 4, b));   // a seed makes waits repeatable
    }
    {   // bad input
        DownloadSpec sp; sp.expectedSha256 = "abc";
        DownloadResult r = DownloadToFile("http://127.0.0.1:1/x", TmpBase() + "/okay_never_written", sp, Opts());
        CHECK(r.status == DownloadStatus::BadInput && !Exists(TmpBase() + "/okay_never_written.part"));
        sp.expectedSha256.clear(); sp.headers = {"Range: bytes=5-"};
        r = DownloadToFile("http://127.0.0.1:1/x", TmpBase() + "/okay_never_written", sp, Opts());
        CHECK(r.status == DownloadStatus::BadInput);
    }
}

// ---- the server cases -----------------------------------------------------------------------
static void TestServer(const std::string& dir) {
    Server s; s.handler = [&](int c, Req& r) { return Route(s, c, r); }; s.Start();
    const std::string base = "http://127.0.0.1:" + std::to_string(s.port);
    const std::string dest = dir + "/out.bin", part = dest + ".part";
    const NetOptions o = Opts();
    const std::string sha100k = GenSha(100000), sha300k = GenSha(300000);

    {   // plain download, hash checked, temp renamed, progress counts up to the size
        DownloadSpec sp; sp.expectedSha256 = sha100k;
        for (char& ch : sp.expectedSha256) ch = char(std::toupper((unsigned char)ch));   // upper-case hex is accepted
        uint64_t last = 0, total = 0; int calls = 0; bool monotone = true;
        sp.progress = [&](uint64_t d, uint64_t t) { if (d < last) monotone = false; last = d; total = t; ++calls; return true; };
        DownloadResult r = DownloadToFile(base + "/file?size=100000", dest, sp, o);
        CHECK(r.status == DownloadStatus::Ok && r.sha256Hex == sha100k && r.bytes == 100000 && r.httpStatus == 200);
        CHECK(Slurp(dest) == GenStr(0, 100000) && !Exists(part));
        CHECK(calls > 0 && monotone && last == 100000 && total == 100000 && r.retry.attempts == 1);
    }
    {   // redirects 301 -> 302 (absolute) -> 307 (relative, with a body) -> 308 -> the file
        DownloadSpec sp; sp.expectedSha256 = sha100k;
        DownloadResult r = DownloadToFile(base + "/r301?size=100000", dest, sp, o);
        CHECK(r.status == DownloadStatus::Ok && r.sha256Hex == sha100k);
        CHECK(s.Hits("/r301") == 1 && s.Hits("/r302") == 1 && s.Hits("/r307") == 1 && s.Hits("/r308") == 1);
        // a redirect loop: refused after the cap, nothing left behind, not retried (it would repeat)
        DownloadSpec lp; lp.retry = Fast(3);
        NetOptions lo = o; lo.limits.maxRedirects = 4;
        r = DownloadToFile(base + "/rloop", dir + "/loop.bin", lp, lo);
        CHECK(r.status == DownloadStatus::NetworkError && r.err == Err::TooManyRedirects && s.Hits("/rloop") == 5 && r.retry.attempts == 1);
        CHECK(!Exists(dir + "/loop.bin") && !Exists(dir + "/loop.bin.part"));
    }
    {   // chunked body with odd chunk sizes
        DownloadSpec sp; sp.expectedSha256 = sha300k;
        DownloadResult r = DownloadToFile(base + "/chunked?size=300000", dest, sp, o);
        CHECK(r.status == DownloadStatus::Ok && r.sha256Hex == sha300k && r.bytes == 300000);
    }
    {   // dropped connection: the second try asks for the rest (Range + If-Range) and gets a 206
        DownloadSpec sp; sp.expectedSha256 = sha300k; sp.retry = Fast(3);
        DownloadResult r = DownloadToFile(base + "/drop?size=300000&cut=70000&times=1", dest, sp, o);
        CHECK(r.status == DownloadStatus::Ok && r.sha256Hex == sha300k && r.resumes == 1 && r.restarts == 0 && r.retry.attempts == 2);
        const std::string h = s.LastHead();
        CHECK(h.find("Range: bytes=70000-") != std::string::npos && h.find("If-Range: \"v1\"") != std::string::npos);
        CHECK(Slurp(dest) == GenStr(0, 300000) && !Exists(part));
        // dropped three times, each try continuing where the last one stopped
        sp.retry = Fast(4); s.Reset("/drop");
        r = DownloadToFile(base + "/drop?size=300000&cut=50000&times=3", dir + "/d3.bin", sp, o);
        CHECK(r.status == DownloadStatus::Ok && r.sha256Hex == sha300k && r.resumes == 3 && r.retry.attempts == 4 && r.retry.delaysMs.size() == 3);
        // without retries the drop is a failure, and the temporary file is gone
        DownloadSpec one; one.expectedSha256 = sha300k;
        r = DownloadToFile(base + "/drop?size=300000&cut=50000&times=99", dir + "/d1.bin", one, o);
        CHECK(r.status == DownloadStatus::NetworkError && !Exists(dir + "/d1.bin") && !Exists(dir + "/d1.bin.part"));
        // ...unless the caller asked to keep it, and a later run picks it up (resumeExisting, checksum given)
        one.keepPartialOnFailure = true;
        r = DownloadToFile(base + "/drop?size=300000&cut=50000&times=99", dir + "/d2.bin", one, o);
        CHECK(r.status == DownloadStatus::NetworkError && Exists(dir + "/d2.bin.part") && !Exists(dir + "/d2.bin"));
        DownloadSpec again; again.expectedSha256 = sha300k; again.resumeExisting = true;
        r = DownloadToFile(base + "/file?size=300000", dir + "/d2.bin", again, o);
        CHECK(r.status == DownloadStatus::Ok && r.resumes == 1 && r.sha256Hex == sha300k && !Exists(dir + "/d2.bin.part"));
        CHECK(s.LastHead().find("Range: bytes=50000-") != std::string::npos);
    }
    {   // a chunked answer cut short, resumed as a 206
        DownloadSpec sp; sp.expectedSha256 = sha300k; sp.retry = Fast(2);
        DownloadResult r = DownloadToFile(base + "/drop-chunked?size=300000&cut=90000", dest, sp, o);
        CHECK(r.status == DownloadStatus::Ok && r.sha256Hex == sha300k && r.resumes == 1);
    }
    {   // stalled connection: the silence limit ends the try, the next one resumes
        DownloadSpec sp; sp.expectedSha256 = sha300k; sp.retry = Fast(2);
        NetOptions so = o; so.ioMs = 400;
        auto t0 = std::chrono::steady_clock::now();
        DownloadResult r = DownloadToFile(base + "/stall?size=300000&cut=120000&times=1", dest, sp, so);
        CHECK(r.status == DownloadStatus::Ok && r.resumes == 1 && Secs(t0) < 5.0);
    }
    {   // slow but steady: no retry needed
        DownloadSpec sp; sp.expectedSha256 = GenSha(120000);
        auto t0 = std::chrono::steady_clock::now();
        DownloadResult r = DownloadToFile(base + "/slow?size=120000", dest, sp, o);
        CHECK(r.status == DownloadStatus::Ok && r.retry.attempts == 1 && Secs(t0) > 0.05);
        // ...and a total limit shorter than the trickle ends it (a timeout is retried, so give it one try only)
        NetOptions to = o; to.totalMs = 30;
        r = DownloadToFile(base + "/slow?size=120000", dir + "/slow.bin", DownloadSpec(), to);
        CHECK(r.status == DownloadStatus::NetworkError && r.err == Err::Timeout && !Exists(dir + "/slow.bin.part"));
    }
    {   // a server that ignores Range: the 200 starts the file over, the result is still right
        DownloadSpec sp; sp.expectedSha256 = sha300k; sp.retry = Fast(3);
        DownloadResult r = DownloadToFile(base + "/norange?size=300000&cut=40000", dest, sp, o);
        CHECK(r.status == DownloadStatus::Ok && r.sha256Hex == sha300k && r.restarts == 1 && r.resumes == 0);
    }
    {   // the file changed between tries: If-Range "v1" no longer matches, the server sends all of v2 (200)
        DownloadSpec sp; sp.retry = Fast(3);
        DownloadResult r = DownloadToFile(base + "/changed?size=300000&cut=40000", dest, sp, o);
        CHECK(r.status == DownloadStatus::Ok && r.sha256Hex == GenSha(300000, 2) && r.restarts == 1);
    }
    {   // a 206 that starts at the wrong byte is refused (never stitched in); not retried
        DownloadSpec sp; sp.retry = Fast(3);
        DownloadResult r = DownloadToFile(base + "/badrange?size=300000&cut=40000", dir + "/br.bin", sp, o);
        CHECK(r.status == DownloadStatus::NetworkError && r.err == Err::BadResponse && !Exists(dir + "/br.bin") && !Exists(dir + "/br.bin.part"));
    }
    {   // 5xx then success; Retry-After in seconds is used as the wait
        DownloadSpec sp; sp.expectedSha256 = sha100k; sp.retry = Fast(4);
        DownloadResult r = DownloadToFile(base + "/flaky?n=2&code=503", dest, sp, o);
        CHECK(r.status == DownloadStatus::Ok && r.retry.attempts == 3 && r.retry.delaysMs.size() == 2);
        CHECK(r.retry.delaysMs.size() == 2 && r.retry.delaysMs[0] >= 10 && r.retry.delaysMs[0] <= 20 && r.retry.delaysMs[1] >= 20 && r.retry.delaysMs[1] <= 40);
        sp.retry.sleeper = [](int) {};   // record the wait, do not sleep it
        DownloadSpec ra = sp; ra.retry.seed = 1;
        s.Reset("/flaky");
        r = DownloadToFile(base + "/flaky?n=1&code=503&ra=7", dest, ra, o);
        CHECK(r.status == DownloadStatus::Ok && r.retry.delaysMs.size() == 1 && r.retry.delaysMs[0] == 7000);
        // 429 with an HTTP date about two seconds ahead: really waited
        DownloadSpec d; d.retry = Fast(2);
        auto t0 = std::chrono::steady_clock::now();
        r = DownloadToFile(base + "/429date?size=1000", dest, d, o);
        CHECK(r.status == DownloadStatus::Ok && r.retry.delaysMs.size() == 1 && r.retry.delaysMs[0] >= 900 && r.retry.delaysMs[0] <= 2000 && Secs(t0) >= 0.85);
        // a Retry-After longer than the policy allows: give up at once, with the answer
        DownloadSpec lw; lw.retry = Fast(5);
        r = DownloadToFile(base + "/longwait", dir + "/lw.bin", lw, o);
        CHECK(r.status == DownloadStatus::HttpError && r.httpStatus == 503 && r.retry.attempts == 1 && r.retry.gaveUpOnRetryAfter && !Exists(dir + "/lw.bin.part"));
        // a server that never recovers: all tries used, the last status reported
        DownloadSpec al; al.retry = Fast(3);
        r = DownloadToFile(base + "/always503", dir + "/al.bin", al, o);
        CHECK(r.status == DownloadStatus::HttpError && r.httpStatus == 503 && r.retry.attempts == 3 && s.Hits("/always503") == 3);
        // 404 is final
        r = DownloadToFile(base + "/404", dir + "/nf.bin", al, o);
        CHECK(r.status == DownloadStatus::HttpError && r.httpStatus == 404 && r.retry.attempts == 1 && !Exists(dir + "/nf.bin.part"));
        // a refused connection is retried (and fails in the end)
        int deadPort = 0; { int fd = Listen(deadPort); ::close(fd); }
        r = DownloadToFile("http://127.0.0.1:" + std::to_string(deadPort) + "/x", dir + "/dead.bin", al, o);
        CHECK(r.status == DownloadStatus::NetworkError && r.err == Err::Connect && r.retry.attempts == 3 && !r.requestSent);
    }
    {   // a wrong checksum fails, removes the temporary file, and leaves an existing destination alone
        { std::ofstream f(dir + "/keep.bin", std::ios::binary); f << "old"; }
        DownloadSpec sp; sp.expectedSha256 = std::string(64, '0'); sp.retry = Fast(3);
        DownloadResult r = DownloadToFile(base + "/file?size=100000", dir + "/keep.bin", sp, o);
        CHECK(r.status == DownloadStatus::ChecksumMismatch && r.sha256Hex == sha100k && r.retry.attempts == 1);
        CHECK(!Exists(dir + "/keep.bin.part") && Slurp(dir + "/keep.bin") == "old");
        // expected size: too small fails as soon as it is announced, nothing kept
        DownloadSpec sz; sz.expectedSize = 99999;
        r = DownloadToFile(base + "/file?size=100000", dir + "/sz.bin", sz, o);
        CHECK(r.status == DownloadStatus::SizeMismatch && !Exists(dir + "/sz.bin.part") && !Exists(dir + "/sz.bin"));
        // size cap
        DownloadSpec cap; cap.maxBytes = 50000;
        r = DownloadToFile(base + "/file?size=100000", dir + "/cap.bin", cap, o);
        CHECK(r.status == DownloadStatus::TooLarge && !Exists(dir + "/cap.bin.part"));
        r = DownloadToFile(base + "/chunked?size=100000", dir + "/cap.bin", cap, o);
        CHECK(r.status == DownloadStatus::TooLarge && !Exists(dir + "/cap.bin.part"));
        // cannot create the temporary file
        r = DownloadToFile(base + "/file", dir + "/no/such/dir/x.bin", DownloadSpec(), o);
        CHECK(r.status == DownloadStatus::WriteFailed);
    }
    {   // cancel: from the progress callback, from the cancel check during a stall, and during a retry wait
        DownloadSpec sp;
        sp.progress = [](uint64_t d, uint64_t) { return d < 20000; };
        DownloadResult r = DownloadToFile(base + "/slow?size=120000", dir + "/c1.bin", sp, o);
        CHECK(r.status == DownloadStatus::Cancelled && !Exists(dir + "/c1.bin.part") && !Exists(dir + "/c1.bin"));
        std::atomic<bool> flag{false};
        DownloadSpec cs; cs.cancel = [&] { return flag.load(); };
        std::thread t([&] { std::this_thread::sleep_for(std::chrono::milliseconds(300)); flag = true; });
        auto t0 = std::chrono::steady_clock::now();
        r = DownloadToFile(base + "/stall?size=300000&cut=1000&times=99", dir + "/c2.bin", cs, o);
        t.join();
        CHECK(r.status == DownloadStatus::Cancelled && Secs(t0) < 2.0 && !Exists(dir + "/c2.bin.part"));
        flag = false;
        DownloadSpec cw; cw.cancel = [&] { return flag.load(); }; cw.retry.maxAttempts = 5; cw.retry.baseDelayMs = 60000;
        std::thread t2([&] { std::this_thread::sleep_for(std::chrono::milliseconds(300)); flag = true; });
        t0 = std::chrono::steady_clock::now();
        r = DownloadToFile(base + "/always503", dir + "/c3.bin", cw, o);
        t2.join();
        CHECK(r.status == DownloadStatus::Cancelled && Secs(t0) < 2.0);
    }
    {   // PerformWithRetry: in memory, gzip, idempotent methods only
        RetryPolicy p = Fast(3); RetryReport rep;
        NetOptions go = o; go.acceptGzip = true;
        FetchResult r = PerformWithRetry("GET", base + "/gzip?size=200000", {}, "", go, p, &rep);
        CHECK(r.ok && r.response.status == 200 && r.response.body == GenStr(0, 200000) && rep.attempts == 1);
        rep = RetryReport();
        r = PerformWithRetry("GET", base + "/gzip-flaky?size=5000", {}, "", go, p, &rep);
        CHECK(r.ok && r.response.status == 200 && r.response.body == GenStr(0, 5000) && rep.attempts == 2);
        rep = RetryReport();
        r = PerformWithRetry("POST", base + "/post", {}, "x", o, p, &rep);   // never retried: may have acted
        CHECK(r.ok && r.response.status == 503 && rep.attempts == 1 && s.Hits("/post") == 1);
        rep = RetryReport();
        r = PerformWithRetry("PUT", base + "/post", {}, "x", o, p, &rep);    // idempotent: retried
        CHECK(r.ok && r.response.status == 503 && rep.attempts == 3 && s.Hits("/post") == 4);
        rep = RetryReport();
        r = PerformWithRetry("GET", base + "/refuse-after", {}, "", o, p, &rep);   // closed with no answer: retried
        CHECK(!r.ok && rep.attempts == 3 && s.Hits("/refuse-after") == 3);
        rep = RetryReport();
        r = PerformWithRetry("GET", base + "/404", {}, "", o, p, &rep);
        CHECK(r.ok && r.response.status == 404 && rep.attempts == 1);
        // the system gzip program's output too (real deflate blocks), when it is installed
        const std::string raw = dir + "/gz-src.bin";
        { std::ofstream f(raw, std::ios::binary); f << GenStr(0, 50000) << std::string(50000, 'a'); }
        if (std::system(("gzip -c -9 '" + raw + "' > '" + raw + ".gz' 2>/dev/null").c_str()) == 0) {
            const std::string gz = Slurp(raw + ".gz");
            Server g; g.handler = [&](int c, Req&) { SendAll(c, "HTTP/1.1 200 OK\r\nContent-Encoding: gzip\r\nContent-Length: " + std::to_string(gz.size()) + "\r\n\r\n" + gz); return false; };
            g.Start();
            r = PerformWithRetry("GET", "http://127.0.0.1:" + std::to_string(g.port) + "/", {}, "", go, p, nullptr);
            CHECK(r.ok && r.response.body == Slurp(raw) && gz.size() < 80000);
        } else { std::printf("SKIP gzip program output: gzip not installed\n"); ++g_skips; }
    }
    {   // a plain HTTP proxy (absolute form), with a resume through it
        TestProxy px; px.Start();
        const std::string proxyAddr = "http://127.0.0.1:" + std::to_string(px.srv.port);
        NetOptions po = o;
        po.env = [proxyAddr](const char* n) -> const char* { return std::strcmp(n, "http_proxy") == 0 ? proxyAddr.c_str() : nullptr; };
        DownloadSpec sp; sp.expectedSha256 = sha300k; sp.retry = Fast(3);
        s.Reset("/drop");
        DownloadResult r = DownloadToFile(base + "/drop?size=300000&cut=60000&times=1", dest, sp, po);
        CHECK(r.status == DownloadStatus::Ok && r.resumes == 1 && px.forwards == 2 && px.connects == 0);
        px.srv.Stop();
    }
    s.Stop();
}

// ---- large file, streamed: memory must stay flat ------------------------------------------------
static void TestLarge(const std::string& dir) {
    const char* mbEnv = std::getenv("OKAY_HTTP_BIG_MB");
    const uint64_t mb = mbEnv ? std::strtoull(mbEnv, nullptr, 10) : 200;
    if (mb == 0) { std::printf("SKIP large file (OKAY_HTTP_BIG_MB=0)\n"); ++g_skips; return; }
    const uint64_t size = mb << 20;
    Server s; s.handler = [&](int c, Req& r) { return Route(s, c, r); }; s.Start();
    const std::string want = GenSha(size);
    DownloadSpec sp; sp.expectedSha256 = want; sp.retry = Fast(3);
    const long before = RssKb();
    long peak = before;
    sp.progress = [&](uint64_t d, uint64_t) { if ((d & ((8u << 20) - 1)) < 16384) { const long k = RssKb(); if (k > peak) peak = k; } return true; };
    const std::string dest = dir + "/large.bin";
    auto t0 = std::chrono::steady_clock::now();
    // dropped once at 60 % to resume a big file as well
    DownloadResult r = DownloadToFile("http://127.0.0.1:" + std::to_string(s.port) + "/drop?size=" + std::to_string(size) + "&cut=" + std::to_string(size / 10 * 6) + "&times=1", dest, sp, Opts());
    const double secs = Secs(t0);
    struct stat st{}; ::stat(dest.c_str(), &st);
    CHECK(r.status == DownloadStatus::Ok && r.sha256Hex == want && uint64_t(st.st_size) == size && r.resumes == 1);
    if (r.status != DownloadStatus::Ok) std::printf("  large download: %s %s (%s)\n", DownloadStatusName(r.status), r.message.c_str(), ErrName(r.err));
    const long growKb = peak - before;
    std::printf("large file: %llu MB in %.2f s (resumed once), resident memory grew by %ld KB during the transfer\n", (unsigned long long)mb, secs, growKb);
    CHECK(before > 0 && growKb < 16 * 1024);   // flat: well under the file size (the buffers are 16 KiB and 64 KiB)
    ::unlink(dest.c_str());
    s.Stop();
}

// ---- TLS through a CONNECT proxy ------------------------------------------------------------
struct SServer {
    pid_t pid = -1; int port = 0;
    bool Start(const std::string& docs, const std::string& cert, const std::string& key) {
        int fd = Listen(port); ::close(fd);
        pid = ::fork();
        if (pid == 0) {
            if (::chdir(docs.c_str()) != 0) _exit(126);
            int nul = ::open("/dev/null", O_RDWR); ::dup2(nul, 0); ::dup2(nul, 1); ::dup2(nul, 2);
            const std::string ps = std::to_string(port);
            ::execlp("openssl", "openssl", "s_server", "-accept", ps.c_str(), "-cert", cert.c_str(), "-key", key.c_str(), "-HTTP", "-quiet", (char*)nullptr);
            _exit(127);
        }
        for (int i = 0; i < 100; ++i) {
            int c = ConnectLocal(port);
            if (c >= 0) { ::close(c); std::this_thread::sleep_for(std::chrono::milliseconds(50)); return true; }
            int st; if (::waitpid(pid, &st, WNOHANG) == pid) { pid = -1; return false; }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        return false;
    }
    void Stop() { if (pid > 0) { ::kill(pid, SIGTERM); int st; ::waitpid(pid, &st, 0); pid = -1; } }
    ~SServer() { Stop(); }
};

static void TestTlsProxy(const std::string& script, const std::string& dir) {
    const bool required = std::getenv("OKAY_REQUIRE_TLS_TESTS") != nullptr;
    auto skip = [&](const std::string& m) { std::printf("SKIP TLS through CONNECT: %s\n", m.c_str()); ++g_skips; if (required) { ++g_fail; std::printf("FAIL: OKAY_REQUIRE_TLS_TESTS is set\n"); } };
    std::string why;
    if (!TlsAvailable(Opts(), why)) { skip(why); return; }
    if (std::system("openssl version >/dev/null 2>&1") != 0) { skip("the openssl program is not installed"); return; }
    const std::string certs = dir + "/certs", docs = dir + "/docs";
    if (std::system(("sh '" + script + "' '" + certs + "' >/dev/null 2>&1").c_str()) != 0) { skip("certificate script failed"); return; }
    if (std::system(("mkdir -p '" + docs + "'").c_str()) != 0) { skip("no docs folder"); return; }
    const uint64_t size = 1 << 20;
    { std::ofstream f(docs + "/dl.http", std::ios::binary); f << "HTTP/1.1 200 OK\r\nContent-Length: " << size << "\r\nConnection: close\r\n\r\n" << GenStr(0, size); }
    SServer ss;
    if (!ss.Start(docs, certs + "/good.crt", certs + "/good.key")) { skip("openssl s_server did not start"); return; }
    TestProxy px; px.Start();
    const std::string proxyAddr = "http://127.0.0.1:" + std::to_string(px.srv.port);
    NetOptions o = Opts(); o.caFile = certs + "/ca.pem";
    o.env = [proxyAddr](const char* n) -> const char* { return (std::strcmp(n, "HTTPS_PROXY") == 0) ? proxyAddr.c_str() : nullptr; };
    const std::string url = "https://localhost:" + std::to_string(ss.port) + "/dl.http";
    DownloadSpec sp; sp.expectedSha256 = GenSha(size);
    DownloadResult r = DownloadToFile(url, dir + "/tls.bin", sp, o);
    CHECK(r.status == DownloadStatus::Ok && r.bytes == size && px.connects == 1 && px.forwards == 0);
    if (r.status != DownloadStatus::Ok) std::printf("  tls download: %s %s\n", DownloadStatusName(r.status), r.message.c_str());
    // a wrong checksum over TLS: refused, nothing left
    sp.expectedSha256 = std::string(64, 'f');
    r = DownloadToFile(url, dir + "/tls2.bin", sp, o);
    CHECK(r.status == DownloadStatus::ChecksumMismatch && !Exists(dir + "/tls2.bin") && !Exists(dir + "/tls2.bin.part"));
    // a certificate for another name behind the proxy is still refused (no retry turns that into a success)
    SServer bad;
    if (bad.Start(docs, certs + "/wronghost.crt", certs + "/wronghost.key")) {
        DownloadSpec b; b.retry = Fast(3);
        r = DownloadToFile("https://localhost:" + std::to_string(bad.port) + "/dl.http", dir + "/tls3.bin", b, o);
        CHECK(r.status == DownloadStatus::NetworkError && r.err == Err::Tls && r.retry.attempts == 1 && !Exists(dir + "/tls3.bin.part"));
    }
    px.srv.Stop();
}

// ---- bench modes ------------------------------------------------------------------------------
static int Serve() {
    Server s; s.handler = [&](int c, Req& r) { return Route(s, c, r); }; s.Start();
    std::printf("%d\n", s.port); std::fflush(stdout);
    char b[64]; while (std::fread(b, 1, sizeof b, stdin) > 0) {}
    s.Stop();
    return 0;
}
static int Get(const std::string& url, const std::string& out, const std::string& flags) {
    NetOptions o; o.env = [](const char*) -> const char* { return nullptr; };   // local server: no proxy
    DownloadSpec sp; sp.syncBeforeRename = flags.find("nosync") == std::string::npos; sp.computeSha256 = flags.find("nohash") == std::string::npos;
    DownloadResult r = DownloadToFile(url, out, sp, o);
    std::printf("%s %s %llu\n", DownloadStatusName(r.status), r.sha256Hex.c_str(), (unsigned long long)r.bytes);
    return r.status == DownloadStatus::Ok ? 0 : 1;
}

/// SHA-256 of a file with the engine's own hash (what a caller does after a curl download to check it).
static int Sha(const std::string& path) {
    const int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0) return 1;
    okay::crypto::Sha256 h; std::vector<char> buf(1 << 16); ssize_t n;
    while ((n = ::read(fd, buf.data(), buf.size())) > 0) h.Update(reinterpret_cast<const uint8_t*>(buf.data()), size_t(n));
    ::close(fd);
    uint8_t d[32]; h.Final(d);
    static const char* k = "0123456789abcdef"; std::string s;
    for (uint8_t b : d) { s += k[b >> 4]; s += k[b & 15]; }
    std::printf("%s\n", s.c_str());
    return 0;
}

int main(int argc, char** argv) {
    std::signal(SIGPIPE, SIG_IGN);
    if (argc > 1 && std::string(argv[1]) == "--serve") return Serve();
    if (argc > 3 && std::string(argv[1]) == "--get") return Get(argv[2], argv[3], argc > 4 ? argv[4] : "");
    if (argc > 2 && std::string(argv[1]) == "--sha") return Sha(argv[2]);
    const std::string script = argc > 1 ? argv[1] : "tests/scripts/gen_http_test_certs.sh";
    std::string tmpl = TmpBase() + "/okay_http_dl_XXXXXX";
    if (!::mkdtemp(&tmpl[0])) { std::perror("mkdtemp"); return 2; }
    const std::string dir = tmpl;
    TestPure();
    TestServer(dir);
    TestLarge(dir);
    TestTlsProxy(script, dir);
    if (std::system(("rm -rf '" + dir + "'").c_str()) != 0) {}
    std::printf("own_http_download: %d checks, %d failed, %d skipped sections\n", g_checks, g_fail, g_skips);
    return g_fail ? 1 : 0;
}
