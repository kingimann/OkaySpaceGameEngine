#pragma once
// ---------------------------------------------------------------------------
// NativeFetch - THE HTTP seam of the launcher, editor, player and engine (downloads, size probes, link previews,
// account and PlayFab calls). Four calls, each with the behaviour of the curl command it once replaced (-L, -f,
// --max-filesize, -m, -r, --retry, a progress callback, a cancel that stops the transfer):
//
//   FetchToFile  GET into a file, with progress, cancel, a size cap and retries (temporary file, renamed when complete)
//   HeadDump     HEAD that follows redirects; the final answer's head as text (for Content-Length)
//   FetchPrefix  the first bytes of a page (Range), with a size cap and a time limit
//   Send         one request with headers and a body (any status is an answer; never re-sent when it may have arrived)
//
// Each uses only our own code on the platform's network API: WinHTTP on Windows (NativeHttp RequestEx), our own HTTP/1.1
// client (okay::ownhttp) on Linux. No program starts the external curl program (tests/check_no_curl.cmake). When the
// native client gives no answer, the call fails with the reason (NotHandled / reached=false). Apple builds are parked
// (CLAUDE.md rule 14) and have no client: every call fails with NoClientReason(). The checksum and signature checks stay
// with the callers (UpdaterCore, VersionsManager); this seam only fetches bytes.
//
// OwnDownload is the own client's half of FetchToFile (POSIX with -DOKAY_OWN_HTTP=ON); WinDownload is WinHTTP's.
// docs/platform/own_http.md lists which call sites use what.
// ---------------------------------------------------------------------------
#include "okay/Platform/NativeHttp.hpp"
#include "okay/Crypto/Sha256.hpp"

#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

#if defined(OKAY_OWN_HTTP_ACTIVE)
#include "okay/Platform/OwnHttpDownload.hpp"   // temporary file, SHA-256 while streaming, resume, retries
#endif

