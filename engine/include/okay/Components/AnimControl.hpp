#pragma once
// ---------------------------------------------------------------------------
// What a script or visual script on an object animates, and the clip commands
// both use (play_clip / stop_clip / clip_speed / clip_normalized and the Play
// Clip node), so a text script and a graph behave the same.
//
// The animation component is looked for on the object itself, then its
// children breadth-first (a player whose imported model sits underneath it),
// then its parents (a script on one mesh part of a model).
// ---------------------------------------------------------------------------
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include <deque>
#include <string>

namespace okay {

class Character;
class ModelAnimator;

/// The nearest T to `from`: itself, then its children breadth-first, then its parents.
template <typename T>
T* FindAnimComponent(GameObject* from) {
    if (!from) return nullptr;
    if (T* c = from->GetComponent<T>()) return c;
    std::deque<Transform*> next;
    if (from->transform) for (Transform* c : from->transform->Children()) next.push_back(c);
    while (!next.empty()) {
        Transform* t = next.front();
        next.pop_front();
        if (!t || !t->gameObject) continue;
        if (T* c = t->gameObject->GetComponent<T>()) return c;
        for (Transform* k : t->Children()) next.push_back(k);
    }
    for (Transform* t = from->transform ? from->transform->Parent() : nullptr; t; t = t->Parent())
        if (t->gameObject)
            if (T* c = t->gameObject->GetComponent<T>()) return c;
    return nullptr;
}

/// Play `clip` on what `from` animates, and hold it: a Character's own clip, else
/// the model's ModelAnimator clip, else a Character built-in animation by name
/// ("Wave", "Point", ...). Movement-driven animation and the humanoid retarget
/// stand down until StopClipFrom. False if nothing has a clip by that name.
/// `warned` is set (and a console warning logged) the first time `from` has no
/// Character or ModelAnimator at all.
bool PlayClipFrom(GameObject* from, const std::string& clip, bool& warned);
/// Release a held clip: the controller / locomotion picks the animation again.
void StopClipFrom(GameObject* from);
/// Playback speed of what `from` animates (1 = normal).
void SetClipSpeedFrom(GameObject* from, float speed);
/// How far through its current clip `from`'s animation is (0..1).
float ClipNormalizedFrom(GameObject* from);

} // namespace okay
