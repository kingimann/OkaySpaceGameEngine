#include "okay/Debug/MemoryTracker.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace okay {

using namespace debugutil;

// ---------------------------------------------------------------------------
// MemoryTracker
// ---------------------------------------------------------------------------

MemoryTracker::MemoryTracker(int historyFrames)
    : m_histCap(historyFrames < 2 ? 2 : (std::size_t)historyFrames) {
    static const char* const kNames[kBuiltinCount] = {
        "Textures", "Meshes", "Audio", "Scripts", "Scene Objects", "Animation", "Physics", "UI", "Shaders", "Other"};
    for (int i = 0; i < kBuiltinCount; ++i) m_slots[i].name = kNames[i];
    m_count.store(kBuiltinCount, std::memory_order_release);
}

MemoryTracker& MemoryTracker::Global() {
    static MemoryTracker t;
    return t;
}

int MemoryTracker::FindCategory(const std::string& name) const {
    int n = m_count.load(std::memory_order_acquire);
    for (int i = 0; i < n; ++i) if (m_slots[i].name == name) return i;
    return -1;
}

int MemoryTracker::Category(const std::string& name) {
    int id = FindCategory(name);
    if (id >= 0) return id;
    SpinGuard g(m_lock);
    id = FindCategory(name);                       // another thread may have just added it
    if (id >= 0) return id;
    int n = m_count.load(std::memory_order_relaxed);
    if (n >= kMaxCategories) return -1;
    m_slots[n].name = name;
    m_count.store(n + 1, std::memory_order_release);
    return n;
}

std::string MemoryTracker::CategoryName(int id) const {
    return Valid(id) ? m_slots[id].name : std::string();
}

void MemoryTracker::RaisePeak(std::atomic<std::int64_t>& peak, std::int64_t v) {
    std::int64_t cur = peak.load(std::memory_order_relaxed);
    while (v > cur && !peak.compare_exchange_weak(cur, v, std::memory_order_relaxed)) {}
}

void MemoryTracker::ApplyTotal(std::int64_t delta) {
    if (delta == 0) return;
    std::int64_t now = m_total.fetch_add(delta, std::memory_order_relaxed) + delta;
    if (delta > 0) RaisePeak(m_totalPeak, now);
}

void MemoryTracker::Add(int category, std::int64_t bytes, std::int64_t count) {
    if (!Valid(category)) return;
    if (bytes < 0) { Remove(category, -bytes, count); return; }
    Slot& s = m_slots[category];
    std::int64_t b = s.bytes.fetch_add(bytes, std::memory_order_relaxed) + bytes;
    std::int64_t c = s.count.fetch_add(count, std::memory_order_relaxed) + count;
    s.added.fetch_add(bytes, std::memory_order_relaxed);
    RaisePeak(s.peakBytes, b);
    RaisePeak(s.peakCount, c);
    ApplyTotal(bytes);
}

void MemoryTracker::Remove(int category, std::int64_t bytes, std::int64_t count) {
    if (!Valid(category)) return;
    if (bytes < 0) bytes = 0;
    Slot& s = m_slots[category];
    bool under = false;
    std::int64_t cur = s.bytes.load(std::memory_order_relaxed), applied = 0;
    for (;;) {
        std::int64_t next = cur - bytes;
        if (next < 0) { next = 0; under = true; }
        applied = cur - next;
        if (s.bytes.compare_exchange_weak(cur, next, std::memory_order_relaxed)) break;
        under = false;
    }
    std::int64_t cc = s.count.load(std::memory_order_relaxed);
    for (;;) {
        std::int64_t next = cc - count;
        if (next < 0) { next = 0; under = true; }
        if (s.count.compare_exchange_weak(cc, next, std::memory_order_relaxed)) break;
    }
    if (under) m_underflows.fetch_add(1, std::memory_order_relaxed);
    s.removed.fetch_add(applied, std::memory_order_relaxed);
    ApplyTotal(-applied);
}

void MemoryTracker::Resize(int category, std::int64_t oldBytes, std::int64_t newBytes) {
    if (!Valid(category)) return;
    if (newBytes >= oldBytes) {
        Slot& s = m_slots[category];
        std::int64_t d = newBytes - oldBytes;
        std::int64_t b = s.bytes.fetch_add(d, std::memory_order_relaxed) + d;
        s.added.fetch_add(d, std::memory_order_relaxed);
        RaisePeak(s.peakBytes, b);
        ApplyTotal(d);
    } else {
        Remove(category, oldBytes - newBytes, 0);
    }
}

