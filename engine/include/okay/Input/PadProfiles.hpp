#pragma once
// What the engine knows about each connected controller slot (0..Input::kMaxGamepads-1):
// who it is (family, ids, name), how its raw inputs map to the logical pad, its battery
// and extras, plus the "Controller Setup" session, connect / disconnect events and
// pending requests (lightbar colour, slot swap). The host bridge (PadSet in
// Input/PlatformGamepad.hpp) fills it; scripts, the playground and UI read it.
// Pure logic and static state: no SDL, no device.
#include "okay/Input/ControllerMapping.hpp"
#include "okay/Input/GamepadInfo.hpp"
#include "okay/Input/InputActions.hpp"
#include <functional>
#include <string>

namespace okay {

struct PadProfile {
    bool present = false;
    int instanceId = -1;
    GamepadIdentity id;
    GamepadInfo info;
    std::string deviceKey;          ///< where a saved mapping lives
    MapSource mapSource = MapSource::None;
    ControllerMapping mapping;      ///< used when useMapping; otherwise the host's own layout drives the pad
    bool useMapping = false;
    bool hostMapped = true;         ///< the host has its own layout for this pad (false: opened as a plain joystick)
    ControllerMapping generic;      ///< the generic guess, kept so ClearSaved can go back to it
    RawPadState raw;                ///< latest raw state (for the playground and Controller Setup)
    LogicalPadState logical;        ///< latest logical state the engine read
    int power = -1;                 ///< -1 unknown, 0 empty, 1 low, 2 medium, 3 full, 4 wired
    int touchpads = 0;
    bool hostGyro = false, hostAccel = false, hostTriggerRumble = false;
    float gyro[3] = {0, 0, 0}, accel[3] = {0, 0, 0};
    struct Finger { bool down = false; float x = 0, y = 0; } finger[2];
    bool sensorsWanted = false;     ///< a reader sets this each frame it wants gyro / accelerometer data
    bool playerLedSet = false;
};

struct PadEvent {
    bool connected = true;
    int slot = 0;
    std::string name, familyName, text;   ///< text is ready for a toast: "Controller 1 connected: DualSense"
};

class PadProfiles {
public:
    // ---- host side
    /// A controller took `slot`. Picks the mapping: the saved one for this device, else the host's own
    /// layout (builtinMapped), else a generic one built from `idle`.
    static void Connect(int slot, int instanceId, const GamepadIdentity& id, const RawPadState& idle, bool builtinMapped, bool notify = true);
    static void Disconnect(int slot, bool notify = true);
    /// Once per frame per slot: the raw state and extras. Also drives Controller Setup.
    static void Update(int slot, const RawPadState& raw, int power);
    static PadProfile& Edit(int slot);   ///< for the host to fill in sensors and touchpad
    static void Reset();                 ///< forget everything (tests)

    // ---- readers
    static const PadProfile& Get(int slot);
    /// The mapping FeedPadAt must apply for this slot, or nullptr to use the host's own layout.
    static const ControllerMapping* Mapping(int slot);
    /// "Cross" / "A" ...: a pad button as THIS controller names it (the slot's family; a Generic one when empty).
    static std::string ButtonName(int slot, int padButton);
    /// An action's binding as the connected controller names it ("Cross" for a PlayStation pad, "A" for an Xbox pad).
    /// Keyboard / mouse bindings, and every binding while no controller is plugged in, read as InputBinding::Label().
    /// Used by the Controls screen model and the keybind field, so rebinding shows the right names.
    static std::string BindingLabel(const InputBinding& b);
    /// Several lines for the Controller station / a bug report: name, ids, family, how it was recognised, mapping source, extras.
    static std::string Describe(int slot);
    /// One line: the raw axes, buttons and hats, for example "axes 0.00 0.50 ...; buttons 0,3; hat up".
    static std::string RawText(int slot);
    static std::string BatteryText(int slot);   ///< "full", "low", "wired", "unknown" ...

    // ---- Controller Setup
    static bool BeginSetup(int slot);            ///< starts asking; false when the slot is empty
    static ControllerSetup* Setup(int slot);     ///< the running (or finished) session, or nullptr
    static void CancelSetup(int slot);
    /// Keep a finished (or partly skipped) setup: saved under the device key and used from now on by every slot with the same pad.
    static bool SaveSetup(int slot);
    /// Forget the saved mapping for this slot's device; back to built-in or generic.
    static void ClearSaved(int slot);
    static bool HasSaved(int slot);
    /// Save / load a mapping by device key (Prefs "padmap.<key>").
    static void SaveMapping(const std::string& key, const ControllerMapping& m);
    static bool LoadMapping(const std::string& key, ControllerMapping& out);

    // ---- events and requests
    static bool PopEvent(PadEvent& out);
    /// Called with the toast text on every connect / disconnect. Set by a game that shows its own notice. With no
    /// handler (the default, or after SetToastHandler(nullptr)) the text is shown as a toast by the UI the host
    /// installed (SetScriptUI: OkayUI's toasts in the player and in the editor's Play); with no UI nothing is shown.
    /// A game that wants no notice at all sets a handler that does nothing.
    static void SetToastHandler(std::function<void(const std::string&)> fn);
    static void RequestLightbar(int slot, int r, int g, int b);
    static bool TakeLightbar(int slot, int& r, int& g, int& b);
    /// Off by default: when on, every rumble request also shakes the trigger motors (Xbox One / Series).
    static void SetMirrorTriggers(bool on);
    static bool MirrorTriggers();
    static void RequestTriggerRumble(int slot, float left, float right, float seconds);
    static bool TakeTriggerRumble(int slot, float& left, float& right, float& seconds);
    static void RequestSwap(int a, int b);
    static bool TakeSwap(int& a, int& b);
};

} // namespace okay
