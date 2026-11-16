#pragma once
// ---------------------------------------------------------------------------
// okay::ownhttp robust transfers (POSIX), on top of Perform (OwnHttpNet.hpp):
//
//  - RetryPolicy / PerformWithRetry: retries with exponential backoff and jitter after connect failures, timeouts,
//    dropped connections and the answers 408, 429, 500, 502, 503 and 504, honouring Retry-After (seconds or an HTTP
//    date). Only idempotent methods are ever retried (RFC 9110 section 9.2.2: GET, HEAD, OPTIONS, TRACE, PUT, DELETE);
//    POST, PATCH and any other method get exactly one attempt.
//  - DownloadToFile: streams a GET into a temporary file next to the destination, computes SHA-256 while the bytes
//    arrive (okay::crypto::Sha256, the engine's own), checks it (and the size) against what the caller expects, then
//    renames the temporary file over the destination in one step (rename(2) is atomic on one file system). After a
//    dropped connection it asks for the rest with "Range: bytes=N-" (and If-Range with the strong ETag or the
//    Last-Modified date of the first answer); a 206 must carry the matching Content-Range (Fetch checks it), a 200
//    means the server sent the whole file again and the temporary file starts over. Progress and cancel callbacks.
//    A wrong checksum, a cancel or any failure removes the temporary file; the destination is never touched unless
//    the whole file arrived and matched.
//
// Written for this engine from RFC 9110 (sections 9.2.2 idempotent methods, 10.2.3 Retry-After, 5.6.7 HTTP-date,
// 13.1.5 If-Range, 14 range requests, 15.3.7 206) and the POSIX rename/fsync manual pages. The backoff is the usual
// "exponential with jitter" idea in our own words: wait base * 2^(n-1), capped, of which a random share (`jitter`)
// is drawn uniformly. No code from curl, wget or any library was used or consulted.
//
// No <mutex>, <thread> or <condition_variable> here (rule 21a): waits use nanosleep in short slices so a cancel ends
// them. Only exists where OwnHttpNet.hpp does (not Windows).
// ---------------------------------------------------------------------------
#include "okay/Platform/OwnHttpNet.hpp"
#include "okay/Crypto/Sha256.hpp"

#if !defined(_WIN32)
#include <cstdio>
#include <ctime>
#include <random>
#include <string>
#include <vector>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace okay::ownhttp {

// ----------------------------------------------------------------------------
// Retry policy
// ----------------------------------------------------------------------------
struct RetryPolicy {
    int maxAttempts = 1;          ///< total tries, the first one included (1 = no retry)
    int baseDelayMs = 500;        ///< wait before the second try; doubles for each further try
    int maxDelayMs = 30000;       ///< the doubled wait never goes above this
    double jitter = 0.5;          ///< share of each wait that is random (0 = fixed waits, 1 = anywhere from 0 to the full wait)
    bool honourRetryAfter = true; ///< a Retry-After on a 429 or 5xx answer sets the wait instead of the backoff
    int maxRetryAfterMs = 120000; ///< a Retry-After asking for longer than this ends the retries (the answer is returned)
    bool retryStatus = true;      ///< retry 408, 429, 500, 502, 503 and 504 answers
    bool retryNetwork = true;     ///< retry connect failures, timeouts, resets and connections closed early
    uint64_t seed = 0;            ///< 0 = seeded from the clock; tests set it for repeatable waits
    /// Test hook: called instead of sleeping (the wait in milliseconds). Empty = really wait (in 50 ms slices,
    /// ending early when the cancel check says so).
    std::function<void(int)> sleeper;
};

/// What a retrying call did, for logs and tests.
struct RetryReport {
    int attempts = 0;
    std::vector<int> delaysMs;    ///< every wait taken, in order
    bool gaveUpOnRetryAfter = false;   ///< the server asked for a wait longer than maxRetryAfterMs
};

inline bool IsIdempotentMethod(const std::string& m) {
    return m == "GET" || m == "HEAD" || m == "OPTIONS" || m == "TRACE" || m == "PUT" || m == "DELETE";
}

inline bool RetryableStatus(int s) { return s == 408 || s == 429 || s == 500 || s == 502 || s == 503 || s == 504; }

