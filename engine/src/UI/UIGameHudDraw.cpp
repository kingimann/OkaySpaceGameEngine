// Drawing for the game HUD parts (okay/Components/UIGameHud.hpp): one function per part, all on layer K_GameHud.
#include "okay/UI/UIGameHudDraw.hpp"
#include "okay/UI/UIRenderer.hpp"
#include "okay/Components/UIElement.hpp"
#include "okay/Components/UIGameHud.hpp"
#include "okay/Inventory/ItemPresentation.hpp"
#include "okay/Graphics/Font.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
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

// A point on a circle: angle in degrees clockwise from the top.
Point OnCircle(float cx, float cy, float r, float deg) {
    const float a = deg * 0.0174532925f;
    return Point{(int)std::lround(cx + r * std::sin(a)), (int)std::lround(cy - r * std::cos(a))};
}

// A ring sector from radius r0 to r1 between two angles, built from small convex quads.
void Sector(Painter& p, float cx, float cy, float r0, float r1, float a0, float a1, RGBA col) {
    const int steps = std::max(1, (int)std::ceil(std::fabs(a1 - a0) / 8.0f));
    for (int s = 0; s < steps; ++s) {
        const float u0 = a0 + (a1 - a0) * (float)s / (float)steps, u1 = a0 + (a1 - a0) * (float)(s + 1) / (float)steps;
        MMFillPoly(p, {OnCircle(cx, cy, r1, u0), OnCircle(cx, cy, r1, u1), OnCircle(cx, cy, r0, u1), OnCircle(cx, cy, r0, u0)}, col);
    }
}

// ---- radial menu ----
void DrawRadial(Painter& p, UIRadialMenu* w, const Vec2& o, const Vec2& sz, float op) {
    if (!w->IsOpen() && !w->alwaysShown) return;
    const std::vector<std::string> labels = w->Labels();
    const int n = (int)labels.size();
    const float cx = o.x + sz.x * 0.5f, cy = o.y + sz.y * 0.5f;
    const float R = std::min(sz.x, sz.y) * 0.5f;
    const float inner = std::min(w->innerRadius, R - 4.0f);
    MMFillCircle(p, (int)cx, (int)cy, (int)R + 2, Ink(Color::FromBytes(8, 10, 16, 150), op));
    if (n == 0) { Words(p, "no items", cx - UITextW("no items", 2.0f) * 0.5f, cy - 8.0f, 2.0f, w->textColor, op); return; }
    const float span = 360.0f / (float)n;
    const float gap = n > 1 ? 1.5f : 0.0f;
    for (int i = 0; i < n; ++i) {
        const float mid = UIRadialMenu::SegmentCenter(i, n);
        const bool hot = w->Hovered() == i;
        const bool picked = w->Chosen() == i && !w->IsOpen();
        Color c = hot ? w->accent : w->background;
        if (picked) c = Color::FromBytes(60, 150, 100, 230);
        Sector(p, cx, cy, inner, R, mid - span * 0.5f + gap, mid + span * 0.5f - gap, Ink(c, op));
        // label at the middle of the wedge
        const Point lp = OnCircle(cx, cy, (inner + R) * 0.5f, mid);
        const float px = 2.0f;
        const float tw = UITextW(labels[(std::size_t)i], px);
        Words(p, labels[(std::size_t)i], (float)lp.x - tw * 0.5f, (float)lp.y - TextH(px) * 0.5f, px, w->textColor, op);
    }
    MMDrawRing(p, (int)cx, (int)cy, (int)inner, 2, Ink(w->accent, op * 0.8f));
    std::string mid = w->centerText;
    if (mid.empty() && w->Hovered() >= 0 && w->Hovered() < n) mid = labels[(std::size_t)w->Hovered()];
    if (!mid.empty()) Words(p, mid, cx - UITextW(mid, 2.0f) * 0.5f, cy - TextH(2.0f) * 0.5f, 2.0f, w->textColor, op);
}

