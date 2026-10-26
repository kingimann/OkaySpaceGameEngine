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
            // Not the component's own (looser) settings: no rules, no players.
            OKAY_ERROR("anti-cheat: ", m_error, "; the server refuses every join until the rules load");
            ok = false;
        }
    }
    // Remember what was there (unless it's our own earlier install), so
    // disabling / removing this component can put it back.
    if (!Installed()) {
        if (m_active.enabled)
            OKAY_INFO("anti-cheat: hit claims need a known weapon (a Net Validator for hit/<weapon>, or one of the ",
                      m_active.weapons.size(), " weapon(s) in the rules)");
        m_prev.settings = nm->anticheat.settings;
        m_prev.cheatHandler = nm->CheatHandler();
        m_prev.validateRpc = nm->anticheat.validateRpc;
        m_prev.rpcRulesFor = nm->anticheat.rpcRulesFor;
        m_prev.coversRpc = nm->anticheat.coversRpc;
        m_prev.onForgetPeer = nm->anticheat.onForgetPeer;
        m_prev.onParkPeer = nm->anticheat.onParkPeer;
        m_prev.onRestorePeer = nm->anticheat.onRestorePeer;
        m_prev.validateHit = nm->anticheat.validateHit;
        m_prev.installer = nm->anticheat.installer;
        m_prev.configError = nm->anticheat.configError;
        m_prev.maxMessagesPerSecond = nm->maxMessagesPerSecond;
        if (nm->maxMessagesPerSecond == 0) nm->maxMessagesPerSecond = kMessagesPerSecond;
        m_token = ++g_nextToken;
        LiveTokens().insert(m_token);
    }
    m_nm = nm;
    nm->anticheat.installer = m_token;
    nm->anticheat.settings = m_active;
    nm->anticheat.configError = ok ? std::string() : m_error;

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
                if (nv->LimitsRate()) ownRate = true;
                if (nv->maxBytes > 0) ownSize = true;
            }
    };
    nm->anticheat.coversRpc = [sc, nm, L](const std::string& name) {
        L->Refresh(sc, nm->TickCount());
        for (auto* nv : L->validators)
            if (nv->enabled && nv->Covers(name)) return true;
        return false;
    };
    nm->anticheat.onForgetPeer = [sc](std::uint32_t peer) {
        for (auto* nv : sc->FindObjectsOfType<NetValidator>()) nv->ForgetPeer(peer);
    };
    nm->anticheat.onParkPeer = [sc](std::uint32_t peer, const std::string& ident) {
        for (auto* nv : sc->FindObjectsOfType<NetValidator>()) nv->Park(peer, ident);
    };
    nm->anticheat.onRestorePeer = [sc](std::uint32_t peer, const std::string& ident) {
        for (auto* nv : sc->FindObjectsOfType<NetValidator>()) nv->Restore(peer, ident);
    };
    // A hit claim that passed the server's own checks: the game's rules. The
    // code hook that was there first, then On Validate Hit graphs (Reject Hit;
    // Set Var hit_damage changes the damage), then on_validate_hit scripts
    // (false rejects, a positive number is the damage, an error rejects; no
    // answer, which OkayScript hands back as 0, leaves it as it was).
    nm->anticheat.validateHit = [sc, nm, L, prev = m_prev.validateHit](HitClaim& h) {
        if (prev && !prev(h)) return false;
        L->Refresh(sc, nm->TickCount());
        bool allowed = true;
        for (auto* v : L->graphs) {
            if (!v->Graph() || v->Graph()->Entries("OnValidateHit").empty()) continue;
            v->SetVariable("hit_reject", vs::VsValue(false));
            v->SetVariable("hit_target", vs::VsValue(h.target));
            v->SetVariable("hit_damage", vs::VsValue(h.damage));
            v->RaiseEvent("OnValidateHit", nullptr, h.weapon, vs::VsValue((float)h.shooter));
            if (v->GetVariable("hit_reject").AsBool()) allowed = false;
            const float d = v->GetVariable("hit_damage").AsFloat();
            if (std::isfinite(d) && d >= 0.0f) h.damage = d;
        }
        const std::vector<vs::VsValue> args{vs::VsValue((float)h.shooter), vs::VsValue(h.target), vs::VsValue(h.weapon),
                                            vs::VsValue(h.damage)};
        for (auto* s : L->scripts) {
            if (!s->VM()->HasEvent("on_validate_hit")) continue;
            std::string err;
            const vs::VsValue r = s->VM()->CallEventArgs("on_validate_hit", args, &err);
            if (!err.empty()) {
                if (L->warned.insert(s).second)
                    OKAY_ERROR("anti-cheat: on_validate_hit on '", s->gameObject ? s->gameObject->name : std::string("?"),
                               "' failed (hits it checks are refused): ", err);
                allowed = false;
            } else if (r.IsBool() && !r.AsBool()) {
                allowed = false;
            } else if (r.IsNumber() && std::isfinite(r.AsFloat()) && r.AsFloat() > 0.0f) {
                h.damage = r.AsFloat();
            }
        }
        return allowed;
    };
    return ok;
}

void AntiCheatConfig::RuntimeUninstall() {
    if (!allowRuntimeToggle && Installed() && m_nm->IsServer()) {
        if (!m_toggleWarned)
            OKAY_WARN("anti-cheat: the rules stay on while the server runs (Allow Runtime Toggle lets disabling the component remove them)");
        m_toggleWarned = true;
        return;
    }
    Uninstall();
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
            const int messages = m_prev.maxMessagesPerSecond;
            m_prev = Previous{};
            m_prev.settings = off;
            m_prev.maxMessagesPerSecond = messages;
        }
        m_nm->anticheat.settings = m_prev.settings;
        m_nm->SetCheatHandler(m_prev.cheatHandler);
        m_nm->anticheat.validateRpc = m_prev.validateRpc;
        m_nm->anticheat.rpcRulesFor = m_prev.rpcRulesFor;
        m_nm->anticheat.coversRpc = m_prev.coversRpc;
        m_nm->anticheat.onForgetPeer = m_prev.onForgetPeer;
        m_nm->anticheat.onParkPeer = m_prev.onParkPeer;
        m_nm->anticheat.onRestorePeer = m_prev.onRestorePeer;
        m_nm->anticheat.validateHit = m_prev.validateHit;
        m_nm->anticheat.installer = m_prev.installer;
        m_nm->anticheat.configError = m_prev.configError;
        m_nm->maxMessagesPerSecond = m_prev.maxMessagesPerSecond;
    }
    if (m_token) LiveTokens().erase(m_token);
    m_nm = nullptr;
    m_token = 0;
    m_applied = false;
    m_prev = Previous{};
}

} // namespace okay
