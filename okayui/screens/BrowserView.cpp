// Browser: the OkayUI view of one tab (see BrowserView.hpp).
#include "BrowserView.hpp"

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
struct WebFace { int weight; bool italic; const UI::Font* font; };
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

void RegisterWebFont(const std::string& family, int weight, bool italic, const UI::Font* font) {
    if (!font || family.empty()) return;
    Faces()[family].push_back({weight, italic, font});
}
void ClearWebFonts() { Faces().clear(); }

// OkayUI's text scale 1 is an 8 px line, so a font size of `px` is the scale px / 8 (what DrawList::AddText with a font size
// does). Measuring and drawing both set it, so the width the layout used is the width that is drawn. A web font is made the
// current font first (OkayUI then sizes it by the same scale).
struct TextScaleFor {
    float saved;
    const UI::Font* savedFont = nullptr;
    bool fontSet = false;
    explicit TextScaleFor(float px, const UI::Font* font = nullptr) : saved(0.0f) {
        if (font) { savedFont = UI::GetFont(); UI::SetFont(font); fontSet = true; }
        saved = UI::Style().textScale;
        UI::Style().textScale = px / 8.0f;
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
    dl->AddText(UI::Vec2(x, y), col, text.c_str(), f.size / 8.0f);
    if (f.bold && !FaceIsBold(f, web)) dl->AddText(UI::Vec2(x + std::max(1.0f, f.size / 24.0f), y), col, text.c_str(), f.size / 8.0f);
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
        UI::PushClipRect(cx, cy, std::max(0.0f, w - h), h);
        DrawText(dl, cx + 2, cy + (h - f.size) * 0.5f, f, col, c.text);
        UI::PopClipRect();
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
            SDL_Texture* tex = image ? image(c.text) : nullptr;
            const UI::Color32 tint = 0x00FFFFFFu | ((UI::Color32)c.color.a << 24);
            if (tex) dl->AddImage(tex, UI::Vec2(cx, cy), UI::Vec2(cx + c.w, cy + c.h), UI::Vec2(0, 0), UI::Vec2(1, 1), tint);
            else dl->AddRect(UI::Vec2(cx, cy), UI::Vec2(cx + c.w, cy + c.h), Pack(br::Color{160, 160, 160, 255}), 0.0f, 0, 1.0f);
            break;
        }
        case br::DrawCmd::BgImage: {
            SDL_Texture* tex = image ? image(c.text) : nullptr;
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

namespace {

// The element under the page point (x, y), for :hover: the link or label there, else the innermost box.
const br::Node* NodeAt(const br::LayoutResult& page, float px, float py, int link) {
    (void)link;
    const br::Node* best = nullptr;
    int bestDepth = -1;
    for (const br::BoxInfo& b : page.boxes) {
        if (!b.node || px < b.x || py < b.y || px >= b.x + b.w || py >= b.y + b.h) continue;
        if (b.depth >= bestDepth) { best = b.node; bestDepth = b.depth; }
    }
    return best;
}

} // namespace

BrowserViewResult DrawBrowserView(br::BrowserTab& tab, float x, float y, float w, float h, const BrowserImageFn& image,
                                  const BrowserViewOptions& options) {
    BrowserViewResult res;
    UI::BareNextWindow();
    if (UI::Begin("Browser##okayui", x, y, w, h)) {
        // ---- the toolbar ----
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
            const float extra = options.programButtons ? UI::CalcTextWidth(star) + UI::CalcTextWidth("Menu") + 56.0f : 0.0f;
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
        UI::Spacing(4.0f);
        if (options.belowToolbar && options.belowToolbarH > 0.0f) {
            const UI::Vec2 at = UI::GetCursorScreenPos();
            const float rowW = UI::GetContentRegionAvail().x;
            options.belowToolbar(at.x, at.y, rowW);
            UI::SetCursorScreenPos(at);
            UI::Dummy(rowW, options.belowToolbarH);
        }

        // ---- the page ----
        const UI::Vec2 avail = UI::GetContentRegionAvail();
        UI::BeginChild("##browserpage", 0.0f, std::max(20.0f, avail.y), false);
        const UI::Vec2 origin = UI::GetCursorScreenPos();   // the page's top-left (already moved up by the scroll)
        const float viewW = std::max(20.0f, UI::GetContentRegionAvail().x);
        const float scroll = UI::GetScrollY();
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
            const UI::Vec2 m = UI::GetMousePos();
            const bool inView = !options.blockInput &&
                                UI::IsMouseHoveringRect(UI::Vec2(origin.x, viewTop), UI::Vec2(origin.x + viewW, viewTop + viewH));
            const float pmx = (m.x - origin.x) / zoom, pmy = (m.y - origin.y) / zoom;   // the pointer in page coordinates
            const float pageScroll = scroll / zoom;
            // the wheel over a scrolling area scrolls it, not the page
            if (inView && UI::GetFrameInput().wheel != 0.0f) {
                const int sc = br::ScrollerAt(page, pmx, pmy, &boxScroll, pageScroll);
                if (sc >= 0) {
                    const br::Scroller& s = page.scrollers[(std::size_t)sc];
                    float& off = boxScroll[(std::size_t)sc].second;
                    const float before = off;
                    off = std::max(0.0f, std::min(std::max(0.0f, s.contentH - s.h), off - UI::GetFrameInput().wheel * 48.0f));
                    if (off != before) { UI::SetScrollY(prevScroll); tab.ScrollY() = prevScroll; }
                }
            }
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
                    if (edit->focusNow) { UI::SetKeyboardFocusHere(); edit->focusNow = false; }
                    bool enter = false;
                    if (kind == br::ControlKind::TextArea) {
                        UI::InputTextMultiline("##field", edit->buf.data(), (int)edit->buf.size(), UI::Vec2(cb.w * zoom, cb.h * zoom));
                    } else {
                        UI::SetNextItemWidth(cb.w * zoom);
                        enter = UI::InputText("##field", edit->buf.data(), (int)edit->buf.size(),
                                              UI::InputText_EnterReturnsTrue | (kind == br::ControlKind::Password ? UI::InputText_Password : 0));
                    }
                    UI::PopID();
                    const std::string typed(edit->buf.data());
                    if (typed != value) { fs.SetValue(*editing, typed); tab.FormChanged(); }
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
                    if (chosen >= 0) { fs.Select(*sel, chosen); tab.FormChanged(); edit->selectOpen = nullptr; controlTook = true; }
                    else if (UI::IsMouseClicked(0) || UI::IsKeyPressed(UI::KeyId_Escape, false)) { edit->selectOpen = nullptr; controlTook = true; }
                    else if (UI::IsMouseHoveringRect(UI::Vec2(sx, sy), UI::Vec2(sx + lw, sy + rowH * (float)opts.size() + 4))) controlTook = true;
                }
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
                    if (br::FormState::IsTypable(*target)) { if (edit->node != target) { edit->node = target; edit->buf.assign(4096, 0); edit->focusNow = true; } tab.SetFocus(target); }
                    else {
                        edit->node = nullptr;
                        tab.SetFocus(target);
                        if (k == br::ControlKind::Checkbox || k == br::ControlKind::Radio) { fs.Toggle(*target); tab.FormChanged(); }
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
                if (link >= 0 && link < (int)page.links.size() && !isLabel) {
                    UI::SetMouseCursor(UI::MouseCursor_Hand);
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
            UI::SetCursorScreenPos(origin);
            UI::Dummy(viewW, std::max(page.height * zoom, 1.0f));
        } else if (tab.GetState() == br::BrowserTab::State::Loading) {
            UI::Spacing(24.0f);
            UI::TextDisabled("Loading the page...");
            UI::Dummy(viewW, 1.0f);
        } else if (tab.GetState() == br::BrowserTab::State::Error) {
            UI::Spacing(24.0f);
            UI::Heading("This page could not be opened");
            UI::TextWrapped(tab.Error().c_str());
            UI::Spacing(8.0f);
            if (!tab.Url().empty() && UI::Button("Try again")) res.reload = true;
        } else {
            UI::TextDisabled("No page.");
        }
        UI::EndChild();
    }
    UI::End();
    if (!res.hoverHref.empty()) {
        // the address under the pointer, at the bottom-left like other browsers
        const auto& fg = UI::GetForegroundDrawList();
        const std::string t = ShortUrl(res.hoverHref, 90);
        const UI::Vec2 ts = UI::CalcTextSize(t.c_str());
        fg->AddRectFilled(UI::Vec2(x, y + h - ts.y - 8.0f), UI::Vec2(x + ts.x + 12.0f, y + h), Pack(br::Color{30, 34, 44, 235}));
        fg->AddText(UI::Vec2(x + 6.0f, y + h - ts.y - 4.0f), Pack(br::Color{220, 225, 235, 255}), t.c_str());
    }
    return res;
}

} // namespace okayuiscreens
