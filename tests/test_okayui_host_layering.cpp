// Hosted OkayUI bodies and Dear ImGui's window order (shared/OkayUIHostImGui.hpp WindowInFrontCovers / CoveredInImGui).
//
// OkayUI is flushed after Dear ImGui, so an OkayUI body hosted in an ImGui window is painted over every ImGui window, including
// floating windows that ImGui draws in front of it. The editor once showed this in Project Settings: a History window behind it
// drew its "Undo  Redo  2 behind 0 ahead" row over Project Settings' first line, and a window squeezed against the left edge drew
// a column of first letters down Project Settings. The hosts now ask CoveredInImGui and draw the Dear ImGui body (which ImGui
// layers correctly) while another window in front overlaps. This test builds that arrangement headlessly (a dock space with a
// docked panel, floating windows on top of each other, a child window, a tooltip) and checks what the helper answers.
// Written from Dear ImGui's documented window order (top-level windows drawn in their list order, back to front; docked windows
// with their dock host; tooltips on top), not from any other test.
#include "test_framework.hpp"

#include "imgui.h"
#include "imgui_internal.h"
#include "OkayUIHostImGui.hpp"

#include <cstring>
#include <functional>

namespace {

struct Rig {
    ImGuiContext* ctx = nullptr;
    Rig() {
        ctx = ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr; io.LogFilename = nullptr;
        io.DisplaySize = ImVec2(1920.0f, 1200.0f);
        io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_NoMouseCursorChange;
        io.Fonts->AddFontDefault();
    }
    ~Rig() { ImGui::DestroyContext(ctx); }
    Rig(const Rig&) = delete;
    Rig& operator=(const Rig&) = delete;
    void Frame(const std::function<void()>& body, ImVec2 mouse = ImVec2(-100.0f, -100.0f)) {
        ImGuiIO& io = ImGui::GetIO();
        io.DeltaTime = 1.0f / 60.0f;
        io.AddMousePosEvent(mouse.x, mouse.y);
        ImGui::NewFrame();
        body();
        ImGui::Render();
    }
};

bool showHistory = true, showNarrow = true, showSettings = true, tooltip = false;
bool focusHistory = false;
ImGuiID dockId = 0;

void Window(const char* name, float x, float y, float w, float h) {
    ImGui::SetNextWindowPos(ImVec2(x, y), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(w, h), ImGuiCond_Always);
    ImGui::Begin(name);
    ImGui::TextUnformatted(name);
    ImGui::End();
}

// The editor's arrangement: a full-screen dock host (never brought to front) with a docked "Hierarchy" on the left and a docked
// "Inspector" on the right, and three floating windows on top, created back to front: History, a narrow window, Project Settings.
void Editor() {
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(1920, 1200));
    ImGui::Begin("##DockHost", nullptr, ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus);
    dockId = ImGui::GetID("TestDockSpace");
    if (!ImGui::DockBuilderGetNode(dockId)) {
        ImGui::DockBuilderAddNode(dockId, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockId, ImVec2(1920, 1200));
        ImGuiID left = 0, right = 0;
        ImGui::DockBuilderSplitNode(dockId, ImGuiDir_Left, 0.30f, &left, &right);
        ImGui::DockBuilderDockWindow("Hierarchy", left);
        ImGui::DockBuilderDockWindow("Inspector", right);
        ImGui::DockBuilderFinish(dockId);
    }
    ImGui::DockSpace(dockId, ImVec2(0, 0));
    ImGui::End();
    ImGui::Begin("Hierarchy"); ImGui::TextUnformatted("objects"); ImGui::End();
    ImGui::Begin("Inspector"); ImGui::TextUnformatted("asset"); ImGui::End();
    if (focusHistory) { ImGui::SetNextWindowFocus(); focusHistory = false; }
    if (showHistory) Window("History", 60, 64, 310, 110);
    if (showNarrow) Window("Narrow", 62, 180, 22, 700);
    if (showSettings) {
        ImGui::SetNextWindowPos(ImVec2(60, 64), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(460, 950), ImGuiCond_Always);
        ImGui::Begin("Project Settings");
        ImGui::TextUnformatted("Defaults a new build/scene inherits.");
        ImGui::BeginChild("section", ImVec2(0, 200), ImGuiChildFlags_Borders);   // drawn with its parent: never "in front" of it
        ImGui::TextUnformatted("Player");
        ImGui::EndChild();
        ImGui::End();
    }
    if (tooltip) {
        ImGui::BeginTooltip();   // placed at the mouse
        ImGui::TextUnformatted("a tooltip");
        ImGui::EndTooltip();
    }
}

