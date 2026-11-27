// SVG renderer (part of Core/ImageDecode). Written from the W3C specifications only: SVG 1.1 (Second Edition) and
// SVG 2 (document structure, coordinate systems and viewBox / preserveAspectRatio, the path data grammar and the
// elliptical arc implementation notes, basic shapes, painting: fill, stroke, markers excluded, gradients, clipping
// paths) and CSS Color 4 for colour values. No code from librsvg, resvg, nanosvg, Skia, cairo, a browser or any other
// project was read, copied or ported.
//
// What is drawn: svg (nested too), g, a, switch (its first child), use (with symbol), defs, path (full grammar),
// rect (rounded), circle, ellipse, line, polyline, polygon; fill and stroke with colours, currentColor and
// linear / radial gradients (units, transforms, spread methods, href inheritance); fill and clip rules, opacities;
// stroke width, caps, joins, miter limit, dashes; transforms; clip-path (union of shapes); presentation attributes,
// style attributes and <style> sheets with simple selectors (tag, .class, #id, *, compounds of those).
// Not drawn (skipped quietly): text, image, filters, masks, patterns, markers, foreignObject, animation, CSS
// selectors with combinators, pseudo-classes or attributes, and @media blocks. Group opacity is applied to each
// shape inside the group, not to the group as one layer.
//
// Anti-aliasing comes from the engine's one coverage rasterizer (okay/Graphics/CoverageRaster.hpp), XML from the
// strict small reader the model importers use (engine/src/Render/XmlLite.hpp: no DTD, no entity expansion), colours
// from the shared CSS colour parser (okay/Core/CssColor.hpp).
//
// Hostile input: the file, the element count and the nesting are capped by the XML reader; <use> chains are cut at a
// depth of 16 and every drawn shape, every path command and every pixel of work counts against a budget, so a file
// that expands exponentially stops drawing instead of hanging. The picture is at most 4096 pixels a side.
#include "ImageDecodeInternal.hpp"
#include "okay/Core/CssColor.hpp"
#include "okay/Core/ImageDecode.hpp"
#include "okay/Core/StringUtil.hpp"
#include "okay/Graphics/CoverageRaster.hpp"
#include "../Render/XmlLite.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace okay {
namespace imgdec {

namespace {

namespace xml = okay::detail::xml;

constexpr int kMaxSide = 4096;
constexpr int kMaxDepth = 64;                       // groups inside groups (and nested svg)
constexpr int kMaxUseDepth = 16;                    // <use> inside <use>
constexpr long long kMaxShapes = 50000;             // shapes drawn, <use> copies included (about 1 s of work)
constexpr long long kMaxCommands = 4000000;         // path commands read, all shapes together
constexpr long long kMaxPixelWork = 1LL << 31;      // pixels touched by fills, all shapes together (also at most
                                                    // 2^26 + 64 times the picture's pixels: see Renderer)
constexpr double kPi = 3.14159265358979323846;

bool Fail(std::string& err, const char* msg) { if (err.empty()) err = msg; return false; }

// ---- small parsing helpers --------------------------------------------------------------------------------------
bool IsSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'; }
void SkipSpace(const char*& p, const char* e) { while (p < e && IsSpace(*p)) ++p; }
void SkipSep(const char*& p, const char* e) {   // white space with at most one comma
    SkipSpace(p, e);
    if (p < e && *p == ',') { ++p; SkipSpace(p, e); }
}

// A number of the SVG grammar: [+-]? (digits ('.' digits?)? | '.' digits) ([eE] [+-]? digits)?. Computed here (not
// with strtod) so the decimal point never depends on the C locale.
bool ReadSvgNumber(const char*& p, const char* e, double& out) {
    const char* q = p;
    bool neg = false;
    if (q < e && (*q == '+' || *q == '-')) { neg = *q == '-'; ++q; }
    double mant = 0;
    int digits = 0, scale = 0;
    while (q < e && *q >= '0' && *q <= '9') { if (digits < 18) mant = mant * 10 + (*q - '0'); else ++scale; ++digits; ++q; }
    if (q < e && *q == '.') {
        ++q;
        while (q < e && *q >= '0' && *q <= '9') { if (digits < 18) { mant = mant * 10 + (*q - '0'); --scale; } ++digits; ++q; }
    }
    if (digits == 0) return false;
    if (q < e && (*q == 'e' || *q == 'E')) {
        const char* r = q + 1;
        bool eneg = false;
        if (r < e && (*r == '+' || *r == '-')) { eneg = *r == '-'; ++r; }
        if (r < e && *r >= '0' && *r <= '9') {
            int ex = 0;
            while (r < e && *r >= '0' && *r <= '9') { if (ex < 10000) ex = ex * 10 + (*r - '0'); ++r; }
            scale += eneg ? -ex : ex;
            q = r;
        }
    }
    if (scale > 300) scale = 300;
    if (scale < -300) scale = -300;
    double v = mant * std::pow(10.0, scale);
    if (!std::isfinite(v)) v = 1e300;
    out = neg ? -v : v;
    p = q;
    return true;
}

using okay::str::Trim;

std::vector<double> NumberList(const std::string& s) {
    std::vector<double> v;
    const char* p = s.data();
    const char* e = p + s.size();
    SkipSpace(p, e);
    double d;
    while (p < e && ReadSvgNumber(p, e, d)) { v.push_back(d); SkipSep(p, e); }
    return v;
}

// ---- 2D affine transforms: x' = a x + c y + e, y' = b x + d y + f ------------------------------------------------
struct Mat {
    double a = 1, b = 0, c = 0, d = 1, e = 0, f = 0;
    // this * o: o is applied first
    Mat operator*(const Mat& o) const {
        return Mat{a * o.a + c * o.b, b * o.a + d * o.b, a * o.c + c * o.d, b * o.c + d * o.d, a * o.e + c * o.f + e, b * o.e + d * o.f + f};
    }
    void Apply(double x, double y, double& ox, double& oy) const { ox = a * x + c * y + e; oy = b * x + d * y + f; }
    double Det() const { return a * d - b * c; }
    bool Invert(Mat& out) const {
        const double det = Det();
        if (!(std::fabs(det) > 1e-12) || !std::isfinite(det)) return false;
        const double id = 1.0 / det;
        out = Mat{d * id, -b * id, -c * id, a * id, (c * f - d * e) * id, (b * e - a * f) * id};
        return true;
    }
    double Scale() const { return std::sqrt(std::max(1e-12, (a * a + b * b + c * c + d * d) * 0.5)); }
    static Mat Translate(double x, double y) { return Mat{1, 0, 0, 1, x, y}; }
    static Mat ScaleBy(double x, double y) { return Mat{x, 0, 0, y, 0, 0}; }
};

// The transform list grammar: matrix, translate, scale, rotate, skewX, skewY (SVG 1.1 7.6; units such as px and deg,
// which the CSS transform property allows, are accepted and ignored).
bool ParseTransformList(const std::string& s, Mat& out) {
    Mat m;
    const char* p = s.data();
    const char* e = p + s.size();
    while (true) {
        SkipSep(p, e);
        if (p >= e) break;
        const char* name = p;
        while (p < e && ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z'))) ++p;
        const std::string fn(name, std::size_t(p - name));
        SkipSpace(p, e);
        if (fn.empty() || p >= e || *p != '(') return false;
        ++p;
        double v[6];
        int n = 0;
        while (true) {
            SkipSep(p, e);
            if (p < e && *p == ')') { ++p; break; }
            double d;
            if (n >= 6 || !ReadSvgNumber(p, e, d)) return false;
            v[n++] = d;
            while (p < e && ((*p >= 'a' && *p <= 'z') || *p == '%')) ++p;   // px, deg
        }
        Mat t;
        if (fn == "matrix" && n == 6) t = Mat{v[0], v[1], v[2], v[3], v[4], v[5]};
        else if (fn == "translate" && (n == 1 || n == 2)) t = Mat::Translate(v[0], n == 2 ? v[1] : 0);
        else if (fn == "scale" && (n == 1 || n == 2)) t = Mat::ScaleBy(v[0], n == 2 ? v[1] : v[0]);
        else if (fn == "rotate" && (n == 1 || n == 3)) {
            const double r = v[0] * kPi / 180, cs = std::cos(r), sn = std::sin(r);
            t = Mat{cs, sn, -sn, cs, 0, 0};
            if (n == 3) t = Mat::Translate(v[1], v[2]) * t * Mat::Translate(-v[1], -v[2]);
        } else if (fn == "skewX" && n == 1) t = Mat{1, 0, std::tan(v[0] * kPi / 180), 1, 0, 0};
        else if (fn == "skewY" && n == 1) t = Mat{1, std::tan(v[0] * kPi / 180), 0, 1, 0, 0};
        else return false;
        m = m * t;
    }
    out = m;
    return true;
}

// ---- paths -------------------------------------------------------------------------------------------------------
enum Verb : std::uint8_t { kMove, kLine, kCubic, kClose };

struct Path {
    std::vector<std::uint8_t> verbs;
    std::vector<double> xy;   // 2 numbers per move / line, 6 per cubic
    double cx = 0, cy = 0, sx = 0, sy = 0;
    bool started = false;
    void Move(double x, double y) { verbs.push_back(kMove); xy.push_back(x); xy.push_back(y); cx = sx = x; cy = sy = y; started = true; }
    void Line(double x, double y) {
        if (!started) Move(cx, cy);
        verbs.push_back(kLine); xy.push_back(x); xy.push_back(y); cx = x; cy = y;
    }
    void Cubic(double x1, double y1, double x2, double y2, double x, double y) {
        if (!started) Move(cx, cy);
        verbs.push_back(kCubic);
        xy.insert(xy.end(), {x1, y1, x2, y2, x, y});
        cx = x; cy = y;
    }
    void Quad(double qx, double qy, double x, double y) {
        Cubic(cx + 2.0 / 3 * (qx - cx), cy + 2.0 / 3 * (qy - cy), x + 2.0 / 3 * (qx - x), y + 2.0 / 3 * (qy - y), x, y);
    }
    void Close() { if (started) { verbs.push_back(kClose); cx = sx; cy = sy; } }
    // Elliptical arc from the current point, by the endpoint-to-centre conversion of SVG 1.1 appendix F.6.5-F.6.6,
    // as cubic pieces of at most 90 degrees.
    void Arc(double rx, double ry, double phiDeg, bool large, bool sweep, double x, double y) {
        const double x1 = cx, y1 = cy;
        if (x1 == x && y1 == y) return;
        rx = std::fabs(rx); ry = std::fabs(ry);
        if (rx == 0 || ry == 0) { Line(x, y); return; }
        const double phi = std::fmod(phiDeg, 360.0) * kPi / 180, cs = std::cos(phi), sn = std::sin(phi);
        const double dx = (x1 - x) / 2, dy = (y1 - y) / 2;
        const double xp = cs * dx + sn * dy, yp = -sn * dx + cs * dy;
        const double lam = (xp * xp) / (rx * rx) + (yp * yp) / (ry * ry);
        if (lam > 1) { const double k = std::sqrt(lam); rx *= k; ry *= k; }
        const double num = rx * rx * ry * ry - rx * rx * yp * yp - ry * ry * xp * xp;
        const double den = rx * rx * yp * yp + ry * ry * xp * xp;
        double coef = den > 0 ? std::sqrt(std::max(0.0, num / den)) : 0;
        if (large == sweep) coef = -coef;
        const double cxp = coef * rx * yp / ry, cyp = -coef * ry * xp / rx;
        const double ccx = cs * cxp - sn * cyp + (x1 + x) / 2, ccy = sn * cxp + cs * cyp + (y1 + y) / 2;
        auto angle = [](double ux, double uy, double vx, double vy) { return std::atan2(ux * vy - uy * vx, ux * vx + uy * vy); };
        const double th1 = angle(1, 0, (xp - cxp) / rx, (yp - cyp) / ry);
        double dth = angle((xp - cxp) / rx, (yp - cyp) / ry, (-xp - cxp) / rx, (-yp - cyp) / ry);
        if (!sweep && dth > 0) dth -= 2 * kPi;
        else if (sweep && dth < 0) dth += 2 * kPi;
        if (!std::isfinite(dth) || !std::isfinite(th1)) { Line(x, y); return; }
        const int n = std::max(1, std::min(64, int(std::ceil(std::fabs(dth) / (kPi / 2) - 1e-9))));
        const double step = dth / n, k = 4.0 / 3.0 * std::tan(step / 4);
        double t = th1;
        for (int i = 0; i < n; ++i) {
            const double t2 = t + step;
            const double c1 = std::cos(t), s1 = std::sin(t), c2 = std::cos(t2), s2 = std::sin(t2);
            // unit-circle points and tangents, then the ellipse's scale, rotation and centre
            auto map = [&](double ux, double uy, double& ox, double& oy) {
                const double ex = rx * ux, ey = ry * uy;
                ox = cs * ex - sn * ey + ccx; oy = sn * ex + cs * ey + ccy;
            };
            double ax, ay, bx, by, px, py;
            map(c1 - k * s1, s1 + k * c1, ax, ay);
            map(c2 + k * s2, s2 - k * c2, bx, by);
            if (i == n - 1) { px = x; py = y; } else map(c2, s2, px, py);
            Cubic(ax, ay, bx, by, px, py);
            t = t2;
        }
    }
    void Ellipse(double ecx, double ecy, double rx, double ry) {
        const double k = 0.5522847498307936;   // 4/3 (sqrt(2) - 1): a quarter circle as one cubic
        Move(ecx + rx, ecy);
        Cubic(ecx + rx, ecy + k * ry, ecx + k * rx, ecy + ry, ecx, ecy + ry);
        Cubic(ecx - k * rx, ecy + ry, ecx - rx, ecy + k * ry, ecx - rx, ecy);
        Cubic(ecx - rx, ecy - k * ry, ecx - k * rx, ecy - ry, ecx, ecy - ry);
        Cubic(ecx + k * rx, ecy - ry, ecx + rx, ecy - k * ry, ecx + rx, ecy);
        Close();
    }
};

// Path data (SVG 1.1 8.3 and its BNF; SVG 2 9.3). On an error the path is drawn up to the last good command.
void ParsePathData(const std::string& d, Path& path, long long& budget) {
    const char* p = d.data();
    const char* e = p + d.size();
    char cmd = 0;
    double lcx = 0, lcy = 0;   // last control point, for S / T
    char lastKind = 0;          // 'C' after C/S, 'Q' after Q/T
    while (true) {
        SkipSpace(p, e);
        if (p >= e) return;
        if (--budget < 0) return;
        const char c = *p;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
            if (!std::strchr("MmLlHhVvCcSsQqTtAaZz", c)) return;
            cmd = c;
            ++p;
            if (cmd == 'Z' || cmd == 'z') { path.Close(); lastKind = 0; continue; }
        } else if (!cmd || cmd == 'Z' || cmd == 'z') {
            return;   // numbers with no command
        }
        const bool rel = cmd >= 'a';
        const double ox = rel ? path.cx : 0, oy = rel ? path.cy : 0;
        double v[7];
        auto nums = [&](int n) {
            for (int i = 0; i < n; ++i) { SkipSep(p, e); if (!ReadSvgNumber(p, e, v[i])) return false; }
            return true;
        };
        auto flag = [&](double& out) {
            SkipSep(p, e);
            if (p < e && (*p == '0' || *p == '1')) { out = *p - '0'; ++p; return true; }
            return false;
        };
        char kind = 0;
        switch (cmd | 0x20) {
            case 'm':
                if (!nums(2)) return;
                path.Move(ox + v[0], oy + v[1]);
                cmd = rel ? 'l' : 'L';   // further pairs are lines
                break;
            case 'l':
                if (!nums(2)) return;
                path.Line(ox + v[0], oy + v[1]);
                break;
            case 'h':
                if (!nums(1)) return;
                path.Line(ox + v[0], path.cy);
                break;
            case 'v':
                if (!nums(1)) return;
                path.Line(path.cx, oy + v[0]);
                break;
            case 'c':
                if (!nums(6)) return;
                path.Cubic(ox + v[0], oy + v[1], ox + v[2], oy + v[3], ox + v[4], oy + v[5]);
                lcx = ox + v[2]; lcy = oy + v[3]; kind = 'C';
                break;
            case 's': {
                if (!nums(4)) return;
                const double x1 = lastKind == 'C' ? 2 * path.cx - lcx : path.cx, y1 = lastKind == 'C' ? 2 * path.cy - lcy : path.cy;
                path.Cubic(x1, y1, ox + v[0], oy + v[1], ox + v[2], oy + v[3]);
                lcx = ox + v[0]; lcy = oy + v[1]; kind = 'C';
                break;
            }
            case 'q':
                if (!nums(4)) return;
                path.Quad(ox + v[0], oy + v[1], ox + v[2], oy + v[3]);
                lcx = ox + v[0]; lcy = oy + v[1]; kind = 'Q';
                break;
            case 't': {
                if (!nums(2)) return;
                const double qx = lastKind == 'Q' ? 2 * path.cx - lcx : path.cx, qy = lastKind == 'Q' ? 2 * path.cy - lcy : path.cy;
                path.Quad(qx, qy, ox + v[0], oy + v[1]);
                lcx = qx; lcy = qy; kind = 'Q';
                break;
            }
            case 'a': {
                double fa, fs;
                if (!nums(3) || !flag(fa) || !flag(fs)) return;
                double xy[2];
                for (double& t : xy) { SkipSep(p, e); if (!ReadSvgNumber(p, e, t)) return; }
                path.Arc(v[0], v[1], v[2], fa != 0, fs != 0, ox + xy[0], oy + xy[1]);
                break;
            }
            default: return;
        }
        lastKind = kind;
    }
}

// Flattened subpaths, for strokes and bounding boxes.
struct Poly { std::vector<double> xy; bool closed = false; };

void Flatten(const Path& path, double tol, std::vector<Poly>& out) {
    std::size_t k = 0;
    Poly cur;
    double x = 0, y = 0;
    // a lone moveto is no subpath (SVG 1.1 implementation notes); "M x y Z" and "M x y L x y" are zero-length ones
    auto flush = [&]() { if (cur.xy.size() > 2 || (cur.xy.size() == 2 && cur.closed)) out.push_back(std::move(cur)); cur = Poly(); };
    for (std::uint8_t v : path.verbs) {
        if (v == kMove) {
            flush();
            x = path.xy[k]; y = path.xy[k + 1]; k += 2;
            cur.xy = {x, y};
        } else if (v == kLine) {
            if (cur.xy.empty()) cur.xy = {x, y};
            x = path.xy[k]; y = path.xy[k + 1]; k += 2;
            cur.xy.push_back(x); cur.xy.push_back(y);
        } else if (v == kCubic) {
            if (cur.xy.empty()) cur.xy = {x, y};
            const double* c = &path.xy[k];
            k += 6;
            const double d1 = std::fabs(x - 2 * c[0] + c[2]) + std::fabs(y - 2 * c[1] + c[3]);
            const double d2 = std::fabs(c[0] - 2 * c[2] + c[4]) + std::fabs(c[1] - 2 * c[3] + c[5]);
            const double m = 0.75 * std::max(d1, d2) / tol;
            const int n = m > 1 ? std::min(100, int(std::ceil(std::sqrt(m)))) : 1;
            for (int i = 1; i <= n; ++i) {
                const double t = double(i) / n, u = 1 - t;
                cur.xy.push_back(u * u * u * x + 3 * u * u * t * c[0] + 3 * u * t * t * c[2] + t * t * t * c[4]);
                cur.xy.push_back(u * u * u * y + 3 * u * u * t * c[1] + 3 * u * t * t * c[3] + t * t * t * c[5]);
            }
            x = c[4]; y = c[5];
        } else if (!cur.xy.empty()) {
            cur.closed = true;
            x = cur.xy[0]; y = cur.xy[1];   // a command after Z starts at the subpath's start
            flush();
        }
    }
    flush();
}

// ---- styles --------------------------------------------------------------------------------------------------------
struct Paint {
    enum Kind { None, Color, Url, Current } kind = None;
    csscolor::Rgba color;
    std::string id;              // for Url
    bool hasFallback = false;
    csscolor::Rgba fallback;
};

struct Style {
    Paint fill, stroke;
    double fillOpacity = 1, strokeOpacity = 1, opacity = 1;
    bool fillEvenOdd = false, clipEvenOdd = false;
    double strokeWidth = 1, miterLimit = 4, dashOffset = 0;
    int cap = 0, join = 0;   // butt / round / square; miter / round / bevel
    std::vector<double> dash;
    csscolor::Rgba color{0, 0, 0, 255};
    bool visible = true, display = true;
    std::string clipPath;
    csscolor::Rgba stopColor{0, 0, 0, 255};
    double stopOpacity = 1;
    Mat cssTransform;
    bool hasCssTransform = false;
    Style() { fill.kind = Paint::Color; fill.color = csscolor::Rgba{0, 0, 0, 255}; }
};

bool ParsePaint(const std::string& vIn, Paint& out) {
    const std::string v = Trim(vIn);
    if (v.empty()) return false;
    if (v == "none") { out = Paint(); return true; }
    if (v == "currentColor" || v == "currentcolor") { out = Paint(); out.kind = Paint::Current; return true; }
    if (v.compare(0, 4, "url(") == 0) {
        const std::size_t close = v.find(')');
        if (close == std::string::npos) return false;
        std::string ref = Trim(v.substr(4, close - 4));
        if (ref.size() >= 2 && (ref[0] == '"' || ref[0] == '\'')) ref = ref.substr(1, ref.size() - 2);
        if (ref.empty() || ref[0] != '#') return false;
        Paint p;
        p.kind = Paint::Url;
        p.id = ref.substr(1);
        const std::string rest = Trim(v.substr(close + 1));
        if (!rest.empty() && rest != "none") p.hasFallback = csscolor::Parse(rest, p.fallback);
        out = p;
        return true;
    }
    Paint p;
    p.kind = Paint::Color;
    if (!csscolor::Parse(v, p.color)) return false;
    out = p;
    return true;
}

double ParseOpacity(const std::string& v, double keep) {
    const char* p = v.data();
    const char* e = p + v.size();
    SkipSpace(p, e);
    double d;
    if (!ReadSvgNumber(p, e, d)) return keep;
    if (p < e && *p == '%') d /= 100;
    return std::max(0.0, std::min(1.0, d));
}

// A length in user units. `ref` is what a percentage refers to; em / ex use a 16 px font.
bool ParseSvgLength(const std::string& v, double ref, double& out) {
    const char* p = v.data();
    const char* e = p + v.size();
    SkipSpace(p, e);
    double d;
    if (!ReadSvgNumber(p, e, d)) return false;
    const std::string unit = Trim(std::string(p, e));
    if (unit.empty() || unit == "px") out = d;
    else if (unit == "%") out = d * ref / 100;
    else if (unit == "pt") out = d * 96.0 / 72;
    else if (unit == "pc") out = d * 16;
    else if (unit == "mm") out = d * 96.0 / 25.4;
    else if (unit == "cm") out = d * 96.0 / 2.54;
    else if (unit == "in") out = d * 96;
    else if (unit == "em" || unit == "rem") out = d * 16;
    else if (unit == "ex") out = d * 8;
    else return false;
    return std::isfinite(out);
}

// One property; `parent` gives "inherit" and currentColor's meaning.
void ApplyProperty(Style& s, const Style& parent, const std::string& name, const std::string& rawValue) {
    std::string v = Trim(rawValue);
    const std::size_t imp = v.find("!important");
    if (imp != std::string::npos) v = Trim(v.substr(0, imp));
    if (v.empty()) return;
    const bool inherit = v == "inherit";
    if (name == "fill") { if (inherit) s.fill = parent.fill; else ParsePaint(v, s.fill); }
    else if (name == "stroke") { if (inherit) s.stroke = parent.stroke; else ParsePaint(v, s.stroke); }
    else if (name == "fill-opacity") s.fillOpacity = inherit ? parent.fillOpacity : ParseOpacity(v, s.fillOpacity);
    else if (name == "stroke-opacity") s.strokeOpacity = inherit ? parent.strokeOpacity : ParseOpacity(v, s.strokeOpacity);
    else if (name == "opacity") s.opacity = inherit ? parent.opacity : ParseOpacity(v, s.opacity);
    else if (name == "fill-rule") s.fillEvenOdd = inherit ? parent.fillEvenOdd : v == "evenodd";
    else if (name == "clip-rule") s.clipEvenOdd = inherit ? parent.clipEvenOdd : v == "evenodd";
    else if (name == "stroke-width") { double w; if (inherit) s.strokeWidth = parent.strokeWidth; else if (ParseSvgLength(v, 100, w) && w >= 0) s.strokeWidth = w; }
    else if (name == "stroke-miterlimit") { double m; const char* p = v.data(); if (inherit) s.miterLimit = parent.miterLimit; else if (ReadSvgNumber(p, v.data() + v.size(), m) && m >= 1) s.miterLimit = m; }
    else if (name == "stroke-linecap") s.cap = inherit ? parent.cap : (v == "round" ? 1 : (v == "square" ? 2 : (v == "butt" ? 0 : s.cap)));
    else if (name == "stroke-linejoin") s.join = inherit ? parent.join : (v == "round" ? 1 : (v == "bevel" ? 2 : ((v == "miter" || v == "miter-clip" || v == "arcs") ? 0 : s.join)));
    else if (name == "stroke-dasharray") {
        if (inherit) s.dash = parent.dash;
        else if (v == "none") s.dash.clear();
        else {
            std::vector<double> d;
            const char* p = v.data();
            const char* e = p + v.size();
            bool ok = true;
            while (true) {
                SkipSep(p, e);
                if (p >= e) break;
                const char* st = p;
                double x;
                if (!ReadSvgNumber(p, e, x) || x < 0) { ok = false; break; }
                while (p < e && !IsSpace(*p) && *p != ',') ++p;
                double len;
                if (!ParseSvgLength(std::string(st, std::size_t(p - st)), 100, len)) { ok = false; break; }
                d.push_back(len);
                if (d.size() > 64) { ok = false; break; }
            }
            if (ok) s.dash = d;
        }
    }
    else if (name == "stroke-dashoffset") { double o; if (inherit) s.dashOffset = parent.dashOffset; else if (ParseSvgLength(v, 100, o)) s.dashOffset = o; }
    else if (name == "color") { if (inherit) s.color = parent.color; else csscolor::Parse(v, s.color); }
    else if (name == "display") s.display = v != "none";
    else if (name == "visibility") s.visible = inherit ? parent.visible : !(v == "hidden" || v == "collapse");
    else if (name == "clip-path") s.clipPath = v;
    else if (name == "stop-color") {
        if (v == "currentColor" || v == "currentcolor") s.stopColor = s.color;
        else if (inherit) s.stopColor = parent.stopColor;
        else csscolor::Parse(v, s.stopColor);
    }
    else if (name == "stop-opacity") s.stopOpacity = inherit ? parent.stopOpacity : ParseOpacity(v, s.stopOpacity);
    else if (name == "transform") { Mat m; if (ParseTransformList(v, m)) { s.cssTransform = m; s.hasCssTransform = true; } }
}

// The properties a child starts with: the inherited ones (SVG 1.1 property index), not opacity, display, clip-path.
Style Inherit(const Style& p) {
    Style s = p;
    s.opacity = 1;
    s.display = true;
    s.clipPath.clear();
    s.stopColor = csscolor::Rgba{0, 0, 0, 255};
    s.stopOpacity = 1;
    s.cssTransform = Mat();
    s.hasCssTransform = false;
    return s;
}

const char* const kPresentation[] = {"fill", "stroke", "fill-opacity", "stroke-opacity", "opacity", "fill-rule", "clip-rule", "stroke-width",
                                     "stroke-miterlimit", "stroke-linecap", "stroke-linejoin", "stroke-dasharray", "stroke-dashoffset", "color",
                                     "display", "visibility", "clip-path", "stop-color", "stop-opacity"};

// ---- <style> sheets with simple selectors --------------------------------------------------------------------------
struct Simple { std::string tag; std::vector<std::string> classes; std::string id; };
struct Rule { Simple sel; int specificity = 0, order = 0; std::vector<std::pair<std::string, std::string>> decls; };

std::vector<std::pair<std::string, std::string>> Declarations(const std::string& body) {
    std::vector<std::pair<std::string, std::string>> out;
    std::size_t i = 0;
    while (i < body.size()) {
        std::size_t semi = body.find(';', i);
        if (semi == std::string::npos) semi = body.size();
        const std::string item = body.substr(i, semi - i);
        const std::size_t colon = item.find(':');
        if (colon != std::string::npos) {
            std::string k = Trim(item.substr(0, colon));
            for (char& c : k) if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
            out.push_back({k, Trim(item.substr(colon + 1))});   // lint-allow: substr-loop (one declaration, not the tail)
        }
        i = semi + 1;
    }
    return out;
}

bool ParseSimpleSelector(const std::string& s, Simple& out, int& spec) {
    out = Simple();
    spec = 0;
    std::size_t i = 0;
    auto ident = [&](std::string& dst) {
        const std::size_t st = i;
        while (i < s.size() && (std::isalnum((unsigned char)s[i]) || s[i] == '-' || s[i] == '_' || (unsigned char)s[i] >= 0x80)) ++i;
        dst = s.substr(st, i - st);
        return !dst.empty();
    };
    if (i < s.size() && s[i] == '*') ++i;
    else if (i < s.size() && (std::isalpha((unsigned char)s[i]))) { if (!ident(out.tag)) return false; spec += 1; }
    while (i < s.size()) {
        if (s[i] == '.') { ++i; std::string c; if (!ident(c)) return false; out.classes.push_back(c); spec += 10; }
        else if (s[i] == '#') { ++i; if (!ident(out.id)) return false; spec += 100; }
        else return false;   // combinators, pseudo-classes, attribute selectors: not supported
    }
    return true;
}

void ParseSheet(const std::string& cssIn, std::vector<Rule>& rules) {
    std::string css;
    css.reserve(cssIn.size());
    for (std::size_t i = 0; i < cssIn.size();) {   // drop comments
        if (cssIn.compare(i, 2, "/*") == 0) { const std::size_t e = cssIn.find("*/", i + 2); i = e == std::string::npos ? cssIn.size() : e + 2; continue; }
        css += cssIn[i++];
    }
    std::size_t i = 0;
    while (i < css.size() && rules.size() < 20000) {
        while (i < css.size() && IsSpace(css[i])) ++i;
        if (i >= css.size()) break;
        if (css[i] == '@') {   // @media, @font-face, @keyframes ... skipped; @import / @charset end at ';'
            std::size_t j = i;
            while (j < css.size() && css[j] != '{' && css[j] != ';') ++j;
            if (j >= css.size() || css[j] == ';') { i = j + 1; continue; }
            int depth = 0;
            for (; j < css.size(); ++j) {
                if (css[j] == '{') ++depth;
                else if (css[j] == '}' && --depth == 0) break;
            }
            i = j + 1;
            continue;
        }
        const std::size_t open = css.find('{', i);
        if (open == std::string::npos) break;
        const std::size_t close = css.find('}', open);
        if (close == std::string::npos) break;
        const std::string selText = css.substr(i, open - i);
        const auto decls = Declarations(css.substr(open + 1, close - open - 1));
        std::size_t a = 0;
        while (a <= selText.size()) {
            std::size_t b = selText.find(',', a);
            if (b == std::string::npos) b = selText.size();
            Rule r;
            if (ParseSimpleSelector(Trim(selText.substr(a, b - a)), r.sel, r.specificity)) {
                r.order = int(rules.size());
                r.decls = decls;
                rules.push_back(std::move(r));
            }
            a = b + 1;
        }
        i = close + 1;
    }
}

// ---- rendering -----------------------------------------------------------------------------------------------------
struct Gradient {
    bool radial = false, userSpace = false;
    int spread = 0;   // pad, reflect, repeat
    double x1 = 0, y1 = 0, x2 = 1, y2 = 0;            // linear (fractions when in bounding-box units)
    double cx = 0.5, cy = 0.5, r = 0.5, fx = 0.5, fy = 0.5;
    Mat transform;
    std::vector<std::pair<double, csscolor::Rgba>> stops;   // offset, colour with the stop's opacity in alpha
};

struct Renderer {
    const xml::Doc& doc;
    std::map<std::string, int> ids;
    std::vector<Rule> rules;
    int W, H;
    std::vector<std::uint8_t> canvas;   // premultiplied RGBA
    long long shapes = 0, commands = kMaxCommands, pixelWork = 0;
    long long scanBudget = 256LL << 20;      // bytes of attribute text read, <use> copies included
    long long declBudget = 20000000;         // style declarations applied
    bool budgetHit = false;
    double curVw = 100, curVh = 100;         // the viewport of the shape being drawn (gradient percentages)
    std::vector<std::size_t> attrBytes;      // per element: the size of its attribute values
    std::vector<std::vector<std::pair<std::string, std::string>>> decls;   // per element: its own declarations, in order
    std::vector<char> declsDone;
    std::map<int, Gradient> gradients;       // per gradient element, loaded once
    std::map<int, bool> gradientOk;

