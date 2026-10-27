#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Components/ScriptComponent.hpp"
#include "okay/Components/UIAnchor.hpp"
#include "okay/Components/UIInputField.hpp"
#include "okay/Components/Canvas.hpp"
#include "okay/Components/UIWorldContext.hpp"   // UIToLocal, rectOf
#include "okay/Graphics/Font.hpp"
#include "okay/Graphics/TtfFont.hpp"
#include "okay/UI/UIShape.hpp"
#include "okay/Render/Color.hpp"
#include "okay/Math/Vec2.hpp"
#include "okay/Input/Input.hpp"
#include <algorithm>
#include <cmath>
#include <functional>
#include <string>
#include <vector>

namespace okay {

/// Text width callback for UITabs layout: (text, font pixel size) -> width in px.
/// Renderers pass the measure their DrawText uses; empty = the scene's UI font.
using UITabsMeasure = std::function<float(const std::string&, float)>;

/// Where every part of a tab bar sits, in bar-local pixels (origin = the bar's
/// top-left on screen, already multiplied by the draw scale). Built by
/// UITabs::Layout and shared by drawing AND hit-testing, so a click always lands
/// on the tab that is drawn under the pointer.
struct UITabsLayout {
    struct Tab {
        float x = 0, y = 0, w = 0, h = 0;          // the tab's box (scrolled)
        std::string label;                          // text to draw ("..." when shortened)
        float textX = 0, textY = 0, textW = 0;      // label pen (top-left) and width
        float iconX = 0, iconY = 0, iconSize = 0;   // iconSize 0 = no icon
        float badgeX = 0, badgeY = 0, badgeW = 0, badgeH = 0;   // badgeW 0 = no badge
        std::string badgeText;                      // "" = a plain dot
        float badgePx = 0;                          // badge number font pixel size
        float closeX = 0, closeY = 0, closeW = 0, closeH = 0;   // closeW 0 = no close box
        bool  visible = true;                       // false when scrolled out of view
    };
    std::vector<Tab> tabs;
    float w = 0, h = 0;                    // the whole bar
    float k = 1.0f;                        // draw scale (Canvas factor)
    float textPx = 2.0f;                   // label font pixel size
    bool  vertical = false;
    // The strip tabs are clipped to (the bar minus the overflow arrow buttons).
    float viewX = 0, viewY = 0, viewW = 0, viewH = 0;
    bool  overflow = false;                // tabs don't fit: scrolling + arrows
    float scroll = 0, maxScroll = 0;       // px along the tab axis
    float prevX = 0, prevY = 0, prevW = 0, prevH = 0;   // arrow buttons (w 0 = none)
    float nextX = 0, nextY = 0, nextW = 0, nextH = 0;
    // The selection indicator's box at its (animated) position.
    float selX = 0, selY = 0, selW = 0, selH = 0;
};

/// One drawing step for a tab bar, in bar-local pixels. UITabs::Paint turns the
/// layout + style into a flat list of these so the editor preview and the game
/// draw exactly the same thing from one description.
struct UITabsDraw {
    enum class Kind { Fill, Text, Icon, Clip, Unclip };
    Kind  kind = Kind::Fill;
    float x = 0, y = 0, w = 0, h = 0;      // Fill/Icon/Clip rect; Text pen + width
    UIShape shape = UIShape::Rectangle;    // Fill
    float radius = 0.0f;                   // Fill corner radius
    int   cornerMask = UICornerAll;        // Fill
    Color color = Color::White;            // Fill colour, Text colour, Icon tint
    float px = 0.0f;                       // Text font pixel size
    std::string text;                      // Text string, or the Icon texture path
};

/// A tab bar: a row (or column) of tabs where exactly one is selected (settings
/// menus, inventories, difficulty pickers, view switches). Clicking a tab sets
/// `value` (the selected index) and calls the sibling ScriptComponent's
/// on_change() handler. Position/size are window pixels (origin top-left).
///
/// Looks (`style`): Segmented (the classic one-piece control, the default),
/// Underline (text tabs with a line under the selected one), Folder (browser
/// tabs joined to the page below), Pills (separate rounded buttons) and Vertical
/// (stacked down the side, text left-aligned). Tabs can share the width equally
/// or fit their text, carry an icon and a badge, be disabled, and scroll when
/// there are more than fit. Q / E (or the gamepad shoulders) and the menu-focus
/// arrows switch tabs from the keyboard.
class UITabs final : public Behaviour {
public:
    enum class Style  { Segmented, Underline, Folder, Pills, Vertical };
    enum class Sizing { Equal, FitText };

