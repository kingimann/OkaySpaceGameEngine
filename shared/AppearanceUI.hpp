#pragma once
// The appearance controls of the Dear ImGui programs: the design picker (a card per design with a small live picture of it,
// painted from that design's own tokens) and the person's options on top (light or dark, accent, density, roundness, UI scale,
// text size, shadows, reduced motion). One copy, drawn by the launcher's Settings > Appearance page and the editor's
// Edit > Appearance menu. A change is applied at once (OkayUI::design::Set), saved to the per-user appearance.json and picked up
// by every other program that follows it (design::Poll). The designs themselves are okay/UI/OkayUIDesign.hpp.
// Written by us (it grew out of the launcher's theme cards); no code from another project.
#include "imgui.h"
#include "OkayTheme.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace okayappearance {

namespace D = OkayUI::design;

inline ImU32 U(const D::Rgba& c, float a = 1.0f) { return IM_COL32(c.r, c.g, c.b, (int)(c.a * a)); }

/// Make `o` the live choice and save it (when the program follows the per-user file).
inline void Commit(const D::Options& o) { D::Set(o); D::SaveUserFile(); }

/// A small picture of a design at `p` (w x h): its page, header rail, side list, a card with text lines, an accent button,
/// a switch and the tab shape it uses, all in that design's colours and corner radii.
inline void DrawPreview(ImDrawList* dl, ImVec2 p, float w, float h, const D::Tokens& t) {
    const D::Palette& c = t.c;
    const float R = std::min(t.shape.radiusLarge * 0.6f, 10.0f), r = std::min(t.shape.radius * 0.6f, 6.0f);
    const ImVec2 q(p.x + w, p.y + h);
    dl->AddRectFilled(p, q, U(c.base), R);
    dl->AddRectFilled(p, ImVec2(q.x, p.y + 14.0f), U(c.sunken), R, ImDrawFlags_RoundCornersTop);
    dl->AddLine(ImVec2(p.x, p.y + 14.0f), ImVec2(q.x, p.y + 14.0f), U(c.hairline));
    dl->AddCircleFilled(ImVec2(p.x + 9.0f, p.y + 7.0f), 2.5f, U(c.textFaint), 10);
    dl->AddRectFilled(ImVec2(p.x + 16.0f, p.y + 5.0f), ImVec2(p.x + 44.0f, p.y + 9.0f), U(c.textDim), 2.0f);
    // tabs in the design's shape
    const float ty = p.y + 18.0f;
    for (int i = 0; i < 3; ++i) {
        const float tx = p.x + 6.0f + i * 22.0f;
        const bool on = i == 0;
        if (t.shape.tabs == D::TabShape::Pill) { if (on) dl->AddRectFilled(ImVec2(tx, ty), ImVec2(tx + 20.0f, ty + 9.0f), U(c.accent), 4.5f); }
        else if (t.shape.tabs == D::TabShape::Segmented) {
            if (i == 0) dl->AddRectFilled(ImVec2(tx - 1.0f, ty - 1.0f), ImVec2(tx + 65.0f, ty + 10.0f), U(c.sunken), r);
            if (on) dl->AddRectFilled(ImVec2(tx, ty), ImVec2(tx + 20.0f, ty + 9.0f), U(c.base), r * 0.7f);
        } else if (on) dl->AddRectFilled(ImVec2(tx + 2.0f, ty + 8.0f), ImVec2(tx + 18.0f, ty + 10.0f), U(c.accent), 1.0f);
        dl->AddRectFilled(ImVec2(tx + 5.0f, ty + 3.0f), ImVec2(tx + 15.0f, ty + 5.5f),
                          on && t.shape.tabs == D::TabShape::Pill ? U(c.onAccent) : U(on ? c.text : c.textDim), 1.0f);
    }
    // side list with a selected row
    const float sy = p.y + 32.0f, sw = w * 0.30f;
    for (int i = 0; i < 4; ++i) {
        const float yy = sy + i * 9.0f;
        if (i == 1) dl->AddRectFilled(ImVec2(p.x + 4.0f, yy - 2.0f), ImVec2(p.x + sw, yy + 6.0f), U(D::WithAlpha(c.accent, 70)), 2.0f);
        dl->AddRectFilled(ImVec2(p.x + 8.0f, yy), ImVec2(p.x + sw - 6.0f - (i % 2) * 6.0f, yy + 3.0f), U(i == 1 ? c.text : c.textDim), 1.0f);
    }
    // a raised card: shadow (when the design has one), outline, text lines, a field, an accent button and a switch
    const ImVec2 a(p.x + sw + 6.0f, p.y + 30.0f), b(q.x - 6.0f, q.y - 6.0f);
    if (t.shape.shadow > 0) dl->AddRectFilled(ImVec2(a.x - 1.0f, a.y + 1.5f), ImVec2(b.x + 1.0f, b.y + 2.5f), IM_COL32(0, 0, 0, c.light ? 30 : 70), R * 0.8f);
    dl->AddRectFilled(a, b, U(c.raised), R * 0.8f);
    dl->AddRect(a, b, U(c.hairline), R * 0.8f, 0, t.shape.borderPx);
    dl->AddRectFilled(ImVec2(a.x + 6.0f, a.y + 6.0f), ImVec2(a.x + 40.0f, a.y + 9.0f), U(c.text), 1.0f);
    dl->AddRectFilled(ImVec2(a.x + 6.0f, a.y + 12.0f), ImVec2(b.x - 10.0f, a.y + 14.5f), U(c.textDim), 1.0f);
    const ImVec2 f0(a.x + 6.0f, a.y + 19.0f), f1(b.x - 6.0f, a.y + 29.0f);
    dl->AddRectFilled(f0, f1, U(c.field), r);
    if (t.shape.controlBorders) dl->AddRect(f0, f1, U(c.hairline), r, 0, t.shape.borderPx);
    const float by = b.y - 13.0f;
    dl->AddRectFilled(ImVec2(a.x + 6.0f, by), ImVec2(a.x + 34.0f, by + 9.0f), U(c.accent), r);
    dl->AddRectFilled(ImVec2(a.x + 12.0f, by + 3.5f), ImVec2(a.x + 28.0f, by + 5.5f), U(c.onAccent), 1.0f);
    const ImVec2 s0(b.x - 22.0f, by + 0.5f);
    dl->AddRectFilled(s0, ImVec2(s0.x + 16.0f, s0.y + 8.0f), U(c.accent), 4.0f);
    dl->AddCircleFilled(ImVec2(s0.x + 12.0f, s0.y + 4.0f), 3.0f, U(c.onAccent), 12);
}

