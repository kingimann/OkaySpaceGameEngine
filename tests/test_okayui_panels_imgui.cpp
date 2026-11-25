// The editor's Crash Log, Audio Mixer, History, Problems, Stats and Profiler windows as the editor draws them now: the OkayUI screen only (no Dear ImGui copy),
// and Project Settings' Player, Physics, Rendering and Tags and Layers sections, each a part of its window (okayuihost::HostPanelSection),
// hosted in a Dear ImGui window by okayuihost::HostPanelWindow (shared/OkayUIImGuiEmbed.hpp, the function editor/OkayPanels.cpp
// calls). The body's shapes go into the ImGui window's draw list, so Dear ImGui's popups and windows in front are drawn over it.
// Before, a covered panel switched to its Dear ImGui version for as long as something lay over it.
//
// Headless: Dear ImGui with no window, OkayUI with synthetic input, both drawn by one software rasteriser in the editor's order
// (Dear ImGui's draw data, then OkayUI's frame on top). Checked:
//   - the hosted picture equals the screen drawn the old way (OkayUI's frame last, over the window), so the goldens of
//     okayui_panels and okayui_editor_panels still describe what the editor shows;
//   - an ImGui tooltip and an ImGui window in front cover the panel, and the panel offers no mouse while covered;
//   - the panel's own modal ("Clear crash reports?") stays on OkayUI's top layer, over everything;
//   - clicks reach the panel: Refresh, Reset, a History row;
//   - a collapsed window draws nothing and offers nothing.
#include "test_framework.hpp"
#include "imgui.h"            // first: OkayUI.hpp keeps Dear ImGui's ImTextureID_Invalid when imgui.h came before it
#include "imgui_internal.h"
#include "okayui_compare_rig.hpp"
#include "OkayUIImGuiEmbed.hpp"
#include "OkayUIPanels.hpp"
#include "EditorPanelFixtures.hpp"
#include "okay/UI/OkayUICapture.hpp"

#include <cstdio>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

using namespace okaycompare;
namespace P = okaypanels;

