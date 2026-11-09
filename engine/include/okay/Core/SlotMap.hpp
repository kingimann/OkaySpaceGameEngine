#pragma once
// Generational handles and a slot map built on them. Header-only.
// See docs/core_runtime.md.
#include <cstddef>
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace okay {

/// A small copyable reference to something stored in a SlotMap: an index plus a
/// generation. When the slot is erased and reused the generation moves on, so an old
/// handle is recognised as stale instead of pointing at someone else's data. The
/// default handle (generation 0) is never valid. `Tag` only makes handles of different
/// maps different types, so they cannot be mixed up.
template <class Tag>
struct Handle {
    std::uint32_t index = 0;
    std::uint32_t generation = 0;

    constexpr Handle() = default;
    constexpr Handle(std::uint32_t i, std::uint32_t g) : index(i), generation(g) {}

    /// Whether this was ever handed out (not whether it is still alive; ask the map).
    constexpr bool Valid() const { return generation != 0; }
    constexpr explicit operator bool() const { return generation != 0; }

    /// The handle packed into one number (for save files, scripts or network messages).
    constexpr std::uint64_t Raw() const { return (static_cast<std::uint64_t>(generation) << 32) | index; }
    static constexpr Handle FromRaw(std::uint64_t raw) {
        return Handle(static_cast<std::uint32_t>(raw & 0xFFFFFFFFu), static_cast<std::uint32_t>(raw >> 32));
    }

    friend constexpr bool operator==(const Handle& a, const Handle& b) { return a.index == b.index && a.generation == b.generation; }
    friend constexpr bool operator!=(const Handle& a, const Handle& b) { return !(a == b); }
    friend constexpr bool operator<(const Handle& a, const Handle& b) { return a.Raw() < b.Raw(); }
};

/// Values in a dense array, found by Handle in O(1): Insert, Erase and lookup are all
/// constant time, and iteration walks only live values, packed together. Erasing moves
/// the last value into the hole, so addresses and iteration order are not stable; handles are.
/// T must be movable. Not thread safe.
template <class T, class Tag = T>
class SlotMap {
public:
    using HandleType = Handle<Tag>;
    using value_type = T;

    SlotMap() = default;

    HandleType Insert(const T& v) { return Emplace(v); }
    HandleType Insert(T&& v) { return Emplace(std::move(v)); }

    /// Construct a value in place and return its handle. Throws only if T's
    /// constructor or the memory allocation throws; the map is unchanged then.
    template <class... Args>
    HandleType Emplace(Args&&... args) {
        if (m_freeHead == kNone && m_slots.size() >= kMaxSlots) throw std::length_error("SlotMap is full");
        m_values.emplace_back(std::forward<Args>(args)...);
        const bool reuse = m_freeHead != kNone;
        std::uint32_t slot;
        try {
            m_slotOfDense.push_back(0);
            if (reuse) {
                slot = m_freeHead;
            } else {
                m_slots.push_back(Slot{});
                slot = static_cast<std::uint32_t>(m_slots.size() - 1);
            }
        } catch (...) {
            m_values.pop_back();
            if (m_slotOfDense.size() > m_values.size()) m_slotOfDense.pop_back();
            throw;
        }
        Slot& s = m_slots[slot];
        if (reuse) m_freeHead = s.link;
        s.link = static_cast<std::uint32_t>(m_values.size() - 1);
        s.live = true;
        m_slotOfDense.back() = slot;
        return HandleType(slot, s.generation);
    }

    /// Remove the value. False for a stale or invalid handle (nothing happens).
    bool Erase(HandleType h) {
        if (!Contains(h)) return false;
        const std::uint32_t slot = h.index;
        Slot& s = m_slots[slot];
        const std::size_t d = s.link;
        const std::size_t last = m_values.size() - 1;
        if (d != last) {
            m_values[d] = std::move(m_values[last]);
            const std::uint32_t movedSlot = m_slotOfDense[last];
            m_slotOfDense[d] = movedSlot;
            m_slots[movedSlot].link = static_cast<std::uint32_t>(d);
        }
        m_values.pop_back();
        m_slotOfDense.pop_back();
        Retire(slot);
        return true;
    }

    /// Erase every value for which `pred(handle, value)` is true. Returns how many.
    template <class Pred>
    std::size_t EraseIf(Pred pred) {
        std::size_t n = 0;
        std::size_t i = 0;
        while (i < m_values.size()) {
            const HandleType h = HandleAt(i);
            if (pred(h, m_values[i])) { Erase(h); ++n; }   // the last value now sits at i: look at it next
            else ++i;
        }
        return n;
    }

    bool Contains(HandleType h) const {
        if (h.index >= m_slots.size()) return false;
        const Slot& s = m_slots[h.index];
        return s.live && s.generation == h.generation;
    }

    /// The value, or nullptr for a stale handle.
    T* Get(HandleType h) { return Contains(h) ? &m_values[m_slots[h.index].link] : nullptr; }
    const T* Get(HandleType h) const { return Contains(h) ? &m_values[m_slots[h.index].link] : nullptr; }

    /// The value; throws std::out_of_range for a stale handle.
    T& At(HandleType h) {
        T* p = Get(h);
        if (!p) throw std::out_of_range("SlotMap: stale handle");
        return *p;
    }
    const T& At(HandleType h) const {
        const T* p = Get(h);
        if (!p) throw std::out_of_range("SlotMap: stale handle");
        return *p;
    }
    T& operator[](HandleType h) { return At(h); }
    const T& operator[](HandleType h) const { return At(h); }

    std::size_t Size() const { return m_values.size(); }
    bool Empty() const { return m_values.empty(); }
    /// Values the map can hold without growing.
    std::size_t Capacity() const { return m_values.capacity(); }
    void Reserve(std::size_t n) {
        m_values.reserve(n);
        m_slotOfDense.reserve(n);
        m_slots.reserve(n);
    }

    /// Remove everything. Every handle handed out so far becomes stale.
    void Clear() {
        for (std::size_t d = m_values.size(); d-- > 0;) Retire(m_slotOfDense[d]);
        m_values.clear();
        m_slotOfDense.clear();
    }

    // ---- iteration over the live values (dense order) ----
    using iterator = typename std::vector<T>::iterator;
    using const_iterator = typename std::vector<T>::const_iterator;
    iterator begin() { return m_values.begin(); }
    iterator end() { return m_values.end(); }
    const_iterator begin() const { return m_values.begin(); }
    const_iterator end() const { return m_values.end(); }

    /// The handle of the value at dense position `i` (0 <= i < Size()).
    HandleType HandleAt(std::size_t i) const {
        const std::uint32_t slot = m_slotOfDense[i];
        return HandleType(slot, m_slots[slot].generation);
    }
    T& ValueAt(std::size_t i) { return m_values[i]; }
    const T& ValueAt(std::size_t i) const { return m_values[i]; }

    /// `for (auto [handle, value] : map.Entries())`. Do not insert or erase while iterating.
    template <bool Const>
    class EntryRange {
        using Map = std::conditional_t<Const, const SlotMap, SlotMap>;
        using Ref = std::conditional_t<Const, const T&, T&>;
    public:
        struct Entry { HandleType handle; Ref value; };
        class iterator {
        public:
            iterator(Map* m, std::size_t i) : m_map(m), m_i(i) {}
            Entry operator*() const { return Entry{m_map->HandleAt(m_i), m_map->ValueAt(m_i)}; }
            iterator& operator++() { ++m_i; return *this; }
            bool operator!=(const iterator& o) const { return m_i != o.m_i; }
            bool operator==(const iterator& o) const { return m_i == o.m_i; }
        private:
            Map* m_map;
            std::size_t m_i;
        };
        explicit EntryRange(Map* m) : m_map(m) {}
        iterator begin() const { return iterator(m_map, 0); }
        iterator end() const { return iterator(m_map, m_map->Size()); }
    private:
        Map* m_map;
    };
    EntryRange<false> Entries() { return EntryRange<false>(this); }
    EntryRange<true> Entries() const { return EntryRange<true>(this); }

private:
    static constexpr std::uint32_t kNone = 0xFFFFFFFFu;
    static constexpr std::size_t kMaxSlots = 0xFFFFFFFEu;

    struct Slot {
        std::uint32_t generation = 1;   // moves on when the slot is freed; 0 is never used
        std::uint32_t link = kNone;     // live: dense index; free: the next free slot
        bool live = false;
    };

    // Free a slot: its old handles go stale. A slot whose generation would wrap is
    // retired for good rather than risk an old handle matching again.
    void Retire(std::uint32_t slot) {
        Slot& s = m_slots[slot];
        s.live = false;
        if (s.generation == 0xFFFFFFFFu) { s.link = kNone; return; }
        ++s.generation;
        s.link = m_freeHead;
        m_freeHead = slot;
    }

    std::vector<T> m_values;                   // dense
    std::vector<std::uint32_t> m_slotOfDense;  // dense index -> slot
    std::vector<Slot> m_slots;
    std::uint32_t m_freeHead = kNone;
};

} // namespace okay

namespace std {
template <class Tag>
struct hash<okay::Handle<Tag>> {
    size_t operator()(const okay::Handle<Tag>& h) const noexcept { return std::hash<std::uint64_t>()(h.Raw()); }
};
} // namespace std
