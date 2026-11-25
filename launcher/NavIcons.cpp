// The sidebar's line icons (NavIcons.hpp), drawn with the draw list so no icon font is needed. Moved out of
// launcher/main.cpp unchanged (docs/engine/modularization_plan.md, Part C section 8, batch L4).
#include "LauncherShared.hpp"
#include "NavIcons.hpp"
#include "PageLayout.hpp"
#include "OkayTheme.hpp"
#include "HierarchyPanelModel.hpp"   // HierWrapWords: the one word wrapper (the editor's empty-panel note uses it too)
#include <cfloat>
#include <cstring>

namespace okl {

// ---- Sidebar icons ---------------------------------------------------------
// One consistent set of 1.6px line icons (Lucide-like), drawn with the draw list
// so no icon font is needed. Ids 0..10 match the launcher tabs; the rest are
// sidebar extras.
void DrawNavIcon(ImDrawList* d, int id, ImVec2 c, ImU32 col) {
    const float r = 7.5f, t = 1.6f, PI = 3.14159265f;
    switch (id) {
    case 0:   // Create: plus in a rounded square
        d->AddRect(ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r), col, 3.5f, 0, t);
        d->AddLine(ImVec2(c.x - 3.6f, c.y), ImVec2(c.x + 3.6f, c.y), col, t);
        d->AddLine(ImVec2(c.x, c.y - 3.6f), ImVec2(c.x, c.y + 3.6f), col, t);
        break;
    case 1:   // Play: play button in a circle
        d->AddCircle(c, r, col, 28, t);
        d->AddTriangleFilled(ImVec2(c.x - 2.3f, c.y - 3.8f), ImVec2(c.x - 2.3f, c.y + 3.8f), ImVec2(c.x + 4.0f, c.y), col);
        break;
    case 2:   // Community: 2x2 grid
        for (int k = 0; k < 4; ++k) {
            float x = (k % 2) ? c.x + 1.2f : c.x - r, y = (k / 2) ? c.y + 1.2f : c.y - r;
            d->AddRect(ImVec2(x, y), ImVec2(x + r - 1.2f, y + r - 1.2f), col, 1.5f, 0, t);
        }
        break;
    case 3:   // Account: user
        d->AddCircle(ImVec2(c.x, c.y - 2.6f), 3.3f, col, 16, t);
        d->PathArcTo(ImVec2(c.x, c.y + 8.2f), 6.4f, PI + 0.32f, 2 * PI - 0.32f, 14);
        d->PathStroke(col, 0, t);
        break;
    case 4:   // Settings: gear
        d->AddCircle(c, 2.4f, col, 12, t);
        d->AddCircle(c, 5.4f, col, 20, t);
        for (int k = 0; k < 8; ++k) {
            float a = k * PI / 4;
            d->AddLine(ImVec2(c.x + std::cos(a) * 5.4f, c.y + std::sin(a) * 5.4f),
                       ImVec2(c.x + std::cos(a) * 7.8f, c.y + std::sin(a) * 7.8f), col, 2.2f);
        }
        break;
    case 5:   // Achievements: trophy
        d->PathLineTo(ImVec2(c.x - 4.6f, c.y - r));
        d->PathLineTo(ImVec2(c.x + 4.6f, c.y - r));
        d->PathArcTo(ImVec2(c.x, c.y - 2.6f), 4.6f, 0.0f, PI, 12);
        d->PathStroke(col, ImDrawFlags_Closed, t);
        d->PathArcTo(ImVec2(c.x - 4.6f, c.y - 4.6f), 2.4f, PI * 0.5f, PI * 1.5f, 8); d->PathStroke(col, 0, t);
        d->PathArcTo(ImVec2(c.x + 4.6f, c.y - 4.6f), 2.4f, -PI * 0.5f, PI * 0.5f, 8); d->PathStroke(col, 0, t);
        d->AddLine(ImVec2(c.x, c.y + 2.0f), ImVec2(c.x, c.y + 5.6f), col, t);
        d->AddLine(ImVec2(c.x - 3.8f, c.y + r - 0.6f), ImVec2(c.x + 3.8f, c.y + r - 0.6f), col, t);
        break;
    case 6:   // Friends: two users
        d->AddCircle(ImVec2(c.x - 2.4f, c.y - 2.8f), 3.0f, col, 16, t);
        d->PathArcTo(ImVec2(c.x - 2.4f, c.y + 7.8f), 5.8f, PI + 0.30f, 2 * PI - 0.30f, 12); d->PathStroke(col, 0, t);
        d->PathArcTo(ImVec2(c.x + 4.2f, c.y - 2.8f), 2.6f, -PI * 0.5f, PI * 0.5f, 8); d->PathStroke(col, 0, t);
        d->PathArcTo(ImVec2(c.x + 4.2f, c.y + 7.8f), 4.6f, PI * 1.55f, 2 * PI - 0.30f, 8); d->PathStroke(col, 0, t);
        break;
    case 7:   // Market: shopping bag
        d->AddRect(ImVec2(c.x - 6.2f, c.y - 3.0f), ImVec2(c.x + 6.2f, c.y + r), col, 2.0f, 0, t);
        d->PathArcTo(ImVec2(c.x, c.y - 3.0f), 3.2f, PI, 2 * PI, 10); d->PathStroke(col, 0, t);
        break;
    case 8:   // Servers: stacked server units
        for (int k = 0; k < 2; ++k) {
            float y0 = k ? c.y + 1.0f : c.y - r + 0.5f, y1 = k ? c.y + r - 0.5f : c.y - 1.0f;
            d->AddRect(ImVec2(c.x - r, y0), ImVec2(c.x + r, y1), col, 2.0f, 0, t);
            d->AddCircleFilled(ImVec2(c.x - 3.8f, (y0 + y1) * 0.5f), 1.2f, col, 8);
        }
        break;
    case 9:   // Forums: document with lines
        d->AddRect(ImVec2(c.x - 6.0f, c.y - r), ImVec2(c.x + 6.0f, c.y + r), col, 2.0f, 0, t);
        for (int k = 0; k < 3; ++k) {
            float y = c.y - 3.4f + k * 3.4f;
            d->AddLine(ImVec2(c.x - 3.2f, y), ImVec2(c.x + (k == 2 ? 1.0f : 3.2f), y), col, t);
        }
        break;
    case 15: {   // Admin: shield
        const ImVec2 pts[] = {ImVec2(c.x, c.y - r), ImVec2(c.x + r * 0.85f, c.y - r * 0.55f), ImVec2(c.x + r * 0.75f, c.y + r * 0.25f),
                              ImVec2(c.x, c.y + r), ImVec2(c.x - r * 0.75f, c.y + r * 0.25f), ImVec2(c.x - r * 0.85f, c.y - r * 0.55f)};
        d->AddPolyline(pts, 6, col, ImDrawFlags_Closed, t);
        d->AddLine(ImVec2(c.x - 3.0f, c.y), ImVec2(c.x - 0.5f, c.y + 2.5f), col, t);
        d->AddLine(ImVec2(c.x - 0.5f, c.y + 2.5f), ImVec2(c.x + 3.5f, c.y - 2.5f), col, t);
        break;
    }
    case 10:  // Downloads: arrow into a tray
        d->AddLine(ImVec2(c.x, c.y - r), ImVec2(c.x, c.y + 2.6f), col, t);
        d->AddLine(ImVec2(c.x - 3.6f, c.y - 1.0f), ImVec2(c.x, c.y + 2.6f), col, t);
        d->AddLine(ImVec2(c.x + 3.6f, c.y - 1.0f), ImVec2(c.x, c.y + 2.6f), col, t);
        d->PathLineTo(ImVec2(c.x - r, c.y + 2.2f)); d->PathLineTo(ImVec2(c.x - r, c.y + r));
        d->PathLineTo(ImVec2(c.x + r, c.y + r));    d->PathLineTo(ImVec2(c.x + r, c.y + 2.2f));
        d->PathStroke(col, 0, t);
        break;
    case kIcoAlert: {   // Crash reports: a warning triangle with "!"
        d->AddTriangle(ImVec2(c.x, c.y - r), ImVec2(c.x + r, c.y + r * 0.85f), ImVec2(c.x - r, c.y + r * 0.85f), col, t);
        d->AddLine(ImVec2(c.x, c.y - 2.6f), ImVec2(c.x, c.y + 2.0f), col, t);
        d->AddCircleFilled(ImVec2(c.x, c.y + 4.4f), 0.95f, col, 8);
        break;
    }
    case kIcoBell:
        d->PathArcTo(ImVec2(c.x, c.y - 1.2f), 5.0f, PI, 2 * PI, 12);
        d->PathLineTo(ImVec2(c.x + 5.0f, c.y + 3.2f)); d->PathLineTo(ImVec2(c.x + 6.6f, c.y + 5.0f));
        d->PathLineTo(ImVec2(c.x - 6.6f, c.y + 5.0f)); d->PathLineTo(ImVec2(c.x - 5.0f, c.y + 3.2f));
        d->PathStroke(col, ImDrawFlags_Closed, t);
        d->AddLine(ImVec2(c.x - 1.8f, c.y + 7.4f), ImVec2(c.x + 1.8f, c.y + 7.4f), col, t);
        break;
    case kIcoChat:
        d->PathArcTo(c, 6.8f, PI * 0.80f, PI * 2.62f, 26);
        d->PathLineTo(ImVec2(c.x - r, c.y + r));
        d->PathStroke(col, ImDrawFlags_Closed, t);
        break;
    case kIcoPanel:   // sidebar toggle: a panel with its left column marked
        d->AddRect(ImVec2(c.x - r, c.y - 6.5f), ImVec2(c.x + r, c.y + 6.5f), col, 2.5f, 0, t);
        d->AddLine(ImVec2(c.x - 2.2f, c.y - 6.5f), ImVec2(c.x - 2.2f, c.y + 6.5f), col, t);
        break;
    case kIcoSpark: {   // What's New: a four-point sparkle + a small one
        auto star = [&](ImVec2 o, float R, float k) {
            for (int q = 0; q < 8; ++q) {
                const float a = q * PI / 4 - PI / 2, rr = (q % 2) ? R * k : R;
                d->PathLineTo(ImVec2(o.x + std::cos(a) * rr, o.y + std::sin(a) * rr));
            }
            d->PathStroke(col, ImDrawFlags_Closed, t);
        };
        star(ImVec2(c.x - 1.2f, c.y + 1.2f), 6.4f, 0.32f);
        d->AddCircleFilled(ImVec2(c.x + 5.4f, c.y - 5.2f), 1.6f, col, 8);
        break;
    }
    case kIcoBulb:      // Tips: a light bulb
        d->PathArcTo(ImVec2(c.x, c.y - 2.2f), 5.2f, PI * 0.78f, PI * 2.22f, 20);
        d->PathLineTo(ImVec2(c.x + 2.2f, c.y + 4.0f)); d->PathLineTo(ImVec2(c.x - 2.2f, c.y + 4.0f));
        d->PathStroke(col, ImDrawFlags_Closed, t);
        d->AddLine(ImVec2(c.x - 2.2f, c.y + 6.6f), ImVec2(c.x + 2.2f, c.y + 6.6f), col, t);
        break;
    case kIcoGlobe:     // Multiplayer: a globe
        d->AddCircle(c, r, col, 28, t);
        d->AddLine(ImVec2(c.x - r, c.y), ImVec2(c.x + r, c.y), col, t);
        for (int k = 0; k <= 16; ++k)   // meridian: a narrow ellipse { const float a = -PI * 0.5f + PI * k / 8.0f; d->PathLineTo(ImVec2(c.x + std::cos(a) * r * 0.42f, c.y + std::sin(a) * r)); }
        d->PathStroke(col, 0, t);
        break;
    case kIcoRefresh: {
        d->PathArcTo(c, 5.6f, -PI * 0.35f, PI * 1.45f, 20); d->PathStroke(col, 0, t);
        ImVec2 e(c.x + std::cos(-PI * 0.35f) * 5.6f, c.y + std::sin(-PI * 0.35f) * 5.6f);
        d->AddTriangleFilled(ImVec2(e.x + 2.6f, e.y - 2.4f), ImVec2(e.x + 2.0f, e.y + 2.6f), ImVec2(e.x - 2.6f, e.y - 0.6f), col);
        break;
    }
    }
}