    long long pixelBudget;

    Renderer(const xml::Doc& d, int w, int h) : doc(d), W(w), H(h), canvas(std::size_t(w) * std::size_t(h) * 4, 0),
        pixelBudget(std::min(kMaxPixelWork, (1LL << 26) + 64LL * w * h)) {
        attrBytes.resize(d.n.size());
        for (std::size_t i = 0; i < d.n.size(); ++i)
            for (const auto& a : d.n[i].attrs) attrBytes[i] += a.second.size();
        decls.resize(d.n.size());
        declsDone.assign(d.n.size(), 0);
    }
    bool Spend(int n) {
        scanBudget -= (long long)attrBytes[std::size_t(n)] + 1;
        if (scanBudget < 0) budgetHit = true;
        return !budgetHit;
    }

    const std::string* Attr(int n, const char* k) const { return doc.Attr(n, k); }
    std::string AttrOr(int n, const char* k, const char* dflt = "") const { return doc.AttrOr(n, k, dflt); }

    // The element's own declarations, worked out once: presentation attributes, then matching sheet rules by
    // specificity and order, then style="".
    const std::vector<std::pair<std::string, std::string>>& OwnDeclarations(int n) {
        auto& out = decls[std::size_t(n)];
        if (declsDone[std::size_t(n)]) return out;
        declsDone[std::size_t(n)] = 1;
        for (const char* k : kPresentation) if (const std::string* v = Attr(n, k)) out.push_back({k, *v});
        if (!rules.empty()) {
            const std::string& tag = doc.Name(n);
            const std::string id = AttrOr(n, "id");
            std::vector<std::string> classes;
            {
                const std::string cl = AttrOr(n, "class");
                std::size_t i = 0;
                while (i < cl.size()) {
                    while (i < cl.size() && IsSpace(cl[i])) ++i;
                    const std::size_t st = i;
                    while (i < cl.size() && !IsSpace(cl[i])) ++i;
                    if (i > st) classes.push_back(cl.substr(st, i - st));
                }
            }
            std::vector<const Rule*> hits;
            for (const Rule& r : rules) {
                if (--declBudget < 0) { budgetHit = true; break; }
                if (!r.sel.tag.empty() && r.sel.tag != tag) continue;
                if (!r.sel.id.empty() && r.sel.id != id) continue;
                bool ok = true;
                for (const std::string& c : r.sel.classes) if (std::find(classes.begin(), classes.end(), c) == classes.end()) { ok = false; break; }
                if (ok) hits.push_back(&r);
            }
            std::sort(hits.begin(), hits.end(), [](const Rule* a, const Rule* b) {
                return a->specificity != b->specificity ? a->specificity < b->specificity : a->order < b->order;
            });
            for (const Rule* r : hits) out.insert(out.end(), r->decls.begin(), r->decls.end());
        }
        if (const std::string* st = Attr(n, "style")) {
            const auto inl = Declarations(*st);
            out.insert(out.end(), inl.begin(), inl.end());
        }
        return out;
    }

