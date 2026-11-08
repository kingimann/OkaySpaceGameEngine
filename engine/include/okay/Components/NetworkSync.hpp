#pragma once
// ---------------------------------------------------------------------------
// NetworkSync — drop-in networked transform. Put it on ANY object and its
// position + rotation replicate to every peer in the session, with no
// networking code. One peer is the authority (it broadcasts); everyone else
// eases their copy toward the latest state.
//
//   * authority = Host  : the server drives it, clients follow (platforms,
//                         doors, AI, pickups — the common case).
//   * authority = Mine  : THIS peer drives it (e.g. an object you own).
//   * authority = Manual: you set `owned` yourself / via script.
//
// Give the SAME `netId` to the matching object on each peer (left blank, it is
// taken from the object's name). Needs a NetworkManager somewhere in the scene.
// ---------------------------------------------------------------------------
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Net/NetworkManager.hpp"
#include "okay/Components/Character.hpp"
#include "okay/Animation/HumanoidLibrary.hpp"
#include "okay/Animation/HumanoidActions.hpp"
#include "okay/Components/AnimStateMachine.hpp"
#include <string>
#include <sstream>
#include <algorithm>
#include <cmath>

namespace okay {

/// Direction of travel (world x/z velocity) relative to the way `facing` points,
/// in degrees, + = toward its right: what Character::moveAngle wants.
inline float MoveAngleFor(const Transform& facing, float vx, float vz) {
    const Vec3 f = facing.Forward(), r = facing.Right();
    const float fwd = vx * f.x + vz * f.z, side = vx * r.x + vz * r.z;
    if (fwd == 0.0f && side == 0.0f) return 0.0f;
    return std::atan2(side, fwd) * 57.2957795f;
}

inline AnimStateMachine* BaseLayer(Character* c) {
    if (!c || !c->gameObject) return nullptr;
    for (const auto& comp : c->gameObject->Components())
        if (auto* m = dynamic_cast<AnimStateMachine*>(comp.get()))
            if (m->layer == 0) return m;
    return nullptr;
}

inline bool NameOrPrefix(const std::string& cur, const char* exact, const char* prefix) {
    return cur == exact || cur.rfind(prefix, 0) == 0;
}

inline bool ProxyInAction(const Character* c, int id) {
    AnimStateMachine* sm = BaseLayer(const_cast<Character*>(c));
    if (!sm) return false;
    const std::string& cur = sm->Current();
    switch (id) {
        case 1: return NameOrPrefix(cur, "Death", "Death_") || NameOrPrefix(cur, "DeadHold", "DeadHold_");
        case 2: return NameOrPrefix(cur, "Roll", "Roll_") || NameOrPrefix(cur, "Dodge", "Dodge_");
        case 3: return cur == "SitDown" || cur == "Sit" || cur == "StandUp";
        case 4: return NameOrPrefix(cur, "Swim", "Swim_");
        default: return cur == "Idle" || cur == "Walk" || cur == "Run" || cur.empty();
    }
}

inline int ClampDir(float param) {
    int d = (int)std::lround(param);
    if (d < 0) d = 0;
    if (d > 3) d = 3;
    return d;
}

/// The blend parameter for the action already playing: death direction, dodge
/// direction, sit type (10 + type means stand up), or swim speed.
inline void RefreshRemoteParam(Character* c, int id, float param) {
    AnimStateMachine* sm = BaseLayer(c);
    if (!sm) return;
    if (id == 1) sm->SetFloat("deathDir", (float)ClampDir(param));
    else if (id == 2) sm->SetFloat("dodgeDir", (float)ClampDir(param));
    else if (id == 3) {
        const bool up = param >= 10.0f;
        float type = up ? param - 10.0f : param;
        if (type < 0.0f) type = 0.0f;
        if (type > 2.0f) type = 2.0f;
        sm->SetFloat("sitType", type);
        sm->SetBool("sitting", !up);
    } else if (id == 4) {
        sm->SetBool("swimming", true);
        if (param < 0.0f) param = 0.0f;
        if (param > 8.0f) param = 8.0f;
        sm->SetFloat("swimSpeed", param);
    }
}

/// Play a replicated action on a proxy. Ids: 1 death, 2 roll, 3 sit, 4 swim.
/// Anything else leaves the action. `param` is the direction or swim speed.
/// Dive, hit, vault and emotes are not ids: the legacy anim field still carries
/// jump, crouch and the other built-in poses. A machine added mid-session has
/// its Start() clear the state, so callers re-apply until the proxy is in it.
/// A roll is not replayed after it has been in the state (it is a one-shot).
inline void ApplyRemoteAction(Character* c, int id, float param) {
    if (!c || !c->gameObject) return;
    GameObject* go = c->gameObject;
    AnimStateMachine* sm = HumanoidLibrary::EnsureAnimator(go);
    if (sm) HumanoidActions::MergeMissing(*sm, c, false);
    switch (id) {
        case 1:
            HumanoidActions::PlayDeath(go, nullptr);   // sets deathDir 0, then we restore
            RefreshRemoteParam(c, 1, param);
            break;
        case 2:
            HumanoidActions::RequestRoll(go, ClampDir(param));
            break;
        case 3:
            RefreshRemoteParam(c, 3, param);
            if (sm && param < 10.0f && sm->Current() != "SitDown" && sm->Current() != "Sit")
                sm->GoTo("SitDown", 0.2f);
            else if (sm && param >= 10.0f && sm->Current() != "StandUp")
                sm->GoTo("StandUp", 0.12f);
            break;
        case 4:
            HumanoidActions::SetSwimming(go, true, param < 0.0f ? 0.0f : param);
            break;
        default:
            if (!sm) break;
            sm->SetBool("dead", false);
            sm->SetBool("sitting", false);
            sm->SetBool("swimming", false);
            if (sm->Current() != "Idle" && sm->Current() != "Walk" && sm->Current() != "Run")
                sm->GoTo("Idle", 0.2f);
            HumanoidActions::SetSwimming(go, false, 0.0f);
            break;
    }
}

/// "<anim> <punchSeq> [<speed> <angle> [<actionSeq> <actionId> <lookYaw> <lookPitch> [<actionParam>]]]".
/// Each later group is optional. An older peer stops when the stream runs out.
struct AnimSyncView {
    int anim = 0, punch = 0, actionSeq = 0, actionId = 0;
    float speed = -1.0f, angle = 0.0f, lookYaw = 0.0f, lookPitch = 0.0f, actionParam = 0.0f;
    bool haveGait = false, haveAction = false, haveParam = false;
};
/// False when `s` has no anim field. Gait, action and actionParam are optional.
inline bool ParseAnimSync(const std::string& s, AnimSyncView& o) {
    std::stringstream ss(s);
    if (!(ss >> o.anim)) return false;
    if (!(ss >> o.punch)) return true;
    float sp = 0.0f, ang = 0.0f;
    if (ss >> sp >> ang && std::isfinite(sp) && std::isfinite(ang)) {
        o.speed = sp; o.angle = ang; o.haveGait = true;
    }
    int aseq = 0, aid = 0; float ly = 0.0f, lp = 0.0f;
    if (ss >> aseq >> aid >> ly >> lp && std::isfinite(ly) && std::isfinite(lp)) {
        o.actionSeq = aseq; o.actionId = aid; o.lookYaw = ly; o.lookPitch = lp; o.haveAction = true;
        float param = 0.0f;
        if (ss >> param && std::isfinite(param)) { o.actionParam = param; o.haveParam = true; }
    }
    return true;
}

class NetworkSync : public Behaviour {
public:
    enum class Authority { Host, Mine, Manual };

