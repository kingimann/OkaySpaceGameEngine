// The launcher's Downloads page, Notifications panel and Achievements page drawn with OkayUI (see OkayUILauncherPages.hpp).
// Written from what the launcher's Dear ImGui versions show and do (launcher/main.cpp); the drawing is OkayUI's own widgets and
// draw list. To look the same as those pages, every size, gap and colour below is the one the launcher's own Dear ImGui code
// uses (its style in DarkTheme and shared/OkayTheme.hpp: a 17 px font, frames padded 10 x 7, items 9 x 8 apart, windows padded
// 14 x 12), and the icons are the launcher's own line icons (DrawNavIcon in launcher/main.cpp) drawn again with OkayUI.
#include "OkayUILauncherPages.hpp"
#include "OkayUILauncherLook.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <unordered_map>

namespace UI = OkayUI;

namespace okayuiscreens {

namespace look {


// A child region the way the launcher's children are: no padding inside, a thin 8 px (or `bar`) scrollbar that takes room only
// while the content overflows.
bool BeginPlainChild(const char* id, float w, float h, float bar) {
    UI::PushStyleVar(UI::StyleVar_ChildPadding, 0.0f);
    UI::PushStyleVar(UI::StyleVar_ScrollbarSize, bar);
    return UI::BeginChild(id, w, h, false);
}
void EndPlainChild(float contentBottom) {
    // The content ends at `contentBottom` (no gap after it, as the launcher measures it).
    UI::PushStyleVar(UI::StyleVar_ItemSpacing, V(0.0f, 0.0f));
    UI::SetCursorScreenPos(V(UI::GetCursorScreenPos().x, contentBottom));
    UI::Dummy(0.0f, 0.0f);
    UI::PopStyleVar();
    UI::EndChild();
    UI::PopStyleVar(2);
}

// The page title as the launcher's page header draws it: a 1.34x title, a short accent bar under it, the subtitle, a faint line
// across the page. Returns where the page's content starts.
float PageHeader(const UI::DrawList& dl, float x0, float y0, float w, const char* title, const char* subtitle) {
    const float fs = UI::GetTextLineHeight();
    float y = NextY(y0, 2.0f);
    PushScale(1.34f);
    const UI::Vec2 ts = UI::CalcTextSize(title);
    dl.AddText(V(x0, y), CTitle(), title);
    UI::PopTextScale();
    dl.AddRectFilled(V(x0, y + ts.y + 5.0f), V(x0 + ts.x, y + ts.y + 7.5f), Col(UI::Style().accent), 1.5f);
    y = NextY(y, ts.y);
    const bool sub = subtitle && *subtitle;
    y = NextY(y, sub ? 11.0f : 13.0f);
    if (sub) {
        dl.AddText(V(x0, y), Col(UI::Style().textDim), subtitle);
        y = NextY(y, fs);
        y = NextY(y, 8.0f);
    }
    dl.AddRectFilled(V(x0, y), V(x0 + w, y + 1.0f), Rgb(255, 255, 255, 13));
    y = NextY(y, 1.0f);
    return NextY(y, 8.0f);
}

// A button as the launcher draws one: its control colour, lighter under the mouse, the accent while held, the label centred;
// a disabled one at 55% strength. `primary` is the accent-coloured kind. Returns true on a click.
bool FlatButton(const char* id, const char* label, float x, float y, float w, float h, bool enabled, bool primary, float rounding) {
    UI::SetCursorScreenPos(V(x, y));
    const bool clicked = UI::InvisibleButton(id, w, h) && enabled;
    const bool hov = enabled && UI::IsItemHovered(), held = hov && UI::IsItemActive();
    const float a = enabled ? 1.0f : 0.55f;
    const UI::DrawList& dl = UI::GetWindowDrawList();
    const unsigned char* acc = UI::Style().accent;
    UI::Color32 bg;
    if (primary) {
        const float lift = hov && !held ? 0.20f : 0.0f;
        bg = held ? Rgb((int)(acc[0] * 0.73f), (int)(acc[1] * 0.73f), (int)(acc[2] * 0.73f), (int)(255 * a))
                  : Rgb((int)(acc[0] + (255 - acc[0]) * lift), (int)(acc[1] + (255 - acc[1]) * lift), (int)(acc[2] + (255 - acc[2]) * lift),
                        (int)((hov ? 255.0f : 235.0f) * a));
    } else bg = held ? Rgb(acc[0], acc[1], acc[2], (int)(255 * a)) : hov ? CControlHover(a) : CControl(a);
    dl.AddRectFilled(V(x, y), V(x + w, y + h), bg, rounding);
    const UI::Vec2 ts = UI::CalcTextSize(label);
    const UI::Color32 tc = primary ? Rgb(255, 255, 255, (int)(255 * a)) : Col(UI::Style().text, a);
    const bool cut = ts.x > w - 2.0f;   // a label wider than its button is cut at the button's edge
    if (cut) dl.PushClipRect(V(x, y), V(x + w, y + h));
    dl.AddText(V(Tr(x + std::max(kFrameX, (w - ts.x) * 0.5f)), Tr(y + (h - ts.y) * 0.5f)), tc, label);
    if (cut) dl.PopClipRect();
    if (hov) UI::SetMouseCursor(UI::MouseCursor_Hand);
    return clicked;
}

// A 1 px rounded outline drawn cheaply (it is on every download row every frame): the four straight sides as bars and each
// corner as three short chords of its quarter circle.
void HairlineBox(const UI::DrawList& dl, UI::Vec2 mn, UI::Vec2 mx, UI::Color32 col, float r) {
    r = std::min(r, std::min(mx.x - mn.x, mx.y - mn.y) * 0.5f);
    dl.AddRectFilled(V(mn.x + r, mn.y), V(mx.x - r, mn.y + 1.0f), col);
    dl.AddRectFilled(V(mn.x + r, mx.y - 1.0f), V(mx.x - r, mx.y), col);
    dl.AddRectFilled(V(mn.x, mn.y + r), V(mn.x + 1.0f, mx.y - r), col);
    dl.AddRectFilled(V(mx.x - 1.0f, mn.y + r), V(mx.x, mx.y - r), col);
    const float rr = r - 0.5f;
    static const float k[4][2] = {{1.0f, 0.0f}, {0.8660254f, 0.5f}, {0.5f, 0.8660254f}, {0.0f, 1.0f}};   // cos, sin of 0, 30, 60, 90 degrees
    const UI::Vec2 c[4] = {V(mn.x + r, mn.y + r), V(mx.x - r, mn.y + r), V(mx.x - r, mx.y - r), V(mn.x + r, mx.y - r)};
    const float sx[4] = {-1, 1, 1, -1}, sy[4] = {-1, -1, 1, 1};
    for (int q = 0; q < 4; ++q)
        for (int i = 0; i < 3; ++i)
            dl.AddLine(V(c[q].x + sx[q] * k[i][0] * rr, c[q].y + sy[q] * k[i][1] * rr), V(c[q].x + sx[q] * k[i + 1][0] * rr, c[q].y + sy[q] * k[i + 1][1] * rr), col, 1.0f);
}

// One line of text centred in `width` from x.
void CenteredAt(const UI::DrawList& dl, float x, float y, float width, const char* s, UI::Color32 col) {
    dl.AddText(V(x + std::max(0.0f, (width - UI::CalcTextSize(s).x) * 0.5f), y), col, s);
}

// ---- icons ----
// Arcs come from unit-circle points worked out once per (start, end, count) instead of a sine and a cosine per point per frame.
// ArcTo adds the n + 1 points of an arc to the current path.
const UI::Vec2* UnitArc(float a0, float a1, int n) {
    struct Entry { float a0, a1; int n; std::vector<UI::Vec2> p; };
    static std::deque<Entry> cache;   // a deque keeps the returned pointers valid as it grows
    for (const Entry& e : cache) if (e.a0 == a0 && e.a1 == a1 && e.n == n) return e.p.data();
    Entry e{a0, a1, n, {}};
    for (int i = 0; i <= n; ++i) { const float a = a0 + (a1 - a0) * (float)i / (float)n; e.p.push_back(V(std::cos(a), std::sin(a))); }
    cache.push_back(std::move(e));
    return cache.back().p.data();
}
void ArcTo(const UI::DrawList& dl, UI::Vec2 c, float r, float a0, float a1, int n) {
    const UI::Vec2* u = UnitArc(a0, a1, n);
    for (int i = 0; i <= n; ++i) dl.PathLineTo(V(c.x + u[i].x * r, c.y + u[i].y * r));
}
// A rectangle outline stroked `t` wide centred half a pixel inside its edge, where the launcher's outlines put it (OkayUI's own
// AddRect strokes inward from the edge).
void OutlineRect(const UI::DrawList& dl, UI::Vec2 mn, UI::Vec2 mx, UI::Color32 col, float rounding, float t) {
    const float o = t * 0.5f - 0.5f;
    dl.AddRect(V(mn.x - o, mn.y - o), V(mx.x + o, mx.y + o), col, rounding + o, t);
}
// A ring stroked `t` wide centred half a pixel inside radius r (where the launcher's circles put their outline).
void Ring(const UI::DrawList& dl, UI::Vec2 c, float r, UI::Color32 col, int n, float t) {
    dl.AddCircle(c, r - 0.5f + t * 0.5f, col, n, t);
}

// The launcher's line icons (DrawNavIcon in launcher/main.cpp): 1.6 px strokes in a 15 px box.
void DrawNavIcon(const UI::DrawList& d, int id, UI::Vec2 c, UI::Color32 col) {
    const float r = 7.5f, t = 1.6f;
    switch (id) {
    case kIcoTrophy:
        d.PathClear();
        d.PathLineTo(V(c.x - 4.6f, c.y - r)); d.PathLineTo(V(c.x + 4.6f, c.y - r));
        ArcTo(d, V(c.x, c.y - 2.6f), 4.6f, 0.0f, kPi, 12);
        d.PathStroke(col, true, t);
        d.PathClear(); ArcTo(d, V(c.x - 4.6f, c.y - 4.6f), 2.4f, kPi * 0.5f, kPi * 1.5f, 8); d.PathStroke(col, false, t);
        d.PathClear(); ArcTo(d, V(c.x + 4.6f, c.y - 4.6f), 2.4f, -kPi * 0.5f, kPi * 0.5f, 8); d.PathStroke(col, false, t);
        d.AddLine(V(c.x, c.y + 2.0f), V(c.x, c.y + 5.6f), col, t);
        d.AddLine(V(c.x - 3.8f, c.y + r - 0.6f), V(c.x + 3.8f, c.y + r - 0.6f), col, t);
        break;
    case kIcoFriends:
        Ring(d, V(c.x - 2.4f, c.y - 2.8f), 3.0f, col, 16, t);
        d.PathClear(); ArcTo(d, V(c.x - 2.4f, c.y + 7.8f), 5.8f, kPi + 0.30f, 2.0f * kPi - 0.30f, 12); d.PathStroke(col, false, t);
        d.PathClear(); ArcTo(d, V(c.x + 4.2f, c.y - 2.8f), 2.6f, -kPi * 0.5f, kPi * 0.5f, 8); d.PathStroke(col, false, t);
        d.PathClear(); ArcTo(d, V(c.x + 4.2f, c.y + 7.8f), 4.6f, kPi * 1.55f, 2.0f * kPi - 0.30f, 8); d.PathStroke(col, false, t);
        break;
    case kIcoBell:
        d.PathClear();
        ArcTo(d, V(c.x, c.y - 1.2f), 5.0f, kPi, 2.0f * kPi, 12);
        d.PathLineTo(V(c.x + 5.0f, c.y + 3.2f)); d.PathLineTo(V(c.x + 6.6f, c.y + 5.0f));
        d.PathLineTo(V(c.x - 6.6f, c.y + 5.0f)); d.PathLineTo(V(c.x - 5.0f, c.y + 3.2f));
        d.PathStroke(col, true, t);
        d.AddLine(V(c.x - 1.8f, c.y + 7.4f), V(c.x + 1.8f, c.y + 7.4f), col, t);
        break;
    case kIcoChat:
        d.PathClear();
        ArcTo(d, c, 6.8f, kPi * 0.80f, kPi * 2.62f, 26);
        d.PathLineTo(V(c.x - r, c.y + r));
        d.PathStroke(col, true, t);
        break;
    case kIcoPlay:   // a play button in a circle
        Ring(d, c, r, col, 28, t);
        d.AddTriangleFilled(V(c.x - 2.3f, c.y - 3.8f), V(c.x - 2.3f, c.y + 3.8f), V(c.x + 4.0f, c.y), col);
        break;
    case kIcoGrid:   // a 2 x 2 grid of rounded squares
        for (int k = 0; k < 4; ++k) {
            const float gx = (k % 2) ? c.x + 1.2f : c.x - r, gy = (k / 2) ? c.y + 1.2f : c.y - r;
            OutlineRect(d, V(gx, gy), V(gx + r - 1.2f, gy + r - 1.2f), col, 1.5f, t);
        }
        break;
    case kIcoBag:   // a shopping bag
        OutlineRect(d, V(c.x - 6.2f, c.y - 3.0f), V(c.x + 6.2f, c.y + r), col, 2.0f, t);
        d.PathClear(); ArcTo(d, V(c.x, c.y - 3.0f), 3.2f, kPi, 2.0f * kPi, 10); d.PathStroke(col, false, t);
        break;
    case kIcoDownload:   // an arrow down into a tray
        d.AddLine(V(c.x, c.y - r), V(c.x, c.y + 2.6f), col, t);
        d.AddLine(V(c.x - 3.6f, c.y - 1.0f), V(c.x, c.y + 2.6f), col, t);
        d.AddLine(V(c.x + 3.6f, c.y - 1.0f), V(c.x, c.y + 2.6f), col, t);
        d.PathClear();
        d.PathLineTo(V(c.x - r, c.y + 2.2f)); d.PathLineTo(V(c.x - r, c.y + r));
        d.PathLineTo(V(c.x + r, c.y + r)); d.PathLineTo(V(c.x + r, c.y + 2.2f));
        d.PathStroke(col, false, t);
        break;
    case kIcoRefresh: {
        d.PathClear(); ArcTo(d, c, 5.6f, -kPi * 0.35f, kPi * 1.45f, 20); d.PathStroke(col, false, t);
        const UI::Vec2 e = V(c.x + std::cos(-kPi * 0.35f) * 5.6f, c.y + std::sin(-kPi * 0.35f) * 5.6f);
        d.AddTriangleFilled(V(e.x + 2.6f, e.y - 2.4f), V(e.x + 2.0f, e.y + 2.6f), V(e.x - 2.6f, e.y - 0.6f), col);
        break;
    }
    default: break;
    }
}

void DrawTrophy(const UI::DrawList& dl, UI::Vec2 c, float s, UI::Color32 col) {
    dl.PathClear();
    dl.PathLineTo(V(c.x - 5.6f * s, c.y - 7.5f * s));
    dl.PathLineTo(V(c.x + 5.6f * s, c.y - 7.5f * s));
    ArcTo(dl, V(c.x, c.y - 2.6f * s), 5.6f * s, 0.0f, kPi, 14);   // right, down round the bottom, to the left
    dl.PathFillConvex(col);
    dl.PathClear(); ArcTo(dl, V(c.x - 5.6f * s, c.y - 4.6f * s), 2.6f * s, kPi * 0.5f, kPi * 1.5f, 8); dl.PathStroke(col, false, 1.5f * s);
    dl.PathClear(); ArcTo(dl, V(c.x + 5.6f * s, c.y - 4.6f * s), 2.6f * s, -kPi * 0.5f, kPi * 0.5f, 8); dl.PathStroke(col, false, 1.5f * s);
    dl.AddRectFilled(V(c.x - 1.2f * s, c.y + 2.6f * s), V(c.x + 1.2f * s, c.y + 6.0f * s), col);
    dl.AddRectFilled(V(c.x - 4.8f * s, c.y + 5.8f * s), V(c.x + 4.8f * s, c.y + 8.4f * s), col, 1.2f * s);
}
// A five-pointed star: the centre pentagon and five tip triangles (the fill is convex piece by piece).
void DrawStar(const UI::DrawList& dl, UI::Vec2 c, float r, UI::Color32 col) {
    UI::Vec2 outer[5], inner[5];
    for (int i = 0; i < 5; ++i) {
        const float a = -kPi * 0.5f + i * (2.0f * kPi / 5.0f), b = a + kPi / 5.0f;
        outer[i] = V(c.x + std::cos(a) * r, c.y + std::sin(a) * r);
        inner[i] = V(c.x + std::cos(b) * r * 0.45f, c.y + std::sin(b) * r * 0.45f);
    }
    dl.AddConvexPolyFilled(inner, 5, col);
    for (int i = 0; i < 5; ++i) dl.AddTriangleFilled(outer[i], inner[i], inner[(i + 4) % 5], col);
}
// The achievement medal: a soft halo, a hexagon shaded from 62% of the id's colour at the top to 30% at the bottom with an
// outline in the colour, a trophy on it (as the launcher's DrawAchMedal).
void DrawMedal(const UI::DrawList& dl, UI::Vec2 c, float r, const unsigned char rgb[3], float alpha) {
    const float s = r / 15.0f;
    dl.AddCircleFilled(c, r + 3.0f, Rgb(rgb[0], rgb[1], rgb[2], (int)(0.14f * alpha * 255.0f)), 32);
    UI::Vec2 hex[6], rim[6];
    UI::Color32 shade[6];
    const UI::Vec2* u = UnitArc(0.0f, 2.0f * kPi, 6);
    const int a = (int)(alpha * 255.0f + 0.5f);
    for (int i = 0; i < 6; ++i) {
        hex[i] = V(c.x + u[i].x * r, c.y + u[i].y * r);
        rim[i] = V(c.x + u[i].x * (r - 0.5f), c.y + u[i].y * (r - 0.5f));
        const float t = std::max(0.0f, std::min(1.0f, (hex[i].y - (c.y - r)) / (2.0f * r))), k = 0.62f + (0.30f - 0.62f) * t;
        shade[i] = Rgb((int)(rgb[0] * k), (int)(rgb[1] * k), (int)(rgb[2] * k), a);
    }
    dl.AddConvexPolyFilledMultiColor(hex, shade, 6);
    dl.AddPolyline(rim, 6, Rgb(rgb[0], rgb[1], rgb[2], a), true, 1.8f);
    DrawTrophy(dl, c, s, Rgb((int)(rgb[0] * 0.30f + 178.5f), (int)(rgb[1] * 0.30f + 178.5f), (int)(rgb[2] * 0.30f + 178.5f), a));
}
// A round [x]: a disc that brightens on hover and two strokes.
void DrawCross(const UI::DrawList& dl, UI::Vec2 c, float r, float arm, bool hover, UI::Color32 col, int baseAlpha, int hoverAlpha, float t) {
    const int da = hover ? hoverAlpha : baseAlpha;
    if (da > 0) dl.AddCircleFilled(c, r, Rgb(255, 255, 255, da), 20);
    dl.AddLine(V(c.x - arm, c.y - arm), V(c.x + arm, c.y + arm), col, t);
    dl.AddLine(V(c.x + arm, c.y - arm), V(c.x - arm, c.y + arm), col, t);
}

} // namespace look

using namespace look;

// ---- text helpers ----------------------------------------------------------------------------------------------------------

std::string FitTextWidth(const std::string& s, float maxW) {
    if (!(maxW > 0.0f)) return std::string();
    if (UI::CalcTextSize(s.c_str()).x <= maxW) return s;
    static const char* kEll = "\xE2\x80\xA6";
    const float ew = UI::CalcTextSize(kEll).x;
    // Binary search over character boundaries: the most characters whose prefix still fits with the ellipsis.
    std::vector<std::size_t> starts;   // byte offset where each character begins: a prefix of k characters is starts[k] bytes
    starts.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) if (((unsigned char)s[i] & 0xC0) != 0x80) starts.push_back(i);
    std::size_t lo = 0, hi = starts.empty() ? 0 : starts.size() - 1;   // the whole text does not fit, so fewer than all
    while (lo < hi) {
        const std::size_t mid = (lo + hi + 1) / 2;
        if (UI::CalcTextSize(s.substr(0, starts[mid]).c_str()).x + ew <= maxW) lo = mid; else hi = mid - 1;
    }
    return s.substr(0, lo < starts.size() ? starts[lo] : 0) + kEll;
}

