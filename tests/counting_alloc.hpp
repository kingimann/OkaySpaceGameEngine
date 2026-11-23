// A counting allocator for the hostile-input tests: it replaces operator new and delete so a test can read the bytes live now and the peak
// since the last reset (g_live, g_peak). Include it ONCE per test program (these are the global replacement functions).
#pragma once
#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <new>

// ---- a counting allocator: bytes live now and the peak since the last reset ----------------------------------------------
static std::atomic<std::size_t> g_live{0}, g_peak{0};
static void* CountedAlloc(std::size_t n) {
    void* raw = std::malloc(n + 16);
    if (!raw) throw std::bad_alloc();
    *static_cast<std::size_t*>(raw) = n;
    const std::size_t now = g_live.fetch_add(n) + n;
    std::size_t p = g_peak.load();
    while (now > p && !g_peak.compare_exchange_weak(p, now)) {}
    return static_cast<char*>(raw) + 16;
}
static void CountedFree(void* q) noexcept {
    if (!q) return;
    void* raw = static_cast<char*>(q) - 16;
    g_live.fetch_sub(*static_cast<std::size_t*>(raw));
    std::free(raw);
}
void* operator new(std::size_t n) { return CountedAlloc(n); }
void* operator new[](std::size_t n) { return CountedAlloc(n); }
void* operator new(std::size_t n, const std::nothrow_t&) noexcept { try { return CountedAlloc(n); } catch (...) { return nullptr; } }   // std::stable_sort takes its buffer this way
void* operator new[](std::size_t n, const std::nothrow_t&) noexcept { try { return CountedAlloc(n); } catch (...) { return nullptr; } }
void operator delete(void* p, const std::nothrow_t&) noexcept { CountedFree(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { CountedFree(p); }
void operator delete(void* p) noexcept { CountedFree(p); }
void operator delete[](void* p) noexcept { CountedFree(p); }
void operator delete(void* p, std::size_t) noexcept { CountedFree(p); }
void operator delete[](void* p, std::size_t) noexcept { CountedFree(p); }
