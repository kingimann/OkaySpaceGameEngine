// StackAllocator and the opt-in memory tracking layer (MemoryTrack.hpp): tags, peak, fragmentation,
// leak report, and that tracking off costs nothing and changes nothing.
#include "test_framework.hpp"
#include "okay/Core/Memory.hpp"

#include <cstring>
#include <string>

using namespace okay;

namespace {
struct Thing { int a = 1; char pad[28]; };

// Returns a copy: a pointer into a temporary snapshot would dangle.
struct Found {
    bool ok = false;
    MemTrack::TagReport r;
    explicit operator bool() const { return ok; }
    const MemTrack::TagReport* operator->() const { return &r; }
};
Found Find(const std::vector<MemTrack::TagReport>& v, const char* tag) {
    for (const auto& t : v) if (t.tag == tag) { Found f; f.ok = true; f.r = t; return f; }
    return Found{};
}

void TestStack() {
    RUN_SUITE("stack allocator");
    StackAllocator st(256);
    void* a = st.Allocate(10);
    void* b = st.Allocate(20, 64);
    CHECK(a && b);
    CHECK(reinterpret_cast<std::uintptr_t>(b) % 64 == 0);
    CHECK(st.Count() == 2);
    CHECK(!st.Free(a));            // not the newest
    CHECK(st.Count() == 2);
    CHECK(st.Free(b));
    CHECK(st.Free(a));
    CHECK(st.Used() == 0);
    CHECK(st.Allocate(1000) == nullptr);
    CHECK(st.Stats().failures == 1);
    CHECK(st.Allocate(8, 3) == nullptr);   // bad alignment
    std::memset(st.Allocate(256, 1), 7, 256);
    CHECK(st.Allocate(1) == nullptr);      // full
    st.Clear();
    CHECK(st.Used() == 0 && st.Count() == 0);
    CHECK(st.Stats().highWater == 256);
}

void TestOffByDefault() {
    RUN_SUITE("tracking off");
    MemTrack::Reset();
    MemTrack::Enable(false);
    FrameArena a; a.SetTag("off.arena");
    a.Allocate(100);
    CHECK(MemTrack::Snapshot().empty());
    CHECK(MemTrack::LeakReport().empty());
    CHECK(MemTrack::Report().find("off") != std::string::npos);
}

void TestTracking() {
    RUN_SUITE("tracking on");
    MemTrack::Reset();
    MemTrack::Enable(true);
    MemTrack::ReportAtExit(false);   // the test prints its own
    {
        FrameArena arena(1024); arena.SetTag("t.arena");
        arena.Allocate(300); arena.Allocate(200);
        auto snap = MemTrack::Snapshot();
        auto t = Find(snap, "t.arena");
        CHECK(t && t->allocators == 1 && t->usedBytes >= 500 && t->reservedBytes >= 1024);
        CHECK(t && t->fragmentation > 0.0 && t->fragmentation < 1.0);
        arena.Reset();
        t = Find(MemTrack::Snapshot(), "t.arena");
        CHECK(t && t->usedBytes == 0 && t->peakBytes >= 500);
        CHECK(MemTrack::LeakReport().empty());   // an arena is never a leak
        // a move keeps the tag
        FrameArena moved(std::move(arena));
        arena.Reset();
        t = Find(MemTrack::Snapshot(), "t.arena");
        CHECK(t && t->allocators == 1);
    }
    auto gone = Find(MemTrack::Snapshot(), "t.arena");
    CHECK(gone && gone->allocators == 0 && gone->peakBytes >= 500);   // history stays after the allocator died
    {
        PoolAllocator<Thing> pool(8); pool.SetTag("t.pool");
        Thing* a = pool.Create(); Thing* b = pool.Create(); (void)b;
        pool.Destroy(a);
        auto t = Find(MemTrack::Snapshot(), "t.pool");
        CHECK(t && t->kind == MemTrack::Kind::Pool && t->usedBytes == sizeof(Thing));
        CHECK(t && t->fragmentation > 0.5);   // 1 of 8 slots in use
        CHECK(MemTrack::LeakReport().find("t.pool") != std::string::npos);   // b is alive: reported while it lives too
    }   // destroyed with b alive: a leak
    const std::string leaks = MemTrack::LeakReport();
    CHECK(leaks.find("t.pool") != std::string::npos);
    CHECK(leaks.find("1 object") != std::string::npos);
    {
        StackAllocator st(128); st.SetTag("t.stack");
        st.Allocate(16);
    }
    CHECK(MemTrack::LeakReport().find("t.stack") != std::string::npos);
    {
        PoolAllocator<Thing> clean(4); clean.SetTag("t.clean");
        Thing* x = clean.Create(); clean.Destroy(x);
    }
    CHECK(MemTrack::LeakReport().find("t.clean") == std::string::npos);
    std::printf("%s%s", MemTrack::Report().c_str(), MemTrack::LeakReport().c_str());
    MemTrack::Enable(false);
    MemTrack::Reset();
}
} // namespace

int main() {
    TestStack();
    TestOffByDefault();
    TestTracking();
    TEST_MAIN_RESULT();
}
