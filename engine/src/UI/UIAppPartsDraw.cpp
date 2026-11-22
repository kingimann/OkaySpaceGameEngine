// Drawing for the app parts (okay/Components/UIAppParts.hpp): one function per part. The parts draw on K_AppParts; a menu
// bar's dropdown draws on K_AppPopup so it lands over everything else. Written for this engine from the layout each part
// describes (rows, columns, a strip of steps); it shares only our own painter helpers.
#include "okay/UI/UIAppPartsDraw.hpp"
#include "okay/UI/UIRenderer.hpp"
#include "okay/Components/UIElement.hpp"
#include "okay/Components/UIAppParts.hpp"
#include "okay/Math/ColorSpace.hpp"
#include "okay/Graphics/Font.hpp"
#include <algorithm>
#include <cmath>
#include <string>

namespace okay {
namespace ui2d {
namespace {

RGBA Ink(const Color& c, float op) {
    auto b = [](float v) { int i = (int)(v * 255.0f + 0.5f); return (std::uint8_t)(i < 0 ? 0 : i > 255 ? 255 : i); };
    float a = c.a * op;
    a = a < 0.0f ? 0.0f : a > 1.0f ? 1.0f : a;
    return RGBA{b(c.r), b(c.g), b(c.b), b(a)};
}
Color Alpha(Color c, float a) { c.a *= a; return c; }
Color Opaque(Color c) { c.a = 1.0f; return c; }

void Box(Painter& p, float x, float y, float w, float h, const Color& c, float radius, float op) {
    if (w < 1.0f || h < 1.0f) return;
    Rect r{(int)x, (int)y, (int)w, (int)h};
    FillUIShape(p, r, radius > 0.0f ? UIShape::Rounded : UIShape::Rectangle, radius, c, c, false, false, op);
}
void Frame(Painter& p, float x, float y, float w, float h, const Color& c, float radius, int width, float op) {
    if (w < 2.0f || h < 2.0f) return;
    Rect r{(int)x, (int)y, (int)w, (int)h};
    FillUIRing(p, r, radius > 0.0f ? UIShape::Rounded : UIShape::Rectangle, radius, c, width, op);
}
void Words(Painter& p, const std::string& s, float x, float y, float px, const Color& c, float op) {
    DrawUIText(p, s, x, y, px, Ink(c, op));
}
float TextH(float px) { return Font8x8::Height * px; }

/// `s` cut with ".." so it is no wider than `width` at `px`.
std::string Fit(const std::string& s, float px, float width) {
    if (UITextW(s, px) <= width) return s;
    std::string t = s;
    while (!t.empty()) {
        do { t.pop_back(); } while (!t.empty() && ((unsigned char)t.back() & 0xC0) == 0x80);
        if (UITextW(t + "..", px) <= width) return t + "..";
    }
    return std::string();
}

/// A little triangle: right-pointing (closed) or down-pointing (open), `s` pixels wide.
void Arrow(Painter& p, float x, float y, float s, bool open, const Color& c, float op) {
    if (open) MMFillPoly(p, {Point{(int)x, (int)(y + s * 0.2f)}, Point{(int)(x + s), (int)(y + s * 0.2f)}, Point{(int)(x + s * 0.5f), (int)(y + s * 0.8f)}}, Ink(c, op));
    else      MMFillPoly(p, {Point{(int)(x + s * 0.2f), (int)y}, Point{(int)(x + s * 0.8f), (int)(y + s * 0.5f)}, Point{(int)(x + s * 0.2f), (int)(y + s)}}, Ink(c, op));
}

void SetClip(Painter& p, float x, float y, float w, float h) {
    Rect r{(int)x, (int)y, (int)std::max(0.0f, w), (int)std::max(0.0f, h)};
    p.SetClipRect(&r);
}

void Scrollbar(Painter& p, float x, float y, float h, float content, float view, float scroll, const Color& c, float op) {
    if (content <= view + 0.5f || h < 8.0f) return;
    Box(p, x, y, 6.0f, h, Color::FromBytes(255, 255, 255, 18), 3.0f, op);
    const float thumb = std::max(18.0f, h * view / content);
    const float max = content - view;
    const float t = max > 0.0f ? scroll / max : 0.0f;
    Box(p, x, y + (h - thumb) * t, 6.0f, thumb, Alpha(c, 0.85f), 3.0f, op);
}

// ---- menu bar ----
void DrawMenuBar(Painter& p, UIMenuBar* w, const Vec2& o, const Vec2& sz, float op) {
    const float px = w->fontScale;
    Box(p, o.x, o.y, sz.x, sz.y, w->background, 0.0f, op);
    Box(p, o.x, o.y + sz.y - 1.0f, sz.x, 1.0f, Alpha(w->accent, 0.45f), 0.0f, op);
    for (int i = 0; i < w->MenuCount(); ++i) {
        const float x0 = o.x + w->MenuX(i), mw = w->MenuW(i);
        const bool open = w->OpenIndex() == i;
        if (open) Box(p, x0 + 1.0f, o.y + 3.0f, mw - 2.0f, sz.y - 6.0f, Alpha(w->accent, 0.85f), 5.0f, op);
        const std::string& name = w->Parsed()[(std::size_t)i].name;
        Words(p, name, x0 + (mw - UITextW(name, px)) * 0.5f, o.y + (sz.y - TextH(px)) * 0.5f, px, w->textColor, op);
    }
    if (w->IsActive() && w->IsOpen()) Frame(p, o.x, o.y, sz.x, sz.y, Alpha(w->accent, 0.6f), 0.0f, 1, op);
}
void DrawMenuDrop(Painter& p, UIMenuBar* w, const Vec2& o, const Vec2& sz, float op) {
    const int m = w->OpenIndex();
    if (m < 0) return;
    const float px = w->fontScale;
    const float x0 = o.x + w->MenuX(m), dw = w->DropWidth(m), top = o.y + sz.y + 2.0f, dh = w->DropHeight(m);
    Box(p, x0 + 3.0f, top + 3.0f, dw, dh, Color::FromBytes(0, 0, 0, 90), 6.0f, op);           // a soft shadow
    Box(p, x0, top, dw, dh, Opaque(w->background), 6.0f, op);
    Frame(p, x0, top, dw, dh, Alpha(w->accent, 0.5f), 6.0f, 1, op);
    const auto& es = w->Parsed()[(std::size_t)m].entries;
    for (int e = 0; e < (int)es.size(); ++e) {
        const float y = o.y + w->EntryY(m, e), h = w->EntryH(m, e);
        const UIMenuBar::Entry& en = es[(std::size_t)e];
        if (en.separator) { Box(p, x0 + 8.0f, y + h * 0.5f, dw - 16.0f, 1.0f, Alpha(w->textColor, 0.25f), 0.0f, op); continue; }
        const float a = en.disabled ? 0.4f : 1.0f;
        if (w->Hovered() == e && !en.disabled) Box(p, x0 + 3.0f, y, dw - 6.0f, h, Alpha(w->accent, 0.9f), 4.0f, op);
        Words(p, en.label, x0 + w->Pad(), y + (h - TextH(px)) * 0.5f, px, w->textColor, op * a);
        if (!en.shortcut.empty())
            Words(p, en.shortcut, x0 + dw - w->Pad() - UITextW(en.shortcut, px), y + (h - TextH(px)) * 0.5f, px, Alpha(w->textColor, 0.6f), op * a);
    }
}

// ---- toolbar ----
void DrawToolbarIcon(Painter& p, const std::string& icon, float x, float y, float s, const Color& tint, float op) {
    if (icon.find('.') != std::string::npos || icon.find('/') != std::string::npos) {
        if (Tex t = p.LoadTexture(icon)) {
            FRect dst{x + 4.0f, y + 4.0f, s - 8.0f, s - 8.0f};
            p.SetTextureAlphaMod(t, Ink(tint, op).a);
            p.CopyF(t, nullptr, &dst);
            return;
        }
    }
    std::string g = icon.substr(0, 2);
    const float px = std::max(1.0f, (s - 12.0f) / 8.0f * (g.size() > 1 ? 0.5f : 1.0f));
    Words(p, g, x + (s - UITextW(g, px)) * 0.5f, y + (s - TextH(px)) * 0.5f, px, tint, op);
}
void DrawToolbar(Painter& p, UIToolbar* w, const Vec2& o, const Vec2& sz, float op) {
    const float px = w->fontScale;
    Box(p, o.x, o.y, sz.x, sz.y, w->background, 8.0f, op);
    const float bh = w->ButtonSize();
    for (int i = 0; i < w->Count(); ++i) {
        const UIToolbar::Item& it = w->Parsed()[(std::size_t)i];
        const float x0 = o.x + w->ItemX(i), bw = w->ItemWidth(i), y0 = o.y + (sz.y - bh) * 0.5f;
        if (it.separator) { Box(p, x0 + 5.0f, y0 + 3.0f, 1.0f, bh - 6.0f, Alpha(w->textColor, 0.3f), 0.0f, op); continue; }
        const bool on = w->IsOn(i), hot = w->Hovered() == i && !it.disabled;
        const float a = it.disabled ? 0.4f : 1.0f;
        if (on) Box(p, x0, y0, bw, bh, Alpha(w->accent, 0.9f), 6.0f, op * a);
        else if (hot) Box(p, x0, y0, bw, bh, Alpha(w->textColor, 0.14f), 6.0f, op * a);
        float tx = x0;
        if (!it.icon.empty()) { DrawToolbarIcon(p, it.icon, x0, y0, bh, w->textColor, op * a); tx += bh; }
        if (w->showLabels && !it.label.empty()) {
            const float room = x0 + bw - tx;
            Words(p, it.label, tx + (room - UITextW(it.label, px)) * 0.5f, y0 + (bh - TextH(px)) * 0.5f, px, w->textColor, op * a);
        }
    }
}

// ---- tree view ----
void DrawTree(Painter& p, UITreeView* w, const Vec2& o, const Vec2& sz, float op) {
    const float px = w->fontScale;
    Box(p, o.x, o.y, sz.x, sz.y, w->background, 6.0f, op);
    Rect saved{}; const bool wasClipped = p.IsClipEnabled(); if (wasClipped) p.GetClipRect(&saved);
    SetClip(p, o.x, o.y, sz.x, sz.y);
    int first = 0, last = -1;
    w->VisibleRange(first, last);                                   // only the rows on screen are drawn
    for (int r = first; r <= last; ++r) {
        const int n = w->Visible()[(std::size_t)r];
        const float y = o.y + (float)r * w->rowHeight - w->Scroll();
        const float x = o.x + (float)w->Depth(n) * w->indent;
        if (w->selected == n) Box(p, o.x + 2.0f, y, sz.x - 4.0f, w->rowHeight, Alpha(w->accent, w->IsActive() ? 0.85f : 0.55f), 4.0f, op);
        if (w->HasChildren(n)) Arrow(p, x + 6.0f, y + (w->rowHeight - 10.0f) * 0.5f, 10.0f, w->IsExpanded(n), w->textColor, op);
        Words(p, w->Text(n), x + 24.0f, y + (w->rowHeight - TextH(px)) * 0.5f, px, w->textColor, op);
    }
    Scrollbar(p, o.x + sz.x - 8.0f, o.y + 2.0f, sz.y - 4.0f, w->ContentHeight(), sz.y, w->Scroll(), w->accent, op);
    if (wasClipped) p.SetClipRect(&saved); else p.SetClipRect(nullptr);
    if (w->IsActive()) Frame(p, o.x, o.y, sz.x, sz.y, Alpha(w->accent, 0.8f), 6.0f, 2, op);
}

// ---- data table ----
void DrawTable(Painter& p, UIDataTable* w, const Vec2& o, const Vec2& sz, float op) {
    const float px = w->fontScale;
    Box(p, o.x, o.y, sz.x, sz.y, w->background, 6.0f, op);
    Rect saved{}; const bool wasClipped = p.IsClipEnabled(); if (wasClipped) p.GetClipRect(&saved);
    SetClip(p, o.x, o.y, sz.x, sz.y);
    const int cols = w->ColumnCount();
    // rows first, the header on top of them
    int first = 0, last = -1;
    w->VisibleRange(first, last);                                   // only the rows on screen are drawn
    SetClip(p, o.x, o.y + w->headerHeight, sz.x, sz.y - w->headerHeight);
    for (int d = first; d <= last; ++d) {
        const float y = o.y + w->headerHeight + (float)d * w->rowHeight - w->Scroll();
        const int src = w->SourceRow(d);
        if (w->selected == src) Box(p, o.x + 1.0f, y, sz.x - 2.0f, w->rowHeight, Alpha(w->accent, w->IsActive() ? 0.85f : 0.55f), 3.0f, op);
        else if (w->zebra && (d & 1)) Box(p, o.x + 1.0f, y, sz.x - 2.0f, w->rowHeight, Color::FromBytes(255, 255, 255, 12), 0.0f, op);
        for (int c = 0; c < cols; ++c) {
            const float cw = w->ColumnWidth(c);
            const std::string t = Fit(w->Cell(d, c), px, cw - 14.0f);
            Words(p, t, o.x + w->ColumnX(c) + 8.0f, y + (w->rowHeight - TextH(px)) * 0.5f, px, w->textColor, op);
        }
    }
    SetClip(p, o.x, o.y, sz.x, sz.y);
    Box(p, o.x, o.y, sz.x, w->headerHeight, Opaque(Color(w->background.r * 0.6f + w->accent.r * 0.18f, w->background.g * 0.6f + w->accent.g * 0.18f, w->background.b * 0.6f + w->accent.b * 0.18f, 1.0f)), 0.0f, op);
    for (int c = 0; c < cols; ++c) {
        const float x0 = o.x + w->ColumnX(c), cw = w->ColumnWidth(c);
        const bool sorted = w->sortColumn == c;
        const std::string t = Fit(w->ColumnTitle(c), px, cw - (sorted ? 34.0f : 16.0f));
        Words(p, t, x0 + 8.0f, o.y + (w->headerHeight - TextH(px)) * 0.5f, px, sorted ? w->accent : w->textColor, op);
        if (sorted) Arrow(p, x0 + 8.0f + UITextW(t, px) + 8.0f, o.y + (w->headerHeight - 10.0f) * 0.5f, 10.0f, w->ascending, w->accent, op);
        if (c > 0) Box(p, x0, o.y + 5.0f, 1.0f, w->headerHeight - 10.0f, Alpha(w->textColor, 0.18f), 0.0f, op);
    }
    Box(p, o.x, o.y + w->headerHeight - 1.0f, sz.x, 1.0f, Alpha(w->accent, 0.5f), 0.0f, op);
    Scrollbar(p, o.x + sz.x - 8.0f, o.y + w->headerHeight + 2.0f, sz.y - w->headerHeight - 4.0f, (float)w->RowCount() * w->rowHeight, w->ViewportHeight(), w->Scroll(), w->accent, op);
    if (wasClipped) p.SetClipRect(&saved); else p.SetClipRect(nullptr);
    if (w->IsActive()) Frame(p, o.x, o.y, sz.x, sz.y, Alpha(w->accent, 0.8f), 6.0f, 2, op);
}

// ---- wizard ----
void DrawWizard(Painter& p, UIWizard* w, const Vec2& o, const Vec2& sz, float op) {
    const float px = w->fontScale;
    const std::vector<std::string> names = w->StepNames();
    const int n = (int)names.size();
    Box(p, o.x, o.y, sz.x, w->HeaderHeight(), w->background, 0.0f, op);   // the header band (the pages in between stay see-through)
    Frame(p, o.x, o.y, sz.x, sz.y, Alpha(w->accent, 0.35f), 8.0f, 1, op);
    if (n > 0) {
        if (n > 1) {
            const float ly = o.y + w->MarkerY();
            Box(p, o.x + w->MarkerX(0), ly - 1.0f, w->MarkerX(n - 1) - w->MarkerX(0), 3.0f, Alpha(w->textColor, 0.2f), 1.0f, op);
            Box(p, o.x + w->MarkerX(0), ly - 1.0f, w->MarkerX(std::min(w->step, n - 1)) - w->MarkerX(0), 3.0f, w->accent, 1.0f, op);
        }
        for (int i = 0; i < n; ++i) {
            const float cx = o.x + w->MarkerX(i), cy = o.y + w->MarkerY();
            const bool done = i < w->step || (w->Finished() && i == w->step), cur = i == w->step && !w->Finished();
            MMFillCircle(p, (int)cx, (int)cy, 13, Ink(done ? w->accent : Color::FromBytes(36, 40, 52), op));
            MMDrawRing(p, (int)cx, (int)cy, 13, cur ? 3 : 2, Ink(done || cur ? w->accent : Alpha(w->textColor, 0.35f), op));
            const std::string num = std::to_string(i + 1);
            Words(p, num, cx - UITextW(num, 1.5f) * 0.5f, cy - TextH(1.5f) * 0.5f, 1.5f, w->textColor, op * (done || cur ? 1.0f : 0.6f));
            const std::string t = Fit(names[(std::size_t)i], 1.5f, std::max(40.0f, (sz.x - 96.0f) / (float)std::max(1, n - 1)));
            Words(p, t, cx - UITextW(t, 1.5f) * 0.5f, cy + 19.0f, 1.5f, w->textColor, op * (cur ? 1.0f : 0.65f));
        }
    }
    if (w->showButtons) {
        const float fy = o.y + sz.y - w->FooterHeight();
        Box(p, o.x, fy, sz.x, w->FooterHeight(), w->background, 0.0f, op);
        Box(p, o.x, fy, sz.x, 1.0f, Alpha(w->accent, 0.35f), 0.0f, op);
        float x, y, bw, bh;
        w->BackRect(x, y, bw, bh);
        const bool canBack = w->step > 0;
        Box(p, o.x + x, o.y + y, bw, bh, canBack ? Color::FromBytes(60, 66, 84) : Color::FromBytes(40, 44, 56), 6.0f, op);
        Words(p, "Back", o.x + x + (bw - UITextW("Back", px)) * 0.5f, o.y + y + (bh - TextH(px)) * 0.5f, px, w->textColor, op * (canBack ? 1.0f : 0.4f));
        const std::string why = w->Blocker();
        const std::string nextLabel = w->IsLast() ? "Finish" : "Next";
        w->NextRect(x, y, bw, bh);
        Box(p, o.x + x, o.y + y, bw, bh, why.empty() ? w->accent : Color::FromBytes(70, 60, 64), 6.0f, op);
        Words(p, nextLabel, o.x + x + (bw - UITextW(nextLabel, px)) * 0.5f, o.y + y + (bh - TextH(px)) * 0.5f, px, w->textColor, op * (why.empty() ? 1.0f : 0.6f));
        std::string note = w->Finished() ? "Finished" : (why.empty() ? std::string() : why);
        if (!note.empty()) Words(p, note, o.x + sz.x * 0.5f - UITextW(note, 1.5f) * 0.5f, o.y + y + (bh - TextH(1.5f)) * 0.5f, 1.5f,
                                 w->Finished() ? Color::FromBytes(110, 210, 140) : Color::FromBytes(235, 120, 100), op);
    }
}

// ---- form group ----
void DrawForm(Painter& p, UIFormGroup* w, const Vec2& o, const Vec2& sz, float op) {
    const float px = w->fontScale;
    const int n = w->Count();
    for (int i = 0; i < n; ++i) {
        const UIFormGroup::Spec& s = w->Specs()[(std::size_t)i];
        const float y = o.y + (float)i * w->rowHeight + (w->fieldHeight - TextH(px)) * 0.5f;
        const std::string t = Fit(s.label, px, w->labelWidth - 12.0f - (s.required ? 14.0f : 0.0f));
        Words(p, t, o.x, y, px, w->textColor, op);
        if (s.required) Words(p, "*", o.x + UITextW(t, px) + 4.0f, y, px, Color::FromBytes(235, 100, 100), op);
    }
    if (w->showSummary && n > 0) {
        bool touched = false;
        for (int i = 0; i < n; ++i) if (auto* v = w->Validator(i)) touched = touched || v->Touched();
        const int bad = w->ErrorCount();
        const float y = o.y + (float)n * w->rowHeight + 2.0f + (36.0f - TextH(1.5f)) * 0.5f;
        const float x = o.x + w->labelWidth + (w->submitLabel.empty() ? 0.0f : 176.0f);
        if (bad > 0 && touched) Words(p, std::to_string(bad) + (bad == 1 ? " problem to fix" : " problems to fix"), x, y, 1.5f, Color::FromBytes(235, 110, 100), op);
        else if (bad == 0) Words(p, w->Submitted() ? "Sent" : "Looks good", x, y, 1.5f, Color::FromBytes(110, 210, 140), op);
    }
}

// ---- colour square ----
void DrawSquare(Painter& p, UIColorSquare* w, const Vec2& o, const Vec2& sz, float op) {
    const int cols = 32, rows = 24;
    const float cw = sz.x / (float)cols, ch = sz.y / (float)rows;
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            const float h = (((float)c + 0.5f) / (float)cols) * 360.0f;
            const float s = 1.0f - (((float)r + 0.5f) / (float)rows);
            const RGBA col = Ink(ColorSpace::FromHsv(h, s, w->brightness), op);
            p.SetDrawColor(col.r, col.g, col.b, col.a);
            const int x0 = (int)(o.x + (float)c * cw), y0 = (int)(o.y + (float)r * ch);
            const int x1 = (int)(o.x + (float)(c + 1) * cw + 0.999f), y1 = (int)(o.y + (float)(r + 1) * ch + 0.999f);
            Rect rc{x0, y0, std::max(1, x1 - x0), std::max(1, y1 - y0)};
            p.FillRect(&rc);
        }
    }
    const int hx = (int)(o.x + w->hue / 360.0f * sz.x), hy = (int)(o.y + (1.0f - w->sat) * sz.y);
    MMDrawRing(p, hx, hy, 7, 3, Ink(Color::FromBytes(0, 0, 0, 200), op));
    MMDrawRing(p, hx, hy, 6, 2, Ink(Color::White, op));
    Frame(p, o.x, o.y, sz.x, sz.y, Color::FromBytes(255, 255, 255, 60), 0.0f, 1, op);
}

} // namespace

