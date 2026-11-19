#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Components/WindZone.hpp"
#include "okay/Components/Foliage.hpp"
#include <cmath>
#include <string>

using namespace okay;

// Wind components in a scene (docs/wind.md): zones feed the field, particles / bodies / meshes / cables / sound follow,
// saving and loading, and "no Wind component = nothing changes".

namespace {
void Run(Scene& s, int frames, float dt = 1.0f / 60.0f) { for (int i = 0; i < frames; ++i) s.Update(dt); }
WindZone* MakeZone(Scene& s, const char* name, WindZone::Mode mode, Vec3 pos, float strength, Vec3 dirLocal = {1, 0, 0}) {
    GameObject* g = s.CreateGameObject(name);
    g->transform->SetPosition(pos);
    auto* z = g->AddComponent<WindZone>();
    z->mode = mode; z->strength = strength; z->direction = dirLocal; z->edge = 0.0f;
    return z;
}
}

int main() {
    RUN_SUITE("wind");
    Log::console = false;
    WindField::Publish({}); WindVector() = {0, 0, 0}; WindField::Clock() = 0.0f;

    // ---- no Wind component: the field is the weather's wind, exactly ----
    {
        Scene s("none");
        s.CreateGameObject("A");
        WindVector() = {2, 0, 1};
        Run(s, 3);
        CHECK(!WindField::Active());
        CHECK(WindField::At({4, 4, 4}) == WindVector());
        WindVector() = {0, 0, 0};
    }

    // ---- a zone in a scene: where it blows, aiming by turning the object, and going away with the object ----
    {
        Scene s("zone");
        WindZone* z = MakeZone(s, "Z", WindZone::Mode::Direction, {0, 0, 0}, 6.0f);
        z->size = {10, 10, 10};
        Run(s, 2);
        CHECK(WindField::Active());
        Vec3 w = WindField::At({1, 0, 0});
        CHECK_NEAR(w.x, 6.0f, 1e-3f); CHECK_NEAR(w.z, 0.0f, 1e-3f);
        CHECK(WindField::At({20, 0, 0}).Magnitude() < 1e-5f);   // outside
        CHECK(z->Covers({1, 0, 0}) && !z->Covers({20, 0, 0}));
        // turn the object a quarter turn: the wind turns with it
        z->gameObject->transform->SetRotation(Quat::Euler(0, 90, 0));
        Run(s, 1);
        w = WindField::At({1, 0, 0});
        CHECK(std::fabs(w.x) < 1e-3f && std::fabs(std::fabs(w.z) - 6.0f) < 1e-3f);
        CHECK_NEAR(z->WorldDirection().Magnitude(), 1.0f, 1e-4f);
        // switching it off removes it; switching on restores it
        z->gameObject->active = false;
        Run(s, 2);
        CHECK(!WindField::Active());
        z->gameObject->active = true;
        Run(s, 2);
        CHECK(WindField::Active());
        s.Destroy(z->gameObject);
        Run(s, 3);
        CHECK(!WindField::Active());
        CHECK(WindZone::Live().empty());
    }

    // ---- everywhere + local zone, priority, and the clock ----
    {
        Scene s("blend");
        WindZone* all = MakeZone(s, "All", WindZone::Mode::Everywhere, {0, 0, 0}, 4.0f, {0, 0, 1});
        all->additive = true; all->gustAmount = 0.5f; all->gustFrequency = 0.25f; all->gustSpeed = 0.0f;
        WindZone* calm = MakeZone(s, "Calm", WindZone::Mode::Direction, {50, 0, 0}, 0.0f);
        calm->size = {10, 10, 10}; calm->priority = 5;
        Run(s, 1);
        const float t0 = WindField::Clock();
        const Vec3 outside = WindField::At({0, 0, 0});
        CHECK(outside.z > 1.9f && outside.z < 6.1f);   // 4 +- 50% gust
        CHECK(WindField::At({50, 0, 0}).Magnitude() < 1e-4f);   // sheltered inside the calm zone
        Run(s, 60);   // one second more
        CHECK_NEAR(WindField::Clock() - t0, 1.0f, 0.02f);
        // The gust repeats every 4 seconds
        CHECK_NEAR(WindField::At({0, 0, 0}, 1.0f).z, WindField::At({0, 0, 0}, 5.0f).z, 1e-3f);
    }

    // ---- particles drift with a zone's wind where they fly ----
    {
        Scene s("particles");
        WindZone* z = MakeZone(s, "Z", WindZone::Mode::Direction, {0, 0, 0}, 8.0f);
        z->size = {1000, 1000, 1000};
        GameObject* pg = s.CreateGameObject("P");
        auto* ps = pg->AddComponent<ParticleSystem>();
        ps->gravity = {0, 0, 0};
        ps->windInfluence = 1.0f;
        Run(s, 120);
        float vx = 0; int n = 0;
        for (const auto& p : ps->Particles()) if (p.alive) { vx += p.velocity.x; ++n; }
        CHECK(n > 0);
        CHECK(n == 0 || vx / (float)n > 3.0f);
        ps->windInfluence = 0.0f;   // "Affected by wind" off: they ignore it
    }

    // ---- Wind drag pushes a light box downwind, a heavy one less; no component, no push ----
    {
        Scene s("drag");
        MakeZone(s, "Z", WindZone::Mode::Everywhere, {0, 0, 0}, 10.0f, {1, 0, 0});
        auto body = [&](const char* name, float mass, bool drag) {
            GameObject* g = s.CreateGameObject(name);
            auto* rb = g->AddComponent<Rigidbody3D>();
            rb->gravityScale = 0.0f; rb->mass = mass;
            if (drag) g->AddComponent<WindDrag>();
            return rb;
        };
        Rigidbody3D* light = body("light", 1.0f, true);
        Rigidbody3D* heavy = body("heavy", 200.0f, true);
        Rigidbody3D* plain = body("plain", 1.0f, false);
        Run(s, 180);
        CHECK(light->velocity.x > 1.0f);
        CHECK(light->velocity.x <= 10.5f);          // never faster than the wind
        CHECK(heavy->velocity.x < light->velocity.x);
        CHECK(std::fabs(plain->velocity.x) < 1e-4f);   // off unless the component is there
        CHECK(std::isfinite(light->velocity.x));
        CHECK(WindDrag::Force({0, 0, 0}, 1, 1, 1, 0.02f).Magnitude() == 0.0f);
        CHECK(WindDrag::Force({10, 0, 0}, 0, 1, 1, 0.02f).Magnitude() == 0.0f);
        // drag equation: 0.5 * 1.2 * 1 * 1 * 100 = 60 N for a heavy body
        CHECK_NEAR(WindDrag::Force({10, 0, 0}, 1, 1, 1000, 0.02f).x, 60.0f, 1e-3f);
        CHECK(WindDrag::Force({10, 0, 0}, 1, 1, 0.1f, 0.02f).x <= 0.1f * 10.0f / 0.02f + 1e-3f);   // the cap
    }

    // ---- Sway: the top moves downwind, the bottom stays, no wind restores it, the sway is not NaN ----
    {
        Scene s("sway");
        MakeZone(s, "Z", WindZone::Mode::Everywhere, {0, 0, 0}, 12.0f, {1, 0, 0});
        GameObject* g = s.CreateGameObject("Tree");
        auto* mr = g->AddComponent<MeshRenderer>();
        mr->mesh = Mesh::Cylinder(0.2f, 4.0f, 8);
        const Mesh base = mr->mesh;
        auto* sw = g->AddComponent<WindSway>();
        sw->amount = 0.5f; sw->maxDistance = 0.0f; sw->flutter = 0.0f;
        Run(s, 10);
        CHECK(sw->Active());
        float topShift = 0, botShift = 0; float top = -1e9f, bot = 1e9f;
        for (const Vec3& v : base.vertices) { top = std::max(top, v.y); bot = std::min(bot, v.y); }
        bool finite = true;
        for (std::size_t i = 0; i < base.vertices.size(); ++i) {
            const Vec3& a = base.vertices[i]; const Vec3& b = mr->mesh.vertices[i];
            finite = finite && std::isfinite(b.x) && std::isfinite(b.y) && std::isfinite(b.z);
            if (a.y > top - 1e-4f) topShift = std::max(topShift, b.x - a.x);
            if (a.y < bot + 1e-4f) botShift = std::max(botShift, std::fabs(b.x - a.x));
        }
        CHECK(finite);
        CHECK(topShift > 0.4f && topShift < 0.8f);   // about `amount` (the wind is a little over the 12 m/s mark: 1.0 lean)
        CHECK(botShift < 1e-5f);
        CHECK(WindSway::Bend(0.0f, 12.0f, 0.5f, 2.0f) == 0.0f);
        CHECK(WindSway::Bend(1.0f, 0.0f, 0.5f, 2.0f) == 0.0f);
        CHECK(WindSway::Bend(0.5f, 12.0f, 0.5f, 2.0f) < WindSway::Bend(1.0f, 12.0f, 0.5f, 2.0f));
        CHECK(WindSway::Bend(1.0f, 1e9f, 1.0f, 2.0f) <= 1.5f + 1e-4f);   // a hurricane does not fold it in half
        g->RemoveComponent(sw);   // removing puts the mesh back
        CHECK(mr->mesh.vertices == base.vertices);
    }
    // Flag mode: the pole side stays, the far side waves, and with the wind off nothing moves.
    {
        Scene s("flag");
        WindZone* w = MakeZone(s, "W", WindZone::Mode::Everywhere, {0, 0, 0}, 8.0f, {0, 0, 1});
        GameObject* g = s.CreateGameObject("Flag");
        auto* mr = g->AddComponent<MeshRenderer>();
        mr->mesh = Mesh::Quad(2.0f);
        const Mesh base = mr->mesh;
        auto* sw = g->AddComponent<WindSway>();
        sw->weight = WindSway::Weight::Flag; sw->turnToWind = false; sw->maxDistance = 0.0f; sw->amount = 0.5f;
        Run(s, 40);
        float poleMove = 0, edgeMove = 0, minX = 1e9f, maxX = -1e9f;
        for (const Vec3& v : base.vertices) { minX = std::min(minX, v.x); maxX = std::max(maxX, v.x); }
        for (std::size_t i = 0; i < base.vertices.size(); ++i) {
            const float d = (mr->mesh.vertices[i] - base.vertices[i]).Magnitude();
            if (base.vertices[i].x < minX + 1e-4f) poleMove = std::max(poleMove, d);
            if (base.vertices[i].x > maxX - 1e-4f) edgeMove = std::max(edgeMove, d);
        }
        CHECK(poleMove < 1e-5f);
        CHECK(edgeMove > 0.05f);
        w->strength = 0.0f;
        Run(s, 40);
        float still = 0;
        for (std::size_t i = 0; i < base.vertices.size(); ++i) still = std::max(still, (mr->mesh.vertices[i] - base.vertices[i]).Magnitude());
        CHECK(still < 0.01f + 0.5f * 0.0f);   // calm: the flag hangs (a small droop at most)
    }

    // ---- Foliage chunks sway only when asked ----
    {
        Scene s("foliage");
        MakeZone(s, "Z", WindZone::Mode::Everywhere, {0, 0, 0}, 12.0f, {1, 0, 0});
        GameObject* g = s.CreateGameObject("Forest");
        auto* f = g->AddComponent<Foliage>();
        f->mesh = "Tree";
        for (int i = 0; i < 5; ++i) { FoliageInstance in; in.position = {(float)i * 3.0f, 0, 0}; f->instances.push_back(in); }
        f->EnsureBuilt();
        int withSway = 0;
        for (const auto& go : s.Objects()) if (go && go->GetComponent<WindSway>()) ++withSway;
        CHECK(withSway == 0);   // default: off, nothing added
        f->windSway = 0.5f;
        f->RebuildAll();
        Run(s, 2);   // new chunk objects join the scene at the next update
        for (const auto& go : s.Objects()) if (go && go->GetComponent<WindSway>()) ++withSway;
        CHECK(withSway > 0);
        Run(s, 10);
        GameObject* chunk = nullptr;
        for (const auto& go : s.Objects()) if (go && go->GetComponent<WindSway>()) chunk = go.get();
        CHECK(chunk && chunk->GetComponent<WindSway>()->Active());
    }

    // ---- Cable swings only when asked ----
    {
        Scene s("cable");
        MakeZone(s, "Z", WindZone::Mode::Everywhere, {0, 0, 0}, 12.0f, {1, 0, 0});
        GameObject* g = s.CreateGameObject("Rope");
        auto* c = g->AddComponent<Cable>();
        c->endOffset = {0, 0, 6}; c->sag = 0.5f;
        Run(s, 3);
        const auto still = g->GetComponent<LineRenderer>()->positions;
        c->windSwing = 1.0f;
        Run(s, 3);
        const auto moved = g->GetComponent<LineRenderer>()->positions;
        CHECK(still.size() == moved.size() && moved.size() > 4);
        CHECK(moved[moved.size() / 2].x - still[still.size() / 2].x > 0.5f);
        CHECK_NEAR(moved.front().x, still.front().x, 1e-5f);   // the ends stay
        CHECK_NEAR(moved.back().x, still.back().x, 1e-5f);
    }

    // ---- Trails drift, Spline Followers lean, Water reads the field ----
    {
        Scene s("more");
        MakeZone(s, "Z", WindZone::Mode::Everywhere, {0, 0, 0}, 10.0f, {1, 0, 0});
        GameObject* t = s.CreateGameObject("T");
        t->transform->SetPosition({0, 5, 0});
        auto* tr = t->AddComponent<TrailRenderer>();
        tr->time = 5.0f; tr->windDrift = 1.0f;
        GameObject* t0 = s.CreateGameObject("T0");
        t0->transform->SetPosition({0, 9, 0});
        auto* tr0 = t0->AddComponent<TrailRenderer>();
        tr0->time = 5.0f;   // no drift: stays put
        Run(s, 60);
        CHECK(tr->PointCount() > 0 && tr->Points().front().x > 5.0f);
        CHECK(tr0->PointCount() > 0 && std::fabs(tr0->Points().front().x) < 1e-5f);

        GameObject* sp = s.CreateGameObject("Rail");
        auto* spl = sp->AddComponent<Spline>();
        spl->AddPoint({0, 3, 0}); spl->AddPoint({0, 3, 10});
        GameObject* f = s.CreateGameObject("Lantern");
        auto* sf = f->AddComponent<SplineFollower>();
        sf->spline = "Rail"; sf->speed = 0.0f; sf->orient = false;
        Run(s, 3);
        const float x0 = f->transform->Position().x;
        sf->windSway = 1.0f;
        Run(s, 3);
        CHECK(f->transform->Position().x - x0 > 0.5f);

        Scene w("water");
        GameObject* wg = w.CreateGameObject("Sea");
        auto* wa = wg->AddComponent<Water>(); wa->depth = 3.0f; wa->Apply();
        Run(w, 5);
        float calm = 0, fl = 0, sy = 0;
        CHECK(wa->Column({3, 0, 2}, calm, fl));
        MakeZone(w, "Gale", WindZone::Mode::Everywhere, {0, 0, 0}, 20.0f);
        Run(w, 5);
        CHECK(wa->Column({3, 0, 2}, sy, fl) && std::fabs(sy - calm) > 1e-4f);   // the water now follows a Wind Zone, not only the weather
    }

    // ---- Sound: seamless, deterministic, follows the wind ----
    {
        const AudioClip a = WindAmbience::MakeClip(3.0f, 7, 8000), b = WindAmbience::MakeClip(3.0f, 7, 8000), c2 = WindAmbience::MakeClip(3.0f, 8, 8000);
        CHECK(a.samples == b.samples);
        CHECK(a.samples != c2.samples);
        CHECK(a.samples.size() == 24000);
        float peak = 0, step = 0; bool finite = true;
        for (std::size_t i = 0; i < a.samples.size(); ++i) {
            peak = std::max(peak, std::fabs(a.samples[i])); finite = finite && std::isfinite(a.samples[i]);
            if (i) step = std::max(step, std::fabs(a.samples[i] - a.samples[i - 1]));
        }
        CHECK(finite && peak > 0.3f && peak <= 0.81f);
        const float wrap = std::fabs(a.samples.front() - a.samples.back());   // the jump when the loop restarts
        CHECK(wrap <= step * 1.5f + 1e-4f);
        CHECK(WindAmbience::Loudness(0.0f, 0.3f, 12.0f) == 0.0f);
        CHECK(WindAmbience::Loudness(12.0f, 0.3f, 12.0f) == 1.0f);
        CHECK(WindAmbience::Loudness(6.0f, 0.3f, 12.0f) > 0.2f && WindAmbience::Loudness(6.0f, 0.3f, 12.0f) < 0.8f);
        CHECK(WindAmbience::Loudness(50.0f, 0.3f, 12.0f) == 1.0f);

        Scene s("sound");
        WindZone* w = MakeZone(s, "Z", WindZone::Mode::Everywhere, {0, 0, 0}, 0.0f);
        GameObject* g = s.CreateGameObject("Wind sound");
        auto* amb = g->AddComponent<WindAmbience>();
        amb->smoothing = 0.1f;
        Run(s, 60);
        CHECK(amb->Current() < 0.01f);
        w->strength = 12.0f;
        Run(s, 120);
        CHECK(amb->Current() > 0.5f);
        CHECK(g->GetComponent<AudioSource>() != nullptr);
        CHECK(g->GetComponent<AudioSource>()->loop);
        CHECK(g->GetComponent<AudioSource>()->volume > 0.5f);
    }

    // ---- Save and load: everything comes back, and saving again gives the same text ----
    {
        Scene a("save");
        WindZone* z = MakeZone(a, "Z", WindZone::Mode::Outward, {1, 2, 3}, -7.5f, {0, 1, 0});
        z->shape = 1; z->size = {3, 4, 5}; z->gustAmount = 0.4f; z->gustFrequency = 0.7f; z->turbulence = 0.25f; z->seed = 42;
        z->curveKeys = "0:0.2, 10:1.5, 20:0.2"; z->priority = 3; z->additive = true; z->falloff = 0.6f; z->edge = 0.5f;
        GameObject* g = a.CreateGameObject("B");
        g->AddComponent<WindDrag>()->area = 2.5f;
        auto* sw = g->AddComponent<WindSway>(); sw->amount = 1.25f; sw->weight = WindSway::Weight::Flag;
        g->AddComponent<WindAmbience>()->volume = 0.3f;
        g->AddComponent<Cable>()->windSwing = 0.75f;
        g->AddComponent<Foliage>()->windSway = 0.9f;
        const std::string text = SceneSerializer::Serialize(a);
        Scene b("load");
        std::string err;
        CHECK(SceneSerializer::Deserialize(b, text, &err, nullptr));
        CHECK(SceneSerializer::Serialize(b) == text);
        auto* z2 = b.FindObjectOfType<WindZone>();
        CHECK(z2 != nullptr);
        if (z2) {
            CHECK(z2->mode == WindZone::Mode::Outward && z2->shape == 1 && z2->seed == 42 && z2->priority == 3 && z2->additive);
            CHECK_NEAR(z2->strength, -7.5f, 1e-5f); CHECK_NEAR(z2->falloff, 0.6f, 1e-5f);
            CHECK(z2->curveKeys == "0:0.2, 10:1.5, 20:0.2");
            CHECK(z2->size == Vec3(3, 4, 5));
        }
        CHECK_NEAR(b.FindObjectOfType<WindDrag>()->area, 2.5f, 1e-5f);
        CHECK(b.FindObjectOfType<WindSway>()->weight == WindSway::Weight::Flag);
        CHECK_NEAR(b.FindObjectOfType<WindAmbience>()->volume, 0.3f, 1e-5f);
        CHECK_NEAR(b.FindObjectOfType<Cable>()->windSwing, 0.75f, 1e-5f);
        CHECK_NEAR(b.FindObjectOfType<Foliage>()->windSway, 0.9f, 1e-5f);
        // an old scene (no wind records) loads with every wind setting off
        Scene c("old");
        c.CreateGameObject("Old")->AddComponent<Cable>();
        const std::string oldText = SceneSerializer::Serialize(c);
        Scene d("old2");
        CHECK(SceneSerializer::Deserialize(d, oldText, &err, nullptr));
        CHECK(d.FindObjectOfType<Cable>()->windSwing == 0.0f);
        // the curve text
        const auto keys = WindZone::ParseCurve("0:1, bad, 5:x, 10:2;20:0.5");
        CHECK(keys.size() == 3);
        CHECK(WindZone::FormatCurve(keys) == "0:1, 10:2, 20:0.5");
    }

    // ---- A broken zone (zero size, NaN numbers) gives no NaN anywhere ----
    {
        Scene s("broken");
        WindZone* z = MakeZone(s, "Z", WindZone::Mode::Outward, {0, 0, 0}, std::nanf(""));
        z->size = {0, 0, 0}; z->gustAmount = std::nanf(""); z->direction = {0, 0, 0};
        Run(s, 3);
        const Vec3 w = WindField::At({0, 0, 0});
        CHECK(std::isfinite(w.x) && std::isfinite(w.y) && std::isfinite(w.z));
        z->mode = WindZone::Mode::Direction; z->size = {4, 4, 4}; z->strength = 5.0f;
        Run(s, 3);
        const Vec3 w2 = WindField::At({0, 0, 0});
        CHECK(std::isfinite(w2.x) && w2.Magnitude() < 1e-5f);   // zero direction: no wind
    }

    WindField::Publish({});
    TEST_MAIN_RESULT();
}
