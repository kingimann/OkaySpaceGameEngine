// The only file in the test suite that turns the opt-in heap hooks on. It replaces
// the global operator new/delete for this one executable.
#define OKAY_ENABLE_MEMORY_HOOKS
#include "okay/Debug/MemoryHooks.hpp"

#include "test_framework.hpp"

#include <memory>
#include <new>
#include <string>
#include <vector>
#ifndef _WIN32
#include <thread>
#endif

using namespace okay;

// Keep the optimiser from removing allocations the test wants to see.
static thread_local void* volatile g_sink = nullptr;

struct alignas(64) Wide { char bytes[64]; };

int main() {
    RUN_SUITE("debug_memory_hooks");

    CHECK(MemoryHooks::Active());

    // plain new / delete
    {
        HeapStats a = MemoryHooks::Read();
        int* p = new int[100];
        g_sink = p;
        HeapStats b = MemoryHooks::Read();
        CHECK(b.allocations == a.allocations + 1);
        CHECK(b.liveBytes == a.liveBytes + 400);
        CHECK(b.totalBytes == a.totalBytes + 400);
        CHECK(b.peakBytes >= b.liveBytes);
        delete[] p;
        HeapStats c = MemoryHooks::Read();
        CHECK(c.frees == a.frees + 1);
        CHECK(c.liveBytes == a.liveBytes);
        HeapStats d = MemoryHooks::Delta(a, c);
        CHECK(d.allocations == 1 && d.frees == 1 && d.totalBytes == 400);
        CHECK(c.LiveAllocations() == a.LiveAllocations());
    }

    // single objects, nothrow, sized delete
    {
        HeapStats a = MemoryHooks::Read();
        auto* s = new std::string(200, 'x');
        auto* n = new (std::nothrow) double(1.5);
        g_sink = n; g_sink = s;
        HeapStats b = MemoryHooks::Read();
        CHECK(b.allocations >= a.allocations + 3);                    // the string object, its text and the double
        delete s;
        delete n;
        HeapStats c = MemoryHooks::Read();
        CHECK(c.liveBytes == a.liveBytes && c.LiveAllocations() == a.LiveAllocations());
    }

    // over-aligned types get properly aligned blocks and balanced counters
    {
        HeapStats a = MemoryHooks::Read();
        Wide* w = new Wide;
        g_sink = w;
        CHECK(reinterpret_cast<std::uintptr_t>(w) % 64 == 0);
        Wide* arr = new Wide[3];
        g_sink = arr;
        CHECK(reinterpret_cast<std::uintptr_t>(arr) % 64 == 0);
        CHECK(MemoryHooks::Read().liveBytes >= a.liveBytes + 64 + 3 * 64);
        delete w;
        delete[] arr;
        CHECK(MemoryHooks::Read().liveBytes == a.liveBytes);
    }

    // containers, peaks and the tracker bridge
    {
        MemoryHooks::ResetPeak();
        HeapStats a = MemoryHooks::Read();
        {
            std::vector<int> v(10000);
            g_sink = v.data();
            CHECK(MemoryHooks::Read().liveBytes >= a.liveBytes + 40000);
            MemoryTracker t;
            MemoryHooks::SyncTo(t, MemoryTracker::Other);
            CHECK(t.Bytes(MemoryTracker::Other) == (std::int64_t)MemoryHooks::Read().liveBytes);
            CHECK(t.Count(MemoryTracker::Other) >= 1);
        }
        HeapStats b = MemoryHooks::Read();
        CHECK(b.liveBytes == a.liveBytes);
        CHECK(b.peakBytes >= a.liveBytes + 40000);                    // the vector's peak is remembered
        std::string text = MemoryHooks::ToText();
        CHECK(text.find("hooks on") != std::string::npos);
    }

    // a huge request fails cleanly through bad_alloc (or the nothrow form returns null);
    // skipped under TSan/ASan, whose own allocators abort on absurd sizes instead of returning null
#if !defined(__SANITIZE_THREAD__) && !defined(__SANITIZE_ADDRESS__)
    {
        bool threw = false;
        try { char* big = new char[static_cast<std::size_t>(-1) / 4]; g_sink = big; delete[] big; }
        catch (const std::bad_alloc&) { threw = true; }
        CHECK(threw);
        CHECK(::operator new(static_cast<std::size_t>(-1) / 4, std::nothrow) == nullptr);
    }
#endif

#ifndef _WIN32
    // thread safety smoke test: counters from several threads stay balanced
    {
        HeapStats a = MemoryHooks::Read();
        std::vector<std::thread> th;
        for (int t = 0; t < 4; ++t)
            th.emplace_back([] {
                for (int i = 0; i < 2000; ++i) {
                    int* p = new int[16];
                    g_sink = p;
                    delete[] p;
                }
            });
        for (auto& x : th) x.join();
        HeapStats b = MemoryHooks::Read();
        // thread bookkeeping itself allocates, so compare the live figures with a margin
        CHECK(b.allocations >= a.allocations + 8000);
        CHECK(b.LiveAllocations() <= a.LiveAllocations() + 16);
    }
#endif

    TEST_MAIN_RESULT();
}
