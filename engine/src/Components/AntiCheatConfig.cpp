#include "okay/Components/AntiCheatConfig.hpp"
#include "okay/Components/NetValidator.hpp"
#include "okay/Components/VisualScriptComponent.hpp"
#include "okay/Net/NetworkManager.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Core/Log.hpp"

namespace okay {

NetworkManager* AntiCheatConfig::FindManager() const {
    Scene* sc = gameObject ? gameObject->scene() : nullptr;
    if (!sc) return nullptr;
    for (const auto& up : sc->Objects())
        if (GameObject* go = up.get())
            if (auto* nm = go->GetComponent<NetworkManager>()) return nm;
    return nullptr;
}

bool AntiCheatConfig::Apply() {
    NetworkManager* nm = FindManager();
    if (!nm) return false;
    m_applied = true;
    m_error.clear();
    m_active = settings;
    bool ok = true;
    if (!file.empty()) {
        AntiCheatSettings fromFile;
        if (fromFile.LoadFile(file, &m_error)) m_active = fromFile;
        else {
            OKAY_WARN("anti-cheat: ", m_error, " (using the component's own settings)");
            ok = false;
        }
    }
    nm->anticheat.settings = m_active;

    // The handlers look the scene up on every call (they never point at this
    // component, so removing it can't leave them dangling).
    Scene* sc = gameObject->scene();
    nm->SetCheatHandler([sc](const CheatReport& r) {
        for (auto* v : sc->FindObjectsOfType<VisualScriptComponent>()) {
            v->SetVariable("cheat_detail", vs::VsValue(r.detail));
            v->RaiseEvent("OnCheatDetected", nullptr, CheatKindName(r.kind), vs::VsValue((float)r.peer));
        }
    });
    nm->anticheat.validateRpc = [sc](std::uint32_t peer, const std::string& name, const std::string& data) {
        for (auto* nv : sc->FindObjectsOfType<NetValidator>())
            if (nv->enabled && !nv->Allows(peer, name, data)) return false;
        bool allowed = true;
        for (auto* v : sc->FindObjectsOfType<VisualScriptComponent>()) {
            if (!v->Graph() || v->Graph()->Entries("OnValidateRpc").empty()) continue;
            v->SetVariable("rpc_reject", vs::VsValue(false));
            v->SetVariable("rpc_sender", vs::VsValue((float)peer));
            v->RaiseEvent("OnValidateRpc", nullptr, name, vs::VsValue(data));
            if (v->GetVariable("rpc_reject").AsBool()) allowed = false;
        }
        return allowed;
    };
    return ok;
}

} // namespace okay
