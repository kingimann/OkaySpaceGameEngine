// The built-in file browser: an ImGui modal drawn from okay::filedialog::DrawBuiltin(). Used when
// no system dialog is available (no zenity / kdialog / osascript), so picking a file always
// works. All the listing / selection logic is in BrowserModel (FileDialog.cpp, unit tested).
#include "FileDialog.hpp"

#include "imgui.h"
#include "imgui_internal.h"   // the popup stack: the browser opens on top of a modal that asked for it and gives it back afterwards

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

namespace okay::filedialog {

static std::string FormatSize(std::uint64_t b) {
    char buf[32];
    if (b < 1024) std::snprintf(buf, sizeof buf, "%llu B", (unsigned long long)b);
    else if (b < 1024ull * 1024) std::snprintf(buf, sizeof buf, "%.1f KB", (double)b / 1024.0);
    else if (b < 1024ull * 1024 * 1024) std::snprintf(buf, sizeof buf, "%.1f MB", (double)b / (1024.0 * 1024.0));
    else std::snprintf(buf, sizeof buf, "%.2f GB", (double)b / (1024.0 * 1024.0 * 1024.0));
    return buf;
}

void DrawBuiltin() {
    static bool s_shown = false;
    static bool s_confirmReplace = false;
    static std::string s_lastDir;
    static char s_pathBuf[1024];
    static char s_nameBuf[512];
    static ImGuiID s_restore = 0;   // the modal the dialog was asked from (e.g. "Open Scene" with its "..." button)

    // A modal given back to its owner whose owner did not draw it this frame (it moved on, e.g. the project was
    // picked) would stay open and invisible, blocking every click: close it.
    static ImGuiID s_verify = 0;
    static int s_verifyFrame = -1;
    if (s_verify && ImGui::GetFrameCount() > s_verifyFrame) {
        ImGuiContext& g = *GImGui;
        if (g.OpenPopupStack.Size > 0 && g.OpenPopupStack[0].PopupId == s_verify && !g.OpenPopupStack[0].Window)
            ImGui::ClosePopupToLevel(0, true);
        s_verify = 0;
    }
    BrowserModel* m = BuiltinModel();
    if (!m) { s_shown = false; return; }

    const Request& rq = m->Req();
    const char* verb = rq.kind == Kind::SaveFile ? "Save" : rq.kind == Kind::Folder ? "Choose folder" : "Open";
    std::string title = (rq.title.empty() ? std::string(verb) : rq.title) + "###okay_filedialog";

    if (!s_shown) {
        // Opening a popup at the top level closes the one that is open there; remember a modal so it can come back.
        ImGuiContext& g = *GImGui;
        s_restore = 0;
        if (g.OpenPopupStack.Size > 0 && g.OpenPopupStack[0].Window && (g.OpenPopupStack[0].Window->Flags & ImGuiWindowFlags_Modal))
            s_restore = g.OpenPopupStack[0].PopupId;
        ImGui::OpenPopup("###okay_filedialog");
        s_shown = true;
        s_confirmReplace = false;
        s_lastDir.clear();
        s_nameBuf[0] = 0;
    }
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImVec2 size(std::min(820.0f, vp->Size.x - 40.0f), std::min(520.0f, vp->Size.y - 40.0f));
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + vp->Size.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(size, ImGuiCond_Appearing);

    if (!ImGui::BeginPopupModal(title.c_str(), nullptr, ImGuiWindowFlags_NoCollapse)) {
        // The popup closed behind our back (another popup took over): treat it as a cancel.
        s_shown = false;
        FinishBuiltin(false);
        if (s_restore) { ImGui::OpenPopup(s_restore); s_verify = s_restore; s_verifyFrame = ImGui::GetFrameCount(); }
        return;
    }

    bool accept = false, cancel = false;
    const float footer = ImGui::GetFrameHeightWithSpacing() * (rq.kind == Kind::Folder ? 2.0f : 3.0f) + 8.0f;

    // Address row.
    if (ImGui::Button("Up")) m->Up();
    ImGui::SameLine();
    if (ImGui::Button("Refresh")) m->Refresh();
    ImGui::SameLine();
    if (m->Dir() != s_lastDir) {
        s_lastDir = m->Dir();
        std::snprintf(s_pathBuf, sizeof s_pathBuf, "%s", s_lastDir.c_str());
    }
    bool hidden = m->ShowHidden();
    const float hiddenW = ImGui::CalcTextSize("Hidden").x + ImGui::GetFrameHeight() + 16.0f;
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - hiddenW);
    if (ImGui::InputText("##fdpath", s_pathBuf, sizeof s_pathBuf, ImGuiInputTextFlags_EnterReturnsTrue)) {
        if (!m->GoTo(s_pathBuf)) std::snprintf(s_pathBuf, sizeof s_pathBuf, "%s", m->Dir().c_str());
        s_lastDir.clear();
    }
    ImGui::SameLine();
    if (ImGui::Checkbox("Hidden", &hidden)) m->SetShowHidden(hidden);