/// A failure that may go away on its own (network trouble), as opposed to one that will repeat (bad URL, refused
/// certificate, redirect loop, a proxy we cannot use, too large, a malformed answer).
inline bool TransientFailure(const FetchResult& r) {
    if (r.ok) return false;
    if (r.err == Err::Connect || r.err == Err::Timeout || r.err == Err::Io) return true;
    return r.err == Err::BadResponse && r.message.find("connection closed early") != std::string::npos;
}

namespace dl_detail {

/// Days from 1970-01-01 to the first day of `year`, counted year by year (the range we accept is small).
inline long long DaysBeforeYear(int year) {
    long long d = 0;
    for (int y = 1970; y < year; ++y) d += ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0) ? 366 : 365;
    return d;
}
inline bool Leap(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }
inline int MonthIndex(const std::string& m) {
    static const char* const kNames[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    for (int i = 0; i < 12; ++i) if (m == kNames[i]) return i;
    return -1;
}
inline bool Digits(const std::string& s, size_t a, size_t n, int& out) {
    if (a + n > s.size()) return false;
    out = 0;
    for (size_t i = a; i < a + n; ++i) { if (s[i] < '0' || s[i] > '9') return false; out = out * 10 + (s[i] - '0'); }
    return true;
}
/// Seconds since 1970 for a calendar time in UTC; false when a field is out of range.
inline bool ToEpoch(int year, int mon, int day, int hh, int mm, int ss, long long& out) {
    static const int kDays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (year < 1970 || year > 9999 || mon < 0 || mon > 11 || hh > 23 || mm > 59 || ss > 60) return false;
    const int dim = kDays[mon] + (mon == 1 && Leap(year) ? 1 : 0);
    if (day < 1 || day > dim) return false;
    long long days = DaysBeforeYear(year);
    for (int m = 0; m < mon; ++m) days += kDays[m] + (m == 1 && Leap(year) ? 1 : 0);
    days += day - 1;
    out = days * 86400 + hh * 3600 + mm * 60 + ss;
    return true;
}
/// "hh:mm:ss" at position a.
inline bool HmsAt(const std::string& s, size_t a, int& hh, int& mm, int& ss) {
    return a + 8 <= s.size() && s[a + 2] == ':' && s[a + 5] == ':' && Digits(s, a, 2, hh) && Digits(s, a + 3, 2, mm) && Digits(s, a + 6, 2, ss);
}

} // namespace dl_detail

/// An HTTP-date (RFC 9110 5.6.7) as seconds since 1970: the preferred IMF-fixdate ("Sun, 06 Nov 1994 08:49:37 GMT")
/// and the two obsolete forms ("Sunday, 06-Nov-94 08:49:37 GMT" and asctime "Sun Nov  6 08:49:37 1994").
/// For the two-digit year, a year more than 50 years in the future is taken to be in the past (RFC 9110).
inline bool ParseHttpDate(const std::string& in, long long& epoch, long long nowEpoch = 0) {
    using namespace dl_detail;
    const std::string s = detail::Trim(in);
    int day = 0, year = 0, hh = 0, mm = 0, ss = 0;
    const size_t comma = s.find(',');
    if (comma == 3) {   // IMF-fixdate: "Sun, 06 Nov 1994 08:49:37 GMT" (29 characters)
        if (s.size() != 29 || s[4] != ' ' || s[7] != ' ' || s[11] != ' ' || s[16] != ' ' || s[25] != ' ' || s.compare(26, 3, "GMT") != 0) return false;
        const int mon = MonthIndex(s.substr(8, 3));
        if (!Digits(s, 5, 2, day) || !Digits(s, 12, 4, year) || !HmsAt(s, 17, hh, mm, ss)) return false;
        return ToEpoch(year, mon, day, hh, mm, ss, epoch);
    }
    if (comma != std::string::npos && comma >= 6) {   // RFC 850: "Sunday, 06-Nov-94 08:49:37 GMT"
        const size_t a = comma + 2;
        if (s.size() != a + 22 || s[comma + 1] != ' ' || s[a + 2] != '-' || s[a + 6] != '-' || s[a + 9] != ' ' || s[a + 18] != ' ' || s.compare(a + 19, 3, "GMT") != 0) return false;
        const int mon = MonthIndex(s.substr(a + 3, 3));
        int yy = 0;
        if (!Digits(s, a, 2, day) || !Digits(s, a + 7, 2, yy) || !HmsAt(s, a + 10, hh, mm, ss)) return false;
        // Two-digit year: pick the century that puts it at most 50 years ahead of now.
        long long now = nowEpoch ? nowEpoch : (long long)std::time(nullptr);
        int nowYear = 1970; while (DaysBeforeYear(nowYear + 1) * 86400 <= now && nowYear < 9998) ++nowYear;
        year = (nowYear / 100) * 100 + yy;
        if (year > nowYear + 50) year -= 100;
        return ToEpoch(year, mon, day, hh, mm, ss, epoch);
    }
    if (comma == std::string::npos && s.size() == 24 && s[3] == ' ' && s[7] == ' ' && s[10] == ' ' && s[19] == ' ') {   // asctime
        const int mon = MonthIndex(s.substr(4, 3));
        const std::string dd = s[8] == ' ' ? "0" + s.substr(9, 1) : s.substr(8, 2);
        if (!Digits(dd, 0, 2, day) || !HmsAt(s, 11, hh, mm, ss) || !Digits(s, 20, 4, year)) return false;
        return ToEpoch(year, mon, day, hh, mm, ss, epoch);
    }
    return false;
}