// ---- kill feed ----
void DrawKillFeed(Painter& p, UIKillFeed* w, const Vec2& o, const Vec2& sz, float op) {
    float y = o.y;
    const float px = 1.75f;
    for (const UIKillFeed::Entry& e : w->Entries()) {
        const float a = w->Alpha(e) * op;
        if (a <= 0.01f) continue;
        const float tw = UITextW(e.text, px);
        const float bw = tw + 20.0f;
        const float x = w->alignRight ? o.x + sz.x - bw : o.x;
        Box(p, x, y, bw, w->rowHeight - 4.0f, w->background, 6.0f, a);
        if (e.mine) Frame(p, x, y, bw, w->rowHeight - 4.0f, w->accent, 6.0f, 2, a);
        Words(p, e.text, x + 10.0f, y + (w->rowHeight - 4.0f - TextH(px)) * 0.5f, px, e.mine ? w->accent : w->textColor, a);
        y += w->rowHeight;
    }
}

// ---- ammo counter ----
void DrawAmmo(Painter& p, UIAmmoCounter* w, const Vec2& o, const Vec2& sz, float op) {
    const UIAmmoCounter::State s = w->Read();
    const int level = s.current <= 0 ? 2 : (s.magazine > 0 && (float)s.current <= w->lowFraction * (float)s.magazine ? 1 : 0);
    const Color num = level == 2 ? w->emptyColor : level == 1 ? w->warnColor : w->textColor;
    Box(p, o.x, o.y, sz.x, sz.y, w->background, 10.0f, op);
    Words(p, w->label, o.x + 14.0f, o.y + 10.0f, 1.5f, w->accent, op);
    const std::string t = w->Text();
    Words(p, t, o.x + sz.x - 14.0f - UITextW(t, 4.0f), o.y + 8.0f, 4.0f, num, op);
    const float py = o.y + sz.y - 22.0f;
    if (s.reloading) { Words(p, "RELOADING", o.x + 14.0f, py + 2.0f, 2.0f, w->warnColor, op); return; }
    if (w->showPips && s.magazine > 0 && s.magazine <= 40) {
        const float avail = sz.x - 28.0f;
        const float step = std::min(10.0f, avail / (float)s.magazine);
        for (int i = 0; i < s.magazine; ++i) {
            const bool full = i < s.current;
            Box(p, o.x + 14.0f + (float)i * step, py, std::max(2.0f, step - 2.0f), 14.0f, full ? num : Color::FromBytes(255, 255, 255, 40), 1.0f, op);
        }
    }
}

// ---- interaction prompt ----
void DrawPrompt(Painter& p, UIInteractionPrompt* w, const Vec2& o, const Vec2& sz, float op) {
    const float a = w->Alpha() * op;
    if (a <= 0.01f) return;
    const float px = 2.0f;
    const std::string key = w->key.empty() ? "?" : w->key;
    const float capW = std::max(34.0f, UITextW(key, px) + 18.0f);
    const float h = std::min(sz.y, 36.0f);
    const float total = capW + 10.0f + UITextW(w->action, px) + 16.0f;
    const float x = o.x + (sz.x - total) * 0.5f;
    Box(p, x, o.y, total, h, w->background, h * 0.5f, a);
    Box(p, x + 4.0f, o.y + 4.0f, capW, h - 8.0f, w->accent, 5.0f, a);
    Words(p, key, x + 4.0f + (capW - UITextW(key, px)) * 0.5f, o.y + (h - TextH(px)) * 0.5f, px, Color::White, a);
    Words(p, w->action, x + 4.0f + capW + 10.0f, o.y + (h - TextH(px)) * 0.5f, px, w->textColor, a);
}

