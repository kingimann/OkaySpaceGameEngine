#pragma once
// Helpers shared by the OkayUI headless tests and the demo: a frame driver that feeds synthetic input
// to OkayUI and rasterises its draw list with the software backend, a PNG reader (our own inflate,
// enough to read what okay::ImageWrite writes) and golden-image comparison.
//
// OkayUI keeps its state in one global context, so every test starts with Reset() (OkayUI::ResetState
// plus a steady caret) and the Sim makes frames deterministic: the same calls give the same pixels.
#include "okay/UI/OkayUI.hpp"
#include "OkayUI_Soft.hpp"
#include "okay/Core/ImageWrite.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <filesystem>
#include <string>
#include <vector>

#ifndef OKAYUI_GOLDEN_DIR
#define OKAYUI_GOLDEN_DIR "."
#endif

namespace okayuitest {

/// Pictures must not depend on the machine: the engine looks for installed fonts (DejaVu, Noto, ...) when the embedded Roboto lacks a character, and a CI
/// image has other fonts than a developer's computer, so every golden test runs with the installed fonts switched off (OKAY_NO_SYSTEM_FONTS, read once by
/// the engine the first time a fallback font is looked up; this runs before main).
inline const int kPinnedFonts = [] {
#ifdef _WIN32
    _putenv("OKAY_NO_SYSTEM_FONTS=1");
#else
    setenv("OKAY_NO_SYSTEM_FONTS", "1", 1);
#endif
    return 1;
}();

using OkayUI::soft::Bitmap;

/// Forget all OkayUI state and make the caret steady (no blink), so frames are reproducible.
inline void Reset() {
    OkayUI::ResetState();
    OkayUI::Style().caretBlink = false;
}

enum Key { Key_Backspace, Key_Delete, Key_Left, Key_Right, Key_Home, Key_End, Key_Enter, Key_Escape, Key_SelectAll, Key_Up, Key_Down, Key_Copy, Key_Cut, Key_Paste, Key_COUNT };

inline float CenterX(const OkayUI::Rect& r) { return r.x + r.w * 0.5f; }
inline float CenterY(const OkayUI::Rect& r) { return r.y + r.h * 0.5f; }

/// Drives frames with a persistent mouse state. One-shot input (typed text, key edges, wheel) is
/// cleared after each frame. Nothing here touches SDL: input is a plain OkayUI::Input.
struct Sim {
    float W = 640, H = 480;
    float mx = 0, my = 0;
    bool down = false, rdown = false, mdown = false, blocked = false, shift = false, ctrl = false;
    float wheel = 0;
    std::string text;
    bool edge[Key_COUNT] = {};
    // IME: the preedit is a state (kept until EndCompose), a commit is one-shot
    std::string composition, commitText;
    bool composing = false;
    int compCaret = -1;
    void Compose(const std::string& s, int caret = -1) { composition = s; composing = true; compCaret = caret; }
    void EndCompose() { composing = false; composition.clear(); compCaret = -1; }
    void Commit(const std::string& s) { commitText = s; }
    bool named[OkayUI::KeyId_COUNT] = {};   // named keys held (see OkayUI::KeyId)
    void Hold(OkayUI::KeyId k, bool held) { named[k] = held; }
    Bitmap bmp;
    int frames = 0;
    Sim(float w = 640, float h = 480) : W(w), H(h) {}

