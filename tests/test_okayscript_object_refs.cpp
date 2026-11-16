// OkayScript object references (issue #888, Findings 108-111).
//
// 108: spawned copies share one name, so a reference that is only a name hits
//      the first copy. References now carry the object's instance id.
// 109: a 3D Rigidbody reached through GetComponent<Rigidbody>() ignored most of
//      its API (AddTorque, AddImpulse, ForceMode, mass, useGravity, ...).
// 110: GameObject.Find returned switched-off (inactive) objects.
// 111: other.GetComponent<T>() acted on the caller's own component.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Physics/Collider3D.hpp"
#include "okay/Physics/Physics3D.hpp"
#include "okay/Physics/Rigidbody2D.hpp"
#include "okay/Physics/Rigidbody3D.hpp"
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace okay;

static std::vector<std::string> g_errs;

// A scene with `n` objects all named "Enemy" (tag Foe) at x = 0, 5, 10, ...,
// each with a Rigidbody, plus a "Bullet" that runs `src`.
struct Arena {
    Scene s{"Arena"};
    std::vector<GameObject*> enemies;
    GameObject* bullet = nullptr;
    ScriptComponent* sc = nullptr;
    bool ok = false;
    Arena(int n, const std::string& src, const char* lang = "okayscript") {
        for (int i = 0; i < n; ++i) {
            GameObject* e = s.CreateGameObject("Enemy");
            e->tag = "Foe";
            e->transform->localPosition = {5.0f * (float)i, 0, 0};
            auto* rb = e->AddComponent<Rigidbody3D>();
            rb->gravityScale = 0.0f;
            enemies.push_back(e);
        }
        bullet = s.CreateGameObject("Bullet");
        bullet->transform->localPosition = {0, 10, 0};
        auto* rb = bullet->AddComponent<Rigidbody3D>();
        rb->gravityScale = 0.0f;
        sc = bullet->AddComponent<ScriptComponent>(lang);
        std::string e;
        ok = sc->LoadSource(src, &e);
        if (!ok) std::printf("  parse: %s\n", e.c_str());
        s.Start();
    }
    void Hit(GameObject* other) {
        Collision3D c; c.gameObject = other;
        sc->OnCollisionEnter3D(c);
    }
    bool Alive(GameObject* g) const { return GameObject::FromInstanceId(g->InstanceId()) != nullptr; }
    vs::VsValue Global(const char* n) const { return sc->VM()->GetGlobal(n); }
};

static bool NoErrors(const char* what) {
    for (auto& m : g_errs) { std::printf("  %s: error: %s\n", what, m.c_str()); return false; }
    return true;
}