// Round icon button (Discord/Messenger-style row actions).
bool RoundIconButton(const char* id, int icon, float d, ImU32 bg, ImU32 bgHover, ImU32 fg, const char* tip) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool click = ImGui::InvisibleButton(id, ImVec2(d, d));
    bool hov = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 c(p.x + d * 0.5f, p.y + d * 0.5f);
    dl->AddCircleFilled(c, d * 0.5f, hov ? bgHover : bg, 28);
    switch (icon) {
    case kRibChat:  DrawNavIcon(dl, kIcoChat, c, fg); break;
    case kRibPlay:  dl->AddTriangleFilled(ImVec2(c.x - 3.5f, c.y - 5.5f), ImVec2(c.x - 3.5f, c.y + 5.5f), ImVec2(c.x + 5.5f, c.y), fg); break;
    case kRibCheck:
        dl->PathLineTo(ImVec2(c.x - 5.0f, c.y + 0.2f)); dl->PathLineTo(ImVec2(c.x - 1.5f, c.y + 4.0f));
        dl->PathLineTo(ImVec2(c.x + 5.5f, c.y - 4.5f)); dl->PathStroke(fg, 0, 2.2f);
        break;
    case kRibClose:
        dl->AddLine(ImVec2(c.x - 4.5f, c.y - 4.5f), ImVec2(c.x + 4.5f, c.y + 4.5f), fg, 2.0f);
        dl->AddLine(ImVec2(c.x + 4.5f, c.y - 4.5f), ImVec2(c.x - 4.5f, c.y + 4.5f), fg, 2.0f);
        break;
    case kRibMore:
        for (int k = -1; k <= 1; ++k) dl->AddCircleFilled(ImVec2(c.x + k * 5.0f, c.y), 1.8f, fg, 8);
        break;
    case kRibStar: case kRibStarOn: {   // a five-pointed star (wishlist)
        ImVec2 pts[10];
        for (int k = 0; k < 10; ++k) {
            const float ang = -1.5707963f + k * 0.6283185f, rad = (k % 2 == 0) ? 7.0f : 3.0f;
            pts[k] = ImVec2(c.x + std::cos(ang) * rad, c.y + 0.5f + std::sin(ang) * rad);
        }
        if (icon == kRibStarOn) for (int k = 0; k < 10; ++k) dl->AddTriangleFilled(ImVec2(c.x, c.y + 0.5f), pts[k], pts[(k + 1) % 10], IM_COL32(240, 196, 92, 255));
        else dl->AddPolyline(pts, 10, fg, ImDrawFlags_Closed, 1.5f);
        break;
    }
    default:
        if (icon >= kIcoBell) DrawNavIcon(dl, icon, c, fg);
        break;
    }
    if (hov) { ImGui::SetMouseCursor(ImGuiMouseCursor_Hand); if (tip && *tip) ImGui::SetTooltip("%s", tip); }
    return click;
}

