#pragma once
// Player slots for hot-plugged controllers, as pure logic. A controller that was in
// slot 2 and is plugged back in gets slot 2 again when it is free; a new one takes the
// first free slot. No device, no SDL.
#include <map>
#include <string>
#include <vector>

namespace okay {

class PadSlotAllocator {
public:
    explicit PadSlotAllocator(int slots = 4) : m_key((size_t)slots), m_used((size_t)slots, false) {}
    int Size() const { return (int)m_used.size(); }
    /// The slot for a pad with this device key, or -1 when every slot is taken.
    int Acquire(const std::string& key) {
        auto it = m_last.find(key);
        if (it != m_last.end() && Free(it->second)) return Take(it->second, key);
        for (int i = 0; i < Size(); ++i) if (!m_used[(size_t)i]) return Take(i, key);
        return -1;
    }
    /// The pad in `slot` left; remembers the slot for its key.
    void Release(int slot) {
        if (slot < 0 || slot >= Size() || !m_used[(size_t)slot]) return;
        m_last[m_key[(size_t)slot]] = slot;
        m_used[(size_t)slot] = false; m_key[(size_t)slot].clear();
    }
    /// Exchange two slots (a player swaps pads). Remembered slots follow.
    void Swap(int a, int b) {
        if (a < 0 || b < 0 || a >= Size() || b >= Size() || a == b) return;
        std::swap(m_key[(size_t)a], m_key[(size_t)b]);
        const bool t = m_used[(size_t)a]; m_used[(size_t)a] = m_used[(size_t)b]; m_used[(size_t)b] = t;
        if (m_used[(size_t)a]) m_last[m_key[(size_t)a]] = a;
        if (m_used[(size_t)b]) m_last[m_key[(size_t)b]] = b;
    }
    bool Used(int slot) const { return slot >= 0 && slot < Size() && m_used[(size_t)slot]; }
    int Count() const { int n = 0; for (bool u : m_used) n += u; return n; }
private:
    bool Free(int s) const { return s >= 0 && s < Size() && !m_used[(size_t)s]; }
    int Take(int s, const std::string& key) { m_used[(size_t)s] = true; m_key[(size_t)s] = key; return s; }
    std::vector<std::string> m_key;
    std::vector<bool> m_used;
    std::map<std::string, int> m_last;
};

} // namespace okay
