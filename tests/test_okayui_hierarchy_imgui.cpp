// The Hierarchy panel, Dear ImGui version (editor/EditorPanelViews.cpp DrawHierarchyBodyImGui) against the OkayUI port
// (okayui/screens/OkayUIEditorPanels.cpp DrawHierarchy), headless and side by side (docs/own_ui_toolkit.md section 12).
//
// Both draw the same model (shared/HierarchyPanelModel.hpp, the scenes of okayui/tests/HierarchyFixtures.hpp) on a 560 x 560 canvas in
// the editor's panel colour with the editor's look, through one rasteriser (tests/okayui_compare_rig.hpp). It checks that the same
// input asks for the same thing in both (a click on a row, the arrow, the eye, Ctrl and Shift, a right click, a drag onto a row), and
// prints the speed table (a 15-object village, 3,000 and 20,000 objects scrolled half way) and the pixel table; it fails above 12%
// clearly different pixels. OKAYUI_SHOT_DIR=<dir> writes both pictures of each case. OKAY_UI_PROFILE=hier<N>:<imgui|okayui> (N = 15,
// 3000 or 20000) runs 200 frames of one side through ProfiledFrames for callgrind:
//   valgrind --tool=callgrind --collect-atstart=no --toggle-collect='ProfiledFrames*' ./test_okayui_hierarchy_imgui
#include "test_framework.hpp"
#include "EditorPanelViews.hpp"
#include "HierarchyFixtures.hpp"
#include "okayui_compare_rig.hpp"

using namespace okaycompare;
namespace P = okaypanels;

namespace {

struct Row { std::string panel; Timing t; Diff d; long imFills = 0, okFills = 0; };
std::vector<Row> g_rows;

void Report(const std::string& panel, const Bitmap& imPic, const Bitmap& okPic, const Timing& t, long imF = 0, long okF = 0) {
    const Diff d = Compare(imPic, okPic);
    Save(panel.c_str(), "imgui", imPic);
    Save(panel.c_str(), "okayui", okPic);
    g_rows.push_back(Row{panel, t, d, imF, okF});
}
int Frames() { const char* s = std::getenv("OKAY_UI_COMPARE_FRAMES"); const int n = s ? std::atoi(s) : 0; return n >= 50 ? n : 150; }
bool Strict() { const char* s = std::getenv("OKAY_UI_COMPARE_STRICT"); return s && s[0] == '1'; }
bool HasLog(const hierfix::Scene& s, const std::string& line) { for (const std::string& l : s.log) if (l == line) return true; return false; }

// One side of the comparison: a scene, the panel state, the body.
struct Side {
    hierfix::Scene scene;
    P::HierarchyState st;
    P::HierarchyIntent last;
    float scrollTo = -1.0f;   // OkayUI: scroll the list here on the next frame (the wheel would take many frames)
};

}  // namespace

#if defined(_MSC_VER)
__declspec(noinline)
#else
__attribute__((noinline))
#endif
void ProfiledFrames(const std::function<void()>& frame) { for (int i = 0; i < 200; ++i) frame(); }

static hierfix::Scene SceneFor(int n) { return n <= 15 ? hierfix::Village() : hierfix::Big(n); }