/// Retry-After (RFC 9110 10.2.3): delay-seconds or an HTTP-date. The wait in milliseconds (0 for a date in the past);
/// -1 when the value is neither.
inline long long ParseRetryAfterMs(const std::string& v, long long nowEpoch = 0) {
    const std::string s = detail::Trim(v);
    if (s.empty()) return -1;
    bool digits = s.size() <= 10;
    for (char c : s) if (c < '0' || c > '9') digits = false;
    if (digits) { long long n = 0; for (char c : s) n = n * 10 + (c - '0'); return n * 1000; }
    long long when = 0;
    if (!ParseHttpDate(s, when, nowEpoch)) return -1;
    const long long now = nowEpoch ? nowEpoch : (long long)std::time(nullptr);
    return when > now ? (when - now) * 1000 : 0;
}

/// The wait before try number `nextAttempt` (2 = the first retry): base * 2^(nextAttempt - 2), capped at maxDelayMs,
/// then the share `jitter` of it drawn at random from [0, share).
inline int BackoffDelayMs(const RetryPolicy& p, int nextAttempt, std::mt19937_64& rng) {
    long long d = p.baseDelayMs > 0 ? p.baseDelayMs : 0;
    for (int i = 2; i < nextAttempt && d < p.maxDelayMs; ++i) d *= 2;
    if (d > p.maxDelayMs) d = p.maxDelayMs;
    double j = p.jitter < 0 ? 0 : (p.jitter > 1 ? 1 : p.jitter);
    const double fixed = double(d) * (1.0 - j);
    std::uniform_real_distribution<double> u(0.0, double(d) * j);
    const double w = fixed + (j > 0 ? u(rng) : 0.0);
    return int(w);
}

/// Wait `ms`, in 50 ms slices; false when `cancel` said stop.
inline bool WaitMs(int ms, const std::function<bool()>& cancel, const std::function<void(int)>& sleeper) {
    if (sleeper) { sleeper(ms); return !(cancel && cancel()); }
    for (int left = ms; left > 0;) {
        if (cancel && cancel()) return false;
        const int slice = left < 50 ? left : 50;
        struct timespec ts; ts.tv_sec = 0; ts.tv_nsec = long(slice) * 1000000L;
        while (::nanosleep(&ts, &ts) != 0 && errno == EINTR) {}
        left -= slice;
    }
    return !(cancel && cancel());
}

namespace dl_detail {
inline std::mt19937_64 MakeRng(uint64_t seed) {
    if (seed == 0) seed = uint64_t(std::chrono::steady_clock::now().time_since_epoch().count()) ^ 0x9e3779b97f4a7c15ull;
    return std::mt19937_64(seed);
}
/// The wait before the next try, or -1 when the answer says not to retry (Retry-After too long).
inline int NextWait(const RetryPolicy& p, int nextAttempt, const Response* answer, std::mt19937_64& rng, RetryReport* rep) {
    int wait = BackoffDelayMs(p, nextAttempt, rng);
    if (answer && p.honourRetryAfter) {
        if (const std::string* ra = answer->Get("retry-after")) {
            const long long ms = ParseRetryAfterMs(*ra);
            if (ms >= 0) {
                if (ms > p.maxRetryAfterMs) { if (rep) rep->gaveUpOnRetryAfter = true; return -1; }
                wait = int(ms);
            }
        }
    }
    return wait;
}
} // namespace dl_detail

