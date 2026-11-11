#include "UIRuntimeTools.hpp"
#include "ConsolePanel.hpp"
#include "ExampleScenes.hpp"
#include "imgui.h"
#include "okay/Scene/UIGallery.hpp"
#include "okay/Scene/PropertyRegistry.hpp"
#include <algorithm>
#include <cstring>
#include <filesystem>

using namespace okay;
using okay::editor::EditorState;

namespace oked {

namespace {

ImU32 U32(const Color& c, float alpha = 1.0f) {
    auto b = [](float v) { return (int)(std::min(1.0f, std::max(0.0f, v)) * 255.0f + 0.5f); };
    return IM_COL32(b(c.r), b(c.g), b(c.b), b(c.a * alpha));
}

// Dim help text that wraps inside the Inspector instead of running off its edge.
void Hint(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}

// "cornerRadius" -> "Corner Radius"
std::string Nice(const std::string& n) {
    std::string out;
    for (std::size_t i = 0; i < n.size(); ++i) {
        const char c = n[i];
        if (i == 0) out.push_back((char)std::toupper((unsigned char)c));
        else {
            if (std::isupper((unsigned char)c) && !std::isupper((unsigned char)n[i - 1])) out.push_back(' ');
            out.push_back(c);
        }
    }
    return out;
}

// One row per registered property of `c`, with the right control for its type.
bool DrawProps(Component& c, EditorState& ed) {
    auto info = PropertyRegistry::FindFor(c);
    if (!info) return false;
    bool any = false;
    for (const PropertyDesc& d : info->properties) {
        if (d.IsHidden() || d.IsReadOnly()) continue;
        const PropertyValue v = d.Get(c);
        if (!v.IsValid()) continue;
        const std::string label = Nice(d.name) + "##rt" + d.name;
        PropertyValue nv = v;
        bool changed = false;
        switch (d.type) {
            case PropertyType::Bool: { bool b = v.AsBool(); if (ImGui::Checkbox(label.c_str(), &b)) { nv = PropertyValue::MakeBool(b); changed = true; } break; }
            case PropertyType::Int: {
                int i = (int)v.AsInt();
                const int lo = d.hasRange ? (int)d.rangeMin : 0, hi = d.hasRange ? (int)d.rangeMax : 0;
                if (ImGui::DragInt(label.c_str(), &i, 1.0f, lo, hi)) { nv = PropertyValue::MakeInt(i); changed = true; }
                break;
            }
            case PropertyType::Float: {
                float f = v.AsFloat();
                const float lo = d.hasRange ? (float)d.rangeMin : 0.0f, hi = d.hasRange ? (float)d.rangeMax : 0.0f;
                if (ImGui::DragFloat(label.c_str(), &f, d.step > 0 ? (float)d.step : 0.1f, lo, hi)) { nv = PropertyValue::MakeFloat(f); changed = true; }
                break;
            }
            case PropertyType::Vec2: {
                float a[2] = {v.AsVec2().x, v.AsVec2().y};
                if (ImGui::DragFloat2(label.c_str(), a, d.step > 0 ? (float)d.step : 1.0f)) { nv = PropertyValue::MakeVec2(Vec2{a[0], a[1]}); changed = true; }
                break;
            }
            case PropertyType::Color: {
                Color col = v.AsColor();
                float a[4] = {col.r, col.g, col.b, col.a};
                if (ImGui::ColorEdit4(label.c_str(), a, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar)) { nv = PropertyValue::MakeColor(Color(a[0], a[1], a[2], a[3])); changed = true; }
                break;
            }
            case PropertyType::Enum: {
                int e = v.AsEnum();
                std::vector<const char*> names;
                for (const auto& s : d.enumOptions) names.push_back(s.c_str());
                if (!names.empty() && ImGui::Combo(label.c_str(), &e, names.data(), (int)names.size())) { nv = PropertyValue::MakeEnum(e); changed = true; }
                break;
            }
            case PropertyType::String:
            case PropertyType::AssetPath: {
                char buf[256];
                std::snprintf(buf, sizeof buf, "%s", v.AsString().c_str());
                if (ImGui::InputText(label.c_str(), buf, sizeof buf)) {
                    nv = d.type == PropertyType::String ? PropertyValue::MakeString(buf) : PropertyValue::MakeAssetPath(buf);
                    changed = true;
                }
                if (d.type == PropertyType::AssetPath && ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("ASSET_PATH")) {
                        nv = PropertyValue::MakeAssetPath((const char*)p->Data);
                        changed = true;
                    }
                    ImGui::EndDragDropTarget();
                }
                break;
            }
            default: continue;
        }
        if (ImGui::IsItemActivated()) ed.PushUndo();
        if (!d.tooltip.empty() && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", d.tooltip.c_str());
        if (changed && d.Set(c, nv)) { ed.dirty = true; any = true; }
    }
    return any;
}

// Chips of the six roles drawn from a theme: the real component values, not a guess.
struct RoleChips {
    Scene scene{"role-chips"};
    GameObject* chip[UITheme::kVariantCount] = {};
    RoleChips() {
        scene.physicsEnabled = false;
        for (int v = 0; v < UITheme::kVariantCount; ++v) {
            chip[v] = scene.CreateGameObject(UITheme::VariantName(v));
            auto* b = chip[v]->AddComponent<UIButton>();
            b->size = {100, 30};
            chip[v]->AddComponent<UIStyleVariant>()->variant = v;
        }
    }
};

UITheme& PreviewTheme() { static UITheme t; return t; }

void DrawRoleChip(ImDrawList* dl, ImVec2 a, ImVec2 b, const UIButton& btn, const char* label) {
    const float r = btn.shape == UIShape::Pill ? (b.y - a.y) * 0.5f : std::min(btn.cornerRadius, (b.y - a.y) * 0.5f);
    dl->AddRectFilled(a, b, U32(btn.color), r);
    if (btn.borderWidth > 0.0f) dl->AddRect(a, b, U32(btn.borderColor), r, 0, std::min(btn.borderWidth, 3.0f));
    const ImVec2 ts = ImGui::CalcTextSize(label);
    dl->AddText(ImVec2(a.x + (b.x - a.x - ts.x) * 0.5f, a.y + (b.y - a.y - ts.y) * 0.5f), U32(btn.textColor), label);
}

std::string ThemeFolder(EditorState& ed) {
    return ed.projectDir().empty() ? std::string() : ed.projectDir() + "/Assets/UI";
}

} // namespace

bool DrawThemePresetCombo(UITheme& th, EditorState& ed) {
    int which = th.preset;
    if (which < 0 || which >= UITheme::kPresetCount) which = 0;
    std::vector<const char*> names;
    for (int i = 0; i < UITheme::kPresetCount; ++i) names.push_back(UITheme::PresetName(i));
    if (ImGui::Combo("Built-in Theme##uitheme", &which, names.data(), (int)names.size())) {
        ed.PushUndo();
        th.ApplyPreset(which);
        ed.dirty = true;
        return true;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Twelve looks fill the colours below. Custom keeps whatever you edit. Apply writes them onto the widgets.");
    return false;
}

void DrawUIThemeWindowBody(EditorState& ed) {
    static int sel = (int)UITheme::Preset::ModernFlat;
    static RoleChips chips;
    static std::string status;
    UITheme& pv = PreviewTheme();
    if (pv.preset != sel && sel >= 1) pv.ApplyPreset(sel);

    ImGui::TextDisabled("Pick a look, then apply it to your whole UI.");
    ImGui::Spacing();

    // ---- The 12 looks as cards ----
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const int cols = 4;
    const float cw = std::max(70.0f, (ImGui::GetContentRegionAvail().x - spacing * (cols - 1)) / cols), ch = 58.0f;
    for (int i = 1; i < UITheme::kPresetCount; ++i) {
        if ((i - 1) % cols) ImGui::SameLine();
        UITheme t; t.ApplyPreset(i);
        ImGui::PushID(i);
        const ImVec2 a = ImGui::GetCursorScreenPos();
        const ImVec2 b(a.x + cw, a.y + ch);
        if (ImGui::InvisibleButton("##card", ImVec2(cw, ch))) { sel = i; pv.ApplyPreset(i); }
        const bool hov = ImGui::IsItemHovered();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float rad = std::min(t.cornerRadius, 12.0f);
        // a light look on a dark editor needs an edge; glass needs something behind it
        dl->AddRectFilled(a, b, IM_COL32(40, 44, 56, 255), rad);
        dl->AddRectFilled(ImVec2(a.x + 3, a.y + 3), ImVec2(b.x - 3, b.y - 3), U32(t.panel), std::max(0.0f, rad - 2.0f));
        dl->AddRectFilled(ImVec2(a.x + 10, a.y + 32), ImVec2(a.x + 10 + cw * 0.42f, a.y + 48), U32(t.button), std::min(rad, 7.0f));
        dl->AddRectFilled(ImVec2(b.x - 24, a.y + 36), ImVec2(b.x - 12, a.y + 48), U32(t.accent), 3.0f);
        dl->AddText(ImVec2(a.x + 10, a.y + 9), U32(t.text), UITheme::PresetName(i));
        if (sel == i) dl->AddRect(a, b, IM_COL32(90, 170, 255, 255), rad, 0, 2.0f);
        else if (hov) dl->AddRect(a, b, IM_COL32(255, 255, 255, 120), rad, 0, 1.0f);
        ImGui::PopID();
    }
    ImGui::Spacing();

    // ---- Preview of the chosen look: panel and every role ----
    {
        const float pw = ImGui::GetContentRegionAvail().x, ph = 128.0f;
        const ImVec2 a = ImGui::GetCursorScreenPos(), b(a.x + pw, a.y + ph);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(a, b, IM_COL32(30, 34, 46, 255), 6.0f);
        const ImVec2 pa(a.x + 12, a.y + 10), pb(b.x - 12, b.y - 10);
        const float rad = std::min(pv.cornerRadius, 16.0f);
        if (pv.shadow) dl->AddRectFilled(ImVec2(pa.x + pv.shadowOffset.x * 0.4f, pa.y + pv.shadowOffset.y * 0.4f), ImVec2(pb.x + pv.shadowOffset.x * 0.4f, pb.y + pv.shadowOffset.y * 0.4f), U32(pv.shadowCol, 0.5f), rad);
        if (pv.gradient) {
            dl->AddRectFilledMultiColor(pa, pb, U32(pv.panel), U32(pv.panel), U32(pv.panelBottom), U32(pv.panelBottom));
        } else dl->AddRectFilled(pa, pb, U32(pv.panel), rad);
        if (pv.borderWidth > 0.0f) dl->AddRect(pa, pb, U32(pv.border), rad, 0, std::min(pv.borderWidth, 3.0f));
        dl->AddText(ImVec2(pa.x + 12, pa.y + 8), U32(pv.text), "Roles");
        const int roles[] = {1, 2, 3, 4, 5, 6};
        const float chipW = (pb.x - pa.x - 24 - 5 * 8) / 6.0f;
        for (int k = 0; k < 6; ++k) {
            GameObject* g = chips.chip[roles[k]];
            pv.ApplyTo(g);
            const ImVec2 ca(pa.x + 12 + k * (chipW + 8), pa.y + 34), cb(ca.x + chipW, ca.y + 30);
            DrawRoleChip(dl, ca, cb, *g->GetComponent<UIButton>(), UITheme::VariantName(roles[k]));
        }
        // a bar and a slider in the look's colours
        const float by = pa.y + 82;
        const float bw = (pb.x - pa.x - 24) * 0.5f;
        dl->AddRectFilled(ImVec2(pa.x + 12, by), ImVec2(pa.x + 12 + bw, by + 10), U32(pv.panelBottom), 5.0f);
        dl->AddRectFilled(ImVec2(pa.x + 12, by), ImVec2(pa.x + 12 + bw * 0.65f, by + 10), U32(pv.success), 5.0f);
        dl->AddRectFilled(ImVec2(pa.x + 24 + bw, by + 4), ImVec2(pb.x - 12, by + 8), U32(pv.panelBottom), 2.0f);
        dl->AddRectFilled(ImVec2(pa.x + 24 + bw, by + 4), ImVec2(pa.x + 24 + bw + bw * 0.4f, by + 8), U32(pv.accent), 2.0f);
        dl->AddCircleFilled(ImVec2(pa.x + 24 + bw + bw * 0.4f, by + 6), 6.0f, U32(pv.buttonText), 16);
        ImGui::Dummy(ImVec2(pw, ph));
    }
    ImGui::Spacing();

    // ---- Apply ----
    Scene& scene = ed.scene();
    int widgets = 0;
    for (const auto& o : scene.Objects()) if (GetUIRect(o.get()).valid) ++widgets;
    const bool hasSel = ed.selected() != nullptr;
    ImGui::BeginDisabled(widgets == 0);
    if (ImGui::Button("Apply to all UI", ImVec2(150, 0))) {
        ed.PushUndo();
        UITheme* th = UIGallery::EnsureTheme(scene, sel);
        if (sel == 0) { UIThemeFromText(*th, UIThemeToText(pv)); th->global = true; }   // a look loaded from a file: use its values, not a preset
        th->Apply();
        ed.dirty = true;
        status = "Styled " + std::to_string(widgets) + " widget(s) as " + UITheme::PresetName(sel);
        ConsoleLog(status);
    }
    ImGui::EndDisabled();
    if (widgets == 0 && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("This scene has no UI yet. Add some from GameObject > UI.");
    ImGui::SameLine();
    ImGui::BeginDisabled(!hasSel);
    if (ImGui::Button("Apply to selected", ImVec2(150, 0))) {
        ed.PushUndo();
        UITheme t; t.ApplyPreset(sel);
        if (sel == 0) UIThemeFromText(t, UIThemeToText(pv));   // a look loaded from a file
        t.ApplyTo(ed.selected());
        ed.dirty = true;
        status = std::string("Styled ") + ed.selected()->name + " and what is inside it";
        ConsoleLog(status);
    }
    ImGui::EndDisabled();
    if (widgets > 0) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("The look lives on the Canvas as a UI Theme: edit it there, or give a widget a role with UI Style Variant.");
        ImGui::PopStyleColor();
    }
    if (!status.empty()) ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.6f, 1.0f), "%s", status.c_str());

