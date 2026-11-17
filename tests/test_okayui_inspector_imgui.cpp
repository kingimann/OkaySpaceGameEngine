// The Inspector, stage 1, Dear ImGui version (editor/EditorPanelViews.cpp: the header card, CompHeader, the Transform, the Mesh
// Renderer's shading fields and Add Component drawn from the model) against the OkayUI port (okayui/screens/OkayUIEditorPanels.cpp),
// headless and side by side (docs/own_ui_toolkit.md section 14).
//
// Both draw the same objects (okayui/tests/InspectorFixtures.hpp) with the Inspector's spacing (Comfortable density) on a 560-pixel-wide
// canvas in the editor's panel colour with the editor's look, through one rasteriser (tests/okayui_compare_rig.hpp). The Dear ImGui side
// is one window with the parts one after the other, as the editor draws them; the OkayUI side stacks the parts the way the editor's host
// places them (okayui/tests/InspectorStackOkayUI.hpp). It checks that the same input asks for the same thing in both (a drag on Position
// X, the resets, a component's arrow and enable box, the check boxes of the Mesh Renderer, Add Component) and prints the speed and
// pixel tables; it fails above 12% clearly different pixels. OKAYUI_SHOT_DIR=<dir> writes both pictures of each case.
// OKAY_UI_PROFILE=insp:<imgui|okayui> runs 200 frames of one side through ProfiledFrames for callgrind:
//   valgrind --tool=callgrind --collect-atstart=no --toggle-collect='ProfiledFrames*' ./test_okayui_inspector_imgui
#include "test_framework.hpp"
#include "EditorPanelViews.hpp"
#include "InspectorStackOkayUI.hpp"
#include "InspectorBodiesFixtures.hpp"
#include "okayui_compare_rig.hpp"

using namespace okaycompare;
namespace P = okaypanels;
using inspfix::Scene;

namespace {

struct Row { std::string panel; Timing t; Diff d; };
std::vector<Row> g_rows;

void Report(const std::string& panel, const Bitmap& imPic, const Bitmap& okPic, const Timing& t) {
    const Diff d = Compare(imPic, okPic);
    Save(panel.c_str(), "imgui", imPic);
    Save(panel.c_str(), "okayui", okPic);
    g_rows.push_back(Row{panel, t, d});
}
int Frames() { const char* s = std::getenv("OKAY_UI_COMPARE_FRAMES"); const int n = s ? std::atoi(s) : 0; return n >= 50 ? n : 150; }
bool Strict() { const char* s = std::getenv("OKAY_UI_COMPARE_STRICT"); return s && s[0] == '1'; }

// The Dear ImGui Inspector: the editor's DrawInspector with the Comfortable density, the parts drawn from the model one after the other.
void ImGuiInspector(Scene& s, const oked::PanelViewHooks& hooks) {
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10, 12));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(9, 8));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2(8, 6));
    P::InspectorHeaderModel hm = s.Header();
    const P::InspectorHeaderIntent hi = oked::DrawInspectorHeaderBodyImGui(hm, s.header, hooks);
    ImGui::Spacing();
    const float labelW = std::clamp(ImGui::GetContentRegionAvail().x * 0.46f, 128.0f, 190.0f);
    ImGui::PushItemWidth(-labelW);
    P::TransformModel tm = s.Transform();
    const P::TransformIntent ti = oked::DrawTransformBodyImGui(tm, hooks);
    std::vector<P::CompHeaderIntent> ci;
    P::MeshRendererValues mv;
    P::MeshRendererIntent mi;
    bool meshDrawn = false;
    inspfix::Obj& o = s.Shown();
    for (std::size_t i = 0; i < o.comps.size(); ++i) {
        ImGui::PushID((int)i);
        const P::CompHeaderModel cm = s.CompHeader(i);
        ci.push_back(oked::DrawComponentHeaderBodyImGui(cm, hooks));
        ImGui::PopID();
        if (i == 0 && o.hasMesh && o.comps[0].open) {
            P::MeshRendererModel mm = s.Mesh(mv);
            mi = oked::DrawMeshRendererBodyImGui(mm, hooks);
            meshDrawn = true;
        }
    }
    ImGui::PopItemWidth();
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    const bool add = oked::DrawAddComponentButtonImGui(hooks);
    ImGui::PopStyleVar(3);
    s.Apply(hi);
    s.Apply(ti, tm);
    if (meshDrawn) s.Apply(mi, mv);
    for (std::size_t i = ci.size(); i-- > 0;) s.Apply(i, ci[i]);
    if (add) ++s.addComponentOpened;
}

