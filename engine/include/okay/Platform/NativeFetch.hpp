#pragma once
// ---------------------------------------------------------------------------
// NativeFetch - THE HTTP seam of the launcher, editor, player and engine (downloads, size probes, link previews,
// account and PlayFab calls). Four calls, each with the behaviour of the curl command it replaced (same flags in
// spirit: -L, -f, --max-filesize, -m, -r, --retry, a progress callback, a cancel that stops the transfer):
//
//   FetchToFile  GET into a file, with progress, cancel, a size cap and retries (temporary file, renamed when complete)
//   HeadDump     HEAD that follows redirects; the final answer's head as text (for Content-Length)
//   FetchPrefix  the first bytes of a page (Range), with a size cap and a time limit
//   Send         one request with headers and a body (any status is an answer; never re-sent when it may have arrived)
//
// Each uses the native client first: WinHTTP on Windows (NativeHttp RequestEx), our own HTTP/1.1 client on Linux
// when it is on (OKAY_OWN_HTTP). Only when there is no native client, or it gives no clear answer and the call is safe
// to repeat, does it run curl, through CurlFallback.hpp (the one place curl is started; tests/check_curl_seam_wiring.cmake
// keeps it that way). Clear answers (an HTTP status, a cancel, a size cap, a full disk) are final. OKAY_NO_CURL=1 turns
// the curl fallback off. The checksum and signature checks stay with the callers (UpdaterCore, VersionsManager); this
// seam only fetches bytes.
//
// OwnDownload is the own client's half (POSIX with -DOKAY_OWN_HTTP=ON); OwnHeadDump and OwnFetchPrefix run on either
// native client (RequestEx). FetchToFile and friends call them.
// docs/platform/own_http.md lists which call sites use what.
// ---------------------------------------------------------------------------
#include "okay/Platform/NativeHttp.hpp"
#include "okay/Platform/CurlFallback.hpp"   // the curl fallback: the only place curl is started
#include "okay/Crypto/Sha256.hpp"

#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#if defined(OKAY_OWN_HTTP_ACTIVE)
#include "okay/Platform/OwnHttpDownload.hpp"   // temporary file, SHA-256 while streaming, resume, retries
#endif
#if !defined(_WIN32)
#include <fcntl.h>   // open() with mode 0600
#endif

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
    bool httpsOnlyRedirects = false;         ///< like curl --proto-redir =https (WinHTTP and the own client refuse https -> http anyway)
    int stallSeconds = 60;                   ///< curl fallback only: a transfer under 1 KB/s for this long ends (--speed-time); 0 = off
};

struct OwnDownloadResult {
    OwnOutcome outcome = OwnOutcome::NotHandled;
    long status = 0;
    uint64_t bytes = 0;
    int attempts = 0;
    int resumes = 0;                         ///< tries that continued a dropped transfer with a 206
    std::string sha256;                      ///< lower-case hex SHA-256 of the file (Ok)
    std::string error;                       ///< the client's reason when NotHandled (for logs)
    bool viaCurl = false;                    ///< the answer came from the curl fallback (`status` is then 0 for an HTTP error)
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
    default:   // NetworkError, SizeMismatch (an answer cut short), BadInput: not a clear answer, the caller runs curl
        out.outcome = wasCancelled ? OwnOutcome::Cancelled : OwnOutcome::NotHandled;
        out.error = std::string(oh::ErrName(r.err)) + ": " + r.message;
        break;
    }
    fs::remove(path, ec);
    return out;
}

#endif // OKAY_OWN_HTTP_ACTIVE

// OwnHeadDump and OwnFetchPrefix run on whichever native client RequestEx has (the own client, or WinHTTP).
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


// ---------------------------------------------------------------------------
// The seam: native first, curl last (every platform).
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

/// A new file in the temporary folder, "<tag><random><ext>".
inline std::filesystem::path TempFile(const char* tag, const char* ext) {
    std::error_code ec;
    std::filesystem::path dir = std::filesystem::temp_directory_path(ec);
    if (ec) dir = ".";
    return dir / (std::string(tag) + RandomHex8() + ext);
}