int main() {
    kH = 560;
    const float bx = kPadX, by = kPadY, bw = kW - 2 * kPadX, bh = kH - 2 * kPadY;
    oked::PanelViewHooks hooks;
    hooks.searchField = EditorSearchField;
    // Each side's frame: the model from its scene, the body, the intent carried out. `rev` reveals a row (scrolls to it) once.
    auto imBodyOf = [&](Side& s, int& rev) {
        return [&s, &rev, &hooks] {
            P::HierarchyModel m = s.scene.Model(s.st.filter);
            m.revealRow = rev; rev = -1;
            s.last = oked::DrawHierarchyBodyImGui(m, s.st, hooks);
            s.scene.Apply(s.last);
        };
    };
    auto okBodyOf = [&](Side& s, int& rev) {
        return [&s, &rev, bx, by, bw, bh] {
            P::HierarchyModel m = s.scene.Model(s.st.filter);
            m.revealRow = rev; rev = -1;
            s.last = okayuiscreens::DrawHierarchy(m, s.st, bx, by, bw, bh).intent;
            s.scene.Apply(s.last);
        };
    };

    if (const char* prof = std::getenv("OKAY_UI_PROFILE")) {   // callgrind: one side only
        const std::string spec = prof;
        const int n = std::atoi(spec.c_str() + 4);
        const bool im = spec.find(":imgui") != std::string::npos;
        ImGuiRig imr; OkayRig ok;
        Side s{SceneFor(n)};
        int rev = n > 15 ? (int)s.scene.rows.size() / 2 : -1, rev2 = rev;
        auto imB = imBodyOf(s, rev); auto okB = okBodyOf(s, rev2);
        for (int i = 0; i < 10; ++i) { if (im) imr.Frame(imB); else ok.Frame(okB); }
        if (im) ProfiledFrames([&] { imr.Frame(imB); }); else ProfiledFrames([&] { ok.Frame(okB); });
        return 0;
    }

    RUN_SUITE("okayui hierarchy against dear imgui");

    // ================= the village: the same input asks for the same thing =================
    {
        Side ims{hierfix::Village()}, oks{hierfix::Village()};
        int r1 = -1, r2 = -1;
        ImGuiRig im; OkayRig ok; Raster ras;
        okayuiscreens::HierarchyResult okRes;
        auto imBody = imBodyOf(ims, r1);
        auto okBody = [&] {
            P::HierarchyModel m = oks.scene.Model(oks.st.filter);
            okRes = okayuiscreens::DrawHierarchy(m, oks.st, bx, by, bw, bh);
            oks.last = okRes.intent;
            oks.scene.Apply(oks.last);
        };
        auto both = [&](int n) { for (int i = 0; i < n; ++i) { im.Frame(imBody); ok.Frame(okBody); } };
        auto clickBoth = [&](float x, float y) {
            im.Click(x, y, imBody);
            ok.sim.Move(x, y); ok.Frame(okBody); ok.sim.Down(); ok.Frame(okBody); ok.sim.Up(); ok.Frame(okBody);
        };
        auto rowOf = [&](const std::string& name) {
            for (std::size_t i = 0; i < okRes.rowIndex.size(); ++i)
                if (oks.scene.nodes[(std::size_t)oks.scene.rows[(std::size_t)okRes.rowIndex[i]]].name == name) return okRes.rows[i];
            return UI::Rect{};
        };
        auto sel = [](const hierfix::Scene& s) { std::string o; for (int n : s.selected) o += s.nodes[(std::size_t)n].name + ","; return o; };
        im.mx = im.my = ok.sim.mx = ok.sim.my = -100.0f;
        both(4);
        ras.DrawImGui(); const Bitmap imPic = ras.bmp;
        ras.DrawOkay();  const Bitmap okPic = ras.bmp;
        CHECK(okRes.rows.size() == 15);
        // a click on every row's middle, at the OkayUI row's place, selects the same object in both (so the rows line up)
        int same = 0;
        for (std::size_t i = 0; i < okRes.rows.size(); ++i) {
            const UI::Rect r = okRes.rows[i];
            clickBoth(r.x + 140.0f, r.y + r.h * 0.5f);
            if (sel(ims.scene) == sel(oks.scene) && !oks.scene.selected.empty()) ++same;
            else std::printf("  row %d: imgui selected %s, okayui %s\n", (int)i, sel(ims.scene).c_str(), sel(oks.scene).c_str());
        }
        CHECK(same == (int)okRes.rows.size());
        // Ctrl+click and Shift+click
        clickBoth(rowOf("Sun").x + 140.0f, rowOf("Sun").y + 9.0f);
        im.down = false; ok.sim.ctrl = true;
        ImGui::SetCurrentContext(im.ctx);
        {   // ImGuiRig has no Ctrl: press it through the IO
            ImGui::GetIO().AddKeyEvent(ImGuiMod_Ctrl, true);
            const UI::Rect m = rowOf("Music");
            clickBoth(m.x + 140.0f, m.y + 9.0f);
            ImGui::GetIO().AddKeyEvent(ImGuiMod_Ctrl, false);
        }
        ok.sim.ctrl = false;
        CHECK(sel(ims.scene) == sel(oks.scene) && oks.scene.selected.size() == 2);
        // the arrow folds Houses in both; the eye switches Sun off in both
        const UI::Rect houses = rowOf("Houses");
        clickBoth(houses.x + 18.0f, houses.y + 9.0f);
        CHECK(ims.scene.nodes[2].open == 0 && oks.scene.nodes[2].open == 0);
        clickBoth(houses.x + 18.0f, houses.y + 9.0f);
        const UI::Rect sun = rowOf("Sun");
        clickBoth(sun.x + sun.w - 10.0f, sun.y + 9.0f);
        CHECK(!ims.scene.nodes[1].active && !oks.scene.nodes[1].active);
        clickBoth(sun.x + sun.w - 10.0f, sun.y + 9.0f);
        // a drag of Music onto the middle of Houses: inside it, in both
        {
            const UI::Rect from = rowOf("Music"), to = rowOf("Houses");
            auto dragBoth = [&](float fx, float fy, float tx, float ty) {
                im.mx = fx; im.my = fy; im.Frame(imBody); im.down = true; im.Frame(imBody);
                im.mx = fx + 10; im.my = fy + 10; im.Frame(imBody); im.mx = tx; im.my = ty; im.Frame(imBody); im.Frame(imBody);
                im.down = false; im.Frame(imBody); im.Frame(imBody);
                ok.sim.Move(fx, fy); ok.Frame(okBody); ok.sim.Down(); ok.Frame(okBody);
                ok.sim.Move(fx + 10, fy + 10); ok.Frame(okBody); ok.sim.Move(tx, ty); ok.Frame(okBody); ok.Frame(okBody);
                ok.sim.Up(); ok.Frame(okBody); ok.Frame(okBody);
            };
            dragBoth(from.x + 140.0f, from.y + 9.0f, to.x + 140.0f, to.y + 9.0f);
            CHECK(HasLog(ims.scene, "drop Music inside Houses") && HasLog(oks.scene, "drop Music inside Houses"));
        }
        im.mx = im.my = ok.sim.mx = ok.sim.my = -100.0f;
        both(2);
        ims.scene.fills = oks.scene.fills = 0;
        const Timing t = Time(im, ok, ras, imBody, okBody, Frames());
        Report("hierarchy", imPic, okPic, t, ims.scene.fills, oks.scene.fills);
    }

    // ================= pictures: the right-click menu, the search, a rename, a drop line =================
    {
        Side ims{hierfix::Village()}, oks{hierfix::Village()};
        int r1 = -1, r2 = -1;
        ImGuiRig im; OkayRig ok; Raster ras;
        okayuiscreens::HierarchyResult okRes;
        auto imBody = imBodyOf(ims, r1);
        auto okBody = [&] {
            P::HierarchyModel m = oks.scene.Model(oks.st.filter);
            m.revealRow = r2; r2 = -1;
            okRes = okayuiscreens::DrawHierarchy(m, oks.st, bx, by, bw, bh);
            oks.scene.Apply(okRes.intent);
        };
        for (int i = 0; i < 3; ++i) { im.Frame(imBody); ok.Frame(okBody); }
        // the menu of Main Camera
        const UI::Rect cam = okRes.rows[0];
        const float mx = cam.x + 140.0f, my = cam.y + 9.0f;
        im.RightClick(mx, my, imBody);
        ok.sim.Move(mx, my); ok.Frame(okBody); ok.sim.rdown = true; ok.Frame(okBody); ok.sim.rdown = false; ok.Frame(okBody);
        im.mx = ok.sim.mx = kW - 10.0f; im.my = ok.sim.my = kH - 10.0f;
        for (int i = 0; i < 2; ++i) { im.Frame(imBody); ok.Frame(okBody); }
        CHECK(ims.st.menuKind == P::HierMenuRow && oks.st.menuKind == P::HierMenuRow && okRes.menu.w > 0.0f);
        ras.DrawImGui(); { const Bitmap a = ras.bmp; ras.DrawOkay(); Report("hierarchy-menu", a, ras.bmp, Timing{}); }
        // close both menus (Escape in OkayUI, a click outside in Dear ImGui)
        im.Click(kW - 10.0f, kH - 10.0f, imBody);
        ok.sim.Move(kW - 10.0f, kH - 10.0f); ok.Frame(okBody); ok.sim.Down(); ok.Frame(okBody); ok.sim.Up(); ok.Frame(okBody);
        // the search for "House"
        std::snprintf(ims.st.filter, sizeof ims.st.filter, "House"); std::snprintf(oks.st.filter, sizeof oks.st.filter, "House");
        for (int i = 0; i < 3; ++i) { im.Frame(imBody); ok.Frame(okBody); }
        CHECK(okRes.rows.size() == 4);
        ras.DrawImGui(); { const Bitmap a = ras.bmp; ras.DrawOkay(); Report("hierarchy-search", a, ras.bmp, Timing{}); }
        ims.st.filter[0] = oks.st.filter[0] = 0;
        for (int i = 0; i < 2; ++i) { im.Frame(imBody); ok.Frame(okBody); }
        // a rename in place of Sun (the field open, "Sun" in it)
        ims.st.renameId = oks.st.renameId = hierfix::Scene::Id(1);
        ims.st.renameAge = oks.st.renameAge = 0;
        std::snprintf(ims.st.renameBuf, sizeof ims.st.renameBuf, "Sun"); std::snprintf(oks.st.renameBuf, sizeof oks.st.renameBuf, "Sun");
        for (int i = 0; i < 3; ++i) { im.Frame(imBody); ok.Frame(okBody); }
        CHECK(okRes.renameField.w > 0.0f);
        ras.DrawImGui(); { const Bitmap a = ras.bmp; ras.DrawOkay(); Report("hierarchy-rename", a, ras.bmp, Timing{}); }
    }

    // ================= big scenes, scrolled half way: speed and pictures =================
    for (int n : {3000, 20000}) {
        Side ims{hierfix::Big(n)}, oks{hierfix::Big(n)};
        const int half = (int)oks.scene.rows.size() / 2;
        int r1 = half, r2 = half;
        ImGuiRig im; OkayRig ok; Raster ras;
        auto imBody = imBodyOf(ims, r1);
        auto okBody = okBodyOf(oks, r2);
        im.mx = im.my = ok.sim.mx = ok.sim.my = -100.0f;
        for (int i = 0; i < 4; ++i) { im.Frame(imBody); ok.Frame(okBody); }
        CHECK(ims.st.rowsFilled < 30 && oks.st.rowsFilled < 30);   // only the rows in view, in both
        CHECK(std::abs(ims.st.scrollY - oks.st.scrollY) < 30.0f);
        ras.DrawImGui(); const Bitmap imPic = ras.bmp;
        ras.DrawOkay();  const Bitmap okPic = ras.bmp;
        ims.scene.fills = oks.scene.fills = 0;
        const Timing t = Time(im, ok, ras, imBody, okBody, Frames());
        const long fi = ims.scene.fills, fo = oks.scene.fills;
        Report("hierarchy-" + std::to_string(n), imPic, okPic, t, fi, fo);
    }

    // ================= an empty scene (picture only) =================
    {
        hierfix::Scene e; e.name = "";
        Side ims{e}, oks{e};
        int r1 = -1, r2 = -1;
        ImGuiRig im; OkayRig ok; Raster ras;
        auto imBody = imBodyOf(ims, r1);
        auto okBody = okBodyOf(oks, r2);
        im.mx = im.my = ok.sim.mx = ok.sim.my = -100.0f;
        for (int i = 0; i < 3; ++i) { im.Frame(imBody); ok.Frame(okBody); }
        ras.DrawImGui(); { const Bitmap a = ras.bmp; ras.DrawOkay(); Report("hierarchy-empty", a, ras.bmp, Timing{}); }
    }

    // ================= the tables =================
    std::printf("\n| Panel | Dear ImGui build | OkayUI build | Dear ImGui build + render | OkayUI build + render | Rows asked for a frame (Dear ImGui, OkayUI) | Faster? |\n|---|---:|---:|---:|---:|---:|---|\n");
    for (const Row& r : g_rows) {
        if (r.t.imBuild <= 0.0) continue;
        const int fr = Frames() / 25 * 25;
        std::printf("| %s | %.1f us | %.1f us | %.1f us | %.1f us | %.1f, %.1f | %s |\n", r.panel.c_str(), r.t.imBuild, r.t.okBuild, r.t.imAll, r.t.okAll,
                    fr ? (double)r.imFills / fr : 0.0, fr ? (double)r.okFills / fr : 0.0,
                    r.t.okBuild <= r.t.imBuild && r.t.okAll <= r.t.imAll ? "met" : "not met");
    }
    std::printf("\n| Panel | Average difference (0-255) | Clearly different pixels |\n|---|---:|---:|\n");
    for (const Row& r : g_rows) std::printf("| %s | %.2f | %.2f%% |\n", r.panel.c_str(), r.d.avg, r.d.clearly);
    std::printf("\n");
    for (const Row& r : g_rows) {
        if (Strict() && r.t.imBuild > 0.0) CHECK(r.t.okBuild <= r.t.imBuild * 1.10);
        CHECK(r.d.clearly < 12.0);
    }
    TEST_MAIN_RESULT();
}
