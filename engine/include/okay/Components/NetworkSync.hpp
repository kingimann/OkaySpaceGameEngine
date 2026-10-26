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

class NetworkSync : public Behaviour {
public:
    enum class Authority { Host, Mine, Manual };

    std::string netId;                          ///< shared id (blank = object name)
    Authority   authority = Authority::Host;    ///< who broadcasts this object
    bool        owned = true;                   ///< used when authority == Manual
    bool        syncAnimation = true;           ///< if a Character is found, replicate its anim + punches

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
        if (m_char && syncAnimation && !OwnedNow() && !m_nm->Predicted(m_id)) DriveProxyGait(dt);
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
            // "<anim> <punchSeq> [<moveSpeed> <moveAngle>]". The gait fields came
            // later: older peers stop reading after the punch counter, and a
            // message without them still reads here.
            m_nm->SetSyncExtra(m_id,
                [c, self]() {
                    std::ostringstream o;
                    o << c->anim << ' ' << self->m_punchSeq << ' ' << c->moveSpeed << ' ' << c->moveAngle;
                    return o.str();
                },
                [c, self](const std::string& s) {
                    std::stringstream ss(s); int a = c->anim, seq = self->m_lastPunchSeq;
                    ss >> a >> seq;
                    c->anim = a;
                    if (self->m_lastPunchSeq >= 0 && seq != self->m_lastPunchSeq) c->Punch();
                    self->m_lastPunchSeq = seq;   // first sample just syncs the counter
                    float sp = -1.0f, ang = 0.0f;
                    if (ss >> sp >> ang && std::isfinite(sp) && std::isfinite(ang)) {
                        self->m_ownerSpeed = sp; self->m_ownerAngle = ang; self->m_haveOwnerGait = true;
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
