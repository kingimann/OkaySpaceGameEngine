// Object pooling audit (1.13): idle objects stay out of a saved scene, Shrink, Scene::Despawn (Lifetime and
// Projectile3D give objects back), the reset of scripts, trails, particles and projectiles, the Spawner's
// `usePool`, the script calls pool_get / pool_release / pool_prewarm / pool_idle, and the pools scripts share.
#include "test_framework.hpp"
#include <Okay.hpp>
#include <cstdio>
#include <string>
#include <vector>

using namespace okay;

namespace {

int CountObjects(const std::string& text) {
    int n = 0;
    for (std::size_t at = 0; (at = text.find("\ngameobject ", at)) != std::string::npos; ++at) ++n;
    return n;
}
void Pump(Scene& s, int frames, float dt = 0.1f) { for (int i = 0; i < frames; ++i) s.Update(dt); }

// A prefab file in the working folder; removed by the caller.
std::string MakePrefab(const char* file, const std::function<void(GameObject&)>& fill) {
    Scene tmp("p");
    GameObject* b = tmp.CreateGameObject("Bullet");
    fill(*b);
    SceneSerializer::SaveObjectToFile(*b, file);
    return file;
}

void TestSavedScene() {
    RUN_SUITE("idle pooled objects are not part of a saved scene");
    Scene sc("pool");
    GameObject* tpl = sc.CreateGameObject("Bullet");
    tpl->AddComponent<SpriteRenderer>();
    GameObject* kid = sc.CreateGameObject("Fin");
    kid->transform->SetParent(tpl->transform, false);
    kid->AddComponent<SpriteRenderer>();
    ObjectPool pool(sc);
    pool.SetTemplate(*tpl);
    sc.Destroy(tpl);
    sc.Update(0.016f);
    CHECK(CountObjects(SceneSerializer::Serialize(sc)) == 0);
    pool.Prewarm(3);
    CHECK(pool.IdleCount() == 3 && sc.Objects().size() == 6);
    CHECK(CountObjects(SceneSerializer::Serialize(sc)) == 0);   // three spares with a child each: none written
    GameObject* a = pool.Get();
    CHECK(a && !a->transient);
    std::string text = SceneSerializer::Serialize(sc);
    CHECK(CountObjects(text) == 2);                              // the one in use and its child
    // A scene saved while a bullet is in flight loads it as an ordinary object.
    Scene loaded("loaded");
    CHECK(SceneSerializer::Deserialize(loaded, text) && loaded.Objects().size() == 2);
    // Back in the pool: out of the text again.
    sc.Update(0.016f);
    CHECK(pool.Release(a) && a->transient);
    CHECK(CountObjects(SceneSerializer::Serialize(sc)) == 0);
    // Handed out again: written again.
    sc.Update(0.016f);
    GameObject* b = pool.Get();
    CHECK(b && !b->transient && CountObjects(SceneSerializer::Serialize(sc)) == 2);
}

void TestShrink() {
    RUN_SUITE("Shrink gives idle objects back, the longest idle first");
    Scene sc("pool");
    GameObject* tpl = sc.CreateGameObject("Bullet");
    tpl->AddComponent<SpriteRenderer>();
    ObjectPool pool(sc);
    pool.SetTemplate(*tpl);
    sc.Destroy(tpl);
    sc.Update(0.016f);
    pool.Prewarm(10);
    CHECK(pool.IdleCount() == 10);
    CHECK(pool.Shrink(4) == 6);
    CHECK(pool.IdleCount() == 4);
    sc.Update(0.016f);
    CHECK(sc.Objects().size() == 4);                 // the six are really gone
    CHECK(pool.Shrink(4) == 0 && pool.Shrink(100) == 0);
    CHECK(pool.IdleCount() == 4);

    // Which ones stay: the most recently released.
    std::vector<GameObject*> out;
    for (int i = 0; i < 4; ++i) out.push_back(pool.Get());
    sc.Update(0.016f);
    for (GameObject* g : out) { pool.Release(g); sc.Update(0.016f); }   // out[0] released first, out[3] last
    CHECK(pool.Shrink(1) == 3);
    sc.Update(0.016f);
    GameObject* kept = pool.Get();
    CHECK(kept == out[3]);
    CHECK(out[0]->IsDestroyed() && out[1]->IsDestroyed() && out[2]->IsDestroyed() && !out[3]->IsDestroyed());
    CHECK(pool.Shrink(0) == 0);                       // nothing idle now
    pool.Release(kept);
    CHECK(pool.Shrink(-5) == 1 && pool.IdleCount() == 0);   // a negative limit means none
    // maxSize lowered later does not shrink by itself.
    pool.maxSize = 8;
    pool.Prewarm(8);
    pool.maxSize = 2;
    CHECK(pool.IdleCount() == 8);
    pool.Shrink(pool.maxSize);
    CHECK(pool.IdleCount() == 2);
}

void TestDespawnLifetime() {
    RUN_SUITE("Scene::Despawn: Lifetime gives a pooled object back instead of destroying it");
    Scene sc("pool");
    GameObject* tpl = sc.CreateGameObject("Spark");
    tpl->AddComponent<SpriteRenderer>();
    tpl->AddComponent<Lifetime>()->seconds = 0.35f;
    ObjectPool pool(sc);
    pool.SetTemplate(*tpl);
    sc.Destroy(tpl);
    sc.Update(0.016f);

    GameObject* a = pool.Get();
    CHECK(a && a->active);
    Pump(sc, 3);                                      // 0.3 s: still alive
    CHECK(a->active && pool.IdleCount() == 0);
    Pump(sc, 2);                                      // past 0.35 s: Lifetime despawns it
    CHECK(!a->IsDestroyed() && !a->active && pool.IdleCount() == 1 && pool.ActiveCount() == 0);
    Pump(sc, 2);
    GameObject* b = pool.Get();
    CHECK(b == a && pool.CreatedCount() == 1 && pool.ReuseCount() == 1);
    Pump(sc, 3);
    CHECK(b->active);                                 // the full life again, not what was left of the last one
    Pump(sc, 2);
    CHECK(!b->active && pool.IdleCount() == 1);

    // An object that is not pooled is destroyed, as it always was.
    GameObject* plain = sc.CreateGameObject("Plain");
    plain->AddComponent<Lifetime>()->seconds = 0.15f;
    Pump(sc, 3);
    CHECK(plain->IsDestroyed());
    GameObject* other = sc.CreateGameObject("Other");
    CHECK(!sc.Despawn(other));
    sc.Update(0.016f);
    CHECK(other->IsDestroyed());
    CHECK(!sc.Despawn(nullptr));
    // Despawn of an object already idle is harmless.
    CHECK(sc.Despawn(a) && pool.IdleCount() == 1);
}

void TestResetOfComponents() {
    RUN_SUITE("a reused object starts over: projectile, trail, particles, animation");
    Scene sc("pool");
    GameObject* tpl = sc.CreateGameObject("Shot");
    auto* pj = tpl->AddComponent<Projectile3D>();
    pj->velocity = {0, 0, 10};
    pj->lifetime = 0.45f;
    auto* trail = tpl->AddComponent<TrailRenderer>();
    (void)trail;
    auto* ps = tpl->AddComponent<ParticleSystem>();
    ps->playing = true;
    auto* an = tpl->AddComponent<SpriteAnimator>();
    an->frames = {"a.png", "b.png", "c.png"};
    an->fps = 10.0f;
    ObjectPool pool(sc);
    pool.SetTemplate(*tpl);
    sc.Destroy(tpl);
    sc.Update(0.016f);

    GameObject* a = pool.Get();
    CHECK(a != nullptr);
    if (!a) return;
    auto* ap = a->GetComponent<Projectile3D>();
    auto* at = a->GetComponent<TrailRenderer>();
    auto* aps = a->GetComponent<ParticleSystem>();
    auto* aa = a->GetComponent<SpriteAnimator>();
    Pump(sc, 2, 0.1f);
    ap->velocity = {5, 6, 7};                         // changed in flight (gravity, a ricochet)
    for (int i = 0; i < 3; ++i) { a->transform->localPosition = a->transform->localPosition + Vec3{1, 0, 0}; sc.Update(0.1f); }
    CHECK(at->PointCount() > 0);
    CHECK(aa->CurrentFrame() > 0);
    aps->Stop();
    Pump(sc, 2, 0.1f);                                // the projectile ends (lifetime 0.45 s) and goes back
    CHECK(!ap->Alive() && !a->active && pool.IdleCount() == 1);
    Pump(sc, 2);
    GameObject* b = pool.Get();
    CHECK(b == a);
    CHECK(ap->Alive() && ap->Hits() == 0);
    CHECK(ap->velocity.x == 0.0f && ap->velocity.y == 0.0f && ap->velocity.z == 10.0f);
    CHECK(at->PointCount() == 0);                     // no streak from where the last use ended
    CHECK(aa->CurrentFrame() == 0);
    CHECK(aps->playing);                              // it played when it was made
    Pump(sc, 3, 0.1f);
    CHECK(b->active && ap->Alive());                  // a whole life again
}

void TestSpawnerUsePool() {
    RUN_SUITE("Spawner usePool");
    const std::string prefab = MakePrefab("test_pool_audit_spark.okayprefab", [](GameObject& g) {
        g.AddComponent<SpriteRenderer>();
        g.AddComponent<Lifetime>()->seconds = 0.35f;
    });
    {
        Scene sc("s");
        GameObject* host = sc.CreateGameObject("Spawner");
        host->transform->localPosition = {10, 0, 0};
        auto* sp = host->AddComponent<Spawner>();
        sp->prefabPath = prefab;
        sp->usePool = true;
        sp->autoStart = false;
        sp->spawnRadius = 0.0f;
        sp->maxAlive = 0;
        sc.Update(0.016f);
        CHECK(sp->Pool() == nullptr);                 // nothing spawned yet
        GameObject* a = sp->SpawnOne();
        CHECK(a && a->active && a->tag == "spawn:Spawner" && sp->Pool() && sp->Pool()->CreatedCount() == 1);
        CHECK(std::fabs(a->transform->Position().x - 10.0f) < 1e-3f);
        CHECK(sp->AliveCount() == 1);
        Pump(sc, 5);                                  // Lifetime ends it: back into the pool
        CHECK(!a->active && sp->AliveCount() == 0 && sp->Pool()->IdleCount() == 1);
        Pump(sc, 2);
        GameObject* b = sp->SpawnOne();
        CHECK(b == a && sp->Pool()->CreatedCount() == 1 && sp->Pool()->ReuseCount() == 1);
        CHECK(b->active && b->tag == "spawn:Spawner" && sp->AliveCount() == 1 && sp->Spawned() == 2);
        // A second spawn while the first lives makes another one.
        GameObject* c = sp->SpawnOne();
        CHECK(c && c != b && sp->Pool()->CreatedCount() == 2);
    }
    {   // without usePool nothing changes: a new object each time, no pool
        Scene sc("s");
        GameObject* host = sc.CreateGameObject("Spawner");
        auto* sp = host->AddComponent<Spawner>();
        sp->prefabPath = prefab;
        sp->autoStart = false;
        GameObject* a = sp->SpawnOne();
        CHECK(a && sp->Pool() == nullptr);
    }
    {   // a template object in the scene is the source
        Scene sc("s");
        GameObject* tpl = sc.CreateGameObject("Template");
        tpl->AddComponent<SpriteRenderer>();
        tpl->AddComponent<Lifetime>()->seconds = 0.25f;
        GameObject* host = sc.CreateGameObject("Spawner");
        auto* sp = host->AddComponent<Spawner>();
        sp->templateName = "Template";
        sp->usePool = true;
        sp->autoStart = false;
        sc.Update(0.016f);
        GameObject* a = sp->SpawnOne();
        CHECK(a && a != tpl && sp->Pool()->CreatedCount() == 1);
        Pump(sc, 4);
        Pump(sc, 2);
        CHECK(!a->active);
        GameObject* b = sp->SpawnOne();
        CHECK(b == a);
    }
    {   // saved with the scene; a record without the field loads with it off
        Scene a("a");
        GameObject* host = a.CreateGameObject("Spawner");
        host->AddComponent<Spawner>()->usePool = true;
        host->AddComponent<Spawner>()->interval = 9.0f;
        const std::string text = SceneSerializer::Serialize(a);
        Scene b("b");
        CHECK(SceneSerializer::Deserialize(b, text));
        auto sp = b.Objects()[0]->GetComponents<Spawner>();
        CHECK(sp.size() == 2 && sp[0]->usePool && !sp[1]->usePool && sp[1]->interval == 9.0f);
        Scene c("c");
        CHECK(SceneSerializer::Deserialize(c, "okayscene 3\ngameobject 0 \"O\"\n  active 1\n  transform 0 0 0 0 0 0 1 1 1 1\n  spawner \"\" 3 5 0 6 1 1 1 0 0 1 \"a.okayprefab\"\n  parent -1\nend\n"));
        CHECK(c.Objects()[0]->GetComponent<Spawner>() && !c.Objects()[0]->GetComponent<Spawner>()->usePool);
    }
    std::remove(prefab.c_str());
}

void TestScriptCalls() {
    RUN_SUITE("script calls: pool_get, pool_release, pool_prewarm, pool_idle");
    // The bullet's own script ends it with pool_release() after a quarter second.
    const std::string prefab = MakePrefab("test_pool_audit_bullet.okayprefab", [](GameObject& g) {
        g.AddComponent<SpriteRenderer>();
        auto* s = g.AddComponent<ScriptComponent>("okayscript");
        s->LoadSource("var t = 0;\nfunction update(d) { t = t + d; if (t >= 0.25) { pool_release(); } }\n");
    });
    Scene sc("s");
    GameObject* gun = sc.CreateGameObject("Gun");
    auto* gs = gun->AddComponent<ScriptComponent>("okayscript");
    const std::string src =
        "var fired = 0;\n"
        "function update(d) { }\n"
        "function fire() { pool_get(\"" + prefab + "\", 1, 2, 3); fired = fired + 1; }\n"
        "function idle() { set(\"idle\", pool_idle(\"" + prefab + "\")); }\n"
        "function warm() { set(\"warm\", pool_prewarm(\"" + prefab + "\", 3)); }\n";
    std::string err;
    CHECK(gs->LoadSource(src, &err));
    sc.Start();
    sc.Update(0.016f);

    CHECK(gs->VM() != nullptr);
    if (!gs->VM()) return;
    gs->VM()->CallEvent("warm");
    CHECK(gs->Host().globals["warm"].AsFloat() == 3.0f);
    ObjectPool* pool = ObjectPool::ForPrefab(sc, prefab);
    CHECK(pool != nullptr && pool == ObjectPool::ForPrefab(sc, prefab) && pool->IdleCount() == 3);
    CHECK(sc.Objects().size() == 4);                  // the gun and three spares
    sc.Update(0.016f);
    gs->VM()->CallEvent("fire");
    CHECK(pool->IdleCount() == 2 && pool->ActiveCount() == 1 && pool->CreatedCount() == 3);
    GameObject* b = nullptr;
    for (const auto& o : sc.Objects()) if (o->name == "Bullet" && o->active) b = o.get();
    CHECK(b != nullptr);
    if (!b) return;
    CHECK(std::fabs(b->transform->localPosition.x - 1.0f) < 1e-4f && std::fabs(b->transform->localPosition.y - 2.0f) < 1e-4f &&
          std::fabs(b->transform->localPosition.z - 3.0f) < 1e-4f);
    Pump(sc, 2, 0.1f);
    CHECK(b->active);
    Pump(sc, 3, 0.1f);                                // the bullet's script released it
    CHECK(!b->active && pool->IdleCount() == 3 && !b->IsDestroyed());
    Pump(sc, 2, 0.1f);
    gs->VM()->CallEvent("idle");
    CHECK(gs->Host().globals["idle"].AsFloat() == 3.0f);

    // Reused: the bullet's script starts from the top, so it lives its quarter second again.
    gs->VM()->CallEvent("fire");
    CHECK(pool->ReuseCount() >= 1 && pool->CreatedCount() == 3);
    Pump(sc, 2, 0.1f);
    CHECK(pool->ActiveCount() == 1);                  // not released at once by the time the last use had run up
    Pump(sc, 3, 0.1f);
    CHECK(pool->ActiveCount() == 0);

    // pool_release from an object that did not come from a pool destroys it.
    GameObject* loose = sc.CreateGameObject("Loose");
    auto* ls = loose->AddComponent<ScriptComponent>("okayscript");
    CHECK(ls->LoadSource("function update(d) { pool_release(); }"));
    sc.Update(0.016f);
    sc.Update(0.016f);
    CHECK(sc.Find("Loose") == nullptr);
    // A prefab that is not there: pool_get says false and nothing is made.
    GameObject* g2 = sc.CreateGameObject("G2");
    auto* s2 = g2->AddComponent<ScriptComponent>("okayscript");
    CHECK(s2->LoadSource("function go() { set(\"r\", pool_get(\"missing_prefab.okayprefab\", 0, 0)); }"));
    sc.Start();
    sc.Update(0.016f);
    CHECK(s2->VM() != nullptr);
    if (s2->VM()) {
        s2->VM()->CallEvent("go");
        CHECK(!s2->Host().globals["r"].AsBool());
    }
    std::remove(prefab.c_str());
}

void TestSharedPools() {
    RUN_SUITE("the pool a scene keeps for a prefab file");
    const std::string prefab = MakePrefab("test_pool_audit_shared.okayprefab", [](GameObject& g) { g.AddComponent<SpriteRenderer>(); });
    ObjectPool* first = nullptr;
    {
        Scene a("a");
        first = ObjectPool::ForPrefab(a, prefab);
        CHECK(first != nullptr && first == ObjectPool::ForPrefab(a, prefab));
        CHECK(ObjectPool::ForPrefab(a, "no_such_prefab.okayprefab") == nullptr);
        CHECK(ObjectPool::ForPrefab(a, "") == nullptr);
        Scene b("b");
        ObjectPool* other = ObjectPool::ForPrefab(b, prefab);
        CHECK(other != nullptr && other != first);   // one pool per scene
        GameObject* g = first->Get();
        CHECK(g != nullptr && first->Owns(g) && !other->Owns(g));
        // The pool survives the scene being cleared and loaded again: it just makes new objects.
        a.Clear();
        CHECK(first->ActiveCount() == 0 && first->Get() != nullptr);
        // Despawn finds the pool of its own scene only.
        GameObject* loose = b.CreateGameObject("Loose");
        CHECK(!b.Despawn(loose));
    }
    // Both scenes are gone with their pools; a new scene gets a new pool.
    Scene c("c");
    ObjectPool* again = ObjectPool::ForPrefab(c, prefab);
    CHECK(again != nullptr && again->CreatedCount() == 0);
    std::remove(prefab.c_str());
}

void TestEdges() {
    RUN_SUITE("edges: released twice, destroyed elsewhere, foreign objects");
    Scene sc("pool");
    GameObject* tpl = sc.CreateGameObject("Bullet");
    tpl->AddComponent<SpriteRenderer>();
    ObjectPool pool(sc);
    pool.SetTemplate(*tpl);
    sc.Destroy(tpl);
    sc.Update(0.016f);
    GameObject* a = pool.Get();
    sc.Update(0.016f);
    CHECK(pool.Release(a) && pool.Release(a) && pool.IdleCount() == 1 && pool.ActiveCount() == 0);
    sc.Update(0.016f);
    GameObject* b = pool.Get();
    CHECK(b == a);
    sc.Destroy(b);                                    // destroyed behind the pool's back
    CHECK(pool.Release(b));                           // asked before the frame ends: it is going anyway
    sc.Update(0.016f);
    CHECK(pool.IdleCount() == 0 && pool.ActiveCount() == 0 && !pool.Release(b) && !pool.Owns(b));
    GameObject* c = pool.Get();
    CHECK(c != nullptr && c != b);
    CHECK(!pool.Release(sc.CreateGameObject("Stranger")));
    // The scene's own despawn of a pool object that is queued for destruction does not bring it back.
    sc.Destroy(c);
    CHECK(sc.Despawn(c) || c->IsDestroyed() || sc.IsDestroyPending(c));
    sc.Update(0.016f);
    CHECK(c->IsDestroyed() || pool.IdleCount() == 0);
}

} // namespace

int main() {
    TestSavedScene();
    TestShrink();
    TestDespawnLifetime();
    TestResetOfComponents();
    TestSpawnerUsePool();
    TestScriptCalls();
    TestSharedPools();
    TestEdges();
    TEST_MAIN_RESULT();
}
