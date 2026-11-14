#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/AI/Perception.hpp"
#include "okay/Components/BehaviorTreeRunner.hpp"
#include <cmath>
#include <memory>
#include <string>

using namespace okay;

// The NPC Controller, the behavior tree's Perception service and its
// line-of-sight test all go through one check. These scenes put that check
// in front of a real floor, a real wall and a real target, and require the
// NPC and the tree to agree — and to agree with Sense() itself.
//
// The controller is read on the first tick. After that it remembers a target
// it has already noticed (lose-sight, search), which is the brain, not the
// sense. Chase with no detection delay turns a first sighting into "Chase"
// and a sound it cannot see into "Search".

namespace {

GameObject* Box(Scene& s, const char* name, Vec3 pos, Vec3 scale, bool trigger) {
    GameObject* g = s.CreateGameObject(name);
    g->transform->localPosition = pos;
    g->transform->localScale = scale;
    auto* box = g->AddComponent<BoxCollider3D>();
    box->size = {1, 1, 1};
    box->isTrigger = trigger;
    g->AddComponent<Rigidbody3D>()->bodyType = Rigidbody3D::BodyType::Static;
    return g;
}

struct Answers {
    std::string npc;     // first-tick state: Chase (saw), Search (heard), Wander (nothing)
    bool tree = false;   // Perception service "can see" key: seen or heard
    bool ray = false;    // the tree's line-of-sight test, which has no cone and no hearing
    bool saw = false;    // Sense().canSee
    bool heard = false;  // Sense().heard
    bool noticed = false;
};

Answers Watch(Vec3 playerAt, float fov, float range, float hearing, bool los,
              bool wall, bool triggerWall, bool bodyOnPlayer) {
    Scene s("sense");
    s.physicsEnabled = false;
    Box(s, "Ground", {0, -0.5f, 0}, {20, 1, 20}, false);
    if (wall) Box(s, "Wall", {0, 1.0f, 2.5f}, {6, 2, 0.4f}, triggerWall);

    GameObject* player = s.CreateGameObject("Player");
    player->transform->localPosition = playerAt;
    if (bodyOnPlayer) {
        player->AddComponent<BoxCollider3D>()->size = {1, 1, 1};
        player->AddComponent<Rigidbody3D>()->bodyType = Rigidbody3D::BodyType::Static;
    }

    auto stand = [&](const char* name) {
        GameObject* g = s.CreateGameObject(name);
        g->transform->localPosition = {0, 0, 0};
        g->transform->localRotation = Quat::LookRotation({0, 0, 1});   // face +Z
        return g;
    };

    GameObject* npcGo = stand("Enemy");
    auto* ai = npcGo->AddComponent<NPCController>();
    ai->behavior = (int)NPCController::Behavior::Chase;
    ai->fieldOfView = fov;
    ai->sightRange = range;
    ai->hearingRange = hearing;
    ai->lineOfSight = los;
    ai->eyeHeight = 1.2f;
    ai->detectionTime = 0.0f;
    ai->turnSpeed = 0.0f;

    GameObject* treeGo = stand("Tree");
    auto* runner = treeGo->AddComponent<BehaviorTreeRunner>();
    auto tree = std::make_shared<BehaviorTree>();
    tree->DeclareKey("canSee", BtValue::Bool(false));
    const int root = tree->AddNode(BtNodeType::Sequence);
    BtNode* per = tree->Find(tree->AddNode(BtNodeType::Perception, root));
    per->Set("targetName", "Player");
    per->Set("range", range);
    per->Set("angle", fov);
    per->Set("hearing", hearing);
    per->Set("lineOfSight", los);
    per->Set("eyeHeight", 1.2f);
    per->Set("interval", 0.0f);
    tree->Find(tree->AddNode(BtNodeType::Wait, root))->Set("min", 100.0f);
    runner->SetTree(tree);

    // The line-of-sight test is not the cone. Distance 0 means "any distance".
    auto condTree = std::make_shared<BehaviorTree>();
    BtNode* cond = condTree->Find(condTree->AddNode(BtNodeType::Condition));
    cond->Set("kind", "lineOfSight");
    cond->Set("target", "Player");
    cond->Set("distance", 0.0f);
    auto* ray = stand("Ray")->AddComponent<BehaviorTreeRunner>();
    ray->SetTree(condTree);

    s.Start();
    s.Update(0.1f);

    SenseSettings set;
    set.sightRange = range;
    set.fieldOfView = fov;
    set.lineOfSight = los;
    set.eyeHeight = 1.2f;
    set.hearingRange = hearing;
    const Vec3 fwd = npcGo->transform->localRotation * Vec3{0, 0, 1};
    const SenseResult sr = Sense(&s, npcGo->transform->Position(), fwd,
                                 player->transform->Position(), npcGo, player, set);
    Answers a;
    a.npc = ai->StateName();
    a.tree = runner->blackboard.GetBool("canSee");
    a.ray = ray->LastResult() == BtStatus::Success;
    a.saw = sr.canSee;
    a.heard = sr.heard;
    a.noticed = sr.noticed;
    return a;
}

// The controller's first tick and the tree's can-see key are the same notice,
// and both are what Sense() just computed. The line-of-sight test matches the
// shared ray (a wall blocks it; the cone and hearing do not apply to it).
void Agree(const Answers& a, bool expectNotice, bool expectSaw, bool wallBlocksRay) {
    CHECK(a.noticed == expectNotice);
    CHECK(a.saw == expectSaw);
    CHECK(a.tree == a.noticed);
    if (expectSaw) CHECK(a.npc == "Chase");
    else if (expectNotice) CHECK(a.npc == "Search");   // heard, not seen: goes to look
    else CHECK(a.npc == "Wander");
    CHECK(a.ray == !wallBlocksRay);
}

} // namespace

