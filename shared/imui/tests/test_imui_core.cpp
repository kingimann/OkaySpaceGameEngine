// okay::imui core: hashing, UTF-8, font atlas, draw list, software backend, windows and the ID stack.
#include "test_framework.hpp"
#include "ImuiTestKit.hpp"

using namespace imuitest;

static int CountLit(const Bitmap& b, int x0, int y0, int x1, int y1) {
    int n = 0;
    for (int y = y0; y < y1; ++y) for (int x = x0; x < x1; ++x) if (b.rgba[((size_t)y * b.w + x) * 4 + 3] != 0 && (b.rgba[((size_t)y * b.w + x) * 4] | b.rgba[((size_t)y * b.w + x) * 4 + 1] | b.rgba[((size_t)y * b.w + x) * 4 + 2])) ++n;
    return n;
}
static Col Px(const Bitmap& b, int x, int y) {
    const unsigned char* p = &b.rgba[((size_t)y * b.w + x) * 4];
    return MakeCol(p[0], p[1], p[2], p[3]);
}

int main() {
    RUN_SUITE("imui core");

    // ---- hashing and IDs ---------------------------------------------------------------------
    CHECK(HashString("a") != 0 && HashString("") != 0);
    CHECK(HashString("a") == HashString("a"));
    CHECK(HashString("a") != HashString("b"));
    CHECK(HashString("a", 1) != HashString("a", 2));
    {
        const char data[] = {1, 2, 3, 4};
        CHECK(HashBytes(data, 4) == HashBytes(data, 4) && HashBytes(data, 3) != HashBytes(data, 4));
        CHECK(HashBytes(nullptr, 0) != 0);
    }

    // ---- UTF-8 -------------------------------------------------------------------------------
    {
        std::uint32_t cp = 0;
        CHECK(Utf8Decode("A", "A" + 1, &cp) == 1 && cp == 'A');
        CHECK(Utf8Decode("\xC3\xA9", "\xC3\xA9" + 2, &cp) == 2 && cp == 0xE9);
        CHECK(Utf8Decode("\xE2\x82\xAC", "\xE2\x82\xAC" + 3, &cp) == 3 && cp == 0x20AC);
        CHECK(Utf8Decode("\xF0\x9F\x98\x80", "\xF0\x9F\x98\x80" + 4, &cp) == 4 && cp == 0x1F600);
        CHECK(Utf8Decode("\xC3", "\xC3" + 1, &cp) == 1 && cp == 0xFFFD);          // truncated
        CHECK(Utf8Decode("\xC0\x80", "\xC0\x80" + 2, &cp) == 1 && cp == 0xFFFD);  // overlong
        CHECK(Utf8Decode("\xED\xA0\x80", "\xED\xA0\x80" + 3, &cp) == 1 && cp == 0xFFFD);  // surrogate
        CHECK(Utf8Decode("\xFF", "\xFF" + 1, &cp) == 1 && cp == 0xFFFD);
        CHECK(Utf8Decode("x", "x", &cp) == 0);
        std::string s; Utf8Append(s, 0x20AC); Utf8Append(s, 'z'); Utf8Append(s, 0x1F600); Utf8Append(s, 0xD800);
        CHECK(Utf8Valid(s));
        CHECK(!Utf8Valid(std::string("\xC3")));
        CHECK(Utf8Valid(""));
    }

    // ---- font atlas --------------------------------------------------------------------------
    Context* ctx = MakeContext();
    {
        const FontAtlas& f = ctx->font;
        CHECK(f.Valid() && f.width == 16 * 16 && f.height == 6 * 16);
        CHECK_NEAR(f.lineHeight, 16, 1e-6);
        CHECK_NEAR(f.CalcTextSize("AB").x, 32, 1e-6);
        CHECK_NEAR(f.CalcTextSize("AB").y, 16, 1e-6);
        CHECK_NEAR(f.CalcTextSize("A\nBCD").x, 48, 1e-6);
        CHECK_NEAR(f.CalcTextSize("A\nBCD").y, 32, 1e-6);
        CHECK_NEAR(f.CalcTextSize("", nullptr).x, 0, 1e-6);
        CHECK_NEAR(f.CalcTextSize("\xC3\xA9").x, 16, 1e-6);       // one glyph (fallback) for one code point
        // the 'A' glyph has the same number of lit pixels as the source bitmap times the scale squared
        int src = 0;
        for (int y = 0; y < 8; ++y) for (int x = 0; x < 8; ++x) if (okay::Font8x8::Pixel('A', x, y)) ++src;
        int lit = 0;
        const Glyph& g = f.GetGlyph('A');
        for (int y = (int)(g.v0 * f.height + 0.5f); y < (int)(g.v1 * f.height + 0.5f); ++y)
            for (int x = (int)(g.u0 * f.width + 0.5f); x < (int)(g.u1 * f.width + 0.5f); ++x) if (f.alpha[(size_t)y * f.width + x]) ++lit;
        CHECK(src > 0 && lit == src * 4);
    }

    // ---- draw list and software backend ------------------------------------------------------
    {
        DrawList dl; dl.Init(&ctx->font, Rect(0, 0, 64, 64));
        dl.AddRectFilled(Vec2(10, 10), Vec2(20, 20), MakeCol(255, 0, 0));
        CHECK(dl.vtx.size() == 4 && dl.idx.size() == 6 && dl.cmds.size() == 1 && dl.cmds[0].idxCount == 6);
        dl.AddRectFilled(Vec2(30, 10), Vec2(40, 20), MakeCol(0, 255, 0));
        CHECK(dl.cmds.size() == 1 && dl.cmds[0].idxCount == 12);                 // merged
        dl.PushClipRect(Rect(0, 0, 30, 30));
        dl.AddRectFilled(Vec2(0, 30), Vec2(5, 35), MakeCol(0, 0, 255));
        CHECK(dl.cmds.size() == 2);                                              // new clip => new command
        dl.PushClipRect(Rect(10, 10, 100, 100));                                 // intersected
        CHECK_NEAR(dl.ClipRect().max.x, 30, 1e-6);
        dl.PopClipRect(); dl.PopClipRect(); dl.PopClipRect();                    // extra pop is harmless
        const size_t n = dl.vtx.size();
        dl.AddRectFilled(Vec2(NAN, 0), Vec2(1, 1), MakeCol(1, 1, 1));
        dl.AddLine(Vec2(0, 0), Vec2(INFINITY, 1), MakeCol(1, 1, 1));
        dl.AddText(Vec2(NAN, 0), MakeCol(1, 1, 1), "x");
        dl.AddRectFilled(Vec2(0, 0), Vec2(5, 5), MakeCol(1, 1, 1, 0));           // transparent: nothing
        CHECK(dl.vtx.size() == n);
        for (const DrawCmd& c : dl.cmds) for (unsigned i = c.idxOffset; i < c.idxOffset + c.idxCount; ++i) CHECK(dl.idx[i] < dl.vtx.size());

        DrawData dd; dd.lists.push_back(&dl);
        Bitmap bm(64, 64); bm.Clear(MakeCol(0, 0, 0));
        SoftRender(dd, ctx->font, bm);
        CHECK(Px(bm, 10, 10) == MakeCol(255, 0, 0) && Px(bm, 19, 19) == MakeCol(255, 0, 0));
        CHECK(Px(bm, 9, 10) == MakeCol(0, 0, 0));   // outside: untouched
        CHECK(Px(bm, 20, 10) != MakeCol(255, 0, 0));
        CHECK(CountLit(bm, 10, 10, 20, 20) == 100);
        CHECK(CountLit(bm, 30, 10, 40, 20) == 100);
    }
    {   // fill rule: two triangles sharing a diagonal cover each pixel exactly once (alpha blend would show a double hit)
        DrawList dl; dl.Init(&ctx->font, Rect(0, 0, 64, 64));
        dl.AddRectFilled(Vec2(8, 8), Vec2(40, 24), MakeCol(255, 255, 255, 128), 6);   // rounded: a fan of many triangles
        dl.AddTriangleFilled(Vec2(44, 8), Vec2(60, 8), Vec2(44, 40), MakeCol(255, 255, 255, 128));
        dl.AddTriangleFilled(Vec2(60, 8), Vec2(60, 40), Vec2(44, 40), MakeCol(255, 255, 255, 128));
        DrawData dd; dd.lists.push_back(&dl);
        Bitmap bm(64, 64); bm.Clear(MakeCol(0, 0, 0));
        SoftRender(dd, ctx->font, bm);
        const Col inner = Px(bm, 24, 16);
        bool same = true;
        for (int y = 10; y < 22; ++y) for (int x = 12; x < 36; ++x) if (Px(bm, x, y) != inner) same = false;
        CHECK(same && ColR(inner) == 128);
        same = true;
        for (int y = 12; y < 38; ++y) for (int x = 56; x < 60; ++x) if (Px(bm, x, y) != inner) same = false;   // right strip lies inside the square
        CHECK(same);
        CHECK(Px(bm, 8, 8) == MakeCol(0, 0, 0));       // rounded corner is cut
    }
    {   // text: lit pixel count matches the glyph bitmap; clipping; user texture
        DrawList dl; dl.Init(&ctx->font, Rect(0, 0, 64, 64));
        dl.AddText(Vec2(0, 0), MakeCol(255, 255, 255), "A");
        DrawData dd; dd.lists.push_back(&dl);
        Bitmap bm(64, 64); bm.Clear(MakeCol(0, 0, 0));
        SoftRender(dd, ctx->font, bm);
        int src = 0;
        for (int y = 0; y < 8; ++y) for (int x = 0; x < 8; ++x) if (okay::Font8x8::Pixel('A', x, y)) ++src;
        CHECK(CountLit(bm, 0, 0, 16, 16) == src * 4);
        CHECK(CountLit(bm, 16, 0, 64, 64) == 0);

        DrawList dc; dc.Init(&ctx->font, Rect(0, 0, 64, 64));
        dc.PushClipRect(Rect(0, 0, 8, 64));
        dc.AddText(Vec2(0, 0), MakeCol(255, 255, 255), "AA");
        DrawData d2; d2.lists.push_back(&dc);
        Bitmap b2(64, 64); b2.Clear(MakeCol(0, 0, 0));
        SoftRender(d2, ctx->font, b2);
        CHECK(CountLit(b2, 8, 0, 64, 64) == 0 && CountLit(b2, 0, 0, 8, 16) > 0);

        const unsigned char texels[2 * 2 * 4] = {255,0,0,255, 0,255,0,255, 0,0,255,255, 255,255,255,255};
        std::vector<SoftTexture> texs(1); texs[0].w = 2; texs[0].h = 2; texs[0].rgba = texels;
        DrawList di; di.Init(&ctx->font, Rect(0, 0, 64, 64));
        di.AddImage(1, Vec2(0, 0), Vec2(4, 4));
        di.AddImage(9, Vec2(10, 10), Vec2(14, 14));    // unknown texture id: skipped, no crash
        DrawData d3; d3.lists.push_back(&di);
        Bitmap b3(64, 64); b3.Clear(MakeCol(0, 0, 0));
        SoftRender(d3, ctx->font, b3, &texs);
        CHECK(Px(b3, 0, 0) == MakeCol(255, 0, 0) && Px(b3, 3, 0) == MakeCol(0, 255, 0) && Px(b3, 0, 3) == MakeCol(0, 0, 255) && Px(b3, 3, 3) == MakeCol(255, 255, 255));
        CHECK(CountLit(b3, 10, 10, 14, 14) == 0);
    }
    {   // hostile geometry must not crash or write outside the bitmap
        DrawList dl; dl.Init(&ctx->font, Rect(0, 0, 32, 32));
        dl.vtx.push_back({Vec2(-1e30f, 0), ctx->font.WhiteUV(), MakeCol(255, 0, 0)});
        dl.vtx.push_back({Vec2(1e30f, 1e30f), ctx->font.WhiteUV(), MakeCol(255, 0, 0)});
        dl.vtx.push_back({Vec2(NAN, 3), ctx->font.WhiteUV(), MakeCol(255, 0, 0)});
        dl.vtx.push_back({Vec2(-5000, -5000), ctx->font.WhiteUV(), MakeCol(255, 0, 0)});
        dl.vtx.push_back({Vec2(5000, -5000), ctx->font.WhiteUV(), MakeCol(255, 0, 0)});
        dl.vtx.push_back({Vec2(0, 5000), ctx->font.WhiteUV(), MakeCol(255, 0, 0)});
        for (unsigned i : {0u, 1u, 2u, 3u, 4u, 5u, 7u, 99u}) dl.idx.push_back(i);
        DrawCmd c; c.clip = Rect(0, 0, 32, 32); c.idxCount = 8; dl.cmds.push_back(c);
        DrawCmd c2; c2.clip = Rect(-100, -100, 1e9f, 1e9f); c2.idxOffset = 3; c2.idxCount = 3; dl.cmds.push_back(c2);
        DrawData dd; dd.lists.push_back(&dl); dd.lists.push_back(nullptr);
        Bitmap bm(32, 32);
        SoftRender(dd, ctx->font, bm);
        CHECK(CountLit(bm, 0, 0, 32, 32) > 0);         // the big finite triangle covers the bitmap
        Bitmap empty; SoftRender(dd, ctx->font, empty);
    }

    // ---- ID stack inside a window ------------------------------------------------------------
    {
        Sim sim(ctx);
        std::uint32_t a = 0, b = 0, c = 0, d = 0, e = 0;
        sim.Frame([&] {
            Begin("ids");
            a = GetID("x");
            PushID("scope"); b = GetID("x"); PopID();
            PushID(3); c = GetID("x"); PopID();
            PushID(4); d = GetID("x"); PopID();
            e = GetID("x");
            PopID();                 // unbalanced: counted, not fatal
            End();
        });
        CHECK(a == e && a != b && a != c && b != c && c != d);
        CHECK(ctx->errors == 1);
        CHECK(GetWindowID("ids") != 0);
    }
    ctx->errors = 0;

    // ---- windows: move, resize, collapse, close, z-order -------------------------------------
    {
        Sim sim(ctx);
        bool open = true;
        auto ui = [&] {
            SetNextWindowPos(Vec2(50, 50), Cond_Once); SetNextWindowSize(Vec2(200, 150), Cond_Once);
            if (Begin("A", &open)) Text("hello");
            End();
        };
        sim.Frames(2, ui);
        CHECK(ctx->windows.size() >= 1);
        Window* w = ctx->windows[GetWindowID("A")].get();
        CHECK_NEAR(w->pos.x, 50, 1e-4); CHECK_NEAR(w->size.x, 200, 1e-4);
        // drag the title bar
        sim.Move(150, 60); sim.Frame(ui);
        CHECK(ctx->hoveredWin == w);
        sim.Down(); sim.Frame(ui);
        CHECK(ctx->activeId == HashString("#title", w->id));
        sim.Move(180, 90); sim.Frame(ui);
        CHECK_NEAR(w->pos.x, 80, 1e-3); CHECK_NEAR(w->pos.y, 80, 1e-3);
        sim.Move(200, 100); sim.Frame(ui);
        sim.Up(); sim.Frame(ui);
        CHECK_NEAR(w->pos.x, 100, 1e-3); CHECK_NEAR(w->pos.y, 90, 1e-3);
        CHECK(ctx->activeId == 0);
        sim.Move(300, 300); sim.Frame(ui);     // moving with the button up does nothing
        CHECK_NEAR(w->pos.x, 100, 1e-3);
        // resize with the bottom-right grip
        const Vec2 br = w->pos + w->size;
        sim.Move(br.x - 5, br.y - 5); sim.Frame(ui);
        sim.Down(); sim.Frame(ui);
        sim.Move(br.x + 25, br.y + 15); sim.Frame(ui);
        sim.Up(); sim.Frame(ui);
        CHECK_NEAR(w->size.x, 230, 1e-3); CHECK_NEAR(w->size.y, 170, 1e-3);
        // shrinking stops at the minimum
        const Vec2 br2 = w->pos + w->size;
        sim.Move(br2.x - 5, br2.y - 5); sim.Frame(ui);
        sim.Down(); sim.Frame(ui);
        sim.Move(-500, -500); sim.Frame(ui);
        sim.Up(); sim.Frame(ui);
        CHECK(w->size.x >= 80 && w->size.y >= 50);
        // collapse via the arrow
        const float th = GetContext()->font.lineHeight + 8;
        sim.Click(Vec2(w->pos.x + th * 0.5f, w->pos.y + th * 0.5f), ui);
        CHECK(w->collapsed);
        sim.Frame(ui);
        CHECK(w->prevRect.Height() == th);
        sim.Click(Vec2(w->pos.x + th * 0.5f, w->pos.y + th * 0.5f), ui);
        CHECK(!w->collapsed);
        // close button
        sim.Click(Vec2(w->pos.x + w->size.x - th * 0.5f, w->pos.y + th * 0.5f), ui);
        CHECK(!open);
        sim.Frame(ui);
    }
    {   // z-order: clicking a window behind another brings it to the front; the front one gets the click
        Sim sim(ctx);
        auto ui = [&] {
            SetNextWindowPos(Vec2(20, 20), Cond_Once); SetNextWindowSize(Vec2(200, 200), Cond_Once);
            Begin("back"); Text("b"); End();
            SetNextWindowPos(Vec2(120, 20), Cond_Once); SetNextWindowSize(Vec2(200, 200), Cond_Once);
            Begin("front"); Text("f"); End();
        };
        sim.Frames(2, ui);
        Window* back = ctx->windows[GetWindowID("back")].get();
        Window* front = ctx->windows[GetWindowID("front")].get();
        CHECK(ctx->order.size() == 2 && ctx->order.back() == front);
        sim.Move(60, 100); sim.Frame(ui);
        CHECK(ctx->hoveredRoot == back);
        sim.Move(150, 100); sim.Frame(ui);
        CHECK(ctx->hoveredRoot == front);          // overlap: the front window wins
        sim.Move(60, 100); sim.Frame(ui);
        sim.Down(); sim.Frame(ui);
        CHECK(ctx->order.back() == back);
        sim.Up(); sim.Frame(ui);
        sim.Move(150, 100); sim.Frame(ui);
        CHECK(ctx->hoveredRoot == back);           // now back is in front
        // the draw data follows the same order
        CHECK(ctx->drawData.lists.size() == 2 && ctx->drawData.lists.back() == &back->own);
    }
    {   // frame discipline: unbalanced calls are counted, never crash
        Sim sim(ctx);
        ctx->errors = 0;
        sim.Frame([&] { End(); EndScrollRegion(); EndGroup(); TreePop(); PopStyleColor(); PopID(); Begin("leak"); });
        CHECK(ctx->errors >= 6);
        sim.Frame([&] {});
        const DrawData& dd = Render();       // Render outside a frame
        (void)dd;
        CHECK(ctx->errors >= 7);
    }
    DestroyContext(ctx);
    TEST_MAIN_RESULT();
}