    Vec2 position{20.0f, 20.0f};
    Vec2 size{300.0f, 40.0f};
    std::vector<std::string> tabs{"One", "Two", "Three"};
    /// Optional content page per tab: the NAME of a GameObject to show when that tab
    /// is selected (all the others are hidden). Index-aligned with `tabs`; an empty
    /// entry (or a missing name) means "no page for this tab". This makes tabs work
    /// with zero scripting — assign a panel to each tab and switching just works.
    std::vector<std::string> pages;
    /// Optional per-tab extras, index-aligned with `tabs` (shorter = the rest have
    /// none). `icons`: a texture path drawn before the label. `badges`: 0 = none,
    /// -1 = a small dot, N > 0 = a number ("99+" above 99). `disabled`: true = the
    /// tab is dimmed and can't be selected (clicks and next/prev skip it).
    std::vector<std::string> icons;
    std::vector<int>         badges;
    std::vector<bool>        disabled;
    int  value = 0;                                  // selected tab index
    Color background        = Color::FromBytes(23, 25, 31);   // track / unselected tab
    Color selected          = Color::FromBytes(66, 150, 250); // selected tab / indicator
    Color textColor         = Color::FromBytes(200, 205, 215);
    Color selectedTextColor = Color::White;
    UIAnchor anchor = UIAnchor::TopLeft;
    // Outer silhouette (Pill = classic segmented control); cornerRadius for Rounded.
    UIShape shape = UIShape::Pill;
    float cornerRadius = 8.0f;
    bool  interactable = true;
    // grok/ui-controls — off by default, so tabs have no close box and layout is unchanged.
    /// Draw an X on each tab. Clicking it removes that tab and fires on_tab_close.
    bool closable = false;
    void SetClosable(bool on) { closable = on; }
    bool Closable() const { return closable; }

    // ---- Look & layout -----------------------------------------------------------
    Style  style  = Style::Segmented;
    Sizing sizing = Sizing::Equal;
    float spacing = 0.0f;              // gap between tabs (px)
    float padding = 12.0f;             // space either side of a tab's content (px)
    float fontScale = 2.0f;            // label pixel size
    float minTabSize = 0.0f;           // a tab is never shorter than this (0 = no limit);
                                       // with Equal sizing this is what makes many tabs scroll
    float indicatorThickness = 3.0f;   // underline / folder baseline / sidebar accent (px)
    float iconSize = 16.0f;            // icon square (px)
    // Hover / pressed tints drawn over an unselected tab.
    Color hoverColor     = Color::FromBytes(255, 255, 255, 20);
    Color pressedColor   = Color::FromBytes(0, 0, 0, 50);
    Color badgeColor     = Color::FromBytes(235, 64, 52);
    Color badgeTextColor = Color::White;
    // The selection slides to the new tab (a short ease); off = it jumps.
    bool  animate = true;
    float animSpeed = 14.0f;

    // ---- Keyboard / gamepad --------------------------------------------------------
    /// Previous / next tab keys (key names: "q", "e", "tab", "1"...) and the gamepad
    /// shoulder buttons (L / R), while the bar is on screen. Older scenes load with
    /// this off so their keys keep doing what they did.
    bool hotkeys = true;
    std::string prevKey = "q";
    std::string nextKey = "e";
    bool wrapAround = false;           // next on the last tab goes to the first

    // Keyboard/gamepad menu focus (driven by NavigateUI: Left/Right switch tabs).
    bool focusable = true;
    bool IsFocused() const { return m_focused; }
    void SetFocused(bool f) { m_focused = f; }

    int Count() const { return (int)tabs.size(); }

    // ---- Per-tab extras --------------------------------------------------------------
    const std::string& IconOf(int i) const {
        static const std::string none;
        return (i >= 0 && i < (int)icons.size()) ? icons[i] : none;
    }
    int  BadgeOf(int i) const { return (i >= 0 && i < (int)badges.size()) ? badges[i] : 0; }
    bool IsTabEnabled(int i) const {
        return i >= 0 && i < Count() && !(i < (int)disabled.size() && disabled[i]);
    }
    void SetIcon(int i, const std::string& path) {
        if (i < 0 || i >= Count()) return;
        if ((int)icons.size() <= i) icons.resize(i + 1);
        icons[i] = path;
    }
    void SetBadge(int i, int n) {
        if (i < 0 || i >= Count()) return;
        if ((int)badges.size() <= i) badges.resize(i + 1, 0);
        badges[i] = n;
    }
    void SetTabEnabled(int i, bool on) {
        if (i < 0 || i >= Count()) return;
        if ((int)disabled.size() <= i) disabled.resize(i + 1, false);
        disabled[i] = !on;
    }
    /// The selected tab's label ("" when there are no tabs).
    std::string SelectedLabel() const {
        return (value >= 0 && value < Count()) ? tabs[value] : std::string{};
    }

    /// Width of `text` at font pixel size `px` in the font the renderers draw labels
    /// with (the scene's UI font, else the 8x8 bitmap font).
    float MeasureText(const std::string& text, float px) const {
        Scene* s = gameObject ? gameObject->scene() : nullptr;
        TtfFont* f = s ? SceneUIFont(s->uiFont) : nullptr;
        if (f && f->Valid()) return f->Measure(text.c_str(), (float)Font8x8::Height) * px;
        return (float)text.size() * (Font8x8::Width + 1) * px;
    }

