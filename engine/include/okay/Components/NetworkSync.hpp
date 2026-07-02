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

namespace okay {

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

    void Start() override { Register(); }

    void Update(float) override {
        if (!m_nm) { Register(); return; }           // manager may appear after us
        if (!m_id.empty()) m_nm->SetSyncOwned(m_id, OwnedNow());
        // Detect the rising edge of our own punch so remotes can replay it once.
        if (m_char && OwnedNow()) {
            bool p = m_char->Punching();
            if (p && !m_wasPunching) ++m_punchSeq;
            m_wasPunching = p;
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
    bool            m_wasPunching = false;

    bool OwnedNow() const {
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
            m_nm->SetSyncExtra(m_id,
                [c, self]() { return std::to_string(c->anim) + " " + std::to_string(self->m_punchSeq); },
                [c, self](const std::string& s) {
                    std::stringstream ss(s); int a = c->anim, seq = self->m_lastPunchSeq;
                    ss >> a >> seq;
                    c->anim = a;
                    if (self->m_lastPunchSeq >= 0 && seq != self->m_lastPunchSeq) c->Punch();
                    self->m_lastPunchSeq = seq;   // first sample just syncs the counter
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
