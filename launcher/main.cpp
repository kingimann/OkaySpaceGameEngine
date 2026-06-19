// OkaySpace Launcher — a small FiveM-style front end with three sections:
//   Create      launch the engine/editor to build a game
//   Play        run a game you've built (any game.okayscene found nearby)
//   Marketplace browse starter templates to open in the editor
//
// It looks for OkaySpaceEngine.exe (editor) and OkaySpacePlayer.exe (runtime)
// sitting next to it, and launches them. Built with Dear ImGui + SDL2.
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include "AppIcon.hpp"
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

#ifndef OKAY_ENGINE_VERSION
#  define OKAY_ENGINE_VERSION "dev"
#endif

namespace {

std::string g_exeDir = ".";
std::string g_selfPath;   // absolute path of the running launcher exe

// Raw GitHub URL of the published dist/ folder (where the exes + VERSION live).
const char* kRawBase =
    "https://raw.githubusercontent.com/kingimann/OkaySpaceGameEngine/main/dist/";

// ---- Auto-updater ----------------------------------------------------------
// On startup the launcher checks the published version and, if newer (or a
// runtime is missing), downloads the engine + player in the background and
// stages a new launcher for the next start. State is written by a worker
// thread and read by the UI. SDL's threading primitives are used because the
// MinGW win32 threads model doesn't provide std::thread / std::mutex.
enum UpState { Up_Idle, Up_Checking, Up_Downloading, Up_UpToDate, Up_Updated, Up_Failed };
SDL_atomic_t g_upState;          // holds a UpState value
SDL_mutex* g_upMutex = nullptr;
std::string g_upMessage = "Checking for updates...";
std::string g_upLatest;
bool g_upRelaunchNeeded = false;
SDL_Thread* g_upThread = nullptr;

void SetState(UpState s) { SDL_AtomicSet(&g_upState, (int)s); }
UpState GetState() { return (UpState)SDL_AtomicGet(&g_upState); }

void SetUpMsg(const std::string& m) {
    if (g_upMutex) SDL_LockMutex(g_upMutex);
    g_upMessage = m;
    if (g_upMutex) SDL_UnlockMutex(g_upMutex);
}
std::string GetUpMsg() {
    std::string m;
    if (g_upMutex) SDL_LockMutex(g_upMutex);
    m = g_upMessage;
    if (g_upMutex) SDL_UnlockMutex(g_upMutex);
    return m;
}

// Defeat the GitHub raw CDN cache (which can serve a stale VERSION.txt or .exe
// for minutes after a release) with a unique query string per request.
std::string BustCache(const std::string& url) {
    static unsigned counter = 0;
    auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    std::string sep = url.find('?') == std::string::npos ? "?" : "&";
    return url + sep + "okaycb=" + std::to_string(now) + "_" + std::to_string(counter++);
}

// Download a URL to a file using whatever HTTP client is on the system.
bool Download(const std::string& url, const std::string& out) {
    std::error_code ec; fs::remove(out, ec);
    std::string u = BustCache(url);
    const char* nc = "-H \"Cache-Control: no-cache\" -H \"Pragma: no-cache\" ";
#if defined(_WIN32)
    std::string c1 = "curl -L -s -f " + std::string(nc) + "-o \"" + out + "\" \"" + u + "\"";
    if (std::system(c1.c_str()) == 0 && fs::exists(out)) return true;
    std::string c2 = "powershell -NoProfile -Command \"try { Invoke-WebRequest "
                     "-Headers @{'Cache-Control'='no-cache'} "
                     "-UseBasicParsing -Uri '" + u + "' -OutFile '" + out +
                     "' } catch { exit 1 }\"";
    return std::system(c2.c_str()) == 0 && fs::exists(out);
#else
    std::string c1 = "curl -L -s -f " + std::string(nc) + "-o \"" + out + "\" \"" + u + "\" 2>/dev/null";
    if (std::system(c1.c_str()) == 0 && fs::exists(out)) return true;
    std::string c2 = "wget -q --no-cache -O \"" + out + "\" \"" + u + "\" 2>/dev/null";
    return std::system(c2.c_str()) == 0 && fs::exists(out);
#endif
}

// Compare dotted versions ("1.5.0"); returns -1 / 0 / 1 for a<b / a==b / a>b.
int CompareVersions(const std::string& a, const std::string& b) {
    auto parse = [](const std::string& s) {
        std::vector<int> v; std::stringstream ss(s); std::string tok;
        while (std::getline(ss, tok, '.')) {
            int n = 0; try { n = std::stoi(tok); } catch (...) { n = 0; }
            v.push_back(n);
        }
        return v;
    };
    std::vector<int> va = parse(a), vb = parse(b);
    std::size_t n = std::max(va.size(), vb.size());
    for (std::size_t i = 0; i < n; ++i) {
        int x = i < va.size() ? va[i] : 0;
        int y = i < vb.size() ? vb[i] : 0;
        if (x != y) return x < y ? -1 : 1;
    }
    return 0;
}

// The installed version: a VERSION.txt beside the launcher (written after each
// upgrade) or, if newer, the version this launcher was compiled with.
std::string LocalVersion() {
    std::error_code ec;
    fs::path vf = fs::path(g_exeDir) / "VERSION.txt";
    if (fs::exists(vf, ec)) {
        std::ifstream f(vf); std::string v; std::getline(f, v);
        while (!v.empty() && (v.back() == '\r' || v.back() == '\n' || v.back() == ' '))
            v.pop_back();
        if (!v.empty())
            return CompareVersions(v, OKAY_ENGINE_VERSION) >= 0 ? v : OKAY_ENGINE_VERSION;
    }
    return OKAY_ENGINE_VERSION;
}

// Download <url> to <dest>.new, validate it, then replace <dest>. If the file
// is in use (the running launcher) the old exe is renamed aside first, which
// Windows allows; the swap then takes effect on the next launch.
bool ReplaceFile(const std::string& url, const fs::path& dest, bool inUse) {
    fs::path tmp = dest; tmp += ".new";
    if (!Download(url, tmp.string())) return false;
    std::error_code ec;
    if (!fs::exists(tmp, ec) || fs::file_size(tmp, ec) < 100000) {
        fs::remove(tmp, ec);
        return false;
    }
    if (inUse) {
        fs::path old = dest; old += ".old";
        fs::remove(old, ec);
        fs::rename(dest, old, ec);          // running exe -> .old (allowed on Win)
    } else {
        fs::remove(dest, ec);
    }
    fs::rename(tmp, dest, ec);
    if (ec) return false;
#if !defined(_WIN32)
    fs::permissions(dest, fs::perms::owner_exec | fs::perms::group_exec |
                    fs::perms::others_exec, fs::perm_options::add, ec);
#endif
    return true;
}

// Worker: check the published version and, if newer, install it. Runs on a
// background thread so the launcher stays responsive while ~35 MB downloads.
int RunUpdateCheck(void*) {
    SetState(Up_Checking);
    SetUpMsg("Checking for updates...");
    std::error_code ec;
    fs::path tmp = fs::temp_directory_path(ec);
    fs::path vf = tmp / "okayspace_launcher_ver.txt";
    if (!Download(std::string(kRawBase) + "VERSION.txt", vf.string())) {
        SetUpMsg("Couldn't reach GitHub (offline?).");
        SetState(Up_Failed);
        return 0;
    }
    std::string latest;
    { std::ifstream f(vf); std::getline(f, latest); }
    while (!latest.empty() && (latest.back() == '\r' || latest.back() == '\n' || latest.back() == ' '))
        latest.pop_back();
    fs::remove(vf, ec);
    g_upLatest = latest;

    std::string local = LocalVersion();
    fs::path dir(g_exeDir);
    std::error_code mec;
    bool haveEngine = fs::exists(dir / "OkaySpaceEngine.exe", mec);
    bool havePlayer = fs::exists(dir / "OkaySpacePlayer.exe", mec);
    bool newer = !latest.empty() && CompareVersions(local, latest) < 0;

    // Nothing to do only if we're current AND both runtimes are present.
    if (!newer && haveEngine && havePlayer) {
        SetUpMsg("Up to date (v" + local + ").");
        SetState(Up_UpToDate);
        return 0;
    }

    // Download what's needed: everything on a version bump, otherwise just the
    // missing runtimes (self-healing a launcher-only download).
    SetState(Up_Downloading);
    bool ok = true;
    if (newer || !haveEngine) {
        SetUpMsg("Downloading engine v" + latest + "...");
        ok = ReplaceFile(std::string(kRawBase) + "OkaySpaceEngine.exe",
                         dir / "OkaySpaceEngine.exe", false) && ok;
    }
    if (newer || !havePlayer) {
        SetUpMsg("Downloading player runtime v" + latest + "...");
        ok = ReplaceFile(std::string(kRawBase) + "OkaySpacePlayer.exe",
                         dir / "OkaySpacePlayer.exe", false) && ok;
    }
    // Only self-update the launcher on a genuine version bump.
    bool launcherOk = false;
    if (newer) {
        SetUpMsg("Updating launcher...");
        launcherOk = !g_selfPath.empty() &&
            ReplaceFile(std::string(kRawBase) + "OkaySpace-Launcher.exe",
                        fs::path(g_selfPath), true);
    }

    if (ok) {
        std::ofstream(dir / "VERSION.txt") << latest << "\n";
        g_upRelaunchNeeded = launcherOk;
        SetUpMsg(launcherOk
            ? "Updated to v" + latest + " — restart the launcher to finish."
            : (newer ? "Updated engine & player to v" + latest + "."
                     : "Downloaded the missing runtime (v" + local + ")."));
        SetState(Up_Updated);
    } else {
        SetUpMsg("Download failed — check your internet connection.");
        SetState(Up_Failed);
    }
    return 0;
}

// Kick off an update check on a background thread (no-op if one is running).
void StartUpdateCheck() {
    UpState s = GetState();
    if (s == Up_Checking || s == Up_Downloading) return;
    if (g_upThread) { SDL_WaitThread(g_upThread, nullptr); g_upThread = nullptr; }
    g_upThread = SDL_CreateThread(RunUpdateCheck, "okay-update", nullptr);
}

// Find the first of `names` that exists next to the launcher.
std::string FindExe(std::initializer_list<const char*> names) {
    std::error_code ec;
    for (const char* n : names) {
        fs::path p = fs::path(g_exeDir) / n;
        if (fs::exists(p, ec)) return p.string();
    }
    return {};
}

// Launch an executable (detached), optionally with one argument.
void Launch(const std::string& exe, const std::string& arg = "") {
    if (exe.empty()) return;
#if defined(_WIN32)
    std::string cmd = "start \"\" \"" + exe + "\"";
    if (!arg.empty()) cmd += " \"" + arg + "\"";
#else
    std::string cmd = "\"" + exe + "\"";
    if (!arg.empty()) cmd += " \"" + arg + "\"";
    cmd += " >/dev/null 2>&1 &";
#endif
    std::system(cmd.c_str());
}

// Scan a few likely folders for built games (*.okayscene).
std::vector<fs::path> FindScenes() {
    std::vector<fs::path> out;
    std::error_code ec;
    auto scan = [&](const fs::path& dir) {
        if (!fs::exists(dir, ec) || !fs::is_directory(dir, ec)) return;
        for (auto it = fs::recursive_directory_iterator(dir, ec);
             it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (it->is_regular_file(ec) && it->path().extension() == ".okayscene")
                out.push_back(it->path());
        }
    };
    fs::path base(g_exeDir);
    scan(base);
    scan(base / "games");
    scan(base / "Projects");
    return out;
}

void DarkTheme() {
    ImGui::StyleColorsDark();
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 0.0f; s.FrameRounding = 5.0f; s.GrabRounding = 5.0f;
    s.FramePadding = ImVec2(10, 7); s.ItemSpacing = ImVec2(10, 10);
    ImVec4* c = s.Colors;
    ImVec4 accent(0.20f, 0.45f, 0.85f, 1.0f);
    c[ImGuiCol_WindowBg]     = ImVec4(0.09f, 0.10f, 0.13f, 1.0f);
    c[ImGuiCol_ChildBg]      = ImVec4(0.12f, 0.13f, 0.17f, 1.0f);
    c[ImGuiCol_Button]       = ImVec4(0.18f, 0.20f, 0.26f, 1.0f);
    c[ImGuiCol_ButtonHovered]= accent;
    c[ImGuiCol_ButtonActive] = ImVec4(0.16f, 0.38f, 0.72f, 1.0f);
    c[ImGuiCol_Header]       = accent;
    c[ImGuiCol_HeaderHovered]= accent;
}

} // namespace

