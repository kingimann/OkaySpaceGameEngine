#include "okay/Animation/HumanoidActions.hpp"
#include "okay/Animation/HumanoidMotion.hpp"
#include "okay/Components/AnimControl.hpp"
#include "okay/Components/AnimStateMachine.hpp"
#include "okay/Components/Character.hpp"
#include "okay/Components/SurvivalComponents.hpp"
#include "okay/Components/Traversal.hpp"
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

struct ActionSpec {
    const char* name;
    int rev;
};

const ActionSpec kStates[] = {
    {"Death", 1}, {"DeadHold", 1},
    {"Vault", 2}, {"MantleLow", 2}, {"MantleHigh", 2}, {"LedgeGrab", 2},
    {"HangShimmy", 2}, {"ClimbUp", 2}, {"LetGo", 2}};

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

bool IsKnownAction(const std::string& name) {
    for (const ActionSpec& s : kStates) if (name == s.name) return true;
    return false;
}

bool IsDeathLabel(const std::string& name) {
    return name == "Death" || name == "DeadHold" || name.rfind("Death_", 0) == 0 || name.rfind("DeadHold_", 0) == 0;
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
    BakeVault(out);
}

void ApplyTimes(AnimStateMachine& sm) {
    const StanceTimes& t = sm.stanceTimes;
    for (SM::State& st : sm.states) {
        if (st.name != "Death" && st.name != "DeadHold") continue;
        for (SM::Transition& tr : st.transitions) {
            if (tr.to == "DeadHold") tr.blend = t.crouchProne;          // ends on the ground
            else if (tr.to == "GetUp" || tr.to == "Idle") tr.blend = t.proneStand;
        }
    }
    for (SM::Transition& tr : sm.anyState)
        if (tr.to == "Death") tr.blend = t.standCrouch;
}

void AddOneShot(SM& sm, const char* name, const char* clip, float col, float ny, const char* to) {
    if (HasState(sm, name)) return;
    SM::State s;
    s.name = name; s.clip = clip; s.loop = false; s.ikWeight = 0.0f;
    // Fall / Jump Any-State would cut a vault the moment the body leaves the
    // ground. Own edges still run, including die.
    s.interruptible = false;
    s.nx = col; s.ny = ny;
    if (HasState(sm, "Death")) {
        SM::Transition die;
        die.to = "Death"; die.cond = C::Trigger; die.param = "die"; die.blend = 0.12f;
        die.interruption = SM::Transition::Interrupt::None;
        s.transitions.push_back(die);
    }
    if (to) {
        SM::Transition done;
        done.to = to; done.cond = C::OnClipEnd; done.hasExitTime = true; done.exitTime = 1.0f; done.blend = 0.15f;
        s.transitions.push_back(done);
    }
    sm.states.push_back(std::move(s));
}

