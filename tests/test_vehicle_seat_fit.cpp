// Seat fit maths (engine/include/okay/Components/VehicleSeatFit.hpp), hand-computed numbers.
#include "test_framework.hpp"
#include "okay/Components/VehicleSeatFit.hpp"
#include <limits>

using namespace okay::seatfit;

int main() {
    RUN_SUITE("vehicle_seat_fit");

    // Rotation about up: +Z turned 90 degrees is +X; +X turned 90 is -Z.
    V3 a = RotY({0, 0, 1}, 90); CHECK_NEAR(a.x, 1, 1e-5); CHECK_NEAR(a.z, 0, 1e-5);
    V3 b = RotY({1, 0, 0}, 90); CHECK_NEAR(b.x, 0, 1e-5); CHECK_NEAR(b.z, -1, 1e-5);
    V3 w = ToWorld({10, 0, 5}, 90, {0.5f, 0.6f, 1.0f});          // x = 0.5*0 + 1*1 = 1, z = -0.5*1 + 1*0 = -0.5
    CHECK_NEAR(w.x, 11, 1e-5); CHECK_NEAR(w.y, 0.6, 1e-6); CHECK_NEAR(w.z, 4.5, 1e-5);
    CHECK_NEAR(WorldYaw(170, 30), -160, 1e-4); CHECK_NEAR(WorldYaw(90, 90), 180, 1e-4); CHECK_NEAR(WorldYaw(0, 0), 0, 1e-6);

    // Fit: cushion 0.45, roof 1.30, margin 0.05 -> room 0.80 above the cushion.
    SeatAnchor an; an.roofHeight = 1.30f;
    SeatedFit f = FitToSeat(an, 1.8f);      // head above seat 0.48*1.8 = 0.864 > 0.80: scale 0.80/0.864 = 0.92593
    CHECK_NEAR(f.scale, 0.8 / 0.864, 1e-5);
    CHECK_NEAR(f.feetLocal.y, 0.6 + 0.45 - 0.57 * f.scale, 1e-5);   // hips (0.57 m at scale 1) on the cushion
    CHECK_NEAR(f.headTop, 1.25, 1e-4); CHECK(f.headClears);
    CHECK_NEAR(f.feetLocal.x, 0.0, 1e-9); CHECK_NEAR(f.yawDeg, 180, 1e-6);
    SeatedFit s = FitToSeat(an, 1.5f);      // short: 0.72 < 0.80, no scaling; feet = 1.05 - 0.57*1.5/1.8 = 0.575
    CHECK_NEAR(s.scale, 1.0, 1e-9); CHECK_NEAR(s.feetLocal.y, 0.575, 1e-5); CHECK_NEAR(s.headTop, 1.17, 1e-5); CHECK(s.headClears);
    SeatedFit t = FitToSeat(an, 2.1f);      // tall: wants 0.7937, floor 0.8 -> 0.8; head 0.45 + 1.008*0.8 = 1.2564 > 1.25
    CHECK_NEAR(t.scale, 0.8, 1e-6); CHECK_NEAR(t.headTop, 1.2564, 1e-4); CHECK(!t.headClears);
    SeatAnchor nofit = an; nofit.scaleToFit = false;
    CHECK_NEAR(FitToSeat(nofit, 2.1f).scale, 1.0, 1e-9); CHECK(!FitToSeat(nofit, 2.1f).headClears);
    SeatAnchor open = an; open.roofHeight = 0.0f;                    // open top: no limit
    CHECK_NEAR(FitToSeat(open, 2.4f).scale, 1.0, 1e-9); CHECK(FitToSeat(open, 2.4f).headClears);
    CHECK_NEAR(FitToSeat(an, std::numeric_limits<float>::quiet_NaN()).scale, f.scale, 1e-6);   // bad height reads as 1.8
    CHECK_NEAR(FitToSeat(an, -3.0f).scale, f.scale, 1e-6);
    SeatAnchor low = an; low.roofHeight = 0.4f;                      // roof lower than the cushion: no valid room, no scaling below floor
    CHECK_NEAR(FitToSeat(low, 1.8f).scale, 1.0, 1e-9); CHECK(!FitToSeat(low, 1.8f).headClears);

    // Enter blend: 3 m at 3 m/s = 1 s, clamped to 0.8 s. At 0.4 s smoothstep(0.5) = 0.5 -> 1.5; at 0.8 s it is there.
    EnterBlend e; e.Begin({0, 0, 0}, {3, 0, 0});
    CHECK_NEAR(e.duration, 0.8, 1e-6); CHECK(e.active);
    V3 p = e.Step(0.4f); CHECK_NEAR(p.x, 1.5, 1e-5); CHECK(e.active);
    p = e.Step(0.4f); CHECK_NEAR(p.x, 3.0, 1e-5); CHECK(!e.active);
    p = e.Step(1.0f); CHECK_NEAR(p.x, 3.0, 1e-9);
    EnterBlend near; near.Begin({0, 0, 0}, {0.1f, 0, 0}); CHECK_NEAR(near.duration, 0.25, 1e-6);   // minimum
    EnterBlend q; q.Begin({0, 0, 0}, {1, 0, 0}); q.Step(0.1f); CHECK(q.Step(0.0f).x > 0.0f);

    // Exit candidates: first is beside the door on the preferred side: 0.9 + 0.3 + 0.1 = 1.3 out.
    auto c = ExitCandidates(0.9f, 2.2f, 0.3f, true, 0.3f, 0.0f);
    CHECK(c.size() == 26);
    CHECK_NEAR(c[0].x, -1.3, 1e-5); CHECK_NEAR(c[0].z, 0.3, 1e-5); CHECK_NEAR(c[1].z, 1.1, 1e-5); CHECK_NEAR(c[2].z, -0.5, 1e-5);
    CHECK_NEAR(c[3].x, -1.7, 1e-5);
    CHECK_NEAR(c[12].x, 1.3, 1e-5);                                  // other side starts after 4 distances x 3 spots
    CHECK_NEAR(c[24].z, -2.6, 1e-5); CHECK_NEAR(c[25].z, 2.6, 1e-5);
    auto cr = ExitCandidates(0.9f, 2.2f, 0.3f, false, 0.3f, 0.0f); CHECK_NEAR(cr[0].x, 1.3, 1e-5);
    CHECK(FirstFree(c, [](const V3&) { return false; }) == 0);
    CHECK(FirstFree(c, [](const V3& v) { return v.x < 0.0f; }) == 12);     // whole left side blocked (a wall)
    CHECK(FirstFree(c, [](const V3&) { return true; }) == -1);              // all blocked: caller lifts above the roof
    CHECK(FirstFree(c, [](const V3& v) { return std::fabs(v.x) > 0.01f; }) == 24);   // only behind the car is free

    // Clip volume and cabin helpers.
    ClipBox box{{0, 1.3f, 0}, {0.5f, 0.1f, 0.6f}};
    CHECK(InsideClip(box, {0.4f, 1.35f, -0.5f})); CHECK(!InsideClip(box, {0.6f, 1.3f, 0}));
    CHECK(!InsideClip(box, {0, 1.5f, 0})); CHECK(InsideClip(box, {0.5f, 1.4f, 0.6f}));   // the boundary counts
    CHECK_NEAR(CockpitNearPlane(1.0f), 0.1, 1e-6); CHECK_NEAR(CockpitNearPlane(0.05f), 0.04, 1e-6);
    CHECK_NEAR(CockpitNearPlane(0.0f), 0.02, 1e-6); CHECK_NEAR(CockpitNearPlane(std::numeric_limits<float>::quiet_NaN()), 0.1, 1e-6);
    V3 eye = KeepEyeInCabin({2.0f, 0.1f, 0.0f}, {-0.8f, 0.5f, -1.0f}, {0.8f, 1.5f, 1.0f}, 0.1f);
    CHECK_NEAR(eye.x, 0.7, 1e-6); CHECK_NEAR(eye.y, 0.6, 1e-6); CHECK_NEAR(eye.z, 0.0, 1e-6);
    TEST_MAIN_RESULT();
}
