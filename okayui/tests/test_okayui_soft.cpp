// The OkayUI software backend: exact pixels, the fill rule, scissor, atlas sampling, hostile draw data, and
// OkayUI's own draw list (text lit-pixel counts, clipped children) rasterised end to end.
#include "test_framework.hpp"
#include "OkayUITestKit.hpp"
#include "okay/Graphics/Font.hpp"

using namespace okayuitest;
namespace UI = OkayUI;

namespace {

// A hand-built draw list.
struct Mini {
    std::vector<UI::DrawVertex> v;
    std::vector<int> idx;
    std::vector<UI::DrawCmd> cmds;
    unsigned char atlas[2 * 2 * 4] = {255,0,0,255, 0,255,0,255, 0,0,255,255, 255,255,255,128};
    int tag = 0;
    void Quad(float x0, float y0, float x1, float y1, unsigned char r, unsigned char g, unsigned char b, unsigned char a = 255) {
        const int base = (int)v.size();
        v.push_back({x0, y0, r, g, b, a, 0, 0}); v.push_back({x1, y0, r, g, b, a, 1, 0});
        v.push_back({x1, y1, r, g, b, a, 1, 1}); v.push_back({x0, y1, r, g, b, a, 0, 1});
        for (int i : {0, 1, 2, 0, 2, 3}) idx.push_back(base + i);
    }
    void Tri(float ax, float ay, float bx, float by, float cx, float cy, unsigned char r, unsigned char g, unsigned char b, unsigned char a = 255) {
        const int base = (int)v.size();
        v.push_back({ax, ay, r, g, b, a, 0, 0}); v.push_back({bx, by, r, g, b, a, 0, 0}); v.push_back({cx, cy, r, g, b, a, 0, 0});
        for (int i : {0, 1, 2}) idx.push_back(base + i);
    }
    void Cmd(int off, int count, bool clip = false, int cx = 0, int cy = 0, int cw = 0, int ch = 0, const void* tex = nullptr) {
        UI::DrawCmd c; c.idxOffset = off; c.idxCount = count; c.clipOn = clip; c.clipX = cx; c.clipY = cy; c.clipW = cw; c.clipH = ch; c.texture = tex;
        cmds.push_back(c);
    }
    UI::DrawData Data(bool withAtlas = false) const {
        UI::DrawData d;
        d.vertices = v.data(); d.vertexCount = (int)v.size(); d.vertexStride = (int)sizeof(UI::DrawVertex);
        d.indices = idx.data(); d.indexCount = (int)idx.size();
        d.cmds = cmds.data(); d.cmdCount = (int)cmds.size();
        if (withAtlas) { d.atlasTexture = &tag; d.atlasRgba = atlas; d.atlasW = 2; d.atlasH = 2; }
        return d;
    }
};
unsigned Px(const Bitmap& b, int x, int y) { return PixelAt(b, x, y); }
int Lit(const Bitmap& b, int x0, int y0, int x1, int y1) { return CountNot(b, x0, y0, x1, y1, Rgba(0, 0, 0)); }

} // namespace