int main() {
    RUN_SUITE("perception");

    // ---- Sight: in front, in range, nothing in the way ----
    {
        Answers a = Watch({0, 0, 5}, 90.0f, 12.0f, 0.0f, true, false, false, false);
        Agree(a, true, true, false);
    }

    // ---- Range: too far to see, and exactly on the range still counts ----
    {
        Answers far = Watch({0, 0, 30}, 360.0f, 10.0f, 0.0f, false, false, false, false);
        Agree(far, false, false, false);   // no wall, so the ray is clear; the cone's range is not
        Answers edge = Watch({0, 0, 10}, 360.0f, 10.0f, 0.0f, false, false, false, false);
        Agree(edge, true, true, false);
    }

    // ---- Cone: behind a 90 degree view is unseen; a little off to the side is seen;
    //      straight out to the side (90 degrees) is outside the half-angle ----
    {
        Answers behind = Watch({0, 0, -5}, 90.0f, 20.0f, 0.0f, false, false, false, false);
        Agree(behind, false, false, false);
        Answers aside = Watch({1, 0, 5}, 90.0f, 20.0f, 0.0f, false, false, false, false);
        Agree(aside, true, true, false);
        Answers flank = Watch({5, 0, 0}, 90.0f, 20.0f, 0.0f, false, false, false, false);
        Agree(flank, false, false, false);
        Answers around = Watch({0, 0, -5}, 360.0f, 20.0f, 0.0f, false, false, false, false);
        Agree(around, true, true, false);
    }

    // ---- A wall blocks sight when line of sight is on, and does not when it is off
    //      (an NPC's line of sight is off unless you turn it on) ----
    {
        Answers blocked = Watch({0, 0, 5}, 120.0f, 12.0f, 0.0f, true, true, false, false);
        Agree(blocked, false, false, true);
        Answers through = Watch({0, 0, 5}, 120.0f, 12.0f, 0.0f, false, true, false, false);
        Agree(through, true, true, true);   // the sense sees through; the ray test still hits the wall
    }

    // ---- A trigger volume is not a wall, and the quarry's own body is not a wall ----
    {
        Answers trigger = Watch({0, 0, 5}, 120.0f, 12.0f, 0.0f, true, true, true, false);
        Agree(trigger, true, true, false);
        Answers body = Watch({0, 0, 5}, 120.0f, 12.0f, 0.0f, true, false, false, true);
        Agree(body, true, true, false);
    }

    // ---- Hearing: behind the cone, close enough to hear, and one step too far ----
    {
        Answers close = Watch({0, 0, -2}, 90.0f, 20.0f, 4.0f, true, false, false, false);
        Agree(close, true, false, false);
        CHECK(close.heard);
        Answers deaf = Watch({0, 0, -8}, 90.0f, 20.0f, 4.0f, true, false, false, false);
        Agree(deaf, false, false, false);
        // A wall does not stop a sound. The eyes still cannot see through it.
        Answers muffled = Watch({0, 0, 5}, 120.0f, 12.0f, 8.0f, true, true, false, false);
        CHECK(muffled.noticed && !muffled.saw && muffled.heard);
        CHECK(muffled.tree);
        CHECK(muffled.npc == "Search");
        CHECK(!muffled.ray);
    }

    // ---- The shared cone, on its own, matches the angles the scenes used ----
    {
        const Vec3 origin{0, 0, 0}, forward{0, 0, 1};
        CHECK(InSightCone(origin, forward, {0, 0, 5}, 90.0f));
        CHECK(!InSightCone(origin, forward, {0, 0, -5}, 90.0f));
        CHECK(!InSightCone(origin, forward, {5, 0, 0}, 90.0f));
        CHECK(InSightCone(origin, forward, {0, 0, -5}, 360.0f));
        CHECK(HorizontalDistance({0, 5, 0}, {0, 0, 10}) == 10.0f);   // height is not distance
    }

    TEST_MAIN_RESULT();
}
