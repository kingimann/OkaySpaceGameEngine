// First-person viewmodel motion layer (engine/include/okay/Components/FpViewmodelMotion.hpp), the pure part.
// Steps the model for thousands of frames through every state the owner's hands go through (idle, start and stop
// walking, sprint toggle, jump and land, crouch, aim down sights, wall, grip changes, named events) and checks that the
// offset and rotation never jump from one frame to the next, that events play and end, and that Reduce Motion scales
// the motion. Written from the engine's own header; no outside code.
#include "test_framework.hpp"
#include <Okay.hpp>
#include <cmath>
#include <cstdio>
#include <string>

using namespace okay;

namespace {
const float kDt = 1.0f / 60.0f;

struct Track {
    FpViewmodelMotion m;
    FpMotionInput in;
    FpMotionPose prev;
    bool have = false;
    float maxPos = 0.0f, maxRot = 0.0f;   // largest frame-to-frame change seen
    float lo = 1e9f, hi = -1e9f;          // range of pos.y
    float stride = 0.0f;
    void Frame() {
        in.dt = kDt;
        stride += in.stepping ? kDt * (0.9f + 0.9f * in.speed01) : 0.0f;
        in.phase = stride - std::floor(stride);
        const FpMotionPose& p = m.Step(in);
        if (have) {
            maxPos = std::fmax(maxPos, (p.pos - prev.pos).Magnitude());
            maxRot = std::fmax(maxRot, (p.euler - prev.euler).Magnitude());
        }
        lo = std::fmin(lo, p.pos.y); hi = std::fmax(hi, p.pos.y);
        prev = p; have = true;
    }
    void Run(int frames) { for (int i = 0; i < frames; ++i) Frame(); }
    void ResetStats() { maxPos = maxRot = 0.0f; lo = 1e9f; hi = -1e9f; }
};
// Thresholds. Ambient motion and state changes are slow; one-shot kicks are quicker (a fire kick is about 2 frames).
// A pop would be a few centimetres or several degrees in one frame; a spring blend of a 14 degree pose is about 1 degree a frame.
const float kPosAmbient = 0.012f, kRotAmbient = 2.0f;
const float kPosKick = 0.035f, kRotKick = 6.0f;

void Walk(Track& t, float speed, bool on) { t.in.stepping = on; t.in.speed01 = on ? speed : 0.0f; }
}

