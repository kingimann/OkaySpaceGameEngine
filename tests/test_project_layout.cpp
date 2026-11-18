#include "test_framework.hpp"
#include "ProjectLayout.hpp"
#include "EditorLayoutMath.hpp"
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace oked::projectlayout;

static void layout_columns_value() {
    CHECK(ColumnsFromInt(1) == Columns::One);
    CHECK(ColumnsFromInt(2) == Columns::Two);
    CHECK(ColumnsFromInt(0) == Columns::Two);    // anything else: the default, two columns
    CHECK(ColumnsFromInt(-4) == Columns::Two);
    CHECK(ColumnsFromInt(99) == Columns::Two);
    CHECK(ColumnsToInt(Columns::One) == 1 && ColumnsToInt(Columns::Two) == 2);
    CHECK(std::string(ColumnsLabel(Columns::One)) == "One column");
    CHECK(std::string(ColumnsLabel(Columns::Two)) == "Two columns");
    CHECK(Other(Columns::One) == Columns::Two && Other(Columns::Two) == Columns::One);
}
static void layout_divider_clamp() {
    CHECK_NEAR(ClampTreeWidth(800, 200), 200, 0.01);
    CHECK_NEAR(ClampTreeWidth(800, 10), kMinTreeW, 0.01);          // not smaller than the minimum
    CHECK_NEAR(ClampTreeWidth(2000, 5000), kMaxTreeW, 0.01);       // not bigger than the maximum
    // the right side always keeps its minimum width
    CHECK_NEAR(ClampTreeWidth(500, 480), 500 - kMinContentW - kSplitterW, 0.01);
    // a very narrow panel: still sane, never negative, never wider than the panel
    const float n = ClampTreeWidth(100, 200);
    CHECK(n >= 0 && n < 100);
    CHECK(ClampTreeWidth(0, 200) == 0);
}
static void layout_split_widths() {
    const Split two = SplitWidths(Columns::Two, 900, 250);
    CHECK_NEAR(two.tree, 250, 0.01);
    CHECK_NEAR(two.splitter, kSplitterW, 0.01);
    CHECK_NEAR(two.tree + two.splitter + two.content, 900, 0.01);
    CHECK(two.content >= kMinContentW);
    const Split one = SplitWidths(Columns::One, 900, 250);
    CHECK(one.tree == 0 && one.splitter == 0);
    CHECK_NEAR(one.content, 900, 0.01);
}
static void layout_divider_drag() {
    CHECK_NEAR(DragTreeWidth(800, 200, 30), 230, 0.01);
    CHECK_NEAR(DragTreeWidth(800, 200, -500), kMinTreeW, 0.01);
    CHECK_NEAR(DragTreeWidth(600, 200, 5000), 600 - kMinContentW - kSplitterW, 0.01);
}
static void layout_indent_and_arrow() {
    CHECK(RowIndent(0) == 0);
    CHECK(RowIndent(2) == 2 * kIndentStep);
    CHECK(RowIndent(-1) == 0);
    CHECK(OnDisclosureArrow(0, 4.0f, 10.0f));       // inside the arrow slot of a top-level row
    CHECK(!OnDisclosureArrow(0, 4.0f, 60.0f));      // on the name
    CHECK(OnDisclosureArrow(2, 4.0f, 4.0f + RowIndent(2) + 5.0f));
    CHECK(!OnDisclosureArrow(2, 4.0f, 4.0f + 3.0f));  // left of this row's arrow
}

// A small fake folder tree:  A(folder) { a1.txt, B(folder){ b1.txt }, a2.txt }   C(folder, empty)   z.txt
static std::vector<Item> kidsOf(const std::string& id) {
    static const std::map<std::string, std::vector<Item>> m = {
        {"", {{"A", true}, {"C", true}, {"z.txt", false}}},
        {"A", {{"A/a1.txt", false}, {"A/B", true}, {"A/a2.txt", false}}},
        {"A/B", {{"A/B/b1.txt", false}}},
        {"C", {}},
    };
    auto it = m.find(id);
    return it == m.end() ? std::vector<Item>() : it->second;
}
static std::vector<std::string> ids(const std::vector<Row>& r) { std::vector<std::string> o; for (auto& x : r) o.push_back(x.id); return o; }

static void layout_flatten_closed() {
    const auto rows = FlattenOpen("", {}, kidsOf);
    CHECK((ids(rows) == std::vector<std::string>{"A", "C", "z.txt"}));
    CHECK(rows[0].depth == 0 && rows[0].isFolder && !rows[0].open);
    CHECK(!rows[2].isFolder);
}
static void layout_flatten_open_in_place() {
    const auto rows = FlattenOpen("", {"A"}, kidsOf);
    CHECK((ids(rows) == std::vector<std::string>{"A", "A/a1.txt", "A/B", "A/a2.txt", "C", "z.txt"}));
    CHECK(rows[0].open);
    CHECK(rows[1].depth == 1 && rows[2].depth == 1 && rows[2].isFolder && !rows[2].open);
    const auto deep = FlattenOpen("", {"A", "A/B"}, kidsOf);
    CHECK(deep.size() == 7);
    CHECK(deep[3].id == "A/B/b1.txt" && deep[3].depth == 2);
    // an open folder whose parent is closed stays hidden
    const auto hidden = FlattenOpen("", {"A/B"}, kidsOf);
    CHECK(hidden.size() == 3);
}
static void layout_flatten_guards() {
    // a loop in the data (a folder that contains itself) must stop at the depth cap
    auto loop = [](const std::string&) { return std::vector<Item>{{"L", true}}; };
    const auto rows = FlattenOpen("", {"L"}, loop, 5);
    CHECK(rows.size() <= 6);
    CHECK(FlattenOpen("nothing", {}, kidsOf).empty());
}