    /// Lay the bar out in a `W` x `H` pixel box drawn at scale `k` (the Canvas
    /// factor: paddings, gaps, fonts and icons grow with it). Everything is in
    /// bar-local pixels. The single source of truth for drawing and hit-testing.
    UITabsLayout Layout(float W, float H, float k = 1.0f, const UITabsMeasure& measure = {}) const {
        UITabsLayout L;
        const int n = Count();
        if (!(k > 1e-4f)) k = 1.0f;
        L.w = W; L.h = H; L.k = k;
        L.vertical = (style == Style::Vertical);
        L.textPx = fontScale * k;
        L.viewW = W; L.viewH = H;
        if (n <= 0 || W <= 0.0f || H <= 0.0f) return L;
        auto textW = [&](const std::string& s, float px) {
            return measure ? measure(s, px) : MeasureText(s, px);
        };
        const bool vert = L.vertical;
        const float pad = padding * k, gap = spacing * k;
        const float textH = Font8x8::Height * L.textPx;
        const float mainLen = vert ? H : W, cross = vert ? W : H;

        // Per-tab decoration sizes, and the length each tab would like.
        std::vector<float> iconW(n), badgeW(n), badgeH(n), len(n);
        std::vector<std::string> badgeTxt(n);
        const float badgePx = L.textPx * 0.7f;
        for (int i = 0; i < n; ++i) {
            iconW[i] = IconOf(i).empty() ? 0.0f : iconSize * k;
            const int b = BadgeOf(i);
            if (b < 0) { badgeW[i] = badgeH[i] = 8.0f * k; }
            else if (b > 0) {
                badgeTxt[i] = b > 99 ? std::string("99+") : std::to_string(b);
                badgeH[i] = Font8x8::Height * badgePx + 4.0f * k;
                badgeW[i] = std::max(badgeH[i], textW(badgeTxt[i], badgePx) + 6.0f * k);
            }
            const float iconPart  = iconW[i] > 0.0f ? iconW[i] + 6.0f * k : 0.0f;
            const float badgePart = badgeW[i] > 0.0f ? badgeW[i] + 4.0f * k : 0.0f;
            len[i] = vert ? std::max(std::max(textH, iconW[i]), badgeH[i]) + pad
                          : pad * 2.0f + iconPart + textW(tabs[i], L.textPx) + badgePart;
            if (closable) len[i] += 20.0f * k;   // room for the X; Equal sizing overwrites this
        }
        const float minLen = minTabSize * k;
        if (sizing == Sizing::Equal) {
            float each = (mainLen - gap * (n - 1)) / (float)n;
            if (each < minLen) each = minLen;
            for (float& l : len) l = each;
        } else {
            for (float& l : len) if (l < minLen) l = minLen;
        }
        float content = gap * (n - 1);
        for (float l : len) content += l;

        // Overflow: arrow buttons at both ends, the strip between them scrolls.
        L.overflow = content > mainLen + 0.5f;
        const float arrow = L.overflow ? std::min(cross, 22.0f * k) : 0.0f;
        const float viewLen = std::max(0.0f, mainLen - arrow * 2.0f);
        L.maxScroll = L.overflow ? std::max(0.0f, content - viewLen) : 0.0f;
        L.scroll = std::min(std::max(m_scroll * k, 0.0f), L.maxScroll);
        if (vert) {
            L.viewX = 0; L.viewY = arrow; L.viewW = W; L.viewH = viewLen;
            if (L.overflow) {
                L.prevX = 0; L.prevY = 0;          L.prevW = W; L.prevH = arrow;
                L.nextX = 0; L.nextY = H - arrow;  L.nextW = W; L.nextH = arrow;
            }
        } else {
            L.viewX = arrow; L.viewY = 0; L.viewW = viewLen; L.viewH = H;
            if (L.overflow) {
                L.prevX = 0;         L.prevY = 0; L.prevW = arrow; L.prevH = H;
                L.nextX = W - arrow; L.nextY = 0; L.nextW = arrow; L.nextH = H;
            }
        }

        // Place each tab along the axis, then its icon / label / badge inside it.
        L.tabs.resize(n);
        float at = arrow - L.scroll;
        for (int i = 0; i < n; ++i) {
            UITabsLayout::Tab& t = L.tabs[i];
            if (vert) { t.x = 0; t.y = at; t.w = W; t.h = len[i]; }
            else      { t.x = at; t.y = 0; t.w = len[i]; t.h = H; }
            at += len[i] + gap;
            const float a0 = vert ? t.y : t.x, a1 = a0 + len[i];
            const float v0 = arrow, v1 = arrow + viewLen;
            t.visible = !L.overflow || (a1 > v0 && a0 < v1);

            const float iconPart  = iconW[i] > 0.0f ? iconW[i] + 6.0f * k : 0.0f;
            const float badgePart = badgeW[i] > 0.0f ? badgeW[i] + 4.0f * k : 0.0f;
            // Labels never spill into the next tab: shorten with "..." to fit. Equal
            // tabs keep only a small edge margin (padding is for fitting the text), so
            // a label that used to fit its segment still shows in full.
            const float edge = (sizing == Sizing::Equal && !vert) ? std::min(pad, 4.0f * k) : pad;
            const float gutter = closable ? 20.0f * k : 0.0f;
            const float avail = t.w - edge * 2.0f - iconPart - badgePart - gutter;
            t.label = FitLabel(tabs[i], avail, L.textPx, textW);
            t.textW = t.label.empty() ? 0.0f : textW(t.label, L.textPx);
            // Horizontal tabs centre their content; vertical ones read left-aligned.
            const float group = iconPart + t.textW + badgePart;
            const float start = vert ? t.x + pad : t.x + std::max(edge, (t.w - group) * 0.5f);
            if (iconW[i] > 0.0f) {
                t.iconSize = iconW[i];
                t.iconX = start; t.iconY = t.y + (t.h - iconW[i]) * 0.5f;
            }
            t.textX = start + iconPart;
            t.textY = t.y + (t.h - textH) * 0.5f;
            if (badgeW[i] > 0.0f) {
                t.badgeW = badgeW[i]; t.badgeH = badgeH[i];
                t.badgeText = badgeTxt[i]; t.badgePx = badgePx;
                t.badgeX = vert ? t.x + t.w - pad - badgeW[i] : t.textX + t.textW + 4.0f * k;
                // A dot sits at the label's top-right corner; a number is centred.
                t.badgeY = (BadgeOf(i) < 0 && !vert) ? t.textY - 3.0f * k
                                                      : t.y + (t.h - badgeH[i]) * 0.5f;
            }
            if (closable) {
                const float cs = std::min(16.0f * k, t.h * 0.55f);
                t.closeW = t.closeH = cs;
                t.closeX = t.x + t.w - 8.0f * k - cs;
                t.closeY = t.y + (t.h - cs) * 0.5f;
            }
        }

        // The selection indicator, between two tabs while it slides.
        float pos = m_anim < 0.0f ? (float)ClampIndex(value) : m_anim;
        pos = std::min(std::max(pos, 0.0f), (float)(n - 1));
        const int a = (int)std::floor(pos), b = std::min(a + 1, n - 1);
        const float f = pos - (float)a;
        const UITabsLayout::Tab& ta = L.tabs[a];
        const UITabsLayout::Tab& tb = L.tabs[b];
        L.selX = ta.x + (tb.x - ta.x) * f; L.selY = ta.y + (tb.y - ta.y) * f;
        L.selW = ta.w + (tb.w - ta.w) * f; L.selH = ta.h + (tb.h - ta.h) * f;
        return L;
    }

