// editor/ModalGuard.hpp, the editor's guard over OkayUI's popups (the editor is built on OkayUI only; its Dear ImGui part was deleted on 2026-11-27).
// Checks, headless (OkayUI only, the stand-in imgui.h of okayui/probe first on the include path):
//  1. OpenModalWhenFree: the first question keeps the screen, the second waits and opens once the first is answered (the two recovery
//     questions of editor/main.cpp, which replaced each other every frame in the Dear ImGui build before the guard);
//  2. an open modal that is no longer drawn is closed by Sweep after kUndrawnFrames and named; one still drawn is left alone;
//  3. CloseIfOpen closes a named modal at once;
//  4. an undrawn OkayUI modal does not block clicks (OkayUI's rule: the blocking overlay is registered only when it is drawn);
//  5. ModalTrail names modals opening and closing; DescribeInputState names the top modal.
// The questions are asked outside every window, as the editor asks them (a popup's id, like Dear ImGui's, depends on where it is named).
// Written from the guard's own contract (its header comments), not from any other project's code.
#include "test_framework.hpp"
#include "OkayUITestKit.hpp"

#include "imgui.h"
#include "imgui_internal.h"
#include "ModalGuard.hpp"

#include <string>
#include <vector>

using namespace okayuitest;
namespace mg = okayed::modalguard;