void AddStates(AnimStateMachine& sm, const Character* body, bool force) {
    (void)body;
    const int had = sm.actionsRev;
    const bool deathDue = force || had < 1;
    const bool vaultDue = force || had < 2;
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
    if (vaultDue) {
        float col = 40.0f;
        for (const SM::State& o : sm.states) col = std::max(col, o.nx);
        col += 220.0f;
        const char* home = HasState(sm, "Idle") ? "Idle" : nullptr;
        const char* drop = HasState(sm, "Fall") ? "Fall" : home;
        AddOneShot(sm, "Vault", "Vault", col, 40.0f, home);
        AddOneShot(sm, "MantleLow", "Mantle_Low", col, 130.0f, home);
        AddOneShot(sm, "MantleHigh", "Mantle_High", col, 220.0f, home);
        AddOneShot(sm, "LedgeGrab", "LedgeGrab", col, 310.0f, HasState(sm, "Hang") ? "Hang" : home);
        AddOneShot(sm, "ClimbUp", "ClimbUp", col, 400.0f, home);
        AddOneShot(sm, "LetGo", "LetGo", col, 490.0f, drop);
        if (!HasState(sm, "HangShimmy")) {
            SM::State s;
            s.name = "HangShimmy"; s.blendTree = true; s.blendDims = 1; s.blendParam = "shimmy";
            s.loop = true; s.ikWeight = 0.0f; s.interruptible = false;
            SM::Motion l; l.clip = "Hang_Shimmy_L"; l.threshold = -1.0f;
            SM::Motion r; r.clip = "Hang_Shimmy_R"; r.threshold = 1.0f;
            s.motions = {l, r};
            s.nx = col; s.ny = 580.0f;
            SM::Transition back;
            back.to = HasState(sm, "Hang") ? "Hang" : (home ? home : "");
            if (HasState(sm, "Death")) {
                SM::Transition die;
                die.to = "Death"; die.cond = C::Trigger; die.param = "die"; die.blend = 0.12f;
                die.interruption = SM::Transition::Interrupt::None;
                s.transitions.push_back(die);
            }
            if (!back.to.empty()) {
                back.cond = C::FloatLess; back.param = "shimmy"; back.value = 0.2f;
                SM::Condition neg; neg.cond = C::FloatGreater; neg.param = "shimmy"; neg.value = -0.2f;
                back.extra.push_back(neg);
                s.transitions.push_back(back);
            }
            sm.states.push_back(std::move(s));
        }
        if (SM::State* hang = FindMutable(sm, "Hang")) {
            bool has = false;
            for (const SM::Transition& t : hang->transitions) if (t.param == "hanging") has = true;
            if (!has && drop) {
                SM::Transition rel;
                rel.to = drop; rel.cond = C::BoolFalse; rel.param = "hanging"; rel.blend = 0.12f;
                hang->transitions.push_back(rel);
                // Fall's Any-State would yo-yo with `hanging` while the feet are off the floor.
                hang->interruptible = false;
                bool hasDie = false;
                for (const SM::Transition& t : hang->transitions) if (t.param == "die") hasDie = true;
                if (!hasDie && HasState(sm, "Death")) {
                    SM::Transition die;
                    die.to = "Death"; die.cond = C::Trigger; die.param = "die"; die.blend = 0.12f;
                    die.interruption = SM::Transition::Interrupt::None;
                    hang->transitions.insert(hang->transitions.begin(), die);
                }
            }
        }
        EnsureParam(sm, "vault", 2);
        EnsureParam(sm, "mantle", 2);
        EnsureParam(sm, "ledgeGrab", 2);
        EnsureParam(sm, "climbUp", 2);
        EnsureParam(sm, "letGo", 2);
        EnsureParam(sm, "hanging", 1);
        EnsureParam(sm, "shimmy", 0);
        auto trig = [&](const char* to, const char* param) {
            if (HasAny(sm, to, param) || !HasState(sm, to)) return;
            SM::Transition go;
            go.to = to; go.cond = C::Trigger; go.param = param; go.blend = 0.12f;
            go.interruption = SM::Transition::Interrupt::None;
            sm.anyState.push_back(std::move(go));
        };
        trig("Vault", "vault");
        trig("MantleLow", "mantle");
        trig("LedgeGrab", "ledgeGrab");
        trig("ClimbUp", "climbUp");
        trig("LetGo", "letGo");
        if (!HasAny(sm, "Hang", "hanging") && HasState(sm, "Hang")) {
            SM::Transition go;
            go.to = "Hang"; go.cond = C::BoolTrue; go.param = "hanging"; go.blend = 0.12f;
            go.interruption = SM::Transition::Interrupt::None;
            sm.anyState.push_back(std::move(go));
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
        "Vault", "MantleLow", "MantleHigh", "LedgeGrab", "HangShimmy", "ClimbUp", "LetGo",
        "Mantle_Low", "Mantle_High", "Hang_Shimmy_L", "Hang_Shimmy_R"};
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
            if (sm->GoTo(name, blend)) return true;
        }
        for (const SM::State& st : sm->states) {
            for (const SM::Motion& m : st.motions) {
                if (m.clip != name) continue;
                if (!st.blendParam.empty()) sm->SetFloat(st.blendParam, m.threshold);
                if (IsDeathLabel(name) || IsDeathLabel(st.name)) LatchDeath(*sm, (int)std::lround(m.threshold));
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

bool IsDead(GameObject* go) {
    if (!go) return false;
    if (auto* hp = go->GetComponent<HealthStat>()) return hp->IsDead();
    if (SM* sm = BaseMachine(go)) return sm->GetBool("dead");
    return false;
}

namespace {

Vec3 BodyForward(GameObject* go) {
    if (!go || !go->transform) return Vec3{0.0f, 0.0f, -1.0f};
    return go->transform->TransformDirection(Vec3{0.0f, 0.0f, -1.0f});
}

bool BodyAirborne(GameObject* go) {
    if (!go || !go->transform) return false;
    auto* rb = go->GetComponent<Rigidbody3D>();
    if (rb && rb->groundedOnTerrain) return false;
    if (go->scene()) {
        const Vec3 o = go->transform->Position() + Vec3{0.0f, 0.08f, 0.0f};
        const RaycastHit3D h = go->scene()->physics3D().Raycast(
            *go->scene(), o, Vec3{0.0f, -1.0f, 0.0f}, 0.22f, go, Physics3D::kAllLayers, false);
        if (h.hit && h.normal.y > 0.5f) return false;
    }
    return true;
}

bool PlayNamed(GameObject* go, const char* state, const char* clip) {
    if (SM* sm = BaseMachine(go)) {
        if (HasState(*sm, state) && sm->GoTo(state, 0.12f)) return true;
    }
    return Play(go, clip, 0.12f);
}

bool StartFromProbe(GameObject* go, const TraverseProbe& p) {
    if (!go || p.kind == TraverseKind::None || IsDead(go) || TraverseBusy(go)) return false;
    const char* state = "Vault";
    const char* clip = "Vault";
    if (p.kind == TraverseKind::MantleLow) { state = "MantleLow"; clip = "Mantle_Low"; }
    else if (p.kind == TraverseKind::MantleHigh) { state = "MantleHigh"; clip = "Mantle_High"; }
    else if (p.kind == TraverseKind::Ledge) { state = "LedgeGrab"; clip = "LedgeGrab"; }
    if (p.kind == TraverseKind::Ledge) SetHanging(go, true);
    PlayNamed(go, state, clip);
    BeginTraverse(go, p);
    if (p.kind == TraverseKind::Ledge) {
        auto it = TraverseJobs().find(go);
        if (it != TraverseJobs().end()) { it->second.rememberLip = true; it->second.lip = p.lip; }
    }
    return true;
}

} // namespace

bool RequestVault(GameObject* go) {
    if (!go || IsDead(go) || TraverseBusy(go)) return false;
    const TraverseProbe p = ProbeTraverse(go, BodyForward(go), BodyAirborne(go));
    if (p.kind != TraverseKind::Vault) return false;
    return StartFromProbe(go, p);
}

bool RequestMantle(GameObject* go) {
    if (!go || IsDead(go) || TraverseBusy(go)) return false;
    const TraverseProbe p = ProbeTraverse(go, BodyForward(go), BodyAirborne(go));
    if (p.kind != TraverseKind::MantleLow && p.kind != TraverseKind::MantleHigh) return false;
    return StartFromProbe(go, p);
}

bool RequestLedgeGrab(GameObject* go) {
    if (!go || IsDead(go) || TraverseBusy(go) || !BodyAirborne(go)) return false;
    const TraverseProbe p = ProbeTraverse(go, BodyForward(go), true);
    if (p.kind != TraverseKind::Ledge) return false;
    return StartFromProbe(go, p);
}

bool RequestClimbUp(GameObject* go) {
    if (!go || IsDead(go)) return false;
    AbortTraverse(go);
    SetHanging(go, false);
    const bool played = PlayNamed(go, "ClimbUp", "ClimbUp");
    auto it = HangPoints().find(go);
    if (it != HangPoints().end() && go->transform) {
        TraverseProbe p;
        p.kind = TraverseKind::MantleHigh;
        p.from = go->transform->Position();
        p.to = it->second;
        p.lip = it->second;
        p.duration = 0.7f;
        HangPoints().erase(it);
        BeginTraverse(go, p);
    } else {
        RestoreHangGravity(go);
    }
    return played;
}

void RequestLetGo(GameObject* go) {
    if (!go) return;
    AbortTraverse(go);
    SetHanging(go, false);
    PlayNamed(go, "LetGo", "LetGo");
    HangPoints().erase(go);
    RestoreHangGravity(go);
}

bool IsHanging(GameObject* go) {
    SM* sm = BaseMachine(go);
    if (!sm) return false;
    if (sm->GetBool("hanging")) return true;
    const std::string& c = sm->Current();
    return c == "Hang" || c == "LedgeGrab" || c == "HangShimmy";
}

void SetHanging(GameObject* go, bool on) {
    if (SM* sm = BaseMachine(go)) sm->SetBool("hanging", on);
}

void SetShimmy(GameObject* go, float v) {
    if (v > 1.0f) v = 1.0f;
    if (v < -1.0f) v = -1.0f;
    SM* sm = BaseMachine(go);
    if (!sm) return;
    sm->SetFloat("shimmy", v);
    if (std::fabs(v) <= 0.3f) return;
    if (!sm->GetBool("hanging") && sm->Current() != "Hang" && sm->Current() != "HangShimmy") return;
    if (sm->Current() == "HangShimmy") return;
    if (HasState(*sm, "HangShimmy")) sm->GoTo("HangShimmy", 0.1f);
}

bool TryTraverse(GameObject* go, const Vec3& fwd, bool airborne, bool vault, bool mantle, bool ledge) {
    if (!go || IsDead(go) || TraverseBusy(go) || IsHanging(go)) return false;
    if (!vault && !mantle && !ledge) return false;
    const TraverseProbe p = ProbeTraverse(go, fwd, airborne);
    if (p.kind == TraverseKind::Vault && !vault) return false;
    if ((p.kind == TraverseKind::MantleLow || p.kind == TraverseKind::MantleHigh) && !mantle) return false;
    if (p.kind == TraverseKind::Ledge && (!ledge || !airborne)) return false;
    if (p.kind == TraverseKind::None) return false;
    return StartFromProbe(go, p);
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
