// OkayUI::BeginCapture / EndCapture (okay/UI/OkayUICapture.hpp): a window's shapes cut out of the frame for a host that draws them
// inside another toolkit's draw order (the Script Editor draws CodeEdit inside a Dear ImGui window, so ImGui popups cover it).
// Headless: the frame's draw data with and without the captured window, the captured shapes drawn on their own, input through a
// captured window, window reordering, nesting, the overlay layer, and the texture pixels a host uploads.
#include "test_framework.hpp"
#include "OkayUITestKit.hpp"
#include "okay/UI/OkayUICapture.hpp"
#include "okay/UI/OkayUICodeEdit.hpp"
#include "okay/UI/OkayUIRender.hpp"
#include "okay/UI/OkayUIRenderBackends.hpp"

#include <memory>
#include <string>

using namespace okayuitest;
namespace UI = OkayUI;
namespace R = OkayUI::render;

namespace {

// Every index of the frame points at a vertex of the frame, every command lies inside the index buffer.
bool DrawDataValid(const UI::DrawData& d) {
    const int* idx = d.indices;
    for (int i = 0; i < d.indexCount; ++i) if (idx[i] < 0 || idx[i] >= d.vertexCount) return false;
    if (d.indexCount % 3) return false;
    for (int c = 0; c < d.cmdCount; ++c) if (d.cmds[c].idxOffset < 0 || d.cmds[c].idxOffset + d.cmds[c].idxCount > d.indexCount) return false;
    return true;
}
bool CapturedValid(const UI::CapturedGeometry& g) {
    for (int i : g.indices) if (i < 0 || i >= (int)g.vertices.size()) return false;
    if (g.indices.size() % 3) return false;
    int next = 0;
    for (const UI::DrawCmd& c : g.cmds) { if (c.idxOffset != next || c.idxCount <= 0) return false; next += c.idxCount; }
    return next == (int)g.indices.size();
}
bool HasColor(const UI::CapturedGeometry& g, UI::Color32 c) {
    for (const UI::DrawVertex& v : g.vertices)
        if (v.r == (c & 0xFF) && v.g == ((c >> 8) & 0xFF) && v.b == ((c >> 16) & 0xFF) && v.a == ((c >> 24) & 0xFF)) return true;
    return false;
}
bool HasColor(const UI::DrawData& d, UI::Color32 c) {
    const UI::DrawVertex* v = static_cast<const UI::DrawVertex*>(d.vertices);
    for (int i = 0; i < d.vertexCount; ++i)
        if (v[i].r == (c & 0xFF) && v[i].g == ((c >> 8) & 0xFF) && v[i].b == ((c >> 16) & 0xFF) && v[i].a == ((c >> 24) & 0xFF)) return true;
    return false;
}

struct Rig {
    Sim sim{640, 400};
    UI::CodeEdit ce;
    UI::CapturedGeometry cap;
    bool capture = true, drawCode = true, before = true, after = true;
    UI::Color32 bg = 0;
    Rig() {
        Reset();
        UI::Style().caretBlink = false;
        UI::CodePalette p = UI::DefaultCodePalette();
        p.background = bg = UI::RGBA(1, 2, 3, 255);   // a colour nothing else uses
        ce.SetPalette(p);
        ce.SetText("int main() {\n    return 42;\n}\n");
    }
    void Frame() {
        sim.Frame([&] {
            if (before) { if (UI::Begin("Before", 10, 10, 200, 120)) UI::Button("Press"); UI::End(); }
            if (drawCode) {
                if (capture) UI::BeginCapture();
                UI::BareNextWindow();
                if (UI::Begin("Code", 220, 10, 400, 300)) ce.Draw("ed", 400, 300);
                UI::End();
                if (capture) UI::EndCapture(cap);
            }
            if (after) { if (UI::Begin("After", 10, 200, 200, 120)) UI::Button("Other"); UI::End(); }
        });
    }
};

void CutsTheWindowOut() {
    Rig r;
    r.capture = false; r.Frame(); r.Frame();
    const UI::DrawData all = UI::GetDrawData();
    const int allIdx = all.indexCount;
    CHECK(HasColor(all, r.bg));                       // not captured: the code is in the frame
    r.capture = true; r.Frame();
    const UI::DrawData rest = UI::GetDrawData();
    CHECK(DrawDataValid(rest));
    CHECK(!HasColor(rest, r.bg));                     // captured: the code is not in the frame any more
    CHECK(!r.cap.Empty());
    CHECK(CapturedValid(r.cap));
    CHECK(HasColor(r.cap, r.bg));                     // ...it is in the captured shapes
    CHECK((int)r.cap.indices.size() + rest.indexCount == allIdx);   // nothing lost, nothing doubled
    // the other two windows still draw
    r.drawCode = false; r.Frame();
    const int withoutCode = UI::GetDrawData().indexCount;
    r.drawCode = true; r.Frame();
    CHECK(UI::GetDrawData().indexCount == withoutCode);
    CHECK(!UI::Capturing());
}

void CapturedDrawsAlone() {
    Rig r;
    r.before = r.after = false;
    r.Frame(); r.Frame();
    // the captured shapes as draw data, through the software renderer: the code's background fills its rectangle
    Bitmap bmp(640, 400);
    bmp.Clear(0, 0, 0);
    std::unique_ptr<R::Backend> be = R::CreateSoftBackend(bmp);
    R::Renderer ren(*be);
    R::FrameInfo fi; fi.fbWidth = 640; fi.fbHeight = 400; fi.dpi = 1.0f;
    UI::DrawData d;
    d.vertices = r.cap.vertices.data(); d.vertexCount = (int)r.cap.vertices.size();
    d.indices = r.cap.indices.data(); d.indexCount = (int)r.cap.indices.size(); d.vertexStride = (int)sizeof(UI::DrawVertex);
    d.cmds = r.cap.cmds.data(); d.cmdCount = (int)r.cap.cmds.size();
    ren.Render(d, fi);
    const unsigned char* px = &bmp.rgba[((size_t)290 * 640 + 600) * 4];       // inside the code, right of the text, above the bottom
    CHECK(px[0] == 1 && px[1] == 2 && px[2] == 3);
    const unsigned char* out = &bmp.rgba[((size_t)100 * 640 + 100) * 4];      // outside the code window: untouched
    CHECK(out[0] == 0 && out[1] == 0 && out[2] == 0);
}

void InputStillWorks() {
    Rig r;
    r.Frame(); r.Frame();
    // a click inside the captured window focuses the editor and puts the caret there, typing edits
    const UI::Vec2 p = r.ce.PosToScreen(UI::CodePos{1, 4});
    r.sim.Move(p.x + 1.0f, p.y + r.ce.LineHeight() * 0.5f); r.Frame();
    r.sim.Down(); r.Frame(); r.sim.Up(); r.Frame();
    CHECK(r.ce.Focused());
    CHECK(r.ce.Caret() == (UI::CodePos{1, 4}));
    r.sim.Type("x"); r.Frame();
    CHECK(r.ce.Line(1) == "    xreturn 42;");
    // the wheel over the captured window scrolls it
    std::string many; for (int i = 0; i < 200; ++i) many += "line " + std::to_string(i) + "\n";
    r.ce.SetText(many);
    r.Frame();
    r.sim.wheel = -2.0f; r.Frame();
    CHECK(r.ce.ScrollY() > 0.0f);
    // a click on a window outside drops the editor's focus
    r.sim.Move(100, 60); r.Frame(); r.sim.Down(); r.Frame(); r.sim.Up(); r.Frame();
    CHECK(!r.ce.Focused());
}

void ReorderedWindows() {
    // Windows on top of each other change their stacking order when clicked; the frame is reordered at its end. With a captured
    // window between them the reorder must still give valid draw data.
    Rig r;
    r.Frame(); r.Frame();
    r.sim.Move(30, 30); r.Frame(); r.sim.Down(); r.Frame(); r.sim.Up(); r.Frame();      // "Before" to the front
    CHECK(DrawDataValid(UI::GetDrawData()));
    r.sim.Move(30, 230); r.Frame(); r.sim.Down(); r.Frame(); r.sim.Up(); r.Frame();     // "After" to the front
    CHECK(DrawDataValid(UI::GetDrawData()));
    CHECK(!HasColor(UI::GetDrawData(), r.bg));
    r.sim.Move(400, 100); r.Frame(); r.sim.Down(); r.Frame(); r.sim.Up(); r.Frame();    // the code to the front
    CHECK(DrawDataValid(UI::GetDrawData()));
    CHECK(!HasColor(UI::GetDrawData(), r.bg));
    CHECK(CapturedValid(r.cap) && HasColor(r.cap, r.bg));
}

void NestingAndStrayEnds() {
    Rig r;
    UI::CapturedGeometry stray;
    stray.indices.push_back(7);
    r.sim.Frame([&] { UI::EndCapture(stray); });    // no BeginCapture: emptied, nothing else
    CHECK(stray.Empty() && stray.vertices.empty() && stray.cmds.empty());
    UI::CapturedGeometry outer, inner;
    r.sim.Frame([&] {
        UI::BeginCapture();
        CHECK(UI::Capturing());
        UI::BeginCapture();                          // nested: counted
        UI::BareNextWindow();
        if (UI::Begin("Code", 220, 10, 400, 300)) r.ce.Draw("ed", 400, 300);
        UI::End();
        UI::EndCapture(inner);                       // inner: nothing
        CHECK(UI::Capturing());
        UI::EndCapture(outer);                       // outer: everything
        CHECK(!UI::Capturing());
    });
    CHECK(inner.Empty());
    CHECK(!outer.Empty() && HasColor(outer, r.bg) && CapturedValid(outer));
    CHECK(!HasColor(UI::GetDrawData(), r.bg));
    // a capture left open at the end of a frame does not leak into the next frame
    r.sim.Frame([&] { UI::BeginCapture(); });
    CHECK(!UI::Capturing());
    r.capture = false; r.Frame();
    CHECK(HasColor(UI::GetDrawData(), r.bg));
}

void OverlayIsCaptured() {
    // What a captured window draws on the foreground list (the overlay layer) is captured too, after its main shapes.
    Rig r;
    const UI::Color32 fg = UI::RGBA(9, 8, 7, 255);
    UI::CapturedGeometry g;
    r.sim.Frame([&] {
        UI::BeginCapture();
        if (UI::Begin("Code", 220, 10, 400, 300)) {
            UI::GetWindowDrawList().AddRectFilled(UI::Vec2{230, 20}, UI::Vec2{260, 40}, r.bg);
            UI::GetForegroundDrawList().AddRectFilled(UI::Vec2{240, 30}, UI::Vec2{270, 50}, fg);
        }
        UI::End();
        UI::EndCapture(g);
    });
    CHECK(HasColor(g, r.bg) && HasColor(g, fg) && CapturedValid(g));
    CHECK(!HasColor(UI::GetDrawData(), fg));
    // the foreground rectangle comes after the window's own shapes
    int lastBg = -1, firstFg = -1;
    for (size_t i = 0; i < g.indices.size(); ++i) {
        const UI::DrawVertex& v = g.vertices[(size_t)g.indices[i]];
        if (v.r == 1 && v.g == 2 && v.b == 3) lastBg = (int)i;
        if (v.r == 9 && v.g == 8 && v.b == 7 && firstFg < 0) firstFg = (int)i;
    }
    CHECK(lastBg >= 0 && firstFg > lastBg);
}

void TexturePixels() {
    UI::TexturePixels tp;
    CHECK(!UI::GetTexturePixels(nullptr, tp));
    int dummy = 0;
    CHECK(!UI::GetTexturePixels(&dummy, tp));       // not a texture OkayUI knows
    // the launcher look: flat shapes and text sample its atlas; the host gets the atlas's pixels
    Rig r;
    UI::UseLauncherLook(true);
    r.Frame(); r.Frame();
    bool sawAtlas = false;
    for (const UI::DrawCmd& c : r.cap.cmds) {
        if (!c.texture) continue;
        UI::TexturePixels p;
        CHECK(UI::GetTexturePixels(c.texture, p));
        CHECK(p.rgba && p.w > 0 && p.h > 0);
        sawAtlas = true;
    }
    CHECK(sawAtlas);
    UI::UseLauncherLook(false);
}

} // namespace

int main() {
    RUN_SUITE("okayui capture");
    CutsTheWindowOut();
    CapturedDrawsAlone();
    InputStillWorks();
    ReorderedWindows();
    NestingAndStrayEnds();
    OverlayIsCaptured();
    TexturePixels();
    TEST_MAIN_RESULT();
}
