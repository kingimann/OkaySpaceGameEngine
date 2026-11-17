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
