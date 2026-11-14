// A player must not end pinned between solid objects.
// A gap wider than the body lets him through. A gap that does not, a corner,
// or two colliders that touch, lets him slide back out when the input points
// away. Spawning inside a solid pushes him out, then the same input carries
// him clear. Measured at several approach angles, walk and run, and at
// 11 / 30 / 60 / 144 Hz.
#include "test_framework.hpp"
#include <Okay.hpp>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace okay;

static void StaticBox(Scene& sc, const char* name, const Vec3& pos, const Vec3& size, float yawDeg = 0.0f) {
    GameObject* g = sc.CreateGameObject(name);
    g->transform->localPosition = pos;
    g->transform->localRotation = Quat::Euler(0.0f, yawDeg, 0.0f);
    g->AddComponent<BoxCollider3D>()->size = size;
    g->AddComponent<Rigidbody3D>()->bodyType = Rigidbody3D::BodyType::Static;
}

static float YawFor(const Vec3& dir) {
    return std::atan2(-dir.x, -dir.z) * 57.2957795f;
}

struct Outcome {
    float along = 0.0f;     // metres traveled along the approach (positive = into the gap)
    float away = 0.0f;      // metres traveled back out once the input reverses
    float endSpeed = 0.0f;
    bool overlapped = false;
};

static Outcome Drive(float dt, float gap, float angleDeg, bool run, bool reverse,
                     bool overlapSpawn, int shape, bool faceCamera,
                     int body = 0) {
    Scene sc("gap");
    StaticBox(sc, "Ground", {0, -0.5f, 0}, {80, 1, 80});

    const float ang = angleDeg * 0.0174532925f;
    Vec3 approach{std::sin(ang), 0.0f, -std::cos(ang)};
    Vec3 side{-approach.z, 0.0f, approach.x};
    Vec3 start = overlapSpawn ? Vec3{0, 0.05f, 0} : approach * -3.5f;
    float yaw = YawFor(approach);
    // The editor's cube and sphere sit on either side of the origin. Drop the
    // player just in front of that pair, looking down -Z, the way Play does.
    if (shape == 4) {
        approach = {0, 0, -1};
        side = {1, 0, 0};
        start = {0, 1.0f, 2.2f};
        yaw = 0.0f;
    }

    const float boxYaw = YawFor(approach);
    if (shape == 0) {
        // Two boxes, faces parallel to the walk. `gap` is the clear width.
        const float half = 0.5f;
        const Vec3 mid = Vec3::Zero;
        StaticBox(sc, "L", mid - side * (gap * 0.5f + half), {1, 2, 3}, boxYaw);
        StaticBox(sc, "R", mid + side * (gap * 0.5f + half), {1, 2, 3}, boxYaw);
    } else if (shape == 1) {
        // A long wall and one box, same clear gap, same orientation.
        StaticBox(sc, "Wall", side * -(gap * 0.5f + 0.25f), {0.5f, 2, 8}, boxYaw);
        StaticBox(sc, "Box", side * (gap * 0.5f + 0.5f), {1, 2, 2}, boxYaw);
    } else if (shape == 2) {
        // Inside corner. Two slabs meet in a V opening toward the player.
        StaticBox(sc, "A", side * -0.15f + approach * 0.4f, {0.4f, 2, 3}, angleDeg + 28.0f);
        StaticBox(sc, "B", side * 0.15f + approach * 0.4f, {0.4f, 2, 3}, angleDeg - 28.0f);
    } else if (shape == 3) {
        // Two colliders whose faces touch (no gap).
        StaticBox(sc, "L", side * -0.5f, {1, 2, 2}, boxYaw);
        StaticBox(sc, "R", side * 0.5f, {1, 2, 2}, boxYaw);
    } else if (shape == 5) {
        StaticBox(sc, "Cube", {-1.1f, 0.5f, 0.0f}, {1, 1, 1});
        StaticBox(sc, "Sphere", {0.9f, 0.55f, 0.4f}, {1.1f, 1.1f, 1.1f});
        start = {0.0f, 1.0f, 0.0f};
        yaw = 0.0f;
        approach = {0, 0, -1};
    } else {
        // The editor's default 3D scene: a 1 m cube and a 1.1 m sphere-box.
        StaticBox(sc, "Cube", {-1.1f, 0.5f, 0.0f}, {1, 1, 1});
        StaticBox(sc, "Sphere", {0.9f, 0.55f, 0.4f}, {1.1f, 1.1f, 1.1f});
    }
    if (overlapSpawn && shape != 4)
        StaticBox(sc, "Overlap", {0, 0.9f, 0}, {1.2f, 1.8f, 1.2f});

    GameObject* player = Templates::AddThirdPersonPlayer(sc, start);
    auto* tp = player->GetComponent<ThirdPersonController>();
    tp->lockCursor = false;
    tp->yaw = yaw;
    if (body == 3) {
        player->transform->localPosition = {0.0f, 0.05f, 0.0f};
        player->transform->localRotation = Quat::Euler(0.0f, 45.0f, 0.0f);
    }
    tp->autoRecenter = false;
    if (faceCamera) tp->faceMode = ThirdPersonController::FaceMode::Camera;
    sc.Start();

    const char sprint = Input::KeyShift;
    std::vector<char> into{'w'};
    if (shape == 4 && angleDeg > 40.0f) into = {'d'};
    else if (shape == 4 && angleDeg > 10.0f) into = {'w', 'd'};
    if (run) into.push_back(sprint);
    std::vector<char> out{'s'};
    if (shape == 4 && angleDeg > 40.0f) out = {'a'};
    if (run) out.push_back(sprint);

    Input::FeedMouse({400.0f, 300.0f}, 0);
    Input::FeedKeys({});
    sc.Update(dt);

    auto horiz = [](const Vec3& v) { return std::sqrt(v.x * v.x + v.z * v.z); };
    const Vec3 origin = player->transform->Position();
    const int intoFrames = std::max(8, (int)std::lround(1.6f / dt));
    for (int i = 0; i < intoFrames; ++i) {
        Input::FeedMouse({400.0f, 300.0f}, 0);
        Input::FeedKeys(into);
        sc.Update(dt);
    }
    const Vec3 mid = player->transform->Position();
    Outcome o;
    o.along = Vec3::Dot(mid - origin, approach);

    const int awayFrames = reverse ? std::max(8, (int)std::lround(1.4f / dt)) : 0;
    for (int i = 0; i < awayFrames; ++i) {
        Input::FeedMouse({400.0f, 300.0f}, 0);
        Input::FeedKeys(out);
        sc.Update(dt);
    }
    const Vec3 end = player->transform->Position();
    o.away = Vec3::Dot(end - mid, approach * -1.0f);
    if (auto* rb = player->GetComponent<Rigidbody3D>()) o.endSpeed = horiz(rb->velocity);
    // Still inside the obstacle band, not moving, while the input points away.
    const float band = shape == 4 ? 1.6f : 1.3f;
    o.overlapped = reverse && o.away < 0.35f && o.endSpeed < 0.2f &&
                   (mid - (shape == 4 ? Vec3{0, mid.y, 0} : Vec3::Zero)).Magnitude() < band;
    return o;
}

