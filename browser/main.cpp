// OkaySpace Browser: the browser program (system "Browser", docs/feature_map/17-launcher-platform.md 17.18; plan:
// docs/platform/browser_plan.md, step 3). A window of its own with the Browser's page engine (okay/Browser/), the OkayUI view
// with an address field (okayui/screens/BrowserView.hpp) and the shared tab host (shared/BrowserHost.hpp): pages, links,
// back / forward, and OkaySpace web games on a page run natively by the player (step 2), each site in its own folder.
//
//   okay-browser [ADDRESS] [--play]
//
// ADDRESS: the page to open (http or https; "example.com" is read as https://example.com). --play: once the page is
// loaded, start its first game (a desktop shortcut to a game's page). Settings > Experimental > "Browser" (OKAY_BROWSER=0)
// switches our own browser off; the window then says so and loads nothing.
// The window, events and drawing go through okay::platform (window mode of OkayUI: shared/OkayUIFrameLoop.hpp); no Dear ImGui.
#define SDL_MAIN_HANDLED
#define OKAYUI_HOST_PLATFORM 1
#include "OkayUIFrameLoop.hpp"
#include "BrowserHost.hpp"
#include "BrowserView.hpp"
#include "Process.hpp"   // tools/okay-cli: SelfPath (where this program is, to find the player beside it)
#include "okay/Browser/Url.hpp"
#include "okay/Core/StringUtil.hpp"
#include "okay/Platform/Painter.hpp"
#include "okay/Platform/Platform.hpp"
#include "okay/UI/OkayUI.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>

namespace br = okay::browser;
namespace S = okayuiscreens;
namespace fs = std::filesystem;

namespace {

// The player that runs games: beside this program, or in its Tools folder (the release layout, docs/release/packaging.md).
std::string FindPlayer() {
    const fs::path self = fs::u8path(okaycli::SelfPath());
    if (self.empty()) return std::string();
    std::error_code ec;
    for (const char* dir : {"", "Tools"})
        for (const char* name : {"OkaySpacePlayer.exe", "okay-player"}) {
            const fs::path p = dir[0] ? self.parent_path() / dir / name : self.parent_path() / name;
            if (fs::is_regular_file(p, ec)) return p.u8string();
        }
    return std::string();
}

// What the person typed into the address field, as an address: "example.com/x" -> "https://example.com/x".
std::string AddressFromInput(const std::string& typed) {
    std::string text = okay::str::Trim(typed);
    if (text.empty()) return text;
    if (text.find("://") == std::string::npos) text = "https://" + text;
    return text;
}

bool SwitchedOff() {
    const char* e = std::getenv("OKAY_BROWSER");   // Settings > Experimental > "Browser" (shared/OwnSwitches.hpp); unset = on
    return e && e[0] == '0';
}

} // namespace

