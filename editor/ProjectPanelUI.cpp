#include "ProjectPanelUI.hpp"
#include "okay/Render/SoftwareRenderer.hpp"   // InvalidateAllTextureCaches
#include "okay/Texture/Export.hpp"             // okay::tex: Make mipmaps / compress (docs/rendering/own_texture_library.md)

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <chrono>
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

namespace {
// "Move to..." picker
struct PickState {
    bool pending = false, active = false;
    okaypb::Browser* browser = nullptr;
    std::vector<std::string> items;             // the outermost of what is being moved ({A, A/x} is just A)
    okaypb::ctx::MoveSources sources;           // what the rule needs to know about them, worked out once when the window opens
    std::string selected;       // the folder chosen (absolute), "" until one is clicked
    std::string reveal;         // a folder to show open on the first frame
    bool firstFrame = false;
    std::string root;
    std::map<std::string, std::string> reasons;   // folder -> why the items cannot go there ("" = fine); asked once per folder
    std::function<void(const std::string&)> onPick;
};
PickState g_pick;

// One folder of the picker and, when it is open, the folders in it.
void PickNode(const std::string& dir, bool isRoot) {
    const std::vector<okaypb::Browser::FolderRow> kids = g_pick.browser->SubFolders(dir);
    auto known = g_pick.reasons.find(dir);
    if (known == g_pick.reasons.end()) known = g_pick.reasons.emplace(dir, okaypb::ctx::MoveBlockedReason(g_pick.sources, dir)).first;
    const std::string why = known->second;
    const std::string label = isRoot ? std::string("Assets") : NameOf(dir);
    ImGuiTreeNodeFlags f = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (kids.empty()) f |= ImGuiTreeNodeFlags_Leaf;
    if (isRoot) f |= ImGuiTreeNodeFlags_DefaultOpen;
    if (g_pick.selected == dir) f |= ImGuiTreeNodeFlags_Selected;
    if (g_pick.firstFrame && !g_pick.reveal.empty() && okaypb::IsInside(dir, g_pick.reveal) && okaypb::Slash(dir) != okaypb::Slash(g_pick.reveal)) ImGui::SetNextItemOpen(true);
    if (!why.empty()) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    const bool open = ImGui::TreeNodeEx(dir.c_str(), f, "%s", label.c_str());
    if (!why.empty()) ImGui::PopStyleColor();
    if (ImGui::IsItemHovered() && !why.empty()) ImGui::SetTooltip("%s", why.c_str());
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen() && why.empty()) g_pick.selected = dir;
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && why.empty()) g_pick.selected = dir;
    if (open) {
        for (const okaypb::Browser::FolderRow& k : kids) PickNode(k.path, false);
        ImGui::TreePop();
    }
}

// Properties window
struct PropState {
    bool pending = false, active = false;
    std::vector<okaypb::ctx::Property> rows;
    std::string title, copyPath;
};
PropState g_props;
} // namespace

void AskFolder(okaypb::Browser& browser, std::vector<std::string> items, std::string reveal, std::function<void(const std::string&)> onPick) {
    browser.Sync();
    g_pick.browser = &browser;
    g_pick.items = okaypb::paths::Outermost(items);   // (a long selection costs one sort here, then a text comparison per folder)
    g_pick.reveal = std::move(reveal);
    g_pick.root = browser.Root();
    g_pick.sources = okaypb::ctx::PrepareMove(g_pick.root, g_pick.items);
    g_pick.selected.clear();
    g_pick.reasons.clear();
    g_pick.onPick = std::move(onPick);
    g_pick.firstFrame = true;
    g_pick.pending = true;
}

