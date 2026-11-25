#pragma once
// Player Input: one local player's input. While the game plays it loads the input asset (.okayinput), joins a player for the chosen
// device (the keyboard and mouse, one controller, or any device with auto switch), picks the control scheme and switches the action maps
// on or off. A game with several local players has one of these per player. Scripts read the player's actions with the ia_* calls and
// this component's index (GetPlayerIndex). The work is in InputRuntime / PlayerInputManager; this is the scene-side handle.
#include "okay/Input/InputRuntime.hpp"
#include "okay/Scene/Component.hpp"
#include <set>
#include <sstream>
#include <string>

namespace okay {

class LocalPlayerInput : public Component {
public:
    enum class Device { Any = 0, Keyboard = 1, Controller = 2 };
    std::string actions;                 ///< path of a .okayinput asset; empty = the default asset (or whatever the project already loaded)
    Device device = Device::Any;         ///< Any: the keyboard and every controller, switching with the last used
    int controller = 0;                  ///< Controller: which one, 0..7 (Input::kMaxGamepads)
    std::string scheme;                  ///< control scheme to start in; empty = the one that fits the device
    bool autoSwitch = true;              ///< Any: follow the device used last
    std::string startMaps;               ///< comma list of action maps switched on at start (the rest off); empty = as the asset says

    /// The index to pass to the ia_* calls, or -1 when this player did not join (not playing, device already taken).
    int GetPlayerIndex() const { return m_player; }
    ActionSet* Set() { return m_player >= 0 ? InputRuntime::Players().Player(m_player) : nullptr; }

    void Start() override {
        if (m_player >= 0) return;
        if (!actions.empty() && InputRuntime::AssetPath() != actions) InputRuntime::LoadAsset(actions);
        const JoinDevice jd = device == Device::Keyboard ? JoinDevice::Keyboard : device == Device::Controller ? JoinDevice::Pad : JoinDevice::Any;
        m_player = InputRuntime::JoinNamed(jd == JoinDevice::Keyboard ? "keyboard" : jd == JoinDevice::Any ? "any" : "pad" + std::to_string(controller < 0 ? 0 : controller >= Input::kMaxGamepads ? Input::kMaxGamepads - 1 : controller));
        ActionSet* s = Set();
        if (!s) return;
        if (!scheme.empty()) s->SetScheme(scheme);
        if (device == Device::Any) s->SetAutoSwitch(autoSwitch);
        if (!startMaps.empty()) {
            std::set<std::string> on;
            std::stringstream ss(startMaps); std::string t;
            while (std::getline(ss, t, ',')) { const std::size_t a = t.find_first_not_of(' '), b = t.find_last_not_of(' '); if (a != std::string::npos) on.insert(t.substr(a, b - a + 1)); }
            for (const ActionMap& m : std::vector<ActionMap>(s->Maps())) s->SetMapEnabled(m.name, on.count(m.name) > 0);
        }
    }
    void OnDestroy() override {
        if (m_player >= 0) InputRuntime::Players().Leave(m_player);
        m_player = -1;
    }
private:
    int m_player = -1;
};

} // namespace okay
