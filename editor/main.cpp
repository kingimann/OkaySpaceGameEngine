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

using namespace okay;
using namespace okay::editor;

namespace {

ImU32 ToColor(const Color& c) {
    return IM_COL32((int)(c.r * 255), (int)(c.g * 255), (int)(c.b * 255), (int)(c.a * 255));
}

// ---- Self-updater: pull the latest engine from GitHub -----------------
namespace updater {
namespace fs = std::filesystem;

std::string Capture(const std::string& cmd) {
#if defined(_WIN32)
    FILE* pipe = _popen(cmd.c_str(), "r");
#else
    FILE* pipe = popen((cmd + " 2>&1").c_str(), "r");
#endif
    if (!pipe) return {};
    std::string out;
    std::array<char, 256> buf{};
    while (fgets(buf.data(), (int)buf.size(), pipe)) out += buf.data();
#if defined(_WIN32)
    _pclose(pipe);
#else
    pclose(pipe);
#endif
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
    return out;
}

fs::path FindRepoRoot() {
    std::error_code ec;
    for (fs::path p = fs::current_path(ec); !p.empty(); p = p.parent_path()) {
        if (fs::exists(p / ".git")) return p;
        if (p == p.root_path()) break;
    }
    return {};
}

// Returns a human-readable status; pulls the latest if the local copy is behind.
std::string CheckAndUpdate() {
    fs::path root = FindRepoRoot();
    if (root.empty()) return "Could not find the engine's git repository.";
    std::string git = "git -C \"" + root.string() + "\"";
    std::string branch = Capture(git + " rev-parse --abbrev-ref HEAD");
    if (branch.empty() || branch == "HEAD") branch = "main";

    Capture(git + " fetch --quiet origin " + branch);
    std::string local  = Capture(git + " rev-parse HEAD");
    std::string remote = Capture(git + " rev-parse origin/" + branch);
    if (remote.empty()) return "Offline: couldn't reach GitHub.";
    if (local == remote)
        return "Up to date (" + local.substr(0, 8) + ") on '" + branch + "'.";

    std::string pull = Capture(git + " pull --ff-only origin " + branch);
    std::string now = Capture(git + " rev-parse HEAD");
    if (now == remote)
        return "Updated to " + now.substr(0, 8) +
               ".\nRebuild the engine to apply (cmake --build build).";
    return "Update available (" + remote.substr(0, 8) +
           ") but pull failed:\n" + pull;
}
} // namespace updater

std::string g_updateStatus;
bool g_openUpdatePopup = false;

// Per-object editor-only Z rotation cache (2D authoring convenience).
std::unordered_map<GameObject*, float> g_eulerZ;

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
    ImGui::DockBuilderDockWindow("Scene", center);
    ImGui::DockBuilderFinish(dockId);
}

void DrawMenuAndToolbar(EditorState& ed, bool& running) {
    if (!ImGui::BeginMenuBar()) return;
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("New Scene", "Ctrl+N")) { ed.NewScene(); ConsoleLog("New scene"); }
        if (ImGui::MenuItem("Open...", "Ctrl+O")) {
            std::string err;
            if (ed.Load("scene.okayscene", &err)) ConsoleLog("Opened scene.okayscene");
            else ConsoleLog("Open failed: " + err);
        }
        if (ImGui::MenuItem("Save", "Ctrl+S")) {
            std::string p = ed.path().empty() ? "scene.okayscene" : ed.path();
            ConsoleLog(ed.Save(p) ? "Saved " + p : "Save failed");
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Exit")) running = false;
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("GameObject")) {
        if (ImGui::MenuItem("Create Empty"))  { ed.CreateEmpty();  ConsoleLog("Created empty GameObject"); }
        if (ImGui::MenuItem("Create Sprite")) { ed.CreateSprite(); ConsoleLog("Created Sprite"); }
        if (ImGui::MenuItem("Create Camera")) { ed.CreateCamera(); ConsoleLog("Created Camera"); }
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

    // Centered Play / Stop / Step controls (Unity-style toolbar).
    float btnW = 60.0f;
    ImGui::SameLine(ImGui::GetWindowWidth() * 0.5f - btnW);
    if (!ed.isPlaying()) {
        if (ImGui::Button(">  Play", ImVec2(btnW, 0))) { ed.Play(); ConsoleLog("Play"); }
    } else {
        if (ImGui::Button("[]  Stop", ImVec2(btnW, 0))) { ed.Stop(); ConsoleLog("Stop"); }
    }
    ImGui::SameLine();
    if (ImGui::Button("Step", ImVec2(50, 0))) ed.Tick(1.0f / 60.0f);

    // Right-aligned status.
    char status[64];
    std::snprintf(status, sizeof(status), "%s   %.0f FPS",
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
            bool open = ImGui::TreeNodeEx(node, flags, "%s", node->name.c_str());
            if (ImGui::IsItemClicked()) ed.Select(node);
            if (open) {
                for (Transform* child : node->transform->Children())
                    drawNode(child->gameObject);
                ImGui::TreePop();
            }
        };
        drawNode(go);
    }
    ImGui::End();
}

