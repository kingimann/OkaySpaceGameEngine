// The parts of the Inspector's behaviour that depend on Dear ImGui itself, run in a window-less ImGui context (the same ImGui the editor
// builds against; no window, no graphics, mouse input is simulated):
//
//  1. Which right-click menu opens (finding 3 of the review). Two popups opened in one frame at the same level replace each other, and the
//     one asked for last wins. A field's menu is asked for while the cards are drawn, the empty-space menu at the end of the window, and text
//     has no widget id, so over a label or a gap "no widget is hovered" is true: the empty-space menu used to replace the field menu there.
//     This test draws rows the way the Inspector does and runs oked's ClickRouter (editor/InspectorClickRules.hpp) with the real ImGui calls
//     (IsMouseReleased, IsWindowHovered, IsAnyItemHovered, OpenPopup, BeginPopup) and checks that exactly ONE popup is open after each click.
//  2. Widget ids of two cards of one kind (finding 1b). Two components of the same kind (a duplicate) name their widgets alike ("Size##bc");
//     without an id scope per component the ids are equal and a drag on the copy's box moves the original's too.
//     editor/ComponentIdScope.hpp, which main.cpp puts around every card, makes them different.
#include "test_framework.hpp"
#include "ComponentIdScope.hpp"
#include "InspectorClickRules.hpp"
#include "imgui.h"
#include "imgui_internal.h"
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace ctx = okay::inspectorctx;

namespace {

struct Harness {
    Harness() {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = ImVec2(800, 600);
        unsigned char* px = nullptr;
        int w = 0, h = 0;
        io.Fonts->GetTexDataAsRGBA32(&px, &w, &h);   // builds the font atlas; nothing is drawn
        for (int i = 0; i < 4; ++i) Frame([] {});
    }
    ~Harness() { ImGui::DestroyContext(); }
    Harness(const Harness&) = delete;
    Harness& operator=(const Harness&) = delete;
    int frame = 0;
    void Frame(const std::function<void()>& body) {
        ImGuiIO& io = ImGui::GetIO();
        io.DeltaTime = 1.0f / 60.0f;
        ImGui::NewFrame();
        ++frame;
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(500, 320));
        ImGui::Begin("Inspector", nullptr, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar);
        body();
        ImGui::End();
        ImGui::Render();
    }
    int OpenPopups() const { return ImGui::GetCurrentContext()->OpenPopupStack.Size; }
    void Move(ImVec2 p) { ImGui::GetIO().AddMousePosEvent(p.x, p.y); }
    void Press(ImGuiMouseButton b) { ImGui::GetIO().AddMouseButtonEvent(b, true); }
    void Release(ImGuiMouseButton b) { ImGui::GetIO().AddMouseButtonEvent(b, false); }
};

// ---- 1. the right-click menus ---------------------------------------------------------------------------------------------------

struct RowSpot { ImVec2 box, label, gap; bool hasMenu = false; };

struct Panel {
    Harness h;
    ctx::ClickRouter router;
    std::vector<RowSpot> spots = std::vector<RowSpot>(3);
    ImVec2 empty;
    float values[3] = {1, 2, 3};
    bool useRouter = true;
    std::string opened;   // which popup's content was drawn in the last frame ("" = none)

