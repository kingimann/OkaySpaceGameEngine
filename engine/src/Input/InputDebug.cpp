#include "okay/Input/InputDebug.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Input/InputActions.hpp"
#include <cstdio>

namespace okay {

namespace {
bool g_on = false;
std::vector<std::string> g_events;
bool g_keysPrev[256] = {};
unsigned g_mousePrev = 0;
unsigned g_padPrev[Input::kMaxGamepads] = {};
bool g_padConnPrev[Input::kMaxGamepads] = {};
std::vector<char> g_actionPrev;   // 1 = held last frame, indexed like InputActions::All()
Input::Device g_devicePrev = Input::Device::Keyboard;

void Note(const std::string& s) {
    if (g_events.size() >= InputDebug::Capacity()) g_events.erase(g_events.begin());
    g_events.push_back(s);
}
const char* DeviceName(Input::Device d) {
    switch (d) { case Input::Device::Gamepad: return "gamepad"; case Input::Device::Mouse: return "mouse";
                 case Input::Device::Touch: return "touch"; default: return "keyboard"; }
}
} // namespace

void InputDebug::Enable(bool on) {
    if (on && !g_on) {   // start from the current state so the first frame is not a flood of "down"
        for (int i = 0; i < 256; ++i) g_keysPrev[i] = Input::GetKey((char)i);
        g_mousePrev = 0;
        for (int b = 0; b < 3; ++b) if (Input::GetMouseButton(b)) g_mousePrev |= 1u << b;
        for (int p = 0; p < Input::kMaxGamepads; ++p) {
            g_padPrev[p] = 0;
            for (int b = 0; b < Input::PadButtonCount; ++b) if (Input::GetGamepadButton(b, p)) g_padPrev[p] |= 1u << b;
            g_padConnPrev[p] = Input::GamepadConnected(p);
        }
        g_actionPrev.assign(InputActions::All().size(), 0);
        for (std::size_t i = 0; i < g_actionPrev.size(); ++i) g_actionPrev[i] = InputActions::Get(InputActions::All()[i].name) ? 1 : 0;
        g_devicePrev = Input::LastDevice();
    }
    g_on = on;
}
bool InputDebug::Enabled() { return g_on; }
const std::vector<std::string>& InputDebug::Events() { return g_events; }
void InputDebug::Clear() { g_events.clear(); }

void InputDebug::Record() {
    if (!g_on) return;
    for (int i = 1; i < 256; ++i) {
        const bool now = Input::GetKey((char)i);
        if (now != g_keysPrev[i]) {
            if (i == Input::KeyShift || i == Input::KeyCtrl || i == Input::KeyAlt) { g_keysPrev[i] = now; continue; }   // the generic copies of the left and right keys
            Note(std::string("Key ") + Input::KeyName((char)i) + (now ? " down" : " up"));
            g_keysPrev[i] = now;
        }
    }
    static const char* const kMouse[3] = {"left", "right", "middle"};
    unsigned m = 0;
    for (int b = 0; b < 3; ++b) if (Input::GetMouseButton(b)) m |= 1u << b;
    for (int b = 0; b < 3; ++b)
        if (((m ^ g_mousePrev) >> b) & 1u) Note(std::string("Mouse ") + kMouse[b] + (((m >> b) & 1u) ? " down" : " up"));
    g_mousePrev = m;
    if (Input::MouseWheel() != 0.0f) { char b[48]; std::snprintf(b, sizeof b, "Mouse wheel %+g", Input::MouseWheel()); Note(b); }
    for (int p = 0; p < Input::kMaxGamepads; ++p) {
        const bool conn = Input::GamepadConnected(p);
        if (conn != g_padConnPrev[p]) { Note("Controller " + std::to_string(p + 1) + (conn ? " connected" : " unplugged")); g_padConnPrev[p] = conn; }
        unsigned cur = 0;
        for (int b = 0; b < Input::PadButtonCount; ++b) if (Input::GetGamepadButton(b, p)) cur |= 1u << b;
        for (int b = 0; b < Input::PadButtonCount; ++b)
            if (((cur ^ g_padPrev[p]) >> b) & 1u)
                Note("Pad " + std::to_string(p + 1) + " " + Input::GamepadButtonLabel(b, Input::GamepadFamily(p)) + (((cur >> b) & 1u) ? " down" : " up"));
        g_padPrev[p] = cur;
    }
    const auto& all = InputActions::All();
    if (g_actionPrev.size() != all.size()) g_actionPrev.assign(all.size(), 0);
    for (std::size_t i = 0; i < all.size(); ++i) {
        const bool now = all[i].value >= all[i].threshold;
        if (now != (g_actionPrev[i] != 0)) { Note("Action " + all[i].name + (now ? " down" : " up")); g_actionPrev[i] = now ? 1 : 0; }
    }
    if (Input::LastDevice() != g_devicePrev) { Note(std::string("Now using the ") + DeviceName(Input::LastDevice())); g_devicePrev = Input::LastDevice(); }
}

std::vector<std::string> InputDebug::Snapshot() {
    std::vector<std::string> out;
    char b[160];
    std::string keys;
    for (int i = 1; i < 256; ++i) {
        if (i == Input::KeyShift || i == Input::KeyCtrl || i == Input::KeyAlt) continue;
        if (Input::GetKey((char)i)) { if (!keys.empty()) keys += ' '; keys += Input::KeyName((char)i); }
    }
    out.push_back("Keys held: " + (keys.empty() ? std::string("none") : keys));
    std::snprintf(b, sizeof b, "Mouse: %.0f, %.0f  move %.0f, %.0f  wheel %g  buttons %s%s%s",
                  Input::MousePosition().x, Input::MousePosition().y, Input::MouseDelta().x, Input::MouseDelta().y,
                  Input::MouseWheel(), Input::GetMouseButton(0) ? "L" : "-", Input::GetMouseButton(1) ? "R" : "-", Input::GetMouseButton(2) ? "M" : "-");
    out.push_back(b);
    out.push_back(std::string("Last device: ") + DeviceName(Input::LastDevice()) + (Input::TextFocused() ? "   (a text field has the keyboard)" : ""));
    for (int p = 0; p < Input::kMaxGamepads; ++p) {
        if (!Input::GamepadConnected(p)) continue;
        const Vec2 l = Input::GamepadAxis(p), r = Input::GamepadRightAxis(p);
        std::string held;
        for (int bt = 0; bt < Input::PadButtonCount; ++bt)
            if (Input::GetGamepadButton(bt, p)) { if (!held.empty()) held += ' '; held += Input::GamepadButtonLabel(bt, Input::GamepadFamily(p)); }
        std::snprintf(b, sizeof b, "Pad %d: L %.2f,%.2f  R %.2f,%.2f  LT %.2f RT %.2f  held %s", p + 1, l.x, l.y, r.x, r.y,
                      Input::GamepadTrigger(0, p), Input::GamepadTrigger(1, p), held.empty() ? "none" : held.c_str());
        out.push_back(b);
    }
    for (const auto& a : InputActions::All()) {
        if (a.value <= 0.0f) continue;
        if (a.direction) std::snprintf(b, sizeof b, "Action %s: %.2f, %.2f", a.name.c_str(), a.vector.x, a.vector.y);
        else std::snprintf(b, sizeof b, "Action %s: %.2f%s", a.name.c_str(), a.value, a.value >= a.threshold ? " (held)" : "");
        out.push_back(b);
    }
    return out;
}

} // namespace okay