    Style StyleOf(int n, const Style& parent) {
        Style s = Inherit(parent);
        const auto& own = OwnDeclarations(n);
        declBudget -= (long long)own.size();
        if (declBudget < 0) { budgetHit = true; return s; }
        for (const auto& d : own) ApplyProperty(s, parent, d.first, d.second);
        return s;
    }

    Mat TransformOf(int n, const Style& s) const {
        Mat m;
        if (const std::string* t = Attr(n, "transform")) ParseTransformList(*t, m);
        if (s.hasCssTransform) m = s.cssTransform;   // the CSS property wins over the attribute
        return m;
    }

    int Lookup(const std::string& ref) const {
        std::string r = Trim(ref);
        if (r.compare(0, 4, "url(") == 0) {
            const std::size_t c = r.find(')');
            r = Trim(r.substr(4, c == std::string::npos ? std::string::npos : c - 4));
            if (r.size() >= 2 && (r[0] == '"' || r[0] == '\'')) r = r.substr(1, r.size() - 2);
        }
        if (r.empty() || r[0] != '#') return -1;
        auto it = ids.find(r.substr(1));
        return it == ids.end() ? -1 : it->second;
    }

    // Viewport transform for a viewBox in a w x h viewport (SVG 1.1 7.8, preserveAspectRatio).
    static Mat ViewBoxTransform(const std::vector<double>& vb, const std::string& parIn, double w, double h) {
        const double sx = w / vb[2], sy = h / vb[3];
        std::string par = Trim(parIn);
        if (par.compare(0, 5, "defer") == 0) par = Trim(par.substr(5));
        if (par.compare(0, 4, "none") == 0) return Mat::ScaleBy(sx, sy) * Mat::Translate(-vb[0], -vb[1]);
        const bool slice = par.find("slice") != std::string::npos;
        const double s = slice ? std::max(sx, sy) : std::min(sx, sy);
        int ax = 1, ay = 1;   // 0 min, 1 mid, 2 max
        if (par.size() >= 8 && par[0] == 'x') {
            ax = par.compare(1, 3, "Min") == 0 ? 0 : (par.compare(1, 3, "Max") == 0 ? 2 : 1);
            ay = par.compare(5, 3, "Min") == 0 ? 0 : (par.compare(5, 3, "Max") == 0 ? 2 : 1);
        }
        const double tx = (w - vb[2] * s) * ax / 2.0, ty = (h - vb[3] * s) * ay / 2.0;
        return Mat::Translate(tx, ty) * Mat::ScaleBy(s, s) * Mat::Translate(-vb[0], -vb[1]);
    }

