#pragma once
// Memory and time budgets for the reader tests: a counting global allocator (the bytes live now and the peak since the
// last reset) and a Measure() that times one read, prints it and checks it against a budget. Written for this engine.
//
// Include this in exactly ONE source file of a test program: it replaces the global operator new and delete (every
// form, the nothrow ones too, because std::stable_sort takes its buffer that way). Each allocation carries a 16-byte
// header with its size, so a free knows how much to take off the live count.
#include "test_framework.hpp"
#include "reader_hostile_common.hpp"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <string>

inline std::atomic<std::size_t> g_live{0}, g_peak{0};

inline void* CountedAlloc(std::size_t n) {
    void* raw = std::malloc(n + 16);
    if (!raw) throw std::bad_alloc();
    *static_cast<std::size_t*>(raw) = n;
    const std::size_t now = g_live.fetch_add(n) + n;
    std::size_t p = g_peak.load();
    while (now > p && !g_peak.compare_exchange_weak(p, now)) {}
    return static_cast<char*>(raw) + 16;
}
inline void CountedFree(void* q) noexcept {
    if (!q) return;
    void* raw = static_cast<char*>(q) - 16;
    g_live.fetch_sub(*static_cast<std::size_t*>(raw));
    std::free(raw);
}
void* operator new(std::size_t n) { return CountedAlloc(n); }
void* operator new[](std::size_t n) { return CountedAlloc(n); }
void* operator new(std::size_t n, const std::nothrow_t&) noexcept { try { return CountedAlloc(n); } catch (...) { return nullptr; } }
void* operator new[](std::size_t n, const std::nothrow_t&) noexcept { try { return CountedAlloc(n); } catch (...) { return nullptr; } }
void operator delete(void* p, const std::nothrow_t&) noexcept { CountedFree(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { CountedFree(p); }
void operator delete(void* p) noexcept { CountedFree(p); }
void operator delete[](void* p) noexcept { CountedFree(p); }
void operator delete(void* p, std::size_t) noexcept { CountedFree(p); }
void operator delete[](void* p, std::size_t) noexcept { CountedFree(p); }

namespace budget {

/// True in a sanitizer build, which is several times slower and bigger: time limits are set wider there.
#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_UNDEFINED__)
inline constexpr bool kSanitizerBuild = true;
#else
inline constexpr bool kSanitizerBuild = false;
#endif

inline double Now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
/// Start a peak measurement: the peak is set to what is live now, which is returned as the base.
inline std::size_t PeakReset() { const std::size_t base = g_live.load(); g_peak.store(base); return base; }
/// The most bytes held at once on top of `base` since PeakReset().
inline std::size_t PeakSince(std::size_t base) { return g_peak.load() - base; }

struct Run { bool ok; double secs; std::size_t peakMB; std::string err; okay::ImportedModel m; };

/// Time one read, `read(model, err)`, and measure the most memory it holds at once. Prints one line (`note` goes after
/// the peak), then checks: under `maxSeconds`, under `capBytes`, and either a sane model or a refusal with a reason that
/// leaves nothing behind.
template <class F>
Run Measure(const char* what, F&& read, std::size_t capBytes, double maxSeconds, const std::string& note = std::string()) {
    Run r;
    const std::size_t base = PeakReset();
    const double t = Now();
    r.ok = read(r.m, r.err);
    r.secs = Now() - t;
    const std::size_t extra = PeakSince(base);
    r.peakMB = extra >> 20;
    std::fprintf(stderr, "  %s: %s in %.2f s, peak %zu MB%s%s%s\n", what, r.ok ? "read" : "refused", r.secs, r.peakMB, note.c_str(), r.ok ? "" : ": ", r.ok ? "" : r.err.c_str());
    CHECK(r.secs < maxSeconds);
    CHECK(extra < capBytes);
    CHECK(r.ok ? hostile::Sane(r.m) : (r.m.parts.empty() && r.m.nodes.empty() && !r.err.empty()));
    return r;
}

} // namespace budget