    // ---- Theme files ----
    ImGui::Spacing();
    ImGui::SeparatorText("Share a look");
    const std::string folder = ThemeFolder(ed);
    if (folder.empty()) {
        ImGui::TextDisabled("Open a project to save looks as files.");
    } else {
        if (ImGui::Button("Save this look to the project")) {
            std::error_code ec;
            std::filesystem::create_directories(folder, ec);
            std::string nm = UITheme::PresetName(sel);
            for (char& c : nm) if (c == ' ') c = '_';
            std::string err;
            if (SaveUIThemeAsset(pv, folder + "/" + nm + ".okaytheme", &err)) { status = "Saved Assets/UI/" + nm + ".okaytheme"; ConsoleLog(status); }
            else status = "Could not save: " + err;
        }
        std::error_code ec;
        bool any = false;
        if (std::filesystem::is_directory(folder, ec))
            for (const auto& e : std::filesystem::directory_iterator(folder, ec)) {
                if (e.path().extension() != ".okaytheme") continue;
                if (!any) { ImGui::TextDisabled("Saved looks"); any = true; }
                const std::string stem = e.path().stem().string();
                if (ImGui::Button((stem + "##file").c_str())) {
                    std::string err;
                    if (LoadUIThemeAsset(pv, e.path().string(), &err)) { sel = 0; status = "Loaded " + stem + ". Apply it above."; }
                    else status = "Could not load: " + err;
                }
                ImGui::SameLine();
            }
        if (any) ImGui::NewLine();
    }
}

