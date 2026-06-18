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
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

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

    // Undo / redo around a creation.
    EditorState ue;
    std::size_t base = ue.scene().Objects().size();
    ue.CreateSprite("Temp");
    check(ue.scene().Objects().size() == base + 1, "created object");
    check(ue.CanUndo(), "can undo");
    ue.Undo();
    check(ue.scene().Objects().size() == base, "undo removes object");
    check(ue.CanRedo(), "can redo");
    ue.Redo();
    check(ue.scene().Objects().size() == base + 1, "redo restores object");

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
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
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

// ---- Executable path helper (used by Build Game to find the player) -------
namespace updater {
namespace fs = std::filesystem;

bool pendingQuit = false; // reserved (self-update was removed)

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

} // namespace updater

std::string g_updateStatus;
bool g_openUpdatePopup = false;
bool g_openAbout = false;
bool g_showNewProject = true;   // show the project chooser on launch

// Panel visibility (View menu).
bool g_showHierarchy = true, g_showInspector = true, g_showConsole = true,
     g_showProject = true, g_showServices = true, g_showScriptEditor = true;
bool g_showGame = true;   // Unity-style Game view (main-camera render)

// File dialogs.
bool g_showSaveAs = false, g_showOpen = false;
char g_pathBuf[256] = "scene.okayscene";

// Extra panels / tools.
bool  g_showStats = true;
bool  g_showInstPrefab = false;
char  g_prefabBuf[256] = "Prefab.okayprefab";
bool  g_showBuildGame = false;
char  g_buildDirBuf[256] = "build/MyGame";
char  g_buildNameBuf[128] = "MyGame";
std::string g_buildStatus;
bool  g_openBuildResult = false;
bool  g_snap = false;
float g_snapSize = 1.0f;
// Active transform tool for the Scene view (Unity's W/E/R).
enum class Tool { Move, Rotate, Scale };
Tool  g_tool = Tool::Move;

// Per-object editor-only Euler-angle cache (degrees) for the inspector.
std::unordered_map<GameObject*, Vec3> g_euler;

// Editable text buffers for script/visual-script components, keyed by pointer.
std::unordered_map<void*, std::vector<char>> g_codeBuf;
std::vector<char>& CodeBuffer(void* key, const std::string& initial) {
    auto it = g_codeBuf.find(key);
    if (it == g_codeBuf.end()) {
        std::vector<char> b(65536, 0);
        std::strncpy(b.data(), initial.c_str(), b.size() - 1);
        it = g_codeBuf.emplace(key, std::move(b)).first;
    }
    return it->second;
}
void SetCodeBuffer(void* key, const std::string& text) {
    auto& b = g_codeBuf[key];
    if (b.size() < 65536) b.assign(65536, 0); else std::fill(b.begin(), b.end(), 0);
    std::strncpy(b.data(), text.c_str(), b.size() - 1);
}

// External-IDE helpers: write/read script files and open them in the system editor.
namespace extide {
namespace fs = std::filesystem;
bool WriteFile(const std::string& path, const std::string& text) {
    std::ofstream f(path);
    if (!f) return false;
    f << text;
    return static_cast<bool>(f);
}
std::string ReadFile(const std::string& path) {
    std::ifstream f(path);
    std::stringstream ss; ss << f.rdbuf();
    return ss.str();
}
void OpenExternal(const std::string& path) {
    // Prefer VS Code if present, otherwise the OS default handler.
#if defined(_WIN32)
    int rc = std::system(("code \"" + path + "\"").c_str());
    if (rc != 0) rc = std::system(("start \"\" \"" + path + "\"").c_str());
#elif defined(__APPLE__)
    int rc = std::system(("code \"" + path + "\" 2>/dev/null").c_str());
    if (rc != 0) rc = std::system(("open \"" + path + "\"").c_str());
#else
    int rc = std::system(("code \"" + path + "\" >/dev/null 2>&1").c_str());
    if (rc != 0) rc = std::system(("xdg-open \"" + path + "\" >/dev/null 2>&1").c_str());
#endif
    (void)rc;
}
std::string ExtFor(const std::string& lang) {
    return lang == "lua" ? "lua" : lang == "csharp" ? "cs" : "okay";
}
} // namespace extide

// ---- Build Game: export the current scene as a standalone runnable game ----
// A shipped game is just <Game>.exe (a copy of the player runtime) + a
// game.okayscene sitting beside it. Double-clicking the exe runs the scene.
namespace builder {
namespace fs = std::filesystem;

// Locate the player runtime that ships next to the editor executable.
std::string FindPlayer() {
    fs::path self = updater::SelfPath();
    if (self.empty()) return {};
    fs::path dir = self.parent_path();
#if defined(_WIN32)
    const char* names[] = {"OkaySpacePlayer.exe", "okay-player.exe"};
#else
    const char* names[] = {"okay-player", "OkaySpacePlayer"};
#endif
    for (const char* n : names) {
        std::error_code ec;
        if (fs::exists(dir / n, ec)) return (dir / n).string();
    }
    return {};
}

// Build the game into outDir. Returns a human-readable status string.
std::string Build(EditorState& ed, const std::string& outDir,
                  const std::string& gameName) {
    std::error_code ec;
    fs::path dir(outDir);
    fs::create_directories(dir, ec);
    if (ec) return "Couldn't create folder: " + ec.message();

    // 1) Write the scene next to the player as game.okayscene.
    if (!SceneSerializer::SaveToFile(ed.scene(), (dir / "game.okayscene").string()))
        return "Failed to write game.okayscene.";

    // 2) Copy the player runtime, renamed to <Game>.exe.
    std::string player = FindPlayer();
#if defined(_WIN32)
    std::string exeName = gameName + ".exe";
#else
    std::string exeName = gameName;
#endif
    if (player.empty()) {
        return "Wrote game.okayscene to " + dir.string() +
               ", but couldn't find the player runtime to copy. Place "
               "OkaySpacePlayer next to the editor and rebuild.";
    }
    fs::copy_file(player, dir / exeName, fs::copy_options::overwrite_existing, ec);
    if (ec) return "Wrote scene but couldn't copy the player: " + ec.message();
#if !defined(_WIN32)
    fs::permissions(dir / exeName, fs::perms::owner_exec | fs::perms::group_exec |
                    fs::perms::others_exec, fs::perm_options::add, ec);
#endif

    // 3) Copy every asset the scene references (textures, WAVs, frames) so the
    // shipped game folder is self-contained, preserving relative subpaths.
    int copied = 0, missing = 0;
    for (const std::string& asset : SceneSerializer::CollectAssetPaths(ed.scene())) {
        fs::path src(asset);
        std::error_code aec;
        if (!fs::exists(src, aec)) { ++missing; continue; }
        fs::path dst = src.is_absolute() ? (dir / src.filename()) : (dir / src);
        if (dst.has_parent_path()) fs::create_directories(dst.parent_path(), aec);
        fs::copy_file(src, dst, fs::copy_options::overwrite_existing, aec);
        if (!aec) ++copied;
    }

    std::string msg = "Built '" + gameName + "' to " + dir.string() +
                      " — run " + exeName + " to play.";
    if (copied)  msg += "  (" + std::to_string(copied) + " asset(s) copied)";
    if (missing) msg += "  WARNING: " + std::to_string(missing) +
                        " referenced asset(s) not found and not copied.";
    return msg;
}
} // namespace builder

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
    // Rounding & spacing for a soft, modern look.
    s.WindowRounding    = 6.0f;  s.ChildRounding    = 6.0f;
    s.FrameRounding     = 5.0f;  s.GrabRounding     = 5.0f;
    s.PopupRounding     = 6.0f;  s.TabRounding      = 5.0f;
    s.ScrollbarRounding = 9.0f;
    s.WindowPadding   = ImVec2(10, 10); s.FramePadding = ImVec2(8, 5);
    s.ItemSpacing     = ImVec2(8, 7);   s.ItemInnerSpacing = ImVec2(6, 5);
    s.ScrollbarSize   = 13.0f;          s.GrabMinSize = 11.0f;
    s.WindowBorderSize = 0.0f;          s.FrameBorderSize = 0.0f;
    s.WindowTitleAlign = ImVec2(0.02f, 0.5f);
    s.WindowMenuButtonPosition = ImGuiDir_None;

    // A cohesive dark palette with a warm blue accent.
    const ImVec4 accent      = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
    const ImVec4 accentDim   = ImVec4(0.26f, 0.59f, 0.98f, 0.55f);
    const ImVec4 accentHover = ImVec4(0.34f, 0.66f, 1.00f, 1.00f);
    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg]         = ImVec4(0.115f, 0.123f, 0.145f, 1.00f);
    c[ImGuiCol_ChildBg]          = ImVec4(0.135f, 0.143f, 0.168f, 0.40f);
    c[ImGuiCol_PopupBg]          = ImVec4(0.10f, 0.107f, 0.128f, 0.98f);
    c[ImGuiCol_Border]           = ImVec4(0.00f, 0.00f, 0.00f, 0.35f);
    c[ImGuiCol_FrameBg]          = ImVec4(0.20f, 0.21f, 0.245f, 1.00f);
    c[ImGuiCol_FrameBgHovered]   = ImVec4(0.26f, 0.28f, 0.32f, 1.00f);
    c[ImGuiCol_FrameBgActive]    = ImVec4(0.30f, 0.33f, 0.38f, 1.00f);
    c[ImGuiCol_TitleBg]          = ImVec4(0.09f, 0.095f, 0.115f, 1.00f);
    c[ImGuiCol_TitleBgActive]    = ImVec4(0.13f, 0.20f, 0.31f, 1.00f);
    c[ImGuiCol_MenuBarBg]        = ImVec4(0.135f, 0.143f, 0.168f, 1.00f);
    c[ImGuiCol_Header]           = accentDim;
    c[ImGuiCol_HeaderHovered]    = accentHover;
    c[ImGuiCol_HeaderActive]     = accent;
    c[ImGuiCol_Button]           = ImVec4(0.22f, 0.24f, 0.29f, 1.00f);
    c[ImGuiCol_ButtonHovered]    = accentHover;
    c[ImGuiCol_ButtonActive]     = accent;
    c[ImGuiCol_CheckMark]        = accent;
    c[ImGuiCol_SliderGrab]       = accent;
    c[ImGuiCol_SliderGrabActive] = accentHover;
    c[ImGuiCol_Separator]        = ImVec4(0.00f, 0.00f, 0.00f, 0.45f);
    c[ImGuiCol_SeparatorHovered] = accentDim;
    c[ImGuiCol_ResizeGrip]       = accentDim;
    c[ImGuiCol_ResizeGripHovered]= accentHover;
    c[ImGuiCol_Tab]              = ImVec4(0.155f, 0.165f, 0.195f, 1.00f);
    c[ImGuiCol_TabHovered]       = accentHover;
    c[ImGuiCol_TabActive]        = ImVec4(0.20f, 0.32f, 0.50f, 1.00f);
    c[ImGuiCol_TabUnfocused]     = ImVec4(0.135f, 0.143f, 0.168f, 1.00f);
    c[ImGuiCol_TabUnfocusedActive] = ImVec4(0.17f, 0.22f, 0.30f, 1.00f);
    c[ImGuiCol_DockingPreview]   = accentDim;
    c[ImGuiCol_TextSelectedBg]   = accentDim;
    c[ImGuiCol_NavHighlight]     = accent;
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
    ImGui::DockBuilderDockWindow("Script Editor", down);
    ImGui::DockBuilderDockWindow("Stats", right);
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
        if (ImGui::MenuItem("Open...", "Ctrl+O")) g_showOpen = true;
        if (ImGui::MenuItem("Save", "Ctrl+S")) {
            std::string p = ed.path().empty() ? "scene.okayscene" : ed.path();
            if (ed.Save(p)) { ConsoleLog("Saved " + p); ed.Achievement("FIRST_SAVE"); }
            else ConsoleLog("Save failed");
        }
        if (ImGui::MenuItem("Save As...")) {
            std::strncpy(g_pathBuf, ed.path().empty() ? "scene.okayscene" : ed.path().c_str(),
                         sizeof(g_pathBuf) - 1);
            g_showSaveAs = true;
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Build Game...", "Ctrl+B")) g_showBuildGame = true;
        ImGui::Separator();
        if (ImGui::MenuItem("Exit")) running = false;
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit")) {
        if (ImGui::MenuItem("Undo", "Ctrl+Z", false, ed.CanUndo())) { ed.Undo(); ConsoleLog("Undo"); }
        if (ImGui::MenuItem("Redo", "Ctrl+Y", false, ed.CanRedo())) { ed.Redo(); ConsoleLog("Redo"); }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
        ImGui::MenuItem("Hierarchy", nullptr, &g_showHierarchy);
        ImGui::MenuItem("Game", nullptr, &g_showGame);
        ImGui::MenuItem("Inspector", nullptr, &g_showInspector);
        ImGui::MenuItem("Console", nullptr, &g_showConsole);
        ImGui::MenuItem("Project", nullptr, &g_showProject);
        ImGui::MenuItem("Services", nullptr, &g_showServices);
        ImGui::MenuItem("Script Editor", nullptr, &g_showScriptEditor);
        ImGui::MenuItem("Stats", nullptr, &g_showStats);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("GameObject")) {
        bool created = false;
        if (ImGui::MenuItem("Create Empty"))   { ed.CreateEmpty();   ConsoleLog("Created empty GameObject"); created = true; }
        if (ImGui::MenuItem("Create Sprite"))  { ed.CreateSprite();  ConsoleLog("Created Sprite"); created = true; }
        if (ImGui::MenuItem("Create Camera"))  { ed.CreateCamera();  ConsoleLog("Created Camera"); created = true; }
        ImGui::Separator();
        if (ImGui::MenuItem("Create Cube (3D)"))     { ed.CreateCube();    ConsoleLog("Created Cube"); created = true; }
        if (ImGui::MenuItem("Create Pyramid (3D)"))  { ed.CreatePyramid(); ConsoleLog("Created Pyramid"); created = true; }
        if (ImGui::MenuItem("Create Sphere (3D)"))   { ed.CreateMesh("Sphere");   ConsoleLog("Created Sphere"); created = true; }
        if (ImGui::MenuItem("Create Cylinder (3D)")) { ed.CreateMesh("Cylinder"); ConsoleLog("Created Cylinder"); created = true; }
        if (ImGui::MenuItem("Create Plane (3D)"))    { ed.CreateMesh("Plane");    ConsoleLog("Created Plane"); created = true; }
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
        ImGui::Separator();
        if (ImGui::BeginMenu("UI")) {   // each UI element is its own GameObject
            if (ImGui::MenuItem("Button"))       { ed.Select(ed.CreateEmpty("Button"));   ed.selected()->AddComponent<UIButton>();      ed.dirty = true; created = true; }
            if (ImGui::MenuItem("Panel"))        { ed.Select(ed.CreateEmpty("Panel"));    ed.selected()->AddComponent<UIPanel>();       ed.dirty = true; created = true; }
            if (ImGui::MenuItem("Image"))        { ed.Select(ed.CreateEmpty("Image"));    ed.selected()->AddComponent<UIImage>();       ed.dirty = true; created = true; }
            if (ImGui::MenuItem("Text"))         { ed.Select(ed.CreateEmpty("Text"));     auto* t = ed.selected()->AddComponent<TextRenderer>(); t->screenSpace = true; ed.dirty = true; created = true; }
            if (ImGui::MenuItem("Progress Bar")) { ed.Select(ed.CreateEmpty("ProgressBar")); ed.selected()->AddComponent<UIProgressBar>(); ed.dirty = true; created = true; }
            if (ImGui::MenuItem("Slider"))       { ed.Select(ed.CreateEmpty("Slider"));   ed.selected()->AddComponent<UISlider>();      ed.dirty = true; created = true; }
            if (ImGui::MenuItem("Toggle"))       { ed.Select(ed.CreateEmpty("Toggle"));   ed.selected()->AddComponent<UIToggle>();      ed.dirty = true; created = true; }
            ImGui::EndMenu();
        }
        if (created) ed.Achievement("FIRST_OBJECT");
        ImGui::Separator();
        if (ImGui::MenuItem("Instantiate Prefab...")) g_showInstPrefab = true;
        if (ImGui::MenuItem("Duplicate Selected", "Ctrl+D", false, ed.selected() != nullptr)) {
            ed.DuplicateSelected(); ConsoleLog("Duplicated selection");
        }
        if (ImGui::MenuItem("Delete Selected", "Del", false, ed.selected() != nullptr)) {
            ed.DeleteSelected(); ConsoleLog("Deleted selection");
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Engine")) {
        if (ImGui::MenuItem("Build Game...", "Ctrl+B")) g_showBuildGame = true;
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Help")) {
        if (ImGui::MenuItem("About OkaySpace")) g_openAbout = true;
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
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.42f, 0.34f, 0.62f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.52f, 0.42f, 0.75f, 1.0f));
    if (ImGui::Button("Build", ImVec2(54, 0))) g_showBuildGame = true;
    ImGui::PopStyleColor(2);

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
    static char filter[96] = "";
    static bool autoScroll = true;
    if (ImGui::Begin("Console")) {
        if (ImGui::Button("Clear")) g_console.clear();
        ImGui::SameLine();
        ImGui::Checkbox("Auto-scroll", &autoScroll);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(180);
        ImGui::InputTextWithHint("##cfilter", "filter", filter, sizeof(filter));
        ImGui::SameLine();
        ImGui::TextDisabled("%zu", g_console.size());
        ImGui::Separator();
        ImGui::BeginChild("log");
        std::string needle = filter;
        for (auto& n : needle) n = (char)std::tolower((unsigned char)n);
        for (const auto& line : g_console) {
            if (!needle.empty()) {
                std::string low = line;
                for (auto& ch : low) ch = (char)std::tolower((unsigned char)ch);
                if (low.find(needle) == std::string::npos) continue;
            }
            // Tint by severity heuristics so problems stand out.
            ImVec4 col(0.82f, 0.84f, 0.88f, 1.0f);
            if (line.find("fail") != std::string::npos || line.find("error") != std::string::npos ||
                line.find("Error") != std::string::npos)
                col = ImVec4(0.96f, 0.45f, 0.42f, 1.0f);
            else if (line.find("Saved") != std::string::npos || line.find("Built") != std::string::npos ||
                     line.find("Updated") != std::string::npos)
                col = ImVec4(0.55f, 0.86f, 0.55f, 1.0f);
            ImGui::PushStyleColor(ImGuiCol_Text, col);
            ImGui::TextUnformatted(line.c_str());
            ImGui::PopStyleColor();
        }
        if (autoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) ImGui::SetScrollHereY(1.0f);
        ImGui::EndChild();
    }
    ImGui::End();
}