    std::string netId;                          ///< shared id (blank = object name)
    Authority   authority = Authority::Host;    ///< who broadcasts this object
    bool        owned = true;                   ///< used when authority == Manual
    bool        syncAnimation = true;           ///< if a Character is found, replicate its anim + punches
    /// With the anti-cheat on, an object nobody owns is the server's: no client
    /// may move it or write its variables. True lets the first client to drive
    /// it claim it (a ball anyone can kick). The server can still hand any
    /// object to a client (AntiCheat::SetOwner).
    bool        clientClaimable = false;

    /// Manually (re)assign authority — handy for ownership transfer from script.
    void SetOwned(bool o) { owned = o; if (m_nm && !m_id.empty()) m_nm->SetSyncOwned(m_id, OwnedNow()); }

    /// True if THIS peer controls this object (so local input / controllers should
    /// drive it). A remote proxy returns false, leaving the network to move it. With
    /// no NetworkManager in the scene (single-player) it's always true.
    bool IsLocallyOwned() const { return !m_nm || OwnedNow(); }
    /// True when the network moves this object from a player's inputs here (a
    /// server with authoritative movement running a client's player): local
    /// input must not drive it, though this peer owns its transform.
    bool NetworkDriven() const { return m_nm && !m_id.empty() && m_nm->InputDriven(m_id); }

    void Start() override { Register(); }

