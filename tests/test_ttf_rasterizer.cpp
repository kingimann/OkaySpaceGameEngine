// Tests for our own TrueType / OpenType reader and rasterizer (Graphics/TtfRaster):
// synthetic fonts with analytically known coverage (glyf simple + composite, CFF
// charstrings, CID-keyed CFF, every cmap format, .ttc, loca / hmtx variants), small
// committed reference bitmaps of DejaVu Sans, and corrupt / truncated / mutated files
// that must neither crash nor hang. (Reference bitmaps: FreeType, unhinted, rendered
// at 16x and box-filtered down, so they are exact area coverage to about 1/255.)
#include "okay/Graphics/TtfRaster.hpp"
#ifndef OKAY_TTF_STANDALONE   // (a sanitizer build of just the rasterizer sets this)
#include "okay/Graphics/TtfFont.hpp"
#endif
#include "test_framework.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

using namespace okay;
using Bytes = std::vector<std::uint8_t>;

namespace {

// ---- A tiny sfnt writer ------------------------------------------------------------
void P8(Bytes& b, unsigned v) { b.push_back((std::uint8_t)v); }
void P16(Bytes& b, unsigned v) { b.push_back((std::uint8_t)(v >> 8)); b.push_back((std::uint8_t)v); }
void P32(Bytes& b, std::uint32_t v) { P16(b, v >> 16); P16(b, v & 0xFFFF); }
void Pad4(Bytes& b) { while (b.size() % 4) b.push_back(0); }
void Put16At(Bytes& b, std::size_t at, unsigned v) { b[at] = (std::uint8_t)(v >> 8); b[at + 1] = (std::uint8_t)v; }
void Put32At(Bytes& b, std::size_t at, std::uint32_t v) { Put16At(b, at, v >> 16); Put16At(b, at + 2, v & 0xFFFF); }

struct Table { std::string tag; Bytes data; };

// Appends an sfnt (directory + tables, offsets absolute in `file`); returns its start.
std::size_t AppendSfnt(Bytes& file, const std::vector<Table>& tables, std::uint32_t sig) {
    Pad4(file);
    const std::size_t dir = file.size();
    P32(file, sig);
    P16(file, (unsigned)tables.size());
    P16(file, 0); P16(file, 0); P16(file, 0);
    std::size_t pos = dir + 12 + tables.size() * 16;
    std::vector<std::size_t> offs;
    for (const Table& t : tables) { pos = (pos + 3) & ~(std::size_t)3; offs.push_back(pos); pos += t.data.size(); }
    for (std::size_t i = 0; i < tables.size(); ++i) {
        for (int k = 0; k < 4; ++k) P8(file, (unsigned char)tables[i].tag[(std::size_t)k]);
        P32(file, 0);
        P32(file, (std::uint32_t)offs[i]);
        P32(file, (std::uint32_t)tables[i].data.size());
    }
    for (std::size_t i = 0; i < tables.size(); ++i) {
        while (file.size() < offs[i]) file.push_back(0);
        file.insert(file.end(), tables[i].data.begin(), tables[i].data.end());
    }
    return dir;
}

struct Pt { int x, y; bool on; };

Bytes SimpleGlyph(const std::vector<std::vector<Pt>>& contours) {
    Bytes g;
    int xmin = 32767, ymin = 32767, xmax = -32768, ymax = -32768;
    for (auto& c : contours) for (auto& p : c) {
        xmin = std::min(xmin, p.x); xmax = std::max(xmax, p.x);
        ymin = std::min(ymin, p.y); ymax = std::max(ymax, p.y);
    }
    P16(g, (unsigned)contours.size());
    P16(g, (unsigned)xmin); P16(g, (unsigned)ymin); P16(g, (unsigned)xmax); P16(g, (unsigned)ymax);
    int end = -1;
    for (auto& c : contours) { end += (int)c.size(); P16(g, (unsigned)end); }
    P16(g, 0);   // no instructions
    for (auto& c : contours) for (auto& p : c) P8(g, p.on ? 1 : 0);   // words for every delta
    int px = 0;
    for (auto& c : contours) for (auto& p : c) { P16(g, (unsigned)(p.x - px)); px = p.x; }
    int py = 0;
    for (auto& c : contours) for (auto& p : c) { P16(g, (unsigned)(p.y - py)); py = p.y; }
    if (g.size() % 2) g.push_back(0);
    return g;
}

struct Comp {
    int glyph; int a1 = 0, a2 = 0; bool xy = true;
    int kind = 0;   // 0 none, 1 uniform scale, 2 x/y scale, 3 2x2
    double m[4] = {1, 0, 0, 1};
    bool scaledOffset = false;
};

Bytes CompositeGlyph(const std::vector<Comp>& comps, int bboxHint) {
    Bytes g;
    P16(g, 0xFFFF);
    P16(g, 0); P16(g, 0); P16(g, (unsigned)bboxHint); P16(g, (unsigned)bboxHint);
    for (std::size_t i = 0; i < comps.size(); ++i) {
        const Comp& c = comps[i];
        unsigned fl = 1;   // words
        if (c.xy) fl |= 2;
        if (i + 1 < comps.size()) fl |= 0x20;
        if (c.kind == 1) fl |= 0x08; else if (c.kind == 2) fl |= 0x40; else if (c.kind == 3) fl |= 0x80;
        if (c.scaledOffset) fl |= 0x800;
        P16(g, fl); P16(g, (unsigned)c.glyph);
        P16(g, (unsigned)c.a1); P16(g, (unsigned)c.a2);
        auto f2 = [&](double v) { P16(g, (unsigned)(int)std::lround(v * 16384.0)); };
        if (c.kind == 1) f2(c.m[0]);
        else if (c.kind == 2) { f2(c.m[0]); f2(c.m[3]); }
        else if (c.kind == 3) { f2(c.m[0]); f2(c.m[1]); f2(c.m[2]); f2(c.m[3]); }
    }
    if (g.size() % 2) g.push_back(0);
    return g;
}

std::vector<Pt> Rect(int x0, int y0, int x1, int y1, bool ccw = true) {
    std::vector<Pt> r = {{x0, y0, true}, {x1, y0, true}, {x1, y1, true}, {x0, y1, true}};
    if (!ccw) std::reverse(r.begin(), r.end());
    return r;
}

struct Glyph { Bytes data; int adv = 600; int lsb = 0; };

Bytes CmapTable(int fmt, const std::vector<std::pair<std::uint32_t, int>>& map, bool dual) {
    Bytes sub;
    if (fmt == 0) {
        P16(sub, 0); P16(sub, 262); P16(sub, 0);
        Bytes arr(256, 0);
        for (auto& m : map) if (m.first < 256) arr[m.first] = (std::uint8_t)m.second;
        sub.insert(sub.end(), arr.begin(), arr.end());
    } else if (fmt == 4) {
        // One segment per mapped code point (idDelta), plus an idRangeOffset segment
        // for 'A'..'C' if present, plus the 0xFFFF sentinel.
        std::vector<std::uint32_t> st, en; std::vector<int> delta, ro; Bytes gids;
        for (auto& m : map) {
            if (m.first >= 'A' && m.first <= 'C') continue;
            st.push_back(m.first); en.push_back(m.first); delta.push_back((m.second - (int)m.first) & 0xFFFF); ro.push_back(0);
        }
        bool haveABC = false; int abc[3] = {0, 0, 0};
        for (auto& m : map) if (m.first >= 'A' && m.first <= 'C') { haveABC = true; abc[m.first - 'A'] = m.second; }
        std::vector<std::size_t> order;
        // Segments must ascend: put the A..C segment in order.
        struct S { std::uint32_t s, e; int d, r; };
        std::vector<S> segs;
        for (std::size_t i = 0; i < st.size(); ++i) segs.push_back({st[i], en[i], delta[i], 0});
        if (haveABC) segs.push_back({'A', 'C', 0, -1});
        std::sort(segs.begin(), segs.end(), [](const S& a, const S& b) { return a.s < b.s; });
        segs.push_back({0xFFFF, 0xFFFF, 1, 0});
        const unsigned segCount = (unsigned)segs.size();
        P16(sub, 4); P16(sub, 0); P16(sub, 0); P16(sub, segCount * 2); P16(sub, 0); P16(sub, 0); P16(sub, 0);
        for (auto& s : segs) P16(sub, s.e);
        P16(sub, 0);
        for (auto& s : segs) P16(sub, s.s);
        for (auto& s : segs) P16(sub, (unsigned)s.d);
        const std::size_t roStart = sub.size();
        for (std::size_t i = 0; i < segs.size(); ++i) P16(sub, 0);
        for (std::size_t i = 0; i < segs.size(); ++i) if (segs[i].r == -1) {
            // glyphIdArray follows the idRangeOffset array
            const std::size_t gidPos = sub.size();
            const std::size_t here = roStart + i * 2;
            Put16At(sub, here, (unsigned)(gidPos - here));
            for (int k = 0; k < 3; ++k) P16(sub, (unsigned)abc[k]);
        }
        Put16At(sub, 2, (unsigned)sub.size());
    } else if (fmt == 6) {
        std::uint32_t lo = map.front().first, hi = map.back().first;
        P16(sub, 6); P16(sub, 0); P16(sub, 0); P16(sub, lo); P16(sub, hi - lo + 1);
        for (std::uint32_t c = lo; c <= hi; ++c) {
            int g = 0; for (auto& m : map) if (m.first == c) g = m.second;
            P16(sub, (unsigned)g);
        }
        Put16At(sub, 2, (unsigned)sub.size());
    } else {   // 12 and 13
        P16(sub, (unsigned)fmt); P16(sub, 0); P32(sub, 0); P32(sub, 0); P32(sub, (std::uint32_t)map.size());
        for (auto& m : map) { P32(sub, m.first); P32(sub, m.first); P32(sub, (std::uint32_t)m.second); }
        Put32At(sub, 4, (std::uint32_t)sub.size());
    }
    Bytes t;
    P16(t, 0);
    P16(t, dual ? 2 : 1);
    const std::size_t hdr = 4 + (dual ? 16 : 8);
    if (dual) {
        // (3,1) format 4 with A..C,  and (3,10) format 12 with a code point past the BMP
        Bytes s4 = CmapTable(4, {{'A', 1}, {'B', 2}, {'C', 3}}, false);
        Bytes s12 = CmapTable(12, {{'A', 1}, {0x1F600, 2}}, false);
        s4.erase(s4.begin(), s4.begin() + 12); s12.erase(s12.begin(), s12.begin() + 12);
        P16(t, 3); P16(t, 1); P32(t, (std::uint32_t)hdr);
        P16(t, 3); P16(t, 10); P32(t, (std::uint32_t)(hdr + s4.size()));
        t.insert(t.end(), s4.begin(), s4.end()); t.insert(t.end(), s12.begin(), s12.end());
        return t;
    }
    P16(t, fmt == 12 || fmt == 13 ? 3 : 3); P16(t, fmt == 12 || fmt == 13 ? 10 : 1); P32(t, (std::uint32_t)hdr);
    t.insert(t.end(), sub.begin(), sub.end());
    return t;
}

struct Spec {
    int upem = 1000, asc = 800, desc = -200;
    std::vector<Glyph> glyphs;
    int cmapFmt = 4;
    std::vector<std::pair<std::uint32_t, int>> map;
    bool longLoca = false;
    int numHM = -1;
    bool dualCmap = false;
};

std::vector<Table> GlyfTables(const Spec& s) {
    std::vector<Table> t;
    Table head{"head", Bytes(54, 0)};
    Put32At(head.data, 0, 0x00010000);
    Put16At(head.data, 18, (unsigned)s.upem);
    Put16At(head.data, 50, s.longLoca ? 1 : 0);
    Table hhea{"hhea", Bytes(36, 0)};
    Put16At(hhea.data, 4, (unsigned)s.asc); Put16At(hhea.data, 6, (unsigned)s.desc);
    const int n = (int)s.glyphs.size();
    const int numHM = s.numHM < 0 ? n : s.numHM;
    Put16At(hhea.data, 34, (unsigned)numHM);
    Table maxp{"maxp", Bytes(6, 0)};
    Put32At(maxp.data, 0, 0x00010000); Put16At(maxp.data, 4, (unsigned)n);
    Table hmtx{"hmtx", {}};
    for (int i = 0; i < n; ++i) {
        if (i < numHM) { P16(hmtx.data, (unsigned)s.glyphs[(std::size_t)i].adv); P16(hmtx.data, (unsigned)s.glyphs[(std::size_t)i].lsb); }
        else P16(hmtx.data, (unsigned)s.glyphs[(std::size_t)i].lsb);
    }
    Table glyf{"glyf", {}}, loca{"loca", {}};
    for (int i = 0; i <= n; ++i) {
        if (s.longLoca) P32(loca.data, (std::uint32_t)glyf.data.size()); else P16(loca.data, (unsigned)(glyf.data.size() / 2));
        if (i < n) {
            Bytes g = s.glyphs[(std::size_t)i].data;
            if (g.size() % 4) g.resize((g.size() + 3) & ~(std::size_t)3, 0);
            glyf.data.insert(glyf.data.end(), g.begin(), g.end());
        }
    }
    Table cmap{"cmap", CmapTable(s.cmapFmt, s.map, s.dualCmap)};
    return {head, hhea, maxp, hmtx, cmap, loca, glyf};
}

Bytes MakeTtf(const Spec& s) { Bytes f; AppendSfnt(f, GlyfTables(s), 0x00010000); return f; }

// ---- CFF writer ------------------------------------------------------------------------
Bytes Num(int v) {
    Bytes b;
    if (v >= -107 && v <= 107) b.push_back((std::uint8_t)(v + 139));
    else if (v >= 108 && v <= 1131) { v -= 108; b.push_back((std::uint8_t)(247 + v / 256)); b.push_back((std::uint8_t)(v % 256)); }
    else if (v <= -108 && v >= -1131) { v = -v - 108; b.push_back((std::uint8_t)(251 + v / 256)); b.push_back((std::uint8_t)(v % 256)); }
    else { b.push_back(28); P16(b, (unsigned)v); }
    return b;
}
enum { HSTEM = 1, VSTEM = 3, VMOVETO = 4, RLINETO = 5, HLINETO = 6, VLINETO = 7, RRCURVETO = 8, CALLSUBR = 10, RETURN = 11,
       ENDCHAR = 14, HSTEMHM = 18, HINTMASK = 19, RMOVETO = 21, HMOVETO = 22, VSTEMHM = 23, RCURVELINE = 24, RLINECURVE = 25,
       VVCURVETO = 26, HHCURVETO = 27, CALLGSUBR = 29, VHCURVETO = 30, HVCURVETO = 31,
       HFLEX = 0x0C22, FLEX = 0x0C23, HFLEX1 = 0x0C24, FLEX1 = 0x0C25 };
// Build with a small builder: C.n(5).op(RMOVETO)...
struct CsB {
    Bytes b;
    CsB& n(int v) { Bytes x = Num(v); b.insert(b.end(), x.begin(), x.end()); return *this; }
    CsB& ns(std::initializer_list<int> vs) { for (int v : vs) n(v); return *this; }
    CsB& op(int o) { if (o >= 0x0C00) { b.push_back(12); b.push_back((std::uint8_t)(o & 0xFF)); } else b.push_back((std::uint8_t)o); return *this; }
    CsB& raw(int byte) { b.push_back((std::uint8_t)byte); return *this; }
};

Bytes CffIndexOf(const std::vector<Bytes>& items) {
    Bytes b;
    P16(b, (unsigned)items.size());
    if (items.empty()) return b;
    P8(b, 2);
    unsigned off = 1;
    P16(b, off);
    for (auto& it : items) { off += (unsigned)it.size(); P16(b, off); }
    for (auto& it : items) b.insert(b.end(), it.begin(), it.end());
    return b;
}
Bytes DictInt(int v) { Bytes b; b.push_back(29); P32(b, (std::uint32_t)v); return b; }
void DictOp(Bytes& d, int op) { if (op >= 0x0C00) { d.push_back(12); d.push_back((std::uint8_t)(op & 0xFF)); } else d.push_back((std::uint8_t)op); }

struct CffSpec {
    std::vector<Bytes> charstrings;
    std::vector<Bytes> gsubrs;
    std::vector<Bytes> localSubrs;                 // non-CID
    bool cid = false;
    std::vector<std::vector<Bytes>> fdSubrs;       // CID: per font dict
    int fdSelectFmt = 3;
    std::vector<int> fdOfGlyph;                    // CID: glyph -> fd
};

Bytes BuildCff(const CffSpec& c) {
    Bytes hdr = {1, 0, 4, 1};
    Bytes name = CffIndexOf({Bytes{'T'}});
    Bytes strs = CffIndexOf({});
    Bytes gs = CffIndexOf(c.gsubrs);
    Bytes cs = CffIndexOf(c.charstrings);
    auto topDict = [&](int csOff, int privSize, int privOff, int fdaOff, int fdsOff) {
        Bytes d;
        if (c.cid) { for (int v : {391, 392, 0}) { Bytes x = DictInt(v); d.insert(d.end(), x.begin(), x.end()); } DictOp(d, 0x0C1E); }
        Bytes x = DictInt(csOff); d.insert(d.end(), x.begin(), x.end()); DictOp(d, 17);
        if (c.cid) {
            x = DictInt(fdaOff); d.insert(d.end(), x.begin(), x.end()); DictOp(d, 0x0C24);
            x = DictInt(fdsOff); d.insert(d.end(), x.begin(), x.end()); DictOp(d, 0x0C25);
        } else {
            x = DictInt(privSize); d.insert(d.end(), x.begin(), x.end());
            x = DictInt(privOff); d.insert(d.end(), x.begin(), x.end()); DictOp(d, 18);
        }
        return d;
    };
    const std::size_t topLen = CffIndexOf({topDict(0, 0, 0, 0, 0)}).size();
    std::size_t pos = hdr.size() + name.size() + topLen + strs.size() + gs.size();
    Bytes out;
    if (!c.cid) {
        const int csOff = (int)pos;
        const int privOff = csOff + (int)cs.size();
        Bytes priv; { Bytes x = DictInt(6); priv.insert(priv.end(), x.begin(), x.end()); DictOp(priv, 19); }   // Subrs at +6
        Bytes subrs = CffIndexOf(c.localSubrs);
        out = hdr;
        for (const Bytes* p : {&name}) out.insert(out.end(), p->begin(), p->end());
        Bytes top = CffIndexOf({topDict(csOff, (int)priv.size(), privOff, 0, 0)});
        out.insert(out.end(), top.begin(), top.end());
        out.insert(out.end(), strs.begin(), strs.end()); out.insert(out.end(), gs.begin(), gs.end());
        out.insert(out.end(), cs.begin(), cs.end()); out.insert(out.end(), priv.begin(), priv.end());
        if (!c.localSubrs.empty()) out.insert(out.end(), subrs.begin(), subrs.end());
        return out;
    }
    // CID-keyed: charstrings, FDArray, FDSelect, then each font dict's Private + Subrs.
    const int csOff = (int)pos;
    const int nfd = (int)c.fdSubrs.size();
    auto fdDict = [&](int psize, int poff) { Bytes d; Bytes x = DictInt(psize); d.insert(d.end(), x.begin(), x.end()); x = DictInt(poff); d.insert(d.end(), x.begin(), x.end()); DictOp(d, 18); return d; };
    std::vector<Bytes> fdd; for (int i = 0; i < nfd; ++i) fdd.push_back(fdDict(0, 0));
    const int fdaOff = csOff + (int)cs.size();
    const int fdaLen = (int)CffIndexOf(fdd).size();
    Bytes fdsel;
    if (c.fdSelectFmt == 0) { P8(fdsel, 0); for (int g : c.fdOfGlyph) P8(fdsel, (unsigned)g); }
    else {
        P8(fdsel, 3);
        std::vector<std::pair<int, int>> ranges;
        for (std::size_t g = 0; g < c.fdOfGlyph.size(); ++g) if (g == 0 || c.fdOfGlyph[g] != c.fdOfGlyph[g - 1]) ranges.push_back({(int)g, c.fdOfGlyph[g]});
        P16(fdsel, (unsigned)ranges.size());
        for (auto& r : ranges) { P16(fdsel, (unsigned)r.first); P8(fdsel, (unsigned)r.second); }
        P16(fdsel, (unsigned)c.fdOfGlyph.size());
    }
    const int fdsOff = fdaOff + fdaLen;
    int p = fdsOff + (int)fdsel.size();
    Bytes privBlock; fdd.clear();
    for (int i = 0; i < nfd; ++i) {
        Bytes priv; { Bytes x = DictInt(6); priv.insert(priv.end(), x.begin(), x.end()); DictOp(priv, 19); }
        Bytes sub = CffIndexOf(c.fdSubrs[(std::size_t)i]);
        fdd.push_back(fdDict((int)priv.size(), p));
        privBlock.insert(privBlock.end(), priv.begin(), priv.end());
        privBlock.insert(privBlock.end(), sub.begin(), sub.end());
        p += (int)(priv.size() + sub.size());
    }
    out = hdr;
    out.insert(out.end(), name.begin(), name.end());
    Bytes top = CffIndexOf({topDict(csOff, 0, 0, fdaOff, fdsOff)});
    out.insert(out.end(), top.begin(), top.end());
    out.insert(out.end(), strs.begin(), strs.end()); out.insert(out.end(), gs.begin(), gs.end());
    out.insert(out.end(), cs.begin(), cs.end());
    Bytes fda = CffIndexOf(fdd);
    out.insert(out.end(), fda.begin(), fda.end());
    out.insert(out.end(), fdsel.begin(), fdsel.end());
    out.insert(out.end(), privBlock.begin(), privBlock.end());
    return out;
}

Bytes MakeOtf(const Spec& s, const CffSpec& c) {
    std::vector<Table> t = GlyfTables(s);
    t.erase(std::remove_if(t.begin(), t.end(), [](const Table& x) { return x.tag == "glyf" || x.tag == "loca"; }), t.end());
    t.push_back({"CFF ", BuildCff(c)});
    Bytes f; AppendSfnt(f, t, 0x4F54544F);
    return f;
}

// ---- Rendering helpers ---------------------------------------------------------------------
struct Bmp { bool ok = false; int x0 = 0, y0 = 0, w = 0, h = 0; std::vector<std::uint8_t> px; double Sum() const { double s = 0; for (auto v : px) s += v; return s / 255.0; } int At(int x, int y) const { return px[(std::size_t)y * w + x]; } };

Bmp Draw(const ttf::Face& f, int glyph, float px) {
    Bmp b;
    const float scale = f.ScaleForPixelHeight(px);
    int x0, y0, x1, y1;
    if (!f.GlyphBox(glyph, scale, x0, y0, x1, y1)) return b;
    b.x0 = x0; b.y0 = y0; b.w = x1 - x0; b.h = y1 - y0;
    b.px.assign((std::size_t)b.w * b.h, 77);
    b.ok = f.Render(glyph, scale, b.px.data(), b.w, b.h, b.w);
    return b;
}

bool SameBmp(const Bmp& a, const Bmp& b, int tol = 0) {
    if (a.ok != b.ok || a.w != b.w || a.h != b.h || a.x0 != b.x0 || a.y0 != b.y0) return false;
    for (std::size_t i = 0; i < a.px.size(); ++i) if (std::abs((int)a.px[i] - (int)b.px[i]) > tol) return false;
    return true;
}

Glyph Notdef() { Glyph g; g.data = SimpleGlyph({Rect(100, 0, 400, 700)}); return g; }

std::vector<std::uint8_t> FromHex(const char* s) {
    std::vector<std::uint8_t> v;
    for (; s[0] && s[1]; s += 2) {
        auto h = [](char c) { return c <= '9' ? c - '0' : c - 'A' + 10; };
        v.push_back((std::uint8_t)(h(s[0]) * 16 + h(s[1])));
    }
    return v;
}

Bytes ReadFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return Bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

// ---- DejaVu Sans reference glyphs (FreeType, unhinted, 16x box-filtered) ---------------------
struct Ref { unsigned cp; float px; int x0, y0, w, h; const char* hex; };
const Ref kRefs[] = {
#include "ttf_refs.inc"
};

std::uint32_t g_rng = 12345;
std::uint32_t Rnd() { g_rng = g_rng * 1664525u + 1013904223u; return g_rng >> 8; }

// Load + exercise a (possibly corrupt) font file: nothing may crash or run away.
void Exercise(const Bytes& data, int glyphs) {
    ttf::Face f;
    if (!f.Load(data.data(), data.size(), 0)) return;
    const int n = std::min(f.GlyphCount(), glyphs);
    static const float sizes[] = {8.0f, 23.0f, 64.0f};
    for (int g = 0; g < n; ++g) {
        const float px = sizes[g % 3];
        const float scale = f.ScaleForPixelHeight(px);
        int x0, y0, x1, y1, adv, lsb;
        f.HMetrics(g, adv, lsb);
        if (f.GlyphBox(g, scale, x0, y0, x1, y1)) {
            std::vector<std::uint8_t> px2((std::size_t)(x1 - x0) * (y1 - y0));
            f.Render(g, scale, px2.data(), x1 - x0, y1 - y0, x1 - x0);
        }
    }
    for (std::uint32_t c = 0; c < 0x300; c += 7) f.FindGlyph(c);
    f.FindGlyph(0x1F600); f.FindGlyph(0xFFFF); f.FindGlyph(0xFFFFFFFFu);
}

} // namespace