// Human "modified N ago" text for a file ("" if the time can't be read).
std::string ModifiedAgo(const fs::path& p) {
    std::error_code ec;
    auto wt = fs::last_write_time(p, ec);
    if (ec) return {};
    auto secs = std::chrono::duration_cast<std::chrono::seconds>(
        fs::file_time_type::clock::now() - wt).count();
    if (secs < 0) secs = 0;
    if (secs < 90) return "just now";
    if (secs < 90 * 60) return std::to_string(secs / 60) + " min ago";
    if (secs < 36 * 3600) return std::to_string(secs / 3600) + " h ago";
    return std::to_string(secs / 86400) + " d ago";
}

// Accent ring around the current child window when the mouse is over it
// (hover feedback for card-style rows and tiles).
void HoverRing() {
    if (!ImGui::IsWindowHovered()) return;
    ImVec2 mn = ImGui::GetWindowPos();
    ImVec2 mx(mn.x + ImGui::GetWindowSize().x, mn.y + ImGui::GetWindowSize().y);
    ImGui::GetWindowDrawList()->AddRect(mn, mx, ImGui::GetColorU32(kAccent), 7.0f, 0, 2.0f);
}

// Draw a 5-star rating row at a screen position. `avg` in [0,5] fills stars
// (a star lights once avg reaches its half-point). Each star is 5 triangles
// radiating from a centre, so it reads as a real star without concave-fill
// issues. Returns the row's pixel width so callers can advance the cursor.
float DrawStars(ImDrawList* dl, ImVec2 pos, float sz, float avg, ImU32 on, ImU32 off) {
    const float step = sz * 1.3f, outer = sz * 0.5f, inner = sz * 0.2f;
    for (int i = 0; i < 5; ++i) {
        ImVec2 c(pos.x + outer + i * step, pos.y + outer);
        ImU32 col = (avg >= i + 0.5f) ? on : off;
        ImVec2 pts[10];
        for (int k = 0; k < 10; ++k) {
            float ang = -1.5707963f + k * 0.6283185f;     // -90deg, +36deg steps
            float rad = (k % 2 == 0) ? outer : inner;
            pts[k] = ImVec2(c.x + std::cos(ang) * rad, c.y + std::sin(ang) * rad);
        }
        for (int k = 0; k < 10; ++k)                        // fan the 10 rim points around the centre
            dl->AddTriangleFilled(c, pts[k], pts[(k + 1) % 10], col);
    }
    return 5 * step;
}

