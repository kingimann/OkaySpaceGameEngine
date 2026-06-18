// OkaySpace Editor — a Unity-style desktop editor (Dear ImGui + SDL2 + OpenGL).
//
// Panels: Hierarchy, Inspector, Scene viewport (mouse select + drag, pan/zoom),
// and a Play/Stop/Step toolbar. File menu saves/loads scenes via the engine's
// SceneSerializer.
//
// Run with --selftest to exercise the (headless) editor logic without opening a
// window — used in CI where there's no display.
#define IMGUI_DEFINE_MATH_OPERATORS
#include "EditorState.hpp"

#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <string>
#include <unordered_map>

// ----------------------------------------------------------------------------
// Headless self-test: verifies the editor's model logic with no GUI.
// ----------------------------------------------------------------------------
static int RunSelfTest() {
    using namespace okay;
    using namespace okay::editor;
    int failures = 0;
    auto check = [&](bool cond, const char* what) {
        if (!cond) { ++failures; std::cerr << "  FAIL: " << what << "\n"; }
    };

    EditorState ed;
    GameObject* sprite = ed.CreateSprite("Hero");
    check(sprite != nullptr, "create sprite");
    check(ed.selected() == sprite, "auto-select new object");
    sprite->transform->localPosition = {5, 6, 0};

    // Save / load round-trip.
    std::string path = "/tmp/okay_editor_selftest.okayscene";
    check(ed.Save(path), "save scene");

    EditorState ed2;
    std::string err;
    check(ed2.Load(path, &err), "load scene");
    GameObject* loaded = ed2.scene().Find("Hero");
    check(loaded != nullptr, "loaded object present");
    if (loaded) check(loaded->transform->localPosition == Vec3(5, 6, 0), "position preserved");

    // Play / stop restores edit state (move during play is reverted).
    ed.Play();
    check(ed.isPlaying(), "is playing");
    ed.Tick(0.1f);
    ed.Stop();
    check(!ed.isPlaying(), "stopped");
    GameObject* afterStop = ed.scene().Find("Hero");
    check(afterStop != nullptr, "object restored after stop");

    // Delete.
    ed.Select(ed.scene().Find("Hero"));
    ed.DeleteSelected();
    check(ed.scene().Find("Hero") == nullptr, "delete selected");

    std::cout << (failures == 0 ? "editor selftest: OK\n" : "editor selftest: FAILED\n");
    return failures == 0 ? 0 : 1;
}

#if defined(OKAY_EDITOR_HEADLESS)
int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i)
        if (std::string(argv[i]) == "--selftest") return RunSelfTest();
    std::cerr << "This editor build is headless (no SDL). Use --selftest.\n";
    return 0;
}
#else

// Take control of the program entry point ourselves so SDL doesn't #define
// `main` to SDL_main (which would also rewrite identifiers like `cam->main`).
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include "imgui.h"
#include "imgui_internal.h" // DockBuilder for the default layout
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_sdlrenderer2.h"

#include <vector>

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <windows.h>
#else
#  include <unistd.h>
#endif

#ifndef OKAY_ENGINE_VERSION
#  define OKAY_ENGINE_VERSION "dev"
#endif

using namespace okay;
using namespace okay::editor;

namespace {

ImU32 ToColor(const Color& c) {
    return IM_COL32((int)(c.r * 255), (int)(c.g * 255), (int)(c.b * 255), (int)(c.a * 255));
}

// ---- Self-updater: pull the latest engine from GitHub -----------------
namespace updater {
namespace fs = std::filesystem;

const char* kRawBase =
    "https://raw.githubusercontent.com/kingimann/OkaySpaceGameEngine/main/dist/";

// Absolute path of the running executable.
std::string SelfPath() {
#if defined(_WIN32)
    char buf[MAX_PATH];
    DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
    return std::string(buf, n);
#else
    char buf[4096];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n > 0) { buf[n] = '\0'; return buf; }
    return {};
#endif
}

// Download a URL to a file using whatever HTTP client is on the system.
bool Download(const std::string& url, const std::string& outPath) {
#if defined(_WIN32)
    std::string c1 = "curl -L -s -o \"" + outPath + "\" \"" + url + "\"";
    if (std::system(c1.c_str()) == 0 && fs::exists(outPath)) return true;
    std::string c2 = "powershell -NoProfile -Command \"try { Invoke-WebRequest "
                     "-UseBasicParsing -Uri '" + url + "' -OutFile '" + outPath +
                     "' } catch { exit 1 }\"";
    return std::system(c2.c_str()) == 0 && fs::exists(outPath);
#else
    std::string c1 = "curl -L -s -o \"" + outPath + "\" \"" + url + "\"";
    if (std::system((c1 + " 2>/dev/null").c_str()) == 0 && fs::exists(outPath)) return true;
    std::string c2 = "wget -q -O \"" + outPath + "\" \"" + url + "\"";
    return std::system((c2 + " 2>/dev/null").c_str()) == 0 && fs::exists(outPath);
#endif
}

// Check GitHub for a newer engine build and, if found, download and swap it in.
// No git required — works for a standalone .exe.
std::string CheckAndUpdate() {
    std::error_code ec;
    fs::path tmpDir = fs::temp_directory_path(ec);

    // 1) Compare the embedded version against the published VERSION.txt.
    fs::path verFile = tmpDir / "okayspace_version.txt";
    std::string latest;
    if (Download(std::string(kRawBase) + "VERSION.txt", verFile.string())) {
        std::ifstream vf(verFile);
        std::getline(vf, latest);
        while (!latest.empty() && (latest.back() == '\r' || latest.back() == ' '))
            latest.pop_back();
        fs::remove(verFile, ec);
    } else {
        return "Couldn't reach GitHub (no internet, or curl/PowerShell missing).";
    }

    std::string current = OKAY_ENGINE_VERSION;
    if (!latest.empty() && latest == current)
        return "You're up to date (v" + current + ").";

    // 2) Download the new executable next to the current one.
    fs::path self = SelfPath();
    if (self.empty()) return "Update available (v" + latest +
                             "), but couldn't locate the running .exe.";
#if defined(_WIN32)
    std::string assetName = "OkaySpaceEngine.exe";
#else
    std::string assetName = "OkaySpaceEngine.exe"; // only a Windows binary is published
#endif
    fs::path newFile = self;
    newFile += ".new";
    if (!Download(std::string(kRawBase) + assetName, newFile.string()))
        return "Update v" + latest + " found, but the download failed.";
    if (fs::file_size(newFile, ec) < 100000) {
        fs::remove(newFile, ec);
        return "Downloaded file looked invalid; update aborted.";
    }

    // 3) Swap: move the running exe aside, move the new one into place.
    fs::path backup = self;
    backup += ".old";
    fs::remove(backup, ec);
    fs::rename(self, backup, ec);
    if (ec) { fs::remove(newFile, ec); return "Couldn't replace the app: " + ec.message(); }
    fs::rename(newFile, self, ec);
    if (ec) { fs::rename(backup, self, ec); return "Couldn't install the update: " + ec.message(); }

    return "Updated to v" + latest + "!\nClose and reopen the app to run it.";
}
} // namespace updater