    /// Hit-test results besides a tab index.
    static constexpr int kNoHit = -1, kPrevArrow = -2, kNextArrow = -3, kClose = -100;

    /// What a bar-local point lands on: a tab index, kPrevArrow / kNextArrow, or
    /// kNoHit (outside, in a gap, or on a tab scrolled out of view).
    static int HitTest(const UITabsLayout& L, float x, float y) {
        if (x < 0 || y < 0 || x >= L.w || y >= L.h) return kNoHit;
        auto in = [&](float rx, float ry, float rw, float rh) {
            return rw > 0.0f && x >= rx && x < rx + rw && y >= ry && y < ry + rh;
        };
        if (L.overflow) {
            if (in(L.prevX, L.prevY, L.prevW, L.prevH)) return kPrevArrow;
            if (in(L.nextX, L.nextY, L.nextW, L.nextH)) return kNextArrow;
            if (!in(L.viewX, L.viewY, L.viewW, L.viewH)) return kNoHit;
        }
        for (int i = 0; i < (int)L.tabs.size(); ++i) {
            const UITabsLayout::Tab& t = L.tabs[i];
            if (t.closeW > 0.0f && in(t.closeX, t.closeY, t.closeW, t.closeH)) return kClose - i;
            if (in(t.x, t.y, t.w, t.h)) return i;
        }
        return kNoHit;
    }