void WrapTwoLines(const std::string& s, float maxW, std::string& l1, std::string& l2) {
    l1.clear(); l2.clear();
    if (UI::CalcTextSize(s.c_str()).x <= maxW) { l1 = s; return; }
    std::size_t cut = 0;
    for (std::size_t i = 0; i < s.size(); ++i)
        if (s[i] == ' ') { if (UI::CalcTextSize(s.substr(0, i).c_str()).x <= maxW) cut = i; else break; }
    if (cut == 0) { l1 = FitTextWidth(s, maxW); return; }
    l1 = s.substr(0, cut);
    l2 = FitTextWidth(s.substr(cut + 1), maxW);
}

// ---- Downloads -------------------------------------------------------------------------------------------------------------

std::string HumanBytes(long long b) {
    char out[32];
    if (b <= 0) std::snprintf(out, sizeof(out), "--");
    else if (b < 1024) std::snprintf(out, sizeof(out), "%lld B", b);
    else if (b < 1024 * 1024) std::snprintf(out, sizeof(out), "%.1f KB", b / 1024.0);
    else if (b < 1024LL * 1024 * 1024) std::snprintf(out, sizeof(out), "%.1f MB", b / (1024.0 * 1024));
    else std::snprintf(out, sizeof(out), "%.2f GB", b / (1024.0 * 1024 * 1024));
    return out;
}

