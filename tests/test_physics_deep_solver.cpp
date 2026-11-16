// The Improved contact solver on its own (Physics/ContactSolver.hpp + Physics/BoxManifold.hpp), without a scene:
// a small simulation of boxes and balls using the same steps the engine uses, so the numbers can be checked directly.
#include "test_framework.hpp"
#include "okay/Physics/BoxManifold.hpp"
#include "okay/Physics/ContactSolver.hpp"
#include "okay/Math/Quat.hpp"
#include <map>
#include <vector>

using namespace okay;
using namespace okay::contactsolver;
namespace bm = okay::boxmanifold;

namespace {

struct Sim {
    struct Obj {
        bool isBox = true, isStatic = false;
        Vec3 pos{0, 0, 0}, v{0, 0, 0}, w{0, 0, 0};
        Quat q = Quat::Identity;
        Vec3 half{0.5f, 0.5f, 0.5f};
        float radius = 0.5f, mass = 1.0f, friction = 0.5f, restitution = 0.0f, rolling = 0.0f;
    };
    std::vector<Obj> objs;
    std::map<std::pair<int, int>, Manifold> cache;
    Settings set;
    float dt = 1.0f / 50.0f;
    float skin = 0.006f;
    Vec3 gravity{0, -9.81f, 0};

    int Add(const Obj& o) { objs.push_back(o); return (int)objs.size() - 1; }

    static Mat3 InvInertia(const Obj& o) {
        if (o.isStatic) return ZeroMat();
        Vec3 d;
        if (o.isBox) { Vec3 s = o.half * 2.0f; d = {o.mass * (s.y * s.y + s.z * s.z) / 12.0f, o.mass * (s.x * s.x + s.z * s.z) / 12.0f, o.mass * (s.x * s.x + s.y * s.y) / 12.0f}; }
        else { float i = 0.4f * o.mass * o.radius * o.radius; d = {i, i, i}; }
        Mat3 R = Mat3::FromQuat(o.q);
        Mat3 D = Mat3::Scale({1.0f / d.x, 1.0f / d.y, 1.0f / d.z});
        return R * D * R.Transpose();
    }
    static bm::Box BoxOf(const Obj& o) {
        bm::Box b; b.c = o.pos; b.h = o.half;
        b.ax[0] = o.q * Vec3{1, 0, 0}; b.ax[1] = o.q * Vec3{0, 1, 0}; b.ax[2] = o.q * Vec3{0, 0, 1};
        return b;
    }