int main(int argc, char** argv) {
    SDL_SetMainReady();
    if (argc > 0) {
        std::error_code ec;
        fs::path self = fs::absolute(argv[0], ec);
        if (!ec) { g_exeDir = self.parent_path().string(); g_selfPath = self.string(); }
    }
    // Clean up a launcher we replaced on a previous run.
    if (!g_selfPath.empty()) {
        std::error_code ec; fs::remove(fs::path(g_selfPath + ".old"), ec);
    }

    if (SDL_Init(SDL_INIT_VIDEO) != 0) return 1;

    // Auto-check for a newer engine version (or a missing runtime) and install
    // it in the background so the launcher window stays responsive meanwhile.
    SDL_AtomicSet(&g_upState, (int)Up_Idle);
    g_upMutex = SDL_CreateMutex();
    StartUpdateCheck();

    SDL_Window* window = SDL_CreateWindow("OkaySpace Launcher",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 960, 600,
        SDL_WINDOW_ALLOW_HIGHDPI);
    if (!window) return 1;
    okay::SetAppIcon(window);   // placeholder OkaySpace logo
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) renderer = SDL_CreateRenderer(window, -1, 0);
    if (!renderer) return 1;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr; // the launcher has a fixed layout
    DarkTheme();
    ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer2_Init(renderer);

    std::string editor = FindExe({"OkaySpaceEngine.exe", "okay-editor.exe", "okay-editor"});
    std::string player = FindExe({"OkaySpacePlayer.exe", "okay-player.exe", "okay-player"});
    std::vector<fs::path> scenes = FindScenes();
    bool rescannedAfterUpdate = false;

    struct Template { const char* name; const char* desc; };
    const Template templates[] = {
        {"Platformer",     "Side-scrolling jump-and-run starter."},
        {"Top-Down",       "Top-down movement and rooms."},
        {"Coin Collector", "A complete pickup-the-coins game."},
        {"Main Menu (UI)", "A title screen with buttons."},
        {"Snake",          "The classic, fully playable."},
    };

    int tab = 0; // 0 Create, 1 Play, 2 Marketplace
    bool running = true;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            ImGui_ImplSDL2_ProcessEvent(&e);
            if (e.type == SDL_QUIT) running = false;
        }
        // Once a background download finishes, re-detect the runtimes so the
        // "not found" notices clear without needing a restart.
        if (GetState() == Up_Updated && !rescannedAfterUpdate) {
            editor = FindExe({"OkaySpaceEngine.exe", "okay-editor.exe", "okay-editor"});
            player = FindExe({"OkaySpacePlayer.exe", "okay-player.exe", "okay-player"});
            scenes = FindScenes();
            rescannedAfterUpdate = true;
        }

        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->WorkPos);
        ImGui::SetNextWindowSize(vp->WorkSize);
        ImGui::Begin("##launcher", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoBringToFrontOnFocus);

        // ---- Left nav ----
        ImGui::BeginChild("nav", ImVec2(190, 0), true);
        ImGui::PushFont(nullptr);
        ImGui::TextColored(ImVec4(0.45f, 0.7f, 1.0f, 1.0f), "OkaySpace");
        ImGui::TextDisabled("game engine");
        ImGui::Dummy(ImVec2(0, 14));
        const char* navs[] = {"  Create", "  Play", "  Marketplace"};
        for (int i = 0; i < 3; ++i)
            if (ImGui::Selectable(navs[i], tab == i, 0, ImVec2(0, 34))) tab = i;
        ImGui::PopFont();

        // ---- Update status (pinned to the bottom of the nav) ----
        UpState st = GetState();
        float footH = 78.0f;
        ImGui::SetCursorPosY(ImGui::GetWindowHeight() - footH);
        ImGui::Separator();
        ImVec4 col = (st == Up_Failed)  ? ImVec4(1.0f, 0.55f, 0.55f, 1)
                   : (st == Up_Updated) ? ImVec4(0.55f, 0.9f, 0.6f, 1)
                   : (st == Up_Downloading || st == Up_Checking)
                                        ? ImVec4(0.85f, 0.8f, 0.4f, 1)
                                        : ImVec4(0.6f, 0.65f, 0.75f, 1);
        const char* head = (st == Up_Downloading) ? "Updating..."
                         : (st == Up_Checking)    ? "Checking..."
                         : (st == Up_Updated)     ? "Updated"
                         : (st == Up_Failed)      ? "Update failed"
                                                  : "Engine";
        ImGui::TextColored(col, "%s", head);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("%s", GetUpMsg().c_str());
        ImGui::PopTextWrapPos();
        bool busy = (st == Up_Checking || st == Up_Downloading);
        ImGui::BeginDisabled(busy);
        if (ImGui::SmallButton("Check for updates")) StartUpdateCheck();
        ImGui::EndDisabled();
        ImGui::EndChild();

        ImGui::SameLine();
        ImGui::BeginChild("content", ImVec2(0, 0), true);

        if (tab == 0) {                                   // ---- Create ----
            ImGui::TextColored(ImVec4(0.85f, 0.9f, 1.0f, 1.0f), "Create a game");
            ImGui::TextWrapped("Open the OkaySpace editor to build 2D or 3D scenes, "
                               "script them, and design UI. Use File > Build Game to "
                               "export a standalone game.");
            ImGui::Dummy(ImVec2(0, 16));
            if (editor.empty()) {
                ImGui::TextColored(ImVec4(1, 0.5f, 0.5f, 1), "Editor not found next to the launcher.");
                ImGui::TextDisabled("Place OkaySpaceEngine.exe beside this launcher.");
            } else {
                if (ImGui::Button("Open Editor", ImVec2(220, 56))) Launch(editor);
                ImGui::TextDisabled("%s", editor.c_str());
            }
        } else if (tab == 1) {                            // ---- Play ----
            ImGui::TextColored(ImVec4(0.85f, 0.9f, 1.0f, 1.0f), "Play a game");
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - 80);
            if (ImGui::Button("Refresh", ImVec2(80, 0))) scenes = FindScenes();
            ImGui::Separator();
            if (player.empty())
                ImGui::TextColored(ImVec4(1, 0.5f, 0.5f, 1), "Player runtime not found next to the launcher.");
            else if (scenes.empty())
                ImGui::TextDisabled("No games found. Build one from the editor (File > Build Game), "
                                    "then put it next to the launcher or in a 'games' folder.");
            else
                for (std::size_t i = 0; i < scenes.size(); ++i) {
                    ImGui::PushID((int)i);
                    if (ImGui::Button("Play", ImVec2(70, 0))) Launch(player, scenes[i].string());
                    ImGui::SameLine();
                    ImGui::TextUnformatted(scenes[i].filename().string().c_str());
                    ImGui::SameLine();
                    ImGui::TextDisabled("%s", scenes[i].parent_path().string().c_str());
                    ImGui::PopID();
                }
        } else {                                          // ---- Marketplace ----
            ImGui::TextColored(ImVec4(0.85f, 0.9f, 1.0f, 1.0f), "Marketplace");
            ImGui::TextDisabled("Starter templates — open one in the editor (New Project) to begin.");
            ImGui::Separator();
            for (const auto& t : templates) {
                ImGui::BeginChild(t.name, ImVec2(0, 64), true);
                ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.95f, 1.0f), "%s", t.name);
                ImGui::TextDisabled("%s", t.desc);
                ImGui::SameLine(ImGui::GetContentRegionAvail().x - 90);
                ImGui::PushID(t.name);
                if (ImGui::Button("Open", ImVec2(86, 0))) Launch(editor);
                ImGui::PopID();
                ImGui::EndChild();
            }
            ImGui::Dummy(ImVec2(0, 8));
            ImGui::TextDisabled("Community content marketplace coming soon.");
        }

        ImGui::EndChild();
        ImGui::End();

        ImGui::Render();
        SDL_SetRenderDrawColor(renderer, 18, 20, 26, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
        SDL_RenderPresent(renderer);
    }

    if (g_upThread) { SDL_WaitThread(g_upThread, nullptr); g_upThread = nullptr; }
    if (g_upMutex) { SDL_DestroyMutex(g_upMutex); g_upMutex = nullptr; }
    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
