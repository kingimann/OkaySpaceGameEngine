// The editor's Hierarchy panel drawn with OkayUI (okayui/screens/OkayUIEditorPanels.cpp DrawHierarchy) and the toolkit pieces it
// needed: drag and drop that a list can draw itself and that can come from another toolkit, and menus (with sub-menus) inside a popup
// (docs/own_ui_toolkit.md section 12). Driven headless: every control is found by its rectangle in the result and used with synthetic
// input; a small host (okayui/tests/HierarchyFixtures.hpp) carries out the intents the way the editor does; golden images (launcher
// look) pin what is drawn. The Dear ImGui version is compared against this one in tests/test_okayui_hierarchy_imgui.cpp.
#include "test_framework.hpp"
#include "OkayUITestKit.hpp"
#include "OkayUIEditorPanels.hpp"
#include "HierarchyFixtures.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

using namespace okayuitest;
namespace UI = OkayUI;
namespace S = okayuiscreens;
namespace P = okaypanels;

static void Check(const char* name, const Bitmap& b) {
    std::string why;
    const bool ok = MatchesGolden(name, b, &why);
    if (!ok) std::cerr << "  golden " << name << ": " << why << " (actual written as " << name << ".actual.png)\n";
    CHECK(ok);
}
template <class R> static float CX(const R& r) { return r.x + r.w * 0.5f; }
template <class R> static float CY(const R& r) { return r.y + r.h * 0.5f; }
static const int kBg[3] = {30, 33, 41};
static bool HasLog(const hierfix::Scene& s, const std::string& line) {
    for (const std::string& l : s.log) if (l == line) return true;
    return false;
}

// One panel on a canvas: the scene, the panel's state, the last result. Each frame builds the model from the scene, draws, and
// carries out the intent.
struct HRig {
    Sim sim;
    hierfix::Scene scene;
    P::HierarchyState st;
    S::HierarchyResult res;
    P::HierarchyIntent seen;       // the last intent with anything in it
    bool focused = true;
    int reveal = -1;
    std::string extType, extText; bool extReleased = false;
    HRig(hierfix::Scene s, float w = 420.0f, float h = 520.0f) : sim(w, h), scene(std::move(s)) {
        Reset(); UI::UseLauncherLook(true); UI::Style().caretBlink = false;
    }
    ~HRig() { UI::UseLauncherLook(false); }
    void Frame() {
        sim.Frame([&] {
            UI::PushTextScale(S::kEditorTextScale);
            P::HierarchyModel m = scene.Model(st.filter);
            m.focused = focused; m.revealRow = reveal; reveal = -1;
            m.externalDragType = extType; m.externalDragText = extText; m.externalDragReleased = extReleased;
            res = S::DrawHierarchy(m, st, 16.0f, 12.0f, sim.W - 32.0f, sim.H - 24.0f);
            UI::PopTextScale();
        });
        const P::HierarchyIntent& in = res.intent;
        if (in.select || in.toggle || in.rangeTo || in.selectAll || in.toggleActive || in.toggleLock || in.openId || in.frame ||
            in.renameId || in.renameBegan || in.dragged || !in.dropAsset.empty() || !in.menuId.empty()) seen = in;
        scene.Apply(in);
    }
    void Frames(int n) { for (int i = 0; i < n; ++i) Frame(); }
    void At(float x, float y) { sim.Move(x, y); Frame(); }
    void Click(float x, float y) { sim.Move(x, y); Frame(); sim.Down(); Frame(); sim.Up(); Frame(); }
    void DoubleClick(float x, float y) { sim.Move(x, y); Frame(); sim.Down(); Frame(); sim.Up(); Frame(); sim.Down(); Frame(); sim.Up(); Frame(); }
    void RightClick(float x, float y) { sim.Move(x, y); Frame(); sim.rdown = true; Frame(); sim.rdown = false; Frame(); }
    void Key(UI::KeyId k) { sim.Hold(k, true); Frame(); sim.Hold(k, false); Frame(); }
    // the drawn row of model row `index` (zero size when it is not drawn)
    UI::Rect Row(int index) const {
        for (std::size_t i = 0; i < res.rowIndex.size(); ++i) if (res.rowIndex[i] == index) return res.rows[i];
        return UI::Rect{};
    }
    int RowOfName(const std::string& n) const {
        for (int i = 0; i < (int)scene.rows.size(); ++i) if (scene.nodes[(std::size_t)scene.rows[(std::size_t)i]].name == n) return i;
        return -1;
    }
    UI::Rect RowNamed(const std::string& n) const { return Row(RowOfName(n)); }
    int Selected(const std::string& n) const {
        for (int i = 0; i < (int)scene.nodes.size(); ++i) if (scene.nodes[(std::size_t)i].name == n) return scene.selected.count(i) ? 1 : 0;
        return -1;
    }
    UI::Rect MenuRow(const std::string& id) const {
        for (std::size_t i = 0; i < res.menuIds.size(); ++i) if (res.menuIds[i] == id) return res.menuRows[i];
        return UI::Rect{};
    }
    const Bitmap& Draw() { return sim.Draw((std::uint8_t)kBg[0], (std::uint8_t)kBg[1], (std::uint8_t)kBg[2]); }
};

