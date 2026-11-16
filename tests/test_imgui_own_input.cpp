// The plain logic of our own Dear ImGui backend (shared/ImGuiOwnInput.*): no Dear ImGui, no window. Expected values are
// written by hand from the documented ImGui input interface (button numbering, cursor numbering, packed colour order).
#include "test_framework.hpp"
#include "ImGuiOwnInput.hpp"

using namespace okay::imguibackend;
namespace plat = okay::platform;

namespace {
plat::Event Ev(plat::EventType t) { plat::Event e; e.type = t; return e; }

void TestKeys() {
    RUN_SUITE("key mapping");
    CHECK(UiKeyFromPlatform(plat::Key::A) == UiKey::A && UiKeyFromPlatform(plat::Key::Z) == UiKey::Z);
    CHECK(UiKeyFromPlatform(plat::Key::N0) == UiKey::N0 && UiKeyFromPlatform(plat::Key::N1) == UiKey::N1 && UiKeyFromPlatform(plat::Key::N9) == UiKey::N9);
    CHECK(UiKeyFromPlatform(plat::Key::F1) == UiKey::F1 && UiKeyFromPlatform(plat::Key::F12) == UiKey::F12);
    CHECK(UiKeyFromPlatform(plat::Key::Return) == UiKey::Enter && UiKeyFromPlatform(plat::Key::KpEnter) == UiKey::KeypadEnter);
    CHECK(UiKeyFromPlatform(plat::Key::Up) == UiKey::UpArrow && UiKeyFromPlatform(plat::Key::Left) == UiKey::LeftArrow);
    CHECK(UiKeyFromPlatform(plat::Key::Kp0) == UiKey::Keypad0 && UiKeyFromPlatform(plat::Key::Kp5) == UiKey::Keypad5 && UiKeyFromPlatform(plat::Key::Kp9) == UiKey::Keypad9);
    CHECK(UiKeyFromPlatform(plat::Key::Grave) == UiKey::GraveAccent && UiKeyFromPlatform(plat::Key::Equals) == UiKey::Equal);
    CHECK(UiKeyFromPlatform(plat::Key::LCtrl) == UiKey::LeftCtrl && UiKeyFromPlatform(plat::Key::RGui) == UiKey::RightSuper);
    CHECK(UiKeyFromPlatform(plat::Key::Unknown) == UiKey::None && UiKeyFromPlatform((plat::Key)300) == UiKey::None);
    CHECK(std::string(UiKeyName(UiKey::LeftArrow)) == "LeftArrow" && std::string(UiKeyName(UiKey::N0)) == "N0" && std::string(UiKeyName(UiKey::ModCtrl)) == "ModCtrl");
    // every key the platform names that ImGui has maps to a distinct UI key
    int mapped = 0, dup = 0; bool seen[(int)UiKey::Count] = {};
    for (int k = 1; k < 232; ++k) { const UiKey u = UiKeyFromPlatform((plat::Key)k); if (u == UiKey::None) continue; ++mapped; if (seen[(int)u]) ++dup; seen[(int)u] = true; }
    CHECK(dup == 0 && mapped >= 100);
    CHECK(ImGuiButtonFromPlatform(plat::MouseLeft) == 0 && ImGuiButtonFromPlatform(plat::MouseRight) == 1 && ImGuiButtonFromPlatform(plat::MouseMiddle) == 2);
    CHECK(ImGuiButtonFromPlatform(plat::MouseX1) == 3 && ImGuiButtonFromPlatform(plat::MouseX2) == 4 && ImGuiButtonFromPlatform(9) == -1);
}

void TestTranslator() {
    RUN_SUITE("input translator");
    InputTranslator t;
    std::vector<InputOp> out;
    plat::Event k = Ev(plat::EventType::KeyDown); k.key = plat::Key::A; k.mods = plat::ModCtrl;
    t.Feed(k, out);
    // first key: all four modifier states are sent (Ctrl down, others up), then the key
    CHECK(out.size() == 5 && out[0].key == UiKey::ModCtrl && out[0].down && out[1].key == UiKey::ModShift && !out[1].down && out[4].key == UiKey::A && out[4].down);
    out.clear();
    t.Feed(k, out);   // same modifiers: only the key
    CHECK(out.size() == 1 && out[0].key == UiKey::A);
    out.clear();
    k.type = plat::EventType::KeyUp; k.mods = 0;
    t.Feed(k, out);   // Ctrl released: one modifier change then the key up
    CHECK(out.size() == 2 && out[0].key == UiKey::ModCtrl && !out[0].down && out[1].key == UiKey::A && !out[1].down);
    out.clear();
    plat::Event unk = Ev(plat::EventType::KeyDown); unk.key = plat::Key::Unknown;
    t.Feed(unk, out);
    CHECK(out.empty());

    plat::Event m = Ev(plat::EventType::MouseMove); m.x = 120; m.y = 40;
    t.Feed(m, out);
    CHECK(out.size() == 1 && out[0].kind == InputOp::MousePos && out[0].x == 120 && out[0].y == 40);
    out.clear();
    plat::Event b = Ev(plat::EventType::MouseButtonDown); b.button = plat::MouseRight; b.x = 7; b.y = 9;
    t.Feed(b, out);
    CHECK(out.size() == 2 && out[0].kind == InputOp::MousePos && out[0].x == 7 && out[1].kind == InputOp::MouseButton && out[1].button == 1 && out[1].down);
    out.clear();
    b.button = 7;
    t.Feed(b, out);
    CHECK(out.empty());   // an unknown button is dropped whole
    plat::Event w = Ev(plat::EventType::MouseWheel); w.wheelX = 0.5f; w.wheelY = -2.0f;
    t.Feed(w, out);
    CHECK(out.size() == 1 && out[0].kind == InputOp::MouseWheel && out[0].x == 0.5f && out[0].y == -2.0f);
    out.clear();
    plat::Event tx = Ev(plat::EventType::TextInput); tx.text = "\xC3\xA4\xE2\x82\xAC";
    t.Feed(tx, out);
    CHECK(out.size() == 1 && out[0].kind == InputOp::Text && out[0].text == tx.text);
    out.clear();
    tx.text.clear();
    t.Feed(tx, out);
    CHECK(out.empty());
    plat::Event f = Ev(plat::EventType::WindowFocusLost);
    t.Feed(f, out);
    CHECK(out.size() == 1 && out[0].kind == InputOp::Focus && !out[0].down);
    out.clear();
    k.type = plat::EventType::KeyDown; k.mods = 0;
    t.Feed(k, out);   // modifiers are re-sent after a focus loss
    CHECK(out.size() == 5);
    out.clear();
    t.Feed(Ev(plat::EventType::GamepadAdded), out); t.Feed(Ev(plat::EventType::DropFile), out); t.Feed(Ev(plat::EventType::Quit), out);
    CHECK(out.empty());

    InputTranslator s;   // window filter and mouse scale
    s.SetWindow(5); s.SetMouseScale(0.5f, 2.0f);
    m.windowId = 4; s.Feed(m, out);
    CHECK(out.empty());
    m.windowId = 5; s.Feed(m, out);
    CHECK(out.size() == 1 && out[0].x == 60 && out[0].y == 80);
    out.clear();
    m.windowId = 0; s.Feed(m, out);   // an event with no window id is accepted
    CHECK(out.size() == 1);
}

void TestMisc() {
    RUN_SUITE("display, timer, cursor, clipboard, text input");
    DisplayMetrics d = ComputeDisplay(800, 600, 1600, 1200);
    CHECK(d.width == 800 && d.height == 600 && d.scaleX == 2 && d.scaleY == 2);
    d = ComputeDisplay(800, 600, 0, 0);
    CHECK(d.scaleX == 1 && d.scaleY == 1);
    d = ComputeDisplay(0, 0, 0, 0);
    CHECK(d.width == 0 && d.scaleX == 1);

    FrameTimer ft;
    CHECK_NEAR(ft.Next(1000, 1000000), 1.0 / 60.0, 1e-6);
    CHECK_NEAR(ft.Next(1000 + 16666, 1000000), 0.016666, 1e-6);
    CHECK(ft.Next(1000 + 16666, 1000000) > 0.0f);                 // a counter that did not move: a tiny positive delta
    CHECK(ft.Next(5, 1000000) > 0.0f);                             // went backwards
    CHECK(ft.Next(5 + 10000000ull, 1000000) == 1.0f);              // 10 s stall: capped at 1
    FrameTimer nf;
    CHECK(nf.Next(5, 0) > 0.0f);                                   // no frequency

    CHECK(!MapCursor(-1).visible);
    CHECK(MapCursor(0).shape == plat::CursorShape::Arrow && MapCursor(1).shape == plat::CursorShape::TextInput);
    CHECK(MapCursor(2).shape == plat::CursorShape::ResizeAll && MapCursor(3).shape == plat::CursorShape::ResizeNS && MapCursor(4).shape == plat::CursorShape::ResizeEW);
    CHECK(MapCursor(5).shape == plat::CursorShape::ResizeNESW && MapCursor(6).shape == plat::CursorShape::ResizeNWSE);
    CHECK(MapCursor(7).shape == plat::CursorShape::Hand && MapCursor(8).shape == plat::CursorShape::NotAllowed);
    CHECK(MapCursor(99).shape == plat::CursorShape::Arrow && MapCursor(99).visible);

    std::string stored = "clip";
    ClipboardCache cc([&] { return stored; }, [&](const std::string& s) { stored = s; return true; });
    const char* p = cc.Get();
    CHECK(p && std::string(p) == "clip");
    cc.Set("new \xC3\xA4");
    CHECK(stored == "new \xC3\xA4");
    stored.clear();
    CHECK(cc.Get() == nullptr);                                    // empty clipboard: null, not ""
    cc.Set(nullptr);
    CHECK(stored.empty());

    TextInputGate g;
    CHECK(g.Update(false) == TextInputGate::Nothing && g.Update(true) == TextInputGate::Start && g.Update(true) == TextInputGate::Nothing);
    CHECK(g.On() && g.Update(false) == TextInputGate::Stop && !g.On());
}

void TestDraw() {
    RUN_SUITE("draw conversion");
    plat::Event dummy; (void)dummy;
    okay::ui2d::Rect r{};
    CHECK(ClipToRect(10.2f, 5.7f, 50.1f, 20.0f, 100, 100, r) && r.x == 10 && r.y == 5 && r.w == 41 && r.h == 15);
    CHECK(ClipToRect(-5, -5, 500, 500, 100, 80, r) && r.x == 0 && r.y == 0 && r.w == 100 && r.h == 80);
    CHECK(!ClipToRect(50, 50, 50, 60, 100, 100, r));               // no width
    CHECK(!ClipToRect(200, 0, 300, 10, 100, 100, r));              // fully right of the display
    CHECK(!ClipToRect(0, 0, 0.0f / 0.0f, 10, 100, 100, r));        // NaN
    CHECK(ClipToRect(3.0f, 3.0f, 4.0f, 4.0f, 100, 100, r) && r.w == 1 && r.h == 1);

    DrawVertexIn in[2] = {{1.5f, 2.5f, 0.25f, 0.75f, 0x80FF4020u}, {0, 0, 0, 0, 0xFFFFFFFFu}};
    std::vector<okay::ui2d::Vertex> v;
    ConvertVertices(in, 2, v);
    CHECK(v.size() == 2 && v[0].position.x == 1.5f && v[0].position.y == 2.5f && v[0].tex_coord.x == 0.25f && v[0].tex_coord.y == 0.75f);
    CHECK(v[0].color.r == 0x20 && v[0].color.g == 0x40 && v[0].color.b == 0xFF && v[0].color.a == 0x80);   // 0xAABBGGRR
    CHECK(v[1].color.r == 255 && v[1].color.a == 255);
    ConvertVertices(in, 0, v);
    CHECK(v.empty());
    std::uint16_t a16[3] = {0, 65535, 7};
    std::uint32_t a32[2] = {70000, 3};
    std::vector<int> idx;
    ConvertIndices(a16, nullptr, 3, idx);
    CHECK(idx.size() == 3 && idx[1] == 65535 && idx[2] == 7);
    ConvertIndices(nullptr, a32, 2, idx);
    CHECK(idx.size() == 2 && idx[0] == 70000);

    const std::uint8_t alpha[12] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};   // 4 x 3
    std::vector<std::uint8_t> rgba;
    Alpha8ToRgba(alpha, 4, 1, 1, 2, 2, rgba);                       // the 2 x 2 block at (1,1): 6 7 / 10 11
    CHECK(rgba.size() == 16 && rgba[0] == 255 && rgba[1] == 255 && rgba[2] == 255 && rgba[3] == 6 && rgba[7] == 7 && rgba[11] == 10 && rgba[15] == 11);
    Alpha8ToRgba(alpha, 4, 0, 0, 0, 0, rgba);
    CHECK(rgba.empty());
}
} // namespace

int main() {
    TestKeys();
    TestTranslator();
    TestMisc();
    TestDraw();
    TEST_MAIN_RESULT();
}
