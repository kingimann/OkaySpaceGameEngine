#pragma once
// Windows.Gaming.Input -> okay::platform::Gamepad, as pure functions and one class (no Windows headers). The glue copies a
// Windows.Gaming.Input.GamepadReading into `PadReading` and hands vibration to a `PadDevice`; everything between is tested on Linux.
// Written from Microsoft's documentation of Windows.Gaming.Input.GamepadReading / GamepadButtons / GamepadVibration (thumbstick axes
// -1..1 with +Y up, triggers 0..1, vibration motors 0..1) and the XInput documentation for the dead-zone constants
// (XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE 7849, RIGHT 8689, TRIGGER_THRESHOLD 30, all out of 32767 / 255). The GamepadButtons bit values
// below are from the documented enum and UNVERIFIED against the real SDK header (the app shell's static_assert would catch a mismatch
// on Windows: see platform/xbox_uwp/app/main.cpp when it is compiled). Also the keyboard key table (VirtualKey -> platform::Key).
#include "okay/Platform/Platform.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace okay::xbox {

// Windows.Gaming.Input.GamepadButtons (flags).
enum PadButtonBits : std::uint32_t {
    kPadMenu = 1u << 0, kPadView = 1u << 1, kPadA = 1u << 2, kPadB = 1u << 3, kPadX = 1u << 4, kPadY = 1u << 5,
    kPadDpadUp = 1u << 6, kPadDpadDown = 1u << 7, kPadDpadLeft = 1u << 8, kPadDpadRight = 1u << 9,
    kPadLeftShoulder = 1u << 10, kPadRightShoulder = 1u << 11, kPadLeftThumb = 1u << 12, kPadRightThumb = 1u << 13,
};

struct PadReading {
    std::uint32_t buttons = 0;
    double leftTrigger = 0, rightTrigger = 0;        // 0..1
    double leftX = 0, leftY = 0, rightX = 0, rightY = 0;   // -1..1, +Y up (Windows.Gaming.Input)
};

struct PadConfig {
    double leftDeadzone = 7849.0 / 32767.0;    // XInput's documented values
    double rightDeadzone = 8689.0 / 32767.0;
    double triggerThreshold = 30.0 / 255.0;
};

/// Radial dead zone with the live range rescaled to 0..1 (no jump at the edge). Returns the new (x, y).
inline void RadialDeadzone(double x, double y, double dz, double& ox, double& oy) {
    if (!(dz >= 0) || dz >= 1.0) dz = 0.0;
    double m = std::sqrt(x * x + y * y);
    if (!(m > dz)) { ox = oy = 0; return; }
    if (m > 1.0) { x /= m; y /= m; m = 1.0; }
    const double s = (m - dz) / (1.0 - dz) / m;
    ox = x * s; oy = y * s;
}
inline double TriggerShape(double v, double threshold) {
    if (!(v > threshold)) return 0;
    if (v > 1.0) v = 1.0;
    return (v - threshold) / (1.0 - threshold);
}
inline std::int16_t ToAxis(double v) {   // -1..1 -> -32768..32767 (symmetric around 0 in the first step: +1 -> 32767, -1 -> -32768)
    if (v > 1.0) v = 1.0;
    if (v < -1.0) v = -1.0;
    return v >= 0 ? (std::int16_t)std::lround(v * 32767.0) : (std::int16_t)std::lround(v * 32768.0);
}

struct PadState {
    std::int16_t axis[6] = {0, 0, 0, 0, 0, 0};   // platform::GamepadAxis order: LeftX, LeftY, RightX, RightY, TriggerLeft, TriggerRight
    bool button[15] = {};                         // platform::GamepadButton order
};