    // One frame of the Inspector: three rows (two with a field menu, one without), then the empty-space menu at the end of the window.
    void Draw() {
        h.Frame([&] {
            opened.clear();
            for (int i = 0; i < 3; ++i) {
                RowSpot& spot = spots[(std::size_t)i];
                ImGui::PushID(i);
                const ImVec2 rowStart = ImGui::GetCursorScreenPos();
                ImGui::SetNextItemWidth(120);
                ImGui::DragFloat("##v", &values[i]);
                spot.box = ImVec2(ImGui::GetItemRectMin().x + 10, ImGui::GetItemRectMin().y + 4);
                ImGui::SameLine();
                ImGui::TextUnformatted("Label");   // text: no widget id
                spot.label = ImVec2(ImGui::GetItemRectMin().x + 8, ImGui::GetItemRectMin().y + 4);
                spot.gap = ImVec2(ImGui::GetItemRectMax().x + 120, ImGui::GetItemRectMin().y + 4);   // right of the label: nothing is drawn there
                spot.hasMenu = i != 2;
                // --- FieldMark, as InspectorPanel.cpp calls it
                const ImVec2 mouse = ImGui::GetIO().MousePos;
                const ctx::RightClick click{ImGui::IsMouseReleased(ImGuiMouseButton_Right), mouse.x, mouse.y};
                const ctx::Rect row = ctx::FieldRowRect(ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMin().x,
                                                        ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x, rowStart.y,
                                                        ImGui::GetItemRectMin().y, ImGui::GetItemRectMax().y, ImGui::GetStyle().FramePadding.y);
                bool opens;
                if (useRouter) opens = router.FieldOpensInRow(h.frame, click, ImGui::IsWindowHovered(), row, spot.hasMenu);
                else opens = click.released && ImGui::IsWindowHovered() && ctx::Contains(row, mouse.x, mouse.y) && spot.hasMenu;   // no claim
                if (opens) ImGui::OpenPopup("##field");
                if (ImGui::BeginPopup("##field")) { opened = "field " + std::to_string(i); ImGui::TextUnformatted("field menu"); ImGui::EndPopup(); }
                ImGui::PopID();
            }
            ImGui::Dummy(ImVec2(0, 100));
            empty = ImVec2(ImGui::GetCursorScreenPos().x + 60, ImGui::GetCursorScreenPos().y + 10);
            // --- DrawInspectorBackgroundMenu
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            const ctx::RightClick click{ImGui::IsMouseReleased(ImGuiMouseButton_Right), mouse.x, mouse.y};
            if (useRouter) {
                if (router.BackgroundOpens(h.frame, click, ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup), ImGui::IsAnyItemHovered()))
                    ImGui::OpenPopup("##bg");
                if (ImGui::BeginPopup("##bg")) { opened = "empty space"; ImGui::TextUnformatted("empty-space menu"); ImGui::EndPopup(); }
            } else {   // the old code
                if (ImGui::BeginPopupContextWindow("##bg", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
                    opened = "empty space"; ImGui::TextUnformatted("empty-space menu"); ImGui::EndPopup();
                }
            }
        });
    }
    void Settle() { Draw(); Draw(); }
    void RightClickAt(ImVec2 p) {
        h.Move(p); Settle();
        h.Press(ImGuiMouseButton_Right); Draw();
        h.Release(ImGuiMouseButton_Right); Settle();
    }
    void CloseAll() {   // a left click on a part of the window that has nothing
        h.Move(ImVec2(490, 312)); Settle();
        h.Press(ImGuiMouseButton_Left); Draw();
        h.Release(ImGuiMouseButton_Left); Settle();
    }
};

void TestOnePopupPerClick() {
    RUN_SUITE("a right click opens exactly one menu (Dear ImGui, window-less)");
    Panel p;
    for (int i = 0; i < 6; ++i) p.Draw();
    CHECK(p.h.OpenPopups() == 0);
    CHECK(p.spots[0].hasMenu && p.spots[1].hasMenu && !p.spots[2].hasMenu);
    // Rows 0 and 1 have a field menu: the number box, the label text and the gap right of it all reach it, and nothing else opens.
    for (int row = 0; row < 2; ++row) {
        const RowSpot s = p.spots[(std::size_t)row];
        for (const ImVec2 at : {s.box, s.label, s.gap}) {
            p.RightClickAt(at);
            CHECK(p.h.OpenPopups() == 1);
            CHECK(p.opened == "field " + std::to_string(row));
            p.CloseAll();
            CHECK(p.h.OpenPopups() == 0);
        }
    }
    // Row 2 has no field menu. Its box has a say of its own (nothing opens); its label and gap are not a field, so the empty-space menu opens.
    const RowSpot none = p.spots[2];
    p.RightClickAt(none.box);
    CHECK(p.h.OpenPopups() == 0);
    p.CloseAll();
    for (const ImVec2 at : {none.label, none.gap}) {
        p.RightClickAt(at);
        CHECK(p.h.OpenPopups() == 1 && p.opened == "empty space");
        p.CloseAll();
    }
    // The empty space: the empty-space menu, once.
    p.RightClickAt(p.empty);
    CHECK(p.h.OpenPopups() == 1 && p.opened == "empty space");
    // With that menu open, a right click on a field label replaces it by the field's menu: still exactly one.
    p.RightClickAt(p.spots[1].label);
    CHECK(p.h.OpenPopups() == 1 && p.opened == "field 1");
    p.RightClickAt(p.empty);
    CHECK(p.h.OpenPopups() == 1 && p.opened == "empty space");
    p.CloseAll();
    // A release with no press over the window (the button was pressed elsewhere): the same rules, still one menu.
    p.RightClickAt(p.spots[0].label);
    CHECK(p.h.OpenPopups() == 1);
}

