// The multi-font store (AddFontTtf, PushFont with a real font, DrawList::AddText with a font, io.Fonts requests that make real fonts, the base-85 array
// mapping), headless. Fonts are the engine's embedded Roboto (okay::EmbeddedUIFontData) and the DejaVu file in assets/. Written from the behaviour the
// editor needs: a heading face and a code face drawn next to the UI face in one frame.
#include "test_framework.hpp"
#include "OkayUITestKit.hpp"
#include "okay/UI/OkayUIInternals.hpp"
#include "okay/Graphics/TtfFont.hpp"

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
        bool tex = false; for (int c = 0; c < d.cmdCount; ++c) if (d.cmds[c].texture && UI::FallbackFontOf(d.cmds[c].texture) == static_cast<const okay::TtfFont*>(f->ttf) ? true : false) tex = true;
        CHECK(tex);
        // after the call the active font and scale are what they were
        CHECK(UI::GetFont() == UI::FontDefault());
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

    {   // ---- a MergeMode request is recorded but loads nothing: OkayUI never merges fonts, so a merged face was a dead copy ----
        // (the editor merged an installed CJK font, the launcher an emoji font: each read the whole file twice and baked an atlas
        // that nothing drew; CJK and emoji text is drawn by the fallback font, okay::FontForText)
        Reset();
        UI::IO& io = UI::GetIO();
        io.Fonts->Clear();   // (the blocks above filled the request list)
        const std::vector<unsigned char> copy = roboto;   // other bytes than the ones registered above: a new font file
        UI::ImFontConfig fc; fc.MergeMode = true;
        UI::ImFont* fromMemory = io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(copy.data()), (int)copy.size(), 16.0f, &fc);
        UI::ImFont* fromFile = io.Fonts->AddFontFromFileTTF(OKAYUI_GOLDEN_DIR "/../../../assets/fonts/DejaVuSans.ttf", 16.0f, &fc);
        CHECK(fromMemory != nullptr && fromFile != nullptr);                        // still answered with a font (PushFont keeps working)
        CHECK(fromMemory->ttf == nullptr && fromFile->ttf == nullptr);              // and nothing loaded for it
        int n = 0; const UI::FontRequest* rq = UI::FontRequests(&n);
        CHECK(n >= 2 && rq[n - 2].merge && rq[n - 1].merge && rq[n - 2].font == fromMemory && rq[n - 1].font == fromFile);   // recorded
        UI::ImFontConfig plain;                                                     // the same bytes without MergeMode do load
        UI::ImFont* loaded = io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(copy.data()), (int)copy.size(), 16.0f, &plain);
        CHECK(loaded != nullptr && loaded->ttf != nullptr);
    }
    TEST_MAIN_RESULT();
}
