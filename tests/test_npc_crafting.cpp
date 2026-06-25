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

    TEST_MAIN_RESULT();
}
