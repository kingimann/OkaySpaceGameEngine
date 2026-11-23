// Drives the engine's own engine/src/Graphics/Font.cpp, compiled unchanged by okay-cc.
#include "okay/Graphics/Font.hpp"

extern "C" int printf(const char *fmt, ...);

int main() {
    const char *word = "Okay!";
    for (int y = 0; y < okay::Font8x8::Height; ++y) {
        for (const char *p = word; *p; ++p) {
            for (int x = 0; x < okay::Font8x8::Width; ++x) printf("%c", okay::Font8x8::Pixel(*p, x, y) ? '#' : '.');
        }
        printf("\n");
    }
    int sum = 0;
    for (int c = 0; c < 256; ++c) {
        const unsigned char *g = okay::Font8x8::Glyph(static_cast<char>(c));
        for (int r = 0; r < 8; ++r) sum = sum * 31 + g[r];
    }
    printf("sum %d width %d %d %d\n", sum, okay::Font8x8::MeasureWidth(word), okay::Font8x8::MeasureWidth(""),
           okay::Font8x8::MeasureWidth(nullptr));
    printf("edge %d %d %d\n", okay::Font8x8::Pixel('A', -1, 0), okay::Font8x8::Pixel('A', 8, 0),
           okay::Font8x8::Pixel('A', 0, 3));
    return 0;
}