int main() {
    RUN_SUITE("ttf_rasterizer");

    // ---- Analytic coverage: an aligned square, a half-pixel square, a quadratic "circle" ----
    {
        Spec s;
        s.glyphs.push_back(Notdef());
        { Glyph g; g.data = SimpleGlyph({Rect(0, 0, 500, 500)}); s.glyphs.push_back(g); }                       // 1: aligned square
        { Glyph g; g.data = SimpleGlyph({Rect(250, 250, 750, 750)}); s.glyphs.push_back(g); }                   // 2: shifted by 2.5px at 10px
        {   // 3: four off-curve corners of a 800-unit square: the area is 5/6 of the square
            Glyph g; g.data = SimpleGlyph({{{0, 0, false}, {800, 0, false}, {800, 800, false}, {0, 800, false}}}); s.glyphs.push_back(g); }
        { Glyph g; g.data = SimpleGlyph({Rect(0, 0, 600, 600), Rect(150, 150, 450, 450, false)}); s.glyphs.push_back(g); }   // 4: ring (hole wound the other way)
        { Glyph g; g.data = SimpleGlyph({Rect(0, 0, 600, 600), Rect(150, 150, 450, 450, true)}); s.glyphs.push_back(g); }    // 5: same winding: filled (non-zero)
        { Glyph g; g.data = SimpleGlyph({{{0, 0, true}, {1000, 0, true}, {0, 1000, true}}}); s.glyphs.push_back(g); }        // 6: triangle
        { Glyph g; g.data = {}; g.adv = 300; s.glyphs.push_back(g); }                                           // 7: space
        s.map = {{'A', 1}, {'B', 2}, {'C', 3}, {'D', 4}, {'E', 5}, {'F', 6}, {' ', 7}};
        Bytes font = MakeTtf(s);
        ttf::Face f;
        CHECK(f.Load(font.data(), font.size()));
        CHECK(f.Valid() && !f.IsCff());
        CHECK(f.GlyphCount() == 8 && f.UnitsPerEm() == 1000);
        int a, d, g; f.VMetrics(a, d, g);
        CHECK(a == 800 && d == -200 && g == 0);
        CHECK_NEAR(f.ScaleForPixelHeight(10.0f), 0.01, 1e-7);

        Bmp sq = Draw(f, 1, 10.0f);      // 5x5 px square, bottom on the baseline
        CHECK(sq.ok && sq.w == 5 && sq.h == 5 && sq.x0 == 0 && sq.y0 == -5);
        bool all255 = sq.ok;
        for (auto v : sq.px) if (v != 255) all255 = false;
        CHECK(all255);

        Bmp hp = Draw(f, 2, 10.0f);      // 2.5..7.5 px: 6x6 box, edge pixels half covered
        CHECK(hp.ok && hp.w == 6 && hp.h == 6 && hp.x0 == 2 && hp.y0 == -8);
        if (hp.ok && hp.w == 6 && hp.h == 6) {
            CHECK(std::abs(hp.At(0, 0) - 64) <= 1);      // corner: a quarter
            CHECK(std::abs(hp.At(2, 0) - 128) <= 1);     // top edge: half
            CHECK(std::abs(hp.At(0, 3) - 128) <= 1);     // left edge: half
            CHECK(hp.At(2, 3) == 255);
            CHECK_NEAR(hp.Sum(), 25.0, 0.05);
        }

        Bmp circ = Draw(f, 3, 80.0f);    // scale 0.08: 800 units = 64px; area = 5/6 * 64^2
        CHECK(circ.ok);
        CHECK_NEAR(circ.Sum(), 5.0 / 6.0 * 64.0 * 64.0, 5.0 / 6.0 * 64.0 * 64.0 * 0.002);

        Bmp ring = Draw(f, 4, 100.0f);   // scale 0.1: 60x60 outer, 30x30 hole
        CHECK(ring.ok && ring.w == 60);
        if (ring.ok) { CHECK(ring.At(30, 30) == 0); CHECK(ring.At(5, 5) == 255); CHECK_NEAR(ring.Sum(), 3600.0 - 900.0, 0.5); }
        Bmp same = Draw(f, 5, 100.0f);
        CHECK(same.ok && same.At(30, 30) == 255);

        Bmp tri = Draw(f, 6, 50.0f);     // scale 0.05: right triangle with legs 50px: area 1250
        CHECK(tri.ok && tri.w == 50 && tri.h == 50);
        CHECK_NEAR(tri.Sum(), 1250.0, 1.0);

        // A glyph with no outline has no box and draws nothing.
        int x0, y0, x1, y1;
        CHECK(!f.GlyphBox(7, 0.01f, x0, y0, x1, y1));
        std::uint8_t one = 9;
        CHECK(!f.Render(7, 0.01f, &one, 1, 1, 1) && one == 0);
        // hmtx
        int adv, lsb;
        CHECK(f.HMetrics(7, adv, lsb) && adv == 300);
        CHECK(!f.HMetrics(99, adv, lsb) && !f.HMetrics(-1, adv, lsb));
        // cmap
        CHECK(f.FindGlyph('A') == 1 && f.FindGlyph('F') == 6 && f.FindGlyph(' ') == 7);
        CHECK(f.FindGlyph('Z') == 0 && f.FindGlyph(0x10FFFF) == 0 && f.FindGlyph(0) == 0);
        // Bad scales.
        CHECK(!f.GlyphBox(1, 0.0f, x0, y0, x1, y1) && !f.GlyphBox(1, -1.0f, x0, y0, x1, y1));
        CHECK(!f.GlyphBox(1, std::nanf(""), x0, y0, x1, y1) && !f.GlyphBox(1, 1e30f, x0, y0, x1, y1));
        CHECK(!f.GlyphBox(1, 100.0f, x0, y0, x1, y1));   // 50000 px: over the 4096 cap
        CHECK(!f.GlyphBox(-1, 0.01f, x0, y0, x1, y1) && !f.GlyphBox(1000, 0.01f, x0, y0, x1, y1));
        // Rendering into a bigger-stride buffer leaves the margin alone.
        std::vector<std::uint8_t> wide(5 * 8, 7);
        CHECK(f.Render(1, 0.01f, wide.data(), 5, 5, 8));
        CHECK(wide[0] == 255 && wide[5] == 7 && wide[7] == 7);
        // Rendering into a window smaller than the box is clipped, not overrun.
        std::vector<std::uint8_t> clipped(3 * 3, 7);
        CHECK(f.Render(1, 0.01f, clipped.data(), 3, 3, 3));
        CHECK(clipped[0] == 255 && clipped[8] == 255);
    }

    // ---- Composite glyphs ----------------------------------------------------------------
    {
        Spec s;
        s.glyphs.push_back(Notdef());
        { Glyph g; g.data = SimpleGlyph({Rect(0, 0, 200, 200)}); s.glyphs.push_back(g); }   // 1: 200 square
        { Comp a; a.glyph = 1; Comp b; b.glyph = 1; b.a1 = 400; b.a2 = 300;
          Glyph g; g.data = CompositeGlyph({a, b}, 700); s.glyphs.push_back(g); }                // 2: two squares
        { Comp a; a.glyph = 1; a.kind = 1; a.m[0] = a.m[3] = 1.0; 
          Comp b; b.glyph = 1; b.kind = 1; b.m[0] = b.m[3] = 0.5; b.a1 = 500; b.a2 = 0;
          Glyph g; g.data = CompositeGlyph({a, b}, 700); s.glyphs.push_back(g); }                // 3: scale 0.5 at (500,0)
        { Comp a; a.glyph = 1;
          Comp b; b.glyph = 1; b.xy = false; b.a1 = 1; b.a2 = 0;   // child point 0 (0,0) onto parent point 1 (200,0)
          Glyph g; g.data = CompositeGlyph({a, b}, 700); s.glyphs.push_back(g); }                // 4: point matching: a square next to it
        { Comp a; a.glyph = 1; a.kind = 3; a.m[0] = 0; a.m[1] = 1; a.m[2] = -1; a.m[3] = 0;   // 90 degree turn: x' = -y, y' = x
          Glyph g; g.data = CompositeGlyph({a}, 700); s.glyphs.push_back(g); }                   // 5
        { Comp a; a.glyph = 1; a.kind = 2; a.m[0] = 1.5; a.m[3] = 0.5; a.a1 = 100; a.a2 = 100; a.scaledOffset = true;
          Glyph g; g.data = CompositeGlyph({a}, 700); s.glyphs.push_back(g); }                   // 6: x1.5, y/2, scaled offset
        { Comp a; a.glyph = 7;   // composite of itself: a cycle must be refused
          Glyph g; g.data = CompositeGlyph({a}, 700); s.glyphs.push_back(g); }                   // 7
        { Comp a; a.glyph = 99;  // out-of-range component
          Glyph g; g.data = CompositeGlyph({a}, 700); s.glyphs.push_back(g); }                   // 8
        s.map = {{'a', 2}, {'b', 3}, {'c', 4}, {'d', 5}, {'e', 6}};
        Bytes font = MakeTtf(s);
        ttf::Face f;
        CHECK(f.Load(font.data(), font.size()));
        Bmp two = Draw(f, 2, 100.0f);   // scale 0.1: squares 20px at (0,0) and (40,30)
        CHECK(two.ok && two.x0 == 0 && two.w == 60 && two.h == 50 && two.y0 == -50);
        if (two.ok) {
            CHECK_NEAR(two.Sum(), 800.0, 0.5);
            CHECK(two.At(5, 45) == 255 && two.At(45, 5) == 255);   // lower-left square, upper-right square
            CHECK(two.At(30, 25) == 0);
        }
        Bmp sc = Draw(f, 3, 100.0f);   // scaled-by-1 square (kind 1, m=1) at 0, plus a 0.5x square at (50,0)
        CHECK(sc.ok);
        if (sc.ok) CHECK_NEAR(sc.Sum(), 400.0 + 100.0, 0.5);
        Bmp pm = Draw(f, 4, 100.0f);   // two squares side by side
        CHECK(pm.ok && pm.w == 40 && pm.h == 20);
        if (pm.ok) CHECK_NEAR(pm.Sum(), 800.0, 0.5);
        Bmp rot = Draw(f, 5, 100.0f);  // rotated into x in [-200,0]
        CHECK(rot.ok && rot.x0 == -20 && rot.w == 20);
        Bmp scl = Draw(f, 6, 100.0f);  // x1.5 y/2: 30 x 10 px; offset (100,100) scaled by (1.5, 0.5) = (150, 50) units
        CHECK(scl.ok && scl.w == 30 && scl.h == 10 && scl.x0 == 15);
        if (scl.ok) { CHECK(scl.y0 == -15); CHECK_NEAR(scl.Sum(), 300.0, 0.5); }
        CHECK(!Draw(f, 7, 20.0f).ok);   // cycle
        CHECK(!Draw(f, 8, 20.0f).ok);
    }

    // ---- cmap formats and selection ------------------------------------------------------
    {
        for (int fmt : {0, 4, 6, 12, 13}) {
            Spec s;
            s.glyphs.push_back(Notdef());
            for (int i = 0; i < 5; ++i) { Glyph g; g.data = SimpleGlyph({Rect(0, 0, 100 * (i + 1), 100)}); s.glyphs.push_back(g); }
            s.cmapFmt = fmt;
            s.map = {{'A', 1}, {'B', 2}, {'C', 3}, {'x', 4}, {'z', 5}};
            if (fmt == 13) s.map = {{'A', 1}, {'B', 1}, {'C', 1}, {'x', 4}, {'z', 4}};
            Bytes font = MakeTtf(s);
            ttf::Face f;
            CHECK(f.Load(font.data(), font.size()));
            const int want[] = {1, fmt == 13 ? 1 : 2, fmt == 13 ? 1 : 3, 4, fmt == 13 ? 4 : 5};
            const unsigned cps[] = {'A', 'B', 'C', 'x', 'z'};
            for (int i = 0; i < 5; ++i) { CHECK(f.FindGlyph(cps[i]) == want[i]); if (f.FindGlyph(cps[i]) != want[i]) std::printf("fmt %d cp %c got %d want %d\n", fmt, (char)cps[i], f.FindGlyph(cps[i]), want[i]); }
            CHECK(f.FindGlyph('D') == 0 && f.FindGlyph('y') == 0 && f.FindGlyph(0x4E00) == 0 && f.FindGlyph(0x10000) == 0);
            if (fmt == 6) CHECK(f.FindGlyph('A' - 1) == 0);
        }
        // A (3,1) format 4 table (with an idRangeOffset segment) next to a (3,10) format 12: the latter wins.
        Spec s;
        s.glyphs.push_back(Notdef());
        for (int i = 0; i < 3; ++i) { Glyph g; g.data = SimpleGlyph({Rect(0, 0, 100, 100)}); s.glyphs.push_back(g); }
        s.dualCmap = true;
        Bytes font = MakeTtf(s);
        ttf::Face f;
        CHECK(f.Load(font.data(), font.size()));
        CHECK(f.FindGlyph('A') == 1 && f.FindGlyph(0x1F600) == 2 && f.FindGlyph('B') == 0);   // the format 12 table has no 'B'
        // Format 4 alone: delta segments and the range-offset segment.
        Spec s4 = s; s4.dualCmap = false; s4.cmapFmt = 4;
        s4.map = {{'A', 3}, {'B', 1}, {'C', 2}, {'q', 1}};
        Bytes f4 = MakeTtf(s4);
        ttf::Face g4;
        CHECK(g4.Load(f4.data(), f4.size()));
        CHECK(g4.FindGlyph('A') == 3 && g4.FindGlyph('B') == 1 && g4.FindGlyph('C') == 2 && g4.FindGlyph('q') == 1 && g4.FindGlyph('r') == 0);
        CHECK(g4.FindGlyph(0x20000) == 0);
    }

    // ---- loca / hmtx variants ---------------------------------------------------------------
    {
        for (int longLoca = 0; longLoca < 2; ++longLoca) {
            Spec s;
            s.glyphs.push_back(Notdef());
            for (int i = 0; i < 4; ++i) { Glyph g; g.data = SimpleGlyph({Rect(0, 0, 100 + 50 * i, 100)}); g.adv = 400 + 10 * i; g.lsb = i; s.glyphs.push_back(g); }
            s.longLoca = longLoca != 0;
            s.numHM = 3;   // glyphs 3.. share glyph 2's advance and have only an lsb entry
            s.map = {{'A', 1}, {'B', 2}, {'C', 3}, {'D', 4}};
            Bytes font = MakeTtf(s);
            ttf::Face f;
            CHECK(f.Load(font.data(), font.size()));
            int adv, lsb;
            CHECK(f.HMetrics(1, adv, lsb) && adv == 400 && lsb == 0);
            CHECK(f.HMetrics(2, adv, lsb) && adv == 410 && lsb == 1);
            CHECK(f.HMetrics(3, adv, lsb) && adv == 410 && lsb == 2);   // past numberOfHMetrics: the last advance
            CHECK(f.HMetrics(4, adv, lsb) && adv == 410 && lsb == 3);
            Bmp b = Draw(f, 4, 100.0f);
            CHECK(b.ok && b.w == 25 && b.h == 10);
        }
    }

    // ---- Missing glyph (glyph 0 is what an unmapped character draws) --------------------------
    {
        Spec s;
        s.glyphs.push_back(Notdef());
        { Glyph g; g.data = SimpleGlyph({Rect(0, 0, 500, 500)}); s.glyphs.push_back(g); }
        s.map = {{'A', 1}};
        Bytes font = MakeTtf(s);
        ttf::Face f;
        CHECK(f.Load(font.data(), font.size()));
        CHECK(f.FindGlyph('Q') == 0);
        Bmp nd = Draw(f, f.FindGlyph('Q'), 100.0f);
        CHECK(nd.ok && nd.w == 30 && nd.h == 70);
    }

    // ---- CFF (Type 2 charstrings) -----------------------------------------------------------------
    {
        CffSpec c;
        Spec s;
        auto addGlyph = [&](const Bytes& cs, int cp) {
            c.charstrings.push_back(cs);
            Glyph g; g.data = {}; s.glyphs.push_back(g);
            if (cp) s.map.push_back({(std::uint32_t)cp, (int)c.charstrings.size() - 1});
        };
        addGlyph(CsB().op(ENDCHAR).b, 0);                                                                          // 0 .notdef
        addGlyph(CsB().ns({250, 0, 0}).op(RMOVETO).ns({500, 500, -500}).op(HLINETO).op(ENDCHAR).b, 'A');       // 1 square (with a width operand)
        // 2: circle as four rrcurvetos
        addGlyph(CsB().ns({300, 0}).op(RMOVETO)
                     .ns({0, 166, -134, 134, -166, 0,   -166, 0, -134, -134, 0, -166,   0, -166, 134, -134, 166, 0,   166, 0, 134, 134, 0, 166}).op(RRCURVETO)
                     .op(ENDCHAR).b, 'B');
        // 3: the same circle via alternating vhcurveto
        addGlyph(CsB().ns({300, 0}).op(RMOVETO)
                     .ns({166, -134, 134, -166,   -166, -134, -134, -166,   -166, 134, -134, 166,   166, 134, 134, 166}).op(VHCURVETO)
                     .op(ENDCHAR).b, 'C');
        // 4: the same square, with stems + a hint mask (one mask byte that must be skipped)
        addGlyph(CsB().ns({0, 100}).op(HSTEMHM).ns({50, 100}).op(HINTMASK).raw(0xC0)
                     .ns({0, 0}).op(RMOVETO).ns({500, 500, -500}).op(HLINETO).op(ENDCHAR).b, 'D');
        // 5: the square through a local subr, 6: through a global subr
        addGlyph(CsB().ns({0, 0}).op(RMOVETO).n(-107).op(CALLSUBR).op(ENDCHAR).b, 'E');
        addGlyph(CsB().ns({0, 0}).op(RMOVETO).n(-107).op(CALLGSUBR).op(ENDCHAR).b, 'F');
        // 7: the square drawn with hlineto / vlineto / rlineto variants mixed + closing without endchar close
        addGlyph(CsB().ns({0, 0}).op(RMOVETO).n(500).op(HLINETO).n(500).op(VLINETO).ns({-500, 0}).op(RLINETO).op(ENDCHAR).b, 'G');
        // 8..: flex family vs. the equivalent rrcurvetos. Every pair must draw the same.
        const Bytes tail = CsB().n(300).op(VLINETO).n(-400).op(HLINETO).op(ENDCHAR).b;
        auto withCurve = [&](CsB curve) { CsB b; b.ns({0, 0}).op(RMOVETO); b.b.insert(b.b.end(), curve.b.begin(), curve.b.end()); b.b.insert(b.b.end(), tail.begin(), tail.end()); return b.b; };
        addGlyph(withCurve(CsB().ns({100, 120, 80, 60, 100, 90, 110}).op(HFLEX)), 'H');                        // 8
        addGlyph(withCurve(CsB().ns({100, 0, 120, 80, 60, 0,   100, 0, 90, -80, 110, 0}).op(RRCURVETO)), 'I');    // 9: hflex = these two curves
        addGlyph(withCurve(CsB().ns({10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 110, 120, 50}).op(FLEX)), 'J');   // 10
        addGlyph(withCurve(CsB().ns({10, 20, 30, 40, 50, 60,   70, 80, 90, 100, 110, 120}).op(RRCURVETO)), 'K'); // 11
        addGlyph(withCurve(CsB().ns({10, 20, 30, 40, 50, 60, 70, 80, 90}).op(HFLEX1)), 'L');                   // 12
        addGlyph(withCurve(CsB().ns({10, 20, 30, 40, 50, 0,   60, 0, 70, 80, 90, -(20 + 40 + 80)}).op(RRCURVETO)), 'M');   // 13
        // flex1: dx = 250, dy = 300: |dy| > |dx|, so the last point is (x - dx_total, y5 + d6)
        addGlyph(withCurve(CsB().ns({10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 110}).op(FLEX1)), 'N');          // 14
        addGlyph(withCurve(CsB().ns({10, 20, 30, 40, 50, 60,   70, 80, 90, 100, -250, 110}).op(RRCURVETO)), 'O'); // 15
        // hhcurveto / vvcurveto / hvcurveto (with its optional last argument) against plain rrcurveto
        addGlyph(withCurve(CsB().ns({20, 100, 50, 60, 70}).op(HHCURVETO)), 'P');                               // 16: dy1 = 20
        addGlyph(withCurve(CsB().ns({100, 20, 50, 60, 70, 0}).op(RRCURVETO)), 'Q');                            // 17
        addGlyph(withCurve(CsB().ns({100, 50, 60, 70, 20}).op(VVCURVETO)), 'R');                               // 18: dx1 = 100
        addGlyph(withCurve(CsB().ns({100, 50, 60, 70, 0, 20}).op(RRCURVETO)), 'S');                            // 19
        addGlyph(withCurve(CsB().ns({100, 50, 60, 70, 20}).op(HVCURVETO)), 'T');                               // 20: dxf = 20
        addGlyph(withCurve(CsB().ns({100, 0, 50, 60, 20, 70}).op(RRCURVETO)), 'U');                            // 21
        CHECK(c.charstrings.size() == 22);
        c.localSubrs = {CsB().ns({500, 500, -500}).op(HLINETO).op(RETURN).b};
        c.gsubrs = {CsB().ns({500, 500, -500}).op(HLINETO).op(RETURN).b};
        s.glyphs[0] = Notdef();
        Bytes font = MakeOtf(s, c);
        ttf::Face f;
        CHECK(f.Load(font.data(), font.size()));
        CHECK(f.IsCff() && f.GlyphCount() == (int)c.charstrings.size());
        Bmp sq = Draw(f, 1, 100.0f);
        CHECK(sq.ok && sq.w == 50 && sq.h == 50);
        if (sq.ok) CHECK_NEAR(sq.Sum(), 2500.0, 0.5);
        for (int g : {4, 5, 6, 7}) { Bmp b = Draw(f, g, 100.0f); CHECK(SameBmp(sq, b)); }
        Bmp c1 = Draw(f, 2, 200.0f), c2 = Draw(f, 3, 200.0f);   // scale 0.2: r = 60px
        CHECK(c1.ok && c1.w == 120 && c1.h == 120);
        if (c1.ok) CHECK_NEAR(c1.Sum(), 3.14159265 * 60 * 60, 3.14159265 * 3600 * 0.003);
        CHECK(SameBmp(c1, c2));
        CHECK(SameBmp(Draw(f, 8, 160.0f), Draw(f, 9, 160.0f)));       // hflex
        CHECK(SameBmp(Draw(f, 10, 160.0f), Draw(f, 11, 160.0f)));     // flex
        CHECK(SameBmp(Draw(f, 12, 160.0f), Draw(f, 13, 160.0f)));     // hflex1
        CHECK(SameBmp(Draw(f, 14, 160.0f), Draw(f, 15, 160.0f)));     // flex1 (|dy| > |dx|: x returns to the start column)
        CHECK(Draw(f, 8, 160.0f).ok && Draw(f, 14, 160.0f).ok);
        CHECK(SameBmp(Draw(f, 16, 160.0f), Draw(f, 17, 160.0f)));     // hhcurveto
        CHECK(SameBmp(Draw(f, 18, 160.0f), Draw(f, 19, 160.0f)));     // vvcurveto
        CHECK(SameBmp(Draw(f, 20, 160.0f), Draw(f, 21, 160.0f)));     // hvcurveto
        CHECK(f.FindGlyph('A') == 1 && f.FindGlyph('U') == 21);
        int adv, lsb;
        CHECK(f.HMetrics(1, adv, lsb));
    }

    // ---- CID-keyed CFF: per-glyph font dict (FDSelect formats 0 and 3) with its own local subrs ------
    for (int fmt : {0, 3}) {
        CffSpec c; Spec s;
        c.cid = true; c.fdSelectFmt = fmt;
        c.fdSubrs = {{CsB().ns({500, 500, -500}).op(HLINETO).op(RETURN).b},      // fd 0: a 500 square
                     {CsB().ns({300, 300, -300}).op(HLINETO).op(RETURN).b}};     // fd 1: a 300 square
        c.charstrings = {CsB().op(ENDCHAR).b,
                         CsB().ns({0, 0}).op(RMOVETO).n(-107).op(CALLSUBR).op(ENDCHAR).b,
                         CsB().ns({0, 0}).op(RMOVETO).n(-107).op(CALLSUBR).op(ENDCHAR).b,
                         CsB().ns({0, 0}).op(RMOVETO).n(-107).op(CALLSUBR).op(ENDCHAR).b};
        c.fdOfGlyph = {0, 0, 1, 0};
        for (int i = 0; i < 4; ++i) { Glyph g; s.glyphs.push_back(g); }
        s.map = {{'A', 1}, {'B', 2}, {'C', 3}};
        Bytes font = MakeOtf(s, c);
        ttf::Face f;
        CHECK(f.Load(font.data(), font.size()));
        Bmp a = Draw(f, 1, 100.0f), b = Draw(f, 2, 100.0f), d = Draw(f, 3, 100.0f);
        CHECK(a.ok && b.ok && d.ok);
        if (a.ok && b.ok) { CHECK(a.w == 50 && b.w == 30); CHECK(SameBmp(a, d)); }
    }

    // ---- TrueType collections ------------------------------------------------------------------------
    {
        Spec s0, s1;
        s0.glyphs = {Notdef(), {}}; s0.glyphs[1].data = SimpleGlyph({Rect(0, 0, 200, 200)}); s0.map = {{'A', 1}};
        s1.glyphs = {Notdef(), {}, {}}; s1.glyphs[1].data = SimpleGlyph({Rect(0, 0, 100, 100)}); s1.glyphs[2].data = SimpleGlyph({Rect(0, 0, 300, 300)}); s1.map = {{'A', 2}, {'B', 1}};
        Bytes file;
        P32(file, 0x74746366); P32(file, 0x00010000); P32(file, 2); P32(file, 0); P32(file, 0);
        const std::size_t d0 = AppendSfnt(file, GlyfTables(s0), 0x00010000);
        const std::size_t d1 = AppendSfnt(file, GlyfTables(s1), 0x00010000);
        Put32At(file, 12, (std::uint32_t)d0); Put32At(file, 16, (std::uint32_t)d1);
        CHECK(ttf::Face::FaceCount(file.data(), file.size()) == 2);
        ttf::Face a, b, c;
        CHECK(a.Load(file.data(), file.size(), 0) && b.Load(file.data(), file.size(), 1));
        CHECK(!c.Load(file.data(), file.size(), 2) && !c.Load(file.data(), file.size(), -1));
        CHECK(a.GlyphCount() == 2 && b.GlyphCount() == 3);
        CHECK(a.FindGlyph('B') == 0 && b.FindGlyph('B') == 1 && a.FindGlyph('A') == 1 && b.FindGlyph('A') == 2);
        CHECK(Draw(b, 2, 100.0f).w == 30);
        Bytes plain = MakeTtf(s0);
        CHECK(ttf::Face::FaceCount(plain.data(), plain.size()) == 1);
        ttf::Face p;
        CHECK(!p.Load(plain.data(), plain.size(), 1));
        CHECK(ttf::Face::FaceCount(plain.data(), 8) == 0);
        Bytes junk(100, 0x55);
        CHECK(ttf::Face::FaceCount(junk.data(), junk.size()) == 0 && !p.Load(junk.data(), junk.size()));
        CHECK(!p.Load(nullptr, 0) && !p.Load(plain.data(), 0));
        // A huge face count must not run off the end.
        Bytes lie = file; Put32At(lie, 8, 0x7FFFFFFF);
        CHECK(ttf::Face::FaceCount(lie.data(), lie.size()) == 0 && !p.Load(lie.data(), lie.size()));
    }

    // ---- Atlas helpers -----------------------------------------------------------------------------------
    {
        ttf::ShelfPacker p(32, 32, 1);
        int x, y;
        CHECK(p.Place(10, 5, x, y) && x == 1 && y == 1);
        CHECK(p.Place(10, 8, x, y) && x == 12 && y == 1);
        CHECK(p.Place(10, 3, x, y) && x == 1 && y == 10 && p.Bottom() == 14);   // 23 + 10 + pad > 32: a new shelf under the tallest glyph
        CHECK(p.Place(10, 3, x, y) && x == 12 && y == 10);
        CHECK(p.Place(0, 0, x, y) && x == 0 && y == 0);     // nothing to place takes no room
        CHECK(!p.Place(40, 3, x, y));                       // wider than the atlas
        CHECK(!p.Place(5, 30, x, y));                       // out of rows

        // Baked glyphs sit inside the atlas, never overlap, and carry the pen metrics.
        Spec s;
        s.glyphs.push_back(Notdef());
        for (int i = 0; i < 6; ++i) { Glyph g; g.data = SimpleGlyph({Rect(0, 0, 300 + 40 * i, 500)}); g.adv = 500 + i; s.glyphs.push_back(g); }
        { Glyph g; g.adv = 250; s.glyphs.push_back(g); }
        Bytes font = MakeTtf(s);
        ttf::Face f;
        CHECK(f.Load(font.data(), font.size()));
        std::vector<std::uint8_t> atlas(128 * 128, 0);
        ttf::ShelfPacker pk(128, 128, 1);
        std::vector<ttf::BakedGlyph> bg(8);
        bool ok = true;
        for (int i = 0; i < 8; ++i) ok = ttf::BakeGlyph(f, i, 0.05f, pk, atlas.data(), bg[(std::size_t)i]) && ok;
        CHECK(ok);
        for (int i = 0; i < 8; ++i) {
            const auto& g = bg[(std::size_t)i];
            CHECK(g.x0 >= 0 && g.y0 >= 0 && g.x1 <= 128 && g.y1 <= 128);
            for (int j = 0; j < i; ++j) {
                const auto& h = bg[(std::size_t)j];
                if (g.x1 > g.x0 && h.x1 > h.x0) CHECK(g.x1 + 0 <= h.x0 || h.x1 <= g.x0 || g.y1 <= h.y0 || h.y1 <= g.y0);
            }
        }
        CHECK(bg[1].x1 - bg[1].x0 == 15 && bg[1].y1 - bg[1].y0 == 25);
        CHECK(bg[1].xoff == 0.0f && bg[1].yoff == -25.0f);
        CHECK_NEAR(bg[3].xadvance, 502 * 0.05, 1e-4);
        CHECK(bg[7].x1 == 0 && bg[7].xadvance > 0.0f);   // the space: advance only
        CHECK(atlas[(std::size_t)bg[1].y0 * 128 + bg[1].x0 + 3] == 255);
        // A full atlas says so.
        ttf::ShelfPacker tiny(16, 16, 1);
        ttf::BakedGlyph one;
        std::vector<std::uint8_t> tinyAtlas(16 * 16, 0);
        CHECK(!ttf::BakeGlyph(f, 6, 0.05f, tiny, tinyAtlas.data(), one));
    }

    // ---- Real font: DejaVu Sans (assets/fonts) -----------------------------------------------------------------
    const std::string dejavuPath = std::string(OKAY_SOURCE_DIR) + "/assets/fonts/DejaVuSans.ttf";
    Bytes dejavu = ReadFile(dejavuPath);
    CHECK(dejavu.size() > 100000);
    {
        ttf::Face f;
        CHECK(f.Load(dejavu.data(), dejavu.size()));
        CHECK(f.GlyphCount() == 6253 && f.UnitsPerEm() == 2048);
        int a, d, g; f.VMetrics(a, d, g);
        CHECK(a == 1901 && d == -483 && g == 0);
        const int A = f.FindGlyph('A');
        CHECK(A == 36);
        int adv, lsb;
        CHECK(f.HMetrics(A, adv, lsb) && adv == 1401 && lsb == 16);
        CHECK(f.FindGlyph(0x4E00) == 0 && f.FindGlyph(0xE9) != 0 && f.FindGlyph(0x416) != 0);

        // Reference bitmaps: box exact, coverage within a small error of exact-area truth.
        double sumAll = 0; long nAll = 0; int worst = 0;
        for (const Ref& r : kRefs) {
            Bmp b = Draw(f, f.FindGlyph(r.cp), r.px);
            CHECK(b.ok && b.x0 == r.x0 && b.y0 == r.y0 && b.w == r.w && b.h == r.h);
            std::vector<std::uint8_t> want = FromHex(r.hex);
            CHECK(want.size() == (std::size_t)r.w * r.h);
            if (!b.ok || want.size() != b.px.size()) continue;
            double sum = 0; int mx = 0;
            for (std::size_t i = 0; i < want.size(); ++i) { const int e = std::abs((int)b.px[i] - (int)want[i]); sum += e; mx = std::max(mx, e); }
            CHECK(sum / (double)want.size() < 0.6);   // mean error, in 1/255ths
            CHECK(mx <= 8);
            sumAll += sum; nAll += (long)want.size(); worst = std::max(worst, mx);
        }
        std::printf("[test_ttf_rasterizer] DejaVu references: mean error %.3f/255, worst %d/255\n", nAll ? sumAll / (double)nAll : 0.0, worst);

        // Every glyph at several sizes renders without trouble; coverage stays in the box.
        long drawn = 0;
        for (int gi = 0; gi < f.GlyphCount(); gi += 5) {
            for (float px : {9.0f, 17.0f, 41.0f}) {
                Bmp b = Draw(f, gi, px);
                if (b.ok) ++drawn;
            }
        }
        CHECK(drawn > 3000);
        // A composite (e-acute) is a base letter plus an accent: taller than the plain e.
        Bmp e = Draw(f, f.FindGlyph('e'), 32.0f), ea = Draw(f, f.FindGlyph(0xE9), 32.0f);
        CHECK(e.ok && ea.ok && ea.h > e.h && ea.w == e.w);
    }

#ifndef OKAY_TTF_STANDALONE
    // ---- TtfFont (the atlas wrapper) over the new rasterizer -----------------------------------------------------
    {
        TtfFont font;
        CHECK(font.LoadFromFile(dejavuPath, 48.0f));
        const float scale = 48.0f / (1901 + 483);
        const TtfFont::Glyph* A = font.Get('A');
        CHECK(A != nullptr);
        if (A) { CHECK_NEAR(A->xadvance, 1401 * scale, 1e-3); CHECK(A->x1 > A->x0 && A->y1 > A->y0 && A->yoff < 0); }
        CHECK_NEAR(font.LineHeight(48.0f), (1901 + 483) * scale, 1e-3);
        CHECK(font.Has(0x416) && !font.Has(0x4E00));
        const TtfFont::Glyph* zh = font.GlyphFor(0x416);
        CHECK(zh && zh->x1 > zh->x0);
        CHECK(font.GlyphFor(0x4E00) == nullptr);
        CHECK(font.Measure("AAAA", 48.0f) > font.Measure("AA", 48.0f));
        TtfFont bad;
        const unsigned char junk[64] = {1, 2, 3};
        CHECK(!bad.LoadFromMemory(junk, sizeof junk));
        CHECK(!bad.Valid() && bad.GlyphFor(0x416) == nullptr && bad.Measure("x", 12.0f) == 0.0f);
        // The built-in launcher font (Roboto) loads too.
        TtfFont* ui = BuiltinUIFont();
        CHECK(ui != nullptr && ui->Valid() && ui->Get('A') != nullptr);
    }

#endif

    // ---- Corrupt, truncated and mutated files: no crash, no hang -----------------------------------------------------
    {
        Spec s;
        s.glyphs.push_back(Notdef());
        { Glyph g; g.data = SimpleGlyph({Rect(0, 0, 500, 500), {{50, 50, false}, {400, 50, true}, {400, 400, false}}}); s.glyphs.push_back(g); }
        { Comp a; a.glyph = 1; Comp b; b.glyph = 1; b.a1 = 100; b.a2 = 100; b.kind = 3; b.m[0] = 0.9; b.m[1] = 0.1; b.m[2] = -0.1; b.m[3] = 0.9;
          Glyph g; g.data = CompositeGlyph({a, b}, 700); s.glyphs.push_back(g); }
        s.map = {{'A', 1}, {'B', 2}};
        const Bytes ttfFont = MakeTtf(s);
        CffSpec c;
        c.charstrings = {CsB().op(ENDCHAR).b,
                         CsB().ns({300, 0}).op(RMOVETO).ns({0, 166, -134, 134, -166, 0, -166, 0, -134, -134, 0, -166}).op(RRCURVETO).n(-107).op(CALLSUBR).op(ENDCHAR).b,
                         CsB().ns({0, 100}).op(HSTEMHM).ns({50, 100}).op(HINTMASK).raw(0xC0).ns({0, 0}).op(RMOVETO).n(-107).op(CALLGSUBR).op(ENDCHAR).b};
        c.localSubrs = {CsB().ns({500, 500, -500}).op(HLINETO).op(RETURN).b};
        c.gsubrs = {CsB().ns({500, 500, -500}).op(HLINETO).op(RETURN).b};
        Spec so = s; so.glyphs.resize(3);
        const Bytes otfFont = MakeOtf(so, c);

        int loaded = 0;
        const Bytes* const bases[] = {&ttfFont, &otfFont, &dejavu};
        for (const Bytes* base : bases) {
            const bool large = base == &dejavu;
            // Truncation at many lengths.
            const std::size_t step = large ? 9973 : 1;
            for (std::size_t n = 0; n <= base->size(); n += step) {
                Bytes cut(base->begin(), base->begin() + (std::ptrdiff_t)n);
                Exercise(cut, large ? 40 : 30);
            }
            // Random byte / word mutations, biased towards the front (directory, head, hhea ...).
            const int iters = large ? 150 : 1500;
            for (int it = 0; it < iters; ++it) {
                Bytes m = *base;
                const int hits = 1 + (int)(Rnd() % 4);
                for (int k = 0; k < hits; ++k) {
                    std::size_t pos = (Rnd() % 3 == 0) ? Rnd() % std::min<std::size_t>(m.size(), 400) : Rnd() % m.size();
                    switch (Rnd() % 4) {
                    case 0: m[pos] = (std::uint8_t)Rnd(); break;
                    case 1: m[pos] = 0xFF; break;
                    case 2: m[pos] = 0; break;
                    default: if (pos + 1 < m.size()) { m[pos] = 0x7F; m[pos + 1] = 0xFF; } break;
                    }
                }
                ttf::Face f; if (f.Load(m.data(), m.size())) ++loaded;
                Exercise(m, large ? 40 : 30);
            }
        }
        CHECK(loaded > 0);
        // All-zero / all-0xFF / empty data, and a table directory pointing far outside the file.
        for (std::uint8_t fill : {0x00, 0xFF}) { Bytes z(4096, fill); Exercise(z, 10); }
        Bytes evil = ttfFont;
        for (std::size_t i = 12; i + 16 <= 12 + 7 * 16; i += 16) { Put32At(evil, i + 8, 0xFFFFFFF0u); Put32At(evil, i + 12, 0xFFFFFFFFu); }
        ttf::Face ef;
        CHECK(!ef.Load(evil.data(), evil.size()));
        Exercise(evil, 10);
        // Rendering one glyph at very large and very clipped scales.
        {
            ttf::Face f; f.Load(dejavu.data(), dejavu.size());
            const int gi = f.FindGlyph('W');
            int x0, y0, x1, y1;
            CHECK(f.GlyphBox(gi, 1e-9f, x0, y0, x1, y1) && x1 - x0 <= 1 && y1 - y0 <= 1);   // a speck, not a crash
            Bmp huge = Draw(f, gi, 3000.0f);   // ~3000 px tall: under the cap, must render
            CHECK(huge.ok && huge.h > 1500);
        }
    }

    TEST_MAIN_RESULT();
}