    /// The drawing steps for a `W` x `H` bar at scale `k` (see UITabsDraw). Both
    /// renderers replay this list, so the editor and the game look identical.
    std::vector<UITabsDraw> Paint(float W, float H, float k = 1.0f, const UITabsMeasure& measure = {}) const {
        std::vector<UITabsDraw> out;
        const UITabsLayout L = Layout(W, H, k, measure);
        const int n = (int)L.tabs.size();
        if (n == 0) return out;
        k = L.k;
        auto fill = [&](float x, float y, float w, float h, UIShape s, float r, const Color& c,
                        int mask = UICornerAll) {
            if (w <= 0.0f || h <= 0.0f || c.a <= 0.001f) return;
            UITabsDraw d; d.kind = UITabsDraw::Kind::Fill;
            d.x = x; d.y = y; d.w = w; d.h = h; d.shape = s; d.radius = r; d.cornerMask = mask; d.color = c;
            out.push_back(std::move(d));
        };
        auto mark = [&](UITabsDraw::Kind kind, float x, float y, float w, float h) {
            UITabsDraw d; d.kind = kind; d.x = x; d.y = y; d.w = w; d.h = h; out.push_back(std::move(d));
        };
        const int sel = ClampIndex(value);
        const float line = std::max(1.0f, indicatorThickness * k);
        const float lift = 4.0f * k;                       // Folder: unselected tabs sit lower
        // A tall sidebar reads badly as a capsule: vertical tabs use rounded corners.
        const UIShape barShape = (style == Style::Vertical && shape == UIShape::Pill) ? UIShape::Rounded : shape;
        const float inset = 2.0f;                          // Segmented / Vertical highlight inset

        // 1. Track.
        if (style == Style::Folder) fill(0, H - line, W, line, UIShape::Rectangle, 0, selected);
        else if (style != Style::Pills) fill(0, 0, W, H, barShape, cornerRadius, background);
        if (style == Style::Underline)                     // faint baseline the indicator rides on
            fill(L.viewX, H - std::max(1.0f, k), L.viewW, std::max(1.0f, k), UIShape::Rectangle, 0,
                 Color{textColor.r, textColor.g, textColor.b, textColor.a * 0.18f});
        if (L.overflow) mark(UITabsDraw::Kind::Clip, L.viewX, L.viewY, L.viewW, L.viewH);

        // 2. Tab bodies + hover / pressed tints (unselected tabs).
        for (int i = 0; i < n; ++i) {
            const UITabsLayout::Tab& t = L.tabs[i];
            if (!t.visible) continue;
            const bool on = (i == sel);
            const bool tint = !on && IsTabEnabled(i) && interactable && (i == m_hoverTab || i == m_pressTab);
            const Color& tc = (i == m_pressTab && m_mouseHeld) ? pressedColor : hoverColor;
            switch (style) {
                case Style::Segmented:
                    if (tint) fill(t.x + inset, t.y + inset, t.w - inset * 2, t.h - inset * 2, shape, cornerRadius, tc);
                    break;
                case Style::Vertical:
                    if (tint) fill(t.x + inset, t.y + inset, t.w - inset * 2, t.h - inset * 2, UIShape::Rounded, cornerRadius, tc);
                    break;
                case Style::Underline:
                    if (tint) fill(t.x, t.y, t.w, t.h - line, UIShape::Rounded, cornerRadius * 0.5f, tc);
                    break;
                case Style::Folder:
                    fill(t.x, t.y + lift, t.w, t.h - lift - line, UIShape::TabTop, cornerRadius, background);
                    if (tint) fill(t.x, t.y + lift, t.w, t.h - lift - line, UIShape::TabTop, cornerRadius, tc);
                    break;
                case Style::Pills:
                    fill(t.x, t.y, t.w, t.h, UIShape::Pill, 0, background);
                    if (tint) fill(t.x, t.y, t.w, t.h, UIShape::Pill, 0, tc);
                    break;
            }
        }

        // 3. The selection (at its sliding position).
        switch (style) {
            case Style::Segmented:
                fill(L.selX + inset, L.selY + inset, L.selW - inset * 2, L.selH - inset * 2, shape, cornerRadius, selected);
                break;
            case Style::Underline:
                fill(L.selX, H - line, L.selW, line, UIShape::Rounded, line * 0.5f, selected);
                break;
            case Style::Folder:
                fill(L.selX, L.selY, L.selW, L.selH, UIShape::TabTop, cornerRadius, selected);
                break;
            case Style::Pills:
                fill(L.selX, L.selY, L.selW, L.selH, UIShape::Pill, 0, selected);
                break;
            case Style::Vertical: {
                fill(L.selX + inset, L.selY + inset, L.selW - inset * 2, L.selH - inset * 2, UIShape::Rounded, cornerRadius, selected);
                const float ah = (L.selH - inset * 2) * 0.6f;
                fill(L.selX + inset, L.selY + (L.selH - ah) * 0.5f, line, ah, UIShape::Rounded, line * 0.5f, selectedTextColor);
                break;
            }
        }

        // 4. Icons, labels and badges.
        for (int i = 0; i < n; ++i) {
            const UITabsLayout::Tab& t = L.tabs[i];
            if (!t.visible) continue;
            const bool on = (i == sel);
            const float dim = IsTabEnabled(i) ? 1.0f : 0.35f;
            // Folder: an unselected tab's content centres in its lowered body.
            const float dy = (style == Style::Folder && !on) ? (lift - line) * 0.5f : 0.0f;
            if (t.iconSize > 0.0f) {
                UITabsDraw d; d.kind = UITabsDraw::Kind::Icon;
                d.x = t.iconX; d.y = t.iconY + dy; d.w = d.h = t.iconSize;
                d.color = Color{1.0f, 1.0f, 1.0f, dim}; d.text = IconOf(i);
                out.push_back(std::move(d));
            }
            if (!t.label.empty()) {
                const Color& base = on ? selectedTextColor : textColor;
                UITabsDraw d; d.kind = UITabsDraw::Kind::Text;
                d.x = t.textX; d.y = t.textY + dy; d.w = t.textW; d.h = Font8x8::Height * L.textPx;
                d.px = L.textPx; d.color = Color{base.r, base.g, base.b, base.a * dim}; d.text = t.label;
                out.push_back(std::move(d));
            }
            if (closable && t.closeW > 0.0f) {
                const std::string mark = "x";
                const float xw = measure ? measure(mark, L.textPx) : MeasureText(mark, L.textPx);
                const Color& base = on ? selectedTextColor : textColor;
                UITabsDraw d; d.kind = UITabsDraw::Kind::Text;
                d.x = t.closeX + (t.closeW - xw) * 0.5f;
                d.y = t.closeY + (t.closeH - Font8x8::Height * L.textPx) * 0.5f;
                d.w = xw; d.h = Font8x8::Height * L.textPx; d.px = L.textPx;
                d.color = Color{base.r, base.g, base.b, base.a * dim}; d.text = mark;
                out.push_back(std::move(d));
            }
            if (t.badgeW > 0.0f) {
                Color bc = badgeColor; bc.a *= dim;
                if (t.badgeText.empty()) {
                    fill(t.badgeX, t.badgeY + dy, t.badgeW, t.badgeH, UIShape::Circle, 0, bc);
                } else {
                    fill(t.badgeX, t.badgeY + dy, t.badgeW, t.badgeH, UIShape::Pill, 0, bc);
                    const float bw = measure ? measure(t.badgeText, t.badgePx) : MeasureText(t.badgeText, t.badgePx);
                    UITabsDraw d; d.kind = UITabsDraw::Kind::Text;
                    d.x = t.badgeX + (t.badgeW - bw) * 0.5f;
                    d.y = t.badgeY + dy + (t.badgeH - Font8x8::Height * t.badgePx) * 0.5f;
                    d.w = bw; d.h = Font8x8::Height * t.badgePx; d.px = t.badgePx;
                    d.color = Color{badgeTextColor.r, badgeTextColor.g, badgeTextColor.b, badgeTextColor.a * dim};
                    d.text = t.badgeText;
                    out.push_back(std::move(d));
                }
            }
        }

        // 5. Overflow arrows (dimmed at the ends of the scroll range).
        if (L.overflow) {
            mark(UITabsDraw::Kind::Unclip, 0, 0, 0, 0);
            Color abg = background; if (abg.a < 0.6f) abg.a = 0.6f;
            const float s = std::min(10.0f * k, std::min(L.prevW, L.prevH) * 0.6f);
            auto arrowBtn = [&](float x, float y, float w, float h, UIShape s0, bool live) {
                fill(x, y, w, h, UIShape::Rectangle, 0, abg);
                Color ac = textColor; if (!live) ac.a *= 0.3f;
                fill(x + (w - s) * 0.5f, y + (h - s) * 0.5f, s, s, s0, 0, ac);
            };
            arrowBtn(L.prevX, L.prevY, L.prevW, L.prevH, L.vertical ? UIShape::ArrowUp : UIShape::ArrowLeft,
                     L.scroll > 0.5f);
            arrowBtn(L.nextX, L.nextY, L.nextW, L.nextH, L.vertical ? UIShape::ArrowDown : UIShape::ArrowRight,
                     L.scroll < L.maxScroll - 0.5f);
        }

        // 6. Greyed out when not interactable.
        if (!interactable) fill(0, 0, W, H, barShape, cornerRadius, Color::FromBytes(30, 30, 35, 150));
        return out;
    }

