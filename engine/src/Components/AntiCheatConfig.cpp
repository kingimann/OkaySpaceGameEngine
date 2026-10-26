#include "okay/Components/AntiCheatConfig.hpp"
#include "okay/Components/NetValidator.hpp"
#include "okay/Components/VisualScriptComponent.hpp"
#include "okay/Components/ScriptComponent.hpp"
#include "okay/Scripting/ScriptVM.hpp"
#include "okay/Net/NetworkManager.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Core/Log.hpp"
#include <memory>
#include <unordered_set>

namespace okay {

NetworkManager* AntiCheatConfig::FindManager() const {
    Scene* sc = gameObject ? gameObject->scene() : nullptr;
    if (!sc) return nullptr;
    for (const auto& up : sc->Objects())
        if (GameObject* go = up.get())
            if (auto* nm = go->GetComponent<NetworkManager>()) return nm;
    return nullptr;
}

namespace {
std::uint64_t g_nextToken = 0;
// Tokens of AntiCheatConfigs whose install is still around, so one being
// removed doesn't put back the rules of another that's already gone.
std::unordered_set<std::uint64_t>& LiveTokens() { static std::unordered_set<std::uint64_t> s; return s; }

// The scene's anti-cheat listeners, listed once per server tick (not per
// message: a busy server checks many RPCs a tick). Destroyed objects leave the
// scene only at the end of a frame, so the pointers hold for the tick.
struct Listeners {
    std::uint64_t tick = ~std::uint64_t(0);
    std::vector<NetValidator*> validators;
    std::vector<VisualScriptComponent*> graphs;
    std::vector<ScriptComponent*> scripts;
    std::unordered_set<const void*> warned;   // scripts whose on_validate_rpc errored (logged once)
    void Refresh(Scene* sc, std::uint64_t now) {
        if (tick == now) return;
        tick = now;
        validators = sc->FindObjectsOfType<NetValidator>();
        graphs = sc->FindObjectsOfType<VisualScriptComponent>();
        scripts.clear();
        for (auto* s : sc->FindObjectsOfType<ScriptComponent>())
            if (s->enabled && s->VM() && ScriptsRun(s->gameObject)) scripts.push_back(s);
    }
};
} // namespace

bool AntiCheatConfig::Installed() const {
    return m_nm && NetworkManager::IsLive(m_nm) && m_nm->anticheat.installer == m_token && m_token != 0;
}

bool AntiCheatConfig::Apply() {
    m_startedOnce = true;
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
    // Remember what was there (unless it's our own earlier install), so
    // disabling / removing this component can put it back.
    if (!Installed()) {
        m_prev.settings = nm->anticheat.settings;
        m_prev.cheatHandler = nm->CheatHandler();
        m_prev.validateRpc = nm->anticheat.validateRpc;
        m_prev.rpcRulesFor = nm->anticheat.rpcRulesFor;
        m_prev.onForgetPeer = nm->anticheat.onForgetPeer;
        m_prev.installer = nm->anticheat.installer;
        m_token = ++g_nextToken;
        LiveTokens().insert(m_token);
    }
    m_nm = nm;
    nm->anticheat.installer = m_token;
    nm->anticheat.settings = m_active;

    // The handlers hold the scene and the manager (which owns them), never this
    // component, so removing it can't leave them dangling.
    Scene* sc = gameObject->scene();
    auto L = std::make_shared<Listeners>();
    nm->SetCheatHandler([sc, nm, L](const CheatReport& r) {
        L->Refresh(sc, nm->TickCount());
        for (auto* v : L->graphs) {
            v->SetVariable("cheat_detail", vs::VsValue(r.detail));
            v->RaiseEvent("OnCheatDetected", nullptr, CheatKindName(r.kind), vs::VsValue((float)r.peer));
        }
        const std::vector<vs::VsValue> args{vs::VsValue(CheatKindName(r.kind)), vs::VsValue((float)r.peer), vs::VsValue(r.detail)};
        for (auto* s : L->scripts) s->VM()->CallEventArgs("on_cheat_detected", args);
    });
    nm->anticheat.validateRpc = [sc, nm, L](std::uint32_t peer, const std::string& name, const std::string& data) {
        L->Refresh(sc, nm->TickCount());
        for (auto* nv : L->validators)
            if (nv->enabled && !nv->Allows(peer, name, data)) return false;
        bool allowed = true;
        for (auto* v : L->graphs) {
            if (!v->Graph() || v->Graph()->Entries("OnValidateRpc").empty()) continue;
            v->SetVariable("rpc_reject", vs::VsValue(false));
            v->SetVariable("rpc_sender", vs::VsValue((float)peer));
            v->RaiseEvent("OnValidateRpc", nullptr, name, vs::VsValue(data));
            if (v->GetVariable("rpc_reject").AsBool()) allowed = false;
        }
        // OkayScript: an explicit false rejects, no answer allows, an error
        // rejects (fail closed) and is logged once per script.
        const std::vector<vs::VsValue> args{vs::VsValue(name), vs::VsValue(data), vs::VsValue((float)peer)};
        for (auto* s : L->scripts) {
            if (!s->VM()->HasEvent("on_validate_rpc")) continue;
            std::string err;
            const vs::VsValue r = s->VM()->CallEventArgs("on_validate_rpc", args, &err);
            if (!err.empty()) {
                if (L->warned.insert(s).second)
                    OKAY_ERROR("anti-cheat: on_validate_rpc on '", s->gameObject ? s->gameObject->name : std::string("?"),
                               "' failed (RPCs it checks are refused): ", err);
                allowed = false;
            } else if (r.IsBool() && !r.AsBool()) {
                allowed = false;
            }
        }
        return allowed;
    };
    nm->anticheat.rpcRulesFor = [sc, nm, L](const std::string& name, bool& ownRate, bool& ownSize) {
        L->Refresh(sc, nm->TickCount());
        for (auto* nv : L->validators)
            if (nv->enabled && nv->Covers(name)) {
                if (nv->maxPerSecond > 0.0f) ownRate = true;
                if (nv->maxBytes > 0) ownSize = true;
            }
    };
    nm->anticheat.onForgetPeer = [sc](std::uint32_t peer) {
        for (auto* nv : sc->FindObjectsOfType<NetValidator>()) nv->ForgetPeer(peer);
    };
    return ok;
}

void AntiCheatConfig::Uninstall() {
    // Only if the manager still exists and our rules are still the ones in force
    // (a second AntiCheatConfig may have taken over since).
    if (Installed()) {
        if (m_prev.installer != 0 && !LiveTokens().count(m_prev.installer)) {
            // What we replaced came from another AntiCheatConfig that's gone
            // since: don't bring its rules back, just turn the checks off.
            AntiCheatSettings off = m_prev.settings;
            off.enabled = false;
            m_prev = Previous{};
            m_prev.settings = off;
        }
        m_nm->anticheat.settings = m_prev.settings;
        m_nm->SetCheatHandler(m_prev.cheatHandler);
        m_nm->anticheat.validateRpc = m_prev.validateRpc;
        m_nm->anticheat.rpcRulesFor = m_prev.rpcRulesFor;
        m_nm->anticheat.onForgetPeer = m_prev.onForgetPeer;
        m_nm->anticheat.installer = m_prev.installer;
    }
    if (m_token) LiveTokens().erase(m_token);
    m_nm = nullptr;
    m_token = 0;
    m_applied = false;
    m_prev = Previous{};
}

} // namespace okay
