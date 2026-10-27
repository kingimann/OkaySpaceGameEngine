#pragma once
#include "okay/Input/Input.hpp"
#include "okay/Core/Game.hpp"
#include "okay/Components/UIInputField.hpp"

namespace okay {

/// Which set of actions may run. The player and the editor set this before the
/// scene updates (same moment as Input::SetUICaptured). Allows() also checks
/// pause and a focused text field, so a key still does one thing if the context
/// is a frame behind.
enum class InputContext { Gameplay, InventoryOpen, TextInput, Paused };

enum class GameAction {
    InventoryToggle,   ///< open / close the backpack
    Interact,          ///< use the nearest chest, bench, pickup, or aimed button
    Drop,              ///< drop one of the selected hotbar item
    DropStack,         ///< drop the whole selected stack
    HotbarPrev,
    HotbarNext,
    CraftToggle,
    GridRotate,
};

/// Default keyboard and gamepad bindings for inventory actions. Components keep
/// their own char fields; those override the keyboard side. The gamepad button
/// is the default unless an Interactor replaces the interact button.
/// Gamepad ids: 0=A 1=B 2=X 3=Y 4=Back 5=Start 6=LB 7=RB 8-11 d-pad.
class InputActions {
public:
    struct Bind { char key; int pad; };   ///< pad -1 = no gamepad button

    static void SetContext(InputContext c) { Ctx() = c; }
    static InputContext Context() { return Ctx(); }

    /// Master switch for "wheel changes the hotbar". Per-bag `scrollSelect`
    /// still has to be on. Off gives the wheel back to the camera.
    static bool HotbarWheelEnabled() { return HotbarWheel() ; }
    static void SetHotbarWheelEnabled(bool on) { HotbarWheel() = on; }

    static Bind Default(GameAction a) {
        switch (a) {
            case GameAction::InventoryToggle: return {'e', 3};   // E, Y
            case GameAction::Interact:        return {'e', 2};   // E, X (A stays jump)
            case GameAction::Drop:            return {0, 1};     // B; keyboard stays on the component
            case GameAction::DropStack:       return {0, -1};
            case GameAction::HotbarPrev:      return {0, 6};     // LB
            case GameAction::HotbarNext:      return {0, 7};     // RB
            case GameAction::CraftToggle:     return {'c', -1};
            case GameAction::GridRotate:      return {'r', 2};   // X, while a grid drag is held
        }
        return {0, -1};
    }

    /// Pause, or a text field that already has focus. Inventory keys do not run.
    static bool BlocksAll() {
        return Game::Paused() || UIInputField::AnyFocused()
            || Ctx() == InputContext::TextInput || Ctx() == InputContext::Paused;
    }
    /// Lean, fire, build, and melee. An open bag counts, including the capture
    /// flag the runtime set from last frame.
    static bool BlocksGameplay() {
        return BlocksAll() || Ctx() == InputContext::InventoryOpen || Input::UICaptured();
    }
    static bool Allows(GameAction) { return !BlocksAll(); }

    /// Keyboard edge for `key` (0 = that side is off) and, when `usePad`, the
    /// action's default gamepad button.
    static bool Pressed(GameAction a, char key, bool usePad = true) {
        if (!Allows(a)) return false;
        if (key && Input::GetKeyDown(key)) return true;
        const int pad = Default(a).pad;
        return usePad && pad >= 0 && Input::GetGamepadButtonDown(pad);
    }

private:
    static InputContext& Ctx() { static InputContext c = InputContext::Gameplay; return c; }
    static bool& HotbarWheel() { static bool on = true; return on; }
};

} // namespace okay
