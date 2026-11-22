#pragma once
// Which kind of controller is this? A pure-logic classifier (no SDL, no device):
// give it the USB vendor / product ids, the name the system reports, the SDL
// controller type when the host knows one, and (optionally) the raw axis / button
// counts, and it says the family, the button style to show the player, and which
// extras (touchpad, gyro, lightbar, trigger rumble ...) that family has.
//
// The table is our own, written from public USB vendor / product id facts and
// the way each maker names its pads (see docs/engine/input.md, "Controllers"). Anything
// it does not recognise comes back as a generic controller, never as "ignore".
#include "okay/Input/Input.hpp"
#include <cstdint>
#include <string>

namespace okay {

enum class ControllerFamily {
    Unknown,
    Xbox360, XboxOne, XboxSeries,
    DualShock3, DualShock4, DualSense, DualSenseEdge,
    SwitchPro, JoyConLeft, JoyConRight, JoyConPair,
    SteamController, SteamDeck,
    Stadia, Luna, Shield,
    GenericXInput,   ///< a third-party pad that speaks the Xbox layout
    GenericDInput,   ///< any other gamepad-shaped device
    FlightStick, Wheel, ArcadeStick
};

/// How the buttons are named and drawn for the player.
enum class GlyphStyle { Xbox, PlayStation, Nintendo, Generic };

/// What the host told us about a device. Unset fields stay 0 / empty / -1.
struct GamepadIdentity {
    std::uint16_t vendor = 0;
    std::uint16_t product = 0;
    std::string name;
    int sdlType = -1;        ///< SDL_GameControllerType value (0 unknown, 1 Xbox 360, 2 Xbox One, 3 PS3, 4 PS4, 5 Switch Pro, 7 PS5, ...), -1 = not given
    int axes = -1, buttons = -1, hats = -1;   ///< raw counts, -1 = not given
};

struct GamepadInfo {
    ControllerFamily family = ControllerFamily::Unknown;
    GlyphStyle style = GlyphStyle::Generic;
    std::string familyName;       ///< "DualSense", "Xbox Series", "Generic controller" ...
    std::string matchedBy;        ///< "sdl type", "usb id", "name", "shape" or "default": how the family was decided
    bool recognised = false;      ///< false = fell back to a generic controller
    // What the family has (true = the hardware has it; whether the host exposes it is a separate question).
    bool touchpad = false, gyro = false, lightbar = false, playerLeds = false;
    bool adaptiveTriggers = false, triggerRumble = false, hdHaptics = false;
    bool shareButton = false, muteButton = false, paddles = false;
};

/// Decide the family. Never fails: an unknown device is Unknown or GenericDInput.
GamepadInfo ClassifyGamepad(const GamepadIdentity& id);

const char* FamilyName(ControllerFamily f);
GlyphStyle GlyphStyleOf(ControllerFamily f);
/// The coarse family the older label code uses (Xbox / PlayStation / Switch / Generic).
Input::PadFamily CoarseFamily(ControllerFamily f);

/// Extra controls beyond Input::PadButton.
enum class PadExtra { Guide, Share, Touchpad, Mute };

/// The name of an Input::PadButton on this family ("Cross", "View", "L2", "ZR" ...).
/// Out-of-range buttons give "?".
std::string PadButtonName(int padButton, ControllerFamily f);
/// "PS", "Home", "Create" / "Share", "Touchpad", "Mute" on this family; "" when it has none.
std::string PadExtraName(PadExtra e, ControllerFamily f);
/// "0x054c:0x0ce6"-style text, lowercase hex, "none" when both are 0.
std::string FormatUsbId(std::uint16_t vendor, std::uint16_t product);

} // namespace okay