const char* DownloadStatusText(int s) {
    return s == 0 ? "Queued" : s == 1 ? "Downloading" : s == 2 ? "Completed" : s == 3 ? "Failed" : "Canceled";
}

bool DownloadIsZip(const std::string& dest) {
    if (dest.size() <= 4) return false;
    const std::string ext = dest.substr(dest.size() - 4);
    return ext == ".zip" || ext == ".ZIP";
}

float DownloadFraction(const DownloadEntry& d) {
    float f = d.total > 0 ? (float)((double)d.received / (double)d.total) : (d.status == 2 ? 1.0f : 0.0f);
    if (!(f > 0.0f)) f = 0.0f;
    return std::min(f, 1.0f);
}

namespace {
// The launcher's progress bar: the frame colour, the fill in its bar colour (the rounded bar cut where the fill ends), and the
// text just right of the fill end, kept inside the bar.
void LauncherProgressBar(const UI::DrawList& dl, float x, float y, float w, float h, float frac, const char* overlay) {
    dl.AddRectFilled(V(x, y), V(x + w, y + h), CField(), 6.0f);
    const float fillW = w * std::max(0.0f, std::min(1.0f, frac));
    if (fillW >= 12.0f) {   // rounded where the bar is rounded, square where the fill stops short of the end
        dl.AddRectFilled(V(x, y), V(x + fillW, y + h), CProgress(), 6.0f);
        if (fillW < w - 6.0f) dl.AddRectFilled(V(x + fillW - 6.0f, y), V(x + fillW, y + h), CProgress());
    }
    else if (fillW > 0.0f) {   // a sliver: the bar's rounded left end, cut where the fill ends
        dl.PushClipRect(V(x, y), V(x + std::max(1.0f, fillW), y + h));
        dl.AddRectFilled(V(x, y), V(x + 12.0f, y + h), CProgress(), 6.0f);
        dl.PopClipRect();
    }
    if (overlay && *overlay) {
        const UI::Vec2 ts = UI::CalcTextSize(overlay);
        const float tx = std::max(x, std::min(x + fillW + kSpX, x + w - ts.x - kInnerX));
        dl.AddText(V(tx, y + (h - ts.y) * 0.5f), Col(UI::Style().text), overlay);
    }
}
} // namespace