/// The design cards. Returns true when the person picked another design (already applied and saved).
inline bool DesignPicker(float width) {
    D::Options o = D::Current();
    const int cur = D::DesignIndexOf(o);
    const int n = D::DesignCount();
    const float gap = 10.0f;
    const int perRow = std::max(1, std::min(n, (int)((width + gap) / (150.0f + gap))));
    const float cw = std::floor((width - gap * (perRow - 1)) / perRow), ch = std::floor(cw * 0.62f) + 22.0f;
    bool changed = false;
    for (int i = 0; i < n; ++i) {
        if (i % perRow) ImGui::SameLine(0, gap);
        ImGui::PushID(i);
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const bool clicked = ImGui::InvisibleButton("##design", ImVec2(cw, ch));
        const bool hov = ImGui::IsItemHovered(), on = i == cur;
        D::Options po = o; std::snprintf(po.design, sizeof po.design, "%s", D::DesignAt(i).name);
        const D::Tokens pt = D::Resolve(po);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        DrawPreview(dl, ImVec2(p.x + 4.0f, p.y + 4.0f), cw - 8.0f, ch - 26.0f, pt);
        const char* name = D::DesignAt(i).name;
        const ImVec2 ts = ImGui::CalcTextSize(name);
        dl->AddText(ImVec2(p.x + (cw - ts.x) * 0.5f, p.y + ch - 20.0f), on ? okaytheme::TextU32() : okaytheme::TextDimU32(), name);
        const ImVec4 acc = okaytheme::DesignAccent(D::CurrentTokens());
        dl->AddRect(p, ImVec2(p.x + cw, p.y + ch), on ? ImGui::GetColorU32(acc) : (hov ? ImGui::GetColorU32(ImVec4(acc.x, acc.y, acc.z, 0.6f)) : okaytheme::Ink(26)),
                    10.0f, 0, on ? 2.5f : 1.5f);
        if (hov) { ImGui::SetMouseCursor(ImGuiMouseCursor_Hand); ImGui::SetTooltip("%s", D::DesignAt(i).blurb); }
        if (clicked && !on) { Commit(po); changed = true; }
        ImGui::PopID();
    }
    return changed;
}

/// The accents offered besides the design's own (our own picks; one row of swatches).
struct AccentChoice { const char* name; D::Rgba c; };
inline const AccentChoice* AccentChoices(int* count) {
    static const AccentChoice k[] = {
        {"Iris", {110, 96, 240, 255}},   {"Blue", {58, 130, 246, 255}},  {"Teal", {20, 160, 150, 255}},  {"Green", {46, 160, 90, 255}},
        {"Amber", {214, 140, 20, 255}},  {"Coral", {230, 96, 80, 255}},  {"Rose", {214, 70, 130, 255}},  {"Slate blue", {66, 150, 250, 255}},
    };
    *count = (int)(sizeof k / sizeof k[0]);
    return k;
}