/// Write `data` to a new file only this user can read (POSIX: mode 0600, O_EXCL); used for request bodies and the curl
/// config file that carries the auth headers, so a secret never sits in a file other users can open.
inline bool WritePrivate(const std::filesystem::path& p, const std::string& data) {
#if defined(_WIN32)
    std::ofstream f(p, std::ios::binary | std::ios::trunc);   // %TEMP% is the user's own folder
    f.write(data.data(), (std::streamsize)data.size());
    return bool(f);
#else
    const int fd = ::open(p.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_TRUNC, 0600);
    if (fd < 0) return false;
    std::FILE* f = ::fdopen(fd, "wb");
    if (!f) return false;
    const bool ok = std::fwrite(data.data(), 1, data.size(), f) == data.size();
    return std::fclose(f) == 0 && ok;
#endif
}

inline std::string Slurp(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss; ss << f.rdbuf();
    return ss.str();
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

/// Escape a value for a quoted curl config line (the config format reads \\ and \" inside double quotes).
inline std::string CurlCfgQuote(const std::string& v) {
    std::string o = "\"";
    for (char c : v) {
        if (c == '\r' || c == '\n') continue;
        if (c == '\\' || c == '"') o.push_back('\\');
        o.push_back(c);
    }
    o.push_back('"');
    return o;
}

inline bool SleepUnlessCancelled(int ms, const std::function<bool()>& cancelled) {
    for (int waited = 0; waited < ms; waited += 50) {
        if (cancelled && cancelled()) return false;
        curlfb::detail::SleepMs(ms - waited < 50 ? ms - waited : 50);
    }
    return !(cancelled && cancelled());
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
inline bool RetryableStatus(long s) { return s == 408 || s == 429 || s == 500 || s == 502 || s == 503 || s == 504; }

/// FetchToFile on WinHTTP: a temporary file next to `path`, a dropped transfer resumed with Range on the next try,
/// 408/429/5xx tried again with a doubling wait, a cancel or a cap that ends it at once. A resume sends If-Range with
/// the first answer's validator (as OwnHttpDownload.hpp does), so a file that changed comes back whole (200: the file
/// starts again); with no validator and no expected SHA-256 the next try starts from byte 0 instead of resuming,
/// and with only the expected SHA-256 a resumed file that fails the checksum is fetched once more from byte 0.
/// A 407 from a proxy is no answer here (NotHandled): curl may know the proxy's password.
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
    out.outcome = OwnOutcome::NotHandled;   // no clear answer: the caller's next step is the curl fallback
    return out;
}
#endif

/// The curl fallback of FetchToFile: `curl -L -s -f [--retry N] [--max-filesize N] ... -o <tmp> <url>`, progress from the
/// growing file, cancel and the size cap by ending the program.
inline OwnDownloadResult CurlDownload(const std::string& url, const std::filesystem::path& path, const OwnDownloadSpec& sp) {
    OwnDownloadResult out;
    out.viaCurl = true;
    std::error_code ec;
    const std::filesystem::path tmp = std::filesystem::path(path).concat(".okdl-" + RandomHex8() + ".part");
    std::vector<std::string> a = {"-L", "-s", "-f", "--connect-timeout", std::to_string(sp.connectMs > 0 ? (sp.connectMs + 999) / 1000 : 20)};
    if (sp.stallSeconds > 0) { a.push_back("--speed-limit"); a.push_back("1024"); a.push_back("--speed-time"); a.push_back(std::to_string(sp.stallSeconds)); }
    if (sp.maxRedirects > 0) { a.push_back("--max-redirs"); a.push_back(std::to_string(sp.maxRedirects)); }
    if (sp.httpsOnlyRedirects && url.compare(0, 8, "https://") == 0) { a.push_back("--proto-redir"); a.push_back("=https"); }
    if (sp.maxBytes) { a.push_back("--max-filesize"); a.push_back(std::to_string((unsigned long long)sp.maxBytes)); }
    if (sp.retries > 0) { a.push_back("--retry"); a.push_back(std::to_string(sp.retries)); }
    if (!sp.userAgent.empty()) { a.push_back("-A"); a.push_back(sp.userAgent); }
    for (const std::string& h : sp.headers) { a.push_back("-H"); a.push_back(h); }
    a.push_back("-o"); a.push_back(Utf8(tmp));
    a.push_back(url);
    enum class Why { None, Cancelled, TooBig } why = Why::None;
    uint64_t last = 0;
    curlfb::RunSpec rs;
    rs.pollMs = 100;
    if (sp.totalMs > 0) rs.timeoutMs = (unsigned)sp.totalMs;
    rs.keepGoing = [&]() {
        const uint64_t now = SizeOf(tmp);
        if (now != last) { last = now; if (sp.onBytes) sp.onBytes(now); }
        if (sp.cancelled && sp.cancelled()) { why = Why::Cancelled; return false; }
        if (sp.maxBytes > 0 && now > sp.maxBytes) { why = Why::TooBig; return false; }
        return true;
    };
    const curlfb::RunResult run = curlfb::RunCurlProcess(a, rs);
    out.attempts = 1;
    if (!run.started) {
        std::filesystem::remove(tmp, ec);
        out.viaCurl = false;
        out.error = curlfb::Disabled() ? "the curl fallback is off (OKAY_NO_CURL=1)" : "curl could not be started";
        return out;   // NotHandled: the caller may still try its last resort
    }
    if (run.stopped) {
        std::filesystem::remove(tmp, ec);
        out.outcome = why == Why::Cancelled ? OwnOutcome::Cancelled : why == Why::TooBig ? OwnOutcome::TooBig : OwnOutcome::NotHandled;
        if (run.timedOut) out.error = "curl ran out of time";
        return out;
    }
    if (run.exitCode == 0 && std::filesystem::is_regular_file(tmp, ec)) return Finish(tmp, path, sp, out);
    std::filesystem::remove(tmp, ec);
    switch (run.exitCode) {
    case 22: out.outcome = OwnOutcome::HttpError; break;   // -f: the server answered 400 or more
    case 23: out.outcome = OwnOutcome::DiskFull; break;    // write error
    case 63: out.outcome = OwnOutcome::TooBig; break;      // --max-filesize
    default: out.outcome = OwnOutcome::NotHandled; out.error = "curl exit code " + std::to_string(run.exitCode); break;
    }
    return out;
}
} // namespace fetch_detail