// A short tag for a file, by extension, for the asset browser.
static const char* AssetTag(const std::string& ext) {
    if (ext == ".okayscene")  return "[Scene] ";
    if (ext == ".okayprefab") return "[Prefab]";
    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tga")
        return "[Image] ";
    if (ext == ".wav")        return "[Sound] ";
    if (ext == ".okay" || ext == ".lua" || ext == ".cs") return "[Script]";
    return "[File]  ";
}

void DrawProject(EditorState& ed) {
    namespace fs = std::filesystem;
    static char dirBuf[512] = ".";
    static bool recursive = false;
    static std::string lastProject;   // re-home the browser when a project opens
    if (ImGui::Begin("Project")) {
        // When a project is opened/created, point the browser at its Assets/.
        if (!ed.projectDir().empty() && ed.projectDir() != lastProject) {
            lastProject = ed.projectDir();
            std::string assets = (fs::path(ed.projectDir()) / "Assets").string();
            std::strncpy(dirBuf, assets.c_str(), sizeof(dirBuf) - 1);
        }

        if (!ed.projectDir().empty()) {
            fs::path root(ed.projectDir());
            ImGui::Text("Project: %s", root.filename().string().c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("Assets")) {
                std::string a = (root / "Assets").string();
                std::strncpy(dirBuf, a.c_str(), sizeof(dirBuf) - 1);
            }
        } else {
            ImGui::TextDisabled("No project — File > New Project to create one.");
        }

        ImGui::SetNextItemWidth(-130);
        ImGui::InputText("##projdir", dirBuf, sizeof(dirBuf));
        ImGui::SameLine();
        if (ImGui::Button("Up")) {
            std::error_code ec;
            fs::path p = fs::absolute(dirBuf, ec).parent_path();
            if (!ec) std::strncpy(dirBuf, p.string().c_str(), sizeof(dirBuf) - 1);
        }
        ImGui::SameLine();
        ImGui::Checkbox("All", &recursive);   // recurse the whole subtree (Unity-like)
        ImGui::TextDisabled("Scene: %s  |  %d objects",
                            ed.path().empty() ? "(unsaved)" : ed.path().c_str(),
                            (int)ed.scene().Objects().size());
        ImGui::Separator();

        ImGui::BeginChild("assets");
        std::error_code ec;
        fs::path dir(dirBuf);
        std::vector<fs::directory_entry> dirs, files;
        if (fs::is_directory(dir, ec)) {
            if (recursive) {
                for (auto& e : fs::recursive_directory_iterator(dir, ec))
                    if (!e.is_directory(ec)) files.push_back(e);
            } else {
                for (auto& e : fs::directory_iterator(dir, ec)) {
                    if (e.is_directory(ec)) dirs.push_back(e);
                    else files.push_back(e);
                }
            }
        }
        auto byName = [](const fs::directory_entry& a, const fs::directory_entry& b) {
            return a.path().filename().string() < b.path().filename().string();
        };
        std::sort(dirs.begin(), dirs.end(), byName);
        std::sort(files.begin(), files.end(), byName);

        for (auto& e : dirs) {
            std::string name = e.path().filename().string();
            if (ImGui::Selectable(("[Dir]    " + name).c_str()))
                std::strncpy(dirBuf, e.path().string().c_str(), sizeof(dirBuf) - 1);
        }
        for (auto& e : files) {
            std::string ext = e.path().extension().string();
            for (auto& ch : ext) ch = (char)std::tolower((unsigned char)ch);
            std::string label = std::string(AssetTag(ext)) + " " + e.path().filename().string();
            if (ImGui::Selectable(label.c_str())) {
                std::string full = e.path().string();
                if (ext == ".okayscene") {
                    std::string err;
                    if (ed.Load(full, &err)) ConsoleLog("Opened " + full);
                    else ConsoleLog("Open failed: " + err);
                } else if (ext == ".okayprefab") {
                    ed.PushUndo();
                    std::string err;
                    if (GameObject* r = SceneSerializer::InstantiateFromFile(ed.scene(), full, &err)) {
                        ed.Select(r); ed.dirty = true; ConsoleLog("Instantiated " + full);
                    } else ConsoleLog("Prefab load failed: " + err);
                }
            }
        }
        if (dirs.empty() && files.empty())
            ImGui::TextDisabled("(empty or unreadable folder)");
        ImGui::EndChild();
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

void DrawStats(EditorState& ed) {
    if (!ImGui::Begin("Stats")) { ImGui::End(); return; }
    ImGuiIO& io = ImGui::GetIO();
    static float hist[120] = {0};
    static int hi = 0;
    hist[hi] = io.Framerate; hi = (hi + 1) % 120;
    ImGui::Text("FPS: %.0f  (%.2f ms)", io.Framerate, io.Framerate > 0 ? 1000.0f / io.Framerate : 0.0f);
    ImGui::PlotLines("##fps", hist, 120, hi, nullptr, 0.0f, 240.0f, ImVec2(-1, 60));
    ImGui::Separator();
    ImGui::Text("Mode: %s", ed.isPlaying() ? "PLAYING" : "EDIT");
    ImGui::Text("GameObjects: %d", (int)ed.scene().Objects().size());
    ImGui::Text("Sprites: %d", (int)ed.scene().FindObjectsOfType<SpriteRenderer>().size());
    ImGui::Text("Meshes: %d", (int)ed.scene().FindObjectsOfType<MeshRenderer>().size());
    ImGui::Text("Colliders: %d", (int)ed.scene().FindObjectsOfType<Collider2D>().size());
    ImGui::End();
}

// A dedicated code/graph editor panel (separate from the Inspector), with
// external-IDE round-tripping.
void DrawScriptEditor(EditorState& ed) {
    if (!ImGui::Begin("Script Editor")) { ImGui::End(); return; }
    GameObject* go = ed.selected();
    ScriptComponent* sc = go ? go->GetComponent<ScriptComponent>() : nullptr;
    VisualScriptComponent* vsc = go ? go->GetComponent<VisualScriptComponent>() : nullptr;

    if (!sc && !vsc) {
        ImGui::TextDisabled("Select an object with a Script or Visual Script.");
        ImGui::TextDisabled("Add one via Inspector > Add Component.");
        ImGui::End();
        return;
    }

    if (sc) {
        ImGui::Text("Script  -  %s", go->name.c_str());
        const char* langs[] = {"okayscript", "lua", "csharp"};
        int li = sc->Language() == "lua" ? 1 : sc->Language() == "csharp" ? 2 : 0;
        ImGui::SetNextItemWidth(150);
        if (ImGui::Combo("Lang", &li, langs, 3)) sc->SetLanguage(langs[li]);
        if (!sc->Path().empty()) { ImGui::SameLine(); ImGui::TextDisabled("(%s)", sc->Path().c_str()); }

        auto& buf = CodeBuffer(sc, sc->Source().empty()
            ? "function start()\nend\n\nfunction update(dt)\n  move(2 * dt, 0)\nend\n"
            : sc->Source());
        ImVec2 av = ImGui::GetContentRegionAvail(); av.y -= 34.0f; if (av.y < 60) av.y = 60;
        ImGui::InputTextMultiline("##editor", buf.data(), buf.size(), av);

        auto filePath = [&]() {
            return sc->Path().empty() ? go->name + "." + extide::ExtFor(sc->Language()) : sc->Path();
        };
        if (ImGui::Button("Compile & Run")) {
            std::string e;
            ConsoleLog(sc->LoadSource(buf.data(), &e) ? "Compiled OK" : "Error: " + e);
            ed.dirty = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Save File")) {
            std::string p = filePath();
            if (extide::WriteFile(p, buf.data())) { sc->SetPath(p); ConsoleLog("Saved " + p); }
            else { ConsoleLog("Save failed"); }
            ed.dirty = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Open in IDE")) {
            std::string p = filePath();
            extide::WriteFile(p, buf.data()); sc->SetPath(p); extide::OpenExternal(p);
            ConsoleLog("Opened " + p + " in external IDE");
        }
        ImGui::SameLine();
        if (ImGui::Button("Reload")) {
            if (!sc->Path().empty()) {
                std::string src = extide::ReadFile(sc->Path());
                SetCodeBuffer(sc, src);
                std::string e; sc->LoadSource(src, &e);
                ConsoleLog("Reloaded " + sc->Path());
            } else ConsoleLog("No file to reload (Save File first)");
        }
    } else {
        ImGui::Text("Visual Script  -  %s", go->name.c_str());
        auto& buf = CodeBuffer(vsc, vsc->Source().empty()
            ? "# Move right at 2 units/sec\nnode 0 OnUpdate\nnode 1 Const 2\n"
              "node 2 DeltaTime\nnode 3 Mul\nnode 4 Const 0\nnode 5 Translate\n"
              "data 3 0 1 0\ndata 3 1 2 0\ndata 5 0 3 0\ndata 5 1 4 0\n"
              "exec 0 0 5\nentry OnUpdate 0\n"
            : vsc->Source());
        ImVec2 av = ImGui::GetContentRegionAvail(); av.y -= 34.0f; if (av.y < 60) av.y = 60;
        ImGui::InputTextMultiline("##vseditor", buf.data(), buf.size(), av);
        std::string p = go->name + ".okayvs";
        if (ImGui::Button("Apply Graph")) {
            std::string e;
            ConsoleLog(vsc->LoadFromText(buf.data(), &e) ? "Graph applied" : "Error: " + e);
            ed.dirty = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Open in IDE")) {
            extide::WriteFile(p, buf.data()); extide::OpenExternal(p);
            ConsoleLog("Opened " + p + " in external IDE");
        }
        ImGui::SameLine();
        if (ImGui::Button("Reload")) {
            if (extide::fs::exists(p)) {
                std::string src = extide::ReadFile(p);
                SetCodeBuffer(vsc, src);
                std::string e; vsc->LoadFromText(src, &e);
                ConsoleLog("Reloaded " + p);
            } else ConsoleLog("No file to reload");
        }
    }
    ImGui::End();
}

// Global keyboard shortcuts (ignored while typing in a field).
void HandleShortcuts(EditorState& ed) {
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput) return;
    bool ctrl = io.KeyCtrl;
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Z, false)) { if (ed.Undo()) ConsoleLog("Undo"); }
    if (ctrl && (ImGui::IsKeyPressed(ImGuiKey_Y, false) ||
                 (io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z, false)))) {
        if (ed.Redo()) ConsoleLog("Redo");
    }
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_N, false)) g_showNewProject = true;
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_O, false)) g_showOpen = true;
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) {
        std::string p = ed.path().empty() ? "scene.okayscene" : ed.path();
        if (ed.Save(p)) { ConsoleLog("Saved " + p); ed.Achievement("FIRST_SAVE"); }
    }
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_D, false) && ed.selected()) {
        ed.DuplicateSelected(); ConsoleLog("Duplicated selection");
    }
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_B, false)) g_showBuildGame = true;
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