// Stage 2 (docs/own_ui_toolkit.md section 15): one component body as field rows. Dear ImGui: the rows drawn as the editor's component
// code draws them, with the Inspector's spacing and value column (PushItemWidth(-label column)); OkayUI: the same rows with the same
// metrics. Each frame builds the rows from the values, as the editor's builders do.
void ImGuiBody(inspbody::Body b, inspbody::Values& v, const oked::PanelViewHooks& hooks) {
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10, 12));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(9, 8));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2(8, 6));
    const float labelW = std::clamp(ImGui::GetContentRegionAvail().x * 0.46f, 128.0f, 190.0f);
    ImGui::PushItemWidth(-labelW);
    P::InspFieldList L = inspbody::Rows(b, v);
    oked::DrawFieldListBodyImGui(L, hooks);
    ImGui::PopItemWidth();
    ImGui::PopStyleVar(3);
    inspbody::Apply(b, v, L);
}
okayuiscreens::FieldListResult OkayBody(inspbody::Body b, inspbody::Values& v, float x, float y, float w, float h) {
    const inspfix::InspectorMetrics im;
    okayuiscreens::EditorMetrics em;
    em.itemSpacing = UI::Vec2{im.itemX, im.itemY}; em.framePadding = UI::Vec2{im.frameX, im.frameY}; em.innerSpacing = UI::Vec2{im.innerX, im.innerY};
    em.itemWidth = inspfix::InspectorItemWidth(w);
    okayuiscreens::SetEditorMetrics(em);
    P::InspFieldList L = inspbody::Rows(b, v);
    const okayuiscreens::FieldListResult r = okayuiscreens::DrawFieldList(L, "Body##okayui", x, y, w, h);
    okayuiscreens::SetEditorMetrics(okayuiscreens::EditorMetrics{});
    inspbody::Apply(b, v, L);
    return r;
}

} // namespace

#if defined(_MSC_VER)
__declspec(noinline)
#else
__attribute__((noinline))
#endif
void ProfiledFrames(const std::function<void()>& frame) { for (int i = 0; i < 200; ++i) frame(); }

