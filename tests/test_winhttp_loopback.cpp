// The HTTP seam (okay/Platform/NativeFetch.hpp) on WinHTTP, against a loopback server on 127.0.0.1: the Windows half of
// claude/third-party-1 ("stop the shipped programs from spawning curl"). The curl fallback is switched off
// (OKAY_NO_CURL=1) and PATH is emptied, so every answer here comes from WinHTTP or the case fails.
// Checks: WinHTTP is compiled in (it was silently off from #1001 to claude/third-party-1), downloads (success, redirect,
// 404, 503 retries, a size cap, cancel, a cut answer resumed with Range + If-Range, a file that changed (200 to the
// resume: started again), no validator (the next try starts from 0), only a checksum (a resumed file that fails it is fetched again from 0)), HEAD, a Range prefix fetch, a POST with headers and
// a body, and the PR #1186 review fixes: Send's timeout, a cancel and the whole-call limit on a silent server (before the
// fix they waited for the receive timeout), a 407 that is no answer (curl would be tried), no redirect for a call with a
// credential header, CR/LF taken out of header lines, a body file with a non-ASCII name and one that is missing. Windows only (built by the Windows test job; also runs under Wine). No <thread>/<mutex> (rule 21a):
// the server runs on a Win32 thread.
#include <winsock2.h>   // before windows.h (NativeHttp.hpp includes it)
#include <ws2tcpip.h>
#include <windows.h>

#include "okay/Platform/NativeFetch.hpp"

#include <cstdio>
#include <string>

#if !defined(OKAY_HTTP_WINHTTP)
#error "WinHTTP must be the native client of the Windows desktop build"
#endif

using namespace okay::http;
namespace fs = std::filesystem;
static int g_fail = 0, g_checks = 0;
#define CHECK(c) do { ++g_checks; if (!(c)) { ++g_fail; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
// Each part of a download check on its own line, so a failure says which part: the outcome (with its value and the
// seam's error text), the destination file, a leftover .part file.
#define CHECK_OUTCOME(r, want) do { ++g_checks; if ((r).outcome != (want)) { ++g_fail; \
    std::printf("FAIL %s:%d: outcome %d, wanted %d (%s); status %ld, attempts %d, error \"%s\"\n", __FILE__, __LINE__, \
                (int)(r).outcome, (int)(want), #want, (long)(r).status, (r).attempts, (r).error.c_str()); } } while (0)
#define CHECK_MS(t0, limit) do { ++g_checks; const DWORD ms_ = GetTickCount() - (t0); if (ms_ >= (DWORD)(limit)) { ++g_fail; \
    std::printf("FAIL %s:%d: took %lu ms, limit %d ms\n", __FILE__, __LINE__, (unsigned long)ms_, (int)(limit)); } } while (0)

static SOCKET g_listen = INVALID_SOCKET;
static int g_port = 0;
static volatile LONG g_flaky = 0, g_trunc = 0, g_stop = 0, g_shaFull = 0, g_shaRange = 0;
static std::string Changed() { return std::string(5000, 'z'); }
static void QuietWait() { for (int i = 0; i < 400 && !g_stop; ++i) Sleep(25); }   // up to 10 s of silence
static std::string g_lastHead;   // written by the server thread before it answers, read after the call returned
static CRITICAL_SECTION g_cs;

static void SendAll(SOCKET c, const std::string& s) {
    size_t o = 0;
    while (o < s.size()) { const int n = ::send(c, s.data() + o, (int)(s.size() - o), 0); if (n <= 0) return; o += (size_t)n; }
}
static std::string Ok(const std::string& body, const std::string& extra = "") {
    return "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: " + std::to_string(body.size()) + "\r\n" + extra + "\r\n" + body;
}
static std::string Payload() { std::string p(5000, 'p'); for (size_t i = 0; i < p.size(); ++i) p[i] = char('a' + i % 26); return p; }

