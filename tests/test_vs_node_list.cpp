// The Nodes tab's list model (no drawing): grouping, filtering, favourites,
// which groups are open, the settings file, and where a dropped node lands.
#include "test_framework.hpp"
#include "VisualScriptNodeList.hpp"
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

using namespace okay::editor::vsed;

namespace {
std::vector<VsNodeItem> Items() {
    std::vector<VsNodeItem> v;
    auto add = [&](const char* key, const char* topic, int ti, int starter) {
        VsNodeItem n; n.key = key; n.topic = topic; n.topicIndex = ti; n.starter = starter; v.push_back(n);
    };
    add("n:OnStart", "Events", 0, 0);
    add("n:OnKey", "Events", 0, 2);
    add("n:Branch", "Flow", 1, 1);
    add("n:Add", "Math", 2, -1);
    add("n:Sub", "Math", 2, -1);
    add("a:move", "Actions", 3, -1);
    return v;
}
std::vector<int> All(const std::vector<VsNodeItem>& v) { std::vector<int> o; for (int i = 0; i < (int)v.size(); ++i) o.push_back(i); return o; }
int CountKind(const std::vector<VsListRow>& r, VsListRow::Kind k) { int n = 0; for (const auto& x : r) if (x.kind == k) ++n; return n; }
const VsListRow* Header(const std::vector<VsListRow>& r, const std::string& id) { for (const auto& x : r) if (x.kind == VsListRow::Header && x.id == id) return &x; return nullptr; }
}  // namespace