std::string g_updateStatus;
bool g_openUpdatePopup = false;
bool g_showNewProject = true; // show the project chooser on launch

// Per-object editor-only Euler-angle cache (degrees) for the inspector.
std::unordered_map<GameObject*, Vec3> g_euler;

// ---- Console log -------------------------------------------------------
std::vector<std::string> g_console;
void ConsoleLog(const std::string& msg) {
    g_console.push_back(msg);
    if (g_console.size() > 300) g_console.erase(g_console.begin());
}

// ---- A dark, Unity-ish theme ------------------------------------------
void ApplyTheme() {
    ImGui::StyleColorsDark();
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 4.0f; s.FrameRounding = 3.0f; s.GrabRounding = 3.0f;
    s.TabRounding = 3.0f;   s.ScrollbarRounding = 3.0f;
    s.WindowPadding = ImVec2(8, 8); s.FramePadding = ImVec2(6, 4);
    s.ItemSpacing = ImVec2(8, 6);
    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg]      = ImVec4(0.16f, 0.16f, 0.18f, 1.0f);
    c[ImGuiCol_Header]        = ImVec4(0.20f, 0.42f, 0.74f, 0.55f);
    c[ImGuiCol_HeaderHovered] = ImVec4(0.26f, 0.52f, 0.88f, 0.80f);
    c[ImGuiCol_HeaderActive]  = ImVec4(0.20f, 0.42f, 0.74f, 1.00f);
    c[ImGuiCol_Button]        = ImVec4(0.24f, 0.24f, 0.28f, 1.00f);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.30f, 0.50f, 0.85f, 1.00f);
    c[ImGuiCol_ButtonActive]  = ImVec4(0.22f, 0.40f, 0.72f, 1.00f);
    c[ImGuiCol_TitleBgActive] = ImVec4(0.13f, 0.26f, 0.42f, 1.00f);
    c[ImGuiCol_Tab]           = ImVec4(0.18f, 0.22f, 0.30f, 1.00f);
    c[ImGuiCol_TabActive]     = ImVec4(0.22f, 0.42f, 0.70f, 1.00f);
    c[ImGuiCol_TabHovered]    = ImVec4(0.28f, 0.52f, 0.85f, 1.00f);
}

// Build the default Unity-style dock layout once.
void BuildDefaultLayout(ImGuiID dockId, ImVec2 size) {
    ImGui::DockBuilderRemoveNode(dockId);
    ImGui::DockBuilderAddNode(dockId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockId, size);

    ImGuiID center = dockId, left, right, down;
    left  = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left,  0.18f, nullptr, &center);
    right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.24f, nullptr, &center);
    down  = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down,  0.26f, nullptr, &center);

    ImGui::DockBuilderDockWindow("Hierarchy", left);
    ImGui::DockBuilderDockWindow("Inspector", right);
    ImGui::DockBuilderDockWindow("Console", down);
    ImGui::DockBuilderDockWindow("Project", down);
    ImGui::DockBuilderDockWindow("Services", down);
    ImGui::DockBuilderDockWindow("Scene", center);
    ImGui::DockBuilderFinish(dockId);
}