namespace {

const std::uint8_t kFront[3] = {250, 0, 250};    // the ImGui window or tooltip in front: a colour nothing else uses
int Count(const Bitmap& b, int x0, int y0, int x1, int y1, const std::uint8_t c[3]) {
    int n = 0;
    for (int y = y0; y < y1; ++y) for (int x = x0; x < x1; ++x) if (PixelIs(b, x, y, c)) ++n;
    return n;
}

// What the editor's okayuihost::Region is given (HostPanelWindow takes anything with this Offer).
struct Offers {
    struct O { float x, y, w, h; bool hovered, overlay; };
    std::vector<O> list;
    void Offer(float x, float y, float w, float h, bool hovered, bool overlay = false) { list.push_back(O{x, y, w, h, hovered, overlay}); }
    bool active = false;                  // NoteActive: the body is hovered or its window has the focus
    void NoteActive() { active = true; }
    bool BodyHovered() const { for (const O& o : list) if (!o.overlay && o.hovered) return true; return false; }
    int Overlays() const { int n = 0; for (const O& o : list) n += o.overlay ? 1 : 0; return n; }
};

const float kWinX = 20.0f, kWinY = 20.0f, kWinW = 520.0f, kWinH = 380.0f;

// A platform "backend" with no windows, so Dear ImGui's multi-viewports run headless: a window that leaves the main viewport gets an
// OS window of its own (here: a remembered position and size), as in the editor, where panels can be dragged out to other monitors.
struct FakeOsWindow { ImVec2 pos, size; };
std::unordered_map<ImGuiID, FakeOsWindow> g_osWindows;
void EnableFakeViewports() {
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    io.BackendFlags |= ImGuiBackendFlags_PlatformHasViewports | ImGuiBackendFlags_RendererHasViewports;
    // The main viewport needs a platform handle, as a real backend gives it; without one Dear ImGui's own check fails in a
    // build with asserts on (Debug, ASan), though a Release build skips it.
    ImGui::GetMainViewport()->PlatformHandle = &g_osWindows;
    ImGuiPlatformIO& pio = ImGui::GetPlatformIO();
    pio.Platform_CreateWindow = [](ImGuiViewport* v) { g_osWindows[v->ID] = FakeOsWindow{v->Pos, v->Size}; };
    pio.Platform_DestroyWindow = [](ImGuiViewport* v) { g_osWindows.erase(v->ID); };
    pio.Platform_ShowWindow = [](ImGuiViewport*) {};
    pio.Platform_SetWindowPos = [](ImGuiViewport* v, ImVec2 p) { g_osWindows[v->ID].pos = p; };
    pio.Platform_GetWindowPos = [](ImGuiViewport* v) { auto it = g_osWindows.find(v->ID); return it == g_osWindows.end() ? v->Pos : it->second.pos; };
    pio.Platform_SetWindowSize = [](ImGuiViewport* v, ImVec2 sz) { g_osWindows[v->ID].size = sz; };
    pio.Platform_GetWindowSize = [](ImGuiViewport* v) { auto it = g_osWindows.find(v->ID); return it == g_osWindows.end() ? v->Size : it->second.size; };
    pio.Platform_SetWindowFocus = [](ImGuiViewport*) {};
    pio.Platform_GetWindowFocus = [](ImGuiViewport*) { return true; };
    pio.Platform_GetWindowMinimized = [](ImGuiViewport*) { return false; };
    pio.Platform_SetWindowTitle = [](ImGuiViewport*, const char*) {};
    ImGuiPlatformMonitor mon;
    mon.MainPos = mon.WorkPos = ImVec2(0, 0); mon.MainSize = mon.WorkSize = ImVec2(4000, 3000); mon.DpiScale = 1.0f;
    pio.Monitors.resize(0);
    pio.Monitors.push_back(mon);
}

// One frame of "the editor": OkayUI's frame open around Dear ImGui's, the panel window, optionally something in front of it.
struct Editor {
    ImGuiRig im;
    Raster ras;
    okayuihost::ImGuiEmbed embed;
    OkayUI::CapturedGeometry cap;
    Offers offers;
    okayuitest::Sim sim{(float)kW, (float)kH};
    std::function<void(ImVec2, ImVec2)> body;   // the OkayUI screen, given OkayUI's pixels
    const char* title = "Crash Log";
    bool hosted = true;          // false: the old way, the body drawn after ImGui over an empty window
    bool tooltip = false;        // an ImGui tooltip over the panel
    bool front = false;          // an ImGui window in front of the panel
    bool open = true, drawn = false;
    bool collapse = false;
    float winH = kWinH;
    ImVec2 winPos{kWinX, kWinY};
    bool imguiModal = false;     // a Dear ImGui modal opened over everything
    bool viewports = false;      // multi-viewports on (EnableFakeViewports)
    // Sections mode (Project Settings): the window holds collapsing headers, each with an OkayUI-only section under it
    // (okayuihost::HostPanelSection), instead of one panel body.
    struct Section { const char* header; const char* id; std::function<float(float, float, float, float)> body; bool shown = false; bool fold = false; };
    std::vector<Section> sections;
    float scrollTo = -1.0f;      // >= 0: scroll the window there this frame
    Editor() {
        okayuitest::Reset();
        OkayUI::UseLauncherLook(true);
        OkayUI::Style().caretBlink = false;
        ImGui::SetCurrentContext(im.ctx);
        ImGui::GetIO().BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
    }
    ~Editor() { ImGui::SetCurrentContext(im.ctx); embed.Release(); OkayUI::UseLauncherLook(false); }
    Editor(const Editor&) = delete;
    Editor& operator=(const Editor&) = delete;
    // One section: hosted (the body in the window's draw list) or the old way (HostSection's: a placeholder as tall as the body
    // drew last frame, the body left in OkayUI's frame, drawn after ImGui and clipped to the window).
    void DrawSection(Section& sec) {
        auto body = [&](float x, float y, float w, float h) {
            OkayUI::PushTextScale(okayuiscreens::kEditorTextScale);
            const float drawnH = sec.body(x, y, w, h);
            OkayUI::PopTextScale();
            return drawnH;
        };
        if (hosted) { sec.shown = okayuihost::HostPanelSection(sec.id, offers, embed, cap, body); return; }
        ImGuiWindow* win = ImGui::GetCurrentWindow();
        float* hh = ImGui::GetStateStorage()->GetFloatRef(ImGui::GetID(sec.id), -1.0f);
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float w = ImGui::GetContentRegionAvail().x;
        if (*hh < 0.0f) {
            OkayUI::PushClipRect(p.x, p.y, 0.0f, 0.0f);
            *hh = body(p.x, p.y, w, 20000.0f);
            OkayUI::PopClipRect();
            ImGui::InvisibleButton(sec.id, ImVec2(w, *hh));
            return;
        }
        ImGui::InvisibleButton(sec.id, ImVec2(w, *hh));
        const ImRect c = win->InnerClipRect;
        OkayUI::PushClipRect(c.Min.x, c.Min.y, c.GetWidth(), c.GetHeight());
        *hh = body(p.x, p.y, w, *hh + 400.0f);
        OkayUI::PopClipRect();
        sec.shown = true;
    }
    void Panel() {
        ImGui::SetNextWindowPos(winPos, ImGuiCond_Always);
        if (collapse) ImGui::SetNextWindowCollapsed(true, ImGuiCond_Always);
        if (!sections.empty()) {
            ImGui::SetNextWindowSize(ImVec2(kWinW, winH), ImGuiCond_FirstUseEver);
            drawn = false;
            for (Section& sec : sections) sec.shown = false;
            if (ImGui::Begin(title, &open)) {
                if (scrollTo >= 0.0f) { ImGui::SetScrollY(scrollTo); scrollTo = -1.0f; }
                for (Section& sec : sections) {
                    if (sec.fold) ImGui::SetNextItemOpen(false, ImGuiCond_Always);
                    if (ImGui::CollapsingHeader(sec.header, ImGuiTreeNodeFlags_DefaultOpen)) DrawSection(sec);
                }
                drawn = true;
            }
            ImGui::End();
            return;
        }
        if (hosted) {
            drawn = okayuihost::HostPanelWindow(title, &open, "##panelbody", offers, embed, cap, ImVec2(kWinW, winH), [&](ImVec2 p, ImVec2 a) {
                OkayUI::PushTextScale(okayuiscreens::kEditorTextScale);
                body(p, a);
                OkayUI::PopTextScale();
            });
        } else {   // the old hosting: the same window and placeholder, the body left in OkayUI's frame
            ImGui::SetNextWindowSize(ImVec2(kWinW, winH), ImGuiCond_FirstUseEver);
            if (ImGui::Begin(title, &open, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
                const ImVec2 p = ImGui::GetCursorScreenPos();
                const ImVec2 a = ImGui::GetContentRegionAvail();
                ImGui::InvisibleButton("##panelbody", a);
                OkayUI::PushTextScale(okayuiscreens::kEditorTextScale);
                body(p, a);
                OkayUI::PopTextScale();
                drawn = true;
            }
            ImGui::End();
        }
    }
    void Front() {
        if (front) {
            ImGui::SetNextWindowPos(ImVec2(200, 150), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(200, 120), ImGuiCond_Always);
            ImGui::SetNextWindowFocus();
            ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(kFront[0], kFront[1], kFront[2], 255));
            ImGui::Begin("In front", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings);
            ImGui::End();
            ImGui::PopStyleColor();
        }
        if (imguiModal) {
            if (!ImGui::IsPopupOpen("Unsaved changes")) ImGui::OpenPopup("Unsaved changes");
            ImGui::SetNextWindowPos(ImVec2(150, 150), ImGuiCond_Always);
            if (ImGui::BeginPopupModal("Unsaved changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::TextUnformatted("Save the scene?");
                ImGui::EndPopup();
            }
        }
        if (tooltip) {
            ImGui::SetNextWindowPos(ImVec2(100, 250));
            ImGui::PushStyleColor(ImGuiCol_PopupBg, IM_COL32(kFront[0], kFront[1], kFront[2], 255));
            ImGui::BeginTooltip();
            ImGui::Dummy(ImVec2(120, 60));
            ImGui::EndTooltip();
            ImGui::PopStyleColor();
        }
    }
    void Frame() {
        offers.list.clear(); offers.active = false;
        im.mx = sim.mx; im.my = sim.my; im.down = sim.down; im.rdown = sim.rdown;
        sim.Frame([&] {
            im.Frame([&] { Panel(); Front(); });
            if (viewports) ImGui::UpdatePlatformWindows();
        });
        ras.DrawImGui();
        ras.renderer.Render(OkayUI::GetDrawData(), ras.fi);
    }
    void Frames(int n) { for (int i = 0; i < n; ++i) Frame(); }
    void Click(float x, float y) { sim.Move(x, y); Frame(); sim.Down(); Frame(); sim.Up(); Frame(); }
    void RightClick(float x, float y) { sim.Move(x, y); Frame(); sim.rdown = true; Frame(); sim.rdown = false; Frame(); }
};

// ---- the three screens with sample data ----------------------------------------------------------------------------------------

struct CrashLogData {
    okayuiscreens::CrashLogModel model;
    okayuiscreens::CrashLogState state;
    okayuiscreens::CrashLogHost host;
    okayuiscreens::CrashLogResult res;
    int refreshes = 0, clears = 0;
    CrashLogData() {
        model.text = "=== Crash 2026-11-20 10:14:03 ===\nException: access violation\n  at okay::Scene::Update\n\n"
                     "=== Crash 2026-11-21 08:02:44 ===\nException: stack overflow\n  at okay::Physics::Step\n";
        model.path = "C:/OkaySpace/okay_crashlog.txt";
        model.revision = 1;
        host.refresh = [this] { ++refreshes; };
        host.clear = [this] { ++clears; };
        host.copyAll = [](const std::string&) {};
        host.openFolder = [] {};
    }
    std::function<void(ImVec2, ImVec2)> Body() {
        return [this](ImVec2 p, ImVec2 a) { res = okayuiscreens::DrawCrashLog(model, state, host, p.x, p.y, a.x, a.y); };
    }
};

struct MixerData {
    float master = 0.4f, vol[3] = {0.2f, 0.7f, 0.5f}, duckLevel = 0.3f, attack = 0.2f, release = 0.5f;
    bool muted = true, groupMuted[3] = {false, true, false}, duck = true;
    okayuiscreens::AudioMixerModel model;
    okayuiscreens::AudioMixerResult res;
    MixerData() {
        model.peakLeft = 0.5f; model.peakRight = 0.25f;
        model.masterVolume = &master; model.masterMuted = &muted;
        const char* names[3] = {"Music", "Effects", "Voice"};
        for (int i = 0; i < 3; ++i) { model.groupNames.push_back(names[i]); model.groupVolume.push_back(&vol[i]); model.groupMuted.push_back(&groupMuted[i]); }
        model.duckMusic = &duck; model.duckLevel = &duckLevel; model.duckAttack = &attack; model.duckRelease = &release; model.duckGain = 0.6f;
    }
    std::function<void(ImVec2, ImVec2)> Body() {
        return [this](ImVec2 p, ImVec2 a) { res = okayuiscreens::DrawAudioMixer(model, p.x, p.y, a.x, a.y); };
    }
};

struct HistoryData {
    P::HistoryModel model = panelfix::History();
    okayuiscreens::HistoryResult res;
    P::HistoryIntent seen;
    std::function<void(ImVec2, ImVec2)> Body() {
        return [this](ImVec2 p, ImVec2 a) {
            res = okayuiscreens::DrawHistory(model, p.x, p.y, a.x, a.y);
            if (res.intent.undo || res.intent.redo) seen = res.intent;
        };
    }
};

struct ProblemsData {
    P::ProblemsModel model = panelfix::Problems();
    P::ProblemsState state;
    okayuiscreens::ProblemsResult res;
    P::ProblemsIntent seen;
    std::function<void(ImVec2, ImVec2)> Body() {
        return [this](ImVec2 p, ImVec2 a) {
            res = okayuiscreens::DrawProblems(model, state, p.x, p.y, a.x, a.y);
            if (res.intent.fix >= 0) seen.fix = res.intent.fix;
            if (res.intent.open >= 0) seen.open = res.intent.open;
        };
    }
};

struct StatsPanelData {
    panelfix::StatsData d;
    okayuiscreens::StatsResult res;
    bool changed = false;
    std::function<void(ImVec2, ImVec2)> Body() {
        return [this](ImVec2 p, ImVec2 a) { res = okayuiscreens::DrawStats(d.model, p.x, p.y, a.x, a.y); if (res.intent.changed) changed = true; };
    }
};

struct ProfilerPanelData {
    panelfix::ProfilerData d;
    okayuiscreens::ProfilerResult res;
    bool pauseChanged = false;
    std::function<void(ImVec2, ImVec2)> Body() {
        return [this](ImVec2 p, ImVec2 a) { res = okayuiscreens::DrawProfiler(d.model, p.x, p.y, a.x, a.y); if (res.intent.pauseChanged) pauseChanged = true; };
    }
};

// The hosted picture against the old way's, for one screen.
template <class Data>
Diff HostedAgainstOldWay(const char* name, const char* title) {
    Bitmap oldWay(kW, kH);
    {
        Data d;
        Editor e;
        e.title = title; e.hosted = false; e.body = d.Body();
        e.Frames(6);
        oldWay = e.ras.bmp;
    }
    Data d;
    Editor e;
    e.title = title; e.body = d.Body();
    e.Frames(6);
    const Diff diff = Compare(oldWay, e.ras.bmp);
    Save(name, "oldway", oldWay);
    Save(name, "hosted", e.ras.bmp);
    std::printf("  %s: hosted against the old way: average difference %.3f, clearly different %.3f%%\n", name, diff.avg, diff.clearly);
    // and nothing of the body is left in OkayUI's frame (it is in ImGui's draw list)
    CHECK(OkayUI::GetDrawData().indexCount == 0);
    CHECK(e.embed.Skipped() == 0);
    return diff;
}

void SamePictureAsBefore() {
    CHECK(HostedAgainstOldWay<CrashLogData>("crashlog", "Crash Log").clearly < 0.5);
    CHECK(HostedAgainstOldWay<MixerData>("audiomixer", "Audio Mixer").clearly < 0.5);
    CHECK(HostedAgainstOldWay<HistoryData>("history", "History").clearly < 0.5);
    CHECK(HostedAgainstOldWay<ProblemsData>("problems", "Problems###Problems").clearly < 0.5);
    CHECK(HostedAgainstOldWay<StatsPanelData>("stats", "Stats").clearly < 0.5);
    CHECK(HostedAgainstOldWay<ProfilerPanelData>("profiler", "Profiler").clearly < 0.5);
}

void ImGuiInFrontCoversThePanel() {
    // The old way first, to show what the test sees: OkayUI drawn last covers the window in front.
    {
        CrashLogData d;
        Editor e;
        e.hosted = false; e.body = d.Body(); e.front = true;
        e.Frames(4);
        CHECK(Count(e.ras.bmp, 210, 160, 390, 245, kFront) < 180 * 85);   // the body painted over part of it
    }
    CrashLogData d;
    Editor e;
    e.body = d.Body(); e.front = true; e.tooltip = true;
    e.Frames(4);
    // (above the tooltip, whose border crosses the window's lower edge)
    const int frontPx = Count(e.ras.bmp, 210, 160, 390, 245, kFront), tipPx = Count(e.ras.bmp, 110, 258, 210, 300, kFront);
    std::printf("  window in front: %d of %d pixels its own colour; tooltip: %d of %d\n", frontPx, 180 * 85, tipPx, 100 * 42);
    CHECK(frontPx == 180 * 85);     // the window in front is whole
    CHECK(tipPx == 100 * 42);       // so is the tooltip
    CHECK(e.drawn);
    // the mouse over the window in front: the panel does not take it
    e.sim.Move(300, 200); e.Frames(2);
    CHECK(!e.offers.list.empty() && !e.offers.BodyHovered());
    // the mouse over the panel where nothing covers it: it does
    e.front = false; e.tooltip = false;
    e.sim.Move(480, 380); e.Frames(2);
    CHECK(e.offers.BodyHovered());
}

void ClicksReachThePanel() {
    {
        CrashLogData d;
        Editor e;
        e.body = d.Body();
        e.Frames(3);
        const OkayUI::Rect b = d.res.refreshButton;
        CHECK(b.w > 0.0f);
        e.Click(b.x + b.w * 0.5f, b.y + b.h * 0.5f);
        CHECK(d.refreshes == 1);
        // Clear opens the panel's own modal: it is OkayUI's (its top layer), drawn over everything, and its Delete clears
        const OkayUI::Rect c = d.res.clearButton;
        e.Click(c.x + c.w * 0.5f, c.y + c.h * 0.5f);
        e.Frames(2);
        CHECK(d.state.confirming);
        CHECK(OkayUI::GetDrawData().indexCount > 0);                   // the modal stayed in OkayUI's frame
        CHECK(e.offers.Overlays() >= 1);                               // and takes the mouse there
        const OkayUI::Rect del = d.res.confirmDelete;
        CHECK(del.w > 0.0f);
        e.Click(del.x + del.w * 0.5f, del.y + del.h * 0.5f);
        CHECK(d.clears == 1);
    }
    {
        kH = 600;                                // a taller window: Reset is in view without scrolling
        MixerData d;
        Editor e;
        e.winH = 560.0f;
        e.title = "Audio Mixer"; e.body = d.Body();
        e.Frames(3);
        const OkayUI::Rect r = d.res.resetButton;
        std::printf("  Reset at y %.0f..%.0f\n", r.y, r.y + r.h);
        CHECK(r.w > 0.0f && r.y + r.h < kWinY + e.winH);
        e.Click(r.x + r.w * 0.5f, r.y + r.h * 0.5f);
        CHECK(d.master == 1.0f && !d.muted && d.vol[0] == 1.0f && !d.groupMuted[1]);
        kH = 420;
    }
    {
        HistoryData d;
        Editor e;
        e.title = "History"; e.body = d.Body();
        e.Frames(3);
        CHECK(d.res.rows.size() == 8);
        const OkayUI::Rect r6 = d.res.rows[5];   // "Before: Rename Crate", three undos (as in okayui_editor_panels_imgui)
        e.Click(r6.x + 60.0f, r6.y + r6.h * 0.5f);
        CHECK(d.seen.undo == 3 && d.seen.redo == 0);
    }
}

void MoreClicksReachThePanels() {
    {   // Problems: the first row's Fix, then a click on the third row
        ProblemsData d;
        Editor e;
        e.title = "Problems###Problems"; e.body = d.Body();
        e.Frames(3);
        CHECK(d.res.fixButtons.size() >= 3 && d.res.fixButtons[0].w > 0.0f);
        const OkayUI::Rect f = d.res.fixButtons[0];
        e.Click(f.x + f.w * 0.5f, f.y + f.h * 0.5f);
        CHECK(d.seen.fix == d.res.rowIndex[0]);
        const OkayUI::Rect r = d.res.rows[2];
        e.Click(r.x + r.w * 0.5f, r.y + r.h * 0.5f);
        CHECK(d.seen.open == d.res.rowIndex[2]);
    }
    {   // Stats: the Occlusion switch, written through its pointer (a taller window: the switches are below the census)
        kH = 700;
        StatsPanelData d;
        Editor e;
        e.winH = 660.0f;
        e.title = "Stats"; e.body = d.Body();
        e.Frames(3);
        const OkayUI::Rect b = d.res.occlusionBox;
        std::printf("  Occlusion at y %.0f..%.0f\n", b.y, b.y + b.h);
        CHECK(b.w > 0.0f && !d.d.occlusion && b.y + b.h < kWinY + e.winH);
        e.Click(b.x + 8.0f, b.y + b.h * 0.5f);
        CHECK(d.d.occlusion && d.changed);
        kH = 420;
    }
    {   // Profiler: Pause, written through its pointer; the scrub position goes to the newest frame
        ProfilerPanelData d;
        Editor e;
        e.title = "Profiler"; e.body = d.Body();
        e.Frames(3);
        const OkayUI::Rect b = d.res.pauseBox;
        CHECK(b.w > 0.0f && !d.d.paused);
        e.Click(b.x + 8.0f, b.y + b.h * 0.5f);
        e.Frames(1);
        CHECK(d.d.paused && d.pauseChanged && d.d.selected == 239);
    }
}

void ActiveOnlyWhenHoveredOrFocused() {
    // The host gives OkayUI the keys and text only while a panel is active (editor/main.cpp OkayUIInputOn), not for the whole editor.
    CrashLogData d;
    Editor e;
    e.body = d.Body();
    e.sim.Move(480, 380); e.Frames(3);
    CHECK(e.offers.active && e.offers.BodyHovered());            // hovered (and focused: a new window takes the focus)
    e.front = true; e.sim.Move(300, 200); e.Frames(3);           // another window took the focus, the mouse is over it
    CHECK(!e.offers.active);
    e.front = false; e.sim.Move(480, 380); e.Frames(2);           // the mouse back over the panel: active again
    CHECK(e.offers.active);
}

void MenusCloseWhenImGuiTakesOver() {
    {   // the Crash Log's own modal ("Clear crash reports?", OkayUI's) closes when a Dear ImGui modal opens over the window
        CrashLogData d;
        Editor e;
        e.body = d.Body();
        e.Frames(3);
        const OkayUI::Rect c = d.res.clearButton;
        e.Click(c.x + c.w * 0.5f, c.y + c.h * 0.5f); e.Frames(2);
        CHECK(d.state.confirming && e.offers.Overlays() >= 1);
        e.imguiModal = true; e.Frames(3);
        CHECK(!d.state.confirming && e.offers.Overlays() == 0);    // gone: it does not sit above the ImGui modal or take its clicks
        CHECK(d.clears == 0);
    }
    {   // ... and when the keyboard focus moves to another ImGui window
        CrashLogData d;
        Editor e;
        e.body = d.Body();
        e.Frames(3);
        const OkayUI::Rect c = d.res.clearButton;
        e.Click(c.x + c.w * 0.5f, c.y + c.h * 0.5f); e.Frames(2);
        CHECK(d.state.confirming);
        e.front = true; e.Frames(3);
        CHECK(!d.state.confirming);
    }
    {   // a row menu (History's right-click menu) closes on a click outside the panel and the menu
        HistoryData d;
        Editor e;
        e.title = "History"; e.body = d.Body();
        e.Frames(3);
        const OkayUI::Rect r = d.res.rows[2];
        e.RightClick(r.x + 60.0f, r.y + r.h * 0.5f); e.Frames(2);
        CHECK(d.res.menuCopy.w > 0.0f);
        // the editor's background, outside the History window: OkayUI does not see that click (the host blocks its mouse over Dear
        // ImGui's windows), only Dear ImGui does
        e.sim.blocked = true; e.Click(8.0f, 410.0f); e.sim.blocked = false; e.Frames(2);
        CHECK(d.res.menuCopy.w == 0.0f && d.seen.undo == 0 && d.seen.redo == 0);
    }
}

void PanelInAnotherOsWindow() {
    // Multi-viewports: the Crash Log dragged out of the main window (here: placed beyond its right edge) gets an OS window of its own.
    // It is drawn there (its own draw list, nothing left in OkayUI's main frame), it is not pulled back, and clicks reach it.
    CrashLogData d;
    Editor e;
    e.viewports = true; EnableFakeViewports();
    e.body = d.Body();
    e.winPos = ImVec2(900.0f, 120.0f);
    e.Frames(6);
    ImGuiWindow* w = ImGui::FindWindowByName("Crash Log");
    CHECK(w && w->Viewport && w->Viewport != ImGui::GetMainViewport());
    CHECK(e.drawn && w && w->DrawList->VtxBuffer.Size > 400);   // the body's shapes are in that window's draw list
    CHECK(OkayUI::GetDrawData().indexCount == 0);               // and not in OkayUI's frame (the main window)
    const OkayUI::Rect b = d.res.refreshButton;
    CHECK(b.x >= 900.0f);                                       // OkayUI's pixels: desktop minus the main window's place (0, 0 here)
    e.sim.Move(b.x + b.w * 0.5f, b.y + b.h * 0.5f); e.Frames(2);
    CHECK(e.offers.BodyHovered());                              // ImGui says the mouse is over it: offered with the mouse
    ImVec2 vm;
    CHECK(okayuihost::MouseInOtherViewport(vm) &&   // (Dear ImGui keeps whole-pixel mouse positions)
          std::abs(vm.x - (b.x + b.w * 0.5f)) <= 1.0f && std::abs(vm.y - (b.y + b.h * 0.5f)) <= 1.0f);
    e.Click(b.x + b.w * 0.5f, b.y + b.h * 0.5f);
    CHECK(d.refreshes == 1);
    // its own modal is drawn on top of that OS window (its foreground list), not in the main window
    const OkayUI::Rect c = d.res.clearButton;
    e.Click(c.x + c.w * 0.5f, c.y + c.h * 0.5f); e.Frames(2);
    CHECK(d.state.confirming && OkayUI::GetDrawData().indexCount == 0);
    CHECK(ImGui::GetForegroundDrawList(w->Viewport)->VtxBuffer.Size > 0);
    // the mouse over the main window: the host keeps the platform's position
    e.sim.Move(100.0f, 100.0f); e.Frames(2);
    CHECK(!okayuihost::MouseInOtherViewport(vm));
    ImGui::DestroyPlatformWindows();
    g_osWindows.clear();
}

void HiddenDrawsNothing() {
    CrashLogData d;
    Editor e;
    e.body = d.Body(); e.collapse = true;
    e.Frames(3);
    CHECK(!e.drawn);
    CHECK(e.offers.list.empty());
    CHECK(OkayUI::GetDrawData().indexCount == 0);
    e.open = false; e.collapse = false;           // closed: no window at all
    e.Frames(2);
    CHECK(!e.drawn && e.offers.list.empty());
}

// ---- Project Settings' sections (okayuihost::HostPanelSection) -----------------------------------------------------------------

struct SettingsData {
    panelfix::PlayerData player;
    panelfix::PhysicsData physics;
    panelfix::RenderingData rendering;
    P::TagsLayersModel tags = panelfix::TagsLayers();
    P::TagsLayersState tagState;
    okayuiscreens::PlayerSettingsResult pres;
    okayuiscreens::PhysicsSettingsResult phres;
    okayuiscreens::RenderingSettingsResult rres;
    okayuiscreens::TagsLayersResult tres;
    bool playerChanged = false, physicsApply = false, renderingChanged = false;
    int removeTag = -1, renameTag = -1;
    std::string renameTo;
    void Add(Editor& e, bool withTags, bool withOthers = true) {
        e.title = "Project Settings";
        if (withOthers) {
            e.sections.push_back({"Player", "##okayui_player", [this](float x, float y, float w, float h) {
                pres = okayuiscreens::DrawPlayerSettings(player.model, x, y, w, h); if (pres.intent.changed) playerChanged = true; return pres.contentHeight; }});
            e.sections.push_back({"Physics", "##okayui_physics", [this](float x, float y, float w, float h) {
                phres = okayuiscreens::DrawPhysicsSettings(physics.model, x, y, w, h); if (phres.intent.apply) physicsApply = true; return phres.contentHeight; }});
            e.sections.push_back({"Rendering", "##okayui_rendering", [this](float x, float y, float w, float h) {
                rres = okayuiscreens::DrawRenderingSettings(rendering.model, x, y, w, h); if (rres.intent.changed) renderingChanged = true; return rres.contentHeight; }});
        }
        if (withTags)
            e.sections.push_back({"Tags and Layers", "##okayui_tagslayers", [this](float x, float y, float w, float h) {
                tres = okayuiscreens::DrawTagsLayers(tags, tagState, x, y, w, h);
                if (tres.intent.removeTag >= 0) removeTag = tres.intent.removeTag;
                if (tres.intent.renameTag >= 0) { renameTag = tres.intent.renameTag; renameTo = tres.intent.renameTo; }
                return tres.contentHeight; }});
    }
};

Diff SectionsAgainstOldWay(const char* name, bool tags, bool others) {
    Bitmap oldWay(kW, kH);
    {
        SettingsData d;
        Editor e;
        e.hosted = false; e.winH = (float)kH - 40.0f; d.Add(e, tags, others);
        e.Frames(6);
        oldWay = e.ras.bmp;
    }
    SettingsData d;
    Editor e;
    e.winH = (float)kH - 40.0f; d.Add(e, tags, others);
    e.Frames(6);
    const Diff diff = Compare(oldWay, e.ras.bmp);
    Save(name, "oldway", oldWay);
    Save(name, "hosted", e.ras.bmp);
    std::printf("  %s: hosted against the old way: average difference %.3f, clearly different %.3f%%\n", name, diff.avg, diff.clearly);
    CHECK(OkayUI::GetDrawData().indexCount == 0);   // nothing of the sections is left in OkayUI's frame
    CHECK(e.embed.Skipped() == 0);
    for (const Editor::Section& sec : e.sections) CHECK(sec.shown);
    return diff;
}

void SectionsSamePictureAsBefore() {
    kH = 760;
    CHECK(SectionsAgainstOldWay("settings_player_physics_rendering", false, true).clearly < 0.5);
    kH = 900;
    CHECK(SectionsAgainstOldWay("settings_tagslayers", true, false).clearly < 0.5);
    kH = 420;
}

void SectionsPlaceholderIsTheBodyHeight() {
    // The first frame only measures (nothing is shown); from the second the placeholder is exactly as tall as the body drew, so the
    // next header follows the section as it did with a Dear ImGui body.
    kH = 760;
    SettingsData d;
    Editor e;
    e.winH = 720.0f; d.Add(e, false);
    e.Frame();
    CHECK(OkayUI::GetDrawData().indexCount == 0);
    ImGuiWindow* w = ImGui::FindWindowByName("Project Settings");
    CHECK(w && w->DrawList->VtxBuffer.Size < 3000);            // the first frame: the window and its headers only
    e.Frames(2);
    CHECK(d.pres.contentHeight > 40.0f && d.phres.contentHeight > d.pres.contentHeight);
    const float playerBottom = d.pres.company.y + d.pres.contentHeight;   // the body's top is its first row's
    CHECK(d.phres.gravity2D.y > playerBottom);                            // Physics' body starts below Player's, past its header
    CHECK(d.phres.gravity2D.y - playerBottom < 80.0f);                     // ... one header and the spacing, not a guessed 300
    kH = 420;
}

void SectionsCoveredByImGui() {
    // A window in front and a tooltip cover the sections; the part under them takes no mouse. Before, the editor drew the Dear ImGui
    // copy of a covered section instead; there is none now.
    kH = 760;
    SettingsData d;
    Editor e;
    e.winH = 720.0f; d.Add(e, false); e.front = true; e.tooltip = true;
    e.Frames(4);
    const int frontPx = Count(e.ras.bmp, 210, 160, 390, 245, kFront), tipPx = Count(e.ras.bmp, 110, 258, 210, 300, kFront);
    std::printf("  sections: window in front %d of %d pixels its own colour; tooltip %d of %d\n", frontPx, 180 * 85, tipPx, 100 * 42);
    CHECK(frontPx == 180 * 85 && tipPx == 100 * 42);
    for (const Editor::Section& sec : e.sections) CHECK(sec.shown);   // still drawn: no switch to another version
    e.sim.Move(300, 200); e.Frames(2);
    CHECK(!e.offers.list.empty() && !e.offers.BodyHovered());
    e.front = false; e.tooltip = false;
    const OkayUI::Rect a = d.rres.apply;
    e.sim.Move(a.x + 4.0f, a.y + a.h * 0.5f); e.Frames(2);
    CHECK(e.offers.BodyHovered());
    kH = 420;
}

void SectionsTakeClicks() {
    kH = 760;
    {
        SettingsData d;
        Editor e;
        e.winH = 720.0f; d.Add(e, false);
        e.Frames(3);
        const OkayUI::Rect a = d.phres.apply;
        CHECK(a.w > 0.0f && a.y + a.h < kWinY + e.winH);
        e.Click(a.x + a.w * 0.5f, a.y + a.h * 0.5f);
        CHECK(d.physicsApply);
        const OkayUI::Rect sk = d.rres.skybox;
        e.Click(sk.x + 8.0f, sk.y + sk.h * 0.5f);
        CHECK(!d.rendering.skybox && d.renderingChanged);
        // the Width field: a plain number (no steppers), typed into
        const OkayUI::Rect wf = d.pres.width;
        e.Click(wf.x + 12.0f, wf.y + wf.h * 0.5f);
        e.sim.Press(okayuitest::Key_SelectAll); e.Frame(); e.sim.Type("1920"); e.Frame(); e.sim.Press(okayuitest::Key_Enter); e.Frame();
        CHECK(d.player.width == 1920 && d.playerChanged);
    }
    kH = 900;
    {   // Tags and Layers: Remove; the rename popup is an OkayUI overlay (top layer, offered), its Rename asks for the rename
        SettingsData d;
        Editor e;
        e.winH = 860.0f; d.Add(e, true, false);
        e.Frames(3);
        CHECK(d.tres.removeButtons.size() == 4);
        const OkayUI::Rect rm = d.tres.removeButtons[1];
        e.Click(rm.x + rm.w * 0.5f, rm.y + rm.h * 0.5f);
        CHECK(d.removeTag == 1);
        const OkayUI::Rect rn = d.tres.renameButtons[0];
        e.Click(rn.x + rn.w * 0.5f, rn.y + rn.h * 0.5f); e.Frames(1);
        CHECK(d.tres.renameField.w > 0.0f && e.offers.Overlays() >= 1);
        CHECK(OkayUI::GetDrawData().indexCount > 0);                 // the popup stayed in OkayUI's frame, over everything
        const OkayUI::Rect f = d.tres.renameField;
        e.Click(f.x + 10.0f, f.y + f.h * 0.5f);
        e.sim.Press(okayuitest::Key_SelectAll); e.Frame(); e.sim.Type("Loot"); e.Frame();
        const OkayUI::Rect b = d.tres.renameButton;
        e.Click(b.x + b.w * 0.5f, b.y + b.h * 0.5f);
        CHECK(d.renameTag == 0 && d.renameTo == "Loot");
        // a Dear ImGui modal over the window closes the popup (it does not sit above the modal)
        e.imguiModal = true; e.Frames(3);
        CHECK(d.tres.renameField.w == 0.0f && e.offers.Overlays() == 0);
    }
    kH = 420;
}

void SectionsScrollWithTheWindow() {
    // All four sections in a short window: it scrolls them like any items. Only the part the window shows is offered and drawn.
    kH = 520;
    SettingsData d;
    Editor e;
    e.winH = 480.0f; d.Add(e, true);
    e.Frames(3);
    ImGuiWindow* w = ImGui::FindWindowByName("Project Settings");
    CHECK(w && w->ScrollMax.y > 600.0f);                          // Tags and Layers runs far past the window
    const ImRect clip = w->InnerClipRect;
    auto offersInside = [&] {
        for (const Offers::O& o : e.offers.list)
            if (!o.overlay && (o.x < clip.Min.x - 0.5f || o.y < clip.Min.y - 0.5f || o.x + o.w > clip.Max.x + 0.5f || o.y + o.h > clip.Max.y + 0.5f)) return false;
        return true;
    };
    CHECK(offersInside());
    const float applyY = d.phres.apply.y;
    e.scrollTo = 200.0f; e.Frames(3);
    CHECK(std::abs((applyY - d.phres.apply.y) - 200.0f) < 1.0f);  // the body moved with the window
    CHECK(offersInside());
    // the background below the window stays clear: nothing of the scrolled sections is drawn outside the window
    const int below = (int)(kWinY + e.winH) + 2;
    std::uint8_t bg[3];
    {
        const std::uint8_t* px = &e.ras.bmp.rgba[((std::size_t)(kH - 1) * (std::size_t)kW) * 4];
        bg[0] = px[0]; bg[1] = px[1]; bg[2] = px[2];
    }
    CHECK(Count(e.ras.bmp, 0, below, kW, kH, bg) == kW * (kH - below));
    // Player and Physics scrolled out of view: they offer nothing (Tags and Layers is in view)
    e.scrollTo = 900.0f; e.Frames(3);
    int bodies = 0;
    for (const Offers::O& o : e.offers.list) bodies += o.overlay ? 0 : 1;
    CHECK(bodies >= 1 && bodies <= 2);
    CHECK(offersInside());
    // a click on a layer field in view reaches it
    const OkayUI::Rect lf = d.tres.layerFields[20];
    if (lf.y > clip.Min.y && lf.y + lf.h < clip.Max.y) {
        e.Click(lf.x + 20.0f, lf.y + lf.h * 0.5f);
        CHECK(OkayUI::WantsTextInput());
    }
    kH = 420;
}

void SectionsHiddenDrawNothing() {
    kH = 760;
    SettingsData d;
    Editor e;
    e.winH = 720.0f; d.Add(e, false);
    e.Frames(3);
    // a folded header: that section is not drawn and offers nothing
    e.sections[1].fold = true;
    e.Frames(2);
    CHECK(!e.sections[1].shown && e.sections[0].shown && e.sections[2].shown);
    // a collapsed window: nothing at all
    e.collapse = true; e.Frames(2);
    for (const Editor::Section& sec : e.sections) CHECK(!sec.shown);
    CHECK(e.offers.list.empty() && OkayUI::GetDrawData().indexCount == 0);
    kH = 420;
}

} // namespace

int main() {
    RUN_SUITE("okayui panels in Dear ImGui windows (Crash Log, Audio Mixer, History, Problems, Stats, Profiler; Project Settings' Player, Physics, Rendering, Tags and Layers)");
    kH = 420;
    SamePictureAsBefore();
    ImGuiInFrontCoversThePanel();
    ClicksReachThePanel();
    MoreClicksReachThePanels();
    ActiveOnlyWhenHoveredOrFocused();
    MenusCloseWhenImGuiTakesOver();
    PanelInAnotherOsWindow();
    HiddenDrawsNothing();
    SectionsSamePictureAsBefore();
    SectionsPlaceholderIsTheBodyHeight();
    SectionsCoveredByImGui();
    SectionsTakeClicks();
    SectionsScrollWithTheWindow();
    SectionsHiddenDrawNothing();
    TEST_MAIN_RESULT();
}