DownloadsResult DrawDownloads(const DownloadsModel& m, float x, float y, float w, float h) {
    DownloadsResult res;
    const std::size_t n = m.items.size();
    res.rows.resize(n); res.cancelButtons.resize(n); res.openButtons.resize(n); res.installButtons.resize(n); res.removeButtons.resize(n);
    for (const DownloadEntry& d : m.items) if (d.status >= 2) { res.clearEnabled = true; break; }
    res.empty = m.items.empty();

    const LauncherLook look;
    UI::BareNextWindow();
    if (UI::Begin("Downloads##okayui", x, y, w, h)) {
        const UI::Theme& th = UI::Style();
        const UI::DrawList& wd = UI::GetWindowDrawList();
        const float fs = UI::GetTextLineHeight(), fh = fs + 2.0f * kFrameY;
        float cy = PageHeader(wd, x, y, w, "Downloads", "Games you download from the marketplace land here.");
        if (FlatButton("##dlopen", "Open downloads folder", x, cy, 200.0f, fh)) res.openFolder = true;
        res.openFolderButton = UI::GetItemRect();
        if (FlatButton("##dlclear", "Clear finished", x + 200.0f + kSpX, cy, 130.0f, fh, res.clearEnabled)) res.clearFinished = true;
        res.clearFinishedButton = UI::GetItemRect();
        cy = NextY(cy, fh);
        cy = NextY(cy, 6.0f);

        UI::SetCursorScreenPos(V(x, cy));
        BeginPlainChild("##dllist", w, std::max(8.0f, y + h - cy));
        const UI::DrawList& dl = UI::GetWindowDrawList();
        const UI::Vec2 o = UI::GetCursorScreenPos();
        const float cw = UI::GetContentRegionAvail().x;
        float ly = o.y;
        if (res.empty) {
            ly = NextY(ly, 34.0f);
            const float icx = o.x + cw * 0.5f, icy = ly + 12.0f;
            const UI::Color32 ic = Col(th.textDim);   // an arrow down into a tray
            dl.AddLine(V(icx, icy - 16), V(icx, icy + 6), ic, 2.6f);
            dl.AddLine(V(icx - 9, icy - 3), V(icx, icy + 6), ic, 2.6f);
            dl.AddLine(V(icx + 9, icy - 3), V(icx, icy + 6), ic, 2.6f);
            dl.AddLine(V(icx - 15, icy + 15), V(icx + 15, icy + 15), ic, 2.6f);
            ly = NextY(ly, 40.0f);
            CenteredAt(dl, o.x, ly, cw, "No downloads yet", Col(th.text));
            ly = NextY(NextY(ly, fs), 2.0f);
            CenteredAt(dl, o.x, ly, cw, "Games you get from the Marketplace land here.", Col(th.textDim));
            ly = NextY(NextY(ly, fs), 12.0f);
            if (FlatButton("##dlbrowse", "Browse Marketplace", o.x + (cw - 210.0f) * 0.5f, ly, 210.0f, 38.0f, true, true)) res.browseMarketplace = true;
            res.browseButton = UI::GetItemRect();
            ly += 38.0f;
        }
        // Each download is a bordered box as tall as its three lines (title; bar and status; buttons) and the box's padding.
        const float padX = 14.0f, padY = 12.0f, lineBar = std::max(16.0f, kFrameY + fs);
        const float rowH = padY * 2.0f + fs + kSpY + lineBar + kSpY + fs;
        for (std::size_t i = 0; i < n; ++i) {
            const DownloadEntry& d = m.items[i];
            UI::PushID(d.id);
            const UI::Vec2 rp = V(o.x, ly);
            res.rows[i] = UI::Rect{rp.x, rp.y, cw, rowH};
            const bool hov = UI::IsMouseHoveringRect(rp, V(rp.x + cw, rp.y + rowH));
            HairlineBox(dl, rp, V(rp.x + cw, rp.y + rowH), CBorder(), 8.0f);
            if (hov) dl.AddRect(rp, V(rp.x + cw, rp.y + rowH), Col(th.accent), 7.0f, 2.0f);
            const float ix = rp.x + padX, inW = cw - 2.0f * padX;
            float iy = rp.y + padY;
            dl.AddText(V(ix, iy), Col(th.text), FitTextWidth(d.title, inW).c_str());
            iy = NextY(iy, fs);
            char overlay[64];
            if (d.total > 0) std::snprintf(overlay, sizeof(overlay), "%s / %s", HumanBytes(d.received).c_str(), HumanBytes(d.total).c_str());
            else std::snprintf(overlay, sizeof(overlay), "%s", HumanBytes(d.received).c_str());
            const float barW = std::max(40.0f, inW - 220.0f);
            LauncherProgressBar(dl, ix, iy, barW, 16.0f, DownloadFraction(d), overlay);
            const char* st = DownloadStatusText(d.status);
            const UI::Color32 sc = d.status == 2 ? Rgb(140, 230, 153) : d.status == 3 ? Rgb(230, 115, 115) : d.status == 4 ? Rgb(191, 179, 128) : Col(th.textDim);
            dl.AddText(V(ix + barW + kSpX, iy + kFrameY), sc, st);
            iy = NextY(iy, lineBar);
            // The buttons its state has, as the launcher's small buttons (no frame padding up and down).
            float bx = ix;
            auto small = [&](const char* id, const char* label, UI::Rect& rect) {
                const float bw = UI::CalcTextSize(label).x + 2.0f * kFrameX;
                const bool c = FlatButton(id, label, bx, iy, bw, fs);
                rect = UI::GetItemRect();
                bx += bw + kSpX;
                return c;
            };
            if (d.status == 0 || d.status == 1) {
                if (small("##dlcancel", "Cancel", res.cancelButtons[i])) res.cancelId = d.id;
            } else {
                if (d.status == 2) {
                    if (small("##dlopenitem", "Open folder", res.openButtons[i])) res.openItemFolderId = d.id;
                    if (DownloadIsZip(d.dest) && small("##dlinstall", "Install to Community", res.installButtons[i])) res.installId = d.id;
                }
                if (small("##dlremove", "Remove", res.removeButtons[i])) res.removeId = d.id;
            }
            UI::PopID();
            ly = NextY(ly, rowH);
        }
        EndPlainChild(n > 0 ? ly - kSpY : ly);
    }
    UI::End();
    return res;
}

// ---- Notifications ---------------------------------------------------------------------------------------------------------

namespace {
int NotifKind(const NotifEntry& n) { return (n.kind >= 1 && n.kind <= 3) ? n.kind : 0; }
const char* kChipLabel[6] = {"All", "Unread", "Friends", "Messages", "Updates", "System"};
const int kFilterKind[6] = {-1, -1, 1, 2, 3, 0};
} // namespace

bool NotifPassesFilter(const NotifEntry& n, int filter) {
    if (filter == 1) return n.unread;
    if (filter >= 2 && filter <= 5) return NotifKind(n) == kFilterKind[filter];
    return true;
}

std::string NotifAgeText(unsigned a) {
    char rt[16];
    if (a < 60) std::snprintf(rt, sizeof(rt), "now");
    else if (a < 3600) std::snprintf(rt, sizeof(rt), "%um", a / 60);
    else if (a < 86400) std::snprintf(rt, sizeof(rt), "%uh", a / 3600);
    else std::snprintf(rt, sizeof(rt), "%ud", a / 86400);
    return rt;
}

std::vector<int> NotifOrder(const NotificationsModel& m, int filter) {
    std::vector<int> order;
    for (int i = 0; i < (int)m.items.size(); ++i) if (NotifPassesFilter(m.items[(std::size_t)i], filter)) order.push_back(i);
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return m.items[(std::size_t)a].ageSeconds < m.items[(std::size_t)b].ageSeconds; });
    return order;
}

void NotifEmptyText(int total, int filter, std::string& title, std::string& hint) {
    const bool caughtUp = total == 0 || filter == 1;
    title = caughtUp ? "You're all caught up" : "Nothing here";
    hint = total == 0 ? "Friend requests, messages and updates show up here."
         : filter == 1 ? "No unread notifications."
         : filter == 2 ? "No friend notifications."
         : filter == 3 ? "No message notifications."
         : filter == 4 ? "No update notifications."
                       : "No system notifications.";
}

namespace {
// The feed's two body lines are measured again only when the text, the width or the text size changed (wrapping measures
// many prefixes of the text; the feed is the same frame after frame). A small cache, cleared when it grows past 256 entries.
void WrapTwoLinesCached(const std::string& body, float maxW, std::string& l1, std::string& l2) {
    struct Wrapped { float w, fs; std::string l1, l2; };
    static std::unordered_map<std::string, Wrapped> cache;
    const float fs = UI::GetTextLineHeight();
    auto it = cache.find(body);
    if (it != cache.end() && it->second.w == maxW && it->second.fs == fs) { l1 = it->second.l1; l2 = it->second.l2; return; }
    WrapTwoLines(body, maxW, l1, l2);
    if (cache.size() > 256) cache.clear();
    cache[body] = Wrapped{maxW, fs, l1, l2};
}
} // namespace