void DrawMenuAndToolbar(EditorState& ed, bool& running) {
    if (!ImGui::BeginMenuBar()) return;
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("New Project...", "Ctrl+N")) g_showNewProject = true;
        if (ImGui::MenuItem("New 2D Scene")) { ed.NewScene2D(); ConsoleLog("New 2D project"); }
        if (ImGui::MenuItem("New 3D Scene")) { ed.NewScene3D(); ConsoleLog("New 3D project"); }
        ImGui::Separator();
        if (ImGui::MenuItem("Open...", "Ctrl+O")) {
            std::string err;
            if (ed.Load("scene.okayscene", &err)) ConsoleLog("Opened scene.okayscene");
            else ConsoleLog("Open failed: " + err);
        }
        if (ImGui::MenuItem("Save", "Ctrl+S")) {
            std::string p = ed.path().empty() ? "scene.okayscene" : ed.path();
            if (ed.Save(p)) { ConsoleLog("Saved " + p); ed.Achievement("FIRST_SAVE"); }
            else ConsoleLog("Save failed");
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Exit")) running = false;
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("GameObject")) {
        bool created = false;
        if (ImGui::MenuItem("Create Empty"))   { ed.CreateEmpty();   ConsoleLog("Created empty GameObject"); created = true; }
        if (ImGui::MenuItem("Create Sprite"))  { ed.CreateSprite();  ConsoleLog("Created Sprite"); created = true; }
        if (ImGui::MenuItem("Create Camera"))  { ed.CreateCamera();  ConsoleLog("Created Camera"); created = true; }
        ImGui::Separator();
        if (ImGui::MenuItem("Create Cube (3D)"))    { ed.CreateCube();    ConsoleLog("Created Cube"); created = true; }
        if (ImGui::MenuItem("Create Pyramid (3D)")) { ed.CreatePyramid(); ConsoleLog("Created Pyramid"); created = true; }
        ImGui::Separator();
        if (ImGui::MenuItem("Create Particle System")) {
            GameObject* go = ed.CreateEmpty("Particles");
            go->AddComponent<ParticleSystem>();
            ConsoleLog("Created Particle System (press Play to see it)");
            created = true;
        }
        if (ImGui::MenuItem("Create Tilemap")) {
            GameObject* go = ed.CreateEmpty("Tilemap");
            auto* tm = go->AddComponent<Tilemap>();
            tm->Resize(12, 8);
            for (int x = 0; x < 12; ++x) tm->SetTile(x, 0, 1); // a ground row
            ConsoleLog("Created Tilemap");
            created = true;
        }
        if (created) ed.Achievement("FIRST_OBJECT");
        ImGui::Separator();
        if (ImGui::MenuItem("Duplicate Selected", "Ctrl+D", false, ed.selected() != nullptr)) {
            ed.DuplicateSelected(); ConsoleLog("Duplicated selection");
        }
        if (ImGui::MenuItem("Delete Selected", "Del", false, ed.selected() != nullptr)) {
            ed.DeleteSelected(); ConsoleLog("Deleted selection");
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Engine")) {
        if (ImGui::MenuItem("Check for Updates")) {
            g_updateStatus = updater::CheckAndUpdate();
            g_openUpdatePopup = true;
            ConsoleLog("Update check: " + g_updateStatus);
        }
        ImGui::EndMenu();
    }

    // Centered Play / Stop / Step controls (Unity-style toolbar), color-coded.
    float btnW = 64.0f;
    ImGui::SameLine(ImGui::GetWindowWidth() * 0.5f - btnW);
    if (!ed.isPlaying()) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.55f, 0.25f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.24f, 0.70f, 0.32f, 1.0f));
        if (ImGui::Button(">  Play", ImVec2(btnW, 0))) { ed.Play(); ConsoleLog("Play"); ed.Achievement("HIT_PLAY"); }
        ImGui::PopStyleColor(2);
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.65f, 0.22f, 0.22f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.80f, 0.28f, 0.28f, 1.0f));
        if (ImGui::Button("[]  Stop", ImVec2(btnW, 0))) { ed.Stop(); ConsoleLog("Stop"); }
        ImGui::PopStyleColor(2);
    }
    ImGui::SameLine();
    if (ImGui::Button("Step", ImVec2(50, 0))) ed.Tick(1.0f / 60.0f);

    // Right-aligned status.
    char status[96];
    std::snprintf(status, sizeof(status), "v%s   %s   %.0f FPS", OKAY_ENGINE_VERSION,
                  ed.isPlaying() ? "PLAYING" : "EDIT", ImGui::GetIO().Framerate);
    ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::CalcTextSize(status).x - 16);
    ImGui::TextColored(ed.isPlaying() ? ImVec4(0.4f, 0.9f, 0.4f, 1) : ImVec4(0.7f, 0.7f, 0.7f, 1),
                       "%s", status);
    ImGui::EndMenuBar();
}

// Full-window host that hosts the dockspace + menu/toolbar.
void DrawDockSpace(EditorState& ed, bool& running) {
    static bool first = true;
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::SetNextWindowViewport(vp->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
        ImGuiWindowFlags_MenuBar;
    ImGui::Begin("##OkayDockHost", nullptr, flags);
    ImGui::PopStyleVar(3);

    ImGuiID dockId = ImGui::GetID("OkayDockSpace");
    if (first) { first = false; BuildDefaultLayout(dockId, vp->WorkSize); }
    ImGui::DockSpace(dockId, ImVec2(0, 0), ImGuiDockNodeFlags_None);

    DrawMenuAndToolbar(ed, running);
    ImGui::End();
}

void DrawConsole() {
    if (ImGui::Begin("Console")) {
        if (ImGui::Button("Clear")) g_console.clear();
        ImGui::SameLine(); ImGui::TextDisabled("%zu messages", g_console.size());
        ImGui::Separator();
        ImGui::BeginChild("log");
        for (const auto& line : g_console) ImGui::TextUnformatted(line.c_str());
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) ImGui::SetScrollHereY(1.0f);
        ImGui::EndChild();
    }
    ImGui::End();
}

void DrawProject(EditorState& ed) {
    if (ImGui::Begin("Project")) {
        ImGui::TextDisabled("Assets");
        ImGui::Separator();
        ImGui::BulletText("Scene: %s", ed.path().empty() ? "(unsaved)" : ed.path().c_str());
        ImGui::BulletText("%d GameObjects", (int)ed.scene().Objects().size());
        ImGui::TextWrapped("Scenes are saved as .okayscene text files via File > Save.");
    }
    ImGui::End();
}

