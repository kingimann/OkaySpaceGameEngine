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

    if (const char* dir = std::getenv("OKAY_SHOT_DIR"); dir && *dir) {   // a picture of what the player draws
        tr->shadow = false; tr->outline = false; tr->color = Color::FromBytes(235, 235, 240);
        tr->text = "Rich world text\n[color=#ffcc33]gold[/color] [size=250][color=#ff5555]BIG[/color][/size] [b]bold[/b]\nthe next line is clear";
        RecordPainter rp;
        ui2d::DrawWorldText(rp, *tr, 16.0f, 16.0f, 3.0f);
        const int w = 520, h = 220;
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
