#pragma once
// Opt-in whole-process heap counters (feature map 23.2). OFF BY DEFAULT.
//
// Including this header is harmless: it only declares the counters and the
// reading API, and the engine never defines the macro below. To count every
// `new` and `delete` in a program, put this in exactly ONE .cpp file of the final
// executable (for example its main.cpp), before the include:
//
//     #define OKAY_ENABLE_MEMORY_HOOKS
//     #include "okay/Debug/MemoryHooks.hpp"
//
// That replaces the global operator new/delete (all forms, including aligned and
// nothrow) with versions that count calls and live bytes, then forward to
// malloc/free. Each block carries a 16 byte header, so the program uses a little
// more memory while the hooks are on; do not ship builds with them. Defining the
// macro in two files is a link error (duplicate operator new), which is what you
// want: it cannot be half enabled.
//
// Only operator new/delete are seen: malloc/free called directly, memory a
// library gets from the OS itself and GPU memory are not. Use MemoryTracker for
// those (and for per-system numbers); MemoryHooks::SyncTo feeds the heap total
// into one tracker category.
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <string>

#include "okay/Debug/MemoryTracker.hpp"

namespace okay {

/// Heap counters since program start (or the last Reset). Relaxed atomics: the
/// numbers are exact in total but a reader may see them a moment apart.
struct HeapStats {
    std::uint64_t allocations = 0;       // operator new calls that succeeded
    std::uint64_t frees = 0;             // operator delete calls on a real block
    std::uint64_t totalBytes = 0;        // bytes requested, ever
    std::uint64_t liveBytes = 0;
    std::uint64_t peakBytes = 0;
    std::uint64_t LiveAllocations() const { return allocations >= frees ? allocations - frees : 0; }
};

class MemoryHooks {
public:
    /// True when the replacement operators are linked in (the macro was defined in some file).
    static bool Active() { return Cnt().active.load(std::memory_order_relaxed); }

    static HeapStats Read() {
        HeapStats h;
        Counters& c = Cnt();
        h.allocations = c.allocations.load(std::memory_order_relaxed);
        h.frees       = c.frees.load(std::memory_order_relaxed);
        h.totalBytes  = c.totalBytes.load(std::memory_order_relaxed);
        h.liveBytes   = c.liveBytes.load(std::memory_order_relaxed);
        h.peakBytes   = c.peakBytes.load(std::memory_order_relaxed);
        return h;
    }
    /// What happened between two reads (peak is the later one's).
    static HeapStats Delta(const HeapStats& before, const HeapStats& after) {
        HeapStats d;
        d.allocations = after.allocations - before.allocations;
        d.frees       = after.frees - before.frees;
        d.totalBytes  = after.totalBytes - before.totalBytes;
        d.liveBytes   = after.liveBytes;   // an absolute figure, not a difference
        d.peakBytes   = after.peakBytes;
        return d;
    }
    /// Start the peak over from the current live size.
    static void ResetPeak() {
        Counters& c = Cnt();
        c.peakBytes.store(c.liveBytes.load(std::memory_order_relaxed), std::memory_order_relaxed);
    }
    /// Put the live heap into one MemoryTracker category (Set: replaces its figures).
    static void SyncTo(MemoryTracker& tracker, int category) {
        HeapStats h = Read();
        tracker.Set(category, (std::int64_t)h.liveBytes, (std::int64_t)h.LiveAllocations());
    }
    static std::string ToText() {
        HeapStats h = Read();
        char buf[200];
        std::snprintf(buf, sizeof(buf), "Heap (%s): %llu allocs, %llu frees, %llu live blocks, live %s, peak %s\n",
                      Active() ? "hooks on" : "hooks OFF",
                      (unsigned long long)h.allocations, (unsigned long long)h.frees,
                      (unsigned long long)h.LiveAllocations(),
                      debugutil::FormatBytes((long long)h.liveBytes).c_str(), debugutil::FormatBytes((long long)h.peakBytes).c_str());
        return buf;
    }

    // Called by the replacement operators (below); public so a custom allocator
    // can report into the same counters.
    static void NoteAlloc(std::size_t bytes) {
        Counters& c = Cnt();
        c.allocations.fetch_add(1, std::memory_order_relaxed);
        c.totalBytes.fetch_add(bytes, std::memory_order_relaxed);
        std::uint64_t live = c.liveBytes.fetch_add(bytes, std::memory_order_relaxed) + bytes;
        std::uint64_t peak = c.peakBytes.load(std::memory_order_relaxed);
        while (live > peak && !c.peakBytes.compare_exchange_weak(peak, live, std::memory_order_relaxed)) {}
    }
    static void NoteFree(std::size_t bytes) {
        Counters& c = Cnt();
        c.frees.fetch_add(1, std::memory_order_relaxed);
        c.liveBytes.fetch_sub(bytes, std::memory_order_relaxed);
    }
    static void MarkActive() { Cnt().active.store(true, std::memory_order_relaxed); }

private:
    struct Counters {
        std::atomic<std::uint64_t> allocations{0}, frees{0}, totalBytes{0}, liveBytes{0}, peakBytes{0};
        std::atomic<bool> active{false};
    };
    static Counters& Cnt() { static Counters c; return c; }
};

} // namespace okay

