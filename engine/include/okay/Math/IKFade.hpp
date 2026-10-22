#pragma once
// ---------------------------------------------------------------------------
// IKFade — eases an IK component's influence in and out so switching IK on or
// off (grab a ledge, let go, start aiming) blends instead of popping. Each IK
// component multiplies its weight by Step(): active -> 1, inactive -> 0, over
// `fadeTime` seconds with a smoothstep curve (fadeTime 0 = instant).
// ---------------------------------------------------------------------------
namespace okay {

struct IKFade {
    float value = -1.0f;   ///< linear 0..1 progress (-1 = not started: snaps to the target)
    float Step(bool active, float fadeTime, float dt) {
        const float want = active ? 1.0f : 0.0f;
        if (value < 0.0f || fadeTime <= 1e-4f) value = want;
        else {
            const float d = dt / fadeTime;
            value = value < want ? (value + d > want ? want : value + d)
                                 : (value - d < want ? want : value - d);
        }
        return value * value * (3.0f - 2.0f * value);
    }
};

} // namespace okay
