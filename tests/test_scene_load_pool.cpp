#include "test_framework.hpp"
#include <Okay.hpp>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

using namespace okay;

namespace {

// Counts lifecycle calls; added by hand to pooled objects (the serializer does not know it).
struct Probe : public Behaviour {
    int awake = 0, enable = 0, disable = 0, start = 0;
    void Awake() override     { ++awake; }
    void OnEnable() override  { ++enable; }
    void OnDisable() override { ++disable; }
    void Start() override     { ++start; }
};

// Rule fixtures.
struct Body : public Behaviour {};
struct Gun  : public Behaviour {};
struct Turret : public Behaviour {};
struct Solo : public Behaviour { int n = 0; };
struct CycA : public Behaviour {};
struct CycB : public Behaviour {};

std::string TempPath(const char* name) { return std::string("test_slp_") + name + ".okayscene"; }

// A scene file: `n` root-and-child pairs and one camera last.
std::string WriteLevel(const char* name, int pairs, Vec3 at = Vec3{0, 0, 0}) {
    Scene sc("level");
    for (int i = 0; i < pairs; ++i) {
        GameObject* a = sc.CreateGameObject("Root" + std::to_string(i));
        a->transform->localPosition = at + Vec3{(float)i, 0, 0};
        a->AddComponent<SpriteRenderer>();
        GameObject* b = sc.CreateGameObject("Kid" + std::to_string(i));
        b->transform->SetParent(a->transform, false);
        b->transform->localPosition = Vec3{0, 1, 0};
        b->AddComponent<SpriteRenderer>();
    }
    GameObject* cam = sc.CreateGameObject("Cam");
    cam->AddComponent<Camera>();
    const std::string path = TempPath(name);
    SceneSerializer::SaveToFile(sc, path);
    return path;
}

} // namespace