static std::vector<Row> sample() { return FlattenOpen("", {"A"}, kidsOf); }   // A, a1, B, a2, C, z

static void layout_keys_up_down() {
    const auto r = sample();
    CHECK(MoveSelection(r, 0, Key::Down).select == 1);
    CHECK(MoveSelection(r, 5, Key::Down).select == 5);   // stops at the end
    CHECK(MoveSelection(r, 0, Key::Up).select == 0);     // stops at the top
    CHECK(MoveSelection(r, 3, Key::Up).select == 2);
    CHECK(MoveSelection(r, -1, Key::Down).select == 0);  // nothing selected: first row
    CHECK(MoveSelection(r, -1, Key::Up).select == 0);
    CHECK(MoveSelection(r, 2, Key::Home).select == 0);
    CHECK(MoveSelection(r, 2, Key::End).select == 5);
    CHECK(MoveSelection({}, -1, Key::Down).select == -1);
}
static void layout_keys_right_left() {
    auto r = sample();
    // Right on a closed folder opens it
    Move m = MoveSelection(r, 2, Key::Right);          // B is closed
    CHECK(m.select == 2 && m.expand == 2 && m.collapse == -1);
    // Right on an open folder steps into its first item
    m = MoveSelection(r, 0, Key::Right);
    CHECK(m.select == 1 && m.expand == -1);
    // Right on a file does nothing
    m = MoveSelection(r, 1, Key::Right);
    CHECK(m.select == 1 && m.expand == -1 && m.collapse == -1);
    // Left on an open folder closes it
    m = MoveSelection(r, 0, Key::Left);
    CHECK(m.select == 0 && m.collapse == 0);
    // Left on a file (or closed folder) goes to the folder that holds it
    m = MoveSelection(r, 1, Key::Left);
    CHECK(m.select == 0 && m.collapse == -1);
    m = MoveSelection(r, 2, Key::Left);
    CHECK(m.select == 0 && m.collapse == -1);
    // Left on a top-level closed row: nowhere to go
    m = MoveSelection(r, 4, Key::Left);
    CHECK(m.select == 4 && m.collapse == -1);
    // Right on an open but empty folder does not move
    std::vector<Row> e = {{"E", 0, true, true}, {"f", 0, false, false}};
    m = MoveSelection(e, 0, Key::Right);
    CHECK(m.select == 0);
}
static void layout_keys_enter() {
    const auto r = sample();
    CHECK(MoveSelection(r, 1, Key::Enter).open == 1);
    CHECK(MoveSelection(r, -1, Key::Enter).open == -1);
    CHECK(MoveSelection(r, 1, Key::Down).open == -1);
}
static void layout_rows_find_and_parent() {
    const auto r = sample();
    CHECK(IndexOfId(r, "A/B") == 2);
    CHECK(IndexOfId(r, "nope") == -1);
    CHECK(ParentRow(r, 3) == 0);
    CHECK(ParentRow(r, 0) == -1);
    CHECK(HasChildrenShown(r, 0));
    CHECK(!HasChildrenShown(r, 2));
}

static void layout_saved_with_editor_layout() {
    using okay::edlayout::ViewPrefs;
    // an old settings file with no project field loads as two columns
    ViewPrefs old = ViewPrefs::Parse("showcamhud 1\ndensity 1\nlayoutpreset 2\n");
    CHECK(old.projectColumns == 2);
    CHECK(old.camHud);
    ViewPrefs p;
    CHECK(p.projectColumns == 2);
    p.projectColumns = 1;
    const ViewPrefs back = ViewPrefs::Parse(p.Write());
    CHECK(back.projectColumns == 1);
    CHECK(ViewPrefs::Parse("projectcolumns 7\n").projectColumns == 2);   // junk falls back to the default
    CHECK(ViewPrefs::Parse("projectcolumns 0\n").projectColumns == 2);
    CHECK(ViewPrefs::Parse("projectcolumns 1\n").projectColumns == 1);
    // the other keys survive next to it
    ViewPrefs q; q.camHud = true; q.projectColumns = 1; q.layoutPreset = 3;
    const ViewPrefs q2 = ViewPrefs::Parse(q.Write());
    CHECK(q2.camHud && q2.projectColumns == 1 && q2.layoutPreset == 3);
}

int main() {
    layout_columns_value(); layout_divider_clamp(); layout_split_widths(); layout_divider_drag();
    layout_indent_and_arrow(); layout_flatten_closed(); layout_flatten_open_in_place(); layout_flatten_guards();
    layout_keys_up_down(); layout_keys_right_left(); layout_keys_enter(); layout_rows_find_and_parent();
    layout_saved_with_editor_layout();
    TEST_MAIN_RESULT();
}
