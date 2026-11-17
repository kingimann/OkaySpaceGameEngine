// The editor's Inspector, stage 1, drawn with OkayUI (okayui/screens/OkayUIEditorPanels.cpp: DrawInspectorHeader, DrawComponentHeader,
// DrawTransform, DrawMeshRenderer, DrawAddComponentButton; docs/own_ui_toolkit.md section 14). Driven headless: every control is found
// by its rectangle in the result and used with synthetic input; a small host (okayui/tests/InspectorFixtures.hpp) carries out the
// intents the way the editor does (undo, the other selected objects); golden images (launcher look) pin what is drawn. The Dear ImGui
// version is compared against this one in tests/test_okayui_inspector_imgui.cpp.
#include "test_framework.hpp"
#include "OkayUITestKit.hpp"
#include "InspectorStackOkayUI.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

using namespace okayuitest;
namespace UI = OkayUI;
namespace S = okayuiscreens;
namespace P = okaypanels;
using inspfix::Scene;

static void Check(const char* name, const Bitmap& b) {
    std::string why;
    const bool ok = MatchesGolden(name, b, &why);
    if (!ok) std::cerr << "  golden " << name << ": " << why << " (actual written as " << name << ".actual.png)\n";
    CHECK(ok);
}
template <class R> static float CX(const R& r) { return r.x + r.w * 0.5f; }
template <class R> static float CY(const R& r) { return r.y + r.h * 0.5f; }
static bool Near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

// The Inspector on a canvas the width of a docked Inspector: the scene, the last frame's result; each frame draws the stack and
// carries out its intents.
struct IRig {
    Sim sim;
    Scene scene;
    inspfix::StackResult res;
    IRig(Scene s, float w = 460.0f, float h = 1400.0f) : sim(w, h), scene(std::move(s)) {
        Reset(); UI::UseLauncherLook(true); UI::Style().caretBlink = false;
    }
    ~IRig() { UI::UseLauncherLook(false); }
    void Frame() {
        sim.Frame([&] {
            UI::PushTextScale(S::kEditorTextScale);
            res = inspfix::DrawStack(scene, 16.0f, 12.0f, sim.W - 32.0f, sim.H - 24.0f);
            UI::PopTextScale();
        });
        inspfix::ApplyStack(scene, res);
    }
    void Frames(int n) { for (int i = 0; i < n; ++i) Frame(); }
    void At(float x, float y) { sim.Move(x, y); Frame(); }
    void Click(float x, float y) { sim.Move(x, y); Frame(); sim.Down(); Frame(); sim.Up(); Frame(); Frame(); }   // and one frame more: drawn after the change
    template <class R> void Click(const R& r) { Click(CX(r), CY(r)); }
    void RightClick(float x, float y) { sim.Move(x, y); Frame(); sim.rdown = true; Frame(); sim.rdown = false; Frame(); }
    void Drag(float x, float y, float dx, int steps = 4) {
        sim.Move(x, y); Frame(); sim.Down(); Frame();
        for (int i = 1; i <= steps; ++i) { sim.Move(x + dx * (float)i / (float)steps, y); Frame(); }
        sim.Up(); Frame(); Frame();
    }
    void Type(const std::string& t) { sim.Type(t); Frame(); }
    void Enter() { sim.Press(Key_Enter); Frame(); }
    const Bitmap& Draw() { return sim.Draw(30, 33, 41); }
    inspfix::Obj& O() { return scene.Shown(); }
};

