// Our own WebP decoder (Core/ImageDecodeWebp.cpp, from RFC 9649 and RFC 6386). Every test file is made here by the
// test-only encoders in webp_test_encoder.hpp; no downloaded image is used.
//  - lossless: every transform, palettes of 1 to 256 colours (all pixel bundlings), colour caches of 1 to 11 bits,
//    LZ77 with short and long distance codes, meta prefix codes, simple and normal code length codes, max_symbol;
//    the decode must give back exactly the source pixels;
//  - lossy: every intra mode, segments (delta and absolute), 1 to 8 token partitions, skipped macroblocks,
//    probability updates, quantiser deltas; with the loop filter off the decoded planes must equal the encoder's own
//    reconstruction bit for bit; with it on (normal and simple filter, sharpness, per-segment levels and deltas) the
//    planes must match pinned hashes, which were checked once against an independent VP8 decoder (ffmpeg's), and stay
//    close to the source picture;
//  - alpha (ALPH: raw and lossless-compressed, filters 0 to 3), the extended format, the first frame of an animation;
//  - header-only queries and malformed files (each must fail cleanly).
// OKAY_WEBP_DUMP_DIR=<dir> writes the lossless and lossy test files there (to check them with another decoder or fuzz them).
#include "test_framework.hpp"
#include "webp_test_encoder.hpp"
#include "okay/Core/ImageDecode.hpp"
#include "okay/Core/StringId.hpp"   // FNV-1a over the decoded planes
#include "ImageDecodeInternal.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

using namespace webptest;
using okay::DecodedImage;

namespace {

std::uint64_t Fnv(const Bytes& b) { return okay::StringId::HashOf(std::string_view(reinterpret_cast<const char*>(b.data()), b.size())); }

std::vector<std::uint32_t> TestArgb(int w, int h, std::uint64_t seed, int colours = 0) {
    Rng r(seed);
    std::vector<std::uint32_t> pal;
    for (int i = 0; i < colours; ++i) pal.push_back(r.Next() | (i % 3 == 0 ? 0xFF000000u : 0u));
    std::vector<std::uint32_t> px(std::size_t(w) * std::size_t(h));
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            std::uint32_t p;
            if (colours) p = pal[std::size_t((x / 3 + y * 2 + (r.Below(7) == 0 ? r.Below(colours) : 0)) % colours)];
            else {
                const int a = (x * 7 + y * 3) % 256, rr = (x * 255) / std::max(1, w - 1), g = (y * 255) / std::max(1, h - 1);
                const int b = ((x / 4 + y / 4) % 2) ? 200 : r.Below(256);
                p = (std::uint32_t(a) << 24) | (std::uint32_t(rr) << 16) | (std::uint32_t(g) << 8) | std::uint32_t(b);
                if (r.Below(5) == 0 && x > 0) p = px[std::size_t(y) * std::size_t(w) + std::size_t(x) - 1];   // runs for LZ77
            }
            px[std::size_t(y) * std::size_t(w) + std::size_t(x)] = p;
        }
    return px;
}

bool SameArgb(const DecodedImage& img, const std::vector<std::uint32_t>& px, int w, int h) {
    if (img.width != w || img.height != h || img.rgba.size() != px.size() * 4) return false;
    for (std::size_t i = 0; i < px.size(); ++i) {
        const std::uint8_t* o = &img.rgba[i * 4];
        const std::uint32_t p = px[i];
        if (o[0] != ((p >> 16) & 255) || o[1] != ((p >> 8) & 255) || o[2] != (p & 255) || o[3] != (p >> 24)) return false;
    }
    return true;
}

Bytes LosslessFile(const std::vector<std::uint32_t>& px, int w, int h, const LLOptions& o) {
    Bytes chunks;
    AddChunk(chunks, "VP8L", EncodeVp8l(px, w, h, o));
    return Riff(chunks);
}

Bytes Vp8xHeader(int w, int h, bool alpha, bool anim) {
    Bytes p;
    p.push_back(std::uint8_t((alpha ? 0x10 : 0) | (anim ? 0x02 : 0)));
    p.insert(p.end(), {0, 0, 0});
    PutLe(p, std::uint32_t(w - 1), 3);
    PutLe(p, std::uint32_t(h - 1), 3);
    Bytes c;
    AddChunk(c, "VP8X", p);
    return c;
}

