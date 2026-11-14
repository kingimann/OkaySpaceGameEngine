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
// Request returns reached=false and callers fall back to the curl path.
//
// Linux and other POSIX systems can OPT IN to our own HTTP/1.1 client
// (OwnHttp.hpp / OwnHttpNet.hpp, docs/own_http_client.md): build with
// -DOKAY_OWN_HTTP=ON and set the environment variable OKAY_OWN_HTTP=1. TLS then
// comes from the system OpenSSL, loaded at run time. If libssl cannot be loaded,
// the certificate does not verify, or anything else goes wrong before a response
// arrives, reached stays false and the spawned curl path runs as before. It is off
// by default; the curl path remains the default and the fallback.
// ---------------------------------------------------------------------------
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#if defined(_WIN32)
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
};

#if defined(_WIN32)
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
#if defined(_WIN32)
    return detail::Session() != nullptr && !std::getenv("OKAY_HTTP_CURL");
#elif defined(OKAY_OWN_HTTP_ACTIVE)
    const char* on = std::getenv("OKAY_OWN_HTTP");
    return on && on[0] == '1' && on[1] == '\0' && !std::getenv("OKAY_HTTP_CURL");
#else
    return false;
#endif
}

/// One request. `headers` are full "Name: value" lines. The body is `body`, or
/// the contents of `bodyFile` when set. With `outFile` the response streams to
/// that file (downloads) instead of `Result::body`. Follows redirects.
/// `timeoutMs` > 0 limits the whole call (the own client: connect, silence and total; WinHTTP: resolve, connect,
/// send and receive each), so a stalled server cannot hang the caller; 0 keeps the defaults.
inline Result Request(const std::string& method, const std::string& url,
                      const std::vector<std::string>& headers,
                      const std::string& body = std::string(),
                      const std::string& bodyFile = std::string(),
                      const std::string& outFile = std::string(),
                      int timeoutMs = 0) {
    Result r;
#if defined(_WIN32)
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
    if (timeoutMs > 0) WinHttpSetTimeouts(req, timeoutMs, timeoutMs < 5000 ? timeoutMs : 5000, timeoutMs, timeoutMs);

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
    if (ok) ok = WinHttpReceiveResponse(req, nullptr);
    if (ok) {
        DWORD code = 0, sz = sizeof(code);
        if (WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX, &code, &sz, WINHTTP_NO_HEADER_INDEX)) {
            r.status = (long)code;
            r.reached = true;
        }
        std::ofstream out;
        if (!outFile.empty()) out.open(outFile, std::ios::binary | std::ios::trunc);
        std::vector<char> buf(64 * 1024);
        for (;;) {
            DWORD avail = 0;
            if (!WinHttpQueryDataAvailable(req, &avail) || avail == 0) break;
            if (avail > buf.size()) buf.resize(avail);
            DWORD got = 0;
            if (!WinHttpReadData(req, buf.data(), avail, &got) || got == 0) break;
            if (out.is_open()) out.write(buf.data(), got);
            else r.body.append(buf.data(), got);
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
    }
#else
    (void)method; (void)url; (void)headers; (void)body; (void)bodyFile; (void)outFile; (void)timeoutMs;
#endif
    return r;
}

/// Extra controls for downloads (own client only; on Windows and without OKAY_OWN_HTTP this returns reached=false
/// with an error and callers use their existing path). All fields optional.
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
    }
#else
    (void)method; (void)url; (void)headers; (void)body; (void)bodyFile; (void)outFile; (void)ro;
    r.error = "RequestEx needs the own HTTP client (OKAY_OWN_HTTP)";
#endif
    return r;
}

} // namespace okay::http
