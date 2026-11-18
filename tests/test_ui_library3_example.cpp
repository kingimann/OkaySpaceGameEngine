// The shipped sample scene examples/ui_library/ui_library_3.okayscene (UI library slice 3): app parts. This test loads the file from
// disk and plays it like a person would (click the menu, the toolbar, the tree, the table, the wizard, the colour square) and checks
// what the screen would show.
//
// The file is made by this test: run it with OKAY_WRITE_UI_LIBRARY_EXAMPLE3=<folder> to write ui_library_3.okayscene there (after a
// change to the builder or to the serializer). Without it the test only reads the committed file. Written for this engine; no other
// project's code or assets were used.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Scene/UILibrary.hpp"
#include "okay/UI/UILayoutUpdate.hpp"
#include <cstdio>
#include <cstdlib>
#include <string>

using namespace okay;

static std::string WithoutGuids(const std::string& text) {
    std::string out, line;
    for (std::size_t i = 0; i <= text.size(); ++i) {
        if (i == text.size() || text[i] == '\n') {
            if (line.rfind("  guid ", 0) != 0) { out += line; out += '\n'; }
            line.clear();
        } else line += text[i];
    }
    return out;
}

int main() {
    RUN_SUITE("ui_library3_example");
    const std::string dir = std::string(OKAY_SOURCE_DIR) + "/examples/ui_library";
    const std::string path = dir + "/ui_library_3.okayscene";

    if (const char* out = std::getenv("OKAY_WRITE_UI_LIBRARY_EXAMPLE3")) {
        Scene s("UI Library 3");
        UILibrary::BuildSlice3(s);
        std::string err;
        const bool ok = SceneSerializer::SaveToFile(s, std::string(out) + "/ui_library_3.okayscene", &err);
        std::printf("  wrote %s/ui_library_3.okayscene: %s %s\n", out, ok ? "ok" : "FAILED", err.c_str());
        CHECK(ok);
        TEST_MAIN_RESULT();
    }

    Scene s("play");
    s.physicsEnabled = false;
    std::string err;
    std::vector<std::string> warnings;
    const bool loaded = SceneSerializer::LoadFromFile(s, path, &err, &warnings);
    if (!loaded) std::printf("  could not load %s: %s\n", path.c_str(), err.c_str());
    CHECK(loaded);
    CHECK(warnings.empty());
    for (const auto& w : warnings) std::printf("  warning: %s\n", w.c_str());
    if (!loaded) TEST_MAIN_RESULT();
    CHECK(s.mainCamera != nullptr || s.FindObjectOfType<Camera>() != nullptr);   // a scene with no camera renders nothing in Play

    {   // the file is what the builder makes today (a stale file would hide a change from the owner)
        Scene fresh("UI Library 3");
        UILibrary::BuildSlice3(fresh);
        Scene reread("reread");
        std::string e2;
        CHECK(SceneSerializer::Deserialize(reread, SceneSerializer::Serialize(fresh), &e2, nullptr));
        CHECK(WithoutGuids(SceneSerializer::Serialize(reread)) == WithoutGuids(SceneSerializer::Serialize(s)));
    }

    ActionList::ResetVars();
    s.Start();
    UpdateUILayouts(s);
    s.Update(0.016f);
    UICanvas::Set(1280.0f, 720.0f);
    UIWorld().rectOf = [](GameObject* go, Vec2& o, Vec2& sz) { return GetUIScreenRect(go, 1280.0f, 720.0f, o, sz); };
    UIWorld().screenW = 1280.0f; UIWorld().screenH = 720.0f;

    auto rectOf = [&](GameObject* g, Vec2& o, Vec2& sz) { CHECK(g != nullptr); return g && GetUIScreenRect(g, 1280.0f, 720.0f, o, sz); };
    // A real click: the pointer goes down and up at (x, y) on the screen.
    auto clickAt = [&](float x, float y) {
        Input::FeedMouse({x, y}, 0);
        Input::FeedMouse({x, y}, 1u << 0);
        s.Update(0.016f);
        Input::FeedMouse({x, y}, 0);
        s.Update(0.016f);
        Input::FeedMouse({0, 0}, 0);
        UpdateUILayouts(s);
        s.Update(0.016f);
    };
    // A click at (lx, ly) in the pixels of the part itself, from its top-left corner.
    auto clickPart = [&](GameObject* g, float lx, float ly) {
        Vec2 o, sz;
        if (!rectOf(g, o, sz)) return;
        clickAt(o.x + lx, o.y + ly);
    };
    auto press = [&](const char* name) {
        GameObject* g = s.Find(name);
        CHECK(g && g->GetComponent<UIButton>());
        if (!g || !g->GetComponent<UIButton>()) return;
        Vec2 o, sz;
        if (!rectOf(g, o, sz)) return;
        clickAt(o.x + sz.x * 0.5f, o.y + sz.y * 0.5f);
    };
    auto key = [&](char k) {
        Input::FeedKeys({});
        Input::FeedKeys({k});
        s.Update(0.016f);
        Input::FeedKeys({});
        s.Update(0.016f);
    };
    auto text = [&](const char* name) { GameObject* g = s.Find(name); auto* tr = g ? g->GetComponent<TextRenderer>() : nullptr; return tr ? tr->text : std::string("<missing>"); };
    float v = 0;

    {   // the parts the file carries
        CHECK(s.Find("MenuBar") && s.Find("MenuBar")->GetComponent<UIMenuBar>());
        CHECK(s.Find("Toolbar") && s.Find("Toolbar")->GetComponent<UIToolbar>());
        CHECK(s.Find("Tree") && s.Find("Tree")->GetComponent<UITreeView>());
        CHECK(s.Find("Table") && s.Find("Table")->GetComponent<UIDataTable>());
        CHECK(s.Find("Wizard") && s.Find("Wizard")->GetComponent<UIWizard>());
        CHECK(s.Find("AccountForm") && s.Find("AccountForm")->GetComponent<UIFormGroup>());
        CHECK(s.Find("ProfileForm") && s.Find("ProfileForm")->GetComponent<UIFormGroup>());
        CHECK(s.Find("Picker") && s.Find("Picker")->GetComponent<UIColorPicker>() && s.Find("Picker")->GetComponent<UIColorPicker>()->advanced);
        CHECK(text("SMenu") == "Menu: none yet");
    }
    {   // menu bar: File > Open
        GameObject* g = s.Find("MenuBar");
        auto* bar = g->GetComponent<UIMenuBar>();
        clickPart(g, bar->MenuX(0) + 6, 10);
        CHECK(bar->IsOpen());
        Vec2 o, sz; rectOf(g, o, sz);
        clickAt(o.x + bar->MenuX(0) + 6, o.y + bar->EntryY(0, 1) + 4);
        CHECK(bar->ChosenPath() == "File/Open");
        s.Update(0.016f);
        CHECK(text("SMenu") == "Menu: File/Open");
        clickPart(g, bar->MenuX(0) + 6, 10);                         // the greyed Export entry does nothing
        clickAt(o.x + bar->MenuX(0) + 6, o.y + bar->EntryY(0, 4) + 4);
        CHECK(bar->ChosenPath() == "File/Open" && bar->IsOpen());
        clickAt(900, 500);
        CHECK(!bar->IsOpen());
    }
    {   // toolbar: Bold on, Right (one of a group) on
        GameObject* g = s.Find("Toolbar");
        auto* tb = g->GetComponent<UIToolbar>();
        CHECK(tb->IsOn(9) && !tb->IsOn(10));                          // Center starts on
        clickPart(g, tb->ItemX(4) + 5, 20);
        CHECK(tb->IsOn(4));
        s.Update(0.016f);
        CHECK(text("SToggles") == "B 1  I 0  U 0");
        CHECK(text("STool") == "Tool: Bold");
        clickPart(g, tb->ItemX(10) + 5, 20);
        CHECK(tb->IsOn(10) && !tb->IsOn(9));
    }
    {   // tree: click Castle, walk with the keys, open Settings
        GameObject* g = s.Find("Tree");
        auto* tr = g->GetComponent<UITreeView>();
        CHECK(tr->VisibleCount() == 6);
        clickPart(g, 150, 4 * tr->rowHeight + 6);                     // the fifth visible row: Castle
        CHECK(tr->selected == 4 && tr->Text(4) == "Castle");
        s.Update(0.016f);
        CHECK(text("STree") == "Tree: World/Castle");
        key(Input::KeyDown);
        CHECK(tr->selected == 7 && tr->Text(7) == "Settings");          // Castle is folded, so the next row is Settings
        key(Input::KeyRight);
        CHECK(tr->IsExpanded(7));
        key(Input::KeyDown);
        CHECK(tr->Text(tr->selected) == "Audio");
    }
    {   // table: sort by level, pick a row, add rows
        GameObject* g = s.Find("Table");
        auto* tb = g->GetComponent<UIDataTable>();
        CHECK(tb->RowCount() == 8 && tb->Cell(0, 0) == "Ava");
        press("SortLevel");
        CHECK(tb->sortColumn == 2 && tb->ascending && tb->Cell(0, 2) == "3");
        press("SortLevel");
        CHECK(!tb->ascending && tb->Cell(0, 2) == "51");
        clickPart(g, 60, tb->headerHeight + 3);                       // the first row on screen: Fen (level 51)
        CHECK(tb->selected == 5);
        s.Update(0.016f);
        CHECK(text("STable") == "Row: Fen");
        press("Unsorted");
        CHECK(tb->sortColumn == -1 && tb->Cell(0, 0) == "Ava");
        CHECK(tb->selected == 5 && tb->DisplayRow(5) == 5);           // still the same person
        press("AddRow");
        press("AddRow");
        CHECK(tb->RowCount() == 10);
        clickPart(g, 20, 4);                                          // header click: sort by name
        CHECK(tb->sortColumn == 0);
        UpdateUILayouts(s);
    }
    {   // wizard: Next is refused until the form is right, then the steps follow
        GameObject* wg = s.Find("Wizard");
        auto* w = wg->GetComponent<UIWizard>();
        auto* account = s.Find("AccountForm")->GetComponent<UIFormGroup>();
        auto* profile = s.Find("ProfileForm")->GetComponent<UIFormGroup>();
        GameObject* pa = s.Find("AccountPage"); GameObject* pp = s.Find("ProfilePage"); GameObject* pd = s.Find("DonePage");
        CHECK(w->step == 0 && pa->active && !pp->active && !pd->active);
        CHECK(account->Count() == 2 && account->FieldObject(0) && account->FieldObject(1));
        float nx, ny, nw, nh;
        w->NextRect(nx, ny, nw, nh);
        clickPart(wg, nx + nw / 2, ny + nh / 2);                       // Next with an empty form
        CHECK(w->step == 0);
        CHECK(account->Validator(0)->ShowsError());                   // the red message shows
        account->SetValue(0, "Ann"); account->SetValue(1, "ann@site.com");
        s.Update(0.016f); s.Update(0.016f);
        CHECK(uivars::ReadNumber(wg, "wiz_valid", v) && v == 1.0f);
        CHECK(text("SForm") == "Fields all valid: 1");
        clickPart(wg, nx + nw / 2, ny + nh / 2);
        CHECK(w->step == 1 && !pa->active && pp->active);
        s.Update(0.016f);
        CHECK(text("SWizard") == "Wizard step 1, 50 percent");
        clickPart(wg, nx + nw / 2, ny + nh / 2);                       // the age is empty
        CHECK(w->step == 1);
        profile->SetValue(0, "12");
        CHECK(profile->ErrorOf(0) == "Between 13 and 120");
        profile->SetValue(0, "29");
        clickPart(wg, nx + nw / 2, ny + nh / 2);
        CHECK(w->step == 2 && pd->active);
        s.Update(0.016f);
        CHECK(text("DoneText") == "Welcome, Ann! Press Finish.");     // the answer from step 1, through a variable
        CHECK(text("DoneNote").find("ann@site.com") != std::string::npos);
        clickPart(wg, nx + nw / 2, ny + nh / 2);                       // Finish
        CHECK(w->Finished());
        s.Update(0.016f);
        CHECK(text("SWizard") == "Wizard step 2, 100 percent");
    }
    {   // colour picker: the square sets the hue and saturation, the button turns the advanced part off
        GameObject* g = s.Find("Picker");
        auto* cp = g->GetComponent<UIColorPicker>();
        GameObject* sqo = s.Find("ColorSquare");
        CHECK(sqo && s.Find("ValueSlider") && s.Find("AlphaSlider"));
        auto* sq = sqo->GetComponent<UIColorSquare>();
        const std::string before = cp->Hex();
        clickPart(sqo, sq->size.x * 0.5f, sq->size.y * 0.25f);
        s.Update(0.016f);
        CHECK(cp->useCustom && std::fabs(cp->hue - 180.0f) < 1.0f && std::fabs(cp->sat - 0.75f) < 0.02f);
        CHECK(cp->Hex() != before);
        press("ToggleAdvanced");
        s.Update(0.016f); s.Update(0.016f);
        CHECK(!cp->advanced && !s.Find("ColorSquare"));
        press("ToggleAdvanced");
        s.Update(0.016f);
        CHECK(cp->advanced && s.Find("ColorSquare") && s.Find("ValueSlider"));
    }
    UIWorld().rectOf = nullptr;
    ActionList::ResetVars();
    TEST_MAIN_RESULT();
}
