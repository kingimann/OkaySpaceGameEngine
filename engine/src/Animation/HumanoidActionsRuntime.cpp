#include "okay/Animation/HumanoidActions.hpp"
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
#include "HumanoidActionsInternal.hpp"

namespace okay {
namespace HumanoidActions {

using namespace humanoid_actions;

using SM = AnimStateMachine;

const std::vector<std::string>& ActionNames() {
    static const std::vector<std::string> names = {
        "Death", "DeadHold",
        "Death_F", "Death_B", "Death_L", "Death_R",
        "DeadHold_F", "DeadHold_B", "DeadHold_L", "DeadHold_R",
        "Dive", "DiveLand",
        "SitDown", "Sit", "StandUp", "SitDown_Chair", "Sit_Chair", "StandUp_Chair", "SitDown_Ground", "Sit_Ground", "Sit_Ground_Knees", "StandUp_Ground",
        "Hit", "HitHeavy", "Hit_Crouch", "Hit_Prone", "Hit_F", "Hit_B", "Hit_L", "Hit_R", "Hit_Heavy_F", "Hit_Heavy_B", "Hit_Off",
        "Roll", "Dodge", "Slide", "SlideEnd", "Roll_F", "Dodge_L", "Dodge_R", "Dodge_B",
        "Vault", "MantleLow", "MantleHigh", "LedgeGrab", "HangShimmy", "ClimbUp", "LetGo", "Mantle_Low", "Mantle_High", "Hang_Shimmy_L", "Hang_Shimmy_R",
        "Swim", "Swim_Tread", "Swim_Fwd",
        "Emote_Nod", "Emote_Shake", "Emote_Shrug"};
    return names;
}

bool IsInAction(GameObject* go) {
    SM* sm = BaseMachine(go);
    if (sm && IsKnownAction(sm->Current())) return true;
    if (!go) return false;
    for (AnimStateMachine* layer : go->GetComponents<AnimStateMachine>())
        if (layer->layer > 0 && layer->Current() == "Hit") return true;
    return false;
}

float ActionTime(GameObject* go) {
    return IsInAction(go) ? BaseMachine(go)->StateTime() : 0.0f;
}

bool IsDead(GameObject* go);

bool CanDive(GameObject* go) {
    if (!go || IsDead(go)) return false;
    SM* sm = BaseMachine(go);
    if (sm) {
        const std::string cur = sm->Current();
        if (cur == "Dive" || cur == "DiveLand") return false;
    }
    bool grounded = sm && sm->GetBool("grounded");
    Rigidbody3D* rb = go->GetComponent<Rigidbody3D>();
    if (rb && rb->groundedOnTerrain) grounded = true;
    if (!grounded && go->transform && go->scene()) {
        const Vec3 o = go->transform->Position();
        const RaycastHit3D h = go->scene()->physics3D().Raycast(
            *go->scene(), o + Vec3{0.0f, 0.2f, 0.0f}, Vec3{0.0f, -1.0f, 0.0f}, 0.45f,
            go, Physics3D::kAllLayers, false);
        grounded = h.hit && (!rb || rb->velocity.y < 1.5f);
    }
    if (!grounded) return false;
    if (sm && (sm->GetBool("sprint") || sm->GetBool("run") || sm->GetFloat("speed") > 3.0f)) return true;
    if (rb) {
        const float hs = std::sqrt(rb->velocity.x * rb->velocity.x + rb->velocity.z * rb->velocity.z);
        if (hs > 3.0f) return true;
    }
    return false;
}

void RequestDive(GameObject* go) {
    if (!CanDive(go)) return;
    SM* sm = BaseMachine(go);
    if (!sm) return;
    sm->SetBool("divePending", true);
    sm->SetBool("grounded", true);
    sm->SetBool("sprint", true);
    if (HasState(*sm, "Dive") && sm->Current() != "Dive") sm->GoTo("Dive", 0.12f);
}

bool BeginDive(GameObject* go) {
    SM* sm = BaseMachine(go);
    if (!sm || !HasState(*sm, "Dive")) return false;
    sm->SetTrigger("dived");
    if (sm->Current() == "Dive") return true;
    const bool ok = sm->GoTo("Dive", 0.12f);
    if (ok) NoteNetEvent(go, 5, 0.0f);
    return ok;
}

bool TakeDivePending(GameObject* go) {
    SM* sm = BaseMachine(go);
    if (!sm || !sm->GetBool("divePending")) return false;
    sm->SetBool("divePending", false);
    return true;
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
            if (name == "Dive") sm->SetBool("divePending", true);
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
        for (AnimStateMachine* layer : go->GetComponents<AnimStateMachine>()) {
            if (layer->layer <= 0) continue;
            if (HasState(*layer, name.c_str()) && layer->GoTo(name, blend)) {
                layer->layerWeight = 1.0f;
                return true;
            }
            for (const SM::State& st : layer->states)
                for (const SM::Motion& m : st.motions) {
                    if (m.clip != name) continue;
                    if (!st.blendParam.empty()) layer->SetFloat(st.blendParam, m.threshold);
                    if (layer->GoTo(st.name, blend)) { layer->layerWeight = 1.0f; return true; }
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

int EmoteIndex(const std::string& name) {
    std::string k;
    k.reserve(name.size());
    for (char c : name) k += (char)std::tolower((unsigned char)c);
    if (k == "nod" || k == "emote_nod" || k == "0") return 0;
    if (k == "shake" || k == "headshake" || k == "emote_shake" || k == "1") return 1;
    if (k == "shrug" || k == "emote_shrug" || k == "2") return 2;
    return -1;
}

bool PlayEmote(GameObject* go, int which) {
    if (!go) return false;
    if (which < 0) which = 0;
    if (which > 2) which = 2;
    if (SM* sm = BaseMachine(go)) sm->SetInt("emoteId", which);
    const bool ok = Play(go, kEmoteNames[which], 0.12f);
    if (ok) NoteNetEvent(go, 8, (float)which);
    return ok;
}

bool PlayEmote(GameObject* go, const std::string& name) {
    const int which = EmoteIndex(name);
    if (which < 0) return false;
    return PlayEmote(go, which);
}

bool IsDead(GameObject* go) {
    if (!go) return false;
    if (auto* hp = go->GetComponent<HealthStat>()) return hp->IsDead();
    if (SM* sm = BaseMachine(go)) return sm->GetBool("dead");
    return false;
}

namespace {

bool FlatFloor(GameObject* go) {
    if (!go || !go->transform || !go->scene()) return true;
    const Vec3 o = go->transform->Position();
    const RaycastHit3D h = go->scene()->physics3D().Raycast(
        *go->scene(), o + Vec3{0.0f, 0.2f, 0.0f}, Vec3{0.0f, -1.0f, 0.0f}, 0.8f,
        go, Physics3D::kAllLayers, false);
    return h.hit && h.normal.y > 0.75f;
}

Seat* SeatHolding(GameObject* who) {
    if (!who || !who->scene()) return nullptr;
    for (const auto& up : who->scene()->Objects()) {
        if (!up) continue;
        if (Seat* s = up->GetComponent<Seat>())
            if (s->Occupant() == who) return s;
    }
    return nullptr;
}

Seat* NearestSeat(GameObject* go) {
    if (!go || !go->transform || !go->scene()) return nullptr;
    Seat* best = nullptr;
    float bestD = 1.0e9f;
    const Vec3 p = go->transform->Position();
    for (const auto& up : go->scene()->Objects()) {
        if (!up || !up->active) continue;
        Seat* s = up->GetComponent<Seat>();
        if (!s || !s->transform || s->occupied) continue;
        Vec3 d = s->SitWorld() - p;
        d.y *= 0.25f;
        const float dist = d.Magnitude();
        if (dist <= s->useRadius && dist < bestD) { bestD = dist; best = s; }
    }
    return best;
}

} // namespace

bool RequestSit(GameObject* go, GameObject* seatObject, int sitType) {
    if (!go || IsDead(go) || IsSitting(go) || VehicleSeat::IsSeated(go)) return false;
    Seat* seat = seatObject ? seatObject->GetComponent<Seat>() : nullptr;
    if (seat) {
        if (seat->occupied && seat->Occupant() != go) return false;
        sitType = (seat->type == 1) ? 1 : 0;
    } else if (!FlatFloor(go)) return false;
    if (sitType < 0) sitType = 0;
    if (sitType > 2) sitType = 2;
    SM* sm = BaseMachine(go);
    if (sm && HasState(*sm, "SitDown")) {
        sm->SetFloat("sitType", (float)sitType);
        sm->SetBool("sitting", true);
        // GoTo, not the trigger: a latched `sit` would re-enter SitDown every frame.
        sm->GoTo("SitDown", 0.2f);
    }
    if (seat) seat->Claim(go);
    return true;
}

bool BeginVehicleSit(GameObject* go) {
    if (!go || IsDead(go)) return false;
    SM* sm = BaseMachine(go);
    if (!sm || !HasState(*sm, "SitDown")) return false;
    sm->SetFloat("sitType", 0.0f);
    sm->SetBool("sitting", true);
    sm->GoTo("SitDown", 0.2f);
    return true;
}

void RequestStandUp(GameObject* go) {
    if (!go) return;
    // Stand-up is already playing. Calling this again (move held) would restart it.
    if (SM* sm = BaseMachine(go)) if (sm->Current() == "StandUp") return;
    if (Seat* seat = SeatHolding(go)) seat->Release();
    if (SM* sm = BaseMachine(go)) {
        sm->SetBool("sitting", false);
        const float type = sm->GetFloat("sitType");
        if (HasState(*sm, "StandUp")) {
            // StandUp only has chair (0) and ground (1). Knees uses the ground clip.
            sm->SetFloat("sitType", type >= 1.5f ? 1.0f : type);
            sm->GoTo("StandUp", 0.12f);
        }
    }
}

bool IsSitting(GameObject* go) {
    SM* sm = BaseMachine(go);
    if (!sm) return false;
    if (sm->GetBool("sitting")) return true;
    const std::string c = sm->Current();
    return c == "SitDown" || c == "Sit" || c == "StandUp";
}

void Interact(GameObject* go) {
    if (!go) return;
    if (IsSitting(go)) { RequestStandUp(go); return; }
    if (Seat* s = NearestSeat(go)) RequestSit(go, s->gameObject, s->type);
    else RequestSit(go, nullptr, 1);
}

std::string SeatPrompt(GameObject* go) {
    if (Seat* s = NearestSeat(go)) return s->prompt;
    return {};
}

int HitDirFromWorld(GameObject* go, const Vec3& fromWorld) {
    // Death falls away from the hit. A flinch faces the hit, so swap the axis.
    static const int kOpp[] = {1, 0, 3, 2};
    return kOpp[DirFromWorld(go, fromWorld)];
}

namespace {
bool HitReactNow(GameObject* go, int dir, bool heavy);
}
bool HitReact(GameObject* go, int dir, bool heavy) {
    if (dir < 0) dir = 0;
    if (dir > 3) dir = 3;
    const bool ok = HitReactNow(go, dir, heavy);
    if (ok) NoteNetEvent(go, 6, (float)(dir + (heavy ? 4 : 0)));
    return ok;
}
namespace {
bool HitReactNow(GameObject* go, int dir, bool heavy) {
    if (!go || IsDead(go)) return false;
    if (dir < 0) dir = 0;
    if (dir > 3) dir = 3;
    static std::unordered_map<const GameObject*, float> last;
    const float now = Time::ElapsedTime();
    if (auto it = last.find(go); it != last.end() && now - it->second < 0.1f) return false;
    if (last.size() > 512) last.clear();
    last[go] = now;

    SM* sm = BaseMachine(go);
    const bool prone = sm && sm->GetBool("prone");
    const bool crouch = sm && sm->GetBool("crouch") && !prone;
    if (sm) {
        sm->SetFloat("hitDir", (float)dir);
        sm->SetBool("hitHeavy", heavy && !crouch && !prone);
    }
    auto goBase = [&](const char* state) {
        return sm && HasState(*sm, state) && sm->GoTo(state, 0.12f);
    };
    if (prone && goBase("Hit_Prone")) return true;
    if (crouch && goBase("Hit_Crouch")) return true;
    if (heavy && goBase("HitHeavy")) return true;
    if (sm && sm->gameObject) {
        for (AnimStateMachine* layer : sm->gameObject->GetComponents<AnimStateMachine>()) {
            if (layer->layer <= 0 || layer->maskBone != "Torso" || !HasState(*layer, "Hit")) continue;
            layer->layerWeight = 1.0f;
            if (layer->GoTo("Hit", 0.08f)) return true;
        }
    }
    static const char* kLight[] = {"Hit_F", "Hit_B", "Hit_L", "Hit_R"};
    static const char* kHeavy[] = {"Hit_Heavy_F", "Hit_Heavy_B", "Hit_Heavy_F", "Hit_Heavy_B"};
    return Play(go, heavy ? kHeavy[dir] : kLight[dir], 0.1f);
}
} // namespace

float StaggerScale(const GameObject* go) {
    const SM* sm = BaseMachine(const_cast<GameObject*>(go));
    if (!sm) return 1.0f;
    const std::string c = sm->Current();
    if (c != "HitHeavy" && c != "Hit_Crouch" && c != "Hit_Prone") return 1.0f;
    const float s = sm->GetFloat("staggerScale");
    return s > 0.0f ? s : 1.0f;
}

namespace {
std::unordered_set<const GameObject*> g_dodgeIFrames;
}

void SetDodgeIFrames(GameObject* go, bool on) {
    if (!go) return;
    if (on) g_dodgeIFrames.insert(go);
    else g_dodgeIFrames.erase(go);
}
bool DodgeIFrames(const GameObject* go) {
    return go && g_dodgeIFrames.count(go) != 0;
}

bool RequestRoll(GameObject* go, int dir) {
    if (!go || IsDead(go)) return false;
    if (dir < 0) dir = 0;
    if (dir > 3) dir = 3;
    if (SM* sm = BaseMachine(go)) {
        sm->SetFloat("dodgeDir", (float)dir);
        if (dir == 0 && HasState(*sm, "Roll") && sm->GoTo("Roll", 0.1f)) return true;
        if (HasState(*sm, "Dodge") && sm->GoTo("Dodge", 0.1f)) return true;
    }
    static const char* k[] = {"Roll_F", "Dodge_B", "Dodge_L", "Dodge_R"};
    return Play(go, k[dir], 0.1f);
}

bool RequestDodge(GameObject* go, int dir) {
    if (!go || IsDead(go)) return false;
    if (dir < 0) dir = 0;
    if (dir > 3) dir = 3;
    Vec3 world{0.0f, 0.0f, -1.0f};
    if (go->transform) {
        const Vec3 facing = go->transform->TransformDirection(Vec3{0.0f, 0.0f, -1.0f});
        const Vec3 right = go->transform->TransformDirection(Vec3{1.0f, 0.0f, 0.0f});
        world = facing;
        if (dir == 1) world = facing * -1.0f;
        else if (dir == 2) world = right * -1.0f;
        else if (dir == 3) world = right;
    }
    auto* fp = go->GetComponent<FirstPersonController>();
    auto* tp = fp ? nullptr : go->GetComponent<ThirdPersonController>();
    if (fp || tp) {
        DodgeMove& move = fp ? fp->dodge : tp->dodge;
        if (!DodgeBurst(go, move, world)) return false;
    }
    return RequestRoll(go, dir);
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
    NoteNetEvent(go, 7, p.kind == TraverseKind::MantleLow ? 1.0f : p.kind == TraverseKind::MantleHigh ? 2.0f
                        : p.kind == TraverseKind::Ledge ? 3.0f : 0.0f);
    BeginTraverse(go, p);
    if (p.kind == TraverseKind::Ledge) {
        auto it = TraverseJobs().find(go);
        if (it != TraverseJobs().end()) {
            it->second.rememberLip = true; it->second.lip = p.lip;
            it->second.haveEdge = true; it->second.edge = p.edge; it->second.edgeNormal = p.edgeNormal;
        }
    }
    return true;
}

} // namespace

bool PlayTraverse(GameObject* go, TraverseClip clip) {
    if (!go || IsDead(go)) return false;
    static const char* kState[] = {"Vault", "MantleLow", "MantleHigh", "LedgeGrab", "ClimbUp", "LetGo"};
    static const char* kClip[] = {"Vault", "Mantle_Low", "Mantle_High", "LedgeGrab", "ClimbUp", "LetGo"};
    const int i = (int)clip;
    if (i < 0 || i > 5) return false;
    if (clip == TraverseClip::LedgeGrab) SetHanging(go, true);
    else if (clip == TraverseClip::ClimbUp || clip == TraverseClip::LetGo) SetHanging(go, false);
    const bool ok = PlayNamed(go, kState[i], kClip[i]);
    if (ok) NoteNetEvent(go, 7, (float)i);
    return ok;
}

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
    TraverseValidate(go);
    AbortTraverse(go);
    SetHanging(go, false);
    const bool played = PlayNamed(go, "ClimbUp", "ClimbUp");
    if (played) NoteNetEvent(go, 7, 4.0f);
    auto it = HangPoints().find(go);
    if (it != HangPoints().end() && go->transform) {
        TraverseProbe p;
        p.kind = TraverseKind::MantleHigh;
        p.from = go->transform->Position();
        p.to = it->second;
        p.lip = it->second;
        p.duration = 0.7f;
        HangPoints().erase(it);
        HangEdges().erase(go);
        BeginTraverse(go, p);
    } else {
        RestoreHangGravity(go);
    }
    return played;
}

void RequestLetGo(GameObject* go) {
    if (!go) return;
    TraverseValidate(go);
    AbortTraverse(go);
    SetHanging(go, false);
    if (PlayNamed(go, "LetGo", "LetGo")) NoteNetEvent(go, 7, 5.0f);
    HangPoints().erase(go);
    HangEdges().erase(go);
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

void Seat::Claim(GameObject* who) {
    if (!who || !who->transform) return;
    m_who = who;
    occupied = true;
    m_from = who->transform->Position();
    m_fromRot = who->transform->Rotation();
    m_blend = 0.0f;
    if (auto* rb = who->GetComponent<Rigidbody3D>()) {
        rb->velocity = Vec3{0, 0, 0};
        m_savedGravity = rb->gravityScale;
        m_hadGravity = true;
        rb->gravityScale = 0.0f;
    }
}

void Seat::Release() {
    if (m_who && m_who->transform && transform && !m_who->IsDestroyed())
        m_who->transform->SetPosition(ExitWorld());
    if (m_who && !m_who->IsDestroyed()) {
        if (auto* rb = m_who->GetComponent<Rigidbody3D>()) {
            if (m_hadGravity) rb->gravityScale = m_savedGravity;
            rb->velocity = Vec3{0, 0, 0};
        }
    }
    m_who = nullptr;
    occupied = false;
    m_hadGravity = false;
    m_blend = 1.0f;
}

void Seat::Update(float dt) {
    if (m_who && (m_who->IsDestroyed() || !m_who->transform || HumanoidActions::IsDead(m_who))) {
        // Death (or the body going away) frees the seat. A body that is still there is put at the exit
        // point, the same place standing up uses, with its gravity back and no speed left over.
        Release();
    }
    if (m_who && m_who->transform && transform) {
        m_blend = std::min(1.0f, m_blend + dt / 0.45f);
        const float u = m_blend * m_blend * (3.0f - 2.0f * m_blend);
        m_who->transform->SetPosition(Vec3::Lerp(m_from, SitWorld(), u));
        m_who->transform->SetRotation(Quat::Slerp(m_fromRot, FaceWorld(), u));
        if (auto* rb = m_who->GetComponent<Rigidbody3D>()) rb->velocity = Vec3{0, 0, 0};
    }
    playerInRange = false;
    if (!gameObject || !gameObject->scene() || !transform || occupied) return;
    GameObject* who = nullptr;
    float best = useRadius;
    const Vec3 sit = SitWorld();
    for (const auto& up : gameObject->scene()->Objects()) {
        if (!up || !up->active || !up->transform || up.get() == gameObject) continue;
        if (!IsLocallyControlled(up.get()) || !up->GetComponent<Character>()) continue;
        if (VehicleSeat::IsSeated(up.get())) continue;   // already in a vehicle
        Vec3 d = up->transform->Position() - sit;
        d.y = 0.0f;
        const float dist = d.Magnitude();
        if (dist < best) { best = dist; who = up.get(); }
    }
    if (!who) return;
    bool closer = false;
    for (const auto& up : gameObject->scene()->Objects()) {
        Seat* other = up ? up->GetComponent<Seat>() : nullptr;
        if (!other || other == this || other->occupied || !other->transform) continue;
        Vec3 d = who->transform->Position() - other->SitWorld();
        d.y = 0.0f;
        if (d.Magnitude() < best) closer = true;
    }
    playerInRange = !closer;
    // The shared Interact action (E / pad X by default, rebindable). Lean steps aside for it (LeanKeyTaken).
    if (playerInRange && InteractPressed())
        HumanoidActions::RequestSit(who, gameObject, type);
}

} // namespace okay