// ---- Page kit ----------------------------------------------------------------
bool SearchPill(const char* id, const char* hint, char* buf, std::size_t cap, float w, int flags, float h) {
    const ImVec2 sp = ImGui::GetCursorScreenPos();
    const float padY = h > 0.0f ? std::max(0.0f, (h - ImGui::GetTextLineHeight()) * 0.5f) : UiPx(7.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, (ImGui::GetTextLineHeight() + 2 * padY) * 0.5f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(UiPx(30.0f), padY));
    ImGui::SetNextItemWidth(w);
    const bool r = ImGui::InputTextWithHint(id, hint, buf, cap, flags);
    ImGui::PopStyleVar(2);
    const ImU32 dim = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    const float k = g_uiScale;
    const ImVec2 mc(sp.x + 15.0f * k, sp.y + ImGui::GetItemRectSize().y * 0.5f - 1.0f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddCircle(mc, 4.6f * k, dim, 16, 1.5f);
    dl->AddLine(ImVec2(mc.x + 3.4f * k, mc.y + 3.4f * k), ImVec2(mc.x + 6.8f * k, mc.y + 6.8f * k), dim, 1.8f);
    return r;
}

int ChipRow(const char* id, const std::vector<Chip>& chips, int selected, float availW) {
    namespace L = okay::launcherlayout;
    const float ch = ImGui::GetTextLineHeight() + UiPx(10.0f), padX = UiPx(11.0f), dotW = UiPx(12.0f), cntGap = UiPx(7.0f);
    std::vector<float> widths;
    std::vector<std::string> counts;
    for (const Chip& c : chips) {
        std::string cnt = c.count >= 0 ? std::to_string(c.count) : std::string();
        float w = ImGui::CalcTextSize(c.label.c_str()).x + 2 * padX + (c.dot ? dotW : 0.0f);
        if (!cnt.empty()) w += cntGap + ImGui::CalcTextSize(cnt.c_str()).x + (c.countBadge ? UiPx(10.0f) : 0.0f);
        widths.push_back(std::ceil(w)); counts.push_back(cnt);
    }
    const std::vector<L::Rect> rs = L::Flow(widths, ch, availW, UiPx(6.0f), UiPx(6.0f));
    const ImVec2 o = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 cText = ImGui::GetColorU32(ImGuiCol_Text), cDim = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    const ImVec4 accLt(kAccent.x * 0.55f + 0.45f, kAccent.y * 0.55f + 0.45f, kAccent.z * 0.55f + 0.45f, 1.0f);
    int clicked = -1;
    ImGui::PushID(id);
    for (std::size_t i = 0; i < chips.size(); ++i) {
        const Chip& c = chips[i];
        const ImVec2 a(o.x + rs[i].x, o.y + rs[i].y), b(a.x + rs[i].w, a.y + rs[i].h);
        ImGui::SetCursorScreenPos(a);
        ImGui::PushID((int)i);
        if (ImGui::InvisibleButton("##chip", ImVec2(rs[i].w, rs[i].h))) clicked = (int)i;
        const bool h = ImGui::IsItemHovered(), on = selected == (int)i;
        const bool focus = ImGui::IsItemFocused() && ImGui::GetIO().NavVisible;
        ImGui::PopID();
        dl->AddRectFilled(a, b, on ? ImGui::GetColorU32(ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.26f))
                                   : (h ? IM_COL32(255, 255, 255, 22) : IM_COL32(255, 255, 255, 10)), ch * 0.5f);
        if (on) dl->AddRect(a, b, ImGui::GetColorU32(ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.55f)), ch * 0.5f, 0, 1.0f);
        if (focus) dl->AddRect(ImVec2(a.x - 2, a.y - 2), ImVec2(b.x + 2, b.y + 2), ImGui::GetColorU32(kAccent), ch * 0.5f + 2, 0, 1.5f);
        float tx = a.x + padX;
        const float ty = a.y + (ch - ImGui::GetTextLineHeight()) * 0.5f;
        if (c.dot) { dl->AddCircleFilled(ImVec2(tx + UiPx(3.5f), a.y + ch * 0.5f), UiPx(4.0f), c.dot, 12); tx += dotW; }
        dl->AddText(ImVec2(tx, ty), on ? ImGui::GetColorU32(accLt) : (h ? cText : cDim), c.label.c_str());
        if (!counts[i].empty()) {
            const float cx = tx + ImGui::CalcTextSize(c.label.c_str()).x + cntGap, cw = ImGui::CalcTextSize(counts[i].c_str()).x;
            if (c.countBadge) {
                const float bh = ImGui::GetTextLineHeight() - UiPx(1.0f);
                dl->AddRectFilled(ImVec2(cx, a.y + (ch - bh) * 0.5f), ImVec2(cx + cw + UiPx(10.0f), a.y + (ch + bh) * 0.5f), ImGui::GetColorU32(kAccent), bh * 0.5f);
                dl->AddText(ImVec2(cx + UiPx(5.0f), ty), IM_COL32_WHITE, counts[i].c_str());
            } else {
                dl->AddText(ImVec2(cx, ty), ImGui::GetColorU32(ImGuiCol_TextDisabled, on ? 1.0f : 0.8f), counts[i].c_str());
            }
        }
        if (h) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }
    ImGui::PopID();
    ImGui::SetCursorScreenPos(ImVec2(o.x, o.y + L::FlowHeight(rs)));
    ImGui::Dummy(ImVec2(0, 0));
    return clicked;
}

