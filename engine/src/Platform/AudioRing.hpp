#pragma once
// A byte FIFO for queue-mode audio devices (pure, no OS headers; tested on Linux). Push appends, Pop takes
// from the front; a capacity drops the OLDEST bytes (capture must not grow without bound when nobody reads).
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

namespace okay::platform {

class AudioRing {
public:
    explicit AudioRing(size_t maxBytes = 0) : m_max(maxBytes) {}   // 0 = unbounded
    void Push(const void* data, size_t n) {
        const uint8_t* b = static_cast<const uint8_t*>(data);
        if (m_max && n >= m_max) { b += n - m_max; n = m_max; m_head = m_size = 0; }   // only the newest m_max survive
        if (m_max && m_size + n > m_max) Drop(m_size + n - m_max);
        Reserve(m_size + n);
        const size_t cap = m_buf.size();
        size_t tail = (m_head + m_size) % cap;
        for (size_t i = 0; i < n;) {
            const size_t chunk = std::min(n - i, cap - tail);
            std::memcpy(m_buf.data() + tail, b + i, chunk);
            i += chunk; tail = (tail + chunk) % cap;
        }
        m_size += n;
    }
    size_t Pop(void* out, size_t n) {
        n = std::min(n, m_size);
        uint8_t* o = static_cast<uint8_t*>(out);
        const size_t cap = m_buf.size();
        for (size_t i = 0; i < n;) {
            const size_t chunk = std::min(n - i, cap - m_head);
            std::memcpy(o + i, m_buf.data() + m_head, chunk);
            i += chunk; m_head = (m_head + chunk) % cap;
        }
        m_size -= n;
        if (m_size == 0) m_head = 0;
        return n;
    }
    void Clear() { m_head = m_size = 0; }
    size_t Size() const { return m_size; }
private:
    void Drop(size_t n) { n = std::min(n, m_size); if (m_buf.empty()) return; m_head = (m_head + n) % m_buf.size(); m_size -= n; }
    void Reserve(size_t need) {
        if (need <= m_buf.size()) return;
        size_t cap = std::max<size_t>(m_buf.size() * 2, std::max<size_t>(need, 4096));
        if (m_max) cap = std::max(need, std::min(cap, m_max));
        std::vector<uint8_t> nb(cap);
        const size_t oldCap = m_buf.size();
        for (size_t i = 0; i < m_size; ++i) nb[i] = m_buf[(m_head + i) % oldCap];
        m_buf.swap(nb);
        m_head = 0;
    }
    std::vector<uint8_t> m_buf;
    size_t m_head = 0, m_size = 0, m_max;
};

// Fill `buf` (bytes long) for a playback period: from the user's callback, else from the ring (silence for
// what the ring cannot supply, like SDL's underrun). Returns how many bytes were real queued audio.
template <class CallbackFn>
size_t FillPlaybackPeriod(uint8_t* buf, size_t bytes, bool haveCallback, CallbackFn&& cb, AudioRing& ring) {
    if (haveCallback) { std::memset(buf, 0, bytes); cb(buf, bytes); return bytes; }
    const size_t got = ring.Pop(buf, bytes);
    if (got < bytes) std::memset(buf + got, 0, bytes - got);
    return got;
}

} // namespace okay::platform
