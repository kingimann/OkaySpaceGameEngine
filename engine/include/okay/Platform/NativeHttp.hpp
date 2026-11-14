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
inline Result Request(const std::string& method, const std::string& url,
                      const std::vector<std::string>& headers,
                      const std::string& body = std::string(),
                      const std::string& bodyFile = std::string(),
                      const std::string& outFile = std::string()) {
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
    opt.limits.maxBodyBytes = outFile.empty() ? (64ull << 20) : (8ull << 30);   // memory vs. a download to disk
    struct Out { std::ofstream f; bool open = false; };
    auto out = std::make_shared<Out>();
    okay::ownhttp::FetchHooks hooks;
    if (!outFile.empty()) {
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
    (void)method; (void)url; (void)headers; (void)body; (void)bodyFile; (void)outFile;
#endif
    return r;
}

} // namespace okay::http
