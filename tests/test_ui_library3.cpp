// UI component library, slice 3: the app parts (menu bar, toolbar, tree view, sortable data table, wizard, form group) and the
// colour picker's advanced mode (hue / saturation square, value and alpha sliders). Headless: the maths, the pointer and key
// paths through scene.Update, the variables each part writes, and a scene save and load round trip for each part.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Components/UIAppParts.hpp"
#include "okay/Components/UIColorPicker.hpp"
#include "okay/Components/NativeUIActions.hpp"
#include "okay/Components/UIVars.hpp"
#include "okay/Render/ColorHSV.hpp"
#include "okay/Scene/PropertyRegistry.hpp"
#include <cmath>
#include <cstdio>
#include <string>

using namespace okay;

namespace {

// A click: the pointer goes down and up at (x, y) on the canvas.
void Click(Scene& s, float x, float y) {
    Input::FeedMouse({x, y}, 0);
    Input::FeedMouse({x, y}, 1u << 0);
    s.Update(0.016f);
    Input::FeedMouse({x, y}, 0);
    s.Update(0.016f);
}
void Move(Scene& s, float x, float y) {
    Input::FeedMouse({x, y}, 0);
    s.Update(0.016f);
}
void Key(Scene& s, char k) {
    Input::FeedKeys({});
    Input::FeedKeys({k});
    s.Update(0.016f);
    Input::FeedKeys({});
    s.Update(0.016f);
}
void Wheel(Scene& s, float notches) {
    Input::FeedMouseWheel(notches);
    s.Update(0.016f);
    Input::ClearTypedText();
}

template <class Check>
bool RoundTrip(Scene& s, const char* name, Check check) {
    const std::string text = SceneSerializer::Serialize(s);
    Scene back("back");
    std::string err;
    if (!SceneSerializer::Deserialize(back, text, &err, nullptr)) { std::printf("  load failed: %s\n", err.c_str()); return false; }
    if (SceneSerializer::Serialize(back) != text) { std::printf("  second save differs for %s\n", name); return false; }
    return check(back.Find(name));
}

int ChildCount(GameObject* g) { return (int)g->transform->Children().size(); }

} // namespace

