#pragma once
// The UI icon set: icons drawn by our own code from simple shapes (assets/ui/ui_icons.okaydata), crisp at any size.
//
// An icon is a list of shapes in a 24 x 24 box: circle, ring, ellipse, rectangle, rounded rectangle, line, polygon, polyline, arc, star
// and regular polygon. A shape that starts with `-` is cut out of what was drawn before it. `use <icon> rot=90 flip=h` turns or mirrors
// another icon. The picture for a size is made by sampling every pixel 4 x 4 times against the shapes, so the same icon at the same
// size always gives the same pixels (tests/test_ui_icons.cpp), on every machine. Written from the plain geometry of those shapes.
//
// Where icons are used: any picture or button icon path written as "icon:heart" draws the vector icon `heart` instead of loading a file
// (UIImage.texture, UIButton.icon), tinted by the widget's colour, and "[icon=icon:heart]" works in rich text.
#include "okay/UI/UILibData.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace okay { namespace uilib {

struct IconShape {
    char op = 0;                 // c o e r q l p s a t g
    bool cut = false;
    std::vector<float> v;        // the numbers, in 24 x 24 units
};
struct IconDef {
    std::string name, category, tags;
    std::vector<IconShape> shapes;
    std::string baseName;        // set for an icon made with `use` (counted as a variant of that drawing)
};

namespace icondetail {

inline std::vector<float> Numbers(const std::string& line, std::size_t from) {
    std::vector<float> v;
    const char* p = line.c_str() + from;
    while (*p) {
        while (*p == ' ') ++p;
        if (!*p) break;
        char* end = nullptr;
        const float f = std::strtof(p, &end);
        if (end == p) break;
        v.push_back(f);
        p = end;
    }
    return v;
}

// Turn a shape by `rot` degrees (0, 90, 180, 270) about the middle of the box and mirror it.
inline void Pt(float& x, float& y, int rot, bool fh, bool fv) {
    if (fh) x = 24.0f - x;
    if (fv) y = 24.0f - y;
    for (int i = 0; i < rot / 90; ++i) { const float nx = 24.0f - y, ny = x; x = nx; y = ny; }
}
inline IconShape Transform(const IconShape& s, int rot, bool fh, bool fv) {
    IconShape o = s;
    auto& v = o.v;
    switch (s.op) {
        case 'c': case 'o': Pt(v[0], v[1], rot, fh, fv); break;
        case 'e': Pt(v[0], v[1], rot, fh, fv); if ((rot / 90) % 2) std::swap(v[2], v[3]); break;
        case 'r': case 'q': {
            float x0 = v[0], y0 = v[1], x1 = v[0] + v[2], y1 = v[1] + v[3];
            Pt(x0, y0, rot, fh, fv); Pt(x1, y1, rot, fh, fv);
            v[0] = std::min(x0, x1); v[1] = std::min(y0, y1); v[2] = std::fabs(x1 - x0); v[3] = std::fabs(y1 - y0);
            break;
        }
        case 'l': Pt(v[0], v[1], rot, fh, fv); Pt(v[2], v[3], rot, fh, fv); break;
        case 'p': for (std::size_t i = 0; i + 1 < v.size(); i += 2) Pt(v[i], v[i + 1], rot, fh, fv); break;
        case 's': { const std::size_t n = v.size() / 2 * 2; for (std::size_t i = 0; i + 1 < n; i += 2) Pt(v[i], v[i + 1], rot, fh, fv); break; }
        case 'a': {
            float a0 = v[3], a1 = v[4];
            Pt(v[0], v[1], rot, fh, fv);
            if (fh) { const float t = 180.0f - a1; a1 = 180.0f - a0; a0 = t; }
            if (fv) { const float t = -a1; a1 = -a0; a0 = t; }
            v[3] = a0 + (float)rot; v[4] = a1 + (float)rot;
            break;
        }
        case 't': Pt(v[0], v[1], rot, fh, fv); v[5] = (fh != fv ? -v[5] : v[5]) + (float)rot; break;
        case 'g': Pt(v[0], v[1], rot, fh, fv); v[4] = (fh != fv ? -v[4] : v[4]) + (float)rot; break;
        default: break;
    }
    return o;
}

inline float DistSeg(float px, float py, float ax, float ay, float bx, float by) {
    const float dx = bx - ax, dy = by - ay, l2 = dx * dx + dy * dy;
    float t = l2 > 0 ? ((px - ax) * dx + (py - ay) * dy) / l2 : 0.0f;
    t = std::max(0.0f, std::min(1.0f, t));
    const float cx = ax + t * dx - px, cy = ay + t * dy - py;
    return std::sqrt(cx * cx + cy * cy);
}
inline bool InPoly(float px, float py, const float* pts, std::size_t n) {   // even-odd
    bool in = false;
    for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
        const float xi = pts[2 * i], yi = pts[2 * i + 1], xj = pts[2 * j], yj = pts[2 * j + 1];
        if (((yi > py) != (yj > py)) && (px < (xj - xi) * (py - yi) / (yj - yi) + xi)) in = !in;
    }
    return in;
}
inline float DistRoundBox(float px, float py, float x, float y, float w, float h, float r) {
    const float cx = x + w * 0.5f, cy = y + h * 0.5f;
    r = std::max(0.0f, std::min(r, std::min(w, h) * 0.5f));
    const float qx = std::fabs(px - cx) - (w * 0.5f - r), qy = std::fabs(py - cy) - (h * 0.5f - r);
    const float ox = std::max(qx, 0.0f), oy = std::max(qy, 0.0f);
    return std::sqrt(ox * ox + oy * oy) + std::min(std::max(qx, qy), 0.0f) - r;   // < 0 inside
}

} // namespace icondetail

