#include "test_framework.hpp"
#include <Okay.hpp>
#include <cstdio>
#include <string>
#include <vector>

using namespace okay;

namespace {

// Logs each lifecycle call into a shared list, in the order it happens.
struct Rec : public Behaviour {
    std::vector<std::string>* log = nullptr;
    int start = 0, update = 0, enable = 0, disable = 0, destroy = 0;
    bool parentSeenInDestroy = false;
    Vec3 posInDestroy;
    void Awake() override   { if (log) log->push_back("A"); }
    void OnEnable() override  { ++enable;  if (log) log->push_back("E"); }
    void Start() override   { ++start;  if (log) log->push_back("S"); }
    void Update(float) override { ++update; if (log) log->push_back("U"); }
    void OnDisable() override { ++disable; if (log) log->push_back("D"); }
    void OnDestroy() override {
        ++destroy; if (log) log->push_back("X");
        parentSeenInDestroy = transform && transform->Parent() != nullptr;
        if (transform) posInDestroy = transform->Position();
    }
};

struct PoseProbe : public Behaviour {
    Vec3 updatePos, latePos;
    void Update(float) override     { updatePos = transform->Position(); }
    void LateUpdate(float) override { latePos = transform->Position(); }
};

bool Near(const Vec3& a, const Vec3& b, float e = 1e-3f) {
    return std::fabs(a.x - b.x) < e && std::fabs(a.y - b.y) < e && std::fabs(a.z - b.z) < e;
}
std::string Join(const std::vector<std::string>& v) { std::string s; for (auto& x : v) s += x; return s; }

} // namespace