    // ---- gradients
    bool LoadGradient(int n, Gradient& g) {
        auto hit = gradientOk.find(n);
        if (hit != gradientOk.end()) { g = gradients[n]; return hit->second; }
        const bool ok = BuildGradient(n, g);
        gradients[n] = g;
        gradientOk[n] = ok;
        return ok;
    }
    bool BuildGradient(int n, Gradient& g) {
        // Attributes and stops come from the first element of the href chain that has them.
        std::vector<int> chain;
        for (int k = n; k >= 0 && chain.size() < 16;) {
            if (std::find(chain.begin(), chain.end(), k) != chain.end()) break;
            chain.push_back(k);
            const std::string* h = Attr(k, "href");
            k = h ? Lookup(*h) : -1;
            if (k >= 0 && doc.Name(k) != "linearGradient" && doc.Name(k) != "radialGradient") k = -1;
        }
        g.radial = doc.Name(n) == "radialGradient";
        auto attr = [&](const char* key) -> const std::string* {
            for (int k : chain) if (const std::string* v = Attr(k, key)) return v;
            return nullptr;
        };
        if (const std::string* u = attr("gradientUnits")) g.userSpace = Trim(*u) == "userSpaceOnUse";
        if (const std::string* s = attr("spreadMethod")) g.spread = Trim(*s) == "reflect" ? 1 : (Trim(*s) == "repeat" ? 2 : 0);
        if (const std::string* t = attr("gradientTransform")) ParseTransformList(*t, g.transform);
        auto num = [&](const char* key, double& out, double ref) {
            if (const std::string* v = attr(key)) {
                double d;
                if (g.userSpace ? ParseSvgLength(*v, ref, d) : ParseSvgLength(*v, 1.0, d)) out = d;
            }
        };
        const double vw = curVw, vh = curVh, diag = std::sqrt((vw * vw + vh * vh) / 2);
        if (!g.radial) {
            g.x1 = 0; g.y1 = 0; g.x2 = g.userSpace ? vw : 1; g.y2 = 0;
            num("x1", g.x1, vw); num("y1", g.y1, vh); num("x2", g.x2, vw); num("y2", g.y2, vh);
        } else {
            g.cx = g.userSpace ? vw / 2 : 0.5; g.cy = g.userSpace ? vh / 2 : 0.5; g.r = g.userSpace ? diag / 2 : 0.5;
            num("cx", g.cx, vw); num("cy", g.cy, vh); num("r", g.r, diag);
            g.fx = g.cx; g.fy = g.cy;
            num("fx", g.fx, vw); num("fy", g.fy, vh);
        }
        for (int k : chain) {
            bool any = false;
            for (int s : doc.Kids(k)) {
                if (doc.Name(s) != "stop") continue;
                if (!Spend(s)) break;
                any = true;
                const Style st = StyleOf(s, Style());
                double off = 0;
                if (const std::string* o = Attr(s, "offset")) off = ParseOpacity(*o, 0);
                if (!g.stops.empty()) off = std::max(off, g.stops.back().first);
                csscolor::Rgba c = st.stopColor;
                c.a = std::uint8_t(std::lround(c.a * st.stopOpacity));
                g.stops.push_back({off, c});
                if (g.stops.size() > 256) break;
            }
            if (any) break;
        }
        return !g.stops.empty();
    }

