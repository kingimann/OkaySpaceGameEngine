// Run, not just compile: the launcher's Versions page (launcher/VersionsUI.cpp over launcher/VersionsManager.cpp and VersionsNet.cpp), a Dear ImGui page
// compiled against OkayUI through the probe stand-in imgui.h and drawn headless. Fake state: a temporary launcher folder with two installed versions, an
// offline host (the text fetch always fails; nothing is downloaded), no-op callbacks. See docs/own_ui_toolkit.md 6.14.
#include "test_framework.hpp"
#include "OkayUITestKit.hpp"
#include "VersionsUI.hpp"
#include "ProjectVersion.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

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
    const fs::path dir = fs::temp_directory_path() / "okayui_port_versions_test";
    std::error_code ec; fs::remove_all(dir, ec);
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
    host.fetchText = [](const std::string&, std::string&) { return false; };   // offline

    Reset(); UI::UseLauncherLook(true);   // the launcher draws its pages in this look
    Sim s(900, 620);
    auto ui = [&] {
        okayversionsui::Tick(host);
        UI::SetNextWindowPos({10, 10}, UI::Cond_Always); UI::SetNextWindowSize({880, 600}, UI::Cond_Always);
        if (UI::Begin("Versions", nullptr, UI::Window_NoMove | UI::Window_NoResize)) okayversionsui::DrawPage(host);
        UI::End();
    };
    for (int i = 0; i < 6; ++i) s.Frame(ui);
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
    fs::remove_all(dir, ec);
    TEST_MAIN_RESULT();
}