namespace okay::http {

enum class OwnOutcome {
    NotHandled,   ///< no usable answer (no client, libssl missing, certificate refused, proxy trouble, bad reply); `error` says why
    Ok,           ///< 200 and the file is complete (for FetchPrefix: the body is in `body`)
    HttpError,    ///< the server answered with a status that curl -f would call a failure; `status` says which
    Cancelled,    ///< the caller's cancel check said stop
    TooBig,       ///< more data than maxBytes (announced or sent)
    DiskFull,     ///< the output file could not be opened or written
    ChecksumMismatch   ///< the file arrived but its SHA-256 is not OwnDownloadSpec::expectedSha256 (nothing is kept)
};

struct OwnDownloadSpec {
    std::string userAgent;                   ///< "" = the client's default
    std::vector<std::string> headers;        ///< extra "Name: value" lines (for example the no-cache pair)
    uint64_t maxBytes = 0;                   ///< like --max-filesize; 0 = the client's default cap
    int maxRedirects = 0;                    ///< 0 = the client's default (10)
    int connectMs = 20000, ioMs = 60000, totalMs = 6 * 60 * 60 * 1000;
    int retries = 0;                         ///< like --retry N: transient failures and 408/429/500/502/503/504 are tried again
                                             ///< (a try after a dropped connection resumes with Range where it stopped)
    int retryDelayMs = 1000;                 ///< first wait; doubles each time (as curl does), with jitter; Retry-After is honoured
    std::string expectedSha256;              ///< 64 hex digits: the file must have this SHA-256 or it is not kept; "" = not checked
    std::function<void(uint64_t)> onBytes;   ///< bytes written so far, at most every 100 ms and once at the end
    std::function<bool()> cancelled;         ///< true = stop now (polled about every 50 ms while data flows, 200 ms while it is silent)
};

struct OwnDownloadResult {
    OwnOutcome outcome = OwnOutcome::NotHandled;
    long status = 0;
    uint64_t bytes = 0;
    int attempts = 0;
    int resumes = 0;                         ///< tries that continued a dropped transfer with a 206
    std::string sha256;                      ///< lower-case hex SHA-256 of the file (Ok)
    std::string error;                       ///< the client's reason when NotHandled (for logs)
};

#if defined(OKAY_OWN_HTTP_ACTIVE)

namespace detail {
inline bool SleepUnlessCancelled(int ms, const std::function<bool()>& cancelled) {
    return okay::ownhttp::WaitMs(ms, cancelled, std::function<void(int)>());
}
} // namespace detail

/// GET `url` into the file `path` (replacing it), like `curl -L -f [--max-filesize N] [--retry N] -o path url` with a
/// progress callback and a cancel check. The bytes go to a temporary file of their own next to `path` first, SHA-256 is computed while they arrive,
/// and only a complete (and, with expectedSha256, matching) file is renamed to `path`. A dropped connection is
/// resumed with Range on the next try (sp.retries). On anything but Ok, `path` and the temporary file are removed.
inline OwnDownloadResult OwnDownload(const std::string& url, const std::string& path, const OwnDownloadSpec& sp) {
    namespace fs = std::filesystem;
    namespace oh = okay::ownhttp;
    OwnDownloadResult out;
    if (!NativeAvailable()) return out;
    std::error_code ec;
    oh::DownloadSpec ds;
    if (!sp.userAgent.empty()) ds.headers.push_back("User-Agent: " + sp.userAgent);
    for (const std::string& h : sp.headers) ds.headers.push_back(h);
    ds.expectedSha256 = sp.expectedSha256;
    ds.maxBytes = sp.maxBytes ? sp.maxBytes : (8ull << 30);
    ds.retry.maxAttempts = sp.retries + 1;
    ds.retry.baseDelayMs = sp.retryDelayMs;
    ds.retry.maxDelayMs = sp.retryDelayMs > 30000 ? sp.retryDelayMs : 30000;
    auto lastTick = std::chrono::steady_clock::now() - std::chrono::milliseconds(1000);
    auto lastPoll = lastTick;
    bool wasCancelled = false;
    ds.cancel = [&]() { if (sp.cancelled && sp.cancelled()) { wasCancelled = true; return true; } return false; };
    ds.progress = [&](uint64_t done, uint64_t) {
        const auto now = std::chrono::steady_clock::now();
        if (now - lastPoll >= std::chrono::milliseconds(50)) {
            lastPoll = now;
            if (sp.cancelled && sp.cancelled()) { wasCancelled = true; return false; }
        }
        if (sp.onBytes && now - lastTick >= std::chrono::milliseconds(100)) { lastTick = now; sp.onBytes(done); }
        return true;
    };
    oh::NetOptions net;
    net.keepAlive = true;
    net.connectMs = sp.connectMs; net.ioMs = sp.ioMs; net.totalMs = sp.totalMs;
    if (sp.maxRedirects > 0) net.limits.maxRedirects = sp.maxRedirects;
    const oh::DownloadResult r = oh::DownloadToFile(url, path, ds, net);
    out.attempts = r.retry.attempts;
    out.resumes = r.resumes;
    out.status = r.httpStatus;
    switch (r.status) {
    case oh::DownloadStatus::Ok:
        out.outcome = OwnOutcome::Ok; out.bytes = r.bytes; out.sha256 = r.sha256Hex;
        if (sp.onBytes) sp.onBytes(out.bytes);
        return out;
    case oh::DownloadStatus::HttpError: out.outcome = OwnOutcome::HttpError; break;
    case oh::DownloadStatus::Cancelled: out.outcome = OwnOutcome::Cancelled; break;
    case oh::DownloadStatus::TooLarge: out.outcome = OwnOutcome::TooBig; break;
    case oh::DownloadStatus::WriteFailed: out.outcome = OwnOutcome::DiskFull; break;
    case oh::DownloadStatus::ChecksumMismatch: out.outcome = OwnOutcome::ChecksumMismatch; break;
    default:   // NetworkError, SizeMismatch (an answer cut short), BadInput: no clear answer
        out.outcome = wasCancelled ? OwnOutcome::Cancelled : OwnOutcome::NotHandled;
        out.error = std::string(oh::ErrName(r.err)) + ": " + r.message;
        break;
    }
    fs::remove(path, ec);
    return out;
}

#endif // OKAY_OWN_HTTP_ACTIVE


// ---------------------------------------------------------------------------
// The seam: the native client only (every platform).
// ---------------------------------------------------------------------------
namespace fetch_detail {
inline std::string Utf8(const std::filesystem::path& p) { return p.u8string(); }   // C++17: UTF-8 in a std::string

inline std::string RandomHex8() {
    static std::atomic<unsigned> counter{0};
    unsigned long long x = (unsigned long long)std::chrono::steady_clock::now().time_since_epoch().count();
    x ^= (unsigned long long)counter.fetch_add(1) * 0x9E3779B97F4A7C15ull;
    try { std::random_device rd; x ^= ((unsigned long long)rd() << 32) ^ rd(); } catch (...) {}
    char b[17];
    std::snprintf(b, sizeof b, "%016llx", x);
    return std::string(b, 8) + std::string(b + 8, 8);
}

inline uint64_t SizeOf(const std::filesystem::path& p) {
    std::error_code ec;
    const auto n = std::filesystem::file_size(p, ec);
    return ec ? 0 : (uint64_t)n;
}

/// Lower-case hex SHA-256 of a file ("" when it cannot be read).
inline std::string Sha256File(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return std::string();
    okay::crypto::Sha256 h;
    std::vector<char> buf(1 << 16);
    while (f) {
        f.read(buf.data(), (std::streamsize)buf.size());
        const std::streamsize n = f.gcount();
        if (n > 0) h.Update((const std::uint8_t*)buf.data(), (std::size_t)n);
    }
    std::uint8_t d[32];
    h.Final(d);
    static const char* kHex = "0123456789abcdef";
    std::string out;
    for (std::uint8_t c : d) { out.push_back(kHex[c >> 4]); out.push_back(kHex[c & 15]); }
    return out;
}

/// Finish a complete temporary file: check the size cap and the expected SHA-256, then rename it over `path`. A cancel
/// that came in meanwhile (a caller that gave up on this transfer) wins: the file is dropped and `path` is not touched.
/// A rename that fails is NotHandled with the reason (not DiskFull: the disk is not full, the target is in use or locked).
inline OwnDownloadResult Finish(const std::filesystem::path& tmp, const std::filesystem::path& path, const OwnDownloadSpec& sp,
                                OwnDownloadResult out) {
    std::error_code ec;
    if (sp.cancelled && sp.cancelled()) { std::filesystem::remove(tmp, ec); out.outcome = OwnOutcome::Cancelled; return out; }
    out.bytes = SizeOf(tmp);
    if (sp.maxBytes && out.bytes > sp.maxBytes) { std::filesystem::remove(tmp, ec); out.outcome = OwnOutcome::TooBig; return out; }
    if (!sp.expectedSha256.empty()) {
        out.sha256 = Sha256File(tmp);   // lower-case hex; the expected value may be upper case
        bool same = out.sha256.size() == sp.expectedSha256.size();
        for (size_t i = 0; same && i < out.sha256.size(); ++i) same = out.sha256[i] == (char)std::tolower((unsigned char)sp.expectedSha256[i]);
        if (!same) {
            std::filesystem::remove(tmp, ec); out.outcome = OwnOutcome::ChecksumMismatch; return out;
        }
    }
    std::filesystem::remove(path, ec);
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        out.error = "rename failed: " + ec.message();
        std::filesystem::remove(tmp, ec);
        out.outcome = OwnOutcome::NotHandled;
        return out;
    }
    if (sp.onBytes) sp.onBytes(out.bytes);
    out.outcome = OwnOutcome::Ok;
    return out;
}

/// Strong validator of an answer for If-Range (RFC 9110 section 13.1.5: a weak ETag must not be used): the ETag unless it is
/// weak, else the Last-Modified date; "" when there is none.
inline std::string IfRangeValidator(const std::string& etag, const std::string& lastModified) {
    auto trim = [](const std::string& v) {
        size_t a = 0, b = v.size();
        while (a < b && (v[a] == ' ' || v[a] == '\t')) ++a;
        while (b > a && (v[b - 1] == ' ' || v[b - 1] == '\t')) --b;
        return v.substr(a, b - a);
    };
    const std::string e = trim(etag);
    if (!e.empty() && !(e.size() >= 2 && (e[0] == 'W' || e[0] == 'w') && e[1] == '/')) return e;
    return trim(lastModified);
}

#if defined(OKAY_HTTP_WINHTTP)
inline bool SleepUnlessCancelled(int ms, const std::function<bool()>& cancelled) {
    for (int waited = 0; waited < ms; waited += 50) {
        if (cancelled && cancelled()) return false;
        Sleep((DWORD)(ms - waited < 50 ? ms - waited : 50));
    }
    return !(cancelled && cancelled());
}

inline bool RetryableStatus(long s) { return s == 408 || s == 429 || s == 500 || s == 502 || s == 503 || s == 504; }

/// FetchToFile on WinHTTP: a temporary file next to `path`, a dropped transfer resumed with Range on the next try,
/// 408/429/5xx tried again with a doubling wait, a cancel or a cap that ends it at once. A resume sends If-Range with
/// the first answer's validator (as OwnHttpDownload.hpp does), so a file that changed comes back whole (200: the file
/// starts again); with no validator and no expected SHA-256 the next try starts from byte 0 instead of resuming,
/// and with only the expected SHA-256 a resumed file that fails the checksum is fetched once more from byte 0.
/// A 407 from a proxy is no answer here (NotHandled, error "proxy-auth ..."): WinHTTP does not answer that challenge.
inline OwnDownloadResult WinDownload(const std::string& url, const std::filesystem::path& path, const OwnDownloadSpec& sp) {
    OwnDownloadResult out;
    std::error_code ec;
    const std::filesystem::path tmp = std::filesystem::path(path).concat(".okdl-" + RandomHex8() + ".part");
    std::vector<std::string> hdrs;
    if (!sp.userAgent.empty()) hdrs.push_back("User-Agent: " + sp.userAgent);
    for (const std::string& h : sp.headers) hdrs.push_back(h);
    const uint64_t cap = sp.maxBytes ? sp.maxBytes : (8ull << 30);
    bool cancelled = false, tooBig = false;
    auto lastTick = std::chrono::steady_clock::now() - std::chrono::milliseconds(1000);
    int wait = sp.retryDelayMs > 0 ? sp.retryDelayMs : 1000;
    const auto t0 = std::chrono::steady_clock::now();
    std::string validator;   // of the answer the bytes in `tmp` came from
    int last = sp.retries;   // the last attempt; one more after a checksum mismatch on resumed bytes (below)
    bool wholeRetried = false;
    for (int attempt = 0; attempt <= last; ++attempt) {
        out.attempts = attempt + 1;
        RequestOptions ro;
        ro.connectMs = sp.connectMs; ro.ioMs = sp.ioMs;
        const long long left = (long long)sp.totalMs - (long long)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
        if (left <= 0) break;
        ro.totalMs = (int)left;
        ro.maxRedirects = sp.maxRedirects;
        ro.maxBytes = cap;
        ro.resumeFrom = attempt > 0 ? SizeOf(tmp) : 0;
        if (ro.resumeFrom > 0 && validator.empty() && sp.expectedSha256.empty()) {   // nothing proves the rest is the same file
            std::filesystem::remove(tmp, ec);
            ro.resumeFrom = 0;
        }
        ro.ifRange = validator;
        ro.cancel = [&]() { if (sp.cancelled && sp.cancelled()) { cancelled = true; return true; } return false; };
        ro.progress = [&](uint64_t done, uint64_t) {
            if (done > cap) { tooBig = true; return false; }
            if (sp.cancelled && sp.cancelled()) { cancelled = true; return false; }
            const auto now = std::chrono::steady_clock::now();
            if (sp.onBytes && now - lastTick >= std::chrono::milliseconds(100)) { lastTick = now; sp.onBytes(done); }
            return true;
        };
        const ExResult r = okay::http::detail::WinRequestEx("GET", url, hdrs, std::string(), std::string(), tmp, ro);
        out.status = r.status;
        if (r.resumed) ++out.resumes;
        if (r.answerStatus == 200) validator = IfRangeValidator(r.etag, r.lastModified);   // the file started again from this answer
        if (cancelled) { std::filesystem::remove(tmp, ec); out.outcome = OwnOutcome::Cancelled; return out; }
        if (r.writeFailed) { std::filesystem::remove(tmp, ec); out.outcome = OwnOutcome::DiskFull; return out; }
        if (tooBig || r.error.rfind("too-large", 0) == 0) { std::filesystem::remove(tmp, ec); out.outcome = OwnOutcome::TooBig; return out; }
        if (r.reached) {
            if (r.status == 200 || r.status == 206) {
                // With only the expected checksum a resume has no If-Range to send, so a file that changed on the server
                // comes back as two versions glued together. Fetch it once more from byte 0 before calling it wrong
                // (PR #1186 review); Finish has removed the temporary file, so the next try starts at 0 with no wait.
                const bool fromResume = out.resumes > 0;
                OwnDownloadResult f = Finish(tmp, path, sp, out);
                if (f.outcome == OwnOutcome::ChecksumMismatch && fromResume && !wholeRetried) {
                    wholeRetried = true;
                    validator.clear();
                    ++last;
                    continue;
                }
                return f;
            }
            if (r.status == 416 && ro.resumeFrom > 0) { std::filesystem::remove(tmp, ec); continue; }   // start the file again
            if (!RetryableStatus(r.status) || attempt == last) {
                std::filesystem::remove(tmp, ec); out.outcome = OwnOutcome::HttpError; return out;
            }
        } else {
            out.error = r.error;
            if (r.error.rfind("bad-response: Content-Range", 0) == 0) std::filesystem::remove(tmp, ec);   // the next try starts over
            if (r.error.rfind("bad-url", 0) == 0 || r.error.rfind("proxy-auth", 0) == 0) break;
        }
        if (attempt < last) {
            if (!SleepUnlessCancelled(wait, sp.cancelled)) { std::filesystem::remove(tmp, ec); out.outcome = OwnOutcome::Cancelled; return out; }
            wait = wait > 15000 ? 30000 : wait * 2;
        }
    }
    std::filesystem::remove(tmp, ec);
    out.outcome = OwnOutcome::NotHandled;   // no clear answer; `error` says why
    return out;
}
#endif

} // namespace fetch_detail

