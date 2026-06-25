#include "test_framework.hpp"
#include <Okay.hpp>
#include <cmath>
using namespace okay;

static float Dist2D(const Vec3& a, const Vec3& b) {
    float dx = a.x - b.x, dz = a.z - b.z; return std::sqrt(dx * dx + dz * dz);
}

// NPCController steering (chase/flee/wander + bite) and recipe Crafting on an Inventory.
int main() {
    RUN_SUITE("npc_crafting");

    // --- Chase: NPC closes on the player ---------------------------------
    {
        Scene s("chase");
        GameObject* p = s.CreateGameObject("Player");
        p->transform->SetPosition({12, 0, 0});
        auto* npc = s.CreateGameObject("Wolf");
        auto* n = npc->AddComponent<NPCController>();   // no Rigidbody -> moves the Transform
        n->behavior = (int)NPCController::Behavior::Chase;
        n->moveSpeed = 4.0f; n->sightRange = 30.0f;
        s.Start();
        float d0 = Dist2D(npc->transform->Position(), p->transform->Position());
        for (int i = 0; i < 60; ++i) s.Update(1.0f / 60.0f);
        float d1 = Dist2D(npc->transform->Position(), p->transform->Position());
        CHECK(d1 < d0 - 2.0f);                          // moved toward the player
    }

    // --- Chase bite: damages the player's health within range ------------
    {
        Scene s("bite");
        GameObject* p = s.CreateGameObject("Player");
        p->transform->SetPosition({1.0f, 0, 0});        // inside attackRange
        auto* hp = p->AddComponent<HealthStat>(); hp->regenPerSecond = 0.0f;
        auto* npc = s.CreateGameObject("Wolf");
        auto* n = npc->AddComponent<NPCController>();
        n->behavior = (int)NPCController::Behavior::Chase;
        n->attackRange = 1.5f; n->attackDamage = 10.0f; n->attackInterval = 0.5f;
        s.Start();
        for (int i = 0; i < 90; ++i) s.Update(1.0f / 60.0f);   // 1.5s -> a few bites
        CHECK(hp->health < 100.0f);
        CHECK(hp->health <= 80.0f);                     // at least 2 bites landed
    }

    // --- Flee: prey runs away -------------------------------------------
    {
        Scene s("flee");
        GameObject* p = s.CreateGameObject("Player");
        p->transform->SetPosition({0, 0, 0});
        auto* npc = s.CreateGameObject("Rabbit");
        npc->transform->SetPosition({2, 0, 0});
        auto* n = npc->AddComponent<NPCController>();
        n->behavior = (int)NPCController::Behavior::Flee;
        n->moveSpeed = 4.0f; n->sightRange = 30.0f;
        s.Start();
        float d0 = Dist2D(npc->transform->Position(), p->transform->Position());
        for (int i = 0; i < 60; ++i) s.Update(1.0f / 60.0f);
        CHECK(Dist2D(npc->transform->Position(), p->transform->Position()) > d0 + 2.0f);
    }

    // --- Wander: roams from its spawn -----------------------------------
    {
        Scene s("wander");
        auto* npc = s.CreateGameObject("Deer");
        npc->transform->SetPosition({5, 0, 5});
        auto* n = npc->AddComponent<NPCController>();
        n->behavior = (int)NPCController::Behavior::Wander;
        n->moveSpeed = 3.0f; n->wanderRadius = 8.0f; n->targetName = "nobody";
        Vec3 home = npc->transform->Position();
        s.Start();
        for (int i = 0; i < 120; ++i) s.Update(1.0f / 60.0f);
        CHECK(Dist2D(npc->transform->Position(), home) > 0.5f);   // it moved
    }

    // --- Crafting: consume inputs, produce output ------------------------
    {
        Scene s("craft");
        GameObject* p = s.CreateGameObject("Player");
        auto* inv = p->AddComponent<Inventory>();
        auto* cr = p->AddComponent<Crafting>();
        cr->AddRecipe("torch", 1, {{"wood", 1}, {"cloth", 2}});
        s.Start();
        CHECK(!cr->CanCraft("torch"));                  // nothing in the bag
        CHECK(!cr->Craft("torch"));
        inv->Add("wood", 2); inv->Add("cloth", 3);
        CHECK(cr->CanCraft("torch"));
        CHECK(cr->Craft("torch"));
        CHECK(inv->Count("wood") == 1);                 // 1 wood consumed
        CHECK(inv->Count("cloth") == 1);                // 2 cloth consumed
        CHECK(inv->Count("torch") == 1);                // output added
        CHECK(!cr->Craft("torch"));                     // not enough cloth now
        CHECK(inv->Count("cloth") == 1);                // unchanged on failure
        CHECK(cr->CraftIndex(0) == false);              // (still short)
    }

    // --- Serialization round-trip (NPC + Crafting) ----------------------
    {
        Scene s("ser");
        GameObject* g = s.CreateGameObject("Goblin");
        auto* n = g->AddComponent<NPCController>();
        n->behavior = (int)NPCController::Behavior::Chase; n->moveSpeed = 5.5f;
        n->attackDamage = 13.0f; n->targetName = "Hero";
        g->AddComponent<Inventory>();
        auto* cr = g->AddComponent<Crafting>();
        cr->AddRecipe("axe", 1, {{"wood", 2}, {"stone", 3}});
        std::string txt = SceneSerializer::SerializeObject(*g);
        CHECK(txt.find("npc ") != std::string::npos);
        CHECK(txt.find("crafting ") != std::string::npos);
        Scene s2("ser2");
        GameObject* c2 = SceneSerializer::InstantiateFromText(s2, txt);
        auto* n2 = c2 ? c2->GetComponent<NPCController>() : nullptr;
        auto* cr2 = c2 ? c2->GetComponent<Crafting>() : nullptr;
        CHECK(n2 != nullptr);
        CHECK(n2->behavior == (int)NPCController::Behavior::Chase);
        CHECK_NEAR(n2->moveSpeed, 5.5f, 1e-3f);
        CHECK_NEAR(n2->attackDamage, 13.0f, 1e-3f);
        CHECK(n2->targetName == "Hero");
        CHECK(cr2 != nullptr);
        CHECK(cr2->recipes.size() == 1);
        CHECK(cr2->recipes[0].output == "axe");
        CHECK(cr2->recipes[0].inputs.size() == 2);
        CHECK(cr2->recipes[0].inputs[1].item == "stone");
        CHECK(cr2->recipes[0].inputs[1].count == 3);
    }

    // --- NPC takes damage and dies --------------------------------------
    {
        Scene s("npchp");
        auto* npc = s.CreateGameObject("Wolf");
        auto* n = npc->AddComponent<NPCController>();
        n->maxHealth = 20.0f;
        s.Start();
        n->Damage(8.0f);
        CHECK_NEAR(n->health, 12.0f, 1e-3f);
        CHECK(!n->IsDead());
        n->Damage(100.0f);
        CHECK(n->IsDead());
        s.Update(1.0f / 60.0f);                          // flush destroy
        CHECK(s.Find("Wolf") == nullptr);
    }

    // --- Melee attacker hits NPCs in the cone in front ------------------
    {
        Scene s("melee");
        GameObject* p = s.CreateGameObject("Player");      // identity rotation -> faces +Z
        auto* atk = p->AddComponent<MeleeAttacker>();
        atk->damage = 15.0f; atk->range = 3.0f; atk->arc = 120.0f;
        GameObject* front = s.CreateGameObject("Front");
        front->transform->SetPosition({0, 0, 2});          // ahead, in range
        auto* nf = front->AddComponent<NPCController>(); nf->maxHealth = 100.0f;
        GameObject* behind = s.CreateGameObject("Behind");
        behind->transform->SetPosition({0, 0, -2});        // behind, outside the cone
        auto* nb = behind->AddComponent<NPCController>(); nb->maxHealth = 100.0f;
        s.Start();
        atk->Swing();
        CHECK_NEAR(nf->health, 85.0f, 1e-3f);              // hit
        CHECK_NEAR(nb->health, 100.0f, 1e-3f);             // missed (behind)
    }

    // --- Spawner clones a template up to maxAlive -----------------------
    {
        Scene s("spawn");
        GameObject* tmpl = s.CreateGameObject("Wolf");
        tmpl->AddComponent<NPCController>();
        auto* sp = s.CreateGameObject("Den")->AddComponent<Spawner>();
        sp->templateName = "Wolf"; sp->maxAlive = 3; sp->interval = 0.1f; sp->startDelay = 0.0f;
        s.Start();
        CHECK(sp->SpawnOne() != nullptr);
        CHECK(sp->SpawnOne() != nullptr);
        CHECK(sp->AliveCount() == 2);
        for (int i = 0; i < 120; ++i) s.Update(1.0f / 60.0f);   // keeps spawning, capped
        CHECK(sp->AliveCount() <= 3);                      // never exceeds maxAlive
        CHECK(sp->AliveCount() == 3);
    }

    // --- Crafting menu auto-builds a button per recipe ------------------
    {
        Scene s("cmenu");
        GameObject* p = s.CreateGameObject("Player");
        p->AddComponent<Inventory>();
        auto* cr = p->AddComponent<Crafting>();
        cr->AddRecipe("torch", 1, {{"wood", 1}});
        cr->AddRecipe("axe", 1, {{"wood", 2}});
        auto* cm = p->AddComponent<CraftingMenu>(); cm->open = true;
        s.Start();
        s.Update(1.0f / 60.0f);
        auto btns = s.FindObjectsOfType<UIButton>();
        CHECK(btns.size() == 2);
        bool craftWired = false;
        for (auto* b : btns) if (b->clickFunction == "Craft" && b->clickTarget == "Player") craftWired = true;
        CHECK(craftWired);
    }

    // --- Serialization: melee / spawner / craftmenu / npc health --------
    {
        Scene s("ser2");
        GameObject* g = s.CreateGameObject("Hero");
        auto* n = g->AddComponent<NPCController>(); n->maxHealth = 55.0f; n->invulnerable = true;
        auto* m = g->AddComponent<MeleeAttacker>(); m->damage = 22.0f; m->arc = 90.0f; m->attackKey = 'g';
        auto* sp = g->AddComponent<Spawner>(); sp->templateName = "Mob"; sp->maxAlive = 7; sp->totalToSpawn = 20;
        auto* cm = g->AddComponent<CraftingMenu>(); cm->toggleKey = 'b'; cm->open = true;
        std::string txt = SceneSerializer::SerializeObject(*g);
        CHECK(txt.find("melee ") != std::string::npos);
        CHECK(txt.find("spawner ") != std::string::npos);
        CHECK(txt.find("craftmenu ") != std::string::npos);
        Scene s2("ser2b");
        GameObject* c2 = SceneSerializer::InstantiateFromText(s2, txt);
        CHECK(c2 != nullptr);
        CHECK_NEAR(c2->GetComponent<NPCController>()->maxHealth, 55.0f, 1e-3f);
        CHECK(c2->GetComponent<NPCController>()->invulnerable);
        CHECK_NEAR(c2->GetComponent<MeleeAttacker>()->damage, 22.0f, 1e-3f);
        CHECK(c2->GetComponent<MeleeAttacker>()->attackKey == 'g');
        CHECK(c2->GetComponent<Spawner>()->templateName == "Mob");
        CHECK(c2->GetComponent<Spawner>()->totalToSpawn == 20);
        CHECK(c2->GetComponent<CraftingMenu>()->toggleKey == 'b');
        CHECK(c2->GetComponent<CraftingMenu>()->open);
    }

    TEST_MAIN_RESULT();
}