/// The person's options. `showScale`: the UI scale slider (the launcher; the editor sizes itself to the screen and multiplies).
/// Returns true when something changed (already applied and saved).
inline bool Customization(float width, bool showScale = true) {
    D::Options o = D::Current();
    const D::Design& d = D::DesignAt(D::DesignIndexOf(o));
    bool ch = false;
    const float labelW = std::min(170.0f, width * 0.36f), ctrlW = std::max(120.0f, std::min(320.0f, width - labelW - 8.0f));
    auto row = [&](const char* label) { ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted(label); ImGui::SameLine(labelW); ImGui::SetNextItemWidth(ctrlW); };
    // Light or dark (only the schemes the design has).
    {
        row("Light or dark");
        const char* names[] = {"Design default", "Dark", "Light"};
        int v = (int)o.scheme;
        ImGui::BeginGroup();
        for (int i = 0; i < 3; ++i) {
            const bool can = i == 0 || (i == 1 ? d.hasDark : d.hasLight);
            if (i) ImGui::SameLine();
            ImGui::BeginDisabled(!can);
            if (ImGui::RadioButton(names[i], v == i) && v != i) { o.scheme = (D::Scheme)i; ch = true; }
            ImGui::EndDisabled();
        }
        ImGui::EndGroup();
    }
    // Accent: the design's own, a preset, or any colour.
    {
        row("Accent color");
        ImGui::BeginGroup();
        int na = 0; const AccentChoice* acc = AccentChoices(&na);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float sz = 22.0f;
        for (int i = -1; i < na; ++i) {
            if (i >= 0) ImGui::SameLine(0, 6);
            ImGui::PushID(i + 100);
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const D::Rgba col = i < 0 ? D::Resolve([&] { D::Options x = o; x.customAccent = false; return x; }()).c.accent : acc[i].c;
            const bool on = i < 0 ? !o.customAccent : (o.customAccent && o.accent.r == col.r && o.accent.g == col.g && o.accent.b == col.b);
            if (ImGui::InvisibleButton("##acc", ImVec2(sz, sz))) {
                if (i < 0) o.customAccent = false; else { o.customAccent = true; o.accent = col; }
                ch = true;
            }
            const bool hov = ImGui::IsItemHovered();
            const ImVec2 c(p.x + sz * 0.5f, p.y + sz * 0.5f);
            if (on) dl->AddCircle(c, sz * 0.5f, okaytheme::TextU32(0.9f), 24, 2.0f);
            else if (hov) dl->AddCircle(c, sz * 0.5f, okaytheme::Ink(90), 24, 1.5f);
            dl->AddCircleFilled(c, sz * 0.5f - 3.0f, U(col), 24);
            if (i < 0) dl->AddLine(ImVec2(c.x - 4.0f, c.y + 4.0f), ImVec2(c.x + 4.0f, c.y - 4.0f), IM_COL32(255, 255, 255, 200), 1.5f);   // "the design's"
            if (hov) ImGui::SetTooltip("%s", i < 0 ? "The design's own accent" : acc[i].name);
            ImGui::PopID();
        }
        ImGui::SameLine(0, 10);
        float rgb[3] = {o.accent.r / 255.0f, o.accent.g / 255.0f, o.accent.b / 255.0f};
        if (!o.customAccent) { const D::Rgba a = D::CurrentTokens().c.accent; rgb[0] = a.r / 255.0f; rgb[1] = a.g / 255.0f; rgb[2] = a.b / 255.0f; }
        if (ImGui::ColorEdit3("##accentpick", rgb, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel)) {
            o.customAccent = true;
            o.accent = D::Rgba{(unsigned char)std::lround(rgb[0] * 255.0f), (unsigned char)std::lround(rgb[1] * 255.0f), (unsigned char)std::lround(rgb[2] * 255.0f), 255};
            ch = true;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Any colour");
        ImGui::EndGroup();
    }
    {
        row("Density");
        const char* names[] = {"Compact", "Comfortable", "Spacious"};
        int v = (int)o.density;
        if (ImGui::Combo("##density", &v, names, 3) && v != (int)o.density) { o.density = (D::Density)v; ch = true; }
    }
    {
        row("Corner roundness");
        float v = o.roundness;
        if (ImGui::SliderFloat("##round", &v, 0.0f, 2.0f, v < 0.05f ? "Square" : "%.1fx")) { o.roundness = v; ch = true; }
    }
    if (showScale) {
        row("UI scale");
        float v = o.uiScale;
        ImGui::SliderFloat("##uiscale", &v, 0.75f, 2.0f, "%.2fx");
        if (ImGui::IsItemDeactivatedAfterEdit()) { o.uiScale = v; ch = true; }   // applied on release: a live rescale under the pointer jumps
    }
    {
        row("Text size");
        float v = o.textSize;
        ImGui::SliderFloat("##textsize", &v, 0.85f, 1.3f, "%.2fx");
        if (ImGui::IsItemDeactivatedAfterEdit()) { o.textSize = v; ch = true; }
    }
    {
        row("Shadows");
        bool v = o.shadows;
        if (ImGui::Checkbox("##shadows", &v)) { o.shadows = v; ch = true; }
    }
    {
        row("Reduce motion");
        bool v = o.reduceMotion;
        if (ImGui::Checkbox("##motion", &v)) { o.reduceMotion = v; ch = true; }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("No hover, press or open animations.");
    }
    ImGui::Dummy(ImVec2(0, 4));
    if (ImGui::Button("Reset to defaults")) { D::Options def; std::snprintf(def.design, sizeof def.design, "%s", o.design); o = def; ch = true; }
    if (ch) Commit(o);
    return ch;
}

} // namespace okayappearance
