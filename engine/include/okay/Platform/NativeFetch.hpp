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
// docs/own_http_readiness.md lists which call sites use what.
// ---------------------------------------------------------------------------
#include "okay/Platform/NativeHttp.hpp"

#if defined(OKAY_OWN_HTTP_ACTIVE)
#include <chrono>
#include <filesystem>
#include <thread>

namespace okay::http {

enum class OwnOutcome {
    NotHandled,   ///< no usable answer (libssl missing, certificate refused, proxy trouble, bad reply): run curl
    Ok,           ///< 200 and the file is complete (for OwnFetchPrefix: the body is in `body`)
    HttpError,    ///< the server answered with a status that curl -f would call a failure; `status` says which
    Cancelled,    ///< the caller's cancel check said stop
    TooBig,       ///< more data than maxBytes (announced or sent)
    DiskFull      ///< the output file could not be opened or written
};

struct OwnDownloadSpec {
    std::string userAgent;                   ///< "" = the client's default
    std::vector<std::string> headers;        ///< extra "Name: value" lines (for example the no-cache pair)
    uint64_t maxBytes = 0;                   ///< like --max-filesize; 0 = the client's default cap
    int maxRedirects = 0;                    ///< 0 = the client's default (10)
    int connectMs = 20000, ioMs = 60000, totalMs = 6 * 60 * 60 * 1000;
    int retries = 0;                         ///< like --retry N: transient failures and 408/429/500/502/503/504 are tried again
    int retryDelayMs = 1000;                 ///< first wait; doubles each time (as curl does)
    std::function<void(uint64_t)> onBytes;   ///< bytes written so far, at most every 100 ms and once at the end
    std::function<bool()> cancelled;         ///< true = stop now (polled about every 50 ms while data flows, 200 ms while it is silent)
};

struct OwnDownloadResult {
    OwnOutcome outcome = OwnOutcome::NotHandled;
    long status = 0;
    uint64_t bytes = 0;
    int attempts = 0;
    std::string error;                       ///< the client's reason when NotHandled (for logs)
};

namespace detail {
inline bool SleepUnlessCancelled(int ms, const std::function<bool()>& cancelled) {
    for (int left = ms; left > 0; left -= 50) {
        if (cancelled && cancelled()) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(left < 50 ? left : 50));
    }
    return !(cancelled && cancelled());
}
inline bool RetryableStatus(long s) { return s == 408 || s == 429 || s == 500 || s == 502 || s == 503 || s == 504; }
/// A failure worth another try (curl --retry: timeouts and connection trouble), not a refusal that will repeat.
inline bool TransientError(const std::string& err) {
    return err.rfind("timeout", 0) == 0 || err.rfind("io", 0) == 0 || err.rfind("connect", 0) == 0 ||
           err.find("connection closed early") != std::string::npos;
}
} // namespace detail

/// GET `url` into the file `path` (replacing it), like `curl -L -f [--max-filesize N] [--retry N] -o path url` with a
/// progress callback and a cancel check. On anything but Ok the file is removed.
inline OwnDownloadResult OwnDownload(const std::string& url, const std::string& path, const OwnDownloadSpec& sp) {
    namespace fs = std::filesystem;
    OwnDownloadResult out;
    if (!NativeAvailable()) return out;
    std::error_code ec;
    int delay = sp.retryDelayMs;
    for (int attempt = 0; attempt <= sp.retries; ++attempt) {
        out.attempts = attempt + 1;
        bool wasCancelled = false, tooBig = false;
        auto cancelNow = [&]() { if (sp.cancelled && sp.cancelled()) { wasCancelled = true; return true; } return false; };
        RequestOptions ro;
        ro.maxBytes = sp.maxBytes;
        ro.maxRedirects = sp.maxRedirects;
        ro.connectMs = sp.connectMs; ro.ioMs = sp.ioMs; ro.totalMs = sp.totalMs;
        ro.cancel = cancelNow;
        auto lastTick = std::chrono::steady_clock::now() - std::chrono::milliseconds(1000);
        auto lastPoll = lastTick;
        ro.progress = [&](uint64_t done, uint64_t) {
            const auto now = std::chrono::steady_clock::now();
            if (now - lastPoll >= std::chrono::milliseconds(50)) {
                lastPoll = now;
                if (cancelNow()) return false;
            }
            if (sp.maxBytes > 0 && done > sp.maxBytes) { tooBig = true; return false; }
            if (sp.onBytes && now - lastTick >= std::chrono::milliseconds(100)) { lastTick = now; sp.onBytes(done); }
            return true;
        };
        std::vector<std::string> hdrs;
        if (!sp.userAgent.empty()) hdrs.push_back("User-Agent: " + sp.userAgent);
        for (const std::string& h : sp.headers) hdrs.push_back(h);
        const ExResult r = RequestEx("GET", url, hdrs, std::string(), std::string(), path, ro);
        out.status = r.status;
        if (wasCancelled) { fs::remove(path, ec); out.outcome = OwnOutcome::Cancelled; return out; }
        if (r.writeFailed) { fs::remove(path, ec); out.outcome = OwnOutcome::DiskFull; return out; }
        if (tooBig || (!r.reached && r.error.rfind("too-large", 0) == 0)) { fs::remove(path, ec); out.outcome = OwnOutcome::TooBig; return out; }
        if (r.reached) {
            if (r.status == 200 && fs::is_regular_file(path, ec)) {
                const std::uintmax_t sz = fs::file_size(path, ec);
                out.bytes = ec ? 0 : uint64_t(sz);
                if (sp.onBytes && !ec) sp.onBytes(out.bytes);
                out.outcome = OwnOutcome::Ok;
                return out;
            }
            fs::remove(path, ec);
            if (detail::RetryableStatus(r.status) && attempt < sp.retries) {
                if (!detail::SleepUnlessCancelled(delay, sp.cancelled)) { out.outcome = OwnOutcome::Cancelled; return out; }
                delay *= 2; continue;
            }
            out.outcome = OwnOutcome::HttpError;
            return out;
        }
        fs::remove(path, ec);
        out.error = r.error;
        if (detail::TransientError(r.error) && attempt < sp.retries) {
            if (!detail::SleepUnlessCancelled(delay, sp.cancelled)) { out.outcome = OwnOutcome::Cancelled; return out; }
            delay *= 2; continue;
        }
        out.outcome = OwnOutcome::NotHandled;
        return out;
    }
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