NotificationsResult DrawNotifications(const NotificationsModel& m, NotificationsState& st, float x, float y, float w, float h) {
    NotificationsResult res;
    if (st.filter < 0 || st.filter > 5) st.filter = 0;
    res.total = (int)m.items.size();
    for (const NotifEntry& n : m.items) res.unread += n.unread ? 1 : 0;

    const LauncherLook look;
    UI::SetNextWindowPos(V(x, y));
    UI::SetNextWindowSize(V(w, h));
    UI::PushStyleVar(UI::StyleVar_WindowPadding, 0.0f);   // the panel's own padding (16 x 14, as the launcher's) is laid out below
    const bool open = UI::Begin("Notifications##okayui", nullptr, UI::Window_NoTitleBar | UI::Window_NoResize | UI::Window_NoMove | UI::Window_NoCollapse |
                                UI::Window_NoScrollbar | (m.drawFrame ? 0 : UI::Window_NoBackground));
    UI::PopStyleVar();
    if (open) {
        const UI::Theme& th = UI::Style();
        const UI::Color32 cText = Col(th.text), cDim = Col(th.textDim), accLt = AccentLight();
        const UI::DrawList& dl = UI::GetWindowDrawList();
        const float fs = UI::GetTextLineHeight();

        // ---- header: title and "N new", then Mark all read, Clear and the close button at the right ----
        const UI::Vec2 hp = V(x + 16.0f, y + 14.0f);
        const float cw = w - 32.0f, hh = 28.0f;
        PushScale(1.22f);
        const UI::Vec2 tsz = UI::CalcTextSize("Notifications");
        dl.AddText(V(hp.x, hp.y + (hh - tsz.y) * 0.5f), cText, "Notifications");
        UI::PopTextScale();
        if (res.unread > 0) {
            char nb[24]; std::snprintf(nb, sizeof(nb), "%d new", res.unread);
            const UI::Vec2 bs = UI::CalcTextSize(nb);
            const UI::Vec2 b0 = V(hp.x + tsz.x + 10.0f, hp.y + hh * 0.5f - 10.0f);
            dl.AddRectFilled(b0, V(b0.x + bs.x + 14.0f, b0.y + 20.0f), AccentAlpha(0.24f), 10.0f);
            dl.AddText(V(b0.x + 7.0f, b0.y + (20.0f - bs.y) * 0.5f), accLt, nb);
        }
        auto textButton = [&](const char* id, const char* label, float bx, bool enabled, bool accent, UI::Rect& rect) {
            const UI::Vec2 ts = UI::CalcTextSize(label);
            const UI::Vec2 p = V(bx, hp.y + (hh - 26.0f) * 0.5f), sz = V(ts.x + 16.0f, 26.0f);
            UI::SetCursorScreenPos(p);
            const bool c = UI::InvisibleButton(id, sz.x, sz.y) && enabled;
            rect = UI::GetItemRect();
            const bool hov = enabled && UI::IsItemHovered();
            if (hov) { dl.AddRectFilled(p, V(p.x + sz.x, p.y + sz.y), Rgb(255, 255, 255, 16), 7.0f); UI::SetMouseCursor(UI::MouseCursor_Hand); }
            const UI::Color32 col = !enabled ? Col(th.textDim, 0.55f) : hov ? cText : accent ? accLt : cDim;
            dl.AddText(V(p.x + 8.0f, p.y + (sz.y - ts.y) * 0.5f), col, label);
            return c;
        };
        {
            const UI::Vec2 xp = V(hp.x + cw - 26.0f, hp.y + (hh - 26.0f) * 0.5f);
            UI::SetCursorScreenPos(xp);
            if (UI::InvisibleButton("##ntclose", 26.0f, 26.0f)) res.close = true;
            res.closeButton = UI::GetItemRect();
            const bool xh = UI::IsItemHovered();
            if (xh) UI::SetTooltip("%s", "Close  (Esc)");
            DrawCross(dl, V(xp.x + 13.0f, xp.y + 13.0f), 13.0f, 4.5f, xh, xh ? cText : cDim, 0, 20, 1.6f);
            const float clearW = UI::CalcTextSize("Clear").x + 16.0f, markW = UI::CalcTextSize("Mark all read").x + 16.0f;
            const float clearX = xp.x - 6.0f - clearW, markX = clearX - 2.0f - markW;
            if (textButton("##ntmark", "Mark all read", markX, res.unread > 0, true, res.markAllButton)) res.markAllRead = true;
            if (textButton("##ntclear", "Clear", clearX, res.total > 0, false, res.clearButton)) res.clearAll = true;
        }

        // ---- filter chips (wrapping onto a second row on a narrow panel) ----
        float listTop = 0.0f;
        {
            const UI::Vec2 cp = V(hp.x, NextY(hp.y + hh + 10.0f, 0.0f));
            const float ch = fs + 10.0f;
            float cx = cp.x, cy = cp.y;
            for (int k = 0; k < 6; ++k) {
                const UI::Vec2 ts = UI::CalcTextSize(kChipLabel[k]);
                char cnt[16] = "";
                if (k == 1 && res.unread > 0) std::snprintf(cnt, sizeof(cnt), "%d", res.unread);
                const UI::Vec2 cs = UI::CalcTextSize(cnt);
                const float cwid = ts.x + 18.0f + (cnt[0] ? cs.x + 16.0f : 0.0f);
                if (cx > cp.x && cx + cwid > cp.x + cw) { cx = cp.x; cy += ch + 6.0f; }
                UI::SetCursorScreenPos(V(cx, cy));
                UI::PushID(k);
                if (UI::InvisibleButton("##ntchip", cwid, ch)) st.filter = k;
                res.chips.push_back(UI::GetItemRect());
                const bool hov = UI::IsItemHovered();
                UI::PopID();
                const bool on = st.filter == k;
                dl.AddRectFilled(V(cx, cy), V(cx + cwid, cy + ch), on ? AccentAlpha(0.24f) : Rgb(255, 255, 255, hov ? 22 : 9), ch * 0.5f);
                dl.AddText(V(cx + 9.0f, cy + (ch - ts.y) * 0.5f), on ? accLt : (hov ? cText : cDim), kChipLabel[k]);
                if (cnt[0]) {
                    const float bx = cx + 9.0f + ts.x + 6.0f, bw = cs.x + 10.0f;
                    dl.AddRectFilled(V(bx, cy + (ch - 16.0f) * 0.5f), V(bx + bw, cy + (ch + 16.0f) * 0.5f), Col(th.accent), 8.0f);
                    dl.AddText(V(bx + 5.0f, cy + (ch - cs.y) * 0.5f), Rgb(255, 255, 255), cnt);
                }
                if (hov) UI::SetMouseCursor(UI::MouseCursor_Hand);
                cx += cwid + 6.0f;
            }
            const float ly = cy + ch + 12.0f;
            dl.AddLine(V(cp.x - 16.0f, ly), V(cp.x + cw + 16.0f, ly), Rgb(255, 255, 255, 16), 1.0f);
            listTop = NextY(ly, 4.0f);
        }

        // ---- the feed, newest first, grouped Today / Yesterday / Earlier ----
        res.shown = NotifOrder(m, st.filter);
        UI::SetCursorScreenPos(V(hp.x, listTop));
        BeginPlainChild("##ntlist", cw, std::max(8.0f, y + h - 14.0f - listTop), 6.0f);
        res.list = UI::Rect{hp.x, listTop, cw, std::max(8.0f, y + h - 14.0f - listTop)};
        const UI::Vec2 o = UI::GetCursorScreenPos();
        float yy = o.y, contentEnd = o.y;
        {
            const UI::DrawList& ld = UI::GetWindowDrawList();
            const float tl = fs;
            int lastGroup = -1;
            const UI::Input& in = UI::GetFrameInput();
            for (int idx : res.shown) {
                const NotifEntry& n = m.items[(std::size_t)idx];
                const int kind = NotifKind(n);
                const int grp = n.day < 0 ? 0 : n.day > 2 ? 2 : n.day;
                if (grp != lastGroup) {
                    if (lastGroup != -1) yy = NextY(yy, 4.0f);
                    PushScale(0.80f);
                    ld.AddText(V(o.x + 4.0f, yy), cDim, grp == 0 ? "TODAY" : grp == 1 ? "YESTERDAY" : "EARLIER");
                    yy = NextY(yy, UI::GetTextLineHeight());
                    UI::PopTextScale();
                    yy = NextY(yy, 1.0f);
                    lastGroup = grp;
                }
                UI::PushID(idx);
                const UI::Vec2 rp = V(o.x, yy);
                const float rw = UI::GetContentRegionAvail().x;
                const float icD = 36.0f, tx = rp.x + 10.0f + icD + 12.0f;
                const float textW = rw - (tx - rp.x) - 46.0f;
                std::string b1, b2;
                WrapTwoLinesCached(n.body, textW, b1, b2);
                const int bodyLines = n.body.empty() ? 0 : (b2.empty() ? 1 : 2);
                const float rh = std::max(58.0f, 11.0f + tl + 3.0f + bodyLines * (tl + 1.0f) + 10.0f);
                const float rx = rp.x + rw - 12.0f;
                const UI::Rect xr{rx - 22.0f, rp.y + 8.0f, 22.0f, 22.0f};   // the dismiss button, shown while the row is hovered
                UI::SetCursorScreenPos(rp);
                const bool click = UI::InvisibleButton("##ntrow", rw, rh);
                const UI::Rect rowRect = UI::GetItemRect();
                res.rows.push_back(rowRect);
                const bool hov = UI::IsItemHovered();
                const bool visible = UI::IsItemVisible();
                const bool overX = hov && in.mouseX >= xr.x && in.mouseX < xr.x + xr.w && in.mouseY >= xr.y && in.mouseY < xr.y + xr.h;
                if (hov) { res.dismissButton = xr; UI::SetMouseCursor(UI::MouseCursor_Hand); }
                if (click) { if (overX) res.dismissed = idx; else res.clicked = idx; }
                if (visible) {
                    ld.AddRectFilled(rp, V(rp.x + rw, rp.y + rh), hov ? Rgb(255, 255, 255, 15) : n.unread ? AccentAlpha(0.07f) : Rgb(0, 0, 0, 0), 10.0f);
                    // the kind's icon on a tinted disc (the launcher's own line icons)
                    const unsigned char kc[4][3] = {{242, 184, 77}, {97, 173, 250}, {97, 204, 140}, {168, 138, 250}};
                    const unsigned char* c = kc[kind];
                    const UI::Vec2 ic = V(rp.x + 10.0f + icD * 0.5f, rp.y + 11.0f + icD * 0.5f - 2.0f);
                    ld.AddCircleFilled(ic, icD * 0.5f, Rgb(c[0], c[1], c[2], 43), 32);
                    const UI::Color32 icol = Rgb((int)(c[0] * 0.8f + 51), (int)(c[1] * 0.8f + 51), (int)(c[2] * 0.8f + 51));
                    DrawNavIcon(ld, kind == 1 ? kIcoFriends : kind == 2 ? kIcoChat : kind == 3 ? kIcoRefresh : kIcoBell, ic, icol);
                    // the title (doubled for weight when unread), then up to two body lines
                    const std::string title = FitTextWidth(n.title.empty() ? std::string("Notification") : n.title, textW);
                    const UI::Color32 tcol = n.unread ? cText : Col(th.text, 0.80f);
                    ld.AddText(V(tx, rp.y + 11.0f), tcol, title.c_str());
                    if (n.unread) ld.AddText(V(tx + 0.7f, rp.y + 11.0f), tcol, title.c_str());
                    float by = rp.y + 11.0f + tl + 3.0f;
                    if (!b1.empty()) { ld.AddText(V(tx, by), cDim, b1.c_str()); by += tl + 1.0f; }
                    if (!b2.empty()) ld.AddText(V(tx, by), cDim, b2.c_str());
                    // right column: the age, or the dismiss button while hovered; the unread dot
                    if (hov) {
                        DrawCross(ld, V(xr.x + 11.0f, xr.y + 11.0f), 11.0f, 3.5f, overX, overX ? cText : cDim, 14, 34, 1.5f);
                        if (overX) UI::SetTooltip("%s", "Dismiss");
                    } else {
                        const std::string age = NotifAgeText(n.ageSeconds);
                        const UI::Vec2 ts = UI::CalcTextSize(age.c_str());
                        ld.AddText(V(rx - ts.x, rp.y + 11.0f), n.unread ? accLt : cDim, age.c_str());
                    }
                    if (n.unread) ld.AddCircleFilled(V(rx - 5.0f, rp.y + 11.0f + tl + 3.0f + tl * 0.5f + 1.0f), 4.5f, Col(th.accent), 14);
                }
                UI::PopID();
                yy = NextY(rp.y + rh + 3.0f, 0.0f);
                contentEnd = rp.y + rh + 3.0f;   // the content ends 3 px under the last row
            }
            if (res.shown.empty()) {   // ---- the empty state ----
                res.empty = true;
                NotifEmptyText(res.total, st.filter, res.emptyTitle, res.emptyHint);
                const float aw = UI::GetContentRegionAvail().x, ah = UI::GetContentRegionAvail().y;
                yy = NextY(yy, std::max(16.0f, ah * 0.20f));
                const UI::Vec2 c = V(o.x + aw * 0.5f, yy + 30.0f);
                ld.AddCircleFilled(c, 30.0f, Rgb(255, 255, 255, 12), 40);
                DrawNavIcon(ld, kIcoBell, c, cDim);
                if (res.total == 0 || st.filter == 1) {   // a small green check badge
                    const UI::Vec2 bc = V(c.x + 20.0f, c.y + 20.0f);
                    ld.AddCircleFilled(bc, 10.0f, CRaised(), 20);
                    ld.AddCircleFilled(bc, 8.0f, Rgb(64, 186, 104), 20);
                    const UI::Vec2 pts[3] = {V(bc.x - 3.6f, bc.y + 0.2f), V(bc.x - 1.0f, bc.y + 2.8f), V(bc.x + 3.8f, bc.y - 2.8f)};
                    ld.AddPolyline(pts, 3, Rgb(255, 255, 255), false, 1.8f);
                }
                yy = NextY(yy, 74.0f);
                PushScale(1.08f);
                CenteredAt(ld, o.x, yy, aw, res.emptyTitle.c_str(), cText);
                yy = NextY(yy, UI::GetTextLineHeight());
                UI::PopTextScale();
                CenteredAt(ld, o.x, yy, aw, res.emptyHint.c_str(), cDim);
                contentEnd = yy + fs;
            }
        }
        EndPlainChild(contentEnd);
    }
    UI::End();
    return res;
}