int main() {
    RUN_SUITE("okayui inspector");

    // ================= the shared rules =================
    {
        auto w1 = [](const char* t) { return (float)std::strlen(t); };   // one pixel a byte
        CHECK(P::CompHeaderTitle("Mesh Renderer", 100.0f, w1) == "Mesh Renderer");
        CHECK(P::CompHeaderTitle("Script (hook_swing.okay)", 22.0f, w1) == "Script (hook_swing.\xE2\x80\xA6");
        CHECK(P::CompHeaderTitle("Script (hook_swing.okay)", 10.0f, w1) == "Script (hook_swing.okay)");   // too narrow to bother
        CHECK(P::CompHeaderTitle("ab\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9xyz", 30.0f, [](const char* t) { return 3.0f * (float)std::strlen(t); }) ==
              "ab\xC3\xA9\xC3\xA9\xE2\x80\xA6");                                                         // whole characters only
        CHECK(std::strcmp(P::InspAxisFormat("%.3f", 7.5f, 100.0f, w1), "%.3f") == 0);
        CHECK(std::strcmp(P::InspAxisFormat("%.3f", 7.5f, 4.0f, w1), "%.2f") == 0);          // "7.500" (5) does not fit, "7.50" (4) does
        CHECK(std::strcmp(P::InspAxisFormat("%.3f", 7.5f, 3.0f, w1), "%.1f") == 0);          // a number that is not whole stops at one decimal
        CHECK(std::strcmp(P::InspAxisFormat("%.3f", 15.0f, 1.0f, w1), "%.0f") == 0);         // a whole number may lose its decimals
        CHECK(std::strcmp(P::InspAxisFormat("%.2f", 0.2f, 1.0f, w1), "%.1f") == 0);          // 0.2 never reads "0"
        CHECK(std::strcmp(P::InspAxisFormat("%g", 1.0f, 1.0f, w1), "%g") == 0);
        float cw[3];
        P::InspCellWidths(232.0f, 3, 8.0f, cw);
        CHECK(cw[0] == 72.0f && cw[1] == 72.0f && cw[2] == 72.0f);
        P::InspCellWidths(235.0f, 3, 8.0f, cw);
        CHECK(cw[0] == 73.0f && cw[1] == 73.0f && cw[2] == 73.0f);
        P::InspCellWidths(237.0f, 3, 8.0f, cw);
        CHECK(cw[0] == 73.0f && cw[2] == 75.0f);                                              // the last cell takes what is left
        float before[3] = {1, 2, 4}, after[3] = {1, 3, 4};
        P::TransformUniformScale(before, after);
        CHECK(Near(after[0], 1.5f) && Near(after[1], 3.0f) && Near(after[2], 6.0f));
        float b0[3] = {0, 2, 4}, a0[3] = {1, 2, 4};
        P::TransformUniformScale(b0, a0);                                                      // from zero: the others stay
        CHECK(a0[0] == 1.0f && a0[1] == 2.0f && a0[2] == 4.0f);
        P::MeshRendererModel mm; mm.verts = 117; mm.tris = 192; mm.uvs = true;
        CHECK(P::MeshStatsText(mm) == "117 verts  \xE2\x80\xA2  192 tris  \xE2\x80\xA2  UVs");
        mm.uvs = false; CHECK(P::MeshStatsText(mm) == "117 verts  \xE2\x80\xA2  192 tris");
        mm.verts = 0; CHECK(P::MeshStatsText(mm).empty());
        std::vector<P::PanelMenuEntry> menu;
        P::CompMenuHead(menu);
        P::CompMenuTail(menu, false);
        CHECK(menu.size() == 7 && menu[0].id == P::kCompMenuUp && menu[2].kind == P::PanelMenuSeparator && menu[6].kind == P::PanelMenuHeading);
        CHECK(P::ColorByte(0.5f) == 128 && P::ColorByte(2.0f) == 255 && P::ColorByte(-1.0f) == 0);
        CHECK(P::AddComponentWidth(428.0f) == 260.0f && P::AddComponentWidth(280.0f) == 280.0f);
        CHECK(inspfix::InspectorItemWidth(428.0f) == 428.0f - 190.0f && inspfix::InspectorItemWidth(260.0f) == 260.0f - 128.0f);
    }

    // ================= the whole stack, and its picture =================
    {
        IRig r(inspfix::Sample());
        r.Frames(3);
        CHECK(r.res.header.card.w > 400.0f && r.res.header.card.h > 60.0f);
        CHECK(r.res.transform.header.h > 30.0f && r.res.transform.pos[0].w > 50.0f);
        CHECK(r.res.comps.size() == 4 && r.res.meshDrawn);
        // the parts follow each other: the Transform under the card, each bar under the one before
        CHECK(r.res.transform.header.y > r.res.header.card.y + r.res.header.card.h);
        CHECK(r.res.comps[0].bar.y > r.res.transform.uniform.y && r.res.comps[1].bar.y > r.res.mesh.primitive.y);
        for (std::size_t i = 2; i < r.res.comps.size(); ++i) CHECK(r.res.comps[i].bar.y > r.res.comps[i - 1].bar.y + 30.0f);
        CHECK(r.res.add.button.w == 260.0f && r.res.add.button.y > r.res.comps.back().bar.y);
        // the header bars reach 8 pixels past the region on both sides (Dear ImGui's framed headers)
        CHECK(r.res.transform.header.x == 8.0f && r.res.transform.header.w == 460.0f - 32.0f + 16.0f);
        // the three cells of a row share the value column: 428 - 190 = 238 pixels, 8 apart
        const UI::Rect* p = r.res.transform.pos;
        CHECK(p[0].w == 74.0f && p[1].w == 74.0f && p[2].w == 74.0f && Near(p[1].x - (p[0].x + p[0].w), 8.0f));
        Check("inspector_full", r.Draw());
    }

    // ================= the header card =================
    {
        IRig r(inspfix::Sample());
        r.Frames(2);
        r.Click(r.res.header.activeBox);
        CHECK(!r.O().active);
        r.Click(r.res.header.activeBox);
        CHECK(r.O().active);
        // the name: click, select all, type
        r.Click(r.res.header.nameField);
        r.sim.Press(Key_SelectAll); r.Frame();
        r.Type("Crane");
        CHECK(r.O().name == "Crane");
        r.Click(r.res.header.staticBox);
        CHECK(r.O().isStatic);
        const std::size_t undo0 = r.scene.undo.size();
        r.Click(r.res.header.lockBox);
        CHECK(r.O().locked && r.scene.undo.size() == undo0 + 1);   // Lock records an undo step
        CHECK(r.scene.Undo() && !r.O().locked);
        // the tag list: Untagged, five built-in tags, the two project tags
        r.Click(r.res.header.tagCombo);
        r.Frame();
        CHECK(r.res.header.tagRows.size() == 8 && r.res.header.newTagField.w > 0.0f);
        Check("inspector_tag_list", r.Draw());
        r.Click(r.res.header.tagRows[1]);
        r.Frame();
        CHECK(r.O().tag == "Player" && r.res.header.tagRows.empty());   // picked and closed
        // a new tag, typed in the list and added with Enter: listed, applied, the list closes
        r.Click(r.res.header.tagCombo); r.Frame();
        r.Click(r.res.header.newTagField);
        r.Type("Hazard");
        r.Enter(); r.Frames(2);
        CHECK(r.O().tag == "Hazard" && r.scene.custom.size() == 3 && r.scene.custom.back() == "Hazard");
        CHECK(r.res.header.tagRows.empty() && r.scene.header.newTag[0] == '\0');
        // a refused tag keeps the list open with the reason
        r.Click(r.res.header.tagCombo); r.Frame();
        r.Click(r.res.header.newTagField);
        r.Type(std::string(40, 'x'));
        r.Click(r.res.header.addTagButton); r.Frame();
        CHECK(r.scene.header.tagError == "too long" && !r.res.header.tagRows.empty());
        // right-click a project tag: rename it in the menu that opens on it (the list stays open while it is used)
        const UI::Rect pickup = r.res.header.tagRows[6];
        r.RightClick(CX(pickup), CY(pickup));
        r.Frame();
        CHECK(r.res.header.renameField.w > 0.0f && std::string(r.scene.header.renameBuf) == "Pickup");
        r.Click(r.res.header.renameField);
        CHECK(!r.res.header.tagRows.empty());                                    // a click in the menu does not close the list
        r.sim.Press(Key_SelectAll); r.Frame();
        r.Type("Coin");
        r.Click(r.res.header.renameButton); r.Frames(2);
        CHECK(r.scene.custom[0] == "Coin" && r.res.header.renameField.w == 0.0f);
        // the same name as another tag is refused, with the reason under the field
        r.RightClick(CX(r.res.header.tagRows[6]), CY(r.res.header.tagRows[6])); r.Frame();
        r.Click(r.res.header.renameField);
        r.sim.Press(Key_SelectAll); r.Frame();
        r.Type("Door");
        r.Enter(); r.Frame();
        CHECK(r.scene.header.renameError == "that tag is already in the list" && r.scene.custom[0] == "Coin");
        // Remove tag from list
        r.Click(r.res.header.removeTagRow); r.Frame();
        CHECK(std::find(r.scene.custom.begin(), r.scene.custom.end(), "Coin") == r.scene.custom.end());
        r.sim.Press(Key_Escape); r.Frame(); r.sim.Press(Key_Escape); r.Frame();
        r.Frames(2);
        CHECK(r.res.header.tagRows.empty());
        // the layer list: the note and the 32 layers; pick layer 4; right-click layer 5 to rename it
        r.Click(r.res.header.layerCombo); r.Frame();
        CHECK(r.res.header.layerRows.size() == 32);
        // eight rows at a time, as Dear ImGui's combo shows them; the wheel scrolls the list by rows
        CHECK(r.res.header.layerRows[7].w > 0.0f && r.res.header.layerRows[8].w == 0.0f);
        r.sim.Move(CX(r.res.header.layerRows[3]), CY(r.res.header.layerRows[3])); r.sim.wheel = -1.0f; r.Frame(); r.Frame();
        CHECK(r.res.header.layerRows[0].w == 0.0f && r.res.header.layerRows[10].w > 0.0f);
        r.sim.wheel = 1.0f; r.Frame(); r.Frame();
        CHECK(r.res.header.layerRows[0].w > 0.0f);
        r.Click(r.res.header.layerRows[4]); r.Frame();
        CHECK(r.O().layer == 4);
        r.Click(r.res.header.layerCombo); r.Frame();
        r.RightClick(CX(r.res.header.layerRows[5]), CY(r.res.header.layerRows[5])); r.Frame();
        CHECK(std::string(r.scene.header.renameBuf) == "UI");
        r.Click(r.res.header.renameField);
        r.sim.Press(Key_SelectAll); r.Frame();
        r.Type("Hud");
        r.Enter(); r.Frames(2);
        CHECK(r.scene.layers[5] == "Hud");
        r.sim.Press(Key_Escape); r.Frame(); r.Frames(2);
        // Save as Prefab
        r.Click(r.res.header.savePrefab);
        CHECK(!r.scene.log.empty() && r.scene.log.back() == "Saved prefab Crane.okayprefab");
    }

    // ================= a narrow Inspector: the name takes the row, Static and Lock drop below; Save as Prefab wraps =================
    {
        IRig r(inspfix::Sample(), 300.0f, 1500.0f);
        r.Frames(3);
        CHECK(r.res.header.staticBox.y > r.res.header.nameField.y + 10.0f);
        CHECK(r.res.header.savePrefab.y > r.res.header.layerCombo.y + 10.0f || r.res.header.layerCombo.y > r.res.header.tagCombo.y + 10.0f);
        CHECK(r.res.add.button.w == 300.0f - 32.0f);                          // under 300: the whole width
    }

    // ================= component header bars =================
    {
        IRig r(inspfix::Sample());
        r.Frames(2);
        const S::CompHeaderResult mr = r.res.comps[0];
        // the arrow folds; the enable box toggles without folding
        r.Click(mr.arrow);
        CHECK(!r.O().comps[0].open && !r.res.meshDrawn);
        r.Click(r.res.comps[0].bar.x + 200.0f, CY(r.res.comps[0].bar));       // the name part of the bar folds it out again
        CHECK(r.O().comps[0].open && r.res.meshDrawn);
        r.Click(r.res.comps[0].enableBox);
        CHECK(!r.O().comps[0].enabled && r.O().comps[0].open);
        r.Click(r.res.comps[0].enableBox);
        CHECK(r.O().comps[0].enabled);
        // the "..." menu: its rows, a greyed-out row takes no click, Move Down swaps with the next component
        r.Click(r.res.comps[0].menuButton); r.Frame();
        CHECK(r.res.comps[0].menuIds.size() == 8 && r.res.comps[0].menu.w > 100.0f);
        Check("inspector_comp_menu", r.Draw());
        auto rowOf = [&](std::size_t c, const char* id) {
            for (std::size_t i = 0; i < r.res.comps[c].menuIds.size(); ++i) if (r.res.comps[c].menuIds[i] == id) return r.res.comps[c].menuRows[i];
            return UI::Rect{};
        };
        r.Click(rowOf(0, P::kCompMenuPaste)); r.Frame();
        CHECK(r.scene.menuChosen.empty());
        r.Click(rowOf(0, P::kCompMenuDown)); r.Frame();
        CHECK(r.scene.menuChosen == P::kCompMenuDown && r.O().comps[0].label == "Rigidbody 3D" && r.O().comps[1].label == "Mesh Renderer");
        CHECK(r.scene.Undo() && r.O().comps[0].label == "Mesh Renderer");
        r.Frames(2);
        // a right-click on the bar opens the same menu; Remove Component takes it away (with an undo step)
        r.RightClick(CX(r.res.comps[1].bar), CY(r.res.comps[1].bar)); r.Frame();
        CHECK(r.res.comps[1].menuIds.size() == 8);
        r.Click(rowOf(1, P::kCompMenuRemove)); r.Frames(2);
        CHECK(r.O().comps.size() == 3 && r.O().comps[1].label == "Box Collider 3D");
        CHECK(r.scene.Undo() && r.O().comps.size() == 4);
        r.Frames(2);
        // Collapse All folds every component
        r.Click(r.res.comps[0].menuButton); r.Frame();
        r.Click(rowOf(0, P::kCompMenuCollapse)); r.Frames(2);
        CHECK(!r.O().comps[0].open && !r.res.meshDrawn);
        // a required component's menu has no Remove: the note instead
        Scene cam = inspfix::Sample(); cam.sel = {1};
        IRig rc(cam);
        rc.Frames(2);
        rc.Click(rc.res.comps[0].menuButton); rc.Frame();
        bool hasRemove = false;
        for (const std::string& id : rc.res.comps[0].menuIds) if (id == P::kCompMenuRemove) hasRemove = true;
        CHECK(!hasRemove && rc.res.comps[0].menuIds.size() == 7);
    }

    // ================= Transform: drag, type, reset, undo, uniform, the camera, the menu =================
    {
        IRig r(inspfix::Sample());
        r.Frames(2);
        // a 20-pixel drag on Position X moves it 20 * 0.05 = 1.0, with one undo step for the whole drag
        const std::size_t u0 = r.scene.undo.size();
        r.Drag(CX(r.res.transform.pos[0]), CY(r.res.transform.pos[0]), 20.0f);
        CHECK(Near(r.O().pos[0], -6.0f, 1e-3f) && r.scene.undo.size() == u0 + 1);
        CHECK(r.scene.Undo() && Near(r.O().pos[0], -7.0f));
        r.Frames(2);
        // Rotation Y: 1 degree a pixel
        r.Drag(CX(r.res.transform.rot[1]), CY(r.res.transform.rot[1]), -15.0f, 3);
        CHECK(Near(r.O().rot[1], -15.0f, 1e-3f));
        // a double click types a number; Enter keeps it
        r.Click(r.res.transform.pos[2]);
        r.sim.Down(); r.Frame(); r.sim.Up(); r.Frame();
        r.sim.Press(Key_SelectAll); r.Frame();
        r.Type("2.5");
        r.Enter(); r.Frames(2);
        CHECK(Near(r.O().pos[2], 2.5f));
        // Ctrl+click types too
        r.sim.ctrl = true; r.Click(r.res.transform.pos[1]); r.sim.ctrl = false;
        r.sim.Press(Key_SelectAll); r.Frame();
        r.Type("-3");
        r.Enter(); r.Frames(2);
        CHECK(Near(r.O().pos[1], -3.0f));
        // Reset Pos: zeroes, one undo step; undo brings it back
        const std::size_t u1 = r.scene.undo.size();
        r.Click(r.res.transform.resetPos);
        CHECK(r.O().pos[0] == 0.0f && r.O().pos[1] == 0.0f && r.O().pos[2] == 0.0f && r.scene.undo.size() == u1 + 1);
        CHECK(r.scene.Undo() && Near(r.O().pos[1], -3.0f));
        r.Frames(2);
        r.Click(r.res.transform.resetRot);
        CHECK(r.O().rot[1] == 0.0f);
        r.Click(r.res.transform.resetScale);
        CHECK(r.O().scale[0] == 1.0f);
        // Uniform: a drag on Scale X takes Y and Z by the same ratio
        r.Click(r.res.transform.uniform);
        CHECK(r.scene.uniform);
        r.Drag(CX(r.res.transform.scale[0]), CY(r.res.transform.scale[0]), 20.0f);
        CHECK(Near(r.O().scale[0], 2.0f, 1e-3f) && Near(r.O().scale[1], 2.0f, 1e-3f) && Near(r.O().scale[2], 2.0f, 1e-3f));
        // the header folds, and its right-click menu copies and pastes the whole transform
        r.RightClick(CX(r.res.transform.header), CY(r.res.transform.header)); r.Frame();
        CHECK(r.res.transform.menuCopy.w > 0.0f);
        r.Click(r.res.transform.menuCopy); r.Frame();
        CHECK(r.scene.copyValid && Near(r.scene.copyScale[0], 2.0f, 1e-3f));
        r.Click(r.res.transform.resetScale);
        r.RightClick(CX(r.res.transform.header), CY(r.res.transform.header)); r.Frame();
        r.Click(r.res.transform.menuPaste); r.Frame();
        CHECK(Near(r.O().scale[0], 2.0f, 1e-3f));
        r.Click(r.res.transform.header);
        CHECK(!r.scene.transformOpen && r.res.transform.pos[0].w == 0.0f);
        r.Click(r.res.transform.header);
        CHECK(r.scene.transformOpen);
    }
    {
        // the camera: its scale is shown but locked, no Reset Scale, no Uniform, and the note says why
        Scene s = inspfix::Sample(); s.sel = {1};
        IRig r(s);
        r.Frames(2);
        CHECK(r.res.transform.resetScale.w == 0.0f && r.res.transform.uniform.w == 0.0f);
        r.Drag(CX(r.res.transform.scale[0]), CY(r.res.transform.scale[0]), 20.0f);
        CHECK(r.O().scale[0] == 1.0f);
        r.Drag(CX(r.res.transform.pos[0]), CY(r.res.transform.pos[0]), 20.0f);
        CHECK(Near(r.O().pos[0], 1.0f, 1e-3f));
    }
    {
        // several objects selected: "(editing 2 selected objects)", a drag moves both to the same value, the Mesh Renderer's bar says
        // which values differ, and a change there goes to both
        Scene s = inspfix::Sample(); s.sel = {0, 2};
        IRig r(s);
        r.Frames(3);
        Check("inspector_multi", r.Draw());
        r.Drag(CX(r.res.transform.pos[1]), CY(r.res.transform.pos[1]), 10.0f);
        // (2.5-pixel steps: the first is inside Dear ImGui's 3-pixel drag threshold, so 7.5 pixels count: 7.5 + 7.5 * 0.05)
        CHECK(Near(r.scene.objs[0].pos[1], 7.875f, 1e-3f) && Near(r.scene.objs[2].pos[1], 7.875f, 1e-3f));
        CHECK(r.scene.objs[0].pos[0] == -7.0f && r.scene.objs[2].pos[0] == -7.0f);   // the whole vector goes to both (the editor's rule)
        CHECK(r.scene.CompHeader(0).mixedNote == "Different on the selected objects: Color, Cast Shadows");
        r.Click(r.res.mesh.castShadows);
        r.Click(r.res.mesh.castShadows);
        CHECK(r.scene.objs[0].mesh.castShadows && r.scene.objs[2].mesh.castShadows);
        CHECK(r.scene.CompHeader(0).mixedNote == "Different on the selected objects: Color");
    }

    // ================= Mesh Renderer =================
    {
        IRig r(inspfix::Sample());
        r.Frames(2);
        r.Click(r.res.mesh.wireframe);
        CHECK(r.O().mesh.wireframe);
        r.Click(r.res.mesh.receiveShadows);
        CHECK(!r.O().mesh.receiveShadows && r.O().mesh.castShadows);
        // the shader list: Toon shows the Cel Bands slider (the rows below move down)
        const float primY = r.res.mesh.primitive.y;
        r.Click(r.res.mesh.shader); r.Frame();
        CHECK(r.res.mesh.listRows.size() == 9);
        r.Click(r.res.mesh.listRows[2]);   // Toon
        r.Frames(2);
        CHECK(r.O().mesh.shader == P::MeshShaderToon && r.res.mesh.primitive.y > primY + 30.0f);
        // Rim: a click at the slider's right end is 2.0, and Rim Power and Rim Color appear
        const UI::Rect rim = r.res.mesh.rim;
        r.Click(rim.x + inspfix::InspectorItemWidth(428.0f) - 3.0f, CY(rim));
        r.Frames(2);
        CHECK(Near(r.O().mesh.rimStrength, 2.0f, 0.02f) && r.res.mesh.primitive.y > primY + 2.0f * 46.0f);
        // the colour's red cell: a 12-pixel drag in 3-pixel steps adds 12 to the 0..255 value (the first step reaches the threshold)
        const UI::Rect col = r.res.mesh.color;
        r.Drag(col.x + 20.0f, CY(col), 12.0f);
        CHECK(P::ColorByte(r.O().mesh.color[0]) == 172);
        // a press and release without moving changes nothing (the drag threshold)
        r.Click(col.x + 20.0f, CY(col));
        CHECK(P::ColorByte(r.O().mesh.color[0]) == 172);
        // Ground Shadow off hides its two rows
        const float specY = r.res.mesh.specular.y;
        r.Click(r.res.mesh.groundShadow); r.Frames(2);
        CHECK(!r.O().mesh.groundShadow && r.res.mesh.specular.y < specY - 80.0f && r.res.mesh.groundShadowY.w == 0.0f);
        // the Primitive list
        r.Click(r.res.mesh.primitive); r.Frame();
        CHECK(r.res.mesh.listRows.size() == 23 && r.res.mesh.listRows[8].w == 0.0f);   // eight rows in view
        r.Click(r.res.mesh.listRows[5]);   // Sphere
        r.Frames(2);
        CHECK(r.O().mesh.primitive == 5);
        // Add Component
        r.Click(r.res.add.button);
        CHECK(r.scene.addComponentOpened == 1);
    }

    // ================= toolkit pieces this needed =================
    {
        // a popup opened from a row of an open popup keeps that popup open while it is used (OkayUI's EndPopup)
        Sim sim(400, 400);
        Reset(); UI::UseLauncherLook(true);
        bool parentOpen = false, childOpen = false; UI::Rect row{}, field{};
        static char buf[32] = "abc";
        auto ui = [&] {
            UI::BareNextWindow();
            if (UI::Begin("pp", 0, 0, 400, 400)) {
                if (UI::Button("open")) UI::OpenPopup("##parent");
                parentOpen = childOpen = false;
                if (UI::BeginPopup("##parent")) {
                    parentOpen = true;
                    UI::Selectable("row", false);
                    row = UI::GetItemRect();
                    if (UI::BeginPopupContextItem("##child")) {
                        childOpen = true;
                        UI::InputText("##f", buf, 32);
                        field = UI::GetItemRect();
                        UI::EndPopup();
                    }
                    UI::EndPopup();
                }
            }
            UI::End();
        };
        sim.Frame(ui);
        sim.Click(30, 15, ui); sim.Frame(ui);
        CHECK(parentOpen);
        sim.Move(CX(row), CY(row)); sim.Frame(ui); sim.rdown = true; sim.Frame(ui); sim.rdown = false; sim.Frame(ui);
        CHECK(childOpen && field.w > 0.0f);
        sim.Click(CX(field), CY(field), ui); sim.Frame(ui);
        CHECK(parentOpen && childOpen);                       // the click in the child menu kept both open
        sim.Click(395, 395, ui); sim.Frame(ui);
        CHECK(!parentOpen && !childOpen);                     // a click outside both closes them
        UI::UseLauncherLook(false);
    }
    {
        // a text field that had the keyboard gives it up when it is not drawn (its popup closed): keys stop going to it
        Sim sim(400, 400);
        Reset(); UI::UseLauncherLook(true);
        static char buf[32] = "abc";
        bool show = true;
        auto ui = [&] {
            UI::BareNextWindow();
            if (UI::Begin("ff", 0, 0, 400, 400) && show) UI::InputText("##f", buf, 32);
            UI::End();
        };
        sim.Frame(ui);
        sim.Click(60, 15, ui);
        CHECK(UI::WantsTextInput());
        show = false; sim.Frame(ui);
        CHECK(!UI::WantsTextInput());
        UI::UseLauncherLook(false);
    }
    {
        // a combo's list as wide as a pushed PopupWidth when that is wider than the box
        Sim sim(400, 400);
        Reset(); UI::UseLauncherLook(true);
        float listW = 0.0f;
        auto ui = [&] {
            UI::BareNextWindow();
            if (UI::Begin("cw", 0, 0, 400, 400)) {
                UI::SetNextItemWidth(120.0f);
                UI::PushStyleVar(UI::StyleVar_PopupWidth, 250.0f);
                if (UI::BeginCombo("##c", "x")) { UI::Selectable("one", false); listW = UI::GetContentRegionAvail().x; UI::EndCombo(); }
                UI::PopStyleVar();
            }
            UI::End();
        };
        sim.Frame(ui);
        sim.Click(30, 15, ui); sim.Frame(ui);
        CHECK(listW > 200.0f);
        UI::UseLauncherLook(false);
    }

    TEST_MAIN_RESULT();
}
