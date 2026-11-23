// The editor's Crash Log, Audio Mixer and History windows as the editor draws them now: the OkayUI screen only (no Dear ImGui copy),
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
    bool BodyHovered() const { for (const O& o : list) if (!o.overlay && o.hovered) return true; return false; }
    int Overlays() const { int n = 0; for (const O& o : list) n += o.overlay ? 1 : 0; return n; }
};

const float kWinX = 20.0f, kWinY = 20.0f, kWinW = 520.0f, kWinH = 380.0f;

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
    void Panel() {
        ImGui::SetNextWindowPos(ImVec2(kWinX, kWinY), ImGuiCond_Always);
        if (collapse) ImGui::SetNextWindowCollapsed(true, ImGuiCond_Always);
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
        offers.list.clear();
        im.mx = sim.mx; im.my = sim.my; im.down = sim.down;
        sim.Frame([&] { im.Frame([&] { Panel(); Front(); }); });
        ras.DrawImGui();
        ras.renderer.Render(OkayUI::GetDrawData(), ras.fi);
    }
    void Frames(int n) { for (int i = 0; i < n; ++i) Frame(); }
    void Click(float x, float y) { sim.Move(x, y); Frame(); sim.Down(); Frame(); sim.Up(); Frame(); }
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

} // namespace

int main() {
    RUN_SUITE("okayui panels in Dear ImGui windows (Crash Log, Audio Mixer, History)");
    kH = 420;
    SamePictureAsBefore();
    ImGuiInFrontCoversThePanel();
    ClicksReachThePanel();
    HiddenDrawsNothing();
    TEST_MAIN_RESULT();
}