int main() {
    RUN_SUITE("vs_node_list");

    // ---- Grouping: favourites, starter, topics; folded topics hide their rows ----
    {
        const auto items = Items();
        VsNodeListPrefs p;
        auto rows = VsBuildListRows(items, All(items), false, p);
        CHECK(rows.front().kind == VsListRow::Header && rows.front().id == "fav");   // favourites are first, even when empty
        CHECK(rows[1].kind == VsListRow::Hint);                                       // and say how to fill them
        CHECK(Header(rows, "recent") == nullptr);                                     // recent only appears once used
        const VsListRow* st = Header(rows, "starter");
        CHECK(st && st->count == 3 && st->open);
        // Starter is in the starter order (0, 1, 2), not the topic order.
        std::vector<std::string> starterKeys;
        bool in = false;
        for (const auto& r : rows) {
            if (r.kind == VsListRow::Header) in = r.id == "starter";
            else if (r.kind == VsListRow::Item && in) starterKeys.push_back(items[(std::size_t)r.item].key);
        }
        CHECK((starterKeys == std::vector<std::string>{"n:OnStart", "n:Branch", "n:OnKey"}));
        // Topics start folded: header only, no item rows under them.
        const VsListRow* math = Header(rows, "topic:Math");
        CHECK(math && !math->open && math->count == 2);
        CHECK(CountKind(rows, VsListRow::Header) == 6);                               // fav, starter, 4 topics
        CHECK(CountKind(rows, VsListRow::Item) == 3);                                 // only starter items show

        VsSetGroupOpen(p, "topic:Math", true, VsGroupDefaultOpen("topic:Math"));
        rows = VsBuildListRows(items, All(items), false, p);
        CHECK(Header(rows, "topic:Math")->open);
        CHECK(CountKind(rows, VsListRow::Item) == 5);
        VsSetGroupOpen(p, "starter", false, VsGroupDefaultOpen("starter"));
        rows = VsBuildListRows(items, All(items), false, p);
        CHECK(!Header(rows, "starter")->open);
        CHECK(CountKind(rows, VsListRow::Item) == 2);
        // Only exceptions are stored: putting a group back to its default forgets it.
        VsSetGroupOpen(p, "starter", true, true);
        CHECK(p.open.count("starter") == 0 && p.open.count("topic:Math") == 1);
    }

    // ---- Filtering: the caller's level filter shrinks the order; empty topics vanish ----
    {
        const auto items = Items();
        VsNodeListPrefs p;
        const std::vector<int> onlyMath = {3, 4};
        auto rows = VsBuildListRows(items, onlyMath, false, p);
        CHECK(Header(rows, "topic:Math") && !Header(rows, "topic:Events") && !Header(rows, "starter"));
        CHECK(Header(rows, "fav") != nullptr);
        // A favourite hidden by the level filter does not show.
        p.fav = {"n:OnStart"};
        rows = VsBuildListRows(items, onlyMath, false, p);
        CHECK(Header(rows, "fav")->count == 0);
        // Nothing passes: a clear message instead of an empty panel.
        rows = VsBuildListRows(items, {}, false, p);
        CHECK(CountKind(rows, VsListRow::Item) == 0 && CountKind(rows, VsListRow::Hint) == 1);
    }

    // ---- Search: one flat list in the order given, groups ignored, cannot be folded ----
    {
        const auto items = Items();
        VsNodeListPrefs p;
        p.fav = {"n:Add"};
        const std::vector<int> ranked = {4, 0, 3};
        auto rows = VsBuildListRows(items, ranked, true, p);
        CHECK(rows.size() == 4 && rows[0].id == "results" && rows[0].count == 3 && !rows[0].foldable && rows[0].open);
        CHECK(rows[1].item == 4 && rows[2].item == 0 && rows[3].item == 3);
        rows = VsBuildListRows(items, {}, true, p);
        CHECK(rows.size() == 2 && rows[1].kind == VsListRow::Hint);
    }

    // ---- Favourites: star pins to the top, star again removes, order kept ----
    {
        const auto items = Items();
        VsNodeListPrefs p;
        CHECK(VsToggleFav(p, "n:Add"));
        CHECK(VsToggleFav(p, "n:Branch"));
        CHECK((p.fav == std::vector<std::string>{"n:Branch", "n:Add"}));              // newest star on top
        auto rows = VsBuildListRows(items, All(items), false, p);
        CHECK(rows[0].id == "fav" && rows[0].count == 2);
        CHECK(items[(std::size_t)rows[1].item].key == "n:Branch" && items[(std::size_t)rows[2].item].key == "n:Add");
        CHECK(!VsToggleFav(p, "n:Branch"));
        CHECK((p.fav == std::vector<std::string>{"n:Add"}));
        CHECK(p.isFav("n:Add") && !p.isFav("n:Branch"));
    }

    // ---- Recently used: newest first, no repeats, capped ----
    {
        VsNodeListPrefs p;
        VsPushRecent(p, "n:A"); VsPushRecent(p, "n:B"); VsPushRecent(p, "n:A");
        CHECK((p.recent == std::vector<std::string>{"n:A", "n:B"}));
        for (int i = 0; i < 30; ++i) VsPushRecent(p, "n:X" + std::to_string(i));
        CHECK(p.recent.size() == kVsRecentMax && p.recent.front() == "n:X29");
        const auto items = Items();
        VsNodeListPrefs q; VsPushRecent(q, "n:Sub"); VsPushRecent(q, "n:Gone");   // "Gone" no longer exists
        auto rows = VsBuildListRows(items, All(items), false, q);
        CHECK(Header(rows, "recent") && Header(rows, "recent")->count == 1);
    }

    // ---- The settings text: round trip, old files, bad files ----
    {
        VsNodeListPrefs p;
        p.level = 2; p.panelShown = false; p.tab = (int)VsSideTab::Outline; p.rail = true; p.width = 333.0f;
        VsToggleFav(p, "n:Print"); VsToggleFav(p, "a:set_text"); VsPushRecent(p, "c:key_down"); VsPushRecent(p, "n:Add");
        VsSetGroupOpen(p, "topic:Math", true, false); VsSetGroupOpen(p, "starter", false, true);
        const VsNodeListPrefs back = VsNodeListPrefs::fromText(p.toText());
        CHECK(back == p);
        CHECK(back.fav.size() == 2 && back.recent.size() == 2 && back.width == 333.0f && back.rail);
        // The file the pop-up wrote before the tab existed still loads.
        const VsNodeListPrefs old = VsNodeListPrefs::fromText("lvl 1\nfav n:Print\nrec n:Add\nrec n:Sub\n");
        CHECK(old.level == 1 && old.fav.size() == 1 && old.recent.size() == 2);
        CHECK(old.panelShown && old.tab == (int)VsSideTab::Nodes && old.width == kVsSideDefaultW);   // new fields default
        // Garbage in: clamped or skipped, never a crash.
        const VsNodeListPrefs bad = VsNodeListPrefs::fromText("lvl 99\nwidth 5\nwidth\n\nrubbish\nfav n:A\nfav n:A\ntab 7\nopen x\nopen 1 topic:Q\r\n");
        CHECK(bad.level == 3 && bad.width == kVsSideMinW && bad.fav.size() == 1 && bad.tab == 1);
        CHECK(bad.open.size() == 1 && bad.open.at("topic:Q"));
        CHECK(VsNodeListPrefs::fromText("width 99999").width == kVsSideMaxW);
        CHECK(VsNodeListPrefs::fromText("").toText() == VsNodeListPrefs().toText());
    }

    // ---- The file: save, load, a missing file, an unwritable path (temp dir only) ----
    {
        namespace fs = std::filesystem;
        const fs::path dir = fs::temp_directory_path() / "okay_vs_node_list_test";
        std::error_code ec; fs::remove_all(dir, ec); fs::create_directories(dir, ec);
        const std::string file = (dir / "okay_vsnodes.txt").string();
        CHECK(VsLoadNodeListPrefs(file) == VsNodeListPrefs());                         // nothing yet: defaults
        VsNodeListPrefs p; p.width = 412; p.panelShown = false; VsToggleFav(p, "n:Print"); VsSetGroupOpen(p, "topic:Flow", true, false);
        CHECK(VsSaveNodeListPrefs(file, p));
        CHECK(VsLoadNodeListPrefs(file) == p);                                         // "restart"
        CHECK(!VsSaveNodeListPrefs((dir / "no_such_dir" / "x.txt").string(), p));
        fs::remove_all(dir, ec);
    }

    // ---- Panel width follows the window and stays in range ----
    {
        CHECK_NEAR(VsClampSideWidth(300, 1500), 300, 0.01);
        CHECK_NEAR(VsClampSideWidth(50, 1500), kVsSideMinW, 0.01);
        CHECK_NEAR(VsClampSideWidth(900, 1500), kVsSideMaxW, 0.01);
        CHECK_NEAR(VsClampSideWidth(500, 600), 360, 0.01);                              // never more than 60% of the window
        CHECK_NEAR(VsClampSideWidth(500, 200), kVsSideMinW, 0.01);                      // tiny window: the minimum wins
        CHECK(VsStepRow(0, -1, 5) == 0 && VsStepRow(4, 1, 5) == 4 && VsStepRow(2, 1, 5) == 3 && VsStepRow(0, 1, 0) == 0);
    }

    // ---- Drop maths: the cursor's screen point is the graph point the canvas uses ----
    {
        float gx = 0, gy = 0;
        VsScreenToGraph(700, 400, 300, 100, 20, -10, 1.0f, 1.4f, gx, gy);
        CHECK_NEAR(gx, (700 - 300 - 20) / 1.4, 0.001);
        CHECK_NEAR(gy, (400 - 100 + 10) / 1.4, 0.001);
        VsScreenToGraph(700, 400, 300, 100, 20, -10, 2.0f, 1.4f, gx, gy);               // zoomed in: half as far in graph units
        CHECK_NEAR(gx, (700 - 300 - 20) / 2.8, 0.001);
        VsScreenToGraph(300, 100, 300, 100, 0, 0, 1.0f, 1.4f, gx, gy);                  // the canvas corner is the graph origin
        CHECK_NEAR(gx, 0, 0.001); CHECK_NEAR(gy, 0, 0.001);
        VsScreenToGraph(10, 10, 0, 0, 0, 0, 0.0f, 1.4f, gx, gy);                        // zoom 0 cannot divide by zero
        CHECK(std::isfinite(gx) && std::isfinite(gy));

        // Adding twice at the same spot does not stack the nodes.
        std::vector<std::pair<float, float>> taken = {{100, 100}};
        float x = 100, y = 100;
        VsFreeSpot(taken, x, y);
        CHECK_NEAR(x, 124, 0.001); CHECK_NEAR(y, 124, 0.001);
        taken.push_back({124, 124});
        x = 100; y = 100; VsFreeSpot(taken, x, y);
        CHECK_NEAR(x, 148, 0.001);
        x = 500; y = 500; VsFreeSpot(taken, x, y);                                       // free spot stays put
        CHECK_NEAR(x, 500, 0.001);
        VsFreeSpot({}, x, y); CHECK_NEAR(y, 500, 0.001);

        char kind = 0; std::string rest;
        CHECK(VsSplitListKey("n:Print", kind, rest) && kind == 'n' && rest == "Print");
        CHECK(VsSplitListKey("a:move_to", kind, rest) && kind == 'a' && rest == "move_to");
        CHECK(VsSplitListKey("c:key", kind, rest) && kind == 'c');
        CHECK(!VsSplitListKey("x:Print", kind, rest) && !VsSplitListKey("n:", kind, rest) && !VsSplitListKey("", kind, rest));
    }

    TEST_MAIN_RESULT();
}
