// Tray-icon callback protocol model (Platform/TrayProtocol.hpp). Message values are written out by hand from the Win32
// documentation (winuser.h / shellapi.h numbers), not taken from the header under test, so a wrong constant fails here.
#include "test_framework.hpp"
#include "Platform/TrayProtocol.hpp"

using namespace okay::platform::tray;

int main() {
    // Constants as documented: WM_USER = 0x0400; NIN_SELECT = WM_USER, NINF_KEY = 1, NIN_BALLOONSHOW = WM_USER+2 ... +5.
    CHECK(kWmContextMenu == 0x007B && kWmLButtonDown == 0x0201 && kWmLButtonUp == 0x0202 && kWmLButtonDblClk == 0x0203);
    CHECK(kWmRButtonDown == 0x0204 && kWmRButtonUp == 0x0205 && kWmRButtonDblClk == 0x0206);
    CHECK(kWmMButtonUp == 0x0208 && kWmMouseMove == 0x0200 && kWmUser == 0x0400);
    CHECK(kNinSelect == 0x0400 && kNinKeySelect == 0x0401 && kNinBalloonShow == 0x0402 && kNinBalloonHide == 0x0403);
    CHECK(kNinBalloonTimeout == 0x0404 && kNinBalloonUserClick == 0x0405 && kNinPopupOpen == 0x0406 && kNinPopupClose == 0x0407);

    // Version 0 (the default; the launcher): lParam is the mouse message; the low word is used, the high word ignored.
    CHECK(DecodeCallback(0x0202, 0).event == TrayEvent::LeftClick);
    CHECK(DecodeCallback(0x0203, 0).event == TrayEvent::LeftDoubleClick);
    CHECK(DecodeCallback(0x0205, 0).event == TrayEvent::RightClick);
    CHECK(DecodeCallback(0x007B, 0).event == TrayEvent::ContextMenu);
    CHECK(DecodeCallback(0x0201, 0).event == TrayEvent::LeftDown);
    CHECK(DecodeCallback(0x0204, 0).event == TrayEvent::RightDown);
    CHECK(DecodeCallback(0x0208, 0).event == TrayEvent::MiddleClick);
    CHECK(DecodeCallback(0x0200, 0).event == TrayEvent::Hover);
    CHECK(DecodeCallback(0x0402, 0).event == TrayEvent::BalloonShown);
    CHECK(DecodeCallback(0x0403, 0).event == TrayEvent::BalloonHidden);
    CHECK(DecodeCallback(0x0404, 0).event == TrayEvent::BalloonTimeout);
    CHECK(DecodeCallback(0x0405, 0).event == TrayEvent::BalloonClicked);
    CHECK(DecodeCallback(0x0406, 0).event == TrayEvent::PopupOpen);
    CHECK(DecodeCallback(0x0407, 0).event == TrayEvent::PopupClose);
    CHECK(DecodeCallback(0x0400, 0).event == TrayEvent::Select);
    CHECK(DecodeCallback(0x0401, 0).event == TrayEvent::KeySelect);
    CHECK(DecodeCallback(0x7777, 0).event == TrayEvent::Unknown);
    // High word ignored in v0: 0x0005'0202 is still a left click, icon id stays 0.
    { auto d = DecodeCallback(0x00050202, 0); CHECK(d.event == TrayEvent::LeftClick && d.iconId == 0); }

    // Version 4 (NOTIFYICON_VERSION_4): low word = event, high word = icon id; wParam = x | y<<16 (screen coords, signed 16 bit).
    { auto d = DecodeCallback(0x00030205, 4); CHECK(d.event == TrayEvent::RightClick && d.iconId == 3); }
    { auto d = DecodeCallback(0x0001007B, 4, 0xFFF60064ull); CHECK(d.event == TrayEvent::ContextMenu && d.iconId == 1);
      CHECK(d.hasPoint && d.x == 100 && d.y == -10); }          // 0x64 = 100, 0xFFF6 = -10 as int16
    { auto d = DecodeCallback(0x00000405, 4, 0x00C80190ull); CHECK(d.event == TrayEvent::BalloonClicked && !d.hasPoint); }
    { auto d = DecodeCallback(0x00020400, 4, 0x00C80190ull); CHECK(d.event == TrayEvent::Select && d.x == 400 && d.y == 200); }
    CHECK(DecodeCallback(0xFFFF0401, 4).iconId == 0xFFFF);

    // Taskbar recreated (explorer restart): the registered message "TaskbarCreated" means the icon must be added again.
    CHECK(NeedsReAdd(0xC0F1, 0xC0F1) && !NeedsReAdd(0xC0F2, 0xC0F1) && !NeedsReAdd(0xC0F1, 0));
    CHECK(NeedsReAdd(0xC0F1, 0xC0F1) == IsTaskbarCreated(0xC0F1, 0xC0F1));

    // Actions: exactly the launcher's current rule (OkayMsgHook): left up / left double click restore; right up / context menu
    // raise the menu; nothing else does anything.
    struct { unsigned lp; bool restore, menu; } t[] = {
        {0x0202, true, false}, {0x0203, true, false}, {0x0205, false, true}, {0x007B, false, true},
        {0x0201, false, false}, {0x0204, false, false}, {0x0200, false, false}, {0x0208, false, false},
        {0x0405, false, false}, {0x0406, false, false}, {0x0000, false, false}};
    for (auto& e : t) {
        const auto a = ActionFor(DecodeCallback(e.lp, 0).event);
        CHECK(a.restore == e.restore && a.menu == e.menu);
        // The launcher masks lParam & 0xFFFF; the model must agree on the same inputs, including high-word noise.
        const auto b = ActionFor(DecodeCallback(e.lp | 0xABCD0000u, 0).event);
        CHECK(b.restore == e.restore && b.menu == e.menu);
    }
    // Version 4 icons send Select / KeySelect for a click or Enter on the focused icon; that opens the app too.
    CHECK(ActionFor(TrayEvent::KeySelect).restore && ActionFor(TrayEvent::Select).restore);
    TEST_MAIN_RESULT();
}