void DrawFileDialogs(EditorState& ed) {
    if (g_showOpen)   { ImGui::OpenPopup("Open Scene");    g_showOpen = false; }
    if (g_showSaveAs) { ImGui::OpenPopup("Save Scene As"); g_showSaveAs = false; }
    ImVec2 c = ImGui::GetMainViewport()->GetCenter();

    ImGui::SetNextWindowPos(c, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Open Scene", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::InputText("Path", g_pathBuf, sizeof(g_pathBuf));
        if (ImGui::Button("Open", ImVec2(120, 0))) {
            std::string err;
            if (ed.Load(g_pathBuf, &err)) ConsoleLog("Opened " + std::string(g_pathBuf));
            else ConsoleLog("Open failed: " + err);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowPos(c, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Save Scene As", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::InputText("Path", g_pathBuf, sizeof(g_pathBuf));
        if (ImGui::Button("Save", ImVec2(120, 0))) {
            if (ed.Save(g_pathBuf)) { ConsoleLog("Saved " + std::string(g_pathBuf)); ed.Achievement("FIRST_SAVE"); }
            else ConsoleLog("Save failed");
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    if (g_showInstPrefab) { ImGui::OpenPopup("Instantiate Prefab"); g_showInstPrefab = false; }
    ImGui::SetNextWindowPos(c, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Instantiate Prefab", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::InputText("Prefab", g_prefabBuf, sizeof(g_prefabBuf));
        if (ImGui::Button("Instantiate", ImVec2(120, 0))) {
            ed.PushUndo();
            std::string err;
            GameObject* r = SceneSerializer::InstantiateFromFile(ed.scene(), g_prefabBuf, &err);
            if (r) { ed.Select(r); ed.dirty = true; ConsoleLog("Instantiated " + std::string(g_prefabBuf)); }
            else ConsoleLog("Prefab load failed: " + err);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    if (g_showBuildGame) { ImGui::OpenPopup("Build Game"); g_showBuildGame = false; }
    ImGui::SetNextWindowPos(c, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Build Game", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Export this scene as a standalone game.");
        ImGui::TextDisabled("Creates <Name>.exe + game.okayscene in the output folder.");
        ImGui::Separator();
        ImGui::InputText("Game name", g_buildNameBuf, sizeof(g_buildNameBuf));
        ImGui::InputText("Output folder", g_buildDirBuf, sizeof(g_buildDirBuf));
        ImGui::Separator();
        if (ImGui::Button("Build", ImVec2(120, 0))) {
            g_buildStatus = builder::Build(ed, g_buildDirBuf, g_buildNameBuf);
            g_openBuildResult = true;
            ConsoleLog("Build Game: " + g_buildStatus);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    if (g_openBuildResult) { ImGui::OpenPopup("Build Result"); g_openBuildResult = false; }
    ImGui::SetNextWindowPos(c, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Build Result", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::PushTextWrapPos(420.0f);
        ImGui::TextUnformatted(g_buildStatus.c_str());
        ImGui::PopTextWrapPos();
        ImGui::Separator();
        if (ImGui::Button("OK", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void DrawNewProjectPopup(EditorState& ed) {
    if (g_showNewProject) { ImGui::OpenPopup("New Project"); g_showNewProject = false; }
    ImVec2 c = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(c, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("New Project", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        static char nameBuf[128] = "MyGame";
        static char locBuf[400]  = ".";
        ImGui::Text("Create a new project");
        ImGui::TextDisabled("A folder <Location>/<Name> is created with an Assets/ subfolder;");
        ImGui::TextDisabled("the starting scene is saved into Assets/.");
        ImGui::SetNextItemWidth(330); ImGui::InputText("Name", nameBuf, sizeof(nameBuf));
        ImGui::SetNextItemWidth(330); ImGui::InputText("Location", locBuf, sizeof(locBuf));
        ImGui::Separator();

        // After a NewScene*/template call, lay down the project folder and save.
        auto finishProject = [&]() {
            namespace fs = std::filesystem;
            std::string name = nameBuf[0] ? nameBuf : "MyGame";
            fs::path root = fs::path(locBuf[0] ? locBuf : ".") / name;
            std::error_code ec;
            fs::create_directories(root / "Assets", ec);
            if (ec) { ConsoleLog("Could not create project folder: " + ec.message()); return; }
            ed.setProjectDir(root.string());
            std::string sp = (root / "Assets" / (name + ".okayscene")).string();
            if (ed.Save(sp)) ConsoleLog("Created project at " + root.string());
            else ConsoleLog("Project folder made, but saving the scene failed.");
            ImGui::CloseCurrentPopup();
        };

        ImGui::TextDisabled("Pick a starting scene type.");
        if (ImGui::Button("2D Scene", ImVec2(160, 60))) { ed.NewScene2D(); finishProject(); }
        ImGui::SameLine();
        if (ImGui::Button("3D Scene", ImVec2(160, 60))) { ed.NewScene3D(); finishProject(); }
        ImGui::Spacing();
        ImGui::TextDisabled("Or start from a playable template:");
        if (ImGui::Button("Platformer", ImVec2(160, 44))) { ed.NewPlatformer(); finishProject(); }
        ImGui::SameLine();
        if (ImGui::Button("Top-Down", ImVec2(160, 44)))   { ed.NewTopDown(); finishProject(); }
        if (ImGui::Button("Coin Collector (full game)", ImVec2(-1, 36))) { ed.NewCoinCollector(); finishProject(); }
        if (ImGui::Button("Main Menu (UI)", ImVec2(-1, 36)))            { ed.NewMainMenu(); finishProject(); }
        if (ImGui::Button("Snake (full game)", ImVec2(-1, 36)))         { ed.NewSnake(); finishProject(); }
        ImGui::Spacing();
        if (ImGui::Button("Empty Scene", ImVec2(-1, 0))) { ed.NewScene(); finishProject(); }
        ImGui::EndPopup();
    }
}

void DrawAboutPopup() {
    if (g_openAbout) { ImGui::OpenPopup("About OkaySpace"); g_openAbout = false; }
    ImVec2 c = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(c, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("About OkaySpace", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.34f, 0.66f, 1.0f, 1.0f));
        ImGui::TextUnformatted("OkaySpace Game Engine");
        ImGui::PopStyleColor();
        ImGui::Text("Version %s", OKAY_ENGINE_VERSION);
        ImGui::Separator();
        ImGui::TextWrapped("A Unity-inspired C++ game engine. Build 2D/3D scenes, "
                           "script them, and export a standalone game with File > Build Game.");
        ImGui::Spacing();
        ImGui::TextDisabled("github.com/kingimann/OkaySpaceGameEngine");
        ImGui::Spacing();
        if (ImGui::Button("Close", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
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

// A short type tag for a GameObject, based on its most distinctive component.
static const char* ObjectKind(GameObject* go) {
    if (go->GetComponent<Camera>())         return "[Cam] ";
    if (go->GetComponent<MeshRenderer>())   return "[3D] ";
    if (go->GetComponent<TextRenderer>())   return "[Txt] ";
    if (go->GetComponent<ParticleSystem>()) return "[FX] ";
    if (go->GetComponent<Tilemap>())        return "[Tile] ";
    if (go->GetComponent<SpriteRenderer>()) return "[Spr] ";
    if (go->GetComponent<AudioSource>())    return "[Snd] ";
    return "";
}

static char g_hierFilter[96] = "";

void DrawHierarchy(EditorState& ed) {
    ImGui::Begin("Hierarchy");
    ImGui::TextDisabled("Scene: %s%s", ed.scene().Name().c_str(), ed.dirty ? " *" : "");
    ImGui::SameLine(ImGui::GetWindowWidth() - 70);
    ImGui::TextDisabled("%d obj", (int)ed.scene().Objects().size());
    if (ImGui::Button("+ Create")) ImGui::OpenPopup("HierCreate");
    if (ImGui::BeginPopup("HierCreate")) {
        if (ImGui::MenuItem("Empty"))  ed.Select(ed.CreateEmpty());
        if (ImGui::MenuItem("Sprite")) ed.Select(ed.CreateSprite());
        if (ImGui::MenuItem("Camera")) ed.Select(ed.CreateCamera());
        if (ImGui::MenuItem("Cube"))   ed.Select(ed.CreateCube());
        ImGui::Separator();
        if (ImGui::MenuItem("UI Button")) { GameObject* g = ed.CreateEmpty("Button"); g->AddComponent<UIButton>(); ed.Select(g); }
        if (ImGui::MenuItem("UI Text"))   { GameObject* g = ed.CreateEmpty("Text"); g->AddComponent<TextRenderer>()->screenSpace = true; ed.Select(g); }
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##hfilter", "search objects...", g_hierFilter, sizeof(g_hierFilter));
    ImGui::Separator();

    // When searching, show a flat list of every matching object.
    if (g_hierFilter[0] != '\0') {
        std::string needle = g_hierFilter;
        for (auto& n : needle) n = (char)std::tolower((unsigned char)n);
        for (const auto& up : ed.scene().Objects()) {
            GameObject* go = up.get();
            std::string low = go->name;
            for (auto& ch : low) ch = (char)std::tolower((unsigned char)ch);
            if (low.find(needle) == std::string::npos) continue;
            bool sel = (go == ed.selected());
            if (ImGui::Selectable((std::string(ObjectKind(go)) + go->name).c_str(), sel))
                ed.Select(go);
        }
        ImGui::End();
        return;
    }

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
            bool open = ImGui::TreeNodeEx(node, flags, "%s%s%s", ObjectKind(node),
                                          node->name.c_str(), node->active ? "" : "  (off)");
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

// A 9-way anchor dropdown shared by every UI widget inspector.
static void AnchorCombo(const char* id, okay::UIAnchor& anchor, EditorState& ed) {
    static const char* kAnchors[] = {
        "Top-Left", "Top", "Top-Right", "Left", "Center", "Right",
        "Bottom-Left", "Bottom", "Bottom-Right"};
    int ai = (int)anchor;
    if (ImGui::Combo(id, &ai, kAnchors, 9)) { anchor = (okay::UIAnchor)ai; ed.dirty = true; }
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

    // Header (Unity-style): an active toggle + the object name on a tinted bar,
    // then Tag, then a divider before the component list.
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.16f, 0.17f, 0.20f, 1.0f));
    ImGui::BeginChild("##objheader", ImVec2(0, 58), true);
    ImGui::Checkbox("##active", &go->active);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Active in scene");
    ImGui::SameLine();
    ImGui::TextUnformatted(ObjectKind(go));   // [Spr]/[Mesh]/... type chip
    ImGui::SameLine();
    char nameBuf[128];
    std::strncpy(nameBuf, go->name.c_str(), sizeof(nameBuf) - 1);
    nameBuf[sizeof(nameBuf) - 1] = '\0';
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputText("##name", nameBuf, sizeof(nameBuf))) { go->name = nameBuf; ed.dirty = true; }
    char tagBuf[64];
    std::strncpy(tagBuf, go->tag.c_str(), sizeof(tagBuf) - 1);
    tagBuf[sizeof(tagBuf) - 1] = '\0';
    ImGui::SetNextItemWidth(150);
    if (ImGui::InputText("Tag", tagBuf, sizeof(tagBuf))) { go->tag = tagBuf; ed.dirty = true; }
    ImGui::SameLine();
    if (ImGui::SmallButton("Save as Prefab")) {
        std::string p = go->name + ".okayprefab";
        if (SceneSerializer::SaveObjectToFile(*go, p)) ConsoleLog("Saved prefab " + p);
        else ConsoleLog("Prefab save failed");
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::Spacing();

    Component* toRemove = nullptr; // removed after drawing (avoids dangling use)

    if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
        Transform* t = go->transform;
        float pos[3] = {t->localPosition.x, t->localPosition.y, t->localPosition.z};
        if (ImGui::DragFloat3("Position", pos, 0.05f)) {
            t->localPosition = {pos[0], pos[1], pos[2]}; ed.dirty = true;
        }
        if (ImGui::IsItemActivated()) ed.PushUndo();
        Vec3& e = g_euler[go];
        float rot[3] = {e.x, e.y, e.z};
        if (ImGui::DragFloat3("Rotation", rot, 1.0f)) {
            e = {rot[0], rot[1], rot[2]};
            t->localRotation = Quat::Euler(e); ed.dirty = true;
        }
        if (ImGui::IsItemActivated()) ed.PushUndo();
        float scl[3] = {t->localScale.x, t->localScale.y, t->localScale.z};
        if (ImGui::DragFloat3("Scale", scl, 0.05f)) {
            t->localScale = {scl[0], scl[1], scl[2]}; ed.dirty = true;
        }
        if (ImGui::IsItemActivated()) ed.PushUndo();
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
            char tex[256];
            std::strncpy(tex, sr->texture.c_str(), sizeof(tex) - 1);
            tex[sizeof(tex) - 1] = '\0';
            if (ImGui::InputText("Texture##sprite", tex, sizeof(tex))) { sr->texture = tex; ed.dirty = true; }
            if (ImGui::DragInt("Sort Order##sprite", &sr->sortOrder, 0.1f, -1000, 1000)) ed.dirty = true;
            if (ImGui::Checkbox("Flip X##sprite", &sr->flipX)) ed.dirty = true;
            ImGui::SameLine();
            if (ImGui::Checkbox("Flip Y##sprite", &sr->flipY)) ed.dirty = true;
            ImGui::TextDisabled("image file (PNG/JPG); higher Sort Order draws on top");
            if (ImGui::SmallButton("Remove##sprite")) toRemove = sr;
        }
    }
    if (auto* mr = go->GetComponent<MeshRenderer>()) {
        if (ImGui::CollapsingHeader("Mesh Renderer", ImGuiTreeNodeFlags_DefaultOpen)) {
            float col[4] = {mr->color.r, mr->color.g, mr->color.b, mr->color.a};
            if (ImGui::ColorEdit4("Color##mesh", col)) {
                mr->color = {col[0], col[1], col[2], col[3]}; ed.dirty = true;
            }
            ImGui::Checkbox("Wireframe", &mr->wireframe);
            const char* shapes[] = {"Cube", "Pyramid", "Wedge", "Quad", "Plane", "Sphere",
                                    "Cylinder", "Cone", "Tube", "Torus", "Capsule", "Icosphere", "Grid"};
            const int kShapeCount = 13;
            int shapeIdx = -1;
            for (int i = 0; i < kShapeCount; ++i) if (mr->mesh.name == shapes[i]) shapeIdx = i;
            if (ImGui::Combo("Primitive", &shapeIdx, shapes, kShapeCount)) {
                mr->mesh = Mesh::FromName(shapes[shapeIdx]);
                mr->meshPath.clear();
                ed.dirty = true;
            }
            // Import a 3D model from an .OBJ file.
            char mp[256];
            std::strncpy(mp, mr->meshPath.c_str(), sizeof(mp) - 1);
            mp[sizeof(mp) - 1] = '\0';
            if (ImGui::InputText("OBJ File##mesh", mp, sizeof(mp))) { mr->meshPath = mp; ed.dirty = true; }
            ImGui::SameLine();
            if (ImGui::SmallButton("Load##obj") && mr->meshPath[0]) {
                bool ok = false;
                Mesh m = Mesh::LoadOBJ(mr->meshPath, &ok);
                if (ok && !m.vertices.empty()) { mr->mesh = m; ConsoleLog("Loaded " + mr->meshPath); }
                else ConsoleLog("OBJ load failed: " + mr->meshPath);
                ed.dirty = true;
            }
            ImGui::TextDisabled("%d verts, %d triangles",
                                (int)mr->mesh.vertices.size(), mr->mesh.TriangleCount());
            if (ImGui::SmallButton("Subdivide##mesh")) { mr->mesh.Subdivide(); ed.dirty = true; }
            ImGui::SameLine();
            if (ImGui::SmallButton("Smooth##mesh")) {     // subdivide + reproject to sphere
                Vec3 sz = mr->mesh.Size();
                mr->mesh.Subdivide();
                mr->mesh.ProjectToSphere(0.5f * std::fmax(sz.x, std::fmax(sz.y, sz.z)));
                ed.dirty = true;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Weld##mesh")) {       // merge coincident verts
                int n = mr->mesh.WeldVertices();
                ConsoleLog("Welded " + std::to_string(n) + " duplicate verts");
                ed.dirty = true;
            }
            if (ImGui::SmallButton("Recenter##mesh")) { mr->mesh.RecenterToOrigin(); ed.dirty = true; }
            ImGui::SameLine();
            if (ImGui::SmallButton("Ground##mesh")) { mr->mesh.GroundPivot(); ed.dirty = true; }
            ImGui::SameLine();
            if (ImGui::SmallButton("Fit 1u##mesh")) { mr->mesh.ScaleToFit(1.0f); ed.dirty = true; }
            ImGui::SameLine();
            if (ImGui::SmallButton("Remove##mesh")) toRemove = mr;
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
            if (ImGui::SmallButton("Remove##cam")) toRemove = cam;
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
            if (ImGui::SmallButton("Remove##rb")) toRemove = rb;
        }
    if (auto* bc = go->GetComponent<BoxCollider2D>()) {
        if (ImGui::CollapsingHeader("Box Collider 2D", ImGuiTreeNodeFlags_DefaultOpen)) {
            float sz[2] = {bc->size.x, bc->size.y};
            if (ImGui::DragFloat2("Size##bc", sz, 0.05f, 0.0f, 1000.0f)) { bc->size = {sz[0], sz[1]}; ed.dirty = true; }
            float off[2] = {bc->offset.x, bc->offset.y};
            if (ImGui::DragFloat2("Offset##bc", off, 0.05f)) { bc->offset = {off[0], off[1]}; ed.dirty = true; }
            ImGui::Checkbox("Trigger##bc", &bc->isTrigger);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(80); ImGui::DragInt("Layer##bc", &bc->layer, 0.1f, 0, 31);
            if (ImGui::SmallButton("Remove##bc")) toRemove = bc;
        }
    }
    if (auto* cc = go->GetComponent<CircleCollider2D>()) {
        if (ImGui::CollapsingHeader("Circle Collider 2D", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat("Radius##cc", &cc->radius, 0.05f, 0.0f, 1000.0f);
            float off[2] = {cc->offset.x, cc->offset.y};
            if (ImGui::DragFloat2("Offset##cc", off, 0.05f)) { cc->offset = {off[0], off[1]}; ed.dirty = true; }
            ImGui::Checkbox("Trigger##cc", &cc->isTrigger);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(80); ImGui::DragInt("Layer##cc", &cc->layer, 0.1f, 0, 31);
            if (ImGui::SmallButton("Remove##cc")) toRemove = cc;
        }
    }
    if (auto* sc = go->GetComponent<ScriptComponent>()) {
        if (ImGui::CollapsingHeader("Script", ImGuiTreeNodeFlags_DefaultOpen)) {
            const char* langs[] = {"okayscript", "lua", "csharp"};
            int li = sc->Language() == "lua" ? 1 : sc->Language() == "csharp" ? 2 : 0;
            if (ImGui::Combo("Language", &li, langs, 3)) sc->SetLanguage(langs[li]);
            if (sc->Path().empty()) ImGui::TextDisabled("inline script");
            else ImGui::TextDisabled("file: %s", sc->Path().c_str());
            ImGui::TextWrapped("Edit code in the Script Editor panel (View > Script Editor).");
            if (ImGui::SmallButton("Remove##script")) toRemove = sc;
        }
    }
    if (auto* vsc = go->GetComponent<VisualScriptComponent>()) {
        if (ImGui::CollapsingHeader("Visual Script", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::TextWrapped("Edit the node graph in the Script Editor panel.");
            if (ImGui::SmallButton("Remove##vs")) toRemove = vsc;
        }
    }
    if (auto* a = go->GetComponent<AudioSource>()) {
        if (ImGui::CollapsingHeader("Audio Source", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat("Volume", &a->volume, 0.01f, 0.0f, 1.0f);
            ImGui::Checkbox("Loop", &a->loop);
            ImGui::SameLine();
            ImGui::Checkbox("Play On Awake", &a->playOnAwake);
            if (ImGui::Button("Beep"))  a->clip = AudioClip::Sine(440.0f, 0.3f);
            ImGui::SameLine();
            if (ImGui::Button("Noise")) a->clip = AudioClip::Noise(0.3f);
            ImGui::SameLine();
            if (ImGui::Button("Play"))  a->Play();
            char cb[256];
            std::strncpy(cb, a->clipPath.c_str(), sizeof(cb) - 1);
            cb[sizeof(cb) - 1] = '\0';
            if (ImGui::InputText("WAV File##audio", cb, sizeof(cb))) { a->clipPath = cb; ed.dirty = true; }
            ImGui::TextDisabled("WAV path loads in the built game; %.2fs clip", a->clip.Duration());
            if (ImGui::SmallButton("Remove##audio")) toRemove = a;
        }
    }

    if (auto* mv = go->GetComponent<Mover>()) {
        if (ImGui::CollapsingHeader("Mover", ImGuiTreeNodeFlags_DefaultOpen)) {
            float v[3] = {mv->velocity.x, mv->velocity.y, mv->velocity.z};
            if (ImGui::DragFloat3("Velocity##mv", v, 0.05f)) { mv->velocity = {v[0], v[1], v[2]}; ed.dirty = true; }
            ImGui::TextDisabled("units / second");
            if (ImGui::SmallButton("Remove##mv")) toRemove = mv;
        }
    }
    if (auto* sp = go->GetComponent<Spinner>()) {
        if (ImGui::CollapsingHeader("Spinner", ImGuiTreeNodeFlags_DefaultOpen)) {
            float v[3] = {sp->angularVelocity.x, sp->angularVelocity.y, sp->angularVelocity.z};
            if (ImGui::DragFloat3("Angular Vel##sp", v, 0.5f)) { sp->angularVelocity = {v[0], v[1], v[2]}; ed.dirty = true; }
            ImGui::TextDisabled("degrees / second (X, Y, Z)");
            if (ImGui::SmallButton("Remove##sp")) toRemove = sp;
        }
    }
    if (auto* lt = go->GetComponent<Lifetime>()) {
        if (ImGui::CollapsingHeader("Lifetime", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::DragFloat("Seconds##lt", &lt->seconds, 0.05f, 0.0f, 10000.0f)) ed.dirty = true;
            ImGui::TextDisabled("destroys this object after N seconds of play");
            if (ImGui::SmallButton("Remove##lt")) toRemove = lt;
        }
    }
    if (auto* cf = go->GetComponent<CameraFollow>()) {
        if (ImGui::CollapsingHeader("Camera Follow", ImGuiTreeNodeFlags_DefaultOpen)) {
            char buf[128];
            std::strncpy(buf, cf->targetName.c_str(), sizeof(buf) - 1);
            buf[sizeof(buf) - 1] = '\0';
            if (ImGui::InputText("Target##cf", buf, sizeof(buf))) { cf->targetName = buf; ed.dirty = true; }
            float off[3] = {cf->offset.x, cf->offset.y, cf->offset.z};
            if (ImGui::DragFloat3("Offset##cf", off, 0.05f)) { cf->offset = {off[0], off[1], off[2]}; ed.dirty = true; }
            if (ImGui::DragFloat("Smoothing##cf", &cf->smoothing, 0.1f, 0.0f, 50.0f)) ed.dirty = true;
            ImGui::TextDisabled("follows the named GameObject (0 = instant snap)");
            if (ImGui::SmallButton("Remove##cf")) toRemove = cf;
        }
    }
    if (auto* tr = go->GetComponent<TextRenderer>()) {
        if (ImGui::CollapsingHeader("Text", ImGuiTreeNodeFlags_DefaultOpen)) {
            char buf[256];
            std::strncpy(buf, tr->text.c_str(), sizeof(buf) - 1);
            buf[sizeof(buf) - 1] = '\0';
            if (ImGui::InputText("Text##txt", buf, sizeof(buf))) { tr->text = buf; ed.dirty = true; }
            float col[4] = {tr->color.r, tr->color.g, tr->color.b, tr->color.a};
            if (ImGui::ColorEdit4("Color##txt", col)) { tr->color = {col[0], col[1], col[2], col[3]}; ed.dirty = true; }
            if (ImGui::DragFloat("Pixel Size##txt", &tr->pixelSize, 0.005f, 0.001f, 100.0f)) ed.dirty = true;
            if (ImGui::Checkbox("Screen Space##txt", &tr->screenSpace)) ed.dirty = true;
            if (tr->screenSpace) {
                float sp[2] = {tr->screenPos.x, tr->screenPos.y};
                if (ImGui::DragFloat2("Screen Pos##txt", sp, 1.0f)) { tr->screenPos = {sp[0], sp[1]}; ed.dirty = true; }
                AnchorCombo("Anchor##txt", tr->anchor, ed);
            }
            if (ImGui::Checkbox("Shadow##txt", &tr->shadow)) ed.dirty = true;
            if (tr->shadow) {
                float scol[4] = {tr->shadowColor.r, tr->shadowColor.g, tr->shadowColor.b, tr->shadowColor.a};
                if (ImGui::ColorEdit4("Shadow Color##txt", scol)) { tr->shadowColor = {scol[0], scol[1], scol[2], scol[3]}; ed.dirty = true; }
                float so[2] = {tr->shadowOffset.x, tr->shadowOffset.y};
                if (ImGui::DragFloat2("Shadow Offset##txt", so, 0.1f)) { tr->shadowOffset = {so[0], so[1]}; ed.dirty = true; }
            }
            ImGui::TextDisabled("8x8 bitmap font; renders in the built game");
            if (ImGui::SmallButton("Remove##txt")) toRemove = tr;
        }
    }
    if (auto* btn = go->GetComponent<UIButton>()) {
        if (ImGui::CollapsingHeader("UI Button", ImGuiTreeNodeFlags_DefaultOpen)) {
            char lb[128];
            std::strncpy(lb, btn->label.c_str(), sizeof(lb) - 1);
            lb[sizeof(lb) - 1] = '\0';
            if (ImGui::InputText("Label##uib", lb, sizeof(lb))) { btn->label = lb; ed.dirty = true; }
            float pos[2] = {btn->position.x, btn->position.y};
            if (ImGui::DragFloat2("Pos (px)##uib", pos, 1.0f)) { btn->position = {pos[0], pos[1]}; ed.dirty = true; }
            float sz[2] = {btn->size.x, btn->size.y};
            if (ImGui::DragFloat2("Size (px)##uib", sz, 1.0f, 1.0f, 4000.0f)) { btn->size = {sz[0], sz[1]}; ed.dirty = true; }
            float c[4] = {btn->color.r, btn->color.g, btn->color.b, btn->color.a};
            if (ImGui::ColorEdit4("Color##uib", c)) { btn->color = {c[0], c[1], c[2], c[3]}; ed.dirty = true; }
            float hc[4] = {btn->hoverColor.r, btn->hoverColor.g, btn->hoverColor.b, btn->hoverColor.a};
            if (ImGui::ColorEdit4("Hover##uib", hc)) { btn->hoverColor = {hc[0], hc[1], hc[2], hc[3]}; ed.dirty = true; }
            float prc[4] = {btn->pressedColor.r, btn->pressedColor.g, btn->pressedColor.b, btn->pressedColor.a};
            if (ImGui::ColorEdit4("Pressed##uib", prc)) { btn->pressedColor = {prc[0], prc[1], prc[2], prc[3]}; ed.dirty = true; }
            float dc[4] = {btn->disabledColor.r, btn->disabledColor.g, btn->disabledColor.b, btn->disabledColor.a};
            if (ImGui::ColorEdit4("Disabled##uib", dc)) { btn->disabledColor = {dc[0], dc[1], dc[2], dc[3]}; ed.dirty = true; }
            if (ImGui::Checkbox("Interactable##uib", &btn->interactable)) ed.dirty = true;
            ImGui::SameLine();
            if (ImGui::Checkbox("Focusable##uib", &btn->focusable)) ed.dirty = true;
            ImGui::TextDisabled("calls the script's on_click(); disabled buttons are greyed out");
            AnchorCombo("Anchor##uib", btn->anchor, ed);
            if (ImGui::SmallButton("Remove##uib")) toRemove = btn;
        }
    }
    if (auto* pn = go->GetComponent<UIPanel>()) {
        if (ImGui::CollapsingHeader("UI Panel", ImGuiTreeNodeFlags_DefaultOpen)) {
            float pos[2] = {pn->position.x, pn->position.y};
            if (ImGui::DragFloat2("Pos (px)##uip", pos, 1.0f)) { pn->position = {pos[0], pos[1]}; ed.dirty = true; }
            float sz[2] = {pn->size.x, pn->size.y};
            if (ImGui::DragFloat2("Size (px)##uip", sz, 1.0f, 0.0f, 8000.0f)) { pn->size = {sz[0], sz[1]}; ed.dirty = true; }
            float c[4] = {pn->color.r, pn->color.g, pn->color.b, pn->color.a};
            if (ImGui::ColorEdit4("Color##uip", c)) { pn->color = {c[0], c[1], c[2], c[3]}; ed.dirty = true; }
            AnchorCombo("Anchor##uip", pn->anchor, ed);
            if (ImGui::SmallButton("Remove##uip")) toRemove = pn;
        }
    }
    if (auto* pb = go->GetComponent<UIProgressBar>()) {
        if (ImGui::CollapsingHeader("UI Progress Bar", ImGuiTreeNodeFlags_DefaultOpen)) {
            float pos[2] = {pb->position.x, pb->position.y};
            if (ImGui::DragFloat2("Pos (px)##upb", pos, 1.0f)) { pb->position = {pos[0], pos[1]}; ed.dirty = true; }
            float sz[2] = {pb->size.x, pb->size.y};
            if (ImGui::DragFloat2("Size (px)##upb", sz, 1.0f, 1.0f, 8000.0f)) { pb->size = {sz[0], sz[1]}; ed.dirty = true; }
            if (ImGui::SliderFloat("Value##upb", &pb->value, 0.0f, 1.0f)) ed.dirty = true;
            float fc[4] = {pb->fill.r, pb->fill.g, pb->fill.b, pb->fill.a};
            if (ImGui::ColorEdit4("Fill##upb", fc)) { pb->fill = {fc[0], fc[1], fc[2], fc[3]}; ed.dirty = true; }
            float bc[4] = {pb->background.r, pb->background.g, pb->background.b, pb->background.a};
            if (ImGui::ColorEdit4("Background##upb", bc)) { pb->background = {bc[0], bc[1], bc[2], bc[3]}; ed.dirty = true; }
            ImGui::TextDisabled("script: set_progress(0..1)");
            AnchorCombo("Anchor##upb", pb->anchor, ed);
            if (ImGui::SmallButton("Remove##upb")) toRemove = pb;
        }
    }
    if (auto* im = go->GetComponent<UIImage>()) {
        if (ImGui::CollapsingHeader("UI Image", ImGuiTreeNodeFlags_DefaultOpen)) {
            float pos[2] = {im->position.x, im->position.y};
            if (ImGui::DragFloat2("Pos (px)##uim", pos, 1.0f)) { im->position = {pos[0], pos[1]}; ed.dirty = true; }
            float sz[2] = {im->size.x, im->size.y};
            if (ImGui::DragFloat2("Size (px)##uim", sz, 1.0f, 1.0f, 8000.0f)) { im->size = {sz[0], sz[1]}; ed.dirty = true; }
            char tx[256];
            std::strncpy(tx, im->texture.c_str(), sizeof(tx) - 1);
            tx[sizeof(tx) - 1] = '\0';
            if (ImGui::InputText("Texture##uim", tx, sizeof(tx))) { im->texture = tx; ed.dirty = true; }
            float c[4] = {im->color.r, im->color.g, im->color.b, im->color.a};
            if (ImGui::ColorEdit4("Tint##uim", c)) { im->color = {c[0], c[1], c[2], c[3]}; ed.dirty = true; }
            ImGui::TextDisabled("image path (PNG/JPG); empty = colored rect");
            if (ImGui::Checkbox("Nine-slice##uim", &im->nineSlice)) ed.dirty = true;
            if (im->nineSlice) {
                if (ImGui::DragFloat("Border (px)##uim", &im->border, 0.5f, 0.0f, 512.0f)) ed.dirty = true;
                ImGui::TextDisabled("corners stay fixed; edges/center stretch to size");
            }
            AnchorCombo("Anchor##uim", im->anchor, ed);
            if (ImGui::SmallButton("Remove##uim")) toRemove = im;
        }
    }
    if (auto* sl = go->GetComponent<UISlider>()) {
        if (ImGui::CollapsingHeader("UI Slider", ImGuiTreeNodeFlags_DefaultOpen)) {
            float pos[2] = {sl->position.x, sl->position.y};
            if (ImGui::DragFloat2("Pos (px)##usl", pos, 1.0f)) { sl->position = {pos[0], pos[1]}; ed.dirty = true; }
            float sz[2] = {sl->size.x, sl->size.y};
            if (ImGui::DragFloat2("Size (px)##usl", sz, 1.0f, 1.0f, 8000.0f)) { sl->size = {sz[0], sz[1]}; ed.dirty = true; }
            if (ImGui::DragFloat("Min##usl", &sl->minValue, 0.1f)) ed.dirty = true;
            if (ImGui::DragFloat("Max##usl", &sl->maxValue, 0.1f)) ed.dirty = true;
            if (ImGui::SliderFloat("Value##usl", &sl->value, sl->minValue, sl->maxValue)) ed.dirty = true;
            float fc[4] = {sl->fill.r, sl->fill.g, sl->fill.b, sl->fill.a};
            if (ImGui::ColorEdit4("Fill##usl", fc)) { sl->fill = {fc[0], fc[1], fc[2], fc[3]}; ed.dirty = true; }
            float kc[4] = {sl->knob.r, sl->knob.g, sl->knob.b, sl->knob.a};
            if (ImGui::ColorEdit4("Knob##usl", kc)) { sl->knob = {kc[0], kc[1], kc[2], kc[3]}; ed.dirty = true; }
            ImGui::TextDisabled("drag in the built game; calls script on_change()");
            AnchorCombo("Anchor##usl", sl->anchor, ed);
            if (ImGui::SmallButton("Remove##usl")) toRemove = sl;
        }
    }
    if (auto* tg = go->GetComponent<UIToggle>()) {
        if (ImGui::CollapsingHeader("UI Toggle", ImGuiTreeNodeFlags_DefaultOpen)) {
            char lb[128];
            std::strncpy(lb, tg->label.c_str(), sizeof(lb) - 1);
            lb[sizeof(lb) - 1] = '\0';
            if (ImGui::InputText("Label##utg", lb, sizeof(lb))) { tg->label = lb; ed.dirty = true; }
            if (ImGui::Checkbox("On##utg", &tg->on)) ed.dirty = true;
            float pos[2] = {tg->position.x, tg->position.y};
            if (ImGui::DragFloat2("Pos (px)##utg", pos, 1.0f)) { tg->position = {pos[0], pos[1]}; ed.dirty = true; }
            float sz[2] = {tg->size.x, tg->size.y};
            if (ImGui::DragFloat2("Size (px)##utg", sz, 1.0f, 1.0f, 4000.0f)) { tg->size = {sz[0], sz[1]}; ed.dirty = true; }
            float cc[4] = {tg->checkColor.r, tg->checkColor.g, tg->checkColor.b, tg->checkColor.a};
            if (ImGui::ColorEdit4("Check##utg", cc)) { tg->checkColor = {cc[0], cc[1], cc[2], cc[3]}; ed.dirty = true; }
            ImGui::TextDisabled("click in the built game; calls script on_toggle()");
            AnchorCombo("Anchor##utg", tg->anchor, ed);
            if (ImGui::SmallButton("Remove##utg")) toRemove = tg;
        }
    }
    if (auto* an = go->GetComponent<SpriteAnimator>()) {
        if (ImGui::CollapsingHeader("Sprite Animator", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::DragFloat("FPS##anim", &an->fps, 0.25f, 0.0f, 120.0f)) ed.dirty = true;
            if (ImGui::Checkbox("Loop##anim", &an->loop)) ed.dirty = true;
            ImGui::SameLine();
            if (ImGui::Checkbox("Playing##anim", &an->playing)) ed.dirty = true;
            ImGui::Spacing();
            ImGui::TextDisabled("Atlas mode (sprite sheet on the SpriteRenderer texture):");
            if (ImGui::DragInt("Columns##anim", &an->atlasColumns, 0.1f, 0, 64)) ed.dirty = true;
            if (ImGui::DragInt("Rows##anim", &an->atlasRows, 0.1f, 1, 64)) ed.dirty = true;
            if (ImGui::DragInt("Count 0=all##anim", &an->atlasCount, 0.1f, 0, 4096)) ed.dirty = true;
            if (an->atlasColumns > 0) ImGui::TextDisabled("Using atlas cells; frame list ignored.");
            ImGui::Spacing();
            ImGui::TextDisabled("Frames (image paths, used when Columns = 0):");
            int removeAt = -1;
            for (std::size_t i = 0; i < an->frames.size(); ++i) {
                char fb[256];
                std::strncpy(fb, an->frames[i].c_str(), sizeof(fb) - 1);
                fb[sizeof(fb) - 1] = '\0';
                ImGui::PushID((int)i);
                if (ImGui::InputText("##frame", fb, sizeof(fb))) { an->frames[i] = fb; ed.dirty = true; }
                ImGui::SameLine();
                if (ImGui::SmallButton("X")) removeAt = (int)i;
                ImGui::PopID();
            }
            if (removeAt >= 0) { an->frames.erase(an->frames.begin() + removeAt); ed.dirty = true; }
            if (ImGui::SmallButton("Add Frame")) { an->frames.push_back("frame.png"); ed.dirty = true; }
            ImGui::SameLine();
            if (ImGui::SmallButton("Remove##anim")) toRemove = an;
        }
    }

    // Apply a queued component removal (undoable).
    if (toRemove) { ed.PushUndo(); go->RemoveComponent(toRemove); ed.dirty = true; }

    ImGui::Spacing();
    ImGui::Separator();
    static char acFilter[64] = "";
    if (ImGui::Button("Add Component", ImVec2(-1, 0))) { acFilter[0] = '\0'; ImGui::OpenPopup("AddComponent"); }
    if (ImGui::BeginPopup("AddComponent")) {
        ImGui::SetNextItemWidth(220);
        ImGui::InputTextWithHint("##acfilter", "search components...", acFilter, sizeof(acFilter));
        ImGui::Separator();
        // Case-insensitive substring match against the search box.
        auto F = [&](const char* name) {
            if (!acFilter[0]) return true;
            std::string a = name, b = acFilter;
            for (auto& ch : a) ch = (char)std::tolower((unsigned char)ch);
            for (auto& ch : b) ch = (char)std::tolower((unsigned char)ch);
            return a.find(b) != std::string::npos;
        };
        // Category headers — shown when not searching, so the filtered list
        // stays a clean flat match.
        auto Hdr = [&](const char* s) { if (!acFilter[0]) { ImGui::Spacing(); ImGui::TextDisabled("%s", s); ImGui::Separator(); } };

        Hdr("Rendering");
        if (!go->GetComponent<SpriteRenderer>() && F("Sprite Renderer") && ImGui::Selectable("Sprite Renderer"))
            { go->AddComponent<SpriteRenderer>(); ed.dirty = true; }
        if (!go->GetComponent<MeshRenderer>() && F("Mesh Renderer (3D)") && ImGui::Selectable("Mesh Renderer (3D)"))
            { go->AddComponent<MeshRenderer>(); ed.view3D = true; ed.dirty = true; }
        if (!go->GetComponent<TextRenderer>() && F("Text") && ImGui::Selectable("Text"))
            { go->AddComponent<TextRenderer>(); ed.dirty = true; }
        if (!go->GetComponent<SpriteAnimator>() && F("Sprite Animator") && ImGui::Selectable("Sprite Animator"))
            { go->AddComponent<SpriteAnimator>(); ed.dirty = true; }
        if (!go->GetComponent<ParticleSystem>() && F("Particle System") && ImGui::Selectable("Particle System"))
            { go->AddComponent<ParticleSystem>(); ed.dirty = true; }

        Hdr("Physics");
        if (!go->GetComponent<Rigidbody2D>() && F("Rigidbody2D") && ImGui::Selectable("Rigidbody2D"))
            { go->AddComponent<Rigidbody2D>(); ed.dirty = true; }
        if (!go->GetComponent<BoxCollider2D>() && F("Box Collider 2D") && ImGui::Selectable("Box Collider 2D"))
            { go->AddComponent<BoxCollider2D>(); ed.dirty = true; }
        if (!go->GetComponent<CircleCollider2D>() && F("Circle Collider 2D") && ImGui::Selectable("Circle Collider 2D"))
            { go->AddComponent<CircleCollider2D>(); ed.dirty = true; }
        if (go->GetComponent<Tilemap>() && !go->GetComponent<TilemapCollider2D>() &&
            F("Tilemap Collider 2D") && ImGui::Selectable("Tilemap Collider 2D"))
            { go->AddComponent<TilemapCollider2D>(); ed.dirty = true; }

        Hdr("Camera");
        if (!go->GetComponent<Camera>() && F("Camera") && ImGui::Selectable("Camera"))
            { go->AddComponent<Camera>(); ed.dirty = true; }
        if (!go->GetComponent<CameraFollow>() && F("Camera Follow") && ImGui::Selectable("Camera Follow"))
            { go->AddComponent<CameraFollow>(); ed.dirty = true; }

        Hdr("Scripting");
        if (!go->GetComponent<ScriptComponent>() && F("Script (OkayScript)") && ImGui::Selectable("Script (OkayScript)"))
            { go->AddComponent<ScriptComponent>("okayscript"); ed.dirty = true; }
        if (!go->GetComponent<VisualScriptComponent>() && F("Visual Script") && ImGui::Selectable("Visual Script"))
            { go->AddComponent<VisualScriptComponent>(); ed.dirty = true; }

        Hdr("Audio");
        if (!go->GetComponent<AudioSource>() && F("Audio Source") && ImGui::Selectable("Audio Source"))
            { go->AddComponent<AudioSource>()->clip = AudioClip::Sine(440.0f, 0.3f); ed.dirty = true; }

        Hdr("Gameplay");
        if (!go->GetComponent<Mover>() && F("Mover") && ImGui::Selectable("Mover"))
            { go->AddComponent<Mover>(); ed.dirty = true; }
        if (!go->GetComponent<Spinner>() && F("Spinner") && ImGui::Selectable("Spinner"))
            { go->AddComponent<Spinner>(); ed.dirty = true; }
        if (!go->GetComponent<Lifetime>() && F("Lifetime") && ImGui::Selectable("Lifetime"))
            { go->AddComponent<Lifetime>(); ed.dirty = true; }

        Hdr("UI");
        if (!go->GetComponent<UIButton>() && F("UI Button") && ImGui::Selectable("UI Button"))
            { go->AddComponent<UIButton>(); ed.dirty = true; }
        if (!go->GetComponent<UIPanel>() && F("UI Panel") && ImGui::Selectable("UI Panel"))
            { go->AddComponent<UIPanel>(); ed.dirty = true; }
        if (!go->GetComponent<UIImage>() && F("UI Image") && ImGui::Selectable("UI Image"))
            { go->AddComponent<UIImage>(); ed.dirty = true; }
        if (!go->GetComponent<UIProgressBar>() && F("UI Progress Bar") && ImGui::Selectable("UI Progress Bar"))
            { go->AddComponent<UIProgressBar>(); ed.dirty = true; }
        if (!go->GetComponent<UISlider>() && F("UI Slider") && ImGui::Selectable("UI Slider"))
            { go->AddComponent<UISlider>(); ed.dirty = true; }
        if (!go->GetComponent<UIToggle>() && F("UI Toggle") && ImGui::Selectable("UI Toggle"))
            { go->AddComponent<UIToggle>(); ed.dirty = true; }
        ImGui::EndPopup();
    }
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.18f, 0.18f, 1.0f));
    if (ImGui::Button("Delete GameObject", ImVec2(-1, 0))) ed.DeleteSelected();
    ImGui::PopStyleColor();
    ImGui::End();
}

// Draw a string with the engine's 8x8 bitmap font into an ImGui draw list, so
// the editor viewport shows the same text the built game will (HUDs, labels).
void DrawBitmapText(ImDrawList* dl, const std::string& text, float ox, float oy,
                    float px, ImU32 col) {
    if (px < 1.0f) px = 1.0f;
    float cx = ox;
    for (char ch : text) {
        if (ch == '\n') { oy += (Font8x8::Height + 1) * px; cx = ox; continue; }
        for (int y = 0; y < Font8x8::Height; ++y)
            for (int x = 0; x < Font8x8::Width; ++x)
                if (Font8x8::Pixel(ch, x, y))
                    dl->AddRectFilled(ImVec2(cx + x * px, oy + y * px),
                                      ImVec2(cx + (x + 1) * px, oy + (y + 1) * px), col);
        cx += (Font8x8::Width + 1) * px;
    }
}

// The camera to frame the Game view through: the scene's main camera if one is
// active (set on Play), else the first Camera component found (edit mode).
static Camera* SceneCamera(Scene& s) {
    if (s.mainCamera) return s.mainCamera;
    for (const auto& go : s.Objects())
        if (go->active)
            if (auto* c = go->GetComponent<Camera>()) return c;
    return nullptr;
}

void DrawScene2D(EditorState& ed, ImDrawList* dl, ImVec2 canvasPos, ImVec2 canvasSize,
                 ImVec2 canvasEnd, bool hovered, ImGuiIO& io, bool gameView = false) {
    ImVec2 center(canvasPos.x + canvasSize.x * 0.5f, canvasPos.y + canvasSize.y * 0.5f);
    // The Game view frames through the scene's main camera (position + ortho
    // size); the Scene view uses the editor's free camera.
    Vec2  camPos = ed.cameraPos;
    float zoom   = ed.cameraZoom;
    if (gameView) {
        if (Camera* mc = SceneCamera(ed.scene())) {
            Vec3 cp = mc->gameObject->transform->Position();
            camPos = {cp.x, cp.y};
            zoom = mc->orthographicSize * 2.0f;        // viewport world-height
        }
    }
    float scale = canvasSize.y / zoom;
    auto worldToScreen = [&](const Vec3& w) {
        return ImVec2(center.x + (w.x - camPos.x) * scale,
                      center.y - (w.y - camPos.y) * scale);
    };
    auto screenToWorld = [&](const ImVec2& s) {
        return Vec2((s.x - center.x) / scale + camPos.x,
                    -(s.y - center.y) / scale + camPos.y);
    };

    if (!gameView && hovered && io.MouseWheel != 0.0f)
        ed.cameraZoom = Mathf::Clamp(ed.cameraZoom * (1.0f - io.MouseWheel * 0.1f), 2.0f, 200.0f);
    if (!gameView && hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
        ed.cameraPos.x -= io.MouseDelta.x / scale;
        ed.cameraPos.y += io.MouseDelta.y / scale;
    }

    dl->PushClipRect(canvasPos, canvasEnd, true);

    // Faint unit grid + world axes — Scene view only (the Game view is clean).
    if (!gameView) {
        float halfH = zoom * 0.5f;
        float halfW = halfH * (canvasSize.x / canvasSize.y);
        int x0 = (int)Mathf::Floor(camPos.x - halfW);
        int x1 = (int)Mathf::Ceil(camPos.x + halfW);
        int y0 = (int)Mathf::Floor(camPos.y - halfH);
        int y1 = (int)Mathf::Ceil(camPos.y + halfH);
        ImU32 grid = IM_COL32(40, 40, 52, 255);
        if (x1 - x0 < 200) // avoid drawing thousands of lines when zoomed way out
            for (int x = x0; x <= x1; ++x)
                dl->AddLine(worldToScreen({(float)x, (float)y0, 0}),
                            worldToScreen({(float)x, (float)y1, 0}), grid);
        if (y1 - y0 < 200)
            for (int y = y0; y <= y1; ++y)
                dl->AddLine(worldToScreen({(float)x0, (float)y, 0}),
                            worldToScreen({(float)x1, (float)y, 0}), grid);
        dl->AddLine(worldToScreen({-1000, 0, 0}), worldToScreen({1000, 0, 0}), IM_COL32(120, 60, 60, 255));
        dl->AddLine(worldToScreen({0, -1000, 0}), worldToScreen({0, 1000, 0}), IM_COL32(60, 120, 60, 255));
    }

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
        if (!gameView && go == ed.selected())
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

    // Text: world-space anchored to the object, screen-space pinned to the canvas.
    for (const auto& up : objs) {
        auto* tr = up->GetComponent<TextRenderer>();
        if (!tr || !up->active) continue;
        ImU32 col = ToColor(tr->color);
        ImU32 sh = ToColor(tr->shadowColor);
        if (tr->screenSpace) {
            Vec2 o = tr->ResolvedScreenPos(canvasSize.x, canvasSize.y);
            float bx = canvasPos.x + o.x, by = canvasPos.y + o.y;
            if (tr->shadow)
                DrawBitmapText(dl, tr->text, bx + tr->shadowOffset.x * tr->pixelSize,
                               by + tr->shadowOffset.y * tr->pixelSize, tr->pixelSize, sh);
            DrawBitmapText(dl, tr->text, bx, by, tr->pixelSize, col);
        } else {
            ImVec2 o = worldToScreen(up->transform->Position());
            float px = tr->pixelSize * scale;
            if (tr->shadow)
                DrawBitmapText(dl, tr->text, o.x + tr->shadowOffset.x * px,
                               o.y + tr->shadowOffset.y * px, px, sh);
            DrawBitmapText(dl, tr->text, o.x, o.y, px, col);
        }
    }

    // Publish the canvas size so anchored widgets resolve and (in Play) hit-test
    // against the same dimensions the preview uses.
    UICanvas::Set(canvasSize.x, canvasSize.y);

    // UI images (logos/icons): preview as a tinted rect with the path centered.
    for (const auto& up : objs) {
        auto* im = up->GetComponent<UIImage>();
        if (!im || !up->active) continue;
        Vec2 o = ResolveAnchor(im->anchor, im->position, im->size, canvasSize.x, canvasSize.y);
        ImVec2 a(canvasPos.x + o.x, canvasPos.y + o.y);
        ImVec2 b(a.x + im->size.x, a.y + im->size.y);
        dl->AddRectFilled(a, b, ToColor(im->color), 3.0f);
        dl->AddRect(a, b, IM_COL32(255, 255, 255, 90), 3.0f);
        if (!im->texture.empty())
            DrawBitmapText(dl, im->texture, a.x + 4, a.y + 4, 1.0f, IM_COL32(255, 255, 255, 160));
    }

    // UI panels (backgrounds) and progress bars: screen-space, canvas-relative.
    for (const auto& up : objs) {
        auto* pn = up->GetComponent<UIPanel>();
        if (!pn || !up->active) continue;
        Vec2 o = ResolveAnchor(pn->anchor, pn->position, pn->size, canvasSize.x, canvasSize.y);
        ImVec2 a(canvasPos.x + o.x, canvasPos.y + o.y);
        dl->AddRectFilled(a, ImVec2(a.x + pn->size.x, a.y + pn->size.y), ToColor(pn->color), 4.0f);
    }
    for (const auto& up : objs) {
        auto* pb = up->GetComponent<UIProgressBar>();
        if (!pb || !up->active) continue;
        Vec2 o = ResolveAnchor(pb->anchor, pb->position, pb->size, canvasSize.x, canvasSize.y);
        ImVec2 a(canvasPos.x + o.x, canvasPos.y + o.y);
        dl->AddRectFilled(a, ImVec2(a.x + pb->size.x, a.y + pb->size.y), ToColor(pb->background), 3.0f);
        dl->AddRectFilled(a, ImVec2(a.x + pb->size.x * pb->Fraction(), a.y + pb->size.y),
                          ToColor(pb->fill), 3.0f);
    }

    // UI sliders: track + fill + knob.
    for (const auto& up : objs) {
        auto* sl = up->GetComponent<UISlider>();
        if (!sl || !up->active) continue;
        Vec2 o = ResolveAnchor(sl->anchor, sl->position, sl->size, canvasSize.x, canvasSize.y);
        ImVec2 a(canvasPos.x + o.x, canvasPos.y + o.y);
        dl->AddRectFilled(a, ImVec2(a.x + sl->size.x, a.y + sl->size.y), ToColor(sl->background), 3.0f);
        dl->AddRectFilled(a, ImVec2(a.x + sl->size.x * sl->Fraction(), a.y + sl->size.y),
                          ToColor(sl->fill), 3.0f);
        float kx = a.x + sl->size.x * sl->Fraction(), kw = sl->size.y * 0.6f;
        dl->AddRectFilled(ImVec2(kx - kw * 0.5f, a.y - 2), ImVec2(kx + kw * 0.5f, a.y + sl->size.y + 2),
                          ToColor(sl->knob), 2.0f);
    }
    // UI toggles: box (+ inset check when on) and a label.
    for (const auto& up : objs) {
        auto* tg = up->GetComponent<UIToggle>();
        if (!tg || !up->active) continue;
        Vec2 o = ResolveAnchor(tg->anchor, tg->position, tg->size, canvasSize.x, canvasSize.y);
        ImVec2 a(canvasPos.x + o.x, canvasPos.y + o.y);
        ImVec2 b(a.x + tg->size.x, a.y + tg->size.y);
        dl->AddRectFilled(a, b, ToColor(tg->boxColor), 3.0f);
        if (tg->on) {
            float pad = tg->size.x * 0.22f;
            dl->AddRectFilled(ImVec2(a.x + pad, a.y + pad), ImVec2(b.x - pad, b.y - pad),
                              ToColor(tg->checkColor), 2.0f);
        }
        float px = 2.0f;
        DrawBitmapText(dl, tg->label, b.x + 8.0f,
                       a.y + (tg->size.y - Font8x8::Height * px) * 0.5f, px, ToColor(tg->textColor));
    }

    // UI buttons: screen-space, pinned to the canvas (pixels from its top-left).
    for (const auto& up : objs) {
        auto* btn = up->GetComponent<UIButton>();
        if (!btn || !up->active) continue;
        Vec2 o = ResolveAnchor(btn->anchor, btn->position, btn->size, canvasSize.x, canvasSize.y);
        ImVec2 a(canvasPos.x + o.x, canvasPos.y + o.y);
        ImVec2 b(a.x + btn->size.x, a.y + btn->size.y);
        dl->AddRectFilled(a, b, ToColor(btn->CurrentColor()), 4.0f);
        float px = 2.0f;
        float tw = btn->label.size() * (Font8x8::Width + 1) * px;
        DrawBitmapText(dl, btn->label, a.x + (btn->size.x - tw) * 0.5f,
                       a.y + (btn->size.y - Font8x8::Height * px) * 0.5f, px,
                       ToColor(btn->textColor));
        if (!gameView && up.get() == ed.selected())
            dl->AddRect(ImVec2(a.x - 1, a.y - 1), ImVec2(b.x + 1, b.y + 1),
                        IM_COL32(255, 200, 0, 255), 4.0f, 0, 2.0f);
    }
    // Transform gizmo at the selection, reflecting the active tool (Move/Rotate/Scale).
    if (!gameView && ed.selected()) {
        ImVec2 g = worldToScreen(ed.selected()->transform->Position());
        if (g_tool == Tool::Move) {
            dl->AddLine(g, ImVec2(g.x + 36, g.y), IM_COL32(230, 70, 70, 255), 2.0f);   // +X red
            dl->AddTriangleFilled(ImVec2(g.x + 36, g.y - 4), ImVec2(g.x + 36, g.y + 4),
                                  ImVec2(g.x + 44, g.y), IM_COL32(230, 70, 70, 255));
            dl->AddLine(g, ImVec2(g.x, g.y - 36), IM_COL32(80, 210, 90, 255), 2.0f);   // +Y green
            dl->AddTriangleFilled(ImVec2(g.x - 4, g.y - 36), ImVec2(g.x + 4, g.y - 36),
                                  ImVec2(g.x, g.y - 44), IM_COL32(80, 210, 90, 255));
        } else if (g_tool == Tool::Rotate) {
            dl->AddCircle(g, 34.0f, IM_COL32(90, 170, 240, 255), 32, 2.0f);
        } else { // Scale — axis lines capped with square handles
            dl->AddLine(g, ImVec2(g.x + 36, g.y), IM_COL32(230, 70, 70, 255), 2.0f);
            dl->AddRectFilled(ImVec2(g.x + 32, g.y - 4), ImVec2(g.x + 40, g.y + 4), IM_COL32(230, 70, 70, 255));
            dl->AddLine(g, ImVec2(g.x, g.y - 36), IM_COL32(80, 210, 90, 255), 2.0f);
            dl->AddRectFilled(ImVec2(g.x - 4, g.y - 40), ImVec2(g.x + 4, g.y - 32), IM_COL32(80, 210, 90, 255));
        }
    }

    dl->PopClipRect();

    if (gameView) return;   // the Game view is non-interactive

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
        Transform* t = ed.selected()->transform;
        if (g_tool == Tool::Move) {
            Vec3& lp = t->localPosition;
            lp.x += io.MouseDelta.x / scale;
            lp.y -= io.MouseDelta.y / scale;
            if (g_snap && g_snapSize > 0.0f) {
                lp.x = Mathf::Round(lp.x / g_snapSize) * g_snapSize;
                lp.y = Mathf::Round(lp.y / g_snapSize) * g_snapSize;
            }
        } else if (g_tool == Tool::Rotate) {
            t->Rotate({0, 0, -io.MouseDelta.x * 0.5f});      // drag X to spin about Z
        } else { // Scale (uniform; horizontal drag grows/shrinks)
            float k = 1.0f + io.MouseDelta.x * 0.01f;
            t->localScale = t->localScale * k;
        }
        ed.dirty = true;
    }
}

void DrawScene3D(EditorState& ed, ImDrawList* dl, ImVec2 canvasPos, ImVec2 canvasSize,
                 ImVec2 canvasEnd, bool hovered, ImGuiIO& io, bool gameView = false) {
    ImVec2 center(canvasPos.x + canvasSize.x * 0.5f, canvasPos.y + canvasSize.y * 0.5f);

    // Orbit controls (Scene view only).
    if (!gameView && hovered && io.MouseWheel != 0.0f)
        ed.camDist = Mathf::Clamp(ed.camDist * (1.0f - io.MouseWheel * 0.1f), 2.0f, 400.0f);
    if (!gameView && hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Left) && !ed.selected()) {
        ed.camYaw   -= io.MouseDelta.x * 0.4f;
        ed.camPitch = Mathf::Clamp(ed.camPitch + io.MouseDelta.y * 0.4f, -85.0f, 85.0f);
    }
    if (!gameView && hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
        ed.camYaw   -= io.MouseDelta.x * 0.4f;
        ed.camPitch = Mathf::Clamp(ed.camPitch + io.MouseDelta.y * 0.4f, -85.0f, 85.0f);
    }

    float yawR = ed.camYaw * Mathf::Deg2Rad, pitchR = ed.camPitch * Mathf::Deg2Rad;
    Vec3 dir{Mathf::Cos(pitchR) * Mathf::Sin(yawR), Mathf::Sin(pitchR),
             Mathf::Cos(pitchR) * Mathf::Cos(yawR)};
    Vec3 eye = ed.camTarget + dir * ed.camDist;
    Mat4 view = Mat4::LookAt(eye, ed.camTarget, Vec3::Up);
    Mat4 proj = Mat4::Perspective(50.0f, canvasSize.x / canvasSize.y, 0.1f, 2000.0f);
    // The Game view renders through the scene's main camera instead.
    if (gameView) {
        if (Camera* mc = SceneCamera(ed.scene())) {
            eye = mc->gameObject->transform->Position();
            view = mc->ViewMatrix();
            proj = mc->ProjectionMatrix(canvasSize.x / canvasSize.y);
        }
    }
    Mat4 vp = proj * view;

    auto toScreen = [&](const Vec4& c, ImVec2& out) -> bool {
        if (c.w <= 0.05f) return false;
        out = ImVec2(center.x + (c.x / c.w) * canvasSize.x * 0.5f,
                     center.y - (c.y / c.w) * canvasSize.y * 0.5f);
        return true;
    };

    dl->PushClipRect(canvasPos, canvasEnd, true);

    // Ground grid on the XZ plane (Scene view only; the Game view is clean).
    if (!gameView)
    for (int i = -10; i <= 10; ++i) {
        ImVec2 a, b;
        if (toScreen(vp * Vec4{Vec3{(float)i, 0, -10}, 1}, a) &&
            toScreen(vp * Vec4{Vec3{(float)i, 0, 10}, 1}, b))
            dl->AddLine(a, b, IM_COL32(50, 50, 64, 255));
        if (toScreen(vp * Vec4{Vec3{-10, 0, (float)i}, 1}, a) &&
            toScreen(vp * Vec4{Vec3{10, 0, (float)i}, 1}, b))
            dl->AddLine(a, b, IM_COL32(50, 50, 64, 255));
    }

    const auto& objs = ed.scene().Objects();

    // Solid meshes (wireframe == false): flat-shaded, back-face-culled, and
    // depth-sorted across all objects (painter's algorithm) so they occlude
    // correctly — mirrors the player's 3D renderer, including the global light.
    struct EdTri { float depth; ImVec2 p[3]; ImU32 col; };
    std::vector<EdTri> solid;
    for (const auto& up : objs) {
        GameObject* go = up.get();
        auto* mr = go->GetComponent<MeshRenderer>();
        if (!mr || !go->active || mr->wireframe) continue;
        Mat4 model = go->transform->LocalToWorldMatrix();
        const auto& v = mr->mesh.vertices;
        const auto& t = mr->mesh.triangles;
        for (size_t i = 0; i + 2 < t.size(); i += 3) {
            Vec3 wp[3];
            for (int k = 0; k < 3; ++k) wp[k] = model.MultiplyPoint(v[t[i + k]]);
            Vec3 normal = Vec3::Cross(wp[1] - wp[0], wp[2] - wp[0]).Normalized();
            Vec3 centroid = (wp[0] + wp[1] + wp[2]) * (1.0f / 3.0f);
            // Two-sided: draw every face, normal toward the eye (painter's sort
            // keeps occlusion right), so meshes never show holes when orbiting.
            float facing = Vec3::Dot(normal, eye - centroid);
            if (facing < 0.0f) normal = normal * -1.0f;
            EdTri tri; bool ok = true;
            for (int k = 0; k < 3; ++k)
                if (!toScreen(vp * Vec4{wp[k], 1}, tri.p[k])) { ok = false; break; }
            if (!ok) continue;
            float shade = SceneLight::Shade(normal);
            Color c = mr->color;
            tri.col = (go == ed.selected())
                ? IM_COL32((int)(255 * shade), (int)(200 * shade), 0, 255)
                : IM_COL32((int)(c.r * 255 * shade), (int)(c.g * 255 * shade),
                           (int)(c.b * 255 * shade), 255);
            tri.depth = (centroid - eye).SqrMagnitude();
            solid.push_back(tri);
        }
    }
    std::sort(solid.begin(), solid.end(),
              [](const EdTri& a, const EdTri& b) { return a.depth > b.depth; });
    for (const auto& tr : solid)
        dl->AddTriangleFilled(tr.p[0], tr.p[1], tr.p[2], tr.col);

    // Wireframe meshes (wireframe == true): edges only, drawn over the solids.
    for (const auto& up : objs) {
        GameObject* go = up.get();
        auto* mr = go->GetComponent<MeshRenderer>();
        if (!mr || !go->active || !mr->wireframe) continue;
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

    if (gameView) return;   // the Game view is non-interactive

    // Click to select: pick the nearest mesh whose projected bounding box
    // contains the cursor (so clicking anywhere on a model selects it).
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        GameObject* hit = nullptr;
        float bestDepth = 1e30f;
        for (const auto& up : objs) {
            GameObject* go = up.get();
            auto* mr = go->GetComponent<MeshRenderer>();
            if (!mr || !go->active) continue;
            Mat4 model = go->transform->LocalToWorldMatrix();
            Vec3 lo, hi; mr->mesh.Bounds(lo, hi);
            float minx = 1e30f, miny = 1e30f, maxx = -1e30f, maxy = -1e30f;
            bool any = false;
            for (int ci = 0; ci < 8; ++ci) {
                Vec3 corner{(ci & 1) ? hi.x : lo.x, (ci & 2) ? hi.y : lo.y, (ci & 4) ? hi.z : lo.z};
                ImVec2 sp;
                if (!toScreen(vp * Vec4{model.MultiplyPoint(corner), 1}, sp)) continue;
                any = true;
                minx = Mathf::Min(minx, sp.x); maxx = Mathf::Max(maxx, sp.x);
                miny = Mathf::Min(miny, sp.y); maxy = Mathf::Max(maxy, sp.y);
            }
            if (!any) continue;
            if (io.MousePos.x >= minx && io.MousePos.x <= maxx &&
                io.MousePos.y >= miny && io.MousePos.y <= maxy) {
                float depth = (go->transform->Position() - eye).SqrMagnitude();
                if (depth < bestDepth) { bestDepth = depth; hit = go; }
            }
        }
        if (hit) ed.Select(hit);
    }

    // Drag the selected object with the active tool. Move follows the camera's
    // screen plane (drag-right moves right on screen, drag-up moves up), so it
    // feels natural from any orbit angle. Rotate spins about Y; Scale is uniform.
    if (!ed.isPlaying() && ed.selected() && hovered &&
        ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        Transform* t = ed.selected()->transform;
        float s = ed.camDist / canvasSize.y;        // screen px -> world units
        if (g_tool == Tool::Move) {
            Vec3 fwd = (ed.camTarget - eye).Normalized();          // camera forward
            Vec3 right = Vec3::Cross(fwd, Vec3::Up).Normalized();  // screen right
            Vec3 up = Vec3::Cross(right, fwd).Normalized();        // screen up
            t->localPosition += right * (io.MouseDelta.x * s) + up * (-io.MouseDelta.y * s);
        } else if (g_tool == Tool::Rotate) {
            t->Rotate({0, io.MouseDelta.x * 0.5f, 0});
        } else {
            float k = 1.0f + io.MouseDelta.x * 0.01f;
            t->localScale = t->localScale * k;
        }
        ed.dirty = true;
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
    // Transform tools (W/E/R), highlighting the active one.
    ImGui::SameLine(); ImGui::TextDisabled("|"); ImGui::SameLine();
    auto toolBtn = [&](const char* lbl, Tool t) {
        bool active = g_tool == t;
        if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.45f, 0.75f, 1.0f));
        if (ImGui::Button(lbl)) g_tool = t;
        if (active) ImGui::PopStyleColor();
        ImGui::SameLine();
    };
    toolBtn("Move", Tool::Move);
    toolBtn("Rotate", Tool::Rotate);
    toolBtn("Scale", Tool::Scale);
    // Keyboard shortcuts W/E/R when the Scene window is focused (and not typing).
    if (ImGui::IsWindowFocused() && !ImGui::GetIO().WantTextInput) {
        if (ImGui::IsKeyPressed(ImGuiKey_W, false)) g_tool = Tool::Move;
        if (ImGui::IsKeyPressed(ImGuiKey_E, false)) g_tool = Tool::Rotate;
        if (ImGui::IsKeyPressed(ImGuiKey_R, false)) g_tool = Tool::Scale;
    }
    ImGui::Checkbox("Snap", &g_snap);
    if (g_snap) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(70);
        ImGui::DragFloat("##snap", &g_snapSize, 0.05f, 0.05f, 10.0f);
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

// Unity-style "Game" view: renders the scene through its main camera with no
// editor chrome (grid, gizmos, selection) — what the built game shows. 2D or 3D
// follows the camera's projection. Read-only; press Play to make it live.
void DrawGameView(EditorState& ed) {
    if (!ImGui::Begin("Game")) { ImGui::End(); return; }

    Camera* mc = SceneCamera(ed.scene());
    bool persp = mc && mc->projection == Camera::Projection::Perspective;

    // Aspect-ratio selector (Unity's Game-view "Free / 16:9 / ..." dropdown).
    static int s_aspect = 0;
    static const char* kAspectNames[] = {"Free", "16:9", "9:16", "4:3", "1:1"};
    static const float kAspectVals[]  = {0.0f, 16.0f / 9, 9.0f / 16, 4.0f / 3, 1.0f};
    ImGui::SetNextItemWidth(110);
    ImGui::Combo("Aspect", &s_aspect, kAspectNames, IM_ARRAYSIZE(kAspectNames));
    ImGui::SameLine();
    ImGui::TextDisabled(ed.isPlaying() ? "live" : "press Play to run");

    ImVec2 avail = ImGui::GetContentRegionAvail();
    if (avail.x < 50) avail.x = 50;
    if (avail.y < 50) avail.y = 50;
    ImVec2 origin = ImGui::GetCursorScreenPos();

    // Letterbox a fixed-aspect rect centered in the available region.
    ImVec2 canvasSize = avail, canvasPos = origin;
    if (kAspectVals[s_aspect] > 0.0f) {
        float target = kAspectVals[s_aspect];
        float boxW = avail.x, boxH = avail.x / target;
        if (boxH > avail.y) { boxH = avail.y; boxW = avail.y * target; }
        canvasSize = {boxW, boxH};
        canvasPos = {origin.x + (avail.x - boxW) * 0.5f, origin.y + (avail.y - boxH) * 0.5f};
    }
    ImVec2 canvasEnd(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    // Dark letterbox bars over the whole region, then the camera background.
    dl->AddRectFilled(origin, ImVec2(origin.x + avail.x, origin.y + avail.y), IM_COL32(8, 8, 10, 255));
    Color bg = mc ? mc->backgroundColor : Color::Black;
    dl->AddRectFilled(canvasPos, canvasEnd,
                      IM_COL32((int)(bg.r * 255), (int)(bg.g * 255), (int)(bg.b * 255), 255));

    ImGui::InvisibleButton("gamecanvas", avail);
    ImGuiIO& io = ImGui::GetIO();
    // Publish the canvas size so anchored UI resolves to this view.
    UICanvas::Set(canvasSize.x, canvasSize.y);

    if (!mc) {
        dl->AddText(ImVec2(canvasPos.x + 10, canvasPos.y + 10),
                    IM_COL32(200, 120, 120, 255), "No main Camera in the scene.");
    } else if (persp) {
        DrawScene3D(ed, dl, canvasPos, canvasSize, canvasEnd, false, io, /*gameView*/ true);
    } else {
        DrawScene2D(ed, dl, canvasPos, canvasSize, canvasEnd, false, io, /*gameView*/ true);
    }

    const char* tag = ed.isPlaying() ? "PLAYING" : "Game (press Play)";
    dl->AddText(ImVec2(canvasPos.x + 8, canvasPos.y + 6),
                ed.isPlaying() ? IM_COL32(120, 230, 140, 255) : IM_COL32(180, 180, 190, 255), tag);
    ImGui::End();
}

} // namespace

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i)
        if (std::string(argv[i]) == "--selftest") return RunSelfTest();

    SDL_SetMainReady(); // we manage the entry point (SDL_MAIN_HANDLED)
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_AUDIO) != 0) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << "\n";
        return 1;
    }

    // Open a mono float audio device (queue mode); audio is optional.
    SDL_AudioSpec want{}, have{};
    want.freq = 44100; want.format = AUDIO_F32SYS; want.channels = 1; want.samples = 1024;
    SDL_AudioDeviceID audioDev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (audioDev) SDL_PauseAudioDevice(audioDev, 0);

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

        // Pump audio while playing (mono float, queued).
        if (audioDev) {
            if (ed.isPlaying()) {
                int n = (int)(dt * 44100.0f);
                if (n > 8192) n = 8192;
                if (n > 0) {
                    std::vector<float> ab(n);
                    AudioMixer::Render(ed.scene(), ab.data(), n);
                    SDL_QueueAudio(audioDev, ab.data(), (Uint32)(n * sizeof(float)));
                }
            } else {
                SDL_ClearQueuedAudio(audioDev);
            }
        }

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
        DrawFileDialogs(ed);
        DrawUpdatePopup();
        DrawAboutPopup();
        if (g_showHierarchy) DrawHierarchy(ed);
        DrawViewport(ed);   // the "Scene" panel (always shown)
        if (g_showGame)      DrawGameView(ed);
        if (g_showInspector) DrawInspector(ed);
        if (g_showConsole)   DrawConsole();
        if (g_showProject)   DrawProject(ed);
        if (g_showServices)  DrawServices(ed);
        if (g_showScriptEditor) DrawScriptEditor(ed);
        if (g_showStats)     DrawStats(ed);

        ImGui::Render();
        SDL_RenderSetScale(renderer, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
        SDL_SetRenderDrawColor(renderer, 30, 30, 34, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
        SDL_RenderPresent(renderer);

        if (updater::pendingQuit) running = false; // auto-update relaunched
    }

    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    if (audioDev) SDL_CloseAudioDevice(audioDev);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
#endif // OKAY_EDITOR_HEADLESS
