#include "ProjectPanelUI.hpp"
#include "okay/Render/SoftwareRenderer.hpp"   // InvalidateAllTextureCaches

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
    okaypb::Browser* browser = nullptr;
    RefMode mode = RefMode::Both;
    bool whole = false;         // the whole graph instead of the direct links
};
RefState g_refs;
void FillRefs() {
    g_refs.users.clear();
    g_refs.uses.clear();
    if (!g_refs.browser) return;
    if (g_refs.mode != RefMode::Uses) g_refs.users = g_refs.browser->UsersOfList(g_refs.asset, g_refs.whole);
    if (g_refs.mode != RefMode::UsedBy) g_refs.uses = g_refs.browser->DependenciesOf(g_refs.asset, g_refs.whole);
}

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

void ShowReferences(okaypb::Browser& browser, const std::string& assetPath, std::function<void(const std::string&)> onPick, RefMode mode) {
    browser.Sync();   // incremental: what changed since the last look is in the graph
    g_refs.asset = assetPath;
    g_refs.root = browser.Root();
    g_refs.browser = &browser;
    g_refs.mode = mode;
    g_refs.whole = false;
    FillRefs();
    g_refs.onPick = std::move(onPick);
    g_refs.pending = true;
}

namespace {
struct ProbState {
    bool pending = false, active = false;
    okaypb::Browser* browser = nullptr;
    std::vector<okaypb::Browser::MissingRefRow> missing;
    std::vector<okaypb::Browser::StaleRefRow> stale;
    std::function<void(const std::string&)> onPick;
    std::function<void(const okaypb::Result&)> onFixed;
    std::string message;
};
ProbState g_prob;
void FillProblems() {
    g_prob.missing = g_prob.browser->MissingRefs();
    g_prob.stale = g_prob.browser->StaleRefs();
}
} // namespace