void ShowProperties(okaypb::Browser& browser, const std::string& pathIn, const PropertyExtras& extras) {
    namespace fs = std::filesystem;
    const std::string path = okaypb::Slash(pathIn);
    okaypb::ctx::PropertyInput in;
    std::error_code ec;
    in.isFolder = fs::is_directory(fs::u8path(path), ec);
    in.name = NameOf(path);
    in.fullPath = path;
    in.projectPath = okaypb::ctx::AssetsRelativeText(browser.ProjectDir(), path);
    if (in.projectPath.empty()) in.projectPath = path;
    in.favourite = extras.favourite;
    in.colourLabel = extras.colourLabel;
    in.startScene = extras.startScene;
    in.now = static_cast<std::int64_t>(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
    if (in.isFolder) {
        in.typeName = "Folder";
        const okaypb::Browser::FolderStats st = browser.StatsOf(path);
        in.files = st.files; in.folders = st.folders; in.bytes = st.bytes;
        in.sizeText = okaypb::HumanSize(st.bytes);
    } else {
        const okaypb::Browser::AssetInfo info = browser.Info(path);
        in.indexed = info.indexed;
        const std::string ext = fs::u8path(path).extension().generic_u8string();
        std::string lower = ext;
        for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        in.typeName = info.indexed && !info.typeName.empty() ? info.typeName : okaypb::TypeLabel(lower, false);
        in.bytes = info.indexed ? info.size : static_cast<std::uint64_t>(fs::is_regular_file(fs::u8path(path), ec) ? fs::file_size(fs::u8path(path), ec) : 0);
        in.sizeText = okaypb::HumanSize(in.bytes);
        in.modified = info.modified;
        in.guid = info.guid.ToString();
        in.hasMeta = info.hasMeta;
        in.uses = info.uses; in.usesAll = info.usesAll; in.usedBy = info.usedBy;
        in.missing = static_cast<int>(info.missing.size());
        in.tags = info.labels;
    }
    g_props.rows = okaypb::ctx::PropertyRows(in);
    g_props.title = in.name;
    g_props.copyPath = in.projectPath;
    g_props.pending = true;
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

    // ---- Move to... folder picker ---------------------------------------------------------------
    if (g_pick.pending) { ImGui::OpenPopup("##projmove"); g_pick.pending = false; g_pick.active = true; }
    if (g_pick.active) {
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(420, 0), ImGuiCond_Appearing);
        if (ImGui::BeginPopupModal("##projmove", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize)) {
            if (g_pick.items.size() == 1) ImGui::Text("Move \"%s\" to...", NameOf(g_pick.items[0]).c_str());
            else ImGui::Text("Move %d items to...", (int)g_pick.items.size());
            ImGui::TextDisabled("Pick a folder. A name that is taken gets a number, and links to what moves are updated.");
            ImGui::Spacing();
            ImGui::BeginChild("##movetree", ImVec2(0, 260), true);
            PickNode(g_pick.root, true);
            ImGui::EndChild();
            g_pick.firstFrame = false;
            ImGui::TextDisabled("%s", g_pick.selected.empty() ? "No folder picked yet." : ("To: " + okaypb::ctx::AssetsRelativeText(g_pick.browser->ProjectDir(), g_pick.selected)).c_str());
            ImGui::Spacing();
            ImGui::BeginDisabled(g_pick.selected.empty());
            const bool go = ImGui::Button("Move", ImVec2(110, 0));
            ImGui::EndDisabled();
            ImGui::SameLine();
            const bool cancel = ImGui::Button("Cancel", ImVec2(110, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape);
            if (go && !g_pick.selected.empty()) {
                auto fn = std::move(g_pick.onPick);
                const std::string dest = g_pick.selected;
                g_pick.active = false; ImGui::CloseCurrentPopup();
                if (fn) fn(dest);
            } else if (cancel) { g_pick.active = false; ImGui::CloseCurrentPopup(); }
            ImGui::EndPopup();
        } else g_pick.active = false;
    }

    // ---- Properties -------------------------------------------------------------------------------
    if (g_props.pending) { ImGui::OpenPopup("##projprops"); g_props.pending = false; g_props.active = true; }
    if (g_props.active) {
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(560, 0), ImGuiCond_Appearing);
        if (ImGui::BeginPopupModal("##projprops", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Properties: %s", g_props.title.c_str());
            ImGui::Spacing();
            DrawPropertyRows(g_props.rows);
            ImGui::Spacing();
            if (ImGui::Button("Copy Path", ImVec2(110, 0))) ImGui::SetClipboardText(g_props.copyPath.c_str());
            ImGui::SameLine();
            if (ImGui::Button("Close", ImVec2(110, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) { g_props.active = false; ImGui::CloseCurrentPopup(); }
            ImGui::EndPopup();
        } else g_props.active = false;
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

// A grey note under a setting, wrapped at the panel's edge (a plain TextDisabled line ran off the Inspector's right
// side: "Read while it plays keeps a long file in a small fixed amount" with the rest cut off).
void Hint(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}

bool Combo(const char* label, int& value, std::initializer_list<const char*> items) {
    bool changed = false;
    std::vector<const char*> v(items);
    // As wide as its longest choice (a fixed 160 px showed "Load the whole fi"), but never wider than the panel.
    float widest = 0.0f;
    for (const char* it : v) widest = std::max(widest, ImGui::CalcTextSize(it).x);
    const float want = widest + ImGui::GetStyle().FramePadding.x * 2.0f + ImGui::GetFrameHeight();
    const float room = ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(label, nullptr, true).x - ImGui::GetStyle().ItemInnerSpacing.x;
    ImGui::SetNextItemWidth(std::max(160.0f, std::min(want, room)));
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
        Hint("Made by a newer editor. Shown, but not changed here.");
        return false;
    }
    switch (e.kind) {
        case ok::AssetType::Texture: {
            static const int sizes[] = {0, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384};
            int si = 7;
            for (int i = 0; i < 11; ++i) if (sizes[i] == e.texture.maxSize) si = i;
            if (Combo("Largest side", si, {"No limit", "32", "64", "128", "256", "512", "1024", "2048", "4096", "8192", "16384"})) e.texture.maxSize = sizes[si];
            Hint("Bigger pictures are made smaller when loaded.");
            if (kShowTextureSamplerSettings) {
            int fi = static_cast<int>(e.texture.filter);
            if (Combo("Filter", fi, {"Point (pixels)", "Bilinear (smooth)", "Trilinear (smooth + mips)"})) e.texture.filter = static_cast<ok::TextureImportSettings::Filter>(fi);
            Hint("Point keeps hard pixel edges.");
            int wi = static_cast<int>(e.texture.wrap);
            if (Combo("Wrap", wi, {"Repeat", "Clamp", "Mirror"})) e.texture.wrap = static_cast<ok::TextureImportSettings::Wrap>(wi);
            Hint("What is drawn past the picture's edge.");
            ImGui::Checkbox("Mipmaps", &e.texture.mipmaps);
            Hint("Smaller copies for far away. Off: always full size.");
            ImGui::Checkbox("Compress on the GPU", &e.texture.compress);
            Hint("OpenGL keeps a BC1/BC3 copy (less video memory, slightly softer). Kept in .okaycache next to the picture.");
            Hint("Applies in the editor view and OpenGL / software games.");
            }
            // Texture tools (our own library, okay::tex): write a .dds with mipmaps, uncompressed or block compressed,
            // next to the picture. It does not change the picture or the import settings above.
            {
                ImGui::Separator();
                ImGui::TextDisabled("MIPMAPS AND COMPRESSION");
                static int outKind = 1;
                static int filterKind = 1;
                static bool colour = true, normal = false, cutout = false;
                static std::string texMsg, texMsgFor;
                Combo("Write", outKind, {"Mipmaps (DDS, full quality)", "Mipmaps + compressed (BC1/BC3/BC5 DDS)"});
                Combo("Filter", filterKind, {"Box (fast)", "Lanczos (sharper)", "Kaiser (smooth)"});
                ImGui::Checkbox("Colour picture (filter in linear light)", &colour);
                ImGui::Checkbox("Normal map", &normal);
                ImGui::Checkbox("Cut-out (keep see-through share)", &cutout);
                if (ImGui::Button("Generate mipmaps / compress")) {
                    okay::tex::TextureBuildOptions bo;
                    bo.compress = outKind == 1;
                    bo.mip.filter = filterKind == 0 ? okay::tex::MipFilter::Box : filterKind == 1 ? okay::tex::MipFilter::Lanczos3 : okay::tex::MipFilter::Kaiser;
                    bo.mip.srgb = colour && !normal;
                    bo.mip.normalMap = normal;
                    bo.mip.alphaCoverage = cutout;
                    std::string out, terr;
                    if (okay::tex::BuildDdsFile(assetPath, &out, bo, &terr)) texMsg = "Wrote " + out;
                    else texMsg = terr.empty() ? "Could not write the file." : terr;
                    texMsgFor = assetPath;
                }
                Hint("Writes a .dds next to the picture. Open it in any DDS viewer.");
                if (texMsgFor == assetPath && !texMsg.empty()) ImGui::TextWrapped("%s", texMsg.c_str());
            }
            break;
        }
        case ok::AssetType::Model: {
            ImGui::SetNextItemWidth(160);
            ImGui::DragFloat("Scale", &e.model.scale, 0.01f, 0.0001f, 10000.0f, "%.4f");
            Hint("Multiplies its size when you add it to a scene.");
            int ni = static_cast<int>(e.model.normals);
            if (Combo("Normals", ni, {"From the file", "Calculate (smooth)", "Flat (faceted)"})) e.model.normals = static_cast<ok::ModelImportSettings::Normals>(ni);
            if (e.model.normals == ok::ModelImportSettings::Normals::Calculate) {
                ImGui::SetNextItemWidth(160);
                ImGui::DragFloat("Smoothing angle", &e.model.smoothingAngle, 0.5f, 0.0f, 180.0f, "%.0f deg");
                Hint("Sharper edges stay hard. Not for rigged parts.");
            } else if (e.model.normals == ok::ModelImportSettings::Normals::Flat) {
                Hint("Each face shaded alone. Not for rigged parts.");
            }
            ImGui::Checkbox("Import animations", &e.model.importAnimations);
            Hint("Off: no clips, rest pose.");
            Hint("Used when the model is next added to a scene.");
            break;
        }
        case ok::AssetType::Audio: {
            int mode = e.audio.loadMode == ok::AudioImportSettings::LoadMode::Stream ? 1 : 0;
            if (Combo("Loading", mode, {"Load the whole file", "Read it while it plays"}))
                e.audio.loadMode = mode == 1 ? ok::AudioImportSettings::LoadMode::Stream
                                             : ok::AudioImportSettings::LoadMode::Decode;
            Hint("Read while it plays keeps a long file in a small fixed amount of memory.");
            ImGui::Checkbox("Mix down to one channel", &e.audio.forceMono);
            Hint("Left and right become a single channel. Off keeps the recording as it is.");
            Hint("Used the next time the file is loaded.");
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
