// Rich text on world text (TextRenderer with screenSpace off) and the [size=N] tag: the shared parser
// (okay/UI/RichText.hpp) reads the size tag, RichTextRuns splits lines into styled runs, and ui2d::DrawWorldText (the
// player's 2D world-text pass) draws each run in its colour and size, with the tags taking no room. Written for this
// engine from its own rich-text rules; no other project's code was used.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/UI/RichText.hpp"
#include "okay/UI/UIRenderer.hpp"
#include <chrono>
#include <climits>
#include <string>
#include <cstdlib>
#include "okay/Graphics/Image.hpp"
#include <cstdint>
#include <vector>

using namespace okay;

namespace {
// Records every filled rectangle with its colour (the 8x8 font draws glyphs as filled rectangles).
struct RecordPainter : ui2d::Painter {
    using URect = ui2d::Rect;
    using Tex = ui2d::Tex;
    struct Fill { int x, y, w, h; std::uint8_t r, g, b; };
    std::vector<Fill> fills;
    std::uint8_t cr = 0, cg = 0, cb = 0;
    bool clip = false; URect clipRect{0, 0, 0, 0};
    void SetDrawColor(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t) override { cr = r; cg = g; cb = b; }
    void FillRect(const URect* r) override { if (r) fills.push_back({r->x, r->y, r->w, r->h, cr, cg, cb}); }
    void DrawRect(const URect*) override {}
    void DrawLine(int, int, int, int) override {}
    void DrawPoint(int, int) override {}
    void SetClipRect(const URect* r) override { clip = r != nullptr; if (r) clipRect = *r; }
    void GetClipRect(URect* r) override { *r = clipRect; }
    bool IsClipEnabled() override { return clip; }
    void SetScale(float, float) override {}
    Tex LoadTexture(const std::string&) override { return nullptr; }
    Tex FontAtlas(TtfFont*) override { return nullptr; }
    void QueryTexture(Tex, int* w, int* h) override { *w = *h = 0; }
    void SetTextureColorMod(Tex, std::uint8_t, std::uint8_t, std::uint8_t) override {}
    void SetTextureAlphaMod(Tex, std::uint8_t) override {}
    void Copy(Tex, const URect*, const URect*) override {}
    void CopyF(Tex, const URect*, const ui2d::FRect*) override {}
    void CopyEx(Tex, const URect*, const URect*, double, const ui2d::Point*, int) override {}
    void CopyExF(Tex, const URect*, const ui2d::FRect*, double, const ui2d::FPoint*, int) override {}
    void Geometry(Tex, const ui2d::Vertex*, int, const int*, int) override {}
    Tex UploadAtlas(const Image&, int, Tex existing) override { return existing; }
    struct Box { int n = 0, x0 = INT_MAX, y0 = INT_MAX, x1 = INT_MIN, y1 = INT_MIN; };
    Box Of(std::uint8_t r, std::uint8_t g, std::uint8_t b) const {
        Box bx;
        for (const Fill& f : fills) if (f.r == r && f.g == g && f.b == b) {
            ++bx.n;
            bx.x0 = std::min(bx.x0, f.x); bx.y0 = std::min(bx.y0, f.y);
            bx.x1 = std::max(bx.x1, f.x + f.w); bx.y1 = std::max(bx.y1, f.y + f.h);
        }
        return bx;
    }
};
} // namespace

