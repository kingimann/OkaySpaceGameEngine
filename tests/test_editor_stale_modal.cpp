// Clicks that go nowhere in the editor (owner, v2.903.8.118: "i just cant interact with anything or click anything" after a project
// opened; frames kept coming, so the hang watchdog stayed quiet; still so with every Settings > Experimental switch off and with the
// launcher ended). The cause is not confirmed on the owner's machine. This test checks the safety net and the diagnosis tools in
// editor/ModalGuard.hpp headlessly with Dear ImGui (the same version as the editor):
//  1. what Dear ImGui does by itself: a modal that had the keyboard focus and is then no longer drawn (the sign-in gate after the
//     server said yes: DrawSignInGate returned before BeginPopupModal without closing it) is closed by Dear ImGui on the next
//     frame, so that path alone does not explain the owner's bug (measured, not assumed);
//  2. but when the focus is elsewhere (simulated: no focused window), an undrawn modal stays open, draws nothing and blocks every
//     click: Sweep closes it after a few frames and names it, and the click works again;
//  3. a modal that is still drawn every frame is left alone and still blocks the windows behind it (the guard never closes a
//     question the person can see);
//  4. CloseIfOpen closes a named modal at once (what the sign-in gate and the project chooser now do when no longer needed);
//  5. a drawn modal that lies off the screen is moved back to the middle;
//  6. the confirmed cause: after the editor is ended in Task Manager both recovery questions are pending ("Recover Autosave",
//     DrawRecoveryPopup, and "Restore recovered work?", DrawDocRecoveryPopup, both in editor/main.cpp). Each re-opened itself every
//     frame, so each OpenPopup closed the other: one modal always open, always on its first (hidden) frame, never shown, every
//     click blocked. The two functions below make the same Dear ImGui calls as the editor's; with OpenModalWhenFree exactly one
//     question is shown, on screen, and its buttons work, then the second one;
//  7. ModalTrail names modals opening and closing; InputProbe writes one line when clicks arrive and none lands on a window.
// Written from Dear ImGui's documented popup behaviour (popups stay open until closed; modals block input behind them).
#include "test_framework.hpp"

#include "imgui.h"
#include "imgui_internal.h"
#include "ModalGuard.hpp"

#include <functional>
#include <string>
#include <vector>

namespace {

struct Rig {
    ImGuiContext* ctx = nullptr;
    Rig() {
        ctx = ImGui::CreateContext();
        ImGui::SetCurrentContext(ctx);
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr; io.LogFilename = nullptr;
        io.DisplaySize = ImVec2(1280.0f, 800.0f);
        io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_NoMouseCursorChange;
        io.Fonts->AddFontDefault();
    }
    ~Rig() { ImGui::DestroyContext(ctx); }
    Rig(const Rig&) = delete;
    Rig& operator=(const Rig&) = delete;
    // One frame: the mouse at `mouse`, the left button `down`; `afterNewFrame` runs right after NewFrame (where the editor sweeps).
    void Frame(const std::function<void()>& body, ImVec2 mouse, bool down, const std::function<void()>& afterNewFrame = {}) {
        ImGuiIO& io = ImGui::GetIO();
        io.DeltaTime = 1.0f / 60.0f;
        io.AddMousePosEvent(mouse.x, mouse.y);
        io.AddMouseButtonEvent(0, down);
        ImGui::NewFrame();
        if (afterNewFrame) afterNewFrame();
        body();
        ImGui::Render();
    }
};

const char* kGate = "Sign in to OkaySpace###SignInGate";
const ImVec2 kButton(60.0f, 60.0f);   // where the editor's "Play" button is in this test

// The editor: a window with a button; the gate's modal while `gateShown` (the sign-in check is running).
bool Editor(bool gateShown) {
    bool clicked = false;
    ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(300, 200), ImGuiCond_Always);
    ImGui::Begin("Toolbar");
    ImGui::SetCursorScreenPos(ImVec2(40, 50));
    clicked = ImGui::Button("Play", ImVec2(80, 24));
    ImGui::End();
    if (gateShown) {   // the gate before the fix: open while needed, then simply no longer drawn
        if (!ImGui::IsPopupOpen(kGate)) ImGui::OpenPopup(kGate);
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        if (ImGui::BeginPopupModal(kGate, nullptr, ImGuiWindowFlags_NoMove)) { ImGui::TextUnformatted("Checking your sign-in..."); ImGui::EndPopup(); }
    }
    return clicked;
}

