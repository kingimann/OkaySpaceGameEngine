#include "UIGalleryWindow.hpp"
#include "ConsolePanel.hpp"
#include "okay/Scene/UIGalleryIndex.hpp"
#include "okay/Components/UIElement.hpp"
#include "imgui.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>

// Written for this editor from the gallery index and the UI components' own fields. The previews are drawn with ImGui's draw list from the
// real widgets of a temporary scene (so a screen shows its real colours under the chosen theme) and from the style/theme data; they are
// close pictures, not the renderer's exact pixels.
namespace okay::editor {
namespace {

using namespace okay;
namespace G = UIGalleryIndex;
namespace S = UIStyleLib;

bool g_open = false;
std::function<bool(EditorState&, const std::string&)> g_adder;
char g_search[96] = "";
int g_kind = 0;            // 0 all, then Screens, Components, Styles, Themes, Icons
int g_category = 0;        // index into the categories of the chosen kind (0 = any)
std::string g_tag;
int g_theme = 0;           // the look for new screens: 0 = none (launcher colours), else index + 1 into the look list. There is no scene-wide theme.
std::string g_styleName;   // the style for new screens ("" = none)
int g_variant = 0;
int g_selected = -1;       // index into the current result list
std::string g_selectedId;
std::string g_status;

constexpr int kKinds[] = {G::kAll, G::kScreen, G::kComponent, G::kStyle, G::kTheme, G::kIcon};
const char* kKindLabels[] = {"Everything", "Screens", "Components", "Styles", "Looks", "Icons"};

ImU32 Col(const Color& c, float mul = 1.0f) {
    auto b = [](float v) { int i = (int)(v * 255.0f + 0.5f); return (unsigned)(i < 0 ? 0 : i > 255 ? 255 : i); };
    return IM_COL32(b(c.r), b(c.g), b(c.b), b(c.a * mul));
}

// ---- a drawn icon: the coverage picture, one filled box per run, so it looks like the in-game one
void DrawIconAt(ImDrawList* dl, const std::string& name, ImVec2 p, int px, ImU32 col) {
    const uilib::IconDef* d = uilib::FindIcon(name);
    if (!d) return;
    static std::map<std::pair<std::string, int>, std::vector<std::uint8_t>> cache;
    auto key = std::make_pair(name, px);
    auto it = cache.find(key);
    if (it == cache.end()) { if (cache.size() > 2000) cache.clear(); it = cache.emplace(key, uilib::RenderIcon(*d, px)).first; }
    const unsigned base = (col >> 24) & 0xFF;
    for (int y = 0; y < px; ++y) {
        int x = 0;
        while (x < px) {
            const std::uint8_t a = it->second[(std::size_t)y * (std::size_t)px + (std::size_t)x];
            if (!a) { ++x; continue; }
            int e = x + 1;
            while (e < px && it->second[(std::size_t)y * (std::size_t)px + (std::size_t)e] == a) ++e;
            const ImU32 c = (col & 0x00FFFFFF) | ((unsigned)(base * a / 255) << 24);
            dl->AddRectFilled(ImVec2(p.x + x, p.y + y), ImVec2(p.x + e, p.y + y + 1), c);
            x = e;
        }
    }
}

// ---- a screen preview: the widgets of a temporary scene, laid out for 1280 x 720 and drawn scaled
struct PItem { int kind; ImVec2 a, b; ImU32 fill, line, text; float radius, px; std::string label; float frac; };
struct Preview { std::vector<PItem> items; bool ok = false; };

void Collect(GameObject* g, std::vector<PItem>& out, float W, float H) {
    if (!g || !g->active) return;
    Vec2 o, sz;
    if (GetUIScreenRect(g, W, H, o, sz)) {
        PItem it{}; it.a = ImVec2(o.x, o.y); it.b = ImVec2(o.x + sz.x, o.y + sz.y); it.frac = 1.0f;
        if (auto* p = g->GetComponent<UIPanel>()) {
            it.kind = 0; it.fill = Col(p->color); it.line = p->borderWidth > 0 ? Col(p->borderColor) : 0; it.radius = p->shape == UIShape::Circle || p->shape == UIShape::Pill ? 9999.0f : p->cornerRadius;
            if (p->useGradient) it.fill = Col(Color(p->color.r * 0.5f + p->colorBottom.r * 0.5f, p->color.g * 0.5f + p->colorBottom.g * 0.5f, p->color.b * 0.5f + p->colorBottom.b * 0.5f, p->color.a));
            out.push_back(it);
        } else if (auto* b = g->GetComponent<UIButton>()) {
            it.kind = 1; it.fill = Col(b->color); it.line = b->borderWidth > 0 ? Col(b->borderColor) : 0; it.text = Col(b->textColor);
            it.radius = b->shape == UIShape::Circle || b->shape == UIShape::Pill ? 9999.0f : b->cornerRadius; it.px = b->fontScale * 8.0f; it.label = b->label;
            out.push_back(it);
            if (const std::string ic = uilib::IconNameOf(b->icon); !ic.empty()) { it.kind = 5; it.label = ic; it.a = ImVec2(o.x + 8, o.y + (sz.y - b->iconSize) * 0.5f); it.b = ImVec2(it.a.x + b->iconSize, it.a.y + b->iconSize); it.fill = it.text; out.push_back(it); }
        } else if (auto* t = g->GetComponent<TextRenderer>(); t && t->screenSpace) {
            it.kind = 2; it.text = Col(t->color); it.px = t->pixelSize * 8.0f; it.label = t->text; it.frac = (float)t->align;
            out.push_back(it);
        } else if (auto* pb = g->GetComponent<UIProgressBar>()) {
            it.kind = 3; it.fill = Col(pb->background); it.text = Col(pb->fill); it.frac = pb->value; it.radius = pb->cornerRadius; out.push_back(it);
        } else if (auto* sl = g->GetComponent<UISlider>()) {
            it.kind = 3; it.fill = Col(sl->background); it.text = Col(sl->fill); it.frac = (sl->value - sl->minValue) / std::max(0.0001f, sl->maxValue - sl->minValue); it.radius = 3; out.push_back(it);
        } else if (auto* tg = g->GetComponent<UIToggle>()) {
            it.kind = 4; it.fill = tg->on ? Col(tg->checkColor) : Col(tg->boxColor); it.radius = 9999.0f; it.label = tg->label; it.text = Col(tg->textColor); out.push_back(it);
        } else if (auto* in = g->GetComponent<UIInputField>()) {
            it.kind = 1; it.fill = Col(in->color); it.line = Col(in->borderColor, 0.5f); it.text = Col(in->placeholderColor); it.radius = in->cornerRadius; it.px = 14; it.label = in->placeholder; out.push_back(it);
        } else if (auto* dd = g->GetComponent<UIDropdown>()) {
            it.kind = 1; it.fill = Col(dd->color); it.line = Col(dd->borderColor); it.text = Col(dd->textColor); it.radius = dd->cornerRadius; it.px = 14; it.label = dd->options.empty() ? "" : dd->options[(std::size_t)std::max(0, std::min(dd->value, (int)dd->options.size() - 1))]; out.push_back(it);
        } else if (auto* tb = g->GetComponent<UITabs>()) {
            it.kind = 1; it.fill = Col(tb->background); it.text = Col(tb->textColor); it.radius = tb->cornerRadius; it.px = 14;
            for (std::size_t i = 0; i < tb->tabs.size(); ++i) { if (i) it.label += "   "; it.label += tb->tabs[i]; }
            out.push_back(it);
        } else if (auto* im = g->GetComponent<UIImage>()) {
            if (const std::string ic = uilib::IconNameOf(im->texture); !ic.empty()) { it.kind = 5; it.label = ic; it.fill = Col(im->color); out.push_back(it); }
        }
    }
    for (Transform* c : g->transform->Children()) if (c && c->gameObject) Collect(c->gameObject, out, W, H);
}

const Preview& PreviewOf(const std::string& id, int variant, int themeIdx, const std::string& styleName, const std::vector<S::ThemeInfo>& themes) {
    static std::map<std::string, Preview> cache;
    const std::string key = id + "|" + std::to_string(variant) + "|" + std::to_string(themeIdx) + "|" + styleName;
    auto it = cache.find(key);
    if (it != cache.end()) return it->second;
    if (cache.size() > 200) cache.clear();
    Preview& pv = cache[key];
    Scene sc("preview");
    if (GameObject* root = UIScreens::Build(sc, id, variant, themeIdx > 0 && themeIdx <= (int)themes.size() ? themes[(std::size_t)themeIdx - 1].name : std::string(), styleName)) {
        Collect(root, pv.items, 1280.0f, 720.0f);
        pv.ok = true;
    }
    return pv;
}

void DrawPreview(const Preview& pv, ImVec2 origin, float width) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float k = width / 1280.0f, h = 720.0f * k;
    dl->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + h), IM_COL32(24, 26, 32, 255));
    dl->PushClipRect(origin, ImVec2(origin.x + width, origin.y + h), true);
    auto P = [&](ImVec2 v) { return ImVec2(origin.x + v.x * k, origin.y + v.y * k); };
    for (const PItem& it : pv.items) {
        const ImVec2 a = P(it.a), b = P(it.b);
        const float rad = std::min(it.radius * k, std::min(b.x - a.x, b.y - a.y) * 0.5f);
        if (it.kind == 0 || it.kind == 1 || it.kind == 4) {
            if ((it.fill >> 24) > 0) dl->AddRectFilled(a, b, it.fill, rad);
            if (it.line) dl->AddRect(a, b, it.line, rad, 0, 1.0f);
        }
        if (it.kind == 3) {
            dl->AddRectFilled(a, b, it.fill, rad);
            dl->AddRectFilled(a, ImVec2(a.x + (b.x - a.x) * std::max(0.0f, std::min(1.0f, it.frac)), b.y), it.text, rad);
        }
        if (it.kind == 1 || it.kind == 2 || it.kind == 4) {
            if (!it.label.empty()) {
                const float fs = std::max(6.0f, it.px * k * 1.0f);
                ImFont* font = ImGui::GetFont();
                const ImVec2 ts = font->CalcTextSizeA(fs, 100000.0f, 0.0f, it.label.c_str());
                float x = a.x + 2.0f, y = a.y + ((b.y - a.y) - ts.y) * 0.5f;
                if (it.kind == 1 && it.px >= 14) x = a.x + ((b.x - a.x) - ts.x) * 0.5f;
                if (it.kind == 2) { if (it.frac == 1.0f) x = a.x + ((b.x - a.x) - ts.x) * 0.5f; else if (it.frac == 2.0f) x = b.x - ts.x - 2.0f; }
                if (it.kind == 4) x = b.x + 4.0f * k;
                dl->AddText(font, fs, ImVec2(x, y), it.text, it.label.c_str());
            }
        }
        if (it.kind == 5) {
            const int px = std::max(6, (int)((b.x - a.x)));
            DrawIconAt(dl, it.label, ImVec2(std::floor(a.x), std::floor(a.y)), std::min(px, 96), it.fill);
        }
    }
    dl->PopClipRect();
    ImGui::Dummy(ImVec2(width, h));
}