int main() {
    RUN_SUITE("world_text_rich");

    {   // the size tag in the shared parser
        RichText t = ParseRichText("a[size=150]b[/size]c");
        CHECK(t.plain == "abc");
        CHECK(t.styles.size() == 3);
        if (t.styles.size() == 3) {
            CHECK_NEAR(t.styles[0].size, 1.0f, 1e-6f);
            CHECK_NEAR(t.styles[1].size, 1.5f, 1e-6f);
            CHECK_NEAR(t.styles[2].size, 1.0f, 1e-6f);
        }
        RichText n = ParseRichText("[size=200][size=50]x[/size]y[/size]");   // nested sizes multiply
        CHECK(n.plain == "xy" && n.styles.size() == 2);
        if (n.styles.size() == 2) { CHECK_NEAR(n.styles[0].size, 1.0f, 1e-6f); CHECK_NEAR(n.styles[1].size, 2.0f, 1e-6f); }
        for (const char* bad : {"[size=10]x", "[size=401]x", "[size=abc]x", "[size=]x", "[size=1000]x", "[size=-50]x"})
            CHECK(StripRichText(bad) == std::string(bad));                  // not a tag: shown as text
        CHECK(StripRichText("[/size]x") == "[/size]x");                      // nothing open: text
        CHECK(StripRichText("[size=150][b]x[/size][/b]") == "x");           // crossing tags still resolve
        // nested sizes are held to 25..400 percent
        std::string deep;
        for (int k = 0; k < 10; ++k) deep += "[size=400]";
        RichText big = ParseRichText(deep + "x");
        CHECK(big.styles.size() == 1 && big.styles[0].size == kRichTextMaxSize);
        std::string tiny;
        for (int k = 0; k < 10; ++k) tiny += "[size=25]";
        RichText small = ParseRichText(tiny + "x");
        CHECK(small.styles.size() == 1 && small.styles[0].size == kRichTextMinSize);
        // at most kRichTextMaxDepth tags open: a deeper one is shown as text
        std::string many;
        for (int k = 0; k < kRichTextMaxDepth + 3; ++k) many += "[b]";
        CHECK(StripRichText(many + "x") == "[b][b][b]x");
    }

    {   // hostile text stays cheap: over the length limit nothing is parsed; inside it, work is bounded
        const auto t0 = std::chrono::steady_clock::now();
        std::string huge;
        huge.reserve(1u << 20);
        while (huge.size() < (1u << 20)) huge += "[b][size=400][color=red]a[/b";
        RichText h = ParseRichText(huge);
        CHECK(h.plain == huge);                                              // over the limit: shown as written
        std::string edge;
        while (edge.size() + 3 <= kRichTextMaxBytes) edge += "[b]";
        for (int k = 0; k < 200; ++k) { RichText e = ParseRichText(edge); (void)e; RichTextRuns(e); }
        std::string flips;                                                   // a style change every character
        while (flips.size() + 20 <= kRichTextMaxBytes) flips += "[color=red]a[/color]b";
        for (int k = 0; k < 200; ++k) { RichText f = ParseRichText(flips); CHECK(!RichTextRuns(f).empty()); }
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        std::printf("  1 MiB hostile string plus 400 parses at the limit: %.1f ms\n", ms);
        CHECK(ms < 2000.0);
    }

    {   // runs: one style each, spaces join the run they sit in, one list per line
        const std::vector<RichRun> runs = RichTextRuns(ParseRichText("ab [color=red]cd[/color] e\n[size=200]f[/size]g"));
        CHECK(runs.size() == 5);
        if (runs.size() == 5) {
            CHECK(runs[0].line == 0 && runs[0].text == "ab " && !runs[0].style.hasColor && runs[0].before.empty());
            CHECK(runs[1].line == 0 && runs[1].text == "cd " && runs[1].style.hasColor && runs[1].before.empty());
            CHECK(runs[2].line == 0 && runs[2].text == "e" && !runs[2].style.hasColor);
            CHECK(runs[3].line == 1 && runs[3].text == "f" && runs[3].style.size == 2.0f && runs[3].before.empty());
            CHECK(runs[4].line == 1 && runs[4].text == "g" && runs[4].style.size == 1.0f && runs[4].before.empty());
        }
    }

    Scene s("world text");
    auto* tr = s.CreateGameObject("Sign")->AddComponent<TextRenderer>();
    tr->screenSpace = false;
    tr->color = Color::FromBytes(0, 255, 0);
    tr->shadow = false; tr->outline = false;

    {   // world text reads the tags: colours by run, tags take no room
        tr->text = "[color=#ff0000]AB[/color]CD"; tr->richText = true;
        RecordPainter rp;
        ui2d::DrawWorldText(rp, *tr, 100.0f, 100.0f, 2.0f);
        const auto red = rp.Of(255, 0, 0), green = rp.Of(0, 255, 0);
        CHECK(red.n > 0 && green.n > 0);
        CHECK(red.x0 >= 100 && red.x1 <= green.x0 + 1);                    // AB first, then CD right after it
        CHECK(green.x1 - 100 <= 4 * 9 * 2 + 2);                             // four glyphs wide: the tags took no room
        CHECK(tr->DisplayText() == "ABCD");                                 // and the plain text is tag-free
        // off: the tags are ordinary text, all in the label colour (old scenes keep their look)
        tr->richText = false;
        RecordPainter plain;
        ui2d::DrawWorldText(plain, *tr, 100.0f, 100.0f, 2.0f);
        CHECK(plain.Of(255, 0, 0).n == 0);
        CHECK(plain.Of(0, 255, 0).x1 - 100 > 20 * 2 * 8);                  // the tag characters were drawn
    }

    {   // size: a bigger run is taller, sits on the same bottom line, and pushes the rest right
        tr->richText = true;
        tr->text = "[color=#ff0000]A[/color]B";
        RecordPainter one;
        ui2d::DrawWorldText(one, *tr, 0.0f, 100.0f, 2.0f);
        tr->text = "[size=200][color=#ff0000]A[/color][/size]B";
        RecordPainter big;
        ui2d::DrawWorldText(big, *tr, 0.0f, 100.0f, 2.0f);
        const auto a1 = one.Of(255, 0, 0), a2 = big.Of(255, 0, 0), b1 = one.Of(0, 255, 0), b2 = big.Of(0, 255, 0);
        CHECK(a1.n > 0 && a2.n > 0 && b1.n > 0 && b2.n > 0);
        const int h1 = a1.y1 - a1.y0, h2 = a2.y1 - a2.y0;
        std::printf("  A height: normal %d, [size=200] %d; B starts at %d then %d\n", h1, h2, b1.x0, b2.x0);
        CHECK(h2 >= 2 * h1 - 2);                                            // twice as tall
        CHECK(std::abs(a2.y1 - b2.y1) <= 2);                                // same bottom line as B
        CHECK(b2.x0 > b1.x0 + 8);                                           // B moved right by the wider A
        CHECK(std::abs((b2.y1 - b2.y0) - (b1.y1 - b1.y0)) <= 1);            // B itself kept its size
    }

    {   // review: a line with a bigger run is moved down so the run does not overlap the line above, and a big run on
        // the first line does not reach above the text's top (before, sized runs grew upward over the line above)
        tr->shadow = false; tr->outline = false;
        tr->text = "AB\n[size=300][color=#ff0000]C[/color][/size]D";
        RecordPainter two;
        ui2d::DrawWorldText(two, *tr, 0.0f, 100.0f, 2.0f);
        const auto green = two.Of(0, 255, 0), red = two.Of(255, 0, 0);
        int line1Bottom = INT_MIN;
        for (const auto& f : two.fills) if (f.r == 0 && f.g == 255 && f.b == 0 && f.y < 100 + 9 * 2) line1Bottom = std::max(line1Bottom, f.y + f.h);
        std::printf("  line 1 bottom %d, big C top %d\n", line1Bottom, red.y0);
        CHECK(green.n > 0 && red.n > 0);
        CHECK(red.y0 >= line1Bottom);                                       // no overlap with the line above
        tr->text = "[size=300][color=#ff0000]A[/color][/size]B";
        RecordPainter first;
        ui2d::DrawWorldText(first, *tr, 0.0f, 100.0f, 2.0f);
        CHECK(first.Of(255, 0, 0).y0 >= 100);                               // not above the text's own top
        // Still one bottom line: the character cells end together. The visible ink differs by the glyph's empty bottom
        // row, which is 3 x 2 = 6 px tall at size 300 against 2 px at size 100, so allow those 4 px.
        CHECK(std::abs(first.Of(255, 0, 0).y1 - first.Of(0, 255, 0).y1) <= 4);
    }

    {   // shadow and outline follow the runs and keep their own colours
        tr->text = "[color=#ff0000]A[/color]B";
        tr->shadow = true; tr->shadowColor = Color::FromBytes(0, 0, 255);
        tr->outline = true; tr->outlineColor = Color::FromBytes(255, 255, 0);
        RecordPainter rp;
        ui2d::DrawWorldText(rp, *tr, 50.0f, 50.0f, 2.0f);
        CHECK(rp.Of(0, 0, 255).n > 0);                                      // shadow drawn
        CHECK(rp.Of(255, 255, 0).n > 0);                                    // outline drawn in its colour
        CHECK(rp.Of(255, 0, 0).n > 0 && rp.Of(0, 255, 0).n > 0);
    }

    {   // a line is as tall as the largest [size] on it: the pure rules
        const RichText rt = ParseRichText("a\n[size=200]b[/size] c\n[size=50]d[/size]\n\n[size=150]e[/size][size=300]f[/size]");
        const std::vector<float> sc = RichLineScales(rt.plain, rt.styles);
        CHECK(sc.size() == 5);                                                  // one per line
        if (sc.size() == 5) {
            CHECK_NEAR(sc[0], 1.0f, 1e-6f);                                      // plain
            CHECK_NEAR(sc[1], 2.0f, 1e-6f);                                      // the big run sets the line (the space is no glyph)
            CHECK_NEAR(sc[2], 0.5f, 1e-6f);                                      // a line of only small text is a small line
            CHECK_NEAR(sc[3], 1.0f, 1e-6f);                                      // an empty line
            CHECK_NEAR(sc[4], 3.0f, 1e-6f);                                      // the largest of two runs
        }
        CHECK(RichLineScales("", {}).size() == 1 && RichLineScales("", {})[0] == 1.0f);
        CHECK(RichLineScales("x\n", {}).size() == 2);                            // a trailing line break starts an empty line
        CHECK(RichLineAdvance(9.0f, 0.0f, 1.0f) == 9.0f);                        // scale 1: exactly the plain advance (old text unchanged)
        CHECK(RichLineAdvance(10.0f, 1.0f, 1.0f) == 10.0f);
        CHECK_NEAR(RichLineAdvance(9.0f, 0.0f, 2.0f), 18.0f, 1e-5f);             // glyphs and gap grow with the size
        CHECK_NEAR(RichLineAdvance(10.0f, 1.0f, 2.0f), 19.0f, 1e-5f);            // the Line Spacing setting does not
        const std::vector<RichRun> runs = RichTextRuns(ParseRichText("a\n[size=200]b[/size]\nc"));
        CHECK(runs.size() == 3);
        if (runs.size() == 3) CHECK(runs[0].lineScale == 1.0f && runs[1].lineScale == 2.0f && runs[2].lineScale == 1.0f);
    }

    {   // drawn: a big run no longer climbs into the line above, and the line under it starts below it
        tr->shadow = false; tr->outline = false; tr->bold = false; tr->italic = false; tr->richText = true;
        const float f = 2.0f;                                                    // screen pixels per font pixel
        // the same letters in every line, a colour each, so the boxes can be told apart
        tr->text = "[color=#ff0000]BBB[/color]\n[size=200][color=#00ff00]BBB[/color][/size]\n[color=#0000ff]BBB[/color]";
        RecordPainter rp;
        ui2d::DrawWorldText(rp, *tr, 100.0f, 100.0f, f);
        const auto l1 = rp.Of(255, 0, 0), l2 = rp.Of(0, 255, 0), l3 = rp.Of(0, 0, 255);
        CHECK(l1.n > 0 && l2.n > 0 && l3.n > 0);
        std::printf("  lines: y %d..%d, %d..%d, %d..%d (big line is %.2f times the height of line 1)\n", l1.y0, l1.y1, l2.y0, l2.y1, l3.y0, l3.y1,
                    (double)(l2.y1 - l2.y0) / (double)std::max(1, l1.y1 - l1.y0));
        CHECK(l2.y0 >= l1.y1);                                                   // the big line starts under the first one (it used to start 8 font px above its place)
        CHECK(l3.y0 >= l2.y1);                                                   // and the last line under the big one
        CHECK(l1.y0 >= 100);                                                     // nothing above the text's top
        CHECK(std::abs((l2.y1 - l2.y0) - 2 * (l1.y1 - l1.y0)) <= 2);             // the big run is twice as tall
        // the pitch: line 1's top to line 2's top is one plain advance (9 font px) plus what the big letters' blank top rows add (none:
        // they start on the line's top), line 2's top to line 3's top is two advances (18 font px)
        const int pitch12 = l2.y0 - l1.y0, pitch23 = l3.y0 - l2.y0;
        CHECK(std::abs(pitch12 - (int)std::lround(9.0f * f)) <= 2);
        CHECK(std::abs(pitch23 - (int)std::lround(18.0f * f)) <= 2);
        // the same text with no [size] lays out as before, line for line (rich on and off agree)
        tr->text = "[color=#ff0000]BBB[/color]\n[color=#00ff00]BBB[/color]\n[color=#0000ff]BBB[/color]";
        RecordPainter on;
        ui2d::DrawWorldText(on, *tr, 100.0f, 100.0f, f);
        tr->richText = false; tr->text = "BBB\nBBB\nBBB"; tr->color = Color::FromBytes(255, 0, 0);
        RecordPainter off;
        ui2d::DrawWorldText(off, *tr, 100.0f, 100.0f, f);
        CHECK(on.Of(255, 0, 0).y0 == off.Of(255, 0, 0).y0);                      // line 1 at the same place
        CHECK(on.Of(0, 255, 0).y0 - on.Of(255, 0, 0).y0 == (int)std::lround(9.0f * f));   // the plain pitch
        CHECK(on.Of(0, 0, 255).y0 - on.Of(0, 255, 0).y0 == (int)std::lround(9.0f * f));
        tr->richText = true; tr->color = Color::FromBytes(0, 255, 0);
        // a small-only line is a small line: the last line below it moves up
        tr->text = "[color=#ff0000]BBB[/color]\n[size=50][color=#00ff00]BBB[/color][/size]\n[color=#0000ff]BBB[/color]";
        RecordPainter small;
        ui2d::DrawWorldText(small, *tr, 100.0f, 100.0f, f);
        const auto s1 = small.Of(255, 0, 0), s2 = small.Of(0, 255, 0), s3 = small.Of(0, 0, 255);
        CHECK(s2.y0 >= s1.y1 && s3.y0 >= s2.y1);
        CHECK(std::abs((s3.y0 - s2.y0) - (int)std::lround(4.5f * f)) <= 2);       // half a line (4.5 font px) under the small one
    }

    {   // the measured height counts it too (centring and Best Fit see the real block)
        tr->screenSpace = false; tr->richText = true; tr->shadow = false;
        tr->text = "a\n[size=200]b[/size]\nc";
        const int plain = 2 * 9 + 8;                                             // three lines at size 1: (3 - 1) * 9 + 8
        CHECK(tr->PixelHeight() == 9 * 1 + 9 * 2 + 8 * 1);                       // 9 + 18 + 8 = 35
        CHECK(tr->PixelHeight() > plain);
        tr->richText = false;
        CHECK(tr->PixelHeight() == plain);                                       // rich off: three plain lines, the tags are ordinary text
        tr->richText = true; tr->lineSpacing = 1.0f;
        CHECK(tr->PixelHeight() == (int)std::ceil(10.0f * 1 + (9.0f * 2 + 1.0f) + 8.0f));   // spacing is added, never multiplied
        tr->lineSpacing = 0.0f;
        tr->text = "x\ny";
        CHECK(tr->PixelHeight() == 9 + 8);                                       // no [size]: the old height, exactly
    }

    if (const char* dir = std::getenv("OKAY_SHOT_DIR"); dir && *dir) {   // a picture of what the player draws
        tr->shadow = false; tr->outline = false; tr->color = Color::FromBytes(235, 235, 240);
        tr->text = "Rich world text\n[color=#ffcc33]gold[/color] [size=250][color=#ff5555]BIG[/color][/size] [b]bold[/b]\nthe next line is clear";
        RecordPainter rp;
        ui2d::DrawWorldText(rp, *tr, 16.0f, 16.0f, 3.0f);
        const int w = 700, h = 170;
        std::vector<std::uint8_t> px((std::size_t)w * h * 4, 0);
        for (std::size_t i = 0; i < px.size(); i += 4) { px[i] = 30; px[i + 1] = 34; px[i + 2] = 44; px[i + 3] = 255; }
        for (const auto& f : rp.fills)
            for (int y = std::max(0, f.y); y < std::min(h, f.y + f.h); ++y)
                for (int x = std::max(0, f.x); x < std::min(w, f.x + f.w); ++x) {
                    std::uint8_t* p = &px[((std::size_t)y * w + x) * 4];
                    p[0] = f.r; p[1] = f.g; p[2] = f.b; p[3] = 255;
                }
        Image img;
        img.Set(w, h, std::move(px));
        img.SavePNG(std::string(dir) + "/world_text_rich.png");
    }
    TEST_MAIN_RESULT();
}