/// GET `url` into the file `path` (replacing it only when the whole file arrived), like `curl -L -f [--max-filesize N]
/// [--retry N] -o path url` with a progress callback and a cancel check. Windows: WinHTTP; Linux: our own client.
/// NotHandled means no clear answer came (`error` says why; NoClientReason() on a build with no client).
inline OwnDownloadResult FetchToFile(const std::string& url, const std::filesystem::path& path, const OwnDownloadSpec& spIn) {
    OwnDownloadSpec sp = spIn;   // header lines lose any CR/LF (CleanHeaderLine)
    sp.userAgent = CleanHeaderLine(sp.userAgent);
    sp.headers = CleanHeaderLines(sp.headers);
    OwnDownloadResult r;
    if (sp.cancelled && sp.cancelled()) r.outcome = OwnOutcome::Cancelled;
    else if (!NativeAvailable()) r.error = NoClientReason();
    else {
#if defined(OKAY_OWN_HTTP_ACTIVE)
        r = OwnDownload(url, fetch_detail::Utf8(path), sp);
#elif defined(OKAY_HTTP_WINHTTP)
        r = fetch_detail::WinDownload(url, path, sp);
#else
        (void)url;
#endif
    }
    // On anything but Ok an existing `path` is removed too, so a caller cannot take a stale file for this download's result.
    if (r.outcome != OwnOutcome::Ok) { std::error_code ec; std::filesystem::remove(path, ec); }
    return r;
}