    void Frame(const std::function<void()>& ui) {
        OkayUI::Input in;
        in.mouseX = mx; in.mouseY = my; in.mouseDown = down; in.rightDown = rdown; in.middleDown = mdown; in.blocked = blocked;
        in.wheel = wheel; in.text = text.empty() ? nullptr : text.c_str();
        in.backspace = edge[Key_Backspace]; in.del = edge[Key_Delete]; in.left = edge[Key_Left]; in.right = edge[Key_Right];
        in.home = edge[Key_Home]; in.end = edge[Key_End]; in.enter = edge[Key_Enter]; in.escape = edge[Key_Escape];
        in.selectAll = edge[Key_SelectAll]; in.up = edge[Key_Up]; in.down = edge[Key_Down]; in.copy = edge[Key_Copy]; in.cut = edge[Key_Cut]; in.paste = edge[Key_Paste]; in.shift = shift; in.ctrl = ctrl; in.displayW = W; in.displayH = H;
        for (int i = 0; i < OkayUI::KeyId_COUNT; ++i) in.keyDown[i] = named[i];
        in.composition = composing ? composition.c_str() : nullptr; in.compositionCaret = compCaret;
        in.commit = commitText.empty() ? nullptr : commitText.c_str();
        OkayUI::BeginFrame(in);
        ui();
        OkayUI::EndFrameData();
        text.clear(); commitText.clear(); wheel = 0;
        for (bool& e : edge) e = false;
        ++frames;
    }
    void Frames(int n, const std::function<void()>& ui) { for (int i = 0; i < n; ++i) Frame(ui); }
    void Move(float x, float y) { mx = x; my = y; }
    void Down() { down = true; }
    void Up() { down = false; }
    void Type(const std::string& s) { text += s; }
    /// A key edge in the next frame.
    void Press(Key k) { edge[k] = true; }
    void Tap(Key k, const std::function<void()>& ui) { Press(k); Frame(ui); }
    /// Move to a point, press, release; each step is its own frame.
    void Click(float x, float y, const std::function<void()>& ui) { Move(x, y); Frame(ui); Down(); Frame(ui); Up(); Frame(ui); }
    /// Rasterise the most recent frame's draw list.
    const Bitmap& Draw(std::uint8_t r = 0, std::uint8_t g = 0, std::uint8_t b = 0) {
        bmp = Bitmap((int)W, (int)H);
        bmp.Clear(r, g, b);
        OkayUI::soft::Render(OkayUI::GetDrawData(), bmp);
        return bmp;
    }
};

inline unsigned PixelAt(const Bitmap& b, int x, int y) {
    if (x < 0 || y < 0 || x >= b.w || y >= b.h) return 0;
    const unsigned char* q = &b.rgba[((size_t)y * b.w + (size_t)x) * 4];
    return ((unsigned)q[0] << 24) | ((unsigned)q[1] << 16) | ((unsigned)q[2] << 8) | q[3];
}
inline unsigned Rgba(int r, int g, int b, int a = 255) { return ((unsigned)r << 24) | ((unsigned)g << 16) | ((unsigned)b << 8) | (unsigned)a; }
inline unsigned ThemeRgba(const unsigned char c[4]) { return Rgba(c[0], c[1], c[2], c[3]); }
/// Number of pixels in the rectangle that differ from `bg`.
inline int CountNot(const Bitmap& b, int x0, int y0, int x1, int y1, unsigned bg) {
    int n = 0;
    for (int y = y0; y < y1; ++y) for (int x = x0; x < x1; ++x) if (PixelAt(b, x, y) != bg) ++n;
    return n;
}

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

/// Where a failing golden writes its actual picture: $OKAYUI_ACTUAL_DIR, else the system temp folder. Never the working directory (that is the source tree
/// when a test is run by hand, and pictures must not be committed by accident).
inline std::string ActualPath(const std::string& name) {
    const char* d = std::getenv("OKAYUI_ACTUAL_DIR");
    std::error_code ec;
    const std::filesystem::path dir = d && d[0] ? std::filesystem::path(d) : std::filesystem::temp_directory_path(ec);
    return (dir / (name + ".actual.png")).string();
}
inline bool WritePng(const std::string& path, const Bitmap& b) {
    return okay::ImageWrite::WritePNG(path, b.w, b.h, 4, b.rgba.data());
}

/// What a failing golden prints: the box and the rows where the pictures differ, and the actual picture as base-64 PNG (when it is under 600 KB), so a CI log is
/// enough to see what the other machine drew (decode: grep ACTUAL_PNG_BASE64 | cut -d' ' -f3 | base64 -d > x.png).
inline void DiffReport(const std::string& name, const Png& g, const Bitmap& b) {
    int x0 = b.w, y0 = b.h, x1 = -1, y1 = -1;
    std::vector<int> rows((size_t)b.h, 0);
    for (int y = 0; y < b.h; ++y) for (int x = 0; x < b.w; ++x) {
        const size_t i = ((size_t)y * (size_t)b.w + (size_t)x) * 4;
        bool d = false; for (int k = 0; k < 4; ++k) if (std::abs((int)g.rgba[i + (size_t)k] - (int)b.rgba[i + (size_t)k]) > 2) d = true;
        if (d) { x0 = std::min(x0, x); x1 = std::max(x1, x); y0 = std::min(y0, y); y1 = std::max(y1, y); ++rows[(size_t)y]; }
    }
    std::fprintf(stderr, "  golden %s: differs inside x %d..%d, y %d..%d; differing pixels per 40 rows:", name.c_str(), x0, x1, y0, y1);
    for (int y = 0; y < b.h; y += 40) { int n = 0; for (int k = y; k < y + 40 && k < b.h; ++k) n += rows[(size_t)k]; std::fprintf(stderr, " %d", n); }
    std::fprintf(stderr, "\n");
    std::FILE* f = std::fopen(ActualPath(name).c_str(), "rb");
    if (!f) { std::fprintf(stderr, "  (the actual picture could not be read back from %s)\n", ActualPath(name).c_str()); return; }
    std::string bytes; char buf[4096]; size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) bytes.append(buf, n);
    std::fclose(f);
    if (bytes.size() > 600 * 1024) { std::fprintf(stderr, "  (the actual picture is %zu bytes: too big to print)\n", bytes.size()); return; }
    static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string o;
    for (size_t i = 0; i < bytes.size(); i += 3) {
        const unsigned v = ((unsigned char)bytes[i] << 16) | (i + 1 < bytes.size() ? (unsigned char)bytes[i + 1] << 8 : 0) | (i + 2 < bytes.size() ? (unsigned char)bytes[i + 2] : 0);
        o += t[(v >> 18) & 63]; o += t[(v >> 12) & 63]; o += i + 1 < bytes.size() ? t[(v >> 6) & 63] : '='; o += i + 2 < bytes.size() ? t[v & 63] : '=';
    }
    std::fprintf(stderr, "ACTUAL_PNG_BASE64 %s %s\n", name.c_str(), o.c_str());
}