// ---- objective tracker ----
void DrawTracker(Painter& p, UIObjectiveTracker* w, const Vec2& o, const Vec2& sz, float op) {
    const std::vector<UIObjectiveTracker::Line> lines = w->Lines();
    if (lines.empty()) return;
    const float h = std::min(sz.y > 0.0f ? sz.y : 1.0e9f, w->ContentHeight());
    Box(p, o.x, o.y, sz.x, h, w->background, 10.0f, op);
    Words(p, w->title, o.x + 14.0f, o.y + 10.0f, 1.75f, w->accent, op);
    float y = o.y + 34.0f;
    const float px = 1.5f;
    for (const auto& l : lines) {
        if (y + 16.0f > o.y + h) break;
        if (l.kind == UIObjectiveTracker::Line::Quest) {
            Words(p, l.text, o.x + 14.0f, y, 2.0f, w->textColor, op);
        } else if (l.kind == UIObjectiveTracker::Line::Note) {
            Words(p, l.text, o.x + 14.0f, y, px, Color::FromBytes(160, 166, 180), op);
        } else {
            const bool done = l.kind == UIObjectiveTracker::Line::Done;
            Frame(p, o.x + 18.0f, y + 1.0f, 11.0f, 11.0f, done ? w->doneColor : w->textColor, 2.0f, 1, op);
            if (done) Box(p, o.x + 21.0f, y + 4.0f, 5.0f, 5.0f, w->doneColor, 1.0f, op);
            const Color c = done ? w->doneColor : w->textColor;
            Words(p, l.text, o.x + 36.0f, y, px, c, op);
            if (l.need > 1) {
                const std::string pr = std::to_string(std::min(l.have, l.need)) + "/" + std::to_string(l.need);
                Words(p, pr, o.x + sz.x - 14.0f - UITextW(pr, px), y, px, c, op);
            }
        }
        y += 20.0f;
    }
}

// ---- compass ----
void DrawCompass(Painter& p, UICompass* w, const Vec2& o, const Vec2& sz, float op) {
    float hd = std::fmod(w->Heading(), 360.0f); if (hd < 0.0f) hd += 360.0f;
    Box(p, o.x, o.y, sz.x, sz.y, w->background, 8.0f, op);
    const float fov = w->fieldOfView > 20.0f ? w->fieldOfView : 20.0f;
    const float left = o.x + 10.0f, right = o.x + sz.x - 10.0f;
    const float first = std::floor((hd - fov * 0.5f) / 15.0f) * 15.0f;
    for (float a = first; a <= hd + fov * 0.5f + 15.0f; a += 15.0f) {
        const float x = o.x + UICompass::StripX(UICompass::Wrap180(a - hd), fov, sz.x);
        // wrap-around: a - hd can fold across 180 for wide fovs; Wrap180 already handles the seam
        if (x < left || x > right) continue;
        int deg = (int)std::lround(a); deg = ((deg % 360) + 360) % 360;
        const bool card = deg % 45 == 0;
        const float th = card ? 14.0f : 7.0f;
        Box(p, x - 1.0f, o.y + sz.y - th - 4.0f, 2.0f, th, card ? w->textColor : Color::FromBytes(160, 166, 180), 0.0f, op);
        if (card) {
            const std::string name = UICompass::Cardinal((float)deg);
            const Color c = deg == 0 ? Color::FromBytes(235, 90, 90) : w->textColor;
            Words(p, name, x - UITextW(name, 2.0f) * 0.5f, o.y + 6.0f, 2.0f, c, op);
        }
    }
    for (const UICompass::Marker& m : w->Markers()) {
        const float rel = UICompass::Wrap180(m.bearing - hd);
        if (std::fabs(rel) > fov * 0.5f) continue;
        const float x = o.x + UICompass::StripX(rel, fov, sz.x);
        if (x < left || x > right) continue;
        const float cy = o.y + sz.y * 0.5f;
        MMFillPoly(p, {Point{(int)x, (int)cy - 7}, Point{(int)x + 6, (int)cy}, Point{(int)x, (int)cy + 7}, Point{(int)x - 6, (int)cy}}, Ink(w->markerColor, op));
        if (w->showMarkerDistance && m.distance > 0.0f) {
            char b[24]; std::snprintf(b, sizeof(b), "%.0fm", m.distance);
            Words(p, b, x - UITextW(b, 1.25f) * 0.5f, o.y + sz.y - 14.0f, 1.25f, w->markerColor, op);
        }
    }
    // the pointer in the middle and the heading
    const float mx = o.x + sz.x * 0.5f;
    MMFillTriangle(p, Point{(int)mx - 5, (int)o.y - 1}, Point{(int)mx + 5, (int)o.y - 1}, Point{(int)mx, (int)o.y + 7}, Ink(w->accent, op));
    if (w->showDegrees) {
        char b[16]; std::snprintf(b, sizeof(b), "%03d", (int)std::lround(hd) % 360);
        Words(p, b, mx - UITextW(b, 1.5f) * 0.5f, o.y + sz.y + 4.0f, 1.5f, w->textColor, op);
    }
}