/// The polygon points of a star (`t`) or a regular polygon (`g`), in 24-unit space.
inline std::vector<float> ShapePoints(const IconShape& s) {
    std::vector<float> pts;
    const float cx = s.v[0], cy = s.v[1];
    if (s.op == 't') {
        const float ro = s.v[2], ri = s.v[3], n = std::max(2.0f, s.v[4]), rot = s.v[5] * 0.0174532925f;
        for (int i = 0; i < (int)n * 2; ++i) {
            const float r = (i % 2 == 0) ? ro : ri, a = rot + (float)i * 3.14159265f / n;
            pts.push_back(cx + r * std::cos(a)); pts.push_back(cy + r * std::sin(a));
        }
    } else {
        const float r = s.v[2], n = std::max(3.0f, s.v[3]), rot = s.v[4] * 0.0174532925f;
        for (int i = 0; i < (int)n; ++i) {
            const float a = rot + (float)i * 6.2831853f / n;
            pts.push_back(cx + r * std::cos(a)); pts.push_back(cy + r * std::sin(a));
        }
    }
    return pts;
}

/// Is the point (x, y) (24-unit space) inside this shape?
inline bool ShapeHit(const IconShape& s, float x, float y) {
    using namespace icondetail;
    const auto& v = s.v;
    switch (s.op) {
        case 'c': return (x - v[0]) * (x - v[0]) + (y - v[1]) * (y - v[1]) <= v[2] * v[2];
        case 'o': { const float d = std::sqrt((x - v[0]) * (x - v[0]) + (y - v[1]) * (y - v[1])); return std::fabs(d - v[2]) <= v[3] * 0.5f; }
        case 'e': {
            const float nx = (x - v[0]) / v[2], ny = (y - v[1]) / v[3];
            const float d = nx * nx + ny * ny;
            if (v.size() >= 5 && v[4] > 0) { const float k = std::min(v[2], v[3]); const float in = 1.0f - v[4] / k; return d <= 1.0f && d >= in * in; }
            return d <= 1.0f;
        }
        case 'r': case 'q': {
            const float rad = s.op == 'q' ? v[4] : 0.0f;
            const float sw = v.size() > (s.op == 'q' ? 5u : 4u) ? v[s.op == 'q' ? 5 : 4] : -1.0f;
            const float d = DistRoundBox(x, y, v[0], v[1], v[2], v[3], rad);
            if (sw > 0) return d <= 0.0f && d >= -sw;
            return d <= 0.0f;
        }
        case 'l': { const float w = v.size() > 4 ? v[4] : 2.0f; return DistSeg(x, y, v[0], v[1], v[2], v[3]) <= w * 0.5f; }
        case 'p': return v.size() >= 6 && InPoly(x, y, v.data(), v.size() / 2);
        case 's': {
            const std::size_t n = v.size() / 2;
            const float w = (v.size() % 2) ? v.back() : 2.0f;
            for (std::size_t i = 0; i + 1 < n; ++i)
                if (DistSeg(x, y, v[2 * i], v[2 * i + 1], v[2 * i + 2], v[2 * i + 3]) <= w * 0.5f) return true;
            return false;
        }
        case 'a': {
            const float w = v.size() > 5 ? v[5] : 2.0f, dx = x - v[0], dy = y - v[1];
            const float d = std::sqrt(dx * dx + dy * dy);
            float a0 = v[3], a1 = v[4];
            if (a1 < a0) std::swap(a0, a1);
            const float ex0 = v[0] + v[2] * std::cos(a0 * 0.0174532925f), ey0 = v[1] + v[2] * std::sin(a0 * 0.0174532925f);
            const float ex1 = v[0] + v[2] * std::cos(a1 * 0.0174532925f), ey1 = v[1] + v[2] * std::sin(a1 * 0.0174532925f);
            if ((x - ex0) * (x - ex0) + (y - ey0) * (y - ey0) <= w * w * 0.25f) return true;
            if ((x - ex1) * (x - ex1) + (y - ey1) * (y - ey1) <= w * w * 0.25f) return true;
            if (std::fabs(d - v[2]) > w * 0.5f) return false;
            float ang = std::atan2(dy, dx) * 57.29578f;
            if (a1 - a0 >= 360.0f) return true;
            while (ang < a0) ang += 360.0f;
            while (ang >= a0 + 360.0f) ang -= 360.0f;
            return ang <= a1;
        }
        case 't': case 'g': {
            const std::vector<float> pts = ShapePoints(s);
            const std::size_t sw = s.op == 't' ? 7u : 5u;
            const float stroke = v.size() > sw ? v[sw] : -1.0f;
            if (stroke > 0) {
                const std::size_t n = pts.size() / 2;
                for (std::size_t i = 0; i < n; ++i) {
                    const std::size_t j = (i + 1) % n;
                    if (DistSeg(x, y, pts[2 * i], pts[2 * i + 1], pts[2 * j], pts[2 * j + 1]) <= stroke * 0.5f) return true;
                }
                return false;
            }
            return InPoly(x, y, pts.data(), pts.size() / 2);
        }
        default: return false;
    }
}