void EmptyState(int icon, const char* title, const char* sub, float topPad, bool ok) {
    const float aw = ImGui::GetContentRegionAvail().x;
    const float R = UiPx(30.0f);
    ImGui::Dummy(ImVec2(0, topPad));
    const ImVec2 c(ImGui::GetCursorScreenPos().x + aw * 0.5f, ImGui::GetCursorScreenPos().y + R);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddCircleFilled(c, R, IM_COL32(255, 255, 255, 12), 40);
    DrawNavIcon(dl, icon, c, ImGui::GetColorU32(ImGuiCol_TextDisabled));
    if (ok) {   // the small green tick: nothing is waiting
        const ImVec2 bc(c.x + R * 0.66f, c.y + R * 0.66f);
        dl->AddCircleFilled(bc, UiPx(10.0f), ImGui::GetColorU32(ImGuiCol_WindowBg), 20);
        dl->AddCircleFilled(bc, UiPx(8.0f), IM_COL32(64, 186, 104, 255), 20);
        dl->PathLineTo(ImVec2(bc.x - 3.6f, bc.y + 0.2f)); dl->PathLineTo(ImVec2(bc.x - 1.0f, bc.y + 2.8f));
        dl->PathLineTo(ImVec2(bc.x + 3.8f, bc.y - 2.8f)); dl->PathStroke(IM_COL32_WHITE, 0, 1.8f);
    }
    ImGui::Dummy(ImVec2(0, 2 * R + UiPx(14.0f)));
    const float x0 = ImGui::GetCursorPosX();
    auto centred = [&](const char* s, bool dim, float scale) {
        if (!s || !*s) return;
        ImGui::SetWindowFontScale(scale);
        const float maxW = std::max(UiPx(120.0f), std::min(aw - UiPx(16.0f), UiPx(460.0f)));
        // Wrap by words to maxW (the shared word wrapper), then centre each line.
        const std::vector<std::string> lines = okaypanels::HierWrapWords(s, maxW, [](const std::string& t) { return ImGui::CalcTextSize(t.c_str()).x; });
        for (const std::string& l : lines) {
            const float tw = ImGui::CalcTextSize(l.c_str()).x;
            ImGui::SetCursorPosX(x0 + std::max(0.0f, (aw - tw) * 0.5f));
            if (dim) ImGui::TextDisabled("%s", l.c_str()); else ImGui::TextUnformatted(l.c_str());
        }
        ImGui::SetWindowFontScale(1.0f);
    };
    centred(title, false, 1.12f);
    ImGui::Dummy(ImVec2(0, UiPx(2.0f)));
    centred(sub, true, 1.0f);
}

