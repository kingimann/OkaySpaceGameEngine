// Window-mode persistence (shared/OkayUIHostState.hpp and the state calls of editor/OkayUIHostMode.cpp), headless: round trip, hostile and damaged files, the
// window sanitiser, atomic saving and the throttled saver, and two simulated runs through the real frame loop (the second restores the first's layout, panel
// flags and window). Step 3 of the window-mode plan, docs/own_ui_toolkit.md 6.24.
#define OKAY_HAVE_OKAYUI 1
#define OKAYUI_HOST_PLATFORM 1
#include "test_framework.hpp"
#include "OkayUITestKit.hpp"
#include "OkayUIFrameLoop.hpp"
#include "OkayUIHostMode.hpp"
#include "FakePlatformBackend.hpp"
#include "okay/UI/OkayUIDock.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace UI = OkayUI;
namespace fs = std::filesystem;
namespace oh = okayuihost;
using fakeplat::FakeBackend; using fakeplat::Move; using fakeplat::Btn;

static std::string Slurp(const fs::path& p) { std::ifstream f(p, std::ios::binary); return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>()); }
static void Spit(const fs::path& p, const std::string& t) { std::ofstream f(p, std::ios::binary | std::ios::trunc); f << t; }

int main() {
    RUN_SUITE("okayui window-mode state");
    const fs::path dir = fs::temp_directory_path() / "okayui_hoststate_test";
    std::error_code ec; fs::remove_all(dir, ec); fs::create_directories(dir, ec);

    {   // ---- round trip ----
        oh::PersistedState a;
        a.window.x = -30; a.window.y = 45; a.window.w = 1111; a.window.h = 777;
        a.open["Audio Mixer"] = true; a.open["Window mode"] = false; a.open["100% odd name"] = false; a.open[u8"Ünï \xE2\x80\x94 panel"] = true;
        a.layout = "okaydock 1\nroot 1\nnode 1 leaf 0 Audio%20Mixer\n";
        oh::PersistedState b;
        CHECK(oh::Parse(oh::Serialize(a), b) && b == a);
        oh::PersistedState e;   // nothing set: default window, no panels, no layout
        CHECK(oh::Parse(oh::Serialize(e), b) && b == e && b.window.x == INT_MIN);
        // CRLF line ends and trailing junk lines are tolerated
        std::string crlf = oh::Serialize(a); std::string r; for (char c : crlf) { if (c == '\n') r += "\r\n"; else r += c; }
        CHECK(oh::Parse(r, b) && b.window == a.window && b.open == a.open);
    }
    {   // ---- hostile text ----
        oh::PersistedState keep; keep.window.w = 999; oh::PersistedState o = keep;
        CHECK(!oh::Parse("", o) && !oh::Parse("okayhost", o) && !oh::Parse("okayhost 2\nwindow 1 2 3 4 1\n", o) && !oh::Parse("hello\nwindow 1 2 3 4 1\n", o) && o == keep);
        CHECK(!oh::Parse(std::string(300 * 1024, 'x'), o) && !oh::Parse("okayhost 1\n" + std::string(300 * 1024, 'y'), o) && o == keep);
        // absurd numbers keep the default size; a position far away is dropped
        CHECK(oh::Parse("okayhost 1\nwindow 99999999 -99999999 -5 0 1\n", o) && o.window.w == 1280 && o.window.h == 800 && o.window.x == INT_MIN);
        CHECK(oh::Parse("okayhost 1\nwindow nan inf 20000 20000 1\nopen A 7\nopen B x\nopen  1\nopen C 1\n", o) && o.window.w == 1280 && o.open.size() == 1 && o.open.count("C"));
        // too many panels, a huge name, a name that is only an escape
        std::string many = "okayhost 1\n"; for (int i = 0; i < 500; ++i) many += "open P" + std::to_string(i) + " 1\n";
        CHECK(oh::Parse(many, o) && o.open.size() <= oh::kMaxPanels);
        CHECK(oh::Parse("okayhost 1\nopen " + std::string(5000, 'n') + " 1\n", o) && o.open.empty());
        // a random mutation run: never crashes, and whatever is accepted is inside the limits
        oh::PersistedState a; a.window.x = 10; a.window.y = 20; a.open["Audio Mixer"] = true; a.layout = "okaydock 1\nroot 1\nnode 1 leaf 0 A\n";
        const std::string base = oh::Serialize(a);
        unsigned seed = 99u; auto rnd = [&] { seed = seed * 1664525u + 1013904223u; return seed >> 8; };
        int accepted = 0;
        for (int i = 0; i < 3000; ++i) {
            std::string t = base; const int edits = 1 + (int)(rnd() % 6u);
            for (int k = 0; k < edits; ++k) { const size_t p = rnd() % (t.size() + 1); const unsigned m = rnd() % 4u; if (m == 0 && p < t.size()) t.erase(p, 1 + rnd() % 4u); else if (m == 1) t.insert(p, 1, (char)(rnd() & 255)); else if (m == 2 && p < t.size()) t[p] = (char)(rnd() & 255); else t.resize(p); }
            oh::PersistedState got;
            if (oh::Parse(t, got)) { ++accepted; CHECK(got.window.w >= 1 && got.window.w <= 16384 && got.window.h >= 1 && got.window.h <= 16384 && got.open.size() <= oh::kMaxPanels); for (const auto& kv : got.open) CHECK(!kv.first.empty() && kv.first.size() <= oh::kMaxPanelName); }
        }
        CHECK(accepted > 1000);
    }
    {   // ---- sanitising the window against the display ----
        oh::WindowState w; w.x = 100; w.y = 80; w.w = 900; w.h = 600;
        CHECK(oh::Sanitize(w, 0, 0, 1920, 1080) == w);                                                   // fits: unchanged
        oh::WindowState off = w; off.x = 5000;
        oh::WindowState f = oh::Sanitize(off, 0, 0, 1920, 1080);
        CHECK(f.x >= 0 && f.x + f.w <= 1920 && f.y >= 0 && f.y + f.h <= 1080 && f.w == 900);              // a gone monitor: back on the display
        off = w; off.x = -2000; off.y = -900; f = oh::Sanitize(off, 0, 0, 1920, 1080);
        CHECK(f.x >= 0 && f.y >= 0);
        oh::WindowState big = w; big.w = 9000; big.h = 9000; f = oh::Sanitize(big, 0, 0, 1366, 768);
        CHECK(f.w == 1366 && f.h == 768);                                                                  // bigger than the display: fitted
        oh::WindowState tiny = w; tiny.w = 5; tiny.h = 5; f = oh::Sanitize(tiny, 0, 0, 1920, 1080);
        CHECK(f.w == 320 && f.h == 200);
        oh::WindowState unk; f = oh::Sanitize(unk, 0, 0, 1920, 1080);
        CHECK(f.x == INT_MIN && f.y == INT_MIN);                                                           // no position: the window manager places it
        f = oh::Sanitize(off, 0, 0, 0, 0); CHECK(f.w == 900);                                              // unknown display: only the size limits
        f = oh::Sanitize(w, 1920, 0, 1920, 1080);                                                          // a second monitor to the right: a window on the first is moved onto it
        CHECK(f.x >= 1920 - 1 || f.x + f.w > 1920 + 100);
    }
    {   // ---- files ----
        const fs::path p = dir / "sub" / "state.txt";
        oh::PersistedState a; a.window.x = 1; a.window.y = 2; a.open["P"] = true; a.layout = "okaydock 1\nroot 1\nnode 1 leaf 0 P\n";
        CHECK(oh::SaveFileAtomic(p, a) && fs::exists(p) && !fs::exists(p.string() + ".tmp"));
        oh::PersistedState b; CHECK(oh::LoadFile(p, b) && b == a);
        // the same again over the old file
        a.open["Q"] = false; CHECK(oh::SaveFileAtomic(p, a) && oh::LoadFile(p, b) && b == a);
        // a failing save leaves the old file alone: the folder is a file
        Spit(dir / "blocker", "x");
        CHECK(!oh::SaveFileAtomic(dir / "blocker" / "state.txt", a));
        Spit(p, "damaged\n"); CHECK(!oh::LoadFile(p, b));
        CHECK(!oh::LoadFile(dir / "missing.txt", b) && !oh::LoadFile(dir, b));
        Spit(p, std::string(300 * 1024, 'x')); CHECK(!oh::LoadFile(p, b));
        // a path with no folder part
        const fs::path cwd = fs::current_path(ec); fs::current_path(dir, ec);
        CHECK(oh::SaveFileAtomic("bare_state.txt", a) && fs::exists(dir / "bare_state.txt")); fs::current_path(cwd, ec);
    }
    {   // ---- the throttled saver ----
        const fs::path p = dir / "saver.txt"; fs::remove(p, ec);
        oh::StateSaver sv(p, 2000);
        oh::PersistedState a; a.open["P"] = true;
        CHECK(sv.Offer(a, 1000) && sv.Writes() == 1);
        CHECK(!sv.Offer(a, 9000) && sv.Writes() == 1);                 // unchanged: nothing written however late
        oh::PersistedState b = a; b.open["P"] = false;
        CHECK(!sv.Offer(b, 1500) && sv.Writes() == 1);                 // changed, but too soon
        CHECK(sv.Offer(b, 3100) && sv.Writes() == 2);                  // changed and old enough
        oh::PersistedState c = b; c.window.w = 640;
        CHECK(!sv.Offer(c, 3200) && sv.Flush(c, 3300) && sv.Writes() == 3);   // closing: flush ignores the wait
        CHECK(!sv.Flush(c, 3400) && sv.Writes() == 3);
        oh::PersistedState got; CHECK(oh::LoadFile(p, got) && got == c);
        oh::StateSaver bad(dir / "blocker" / "x.txt", 0);
        CHECK(!bad.Offer(a, 0) && bad.Writes() == 0 && !bad.Offer(a, 5) && bad.Writes() == 0);   // a failing path never counts as saved
    }
    {   // ---- the state file path ----
        CHECK(oked::HostStatePath("C:/x/state.txt", "/pref/") == "C:/x/state.txt");
        CHECK(oked::HostStatePath(nullptr, "/pref/") == "/pref/okay_uihost.txt" && oked::HostStatePath("", "/pref/") == "/pref/okay_uihost.txt");
        CHECK(oked::HostStatePath(nullptr, "").empty() && oked::HostStatePath("", "").empty());
    }
    {   // ---- two runs through the frame loop ----
        const fs::path p = dir / "run.txt"; fs::remove(p, ec);
        UI::Rect closeBox;
        // run 1: default layout, close the status panel's tab, a window the host moved and resized
        {
            okayuitest::Reset();
            auto painter = UI::soft::CreateBitmapPainter(800, 500);
            FakeBackend be; okayuihost::platform::FrameLoop loop(be, *painter);
            oked::HostModeState st; oh::StateSaver saver(p, 0);
            auto frame = [&] { return loop.Frame([&] { oked::DrawHostModeUi(st); }); };
            for (int i = 0; i < 4; ++i) frame();
            CHECK(st.docked && UI::GetDockLayout().Contains("Window mode"));
            for (const UI::DockTabInfo& t : UI::GetDockTabs()) if (t.window == "Window mode") closeBox = UI::Rect{t.rect.x + t.rect.w - 6.0f, t.rect.y + t.rect.h * 0.5f, 0, 0};
            be.queue.push_back(Move((int)closeBox.x, (int)closeBox.y)); frame();
            be.queue.push_back(Btn(true, (int)closeBox.x, (int)closeBox.y)); frame();
            be.queue.push_back(Btn(false, (int)closeBox.x, (int)closeBox.y)); frame(); frame();
            CHECK(!st.IsOpen("Window mode") && !UI::GetDockLayout().Contains("Window mode"));
            oh::WindowState w; w.x = 50; w.y = 60; w.w = 800; w.h = 500;
            CHECK(saver.Flush(oked::ToPersisted(st, w), 10));
        }
        const std::string text = Slurp(p);
        CHECK(text.rfind("okayhost 1", 0) == 0 && text.find("open Window%20mode 0") != std::string::npos && text.find("\nlayout\nokaydock 1") != std::string::npos && text.find("window 50 60 800 500 1") != std::string::npos);
        // run 2: load it back; the layout, the closed panel and the window come back
        {
            okayuitest::Reset();
            oh::PersistedState got; CHECK(oh::LoadFile(p, got) && got.window.w == 800 && got.window.x == 50);
            auto painter = UI::soft::CreateBitmapPainter(800, 500);
            FakeBackend be; okayuihost::platform::FrameLoop loop(be, *painter);
            oked::HostModeState st; oked::ApplyPersisted(st, got);
            for (int i = 0; i < 4; ++i) loop.Frame([&] { oked::DrawHostModeUi(st); });
            const UI::DockLayout& L = UI::GetDockLayout();
            CHECK(!st.IsOpen("Window mode") && st.IsOpen("Audio Mixer") && L.Contains("Audio Mixer") && !L.Contains("Window mode") && !L.IsFloating("Audio Mixer"));
            const UI::DockLayout::Node* n = L.Find(L.NodeOf("Audio Mixer"));
            CHECK(n && n->rect.w > 700.0f && n->rect.h > 450.0f);   // the only panel left fills the display
            std::string why; CHECK(L.Validate(&why));
        }
        // run 3: a damaged state file (valid header, garbage layout and numbers): the default layout is built, nothing crashes
        {
            Spit(p, "okayhost 1\nwindow -1 -1 0 0 1\nopen Audio%20Mixer 1\nlayout\nnode 99 junk\n\x01\x02\xff\n");
            okayuitest::Reset();
            oh::PersistedState got; CHECK(oh::LoadFile(p, got));
            auto painter = UI::soft::CreateBitmapPainter(800, 500);
            FakeBackend be; okayuihost::platform::FrameLoop loop(be, *painter);
            oked::HostModeState st; oked::ApplyPersisted(st, got);
            for (int i = 0; i < 3; ++i) loop.Frame([&] { oked::DrawHostModeUi(st); });
            const UI::DockLayout& L = UI::GetDockLayout();
            std::string why; CHECK(L.Validate(&why) && L.Contains("Audio Mixer") && L.Contains("Window mode"));
        }
    }
    fs::remove_all(dir, ec);
    TEST_MAIN_RESULT();
}
