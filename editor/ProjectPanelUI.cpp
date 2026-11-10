#include "ProjectPanelUI.hpp"

#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>

namespace oked::projectpanel {

namespace {

struct NameState {
    bool pending = false;       // open the popup on the next DrawDialogs
    bool active = false;
    NameRequest req;
    char buf[256] = "";
    std::string error;          // from accept()
    bool focus = false;
};
NameState g_name;

struct DeleteState {
    bool pending = false, active = false;
    std::vector<std::string> paths;
    std::vector<okaypb::Referrer> refs;
    std::string root;           // Assets folder, to show short paths
    std::function<void()> onConfirm;
};
DeleteState g_del;

struct RefState {
    bool pending = false, active = false;
    std::string asset;
    std::vector<std::string> users, uses;
    std::string root;
    std::function<void(const std::string&)> onPick;
};
RefState g_refs;

std::string Short(const std::string& root, const std::string& abs) {
    const std::string r = okaypb::Slash(root);
    std::string a = okaypb::Slash(abs);
    if (a.size() > r.size() && a.compare(0, r.size(), r) == 0) a = a.substr(r.size() + 1);
    return a;
}
std::string NameOf(const std::string& p) {
    const auto s = okaypb::Slash(p);
    const auto sl = s.find_last_of('/');
    return sl == std::string::npos ? s : s.substr(sl + 1);
}

} // namespace

void AskName(NameRequest request) {
    g_name.req = std::move(request);
    std::snprintf(g_name.buf, sizeof g_name.buf, "%s", g_name.req.initial.c_str());
    g_name.error.clear();
    g_name.pending = true;
    g_name.focus = true;
}
bool NamePromptOpen() { return g_name.pending || g_name.active; }

void AskDelete(okaypb::Browser& browser, std::vector<std::string> paths, std::function<void()> onConfirm) {
    g_del.paths = std::move(paths);
    g_del.root = browser.Root();
    g_del.refs = browser.ReferencesTo(g_del.paths);
    g_del.onConfirm = std::move(onConfirm);
    g_del.pending = true;
}

void ShowReferences(okaypb::Browser& browser, const std::string& assetPath, std::function<void(const std::string&)> onPick) {
    g_refs.asset = assetPath;
    g_refs.root = browser.Root();
    g_refs.users.clear();
    for (const auto& r : browser.ReferencesTo({assetPath})) g_refs.users.push_back(r.path);
    g_refs.uses = browser.DependenciesOf(assetPath);
    g_refs.onPick = std::move(onPick);
    g_refs.pending = true;
}

void DrawDialogs() {
    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();

    // ---- name prompt ---------------------------------------------------------------------
    if (g_name.pending) { ImGui::OpenPopup("##projname"); g_name.pending = false; g_name.active = true; }
    if (g_name.active) {
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(420, 0), ImGuiCond_Appearing);
        if (ImGui::BeginPopupModal("##projname", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted(g_name.req.title.c_str());
            ImGui::Spacing();
            if (g_name.focus) { ImGui::SetKeyboardFocusHere(); g_name.focus = false; }
            const float sufW = g_name.req.suffix.empty() ? 0.0f : ImGui::CalcTextSize(g_name.req.suffix.c_str()).x + 8.0f;
            ImGui::SetNextItemWidth(-sufW - 1.0f);
            const bool enter = ImGui::InputText("##nm", g_name.buf, sizeof g_name.buf,
                                                ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
            if (!g_name.req.suffix.empty()) { ImGui::SameLine(0, 4); ImGui::TextDisabled("%s", g_name.req.suffix.c_str()); }
            const std::string why = okaypb::ValidateName(g_name.buf);
            if (!g_name.error.empty()) ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.42f, 1.0f), "%s", g_name.error.c_str());
            else if (!why.empty() && g_name.buf[0]) ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.42f, 1.0f), "%s", why.c_str());
            else if (!g_name.req.hint.empty()) ImGui::TextDisabled("%s", g_name.req.hint.c_str());
            else ImGui::TextDisabled(" ");
            ImGui::Spacing();
            ImGui::BeginDisabled(!why.empty());
            bool ok = ImGui::Button(g_name.req.okLabel.c_str(), ImVec2(110, 0));
            ImGui::EndDisabled();
            ImGui::SameLine();
            const bool cancel = ImGui::Button("Cancel", ImVec2(110, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape);
            if ((ok || enter) && why.empty()) {
                std::string full = g_name.buf;
                while (!full.empty() && full.front() == ' ') full.erase(full.begin());
                while (!full.empty() && full.back() == ' ') full.pop_back();
                full += g_name.req.suffix;
                g_name.error = g_name.req.accept ? g_name.req.accept(full) : std::string();
                if (g_name.error.empty()) { g_name.active = false; ImGui::CloseCurrentPopup(); }
                else g_name.focus = true;
            } else if (cancel) { g_name.active = false; g_name.error.clear(); ImGui::CloseCurrentPopup(); }
            ImGui::EndPopup();
        } else g_name.active = false;
    }

    // ---- delete confirmation ---------------------------------------------------------------
    if (g_del.pending) { ImGui::OpenPopup("##projdel"); g_del.pending = false; g_del.active = true; }
    if (g_del.active) {
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(480, 0), ImGuiCond_Appearing);
        if (ImGui::BeginPopupModal("##projdel", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize)) {
            if (g_del.paths.size() == 1) ImGui::Text("Delete \"%s\"?", NameOf(g_del.paths[0]).c_str());
            else ImGui::Text("Delete %d items?", (int)g_del.paths.size());
            ImGui::TextDisabled("%s", g_del.paths.size() == 1 ? "It goes to the project's trash. Ctrl+Z in the Project panel brings it back."
                                                              : "They go to the project's trash. Ctrl+Z in the Project panel brings them back.");
            if (g_del.paths.size() > 1) {
                ImGui::Spacing();
                for (std::size_t i = 0; i < g_del.paths.size() && i < 6; ++i) ImGui::BulletText("%s", NameOf(g_del.paths[i]).c_str());
                if (g_del.paths.size() > 6) ImGui::TextDisabled("and %d more", (int)g_del.paths.size() - 6);
            }
            ImGui::Spacing();
            if (g_del.refs.empty()) {
                ImGui::TextDisabled("Nothing in the project uses %s.", g_del.paths.size() == 1 ? "it" : "them");
            } else {
                std::vector<std::string> users;
                for (const auto& r : g_del.refs) if (std::find(users.begin(), users.end(), r.path) == users.end()) users.push_back(r.path);
                ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.35f, 1.0f), "%d asset%s use%s %s. Their links will be empty:",
                                   (int)users.size(), users.size() == 1 ? "" : "s", users.size() == 1 ? "s" : "",
                                   g_del.paths.size() == 1 ? "it" : "them");
                ImGui::BeginChild("##delrefs", ImVec2(0, std::min<float>(150.0f, 24.0f * (float)users.size() + 22.0f)), true);
                for (const std::string& u : users) ImGui::TextUnformatted(Short(g_del.root, u).c_str());
                ImGui::EndChild();
            }
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.65f, 0.22f, 0.22f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.80f, 0.28f, 0.28f, 1.0f));
            const bool del = ImGui::Button("Delete", ImVec2(110, 0));
            ImGui::PopStyleColor(2);
            ImGui::SameLine();
            const bool cancel = ImGui::Button("Cancel", ImVec2(110, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape);
            if (del) {
                auto fn = std::move(g_del.onConfirm);
                g_del.active = false; ImGui::CloseCurrentPopup();
                if (fn) fn();
            } else if (cancel) { g_del.active = false; ImGui::CloseCurrentPopup(); }
            ImGui::EndPopup();
        } else g_del.active = false;
    }

    // ---- used by / uses ----------------------------------------------------------------------
    if (g_refs.pending) { ImGui::OpenPopup("##projrefs"); g_refs.pending = false; g_refs.active = true; }
    if (g_refs.active) {
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(520, 0), ImGuiCond_Appearing);
        if (ImGui::BeginPopupModal("##projrefs", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("References: %s", NameOf(g_refs.asset).c_str());
            ImGui::Spacing();
            auto list = [&](const char* head, const char* empty, const std::vector<std::string>& v, const char* id) {
                ImGui::TextDisabled("%s", head);
                if (v.empty()) { ImGui::Text("  %s", empty); return; }
                ImGui::BeginChild(id, ImVec2(0, std::min<float>(150.0f, 26.0f * (float)v.size() + 20.0f)), true);
                for (const std::string& s : v) {
                    ImGui::PushID(s.c_str());
                    if (ImGui::Selectable(Short(g_refs.root, s).c_str()) && g_refs.onPick) {
                        auto pick = g_refs.onPick;
                        g_refs.active = false; ImGui::CloseCurrentPopup();
                        pick(s);
                    }
                    ImGui::PopID();
                    if (!g_refs.active) break;
                }
                ImGui::EndChild();
            };
            list("USED BY (click to show it in the Project panel)", "Nothing uses it.", g_refs.users, "##ru");
            ImGui::Spacing();
            if (g_refs.active) list("USES", "It uses nothing.", g_refs.uses, "##rd");
            if (g_refs.active) {
                ImGui::Spacing();
                if (ImGui::Button("Close", ImVec2(110, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) { g_refs.active = false; ImGui::CloseCurrentPopup(); }
            }
            ImGui::EndPopup();
        } else g_refs.active = false;
    }
}

// ---- import settings ------------------------------------------------------------------------------

namespace {
struct ImportEdit {
    std::string path;
    okay::Guid guid;
    okay::ImportSettings original, edit;
    bool tried = false;
};
ImportEdit g_imp;

bool Combo(const char* label, int& value, std::initializer_list<const char*> items) {
    bool changed = false;
    std::vector<const char*> v(items);
    ImGui::SetNextItemWidth(160);
    if (ImGui::BeginCombo(label, value >= 0 && value < (int)v.size() ? v[value] : "")) {
        for (int i = 0; i < (int)v.size(); ++i)
            if (ImGui::Selectable(v[i], value == i)) { value = i; changed = true; }
        ImGui::EndCombo();
    }
    return changed;
}
} // namespace

bool HasImportSettings(okaypb::Browser& browser, const std::string& assetPath) {
    const std::string rel = browser.Db().ToProjectPath(assetPath);
    if (rel.empty()) return false;
    if (!browser.Indexed()) browser.Sync();
    const okay::AssetRecord* rec = browser.Db().Find(rel);
    return rec && okay::ImportSettings::HasSettings(rec->kind);
}

bool DrawImportSettings(okaypb::Browser& browser, const std::string& assetPath) {
    namespace ok = okay;
    const std::string rel = browser.Db().ToProjectPath(assetPath);
    if (rel.empty()) return false;
    if (g_imp.path != assetPath) { g_imp = ImportEdit(); g_imp.path = assetPath; }
    if (g_imp.guid.IsNull()) {
        g_imp.guid = browser.Db().PathToGuid(rel);
        if (g_imp.guid.IsNull() && !g_imp.tried) {   // not indexed yet: look once
            g_imp.tried = true;
            browser.Sync();
            g_imp.guid = browser.Db().PathToGuid(rel);
        }
        if (g_imp.guid.IsNull()) return false;
        const ok::ImportSettings* s = browser.Db().GetSettings(g_imp.guid);
        if (!s || !ok::ImportSettings::HasSettings(s->kind)) { g_imp.guid = ok::Guid(); return false; }
        g_imp.original = g_imp.edit = *s;
    }
    ok::ImportSettings& e = g_imp.edit;
    ImGui::TextDisabled("IMPORT SETTINGS");
    if (e.fromNewerVersion) {
        ImGui::TextDisabled("Made by a newer editor. Shown, but not changed here.");
        return false;
    }
    using TX = ok::TextureImportSettings;
    switch (e.kind) {
        case ok::AssetType::Texture: {
            ImGui::Checkbox("Color image (sRGB)", &e.texture.sRGB);
            int f = (int)e.texture.filter; if (Combo("Filter", f, {"Point", "Bilinear", "Trilinear"})) e.texture.filter = (TX::Filter)f;
            int w = (int)e.texture.wrap; if (Combo("Wrap", w, {"Repeat", "Clamp", "Mirror"})) e.texture.wrap = (TX::Wrap)w;
            static const int sizes[] = {0, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384};
            int si = 7;
            for (int i = 0; i < 11; ++i) if (sizes[i] == e.texture.maxSize) si = i;
            if (Combo("Largest side", si, {"No limit", "32", "64", "128", "256", "512", "1024", "2048", "4096", "8192", "16384"})) e.texture.maxSize = sizes[si];
            ImGui::Checkbox("Mipmaps", &e.texture.mipmaps);
            ImGui::Checkbox("Compress", &e.texture.compress);
            break;
        }
        case ok::AssetType::Model: {
            ImGui::SetNextItemWidth(160);
            ImGui::DragFloat("Scale", &e.model.scale, 0.01f, 0.0001f, 10000.0f, "%.4f");
            int n = (int)e.model.normals; if (Combo("Normals", n, {"From the file", "Smooth", "Flat"})) e.model.normals = (ok::ModelImportSettings::Normals)n;
            ImGui::Checkbox("Import animations", &e.model.importAnimations);
            ImGui::SetNextItemWidth(160);
            ImGui::SliderFloat("Smoothing angle", &e.model.smoothingAngle, 0.0f, 180.0f, "%.0f deg");
            break;
        }
        case ok::AssetType::Audio: {
            int m = (int)e.audio.loadMode; if (Combo("Load", m, {"Decode at load", "Stream"})) e.audio.loadMode = (ok::AudioImportSettings::LoadMode)m;
            ImGui::Checkbox("Force mono", &e.audio.forceMono);
            ImGui::SetNextItemWidth(160);
            ImGui::SliderFloat("Quality", &e.audio.quality, 0.0f, 1.0f, "%.2f");
            break;
        }
        default: return false;
    }
    e.Clamp();
    const bool dirty = !e.SameAs(g_imp.original);
    bool applied = false;
    static std::string failMsg;
    ImGui::BeginDisabled(!dirty);
    if (ImGui::Button("Apply")) {
        std::string err;
        if (browser.Db().SetSettings(g_imp.guid, e, &err)) { g_imp.original = e; applied = true; }
        else { failMsg = err.empty() ? "The settings could not be saved." : err; ImGui::OpenPopup("##impfail"); }
    }
    ImGui::SameLine();
    if (ImGui::Button("Revert")) e = g_imp.original;
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("Saved next to the asset.");
    if (ImGui::BeginPopup("##impfail")) { ImGui::TextUnformatted(failMsg.c_str()); ImGui::EndPopup(); }
    return applied;
}

} // namespace oked::projectpanel