// The online services that ship inside the engine: Steam, PlayFab, Multiplayer.
void DrawServices(EditorState& ed) {
    if (!ImGui::Begin("Services")) { ImGui::End(); return; }

    // ---- Steam ----
    if (ImGui::CollapsingHeader("Steam", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (auto* s = ed.steam()) {
            ImGui::Text("Backend: %s%s", s->BackendName(),
                        s->IsAvailable() ? " (live)" : " (simulation)");
            ImGui::Text("User: %s", s->UserName().c_str());
            static char ach[64] = "MY_ACHIEVEMENT";
            ImGui::SetNextItemWidth(180);
            ImGui::InputText("##ach", ach, sizeof(ach));
            ImGui::SameLine();
            if (ImGui::Button("Unlock")) s->UnlockAchievement(ach);
            ImGui::SameLine();
            if (ImGui::Button("Clear")) s->ClearAchievement(ach);
            const char* known[] = {"FIRST_OBJECT", "FIRST_SAVE", "HIT_PLAY"};
            for (const char* a : known)
                ImGui::BulletText("%s: %s", a,
                    s->IsAchievementUnlocked(a) ? "unlocked" : "locked");
        } else ImGui::TextDisabled("unavailable");
    }

    // ---- PlayFab ----
    if (ImGui::CollapsingHeader("PlayFab", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (auto* p = ed.playfab()) {
            ImGui::Text("Backend: %s%s", p->BackendName(),
                        p->IsRealBackend() ? " (live)" : " (simulation)");
            static char customId[64] = "editor-user";
            ImGui::SetNextItemWidth(180);
            ImGui::InputText("Custom Id", customId, sizeof(customId));
            ImGui::SameLine();
            if (ImGui::Button("Login")) p->LoginWithCustomId(customId);
            if (p->IsLoggedIn()) ImGui::Text("Logged in as %s", p->PlayFabId().c_str());
            else ImGui::TextDisabled("not logged in");

            static int score = 100;
            ImGui::SetNextItemWidth(120);
            ImGui::InputInt("high_score", &score);
            ImGui::SameLine();
            if (ImGui::Button("Submit") && p->IsLoggedIn())
                p->UpdateStatistic("high_score", score);
            if (p->IsLoggedIn())
                ImGui::Text("stored high_score: %d", p->GetStatistic("high_score"));
        } else ImGui::TextDisabled("unavailable");
    }

    // ---- Multiplayer ----
    if (ImGui::CollapsingHeader("Multiplayer", ImGuiTreeNodeFlags_DefaultOpen)) {
        static int port = 45000;
        static char host[64] = "127.0.0.1";
        ImGui::SetNextItemWidth(120);
        ImGui::InputInt("Port", &port);
        if (ImGui::Button("Host")) ed.StartHost((std::uint16_t)port);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(140);
        ImGui::InputText("##host", host, sizeof(host));
        ImGui::SameLine();
        if (ImGui::Button("Join")) ed.StartJoin(host, (std::uint16_t)port);
        ImGui::SameLine();
        if (ImGui::Button("Disconnect")) ed.StopNetwork();
        if (auto* n = ed.net()) {
            const char* mode = n->IsServer() ? "Server" : n->IsClient() ? "Client" : "Offline";
            ImGui::Text("Mode: %s   Peers: %d   LocalId: %u",
                        mode, (int)n->PeerCount(), n->LocalId());
        } else ImGui::TextDisabled("not connected");
    }

    ImGui::End();
}

// Global keyboard shortcuts (ignored while typing in a field).
void HandleShortcuts(EditorState& ed) {
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput) return;
    bool ctrl = io.KeyCtrl;
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_N, false)) g_showNewProject = true;
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) {
        std::string p = ed.path().empty() ? "scene.okayscene" : ed.path();
        if (ed.Save(p)) { ConsoleLog("Saved " + p); ed.Achievement("FIRST_SAVE"); }
    }
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_D, false) && ed.selected()) {
        ed.DuplicateSelected(); ConsoleLog("Duplicated selection");
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) && ed.selected()) {
        ed.DeleteSelected(); ConsoleLog("Deleted selection");
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Space, false)) {
        if (ed.isPlaying()) { ed.Stop(); ConsoleLog("Stop"); }
        else { ed.Play(); ConsoleLog("Play"); ed.Achievement("HIT_PLAY"); }
    }
    if (ImGui::IsKeyPressed(ImGuiKey_F, false) && ed.selected()) {
        Vec3 pp = ed.selected()->transform->Position();
        ed.camTarget = pp; ed.cameraPos = {pp.x, pp.y};
    }
}