namespace {

// The editor's two recovery questions, as the editor draws them: each asks for its modal every frame it is wanted.
struct Questions {
    bool wantA = true, wantB = true;
    bool shownA = false, shownB = false;
    OkayUI::Rect btn{};
    void Draw() {
        shownA = shownB = false;
        if (wantA && mg::OpenModalWhenFree("Recover Autosave")) {
            if (ImGui::BeginPopupModal("Recover Autosave", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                shownA = true;
                const bool hit = ImGui::Button("Discard##a"); btn = OkayUI::GetItemRect();
                if (hit) { wantA = false; ImGui::CloseCurrentPopup(); }
                ImGui::EndPopup();
            }
        }
        if (wantB && mg::OpenModalWhenFree("Restore recovered work?")) {
            if (ImGui::BeginPopupModal("Restore recovered work?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                shownB = true;
                const bool hit = ImGui::Button("Discard##b"); btn = OkayUI::GetItemRect();
                if (hit) { wantB = false; ImGui::CloseCurrentPopup(); }
                ImGui::EndPopup();
            }
        }
    }
};

void Window(const std::function<void()>& body) {
    if (OkayUI::Begin("Main", 0, 0, 640, 480)) body();
    OkayUI::End();
}

void one_question_at_a_time() {
    Reset();
    Sim sim(640, 480);
    Questions q;
    auto ui = [&] { Window([] {}); q.Draw(); };   // the questions are asked outside every window, as the editor asks them
    sim.Frames(4, ui);
    CHECK(q.shownA && !q.shownB);                       // the first one asked holds the screen, the second waits
    CHECK(ImGui::IsPopupOpen("Recover Autosave") && !ImGui::IsPopupOpen("Restore recovered work?"));
    sim.Frames(10, ui);
    CHECK(q.shownA && !q.shownB);                       // and they do not swap
    sim.Click(CenterX(q.btn), CenterY(q.btn), ui);      // answer the first
    CHECK(!q.wantA);
    sim.Frames(3, ui);
    CHECK(q.shownB && !q.shownA);                       // now the second
}

void sweep_closes_an_undrawn_modal_and_keeps_a_drawn_one() {
    Reset();
    Sim sim(640, 480);
    bool draw = true;
    std::vector<std::string> closed;
    auto ui = [&] {
        mg::Sweep([&](const char* n) { closed.push_back(n); });
        if (draw && mg::OpenModalWhenFree("Open a project")) {
            if (ImGui::BeginPopupModal("Open a project")) { ImGui::Text("pick one"); ImGui::EndPopup(); }
        }
    };
    sim.Frames(mg::kUndrawnFrames + 6, ui);
    CHECK(closed.empty());                              // drawn every frame: left alone
    CHECK(ImGui::IsPopupOpen("Open a project"));
    draw = false;                                       // the gate returns early without closing its modal
    sim.Frames(mg::kUndrawnFrames + 3, ui);
    CHECK(closed.size() == 1 && closed[0] == "Open a project");
    CHECK(!ImGui::IsPopupOpen("Open a project"));
}

void close_if_open() {
    Reset();
    Sim sim(640, 480);
    auto ui = [&] { if (mg::OpenModalWhenFree("Sign in")) { if (ImGui::BeginPopupModal("Sign in")) ImGui::EndPopup(); } };
    sim.Frames(2, ui);
    CHECK(ImGui::IsPopupOpen("Sign in"));
    CHECK(mg::CloseIfOpen("Sign in"));
    CHECK(!ImGui::IsPopupOpen("Sign in"));
    CHECK(!mg::CloseIfOpen("Sign in"));                 // not open any more: nothing to close
    CHECK(!mg::CloseIfOpen("never opened"));
}

void undrawn_modal_does_not_block_clicks() {
    Reset();
    Sim sim(640, 480);
    bool draw = true; int clicks = 0; OkayUI::Rect b{};
    auto ui = [&] {
        Window([&] {
            if (OkayUI::Button("Behind")) ++clicks;
            b = OkayUI::GetItemRect();
        });
        if (draw && mg::OpenModalWhenFree("Q")) { if (ImGui::BeginPopupModal("Q")) ImGui::EndPopup(); }
    };
    sim.Frames(3, ui);
    sim.Click(CenterX(b), CenterY(b), ui);
    CHECK(clicks == 0);                                 // a drawn modal blocks the button behind it
    draw = false;                                       // still open, no longer drawn
    sim.Frames(2, ui);
    CHECK(ImGui::IsPopupOpen("Q"));
    sim.Click(CenterX(b), CenterY(b), ui);
    CHECK(clicks == 1);                                 // OkayUI: an undrawn modal blocks nothing
}

void trail_and_description() {
    Reset();
    Sim sim(640, 480);
    mg::ModalTrail trail;
    bool want = true;
    std::vector<std::string> lines;
    auto ui = [&] {
        if (want && mg::OpenModalWhenFree("Save changes?")) { if (ImGui::BeginPopupModal("Save changes?")) ImGui::EndPopup(); }
        for (const std::string& l : trail.Update()) lines.push_back(l);
    };
    sim.Frames(2, ui);
    CHECK(!lines.empty() && lines[0] == "Modal opened: Save changes?");
    const std::string d = mg::DescribeInputState();
    CHECK(d.find("top modal \"Save changes?\"") != std::string::npos);
    want = false; mg::CloseIfOpen("Save changes?");
    sim.Frames(1, ui);
    CHECK(lines.back() == "Modal closed: Save changes?");
    CHECK(mg::DescribeInputState().find("no modal") != std::string::npos);
}

// The editor asks its questions after its dock host window has ended (DrawProjectGate runs outside every window): a modal is a top-level
// window, so it must open and draw there too, and block the windows behind it.
void modal_outside_any_window() {
    Reset();
    Sim sim(640, 480);
    int shown = 0, clicks = 0; OkayUI::Rect b{}, ok{};
    bool want = true;
    auto ui = [&] {
        Window([&] { if (OkayUI::Button("Behind")) ++clicks; b = OkayUI::GetItemRect(); });
        if (want && mg::OpenModalWhenFree("Open a project")) {
            if (ImGui::BeginPopupModal("Open a project", nullptr, ImGuiWindowFlags_NoMove)) {
                ++shown; ImGui::Text("pick a project");
                if (ImGui::Button("OK")) { want = false; ImGui::CloseCurrentPopup(); }
                ok = OkayUI::GetItemRect();
                ImGui::EndPopup();
            }
        }
    };
    sim.Frames(3, ui);
    CHECK(shown >= 2);
    CHECK(ImGui::IsPopupOpen("Open a project"));
    sim.Click(CenterX(b), CenterY(b), ui);
    CHECK(clicks == 0);                                 // the window behind is blocked
    sim.Click(CenterX(ok), CenterY(ok), ui);
    CHECK(!want && !ImGui::IsPopupOpen("Open a project"));
    sim.Click(CenterX(b), CenterY(b), ui);
    CHECK(clicks == 1);
}

} // namespace

int main() {
    RUN_SUITE("okayui_port_modalguard");
    one_question_at_a_time();
    sweep_closes_an_undrawn_modal_and_keeps_a_drawn_one();
    close_if_open();
    undrawn_modal_does_not_block_clicks();
    trail_and_description();
    modal_outside_any_window();
    TEST_MAIN_RESULT();
}
