// Browser: the OkayUI view of one tab (see BrowserView.hpp).
#include "BrowserView.hpp"

#include "okay/UI/OkayUICompat.hpp"   // BeginChild with flags: a page with no padding
#include "okay/Browser/Profile.hpp"   // the site badge, the address shown, the error pages
#include "okay/Graphics/TtfFont.hpp"   // EmbeddedUIFontData: the launcher look's font, for its em
#include "okay/Graphics/TtfRaster.hpp"   // the font's em and line (ttf::Face)

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <map>

namespace okayuiscreens {

namespace UI = OkayUI;
namespace br = okay::browser;

namespace {

UI::Color32 Pack(const br::Color& c) {
    return (UI::Color32)c.r | ((UI::Color32)c.g << 8) | ((UI::Color32)c.b << 16) | ((UI::Color32)c.a << 24);
}

// The address as shown in the toolbar: cut in the middle when it is too long.
std::string ShortUrl(const std::string& url, std::size_t max) {
    if (url.size() <= max || max < 8) return url;
    const std::size_t keep = (max - 3) / 2;
    return url.substr(0, keep) + "..." + url.substr(url.size() - keep);
}

// ---- web fonts ----
struct WebFace { int weight; bool italic; const UI::Font* font; FontEm em; };
std::map<std::string, std::vector<WebFace>>& Faces() { static std::map<std::string, std::vector<WebFace>> f; return f; }

// The registered face for a font-family list ("'My Font', Arial, sans-serif"): the first family that has one, the closest weight.
const UI::Font* WebFontFor(const br::FontSpec& spec) {
    if (spec.family.empty() || Faces().empty()) return nullptr;
    std::size_t i = 0;
    while (i < spec.family.size()) {
        std::size_t e = spec.family.find(',', i);
        if (e == std::string::npos) e = spec.family.size();
        std::string name = spec.family.substr(i, e - i);
        while (!name.empty() && (name.front() == ' ' || name.front() == '"' || name.front() == '\'')) name.erase(name.begin());
        while (!name.empty() && (name.back() == ' ' || name.back() == '"' || name.back() == '\'')) name.pop_back();
        auto it = Faces().find(name);
        if (it != Faces().end() && !it->second.empty()) {
            const WebFace* best = nullptr;
            int bestScore = 1 << 30;
            for (const WebFace& f : it->second) {
                const int score = std::abs(f.weight - spec.weight) + (f.italic != spec.italic ? 1000 : 0);
                if (score < bestScore) { bestScore = score; best = &f; }
            }
            return best ? best->font : nullptr;
        }
        i = e + 1;
    }
    return nullptr;
}
// True when the chosen face is already bold (no fake bold then).
bool FaceIsBold(const br::FontSpec& spec, const UI::Font* f) {
    if (!f) return false;
    for (const auto& kv : Faces()) for (const WebFace& w : kv.second) if (w.font == f) return w.weight >= 600 || !spec.bold;
    return false;
}

} // namespace

void RegisterWebFont(const std::string& family, int weight, bool italic, const UI::Font* font, const FontEm& em) {
    if (!font || family.empty()) return;
    Faces()[family].push_back({weight, italic, font, em});
}
void ClearWebFonts() { Faces().clear(); }

bool FontEmOf(const std::uint8_t* data, std::size_t size, FontEm& out) {
    okay::ttf::Face face;
    if (!data || !face.Load(data, size, 0) || face.UnitsPerEm() <= 0) return false;
    int asc = 0, desc = 0, gap = 0;
    face.VMetrics(asc, desc, gap);
    if (asc - desc <= 0 || asc <= 0) return false;
    const float upem = (float)face.UnitsPerEm();
    // a font whose line is not within 0.5 to 3 em is not trusted (a broken table would make text huge or invisible)
    const float line = (float)(asc - desc) / upem, ascent = (float)asc / upem;
    if (!(line >= 0.5f && line <= 3.0f && ascent > 0.2f && ascent <= line)) return false;
    out.linePerEm = line;
    out.ascentPerEm = ascent;
    return true;
}

namespace {

// The em of the font text is drawn with when no web font is chosen: the launcher look's TrueType font (read once from its
// file), else OkayUI's 8x8 bitmap font, whose cell is taken as the em (the layout's own proportions, nothing changes).
const FontEm& DefaultEm() {
    static FontEm ttf;
    static bool read = false;
    static const FontEm bitmap;
    if (!UI::LauncherLook()) return bitmap;
    if (!read) {
        read = true;
        std::vector<unsigned char> bytes;
        if (!okay::EmbeddedUIFontData(bytes) || !FontEmOf(bytes.data(), bytes.size(), ttf)) ttf = FontEm();
    }
    return ttf;
}
const FontEm& EmOf(const UI::Font* web) {
    if (web) for (const auto& kv : Faces()) for (const WebFace& w : kv.second) if (w.font == web) return w.em;
    return DefaultEm();
}

} // namespace

// OkayUI sizes a font by its line (ascent - descent, "pixel height"); its text scale 1 is an 8 px line. CSS sizes text by the em
// (CSS Fonts 4, font-size: the em square), and a font's line is usually 1.1 to 1.3 em, so a font size of `px` is the scale
// px x linePerEm / 8 (scaling by px / 8 drew text about 15% too small). Measuring and drawing both set it, so the width the
// layout used is the width that is drawn. A web font is made the current font first (OkayUI then sizes it by the same scale).
// `k` is the scale for DrawList::AddText; `shift` moves the drawn text so its baseline is where the layout put it (0.8 em
// below the em box's top, Layout.cpp) rather than the font's ascent below it.
struct TextScaleFor {
    float saved;
    const UI::Font* savedFont = nullptr;
    bool fontSet = false;
    float k = 1.0f, shift = 0.0f;
    explicit TextScaleFor(float px, const UI::Font* font = nullptr) : saved(0.0f) {
        if (font) { savedFont = UI::GetFont(); UI::SetFont(font); fontSet = true; }
        const FontEm& em = EmOf(font);
        k = px * em.linePerEm / 8.0f;
        shift = px * (0.8f - em.ascentPerEm);
        saved = UI::Style().textScale;
        UI::Style().textScale = k;
    }
    ~TextScaleFor() {
        UI::Style().textScale = saved;
        if (fontSet) UI::SetFont(savedFont);
    }
};

float OkayUIMeasure::Width(const std::string& utf8, const br::FontSpec& font) const {
    if (utf8.empty() || !(font.size > 0.0f)) return 0.0f;
    const UI::Font* web = WebFontFor(font);
    const TextScaleFor scale(font.size, web);
    float w = UI::CalcTextWidth(utf8.c_str());
    if (font.bold && !FaceIsBold(font, web)) w += std::max(1.0f, font.size / 24.0f);   // the drawn bold is the text twice, a little apart
    return w;
}

UI::Rect EmbedPlayButton(const br::DrawCmd& c) {
    const float bw = std::min(std::max(c.w - 16.0f, 20.0f), 120.0f), bh = std::min(std::max(c.h - 8.0f, 12.0f), 30.0f);
    return UI::Rect{c.x + (c.w - bw) * 0.5f, c.y + (c.h - bh) * 0.5f, bw, bh};
}

namespace {

void DrawText(const UI::DrawList* dl, float x, float y, const br::FontSpec& f, UI::Color32 col, const std::string& text) {
    const UI::Font* web = WebFontFor(f);
    const TextScaleFor scale(f.size, web);
    y += scale.shift;
    dl->AddText(UI::Vec2(x, y), col, text.c_str(), scale.k);
    if (f.bold && !FaceIsBold(f, web)) dl->AddText(UI::Vec2(x + std::max(1.0f, f.size / 24.0f), y), col, text.c_str(), scale.k);
}

// A form control's inside (its box and border are drawn by the page): the value, a check mark, a list arrow, a button label.
void DrawControl(const UI::DrawList* dl, const br::LayoutResult& page, const br::DrawCmd& c, float cx, float cy, float z, bool editing) {
    if (c.control < 0 || (std::size_t)c.control >= page.controls.size()) return;
    const br::Node* el = page.controls[(std::size_t)c.control].node;
    if (!el) return;
    const br::ControlKind k = br::KindOf(*el);
    const UI::Color32 col = Pack(c.color);
    br::FontSpec f = c.font;
    f.size *= z;
    const float w = c.w * z, h = c.h * z;
    // appearance: none (CSS UI 4, 7.2): the page draws the control with its own CSS; no native check mark, dot or list arrow
    const bool native = !page.controls[(std::size_t)c.control].appearanceNone;
    if (!native && (k == br::ControlKind::Checkbox || k == br::ControlKind::Radio)) return;
    switch (k) {
    case br::ControlKind::Checkbox: {
        const float s = std::min(w, h);
        dl->AddRectFilled(UI::Vec2(cx, cy), UI::Vec2(cx + s, cy + s), 0xFFFFFFFFu, 2.0f);
        dl->AddRect(UI::Vec2(cx, cy), UI::Vec2(cx + s, cy + s), Pack(br::Color{118, 118, 118, 255}), 2.0f, 1.0f);
        if (c.text == "\x01") {
            dl->AddRectFilled(UI::Vec2(cx, cy), UI::Vec2(cx + s, cy + s), Pack(br::Color{0, 117, 255, 255}), 2.0f);
            dl->AddLine(UI::Vec2(cx + s * 0.2f, cy + s * 0.52f), UI::Vec2(cx + s * 0.42f, cy + s * 0.74f), 0xFFFFFFFFu, std::max(1.5f, s / 7));
            dl->AddLine(UI::Vec2(cx + s * 0.42f, cy + s * 0.74f), UI::Vec2(cx + s * 0.8f, cy + s * 0.28f), 0xFFFFFFFFu, std::max(1.5f, s / 7));
        }
        break;
    }
    case br::ControlKind::Radio: {
        const float s = std::min(w, h), r = s * 0.5f;
        dl->AddCircleFilled(UI::Vec2(cx + r, cy + r), r, 0xFFFFFFFFu);
        dl->AddCircle(UI::Vec2(cx + r, cy + r), r - 0.5f, Pack(br::Color{118, 118, 118, 255}), 0, 1.0f);
        if (c.text == "\x01") dl->AddCircleFilled(UI::Vec2(cx + r, cy + r), r * 0.55f, Pack(br::Color{0, 117, 255, 255}));
        break;
    }
    case br::ControlKind::Select: {
        UI::PushClipRect(cx, cy, std::max(0.0f, native ? w - h : w), h);
        DrawText(dl, cx + 2, cy + (h - f.size) * 0.5f, f, col, c.text);
        UI::PopClipRect();
        if (!native) break;
        const float ax = cx + w - h * 0.7f, ay = cy + h * 0.42f, as = h * 0.22f;
        dl->AddLine(UI::Vec2(ax - as, ay), UI::Vec2(ax, ay + as), col, 1.5f);
        dl->AddLine(UI::Vec2(ax, ay + as), UI::Vec2(ax + as, ay), col, 1.5f);
        break;
    }
    case br::ControlKind::Submit: case br::ControlKind::Reset: case br::ControlKind::Button: {
        const UI::Font* web = WebFontFor(f);
        float tw;
        { const TextScaleFor s(f.size, web); tw = UI::CalcTextWidth(c.text.c_str()); }
        DrawText(dl, cx + (w - tw) * 0.5f, cy + (h - f.size) * 0.5f, f, col, c.text);
        break;
    }
    case br::ControlKind::Text: case br::ControlKind::Password: case br::ControlKind::TextArea: case br::ControlKind::File: {
        if (editing) break;   // the OkayUI field over it shows the text
        UI::PushClipRect(cx, cy, w, h);
        std::string text = c.text;
        UI::Color32 tc = col;
        if (text.empty()) if (const std::string* ph = el->Attr("placeholder")) { text = *ph; tc = Pack(br::Color{117, 117, 117, 255}); }
        if (k == br::ControlKind::TextArea) {
            float ly = cy;
            std::size_t i = 0;
            while (i <= text.size() && ly < cy + h) {
                const std::size_t e = std::min(text.find('\n', i), text.size());
                DrawText(dl, cx + 1, ly, f, tc, text.substr(i, e - i));
                ly += f.size * 1.2f;
                i = e + 1;
            }
        } else DrawText(dl, cx + 1, cy + (h - f.size) * 0.5f, f, tc, text);
        UI::PopClipRect();
        break;
    }
    default: break;
    }
}

// A soft shadow: rounded rectangles that grow and fade over the blur distance.
void DrawShadow(const UI::DrawList* dl, float x, float y, float w, float h, float radius, float blur, br::Color col) {
    const int steps = std::max(1, std::min(8, (int)(blur / 2.0f)));
    for (int k = steps; k >= 0; --k) {
        const float grow = blur * 0.5f * (float)k / (float)steps;
        br::Color c = col;
        c.a = (std::uint8_t)std::lround((float)col.a / (float)(steps + 1));
        dl->AddRectFilled(UI::Vec2(x - grow, y - grow), UI::Vec2(x + w + grow, y + h + grow), Pack(c), radius + grow);
    }
}

} // namespace

void DrawBrowserPage(const br::LayoutResult& page, float x, float y, float w, float h, float scrollY, const BrowserImageFn& image,
                     const EmbedStateFn& embedState, int hoveredPlay, float zoomIn, const std::vector<br::FindHit>* findHits,
                     int findCurrent, const br::ScrollOffsets* boxScroll, const void* editing) {
    const auto& dl = UI::GetWindowDrawList();
    UI::PushClipRect(x, y, w, h);
    dl->AddRectFilled(UI::Vec2(x, y), UI::Vec2(x + w, y + h), Pack(page.canvas));
    const float z = zoomIn > 0.0f ? zoomIn : 1.0f;
    for (const br::DrawCmd& src : page.cmds) {
        br::DrawCmd c = src;   // in screen pixels: the zoom applied
        // inside scrolling areas: moved by their scroll, clipped to them
        float dx = 0, dy = 0, kx0 = -FLT_MAX, ky0 = -FLT_MAX, kx1 = FLT_MAX, ky1 = FLT_MAX;
        if (c.clip >= 0 && !br::ClipAndScroll(page, c.clip, boxScroll, dx, dy, kx0, ky0, kx1, ky1)) continue;
        c.x += dx; c.y += dy; c.ix += dx; c.iy += dy;
        if (z != 1.0f) { c.x *= z; c.y *= z; c.w *= z; c.h *= z; c.font.size *= z; c.ix *= z; c.iy *= z; c.iw *= z; c.ih *= z; c.radius *= z; c.thick *= z; c.blur *= z; }
        const float top = c.fixed ? y : y - scrollY;
        const float cx = x + c.x, cy = top + c.y;
        if (cy > y + h || cy + std::max(c.h, 1.0f) + c.blur < y) continue;   // outside the view
        bool clipped = false;
        if (c.clip >= 0) {
            const float px0 = x + kx0 * z, py0 = top + ky0 * z, px1 = x + kx1 * z, py1 = top + ky1 * z;
            const float ax = std::max(px0, x), ay = std::max(py0, y), bx = std::min(px1, x + w), by = std::min(py1, y + h);
            if (bx <= ax || by <= ay) continue;
            UI::PushClipRect(ax, ay, bx - ax, by - ay);
            clipped = true;
        }
        switch (c.kind) {
        case br::DrawCmd::Fill:
            dl->AddRectFilled(UI::Vec2(cx, cy), UI::Vec2(cx + c.w, cy + c.h), Pack(c.color), c.radius);
            break;
        case br::DrawCmd::Border:
            dl->AddRect(UI::Vec2(cx + c.thick * 0.5f, cy + c.thick * 0.5f), UI::Vec2(cx + c.w - c.thick * 0.5f, cy + c.h - c.thick * 0.5f), Pack(c.color),
                        std::max(0.0f, c.radius - c.thick * 0.5f), c.thick);
            break;
        case br::DrawCmd::Shadow:
            DrawShadow(&dl, cx, cy, c.w, c.h, c.radius, c.blur, c.color);
            break;
        case br::DrawCmd::Text: {
            const UI::Color32 col = Pack(c.color);
            DrawText(&dl, cx, cy, c.font, col, c.text);
            const float thick = std::max(1.0f, c.font.size / 16.0f);
            if (c.underline) dl->AddRectFilled(UI::Vec2(cx, cy + c.font.size * 0.92f), UI::Vec2(cx + c.w, cy + c.font.size * 0.92f + thick), col);
            if (c.strike) dl->AddRectFilled(UI::Vec2(cx, cy + c.font.size * 0.55f), UI::Vec2(cx + c.w, cy + c.font.size * 0.55f + thick), col);
            break;
        }
        case br::DrawCmd::Image: {
            OkayUI::Tex tex = image ? image(c.text) : nullptr;
            const UI::Color32 tint = 0x00FFFFFFu | ((UI::Color32)c.color.a << 24);
            if (tex) dl->AddImage(tex, UI::Vec2(cx, cy), UI::Vec2(cx + c.w, cy + c.h), UI::Vec2(0, 0), UI::Vec2(1, 1), tint);
            else dl->AddRect(UI::Vec2(cx, cy), UI::Vec2(cx + c.w, cy + c.h), Pack(br::Color{160, 160, 160, 255}), 0.0f, 0, 1.0f);
            break;
        }
        case br::DrawCmd::BgImage: {
            OkayUI::Tex tex = image ? image(c.text) : nullptr;
            if (!tex || c.iw < 1 || c.ih < 1) break;
            UI::PushClipRect(std::max(cx, x), std::max(cy, y), std::max(0.0f, std::min(cx + c.w, x + w) - std::max(cx, x)), std::max(0.0f, std::min(cy + c.h, y + h) - std::max(cy, y)));
            float x0 = x + c.ix, y0 = top + c.iy;
            if (c.repeatX) x0 -= std::ceil((x0 - cx) / c.iw) * c.iw;
            if (c.repeatY) y0 -= std::ceil((y0 - cy) / c.ih) * c.ih;
            int tiles = 0;
            for (float ty = y0; ty < cy + c.h && tiles < 4000; ty += c.ih) {
                for (float tx = x0; tx < cx + c.w && tiles < 4000; tx += c.iw) {
                    if (ty + c.ih >= y && ty <= y + h) dl->AddImage(tex, UI::Vec2(tx, ty), UI::Vec2(tx + c.iw, ty + c.ih));
                    ++tiles;
                    if (!c.repeatX) break;
                }
                if (!c.repeatY) break;
            }
            UI::PopClipRect();
            break;
        }
        case br::DrawCmd::Control:
            DrawControl(&dl, page, src, cx, cy, z,
                        editing && src.control >= 0 && (std::size_t)src.control < page.controls.size() && page.controls[(std::size_t)src.control].node == editing);
            break;
        case br::DrawCmd::Embed: {
            // A box with its kind; when the host says it can run an OkaySpace game (step 2), a Play button and the game's state.
            dl->AddRectFilled(UI::Vec2(cx, cy), UI::Vec2(cx + c.w, cy + c.h), Pack(br::Color{24, 28, 38, 255}));
            dl->AddRect(UI::Vec2(cx, cy), UI::Vec2(cx + c.w, cy + c.h), Pack(br::Color{90, 100, 120, 255}), 0.0f, 0, 1.0f);
            const EmbedState st = embedState ? embedState(src) : EmbedState();
            const std::string label = !st.title.empty() ? st.title : c.tag == "okay-game" || st.playable ? std::string("OkaySpace game") : c.tag;
            const UI::Vec2 ts = UI::CalcTextSize(label.c_str());
            if (!st.playable) {
                dl->AddText(UI::Vec2(cx + (c.w - ts.x) * 0.5f, cy + (c.h - ts.y) * 0.5f), Pack(br::Color{200, 205, 215, 255}), label.c_str());
                if (!st.status.empty()) {
                    const UI::Vec2 ss = UI::CalcTextSize(st.status.c_str());
                    dl->AddText(UI::Vec2(cx + (c.w - ss.x) * 0.5f, cy + (c.h + ts.y) * 0.5f + 4.0f), Pack(br::Color{150, 158, 172, 255}), st.status.c_str());
                }
                break;
            }
            const UI::Rect b = EmbedPlayButton(c);
            const float bx = x + b.x, by = top + b.y;
            if (b.y - ts.y - 6.0f >= c.y)
                dl->AddText(UI::Vec2(cx + (c.w - ts.x) * 0.5f, by - ts.y - 6.0f), Pack(br::Color{200, 205, 215, 255}), label.c_str());
            const bool hot = !st.busy && hoveredPlay == (int)(&src - page.cmds.data());
            const br::Color fill = st.busy ? br::Color{70, 78, 92, 255} : hot ? br::Color{72, 140, 235, 255} : br::Color{58, 123, 213, 255};
            dl->AddRectFilled(UI::Vec2(bx, by), UI::Vec2(bx + b.w, by + b.h), Pack(fill), 4.0f);
            const char* play = st.busy ? "Please wait" : "Play";
            const UI::Vec2 ps = UI::CalcTextSize(play);
            dl->AddText(UI::Vec2(bx + (b.w - ps.x) * 0.5f, by + (b.h - ps.y) * 0.5f), Pack(br::Color{255, 255, 255, 255}), play);
            if (!st.status.empty()) {
                const UI::Vec2 ss = UI::CalcTextSize(st.status.c_str());
                dl->AddText(UI::Vec2(cx + (c.w - ss.x) * 0.5f, by + b.h + 6.0f), Pack(br::Color{170, 178, 192, 255}), st.status.c_str());
            }
            break;
        }
        case br::DrawCmd::Disc:
            dl->AddCircleFilled(UI::Vec2(cx + c.w * 0.5f, cy + c.h * 0.5f), c.w * 0.5f, Pack(c.color));
            break;
        case br::DrawCmd::Circle:
            dl->AddCircle(UI::Vec2(cx + c.w * 0.5f, cy + c.h * 0.5f), c.w * 0.5f - 0.5f, Pack(c.color), 0, 1.0f);
            break;
        case br::DrawCmd::Square:
            dl->AddRectFilled(UI::Vec2(cx, cy), UI::Vec2(cx + c.w, cy + c.h), Pack(c.color));
            break;
        }
        if (clipped) UI::PopClipRect();
    }
    // the scroll bars of scrolling areas that have more to show
    for (std::size_t i = 0; i < page.scrollers.size(); ++i) {
        const br::Scroller& s = page.scrollers[i];
        if (s.contentH <= s.h + 0.5f) continue;
        float dx = 0, dy = 0, kx0, ky0, kx1, ky1;
        const int parent = s.clip >= 0 && (std::size_t)s.clip < page.clips.size() ? page.clips[(std::size_t)s.clip].parent : -1;
        if (parent >= 0 && !br::ClipAndScroll(page, parent, boxScroll, dx, dy, kx0, ky0, kx1, ky1)) continue;
        const float off = boxScroll && i < boxScroll->size() ? (*boxScroll)[i].second : 0.0f;
        const float top = s.fixed ? y : y - scrollY;
        const float bx = x + (s.x + dx + s.w) * z - 5.0f, by = top + (s.y + dy) * z, bh = s.h * z;
        const float th = std::max(16.0f, bh * s.h / s.contentH), ty = by + (bh - th) * std::min(1.0f, off / std::max(1.0f, s.contentH - s.h));
        dl->AddRectFilled(UI::Vec2(bx, ty), UI::Vec2(bx + 4.0f, ty + th), Pack(br::Color{110, 110, 110, 160}), 2.0f);
    }
    if (findHits) {   // find on the page: every hit in yellow, the current one in orange (drawn over the text, half see-through)
        for (std::size_t i = 0; i < findHits->size(); ++i) {
            const br::FindHit& f = (*findHits)[i];
            const br::DrawCmd* fc = f.cmd >= 0 && (std::size_t)f.cmd < page.cmds.size() ? &page.cmds[(std::size_t)f.cmd] : nullptr;
            float dx = 0, dy = 0, kx0, ky0, kx1, ky1;
            if (fc && fc->clip >= 0 && !br::ClipAndScroll(page, fc->clip, boxScroll, dx, dy, kx0, ky0, kx1, ky1)) continue;
            const float top = fc && fc->fixed ? y : y - scrollY;
            const float fx = x + (f.x + dx) * z, fy = top + (f.y + dy) * z;
            if (fy > y + h || fy + f.h * z < y) continue;
            const bool cur = (int)i == findCurrent;
            dl->AddRectFilled(UI::Vec2(fx - 1, fy - 1), UI::Vec2(fx + f.w * z + 1, fy + f.h * z + 2),
                              Pack(cur ? br::Color{255, 150, 30, 140} : br::Color{255, 235, 60, 120}), 2.0f);
        }
    }
    UI::PopClipRect();
}


namespace lk = browserlook;

void DrawBrowserIcon(BrowserIcon icon, float cx, float cy, float size, UI::Color32 col, bool onTop) {
    const auto& dl = onTop ? UI::GetForegroundDrawList() : UI::GetWindowDrawList();
    const float s = size / 16.0f, t = std::max(1.5f, 1.6f * s);   // drawn on a 16 px grid, scaled
    auto P = [&](float x, float y) { return UI::Vec2(cx + x * s, cy + y * s); };
    switch (icon) {
    case BrowserIcon::Back: case BrowserIcon::Forward: {
        const float d = icon == BrowserIcon::Back ? 1.0f : -1.0f;
        dl->AddLine(P(6 * d, 0), P(-6 * d, 0), col, t);
        dl->AddLine(P(-6 * d, 0), P(-1 * d, -5), col, t);
        dl->AddLine(P(-6 * d, 0), P(-1 * d, 5), col, t);
        break;
    }
    case BrowserIcon::Reload: {
        dl->PathClear();
        dl->PathArcTo(P(0, 0), 6.0f * s, -1.2f, 4.3f, 18);
        dl->PathStroke(col, false, t);
        const UI::Vec2 tip = P(6.0f * std::cos(-1.2f), 6.0f * std::sin(-1.2f));   // the arrow head at the open end
        dl->AddTriangleFilled(UI::Vec2(tip.x - 3.5f * s, tip.y - 3.5f * s), UI::Vec2(tip.x + 3.5f * s, tip.y - 2.0f * s), UI::Vec2(tip.x - 0.5f * s, tip.y + 3.0f * s), col);
        break;
    }
    case BrowserIcon::Stop: case BrowserIcon::Close:
        dl->AddLine(P(-5, -5), P(5, 5), col, t);
        dl->AddLine(P(5, -5), P(-5, 5), col, t);
        break;
    case BrowserIcon::Plus:
        dl->AddLine(P(-6, 0), P(6, 0), col, t);
        dl->AddLine(P(0, -6), P(0, 6), col, t);
        break;
    case BrowserIcon::Home: {
        const UI::Vec2 roof[3] = {P(-7, -0.5f), P(0, -7), P(7, -0.5f)};
        dl->AddPolyline(roof, 3, col, false, t);
        const UI::Vec2 body[4] = {P(-5, -2), P(-5, 6), P(5, 6), P(5, -2)};
        dl->AddPolyline(body, 4, col, false, t);
        break;
    }
    case BrowserIcon::Menu:
        for (int i = -1; i <= 1; ++i) dl->AddCircleFilled(P(0, 5.0f * (float)i), 1.7f * s, col);
        break;
    case BrowserIcon::Star: case BrowserIcon::StarFilled: {
        UI::Vec2 pts[10];
        for (int i = 0; i < 10; ++i) {
            const float a = -1.5707963f + (float)i * 0.6283185f, r = (i % 2 == 0 ? 7.0f : 3.0f);
            pts[i] = P(r * std::cos(a), r * std::sin(a) + 0.6f);
        }
        if (icon == BrowserIcon::StarFilled) dl->AddConcavePolyFilled(pts, 10, col);
        else dl->AddPolyline(pts, 10, col, true, std::max(1.2f, 1.3f * s));
        break;
    }
    case BrowserIcon::Lock: {
        dl->AddRectFilled(P(-4.5f, -1), P(4.5f, 6), col, 1.5f * s);
        dl->PathClear();
        dl->PathArcTo(P(0, -1.5f), 3.0f * s, 3.1415926f, 6.2831853f, 10);
        dl->PathStroke(col, false, std::max(1.3f, 1.5f * s));
        dl->AddLine(P(-3, -1.5f), P(-3, -0.5f), col, std::max(1.3f, 1.5f * s));
        dl->AddLine(P(3, -1.5f), P(3, -0.5f), col, std::max(1.3f, 1.5f * s));
        break;
    }
    case BrowserIcon::Info: {
        dl->AddCircle(P(0, 0), 7.0f * s, col, 0, std::max(1.2f, 1.3f * s));
        dl->AddRectFilled(P(-0.8f, -1), P(0.8f, 4), col);
        dl->AddCircleFilled(P(0, -3.5f), 1.1f * s, col);
        break;
    }
    case BrowserIcon::Warning: {
        const UI::Vec2 a = P(0, -7), b = P(7, 6), c = P(-7, 6);
        dl->AddTriangle(a, b, c, col, std::max(1.2f, 1.3f * s));
        dl->AddRectFilled(P(-0.8f, -2.5f), P(0.8f, 2), col);
        dl->AddCircleFilled(P(0, 3.8f), 1.0f * s, col);
        break;
    }
    case BrowserIcon::Search: {
        dl->AddCircle(P(-1.5f, -1.5f), 5.0f * s, col, 0, std::max(1.3f, 1.5f * s));
        dl->AddLine(P(2.2f, 2.2f), P(6.5f, 6.5f), col, t);
        break;
    }
    case BrowserIcon::Clock: {
        dl->AddCircle(P(0, 0), 7.0f * s, col, 0, std::max(1.2f, 1.3f * s));
        dl->AddLine(P(0, 0), P(0, -4.5f), col, std::max(1.2f, 1.3f * s));
        dl->AddLine(P(0, 0), P(3.5f, 2.0f), col, std::max(1.2f, 1.3f * s));
        break;
    }
    case BrowserIcon::Tab: {
        dl->AddRect(P(-7, -5.5f), P(7, 6), col, 2.0f * s, std::max(1.2f, 1.3f * s));
        dl->AddRectFilled(P(-7, -5.5f), P(0, -2.5f), col, 1.0f * s);
        break;
    }
    case BrowserIcon::Globe: {
        dl->AddCircle(P(0, 0), 7.0f * s, col, 0, std::max(1.1f, 1.2f * s));
        dl->AddEllipse(P(0, 0), UI::Vec2(3.0f * s, 7.0f * s), col, 0.0f, 0, std::max(1.0f, 1.1f * s));
        dl->AddLine(P(-7, 0), P(7, 0), col, std::max(1.0f, 1.1f * s));
        break;
    }
    }
}

void BrowserDelayedTooltip(unsigned id, const std::string& text) {
    static unsigned hot = 0;
    static int lastFrame = -10;
    static double since = 0.0;
    const int f = UI::GetFrameCount();
    if (hot != id || f - lastFrame > 1) { hot = id; since = UI::GetTime(); }
    lastFrame = f;
    if (UI::GetTime() - since >= 0.5 && !UI::IsMouseDown(0)) UI::SetTooltip("%s", text.c_str());
}

bool BrowserIconButton(const char* id, BrowserIcon icon, float x, float y, float size, bool enabled, const char* tip, bool* rightClicked,
                       bool* held) {
    UI::SetCursorScreenPos(UI::Vec2(x, y));
    const bool clicked = UI::InvisibleButton(id, size, size);
    const bool hov = UI::IsItemHovered();
    const unsigned itemId = UI::GetItemID();
    const auto& dl = UI::GetWindowDrawList();
    const UI::Vec2 c(x + size * 0.5f, y + size * 0.5f);
    if (enabled && hov) dl->AddCircleFilled(c, size * 0.5f, UI::IsMouseDown(0) ? lk::kPressed : lk::kHover);
    DrawBrowserIcon(icon, c.x, c.y, size * 0.55f, enabled ? lk::kText : lk::kIconOff);
    if (rightClicked && enabled && hov && UI::IsMouseClicked(1)) *rightClicked = true;
    if (hov && tip && *tip) BrowserDelayedTooltip(itemId, tip);
    // press and hold: one button at a time is followed from its press until the button is let go
    static unsigned holdId = 0;
    static double holdSince = 0.0;
    static bool holdFired = false;
    bool swallow = false;
    if (held && enabled) {
        if (hov && UI::IsMouseClicked(0)) { holdId = itemId; holdSince = UI::GetTime(); holdFired = false; }
        if (holdId == itemId) {
            if (UI::IsMouseDown(0)) {
                if (!holdFired && hov && UI::GetTime() - holdSince >= kHoldSeconds) { holdFired = true; *held = true; }
            } else {
                swallow = holdFired;   // the press opened the list: letting go is not a click
                holdId = 0;
                holdFired = false;
            }
        }
    }
    return clicked && enabled && !swallow;
}

bool PageKeyInfo(UI::KeyId key, br::KeyInfo& out) {
    out = br::KeyInfo();
    const UI::Input& in = UI::GetFrameInput();
    out.shift = in.shift || UI::IsKeyDown(UI::KeyId_Shift);
    out.ctrl = in.ctrl || UI::IsKeyDown(UI::KeyId_Ctrl);
    out.alt = UI::IsKeyDown(UI::KeyId_Alt);
    // UI Events KeyboardEvent key / code values and the legacy keyCode (the US layout's: the OkayUI keys are physical keys)
    if (key >= UI::KeyId_A && key <= UI::KeyId_Z) {
        const char ch = (char)('a' + (key - UI::KeyId_A));
        out.key = std::string(1, out.shift ? (char)(ch - 32) : ch);
        out.code = std::string("Key") + (char)(ch - 32);
        out.keyCode = ch - 32;
        return true;
    }
    if (key >= UI::KeyId_0 && key <= UI::KeyId_9) {
        const char ch = (char)('0' + (key - UI::KeyId_0));
        out.key = std::string(1, ch);
        out.code = std::string("Digit") + ch;
        out.keyCode = ch;
        return true;
    }
    if (key >= UI::KeyId_F1 && key <= UI::KeyId_F12) {
        out.key = out.code = "F" + std::to_string(1 + (key - UI::KeyId_F1));
        out.keyCode = 112 + (key - UI::KeyId_F1);
        return true;
    }
    struct Named { UI::KeyId id; const char* key; const char* code; int keyCode; };
    static const Named kNamed[] = {
        {UI::KeyId_Enter, "Enter", "Enter", 13}, {UI::KeyId_Escape, "Escape", "Escape", 27}, {UI::KeyId_Tab, "Tab", "Tab", 9},
        {UI::KeyId_Backspace, "Backspace", "Backspace", 8}, {UI::KeyId_Delete, "Delete", "Delete", 46}, {UI::KeyId_Space, " ", "Space", 32},
        {UI::KeyId_Left, "ArrowLeft", "ArrowLeft", 37}, {UI::KeyId_Up, "ArrowUp", "ArrowUp", 38}, {UI::KeyId_Right, "ArrowRight", "ArrowRight", 39},
        {UI::KeyId_Down, "ArrowDown", "ArrowDown", 40}, {UI::KeyId_PageUp, "PageUp", "PageUp", 33}, {UI::KeyId_PageDown, "PageDown", "PageDown", 34},
        {UI::KeyId_Home, "Home", "Home", 36}, {UI::KeyId_End, "End", "End", 35}, {UI::KeyId_Insert, "Insert", "Insert", 45},
        {UI::KeyId_Minus, "-", "Minus", 189}, {UI::KeyId_Equal, "=", "Equal", 187}, {UI::KeyId_Comma, ",", "Comma", 188},
        {UI::KeyId_Period, ".", "Period", 190}, {UI::KeyId_Slash, "/", "Slash", 191}, {UI::KeyId_Semicolon, ";", "Semicolon", 186},
        {UI::KeyId_Apostrophe, "'", "Quote", 222}, {UI::KeyId_LeftBracket, "[", "BracketLeft", 219}, {UI::KeyId_RightBracket, "]", "BracketRight", 221},
        {UI::KeyId_Backslash, "\\", "Backslash", 220}, {UI::KeyId_GraveAccent, "`", "Backquote", 192},
    };
    for (const Named& n : kNamed)
        if (n.id == key) { out.key = n.key; out.code = n.code; out.keyCode = n.keyCode; return true; }
    return false;
}

namespace {

// The element under the page point (x, y), for :hover: the link or label there, else the innermost box.
const br::Node* NodeAt(const br::LayoutResult& page, float px, float py, int link) {
    (void)link;
    const br::Node* best = nullptr;
    int bestDepth = -1;
    for (const br::BoxInfo& b : page.boxes) {
        if (!b.node || b.noHit || px < b.x || py < b.y || px >= b.x + b.w || py >= b.y + b.h) continue;   // pointer-events: none: under it
        if (b.depth >= bestDepth) { best = b.node; bestDepth = b.depth; }
    }
    return best;
}

// The page's cursor property (CSS UI 4, 5.1) as OkayUI's shape; Auto is `automatic` (what the host shows by itself there).
UI::MouseCursor CursorShape(br::Cursor c, UI::MouseCursor automatic) {
    switch (c) {
    case br::Cursor::Default: return UI::MouseCursor_Arrow;
    case br::Cursor::None: return UI::MouseCursor_None;
    case br::Cursor::Pointer: return UI::MouseCursor_Hand;
    case br::Cursor::Text: return UI::MouseCursor_TextInput;
    case br::Cursor::Move: return UI::MouseCursor_ResizeAll;
    case br::Cursor::NotAllowed: return UI::MouseCursor_NotAllowed;
    case br::Cursor::Wait: return UI::MouseCursor_Wait;
    case br::Cursor::Progress: return UI::MouseCursor_Progress;
    case br::Cursor::ResizeNS: return UI::MouseCursor_ResizeNS;
    case br::Cursor::ResizeEW: return UI::MouseCursor_ResizeEW;
    case br::Cursor::ResizeNESW: return UI::MouseCursor_ResizeNESW;
    case br::Cursor::ResizeNWSE: return UI::MouseCursor_ResizeNWSE;
    default: return automatic;
    }
}
// The cursor the page asks for at page point (px, py): its form control's, else its link's (a hand by default), else the
// innermost box's. `found` false: the page says nothing (auto everywhere): the view leaves the cursor as it is.
UI::MouseCursor PageCursorAt(const br::LayoutResult& page, float px, float py, int ci, int link, bool& found) {
    found = true;
    if (ci >= 0 && (std::size_t)ci < page.controls.size()) {
        const br::ControlBox& c = page.controls[(std::size_t)ci];
        const bool typable = c.node && br::FormState::IsTypable(*c.node);
        if (c.cursor != br::Cursor::Auto || typable) return CursorShape(c.cursor, typable ? UI::MouseCursor_TextInput : UI::MouseCursor_Arrow);
    }
    if (link >= 0) {
        for (const br::LinkArea& a : page.areas)
            if (a.link == link && px >= a.x && px < a.x + a.w && py >= a.y && py < a.y + a.h) return CursorShape(a.cursor, UI::MouseCursor_Hand);
        return UI::MouseCursor_Hand;
    }
    const br::BoxInfo* best = nullptr;
    for (const br::BoxInfo& b : page.boxes) {
        if (!b.node || b.noHit || px < b.x || py < b.y || px >= b.x + b.w || py >= b.y + b.h) continue;
        if (!best || b.depth >= best->depth) best = &b;
    }
    if (best && best->cursor != br::Cursor::Auto) return CursorShape(best->cursor, UI::MouseCursor_Arrow);
    found = false;
    return UI::MouseCursor_Arrow;
}

} // namespace

namespace {

// The browser program's toolbar (round 2): round icon buttons, the rounded address bar with the site's badge, the address with
// its host standing out (the whole address while the person types), the star inside the bar's right end, the menu button.
void ProgramToolbar(br::BrowserTab& tab, float x, float y, float w, float tbH, const BrowserViewOptions& o, BrowserViewResult& res) {
    const auto& dl = UI::GetWindowDrawList();
    const float bs = 30.0f, by = y + (tbH - bs) * 0.5f;
    float bx = x + 8.0f;
    const bool loadingNow = tab.GetState() == br::BrowserTab::State::Loading;
    // Back and Forward: a right click, or a press held on them, shows the tab's history list under the button
    bool listBack = false, listForward = false;
    if (BrowserIconButton("##back", BrowserIcon::Back, bx, by, bs, tab.CanGoBack(), "Back (Alt+Left); right-click or hold for history", &listBack, &listBack))
        res.back = true;
    if (listBack) { res.historyMenu = -1; res.historyAt = UI::Vec2(bx, by + bs + 4.0f); }
    bx += bs + 2.0f;
    if (BrowserIconButton("##forward", BrowserIcon::Forward, bx, by, bs, tab.CanGoForward(), "Forward (Alt+Right); right-click or hold for history",
                          &listForward, &listForward))
        res.forward = true;
    if (listForward) { res.historyMenu = 1; res.historyAt = UI::Vec2(bx, by + bs + 4.0f); }
    bx += bs + 2.0f;
    if (loadingNow) { if (BrowserIconButton("##stop", BrowserIcon::Stop, bx, by, bs, true, "Stop loading (Esc)")) res.stop = true; }
    else if (BrowserIconButton("##reload", BrowserIcon::Reload, bx, by, bs, !tab.Url().empty(), "Reload (Ctrl+R)")) res.reload = true;
    bx += bs + 2.0f;
    if (BrowserIconButton("##home", BrowserIcon::Home, bx, by, bs, true, "Home page (Alt+Home)")) res.home = true;
    bx += bs + 8.0f;
    const float menuX = x + w - 8.0f - bs;
    if (BrowserIconButton("##menu", BrowserIcon::Menu, menuX, by, bs, true, "Menu")) res.menu = true;
    res.menuButton = UI::Rect{menuX, by, bs, bs};

    AddressField* af = o.address;
    const std::string& url = tab.Url();
    const bool ownPage = url == "about:newtab";
    // the field is filled from the tab whenever its address changes, unless the person is typing in it
    if (af->shownFor != url && !UI::WantsTextInput()) {
        std::snprintf(af->text, sizeof af->text, "%s", ownPage ? "" : url.c_str());
        af->shownFor = url;
    }
    const float ph = 34.0f, py = y + (tbH - ph) * 0.5f, px0 = bx, px1 = std::max(bx + 120.0f, menuX - 8.0f);
    const bool overBar = !o.blockInput && UI::IsMouseHoveringRect(UI::Vec2(px0, py), UI::Vec2(px1, py + ph));
    dl->AddRectFilled(UI::Vec2(px0, py), UI::Vec2(px1, py + ph), overBar && !af->focused ? lk::kFieldHover : lk::kField, ph * 0.5f);
    if (af->focused) dl->AddRect(UI::Vec2(px0, py), UI::Vec2(px1, py + ph), lk::kAccent, ph * 0.5f, 2.0f);
    // the badge in front: a lock (https), "Not secure" (http), our own page; none while typing
    float fx = px0 + 12.0f;
    const float cy = py + ph * 0.5f;
    const br::SiteBadge badge = af->focused || ownPage ? br::SiteBadge::None : br::BadgeOf(url);
    const UI::Vec2 m = UI::GetMousePos();
    auto badgeTip = [&](float x0, float x1, const char* tip) {
        if (overBar && m.x >= x0 && m.x < x1) BrowserDelayedTooltip(UI::GetID("##badge"), tip);
    };
    if (badge == br::SiteBadge::Secure) {
        DrawBrowserIcon(BrowserIcon::Lock, fx + 7.0f, cy, 15.0f, lk::kTextDim);
        badgeTip(fx - 4.0f, fx + 18.0f, "The connection to this site is secure (https)");
        fx += 22.0f;
    } else if (badge == br::SiteBadge::NotSecure) {
        DrawBrowserIcon(BrowserIcon::Warning, fx + 7.0f, cy, 15.0f, lk::kWarn);
        const float lh = UI::GetTextLineHeight();
        dl->AddText(UI::Vec2(fx + 19.0f, cy - lh * 0.5f), lk::kWarn, "Not secure");
        const float tw = UI::CalcTextWidth("Not secure");
        badgeTip(fx - 4.0f, fx + 24.0f + tw, "The connection to this site is not secure (http): others on the network can see and change what you send");
        dl->AddRectFilled(UI::Vec2(fx + 27.0f + tw, cy - 9.0f), UI::Vec2(fx + 28.0f + tw, cy + 9.0f), lk::kDivider);
        fx += 32.0f + tw;
    } else if (badge == br::SiteBadge::Internal) {
        DrawBrowserIcon(BrowserIcon::Info, fx + 7.0f, cy, 15.0f, lk::kTextDim);
        badgeTip(fx - 4.0f, fx + 18.0f, "A page of the browser itself");
        fx += 22.0f;
    } else {
        DrawBrowserIcon(BrowserIcon::Globe, fx + 7.0f, cy, 15.0f, lk::kTextDim);
        fx += 22.0f;
    }
    // the star at the bar's right end
    const float ss = 28.0f, starX = px1 - 4.0f - ss;
    if (o.programButtons && BrowserIconButton("##star", o.bookmarked ? BrowserIcon::StarFilled : BrowserIcon::Star, starX, cy - ss * 0.5f, ss,
                                              !url.empty() && !ownPage, o.bookmarked ? "Remove this bookmark (Ctrl+D)" : "Bookmark this page (Ctrl+D)"))
        res.bookmark = true;
    if (o.bookmarked) DrawBrowserIcon(BrowserIcon::StarFilled, starX + ss * 0.5f, cy, ss * 0.55f, lk::kAccent);
    // the field itself: no frame of its own (the bar is its frame); its text is hidden while the host-first address is shown
    const float fh = UI::GetFrameHeight(), fieldX = fx - 6.0f;
    UI::SetCursorScreenPos(UI::Vec2(fieldX, cy - fh * 0.5f));
    UI::SetNextItemWidth(std::max(40.0f, starX - 4.0f - fieldX));
    if (o.focusAddress) UI::SetKeyboardFocusHere();
    const bool showsUrl = !url.empty() && !ownPage && std::string(af->text) == url;
    const bool shown = showsUrl && !af->focused && !o.focusAddress;
    UI::PushStyleVar(UI::StyleVar_FramedWidgets, 1.0f);
    UI::PushStyleColor(UI::Col_FrameBg, 0u);
    UI::PushStyleColor(UI::Col_FrameBgHovered, 0u);
    UI::PushStyleColor(UI::Col_FrameBgActive, 0u);
    if (shown) UI::PushStyleColor(UI::Col_Text, 0u);
    bool go = o.placeholder.empty()
                  ? UI::InputText("##address", af->text, (int)sizeof af->text, UI::InputText_EnterReturnsTrue | UI::InputText_AutoSelectAll)
                  : UI::InputTextWithHint("##address", o.placeholder.c_str(), af->text, (int)sizeof af->text,
                                          UI::InputText_EnterReturnsTrue | UI::InputText_AutoSelectAll);
    const UI::Rect fr = UI::GetItemRect();
    af->focused = UI::IsItemFocused();
    if (shown) UI::PopStyleColor(1);
    UI::PopStyleColor(3);
    UI::PopStyleVar(1);
    if (go && af->text[0]) { res.navigate = af->text; af->shownFor.clear(); }
    if (shown && !af->focused) {
        const br::AddressShown a = br::ShowAddress(url);
        const float lh = UI::GetTextLineHeight();
        float tx = fr.x + 9.0f;
        const float ty = fr.y + (fr.h - lh) * 0.5f;
        UI::PushClipRect(fr.x, fr.y, std::max(0.0f, fr.w - 4.0f), fr.h);
        if (!a.lead.empty()) { dl->AddText(UI::Vec2(tx, ty), lk::kTextDim, a.lead.c_str()); tx += UI::CalcTextWidth(a.lead.c_str()); }
        dl->AddText(UI::Vec2(tx, ty), lk::kText, a.host.c_str());
        tx += UI::CalcTextWidth(a.host.c_str());
        if (!a.rest.empty()) dl->AddText(UI::Vec2(tx, ty), lk::kTextDim, a.rest.c_str());
        UI::PopClipRect();
    }
    res.address = UI::Rect{px0, py, px1 - px0, ph};
    res.addressFocused = af->focused;
}

// One link or form control the Tab key can reach, where it starts on the page.
struct Focusable { float x = 0, y = 0; int link = -1; const br::Node* control = nullptr; };

// Tab order: the links and the form controls in reading order (top to bottom, then left to right).
std::vector<Focusable> FocusOrder(const br::LayoutResult& page) {
    std::vector<Focusable> out;
    std::vector<int> seen(page.links.size(), 0);
    for (const br::LinkArea& a : page.areas) {
        if (a.link < 0 || (std::size_t)a.link >= page.links.size() || seen[(std::size_t)a.link]) continue;
        const br::Node* n = (std::size_t)a.link < page.linkNodes.size() ? page.linkNodes[(std::size_t)a.link] : nullptr;
        if (page.links[(std::size_t)a.link].empty() || (n && n->name == "label")) continue;
        seen[(std::size_t)a.link] = 1;
        Focusable f; f.x = a.x; f.y = a.y; f.link = a.link;
        out.push_back(f);
    }
    for (const br::ControlBox& c : page.controls) {
        if (!c.node || br::FormState::IsDisabled(*c.node)) continue;
        Focusable f; f.x = c.x; f.y = c.y; f.control = c.node;
        out.push_back(f);
    }
    std::stable_sort(out.begin(), out.end(), [](const Focusable& a, const Focusable& b) {
        if (std::fabs(a.y - b.y) > 4.0f) return a.y < b.y;
        return a.x < b.x;
    });
    return out;
}

// The rectangle (page coordinates) the focus ring goes around: every piece of the link, or the control's box.
bool FocusRect(const br::LayoutResult& page, const PageKeys& k, float& x0, float& y0, float& x1, float& y1, bool& fixed) {
    x0 = y0 = FLT_MAX; x1 = y1 = -FLT_MAX; fixed = false;
    if (k.focusLink >= 0) {
        for (const br::LinkArea& a : page.areas) {
            if (a.link != k.focusLink) continue;
            x0 = std::min(x0, a.x); y0 = std::min(y0, a.y); x1 = std::max(x1, a.x + a.w); y1 = std::max(y1, a.y + a.h);
            fixed = a.fixed;
        }
    } else if (k.focusControl) {
        for (const br::ControlBox& c : page.controls)
            if (c.node == k.focusControl) { x0 = c.x; y0 = c.y; x1 = c.x + c.w; y1 = c.y + c.h; fixed = c.fixed; }
    }
    return x1 > x0 && y1 > y0;
}

// The nearest place in the page's text to the page point (px, py): a text run (index into cmds) and a byte offset in it. The
// run on the line under the point (its nearest run by x); above every line: the start of the first run; below: the end of the
// last. Runs inside scrolling areas are left out (their place moves with the area). False when the page has no text.
bool TextPlaceAt(const br::LayoutResult& page, float px, float py, int& cmd, int& at) {
    const OkayUIMeasure measure;
    int best = -1, firstBelow = -1, lastAbove = -1;
    float bestDx = FLT_MAX;
    for (std::size_t i = 0; i < page.cmds.size(); ++i) {
        const br::DrawCmd& c = page.cmds[i];
        if (c.kind != br::DrawCmd::Text || c.clip >= 0 || c.fixed || c.text.empty() || c.noSelect) continue;   // user-select: none
        const float lineH = std::max(c.h * 1.25f, 1.0f);
        if (py >= c.y - c.h * 0.15f && py < c.y + lineH) {
            const float dx = px < c.x ? c.x - px : px > c.x + c.w ? px - (c.x + c.w) : 0.0f;
            if (dx < bestDx) { bestDx = dx; best = (int)i; }
        } else if (c.y >= py) { if (firstBelow < 0) firstBelow = (int)i; }
        else lastAbove = (int)i;
    }
    if (best < 0) {
        if (firstBelow >= 0 && (lastAbove < 0 || firstBelow < lastAbove)) { cmd = firstBelow; at = 0; return true; }
        if (lastAbove >= 0) { cmd = lastAbove; at = (int)page.cmds[(std::size_t)lastAbove].text.size(); return true; }
        if (firstBelow >= 0) { cmd = firstBelow; at = 0; return true; }
        return false;
    }
    const br::DrawCmd& c = page.cmds[(std::size_t)best];
    cmd = best;
    at = 0;
    std::size_t i = 0;
    while (i < c.text.size()) {   // the first character whose middle is right of the point
        std::size_t n = i + 1;
        while (n < c.text.size() && ((unsigned char)c.text[n] & 0xC0) == 0x80) ++n;
        const float before = measure.Width(c.text.substr(0, i), c.font), after = measure.Width(c.text.substr(0, n), c.font);
        if (c.x + (before + after) * 0.5f > px) break;
        i = n;
    }
    at = (int)i;
    return true;
}

constexpr float kWheelStep = 100.0f;   // px a notch of the wheel scrolls the page (it glides there)

// This frame's keys for the page's scripts: keydown (and keypress for a key that makes a character) when a key goes down or
// repeats, keyup when it comes up. The browser's own shortcuts are left out. True when the page cancelled one; this frame's
// typed text is then dropped, so no field types it.
bool SendPageKeys(br::BrowserTab& tab) {
    bool cancelled = false;
    for (int k = UI::KeyId_None + 1; k < UI::KeyId_COUNT; ++k) {
        const UI::KeyId id = (UI::KeyId)k;
        const bool down = UI::IsKeyPressed(id, true), up = UI::IsKeyReleased(id);
        if (!down && !up) continue;
        br::KeyInfo ki;
        if (!PageKeyInfo(id, ki) || br::IsBrowserShortcut(ki)) continue;
        if (down) {
            ki.repeat = !UI::IsKeyPressed(id, false);
            if (tab.KeyEvent("keydown", ki)) cancelled = true;
            else if (!ki.ctrl && !ki.alt && (ki.key.size() == 1 || ki.key == "Enter") && tab.KeyEvent("keypress", ki)) cancelled = true;
        }
        if (up) tab.KeyEvent("keyup", ki);
    }
    if (cancelled) UI::GetIO().InputQueueCharacters.resize(0);
    return cancelled;
}

// This frame's mouse for the page's scripts, at the element under the pointer (its form control, its link, else its innermost
// box): mousedown / mouseup for each button that went down / up, mousemove when the pointer moved. True when a mousedown was
// cancelled.
// The element a page's mouse event goes to at the page point (pmx, pmy): its form control, its link, else its innermost box.
const br::Node* EventTargetAt(const br::LayoutResult& page, float pmx, float pmy, const br::ScrollOffsets* boxScroll, float pageScroll) {
    const int ci = br::ControlAt(page, pmx, pmy, boxScroll, pageScroll);
    const int link = br::LinkAt(page, pmx, pmy, boxScroll, pageScroll);
    const br::Node* target = ci >= 0 ? page.controls[(std::size_t)ci].node : nullptr;
    if (!target && link >= 0 && (std::size_t)link < page.linkNodes.size()) target = page.linkNodes[(std::size_t)link];
    if (!target) target = NodeAt(page, pmx, pmy, link);
    return target;
}

bool SendPageMouse(br::BrowserTab& tab, const br::LayoutResult& page, float pmx, float pmy, float clientY,
                   const br::ScrollOffsets* boxScroll, float pageScroll) {
    const br::Node* target = EventTargetAt(page, pmx, pmy, boxScroll, pageScroll);
    if (!target) return false;
    br::MouseInfo mi;
    mi.x = pmx;
    mi.y = clientY;
    const UI::Input& in = UI::GetFrameInput();
    mi.shift = in.shift;
    mi.ctrl = in.ctrl;
    mi.alt = UI::IsKeyDown(UI::KeyId_Alt);
    // OkayUI's buttons are 0 left, 1 right, 2 middle; the page's are 0 left, 1 middle, 2 right (buttons: 1, 4, 2 as bits)
    static const int kDomButton[3] = {0, 2, 1};
    static const int kDomBit[3] = {1, 2, 4};
    for (int b = 0; b < 3; ++b) if (UI::IsMouseDown(b)) mi.buttons |= kDomBit[b];
    bool cancelled = false;
    for (int b = 0; b < 3; ++b) {
        mi.button = kDomButton[b];
        if (UI::IsMouseClicked(b) && tab.PointerEvent(*target, "mousedown", mi)) cancelled = true;
        if (UI::IsMouseReleased(b)) tab.PointerEvent(*target, "mouseup", mi);
    }
    const UI::Vec2 d = UI::GetMouseDelta();
    if (d.x != 0.0f || d.y != 0.0f) { mi.button = 0; tab.PointerEvent(*target, "mousemove", mi); }
    return cancelled;
}

// A page that could not be opened: a large sign, what happened in a sentence, what to do, the small print, Try again.
void ErrorPage(br::BrowserTab& tab, float left, float top, float viewW, float viewH, BrowserViewResult& res) {
    const br::LoadErrorInfo e = br::DescribeLoadError(tab.Url(), tab.Error());
    const float colW = std::min(560.0f, std::max(200.0f, viewW - 48.0f));
    const float cx = left + std::max(24.0f, (viewW - colW) * 0.5f);
    float cy = top + std::max(24.0f, viewH * 0.16f);
    const auto& dl = UI::GetWindowDrawList();
    const BrowserIcon sign = e.kind == br::LoadErrorInfo::Blocked ? BrowserIcon::Lock
                           : e.kind == br::LoadErrorInfo::NotFound ? BrowserIcon::Info
                           : e.kind == br::LoadErrorInfo::Stopped ? BrowserIcon::Stop : BrowserIcon::Warning;
    dl->AddCircleFilled(UI::Vec2(cx + 28.0f, cy + 28.0f), 28.0f, lk::kToolbar);
    DrawBrowserIcon(sign, cx + 28.0f, cy + 28.0f, 30.0f, e.kind == br::LoadErrorInfo::Stopped ? lk::kTextDim : lk::Rgb(240, 180, 90));
    cy += 76.0f;
    dl->AddText(UI::Vec2(cx, cy), lk::kText, e.title.c_str(), 3.0f);
    cy += 40.0f;
    UI::SetCursorScreenPos(UI::Vec2(cx, cy));
    UI::PushTextWrapPos(cx + colW);
    UI::TextWrapped(e.advice.c_str());
    UI::PopTextWrapPos();
    const std::string host = br::HostOf(tab.Url());
    const std::string small = (host.empty() ? tab.Url() : host) + (e.code.empty() ? std::string() : "  -  " + e.code);
    UI::SetCursorScreenPos(UI::Vec2(cx, UI::GetCursorScreenPos().y + 6.0f));
    UI::TextDisabled(small.c_str());
    UI::SetCursorScreenPos(UI::Vec2(cx, UI::GetCursorScreenPos().y + 14.0f));
    if (!tab.Url().empty() && UI::PrimaryButton("Try again")) res.reload = true;
    if (tab.CanGoBack()) {
        UI::SameLine();
        if (UI::Button("Go back")) res.back = true;
    }
}

} // namespace

std::string SelectedPageText(const br::LayoutResult& page, const PageKeys& k) {
    if (!k.HasSelection()) return std::string();
    int a = k.selFromCmd, ac = k.selFromChar, b = k.selToCmd, bc = k.selToChar;
    if (b < a || (a == b && bc < ac)) { std::swap(a, b); std::swap(ac, bc); }
    std::string out;
    float lastY = -FLT_MAX;
    for (int i = std::max(0, a); i <= b && i < (int)page.cmds.size(); ++i) {
        const br::DrawCmd& c = page.cmds[(std::size_t)i];
        if (c.kind != br::DrawCmd::Text || c.clip >= 0 || c.fixed || c.noSelect) continue;
        const std::size_t from = i == a ? std::min((std::size_t)ac, c.text.size()) : 0, to = i == b ? std::min((std::size_t)bc, c.text.size()) : c.text.size();
        if (to <= from) continue;
        if (lastY != -FLT_MAX && std::fabs(c.y - lastY) > c.h * 0.5f) {
            while (!out.empty() && out.back() == ' ') out.pop_back();
            out += '\n';
        }
        out += c.text.substr(from, to - from);
        lastY = c.y;
    }
    return out;
}

BrowserViewResult DrawBrowserView(br::BrowserTab& tab, float x, float y, float w, float h, const BrowserImageFn& image,
                                  const BrowserViewOptions& options) {
    BrowserViewResult res;
    UI::BareNextWindow();
    if (UI::Begin("Browser##okayui", x, y, w, h)) {
        // ---- the toolbar ----
        const bool program = options.programButtons && options.address;   // the browser program's own toolbar (round 2)
        const float tbH = 46.0f;
        const UI::Vec2 start = UI::GetCursorScreenPos();
        if (program) {
            const auto& dl = UI::GetWindowDrawList();
            const float barH = tbH + (options.belowToolbar ? options.belowToolbarH : 0.0f);
            dl->AddRectFilled(UI::Vec2(x, y), UI::Vec2(x + w, y + barH), lk::kToolbar);
            dl->AddRectFilled(UI::Vec2(x, y + barH), UI::Vec2(x + w, y + barH + 1.0f), lk::Rgb(16, 18, 23));
            ProgramToolbar(tab, x, y, w, tbH, options, res);
            UI::SetCursorScreenPos(UI::Vec2(start.x, y + tbH));
        } else {
            UI::BeginDisabled(!tab.CanGoBack());
            if (UI::Button("Back")) res.back = true;
            UI::EndDisabled();
            UI::SameLine();
            UI::BeginDisabled(!tab.CanGoForward());
            if (UI::Button("Forward")) res.forward = true;
            UI::EndDisabled();
            UI::SameLine();
            if (options.programButtons) {
                if (tab.GetState() == br::BrowserTab::State::Loading) { if (UI::Button("Stop")) res.stop = true; }
                else if (UI::Button("Reload")) res.reload = true;
                UI::SameLine();
                if (UI::Button("Home")) res.home = true;
            } else if (UI::Button(tab.GetState() == br::BrowserTab::State::Loading ? "Loading..." : "Reload")) res.reload = true;
            UI::SameLine();
            if (options.hostButtons) {
                if (UI::Button("Open in browser")) res.openInSystemBrowser = true;
                UI::SameLine();
                if (UI::Button("Close")) res.close = true;
                UI::SameLine();
            }
            if (AddressField* af = options.address) {
                // the address field: filled from the tab whenever its address changes, unless the person is typing in it
                if (af->shownFor != tab.Url() && !UI::WantsTextInput()) {
                    std::snprintf(af->text, sizeof af->text, "%s", tab.Url() == "about:newtab" ? "" : tab.Url().c_str());
                    af->shownFor = tab.Url();
                }
                const char* star = options.bookmarked ? "Bookmarked" : "Bookmark";
                const float extra = options.programButtons ? UI::CalcTextWidth(star) + UI::CalcTextWidth("Menu") + 90.0f : 0.0f;
                UI::SetNextItemWidth(std::max(60.0f, UI::GetContentRegionAvail().x - UI::CalcTextWidth("Go") - 28.0f - extra));
                if (options.focusAddress) UI::SetKeyboardFocusHere();
                bool go = options.placeholder.empty()
                              ? UI::InputText("##address", af->text, (int)sizeof af->text, UI::InputText_EnterReturnsTrue)
                              : UI::InputTextWithHint("##address", options.placeholder.c_str(), af->text, (int)sizeof af->text,
                                                      UI::InputText_EnterReturnsTrue | UI::InputText_AutoSelectAll);
                UI::SameLine();
                if (UI::Button("Go")) go = true;
                if (go && af->text[0]) { res.navigate = af->text; af->shownFor.clear(); }
                if (options.programButtons) {
                    UI::SameLine();
                    if (UI::Button(star)) res.bookmark = true;
                    UI::SameLine();
                    if (UI::Button("Menu")) res.menu = true;
                    res.menuButton = UI::GetItemRect();
                }
            } else {
                // the address last, cut in the middle to the room that is left
                const float room = std::max(40.0f, UI::GetContentRegionAvail().x - 8.0f);
                std::size_t chars = tab.Url().size();
                while (chars > 8 && UI::CalcTextWidth(ShortUrl(tab.Url(), chars).c_str()) > room) chars -= chars > 40 ? 8 : 2;
                UI::TextDisabled(ShortUrl(tab.Url(), chars).c_str());
            }
        }
        if (!program) UI::Spacing(4.0f);
        if (options.belowToolbar && options.belowToolbarH > 0.0f) {
            const UI::Vec2 at = UI::GetCursorScreenPos();
            const float rowW = UI::GetContentRegionAvail().x;
            options.belowToolbar(at.x, at.y, rowW);
            UI::SetCursorScreenPos(at);
            UI::Dummy(rowW, options.belowToolbarH);
        }
        if (program) UI::SetCursorScreenPos(UI::Vec2(start.x, y + tbH + (options.belowToolbar ? options.belowToolbarH : 0.0f) + 1.0f));

        // ---- the page ----
        const UI::Vec2 avail = UI::GetContentRegionAvail();
        if (program) {   // the page fills the area edge to edge (Dear ImGui's child form: no padding, a bar only when it overflows)
            UI::PushStyleVar(UI::StyleVar_FramedWidgets, 1.0f);
            UI::BeginChild("##browserpage", UI::Vec2(0.0f, std::max(20.0f, avail.y)), UI::Child_None);
            UI::PopStyleVar(1);
        } else UI::BeginChild("##browserpage", 0.0f, std::max(20.0f, avail.y), false);
        const UI::Vec2 layoutOrigin = UI::GetCursorScreenPos();   // the page's top-left (already moved up by the scroll)
        UI::Vec2 origin = layoutOrigin;                           // ... where it is drawn (a wheel glide draws it where the glide is)
        const float viewW = std::max(20.0f, UI::GetContentRegionAvail().x);
        float scroll = UI::GetScrollY();
        const float viewTop = origin.y + scroll;
        const float viewH = std::max(20.0f, avail.y);
        res.page.x = origin.x; res.page.y = viewTop; res.page.w = viewW; res.page.h = viewH;
        res.pageTop = origin.y;
        const float zoom = options.zoom > 0.0f ? options.zoom : 1.0f;
        if (tab.GetState() == br::BrowserTab::State::Ready || (options.programButtons && tab.HasPage())) {
            static const OkayUIMeasure measure;
            const bool loading = tab.GetState() == br::BrowserTab::State::Loading;   // the last page, while the next one loads
            if (options.programButtons) tab.SetViewportHeight(viewH / zoom);
            const br::LayoutResult& page = tab.Layout(viewW / zoom, measure);
            br::ScrollOffsets& boxScroll = tab.BoxScroll();
            if (boxScroll.size() != page.scrollers.size()) boxScroll.resize(page.scrollers.size(), {0.0f, 0.0f});
            const float prevScroll = tab.ScrollY();
            if (!loading) {
                const float pending = tab.PendingScroll();
                const bool shown = tab.TakeJustShown();
                if (pending >= 0.0f) { UI::SetScrollY(pending * zoom); tab.ScrollY() = pending * zoom; }
                else if (shown) UI::SetScrollY(tab.ScrollY());   // back where the person was on this page (0 for a new one)
                else if (options.scrollToPageY >= 0.0f) {
                    const float want = options.scrollToPageY * zoom;
                    if (want < scroll + 20.0f || want > scroll + viewH - 40.0f) { UI::SetScrollY(std::max(0.0f, want - viewH * 0.4f)); }
                    tab.ScrollY() = scroll;
                } else tab.ScrollY() = scroll;                     // follow the wheel and the bar
            }
            // ---- the keyboard (round 2): key scrolls that glide, Tab / Shift+Tab between links and fields, Enter on a link ----
            PageKeys* nav = options.keys;
            ControlEdit* kedit = options.edit;
            const br::HtmlDocument* kdoc = tab.Document();
            if (nav && nav->forDoc != kdoc) { nav->focusLink = -1; nav->focusControl = nullptr; nav->forDoc = kdoc; nav->scrollTarget = -1.0f; }
            if (nav && !loading && kdoc && !options.blockInput && !res.addressFocused) {
                const bool ctrlK = UI::GetFrameInput().ctrl, shiftK = UI::GetFrameInput().shift, altK = UI::IsKeyDown(UI::KeyId_Alt);
                const bool typing = UI::WantsTextInput();
                const bool pageField = kedit && kedit->node && typing;   // a field of the page has the keyboard
                // the page's scripts see the keys first (keydown / keypress / keyup at the focused element); a key they cancel is
                // not typed and does not scroll or move the focus. The browser's own keys (Ctrl+T ...) never reach them.
                if (tab.Scripts() && (!typing || pageField)) res.keyCancelled = SendPageKeys(tab);
                const bool keysFree = !res.keyCancelled;
                const float maxScroll = std::max(0.0f, std::min(page.height * zoom - viewH, UI::GetScrollMaxY() > 0.0f ? UI::GetScrollMaxY() : FLT_MAX));
                const br::Node* fc = static_cast<const br::Node*>(nav->focusControl);
                const br::ControlKind fk = fc ? br::KindOf(*fc) : br::ControlKind::Text;
                const bool spaceWorksControl = fc && (fk == br::ControlKind::Checkbox || fk == br::ControlKind::Radio || fk == br::ControlKind::Submit ||
                                                      fk == br::ControlKind::Select || fk == br::ControlKind::Button || fk == br::ControlKind::Reset);
                if (keysFree && !typing && !ctrlK && !altK) {
                    float delta = 0.0f, dest = -1.0f;
                    const float pageStep = std::max(40.0f, viewH * 0.875f);
                    if (UI::IsKeyPressed(UI::KeyId_Down)) delta = 40.0f;
                    else if (UI::IsKeyPressed(UI::KeyId_Up)) delta = -40.0f;
                    else if (UI::IsKeyPressed(UI::KeyId_PageDown)) delta = pageStep;
                    else if (UI::IsKeyPressed(UI::KeyId_PageUp)) delta = -pageStep;
                    else if (UI::IsKeyPressed(UI::KeyId_Space) && !spaceWorksControl) delta = shiftK ? -pageStep : pageStep;
                    else if (UI::IsKeyPressed(UI::KeyId_Home, false)) dest = 0.0f;
                    else if (UI::IsKeyPressed(UI::KeyId_End, false)) dest = maxScroll;
                    if (delta != 0.0f || dest >= 0.0f) {
                        const float base = nav->scrollTarget >= 0.0f ? nav->scrollTarget : scroll;
                        nav->scrollTarget = std::max(0.0f, std::min(maxScroll, dest >= 0.0f ? dest : base + delta));
                        nav->glideFrames = 0;
                    }
                }
                if (keysFree && (!typing || pageField) && !ctrlK && !altK && UI::IsKeyPressed(UI::KeyId_Tab)) {
                    const std::vector<Focusable> order = FocusOrder(page);
                    if (!order.empty()) {
                        int cur = -1;
                        const void* curControl = pageField ? kedit->node : nav->focusControl;
                        for (std::size_t i = 0; i < order.size(); ++i)
                            if ((nav->focusLink >= 0 && order[i].link == nav->focusLink && !pageField) || (curControl && order[i].control == curControl)) cur = (int)i;
                        const int n = (int)order.size();
                        const int next = cur < 0 ? (shiftK ? n - 1 : 0) : ((cur + (shiftK ? -1 : 1)) % n + n) % n;
                        const Focusable& f = order[(std::size_t)next];
                        nav->focusLink = f.link;
                        nav->focusControl = f.control;
                        res.tabFocused = true;
                        // the field that had the keyboard lets go first, so the next one starts from its own value
                        if (typing) UI::SetKeyboardFocusId(0);
                        if (f.control && kedit && br::FormState::IsTypable(*f.control)) {
                            kedit->node = f.control; kedit->buf.assign(4096, 0); kedit->focusNow = true;
                        } else if (kedit) kedit->node = nullptr;
                        tab.SetFocus(f.control ? f.control : (f.link >= 0 && (std::size_t)f.link < page.linkNodes.size() ? page.linkNodes[(std::size_t)f.link] : nullptr));
                        float rx0, ry0, rx1, ry1; bool rfixed;
                        if (FocusRect(page, *nav, rx0, ry0, rx1, ry1, rfixed) && !rfixed) {   // bring it into view
                            const float top = ry0 * zoom, bottom = ry1 * zoom, base = nav->scrollTarget >= 0.0f ? nav->scrollTarget : scroll;
                            if (top < base + 8.0f || bottom > base + viewH - 8.0f)
                                nav->scrollTarget = std::max(0.0f, std::min(maxScroll, top - viewH * 0.3f)), nav->glideFrames = 0;
                        }
                    }
                }
                if (keysFree && !typing && !ctrlK && !altK && nav->focusLink >= 0 && nav->focusLink < (int)page.links.size() && UI::IsKeyPressed(UI::KeyId_Enter, false)) {
                    res.linkClicked = nav->focusLink;
                    res.linkInNewTab = false;
                }
                // Space or Enter on a focused box, button or list works it like a click
                if (keysFree && !typing && fc && kedit && (UI::IsKeyPressed(UI::KeyId_Space, false) || UI::IsKeyPressed(UI::KeyId_Enter, false))) {
                    br::FormState& fs = tab.Forms();
                    if (fk == br::ControlKind::Checkbox || fk == br::ControlKind::Radio) { fs.Toggle(*fc); tab.FormChanged(); tab.ControlInput(*fc); }
                    else if (fk == br::ControlKind::Select) kedit->selectOpen = fc;
                    else if (fk == br::ControlKind::Submit) {
                        if (const br::Node* form = br::FormState::FormOwner(*fc, *kdoc)) res.formSent = tab.SubmitForm(*form, fc, res.formNote);
                    } else if (fk == br::ControlKind::Reset) { fs.ResetForm(br::FormState::FormOwner(*fc, *kdoc)); tab.FormChanged(); }
                }
            }
            const UI::Vec2 m = UI::GetMousePos();
            const bool inView = !options.blockInput &&
                                UI::IsMouseHoveringRect(UI::Vec2(origin.x, viewTop), UI::Vec2(origin.x + viewW, viewTop + viewH));
            const float wheel = UI::GetFrameInput().wheel;
            // the wheel over a scrolling area scrolls it, not the page (which stays where it was)
            bool wheelTaken = false;
            if (inView && wheel != 0.0f) {
                const int sc = br::ScrollerAt(page, (m.x - origin.x) / zoom, (m.y - origin.y) / zoom, &boxScroll, scroll / zoom);
                if (sc >= 0) {
                    const br::Scroller& s = page.scrollers[(std::size_t)sc];
                    float& off = boxScroll[(std::size_t)sc].second;
                    const float before = off;
                    off = std::max(0.0f, std::min(std::max(0.0f, s.contentH - s.h), off - wheel * 48.0f));
                    if (off != before) {
                        UI::SetScrollY(prevScroll);
                        tab.ScrollY() = prevScroll;
                        origin.y += scroll - prevScroll;
                        scroll = prevScroll;
                        wheelTaken = true;
                    }
                }
            }
            // the wheel over the page glides there over a few frames, like the keys (the view's own jump is undone)
            bool wheelGlide = false;
            if (nav && options.smoothScroll && !loading && inView && wheel != 0.0f && !wheelTaken) {
                const float maxS = std::max(0.0f, std::min(page.height * zoom - viewH, UI::GetScrollMaxY() > 0.0f ? UI::GetScrollMaxY() : FLT_MAX));
                const float base = nav->scrollTarget >= 0.0f ? nav->scrollTarget : prevScroll;
                nav->scrollTarget = std::max(0.0f, std::min(maxS, base - wheel * kWheelStep));
                nav->glideFrames = 0;
                wheelGlide = true;
            }
            if (nav && nav->scrollTarget >= 0.0f && !loading) {
                if (wheel != 0.0f && !wheelGlide) nav->scrollTarget = -1.0f;   // the wheel took over (a scrolling area, or no glide)
                else {
                    const float from = wheelGlide ? prevScroll : scroll;
                    float next = options.smoothScroll ? from + (nav->scrollTarget - from) * 0.35f : nav->scrollTarget;
                    if (std::fabs(nav->scrollTarget - next) < 1.0f || ++nav->glideFrames > 40) { next = nav->scrollTarget; nav->scrollTarget = -1.0f; }
                    UI::SetScrollY(next);
                    tab.ScrollY() = next;
                    if (wheelGlide) { origin.y += scroll - next; scroll = next; }   // drawn where the glide is, not where the wheel jumped
                }
            }
            res.pageTop = origin.y;
            const float pmx = (m.x - origin.x) / zoom, pmy = (m.y - origin.y) / zoom;   // the pointer in page coordinates
            const float pageScroll = scroll / zoom;
            // the Play button of an embedded game under the pointer (index into cmds), checked before drawing so it lights up
            int hotPlay = -1;
            if (inView && options.embedState)
                for (int ei : page.embeds) {
                    if (ei < 0 || ei >= (int)page.cmds.size()) continue;
                    const br::DrawCmd& c = page.cmds[(std::size_t)ei];
                    const UI::Rect b = EmbedPlayButton(c);
                    if (pmx < b.x || pmy < b.y || pmx >= b.x + b.w || pmy >= b.y + b.h) continue;
                    const EmbedState st = options.embedState(c);
                    if (st.playable && !st.busy) hotPlay = ei;
                }
            ControlEdit* edit = options.edit;
            const br::Node* editing = edit ? static_cast<const br::Node*>(edit->node) : nullptr;
            DrawBrowserPage(page, origin.x, viewTop, viewW, viewH, scroll, image, options.embedState, hotPlay, zoom, options.findHits,
                            options.findCurrent, &boxScroll, editing);
            if (nav && nav->HasSelection() && !loading) {   // the selected text, behind a see-through accent
                int a = nav->selFromCmd, ac = nav->selFromChar, b = nav->selToCmd, bc = nav->selToChar;
                if (b < a || (a == b && bc < ac)) { std::swap(a, b); std::swap(ac, bc); }
                const auto& dl = UI::GetWindowDrawList();
                UI::PushClipRect(origin.x, viewTop, viewW, viewH);
                for (int i = std::max(0, a); i <= b && i < (int)page.cmds.size(); ++i) {
                    const br::DrawCmd& c = page.cmds[(std::size_t)i];
                    if (c.kind != br::DrawCmd::Text || c.clip >= 0 || c.fixed || c.noSelect) continue;
                    const std::size_t from = i == a ? std::min((std::size_t)ac, c.text.size()) : 0, to = i == b ? std::min((std::size_t)bc, c.text.size()) : c.text.size();
                    if (to <= from) continue;
                    const float x0 = c.x + measure.Width(c.text.substr(0, from), c.font), x1 = c.x + measure.Width(c.text.substr(0, to), c.font);
                    const float sy = origin.y + c.y * zoom;
                    if (sy > viewTop + viewH || sy + c.h * 1.25f * zoom < viewTop) continue;
                    dl->AddRectFilled(UI::Vec2(origin.x + x0 * zoom, sy - c.h * 0.1f * zoom), UI::Vec2(origin.x + x1 * zoom, sy + c.h * 1.15f * zoom),
                                      lk::Rgb(66, 150, 250, 90));
                }
                UI::PopClipRect();
            }
            if (nav && (nav->focusLink >= 0 || nav->focusControl) && !loading) {   // the focus ring around the focused link or field
                float rx0, ry0, rx1, ry1; bool rfixed;
                if (FocusRect(page, *nav, rx0, ry0, rx1, ry1, rfixed)) {
                    const float top = rfixed ? viewTop : origin.y;
                    const auto& dl = UI::GetWindowDrawList();
                    UI::PushClipRect(origin.x, viewTop, viewW, viewH);
                    dl->AddRect(UI::Vec2(origin.x + rx0 * zoom - 3.0f, top + ry0 * zoom - 2.0f), UI::Vec2(origin.x + rx1 * zoom + 3.0f, top + ry1 * zoom + 2.0f),
                                lk::kAccent, 4.0f, 2.0f);
                    UI::PopClipRect();
                }
            }
            if (loading) {   // the loading bar: a block sliding along the top of the page
                static int frame = 0;
                ++frame;
                const float bw = viewW * 0.25f, bx = std::fmod((float)frame * 6.0f, viewW + bw) - bw;
                const auto& dl = UI::GetWindowDrawList();
                dl->AddRectFilled(UI::Vec2(origin.x, viewTop), UI::Vec2(origin.x + viewW, viewTop + 3.0f), 0x40D57B3Au);
                dl->AddRectFilled(UI::Vec2(origin.x + std::max(0.0f, bx), viewTop), UI::Vec2(origin.x + std::min(viewW, bx + bw), viewTop + 3.0f), 0xFFD57B3Au);
            }
            if (hotPlay >= 0) {
                UI::SetMouseCursor(UI::MouseCursor_Hand);
                if (UI::IsMouseClicked(0)) res.playEmbed = hotPlay;
            }
            const br::HtmlDocument* doc = tab.Document();
            // ---- form controls: the field being typed in is an OkayUI text field over the page's own ----
            bool controlTook = false;
            if (edit && editing && doc && !loading) {
                int ci = -1;
                for (std::size_t i = 0; i < page.controls.size(); ++i) if (page.controls[i].node == editing) ci = (int)i;
                if (ci < 0) { edit->node = nullptr; editing = nullptr; }
                else {
                    const br::ControlBox& cb = page.controls[(std::size_t)ci];
                    float dx = 0, dy = 0, kx0, ky0, kx1, ky1;
                    br::ClipAndScroll(page, cb.clip, &boxScroll, dx, dy, kx0, ky0, kx1, ky1);
                    const float sx = origin.x + (cb.x + dx) * zoom, sy = (cb.fixed ? viewTop : origin.y) + (cb.y + dy) * zoom;
                    br::FormState& fs = tab.Forms();
                    const br::ControlKind kind = br::KindOf(*editing);
                    const std::string value = fs.Value(*editing);
                    if (edit->buf.size() < 4096) edit->buf.resize(4096, 0);
                    if (std::string(edit->buf.data()) != value && !UI::WantsTextInput()) std::snprintf(edit->buf.data(), edit->buf.size(), "%s", value.c_str());
                    UI::SetCursorScreenPos(UI::Vec2(sx, sy));
                    UI::PushID("##pagefield");
                    if (edit->focusNow && kind != br::ControlKind::TextArea) { UI::SetKeyboardFocusHere(); edit->focusNow = false; }
                    bool enter = false;
                    if (kind == br::ControlKind::TextArea) {
                        // the multi-line field takes no SetKeyboardFocusHere: it is given the keyboard by its id after it is drawn
                        const bool give = edit->focusNow;
                        edit->focusNow = false;
                        UI::InputTextMultiline("##field", edit->buf.data(), (int)edit->buf.size(), UI::Vec2(cb.w * zoom, cb.h * zoom),
                                               res.keyCancelled ? UI::InputText_ReadOnly : 0u);
                        if (give) UI::SetKeyboardFocusId((int)UI::GetItemID());
                    } else {
                        UI::SetNextItemWidth(cb.w * zoom);
                        enter = UI::InputText("##field", edit->buf.data(), (int)edit->buf.size(),
                                              UI::InputText_EnterReturnsTrue | (kind == br::ControlKind::Password ? UI::InputText_Password : 0) |
                                                  (res.keyCancelled ? UI::InputText_ReadOnly : 0));   // a key the page cancelled is not typed
                        if (res.keyCancelled) enter = false;
                    }
                    UI::PopID();
                    const std::string typed(edit->buf.data());
                    if (typed != value) { fs.SetValue(*editing, typed); tab.FormChanged(); tab.ControlInput(*editing); }
                    if (enter) {   // Enter: implicit submission (HTML 4.10.21.2)
                        const br::Node* form = nullptr; const br::Node* submitter = nullptr;
                        if (fs.ImplicitSubmitter(*editing, *doc, form, submitter) && form) {
                            res.formSent = tab.SubmitForm(*form, submitter, res.formNote);
                            edit->node = nullptr;
                        }
                    }
                    UI::SetCursorScreenPos(origin);
                }
            }
            // an open <select> list
            if (edit && edit->selectOpen && doc && !loading) {
                const br::Node* sel = static_cast<const br::Node*>(edit->selectOpen);
                int ci = -1;
                for (std::size_t i = 0; i < page.controls.size(); ++i) if (page.controls[i].node == sel) ci = (int)i;
                if (ci < 0) edit->selectOpen = nullptr;
                else {
                    const br::ControlBox& cb = page.controls[(std::size_t)ci];
                    float dx = 0, dy = 0, kx0, ky0, kx1, ky1;
                    br::ClipAndScroll(page, cb.clip, &boxScroll, dx, dy, kx0, ky0, kx1, ky1);
                    const float sx = origin.x + (cb.x + dx) * zoom, sy = (cb.fixed ? viewTop : origin.y) + (cb.y + dy + cb.h) * zoom;
                    br::FormState& fs = tab.Forms();
                    const auto opts = fs.Options(*sel);
                    const float rowH = std::max(18.0f, cb.font.size * zoom * 1.4f), lw = std::max(cb.w * zoom, 120.0f);
                    const auto& fg = UI::GetForegroundDrawList();
                    fg->AddRectFilled(UI::Vec2(sx, sy), UI::Vec2(sx + lw, sy + rowH * (float)opts.size() + 4), 0xFFFFFFFFu, 3.0f);
                    fg->AddRect(UI::Vec2(sx, sy), UI::Vec2(sx + lw, sy + rowH * (float)opts.size() + 4), 0xFF888888u, 3.0f, 1.0f);
                    int chosen = -1;
                    for (std::size_t i = 0; i < opts.size(); ++i) {
                        const float ry = sy + 2 + rowH * (float)i;
                        const bool hov = m.x >= sx && m.x < sx + lw && m.y >= ry && m.y < ry + rowH;
                        if (hov || (int)i == fs.SelectedIndex(*sel)) fg->AddRectFilled(UI::Vec2(sx + 2, ry), UI::Vec2(sx + lw - 2, ry + rowH), hov ? 0xFFE6A04Bu : 0xFFF0E0D0u, 2.0f);
                        fg->AddText(UI::Vec2(sx + 6, ry + (rowH - 16) * 0.5f), 0xFF202020u, opts[i].second.c_str(), 2.0f);
                        if (hov && UI::IsMouseClicked(0)) chosen = (int)i;
                    }
                    if (chosen >= 0) { fs.Select(*sel, chosen); tab.FormChanged(); tab.ControlInput(*sel); edit->selectOpen = nullptr; controlTook = true; }
                    else if (UI::IsMouseClicked(0) || UI::IsKeyPressed(UI::KeyId_Escape, false)) { edit->selectOpen = nullptr; controlTook = true; }
                    else if (UI::IsMouseHoveringRect(UI::Vec2(sx, sy), UI::Vec2(sx + lw, sy + rowH * (float)opts.size() + 4))) controlTook = true;
                }
            }
            // the page's scripts see the mouse: mousedown / mouseup for each button and mousemove, at the element under the pointer
            if (inView && hotPlay < 0 && !controlTook && doc && !loading && tab.Scripts())
                res.mouseDownCancelled = SendPageMouse(tab, page, pmx, pmy, (m.y - viewTop) / zoom, &boxScroll, pageScroll);
            if (nav && inView && UI::IsMouseClicked(0)) { nav->focusLink = -1; nav->focusControl = nullptr; }
            // the page's scripts see the click first (on the control, the link, else the innermost box under the pointer); a
            // page that cancels it (preventDefault) keeps the link from being followed and the control from being worked
            if (inView && hotPlay < 0 && !controlTook && doc && !loading && tab.Scripts() && UI::IsMouseClicked(0)) {
                const br::Node* target = EventTargetAt(page, pmx, pmy, &boxScroll, pageScroll);
                if (target && tab.Click(*target)) controlTook = true;
            }
            // clicks on form controls and labels
            if (edit && inView && hotPlay < 0 && !controlTook && doc && !loading && UI::IsMouseClicked(0)) {
                const int ci = br::ControlAt(page, pmx, pmy, &boxScroll, pageScroll);
                int link = br::LinkAt(page, pmx, pmy, &boxScroll, pageScroll);
                const br::Node* target = ci >= 0 ? page.controls[(std::size_t)ci].node : nullptr;
                if (!target && link >= 0 && (std::size_t)link < page.linkNodes.size() && page.linkNodes[(std::size_t)link] &&
                    page.linkNodes[(std::size_t)link]->name == "label")
                    target = br::FormState::LabelTarget(*page.linkNodes[(std::size_t)link], *doc);
                br::FormState& fs = tab.Forms();
                if (target && !br::FormState::IsDisabled(*target)) {
                    controlTook = true;
                    const br::ControlKind k = br::KindOf(*target);
                    if (br::FormState::IsTypable(*target)) {
                        if (!res.mouseDownCancelled) {   // a page that cancels mousedown keeps the field from taking the keyboard
                            if (edit->node != target) { edit->node = target; edit->buf.assign(4096, 0); edit->focusNow = true; }
                            tab.SetFocus(target);
                        }
                    }
                    else {
                        edit->node = nullptr;
                        tab.SetFocus(target);
                        if (k == br::ControlKind::Checkbox || k == br::ControlKind::Radio) { fs.Toggle(*target); tab.FormChanged(); tab.ControlInput(*target); }
                        else if (k == br::ControlKind::Select) edit->selectOpen = target;
                        else if (k == br::ControlKind::Submit) {
                            if (const br::Node* form = br::FormState::FormOwner(*target, *doc)) res.formSent = tab.SubmitForm(*form, target, res.formNote);
                        } else if (k == br::ControlKind::Reset) { fs.ResetForm(br::FormState::FormOwner(*target, *doc)); tab.FormChanged(); }
                    }
                } else if (ci < 0 && edit->node) { edit->node = nullptr; tab.SetFocus(nullptr); }
            }
            // links: the pointer over a link shows a hand; a click reports it
            if (inView && hotPlay < 0) {
                const int link = br::LinkAt(page, pmx, pmy, &boxScroll, pageScroll);
                const bool isLabel = link >= 0 && (std::size_t)link < page.linkNodes.size() && page.linkNodes[(std::size_t)link] &&
                                     page.linkNodes[(std::size_t)link]->name == "label";
                {   // the page's cursor property where the pointer is (a link's hand unless the page says otherwise)
                    bool said = false;
                    const UI::MouseCursor shape = PageCursorAt(page, pmx, pmy, br::ControlAt(page, pmx, pmy, &boxScroll, pageScroll),
                                                               link >= 0 && link < (int)page.links.size() && !isLabel ? link : -1, said);
                    if (said) UI::SetMouseCursor(shape);
                }
                if (link >= 0 && link < (int)page.links.size() && !isLabel) {
                    res.hoverHref = page.links[(std::size_t)link];
                    if (!controlTook && UI::IsMouseClicked(0)) { res.linkClicked = link; res.linkInNewTab = UI::GetFrameInput().ctrl; }
                    else if (UI::IsMouseClicked(2)) { res.linkClicked = link; res.linkInNewTab = true; }
                }
                if (page.dynamic) {   // :hover: the element under the pointer (its link, else its innermost box)
                    const br::Node* hn = link >= 0 && (std::size_t)link < page.linkNodes.size() && page.linkNodes[(std::size_t)link]
                                             ? page.linkNodes[(std::size_t)link] : NodeAt(page, pmx, pmy, link);
                    tab.SetHover(hn);
                }
                if (UI::IsMouseClicked(1)) {   // the context menu: what is under the pointer
                    res.contextMenu = true;
                    res.contextAt = m;
                    res.contextLink = link >= 0 && link < (int)page.links.size() && !isLabel ? link : -1;
                    for (const br::DrawCmd& c : page.cmds)
                        if (c.kind == br::DrawCmd::Text && c.clip < 0 && pmx >= c.x && pmx < c.x + c.w && pmy >= c.y - 2 && pmy < c.y + c.h + 2) res.contextText = c.text;
                }
            } else if (page.dynamic && !inView) tab.SetHover(nullptr);
            // text selection: a press on the page's text (not a link, a field or a button) starts it, dragging moves its end;
            // Ctrl+A selects all the text and Ctrl+C copies it while no field has the keyboard
            if (nav && !loading && !options.blockInput) {
                if (inView && hotPlay < 0 && !controlTook && !res.mouseDownCancelled && UI::IsMouseClicked(0)) {
                    nav->ClearSelection();
                    int sc, sa;
                    if (br::LinkAt(page, pmx, pmy, &boxScroll, pageScroll) < 0 && br::ControlAt(page, pmx, pmy, &boxScroll, pageScroll) < 0 &&
                        TextPlaceAt(page, pmx, pmy, sc, sa)) {
                        nav->selFromCmd = nav->selToCmd = sc; nav->selFromChar = nav->selToChar = sa; nav->selecting = true;
                    }
                } else if (nav->selecting && UI::IsMouseDown(0)) {
                    int sc, sa;
                    if (TextPlaceAt(page, pmx, pmy, sc, sa)) { nav->selToCmd = sc; nav->selToChar = sa; }
                } else nav->selecting = false;
                const UI::Input& kin = UI::GetFrameInput();
                if (!UI::WantsTextInput() && !res.addressFocused) {
                    if ((kin.ctrl && UI::IsKeyPressed(UI::KeyId_A, false)) || kin.selectAll) {
                        int first = -1, last = -1;
                        for (std::size_t i = 0; i < page.cmds.size(); ++i)
                            if (page.cmds[i].kind == br::DrawCmd::Text && page.cmds[i].clip < 0 && !page.cmds[i].fixed && !page.cmds[i].noSelect) { if (first < 0) first = (int)i; last = (int)i; }
                        if (first >= 0) { nav->selFromCmd = first; nav->selFromChar = 0; nav->selToCmd = last; nav->selToChar = (int)page.cmds[(std::size_t)last].text.size(); }
                    }
                    if (((kin.ctrl && UI::IsKeyPressed(UI::KeyId_C, false)) || kin.copy) && nav->HasSelection())
                        UI::SetClipboardText(SelectedPageText(page, *nav).c_str());
                }
            }
            UI::SetCursorScreenPos(layoutOrigin);   // the content's height is measured from where the view laid it out
            UI::Dummy(viewW, std::max(page.height * zoom, 1.0f));
        } else if (tab.GetState() == br::BrowserTab::State::Loading) {
            UI::Spacing(24.0f);
            const std::string host = br::HostOf(tab.Url());
            UI::TextDisabled(host.empty() ? "Loading the page..." : ("Loading " + host + "...").c_str());
            UI::Dummy(viewW, 1.0f);
        } else if (tab.GetState() == br::BrowserTab::State::Error) {
            ErrorPage(tab, origin.x, viewTop, viewW, viewH, res);
        } else {
            UI::TextDisabled("No page.");
        }
        UI::EndChild();
    }
    UI::End();
    std::string bubble = res.hoverHref;
    if (bubble.empty() && options.keys && options.keys->focusLink >= 0 && tab.GetState() == br::BrowserTab::State::Ready) {
        const br::LayoutResult& lay = tab.Layout(res.page.w / (options.zoom > 0.0f ? options.zoom : 1.0f), OkayUIMeasure());
        if ((std::size_t)options.keys->focusLink < lay.links.size()) bubble = lay.links[(std::size_t)options.keys->focusLink];
    }
    if (!bubble.empty()) {
        // the address the link goes to (resolved against the page), in a bubble at the bottom-left like other browsers
        const std::string full = bubble.rfind("javascript:", 0) == 0 ? bubble : tab.ResolveImage(bubble);
        const std::string& target = full.empty() ? bubble : full;
        const auto& fg = UI::GetForegroundDrawList();
        const float maxW = std::max(80.0f, w * 0.6f - 20.0f);
        std::size_t chars = target.size();
        std::string t = target;
        while (chars > 8 && UI::CalcTextWidth(t.c_str()) > maxW) { chars -= chars > 40 ? 6 : 2; t = ShortUrl(target, chars); }
        const float lh = UI::GetTextLineHeight(), bw = UI::CalcTextWidth(t.c_str()) + 20.0f, bh = lh + 10.0f;
        const float bx = x, by = y + h - bh;
        fg->AddRectFilled(UI::Vec2(bx, by), UI::Vec2(bx + bw, by + bh), lk::Rgb(36, 40, 50, 248), 7.0f, UI::DrawFlags_RoundCornersTopRight);
        fg->AddRect(UI::Vec2(bx, by), UI::Vec2(bx + bw, by + bh), lk::kDivider, 7.0f, UI::DrawFlags_RoundCornersTopRight, 1.0f);
        fg->AddText(UI::Vec2(bx + 10.0f, by + 5.0f), lk::Rgb(220, 226, 236), t.c_str());
    }
    return res;
}

} // namespace okayuiscreens