// ---- item tooltip card ----
std::vector<std::string> Wrap(const std::string& text, float width, float px) {
    std::vector<std::string> out;
    std::string line, word;
    auto flushWord = [&]() {
        if (word.empty()) return;
        const std::string trial = line.empty() ? word : line + " " + word;
        if (!line.empty() && UITextW(trial, px) > width) { out.push_back(line); line = word; }
        else line = trial;
        word.clear();
    };
    for (char c : text) { if (c == ' ') flushWord(); else word += c; }
    flushWord();
    if (!line.empty()) out.push_back(line);
    if (out.empty()) out.push_back("");
    return out;
}

void DrawCard(Painter& p, UIItemTooltipCard* w, const Vec2& o, const Vec2& sz, float op) {
    const auto lines = w->Lines();
    if (lines.empty()) return;
    const float pad = 12.0f;
    const bool icon = w->showIcon;
    const float iconW = icon ? 46.0f : 0.0f;
    const float textX = o.x + pad + iconW + (icon ? 10.0f : 0.0f);
    const float textW = sz.x - (textX - o.x) - pad;
    struct Row { std::string text; float px; Color col; };
    std::vector<Row> rows;
    Color border = w->accent;
    for (const auto& l : lines) {
        const float px = l.name ? 2.0f : 1.5f;
        Color c = l.name ? (l.useTint ? l.tint : w->nameColor) : (l.useTint ? l.tint : Color::FromBytes(200, 205, 218));
        if (l.name && l.useTint) border = l.tint;
        for (const std::string& part : Wrap(l.text, textW, px)) rows.push_back({part, px, c});
    }
    float total = pad * 2.0f;
    for (const Row& r : rows) total += TextH(r.px) + 5.0f;
    if (icon) total = std::max(total, pad * 2.0f + iconW);
    Box(p, o.x, o.y, sz.x, total, w->background, 10.0f, op);
    Frame(p, o.x, o.y, sz.x, total, border, 10.0f, 2, op);
    if (icon) {
        const std::string id = ItemDatabase::Get().Resolve(w->CurrentItem());
        const ItemGlyph g = GlyphFor(id);
        const ItemIconRef ref = ItemIconOf("", id);
        const float ix = o.x + pad, iy = o.y + pad;
        Tex tex = ref.path.empty() ? nullptr : p.LoadTexture(ref.path);
        if (tex) {
            int tw = 0, th = 0; p.QueryTexture(tex, &tw, &th);
            Rect src{(int)(ref.u0 * (float)tw), (int)(ref.v0 * (float)th), std::max(1, (int)((ref.u1 - ref.u0) * (float)tw)), std::max(1, (int)((ref.v1 - ref.v0) * (float)th))};
            Rect dst{(int)ix, (int)iy, (int)iconW, (int)iconW};
            p.SetTextureAlphaMod(tex, (std::uint8_t)(255.0f * op));
            p.Copy(tex, ref.atlas ? &src : nullptr, &dst);
        } else {
            Box(p, ix, iy, iconW, iconW, g.bg, 8.0f, op);
            const float px = g.letters.size() > 1 ? 2.5f : 3.5f;
            Words(p, g.letters, ix + (iconW - UITextW(g.letters, px)) * 0.5f, iy + (iconW - TextH(px)) * 0.5f, px, g.fg, op);
        }
    }
    float y = o.y + pad;
    for (const Row& r : rows) { Words(p, r.text, textX, y, r.px, r.col, op); y += TextH(r.px) + 5.0f; }
}