ImGuiWindow* Find(const char* name) { return ImGui::FindWindowByName(name); }
bool Covered(const char* name) {
    const ImGuiWindow* w = Find(name);
    return w && okayuihost::CoveredInImGui(w, w->Pos.x, w->Pos.y, w->Size.x, w->Size.y);
}
bool CoveredAt(const char* name, float x, float y, float w, float h) {
    const ImGuiWindow* win = Find(name);
    return win && okayuihost::CoveredInImGui(win, x, y, w, h);
}

// The Inspector and the Hierarchy as two tabs of one dock node (the editor's right dock), or side by side. `tabs` picks the arrangement
// when the dock is first built; `front` names the tab brought to the front this frame (nullptr: none). `inside(name)` runs inside each
// panel's window, after Begin, as a host's body would.
bool tabs = true;
const char* front = nullptr;
std::function<void(const char*)> inside;
void RightDock() {
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(1920, 1200));
    ImGui::Begin("##DockHost", nullptr, ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus);
    const ImGuiID id = ImGui::GetID("TabDock");
    if (!ImGui::DockBuilderGetNode(id)) {
        ImGui::DockBuilderAddNode(id, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(id, ImVec2(1920, 1200));
        ImGuiID left = 0, right = 0;
        ImGui::DockBuilderSplitNode(id, ImGuiDir_Left, 0.70f, &left, &right);
        if (tabs) { ImGui::DockBuilderDockWindow("Inspector", right); ImGui::DockBuilderDockWindow("Hierarchy", right); }
        else {
            ImGuiID top = 0, bottom = 0;
            ImGui::DockBuilderSplitNode(right, ImGuiDir_Up, 0.5f, &top, &bottom);
            ImGui::DockBuilderDockWindow("Hierarchy", top); ImGui::DockBuilderDockWindow("Inspector", bottom);
        }
        ImGui::DockBuilderFinish(id);
    }
    ImGui::DockSpace(id, ImVec2(0, 0));
    ImGui::End();
    for (const char* name : {"Hierarchy", "Inspector"}) {
        if (front && std::strcmp(front, name) == 0) ImGui::SetNextWindowFocus();
        ImGui::Begin(name);   // as the editor's Inspector: its return is not looked at, the body runs either way
        if (inside) inside(name);
        ImGui::End();
    }
    front = nullptr;
}

// Is the panel shown? Asked inside it, as HostSection and HostWindow (editor/OkayPanels.cpp) ask before an OkayUI body draws.
// Also records where two placeholder items one after the other land (the Inspector's card, then its Add Component button).
struct Seen { bool hidden = false; float firstY = 0.0f, secondY = 0.0f; };
Seen seenHierarchy, seenInspector;
void Look(const char* name) {
    Seen& s = std::strcmp(name, "Inspector") == 0 ? seenInspector : seenHierarchy;
    s.hidden = okayuihost::HiddenInImGui(ImGui::GetCurrentWindow());
    s.firstY = ImGui::GetCursorScreenPos().y;
    ImGui::InvisibleButton("##card", ImVec2(200.0f, 90.0f));
    s.secondY = ImGui::GetCursorScreenPos().y;
    ImGui::InvisibleButton("##add", ImVec2(200.0f, 30.0f));
}

