#include "test_framework.hpp"
#include "ProjectTabs.hpp"
#include <cstdio>
#include <filesystem>

using namespace oked::projecttabs;

static void tabs_active_follows_smart_and_search() {
    CHECK(ActiveTab(kSmartNone, "") == Tab::Assets);
    CHECK(ActiveTab(kSmartFavourites, "") == Tab::Favourites);
    CHECK(ActiveTab(kSmartRecent, "") == Tab::Recent);
    CHECK(ActiveTab(kSmartRecent, "  ") == Tab::Recent);   // blanks are not a search
    CHECK(ActiveTab(kSmartNone, "hero") == Tab::Search);
    CHECK(ActiveTab(kSmartFavourites, "hero") == Tab::Search);
    CHECK(ActiveTab(101, "") == Tab::Assets);               // other smart views are not tabs
}
static void tabs_click_maps_to_view() {
    CHECK(SmartForTab(Tab::Assets, kSmartRecent) == kSmartNone);
    CHECK(SmartForTab(Tab::Favourites, kSmartNone) == kSmartFavourites);
    CHECK(SmartForTab(Tab::Recent, kSmartNone) == kSmartRecent);
    CHECK(SmartForTab(Tab::Search, 5) == 5);
    CHECK(TabIsFolderView(Tab::Assets));
    CHECK(!TabIsFolderView(Tab::Favourites));
    CHECK(!TabIsFolderView(Tab::Recent));
}
static void tabs_count_badge() {
    CHECK(CountBadge(0) == "");
    CHECK(CountBadge(-3) == "");
    CHECK(CountBadge(7) == "7");
    CHECK(CountBadge(100) == "99+");
}
static void tabs_remember_round_trip() {
    Remembered r; r.smart = kSmartFavourites; r.folder = "Models/Props";
    const Remembered b = Parse(Serialize(r));
    CHECK(b.smart == kSmartFavourites);
    CHECK(b.folder == "Models/Props");
    Remembered u; u.smart = 3;   // a type view is not remembered
    CHECK(Parse(Serialize(u)).smart == kSmartNone);
}
static void tabs_remember_rejects_escapes() {
    CHECK(Parse("folder ../secret\n").folder.empty());
    CHECK(Parse("folder a/../../b\n").folder.empty());
    CHECK(Parse("folder /etc\n").folder.empty());
    CHECK(Parse("folder C:/x\n").folder.empty());
    CHECK(Parse("folder a\\b\n").folder == "a/b");
    CHECK(Parse("smart 999\n").smart == kSmartNone);
    CHECK(Parse("").smart == kSmartNone);
}
static void tabs_remember_file() {
    const std::string f = (std::filesystem::temp_directory_path() / "okay_projecttabs_test.txt").string();
    Remembered r; r.smart = kSmartRecent; r.folder = "Textures";
    CHECK(Save(f, r));
    const Remembered b = Load(f);
    CHECK(b.smart == kSmartRecent && b.folder == "Textures");
    std::remove(f.c_str());
    CHECK(Load(f).folder.empty());   // missing file: defaults
}

int main() {
    tabs_active_follows_smart_and_search();
    tabs_click_maps_to_view();
    tabs_count_badge();
    tabs_remember_round_trip();
    tabs_remember_rejects_escapes();
    tabs_remember_file();
    TEST_MAIN_RESULT();
}
