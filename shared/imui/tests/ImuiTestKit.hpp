#pragma once
// Helpers shared by the imui tests and the demo: a frame driver that feeds synthetic input, a PNG
// reader (our own inflate, enough to read what okay::ImageWrite writes), and image comparison.
#include "imui/Imui.hpp"
#include "imui/ImuiSoft.hpp"
#include "okay/Core/ImageWrite.hpp"
#include "okay/Graphics/Font.hpp"

#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>

#ifndef IMUI_GOLDEN_DIR
#define IMUI_GOLDEN_DIR "."
#endif

namespace imuitest {

using namespace okay::imui;

inline Context* MakeContext() {
    Context* c = CreateContext(&okay::Font8x8::Glyph, 2);
    c->style.cursorBlink = false;        // deterministic caret
    SetCurrentContext(c);
    return c;
}

/// Drives frames with a persistent mouse / keyboard state. One-shot input (typed text, wheel) is
/// cleared after each frame.
struct Sim {
    Context* ctx = nullptr;
    Input in;
    Vec2 display{640, 480};
    Bitmap bmp;
    explicit Sim(Context* c, Vec2 d = Vec2(640, 480)) : ctx(c), display(d) {}
    void Frame(const std::function<void()>& ui) {
        SetCurrentContext(ctx);
        NewFrame(in, display);
        ui();
        Render();
        in.text.clear();
        in.wheel = 0;
    }
    void Frames(int n, const std::function<void()>& ui) { for (int i = 0; i < n; ++i) Frame(ui); }
    void Move(float x, float y) { in.mousePos = Vec2(x, y); }
    void Down(int b = 0) { in.mouseDown[b] = true; }
    void Up(int b = 0) { in.mouseDown[b] = false; }
    void Key(okay::imui::Key k, bool d) { in.keyDown[k] = d; }
    void Type(const std::string& s) { in.text += s; }
    /// Press and release a key over two frames.
    void Tap(okay::imui::Key k, const std::function<void()>& ui) { Key(k, true); Frame(ui); Key(k, false); Frame(ui); }
    /// Move to a point, press, release; each step is its own frame.
    void Click(Vec2 p, const std::function<void()>& ui) { Move(p.x, p.y); Frame(ui); Down(); Frame(ui); Up(); Frame(ui); }
    const Bitmap& Draw(Col clear = MakeCol(0, 0, 0)) {
        bmp = Bitmap((int)display.x, (int)display.y);
        bmp.Clear(clear);
        SoftRender(ctx->drawData, ctx->font, bmp);
        return bmp;
    }
};

inline Vec2 Center(const Rect& r) { return Vec2((r.min.x + r.max.x) * 0.5f, (r.min.y + r.max.y) * 0.5f); }

// ---- PNG reading (inflate + unfilter) --------------------------------------------------------
struct Png { int w = 0, h = 0; std::vector<unsigned char> rgba; bool ok = false; };

namespace detail {
struct Bits {
    const unsigned char* p; size_t n, pos = 0; unsigned bit = 0; bool err = false;
    int Get(int count) {
        int v = 0;
        for (int i = 0; i < count; ++i) {
            if (pos >= n) { err = true; return 0; }
            v |= ((p[pos] >> bit) & 1) << i;
            if (++bit == 8) { bit = 0; ++pos; }
        }
        return v;
    }
};
struct Huff {
    std::vector<int> count, symbol;
    void Build(const int* lens, int n) {
        count.assign(16, 0); symbol.assign((size_t)n, 0);
        for (int i = 0; i < n; ++i) count[(size_t)lens[i]]++;
        count[0] = 0;
        std::vector<int> offs(16, 0);
        for (int i = 1; i < 16; ++i) offs[(size_t)i] = offs[(size_t)i - 1] + count[(size_t)i - 1];
        for (int i = 0; i < n; ++i) if (lens[i]) symbol[(size_t)offs[(size_t)lens[i]]++] = i;
    }
    int Decode(Bits& b) const {
        int code = 0, first = 0, index = 0;
        for (int len = 1; len < 16; ++len) {
            code |= b.Get(1);
            const int c = count[(size_t)len];
            if (code - c < first) return symbol[(size_t)(index + (code - first))];
            index += c; first += c; first <<= 1; code <<= 1;
            if (b.err) return -1;
        }
        return -1;
    }
};
inline bool Inflate(const unsigned char* src, size_t n, std::vector<unsigned char>& out) {
    static const int lbase[] = {3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
    static const int lext[] = {0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
    static const int dbase[] = {1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};
    static const int dext[] = {0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};
    Bits b{src, n};
    int last;
    do {
        last = b.Get(1);
        const int type = b.Get(2);
        if (b.err) return false;
        if (type == 0) {
            if (b.bit) { b.bit = 0; ++b.pos; }
            if (b.pos + 4 > n) return false;
            const unsigned len = src[b.pos] | (src[b.pos + 1] << 8);
            b.pos += 4;
            if (b.pos + len > n) return false;
            out.insert(out.end(), src + b.pos, src + b.pos + len);
            b.pos += len;
        } else if (type == 1 || type == 2) {
            Huff lit, dist;
            int lens[320] = {0};
            if (type == 1) {
                int i = 0;
                for (; i < 144; ++i) lens[i] = 8;
                for (; i < 256; ++i) lens[i] = 9;
                for (; i < 280; ++i) lens[i] = 7;
                for (; i < 288; ++i) lens[i] = 8;
                lit.Build(lens, 288);
                int dl[30]; for (int k = 0; k < 30; ++k) dl[k] = 5;
                dist.Build(dl, 30);
            } else {
                const int nlen = b.Get(5) + 257, ndist = b.Get(5) + 1, ncode = b.Get(4) + 4;
                static const int order[19] = {16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};
                int cl[19] = {0};
                for (int i = 0; i < ncode; ++i) cl[order[i]] = b.Get(3);
                Huff ch; ch.Build(cl, 19);
                int idx = 0;
                while (idx < nlen + ndist) {
                    int sym = ch.Decode(b);
                    if (sym < 0) return false;
                    if (sym < 16) lens[idx++] = sym;
                    else {
                        int prev = 0, rep;
                        if (sym == 16) { if (!idx) return false; prev = lens[idx - 1]; rep = 3 + b.Get(2); }
                        else if (sym == 17) rep = 3 + b.Get(3);
                        else rep = 11 + b.Get(7);
                        if (idx + rep > nlen + ndist) return false;
                        while (rep--) lens[idx++] = prev;
                    }
                }
                lit.Build(lens, nlen); dist.Build(lens + nlen, ndist);
            }
            for (;;) {
                const int sym = lit.Decode(b);
                if (sym < 0 || b.err) return false;
                if (sym < 256) out.push_back((unsigned char)sym);
                else if (sym == 256) break;
                else {
                    const int s = sym - 257;
                    if (s >= 29) return false;
                    const int len = lbase[s] + b.Get(lext[s]);
                    const int ds = dist.Decode(b);
                    if (ds < 0 || ds >= 30) return false;
                    const size_t d = (size_t)(dbase[ds] + b.Get(dext[ds]));
                    if (d > out.size()) return false;
                    for (int i = 0; i < len; ++i) out.push_back(out[out.size() - d]);
                }
            }
        } else return false;
    } while (!last);
    return true;
}
}

inline Png ReadPng(const std::string& path) {
    Png png;
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return png;
    std::vector<unsigned char> d;
    unsigned char buf[4096]; size_t k;
    while ((k = std::fread(buf, 1, sizeof buf, f)) > 0) d.insert(d.end(), buf, buf + k);
    std::fclose(f);
    if (d.size() < 33 || d[1] != 'P') return png;
    size_t pos = 8; int channels = 0; std::vector<unsigned char> idat;
    auto be = [&](size_t o) { return ((unsigned)d[o] << 24) | (d[o + 1] << 16) | (d[o + 2] << 8) | d[o + 3]; };
    while (pos + 12 <= d.size()) {
        const unsigned len = be(pos); const std::string type((const char*)&d[pos + 4], 4);
        if (pos + 12 + len > d.size()) return png;
        if (type == "IHDR") { png.w = (int)be(pos + 8); png.h = (int)be(pos + 12); const int ct = d[pos + 17]; channels = ct == 6 ? 4 : ct == 2 ? 3 : ct == 0 ? 1 : ct == 4 ? 2 : 0; if (d[pos + 16] != 8 || d[pos + 20] != 0) return png; }
        else if (type == "IDAT") idat.insert(idat.end(), d.begin() + (long)pos + 8, d.begin() + (long)pos + 8 + len);
        else if (type == "IEND") break;
        pos += 12 + len;
    }
    if (!channels || png.w <= 0 || png.h <= 0 || idat.size() < 6) return png;
    std::vector<unsigned char> raw;
    if (!detail::Inflate(idat.data() + 2, idat.size() - 2, raw)) return png;
    const size_t stride = (size_t)png.w * channels;
    if (raw.size() < (stride + 1) * (size_t)png.h) return png;
    std::vector<unsigned char> img(stride * (size_t)png.h);
    for (int y = 0; y < png.h; ++y) {
        const int ft = raw[(stride + 1) * (size_t)y];
        const unsigned char* src = &raw[(stride + 1) * (size_t)y + 1];
        unsigned char* row = &img[stride * (size_t)y];
        const unsigned char* up = y ? &img[stride * (size_t)(y - 1)] : nullptr;
        for (size_t i = 0; i < stride; ++i) {
            const int a = i >= (size_t)channels ? row[i - (size_t)channels] : 0, b = up ? up[i] : 0, c = (up && i >= (size_t)channels) ? up[i - (size_t)channels] : 0;
            int v = src[i];
            switch (ft) {
                case 1: v += a; break; case 2: v += b; break; case 3: v += (a + b) / 2; break;
                case 4: { const int p = a + b - c, pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c); v += (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c); break; }
                default: break;
            }
            row[i] = (unsigned char)v;
        }
    }
    png.rgba.assign((size_t)png.w * png.h * 4, 255);
    for (size_t i = 0; i < (size_t)png.w * png.h; ++i) {
        const unsigned char* s = &img[i * (size_t)channels]; unsigned char* o = &png.rgba[i * 4];
        if (channels >= 3) { o[0] = s[0]; o[1] = s[1]; o[2] = s[2]; if (channels == 4) o[3] = s[3]; }
        else { o[0] = o[1] = o[2] = s[0]; if (channels == 2) o[3] = s[1]; }
    }
    png.ok = true;
    return png;
}

inline bool WritePng(const std::string& path, const Bitmap& b) {
    return okay::ImageWrite::WritePNG(path, b.w, b.h, 4, b.rgba.data());
}

/// Compare with a committed golden PNG (per-channel tolerance 2, up to 0.05 percent of pixels may differ
/// beyond it, to survive compilers that round floats differently). IMUI_UPDATE_GOLDEN=1 rewrites the golden.
/// On a mismatch the actual image is written next to the working directory as <name>.actual.png.
inline bool MatchesGolden(const std::string& name, const Bitmap& b, std::string* why = nullptr) {
    const std::string path = std::string(IMUI_GOLDEN_DIR) + "/" + name + ".png";
    const char* up = std::getenv("IMUI_UPDATE_GOLDEN");
    if (up && up[0] == '1') return WritePng(path, b);
    Png g = ReadPng(path);
    if (!g.ok) { if (why) *why = "cannot read golden " + path; WritePng(name + ".actual.png", b); return false; }
    if (g.w != b.w || g.h != b.h) { if (why) *why = "size differs"; WritePng(name + ".actual.png", b); return false; }
    size_t bad = 0;
    for (size_t i = 0; i < b.rgba.size(); i += 4) {
        bool differs = false;
        for (int k = 0; k < 4; ++k) if (std::abs((int)g.rgba[i + (size_t)k] - (int)b.rgba[i + (size_t)k]) > 2) differs = true;
        if (differs) ++bad;
    }
    const size_t allowed = (size_t)b.w * b.h / 2000;
    if (bad > allowed) { if (why) *why = std::to_string(bad) + " pixels differ"; WritePng(name + ".actual.png", b); return false; }
    return true;
}

} // namespace imuitest