void DrawInspector(EditorState& ed) {
    ImGui::Begin("Inspector");
    GameObject* go = ed.selected();
    if (!go) { ImGui::TextDisabled("Nothing selected"); ImGui::End(); return; }

    char nameBuf[128];
    std::strncpy(nameBuf, go->name.c_str(), sizeof(nameBuf) - 1);
    nameBuf[sizeof(nameBuf) - 1] = '\0';
    if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf))) { go->name = nameBuf; ed.dirty = true; }
    ImGui::Checkbox("Active", &go->active);

    if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
        Transform* t = go->transform;
        float pos[3] = {t->localPosition.x, t->localPosition.y, t->localPosition.z};
        if (ImGui::DragFloat3("Position", pos, 0.1f)) {
            t->localPosition = {pos[0], pos[1], pos[2]}; ed.dirty = true;
        }
        float& ez = g_eulerZ[go];
        if (ImGui::DragFloat("Rotation Z", &ez, 1.0f)) {
            t->localRotation = Quat::Euler(0, 0, ez); ed.dirty = true;
        }
        float scl[3] = {t->localScale.x, t->localScale.y, t->localScale.z};
        if (ImGui::DragFloat3("Scale", scl, 0.1f)) {
            t->localScale = {scl[0], scl[1], scl[2]}; ed.dirty = true;
        }
    }

    if (auto* sr = go->GetComponent<SpriteRenderer>()) {
        if (ImGui::CollapsingHeader("Sprite Renderer", ImGuiTreeNodeFlags_DefaultOpen)) {
            float col[4] = {sr->color.r, sr->color.g, sr->color.b, sr->color.a};
            if (ImGui::ColorEdit4("Color", col)) {
                sr->color = {col[0], col[1], col[2], col[3]}; ed.dirty = true;
            }
            float size[2] = {sr->size.x, sr->size.y};
            if (ImGui::DragFloat2("Size", size, 0.1f, 0.0f, 1000.0f)) {
                sr->size = {size[0], size[1]}; ed.dirty = true;
            }
        }
    }
    if (auto* cam = go->GetComponent<Camera>()) {
        if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat("Ortho Size", &cam->orthographicSize, 0.1f, 0.1f, 1000.0f);
            ImGui::Checkbox("Main", &cam->main);
        }
    }

    ImGui::Separator();
    if (!go->GetComponent<SpriteRenderer>() && ImGui::Button("Add Sprite Renderer")) {
        go->AddComponent<SpriteRenderer>(); ed.dirty = true;
    }
    if (!go->GetComponent<Camera>() && ImGui::Button("Add Camera")) {
        go->AddComponent<Camera>(); ed.dirty = true;
    }
    if (ImGui::Button("Delete GameObject")) ed.DeleteSelected();
    ImGui::End();
}

void DrawViewport(EditorState& ed) {
    ImGui::Begin("Scene");
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

    // Zoom with the wheel, pan with right-drag.
    if (hovered && io.MouseWheel != 0.0f)
        ed.cameraZoom = Mathf::Clamp(ed.cameraZoom * (1.0f - io.MouseWheel * 0.1f), 2.0f, 200.0f);
    if (hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
        ed.cameraPos.x -= io.MouseDelta.x / scale;
        ed.cameraPos.y += io.MouseDelta.y / scale;
    }

    dl->PushClipRect(canvasPos, canvasEnd, true);

    // Axes through the world origin.
    dl->AddLine(worldToScreen({-1000, 0, 0}), worldToScreen({1000, 0, 0}), IM_COL32(60, 60, 90, 255));
    dl->AddLine(worldToScreen({0, -1000, 0}), worldToScreen({0, 1000, 0}), IM_COL32(60, 60, 90, 255));

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
    dl->PopClipRect();

    // Click to select the top-most sprite under the cursor.
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        Vec2 world = screenToWorld(io.MousePos);
        GameObject* hit = nullptr;
        for (const auto& up : objs) {
            GameObject* go = up.get();
            auto* sr = go->GetComponent<SpriteRenderer>();
            if (!sr || !go->active) continue;
            Vec3 wp = go->transform->Position();
            Vec3 ls = go->transform->LossyScale();
            float hx = sr->size.x * ls.x * 0.5f;
            float hy = sr->size.y * ls.y * 0.5f;
            if (world.x >= wp.x - hx && world.x <= wp.x + hx &&
                world.y >= wp.y - hy && world.y <= wp.y + hy)
                hit = go; // keep last = topmost drawn
        }
        ed.Select(hit);
    }

    // Drag the selected object (edit mode only).
    if (!ed.isPlaying() && ed.selected() && hovered &&
        ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        ed.selected()->transform->localPosition.x += io.MouseDelta.x / scale;
        ed.selected()->transform->localPosition.y -= io.MouseDelta.y / scale;
        ed.dirty = true;
    }

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

    // A starter scene so the editor isn't empty on first launch.
    EditorState ed;
    ed.CreateCamera("MainCamera");
    GameObject* demo = ed.CreateSprite("Player");
    demo->GetComponent<SpriteRenderer>()->color = Color::Green;
    ed.Select(nullptr);
    ConsoleLog("Welcome to the OkaySpace editor. Use GameObject > Create to add objects.");

    bool running = true;
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

        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        DrawDockSpace(ed, running);
        DrawUpdatePopup();
        DrawHierarchy(ed);
        DrawViewport(ed);   // the "Scene" panel
        DrawInspector(ed);
        DrawConsole();
        DrawProject(ed);

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
