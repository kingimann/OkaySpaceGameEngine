// Pure tests for the web backend's decisions (engine/src/Platform/WebInput.hpp): no browser and no Emscripten.
// Expected values are worked out by hand from the W3C UI Events code list, the Gamepad "Standard Gamepad" layout and the
// USB HID Usage Tables (keyboard page).
#include "WebInput.hpp"
#include <cstdio>

using namespace okay::platform;
using namespace okay::platform::web;

static int g_fail = 0, g_n = 0;
#define CHECK(c) do { ++g_n; if (!(c)) { ++g_fail; std::printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

static void Keys() {
    CHECK(KeyFromDomCode("KeyA") == Key::A && KeyFromDomCode("KeyZ") == Key::Z && KeyFromDomCode("KeyQ") == Key::Q);
    CHECK(KeyFromDomCode("Digit1") == Key::N1 && KeyFromDomCode("Digit9") == Key::N9 && KeyFromDomCode("Digit0") == Key::N0);
    CHECK(KeyFromDomCode("Numpad1") == Key::Kp1 && KeyFromDomCode("Numpad9") == Key::Kp9 && KeyFromDomCode("Numpad0") == Key::Kp0);
    CHECK(KeyFromDomCode("F1") == Key::F1 && KeyFromDomCode("F12") == Key::F12 && (int)KeyFromDomCode("F13") == 0x68 &&
          (int)KeyFromDomCode("F24") == 0x73);
    CHECK(KeyFromDomCode("F0") == Key::Unknown && KeyFromDomCode("F25") == Key::Unknown && KeyFromDomCode("F1x") == Key::Unknown);
    CHECK(KeyFromDomCode("Space") == Key::Space && KeyFromDomCode("Enter") == Key::Return && KeyFromDomCode("NumpadEnter") == Key::KpEnter);
    CHECK(KeyFromDomCode("Escape") == Key::Escape && KeyFromDomCode("Backquote") == Key::Grave && KeyFromDomCode("Tab") == Key::Tab);
    CHECK(KeyFromDomCode("ArrowUp") == Key::Up && KeyFromDomCode("ArrowDown") == Key::Down &&
          KeyFromDomCode("ArrowLeft") == Key::Left && KeyFromDomCode("ArrowRight") == Key::Right);
    CHECK(KeyFromDomCode("ShiftLeft") == Key::LShift && KeyFromDomCode("ShiftRight") == Key::RShift &&
          KeyFromDomCode("ControlLeft") == Key::LCtrl && KeyFromDomCode("AltRight") == Key::RAlt &&
          KeyFromDomCode("MetaLeft") == Key::LGui && KeyFromDomCode("OSRight") == Key::RGui);
    CHECK(KeyFromDomCode("IntlBackslash") == Key::NonUsBackslash && KeyFromDomCode("ContextMenu") == Key::Application);
    CHECK(KeyFromDomCode("Quote") == Key::Apostrophe && KeyFromDomCode("Equal") == Key::Equals && KeyFromDomCode("NumpadDecimal") == Key::KpPeriod);
    CHECK(KeyFromDomCode("") == Key::Unknown && KeyFromDomCode(nullptr) == Key::Unknown && KeyFromDomCode("Keya") == Key::Unknown &&
          KeyFromDomCode("KeyAB") == Key::Unknown && KeyFromDomCode("Fn") == Key::Unknown && KeyFromDomCode("Unidentified") == Key::Unknown);
    // every named key is below kKeyCount (the backend's held-key array)
    for (const char* c : {"KeyA", "Lang2", "OSRight", "F24", "Numpad0", "AudioVolumeDown"}) CHECK((int)KeyFromDomCode(c) < kKeyCount);

    CHECK(KeyTypesText("a") && KeyTypesText(" ") && KeyTypesText("\xC3\xA9") && KeyTypesText("\xE2\x82\xAC") && KeyTypesText("\xF0\x9F\x98\x80"));
    CHECK(!KeyTypesText("Enter") && !KeyTypesText("ArrowUp") && !KeyTypesText("Dead") && !KeyTypesText("") && !KeyTypesText(nullptr));
    CHECK(!KeyTypesText("\x7F") && !KeyTypesText("\t") && !KeyTypesText("\x80") && !KeyTypesText("\xC3") && !KeyTypesText("ab"));

    CHECK(!PreventKeyDown(Key::Space, true, false, false));   // Space types " ": the keypress (stopped there) carries it
    CHECK(PreventKeyDown(Key::Down, false, false, false) && PreventKeyDown(Key::Tab, false, false, false) &&
          PreventKeyDown(Key::PageDown, false, false, false) && PreventKeyDown(Key::Backspace, false, false, false));
    CHECK(!PreventKeyDown(Key::C, true, true, false) && !PreventKeyDown(Key::V, true, false, true) && !PreventKeyDown(Key::X, true, true, false));
    CHECK(PreventKeyDown(Key::F, true, true, false) && PreventKeyDown(Key::S, true, false, true));   // Ctrl+F, Cmd+S stay with the game
    CHECK(!PreventKeyDown(Key::F12, false, false, false) && PreventKeyDown(Key::F11, false, false, false));

    LayoutMap m;
    CHECK(m.ForChar('a') == Key::Unknown);
    m.Learn(Key::Q, "a");        // AZERTY: the key in the QWERTY "Q" place types a
    m.Learn(Key::A, "q");
    m.Learn(Key::W, "Z");        // upper case counts as the letter
    m.Learn(Key::Space, " ");
    m.Learn(Key::N1, "&");       // not a letter, digit or space: ignored
    m.Learn(Key::E, "Enter");    // a key name: ignored
    m.Learn(Key::Unknown, "e");
    CHECK(m.ForChar('a') == Key::Q && m.ForChar('A') == Key::Q && m.ForChar('q') == Key::A && m.ForChar('z') == Key::W);
    CHECK(m.ForChar(' ') == Key::Space && m.ForChar('&') == Key::Unknown && m.ForChar('e') == Key::Unknown && m.ForChar((char)0xE9) == Key::Unknown);
}