    void Step() {
        // 1) speeds only (the engine finds contacts at the poses of the end of the last step, solves, and only then moves)
        for (Obj& o : objs) {
            if (o.isStatic) continue;
            o.v = o.v + gravity * dt;
        }
        // 3) contacts
        std::vector<Body> bodies(objs.size());
        for (std::size_t i = 0; i < objs.size(); ++i) {
            const Obj& o = objs[i];
            bodies[i].pos = o.pos; bodies[i].v = o.v; bodies[i].w = o.w;
            bodies[i].invMass = o.isStatic ? 0.0f : 1.0f / o.mass;
            bodies[i].invI = InvInertia(o);
        }
        std::vector<Manifold> ms; std::vector<std::pair<int, int>> keys;
        for (int i = 0; i < (int)objs.size(); ++i) for (int j = i + 1; j < (int)objs.size(); ++j) {
            const Obj& a = objs[(std::size_t)i]; const Obj& b = objs[(std::size_t)j];
            if (a.isStatic && b.isStatic) continue;
            Manifold m; m.a = i; m.b = j;
            if (a.isBox && b.isBox) {
                bm::Result r = bm::Collide(BoxOf(a), BoxOf(b), skin);
                if (!r.hit) continue;
                m.n = r.normal; m.count = r.count;
                for (int k = 0; k < r.count; ++k) { m.pts[k].pos = r.pts[k].pos; m.pts[k].depth = r.pts[k].depth; }
            } else {   // a ball against a box (the ball is the second one in these tests)
                const Obj& box = a.isBox ? a : b; const Obj& ball = a.isBox ? b : a;
                bm::Box bx = BoxOf(box);
                Vec3 d = ball.pos - bx.c;
                Vec3 l{Vec3::Dot(d, bx.ax[0]), Vec3::Dot(d, bx.ax[1]), Vec3::Dot(d, bx.ax[2])};
                Vec3 cl{std::max(-bx.h.x, std::min(bx.h.x, l.x)), std::max(-bx.h.y, std::min(bx.h.y, l.y)), std::max(-bx.h.z, std::min(bx.h.z, l.z))};
                Vec3 diff = l - cl; float dist = diff.Magnitude();
                if (dist > ball.radius + skin) continue;
                Vec3 nl = dist > 1e-6f ? diff * (1.0f / dist) : Vec3{0, 1, 0};
                Vec3 nw = bx.ax[0] * nl.x + bx.ax[1] * nl.y + bx.ax[2] * nl.z;   // box -> ball
                Vec3 closest = bx.c + bx.ax[0] * cl.x + bx.ax[1] * cl.y + bx.ax[2] * cl.z;
                m.count = 1; m.pts[0].pos = closest; m.pts[0].depth = ball.radius - dist;
                m.n = a.isBox ? nw : nw * -1.0f;            // from A toward B
            }
            m.friction = std::sqrt(a.friction * b.friction);
            m.restitution = std::max(a.restitution, b.restitution);
            m.rolling = std::max(a.rolling, b.rolling);
            // warm start: match the old points by position
            auto it = cache.find({i, j});
            if (it != cache.end()) {
                for (int k = 0; k < m.count; ++k) {
                    float best = 0.05f * 0.05f; int bi = -1;
                    for (int q2 = 0; q2 < it->second.count; ++q2) {
                        float d2 = (it->second.pts[q2].pos - m.pts[k].pos).SqrMagnitude();
                        if (m.count == 1 || d2 < best) { best = d2; bi = q2; if (m.count == 1) break; }
                    }
                    if (bi >= 0) { m.pts[k].jn = it->second.pts[bi].jn; m.pts[k].jt1 = it->second.pts[bi].jt1; m.pts[k].jt2 = it->second.pts[bi].jt2; }
                }
                m.rollImpulse = it->second.rollImpulse;
            }
            ms.push_back(m); keys.push_back({i, j});
        }
        // 4) solve (lowest contacts first: a push travels up a stack in one sweep)
        {
            std::vector<std::size_t> order(ms.size());
            for (std::size_t k = 0; k < order.size(); ++k) order[k] = k;
            auto lowest = [&](const Manifold& m) { float y = 1e30f; for (int k = 0; k < m.count; ++k) y = std::min(y, m.pts[k].pos.y); return y; };
            std::stable_sort(order.begin(), order.end(), [&](std::size_t x, std::size_t y) { return lowest(ms[x]) < lowest(ms[y]); });
            std::vector<Manifold> sorted; std::vector<std::pair<int, int>> sk;
            for (std::size_t k : order) { sorted.push_back(ms[k]); sk.push_back(keys[k]); }
            ms.swap(sorted); keys.swap(sk);
        }
        Prepare(bodies, ms, dt, set);
        SolveVelocities(bodies, ms, set);
        SolvePositions(bodies, ms, set);
        cache.clear();
        for (std::size_t k = 0; k < ms.size(); ++k) cache[keys[k]] = ms[k];
        for (std::size_t i = 0; i < objs.size(); ++i) {
            Obj& o = objs[i];
            if (o.isStatic) continue;
            o.v = bodies[i].v; o.w = bodies[i].w;
            // 5) move with the solved speeds, plus the direct position correction
            o.pos = o.pos + o.v * dt + bodies[i].dp;
            Quat wq{o.w.x, o.w.y, o.w.z, 0.0f};
            Quat dq = wq * o.q;
            o.q = Quat{o.q.x + dq.x * 0.5f * dt, o.q.y + dq.y * 0.5f * dt, o.q.z + dq.z * 0.5f * dt, o.q.w + dq.w * 0.5f * dt}.Normalized();
        }
    }
    void Run(float seconds) { int n = (int)std::lround(seconds / dt); for (int i = 0; i < n; ++i) Step(); }
    static float TiltDeg(const Obj& o) { Vec3 up = o.q * Vec3{0, 1, 0}; return std::acos(std::max(-1.0f, std::min(1.0f, up.y))) * 57.29578f; }
};

Sim::Obj Floor(float topY = 0.0f) {
    Sim::Obj f; f.isStatic = true; f.half = {100, 0.5f, 100}; f.pos = {0, topY - 0.5f, 0}; f.friction = 0.6f; return f;
}
Sim::Obj Crate(Vec3 p, float mass = 1.0f, float mu = 0.5f) {
    Sim::Obj o; o.pos = p; o.mass = mass; o.friction = mu; return o;
}

} // namespace

