// Shared editor widgets: the accent colour, component category tints, the drawn
// toolbar icons and the icon / search / toggle buttons every panel uses.
#include "EditorCommon.hpp"
#include "EditorWidgets.hpp"

namespace oked {

// The current accent colour, for panels that want to tint their own widgets to
// match the theme (component headers, the Add Component button, ...).
ImVec4 AccentCol(float alpha) {
    const AccentPreset& ap = kAccents[(g_accent < 0 || g_accent >= kAccentCount) ? 0 : g_accent];
    return ImVec4(ap.r, ap.g, ap.b, alpha);
}
// A tasteful category tint for a component header, derived from its label — so the
// Inspector groups visually by system (physics/render/audio/UI/animation/script)
// while staying cohesive. Falls back to the theme accent.
ImVec4 CategoryColor(const char* label) {
    auto has = [&](const char* k) { return std::strstr(label, k) != nullptr; };
    if (has("Collider") || has("Rigidbody") || has("Joint"))          return ImVec4(0.42f, 0.80f, 0.50f, 1.0f); // physics — green
    if (has("Audio"))                                                 return ImVec4(0.95f, 0.62f, 0.30f, 1.0f); // audio — orange
    if (has("Animat") || has("IK") || has("Root Motion"))            return ImVec4(0.30f, 0.78f, 0.80f, 1.0f); // animation — teal
    if (has("Script") || has("Visual Script"))                       return ImVec4(0.90f, 0.80f, 0.35f, 1.0f); // scripting — yellow
    if (has("UI ") || has("Canvas") || has("Button") || has("Panel") || has("Slider") ||
        has("Toggle") || has("Tabs") || has("Dropdown") || has("Scroll") || has("Layout") ||
        has("Progress") || has("Rating") || has("Stepper") || has("Input Field") || has("Tooltip"))
                                                                       return ImVec4(0.66f, 0.52f, 0.96f, 1.0f); // UI — violet
    if (has("Renderer") || has("Sprite") || has("Mesh") || has("Text") || has("Particle") ||
        has("Light") || has("Camera") || has("Tilemap") || has("Terrain") || has("Water"))
                                                                       return ImVec4(0.42f, 0.66f, 0.98f, 1.0f); // rendering — blue
    return AccentCol(1.0f);
}
// Icon family for a component header, matching CategoryColor's keywords:
// 0 dot (default), 1 square (rendering), 2 circle (physics), 3 triangle
// (animation), 4 diamond (scripts), 5 speaker (audio), 6 ring (UI).
int CategoryGlyph(const char* label) {
    auto has = [&](const char* k) { return std::strstr(label, k) != nullptr; };
    if (has("Collider") || has("Rigidbody") || has("Joint"))          return 2;
    if (has("Audio"))                                                 return 5;
    if (has("Animat") || has("IK") || has("Root Motion"))             return 3;
    if (has("Script") || has("Visual Script"))                        return 4;
    if (has("UI ") || has("Canvas") || has("Button") || has("Panel") || has("Slider") ||
        has("Toggle") || has("Tabs") || has("Dropdown") || has("Scroll") || has("Layout") ||
        has("Progress") || has("Rating") || has("Stepper") || has("Input Field") || has("Tooltip"))
                                                                       return 6;
    if (has("Renderer") || has("Sprite") || has("Mesh") || has("Text") || has("Particle") ||
        has("Light") || has("Camera") || has("Tilemap") || has("Terrain") || has("Water"))
                                                                       return 1;
    return 0;
}

// A button that shows an accent "pressed" look while `active` — for toolbar
// toggles (transform tools, Local/Global, Snap, edit modes) so the active state
// reads consistently in the theme colour instead of ad-hoc blues.
bool AccentToggleButton(const char* label, bool active, const ImVec2& size) {
    if (active) {
        ImGui::PushStyleColor(ImGuiCol_Button,        AccentCol(0.90f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, AccentCol(1.00f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  AccentCol(0.80f));
        ImGui::PushStyleColor(ImGuiCol_Text,          ImVec4(1, 1, 1, 1));
    }
    bool clicked = ImGui::Button(label, size);
    if (active) ImGui::PopStyleColor(4);
    return clicked;
}

// Compact square toolbar buttons with drawn vector glyphs (the UI font has no
// icon glyphs). Panels use these instead of rows of text buttons and checkboxes:
// the primary actions stay one click away and everything else lives in a "more"
// menu, so toolbars fit on one line like other editors.
void DrawTbIcon(ImDrawList* dl, TbIcon ic, ImVec2 c, float s, ImU32 col) {
    const float t = 1.6f;
    switch (ic) {
    case TbIcon::More:
        for (int i = -1; i <= 1; ++i) dl->AddCircleFilled(ImVec2(c.x + i * s * 0.36f, c.y), s * 0.1f, col);
        break;
    case TbIcon::Plus:
        dl->AddLine(ImVec2(c.x - s * 0.4f, c.y), ImVec2(c.x + s * 0.4f, c.y), col, t + 0.4f);
        dl->AddLine(ImVec2(c.x, c.y - s * 0.4f), ImVec2(c.x, c.y + s * 0.4f), col, t + 0.4f);
        break;
    case TbIcon::Move: {
        float r = s * 0.48f, a = s * 0.16f;
        dl->AddLine(ImVec2(c.x - r, c.y), ImVec2(c.x + r, c.y), col, t);
        dl->AddLine(ImVec2(c.x, c.y - r), ImVec2(c.x, c.y + r), col, t);
        dl->AddTriangleFilled(ImVec2(c.x + r + 1, c.y), ImVec2(c.x + r - a, c.y - a), ImVec2(c.x + r - a, c.y + a), col);
        dl->AddTriangleFilled(ImVec2(c.x - r - 1, c.y), ImVec2(c.x - r + a, c.y + a), ImVec2(c.x - r + a, c.y - a), col);
        dl->AddTriangleFilled(ImVec2(c.x, c.y - r - 1), ImVec2(c.x + a, c.y - r + a), ImVec2(c.x - a, c.y - r + a), col);
        dl->AddTriangleFilled(ImVec2(c.x, c.y + r + 1), ImVec2(c.x - a, c.y + r - a), ImVec2(c.x + a, c.y + r - a), col);
        break; }
    case TbIcon::Rotate: {
        float r = s * 0.38f;
        dl->PathArcTo(c, r, IM_PI * 0.15f, IM_PI * 1.75f, 18);
        dl->PathStroke(col, 0, t);
        ImVec2 e(c.x + r * std::cos(IM_PI * 0.15f), c.y + r * std::sin(IM_PI * 0.15f));
        float a = s * 0.17f;
        dl->AddTriangleFilled(ImVec2(e.x - a, e.y - a * 0.2f), ImVec2(e.x + a, e.y - a * 0.2f), ImVec2(e.x, e.y + a * 1.1f), col);
        break; }
    case TbIcon::Scale: {
        float r = s * 0.42f;
        dl->AddRect(ImVec2(c.x - r, c.y - r * 0.1f), ImVec2(c.x + r * 0.1f, c.y + r), col, 1.0f, 0, t);
        dl->AddLine(ImVec2(c.x - r * 0.1f, c.y + r * 0.1f), ImVec2(c.x + r, c.y - r), col, t);
        dl->AddLine(ImVec2(c.x + r, c.y - r), ImVec2(c.x + r * 0.35f, c.y - r), col, t);
        dl->AddLine(ImVec2(c.x + r, c.y - r), ImVec2(c.x + r, c.y - r * 0.35f), col, t);
        break; }
    case TbIcon::Global: {
        float r = s * 0.42f;
        dl->AddCircle(c, r, col, 20, t);
        dl->AddEllipse(c, ImVec2(r * 0.42f, r), col, 0.0f, 16, t * 0.8f);
        dl->AddLine(ImVec2(c.x - r, c.y), ImVec2(c.x + r, c.y), col, t * 0.8f);
        break; }
    case TbIcon::Local: {   // isometric cube outline
        float r = s * 0.42f, h = r * 0.5f;
        ImVec2 top(c.x, c.y - r), lft(c.x - r, c.y - h), rgt(c.x + r, c.y - h), mid(c.x, c.y);
        ImVec2 bl(c.x - r, c.y + h), br(c.x + r, c.y + h), bot(c.x, c.y + r);
        ImVec2 hex[6] = {top, rgt, br, bot, bl, lft};
        dl->AddPolyline(hex, 6, col, ImDrawFlags_Closed, t);
        dl->AddLine(lft, mid, col, t); dl->AddLine(rgt, mid, col, t); dl->AddLine(mid, bot, col, t);
        break; }
    case TbIcon::Snap: {    // magnet
        float r = s * 0.3f, top = c.y - s * 0.4f;
        dl->PathArcTo(ImVec2(c.x, c.y + s * 0.05f), r, 0.0f, IM_PI, 14);
        dl->PathStroke(col, 0, t + 1.6f);
        dl->AddLine(ImVec2(c.x - r, c.y + s * 0.05f), ImVec2(c.x - r, top), col, t + 1.6f);
        dl->AddLine(ImVec2(c.x + r, c.y + s * 0.05f), ImVec2(c.x + r, top), col, t + 1.6f);
        dl->AddLine(ImVec2(c.x - r, top + s * 0.18f), ImVec2(c.x - r, top), IM_COL32(230, 90, 90, 255), t + 1.8f);
        dl->AddLine(ImVec2(c.x + r, top + s * 0.18f), ImVec2(c.x + r, top), IM_COL32(230, 90, 90, 255), t + 1.8f);
        break; }
    case TbIcon::Frame: {   // corner brackets around a dot
        float r = s * 0.44f, k = s * 0.2f;
        for (int sx = -1; sx <= 1; sx += 2)
            for (int sy = -1; sy <= 1; sy += 2) {
                ImVec2 p(c.x + sx * r, c.y + sy * r);
                dl->AddLine(p, ImVec2(p.x - sx * k, p.y), col, t);
                dl->AddLine(p, ImVec2(p.x, p.y - sy * k), col, t);
            }
        dl->AddCircleFilled(c, s * 0.12f, col);
        break; }
    case TbIcon::Search: {
        float r = s * 0.27f;
        ImVec2 o(c.x - s * 0.08f, c.y - s * 0.08f);
        dl->AddCircle(o, r, col, 14, t);
        dl->AddLine(ImVec2(o.x + r * 0.7f, o.y + r * 0.7f), ImVec2(c.x + s * 0.4f, c.y + s * 0.4f), col, t + 0.6f);
        break; }
    case TbIcon::Clear: {   // circle with a slash ("clear all")
        float r = s * 0.38f;
        dl->AddCircle(c, r, col, 18, t);
        dl->AddLine(ImVec2(c.x - r * 0.7f, c.y + r * 0.7f), ImVec2(c.x + r * 0.7f, c.y - r * 0.7f), col, t);
        break; }
    case TbIcon::Expand: case TbIcon::Collapse: {
        float r = s * 0.3f, d = (ic == TbIcon::Expand) ? 1.0f : -1.0f;
        for (int k = 0; k < 2; ++k) {
            float y = c.y + (k ? s * 0.22f : -s * 0.22f);
            dl->AddLine(ImVec2(c.x - r, y - d * r * 0.5f), ImVec2(c.x, y + d * r * 0.5f), col, t);
            dl->AddLine(ImVec2(c.x, y + d * r * 0.5f), ImVec2(c.x + r, y - d * r * 0.5f), col, t);
        }
        break; }
    case TbIcon::Folder: {
        float w = s * 0.46f, h = s * 0.34f;
        dl->AddRect(ImVec2(c.x - w, c.y - h * 0.6f), ImVec2(c.x + w, c.y + h), col, 1.5f, 0, t);
        dl->AddLine(ImVec2(c.x - w, c.y - h * 0.6f), ImVec2(c.x - w, c.y - h * 1.1f), col, t);
        dl->AddLine(ImVec2(c.x - w, c.y - h * 1.1f), ImVec2(c.x - w * 0.15f, c.y - h * 1.1f), col, t);
        dl->AddLine(ImVec2(c.x - w * 0.15f, c.y - h * 1.1f), ImVec2(c.x + w * 0.05f, c.y - h * 0.6f), col, t);
        break; }
    case TbIcon::Grid: {
        float g = s * 0.17f, o = s * 0.2f;
        for (int i = -1; i <= 1; i += 2)
            for (int j = -1; j <= 1; j += 2)
                dl->AddRectFilled(ImVec2(c.x + i * o - g, c.y + j * o - g), ImVec2(c.x + i * o + g, c.y + j * o + g), col, 1.0f);
        break; }
    case TbIcon::List: {
        for (int k = -1; k <= 1; ++k) {
            float y = c.y + k * s * 0.26f;
            dl->AddCircleFilled(ImVec2(c.x - s * 0.36f, y), s * 0.06f, col);
            dl->AddLine(ImVec2(c.x - s * 0.22f, y), ImVec2(c.x + s * 0.42f, y), col, t);
        }
        break; }
    case TbIcon::Sort: {
        for (int k = 0; k < 3; ++k) {
            float y = c.y - s * 0.3f + k * s * 0.3f, w = s * (0.25f + 0.2f * k);
            dl->AddLine(ImVec2(c.x - s * 0.42f, y), ImVec2(c.x - s * 0.42f + w, y), col, t);
        }
        break; }
    }
}
// Square icon button (frame-height); accent-tinted while `active`. `tip` shows on hover.
bool IconButton(const char* id, TbIcon ic, const char* tip, bool active) {
    float h = ImGui::GetFrameHeight();
    bool clicked = AccentToggleButton(id, active, ImVec2(h, h));
    ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
    ImU32 col = ImGui::GetColorU32(active ? ImVec4(1, 1, 1, 1)
                                          : ImGui::GetStyleColorVec4(ImGuiCol_Text));
    if (!active && !ImGui::IsItemHovered()) col = ImGui::GetColorU32(ImGuiCol_Text, 0.8f);
    DrawTbIcon(ImGui::GetWindowDrawList(), ic, ImVec2((mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f),
               h * 0.5f, col);
    if (tip && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
    return clicked;
}
// A search field with a drawn magnifier inside and a clear (x) affordance when it
// has text. `width` <= 0 fills the remaining width minus |width|.
bool SearchField(const char* id, char* buf, std::size_t bufSize, const char* hint, float width) {
    ImGuiStyle& st = ImGui::GetStyle();
    float h = ImGui::GetFrameHeight();
    float w = width > 0.0f ? width : ImGui::GetContentRegionAvail().x + width;
    if (w < h * 3.0f) w = h * 3.0f;
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(h * 0.95f, st.FramePadding.y));
    ImGui::SetNextItemWidth(w);
    bool changed = ImGui::InputTextWithHint(id, hint, buf, bufSize);
    ImGui::PopStyleVar();
    ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    DrawTbIcon(dl, TbIcon::Search, ImVec2(mn.x + h * 0.5f, (mn.y + mx.y) * 0.5f), h * 0.42f,
               ImGui::GetColorU32(ImGuiCol_TextDisabled));
    if (buf[0]) {   // clear button drawn over the right edge of the field
        ImVec2 xc(mx.x - h * 0.5f, (mn.y + mx.y) * 0.5f);
        ImVec2 mp = ImGui::GetIO().MousePos;
        bool hot = std::fabs(mp.x - xc.x) < h * 0.45f && std::fabs(mp.y - xc.y) < h * 0.45f &&
                   ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
        float r = h * 0.17f;
        ImU32 xcCol = ImGui::GetColorU32(hot ? ImGuiCol_Text : ImGuiCol_TextDisabled);
        dl->AddLine(ImVec2(xc.x - r, xc.y - r), ImVec2(xc.x + r, xc.y + r), xcCol, 1.6f);
        dl->AddLine(ImVec2(xc.x - r, xc.y + r), ImVec2(xc.x + r, xc.y - r), xcCol, 1.6f);
        if (hot && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            buf[0] = '\0'; changed = true;
            ImGui::ClearActiveID();
        }
    }
    return changed;
}
// Right-align the next `w` pixels of items on the current line.
void SameLineRight(float w) {
    ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - w);
}

} // namespace oked
