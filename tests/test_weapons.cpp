// Weapon definitions: a melee swing lands only inside its window and arc (or hit
// box), a gun waits out its fire rate and its reload and spends rounds, and both
// apply damage and knockback. The scene is a real Scene, the same one the player runs.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Combat/WeaponDefinition.hpp"
#include "okay/Components/Equipment.hpp"
#include "okay/Components/MeleeAttacker.hpp"
#include "okay/Components/RangedWeapon.hpp"
#include "okay/Components/ScriptComponent.hpp"
#include "okay/Physics/Collider3D.hpp"
#include "okay/Physics/Rigidbody3D.hpp"
#include "okay/VisualScript/NodeGraph.hpp"
#include <fstream>
#include <sstream>
#include <string>

using namespace okay;

static const char* kBook =
    "okayweapon 1\n"
    "weapon \"sword\"\n"
    "  name \"Sword\"\n"
    "  kind melee\n"
    "  damage 10\n"
    "  swing 0.8\n"
    "  range 3\n"
    "  arc 80\n"
    "  hit 0.25 0.45\n"
    "  knockback 4\n"
    "  combo 3 0.7 0.5\n"
    "end\n"
    "weapon \"pistol\"\n"
    "  name \"Pistol\"\n"
    "  kind gun\n"
    "  damage 9\n"
    "  fire 4\n"
    "  range 20\n"
    "  magazine 2\n"
    "  ammo 6\n"
    "  reload 0.3\n"
    "  knockback 5\n"
    "  hitscan 1\n"
    "end\n"
    "weapon \"dagger\"\n"
    "  name \"Dagger\"\n"
    "  kind melee\n"
    "  item \"dagger\"\n"
    "  damage 7\n"
    "  swing 0\n"
    "  range 3\n"
    "  arc 120\n"
    "end\n";

static HealthStat* Hurtbox(Scene& s, const char* name, const Vec3& at, bool body) {
    GameObject* g = s.CreateGameObject(name);
    g->transform->SetPosition(at);
    auto* hp = g->AddComponent<HealthStat>();
    hp->maxHealth = 100.0f;
    hp->health = 100.0f;
    auto* box = g->AddComponent<BoxCollider3D>();
    box->size = {1.0f, 1.0f, 1.0f};
    if (body) {
        auto* rb = g->AddComponent<Rigidbody3D>();
        rb->mass = 1.0f;
        rb->gravityScale = 0.0f;
        rb->drag = 0.0f;
    }
    return hp;
}