    // ---- filling ------------------------------------------------------------------------------------------------
    // Coverage of a path (device space) over the box [x0, x1) x [y0, y1).
    static void PathCoverage(const Path& path, const Mat& m, int x0, int y0, int bw, int bh, bool evenOdd, std::vector<std::uint8_t>& cov) {
        raster::Coverage r(bw, bh);
        std::size_t k = 0;
        for (std::uint8_t v : path.verbs) {
            double x, y;
            if (v == kMove) { m.Apply(path.xy[k], path.xy[k + 1], x, y); r.MoveTo(float(x - x0), float(y - y0)); k += 2; }
            else if (v == kLine) { m.Apply(path.xy[k], path.xy[k + 1], x, y); r.LineTo(float(x - x0), float(y - y0)); k += 2; }
            else if (v == kCubic) {
                double ax, ay, bx, by;
                m.Apply(path.xy[k], path.xy[k + 1], ax, ay);
                m.Apply(path.xy[k + 2], path.xy[k + 3], bx, by);
                m.Apply(path.xy[k + 4], path.xy[k + 5], x, y);
                r.CubicTo(float(ax - x0), float(ay - y0), float(bx - x0), float(by - y0), float(x - x0), float(y - y0));
                k += 6;
            } else r.Close();
        }
        cov.assign(std::size_t(bw) * std::size_t(bh), 0);
        r.Finish(cov.data(), bw, evenOdd);
    }

    // Device box of the path's points (control points included), clipped to the canvas; false when empty.
    bool DeviceBox(const Path& path, const Mat& m, double grow, int& x0, int& y0, int& x1, int& y1) const {
        double lx = 1e300, ly = 1e300, hx = -1e300, hy = -1e300;
        for (std::size_t k = 0; k + 1 < path.xy.size(); k += 2) {
            double x, y;
            m.Apply(path.xy[k], path.xy[k + 1], x, y);
            if (!std::isfinite(x) || !std::isfinite(y)) continue;
            lx = std::min(lx, x); ly = std::min(ly, y); hx = std::max(hx, x); hy = std::max(hy, y);
        }
        if (lx > hx) return false;
        x0 = int(std::max(0.0, std::floor(lx - grow)));
        y0 = int(std::max(0.0, std::floor(ly - grow)));
        x1 = int(std::min(double(W), std::ceil(hx + grow) + 1));
        y1 = int(std::min(double(H), std::ceil(hy + grow) + 1));
        return x1 > x0 && y1 > y0;
    }

    // User-space bounding box of the geometry (for objectBoundingBox units).
    static bool UserBox(const Path& path, double& bx, double& by, double& bw, double& bh) {
        std::vector<Poly> polys;
        Flatten(path, 0.01, polys);
        double lx = 1e300, ly = 1e300, hx = -1e300, hy = -1e300;
        for (const Poly& p : polys)
            for (std::size_t i = 0; i + 1 < p.xy.size(); i += 2) {
                lx = std::min(lx, p.xy[i]); hx = std::max(hx, p.xy[i]);
                ly = std::min(ly, p.xy[i + 1]); hy = std::max(hy, p.xy[i + 1]);
            }
        if (lx > hx) return false;
        bx = lx; by = ly; bw = hx - lx; bh = hy - ly;
        return true;
    }

    // Paints `cov` (bw x bh at x0, y0) with `paint`. `clip` (canvas-sized coverage) may be null.
    void Composite(const std::vector<std::uint8_t>& cov, int x0, int y0, int bw, int bh, const Paint& paintIn, double alpha,
                   const Style& st, const Path& geometry, const Mat& ctm, const std::vector<std::uint8_t>* clip) {
        Paint paint = paintIn;
        Gradient g;
        bool grad = false;
        Mat inv;
        if (paint.kind == Paint::Url) {
            const int gn = ids.count(paint.id) ? ids.at(paint.id) : -1;
            const std::string gname = gn >= 0 ? doc.Name(gn) : std::string();
            if (gn >= 0 && (gname == "linearGradient" || gname == "radialGradient") && LoadGradient(gn, g)) {
                Mat space = ctm;
                if (!g.userSpace) {
                    double bx, by, bwid, bhgt;
                    if (!UserBox(geometry, bx, by, bwid, bhgt) || bwid <= 0 || bhgt <= 0) return;   // no box: nothing drawn
                    space = space * Mat{bwid, 0, 0, bhgt, bx, by};
                }
                space = space * g.transform;
                if (g.stops.size() == 1) { paint.kind = Paint::Color; paint.color = g.stops[0].second; }
                else if (space.Invert(inv)) grad = true;
                else return;
            } else if (paint.hasFallback) {
                paint.kind = Paint::Color;
                paint.color = paint.fallback;
            } else {
                return;   // a missing paint server without a fallback: not drawn
            }
        }
        if (paint.kind == Paint::Current) { paint.kind = Paint::Color; paint.color = st.color; }
        if (paint.kind == Paint::None) return;
        // Gradient colours as a 256-entry table of premultiplied values.
        std::uint8_t lut[256][4];
        if (grad) {
            for (int i = 0; i < 256; ++i) {
                const double t = i / 255.0;
                std::size_t k = 0;
                while (k + 1 < g.stops.size() && g.stops[k + 1].first < t) ++k;
                const auto& s0 = g.stops[k];
                const auto& s1 = g.stops[std::min(k + 1, g.stops.size() - 1)];
                double f = 0;
                if (t <= s0.first) f = 0;
                else if (s1.first > s0.first) f = std::min(1.0, (t - s0.first) / (s1.first - s0.first));
                else f = 1;
                if (k == 0 && t < s0.first) f = 0;
                double pm0[4], pm1[4];
                const csscolor::Rgba c0 = s0.second, c1 = s1.second;
                pm0[3] = c0.a / 255.0; pm1[3] = c1.a / 255.0;
                pm0[0] = c0.r * pm0[3]; pm0[1] = c0.g * pm0[3]; pm0[2] = c0.b * pm0[3];
                pm1[0] = c1.r * pm1[3]; pm1[1] = c1.g * pm1[3]; pm1[2] = c1.b * pm1[3];
                for (int ch = 0; ch < 3; ++ch) lut[i][ch] = std::uint8_t(std::lround(pm0[ch] + (pm1[ch] - pm0[ch]) * f));
                lut[i][3] = std::uint8_t(std::lround(255.0 * (pm0[3] + (pm1[3] - pm0[3]) * f)));
            }
        }
        double fx = g.fx, fy = g.fy;
        if (grad && g.radial) {   // a focal point outside the circle is moved onto it (SVG 1.1 13.2.3)
            const double dx = fx - g.cx, dy = fy - g.cy, dist = std::sqrt(dx * dx + dy * dy);
            if (dist > g.r * 0.999 && dist > 0) { fx = g.cx + dx * g.r * 0.999 / dist; fy = g.cy + dy * g.r * 0.999 / dist; }
        }
        const double gdx = g.x2 - g.x1, gdy = g.y2 - g.y1, glen2 = gdx * gdx + gdy * gdy;
        const double ex = g.cx - fx, ey = g.cy - fy, qa = ex * ex + ey * ey - g.r * g.r;
        const int a255 = int(std::lround(std::max(0.0, std::min(1.0, alpha)) * 255));
        for (int y = 0; y < bh; ++y) {
            for (int x = 0; x < bw; ++x) {
                int c = cov[std::size_t(y) * std::size_t(bw) + std::size_t(x)];
                if (!c) continue;
                const std::size_t di = (std::size_t(y0 + y) * std::size_t(W) + std::size_t(x0 + x));
                if (clip) { c = c * (*clip)[di] / 255; if (!c) continue; }
                int sr, sg, sb, sa;
                if (!grad) {
                    sa = paint.color.a;
                    sr = paint.color.r * sa / 255; sg = paint.color.g * sa / 255; sb = paint.color.b * sa / 255;
                } else {
                    double gx, gy;
                    inv.Apply(x0 + x + 0.5, y0 + y + 0.5, gx, gy);
                    double t;
                    if (!g.radial) t = glen2 > 0 ? ((gx - g.x1) * gdx + (gy - g.y1) * gdy) / glen2 : 1.0;
                    else {
                        // circles from the focus (radius 0) to the end circle: |p - f - t e| = t r
                        const double dx = gx - fx, dy = gy - fy;
                        const double qb = -2 * (dx * ex + dy * ey), qc = dx * dx + dy * dy;
                        if (std::fabs(qa) < 1e-12) t = qb != 0 ? -qc / qb : 0;
                        else {
                            const double disc = qb * qb - 4 * qa * qc;
                            t = disc >= 0 ? (-qb - std::sqrt(disc)) / (2 * qa) : 0;
                            if (t < 0) t = (-qb + std::sqrt(std::max(0.0, disc))) / (2 * qa);
                        }
                    }
                    if (!std::isfinite(t)) t = 0;
                    if (g.spread == 2) t -= std::floor(t);
                    else if (g.spread == 1) { t = std::fmod(std::fabs(t), 2.0); if (t > 1) t = 2 - t; }
                    t = std::max(0.0, std::min(1.0, t));
                    const std::uint8_t* l = lut[int(t * 255 + 0.5)];
                    sr = l[0]; sg = l[1]; sb = l[2]; sa = l[3];
                }
                const int k = c * a255;   // up to 255 * 255
                sr = sr * k / 65025; sg = sg * k / 65025; sb = sb * k / 65025; sa = sa * k / 65025;
                std::uint8_t* d = &canvas[di * 4];
                const int inv8 = 255 - sa;
                d[0] = std::uint8_t(sr + (d[0] * inv8 + 127) / 255);
                d[1] = std::uint8_t(sg + (d[1] * inv8 + 127) / 255);
                d[2] = std::uint8_t(sb + (d[2] * inv8 + 127) / 255);
                d[3] = std::uint8_t(sa + (d[3] * inv8 + 127) / 255);
            }
        }
    }

    // Fills `fillPath` (and the device box `grow` wider) with a paint.
    void FillPath(const Path& fillPath, const Mat& ctm, bool evenOdd, const Paint& paint, double alpha, const Style& st, const Path& geometry,
                  const std::vector<std::uint8_t>* clip) {
        if (paint.kind == Paint::None || alpha <= 0) return;
        int x0, y0, x1, y1;
        if (!DeviceBox(fillPath, ctm, 1, x0, y0, x1, y1)) return;
        const long long area = (long long)(x1 - x0) * (y1 - y0);
        if (pixelWork + area > pixelBudget) { budgetHit = true; return; }
        pixelWork += area;
        std::vector<std::uint8_t> cov;
        PathCoverage(fillPath, ctm, x0, y0, x1 - x0, y1 - y0, evenOdd, cov);
        Composite(cov, x0, y0, x1 - x0, y1 - y0, paint, alpha, st, geometry, ctm, clip);
    }