    void Update(float dt) override {
        if (!m_nm) { Register(); return; }           // manager may appear after us
        if (!m_id.empty()) m_nm->SetSyncOwned(m_id, OwnedNow());
        // Detect the rising edge of our own punch so remotes can replay it once.
        if (m_char && OwnedNow()) {
            bool p = m_char->Punching();
            if (p && !m_wasPunching) ++m_punchSeq;
            m_wasPunching = p;
        }
        // (Our own predicted player is animated by its controller, not as a proxy.)
        if (m_char && syncAnimation && !OwnedNow() && !m_nm->Predicted(m_id)) {
            DriveProxyGait(dt);
            // A machine created to play the action has Start() clear its state
            // on the next frame. Keep asking until it is actually in it. A roll
            // latches once it has been playing, so the clip is not restarted
            // when it ends while the owner's sequence is still that roll.
            if (m_wantAction > 0) HoldRemoteAction();
        }
    }

    void OnDestroy() override {
        if (m_nm && !m_id.empty()) m_nm->UnregisterSync(m_id);
        m_nm = nullptr;
    }

private:
    NetworkManager* m_nm = nullptr;
    std::string     m_id;
    Character*      m_char = nullptr;
    int             m_punchSeq = 0;       // owner: bumped on each new punch
    int             m_lastPunchSeq = -1;  // remote: last punch replayed
    int             m_lastActionSeq = -1; // remote: last action sequence applied
    int             m_wantAction = 0;     // remote: action to hold until the proxy is in it
    float           m_actionParam = 0.0f; // remote: direction / swim speed for that action
    bool            m_actionLatched = false; // remote: a one-shot roll has been in its state
    bool            m_wasPunching = false;
    // Proxy gait: what the owner reported, and the smoothed speed this copy
    // actually moves at (the interpolated position, so the feet match it).
    float           m_ownerSpeed = -1.0f, m_ownerAngle = 0.0f;
    bool            m_haveOwnerGait = false;
    Vec3            m_lastPos;
    bool            m_haveLast = false;
    float           m_vx = 0.0f, m_vz = 0.0f;

    // A remote copy's Character gets the speed it is really moving at here (the
    // eased position), so its stride matches the ground it covers instead of
    // playing at a nominal cadence; the direction comes from the owner when it
    // reports one (strafe / backpedal), else from the travel direction.
    void DriveProxyGait(float dt) {
        if (!transform || dt <= 0.0f) return;
        const Vec3 p = transform->Position();
        if (!m_haveLast) { m_lastPos = p; m_haveLast = true; return; }
        const float vx = (p.x - m_lastPos.x) / dt, vz = (p.z - m_lastPos.z) / dt;
        m_lastPos = p;
        const float k = std::min(1.0f, dt * 12.0f);   // ~80 ms smoothing: the eased steps jitter
        m_vx += (vx - m_vx) * k; m_vz += (vz - m_vz) * k;
        float speed = std::sqrt(m_vx * m_vx + m_vz * m_vz);
        if (speed < 0.02f) speed = 0.0f;
        // A teleport / respawn snap isn't walking.
        if (m_haveOwnerGait && m_ownerSpeed >= 0.0f && speed > m_ownerSpeed * 3.0f + 2.0f) speed = m_ownerSpeed;
        m_char->moveSpeed = speed;
        const Transform& facing = m_char->transform ? *m_char->transform : *transform;
        m_char->moveAngle = m_haveOwnerGait ? m_ownerAngle : (speed > 0.0f ? MoveAngleFor(facing, m_vx, m_vz) : 0.0f);
        // The owner already sent this heading (moveAngle). A proxy has no
        // controller, so the 2D blend never hears moveX / moveZ unless we
        // write them. Forward is negative moveZ. This is not a new field.
        FeedMoveAxes(m_char->gameObject, speed, m_char->moveAngle);
    }

    static void FeedMoveAxes(GameObject* go, float speed, float angleDeg) {
        if (!go) return;
        auto feed = [&](GameObject* g) {
            if (!g) return;
            for (AnimStateMachine* sm : g->GetComponents<AnimStateMachine>()) {
                if (sm->layer != 0) continue;
                const float rad = angleDeg * 0.017453292f;
                sm->SetFloat("speed", speed);
                sm->SetFloat("moveX", speed * std::sin(rad));
                sm->SetFloat("moveZ", -speed * std::cos(rad));
            }
        };
        feed(go);
        if (go->transform)
            for (Transform* ch : go->transform->Children())
                if (ch && ch->gameObject) feed(ch->gameObject);
    }

