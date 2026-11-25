// The sidebar's line icons (NavIcons.hpp), drawn with the draw list so no icon font is needed. Moved out of
// launcher/main.cpp unchanged (docs/engine/modularization_plan.md, Part C section 8, batch L4).
#include "LauncherShared.hpp"
#include "NavIcons.hpp"

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

} // namespace okl