    // ---- strokes: the outline as closed pieces (segments, joins, caps) filled with the non-zero rule -----------------
    static void Piece(Path& out, const double* pts, int n) {   // one counter-clockwise (positive area) polygon
        double area = 0;
        for (int i = 0; i < n; ++i) { const int j = (i + 1) % n; area += pts[2 * i] * pts[2 * j + 1] - pts[2 * j] * pts[2 * i + 1]; }
        if (std::fabs(area) < 1e-18) return;
        if (area > 0) { out.Move(pts[0], pts[1]); for (int i = 1; i < n; ++i) out.Line(pts[2 * i], pts[2 * i + 1]); }
        else { out.Move(pts[2 * (n - 1)], pts[2 * (n - 1) + 1]); for (int i = n - 2; i >= 0; --i) out.Line(pts[2 * i], pts[2 * i + 1]); }
        out.Close();
    }
    // A circle as a polygon whose radius is enlarged so that its area is the circle's (pi r^2): round caps and joins
    // then cover exactly what they should.
    static void Disc(Path& out, double x, double y, double r, double tol) {
        int n = int(std::ceil(kPi / std::acos(std::max(-1.0, std::min(1.0, 1 - tol / std::max(r, 1e-9))))));
        n = std::max(8, std::min(256, n));
        const double rr = r * std::sqrt(2 * kPi / (n * std::sin(2 * kPi / n)));
        out.Move(x + rr, y);
        for (int i = 1; i < n; ++i) out.Line(x + rr * std::cos(2 * kPi * i / n), y + rr * std::sin(2 * kPi * i / n));
        out.Close();
    }

    static void StrokePolyline(const std::vector<double>& pin, bool closed, const Style& st, double tol, Path& out) {
        const double hw = st.strokeWidth / 2;
        std::vector<double> p;   // without repeated points
        for (std::size_t i = 0; i + 1 < pin.size(); i += 2) {
            if (!p.empty() && std::fabs(p[p.size() - 2] - pin[i]) < 1e-12 && std::fabs(p.back() - pin[i + 1]) < 1e-12) continue;
            p.push_back(pin[i]); p.push_back(pin[i + 1]);
        }
        if (closed && p.size() >= 4 && std::fabs(p[0] - p[p.size() - 2]) < 1e-12 && std::fabs(p[1] - p.back()) < 1e-12) { p.pop_back(); p.pop_back(); }
        const int n = int(p.size() / 2);
        if (n == 0) return;
        if (n == 1) {   // a zero-length subpath: a dot for round and square caps
            if (st.cap == 1) Disc(out, p[0], p[1], hw, tol);
            else if (st.cap == 2) { const double q[8] = {p[0] - hw, p[1] - hw, p[0] + hw, p[1] - hw, p[0] + hw, p[1] + hw, p[0] - hw, p[1] + hw}; Piece(out, q, 4); }
            return;
        }
        const int segs = closed ? n : n - 1;
        auto dirOf = [&](int i, double& dx, double& dy) {
            const int j = (i + 1) % n;
            dx = p[2 * j] - p[2 * i]; dy = p[2 * j + 1] - p[2 * i + 1];
            const double l = std::sqrt(dx * dx + dy * dy);
            dx /= l; dy /= l;
        };
        for (int i = 0; i < segs; ++i) {
            double dx, dy;
            dirOf(i, dx, dy);
            const int j = (i + 1) % n;
            const double nx = -dy * hw, ny = dx * hw;
            const double q[8] = {p[2 * i] + nx, p[2 * i + 1] + ny, p[2 * j] + nx, p[2 * j + 1] + ny, p[2 * j] - nx, p[2 * j + 1] - ny, p[2 * i] - nx, p[2 * i + 1] - ny};
            Piece(out, q, 4);
        }
        // joins at the inner vertices (all of them on a closed path)
        for (int v = closed ? 0 : 1; v < (closed ? n : n - 1); ++v) {
            const int a = (v - 1 + n) % n;
            double d0x, d0y, d1x, d1y;
            dirOf(a, d0x, d0y);
            dirOf(v, d1x, d1y);
            const double cross = d0x * d1y - d0y * d1x, dot = d0x * d1x + d0y * d1y;
            const double vx = p[2 * v], vy = p[2 * v + 1];
            if (st.join == 1) { Disc(out, vx, vy, hw, tol); continue; }
            if (std::fabs(cross) < 1e-12 && dot > 0) continue;   // straight on
            const double side = cross > 0 ? -1 : 1;           // the outer side of the turn
            const double ax = vx + side * -d0y * hw, ay = vy + side * d0x * hw;
            const double bx = vx + side * -d1y * hw, by = vy + side * d1x * hw;
            if (st.join == 0 && 1 + dot > 1e-9) {
                const double ratio = std::sqrt(2 / (1 + dot));   // miter length / stroke width
                if (ratio <= st.miterLimit) {
                    const double k = hw / (1 + dot);
                    const double mx = vx + side * (-d0y - d1y) * k, my = vy + side * (d0x + d1x) * k;
                    const double q[8] = {vx, vy, ax, ay, mx, my, bx, by};
                    Piece(out, q, 4);
                    continue;
                }
            }
            const double q[6] = {vx, vy, ax, ay, bx, by};
            Piece(out, q, 3);
        }
        if (!closed && st.cap != 0) {
            for (int end = 0; end < 2; ++end) {
                const int i = end ? n - 1 : 0;
                double dx, dy;
                dirOf(end ? n - 2 : 0, dx, dy);
                if (!end) { dx = -dx; dy = -dy; }   // pointing out of the line
                const double x = p[2 * i], y = p[2 * i + 1];
                if (st.cap == 1) Disc(out, x, y, hw, tol);
                else {
                    const double nx = -dy * hw, ny = dx * hw, ox = dx * hw, oy = dy * hw;
                    const double q[8] = {x + nx, y + ny, x + nx + ox, y + ny + oy, x - nx + ox, y - ny + oy, x - nx, y - ny};
                    Piece(out, q, 4);
                }
            }
        }
    }

    // Splits a polyline into dashes (SVG 1.1 11.4 stroke-dasharray / stroke-dashoffset).
    static void Dashes(const Poly& poly, const std::vector<double>& pattern, double offset, std::vector<std::vector<double>>& out) {
        std::vector<double> pts = poly.xy;
        if (poly.closed && pts.size() >= 2) { pts.push_back(pts[0]); pts.push_back(pts[1]); }
        double total = 0;
        for (double d : pattern) total += d;
        if (!(total > 0)) { out.push_back(pts); return; }
        double pos = std::fmod(offset, total);
        if (pos < 0) pos += total;
        std::size_t k = 0;
        while (pos >= pattern[k]) { pos -= pattern[k]; k = (k + 1) % pattern.size(); }
        double left = pattern[k] - pos;
        bool on = (k % 2) == 0;
        std::vector<double> cur;
        if (on && pts.size() >= 2) { cur.push_back(pts[0]); cur.push_back(pts[1]); }
        int pieces = 0;
        for (std::size_t i = 0; i + 3 < pts.size(); i += 2) {
            double x = pts[i], y = pts[i + 1];
            const double ex = pts[i + 2], ey = pts[i + 3];
            double seg = std::sqrt((ex - x) * (ex - x) + (ey - y) * (ey - y));
            while (seg > left && pieces < 100000) {
                const double t = left / seg;
                x += (ex - x) * t; y += (ey - y) * t;
                seg -= left;
                if (on) { cur.push_back(x); cur.push_back(y); out.push_back(cur); cur.clear(); ++pieces; }
                else { cur.clear(); cur.push_back(x); cur.push_back(y); }
                on = !on;
                k = (k + 1) % pattern.size();
                left = pattern[k];
            }
            left -= seg;
            if (on) { cur.push_back(ex); cur.push_back(ey); }
        }
        if (on && cur.size() >= 4) out.push_back(cur);
    }

    void StrokePath(const Path& path, const Mat& ctm, const Style& st, double alpha, const std::vector<std::uint8_t>* clip) {
        if (st.stroke.kind == Paint::None || st.strokeWidth <= 0 || alpha <= 0) return;
        const double tol = 0.05 / ctm.Scale();   // flattening of strokes, device pixels
        std::vector<Poly> polys;
        Flatten(path, tol, polys);
        std::vector<double> pattern = st.dash;
        if (pattern.size() % 2) { const std::size_t n = pattern.size(); for (std::size_t i = 0; i < n; ++i) pattern.push_back(pattern[i]); }
        Path outline;
        for (const Poly& poly : polys) {
            if (!pattern.empty()) {
                std::vector<std::vector<double>> dashes;
                Dashes(poly, pattern, st.dashOffset, dashes);
                for (const auto& d : dashes) StrokePolyline(d, false, st, tol, outline);
            } else {
                StrokePolyline(poly.xy, poly.closed, st, tol, outline);
            }
            if ((long long)outline.verbs.size() > kMaxCommands) { budgetHit = true; break; }
        }
        FillPath(outline, ctm, false, st.stroke, alpha, st, path, clip);
    }

    // ---- shapes -------------------------------------------------------------------------------------------------
    double Len(int n, const char* key, double ref, double dflt = 0) const {
        double v = dflt;
        if (const std::string* s = Attr(n, key)) { double d; if (ParseSvgLength(*s, ref, d)) v = d; }
        return v;
    }

