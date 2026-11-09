#pragma once
// Memory tracking by named category (feature map 23.2). Systems report what they
// hold (a texture upload, a loaded audio clip, a spawned object) with Add/Remove/
// Resize; the tracker keeps live bytes and counts, peaks, per-frame deltas, a
// short history per category, a leak-suspect heuristic and a snapshot diff report.
//
//     MemoryTracker& mem = MemoryTracker::Global();
//     mem.Add(MemoryTracker::Textures, tex.SizeBytes());       // on load
//     mem.Remove(MemoryTracker::Textures, tex.SizeBytes());    // on unload
//     mem.EndFrame();                                          // once per frame
//     for (auto& s : mem.LeakSuspects()) Log::Warning("growing: ", s.name);
//
// Accounting is manual and cheap (relaxed atomics, no allocation on Add/Remove),
// so asset loader threads can report directly. Whole-process heap counters are a
// separate, opt-in piece: see MemoryHooks.hpp.
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

#include "okay/Debug/DebugUtil.hpp"

namespace okay {

/// One category in a snapshot.
struct MemoryCategoryInfo {
    std::string name;
    std::int64_t bytes = 0;        // live
    std::int64_t count = 0;        // live allocations/objects
    std::int64_t peakBytes = 0;
    std::int64_t peakCount = 0;
    std::int64_t totalAdded = 0;   // bytes ever added
    std::int64_t totalRemoved = 0;
    std::int64_t frameDelta = 0;   // change in bytes between the last two EndFrame calls
};

struct MemorySnapshot {
    std::string label;
    std::uint64_t frame = 0;       // the tracker's frame counter when taken
    std::int64_t totalBytes = 0;
    std::int64_t peakTotalBytes = 0;
    std::vector<MemoryCategoryInfo> categories;

    const MemoryCategoryInfo* Find(const std::string& name) const;
    std::string ToText() const;
    std::string ToJson() const;
};

struct MemoryDiffEntry {
    std::string name;
    std::int64_t bytesBefore = 0, bytesAfter = 0, deltaBytes = 0;
    std::int64_t countBefore = 0, countAfter = 0, deltaCount = 0;
};

/// What changed between two snapshots, largest change first.
struct MemoryDiff {
    std::string labelBefore, labelAfter;
    std::uint64_t framesBetween = 0;
    std::int64_t totalDeltaBytes = 0;
    std::vector<MemoryDiffEntry> entries;

    std::string ToText() const;
    std::string ToJson() const;
};

struct MemoryLeakOptions {
    int windowFrames = 120;                  // look at this many EndFrame samples
    std::int64_t minGrowthBytes = 1 << 20;   // ignore growth smaller than this
    std::int64_t toleranceBytes = 0;         // a drop larger than this between frames clears the suspicion (0 = strictly monotonic)
    double minRisingFraction = 0.25;         // share of frame steps that must grow (a single jump that then stays flat is not a leak)
};

struct MemoryLeakSuspect {
    int category = -1;
    std::string name;
    std::int64_t startBytes = 0, endBytes = 0, growthBytes = 0;
    std::int64_t startCount = 0, endCount = 0;
    int windowFrames = 0;
    int risingFrames = 0;
    double bytesPerFrame = 0.0;
};

class MemoryTracker {
public:
    static constexpr int kMaxCategories = 64;

    /// Category ids registered by the constructor, in this order (stable).
    enum Builtin : int {
        Textures = 0, Meshes, Audio, Scripts, SceneObjects, Animation, Physics, UI, Shaders, Other,
        kBuiltinCount
    };

    /// `historyFrames` EndFrame samples are kept per category.
    explicit MemoryTracker(int historyFrames = 600);
    MemoryTracker(const MemoryTracker&) = delete;
    MemoryTracker& operator=(const MemoryTracker&) = delete;

    /// A process-wide tracker for code that has no instance to hand.
    static MemoryTracker& Global();

    // ---- categories ------------------------------------------------------
    /// Id of a category, creating it on first use; -1 when kMaxCategories are used up.
    int Category(const std::string& name);
    /// Id of an existing category, or -1.
    int FindCategory(const std::string& name) const;
    int CategoryCount() const { return m_count.load(std::memory_order_acquire); }
    std::string CategoryName(int id) const;

