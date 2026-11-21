#include "test_framework.hpp"
#ifdef OKAY_QUAT_SLERP_HEADER_ONLY
#include "okay/Animation/AnimationClip.hpp"
#else
#include <Okay.hpp>
#endif
#include <cmath>
#include <string>

using namespace okay;

// Rotation tracks are four float curves (qx,qy,qz,qw). Linear rotation keys
// must play back as slerp (constant angular speed, shortest path), as glTF
// LINEAR requires, not as a per-component lerp + renormalize (nlerp), which
// runs fast in the middle of a big turn and slow at the ends.

// Rotation angle between two unit quaternions, from the chord |a -/+ b| =
// 2 sin(theta/4) (accurate near zero, where acos of the dot is not).
static float AngleDeg(const Quat& a, const Quat& b) {
    double m = std::sqrt(double(a.x-b.x)*(a.x-b.x) + double(a.y-b.y)*(a.y-b.y) + double(a.z-b.z)*(a.z-b.z) + double(a.w-b.w)*(a.w-b.w));
    double p = std::sqrt(double(a.x+b.x)*(a.x+b.x) + double(a.y+b.y)*(a.y+b.y) + double(a.z+b.z)*(a.z+b.z) + double(a.w+b.w)*(a.w+b.w));
    double c = std::fmin(std::fmin(m, p) * 0.5, 1.0);
    return (float)(4.0 * std::asin(c) * 57.29577951308232);
}

static void KeyQuat(AnimationClip& c, float t, const Quat& q) {
    c.AddKey("rotation.qx", t, q.x); c.AddKey("rotation.qy", t, q.y);
    c.AddKey("rotation.qz", t, q.z); c.AddKey("rotation.qw", t, q.w);
}

static Quat Sample(const AnimationClip& c, float t) {
    Vec3 p, s{1, 1, 1}; Quat r = Quat::Identity;
    c.SampleTRS(t, p, r, s);
    return r;
}

int main() {
    RUN_SUITE("anim_quat_slerp");
    const Vec3 axis = Vec3(0.3f, 1.0f, 0.2f).Normalized();

    // 1) Two linear keys, 170 and 90 degrees apart: angle must follow slerp.
    for (float turn : {170.0f, 90.0f}) {
        AnimationClip c; c.smoothKeys = false; c.loop = false;
        Quat q0 = Quat::AngleAxis(10.0f, axis), q1 = Quat::AngleAxis(10.0f + turn, axis);
        KeyQuat(c, 0.0f, q0); KeyQuat(c, 1.0f, q1);
        for (float t : {0.25f, 0.5f, 0.75f}) {
            float err = AngleDeg(Sample(c, t), Quat::Slerp(q0, q1, t));
            std::cout << "  turn " << turn << " t=" << t << " error vs slerp = " << err << " deg\n";
            CHECK(err < 0.1f);
        }
    }

    // 2) Sign continuity: second key stored as -q (same rotation). Must take
    //    the short way round, not spin through 360-90.
    {
        AnimationClip c; c.smoothKeys = false; c.loop = false;
        Quat q0 = Quat::Identity, q1 = Quat::AngleAxis(90.0f, axis);
        KeyQuat(c, 0.0f, q0); KeyQuat(c, 1.0f, Quat{-q1.x, -q1.y, -q1.z, -q1.w});
        float err = AngleDeg(Sample(c, 0.5f), Quat::AngleAxis(45.0f, axis));
        std::cout << "  flipped-sign key error = " << err << " deg\n";
        CHECK(err < 0.1f);
    }

    // 3) Smooth keys stay smooth and hit the keys.
    {
        AnimationClip c; c.smoothKeys = true; c.loop = false;
        for (int i = 0; i <= 4; ++i) KeyQuat(c, (float)i, Quat::AngleAxis(40.0f * i, axis));
        CHECK(AngleDeg(Sample(c, 2.0f), Quat::AngleAxis(80.0f, axis)) < 0.01f);
        float prev = -1.0f; bool mono = true;
        for (int i = 0; i <= 40; ++i) {
            float a = AngleDeg(Sample(c, i * 0.1f), Quat::Identity);
            if (a + 1e-3f < prev) mono = false;
            prev = a;
        }
        CHECK(mono);
    }

    // 4) Reduce on a dense rotation track measures error as an angle: a
    //    constant-speed 180-degree turn (60 keys) collapses to a few keys and
    //    the reduced clip stays within the stated tolerance everywhere.
    {
        const float tolDeg = 0.1f;
        AnimationClip dense; dense.smoothKeys = false; dense.loop = false;
        Quat q0 = Quat::Identity, q1 = Quat::AngleAxis(179.0f, axis);
        for (int i = 0; i <= 60; ++i) KeyQuat(dense, i / 60.0f, Quat::Slerp(q0, q1, i / 60.0f));
        AnimationClip red = dense;
        red.Reduce(0.0005f, tolDeg * 0.0174532925f, 0.0005f);
        std::size_t keys = red.Tracks().at("rotation.qw").Count();
        float maxErr = 0.0f;
        for (int i = 0; i <= 600; ++i) {
            float t = i / 600.0f;
            maxErr = std::fmax(maxErr, AngleDeg(Sample(red, t), Quat::Slerp(q0, q1, t)));
        }
        std::cout << "  reduce: 61 -> " << keys << " keys, max angular error " << maxErr << " deg (tol " << tolDeg << ")\n";
        CHECK(maxErr < tolDeg);
        CHECK(keys <= 4);
        // All four component tracks keep the same key times.
        for (const char* n : {"rotation.qx", "rotation.qy", "rotation.qz"}) CHECK(red.Tracks().at(n).Count() == keys);
    }

#ifndef OKAY_QUAT_SLERP_HEADER_ONLY
    // 5) Save + reopen keeps the rotation keys and their playback unchanged.
    {
        Scene s("A"); s.physicsEnabled = false;
        GameObject* g = s.CreateGameObject("Model");
        auto* ma = g->AddComponent<ModelAnimator>();
        ModelAnimator::Clip clip; clip.name = "Turn";
        AnimationClip ac; ac.smoothKeys = false;
        KeyQuat(ac, 0.0f, Quat::Identity); KeyQuat(ac, 1.0f, Quat::AngleAxis(170.0f, axis));
        clip.nodes.push_back({"Hips", ac});
        ma->clips.push_back(clip);
        Scene s2("B"); s2.physicsEnabled = false;
        CHECK(SceneSerializer::Deserialize(s2, SceneSerializer::Serialize(s)));
        auto* ma2 = s2.Find("Model") ? s2.Find("Model")->GetComponent<ModelAnimator>() : nullptr;
        CHECK(ma2 && ma2->clips.size() == 1);
        if (ma2 && !ma2->clips.empty()) {
            const AnimationClip& back = ma2->clips[0].nodes[0].clip;
            CHECK(back.Tracks().at("rotation.qw").Count() == 2);
            for (float t : {0.25f, 0.5f, 0.75f}) {
                float e = AngleDeg(Sample(back, t), Sample(ac, t));
                
                CHECK(e < 1e-3f);
            }
        }
    }

#endif
    TEST_MAIN_RESULT();
}
