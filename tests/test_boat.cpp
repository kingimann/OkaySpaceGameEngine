// Water surface, buoyancy and the boat (docs/water_boat.md). Everything here is deterministic: the waves are seeded, the
// physics runs in fixed steps, and each check says what it proves. Frame-rate checks run the same scene at 30, 60 and
// 144 frames per second and compare.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Environment/WaterWaves.hpp"
#include "okay/Components/Buoyancy.hpp"
#include "okay/Components/BoatController.hpp"
#include "okay/Scene/SceneTemplates.hpp"
#include "okay/Scene/PropertyRegistry.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace okay;

namespace {

bool AllFinite(const Vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

// A scene with one big Water. flat = no waves at all (waveHeight 0). depth 20 m.
struct World {
    Scene scene{"boat"};
    Water* water = nullptr;
    explicit World(bool flat = true, Water::WaveStyle style = Water::WaveStyle::Classic) {
        scene.fixedTimeStep = 1.0f / 50.0f;
        GameObject* lake = scene.CreateGameObject("Lake");
        water = lake->AddComponent<Water>();
        water->size = 1000.0f; water->resolution = 8; water->depth = 20.0f;
        water->style = style;
        if (flat) water->waveHeight = 0.0f;
        else { water->waveHeight = 0.6f; water->waveLength = 14.0f; water->waveSteepness = 0.35f; water->waveSeed = 11; }
        water->Apply();
    }
    GameObject* Crate(const Vec3& at, float mass, const char* name = "Crate") {
        GameObject* o = scene.CreateGameObject(name);
        o->transform->localPosition = at;
        o->AddComponent<BoxCollider3D>()->size = {1, 1, 1};
        auto* rb = o->AddComponent<Rigidbody3D>();
        rb->mass = mass; rb->freezeRotation = false; rb->allowSleep = false;
        o->AddComponent<Buoyancy>();
        return o;
    }
    // Run `seconds` of play at `fps` frames per second.
    void Run(float seconds, float fps) {
        const int n = (int)std::lround(seconds * fps);
        for (int i = 0; i < n; ++i) scene.Update(1.0f / fps);
    }
};

float YOf(GameObject* g) { return g->transform->Position().y; }

} // namespace

int main() {
    RUN_SUITE("boat");

    // ---------------------------------------------------------------- (1) the wave function itself
    {
        waves::Params p; p.count = 4; p.height = 0.8f; p.length = 12.0f; p.directionDeg = 30.0f; p.steepness = 0.5f; p.seed = 3;
        waves::Field f(p), g(p);
        // Same seed and settings: bit-for-bit the same numbers. A different seed: a different sea.
        bool same = true, differs = false;
        waves::Params q = p; q.seed = 4; waves::Field other(q);
        for (int i = 0; i < 50; ++i) {
            const float x = -20.0f + 0.9f * i, z = 7.0f - 0.6f * i, t = 0.37f * i;
            if (f.Height(x, z, t) != g.Height(x, z, t)) same = false;
            if (std::fabs(f.Height(x, z, t) - other.Height(x, z, t)) > 1e-3f) differs = true;
        }
        CHECK(same);
        CHECK(differs);

        // The surface never goes beyond the sum of the amplitudes, and half of the stated height is the largest swing.
        float lo = 1e9f, hi = -1e9f;
        for (int i = 0; i < 4000; ++i) {
            const float h = f.Height(0.37f * i, 0.11f * i, 0.013f * i);
            lo = std::fmin(lo, h); hi = std::fmax(hi, h);
        }
        CHECK(hi <= f.MaxAmplitude() + 1e-3f && lo >= -f.MaxAmplitude() - 1e-3f);
        CHECK_NEAR(f.MaxAmplitude(), 0.4f, 1e-4f);   // height 0.8 crest to trough = 0.4 amplitude
        CHECK(hi > 0.15f && lo < -0.15f);             // and it really moves

        // The normal is the real surface normal: it matches the slope of Height by finite differences.
        // The velocity is the real water velocity: it matches the movement of one bit of water over time.
        double worstN = 0.0, worstV = 0.0, worstRound = 0.0;
        for (int i = 0; i < 60; ++i) {
            const float x = -15.0f + 0.53f * i, z = 4.0f + 0.31f * i, t = 0.21f * i;
            const waves::Sample s = f.At(x, z, t);
            const float e = 0.01f;
            const double hx = (f.Height(x + e, z, t) - f.Height(x - e, z, t)) / (2.0 * e);
            const double hz = (f.Height(x, z + e, t) - f.Height(x, z - e, t)) / (2.0 * e);
            const double l = std::sqrt(hx * hx + 1.0 + hz * hz);
            worstN = std::fmax(worstN, std::fabs(s.nx - (-hx / l)) + std::fabs(s.ny - 1.0 / l) + std::fabs(s.nz - (-hz / l)));
            CHECK_NEAR(std::sqrt(s.nx * s.nx + s.ny * s.ny + s.nz * s.nz), 1.0, 1e-4);
            CHECK(s.ny > 0.0f);
            // one bit of water that rests at (x, z): its velocity is the change of its position
            float ax, ay, az, bx, by, bz;
            const float dt = 0.002f;
            f.Displace(x, z, t - dt, ax, ay, az); f.Displace(x, z, t + dt, bx, by, bz);
            float cx, cy, cz; f.Displace(x, z, t, cx, cy, cz);
            const waves::Sample w = f.At(cx, cz, t);
            worstV = std::fmax(worstV, std::fabs(w.vx - (bx - ax) / (2 * dt)) + std::fabs(w.vy - (by - ay) / (2 * dt)) + std::fabs(w.vz - (bz - az) / (2 * dt)));
            // The height asked at the shifted place is the height of that bit of water.
            worstRound = std::fmax(worstRound, std::fabs(f.Height(cx, cz, t) - cy));
        }
        CHECK(worstN < 0.01);
        CHECK(worstV < 0.01);
        CHECK(worstRound < 1e-3);

        // No NaN for odd settings (zero height, one wave, eight waves, steepness out of range, huge time).
        for (int n : {0, 1, 8, 50}) {
            for (float steep : {-1.0f, 0.0f, 1.0f, 5.0f}) {
                waves::Params r; r.count = n; r.steepness = steep; r.height = n == 8 ? 3.0f : 0.0f; r.length = n == 50 ? 0.0f : 5.0f;
                waves::Field h(r);
                const waves::Sample s = h.At(3.0f, -2.0f, 1.0e5f);
                CHECK(std::isfinite(s.height) && std::isfinite(s.nx) && std::isfinite(s.ny) && std::isfinite(s.vx) && std::isfinite(s.vy));
            }
        }
        // Waves run in the direction they are told: the crest of the main swell moves along +X for direction 0.
        waves::Params d; d.count = 1; d.directionDeg = 0.0f; d.spreadDeg = 0.0f; d.height = 1.0f; d.steepness = 0.0f;
        waves::Field one(d);
        const float h0 = one.Height(0.0f, 0.0f, 0.0f), h1 = one.Height(0.0f, 5.0f, 0.0f);
        CHECK_NEAR(h0, h1, 1e-3f);   // along Z (across the swell) the height is the same
    }

    // ---------------------------------------------------------------- the Water component's new surface API
    {
        World w(false, Water::WaveStyle::Ocean);
        w.water->waveSpeed = 1.0f;
        w.Run(0.5f, 60.0f);
        Water::Surface s;
        CHECK(w.water->SurfaceAt({3, 5, -4}, s));
        CHECK(std::isfinite(s.height));
        CHECK_NEAR(std::sqrt(s.normal.x * s.normal.x + s.normal.y * s.normal.y + s.normal.z * s.normal.z), 1.0, 1e-3);
        CHECK(s.floorY < s.height - 19.0f && s.floorY > s.height - 21.0f);   // depth 20
        CHECK(!w.water->SurfaceAt({600, 0, 0}, s));                          // off the edge
        CHECK(w.water->SurfaceAt({501, 0, 0}, s, 2.0f));                     // inside the margin
        // The drawn mesh is the same surface: every vertex lies on the height the physics asks for.
        auto* mr = w.water->gameObject->GetComponent<MeshRenderer>();
        double worst = 0.0;
        for (const Vec3& v : mr->mesh.vertices) {
            Water::Surface t;
            if (!w.water->SurfaceAt({v.x, 0.0f, v.z}, t, 1.0f)) continue;
            worst = std::fmax(worst, std::fabs(t.height - v.y));
        }
        CHECK(worst < 5e-3);
        // Calm lake is lower than the ocean with the same settings; flat water is flat.
        World calm(false, Water::WaveStyle::Calm), ocean(false, Water::WaveStyle::Ocean), flat(true, Water::WaveStyle::Ocean);
        double rc = 0, ro = 0, rf = 0;
        for (int i = 0; i < 200; ++i) {
            Water::Surface a, b, c;
            const Vec3 p{-30.0f + 0.3f * i, 0, 10.0f - 0.1f * i};
            calm.water->SurfaceAt(p, a); ocean.water->SurfaceAt(p, b); flat.water->SurfaceAt(p, c);
            rc = std::fmax(rc, std::fabs(a.height)); ro = std::fmax(ro, std::fabs(b.height)); rf = std::fmax(rf, std::fabs(c.height));
        }
        CHECK(rc < ro);
        CHECK(rf < 1e-4);
        // A current shows up in the flow, and the old Classic style still answers.
        flat.water->current = {2.0f, -1.0f};
        flat.water->SurfaceAt({0, 0, 0}, s);
        CHECK_NEAR(s.velocity.x, 2.0f, 1e-3f); CHECK_NEAR(s.velocity.z, -1.0f, 1e-3f);
        World classic(false, Water::WaveStyle::Classic);
        classic.Run(1.0f, 60.0f);
        CHECK(classic.water->SurfaceAt({1, 0, 1}, s) && std::isfinite(s.height) && s.normal.y > 0.5f);
    }

    // ---------------------------------------------------------------- (2) buoyancy: draft, sinking, frame rate, energy
    {
        // A 1 m crate in water of density 1 floats with a draft of (mass / 1): mass 0.5 sits half under, so its centre is
        // at the waterline; mass 0.25 sits a quarter under, so its centre is 0.25 above the waterline.
        const float masses[] = {0.5f, 0.25f, 0.8f};
        const float expectY[] = {0.0f, 0.25f, -0.3f};
        for (int i = 0; i < 3; ++i) {
            World w;
            GameObject* c = w.Crate({0, 1.5f, 0}, masses[i]);
            w.Run(10.0f, 60.0f);
            CHECK_NEAR(YOf(c), expectY[i], 0.03);
            auto* rb = c->GetComponent<Rigidbody3D>();
            CHECK(AllFinite(rb->velocity) && rb->velocity.Magnitude() < 0.05f);   // and it has come to rest
            CHECK(c->transform->Up().y > 0.999f);                              // still upright
        }
        // A heavy crate sinks, and keeps going down (it does not bob up again).
        {
            World w;
            GameObject* c = w.Crate({0, 1.0f, 0}, 2.0f, "Stone");
            w.Run(1.0f, 60.0f);
            float prev = YOf(c); bool monotone = true;
            for (int i = 0; i < 20; ++i) { w.Run(0.25f, 60.0f); if (YOf(c) > prev + 1e-3f) monotone = false; prev = YOf(c); }
            CHECK(monotone);
            CHECK(YOf(c) < -2.0f);
            CHECK(AllFinite(c->GetComponent<Rigidbody3D>()->velocity));
        }
        // Frame rate: the same scene at 30, 60 and 144 fps gives the same bobbing and the same rest height.
        {
            const float fps[] = {30.0f, 60.0f, 144.0f};
            float at1[3], at6[3];
            for (int i = 0; i < 3; ++i) {
                World w;
                GameObject* c = w.Crate({0, 1.5f, 0}, 0.5f);
                w.Run(1.0f, fps[i]); at1[i] = YOf(c);
                w.Run(5.0f, fps[i]); at6[i] = YOf(c);
            }
            for (int i = 1; i < 3; ++i) {
                CHECK_NEAR(at1[i], at1[0], 0.06);
                CHECK_NEAR(at6[i], at6[0], 0.02);
            }
        }
        // Energy: a crate dropped from 4 m never moves faster than a free fall of that height, and it ends at rest.
        // On waves too (every frame rate): nothing is NaN, speeds stay modest, nothing is thrown out of the water.
        for (float fps : {30.0f, 60.0f, 144.0f}) {
            World w(false, Water::WaveStyle::Ocean);
            GameObject* c = w.Crate({0, 4.0f, 0}, 0.5f);
            auto* rb = c->GetComponent<Rigidbody3D>();
            float vmax = 0.0f, ymax = -1e9f; bool ok = true;
            const int frames = (int)(20.0f * fps);
            for (int i = 0; i < frames; ++i) {
                w.scene.Update(1.0f / fps);
                if (!AllFinite(rb->velocity) || !AllFinite(c->transform->Position())) { ok = false; break; }
                vmax = std::fmax(vmax, rb->velocity.Magnitude());
                if (i > (int)(2.0f * fps)) ymax = std::fmax(ymax, YOf(c));
            }
            CHECK(ok);
            CHECK(vmax < std::sqrt(2.0f * 9.81f * 4.4f) * 1.02f);   // free fall from 4 m (plus the wave height) is the most it can have
            CHECK(ymax < 1.5f);                                     // after landing it stays with the waves
            CHECK(rb->velocity.Magnitude() < 3.0f);
        }
        // A very light body (mass 0.02): resting at its draft it stays calm; flung deep under it is pushed hard but the
        // push is capped, so nothing is thrown out of the scene and nothing turns NaN (at the worst frame rate too).
        {
            World w;
            GameObject* c = w.Crate({0, 0.48f, 0}, 0.02f, "Balloon");
            auto* rb = c->GetComponent<Rigidbody3D>();
            float vmax = 0.0f; bool ok = true;
            for (int i = 0; i < 600; ++i) {
                w.scene.Update(1.0f / 30.0f);
                if (!AllFinite(rb->velocity)) { ok = false; break; }
                vmax = std::fmax(vmax, rb->velocity.Magnitude());
            }
            CHECK(ok);
            CHECK(vmax < 1.0f);
            CHECK_NEAR(YOf(c), 0.48f, 0.05);   // draft 0.02
            World d;
            GameObject* e = d.Crate({0, -1.0f, 0}, 0.02f, "Balloon2");
            auto* rb2 = e->GetComponent<Rigidbody3D>();
            float v2 = 0.0f, y2 = -1e9f; bool ok2 = true;
            for (int i = 0; i < 1200; ++i) {
                d.scene.Update(1.0f / 30.0f);
                if (!AllFinite(rb2->velocity)) { ok2 = false; break; }
                v2 = std::fmax(v2, rb2->velocity.Magnitude());
                y2 = std::fmax(y2, YOf(e));
            }
            CHECK(ok2);
            // The push is capped at 12 g, so from 1 m down it cannot go faster than sqrt(2 * (120 - 9.81) * 1 m) = 14.9 m/s
            // and so cannot rise above about 11 m: a cork can pop out of the water, but nothing blows up.
            CHECK(v2 < 15.5f);
            CHECK(y2 < 12.0f);
        }
        // A box tipped 20 degrees in still water rights itself (the points on the low side go deeper).
        {
            World w;
            GameObject* b = w.Crate({0, 0.0f, 0}, 1.5f, "Barge");
            b->transform->localScale = {2.0f, 1.0f, 3.0f};
            b->transform->localRotation = Quat::Euler({0, 0, 20});
            w.Run(12.0f, 60.0f);
            CHECK(b->transform->Up().y > std::cos(2.0f * 0.0174533f));
        }
        // Splash events: a crate dropped from 2 m raises an entering splash; it calls the callback too.
        {
            World w;
            GameObject* c = w.Crate({0, 2.0f, 0}, 0.5f);
            auto* bu = c->GetComponent<Buoyancy>();
            int calls = 0; float strongest = 0.0f;
            bu->onSplash = [&](const Buoyancy::Splash& s) { ++calls; strongest = std::fmax(strongest, s.strength); };
            w.Run(3.0f, 60.0f);
            CHECK(bu->SplashCount() >= 1);
            CHECK(calls == bu->SplashCount());
            CHECK(strongest > 3.0f);
            CHECK(bu->LastSplash().entering || calls > 1);
            CHECK_NEAR(bu->LastSplash().position.y, 0.0f, 0.2f);   // at the water surface
            // spray is thrown: an object named "Splash" with live particles was made
            GameObject* sp = w.scene.Find("Splash");
            CHECK(sp != nullptr);
        }
        // A current carries a floating crate along: it ends up moving with the water.
        {
            World w;
            w.water->current = {2.0f, 0.0f};
            GameObject* c = w.Crate({0, 0.0f, 0}, 0.5f);
            w.Run(25.0f, 60.0f);
            auto* rb = c->GetComponent<Rigidbody3D>();
            CHECK_NEAR(rb->velocity.x, 2.0f, 0.2);
            CHECK(c->transform->Position().x > 20.0f);
        }
        // Your own float points, and the text form they are saved as.
        {
            World w;
            GameObject* c = w.Crate({0, 0.0f, 0}, 0.5f);
            auto* bu = c->GetComponent<Buoyancy>();
            bu->SetPointsText("0.5,0,0.5;-0.5,0,0.5;0.5,0,-0.5;-0.5,0,-0.5; nonsense ;1,2");
            CHECK(bu->points.size() == 4);
            bu->SetPointsText("0.5,0,0.5;-0.5,0,0.5;0.5,0,-0.5;-0.5,0,-0.5");
            CHECK(bu->PointsText() == "0.5,0,0.5;-0.5,0,0.5;0.5,0,-0.5;-0.5,0,-0.5");
            bu->points.push_back({0, 0, 0});   // changed from code: the text follows
            CHECK(bu->PointsText() == "0.5,0,0.5;-0.5,0,0.5;0.5,0,-0.5;-0.5,0,-0.5;0,0,0");
            bu->points.pop_back();
            std::vector<Vec3> pts; float vol = 0, h = 0;
            bu->WorldPoints(pts, vol, h);
            CHECK(pts.size() == 4);
            CHECK_NEAR(vol, 0.25f, 1e-4f);   // a 1 m box split over 4 points
            w.Run(8.0f, 60.0f);
            CHECK_NEAR(YOf(c), 0.0f, 0.05);
        }
        // Plain props of the Water that have no Buoyancy still float as before (the old behaviour is kept).
        {
            World w;
            GameObject* o = w.scene.CreateGameObject("Old Crate");
            o->transform->localPosition = {0, 1.0f, 0};
            o->AddComponent<BoxCollider3D>()->size = {1, 1, 1};
            auto* rb = o->AddComponent<Rigidbody3D>(); rb->mass = 0.5f;
            w.Run(10.0f, 60.0f);
            CHECK(YOf(o) > -0.4f && YOf(o) < 0.4f);
        }
    }

    // ---------------------------------------------------------------- bouncing at the surface (owner: "why do things start bouncing on the water")
    {
        // Trace the height of a 1 m crate dropped from 3 m. A "bounce" is a rise of at least 5 cm after the splash.
        struct Trace { std::vector<float> y; float t0 = 0; };
        auto bounces = [](const std::vector<float>& y, int from) {
            // local minima followed by a rise of >= 0.05 m, counted after the first minimum (the splash)
            int n = 0; float lowest = y[from]; bool rising = false, started = false; float lastMin = y[from];
            for (size_t i = (size_t)from + 1; i < y.size(); ++i) {
                if (y[i] < y[i - 1]) { if (rising && y[i - 1] - lastMin >= 0.05f && started) ++n; rising = false; lastMin = y[i]; started = true; }
                else rising = true;
                lowest = std::fmin(lowest, y[i]);
            }
            return n;
        };
        auto runCrate = [&](bool autoBuoy, float mass, float waveH, float fps, float& range5, int& bnc, float& meanY) {
            World w(waveH <= 0.0f);
            if (waveH > 0.0f) w.water->waveHeight = waveH;
            w.water->autoBuoyancy = autoBuoy;
            w.water->Apply();
            GameObject* o = w.scene.CreateGameObject("Plain Crate");   // no Buoyancy: only the Water floats it
            o->transform->localPosition = {0, 3.0f, 0};
            o->AddComponent<BoxCollider3D>()->size = {1, 1, 1};
            auto* rb = o->AddComponent<Rigidbody3D>(); rb->mass = mass; rb->allowSleep = false;
            std::vector<float> y; float lo = 1e9f, hi = -1e9f, sum = 0; int cnt = 0;
            const int frames = (int)(10.0f * fps);
            for (int i = 0; i < frames; ++i) {
                w.scene.Update(1.0f / fps);
                const float t = (i + 1) / fps;
                y.push_back(YOf(o));
                if (t >= 5.0f) { lo = std::fmin(lo, y.back()); hi = std::fmax(hi, y.back()); sum += y.back(); ++cnt; }
            }
            range5 = hi - lo; meanY = sum / (float)cnt;
            bnc = bounces(y, (int)(0.7f * fps));   // from the first contact on (a 3 m drop lands at about 0.7 s)
        };
        for (float mass : {0.5f, 0.9f}) {
            const float draft = mass;                 // density 1: draft = mass
            for (float fps : {30.0f, 60.0f, 144.0f}) {
                float rOld, rNew, mOld, mNew; int bOld, bNew;
                runCrate(false, mass, 0.0f, fps, rOld, bOld, mOld);
                runCrate(true, mass, 0.0f, fps, rNew, bNew, mNew);
                std::printf("  bounce mass %.1f fps %3.0f  OLD: bounces %d, motion after 5 s %.4f m, rest y %.3f   NEW: bounces %d, motion %.4f m, rest y %.3f\n",
                            mass, fps, bOld, rOld, mOld, bNew, rNew, mNew);
                CHECK(bNew <= 2);
                CHECK(rNew < 0.009f);                       // under 5% of the wave amplitude (0.175 m at height 0.35)
                CHECK_NEAR(mNew, 0.5f - draft, 0.03);       // the right draft
            }
        }
        // On waves (height 0.35 and 1.0): it follows the water, does not hop, and sits at the right draft on average.
        for (float waveH : {0.35f, 1.0f}) {
            for (float fps : {30.0f, 144.0f}) {
                float r, m; int b;
                runCrate(true, 0.5f, waveH, fps, r, b, m);
                std::printf("  waves %.2f fps %3.0f  NEW: bounces after splash %d, motion %.3f m (wave amplitude %.3f), mean y %.3f\n", waveH, fps, b, r, waveH * 0.5f, m);
                CHECK(r < waveH * 1.3f);                     // never moves more than the waves do
                CHECK_NEAR(m, 0.0f, 0.12 + waveH * 0.1);
            }
        }
        // A bouncy ball (restitution 1) does not bounce off the water: Water has no collider, so it just goes in.
        {
            World w;
            GameObject* o = w.scene.CreateGameObject("Ball");
            o->transform->localPosition = {0, 3.0f, 0};
            o->AddComponent<SphereCollider3D>()->radius = 0.5f;
            auto* rb = o->AddComponent<Rigidbody3D>(); rb->mass = 0.45f; rb->bounciness = 1.0f; rb->allowSleep = false;
            float lowest = 1e9f, rebound = 0.0f; bool landed = false;
            for (int i = 0; i < 6 * 60; ++i) {
                w.scene.Update(1.0f / 60.0f);
                const float y = YOf(o);
                if (y < lowest) lowest = y;
                else if (lowest < 0.5f) { landed = true; rebound = std::fmax(rebound, y - lowest); }
            }
            std::printf("  ball restitution 1: lowest %.3f m, highest rise after that %.3f m\n", lowest, rebound);
            CHECK(landed);
            CHECK(rebound < 0.5f);
        }
    }

    // ---------------------------------------------------------------- (3) the boat
    {
        // Afloat and level with no input.
        {
            World w;
            GameObject* boat = Templates::AddBoat(w.scene, {0, 0.0f, 0});
            w.Run(8.0f, 60.0f);
            auto* bu = boat->GetComponent<Buoyancy>();
            CHECK(bu != nullptr);
            CHECK(YOf(boat) > -0.3f && YOf(boat) < 0.3f);
            CHECK(boat->transform->Up().y > 0.995f);
            CHECK(bu->InWater());
            CHECK(AllFinite(boat->GetComponent<Rigidbody3D>()->velocity));
        }
        // Throttle: full ahead reaches the top speed. Half throttle about half of it. Let go and it coasts down.
        {
            World w;
            GameObject* boat = Templates::AddBoat(w.scene, {0, 0.0f, 0});
            auto* bc = boat->GetComponent<BoatController>();
            bc->isPlayer = false; bc->followCamera = false;
            bc->throttleInput = 1.0f;
            w.Run(20.0f, 60.0f);
            CHECK(bc->Speed() > bc->maxSpeed * 0.92f && bc->Speed() < bc->maxSpeed * 1.04f);
            CHECK(boat->transform->Position().z > 100.0f * 0.5f);      // it went forward (nose is +Z)
            CHECK(boat->transform->Up().y > 0.97f);                    // and did not tip over
            CHECK(bc->EngineRevs() > 0.7f);
            bc->throttleInput = 0.5f;
            w.Run(15.0f, 60.0f);
            CHECK_NEAR(bc->Speed(), bc->maxSpeed * 0.5f, 0.8);
            bc->throttleInput = 0.0f;
            w.Run(20.0f, 60.0f);
            CHECK(bc->Speed() < 3.0f && bc->Speed() >= -0.1f);         // coasting slows it
        }
        // Reverse is slower than forward, and braking stops it.
        {
            World w;
            GameObject* boat = Templates::AddBoat(w.scene, {0, 0.0f, 0});
            auto* bc = boat->GetComponent<BoatController>();
            bc->isPlayer = false; bc->followCamera = false;
            bc->throttleInput = -1.0f;
            w.Run(15.0f, 60.0f);
            CHECK(bc->Speed() < -bc->reverseSpeed * 0.9f && bc->Speed() > -bc->reverseSpeed * 1.1f);
            bc->throttleInput = 1.0f;
            w.Run(15.0f, 60.0f);
            const float cruising = bc->Speed();
            CHECK(cruising > 10.0f);
            bc->throttleInput = 0.0f; bc->brakeInput = true;
            w.Run(3.0f, 60.0f);
            CHECK(bc->Speed() < cruising * 0.3f);
        }
        // Steering: D turns right (towards +X for a boat facing +Z), A turns left; at rest with no throttle nothing turns.
        {
            auto heading = [](float steer, float throttle) {
                World w;
                GameObject* boat = Templates::AddBoat(w.scene, {0, 0.0f, 0});
                auto* bc = boat->GetComponent<BoatController>();
                bc->isPlayer = false; bc->followCamera = false;
                bc->throttleInput = throttle; bc->steerInput = steer;
                w.Run(5.0f, 60.0f);
                const Vec3 f = boat->transform->Forward();
                return std::atan2(f.x, f.z) * 57.29578f;   // degrees, + = turned right
            };
            CHECK(heading(1.0f, 1.0f) > 40.0f);
            CHECK(heading(-1.0f, 1.0f) < -40.0f);
            CHECK(std::fabs(heading(1.0f, 0.0f)) < 1.0f);
            CHECK(heading(1.0f, -1.0f) < -5.0f);    // reversing flips the steering like a car
            CHECK(std::fabs(heading(0.0f, 1.0f)) < 1.0f);
        }
        // Out of the water: three metres up, the engine and rudder do nothing.
        {
            World w;
            GameObject* boat = Templates::AddBoat(w.scene, {0, 3.0f, 0});
            auto* bc = boat->GetComponent<BoatController>();
            bc->isPlayer = false; bc->followCamera = false;
            bc->throttleInput = 1.0f; bc->steerInput = 1.0f;
            w.Run(0.4f, 60.0f);   // still falling (3 m takes 0.78 s)
            CHECK(bc->Control() < 0.05f);
            const Vec3 v = boat->GetComponent<Rigidbody3D>()->velocity;
            CHECK(std::sqrt(v.x * v.x + v.z * v.z) < 0.1f);
            CHECK(std::fabs(boat->GetComponent<Rigidbody3D>()->angularVelocity.y) < 0.05f);
        }
        // Frame rate: same drive at 30, 60 and 144 fps ends in the same place.
        {
            const float fps[] = {30.0f, 60.0f, 144.0f};
            Vec3 end[3]; float spd[3];
            for (int i = 0; i < 3; ++i) {
                World w;
                GameObject* boat = Templates::AddBoat(w.scene, {0, 0.0f, 0});
                auto* bc = boat->GetComponent<BoatController>();
                bc->isPlayer = false; bc->followCamera = false;
                bc->throttleInput = 1.0f; bc->steerInput = 0.4f;
                w.Run(10.0f, fps[i]);
                end[i] = boat->transform->Position(); spd[i] = bc->Speed();
            }
            for (int i = 1; i < 3; ++i) {
                CHECK((end[i] - end[0]).Magnitude() < 0.6f);
                CHECK_NEAR(spd[i], spd[0], 0.3);
            }
        }
        // On waves, with wild input, for a long time: no NaN, it stays upright, speeds stay sane, at every frame rate.
        for (float fps : {30.0f, 60.0f, 144.0f}) {
            World w(false, Water::WaveStyle::Ocean);
            GameObject* boat = Templates::AddBoat(w.scene, {0, 0.0f, 0});
            auto* bc = boat->GetComponent<BoatController>();
            auto* rb = boat->GetComponent<Rigidbody3D>();
            bc->isPlayer = false; bc->followCamera = false;
            bool ok = true; float vmax = 0.0f, upMin = 1.0f, ymin = 1e9f, ymax = -1e9f;
            const int frames = (int)(40.0f * fps);
            for (int i = 0; i < frames; ++i) {
                const float t = (float)i / fps;
                bc->throttleInput = std::sin(t * 0.7f) > -0.3f ? 1.0f : -1.0f;
                bc->steerInput = std::sin(t * 1.3f);
                w.scene.Update(1.0f / fps);
                if (!AllFinite(rb->velocity) || !AllFinite(boat->transform->Position()) || !AllFinite(rb->angularVelocity)) { ok = false; break; }
                vmax = std::fmax(vmax, rb->velocity.Magnitude());
                upMin = std::fmin(upMin, boat->transform->Up().y);
                ymin = std::fmin(ymin, YOf(boat)); ymax = std::fmax(ymax, YOf(boat));
            }
            CHECK(ok);
            CHECK(vmax < 16.0f);
            CHECK(upMin > 0.6f);      // never more than about 50 degrees from level
            CHECK(ymin > -1.5f && ymax < 1.5f);
            CHECK(rb->angularVelocity.Magnitude() < 6.0f);
        }
        // The keys: "Move" from the Input Actions drives it (W = throttle) when Is Player is on; the camera follows.
        {
            World w;
            GameObject* camObj = w.scene.CreateGameObject("Main Camera");
            auto* cam = camObj->AddComponent<Camera>(); cam->main = true;
            camObj->transform->localPosition = {0, 4, -10};
            w.scene.mainCamera = cam;
            GameObject* boat = Templates::AddBoat(w.scene, {0, 0.0f, 0});
            auto* bc = boat->GetComponent<BoatController>();
            CHECK(bc->isPlayer && bc->followCamera);
            for (int i = 0; i < 360; ++i) {
                InputActions::ContributeVector("Move", {0.0f, 1.0f});
                InputActions::Tick(1.0f / 60.0f);
                w.scene.Update(1.0f / 60.0f);
            }
            InputActions::ClearContributions();
            InputActions::Tick(1.0f / 60.0f);
            CHECK(boat->transform->Position().z > 15.0f);
            CHECK(bc->Speed() > 8.0f);
            const float dist = (camObj->transform->Position() - boat->transform->Position()).Magnitude();
            CHECK(dist < 16.0f && dist > 5.0f);                       // the camera is behind it, not stuck at the start
            CHECK(camObj->transform->Position().z < boat->transform->Position().z);
            // Is Player off: the keys are ignored.
            bc->isPlayer = false; bc->throttleInput = 0.0f;
            InputActions::ContributeVector("Move", {0.0f, 1.0f});
            InputActions::Tick(1.0f / 60.0f);
            w.Run(20.0f, 60.0f);
            InputActions::ClearContributions();
            InputActions::Tick(1.0f / 60.0f);
            CHECK(bc->Speed() < 2.0f);
        }
        // Wake and bow splash: driving fast makes foam trails and spray objects.
        {
            World w;
            GameObject* boat = Templates::AddBoat(w.scene, {0, 0.0f, 0});
            auto* bc = boat->GetComponent<BoatController>();
            bc->isPlayer = false; bc->followCamera = false; bc->throttleInput = 1.0f;
            w.Run(6.0f, 60.0f);
            GameObject* wake = w.scene.Find("Boat Wake Port");
            GameObject* bow = w.scene.Find("Boat Bow Splash");
            CHECK(wake != nullptr && bow != nullptr);
            if (wake) {
                auto* tr = wake->GetComponent<TrailRenderer>();
                CHECK(tr != nullptr && tr->PointCount() > 3);
            }
            if (bow) CHECK(bow->GetComponent<ParticleSystem>()->AliveCount() > 0);
            // The boat's own effects go when the boat goes.
            w.scene.Destroy(boat);
            w.Run(0.2f, 60.0f);
            CHECK(w.scene.Find("Boat Wake Port") == nullptr);
            CHECK(w.scene.Find("Boat Bow Splash") == nullptr);
        }
        // A boat without a Buoyancy gets one when Play starts (so Add Component 'Boat Controller' alone works).
        {
            World w;
            GameObject* o = w.scene.CreateGameObject("Skiff");
            o->AddComponent<BoxCollider3D>()->size = {2.0f, 0.9f, 4.6f};
            auto* rb = o->AddComponent<Rigidbody3D>(); rb->mass = 5.0f;
            o->AddComponent<BoatController>()->followCamera = false;
            w.Run(10.0f, 60.0f);
            CHECK(o->GetComponent<Buoyancy>() != nullptr);
            CHECK(YOf(o) > -1.0f && YOf(o) < 1.0f);
        }
    }

    // ---------------------------------------------------------------- saving and property reflection
    {
        World w(false, Water::WaveStyle::Ocean);
        w.water->waveDirection = 33.0f; w.water->waveSteepness = 0.42f; w.water->waveCount = 5; w.water->waveSeed = 99;
        w.water->current = {0.5f, -0.25f};
        GameObject* boat = Templates::AddBoat(w.scene, {4, 0.0f, 5}, 30.0f);
        auto* bu = boat->GetComponent<Buoyancy>();
        bu->SetPointsText("1,0,2;-1,0,2");
        bu->density = 1.1f; bu->dragSide = 3.5f; bu->splashEffect = false;
        auto* bc = boat->GetComponent<BoatController>();
        bc->maxSpeed = 17.0f; bc->turnRate = 70.0f; bc->wake = false; bc->camDistance = 12.0f;
        const std::string text = SceneSerializer::Serialize(w.scene);
        CHECK(text.find("waterwaves") != std::string::npos);
        CHECK(text.find("buoyancy") != std::string::npos);
        CHECK(text.find("  boat ") != std::string::npos);
        Scene s2("loaded");
        SceneSerializer::Deserialize(s2, text);
        Water* w2 = s2.Find("Lake") ? s2.Find("Lake")->GetComponent<Water>() : nullptr;
        CHECK(w2 != nullptr);
        if (w2) {
            CHECK(w2->style == Water::WaveStyle::Ocean);
            CHECK_NEAR(w2->waveDirection, 33.0f, 1e-3f); CHECK_NEAR(w2->waveSteepness, 0.42f, 1e-3f);
            CHECK(w2->waveCount == 5); CHECK(w2->waveSeed == 99);
            CHECK_NEAR(w2->current.x, 0.5f, 1e-4f); CHECK_NEAR(w2->current.y, -0.25f, 1e-4f);
        }
        GameObject* b2 = s2.Find("Boat");
        CHECK(b2 != nullptr);
        if (b2) {
            auto* u = b2->GetComponent<Buoyancy>();
            CHECK(u != nullptr && b2->GetComponents<Buoyancy>().size() == 1);
            if (u) {
                CHECK(u->points.size() == 2); CHECK_NEAR(u->density, 1.1f, 1e-4f); CHECK_NEAR(u->dragSide, 3.5f, 1e-4f);
                CHECK(!u->splashEffect); CHECK_NEAR(u->fullness, 0.55f, 1e-4f); CHECK(u->gridZ == 5);
            }
            auto* c = b2->GetComponent<BoatController>();
            CHECK(c != nullptr && b2->GetComponents<BoatController>().size() == 1);
            if (c) { CHECK_NEAR(c->maxSpeed, 17.0f, 1e-4f); CHECK_NEAR(c->turnRate, 70.0f, 1e-4f); CHECK(!c->wake); CHECK_NEAR(c->camDistance, 12.0f, 1e-4f); }
        }
        CHECK(SceneSerializer::Serialize(s2) == text);   // saving the loaded scene gives the same text
        // Property registry: the new settings can be read and set by name (the inspector and scripts use this).
        GameObject* g = boat;
        CHECK(PropertyRegistry::GetProperty(*g, "BoatController.maxSpeed") == PropertyValue::MakeFloat(17.0f));
        CHECK(PropertyRegistry::SetProperty(*g, "BoatController.maxSpeed", PropertyValue::MakeFloat(9.0f)));
        CHECK_NEAR(bc->maxSpeed, 9.0f, 1e-4f);
        CHECK(PropertyRegistry::SetProperty(*g, "Buoyancy.points", PropertyValue::MakeString("0,0,1;0,0,-1")));
        CHECK(bu->points.size() == 2);
        CHECK(PropertyRegistry::SetProperty(*w.water->gameObject, "Water.style", PropertyValue::MakeEnum(2)));
        CHECK(w.water->style == Water::WaveStyle::Calm);
        CHECK(PropertyRegistry::GetProperty(*w.water->gameObject, "Water.waveSeed") == PropertyValue::MakeInt(99));
    }

    // ---------------------------------------------------------------- (5) the shipped scene and the menu's boat
    {
        // The template builds, and the file in examples/boat/ is exactly what it saves (regenerate: OKAY_REGEN_BOAT=1).
        Scene t("t");
        Templates::BoatLake(t);
        const std::string text = SceneSerializer::Serialize(t);
        const std::string path = std::string(OKAY_SOURCE_DIR) + "/examples/boat/boat_lake.okayscene";
        if (std::getenv("OKAY_REGEN_BOAT")) { std::ofstream(path) << text; std::printf("  wrote %s\n", path.c_str()); }
        std::ifstream in(path);
        std::stringstream ss; ss << in.rdbuf();
        CHECK(in.good() || !ss.str().empty());
        // Object ids are made fresh every time, so the lines that hold them are left out of the comparison.
        auto noGuids = [](const std::string& t) {
            std::string out, line; std::istringstream is(t);
            while (std::getline(is, line)) if (line.rfind("  guid ", 0) != 0) out += line + "\n";
            return out;
        };
        CHECK(noGuids(ss.str()) == noGuids(text));

        // Open the file as a person would, press Play for 12 seconds: crates float, the stone sinks, the boat floats and drives.
        Scene s("play");
        s.fixedTimeStep = 1.0f / 50.0f;
        CHECK(SceneSerializer::Deserialize(s, ss.str()));
        CHECK(s.Find("Boat") != nullptr && s.Find("Lake") != nullptr);
        for (int i = 0; i < 12 * 60; ++i) s.Update(1.0f / 60.0f);
        int crates = 0, floating = 0;
        for (Rigidbody3D* r : s.FindObjectsOfType<Rigidbody3D>())
            if (r->gameObject->name == "Floating Crate") { ++crates; if (std::fabs(YOf(r->gameObject)) < 0.7f) ++floating; }
        CHECK(crates == 6 && floating == 6);
        GameObject* stone = s.Find("Heavy Stone (sinks)");
        CHECK(stone != nullptr && YOf(stone) < -3.0f);
        GameObject* boat = s.Find("Boat");
        CHECK(YOf(boat) > -0.5f && YOf(boat) < 0.5f);
        auto* bc = boat->GetComponent<BoatController>();
        bc->isPlayer = false; bc->throttleInput = 1.0f;
        for (int i = 0; i < 5 * 60; ++i) s.Update(1.0f / 60.0f);   // 5 s ahead is about 45 m: still on the lake
        CHECK(bc->Speed() > 6.0f);
        CHECK(AllFinite(boat->transform->Position()));

        // The menu's boat: into an empty scene it brings its own lake and floats at once.
        Scene m("menu");
        m.fixedTimeStep = 1.0f / 50.0f;
        GameObject* mb = Templates::AddBoat(m, {10, 0, 10});
        CHECK(!m.FindObjectsOfType<Water>().empty());
        for (int i = 0; i < 6 * 60; ++i) m.Update(1.0f / 60.0f);
        CHECK(YOf(mb) > -0.5f && YOf(mb) < 0.5f);
        // Into a scene that already has water, it does not add a second lake.
        Scene m2("menu2");
        { GameObject* l = m2.CreateGameObject("Water"); l->AddComponent<Water>()->depth = 5.0f; }
        Templates::AddBoat(m2, {0, 0, 0});
        CHECK(m2.FindObjectsOfType<Water>().size() == 1);
    }

    TEST_MAIN_RESULT();
}
