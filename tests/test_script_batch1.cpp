#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Components/Stats.hpp"
#include "okay/Components/SurvivalStats.hpp"
#include "okay/Components/Light.hpp"
#include <string>
#include <vector>

using namespace okay;

// Batch 1 of script builtins: Stats / SurvivalStats access, hierarchy queries,
// Light setters, and the WaitUntil / WaitWhile coroutine waits.
static std::vector<std::string> g_errs;

static ScriptComponent* Load(GameObject* go, const std::string& src) {
    auto* sc = go->AddComponent<ScriptComponent>(); std::string e;
    if (!sc->LoadSource(src, &e)) { std::printf("  parse: %s\n", e.c_str()); return nullptr; }
    return sc;
}
static void Run(Scene& s, int frames) { for (int i = 0; i < frames; ++i) s.Update(1.0f / 60.0f); }

int main() {
    RUN_SUITE("script_batch1");
    Log::sink = [](Log::Level l, const std::string& m) { if (l >= Log::Level::Error) g_errs.push_back(m); };

    { // Stats: hurt / heal / add_xp / get_hp / is_dead
        Scene s; auto* hero = s.CreateGameObject("Hero"); auto* orc = s.CreateGameObject("Orc");
        auto* st = orc->AddComponent<Stats>(); st->defense = 0;
        auto* sc = Load(hero,
            "var a = 0\nvar b = 0\nvar c = 0\nvar d = 1\nvar lv = 0\nvar dead0 = 1\nvar dead1 = 0\n"
            "func start() {\n"
            "  hurt(\"Orc\", 30)\n a = get_hp(\"Orc\")\n"
            "  heal(\"Orc\", 10)\n b = get_hp(\"Orc\")\n"
            "  c = get_max_hp(\"Orc\")\n"
            "  add_xp(\"Orc\", 100)\n lv = get_hp(\"Orc\")\n"
            "  dead0 = is_dead(\"Orc\")\n"
            "  hurt(\"Orc\", 9999)\n dead1 = is_dead(\"Orc\")\n d = hurt(\"Nobody\", 1)\n}\n");
        CHECK(sc != nullptr);
        if (sc) {
            s.Start(); Run(s, 1);
            CHECK_NEAR(sc->VM()->GetGlobal("a").AsFloat(), 70.0f, 0.01f);
            CHECK_NEAR(sc->VM()->GetGlobal("b").AsFloat(), 80.0f, 0.01f);
            CHECK_NEAR(sc->VM()->GetGlobal("c").AsFloat(), 100.0f, 0.01f);
            CHECK(st->level == 2);
            CHECK_NEAR(sc->VM()->GetGlobal("lv").AsFloat(), 120.0f, 0.01f);   // level-up refilled to the raised max
            CHECK_NEAR(sc->VM()->GetGlobal("dead0").AsFloat(), 0.0f, 0.01f);
            CHECK_NEAR(sc->VM()->GetGlobal("dead1").AsFloat(), 1.0f, 0.01f);
            CHECK(!sc->VM()->GetGlobal("d").AsBool());
        }
    }
    { // SurvivalStats
        Scene s; auto* hero = s.CreateGameObject("Hero"); auto* t = s.CreateGameObject("Tank");
        auto* sv = t->AddComponent<SurvivalStats>(); sv->armor = 0; sv->publishPrefs = false; sv->publishBars = false;
        auto* sc = Load(hero, "var dd = 1\nfunc start() { hurt(\"Tank\", 40)\n heal(\"Tank\", 5)\n dd = is_dead(\"Tank\") }\n");
        CHECK(sc != nullptr);
        if (sc) { s.Start(); Run(s, 1); CHECK_NEAR(sv->health, 65.0f, 0.01f); CHECK_NEAR(sc->VM()->GetGlobal("dd").AsFloat(), 0.0f, 0.01f); }
    }
    { // Hierarchy
        Scene s; auto* hero = s.CreateGameObject("Hero"); auto* ship = s.CreateGameObject("Ship");
        auto* g1 = s.CreateGameObject("Gun1"); auto* g2 = s.CreateGameObject("Gun2");
        g1->transform->SetParent(ship->transform, true); g2->transform->SetParent(ship->transform, true);
        auto* sc = Load(hero,
            "var n = 0\nvar c0 = \"\"\nvar c1 = \"\"\nvar c2 = \"x\"\nvar p = \"\"\nvar p0 = \"x\"\n"
            "func start() {\n n = child_count(\"Ship\")\n c0 = get_child(\"Ship\", 0)\n c1 = get_child(\"Ship\", 1)\n"
            " c2 = get_child(\"Ship\", 2)\n p = parent_of(\"Gun2\")\n p0 = parent_of(\"Ship\")\n}\n");
        CHECK(sc != nullptr);
        if (sc) {
            s.Start(); Run(s, 1);
            CHECK_NEAR(sc->VM()->GetGlobal("n").AsFloat(), 2.0f, 0.01f);
            CHECK(sc->VM()->GetGlobal("c0").AsString() == "Gun1");
            CHECK(sc->VM()->GetGlobal("c1").AsString() == "Gun2");
            CHECK(sc->VM()->GetGlobal("c2").AsString().empty());
            CHECK(sc->VM()->GetGlobal("p").AsString() == "Ship");
            CHECK(sc->VM()->GetGlobal("p0").AsString().empty());
        }
    }
    { // Light setters
        Scene s; auto* hero = s.CreateGameObject("Hero"); auto* lamp = s.CreateGameObject("Lamp");
        auto* l = lamp->AddComponent<Light>();
        auto* sc = Load(hero,
            "func start() {\n set_light_intensity(\"Lamp\", 2.5)\n set_light_range(\"Lamp\", 33)\n"
            " set_light_angle(\"Lamp\", 60)\n set_light_color(\"Lamp\", 1, 0.5, 0.25)\n}\n");
        CHECK(sc != nullptr);
        if (sc) {
            s.Start(); Run(s, 1);
            CHECK_NEAR(l->intensity, 2.5f, 0.001f); CHECK_NEAR(l->range, 33.0f, 0.001f);
            CHECK_NEAR(l->spotAngle, 60.0f, 0.001f);
            CHECK_NEAR(l->color.g, 0.5f, 0.001f); CHECK_NEAR(l->color.b, 0.25f, 0.001f);
        }
    }
    { // WaitUntil / WaitWhile
        Scene s; auto* hero = s.CreateGameObject("Hero");
        auto* sc = Load(hero,
            "using System.Collections;\npublic class P : Component {\n"
            "  bool gate; bool wdone; bool udone; bool early; bool wgate; int frames;\n"
            "  IEnumerator U() { yield return new WaitUntil(() => gate); udone = true; }\n"
            "  IEnumerator W() { yield return new WaitWhile(() => !wgate); wdone = true; }\n"
            "  void Start() { StartCoroutine(U()); StartCoroutine(W()); }\n"
            "  void Update() { frames++; if (frames == 5) { early = udone || wdone; gate = true; wgate = true; } }\n"
            "}\n");
        CHECK(sc != nullptr);
        if (sc) {
            s.Start(); Run(s, 4);
            CHECK(!sc->VM()->GetGlobal("udone").AsBool());
            Run(s, 6);
            CHECK(!sc->VM()->GetGlobal("early").AsBool());
            CHECK(sc->VM()->GetGlobal("udone").AsBool());
            CHECK(sc->VM()->GetGlobal("wdone").AsBool());
        }
    }
    CHECK(g_errs.empty());
    for (auto& m : g_errs) std::printf("  err: %s\n", m.c_str());
    TEST_MAIN_RESULT();
}