int main(int argc, char** argv) {
    std::string start;
    bool autoplay = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--help" || a == "-h") {
            std::printf("Usage: %s [ADDRESS] [--play]\n  ADDRESS  the page to open (http or https)\n  --play   start the page's first game once it is loaded\n", argv[0]);
            return 0;
        }
        if (a == "--play") autoplay = true;
        else if (!a.empty() && a[0] != '-') start = a;
    }

    okay::platform::Backend* be = okay::platform::Default();
    if (!be || !be->Init(okay::platform::InitFlags())) {
        std::fprintf(stderr, "[browser] no platform backend: %s\n", be ? be->LastError().c_str() : "none");
        return 1;
    }
    okay::platform::WindowDesc wd;
    wd.title = "OkaySpace Browser";
    wd.width = 1100;
    wd.height = 760;
    wd.resizable = true;
    std::unique_ptr<okay::platform::Window> win = be->MakeWindow(wd);
    if (!win) { std::fprintf(stderr, "[browser] no window: %s\n", be->LastError().c_str()); be->Shutdown(); return 1; }
    std::string err;
    std::unique_ptr<okay::platform::Painter> painter = okay::platform::CreateSdl2Painter(*win, okay::platform::PainterDesc(), &err);
    if (!painter) { std::fprintf(stderr, "[browser] no painter: %s\n", err.c_str()); be->Shutdown(); return 1; }

    br::TabHost::TextureFns tex;
    okay::platform::Painter* p = painter.get();
    tex.make = [p](const std::uint8_t* rgba, int w, int h) -> void* {
        okay::platform::TextureDesc d;
        d.width = w; d.height = h; d.scale = okay::platform::ScaleMode::Linear;
        return p->CreateTexture(d, rgba);
    };
    tex.destroy = [p](void* t) { p->DestroyTexture(t); };
    std::unique_ptr<br::TabHost> host(new br::TabHost(tex, "OkaySpace Browser"));
    br::TabHost::GameRunner runner;
    runner.dataRoot = be->PrefPath("OkaySpace", "Browser");
    runner.playerExe = FindPlayer();
    host->SetGameRunner(runner);
    std::fprintf(stderr, "[browser] data %s, player %s\n", runner.dataRoot.c_str(), runner.playerExe.empty() ? "(not found: games cannot run)" : runner.playerExe.c_str());

    const bool off = SwitchedOff();
    std::string notice;   // one line under the toolbar: why a link or a game did not open
    if (off) notice = "Our own browser is switched off (Settings > Experimental > Browser). Nothing is loaded.";
    else if (!start.empty() && !host->Open(AddressFromInput(start))) notice = "Only http and https addresses can be opened.";

    S::AddressField field;
    S::BrowserViewOptions opt;
    opt.address = &field;
    opt.hostButtons = false;
    opt.embedState = [&host](const br::DrawCmd& c) {
        S::EmbedState st;
        const std::string url = host->GameUrlFor(c.tag, c.text);
        if (url.empty()) {
            if (c.tag == "okay-game") st.status = host->CanRunGames() ? "This game is on another site and cannot run here." : "The OkaySpace player is missing, so games cannot run.";
            return st;
        }
        st.playable = true;
        st.title = host->GameTitle();
        const br::TabHost::GameStatus g = host->Game(url);
        switch (g.phase) {
        case br::TabHost::GameStatus::None: st.status = "Runs natively with the OkaySpace player"; break;
        case br::TabHost::GameStatus::Downloading:
            st.busy = true;
            st.status = "Downloading... " + std::to_string((unsigned long long)(g.bytes >> 10)) + " KB";
            break;
        case br::TabHost::GameStatus::Unpacking: st.busy = true; st.status = "Checking and unpacking..."; break;
        case br::TabHost::GameStatus::Running: st.busy = true; st.status = "Running in its own window"; break;
        case br::TabHost::GameStatus::Finished: st.status = g.message; break;
        case br::TabHost::GameStatus::Failed: st.status = g.message; break;
        }
        return st;
    };

    OkayUI::UseLauncherLook(true);
    okayuihost::platform::FrameLoop loop(*be, *painter);
    bool autoplayDone = false;
    while (loop.Frame([&] {
        br::BrowserTab& tab = host->Tab();
        if (!off) host->Pump();
        const float W = (float)loop.GetHost().DisplayWidth(), H = (float)loop.GetHost().DisplayHeight();
        const float noticeH = notice.empty() ? 0.0f : 24.0f;
        if (!notice.empty()) {
            const auto& fg = OkayUI::GetForegroundDrawList();
            fg->AddRectFilled(OkayUI::Vec2(0, H - noticeH), OkayUI::Vec2(W, H), 0xF0302A22u);
            fg->AddText(OkayUI::Vec2(8, H - noticeH + 6), 0xFFB8E8F5u, notice.c_str());
        }
        const S::BrowserViewResult r = S::DrawBrowserView(tab, 0, 0, W, H - noticeH,
            [&host](const std::string& src) { return (SDL_Texture*)host->ImageFor(src); }, opt);
        if (off) return;
        if (r.back) { tab.GoBack(); notice.clear(); }
        if (r.forward) { tab.GoForward(); notice.clear(); }
        if (r.reload) tab.Reload();
        if (!r.navigate.empty()) {
            notice.clear();
            if (!host->Open(AddressFromInput(r.navigate))) notice = "Only http and https addresses can be opened.";
        }
        if (r.linkClicked >= 0) {
            const br::LayoutResult& page = tab.Layout(r.page.w, S::OkayUIMeasure());
            if (r.linkClicked < (int)page.links.size()) {
                const br::BrowserTab::Action a = tab.Activate(page.links[(std::size_t)r.linkClicked]);
                // The tab keeps to one site; in the browser program another site is simply a new visit in the same window.
                if (a.kind == br::BrowserTab::Action::External) host->Open(a.url);
                else if (a.kind == br::BrowserTab::Action::Refused) notice = "That link cannot be opened (only http and https).";
                else notice.clear();
            }
        }
        int play = r.playEmbed;
        if (autoplay && !autoplayDone && tab.GetState() == br::BrowserTab::State::Ready) {
            autoplayDone = true;
            const br::LayoutResult& page = tab.Layout(r.page.w, S::OkayUIMeasure());
            for (int ei : page.embeds)
                if (!host->GameUrlFor(page.cmds[(std::size_t)ei].tag, page.cmds[(std::size_t)ei].text).empty()) { play = ei; break; }
        }
        if (play >= 0) {
            const br::LayoutResult& page = tab.Layout(r.page.w, S::OkayUIMeasure());
            if (play < (int)page.cmds.size()) {
                const br::DrawCmd& c = page.cmds[(std::size_t)play];
                std::string why;
                if (!host->Play(host->GameUrlFor(c.tag, c.text), why)) notice = why;
                else notice.clear();
            }
        }
    })) {}
    host.reset();   // stops a running game and frees the textures while the painter still exists
    painter.reset();
    win.reset();
    be->Shutdown();
    return 0;
}
