// The multi-font store (AddFontTtf, PushFont with a real font, DrawList::AddText with a font, io.Fonts requests that make real fonts, the base-85 array
// mapping), headless. Fonts are the engine's embedded Roboto (okay::EmbeddedUIFontData) and the DejaVu file in assets/. Written from the behaviour the
// editor needs: a heading face and a code face drawn next to the UI face in one frame.
#include "test_framework.hpp"
#include "OkayUITestKit.hpp"
#include "okay/UI/OkayUIInternals.hpp"
#include "okay/Graphics/TtfFont.hpp"
#include "okay/Platform/Painter.hpp"
#include "OkayUI_SoftPainter.hpp"

#include <cmath>
#include <cstring>
#include <string>
#include <vector>

using namespace okayuitest;
namespace UI = OkayUI;

namespace {
// The bounding box height of the glyph quads that use a fallback (user) font in the frame's draw data.
bool TextBox(float* h, float* w, float* top) {
    const UI::DrawData d = UI::GetDrawData();
    const UI::DrawVertex* v = static_cast<const UI::DrawVertex*>(d.vertices);
    const int* idx = d.indices;
    float y0 = 1e9f, y1 = -1e9f, x0 = 1e9f, x1 = -1e9f; bool any = false;
    for (int c = 0; c < d.cmdCount; ++c) {
        if (!d.cmds[c].texture || !UI::FallbackFontOf(d.cmds[c].texture)) continue;
        for (int i = d.cmds[c].idxOffset; i < d.cmds[c].idxOffset + d.cmds[c].idxCount; ++i) {
            const UI::DrawVertex& p = v[idx[i]];
            y0 = std::min(y0, p.y); y1 = std::max(y1, p.y); x0 = std::min(x0, p.x); x1 = std::max(x1, p.x); any = true;
        }
    }
    if (any) { *h = y1 - y0; *w = x1 - x0; *top = y0; }
    return any;
}

// Sharp text, measured on the draw data: every glyph quad of a font texture covers exactly as many screen pixels as atlas pixels (no
// scaling) and starts on a whole pixel. Returns the number of quads checked; `bad` counts the ones that break the rule.
int OneToOneQuads(int* bad) {
    const UI::DrawData d = UI::GetDrawData();
    const UI::DrawVertex* v = static_cast<const UI::DrawVertex*>(d.vertices);
    int n = 0; *bad = 0;
    for (int c = 0; c < d.cmdCount; ++c) {
        const okay::TtfFont* f = d.cmds[c].texture ? UI::FallbackFontOf(d.cmds[c].texture) : nullptr;
        if (!f) continue;
        const float aw = (float)f->Atlas().Width(), ah = (float)f->Atlas().Height();
        for (int i = d.cmds[c].idxOffset; i + 5 < d.cmds[c].idxOffset + d.cmds[c].idxCount; i += 6) {   // a quad: b0 b0+1 b0+2, b0 b0+2 b0+3
            const UI::DrawVertex& a = v[d.indices[i]]; const UI::DrawVertex& b = v[d.indices[i + 2]];
            const float sw = b.x - a.x, sh = b.y - a.y, tw = (b.u - a.u) * aw, th = (b.v - a.v) * ah;
            const bool whole = std::fabs(a.x - std::round(a.x)) < 0.01f && std::fabs(a.y - std::round(a.y)) < 0.01f;
            if (!whole || std::fabs(sw - tw) > 0.01f || std::fabs(sh - th) > 0.01f) ++*bad;
            ++n;
        }
    }
    return n;
}
// Sharp text, measured on pixels: "H" drawn white on black through the software painter (linear sampling, as a GPU samples) compared
// with the font's own rasterisation at that pixel size (the reference: what a perfect renderer shows). The mean difference per pixel
// over the glyph's box, 0..255.
double GlyphError(const std::vector<unsigned char>& bytes, float px, float x, float y) {
    okay::TtfFont ref;
    if (!ref.LoadFromMemory(bytes.data(), bytes.size(), px)) return 255.0;
    auto painter = UI::soft::CreateBitmapPainter(200, 120);
    if (!painter) return 255.0;
    painter->SetClipRect(nullptr); painter->SetBlendMode(okay::platform::BlendMode::None); painter->SetDrawColor(0, 0, 0, 255); painter->Clear();
    UI::soft::FrameDrawer fd(*painter);
    fd.Draw(UI::GetDrawData());
    std::vector<std::uint8_t> rgba; int w = 0, h = 0;
    painter->ReadPixels(rgba, w, h);
    const okay::TtfFont::Glyph* g = ref.GlyphFor('H');
    if (!g || w != 200 || h != 120) return 255.0;
    const int ox = (int)std::floor(x) + (int)g->xoff, oy = (int)std::floor(y + ref.Ascent(px) + 0.5f) + (int)g->yoff;
    double sum = 0.0; int n = 0;
    for (int j = -1; j <= g->y1 - g->y0; ++j) for (int i = -1; i <= g->x1 - g->x0; ++i) {   // a pixel of margin: a blurred edge spreads into it
        const int sx = ox + i, sy = oy + j;
        if (sx < 0 || sy < 0 || sx >= w || sy >= h) continue;
        const bool in = i >= 0 && j >= 0 && i < g->x1 - g->x0 && j < g->y1 - g->y0;
        const int want = in ? ref.Atlas().Data()[((std::size_t)(g->y0 + j) * (std::size_t)ref.Atlas().Width() + (std::size_t)(g->x0 + i)) * 4 + 3] : 0;
        sum += std::abs((int)rgba[((std::size_t)sy * (std::size_t)w + (std::size_t)sx) * 4] - want); ++n;
    }
    return n ? sum / n : 255.0;
}
}

