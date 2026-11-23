#pragma once
// ---------------------------------------------------------------------------
// NativeHttp — in-process HTTPS for the account/backend calls on Windows, via
// WinHTTP (part of Windows; no extra DLLs). One process-wide session keeps a
// pool of kept-alive connections, so after the first request to a host each
// call skips the TCP + TLS handshake — and nothing spawns cmd.exe + curl.exe
// per request (which cost a few hundred ms each and made the launcher slow to
// fill in on startup). Thread-safe: every worker can call Request concurrently.
//
// Elsewhere (and if WinHTTP can't reach the server, e.g. an odd proxy setup)
// Request returns reached=false and the HTTP seam (NativeFetch.hpp) falls back
// to curl, which only CurlFallback.hpp may start.
//
// Linux and other POSIX systems can OPT IN to our own HTTP/1.1 client
// (OwnHttp.hpp / OwnHttpNet.hpp, docs/platform/own_http.md): build with
// -DOKAY_OWN_HTTP=ON and set the environment variable OKAY_OWN_HTTP=1. TLS then
// comes from the system OpenSSL, loaded at run time. If libssl cannot be loaded,
// the certificate does not verify, or anything else goes wrong before a response
// arrives, reached stays false and the spawned curl path runs as before. It is off
// by default; the curl path remains the default and the fallback.
// ---------------------------------------------------------------------------
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

// WinHTTP is the native client on the Windows desktop; the UWP build has none yet (docs/platform/xbox_uwp.md).
// (Until claude/third-party-1 this test read `defined(OKAY_HTTP_WINHTTP)`, a macro nothing defined, so from #1001 on the
// Windows programs had no WinHTTP and spawned curl for every call. tests/test_winhttp_loopback.cpp now fails if it is off.)
#if defined(_WIN32) && !defined(OKAY_XBOX_UWP) && !defined(OKAY_HTTP_WINHTTP)
#  define OKAY_HTTP_WINHTTP 1
#endif

#if defined(OKAY_HTTP_WINHTTP)
#  include <windows.h>
#  include <winhttp.h>
#elif defined(OKAY_OWN_HTTP) && !defined(__APPLE__)
#  include "okay/Platform/OwnHttpNet.hpp"
#  define OKAY_OWN_HTTP_ACTIVE 1
#endif

