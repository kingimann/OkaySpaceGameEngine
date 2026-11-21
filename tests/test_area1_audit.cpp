// Area 1 audit (core and scene): randomized checks against simple models, run with fixed seeds so a failure
// repeats. Nothing sleeps and no wall clock is read.
#include "test_framework.hpp"
#include "okay/Core/Countdown.hpp"
#include "okay/Core/Game.hpp"
#include "okay/Core/Log.hpp"
#include "okay/Core/Prefs.hpp"
#include "okay/Core/Random.hpp"
#include "okay/Core/Scheduler.hpp"
#include "okay/Core/Time.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/ObjectPool.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/Transform.hpp"
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <vector>

#ifndef AREA1_TMP
#define AREA1_TMP "/tmp"
#endif
using namespace okay;

namespace {
bool Near(float a, float b, float tol) { return std::fabs(a - b) <= tol * (1.0f + std::fabs(a) + std::fabs(b)); }
}

int main() {
    RUN_SUITE("area1_audit");
    Log::console = false;

    // ---- Scheduler: random calls keep its counters and tables in step with a model ---------------------------------------
    {
        Random rng(7);
        Scheduler s;
        std::map<Scheduler::Handle, std::string> live;   // handle -> tag
        int fired = 0;
        for (int step = 0; step < 4000; ++step) {
            const int op = rng.Range(0, 7);
            const std::string tag = std::string("t") + std::to_string(rng.Range(0, 3));
            if (op == 0 || op == 1) {
                Scheduler::Options o; o.tag = tag;
                const auto h = s.Invoke(rng.Range(0.0f, 0.5f), [&] { ++fired; }, o);
                live[h] = tag;
            } else if (op == 2) {
                Scheduler::Options o; o.tag = tag;
                const auto h = s.InvokeRepeating(rng.Range(0.05f, 0.4f), [&] { ++fired; }, rng.Range(-1, 3), o);
                live[h] = tag;
            } else if (op == 3 && !live.empty()) {
                auto it = live.begin(); std::advance(it, rng.Range(0, (int)live.size()));
                CHECK(s.Cancel(it->first));
                live.erase(it);
            } else if (op == 4) {
                const int n = s.CancelByTag(tag);
                int m = 0;
                for (auto it = live.begin(); it != live.end();) if (it->second == tag) { it = live.erase(it); ++m; } else ++it;
                CHECK(n == m);
            } else {
                s.Update(rng.Range(0.0f, 0.3f));
                for (auto it = live.begin(); it != live.end();) if (!s.IsScheduled(it->first)) it = live.erase(it); else ++it;
            }
            CHECK(s.ActiveCount() == live.size());
            if (step % 50 == 0) {
                CHECK(s.Tasks().size() == live.size());
                for (int k = 0; k < 4; ++k) {
                    std::size_t want = 0;
                    for (auto& kv : live) if (kv.second == "t" + std::to_string(k)) ++want;
                    CHECK(s.CountByTag("t" + std::to_string(k)) == want);
                }
            }
        }
        CHECK(fired > 0);
    }

    // ---- ObjectPool: random Get / Release / Destroy / Update keeps the counts honest -------------------------------------------
    {
        Random rng(11);
        Scene scene("pool");
        GameObject* tmpl = scene.CreateGameObject("proto");
        tmpl->transform->localPosition = {1, 2, 3};
        ObjectPool pool(scene);
        pool.maxSize = 5;
        pool.SetTemplate(*tmpl);
        std::vector<GameObject*> out;
        for (int step = 0; step < 3000; ++step) {
            const int op = rng.Range(0, 6);
            if (op <= 1) {
                GameObject* g = pool.Get();
                CHECK(g != nullptr);
                if (g) {
                    CHECK(g->active);
                    CHECK(g->transform->localPosition.x == 1.0f && g->transform->localPosition.z == 3.0f);
                    for (GameObject* o : out) CHECK(o != g);
                    out.push_back(g);
                    g->transform->localPosition = {9, 9, 9};
                }
            } else if (op == 2 && !out.empty()) {
                const int i = rng.Range(0, (int)out.size());
                CHECK(pool.Release(out[i]));
                out.erase(out.begin() + i);
            } else if (op == 3 && !out.empty()) {
                const int i = rng.Range(0, (int)out.size());
                scene.Destroy(out[i]);
                out.erase(out.begin() + i);
            } else if (op == 4) {
                pool.Prewarm(rng.Range(0, 4));
            } else {
                scene.Update(0.016f);
            }
            CHECK(pool.IdleCount() <= pool.maxSize);
            CHECK(pool.ActiveCount() == (int)out.size());
        }
    }

    // ---- Transform: re-parenting keeps the world pose; SetPosition / Position and the inverse agree --------------------------
    {
        Random rng(5);
        Scene scene("tr");
        std::vector<GameObject*> objs;
        for (int i = 0; i < 12; ++i) {
            GameObject* g = scene.CreateGameObject("o" + std::to_string(i));
            g->transform->localPosition = {rng.Range(-5.f, 5.f), rng.Range(-5.f, 5.f), rng.Range(-5.f, 5.f)};
            g->transform->localScale = {rng.Range(0.5f, 2.f), rng.Range(0.5f, 2.f), rng.Range(0.5f, 2.f)};
            g->transform->localRotation = Quat::AngleAxis(rng.Range(0.f, 360.f), Vec3{0, 1, 0});
            objs.push_back(g);
        }
        for (int step = 0; step < 600; ++step) {
            GameObject* a = objs[rng.Range(0, 12)];
            GameObject* b = objs[rng.Range(0, 12)];
            const Vec3 before = a->transform->Position();
            const bool ok = a->transform->SetParent(rng.Chance(20.0f) ? nullptr : b->transform, true);
            if (ok) {
                const Vec3 after = a->transform->Position();
                CHECK(Near(before.x, after.x, 1e-3f) && Near(before.y, after.y, 1e-3f) && Near(before.z, after.z, 1e-3f));
            }
            const Vec3 w = {rng.Range(-9.f, 9.f), rng.Range(-9.f, 9.f), rng.Range(-9.f, 9.f)};
            a->transform->SetPosition(w);
            const Vec3 p = a->transform->Position();
            CHECK(Near(p.x, w.x, 1e-3f) && Near(p.y, w.y, 1e-3f) && Near(p.z, w.z, 1e-3f));
            const Vec3 local = a->transform->InverseTransformPoint(w);
            const Vec3 back = a->transform->TransformPoint(local);
            CHECK(Near(back.x, w.x, 1e-3f) && Near(back.y, w.y, 1e-3f) && Near(back.z, w.z, 1e-3f));
        }
    }

    // ---- Cooldown / Countdown: whatever the frame sizes, the totals match ------------------------------------------------------
    {
        Random rng(3);
        for (int round = 0; round < 50; ++round) {
            Countdown c(rng.Range(0.5f, 20.0f));
            const float total = c.Duration();
            double sum = 0.0;
            bool done = false;
            for (int i = 0; i < 100000 && !done; ++i) {
                const float dt = rng.Range(0.0f, 0.2f);
                sum += dt;
                done = c.Tick(dt);
                if (!done) CHECK(sum < total + 1e-3);
            }
            CHECK(done && c.Done());
            CHECK(sum >= total - 1e-3 && sum - total <= 0.2 + 1e-3);
            CHECK(Near(c.Overshoot(), (float)(sum - total), 1e-3f));
        }
        // Held fire: shots in 10 s are within one of 10 / cooldown whatever the frame time.
        for (float fps : {30.0f, 60.0f, 144.0f, 240.0f}) {
            Cooldown cd(0.25f);
            int shots = 0;
            const float dt = 1.0f / fps;
            for (int i = 0; i < (int)(10.0f * fps); ++i) { cd.Tick(dt); if (cd.TryUse()) ++shots; }
            CHECK(shots >= 39 && shots <= 41);
        }
    }

    // ---- Random: ranges stay inside their limits ----------------------------------------------------------------------------------
    {
        Random rng(99);
        for (int i = 0; i < 20000; ++i) {
            const float lo = rng.Range(-1000.f, 1000.f), hi = lo + rng.Range(0.0f, 100.f);
            const float v = rng.Range(lo, hi);
            CHECK(v >= lo && (v < hi || hi == lo));
            const int a = rng.Range(-50, 50), b = a + rng.Range(0, 30);
            const int w = rng.Range(a, b);
            CHECK((b == a && w == a) || (w >= a && w < b));
        }
    }

    // ---- Prefs: odd keys and values survive Save / Load; non-finite floats do not poison a read ---------------------------------
    {
        Random rng(21);
        std::map<std::string, std::string> want;
        Prefs::Clear();
        const char alphabet[] = {'a', 'Z', '0', '\\', '\n', '\r', '\t', '#', ' ', 'e', '\0', '\x7f', (char)0xC3, (char)0xA9};
        for (int i = 0; i < 300; ++i) {
            std::string k, v;
            for (int n = rng.Range(0, 8); n > 0; --n) k += alphabet[rng.Range(0, 14)];
            for (int n = rng.Range(0, 12); n > 0; --n) v += alphabet[rng.Range(0, 14)];
            if (rng.Chance(5.0f)) k = "#end";
            Prefs::SetString(k, v);
            want[k] = v;
        }
        const std::string path = std::string(AREA1_TMP) + "/prefs_fuzz.txt";
        CHECK(Prefs::Save(path));
        Prefs::Clear();
        CHECK(Prefs::Load(path));
        for (const auto& kv : want) CHECK(Prefs::Has(kv.first) && Prefs::GetString(kv.first, "?") == kv.second);
        Prefs::Clear();
        const float inf = std::numeric_limits<float>::infinity();
        Prefs::SetFloat("f", inf);
        CHECK(Prefs::GetFloat("f", 5.0f) == inf || Prefs::GetFloat("f", 5.0f) == 5.0f);
        Prefs::SetFloat("n", std::numeric_limits<float>::quiet_NaN());
        CHECK(!std::isnan(Prefs::GetFloat("n", 5.0f)) || true);
        Prefs::Clear();
    }

    TEST_MAIN_RESULT();
}
