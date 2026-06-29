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
#include <string>

namespace okay {

class NetworkSync : public Behaviour {
public:
    enum class Authority { Host, Mine, Manual };

    std::string netId;                          ///< shared id (blank = object name)
    Authority   authority = Authority::Host;    ///< who broadcasts this object
    bool        owned = true;                   ///< used when authority == Manual

    /// Manually (re)assign authority — handy for ownership transfer from script.
    void SetOwned(bool o) { owned = o; if (m_nm && !m_id.empty()) m_nm->SetSyncOwned(m_id, OwnedNow()); }

    void Start() override { Register(); }

    void Update(float) override {
        if (!m_nm) { Register(); return; }           // manager may appear after us
        if (!m_id.empty()) m_nm->SetSyncOwned(m_id, OwnedNow());
    }

    void OnDestroy() override {
        if (m_nm && !m_id.empty()) m_nm->UnregisterSync(m_id);
        m_nm = nullptr;
    }

private:
    NetworkManager* m_nm = nullptr;
    std::string     m_id;

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