    void HoldRemoteAction() {
        if (!m_char || m_wantAction <= 0) return;
        const bool entered = ProxyInAction(m_char, m_wantAction);
        const bool oneShot = m_wantAction == 2;
        if (!entered && !(oneShot && m_actionLatched))
            ApplyRemoteAction(m_char, m_wantAction, m_actionParam);
        else if (entered) {
            RefreshRemoteParam(m_char, m_wantAction, m_actionParam);
            if (AnimStateMachine* sm = BaseLayer(m_char))
                if (sm->StateTime() > 0.05f) m_actionLatched = true;
        }
    }

    bool OwnedNow() const {
        // Server-authoritative movement decides for a player's object: the
        // server owns it, the client that plays it doesn't.
        if (m_nm && !m_id.empty()) {
            const int a = m_nm->MovementAuthority(m_id);
            if (a >= 0) return a != 0;
        }
        switch (authority) {
            case Authority::Host:   return m_nm && m_nm->IsServer();
            case Authority::Mine:   return true;
            case Authority::Manual: default: return owned;
        }
    }
    void Register() {
        m_nm = FindManager();
        if (!m_nm || !gameObject || !gameObject->transform) return;
        m_id = netId.empty() ? gameObject->name : netId;
        if (m_id.empty()) return;
        m_nm->RegisterSync(m_id, gameObject->transform, OwnedNow());
        // Replicate Character animation if one is present (here or on a child).
        m_char = FindCharacter();
        if (m_char && syncAnimation) {
            Character* c = m_char; NetworkSync* self = this;
            // "<anim> <punchSeq> [<moveSpeed> <moveAngle> [<actionSeq> <actionId> <lookYaw> <lookPitch> [<actionParam>]]]".
            // Each later group is optional. An older peer stops when the stream
            // runs out. First-person pitch, the raised arm and the camera hide
            // are not in this message. Dive, hit, vault and emotes are not
            // action ids. actionParam is death/roll direction, sit type
            // (10 + type = stand up), or swim speed.
            m_nm->SetSyncExtra(m_id,
                [c, self]() {
                    std::ostringstream o;
                    o << c->anim << ' ' << self->m_punchSeq << ' ' << c->moveSpeed << ' ' << c->moveAngle
                      << ' ' << c->netActionSeq << ' ' << c->netActionId
                      << ' ' << c->lookYaw << ' ' << c->lookPitch
                      << ' ' << c->netActionParam;
                    return o.str();
                },
                [c, self](const std::string& s) {
                    AnimSyncView v;
                    v.anim = c->anim;
                    v.punch = self->m_lastPunchSeq;
                    if (!ParseAnimSync(s, v)) return;
                    c->anim = v.anim;
                    if (self->m_lastPunchSeq >= 0 && v.punch != self->m_lastPunchSeq) c->Punch();
                    self->m_lastPunchSeq = v.punch;   // first sample just syncs the counter
                    if (v.haveGait) {
                        self->m_ownerSpeed = v.speed; self->m_ownerAngle = v.angle; self->m_haveOwnerGait = true;
                    }
                    if (v.haveAction) {
                        const bool changed = self->m_lastActionSeq < 0 || v.actionSeq != self->m_lastActionSeq;
                        if (changed) { self->m_wantAction = v.actionId; self->m_actionLatched = false; }
                        if (v.haveParam) self->m_actionParam = v.actionParam;
                        if (self->m_wantAction > 0) self->HoldRemoteAction();
                        else if (changed && self->m_lastActionSeq >= 0)
                            ApplyRemoteAction(c, 0, 0.0f);
                        self->m_lastActionSeq = v.actionSeq;
                        c->lookYaw = v.lookYaw;
                        c->lookPitch = v.lookPitch;
                    }
                });
        }
    }
    Character* FindCharacter() const {
        if (!gameObject) return nullptr;
        if (auto* c = gameObject->GetComponent<Character>()) return c;
        Scene* sc = gameObject->scene();
        if (sc && gameObject->transform)
            for (const auto& up : sc->Objects())
                if (GameObject* go = up.get())
                    if (go != gameObject && IsDescendant(go->transform, gameObject->transform))
                        if (auto* c = go->GetComponent<Character>()) return c;
        return nullptr;
    }
    static bool IsDescendant(Transform* t, Transform* root) {
        for (Transform* p = t ? t->Parent() : nullptr; p; p = p->Parent())
            if (p == root) return true;
        return false;
    }
    NetworkManager* FindManager() const {
        Scene* sc = gameObject ? gameObject->scene() : nullptr;
        if (!sc) return nullptr;
        for (const auto& up : sc->Objects())
            if (GameObject* go = up.get())
                if (auto* nm = go->GetComponent<NetworkManager>()) return nm;
        return nullptr;
    }
};

} // namespace okay