// ---- Achievements ----------------------------------------------------------------------------------------------------------

std::string AchPrettyId(const std::string& id) {
    std::string o; bool up = true;
    for (char c : id) {
        if (c == '_' || c == '-') { o += ' '; up = true; continue; }
        o += up ? (char)std::toupper((unsigned char)c) : c;
        up = false;
    }
    return o;
}

int AchMonthCount(const AchievementsModel& m) {
    int k = 0;
    for (const AchEntry& a : m.items) if (a.when > m.now - 30L * 86400) ++k;
    return k;
}

std::vector<int> AchList(const AchievementsModel& m, int view, int sort, const std::string& filter) {
    const std::string q = Lower(filter);
    std::vector<int> list;
    for (int i = 0; i < (int)m.items.size(); ++i) {
        const AchEntry& a = m.items[(std::size_t)i];
        if (view == 1 && a.when <= m.now - 30L * 86400) continue;
        if (view == 2 && !a.showcased) continue;
        if (!q.empty() && Lower(a.label + " " + a.id).find(q) == std::string::npos) continue;
        list.push_back(i);
    }
    std::vector<std::string> lower;
    if (sort == 2) for (const AchEntry& a : m.items) lower.push_back(Lower(a.label));
    std::sort(list.begin(), list.end(), [&](int x, int y) {
        if (sort == 2) return lower[(std::size_t)x] < lower[(std::size_t)y];
        const long wx = m.items[(std::size_t)x].when, wy = m.items[(std::size_t)y].when;
        return sort == 1 ? wx < wy : wx > wy;
    });
    return list;
}