namespace okay::http {

struct Result {
    bool        reached = false;   ///< got an HTTP response (any status)
    long        status = 0;
    std::string body;              ///< response body (empty when written to outFile)
    std::string error;             ///< own client only: why no response was obtained (for logs)
    /// Only meaningful when reached is false: the request may have reached the server (the own client wrote request bytes to an
    /// open connection; WinHTTP: ANY failure after WinHttpSendRequest succeeded, including a failed body read). Callers must NOT send a POST, PUT, PATCH or DELETE again by
    /// another route (the curl fallback) then; a GET or HEAD may be repeated. A clean "not reached" (DNS failure,
    /// connection refused, TLS refused, bad URL) leaves this false.
    bool        maybeSent = false;
};

#if defined(OKAY_HTTP_WINHTTP)
namespace detail {
inline std::wstring Widen(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}
// The shared session (created once, lives for the process; WinHTTP pools
// connections per session).
inline HINTERNET Session() {
    static HINTERNET s = [] {
        HINTERNET h = WinHttpOpen(L"OkaySpace/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                  WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!h)   // pre-8.1 Windows: no automatic proxy flag
            h = WinHttpOpen(L"OkaySpace/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (h) {
            WinHttpSetTimeouts(h, 8000, 8000, 20000, 30000);   // resolve, connect, send, receive (ms)
#ifdef WINHTTP_OPTION_ENABLE_HTTP_PROTOCOL
            DWORD proto = 0x1;   // WINHTTP_PROTOCOL_FLAG_HTTP2 (ignored where unsupported)
            WinHttpSetOption(h, WINHTTP_OPTION_ENABLE_HTTP_PROTOCOL, &proto, sizeof(proto));
#endif
        }
        return h;
    }();
    return s;
}
} // namespace detail
#endif

/// True where the native client exists: Windows (WinHTTP), or POSIX built with
/// OKAY_OWN_HTTP and run with OKAY_OWN_HTTP=1. The curl path is used otherwise.
inline bool NativeAvailable() {
#if defined(OKAY_HTTP_WINHTTP)
    return detail::Session() != nullptr && !std::getenv("OKAY_HTTP_CURL");
#elif defined(OKAY_OWN_HTTP_ACTIVE)
    const char* on = std::getenv("OKAY_OWN_HTTP");
#if defined(OKAY_OWN_HTTP_DEFAULT_ON)   // -DOKAY_OWN_STACK=ON (dogfood build): on unless OKAY_OWN_HTTP=0 (or anything but 1)
    const bool want = on ? (on[0] == '1' && on[1] == '\0') : true;
#else
    const bool want = on && on[0] == '1' && on[1] == '\0';
#endif
    return want && !std::getenv("OKAY_HTTP_CURL");
#else
    return false;
#endif
}

/// One request. `headers` are full "Name: value" lines. The body is `body`, or
/// the contents of `bodyFile` when set. With `outFile` the response streams to
/// that file (downloads) instead of `Result::body`. Follows redirects.
/// `timeoutMs` > 0 limits the whole call on the own client (connect, silence and total), so a stalled server cannot
/// hang the caller; 0 keeps the defaults. On Windows it is ignored: WinHTTP keeps the session timeouts set above
/// (a per-step limit that cannot bound the whole call, and Windows was deliberately left as it was).
inline Result Request(const std::string& method, const std::string& url,
                      const std::vector<std::string>& headers,
                      const std::string& body = std::string(),
                      const std::string& bodyFile = std::string(),
                      const std::string& outFile = std::string(),
                      int timeoutMs = 0) {
    Result r;
#if defined(OKAY_HTTP_WINHTTP)
    HINTERNET ses = detail::Session();
    if (!ses) return r;
    std::wstring wurl = detail::Widen(url);
    URL_COMPONENTS uc; ZeroMemory(&uc, sizeof(uc)); uc.dwStructSize = sizeof(uc);
    wchar_t host[256] = {0}, path[4096] = {0};
    uc.lpszHostName = host; uc.dwHostNameLength = 255;
    uc.lpszUrlPath = path;  uc.dwUrlPathLength = 4095;
    wchar_t extra[8192] = {0};
    uc.lpszExtraInfo = extra; uc.dwExtraInfoLength = 8191;
    if (!WinHttpCrackUrl(wurl.c_str(), (DWORD)wurl.size(), 0, &uc)) return r;
    const bool secure = uc.nScheme == INTERNET_SCHEME_HTTPS;
    std::wstring object = std::wstring(path, uc.dwUrlPathLength) + std::wstring(extra, uc.dwExtraInfoLength);
    if (object.empty()) object = L"/";

    HINTERNET con = WinHttpConnect(ses, host, uc.nPort, 0);
    if (!con) return r;
    std::wstring wmethod = detail::Widen(method);
    HINTERNET req = WinHttpOpenRequest(con, wmethod.c_str(), object.c_str(), nullptr, WINHTTP_NO_REFERER,
                                       WINHTTP_DEFAULT_ACCEPT_TYPES, secure ? WINHTTP_FLAG_SECURE : 0);
    if (!req) { WinHttpCloseHandle(con); return r; }
    (void)timeoutMs;   // Windows keeps the session timeouts (see above); a per-request limit is not applied here

    std::wstring hdrs;
    for (const std::string& h : headers) { hdrs += detail::Widen(h); hdrs += L"\r\n"; }
    std::string payload = body;
    if (!bodyFile.empty()) {
        std::ifstream f(bodyFile, std::ios::binary);
        std::stringstream ss; ss << f.rdbuf(); payload = ss.str();
    }
    BOOL ok = WinHttpSendRequest(req, hdrs.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : hdrs.c_str(),
                                 hdrs.empty() ? 0 : (DWORD)-1L,
                                 payload.empty() ? WINHTTP_NO_REQUEST_DATA : (LPVOID)payload.data(),
                                 (DWORD)payload.size(), (DWORD)payload.size(), 0);
    const bool sent = ok != FALSE;
    if (ok) ok = WinHttpReceiveResponse(req, nullptr);
    // Any failure after WinHttpSendRequest succeeded (timeout, reset, invalid reply, read error) may have reached the
    // server, so the caller must not re-send a POST/PUT/PATCH/DELETE by curl. Only failures before the send
    // (crack url, connect, open) leave maybeSent false.
    if (sent && !ok) { r.maybeSent = true; r.error = "winhttp error " + std::to_string((unsigned long)GetLastError()); }
    if (ok) {
        DWORD code = 0, sz = sizeof(code);
        if (WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX, &code, &sz, WINHTTP_NO_HEADER_INDEX)) {
            DWORD clen = 0, csz = sizeof(clen);
            const bool haveLen = method != "HEAD" &&
                WinHttpQueryHeaders(req, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
                                    WINHTTP_HEADER_NAME_BY_INDEX, &clen, &csz, WINHTTP_NO_HEADER_INDEX) != FALSE;
            std::ofstream out;
            if (!outFile.empty()) out.open(outFile, std::ios::binary | std::ios::trunc);
            std::vector<char> buf(64 * 1024);
            unsigned long long total = 0;
            bool cut = false;
            for (;;) {
                DWORD avail = 0;
                if (!WinHttpQueryDataAvailable(req, &avail)) { cut = true; break; }
                if (avail == 0) break;
                if (avail > buf.size()) buf.resize(avail);
                DWORD got = 0;
                if (!WinHttpReadData(req, buf.data(), avail, &got)) { cut = true; break; }
                if (got == 0) break;
                total += got;
                if (out.is_open()) out.write(buf.data(), got);
                else r.body.append(buf.data(), got);
            }
            // A read error, or fewer bytes than Content-Length announced, is a failed call (never a 200 with a cut body).
            if (!cut && haveLen && total < clen) cut = true;
            if (cut) {
                r.reached = false; r.status = 0; r.body.clear(); r.maybeSent = true;
                r.error = "response body cut short";
            } else {
                r.status = (long)code;
                r.reached = true;
            }
        }
    }
    WinHttpCloseHandle(req);
    WinHttpCloseHandle(con);
#elif defined(OKAY_OWN_HTTP_ACTIVE)
    if (!NativeAvailable()) return r;
    std::string payload = body;
    if (!bodyFile.empty()) {
        std::ifstream f(bodyFile, std::ios::binary);
        if (!f) { r.error = "cannot read the body file"; return r; }
        std::stringstream ss; ss << f.rdbuf(); payload = ss.str();
    }
    okay::ownhttp::NetOptions opt;
    opt.keepAlive = true;   // sequential calls to one host on one thread reuse the connection (safe retry if it went stale)
    opt.limits.maxBodyBytes = outFile.empty() ? (64ull << 20) : (8ull << 30);   // memory vs. a download to disk
    if (timeoutMs > 0) {
        opt.totalMs = timeoutMs;
        opt.ioMs = timeoutMs;
        opt.connectMs = timeoutMs < 5000 ? timeoutMs : 5000;
        opt.resolveMs = timeoutMs < 5000 ? timeoutMs : 5000;
    }
    struct Out { std::ofstream f; bool open = false; };
    auto out = std::make_shared<Out>();
    okay::ownhttp::FetchHooks hooks;
    if (!outFile.empty() && method != "HEAD") {
        hooks.onHead = [out, outFile](const okay::ownhttp::Response&) {
            out->f.open(outFile, std::ios::binary | std::ios::trunc);
            out->open = out->f.is_open();
            return out->open;
        };
        hooks.onData = [out](const char* p, size_t n) { out->f.write(p, std::streamsize(n)); return bool(out->f); };
    }
    okay::ownhttp::FetchResult fr = okay::ownhttp::Perform(method, url, headers, payload, opt, hooks);
    if (out->open) out->f.close();
    if (fr.ok) {
        r.reached = true;
        r.status = fr.response.status;
        r.body = std::move(fr.response.body);
    } else {
        r.error = std::string(okay::ownhttp::ErrName(fr.err)) + ": " + fr.message;
        r.maybeSent = fr.requestSent;
    }
#else
    (void)method; (void)url; (void)headers; (void)body; (void)bodyFile; (void)outFile; (void)timeoutMs;
#endif
    return r;
}

/// The no-re-send decision, kept here so it can be tested. After a failed native call, may the caller try the same
/// request again through curl? A success never needs it. A clean not-reached (maybeSent false) always may. If the
/// request may have reached the server, only GET and HEAD may be repeated; POST, PUT, PATCH, DELETE (and any other
/// method) must not, or the action could happen twice.
inline bool ShouldFallBackToCurl(const std::string& method, const Result& nativeResult) {
    if (nativeResult.reached) return false;
    if (!nativeResult.maybeSent) return true;
    return method == "GET" || method == "HEAD";
}

/// Extra controls for downloads (the own client on Linux, WinHTTP on Windows; elsewhere RequestEx returns
/// reached=false with an error and callers use the curl fallback). All fields optional.
struct RequestOptions {
    /// Called as the body arrives: bytes so far (counting from the resume offset) and the complete size when
    /// known (0 when not). Return false to cancel.
    std::function<bool(uint64_t done, uint64_t total)> progress;
    /// Polled about every 200 ms, also while waiting on a silent server. Return true to cancel.
    std::function<bool()> cancel;
    /// With outFile: continue a partial download. Sends Range: bytes=N-; a 206 is appended to the file, a 200
    /// (the server ignored the range) starts the file again, a 416 leaves the file alone (status tells).
    uint64_t resumeFrom = 0;
    uint64_t maxBytes = 0;       ///< 0 = the default cap (64 MiB in memory, 8 GiB to a file)
    int connectMs = 0, ioMs = 0, totalMs = 0;   ///< 0 = the defaults (8 s, 30 s, 15 min)
    int maxRedirects = 0;        ///< 0 = the default (10)
    bool acceptGzip = false;     ///< in-memory bodies only; ignored with outFile
    /// With outFile: write the file only for a 200 or 206 answer (like curl -f); other statuses leave it alone.
    bool fileOnSuccessOnly = true;
    /// Conditional request: sends If-None-Match / If-Modified-Since. A 304 answer comes back as status 304,
    /// `notModified` true, no body, and an outFile is left alone.
    std::string ifNoneMatch, ifModifiedSince;
    /// Remember Set-Cookie answers for the redirects of this call and send them back (host-only, simple; see CookieJar).
    /// Off by default. The cookies that were set are returned in ExResult::cookies.
    bool cookies = false;
    /// Reuse a kept-alive connection to the same host and keep this one for the next call (own client, same thread).
    bool keepAlive = true;
};

/// Result of RequestEx: Result plus what a resumable download needs.
struct ExResult : Result {
    bool     resumed = false;       ///< the server sent the part we asked for; the file was appended to
    uint64_t totalSize = 0;         ///< complete size when the server said so (Content-Range or Content-Length)
    bool     writeFailed = false;   ///< outFile could not be opened or written (disk full, no permission)
    bool     notModified = false;   ///< the server answered 304 to a conditional request
    std::string etag, lastModified; ///< validators of the final answer, for the next conditional request
    std::vector<std::string> cookies;   ///< "name=value" the jar holds after the call (only with RequestOptions::cookies)
    int      reusedConnections = 0; ///< requests of this call that used a kept-alive connection
    std::string rawHead;            ///< the final answer's status line and headers as text (like `curl -D`), for callers that parse them
};

#if defined(OKAY_HTTP_WINHTTP)
namespace detail {
inline std::string Narrow(const wchar_t* w, size_t n) {
    if (!w || n == 0) return std::string();
    const int k = WideCharToMultiByte(CP_UTF8, 0, w, (int)n, nullptr, 0, nullptr, nullptr);
    std::string s((size_t)(k > 0 ? k : 0), '\0');
    if (k > 0) WideCharToMultiByte(CP_UTF8, 0, w, (int)n, &s[0], k, nullptr, nullptr);
    return s;
}
/// One header of the final answer as text ("" when it is not there).
inline std::string QueryText(HINTERNET req, DWORD what) {
    DWORD len = 0;
    WinHttpQueryHeaders(req, what, WINHTTP_HEADER_NAME_BY_INDEX, WINHTTP_NO_OUTPUT_BUFFER, &len, WINHTTP_NO_HEADER_INDEX);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || len == 0) return std::string();
    std::wstring w(len / sizeof(wchar_t) + 1, L'\0');
    if (!WinHttpQueryHeaders(req, what, WINHTTP_HEADER_NAME_BY_INDEX, &w[0], &len, WINHTTP_NO_HEADER_INDEX)) return std::string();
    return Narrow(w.data(), len / sizeof(wchar_t));
}
/// Decimal digits only (no sign, no spaces inside), at most 18 of them.
inline bool ParseU64(const std::string& t, uint64_t& v) {
    size_t a = 0, b = t.size();
    while (a < b && (t[a] == ' ' || t[a] == '\t')) ++a;
    while (b > a && (t[b - 1] == ' ' || t[b - 1] == '\t')) --b;
    if (a == b || b - a > 18) return false;
    v = 0;
    for (size_t i = a; i < b; ++i) { if (t[i] < '0' || t[i] > '9') return false; v = v * 10 + (uint64_t)(t[i] - '0'); }
    return true;
}
/// RequestEx on WinHTTP: the same contract as the own client's (RFC 9110 sections 14 and 15.3.7 for ranges), written
/// from the WinHTTP documentation. Progress and cancel are checked between reads; a silent server is bounded by the
/// receive timeout (ioMs), because the synchronous WinHTTP calls cannot be interrupted from the same thread. Cookies
/// are WinHTTP's own per-session handling; acceptGzip is ignored (no Accept-Encoding is sent, so bodies come as is).
inline ExResult WinRequestEx(const std::string& method, const std::string& url, const std::vector<std::string>& headers,
                             const std::string& body, const std::string& bodyFile, const std::filesystem::path& outFile,
                             const RequestOptions& ro) {
    ExResult r;
    if (!NativeAvailable()) { r.error = "winhttp unavailable"; return r; }
    HINTERNET ses = Session();
    std::wstring wurl = Widen(url);
    URL_COMPONENTS uc; ZeroMemory(&uc, sizeof(uc)); uc.dwStructSize = sizeof(uc);
    std::wstring host(256, L'\0'), path(4096, L'\0'), extra(8192, L'\0');
    uc.lpszHostName = &host[0]; uc.dwHostNameLength = 255;
    uc.lpszUrlPath = &path[0];  uc.dwUrlPathLength = 4095;
    uc.lpszExtraInfo = &extra[0]; uc.dwExtraInfoLength = 8191;
    if (!WinHttpCrackUrl(wurl.c_str(), (DWORD)wurl.size(), 0, &uc) ||
        (uc.nScheme != INTERNET_SCHEME_HTTP && uc.nScheme != INTERNET_SCHEME_HTTPS)) { r.error = "bad-url"; return r; }
    const bool secure = uc.nScheme == INTERNET_SCHEME_HTTPS;
    std::wstring object = std::wstring(uc.lpszUrlPath, uc.dwUrlPathLength) + std::wstring(uc.lpszExtraInfo, uc.dwExtraInfoLength);
    if (object.empty()) object = L"/";
    host.resize(uc.dwHostNameLength);
    std::string payload = body;
    if (!bodyFile.empty()) {
        std::ifstream f(bodyFile, std::ios::binary);
        if (!f) { r.error = "cannot read the body file"; return r; }
        std::stringstream ss; ss << f.rdbuf(); payload = ss.str();
    }
    HINTERNET con = WinHttpConnect(ses, host.c_str(), uc.nPort, 0);
    if (!con) { r.error = "connect: winhttp error " + std::to_string((unsigned long)GetLastError()); return r; }
    const std::wstring wmethod = Widen(method);
    HINTERNET req = WinHttpOpenRequest(con, wmethod.c_str(), object.c_str(), nullptr, WINHTTP_NO_REFERER,
                                       WINHTTP_DEFAULT_ACCEPT_TYPES, secure ? WINHTTP_FLAG_SECURE : 0);
    if (!req) { WinHttpCloseHandle(con); r.error = "open: winhttp error " + std::to_string((unsigned long)GetLastError()); return r; }
    const int connectMs = ro.connectMs > 0 ? ro.connectMs : 8000, ioMs = ro.ioMs > 0 ? ro.ioMs : 30000;
    WinHttpSetTimeouts(req, connectMs, connectMs, ioMs, ioMs);
    DWORD redirs = (DWORD)(ro.maxRedirects > 0 ? ro.maxRedirects : 10);
    WinHttpSetOption(req, WINHTTP_OPTION_MAX_HTTP_AUTOMATIC_REDIRECTS, &redirs, sizeof(redirs));
    // (WinHTTP's default redirect policy already refuses an https -> http redirect, like curl --proto-redir =https.)
    std::wstring hdrs;
    for (const std::string& h : headers) { hdrs += Widen(h); hdrs += L"\r\n"; }
    const bool wantFile = !outFile.empty() && method != "HEAD";
    if (ro.resumeFrom > 0 && wantFile) hdrs += L"Range: bytes=" + std::to_wstring(ro.resumeFrom) + L"-\r\n";
    if (!ro.ifNoneMatch.empty()) hdrs += L"If-None-Match: " + Widen(ro.ifNoneMatch) + L"\r\n";
    if (!ro.ifModifiedSince.empty()) hdrs += L"If-Modified-Since: " + Widen(ro.ifModifiedSince) + L"\r\n";
    const DWORD t0 = GetTickCount();   // (not GetTickCount64: older _WIN32_WINNT headers lack it; these intervals are far below the 49-day wrap)
    const DWORD totalMs = (DWORD)(ro.totalMs > 0 ? ro.totalMs : 15 * 60 * 1000);
    auto done = [&](const std::string& why, bool maybeSent) {
        r.reached = false; r.status = 0; r.body.clear(); r.error = why; r.maybeSent = maybeSent;
        WinHttpCloseHandle(req); WinHttpCloseHandle(con);
        return r;
    };
    if (ro.cancel && ro.cancel()) return done("cancelled", false);
    BOOL ok = WinHttpSendRequest(req, hdrs.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : hdrs.c_str(), hdrs.empty() ? 0 : (DWORD)-1L,
                                 payload.empty() ? WINHTTP_NO_REQUEST_DATA : (LPVOID)payload.data(),
                                 (DWORD)payload.size(), (DWORD)payload.size(), 0);
    if (!ok) {
        const DWORD e = GetLastError();
        return done((e == ERROR_WINHTTP_TIMEOUT ? "timeout: winhttp error " : "connect: winhttp error ") + std::to_string((unsigned long)e), false);
    }
    if (!WinHttpReceiveResponse(req, nullptr)) {
        const DWORD e = GetLastError();
        return done((e == ERROR_WINHTTP_REDIRECT_FAILED ? "too-many-redirects: winhttp error " :
                     e == ERROR_WINHTTP_TIMEOUT ? "timeout: winhttp error " : "winhttp error ") + std::to_string((unsigned long)e), true);
    }
    DWORD code = 0, csz = sizeof(code);
    if (!WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                             &code, &csz, WINHTTP_NO_HEADER_INDEX))
        return done("bad-response: no status", true);
    r.status = (long)code;
    r.rawHead = QueryText(req, WINHTTP_QUERY_RAW_HEADERS_CRLF);
    r.etag = QueryText(req, WINHTTP_QUERY_ETAG);
    r.lastModified = QueryText(req, WINHTTP_QUERY_LAST_MODIFIED);
    r.notModified = code == 304;
    uint64_t clen = 0;
    const bool haveLen = method != "HEAD" && code != 304 && code != 204 && ParseU64(QueryText(req, WINHTTP_QUERY_CONTENT_LENGTH), clen);
    uint64_t base = 0;
    if (code == 206 && wantFile && ro.resumeFrom > 0) {
        // Content-Range: bytes FIRST-LAST/TOTAL must start where the file stops, or the bytes would land in the wrong place.
        const std::string cr = QueryText(req, WINHTTP_QUERY_CONTENT_RANGE);
        const size_t sp = cr.find(' '), dash = cr.find('-'), slash = cr.find('/');
        uint64_t first = 0, total = 0;
        if (sp == std::string::npos || dash == std::string::npos || dash < sp ||
            !ParseU64(cr.substr(sp + 1, dash - sp - 1), first) || first != ro.resumeFrom)
            return done("bad-response: Content-Range does not match the resume point", true);
        if (slash != std::string::npos && ParseU64(cr.substr(slash + 1), total)) r.totalSize = total;
        base = ro.resumeFrom;
        r.resumed = true;
    } else if (haveLen) {
        r.totalSize = clen;
    }
    const uint64_t cap = ro.maxBytes ? ro.maxBytes : (wantFile ? (8ull << 30) : (64ull << 20));
    if (haveLen && clen > cap) return done("too-large: the answer announced " + std::to_string((unsigned long long)clen) + " bytes", true);
    std::ofstream out;
    bool toFile = false;
    if (wantFile && code != 304 && (code == 200 || code == 206 || !ro.fileOnSuccessOnly)) {
        out.open(outFile, std::ios::binary | (r.resumed ? std::ios::app : std::ios::trunc));
        if (!out.is_open()) { r.writeFailed = true; return done("write-failed: cannot open the output file", true); }
        toFile = true;
    }
    if (method != "HEAD") {
        std::vector<char> buf(64 * 1024);
        uint64_t got = 0;
        for (;;) {
            if (ro.cancel && ro.cancel()) return done("cancelled", true);
            if ((DWORD)(GetTickCount() - t0) > totalMs) return done("timeout: the whole call took too long", true);
            DWORD avail = 0;
            if (!WinHttpQueryDataAvailable(req, &avail)) return done("io: winhttp error " + std::to_string((unsigned long)GetLastError()), true);
            if (avail == 0) break;
            if (avail > buf.size()) buf.resize(avail);
            DWORD n = 0;
            if (!WinHttpReadData(req, buf.data(), avail, &n)) return done("io: winhttp error " + std::to_string((unsigned long)GetLastError()), true);
            if (n == 0) break;
            got += n;
            if (got > cap) return done("too-large: more than " + std::to_string((unsigned long long)cap) + " bytes", true);
            if (toFile) {
                out.write(buf.data(), n);
                if (!out) { r.writeFailed = true; return done("write-failed: the output file could not be written", true); }
            } else if (!wantFile) {
                r.body.append(buf.data(), n);
            }
            if (ro.progress && !ro.progress(base + got, r.totalSize)) return done("cancelled", true);
        }
        // Fewer bytes than Content-Length announced is a failed call (never a 200 with a cut body).
        if (haveLen && got < clen) return done("io: response body cut short", true);
        if (toFile) { out.flush(); if (!out) { r.writeFailed = true; return done("write-failed: flush", true); } out.close(); }
    }
    r.reached = true;
    WinHttpCloseHandle(req);
    WinHttpCloseHandle(con);
    return r;
}
} // namespace detail
#endif

