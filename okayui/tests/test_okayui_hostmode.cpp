// Window mode (shared/OkayUIHostMode.hpp, editor/OkayUIHostMode.cpp), headless: the three-switch gate, and the window-mode screen run through the real frame
// loop with a fake backend and a software painter. Steps 1 and 2 of the plan in docs/own_ui_toolkit.md 6.23.
#define OKAY_HAVE_OKAYUI 1
#define OKAYUI_HOST_PLATFORM 1
#include "test_framework.hpp"
#include "OkayUITestKit.hpp"
#include "OkayUIFrameLoop.hpp"
#include "OkayUIHostMode.hpp"
#include "FakePlatformBackend.hpp"
#include "okay/UI/OkayUIDock.hpp"

#include <string>
#include <vector>

namespace UI = OkayUI;
using fakeplat::FakeBackend; using fakeplat::Move; using fakeplat::Btn;

int main() {
    RUN_SUITE("okayui window mode");
    // ---- the gate: all three switches, nothing else ----
    CHECK(oked::HostModeRequested("1", "okayui", "okayui"));
    CHECK(oked::HostModeRequested("1", "OkayUI", "okayui") && oked::HostModeRequested("yes", "okay ui", "okayui"));
    CHECK(!oked::HostModeRequested(nullptr, "okayui", "okayui") && !oked::HostModeRequested("", "okayui", "okayui") && !oked::HostModeRequested("0", "okayui", "okayui"));   // no OKAY_DEV
    CHECK(!oked::HostModeRequested("1", nullptr, "okayui") && !oked::HostModeRequested("1", "imgui", "okayui") && !oked::HostModeRequested("1", "", "okayui") && !oked::HostModeRequested("1", "junk", "okayui"));
    CHECK(!oked::HostModeRequested("1", "okayui", nullptr) && !oked::HostModeRequested("1", "okayui", "") && !oked::HostModeRequested("1", "okayui", "imgui") && !oked::HostModeRequested("1", "okayui", "OkayUI "));
    CHECK(!oked::HostModeRequested(nullptr, nullptr, nullptr));

    // ---- the screen through the loop ----
    okayuitest::Reset();
    auto painter = UI::soft::CreateBitmapPainter(960, 600);
    FakeBackend be;
    okayuihost::platform::FrameLoop loop(be, *painter);
    oked::HostModeState st;
    auto frame = [&] { return loop.Frame([&] { oked::DrawHostModeUi(st); }); };
    for (int i = 0; i < 4; ++i) { be.now += 16; CHECK(frame()); }
    CHECK(st.frame == 4 && st.docked);
    const UI::DockLayout& L = UI::GetDockLayout();
    CHECK(L.Contains("Audio Mixer") && L.Contains("Window mode") && !L.IsFloating("Audio Mixer") && !L.IsFloating("Window mode") && L.NodeOf("Audio Mixer") != L.NodeOf("Window mode"));
    std::string why; CHECK(L.Validate(&why));
    // the two panels share the display: the status panel is the narrower one on the right
    const UI::DockLayout::Node* nm = L.Find(L.NodeOf("Audio Mixer")); const UI::DockLayout::Node* ns = L.Find(L.NodeOf("Window mode"));
    CHECK(nm && ns && nm->rect.x == 0.0f && ns->rect.x > nm->rect.x + 100.0f && ns->rect.w < nm->rect.w && nm->rect.h > 500.0f);
    CHECK(nm && ns && nm->rect.w + ns->rect.w + UI::DockLayout::kSplitter > 940.0f);
    std::vector<std::uint8_t> px; int w = 0, h = 0;
    CHECK(painter->ReadPixels(px, w, h) && w == 960 && h == 600);
    int lit = 0; for (size_t i = 0; i + 3 < px.size(); i += 4) if (px[i] != 24 || px[i + 1] != 26 || px[i + 2] != 32) ++lit;
    CHECK(lit > 100000);   // the dock space and both panels filled the display
    // a window resize: the painter follows, the loop notices, the layout keeps filling the display
    painter = UI::soft::CreateBitmapPainter(640, 400);   // (a new painter: a bitmap painter has a fixed size)
    okayuihost::platform::FrameLoop loop2(be, *painter);
    oked::HostModeState st2;
    UI::ResetState(); UI::Style().caretBlink = false;
    for (int i = 0; i < 3; ++i) loop2.Frame([&] { oked::DrawHostModeUi(st2); });
    const UI::DockLayout& L2 = UI::GetDockLayout();
    const UI::DockLayout::Node* a2 = L2.Find(L2.NodeOf("Audio Mixer"));
    CHECK(a2 && a2->rect.h > 380.0f && loop2.GetHost().DisplayWidth() == 640);
    // closing a panel's tab removes it from the layout and the screen goes on
    const UI::DockTabInfo* closeTab = nullptr; std::vector<UI::DockTabInfo> tabs = UI::GetDockTabs();
    for (const UI::DockTabInfo& t : tabs) if (t.window == "Window mode") closeTab = &t;
    CHECK(closeTab != nullptr);
    if (closeTab) {
        const int cx = (int)(closeTab->rect.x + closeTab->rect.w - 6.0f), cy = (int)(closeTab->rect.y + closeTab->rect.h * 0.5f);
        be.queue.push_back(Move(cx, cy)); loop2.Frame([&] { oked::DrawHostModeUi(st2); });
        be.queue.push_back(Btn(true, cx, cy)); loop2.Frame([&] { oked::DrawHostModeUi(st2); });
        be.queue.push_back(Btn(false, cx, cy)); loop2.Frame([&] { oked::DrawHostModeUi(st2); }); loop2.Frame([&] { oked::DrawHostModeUi(st2); });
        CHECK(!st2.IsOpen("Window mode") && !UI::GetDockLayout().Contains("Window mode") && UI::GetDockLayout().Contains("Audio Mixer"));
    }
    // quitting
    be.queue.push_back(fakeplat::Ev(okay::platform::EventType::Quit));
    CHECK(!loop2.Frame([&] { oked::DrawHostModeUi(st2); }));
    TEST_MAIN_RESULT();
}