void WrapTwoLines(const std::string& s, float maxW, std::string& l1, std::string& l2) {
    l1.clear(); l2.clear();
    if (ImGui::CalcTextSize(s.c_str()).x <= maxW) { l1 = s; return; }
    std::size_t cut = 0;
    for (std::size_t i = 0; i < s.size(); ++i)
        if (s[i] == ' ') { if (ImGui::CalcTextSize(s.c_str(), s.c_str() + i).x <= maxW) cut = i; else break; }
    if (cut == 0) { l1 = FitText(s, maxW); return; }
    l1 = s.substr(0, cut); l2 = FitText(s.substr(cut + 1), maxW);
}

void CardBackground(ImDrawList* dl, ImVec2 a, ImVec2 b, bool hover, ImU32 hoverBorder, float borderW) {
    const float r = UiPx(12.0f);
    dl->AddRectFilled(a, b, ImGui::GetColorU32(okaytheme::kRaised), r);
    if (!hover) return;
    dl->AddRectFilled(a, b, IM_COL32(255, 255, 255, 7), r);
    dl->AddRect(a, b, hoverBorder ? hoverBorder : IM_COL32(255, 255, 255, 36), r, 0, borderW);
}

void SkeletonBlock(ImDrawList* dl, ImVec2 a, ImVec2 b, float rounding) {
    const float t = (float)ImGui::GetTime();
    const float pulse = 0.5f + 0.5f * std::sin(t * 3.2f);
    dl->AddRectFilled(a, b, IM_COL32(255, 255, 255, (int)(10 + 10 * pulse)), rounding);
}

