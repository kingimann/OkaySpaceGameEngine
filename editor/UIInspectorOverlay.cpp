#include "UIInspectorOverlay.hpp"
#include "imgui.h"
#include <cstring>
#include <string>

using namespace okay;
using okay::editor::EditorState;

namespace oked {

namespace {
bool EditColor(const char* label, Color& c, EditorState& ed) {
    float v[4] = {c.r, c.g, c.b, c.a};
    if (!ImGui::ColorEdit4(label, v)) return false;
    c = {v[0], v[1], v[2], v[3]};
    ed.dirty = true;
    return true;
}

bool EditText(const char* label, std::string& s, EditorState& ed) {
    char buf[256];
    std::strncpy(buf, s.c_str(), sizeof(buf) - 1); buf[sizeof(buf) - 1] = '\0';
    if (!ImGui::InputText(label, buf, sizeof(buf))) return false;
    s = buf;
    ed.dirty = true;
    return true;
}

// One line per entry in a multiline box ("-" = separator).
void EditLines(const char* label, std::vector<std::string>& lines, EditorState& ed) {
    std::string joined;
    for (std::size_t i = 0; i < lines.size(); ++i) { if (i) joined += '\n'; joined += lines[i]; }
    static char buf[4096];
    std::strncpy(buf, joined.c_str(), sizeof(buf) - 1); buf[sizeof(buf) - 1] = '\0';
    if (!ImGui::InputTextMultiline(label, buf, sizeof(buf), ImVec2(-1.0f, ImGui::GetTextLineHeight() * 5.0f))) return;
    lines.clear();
    std::string cur;
    for (const char* c = buf; ; ++c) {
        if (*c == '\n' || *c == '\0') { if (!cur.empty()) lines.push_back(cur); cur.clear(); if (!*c) break; }
        else cur += *c;
    }
    ed.dirty = true;
}
} // namespace

void DrawUIOverlayComponentInspector(Component* comp, EditorState& ed, Component** toRemove,
                                     const UIInspectorHooks& hooks) {
    if (auto* md = dynamic_cast<UIModal*>(comp)) {
        if (hooks.header("UI Modal", md, toRemove, true)) {
            if (ImGui::Checkbox("Dim Background##umd", &md->dimBackground)) ed.dirty = true;
            if (md->dimBackground) EditColor("Dim Color##umd", md->dimColor, ed);
            if (ImGui::Checkbox("Escape / B Closes##umd", &md->closeOnCancel)) ed.dirty = true;
            if (ImGui::Checkbox("Click Outside Closes##umd", &md->closeOnClickOutside)) ed.dirty = true;
            ImGui::TextDisabled("While shown, only widgets inside this object take input.");
            ImGui::TextDisabled("Buttons: On Click -> this object, Open / Close.");
            // grok/ui-toast
            ImGui::Separator();
            if (ImGui::Checkbox("Style Card##umd", &md->styleCard)) ed.dirty = true;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Restyle children named Card, Title, Message, ConfirmButton and CancelButton.");
            if (md->styleCard) {
                if (ImGui::DragFloat("Card Radius##umd", &md->cardRadius, 0.2f, 0.0f, 80.0f)) ed.dirty = true;
                EditColor("Card##umd", md->cardColor, ed);
                EditColor("Title Color##umd", md->titleColor, ed);
                EditColor("Body Color##umd", md->bodyColor, ed);
                EditColor("Confirm Color##umd", md->confirmColor, ed);
                EditColor("Cancel Color##umd", md->cancelColor, ed);
                EditText("Title Text##umd", md->titleText, ed);
                EditText("Message Text##umd", md->bodyText, ed);
                EditText("Confirm Label##umd", md->confirmText, ed);
                EditText("Cancel Label##umd", md->cancelText, ed);
                ImGui::TextDisabled("Empty text leaves the label that was already on the widget.");
            }
        }
        return;
    }
    if (auto* toast = dynamic_cast<UIToast*>(comp)) {
        if (hooks.header("UI Toast", toast, toRemove, true)) {
            const char* corners[] = {"Top Right", "Top Left", "Bottom Right", "Bottom Left", "Top Center"};
            if (ImGui::Combo("Corner##toast", &toast->corner, corners, 5)) ed.dirty = true;
            if (ImGui::DragFloat("Duration##toast", &toast->duration, 0.05f, 0.05f, 60.0f)) ed.dirty = true;
            if (ImGui::DragInt("Max Stack##toast", &toast->maxStack, 0.1f, 1, 12)) ed.dirty = true;
            if (ImGui::DragFloat("Margin##toast", &toast->margin, 0.5f, 0.0f, 400.0f)) ed.dirty = true;
            if (ImGui::DragFloat("Gap##toast", &toast->gap, 0.25f, 0.0f, 80.0f)) ed.dirty = true;
            if (ImGui::DragFloat("Width##toast", &toast->width, 1.0f, 80.0f, 800.0f)) ed.dirty = true;
            if (ImGui::DragFloat("Height##toast", &toast->height, 0.5f, 32.0f, 240.0f)) ed.dirty = true;
            if (ImGui::Checkbox("Show Icon##toast", &toast->showIcon)) ed.dirty = true;
            ImGui::TextDisabled("toast_push(title, body, kind). Kind 0 info, 1 success, 2 warning, 3 error.");
            ImGui::TextDisabled("A click dismisses that toast and fires on_click on this object.");
        }
        return;
    }
    if (auto* cm = dynamic_cast<UIContextMenu*>(comp)) {
        if (hooks.header("UI Context Menu", cm, toRemove, true)) {
            EditLines("Items##ucm", cm->items, ed);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("One item per line; a line with just - is a separator.");
            if (ImGui::Checkbox("Anywhere##ucm", &cm->anywhere)) ed.dirty = true;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Open on a right-click anywhere no widget menu claims (else only on this widget).");
            if (ImGui::DragFloat("Width##ucm", &cm->width, 1.0f, 40.0f, 1000.0f)) ed.dirty = true;
            if (ImGui::DragFloat("Item Height##ucm", &cm->itemHeight, 0.5f, 10.0f, 200.0f)) ed.dirty = true;
            if (ImGui::DragFloat("Text Size##ucm", &cm->textSize, 0.05f, 0.5f, 8.0f)) ed.dirty = true;
            EditColor("Background##ucm", cm->background, ed);
            EditColor("Hover##ucm", cm->hoverColor, ed);
            EditColor("Text##ucm", cm->textColor, ed);
            ImGui::TextDisabled("script: on_context_select (Prefs ui_context_index / ui_context_item)");
        }
        return;
    }
    if (auto* kb = dynamic_cast<UIKeybindField*>(comp)) {
        if (hooks.header("UI Keybind Field", kb, toRemove, true)) {
            float pos[2] = {kb->position.x, kb->position.y}, sz[2] = {kb->size.x, kb->size.y};
            if (ImGui::DragFloat2("Pos (px)##ukb", pos, 1.0f)) { kb->position = {pos[0], pos[1]}; ed.dirty = true; }
            if (ImGui::DragFloat2("Size (px)##ukb", sz, 1.0f, 8.0f, 4000.0f)) { kb->size = {sz[0], sz[1]}; ed.dirty = true; }
            EditText("Action##ukb", kb->action, ed);
            std::string keyName = Input::KeyName(kb->key), defName = Input::KeyName(kb->defaultKey);
            if (EditText("Key##ukb", keyName, ed)) kb->key = Input::KeyFromName(keyName);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("A key name: a letter or digit, Space, Enter, Shift, Ctrl, Tab...");
            if (EditText("Default Key##ukb", defName, ed)) kb->defaultKey = Input::KeyFromName(defName);
            EditText("Save As (Prefs)##ukb", kb->prefKey, ed);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Prefs name the binding is saved under, so it survives restarts (empty = not saved).");
            if (ImGui::DragFloat("Text Size##ukb", &kb->textSize, 0.05f, 0.5f, 8.0f)) ed.dirty = true;
            EditColor("Background##ukb", kb->background, ed);
            EditColor("Key Color##ukb", kb->keyColor, ed);
            EditColor("Listening Color##ukb", kb->listenColor, ed);
            EditColor("Text##ukb", kb->textColor, ed);
            if (ImGui::Checkbox("Interactable##ukb", &kb->interactable)) ed.dirty = true;
            ImGui::SameLine();
            if (ImGui::Checkbox("Focusable##ukb", &kb->focusable)) ed.dirty = true;
            hooks.anchorCombo("Anchor##ukb", kb->anchor, ed);
            ImGui::TextDisabled("script: on_keybind_change (Prefs ui_keybind_action / ui_keybind_key)");
        }
    }
}

void BuildUIModalDialog(GameObject* g) {
    auto* panel = g->AddComponent<UIPanel>();
    panel->size = {420.0f, 220.0f};
    g->AddComponent<UIModal>();
    Scene* s = g->scene();
    if (!s) return;
    GameObject* title = s->CreateGameObject("Title");
    title->transform->SetParent(g->transform, false);
    auto* tr = title->AddComponent<TextRenderer>();
    tr->screenSpace = true; tr->text = "Are you sure?"; tr->pixelSize = 3.0f;
    tr->anchor = UIAnchor::TopCenter; tr->screenPos = {0.0f, 28.0f}; tr->size = {380.0f, 40.0f};
    GameObject* close = s->CreateGameObject("Close");
    close->transform->SetParent(g->transform, false);
    auto* b = close->AddComponent<UIButton>();
    b->label = "Close"; b->size = {140.0f, 44.0f};
    b->anchor = UIAnchor::BottomCenter; b->position = {0.0f, -24.0f};
    b->clickTarget = g->name; b->clickFunction = "Close";   // closes this dialog, no script needed
}

void BuildUIToast(GameObject* g) { g->AddComponent<UIToast>(); }   // grok/ui-toast
void BuildUIKeybindField(GameObject* g) { g->AddComponent<UIKeybindField>(); }

void BuildUIContextMenu(GameObject* g) {
    g->AddComponent<UIPanel>();
    g->AddComponent<UIContextMenu>();
}

} // namespace oked