static void Serve(SOCKET c) {
    std::string in; char b[4096];
    while (in.find("\r\n\r\n") == std::string::npos) { const int n = ::recv(c, b, sizeof b, 0); if (n <= 0) return; in.append(b, (size_t)n); }
    const size_t he = in.find("\r\n\r\n");
    std::string head = in.substr(0, he + 4), rest = in.substr(he + 4);
    size_t want = 0;
    { const size_t p = head.find("Content-Length: "); if (p != std::string::npos) want = (size_t)std::atoi(head.c_str() + p + 16); }
    while (rest.size() < want) { const int n = ::recv(c, b, sizeof b, 0); if (n <= 0) break; rest.append(b, (size_t)n); }
    EnterCriticalSection(&g_cs); g_lastHead = head; LeaveCriticalSection(&g_cs);
    const size_t a = head.find(' '), z = head.find(' ', a + 1);
    const std::string method = head.substr(0, a), path = head.substr(a + 1, z - a - 1);
    const std::string pl = Payload();
    if (path == "/ok") { SendAll(c, method == "HEAD" ? "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: 5000\r\n\r\n" : Ok(pl)); }
    else if (path == "/redir") SendAll(c, "HTTP/1.1 302 Found\r\nConnection: close\r\nLocation: /ok\r\nContent-Length: 0\r\n\r\n");
    else if (path == "/missing") SendAll(c, "HTTP/1.1 404 Not Found\r\nConnection: close\r\nContent-Length: 4\r\n\r\nnope");
    else if (path == "/flaky") { if (InterlockedIncrement(&g_flaky) <= 2) SendAll(c, "HTTP/1.1 503 Busy\r\nConnection: close\r\nContent-Length: 0\r\n\r\n"); else SendAll(c, Ok(pl)); }
    else if (path == "/trunc") {   // the first answer dies half way; a later one honours Range
        const size_t r = head.find("Range: bytes=");
        if (r == std::string::npos || head.find("If-Range: \"v1\"\r\n") == std::string::npos) {   // no If-Range: the whole file
            InterlockedIncrement(&g_trunc);
            SendAll(c, "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: 5000\r\nETag: \"v1\"\r\n\r\n" + pl.substr(0, 2000));
        } else {
            const size_t from = (size_t)std::atoi(head.c_str() + r + 13);
            const std::string part = pl.substr(from);
            SendAll(c, "HTTP/1.1 206 Partial Content\r\nConnection: close\r\nContent-Length: " + std::to_string(part.size()) +
                       "\r\nContent-Range: bytes " + std::to_string(from) + "-4999/5000\r\n\r\n" + part);
        }
    }
    else if (path == "/changed") {   // cut half way (ETag v1); the resume's If-Range no longer matches: 200, the new file
        if (head.find("Range: bytes=") == std::string::npos)
            SendAll(c, "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: 5000\r\nETag: \"v1\"\r\n\r\n" + pl.substr(0, 2000));
        else
            SendAll(c, Ok(Changed(), "ETag: \"v2\"\r\n"));
    }
    else if (path == "/novalid") {   // cut half way with no ETag and no Last-Modified: the next try must not send Range
        if (head.find("Range: bytes=") == std::string::npos && InterlockedIncrement(&g_trunc) % 2 == 1)
            SendAll(c, "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: 5000\r\n\r\n" + pl.substr(0, 2000));
        else SendAll(c, Ok(pl));
    }
    else if (path == "/shachange") {   // no validator; cut half way, and the file changes to Changed() before the resume
        const size_t r = head.find("Range: bytes=");
        const std::string now = Changed();
        if (r != std::string::npos) {
            InterlockedIncrement(&g_shaRange);
            const size_t from = (size_t)std::atoi(head.c_str() + r + 13);
            SendAll(c, "HTTP/1.1 206 Partial Content\r\nConnection: close\r\nContent-Length: " + std::to_string(now.size() - from) +
                       "\r\nContent-Range: bytes " + std::to_string(from) + "-4999/5000\r\n\r\n" + now.substr(from));
        } else if (InterlockedIncrement(&g_shaFull) == 1) {
            SendAll(c, "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: 5000\r\n\r\n" + pl.substr(0, 2000));
        } else SendAll(c, Ok(now));
    }
    else if (path == "/silent") QuietWait();   // never answers
    else if (path == "/stall") { SendAll(c, "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: 1000\r\n\r\n" + pl.substr(0, 100)); QuietWait(); }
    else if (path == "/proxyauth") SendAll(c, "HTTP/1.1 407 Proxy Authentication Required\r\nConnection: close\r\nProxy-Authenticate: Basic realm=\"x\"\r\nContent-Length: 0\r\n\r\n");
    else if (path == "/slow") { SendAll(c, "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: 100000\r\n\r\n");
        for (int i = 0; i < 400 && !g_stop; ++i) { SendAll(c, std::string(250, 's')); Sleep(25); } }
    else if (path == "/page") SendAll(c, "HTTP/1.1 206 Partial Content\r\nConnection: close\r\nContent-Length: 100\r\nContent-Range: bytes 0-99/5000\r\n\r\n" + pl.substr(0, 100));
    else if (path == "/echo") SendAll(c, "HTTP/1.1 201 Created\r\nConnection: close\r\nContent-Length: " + std::to_string(rest.size()) + "\r\n\r\n" + rest);
    else SendAll(c, "HTTP/1.1 500 What\r\nConnection: close\r\nContent-Length: 0\r\n\r\n");
}
static DWORD WINAPI ConnThread(LPVOID p) { SOCKET c = (SOCKET)(UINT_PTR)p; Serve(c); ::shutdown(c, SD_BOTH); ::closesocket(c); return 0; }
static DWORD WINAPI AcceptThread(LPVOID) {
    while (!g_stop) {
        SOCKET c = ::accept(g_listen, nullptr, nullptr);
        if (c == INVALID_SOCKET) break;
        HANDLE h = ::CreateThread(nullptr, 0, ConnThread, (LPVOID)(UINT_PTR)c, 0, nullptr);
        if (h) ::CloseHandle(h); else ::closesocket(c);
    }
    return 0;
}
static std::string LastHead() { EnterCriticalSection(&g_cs); std::string h = g_lastHead; LeaveCriticalSection(&g_cs); return h; }
static std::string Slurp(const fs::path& p) { std::ifstream f(p, std::ios::binary); return std::string((std::istreambuf_iterator<char>(f)), {}); }
static bool NoParts(const fs::path& dir) {
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(dir, ec)) if (e.path().string().find(".part") != std::string::npos) return false;
    return true;
}

