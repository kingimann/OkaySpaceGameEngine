#pragma once
// Moving keys on the Timeline window's key tracks (Transform, Letterbox, Shake, Fade). ImGui-free, so
// tests/test_stop_save_timeline_lod.cpp checks it headless. A key on these tracks is found by its time,
// so two keys can never share a time.
#include "okay/Components/Sequence.hpp"
#include <cstddef>
#include <vector>

namespace oked::tlkeys {

// Move every channel key at `from` to `to`; a key already at `to` is replaced.
inline bool MoveTransformKeys(okay::SeqTransformTrack& tr, float from, float to) {
    for (std::vector<okay::SeqKey>* ch : {&tr.position, &tr.rotation, &tr.scale}) {
        const int at = okay::Sequence::KeyAt(*ch, from);
        if (at < 0) continue;
        okay::SeqKey k = (*ch)[(std::size_t)at];
        ch->erase(ch->begin() + at);
        const int landing = okay::Sequence::KeyAt(*ch, to);
        if (landing >= 0) ch->erase(ch->begin() + landing);
        k.time = to;
        std::size_t i = 0;
        while (i < ch->size() && (*ch)[i].time < to) ++i;
        ch->insert(ch->begin() + (std::ptrdiff_t)i, k);
    }
    return true;
}

inline bool MoveFloatKeys(std::vector<okay::SeqFloatKey>& keys, float from, float to) {
    const int at = okay::Sequence::FloatKeyAt(keys, from);
    if (at < 0) return true;
    okay::SeqFloatKey k = keys[(std::size_t)at];
    keys.erase(keys.begin() + at);
    const int landing = okay::Sequence::FloatKeyAt(keys, to);
    if (landing >= 0) keys.erase(keys.begin() + landing);
    k.time = to;
    std::size_t i = 0;
    while (i < keys.size() && keys[i].time < to) ++i;
    keys.insert(keys.begin() + (std::ptrdiff_t)i, k);
    return true;
}

inline bool MoveColorKeys(std::vector<okay::SeqColorKey>& keys, float from, float to) {
    const int at = okay::Sequence::ColorKeyAt(keys, from);
    if (at < 0) return true;
    okay::SeqColorKey k = keys[(std::size_t)at];
    keys.erase(keys.begin() + at);
    const int landing = okay::Sequence::ColorKeyAt(keys, to);
    if (landing >= 0) keys.erase(keys.begin() + landing);
    k.time = to;
    std::size_t i = 0;
    while (i < keys.size() && keys[i].time < to) ++i;
    keys.insert(keys.begin() + (std::ptrdiff_t)i, k);
    return true;
}

}  // namespace oked::tlkeys