/// A HEAD request that follows redirects, like `curl -sIL`: true when a final answer arrived (any status, as curl -I
/// does without -f). `dump` is that answer's status line and headers as text, so a caller's Content-Length parser works
/// on it unchanged. false = no usable answer.
inline bool HeadDump(const std::string& url, const std::string& userAgent, std::string& dump, int totalMs = 60000) {
    dump.clear();
    if (!NativeAvailable()) return false;
    RequestOptions ro;
    ro.connectMs = 20000; ro.ioMs = 30000; ro.totalMs = totalMs;
    ro.maxRedirects = 20;
    std::vector<std::string> hdrs;
    if (!userAgent.empty()) hdrs.push_back("User-Agent: " + CleanHeaderLine(userAgent));
    const ExResult r = RequestEx("HEAD", url, hdrs, std::string(), std::string(), std::string(), ro);
    if (!r.reached) return false;
    dump = r.rawHead;
    return true;
}

/// The first bytes of a page, like `curl -L -f -m T --max-filesize M -r 0-LAST -A ua url`. Ok means a 200 or 206
/// answer; `body` is what came (the server may ignore the range and send more, up to maxBytes: the caller cuts).
/// HttpError: status >= 400 (or any other non-success). TooBig: more than maxBytes. NotHandled: no usable answer.
inline OwnOutcome FetchPrefix(const std::string& url, const std::string& userAgent, uint64_t maxBytes, uint64_t lastByte,
                              int totalMs, std::string& body, long* status = nullptr) {
    body.clear();
    if (status) *status = 0;
    if (!NativeAvailable()) return OwnOutcome::NotHandled;
    RequestOptions ro;
    ro.maxBytes = maxBytes;
    ro.maxRedirects = 10;
    ro.connectMs = totalMs < 8000 ? totalMs : 8000; ro.ioMs = totalMs; ro.totalMs = totalMs;
    std::vector<std::string> hdrs;
    if (!userAgent.empty()) hdrs.push_back("User-Agent: " + CleanHeaderLine(userAgent));
    hdrs.push_back("Range: bytes=0-" + std::to_string(lastByte));
    ExResult r = RequestEx("GET", url, hdrs, std::string(), std::string(), std::string(), ro);
    if (status) *status = r.status;
    if (!r.reached) return r.error.rfind("too-large", 0) == 0 ? OwnOutcome::TooBig : OwnOutcome::NotHandled;
    if (r.status == 200 || r.status == 206) { body = std::move(r.body); return OwnOutcome::Ok; }
    return OwnOutcome::HttpError;   // 416 included: curl -f fails on it too
}

/// One request with headers ("Name: value" lines) and a body (`body`, or the file `bodyFile`), like `curl -s -X METHOD
/// -w %{http_code}` without -f: any status is an answer and the body comes back with it. `timeoutMs` > 0 limits the
/// whole call. Never sent a second time: when the call fails, reached is false and `error` says why (maybeSent tells
/// whether it may have arrived). Header lines lose any CR/LF. Like curl without -L, a call that carries a credential
/// header (Authorization, apikey, ...: HeaderCarriesSecret) does not follow a redirect on WinHTTP: the 3xx is the answer,
/// so the header never goes to another host.
inline Result Send(const std::string& method, const std::string& url, const std::vector<std::string>& rawHeaders,
                   const std::string& body = std::string(), const std::string& bodyFile = std::string(), int timeoutMs = 0) {
    const std::vector<std::string> headers = CleanHeaderLines(rawHeaders);
    if (!NativeAvailable()) { Result r; r.error = NoClientReason(); return r; }
    return Request(method, url, headers, body, bodyFile, std::string(), timeoutMs, !AnyHeaderCarriesSecret(headers));
}

} // namespace okay::http