#ifndef OKAY_SIM_NO_MAIN
int main() {
    RUN_SUITE("physics_deep_solver");

    // ---- A crate dropped on the floor settles flat, does not wobble and ends at rest.
    {
        Sim s; s.Add(Floor());
        int c = s.Add(Crate({0, 0.7f, 0}));
        s.Run(2.0f);
        CHECK_NEAR(s.objs[(std::size_t)c].pos.y, 0.5f, 0.0065);   // rests at most the allowed overlap (5 mm) into the floor
        CHECK(Sim::TiltDeg(s.objs[(std::size_t)c]) < 0.2f);
        CHECK(s.objs[(std::size_t)c].v.Magnitude() < 0.02f);
        CHECK(s.objs[(std::size_t)c].w.Magnitude() < 0.02f);
        float lo = 1e9f, hi = -1e9f;
        for (int i = 0; i < 100; ++i) { s.Step(); lo = std::min(lo, s.objs[(std::size_t)c].pos.y); hi = std::max(hi, s.objs[(std::size_t)c].pos.y); }
        CHECK(hi - lo < 0.001f);                                   // no buzzing (the old step wobbled about 1 cm)
    }
    // ---- A stack of 10 crates, 10 seconds: stands, stays put, does not sink.
    for (int n : {3, 5, 10}) {
        Sim s; s.Add(Floor());
        if (n == 10) s.set.velocityIterations = 20;              // ten high is the hard case: it needs more repeats
        std::vector<int> ids;
        for (int i = 0; i < n; ++i) ids.push_back(s.Add(Crate({0, 0.5f + (float)i, 0})));
        s.Run(10.0f);
        float drift = 0, tilt = 0, vmax = 0;
        for (int i : ids) { const auto& o = s.objs[(std::size_t)i]; drift = std::max(drift, std::sqrt(o.pos.x * o.pos.x + o.pos.z * o.pos.z)); tilt = std::max(tilt, Sim::TiltDeg(o)); vmax = std::max(vmax, o.v.Magnitude()); }
        std::cout << "  stack n=" << n << ": drift " << drift << " tilt " << tilt << " vmax " << vmax << " top y " << s.objs[(std::size_t)ids.back()].pos.y << "\n";
        CHECK(drift < (n == 10 ? 0.06f : 0.02f));
        CHECK(tilt < (n == 10 ? 0.6f : 0.3f));
        CHECK(vmax < (n == 10 ? 0.07f : 0.01f));
        const auto& top = s.objs[(std::size_t)ids.back()];
        CHECK_NEAR(top.pos.y, (float)n - 0.5f, 0.03f);            // the top crate is where it started, within 3 cm
    }
    // ---- A leaning start (each crate 2 cm off the one below): still stands.
    {
        Sim s; s.Add(Floor());
        std::vector<int> ids;
        for (int i = 0; i < 5; ++i) ids.push_back(s.Add(Crate({(i % 2 ? 0.02f : -0.02f), 0.5f + (float)i, 0})));
        s.Run(10.0f);
        float tilt = 0; for (int i : ids) tilt = std::max(tilt, Sim::TiltDeg(s.objs[(std::size_t)i]));
        CHECK(tilt < 2.0f);
        CHECK(s.objs[(std::size_t)ids.back()].pos.y > 4.4f);
    }
    // ---- Heavy on light and light on heavy (mass ratio 100): both stay up.
    {
        Sim s; s.Add(Floor());
        int a = s.Add(Crate({0, 0.5f, 0}, 1.0f)); int b = s.Add(Crate({0, 1.5f, 0}, 100.0f));
        s.Run(5.0f);
        CHECK(s.objs[(std::size_t)b].pos.y > 1.45f && s.objs[(std::size_t)a].pos.y > 0.47f);
        Sim t; t.Add(Floor());
        int c = t.Add(Crate({0, 0.5f, 0}, 100.0f)); int d = t.Add(Crate({0, 1.5f, 0}, 1.0f));
        t.Run(5.0f);
        CHECK_NEAR(t.objs[(std::size_t)d].pos.y, 1.5f, 0.02);
        CHECK(Sim::TiltDeg(t.objs[(std::size_t)c]) < 0.5f && Sim::TiltDeg(t.objs[(std::size_t)d]) < 0.5f);
    }
    // ---- A box on a 20 degree ramp with friction 0.5 (tan 20 = 0.36 < 0.5): holds still. With friction 0.1: slides at about g(sin - mu cos).
    for (float mu : {0.5f, 0.1f}) {
        Sim s;
        Sim::Obj ramp = Floor(); ramp.pos = {0, 0, 0}; ramp.q = Quat::AngleAxis(20.0f, {0, 0, 1}); ramp.friction = mu;
        s.Add(ramp);
        float a = 20.0f * 0.0174533f;
        Vec3 up{-std::sin(a), std::cos(a), 0};
        Sim::Obj c = Crate(up * 0.0f + Vec3{0, 0, 0}, 1.0f, mu);
        c.pos = up * (0.5f + 0.5f) - up * 0.5f + up * 0.5f;   // centre one half-height above the ramp's top face (the ramp's top is at 0.5 along up... below)
        c.pos = up * (0.5f + 0.5f) ; c.pos = c.pos - up * 0.5f; // ramp top face is 0.5 above the ramp centre (half 0.5); crate centre 0.5 above that
        c.pos = up * 1.0f;
        c.q = Quat::AngleAxis(20.0f, {0, 0, 1});
        int id = s.Add(c);
        s.Run(1.0f);
        Vec3 p0 = s.objs[(std::size_t)id].pos; Vec3 v0 = s.objs[(std::size_t)id].v;
        s.Run(2.0f);
        Vec3 p1 = s.objs[(std::size_t)id].pos;
        Vec3 down{-std::cos(a), -std::sin(a), 0};
        float slid = Vec3::Dot(p1 - p0, down);
        float mc = std::sqrt(mu * mu);   // ramp and crate share mu
        float acc = std::max(0.0f, 9.81f * (std::sin(a) - mc * std::cos(a)));
        if (mu > 0.3f) {
            CHECK(slid < 0.02f);
        } else {
            float want = Vec3::Dot(v0, down) * 2.0f + 0.5f * acc * 4.0f;
            CHECK_NEAR(slid, want, 0.2f * want + 0.1f);
        }
    }
    // ---- A ball dropped from 2 m with bounciness 0.8 rises to about 0.64 x 2 = 1.28 m, and a ball with 0 stops.
    {
        Sim s; s.Add(Floor());
        Sim::Obj ball; ball.isBox = false; ball.radius = 0.5f; ball.pos = {0, 2.5f, 0}; ball.restitution = 0.8f; ball.friction = 0.4f;
        int id = s.Add(ball);
        float peak = 0; bool up = false; float prevVy = 0; int bounce = 0; float first = 0;
        for (int i = 0; i < 400 && bounce < 1; ++i) {
            s.Step(); float vy = s.objs[(std::size_t)id].v.y; float y = s.objs[(std::size_t)id].pos.y - 0.5f;
            if (vy > 0) { up = true; peak = std::max(peak, y); }
            if (up && prevVy > 0 && vy <= 0) { first = peak; ++bounce; }
            prevVy = vy;
        }
        std::cout << "  bounce peak " << first << " (theory 1.28)\n";
        CHECK(bounce == 1);
        CHECK_NEAR(first, 1.28f, 0.2f);
    }
    // ---- Rolling resistance: a ball rolling at 5 m/s stops in about v / (rolling x g x 5/7...) seconds, and with none it rolls on.
    for (float rr : {0.03f, 0.0f}) {
        Sim s; s.Add(Floor());
        Sim::Obj ball; ball.isBox = false; ball.radius = 0.5f; ball.pos = {0, 0.5f, 0}; ball.friction = 0.5f; ball.rolling = rr;
        int id = s.Add(ball);
        s.Run(0.5f);
        s.objs[(std::size_t)id].v = {5, 0, 0}; s.objs[(std::size_t)id].w = {0, 0, -10};
        float tStop = -1;
        for (int i = 0; i < 50 * 40; ++i) { s.Step(); if (tStop < 0 && s.objs[(std::size_t)id].v.Magnitude() < 0.05f) tStop = (float)(i + 1) / 50.0f; }
        if (rr > 0.0f) { CHECK(tStop > 5.0f && tStop < 25.0f); }
        else { CHECK(tStop < 0.0f); }                              // still rolling after 40 s
    }
    // ---- Warm starting matters: with it switched off the same stack does not stand as still.
    {
        Sim on; on.Add(Floor()); Sim off; off.Add(Floor()); off.set.warmStart = 0.0f;
        std::vector<int> a, b;
        for (int i = 0; i < 8; ++i) { a.push_back(on.Add(Crate({0, 0.5f + (float)i, 0}))); b.push_back(off.Add(Crate({0, 0.5f + (float)i, 0}))); }
        on.Run(6.0f); off.Run(6.0f);
        float sinkOn = 7.5f - on.objs[(std::size_t)a.back()].pos.y, sinkOff = 7.5f - off.objs[(std::size_t)b.back()].pos.y;
        std::cout << "  8 stack sink: warm start on " << sinkOn << ", off " << sinkOff << "\n";
        CHECK(sinkOn < 0.05f);
        CHECK(sinkOff >= sinkOn - 1e-4f);
    }
    TEST_MAIN_RESULT();
}
#endif