void DrawNewProjectPopup(EditorState& ed) {
    if (g_showNewProject) { ImGui::OpenPopup("New Project"); g_showNewProject = false; }
    ImVec2 c = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(c, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("New Project", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Create a new project");
        ImGui::TextDisabled("Pick a starting scene type.");
        ImGui::Separator();
        if (ImGui::Button("2D Scene", ImVec2(160, 70))) {
            ed.NewScene2D(); ConsoleLog("New 2D project"); ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("3D Scene", ImVec2(160, 70))) {
            ed.NewScene3D(); ConsoleLog("New 3D project"); ImGui::CloseCurrentPopup();
        }
        ImGui::Spacing();
        if (ImGui::Button("Empty Scene", ImVec2(-1, 0))) {
            ed.NewScene(); ConsoleLog("New empty scene"); ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void DrawUpdatePopup() {
    if (g_openUpdatePopup) { ImGui::OpenPopup("Engine Update"); g_openUpdatePopup = false; }
    if (ImGui::BeginPopupModal("Engine Update", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted(g_updateStatus.c_str());
        ImGui::Separator();
        if (ImGui::Button("OK", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void DrawHierarchy(EditorState& ed) {
    ImGui::Begin("Hierarchy");
    ImGui::TextDisabled("Scene: %s%s", ed.scene().Name().c_str(), ed.dirty ? " *" : "");
    ImGui::Separator();
    const auto& objs = ed.scene().Objects();
    for (const auto& up : objs) {
        GameObject* go = up.get();
        // Show only roots; children are listed under their parent.
        if (go->transform->Parent() != nullptr) continue;
        std::function<void(GameObject*)> drawNode = [&](GameObject* node) {
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
                                       ImGuiTreeNodeFlags_DefaultOpen |
                                       ImGuiTreeNodeFlags_SpanAvailWidth;
            if (node == ed.selected()) flags |= ImGuiTreeNodeFlags_Selected;
            if (node->transform->ChildCount() == 0) flags |= ImGuiTreeNodeFlags_Leaf;
            bool open = ImGui::TreeNodeEx(node, flags, "%s%s", node->name.c_str(),
                                          node->active ? "" : "  (off)");
            if (ImGui::IsItemClicked()) ed.Select(node);
            // Right-click context menu per item.
            if (ImGui::BeginPopupContextItem()) {
                ed.Select(node);
                if (ImGui::MenuItem("Duplicate")) { ed.DuplicateSelected(); ConsoleLog("Duplicated"); }
                if (ImGui::MenuItem("Delete"))    { ed.DeleteSelected(); ConsoleLog("Deleted"); }
                ImGui::Separator();
                if (ImGui::MenuItem("Add Child")) {
                    GameObject* child = ed.scene().CreateGameObject("Child");
                    child->transform->SetParent(node->transform, false);
                    ed.Select(child);
                }
                ImGui::EndPopup();
            }
            if (open) {
                for (Transform* child : node->transform->Children())
                    drawNode(child->gameObject);
                ImGui::TreePop();
            }
        };
        drawNode(go);
    }
    // Right-click empty space to create objects.
    if (ImGui::BeginPopupContextWindow("HierarchyCtx",
            ImGuiPopupFlags_NoOpenOverItems | ImGuiPopupFlags_MouseButtonRight)) {
        if (ImGui::MenuItem("Create Empty"))  ed.CreateEmpty();
        if (ImGui::MenuItem("Create Sprite")) ed.CreateSprite();
        if (ImGui::MenuItem("Create Cube"))   ed.CreateCube();
        ImGui::EndPopup();
    }
    ImGui::End();
}

void DrawInspector(EditorState& ed) {
    ImGui::Begin("Inspector");
    GameObject* go = ed.selected();
    if (!go) {
        ImGui::Dummy(ImVec2(0, 8));
        ImGui::TextDisabled("  Select an object in the Hierarchy");
        ImGui::TextDisabled("  or Scene to edit it here.");
        ImGui::End();
        return;
    }

    // Header: active + name.
    ImGui::Checkbox("##active", &go->active);
    ImGui::SameLine();
    char nameBuf[128];
    std::strncpy(nameBuf, go->name.c_str(), sizeof(nameBuf) - 1);
    nameBuf[sizeof(nameBuf) - 1] = '\0';
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputText("##name", nameBuf, sizeof(nameBuf))) { go->name = nameBuf; ed.dirty = true; }
    if (!go->tag.empty()) ImGui::TextDisabled("Tag: %s", go->tag.c_str());
    ImGui::Spacing();

    if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
        Transform* t = go->transform;
        float pos[3] = {t->localPosition.x, t->localPosition.y, t->localPosition.z};
        if (ImGui::DragFloat3("Position", pos, 0.05f)) {
            t->localPosition = {pos[0], pos[1], pos[2]}; ed.dirty = true;
        }
        Vec3& e = g_euler[go];
        float rot[3] = {e.x, e.y, e.z};
        if (ImGui::DragFloat3("Rotation", rot, 1.0f)) {
            e = {rot[0], rot[1], rot[2]};
            t->localRotation = Quat::Euler(e); ed.dirty = true;
        }
        float scl[3] = {t->localScale.x, t->localScale.y, t->localScale.z};
        if (ImGui::DragFloat3("Scale", scl, 0.05f)) {
            t->localScale = {scl[0], scl[1], scl[2]}; ed.dirty = true;
        }
    }

    if (auto* sr = go->GetComponent<SpriteRenderer>()) {
        if (ImGui::CollapsingHeader("Sprite Renderer", ImGuiTreeNodeFlags_DefaultOpen)) {
            float col[4] = {sr->color.r, sr->color.g, sr->color.b, sr->color.a};
            if (ImGui::ColorEdit4("Color##sprite", col)) {
                sr->color = {col[0], col[1], col[2], col[3]}; ed.dirty = true;
            }
            float size[2] = {sr->size.x, sr->size.y};
            if (ImGui::DragFloat2("Size", size, 0.05f, 0.0f, 1000.0f)) {
                sr->size = {size[0], size[1]}; ed.dirty = true;
            }
        }
    }
    if (auto* mr = go->GetComponent<MeshRenderer>()) {
        if (ImGui::CollapsingHeader("Mesh Renderer", ImGuiTreeNodeFlags_DefaultOpen)) {
            float col[4] = {mr->color.r, mr->color.g, mr->color.b, mr->color.a};
            if (ImGui::ColorEdit4("Color##mesh", col)) {
                mr->color = {col[0], col[1], col[2], col[3]}; ed.dirty = true;
            }
            ImGui::Checkbox("Wireframe", &mr->wireframe);
            const char* shapes[] = {"Cube", "Pyramid", "Quad"};
            static int shapeIdx = 0;
            if (ImGui::Combo("Mesh", &shapeIdx, shapes, 3)) {
                mr->mesh = shapeIdx == 0 ? Mesh::Cube()
                         : shapeIdx == 1 ? Mesh::Pyramid() : Mesh::Quad();
                ed.dirty = true;
            }
            ImGui::TextDisabled("%d triangles", mr->mesh.TriangleCount());
        }
    }
    if (auto* cam = go->GetComponent<Camera>()) {
        if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
            int proj = (int)cam->projection;
            const char* projs[] = {"Orthographic", "Perspective"};
            if (ImGui::Combo("Projection", &proj, projs, 2))
                cam->projection = (Camera::Projection)proj;
            if (cam->projection == Camera::Projection::Orthographic)
                ImGui::DragFloat("Size", &cam->orthographicSize, 0.1f, 0.1f, 1000.0f);
            else
                ImGui::DragFloat("FOV", &cam->fieldOfView, 0.5f, 10.0f, 170.0f);
            ImGui::Checkbox("Main", &cam->main);
        }
    }
    if (go->GetComponent<Rigidbody2D>())
        if (ImGui::CollapsingHeader("Rigidbody2D", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto* rb = go->GetComponent<Rigidbody2D>();
            int bt = (int)rb->bodyType;
            const char* types[] = {"Dynamic", "Kinematic", "Static"};
            if (ImGui::Combo("Body Type", &bt, types, 3)) rb->bodyType = (Rigidbody2D::BodyType)bt;
            ImGui::DragFloat("Gravity Scale", &rb->gravityScale, 0.05f);
            ImGui::DragFloat("Mass", &rb->mass, 0.05f, 0.01f, 1000.0f);
            ImGui::DragFloat("Bounciness", &rb->bounciness, 0.01f, 0.0f, 1.0f);
        }

    ImGui::Spacing();
    ImGui::Separator();
    if (ImGui::Button("Add Component", ImVec2(-1, 0))) ImGui::OpenPopup("AddComponent");
    if (ImGui::BeginPopup("AddComponent")) {
        if (!go->GetComponent<SpriteRenderer>() && ImGui::Selectable("Sprite Renderer"))
            { go->AddComponent<SpriteRenderer>(); ed.dirty = true; }
        if (!go->GetComponent<MeshRenderer>() && ImGui::Selectable("Mesh Renderer (Cube)"))
            { go->AddComponent<MeshRenderer>(); ed.view3D = true; ed.dirty = true; }
        if (!go->GetComponent<Camera>() && ImGui::Selectable("Camera"))
            { go->AddComponent<Camera>(); ed.dirty = true; }
        if (!go->GetComponent<Rigidbody2D>() && ImGui::Selectable("Rigidbody2D"))
            { go->AddComponent<Rigidbody2D>(); ed.dirty = true; }
        if (!go->GetComponent<BoxCollider2D>() && ImGui::Selectable("Box Collider 2D"))
            { go->AddComponent<BoxCollider2D>(); ed.dirty = true; }
        ImGui::EndPopup();
    }
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.18f, 0.18f, 1.0f));
    if (ImGui::Button("Delete GameObject", ImVec2(-1, 0))) ed.DeleteSelected();
    ImGui::PopStyleColor();
    ImGui::End();
}

void DrawScene2D(EditorState& ed, ImDrawList* dl, ImVec2 canvasPos, ImVec2 canvasSize,
                 ImVec2 canvasEnd, bool hovered, ImGuiIO& io) {
    ImVec2 center(canvasPos.x + canvasSize.x * 0.5f, canvasPos.y + canvasSize.y * 0.5f);
    float scale = canvasSize.y / ed.cameraZoom;
    auto worldToScreen = [&](const Vec3& w) {
        return ImVec2(center.x + (w.x - ed.cameraPos.x) * scale,
                      center.y - (w.y - ed.cameraPos.y) * scale);
    };
    auto screenToWorld = [&](const ImVec2& s) {
        return Vec2((s.x - center.x) / scale + ed.cameraPos.x,
                    -(s.y - center.y) / scale + ed.cameraPos.y);
    };

    if (hovered && io.MouseWheel != 0.0f)
        ed.cameraZoom = Mathf::Clamp(ed.cameraZoom * (1.0f - io.MouseWheel * 0.1f), 2.0f, 200.0f);
    if (hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
        ed.cameraPos.x -= io.MouseDelta.x / scale;
        ed.cameraPos.y += io.MouseDelta.y / scale;
    }

    dl->PushClipRect(canvasPos, canvasEnd, true);

    // Faint unit grid across the visible area.
    {
        float halfH = ed.cameraZoom * 0.5f;
        float halfW = halfH * (canvasSize.x / canvasSize.y);
        int x0 = (int)Mathf::Floor(ed.cameraPos.x - halfW);
        int x1 = (int)Mathf::Ceil(ed.cameraPos.x + halfW);
        int y0 = (int)Mathf::Floor(ed.cameraPos.y - halfH);
        int y1 = (int)Mathf::Ceil(ed.cameraPos.y + halfH);
        ImU32 grid = IM_COL32(40, 40, 52, 255);
        if (x1 - x0 < 200) // avoid drawing thousands of lines when zoomed way out
            for (int x = x0; x <= x1; ++x)
                dl->AddLine(worldToScreen({(float)x, (float)y0, 0}),
                            worldToScreen({(float)x, (float)y1, 0}), grid);
        if (y1 - y0 < 200)
            for (int y = y0; y <= y1; ++y)
                dl->AddLine(worldToScreen({(float)x0, (float)y, 0}),
                            worldToScreen({(float)x1, (float)y, 0}), grid);
    }
    dl->AddLine(worldToScreen({-1000, 0, 0}), worldToScreen({1000, 0, 0}), IM_COL32(120, 60, 60, 255));
    dl->AddLine(worldToScreen({0, -1000, 0}), worldToScreen({0, 1000, 0}), IM_COL32(60, 120, 60, 255));

    const auto& objs = ed.scene().Objects();
    for (const auto& up : objs) {
        GameObject* go = up.get();
        auto* sr = go->GetComponent<SpriteRenderer>();
        if (!sr || !go->active) continue;
        Vec3 wp = go->transform->Position();
        Vec3 ls = go->transform->LossyScale();
        float hx = sr->size.x * ls.x * 0.5f * scale;
        float hy = sr->size.y * ls.y * 0.5f * scale;
        ImVec2 c = worldToScreen(wp);
        ImVec2 a(c.x - hx, c.y - hy), b(c.x + hx, c.y + hy);
        dl->AddRectFilled(a, b, ToColor(sr->color));
        if (go == ed.selected())
            dl->AddRect(ImVec2(a.x - 2, a.y - 2), ImVec2(b.x + 2, b.y + 2),
                        IM_COL32(255, 200, 0, 255), 0, 0, 2.0f);
    }

    // Tilemaps: filled cells, color keyed by tile id.
    for (const auto& up : objs) {
        auto* tm = up->GetComponent<Tilemap>();
        if (!tm || !up->active) continue;
        float half = tm->tileSize * 0.5f * scale;
        for (int y = 0; y < tm->Height(); ++y)
            for (int x = 0; x < tm->Width(); ++x) {
                int id = tm->GetTile(x, y);
                if (id == 0) continue;
                ImVec2 cc = worldToScreen(tm->CellToWorld(x, y));
                ImU32 col = IM_COL32(60 + (id * 70) % 196, 60 + (id * 130) % 196,
                                     80 + (id * 50) % 176, 255);
                dl->AddRectFilled(ImVec2(cc.x - half, cc.y - half),
                                  ImVec2(cc.x + half, cc.y + half), col);
            }
    }

    // Particles: filled circles (move during Play).
    for (const auto& up : objs) {
        auto* ps = up->GetComponent<ParticleSystem>();
        if (!ps || !up->active) continue;
        for (const auto& p : ps->Particles()) {
            if (!p.alive) continue;
            ImVec2 sp = worldToScreen(p.position);
            dl->AddCircleFilled(sp, Mathf::Max(1.0f, p.size * scale * 0.5f), ToColor(p.color));
        }
    }
    dl->PopClipRect();

    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        Vec2 world = screenToWorld(io.MousePos);
        GameObject* hit = nullptr;
        for (const auto& up : objs) {
            GameObject* go = up.get();
            auto* sr = go->GetComponent<SpriteRenderer>();
            if (!sr || !go->active) continue;
            Vec3 wp = go->transform->Position();
            Vec3 ls = go->transform->LossyScale();
            float hx = sr->size.x * ls.x * 0.5f, hy = sr->size.y * ls.y * 0.5f;
            if (world.x >= wp.x - hx && world.x <= wp.x + hx &&
                world.y >= wp.y - hy && world.y <= wp.y + hy)
                hit = go;
        }
        ed.Select(hit);
    }
    if (!ed.isPlaying() && ed.selected() && hovered &&
        ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        ed.selected()->transform->localPosition.x += io.MouseDelta.x / scale;
        ed.selected()->transform->localPosition.y -= io.MouseDelta.y / scale;
        ed.dirty = true;
    }
}

void DrawScene3D(EditorState& ed, ImDrawList* dl, ImVec2 canvasPos, ImVec2 canvasSize,
                 ImVec2 canvasEnd, bool hovered, ImGuiIO& io) {
    ImVec2 center(canvasPos.x + canvasSize.x * 0.5f, canvasPos.y + canvasSize.y * 0.5f);

    // Orbit controls.
    if (hovered && io.MouseWheel != 0.0f)
        ed.camDist = Mathf::Clamp(ed.camDist * (1.0f - io.MouseWheel * 0.1f), 2.0f, 400.0f);
    if (hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Left) && !ed.selected()) {
        ed.camYaw   -= io.MouseDelta.x * 0.4f;
        ed.camPitch = Mathf::Clamp(ed.camPitch + io.MouseDelta.y * 0.4f, -85.0f, 85.0f);
    }
    if (hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
        ed.camYaw   -= io.MouseDelta.x * 0.4f;
        ed.camPitch = Mathf::Clamp(ed.camPitch + io.MouseDelta.y * 0.4f, -85.0f, 85.0f);
    }

    float yawR = ed.camYaw * Mathf::Deg2Rad, pitchR = ed.camPitch * Mathf::Deg2Rad;
    Vec3 dir{Mathf::Cos(pitchR) * Mathf::Sin(yawR), Mathf::Sin(pitchR),
             Mathf::Cos(pitchR) * Mathf::Cos(yawR)};
    Vec3 eye = ed.camTarget + dir * ed.camDist;
    Mat4 view = Mat4::LookAt(eye, ed.camTarget, Vec3::Up);
    Mat4 proj = Mat4::Perspective(50.0f, canvasSize.x / canvasSize.y, 0.1f, 2000.0f);
    Mat4 vp = proj * view;

    auto toScreen = [&](const Vec4& c, ImVec2& out) -> bool {
        if (c.w <= 0.05f) return false;
        out = ImVec2(center.x + (c.x / c.w) * canvasSize.x * 0.5f,
                     center.y - (c.y / c.w) * canvasSize.y * 0.5f);
        return true;
    };

    dl->PushClipRect(canvasPos, canvasEnd, true);

    // Ground grid on the XZ plane.
    for (int i = -10; i <= 10; ++i) {
        ImVec2 a, b;
        if (toScreen(vp * Vec4{Vec3{(float)i, 0, -10}, 1}, a) &&
            toScreen(vp * Vec4{Vec3{(float)i, 0, 10}, 1}, b))
            dl->AddLine(a, b, IM_COL32(50, 50, 64, 255));
        if (toScreen(vp * Vec4{Vec3{-10, 0, (float)i}, 1}, a) &&
            toScreen(vp * Vec4{Vec3{10, 0, (float)i}, 1}, b))
            dl->AddLine(a, b, IM_COL32(50, 50, 64, 255));
    }

    // Wireframe meshes.
    const auto& objs = ed.scene().Objects();
    for (const auto& up : objs) {
        GameObject* go = up.get();
        auto* mr = go->GetComponent<MeshRenderer>();
        if (!mr || !go->active) continue;
        Mat4 m = vp * go->transform->LocalToWorldMatrix();
        ImU32 col = (go == ed.selected()) ? IM_COL32(255, 200, 0, 255) : ToColor(mr->color);
        const auto& v = mr->mesh.vertices;
        const auto& t = mr->mesh.triangles;
        for (size_t i = 0; i + 2 < t.size(); i += 3) {
            ImVec2 p0, p1, p2;
            if (toScreen(m * Vec4{v[t[i]], 1}, p0) &&
                toScreen(m * Vec4{v[t[i + 1]], 1}, p1) &&
                toScreen(m * Vec4{v[t[i + 2]], 1}, p2)) {
                dl->AddLine(p0, p1, col);
                dl->AddLine(p1, p2, col);
                dl->AddLine(p2, p0, col);
            }
        }
    }
    dl->PopClipRect();

    // Click to select the nearest object center on screen.
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        GameObject* hit = nullptr;
        float best = 18.0f; // pixels
        for (const auto& up : objs) {
            GameObject* go = up.get();
            if (!go->GetComponent<MeshRenderer>() || !go->active) continue;
            ImVec2 sp;
            if (!toScreen(vp * Vec4{go->transform->Position(), 1}, sp)) continue;
            float d = Mathf::Sqrt((sp.x - io.MousePos.x) * (sp.x - io.MousePos.x) +
                                  (sp.y - io.MousePos.y) * (sp.y - io.MousePos.y));
            if (d < best) { best = d; hit = go; }
        }
        if (hit) ed.Select(hit);
    }
}

void DrawViewport(EditorState& ed) {
    ImGui::Begin("Scene");

    // 2D / 3D toggle + frame the selection.
    if (ImGui::Button(ed.view3D ? "3D" : "2D")) ed.view3D = !ed.view3D;
    ImGui::SameLine();
    if (ImGui::Button("Frame") && ed.selected()) {
        Vec3 p = ed.selected()->transform->Position();
        ed.camTarget = p;
        ed.cameraPos = {p.x, p.y};
    }
    ImGui::SameLine();
    ImGui::TextDisabled(ed.view3D ? "drag: orbit  wheel: zoom"
                                  : "drag: move  right-drag: pan  wheel: zoom");

    ImVec2 canvasPos  = ImGui::GetCursorScreenPos();
    ImVec2 canvasSize = ImGui::GetContentRegionAvail();
    if (canvasSize.x < 50) canvasSize.x = 50;
    if (canvasSize.y < 50) canvasSize.y = 50;
    ImVec2 canvasEnd(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(canvasPos, canvasEnd, IM_COL32(15, 15, 30, 255));

    ImGui::InvisibleButton("canvas", canvasSize,
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    bool hovered = ImGui::IsItemHovered();
    ImGuiIO& io = ImGui::GetIO();

    if (ed.view3D) DrawScene3D(ed, dl, canvasPos, canvasSize, canvasEnd, hovered, io);
    else           DrawScene2D(ed, dl, canvasPos, canvasSize, canvasEnd, hovered, io);

    // Corner overlay.
    char overlay[128];
    std::snprintf(overlay, sizeof(overlay), "%s  |  %d objects%s%s",
                  ed.view3D ? "3D" : "2D", (int)ed.scene().Objects().size(),
                  ed.selected() ? "  |  " : "",
                  ed.selected() ? ed.selected()->name.c_str() : "");
    dl->AddText(ImVec2(canvasPos.x + 8, canvasPos.y + 6), IM_COL32(200, 200, 210, 255), overlay);

    ImGui::End();
}

} // namespace

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i)
        if (std::string(argv[i]) == "--selftest") return RunSelfTest();

    SDL_SetMainReady(); // we manage the entry point (SDL_MAIN_HANDLED)
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << "\n";
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow(
        "OkaySpace Editor", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        1280, 800, SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!window) { std::cerr << "CreateWindow failed: " << SDL_GetError() << "\n"; return 1; }

    // SDL's 2D renderer (Direct3D/Metal/OpenGL, chosen by SDL); fall back to software.
    SDL_Renderer* renderer = SDL_CreateRenderer(
        window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) renderer = SDL_CreateRenderer(window, -1, 0);
    if (!renderer) { std::cerr << "CreateRenderer failed: " << SDL_GetError() << "\n"; return 1; }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    ApplyTheme();
    ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer2_Init(renderer);

    // Start empty; the New Project chooser pops up on launch (2D / 3D / Empty).
    EditorState ed;
    ConsoleLog("Welcome to OkaySpace v" OKAY_ENGINE_VERSION
               ". Choose a 2D or 3D project to begin.");

    bool running = true;
    std::string lastTitle;
    Uint64 last = SDL_GetPerformanceCounter();
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            ImGui_ImplSDL2_ProcessEvent(&e);
            if (e.type == SDL_QUIT) running = false;
            if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_CLOSE &&
                e.window.windowID == SDL_GetWindowID(window))
                running = false;
        }

        Uint64 now = SDL_GetPerformanceCounter();
        float dt = (float)((now - last) / (double)SDL_GetPerformanceFrequency());
        last = now;
        ed.Tick(dt);
        ed.TickServices(dt); // Steam callbacks + networking every frame

        // Keep the window title in sync with the scene + dirty state.
        std::string title = "OkaySpace Editor  -  " + ed.scene().Name() +
                            (ed.dirty ? " *" : "") + "   [v" OKAY_ENGINE_VERSION "]";
        if (title != lastTitle) { SDL_SetWindowTitle(window, title.c_str()); lastTitle = title; }

        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        HandleShortcuts(ed);
        DrawDockSpace(ed, running);
        DrawNewProjectPopup(ed);
        DrawUpdatePopup();
        DrawHierarchy(ed);
        DrawViewport(ed);   // the "Scene" panel
        DrawInspector(ed);
        DrawConsole();
        DrawProject(ed);
        DrawServices(ed);

        ImGui::Render();
        SDL_RenderSetScale(renderer, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
        SDL_SetRenderDrawColor(renderer, 30, 30, 34, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
        SDL_RenderPresent(renderer);
    }

    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
#endif // OKAY_EDITOR_HEADLESS