int main() {
    RUN_SUITE("scene_lifecycle");

    // ---- 1. Start waits until the component can run, and runs once ----------------
    {
        Scene sc("start");
        GameObject* go = sc.CreateGameObject("Late");
        std::vector<std::string> log;
        auto* r = go->AddComponent<Rec>(); r->log = &log;
        r->enabled = false;
        sc.Update(0.016f);
        CHECK(r->start == 0 && r->update == 0);
        r->enabled = true;
        sc.Update(0.016f);
        CHECK(r->start == 1 && r->update == 1);
        sc.Update(0.016f); sc.Update(0.016f);
        CHECK(r->start == 1 && r->update == 3);
        CHECK(Join(log).substr(0, 4) == "AESU");   // Awake, OnEnable, Start, then Update

        GameObject* off = sc.CreateGameObject("Inactive");
        off->active = false;
        auto* q = off->AddComponent<Rec>();
        sc.Update(0.016f);
        CHECK(q->start == 0 && q->update == 0);
        off->active = true;
        sc.Update(0.016f);
        CHECK(q->start == 1 && q->update == 1);
        sc.Update(0.016f);
        CHECK(q->start == 1 && q->update == 2);

        // Switching off and on again never repeats Start.
        r->enabled = false; sc.Update(0.016f);
        r->enabled = true;  sc.Update(0.016f);
        CHECK(r->start == 1);
    }

    // ---- 2. SetParent(.., true) keeps the whole world pose ------------------------
    {
        Scene sc("parent");
        GameObject* p = sc.CreateGameObject("P");
        p->transform->localPosition = {5, 0, 0};
        p->transform->localRotation = Quat::Euler(0, 0, 90);
        p->transform->localScale = {2, 2, 2};
        GameObject* c = sc.CreateGameObject("C");
        c->transform->localPosition = {7, 1, 0};
        c->transform->localRotation = Quat::Euler(0, 45, 0);
        c->transform->localScale = {1, 3, 1};
        const Vec3 pos = c->transform->Position();
        const Quat rot = c->transform->Rotation();
        const Vec3 scl = c->transform->LossyScale();
        CHECK(c->transform->SetParent(p->transform, true));
        CHECK(Near(c->transform->Position(), pos));
        CHECK(std::fabs(Quat::Dot(c->transform->Rotation(), rot)) > 0.9999f);
        CHECK(Near(c->transform->LossyScale(), scl));
        CHECK(c->transform->SetParent(nullptr, true));   // and back out again
        CHECK(Near(c->transform->Position(), pos));
        CHECK(std::fabs(Quat::Dot(c->transform->Rotation(), rot)) > 0.9999f);
        CHECK(Near(c->transform->LossyScale(), scl));

        // false keeps the local values.
        c->transform->localPosition = {1, 2, 3};
        c->transform->localRotation = Quat::Identity;
        c->transform->localScale = {1, 1, 1};
        c->transform->SetParent(p->transform, false);
        CHECK(Near(c->transform->localPosition, Vec3{1, 2, 3}));
        CHECK(Near(c->transform->localScale, Vec3{1, 1, 1}));

        // Rotated parent with non-uniform scale: position and rotation are exact, and
        // an unrotated child keeps its lossy scale.
        c->transform->SetParent(nullptr, false);
        p->transform->localScale = {2, 1, 1};
        c->transform->localPosition = {3, 4, 5};
        c->transform->localRotation = Quat::Identity;
        c->transform->localScale = {1, 1, 1};
        const Vec3 pos2 = c->transform->Position();
        c->transform->SetParent(p->transform, true);
        CHECK(Near(c->transform->Position(), pos2));
        CHECK(std::fabs(Quat::Dot(c->transform->Rotation(), Quat::Identity)) > 0.9999f);
        CHECK(Near(c->transform->LossyScale(), Vec3{1, 1, 1}));
    }

    // ---- 3. Destroy keeps the pose until the flush; OnDestroy sees its parent ------
    {
        Scene sc("destroy");
        GameObject* p = sc.CreateGameObject("P");
        p->transform->localPosition = {10, 0, 0};
        p->transform->localRotation = Quat::Euler(0, 0, 90);
        GameObject* c = sc.CreateGameObject("C");
        c->transform->SetParent(p->transform, false);
        c->transform->localPosition = {0, 5, 0};
        auto* rec = c->AddComponent<Rec>();
        auto* probe = c->AddComponent<PoseProbe>();
        sc.Update(0.016f);
        const Vec3 pos = c->transform->Position();
        CHECK(Near(pos, Vec3{5, 0, 0}));
        sc.Destroy(c);
        CHECK(Near(c->transform->Position(), pos));   // still there, still placed by its parent
        CHECK(c->transform->Parent() == p->transform);
        sc.Update(0.016f);
        CHECK(Near(probe->updatePos, pos) && Near(probe->latePos, pos));
        CHECK(rec->destroy == 1 && rec->parentSeenInDestroy && Near(rec->posInDestroy, pos));
        CHECK(sc.Find("C") == nullptr);
        CHECK(p->transform->ChildCount() == 0);
        CHECK(Near(c->transform->Position(), pos));   // the shell keeps the pose it died with

        // A parent and its child die together; the child's OnDestroy still sees the parent.
        GameObject* p2 = sc.CreateGameObject("P2");
        p2->transform->localPosition = {1, 2, 3};
        GameObject* c2 = sc.CreateGameObject("C2");
        c2->transform->SetParent(p2->transform, false);
        auto* rec2 = c2->AddComponent<Rec>();
        sc.Update(0.016f);
        sc.Destroy(p2);
        sc.Update(0.016f);
        CHECK(sc.Find("P2") == nullptr && sc.Find("C2") == nullptr);
        CHECK(rec2->destroy == 1 && rec2->parentSeenInDestroy);
    }

    // ---- 4. The last load request wins; a failed LoadScene keeps the index ---------
    {
        { Scene a("A"); a.CreateGameObject("FromFile"); SceneSerializer::SaveToFile(a, "lc_a.okayscene"); }
        Scene b("B"); b.CreateGameObject("FromText");
        const std::string text = SceneSerializer::Serialize(b);
        Scene sc("loads");
        sc.CreateGameObject("Old");
        sc.RequestLoadText(text);
        sc.RequestLoad("lc_a.okayscene");
        sc.Update(0.016f);
        CHECK(sc.Find("FromFile") != nullptr && sc.Find("FromText") == nullptr);
        sc.RequestLoad("lc_a.okayscene");
        sc.RequestLoadText(text);
        sc.Update(0.016f);
        CHECK(sc.Find("FromText") != nullptr && sc.Find("FromFile") == nullptr);

        SceneManager::ClearScenes();
        SceneManager::AddScene("lc_a.okayscene");
        SceneManager::AddScene("lc_does_not_exist.okayscene");
        Scene sm("sm");
        CHECK(SceneManager::LoadScene(sm, 0));
        sm.Update(0.016f);
        CHECK(SceneManager::ActiveIndex() == 0 && sm.Find("FromFile") != nullptr);
        CHECK(SceneManager::LoadScene(sm, 1));
        sm.Update(0.016f);
        CHECK(SceneManager::ActiveIndex() == 0);       // the failed load did not move it
        CHECK(SceneManager::ReloadScene(sm));          // so a reload is still the last good scene
        CHECK(SceneManager::LoadNextScene(sm));        // and next is the one after it
        sm.Update(0.016f);
        CHECK(SceneManager::ActiveIndex() == 0);
        // Two requests in a frame: the later one decides the index.
        SceneManager::AddScene("lc_a2.okayscene");
        { Scene a2("A2"); a2.CreateGameObject("Second"); SceneSerializer::SaveToFile(a2, "lc_a2.okayscene"); }
        SceneManager::LoadScene(sm, 1);
        SceneManager::LoadScene(sm, 2);
        sm.Update(0.016f);
        CHECK(SceneManager::ActiveIndex() == 2 && sm.Find("Second") != nullptr);
        SceneManager::ClearScenes();
        std::remove("lc_a.okayscene"); std::remove("lc_a2.okayscene");
    }

    // ---- 5. OnEnable / OnDisable ---------------------------------------------------
    {
        Scene sc("enable");
        std::vector<std::string> log;
        GameObject* go = sc.CreateGameObject("E");
        auto* r = go->AddComponent<Rec>(); r->log = &log;
        sc.Update(0.016f);
        CHECK(Join(log) == "AESU");
        log.clear();
        r->enabled = false;
        sc.Update(0.016f);
        CHECK(Join(log) == "D");
        log.clear();
        r->enabled = true;
        sc.Update(0.016f);
        CHECK(Join(log) == "EU");
        log.clear();
        go->active = false;
        sc.Update(0.016f);
        CHECK(Join(log) == "D");
        log.clear();
        go->active = true;
        sc.Update(0.016f);
        CHECK(Join(log) == "EU");
        log.clear();
        sc.Destroy(go);
        sc.Update(0.016f);
        CHECK(Join(log) == "UDX");    // OnDisable right before OnDestroy

        // Already off when destroyed: no second OnDisable.
        GameObject* g2 = sc.CreateGameObject("E2");
        std::vector<std::string> log2;
        auto* r2 = g2->AddComponent<Rec>(); r2->log = &log2;
        sc.Update(0.016f);
        r2->enabled = false;
        sc.Update(0.016f);
        log2.clear();
        sc.Destroy(g2);
        sc.Update(0.016f);
        CHECK(Join(log2) == "X");

        // Removing a component also disables it first.
        GameObject* g3 = sc.CreateGameObject("E3");
        std::vector<std::string> log3;
        auto* r3 = g3->AddComponent<Rec>(); r3->log = &log3;
        sc.Update(0.016f);
        log3.clear();
        g3->RemoveComponent(r3);
        CHECK(Join(log3) == "DX");
    }

    // ---- 6. Delayed destroy ----------------------------------------------------------
    {
        Scene sc("delayed");
        GameObject* a = sc.CreateGameObject("A");
        auto* ra = a->AddComponent<Rec>();
        sc.Destroy(a, 1.0f);
        sc.Update(0.5f);
        CHECK(sc.Find("A") == a && ra->destroy == 0);
        sc.Update(0.6f);
        CHECK(sc.Find("A") == nullptr && ra->destroy == 1);

        // Destroyed earlier: the timer is dropped, nothing happens twice.
        GameObject* b = sc.CreateGameObject("B");
        auto* rb = b->AddComponent<Rec>();
        sc.Destroy(b, 1.0f);
        sc.Update(0.1f);
        sc.Destroy(b);
        sc.Update(0.1f);
        CHECK(sc.Find("B") == nullptr && rb->destroy == 1);
        sc.Update(2.0f);
        CHECK(rb->destroy == 1);

        GameObject* c = sc.CreateGameObject("C");
        sc.Destroy(c, 0.0f);   // zero = now
        sc.Update(0.016f);
        CHECK(sc.Find("C") == nullptr);

        GameObject* d = sc.CreateGameObject("D");
        sc.Destroy(d, 2.0f); sc.Destroy(d, 0.5f);   // the sooner time wins
        sc.Update(0.6f);
        CHECK(sc.Find("D") == nullptr);
    }

    // ---- 7. DontDestroyOnLoad ----------------------------------------------------------
    {
        { Scene lvl("L"); lvl.CreateGameObject("Level"); SceneSerializer::SaveToFile(lvl, "lc_level.okayscene"); }
        Scene sc("keep");
        GameObject* keeper = sc.CreateGameObject("Keeper");
        GameObject* kid = sc.CreateGameObject("KeeperKid");
        kid->transform->SetParent(keeper->transform, false);
        auto* rk = kid->AddComponent<Rec>();
        GameObject* doomed = sc.CreateGameObject("Doomed");
        sc.Update(0.016f);
        const std::uint64_t id = keeper->InstanceId(), kidId = kid->InstanceId();
        CHECK(rk->start == 1);
        sc.DontDestroyOnLoad(keeper);
        CHECK(sc.IsDontDestroyOnLoad(keeper) && !sc.IsDontDestroyOnLoad(doomed));

        sc.RequestMerge("lc_level.okayscene");   // a merge never clears anything
        sc.Update(0.016f);
        CHECK(sc.Find("Doomed") != nullptr && sc.Find("Keeper") == keeper);

        sc.RequestLoad("lc_level.okayscene");
        sc.Update(0.016f);
        CHECK(sc.Find("Level") != nullptr);
        CHECK(sc.Find("Doomed") == nullptr);
        CHECK(sc.Find("Keeper") == keeper && sc.Find("KeeperKid") == kid);
        CHECK(keeper->InstanceId() == id && kid->InstanceId() == kidId);
        CHECK(GameObject::FromInstanceId(id) == keeper && GameObject::FromInstanceId(kidId) == kid);
        CHECK(kid->transform->Parent() == keeper->transform);
        CHECK(sc.IsDontDestroyOnLoad(keeper));
        const int updates = rk->update;
        sc.Update(0.016f);
        CHECK(rk->update == updates + 1 && rk->start == 1 && rk->destroy == 0);   // alive, not restarted

        sc.RequestLoad("lc_level.okayscene");    // and again
        sc.Update(0.016f);
        CHECK(sc.Find("Keeper") == keeper && keeper->InstanceId() == id);

        sc.Destroy(keeper);                       // an explicit Destroy still works
        sc.Update(0.016f);
        CHECK(sc.Find("Keeper") == nullptr && !sc.IsDontDestroyOnLoad(keeper));
        std::remove("lc_level.okayscene");
    }

    // ---- 8. Freed graveyard shells leave nothing pointing at them ----------------------
    {
        Scene sc("graveyard");
        GameObject* adopter = sc.CreateGameObject("Adopter");
        auto flushFillers = [&](int n) {
            for (int i = 0; i < n; ++i) sc.Destroy(sc.CreateGameObject("f"));
            sc.FlushDestroyed();
        };
        flushFillers(9999);                       // shells 0..9998
        GameObject* p = sc.CreateGameObject("P");
        GameObject* c = sc.CreateGameObject("C");
        c->transform->SetParent(p->transform, false);
        sc.Destroy(p);                            // P is shell 9999, its child C shell 10000
        sc.FlushDestroyed();
        adopter->transform->SetParent(p->transform, false);   // adopted by a dead parent
        flushFillers(10000);                      // 20001 shells: the oldest 10000 (P among them) are freed
        CHECK(c->transform->Parent() == nullptr);       // C outlives P, so it must not point at it
        CHECK(adopter->transform->Parent() == nullptr);
        (void)c->transform->Position(); (void)adopter->transform->Position();
    }

    // ---- 9. SceneBridge does not keep a freed GameObject ---------------------------------
    {
        using namespace okay::ecs;
        Scene sc("bridge");
        World w;
        SceneBridge bridge(w, sc);
        Entity e = w.create();
        w.add<EcsTransform>(e, {});
        bridge.sync();
        CHECK(bridge.gameObjectFor(e) != nullptr);
        sc.Clear();                               // frees every object the bridge made
        CHECK(bridge.gameObjectFor(e) == nullptr);
        bridge.sync();                            // must not touch the freed object
        CHECK(bridge.mappedCount() == 1);
    }

    TEST_MAIN_RESULT();
}