int main() {
    RUN_SUITE("okayui hierarchy");

    // ================= the shared rules =================
    {
        CHECK(P::HierTail(3, true) == "  (3)" && P::HierTail(0, false) == "  (off)" && P::HierTail(2, false) == "  (2)  (off)" && P::HierTail(0, true).empty());
        CHECK(P::HierHeaderLabel("", "Level") == "Level (main)" && P::HierHeaderLabel("", "") == "Main Scene" && P::HierHeaderLabel("Extra.okayscene", "Level") == "Extra.okayscene");
        CHECK(P::HierSceneTitle("", false) == "Untitled" && P::HierSceneTitle("Village", true) == "Village  \xC2\xB7  unsaved");
        CHECK(P::HierSearchFooter(0, "rock") == "No objects match 'rock'." && P::HierSearchFooter(12, "rock") == "12 match(es)");
        CHECK(P::HierDropZone(0.1f) == P::HierDropBefore && P::HierDropZone(0.5f) == P::HierDropInside && P::HierDropZone(0.9f) == P::HierDropAfter);
        CHECK(P::HierDropZone(0.30f) == P::HierDropInside && P::HierDropZone(0.70f) == P::HierDropInside);
        CHECK(P::HierDragLabel("Rock", true, 4) == "Rock  (+3 more)" && P::HierDragLabel("Rock", false, 4) == "Rock" && P::HierDragLabel("Rock", true, 1) == "Rock");
        // ellipsis: one pixel a byte, so the cut is easy to see; UTF-8 is never split
        auto w1 = [](const std::string& t) { return (float)t.size(); };
        CHECK(P::HierEllipsize("abcdefghijklmnopqrstuvwxyz", 15.0f, w1) == "abcdefghijkl\xE2\x80\xA6");
        CHECK(P::HierEllipsize("short", 15.0f, w1) == "short" && P::HierEllipsize("abcdefghijklmnopqrstuvwxyz", 8.0f, w1).size() == 26);
        const std::string cut = P::HierEllipsize("ab\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9xyz", 13.0f, w1);
        CHECK(cut == "ab\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xE2\x80\xA6");   // whole characters only: no first byte left without its second
        const std::vector<int> order = {5, 2, 9, 7, 1};
        CHECK((P::HierRange(order, 9, 1) == std::vector<int>{9, 7, 1}) && (P::HierRange(order, 1, 2) == std::vector<int>{2, 9, 7, 1}));
        CHECK(P::HierRange(order, 4, 1).empty());
        // the clip: 3,000 rows of 26 pixels scrolled to 26,000 in a 400-pixel view show rows 999..1017 (one extra each side)
        const P::HierClipRange c = P::HierVisibleRange(3000, 26.0f, 26000.0f, 400.0f);
        CHECK(c.first == 999 && c.last == 1017);
        CHECK(P::HierVisibleRange(0, 26.0f, 0.0f, 400.0f).last == 0 && P::HierVisibleRange(10, 0.0f, 0.0f, 400.0f).last == 10);
        CHECK(P::HierVisibleRange(20000, 26.0f, 1e30f, 400.0f).first == 20000);   // a huge scroll: nothing, no overflow
        CHECK(P::HierRevealScroll(2, 26.0f, 0.0f, 400.0f) == 0.0f && P::HierRevealScroll(1000, 26.0f, 0.0f, 400.0f) > 25000.0f);
        const std::vector<P::PanelMenuEntry> opts = P::HierOptionsMenu(true);
        CHECK(opts.size() == 4 && opts[3].kind == P::PanelMenuToggle && opts[3].checked && opts[0].id == "h.expandall");
    }

    // ================= the tree: rows, golden =================
    {
        HRig r(hierfix::Village());
        r.At(-50.0f, -50.0f); r.Frames(2);
        // 15 rows: Main Camera, Sun, Houses (open), House 1-3 (House 3 closed over its Chimney), Player (open), Hat, Cart, Smoke, Sign, Canvas (open), Start Button, Volume, Music
        CHECK(r.scene.rows.size() == 15);
        CHECK(r.res.rows.size() == r.scene.rows.size());          // all of them fit
        CHECK(r.res.rowHeight > 20.0f && r.res.rowHeight < 30.0f);
        CHECK(r.st.rowsFilled <= (int)r.scene.rows.size() + 1);   // every row once (and the active one again for the keys)
        const UI::Rect house1 = r.RowNamed("House 1"), houses = r.RowNamed("Houses");
        CHECK(house1.x > houses.x + 15.0f);                       // indented one level
        CHECK(r.res.arrows[(std::size_t)r.RowOfName("Houses")].w > 0.0f && r.res.arrows[(std::size_t)r.RowOfName("Sun")].w == 0.0f);
        Check("hierarchy_tree", r.Draw());
    }

    // ================= selection: click, Ctrl, Shift; the arrow; the eye; the padlock =================
    {
        HRig r(hierfix::Village());
        r.Frames(2);
        const UI::Rect sun = r.RowNamed("Sun");
        r.Click(sun.x + 120.0f, CY(sun));
        CHECK(r.Selected("Sun") == 1 && r.Selected("Player") == 0 && r.scene.selected.size() == 1);
        r.sim.ctrl = true;
        r.Click(r.RowNamed("Music").x + 120.0f, CY(r.RowNamed("Music")));
        r.sim.ctrl = false;
        CHECK(r.Selected("Sun") == 1 && r.Selected("Music") == 1);
        r.sim.ctrl = true;   // Ctrl+click again takes it out
        r.Click(r.RowNamed("Music").x + 120.0f, CY(r.RowNamed("Music")));
        r.sim.ctrl = false;
        CHECK(r.Selected("Music") == 0 && r.Selected("Sun") == 1);
        // Shift+click: from the active object (Sun) to House 2, in listed order
        r.Click(sun.x + 120.0f, CY(sun));
        r.sim.shift = true;
        r.Click(r.RowNamed("House 2").x + 120.0f, CY(r.RowNamed("House 2")));
        r.sim.shift = false;
        CHECK(r.scene.selected.size() == 4 && r.Selected("Houses") == 1 && r.Selected("House 1") == 1 && r.Selected("House 2") == 1 && r.Selected("Main Camera") == 0);
        // a plain click on one of several selected rows selects just it, on release (a drag would move them all)
        const UI::Rect h1 = r.RowNamed("House 1");
        r.sim.Move(h1.x + 120.0f, CY(h1)); r.Frame(); r.sim.Down(); r.Frame();
        CHECK(r.scene.selected.size() == 4);
        r.sim.Up(); r.Frame();
        CHECK(r.scene.selected.size() == 1 && r.Selected("House 1") == 1);
        // the arrow folds Houses in (and selects it, as Dear ImGui's tree node does)
        const std::size_t before = r.scene.rows.size();
        const UI::Rect arrow = r.res.arrows[(std::size_t)r.RowOfName("Houses")];
        r.Click(arrow.x + 18.0f, CY(arrow)); r.Frame();
        CHECK(r.scene.rows.size() == before - 3 && r.Selected("Houses") == 1);
        r.Click(arrow.x + 18.0f, CY(arrow)); r.Frame();
        CHECK(r.scene.rows.size() == before);
        // the eye and the padlock act on the row, not on the selection
        const std::size_t ei = (std::size_t)r.RowOfName("Sun");
        r.Click(CX(r.res.eyes[ei]), CY(r.res.eyes[ei]));
        CHECK(HasLog(r.scene, "active Sun") && !r.scene.nodes[1].active && r.Selected("Sun") == 0);
        r.Click(CX(r.res.locks[ei]), CY(r.res.locks[ei]));
        CHECK(HasLog(r.scene, "lock Sun") && r.scene.nodes[1].locked);
        // hovering the eye shows its tip; the padlock shows on the hovered row only
        r.At(CX(r.res.eyes[ei]), CY(r.res.eyes[ei]));
        CHECK(r.res.eyes[ei].w == P::kHierEyeW);
    }

    // ================= rename in place =================
    {
        HRig r(hierfix::Village());
        r.Frames(2);
        const UI::Rect sun = r.RowNamed("Sun");
        r.DoubleClick(sun.x + 120.0f, CY(sun));
        r.Frame();
        CHECK(r.st.renameId == hierfix::Scene::Id(1) && r.res.renameField.w > 40.0f && std::string(r.st.renameBuf) == "Sun");
        CHECK(r.seen.renameBegan == hierfix::Scene::Id(1));
        Check("hierarchy_rename", r.Draw());
        r.sim.Press(Key_SelectAll); r.Frame();
        r.sim.Type("Moon"); r.Frame();
        r.sim.Press(Key_Enter); r.Frame(); r.Frame();
        CHECK(r.scene.nodes[1].name == "Moon" && r.st.renameId == 0 && HasLog(r.scene, "rename Moon"));
        // Escape gives up
        r.DoubleClick(r.RowNamed("Moon").x + 120.0f, CY(r.RowNamed("Moon")));
        r.sim.Type("x"); r.Frame();
        r.sim.Press(Key_Escape); r.Frame(); r.Frame();
        CHECK(r.scene.nodes[1].name == "Moon" && r.st.renameId == 0);
        // F2 renames the active object; a click elsewhere keeps the new name
        r.Click(r.RowNamed("Music").x + 120.0f, CY(r.RowNamed("Music")));
        r.Key(UI::KeyId_F2);
        CHECK(r.st.renameId == hierfix::Scene::Id(r.scene.active));
        r.sim.Press(Key_SelectAll); r.Frame();
        r.sim.Type("Song"); r.Frame();
        r.Click(r.RowNamed("Moon").x + 120.0f, CY(r.RowNamed("Moon")));
        r.Frame();
        CHECK(r.scene.nodes[(std::size_t)r.scene.nodes.size() - 1].name == "Song" && r.st.renameId == 0);
        // an empty name is refused
        r.DoubleClick(r.RowNamed("Moon").x + 120.0f, CY(r.RowNamed("Moon")));
        r.sim.Press(Key_SelectAll); r.Frame();
        r.sim.Press(Key_Backspace); r.Frame();
        r.sim.Press(Key_Enter); r.Frame(); r.Frame();
        CHECK(r.scene.nodes[1].name == "Moon");
    }

    // ================= the right-click menus, the [+] and the options =================
    {
        HRig r(hierfix::Village(), 720.0f, 520.0f);   // wide enough for the sub-menus to open on the right (they flip left at the edge)
        r.Frames(2);
        const UI::Rect cam = r.RowNamed("Main Camera");
        r.RightClick(cam.x + 120.0f, CY(cam));
        r.Frame();
        CHECK(r.st.menuKind == P::HierMenuRow && r.st.menuRow == hierfix::Scene::Id(0));
        CHECK(r.Selected("Main Camera") == 1 && r.Selected("Player") == 0);   // right-click selects the row first
        CHECK(r.res.menu.w > 100.0f && r.MenuRow("h.copy").w > 0.0f && r.MenuRow("h.order").w > 0.0f);
        Check("hierarchy_menu", r.Draw());
        // a greyed-out item takes no click
        const UI::Rect un = r.MenuRow("h.unparent");
        r.Click(CX(un), CY(un));
        CHECK(r.seen.menuId.empty() && r.st.menuKind == P::HierMenuRow);
        // the Order sub-menu opens on hover; Move Down in it is chosen and the whole menu closes
        const UI::Rect order = r.MenuRow("h.order");
        r.At(CX(order), CY(order)); r.Frame();
        r.At(order.x + order.w + 40.0f, CY(order)); r.Frame();
        // the sub-menu's first row sits level with Order, the second one row below
        r.Click(order.x + order.w + 40.0f, CY(order) + r.MenuRow("h.order").h + 4.0f);

        r.Frame();
        CHECK(r.seen.menuId == "h.movedown" && r.seen.menuKind == P::HierMenuRow && r.st.menuKind == P::HierMenuNone);
        // Rename from the menu
        r.RightClick(cam.x + 120.0f, CY(cam)); r.Frame();
        const UI::Rect rn = r.MenuRow("h.rename");
        r.Click(CX(rn), CY(rn)); r.Frame();
        CHECK(r.seen.menuId == "h.rename" && r.st.menuKind == P::HierMenuNone);
        // a click outside closes the menu without choosing
        r.RightClick(cam.x + 120.0f, CY(cam)); r.Frame();
        CHECK(r.st.menuKind == P::HierMenuRow);
        r.seen = P::HierarchyIntent();
        r.Click(r.sim.W - 30.0f, r.sim.H - 30.0f); r.Frame();
        CHECK(r.st.menuKind == P::HierMenuNone && r.seen.menuId.empty());
        // the empty space: its own menu (Sort A-Z ticked through the host)
        r.RightClick(160.0f, r.sim.H - 60.0f); r.Frame();
        CHECK(r.st.menuKind == P::HierMenuEmpty && r.MenuRow("h.sortaz").w > 0.0f);
        const UI::Rect sort = r.MenuRow("h.sortaz");
        r.Click(CX(sort), CY(sort)); r.Frame();
        CHECK(r.seen.menuId == "h.sortaz" && r.scene.sortAZ);
        // [+]: the create menu, its 3D Object sub-menu, Cube
        r.Click(CX(r.res.createButton), CY(r.res.createButton)); r.Frame();
        CHECK(r.st.menuKind == P::HierMenuCreate && r.MenuRow("create.3d").w > 0.0f);
        const UI::Rect d3 = r.MenuRow("create.3d");
        r.At(CX(d3), CY(d3)); r.Frame();
        r.Click(d3.x + d3.w + 30.0f, CY(d3)); r.Frame();
        CHECK(r.seen.menuId == "create.3d/0:Cube" && r.seen.menuKind == P::HierMenuCreate);
        // [...]: the options, lit while Sort A-Z is on; Collapse all
        r.Click(CX(r.res.optionsButton), CY(r.res.optionsButton)); r.Frame();
        CHECK(r.st.menuKind == P::HierMenuOptions && r.MenuRow("h.collapseall").w > 0.0f);
        const UI::Rect ca = r.MenuRow("h.collapseall");
        r.Click(CX(ca), CY(ca)); r.Frame();
        CHECK(r.seen.menuId == "h.collapseall" && r.seen.menuKind == P::HierMenuOptions);
    }

    // ================= drag and drop: before, inside, after, the empty space; Escape; a file from another panel =================
    {
        HRig r(hierfix::Village());
        r.Frames(2);
        auto drag = [&](const UI::Rect& from, float tx, float ty, bool escape = false) {
            r.sim.Move(from.x + 140.0f, CY(from)); r.Frame();
            r.sim.Down(); r.Frame();
            r.sim.Move(from.x + 150.0f, CY(from) + 10.0f); r.Frame();
            r.sim.Move(tx, ty); r.Frame(); r.Frame();
            if (escape) { r.sim.Press(Key_Escape); r.Frame(); }
            r.sim.Up(); r.Frame(); r.Frame();
        };
        // Music onto the middle of Houses: inside it
        UI::Rect houses = r.RowNamed("Houses");
        drag(r.RowNamed("Music"), houses.x + 140.0f, CY(houses));
        CHECK(HasLog(r.scene, "drop Music inside Houses") && r.scene.nodes.back().parent == 2);
        // Sun onto the top edge of House 3: before it (a sibling under Houses)
        UI::Rect h3 = r.RowNamed("House 3");
        drag(r.RowNamed("Sun"), h3.x + 140.0f, h3.y + 2.0f);
        CHECK(HasLog(r.scene, "drop Sun before House 3") && r.scene.nodes[1].parent == 2);
        // House 1 onto the bottom edge of Player: after it
        UI::Rect pl = r.RowNamed("Player");
        drag(r.RowNamed("House 1"), pl.x + 140.0f, pl.y + pl.h - 2.0f);
        CHECK(HasLog(r.scene, "drop House 1 after Player"));
        // the drop line shows while it hovers (golden): Campfire Smoke over Canvas's middle, held
        {
            const UI::Rect from = r.RowNamed("Campfire Smoke"), to = r.RowNamed("Canvas");
            r.sim.Move(from.x + 140.0f, CY(from)); r.Frame(); r.sim.Down(); r.Frame();
            r.sim.Move(from.x + 150.0f, CY(from) + 10.0f); r.Frame();
            r.sim.Move(to.x + 140.0f, CY(to)); r.Frame(); r.Frame();
            CHECK(r.st.dragging == hierfix::Scene::Id(10));
            Check("hierarchy_drag", r.Draw());
            r.sim.Press(Key_Escape); r.Frame();   // Escape: no drop
            r.sim.Up(); r.Frame(); r.Frame();
            CHECK(!HasLog(r.scene, "drop Campfire Smoke inside Canvas") && r.scene.nodes[10].parent == -1);
        }
        // a child onto the empty space: to the top level
        const UI::Rect hat = r.RowNamed("Hat");
        {
            const UI::Rect from = hat;
            r.sim.Move(from.x + 140.0f, CY(from)); r.Frame(); r.sim.Down(); r.Frame();
            r.sim.Move(from.x + 150.0f, CY(from) + 10.0f); r.Frame();
            r.sim.Move(160.0f, r.res.emptyArea.y + 10.0f); r.Frame(); r.Frame();
            r.sim.Up(); r.Frame(); r.Frame();
        }
        CHECK(HasLog(r.scene, "drop Hat root") && r.scene.nodes[(std::size_t)r.scene.nodes.size() - 1].parent >= -1);
        // a row dropped on itself does nothing
        const std::size_t logs = r.scene.log.size();
        UI::Rect cart = r.RowNamed("Cart (variant)");
        drag(cart, cart.x + 160.0f, CY(cart));
        CHECK(r.scene.log.size() == logs || r.scene.log.back().find("drop Cart") == 0);
        // a file from the Project panel (an outside drag): over Canvas, let go
        houses = r.RowNamed("Canvas");
        r.extType = "ASSET_PATH"; r.extText = "Assets/Crate.okayprefab";
        r.sim.Move(houses.x + 140.0f, CY(houses)); r.Frame(); r.Frame();
        r.extReleased = true; r.Frame();
        r.extType.clear(); r.extText.clear(); r.extReleased = false; r.Frame();
        CHECK(HasLog(r.scene, "asset Assets/Crate.okayprefab on Canvas"));
        CHECK(r.seen.dropWhere == P::HierDropInside);
        // and onto the empty space
        r.extType = "ASSET_PATH"; r.extText = "Assets/Level2.okayscene";
        r.sim.Move(160.0f, r.res.emptyArea.y + 10.0f); r.Frame(); r.Frame();
        r.extReleased = true; r.Frame();
        r.extType.clear(); r.extText.clear(); r.extReleased = false; r.Frame();
        CHECK(HasLog(r.scene, "asset Assets/Level2.okayscene at top"));
    }

    // ================= several selected rows dragged together =================
    {
        HRig r(hierfix::Village());
        r.Frames(2);
        r.Click(r.RowNamed("Sun").x + 120.0f, CY(r.RowNamed("Sun")));
        r.sim.ctrl = true; r.Click(r.RowNamed("Music").x + 120.0f, CY(r.RowNamed("Music"))); r.sim.ctrl = false;
        CHECK(r.scene.selected.size() == 2);
        const UI::Rect from = r.RowNamed("Sun"), to = r.RowNamed("Canvas");
        r.sim.Move(from.x + 140.0f, CY(from)); r.Frame(); r.sim.Down(); r.Frame();
        r.sim.Move(from.x + 150.0f, CY(from) + 10.0f); r.Frame();
        r.sim.Move(to.x + 140.0f, CY(to)); r.Frame();
        r.sim.Up(); r.Frame(); r.Frame();
        CHECK(r.seen.dragged == hierfix::Scene::Id(1) && r.seen.dropTarget == hierfix::Scene::Id(12) && r.seen.dropWhere == P::HierDropInside);
        CHECK(r.scene.selected.size() == 2);   // the release after a drag does not shrink the selection
    }

    // ================= keys: arrows, Left / Right, Ctrl+A; nothing without the keyboard =================
    {
        HRig r(hierfix::Village());
        r.Frames(2);
        r.Click(r.RowNamed("Sun").x + 120.0f, CY(r.RowNamed("Sun")));
        r.Key(UI::KeyId_Down);
        CHECK(r.Selected("Houses") == 1 && r.scene.selected.size() == 1);
        r.Key(UI::KeyId_Left);   // fold Houses in
        CHECK(r.scene.nodes[2].open == 0);
        r.Key(UI::KeyId_Down);
        CHECK(r.Selected("Player") == 1);
        r.Key(UI::KeyId_Up); r.Key(UI::KeyId_Right);
        CHECK(r.Selected("Houses") == 1 && r.scene.nodes[2].open == 1);
        r.sim.ctrl = true; r.Key(UI::KeyId_A); r.sim.ctrl = false;
        CHECK(r.scene.selected.size() == r.scene.rows.size());
        r.Click(r.RowNamed("Sun").x + 120.0f, CY(r.RowNamed("Sun")));
        r.focused = false;
        r.Key(UI::KeyId_Down);
        CHECK(r.Selected("Sun") == 1);
    }

    // ================= search: the flat list, Ctrl+click, a double click looks at it, the footer =================
    {
        HRig r(hierfix::Village());
        r.Frames(2);
        r.Click(CX(r.res.search), CY(r.res.search));
        r.sim.Type("House"); r.Frame(); r.Frame();
        CHECK(r.res.rows.size() == 4 && r.seen.select == 0);
        Check("hierarchy_search", r.Draw());
        r.Click(r.res.rows[1].x + 160.0f, CY(r.res.rows[1]));
        CHECK(r.Selected("House 1") == 1 && r.scene.selected.size() == 1);
        r.sim.ctrl = true; r.Click(r.res.rows[2].x + 160.0f, CY(r.res.rows[2])); r.sim.ctrl = false;
        CHECK(r.Selected("House 2") == 1 && r.scene.selected.size() == 2);
        r.DoubleClick(r.res.rows[3].x + 160.0f, CY(r.res.rows[3]));
        CHECK(HasLog(r.scene, "frame House 3"));
        r.Click(CX(r.res.search), CY(r.res.search));
        r.sim.Press(Key_SelectAll); r.Frame(); r.sim.Type("zzz"); r.Frame(); r.Frame();
        CHECK(r.res.rows.empty() && std::string(r.st.filter) == "zzz");
    }

    // ================= big scenes: only the rows in view; the wheel; reveal =================
    for (int count : {3000, 20000}) {
        HRig r(hierfix::Big(count), 420.0f, 520.0f);
        r.Frames(2);
        CHECK(r.res.rows.size() < 25 && r.st.rowsFilled < 25);
        r.sim.Move(200.0f, 300.0f);
        for (int i = 0; i < 30; ++i) { r.sim.wheel = -3.0f; r.Frame(); }
        CHECK(r.res.scrollY > 300.0f);
        CHECK(r.st.rowsFilled < 25 && !r.res.rowIndex.empty() && r.res.rowIndex.front() > 5);
        // reveal: a row picked in the Scene view is scrolled to
        const int target = (int)r.scene.rows.size() - 40;
        r.scene.selected = {r.scene.rows[(std::size_t)target]}; r.scene.active = r.scene.rows[(std::size_t)target];
        r.reveal = target; r.Frame(); r.Frame();
        CHECK(r.Row(target).w > 0.0f);
        CHECK(r.st.rowsFilled < 30);
        // a click on a far row selects it
        const UI::Rect t = r.Row(target);
        r.Click(t.x + 120.0f, CY(t));
        CHECK(r.scene.active == r.scene.rows[(std::size_t)target]);
        std::printf("  %d objects: %d rows asked for in a frame, scrolled to %.0f px\n", count, r.st.rowsFilled, r.res.scrollY);
    }

    // ================= an empty scene, and the combined-scene headers =================
    {
        hierfix::Scene e; e.name = "";
        HRig r(e);
        r.Frames(2);
        CHECK(r.res.empty && r.res.rows.empty());
        Check("hierarchy_empty", r.Draw());
        // right-click there still offers the create menu
        r.RightClick(200.0f, 300.0f); r.Frame();
        CHECK(r.st.menuKind == P::HierMenuEmpty);
    }
    {
        // a header row (the fixture has none: the model of a combined scene is made by hand)
        hierfix::Scene s = hierfix::Village();
        Sim sim(420, 300); Reset(); UI::UseLauncherLook(true);
        P::HierarchyState st;
        S::HierarchyResult res;
        auto frame = [&] {
            sim.Frame([&] {
                UI::PushTextScale(S::kEditorTextScale);
                P::HierarchyModel m = s.Model();
                m.rowCount = 3;
                m.fillRow = [&](int i, P::HierRow& row) {
                    if (i == 0) { row.header = true; row.headerMain = true; row.name = P::HierHeaderLabel("", "Village"); }
                    else if (i == 1) s.Fill(0, 0, row);
                    else { row.header = true; row.name = P::HierHeaderLabel("Caves.okayscene", "Village"); }
                };
                res = S::DrawHierarchy(m, st, 16, 12, 388, 276);
                UI::PopTextScale();
            });
        };
        frame(); frame();
        CHECK(res.rows.size() == 1 && res.rowIndex[0] == 1);   // headers are not rows you can click
        UI::UseLauncherLook(false);
    }

    // ================= the toolkit pieces =================
    {   // GetDragDropPayload, the drop outline left out, Escape, an outside drag
        Sim sim(300, 200); Reset();
        int got = -1; bool over = false; std::string ext;
        const UI::DragPayload* live = nullptr;
        UI::Rect srcR, dstR;
        auto ui = [&] {
            UI::BareNextWindow();
            UI::Begin("dd", 0, 0, 300, 200);
            UI::Button("source", UI::Vec2{100, 30});
            srcR = UI::GetItemRect();
            if (UI::BeginDragDropSource()) { const int v = 7; UI::SetDragDropPayload("N", &v, sizeof v); UI::EndDragDropSource(); }
            live = UI::GetDragDropPayload();
            UI::Button("target", UI::Vec2{100, 30});
            dstR = UI::GetItemRect();
            over = false;
            if (UI::BeginDragDropTarget(UI::DragDrop_NoDefaultRect)) {
                over = true;
                if (const UI::DragPayload* p = UI::AcceptDragDropPayload("N")) got = *(const int*)p->data;
                if (const UI::DragPayload* p = UI::AcceptDragDropPayload("ASSET_PATH")) ext = (const char*)p->data;
                UI::EndDragDropTarget();
            }
            UI::End();
        };
        sim.Frame(ui);
        const UI::Vec2 src{CX(srcR), CY(srcR)}, dst{CX(dstR), CY(dstR)};
        sim.Move(src.x, src.y); sim.Frame(ui); sim.Down(); sim.Frame(ui);
        CHECK(live == nullptr);
        sim.Move(src.x + 10, src.y + 10); sim.Frame(ui); sim.Frame(ui);
        CHECK(live != nullptr && std::strcmp(live->type, "N") == 0 && !live->delivery && UI::IsDragging());
        sim.Move(dst.x, dst.y); sim.Frame(ui);
        CHECK(over);
        sim.Press(Key_Escape); sim.Frame(ui);   // Escape: the drag is gone, nothing is dropped
        CHECK(!UI::IsDragging());
        sim.Up(); sim.Frame(ui);
        CHECK(got == -1);
        // again, released over the target
        sim.Move(src.x, src.y); sim.Frame(ui); sim.Down(); sim.Frame(ui);
        sim.Move(src.x + 10, src.y + 10); sim.Frame(ui); sim.Move(dst.x, dst.y); sim.Frame(ui);
        sim.Up(); sim.Frame(ui);
        CHECK(got == 7);
        // CancelDragDrop from code
        got = -1;
        sim.Move(src.x, src.y); sim.Frame(ui); sim.Down(); sim.Frame(ui);
        sim.Move(src.x + 10, src.y + 10); sim.Frame(ui); sim.Move(dst.x, dst.y); sim.Frame(ui);
        UI::CancelDragDrop();
        sim.Up(); sim.Frame(ui);
        CHECK(got == -1);
        // an outside drag: the other toolkit owns the mouse (blocked), OkayUI still sees where it is and takes it when it is let go
        const char path[] = "Assets/Tree.png";
        sim.blocked = true; sim.Move(dst.x, dst.y);
        auto withExt = [&](bool released) { return [&, released] {
            UI::SetExternalDragDrop("ASSET_PATH", path, (int)sizeof path, released);
            ui();
        }; };
        sim.Frame(withExt(false));
        CHECK(over && ext.empty() && UI::IsDragDropExternal());
        sim.Frame(withExt(true));
        CHECK(ext == "Assets/Tree.png");
        sim.Frame(ui);
        CHECK(!UI::IsDragging());
        // an outside drag the host stops reporting just ends
        ext.clear();
        sim.Frame(withExt(false)); sim.Frame(ui); sim.Frame(ui);
        CHECK(ext.empty() && !UI::IsDragging());
        sim.blocked = false;
    }
    {   // Menus in a short window: the row menu opened at the bottom moves up to fit; a sub-menu longer than the display starts at the
        // top and scrolls (the wheel), with rows as tall as the popup's own (Dear ImGui's menu rows); Escape closes the open sub-menu,
        // then the menu; what lies under a menu does not show through it.
        HRig r(hierfix::Village(), 720.0f, 320.0f);
        r.scene.parentToCount = 40;
        r.Frames(2);
        CHECK(!r.res.rows.empty());
        UI::Rect last = r.res.rows.front();   // the lowest row that is wholly in view
        for (const UI::Rect& q : r.res.rows) if (q.y + q.h <= 300.0f && q.y > last.y) last = q;
        CHECK(last.y > 200.0f);
        last.x += 120.0f; last.w = 0.0f;       // right-click on the name, as above
        r.RightClick(CX(last), CY(last));
        r.Frames(3);
        CHECK(r.st.menuKind == P::HierMenuRow);
        const UI::Rect menu = r.res.menu;
        CHECK(menu.h > 100.0f && menu.y >= 0.0f && menu.y + menu.h - 4.0f + 12.0f <= 320.5f);   // the rows (their highlight reaches 4 px past the item) and the popup's bottom padding fit
        const UI::Rect copy = r.MenuRow("h.copy"), pt = r.MenuRow("h.parentto");
        CHECK(copy.h > 10.0f && pt.w > 0.0f);
        // the sub-menu's list: the overlay to the right of the menu
        auto subList = [&]() {
            for (int i = 0; i < UI::GetOverlayCount(); ++i) {
                const UI::Rect o = UI::GetOverlayRect(i);
                if (o.x >= menu.x + menu.w - 1.0f) return o;
            }
            return UI::Rect{};
        };
        auto openSub = [&]() {
            if (r.st.menuKind != P::HierMenuRow) { r.RightClick(CX(last), CY(last)); r.Frames(2); }
            const UI::Rect row = r.MenuRow("h.parentto");
            r.At(CX(row), CY(row)); r.Frames(3);
        };
        openSub();
        UI::Rect list = subList();
        CHECK(list.w > 50.0f && list.y >= -0.5f && list.y + list.h <= 320.5f && list.h > 300.0f);   // inside the display, filling it
        {   // opaque: on a white canvas the list's background is exactly the menu colour
            const Bitmap& b = r.sim.Draw(255, 255, 255);
            const unsigned px = PixelAt(b, (int)(list.x + list.w - 4.0f), (int)(list.y + copy.h * 0.5f));
            CHECK((px >> 8) == ((35u << 16) | (39u << 8) | 48u));
        }
        // rows as tall as the popup's: the fourth row is where four popup rows would put it
        r.Click(list.x + 40.0f, list.y + copy.h * 3.5f);
        CHECK(r.seen.menuId == "h.parentto/3" && r.st.menuKind == P::HierMenuNone);
        // one notch of the wheel down scrolls three rows: the top row is now the fourth object
        openSub();
        list = subList();
        r.sim.Move(list.x + 40.0f, list.y + 40.0f); r.Frame();
        r.sim.wheel = -1.0f; r.Frame(); r.Frames(2);
        list = subList();
        r.seen = P::HierarchyIntent{};
        r.Click(list.x + 40.0f, list.y + copy.h * 0.5f);
        CHECK(r.seen.menuId == "h.parentto/3");
        // Escape: first the sub-menu, then the menu
        openSub();
        list = subList();
        r.At(list.x + 40.0f, list.y + 40.0f);
        CHECK(subList().w > 0.0f);
        r.Key(UI::KeyId_Escape);
        r.Frames(2);
        CHECK(r.st.menuKind == P::HierMenuRow && subList().w == 0.0f);
        r.Key(UI::KeyId_Escape);
        r.Frames(2);
        CHECK(r.st.menuKind == P::HierMenuNone);
        // and the edge form of Escape (a host that hands OkayUI the key edges) closes it as well
        r.RightClick(CX(last), CY(last)); r.Frames(2);
        CHECK(r.st.menuKind == P::HierMenuRow);
        r.sim.Press(okayuitest::Key_Escape); r.Frame(); r.Frames(2);
        CHECK(r.st.menuKind == P::HierMenuNone);
    }
    {   // MenuItem and BeginMenu inside a popup: rows, a tick, a shortcut, a greyed-out row, a sub-menu whose item closes the popup
        Sim sim(400, 300); Reset(); UI::UseLauncherLook(true);
        std::string chosen; UI::Rect sub, item, openR;
        auto ui = [&] {
            UI::BareNextWindow();
            UI::Begin("pm", 0, 0, 400, 300);
            UI::PushStyleVar(UI::StyleVar_FramedWidgets, 1.0f);   // Dear ImGui's menu rows and sub-menu lists inside a popup (OkayUI's own expand in place)
            UI::Button("open");
            openR = UI::GetItemRect();
            if (UI::IsItemClicked(0)) UI::OpenPopup("ctx");
            if (UI::BeginPopup("ctx")) {
                if (UI::MenuItem("Copy", "Ctrl+C")) chosen = "copy";
                item = UI::GetItemRect();
                UI::MenuItem("Sorted", nullptr, true, true);
                if (UI::MenuItem("Off", nullptr, false, false)) chosen = "off";
                if (UI::BeginMenu("More")) {
                    if (UI::MenuItem("Deep")) chosen = "deep";
                    UI::EndMenu();
                }
                sub = UI::GetItemRect();
                UI::EndPopup();
            }
            UI::PopStyleVar();
            UI::End();
        };
        sim.Frame(ui);
        sim.Click(CX(openR), CY(openR), ui); sim.Frame(ui);
        CHECK(UI::IsPopupOpen("ctx") && item.w > 50.0f && sub.w > 50.0f && sub.y > item.y);
        sim.Click(item.x + 20, item.y + item.h * 0.5f, ui);
        CHECK(chosen == "copy" && !UI::IsPopupOpen("ctx"));
        sim.Click(CX(openR), CY(openR), ui); sim.Frame(ui);
        sim.Click(item.x + 20, item.y + item.h * 2.5f, ui);   // the greyed-out row
        CHECK(chosen == "copy" && UI::IsPopupOpen("ctx"));
        sim.Move(sub.x + 20, sub.y + sub.h * 0.5f); sim.Frame(ui); sim.Frame(ui);
        sim.Click(sub.x + sub.w + 40, sub.y + sub.h * 0.5f, ui);
        CHECK(chosen == "deep" && !UI::IsPopupOpen("ctx"));
        UI::UseLauncherLook(false);
    }

    TEST_MAIN_RESULT();
}