    /// Which tab a local x (pixels from the bar's left edge, at scale 1) falls in, or
    /// -1. Kept for scripts/tests written against the classic segmented control.
    int SegmentAt(float localX) const {
        if (Count() <= 0 || size.x <= 0.0f) return -1;
        int i = HitTest(Layout(size.x, size.y), localX, size.y * 0.5f);
        return i >= 0 ? i : -1;
    }

    /// The pixel rect (relative to the bar's top-left, at scale 1) of tab `i`.
    void SegmentRect(int i, float& ox, float& oy, float& w, float& h) const {
        UITabsLayout L = Layout(size.x, size.y);
        if (i < 0 || i >= (int)L.tabs.size()) { ox = oy = w = h = 0.0f; return; }
        ox = L.tabs[i].x; oy = L.tabs[i].y; w = L.tabs[i].w; h = L.tabs[i].h;
    }

    /// A screen point mapped into this widget's own frame (where it is really drawn:
    /// inside its UI parent, at the Canvas scale). See UIToLocal.
    Vec2 ToLocal(const Vec2& p) const { return UIToLocal(gameObject, ResolveAnchor(anchor, position, size), size, p); }
    bool Contains(const Vec2& sp) const {
        Vec2 o, sz; float k;
        if (!ScreenFrame(o, sz, k)) return false;
        return sp.x >= o.x && sp.y >= o.y && sp.x <= o.x + sz.x && sp.y <= o.y + sz.y;
    }

    /// Where the bar is on screen right now (top-left + size, px) and the scale it
    /// is drawn at. Uses the host's rectOf (inside UI parents, at the Canvas scale,
    /// stretch anchors resolved), else the bare anchor against the UI canvas.
    bool ScreenFrame(Vec2& o, Vec2& sz, float& k) const {
        k = 1.0f;
        if (gameObject && UIWorld().rectOf) {
            if (!UIWorld().rectOf(gameObject, o, sz)) return false;
            // The scale is the drawn / authored ratio along an axis the anchor
            // doesn't stretch (a stretched axis holds margins, not a length).
            switch (anchor) {
                case UIAnchor::StretchHorizontal: k = size.y > 1e-4f ? sz.y / size.y : 1.0f; break;
                case UIAnchor::StretchVertical:   k = size.x > 1e-4f ? sz.x / size.x : 1.0f; break;
                case UIAnchor::StretchFull:       k = CanvasScale(); break;
                default:                          k = size.y > 1e-4f ? sz.y / size.y : 1.0f; break;
            }
            if (!(k > 1e-4f)) k = 1.0f;
            return true;
        }
        ResolveAnchorRect(anchor, position, size, UICanvas::Width(), UICanvas::Height(), o, sz);
        return true;
    }

    /// Show the selected tab's content page and hide every other tab's page (by
    /// GameObject name). Safe to call every frame; no-op for empty entries. A page
    /// named by several tabs stays shown while any of them is selected. This is
    /// what makes tab switching work without any script.
    void ApplyPages() {
        if (!gameObject) return;
        Scene* s = gameObject->scene();
        if (!s) return;
        const std::string& shown = (value >= 0 && value < (int)pages.size()) ? pages[value] : std::string();
        for (int i = 0; i < (int)pages.size(); ++i) {
            if (pages[i].empty()) continue;
            if (GameObject* g = s->Find(pages[i])) g->active = (pages[i] == shown);
        }
    }