static void Mouse() {
    CHECK(MouseButtonFromDom(0) == MouseLeft && MouseButtonFromDom(1) == MouseMiddle && MouseButtonFromDom(2) == MouseRight &&
          MouseButtonFromDom(3) == MouseX1 && MouseButtonFromDom(4) == MouseX2 && MouseButtonFromDom(5) == 0 && MouseButtonFromDom(-1) == 0);
    CHECK(WheelNotches(100, 0) == 1.0f && WheelNotches(-300, 0) == -3.0f && WheelNotches(3, 1) == 1.0f && WheelNotches(-1, 2) == -1.0f);
    CHECK(WheelNotches(0.0 / 0.0, 0) == 0.0f && WheelNotches(1e300, 0) == 1000.0f && WheelNotches(-1e300, 1) == -1000.0f);
    CHECK(Fraction(50, 200) == 0.25f && Fraction(-5, 200) == 0.0f && Fraction(500, 200) == 1.0f && Fraction(5, 0) == 0.0f && Fraction(5, -1) == 0.0f);
    CHECK(std::strcmp(CssCursor(CursorShape::Hand), "pointer") == 0 && std::strcmp(CssCursor(CursorShape::TextInput), "text") == 0 &&
          std::strcmp(CssCursor(CursorShape::NotAllowed), "not-allowed") == 0 && std::strcmp(CssCursor(CursorShape::ResizeNWSE), "nwse-resize") == 0);
}

static void Canvas() {
    int w = 0, h = 0;
    CanvasPixels(640, 360, 2.0, true, w, h);   CHECK(w == 1280 && h == 720);    // what tests/scripts/test_web_player.js checks
    CanvasPixels(800, 500, 2.0, true, w, h);   CHECK(w == 1600 && h == 1000);
    CanvasPixels(640, 360, 2.0, false, w, h);  CHECK(w == 640 && h == 360);     // not high-DPI: CSS pixels
    CanvasPixels(333.3, 100.5, 1.5, true, w, h); CHECK(w == 500 && h == 151);   // 499.95 -> 500, 150.75 -> 151
    CanvasPixels(0, -10, 2.0, true, w, h);     CHECK(w == 1 && h == 1);
    CanvasPixels(640, 360, 0.0 / 0.0, true, w, h); CHECK(w == 640 && h == 360);
    CanvasPixels(640, 360, 100.0, true, w, h); CHECK(w == 640 && h == 360);    // a ratio past 8 is not believed
    CanvasPixels(1e9, 1e9, 1.0, true, w, h);   CHECK(w == kMaxWindowExtent && h == kMaxWindowExtent);
}

