// OkayScript query builtins return object references (follow-up to #888 Finding 108).
//
// Rays, casts, overlaps, nearest_tag and the hierarchy helpers used to return
// the hit object's NAME. When two objects share a name, acting on that name
// (Destroy, setting a position) reached the FIRST object with it, not the one
// that was hit. They now return a reference carrying the instance id, like
// GameObject.Find and `other` already do.
//
// Every case: two objects named "Crate". The first sits far away (at x = -50),
// the second (at x = 10) is the one the query finds. The script acts on the
// result; only the second may change.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Components/LogicLinks.hpp"
#include "okay/Physics/Collider3D.hpp"
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <vector>

using namespace okay;

static std::vector<std::string> g_errs;

namespace {

struct World {
    std::unique_ptr<Scene> s = std::make_unique<Scene>("Q");
    std::vector<GameObject*> crates;
    GameObject* probe = nullptr;
    ScriptComponent* sc = nullptr;
    bool ok = false;
    bool Alive(GameObject* g) const { return GameObject::FromInstanceId(g->InstanceId()) != nullptr; }
    vs::VsValue Global(const char* n) const { return sc->VM()->GetGlobal(n); }
};

// dim: 2 = 2D box colliders, 3 = 3D box colliders. `extra` adds components
// to each crate before the scene starts.
std::unique_ptr<World> Make(int dim, const std::string& src,
                            const std::function<void(GameObject*, int)>& extra = {}) {
    auto w = std::make_unique<World>();
    for (int i = 0; i < 2; ++i) {
        GameObject* c = w->s->CreateGameObject("Crate");
        c->tag = "Box";
        c->transform->localPosition = {i == 0 ? -50.0f : 10.0f, 0, 0};
        if (dim == 2) c->AddComponent<BoxCollider2D>()->size = {2, 2};
        if (dim == 3) c->AddComponent<BoxCollider3D>()->size = {2, 2, 2};
        if (extra) extra(c, i);
        w->crates.push_back(c);
    }
    w->probe = w->s->CreateGameObject("Probe");
    w->probe->transform->localPosition = {8, 3, 0};   // nearer the second crate
    w->sc = w->probe->AddComponent<ScriptComponent>("okayscript");
    std::string e;
    w->ok = w->sc->LoadSource(src, &e);
    if (!w->ok) std::printf("  parse: %s\n", e.c_str());
    w->s->physicsEnabled = false;
    w->s->Start();
    w->s->Update(1.0f / 60.0f);
    w->s->Update(1.0f / 60.0f);   // Destroy() takes effect at the end of a frame
    return w;
}

// Run `body` once from Update().
std::string Once(const std::string& body) {
    return "bool done; string seen;\nvoid Update() { if (done) return; done = true;\n  " + body + "\n}\n";
}

bool NoErrors(const char* what) {
    for (auto& m : g_errs) { std::printf("  %s: error: %s\n", what, m.c_str()); return false; }
    return true;
}

// The second crate was destroyed and the first one kept.
void ExpectSecondDestroyed(const char* what, int dim, const std::string& body) {
    g_errs.clear();
    auto w = Make(dim, Once(body));
    CHECK(w->ok);
    const bool first = w->Alive(w->crates[0]), second = w->Alive(w->crates[1]);
    if (!first || second)
        std::printf("  %s: first crate %s, second crate %s (want alive, destroyed)\n", what,
                    first ? "alive" : "DESTROYED", second ? "ALIVE" : "destroyed");
    CHECK(first);
    CHECK(!second);
    CHECK(NoErrors(what));
}

} // namespace