void MemoryTracker::Set(int category, std::int64_t bytes, std::int64_t count) {
    if (!Valid(category)) return;
    if (bytes < 0) bytes = 0;
    if (count < 0) count = 0;
    Slot& s = m_slots[category];
    std::int64_t prev = s.bytes.exchange(bytes, std::memory_order_relaxed);
    s.count.store(count, std::memory_order_relaxed);
    if (bytes > prev) s.added.fetch_add(bytes - prev, std::memory_order_relaxed);
    else s.removed.fetch_add(prev - bytes, std::memory_order_relaxed);
    RaisePeak(s.peakBytes, bytes);
    RaisePeak(s.peakCount, count);
    ApplyTotal(bytes - prev);
}

std::int64_t MemoryTracker::Bytes(int c) const { return Valid(c) ? m_slots[c].bytes.load(std::memory_order_relaxed) : 0; }
std::int64_t MemoryTracker::Count(int c) const { return Valid(c) ? m_slots[c].count.load(std::memory_order_relaxed) : 0; }
std::int64_t MemoryTracker::PeakBytes(int c) const { return Valid(c) ? m_slots[c].peakBytes.load(std::memory_order_relaxed) : 0; }

void MemoryTracker::ResetPeaks() {
    int n = m_count.load(std::memory_order_acquire);
    for (int i = 0; i < n; ++i) {
        m_slots[i].peakBytes.store(m_slots[i].bytes.load(std::memory_order_relaxed), std::memory_order_relaxed);
        m_slots[i].peakCount.store(m_slots[i].count.load(std::memory_order_relaxed), std::memory_order_relaxed);
    }
    m_totalPeak.store(m_total.load(std::memory_order_relaxed), std::memory_order_relaxed);
}

void MemoryTracker::Reset() {
    SpinGuard g(m_lock);
    int n = m_count.load(std::memory_order_acquire);
    for (int i = 0; i < n; ++i) {
        Slot& s = m_slots[i];
        s.bytes = 0; s.count = 0; s.peakBytes = 0; s.peakCount = 0; s.added = 0; s.removed = 0;
        s.hist.clear(); s.histCount.clear(); s.histHead = 0; s.lastDelta = 0;
    }
    m_total = 0; m_totalPeak = 0; m_underflows = 0; m_frame = 0;
    m_totalHist.clear(); m_totalHead = 0; m_totalLastDelta = 0;
}

// ---------------------------------------------------------------------------
// Frames and history
// ---------------------------------------------------------------------------

static void RingPush(std::vector<std::int64_t>& ring, std::size_t& head, std::size_t cap, std::int64_t v) {
    if (ring.size() < cap) ring.push_back(v);
    else { ring[head] = v; head = (head + 1) % ring.size(); }
}

std::vector<std::int64_t> MemoryTracker::Ordered(const std::vector<std::int64_t>& ring, std::size_t head, std::size_t maxFrames) {
    std::vector<std::int64_t> out;
    std::size_t n = ring.size();
    std::size_t take = (maxFrames == 0 || maxFrames > n) ? n : maxFrames;
    out.reserve(take);
    for (std::size_t i = n - take; i < n; ++i) out.push_back(ring[(head + i) % n]);
    return out;
}

void MemoryTracker::EndFrame() {
    SpinGuard g(m_lock);
    int n = m_count.load(std::memory_order_acquire);
    std::int64_t total = 0;
    for (int i = 0; i < n; ++i) {
        Slot& s = m_slots[i];
        std::int64_t b = s.bytes.load(std::memory_order_relaxed);
        std::int64_t c = s.count.load(std::memory_order_relaxed);
        // newest stored sample: just before the head (the head stays 0 until the ring is full)
        std::int64_t prev = s.hist.empty() ? b : s.hist[(s.histHead + s.hist.size() - 1) % s.hist.size()];
        s.lastDelta = b - prev;
        std::size_t head2 = s.histHead;
        RingPush(s.hist, s.histHead, m_histCap, b);
        RingPush(s.histCount, head2, m_histCap, c);
        total += b;
    }
    std::int64_t prevTotal = total;
    if (!m_totalHist.empty()) prevTotal = m_totalHist[(m_totalHead + m_totalHist.size() - 1) % m_totalHist.size()];
    m_totalLastDelta = total - prevTotal;
    RingPush(m_totalHist, m_totalHead, m_histCap, total);
    m_frame.fetch_add(1, std::memory_order_relaxed);
}

std::int64_t MemoryTracker::FrameDelta(int category) const {
    if (!Valid(category)) return 0;
    SpinGuard g(m_lock);
    return m_slots[category].lastDelta;
}

std::int64_t MemoryTracker::TotalFrameDelta() const {
    SpinGuard g(m_lock);
    return m_totalLastDelta;
}

std::vector<std::int64_t> MemoryTracker::History(int category, std::size_t maxFrames) const {
    if (!Valid(category)) return {};
    SpinGuard g(m_lock);
    return Ordered(m_slots[category].hist, m_slots[category].histHead, maxFrames);
}

