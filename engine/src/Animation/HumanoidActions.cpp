#include "okay/Animation/HumanoidActions.hpp"
#include "okay/Animation/HumanoidMotion.hpp"
#include "okay/Components/AnimControl.hpp"
#include "okay/Components/AnimStateMachine.hpp"
#include "okay/Components/Character.hpp"
#include "okay/Components/SurvivalComponents.hpp"
#include "okay/Core/Log.hpp"
#include "okay/Physics/Collider3D.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include <algorithm>
#include <cmath>
#include <string>

namespace okay {
namespace HumanoidActions {
namespace {

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

struct ActionSpec {
    const char* name;
    int rev;
};

const ActionSpec kStates[] = {{"Death", 1}, {"DeadHold", 1}, {"Swim", 2}};

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

void LatchDeath(SM& sm, int dir) {
    if (dir < 0) dir = 0;
    if (dir > 3) dir = 3;
    sm.SetFloat("deathDir", (float)dir);
    sm.SetBool("dead", true);
}

} // namespace

void Bake(std::vector<HumanoidLibrary::Entry>& out) {
    BakeDeath(out);
    BakeSwim(out);
}

void ApplyTimes(AnimStateMachine& sm) {
    const StanceTimes& t = sm.stanceTimes;
    for (SM::State& st : sm.states) {
        if (st.name != "Death" && st.name != "DeadHold" && st.name != "Swim") continue;
        for (SM::Transition& tr : st.transitions) {
            if (tr.to == "DeadHold") tr.blend = t.crouchProne;          // ends on the ground
            else if (tr.to == "GetUp" || tr.to == "Idle") tr.blend = t.proneStand;
        }
    }
    for (SM::Transition& tr : sm.anyState) {
        if (tr.to == "Death") tr.blend = t.standCrouch;
        else if (tr.to == "Swim") tr.blend = t.crouchProne;
    }
}

void AddStates(AnimStateMachine& sm, const Character* body, bool force) {
    (void)body;
    const int had = sm.actionsRev;
    const bool deathDue = force || had < 1;
    const bool swimDue = force || had < 2;
    const bool freshDeath = deathDue && !HasState(sm, "Death");
    const bool freshHold = deathDue && !HasState(sm, "DeadHold");
    float col = 40.0f;
    if (const SM::State* existing = FindMutable(sm, "Death")) col = existing->nx;
    else if (const SM::State* existing = FindMutable(sm, "DeadHold")) col = existing->nx;
    else {
        for (const SM::State& o : sm.states) col = std::max(col, o.nx);
        col += 220.0f;
    }
    if (freshDeath) {
        SM::State death = MakeDeathState();
        death.nx = col;
        death.ny = 40.0f;
        sm.states.push_back(std::move(death));
    }
    if (freshHold) {
        SM::State hold = MakeHoldState();
        hold.nx = col;
        hold.ny = 130.0f;
        sm.states.push_back(std::move(hold));
    }
    // Only fill transitions on states this call created. A saved Death keeps
    // the user's edges.
    if (freshDeath) if (SM::State* s = FindMutable(sm, "Death")) WireDeath(*s, sm);
    if (freshHold) if (SM::State* s = FindMutable(sm, "DeadHold")) WireHold(*s, sm);
    if (freshDeath || freshHold) {
        EnsureParam(sm, "deathDir", 0);
        EnsureParam(sm, "dead", 1);
        EnsureParam(sm, "die", 2);
        if (!HasAny(sm, "Death", "die")) {
            SM::Transition die;
            die.to = "Death";
            die.cond = C::Trigger;
            die.param = "die";
            die.blend = 0.2f;
            // None, not CurrentState: Death is already current while this blend
            // runs, and CurrentState would let the state being left (Walk, Jump)
            // cancel the fall. interruptible = false stops Any State after that.
            die.interruption = SM::Transition::Interrupt::None;
            sm.anyState.push_back(std::move(die));
        }
    }
    const bool freshSwim = swimDue && !HasState(sm, "Swim");
    if (freshSwim) {
        float scol = 40.0f;
        for (const SM::State& o : sm.states) scol = std::max(scol, o.nx);
        scol += 220.0f;
        SM::State swim = MakeSwimState();
        swim.nx = scol;
        swim.ny = 40.0f;
        sm.states.push_back(std::move(swim));
        if (SM::State* s = FindMutable(sm, "Swim")) WireSwim(*s, sm);
        EnsureParam(sm, "swimSpeed", 0);
        EnsureParam(sm, "swimming", 1);
        if (!HasAny(sm, "Swim", "swimming")) {
            SM::Transition go;
            go.to = "Swim";
            go.cond = C::BoolTrue;
            go.param = "swimming";
            go.blend = 0.25f;
            // None so Walk cannot cancel the blend. Swim itself is not
            // interruptible, so only its die / leave edges run after that.
            go.interruption = SM::Transition::Interrupt::None;
            // Before jump and fall, so a body already in the air still enters.
            sm.anyState.insert(sm.anyState.begin(), std::move(go));
        }
    }
    if (sm.actionsRev < kActionsRev) sm.actionsRev = kActionsRev;
    ApplyTimes(sm);
}

void MergeMissing(AnimStateMachine& sm, const Character* body, bool force) {
    const bool looksDefault = HasState(sm, "Idle") && HasState(sm, "Walk") && HasState(sm, "Run") && HasState(sm, "Jump");
    if (!force && !looksDefault) return;
    if (!force && sm.actionsRev >= kActionsRev) return;
    // AddStates only creates states that are absent, which is the whole merge.
    AddStates(sm, body, force);
}

const std::vector<std::string>& ActionNames() {
    static const std::vector<std::string> names = {
        "Death", "DeadHold",
        "Death_F", "Death_B", "Death_L", "Death_R",
        "DeadHold_F", "DeadHold_B", "DeadHold_L", "DeadHold_R",
        "Swim", "Swim_Tread", "Swim_Fwd"};
    return names;
}

bool IsInAction(GameObject* go) {
    SM* sm = BaseMachine(go);
    return sm && IsKnownAction(sm->Current());
}

float ActionTime(GameObject* go) {
    return IsInAction(go) ? BaseMachine(go)->StateTime() : 0.0f;
}

int DirFromName(const std::string& name) {
    std::string k;
    k.reserve(name.size());
    for (char c : name) k += (char)std::tolower((unsigned char)c);
    if (k == "forward" || k == "f" || k == "0") return 0;
    if (k == "back" || k == "backward" || k == "b" || k == "1") return 1;
    if (k == "left" || k == "l" || k == "2") return 2;
    if (k == "right" || k == "r" || k == "3") return 3;
    return 0;
}

bool Play(GameObject* go, const std::string& name, float blend) {
    if (!go || name.empty()) return false;
    if (blend < 0.0f) blend = 0.2f;
    if (SM* sm = BaseMachine(go)) {
        if (HasState(*sm, name.c_str())) {
            if (IsDeathLabel(name)) {
                const int d = DirOfClip(name);
                LatchDeath(*sm, d >= 0 ? d : (int)std::lround(sm->GetFloat("deathDir")));
            }
            if (IsSwimLabel(name))
                LatchSwim(*sm, name == "Swim_Fwd" ? 1.5f : (name == "Swim" ? -1.0f : 0.0f));
            if (sm->GoTo(name, blend)) return true;
        }
        for (const SM::State& st : sm->states) {
            for (const SM::Motion& m : st.motions) {
                if (m.clip != name) continue;
                if (!st.blendParam.empty()) sm->SetFloat(st.blendParam, m.threshold);
                if (IsDeathLabel(name) || IsDeathLabel(st.name)) LatchDeath(*sm, (int)std::lround(m.threshold));
                if (IsSwimLabel(name) || st.name == "Swim") LatchSwim(*sm, m.threshold);
                if (sm->GoTo(st.name, blend)) return true;
            }
        }
    }
    static bool warned = false;
    if (PlayClipFrom(go, name, warned)) return true;
    if (!warned) {
        warned = true;
        Log::Warning("play_action(\"", name, "\"): no Animator state or clip by that name");
    }
    return false;
}

bool PlayDeath(GameObject* go, const Vec3* hitFromWorld) {
    if (!go) return false;
    const int dir = hitFromWorld ? DirFromWorld(go, *hitFromWorld) : 0;
    static const char* kClip[] = {"Death_F", "Death_B", "Death_L", "Death_R"};
    if (SM* sm = BaseMachine(go)) {
        LatchDeath(*sm, dir);
        if (HasState(*sm, "Death")) return sm->GoTo("Death", 0.15f);
    }
    return Play(go, kClip[dir], 0.15f);
}

void ReviveAnim(GameObject* go) {
    if (SM* sm = BaseMachine(go)) sm->SetBool("dead", false);
}

void RequestDeath(GameObject* go, int dir, const Vec3* hitFromWorld) {
    if (!go) return;
    if (auto* hp = go->GetComponent<HealthStat>()) {
        Vec3 from = hitFromWorld ? *hitFromWorld : Vec3{};
        if (!hitFromWorld && go->transform) {
            // A named direction is a fall direction. The hit came from the opposite way.
            Vec3 facing = go->transform->TransformDirection(Vec3{0.0f, 0.0f, -1.0f});
            Vec3 right = go->transform->TransformDirection(Vec3{1.0f, 0.0f, 0.0f});
            Vec3 fall = facing;
            if (dir == 1) fall = facing * -1.0f;
            else if (dir == 2) fall = right * -1.0f;
            else if (dir == 3) fall = right;
            from = go->transform->Position() - fall;
            hp->KillFromAction(true, from);
            return;
        }
        hp->KillFromAction(hitFromWorld != nullptr, from);
        return;
    }
    if (hitFromWorld) PlayDeath(go, hitFromWorld);
    else {
        if (SM* sm = BaseMachine(go)) {
            LatchDeath(*sm, dir);
            if (HasState(*sm, "Death") && sm->GoTo("Death", 0.15f)) return;
        }
        static const char* kClip[] = {"Death_F", "Death_B", "Death_L", "Death_R"};
        Play(go, kClip[dir < 0 ? 0 : (dir > 3 ? 3 : dir)], 0.15f);
    }
}

void RequestRevive(GameObject* go) {
    if (!go) return;
    if (auto* hp = go->GetComponent<HealthStat>()) {
        if (hp->IsDead()) { hp->Revive(); return; }
    }
    ReviveAnim(go);
}

void SetSwimming(GameObject* go, bool on, float speed) {
    if (SM* sm = BaseMachine(go)) {
        sm->SetBool("swimming", on);
        if (on && speed >= 0.0f) sm->SetFloat("swimSpeed", speed);
    }
}

bool IsSwimming(GameObject* go) {
    SM* sm = BaseMachine(go);
    if (!sm) return false;
    if (sm->GetBool("swimming")) return true;
    return sm->Current() == "Swim";
}

bool IsDead(GameObject* go) {
    if (!go) return false;
    if (auto* hp = go->GetComponent<HealthStat>()) return hp->IsDead();
    if (SM* sm = BaseMachine(go)) return sm->GetBool("dead");
    return false;
}

bool DeathPoseSettled(GameObject* go) {
    SM* sm = BaseMachine(go);
    if (!sm) return false;
    if (sm->Current() == "DeadHold") return true;
    return sm->Current() == "Death" && sm->StateTime() >= 1.05f;
}

void ShrinkDeadBody(GameObject* go, DeadShape& shape) {
    if (!go || shape.saved) return;
    if (auto* cap = go->GetComponent<CapsuleCollider3D>()) {
        shape.hadCap = true;
        shape.capH = cap->height;
        shape.capOff = cap->offset.y;
        const float h = std::max(cap->height * 0.65f, 2.0f * cap->radius);
        const float feet = cap->offset.y - cap->height * 0.5f;
        cap->height = h;
        cap->offset.y = feet + h * 0.5f;
    }
    if (auto* box = go->GetComponent<BoxCollider3D>()) {
        shape.hadBox = true;
        shape.boxY = box->size.y;
        shape.boxOff = box->offset.y;
        const float h = box->size.y * 0.65f;
        const float feet = box->offset.y - box->size.y * 0.5f;
        box->size.y = h;
        box->offset.y = feet + h * 0.5f;
    }
    shape.saved = shape.hadCap || shape.hadBox;
}

void RestoreDeadBody(GameObject* go, DeadShape& shape) {
    if (!go || !shape.saved) return;
    if (shape.hadCap) if (auto* cap = go->GetComponent<CapsuleCollider3D>()) {
        cap->height = shape.capH;
        cap->offset.y = shape.capOff;
    }
    if (shape.hadBox) if (auto* box = go->GetComponent<BoxCollider3D>()) {
        box->size.y = shape.boxY;
        box->offset.y = shape.boxOff;
    }
    shape = {};
}

} // namespace HumanoidActions
} // namespace okay