int main() {
    RUN_SUITE("scene_load_pool");

    // ---- 1. Additive load over several frames, with progress ----------------------
    {
        const std::string path = WriteLevel("add", 10);   // 21 objects
        Scene host("host");
        GameObject* keep = host.CreateGameObject("Host");
        host.Update(0.016f);

        std::vector<float> seen; bool doneOk = false; int doneCalls = 0;
        Scene::AsyncLoadOptions o;
        o.additive = true; o.objectsPerFrame = 4; o.offset = Vec3{100, 0, 0};
        o.onProgress = [&](float p) { seen.push_back(p); };
        o.onDone = [&](bool ok) { doneOk = ok; ++doneCalls; };
        int h = host.BeginAsyncLoad(path, o);
        CHECK(h > 0);
        CHECK(host.IsLoading(h) && host.AnyLoading());
        CHECK(host.Objects().size() == 1);               // nothing happens until Update

        std::size_t last = host.Objects().size();
        int frames = 0;
        while (host.IsLoading(h) && frames < 100) {
            host.Update(0.016f); ++frames;
            std::size_t now = host.Objects().size();
            CHECK(now >= last && now - last <= 4);       // chunked
            last = now;
            if (host.IsLoading(h)) CHECK(host.mainCamera == nullptr);   // nothing Awake while half built
        }
        CHECK(frames >= 5 && frames <= 8);
        CHECK(host.IsLoaded(h) && !host.IsLoading(h) && !host.AnyLoading());
        CHECK(doneOk && doneCalls == 1);
        CHECK(host.Objects().size() == 22);
        bool mono = !seen.empty() && seen.back() == 1.0f;
        for (std::size_t i = 1; i < seen.size(); ++i) if (seen[i] < seen[i - 1]) mono = false;
        CHECK(mono);
        CHECK(seen.size() >= 4 && seen.front() < 1.0f);
        CHECK_NEAR(host.LoadProgress(h), 1.0f, 1e-6);

        // Started by the load, parents linked, the offset applied to roots only.
        host.Update(0.016f);
        CHECK(host.mainCamera != nullptr);
        GameObject* r3 = host.Find("Root3"); GameObject* k3 = host.Find("Kid3");
        CHECK(r3 && k3 && k3->transform->Parent() == r3->transform);
        CHECK_NEAR(r3->transform->localPosition.x, 103.0f, 1e-4);
        CHECK_NEAR(k3->transform->localPosition.x, 0.0f, 1e-4);

        // Unload by handle: the loaded content goes, the host's own stays.
        CHECK(host.UnloadAdditive(h));
        host.Update(0.016f);
        CHECK(host.Objects().size() == 1 && host.Objects()[0].get() == keep);
        CHECK(!host.IsLoaded(h) && !host.UnloadAdditive(h));
        std::remove(path.c_str());
    }

    // ---- 2. Two additive loads unload independently --------------------------------
    {
        const std::string pa = WriteLevel("a", 2), pb = WriteLevel("b", 3);
        Scene sc("host");
        Scene::AsyncLoadOptions o; o.additive = true; o.objectsPerFrame = 100;
        int a = sc.BeginAsyncLoad(pa, o), b = sc.BeginAsyncLoad(pb, o);
        CHECK(a > 0 && b > 0 && a != b);
        sc.Update(0.016f);
        CHECK(sc.IsLoaded(a) && sc.IsLoaded(b));
        CHECK(sc.Objects().size() == 5 + 7);
        sc.UnloadAdditive(a);
        sc.Update(0.016f);
        CHECK(sc.Objects().size() == 7);
        CHECK(sc.IsLoaded(b));
        std::remove(pa.c_str()); std::remove(pb.c_str());
    }

    // ---- 3. Replacing load keeps DontDestroyOnLoad objects -------------------------
    {
        const std::string path = WriteLevel("rep", 3);
        Scene sc("main");
        GameObject* old = sc.CreateGameObject("Old");
        GameObject* hero = sc.CreateGameObject("Hero");
        sc.DontDestroyOnLoad(hero);
        const auto heroId = hero->InstanceId();
        sc.Update(0.016f);
        Scene::AsyncLoadOptions o; o.objectsPerFrame = 2;
        bool ok = false; o.onDone = [&](bool v) { ok = v; };
        int h = sc.BeginAsyncLoad(path, o);
        int frames = 0;
        while (sc.IsLoading(h) && frames < 50) { sc.Update(0.016f); ++frames; }
        CHECK(ok && sc.IsLoaded(h));
        CHECK(sc.Find("Old") == nullptr);
        CHECK(sc.Find("Hero") == hero && hero->InstanceId() == heroId && sc.IsDontDestroyOnLoad(hero));
        CHECK(sc.Find("Root2") != nullptr);
        CHECK(sc.Objects().size() == 8);
        (void)old;
        std::remove(path.c_str());
    }

    // ---- 4. Failures and cancel ----------------------------------------------------
    {
        Scene sc("f");
        bool called = false, result = true;
        Scene::AsyncLoadOptions o; o.onDone = [&](bool v) { called = true; result = v; };
        CHECK(sc.BeginAsyncLoad("no_such_file.okayscene", o) == 0);
        CHECK(called && !result);

        const std::string bad = TempPath("bad");
        { std::ofstream f(bad); f << "okayscene 3\ngameobject 0 \"A\"\nend\ngameobject 1 \"B\"\n  bogusfield 1\nend\n"; }
        Scene::AsyncLoadOptions ob; ob.additive = true; ob.objectsPerFrame = 1;
        bool bres = true; ob.onDone = [&](bool v) { bres = v; };
        int h = sc.BeginAsyncLoad(bad, ob);
        CHECK(h > 0);
        for (int i = 0; i < 4; ++i) sc.Update(0.016f);
        CHECK(!bres && !sc.IsLoaded(h) && !sc.IsLoading(h));
        CHECK(sc.Objects().size() == 0);                 // what the failed load made is gone again
        std::remove(bad.c_str());

        const std::string path = WriteLevel("cancel", 5);
        Scene::AsyncLoadOptions oc; oc.additive = true; oc.objectsPerFrame = 2;
        bool cres = true; oc.onDone = [&](bool v) { cres = v; };
        int c = sc.BeginAsyncLoad(path, oc);
        sc.Update(0.016f); sc.Update(0.016f);
        CHECK(sc.IsLoading(c) && sc.Objects().size() == 4);
        CHECK(sc.UnloadAdditive(c));
        sc.Update(0.016f);
        CHECK(!cres && sc.Objects().size() == 0 && !sc.AnyLoading());

        // Clear() abandons loads in flight.
        int d = sc.BeginAsyncLoad(path, oc);
        sc.Update(0.016f);
        sc.Clear();
        CHECK(!sc.IsLoading(d) && sc.Objects().empty());
        sc.Update(0.016f);
        CHECK(sc.Objects().empty());
        std::remove(path.c_str());
    }

    // ---- 5. SceneManager wrappers ---------------------------------------------------
    {
        const std::string path = WriteLevel("mgr", 2);
        SceneManager::ClearScenes();
        int idx = SceneManager::AddScene(path);
        Scene sc("m");
        CHECK(SceneManager::LoadSceneAsync(sc, 99) == 0);
        CHECK(SceneManager::LoadSceneAsyncByName(sc, "nope") == 0);
        Scene::AsyncLoadOptions o; o.objectsPerFrame = 3;
        int h = SceneManager::LoadSceneAsyncByName(sc, TempPath("mgr"), o);
        CHECK(h > 0 && SceneManager::IsLoading(sc, h));
        CHECK(SceneManager::ActiveIndex() == idx);
        for (int i = 0; i < 10 && SceneManager::IsLoading(sc, h); ++i) sc.Update(0.016f);
        CHECK(SceneManager::IsLoaded(sc, h) && SceneManager::LoadProgress(sc, h) == 1.0f);
        CHECK(sc.Objects().size() == 5);
        CHECK(SceneManager::UnloadScene(sc, h));
        sc.Update(0.016f);
        CHECK(sc.Objects().empty());
        // The synchronous path still works.
        CHECK(SceneManager::LoadScene(sc, idx));
        sc.Update(0.016f);
        CHECK(sc.Objects().size() == 5);
        SceneManager::ClearScenes();
        std::remove(path.c_str());
    }

    // ---- 6. Object pool -------------------------------------------------------------
    {
        Scene sc("pool");
        GameObject* tpl = sc.CreateGameObject("Bullet");
        tpl->AddComponent<SpriteRenderer>();
        tpl->transform->localScale = Vec3{2, 2, 2};
        ObjectPool pool(sc);
        CHECK(pool.Get() == nullptr);                    // no source yet
        pool.SetTemplate(*tpl);
        sc.Destroy(tpl); sc.Update(0.016f);              // the template may go

        pool.maxSize = 3;
        pool.Prewarm(10);
        CHECK(pool.IdleCount() == 3 && pool.ActiveCount() == 0 && pool.CreatedCount() == 3);
        sc.Update(0.016f);

        GameObject* a = pool.Get(Vec3{5, 6, 7});
        CHECK(a && a->active && pool.CreatedCount() == 3 && pool.ReuseCount() == 1);
        CHECK(a->transform->localPosition.x == 5.0f && a->transform->localScale.x == 2.0f);
        CHECK(pool.Owns(a) && pool.ActiveCount() == 1 && pool.IdleCount() == 2);
        auto* pr = a->AddComponent<Probe>();
        sc.Update(0.016f);
        CHECK(pr->awake == 1 && pr->enable == 1 && pr->start == 1 && pr->disable == 0);

        // Released: off now, OnDisable at the start of the next frame.
        a->transform->localPosition = Vec3{50, 0, 0};
        CHECK(pool.Release(a));
        CHECK(!a->active && pool.IdleCount() == 3);
        CHECK(pool.Release(a));                          // twice is harmless
        CHECK(pool.IdleCount() == 3);
        // Same frame: the reset has not run, so it is not handed out again.
        GameObject* same = pool.Get();
        CHECK(same != a);
        CHECK(pool.Release(same));
        sc.Update(0.016f);
        CHECK(pr->disable == 1);
        GameObject* b = pool.Get(Vec3{1, 1, 1});
        CHECK(b == a || b == same || pool.ReuseCount() >= 2);
        // Find the reuse of `a` explicitly.
        pool.Release(b); sc.Update(0.016f);
        GameObject* got = nullptr;
        std::vector<GameObject*> held;
        for (int i = 0; i < 4; ++i) { GameObject* g = pool.Get(); held.push_back(g); if (g == a) got = g; }
        CHECK(got == a);
        CHECK(a->transform->localPosition.x == 0.0f);    // back to the template pose
        sc.Update(0.016f);
        CHECK(pr->enable == 2 && pr->start == 1 && pr->awake == 1 && pr->disable == 1);

        // Releasing more than maxSize destroys the extras.
        for (GameObject* g : held) pool.Release(g);
        CHECK(pool.IdleCount() <= 3);
        sc.Update(0.016f);
        CHECK(pool.IdleCount() <= 3);
        pool.Clear();
        sc.Update(0.016f);
        CHECK(pool.IdleCount() == 0);

        // Not ours / destroyed behind our back.
        GameObject* stranger = sc.CreateGameObject("Other");
        CHECK(!pool.Release(stranger) && !pool.Release(nullptr));
        GameObject* c = pool.Get();
        sc.Destroy(c); sc.Update(0.016f);
        CHECK(pool.ActiveCount() == 0 && !pool.Release(c));

        // A scene that is cleared under the pool.
        GameObject* d = pool.Get();
        (void)d;
        sc.Clear();
        CHECK(pool.IdleCount() == 0 && pool.ActiveCount() == 0);
        CHECK(pool.Get() != nullptr);                    // it just makes new ones
    }
    {
        // Prefab file source.
        Scene mk("mk");
        GameObject* p = mk.CreateGameObject("Spark");
        p->AddComponent<SpriteRenderer>();
        SceneSerializer::SaveObjectToFile(*p, "test_slp_spark.okayprefab");
        Scene sc("pool2");
        ObjectPool pool(sc);
        CHECK(!pool.SetPrefabFile("test_slp_missing.okayprefab"));
        CHECK(pool.SetPrefabFile("test_slp_spark.okayprefab"));
        GameObject* s = pool.Get();
        CHECK(s && s->name == "Spark" && s->prefabSource == "test_slp_spark.okayprefab");
        std::remove("test_slp_spark.okayprefab");
    }

    // ---- 7. RequireComponent / DisallowMultipleComponent ----------------------------
    {
        ComponentRules::Clear();
        {   // no rules: nothing changes
            Scene sc("r0");
            GameObject* g = sc.CreateGameObject("G");
            g->AddComponent<Gun>(); g->AddComponent<Gun>();
            CHECK(g->GetComponents<Gun>().size() == 2 && g->GetComponent<Body>() == nullptr);
        }
        RequireComponent<Gun, Body>();
        RequireComponent<Turret, Gun>();
        DisallowMultipleComponent<Solo>();
        RequireComponent<CycA, CycB>();
        RequireComponent<CycB, CycA>();
        RequireComponent<Gun, Body>();                   // declaring twice does nothing

        Scene sc("r1");
        GameObject* g = sc.CreateGameObject("G");
        auto* gun = g->AddComponent<Gun>();
        CHECK(g->GetComponent<Body>() != nullptr && g->GetComponents<Body>().size() == 1);
        // The required one comes first in the stored order (Transform, Body, Gun).
        CHECK(g->Components().size() == 3 && g->Components()[1].get() == (Component*)g->GetComponent<Body>());
        // Already there: not added again.
        GameObject* g2 = sc.CreateGameObject("G2");
        auto* body = g2->AddComponent<Body>();
        g2->AddComponent<Gun>();
        CHECK(g2->GetComponents<Body>().size() == 1 && g2->GetComponent<Body>() == body);
        // Chains.
        GameObject* g3 = sc.CreateGameObject("G3");
        g3->AddComponent<Turret>();
        CHECK(g3->GetComponent<Gun>() && g3->GetComponent<Body>());
        // Unique.
        GameObject* g4 = sc.CreateGameObject("G4");
        auto* s1 = g4->AddComponent<Solo>(); s1->n = 5;
        auto* s2 = g4->AddComponent<Solo>();
        CHECK(s1 == s2 && g4->GetComponents<Solo>().size() == 1 && s2->n == 5);
        // A loop of requirements ends.
        GameObject* g5 = sc.CreateGameObject("G5");
        g5->AddComponent<CycA>();
        CHECK(g5->GetComponents<CycA>().size() == 1 && g5->GetComponents<CycB>().size() == 1);
        // Removal check for editors.
        CHECK(g->RequiredBy(g->GetComponent<Body>()) == gun);
        CHECK(g->RequiredBy(gun) == nullptr);
        CHECK(g->RemoveComponent(gun));
        CHECK(g->RequiredBy(g->GetComponent<Body>()) == nullptr);
        // Lifecycle still runs for the added ones.
        sc.Update(0.016f);
        // A serialized scene with a rule type goes through the rules too.
        ComponentRules::Clear();
        CHECK(!ComponentRules::Any() && !ComponentRules::IsUnique(typeid(Solo)));
    }

    TEST_MAIN_RESULT();
}