/// Perform with retries, for bodies held in memory (no data hook: a failed try leaves nothing behind). Non-idempotent
/// methods get one try whatever the policy says. Returns the last try's result.
inline FetchResult PerformWithRetry(const std::string& method, const std::string& url,
                                    const std::vector<std::string>& headerLines, const std::string& body,
                                    const NetOptions& o, const RetryPolicy& p, RetryReport* rep = nullptr) {
    std::mt19937_64 rng = dl_detail::MakeRng(p.seed);
    const int maxAttempts = IsIdempotentMethod(method) ? (p.maxAttempts < 1 ? 1 : p.maxAttempts) : 1;
    FetchResult r;
    for (int attempt = 1;; ++attempt) {
        if (rep) rep->attempts = attempt;
        r = Perform(method, url, headerLines, body, o);
        if (attempt >= maxAttempts) return r;
        const bool again = r.ok ? (p.retryStatus && RetryableStatus(r.response.status)) : (p.retryNetwork && TransientFailure(r));
        if (!again) return r;
        const int wait = dl_detail::NextWait(p, attempt + 1, r.ok ? &r.response : nullptr, rng, rep);
        if (wait < 0) return r;
        if (rep) rep->delaysMs.push_back(wait);
        if (!WaitMs(wait, o.cancel, p.sleeper)) { r.ok = false; r.err = Err::Aborted; r.message = "cancelled while waiting to retry"; return r; }
    }
}

// ----------------------------------------------------------------------------
// Download to a file
// ----------------------------------------------------------------------------
enum class DownloadStatus {
    Ok,                ///< the whole file arrived, matched, and is at `dest`
    HttpError,         ///< a final answer other than 200/206 (after any retries); `httpStatus` says which
    ChecksumMismatch,  ///< the SHA-256 differs from `expectedSha256` (never retried; the temporary file is removed)
    SizeMismatch,      ///< more or fewer bytes than `expectedSize`
    Cancelled,         ///< the cancel check or the progress callback said stop
    TooLarge,          ///< more than `maxBytes`, announced or sent
    WriteFailed,       ///< the temporary file could not be created, written, synced or renamed (disk full, permissions)
    NetworkError,      ///< no usable answer (after any retries); `err` and `message` say why
    BadInput           ///< `expectedSha256` is not 64 hex digits, or no destination
};

inline const char* DownloadStatusName(DownloadStatus s) {
    switch (s) {
    case DownloadStatus::Ok: return "ok";
    case DownloadStatus::HttpError: return "http-error";
    case DownloadStatus::ChecksumMismatch: return "checksum-mismatch";
    case DownloadStatus::SizeMismatch: return "size-mismatch";
    case DownloadStatus::Cancelled: return "cancelled";
    case DownloadStatus::TooLarge: return "too-large";
    case DownloadStatus::WriteFailed: return "write-failed";
    case DownloadStatus::NetworkError: return "network-error";
    case DownloadStatus::BadInput: return "bad-input";
    }
    return "?";
}

struct DownloadSpec {
    std::vector<std::string> headers;   ///< extra "Name: value" lines (User-Agent, no-cache, ...); never Range or If-Range
    std::string expectedSha256;         ///< 64 hex digits (any case), or "" = not checked (the hash is still returned)
    uint64_t expectedSize = 0;          ///< 0 = not checked
    uint64_t maxBytes = 8ull << 30;     ///< size cap for the whole file (announced or sent); replaces NetOptions::limits.maxBodyBytes
    std::string tempPath;               ///< "" = dest + ".part"
    bool resume = true;                 ///< after a dropped connection, ask for the rest (Range) instead of starting over
    /// Start from a temporary file an earlier run left behind (its bytes are hashed again). Only used when the server
    /// can be held to the same file: with expectedSha256 set (a different file then fails the checksum) or validators.
    bool resumeExisting = false;
    bool keepPartialOnFailure = false;  ///< leave the temporary file after a network failure or cancel (never after a checksum or size mismatch)
    bool syncBeforeRename = true;       ///< fsync the file (and the folder after the rename) so a crash cannot leave a short file under the final name
    RetryPolicy retry;                  ///< maxAttempts counts every try; a resume is a try
    /// Bytes in the file so far (counting resumed bytes) and the complete size when known (0 when not). false = cancel.
    std::function<bool(uint64_t done, uint64_t total)> progress;
    std::function<bool()> cancel;       ///< polled while connecting, waiting, reading and between tries; true = stop
};