/// The whole mapping: a reading to the platform's pad state. Sticks get +Y down (like SDL), triggers 0..32767, dead zones applied.
/// Menu = Start, View = Back (the Xbox naming); Guide is never reported (the system keeps it).
inline PadState MapReading(const PadReading& r, const PadConfig& c = PadConfig()) {
    using platform::GamepadAxis; using platform::GamepadButton;
    PadState s;
    double lx, ly, rx, ry;
    RadialDeadzone(r.leftX, r.leftY, c.leftDeadzone, lx, ly);
    RadialDeadzone(r.rightX, r.rightY, c.rightDeadzone, rx, ry);
    s.axis[(int)GamepadAxis::LeftX] = ToAxis(lx);   s.axis[(int)GamepadAxis::LeftY] = ToAxis(-ly);
    s.axis[(int)GamepadAxis::RightX] = ToAxis(rx);  s.axis[(int)GamepadAxis::RightY] = ToAxis(-ry);
    s.axis[(int)GamepadAxis::TriggerLeft] = (std::int16_t)std::lround(TriggerShape(r.leftTrigger, c.triggerThreshold) * 32767.0);
    s.axis[(int)GamepadAxis::TriggerRight] = (std::int16_t)std::lround(TriggerShape(r.rightTrigger, c.triggerThreshold) * 32767.0);
    auto b = [&](PadButtonBits bit, GamepadButton g) { s.button[(int)g] = (r.buttons & bit) != 0; };
    b(kPadA, GamepadButton::A); b(kPadB, GamepadButton::B); b(kPadX, GamepadButton::X); b(kPadY, GamepadButton::Y);
    b(kPadView, GamepadButton::Back); b(kPadMenu, GamepadButton::Start);
    b(kPadLeftThumb, GamepadButton::LeftStick); b(kPadRightThumb, GamepadButton::RightStick);
    b(kPadLeftShoulder, GamepadButton::LeftShoulder); b(kPadRightShoulder, GamepadButton::RightShoulder);
    b(kPadDpadUp, GamepadButton::DpadUp); b(kPadDpadDown, GamepadButton::DpadDown); b(kPadDpadLeft, GamepadButton::DpadLeft); b(kPadDpadRight, GamepadButton::DpadRight);
    return s;
}

/// Windows.Gaming.Input.GamepadVibration: four motors 0..1.
struct Vibration { double leftMotor = 0, rightMotor = 0, leftTrigger = 0, rightTrigger = 0; bool Zero() const { return leftMotor == 0 && rightMotor == 0 && leftTrigger == 0 && rightTrigger == 0; } };
inline double Motor(std::uint16_t v) { return v / 65535.0; }

/// Rumble with a duration: the platform asks for "strength for `ms`", Windows.Gaming.Input holds a vibration until it is changed,
/// so something has to switch it off. Update(nowMs) returns true when the vibration to send changed.
class VibrationTimer {
public:
    void Start(const Vibration& v, std::uint32_t ms, std::uint64_t nowMs) { cur_ = v; until_ = ms == 0 ? nowMs : nowMs + ms; changed_ = true; active_ = !v.Zero() && ms > 0; if (!active_) cur_ = Vibration(); }
    /// Merge a trigger-motor request into the running one (RumbleTriggers after Rumble).
    void StartTriggers(double l, double r, std::uint32_t ms, std::uint64_t nowMs) { Vibration v = cur_; v.leftTrigger = l; v.rightTrigger = r; Start(v, ms, nowMs); }
    bool Update(std::uint64_t nowMs, Vibration& out) {
        if (active_ && nowMs >= until_) { active_ = false; cur_ = Vibration(); changed_ = true; }
        out = cur_;
        const bool c = changed_; changed_ = false; return c;
    }
    bool Active() const { return active_; }
    void Stop() { active_ = false; cur_ = Vibration(); changed_ = true; }
private:
    Vibration cur_; std::uint64_t until_ = 0; bool changed_ = false, active_ = false;
};

/// What the glue provides for one pad: its latest reading and a way to send vibration (Windows.Gaming.Input.Gamepad::Vibration).
struct PadDevice {
    virtual ~PadDevice() = default;
    virtual bool Read(PadReading& out) = 0;          // false = disconnected
    virtual void SendVibration(const Vibration& v) = 0;
    virtual const char* Name() const { return "Xbox controller"; }
    virtual bool HasTriggerMotors() const { return true; }
};