void TabbedPanels() {
    // Tabs: the Inspector and the Hierarchy share the right dock (the owner's layout in v2.903.1.3, every Experimental item on).
    {
        Rig rig;
        tabs = true; inside = Look;
        for (int i = 0; i < 3; ++i) rig.Frame(RightDock);
        front = "Hierarchy";
        for (int i = 0; i < 3; ++i) rig.Frame(RightDock);
        // The Hierarchy tab is picked: the Inspector is hidden, so its OkayUI card, component bars and Add Component button must not
        // be drawn or take a click. ImGui does not even move the cursor there: every placeholder lands on the same line, which is
        // where the card, a component bar and the Add Component button were all painted over the Hierarchy before the fix.
        CHECK(seenInspector.hidden);
        CHECK(!seenHierarchy.hidden);
        CHECK(seenInspector.secondY == seenInspector.firstY);
        CHECK(seenHierarchy.secondY > seenHierarchy.firstY + 80.0f);
        // The Inspector tab picked: the other way round.
        front = "Inspector";
        for (int i = 0; i < 3; ++i) rig.Frame(RightDock);
        CHECK(!seenInspector.hidden);
        CHECK(seenHierarchy.hidden);
        CHECK(seenInspector.secondY > seenInspector.firstY + 80.0f);
        // Back to the Hierarchy tab: never a frame where both bodies would draw over each other.
        front = "Hierarchy";
        for (int i = 0; i < 3; ++i) {
            rig.Frame(RightDock);
            CHECK(seenInspector.hidden || seenHierarchy.hidden);
        }
        CHECK(seenInspector.hidden && !seenHierarchy.hidden);
    }
    // Side by side: both panels are shown and both bodies draw.
    {
        Rig rig;
        tabs = false; inside = Look;
        for (int i = 0; i < 4; ++i) rig.Frame(RightDock);
        CHECK(!seenInspector.hidden && !seenHierarchy.hidden);
        CHECK(seenInspector.secondY > seenInspector.firstY + 80.0f && seenHierarchy.secondY > seenHierarchy.firstY + 80.0f);
    }
    // A collapsed floating window shows nothing either; no window at all is not hidden (the helper takes a null window).
    {
        Rig rig;
        auto floating = [] {
            ImGui::SetNextWindowPos(ImVec2(100, 100), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(300, 300), ImGuiCond_Always);
            ImGui::SetNextWindowCollapsed(true, ImGuiCond_Always);
            ImGui::Begin("Floating");
            seenInspector.hidden = okayuihost::HiddenInImGui(ImGui::GetCurrentWindow());
            ImGui::End();
        };
        for (int i = 0; i < 3; ++i) rig.Frame(floating);
        CHECK(seenInspector.hidden);
        CHECK(!okayuihost::HiddenInImGui(nullptr));
    }
    inside = nullptr;
}

} // namespace