    bool ShapePath(int n, Path& path, double vw, double vh) {
        const std::string& tag = doc.Name(n);
        const double diag = std::sqrt((vw * vw + vh * vh) / 2);
        if (tag == "path") {
            ParsePathData(AttrOr(n, "d"), path, commands);
            if (commands < 0) budgetHit = true;
        } else if (tag == "rect") {
            const double x = Len(n, "x", vw), y = Len(n, "y", vh), w = Len(n, "width", vw), h = Len(n, "height", vh);
            if (!(w > 0) || !(h > 0)) return false;
            double rx = -1, ry = -1;
            if (const std::string* s = Attr(n, "rx")) { double d; if (ParseSvgLength(*s, vw, d) && d >= 0) rx = d; }
            if (const std::string* s = Attr(n, "ry")) { double d; if (ParseSvgLength(*s, vh, d) && d >= 0) ry = d; }
            if (rx < 0) rx = ry < 0 ? 0 : ry;
            if (ry < 0) ry = rx;
            rx = std::min(rx, w / 2); ry = std::min(ry, h / 2);
            if (rx <= 0 || ry <= 0) {
                path.Move(x, y); path.Line(x + w, y); path.Line(x + w, y + h); path.Line(x, y + h); path.Close();
            } else {
                path.Move(x + rx, y);
                path.Line(x + w - rx, y);
                path.Arc(rx, ry, 0, false, true, x + w, y + ry);
                path.Line(x + w, y + h - ry);
                path.Arc(rx, ry, 0, false, true, x + w - rx, y + h);
                path.Line(x + rx, y + h);
                path.Arc(rx, ry, 0, false, true, x, y + h - ry);
                path.Line(x, y + ry);
                path.Arc(rx, ry, 0, false, true, x + rx, y);
                path.Close();
            }
        } else if (tag == "circle") {
            const double r = Len(n, "r", diag);
            if (!(r > 0)) return false;
            path.Ellipse(Len(n, "cx", vw), Len(n, "cy", vh), r, r);
        } else if (tag == "ellipse") {
            double rx = Len(n, "rx", vw, -1), ry = Len(n, "ry", vh, -1);
            if (rx < 0) rx = ry;   // SVG 2: auto
            if (ry < 0) ry = rx;
            if (!(rx > 0) || !(ry > 0)) return false;
            path.Ellipse(Len(n, "cx", vw), Len(n, "cy", vh), rx, ry);
        } else if (tag == "line") {
            path.Move(Len(n, "x1", vw), Len(n, "y1", vh));
            path.Line(Len(n, "x2", vw), Len(n, "y2", vh));
        } else if (tag == "polyline" || tag == "polygon") {
            const std::vector<double> v = NumberList(AttrOr(n, "points"));
            if (v.size() < 2) return false;
            commands -= (long long)v.size() / 2;
            if (commands < 0) { budgetHit = true; return false; }
            path.Move(v[0], v[1]);
            for (std::size_t i = 2; i + 1 < v.size(); i += 2) path.Line(v[i], v[i + 1]);
            if (tag == "polygon") path.Close();
        } else {
            return false;
        }
        return !path.verbs.empty();
    }

    // Canvas-sized coverage of a clipPath element (the union of its shapes), or false when it clips everything away.
    bool ClipMask(int cp, const Mat& ctm, const Path& geometry, std::vector<std::uint8_t>& mask, double vw, double vh) {
        // a mask covers the whole picture and may be combined with the parent's: count both passes as work
        const long long whole = 2LL * W * H;
        if (pixelWork + whole > pixelBudget) { budgetHit = true; return false; }
        pixelWork += whole;
        mask.assign(std::size_t(W) * std::size_t(H), 0);
        Mat space = ctm;
        if (Trim(AttrOr(cp, "clipPathUnits")) == "objectBoundingBox") {
            double bx, by, bw, bh;
            if (!UserBox(geometry, bx, by, bw, bh) || bw <= 0 || bh <= 0) return false;
            space = space * Mat{bw, 0, 0, bh, bx, by};
        }
        {
            Mat t;
            if (const std::string* s = Attr(cp, "transform")) ParseTransformList(*s, t);
            space = space * t;
        }
        const Style base = StyleOf(cp, Style());
        for (int k : doc.Kids(cp)) {
            if (++shapes > kMaxShapes || !Spend(k)) { budgetHit = true; break; }
            int shape = k;
            Mat m = space;
            const std::string& tag = doc.Name(k);
            if (tag == "use") {
                shape = Lookup(AttrOr(k, "href"));
                if (shape < 0 || !Spend(shape)) continue;
                m = m * TransformOf(k, StyleOf(k, base)) * Mat::Translate(Len(k, "x", vw), Len(k, "y", vh));
            }
            const Style st = StyleOf(shape, base);
            if (!st.display || !st.visible) continue;
            Path p;
            if (!ShapePath(shape, p, vw, vh)) continue;
            m = m * TransformOf(shape, st);
            int x0, y0, x1, y1;
            if (!DeviceBox(p, m, 1, x0, y0, x1, y1)) continue;
            const long long area = (long long)(x1 - x0) * (y1 - y0);
            if (pixelWork + area > pixelBudget) { budgetHit = true; break; }
            pixelWork += area;
            std::vector<std::uint8_t> cov;
            PathCoverage(p, m, x0, y0, x1 - x0, y1 - y0, st.clipEvenOdd, cov);
            for (int y = y0; y < y1; ++y)
                for (int x = x0; x < x1; ++x) {
                    std::uint8_t& d = mask[std::size_t(y) * std::size_t(W) + std::size_t(x)];
                    const int c = cov[std::size_t(y - y0) * std::size_t(x1 - x0) + std::size_t(x - x0)];
                    d = std::uint8_t(c + d - c * d / 255);   // union
                }
        }
        return true;
    }

    void DrawShape(int n, const Mat& ctm, const Style& st, double opacity, const std::vector<std::uint8_t>* parentClip, double vw, double vh) {
        if (++shapes > kMaxShapes) { budgetHit = true; return; }
        if (!st.visible) return;
        curVw = vw; curVh = vh;
        Path path;
        if (!ShapePath(n, path, vw, vh)) return;
        const double op = opacity * st.opacity;
        std::vector<std::uint8_t> clipMask;
        const std::vector<std::uint8_t>* clip = parentClip;
        if (!st.clipPath.empty() && st.clipPath != "none") {
            const int cp = Lookup(st.clipPath);
            if (cp >= 0 && doc.Name(cp) == "clipPath") {
                if (!ClipMask(cp, ctm, path, clipMask, vw, vh)) return;
                if (parentClip) for (std::size_t i = 0; i < clipMask.size(); ++i) clipMask[i] = std::uint8_t(clipMask[i] * (*parentClip)[i] / 255);
                clip = &clipMask;
            }
        }
        FillPath(path, ctm, st.fillEvenOdd, st.fill, op * st.fillOpacity, st, path, clip);   // a line has no area to fill
        StrokePath(path, ctm, st, op * st.strokeOpacity, clip);
    }

