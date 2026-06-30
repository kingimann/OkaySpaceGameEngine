#include "test_framework.hpp"
#include <Okay.hpp>
#include <cmath>

using namespace okay;

// The smarter NPC brain: perception (sight cone + lose-sight + search), patrol
// waypoints, flee-at-low-health, provocation, leash, and state reporting.
int main() {
    RUN_SUITE("npc_ai");

    auto place = [](Scene& s, const char* name, Vec3 p) {
        GameObject* g = s.CreateGameObject(name);
        g->transform->localPosition = p;
        return g;
    };

    // ---- Chase: acquires a visible target, then SEARCHES after losing sight ----
    {
        Scene s("chase");
        GameObject* player = place(s, "Player", {0, 0, 0});
        GameObject* npc = place(s, "Enemy", {3, 0, 0});
        auto* ai = npc->AddComponent<NPCController>();
        ai->behavior = (int)NPCController::Behavior::Chase;
        ai->fieldOfView = 360.0f;           // see all around (no facing needed)
        ai->sightRange = 10.0f;
        ai->loseSightTime = 0.5f; ai->searchTime = 2.0f;
        ai->turnSpeed = 0.0f;               // snap-face so it heads straight in
        s.Start();
        s.Update(0.1f);
        CHECK(ai->IsAlerted());                          // spotted the player
        CHECK(std::string(ai->StateName()) == "Chase");
        float d0 = std::hypot(npc->transform->Position().x, npc->transform->Position().z);

        // Move the player far away (out of sight) and let sight lapse -> Search.
        player->transform->localPosition = {100, 0, 100};
        for (int i = 0; i < 10; ++i) s.Update(0.1f);     // > loseSightTime
        CHECK(std::string(ai->StateName()) == "Search");
        // It closed distance toward where the player was while chasing.
        CHECK(std::hypot(npc->transform->Position().x, npc->transform->Position().z) <= d0 + 0.01f);
    }

    // ---- Field of view: a target behind the NPC is NOT seen ----
    {
        Scene s("fov");
        place(s, "Player", {0, 0, -5});                  // directly behind (NPC faces +Z by default? test the cone)
        GameObject* npc = place(s, "Enemy", {0, 0, 0});
        npc->transform->localRotation = Quat::LookRotation({0, 0, 1});   // face +Z
        auto* ai = npc->AddComponent<NPCController>();
        ai->behavior = (int)NPCController::Behavior::Chase;
        ai->fieldOfView = 90.0f; ai->sightRange = 10.0f;
        s.Start();
        s.Update(0.1f);
        CHECK(!ai->IsAlerted());                         // player is behind the 90-deg cone
    }

    // ---- Patrol: walks toward its first waypoint ----
    {
        Scene s("patrol");
        GameObject* npc = place(s, "Guard", {0, 0, 0});
        auto* ai = npc->AddComponent<NPCController>();
        ai->behavior = (int)NPCController::Behavior::Patrol;
        ai->targetName = "nobody";
        ai->turnSpeed = 0.0f; ai->moveSpeed = 2.0f;
        ai->waypoints = { {10, 0, 0} };
        s.Start();
        for (int i = 0; i < 10; ++i) s.Update(0.1f);
        CHECK(npc->transform->Position().x > 0.5f);      // headed toward the waypoint
    }

    // ---- Flee at low health ----
    {
        Scene s("flee");
        place(s, "Player", {1, 0, 0});
        GameObject* npc = place(s, "Prey", {0, 0, 0});
        auto* ai = npc->AddComponent<NPCController>();
        ai->behavior = (int)NPCController::Behavior::Wander;
        ai->fieldOfView = 360.0f; ai->sightRange = 10.0f;
        ai->fleeHealthPct = 0.5f; ai->maxHealth = 100; ai->turnSpeed = 0.0f;
        s.Start();
        ai->Damage(60);                                  // drop to 40% -> should flee when aware
        s.Update(0.1f);
        CHECK(std::string(ai->StateName()) == "Flee");
        CHECK(npc->transform->Position().x < 0.0f);      // moved away from the player at +X
    }

    // ---- Provocation: a passive NPC fights back after being hit ----
    {
        Scene s("provoke");
        place(s, "Player", {2, 0, 0});
        GameObject* npc = place(s, "Critter", {0, 0, 0});
        auto* ai = npc->AddComponent<NPCController>();
        ai->behavior = (int)NPCController::Behavior::Wander;   // normally peaceful
        ai->provokable = true; ai->fieldOfView = 360.0f; ai->sightRange = 10.0f;
        ai->turnSpeed = 0.0f;
        s.Start();
        s.Update(0.1f);
        ai->Damage(5);
        s.Update(0.1f);
        CHECK(ai->IsAlerted());                          // now hunts the attacker
    }

    // ---- Serialization round-trips the new fields + waypoints ----
    {
        Scene a("A");
        auto* npc = a.CreateGameObject("E");
        auto* ai = npc->AddComponent<NPCController>();
        ai->behavior = (int)NPCController::Behavior::Patrol;
        ai->runSpeed = 7.5f; ai->fieldOfView = 120.0f; ai->lineOfSight = true;
        ai->aggressive = true; ai->fleeHealthPct = 0.3f; ai->detectionTime = 1.5f;
        ai->waypoints = { {1, 2, 3}, {-4, 0, 5} };
        std::string text = SceneSerializer::Serialize(a);
        Scene b("B");
        CHECK(SceneSerializer::Deserialize(b, text));
        auto* lf = b.Find("E") ? b.Find("E")->GetComponent<NPCController>() : nullptr;
        CHECK(lf != nullptr);
        if (lf) {
            CHECK(std::fabs(lf->runSpeed - 7.5f) < 1e-3f);
            CHECK(std::fabs(lf->fieldOfView - 120.0f) < 1e-3f);
            CHECK(lf->lineOfSight == true);
            CHECK(lf->aggressive == true);
            CHECK(std::fabs(lf->fleeHealthPct - 0.3f) < 1e-3f);
            CHECK(std::fabs(lf->detectionTime - 1.5f) < 1e-3f);
            CHECK(lf->waypoints.size() == 2);
            if (lf->waypoints.size() == 2) {
                CHECK(std::fabs(lf->waypoints[0].y - 2.0f) < 1e-3f);
                CHECK(std::fabs(lf->waypoints[1].x + 4.0f) < 1e-3f);
            }
        }
    }

    TEST_MAIN_RESULT();
}
