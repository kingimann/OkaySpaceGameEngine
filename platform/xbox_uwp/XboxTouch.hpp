#pragma once
// Touch / pointer tracking and text composition for the Xbox UWP backend. Pure logic (no WinRT), tested on Linux; the real
// CoreWindow PointerPressed/Moved/Released/Canceled and CharacterReceived / TextComposition handlers are Windows glue that is
// UNCOMPILED and NEVER RUN. Written from Microsoft's documentation of CoreWindow pointer events (pointer id, device type,
// Canceled) and general tap / drag recognition ideas (a press that stays inside a small radius and is released quickly is a tap;
// a press that leaves the radius is a drag). No <thread>, no <mutex>. UNVERIFIED on Windows and on any console.
#include <cmath>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace okay::xbox {

enum class PointerPhase { Down, Move, Up, Cancel };

struct TouchConfig {
    std::uint32_t tapMaxMs = 300;   // released within this time (and inside the slop) = a tap
    int tapSlopPx = 10;             // a point that moves farther than this from where it went down is a drag, not a tap
    int dragSlopPx = 10;            // the drag starts after this much movement
};

/// What the tracker saw. Raw* mirror the pointer; Tap / Drag* are recognised gestures.
struct TouchEvent {
    enum Kind { RawDown, RawMove, RawUp, RawCancel, Tap, DragStart, DragMove, DragEnd, DragCancel } kind = RawDown;
    long long id = 0;            // the pointer id
    int x = 0, y = 0;            // pixels now
    int startX = 0, startY = 0;  // pixels where it went down
    bool primary = false;        // the oldest finger that is down (it also drives the mouse)
};

class TouchTracker {
public:
    explicit TouchTracker(const TouchConfig& c = TouchConfig()) : cfg_(c) {}
    const TouchConfig& Config() const { return cfg_; }
    int ActiveCount() const { return (int)pts_.size(); }
    bool Active(long long id) const { return pts_.count(id) != 0; }
    long long PrimaryId() const { return primary_; }   // -1 when none

    /// One pointer sample. Returns the events it produced (empty for an unknown id that is not a Down).
    std::vector<TouchEvent> Feed(long long id, PointerPhase ph, int x, int y, std::uint64_t timeMs) {
        std::vector<TouchEvent> out;
        auto it = pts_.find(id);
        if (ph == PointerPhase::Down) {
            if (it != pts_.end()) { Drop(id, out, PointerPhase::Cancel, 0); }   // a repeated Down for a live id restarts it
            Pt p; p.sx = p.x = x; p.sy = p.y = y; p.t0 = timeMs;
            if (!pts_.empty()) { p.multi = true; for (auto& kv : pts_) kv.second.multi = true; }
            else primary_ = id;
            pts_[id] = p; out.push_back(Mk(TouchEvent::RawDown, id, p));
            return out;
        }
        if (it == pts_.end()) return out;
        Pt& p = it->second;
        if (ph == PointerPhase::Move) {
            if (x == p.x && y == p.y) return out;
            p.x = x; p.y = y; out.push_back(Mk(TouchEvent::RawMove, id, p));
            if (!p.dragging && !p.multi && Dist(p) > cfg_.dragSlopPx) { p.dragging = true; out.push_back(Mk(TouchEvent::DragStart, id, p)); }
            else if (p.dragging) out.push_back(Mk(TouchEvent::DragMove, id, p));
            return out;
        }
        p.x = x; p.y = y;
        Drop(id, out, ph, timeMs);
        return out;
    }
    /// Cancel everything (the window lost focus, the app suspended).
    std::vector<TouchEvent> CancelAll() {
        std::vector<TouchEvent> out;
        std::vector<long long> ids; for (auto& kv : pts_) ids.push_back(kv.first);
        for (long long id : ids) Drop(id, out, PointerPhase::Cancel, 0);
        return out;
    }

private:
    struct Pt { int sx = 0, sy = 0, x = 0, y = 0; std::uint64_t t0 = 0; bool dragging = false, multi = false; };
    TouchEvent Mk(TouchEvent::Kind k, long long id, const Pt& p) const { TouchEvent e; e.kind = k; e.id = id; e.x = p.x; e.y = p.y; e.startX = p.sx; e.startY = p.sy; e.primary = (id == primary_); return e; }
    static double Dist(const Pt& p) { return std::sqrt((double)(p.x - p.sx) * (p.x - p.sx) + (double)(p.y - p.sy) * (p.y - p.sy)); }
    void Drop(long long id, std::vector<TouchEvent>& out, PointerPhase ph, std::uint64_t timeMs) {
        auto it = pts_.find(id); if (it == pts_.end()) return;
        const Pt p = it->second; const bool up = (ph == PointerPhase::Up);
        out.push_back(Mk(up ? TouchEvent::RawUp : TouchEvent::RawCancel, id, p));
        if (p.dragging) out.push_back(Mk(up ? TouchEvent::DragEnd : TouchEvent::DragCancel, id, p));
        else if (up && !p.multi && timeMs >= p.t0 && timeMs - p.t0 <= cfg_.tapMaxMs && Dist(p) <= cfg_.tapSlopPx) out.push_back(Mk(TouchEvent::Tap, id, p));
        const bool wasPrimary = (id == primary_);
        pts_.erase(it);
        if (wasPrimary) { primary_ = -1; bool first = true; std::uint64_t bt = 0; for (auto& kv : pts_) if (first || kv.second.t0 < bt) { first = false; bt = kv.second.t0; primary_ = kv.first; } }
    }
    TouchConfig cfg_; std::map<long long, Pt> pts_; long long primary_ = -1;
};

/// Text composition (an input method editor or the on-screen keyboard's inline suggestion): a string being composed with a caret,
/// then committed (becomes text) or cancelled. Caret counts code points. Pure state; the backend turns it into events.
class TextComposition {
public:
    bool Active() const { return active_; }
    const std::string& Text() const { return text_; }
    int Caret() const { return caret_; }
    /// New composition string (UTF-8). Empty text ends the composition without committing. Returns true when the state changed.
    bool Update(const std::string& utf8, int caret) {
        if (utf8.empty()) return Cancel();
        const int cp = CodePoints(utf8); if (caret < 0) caret = 0; if (caret > cp) caret = cp;
        if (active_ && utf8 == text_ && caret == caret_) return false;
        active_ = true; text_ = utf8; caret_ = caret; return true;
    }
    /// Commit: returns the text to insert (the given final text if non-empty, else what was composed) and clears the composition.
    std::string Commit(const std::string& finalText = std::string()) { std::string r = finalText.empty() ? text_ : finalText; active_ = false; text_.clear(); caret_ = 0; return r; }
    bool Cancel() { const bool was = active_; active_ = false; text_.clear(); caret_ = 0; return was; }
    static int CodePoints(const std::string& s) { int n = 0; for (unsigned char c : s) if ((c & 0xC0) != 0x80) ++n; return n; }
private:
    bool active_ = false; std::string text_; int caret_ = 0;
};

} // namespace okay::xbox