int main() {
    RUN_SUITE("okayscript_object_refs");
    Log::sink = [](Log::Level l, const std::string& m) { if (l >= Log::Level::Error) g_errs.push_back(m); };

    // ---- 108: a bullet destroys the copy it hit, not the first one ----
    {
        g_errs.clear();
        Arena a(3, "void OnCollisionEnter(string other) {\n"
                   "  if (other.CompareTag(\"Foe\")) { Destroy(other); }\n"
                   "}\n");
        CHECK(a.ok);
        a.Hit(a.enemies[2]);
        CHECK(a.Alive(a.enemies[0]));
        CHECK(a.Alive(a.enemies[1]));
        CHECK(!a.Alive(a.enemies[2]));
        CHECK(NoErrors("bullet destroys what it hit"));
    }
    // ---- 108: damage through `other` goes to the hit copy only ----
    {
        g_errs.clear();
        Arena a(3, "string seen; bool named; bool eq;\n"
                   "void OnCollisionEnter(string other) {\n"
                   "  other.GetComponent<Rigidbody>().velocity = new Vector3(0, 7, 0);\n"
                   "  other.transform.position = new Vector3(42, 0, 0);\n"
                   "  seen = $\"{other}\";\n"
                   "  named = other == \"Enemy\";\n"
                   "  eq = other.name == \"Enemy\";\n"
                   "}\n");
        CHECK(a.ok);
        a.Hit(a.enemies[1]);
        CHECK_NEAR(a.enemies[1]->GetComponent<Rigidbody3D>()->velocity.y, 7.0f, 1e-4f);
        CHECK_NEAR(a.enemies[0]->GetComponent<Rigidbody3D>()->velocity.y, 0.0f, 1e-4f);
        CHECK_NEAR(a.enemies[2]->GetComponent<Rigidbody3D>()->velocity.y, 0.0f, 1e-4f);
        CHECK_NEAR(a.bullet->GetComponent<Rigidbody3D>()->velocity.y, 0.0f, 1e-4f);
        CHECK_NEAR(a.enemies[1]->transform->Position().x, 42.0f, 1e-4f);
        CHECK_NEAR(a.enemies[0]->transform->Position().x, 0.0f, 1e-4f);
        CHECK_NEAR(a.bullet->transform->Position().x, 0.0f, 1e-4f);
        CHECK(a.Global("seen").AsString() == "Enemy");   // printing a reference shows the name
        CHECK(a.Global("named").AsBool());               // comparing with a name still works
        CHECK(a.Global("eq").AsBool());
        CHECK(NoErrors("damage via other"));
    }
    // ---- 108: "destroy every enemy" destroys all of them ----
    {
        g_errs.clear();
        Arena a(5, "void Start() {\n"
                   "  foreach (var e in GameObject.FindGameObjectsWithTag(\"Foe\")) { Destroy(e); }\n"
                   "}\n");
        CHECK(a.ok);
        int alive = 0;
        for (auto* e : a.enemies) alive += a.Alive(e) ? 1 : 0;
        CHECK(alive == 0);
        if (alive) std::printf("  destroy-all: %d of 5 still alive\n", alive);
        CHECK(NoErrors("destroy all"));
    }
    // ---- 108: the same object twice is equal, two copies are not ----
    {
        g_errs.clear();
        Arena a(2, "bool same; bool diff; bool notNull;\n"
                   "void Start() {\n"
                   "  var all = GameObject.FindGameObjectsWithTag(\"Foe\");\n"
                   "  var x = all[0]; var y = all[0]; var z = all[1];\n"
                   "  same = x == y; diff = x != z; notNull = x != null;\n"
                   "}\n");
        CHECK(a.ok);
        CHECK(a.Global("same").AsBool());
        CHECK(a.Global("diff").AsBool());
        CHECK(a.Global("notNull").AsBool());
        CHECK(NoErrors("equality"));
    }
    // ---- 108: an old script that passes a plain name keeps its behaviour ----
    {
        g_errs.clear();
        Arena a(3, "void Start() { Destroy(\"Enemy\"); }\n");
        CHECK(a.ok);
        CHECK(!a.Alive(a.enemies[0]));   // the first one with that name, as before
        CHECK(a.Alive(a.enemies[1]));
        CHECK(a.Alive(a.enemies[2]));
        CHECK(NoErrors("plain name"));
    }
    // ---- 108: Instantiate returns a reference to the new copy ----
    {
        g_errs.clear();
        const std::string prefab = "okayscript_object_refs_test.okayprefab";
        {
            Scene tmp("P");
            GameObject* p = tmp.CreateGameObject("Enemy");
            p->AddComponent<Rigidbody3D>()->gravityScale = 0.0f;
            CHECK(SceneSerializer::SaveObjectToFile(*p, prefab));
        }
        Arena a(1, "bool made; string nm;\n"
                   "void Start() {\n"
                   "  var e = Instantiate(\"" + prefab + "\", new Vector3(3, 0, 0), Quaternion.identity);\n"
                   "  made = e != null; nm = $\"{e}\";\n"
                   "  e.GetComponent<Rigidbody>().velocity = new Vector3(0, 0, 9);\n"
                   "}\n");
        CHECK(a.ok);
        CHECK(a.Global("made").AsBool());
        CHECK(a.Global("nm").AsString() == "Enemy");
        CHECK_NEAR(a.enemies[0]->GetComponent<Rigidbody3D>()->velocity.z, 0.0f, 1e-4f);   // not the first Enemy
        int fast = 0;
        for (const auto& up : a.s.Objects())
            if (up && up->name == "Enemy")
                if (auto* rb = up->GetComponent<Rigidbody3D>(); rb && std::fabs(rb->velocity.z - 9.0f) < 1e-4f) ++fast;
        CHECK(fast == 1);
        std::remove(prefab.c_str());
        CHECK(NoErrors("instantiate ref"));
    }

    // ---- 109: the 3D Rigidbody API through GetComponent<Rigidbody>() ----
    auto body = [](const std::string& src, int frames, Rigidbody3D*& rbOut, std::string& err) {
        auto s = std::make_unique<Scene>("Rb");
        GameObject* box = s->CreateGameObject("Box");
        auto* rb = box->AddComponent<Rigidbody3D>();
        rb->gravityScale = 0.0f;
        rb->freezeRotation = false;
        rb->allowSleep = false;
        auto* bc = box->AddComponent<BoxCollider3D>(); (void)bc;
        auto* sc = box->AddComponent<ScriptComponent>("okayscript");
        if (!sc->LoadSource(src, &err)) return s;
        s->Start();
        for (int i = 0; i < frames; ++i) s->Update(1.0f / 60.0f);
        rbOut = rb;
        return s;
    };
    {
        struct Case { const char* what; std::string src; int frames; };
        auto once = [](const std::string& body) {
            return "bool done; float got; Vector3 gv;\nvoid Update() { if (done) return; done = true;\n"
                   "  Rigidbody rb = GetComponent<Rigidbody>();\n  " + body + "\n}\n";
        };
        std::string err;
        Rigidbody3D* rb = nullptr;
        // AddForce with ForceMode.Impulse: an immediate kick (1 kg -> +5 m/s).
        g_errs.clear(); err.clear(); rb = nullptr;
        auto s1 = body(once("rb.AddForce(new Vector3(0, 5, 0), ForceMode.Impulse);"), 1, rb, err);
        CHECK(rb && std::fabs(rb->velocity.y - 5.0f) < 0.05f);
        if (rb) std::printf("  ForceMode.Impulse vy = %.3f\n", rb->velocity.y);
        // VelocityChange ignores mass.
        rb = nullptr;
        auto s2 = body(once("rb.mass = 4; rb.AddForce(new Vector3(0, 3, 0), ForceMode.VelocityChange);"), 1, rb, err);
        CHECK(rb && std::fabs(rb->velocity.y - 3.0f) < 0.05f);
        CHECK(rb && std::fabs(rb->mass - 4.0f) < 1e-4f);
        // AddImpulse.
        rb = nullptr;
        auto s3 = body(once("rb.mass = 2; rb.AddImpulse(new Vector3(4, 0, 0));"), 1, rb, err);
        CHECK(rb && std::fabs(rb->velocity.x - 2.0f) < 0.05f);
        // AddTorque spins it.
        rb = nullptr;
        auto s4 = body(once("rb.AddTorque(new Vector3(0, 30, 0));"), 4, rb, err);
        CHECK(rb && std::fabs(rb->angularVelocity.y) > 0.01f);
        if (rb) std::printf("  AddTorque wy = %.3f\n", rb->angularVelocity.y);
        // angularVelocity write + read back.
        rb = nullptr;
        auto s5 = body(once("rb.angularVelocity = new Vector3(0, 0, 2); gv = rb.angularVelocity;"), 1, rb, err);
        CHECK(rb && rb->angularVelocity.z > 1.5f);
        // useGravity / gravityScale / isKinematic / drag.
        rb = nullptr;
        auto s6 = body(once("rb.gravityScale = 3; rb.useGravity = false; rb.isKinematic = true; rb.drag = 0.5f; got = rb.mass;"), 1, rb, err);
        CHECK(rb && rb->gravityScale == 0.0f);
        CHECK(rb && rb->bodyType == Rigidbody3D::BodyType::Kinematic);
        CHECK(rb && std::fabs(rb->drag - 0.5f) < 1e-4f);
        // AddForceAtPosition off-centre spins and pushes.
        rb = nullptr;
        auto s7 = body(once("rb.AddForceAtPosition(new Vector3(0, 0, 60), transform.position + new Vector3(1, 0, 0));"), 4, rb, err);
        CHECK(rb && rb->velocity.z > 0.01f);
        CHECK(rb && rb->angularVelocity.Magnitude() > 0.01f);
        // AddExplosionForce pushes away from the centre.
        rb = nullptr;
        auto s8 = body(once("rb.AddExplosionForce(100, transform.position + new Vector3(-1, 0, 0), 5);"), 4, rb, err);
        CHECK(rb && rb->velocity.x > 0.01f);
        // AddRelativeForce in local space: turned 90 degrees about Y, local +Z is world +X.
        rb = nullptr;
        auto s9 = body(once("transform.eulerAngles = new Vector3(0, 90, 0); rb.AddRelativeForce(new Vector3(0, 0, 5), ForceMode.VelocityChange);"), 1, rb, err);
        CHECK(rb && std::fabs(rb->velocity.x - 5.0f) < 0.1f);
        if (rb) std::printf("  AddRelativeForce v = (%.2f, %.2f, %.2f)\n", rb->velocity.x, rb->velocity.y, rb->velocity.z);
        // Reading mass / useGravity / isKinematic back.
        {
            Scene s("Read"); GameObject* box = s.CreateGameObject("Box");
            auto* r = box->AddComponent<Rigidbody3D>(); r->mass = 6.0f; r->gravityScale = 1.0f;
            auto* sc = box->AddComponent<ScriptComponent>("okayscript");
            CHECK(sc->LoadSource("float m; bool g; bool k;\nvoid Start() { Rigidbody rb = GetComponent<Rigidbody>(); m = rb.mass; g = rb.useGravity; k = rb.isKinematic; }\n"));
            s.Start();
            CHECK_NEAR(sc->VM()->GetGlobal("m").AsFloat(), 6.0f, 1e-4f);
            CHECK(sc->VM()->GetGlobal("g").AsBool());
            CHECK(!sc->VM()->GetGlobal("k").AsBool());
        }
        // An unknown method is reported, not silently dropped.
        g_errs.clear();
        rb = nullptr;
        auto s10 = body(once("rb.Frobnicate(1);"), 1, rb, err);
        CHECK(!g_errs.empty());
        g_errs.clear();
    }
    // ---- 109 (addendum): 2D ForceMode2D.Impulse as a value, AddTorque, mass ----
    {
        Scene s("Rb2"); GameObject* box = s.CreateGameObject("Box2");
        auto* rb = box->AddComponent<Rigidbody2D>(); rb->gravityScale = 0.0f; rb->freezeRotation = false;
        auto* sc = box->AddComponent<ScriptComponent>("okayscript");
        CHECK(sc->LoadSource("bool done;\nvoid Update() { if (done) return; done = true;\n"
                             "  Rigidbody2D rb = GetComponent<Rigidbody2D>();\n"
                             "  rb.mass = 2; rb.AddForce(new Vector2(0, 8), ForceMode2D.Impulse); rb.angularVelocity = 1.5f;\n}\n"));
        s.Start(); s.Update(1.0f / 60.0f);
        CHECK_NEAR(rb->mass, 2.0f, 1e-4f);
        CHECK_NEAR(rb->velocity.y, 4.0f, 0.05f);
        CHECK(rb->angularVelocity > 1.0f);
    }

    // ---- 110: GameObject.Find / FindWithTag skip inactive objects ----
    {
        g_errs.clear();
        Scene s("Find");
        GameObject* hidden = s.CreateGameObject("Enemy"); hidden->tag = "Foe"; hidden->active = false;
        hidden->transform->localPosition = {1, 0, 0};
        GameObject* shown = s.CreateGameObject("Enemy"); shown->tag = "Foe";
        shown->transform->localPosition = {2, 0, 0};
        GameObject* door = s.CreateGameObject("Door"); door->tag = "DoorTag"; door->active = false;
        door->transform->localPosition = {3, 0, 0};
        GameObject* sw = s.CreateGameObject("Switch");
        auto* sc = sw->AddComponent<ScriptComponent>("okayscript");
        CHECK(sc->LoadSource("float fx; float tx; bool doorFound; bool doorTag; bool doorAny; float anyX;\n"
                             "void Start() {\n"
                             "  fx = GameObject.Find(\"Enemy\").transform.position.x;\n"
                             "  tx = GameObject.FindWithTag(\"Foe\").transform.position.x;\n"
                             "  doorFound = GameObject.Find(\"Door\") != null;\n"
                             "  doorTag = GameObject.FindWithTag(\"DoorTag\") != null;\n"
                             "  var d = GameObject.FindIncludingInactive(\"Door\");\n"
                             "  doorAny = d != null; anyX = d.transform.position.x;\n"
                             "}\n"));
        s.Start();
        CHECK_NEAR(sc->VM()->GetGlobal("fx").AsFloat(), 2.0f, 1e-4f);
        CHECK_NEAR(sc->VM()->GetGlobal("tx").AsFloat(), 2.0f, 1e-4f);
        CHECK(!sc->VM()->GetGlobal("doorFound").AsBool());
        CHECK(!sc->VM()->GetGlobal("doorTag").AsBool());
        CHECK(sc->VM()->GetGlobal("doorAny").AsBool());
        CHECK_NEAR(sc->VM()->GetGlobal("anyX").AsFloat(), 3.0f, 1e-4f);
        CHECK(NoErrors("find active"));
    }

    // ---- 111: other.GetComponent<T>() resolves on `other` (unique names) ----
    {
        g_errs.clear();
        Scene s("Knock");
        GameObject* target = s.CreateGameObject("Target");
        auto* trb = target->AddComponent<Rigidbody3D>(); trb->gravityScale = 0.0f;
        auto* tmr = target->AddComponent<MeshRenderer>(); tmr->color = Color{1, 1, 1, 1};
        GameObject* ball = s.CreateGameObject("Ball");
        auto* brb = ball->AddComponent<Rigidbody3D>(); brb->gravityScale = 0.0f;
        auto* bmr = ball->AddComponent<MeshRenderer>(); bmr->color = Color{1, 1, 1, 1};
        trb->velocity = {0, 0, 0}; brb->velocity = {0, -0.25f, 0};
        auto* sc = ball->AddComponent<ScriptComponent>("okayscript");
        CHECK(sc->LoadSource("float seen = -1; bool same;\n"
                             "void OnCollisionEnter(string other) {\n"
                             "  if (other == \"Target\") {\n"
                             "    seen = other.GetComponent<Rigidbody>().velocity.y;\n"
                             "    other.GetComponent<Rigidbody>().AddForce(new Vector3(6, 0, 0), ForceMode.VelocityChange);\n"
                             "    Rigidbody r = other.GetComponent<Rigidbody>(); r.angularVelocity = new Vector3(0, 1, 0);\n"
                             "    other.GetComponent<Renderer>().color = Color.red;\n"
                             "    same = GetComponent<Rigidbody>() == other.GetComponent<Rigidbody>();\n"
                             "  }\n"
                             "}\n"));
        s.Start();
        Collision3D c; c.gameObject = target;
        sc->OnCollisionEnter3D(c);
        CHECK_NEAR(sc->VM()->GetGlobal("seen").AsFloat(), 0.0f, 1e-4f);   // the target's, not the ball's -0.25
        CHECK_NEAR(trb->velocity.x, 6.0f, 1e-3f);
        CHECK_NEAR(brb->velocity.x, 0.0f, 1e-4f);
        CHECK_NEAR(trb->angularVelocity.y, 1.0f, 1e-4f);
        CHECK_NEAR(brb->angularVelocity.y, 0.0f, 1e-4f);
        CHECK_NEAR(tmr->color.g, 0.0f, 1e-4f);   // target turned red
        CHECK_NEAR(bmr->color.g, 1.0f, 1e-4f);   // the ball did not
        CHECK(!sc->VM()->GetGlobal("same").AsBool());
        CHECK(NoErrors("other.GetComponent"));
    }

    // ---- The shipped example scene, run through real physics ----
    {
        g_errs.clear();
        Scene s("Example");
        std::string err;
        const std::string path = std::string(OKAY_SOURCE_DIR) + "/examples/physics/script_object_refs.okayscene";
        CHECK(SceneSerializer::LoadFromFile(s, path, &err));
        if (!err.empty()) std::printf("  example load: %s\n", err.c_str());
        std::vector<GameObject*> enemies;
        GameObject* spinner = nullptr;
        for (const auto& up : s.Objects()) {
            if (up && up->name == "Enemy") enemies.push_back(up.get());
            if (up && up->name == "Spinner") spinner = up.get();
        }
        CHECK(enemies.size() == 3);
        s.Start();
        for (int i = 0; i < 240; ++i) s.Update(1.0f / 60.0f);   // 4 seconds: the ball falls about 3 m
        if (enemies.size() == 3) {
            const bool a0 = GameObject::FromInstanceId(enemies[0]->InstanceId()) != nullptr;
            const bool a1 = GameObject::FromInstanceId(enemies[1]->InstanceId()) != nullptr;
            const bool a2 = GameObject::FromInstanceId(enemies[2]->InstanceId()) != nullptr;
            std::printf("  example: enemies alive at x=-3/0/3: %d %d %d\n", a0, a1, a2);
            CHECK(a0); CHECK(a1); CHECK(!a2);   // the ball lands on the one at x = 3
        }
        CHECK(spinner != nullptr);
        if (spinner) {
            auto* rb = spinner->GetComponent<Rigidbody3D>();
            CHECK(rb && std::fabs(rb->angularVelocity.y) > 1.0f);
            CHECK(rb && rb->gravityScale == 0.0f);
            auto* sc = spinner->GetComponent<ScriptComponent>();
            CHECK(sc && !sc->VM()->GetGlobal("hiddenFound").AsBool());
            CHECK(sc && sc->VM()->GetGlobal("hiddenFoundAny").AsBool());
        }
        CHECK(NoErrors("example scene"));
    }

    TEST_MAIN_RESULT();
}