int main() {
    RUN_SUITE("okayscript_query_refs");
    Log::sink = [](Log::Level l, const std::string& m) { if (l >= Log::Level::Error) g_errs.push_back(m); };

    // ---- 3D rays, casts and overlaps -> Destroy the result ----
    ExpectSecondDestroyed("raycast3", 3, "Destroy(raycast3(0, 0, 0, 1, 0, 0, 100));");
    ExpectSecondDestroyed("ray3_object", 3, "raycast3(0, 0, 0, 1, 0, 0, 100); Destroy(ray3_object());");
    ExpectSecondDestroyed("raycast_all3", 3, "var hs = raycast_all3(0, 0, 0, 1, 0, 0, 100); Destroy(hs[0]);");
    ExpectSecondDestroyed("ray_all_object", 3, "raycast_all3(0, 0, 0, 1, 0, 0, 100); Destroy(ray_all_object(0));");
    ExpectSecondDestroyed("sphere_cast3", 3, "Destroy(sphere_cast3(0, 0, 0, 1, 0, 0, 0.5, 100));");
    ExpectSecondDestroyed("box_cast3", 3, "Destroy(box_cast3(0, 0, 0, 0.5, 0.5, 0.5, 1, 0, 0, 100));");
    ExpectSecondDestroyed("capsule_cast3", 3, "Destroy(capsule_cast3(0, -0.5, 0, 0, 0.5, 0, 0.4, 1, 0, 0, 100));");
    ExpectSecondDestroyed("ray3_cast object", 3,
        "Destroy(ray3_cast(\"object\", new Vector3(0, 0, 0), new Vector3(1, 0, 0), 100));");
    ExpectSecondDestroyed("ray3_cast all", 3,
        "var hs = ray3_cast(\"all\", new Vector3(0, 0, 0), new Vector3(1, 0, 0), 100); Destroy(hs[0]);");
    ExpectSecondDestroyed("overlap_sphere3", 3, "foreach (var o in overlap_sphere3(10, 0, 0, 2)) { Destroy(o); }");
    ExpectSecondDestroyed("overlap_box3", 3, "foreach (var o in overlap_box3(10, 0, 0, 2, 2, 2)) { Destroy(o); }");
    ExpectSecondDestroyed("overlap_capsule3", 3, "foreach (var o in overlap_capsule3(10, -1, 0, 10, 1, 0, 1)) { Destroy(o); }");
    ExpectSecondDestroyed("overlap3", 3,
        "foreach (var o in overlap3(\"sphere\", new Vector3(10, 0, 0), 2)) { Destroy(o); }");

    // ---- 2D rays, casts and overlaps ----
    ExpectSecondDestroyed("raycast", 2, "Destroy(raycast(0, 0, 1, 0, 100));");
    ExpectSecondDestroyed("ray_object", 2, "raycast(0, 0, 1, 0, 100); Destroy(ray_object());");
    ExpectSecondDestroyed("raycast_all", 2, "var hs = raycast_all(0, 0, 1, 0, 100); Destroy(hs[0]);");
    ExpectSecondDestroyed("box_cast", 2, "Destroy(box_cast(0, 0, 0.5, 0.5, 1, 0, 100));");
    ExpectSecondDestroyed("box_cast object", 2, "Destroy(box_cast(\"object\", 0, 0, 0.5, 0.5, 1, 0, 100));");
    ExpectSecondDestroyed("capsule_cast", 2, "Destroy(capsule_cast(0, -0.4, 0, 0.4, 0.3, 1, 0, 100));");
    ExpectSecondDestroyed("overlap_box", 2, "foreach (var o in overlap_box(10, 0, 2, 2)) { Destroy(o); }");
    ExpectSecondDestroyed("overlap_capsule", 2, "foreach (var o in overlap_capsule(10, -1, 10, 1, 1)) { Destroy(o); }");

    // ---- nearest_tag (and its alias) ----
    ExpectSecondDestroyed("nearest_tag", 0, "Destroy(nearest_tag(\"Box\"));");
    ExpectSecondDestroyed("nearest_with_tag", 0, "Destroy(nearest_with_tag(\"Box\"));");

    // ---- setting a position through a ray result moves the hit copy ----
    {
        g_errs.clear();
        auto w = Make(3, Once("var h = raycast3(0, 0, 0, 1, 0, 0, 100);\n"
                              "  h.transform.position = new Vector3(10, 33, 0);\n"
                              "  seen = $\"{h}\";"));
        CHECK(w->ok);
        CHECK_NEAR(w->crates[1]->transform->Position().y, 33.0f, 1e-4f);
        CHECK_NEAR(w->crates[0]->transform->Position().y, 0.0f, 1e-4f);
        if (w->crates[0]->transform->Position().y != 0.0f) std::printf("  ray position: moved the FIRST crate\n");
        CHECK(w->Global("seen").AsString() == "Crate");   // still prints as the name
        CHECK(NoErrors("ray position"));
    }
    // ---- old scripts: comparing a hit with a name, and a miss with "" ----
    {
        g_errs.clear();
        auto w = Make(3, "bool done; bool named; bool missEmpty; bool missFalse;\n"
                         "void Update() { if (done) return; done = true;\n"
                         "  named = raycast3(0, 0, 0, 1, 0, 0, 100) == \"Crate\";\n"
                         "  var m = raycast3(0, 0, 0, 0, 1, 0, 100);\n"
                         "  missEmpty = m == \"\"; missFalse = !m;\n"
                         "}\n");
        CHECK(w->ok);
        CHECK(w->Global("named").AsBool());
        CHECK(w->Global("missEmpty").AsBool());
        CHECK(w->Global("missFalse").AsBool());
        CHECK(NoErrors("compat"));
    }
    // ---- hierarchy: parent_of and get_child return references ----
    {
        g_errs.clear();
        // Two parents named "Crate"; the second has a uniquely named child.
        auto w = Make(0, Once("Destroy(parent_of(\"Shelf\"));"), [](GameObject* c, int i) {
            if (i != 1) return;
            GameObject* ch = c->scene()->CreateGameObject("Shelf");
            ch->transform->SetParent(c->transform);
        });
        CHECK(w->ok);
        CHECK(w->Alive(w->crates[0]));
        CHECK(!w->Alive(w->crates[1]));
        if (!w->Alive(w->crates[0])) std::printf("  parent_of: destroyed the FIRST crate\n");
        CHECK(NoErrors("parent_of"));
    }
    {
        g_errs.clear();
        // Two children named "Crate", under parents "A" and "B".
        auto w = std::make_unique<World>();
        Scene& s = *w->s;
        GameObject* pa = s.CreateGameObject("A");
        GameObject* pb = s.CreateGameObject("B");
        for (GameObject* p : {pa, pb}) {
            GameObject* c = s.CreateGameObject("Crate");
            c->transform->SetParent(p->transform);
            w->crates.push_back(c);
        }
        w->probe = s.CreateGameObject("Probe");
        w->sc = w->probe->AddComponent<ScriptComponent>("okayscript");
        w->ok = w->sc->LoadSource(Once("Destroy(get_child(\"B\", 0));"));
        s.Start(); s.Update(1.0f / 60.0f); s.Update(1.0f / 60.0f);
        CHECK(w->ok);
        CHECK(w->Alive(w->crates[0]));
        CHECK(!w->Alive(w->crates[1]));
        if (!w->Alive(w->crates[0])) std::printf("  get_child: destroyed the FIRST crate\n");
        CHECK(NoErrors("get_child"));
    }

    // ---- helpers that took a name now take a reference too (logic_state) ----
    {
        g_errs.clear();
        auto w2 = std::make_unique<World>();
        Scene& s = *w2->s;
        for (int i = 0; i < 2; ++i) {
            GameObject* c = s.CreateGameObject("Crate");
            c->tag = "Box";
            c->AddComponent<LogicSource>();
            w2->crates.push_back(c);
        }
        w2->probe = s.CreateGameObject("Probe");
        w2->sc = w2->probe->AddComponent<ScriptComponent>("okayscript");
        w2->ok = w2->sc->LoadSource("bool done; bool st0; bool st1;\n"
                                    "void Update() { if (done) return; done = true;\n"
                                    "  var all = GameObject.FindGameObjectsWithTag(\"Box\");\n"
                                    "  st0 = logic_state(all[0]); st1 = logic_state(all[1]);\n"
                                    "}\n");
        s.Start();
        w2->crates[1]->GetComponent<LogicSource>()->SetOutput(true);
        s.Update(1.0f / 60.0f);
        CHECK(w2->ok);
        const bool st0 = w2->Global("st0").AsBool(), st1 = w2->Global("st1").AsBool();
        if (st0 || !st1) std::printf("  logic_state: st0=%d st1=%d (want 0 1)\n", (int)st0, (int)st1);
        CHECK(!st0);
        CHECK(st1);
        CHECK(NoErrors("logic_state"));
    }

    TEST_MAIN_RESULT();
}