std::vector<std::int64_t> MemoryTracker::TotalHistory(std::size_t maxFrames) const {
    SpinGuard g(m_lock);
    return Ordered(m_totalHist, m_totalHead, maxFrames);
}

// ---------------------------------------------------------------------------
// Leak heuristic
// ---------------------------------------------------------------------------

std::vector<MemoryLeakSuspect> MemoryTracker::LeakSuspects(const MemoryLeakOptions& opt) const {
    std::vector<MemoryLeakSuspect> out;
    const std::size_t window = opt.windowFrames < 2 ? 2 : (std::size_t)opt.windowFrames;
    SpinGuard g(m_lock);
    int n = m_count.load(std::memory_order_acquire);
    for (int i = 0; i < n; ++i) {
        const Slot& s = m_slots[i];
        if (s.hist.size() < window) continue;
        std::vector<std::int64_t> bytes = Ordered(s.hist, s.histHead, window);
        std::vector<std::int64_t> counts = Ordered(s.histCount, s.histHead, window);
        bool ok = true;
        int rising = 0;
        for (std::size_t k = 1; k < bytes.size(); ++k) {
            std::int64_t d = bytes[k] - bytes[k - 1];
            if (d < -opt.toleranceBytes) { ok = false; break; }
            if (d > 0) ++rising;
        }
        if (!ok) continue;
        std::int64_t growth = bytes.back() - bytes.front();
        if (growth < opt.minGrowthBytes || growth <= 0) continue;
        if ((double)rising < opt.minRisingFraction * (double)(window - 1)) continue;
        MemoryLeakSuspect sp;
        sp.category = i;
        sp.name = s.name;
        sp.startBytes = bytes.front(); sp.endBytes = bytes.back(); sp.growthBytes = growth;
        sp.startCount = counts.front(); sp.endCount = counts.back();
        sp.windowFrames = (int)window;
        sp.risingFrames = rising;
        sp.bytesPerFrame = (double)growth / (double)(window - 1);
        out.push_back(std::move(sp));
    }
    std::sort(out.begin(), out.end(), [](const MemoryLeakSuspect& a, const MemoryLeakSuspect& b) {
        return a.growthBytes > b.growthBytes; });
    return out;
}

// ---------------------------------------------------------------------------
// Snapshots
// ---------------------------------------------------------------------------

MemorySnapshot MemoryTracker::Snapshot(const std::string& label) const {
    MemorySnapshot snap;
    snap.label = label;
    snap.frame = m_frame.load(std::memory_order_relaxed);
    int n = m_count.load(std::memory_order_acquire);
    for (int i = 0; i < n; ++i) {
        const Slot& s = m_slots[i];
        MemoryCategoryInfo c;
        c.name = s.name;
        c.bytes = s.bytes.load(std::memory_order_relaxed);
        c.count = s.count.load(std::memory_order_relaxed);
        c.peakBytes = s.peakBytes.load(std::memory_order_relaxed);
        c.peakCount = s.peakCount.load(std::memory_order_relaxed);
        c.totalAdded = s.added.load(std::memory_order_relaxed);
        c.totalRemoved = s.removed.load(std::memory_order_relaxed);
        {
            SpinGuard g(m_lock);
            c.frameDelta = s.lastDelta;
        }
        snap.totalBytes += c.bytes;
        snap.categories.push_back(std::move(c));
    }
    snap.peakTotalBytes = m_totalPeak.load(std::memory_order_relaxed);
    return snap;
}

const MemoryCategoryInfo* MemorySnapshot::Find(const std::string& name) const {
    for (const MemoryCategoryInfo& c : categories) if (c.name == name) return &c;
    return nullptr;
}

std::string MemorySnapshot::ToText() const {
    std::string out = "Memory snapshot";
    if (!label.empty()) out += " \"" + label + "\"";
    out += " (frame " + std::to_string(frame) + "): " + FormatBytes(totalBytes) + " live, peak " + FormatBytes(peakTotalBytes) + "\n";
    for (const MemoryCategoryInfo& c : categories) {
        if (c.bytes == 0 && c.peakBytes == 0 && c.count == 0) continue;
        char buf[160];
        std::snprintf(buf, sizeof(buf), "  %-16s %12s  x%lld  (peak %s)\n", c.name.c_str(), FormatBytes(c.bytes).c_str(),
                      (long long)c.count, FormatBytes(c.peakBytes).c_str());
        out += buf;
    }
    return out;
}