int main() {
    RUN_SUITE("fp_motion");

    // names and aliases
    {
        CHECK(FpEventFromName("equip") == FpEventKind::Equip);
        CHECK(FpEventFromName("UNEQUIP") == FpEventKind::Unequip);
        CHECK(FpEventFromName("use") == FpEventKind::Use);
        CHECK(FpEventFromName("place") == FpEventKind::Use);
        CHECK(FpEventFromName("swing") == FpEventKind::Swing);
        CHECK(FpEventFromName("dig") == FpEventKind::Swing);
        CHECK(FpEventFromName("fire") == FpEventKind::Fire);
        CHECK(FpEventFromName("reload") == FpEventKind::Reload);
        CHECK(FpEventFromName("nonsense") == FpEventKind::None);
        FpViewmodelMotion m;
        CHECK(!m.Trigger("nonsense"));
        CHECK(m.Trigger("dig"));
        CHECK(m.Playing(FpEventKind::Swing));
    }

    // idle: breathing moves, but only a little, and smoothly
    {
        Track t;
        t.Run(1200);
        t.ResetStats();
        t.Run(1200);
        std::printf("  idle: pos.y range %.4f..%.4f, max step %.5f m, %.4f deg\n", t.lo, t.hi, t.maxPos, t.maxRot);
        CHECK(t.hi - t.lo > 0.004f);        // it breathes
        CHECK(t.hi - t.lo < 0.03f);         // but does not wander
        CHECK(t.maxPos < 0.0006f);
        CHECK(t.maxRot < 0.05f);
    }

    // start, walk, sprint toggle on and off, stop: continuous through every blend
    {
        Track t;
        t.Run(120);
        Walk(t, 0.4f, true);  t.Run(240);           // start walking
        Walk(t, 1.0f, true);  t.in.sprint = 1.0f; t.Run(240);   // sprint on
        t.in.sprint = 0.0f;   t.Run(240);           // sprint off
        Walk(t, 0.0f, false); t.Run(240);           // stop
        std::printf("  walk/sprint/stop: max step %.5f m, %.4f deg\n", t.maxPos, t.maxRot);
        CHECK(t.maxPos < kPosAmbient);
        CHECK(t.maxRot < kRotAmbient);
    }
    // the stride: with the feet moving the hands bob in time with the phase, and more at a run
    {
        Track walk, run;
        Walk(walk, 0.45f, true); walk.Run(300); walk.ResetStats(); walk.Run(300);
        Walk(run, 1.0f, true); run.in.sprint = 1.0f; run.Run(300); run.ResetStats(); run.Run(300);
        // Remove the sprint pose offset: compare the swing (range) only.
        const float wr = walk.hi - walk.lo, rr = run.hi - run.lo;
        std::printf("  bob range walk %.4f run %.4f\n", wr, rr);
        CHECK(wr > 0.006f);
        CHECK(rr > wr);
        // The bob repeats exactly once per stride (phase 0 and 1 are the same point).
        Track a;
        Walk(a, 0.6f, true); a.Run(400);
        a.in.phase = 0.0f; a.stride = 0.0f;
        FpViewmodelMotion b = a.m;
        a.in.dt = kDt; b.Step(a.in);
        FpMotionInput i2 = a.in; i2.phase = 1.0f;
        FpViewmodelMotion c = a.m;
        const FpMotionPose p0 = FpViewmodelMotion(a.m).Step(a.in);
        const FpMotionPose p1 = c.Step(i2);
        CHECK_NEAR(p0.pos.y, p1.pos.y, 1e-4f);
        CHECK_NEAR(p0.euler.y, p1.euler.y, 1e-3f);
    }
    // jump, fall, land, takeoff dip
    {
        Track t;
        Walk(t, 0.7f, true); t.Run(180);
        t.in.jumpCrouch = 0.0f;
        for (int i = 0; i < 12; ++i) { t.in.jumpCrouch = std::sin(3.14159f * i / 12.0f); t.Frame(); }
        t.in.jumpCrouch = 0.0f;
        t.in.airborne = true; t.in.rising = true; t.Run(20);
        t.in.rising = false; t.Run(30);
        t.in.airborne = false;
        for (int i = 0; i < 30; ++i) { t.in.landK = std::sin(3.14159f * i / 30.0f); t.Frame(); }
        t.in.landK = 0.0f; t.Run(120);
        std::printf("  jump/land: max step %.5f m, %.4f deg\n", t.maxPos, t.maxRot);
        CHECK(t.maxPos < kPosKick);
        CHECK(t.maxRot < kRotKick);
        // A landing dips the hands below where they were.
        Track l;
        l.Run(200);
        const float before = l.prev.pos.y;
        float low = before;
        for (int i = 0; i < 30; ++i) { l.in.landK = std::sin(3.14159f * i / 30.0f); l.Frame(); low = std::fmin(low, l.prev.pos.y); }
        CHECK(low < before - 0.02f);
    }
    // crouch down and up; slide is the same stance level held low
    {
        Track t;
        t.Run(60);
        const float stand = t.prev.pos.y;
        for (int i = 0; i < 60; ++i) { t.in.stance = i / 59.0f; t.Frame(); }
        t.Run(120);
        const float crouched = t.prev.pos.y;
        t.in.stance = 0.0f; t.Run(180);
        std::printf("  crouch: stand %.4f crouched %.4f, max step %.5f\n", stand, crouched, t.maxPos);
        CHECK(crouched < stand - 0.012f);
        CHECK(t.maxPos < kPosAmbient);
        CHECK(t.maxRot < kRotAmbient);
        // A snap from stand to crouch (a controller that does not ease its stance) is still smooth.
        Track s;
        s.Run(60); s.ResetStats();
        s.in.stance = 1.0f; s.Run(60);
        CHECK(s.maxPos < kPosAmbient);
    }
    // strafe tilt goes with the side you move to and ends when you stop
    {
        Track r, l;
        Walk(r, 0.6f, true); r.in.strafe = 1.0f; r.Run(240);
        Walk(l, 0.6f, true); l.in.strafe = -1.0f; l.Run(240);
        CHECK(std::fabs(r.prev.euler.z - l.prev.euler.z) > 0.0f);   // differs by side (the stride adds its own roll)
        Track q;
        Walk(q, 0.6f, true); q.Run(100); q.ResetStats();
        q.in.strafe = 1.0f; q.Run(60); q.in.strafe = -1.0f; q.Run(60); q.in.strafe = 0.0f; q.Run(60);
        CHECK(q.maxPos < kPosAmbient);
        CHECK(q.maxRot < kRotAmbient);
    }
    // aim down sights: only a pistol or rifle grip moves, toward the centre, and it is smooth both ways
    {
        Track t;
        t.in.grip = FpGripKind::Rifle; t.Run(120);
        const float x0 = t.prev.pos.x;
        t.ResetStats();
        t.in.aim = true; t.Run(90);
        const float x1 = t.prev.pos.x;
        t.in.aim = false; t.Run(90);
        std::printf("  aim: x %.4f -> %.4f, max step %.5f\n", x0, x1, t.maxPos);
        CHECK(x1 < x0 - 0.08f);                // side = +1 (right hand): moves left, toward the centre
        CHECK(t.maxPos < kPosAmbient);
        Track e;
        e.in.grip = FpGripKind::Empty; e.Run(60);
        const float xe = e.prev.pos.x;
        e.in.aim = true; e.Run(90);
        CHECK_NEAR(e.prev.pos.x, xe, 0.01f);   // empty hands do not aim
        Track m;                                // left hand: mirrored
        m.in.side = -1.0f; m.in.grip = FpGripKind::Pistol; m.Run(100);
        const float m0 = m.prev.pos.x; m.in.aim = true; m.Run(90);
        CHECK(m.prev.pos.x > m0 + 0.05f);
    }
    // a surface close in front pulls the hands back, lower and up-tilted, smoothly; leaving it eases them out
    {
        Track t;
        t.Run(100);
        const FpMotionPose free = t.prev;
        t.ResetStats();
        t.in.wall = 1.0f; t.Run(60);
        const FpMotionPose near = t.prev;
        t.in.wall = 0.0f; t.Run(90);
        std::printf("  wall: dy %.3f dz %.3f dpitch %.1f, max step %.5f\n", near.pos.y - free.pos.y, near.pos.z - free.pos.z,
                    near.euler.x - free.euler.x, t.maxPos);
        CHECK(near.pos.y < free.pos.y - 0.08f);
        CHECK(near.pos.z > free.pos.z + 0.06f);
        CHECK(near.euler.x > free.euler.x + 15.0f);
        CHECK(t.maxPos < kPosKick);        // a wall that appears at once is the worst case: 0 to 1 in one step
        CHECK(t.maxRot < kRotKick);
    }
    // grips: every kind sits somewhere different, and changing kind glides (rifle -> empty -> tool -> pistol)
    {
        Track t;
        const FpGripKind order[] = {FpGripKind::Rifle, FpGripKind::Empty, FpGripKind::Tool, FpGripKind::Pistol,
                                    FpGripKind::TwoHand, FpGripKind::OneHand, FpGripKind::Empty};
        for (FpGripKind k : order) { t.in.grip = k; t.Run(90); }
        CHECK(t.maxPos < kPosAmbient);
        CHECK(t.maxRot < kRotAmbient);
        Track a, b;
        a.in.grip = FpGripKind::Rifle; a.Run(200);
        b.in.grip = FpGripKind::Pistol; b.Run(200);
        CHECK((a.prev.pos - b.prev.pos).Magnitude() > 0.015f);
        CHECK(std::string(FpGripName(FpGripKind::TwoHand)) == "two-handed");
    }
    // named events: each plays, returns to rest, and starts and ends without a jump
    {
        const FpEventKind kinds[] = {FpEventKind::Equip, FpEventKind::Unequip, FpEventKind::Use, FpEventKind::Swing,
                                     FpEventKind::Fire, FpEventKind::Reload};
        for (FpEventKind k : kinds) {
            Track t, base;      // base never gets the event: the same breathing, so the difference is the event alone
            t.in.grip = FpGripKind::Rifle; base.in.grip = FpGripKind::Rifle;
            t.Run(300); base.Run(300);
            FpMotionPose rest = base.prev;
            t.ResetStats();
            CHECK(t.m.Trigger(k));
            CHECK(t.m.Playing(k));
            float far = 0.0f;
            const int frames = (int)(FpViewmodelMotion::Seconds(k) * 60.0f) + 8;
            for (int i = 0; i < frames; ++i) { t.Frame(); base.Frame(); far = std::fmax(far, (t.prev.pos - base.prev.pos).Magnitude()); }
            CHECK(!t.m.Playing(k));
            t.Run(120); base.Run(120);
            std::printf("  event %-8s: reach %.3f m, max step %.4f m / %.2f deg\n", FpEventName(k), far, t.maxPos, t.maxRot);
            CHECK(far > 0.015f);
            CHECK(t.maxPos < kPosKick);
            CHECK(t.maxRot < kRotKick);
            CHECK((t.prev.pos - base.prev.pos).Magnitude() < 0.001f);    // back at rest
        }
        // Several at once (fire while reloading) and re-triggering while it plays stays smooth.
        Track t;
        t.Run(100); t.ResetStats();
        t.m.Trigger("reload"); t.Run(30); t.m.Trigger("fire"); t.Run(5); t.m.Trigger("fire"); t.Run(10); t.m.Trigger("swing");
        t.Run(200);
        CHECK(t.maxPos < kPosKick * 1.5f);
        CHECK(!t.m.AnyPlaying());
        // A stronger event reaches further.
        Track a, b;
        a.Run(100); b.Run(100);
        a.m.Trigger(FpEventKind::Swing, 1.0f); b.m.Trigger(FpEventKind::Swing, 0.5f);
        float ra = 0, rb = 0;
        const FpMotionPose r0 = a.prev;
        for (int i = 0; i < 40; ++i) { a.Frame(); b.Frame(); ra = std::fmax(ra, (a.prev.pos - r0.pos).Magnitude()); rb = std::fmax(rb, (b.prev.pos - r0.pos).Magnitude()); }
        CHECK(ra > rb * 1.5f);
    }

    // Reduce Motion: ambient motion is gone, events shrink to a third, poses that show a state stay.
    {
        auto bob = [&](bool reduce) {
            Track t; t.in.reduceMotion = reduce; Walk(t, 0.8f, true); t.Run(300); t.ResetStats(); t.Run(300);
            return t.hi - t.lo;
        };
        const float on = bob(false), off = bob(true);
        std::printf("  bob range: normal %.4f, reduce motion %.4f\n", on, off);
        CHECK(on > 0.008f);
        CHECK(off < on * 0.25f);
        auto breathe = [&](bool reduce) {
            Track t; t.in.reduceMotion = reduce; t.Run(600); t.ResetStats(); t.Run(1200); return t.hi - t.lo;
        };
        CHECK(breathe(true) < breathe(false) * 0.1f);
        auto swingReach = [&](bool reduce) {
            Track t; t.in.reduceMotion = reduce; t.Run(200); const FpMotionPose r = t.prev;
            t.m.Trigger("swing"); float far = 0;
            for (int i = 0; i < 40; ++i) { t.Frame(); far = std::fmax(far, (t.prev.pos - r.pos).Magnitude()); }
            return far;
        };
        const float sn = swingReach(false), sr = swingReach(true);
        std::printf("  swing reach: normal %.3f, reduce motion %.3f\n", sn, sr);
        CHECK(sr < sn * 0.4f);
        CHECK(sr > 0.0f);                       // the action still reads
        Track c; c.in.reduceMotion = true; c.Run(60); const float y0 = c.prev.pos.y;
        c.in.stance = 1.0f; c.Run(90);
        CHECK(c.prev.pos.y < y0 - 0.012f);      // the crouch pose still shows
        Track r; r.in.reduceMotion = true; Walk(r, 1.0f, true); r.in.sprint = 1.0f; r.Run(120);
        CHECK(r.prev.euler.x > 8.0f);           // so does the sprint pose
        // flipping it mid-walk eases, it does not snap
        Track f; Walk(f, 0.8f, true); f.Run(300); f.ResetStats();
        f.in.reduceMotion = true; f.Run(120);
        std::printf("  flip reduce motion mid-walk: max step %.5f m, %.3f deg\n", f.maxPos, f.maxRot);
        CHECK(f.maxPos < kPosAmbient * 2.0f);
        CHECK(f.maxRot < kRotAmbient * 2.0f);
    }

    // Settings scale: bobAmount 0 = no bob, punchAmount 0 = no kicks, swayAmount 0 = no breathing
    {
        Track t; t.in.bobAmount = 0.0f; Walk(t, 0.8f, true); t.Run(300); t.ResetStats(); t.Run(300);
        CHECK(t.hi - t.lo < 0.003f);
        Track p, q; p.in.punchAmount = 0.0f; p.Run(100); q.Run(100);
        p.m.Trigger("swing"); float far = 0;
        for (int i = 0; i < 40; ++i) { p.Frame(); q.Frame(); far = std::fmax(far, (p.prev.pos - q.prev.pos).Magnitude()); }
        CHECK(far < 0.002f);
    }

    // A long random walk through every input never produces a NaN or a runaway, and a huge dt is clamped.
    {
        Track t;
        unsigned seed = 12345;
        auto rnd = [&]() { seed = seed * 1664525u + 1013904223u; return (float)((seed >> 8) & 0xFFFF) / 65535.0f; };
        for (int i = 0; i < 20000; ++i) {
            if (i % 90 == 0) {
                t.in.stepping = rnd() > 0.4f; t.in.speed01 = rnd() * 1.3f; t.in.sprint = rnd() > 0.7f ? 1.0f : 0.0f;
                t.in.airborne = rnd() > 0.85f; t.in.rising = rnd() > 0.5f; t.in.stance = rnd() > 0.7f ? 1.0f : 0.0f;
                t.in.aim = rnd() > 0.5f; t.in.wall = rnd() > 0.7f ? 1.0f : 0.0f; t.in.strafe = rnd() * 2.0f - 1.0f;
                t.in.grip = (FpGripKind)(int)(rnd() * 5.99f);
                if (rnd() > 0.7f) t.m.Trigger((FpEventKind)(1 + (int)(rnd() * 5.99f)));
            }
            t.Frame();
            CHECK(std::isfinite(t.prev.pos.x) && std::isfinite(t.prev.pos.y) && std::isfinite(t.prev.pos.z));
            CHECK(t.prev.pos.Magnitude() < 0.6f);
            CHECK(t.prev.euler.Magnitude() < 120.0f);
        }
        std::printf("  random 20000 frames: max step %.4f m, %.3f deg\n", t.maxPos, t.maxRot);
        CHECK(t.maxPos < kPosKick * 2.0f);
        FpViewmodelMotion m;
        FpMotionInput in; in.dt = 5.0f;
        const FpMotionPose p = m.Step(in);
        CHECK(p.pos.Magnitude() < 0.1f);
    }
    TEST_MAIN_RESULT();
}