    // Places + list.
    const float bodyH = ImGui::GetContentRegionAvail().y - footer;
    ImGui::BeginChild("##fdplaces", ImVec2(150.0f, bodyH), ImGuiChildFlags_Borders);
    for (const auto& pl : m->Places()) {
        ImGui::PushID(pl.second.c_str());
        if (ImGui::Selectable(pl.first.c_str(), m->Dir() == pl.second)) { m->GoTo(pl.second); }
        ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##fdlist", ImVec2(0, bodyH), ImGuiChildFlags_Borders);
    if (ImGui::BeginTable("##fdtable", 2, ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();
        std::string enterDir;
        const auto entries = m->Entries();   // copy: GoTo below re-lists
        for (const auto& e : entries) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::PushID(e.name.c_str());
            const std::string label = (e.isDir ? "[+] " : "    ") + e.name;
            const bool sel = m->IsSelected(e.name);
            if (ImGui::Selectable(label.c_str(), sel, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)) {
                const bool additive = ImGui::GetIO().KeyCtrl;
                if (e.isDir) {
                    if (rq.kind == Kind::Folder) m->Select(e.name, false);
                    if (ImGui::IsMouseDoubleClicked(0)) enterDir = e.name;
                } else {
                    m->Select(e.name, additive);
                    if (ImGui::IsMouseDoubleClicked(0) && rq.kind != Kind::Folder) accept = true;
                }
            }
            ImGui::TableNextColumn();
            if (!e.isDir) ImGui::TextDisabled("%s", FormatSize(e.size).c_str());
            ImGui::PopID();
        }
        if (entries.empty()) { ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::TextDisabled("(nothing here)"); }
        if (!enterDir.empty()) m->Enter(enterDir);
        ImGui::EndTable();
    }
    ImGui::EndChild();

    // Footer: name field, filter, buttons.
    if (rq.kind != Kind::Folder) {
        char label[16];
        std::snprintf(label, sizeof label, "%s", rq.kind == Kind::SaveFile ? "File name" : "File");
        // Open mode shows the selection; typing a name opens that file instead.
        if (rq.kind == Kind::OpenFile && !m->Selected().empty()) {
            std::string sel;
            for (size_t i = 0; i < m->Selected().size() && i < 3; ++i) sel += (i ? ", " : "") + m->Selected()[i];
            if (m->Selected().size() > 3) sel += " ... (" + std::to_string(m->Selected().size()) + " files)";
            std::snprintf(s_nameBuf, sizeof s_nameBuf, "%s", sel.c_str());
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 260.0f);
            ImGui::InputText("##fdname_ro", s_nameBuf, sizeof s_nameBuf, ImGuiInputTextFlags_ReadOnly);
        } else {
            std::snprintf(s_nameBuf, sizeof s_nameBuf, "%s", m->typed.c_str());
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 260.0f);
            if (ImGui::InputText("##fdname", s_nameBuf, sizeof s_nameBuf, ImGuiInputTextFlags_EnterReturnsTrue)) { m->typed = s_nameBuf; accept = true; }
            else m->typed = s_nameBuf;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%s", label);
        if (!rq.filters.empty()) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(160.0f);
            const int cur = m->FilterIndex();
            const std::string cname = cur >= 0 && cur < (int)rq.filters.size() ? (rq.filters[(size_t)cur].name.empty() ? "Files" : rq.filters[(size_t)cur].name) : "All files";
            if (ImGui::BeginCombo("##fdfilter", cname.c_str())) {
                for (int i = 0; i < (int)rq.filters.size(); ++i) {
                    const std::string n = rq.filters[(size_t)i].name.empty() ? "Files" : rq.filters[(size_t)i].name;
                    if (ImGui::Selectable(n.c_str(), i == cur)) m->SetFilterIndex(i);
                }
                if (ImGui::Selectable("All files", cur >= (int)rq.filters.size())) m->SetFilterIndex((int)rq.filters.size());
                ImGui::EndCombo();
            }
        }
    } else {
        const std::string where = m->Selected().empty() ? m->Dir() : m->Dir() + "/" + m->Selected()[0];
        ImGui::TextDisabled("Folder: %s", where.c_str());
    }

    std::vector<std::string> res = m->Result();
    const bool ok = !res.empty();
    if (s_confirmReplace) {
        ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.3f, 1), "\"%s\" already exists. Replace it?", res.empty() ? "" : res[0].c_str());
        ImGui::SameLine();
        if (ImGui::Button("Replace")) { s_confirmReplace = false; accept = true; }
        ImGui::SameLine();
        if (ImGui::Button("Back")) s_confirmReplace = false;
        if (accept && s_confirmReplace) accept = false;
    } else {
        if (!ok) ImGui::TextDisabled("%s", m->Why().c_str()); else ImGui::TextDisabled(" ");
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 170.0f + ImGui::GetCursorPosX());
        if (ImGui::Button("Cancel", ImVec2(80, 0))) cancel = true;
        ImGui::SameLine();
        ImGui::BeginDisabled(!ok);
        if (ImGui::Button(rq.kind == Kind::SaveFile ? "Save" : rq.kind == Kind::Folder ? "Choose" : "Open", ImVec2(80, 0))) accept = true;
        ImGui::EndDisabled();
        if (accept && ok && rq.kind == Kind::SaveFile && m->ResultExists()) { accept = false; s_confirmReplace = true; }
        if (accept && !ok) accept = false;
    }
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsKeyPressed(ImGuiKey_Escape)) cancel = true;

    if (accept || cancel) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        s_shown = false;
        if (s_restore) { ImGui::OpenPopup(s_restore); s_verify = s_restore; s_verifyFrame = ImGui::GetFrameCount(); }   // the modal that asked for the file is back
        FinishBuiltin(accept);   // may start the next queued dialog; it opens next frame
        return;
    }
    ImGui::EndPopup();
}

}  // namespace okay::filedialog
