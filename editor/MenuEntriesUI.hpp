#pragma once
// Draws a list of menu entries (MenuBarModel.hpp) with Dear ImGui: the one drawer behind the main menu bar and the
// Hierarchy menus. It owns the look (shortcuts right-aligned by ImGui, a hover tip, the reason a greyed-out item gives) and
// leaves what an item DOES to the host, which gets the entry's id. Header-only so editor/CMakeLists.txt needs no new file.
#include "MenuBarModel.hpp"

#include "imgui.h"

#include <functional>

namespace oked {
namespace menubar {

struct Host {
    std::function<bool*(const Entry&)> flag;    // a Toggle's live flag; null = the id is unknown (drawn greyed out)
    std::function<void(const Entry&)> run;      // an Item was clicked
    std::function<void(const Entry&)> slot;     // draw the content of a Slot or of a SlotMenu (inside its submenu)
};

/// The hover text of the item just drawn: the reason when it is greyed out, else its tip.
inline void ShowTip(const Entry& e) {
    if (!ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) return;
    if (!e.enabled && !e.reason.empty()) ImGui::SetTooltip("%s", e.reason.c_str());
    else if (!e.tip.empty()) ImGui::SetTooltip("%s", e.tip.c_str());
}

/// SlotUI drawn with Dear ImGui: what the slots drew before they were written against SlotUI.
struct ImGuiSlotUI : SlotUI {
    bool Item(const char* label, bool enabled) override { return ImGui::MenuItem(label, nullptr, false, enabled); }
    bool BeginMenu(const char* label, bool enabled) override { return ImGui::BeginMenu(label, enabled); }
    void EndMenu() override { ImGui::EndMenu(); }
    void Separator() override { ImGui::Separator(); }
    void Note(const char* text) override { ImGui::TextDisabled("%s", text); }
    void Tip(const char* text) override { if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", text); }
};

inline void DrawEntries(const std::vector<Entry>& es, const Host& h) {
    for (const Entry& e : es) {
        switch (e.kind) {
        case Kind::Separator:
            ImGui::Separator();
            break;
        case Kind::Heading:
            ImGui::TextDisabled("%s", e.label.c_str());
            break;
        case Kind::Item: {
            const bool hit = ImGui::MenuItem(e.label.c_str(), e.shortcut.empty() ? nullptr : e.shortcut.c_str(), false, e.enabled);
            ShowTip(e);
            if (hit && e.enabled && h.run) h.run(e);
            break;
        }
        case Kind::Toggle: {
            bool* f = h.flag ? h.flag(e) : nullptr;
            bool none = false;
            ImGui::MenuItem(e.label.c_str(), e.shortcut.empty() ? nullptr : e.shortcut.c_str(), f ? f : &none, e.enabled && f != nullptr);
            ShowTip(e);
            break;
        }
        case Kind::Submenu: {
            const bool open = ImGui::BeginMenu(e.label.c_str(), e.enabled);
            if (!open) ShowTip(e);
            if (open) { DrawEntries(e.children, h); ImGui::EndMenu(); }
            break;
        }
        case Kind::SlotMenu: {
            const bool open = ImGui::BeginMenu(e.label.c_str(), e.enabled);
            if (!open) ShowTip(e);
            if (open) { if (h.slot) h.slot(e); ImGui::EndMenu(); }
            break;
        }
        case Kind::Slot:
            if (h.slot) h.slot(e);
            break;
        }
    }
}

}  // namespace menubar
}  // namespace oked