int main() {
    RUN_SUITE("ui_library3");
    UICanvas::Set(1280.0f, 720.0f);
    ActionList::ResetVars();

    // ===== Helpers ================================================================================================
    {
        const auto v = uiapp::Split("a|b||c", '|');
        CHECK(v.size() == 4 && v[2].empty());
        CHECK(uiapp::Split("a|b||c", '|', false).size() == 3);
        CHECK(uiapp::Key("Last name") == "last_name");
        CHECK(uiapp::Key("  E-mail!! ") == "e_mail");
        double d = 0;
        CHECK(uiapp::ToNumber("-3.5", d) && d == -3.5);
        CHECK(!uiapp::ToNumber("nan", d) && !uiapp::ToNumber("12abc", d) && !uiapp::ToNumber("", d));
    }

    // ===== Colour maths ===========================================================================================
    {
        using namespace colorhsv;
        const HSV red = FromRGB(1, 0, 0);
        CHECK_NEAR(red.h, 0.0f, 1e-4); CHECK_NEAR(red.s, 1.0f, 1e-4); CHECK_NEAR(red.v, 1.0f, 1e-4);
        const HSV green = FromRGB(0, 1, 0); CHECK_NEAR(green.h, 120.0f, 1e-3);
        const HSV blue = FromRGB(0, 0, 1);  CHECK_NEAR(blue.h, 240.0f, 1e-3);
        const HSV grey = FromRGB(0.5f, 0.5f, 0.5f); CHECK_NEAR(grey.s, 0.0f, 1e-5); CHECK_NEAR(grey.v, 0.5f, 1e-5);
        const Color c = ToColor(120, 1, 1);
        CHECK_NEAR(c.r, 0.0f, 1e-4); CHECK_NEAR(c.g, 1.0f, 1e-4); CHECK_NEAR(c.b, 0.0f, 1e-4);
        const Color w = ToColor(77, 0, 1); CHECK_NEAR(w.r, 1.0f, 1e-4); CHECK_NEAR(w.g, 1.0f, 1e-4); CHECK_NEAR(w.b, 1.0f, 1e-4);
        const Color wrap = ToColor(480, 1, 1); CHECK_NEAR(wrap.g, 1.0f, 1e-4);   // 480 degrees is 120
        bool allClose = true;
        for (int r = 0; r <= 4; ++r) for (int g = 0; g <= 4; ++g) for (int b = 0; b <= 4; ++b) {   // every colour survives a round trip
            const float R = r / 4.0f, G = g / 4.0f, B = b / 4.0f;
            const HSV h = FromRGB(R, G, B);
            const Color back = ToColor(h.h, h.s, h.v);
            if (std::fabs(back.r - R) > 1e-4f || std::fabs(back.g - G) > 1e-4f || std::fabs(back.b - B) > 1e-4f) allClose = false;
        }
        CHECK(allClose);
        CHECK(Hex(Color(1, 0, 0, 1)) == "#FF0000");
        CHECK(Hex(Color(0, 1, 0, 0.5f)) == "#00FF0080");
    }

    // ===== Menu bar ===============================================================================================
    {
        Scene s("Menu"); s.physicsEnabled = false;
        GameObject* g = s.CreateGameObject("Bar");
        auto* w = g->AddComponent<UIMenuBar>();
        w->anchor = UIAnchor::TopLeft; w->position = {0, 0}; w->size = {640, 32};
        w->menus = "File:New|Open@Ctrl+O|-|~Export|Quit;Edit:Undo|Redo;Help:About";
        w->chosenVar = "menu_pick";
        auto* sc = g->AddComponent<ScriptComponent>("okayscript");
        CHECK(sc->LoadSource("function on_select() { set_x(7); }"));
        CHECK(w->MenuCount() == 3);
        CHECK(w->Parsed()[0].name == "File" && w->EntryCount(0) == 5);
        CHECK(w->Parsed()[0].entries[1].shortcut == "Ctrl+O");
        CHECK(w->Parsed()[0].entries[2].separator);
        CHECK(w->Parsed()[0].entries[3].disabled && w->Parsed()[0].entries[3].label == "Export");
        // geometry: names sit side by side; a dropdown entry is found by its row
        CHECK(w->MenuAt(w->MenuX(1) + 4, 10) == 1);
        CHECK(w->MenuAt(w->MenuX(1) + 4, 40) == -1);                  // below the bar is not a menu name
        CHECK(w->MenuX(1) > w->MenuX(0) + w->MenuW(0) - 0.01f);
        CHECK(w->DropWidth(0) >= w->menuWidth);
        CHECK(w->EntryAt(0, w->MenuX(0) + 5, w->EntryY(0, 1) + 3) == 1);
        CHECK(w->EntryAt(0, w->MenuX(0) + 5, w->EntryY(0, 2) + 3) == -1);   // a separator is not an entry
        s.Start();
        CHECK(!w->IsOpen());
        Click(s, w->MenuX(0) + 5, 10);                                // open File
        CHECK(w->IsOpen() && w->OpenIndex() == 0);
        Move(s, w->MenuX(0) + 5, w->EntryY(0, 1) + 3);
        CHECK(w->Hovered() == 1);
        Move(s, w->MenuX(1) + 5, 10);                                 // sliding over Edit switches menus
        CHECK(w->OpenIndex() == 1);
        Click(s, w->MenuX(1) + 5, 10);                                // a click on the open name closes it
        CHECK(!w->IsOpen());
        Click(s, w->MenuX(0) + 5, 10);
        Click(s, w->MenuX(0) + 5, w->EntryY(0, 3) + 3);               // the greyed entry does nothing, the menu stays
        CHECK(w->ChosenPath().empty());
        Click(s, w->MenuX(0) + 5, w->EntryY(0, 1) + 3);               // Open
        CHECK(!w->IsOpen());
        CHECK(w->ChosenPath() == "File/Open");
        CHECK(uivars::Read(g, "menu_pick_text").text == "File/Open");
        float v = -1; CHECK(uivars::ReadNumber(g, "menu_pick", v) && v == 1.0f);
        CHECK_NEAR(g->transform->localPosition.x, 7.0f, 0.001f);      // on_select ran
        Click(s, w->MenuX(0) + 5, 10);                                // click outside closes
        CHECK(w->IsOpen());
        Click(s, 900, 400);
        CHECK(!w->IsOpen());
        // keys
        w->Open(0);
        Key(s, Input::KeyDown);  CHECK(w->Hovered() == 0);
        Key(s, Input::KeyDown);  CHECK(w->Hovered() == 1);
        Key(s, Input::KeyDown);  CHECK(w->Hovered() == 4);            // skips the separator and the greyed Export
        Key(s, Input::KeyUp);    CHECK(w->Hovered() == 1);
        Key(s, Input::KeyRight); CHECK(w->OpenIndex() == 1);
        Key(s, Input::KeyLeft);  CHECK(w->OpenIndex() == 0);
        Key(s, (char)27);        CHECK(!w->IsOpen());
        w->Open(1); w->StepHover(1);
        Key(s, '\r');
        CHECK(w->ChosenPath() == "Edit/Undo");
        CHECK(!w->Choose(0, 2) && !w->Choose(0, 3) && !w->Choose(9, 0) && w->Choose(2, 0));
        CHECK(w->ChosenPath() == "Help/About");
        CHECK(InvokeNativeUIAction(g, "Open", 1) && w->IsOpen() && w->OpenIndex() == 1);
        CHECK(InvokeNativeUIAction(g, "Close", 0) && !w->IsOpen());
        // a menu that is open takes the click, so a table under it does not also see it
        GameObject* tg = s.CreateGameObject("Under");
        auto* t = tg->AddComponent<UIDataTable>();
        t->anchor = UIAnchor::TopLeft; t->position = {0, 40}; t->size = {400, 200};
        t->rows = "A|1;B|2;C|3;D|4"; t->columns = "Name|Num";
        s.Start();
        w->Open(0);
        const float ey = w->EntryY(0, 4) + 3;                         // an entry that sits over the table's rows
        Click(s, w->MenuX(0) + 5, ey);
        CHECK(w->ChosenPath() == "File/Quit");
        CHECK(t->selected == -1);
        Click(s, 300, 40 + t->headerHeight + 3);                      // now nothing is open: the click selects a row
        CHECK(t->selected == 0);
        // save and load
        w->menus = "A:x|y;B:z"; w->itemHeight = 31; w->fontScale = 2.0f; w->interactable = false; w->accent = Color(0.1f, 0.2f, 0.3f, 1.0f);
        CHECK(RoundTrip(s, "Bar", [&](GameObject* o) {
            auto* r = o ? o->GetComponent<UIMenuBar>() : nullptr;
            return r && r->menus == "A:x|y;B:z" && r->itemHeight == 31 && r->fontScale == 2.0f && !r->interactable && r->chosenVar == "menu_pick" && r->accent.g > 0.19f && r->accent.g < 0.21f;
        }));
    }

    // ===== Toolbar ================================================================================================
    {
        ActionList::ResetVars();
        Scene s("Tool"); s.physicsEnabled = false;
        GameObject* g = s.CreateGameObject("Tools");
        auto* w = g->AddComponent<UIToolbar>();
        w->anchor = UIAnchor::TopLeft; w->position = {10, 50}; w->size = {900, 40};
        w->items = "New@N|~Open|-|Bold*|Italic*|-|Left=|Center=|Right=";
        w->states = "000010000";
        w->chosenVar = "tool_pick"; w->varPrefix = "tb_";
        auto* sc = g->AddComponent<ScriptComponent>("okayscript");
        CHECK(sc->LoadSource("function on_select() { set_y(4); }"));
        CHECK(w->Count() == 9);
        CHECK(w->Parsed()[0].icon == "N" && w->Parsed()[0].label == "New");
        CHECK(w->Parsed()[1].disabled);
        CHECK(w->Parsed()[2].separator);
        CHECK(w->Parsed()[3].toggle && w->Parsed()[6].radio);
        s.Start();
        float v = -1;
        CHECK(uivars::ReadNumber(g, "tb_bold", v) && v == 0.0f);      // the start state is written to the variables
        CHECK(uivars::ReadNumber(g, "tb_center", v) && v == 0.0f);
        CHECK(w->IsOn(4));                                            // Italic starts on
        CHECK(uivars::ReadNumber(g, "tb_italic", v) && v == 1.0f);
        // geometry: every button has its own box, the separator has none
        CHECK(w->ItemAt(w->ItemX(0) + 2, 20) == 0);
        CHECK(w->ItemAt(w->ItemX(2) + 2, 20) == -1);
        CHECK(w->ItemX(3) >= w->ItemX(2) + 12.0f);
        auto at = [&](int i) { return Vec2{10 + w->ItemX(i) + 3, 50 + 20}; };
        Click(s, at(3).x, at(3).y);                                   // Bold on
        CHECK(w->IsOn(3));
        CHECK(uivars::ReadNumber(g, "tb_bold", v) && v == 1.0f);
        CHECK(uivars::Read(g, "tool_pick").text == "Bold");
        CHECK_NEAR(g->transform->localPosition.y, 4.0f, 1e-3);
        Click(s, at(3).x, at(3).y);                                   // Bold off
        CHECK(!w->IsOn(3));
        CHECK(uivars::ReadNumber(g, "tb_bold", v) && v == 0.0f);
        Click(s, at(1).x, at(1).y);                                   // the greyed button does nothing
        CHECK(w->Chosen() == 3);
        Click(s, at(7).x, at(7).y);                                   // Center: the others of the group go off
        CHECK(w->IsOn(7) && !w->IsOn(6) && !w->IsOn(8));
        Click(s, at(8).x, at(8).y);
        CHECK(w->IsOn(8) && !w->IsOn(7));
        CHECK(uivars::ReadNumber(g, "tb_center", v) && v == 0.0f);
        CHECK(uivars::ReadNumber(g, "tb_right", v) && v == 1.0f);
        CHECK(w->states.size() == 9 && w->states[8] == '1' && w->states[4] == '1' && w->states[3] == '0');
        CHECK(!w->Press(2) && !w->Press(1) && !w->Press(99));
        CHECK(w->Press(0) && !w->IsOn(0));                            // a plain button never stays on
        w->SetOn(6, true);                                            // a script can flip a state
        CHECK(w->IsOn(6) && uivars::ReadNumber(g, "tb_left", v) && v == 1.0f);
        w->showLabels = true;
        CHECK(w->ItemWidth(0) > w->ButtonSize());                     // a label makes the button wider
        w->varPrefix = "tb_"; w->items = "A@a|B@b"; w->states = "";
        CHECK(!w->IsOn(0) && w->Count() == 2);
        w->items = "New@N|~Open|-|Bold*|Italic*|-|Left=|Center=|Right="; w->states = "000010001"; w->showLabels = false; w->interactable = true;
        CHECK(RoundTrip(s, "Tools", [&](GameObject* o) {
            auto* r = o ? o->GetComponent<UIToolbar>() : nullptr;
            return r && r->items == w->items && r->states == "000010001" && !r->showLabels && r->varPrefix == "tb_" && r->chosenVar == "tool_pick";
        }));
    }

    // ===== Tree view ==============================================================================================
    {
        ActionList::ResetVars();
        Scene s("Tree"); s.physicsEnabled = false;
        GameObject* g = s.CreateGameObject("Tree");
        auto* w = g->AddComponent<UITreeView>();
        w->anchor = UIAnchor::TopLeft; w->position = {10, 10}; w->size = {300, 130};
        w->nodes = "World|>Forest|>>Wolf den|>>Old oak|>Castle|>>Gate|Settings|>Audio|>>>Deep";
        w->expanded = ""; w->selectedVar = "tree_pick";
        auto* sc = g->AddComponent<ScriptComponent>("okayscript");
        CHECK(sc->LoadSource("function on_select() { set_z(2); }"));
        CHECK(w->Count() == 9);
        CHECK(w->Depth(2) == 2 && w->Parent(2) == 1 && w->Parent(1) == 0 && w->Parent(0) == -1);
        CHECK(w->HasChildren(0) && w->HasChildren(1) && !w->HasChildren(2) && w->HasChildren(6));
        CHECK(w->Depth(8) == 2 && w->Parent(8) == 7);                 // >>> under a row one level down is held to one more level
        CHECK(w->Path(2) == "World/Forest/Wolf den");
        CHECK(w->VisibleCount() == 2);                                // World and Settings (everything else is folded)
        w->expanded = "0";
        CHECK(w->VisibleCount() == 4);                                // + Forest, Castle
        w->expanded = "0,1,6";
        CHECK(w->VisibleCount() == 7);
        w->expanded = "0,1,4";                                        // Castle opened too
        CHECK(w->RowOf(5) == w->RowOf(4) + 1);
        w->expanded = "";
        // a row cannot sit more than one level under the row above it
        w->nodes = "A|>>>B|>C"; CHECK(w->Depth(1) == 1 && w->Parent(1) == 0);
        w->nodes = "World|>Forest|>>Wolf den|>>Old oak|>Castle|>>Gate|Settings|>Audio|>>>Deep";
        w->expanded = "0";
        s.Start();
        const float rh = w->rowHeight;
        // click the arrow of Forest (row 1: depth 1)
        CHECK(w->OnArrow(1, 1 * w->indent + 5.0f));
        CHECK(!w->OnArrow(1, 1 * w->indent + 60.0f));
        Click(s, 10 + 1 * w->indent + 8.0f, 10 + 1 * rh + 5.0f);
        CHECK(w->IsExpanded(1));
        CHECK(w->selected == -1);                                     // the arrow only opens
        CHECK(w->IsActive());
        Click(s, 10 + 150, 10 + 2 * rh + 5.0f);                       // the row of Wolf den
        CHECK(w->selected == 2);
        CHECK(uivars::Read(g, "tree_pick_text").text == "World/Forest/Wolf den");
        CHECK_NEAR(g->transform->localPosition.z, 2.0f, 1e-3);
        // keys
        Key(s, Input::KeyDown);  CHECK(w->selected == 3);
        Key(s, Input::KeyDown);  CHECK(w->selected == 4);             // Castle
        Key(s, Input::KeyRight); CHECK(w->IsExpanded(4) && w->selected == 4);
        Key(s, Input::KeyRight); CHECK(w->selected == 5);             // steps into the first child
        Key(s, Input::KeyLeft);  CHECK(w->selected == 4);             // back to the parent
        Key(s, Input::KeyLeft);  CHECK(!w->IsExpanded(4));
        Key(s, Input::KeyHome);  CHECK(w->selected == 0);
        Key(s, Input::KeyEnd);   CHECK(w->selected == w->Visible().back());
        w->Select(2);
        w->SetExpanded(1, false);                                     // folding the parent of the selection moves the selection to it
        CHECK(w->selected == 1);
        // selecting a hidden node opens the way to it
        w->CollapseAll();
        CHECK(w->selected == 0);
        CHECK(w->Select(8));
        CHECK(w->IsExpanded(6) && w->IsExpanded(7) && w->RowOf(8) >= 0);
        CHECK(!w->Select(99));
        w->ExpandAll();
        CHECK(w->VisibleCount() == 9);
        // scrolling: nine 26 pixel rows in a 130 pixel box
        CHECK_NEAR(w->ContentHeight(), 9 * rh, 1e-3);
        CHECK_NEAR(w->MaxScroll(), 9 * rh - 130.0f, 1e-3);
        int first = 0, last = 0;
        w->SetScroll(0); w->VisibleRange(first, last);
        CHECK(first == 0 && last == 5);                               // only the rows touching the box
        w->SetScroll(rh * 3.0f); w->VisibleRange(first, last);
        CHECK(first == 3 && last == 8);
        w->SetScroll(0);
        Move(s, 100, 60);
        Wheel(s, -1.0f);                                              // a wheel notch down scrolls three rows
        CHECK_NEAR(w->Scroll(), rh * 3.0f, 1e-3);
        Wheel(s, 10.0f);
        CHECK_NEAR(w->Scroll(), 0.0f, 1e-3);
        w->Select(8);                                                 // the selection scrolls into view
        CHECK(w->Scroll() > 0.0f);
        CHECK(InvokeNativeUIAction(g, "CollapseAll", 0) && w->VisibleCount() == 2);
        CHECK(InvokeNativeUIAction(g, "Select", 5) && w->selected == 5);
        // a big tree: only the rows on screen are visited by a draw
        {
            std::string big;
            for (int i = 0; i < 3000; ++i) { if (i) big += '|'; big += (i % 10 == 0) ? "Group " + std::to_string(i) : ">Item " + std::to_string(i); }
            w->nodes = big; w->expanded = ""; w->ExpandAll();
            CHECK(w->Count() == 3000 && w->VisibleCount() == 3000);
            w->SetScroll(rh * 1000.0f); w->VisibleRange(first, last);
            CHECK(first == 1000 && last - first < 8);
        }
        w->nodes = "World|>Forest|>>Wolf den|>>Old oak|>Castle|>>Gate|Settings|>Audio|>>>Deep"; w->expanded = "0,1,6"; w->selected = 2; w->rowHeight = 30; w->indent = 20;
        CHECK(RoundTrip(s, "Tree", [&](GameObject* o) {
            auto* r = o ? o->GetComponent<UITreeView>() : nullptr;
            return r && r->nodes == w->nodes && r->expanded == "0,1,6" && r->selected == 2 && r->rowHeight == 30 && r->indent == 20 && r->VisibleCount() == 7;
        }));
    }

    // ===== Data table =============================================================================================
    {
        ActionList::ResetVars();
        Scene s("Table"); s.physicsEnabled = false;
        GameObject* g = s.CreateGameObject("Table");
        auto* w = g->AddComponent<UIDataTable>();
        w->anchor = UIAnchor::TopLeft; w->position = {20, 20}; w->size = {400, 30 + 26 * 4};
        w->columns = "Name:150|Class|Level";
        w->rows = "ava|Mage|42;Bo|Rogue|7;Cy|Knight|30;di|Mage|100;Eli|rogue|25;Fen|Knight|abc;Gia|Mage|";
        w->selectedVar = "table_pick";
        auto* sc = g->AddComponent<ScriptComponent>("okayscript");
        CHECK(sc->LoadSource("function on_select() { set_x(9); } function on_sort() { set_y(8); }"));
        CHECK(w->ColumnCount() == 3 && w->RowCount() == 7);
        CHECK(w->ColumnTitle(0) == "Name" && w->Cell(1, 0) == "Bo" && w->Cell(0, 2) == "42");
        CHECK(w->Cell(6, 2).empty());
        CHECK_NEAR(w->ColumnWidth(0), 150.0f, 1e-3);
        CHECK_NEAR(w->ColumnWidth(1), (400.0f - 10.0f - 150.0f) / 2.0f, 1e-3);   // the others share what is left (minus the scroll bar)
        CHECK(w->ColumnAt(w->ColumnX(1) + 1) == 1 && w->ColumnAt(-1) == -1);
        // sorting: numbers by value (100 after 7), words ignoring capitals, numbers before words, ties in the written order
        w->SortBy(2, true);
        CHECK(w->Cell(0, 2) == "7" && w->Cell(1, 2) == "25" && w->Cell(2, 2) == "30" && w->Cell(3, 2) == "42" && w->Cell(4, 2) == "100");
        CHECK(w->Cell(5, 2).empty() && w->Cell(6, 2) == "abc");       // numbers first, then the words
        w->SortBy(2, false);
        CHECK(w->Cell(0, 2) == "abc" && w->Cell(1, 2).empty() && w->Cell(2, 2) == "100" && w->Cell(6, 2) == "7");
        w->SortBy(0, true);
        CHECK(w->Cell(0, 0) == "ava" && w->Cell(1, 0) == "Bo" && w->Cell(2, 0) == "Cy" && w->Cell(3, 0) == "di");
        w->SortBy(1, true);                                           // Mage Mage Mage Knight... : stable ties
        CHECK(w->Cell(0, 1) == "Knight" && w->Cell(0, 0) == "Cy" && w->Cell(1, 0) == "Fen");
        w->SortBy(-1, true);
        CHECK(w->Cell(0, 0) == "ava" && w->Cell(1, 0) == "Bo" && w->Cell(6, 0) == "Gia");
        // header click: ascending, flip, flip
        s.Start();
        const float hx = 20 + w->ColumnX(2) + 10, hy = 20 + 10;
        Click(s, hx, hy);
        CHECK(w->sortColumn == 2 && w->ascending);
        CHECK_NEAR(g->transform->localPosition.y, 8.0f, 1e-3);        // on_sort ran
        Click(s, hx, hy);
        CHECK(w->sortColumn == 2 && !w->ascending);
        Click(s, 20 + w->ColumnX(0) + 10, hy);
        CHECK(w->sortColumn == 0 && w->ascending);
        w->sortable = false;
        Click(s, hx, hy);
        CHECK(w->sortColumn == 0);                                    // not sortable: a header click does nothing
        w->sortable = true;
        // selection follows the row through sorts
        w->SortBy(-1, true);
        Click(s, 100, 20 + 30 + 26 * 1 + 5);                          // the second row: Bo
        CHECK(w->selected == 1);
        CHECK(uivars::Read(g, "table_pick_text").text == "Bo");
        CHECK_NEAR(g->transform->localPosition.x, 9.0f, 1e-3);
        w->SortBy(2, true);
        CHECK(w->selected == 1 && w->SourceRow(0) == 1);              // Bo (level 7) is now first and still selected
        CHECK(w->DisplayRow(1) == 0);
        // keys move through the order shown
        Key(s, Input::KeyDown); CHECK(w->selected == w->SourceRow(1));
        Key(s, Input::KeyEnd);  CHECK(w->selected == w->SourceRow(6));
        Key(s, Input::KeyHome); CHECK(w->selected == w->SourceRow(0));
        Key(s, Input::KeyUp);   CHECK(w->selected == w->SourceRow(0));   // stays at the top
        Key(s, Input::KeyPageDown); CHECK(w->DisplayRow(w->selected) == w->ViewportRows() - 1);
        // scrolling
        CHECK(w->ViewportRows() == 4);
        CHECK_NEAR(w->MaxScroll(), 7 * 26.0f - 4 * 26.0f, 1e-3);
        int first = 0, last = 0;
        w->SetScroll(0); w->VisibleRange(first, last); CHECK(first == 0 && last == 3);
        Move(s, 100, 60);
        Wheel(s, -1.0f);
        CHECK_NEAR(w->Scroll(), w->MaxScroll(), 1e-3);
        w->VisibleRange(first, last); CHECK(first == 3 && last == 6);
        w->Select(w->SourceRow(0));                                   // selecting a row scrolls it into view
        CHECK_NEAR(w->Scroll(), 0.0f, 1e-3);
        // adding rows, replacing rows
        w->AddRow("Hux|Mage|1");
        CHECK(w->RowCount() == 8);
        CHECK(InvokeNativeUIAction(g, "AddSampleRow", 0) && w->RowCount() == 9);
        CHECK(InvokeNativeUIAction(g, "SortBy", 1) && w->sortColumn == 1);
        CHECK(InvokeNativeUIAction(g, "ClearSort", 0) && w->sortColumn == -1);
        w->SetRows("Z|1;Y|2");
        CHECK(w->RowCount() == 2 && w->selected == -1);
        w->SetRows("");
        CHECK(w->RowCount() == 0 && w->SourceRow(0) == -1);
        Click(s, 100, 80);                                            // an empty table: a click selects nothing
        CHECK(w->selected == -1);
        // thousands of rows: only the rows on screen are visited, and the sort stays correct
        {
            std::string big;
            for (int i = 0; i < 10000; ++i) { if (i) big += ';'; big += "Row" + std::to_string(i) + "|" + std::to_string((i * 7919) % 10007); }
            w->columns = "Name|Score"; w->rows = big;
            CHECK(w->RowCount() == 10000);
            w->SortBy(1, true);
            bool ordered = true;
            double prev = -1, cur = 0;
            for (int d = 0; d < 10000; ++d) { uiapp::ToNumber(w->Cell(d, 1), cur); if (cur < prev) ordered = false; prev = cur; }
            CHECK(ordered);
            w->SetScroll(26.0f * 5000);
            w->VisibleRange(first, last);
            CHECK(first == 5000 && last - first + 1 <= w->ViewportRows() + 1);
        }
        w->columns = "Name:150|Class|Level";
        w->rows = "ava|Mage|42;Bo|Rogue|7;Cy|Knight|30"; w->sortColumn = 2; w->ascending = false; w->selected = 2; w->zebra = false; w->rowHeight = 28; w->headerHeight = 34;
        CHECK(RoundTrip(s, "Table", [&](GameObject* o) {
            auto* r = o ? o->GetComponent<UIDataTable>() : nullptr;
            return r && r->columns == w->columns && r->rows == w->rows && r->sortColumn == 2 && !r->ascending && r->selected == 2 && !r->zebra && r->rowHeight == 28 && r->headerHeight == 34 && r->Cell(0, 2) == "42";
        }));
    }

    // ===== Form group =============================================================================================
    {
        ActionList::ResetVars();
        Scene s("Form"); s.physicsEnabled = false;
        GameObject* g = s.CreateGameObject("Sign");
        auto* f = g->AddComponent<UIFormGroup>();
        f->anchor = UIAnchor::TopLeft; f->position = {20, 20}; f->size = {500, 400};
        f->fields = "Name:text|Email:email|Age:int:13..120|Password:text:min6|Repeat:text:=Password|Nick:text:?";
        f->valuesVar = "su_"; f->validVar = "su_ok";
        auto* sc = g->AddComponent<ScriptComponent>("okayscript");
        CHECK(sc->LoadSource("function on_submit() { set_x(5); } function on_invalid() { set_y(6); }"));
        f->Rebuild();
        CHECK(f->Count() == 6);
        CHECK(ChildCount(g) == 7);                                    // six fields and the Submit button
        CHECK(f->FieldObject(1) && f->FieldObject(1)->name == "Sign_email");
        CHECK(f->Validator(2)->useRange && f->Validator(2)->minNumber == 13 && f->Validator(2)->maxNumber == 120);
        CHECK(f->Validator(3)->minLength == 6);
        CHECK(!f->Validator(5)->required);
        CHECK(f->Validator(4)->mustMatch == "Sign_password");
        CHECK(f->IndexOfLabel("Age") == 2 && f->IndexOfLabel("Last") == -1);
        f->Rebuild(); f->Rebuild();
        CHECK(ChildCount(g) == 7);                                    // rebuilding does not make copies
        s.Start();
        s.Update(0.016f);
        CHECK(!f->IsValid() && f->ErrorCount() == 5);                 // nothing typed: five required, the optional one is fine
        float v = -1; CHECK(uivars::ReadNumber(g, "su_ok", v) && v == 0.0f);
        CHECK(!f->Validator(0)->ShowsError());                        // quiet until touched
        f->SetValue(0, "Ann"); f->SetValue(1, "ann@site.com"); f->SetValue(2, "12"); f->SetValue(3, "secret1"); f->SetValue(4, "secret2");
        CHECK(f->ErrorOf(2) == "Between 13 and 120");
        CHECK(f->ErrorOf(4) == "The two entries do not match");
        CHECK(!f->Submit());                                          // refused: messages show, on_invalid runs
        CHECK(f->Validator(2)->ShowsError() && f->Validator(4)->ShowsError() && !f->Validator(0)->ShowsError());
        CHECK_NEAR(g->transform->localPosition.y, 6.0f, 1e-3);
        CHECK(!f->Submitted());
        f->SetValue(2, "30"); f->SetValue(4, "secret1");
        CHECK(f->IsValid() && f->ErrorCount() == 0);
        s.Update(0.016f);
        CHECK(uivars::ReadNumber(g, "su_ok", v) && v == 1.0f);
        CHECK(uivars::Read(g, "su_name").text == "Ann" && uivars::Read(g, "su_email").text == "ann@site.com");
        CHECK(f->Submit());
        CHECK(f->Submitted());
        CHECK_NEAR(g->transform->localPosition.x, 5.0f, 1e-3);
        CHECK(f->Value(f->IndexOfLabel("Email")) == "ann@site.com");
        // the Submit button is a plain UI button wired to the group
        GameObject* sub = nullptr;
        for (Transform* c : g->transform->Children()) if (c->gameObject->name == "Submit") sub = c->gameObject;
        CHECK(sub && sub->GetComponent<UIButton>() && sub->GetComponent<UIButton>()->clickFunction == "Submit" && sub->GetComponent<UIButton>()->clickTarget == "Sign");
        CHECK(InvokeNativeUIAction(g, "Reset", 0));
        CHECK(f->Value(0).empty() && !f->IsValid() && ChildCount(g) == 7);
        // the fields text edits the children
        f->fields = "One:text|Two:number";
        s.Update(0.016f);
        s.Update(0.016f);
        CHECK(f->Count() == 2 && ChildCount(g) == 3);
        CHECK(f->Validator(1)->kind == UIInputValidator::Kind::Number);
        f->submitLabel.clear();
        s.Update(0.016f);
        CHECK(ChildCount(g) == 2);                                    // no Submit button without a label
        // placement: fields sit under each other, to the right of the labels
        CHECK(f->InputOf(0)->position.x == f->labelWidth && f->InputOf(1)->position.y == f->rowHeight);
        f->fields = "Name:text|Email:email|Age:int:13..120"; f->submitLabel = "Go"; f->labelWidth = 150; f->rowHeight = 60; f->valuesVar = "su_"; f->showSummary = false;
        f->Rebuild();
        CHECK(RoundTrip(s, "Sign", [&](GameObject* o) {
            auto* r = o ? o->GetComponent<UIFormGroup>() : nullptr;
            return r && r->fields == f->fields && r->labelWidth == 150 && r->rowHeight == 60 && r->submitLabel == "Go" && r->valuesVar == "su_" && !r->showSummary && r->validVar == "su_ok";
        }));
        // after loading, the saved children are found again (no duplicates)
        {
            Scene back("back");
            std::string err;
            CHECK(SceneSerializer::Deserialize(back, SceneSerializer::Serialize(s), &err, nullptr));
            back.physicsEnabled = false;
            back.Start(); back.Update(0.016f); back.Update(0.016f);
            GameObject* bg = back.Find("Sign");
            CHECK(bg && ChildCount(bg) == 4);                          // three fields and the button, as saved
            CHECK(bg->GetComponent<UIFormGroup>()->Count() == 3 && bg->GetComponent<UIFormGroup>()->Validator(2)->useRange);
        }
    }

    // ===== Wizard =================================================================================================
    {
        ActionList::ResetVars();
        Scene s("Wizard"); s.physicsEnabled = false;
        GameObject* fg = s.CreateGameObject("FormA");
        auto* f = fg->AddComponent<UIFormGroup>();
        f->anchor = UIAnchor::TopLeft; f->position = {20, 100}; f->size = {400, 100};
        f->fields = "Name:text|Email:email"; f->submitLabel.clear();
        f->Rebuild();
        GameObject* pa = s.CreateGameObject("PageA");
        GameObject* pb = s.CreateGameObject("PageB");
        GameObject* pc = s.CreateGameObject("PageC");
        GameObject* g = s.CreateGameObject("Wiz");
        auto* w = g->AddComponent<UIWizard>();
        w->anchor = UIAnchor::TopLeft; w->position = {20, 20}; w->size = {600, 300};
        w->steps = "Account|Profile|Done"; w->pages = "PageA|PageB|PageC"; w->validate = "FormA;;if:agreed";
        w->progressVar = "wiz_progress"; w->stepVar = "wiz_step";
        auto* sc = g->AddComponent<ScriptComponent>("okayscript");
        CHECK(sc->LoadSource("function on_next() { set_x(1); } function on_back() { set_x(2); } function on_invalid() { set_y(3); } function on_finish() { set_z(4); }"));
        CHECK(w->Count() == 3 && w->StepNames()[1] == "Profile");
        s.Start();
        s.Update(0.016f);
        CHECK(w->step == 0 && pa->active && !pb->active && !pc->active);   // only the first step's page shows
        float v = -1;
        CHECK(uivars::ReadNumber(g, "wiz_progress", v) && v == 0.0f);
        CHECK(!w->CanGoNext() && w->Blocker() == "2 to fix");
        CHECK(!w->Next());
        CHECK(w->step == 0);
        CHECK_NEAR(g->transform->localPosition.y, 3.0f, 1e-3);        // on_invalid
        CHECK(f->Validator(0)->ShowsError());                         // the form shows what is wrong
        f->SetValue(0, "Ann"); f->SetValue(1, "ann@site.com");
        CHECK(w->CanGoNext() && w->Blocker().empty());
        CHECK(w->Next());
        CHECK(w->step == 1 && !pa->active && pb->active && !pc->active);
        CHECK_NEAR(g->transform->localPosition.x, 1.0f, 1e-3);        // on_next
        CHECK(uivars::ReadNumber(g, "wiz_progress", v) && v == 50.0f);
        CHECK(uivars::ReadNumber(g, "wiz_step", v) && v == 1.0f);
        CHECK(w->Back());
        CHECK(w->step == 0 && pa->active && !pb->active);
        CHECK_NEAR(g->transform->localPosition.x, 2.0f, 1e-3);        // on_back
        CHECK(!w->Back());                                            // already at the first step
        CHECK(!w->GoTo(2));                                           // forward jumps need Free Navigation
        CHECK(w->Next() && w->Next());
        CHECK(w->step == 2 && w->IsLast() && pc->active);
        CHECK(w->Blocker() == "Not ready yet");                       // the condition over a variable
        CHECK(!w->Next() && !w->Finished());
        uivars::WriteNumber(g, "agreed", 1.0f);
        CHECK(w->Blocker().empty());
        CHECK(w->Next() && w->Finished());
        CHECK_NEAR(g->transform->localPosition.z, 4.0f, 1e-3);        // on_finish
        CHECK(uivars::ReadNumber(g, "wiz_progress", v) && v == 100.0f);
        CHECK(w->Back() && !w->Finished() && w->step == 1);
        w->Restart();
        CHECK(w->step == 0 && !w->Finished());
        // free navigation: forward only when every step in between is valid
        w->freeNavigation = true;
        f->SetValue(1, "");
        CHECK(!w->GoTo(2));
        f->SetValue(1, "ann@site.com");
        CHECK(w->GoTo(1) && w->step == 1);
        CHECK(w->GoTo(2) && w->step == 2);                            // step 1 has no rule, so it may be left
        w->Restart(); w->freeNavigation = false;
        // the buttons and the step markers take clicks
        float x, y, bw, bh;
        w->NextRect(x, y, bw, bh);
        Click(s, 20 + x + bw / 2, 20 + y + bh / 2);
        CHECK(w->step == 1);
        w->BackRect(x, y, bw, bh);
        Click(s, 20 + x + bw / 2, 20 + y + bh / 2);
        CHECK(w->step == 0);
        Click(s, 20 + w->MarkerX(2), 20 + w->MarkerY());              // a marker ahead: refused without Free Navigation
        CHECK(w->step == 0);
        w->Next();
        Click(s, 20 + w->MarkerX(0), 20 + w->MarkerY());              // a marker behind: goes back
        CHECK(w->step == 0);
        CHECK(InvokeNativeUIAction(g, "Next", 0) && w->step == 1);
        CHECK(InvokeNativeUIAction(g, "Back", 0) && w->step == 0);
        CHECK(InvokeNativeUIAction(g, "Restart", 0));
        // a lone input validator also works as a step's rule
        w->validate = "FormA;;"; w->steps = "One"; w->pages.clear();
        CHECK(w->Count() == 1 && w->IsLast());
        CHECK(w->Next() && w->Finished());
        w->steps = "Account|Profile|Done"; w->pages = "PageA|PageB|PageC"; w->validate = "FormA;;if:agreed"; w->step = 1; w->progressVar = "wiz_progress"; w->freeNavigation = true; w->showButtons = false;
        CHECK(RoundTrip(s, "Wiz", [&](GameObject* o) {
            auto* r = o ? o->GetComponent<UIWizard>() : nullptr;
            return r && r->steps == w->steps && r->pages == w->pages && r->validate == w->validate && r->step == 1 && r->freeNavigation && !r->showButtons && r->stepVar == "wiz_step";
        }));
    }

    // ===== Colour picker, advanced mode ===========================================================================
    {
        Scene s("Color"); s.physicsEnabled = false;
        GameObject* g = s.CreateGameObject("Picker");
        auto* cp = g->AddComponent<UIColorPicker>();
        cp->position = {20, 20};
        cp->Rebuild();
        CHECK(!cp->advanced && !cp->useCustom);
        CHECK(ChildCount(g) == cp->Count() + 1);                      // swatches and the preview chip only
        CHECK_NEAR(cp->Current().r, cp->swatches[0].r, 1e-5);
        CHECK(cp->Hex() == colorhsv::Hex(cp->swatches[0]));
        auto* sc = g->AddComponent<ScriptComponent>("okayscript");
        CHECK(sc->LoadSource("function on_change() { set_x(3); }"));
        cp->SetAdvanced(true);
        s.Update(0.016f);
        CHECK(ChildCount(g) == cp->Count() + 1 + 3);
        auto child = [&](const char* name) -> GameObject* { for (Transform* c : g->transform->Children()) if (c->gameObject->name == name) return c->gameObject; return nullptr; };
        GameObject* sqo = child("ColorSquare"); GameObject* vso = child("ValueSlider"); GameObject* aso = child("AlphaSlider");
        CHECK(sqo && vso && aso && sqo->GetComponent<UIColorSquare>() && vso->GetComponent<UISlider>() && aso->GetComponent<UISlider>());
        auto* sq = sqo->GetComponent<UIColorSquare>(); auto* vs = vso->GetComponent<UISlider>(); auto* as = aso->GetComponent<UISlider>();
        s.Start();
        // the pieces are laid out under the strip
        CHECK(sq->position.y > 20 + cp->swatch * 1.6f && sq->size.x == cp->squareSize);
        CHECK(vs->position.x > sq->position.x + sq->size.x && as->position.x > vs->position.x);
        CHECK(vs->vertical && as->vertical);
        // a script (or the API) makes a colour
        cp->SetHSVA(120.0f, 1.0f, 1.0f, 1.0f);
        CHECK(cp->useCustom && cp->Hex() == "#00FF00");
        CHECK_NEAR(g->transform->localPosition.x, 3.0f, 1e-3);        // on_change ran
        s.Update(0.016f);
        CHECK_NEAR(sq->hue, 120.0f, 1e-3); CHECK_NEAR(sq->sat, 1.0f, 1e-4); CHECK_NEAR(vs->value, 1.0f, 1e-4);   // the children follow
        cp->SetColor(Color(1, 0, 0, 0.5f));
        CHECK(cp->Hex() == "#FF000080");
        // a click on a swatch goes back to the swatches and loads its numbers
        cp->SetValue(2);
        CHECK(!cp->useCustom && cp->value == 2);
        const colorhsv::HSV want = colorhsv::FromRGB(cp->swatches[2].r, cp->swatches[2].g, cp->swatches[2].b);
        CHECK_NEAR(cp->hue, want.h, 1e-3); CHECK_NEAR(cp->sat, want.s, 1e-4);
        s.Update(0.016f);
        CHECK_NEAR(sq->hue, want.h, 1e-3);
        CHECK(!cp->useCustom);                                        // a frame of syncing does not count as an edit
        // dragging in the square: left to right is hue, top to bottom is saturation
        const float sx = sq->position.x, sy = sq->position.y, sw = sq->size.x;
        Click(s, sx + sw * 0.5f, sy + sw * 0.25f);
        s.Update(0.016f);
        CHECK_NEAR(sq->hue, 180.0f, 1.0);
        CHECK_NEAR(sq->sat, 0.75f, 0.01);
        CHECK(cp->useCustom && std::fabs(cp->hue - 180.0f) < 1.0f && std::fabs(cp->sat - 0.75f) < 0.01f);
        // a value slider and an alpha slider
        vs->value = 0.5f;
        s.Update(0.016f);
        CHECK_NEAR(cp->val, 0.5f, 1e-4);
        as->value = 0.25f;
        s.Update(0.016f);
        CHECK_NEAR(cp->alpha, 0.25f, 1e-4);
        CHECK_NEAR(cp->Current().a, 0.25f, 1e-4);
        CHECK_NEAR(sq->brightness, 0.5f, 1e-4);                       // the square shows the picked brightness
        CHECK(InvokeNativeUIAction(g, "ToggleAdvanced", 0) && !cp->advanced);
        s.Update(0.016f); s.Update(0.016f);
        CHECK(ChildCount(g) == cp->Count() + 1);                      // the extra children are gone
        CHECK(cp->useCustom);                                         // the colour you made stays
        // save and load: the advanced fields come back, and an older line (no advanced fields) loads with the defaults
        cp->SetAdvanced(true);
        cp->SetHSVA(200.0f, 0.4f, 0.6f, 0.8f);
        cp->squareSize = 120.0f;
        CHECK(RoundTrip(s, "Picker", [&](GameObject* o) {
            auto* r = o ? o->GetComponent<UIColorPicker>() : nullptr;
            return r && r->advanced && r->useCustom && std::fabs(r->hue - 200.0f) < 0.01f && std::fabs(r->sat - 0.4f) < 0.001f && std::fabs(r->val - 0.6f) < 0.001f && std::fabs(r->alpha - 0.8f) < 0.001f && r->squareSize == 120.0f;
        }));
        Scene old("old");
        GameObject* og = old.CreateGameObject("Plain");
        auto* op = og->AddComponent<UIColorPicker>();
        op->value = 3;
        const std::string text = SceneSerializer::Serialize(old);
        CHECK(text.find("uicolorpicker") != std::string::npos);
        Scene back("back");
        std::string err;
        CHECK(SceneSerializer::Deserialize(back, text, &err, nullptr));
        auto* bp = back.Find("Plain")->GetComponent<UIColorPicker>();
        CHECK(bp && !bp->advanced && !bp->useCustom && bp->value == 3 && bp->hue == 210.0f && bp->squareSize == 140.0f);
        // the colour square on its own: pointer to hue and saturation
        Scene s2("Square"); s2.physicsEnabled = false;
        GameObject* q = s2.CreateGameObject("Sq");
        auto* qs = q->AddComponent<UIColorSquare>();
        qs->position = {10, 10}; qs->size = {200, 100};
        qs->PickAt(100, 25);
        CHECK_NEAR(qs->hue, 180.0f, 1e-3); CHECK_NEAR(qs->sat, 0.75f, 1e-4);
        qs->PickAt(-50, 500);                                         // outside: clamped to the edge
        CHECK_NEAR(qs->hue, 0.0f, 1e-3); CHECK_NEAR(qs->sat, 0.0f, 1e-4);
        qs->hue = 40; qs->sat = 0.3f; qs->brightness = 0.7f;
        CHECK(RoundTrip(s2, "Sq", [&](GameObject* o) {
            auto* r = o ? o->GetComponent<UIColorSquare>() : nullptr;
            return r && r->hue == 40.0f && std::fabs(r->sat - 0.3f) < 1e-4f && std::fabs(r->brightness - 0.7f) < 1e-4f;
        }));
    }

    // ===== Layers and registry ====================================================================================
    {
        Scene s("Layers"); s.physicsEnabled = false;
        struct Case { GameObject* g; };
        std::vector<GameObject*> parts;
        auto mk = [&](const char* n) { GameObject* g = s.CreateGameObject(n); parts.push_back(g); return g; };
        mk("a")->AddComponent<UIMenuBar>();
        mk("b")->AddComponent<UIToolbar>();
        mk("c")->AddComponent<UITreeView>();
        mk("d")->AddComponent<UIDataTable>();
        mk("e")->AddComponent<UIWizard>();
        mk("f")->AddComponent<UIFormGroup>();
        mk("h")->AddComponent<UIColorSquare>();
        for (GameObject* g : parts) CHECK(UIDefaultLayer(g) == uilayers::K_AppParts);
        std::vector<UIDrawItem> items;
        UIQueueDrawItems(parts[0], 0, items);
        CHECK(items.size() == 2 && items[0].kind == uilayers::K_AppParts && items[1].kind == uilayers::K_AppPopup);   // a menu bar's dropdown has its own layer
        items.clear();
        UIQueueDrawItems(parts[2], 0, items);
        CHECK(items.size() == 1 && items[0].kind == uilayers::K_AppParts);
        CHECK(std::string(UILayerName(uilayers::K_AppParts)) == "app parts");
        CHECK(std::string(UILayerName(uilayers::K_AppPopup)) == "app popup");
        // they are not game HUD parts (a different layer), but they do have a rectangle like one
        CHECK(GetUIRect(parts[3]).valid && GetUIRect(parts[3]).size.x == parts[3]->GetComponent<UIDataTable>()->size.x);
    }
    RegisterBuiltinProperties();
    {
        const char* names[] = {"UIMenuBar", "UIToolbar", "UITreeView", "UIDataTable", "UIWizard", "UIFormGroup", "UIColorSquare"};
        Scene s("Reg"); s.physicsEnabled = false;
        GameObject* g = s.CreateGameObject("R");
        for (const char* n : names) {
            auto info = PropertyRegistry::FindByName(n);
            CHECK(info != nullptr);
            if (!info) continue;
            CHECK(info->properties.size() >= 8);
        }
        auto* t = g->AddComponent<UITreeView>();
        auto info = PropertyRegistry::FindFor(*t);
        CHECK(info != nullptr);
        if (info) {
            for (const PropertyDesc& pd : info->properties) {
                if (pd.name == "nodes") CHECK(pd.Set(*t, PropertyValue::MakeString("A|>B")));
                if (pd.name == "rowHeight") CHECK(pd.Set(*t, PropertyValue::MakeFloat(40.0f)));
            }
            CHECK(t->nodes == "A|>B" && t->rowHeight == 40.0f && t->Count() == 2);
        }
    }

    ActionList::ResetVars();
    TEST_MAIN_RESULT();
}