    // ---- the element tree ---------------------------------------------------------------------------------------
    void Node(int n, const Mat& parentCtm, const Style& parent, double opacity, int depth, int useDepth,
              const std::vector<std::uint8_t>* clip, double vw, double vh) {
        if (depth > kMaxDepth || budgetHit || !Spend(n)) return;
        const std::string& tag = doc.Name(n);
        static const char* const kSkip[] = {"defs", "symbol", "linearGradient", "radialGradient", "clipPath", "mask", "pattern", "marker",
                                            "filter", "style", "title", "desc", "metadata", "text", "image", "foreignObject", "script",
                                            "stop", "animate", "animateTransform", "animateMotion", "set", "font", "font-face", "cursor", "view"};
        for (const char* s : kSkip) if (tag == s) return;
        const Style st = StyleOf(n, parent);
        if (!st.display) return;
        Mat ctm = parentCtm * TransformOf(n, st);
        if (tag == "svg") {   // a nested viewport
            const double x = Len(n, "x", vw), y = Len(n, "y", vh);
            const double w = Len(n, "width", vw, vw), h = Len(n, "height", vh, vh);
            if (!(w > 0) || !(h > 0)) return;
            ctm = ctm * Mat::Translate(x, y);
            double nvw = w, nvh = h;
            const std::vector<double> vb = NumberList(AttrOr(n, "viewBox"));
            if (vb.size() == 4) {
                if (!(vb[2] > 0) || !(vb[3] > 0)) return;
                ctm = ctm * ViewBoxTransform(vb, AttrOr(n, "preserveAspectRatio"), w, h);
                nvw = vb[2]; nvh = vb[3];
            }
            for (int k : doc.Kids(n)) Node(k, ctm, st, opacity * st.opacity, depth + 1, useDepth, clip, nvw, nvh);
            return;
        }
        if (tag == "g" || tag == "a" || tag == "switch") {
            std::vector<std::uint8_t> mask;
            const std::vector<std::uint8_t>* c = clip;
            if (!st.clipPath.empty() && st.clipPath != "none") {
                const int cp = Lookup(st.clipPath);
                if (cp >= 0 && doc.Name(cp) == "clipPath" && Trim(AttrOr(cp, "clipPathUnits")) != "objectBoundingBox") {
                    Path none;
                    if (!ClipMask(cp, ctm, none, mask, vw, vh)) return;
                    if (clip) for (std::size_t i = 0; i < mask.size(); ++i) mask[i] = std::uint8_t(mask[i] * (*clip)[i] / 255);
                    c = &mask;
                }
            }
            for (int k : doc.Kids(n)) {
                Node(k, ctm, st, opacity * st.opacity, depth + 1, useDepth, c, vw, vh);
                if (tag == "switch") break;
            }
            return;
        }
        if (tag == "use") {
            if (useDepth >= kMaxUseDepth) return;
            const int target = Lookup(AttrOr(n, "href"));
            if (target < 0 || target == n) return;
            ctm = ctm * Mat::Translate(Len(n, "x", vw), Len(n, "y", vh));
            if (++shapes > kMaxShapes) { budgetHit = true; return; }
            if (doc.Name(target) == "symbol") {
                const Style ss = StyleOf(target, st);
                if (!ss.display) return;
                const double w = Len(n, "width", vw, vw), h = Len(n, "height", vh, vh);
                Mat m = ctm * TransformOf(target, ss);
                double nvw = w, nvh = h;
                const std::vector<double> vb = NumberList(AttrOr(target, "viewBox"));
                if (vb.size() == 4) {
                    if (!(vb[2] > 0) || !(vb[3] > 0)) return;
                    m = m * ViewBoxTransform(vb, AttrOr(target, "preserveAspectRatio"), w, h);
                    nvw = vb[2]; nvh = vb[3];
                }
                for (int k : doc.Kids(target)) Node(k, m, ss, opacity * st.opacity * ss.opacity, depth + 1, useDepth + 1, clip, nvw, nvh);
                return;
            }
            Node(target, ctm, st, opacity * st.opacity, depth + 1, useDepth + 1, clip, vw, vh);
            return;
        }
        DrawShape(n, ctm, st, opacity, clip, vw, vh);
    }
};

bool IntrinsicSize(const xml::Doc& doc, double& w, double& h, std::vector<double>& vb) {
    vb = NumberList(doc.AttrOr(0, "viewBox"));
    if (vb.size() != 4) vb.clear();
    double ww = -1, hh = -1;
    auto len = [&](const char* k, double& out) {
        const std::string* s = doc.Attr(0, k);
        if (!s) return;
        const std::string v = Trim(*s);
        if (v.empty() || v.back() == '%') return;   // a percentage of an unknown page: use the viewBox
        double d;
        if (ParseSvgLength(v, 0, d) && d > 0) out = d;
    };
    len("width", ww);
    len("height", hh);
    if (ww < 0 && hh < 0) {
        if (!vb.empty() && vb[2] > 0 && vb[3] > 0) { ww = vb[2]; hh = vb[3]; }
        else { ww = 300; hh = 150; }
    } else if (ww < 0) {
        ww = (!vb.empty() && vb[3] > 0) ? hh * vb[2] / vb[3] : 300;
    } else if (hh < 0) {
        hh = (!vb.empty() && vb[2] > 0) ? ww * vb[3] / vb[2] : 150;
    }
    w = ww; h = hh;
    return std::isfinite(w) && std::isfinite(h) && w > 0 && h > 0;
}

// Illustrator and some other tools put plain text entities in the DOCTYPE's internal subset
// (<!ENTITY st0 "fill:#FFF;">) and use them in attributes. The XML reader never expands entities, so they are
// replaced here first, and only this simple kind: a general entity whose literal value has no '&', '%' or '<' (so an
// expansion can hold no further reference), at most 256 of them, the document at most 16 MiB after expansion. A
// parameter or external entity refuses the file. Written from the XML 1.0 specification (sections 2.8, 4.2).
bool ExpandInternalEntities(std::string& text, std::string& err) {
    const std::size_t dt = text.find("<!DOCTYPE");
    if (dt == std::string::npos || dt > 65536) return true;
    std::size_t open = std::string::npos, q = dt + 9;
    for (; q < text.size() && text[q] != '>'; ++q) if (text[q] == '[') { open = q; break; }
    if (open == std::string::npos) return true;
    std::map<std::string, std::string> ents;
    std::size_t i = open + 1;
    while (true) {
        while (i < text.size() && IsSpace(text[i])) ++i;
        if (i >= text.size()) return Fail(err, "SVG: unterminated DOCTYPE");
        if (text[i] == ']') break;
        if (text.compare(i, 4, "<!--") == 0) {
            const std::size_t e = text.find("-->", i + 4);
            if (e == std::string::npos) return Fail(err, "SVG: unterminated comment");
            i = e + 3;
            continue;
        }
        if (text.compare(i, 8, "<!ENTITY") == 0) {
            std::size_t k = i + 8;
            while (k < text.size() && IsSpace(text[k])) ++k;
            if (k < text.size() && text[k] == '%') return Fail(err, "SVG: parameter entities are not supported");
            const std::size_t ns = k;
            while (k < text.size() && !IsSpace(text[k]) && text[k] != '"' && text[k] != '\'' && text[k] != '>') ++k;
            const std::string name = text.substr(ns, k - ns);
            while (k < text.size() && IsSpace(text[k])) ++k;
            if (name.empty() || k >= text.size() || (text[k] != '"' && text[k] != '\'')) return Fail(err, "SVG: external entities are not supported");
            const char quote = text[k];
            const std::size_t ve = text.find(quote, k + 1);
            if (ve == std::string::npos) return Fail(err, "SVG: unterminated entity value");
            const std::string value = text.substr(k + 1, ve - k - 1);
            if (value.find_first_of("&%<") != std::string::npos || value.size() > 4096 || ents.size() >= 256)
                return Fail(err, "SVG: entity declarations beyond plain text are not supported");
            ents.emplace(name, value);
            const std::size_t gt = text.find('>', ve);
            if (gt == std::string::npos) return Fail(err, "SVG: unterminated entity declaration");
            i = gt + 1;
            continue;
        }
        if (text.compare(i, 2, "<!") == 0 || text.compare(i, 2, "<?") == 0) {   // ELEMENT, ATTLIST, NOTATION, PIs: skipped
            std::size_t e = i + 2;
            char quote = 0;
            for (; e < text.size(); ++e) {
                if (quote) { if (text[e] == quote) quote = 0; continue; }
                if (text[e] == '"' || text[e] == '\'') quote = text[e];
                else if (text[e] == '>') break;
            }
            if (e >= text.size()) return Fail(err, "SVG: unterminated DOCTYPE");
            i = e + 1;
            continue;
        }
        return Fail(err, "SVG: unreadable DOCTYPE");
    }
    const std::size_t close = i;   // the ']'
    std::string out;
    out.reserve(text.size());
    out.append(text, 0, open);
    std::size_t p = close + 1;
    while (p < text.size()) {
        const std::size_t amp = text.find('&', p);
        if (amp == std::string::npos) { out.append(text, p, std::string::npos); break; }
        out.append(text, p, amp - p);
        const std::size_t semi = text.find(';', amp + 1);
        std::map<std::string, std::string>::const_iterator it = ents.end();
        if (semi != std::string::npos && semi - amp <= 64) it = ents.find(text.substr(amp + 1, semi - amp - 1));
        if (it != ents.end()) { out += it->second; p = semi + 1; }
        else { out += '&'; p = amp + 1; }
        if (out.size() > (std::size_t(16) << 20)) return Fail(err, "SVG: too large after entity expansion");
    }
    text.swap(out);
    return true;
}

bool ParseSvgDoc(const std::uint8_t* data, std::size_t size, xml::Doc& doc, std::string& err) {
    if (size > (std::size_t(16) << 20)) return Fail(err, "SVG file too large");
    xml::Limits lim;
    lim.maxBytes = std::size_t(16) << 20;
    lim.maxElements = 200000;
    lim.maxDepth = 128;
    lim.maxTextBytes = std::size_t(4) << 20;
    std::string text(reinterpret_cast<const char*>(data), size);
    if (!ExpandInternalEntities(text, err)) return false;
    std::string xerr;
    if (!xml::Parse(text, doc, xerr, lim)) { err = "SVG: " + xerr; return false; }
    if (doc.n.empty() || doc.Name(0) != "svg") return Fail(err, "SVG: the root element is not <svg>");
    return true;
}

// The rendered size: the intrinsic size (or the requested one), at most kMaxSide a side keeping the shape.
void SvgOutputSize(double w, double h, int reqW, int reqH, int& ow, int& oh) {
    double fw = reqW > 0 ? reqW : w, fh = reqH > 0 ? reqH : h;
    if (reqW > 0 && reqH <= 0) fh = h * fw / w;
    if (reqH > 0 && reqW <= 0) fw = w * fh / h;
    const double big = std::max(fw, fh);
    if (big > kMaxSide) { fw = fw * kMaxSide / big; fh = fh * kMaxSide / big; }
    ow = std::max(1, int(std::lround(fw)));
    oh = std::max(1, int(std::lround(fh)));
}

} // namespace

bool LooksLikeSvg(const std::uint8_t* d, std::size_t n) {
    // The prolog (XML declaration, comments, processing instructions, a DOCTYPE) and then a root element named svg.
    const char* s = reinterpret_cast<const char*>(d);
    std::size_t p = 0;
    if (n >= 3 && d[0] == 0xEF && d[1] == 0xBB && d[2] == 0xBF) p = 3;
    const std::size_t lim = std::min(n, std::size_t(65536));
    auto starts = [&](const char* t) { const std::size_t k = std::strlen(t); return lim - p >= k && std::memcmp(s + p, t, k) == 0; };
    while (true) {
        while (p < lim && IsSpace(s[p])) ++p;
        if (p >= lim || s[p] != '<') return false;
        if (starts("<?")) {
            const char* e = static_cast<const char*>(std::memchr(s + p, '>', lim - p));
            if (!e) return false;
            p = std::size_t(e - s) + 1;
        } else if (starts("<!--")) {
            std::size_t q = p + 4;
            while (q + 3 <= lim && std::memcmp(s + q, "-->", 3) != 0) ++q;
            if (q + 3 > lim) return false;
            p = q + 3;
        } else if (starts("<!DOCTYPE")) {
            std::size_t q = p + 9;
            while (q < lim && IsSpace(s[q])) ++q;
            if (!(lim - q >= 3 && (s[q] == 's' || s[q] == 'S') && (s[q + 1] == 'v' || s[q + 1] == 'V') && (s[q + 2] == 'g' || s[q + 2] == 'G'))) return false;
            int bracket = 0;
            for (; q < lim; ++q) {
                if (s[q] == '[') ++bracket;
                else if (s[q] == ']') --bracket;
                else if (s[q] == '>' && bracket <= 0) break;
            }
            if (q >= lim) return false;
            p = q + 1;
        } else {
            std::size_t q = p + 1;
            while (q < lim && !IsSpace(s[q]) && s[q] != '>' && s[q] != '/') ++q;
            std::string name(s + p + 1, q - p - 1);
            const std::size_t colon = name.find(':');
            if (colon != std::string::npos) name = name.substr(colon + 1);   // lint-allow: substr-loop (once, then return)
            return name == "svg" && q < lim;
        }
    }
}

bool DecodeSvg(const std::uint8_t* data, std::size_t size, int reqW, int reqH, int* width, int* height, std::vector<std::uint8_t>* rgba,
               std::string& err) {
    xml::Doc doc;
    if (!ParseSvgDoc(data, size, doc, err)) return false;
    double iw, ih;
    std::vector<double> vb;
    if (!IntrinsicSize(doc, iw, ih, vb)) return Fail(err, "SVG: bad size");
    int ow, oh;
    SvgOutputSize(iw, ih, reqW, reqH, ow, oh);
    if (width) *width = ow;
    if (height) *height = oh;
    if (!rgba) return true;
    Renderer r(doc, ow, oh);
    for (std::size_t i = 0; i < doc.n.size(); ++i) {
        const std::string* id = doc.Attr(int(i), "id");
        if (id && !r.ids.count(*id)) r.ids[*id] = int(i);
        if (doc.n[i].name == "style") ParseSheet(doc.n[i].text, r.rules);
    }
    // viewport: the intrinsic size scaled to the output, then the viewBox
    Mat ctm = Mat::ScaleBy(ow / iw, oh / ih);
    double vw = iw, vh = ih;
    bool draw = true;
    if (!vb.empty()) {
        if (vb[2] > 0 && vb[3] > 0) {
            ctm = ctm * Renderer::ViewBoxTransform(vb, doc.AttrOr(0, "preserveAspectRatio"), iw, ih);
            vw = vb[2]; vh = vb[3];
        } else {
            draw = false;   // a zero viewBox disables rendering (SVG 1.1 7.7)
        }
    }
    if (draw) {
        const Style root = r.StyleOf(0, Style());
        if (root.display) {
            const Mat rootCtm = ctm * r.TransformOf(0, root);
            for (int k : doc.Kids(0)) r.Node(k, rootCtm, root, root.opacity, 1, 0, nullptr, vw, vh);
        }
    }
    // premultiplied to straight alpha
    rgba->resize(r.canvas.size());
    for (std::size_t i = 0; i < r.canvas.size(); i += 4) {
        const int a = r.canvas[i + 3];
        std::uint8_t* o = &(*rgba)[i];
        if (a == 0) { o[0] = o[1] = o[2] = o[3] = 0; continue; }
        for (int c = 0; c < 3; ++c) o[c] = std::uint8_t(std::min(255, (r.canvas[i + std::size_t(c)] * 255 + a / 2) / a));
        o[3] = std::uint8_t(a);
    }
    return true;
}

} // namespace imgdec
} // namespace okay
