// Grok finding 16 (issue #888): a Spring joint on a light body blew up to about 1e19-1e21
// m/s and then NaN within 1-1.5 s at a normal 60 fps.
//
// The spring was stepped with plain explicit damping: in one step the damping changed the
// stretch speed by damper * dt * (1/mA + 1/mB) of itself, so once that passed 2 every step
// overshot by more than it removed. The trigger table from the report: spring 30 (the
// default), rest length 1, a start 2 m from a fixed anchor, fixed 1/60 s steps, no gravity,
//   damper 8, mass 0.05 / damper 16, mass 0.1 / damper 16, mass 0.05 / damper 2, mass 0.01.
// Each must stay finite, never fly off, and settle at the rest length, in 2D and in 3D,
// against a world anchor, a Static anchor body and a second light body.
//
// A heavy body (the ordinary case) is checked too: it must still bounce like a spring.
#include "test_framework.hpp"
#include <Okay.hpp>
#include <cmath>
#include <cstdio>

using namespace okay;

namespace {
struct Result { bool finite = true; float maxSpeed = 0.0f, maxDist = 0.0f, minDist = 1e9f, endDist = 0.0f; };

// anchorKind: 0 = world anchor, 1 = a Static body named "A", 2 = a second dynamic body of the same mass.
Result Run2D(float damper, float mass, int anchorKind, int steps = 300) {
    Scene s("spring2d");
    GameObject* a = nullptr;
    if (anchorKind > 0) {
        a = s.CreateGameObject("A");
        auto* ra = a->AddComponent<Rigidbody2D>();
        ra->gravityScale = 0.0f; ra->mass = mass;
        if (anchorKind == 1) ra->bodyType = Rigidbody2D::BodyType::Static;
    }
    GameObject* b = s.CreateGameObject("B");
    b->transform->localPosition = {2, 0, 0};
    auto* rb = b->AddComponent<Rigidbody2D>();
    rb->gravityScale = 0.0f; rb->mass = mass;
    auto* j = b->AddComponent<Joint2D>();
    j->mode = (int)Joint2D::Mode::Spring;
    if (a) j->connectedBody = "A";
    j->autoConfigure = false; j->distance = 1.0f;
    j->spring = 30.0f; j->damper = damper;
    s.Start();
    Result r;
    for (int i = 0; i < steps; ++i) {
        s.Update(1.0f / 60.0f);
        Vec3 pb = b->transform->Position(), pa = a ? a->transform->Position() : Vec3{0, 0, 0};
        float d = std::sqrt((pb.x - pa.x) * (pb.x - pa.x) + (pb.y - pa.y) * (pb.y - pa.y));
        float v = rb->velocity.Magnitude();
        r.finite = r.finite && std::isfinite(d) && std::isfinite(v);
        r.maxSpeed = std::fmax(r.maxSpeed, v);
        r.maxDist = std::fmax(r.maxDist, d); r.minDist = std::fmin(r.minDist, d);
        r.endDist = d;
    }
    return r;
}

Result Run3D(float damper, float mass, int anchorKind, int steps = 300) {
    Scene s("spring3d");
    GameObject* a = nullptr;
    if (anchorKind > 0) {
        a = s.CreateGameObject("A");
        auto* ra = a->AddComponent<Rigidbody3D>();
        ra->gravityScale = 0.0f; ra->mass = mass;
        if (anchorKind == 1) ra->bodyType = Rigidbody3D::BodyType::Static;
    }
    GameObject* b = s.CreateGameObject("B");
    b->transform->localPosition = {2, 0, 0};
    auto* rb = b->AddComponent<Rigidbody3D>();
    rb->gravityScale = 0.0f; rb->mass = mass;
    auto* j = b->AddComponent<Joint3D>();
    j->mode = (int)Joint3D::Mode::Spring;
    if (a) j->connectedBody = "A";
    j->autoConfigure = false; j->distance = 1.0f;
    j->spring = 30.0f; j->damper = damper;
    s.Start();
    Result r;
    for (int i = 0; i < steps; ++i) {
        s.Update(1.0f / 60.0f);
        Vec3 pb = b->transform->Position(), pa = a ? a->transform->Position() : Vec3{0, 0, 0};
        Vec3 dv = pb - pa;
        float d = dv.Magnitude();
        float v = rb->velocity.Magnitude();
        r.finite = r.finite && std::isfinite(d) && std::isfinite(v);
        r.maxSpeed = std::fmax(r.maxSpeed, v);
        r.maxDist = std::fmax(r.maxDist, d); r.minDist = std::fmin(r.minDist, d);
        r.endDist = d;
    }
    return r;
}

void CheckStable(const Result& r, const char* what) {
    // The capped spring closes at most the whole 1 m stretch in one 1/60 s step, so no speed
    // above 60 m/s (plus rounding); the report saw 1e19-1e21 m/s.
    const bool ok = r.finite && r.maxSpeed < 61.0f && r.maxDist < 2.2f && std::fabs(r.endDist - 1.0f) < 0.05f;
    if (!ok) std::printf("  %s: finite=%d maxSpeed=%g maxDist=%g endDist=%g\n", what, (int)r.finite, r.maxSpeed, r.maxDist, r.endDist);
    CHECK(r.finite);
    CHECK(r.maxSpeed < 61.0f);                       // the report saw 1e19-1e21 m/s
    CHECK(r.maxDist < 2.2f);                         // never flung past where it started
    CHECK(std::fabs(r.endDist - 1.0f) < 0.05f);      // settled at the rest length after 5 s
}
} // namespace

int main() {
    RUN_SUITE("grok_spring_stability");

    struct Case { float damper, mass; };
    const Case table[] = {{8.0f, 0.05f}, {16.0f, 0.1f}, {16.0f, 0.05f}, {2.0f, 0.01f}, {100.0f, 0.001f}};
    for (const Case& c : table)
        for (int anchor = 0; anchor < 3; ++anchor) {
            char name[96];
            std::snprintf(name, sizeof name, "2D damper %g mass %g anchor %d", c.damper, c.mass, anchor);
            CheckStable(Run2D(c.damper, c.mass, anchor), name);
            std::snprintf(name, sizeof name, "3D damper %g mass %g anchor %d", c.damper, c.mass, anchor);
            CheckStable(Run3D(c.damper, c.mass, anchor), name);
        }

    // An ordinary body still behaves like a spring: it overshoots the rest length (bounces)
    // and then settles near it.
    for (int dim = 0; dim < 2; ++dim) {
        Result r = dim == 0 ? Run2D(2.0f, 1.0f, 0, 600) : Run3D(2.0f, 1.0f, 0, 600);
        CHECK(r.finite);
        CHECK(r.minDist < 0.9f);                     // swung past the rest length
        CHECK(std::fabs(r.endDist - 1.0f) < 0.05f);
    }

    TEST_MAIN_RESULT();
}