static void Pads() {
    CHECK(StandardButtonIndex(GamepadButton::A) == 0 && StandardButtonIndex(GamepadButton::B) == 1 &&
          StandardButtonIndex(GamepadButton::X) == 2 && StandardButtonIndex(GamepadButton::Y) == 3);
    CHECK(StandardButtonIndex(GamepadButton::LeftShoulder) == 4 && StandardButtonIndex(GamepadButton::RightShoulder) == 5 &&
          StandardButtonIndex(GamepadButton::Back) == 8 && StandardButtonIndex(GamepadButton::Start) == 9);
    CHECK(StandardButtonIndex(GamepadButton::LeftStick) == 10 && StandardButtonIndex(GamepadButton::RightStick) == 11 &&
          StandardButtonIndex(GamepadButton::DpadUp) == 12 && StandardButtonIndex(GamepadButton::DpadDown) == 13 &&
          StandardButtonIndex(GamepadButton::DpadLeft) == 14 && StandardButtonIndex(GamepadButton::DpadRight) == 15 &&
          StandardButtonIndex(GamepadButton::Guide) == 16);
    // no two buttons share an index, and none is a trigger's
    for (int a = 0; a <= (int)GamepadButton::DpadRight; ++a) {
        const int ia = StandardButtonIndex((GamepadButton)a);
        CHECK(ia >= 0 && ia <= 16 && ia != kStandardLeftTrigger && ia != kStandardRightTrigger);
        for (int b = a + 1; b <= (int)GamepadButton::DpadRight; ++b) CHECK(ia != StandardButtonIndex((GamepadButton)b));
    }
    CHECK(StickValue(0) == 0 && StickValue(1) == 32767 && StickValue(-1) == -32768 && StickValue(2) == 32767 && StickValue(-7) == -32768);
    CHECK(StickValue(0.5) == 16384 && StickValue(-0.5) == -16384 && StickValue(0.0 / 0.0) == 0);   // 16383.5 rounds up
    CHECK(TriggerValue(0) == 0 && TriggerValue(1) == 32767 && TriggerValue(0.5) == 16384 && TriggerValue(-0.2) == 0 &&
          TriggerValue(3) == 32767 && TriggerValue(0.0 / 0.0) == 0);
    uint16_t v = 1, p = 1;
    CHECK(VendorProductFromId("Wireless Controller (STANDARD GAMEPAD Vendor: 054c Product: 09cc)", v, p) && v == 0x054c && p == 0x09cc);
    CHECK(VendorProductFromId("045e-028e-Microsoft X-Box 360 pad", v, p) && v == 0x045e && p == 0x028e);
    CHECK(!VendorProductFromId("Xbox 360 Controller (XInput STANDARD GAMEPAD)", v, p) && v == 0 && p == 0);
    CHECK(!VendorProductFromId("Pad (Vendor: 05 Product: 09cc)", v, p) && !VendorProductFromId("Pad Vendor: 054c", v, p));
    CHECK(!VendorProductFromId("x45e-028e-Pad", v, p) && !VendorProductFromId("", v, p) && !VendorProductFromId(nullptr, v, p));
}

static void Audio() {
    CHECK(ProcessorFrames(0) == 256 && ProcessorFrames(256) == 256 && ProcessorFrames(257) == 512 && ProcessorFrames(1024) == 1024 &&
          ProcessorFrames(1500) == 2048 && ProcessorFrames(100000) == 16384 && ProcessorFrames(-5) == 256);
}

int main() {
    Keys();
    Mouse();
    Canvas();
    Pads();
    Audio();
    std::printf("platform_web_pure: %d checks, %d failed\n", g_n, g_fail);
    return g_fail ? 1 : 0;
}