inline ExResult RequestEx(const std::string& method, const std::string& url,
                          const std::vector<std::string>& headers, const std::string& body,
                          const std::string& bodyFile, const std::string& outFile, const RequestOptions& ro) {
    ExResult r;
#if defined(OKAY_OWN_HTTP_ACTIVE)
    if (!NativeAvailable()) return r;
    std::string payload = body;
    if (!bodyFile.empty()) {
        std::ifstream f(bodyFile, std::ios::binary);
        if (!f) { r.error = "cannot read the body file"; return r; }
        std::stringstream ss; ss << f.rdbuf(); payload = ss.str();
    }
    okay::ownhttp::NetOptions opt;
    opt.limits.maxBodyBytes = ro.maxBytes ? ro.maxBytes : (outFile.empty() ? (64ull << 20) : (8ull << 30));
    if (ro.connectMs > 0) opt.connectMs = ro.connectMs;
    if (ro.ioMs > 0) opt.ioMs = ro.ioMs;
    if (ro.totalMs > 0) opt.totalMs = ro.totalMs;
    opt.cancel = ro.cancel;
    if (ro.maxRedirects > 0) opt.limits.maxRedirects = ro.maxRedirects;
    opt.acceptGzip = ro.acceptGzip && outFile.empty();
    opt.keepAlive = ro.keepAlive;
    std::shared_ptr<okay::ownhttp::CookieJar> jar;
    if (ro.cookies) { jar = std::make_shared<okay::ownhttp::CookieJar>(); opt.cookieJar = jar; }
    std::vector<std::string> hdrs = headers;
    if (ro.resumeFrom > 0 && !outFile.empty()) hdrs.push_back("Range: bytes=" + std::to_string(ro.resumeFrom) + "-");
    if (!ro.ifNoneMatch.empty()) hdrs.push_back("If-None-Match: " + ro.ifNoneMatch);
    if (!ro.ifModifiedSince.empty()) hdrs.push_back("If-Modified-Since: " + ro.ifModifiedSince);
    struct Out { std::ofstream f; bool open = false; uint64_t base = 0; bool wfail = false; };
    auto out = std::make_shared<Out>();
    okay::ownhttp::FetchHooks hooks;
    if (!outFile.empty() && method != "HEAD") {
        const uint64_t resumeFrom = ro.resumeFrom;
        const bool successOnly = ro.fileOnSuccessOnly;
        hooks.onHead = [out, outFile, resumeFrom, successOnly](const okay::ownhttp::Response& h) {
            if (h.status == 304) return true;   // not modified: never touch the file
            const bool ok = h.status == 200 || h.status == 206;
            if (!ok && successOnly) return true;   // the body (an error page) is read and dropped
            const bool append = h.status == 206 && resumeFrom > 0;
            out->f.open(outFile, std::ios::binary | (append ? std::ios::app : std::ios::trunc));
            out->open = out->f.is_open();
            out->base = append ? resumeFrom : 0;
            if (!out->open) out->wfail = true;
            return out->open;
        };
        hooks.onData = [out](const char* p, size_t n) {
            if (!out->open) return true;
            out->f.write(p, std::streamsize(n));
            if (!out->f) out->wfail = true;
            return bool(out->f);
        };
    }
    if (ro.progress) {
        auto prog = ro.progress;
        hooks.onProgress = [out, prog, resumeFrom = ro.resumeFrom](uint64_t done, uint64_t total) {
            const uint64_t base = out->open ? out->base : 0;
            (void)resumeFrom;
            return prog(base + done, total ? base + total : 0);
        };
    }
    okay::ownhttp::FetchResult fr = okay::ownhttp::Perform(method, url, hdrs, payload, opt, hooks);
    if (out->open) { out->f.flush(); if (!out->f) out->wfail = true; out->f.close(); }
    r.writeFailed = out->wfail;
    if (fr.ok) {
        r.reached = true;
        r.status = fr.response.status;
        r.body = std::move(fr.response.body);
        r.resumed = fr.partial && out->open;
        r.notModified = fr.response.status == 304;
        r.rawHead = "HTTP/1." + std::string(fr.response.minor ? "1" : "0") + " " + std::to_string(fr.response.status) + " " + fr.response.reason + "\r\n";
        for (const okay::ownhttp::Header& h : fr.response.headers) r.rawHead += h.name + ": " + h.value + "\r\n";
        r.rawHead += "\r\n";
        r.reusedConnections = fr.reusedConnections;
        if (const std::string* v = fr.response.Get("etag")) r.etag = *v;
        if (const std::string* v = fr.response.Get("last-modified")) r.lastModified = *v;
        if (jar) for (const auto& c : jar->Items()) r.cookies.push_back(c.name + "=" + c.value);
        if (fr.partial) r.totalSize = fr.rangeTotal;
        else if (const std::string* cl = fr.response.Get("content-length")) {
            uint64_t n = 0; bool digits = !cl->empty() && cl->size() < 19;
            for (unsigned char c : *cl) { if (c < '0' || c > '9') digits = false; else n = n * 10 + (c - '0'); }
            if (digits) r.totalSize = n;
        }
    } else {
        r.error = std::string(okay::ownhttp::ErrName(fr.err)) + ": " + fr.message;
        r.maybeSent = fr.requestSent;
    }
#elif defined(OKAY_HTTP_WINHTTP)
    r = detail::WinRequestEx(method, url, headers, body, bodyFile, std::filesystem::path(outFile), ro);
#else
    (void)method; (void)url; (void)headers; (void)body; (void)bodyFile; (void)outFile; (void)ro;
    r.error = "RequestEx needs a native client (WinHTTP on Windows, OKAY_OWN_HTTP elsewhere)";
#endif
    return r;
}

} // namespace okay::http
