#include "okay/UI/UIWidgetsMoreDraw.hpp"
#include "okay/UI/UIRenderer.hpp"
#include "okay/Components/UIElement.hpp"
#include "okay/Graphics/Font.hpp"
#include <algorithm>
#include <cstdio>
#include <string>

namespace okay {
namespace ui2d {
namespace {

constexpr float kPx = 2.0f;

RGBA Ink(const Color& c, float op) {
    auto b = [](float v) {
        int i = (int)(v * 255.0f + 0.5f);
        if (i < 0) i = 0;
        if (i > 255) i = 255;
        return (std::uint8_t)i;
    };
    float a = c.a * op;
    if (a < 0.0f) a = 0.0f;
    if (a > 1.0f) a = 1.0f;
    return RGBA{b(c.r), b(c.g), b(c.b), b(a)};
}

void Box(Painter& p, float x, float y, float w, float h, const Color& c, float radius, float op) {
    if (w < 1.0f || h < 1.0f) return;
    Rect r{(int)x, (int)y, (int)w, (int)h};
    FillUIShape(p, r, radius > 0.0f ? UIShape::Rounded : UIShape::Rectangle, radius, c, c, false, false, op);
}

void Words(Painter& p, const std::string& s, float x, float y, const Color& c, float op) {
    DrawUIText(p, s, x, y, kPx, Ink(c, op));
}

float TextRow(float h) { return (h - Font8x8::Height * kPx) * 0.5f; }

void ClipTo(Painter& p, GameObject* go, float w, float h) {
    Vec2 o, sz;
    if (UIClipRectLogical(go, w, h, o, sz)) {
        Rect clip{(int)o.x, (int)o.y, (int)sz.x, (int)sz.y};
        p.SetClipRect(&clip);
    } else p.SetClipRect(nullptr);
}

void Grey(Painter& p, float x, float y, float w, float h) {
    Rect r{(int)x, (int)y, (int)w, (int)h};
    p.SetDrawColor(20, 22, 28, 120);
    p.FillRect(&r);
}

void DrawSearch(Painter& p, UISearchBar* w, const Vec2& o, float op) {
    Box(p, o.x, o.y, w->size.x, w->size.y, w->background, w->cornerRadius, op);
    const float fh = w->FieldH();
    Box(p, o.x + 4.0f, o.y + 4.0f, w->size.x - 8.0f, fh - 6.0f, w->field, w->cornerRadius, op);
    const std::string shown = w->query.empty() ? w->placeholder : w->query;
    const Color ink = w->query.empty() ? w->muted : w->textColor;
    Words(p, shown, o.x + 12.0f, o.y + 4.0f + TextRow(fh - 6.0f), ink, op);
    if (!w->query.empty())
        Words(p, "x", o.x + w->size.x - 22.0f, o.y + 4.0f + TextRow(fh - 6.0f), w->muted, op);
    const auto matches = w->Matches();
    const auto indexes = w->MatchIndexes();
    float y = o.y + fh + 4.0f;
    for (int i = 0; i < (int)matches.size(); ++i) {
        if (y + w->rowHeight > o.y + w->size.y) break;
        const bool on = i < (int)indexes.size() && indexes[(std::size_t)i] == w->value;
        if (on) Box(p, o.x + 4.0f, y, w->size.x - 8.0f, w->rowHeight - 2.0f, w->accent, 4.0f, op);
        Words(p, matches[(std::size_t)i], o.x + 12.0f, y + TextRow(w->rowHeight), on ? Color::White : w->textColor, op);
        y += w->rowHeight;
    }
}

void DrawDate(Painter& p, UIDateTimePicker* w, const Vec2& o, float op) {
    Box(p, o.x, o.y, w->size.x, w->size.y, w->background, w->cornerRadius, op);
    const int n = w->Columns();
    const float col = n > 0 ? w->size.x / (float)n : w->size.x;
    const char* labels[5] = {"Year", "Mon", "Day", "Hour", "Min"};
    const int values[5] = {w->year, w->month, w->day, w->hour, w->minute};
    for (int i = 0; i < n; ++i) {
        const float x = o.x + col * (float)i;
        const float numY = o.y + 6.0f;
        Words(p, "-", x + 4.0f, numY, w->button, op);
        Words(p, "+", x + col - 16.0f, numY, w->button, op);
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%02d", values[i]);
        if (i == 0) std::snprintf(buf, sizeof(buf), "%04d", values[i]);
        const float tw = (float)std::char_traits<char>::length(buf) * UIWidgetCharW();
        const float inner = std::max(8.0f, col - 36.0f);
        float nx = x + 18.0f + (inner - tw) * 0.5f;
        if (nx < x + 18.0f) nx = x + 18.0f;
        Words(p, buf, nx, numY, w->textColor, op);
        const float lw = (float)std::char_traits<char>::length(labels[i]) * UIWidgetCharW();
        if (lw < col - 4.0f)
            Words(p, labels[i], x + (col - lw) * 0.5f, o.y + w->size.y - 16.0f, w->muted, op);
    }
}

void DrawFiles(Painter& p, UIFilePicker* w, const Vec2& o, float op) {
    Box(p, o.x, o.y, w->size.x, w->size.y, w->background, w->cornerRadius, op);
    std::string head = w->Status();
    if (head.empty()) {
        if (!w->current.empty()) head = w->current;
        else {
            const auto slash = w->root.find_last_of("/\\");
            head = slash == std::string::npos ? w->root : w->root.substr(slash + 1);
        }
    }
    Words(p, head, o.x + 8.0f, o.y + TextRow(w->headerHeight), w->muted, op);
    const auto rows = w->Rows();
    if (rows.empty() && !w->Status().empty() && w->root.empty()) return;
    if (rows.empty())
        Words(p, w->Status().empty() ? "This folder is empty" : w->Status(), o.x + 8.0f, o.y + w->headerHeight + 6.0f, w->muted, op);
    float y = o.y + w->headerHeight;
    for (int i = 0; i < (int)rows.size(); ++i) {
        if (y + w->rowHeight > o.y + w->size.y) break;
        if (i == w->value) Box(p, o.x + 4.0f, y, w->size.x - 8.0f, w->rowHeight - 2.0f, w->accent, 4.0f, op);
        std::string label = rows[(std::size_t)i].name;
        if (rows[(std::size_t)i].directory && label != "..") label += "/";
        Words(p, label, o.x + 10.0f, y + TextRow(w->rowHeight), i == w->value ? Color::White : w->textColor, op);
        y += w->rowHeight;
    }
}

void DrawAccordion(Painter& p, UIAccordion* w, const Vec2& o, float op) {
    Box(p, o.x, o.y, w->size.x, w->size.y, w->background, w->cornerRadius, op);
    const auto sections = w->Sections();
    float y = o.y;
    for (int i = 0; i < (int)sections.size(); ++i) {
        if (y + w->headerHeight > o.y + w->size.y) break;
        const bool on = sections[(std::size_t)i].open;
        Box(p, o.x + 4.0f, y + 2.0f, w->size.x - 8.0f, w->headerHeight - 4.0f, on ? w->accent : w->header, 4.0f, op);
        Words(p, on ? "v" : ">", o.x + 12.0f, y + TextRow(w->headerHeight), Color::White, op);
        Words(p, sections[(std::size_t)i].title, o.x + 32.0f, y + TextRow(w->headerHeight), Color::White, op);
        y += w->headerHeight;
        if (on) {
            Words(p, sections[(std::size_t)i].body, o.x + 12.0f, y + 4.0f, w->muted, op);
            y += w->bodyHeight;
        }
    }
}

void DrawCrumbs(Painter& p, UIBreadcrumbs* w, const Vec2& o, float op) {
    Box(p, o.x, o.y, w->size.x, w->size.y, w->background, w->cornerRadius, op);
    const auto segs = w->Segments();
    const auto boxes = w->Layout();
    const float ty = o.y + TextRow(w->size.y);
    for (int i = 0; i < (int)segs.size() && i < (int)boxes.size(); ++i) {
        const bool on = i == w->value;
        Words(p, segs[(std::size_t)i], o.x + boxes[(std::size_t)i].x, ty, on ? w->accent : w->textColor, op);
        if (i + 1 < (int)segs.size())
            Words(p, "/", o.x + boxes[(std::size_t)i].x + boxes[(std::size_t)i].w, ty, w->textColor, op);
    }
}

void DrawPages(Painter& p, UIPagination* w, const Vec2& o, float op) {
    Box(p, o.x, o.y, w->size.x, w->size.y, w->background, w->cornerRadius, op);
    const float ty = o.y + TextRow(w->size.y);
    for (const auto& s : w->Slots()) {
        std::string label;
        bool on = false;
        if (s.page == -1) label = "Prev";
        else if (s.page == -2) label = "Next";
        else if (s.page == 0) label = "..";
        else { label = std::to_string(s.page); on = s.page == w->page; }
        if (on) Box(p, o.x + s.x + 2.0f, o.y + 4.0f, s.w - 4.0f, w->size.y - 8.0f, w->accent, 4.0f, op);
        Words(p, label, o.x + s.x + 8.0f, ty, on ? Color::White : w->textColor, op);
    }
}

void DrawSkeleton(Painter& p, UISkeleton* w, const Vec2& o, float op) {
    Box(p, o.x, o.y, w->size.x, w->size.y, w->background, w->cornerRadius, op);
    for (const auto& bar : w->Bars()) {
        Box(p, o.x + bar.x, o.y + bar.y, bar.w, bar.h, w->bar, bar.circle ? bar.h * 0.5f : 4.0f, op);
        const float band = bar.w * 0.35f;
        float hx = bar.x + (bar.w + band) * w->shimmer - band;
        if (hx < bar.x) hx = bar.x;
        float hw = band;
        if (hx + hw > bar.x + bar.w) hw = bar.x + bar.w - hx;
        if (hw > 2.0f) Box(p, o.x + hx, o.y + bar.y, hw, bar.h, w->highlight, bar.circle ? bar.h * 0.5f : 4.0f, op * 0.85f);
    }
}

void DrawSheet(Painter& p, UIBottomSheet* w, const Vec2& o, const Vec2& sz, float logicalW, float logicalH, float op) {
    const float t = w->ShownFraction();
    if (t <= 0.001f) return;
    if (w->dim) {
        p.SetClipRect(nullptr);
        Rect all{0, 0, (int)logicalW, (int)logicalH};
        const RGBA d = Ink(w->dimColor, op * t);
        p.SetDrawColor(d.r, d.g, d.b, d.a);
        p.FillRect(&all);
    }
    const float y = o.y + (1.0f - t) * sz.y;
    Box(p, o.x, y, sz.x, sz.y, w->background, w->cornerRadius, op);
    Box(p, o.x + sz.x * 0.5f - 24.0f, y + 6.0f, 48.0f, 4.0f, w->accent, 2.0f, op);
    Words(p, w->title, o.x + 16.0f, y + w->handle, w->textColor, op);
    Words(p, "x", o.x + sz.x - 22.0f, y + 2.0f, w->textColor, op);
}

} // namespace

bool DrawMoreUIWidget(int kind, GameObject* go, Painter& p, float logicalW, float logicalH, float opacity) {
    using namespace uilayers;
    const bool ours = kind == K_Search || kind == K_DateTime || kind == K_FilePicker || kind == K_Accordion
        || kind == K_Breadcrumbs || kind == K_Pagination || kind == K_Skeleton || kind == K_BottomSheet;
    if (!ours) return false;
    if (!go || !go->active || UIHidden(go)) return true;
    UIRect rect = GetUIRect(go);
    if (!rect.valid) return true;
    Vec2 o = UIResolveOrigin(go, logicalW, logicalH) - UIScrollTotal(go);
    Vec2 sz = rect.size;
    if (kind != K_BottomSheet) ClipTo(p, go, logicalW, logicalH);
    const float op = opacity < 0.0f ? 0.0f : (opacity > 1.0f ? 1.0f : opacity);
    if (kind == K_Search) { if (auto* w = go->GetComponent<UISearchBar>()) { DrawSearch(p, w, o, op); if (!w->interactable) Grey(p, o.x, o.y, sz.x, sz.y); } }
    else if (kind == K_DateTime) { if (auto* w = go->GetComponent<UIDateTimePicker>()) { DrawDate(p, w, o, op); if (!w->interactable) Grey(p, o.x, o.y, sz.x, sz.y); } }
    else if (kind == K_FilePicker) { if (auto* w = go->GetComponent<UIFilePicker>()) { DrawFiles(p, w, o, op); if (!w->interactable) Grey(p, o.x, o.y, sz.x, sz.y); } }
    else if (kind == K_Accordion) { if (auto* w = go->GetComponent<UIAccordion>()) { DrawAccordion(p, w, o, op); if (!w->interactable) Grey(p, o.x, o.y, sz.x, sz.y); } }
    else if (kind == K_Breadcrumbs) { if (auto* w = go->GetComponent<UIBreadcrumbs>()) { DrawCrumbs(p, w, o, op); if (!w->interactable) Grey(p, o.x, o.y, sz.x, sz.y); } }
    else if (kind == K_Pagination) { if (auto* w = go->GetComponent<UIPagination>()) { DrawPages(p, w, o, op); if (!w->interactable) Grey(p, o.x, o.y, sz.x, sz.y); } }
    else if (kind == K_Skeleton) { if (auto* w = go->GetComponent<UISkeleton>()) DrawSkeleton(p, w, o, op); }
    else if (kind == K_BottomSheet) { if (auto* w = go->GetComponent<UIBottomSheet>()) DrawSheet(p, w, o, sz, logicalW, logicalH, op); }
    return true;
}

} // namespace ui2d
} // namespace okay