void ShowReferenceProblems(okaypb::Browser& browser, std::function<void(const std::string&)> onPick, std::function<void(const okaypb::Result&)> onFixed) {
    browser.Sync();   // incremental: what changed since the last look is in the graph
    g_prob.browser = &browser;
    g_prob.onPick = std::move(onPick);
    g_prob.onFixed = std::move(onFixed);
    g_prob.message.clear();
    FillProblems();
    g_prob.pending = true;
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
            ImGui::Text("%s: %s", g_refs.mode == RefMode::Uses ? "Dependencies" : g_refs.mode == RefMode::UsedBy ? "Used by" : "References", NameOf(g_refs.asset).c_str());
            if (ImGui::Checkbox("Whole graph (everything it reaches)", &g_refs.whole)) FillRefs();
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
            if (g_refs.mode != RefMode::Uses) list(g_refs.whole ? "USED BY, directly or through other assets (click to show it)" : "USED BY (click to show it in the Project panel)", "Nothing uses it.", g_refs.users, "##ru");
            if (g_refs.mode == RefMode::Both) ImGui::Spacing();
            if (g_refs.active && g_refs.mode != RefMode::UsedBy) list(g_refs.whole ? "USES, directly or through other assets (click to show it)" : "USES (click to show it)", "It uses nothing.", g_refs.uses, "##rd");
            if (g_refs.active) {
                ImGui::Spacing();
                if (ImGui::Button("Close", ImVec2(110, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) { g_refs.active = false; ImGui::CloseCurrentPopup(); }
            }
            ImGui::EndPopup();
        } else g_refs.active = false;
    }

    // ---- broken references -------------------------------------------------------------------
    if (g_prob.pending) { ImGui::OpenPopup("##projprob"); g_prob.pending = false; g_prob.active = true; }
    if (g_prob.active) {
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(640, 0), ImGuiCond_Appearing);
        if (ImGui::BeginPopupModal("##projprob", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize)) {
            const std::string root = g_prob.browser->Root();
            ImGui::Text("Broken references");
            ImGui::TextDisabled("Read from the asset database's dependency graph.");
            ImGui::Spacing();
            ImGui::TextDisabled("MISSING: the file is not there (click a line to show the asset that names it)");
            if (g_prob.missing.empty()) ImGui::Text("  None.");
            else {
                ImGui::BeginChild("##probmiss", ImVec2(0, std::min<float>(150.0f, 26.0f * (float)g_prob.missing.size() + 20.0f)), true);
                for (std::size_t i = 0; i < g_prob.missing.size(); ++i) {
                    ImGui::PushID((int)i);
                    const std::string line = Short(root, g_prob.missing[i].referrer) + "  names  " + g_prob.missing[i].reference;
                    if (ImGui::Selectable(line.c_str()) && g_prob.onPick) { auto pick = g_prob.onPick; g_prob.active = false; ImGui::CloseCurrentPopup(); pick(g_prob.missing[i].referrer); ImGui::PopID(); break; }
                    ImGui::PopID();
                }
                ImGui::EndChild();
            }
            if (g_prob.active) {
                ImGui::Spacing();
                ImGui::TextDisabled("STALE: still the old path of a file that was moved outside the editor");
                if (g_prob.stale.empty()) ImGui::Text("  None.");
                else {
                    ImGui::BeginChild("##probstale", ImVec2(0, std::min<float>(150.0f, 26.0f * (float)g_prob.stale.size() + 20.0f)), true);
                    for (std::size_t i = 0; i < g_prob.stale.size(); ++i) {
                        ImGui::PushID(1000 + (int)i);
                        const std::string line = Short(root, g_prob.stale[i].referrer) + ":  " + g_prob.stale[i].reference + "  ->  " + Short(root, g_prob.stale[i].target);
                        if (ImGui::Selectable(line.c_str()) && g_prob.onPick) { auto pick = g_prob.onPick; g_prob.active = false; ImGui::CloseCurrentPopup(); pick(g_prob.stale[i].referrer); ImGui::PopID(); break; }
                        ImGui::PopID();
                    }
                    ImGui::EndChild();
                }
            }
            if (g_prob.active) {
                if (!g_prob.message.empty()) ImGui::TextColored(ImVec4(0.55f, 0.85f, 0.6f, 1.0f), "%s", g_prob.message.c_str());
                ImGui::Spacing();
                ImGui::BeginDisabled(g_prob.stale.empty());
                if (ImGui::Button("Fix stale references", ImVec2(170, 0))) {
                    const okaypb::Result r = g_prob.browser->FixStaleRefs();
                    g_prob.message = r.ok ? ("Rewrote " + std::to_string(r.referencesRewritten) + " reference(s) in " + std::to_string(r.filesRewritten) + " file(s). A scene that is open in the editor keeps its old text until you open it again.") : r.error;
                    if (r.ok && g_prob.onFixed) g_prob.onFixed(r);
                    FillProblems();
                }
                ImGui::EndDisabled();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Rewrites only the path text in the files that use the old path. Ctrl+Z in the Project panel undoes it.\nMissing references cannot be fixed here: restore the file, or point the asset at another one.");
                ImGui::SameLine();
                if (ImGui::Button("Close", ImVec2(110, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) { g_prob.active = false; ImGui::CloseCurrentPopup(); }
            }
            ImGui::EndPopup();
        } else g_prob.active = false;
    }
}

// ---- import settings ------------------------------------------------------------------------------
// Only settings that an importer really reads are shown. A texture's largest side, a model's scale and normals,
// and an audio file's loading and channel mix. Quality is not shown: audio is not re-encoded, so that number
// cannot change the sound (it stays in the file so an older settings file still round-trips).

namespace {
struct ImportEdit {
    std::string path;
    okay::ImportSettings original, edit;
    bool loaded = false;
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

// Texture Filter / Wrap / Mipmaps work in the software and OpenGL renderers only. The Direct3D renderers (the
// default on Windows) do not read them yet, so the controls stay hidden until they do. Turn this on then.
constexpr bool kShowTextureSamplerSettings = false;

bool HasImportSettings(okaypb::Browser& browser, const std::string& assetPath) {
    (void)browser;
    const okay::AssetType k = okay::AssetTypeFromPath(assetPath);
    return k == okay::AssetType::Texture || k == okay::AssetType::Model || k == okay::AssetType::Audio;
}

bool DrawImportSettings(okaypb::Browser& browser, const std::string& assetPath) {
    namespace ok = okay;
    if (!HasImportSettings(browser, assetPath)) return false;
    if (g_imp.path != assetPath || !g_imp.loaded) {
        g_imp = ImportEdit();
        g_imp.path = assetPath;
        if (!browser.GetImportSettings(assetPath, g_imp.original)) return false;
        g_imp.edit = g_imp.original;
        g_imp.loaded = true;
    }
    ok::ImportSettings& e = g_imp.edit;
    ImGui::TextDisabled("IMPORT SETTINGS");
    if (e.fromNewerVersion) {
        ImGui::TextDisabled("Made by a newer editor. Shown, but not changed here.");
        return false;
    }
    switch (e.kind) {
        case ok::AssetType::Texture: {
            static const int sizes[] = {0, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384};
            int si = 7;
            for (int i = 0; i < 11; ++i) if (sizes[i] == e.texture.maxSize) si = i;
            if (Combo("Largest side", si, {"No limit", "32", "64", "128", "256", "512", "1024", "2048", "4096", "8192", "16384"})) e.texture.maxSize = sizes[si];
            ImGui::TextDisabled("Bigger pictures are made smaller when loaded.");
            if (kShowTextureSamplerSettings) {
            int fi = static_cast<int>(e.texture.filter);
            if (Combo("Filter", fi, {"Point (pixels)", "Bilinear (smooth)", "Trilinear (smooth + mips)"})) e.texture.filter = static_cast<ok::TextureImportSettings::Filter>(fi);
            ImGui::TextDisabled("Point keeps hard pixel edges.");
            int wi = static_cast<int>(e.texture.wrap);
            if (Combo("Wrap", wi, {"Repeat", "Clamp", "Mirror"})) e.texture.wrap = static_cast<ok::TextureImportSettings::Wrap>(wi);
            ImGui::TextDisabled("What is drawn past the picture's edge.");
            ImGui::Checkbox("Mipmaps", &e.texture.mipmaps);
            ImGui::TextDisabled("Smaller copies for far away. Off: always full size.");
            ImGui::TextDisabled("Applies in the editor view and OpenGL / software games.");
            }
            break;
        }
        case ok::AssetType::Model: {
            ImGui::SetNextItemWidth(160);
            ImGui::DragFloat("Scale", &e.model.scale, 0.01f, 0.0001f, 10000.0f, "%.4f");
            ImGui::TextDisabled("Multiplies its size when you add it to a scene.");
            int ni = static_cast<int>(e.model.normals);
            if (Combo("Normals", ni, {"From the file", "Calculate (smooth)", "Flat (faceted)"})) e.model.normals = static_cast<ok::ModelImportSettings::Normals>(ni);
            if (e.model.normals == ok::ModelImportSettings::Normals::Calculate) {
                ImGui::SetNextItemWidth(160);
                ImGui::DragFloat("Smoothing angle", &e.model.smoothingAngle, 0.5f, 0.0f, 180.0f, "%.0f deg");
                ImGui::TextDisabled("Sharper edges stay hard. Not for rigged parts.");
            } else if (e.model.normals == ok::ModelImportSettings::Normals::Flat) {
                ImGui::TextDisabled("Each face shaded alone. Not for rigged parts.");
            }
            ImGui::Checkbox("Import animations", &e.model.importAnimations);
            ImGui::TextDisabled("Off: no clips, rest pose.");
            ImGui::TextDisabled("Used when the model is next added to a scene.");
            break;
        }
        case ok::AssetType::Audio: {
            int mode = e.audio.loadMode == ok::AudioImportSettings::LoadMode::Stream ? 1 : 0;
            if (Combo("Loading", mode, {"Load the whole file", "Read it while it plays"}))
                e.audio.loadMode = mode == 1 ? ok::AudioImportSettings::LoadMode::Stream
                                             : ok::AudioImportSettings::LoadMode::Decode;
            ImGui::TextDisabled("Read while it plays keeps a long file in a small fixed amount of memory.");
            ImGui::Checkbox("Mix down to one channel", &e.audio.forceMono);
            ImGui::TextDisabled("Left and right become a single channel. Off keeps the recording as it is.");
            ImGui::TextDisabled("Used the next time the file is loaded.");
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
        if (browser.SetImportSettings(assetPath, e, &err)) {
            g_imp.original = e; applied = true;
            if (e.kind == ok::AssetType::Texture) ok::InvalidateAllTextureCaches();   // the scene view reads the picture again
        }
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