void TestWhatTheOldCodeDid() {
    RUN_SUITE("the old code: the label of a row opened the empty-space menu (the reported bug)");
    Panel p;
    p.useRouter = false;
    for (int i = 0; i < 6; ++i) p.Draw();
    // The number box worked (a widget is under the mouse, so the empty-space menu stays out of it) ...
    p.RightClickAt(p.spots[1].box);
    CHECK(p.h.OpenPopups() == 1 && p.opened == "field 1");
    p.CloseAll();
    // ... the label did not: both menus were asked for in the same frame and the empty-space menu, asked for last, replaced the field's.
    p.RightClickAt(p.spots[1].label);
    std::printf("  old code, right click on a label: %s menu is open\n", p.opened.c_str());
    CHECK(p.opened == "empty space");
    p.CloseAll();
    p.RightClickAt(p.spots[1].gap);
    CHECK(p.opened == "empty space");
}

// ---- 2. widget ids of two cards -------------------------------------------------------------------------------------------------

void TestWidgetIdsOfTwoCards() {
    RUN_SUITE("two components of one kind do not share widget ids (Dear ImGui, window-less)");
    Harness h;
    int a = 0, b = 0;   // two components: their addresses are the scope keys
    ImGuiID plainFirst = 0, plainSecond = 0, scopedA = 0, scopedB = 0;
    h.Frame([&] {
        plainFirst = ImGui::GetID("Size##bc");
        plainSecond = ImGui::GetID("Size##bc");
        { const oked::ComponentIdScope s(&a); scopedA = ImGui::GetID("Size##bc"); }
        { const oked::ComponentIdScope s(&b); scopedB = ImGui::GetID("Size##bc"); }
    });
    CHECK(plainFirst == plainSecond);                        // the same label in the same scope is the same widget to ImGui
    CHECK(scopedA != scopedB);                               // a scope per component makes them different ...
    CHECK(scopedA != plainFirst && scopedB != plainFirst);   // ... and different from the unscoped one

    // What a person sees: two cards each with a "Size" box; drag the second card's box to the right.
    for (const bool scoped : {false, true}) {
        Harness& g = h;   // one context for the whole test
        float first = 1.0f, second = 1.0f;
        ImVec2 box2;
        const auto draw = [&] {
            g.Frame([&] {
                for (int i = 0; i < 2; ++i) {
                    if (scoped) ImGui::PushID(i == 0 ? (const void*)&a : (const void*)&b);
                    ImGui::SetNextItemWidth(150);
                    ImGui::DragFloat("Size##bc", i == 0 ? &first : &second, 0.1f);
                    if (i == 1) box2 = ImVec2(ImGui::GetItemRectMin().x + 40, ImGui::GetItemRectMin().y + 4);
                    if (scoped) ImGui::PopID();
                }
            });
        };
        for (int i = 0; i < 4; ++i) draw();
        g.Move(box2); draw(); draw();
        g.Press(ImGuiMouseButton_Left); draw();
        for (int step = 1; step <= 6; ++step) { g.Move(ImVec2(box2.x + 10.0f * step, box2.y)); draw(); }
        g.Release(ImGuiMouseButton_Left); draw(); draw();
        const float afterFirst = first, afterSecond = second;
        g.Move(ImVec2(450, 300));
        for (int i = 0; i < 40; ++i) draw();   // longer than a double click: the next press must not count as one (a double click on a drag box starts typing)
        std::printf("  %s: after dragging the second box, first = %g, second = %g\n", scoped ? "with an id scope per component   " : "without an id scope (the old code)", afterFirst, afterSecond);
        if (scoped) {
            CHECK(afterSecond != 1.0f);        // the dragged box moved ...
            CHECK(afterFirst == 1.0f);         // ... and the other card's did not
        } else {
            CHECK(afterSecond != 1.0f);
            CHECK(afterFirst != 1.0f);         // what used to happen: the original followed the copy
        }
    }
}

}   // namespace

int main() {
    TestOnePopupPerClick();
    TestWhatTheOldCodeDid();
    TestWidgetIdsOfTwoCards();
    TEST_MAIN_RESULT();
}
