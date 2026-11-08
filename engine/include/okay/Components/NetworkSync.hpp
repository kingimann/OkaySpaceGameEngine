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

inline bool ProxyInAction(const Character* c, int id) {
    if (!c || !c->gameObject) return false;
    const AnimStateMachine* sm = nullptr;
    for (const auto& comp : c->gameObject->Components())
        if (auto* m = dynamic_cast<const AnimStateMachine*>(comp.get()))
            if (m->layer == 0) { sm = m; break; }
    if (!sm) return false;
    const std::string& cur = sm->Current();
    switch (id) {
        // Same names CharacterAnimBackend maps to the action ids.
        case 1: return cur == "Death" || cur == "DeadHold" || cur.rfind("Death_", 0) == 0 || cur.rfind("DeadHold_", 0) == 0;
        case 2: return cur == "Roll" || cur == "Dodge" || cur == "Roll_F" || cur.rfind("Dodge_", 0) == 0;
        case 3: return cur == "SitDown" || cur == "Sit" || cur == "StandUp";
        case 4: return cur == "Swim" || cur.rfind("Swim_", 0) == 0;
        default: return cur == "Idle" || cur == "Walk" || cur == "Run" || cur.empty();
    }
}

/// Play a replicated action on a proxy. Ids match CharacterAnimBackend:
/// 1 death, 2 roll, 3 sit, 4 swim, anything else leaves the action.
/// A machine added mid-session has its Start() clear the state, so callers
/// re-apply until the proxy is actually in it.
inline void ApplyRemoteAction(Character* c, int id) {
    if (!c || !c->gameObject) return;
    GameObject* go = c->gameObject;
    AnimStateMachine* sm = HumanoidLibrary::EnsureAnimator(go);
    if (sm) HumanoidActions::MergeMissing(*sm, c, false);
    switch (id) {
        case 1: HumanoidActions::PlayDeath(go, nullptr); break;
        case 2: HumanoidActions::RequestRoll(go, 0); break;
        case 3: HumanoidActions::Play(go, "SitDown", 0.2f); break;
        case 4: HumanoidActions::SetSwimming(go, true, 0.0f); break;
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
            // on the next frame. Keep asking until it is actually in it.
            // Once it has stayed in it past that first frame, leave it alone: a
            // roll that ends before the owner reports "none" must not replay.
            if (m_wantAction > 0) {
                if (ProxyInAction(m_char, m_wantAction)) { if (++m_actionFrames >= 2) m_actionSeen = true; }
                else { m_actionFrames = 0; if (!m_actionSeen) ApplyRemoteAction(m_char, m_wantAction); }
            }
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
    bool            m_actionSeen = false; // remote: the proxy has held m_wantAction (don't replay it)
    int             m_actionFrames = 0;   // remote: consecutive frames the proxy has been in it
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
            // "<anim> <punchSeq> [<moveSpeed> <moveAngle> [<actionSeq> <actionId> <lookYaw> <lookPitch>]]".
            // Each later group is optional: an older peer stops when the stream
            // runs out, and a message without the new fields still reads here.
            m_nm->SetSyncExtra(m_id,
                [c, self]() {
                    std::ostringstream o;
                    o << c->anim << ' ' << self->m_punchSeq << ' ' << c->moveSpeed << ' ' << c->moveAngle
                      << ' ' << c->netActionSeq << ' ' << c->netActionId
                      << ' ' << c->lookYaw << ' ' << c->lookPitch;
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
                    int aseq = 0, aid = 0; float ly = 0.0f, lp = 0.0f;
                    // The sender is untrusted (the host also reads a client's): known
                    // action ids only, and the look is bounded so the easing stays finite.
                    if (ss >> aseq >> aid >> ly >> lp && std::isfinite(ly) && std::isfinite(lp)
                        && aseq >= 0 && aid >= 0 && aid <= 4) {
                        ly = std::max(-90.0f, std::min(90.0f, ly));
                        lp = std::max(-90.0f, std::min(90.0f, lp));
                        const bool changed = self->m_lastActionSeq < 0 || aseq != self->m_lastActionSeq;
                        if (changed) { self->m_wantAction = aid; self->m_actionSeen = false; self->m_actionFrames = 0; }
                        if (self->m_wantAction > 0 && (changed || (!self->m_actionSeen && !ProxyInAction(c, self->m_wantAction))))
                            ApplyRemoteAction(c, self->m_wantAction);
                        else if (self->m_wantAction == 0 && changed && self->m_lastActionSeq >= 0)
                            ApplyRemoteAction(c, 0);
                        self->m_lastActionSeq = aseq;
                        c->lookYaw = ly;
                        c->lookPitch = lp;
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