int main() {
    RUN_SUITE("weapons");
    Log::console = false;
    std::string err;
    CHECK(WeaponBook::Get().LoadText(kBook, &err));
    CHECK(err.empty());

    // The file keeps every field, and says what is wrong in plain English.
    {
        WeaponBook book;
        CHECK(book.LoadText(WeaponBook::Get().Serialize(), &err));
        const WeaponDefinition* sword = book.Find("sword");
        const WeaponDefinition* pistol = book.Find("pistol");
        CHECK(sword && pistol);
        if (sword) {
            CHECK(sword->kind == WeaponKind::Melee);
            CHECK_NEAR(sword->damage, 10.0f, 1e-3f);
            CHECK_NEAR(sword->swingTime, 0.8f, 1e-3f);
            CHECK_NEAR(sword->hitStart, 0.25f, 1e-3f);
            CHECK_NEAR(sword->hitEnd, 0.45f, 1e-3f);
            CHECK_NEAR(sword->arc, 80.0f, 1e-3f);
            CHECK_NEAR(sword->knockback, 4.0f, 1e-3f);
            CHECK(sword->combo == 3);
            CHECK_NEAR(sword->comboScale, 0.5f, 1e-3f);
        }
        if (pistol) {
            CHECK(pistol->kind == WeaponKind::Gun);
            CHECK_NEAR(pistol->fireRate, 4.0f, 1e-3f);
            CHECK(pistol->magazine == 2);
            CHECK(pistol->ammo == 6);
            CHECK_NEAR(pistol->reload, 0.3f, 1e-3f);
            CHECK(pistol->hitscan);
        }
        WeaponBook bad;
        CHECK(bad.LoadText(
            "okayweapon 1\n"
            "weapon \"a\"\n  kind melee\n  swing 0.5\n  hit 0.4 0.2\nend\n"
            "weapon \"a\"\n  kind gun\n  fire 0\nend\n", &err));
        const std::vector<std::string> issues = bad.Issues();
        CHECK(issues.size() >= 2);
        bool dup = false, window = false, rate = false;
        for (const auto& line : issues) {
            if (line.find("more than one weapon") != std::string::npos) dup = true;
            if (line.find("hit window") != std::string::npos) window = true;
            if (line.find("shots per second") != std::string::npos) rate = true;
        }
        CHECK(dup && window && rate);
    }

    // A swing hits only inside the window and the arc, and the push matches the knockback.
    {
        Scene s("swing");
        GameObject* fighter = s.CreateGameObject("Fighter");
        auto* melee = fighter->AddComponent<MeleeAttacker>();
        melee->weapon = "sword";
        melee->useMouse = false;
        HealthStat* front = Hurtbox(s, "Crate", {0, 0, 1.5f}, true);
        HealthStat* side = Hurtbox(s, "Side", {2.0f, 0, 0.2f}, false);
        GameObject* behind = s.CreateGameObject("Behind");
        behind->transform->SetPosition({0, 0, -1.5f});
        auto* npc = behind->AddComponent<NPCController>();
        npc->maxHealth = 40.0f;
        GameObject* ahead = s.CreateGameObject("Guard");
        ahead->transform->SetPosition({0, 0, 2.0f});
        auto* guard = ahead->AddComponent<NPCController>();
        guard->maxHealth = 40.0f;
        s.Start();
        melee->Swing();
        s.Update(0.20f);                                   // still before the window
        CHECK_NEAR(front->health, 100.0f, 1e-3f);
        CHECK_NEAR(guard->health, 40.0f, 1e-3f);
        s.Update(0.10f);                                   // age 0.30, window is 0.25..0.45
        CHECK_NEAR(front->health, 90.0f, 1e-3f);
        CHECK_NEAR(side->health, 100.0f, 1e-3f);           // outside the arc
        CHECK_NEAR(npc->health, 40.0f, 1e-3f);             // behind the fighter
        CHECK_NEAR(guard->health, 30.0f, 1e-3f);           // an NPC in front is hit too
        CHECK(front->gameObject->GetComponent<Rigidbody3D>()->velocity.z > 3.5f);
        front->gameObject->GetComponent<Rigidbody3D>()->velocity = Vec3::Zero;
        front->gameObject->transform->SetPosition({0, 0, 1.5f});
        const float after = front->health;
        s.Update(0.30f);                                   // age 0.60, the window has closed
        CHECK_NEAR(front->health, after, 1e-3f);
        s.Update(0.25f);                                   // the swing ends and the combo window opens
        CHECK(!melee->Swinging());
        melee->Swing();
        s.Update(0.30f);
        CHECK(melee->ComboStep() == 1);
        CHECK_NEAR(front->health, after - 15.0f, 1e-3f);   // 10 * (1 + 0.5)
    }

    // A hit box connects in front of the fighter and misses beside it.
    {
        Scene s("box");
        auto* melee = s.CreateGameObject("Fighter")->AddComponent<MeleeAttacker>();
        melee->useHitbox = true;
        melee->hitboxWidth = 1.0f;
        melee->hitboxHeight = 2.0f;
        melee->hitboxDepth = 2.0f;
        melee->swingTime = 0.0f;
        melee->damage = 5.0f;
        HealthStat* inside = Hurtbox(s, "Inside", {0, 0, 1.0f}, false);
        HealthStat* outside = Hurtbox(s, "Outside", {2.0f, 0, 1.0f}, false);
        s.Start();
        melee->Swing();
        CHECK_NEAR(inside->health, 95.0f, 1e-3f);
        CHECK_NEAR(outside->health, 100.0f, 1e-3f);
    }

    // Holding the dagger item selects that weapon without typing its name.
    {
        ItemDatabase::Get().Clear();
        ItemDefinition item;
        item.id = "dagger";
        item.equip = EquipSlot::MainHand;
        ItemDatabase::Get().Put(item);
        WeaponBook::Get().LoadText(kBook, &err);
        Scene s("held");
        GameObject* fighter = s.CreateGameObject("Fighter");
        auto* eq = fighter->AddComponent<Equipment>();
        const int slot = eq->Find("Main Hand");
        CHECK(slot >= 0);
        CHECK(eq->Set(slot, "dagger"));
        auto* melee = fighter->AddComponent<MeleeAttacker>();
        melee->damage = 1.0f;                             // the dagger's 7 replaces this
        HealthStat* dummy = Hurtbox(s, "Dummy", {0, 0, 1.0f}, false);
        melee->Swing();
        CHECK_NEAR(dummy->health, 93.0f, 1e-3f);
    }

    // The gun waits for its fire rate, spends rounds, and reloads before it fires again.
    {
        WeaponBook::Get().LoadText(kBook, &err);
        Scene s("gun");
        GameObject* shooter = s.CreateGameObject("Shooter");
        auto* gun = shooter->AddComponent<RangedWeapon>();
        gun->weapon = "pistol";
        gun->fireButton = -1;
        gun->fireKey = 0;
        HealthStat* target = Hurtbox(s, "Target", {0, 0, 4.0f}, true);
        HealthStat* far = Hurtbox(s, "Far", {0, 0, 40.0f}, false);
        s.Start();
        CHECK(gun->Rounds() == 2);
        CHECK(gun->Spare() == 6);
        CHECK(gun->Fire());
        CHECK_NEAR(target->health, 91.0f, 1e-3f);
        CHECK_NEAR(far->health, 100.0f, 1e-3f);            // past the gun's range
        CHECK(target->gameObject->GetComponent<Rigidbody3D>()->velocity.z > 4.5f);
        CHECK(gun->Rounds() == 1);
        CHECK(!gun->Fire());                               // the fire rate has not elapsed
        CHECK(gun->Rounds() == 1);
        s.Update(0.24f);
        CHECK(!gun->Fire());
        s.Update(0.02f);                                   // 0.26s > 1/4s
        CHECK(gun->Fire());
        CHECK(gun->Rounds() == 0);
        CHECK(gun->IsReloading());
        CHECK(!gun->Fire());
        s.Update(0.29f);
        CHECK(gun->IsReloading());
        s.Update(0.02f);                                   // the 0.3s reload has finished
        CHECK(!gun->IsReloading());
        CHECK(gun->Rounds() == 2);
        CHECK(gun->Spare() == 4);
        CHECK(gun->Fire());
        CHECK(gun->Rounds() == 1);
        CHECK_NEAR(target->health, 91.0f - 18.0f, 1e-3f);  // two more shots of 9
    }

    // What the scene file keeps.
    {
        Scene s("save");
        GameObject* g = s.CreateGameObject("Hero");
        auto* m = g->AddComponent<MeleeAttacker>();
        m->weapon = "sword";
        m->swingTime = 0.4f;
        m->hitStart = 0.1f;
        m->hitEnd = 0.3f;
        m->knockback = 2.5f;
        auto* gun = g->AddComponent<RangedWeapon>();
        gun->weapon = "pistol";
        gun->magazine = 6;
        gun->reserve = 18;
        gun->fireRate = 3.0f;
        const std::string text = SceneSerializer::Serialize(s);
        Scene back("back");
        CHECK(SceneSerializer::Deserialize(back, text));
        GameObject* hero = back.Find("Hero");
        CHECK(hero);
        if (hero) {
            CHECK(hero->GetComponent<MeleeAttacker>()->weapon == "sword");
            CHECK_NEAR(hero->GetComponent<MeleeAttacker>()->hitEnd, 0.3f, 1e-3f);
            CHECK(hero->GetComponent<RangedWeapon>()->weapon == "pistol");
            CHECK(hero->GetComponent<RangedWeapon>()->magazine == 6);
            CHECK(hero->GetComponent<RangedWeapon>()->reserve == 18);
        }
    }

    // Script builtins and the visual-script nodes call the same components.
    {
        WeaponBook::Get().LoadText(kBook, &err);
        Scene s("script");
        GameObject* hero = s.CreateGameObject("Hero");
        hero->AddComponent<MeleeAttacker>()->weapon = "dagger";
        auto* gun = hero->AddComponent<RangedWeapon>();
        gun->weapon = "pistol";
        gun->fireButton = -1;
        auto* sc = hero->AddComponent<ScriptComponent>("okayscript");
        CHECK(sc->LoadSource(
            "var swung = 0; var fired = 0; var ammo = -1; var set = 0;\n"
            "function start() {\n"
            "  swung = weapon_swing();\n"
            "  fired = weapon_fire();\n"
            "  ammo = weapon_ammo();\n"
            "  set = weapon_set(\"dagger\");\n"
            "}\n", &err));
        s.Start();
        CHECK(sc->VM()->GetGlobal("swung").AsFloat() == 1.0f);
        CHECK(sc->VM()->GetGlobal("fired").AsFloat() == 1.0f);
        CHECK(sc->VM()->GetGlobal("ammo").AsFloat() == 1.0f);
        CHECK(sc->VM()->GetGlobal("set").AsFloat() == 1.0f);
        CHECK(gun->weapon == "dagger");
        vs::VsContext ctx;
        ctx.gameObject = hero;
        vs::NodeGraph graph;
        std::unique_ptr<vs::VsNode> reload = vs::MakeNode("ReloadWeapon", {}, &err);
        std::unique_ptr<vs::VsNode> ammo = vs::MakeNode("WeaponAmmo", {}, &err);
        CHECK(reload && ammo);
        if (reload) reload->Exec(graph, ctx);
        CHECK(gun->IsReloading() || gun->Rounds() == 2);
        if (ammo) CHECK(ammo->Eval(0, graph, ctx).AsFloat() >= 0.0f);
    }

    // The example scene is a real scene that names the sample weapons.
    {
        const std::string root = std::string(OKAY_SOURCE_DIR) + "/examples/weapons/";
        std::ifstream sceneFile(root + "arena.okayscene");
        std::stringstream sceneText;
        sceneText << sceneFile.rdbuf();
        CHECK(sceneFile.good());
        Scene arena("arena");
        std::string loadErr;
        CHECK(SceneSerializer::Deserialize(arena, sceneText.str(), &loadErr));
        if (!loadErr.empty()) std::cerr << "    " << loadErr << "\n";
        GameObject* fighter = arena.Find("Fighter");
        GameObject* gun = arena.Find("Gun");
        GameObject* dummy = arena.Find("Dummy");
        CHECK(fighter && gun && dummy);
        if (fighter && gun && dummy) {
            CHECK(fighter->GetComponent<MeleeAttacker>()->weapon == "sword");
            CHECK(gun->GetComponent<RangedWeapon>()->weapon == "pistol");
            CHECK(WeaponBook::Get().Load(root + "weapons.okayweapon", &err));
            arena.Start();
            fighter->GetComponent<MeleeAttacker>()->Swing();
            arena.Update(0.20f);
            auto* hp = dummy->GetComponent<HealthStat>();
            CHECK(hp && hp->health == 100.0f);            // the sample sword's window is not open yet
            arena.Update(0.15f);
            CHECK(hp && hp->health < 100.0f);
        }
    }

    TEST_MAIN_RESULT();
}