struct DownloadResult {
    DownloadStatus status = DownloadStatus::NetworkError;
    int httpStatus = 0;                 ///< the last answer's status (0 = none)
    uint64_t bytes = 0;                 ///< size of the finished file (or bytes written before the failure)
    std::string sha256Hex;              ///< lower-case hex of the finished file (Ok and ChecksumMismatch)
    Err err = Err::None;                ///< the client's error for NetworkError
    std::string message;
    int resumes = 0;                    ///< tries that continued from a byte offset with a 206
    int restarts = 0;                   ///< tries where the file had to start over (200 to a Range, a changed file)
    bool requestSent = false;
    RetryReport retry;
};

namespace dl_detail {
inline std::string Hex(const uint8_t* p, size_t n) {
    static const char* const k = "0123456789abcdef";
    std::string s; s.reserve(n * 2);
    for (size_t i = 0; i < n; ++i) { s += k[p[i] >> 4]; s += k[p[i] & 15]; }
    return s;
}
inline bool NormaliseHex(const std::string& in, std::string& out) {
    out.clear();
    if (in.size() != 64) return false;
    for (char c : in) {
        if (c >= '0' && c <= '9') out += c;
        else if (c >= 'a' && c <= 'f') out += c;
        else if (c >= 'A' && c <= 'F') out += char(c - 'A' + 'a');
        else return false;
    }
    return true;
}
inline bool WriteAll(int fd, const char* p, size_t n) {
    while (n > 0) {
        const ssize_t w = ::write(fd, p, n);
        if (w < 0) { if (errno == EINTR) continue; return false; }
        if (w == 0) return false;
        p += w; n -= size_t(w);
    }
    return true;
}
inline std::string ParentDir(const std::string& path) {
    const size_t s = path.find_last_of('/');
    if (s == std::string::npos) return ".";
    return s == 0 ? "/" : path.substr(0, s);
}
} // namespace dl_detail