double Psnr(const Bytes& a, const Bytes& b) {
    double se = 0;
    for (std::size_t i = 0; i < a.size(); ++i) { const double d = double(a[i]) - double(b[i]); se += d * d; }
    if (se == 0) return 99;
    return 10 * std::log10(255.0 * 255.0 * double(a.size()) / se);
}

void Dump(const std::string& name, const Bytes& file) {
    const char* dir = std::getenv("OKAY_WEBP_DUMP_DIR");
    if (!dir || !*dir) return;
    std::ofstream f(std::string(dir) + "/" + name, std::ios::binary);
    f.write(reinterpret_cast<const char*>(file.data()), std::streamsize(file.size()));
}

bool Decode(const Bytes& f, DecodedImage& img, std::string* err = nullptr) { return okay::DecodeImage(f.data(), f.size(), img, err); }

} // namespace

int main() {
    RUN_SUITE("image_webp");

    // ---- lossless round trips ----------------------------------------------------------------------------------
    {
        struct Case { int w, h, colours; LLOptions o; const char* name; };
        std::vector<Case> cases;
        auto add = [&](int w, int h, int colours, const char* name, auto&& set) { LLOptions o; set(o); cases.push_back({w, h, colours, o, name}); };
        add(1, 1, 0, "1x1 plain", [](LLOptions&) {});
        add(17, 9, 0, "plain", [](LLOptions&) {});
        add(17, 9, 0, "no simple codes", [](LLOptions& o) { o.simpleCodes = false; });
        add(33, 21, 0, "subtract green", [](LLOptions& o) { o.subtractGreen = true; });
        add(33, 21, 0, "predictor, 14 modes", [](LLOptions& o) { o.predictor = true; o.predBits = 2; });
        add(64, 40, 0, "predictor bits 9", [](LLOptions& o) { o.predictor = true; o.predBits = 9; });
        add(33, 21, 0, "cross colour", [](LLOptions& o) { o.crossColor = true; o.colorBits = 2; });
        add(40, 30, 0, "all transforms", [](LLOptions& o) { o.subtractGreen = o.predictor = o.crossColor = true; o.predBits = 3; o.colorBits = 4; });
        for (int cb = 1; cb <= 11; ++cb) add(29, 13, 0, "colour cache", [cb](LLOptions& o) { o.cacheBits = cb; o.seed = std::uint64_t(cb); });
        add(70, 31, 0, "lz77", [](LLOptions& o) { o.lz77 = true; });
        add(70, 31, 0, "lz77 + cache + max_symbol", [](LLOptions& o) { o.lz77 = true; o.cacheBits = 6; o.maxSymbol = true; });
        add(70, 31, 0, "meta prefix codes", [](LLOptions& o) { o.metaBits = 2; o.groups = 5; o.lz77 = true; });
        add(70, 31, 0, "meta codes 1 group", [](LLOptions& o) { o.metaBits = 3; o.groups = 1; });
        for (int n : {1, 2, 3, 4, 5, 16, 17, 200, 256})
            add(37, 11, n, "palette", [n](LLOptions& o) { o.palette = true; o.seed = std::uint64_t(n); });
        add(37, 11, 9, "palette + predictor + lz77", [](LLOptions& o) { o.palette = o.predictor = o.lz77 = true; o.predBits = 2; });
        add(150, 3, 0, "everything", [](LLOptions& o) {
            o.subtractGreen = o.predictor = o.crossColor = o.lz77 = o.maxSymbol = true; o.cacheBits = 10; o.metaBits = 4; o.groups = 7;
        });
        int ok = 0, n = 0;
        for (const Case& c : cases) {
            const std::vector<std::uint32_t> px = TestArgb(c.w, c.h, std::uint64_t(c.w * 131 + c.h), c.colours);
            const Bytes f = LosslessFile(px, c.w, c.h, c.o);
            char nm[64];
            std::snprintf(nm, sizeof(nm), "lossless_%02d.webp", n++);
            Dump(nm, f);
            DecodedImage img;
            std::string err;
            const bool good = Decode(f, img, &err) && SameArgb(img, px, c.w, c.h);
            if (!good) std::fprintf(stderr, "  lossless case '%s' %dx%d (%d colours): %s\n", c.name, c.w, c.h, c.colours, err.c_str());
            CHECK(good);
            ok += good;
            int w = 0, h = 0;
            okay::ImageFormat fmt = okay::ImageFormat::Unknown;
            CHECK(okay::GetImageInfo(f.data(), f.size(), &w, &h, &fmt) && w == c.w && h == c.h && fmt == okay::ImageFormat::WEBP);
        }
        std::printf("  lossless: %d of %zu round trips exact\n", ok, cases.size());
    }

    // ---- lossy ----------------------------------------------------------------------------------------------------
    {
        struct Case { int w, h; LossyOptions o; const char* name; std::uint64_t pinned; };
        std::vector<Case> cases;
        auto add = [&](int w, int h, const char* name, std::uint64_t pin, auto&& set) { LossyOptions o; set(o); cases.push_back({w, h, o, name, pin}); };
        // filter off: exact against the encoder's reconstruction (pin 0 = not pinned)
        add(16, 16, "one macroblock", 0, [](LossyOptions& o) { o.q = 10; });
        add(1, 1, "1x1", 0, [](LossyOptions& o) { o.q = 30; });
        add(53, 37, "all modes", 0, [](LossyOptions& o) { o.q = 20; });
        add(53, 37, "B_PRED only", 0, [](LossyOptions& o) { o.q = 5; o.modes = 1; });
        add(64, 48, "16x16 modes, high q", 0, [](LossyOptions& o) { o.q = 120; o.modes = 2; });
        add(64, 48, "q 0, no skip", 0, [](LossyOptions& o) { o.q = 0; o.skip = false; });
        add(80, 40, "8 partitions + prob updates", 0, [](LossyOptions& o) { o.log2Parts = 3; o.probUpdates = true; o.q = 25; });
        add(80, 40, "segments delta", 0, [](LossyOptions& o) { o.segments = true; o.segQ[1] = 30; o.segQ[2] = -10; o.segQ[3] = 60; o.q = 30; });
        add(80, 40, "segments absolute", 0, [](LossyOptions& o) { o.segments = true; o.segAbs = true; o.segQ[0] = 4; o.segQ[1] = 50; o.segQ[2] = 90; o.segQ[3] = 127; });
        add(40, 40, "quantiser deltas", 0, [](LossyOptions& o) { o.q = 40; o.dYdc = -5; o.dY2dc = 7; o.dY2ac = -8; o.dUVdc = 15; o.dUVac = -3; });
        // filter on: plane hashes pinned after ffmpeg's VP8 decoder gave the same planes for these files (2026-11-26)
        add(53, 37, "normal filter", 0x600e424eb3ed8fffull, [](LossyOptions& o) { o.q = 40; o.level = 20; });
        add(53, 37, "normal filter strong", 0xe7ca1a66be42bdc0ull, [](LossyOptions& o) { o.q = 90; o.level = 63; o.sharpness = 0; });
        add(53, 37, "normal filter sharp", 0x36805e7f412182a7ull, [](LossyOptions& o) { o.q = 60; o.level = 45; o.sharpness = 5; });
        add(53, 37, "simple filter", 0x318b4d326a5e7358ull, [](LossyOptions& o) { o.q = 50; o.level = 30; o.simpleFilter = true; o.sharpness = 2; });
        add(80, 48, "filter deltas + segments", 0xbd7c87f4d09b3bcaull, [](LossyOptions& o) {
            o.q = 45; o.level = 25; o.lfDeltas = true; o.refDelta0 = 6; o.modeDeltaB = -9; o.segments = true; o.segLf[1] = 20; o.segLf[2] = -25; o.segLf[3] = 38;
        });
        add(80, 48, "absolute segment levels", 0x0e74aa43b32827e0ull, [](LossyOptions& o) {
            o.q = 70; o.level = 10; o.segments = true; o.segAbs = true; o.segQ[0] = 20; o.segQ[1] = 70; o.segQ[2] = 100; o.segQ[3] = 40;
            o.segLf[0] = 0; o.segLf[1] = 63; o.segLf[2] = 12; o.segLf[3] = 40;
        });
        int idx = 0;
        for (const Case& c : cases) {
            const Planes src = TestPlanes(c.w, c.h, std::uint64_t(c.w + c.h));
            Planes rec;
            const Bytes frame = EncodeVp8(src, c.o, &rec);
            Bytes chunks;
            AddChunk(chunks, "VP8 ", frame);
            const Bytes file = Riff(chunks);
            char nm[64];
            std::snprintf(nm, sizeof(nm), "lossy_%02d.webp", idx++);
            Dump(nm, file);
            int w = 0, h = 0;
            Bytes y, u, v;
            std::string err;
            const bool dec = okay::imgdec::DecodeVp8Planes(frame.data(), frame.size(), w, h, y, u, v, err);
            if (!dec) std::fprintf(stderr, "  lossy case '%s': %s\n", c.name, err.c_str());
            CHECK(dec && w == c.w && h == c.h);
            if (!dec) continue;
            Bytes all = y;
            all.insert(all.end(), u.begin(), u.end());
            all.insert(all.end(), v.begin(), v.end());
            if (c.o.level == 0) {
                const bool same = y == rec.y && u == rec.u && v == rec.v;
                if (!same) std::fprintf(stderr, "  lossy case '%s': planes differ from the encoder's reconstruction\n", c.name);
                CHECK(same);
            } else {
                std::printf("  lossy case '%s' planes %016llx\n", c.name, (unsigned long long)Fnv(all));
                CHECK(Fnv(all) == c.pinned);
                CHECK(y != rec.y);   // the loop filter did change something
            }
            const double ps = Psnr(y, src.y);
            if (c.o.q <= 60 && ps < 28) std::fprintf(stderr, "  lossy case '%s': luma PSNR %.1f dB\n", c.name, ps);
            if (c.o.q <= 60) CHECK(ps >= 28);
            DecodedImage img;
            CHECK(Decode(file, img) && img.width == c.w && img.height == c.h);
            bool opaque = true;
            for (std::size_t i = 3; i < img.rgba.size(); i += 4) opaque &= img.rgba[i] == 255;
            CHECK(opaque);
        }
    }

    // ---- colour conversion: flat planes give the BT.601 colour ------------------------------------------------------
    {
        Planes p;
        p.w = 20; p.h = 18;
        p.y.assign(20 * 18, 128); p.u.assign(10 * 9, 128); p.v.assign(10 * 9, 128);
        LossyOptions o;
        o.q = 0;
        Bytes chunks;
        AddChunk(chunks, "VP8 ", EncodeVp8(p, o));
        DecodedImage img;
        CHECK(Decode(Riff(chunks), img));
        // Y 128, Cb = Cr = 128: 1.164 * 112 = 130.4
        CHECK(img.rgba.size() == 20 * 18 * 4 && img.rgba[0] == 130 && img.rgba[1] == 130 && img.rgba[2] == 130);
        p.y.assign(20 * 18, 81); p.u.assign(10 * 9, 90); p.v.assign(10 * 9, 240);   // a strong red
        Bytes c2;
        AddChunk(c2, "VP8 ", EncodeVp8(p, o));
        CHECK(Decode(Riff(c2), img));
        // R = 1.164*65 + 1.596*112 = 254.4, G = 75.7 + 14.9 - 91.1 = -0.5 -> 0, B = 75.7 - 76.7 = -1 -> 0
        CHECK(img.rgba.size() == 20 * 18 * 4 && img.rgba[0] >= 253 && img.rgba[1] <= 1 && img.rgba[2] <= 1);
    }

    // ---- alpha: ALPH chunk with a lossy picture -----------------------------------------------------------------
    {
        const int w = 37, h = 23;
        const Planes src = TestPlanes(w, h, 9);
        LossyOptions lo;
        lo.q = 30;
        const Bytes frame = EncodeVp8(src, lo);
        std::vector<std::uint8_t> alpha(std::size_t(w) * std::size_t(h));
        for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) alpha[std::size_t(y) * w + std::size_t(x)] = std::uint8_t((x * 9 + y * 17) % 256 ^ (x > 20 ? 255 : 0));
        DecodedImage ref;
        { Bytes c; AddChunk(c, "VP8 ", frame); CHECK(Decode(Riff(c), ref)); }
        int good = 0, total = 0;
        for (int filter = 0; filter < 4; ++filter)
            for (int comp = 0; comp < 2; ++comp) {
                LLOptions o;
                o.lz77 = true; o.cacheBits = comp ? 4 : 0; o.predictor = filter == 3; o.seed = std::uint64_t(filter * 2 + comp);
                Bytes c = Vp8xHeader(w, h, true, false);
                AddChunk(c, "ALPH", EncodeAlph(alpha, w, h, filter, comp != 0, o));
                AddChunk(c, "VP8 ", frame);
                const Bytes file = Riff(c);
                DecodedImage img;
                std::string err;
                bool same = Decode(file, img, &err) && img.width == w && img.height == h;
                for (std::size_t i = 0; same && i < alpha.size(); ++i)
                    same = img.rgba[i * 4 + 3] == alpha[i] && img.rgba[i * 4] == ref.rgba[i * 4] && img.rgba[i * 4 + 2] == ref.rgba[i * 4 + 2];
                if (!same) std::fprintf(stderr, "  alpha filter %d compressed %d: %s\n", filter, comp, err.c_str());
                CHECK(same);
                good += same; ++total;
            }
        std::printf("  alpha: %d of %d ALPH variants exact\n", good, total);
    }

    // ---- extended format with a lossless image, and an animation's first frame -----------------------------------
    {
        const int w = 12, h = 7;
        const std::vector<std::uint32_t> px = TestArgb(w, h, 5);
        LLOptions o;
        o.predictor = true;
        Bytes c = Vp8xHeader(w, h, true, false);
        AddChunk(c, "ICCP", Bytes(5, 7));   // unknown to us: skipped
        AddChunk(c, "VP8L", EncodeVp8l(px, w, h, o));
        AddChunk(c, "EXIF", Bytes(3, 1));
        DecodedImage img;
        CHECK(Decode(Riff(c), img) && SameArgb(img, px, w, h));

        // a 20 x 16 canvas, frame 1 is 5 x 3 at (2, 4), frame 2 covers everything
        const int cw = 20, chh = 16, fw = 5, fh = 3;
        const std::vector<std::uint32_t> f1 = TestArgb(fw, fh, 11), f2 = TestArgb(cw, chh, 12);
        Bytes a = Vp8xHeader(cw, chh, true, true);
        Bytes anim;
        PutLe(anim, 0xFF00FF00u, 4); PutLe(anim, 0, 2);
        AddChunk(a, "ANIM", anim);
        for (int k = 0; k < 2; ++k) {
            Bytes fr;
            PutLe(fr, k ? 0 : 1, 3); PutLe(fr, k ? 0 : 2, 3);
            PutLe(fr, std::uint32_t((k ? cw : fw) - 1), 3); PutLe(fr, std::uint32_t((k ? chh : fh) - 1), 3);
            PutLe(fr, 100, 3); fr.push_back(0);
            AddChunk(fr, "VP8L", EncodeVp8l(k ? f2 : f1, k ? cw : fw, k ? chh : fh, LLOptions()));
            AddChunk(a, "ANMF", fr);
        }
        const Bytes af = Riff(a);
        DecodedImage an;
        bool ok = Decode(af, an) && an.width == cw && an.height == chh;
        for (int y = 0; ok && y < chh; ++y)
            for (int x = 0; x < cw; ++x) {
                const std::uint8_t* o2 = &an.rgba[(std::size_t(y) * cw + std::size_t(x)) * 4];
                const bool inside = x >= 2 && x < 2 + fw && y >= 4 && y < 4 + fh;
                std::uint32_t want = inside ? f1[std::size_t(y - 4) * fw + std::size_t(x - 2)] : 0u;
                if (o2[0] != ((want >> 16) & 255) || o2[1] != ((want >> 8) & 255) || o2[2] != (want & 255) || o2[3] != (want >> 24)) ok = false;
            }
        CHECK(ok);
        int iw = 0, ih = 0;
        CHECK(okay::GetImageInfo(af.data(), af.size(), &iw, &ih) && iw == cw && ih == chh);
    }

    // ---- malformed input fails cleanly ------------------------------------------------------------------------------
    {
        const std::vector<std::uint32_t> px = TestArgb(31, 17, 77);
        LLOptions o;
        o.predictor = o.crossColor = o.subtractGreen = o.lz77 = true; o.cacheBits = 5; o.metaBits = 2;
        const Bytes ll = LosslessFile(px, 31, 17, o);
        const Planes src = TestPlanes(40, 24, 3);
        LossyOptions lo;
        lo.q = 30; lo.level = 20; lo.log2Parts = 2;
        Bytes lc;
        AddChunk(lc, "VP8 ", EncodeVp8(src, lo));
        const Bytes ly = Riff(lc);
        int truncFails = 0, truncs = 0;
        for (const Bytes* f : {&ll, &ly}) {
            for (std::size_t n = 0; n + 1 < f->size(); ++n) {
                Bytes t(f->begin(), f->begin() + std::ptrdiff_t(n));
                // keep the RIFF size honest so only the data is short
                if (t.size() >= 8) { const std::uint32_t s = std::uint32_t(t.size() - 8); t[4] = std::uint8_t(s); t[5] = std::uint8_t(s >> 8); t[6] = std::uint8_t(s >> 16); t[7] = std::uint8_t(s >> 24); }
                DecodedImage img;
                std::string err;
                const bool ok = Decode(t, img, &err);
                if (!ok) { CHECK(img.rgba.empty() && !err.empty()); ++truncFails; }
                ++truncs;
            }
        }
        // A lossless stream cut short can never be complete; a lossy one may lose only padding at the very end.
        std::printf("  truncations: %d of %d refused\n", truncFails, truncs);
        CHECK(truncFails >= truncs - 8);
        Rng r(99);
        int flips = 0;
        for (int i = 0; i < 3000; ++i) {
            Bytes t = (i & 1) ? ll : ly;
            const int k = 1 + r.Below(4);
            for (int j = 0; j < k; ++j) t[std::size_t(12 + r.Below(int(t.size()) - 12))] ^= std::uint8_t(1 << r.Below(8));
            DecodedImage img;
            std::string err;
            if (Decode(t, img, &err)) CHECK(img.rgba.size() == std::size_t(img.width) * std::size_t(img.height) * 4);
            else { CHECK(!err.empty()); ++flips; }
        }
        std::printf("  bit flips: %d of 3000 refused, the rest decoded to a consistent image\n", flips);
        // lying headers
        {
            Bytes c = Vp8xHeader(16385, 4, false, false);
            AddChunk(c, "VP8L", EncodeVp8l(px, 31, 17, LLOptions()));
            DecodedImage img;
            CHECK(!Decode(Riff(c), img));
        }
        {
            Bytes c = Vp8xHeader(32, 17, false, false);   // canvas differs from the image
            AddChunk(c, "VP8L", EncodeVp8l(px, 31, 17, LLOptions()));
            DecodedImage img;
            CHECK(!Decode(Riff(c), img));
        }
        {
            Bytes c = Vp8xHeader(16384, 16384, false, true);   // animation frame outside the canvas
            Bytes fr;
            PutLe(fr, 8190, 3); PutLe(fr, 0, 3); PutLe(fr, 99, 3); PutLe(fr, 0, 3); PutLe(fr, 0, 4);
            AddChunk(c, "ANMF", fr);
            DecodedImage img;
            CHECK(!Decode(Riff(c), img));
        }
        {
            Bytes bad = ly;   // first partition size larger than the file
            bad[20] = 0xFF; bad[21] = 0xFF; bad[22] = 0xFF;
            DecodedImage img;
            CHECK(!Decode(bad, img));
        }
        {
            Bytes bad = ll;   // version bits set
            bad[24] |= 0xE0;
            DecodedImage img;
            CHECK(!Decode(bad, img));
        }
        {
            const Bytes garbage = {'R', 'I', 'F', 'F', 4, 0, 0, 0, 'W', 'E', 'B', 'P'};
            DecodedImage img;
            CHECK(!Decode(garbage, img));
            CHECK(okay::DetectImageFormat(garbage.data(), garbage.size()) == okay::ImageFormat::WEBP);
        }
    }

    TEST_MAIN_RESULT();
}