/// Compare with a committed golden PNG (per-channel tolerance 2, up to 0.05 percent of pixels may differ
/// beyond it, to survive compilers that round floats differently). OKAYUI_UPDATE_GOLDEN=1 rewrites the golden.
/// On a mismatch the actual image is written as <name>.actual.png into $OKAYUI_ACTUAL_DIR or the temp folder.
inline bool MatchesGolden(const std::string& name, const Bitmap& b, std::string* why = nullptr) {
    const std::string path = std::string(OKAYUI_GOLDEN_DIR) + "/" + name + ".png";
    const char* up = std::getenv("OKAYUI_UPDATE_GOLDEN");
    if (up && up[0] == '1') return WritePng(path, b);
    Png g = ReadPng(path);
    if (!g.ok) { if (why) *why = "cannot read golden " + path; WritePng(ActualPath(name), b); return false; }
    if (g.w != b.w || g.h != b.h) { if (why) *why = "size differs"; WritePng(ActualPath(name), b); return false; }
    size_t bad = 0;
    for (size_t i = 0; i < b.rgba.size(); i += 4) {
        bool differs = false;
        for (int k = 0; k < 4; ++k) if (std::abs((int)g.rgba[i + (size_t)k] - (int)b.rgba[i + (size_t)k]) > 2) differs = true;
        if (differs) ++bad;
    }
    const size_t allowed = (size_t)b.w * b.h / 2000;
    if (bad > allowed) {
        if (why) *why = std::to_string(bad) + " pixels differ";
        WritePng(ActualPath(name), b);
        DiffReport(name, g, b);   // CI logs are all anyone sees of a failing machine: say where, and carry the picture
        return false;
    }
    return true;
}

} // namespace okayuitest
