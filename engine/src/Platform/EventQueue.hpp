#pragma once
// The queue the own backends put OS events in until the app polls. Pure logic, no OS.
//  * Order is kept. Consecutive MouseMove events of one window are merged (dx, dy add up, x, y are the latest),
//    consecutive MouseWheel events of one window add up, consecutive WindowResized events of one window keep the
//    latest size. Nothing else is merged, so a click between two moves is never reordered.
//  * The queue is bounded. When full, the oldest merge-able event (move, wheel, resize) is dropped to make room;
//    when there is none, the new event is dropped and counted. Quit, close and key events are therefore only
//    lost when the app stops polling altogether for a very long time.
#include "okay/Platform/Platform.hpp"
#include <cstddef>
#include <deque>

namespace okay::platform {

class EventQueue {
public:
    explicit EventQueue(size_t capacity = 4096) : m_cap(capacity < 2 ? 2 : capacity) {}

    void Push(const Event& e) {
        if (!m_q.empty() && Merge(m_q.back(), e)) return;
        if (m_q.size() >= m_cap) {
            bool made = false;
            for (auto it = m_q.begin(); it != m_q.end(); ++it) {
                if (Mergeable(it->type)) { m_q.erase(it); m_dropped++; made = true; break; }
            }
            if (!made) { m_dropped++; return; }
        }
        m_q.push_back(e);
    }
    bool Poll(Event& out) {
        if (m_q.empty()) return false;
        out = std::move(m_q.front());
        m_q.pop_front();
        return true;
    }
    size_t Size() const { return m_q.size(); }
    bool Empty() const { return m_q.empty(); }
    size_t Capacity() const { return m_cap; }
    /// Events lost to a full queue since the last Clear.
    size_t Dropped() const { return m_dropped; }
    void Clear() { m_q.clear(); m_dropped = 0; }
    /// Remove queued events of one window (it was destroyed).
    void DropWindow(uint32_t windowId) {
        for (auto it = m_q.begin(); it != m_q.end();) {
            if (it->windowId == windowId && it->type != EventType::Quit) it = m_q.erase(it); else ++it;
        }
    }

private:
    static bool Mergeable(EventType t) {
        return t == EventType::MouseMove || t == EventType::MouseWheel || t == EventType::WindowResized;
    }
    static bool Merge(Event& last, const Event& e) {
        if (last.type != e.type || last.windowId != e.windowId || !Mergeable(e.type)) return false;
        switch (e.type) {
        case EventType::MouseMove:
            last.dx += e.dx; last.dy += e.dy; last.x = e.x; last.y = e.y; last.timestampMs = e.timestampMs;
            return true;
        case EventType::MouseWheel:
            if (last.wheelFlipped != e.wheelFlipped) return false;
            last.wheelX += e.wheelX; last.wheelY += e.wheelY; last.timestampMs = e.timestampMs;
            return true;
        case EventType::WindowResized:
            last.width = e.width; last.height = e.height; last.timestampMs = e.timestampMs;
            return true;
        default: return false;
        }
    }
    std::deque<Event> m_q;
    size_t m_cap;
    size_t m_dropped = 0;
};

} // namespace okay::platform