/// GET `url` into `dest` (see the top of this file). `net` carries the timeouts, limits, proxy, CA and keep-alive
/// settings; its cancel is combined with spec.cancel.
inline DownloadResult DownloadToFile(const std::string& url, const std::string& dest, const DownloadSpec& spec,
                                     const NetOptions& net = NetOptions()) {
    DownloadResult out;
    std::string wantHex;
    if (dest.empty() || (!spec.expectedSha256.empty() && !dl_detail::NormaliseHex(spec.expectedSha256, wantHex))) {
        out.status = DownloadStatus::BadInput; out.message = dest.empty() ? "no destination" : "expectedSha256 is not 64 hex digits";
        return out;
    }
    for (const std::string& h : spec.headers) {
        if (detail::IPrefix(h, "range:") || detail::IPrefix(h, "if-range:")) {
            out.status = DownloadStatus::BadInput; out.message = "Range and If-Range are set by the download itself"; return out;
        }
    }
    const std::string temp = spec.tempPath.empty() ? dest + ".part" : spec.tempPath;
    const uint64_t cap = spec.maxBytes ? spec.maxBytes : (8ull << 30);

    // --- the temporary file and what we know about it
    okay::crypto::Sha256 hasher;
    uint64_t written = 0;
    int fd = -1;
    std::string validator;          // strong ETag or Last-Modified of the answer the bytes came from
    uint64_t knownTotal = 0;        // complete size, once an answer said so
    auto closeFd = [&]() { if (fd >= 0) { ::close(fd); fd = -1; } };
    auto removeTemp = [&]() { closeFd(); ::unlink(temp.c_str()); };
    auto startOver = [&]() -> bool {
        if (::ftruncate(fd, 0) != 0 || ::lseek(fd, 0, SEEK_SET) != 0) return false;
        hasher = okay::crypto::Sha256(); written = 0; validator.clear(); knownTotal = 0;
        return true;
    };
    auto fail = [&](DownloadStatus st, const std::string& m, bool mayKeep) {
        out.status = st; out.message = m; out.bytes = written;
        if (mayKeep && spec.keepPartialOnFailure && written > 0) closeFd(); else removeTemp();
        return out;
    };

    fd = ::open(temp.c_str(), O_WRONLY | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0644);
    if (fd < 0) return fail(DownloadStatus::WriteFailed, "cannot create the temporary file " + temp, false);
    {
        struct stat st{};
        const bool reuse = spec.resumeExisting && spec.resume && !wantHex.empty() && ::fstat(fd, &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0;
        if (reuse) {   // hash what an earlier run left, then append to it
            const int rfd = ::open(temp.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
            std::vector<char> buf(1 << 16);
            bool okRead = rfd >= 0;
            while (okRead) {
                const ssize_t n = ::read(rfd, buf.data(), buf.size());
                if (n < 0) { if (errno == EINTR) continue; okRead = false; break; }
                if (n == 0) break;
                hasher.Update(reinterpret_cast<const uint8_t*>(buf.data()), size_t(n));
                written += uint64_t(n);
            }
            if (rfd >= 0) ::close(rfd);
            if (!okRead || ::lseek(fd, off_t(written), SEEK_SET) != off_t(written)) {
                if (!startOver()) return fail(DownloadStatus::WriteFailed, "cannot reset the temporary file", false);
            }
        } else if (!startOver()) {
            return fail(DownloadStatus::WriteFailed, "cannot reset the temporary file", false);
        }
    }

    std::mt19937_64 rng = dl_detail::MakeRng(spec.retry.seed);
    const int maxAttempts = spec.retry.maxAttempts < 1 ? 1 : spec.retry.maxAttempts;
    bool cancelled = false;
    auto cancelNow = [&]() -> bool {
        if (cancelled) return true;
        if ((spec.cancel && spec.cancel()) || (net.cancel && net.cancel())) cancelled = true;
        return cancelled;
    };

    for (int attempt = 1;; ++attempt) {
        out.retry.attempts = attempt;
        if (cancelNow()) return fail(DownloadStatus::Cancelled, "cancelled", true);
        // A resume needs a way to be sure the rest belongs to the same file: a validator, or the expected checksum.
        const bool resuming = written > 0 && spec.resume && (!validator.empty() || !wantHex.empty());
        if (written > 0 && !resuming) { if (!startOver()) return fail(DownloadStatus::WriteFailed, "cannot reset the temporary file", false); ++out.restarts; }
        std::vector<std::string> hdrs = spec.headers;
        const uint64_t from = written;
        if (resuming) {
            hdrs.push_back("Range: bytes=" + std::to_string(from) + "-");
            if (!validator.empty()) hdrs.push_back("If-Range: " + validator);
        }

        enum class Stop { None, Write, TooBig, SizeMismatch, Changed } stop = Stop::None;
        bool writing = false;
        int headStatus = 0;
        NetOptions o = net;
        o.limits.maxBodyBytes = cap;
        o.acceptGzip = false;   // a file is stored as sent
        o.cancel = cancelNow;
        FetchHooks hooks;
        hooks.onHead = [&](const Response& h) -> bool {
            headStatus = h.status;
            writing = false;
            if (h.status == 200) {
                if (from > 0) { if (!startOver()) { stop = Stop::Write; return false; } ++out.restarts; }
                if (const std::string* cl = h.Get("content-length")) {
                    uint64_t n = 0; bool d = !cl->empty() && cl->size() < 19;
                    for (unsigned char c : *cl) { if (c < '0' || c > '9') d = false; else n = n * 10 + (c - '0'); }
                    if (d) knownTotal = n;
                }
            } else if (h.status == 206) {   // Fetch has already checked that it starts at `from`
                uint64_t a = 0, b = 0, t = 0;
                const std::string* cr = h.Get("content-range");
                if (!cr || !ParseContentRange(*cr, a, b, t) || a != from) { stop = Stop::Changed; return false; }
                if (t != 0 && knownTotal != 0 && t != knownTotal) { stop = Stop::Changed; return false; }
                if (t != 0) knownTotal = t;
                ++out.resumes;
            } else {
                return true;   // an error or retry answer: its body is read and dropped
            }
            if (cap && knownTotal > cap) { stop = Stop::TooBig; return false; }
            if (spec.expectedSize && knownTotal && knownTotal != spec.expectedSize) { stop = Stop::SizeMismatch; return false; }
            if (h.status == 200 || validator.empty()) {
                validator.clear();
                if (const std::string* et = h.Get("etag")) { if (!detail::IPrefix(*et, "W/")) validator = detail::Trim(*et); }
                if (validator.empty()) if (const std::string* lm = h.Get("last-modified")) validator = detail::Trim(*lm);
            }
            writing = true;
            return true;
        };
        hooks.onData = [&](const char* p, size_t n) -> bool {
            if (!writing) return true;
            if (cap && written + n > cap) { stop = Stop::TooBig; return false; }
            if (spec.expectedSize && written + n > spec.expectedSize) { stop = Stop::SizeMismatch; return false; }
            if (!dl_detail::WriteAll(fd, p, n)) { stop = Stop::Write; return false; }
            hasher.Update(reinterpret_cast<const uint8_t*>(p), n);
            written += n;
            if (spec.progress && !spec.progress(written, knownTotal)) { cancelled = true; return false; }
            return !cancelNow();
        };
        const FetchResult r = Perform("GET", url, hdrs, std::string(), o, hooks);
        out.requestSent = out.requestSent || r.requestSent;
        out.httpStatus = headStatus ? headStatus : (r.ok ? r.response.status : out.httpStatus);
        if (stop == Stop::Write) return fail(DownloadStatus::WriteFailed, "cannot write the temporary file " + temp, false);
        if (stop == Stop::TooBig) return fail(DownloadStatus::TooLarge, "more data than the size limit", false);
        if (stop == Stop::SizeMismatch) return fail(DownloadStatus::SizeMismatch, "the file is not the expected size", false);
        if (cancelled) return fail(DownloadStatus::Cancelled, "cancelled", true);
        if (!r.ok && r.err == Err::TooLarge) return fail(DownloadStatus::TooLarge, r.message, false);

        Response const* answer = nullptr;
        if (stop == Stop::Changed || (r.ok && r.response.status == 416 && from > 0)) {
            // The file on the server changed under us, or our offset is past its end: start over on the next try.
            if (!startOver()) return fail(DownloadStatus::WriteFailed, "cannot reset the temporary file", false);
            ++out.restarts;
            if (attempt >= maxAttempts) return fail(DownloadStatus::NetworkError, "the file changed on the server while it was downloaded", false);
        } else if (r.ok && (r.response.status == 200 || r.response.status == 206)) {
            // --- complete: check, sync, rename
            uint8_t digest[32];
            hasher.Final(digest);
            out.sha256Hex = dl_detail::Hex(digest, 32);
            out.bytes = written;
            if (knownTotal && written != knownTotal) return fail(DownloadStatus::SizeMismatch, "the answer ended before the announced size", false);
            if (spec.expectedSize && written != spec.expectedSize) return fail(DownloadStatus::SizeMismatch, "the file is not the expected size", false);
            if (!wantHex.empty() && out.sha256Hex != wantHex) return fail(DownloadStatus::ChecksumMismatch, "SHA-256 does not match", false);
            if (spec.syncBeforeRename && ::fsync(fd) != 0) return fail(DownloadStatus::WriteFailed, "cannot sync the temporary file", false);
            const int closed = ::close(fd);
            fd = -1;
            if (closed != 0) return fail(DownloadStatus::WriteFailed, "cannot close the temporary file", false);
            if (::rename(temp.c_str(), dest.c_str()) != 0) return fail(DownloadStatus::WriteFailed, "cannot move the file into place", false);
            if (spec.syncBeforeRename) {   // the folder entry too, best effort
                const int dfd = ::open(dl_detail::ParentDir(dest).c_str(), O_RDONLY | O_CLOEXEC);
                if (dfd >= 0) { (void)::fsync(dfd); ::close(dfd); }
            }
            out.status = DownloadStatus::Ok;
            out.err = Err::None;
            return out;
        } else if (r.ok) {
            out.httpStatus = r.response.status;
            answer = &r.response;
            if (!(spec.retry.retryStatus && RetryableStatus(r.response.status)) || attempt >= maxAttempts)
                return fail(DownloadStatus::HttpError, "HTTP " + std::to_string(r.response.status), false);
        } else {
            out.err = r.err;
            if (!(spec.retry.retryNetwork && TransientFailure(r)) || attempt >= maxAttempts)
                return fail(DownloadStatus::NetworkError, r.message, true);
        }
        const int wait = dl_detail::NextWait(spec.retry, attempt + 1, answer, rng, &out.retry);
        if (wait < 0) return fail(DownloadStatus::HttpError, "the server asked to wait longer than allowed", false);
        out.retry.delaysMs.push_back(wait);
        if (!WaitMs(wait, cancelNow, spec.retry.sleeper)) return fail(DownloadStatus::Cancelled, "cancelled", true);
    }
}

} // namespace okay::ownhttp
#endif // !_WIN32