/// GET `url` into the file `path` (replacing it only when the whole file arrived), like `curl -L -f [--max-filesize N]
/// [--retry N] -o path url` with a progress callback and a cancel check. Native first (Windows: WinHTTP; Linux: our own
/// client when on), then curl. NotHandled means nothing could fetch it (no native answer and curl missing or off).
inline OwnDownloadResult FetchToFile(const std::string& url, const std::filesystem::path& path, const OwnDownloadSpec& spIn) {
    OwnDownloadSpec sp = spIn;   // header lines lose any CR/LF on every path (CleanHeaderLine)
    sp.userAgent = CleanHeaderLine(sp.userAgent);
    sp.headers = CleanHeaderLines(sp.headers);
    // On anything but Ok, an existing `path` is removed as well, on every path (the own client's OwnDownload always did;
    // WinHTTP and curl left the old file there, so a caller could take a stale file for this download's result).
    auto dropOld = [&](OwnDownloadResult r) {
        if (r.outcome != OwnOutcome::Ok) { std::error_code ec; std::filesystem::remove(path, ec); }
        return r;
    };
    if (NativeAvailable()) {
#if defined(OKAY_OWN_HTTP_ACTIVE)
        OwnDownloadResult r = OwnDownload(url, fetch_detail::Utf8(path), sp);
        if (r.outcome != OwnOutcome::NotHandled) return dropOld(r);
#elif defined(OKAY_HTTP_WINHTTP)
        OwnDownloadResult r = fetch_detail::WinDownload(url, path, sp);
        if (r.outcome != OwnOutcome::NotHandled) return dropOld(r);
        if (r.error.rfind("rename failed", 0) == 0) return dropOld(r);   // the file came; curl would only fetch it again to the same rename
#endif
    }
    if (sp.cancelled && sp.cancelled()) { OwnDownloadResult c; c.outcome = OwnOutcome::Cancelled; return dropOld(c); }
    return dropOld(fetch_detail::CurlDownload(url, path, sp));
}