int main() {
    kH = 1480;
    const float bx = kPadX, by = kPadY, bw = kW - 2 * kPadX, bh = kH - 2 * kPadY;
    oked::PanelViewHooks hooks;
    inspfix::StackResult okRes;
    auto imBodyOf = [&](Scene& s) { return [&s, &hooks] { ImGuiInspector(s, hooks); }; };
    auto okBodyOf = [&](Scene& s) {
        return [&s, &okRes, bx, by, bw, bh] {
            okRes = inspfix::DrawStack(s, bx, by, bw, bh);
            inspfix::ApplyStack(s, okRes);
        };
    };

    if (const char* prof = std::getenv("OKAY_UI_PROFILE"); prof && std::string(prof).rfind("body", 0) == 0) {   // body:<name>:<imgui|okayui>
        const std::string pr(prof);
        const bool im = pr.find(":imgui") != std::string::npos;
        inspbody::Body b = inspbody::BodyLight;
        for (int i = 0; i < inspbody::BodyCount; ++i) if (pr.find(std::string(":") + inspbody::kBodyNames[i] + ":") != std::string::npos) b = (inspbody::Body)i;
        ImGuiRig imr; OkayRig ok;
        inspbody::Values v;
        oked::PanelViewHooks h = hooks; h.headingFont = imr.heading;
        auto imB = [&] { ImGuiBody(b, v, h); };
        auto okB = [&] { OkayBody(b, v, bx, by, bw, bh); };
        imr.mx = imr.my = ok.sim.mx = ok.sim.my = -100.0f;
        for (int i = 0; i < 10; ++i) { if (im) imr.Frame(imB); else ok.Frame(okB); }
        if (im) ProfiledFrames([&] { imr.Frame(imB); }); else ProfiledFrames([&] { ok.Frame(okB); });
        return 0;
    }
    if (const char* prof = std::getenv("OKAY_UI_PROFILE")) {   // callgrind: one side only
        const bool im = std::string(prof).find(":imgui") != std::string::npos;
        ImGuiRig imr; OkayRig ok;
        Scene s = inspfix::Sample();
        auto imB = imBodyOf(s); auto okB = okBodyOf(s);
        imr.mx = imr.my = ok.sim.mx = ok.sim.my = -100.0f;
        for (int i = 0; i < 10; ++i) { if (im) imr.Frame(imB); else ok.Frame(okB); }
        if (im) ProfiledFrames([&] { imr.Frame(imB); }); else ProfiledFrames([&] { ok.Frame(okB); });
        return 0;
    }

    RUN_SUITE("okayui inspector against dear imgui");

    // ================= the sample object: the same input asks for the same thing; speed; picture =================
    {
        Scene ims = inspfix::Sample(), oks = inspfix::Sample();
        ImGuiRig im; OkayRig ok; Raster ras;
        auto imBody = imBodyOf(ims);
        auto okBody = okBodyOf(oks);
        auto both = [&](int n) { for (int i = 0; i < n; ++i) { im.Frame(imBody); ok.Frame(okBody); } };
        auto clickBoth = [&](float x, float y) {
            im.Click(x, y, imBody);
            ok.sim.Move(x, y); ok.Frame(okBody); ok.sim.Down(); ok.Frame(okBody); ok.sim.Up(); ok.Frame(okBody);
        };
        // a drag in whole pixels (a real pointer's positions), `steps` equal steps
        auto dragBoth = [&](float x, float y, float dx, int steps = 4) {
            x = std::floor(x); y = std::floor(y);
            im.mx = x; im.my = y; im.Frame(imBody); im.down = true; im.Frame(imBody);
            ok.sim.Move(x, y); ok.Frame(okBody); ok.sim.Down(); ok.Frame(okBody);
            for (int i = 1; i <= steps; ++i) {
                const float px = x + std::round(dx * (float)i / (float)steps);
                im.mx = px; im.Frame(imBody);
                ok.sim.Move(px, y); ok.Frame(okBody);
            }
            im.down = false; im.Frame(imBody); ok.sim.Up(); ok.Frame(okBody);
        };
        im.mx = im.my = ok.sim.mx = ok.sim.my = -100.0f;
        both(4);
        ras.DrawImGui(); const Bitmap imPic = ras.bmp;
        ras.DrawOkay();  const Bitmap okPic = ras.bmp;
        const Timing t = Time(im, ok, ras, imBody, okBody, Frames());
        Report("inspector", imPic, okPic, t);
        CHECK(okRes.comps.size() == 4 && okRes.meshDrawn);

        // a 20-pixel drag on Position X: the same value in both (so the cells line up)
        const UI::Rect px = okRes.transform.pos[0];
        dragBoth(px.x + px.w * 0.5f, px.y + px.h * 0.5f, 20.0f);
        CHECK(std::fabs(ims.Shown().pos[0] - oks.Shown().pos[0]) < 1e-4f && std::fabs(oks.Shown().pos[0] + 6.0f) < 1e-3f);
        CHECK(ims.undo.size() == 1 && oks.undo.size() == 1);   // one undo step for the drag in both
        both(2);
        // Rotation Z and Scale Y cells, in steps that start inside the 3-pixel drag threshold (the same pixels count in both)
        const UI::Rect rz = okRes.transform.rot[2];
        dragBoth(rz.x + rz.w * 0.5f, rz.y + rz.h * 0.5f, 10.0f, 5);   // 2-pixel steps: the first is inside the threshold, 8 pixels count
        CHECK(ims.Shown().rot[2] == oks.Shown().rot[2] && oks.Shown().rot[2] == 8.0f);
        both(2);
        const UI::Rect sy = okRes.transform.scale[1];
        dragBoth(sy.x + sy.w * 0.5f, sy.y + sy.h * 0.5f, -4.0f);
        CHECK(std::fabs(ims.Shown().scale[1] - oks.Shown().scale[1]) < 1e-4f && std::fabs(oks.Shown().scale[1] - 0.2f) < 1e-3f);
        both(2);
        // the resets
        const UI::Rect rp = okRes.transform.resetPos;
        clickBoth(rp.x + rp.w * 0.5f, rp.y + rp.h * 0.5f);
        CHECK(ims.Shown().pos[0] == 0.0f && oks.Shown().pos[0] == 0.0f);
        const UI::Rect rs = okRes.transform.resetScale;
        clickBoth(rs.x + rs.w * 0.5f, rs.y + rs.h * 0.5f);
        CHECK(ims.Shown().scale[1] == 1.0f && oks.Shown().scale[1] == 1.0f);
        // the Mesh Renderer's check boxes
        const UI::Rect cs = okRes.mesh.castShadows;
        clickBoth(cs.x + 11.0f, cs.y + cs.h * 0.5f);
        CHECK(!ims.Shown().mesh.castShadows && !oks.Shown().mesh.castShadows);
        const UI::Rect gs = okRes.mesh.groundShadow;
        clickBoth(gs.x + 11.0f, gs.y + gs.h * 0.5f);
        CHECK(!ims.Shown().mesh.groundShadow && !oks.Shown().mesh.groundShadow);
        both(2);
        // the Rigidbody's enable box, then its arrow
        const UI::Rect eb = okRes.comps[1].enableBox;
        clickBoth(eb.x + eb.w * 0.5f, eb.y + eb.h * 0.5f);
        CHECK(!ims.Shown().comps[1].enabled && !oks.Shown().comps[1].enabled && !oks.Shown().comps[1].open);
        const UI::Rect ar = okRes.comps[1].arrow;
        clickBoth(ar.x + ar.w * 0.5f, ar.y + ar.h * 0.5f);
        CHECK(ims.Shown().comps[1].open && oks.Shown().comps[1].open);
        // fold the Mesh Renderer by its arrow: the rows under it move up the same in both (Add Component lines up)
        const UI::Rect ma = okRes.comps[0].arrow;
        clickBoth(ma.x + ma.w * 0.5f, ma.y + ma.h * 0.5f);
        CHECK(!ims.Shown().comps[0].open && !oks.Shown().comps[0].open);
        both(2);
        const UI::Rect ab = okRes.add.button;
        clickBoth(ab.x + ab.w * 0.5f, ab.y + ab.h * 0.5f);
        CHECK(ims.addComponentOpened == 1 && oks.addComponentOpened == 1);
        // the Static box
        const UI::Rect st = okRes.header.staticBox;
        clickBoth(st.x + 11.0f, st.y + st.h * 0.5f);
        CHECK(ims.Shown().isStatic && oks.Shown().isStatic);
    }

    // ================= several objects selected; the camera (pictures and speed) =================
    for (int which = 0; which < 2; ++which) {
        Scene ims = inspfix::Sample(), oks = inspfix::Sample();
        if (which == 0) { ims.sel = oks.sel = {0, 2}; } else { ims.sel = oks.sel = {1}; }
        ImGuiRig im; OkayRig ok; Raster ras;
        auto imBody = imBodyOf(ims);
        auto okBody = okBodyOf(oks);
        im.mx = im.my = ok.sim.mx = ok.sim.my = -100.0f;
        for (int i = 0; i < 4; ++i) { im.Frame(imBody); ok.Frame(okBody); }
        ras.DrawImGui(); const Bitmap imPic = ras.bmp;
        ras.DrawOkay();  const Bitmap okPic = ras.bmp;
        const Timing t = Time(im, ok, ras, imBody, okBody, Frames());
        Report(which == 0 ? "inspector-multi" : "inspector-camera", imPic, okPic, t);
    }


    // ================= stage 2: the component bodies (pictures and speed; the same input does the same) =================
    for (int bi = 0; bi < inspbody::BodyCount; ++bi) {
        const inspbody::Body b = (inspbody::Body)bi;
        // the bodies taller than the usual canvas get one tall enough that Dear ImGui adds no scroll bar (which narrowed its value column
        // and made the UI Button 4.54% different, section 19)
        const bool tall = b == inspbody::BodyButton || b == inspbody::BodyTerrain || b == inspbody::BodyVoxelDigger || b == inspbody::BodyVoxelTerrain;
        kH = tall ? 2700 : 1480;
        const float bh = (float)kH - 2 * kPadY;
        inspbody::Values imv, okv;
        ImGuiRig im; OkayRig ok; Raster ras;
        oked::PanelViewHooks h = hooks; h.headingFont = im.heading;
        okayuiscreens::FieldListResult okr;
        auto imBody = [&] { ImGuiBody(b, imv, h); };
        auto okBody = [&] { okr = OkayBody(b, okv, bx, by, bw, bh); };
        im.mx = im.my = ok.sim.mx = ok.sim.my = -100.0f;
        for (int i = 0; i < 4; ++i) { im.Frame(imBody); ok.Frame(okBody); }
        ras.DrawImGui(); const Bitmap imPic = ras.bmp;
        ras.DrawOkay();  const Bitmap okPic = ras.bmp;
        const Timing t = Time(im, ok, ras, imBody, okBody, Frames());
        Report(std::string("body-") + inspbody::kBodyNames[bi], imPic, okPic, t);
        auto clickBoth = [&](float x, float y) {
            im.Click(x, y, imBody);
            ok.sim.Move(x, y); ok.Frame(okBody); ok.sim.Down(); ok.Frame(okBody); ok.sim.Up(); ok.Frame(okBody); ok.Frame(okBody);
        };
        auto rowOf = [&](const char* label) -> UI::Rect {
            P::InspFieldList L = inspbody::Rows(b, okv);
            for (std::size_t k = 0; k < L.rows.size() && k < okr.rows.size(); ++k)
                if (std::string(L.rows[k].label, P::InspVisibleLen(L.rows[k].label)) == label) return okr.rows[k];
            return UI::Rect{};
        };
        if (b == inspbody::BodyLight) {   // a check box and a small button: the same in both
            const UI::Rect cs = rowOf("Cast Shadows");
            clickBoth(cs.x + 11.0f, cs.y + cs.h * 0.5f);
            CHECK(!imv.castShadows && !okv.castShadows);
            const UI::Rect tb = rowOf("Torch");
            clickBoth(tb.x + tb.w * 0.5f, tb.y + tb.h * 0.5f);
            CHECK(imv.clicked.size() == 1 && okv.clicked.size() == 1 && imv.clicked[0] == "Torch" && okv.clicked[0] == "Torch");
        }
        if (b == inspbody::BodyBoxCollider) {   // the fold-out folds in both
            const UI::Rect pm = rowOf("Physics Material");
            clickBoth(pm.x + 30.0f, pm.y + pm.h * 0.5f);
            for (int i = 0; i < 2; ++i) { im.Frame(imBody); ok.Frame(okBody); }
            const UI::Rect fr = rowOf("Friction");
            CHECK(fr.w == 0.0f);
            const UI::Rect ot = rowOf("Only Tag");
            CHECK(ot.y < pm.y + 60.0f);
        }
        if (b == inspbody::BodyCurves) {   // stage 4: a click on the gradient bar adds the same stop in both; a click on the plot the same key
            const UI::Rect g = rowOf("psgrad");
            clickBoth(std::floor(g.x + g.w * 0.25f), std::floor(g.y + 9.0f));
            CHECK(imv.gCount == 4 && okv.gCount == 4 && std::fabs(imv.gStops[15] - okv.gStops[15]) < 0.01f);
            const UI::Rect c = rowOf("pssize");
            clickBoth(std::floor(c.x + c.w * 0.8f), std::floor(c.y + c.h - 10.0f));
            CHECK(imv.cCount == 4 && okv.cCount == 4 && std::fabs(imv.cKeys[7] - okv.cKeys[7]) < 0.05f);
        }
        if (b == inspbody::BodyTerrain) {   // stage 4: a Generate click in both (the radio rows lie below this canvas; okayui_inspector tries them)
            const UI::Rect gb = rowOf("Generate");
            clickBoth(gb.x + gb.w * 0.5f, gb.y + gb.h * 0.5f);
            CHECK(imv.clicked.size() == 1 && okv.clicked.size() == 1 && okv.clicked[0] == "Generate");
        }
        if (b == inspbody::BodyRigidbody) {   // the freeze boxes on one line: a click on Y ticks Y in both
            const UI::Rect fy = rowOf("Y");
            clickBoth(fy.x + 11.0f, fy.y + fy.h * 0.5f);
            CHECK(imv.fy && okv.fy && !imv.fx && !okv.fx);
        }
    }

    // ================= stage 7: a logarithmic drag goes the same way in both (the Voxel Digger's Tool Power, section 20) =================
    {
        struct Case { float lo, hi, speed, start; const char* fmt; float dx; };
        const Case cases[] = {{0.0f, 100.0f, 0.5f, 1.0f, "%.2f", 30.0f}, {0.25f, 8.0f, 0.01f, 1.0f, "%.3f", -40.0f},
                              {0.0f, 1.0e9f, 0.1f, 3.0f, "%.1f", 60.0f}, {0.0f, 1.0e9f, 0.1f, 3.0f, "%.1f", -60.0f}};
        for (const Case& c : cases) {
            kH = 1480;
            float iv = c.start, ov = c.start;
            UI::Rect rr{};
            auto rows = [&](float* v) { P::InspFieldList L; L.Drag("Power##lg", v, 1, c.speed, c.lo, c.hi, c.fmt); L.Last().logarithmic = true; return L; };
            ImGuiRig im; OkayRig ok;
            auto imB = [&] {
                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10, 12));
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(9, 8));
                ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2(8, 6));
                ImGui::PushItemWidth(-160.0f);
                P::InspFieldList L = rows(&iv);
                oked::DrawFieldListBodyImGui(L, hooks);
                ImGui::PopItemWidth();
                ImGui::PopStyleVar(3);
            };
            auto okB = [&] {
                const inspfix::InspectorMetrics m;
                okayuiscreens::EditorMetrics em;
                em.itemSpacing = UI::Vec2{m.itemX, m.itemY}; em.framePadding = UI::Vec2{m.frameX, m.frameY}; em.innerSpacing = UI::Vec2{m.innerX, m.innerY};
                em.itemWidth = inspfix::InspectorItemWidth(bw);
                okayuiscreens::SetEditorMetrics(em);
                P::InspFieldList L = rows(&ov);
                const okayuiscreens::FieldListResult r = okayuiscreens::DrawFieldList(L, "Log##okayui", bx, by, bw, bh);
                okayuiscreens::SetEditorMetrics(okayuiscreens::EditorMetrics{});
                if (!r.rows.empty()) rr = r.rows[0];
            };
            im.mx = im.my = ok.sim.mx = ok.sim.my = -100.0f;
            for (int i = 0; i < 3; ++i) { im.Frame(imB); ok.Frame(okB); }
            const float x = std::floor(rr.x + 30.0f), y = std::floor(rr.y + rr.h * 0.5f);
            im.mx = x; im.my = y; im.Frame(imB); im.down = true; im.Frame(imB);
            ok.sim.Move(x, y); ok.Frame(okB); ok.sim.Down(); ok.Frame(okB);
            for (int i = 1; i <= 10; ++i) {
                const float px = x + std::round(c.dx * (float)i / 10.0f);
                im.mx = px; im.Frame(imB);
                ok.sim.Move(px, y); ok.Frame(okB);
            }
            im.down = false; im.Frame(imB); ok.sim.Up(); ok.Frame(okB);
            std::printf("logarithmic drag %g..%g speed %g from %g by %+g px: Dear ImGui %.4f, OkayUI %.4f\n", c.lo, c.hi, c.speed, c.start, c.dx, iv, ov);
            CHECK(std::fabs(iv - ov) <= 0.02f * std::max(1.0f, std::fabs(iv)));
        }
    }

    // ================= the tables =================
    std::printf("\n| Panel | Dear ImGui build | OkayUI build | Dear ImGui build + render | OkayUI build + render | Faster? |\n|---|---:|---:|---:|---:|---|\n");
    for (const Row& r : g_rows) {
        if (r.t.imBuild <= 0.0) continue;
        std::printf("| %s | %.1f us | %.1f us | %.1f us | %.1f us | %s |\n", r.panel.c_str(), r.t.imBuild, r.t.okBuild, r.t.imAll, r.t.okAll,
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