// ---- a style preview: a button and a panel in the look, from the style's own numbers (a close picture, not the renderer's pixels)
void DrawStylePreview(const S::StyleSpec& sp, const S::Palette& pal, float width) {
    const S::detail::Look L = S::detail::Compute(sp, pal);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 o = ImGui::GetCursorScreenPos();
    const float h = 110.0f;
    dl->AddRectFilled(o, ImVec2(o.x + width, o.y + h), Col(pal.panel));
    const float bw = std::min(width - 32.0f, 220.0f), bh = std::min(L.height * 1.2f, 70.0f);
    ImVec2 a(o.x + (width - bw) * 0.5f, o.y + (h - bh) * 0.5f), b(a.x + bw, a.y + bh);
    float rad = (L.shape == UIShape::Pill || L.shape == UIShape::Circle) ? bh * 0.5f : L.radius;
    if (L.shadow) for (int i = 0; i < 5; ++i) { const float g = (L.shadowSoft + 2.0f) * (float)i / 5.0f; dl->AddRectFilled(ImVec2(a.x - g + L.shadowOff.x, a.y - g + L.shadowOff.y), ImVec2(b.x + g + L.shadowOff.x, b.y + g + L.shadowOff.y), Col(L.shadowCol, 0.12f), rad + g); }
    if (L.outlineW > 0) dl->AddRect(ImVec2(a.x - L.outlineW, a.y - L.outlineW), ImVec2(b.x + L.outlineW, b.y + L.outlineW), Col(L.outline), rad + L.outlineW, 0, L.outlineW);
    if (L.shape == UIShape::Hexagon || L.shape == UIShape::Octagon || L.shape == UIShape::Parallelogram || L.shape == UIShape::Trapezoid) {
        const float c = L.shape == UIShape::Octagon ? std::min(L.radius, bh * 0.4f) : bh * 0.25f; ImVec2 pts[8]; int n = 0;
        if (L.shape == UIShape::Octagon) { pts[n++] = {a.x + c, a.y}; pts[n++] = {b.x - c, a.y}; pts[n++] = {b.x, a.y + c}; pts[n++] = {b.x, b.y - c}; pts[n++] = {b.x - c, b.y}; pts[n++] = {a.x + c, b.y}; pts[n++] = {a.x, b.y - c}; pts[n++] = {a.x, a.y + c}; }
        else if (L.shape == UIShape::Hexagon) { pts[n++] = {a.x + c, a.y}; pts[n++] = {b.x - c, a.y}; pts[n++] = {b.x, (a.y + b.y) * 0.5f}; pts[n++] = {b.x - c, b.y}; pts[n++] = {a.x + c, b.y}; pts[n++] = {a.x, (a.y + b.y) * 0.5f}; }
        else if (L.shape == UIShape::Parallelogram) { pts[n++] = {a.x + c, a.y}; pts[n++] = {b.x, a.y}; pts[n++] = {b.x - c, b.y}; pts[n++] = {a.x, b.y}; }
        else { pts[n++] = {a.x + c, a.y}; pts[n++] = {b.x - c, a.y}; pts[n++] = {b.x, b.y}; pts[n++] = {a.x, b.y}; }
        dl->AddConvexPolyFilled(pts, n, Col(L.fill));
        if (L.borderW > 0) dl->AddPolyline(pts, n, Col(L.border), ImDrawFlags_Closed, std::min(L.borderW, 4.0f));
    } else {
        dl->AddRectFilled(a, b, Col(L.fill), rad);
        if (L.gradient) dl->AddRectFilledMultiColor(a, b, Col(L.fill), Col(L.fill), Col(L.fillBottom), Col(L.fillBottom));
        if (L.highlight) dl->AddRectFilled(ImVec2(a.x + 2, a.y + 2), ImVec2(b.x - 2, a.y + (b.y - a.y) * 0.4f), IM_COL32(255, 255, 255, 40), rad);
        if (L.borderW > 0) dl->AddRect(a, b, Col(L.border), rad, 0, std::min(L.borderW, 4.0f));
    }
    const char* label = sp.style.empty() ? "Button" : sp.style.c_str();
    const ImVec2 ts = ImGui::CalcTextSize(label);
    dl->AddText(ImVec2(a.x + (bw - ts.x) * 0.5f, a.y + (bh - ts.y) * 0.5f), Col(L.text), label);
    ImGui::Dummy(ImVec2(width, h));
}

