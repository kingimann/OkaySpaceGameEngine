#pragma once
// ---------------------------------------------------------------------------
// AntiCheatConfig — turns the server's anti-cheat on for this scene (see
// okay/Net/AntiCheat.hpp) and connects it to the game:
//   * its settings (or a .okayac file) go to the scene's NetworkManager;
//   * every refusal raises "On Cheat Detected" on the scene's visual scripts
//     (Event Name = what was caught, Event Value = the peer id, and the object
//     variable cheat_detail = the object / prefab / key involved);
//   * a client's RPC must pass every NetValidator in the scene and every
//     "On Validate Rpc" graph (Event Name = the RPC, Event Value = its data,
//     rpc_sender = the peer; a Reject Rpc node refuses it);
//   * OkayScript gets the same: on_cheat_detected(kind, peer, detail) and
//     on_validate_rpc(name, data, sender) (OnCheatDetected / OnValidateRpc).
//     A handler returning false rejects the RPC; returning nothing allows it;
//     an error in the handler rejects it (fail closed) and is logged once.
// Disabling or removing the component takes all of that away again (the
// manager's previous rules and handlers come back); enabling re-applies it.
// While the manager runs as a server that's ignored unless allowRuntimeToggle
// is set, so a stray script can't switch the checks off mid-game.
// If `file` doesn't load, the server refuses every join until it does.
// Only the server checks anything; on clients and offline it does nothing.
// ---------------------------------------------------------------------------
#include "okay/Scene/Component.hpp"
#include "okay/Net/AntiCheat.hpp"
#include "okay/Net/SecureChannel.hpp"
#include <cstdint>
#include <functional>
#include <string>

namespace okay {

class NetworkManager;

class AntiCheatConfig : public Behaviour {
public:
    /// Optional .okayac file; when set and it loads, it replaces `settings`.
    std::string file;
    /// The rules (used when `file` is empty). On by default: adding the
    /// component is the opt-in.
    AntiCheatSettings settings = DefaultSettings();
    /// Let disabling / removing the component take the rules off a running
    /// server (off: they stay until the server stops).
    bool allowRuntimeToggle = false;
    /// Messages a client may send per second, set on the manager when it has
    /// no limit of its own (maxMessagesPerSecond 0).
    static constexpr int kMessagesPerSecond = 120;

    static AntiCheatSettings DefaultSettings() {
        AntiCheatSettings s;
        s.enabled = true;
        s.requireEncryption = net::SecureChannel::Available();
        s.maxSyncSpeed = 20.0f;
        s.kickAfterViolations = 5;
        s.maxRpcsPerSecond = 30;
        s.maxHitRange = 300.0f;
        return s;
    }

    void Start() override { Apply(); }
    void Update(float) override { if (!m_applied) Apply(); }   // the manager may appear after us
    void OnEnable() override { if (m_startedOnce) Apply(); }
    void OnDisable() override { RuntimeUninstall(); }
    void OnDestroy() override { RuntimeUninstall(); }

    /// (Re)apply the rules to the scene's NetworkManager. False if there's no
    /// manager yet or `file` failed to load (then LoadError() says why and the
    /// server refuses joins).
    bool Apply();
    const std::string& LoadError() const { return m_error; }
    /// The rules in force after Apply (the file's, or `settings`).
    const AntiCheatSettings& Active() const { return m_active; }

    /// True while this component's rules are the ones in force on its manager.
    bool Installed() const;

private:
    bool m_applied = false;
    bool m_startedOnce = false;
    std::string m_error;
    AntiCheatSettings m_active;
    // What Apply replaced on the manager, put back by Uninstall.
    NetworkManager* m_nm = nullptr;
    std::uint64_t m_token = 0;
    struct Previous {
        AntiCheatSettings settings;
        std::function<void(const CheatReport&)> cheatHandler;
        std::function<bool(std::uint32_t, const std::string&, const std::string&)> validateRpc;
        std::function<void(const std::string&, bool&, bool&)> rpcRulesFor;
        std::function<bool(const std::string&)> coversRpc;
        std::function<void(std::uint32_t)> onForgetPeer;
        std::function<bool(HitClaim&)> validateHit;
        std::uint64_t installer = 0;
        std::string configError;
        int maxMessagesPerSecond = 0;
    } m_prev;
    bool m_toggleWarned = false;
    NetworkManager* FindManager() const;
    void Uninstall();
    void RuntimeUninstall();
};

} // namespace okay