bool DrawRuntimeUIComponentInspector(Component* comp, EditorState& ed, Component** toRemove, const UIInspectorHooks& hooks) {
    if (auto* sv = dynamic_cast<UIStyleVariant*>(comp)) {
        if (hooks.header("UI Style Variant", sv, toRemove, true)) {
            std::vector<const char*> names;
            for (int i = 0; i < UITheme::kVariantCount; ++i) names.push_back(UITheme::VariantName(i));
            int v = std::clamp(sv->variant, 0, UITheme::kVariantCount - 1);
            if (ImGui::Combo("Role##uisv", &v, names.data(), (int)names.size())) {
                ed.PushUndo();
                sv->variant = v;
                if (UITheme* th = UITheme::Find(&ed.scene(), sv->themeName)) th->ApplyTo(sv->gameObject);
                ed.dirty = true;
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("How this widget stands out in the theme: Primary, Secondary, Danger, Ghost, Outline or Pill.");
            char nb[64]; std::snprintf(nb, sizeof nb, "%s", sv->themeName.c_str());
            if (ImGui::InputText("Theme Name##uisv", nb, sizeof nb)) { sv->themeName = nb; ed.dirty = true; }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Leave empty to use the scene's first UI Theme.");
            Hint("Applied when the theme is applied. Change the look and every role follows.");
        }
        return true;
    }
    if (auto* dn = dynamic_cast<UIDamageNumbers*>(comp)) {
        if (hooks.header("UI Damage Numbers", dn, toRemove, true)) {
            DrawProps(*dn, ed);
            Hint("Floating combat text. A button's On Click can call Hit, Crit, Heal, Miss or Info on this object; scripts use ui_damage_number(\"-25\", x, y).");
        }
        return true;
    }
    if (auto* db = dynamic_cast<UIDialogueBox*>(comp)) {
        if (hooks.header("UI Dialogue Box", db, toRemove, true)) {
            DrawProps(*db, ed);
            Hint("Finds children named Speaker, Body, Choice1-3, Next and Box. Scripts: ui_dialogue_show(\"Mara\", \"Hello\", \"Yes\", \"No\"), ui_dialogue_choice().");
        }
        return true;
    }
    if (auto* ib = dynamic_cast<UIInventoryBind*>(comp)) {
        if (hooks.header("UI Inventory Bind", ib, toRemove, true)) {
            DrawProps(*ib, ed);
            if (!ib->gameObject->GetComponent<UISlotGrid>()) Hint("Put this on the object with the UI Slot Grid: its slots then show the items of an Inventory.");
            else if (!ib->FindInventory()) Hint("No Inventory found. Add an Inventory to the Player (or name another object above).");
            else Hint("The slots show the items of the Inventory, the stack size after the name. Click a slot, then ask SelectedItem().");
        }
        return true;
    }
    return false;
}

void DrawUIAddMenuExtras(EditorState& ed, GameObject* parent, bool& created) {
    auto add = [&](const char* label, const char* tip, auto fn) {
        if (ImGui::MenuItem(label)) {
            ed.PushUndo();
            GameObject* g = fn();
            ed.Select(g); ed.dirty = true; created = true;
            ConsoleLog(std::string("Added UI: ") + label);
        }
        if (tip && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
    };
    Scene& sc = ed.scene();
    ImGui::TextDisabled("Game pieces");
    add("Window", "A panel with a title bar you can drag, and a close button.", [&] {
        GameObject* canvas = parent ? parent : UITemplates::EnsureCanvas(sc);
        GameObject* w = UIGallery::AddWindow(sc, canvas, "Window", "Window", {200, 140}, {320, 220});
        UITemplates::detail::Text(sc, w, "Body", "Put your content here.", UIAnchor::TopLeft, {16, 52}, {288, 40}, 14.0f, UITemplates::slate::Dim());
        return w;
    });
    add("Radio Buttons", "Three round buttons where only one can be on.", [&] {
        using namespace UITemplates;
        GameObject* row = UITemplates::detail::Group(sc, parent ? parent : UITemplates::EnsureCanvas(sc), "Radio Buttons");
        const char* n[] = {"Easy", "Normal", "Hard"};
        for (int i = 0; i < 3; ++i) {
            GameObject* r = UITemplates::detail::Child(sc, row, n[i]);
            auto* t = r->AddComponent<UIToggle>();
            t->anchor = UIAnchor::TopLeft; t->position = {24.0f, 24.0f + i * 36.0f}; t->size = {24, 24};
            t->style = UIToggle::Style::Radio; t->radioGroup = "Choice"; t->label = n[i]; t->on = (i == 1);
        }
        return row;
    });
    add("Damage Numbers", "Floating numbers for hits and heals. Buttons call Hit, Crit and Heal on it.", [&] {
        GameObject* g = UITemplates::detail::Child(sc, parent ? parent : UITemplates::EnsureCanvas(sc), "Damage Numbers");
        g->AddComponent<UIDamageNumbers>();
        return g;
    });
    add("Nameplate", "A name, level and a health bar that follows the Health Stat (the player's health). The name and level are plain text you edit.", [&] { return UIGallery::AddNameplate(sc, false); });
    add("Nameplate (in the world)", "The same plate as a 3D object that faces the camera: make it a child of a character. Its bar follows the Health Stat.", [&] { return UIGallery::AddNameplate(sc, true); });
    add("Inventory", "A bag screen with a grid of slots.", [&] { return UIGallery::AddInventoryScreen(sc); });
    add("Dialogue", "Speaker, typing text and choice buttons. Scripts call ui_dialogue_show().", [&] { return UIGallery::AddDialogueScreen(sc); });
}

void OpenUIGalleryExample(EditorState& ed, bool portrait) {
    std::string where, err;
    if (!BuildUIGalleryExample(ed, portrait, &where, &err)) ConsoleLog("Could not save the example into the project: " + err, 1);
    ConsoleLog(std::string(portrait ? "Opened the UI Gallery for phones. Press Play, pick a phone in the Game view, and scroll."
                                    : "Opened the UI Gallery. Press Play to try the widgets; pick a theme at the top right.") +
               (where.empty() ? std::string() : " Saved as " + where + "."));
}

void DrawExamplesMenu(EditorState& ed, const std::function<void(const std::function<void()>&)>& afterSavePrompt) {
    if (ImGui::BeginMenu("Examples")) {
        if (ImGui::MenuItem("UI Gallery")) afterSavePrompt([&ed] { OpenUIGalleryExample(ed, false); });
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Every in-game UI widget in one scene, in twelve looks. Press Play to try them.");
        if (ImGui::MenuItem("UI Gallery (phone)")) afterSavePrompt([&ed] { OpenUIGalleryExample(ed, true); });
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("The same widgets in one scrolling column on a portrait canvas. Pick a phone in the Game view.");
        ImGui::EndMenu();
    }
}

} // namespace oked
