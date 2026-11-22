// The Script Editor's OkayUI code (OkayUI::CodeEdit behind editor/ScriptEditorCodeEdit) inside a Dear ImGui window, the way
// editor/ScriptEditorWidget.hpp draws it: its shapes are cut out of the OkayUI frame (OkayUI::BeginCapture / EndCapture) and put
// into the ImGui window's draw list (shared/OkayUIImGuiEmbed.hpp), so Dear ImGui popups, tooltips and modals opened over the code
// are drawn over it. Before this, OkayUI's frame was drawn after ImGui and covered them (seen in the editor: the autocomplete list,
// its help tooltip and the "Unsaved Changes" dialog under the code).
//
// Headless: Dear ImGui with no window, OkayUI with synthetic input, both drawn by one software rasteriser in the order the editor
// draws them (Dear ImGui's draw data, then OkayUI's frame on top), and the screen checked pixel by pixel.
#include "test_framework.hpp"
#include "imgui.h"            // first: OkayUI.hpp keeps Dear ImGui's ImTextureID_Invalid when imgui.h came before it
#include "imgui_internal.h"
#include "okayui_compare_rig.hpp"
#include "OkayUIImGuiEmbed.hpp"
#include "ScriptEditorCodeEdit.hpp"
#include "okay/UI/OkayUICapture.hpp"

#include <cstdio>
#include <string>

using namespace okaycompare;