    /// Select a tab and fire on_change (clamped to a valid index). A disabled tab
    /// can't be selected.
    void Select(int index) {
        int n = Count();
        if (n <= 0) return;
        index = ClampIndex(index);
        if (index == value || !IsTabEnabled(index)) return;
        value = index;
        ApplyPages();
        m_appliedValue = value;
        if (gameObject)
            if (auto* sc = gameObject->GetComponent<ScriptComponent>())
                if (sc->VM()) sc->VM()->CallEvent("on_change");
    }

    /// Step to the next (+1) / previous (-1) enabled tab, skipping disabled ones.
    /// Stops at the ends unless wrapAround. Returns true if the selection moved.
    bool Step(int dir) {
        const int n = Count();
        if (n <= 0 || dir == 0) return false;
        dir = dir > 0 ? 1 : -1;
        int i = ClampIndex(value);
        for (int tries = 0; tries < n; ++tries) {
            i += dir;
            if (i < 0 || i >= n) {
                if (!wrapAround) return false;
                i = (i + n) % n;
            }
            if (i == value) return false;
            if (IsTabEnabled(i)) { Select(i); return value == i; }
        }
        return false;
    }
    bool SelectNext()     { return Step(+1); }
    bool SelectPrevious() { return Step(-1); }

    /// Remove tab `index` and fire on_tab_close. The selected index stays on the same
    /// tab when one before it closes, and clamps when the selected tab itself closes.
    void CloseTab(int index) {
        if (index < 0 || index >= Count()) return;
        auto drop = [&](auto& v) { if (index < (int)v.size()) v.erase(v.begin() + index); };
        tabs.erase(tabs.begin() + index);
        drop(pages); drop(icons); drop(badges); drop(disabled);
        if (Count() <= 0) value = 0;
        else if (value > index) --value;
        else if (value >= Count()) value = Count() - 1;
        m_revealed = -1;
        ApplyPages();
        m_appliedValue = value;
        if (gameObject)
            if (auto* sc = gameObject->GetComponent<ScriptComponent>())
                if (sc->VM()) sc->VM()->CallEvent("on_tab_close");
    }

    bool IsHovered() const { return m_hover; }
    /// The tab under the pointer (-1 = none / disabled) and the tab held down.
    int HoveredTab() const { return m_hoverTab; }
    int PressedTab() const { return m_pressTab; }
    /// The selection indicator's (animated) position as a fractional tab index.
    float IndicatorPosition() const { return m_anim < 0.0f ? (float)ClampIndex(value) : m_anim; }
    /// Scroll offset of an overflowing bar (authored px) and setting it.
    float Scroll() const { return m_scroll; }
    void SetScroll(float s) { m_scroll = s < 0.0f ? 0.0f : s; }

