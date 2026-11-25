// Drives the engine's own engine/src/Texture/Bc.cpp, compiled unchanged by okay-cc (round 20:
// <cctype> for the format names, std::string, BC1 to BC5 block encode and decode with integer and
// float math). Prints a checksum of each encoded image and of its decoded pixels.
#include "okay/Texture/Bc.hpp"

extern "C" int printf(const char *fmt, ...);

static unsigned sum(const std::vector<std::uint8_t> &v) {
    unsigned h = 2166136261u;
    for (unsigned long i = 0; i < v.size(); ++i) h = (h ^ v[i]) * 16777619u;
    return h;
}

int main() {
    const int w = 12, h = 8;
    std::vector<std::uint8_t> img((unsigned long)(w * h * 4));
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            std::uint8_t *p = &img[(unsigned long)((y * w + x) * 4)];
            p[0] = (std::uint8_t)(x * 21);
            p[1] = (std::uint8_t)(y * 31 + x * 3);
            p[2] = (std::uint8_t)((x * y * 7) & 255);
            p[3] = (std::uint8_t)(x < 6 ? 255 : y * 30);
        }
    const char *names[] = {"bc1", "BC2", "Bc3", "bc4", "bc5", "bc7", ""};
    for (const char *n : names) {
        okay::tex::BcFormat f = okay::tex::BcFormat::BC1;
        bool ok = okay::tex::ParseBcFormat(n, &f);
        printf("%s -> %d %d\n", n, ok, ok ? (int)f : -1);
    }
    okay::tex::BcFormat all[] = {okay::tex::BcFormat::BC1, okay::tex::BcFormat::BC2, okay::tex::BcFormat::BC3, okay::tex::BcFormat::BC4,
                            okay::tex::BcFormat::BC5};
    for (okay::tex::BcFormat f : all) {
        for (int q = 0; q <= 2; ++q) {
            okay::tex::BcEncodeOptions opt;
            opt.quality = q;
            opt.bc1AlphaCutoff = q == 2 ? 128 : 0;
            std::vector<std::uint8_t> enc = okay::tex::EncodeBc(img.data(), w, h, f, opt);
            std::vector<std::uint8_t> dec = okay::tex::DecodeBc(enc.data(), enc.size(), w, h, f);
            printf("%s q%d bytes %d (%d) bpp %.2f enc %08x dec %08x size %d\n", okay::tex::BcName(f), q, (int)enc.size(),
                   (int)okay::tex::BcImageBytes(f, w, h), okay::tex::BcBytesPerPixel(f), sum(enc), sum(dec), (int)dec.size());
        }
    }
    printf("bad %d %d\n", (int)okay::tex::EncodeBc(nullptr, 4, 4, okay::tex::BcFormat::BC1).size(),
           (int)okay::tex::BcImageBytes(okay::tex::BcFormat::BC3, 0, 4));
    return 0;
}
