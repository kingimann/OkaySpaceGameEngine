#include "okay/Animation/HumanoidActions.hpp"
#include "HumanoidActionsInternal.hpp"
#include "okay/Animation/HumanoidMotion.hpp"
#include "okay/Components/AnimControl.hpp"
#include "okay/Components/AnimStateMachine.hpp"
#include "okay/Components/Character.hpp"
#include "okay/Components/Seat.hpp"
#include "okay/Components/VehicleSeat.hpp"
#include "okay/Components/DodgeAction.hpp"
#include "okay/Components/FirstPersonController.hpp"
#include "okay/Components/SurvivalComponents.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Net/NetOwnership.hpp"
#include "okay/Physics/Physics3D.hpp"
#include "okay/Physics/Rigidbody3D.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Components/ThirdPersonController.hpp"
#include "okay/Components/Traversal.hpp"
#include "okay/Core/Log.hpp"
#include "okay/Input/GameInput.hpp"
#include "okay/Core/Time.hpp"
#include "okay/Physics/Collider3D.hpp"
#include "okay/Physics/Physics3D.hpp"
#include "okay/Physics/Rigidbody3D.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/Transform.hpp"
#include <algorithm>
#include <cmath>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace okay {
namespace HumanoidActions {

namespace humanoid_actions {
// Replication: a one-shot the owner started is noted on its Character so the
// network layer can send it once (see Character::netEventSeq).
void NoteNetEvent(GameObject* go, int id, float param) {
    if (!go) return;
    if (Character* c = go->GetComponent<Character>()) c->RecordNetEvent(id, param);
}
} // namespace
namespace humanoid_actions {

using SM = AnimStateMachine;
using C = SM::Cond;
using B = HumanoidMotion::Bone;

KeyPose FromState(int state) {
    KeyPose k;
    std::vector<Vec3> pose;
    HumanoidMotion::Inputs in;
    HumanoidMotion::Pose(state, 0.0f, in, pose);
    for (int i = 0; i < 15; ++i) k.bones[i] = i < (int)pose.size() ? pose[i] : Vec3{};
    k.hipY = HumanoidMotion::HipY(state, in);
    return k;
}

KeyPose At(KeyPose k, float t) { k.t = t; return k; }

float Smooth(float u) {
    if (u < 0.0f) return 0.0f;
    if (u > 1.0f) return 1.0f;
    return u * u * (3.0f - 2.0f * u);
}

KeyPose SampleKeys(const std::vector<KeyPose>& keys, float t) {
    if (keys.empty()) return {};
    if (t <= keys.front().t) return keys.front();
    if (t >= keys.back().t) return keys.back();
    for (std::size_t i = 0; i + 1 < keys.size(); ++i) {
        if (t > keys[i + 1].t) continue;
        const float span = keys[i + 1].t - keys[i].t;
        const float u = span > 1e-5f ? Smooth((t - keys[i].t) / span) : 1.0f;
        KeyPose s = keys[i];
        s.t = t;
        for (int b = 0; b < 15; ++b)
            s.bones[b] = AnimClip::BlendEuler(keys[i].bones[b], keys[i + 1].bones[b], u);
        s.hipY = keys[i].hipY + (keys[i + 1].hipY - keys[i].hipY) * u;
        s.rootOffset = Vec3::Lerp(keys[i].rootOffset, keys[i + 1].rootOffset, u);
        return s;
    }
    return keys.back();
}

AnimClip BakeAction(const std::string& name, const std::vector<KeyPose>& keys, bool loop,
                    const std::vector<std::pair<float, const char*>>& events) {
    AnimClip c;
    c.name = name;
    c.loop = loop;
    c.hasRoot = true;
    if (keys.empty()) return c;
    if (loop && keys.size() == 1) {
        AnimKey key;
        key.time = 0.0f;
        key.pose.assign(keys[0].bones, keys[0].bones + 15);
        key.root = Vec3{keys[0].rootOffset.x, keys[0].hipY, keys[0].rootOffset.z};
        c.keys.push_back(std::move(key));
        c.length = 1.0f;
        return c;
    }
    const float length = keys.back().t;
    c.length = length;
    const int n = std::max(2, (int)std::ceil(length * 30.0f));
    for (int i = 0; i <= n; ++i) {
        const float t = length * (float)i / (float)n;
        const KeyPose s = SampleKeys(keys, t);
        AnimKey key;
        key.time = t;
        key.pose.assign(s.bones, s.bones + 15);
        key.root = Vec3{s.rootOffset.x, s.hipY, s.rootOffset.z};
        c.keys.push_back(std::move(key));
    }
    for (const auto& ev : events) c.AddEvent(ev.first, ev.second);
    return c;
}

void AddClip(std::vector<HumanoidLibrary::Entry>& out, AnimClip clip) {
    HumanoidLibrary::Entry e;
    e.clip = std::move(clip);
    e.state = 0;
    out.push_back(std::move(e));
}

KeyPose ForwardHold() {
    KeyPose k = FromState(7);   // prone, then the head turns aside and the arms relax
    k.bones[B::B_HEAD].y += 36.0f;
    k.bones[B::B_HEAD].z += 8.0f;
    k.bones[B::B_LUPARM].x = -130.0f;
    k.bones[B::B_RUPARM].x = -110.0f;
    k.bones[B::B_LFORE].x = -28.0f;
    k.bones[B::B_RFORE].x = -36.0f;
    k.hipY = -0.78f;
    return k;
}

KeyPose BackHold() {
    KeyPose k;
    k.bones[B::B_HIPS] = {-88.0f, 0.0f, 0.0f};
    k.bones[B::B_TORSO] = {-6.0f, 0.0f, 0.0f};
    k.bones[B::B_HEAD] = {16.0f, 24.0f, 0.0f};
    k.bones[B::B_LUPARM] = {-28.0f, 0.0f, 72.0f};
    k.bones[B::B_RUPARM] = {-28.0f, 0.0f, -72.0f};
    k.bones[B::B_LFORE] = {-18.0f, 0.0f, 0.0f};
    k.bones[B::B_RFORE] = {-18.0f, 0.0f, 0.0f};
    k.bones[B::B_LTHIGH] = {-12.0f, 0.0f, -8.0f};
    k.bones[B::B_RTHIGH] = {-8.0f, 0.0f, 8.0f};
    k.bones[B::B_LSHIN] = {22.0f, 0.0f, 0.0f};
    k.bones[B::B_RSHIN] = {16.0f, 0.0f, 0.0f};
    k.hipY = -0.70f;
    return k;
}

KeyPose SideHold(float side) {
    KeyPose k;
    k.bones[B::B_HIPS] = {48.0f, 0.0f, side * 72.0f};
    k.bones[B::B_TORSO] = {10.0f, 0.0f, side * -10.0f};
    k.bones[B::B_HEAD] = {-6.0f, side * 18.0f, side * 14.0f};
    k.bones[B::B_LUPARM] = {-16.0f, 0.0f, 55.0f};
    k.bones[B::B_RUPARM] = {-16.0f, 0.0f, -55.0f};
    k.bones[B::B_LFORE] = {-42.0f, 0.0f, 0.0f};
    k.bones[B::B_RFORE] = {-42.0f, 0.0f, 0.0f};
    k.bones[B::B_LTHIGH] = {22.0f, 0.0f, side * 6.0f};
    k.bones[B::B_RTHIGH] = {14.0f, 0.0f, side * -4.0f};
    k.bones[B::B_LSHIN] = {36.0f, 0.0f, 0.0f};
    k.bones[B::B_RSHIN] = {28.0f, 0.0f, 0.0f};
    k.hipY = -0.62f;
    k.rootOffset = {side * 0.04f, 0.0f, 0.0f};
    return k;
}

void BakeDeath(std::vector<HumanoidLibrary::Entry>& out) {
    const KeyPose idle = FromState(1);
    const KeyPose crouch = FromState(6);
    const KeyPose fwd = ForwardHold();
    const KeyPose back = BackHold();
    const KeyPose left = SideHold(1.0f);
    const KeyPose right = SideHold(-1.0f);

    KeyPose buckle = crouch;
    buckle.bones[B::B_HIPS].x += 18.0f;
    buckle.bones[B::B_TORSO].x += 22.0f;
    buckle.bones[B::B_HEAD].x += 12.0f;
    buckle.hipY = -0.26f;

    KeyPose chest = fwd;
    chest.bones[B::B_HIPS].x = 68.0f;
    chest.bones[B::B_HEAD].y = 8.0f;
    chest.hipY = -0.50f;
    chest.rootOffset = {0.0f, 0.0f, 0.06f};

    // Last 0.15s is the hold, so a later ragdoll can take this frame with no velocity.
    const std::vector<std::pair<float, const char*>> endEv = {{1.00f, "death_settled"}, {1.15f, "action_end"}};
    AddClip(out, BakeAction("Death_F", {At(idle, 0.0f), At(buckle, 0.28f), At(chest, 0.58f), At(fwd, 0.85f), At(fwd, 1.00f), At(fwd, 1.15f)}, false, endEv));

    KeyPose stumble = idle;
    stumble.bones[B::B_TORSO].x = -18.0f;
    stumble.bones[B::B_HIPS].x = -12.0f;
    stumble.bones[B::B_LUPARM] = {-20.0f, 0.0f, 40.0f};
    stumble.bones[B::B_RUPARM] = {-10.0f, 0.0f, -30.0f};
    stumble.bones[B::B_LTHIGH] = {20.0f, 0.0f, 0.0f};
    stumble.bones[B::B_RTHIGH] = {-16.0f, 0.0f, 0.0f};
    stumble.hipY = -0.06f;
    stumble.rootOffset = {0.0f, 0.0f, -0.04f};
    KeyPose falling = back;
    falling.bones[B::B_HIPS].x = -48.0f;
    falling.hipY = -0.32f;
    const std::vector<std::pair<float, const char*>> backEv = {{1.00f, "death_settled"}, {1.15f, "action_end"}};
    AddClip(out, BakeAction("Death_B", {At(idle, 0.0f), At(stumble, 0.26f), At(falling, 0.55f), At(back, 0.85f), At(back, 1.00f), At(back, 1.15f)}, false, backEv));

    auto sideClip = [&](const char* name, const KeyPose& hold, float side) {
        KeyPose dip = idle;
        dip.bones[B::B_HIPS].z = side * 18.0f;
        dip.bones[B::B_TORSO].z = side * 12.0f;
        dip.bones[B::B_LTHIGH].x = 16.0f;
        dip.bones[B::B_RTHIGH].x = 16.0f;
        dip.hipY = -0.12f;
        KeyPose mid = hold;
        mid.bones[B::B_HIPS].z = side * 40.0f;
        mid.hipY = -0.36f;
        const std::vector<std::pair<float, const char*>> ev = {{0.95f, "death_settled"}, {1.10f, "action_end"}};
        AddClip(out, BakeAction(name, {At(idle, 0.0f), At(dip, 0.24f), At(mid, 0.55f), At(hold, 0.82f), At(hold, 0.95f), At(hold, 1.10f)}, false, ev));
    };
    sideClip("Death_L", left, 1.0f);
    sideClip("Death_R", right, -1.0f);

    AddClip(out, BakeAction("DeadHold_F", {fwd}, true, {}));
    AddClip(out, BakeAction("DeadHold_B", {back}, true, {}));
    AddClip(out, BakeAction("DeadHold_L", {left}, true, {}));
    AddClip(out, BakeAction("DeadHold_R", {right}, true, {}));
}

void BakeDive(std::vector<HumanoidLibrary::Entry>& out) {
    const KeyPose run = FromState(3);
    const KeyPose prone = FromState(7);

    KeyPose plant = run;
    plant.bones[B::B_HIPS].x += 22.0f;
    plant.bones[B::B_TORSO].x += 16.0f;
    plant.bones[B::B_LTHIGH].x += 26.0f;
    plant.bones[B::B_RTHIGH].x += 18.0f;
    plant.bones[B::B_LSHIN].x += 28.0f;
    plant.bones[B::B_RSHIN].x += 22.0f;
    plant.bones[B::B_LUPARM] = {24.0f, 0.0f, 28.0f};
    plant.bones[B::B_RUPARM] = {24.0f, 0.0f, -28.0f};
    plant.hipY = -0.10f;

    KeyPose launch = run;
    launch.bones[B::B_HIPS].x = 50.0f;
    launch.bones[B::B_TORSO].x = 20.0f;
    launch.bones[B::B_HEAD].x = -14.0f;
    launch.bones[B::B_LUPARM] = {-78.0f, 0.0f, 16.0f};
    launch.bones[B::B_RUPARM] = {-78.0f, 0.0f, -16.0f};
    launch.bones[B::B_LFORE].x = -14.0f;
    launch.bones[B::B_RFORE].x = -14.0f;
    launch.bones[B::B_LTHIGH].x = -16.0f;
    launch.bones[B::B_RTHIGH].x = -8.0f;
    launch.hipY = -0.02f;
    launch.rootOffset = {0.0f, 0.0f, 0.05f};

    KeyPose flight = prone;
    flight.bones[B::B_HIPS].x = 76.0f;
    flight.bones[B::B_TORSO].x = 10.0f;
    flight.bones[B::B_HEAD].x = -16.0f;
    flight.bones[B::B_LUPARM] = {-148.0f, 0.0f, 10.0f};
    flight.bones[B::B_RUPARM] = {-148.0f, 0.0f, -10.0f};
    flight.bones[B::B_LFORE].x = -6.0f;
    flight.bones[B::B_RFORE].x = -6.0f;
    flight.bones[B::B_LTHIGH].x = 14.0f;
    flight.bones[B::B_RTHIGH].x = 20.0f;
    flight.hipY = -0.26f;

    KeyPose chest = prone;
    chest.bones[B::B_HIPS].x = 84.0f;
    chest.bones[B::B_LUPARM] = {-128.0f, 0.0f, 18.0f};
    chest.bones[B::B_RUPARM] = {-128.0f, 0.0f, -18.0f};
    chest.bones[B::B_LFORE].x = -22.0f;
    chest.bones[B::B_RFORE].x = -22.0f;
    chest.hipY = -0.58f;

    // Ends on the prone pose the crawl and get-up already use.
    const std::vector<std::pair<float, const char*>> ev = {
        {0.18f, "takeoff"}, {0.50f, "impact"}, {0.70f, "action_end"}};
    AddClip(out, BakeAction("Dive", {
        At(run, 0.0f), At(plant, 0.12f), At(launch, 0.22f), At(flight, 0.40f),
        At(chest, 0.55f), At(prone, 0.70f)}, false, ev));

    KeyPose absorb = flight;
    absorb.hipY = -0.40f;
    const std::vector<std::pair<float, const char*>> landEv = {{0.08f, "impact"}, {0.28f, "action_end"}};
    AddClip(out, BakeAction("DiveLand", {At(absorb, 0.0f), At(chest, 0.12f), At(prone, 0.28f)}, false, landEv));
}

void BakeHits(std::vector<HumanoidLibrary::Entry>& out) {
    const KeyPose idle = FromState(1);
    const KeyPose crouch = FromState(6);
    const KeyPose prone = FromState(7);
    auto flinch = [](KeyPose base, float torsoX, float torsoZ, float headX) {
        base.bones[B::B_TORSO].x += torsoX;
        base.bones[B::B_TORSO].z += torsoZ;
        base.bones[B::B_HEAD].x += headX;
        base.bones[B::B_LUPARM].z += 18.0f;
        base.bones[B::B_RUPARM].z -= 18.0f;
        return base;
    };
    const std::vector<std::pair<float, const char*>> lightEv = {{0.35f, "action_end"}};
    AddClip(out, BakeAction("Hit_F", {At(idle, 0.0f), At(flinch(idle, 22.0f, 0.0f, -14.0f), 0.12f), At(idle, 0.35f)}, false, lightEv));
    AddClip(out, BakeAction("Hit_B", {At(idle, 0.0f), At(flinch(idle, -18.0f, 0.0f, 12.0f), 0.12f), At(idle, 0.35f)}, false, lightEv));
    AddClip(out, BakeAction("Hit_L", {At(idle, 0.0f), At(flinch(idle, 6.0f, 20.0f, 0.0f), 0.12f), At(idle, 0.35f)}, false, lightEv));
    AddClip(out, BakeAction("Hit_R", {At(idle, 0.0f), At(flinch(idle, 6.0f, -20.0f, 0.0f), 0.12f), At(idle, 0.35f)}, false, lightEv));

    KeyPose heavyF = flinch(idle, 28.0f, 0.0f, -16.0f);
    heavyF.bones[B::B_HIPS].x = 12.0f;
    heavyF.bones[B::B_LTHIGH].x = 22.0f;
    heavyF.bones[B::B_RTHIGH].x = -10.0f;
    heavyF.hipY = -0.08f;
    heavyF.rootOffset = {0.0f, 0.0f, 0.06f};
    KeyPose heavyB = flinch(idle, -24.0f, 0.0f, 14.0f);
    heavyB.bones[B::B_HIPS].x = -10.0f;
    heavyB.hipY = -0.06f;
    heavyB.rootOffset = {0.0f, 0.0f, -0.05f};
    const std::vector<std::pair<float, const char*>> heavyEv = {{0.80f, "action_end"}};
    AddClip(out, BakeAction("Hit_Heavy_F", {At(idle, 0.0f), At(heavyF, 0.18f), At(heavyF, 0.42f), At(idle, 0.80f)}, false, heavyEv));
    AddClip(out, BakeAction("Hit_Heavy_B", {At(idle, 0.0f), At(heavyB, 0.18f), At(heavyB, 0.42f), At(idle, 0.80f)}, false, heavyEv));
    AddClip(out, BakeAction("Hit_Crouch", {At(crouch, 0.0f), At(flinch(crouch, 16.0f, 0.0f, -8.0f), 0.14f), At(crouch, 0.40f)}, false, {{0.40f, "action_end"}}));
    AddClip(out, BakeAction("Hit_Prone", {At(prone, 0.0f), At(flinch(prone, 8.0f, 6.0f, -4.0f), 0.12f), At(prone, 0.36f)}, false, {{0.36f, "action_end"}}));
    AddClip(out, BakeAction("Hit_Off", {idle}, true, {}));
}

void BakeRoll(std::vector<HumanoidLibrary::Entry>& out) {
    const KeyPose idle = FromState(1);
    const KeyPose crouch = FromState(6);
    KeyPose tuck = idle;
    tuck.bones[B::B_HIPS].x = 28.0f;
    tuck.bones[B::B_TORSO].x = 36.0f;
    tuck.bones[B::B_HEAD].x = -20.0f;
    tuck.bones[B::B_LTHIGH] = {110.0f, 8.0f, -6.0f};
    tuck.bones[B::B_RTHIGH] = {110.0f, -8.0f, 6.0f};
    tuck.bones[B::B_LSHIN].x = -110.0f;
    tuck.bones[B::B_RSHIN].x = -110.0f;
    tuck.hipY = -0.15f;
    KeyPose tuck2 = tuck;
    tuck2.bones[B::B_HIPS].x = -20.0f;
    tuck2.bones[B::B_TORSO].x = -10.0f;
    AddClip(out, BakeAction("Roll_F", {At(idle, 0.0f), At(tuck, 0.22f), At(tuck2, 0.42f), At(idle, 0.65f)}, false, {{0.65f, "action_end"}}));

    auto dodge = [&](const char* name, float side, float back) {
        KeyPose lean = idle;
        lean.bones[B::B_TORSO].z = side * 16.0f;
        lean.bones[B::B_HIPS].z = side * 8.0f;
        lean.bones[B::B_TORSO].x = back * 12.0f;
        lean.bones[B::B_LTHIGH].x = back > 0.0f ? -18.0f : 14.0f;
        lean.bones[B::B_RTHIGH].x = back > 0.0f ? -18.0f : 14.0f;
        lean.rootOffset = {side * 0.04f, 0.0f, back * 0.03f};
        AddClip(out, BakeAction(name, {At(idle, 0.0f), At(lean, 0.14f), At(idle, 0.40f)}, false, {{0.40f, "action_end"}}));
    };
    dodge("Dodge_L", 1.0f, 0.0f);
    dodge("Dodge_R", -1.0f, 0.0f);
    dodge("Dodge_B", 0.0f, 1.0f);

    KeyPose feet = crouch;
    feet.bones[B::B_HIPS].x = 18.0f;
    feet.bones[B::B_LTHIGH].x = 40.0f;
    feet.bones[B::B_RTHIGH].x = 70.0f;
    feet.bones[B::B_LSHIN].x = -30.0f;
    feet.bones[B::B_RSHIN].x = -50.0f;
    feet.bones[B::B_LUPARM] = {20.0f, 0.0f, 30.0f};
    feet.bones[B::B_RUPARM] = {20.0f, 0.0f, -30.0f};
    feet.hipY = crouch.hipY;
    AddClip(out, BakeAction("Slide", {At(feet, 0.0f), At(feet, 0.6f)}, true, {}));
    AddClip(out, BakeAction("SlideEnd", {At(feet, 0.0f), At(crouch, 0.32f)}, false, {{0.32f, "action_end"}}));
}

// Prone base, so the body stays low. The first and last keys match: a loop that
// does not pop. hipY stays near the prone height. rootOffset is visual only.
void BakeSwim(std::vector<HumanoidLibrary::Entry>& out) {
    const KeyPose prone = FromState(7);

    KeyPose treadOut = prone;
    treadOut.bones[B::B_HEAD].x = -40.0f;
    treadOut.bones[B::B_LUPARM] = {-130.0f, 10.0f, 55.0f};
    treadOut.bones[B::B_RUPARM] = {-130.0f, -10.0f, -55.0f};
    treadOut.bones[B::B_LFORE].x = -18.0f;
    treadOut.bones[B::B_RFORE].x = -18.0f;
    treadOut.bones[B::B_LTHIGH].x = 8.0f;
    treadOut.bones[B::B_RTHIGH].x = 8.0f;
    treadOut.bones[B::B_LSHIN].x = 18.0f;
    treadOut.bones[B::B_RSHIN].x = 18.0f;
    treadOut.hipY = -0.74f;

    KeyPose treadIn = prone;
    treadIn.bones[B::B_HEAD].x = -36.0f;
    treadIn.bones[B::B_LUPARM] = {-78.0f, -8.0f, 20.0f};
    treadIn.bones[B::B_RUPARM] = {-78.0f, 8.0f, -20.0f};
    treadIn.bones[B::B_LFORE].x = -62.0f;
    treadIn.bones[B::B_RFORE].x = -62.0f;
    treadIn.bones[B::B_LTHIGH].x = 22.0f;
    treadIn.bones[B::B_RTHIGH].x = 22.0f;
    treadIn.bones[B::B_LSHIN].x = 40.0f;
    treadIn.bones[B::B_RSHIN].x = 40.0f;
    treadIn.hipY = -0.70f;

    const std::vector<std::pair<float, const char*>> treadEv = {{0.55f, "stroke"}};
    AddClip(out, BakeAction("Swim_Tread", {At(treadOut, 0.0f), At(treadIn, 0.55f), At(treadOut, 1.10f)}, true, treadEv));

    KeyPose reach = prone;
    reach.bones[B::B_HEAD].x = -28.0f;
    reach.bones[B::B_TORSO].x = 6.0f;
    reach.bones[B::B_LUPARM] = {-172.0f, 0.0f, 14.0f};
    reach.bones[B::B_RUPARM] = {-172.0f, 0.0f, -14.0f};
    reach.bones[B::B_LFORE].x = -6.0f;
    reach.bones[B::B_RFORE].x = -6.0f;
    reach.hipY = -0.76f;

    KeyPose pull = prone;
    pull.bones[B::B_HEAD].x = -32.0f;
    pull.bones[B::B_TORSO].x = 4.0f;
    pull.bones[B::B_LUPARM] = {-96.0f, 0.0f, 8.0f};
    pull.bones[B::B_RUPARM] = {-96.0f, 0.0f, -8.0f};
    pull.bones[B::B_LFORE].x = -48.0f;
    pull.bones[B::B_RFORE].x = -48.0f;
    pull.bones[B::B_LTHIGH].x = 16.0f;
    pull.bones[B::B_RTHIGH].x = 10.0f;
    pull.bones[B::B_LSHIN].x = 24.0f;
    pull.bones[B::B_RSHIN].x = 16.0f;
    pull.hipY = -0.73f;
    pull.rootOffset = {0.0f, 0.0f, 0.05f};

    const std::vector<std::pair<float, const char*>> fwdEv = {{0.40f, "stroke"}};
    AddClip(out, BakeAction("Swim_Fwd", {At(reach, 0.0f), At(pull, 0.40f), At(reach, 0.80f)}, true, fwdEv));
}

// One-shots from idle back to idle. The looping Wave / Cheer gestures are separate.
void BakeEmotes(std::vector<HumanoidLibrary::Entry>& out) {
    const KeyPose idle = FromState(1);

    KeyPose nod = idle;
    nod.bones[B::B_HEAD].x += 26.0f;
    nod.bones[B::B_TORSO].x += 6.0f;
    const std::vector<std::pair<float, const char*>> nodEv = {{0.55f, "action_end"}};
    AddClip(out, BakeAction("Emote_Nod", {At(idle, 0.0f), At(nod, 0.22f), At(nod, 0.38f), At(idle, 0.55f)}, false, nodEv));

    KeyPose lookL = idle;
    lookL.bones[B::B_HEAD].y += 32.0f;
    lookL.bones[B::B_TORSO].y += 6.0f;
    KeyPose lookR = idle;
    lookR.bones[B::B_HEAD].y -= 32.0f;
    lookR.bones[B::B_TORSO].y -= 4.0f;
    const std::vector<std::pair<float, const char*>> shakeEv = {{0.72f, "action_end"}};
    AddClip(out, BakeAction("Emote_Shake", {
        At(idle, 0.0f), At(lookL, 0.16f), At(lookR, 0.38f), At(lookL, 0.55f), At(idle, 0.72f)}, false, shakeEv));

    KeyPose shrug = idle;
    shrug.bones[B::B_LUPARM] = {-48.0f, 0.0f, -36.0f};
    shrug.bones[B::B_RUPARM] = {-48.0f, 0.0f, 36.0f};
    shrug.bones[B::B_LFORE].x = -55.0f;
    shrug.bones[B::B_RFORE].x = -55.0f;
    shrug.bones[B::B_HEAD].x = -10.0f;
    shrug.bones[B::B_HEAD].z = 8.0f;
    shrug.bones[B::B_TORSO].x = -4.0f;
    const std::vector<std::pair<float, const char*>> shrugEv = {{0.68f, "action_end"}};
    AddClip(out, BakeAction("Emote_Shrug", {At(idle, 0.0f), At(shrug, 0.22f), At(shrug, 0.42f), At(idle, 0.68f)}, false, shrugEv));
}

struct ActionSpec {
    const char* name;
    int rev;
};

const ActionSpec kStates[] = {
    {"Death", 1}, {"DeadHold", 1},
    {"Dive", 2}, {"DiveLand", 2},
    {"SitDown", 3}, {"Sit", 3}, {"StandUp", 3},
    {"Hit", 4}, {"HitHeavy", 4}, {"Hit_Crouch", 4}, {"Hit_Prone", 4},
    {"Roll", 5}, {"Dodge", 5}, {"Slide", 5}, {"SlideEnd", 5},
    {"Vault", 6}, {"MantleLow", 6}, {"MantleHigh", 6}, {"LedgeGrab", 6},
    {"HangShimmy", 6}, {"ClimbUp", 6}, {"LetGo", 6},
    {"Swim", 7},
    {"Emote_Nod", 8}, {"Emote_Shake", 8}, {"Emote_Shrug", 8}};

SM::State* FindMutable(SM& sm, const std::string& name) {
    for (SM::State& s : sm.states) if (s.name == name) return &s;
    return nullptr;
}
bool HasState(const SM& sm, const char* name) {
    for (const SM::State& s : sm.states) if (s.name == name) return true;
    return false;
}
void EnsureParam(SM& sm, const char* name, int type) {
    for (const SM::Param& p : sm.params) if (p.name == name) return;
    sm.params.push_back({name, type});
}
bool HasAny(const SM& sm, const char* to, const char* param) {
    for (const SM::Transition& t : sm.anyState)
        if (t.to == to && t.param == param) return true;
    return false;
}

SM::Motion MotionAt(const char* clip, float threshold) {
    SM::Motion m;
    m.clip = clip;
    m.threshold = threshold;
    return m;
}

// GetUp when the graph has it, otherwise Idle, otherwise no stand-up edge.
const char* StandUpName(const SM& sm) {
    if (HasState(sm, "GetUp")) return "GetUp";
    if (HasState(sm, "Idle")) return "Idle";
    return nullptr;
}

void WireDeath(SM::State& death, const SM& sm) {
    death.transitions.clear();
    SM::Transition hold;
    hold.to = "DeadHold";
    hold.cond = C::OnClipEnd;
    hold.hasExitTime = true;
    hold.exitTime = 1.0f;
    hold.blend = 0.15f;
    death.transitions.push_back(hold);
    if (const char* up = StandUpName(sm)) {
        SM::Transition revive;
        revive.to = up;
        revive.cond = C::BoolFalse;
        revive.param = "dead";
        revive.blend = 0.35f;
        // Revive wins over "wait for the clip" when both are true.
        death.transitions.insert(death.transitions.begin(), revive);
    }
}

void WireHold(SM::State& hold, const SM& sm) {
    hold.transitions.clear();
    if (const char* up = StandUpName(sm)) {
        SM::Transition revive;
        revive.to = up;
        revive.cond = C::BoolFalse;
        revive.param = "dead";
        revive.blend = 0.35f;
        hold.transitions.push_back(revive);
    }
}

SM::State MakeDeathState() {
    SM::State s;
    s.name = "Death";
    s.blendTree = true;
    s.blendDims = 1;
    s.blendParam = "deathDir";
    s.loop = false;
    s.ikWeight = 0.0f;
    s.lookWeight = 0.0f;
    s.fullBodyIkWeight = 0.0f;
    s.interruptible = false;
    s.motions = {MotionAt("Death_F", 0.0f), MotionAt("Death_B", 1.0f), MotionAt("Death_L", 2.0f), MotionAt("Death_R", 3.0f)};
    return s;
}

SM::State MakeHoldState() {
    SM::State s;
    s.name = "DeadHold";
    s.blendTree = true;
    s.blendDims = 1;
    s.blendParam = "deathDir";
    s.loop = true;
    s.ikWeight = 0.0f;
    s.lookWeight = 0.0f;
    s.fullBodyIkWeight = 0.0f;
    s.interruptible = false;
    s.motions = {MotionAt("DeadHold_F", 0.0f), MotionAt("DeadHold_B", 1.0f), MotionAt("DeadHold_L", 2.0f), MotionAt("DeadHold_R", 3.0f)};
    return s;
}

SM::State MakeSwimState() {
    SM::State s;
    s.name = "Swim";
    s.blendTree = true;
    s.blendDims = 1;
    s.blendParam = "swimSpeed";
    s.loop = true;
    s.ikWeight = 0.0f;
    s.fullBodyIkWeight = 0.0f;
    // Locked so jump and fall (Any State, and this state is not prone) cannot
    // cut a stroke. Death is an edge on the state itself.
    s.interruptible = false;
    s.motions = {MotionAt("Swim_Tread", 0.0f), MotionAt("Swim_Fwd", 1.5f)};
    return s;
}

void WireSwim(SM::State& swim, const SM& sm) {
    swim.transitions.clear();
    if (HasState(sm, "Death")) {
        SM::Transition die;
        die.to = "Death";
        die.cond = C::Trigger;
        die.param = "die";
        die.blend = 0.2f;
        die.interruption = SM::Transition::Interrupt::None;
        swim.transitions.push_back(die);
    }
    if (HasState(sm, "Idle")) {
        SM::Transition out;
        out.to = "Idle";
        out.cond = C::BoolFalse;
        out.param = "swimming";
        out.blend = 0.35f;
        swim.transitions.push_back(out);
    }
}

const char* kEmoteNames[] = {"Emote_Nod", "Emote_Shake", "Emote_Shrug"};

SM::State MakeEmoteState(const char* name) {
    SM::State s;
    s.name = name;
    s.clip = name;
    s.loop = false;
    s.lookWeight = 0.0f;   // a nod or a shrug does not keep the head on a target
    // Jump, fall and death (Any State) can cut a nod. Walking leaves through
    // the state's own edges below.
    s.interruptible = true;
    return s;
}

void WireEmote(SM::State& st, const SM& sm) {
    st.transitions.clear();
    if (HasState(sm, "Run")) {
        SM::Transition run;
        run.to = "Run";
        run.cond = C::FloatGreater;
        run.param = "speed";
        run.value = 0.35f;
        run.blend = 0.12f;
        SM::Condition flag;
        flag.cond = C::BoolTrue;
        flag.param = "run";
        run.extra.push_back(flag);
        st.transitions.push_back(std::move(run));
    }
    if (HasState(sm, "Walk")) {
        SM::Transition walk;
        walk.to = "Walk";
        walk.cond = C::FloatGreater;
        walk.param = "speed";
        walk.value = 0.35f;
        walk.blend = 0.12f;
        st.transitions.push_back(std::move(walk));
    }
    if (HasState(sm, "Idle")) {
        SM::Transition done;
        done.to = "Idle";
        done.cond = C::OnClipEnd;
        done.hasExitTime = true;
        done.exitTime = 1.0f;
        done.blend = 0.15f;
        st.transitions.push_back(std::move(done));
    }
}

void AddEmoteAny(SM& sm, const char* to, float id) {
    if (!HasState(sm, to) || HasAny(sm, to, "emote")) return;
    SM::Transition go;
    go.to = to;
    go.cond = C::Trigger;
    go.param = "emote";
    go.blend = 0.12f;
    SM::Condition which;
    which.cond = C::IntEquals;
    which.param = "emoteId";
    which.value = id;
    go.extra.push_back(which);
    sm.anyState.push_back(std::move(go));
}

bool IsKnownAction(const std::string& name) {
    for (const ActionSpec& s : kStates) if (name == s.name) return true;
    return false;
}

bool IsDeathLabel(const std::string& name) {
    return name == "Death" || name == "DeadHold" || name.rfind("Death_", 0) == 0 || name.rfind("DeadHold_", 0) == 0;
}

bool IsSwimLabel(const std::string& name) {
    return name == "Swim" || name.rfind("Swim_", 0) == 0;
}

void LatchSwim(SM& sm, float speed) {
    sm.SetBool("swimming", true);
    if (speed >= 0.0f) sm.SetFloat("swimSpeed", speed);
}

int DirOfClip(const std::string& name) {
    if (name.size() < 2) return -1;
    const char d = name.back();
    if (d == 'F') return 0;
    if (d == 'B') return 1;
    if (d == 'L') return 2;
    if (d == 'R') return 3;
    return -1;
}

SM* BaseMachine(GameObject* go) {
    if (!go) return nullptr;
    Character* body = FindAnimComponent<Character>(go);
    GameObject* host = (body && body->gameObject) ? body->gameObject : go;
    for (SM* sm : host->GetComponents<SM>())
        if (sm && sm->layer == 0) return sm;
    return FindAnimComponent<SM>(go);
}

int DirFromWorld(GameObject* go, const Vec3& fromWorld) {
    if (!go || !go->transform) return 0;
    Vec3 away = go->transform->Position() - fromWorld;
    away.y = 0.0f;
    if (away.SqrMagnitude() < 1e-6f) return 0;
    // The blocky body is authored facing +Z and then turned to face -Z, which
    // is the direction the player controllers move. Classify the fall in that frame.
    Vec3 facing = go->transform->TransformDirection(Vec3{0.0f, 0.0f, -1.0f});
    Vec3 right = go->transform->TransformDirection(Vec3{1.0f, 0.0f, 0.0f});
    facing.y = 0.0f;
    right.y = 0.0f;
    const float f = Vec3::Dot(away, facing);
    const float r = Vec3::Dot(away, right);
    if (std::fabs(f) >= std::fabs(r)) return f >= 0.0f ? 0 : 1;
    return r >= 0.0f ? 3 : 2;
}

void BakeSit(std::vector<HumanoidLibrary::Entry>& out) {
    // hipY is a drop from the standing hip height, in metres (idle 0, crouch -0.17, prone -0.78). The sit
    // poses below were written as the hip height above the floor (0.48, 0.22, 0.32), which RAISED the body
    // about 0.5 m instead of lowering it. kRestHip is the standing hip height above the soles
    // (hip to ankle 0.80 + ankle 0.12, as in HumanoidMotion), so a hip height h above the floor is h - kRestHip.
    constexpr float kRestHip = 0.92f;
    const KeyPose idle = FromState(1);
    KeyPose chair = idle;
    chair.bones[B::B_HIPS].x = -6.0f;
    chair.bones[B::B_TORSO].x = 6.0f;
    chair.bones[B::B_LTHIGH] = {86.0f, 10.0f, -8.0f};
    chair.bones[B::B_RTHIGH] = {86.0f, -10.0f, 8.0f};
    chair.bones[B::B_LSHIN] = {-78.0f, 0.0f, 0.0f};
    chair.bones[B::B_RSHIN] = {-78.0f, 0.0f, 0.0f};
    chair.bones[B::B_LUPARM] = {-18.0f, 0.0f, 22.0f};
    chair.bones[B::B_RUPARM] = {-18.0f, 0.0f, -22.0f};
    chair.bones[B::B_LFORE].x = -24.0f;
    chair.bones[B::B_RFORE].x = -24.0f;
    // Thigh 86 deg, shin 78 deg back: the soles are on the floor with the hips 0.57 m up (thigh 0.37, shin 0.43,
    // ankle 0.12), a hip joint about 0.12 m over a 0.45 m seat. The root (feet) stays on the floor under the seat.
    chair.hipY = 0.57f - kRestHip;

    KeyPose ground = idle;
    ground.bones[B::B_HIPS].x = 10.0f;
    ground.bones[B::B_TORSO].x = 8.0f;
    ground.bones[B::B_LTHIGH] = {68.0f, 26.0f, -28.0f};
    ground.bones[B::B_RTHIGH] = {68.0f, -26.0f, 28.0f};
    ground.bones[B::B_LSHIN] = {-108.0f, 0.0f, 8.0f};
    ground.bones[B::B_RSHIN] = {-108.0f, 0.0f, -8.0f};
    ground.bones[B::B_LUPARM] = {-24.0f, 0.0f, 16.0f};
    ground.bones[B::B_RUPARM] = {-24.0f, 0.0f, -16.0f};
    ground.hipY = 0.22f - kRestHip;

    KeyPose knees = ground;
    knees.bones[B::B_LTHIGH] = {42.0f, 8.0f, -10.0f};
    knees.bones[B::B_RTHIGH] = {42.0f, -8.0f, 10.0f};
    knees.bones[B::B_LSHIN] = {-96.0f, 0.0f, 0.0f};
    knees.bones[B::B_RSHIN] = {-96.0f, 0.0f, 0.0f};
    knees.hipY = 0.32f - kRestHip;

    KeyPose chairBreath = chair;
    chairBreath.bones[B::B_TORSO].x += 3.0f;
    chairBreath.bones[B::B_HEAD].x -= 4.0f;
    KeyPose groundBreath = ground;
    groundBreath.bones[B::B_TORSO].x += 2.5f;
    groundBreath.bones[B::B_HEAD].y += 6.0f;

    // On Sit / On Stand are the state enter events. Clips only mark action_end.
    AddClip(out, BakeAction("SitDown_Chair", {At(idle, 0.0f), At(chair, 0.50f)}, false, {{0.50f, "action_end"}}));
    AddClip(out, BakeAction("SitDown_Ground", {At(idle, 0.0f), At(ground, 0.55f)}, false, {{0.55f, "action_end"}}));
    AddClip(out, BakeAction("StandUp_Chair", {At(chair, 0.0f), At(idle, 0.55f)}, false, {{0.55f, "action_end"}}));
    AddClip(out, BakeAction("StandUp_Ground", {At(ground, 0.0f), At(idle, 0.60f)}, false, {{0.60f, "action_end"}}));
    AddClip(out, BakeAction("Sit_Chair", {At(chair, 0.0f), At(chairBreath, 1.1f), At(chair, 2.2f)}, true, {}));
    AddClip(out, BakeAction("Sit_Ground", {At(ground, 0.0f), At(groundBreath, 1.2f), At(ground, 2.4f)}, true, {}));
    AddClip(out, BakeAction("Sit_Ground_Knees", {At(knees, 0.0f), At(knees, 2.0f)}, true, {}));
}

void LatchDeath(SM& sm, int dir) {
    if (dir < 0) dir = 0;
    if (dir > 3) dir = 3;
    sm.SetFloat("deathDir", (float)dir);
    sm.SetBool("dead", true);
}

} // namespace

using namespace humanoid_actions;

void BakeVault(std::vector<HumanoidLibrary::Entry>& out) {
    const KeyPose idle = FromState(1);
    const KeyPose hang = FromState(19);
    auto plant = [](KeyPose k, float hip) {
        k.bones[B::B_LUPARM] = {-140.0f, -20.0f, 16.0f};
        k.bones[B::B_RUPARM] = {-140.0f, 20.0f, -16.0f};
        k.bones[B::B_LFORE].x = -20.0f;
        k.bones[B::B_RFORE].x = -20.0f;
        k.bones[B::B_TORSO].x = 20.0f;
        k.hipY = hip;
        return k;
    };
    KeyPose over = plant(idle, 0.15f);
    over.bones[B::B_LTHIGH].x = 50.0f;
    over.bones[B::B_RTHIGH].x = 20.0f;
    AddClip(out, BakeAction("Vault", {At(idle, 0.0f), At(plant(idle, 0.05f), 0.18f), At(over, 0.36f), At(idle, 0.60f)}, false, {{0.60f, "action_end"}}));
    AddClip(out, BakeAction("Mantle_Low", {At(idle, 0.0f), At(plant(idle, 0.1f), 0.2f), At(idle, 0.5f)}, false, {{0.50f, "action_end"}}));
    KeyPose high = plant(idle, 0.35f);
    high.bones[B::B_LTHIGH].x = 70.0f;
    high.bones[B::B_RTHIGH].x = 70.0f;
    AddClip(out, BakeAction("Mantle_High", {At(idle, 0.0f), At(high, 0.3f), At(plant(idle, 0.2f), 0.55f), At(idle, 0.8f)}, false, {{0.80f, "action_end"}}));
    AddClip(out, BakeAction("LedgeGrab", {At(idle, 0.0f), At(hang, 0.35f)}, false, {{0.35f, "action_end"}}));
    KeyPose shimL = hang; shimL.bones[B::B_HIPS].z = 8.0f; shimL.bones[B::B_TORSO].z = 10.0f;
    KeyPose shimR = hang; shimR.bones[B::B_HIPS].z = -8.0f; shimR.bones[B::B_TORSO].z = -10.0f;
    AddClip(out, BakeAction("Hang_Shimmy_L", {At(hang, 0.0f), At(shimL, 0.25f), At(hang, 0.5f)}, true, {}));
    AddClip(out, BakeAction("Hang_Shimmy_R", {At(hang, 0.0f), At(shimR, 0.25f), At(hang, 0.5f)}, true, {}));
    AddClip(out, BakeAction("ClimbUp", {At(hang, 0.0f), At(plant(idle, 0.2f), 0.35f), At(idle, 0.7f)}, false, {{0.70f, "action_end"}}));
    AddClip(out, BakeAction("LetGo", {At(hang, 0.0f), At(idle, 0.3f)}, false, {{0.30f, "action_end"}}));
}

void Bake(std::vector<HumanoidLibrary::Entry>& out) {
    BakeDeath(out);
    BakeDive(out);
    BakeSit(out);
    BakeHits(out);
    BakeRoll(out);
    BakeVault(out);
    BakeSwim(out);
    BakeEmotes(out);
}


void MergeMissing(AnimStateMachine& sm, const Character* body, bool force) {
    // The layer callback is not saved. Put it back even when the graph is current.
    BindHitLayer(sm);
    const bool looksDefault = HasState(sm, "Idle") && HasState(sm, "Walk") && HasState(sm, "Run") && HasState(sm, "Jump");
    if (!force && !looksDefault) return;
    if (!force && sm.actionsRev >= kActionsRev) return;
    // AddStates only creates states that are absent, which is the whole merge.
    AddStates(sm, body, force);
}
} // namespace HumanoidActions

} // namespace okay