// Click the button: press one frame, release the next. True when the button saw the click.
bool ClickButton(Rig& rig, const std::function<void()>& sweep) {
    bool clicked = false;
    rig.Frame([&] { clicked |= Editor(false); }, kButton, false, sweep);
    rig.Frame([&] { clicked |= Editor(false); }, kButton, true, sweep);
    rig.Frame([&] { clicked |= Editor(false); }, kButton, false, sweep);
    return clicked;
}

}  // namespace

void imgui_closes_a_focused_undrawn_modal_by_itself() {
    Rig rig;
    for (int i = 0; i < 5; ++i) rig.Frame([] { Editor(true); }, ImVec2(-100, -100), false);   // checking the sign-in
    CHECK(ImGui::GetTopMostPopupModal() != nullptr);
    for (int i = 0; i < 2; ++i) rig.Frame([] { Editor(false); }, ImVec2(-100, -100), false);  // signed in: the gate no longer drawn
    CHECK(GImGui->OpenPopupStack.Size == 0);
    CHECK(ClickButton(rig, {}));
}

// The gate's modal open and drawn, then the focus lost (no focused window) and the modal no longer drawn.
void OpenGateThenLoseFocus(Rig& rig, const std::function<void()>& sweep) {
    for (int i = 0; i < 5; ++i) rig.Frame([] { Editor(true); }, ImVec2(-100, -100), false, sweep);
    rig.Frame([] { GImGui->NavWindow = nullptr; Editor(false); }, ImVec2(-100, -100), false, sweep);
}

void undrawn_modal_without_focus_blocks_clicks_and_sweep_frees_them() {
    {
        Rig rig;
        OpenGateThenLoseFocus(rig, {});
        for (int i = 0; i < 6; ++i) rig.Frame([] { Editor(false); }, ImVec2(-100, -100), false);
        CHECK(GImGui->OpenPopupStack.Size == 1);          // still open, never drawn: nothing on screen
        CHECK(ImGui::GetTopMostPopupModal() != nullptr);
        CHECK(!ClickButton(rig, {}));                     // and no click reaches the editor
    }
    Rig rig;
    std::vector<std::string> closed;
    auto sweep = [&] { okayed::modalguard::Sweep([&](const char* name) { closed.push_back(name); }); };
    OpenGateThenLoseFocus(rig, sweep);
    for (int i = 0; i < 6; ++i) rig.Frame([] { Editor(false); }, ImVec2(-100, -100), false, sweep);
    CHECK(GImGui->OpenPopupStack.Size == 0);
    CHECK(closed.size() == 1);
    CHECK(!closed.empty() && closed[0].find("Sign in to OkaySpace") != std::string::npos);
    CHECK(ClickButton(rig, sweep));
}

void sweep_leaves_a_modal_that_is_still_drawn() {
    Rig rig;
    int closed = 0;
    auto sweep = [&] { okayed::modalguard::Sweep([&](const char*) { ++closed; }); };
    for (int i = 0; i < 30; ++i) rig.Frame([] { Editor(true); }, ImVec2(-100, -100), false, sweep);
    CHECK(closed == 0);
    CHECK(ImGui::GetTopMostPopupModal() != nullptr);
    // and the modal does its job: the button behind it still takes no click
    bool clicked = false;
    rig.Frame([&] { clicked |= Editor(true); }, kButton, false, sweep);
    rig.Frame([&] { clicked |= Editor(true); }, kButton, true, sweep);
    rig.Frame([&] { clicked |= Editor(true); }, kButton, false, sweep);
    CHECK(!clicked);
}

void close_if_open_closes_the_named_modal_at_once() {
    Rig rig;
    for (int i = 0; i < 3; ++i) rig.Frame([] { Editor(true); }, ImVec2(-100, -100), false);
    bool did = false;
    rig.Frame([&] { did = okayed::modalguard::CloseIfOpen(kGate); Editor(false); }, ImVec2(-100, -100), false);
    CHECK(did);
    CHECK(GImGui->OpenPopupStack.Size == 0);
    bool again = true;
    rig.Frame([&] { again = okayed::modalguard::CloseIfOpen(kGate); Editor(false); }, ImVec2(-100, -100), false);
    CHECK(!again);   // nothing open: nothing done
    CHECK(ClickButton(rig, {}));
}

