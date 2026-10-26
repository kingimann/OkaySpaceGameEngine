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
//     rpc_sender = the peer; a Reject Rpc node refuses it).
// Only the server checks anything; on clients and offline it does nothing.
// ---------------------------------------------------------------------------
#include "okay/Scene/Component.hpp"
#include "okay/Net/AntiCheat.hpp"
#include <string>

namespace okay {

class NetworkManager;

class AntiCheatConfig : public Behaviour {
public:
    /// Optional .okayac file; when set and it loads, it replaces `settings`.
    std::string file;
    /// The rules (used when `file` is empty or doesn't load). On by default:
    /// adding the component is the opt-in.
    AntiCheatSettings settings = DefaultSettings();

    static AntiCheatSettings DefaultSettings() {
        AntiCheatSettings s;
        s.enabled = true;
        s.maxSyncSpeed = 0.0f;
        s.kickAfterViolations = 5;
        return s;
    }

    void Start() override { Apply(); }
    void Update(float) override { if (!m_applied) Apply(); }   // the manager may appear after us

    /// (Re)apply the rules to the scene's NetworkManager. False if there's no
    /// manager yet or `file` failed to load (then `settings` is used and
    /// LoadError() says why).
    bool Apply();
    const std::string& LoadError() const { return m_error; }
    /// The rules in force after Apply (the file's, or `settings`).
    const AntiCheatSettings& Active() const { return m_active; }

private:
    bool m_applied = false;
    std::string m_error;
    AntiCheatSettings m_active;
    NetworkManager* FindManager() const;
};

} // namespace okay