void SectionLabel(const char* title, int n) {
    char t[96];
    if (n >= 0) std::snprintf(t, sizeof(t), "%s  \xE2\x80\x94  %d", title, n); else std::snprintf(t, sizeof(t), "%s", title);
    ImGui::SetWindowFontScale(0.82f);
    ImGui::TextDisabled("%s", t);
    ImGui::SetWindowFontScale(1.0f);
}

bool BeginSlateCombo(const char* label, const char* preview, int flags) {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = ImGui::CalcItemWidth(), h = ImGui::GetFrameHeight(), chev = h;
    const std::string pv = FitText(preview ? preview : "", std::max(0.0f, w - ImGui::GetStyle().FramePadding.x * 2 - chev * 0.7f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, std::max(ImGui::GetStyle().FrameRounding, UiPx(8.0f)));
    const bool open = ImGui::BeginCombo(label, pv.c_str(), flags | ImGuiComboFlags_NoArrowButton);
    ImGui::PopStyleVar();
    // The chevron, inside the box (a "v" of two strokes), brighter while the list is open or the box is hovered.
    const bool hot = open || ImGui::IsMouseHoveringRect(p, ImVec2(p.x + w, p.y + h));
    const ImVec2 c(p.x + w - chev * 0.5f, p.y + h * 0.5f + 1.0f);
    const float r = std::max(3.5f, h * 0.15f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (open) dl = ImGui::GetForegroundDrawList(ImGui::GetMainViewport());   // (the window's own list belongs to the open popup now)
    dl->PathLineTo(ImVec2(c.x - r, c.y - r * 0.5f));
    dl->PathLineTo(ImVec2(c.x, c.y + r * 0.5f));
    dl->PathLineTo(ImVec2(c.x + r, c.y - r * 0.5f));
    dl->PathStroke(ImGui::GetColorU32(hot ? ImGuiCol_Text : ImGuiCol_TextDisabled), 0, 1.8f);
    return open;
}
bool SlateCombo(const char* label, int* current, const char* const items[], int count, int popupMaxItems) {
    const char* preview = (*current >= 0 && *current < count) ? items[*current] : "";
    if (popupMaxItems > 0) ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0), ImVec2(FLT_MAX, ImGui::GetTextLineHeightWithSpacing() * popupMaxItems + ImGui::GetStyle().WindowPadding.y * 2));
    if (!BeginSlateCombo(label, preview)) return false;
    bool changed = false;
    for (int i = 0; i < count; ++i) {
        ImGui::PushID(i);
        const bool sel = i == *current;
        if (ImGui::Selectable(items[i], sel)) { if (*current != i) changed = true; *current = i; }
        if (sel) ImGui::SetItemDefaultFocus();
        ImGui::PopID();
    }
    ImGui::EndCombo();
    return changed;
}
bool SlateCombo(const char* label, int* current, const char* itemsZeroSeparated, int popupMaxItems) {
    std::vector<const char*> items;
    for (const char* q = itemsZeroSeparated; q && *q; q += std::strlen(q) + 1) items.push_back(q);
    return SlateCombo(label, current, items.data(), (int)items.size(), popupMaxItems);
}

bool g_panelShortcut = false;
bool PanelToggle(const char* id, bool open, const char* what, float d, ImU32 hoverBg, ImU32 fg, ImU32 fgHover) {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool click = ImGui::InvisibleButton(id, ImVec2(d, d));
    const bool h = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (h) dl->AddRectFilled(p, ImVec2(p.x + d, p.y + d), hoverBg, 6.0f);
    DrawNavIcon(dl, kIcoPanel, ImVec2(p.x + d * 0.5f, p.y + d * 0.5f), h ? fgHover : fg);
    if (h) { ImGui::SetMouseCursor(ImGuiMouseCursor_Hand); ImGui::SetTooltip("%s %s  (Ctrl+B)", open ? "Hide" : "Show", what); }
    return click;
}

float FitButtonW(const char* label, float minW) {
    const char* end = std::strstr(label, "##");   // the visible part of the label
    return okay::launcherlayout::ButtonW(ImGui::CalcTextSize(label, end).x, UiPx(14.0f), minW);
}
bool FitButton(const char* label, float h, float minW, bool primary) {
    const ImVec2 sz(FitButtonW(label, minW), h);
    return primary ? PrimaryButton(label, sz) : ImGui::Button(label, sz);
}

} // namespace okl
