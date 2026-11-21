// Camera maths over simulated frames (engine/include/okay/Components/CameraMath.hpp).
// No scene, no window: plain numbers, so the same run gives the same answer every time.
#include "test_framework.hpp"
#include "okay/Components/CameraMath.hpp"
#include <cmath>

using namespace okay;
using namespace okay::cam;

static const float kRates[3] = {30.0f, 60.0f, 144.0f};

int main() {
    RUN_SUITE("camera_math");

    // ---- Spring: same answer at 30, 60 and 144 fps, no overshoot, no NaN ----
    {
        float at1[3];
        for (int r = 0; r < 3; ++r) {
            float x = 0, v = 0;
            const float dt = 1.0f / kRates[r];
            const int n = (int)std::round(kRates[r]);
            float maxX = 0;
            for (int i = 0; i < n; ++i) { SpringStep(x, v, 10.0f, 6.0f, dt); maxX = std::max(maxX, x); }
            at1[r] = x;
            CHECK(maxX <= 10.0001f);              // critically damped: never passes the target
            CHECK(std::isfinite(x) && std::isfinite(v));
        }
        CHECK_NEAR(at1[0], at1[1], 1e-3f);
        CHECK_NEAR(at1[1], at1[2], 1e-3f);
        CHECK(at1[0] > 8.0f && at1[0] < 10.0f);   // mostly there after one second at omega 6
    }
    // A starting speed keeps its direction (the spring has momentum), and settles.
    {
        float x = 0, v = 20.0f;
        for (int i = 0; i < 600; ++i) SpringStep(x, v, 0.0f, 8.0f, 1.0f / 60.0f);
        CHECK_NEAR(x, 0.0f, 1e-3f);
        CHECK_NEAR(v, 0.0f, 1e-3f);
    }
    // Bad input: NaN target ignored, NaN state repaired, huge step fine, omega 0 snaps.
    {
        float x = 1, v = 0;
        SpringStep(x, v, NAN, 5.0f, 0.016f);
        CHECK_NEAR(x, 1.0f, 1e-6f);
        float bx = NAN, bv = 3;
        SpringStep(bx, bv, 4.0f, 5.0f, 0.016f);
        CHECK_NEAR(bx, 4.0f, 1e-6f);
        SpringStep(x, v, 9.0f, 5.0f, 5.0f);       // a five second hitch
        CHECK(std::isfinite(x) && std::fabs(x - 9.0f) < 1e-3f);
        SpringStep(x, v, 2.0f, 0.0f, 0.016f);
        CHECK_NEAR(x, 2.0f, 1e-6f);
        SpringStep(x, v, 100.0f, 5.0f, 0.0f);     // zero step does nothing
        CHECK_NEAR(x, 2.0f, 1e-6f);
    }
    // Following a smoothly moving target: the step to step change is smooth (no jitter).
    for (int r = 0; r < 3; ++r) {
        Vec3 x{0, 0, 0}, v{0, 0, 0};
        const float dt = 1.0f / kRates[r];
        float prev = 0, maxJerk = 0, prevStep = 0;
        for (int i = 0; i < (int)(kRates[r] * 4); ++i) {
            const float t = i * dt;
            SpringStep(x, v, Vec3{5.0f * t, 0, 0}, 8.0f, dt);
            const float step = x.x - prev; prev = x.x;
            if (i > 2) maxJerk = std::max(maxJerk, std::fabs(step - prevStep) / dt);
            prevStep = step;
        }
        CHECK(maxJerk < 6.0f);   // metres per second per second: gentle at every rate
    }

    // ---- Noise: deterministic, bounded, smooth ----
    {
        CHECK_NEAR(Noise1(3.7f, 5), Noise1(3.7f, 5), 0.0f);
        CHECK(Noise1(3.7f, 5) != Noise1(3.7f, 6));
        float mx = 0, maxStep = 0, prev = Noise1(0, 9);
        for (int i = 1; i < 20000; ++i) {
            const float n = Noise1(i * 0.01f, 9);
            mx = std::max(mx, std::fabs(n));
            maxStep = std::max(maxStep, std::fabs(n - prev)); prev = n;
        }
        CHECK(mx <= 1.0f);
        CHECK(mx > 0.4f);
        CHECK(maxStep < 0.06f);   // smooth: no sudden jumps
        CHECK(std::fabs(Noise1(-7.3f, 1)) <= 1.0f);
    }

    // ---- Shake: bounded, dies away, same at every frame rate, presets differ ----
    {
        for (int p = 0; p < 4; ++p) {
            CameraShaker s;
            s.Add((ShakePreset)p, 1.0f);
            const ShakeParams sp = ShakePresetParams((ShakePreset)p);
            float peak = 0;
            for (int i = 0; i < 200; ++i) {
                s.Step(1.0f / 60.0f);
                CHECK(AllFinite(s.Pos()) && AllFinite(s.RotDeg()));
                peak = std::max(peak, s.Pos().Magnitude());
            }
            CHECK(peak <= sp.posAmount * 1.8f);      // never bigger than its own size
            if (p != (int)ShakePreset::Handheld) { CHECK(!s.Active()); CHECK_NEAR(s.Pos().x, 0.0f, 1e-6f); }
            else CHECK(s.Active());                   // handheld keeps going until cleared
        }
        // Footstep moves up and down only.
        CameraShaker f; f.Add(ShakePreset::Footstep, 1.0f);
        for (int i = 0; i < 5; ++i) { f.Step(1.0f / 60.0f); CHECK_NEAR(f.Pos().x, 0.0f, 1e-7f); CHECK_NEAR(f.Pos().z, 0.0f, 1e-7f); }
        // Explosion is bigger and lasts longer than impact.
        CHECK(ShakePresetParams(ShakePreset::Explosion).posAmount > ShakePresetParams(ShakePreset::Impact).posAmount);
        CHECK(ShakePresetParams(ShakePreset::Explosion).duration > ShakePresetParams(ShakePreset::Impact).duration);
        // Frame rate: the shake at the same moment in time is the same up to the step.
        Vec3 at[3];
        for (int r = 0; r < 3; ++r) {
            CameraShaker s; s.Add(ShakePreset::Explosion, 0.8f);
            const int n = (int)std::round(kRates[r] * 0.5f);
            for (int i = 0; i < n; ++i) s.Step(1.0f / kRates[r]);
            at[r] = s.Pos();
        }
        CHECK((at[0] - at[2]).Magnitude() < 0.02f);
        CHECK((at[1] - at[2]).Magnitude() < 0.02f);
        // The same shake twice gives the same numbers.
        CameraShaker a, b; a.Add(ShakePreset::Impact, 0.7f); b.Add(ShakePreset::Impact, 0.7f);
        for (int i = 0; i < 10; ++i) { a.Step(0.016f); b.Step(0.016f); CHECK_NEAR(a.Pos().x, b.Pos().x, 0.0f); }
        // Many at once are capped.
        CameraShaker m; for (int i = 0; i < 100; ++i) m.Add(ShakePreset::Impact, 1.0f);
        CHECK(m.Count() <= CameraShaker::kMaxShakes);
        // Zero, negative and NaN strengths do nothing.
        CameraShaker z; z.Add(ShakePreset::Impact, 0.0f); z.Add(ShakePreset::Impact, -1.0f); z.Add(ShakePreset::Impact, NAN);
        CHECK(!z.Active());
        // Handheld eases in and out.
        CameraShaker h; h.SetHandheld(1.0f, 4.0f);
        for (int i = 0; i < 30; ++i) h.Step(1.0f / 60.0f);
        CHECK(h.Handheld() > 0.5f && h.Handheld() < 1.0f);
        h.SetHandheld(0.0f);
        for (int i = 0; i < 600; ++i) h.Step(1.0f / 60.0f);
        CHECK(!h.Active());
    }

    // ---- Blend curves ----
    {
        const float bez[4] = {0.2f, 0.9f, 0.8f, 0.1f};
        for (int s = 0; s <= 5; ++s) {
            CHECK_NEAR(BlendCurve((BlendStyle)s, 0.0f, bez), 0.0f, 1e-6f);
            CHECK_NEAR(BlendCurve((BlendStyle)s, 1.0f, bez), 1.0f, 1e-6f);
            float prev = 0;
            for (int i = 1; i <= 200; ++i) {
                const float y = BlendCurve((BlendStyle)s, i / 200.0f, bez);
                CHECK(std::isfinite(y));
                CHECK(y >= prev - 1e-4f || s == (int)BlendStyle::Custom);   // custom may dip: it is the designer's curve
                prev = y;
            }
        }
        CHECK_NEAR(BlendCurve(BlendStyle::Cut, 0.001f), 1.0f, 1e-6f);     // a cut is all at once
        CHECK_NEAR(BlendCurve(BlendStyle::Linear, 0.3f), 0.3f, 1e-6f);
        CHECK(BlendCurve(BlendStyle::EaseIn, 0.25f) < 0.25f);               // slow start
        CHECK(BlendCurve(BlendStyle::EaseOut, 0.25f) > 0.25f);              // fast start
        CHECK_NEAR(BlendCurve(BlendStyle::EaseInOut, 0.5f), 0.5f, 1e-5f);
        CHECK_NEAR(BezierEase(0.5f, 0.25f, 0.25f, 0.75f, 0.75f), 0.5f, 1e-3f);   // a straight-line curve is linear
        CHECK(std::isfinite(BezierEase(NAN, 0.3f, 0.2f, 0.7f, 0.9f)));
        CHECK(std::isfinite(BlendCurve(BlendStyle::Custom, 0.5f, nullptr)));
    }

    // ---- Zones ----
    {
        CHECK_NEAR(ZoneCorrection(0.1f, 0.2f, 0.6f, 5.0f, 0.016f), 0.0f, 0.0f);   // inside the dead zone: still
        CHECK_NEAR(ZoneCorrection(-0.2f, 0.2f, 0.6f, 5.0f, 0.016f), 0.0f, 0.0f);   // on the edge: still
        const float c = ZoneCorrection(0.4f, 0.2f, 0.6f, 5.0f, 0.016f);
        CHECK(c > 0.0f && c < 0.2f);                                              // soft zone: eases
        CHECK(ZoneCorrection(-0.4f, 0.2f, 0.6f, 5.0f, 0.016f) < 0.0f);           // mirrored
        CHECK_NEAR(ZoneCorrection(0.9f, 0.2f, 0.6f, 5.0f, 0.016f), 0.3f, 1e-5f); // past the soft zone: caught at once
        CHECK_NEAR(ZoneCorrection(0.9f, 0.2f, 0.1f, 0.0f, 0.016f), 0.7f, 1e-5f); // soft smaller than dead = hard edge
        CHECK_NEAR(ZoneCorrection(NAN, 0.2f, 0.6f, 5.0f, 0.016f), 0.0f, 0.0f);
        // A target standing still outside the dead zone: the view settles with the target ON the dead zone edge,
        // at every frame rate, without ever overshooting.
        for (int r = 0; r < 3; ++r) {
            float off = 0.5f;               // target 0.5 from centre
            float low = off;
            const float dt = 1.0f / kRates[r];
            for (int i = 0; i < (int)kRates[r] * 4; ++i) { off -= ZoneCorrection(off, 0.2f, 0.7f, 6.0f, dt); low = std::min(low, off); }
            CHECK_NEAR(off, 0.2f, 0.002f);
            CHECK(low >= 0.2f - 1e-4f);
        }
        // After 0.5 s the remaining gap is the same at every rate.
        float rest[3];
        for (int r = 0; r < 3; ++r) {
            float off = 0.6f; const float dt = 1.0f / kRates[r];
            for (int i = 0; i < (int)(kRates[r] * 0.5f); ++i) off -= ZoneCorrection(off, 0.2f, 0.9f, 6.0f, dt);
            rest[r] = off;
        }
        CHECK_NEAR(rest[0], rest[2], 0.004f);
        CHECK_NEAR(rest[1], rest[2], 0.004f);
    }

    // ---- Look ahead ----
    {
        for (int r = 0; r < 3; ++r) {
            LookaheadFilter f;
            const float dt = 1.0f / kRates[r];
            Vec3 p{0, 0, 0}; Vec3 off{0, 0, 0};
            for (int i = 0; i < (int)kRates[r] * 3; ++i) { p.x += 4.0f * dt; off = f.Step(p, 0.5f, 3.0f, 4.0f, dt); CHECK(AllFinite(off)); }
            CHECK_NEAR(off.x, 2.0f, 0.15f);          // 4 m/s * 0.5 s ahead
            CHECK(off.y == 0.0f);
        }
        LookaheadFilter f; Vec3 o{0, 0, 0};              // the max is respected
        Vec3 p{0, 0, 0};
        for (int i = 0; i < 300; ++i) { p.x += 0.5f; o = f.Step(p, 1.0f, 1.5f, 6.0f, 1.0f / 60.0f); }
        CHECK(o.Magnitude() <= 1.5f + 1e-3f);
        // A teleport does not fling the camera ahead.
        Vec3 before = o; p.x += 500.0f; o = f.Step(p, 1.0f, 1.5f, 6.0f, 1.0f / 60.0f);
        CHECK(o.Magnitude() < 0.01f); (void)before;
        // A target moved by a 50 Hz physics step, seen at 144 fps: the shift barely wobbles.
        LookaheadFilter g; Vec3 q{0, 0, 0};
        float mn = 1e9f, mx = -1e9f;
        for (int i = 0; i < 144 * 4; ++i) {
            const float t = i / 144.0f;
            q.x = std::floor(t * 50.0f) / 50.0f * 5.0f;   // moves only on 50 Hz ticks
            const Vec3 off2 = g.Step(q, 0.4f, 3.0f, 4.0f, 1.0f / 144.0f);
            if (t > 2.0f) { mn = std::min(mn, off2.x); mx = std::max(mx, off2.x); }
        }
        CHECK(mx - mn < 0.05f);                          // under 5 cm of wobble on a 2 m look ahead
        CHECK_NEAR(0.5f * (mn + mx), 2.0f, 0.1f);
    }

    // ---- Group ----
    {
        const Vec3 pts[3] = {{0, 0, 0}, {10, 0, 0}, {5, 0, 4}};
        GroupFrame g = FrameGroup(pts, 3, 1.0f);
        CHECK(g.valid);
        CHECK_NEAR(g.center.x, 5.0f, 1e-5f); CHECK_NEAR(g.center.z, 2.0f, 1e-5f);
        CHECK(g.radius >= 5.0f + 1.0f - 1e-3f);
        CHECK(!FrameGroup(pts, 0).valid);
        const Vec3 withNan[2] = {{1, 1, 1}, {NAN, 0, 0}};
        GroupFrame h = FrameGroup(withNan, 2);
        CHECK(h.valid); CHECK_NEAR(h.center.x, 1.0f, 1e-6f);
        // A bigger group needs a bigger distance; everything fits at that distance.
        const float d1 = DistanceToFit(3.0f, 60.0f, 16.0f / 9.0f), d2 = DistanceToFit(6.0f, 60.0f, 16.0f / 9.0f);
        CHECK(d2 > d1 * 1.99f && d2 < d1 * 2.01f);
        CHECK(DistanceToFit(3.0f, 60.0f, 0.5f) > d1);    // a tall narrow window needs more room
        CHECK(OrthoSizeToFit(4.0f, 2.0f) == 4.0f);
        CHECK(OrthoSizeToFit(4.0f, 0.5f) == 8.0f);
    }

    // ---- 2D: confine, rooms, pixels ----
    {
        Box2 b; b.minX = 0; b.minY = 0; b.maxX = 40; b.maxY = 20;
        Vec2 c = ConfineView({-5, 30}, {8, 4.5f}, b);
        CHECK_NEAR(c.x, 8.0f, 1e-5f); CHECK_NEAR(c.y, 15.5f, 1e-5f);
        Vec2 inside = ConfineView({20, 10}, {8, 4.5f}, b);
        CHECK_NEAR(inside.x, 20.0f, 1e-5f);
        Box2 tiny; tiny.minX = 0; tiny.minY = 0; tiny.maxX = 10; tiny.maxY = 5;
        Vec2 t2 = ConfineView({100, -100}, {8, 4.5f}, tiny);   // room smaller than the view: centred, still
        CHECK_NEAR(t2.x, 5.0f, 1e-5f); CHECK_NEAR(t2.y, 2.5f, 1e-5f);

        Box2 rooms[3];
        rooms[0].minX = 0; rooms[0].minY = 0; rooms[0].maxX = 10; rooms[0].maxY = 10;
        rooms[1].minX = 10; rooms[1].minY = 0; rooms[1].maxX = 20; rooms[1].maxY = 10;
        rooms[2].minX = 0; rooms[2].minY = 0; rooms[2].maxX = 20; rooms[2].maxY = 10;   // a big one over both
        CHECK(PickRoom(rooms, 3, 5, 5, -1) == 0);        // the smallest that holds it
        CHECK(PickRoom(rooms, 3, 10, 5, 0) == 0);        // on the shared edge: stays in the current room
        CHECK(PickRoom(rooms, 3, 10, 5, 1) == 1);
        CHECK(PickRoom(rooms, 3, 15, 5, 0) == 1);        // moved over
        CHECK(PickRoom(rooms, 3, 99, 99, 1) == 1);       // outside everything: stays
        CHECK(PickRoom(rooms, 0, 1, 1, -1) == -1);

        CHECK_NEAR(SnapToPixel(1.234f, 16.0f), std::round(1.234f * 16.0f) / 16.0f, 1e-6f);
        CHECK_NEAR(SnapToPixel(1.234f, 0.0f), 1.234f, 0.0f);
        // Moving slowly: the snapped position only ever changes by one whole pixel, and never goes backward.
        float prev = SnapToPixel(0, 32.0f); bool ok = true;
        for (int i = 1; i < 2000; ++i) {
            const float s = SnapToPixel(i * 0.0037f, 32.0f);
            const float d = s - prev;
            if (d < -1e-6f || d > 1.0f / 32.0f + 1e-5f) ok = false;
            prev = s;
        }
        CHECK(ok);
        // A 360 pixel tall screen at 16 pixels per unit wants a whole zoom.
        const float sz = PixelPerfectOrthoSize(360.0f, 16.0f, 11.0f);
        const float scale = 360.0f / (2.0f * 16.0f * sz);
        CHECK_NEAR(scale, std::round(scale), 1e-4f);
        CHECK_NEAR(PixelPerfectOrthoSize(0.0f, 16.0f, 5.0f), 5.0f, 0.0f);
    }

    // ---- Fade tracker ----
    {
        FadeTracker t; std::vector<FadeTracker::Entry> done;
        int a = 0, b = 0;
        for (int i = 0; i < 60; ++i) { t.Mark(&a, 1.0f); t.Step(0.25f, 8.0f, 1.0f / 60.0f, done); }
        CHECK(t.Entries().size() == 1);
        CHECK(t.Entries()[0].alpha < 0.3f && t.Entries()[0].alpha >= 0.25f - 1e-4f);
        for (int i = 0; i < 120; ++i) t.Step(0.25f, 8.0f, 1.0f / 60.0f, done);   // no longer blocking: back to normal
        CHECK(t.Entries().empty());
        CHECK(done.size() == 1 && done[0].id == &a && done[0].alpha == 1.0f);
        t.Mark(&b, 0.8f); t.Forget(&b);
        CHECK(t.Entries().empty());
    }

    TEST_MAIN_RESULT();
}
