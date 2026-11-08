#include "test_framework.hpp"
#include "okay/Debug/MemoryTracker.hpp"

#include <cstdint>
#include <string>
#include <vector>
#ifndef _WIN32
#include <thread>
#endif

using namespace okay;

static bool Balanced(const std::string& s) {
    int curly = 0, square = 0; bool inStr = false;
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (inStr) { if (c == '\\') ++i; else if (c == '"') inStr = false; continue; }
        if (c == '"') inStr = true;
        else if (c == '{') ++curly; else if (c == '}') { if (--curly < 0) return false; }
        else if (c == '[') ++square; else if (c == ']') { if (--square < 0) return false; }
    }
    return !inStr && curly == 0 && square == 0;
}

int main() {
    RUN_SUITE("debug_memory");

    // ---- categories ---------------------------------------------------------------
    {
        MemoryTracker m;
        CHECK(m.CategoryCount() == MemoryTracker::kBuiltinCount);
        CHECK(m.CategoryName(MemoryTracker::Textures) == "Textures");
        CHECK(m.CategoryName(MemoryTracker::SceneObjects) == "Scene Objects");
        CHECK(m.FindCategory("Audio") == MemoryTracker::Audio);
        CHECK(m.FindCategory("Fluids") == -1);
        int fluids = m.Category("Fluids");
        CHECK(fluids == MemoryTracker::kBuiltinCount);
        CHECK(m.Category("Fluids") == fluids);                       // same id on a second lookup
        CHECK(m.CategoryCount() == MemoryTracker::kBuiltinCount + 1);
        CHECK(m.CategoryName(999).empty() && m.CategoryName(-1).empty());
        // unknown ids are ignored, never crash
        m.Add(-1, 10); m.Remove(999, 10); m.Resize(-5, 1, 2); m.Set(77, 1, 1);
        CHECK(m.TotalBytes() == 0 && m.Bytes(-1) == 0 && m.Count(500) == 0 && m.PeakBytes(500) == 0);
        // the table is bounded
        for (int i = 0; i < 200; ++i) m.Category("cat" + std::to_string(i));
        CHECK(m.CategoryCount() == MemoryTracker::kMaxCategories);
        CHECK(m.Category("one too many") == -1);
        m.Add("one too many", 5);                                    // by name, no category: ignored
        CHECK(m.TotalBytes() == 0);
        CHECK(&MemoryTracker::Global() == &MemoryTracker::Global());
    }

    // ---- add / remove / resize / set, peaks ------------------------------------------
    {
        MemoryTracker m;
        const int T = MemoryTracker::Textures, M = MemoryTracker::Meshes;
        m.Add(T, 1000); m.Add(T, 500, 2); m.Add(M, 200);
        CHECK(m.Bytes(T) == 1500 && m.Count(T) == 3);
        CHECK(m.Bytes(M) == 200 && m.TotalBytes() == 1700);
        CHECK(m.PeakBytes(T) == 1500 && m.PeakTotalBytes() == 1700);
        m.Remove(T, 1000);
        CHECK(m.Bytes(T) == 500 && m.Count(T) == 2 && m.TotalBytes() == 700);
        CHECK(m.PeakBytes(T) == 1500 && m.PeakTotalBytes() == 1700);   // peaks stay
        m.Resize(T, 500, 800);                                         // grew in place
        CHECK(m.Bytes(T) == 800 && m.Count(T) == 2 && m.TotalBytes() == 1000);
        m.Resize(T, 800, 300);                                         // shrank
        CHECK(m.Bytes(T) == 300 && m.Count(T) == 2 && m.TotalBytes() == 500);
        // by name
        m.Add("Textures", 100); m.Remove("Textures", 50); m.Resize("Textures", 50, 60);
        CHECK(m.Bytes(T) == 300 + 100 - 50 + 10);
        // removing more than is live clamps to zero and is counted
        CHECK(m.Underflows() == 0);
        m.Remove(M, 5000);
        CHECK(m.Bytes(M) == 0 && m.Count(M) == 0 && m.Underflows() == 1);
        CHECK(m.TotalBytes() == m.Bytes(T));                            // the total did not go negative
        // a negative Add is a removal
        m.Add(T, -60);
        CHECK(m.Bytes(T) == 300);
        // Set replaces the figures
        m.Set(MemoryTracker::Audio, 4096, 3);
        CHECK(m.Bytes(MemoryTracker::Audio) == 4096 && m.Count(MemoryTracker::Audio) == 3);
        m.Set(MemoryTracker::Audio, 1024, 1);
        CHECK(m.Bytes(MemoryTracker::Audio) == 1024 && m.PeakBytes(MemoryTracker::Audio) == 4096);
        CHECK(m.TotalBytes() == 300 + 1024);
        m.ResetPeaks();
        CHECK(m.PeakBytes(MemoryTracker::Audio) == 1024 && m.PeakTotalBytes() == 1324);
        m.Reset();
        CHECK(m.TotalBytes() == 0 && m.Bytes(T) == 0 && m.Frame() == 0 && m.Underflows() == 0);
        CHECK(m.CategoryCount() == MemoryTracker::kBuiltinCount);       // categories survive a reset
    }

    // ---- per frame deltas and history ring ----------------------------------------------
    {
        MemoryTracker m(4);                                             // keeps the last 4 frames
        const int T = MemoryTracker::Textures;
        m.Add(T, 100);
        m.EndFrame();
        CHECK(m.FrameDelta(T) == 0);                                    // first frame: nothing to compare with
        m.Add(T, 50);
        m.EndFrame();
        CHECK(m.FrameDelta(T) == 50 && m.TotalFrameDelta() == 50);
        m.Remove(T, 120);
        m.EndFrame();
        CHECK(m.FrameDelta(T) == -120 && m.TotalFrameDelta() == -120);
        m.EndFrame();
        CHECK(m.FrameDelta(T) == 0);
        CHECK(m.Frame() == 4);
        std::vector<std::int64_t> h = m.History(T);
        CHECK(h.size() == 4 && h[0] == 100 && h[1] == 150 && h[2] == 30 && h[3] == 30);
        m.Add(T, 10); m.EndFrame();                                      // wraps: the oldest sample goes
        m.Add(T, 10); m.EndFrame();
        h = m.History(T);
        CHECK(h.size() == 4 && h[0] == 30 && h[1] == 30 && h[2] == 40 && h[3] == 50);
        CHECK(m.FrameDelta(T) == 10);
        std::vector<std::int64_t> last2 = m.History(T, 2);
        CHECK(last2.size() == 2 && last2[0] == 40 && last2[1] == 50);
        CHECK(m.TotalHistory().size() == 4 && m.TotalHistory().back() == 50);
        CHECK(m.History(-1).empty());
    }

    // ---- leak suspect heuristic ----------------------------------------------------------------
    {
        MemoryLeakOptions opt;
        opt.windowFrames = 50;
        opt.minGrowthBytes = 1000;
        MemoryTracker m;
        const int A = MemoryTracker::Audio, T = MemoryTracker::Textures, M = MemoryTracker::Meshes, S = MemoryTracker::Scripts;
        const int P = MemoryTracker::Physics;
        m.Add(T, 5000); m.Add(M, 5000); m.Add(S, 5000); m.Add(P, 5000);

        // too few frames: nothing is reported yet
        for (int i = 0; i < 49; ++i) { m.Add(A, 100); m.EndFrame(); }
        CHECK(m.LeakSuspects(opt).empty());

        for (int i = 0; i < 80; ++i) {
            m.Add(A, 100);                                   // grows every frame: the leak
            if (i == 10) m.Add(T, 8000);                     // one jump, then flat: a load, not a leak
            m.Add(M, 20);                                    // grows steadily...
            if (i == 61) m.Remove(M, 50);                    // ...but drops once: not monotonic
            if (i % 2 == 0) m.Add(S, 3); else m.Remove(S, 3);   // wobbles: not monotonic
            m.Add(P, 1);                                     // grows 1 byte a frame: below minGrowthBytes
            m.EndFrame();
        }
        std::vector<MemoryLeakSuspect> s = m.LeakSuspects(opt);
        CHECK(s.size() == 1);
        if (s.size() == 1) {
            CHECK(s[0].name == "Audio" && s[0].category == A);
            CHECK(s[0].windowFrames == 50 && s[0].risingFrames == 49);
            CHECK(s[0].growthBytes == 49 * 100);
            CHECK(s[0].endBytes - s[0].startBytes == s[0].growthBytes);
            CHECK_NEAR(s[0].bytesPerFrame, 100.0, 1e-9);
        }
        // a tolerance lets the small dip through: Meshes now qualifies as well
        MemoryLeakOptions loose = opt;
        loose.toleranceBytes = 100;
        loose.minGrowthBytes = 300;
        bool meshes = false, others = false;
        for (const MemoryLeakSuspect& x : m.LeakSuspects(loose)) {
            if (x.name == "Meshes") meshes = true;
            else if (x.name != "Audio") others = true;           // Scripts, Physics, Textures stay out
        }
        CHECK(meshes && !others);
        // a bigger minimum growth removes everything
        MemoryLeakOptions strict = opt; strict.minGrowthBytes = 1 << 20;
        CHECK(m.LeakSuspects(strict).empty());
        // a staircase (growth every 5th frame) needs a lower minRisingFraction to count
        MemoryTracker st;
        for (int i = 0; i < 60; ++i) { if (i % 5 == 0) st.Add(MemoryTracker::Audio, 100); st.EndFrame(); }
        MemoryLeakOptions stairs = opt; stairs.minGrowthBytes = 500;
        CHECK(st.LeakSuspects(stairs).empty());                  // only ~20% of the steps rise (default needs 25%)
        stairs.minRisingFraction = 0.1;
        CHECK(st.LeakSuspects(stairs).size() == 1);
        // once the growth stops the suspicion fades as the window slides past it
        for (int i = 0; i < 60; ++i) m.EndFrame();
        CHECK(m.LeakSuspects(opt).empty());
        // and it is sorted by growth when several categories leak
        MemoryTracker two;
        for (int i = 0; i < 60; ++i) { two.Add(MemoryTracker::Audio, 100); two.Add(MemoryTracker::Meshes, 300); two.EndFrame(); }
        std::vector<MemoryLeakSuspect> both = two.LeakSuspects(opt);
        CHECK(both.size() == 2 && both[0].name == "Meshes" && both[1].name == "Audio");
        // the history ring is the limit: a window longer than what is kept finds nothing
        MemoryTracker small(10);
        for (int i = 0; i < 60; ++i) { small.Add(MemoryTracker::Audio, 1000); small.EndFrame(); }
        CHECK(small.LeakSuspects(opt).empty());
    }

    // ---- snapshots and the diff report --------------------------------------------------------------
    {
        MemoryTracker m;
        m.Add(MemoryTracker::Textures, 4096, 2);
        m.Add(MemoryTracker::Meshes, 1024);
        m.Add(MemoryTracker::Audio, 2048);
        m.EndFrame();
        MemorySnapshot a = m.Snapshot("before");
        CHECK(a.label == "before" && a.totalBytes == 4096 + 1024 + 2048 && a.frame == 1);
        CHECK(a.Find("Textures") && a.Find("Textures")->bytes == 4096 && a.Find("Textures")->count == 2);
        CHECK(a.Find("Nope") == nullptr);

        m.Add(MemoryTracker::Textures, 8192);
        m.Remove(MemoryTracker::Audio, 2048);
        m.Add(MemoryTracker::Scripts, 100);
        int custom = m.Category("Fluids");
        m.Add(custom, 300);
        m.EndFrame(); m.EndFrame();
        MemorySnapshot b = m.Snapshot("after");

        MemoryDiff d = MemoryTracker::Diff(a, b);
        CHECK(d.labelBefore == "before" && d.labelAfter == "after" && d.framesBetween == 2);
        CHECK(d.totalDeltaBytes == 8192 - 2048 + 100 + 300);
        CHECK(d.entries.size() == 4);                         // Meshes did not change: left out
        if (d.entries.size() == 4) {
            CHECK(d.entries[0].name == "Textures" && d.entries[0].deltaBytes == 8192);        // largest change first
            CHECK(d.entries[1].name == "Audio" && d.entries[1].deltaBytes == -2048 && d.entries[1].deltaCount == -1);
            CHECK(d.entries[2].name == "Fluids" && d.entries[2].bytesBefore == 0 && d.entries[2].bytesAfter == 300);
            CHECK(d.entries[3].name == "Scripts");
        }
        CHECK(MemoryTracker::Diff(a, b, true).entries.size() > d.entries.size());               // includeUnchanged
        CHECK(MemoryTracker::Diff(a, a).entries.empty() && MemoryTracker::Diff(a, a).totalDeltaBytes == 0);
        // diff the other way round: signs flip
        MemoryDiff rev = MemoryTracker::Diff(b, a);
        CHECK(rev.totalDeltaBytes == -d.totalDeltaBytes);
        // a category that exists only in the older snapshot shows as fully released
        MemorySnapshot oldOnly; oldOnly.categories.push_back(MemoryCategoryInfo()); oldOnly.categories[0].name = "Gone"; oldOnly.categories[0].bytes = 64;
        MemoryDiff gone = MemoryTracker::Diff(oldOnly, MemorySnapshot());
        CHECK(gone.entries.size() == 1 && gone.entries[0].deltaBytes == -64);

        std::string text = d.ToText();
        CHECK(text.find("before") != std::string::npos && text.find("Textures") != std::string::npos);
        CHECK(text.find("+8.00 KB") != std::string::npos);
        CHECK(b.ToText().find("Memory snapshot \"after\"") != std::string::npos);
        CHECK(Balanced(d.ToJson()) && Balanced(a.ToJson()) && Balanced(MemoryDiff().ToJson()));
        CHECK(d.ToJson().find("\"deltaBytes\":8192") != std::string::npos);
    }

#ifndef _WIN32
    // ---- thread safety smoke test: concurrent accounting adds up exactly ---------------------------
    {
        MemoryTracker m;
        const int kThreads = 4, kOps = 20000;
        std::vector<std::thread> th;
        for (int t = 0; t < kThreads; ++t)
            th.emplace_back([&m, kOps, t] {
                for (int i = 0; i < kOps; ++i) {
                    m.Add(MemoryTracker::Textures, 16);
                    m.Add("shared-name", 8);                         // concurrent registration + lookup
                    m.Add(MemoryTracker::Meshes, 4);
                    m.Remove(MemoryTracker::Meshes, 4);
                    if ((i & 1023) == 0) m.EndFrame();
                    (void)t;
                }
            });
        for (auto& x : th) x.join();
        CHECK(m.Bytes(MemoryTracker::Textures) == (std::int64_t)kThreads * kOps * 16);
        CHECK(m.Bytes(m.FindCategory("shared-name")) == (std::int64_t)kThreads * kOps * 8);
        CHECK(m.Bytes(MemoryTracker::Meshes) == 0);
        CHECK(m.Count(MemoryTracker::Textures) == (std::int64_t)kThreads * kOps);
        CHECK(m.TotalBytes() == (std::int64_t)kThreads * kOps * (16 + 8));
        CHECK(m.Underflows() == 0);
        CHECK(m.PeakTotalBytes() >= m.TotalBytes());
        int shared = 0;
        for (int i = 0; i < m.CategoryCount(); ++i) if (m.CategoryName(i) == "shared-name") ++shared;
        CHECK(shared == 1);                                          // registered exactly once
    }
#endif

    TEST_MAIN_RESULT();
}