std::string MemorySnapshot::ToJson() const {
    std::string out = "{\"label\":";
    AppendJsonString(out, label);
    out += ",\"frame\":" + std::to_string(frame) + ",\"totalBytes\":" + std::to_string(totalBytes)
         + ",\"peakTotalBytes\":" + std::to_string(peakTotalBytes) + ",\"categories\":[";
    for (std::size_t i = 0; i < categories.size(); ++i) {
        const MemoryCategoryInfo& c = categories[i];
        if (i) out += ',';
        out += "{\"name\":"; AppendJsonString(out, c.name);
        out += ",\"bytes\":" + std::to_string(c.bytes) + ",\"count\":" + std::to_string(c.count)
             + ",\"peakBytes\":" + std::to_string(c.peakBytes) + ",\"totalAdded\":" + std::to_string(c.totalAdded)
             + ",\"totalRemoved\":" + std::to_string(c.totalRemoved) + ",\"frameDelta\":" + std::to_string(c.frameDelta) + "}";
    }
    out += "]}";
    return out;
}

MemoryDiff MemoryTracker::Diff(const MemorySnapshot& before, const MemorySnapshot& after, bool includeUnchanged) {
    MemoryDiff d;
    d.labelBefore = before.label; d.labelAfter = after.label;
    d.framesBetween = after.frame >= before.frame ? after.frame - before.frame : 0;
    d.totalDeltaBytes = after.totalBytes - before.totalBytes;
    for (const MemoryCategoryInfo& a : after.categories) {
        MemoryDiffEntry e;
        e.name = a.name;
        const MemoryCategoryInfo* b = before.Find(a.name);
        e.bytesBefore = b ? b->bytes : 0; e.countBefore = b ? b->count : 0;
        e.bytesAfter = a.bytes; e.countAfter = a.count;
        e.deltaBytes = e.bytesAfter - e.bytesBefore; e.deltaCount = e.countAfter - e.countBefore;
        if (includeUnchanged || e.deltaBytes != 0 || e.deltaCount != 0) d.entries.push_back(std::move(e));
    }
    for (const MemoryCategoryInfo& b : before.categories) {   // categories that vanished from `after`
        if (after.Find(b.name)) continue;
        MemoryDiffEntry e;
        e.name = b.name; e.bytesBefore = b.bytes; e.countBefore = b.count;
        e.deltaBytes = -b.bytes; e.deltaCount = -b.count;
        if (includeUnchanged || e.deltaBytes != 0 || e.deltaCount != 0) d.entries.push_back(std::move(e));
    }
    std::stable_sort(d.entries.begin(), d.entries.end(), [](const MemoryDiffEntry& x, const MemoryDiffEntry& y) {
        std::int64_t ax = x.deltaBytes < 0 ? -x.deltaBytes : x.deltaBytes;
        std::int64_t ay = y.deltaBytes < 0 ? -y.deltaBytes : y.deltaBytes;
        return ax > ay; });
    return d;
}

std::string MemoryDiff::ToText() const {
    std::string out = "Memory diff";
    if (!labelBefore.empty() || !labelAfter.empty()) out += " \"" + labelBefore + "\" -> \"" + labelAfter + "\"";
    out += " over " + std::to_string(framesBetween) + " frame(s): total ";
    out += (totalDeltaBytes >= 0 ? "+" : "") + FormatBytes(totalDeltaBytes) + "\n";
    for (const MemoryDiffEntry& e : entries) {
        char buf[200];
        std::string delta = (e.deltaBytes >= 0 ? "+" : "") + FormatBytes(e.deltaBytes);
        std::snprintf(buf, sizeof(buf), "  %-16s %12s  (%s -> %s)  count %+lld\n", e.name.c_str(), delta.c_str(),
                      FormatBytes(e.bytesBefore).c_str(), FormatBytes(e.bytesAfter).c_str(), (long long)e.deltaCount);
        out += buf;
    }
    return out;
}

std::string MemoryDiff::ToJson() const {
    std::string out = "{\"labelBefore\":";
    AppendJsonString(out, labelBefore);
    out += ",\"labelAfter\":"; AppendJsonString(out, labelAfter);
    out += ",\"framesBetween\":" + std::to_string(framesBetween) + ",\"totalDeltaBytes\":" + std::to_string(totalDeltaBytes)
         + ",\"entries\":[";
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const MemoryDiffEntry& e = entries[i];
        if (i) out += ',';
        out += "{\"name\":"; AppendJsonString(out, e.name);
        out += ",\"bytesBefore\":" + std::to_string(e.bytesBefore) + ",\"bytesAfter\":" + std::to_string(e.bytesAfter)
             + ",\"deltaBytes\":" + std::to_string(e.deltaBytes) + ",\"countBefore\":" + std::to_string(e.countBefore)
             + ",\"countAfter\":" + std::to_string(e.countAfter) + ",\"deltaCount\":" + std::to_string(e.deltaCount) + "}";
    }
    out += "]}";
    return out;
}

} // namespace okay