namespace {

const std::uint8_t kCodeBg[3] = {5, 6, 7};        // the code's background: a colour nothing else uses
const std::uint8_t kPopup[3] = {250, 0, 250};     // the ImGui popup's background
bool Is(const Bitmap& b, int x, int y, const std::uint8_t c[3]) {
    const std::uint8_t* p = &b.rgba[((size_t)y * (size_t)b.w + (size_t)x) * 4];
    return std::abs(p[0] - c[0]) <= 2 && std::abs(p[1] - c[1]) <= 2 && std::abs(p[2] - c[2]) <= 2;
}
int CountNot(const Bitmap& b, int x0, int y0, int x1, int y1, const std::uint8_t c[3]) {
    int n = 0;
    for (int y = y0; y < y1; ++y) for (int x = x0; x < x1; ++x) if (!Is(b, x, y, c)) ++n;
    return n;
}

// One frame of "the editor": OkayUI's frame open around Dear ImGui's, the code in the ImGui window, an optional ImGui popup.
struct Editor {
    ImGuiRig im;
    Raster ras;
    scripteditor::ScriptEditorCodeEdit code;
    okayuihost::ImGuiEmbed embed;
    OkayUI::CapturedGeometry cap;
    bool embedded = true;          // false: the old way, OkayUI draws the code after ImGui
    bool popup = false;
    float px = 200, py = 150;      // the popup's top-left
    ImVec2 codeMin, codeMax;
    okayuitest::Sim sim{(float)kW, (float)kH};
    Editor() {
        okayuitest::Reset();
        OkayUI::UseLauncherLook(true);
        OkayUI::Style().caretBlink = false;
        ImGui::SetCurrentContext(im.ctx);
        ImGui::GetIO().BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
        OkayUI::CodePalette p = OkayUI::DefaultCodePalette();
        p.background = p.gutter = OkayUI::RGBA(kCodeBg[0], kCodeBg[1], kCodeBg[2], 255);
        code.Edit().SetPalette(p);
        code.SetText("// a script\nvoid Start() {\n    print(\"hello\");\n}\n");
        code.UseCodeFont(18.0f);
    }
    ~Editor() { ImGui::SetCurrentContext(im.ctx); embed.Release(); OkayUI::UseLauncherLook(false); }
    void Body() {
        ImGui::TextUnformatted("Script Editor");
        codeMin = ImGui::GetCursorScreenPos();
        const ImVec2 a = ImGui::GetContentRegionAvail();
        codeMax = ImVec2(codeMin.x + a.x, codeMin.y + a.y);
        ImGui::InvisibleButton("##code", a);
        if (embedded) OkayUI::BeginCapture();
        OkayUI::BareNextWindow();
        if (OkayUI::Begin("##codewin", codeMin.x, codeMin.y, a.x, a.y)) code.Edit().Draw("##codeedit", a.x, a.y);
        OkayUI::End();
        if (embedded) {
            OkayUI::EndCapture(cap);
            embed.Append(ImGui::GetWindowDrawList(), cap, ImVec2(0, 0), codeMin, codeMax);
        }
        if (popup) {   // what the autocomplete list is: a Dear ImGui popup window over the code
            ImGui::SetNextWindowPos(ImVec2(px, py));
            ImGui::PushStyleColor(ImGuiCol_PopupBg, IM_COL32(kPopup[0], kPopup[1], kPopup[2], 255));
            ImGui::BeginTooltip();
            ImGui::Dummy(ImVec2(160, 90));
            ImGui::EndTooltip();
            ImGui::PopStyleColor();
        }
    }
    // The screen as the editor composes it: Dear ImGui's draw data, then OkayUI's frame over it.
    void Frame() {
        sim.Frame([&] { im.Frame([&] { Body(); }); });
        ras.DrawImGui();
        ras.renderer.Render(OkayUI::GetDrawData(), ras.fi);
    }
};

void PopupDrawsOverTheCode() {
    // The old way first, to show what the test sees: the OkayUI frame drawn last covers the popup.
    {
        Editor e;
        e.embedded = false; e.popup = true;
        e.Frame(); e.Frame();
        CHECK(Is(e.ras.bmp, (int)e.px + 80, (int)e.py + 45, kCodeBg));      // the popup's middle shows the code's background
    }
    Editor e;
    e.popup = true;
    e.Frame(); e.Frame();
    CHECK(Is(e.ras.bmp, (int)e.px + 80, (int)e.py + 45, kPopup));            // the popup is on top
    CHECK(Is(e.ras.bmp, (int)e.codeMax.x - 20, (int)e.codeMax.y - 20, kCodeBg));   // the code is there where nothing covers it
    // the code's text is drawn (glyphs from the code font's texture): pixels that are not background in the first lines
    const int textPixels = CountNot(e.ras.bmp, (int)e.codeMin.x + 60, (int)e.codeMin.y + 2, (int)e.codeMin.x + 190, (int)e.codeMin.y + 60, kCodeBg);
    CHECK(textPixels > 50);
    std::printf("  text pixels in the first lines: %d\n", textPixels);
    // nothing of the code is left in the OkayUI frame
    const OkayUI::DrawData d = OkayUI::GetDrawData();
    const OkayUI::DrawVertex* v = static_cast<const OkayUI::DrawVertex*>(d.vertices);
    bool codeInFrame = false;
    for (int i = 0; i < d.vertexCount; ++i) if (v[i].r == kCodeBg[0] && v[i].g == kCodeBg[1] && v[i].b == kCodeBg[2]) codeInFrame = true;
    CHECK(!codeInFrame);
    // the popup goes away: the code shows again where it was
    e.popup = false; e.Frame();
    CHECK(Is(e.ras.bmp, (int)e.px + 150, (int)e.py + 80, kCodeBg));
}

void SameAsTheOldWayWithoutPopups() {
    // With nothing over it, the code looks the same whether OkayUI draws it last (the old way) or ImGui draws it in place.
    Editor a, b;
    a.embedded = false;
    a.Frame(); a.Frame(); b.Frame(); b.Frame();
    const Diff d = Compare(a.ras.bmp, b.ras.bmp);
    std::printf("  old way against embedded: average difference %.3f, clearly different %.3f%%\n", d.avg, d.clearly);
    CHECK(d.clearly < 0.5);
}

void TexturesAreImGuiTextures() {
    Editor e;
    e.Frame(); e.Frame();
    CHECK(e.embed.TextureCount() >= 1);               // the launcher look's atlas and the code font's glyphs
    CHECK(e.embed.Skipped() == 0);
    bool inList = false;
    for (ImTextureData* t : *ImGui::GetDrawData()->Textures)
        for (const OkayUI::DrawCmd& c : e.cap.cmds) if (c.texture && e.embed.TextureOf(c.texture) == t) inList = true;
    CHECK(inList);                                    // the renderer sees them with the ImGui atlas
    for (const OkayUI::DrawCmd& c : e.cap.cmds) if (c.texture) CHECK(e.embed.TextureOf(c.texture) && e.embed.TextureOf(c.texture)->Status == ImTextureStatus_OK);
    // typing characters the font has not baked yet: the texture is uploaded again (or replaced when the atlas grew) and still draws
    e.code.Edit().Focus();
    e.Frame();
    e.sim.Type("\xce\xa9\xce\xb6\xd0\x96\xd0\xaf");   // Greek and Cyrillic letters
    e.Frame(); e.Frame(); e.Frame();
    CHECK(e.code.Edit().Line(0).find("\xce\xa9") != std::string::npos);
    for (const OkayUI::DrawCmd& c : e.cap.cmds) if (c.texture) {
        OkayUI::TexturePixels p; CHECK(OkayUI::GetTexturePixels(c.texture, p));
        ImTextureData* t = e.embed.TextureOf(c.texture);
        CHECK(t && t->Width == p.w && t->Height == p.h && t->Status == ImTextureStatus_OK);
        if (t && t->Width == p.w && t->Height == p.h) CHECK(std::memcmp(t->GetPixels(), p.rgba, (size_t)p.w * p.h * 4) == 0);
    }
    // Release hands them back
    e.embed.Release();
    CHECK(e.embed.TextureCount() == 0);
}

void ManyLinesStayWhole() {
    // A big view: more than 65,536 corners in one draw command must still all arrive (16-bit indices, chunked with vertex offsets).
    Editor e;
    std::string t;
    for (int i = 0; i < 400; ++i) t += "    var value_" + std::to_string(i) + " = compute(alpha, beta, gamma) + other_function(delta) * 12345;\n";
    e.code.SetText(t);
    e.code.UseCodeFont(6.0f);   // small: many lines in view
    e.Frame(); e.Frame();
    size_t corners = 0;
    for (const OkayUI::DrawCmd& c : e.cap.cmds) corners += (size_t)c.idxCount;
    size_t inLists = 0;
    for (const ImDrawList* l : ImGui::GetDrawData()->CmdLists) inLists += (size_t)l->IdxBuffer.Size;
    std::printf("  captured corners %zu, ImGui indices %zu\n", corners, inLists);
    CHECK(corners > 0 && inLists >= corners);
}

} // namespace

int main() {
    RUN_SUITE("okayui codeedit in Dear ImGui");
    PopupDrawsOverTheCode();
    SameAsTheOldWayWithoutPopups();
    TexturesAreImGuiTextures();
    ManyLinesStayWhole();
    TEST_MAIN_RESULT();
}
