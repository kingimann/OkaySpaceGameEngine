#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Animation/AnimationCurve.hpp"
#include "okay/Animation/AnimationClip.hpp"
#include "okay/Components/ModelAnimator.hpp"
#include <cmath>

using namespace okay;

// Playback quality of imported animation:
//  * smooth curves pass THROUGH dense keys (mocap) instead of easing to a stop
//    at every key (the old smoothstep-per-segment made motion stutter);
//  * every bone track of a model clip spans the whole clip, so a bone whose
//    keys end early holds its last value instead of looping on its own.
int main() {
    RUN_SUITE("anim_playback");

    // A steady ramp sampled every frame: the smooth curve should move at a
    // steady speed right at a key (no stop), and match the linear value.
    AnimationCurve ramp;
    for (int i = 0; i <= 30; ++i) ramp.AddKey(i / 30.0f, (float)i);
    const float h = 0.002f, k = 10.0f / 30.0f;
    const float speedAtKey = (ramp.Evaluate(k + h) - ramp.Evaluate(k - h)) / (2.0f * h);
    CHECK(std::fabs(speedAtKey - 30.0f) < 1.0f);            // ~30 units/s, not ~0
    CHECK(std::fabs(ramp.Evaluate(0.5f + 1.0f / 60.0f) - 15.5f) < 0.05f);

    // Two keys still ease in and out exactly like before.
    AnimationCurve two; two.AddKey(0.0f, 0.0f); two.AddKey(1.0f, 1.0f);
    CHECK(std::fabs(two.Evaluate(0.5f) - 0.5f) < 1e-4f);
    CHECK(std::fabs(two.Evaluate(0.25f) - 0.15625f) < 1e-4f);   // smoothstep(0.25)

    // No overshoot past a peak.
    AnimationCurve peak; peak.AddKey(0, 0); peak.AddKey(1, 1); peak.AddKey(2, 0);
    for (float t = 0; t <= 2.0f; t += 0.05f) CHECK(peak.Evaluate(t) <= 1.0f + 1e-5f);

    // Linear tracks (imported/baked data) interpolate linearly.
    AnimationClip lin; lin.smoothKeys = false;
    lin.AddKey("position.x", 0.0f, 0.0f); lin.AddKey("position.x", 1.0f, 1.0f);
    bool f = false;
    CHECK(std::fabs(lin.Evaluate("position.x", 0.25f, f) - 0.25f) < 1e-5f);

    // Spanned clip: a short track holds its last value up to the clip's end.
    ModelAnimator::Clip c;
    ModelAnimator::NodeClip arm{"arm", AnimationClip{}}, leg{"leg", AnimationClip{}};
    arm.clip.AddKey("position.x", 0.0f, 0.0f); arm.clip.AddKey("position.x", 0.4f, 4.0f);
    leg.clip.AddKey("position.x", 0.0f, 0.0f); leg.clip.AddKey("position.x", 1.0f, 1.0f);
    c.nodes.push_back(arm); c.nodes.push_back(leg);
    ModelAnimator::NormalizeClip(c);
    CHECK(std::fabs(c.nodes[0].clip.Length() - 1.0f) < 1e-5f);
    CHECK(std::fabs(c.nodes[0].clip.Evaluate("position.x", 0.7f, f) - 4.0f) < 1e-4f);   // held, not looped
    c.nodes[0].clip.RemoveTrack("position.y");
    CHECK(std::fabs(c.nodes[0].clip.Length() - 1.0f) < 1e-5f);                          // span survives

    TEST_MAIN_RESULT();
}