int main() {
    InitializeCriticalSection(&g_cs);
    _putenv("OKAY_NO_CURL=1");   // the curl fallback is off: a WinHTTP miss shows up as a failure
    _putenv("PATH=");
    _putenv("OKAY_HTTP_CURL=");
    CHECK(NativeAvailable());
    WSADATA wsa; if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) { std::printf("no winsock\n"); return 2; }
    g_listen = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in a{}; a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::bind(g_listen, (sockaddr*)&a, sizeof a) != 0 || ::listen(g_listen, 16) != 0) { std::printf("bind failed\n"); return 2; }
    int l = sizeof a; ::getsockname(g_listen, (sockaddr*)&a, &l); g_port = ntohs(a.sin_port);
    HANDLE acc = ::CreateThread(nullptr, 0, AcceptThread, nullptr, 0, nullptr);
    const std::string base = "http://127.0.0.1:" + std::to_string(g_port);

    std::error_code ec;
    const fs::path dir = fs::temp_directory_path(ec) / ("okay_winhttp_test_" + std::to_string(GetCurrentProcessId()));
    fs::create_directories(dir, ec);
    const fs::path out = dir / "file.bin";
    const std::string pl = Payload();

    {   // a plain download, with progress and the user agent / extra headers sent
        OwnDownloadSpec sp; sp.userAgent = "OkayTest/1"; sp.headers = {"Cache-Control: no-cache"};
        uint64_t last = 0; sp.onBytes = [&](uint64_t n) { last = n; };
        OwnDownloadResult r = FetchToFile(base + "/ok", out, sp);
        CHECK(r.outcome == OwnOutcome::Ok && !r.viaCurl && Slurp(out) == pl && r.bytes == 5000 && last == 5000);
        const std::string h = LastHead();
        CHECK(h.find("User-Agent: OkayTest/1") != std::string::npos && h.find("Cache-Control: no-cache") != std::string::npos);
        CHECK(NoParts(dir));
        r = FetchToFile(base + "/redir", out, sp);
        CHECK(r.outcome == OwnOutcome::Ok && Slurp(out) == pl);
        sp.expectedSha256 = std::string(64, '0');   // a wrong checksum: nothing is kept
        r = FetchToFile(base + "/ok", out, sp);
        CHECK_OUTCOME(r, OwnOutcome::ChecksumMismatch); CHECK(!fs::exists(out)); CHECK(NoParts(dir));
    }
    {   // a 404 is final (no retry, no file); 503 twice then 200 with two retries
        OwnDownloadSpec sp; sp.retries = 2; sp.retryDelayMs = 10;
        OwnDownloadResult r = FetchToFile(base + "/missing", out, sp);
        CHECK_OUTCOME(r, OwnOutcome::HttpError); CHECK(r.status == 404); CHECK(r.attempts == 1); CHECK(!fs::exists(out));
        r = FetchToFile(base + "/flaky", out, sp);
        CHECK(r.outcome == OwnOutcome::Ok && r.attempts == 3 && Slurp(out) == pl);
    }
    {   // a size cap (announced and counted)
        OwnDownloadSpec sp; sp.maxBytes = 1000;
        OwnDownloadResult r = FetchToFile(base + "/ok", out, sp);
        CHECK_OUTCOME(r, OwnOutcome::TooBig); CHECK(!fs::exists(out)); CHECK(NoParts(dir));
    }
    {   // cancel while the data trickles in: ends quickly, nothing kept
        OwnDownloadSpec sp; int polls = 0;
        sp.cancelled = [&] { return ++polls > 20; };
        const DWORD t0 = GetTickCount();
        OwnDownloadResult r = FetchToFile(base + "/slow", out, sp);
        CHECK_OUTCOME(r, OwnOutcome::Cancelled); CHECK(!fs::exists(out)); CHECK(NoParts(dir)); CHECK_MS(t0, 8000);
    }
    {   // a cut answer: without retries it is no clear answer (and curl is off); with one retry it resumes with Range
        OwnDownloadSpec sp; sp.retryDelayMs = 10;
        OwnDownloadResult r = FetchToFile(base + "/trunc", out, sp);
        CHECK_OUTCOME(r, OwnOutcome::NotHandled); CHECK(!fs::exists(out)); CHECK(NoParts(dir));
        sp.retries = 1;
        r = FetchToFile(base + "/trunc", out, sp);
        CHECK(r.outcome == OwnOutcome::Ok && r.resumes == 1 && Slurp(out) == pl);
        CHECK(LastHead().find("Range: bytes=2000-") != std::string::npos);
        CHECK(LastHead().find("If-Range: \"v1\"") != std::string::npos);   // the first answer's ETag
        r = FetchToFile(base + "/changed", out, sp);   // the file changed between the tries: started again, never glued
        CHECK(r.outcome == OwnOutcome::Ok && r.resumes == 0 && Slurp(out) == Changed());
        CHECK(LastHead().find("If-Range: \"v1\"") != std::string::npos);
        g_trunc = 0;
        r = FetchToFile(base + "/novalid", out, sp);   // no validator, no checksum: the second try starts from byte 0
        CHECK(r.outcome == OwnOutcome::Ok && r.resumes == 0 && Slurp(out) == pl);
        CHECK(LastHead().find("Range:") == std::string::npos);
        // no validator but an expected SHA-256: the resume (no If-Range to send) glues the new file's tail onto the old
        // start; the checksum fails, and the file is fetched once more from byte 0 instead of ending as ChecksumMismatch
        const fs::path want = dir / "want.bin";
        { std::ofstream f(want, std::ios::binary); f << Changed(); }
        sp.expectedSha256 = fetch_detail::Sha256File(want);
        g_shaFull = 0; g_shaRange = 0;
        r = FetchToFile(base + "/shachange", out, sp);
        CHECK_OUTCOME(r, OwnOutcome::Ok); CHECK(Slurp(out) == Changed()); CHECK(g_shaRange == 1 && g_shaFull == 2); CHECK(NoParts(dir));
        sp.expectedSha256.clear();
    }
    {   // a silent server: Send's timeout holds; a cancel and the whole-call limit end a blocked call quickly
        DWORD t0 = GetTickCount();
        Result r = Send("GET", base + "/silent", {}, "", "", 1000);
        CHECK(!r.reached); CHECK_MS(t0, 4000);
        t0 = GetTickCount();
        r = Send("POST", base + "/silent", {}, "x", "", 1000);
        CHECK(!r.reached); CHECK(r.maybeSent); CHECK_MS(t0, 4000);   // a POST that may have arrived: never re-sent by curl
        for (const char* p : {"/silent", "/stall"}) {
            OwnDownloadSpec sp; sp.ioMs = 60000;
            const DWORD c0 = GetTickCount();
            sp.cancelled = [c0] { return GetTickCount() - c0 > 300; };
            t0 = GetTickCount();
            OwnDownloadResult d = FetchToFile(base + p, out, sp);
            CHECK_OUTCOME(d, OwnOutcome::Cancelled); CHECK(!fs::exists(out)); CHECK(NoParts(dir));
            CHECK(GetTickCount() - t0 < 3000);
            RequestOptions ro; ro.ioMs = 60000; ro.totalMs = 800;
            t0 = GetTickCount();
            const ExResult x = RequestEx("GET", base + p, {}, "", "", "", ro);
            CHECK(!x.reached); CHECK(x.error.rfind("timeout", 0) == 0); CHECK_MS(t0, 3000);
            if (x.error.rfind("timeout", 0) != 0) std::printf("  (error was \"%s\")\n", x.error.c_str());
        }
    }
    {   // a 407 is no answer from WinHTTP: the seam goes on to curl (off here, so it reports that), even for a POST
        OwnDownloadSpec sp; sp.retries = 2; sp.retryDelayMs = 10;
        OwnDownloadResult d = FetchToFile(base + "/proxyauth", out, sp);
        CHECK_OUTCOME(d, OwnOutcome::NotHandled); CHECK(d.attempts == 1); CHECK(!fs::exists(out));
        Result r = Send("POST", base + "/proxyauth", {}, "x");
        CHECK(!r.reached && r.error.find("curl") != std::string::npos);
    }
    {   // redirects: a call with a credential header gets the 3xx itself (curl without -L); others follow
        Result r = Send("GET", base + "/redir", {"Authorization: Bearer secret"});
        CHECK(r.reached && r.status == 302);
        CHECK(LastHead().rfind("GET /redir ", 0) == 0);
        r = Send("GET", base + "/redir", {"apikey: k"});
        CHECK(r.reached && r.status == 302);
        r = Send("GET", base + "/redir", {"Accept: */*"});
        CHECK(r.reached && r.status == 200 && r.body == pl);
    }
    {   // CR/LF inside a header line is taken out, never a second header
        Result r = Send("POST", base + "/echo", {"X-Test: a\r\nInjected: 1"}, "b");
        CHECK(r.reached && r.status == 201);
        const std::string h = LastHead();
        CHECK(h.find("X-Test: aInjected: 1\r\n") != std::string::npos && h.find("\r\nInjected:") == std::string::npos);
    }
    {   // the body file: a UTF-8 name with a non-ASCII letter is read; a missing one fails before anything is sent
        const fs::path bf = dir / fs::u8path("k\xC3\xB6rper.json");
        { std::ofstream f(bf, std::ios::binary); f << "{\"u\":1}"; }
        Result r = Send("POST", base + "/echo", {"Content-Type: application/json"}, "", bf.u8string());
        CHECK(r.reached && r.status == 201 && r.body == "{\"u\":1}");
        r = Request("POST", base + "/echo", {}, "", (dir / "missing.json").u8string());
        CHECK(!r.reached && !r.maybeSent && r.error == "cannot read the body file");
    }
    {   // HEAD and a prefix fetch
        std::string dump;
        CHECK(HeadDump(base + "/ok", "OkayTest/1", dump) && dump.find("Content-Length: 5000") != std::string::npos);
        CHECK(LastHead().rfind("HEAD /ok ", 0) == 0);
        std::string body; long st = 0;
        CHECK(FetchPrefix(base + "/page", "OkayTest/1", 3000000, 99, 8000, body, &st) == OwnOutcome::Ok && st == 206 && body == pl.substr(0, 100));
        CHECK(LastHead().find("Range: bytes=0-99") != std::string::npos);
        CHECK(FetchPrefix(base + "/missing", "", 1000, 99, 8000, body, &st) == OwnOutcome::HttpError && st == 404);
        CHECK(FetchPrefix(base + "/ok", "", 1000, 99, 8000, body) == OwnOutcome::TooBig);
    }
    {   // a POST with headers and a body (Account / PlayFab); any status is an answer
        Result r = Send("POST", base + "/echo", {"Content-Type: application/json", "X-Authorization: tick"}, "{\"a\":1}");
        CHECK(r.reached && r.status == 201 && r.body == "{\"a\":1}");
        const std::string h = LastHead();
        CHECK(h.rfind("POST /echo ", 0) == 0 && h.find("X-Authorization: tick") != std::string::npos);
        r = Send("GET", base + "/missing", {});
        CHECK(r.reached && r.status == 404 && r.body == "nope");
        r = Send("POST", "http://127.0.0.1:1/nothing-listens", {}, "x");   // nobody there: not reached (and curl is off)
        CHECK(!r.reached);
    }

    g_stop = 1;
    ::closesocket(g_listen);
    if (acc) { WaitForSingleObject(acc, 3000); CloseHandle(acc); }
    fs::remove_all(dir, ec);
    WSACleanup();
    std::printf("winhttp_loopback: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
