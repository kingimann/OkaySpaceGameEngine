#pragma once
// Small shared helpers for the profiling and debugging data layers in okay/Debug.
// Header-only, no <mutex>/<thread> (they do not work with MinGW's "win32" thread
// model and these headers are included widely): the lock is a spin lock on
// std::atomic, the same approach Log.hpp takes.
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace okay {
namespace debugutil {

/// A tiny test-and-test-and-set lock. For short critical sections only (copying a
/// few hundred small records); never held across a call into user code or Log.
class SpinLock {
public:
    void lock() {
        for (;;) {
            if (!m_locked.exchange(true, std::memory_order_acquire)) return;
            while (m_locked.load(std::memory_order_relaxed)) {}
        }
    }
    void unlock() { m_locked.store(false, std::memory_order_release); }
private:
    std::atomic<bool> m_locked{false};
};

class SpinGuard {
public:
    explicit SpinGuard(SpinLock& l) : m_l(l) { m_l.lock(); }
    ~SpinGuard() { m_l.unlock(); }
    SpinGuard(const SpinGuard&) = delete;
    SpinGuard& operator=(const SpinGuard&) = delete;
private:
    SpinLock& m_l;
};

/// Append `s` as a quoted, escaped JSON string.
inline void AppendJsonString(std::string& out, const std::string& s) {
    out += '"';
    for (unsigned char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) { char b[8]; std::snprintf(b, sizeof(b), "\\u%04x", c); out += b; }
                else out += (char)c;
        }
    }
    out += '"';
}

/// Append a number with `decimals` places; NaN and infinity (not valid JSON) become 0.
inline void AppendNum(std::string& out, double v, int decimals = 3) {
    if (!std::isfinite(v)) v = 0.0;
    char b[64];
    std::snprintf(b, sizeof(b), "%.*f", decimals, v);
    out += b;
}
inline void AppendInt(std::string& out, long long v) { out += std::to_string(v); }
inline void AppendUInt(std::string& out, unsigned long long v) { out += std::to_string(v); }

/// "1.50 MB", "812 B", "-3.00 KB" (binary units).
inline std::string FormatBytes(long long bytes) {
    const char* unit[] = {"B", "KB", "MB", "GB", "TB"};
    bool neg = bytes < 0;
    double v = neg ? -(double)bytes : (double)bytes;
    int u = 0;
    while (v >= 1024.0 && u < 4) { v /= 1024.0; ++u; }
    char b[48];
    if (u == 0) std::snprintf(b, sizeof(b), "%s%lld %s", neg ? "-" : "", (long long)v, unit[u]);
    else        std::snprintf(b, sizeof(b), "%s%.2f %s", neg ? "-" : "", v, unit[u]);
    return b;
}

inline char LowerAscii(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }

inline std::string ToLowerAscii(const std::string& s) {
    std::string r = s;
    for (char& c : r) c = LowerAscii(c);
    return r;
}

/// True if `hay` contains `needle`. An empty needle always matches.
inline bool Contains(const std::string& hay, const std::string& needle, bool caseSensitive) {
    if (needle.empty()) return true;
    if (caseSensitive) return hay.find(needle) != std::string::npos;
    return ToLowerAscii(hay).find(ToLowerAscii(needle)) != std::string::npos;
}

/// Percentile of an ascending-sorted series by linear interpolation between the
/// two nearest ranks (p in 0..100). Empty -> 0.
inline double PercentileSorted(const std::vector<double>& sorted, double p) {
    if (sorted.empty()) return 0.0;
    if (sorted.size() == 1) return sorted[0];
    if (p <= 0.0) return sorted.front();
    if (p >= 100.0) return sorted.back();
    double rank = p / 100.0 * (double)(sorted.size() - 1);
    std::size_t lo = (std::size_t)rank;
    double frac = rank - (double)lo;
    if (lo + 1 >= sorted.size()) return sorted.back();
    return sorted[lo] + (sorted[lo + 1] - sorted[lo]) * frac;
}

inline bool WriteTextFile(const std::string& path, const std::string& text) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(text.data(), (std::streamsize)text.size());
    return (bool)f;
}

} // namespace debugutil
} // namespace okay