/// A HEAD request that follows redirects, like `curl -sIL`: true when an answer arrived (any status, as curl -I does
/// without -f). `dump` is the answer's status line and headers as text (curl's fallback dumps every answer of the
/// redirect chain; read the LAST Content-Length). false = no answer at all.
inline bool HeadDump(const std::string& url, const std::string& userAgent, std::string& dump, int totalMs = 60000) {
    dump.clear();
    if (NativeAvailable()) {
        if (OwnHeadDump(url, userAgent, dump, totalMs)) return true;
    }
    std::error_code ec;
    const std::filesystem::path hdr = fetch_detail::TempFile("okay_head_", ".txt");
    std::vector<std::string> a = {"-s", "-I", "-L", "--max-time", std::to_string(totalMs > 0 ? (totalMs + 999) / 1000 : 60)};
    if (!userAgent.empty()) { a.push_back("-A"); a.push_back(userAgent); }
#if defined(_WIN32)
    a.push_back("-o"); a.push_back("NUL");
#else
    a.push_back("-o"); a.push_back("/dev/null");
#endif
    a.push_back("-D"); a.push_back(fetch_detail::Utf8(hdr));
    a.push_back(url);
    const curlfb::RunResult run = curlfb::RunCurlProcess(a);
    if (run.started && run.exitCode == 0) dump = fetch_detail::Slurp(hdr);
    std::filesystem::remove(hdr, ec);
    return !dump.empty();
}

/// The first bytes of a page, like `curl -L -f -m T --max-filesize M -r 0-LAST -A ua url`. Ok means a 200 or 206
/// answer; `body` is what came (the server may ignore the range and send more, up to maxBytes: the caller cuts).
/// HttpError: status >= 400 (or any other non-success). TooBig: more than maxBytes. NotHandled: nothing answered.
inline OwnOutcome FetchPrefix(const std::string& url, const std::string& userAgent, uint64_t maxBytes, uint64_t lastByte,
                              int totalMs, std::string& body, long* status = nullptr) {
    body.clear();
    if (status) *status = 0;
    if (NativeAvailable()) {
        const OwnOutcome o = OwnFetchPrefix(url, userAgent, maxBytes, lastByte, totalMs, body, status);
        if (o != OwnOutcome::NotHandled) return o;
    }
    std::error_code ec;
    const std::filesystem::path tmp = fetch_detail::TempFile("okay_prefix_", ".bin");
    std::vector<std::string> a = {"-L", "-s", "-f", "-m", std::to_string(totalMs > 0 ? (totalMs + 999) / 1000 : 8),
                                  "-r", "0-" + std::to_string((unsigned long long)lastByte)};
    if (maxBytes) { a.push_back("--max-filesize"); a.push_back(std::to_string((unsigned long long)maxBytes)); }
    if (!userAgent.empty()) { a.push_back("-A"); a.push_back(userAgent); }
    a.push_back("-o"); a.push_back(fetch_detail::Utf8(tmp));
    a.push_back(url);
    const curlfb::RunResult run = curlfb::RunCurlProcess(a);
    OwnOutcome o = OwnOutcome::NotHandled;
    if (run.started) {
        body = fetch_detail::Slurp(tmp);
        // 0 = done; 28 = the time ran out after some bytes came (the old command parsed those too)
        if ((run.exitCode == 0 || run.exitCode == 28) && !body.empty()) o = OwnOutcome::Ok;
        else if (run.exitCode == 22) o = OwnOutcome::HttpError;
        else if (run.exitCode == 63) o = OwnOutcome::TooBig;
        if (o != OwnOutcome::Ok) body.clear();
        if (o == OwnOutcome::Ok && maxBytes && body.size() > maxBytes) body.resize((size_t)maxBytes);
    }
    std::filesystem::remove(tmp, ec);
    return o;
}