void rescue_brings_an_off_screen_modal_back() {
    Rig rig;
    const char* name = "Lost question";
    std::vector<std::string> moved;
    auto body = [&] {
        if (!ImGui::IsPopupOpen(name)) ImGui::OpenPopup(name);
        ImGui::SetNextWindowPos(ImVec2(5000, 5000), ImGuiCond_Appearing);
        if (ImGui::BeginPopupModal(name, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) { ImGui::TextUnformatted("Answer me"); ImGui::EndPopup(); }
    };
    auto sweep = [&] { okayed::modalguard::Sweep([](const char*) {}, [&](const char* n) { moved.push_back(n); }); };
    for (int i = 0; i < 6; ++i) rig.Frame(body, ImVec2(-100, -100), false, sweep);
    ImGuiWindow* w = ImGui::GetTopMostPopupModal();
    CHECK(w != nullptr);
    CHECK(moved.size() == 1);
    if (w) {
        const ImVec2 ds = ImGui::GetIO().DisplaySize;
        CHECK(w->Pos.x >= 0.0f && w->Pos.y >= 0.0f && w->Pos.x < ds.x && w->Pos.y < ds.y);
    }
}

void trail_and_probe_report() {
    Rig rig;
    okayed::modalguard::ModalTrail trail;
    std::vector<std::string> lines;
    for (int i = 0; i < 3; ++i) rig.Frame([&] { Editor(true); for (auto& l : trail.Update()) lines.push_back(l); }, ImVec2(-100, -100), false);
    CHECK(lines.size() == 1 && lines[0].find("Modal opened: Sign in to OkaySpace") == 0);
    // clicks arrive while the modal blocks the toolbar: they land on no window of the editor's (the modal is elsewhere)
    okayed::modalguard::InputProbe probe(1.0);
    std::string report;
    double t = 0.0;
    for (int i = 0; i < 90; ++i) {
        const bool down = (i % 10) == 5;
        if (down) probe.NoteClick(7, true);
        rig.Frame([&] { Editor(true); }, kButton, down);
        t += 1.0 / 60.0;
        const std::string l = probe.EndFrame(t);
        if (!l.empty() && report.empty()) report = l;
    }
    CHECK(report.find("Clicks land nowhere") == 0);
    CHECK(report.find("top modal \"Sign in to OkaySpace") != std::string::npos);
    // clicks that land on the toolbar write nothing
    okayed::modalguard::InputProbe ok(1.0);
    Rig rig2;
    std::string none;
    t = 0.0;
    for (int i = 0; i < 90; ++i) {
        const bool down = (i % 10) == 5;
        if (down) ok.NoteClick(7, true);
        rig2.Frame([&] { Editor(false); }, kButton, down);
        t += 1.0 / 60.0;
        none += ok.EndFrame(t);
    }
    CHECK(none.empty());
    // the modal closing is named too
    lines.clear();
    rig.Frame([&] { okayed::modalguard::CloseIfOpen(kGate); Editor(false); for (auto& l : trail.Update()) lines.push_back(l); }, ImVec2(-100, -100), false);
    CHECK(lines.size() == 1 && lines[0].find("Modal closed: Sign in to OkaySpace") == 0);
}

// The editor's two recovery questions, with the same Dear ImGui calls (the bodies are cut down to one line and the buttons).
bool g_recA = false, g_recB = false;   // "a newer autosave of the scene" / "a recovery copy of the open documents"
bool g_guarded = true;                 // false: the pattern the editor had (if (!IsPopupOpen) OpenPopup), to show the fight
bool g_clickedA = false, g_clickedB = false;
bool Want(const char* name) {
    if (g_guarded) return okayed::modalguard::OpenModalWhenFree(name);
    if (!ImGui::IsPopupOpen(name)) ImGui::OpenPopup(name);
    return true;
}
void DrawRecoveryPopupMirror() {
    if (g_recA && !Want("Recover Autosave")) return;
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Recover Autosave", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("A newer autosave was found for this scene:");
        if (ImGui::Button("Discard", ImVec2(130, 0))) { g_clickedA = true; g_recA = false; ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }
}
void DrawDocRecoveryPopupMirror() {
    if (!g_recB) return;
    if (!Want("Restore recovered work?")) return;
    ImGui::SetNextWindowSize(ImVec2(640, 0), ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal("Restore recovered work?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    ImGui::TextWrapped("The editor closed without a clean exit. A recovery copy from that session is still here.");
    if (ImGui::Button("Discard", ImVec2(120, 0))) { g_clickedB = true; g_recB = false; ImGui::CloseCurrentPopup(); }
    ImGui::EndPopup();
}
// The shown modal: open, drawn this frame, not hidden, with a rectangle on the screen. Null when none is shown.
ImGuiWindow* ShownModal() {
    ImGuiWindow* m = ImGui::GetTopMostPopupModal();
    if (!m || !m->Active || m->Hidden || m->Size.x <= 0.0f || m->Size.y <= 0.0f || okayed::modalguard::OffScreen(m)) return nullptr;
    return m;
}
// Click the "Discard" button of the modal shown now (found by its id inside the modal), press then release.
void ClickDiscard(Rig& rig, ImGuiWindow* m) {
    const ImRect r = m->Rect();
    // the button is the last item: bottom-left of the modal, inside its padding
    const ImVec2 at(r.Min.x + m->WindowPadding.x + 20.0f, r.Max.y - m->WindowPadding.y - 6.0f);
    auto frame = [] { DrawRecoveryPopupMirror(); DrawDocRecoveryPopupMirror(); };
    rig.Frame(frame, at, false);
    rig.Frame(frame, at, true);
    rig.Frame(frame, at, false);
}

void two_recovery_questions_fight_without_the_guard() {
    Rig rig;
    g_guarded = false; g_recA = g_recB = true; g_clickedA = g_clickedB = false;
    int shown = 0, open = 0;
    for (int i = 0; i < 20; ++i)
        rig.Frame([&] { DrawRecoveryPopupMirror(); DrawDocRecoveryPopupMirror(); if (ShownModal()) ++shown; if (ImGui::GetTopMostPopupModal()) ++open; },
                  ImVec2(-100, -100), false);
    CHECK(open == 20);   // a modal is open on every frame (every click behind it is blocked)...
    CHECK(shown == 0);   // ...and none is ever shown: the owner's "can't click anything"
    g_guarded = true;
}

void two_recovery_questions_one_at_a_time() {
    Rig rig;
    g_guarded = true; g_recA = g_recB = true; g_clickedA = g_clickedB = false;
    ImGuiWindow* m = nullptr;
    for (int i = 0; i < 4; ++i) rig.Frame([&] { DrawRecoveryPopupMirror(); DrawDocRecoveryPopupMirror(); m = ShownModal(); }, ImVec2(-100, -100), false);
    CHECK(m != nullptr);
    CHECK(m && std::string(m->Name) == "Recover Autosave");   // the scene's question first
    if (m) ClickDiscard(rig, m);
    CHECK(g_clickedA);
    for (int i = 0; i < 4; ++i) rig.Frame([&] { DrawRecoveryPopupMirror(); DrawDocRecoveryPopupMirror(); m = ShownModal(); }, ImVec2(-100, -100), false);
    CHECK(m != nullptr);
    CHECK(m && std::string(m->Name) == "Restore recovered work?");   // then the documents'
    if (m) ClickDiscard(rig, m);
    CHECK(g_clickedB);
    rig.Frame([] { DrawRecoveryPopupMirror(); DrawDocRecoveryPopupMirror(); }, ImVec2(-100, -100), false);
    CHECK(ImGui::GetTopMostPopupModal() == nullptr);
    for (int i = 0; i < 2; ++i) rig.Frame([] { Editor(false); }, ImVec2(-100, -100), false);   // the editor's window appears
    CHECK(ClickButton(rig, {}));   // and takes clicks again
}

void hidden_modal_is_reported_as_blocking() {
    Rig rig;
    g_guarded = false; g_recA = g_recB = true;
    std::vector<std::string> blocking;
    auto sweep = [&] { okayed::modalguard::Sweep({}, {}, [&](const char* n) { blocking.push_back(n); }); };
    for (int i = 0; i < okayed::modalguard::kHiddenFrames + 5; ++i)
        rig.Frame([] { DrawRecoveryPopupMirror(); DrawDocRecoveryPopupMirror(); }, ImVec2(-100, -100), false, sweep);
    CHECK(blocking.size() == 1);
    g_guarded = true; g_recA = g_recB = false;
}

int main() {
    RUN_SUITE("editor_stale_modal");
    imgui_closes_a_focused_undrawn_modal_by_itself();
    undrawn_modal_without_focus_blocks_clicks_and_sweep_frees_them();
    sweep_leaves_a_modal_that_is_still_drawn();
    close_if_open_closes_the_named_modal_at_once();
    rescue_brings_an_off_screen_modal_back();
    trail_and_probe_report();
    two_recovery_questions_fight_without_the_guard();
    two_recovery_questions_one_at_a_time();
    hidden_modal_is_reported_as_blocking();
    TEST_MAIN_RESULT();
}