int main() {
    RUN_SUITE("gap_stuck");
    const float rates[] = {1.0f / 11.0f, 1.0f / 30.0f, 1.0f / 60.0f, 1.0f / 144.0f};
    const char* hz[] = {"11", "30", "60", "144"};
    const float angles[] = {0.0f, 25.0f, 45.0f};
    int pinned = 0;

    auto report = [&](const char* what, const Outcome& o, bool wantThrough, bool wantOut) {
        const char* tag = o.overlapped ? "PINNED" : "ok";
        std::printf("  %-28s along %6.2f  away %6.2f  speed %5.2f  %s\n",
                    what, o.along, o.away, o.endSpeed, tag);
        if (o.overlapped) ++pinned;
        if (wantThrough) CHECK(o.along > 2.0f);
        if (wantOut) CHECK(!o.overlapped);
        if (wantOut) CHECK(o.away > 0.35f || o.endSpeed > 0.4f);
    };

    for (int ri = 0; ri < 4; ++ri) {
        for (float ang : angles) {
            for (int run = 0; run < 2; ++run) {
                char name[64];
                std::snprintf(name, sizeof(name), "wide %s %s %.0f", hz[ri], run ? "run" : "walk", ang);
                Outcome o = Drive(rates[ri], 1.15f, ang, run == 1, false, false, 0, false);
                // 1.15 m is wider than the 0.60 m body, and wider than that body
                // turned 45 degrees (0.85 m). He has to get through.
                report(name, o, true, false);
            }
        }
        char name[64];
        std::snprintf(name, sizeof(name), "fit-straight %s", hz[ri]);
        report(name, Drive(rates[ri], 0.75f, 0.0f, false, true, false, 0, false), true, false);

        std::snprintf(name, sizeof(name), "fit-45 backout %s", hz[ri]);
        report(name, Drive(rates[ri], 0.75f, 45.0f, false, true, false, 0, false), false, true);

        std::snprintf(name, sizeof(name), "wall backout %s", hz[ri]);
        report(name, Drive(rates[ri], 0.70f, 20.0f, true, true, false, 1, false), false, true);

        std::snprintf(name, sizeof(name), "corner backout %s", hz[ri]);
        report(name, Drive(rates[ri], 0.0f, 0.0f, false, true, false, 2, false), false, true);

        std::snprintf(name, sizeof(name), "touching backout %s", hz[ri]);
        report(name, Drive(rates[ri], 0.0f, 15.0f, true, true, false, 3, false), false, true);

        std::snprintf(name, sizeof(name), "spawn-overlap %s", hz[ri]);
        report(name, Drive(rates[ri], 0.0f, 0.0f, false, true, true, 0, false), false, true);

        std::snprintf(name, sizeof(name), "editor pair W %s", hz[ri]);
        report(name, Drive(rates[ri], 0.0f, 0.0f, false, true, false, 4, false), false, true);

        std::snprintf(name, sizeof(name), "editor pair face-cam %s", hz[ri]);
        report(name, Drive(rates[ri], 0.0f, 30.0f, false, true, false, 4, true), false, true);

        // Body already turned 45 degrees inside a gap only a little wider than
        // the box's footprint. The diamond overlaps both walls.
        std::snprintf(name, sizeof(name), "wedged-45 backout %s", hz[ri]);
        report(name, Drive(rates[ri], 0.70f, 0.0f, false, true, false, 0, false, 3), false, true);

        // Dropped where the editor drops a third-person player: between the
        // default cube and sphere, then walking, strafing, and backing out.
        std::snprintf(name, sizeof(name), "editor spawn W %s", hz[ri]);
        {
            Outcome o = Drive(rates[ri], 0.0f, 0.0f, false, true, false, 5, false);
            report(name, o, false, true);
        }
    }

    std::printf("  pinned cases: %d\n", pinned);
    CHECK(pinned == 0);
    TEST_MAIN_RESULT();
}
