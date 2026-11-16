#pragma once
// A vertex weld shared by the own model readers. Internal: not installed. Written for this engine (open addressing with
// linear probing over a table of slots, a 64-bit multiply-xorshift mix of the key words); no code of any other project
// was used.
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace okay {
namespace detail {

// A corner key of N 32-bit words (floats as their bits, compared bit for bit) -> the vertex made for it. The keys sit side by
// side in one array in the order they came; the hash table holds only their numbers (4 bytes a slot, at most half full),
// so a key costs 4N + 8 to 16 bytes and no allocation of its own. It replaces unordered_maps keyed by the key's bytes (as a
// std::string, or a struct): only the lookup changed, so the vertices and their order are the same.
template <int N>
struct WeldMap {
    std::vector<std::uint32_t> keys;   // N words per key, in the order they were added
    std::vector<int> vals;             // the vertex of each key
    std::vector<int> slot;             // -1 empty, else a key number
    static std::size_t Hash(const std::uint32_t* k) {
        std::uint64_t h = 0x9E3779B97F4A7C15ull;
        for (int i = 0; i < N; ++i) { h = (h ^ k[i]) * 0xBF58476D1CE4E5B9ull; h ^= h >> 31; }
        return (std::size_t)h;
    }
    // Room for `want` keys without growing.
    void Reserve(std::size_t want) {
        std::size_t cap = 64;
        while (cap < want * 2) cap *= 2;
        if (cap > slot.size()) Rehash(cap);
        keys.reserve(want * N);
        vals.reserve(want);
    }
    // The vertex of `k`, or `fresh` (stored for it) when the key is new; `added` says which.
    int Get(const std::uint32_t* k, int fresh, bool& added) {
        if ((vals.size() + 1) * 2 > slot.size()) Rehash(slot.size() < 32 ? 64 : slot.size() * 2);
        const std::size_t mask = slot.size() - 1;
        for (std::size_t i = Hash(k) & mask;; i = (i + 1) & mask) {
            const int s = slot[i];
            if (s < 0) {
                slot[i] = (int)vals.size();
                keys.insert(keys.end(), k, k + N);
                vals.push_back(fresh);
                added = true;
                return fresh;
            }
            if (std::memcmp(&keys[(std::size_t)s * N], k, sizeof(std::uint32_t) * N) == 0) { added = false; return vals[(std::size_t)s]; }
        }
    }
    // Frees everything (the map is then empty).
    void clear() { std::vector<std::uint32_t>().swap(keys); std::vector<int>().swap(vals); std::vector<int>().swap(slot); }
    void Rehash(std::size_t cap) {   // cap: a power of two, more than twice the count
        slot.assign(cap, -1);
        const std::size_t mask = cap - 1;
        for (std::size_t s = 0; s < vals.size(); ++s)
            for (std::size_t i = Hash(&keys[s * N]) & mask;; i = (i + 1) & mask) if (slot[i] < 0) { slot[i] = (int)s; break; }
    }
};

} // namespace detail
} // namespace okay
