#pragma once
// Moving keys on the Timeline window's key tracks (Transform, Letterbox, Shake, Fade). ImGui-free, so
// tests/test_stop_save_timeline_lod.cpp checks it headless.
//
// A key on these tracks is found by its time, so two keys can never share a time. A move onto a time
// another key already has is refused (nothing changes, false comes back): the window's drag runs a move
// on every frame the mouse time changes, and replacing the key there (the old rule) deleted every key
// the dragged one passed over (issue #888, finding 76). Refusing means the dragged key waits one frame
// short of the other key and carries on past it on the next frame; nothing is ever deleted.
#include "okay/Components/Sequence.hpp"
#include <cmath>
#include <cstddef>
#include <initializer_list>
#include <vector>

namespace oked::tlkeys {

constexpr float kSameTime = 1e-3f;   // the tolerance Sequence::KeyAt uses

// True when a key other than the one at `from` sits at `to`.
template <class Key> bool Occupied(const std::vector<Key>& keys, float from, float to) {
    for (const Key& k : keys)
        if (std::fabs(k.time - to) <= kSameTime && std::fabs(k.time - from) > kSameTime) return true;
    return false;
}

// Take the key at `from` out and put it back at `to`, in time order. The caller checked `to` is free.
template <class Key> void Relocate(std::vector<Key>& keys, int at, float to) {
    Key k = keys[(std::size_t)at];
    keys.erase(keys.begin() + at);
    k.time = to;
    std::size_t i = 0;
    while (i < keys.size() && keys[i].time < to) ++i;
    keys.insert(keys.begin() + (std::ptrdiff_t)i, k);
}

// Move every channel's key at `from` to `to` together. False (and no change) when any channel has
// another key at `to`.
inline bool MoveTransformKeys(okay::SeqTransformTrack& tr, float from, float to) {
    for (const std::vector<okay::SeqKey>* ch : {&tr.position, &tr.rotation, &tr.scale})
        if (Occupied(*ch, from, to)) return false;
    for (std::vector<okay::SeqKey>* ch : {&tr.position, &tr.rotation, &tr.scale}) {
        const int at = okay::Sequence::KeyAt(*ch, from);
        if (at >= 0) Relocate(*ch, at, to);
    }
    return true;
}

inline bool MoveFloatKeys(std::vector<okay::SeqFloatKey>& keys, float from, float to) {
    if (Occupied(keys, from, to)) return false;
    const int at = okay::Sequence::FloatKeyAt(keys, from);
    if (at >= 0) Relocate(keys, at, to);
    return true;
}

inline bool MoveColorKeys(std::vector<okay::SeqColorKey>& keys, float from, float to) {
    if (Occupied(keys, from, to)) return false;
    const int at = okay::Sequence::ColorKeyAt(keys, from);
    if (at >= 0) Relocate(keys, at, to);
    return true;
}

// Camera shake keeps two lists keyed together: move both or neither.
inline bool MoveFloatKeyPair(std::vector<okay::SeqFloatKey>& a, std::vector<okay::SeqFloatKey>& b, float from, float to) {
    if (Occupied(a, from, to) || Occupied(b, from, to)) return false;
    MoveFloatKeys(a, from, to);
    MoveFloatKeys(b, from, to);
    return true;
}

}  // namespace oked::tlkeys
