#include "test_framework.hpp"
#include <Okay.hpp>

#include <cstdint>
#include <cstring>
#include <set>
#include <string>
#include <vector>

using namespace okay;

namespace {
// Draw text with the same cell advance the engine's text paths use (W + 1).
std::vector<std::uint8_t> Render(const std::string& s, int& w, int& h) {
    w = s.empty() ? 0 : (int)(s.size() - 1) * (Font8x8::Width + 1) + Font8x8::Width;
    h = Font8x8::Height;
    std::vector<std::uint8_t> img((size_t)w * h, 0);
    for (size_t i = 0; i < s.size(); ++i)
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < Font8x8::Width; ++x)
                if (Font8x8::Pixel(s[i], x, y)) img[(size_t)y * w + (int)i * (Font8x8::Width + 1) + x] = 1;
    return img;
}
std::uint64_t Fnv(const std::vector<std::uint8_t>& v) {
    std::uint64_t h = 1469598103934665603ull;
    for (auto b : v) { h ^= b; h *= 1099511628211ull; }
    return h;
}
}

int main() {
    RUN_SUITE("bitmap_font");

    // Every printable glyph is non-empty (except space) and pairwise distinct.
    {
        std::set<std::string> seen;
        int blank = 0, dup = 0;
        for (int c = 32; c <= 126; ++c) {
            const unsigned char* g = Font8x8::Glyph((char)c);
            bool ink = false;
            for (int i = 0; i < 8; ++i) if (g[i]) ink = true;
            if (c == ' ') CHECK(!ink);
            else if (!ink) ++blank;
            // Column 7 is the inter-glyph gap and stays empty.
            for (int i = 0; i < 8; ++i) CHECK((g[i] & 0x80) == 0);
            if (!seen.insert(std::string((const char*)g, 8)).second) ++dup;
        }
        CHECK(blank == 0);
        CHECK(dup == 0);
        CHECK(seen.size() == 95);
    }

    // Non-printable / non-ASCII input is blank.
    {
        for (int c = 0; c < 32; ++c) for (int i = 0; i < 8; ++i) CHECK(Font8x8::Glyph((char)c)[i] == 0);
        for (int c = 127; c < 256; ++c) for (int i = 0; i < 8; ++i) CHECK(Font8x8::Glyph((char)c)[i] == 0);
    }

    // Spot-check shapes: 'T' has a full top bar and a centre stem; '.' is bottom-heavy.
    {
        for (int x = 0; x < 7; ++x) CHECK(Font8x8::Pixel('T', x, 0));
        for (int y = 1; y <= 6; ++y) CHECK(Font8x8::Pixel('T', 3, y) && !Font8x8::Pixel('T', 0, y));
        CHECK(!Font8x8::Pixel('T', 3, 7));
        CHECK(!Font8x8::Pixel('.', 3, 0) && Font8x8::Pixel('.', 3, 6));
        CHECK(Font8x8::Pixel('g', 3, 7));      // descender row
        CHECK(!Font8x8::Pixel('a', 3, 0));     // lowercase x-height leaves the top rows clear
        // 0 and O are intentionally distinct (slashed zero).
        bool differ = false;
        for (int i = 0; i < 8; ++i) if (Font8x8::Glyph('0')[i] != Font8x8::Glyph('O')[i]) differ = true;
        CHECK(differ);
    }

    // Render test: deterministic, non-blank, expected size.
    {
        const std::string s = "The quick brown fox 0123456789";
        int w = 0, h = 0, w2 = 0, h2 = 0;
        auto a = Render(s, w, h);
        auto b = Render(s, w2, h2);
        CHECK(w == (int)(s.size() - 1) * 9 + 8);
        CHECK(h == 8);
        CHECK(w == w2 && h == h2 && a == b);
        size_t on = 0;
        for (auto v : a) on += v;
        CHECK(on > 150 && on < a.size() / 2);
        CHECK(Fnv(a) == 0xe2ac3239e7d2d08dull);
        // 'T' starts the string: its top bar occupies x 0..6 of row 0; the gap column is empty.
        for (int x = 0; x < 7; ++x) CHECK(a[x] == 1);
        CHECK(a[7] == 0 && a[8] == 0);
        // Space (index 3 -> after "The") leaves its whole cell empty.
        for (int y = 0; y < 8; ++y) for (int x = 0; x < 9; ++x) CHECK(a[(size_t)y * w + 3 * 9 + x] == 0);
    }

    // Callers keep the same layout: metrics are unchanged.
    {
        CHECK(Font8x8::Width == 8 && Font8x8::Height == 8);
        CHECK(Font8x8::MeasureWidth("The quick brown fox 0123456789") == 30 * 8);
        Scene scene("T");
        auto* tr = scene.CreateGameObject("L")->AddComponent<TextRenderer>();
        tr->text = "The quick brown fox 0123456789";
        CHECK(tr->PixelWidth() == 29 * 9 + 8);
        CHECK(tr->PixelHeight() == 8);
    }

    TEST_MAIN_RESULT();
}