int main() {
    RUN_SUITE("okayui_host_layering");
    TabbedPanels();
    Rig rig;
    for (int i = 0; i < 4; ++i) rig.Frame(Editor);

    rig.Frame([] {
        Editor();
        CHECK(Find("History") && Find("Narrow") && Find("Project Settings") && Find("Hierarchy") && Find("Inspector"));
        // Project Settings is in front of everything: its sections may be drawn by OkayUI.
        CHECK(!Covered("Project Settings"));
        // History and the narrow window are behind Project Settings: OkayUI would paint their bodies over it (the bug).
        CHECK(Covered("History"));
        CHECK(Covered("Narrow"));
        // The docked Hierarchy lies under the floating windows; the docked Inspector on the right has nothing over it.
        CHECK(Covered("Hierarchy"));
        CHECK(!Covered("Inspector"));
        // Only overlaps count: a part of the Hierarchy below and right of every floating window is clear.
        CHECK(!CoveredAt("Hierarchy", 100.0f, 1100.0f, 50.0f, 50.0f));
        // A part of History that Project Settings covers counts, wherever it is.
        CHECK(CoveredAt("History", 300.0f, 100.0f, 20.0f, 20.0f));
    });

    // Bring History to the front: now its body may be drawn by OkayUI, and Project Settings is covered where History lies on it,
    // but not further down (a section below History's bottom edge stays with OkayUI).
    focusHistory = true;
    for (int i = 0; i < 3; ++i) rig.Frame(Editor);
    rig.Frame([] {
        Editor();
        CHECK(!Covered("History"));
        CHECK(Covered("Project Settings"));
        CHECK(CoveredAt("Project Settings", 80.0f, 100.0f, 400.0f, 40.0f));
        CHECK(!CoveredAt("Project Settings", 80.0f, 400.0f, 400.0f, 200.0f));
    });

    // A window that is closed does not count from the next frame on.
    showHistory = false; showNarrow = false;
    for (int i = 0; i < 2; ++i) rig.Frame(Editor);
    rig.Frame([] {
        Editor();
        CHECK(!Covered("Project Settings"));
    });

    // A tooltip is drawn above every window: one over the Inspector covers it while it shows.
    tooltip = true;
    for (int i = 0; i < 3; ++i) rig.Frame(Editor, ImVec2(1500.0f, 500.0f));
    rig.Frame([] {
        Editor();
        CHECK(CoveredAt("Inspector", 1450.0f, 450.0f, 200.0f, 200.0f));
        CHECK(!CoveredAt("Inspector", 1450.0f, 900.0f, 200.0f, 200.0f));
    }, ImVec2(1500.0f, 500.0f));
    tooltip = false;
    for (int i = 0; i < 2; ++i) rig.Frame(Editor);

    // The tooltip a Dear ImGui drag shows under the mouse does not count (the panel must not switch toolkits halfway through a drag).
    auto dragFrame = [](bool check) {
        Editor();
        ImGui::SetNextWindowPos(ImVec2(1300, 300), ImGuiCond_Always);
        ImGui::Begin("Source");
        ImGui::Button("drag me");
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
            ImGui::SetDragDropPayload("TEST", "x", 1);
            ImGui::TextUnformatted("dragging");
            ImGui::EndDragDropSource();
        }
        ImGui::End();
        if (check) {
            CHECK(ImGui::GetDragDropPayload() != nullptr);
            CHECK(!CoveredAt("Inspector", 1450.0f, 650.0f, 200.0f, 200.0f));
        }
    };
    {
        ImGuiIO& io = ImGui::GetIO();
        for (int i = 0; i < 3; ++i) rig.Frame([&] { dragFrame(false); }, ImVec2(1340, 340));
        io.AddMouseButtonEvent(0, true);
        rig.Frame([&] { dragFrame(false); }, ImVec2(1340, 340));
        for (int i = 0; i < 4; ++i) rig.Frame([&] { dragFrame(false); }, ImVec2(1500, 700));
        rig.Frame([&] { dragFrame(true); }, ImVec2(1500, 700));
        io.AddMouseButtonEvent(0, false);
        rig.Frame([&] { dragFrame(false); }, ImVec2(1500, 700));
        rig.Frame(Editor);
    }
    rig.Frame([] {
        Editor();
        CHECK(!Covered("Inspector"));
    });

    // An open popup still counts, as before (PopupCoversRect).
    auto popup = [] {
        ImGui::SetNextWindowPos(ImVec2(1500.0f, 500.0f));
        if (ImGui::BeginPopup("menu")) { ImGui::TextUnformatted("item"); ImGui::EndPopup(); }
    };
    rig.Frame([&] { Editor(); ImGui::OpenPopup("menu"); popup(); });
    for (int i = 0; i < 2; ++i) rig.Frame([&] { Editor(); popup(); });
    rig.Frame([&] {
        Editor();
        CHECK(CoveredAt("Inspector", 1450.0f, 450.0f, 200.0f, 200.0f));
        CHECK(!CoveredAt("Inspector", 1450.0f, 900.0f, 200.0f, 200.0f));
        popup();
    });

    TEST_MAIN_RESULT();
}