/// platform::Gamepad over a PadDevice. Call Poll(nowMs) once per frame (reads the pad, ends expired vibration); Axis()/Button() then
/// answer from that snapshot.
class UwpGamepad : public platform::Gamepad {
public:
    UwpGamepad(int instanceId, PadDevice* dev, PadConfig cfg = PadConfig()) : id_(instanceId), dev_(dev), cfg_(cfg) {}
    ~UwpGamepad() override { if (dev_) dev_->SendVibration(Vibration()); }
    void Poll(std::uint64_t nowMs) {
        PadReading r;
        if (dev_ && dev_->Read(r)) { st_ = MapReading(r, cfg_); connected_ = true; } else { st_ = PadState(); connected_ = false; }
        Vibration v; if (vib_.Update(nowMs, v) && dev_) dev_->SendVibration(v);
        now_ = nowMs;
    }
    bool Connected() const { return connected_; }
    int InstanceId() const override { return id_; }
    const char* Name() const override { return dev_ ? dev_->Name() : "Xbox controller"; }
    std::int16_t Axis(platform::GamepadAxis a) const override { return st_.axis[(int)a]; }
    bool Button(platform::GamepadButton b) const override { return st_.button[(int)b]; }
    bool Rumble(std::uint16_t lo, std::uint16_t hi, std::uint32_t ms) override {
        if (!dev_) return false;
        Vibration v; v.leftMotor = Motor(lo); v.rightMotor = Motor(hi); v.leftTrigger = trigL_; v.rightTrigger = trigR_;
        vib_.Start(v, ms, now_); return true;
    }
    bool RumbleTriggers(std::uint16_t l, std::uint16_t r, std::uint32_t ms) override {
        if (!dev_ || !dev_->HasTriggerMotors()) return false;
        trigL_ = Motor(l); trigR_ = Motor(r); vib_.StartTriggers(trigL_, trigR_, ms, now_); return true;
    }
    bool HasTriggerRumble() const override { return dev_ && dev_->HasTriggerMotors(); }
    std::uint16_t VendorId() const override { return 0x045E; }   // Microsoft's USB vendor id
private:
    int id_; PadDevice* dev_; PadConfig cfg_; PadState st_; VibrationTimer vib_; bool connected_ = false; std::uint64_t now_ = 0; double trigL_ = 0, trigR_ = 0;
};

// ---- keyboard: Windows.System.VirtualKey -> platform::Key ---------------------------------------------------------------------
/// VirtualKey values are the Win32 VK_* numbers (documented: A-Z 0x41.., 0-9 0x30.., F1 0x70.., arrows 0x25-0x28, Enter 0x0D, Escape 0x1B). // lint-allow: secret (key codes, not a secret)
inline platform::Key KeyFromVirtualKey(int vk) {
    using platform::Key;
    if (vk >= 0x41 && vk <= 0x5A) return (Key)((int)Key::A + (vk - 0x41));
    if (vk >= 0x31 && vk <= 0x39) return (Key)((int)Key::N1 + (vk - 0x31));
    if (vk == 0x30) return Key::N0;
    if (vk >= 0x70 && vk <= 0x7B) return (Key)((int)Key::F1 + (vk - 0x70));
    if (vk >= 0x60 && vk <= 0x69) return vk == 0x60 ? Key::Kp0 : (Key)((int)Key::Kp1 + (vk - 0x61));
    switch (vk) {
        case 0x0D: return Key::Return; case 0x1B: return Key::Escape; case 0x08: return Key::Backspace; case 0x09: return Key::Tab; case 0x20: return Key::Space;
        case 0x25: return Key::Left; case 0x26: return Key::Up; case 0x27: return Key::Right; case 0x28: return Key::Down;
        case 0x21: return Key::PageUp; case 0x22: return Key::PageDown; case 0x23: return Key::End; case 0x24: return Key::Home;
        case 0x2D: return Key::Insert; case 0x2E: return Key::Delete;
        case 0x10: return Key::LShift; case 0x11: return Key::LCtrl; case 0x12: return Key::LAlt;
        case 0xA0: return Key::LShift; case 0xA1: return Key::RShift; case 0xA2: return Key::LCtrl; case 0xA3: return Key::RCtrl; case 0xA4: return Key::LAlt; case 0xA5: return Key::RAlt;
        case 0xBD: return Key::Minus; case 0xBB: return Key::Equals; case 0xDB: return Key::LeftBracket; case 0xDD: return Key::RightBracket;
        case 0xDC: return Key::Backslash; case 0xBA: return Key::Semicolon; case 0xDE: return Key::Apostrophe; case 0xC0: return Key::Grave;
        case 0xBC: return Key::Comma; case 0xBE: return Key::Period; case 0xBF: return Key::Slash; case 0x14: return Key::CapsLock;
        case 0x6B: return Key::KpPlus; case 0x6D: return Key::KpMinus; case 0x6A: return Key::KpMultiply; case 0x6F: return Key::KpDivide; case 0x6E: return Key::KpPeriod;
        default: return Key::Unknown;
    }
}

} // namespace okay::xbox
