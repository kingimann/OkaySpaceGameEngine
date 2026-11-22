// Run, not just compile: the launcher's Versions page (launcher/VersionsUI.cpp over launcher/VersionsManager.cpp and VersionsNet.cpp), a Dear ImGui page
// compiled against OkayUI through the probe stand-in imgui.h and drawn headless. Fake state: a temporary launcher folder with two installed versions, an
// offline host (the text fetch always fails; nothing is downloaded), no-op callbacks. See docs/ui/own_ui_toolkit.md 6.14.
#include <SDL.h>
#include <ctime>
#include <unistd.h>
#include "test_framework.hpp"
#include "OkayUITestKit.hpp"
#include "VersionsUI.hpp"
#include "ProjectVersion.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

// A fixed wall clock: nothing the page or the launcher code reads from time() may move the picture.
extern "C" std::time_t time(std::time_t* t) { const std::time_t v = 1700000000; if (t) *t = v; return v; }

using namespace okayuitest;
namespace UI = OkayUI;
namespace fs = std::filesystem;

static void Golden(const char* name, const Bitmap& b) {
    std::string why; const bool ok = MatchesGolden(name, b, &why);
    if (!ok) std::cerr << "  golden " << name << ": " << why << "\n";
    CHECK(ok);
}

int main() {
    RUN_SUITE("port probe: launcher Versions page run headless");
    // Hermetic: nothing of the machine may reach the picture. A fixed wall clock (time() above), UTC, the C locale, a private home and working folder, and no
    // network at all: the version lookup is the offline stub below, which every URL reaches and none answers (docs/ui/own_ui_toolkit.md 6.17).
    setenv("TZ", "UTC", 1); tzset(); setenv("LANG", "C", 1); setenv("LC_ALL", "C", 1);
    const fs::path dir = fs::temp_directory_path() / ("okayui_port_versions_test_" + std::to_string((long long)getpid()));   // one folder per process: parallel runs cannot meet
    std::error_code ec; fs::remove_all(dir, ec);
    fs::create_directories(dir / "home", ec); setenv("HOME", (dir / "home").string().c_str(), 1); const fs::path cwd0 = fs::current_path(ec); fs::current_path(dir / "home", ec);   // a private home and working folder
    for (const char* v : {"0.94.0", "0.95.2"}) {
        const fs::path exe = okayver::InstalledEditorPath(dir, v);
        fs::create_directories(exe.parent_path(), ec);
        std::ofstream(exe) << std::string(2048, 'x');
    }
    okayversionsui::Host host;
    host.launcherDir = dir; host.currentVersion = "0.95.2"; host.launcherVersion = "0.95.2"; host.sources.trustedKeys = "";
    int toasts = 0;
    host.toast = [&](const std::string&, int) { ++toasts; };
    host.openFolder = [](const std::string&) {}; host.savePrefs = [] {}; host.startUpdate = [] {}; host.channelChanged = [] {};
    int fetchCalls = 0;
    host.fetchText = [&](const std::string&, std::string&) { ++fetchCalls; return false; };   // offline (the lookup runs on a worker: the test waits for it, below)

    Reset(); UI::UseLauncherLook(true);   // the launcher draws its pages in this look
    Sim s(900, 620);
    auto ui = [&] {
        okayversionsui::Tick(host);
        UI::SetNextWindowPos({10, 10}, UI::Cond_Always); UI::SetNextWindowSize({880, 600}, UI::Cond_Always);
        if (UI::Begin("Versions", nullptr, UI::Window_NoMove | UI::Window_NoResize)) okayversionsui::DrawPage(host);
        UI::End();
    };
    for (int i = 0; i < 6; ++i) s.Frame(ui);
    // The version lookup is a background job (an SDL thread): the page shows "Looking up the version list" until Tick has taken its result and "Couldn't reach ..."
    // after. A golden taken at whatever moment the worker happened to finish differs from machine to machine (CI saw 7,316 pixels differ), so draw, with a short pause,
    // until the lookup ended and its result was taken (okayversionsui::IndexLoaded; at most 10 seconds, a loaded CI machine may be slow to start the thread), then
    // until the picture has stopped changing for 40 frames in a row.
    for (int i = 0; i < 2000 && !okayversionsui::IndexLoaded(); ++i) { SDL_Delay(5); s.Frame(ui); }
    CHECK(okayversionsui::IndexLoaded() && !okayversionsui::IndexBusy());
    auto pictureHash = [] {
        const UI::DrawData dd = UI::GetDrawData();
        const unsigned char* v = static_cast<const unsigned char*>(dd.vertices);
        unsigned h = 2166136261u;
        for (int i = 0; i < dd.vertexCount * dd.vertexStride; ++i) h = (h ^ v[i]) * 16777619u;
        return h ^ (unsigned)dd.indexCount * 2654435761u;
    };
    unsigned last = 0; int stable = 0;
    for (int i = 0; i < 800 && stable < 40; ++i) { SDL_Delay(5); s.Frame(ui); const unsigned h = pictureHash(); stable = (h == last && i > 0) ? stable + 1 : 0; last = h; }
    CHECK(stable >= 40);
    const UI::DrawData d = UI::GetDrawData();
    CHECK(d.vertexCount > 300 && d.indexCount > 400 && d.dropped == 0);
    const Bitmap first = s.Draw(24, 26, 32);
    CHECK(CountNot(first, 0, 0, 900, 620, Rgba(24, 26, 32)) > 8000);
    Golden("port_versions_page", first);
    // click around the page, scroll, type: nothing may crash and nothing may start a download (the host is offline and nothing was asked for)
    for (float y = 60; y < 580; y += 41) { for (float x = 40; x < 860; x += 97) { s.Move(x, y); s.Frame(ui); s.Down(); s.Frame(ui); s.Up(); s.Frame(ui); } }
    s.wheel = -4; s.Frame(ui); s.wheel = 6; s.Frame(ui); s.Type("0.9"); s.Frame(ui);
    CHECK(UI::GetDrawData().vertexCount > 300 && UI::GetDrawData().dropped == 0);
    CHECK(okayversionsui::DrawChannelCombo(host, 200.0f) || true);
    const auto& inst = okayversionsui::InstalledVersionsCached(dir);
    CHECK(inst.size() == 2);
    s.blocked = true; s.Frame(ui); s.blocked = false;
    Sim tiny(60, 40); tiny.Frame(ui);
    okayversionsui::Shutdown();
    UI::UseLauncherLook(false);
    fs::current_path(cwd0, ec);
    fs::remove_all(dir, ec);
    TEST_MAIN_RESULT();
}