// ---- ability icon ----
void DrawAbility(Painter& p, UIAbilityIcon* w, const Vec2& o, const Vec2& sz, float op) {
    auto* ring = w->gameObject->GetComponent<UIRadialProgress>();
    const float R = std::min(sz.x, sz.y) * 0.5f;
    const float t = ring ? std::max(0.0f, ring->thickness) : 0.0f;
    const float cx = o.x + sz.x * 0.5f, cy = o.y + sz.y * 0.5f;
    const float r = std::max(4.0f, R - t - 3.0f);
    const bool ready = w->Ready();
    const Color tile = ready ? w->iconColor : Color::FromBytes((int)(w->iconColor.r * 120), (int)(w->iconColor.g * 120), (int)(w->iconColor.b * 120), 255);
    MMFillCircle(p, (int)cx, (int)cy, (int)r, Ink(tile, op));
    Tex tex = w->iconPath.empty() ? nullptr : p.LoadTexture(w->iconPath);
    if (tex) {
        Rect dst{(int)(cx - r * 0.75f), (int)(cy - r * 0.75f), (int)(r * 1.5f), (int)(r * 1.5f)};
        p.SetTextureAlphaMod(tex, (std::uint8_t)(255.0f * op * (ready ? 1.0f : 0.5f)));
        p.Copy(tex, nullptr, &dst);
    } else {
        const float px = r > 22.0f ? 3.0f : 2.0f;
        Words(p, w->label, cx - UITextW(w->label, px) * 0.5f, cy - TextH(px) * 0.5f, px, Color::White, op * (ready ? 1.0f : 0.6f));
    }
    if (w->Remaining() > 0.0f && w->Charges() == 0) {
        char b[16];
        if (w->Remaining() < 10.0f) std::snprintf(b, sizeof(b), "%.1f", w->Remaining()); else std::snprintf(b, sizeof(b), "%.0f", w->Remaining());
        Words(p, b, cx - UITextW(b, 2.5f) * 0.5f, cy - TextH(2.5f) * 0.5f, 2.5f, Color::White, op);
    }
    if (!w->keyText.empty()) {
        const float kw = UITextW(w->keyText, 1.5f) + 10.0f;
        Box(p, o.x + sz.x - kw - 1.0f, o.y + sz.y - 17.0f, kw, 16.0f, Color::FromBytes(10, 12, 18, 230), 4.0f, op);
        Words(p, w->keyText, o.x + sz.x - kw + 4.0f, o.y + sz.y - 17.0f + 3.0f, 1.5f, Color::White, op);
    }
    if (w->maxCharges > 1) {
        const std::string c = std::to_string(w->Charges());
        MMFillCircle(p, (int)(o.x + 9.0f), (int)(o.y + 9.0f), 9, Ink(w->readyColor, op));
        Words(p, c, o.x + 9.0f - UITextW(c, 1.5f) * 0.5f, o.y + 9.0f - TextH(1.5f) * 0.5f, 1.5f, Color::FromBytes(10, 12, 18), op);
    }
}

} // namespace

bool DrawGameHud(GameObject* go, Painter& p, float logicalW, float logicalH, float opacity) {
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
    if (auto* w = go->GetComponent<UIRadialMenu>()) DrawRadial(p, w, o, sz, op);
    if (auto* w = go->GetComponent<UIKillFeed>()) DrawKillFeed(p, w, o, sz, op);
    if (auto* w = go->GetComponent<UIAmmoCounter>()) DrawAmmo(p, w, o, sz, op);
    if (auto* w = go->GetComponent<UIInteractionPrompt>()) DrawPrompt(p, w, o, sz, op);
    if (auto* w = go->GetComponent<UIObjectiveTracker>()) DrawTracker(p, w, o, sz, op);
    if (auto* w = go->GetComponent<UICompass>()) DrawCompass(p, w, o, sz, op);
    if (auto* w = go->GetComponent<UIItemTooltipCard>()) DrawCard(p, w, o, sz, op);
    if (auto* w = go->GetComponent<UIAbilityIcon>()) DrawAbility(p, w, o, sz, op);
    return true;
}

} // namespace ui2d
} // namespace okay
