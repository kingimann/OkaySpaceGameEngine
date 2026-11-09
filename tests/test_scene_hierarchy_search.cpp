// Hierarchy-aware active state, float round trips, OnDestroy on unload, load error
// reporting, mass destroy, the search API, Transform helpers, scene validation and
// the native FixedUpdate.
#include "test_framework.hpp"
#include <Okay.hpp>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

using namespace okay;

namespace {

struct Counter : public Behaviour {
    int update = 0, late = 0, fixed = 0, destroy = 0, disable = 0, enable = 0;
    float fixedSum = 0.0f;
    void Update(float) override { ++update; }
    void LateUpdate(float) override { ++late; }
    void FixedUpdate(float dt) override { ++fixed; fixedSum += dt; }
    void OnEnable() override { ++enable; }
    void OnDisable() override { ++disable; }
    void OnDestroy() override { ++destroy; }
};

struct DestroyCount : public Behaviour {
    int* counter = nullptr;
    void OnDestroy() override { if (counter) ++*counter; }
};

// Removes a sibling component from its Awake (the use-after-free guard case).
struct RemoveOther : public Behaviour {
    Component* victim = nullptr;
    void Awake() override { if (victim && gameObject) gameObject->RemoveComponent(victim); }
};
struct AwakeMark : public Behaviour {
    static inline int awakes = 0;
    void Awake() override { ++awakes; }
};

std::string Slurp(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

} // namespace

int main() {
    // ---- 1. hierarchy-aware active -------------------------------------------
    RUN_SUITE("active in hierarchy");
    {
        Scene s("H");
        s.physics3D().gravity = {0, -10, 0};
        GameObject* holder = s.CreateGameObject("Holder");
        GameObject* child = s.CreateGameObject("Child");
        child->transform->SetParent(holder->transform, false);
        GameObject* grand = s.CreateGameObject("Grand");
        grand->transform->SetParent(child->transform, false);
        auto* cc = child->AddComponent<Counter>();
        auto* gc = grand->AddComponent<Counter>();
        // a body under the holder
        grand->transform->localPosition = {0, 5, 0};
        grand->AddComponent<BoxCollider3D>();
        auto* rb = grand->AddComponent<Rigidbody3D>();

        CHECK(holder->ActiveInHierarchy() && grand->ActiveInHierarchy());
        s.Update(0.02f);
        CHECK(cc->update == 1 && gc->late == 1);

        holder->active = false;
        CHECK(!holder->ActiveInHierarchy() && !child->ActiveInHierarchy() && !grand->ActiveInHierarchy());
        CHECK(child->active && grand->active);        // `active` stays the self flag
        const float y0 = grand->transform->Position().y;
        const float vy0 = rb->velocity.y;
        for (int i = 0; i < 5; ++i) s.Update(0.02f);
        CHECK(cc->update == 1 && cc->late == 1 && gc->update == 1);   // nothing ran while the parent was off
        CHECK_NEAR(grand->transform->Position().y, y0, 1e-5f);        // and the body did not fall
        CHECK(rb->velocity.y == vy0);
        CHECK(cc->disable == 1 && gc->disable == 1);                  // children got OnDisable

        holder->active = true;
        s.Update(0.02f);
        CHECK(cc->update == 2 && cc->enable == 2);
        CHECK(grand->transform->Position().y < y0);                   // simulating again

        // a child that is itself inactive stays off under an active parent
        child->active = false;
        s.Update(0.02f);
        CHECK(cc->update == 2 && gc->update == 2);
        CHECK(!grand->ActiveInHierarchy());
    }

    // ---- 2. float precision ---------------------------------------------------
    RUN_SUITE("float round trip");
    {
        Scene s("F");
        GameObject* go = s.CreateGameObject("Far");
        go->transform->localPosition = {12345.678f, 123456.78f, 0.1f};
        go->transform->localScale = {1.0f / 3.0f, 0.5f, 2.0f};
        const std::string text = SceneSerializer::Serialize(s);
        Scene t("T");
        CHECK(SceneSerializer::Deserialize(t, text));
        GameObject* b = t.Find("Far");
        CHECK(b != nullptr);
        if (b) {
            CHECK(b->transform->localPosition.x == 12345.678f);
            CHECK(b->transform->localPosition.y == 123456.78f);
            CHECK(b->transform->localPosition.z == 0.1f);
            CHECK(b->transform->localScale.x == 1.0f / 3.0f);
        }
        CHECK(text.find("12345.678") != std::string::npos);
        CHECK(text.find("0.1 ") != std::string::npos || text.find("0.1\n") != std::string::npos);   // short values stay short
        CHECK(text.find("0.100000") == std::string::npos);
        // a second save of the loaded scene is byte-identical: no drift
        CHECK(SceneSerializer::Serialize(t) == text);
        // an old-style file with 6-digit numbers still loads
        Scene o("O");
        CHECK(SceneSerializer::Deserialize(o,
            "okayscene 3\nname \"x\"\ngravity 0 -9.81\ngameobject 0 \"A\"\n  active 1\n  parent -1\n"
            "  transform 12345.7 1.5e+06 0 0 0 0 1 1 1 1\nend\n"));
        CHECK(o.Find("A") != nullptr);
    }

    // ---- 3. OnDestroy on Clear / ~Scene ---------------------------------------
    RUN_SUITE("OnDestroy on unload");
    {
        int destroyed = 0;
        {
            Scene s("D");
            for (int i = 0; i < 3; ++i) s.CreateGameObject("O")->AddComponent<DestroyCount>()->counter = &destroyed;
            s.Update(0.016f);
            s.Clear();
            CHECK(destroyed == 3);
            s.CreateGameObject("P")->AddComponent<DestroyCount>()->counter = &destroyed;
            s.Update(0.016f);
        }
        CHECK(destroyed == 4);   // ~Scene ran the last one
        // a scene load replaces the scene: the old objects' OnDestroy runs
        Scene a("src"); a.CreateGameObject("New");
        CHECK(SceneSerializer::SaveToFile(a, "hs_new.okayscene"));
        Scene s("L");
        int n = 0;
        s.CreateGameObject("Old")->AddComponent<DestroyCount>()->counter = &n;
        s.Update(0.016f);
        s.RequestLoad("hs_new.okayscene");
        s.Update(0.016f);
        CHECK(n == 1 && s.Find("New") && !s.Find("Old"));
        // components that never got to Awake have nothing to undo
        int m = 0;
        { Scene u("U"); u.CreateGameObject("X")->AddComponent<DestroyCount>()->counter = &m; }
        CHECK(m == 0);
        std::remove("hs_new.okayscene");
    }

    // ---- 4. 3D physics reset + gravity record ---------------------------------
    RUN_SUITE("3D gravity");
    {
        Scene s("G");
        s.physics3D().gravity = {0, -1.6f, 0};
        s.CreateGameObject("X");
        const std::string text = SceneSerializer::Serialize(s);
        CHECK(text.find("gravity3 ") != std::string::npos);
        Scene t("T");
        t.physics3D().gravity = {0, -30, 0};
        CHECK(SceneSerializer::Deserialize(t, text));
        CHECK_NEAR(t.physics3D().gravity.y, -1.6f, 1e-6f);
        // a file with no gravity3 record loads the default, not the old scene's value
        Scene u("U");
        u.physics3D().gravity = {0, -30, 0};
        CHECK(SceneSerializer::Deserialize(u, "okayscene 3\nname \"x\"\ngravity 0 -9.81\n"));
        CHECK_NEAR(u.physics3D().gravity.y, -9.81f, 1e-6f);
        u.physics3D().gravity = {0, -1, 0};
        u.Clear();
        CHECK_NEAR(u.physics3D().gravity.y, -9.81f, 1e-6f);
        u.uiFont = "a.ttf"; u.Clear(); CHECK(u.uiFont.empty());
    }

    // ---- 5. load failure reporting -------------------------------------------
    RUN_SUITE("load errors");
    {
        Scene a("A"); a.CreateGameObject("Good");
        SceneSerializer::SaveToFile(a, "hs_good.okayscene");
        { std::ofstream f("hs_bad.okayscene"); f << "not a scene\n"; }
        SceneManager::ClearScenes();
        SceneManager::AddScene("hs_good.okayscene");
        SceneManager::AddScene("hs_bad.okayscene");
        SceneManager::AddScene("hs_missing.okayscene");
        Scene s("S");
        bool okSeen = true;
        SceneManager::LoadScene(s, 0);
        s.Update(0.0f);
        CHECK(SceneManager::ActiveIndex() == 0 && s.LastLoadError().empty());
        SceneManager::LoadScene(s, 1);
        CHECK(SceneManager::ActiveIndex() == 0);     // not moved by the request
        s.Update(0.0f);
        CHECK(SceneManager::ActiveIndex() == 0);     // nor by a corrupt file
        CHECK(!s.LastLoadError().empty() && s.LastLoadError().find("hs_bad") != std::string::npos);
        CHECK(s.Find("Good") != nullptr);            // old scene intact
        s.RequestLoad("hs_missing.okayscene", [&](bool ok) { okSeen = ok; });
        s.Update(0.0f);
        CHECK(!okSeen && !s.LastLoadError().empty());
        SceneManager::LoadScene(s, 0);
        s.Update(0.0f);
        CHECK(s.LastLoadError().empty());
        // two LoadNextScene calls in one frame step twice
        SceneManager::LoadNextScene(s);
        SceneManager::LoadNextScene(s);
        CHECK(s.HasPendingLoad());
        SceneManager::ClearScenes();
        std::remove("hs_good.okayscene"); std::remove("hs_bad.okayscene");
    }

    // ---- 6. mass destroy, flush guard, prefab cache ---------------------------
    RUN_SUITE("destroy + caches");
    {
        Scene s("M");
        GameObject* root = s.CreateGameObject("Root");
        std::vector<GameObject*> all;
        for (int i = 0; i < 4000; ++i) {
            GameObject* g = s.CreateGameObject("n" + std::to_string(i));
            g->AddComponent<Counter>();
            if (i % 2) g->transform->SetParent(root->transform, false);
            all.push_back(g);
        }
        s.Update(0.016f);
        auto t0 = std::chrono::steady_clock::now();
        for (GameObject* g : all) { s.Destroy(g); s.Destroy(g); }   // double destroy is harmless
        s.FlushDestroyed();
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        CHECK(s.Objects().size() == 1 && s.Find("Root") != nullptr);
        CHECK(root->transform->ChildCount() == 0);
        CHECK(ms < 2000.0);   // generous: the old quadratic version took many seconds here
        s.Update(0.016f);

        // a component removed by another's Awake in the same batch is never touched
        AwakeMark::awakes = 0;
        GameObject* g = s.CreateGameObject("Guard");
        auto* ro = g->AddComponent<RemoveOther>();
        auto* victim = g->AddComponent<AwakeMark>();
        ro->victim = victim;
        g->AddComponent<AwakeMark>();
        s.Update(0.016f);
        CHECK(AwakeMark::awakes == 1 && g->GetComponents<AwakeMark>().size() == 1);

        // InstantiateFromFile reads a changed prefab file again
        Scene src("P"); GameObject* pf = src.CreateGameObject("Bullet");
        pf->transform->localPosition = {1, 2, 3};
        SceneSerializer::SaveObjectToFile(*pf, "hs_prefab.okayprefab");
        Scene d("D");
        GameObject* a = SceneSerializer::InstantiateFromFile(d, "hs_prefab.okayprefab");
        GameObject* b = SceneSerializer::InstantiateFromFile(d, "hs_prefab.okayprefab");
        CHECK(a && b && a != b && a->transform->localPosition.y == 2.0f && b->transform->localPosition.z == 3.0f);
        pf->name = "BulletMuchLongerName"; pf->transform->localPosition = {9, 9, 9};
        SceneSerializer::SaveObjectToFile(*pf, "hs_prefab.okayprefab");
        GameObject* c = SceneSerializer::InstantiateFromFile(d, "hs_prefab.okayprefab");
        CHECK(c && c->name == "BulletMuchLongerName" && c->transform->localPosition.x == 9.0f);
        std::remove("hs_prefab.okayprefab");
    }

    // ---- 7. search API -----------------------------------------------------------
    RUN_SUITE("search");
    {
        Scene s("Q");
        GameObject* a = s.CreateGameObject("A");
        GameObject* b = s.CreateGameObject("B");
        GameObject* c = s.CreateGameObject("C");
        GameObject* other = s.CreateGameObject("B");   // a second root called B
        b->transform->SetParent(a->transform, false);
        c->transform->SetParent(b->transform, false);
        a->tag = "Enemy"; c->tag = "Enemy"; other->tag = "Boss";
        CHECK(s.FindGameObjectsWithTag("Enemy").size() == 2);
        CHECK(s.FindGameObjectsWithTag("Nope").empty());
        CHECK(s.FindByPath("A/B/C") == c && s.Find("A/B/C") == c && s.Find("/A/B") == b);
        CHECK(s.Find("B/C") == nullptr);                 // B is not a root with a child C... the root B has none
        CHECK(s.Find("A/Z") == nullptr);
        auto roots = s.Roots();
        CHECK(roots.size() == 2 && roots[0] == a && roots[1] == other);

        auto* cl = c->AddComponent<Counter>();
        auto* al = a->AddComponent<Counter>();
        CHECK(a->GetComponentInChildren<Counter>() == al);
        CHECK(b->GetComponentInChildren<Counter>() == cl);
        CHECK(c->GetComponentInParent<Counter>() == cl);
        CHECK(b->GetComponentInParent<Counter>() == al);
        CHECK(a->GetComponentsInChildren<Counter>().size() == 2);
        c->active = false;
        CHECK(b->GetComponentInChildren<Counter>() == nullptr);
        CHECK(b->GetComponentInChildren<Counter>(true) == cl);
        CHECK(a->GetComponentsInChildren<Counter>().size() == 1);
        CHECK(a->GetComponentsInChildren<Counter>(true).size() == 2);
        CHECK(c->GetComponentInParent<Counter>() == al);          // own is inactive: skipped
        CHECK(c->GetComponentInParent<Counter>(true) == cl);
    }

    // ---- 8. transform helpers ----------------------------------------------------
    RUN_SUITE("transform helpers");
    {
        Scene s("T");
        GameObject* p = s.CreateGameObject("P");
        GameObject* k = s.CreateGameObject("K");
        k->transform->SetParent(p->transform, false);
        p->transform->localScale = {2, 4, 1};
        k->transform->SetLossyScale({6, 8, 3});
        CHECK_NEAR(k->transform->localScale.x, 3.0f, 1e-5f);
        CHECK_NEAR(k->transform->localScale.y, 2.0f, 1e-5f);
        CHECK_NEAR(k->transform->LossyScale().z, 3.0f, 1e-5f);

        GameObject* l = s.CreateGameObject("L");
        l->transform->localPosition = {1, 1, 1};
        l->transform->LookAt({1, 1, 11});
        Vec3 f = l->transform->Forward();
        CHECK_NEAR(f.x, 0.0f, 1e-4f); CHECK_NEAR(f.z, 1.0f, 1e-4f);
        l->transform->LookAt({11, 1, 1});
        f = l->transform->Forward();
        CHECK_NEAR(f.x, 1.0f, 1e-4f); CHECK_NEAR(f.z, 0.0f, 1e-4f);

        GameObject* e = s.CreateGameObject("E");
        e->transform->SetEulerAngles({10, 20, 30});
        Vec3 eu = e->transform->EulerAngles();
        CHECK_NEAR(eu.x, 10.0f, 1e-3f); CHECK_NEAR(eu.y, 20.0f, 1e-3f); CHECK_NEAR(eu.z, 30.0f, 1e-3f);
        e->transform->SetLocalEulerAngles({0, 90, 0});
        CHECK_NEAR(e->transform->LocalEulerAngles().y, 90.0f, 1e-3f);

        // sibling index among children
        GameObject* par = s.CreateGameObject("Par");
        GameObject* c0 = s.CreateGameObject("c0"); GameObject* c1 = s.CreateGameObject("c1"); GameObject* c2 = s.CreateGameObject("c2");
        for (GameObject* g : {c0, c1, c2}) g->transform->SetParent(par->transform, false);
        CHECK(c0->transform->GetSiblingIndex() == 0 && c2->transform->GetSiblingIndex() == 2);
        c0->transform->SetSiblingIndex(2);
        CHECK(par->transform->Children()[0] == c1->transform && par->transform->Children()[2] == c0->transform);
        c2->transform->SetSiblingIndex(-5);
        CHECK(c2->transform->GetSiblingIndex() == 0);
        c1->transform->SetSiblingIndex(99);
        CHECK(c1->transform->GetSiblingIndex() == 2);
        // roots
        const int pi = p->transform->GetSiblingIndex();
        CHECK(pi == 0);
        p->transform->SetSiblingIndex(3);
        CHECK(p->transform->GetSiblingIndex() == 3);
        auto roots = s.Roots();
        CHECK(roots[3] == p);
    }

    // ---- 9. validation --------------------------------------------------------------
    RUN_SUITE("validate");
    {
        Scene s("V");
        GameObject* a = s.CreateGameObject("Sprite");
        a->AddComponent<SpriteRenderer>()->texture = "definitely_missing_texture.png";
        s.CreateGameObject("Plain");
        SceneValidationReport clean = s.Validate();
        CHECK(clean.Count(SceneIssue::Kind::MissingAsset) == 1);
        CHECK(clean.Count(SceneIssue::Kind::BrokenParentLink) == 0);

        std::string text = SceneSerializer::Serialize(s);
        // a parent link into nowhere, a component this build lacks, a top-level record it lacks, duplicate guids
        std::string bad = text;
        bad += "futurething 1 2 3\n";
        bad += "gameobject 40 \"Broken\"\n  active 1\n  guid \"abc\"\n  parent 77\n  hologram 1 2\nend\n";
        bad += "gameobject 41 \"Twin\"\n  active 1\n  guid \"abc\"\n  parent -1\nend\n";
        SceneValidationReport r = SceneSerializer::Validate(bad);
        CHECK(r.Count(SceneIssue::Kind::BrokenParentLink) == 1);
        CHECK(r.Count(SceneIssue::Kind::MissingComponentType) == 1);
        CHECK(r.Count(SceneIssue::Kind::UnknownField) == 1);
        CHECK(r.Count(SceneIssue::Kind::DuplicateGuid) == 1);
        CHECK(r.Count(SceneIssue::Kind::MissingAsset) == 1);
        CHECK(!r.Ok() && r.ToString().find("hologram") != std::string::npos);
        CHECK(SceneSerializer::Validate("garbage").Count(SceneIssue::Kind::LoadError) == 1);
        CHECK(SceneSerializer::ValidateFile("hs_nonexistent_file.okayscene").Count(SceneIssue::Kind::LoadError) == 1);

        { std::ofstream f("hs_asset.png"); f << "x"; }
        a->GetComponent<SpriteRenderer>()->texture = "hs_asset.png";
        CHECK(s.Validate().Ok());
        std::remove("hs_asset.png");
    }

    // ---- 10. native FixedUpdate -------------------------------------------------------
    RUN_SUITE("FixedUpdate");
    {
        Scene s("X");
        s.UseFixedStepPhysics(true);
        GameObject* hold = s.CreateGameObject("Hold");
        GameObject* g = s.CreateGameObject("G");
        g->transform->SetParent(hold->transform, false);
        auto* c = g->AddComponent<Counter>();
        GameObject* free = s.CreateGameObject("Free");
        auto* fc = free->AddComponent<Counter>();
        s.Update(0.0f);                 // start things
        s.Update(0.1f);                 // five 0.02 steps
        CHECK(fc->fixed == 5);
        CHECK_NEAR(fc->fixedSum, 0.1f, 1e-4f);
        CHECK(c->fixed == 5);
        hold->active = false;
        s.Update(0.1f);
        CHECK(c->fixed == 5 && fc->fixed == 10);
        hold->active = true;
        fc->enabled = false;
        s.Update(0.04f);
        CHECK(fc->fixed == 10 && c->fixed == 7);
        // no fixed step: once per frame with the frame time
        Scene t("Y");
        auto* tc = t.CreateGameObject("Z")->AddComponent<Counter>();
        t.Update(0.0f);
        t.Update(0.25f);
        CHECK(tc->fixed >= 1);
        // physics off: no FixedUpdate
        Scene u("W");
        u.physicsEnabled = false;
        auto* uc = u.CreateGameObject("Z")->AddComponent<Counter>();
        u.Update(0.0f); u.Update(0.25f);
        CHECK(uc->fixed == 0);
    }

    TEST_MAIN_RESULT();
}