int main() {
    RUN_SUITE("okayui fonts");
    std::vector<unsigned char> roboto;
    CHECK(okay::EmbeddedUIFontData(roboto) && roboto.size() > 100000);

    {   // ---- registering ----
        Reset();
        UI::Font* smallFont = UI::AddFontTtf(roboto.data(), roboto.size(), 16.0f, "Roboto");
        UI::Font* big = UI::AddFontTtf(roboto.data(), roboto.size(), 32.0f);
        CHECK(smallFont && big && smallFont != big && smallFont->ttf && smallFont->ttf == big->ttf && smallFont->px == 16.0f && big->px == 32.0f);
        const unsigned char junk[64] = {1, 2, 3};
        CHECK(UI::AddFontTtf(nullptr, 100, 16.0f) == nullptr && UI::AddFontTtf(roboto.data(), 4, 16.0f) == nullptr && UI::AddFontTtf(junk, sizeof junk, 16.0f) == nullptr);
        CHECK(UI::AddFontTtf(roboto.data(), roboto.size(), 0.0f) == nullptr && UI::AddFontTtf(roboto.data(), roboto.size(), NAN) == nullptr && UI::AddFontTtf(roboto.data(), roboto.size(), 5000.0f) == nullptr);
        CHECK(UI::AddFontTtfFile("no/such/file.ttf", 16.0f) == nullptr && UI::AddFontTtfFile(nullptr, 16.0f) == nullptr);
    }

    {   // ---- measuring and PushFont ----
        Reset(); Sim s(400, 300);
        UI::Font* smallFont = UI::AddFontTtf(roboto.data(), roboto.size(), 16.0f);
        UI::Font* big = UI::AddFontTtf(roboto.data(), roboto.size(), 32.0f);
        const float scale0 = UI::Style().textScale;
        float wSmall = 0, wBig = 0, hSmall = 0, hBig = 0, hAfter = 0, wAfter = 0, rowBig = 0, rowSmall = 0; float scaleIn = 0, scaleAfter = 0; const UI::Font* fontAfter = nullptr;
        s.Frame([&] {
            UI::SetNextWindowPos(UI::Vec2{0, 0}, UI::Cond_Always); UI::SetNextWindowSize(UI::Vec2{390, 290}, UI::Cond_Always);
            UI::Begin("f", nullptr, 0);
            UI::PushFont(smallFont); wSmall = UI::CalcTextSize("Heading text").x; hSmall = UI::GetTextLineHeight(); rowSmall = UI::GetFrameHeight(); UI::PopFont();
            UI::PushFont(big);   wBig = UI::CalcTextSize("Heading text").x;   hBig = UI::GetTextLineHeight(); rowBig = UI::GetFrameHeight(); scaleIn = UI::Style().textScale; UI::PopFont();
            wAfter = UI::CalcTextSize("Heading text").x; hAfter = UI::GetTextLineHeight(); fontAfter = UI::GetFont(); scaleAfter = UI::Style().textScale;
            UI::End();
        });
        CHECK(wSmall > 40.0f && std::fabs(wBig / wSmall - 2.0f) < 0.03f);
        CHECK(std::fabs(hSmall - 16.0f) < 0.6f && std::fabs(hBig - 32.0f) < 0.6f && rowBig > rowSmall + 10.0f);
        CHECK(fontAfter == UI::FontDefault() && scaleAfter == scale0 && scaleIn != scale0 && std::fabs(hAfter - 8.0f * scale0) < 0.01f && wAfter != wBig);
        // the font changes how wide the same text is: not the 8x8 cell width
        CHECK(std::fabs(wSmall - 12.0f * 8.0f * 2.0f) > 5.0f);
    }

    {   // ---- drawing: glyph quads of the right size, in a texture the backend can find ----
        float hs = 0, ws = 0, ts = 0, hb = 0, wb = 0, tb = 0;
        for (int pass = 0; pass < 2; ++pass) {
            Reset(); Sim s(400, 300);
            UI::Font* f = UI::AddFontTtf(roboto.data(), roboto.size(), pass ? 32.0f : 16.0f);
            s.Frame([&] {
                UI::SetNextWindowPos(UI::Vec2{0, 0}, UI::Cond_Always); UI::SetNextWindowSize(UI::Vec2{390, 290}, UI::Cond_Always);
                UI::Begin("f", nullptr, UI::Window_NoTitleBar);
                UI::PushFont(f); UI::Text("HHHH"); UI::PopFont();
                UI::End();
            });
            if (pass) CHECK(TextBox(&hb, &wb, &tb)); else CHECK(TextBox(&hs, &ws, &ts));
        }
        CHECK(hs > 8.0f && hb > hs * 1.8f && hb < hs * 2.2f && wb > ws * 1.8f && wb < ws * 2.2f);
        // a user font is a fallback-font texture: the backend uploads its atlas
        Reset(); Sim s(400, 300);
        UI::Font* f = UI::AddFontTtf(roboto.data(), roboto.size(), 20.0f);
        s.Frame([&] {
            UI::SetNextWindowPos(UI::Vec2{0, 0}, UI::Cond_Always); UI::SetNextWindowSize(UI::Vec2{390, 290}, UI::Cond_Always);
            UI::Begin("f", nullptr, 0);
            UI::GetWindowDrawList().AddText(f, 24.0f, UI::Vec2{20, 100}, 0xffffffffu, "Draw list text");
            UI::End();
        });
        float h = 0, w = 0, t = 0;
        CHECK(TextBox(&h, &w, &t) && h > 10.0f && h < 30.0f && t > 90.0f);
        const UI::DrawData d = UI::GetDrawData();
        // (drawn sharp: the face of that font baked at the 24 pixels drawn)
        const okay::TtfFont* sized = static_cast<const okay::TtfFont*>(f->ttf)->ForDeviceSize(24.0f, false);
        bool tex = false; for (int c = 0; c < d.cmdCount; ++c) if (d.cmds[c].texture && UI::FallbackFontOf(d.cmds[c].texture) == sized) tex = true;
        CHECK(tex);
        // after the call the active font and scale are what they were
        CHECK(UI::GetFont() == UI::FontDefault());
    }

    {   // ---- sharp text at 100%, 125% and 150%: the glyphs are baked at the size drawn and placed on whole pixels ----
        // The editor's UI font is a user font (16 px Roboto); its UI scale makes the text 20 px at 125% and 24 px at 150%. Before round 6
        // every size was the font's 48-pixel bake scaled down (and not snapped), and the launcher look's 24 px text was its 26 px glyphs.
        const float sizes[3] = {16.0f, 20.0f, 24.0f};
        for (int look = 0; look < 2; ++look) for (float px : sizes) {
            Reset(); Sim s(200, 120);
            if (look) UI::UseLauncherLook(true);
            UI::Font* f = look ? nullptr : UI::AddFontTtf(roboto.data(), roboto.size(), 16.0f);
            s.Frame([&] {
                UI::SetNextWindowPos(UI::Vec2{0, 0}, UI::Cond_Always); UI::SetNextWindowSize(UI::Vec2{200, 120}, UI::Cond_Always);
                UI::Begin("sharp", nullptr, UI::Window_NoTitleBar | UI::Window_NoBackground);
                if (look) { const float saved = UI::Style().textScale; UI::Style().textScale = px / 8.0f;
                            UI::GetWindowDrawList().AddText(UI::Vec2{20.0f, 40.6f}, 0xffffffffu, "H", 0.0f); UI::Style().textScale = saved; }
                else UI::GetWindowDrawList().AddText(f, px, UI::Vec2{20.0f, 40.6f}, 0xffffffffu, "H");
                UI::End();
            });
            int bad = -1;
            const int quads = OneToOneQuads(&bad);
            const double err = GlyphError(roboto, px, 20.0f, 40.6f);
            std::cout << "  sharp text: " << (look ? "launcher look" : "user font") << " at " << px << " px: " << quads << " glyph quads, "
                      << bad << " scaled or off the pixel grid; mean error against the font's own rasterisation " << err << " of 255\n";
            if (!look || px == 24.0f) CHECK(quads == 1 && bad == 0);   // (the launcher look's 16 and 20 px are sizes its own atlas bakes, drawn 1:1 already)
            CHECK(err < 6.0);
        }
    }

    {   // ---- widgets with a real font, switching between fonts in one frame ----
        Reset(); Sim s(400, 300);
        UI::Font* head = UI::AddFontTtf(roboto.data(), roboto.size(), 26.0f);
        UI::Font* body = UI::AddFontTtf(roboto.data(), roboto.size(), 14.0f);
        char buf[32] = "abc"; bool chk = false; float fv = 1.0f; int clicks = 0;
        for (int i = 0; i < 3; ++i) s.Frame([&] {
            UI::SetNextWindowPos(UI::Vec2{0, 0}, UI::Cond_Always); UI::SetNextWindowSize(UI::Vec2{390, 290}, UI::Cond_Always);
            UI::Begin("w", nullptr, 0);
            UI::PushFont(head); UI::Text("Title"); UI::PopFont();
            UI::PushFont(body);
            if (UI::Button("Go")) ++clicks;
            UI::InputText("##t", buf, sizeof buf); UI::Checkbox("c", &chk); UI::SliderFloat("s", &fv, 0, 2);
            UI::PopFont();
            UI::Text("back to the default");
            UI::End();
        });
        CHECK(UI::GetFont() == UI::FontDefault());
        // nested pushes and an unbalanced pop do not break the stack
        UI::PushFont(head); UI::PushFont(body); UI::PopFont(); CHECK(UI::GetFont() == head); UI::PopFont(); CHECK(UI::GetFont() == UI::FontDefault()); UI::PopFont(); CHECK(UI::GetFont() == UI::FontDefault());
        UI::SetFont(head); UI::SetFont(nullptr); CHECK(UI::GetFont() == UI::FontDefault() && std::fabs(UI::Style().textScale - 2.0f) < 5.0f);
        UI::ResetState(); CHECK(UI::GetFont() == UI::FontDefault());
        (void)clicks;
    }

    {   // ---- io.Fonts makes real fonts; the base-85 arrays map to decoded bytes ----
        Reset();
        UI::IO& io = UI::GetIO();
        static const char kArrayA[] = "ABCDEF", kArrayB[] = "UVWXYZ";
        UI::RegisterBase85FontSource(kArrayA, roboto.data(), roboto.size());
        UI::ImFontConfig fc; fc.OversampleH = 2;
        UI::ImFont* ui = io.Fonts->AddFontFromMemoryCompressedBase85TTF(kArrayA, 18.0f, &fc);
        UI::ImFont* heading = io.Fonts->AddFontFromMemoryCompressedBase85TTF(kArrayA, 22.0f, &fc);
        UI::ImFont* unmapped = io.Fonts->AddFontFromMemoryCompressedBase85TTF(kArrayB, 18.0f, &fc);
        CHECK(ui && heading && ui != heading && ui->ttf && ui->px == 18.0f && heading->px == 22.0f && ui->ttf == heading->ttf);
        CHECK(unmapped && unmapped->ttf == nullptr);   // not mapped: the default font, recorded
        UI::ImFont* mem = io.Fonts->AddFontFromMemoryTTF(roboto.data(), (int)roboto.size(), 15.0f);
        CHECK(mem && mem->ttf && mem->px == 15.0f);
        UI::ImFont* file = io.Fonts->AddFontFromFileTTF(OKAYUI_GOLDEN_DIR "/../../../assets/fonts/DejaVuSans.ttf", 13.0f);
        CHECK(file != nullptr);
        int n = 0; const UI::FontRequest* rq = UI::FontRequests(&n);
        CHECK(n >= 4 && rq[n - 4].font == ui || rq[0].font != nullptr);
        // past the fixed room for font FILES the call still answers with a font
        for (int i = 0; i < 10; ++i) CHECK(io.Fonts->AddFontFromMemoryTTF(roboto.data(), (int)roboto.size(), 10.0f + (float)i) != nullptr);
        for (int i = 0; i < 40; ++i) CHECK(io.Fonts->AddFontDefault() != nullptr);
    }
    TEST_MAIN_RESULT();
}