void Chip(const char* text, ImU32 col) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    dl->AddRectFilled(p, ImVec2(p.x + 22, p.y + 14), col, 3.0f);
    ImGui::Dummy(ImVec2(22, 14));
    ImGui::SameLine(0, 4);
    ImGui::TextUnformatted(text);
}

void Say(const std::string& msg) { g_status = msg; oked::ConsoleLog(msg); }

} // namespace

void OpenUIGalleryWindow() { g_open = true; }
bool& UIGalleryOpenFlag() { return g_open; }
void CurrentScreenLook(std::string& look, std::string& style) {
    static const std::vector<S::ThemeInfo> themes = S::Themes();
    look = g_theme > 0 && g_theme <= (int)themes.size() ? themes[(std::size_t)g_theme - 1].name : std::string();
    style = g_styleName;
}
void DrawStylePreviewWidget(const S::StyleSpec& spec, const S::Palette& palette, float width) { DrawStylePreview(spec, palette, width); }
void SetUIGalleryAdder(std::function<bool(EditorState&, const std::string&)> adder) { g_adder = std::move(adder); }

void DrawUIGalleryWindow(EditorState& ed) {
    if (!g_open) return;
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(std::max(260.0f, display.x * 0.18f), 70.0f), ImGuiCond_Appearing);
    ImGui::SetNextWindowSize(ImVec2(std::min(1000.0f, display.x * 0.7f), std::min(640.0f, display.y * 0.8f)), ImGuiCond_Appearing);
    if (!ImGui::Begin("UI Gallery", &g_open)) { ImGui::End(); return; }

    static const std::vector<S::ThemeInfo> themes = S::Themes();
    static const G::Counts counts = G::Count();
    ImGui::TextDisabled("%d screens (%d layouts), %d components, %d styles, %d looks, %d icons. Search, pick, preview, add.",
                        counts.screens, counts.screenVariants, counts.components, counts.styles, counts.themes, counts.icons);

    ImGui::SetNextItemWidth(260.0f);
    ImGui::InputTextWithHint("##search", "Search (name, tag, what it does)...", g_search, sizeof g_search);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(130.0f);
    const int prevKind = g_kind;
    ImGui::Combo("##kind", &g_kind, kKindLabels, 6);
    if (g_kind != prevKind) { g_category = 0; g_selected = -1; }
    const int kinds = kKinds[g_kind];
    const std::vector<std::string> cats = G::Categories(kinds);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(170.0f);
    {
        const std::string cur = g_category == 0 || g_category > (int)cats.size() ? "Any category" : cats[(std::size_t)g_category - 1];
        if (ImGui::BeginCombo("##cat", cur.c_str())) {
            if (ImGui::Selectable("Any category", g_category == 0)) g_category = 0;
            for (std::size_t i = 0; i < cats.size(); ++i) if (ImGui::Selectable(cats[i].c_str(), g_category == (int)i + 1)) g_category = (int)i + 1;
            ImGui::EndCombo();
        }
    }
    if (!g_tag.empty()) { ImGui::SameLine(); if (ImGui::SmallButton(("tag: " + g_tag + "  x").c_str())) g_tag.clear(); }

    G::Query q; q.text = g_search; q.kinds = kinds; q.tag = g_tag;
    if (g_category > 0 && g_category <= (int)cats.size()) q.category = cats[(std::size_t)g_category - 1];
    const std::vector<const G::Entry*> hits = G::Search(q);
    ImGui::Text("%d found", (int)hits.size());
    if (!g_status.empty()) { ImGui::SameLine(); ImGui::TextDisabled("   %s", g_status.c_str()); }
    ImGui::Separator();

    // ---- the list (left) and the details (right)
    ImGui::BeginChild("##list", ImVec2(ImGui::GetContentRegionAvail().x * 0.38f, 0), true);
    int cur = -1;
    for (std::size_t i = 0; i < hits.size(); ++i) if (hits[i]->id == g_selectedId && hits[i]->kind == (g_kind == 0 ? hits[i]->kind : hits[i]->kind)) { cur = (int)i; break; }
    ImGuiListClipper clip; clip.Begin((int)hits.size(), 22.0f);
    while (clip.Step())
        for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i) {
            const G::Entry& e = *hits[(std::size_t)i];
            ImGui::PushID(i);
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const bool sel = e.id == g_selectedId;
            if (ImGui::Selectable("##row", sel, 0, ImVec2(0, 22.0f))) { g_selectedId = e.id; g_variant = 0; }
            ImDrawList* dl = ImGui::GetWindowDrawList();
            float tx = p.x + 4.0f;
            if (e.kind == G::kIcon) { DrawIconAt(dl, e.name, ImVec2(p.x + 3, p.y + 3), 16, IM_COL32(230, 235, 245, 255)); tx = p.x + 26.0f; }
            dl->AddText(ImVec2(tx, p.y + 3.0f), IM_COL32(230, 235, 245, 255), e.name.c_str());
            if (g_kind == 0) { const char* kn = G::KindName(e.kind); dl->AddText(ImVec2(p.x + ImGui::GetContentRegionAvail().x - 70.0f, p.y + 3.0f), IM_COL32(130, 140, 160, 255), kn); }
            ImGui::PopID();
        }
    (void)cur;
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##detail", ImVec2(0, 0), true);
    const G::Entry* sel = nullptr;
    for (const G::Entry* e : hits) if (e->id == g_selectedId) { sel = e; break; }
    if (!sel) { ImGui::TextDisabled("Pick something on the left to see it here."); ImGui::EndChild(); ImGui::End(); return; }

    ImGui::TextUnformatted(sel->name.c_str());
    ImGui::SameLine(); ImGui::TextDisabled("  %s%s%s", G::KindName(sel->kind), sel->category.empty() ? "" : " / ", sel->category.c_str());
    ImGui::PushTextWrapPos(0.0f); ImGui::TextWrapped("%s", sel->desc.c_str()); ImGui::PopTextWrapPos();
    for (const std::string& t : uilib::Split(sel->tags, ',')) {   // a tag is a button: click it to filter by it
        if (t.empty()) continue;
        ImGui::SameLine(0, 4);
        if (ImGui::SmallButton(t.c_str())) g_tag = t;
    }
    ImGui::NewLine();
    const float w = ImGui::GetContentRegionAvail().x;
    const S::Palette pal = S::PaletteOf(nullptr);   // no scene-wide theme: a style draws from its widget's own look, else the launcher colours

    if (sel->kind == G::kScreen) {
        const UIScreens::Def* d = UIScreens::Find(sel->id);
        int vcount = d ? UIScreens::VariantCount(*d) : 1;
        {   // the Look and the Style of THIS screen: picked here, stored on the screen, never changed by anything else
            ImGui::SetNextItemWidth(190.0f);
            const std::string curLook = g_theme == 0 ? "(launcher colours)" : themes[(std::size_t)g_theme - 1].name;
            if (ImGui::BeginCombo("Look", curLook.c_str())) {
                if (ImGui::Selectable("(launcher colours)", g_theme == 0)) g_theme = 0;
                for (std::size_t i = 0; i < themes.size(); ++i) if (ImGui::Selectable(themes[i].name.c_str(), g_theme == (int)i + 1)) g_theme = (int)i + 1;
                ImGui::EndCombo();
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("The colours, corners and spacing of this screen. Each screen has its own; a menu from the menu uses this one too.");
            ImGui::SameLine(); ImGui::SetNextItemWidth(190.0f);
            if (ImGui::BeginCombo("Style", g_styleName.empty() ? "(none)" : g_styleName.c_str())) {
                if (ImGui::Selectable("(none)", g_styleName.empty())) g_styleName.clear();
                for (const auto& st : S::Styles().styles) if (ImGui::Selectable(st.name.c_str(), st.name == g_styleName)) g_styleName = st.name;
                ImGui::EndCombo();
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("The shape, fill, border and effect of this screen's buttons and panels.");
        }
        if (vcount > 1 && d) {
            ImGui::SetNextItemWidth(200.0f);
            if (g_variant >= vcount) g_variant = 0;
            if (ImGui::BeginCombo("Layout", d->variants[(std::size_t)g_variant].c_str())) {
                for (int v = 0; v < vcount; ++v) if (ImGui::Selectable(d->variants[(std::size_t)v].c_str(), v == g_variant)) g_variant = v;
                ImGui::EndCombo();
            }
        } else g_variant = 0;
        if (d && d->native.empty()) DrawPreview(PreviewOf(sel->id, g_variant, g_theme, g_styleName, themes), ImGui::GetCursorScreenPos(), std::min(w, 520.0f));
        else ImGui::TextDisabled("This older screen has no preview here; add it to see it in the Scene view.");
        if (ImGui::Button("Add to scene")) {
            ed.PushUndo();
            GameObject* g = UIScreens::Build(ed.scene(), sel->id, g_variant, g_theme > 0 ? themes[(std::size_t)g_theme - 1].name : std::string(), g_styleName);
            if (g) {
                ed.Select(g); ed.dirty = true; Say("Added screen: " + sel->name);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Add the Starter Pack (whole game UI)")) {
            ed.PushUndo();
            GameObject* g = UIScreens::BuildStarterPack(ed.scene(), g_theme > 0 ? themes[(std::size_t)g_theme - 1].name : std::string(), g_styleName);
            if (g) { ed.Select(g); ed.dirty = true; Say("Added the Starter Pack: press Play, then click Play, Settings, Pause..."); }
        }
    } else if (sel->kind == G::kComponent) {
        if (!sel->menu.empty() && g_adder) { if (ImGui::Button("Add to scene")) { if (g_adder(ed, sel->menu)) Say("Added " + sel->name); } }
        else ImGui::TextDisabled("Select an object, then Add Component > %s.", sel->name.c_str());
    } else if (sel->kind == G::kStyle) {
        static int size = 2, dens = 1;
        static const char* sizes[] = {"xs", "s", "m", "l", "xl"}; static const char* denss[] = {"compact", "cosy", "comfortable", "spacious"};
        ImGui::SetNextItemWidth(80); ImGui::Combo("Size", &size, sizes, 5); ImGui::SameLine();
        ImGui::SetNextItemWidth(130); ImGui::Combo("Density", &dens, denss, 4);
        const S::StyleSpec sp = S::SpecOf(sel->name, sizes[size], denss[dens]);
        DrawStylePreview(sp, pal, std::min(w, 420.0f));
        ImGui::TextDisabled("Shape %s, fill %s, border %s, effect %s; hover %s, press %s, disabled %s, focus %s.", sp.shape.c_str(), sp.fill.c_str(), sp.border.c_str(), sp.effect.c_str(), sp.hover.c_str(), sp.press.c_str(), sp.disabled.c_str(), sp.focus.c_str());
        GameObject* target = ed.selected();
        ImGui::BeginDisabled(!target);
        if (ImGui::Button("Apply to selected")) {
            ed.PushUndo();
            auto* sv = target->GetComponent<UIStyleVariant>(); if (!sv) sv = target->AddComponent<UIStyleVariant>();
            const int n = S::ApplyStyle(target, sp, S::PaletteFor(sv));
            sv->style = sel->name; sv->styleSize = sizes[size]; sv->styleDensity = denss[dens];
            ed.dirty = true; Say("Applied " + sel->name + " to " + std::to_string(n) + " widget(s) under " + target->name);
        }
        if (ImGui::Button("Apply to this screen")) {
            ed.PushUndo();
            GameObject* root = S::ScreenRootOf(target);
            const std::string look = root->GetComponent<UIStyleVariant>() ? root->GetComponent<UIStyleVariant>()->themeName : std::string();
            S::ApplyLook(root, look, sel->name, sizes[size], denss[dens]);
            ed.dirty = true; Say("Applied " + sel->name + " to the screen " + root->name);
        }
        ImGui::EndDisabled();
        if (!target) { ImGui::SameLine(); ImGui::TextDisabled("Select a UI object first."); }
        ImGui::SameLine();
        if (ImGui::Button("Randomise selected")) {
            if (target) { ed.PushUndo(); static unsigned seed = 1; const std::string n = S::RandomiseStyle(target, ++seed, pal); ed.dirty = true; Say("Random style: " + n); }
        }
    } else if (sel->kind == G::kTheme) {
        UITheme th; S::ApplyThemeData(th, sel->name);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 o = ImGui::GetCursorScreenPos(); const float tw = std::min(w, 460.0f);
        dl->AddRectFilledMultiColor(o, ImVec2(o.x + tw, o.y + 150), Col(th.panel), Col(th.panel), Col(th.panelBottom), Col(th.panelBottom));
        dl->AddText(ImVec2(o.x + 14, o.y + 12), Col(th.text), sel->name.c_str());
        dl->AddText(ImVec2(o.x + 14, o.y + 32), Col(th.muted), "Muted text looks like this");
        dl->AddRectFilled(ImVec2(o.x + 14, o.y + 64), ImVec2(o.x + 134, o.y + 96), Col(th.button), std::min(th.cornerRadius, 16.0f));
        dl->AddText(ImVec2(o.x + 38, o.y + 72), Col(th.buttonText), "Button");
        dl->AddRectFilled(ImVec2(o.x + 146, o.y + 64), ImVec2(o.x + 266, o.y + 96), Col(th.danger), std::min(th.cornerRadius, 16.0f));
        dl->AddRectFilled(ImVec2(o.x + 278, o.y + 64), ImVec2(o.x + 398, o.y + 96), Col(th.success), std::min(th.cornerRadius, 16.0f));
        dl->AddRectFilled(ImVec2(o.x + 14, o.y + 112), ImVec2(o.x + 14 + 300, o.y + 124), Col(th.disabled), 6.0f);
        dl->AddRectFilled(ImVec2(o.x + 14, o.y + 112), ImVec2(o.x + 14 + 180, o.y + 124), Col(th.accent), 6.0f);
        ImGui::Dummy(ImVec2(tw, 150));
        ImGui::TextDisabled("Corner radius %.0f, border %.0f, padding %.0f, font %s, speed %.0f. Sounds: %s / %s / %s.", th.cornerRadius, th.borderWidth, th.padding,
                            th.fontPath.empty() ? "built-in pixel font" : "DejaVu Sans", th.transitionSpeed, S::ThemeSound(sel->name, "hover").c_str(), S::ThemeSound(sel->name, "click").c_str(), S::ThemeSound(sel->name, "open").c_str());
        GameObject* target = ed.selected();
        ImGui::BeginDisabled(!target);
        if (ImGui::Button("Apply to this screen")) { ed.PushUndo(); GameObject* root = S::ScreenRootOf(target); auto* sv = root->GetComponent<UIStyleVariant>(); if (S::ApplyLook(root, sel->name, sv ? sv->style : std::string(), sv ? sv->styleSize : std::string(), sv ? sv->styleDensity : std::string())) { ed.dirty = true; Say("Look " + sel->name + " on the screen " + root->name); } }
        ImGui::SameLine();
        if (ImGui::Button("Apply to selection")) { ed.PushUndo(); if (S::ApplyLook(target, sel->name)) { ed.dirty = true; Say("Look " + sel->name + " on " + target->name + " and its children"); } }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Use for new screens")) { for (std::size_t i = 0; i < themes.size(); ++i) if (themes[i].name == sel->name) g_theme = (int)i + 1; Say("New screens will use the look " + sel->name); }
        ImGui::TextDisabled("Only what you pick changes. Script: ui_set_look(\"Pause Menu\", \"%s\")", sel->name.c_str());
    } else if (sel->kind == G::kIcon) {
        const ImVec2 o = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        static float tint[3] = {0.9f, 0.93f, 1.0f}; static int px = 64;
        ImGui::ColorEdit3("Tint", tint, ImGuiColorEditFlags_NoInputs); ImGui::SameLine(); ImGui::SetNextItemWidth(160); ImGui::SliderInt("Size", &px, 12, 128);
        const ImVec2 o2 = ImGui::GetCursorScreenPos();
        dl->AddRectFilled(o2, ImVec2(o2.x + 140, o2.y + 140), IM_COL32(32, 35, 44, 255), 8.0f);
        DrawIconAt(dl, sel->name, ImVec2(o2.x + (140 - px) * 0.5f, o2.y + (140 - px) * 0.5f), px, IM_COL32((int)(tint[0] * 255), (int)(tint[1] * 255), (int)(tint[2] * 255), 255));
        (void)o;
        ImGui::Dummy(ImVec2(140, 140));
        ImGui::TextUnformatted(sel->id.c_str());
        if (ImGui::Button("Copy name")) { ImGui::SetClipboardText(sel->id.c_str()); Say("Copied " + sel->id); }
        GameObject* target = ed.selected();
        ImGui::SameLine();
        ImGui::BeginDisabled(!target || !(target->GetComponent<UIImage>() || target->GetComponent<UIButton>()));
        if (ImGui::Button("Use on selected Image / Button")) {
            ed.PushUndo();
            if (auto* im = target->GetComponent<UIImage>()) { im->texture = sel->id; im->color = Color(tint[0], tint[1], tint[2], 1.0f); }
            else if (auto* b = target->GetComponent<UIButton>()) { b->icon = sel->id; if (b->iconSize <= 0.0f) b->iconSize = 20.0f; }
            ed.dirty = true; Say("Set the icon of " + target->name + " to " + sel->name);
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Select an Image or a Button in the Hierarchy first.");
    }
    ImGui::EndChild();
    ImGui::End();
}

} // namespace okay::editor