int main() {
    RUN_SUITE("okayui soft backend");
    {   // flat quads: exact pixels, outside untouched, two commands
        Mini m;
        m.Quad(10, 10, 20, 20, 255, 0, 0); m.Quad(30, 10, 40, 20, 0, 255, 0);
        m.Cmd(0, 12);
        Bitmap bm(64, 64); bm.Clear(0, 0, 0);
        UI::soft::Render(m.Data(), bm);
        CHECK(Px(bm, 10, 10) == Rgba(255, 0, 0) && Px(bm, 19, 19) == Rgba(255, 0, 0));
        CHECK(Px(bm, 9, 10) == Rgba(0, 0, 0) && Px(bm, 20, 10) == Rgba(0, 0, 0));
        CHECK(Lit(bm, 10, 10, 20, 20) == 100 && Lit(bm, 30, 10, 40, 20) == 100);
    }
    {   // fill rule: two triangles sharing a diagonal cover each pixel exactly once (a blend would show a double hit)
        Mini m;
        m.Tri(44, 8, 60, 8, 44, 40, 255, 255, 255, 128);
        m.Tri(60, 8, 60, 40, 44, 40, 255, 255, 255, 128);
        m.Quad(8, 8, 40, 24, 255, 255, 255, 128);     // an axis-aligned quad is two triangles with a shared diagonal too
        m.Cmd(0, (int)m.idx.size());
        Bitmap bm(64, 64); bm.Clear(0, 0, 0);
        UI::soft::Render(m.Data(), bm);
        const unsigned inner = Px(bm, 24, 16);
        bool same = true;
        for (int y = 8; y < 24; ++y) for (int x = 8; x < 40; ++x) if (Px(bm, x, y) != inner) same = false;
        CHECK(same && (inner >> 24) == 128);
        same = true;
        for (int y = 12; y < 38; ++y) for (int x = 56; x < 60; ++x) if (Px(bm, x, y) != inner) same = false;
        CHECK(same);
    }
    {   // scissor of a command
        Mini m;
        m.Quad(0, 0, 40, 40, 255, 255, 255);
        m.Cmd(0, 6, true, 5, 5, 10, 10);
        Bitmap bm(64, 64); bm.Clear(0, 0, 0);
        UI::soft::Render(m.Data(), bm);
        CHECK(Lit(bm, 0, 0, 64, 64) == 100 && Px(bm, 5, 5) != 0xff && Px(bm, 4, 5) == Rgba(0, 0, 0) && Px(bm, 15, 5) == Rgba(0, 0, 0));
    }
    {   // the atlas: sampled and multiplied by the vertex colour; another texture is skipped
        Mini m;
        m.Quad(0, 0, 4, 4, 255, 255, 255);
        m.Quad(10, 10, 14, 14, 255, 255, 255);
        m.Cmd(0, 6, false, 0, 0, 0, 0, &m.tag);
        int other = 0;
        m.Cmd(6, 6, false, 0, 0, 0, 0, &other);
        Bitmap bm(64, 64); bm.Clear(0, 0, 0);
        UI::soft::Render(m.Data(true), bm);
        CHECK(Px(bm, 0, 0) == Rgba(255, 0, 0) && Px(bm, 3, 0) == Rgba(0, 255, 0) && Px(bm, 0, 3) == Rgba(0, 0, 255));
        CHECK((Px(bm, 3, 3) >> 24) == 128 && Lit(bm, 10, 10, 14, 14) == 0);
        Bitmap b2(64, 64); b2.Clear(0, 0, 0);
        UI::soft::Render(m.Data(false), b2);          // no atlas given: the textured command draws nothing
        CHECK(Lit(b2, 0, 0, 64, 64) == 0);
    }
    {   // hostile draw data must not crash or write outside the bitmap
        Mini m;
        m.Quad(-1e30f, 0, 1e30f, 1e30f, 255, 0, 0);
        m.Quad(NAN, 3, 5, 5, 255, 0, 0);
        m.Quad(0, 0, 1e7f, 1e7f, 0, 255, 0);
        m.Quad(0, 0, 8, 8, 255, 255, 255);
        m.v.back().u = NAN; m.v[m.v.size() - 2].v = INFINITY;      // non-finite texture coordinates
        m.idx.push_back(-1); m.idx.push_back(2); m.idx.push_back(99999);   // out-of-range indices
        const int n = (int)m.idx.size();
        m.Cmd(0, n);
        m.Cmd(6, 12, true, -100, -100, 2000000000, 2000000000);     // clip that overflows 32-bit maths
        m.Cmd(-4, 9, true, 0, 0, -5, -5);                            // negative offset, negative size
        m.Cmd(18, 6, false, 0, 0, 0, 0, &m.tag);                     // NaN uv sampled from the atlas
        m.Cmd(1000, 6);                                              // past the end
        Bitmap bm(32, 32); bm.Clear(0, 0, 0);
        UI::soft::Render(m.Data(true), bm);
        CHECK(Lit(bm, 0, 0, 32, 32) > 0);
        Bitmap empty; UI::soft::Render(m.Data(), empty);
        UI::DrawData bad = m.Data(); bad.vertexStride = 7; UI::soft::Render(bad, bm);
        UI::DrawData none; UI::soft::Render(none, bm);
    }

    // ---- OkayUI's own draw list through the backend ------------------------------------------
    {   // classic text: lit pixels = source glyph pixels * scale^2
        Reset();
        Sim sim(64, 64);
        sim.Frame([&] { UI::Label(0, 0, "A"); });
        const Bitmap& bm = sim.Draw();
        int src = 0;
        for (int y = 0; y < 8; ++y) for (int x = 0; x < 8; ++x) if (okay::Font8x8::Pixel('A', x, y)) ++src;
        CHECK(src > 0 && Lit(bm, 0, 0, 16, 16) == src * 4);
        CHECK(Lit(bm, 16, 0, 64, 64) == 0);
    }
    {   // a button: the frame is drawn, the corners are rounded away, the label is inside
        Reset();
        Sim sim(200, 80);
        sim.Frame([&] { UI::Button(1, 20, 20, 120, 40, "Go"); });
        const Bitmap& bm = sim.Draw();
        CHECK(Px(bm, 80, 25) == ThemeRgba(UI::Style().bg));
        CHECK(Px(bm, 20, 20) == Rgba(0, 0, 0));                       // the 4 px corner radius cuts the very corner
        CHECK(Lit(bm, 0, 0, 200, 80) > 120 * 40 / 2);
    }
    {   // a scrolling child is clipped: a tall list does not paint below its box
        Reset();
        Sim sim(300, 300);
        UI::Rect box;
        auto ui = [&] {
            if (UI::Begin("w", 0, 0, 280, 280)) {
                if (UI::BeginChild("kid", 200, 60)) { for (int i = 0; i < 30; ++i) UI::Selectable("a long enough row label", true); UI::EndChild(); }
                box = UI::GetItemRect();
            }
            UI::End();
        };
        sim.Frames(2, ui);
        const Bitmap& bm = sim.Draw();
        const unsigned panelBelow = Px(bm, (int)box.x + 20, (int)(box.y + box.h + 6));
        CHECK(Px(bm, (int)box.x + 20, (int)(box.y + 8)) == ThemeRgba(UI::Style().accent));   // the first row, above its text
        CHECK(Px(bm, (int)box.x + 20, (int)(box.y + box.h + 2)) == panelBelow);   // below the box: plain window panel
        const UI::DrawData d = UI::GetDrawData();
        bool clipped = false; for (int i = 0; i < d.cmdCount; ++i) clipped |= d.cmds[i].clipOn;
        CHECK(clipped);
    }
    TEST_MAIN_RESULT();
}
