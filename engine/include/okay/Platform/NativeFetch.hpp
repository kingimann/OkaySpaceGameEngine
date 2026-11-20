#pragma once
// ---------------------------------------------------------------------------
// NativeFetch - the call-site helpers that put our own HTTP client (okay::ownhttp, through NativeHttp's RequestEx)
// in front of the spawned curl for the launcher's and editor's downloads, size probes, link previews and for
// PlayFab. Each helper has the behaviour of the curl command it stands in for (same flags in spirit: -L, -f,
// --max-filesize, -m, -r, --retry, a progress file that grows, a cancel that stops the transfer) and the same
// rule as VersionsNet DownloadFile: anything that is not a clear answer returns NotHandled and the caller runs its
// curl command exactly as before. Clear answers (an HTTP status, a cancel, a size cap, a full disk) are final.
//
// Only exists where the own client does: POSIX built with -DOKAY_OWN_HTTP=ON (OKAY_OWN_HTTP_ACTIVE), and it is
// used only when NativeAvailable() (environment variable OKAY_OWN_HTTP=1). Windows (WinHTTP) and macOS never
// see these functions; callers wrap their use in `#if defined(OKAY_OWN_HTTP_ACTIVE)`.
// docs/platform/own_http_readiness.md lists which call sites use what.
// ---------------------------------------------------------------------------
#include "okay/Platform/NativeHttp.hpp"

#if defined(OKAY_OWN_HTTP_ACTIVE)
#include "okay/Platform/OwnHttpDownload.hpp"   // temporary file, SHA-256 while streaming, resume, retries
#include <chrono>
#include <filesystem>

namespace okay::http {

enum class OwnOutcome {
    NotHandled,   ///< no usable answer (libssl missing, certificate refused, proxy trouble, bad reply): run curl
    Ok,           ///< 200 and the file is complete (for OwnFetchPrefix: the body is in `body`)
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
    default:   // NetworkError, SizeMismatch (an answer cut short), BadInput: not a clear answer, the caller runs curl
        out.outcome = wasCancelled ? OwnOutcome::Cancelled : OwnOutcome::NotHandled;
        out.error = std::string(oh::ErrName(r.err)) + ": " + r.message;
        break;
    }
    fs::remove(path, ec);
    return out;
}

/// A HEAD request that follows redirects, like `curl -sIL`: true when a final answer arrived (any status, as curl -I
/// does without -f). `dump` is that answer's status line and headers as text, so a caller's existing Content-Length
/// parser works on it unchanged. false = no usable answer (run curl).
inline bool OwnHeadDump(const std::string& url, const std::string& userAgent, std::string& dump, int totalMs = 60000) {
    dump.clear();
    if (!NativeAvailable()) return false;
    RequestOptions ro;
    ro.connectMs = 20000; ro.ioMs = 30000; ro.totalMs = totalMs;
    ro.maxRedirects = 20;
    std::vector<std::string> hdrs;
    if (!userAgent.empty()) hdrs.push_back("User-Agent: " + userAgent);
    const ExResult r = RequestEx("HEAD", url, hdrs, std::string(), std::string(), std::string(), ro);
    if (!r.reached) return false;
    dump = r.rawHead;
    return true;
}

/// The first bytes of a page, like `curl -L -f -m T --max-filesize M -r 0-LAST -A ua url`. Ok means a 200 or 206
/// answer; `body` is what came (the server may ignore the range and send more, up to maxBytes: the caller cuts).
/// HttpError: status >= 400 (or any other non-success). TooBig: more than maxBytes. NotHandled: run curl.
inline OwnOutcome OwnFetchPrefix(const std::string& url, const std::string& userAgent, uint64_t maxBytes,
                                 uint64_t lastByte, int totalMs, std::string& body, long* status = nullptr) {
    body.clear();
    if (status) *status = 0;
    if (!NativeAvailable()) return OwnOutcome::NotHandled;
    RequestOptions ro;
    ro.maxBytes = maxBytes;
    ro.maxRedirects = 10;
    ro.connectMs = totalMs < 8000 ? totalMs : 8000; ro.ioMs = totalMs; ro.totalMs = totalMs;
    std::vector<std::string> hdrs;
    if (!userAgent.empty()) hdrs.push_back("User-Agent: " + userAgent);
    hdrs.push_back("Range: bytes=0-" + std::to_string(lastByte));
    ExResult r = RequestEx("GET", url, hdrs, std::string(), std::string(), std::string(), ro);
    if (status) *status = r.status;
    if (!r.reached) return r.error.rfind("too-large", 0) == 0 ? OwnOutcome::TooBig : OwnOutcome::NotHandled;
    if (r.status == 200 || r.status == 206) { body = std::move(r.body); return OwnOutcome::Ok; }
    return OwnOutcome::HttpError;   // 416 included: curl -f fails on it too
}

} // namespace okay::http
#endif // OKAY_OWN_HTTP_ACTIVE
