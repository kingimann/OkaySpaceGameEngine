#include "okay/Components/AnimControl.hpp"
#include "okay/Components/Character.hpp"
#include "okay/Components/ModelAnimator.hpp"
#include "okay/Core/Log.hpp"
#include <cmath>

namespace okay {

bool PlayClipFrom(GameObject* from, const std::string& clip, bool& warned) {
    if (!from || clip.empty()) return false;
    Character* c = FindAnimComponent<Character>(from);
    ModelAnimator* m = FindAnimComponent<ModelAnimator>(from);
    if (!c && !m) {
        if (!warned) {
            warned = true;
            Log::Warning("play_clip(\"", clip, "\") on '", from->name,
                         "': no Character or Model Animator on this object, its children or its parents");
        }
        return false;
    }
    if (c && c->PlayClip(clip)) return true;                  // a Character's own clip
    if (m && m->PlayHeld(clip)) return true;                  // the imported model's clip
    if (c) {                                                  // a built-in animation ("Wave", "Point", ...)
        const int a = Character::BuiltinAnimIndex(clip);
        if (a > 0) { c->HoldAnim(a); return true; }
    }
    return false;
}

void StopClipFrom(GameObject* from) {
    if (Character* c = FindAnimComponent<Character>(from)) { c->StopClip(); c->ReleaseAnim(); }
    if (ModelAnimator* m = FindAnimComponent<ModelAnimator>(from)) m->ReleaseHold();
}

void SetClipSpeedFrom(GameObject* from, float speed) {
    // A script can hand in NaN. Playback only runs forward, up to 10x.
    if (!std::isfinite(speed)) return;
    if (speed < 0.0f) speed = 0.0f;
    if (speed > 10.0f) speed = 10.0f;
    if (Character* c = FindAnimComponent<Character>(from)) c->animSpeed = speed;
    if (ModelAnimator* m = FindAnimComponent<ModelAnimator>(from)) m->speed = speed;
}

float ClipNormalizedFrom(GameObject* from) {
    if (Character* c = FindAnimComponent<Character>(from); c && c->IsPlayingClip()) return c->ClipNormalizedTime();
    ModelAnimator* m = FindAnimComponent<ModelAnimator>(from);
    if (!m) return 0.0f;
    const float len = m->ClipLength(m->active);
    if (len <= 0.0f) return 0.0f;
    const float t = m->Time() / len;
    return t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
}

} // namespace okay