AchievementsResult DrawAchievements(const AchievementsModel& m, AchievementsState& st, float x, float y, float w, float h) {
    AchievementsResult res;
    if (st.view < 0 || st.view > 2) st.view = 0;
    if (st.sort < 0 || st.sort > 2) st.sort = 0;
    st.filter[sizeof(st.filter) - 1] = '\0';
    res.monthCount = AchMonthCount(m);
    const UI::Color32 gold = Rgb(250, 200, 70);

    const LauncherLook look;
    UI::BareNextWindow();
    if (UI::Begin("Achievements##okayui", x, y, w, h)) {
        const UI::Theme& th = UI::Style();
        const UI::Color32 cText = Col(th.text), cDim = Col(th.textDim), accLt = AccentLight();
        const UI::DrawList& wd = UI::GetWindowDrawList();
        const float fs = UI::GetTextLineHeight();
        float cy = PageHeader(wd, x, y, w, "Achievements", nullptr);

        // ---- header card: the three newest medals, the count, the latest unlock, level, points, this month, profile ----
        int latest = -1;
        for (int i = 0; i < (int)m.items.size(); ++i) if (latest < 0 || m.items[(std::size_t)i].when > m.items[(std::size_t)latest].when) latest = i;
        {
            const float cardH = 16.0f * 2.0f + 66.0f;   // its padding (18 x 16) around 66 px of content
            wd.AddRectFilled(V(x, cy), V(x + w, cy + cardH), CRaised(), 12.0f);
            const UI::DrawList& dl = wd;
            const UI::Vec2 p = V(x + 18.0f, cy + 16.0f);
            const float avail = w - 36.0f;
            std::vector<int> top((std::size_t)m.items.size());
            for (int i = 0; i < (int)top.size(); ++i) top[(std::size_t)i] = i;
            std::stable_sort(top.begin(), top.end(), [&](int a, int b) { return m.items[(std::size_t)a].when > m.items[(std::size_t)b].when; });
            const int heroN = std::min<int>(3, (int)top.size());
            for (int k = heroN - 1; k >= 0; --k)
                DrawMedal(dl, V(p.x + 30.0f + k * 26.0f, p.y + 32.0f), k == 0 ? 26.0f : 22.0f, m.items[(std::size_t)top[(std::size_t)k]].color, k == 0 ? 1.0f : 0.75f);
            if (heroN == 0) {
                UI::Vec2 hex[6];
                const UI::Vec2* u = UnitArc(0.0f, 2.0f * kPi, 6);
                for (int i = 0; i < 6; ++i) hex[i] = V(p.x + 30.0f + u[i].x * 23.5f, p.y + 32.0f + u[i].y * 23.5f);
                dl.AddPolyline(hex, 6, cDim, true, 1.8f);
                DrawNavIcon(dl, kIcoTrophy, V(p.x + 30.0f, p.y + 32.0f), cDim);
            }
            const float tx = p.x + 30.0f + std::max(1, heroN) * 26.0f + 32.0f;
            char cnt[24]; std::snprintf(cnt, sizeof(cnt), "%d", (int)m.items.size());
            PushScale(1.45f);
            const UI::Vec2 csz = UI::CalcTextSize(cnt);
            dl.AddText(V(tx, p.y + 2.0f), cText, cnt);
            UI::PopTextScale();
            dl.AddText(V(tx + csz.x + 8.0f, p.y + 8.0f), cText, m.items.size() == 1 ? "achievement unlocked" : "achievements unlocked");
            const float ly = NextY(p.y + 2.0f, std::max(csz.y, 6.0f + fs));   // the second line, under the first
            if (latest >= 0) {
                const AchEntry& a = m.items[(std::size_t)latest];
                const std::string line = "Latest: " + (a.label.empty() ? AchPrettyId(a.id) : a.label) + "  \xC2\xB7  " + a.rel;
                dl.AddText(V(tx, ly), cDim, line.c_str());
            } else dl.AddText(V(tx, ly), cDim, "Play games to start earning achievements.");
            const float bw = 150.0f, rx = p.x + avail;
            auto stat = [&](float sx, const char* big, const char* small) {
                PushScale(1.2f);
                const UI::Vec2 bs = UI::CalcTextSize(big);
                dl.AddText(V(sx, p.y + 6.0f), cText, big);
                UI::PopTextScale();
                PushScale(0.8f);
                dl.AddText(V(sx, p.y + 10.0f + bs.y), cDim, small);
                UI::PopTextScale();
            };
            if (avail > 640.0f) {
                const UI::Vec2 lc = V(rx - bw - 300.0f, p.y + 30.0f);
                const float prog = std::max(0.0f, std::min(1.0f, m.levelProgress == m.levelProgress ? m.levelProgress : 0.0f));
                Ring(dl, lc, 22.0f, Rgb(255, 255, 255, 30), 40, 4.0f);
                if (prog > 0.0f) {
                    dl.PathClear();
                    ArcTo(dl, lc, 22.0f, -kPi * 0.5f, -kPi * 0.5f + prog * 2.0f * kPi, 40);
                    dl.PathStroke(gold, false, 4.0f);
                }
                char ls[12]; std::snprintf(ls, sizeof(ls), "%d", m.level);
                PushScale(1.2f);
                const UI::Vec2 lsz = UI::CalcTextSize(ls);
                dl.AddText(V(lc.x - lsz.x * 0.5f, lc.y - lsz.y * 0.5f), cText, ls);
                UI::PopTextScale();
                UI::SetCursorScreenPos(V(lc.x - 24.0f, lc.y - 24.0f));
                UI::InvisibleButton("##lvl", 48.0f, 48.0f);
                res.levelBadge = UI::GetItemRect();
                if (UI::IsItemHovered())
                    UI::SetTooltip("Level %d  \xC2\xB7  %d%% to level %d\nEvery achievement earns points; games can make hard ones worth more.",
                                   m.level, (int)(prog * 100.0f), m.level + 1);
                char ps[24]; std::snprintf(ps, sizeof(ps), "%d", m.points);
                char ms[16]; std::snprintf(ms, sizeof(ms), "%d", res.monthCount);
                stat(rx - bw - 230.0f, ps, "POINTS");
                stat(rx - bw - 120.0f, ms, "THIS MONTH");
            }
            if (FlatButton("##achprofile", "View my profile", rx - bw, p.y + 12.0f, bw, 34.0f)) res.viewProfile = true;
            res.profileButton = UI::GetItemRect();
            cy = NextY(NextY(cy, cardH), 8.0f);
        }

        // ---- chips, sort and search ----
        {
            const UI::DrawList& dl = wd;
            const UI::Vec2 cp = V(x, cy);
            const float ch = fs + 12.0f, right = x + w;
            float cx = cp.x;
            const char* labels[3] = {"All", "This month", "Showcased"};
            const int counts[3] = {(int)m.items.size(), res.monthCount, m.showcaseCount};
            for (int k = 0; k < 3; ++k) {
                char cnt[12]; std::snprintf(cnt, sizeof(cnt), "%d", counts[k]);
                const UI::Vec2 ts = UI::CalcTextSize(labels[k]), cs = UI::CalcTextSize(cnt);
                const float cw = ts.x + cs.x + 40.0f;
                UI::SetCursorScreenPos(V(cx, cp.y));
                UI::PushID(k);
                if (UI::InvisibleButton("##achchip", cw, ch)) st.view = k;
                res.chips.push_back(UI::GetItemRect());
                const bool hov = UI::IsItemHovered();
                UI::PopID();
                const bool on = st.view == k;
                dl.AddRectFilled(V(cx, cp.y), V(cx + cw, cp.y + ch), on ? AccentAlpha(0.24f) : Rgb(255, 255, 255, hov ? 22 : 9), ch * 0.5f);
                dl.AddText(V(cx + 13.0f, cp.y + (ch - ts.y) * 0.5f), on ? accLt : (hov ? cText : cDim), labels[k]);
                const float bx = cx + 13.0f + ts.x + 7.0f;
                dl.AddRectFilled(V(bx, cp.y + (ch - 16.0f) * 0.5f), V(bx + cs.x + 10.0f, cp.y + (ch + 16.0f) * 0.5f), Rgb(255, 255, 255, 28), 8.0f);
                dl.AddText(V(bx + 5.0f, cp.y + (ch - cs.y) * 0.5f), Rgb(255, 255, 255), cnt);
                if (hov) UI::SetMouseCursor(UI::MouseCursor_Hand);
                cx += cw + 8.0f;
            }
            const float sortW = 132.0f;
            static const char* kSorts[3] = {"Newest first", "Oldest first", "A to Z"};
            UI::SetCursorScreenPos(V(right - sortW, cp.y));
            UI::SetNextItemWidth(sortW);
            UI::PushStyleVar(UI::StyleVar_FrameRounding, 14.0f);
            UI::PushStyleVar(UI::StyleVar_BorderSize, 0.0f);
            UI::Combo("##achsort", kSorts, 3, &st.sort);
            res.sortCombo = UI::GetItemRect();
            UI::PopStyleVar(2);
            const float sw = std::min(280.0f, right - sortW - 10.0f - (cx + 10.0f));
            if (sw > 120.0f) {
                const UI::Vec2 sp = V(right - sortW - 10.0f - sw, cp.y);
                UI::SetCursorScreenPos(sp);
                UI::SetNextItemWidth(sw);
                UI::PushStyleVar(UI::StyleVar_FrameRounding, 16.0f);
                UI::PushStyleVar(UI::StyleVar_FramePadding, V(30.0f, kFrameY));
                UI::PushStyleVar(UI::StyleVar_BorderSize, 0.0f);
                UI::InputTextWithHint("##achsearch", "Search achievements", st.filter, (int)sizeof(st.filter));
                res.searchField = UI::GetItemRect();
                UI::PopStyleVar(3);
                const UI::Vec2 mc = V(sp.x + 15.0f, sp.y + res.searchField.h * 0.5f - 1.0f);   // the magnifier
                Ring(dl, mc, 4.6f, cDim, 16, 1.5f);
                dl.AddLine(V(mc.x + 3.4f, mc.y + 3.4f), V(mc.x + 6.8f, mc.y + 6.8f), cDim, 1.8f);
            }
            cy = NextY(cp.y + ch + 12.0f, 0.0f);
        }

        // ---- the grid of medal tiles ----
        res.shown = AchList(m, st.view, st.sort, st.filter);
        UI::SetCursorScreenPos(V(x, cy));
        BeginPlainChild("##achlist", w, std::max(8.0f, y + h - cy));
        const UI::DrawList& dl = UI::GetWindowDrawList();
        const UI::Vec2 o = UI::GetCursorScreenPos();
        float contentEnd = o.y;
        if (res.shown.empty()) {
            res.empty = true;
            const float aw = UI::GetContentRegionAvail().x;
            float ly = NextY(o.y, 30.0f);
            const UI::Vec2 c = V(o.x + aw * 0.5f, ly + 30.0f);
            dl.AddCircleFilled(c, 30.0f, Rgb(255, 255, 255, 12), 40);
            DrawNavIcon(dl, kIcoTrophy, c, cDim);
            ly = NextY(ly, 70.0f);
            auto line = [&](const char* s, bool dim) { CenteredAt(dl, o.x, ly, aw, s, dim ? cDim : cText); ly = NextY(ly, fs); };
            if (m.items.empty()) {
                res.emptyTitle = "No achievements yet";
                line(res.emptyTitle.c_str(), false);
                line("Games award them with the \"Unlock Achievement\" action. Go play something!", true);
                ly = NextY(ly, 8.0f);
                if (FlatButton("##achbrowse", "Browse games", o.x + std::max(0.0f, (aw - 150.0f) * 0.5f), ly, 150.0f, 34.0f, true, true)) res.browseGames = true;
                res.browseButton = UI::GetItemRect();
                ly += 34.0f + kSpY;
            } else if (st.view == 2) {
                res.emptyTitle = "Nothing showcased yet";
                line(res.emptyTitle.c_str(), false);
                line("Pick up to 6 achievements to show off on your profile (right-click a tile).", true);
            } else if (st.view == 1) {
                res.emptyTitle = "No new achievements this month";
                line(res.emptyTitle.c_str(), false);
                line("Your latest unlocks will show up here.", true);
            } else {
                res.emptyTitle = "No achievements match your search";
                line(res.emptyTitle.c_str(), false);
            }
            contentEnd = ly - kSpY;
        } else {
            const float gap = 12.0f, tileH = 78.0f;
            const float aw = UI::GetContentRegionAvail().x;
            res.columns = std::max(1, std::min(4, (int)((aw + gap) / (260.0f + gap))));
            const float tileW = (aw - gap * (res.columns - 1)) / res.columns;
            for (std::size_t i = 0; i < res.shown.size(); ++i) {
                const AchEntry& a = m.items[(std::size_t)res.shown[i]];
                const int r = (int)i / res.columns, c = (int)i % res.columns;
                const UI::Vec2 p = V(o.x + c * (tileW + gap), o.y + r * (tileH + gap));
                UI::SetCursorScreenPos(p);
                UI::PushID(a.id.c_str());
                UI::InvisibleButton("##achtile", tileW, tileH);
                res.tiles.push_back(UI::GetItemRect());
                const bool hov = UI::IsItemHovered();
                const bool visible = UI::IsItemVisible();
                if (UI::BeginPopupContextItem("##achctx")) {
                    if (a.showcased) {
                        if (UI::Selectable("Remove from showcase", false)) { res.removeShowcase = a.id; UI::CloseCurrentPopup(); }
                        res.menuShowcase = UI::GetItemRect();
                    } else {
                        const bool room = m.showcaseCount < m.showcaseMax;   // six at most, as the profile shows
                        UI::BeginDisabled(!room);
                        if (UI::Selectable("Add to showcase", false) && room) { res.addShowcase = a.id; UI::CloseCurrentPopup(); }
                        res.menuShowcase = UI::GetItemRect();
                        UI::EndDisabled();
                    }
                    if (UI::Selectable("Copy id", false)) { res.copyId = a.id; UI::CloseCurrentPopup(); }
                    res.menuCopyId = UI::GetItemRect();
                    UI::EndPopup();
                }
                UI::PopID();
                if (!visible) continue;   // scrolled out of the list: laid out (for the scroll range) but not drawn
                const bool fresh = a.when > m.now - 2L * 86400;
                dl.AddRectFilled(p, V(p.x + tileW, p.y + tileH), hov ? Rgb(47, 51, 59) : CRaised(), 12.0f);   // a hovered tile is 4% lighter
                if (a.showcased) dl.AddRect(p, V(p.x + tileW, p.y + tileH), Rgb(255, 204, 77, 115), 12.0f, 1.5f);
                DrawMedal(dl, V(p.x + 38.0f, p.y + tileH * 0.5f), 24.0f, a.color, 1.0f);
                const float tx = p.x + 74.0f, maxW = tileW - 74.0f - 14.0f - (a.showcased ? 16.0f : 0.0f);
                dl.AddText(V(tx, p.y + 18.0f), cText, FitTextWidth(a.label.empty() ? AchPrettyId(a.id) : a.label, maxW).c_str());
                char ptxt[16]; std::snprintf(ptxt, sizeof(ptxt), "+%d", a.points);
                const UI::Vec2 pts = UI::CalcTextSize(ptxt);
                dl.AddRectFilled(V(tx, p.y + 40.0f), V(tx + pts.x + 12.0f, p.y + 40.0f + pts.y + 2.0f), Rgb(250, 200, 70, a.points >= 50 ? 60 : 32), 8.0f);
                dl.AddText(V(tx + 6.0f, p.y + 41.0f), gold, ptxt);
                dl.AddText(V(tx + pts.x + 20.0f, p.y + 40.0f), cDim, FitTextWidth(a.date.empty() ? std::string("Unlocked") : a.date, maxW - pts.x - 20.0f).c_str());
                if (a.showcased) DrawStar(dl, V(p.x + tileW - 18.0f, p.y + 18.0f), 6.0f, gold);
                if (fresh) {   // "NEW" on the last two days' unlocks
                    UI::PushTextScale(0.72f);   // an exact size (not rounded), as the launcher draws its NEW pill
                    const UI::Vec2 ns = UI::CalcTextSize("NEW");
                    const UI::Vec2 np = V(p.x + tileW - ns.x - 22.0f - (a.showcased ? 18.0f : 0.0f), p.y + 11.0f);
                    dl.AddRectFilled(V(np.x - 6.0f, np.y - 2.0f), V(np.x + ns.x + 6.0f, np.y + ns.y + 2.0f), Col(th.accent), 8.0f);
                    dl.AddText(np, Rgb(255, 255, 255), "NEW");
                    UI::PopTextScale();
                }
                if (hov) {
                    UI::SetMouseCursor(UI::MouseCursor_Hand);
                    UI::SetTooltip("%s\n%s  \xC2\xB7  %s\nRight-click to %s your showcase", a.label.c_str(), a.id.c_str(), a.rel.c_str(),
                                   a.showcased ? "remove it from" : "add it to");
                }
            }
            const int rows = ((int)res.shown.size() + res.columns - 1) / res.columns;
            contentEnd = o.y + rows * (tileH + gap) - gap;
        }
        EndPlainChild(contentEnd);
    }
    UI::End();
    return res;
}

} // namespace okayuiscreens