    void Update(float dt) override {
        const int n = Count();
        // A tab removed under the selection: keep a valid index (no on_change).
        if (n > 0 && (value < 0 || value >= n)) value = ClampIndex(value);
        // Keep the visible page in sync with the selection (also applies the initial
        // state on the first frame, and after the value is set externally / by script).
        if (m_appliedValue != value) { ApplyPages(); m_appliedValue = value; }
        // Ease the indicator toward the selected tab.
        if (m_anim < 0.0f || !animate || animSpeed <= 0.0f || n <= 0) m_anim = (float)ClampIndex(value);
        else {
            float t = animSpeed * dt; if (t > 1.0f) t = 1.0f;
            m_anim += ((float)value - m_anim) * t;
            if (std::fabs((float)value - m_anim) < 0.002f) m_anim = (float)value;
        }
        m_hoverTab = -1;
        m_mouseHeld = Input::GetMouseButton(0);
        if (!interactable || n <= 0) { m_hover = false; m_pressTab = -1; m_dragging = m_pressing = false; return; }

        // Keyboard / gamepad: previous / next tab while the bar is on screen (not
        // while the player is typing into a text field).
        if (hotkeys) {
            const bool prev = KeyDown(prevKey) || Input::GetGamepadButtonDown(6);
            const bool next = KeyDown(nextKey) || Input::GetGamepadButtonDown(7);
            if ((prev || next) && OnScreen() && !TypingInField()) {
                if (prev) SelectPrevious(); else SelectNext();
            }
        }

        Vec2 o, sz; float k;
        if (!ScreenFrame(o, sz, k)) { m_hover = false; m_pressTab = -1; m_dragging = m_pressing = false; return; }
        UITabsLayout L = Layout(sz.x, sz.y, k);
        // Bring a newly selected tab into view (clicks, keys, scripts alike).
        if (m_revealed != value) {
            m_revealed = value;
            if (L.overflow) { Reveal(L, ClampIndex(value)); L = Layout(sz.x, sz.y, k); }
        }

        const Vec2 m = Input::MousePosition();
        const float lx = m.x - o.x, ly = m.y - o.y;
        m_hover = lx >= 0.0f && ly >= 0.0f && lx <= sz.x && ly <= sz.y;
        const int hit = m_hover ? HitTest(L, lx, ly) : kNoHit;
        m_hoverTab = (hit >= 0 && IsTabEnabled(hit)) ? hit : -1;
        const float along = L.vertical ? m.y : m.x;

        if (m_hover && L.overflow) {                         // wheel scrolls the strip
            const float wh = Input::MouseWheel();
            if (wh != 0.0f) m_scroll -= wh * 40.0f;
        }
        if (m_hover && Input::GetMouseButtonDown(0)) {
            const float page = (L.vertical ? L.viewH : L.viewW) * 0.6f / k;
            if (closable && hit <= kClose) CloseTab(-hit - 100);
            else if (hit == kPrevArrow) m_scroll -= page;
            else if (hit == kNextArrow) m_scroll += page;
            else if (L.overflow) {
                // Overflowing: a press may be the start of a drag-scroll, so the tab
                // is chosen on release (if the pointer didn't travel).
                m_pressing = true; m_dragging = false;
                m_pressAlong = along; m_pressScroll = m_scroll;
                m_pressTab = m_hoverTab;
            } else if (hit >= 0) {
                Select(hit);                                // classic: select on press
            }
        }
        if (m_pressing) {
            if (Input::GetMouseButton(0)) {
                const float d = along - m_pressAlong;
                if (!m_dragging && std::fabs(d) > 4.0f * k) { m_dragging = true; m_pressTab = -1; }
                if (m_dragging) m_scroll = m_pressScroll - d / k;
            } else {
                if (!m_dragging && m_pressTab >= 0 && hit == m_pressTab) Select(m_pressTab);
                m_pressing = m_dragging = false; m_pressTab = -1;
            }
        }
        // Keep the scroll inside its range (it is stored in authored px).
        const float maxS = L.maxScroll / k;
        m_scroll = std::min(std::max(m_scroll, 0.0f), maxS);
        if (!L.overflow) m_scroll = 0.0f;
    }

private:
    int ClampIndex(int i) const {
        const int n = Count();
        return n <= 0 ? 0 : (i < 0 ? 0 : (i >= n ? n - 1 : i));
    }
    // Shorten `s` with "..." until it fits `avail` px (empty if nothing fits).
    static std::string FitLabel(const std::string& s, float avail, float px,
                                const std::function<float(const std::string&, float)>& width) {
        if (s.empty() || width(s, px) <= avail + 0.01f) return s;
        std::string cut = s;
        while (!cut.empty()) {
            cut.pop_back();
            while (!cut.empty() && cut.back() == ' ') cut.pop_back();
            if (width(cut + "...", px) <= avail + 0.01f) return cut + "...";
        }
        return width("...", px) <= avail + 0.01f ? std::string("...") : std::string();
    }
    // Scroll so tab `i` is fully inside the strip.
    void Reveal(const UITabsLayout& L, int i) {
        if (i < 0 || i >= (int)L.tabs.size() || L.k <= 0.0f) return;
        const UITabsLayout::Tab& t = L.tabs[i];
        const float viewStart = L.vertical ? L.viewY : L.viewX, viewLen = L.vertical ? L.viewH : L.viewW;
        const float u = (L.vertical ? t.y : t.x) + L.scroll - viewStart;   // unscrolled offset
        const float len = L.vertical ? t.h : t.w;
        float s = L.scroll;
        if (u < s) s = u;
        if (u + len > s + viewLen) s = u + len - viewLen;
        m_scroll = std::min(std::max(s, 0.0f), L.maxScroll) / L.k;
    }
    // The owning screen Canvas's scale (1 without one) — used for full-stretch bars.
    float CanvasScale() const {
        for (Transform* t = gameObject ? gameObject->transform : nullptr; t; t = t->Parent())
            if (t->gameObject)
                if (auto* cv = t->gameObject->GetComponent<Canvas>())
                    return cv->worldSpace ? 1.0f : cv->ScaleFactor(UICanvas::Width(), UICanvas::Height());
        return 1.0f;
    }
    // Whether the bar is actually showing: every ancestor active and no hidden Canvas.
    bool OnScreen() const {
        for (Transform* t = gameObject ? gameObject->transform : nullptr; t; t = t->Parent()) {
            if (!t->gameObject) continue;
            if (!t->gameObject->active) return false;
            if (auto* cv = t->gameObject->GetComponent<Canvas>()) if (!cv->visible) return false;
        }
        return true;
    }
    // A text field has keyboard focus, so letter keys are typing, not hotkeys.
    bool TypingInField() const {
        Scene* s = gameObject ? gameObject->scene() : nullptr;
        if (!s) return false;
        for (const auto& up : s->Objects())
            if (up->active) if (auto* in = up->GetComponent<UIInputField>()) if (in->focused) return true;
        return false;
    }
    static bool KeyDown(const std::string& name) {
        const char c = Input::KeyFromName(name);
        return c != 0 && Input::GetKeyDown(c);
    }

    bool  m_hover = false;
    bool  m_focused = false;
    int   m_appliedValue = -1;   // last value ApplyPages() reflected (-1 = not yet)
    int   m_hoverTab = -1;       // enabled tab under the pointer
    int   m_pressTab = -1;       // tab pressed (overflowing bars pick on release)
    bool  m_mouseHeld = false;
    float m_anim = -1.0f;        // indicator position (fractional index; -1 = snap)
    float m_scroll = 0.0f;       // overflow scroll, authored px
    int   m_revealed = -1;       // value last scrolled into view
    bool  m_pressing = false, m_dragging = false;
    float m_pressAlong = 0.0f, m_pressScroll = 0.0f;
};

} // namespace okay
