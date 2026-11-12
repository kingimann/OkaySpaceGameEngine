// The keyboard feed shared by the built player and the editor's Play mode
// (Input/PlatformKeys.hpp), driven by a fake "held keys" set so no window or
// keyboard is needed, plus two controllers fed as players 1 and 2.
#include "test_framework.hpp"
#include "okay/Input/PlatformKeys.hpp"
#include "okay/Input/PlatformGamepad.hpp"
#include <set>

using namespace okay;
namespace pf = okay::platform;

namespace {
struct Held {
    std::set<int> keys;
    Held& Add(pf::Key k) { keys.insert((int)k); return *this; }
    bool operator()(pf::Key k) const { return keys.count((int)k) != 0; }
};
pf::Key UsLayout(char c) {   // a US layout: the key that types c
    if (c >= 'a' && c <= 'z') return (pf::Key)((int)pf::Key::A + (c - 'a'));
    if (c >= '1' && c <= '9') return (pf::Key)((int)pf::Key::N1 + (c - '1'));
    if (c == '0') return pf::Key::N0;
    return pf::Key::Unknown;
}
// AZERTY: the physical Q key types 'a' and the physical A key types 'q'.
pf::Key Azerty(char c) {
    if (c == 'a') return pf::Key::Q;
    if (c == 'q') return pf::Key::A;
    return UsLayout(c);
}
bool Has(const std::vector<char>& v, char c) { for (char x : v) if (x == c) return true; return false; }
std::vector<char> Feed(const Held& h, bool esc = false, pf::Key (*lay)(char) = UsLayout) {
    std::vector<char> down;
    AppendPlayKeysWith(h, lay, down, esc);
    Input::FeedKeys(down);
    return down;
}
struct FakePad final : pf::Gamepad {
    int id; int16_t axis[6] = {}; bool btn[15] = {};
    explicit FakePad(int i) : id(i) {}
    int InstanceId() const override { return id; }
    const char* Name() const override { return "fake"; }
    int16_t Axis(pf::GamepadAxis a) const override { return axis[(int)a]; }
    bool Button(pf::GamepadButton b) const override { return btn[(int)b]; }
    bool Rumble(uint16_t, uint16_t, uint32_t) override { return true; }
};
}

int main() {
    RUN_SUITE("platform_keys");

    // Nothing held: nothing fed.
    CHECK(Feed(Held{}).empty());

    // Every function key reaches the game under its own code.
    for (int n = 1; n <= 12; ++n) {
        Held h; h.Add((pf::Key)((int)pf::Key::F1 + n - 1));
        auto d = Feed(h);
        CHECK(d.size() == 1 && d[0] == Input::KeyF(n));
        CHECK(Input::GetKey(Input::KeyFromName("f" + std::to_string(n))));
    }

    // Navigation block and Tab.
    {
        auto d = Feed(Held{}.Add(pf::Key::Home).Add(pf::Key::End).Add(pf::Key::PageUp).Add(pf::Key::PageDown)
                          .Add(pf::Key::Insert).Add(pf::Key::Delete).Add(pf::Key::Tab));
        CHECK(Has(d, Input::KeyHome) && Has(d, Input::KeyEnd) && Has(d, Input::KeyPageUp) && Has(d, Input::KeyPageDown));
        CHECK(Has(d, Input::KeyInsert) && Has(d, Input::KeyDelete) && Has(d, '\t'));
        CHECK(Input::GetKey(Input::KeyFromName("pageup")) && Input::GetKey(Input::KeyFromName("home")));
    }

    // Left and right modifiers separately, and the generic ones derived.
    {
        Feed(Held{}.Add(pf::Key::LShift).Add(pf::Key::RCtrl).Add(pf::Key::LAlt));
        CHECK(Input::GetKey(Input::KeyLShift) && !Input::GetKey(Input::KeyRShift));
        CHECK(Input::GetKey(Input::KeyRCtrl) && !Input::GetKey(Input::KeyLCtrl));
        CHECK(Input::GetKey(Input::KeyLAlt) && !Input::GetKey(Input::KeyRAlt));
        CHECK(Input::GetKey(Input::KeyShift) && Input::GetKey(Input::KeyCtrl) && Input::GetKey(Input::KeyAlt));
        Feed(Held{}.Add(pf::Key::RShift));
        CHECK(Input::GetKey(Input::KeyRShift) && !Input::GetKey(Input::KeyLShift) && Input::GetKey(Input::KeyShift));
        CHECK(!Input::GetKey(Input::KeyAlt));
    }

    // Arrows count as themselves and as WASD; Space, Enter (either), Backspace.
    {
        auto d = Feed(Held{}.Add(pf::Key::Up).Add(pf::Key::Right).Add(pf::Key::Space).Add(pf::Key::KpEnter).Add(pf::Key::Backspace));
        CHECK(Has(d, Input::KeyUp) && Has(d, 'w') && Has(d, Input::KeyRight) && Has(d, 'd'));
        CHECK(Has(d, ' ') && Has(d, '\r') && Has(d, (char)8));
        CHECK(!Has(d, (char)27));
    }

    // Escape only when the caller says the game should see it.
    CHECK(Has(Feed(Held{}, true), (char)27));
    CHECK(!Has(Feed(Held{}, false), (char)27));

    // The layout decides which physical key types a letter (AZERTY WASD).
    {
        auto d = Feed(Held{}.Add(pf::Key::Q), false, Azerty);
        CHECK(Has(d, 'a') && !Has(d, 'q'));
        d = Feed(Held{}.Add(pf::Key::A), false, UsLayout);
        CHECK(Has(d, 'a'));
        d = Feed(Held{}.Add(pf::Key::N7).Add(pf::Key::N0));
        CHECK(Has(d, '7') && Has(d, '0'));
    }

    // The same feed with everything held at once (a stress for duplicates and size).
    {
        Held h;
        for (int k = 0; k < 120; ++k) h.Add((pf::Key)k);
        auto d = Feed(h, true);
        CHECK(d.size() > 40 && d.size() < 200);
    }

    // Extra controllers: each slot is its own player; only player 0 also types WASD / Space.
    {
        for (int p = 0; p < Input::kMaxGamepads; ++p) Input::FeedGamepadRaw(p, {}, {}, 0, 0, 0, false);
        FakePad a(1), b(2), c(3);
        a.btn[(int)pf::GamepadButton::A] = true;
        b.btn[(int)pf::GamepadButton::B] = true;
        b.axis[(int)pf::GamepadAxis::LeftX] = -32767;
        c.btn[(int)pf::GamepadButton::Y] = true;
        std::vector<char> keys;
        FeedPadAt(0, &a, &keys); FeedPadAt(1, &b, nullptr); FeedPadAt(2, &c, nullptr);   // as PadSet::Feed does: only player 0 types keys
        CHECK(Input::GamepadConnected(0) && Input::GamepadConnected(1) && Input::GamepadConnected(2) && !Input::GamepadConnected(3));
        CHECK(Input::GetGamepadButton(Input::PadA, 0) && !Input::GetGamepadButton(Input::PadA, 1));
        CHECK(Input::GetGamepadButton(Input::PadB, 1) && !Input::GetGamepadButton(Input::PadB, 0));
        CHECK(Input::GetGamepadButton(Input::PadY, 2));
        CHECK(Input::GamepadAxis(1).x < -0.5f && Input::GamepadAxis(0).x == 0.0f);
        CHECK(Has(keys, ' ') && !Has(keys, 'a'));   // only pad 0 typed Space; pad 1's stick did not type A
    }
    TEST_MAIN_RESULT();
}