    // ---- accounting (thread safe, lock free) -----------------------------
    /// `count` is how many allocations/objects the bytes stand for (default 1).
    void Add(int category, std::int64_t bytes, std::int64_t count = 1);
    /// Removing more than is live clamps to zero and counts an underflow.
    void Remove(int category, std::int64_t bytes, std::int64_t count = 1);
    /// An allocation changed size (a growing array): bytes change, count does not.
    void Resize(int category, std::int64_t oldBytes, std::int64_t newBytes);
    /// Replace the live figures (for systems that know their total, e.g. a pool).
    void Set(int category, std::int64_t bytes, std::int64_t count);
    // By name (a lookup per call; cache the id on hot paths).
    void Add(const std::string& category, std::int64_t bytes, std::int64_t count = 1) { Add(Category(category), bytes, count); }
    void Remove(const std::string& category, std::int64_t bytes, std::int64_t count = 1) { Remove(Category(category), bytes, count); }
    void Resize(const std::string& category, std::int64_t oldBytes, std::int64_t newBytes) { Resize(Category(category), oldBytes, newBytes); }

    // ---- reading ---------------------------------------------------------
    std::int64_t Bytes(int category) const;
    std::int64_t Count(int category) const;
    std::int64_t PeakBytes(int category) const;
    std::int64_t TotalBytes() const { return m_total.load(std::memory_order_relaxed); }
    std::int64_t PeakTotalBytes() const { return m_totalPeak.load(std::memory_order_relaxed); }
    /// Remove() calls that asked for more than was live.
    std::int64_t Underflows() const { return m_underflows.load(std::memory_order_relaxed); }
    void ResetPeaks();
    /// Zero everything, history included (not for use while other threads account).
    void Reset();

    // ---- frames ----------------------------------------------------------
    /// Close a frame: record each category's live bytes into its history and the
    /// change since the previous frame. Call once per frame from one thread.
    void EndFrame();
    std::uint64_t Frame() const { return m_frame.load(std::memory_order_relaxed); }
    /// Bytes gained (positive) or released since the previous EndFrame.
    std::int64_t FrameDelta(int category) const;
    std::int64_t TotalFrameDelta() const;
    /// Live bytes at each EndFrame, oldest first; at most maxFrames (0 = all kept).
    std::vector<std::int64_t> History(int category, std::size_t maxFrames = 0) const;
    std::vector<std::int64_t> TotalHistory(std::size_t maxFrames = 0) const;

    // ---- leak suspects ---------------------------------------------------
    /// Categories whose live bytes never dropped over the last windowFrames frames,
    /// rose in at least minRisingFraction of the steps and grew by minGrowthBytes.
    /// A heuristic: a cache warming up looks the same, so treat results as leads.
    std::vector<MemoryLeakSuspect> LeakSuspects(const MemoryLeakOptions& opt = MemoryLeakOptions()) const;

    // ---- snapshots -------------------------------------------------------
    MemorySnapshot Snapshot(const std::string& label = std::string()) const;
    /// Entries with no change are left out unless includeUnchanged.
    static MemoryDiff Diff(const MemorySnapshot& before, const MemorySnapshot& after, bool includeUnchanged = false);

private:
    struct Slot {
        std::string name;                       // written once before the slot is published
        std::atomic<std::int64_t> bytes{0}, count{0}, peakBytes{0}, peakCount{0}, added{0}, removed{0};
        std::vector<std::int64_t> hist;         // bytes at each EndFrame (ring), under m_lock
        std::vector<std::int64_t> histCount;
        std::size_t histHead = 0;
        std::int64_t lastDelta = 0;
    };

    bool Valid(int id) const { return id >= 0 && id < m_count.load(std::memory_order_acquire); }
    static void RaisePeak(std::atomic<std::int64_t>& peak, std::int64_t v);
    void ApplyTotal(std::int64_t delta);
    static std::vector<std::int64_t> Ordered(const std::vector<std::int64_t>& ring, std::size_t head, std::size_t maxFrames);

    mutable debugutil::SpinLock m_lock;
    Slot m_slots[kMaxCategories];
    std::atomic<int> m_count{0};
    std::atomic<std::int64_t> m_total{0}, m_totalPeak{0}, m_underflows{0};
    std::atomic<std::uint64_t> m_frame{0};
    std::vector<std::int64_t> m_totalHist;
    std::size_t m_totalHead = 0;
    std::int64_t m_totalLastDelta = 0;
    std::size_t m_histCap = 600;
};

} // namespace okay