/// One request with headers ("Name: value" lines) and a body (`body`, or the file `bodyFile`), like `curl -s -X METHOD
/// -w %{http_code}` without -f: any status is an answer and the body comes back with it. `timeoutMs` > 0 limits the
/// whole call. Native first; curl only when the native call cannot have reached the server, or the method is GET or
/// HEAD (ShouldFallBackToCurl), so a POST is never sent twice. Headers and the body go to curl through files only this
/// user can read (never the command line), so secrets do not show up in the process list. Header lines lose any CR/LF.
/// Like curl without -L, a call that carries a credential header (Authorization, apikey, ...: HeaderCarriesSecret) does
/// not follow a redirect on WinHTTP: the 3xx is the answer, so the header never goes to another host. A 407 from a proxy
/// is no answer from WinHTTP, so curl is tried (the proxy did not pass the request on, so even a POST is safe to send).
inline Result Send(const std::string& method, const std::string& url, const std::vector<std::string>& rawHeaders,
                   const std::string& body = std::string(), const std::string& bodyFile = std::string(), int timeoutMs = 0) {
    const std::vector<std::string> headers = CleanHeaderLines(rawHeaders);
    if (NativeAvailable()) {
        Result nr = Request(method, url, headers, body, bodyFile, std::string(), timeoutMs, !AnyHeaderCarriesSecret(headers));
        if (nr.reached) return nr;
        if (!ShouldFallBackToCurl(method, nr)) return nr;
    }
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path resp = fetch_detail::TempFile("okay_http_", ".out");
    const fs::path code = fetch_detail::TempFile("okay_http_", ".code");
    const fs::path bodyF = fetch_detail::TempFile("okay_http_", ".body");
    const fs::path cfgF = fetch_detail::TempFile("okay_http_", ".cfg");
    std::vector<std::string> a = {"-s"};
    if (method == "HEAD") a.push_back("-I");   // (-X HEAD would wait for a body that never comes)
    else { a.push_back("-X"); a.push_back(method); }
    if (timeoutMs > 0) {
        const int secs = (timeoutMs + 999) / 1000;
        a.push_back("--connect-timeout"); a.push_back(std::to_string(secs < 5 ? secs : 5));
        a.push_back("--max-time"); a.push_back(std::to_string(secs));
    }
    a.push_back("-o"); a.push_back(fetch_detail::Utf8(resp));
    a.push_back("-w"); a.push_back("%{http_code}");
    Result out;
    bool okFiles = true;
    if (!bodyFile.empty()) { a.push_back("--data-binary"); a.push_back("@" + bodyFile); }
    else if (!body.empty()) {
        okFiles = fetch_detail::WritePrivate(bodyF, body);
        a.push_back("--data-binary"); a.push_back("@" + fetch_detail::Utf8(bodyF));
    }
    std::string cfg;
    for (const std::string& h : headers) cfg += "header = " + fetch_detail::CurlCfgQuote(h) + "\n";
    if (!cfg.empty()) {
        okFiles = okFiles && fetch_detail::WritePrivate(cfgF, cfg);
        a.push_back("-K"); a.push_back(fetch_detail::Utf8(cfgF));
    }
    a.push_back(url);
    if (okFiles) {
        curlfb::RunSpec rs;
        rs.stdoutFile = fetch_detail::Utf8(code);
        if (timeoutMs > 0) rs.timeoutMs = (unsigned)timeoutMs + 5000;   // curl's own --max-time ends it first
        const curlfb::RunResult run = curlfb::RunCurlProcess(a, rs);
        if (run.started && !run.stopped) {
            out.body = fetch_detail::Slurp(resp);
            const std::string codeS = fetch_detail::Slurp(code);
            long st = 0;
            for (char c : codeS) { if (c < '0' || c > '9') break; st = st * 10 + (c - '0'); if (st > 999) break; }
            out.status = st;
            out.reached = run.exitCode == 0 && st != 0;
            if (!out.reached) { out.body.clear(); out.status = 0; out.error = "curl exit code " + std::to_string(run.exitCode); }
        } else {
            out.error = run.started ? "curl ran out of time" : "curl could not be started";
        }
    } else {
        out.error = "cannot write the request files";
    }
    fs::remove(resp, ec); fs::remove(code, ec); fs::remove(bodyF, ec); fs::remove(cfgF, ec);
    return out;
}

} // namespace okay::http