struct IconRegistry {
    std::vector<IconDef> icons;
    std::map<std::string, std::size_t> byName;
};

inline IconRegistry ParseIcons(const std::string& text) {
    using namespace icondetail;
    IconRegistry r;
    for (const DataSection& sec : ParseData(text)) {
        if (sec.kind != "icon") continue;
        IconDef d; d.name = sec.name; d.category = sec.Str("category"); d.tags = sec.Str("tags");
        for (const std::string& raw : sec.body) {
            const std::string line = Trim(raw);
            if (line.empty()) continue;
            if (line.compare(0, 4, "use ") == 0) {
                std::string rest = Trim(line.substr(4));
                std::string base = rest.substr(0, rest.find(' '));
                int rot = 0; bool fh = false, fv = false;
                const std::size_t rp = rest.find("rot="); if (rp != std::string::npos) rot = (std::atoi(rest.c_str() + rp + 4) % 360 + 360) % 360;
                const std::size_t fp = rest.find("flip="); if (fp != std::string::npos) { std::string f = rest.substr(fp + 5); fh = f.find('h') != std::string::npos; fv = f.find('v') != std::string::npos; }
                auto it = r.byName.find(base);
                if (it == r.byName.end()) continue;
                for (const IconShape& s : r.icons[it->second].shapes) d.shapes.push_back(Transform(s, rot, fh, fv));
                d.baseName = base;
                continue;
            }
            IconShape s;
            std::size_t i = 0;
            if (line[0] == '-') { s.cut = true; i = 1; }
            if (i >= line.size()) continue;
            s.op = line[i];
            s.v = Numbers(line, i + 1);
            std::size_t need = 0;
            switch (s.op) { case 'c': need = 3; break; case 'o': need = 4; break; case 'e': need = 4; break; case 'r': need = 4; break;
                            case 'q': need = 5; break; case 'l': need = 4; break; case 'p': need = 6; break; case 's': need = 4; break;
                            case 'a': need = 5; break; case 't': need = 6; break; case 'g': need = 5; break; default: need = 99; }
            if (s.v.size() < need) continue;   // a broken line is skipped (tests/test_ui_icons.cpp counts them)
            d.shapes.push_back(std::move(s));
        }
        r.byName[d.name] = r.icons.size();
        r.icons.push_back(std::move(d));
    }
    return r;
}
inline const IconRegistry& Icons() {
    static const IconRegistry r = ParseIcons(EmbeddedData("ui_icons"));
    return r;
}
inline const IconDef* FindIcon(const std::string& name) {
    auto it = Icons().byName.find(name);
    return it == Icons().byName.end() ? nullptr : &Icons().icons[it->second];
}

