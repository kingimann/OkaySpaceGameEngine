// Behaviour of the app widgets: search filters, the date picker clamps, the file
// picker stays in its folder, the accordion opens and closes, breadcrumbs shorten
// the path, pagination changes page, the skeleton shows and hides, the sheet opens
// and closes. Each one also survives a scene save and load.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Scene/UIGallery.hpp"
#include <filesystem>
#include <fstream>
#include <string>

using namespace okay;

template <class Check>
static bool RoundTrip(Scene& s, const char* name, Check check) {
    const std::string text = SceneSerializer::Serialize(s);
    Scene back("back");
    std::string err;
    if (!SceneSerializer::Deserialize(back, text, &err, nullptr)) return false;
    if (SceneSerializer::Serialize(back) != text) return false;
    return check(back.Find(name));
}

int main() {
    RUN_SUITE("ui_widgets_more");

    // --- Search filters the list -------------------------------------------------
    {
        Scene s("Search"); s.physicsEnabled = false;
        auto* w = s.CreateGameObject("Find")->AddComponent<UISearchBar>();
        w->anchor = UIAnchor::TopLeft; w->position = {0, 0}; w->size = {300, 160};
        w->itemsText = "Apple\nApricot\nBanana";
        w->SetQuery("ap");
        auto m = w->Matches();
        CHECK(m.size() == 2);
        if (m.size() == 2) { CHECK(m[0] == "Apple"); CHECK(m[1] == "Apricot"); }
        w->SetQuery("BAN");
        m = w->Matches();
        CHECK(m.size() == 1 && m[0] == "Banana");
        w->SetQuery("");
        CHECK(w->MatchCount() == 3);
        w->SetQuery("zzz");
        CHECK(w->MatchCount() == 0);
        w->SetQuery("ap");
        w->PressLocal({12.0f, w->FieldH() + 8.0f});
        CHECK(w->value == 0);
        CHECK(w->SelectedText() == "Apple");
        // Typing goes through the same Update path as the game.
        w->editing = true; w->query.clear(); w->value = -1;
        Input::FeedText("ba");
        w->Update(0.016f);
        Input::ClearTypedText();
        CHECK(w->query == "ba");
        CHECK(w->MatchCount() == 1);
        CHECK(RoundTrip(s, "Find", [](GameObject* g) {
            auto* b = g ? g->GetComponent<UISearchBar>() : nullptr;
            return b && b->query == "ba" && b->MatchCount() == 1 && b->Items().size() == 3;
        }));
    }

    // --- Dates that do not exist are pulled back --------------------------------
    {
        Scene s("Date"); s.physicsEnabled = false;
        auto* w = s.CreateGameObject("When")->AddComponent<UIDateTimePicker>();
        w->anchor = UIAnchor::TopLeft; w->position = {0, 0}; w->size = {400, 64};
        w->minYear = 1800; w->maxYear = 2200;
        w->SetDate(2024, 2, 31);
        CHECK(w->year == 2024 && w->month == 2 && w->day == 29);   // leap year
        w->SetDate(2023, 2, 31);
        CHECK(w->year == 2023 && w->month == 2 && w->day == 28);
        w->SetDate(1900, 2, 29);
        CHECK(w->day == 28);                                       // 1900 is not a leap year
        w->SetDate(2024, 4, 31);
        CHECK(w->month == 4 && w->day == 30);
        w->SetDate(2024, 13, 40);
        CHECK(w->month == 12 && w->day == 31);
        w->SetDate(2024, 0, 0);
        CHECK(w->month == 1 && w->day == 1);
        w->SetTime(25, 70);
        CHECK(w->hour == 23 && w->minute == 59);
        w->SetTime(-3, -1);
        CHECK(w->hour == 0 && w->minute == 0);
        w->SetDate(2024, 1, 31);
        w->StepDay(1);
        CHECK(w->month == 2 && w->day == 1);
        // The minus control on the day column steps backward.
        const float col = w->size.x / (float)w->Columns();
        w->PressLocal({col * 2.0f + 4.0f, 20.0f});
        CHECK(w->month == 1 && w->day == 31);
        CHECK(w->Text().find("2024-01-31") == 0);
        CHECK(RoundTrip(s, "When", [](GameObject* g) {
            auto* b = g ? g->GetComponent<UIDateTimePicker>() : nullptr;
            return b && b->year == 2024 && b->month == 1 && b->day == 31 && b->hour == 0;
        }));
    }

    // --- The file picker cannot leave its folder --------------------------------
    {
        namespace fs = std::filesystem;
        const fs::path root = fs::temp_directory_path() / "okay-ui-file-picker-test";
        std::error_code ec;
        fs::remove_all(root, ec);
        fs::create_directories(root / "sub", ec);
        { std::ofstream(root / "a.txt") << "a"; }
        { std::ofstream(root / "sub" / "b.txt") << "b"; }
        fs::create_symlink("/etc/passwd", root / "leak", ec);

        Scene s("Files"); s.physicsEnabled = false;
        auto* w = s.CreateGameObject("Files")->AddComponent<UIFilePicker>();
        w->anchor = UIAnchor::TopLeft; w->position = {0, 0}; w->size = {280, 200};
        CHECK(w->SetRoot(root.string()));
        CHECK(w->AtRoot());
        auto rows = w->Entries();
        bool sawA = false, sawSub = false, sawLeak = false;
        for (const auto& e : rows) {
            if (e.name == "a.txt" && !e.directory) sawA = true;
            if (e.name == "sub" && e.directory) sawSub = true;
            if (e.name == "leak") sawLeak = true;
        }
        CHECK(sawA && sawSub);
        CHECK(!sawLeak);
        CHECK(!w->Open(".."));
        CHECK(!w->Open("../../etc/passwd"));
        CHECK(!w->SetSelected("/etc/passwd"));
        CHECK(!w->SetSelected("../secret.txt"));
        CHECK(w->selected.empty());
        CHECK(w->Open("leak") == false);
        CHECK(w->selected.empty());
        CHECK(w->Open("sub"));
        CHECK(w->current == "sub");
        bool sawB = false;
        for (const auto& e : w->Entries()) if (e.name == "b.txt") sawB = true;
        CHECK(sawB);
        CHECK(w->GoUp());
        CHECK(w->AtRoot());
        w->PressLocal({8.0f, w->headerHeight + 4.0f});   // first row is the folder
        CHECK(w->current == "sub");
        CHECK(w->GoUp());
        CHECK(w->Open("a.txt"));
        CHECK(w->selected == "a.txt");
        const std::string saved = SceneSerializer::Serialize(s);
        Scene back("Files2");
        std::string err;
        CHECK(SceneSerializer::Deserialize(back, saved, &err, nullptr));
        auto* b = back.Find("Files") ? back.Find("Files")->GetComponent<UIFilePicker>() : nullptr;
        CHECK(b && b->selected == "a.txt");
        CHECK(b && b->Inside(fs::path(b->root) / "a.txt"));
        CHECK(b && !b->SetSelected("../../etc/passwd"));
        fs::remove_all(root, ec);
    }

    // --- Accordion opens and closes ---------------------------------------------
    {
        Scene s("Acc"); s.physicsEnabled = false;
        auto* w = s.CreateGameObject("Sections")->AddComponent<UIAccordion>();
        w->anchor = UIAnchor::TopLeft; w->position = {0, 0}; w->size = {300, 220};
        w->allowMultiple = false;
        w->sectionsText = "One | First\nTwo | Second";
        w->openText = "0,0";
        CHECK(!w->IsExpanded(0) && !w->IsExpanded(1));
        w->PressLocal({10.0f, 8.0f});
        CHECK(w->IsExpanded(0) && !w->IsExpanded(1));
        w->PressLocal({10.0f, 8.0f});
        CHECK(!w->IsExpanded(0));
        w->SetExpanded(0, true);
        w->SetExpanded(1, true);
        CHECK(!w->IsExpanded(0) && w->IsExpanded(1));
        w->allowMultiple = true;
        w->SetExpanded(0, true);
        CHECK(w->IsExpanded(0) && w->IsExpanded(1));
        w->Toggle(1);
        CHECK(w->IsExpanded(0) && !w->IsExpanded(1));
        CHECK(RoundTrip(s, "Sections", [](GameObject* g) {
            auto* b = g ? g->GetComponent<UIAccordion>() : nullptr;
            return b && b->allowMultiple && b->IsExpanded(0) && !b->IsExpanded(1) && b->Count() == 2;
        }));
    }

    // --- Breadcrumbs shorten the path -------------------------------------------
    {
        Scene s("Crumbs"); s.physicsEnabled = false;
        auto* w = s.CreateGameObject("Crumbs")->AddComponent<UIBreadcrumbs>();
        w->anchor = UIAnchor::TopLeft; w->position = {0, 0}; w->size = {420, 36};
        w->SetPath("Home/Library/Books");
        CHECK(w->Segments().size() == 3);
        w->Select(1);
        CHECK(w->Path() == "Home/Library");
        CHECK(w->value == 1);
        w->Select(0);
        CHECK(w->Path() == "Home");
        w->SetPath("Home/Library/Books");
        w->value = 0;
        w->Select(5);
        CHECK(w->value == 2);
        CHECK(w->Path() == "Home/Library/Books");
        w->Select(-1);
        CHECK(w->value == 0 && w->Path() == "Home");
        w->SetPath("Home/Library/Books");
        const auto boxes = w->Layout();
        CHECK(boxes.size() == 3);
        w->PressLocal({boxes[1].x + 2.0f, 8.0f});
        CHECK(w->Path() == "Home/Library");
        CHECK(RoundTrip(s, "Crumbs", [](GameObject* g) {
            auto* b = g ? g->GetComponent<UIBreadcrumbs>() : nullptr;
            return b && b->Path() == "Home/Library";
        }));
    }

    // --- Pagination changes page -------------------------------------------------
    {
        Scene s("Pages"); s.physicsEnabled = false;
        auto* w = s.CreateGameObject("Pages")->AddComponent<UIPagination>();
        w->anchor = UIAnchor::TopLeft; w->position = {0, 0}; w->size = {360, 36};
        w->page = 1; w->pageCount = 10;
        w->SetPage(3);
        CHECK(w->Page() == 3);
        w->SetPage(0);
        CHECK(w->Page() == 1);
        w->SetPage(99);
        CHECK(w->Page() == 10);
        w->Next();
        CHECK(w->Page() == 10);
        w->SetPage(1);
        w->Previous();
        CHECK(w->Page() == 1);
        w->SetTotal(25, 10);
        CHECK(w->PageCount() == 3);
        CHECK(w->Page() == 1);
        w->SetPage(3);
        w->SetTotal(5, 10);
        CHECK(w->PageCount() == 1 && w->Page() == 1);
        w->pageCount = 8; w->SetPage(2);
        // Prev sits at the left edge.
        w->PressLocal({8.0f, 8.0f});
        CHECK(w->Page() == 1);
        w->PressLocal({w->size.x - 8.0f, 8.0f});
        CHECK(w->Page() == 2);
        // Gamepad / keyboard right moves a page.
        w->SetFocused(true);
        Input::FeedKeys({});
        Input::FeedKeys({'d'});
        NavigateUI(s);
        CHECK(w->Page() == 3);
        Input::FeedKeys({});
        CHECK(RoundTrip(s, "Pages", [](GameObject* g) {
            auto* b = g ? g->GetComponent<UIPagination>() : nullptr;
            return b && b->Page() == 3 && b->PageCount() == 8;
        }));
    }

    // --- Skeleton shows bars only while loading ---------------------------------
    {
        Scene s("Skel"); s.physicsEnabled = false;
        auto* w = s.CreateGameObject("Loading")->AddComponent<UISkeleton>();
        w->anchor = UIAnchor::TopLeft; w->position = {0, 0}; w->size = {320, 96};
        w->loading = true; w->lines = 3; w->showAvatar = true;
        const auto bars = w->Bars();
        CHECK(w->PlaceholderCount() == 4);   // circle + 3 lines
        CHECK(bars.size() == 4 && bars[0].circle);
        for (const auto& bar : bars) {
            CHECK(bar.w > 1.0f && bar.h > 1.0f);
            CHECK(bar.x >= 0.0f && bar.y >= 0.0f);
            CHECK(bar.x + bar.w <= w->size.x + 0.5f);
        }
        w->Tick(0.5f);
        CHECK(w->shimmer > 0.2f && w->shimmer < 1.0f);
        const float before = w->shimmer;
        w->Tick(2.0f);
        CHECK(w->shimmer >= 0.0f && w->shimmer < 1.0f);
        CHECK(w->shimmer != before);
        w->SetLoading(false);
        CHECK(w->PlaceholderCount() == 0);
        w->SetLoading(true);
        CHECK(w->PlaceholderCount() == 4);
        CHECK(RoundTrip(s, "Loading", [](GameObject* g) {
            auto* b = g ? g->GetComponent<UISkeleton>() : nullptr;
            return b && b->loading && b->lines == 3 && b->PlaceholderCount() == 4;
        }));
    }

    // --- Bottom sheet opens and closes ------------------------------------------
    {
        Scene s("Sheet"); s.physicsEnabled = false;
        auto* w = s.CreateGameObject("Sheet")->AddComponent<UIBottomSheet>();
        w->anchor = UIAnchor::TopLeft; w->position = {0, 200}; w->size = {300, 180};
        w->duration = 0.0f; w->open = false; w->progress = 0.0f;
        CHECK(!w->IsOpen() && w->ShownFraction() == 0.0f);
        w->Open();
        CHECK(w->IsOpen() && w->ShownFraction() == 1.0f);
        w->Close();
        CHECK(!w->IsOpen() && w->ShownFraction() == 0.0f);
        w->Toggle();
        CHECK(w->IsOpen());
        w->PressLocal({10.0f, 4.0f});   // the handle
        CHECK(!w->IsOpen());
        w->Open();
        w->PressLocal({10.0f, -12.0f}); // above the sheet
        CHECK(!w->IsOpen());
        w->duration = 0.2f;
        w->Open();
        CHECK(w->IsOpen() && w->ShownFraction() < 0.2f);
        w->Update(0.1f);
        CHECK(w->ShownFraction() > 0.3f && w->ShownFraction() < 0.7f);
        w->Update(1.0f);
        CHECK(w->ShownFraction() == 1.0f);
        w->duration = 0.0f;
        w->Close();
        CHECK(RoundTrip(s, "Sheet", [](GameObject* g) {
            auto* b = g ? g->GetComponent<UIBottomSheet>() : nullptr;
            return b && !b->IsOpen() && b->title == "Sheet";
        }));
    }

    // --- A script can drive a page, and on_change counts -------------------------
    {
        Scene s("Script"); s.physicsEnabled = false;
        auto* w = s.CreateGameObject("Pages")->AddComponent<UIPagination>();
        w->pageCount = 6;
        auto* own = s.Find("Pages")->AddComponent<ScriptComponent>("okayscript");
        own->LoadSource("var changes = 0;\nfunction on_change() { changes = changes + 1; }\n");
        auto* ctrl = s.CreateGameObject("Ctrl")->AddComponent<ScriptComponent>("okayscript");
        ctrl->LoadSource(
            "var page = 0;\n"
            "function update() {\n"
            "  if (page == 0) { ui_set_page(\"Pages\", 4); page = ui_page(\"Pages\"); }\n"
            "}\n");
        s.Start();
        s.Update(0.016f);
        CHECK(w->Page() == 4);
        CHECK_NEAR(ctrl->VM()->GetGlobal("page").AsFloat(), 4.0f, 1e-4f);
        CHECK_NEAR(own->VM()->GetGlobal("changes").AsFloat(), 1.0f, 1e-4f);
        w->SetPage(99);
        CHECK(w->Page() == 6);
    }

    // --- The theme recolours a search bar ----------------------------------------
    {
        Scene s("Theme"); s.physicsEnabled = false;
        auto* th = s.CreateGameObject("Canvas")->AddComponent<UITheme>();
        th->ApplyPreset((int)UITheme::Preset::Light);
        auto* w = s.CreateGameObject("Find")->AddComponent<UISearchBar>();
        th->ApplyToScene(s);
        CHECK_NEAR(w->textColor.r, th->text.r, 1e-4f);
        CHECK_NEAR(w->accent.r, th->accent.r, 1e-4f);
    }

    // --- The gallery builds one of each ------------------------------------------
    {
        Scene s("Gallery");
        UIGallery::BuildGallery(s, (int)UITheme::Preset::Dark);
        CHECK(s.Find("Search") && s.Find("Search")->GetComponent<UISearchBar>());
        CHECK(s.Find("When") && s.Find("When")->GetComponent<UIDateTimePicker>());
        CHECK(s.Find("Files") && s.Find("Files")->GetComponent<UIFilePicker>());
        CHECK(s.Find("Sections") && s.Find("Sections")->GetComponent<UIAccordion>());
        CHECK(s.Find("Crumbs") && s.Find("Crumbs")->GetComponent<UIBreadcrumbs>());
        CHECK(s.Find("Pages") && s.Find("Pages")->GetComponent<UIPagination>());
        CHECK(s.Find("Loading") && s.Find("Loading")->GetComponent<UISkeleton>());
        CHECK(s.Find("Sheet") && s.Find("Sheet")->GetComponent<UIBottomSheet>());
        CHECK(s.Find("Search")->GetComponent<UISearchBar>()->MatchCount() == 2);
        const std::string text = SceneSerializer::Serialize(s);
        Scene back("Gallery2");
        std::string err;
        CHECK(SceneSerializer::Deserialize(back, text, &err, nullptr));
        CHECK(SceneSerializer::Serialize(back) == text);
    }

    Input::FeedKeys({});
    Input::FeedMouse({0, 0}, 0);
    TEST_MAIN_RESULT();
}