// ---------------------------------------------------------------------------
// The replacement operators: only when the program asked for them.
// ---------------------------------------------------------------------------
#ifdef OKAY_ENABLE_MEMORY_HOOKS

namespace okay {
namespace memhooks_detail {

struct BlockHeader { std::size_t size; void* raw; };

inline void* Allocate(std::size_t size, std::size_t align) noexcept {
    if (align < alignof(std::max_align_t)) align = alignof(std::max_align_t);
    std::size_t total = size + align + sizeof(BlockHeader);
    if (total < size) return nullptr;                       // overflow
    void* raw = std::malloc(total);
    if (!raw) return nullptr;
    std::uintptr_t u = reinterpret_cast<std::uintptr_t>(raw) + sizeof(BlockHeader);
    u = (u + (align - 1)) & ~(static_cast<std::uintptr_t>(align) - 1);
    BlockHeader* h = reinterpret_cast<BlockHeader*>(u) - 1;
    h->size = size;
    h->raw = raw;
    MemoryHooks::NoteAlloc(size);
    return reinterpret_cast<void*>(u);
}

inline void Release(void* p) noexcept {
    if (!p) return;
    BlockHeader* h = reinterpret_cast<BlockHeader*>(p) - 1;
    MemoryHooks::NoteFree(h->size);
    std::free(h->raw);
}

inline void* AllocateOrThrow(std::size_t size, std::size_t align) {
    for (;;) {
        if (void* p = Allocate(size, align)) return p;
        std::new_handler h = std::get_new_handler();
        if (!h) throw std::bad_alloc();
        h();
    }
}

inline bool Register() { MemoryHooks::MarkActive(); return true; }
[[maybe_unused]] static const bool s_registered = Register();

} // namespace memhooks_detail
} // namespace okay

// Replaceable functions must not be inline: these are the one definition.
void* operator new(std::size_t n) { return okay::memhooks_detail::AllocateOrThrow(n, 0); }
void* operator new[](std::size_t n) { return okay::memhooks_detail::AllocateOrThrow(n, 0); }
void* operator new(std::size_t n, const std::nothrow_t&) noexcept { return okay::memhooks_detail::Allocate(n, 0); }
void* operator new[](std::size_t n, const std::nothrow_t&) noexcept { return okay::memhooks_detail::Allocate(n, 0); }
void operator delete(void* p) noexcept { okay::memhooks_detail::Release(p); }
void operator delete[](void* p) noexcept { okay::memhooks_detail::Release(p); }
void operator delete(void* p, std::size_t) noexcept { okay::memhooks_detail::Release(p); }
void operator delete[](void* p, std::size_t) noexcept { okay::memhooks_detail::Release(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { okay::memhooks_detail::Release(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { okay::memhooks_detail::Release(p); }

#if defined(__cpp_aligned_new)
void* operator new(std::size_t n, std::align_val_t a) { return okay::memhooks_detail::AllocateOrThrow(n, (std::size_t)a); }
void* operator new[](std::size_t n, std::align_val_t a) { return okay::memhooks_detail::AllocateOrThrow(n, (std::size_t)a); }
void* operator new(std::size_t n, std::align_val_t a, const std::nothrow_t&) noexcept { return okay::memhooks_detail::Allocate(n, (std::size_t)a); }
void* operator new[](std::size_t n, std::align_val_t a, const std::nothrow_t&) noexcept { return okay::memhooks_detail::Allocate(n, (std::size_t)a); }
void operator delete(void* p, std::align_val_t) noexcept { okay::memhooks_detail::Release(p); }
void operator delete[](void* p, std::align_val_t) noexcept { okay::memhooks_detail::Release(p); }
void operator delete(void* p, std::size_t, std::align_val_t) noexcept { okay::memhooks_detail::Release(p); }
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept { okay::memhooks_detail::Release(p); }
void operator delete(void* p, std::align_val_t, const std::nothrow_t&) noexcept { okay::memhooks_detail::Release(p); }
void operator delete[](void* p, std::align_val_t, const std::nothrow_t&) noexcept { okay::memhooks_detail::Release(p); }
#endif

#endif // OKAY_ENABLE_MEMORY_HOOKS