/// "icon:heart" -> "heart"; empty when the path is a normal file path.
inline std::string IconNameOf(const std::string& path) {
    return path.compare(0, 5, "icon:") == 0 ? path.substr(5) : std::string();
}

/// The picture of an icon at `px` x `px` pixels: one byte of coverage (0..255) per pixel, row by row. Deterministic.
inline std::vector<std::uint8_t> RenderIcon(const IconDef& icon, int px) {
    px = std::max(1, std::min(px, 1024));
    std::vector<std::uint8_t> out((std::size_t)px * (std::size_t)px, 0);
    const float k = 24.0f / (float)px;
    for (int y = 0; y < px; ++y)
        for (int x = 0; x < px; ++x) {
            int hits = 0;
            for (int sy = 0; sy < 4; ++sy)
                for (int sx = 0; sx < 4; ++sx) {
                    const float ux = ((float)x + ((float)sx + 0.5f) / 4.0f) * k, uy = ((float)y + ((float)sy + 0.5f) / 4.0f) * k;
                    bool in = false;
                    for (const IconShape& s : icon.shapes) {
                        if (ShapeHit(s, ux, uy)) in = !s.cut;
                    }
                    hits += in ? 1 : 0;
                }
            out[(std::size_t)y * (std::size_t)px + (std::size_t)x] = (std::uint8_t)(hits * 255 / 16);
        }
    return out;
}

/// A cheap number that changes when an icon's picture changes (FNV-1a over the pixels), for the determinism test.
inline std::uint32_t IconChecksum(const IconDef& icon, int px) {
    std::uint32_t h = 2166136261u;
    for (std::uint8_t b : RenderIcon(icon, px)) { h ^= b; h *= 16777619u; }
    return h;
}

/// Names matching a search text (every word must be in the name, category or tags), in file order.
inline std::vector<const IconDef*> SearchIcons(const std::string& query, const std::string& category = std::string()) {
    std::vector<std::string> words;
    std::string cur;
    for (char c : query + " ") {
        if (c == ' ') { if (!cur.empty()) words.push_back(cur); cur.clear(); }
        else cur.push_back((char)std::tolower((unsigned char)c));
    }
    std::vector<const IconDef*> out;
    for (const IconDef& d : Icons().icons) {
        if (!category.empty() && d.category != category) continue;
        std::string hay = d.name + " " + d.category + " " + d.tags;
        for (char& c : hay) c = (char)std::tolower((unsigned char)c);
        bool ok = true;
        for (const std::string& w : words) if (hay.find(w) == std::string::npos) { ok = false; break; }
        if (ok) out.push_back(&d);
    }
    return out;
}

}} // namespace okay::uilib