bool DrawAppParts(int kind, GameObject* go, Painter& p, float logicalW, float logicalH, float opacity) {
    using namespace uilayers;
    if (kind != K_AppParts && kind != K_AppPopup) return false;
    if (!go || !go->active || UIHidden(go)) return true;
    UIRect rect = GetUIRect(go);
    if (!rect.valid) return true;
    const Vec2 o = UIResolveOrigin(go, logicalW, logicalH) - UIScrollTotal(go);
    const Vec2 sz = rect.size;
    const float op = opacity < 0.0f ? 0.0f : (opacity > 1.0f ? 1.0f : opacity);
    Rect clip{};
    Vec2 co, cs;
    if (UIClipRectLogical(go, logicalW, logicalH, co, cs)) { clip = Rect{(int)co.x, (int)co.y, (int)cs.x, (int)cs.y}; p.SetClipRect(&clip); }
    else p.SetClipRect(nullptr);
    if (kind == K_AppPopup) { if (auto* w = go->GetComponent<UIMenuBar>()) DrawMenuDrop(p, w, o, sz, op); return true; }
    if (auto* w = go->GetComponent<UIMenuBar>()) DrawMenuBar(p, w, o, sz, op);
    if (auto* w = go->GetComponent<UIToolbar>()) DrawToolbar(p, w, o, sz, op);
    if (auto* w = go->GetComponent<UITreeView>()) DrawTree(p, w, o, sz, op);
    if (auto* w = go->GetComponent<UIDataTable>()) DrawTable(p, w, o, sz, op);
    if (auto* w = go->GetComponent<UIWizard>()) DrawWizard(p, w, o, sz, op);
    if (auto* w = go->GetComponent<UIFormGroup>()) DrawForm(p, w, o, sz, op);
    if (auto* w = go->GetComponent<UIColorSquare>()) DrawSquare(p, w, o, sz, op);
    return true;
}

} // namespace ui2d
} // namespace okay
