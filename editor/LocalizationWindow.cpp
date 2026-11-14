// The Localization window: the project's string table (<project>/strings.okayloc)
// as keys x languages. Cells edit in place (the scene's localized text follows as
// you type), a row per key with its note for translators, a bar per language
// with how much of it is translated, search and filters, CSV import / export for
// translators, a pseudo-localized preview to spot truncation, and Find in Scene
// for the selected key. The grid, the check, the project scan and the preview
// line come from LocalizationTools (LocTable, LocCheck, LocScan, LocPreview).
// Also the toolbar's preview-language menu and the Inspector's Localized options.
#include "LocalizationWindow.hpp"
#include "LocalizationTools.hpp"
#include "ConsolePanel.hpp"     // ConsoleLog
#include "EditorWidgets.hpp"    // AccentCol, SearchField
#include "okay/Components/DialogueRunner.hpp"
#include "okay/Components/HoldPrompt.hpp"
#include "okay/Components/LoadingScreen.hpp"
#include "okay/Components/PauseMenu.hpp"
#include "okay/Components/QuestLog.hpp"
#include "okay/Components/ScriptComponent.hpp"
#include "okay/Components/TextRenderer.hpp"
#include "okay/Components/UIButton.hpp"
#include "okay/Components/UIInputField.hpp"
#include "okay/Components/UIToggle.hpp"
#include "okay/Components/UITooltip.hpp"
#include "okay/Components/VisualScriptComponent.hpp"
#include "okay/Components/WorldUI.hpp"
#include "okay/Core/Localization.hpp"
#include "okay/Graphics/TtfFont.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/SceneLocalization.hpp"
#include "imgui.h"
#include "FileDialog.hpp"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace okay;
using namespace okay::editor;

namespace oked {

bool g_showLocalization = false;

namespace {
namespace fs = std::filesystem;

// ---- Small helpers ------------------------------------------------------------------
std::string Lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

int StringResize(ImGuiInputTextCallbackData* d) {
    if (d->EventFlag == ImGuiInputTextFlags_CallbackResize) {
        auto* s = static_cast<std::string*>(d->UserData);
        s->resize((std::size_t)d->BufTextLen);
        d->Buf = s->data();
    }
    return 0;
}
// An InputText over a std::string.
bool InputString(const char* id, std::string& s, const char* hint = nullptr, ImGuiInputTextFlags flags = 0) {
    flags |= ImGuiInputTextFlags_CallbackResize;
    if (hint) return ImGui::InputTextWithHint(id, hint, s.data(), s.capacity() + 1, flags, StringResize, &s);
    return ImGui::InputText(id, s.data(), s.capacity() + 1, flags, StringResize, &s);
}

void Tip(const char* text) { if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", text); }

// A cell edits on one line: a newline shows as \n (and a backslash as \\).
std::string ToCell(const std::string& v) {
    std::string out;
    for (char c : v) {
        if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else if (c == '\t') out += "\\t";
        else out += c;
    }
    return out;
}
std::string FromCell(const std::string& v) {
    std::string out;
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (v[i] == '\\' && i + 1 < v.size()) {
            const char n = v[i + 1];
            if (n == 'n') { out += '\n'; ++i; continue; }
            if (n == 't') { out += '\t'; ++i; continue; }
            if (n == '\\') { out += '\\'; ++i; continue; }
        }
        out += v[i];
    }
    return out;
}

// ---- The table file, undo, saving ---------------------------------------------------
std::vector<std::string> s_undo, s_redo;   // table snapshots (StringTable::Write)
std::string s_status;
bool s_focused = false;

std::string TablePath(const EditorState& ed) {
    return !Localization::TablePath().empty() ? Localization::TablePath() : ed.StringTablePath();
}

void Save(const EditorState& ed) {
    const std::string path = TablePath(ed);
    if (path.empty()) { s_status = "No project is open: these strings last until the editor closes."; return; }
    if (Localization::Table().Save(path)) {
        if (Localization::TablePath() != path) Localization::SetTable(Localization::Table(), path);
        s_status = "Saved " + fs::u8path(path).filename().u8string();
    } else {
        s_status = "Couldn't write " + path;
        ConsoleLog("Localization: couldn't write " + path, 2);
    }
}

// An edit finished: one undo step (when something changed), the scene's text follows, the file is saved.
void Commit(const EditorState& ed, const std::string& before) {
    if (Localization::Table().Write() == before) return;
    s_undo.push_back(before);
    if (s_undo.size() > 100) s_undo.erase(s_undo.begin());
    s_redo.clear();
    Localization::TableChanged();
    Save(ed);
}

// A field typed into: the table as it was before the field's first change, committed
// when the field is left. (Clicking from one cell into the next activates the next
// one before the first reports its edit, so this snapshots on change, not activation.)
std::string s_editBefore;
bool s_editOpen = false;
void BeginChange() {
    if (s_editOpen) return;
    s_editBefore = Localization::Table().Write();
    s_editOpen = true;
}
void EndEdit(const EditorState& ed) {
    if (!s_editOpen) return;
    s_editOpen = false;
    Commit(ed, s_editBefore);
}

// The default language, giving an empty table English first.
std::string EnsureLanguage(StringTable& t) {
    if (t.languages.empty()) { t.AddLanguage("en", "English"); t.defaultLanguage = "en"; }
    return t.DefaultLanguage();
}

// A key whose default-language text is `text`, else a new one named after `base`.
std::string KeyForText(const EditorState& ed, const std::string& text, const std::string& base) {
    StringTable& t = Localization::Table();
    const std::string before = t.Write();
    const std::string def = EnsureLanguage(t);
    if (!text.empty())
        for (const auto& kv : t.Entries()) {
            auto it = kv.second.text.find(def);
            if (it != kv.second.text.end() && it->second == text) { Commit(ed, before); return kv.first; }
        }
    std::string key = base.empty() ? std::string("text") : base;
    for (int n = 2; t.Has(key); ++n) key = base + "_" + std::to_string(n);
    t.Add(key);
    t.Set(key, def, text);
    Commit(ed, before);
    ConsoleLog("Localization: added the key \"" + key + "\" (" + def + ": " + text + ")");
    return key;
}

// Add keys with their default-language text when the table doesn't have them yet.
int AddMissing(const EditorState& ed, const std::vector<std::pair<std::string, std::string>>& keys) {
    StringTable& t = Localization::Table();
    const std::string before = t.Write();
    const std::string def = EnsureLanguage(t);
    int added = 0;
    for (const auto& kv : keys) {
        if (kv.first.empty() || t.Has(kv.first)) continue;
        t.Add(kv.first);
        t.Set(kv.first, def, kv.second);
        ++added;
    }
    Commit(ed, before);
    return added;
}

std::string GraphName(const DialogueGraph& g, const std::string& file) {
    return !g.name.empty() ? g.name : fs::u8path(file).stem().u8string();
}

// ---- Window state ------------------------------------------------------------------------
char s_search[128] = "";
LocWindowFilter s_filter = LocWindowFilter::All;
std::string s_filterLang;
bool s_showReport = false;
bool s_rescan = false;
bool s_haveScan = false;
std::string s_scanDir;
LocScanResult s_scan;          // the last LocScanProject, including the open scene
std::string s_selected;
std::vector<LocKeyUse> s_uses; // the selected key's uses in the scene
std::string s_usesKey;
std::uint64_t s_usesStamp = 0;
std::string s_prevLanguage;    // the language to go back to from the pseudo preview
// Popups.
std::string s_newKey, s_newText, s_renameFrom, s_renameTo, s_newCode, s_newName, s_langEdit;
bool s_openAddKey = false, s_openRename = false, s_openAddLang = false, s_openLang = false;

void SelectKey(const std::string& key) { s_selected = key; s_usesKey.clear(); }

// The uses of the selected key in the scene, refreshed when the scene or table changes.
const std::vector<LocKeyUse>& SelectedUses(const EditorState& ed) {
    const std::uint64_t stamp = ed.scene().StructureVersion() * 1000003ull + ed.scene().ComponentVersion() * 31ull +
                                Localization::Version();
    if (s_usesKey != s_selected || stamp != s_usesStamp) {
        s_uses.clear();
        for (LocKeyUse& u : CollectLocKeys(ed.scene())) if (u.key == s_selected) s_uses.push_back(std::move(u));
        s_usesKey = s_selected;
        s_usesStamp = stamp;
    }
    return s_uses;
}

// Point the open scene's texts at a renamed key (one scene undo step when any used it).
int RenameInScene(EditorState& ed, const std::string& from, const std::string& to) {
    std::vector<std::string*> refs;
    auto use = [&](std::string& k) { if (k == from) refs.push_back(&k); };
    for (const auto& up : ed.scene().Objects()) {
        GameObject* go = up.get();
        if (auto* c = go->GetComponent<TextRenderer>()) use(c->loc.key);
        if (auto* c = go->GetComponent<UIButton>()) use(c->loc.key);
        if (auto* c = go->GetComponent<UIToggle>()) use(c->loc.key);
        if (auto* c = go->GetComponent<UITooltip>()) use(c->loc.key);
        if (auto* c = go->GetComponent<WorldUI>()) use(c->loc.key);
        if (auto* c = go->GetComponent<UIInputField>()) use(c->loc.key);
        if (auto* ls = go->GetComponent<LoadingScreen>(); ls && ls->localized) {
            use(ls->title); use(ls->subtitle);
            for (auto& tip : ls->tips) use(tip);
        }
    }
    if (refs.empty()) return 0;
    ed.PushUndo();
    for (std::string* k : refs) *k = to;
    ed.dirty = true;
    return (int)refs.size();
}

// The dialogue graphs and quests of the project and the scene, and the words of the
// dialogue box and quest journal: their keys with the text typed in them.
std::vector<std::pair<std::string, std::string>> StoryStrings(const EditorState& ed) {
    std::vector<std::pair<std::string, std::string>> out;
    bool dialogue = false, quests = false;
    auto addAll = [&](const std::vector<std::pair<std::string, std::string>>& v) { out.insert(out.end(), v.begin(), v.end()); };
    for (const auto& up : ed.scene().Objects()) {
        if (auto* dr = up->GetComponent<DialogueRunner>()) {
            DialogueGraph g = dr->Graph();
            if (g.nodes.empty() && !dr->graphFile.empty()) g.Load(dr->ResolvedGraphFile());
            addAll(DialogueStrings(g, GraphName(g, dr->graphFile)));
            dialogue = true;
        }
        if (auto* ql = up->GetComponent<QuestLog>()) {
            for (const QuestDef& q : ql->quests) addAll(QuestStrings(q));
            quests = true;
        }
    }
    if (!ed.projectDir().empty()) {
        std::error_code ec;
        for (auto it = fs::recursive_directory_iterator(fs::u8path(ed.projectDir()), fs::directory_options::skip_permission_denied, ec);
             it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (ec) break;
            const std::string ext = Lower(it->path().extension().u8string());
            const std::string path = it->path().u8string();
            if (ext == ".okaydialog") {
                DialogueGraph g;
                if (g.Load(path)) { addAll(DialogueStrings(g, GraphName(g, path))); dialogue = true; }
            } else if (ext == ".okayquest") {
                std::vector<QuestDef> qs;
                if (QuestFile::Load(path, qs)) { for (const QuestDef& q : qs) addAll(QuestStrings(q)); quests = true; }
            }
        }
    }
    if (dialogue) addAll(DialogueRunner::LocKeys());
    if (quests) addAll(QuestLog::LocKeys());
    return out;
}

// ---- Window sections -----------------------------------------------------------------------
void PreviewCombo(const char* id, float width) {
    const std::string cur = Localization::Language();
    const std::string label = cur == Localization::kPseudo ? std::string("Pseudo-localized")
                            : Localization::LanguageName(cur) + " (" + cur + ")";
    ImGui::SetNextItemWidth(width);
    if (ImGui::BeginCombo(id, label.c_str())) {
        for (const std::string& code : Localization::Languages()) {
            const std::string item = Localization::LanguageName(code) + " (" + code + ")";
            if (ImGui::Selectable(item.c_str(), code == cur)) Localization::SetLanguage(code);
        }
        ImGui::Separator();
        if (ImGui::Selectable("Pseudo-localized", cur == Localization::kPseudo)) {
            if (cur != Localization::kPseudo) s_prevLanguage = cur;
            Localization::SetLanguage(Localization::kPseudo);
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("The default language with accented letters and about a third more length,\n"
                              "in [brackets]: text that is cut off, or that never went through the table, shows up.");
        ImGui::EndCombo();
    }
}

// The open scene and its unsaved scripts, for LocScan. Saved files are LocScanProject's job.
void FillScanQuery(const EditorState& ed, LocWindowQuery& query) {
    query.scan = true;
    query.projectDir = ed.projectDir();
    for (const LocKeyUse& use : CollectLocKeys(ed.scene())) query.sceneKeys.push_back(use.key);
    int n = 0;
    for (const auto& up : ed.scene().Objects()) {
        if (!up) continue;
        for (auto* sc : up->GetComponents<ScriptComponent>())
            query.extraFiles.emplace_back("scene-script-" + std::to_string(n++) + ".okay", sc->Source());
        for (auto* vc : up->GetComponents<VisualScriptComponent>())
            query.extraFiles.emplace_back("scene-graph-" + std::to_string(n++) + ".okayvs", vc->Source());
    }
}

void RunScan(const EditorState& ed) {
    LocWindowQuery query;
    FillScanQuery(ed, query);
    s_scan = ScanLocWindow(Localization::Table(), query);
    s_haveScan = true;
    s_scanDir = ed.projectDir();
}

void DrawLanguages() {
    StringTable& t = Localization::Table();
    const std::vector<LocStats> stats = LocTable(t).AllStats();
    const std::string def = t.DefaultLanguage();
    const float barW = 170.0f;
    const float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
    for (std::size_t i = 0; i < t.languages.size(); ++i) {
        const LocLanguage& l = t.languages[i];
        if (i > 0 && ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + barW <= right) ImGui::SameLine();
        ImGui::PushID((int)i);
        const LocStats* st = (i < stats.size() && stats[i].language == l.code) ? &stats[i] : nullptr;
        const float done = st ? st->Completion() : t.Completion(l.code);
        char overlay[96];
        std::snprintf(overlay, sizeof(overlay), "%s%s  %d%%", l.code.c_str(), l.code == def ? " *" : "", (int)(done * 100.0f + 0.5f));
        const ImVec4 fill = done >= 0.999f ? ImVec4(0.30f, 0.66f, 0.40f, 1.0f) : done >= 0.5f ? AccentCol(0.9f) : ImVec4(0.75f, 0.45f, 0.25f, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, fill);
        ImGui::ProgressBar(done, ImVec2(barW, 0), overlay);
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) {
            const int have = st ? st->total - st->missing : (int)(t.Completion(l.code) * (float)t.Size() + 0.5f);
            const int total = st ? st->total : (int)t.Size();
            std::string counts = st ? std::to_string(st->missing) + " missing, " + std::to_string(st->placeholderMismatch) +
                                          " placeholder mismatches, " + std::to_string(st->sameAsSource) + " same as the source\n"
                                    : std::string();
            ImGui::SetTooltip("%s (%s): %d of %d keys have text%s\n%s%s\nClick for its name, font and default; double-click to preview it.",
                              l.name.c_str(), l.code.c_str(), have, total, l.code == def ? " (the default language)" : "",
                              counts.c_str(), l.font.empty() ? "Font: the one each text uses" : ("Font: " + l.font).c_str());
        }
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) Localization::SetLanguage(l.code);
            else { s_langEdit = l.code; s_openLang = true; }
        }
        ImGui::PopID();
    }
    if (!t.languages.empty()) ImGui::SameLine();
    if (ImGui::SmallButton("+ Language")) { s_newCode.clear(); s_newName.clear(); s_openAddLang = true; }
    Tip("Add a language (a code like fr, de, ja, pt-BR, and the name players see).");
}

ImU32 CellTint(LocCellState state) {
    switch (state) {
        case LocCellState::Missing: return IM_COL32(130, 52, 40, 90);
        case LocCellState::PlaceholderMismatch: return IM_COL32(176, 120, 32, 120);
        case LocCellState::SameAsSource: return IM_COL32(64, 96, 140, 80);
        case LocCellState::Extra: return IM_COL32(120, 72, 140, 80);
        case LocCellState::Translated: break;
    }
    return 0;
}

void DrawTable(EditorState& ed, float height, const LocGrid& grid) {
    StringTable& t = Localization::Table();
    const int nLang = (int)t.languages.size();
    const ImGuiTableFlags flags = ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY |
                                  ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersOuter |
                                  ImGuiTableFlags_SizingFixedFit;
    if (!ImGui::BeginTable("##strings", 2 + nLang, flags, ImVec2(0, height))) return;
    ImGui::TableSetupScrollFreeze(1, 1);
    ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_NoHide, 190.0f);
    ImGui::TableSetupColumn("Note", 0, 150.0f);
    for (const LocLanguage& l : t.languages) {
        const std::string head = l.name + " (" + l.code + ")" + (l.code == t.DefaultLanguage() ? " *" : "");
        ImGui::TableSetupColumn(head.c_str(), 0, 210.0f);
    }
    ImGui::TableHeadersRow();
    ImGuiListClipper clip;
    clip.Begin(grid.RowCount());
    std::string renameKey, deleteKey;
    while (clip.Step()) {
        for (int r = clip.DisplayStart; r < clip.DisplayEnd; ++r) {
            const LocGridRow& grow = grid.rows[(std::size_t)r];
            const std::string& key = grow.key;
            LocEntry* e = t.Find(key);
            if (!e) continue;
            ImGui::TableNextRow();
            ImGui::PushID(key.c_str());
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            if (ImGui::Selectable(key.c_str(), s_selected == key)) SelectKey(key);
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) renameKey = key;
            if (ImGui::BeginPopupContextItem("##keymenu")) {
                SelectKey(key);
                if (ImGui::MenuItem("Rename...")) renameKey = key;
                if (ImGui::MenuItem("Copy key")) ImGui::SetClipboardText(key.c_str());
                if (ImGui::MenuItem("Delete")) deleteKey = key;
                ImGui::EndPopup();
            }
            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(-FLT_MIN);
            std::string note = e->note;
            if (InputString("##note", note, "note for translators")) { BeginChange(); e->note = note; }
            if (ImGui::IsItemDeactivatedAfterEdit()) EndEdit(ed);
            for (int li = 0; li < nLang; ++li) {
                const std::string code = t.languages[(std::size_t)li].code;
                ImGui::TableSetColumnIndex(2 + li);
                LocCellState state = LocCellState::Translated;
                if ((std::size_t)li < grid.languages.size() && grid.languages[(std::size_t)li] == code &&
                    (std::size_t)li < grow.cells.size())
                    state = grow.cells[(std::size_t)li].state;
                else if (const int column = grid.ColumnOf(code); column >= 0 && column < (int)grow.cells.size())
                    state = grow.cells[(std::size_t)column].state;
                const std::string* cur = t.Text(key, code);
                if (const ImU32 tint = CellTint(state)) ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, tint);
                std::string cell = cur ? ToCell(*cur) : std::string();
                ImGui::PushID(li);
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (InputString("##v", cell, "missing")) {
                    BeginChange();
                    t.Set(key, code, FromCell(cell));
                    Localization::TableChanged();   // the scene shows it as you type
                }
                if (ImGui::IsItemActivated()) SelectKey(key);
                if (ImGui::IsItemDeactivatedAfterEdit()) EndEdit(ed);
                cur = t.Text(key, code);   // (an edit may have changed or cleared it)
                const bool cut = cur && (cur->find('\n') != std::string::npos ||
                    ImGui::CalcTextSize(cell.c_str()).x > ImGui::GetItemRectSize().x - ImGui::GetStyle().FramePadding.x * 2.0f);
                if (ImGui::IsItemHovered() && !ImGui::IsItemActive() && (cut || state != LocCellState::Translated)) {
                    std::string tip = state == LocCellState::Translated ? std::string() : LocCellStateName(state);
                    if (cur && cut) {
                        if (!tip.empty()) tip += "\n";
                        tip += *cur;
                    } else if (state == LocCellState::Missing) {
                        if (!tip.empty()) tip += "\n";
                        tip += "No text in this language.";
                    }
                    if (!tip.empty()) ImGui::SetTooltip("%s", tip.c_str());
                }
                ImGui::PopID();
            }
            ImGui::PopID();
        }
    }
    ImGui::EndTable();
    if (!renameKey.empty()) { s_renameFrom = renameKey; s_renameTo = renameKey; s_openRename = true; }
    if (!deleteKey.empty()) {
        const std::string before = t.Write();
        t.Remove(deleteKey);
        Commit(ed, before);
        if (s_selected == deleteKey) SelectKey("");
        ConsoleLog("Localization: deleted \"" + deleteKey + "\" (Ctrl+Z in the window brings it back)");
    }
}

void DrawSelected(EditorState& ed) {
    if (s_selected.empty()) {
        ImGui::TextDisabled("Select a key to see where the scene uses it.");
        return;
    }
    if (!Localization::Table().Has(s_selected)) {
        ImGui::TextDisabled("\"%s\" is used, but it is not in the table. Add it with + Key.", s_selected.c_str());
        return;
    }
    ImGui::AlignTextToFramePadding();
    ImGui::Text("%s", s_selected.c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton("Rename")) { s_renameFrom = s_selected; s_renameTo = s_selected; s_openRename = true; }
    ImGui::SameLine();
    if (ImGui::SmallButton("Copy")) ImGui::SetClipboardText(s_selected.c_str());
    const auto& uses = SelectedUses(ed);
    ImGui::SameLine();
    if (uses.empty()) ImGui::TextDisabled("Find in Scene: nothing in this scene uses it.");
    else {
        ImGui::TextDisabled("Find in Scene:");
        for (std::size_t i = 0; i < uses.size(); ++i) {
            const LocKeyUse& u = uses[i];
            if (!u.object) continue;
            ImGui::SameLine();
            ImGui::PushID((int)i);
            const std::string label = u.object->name + " (" + u.where + ")";
            if (ImGui::SmallButton(label.c_str())) ed.Select(u.object);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Select %s", u.object->name.c_str());
            ImGui::PopID();
        }
    }
    const std::string shown = Localization::Language();
    if (!LocWindowShowsPreview(shown)) return;
    const bool pseudo = shown == Localization::kPseudo;
    const LocWindowPreview preview = PreviewLocKey(Localization::Table(), s_selected, pseudo ? std::string() : shown, pseudo);
    const char* previewLabel = preview.mirrored && !preview.pseudo ? "Mirrored preview" : "Pseudo preview";
    ImGui::TextDisabled("%s", previewLabel);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-FLT_MIN);
    std::string shownText = preview.text;
    InputString("##locpreviewline", shownText, nullptr, ImGuiInputTextFlags_ReadOnly);
    if (ImGui::IsItemHovered()) {
        const char* tip = preview.pseudo && preview.mirrored
            ? "Pseudo-localized and mirrored for a right-to-left language. Placeholders stay whole.\n"
              "This line does not change the table. The Scene view follows the Pseudo language."
            : preview.pseudo
            ? "Pseudo-localized: accented letters and about a third more length, in markers.\n"
              "A placeholder such as {0} stays put. This line does not change the table."
            : "Mirrored so a right-to-left language can be read here. The stored text is unchanged;\n"
              "the Scene view still draws left to right.";
        ImGui::SetTooltip("%s", tip);
    }
}

void DrawPopups(EditorState& ed) {
    StringTable& t = Localization::Table();
    if (s_openAddKey) { ImGui::OpenPopup("Add Key##loc"); s_openAddKey = false; }
    if (s_openRename) { ImGui::OpenPopup("Rename Key##loc"); s_openRename = false; }
    if (s_openAddLang) { ImGui::OpenPopup("Add Language##loc"); s_openAddLang = false; }
    if (s_openLang) { ImGui::OpenPopup("Language##loc"); s_openLang = false; }

    if (ImGui::BeginPopup("Add Key##loc")) {
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        InputString("Key", s_newKey, "menu.start");
        const std::string def = t.languages.empty() ? std::string("en") : t.DefaultLanguage();
        InputString(("Text (" + def + ")").c_str(), s_newText, "Start");
        const bool exists = t.Has(s_newKey);
        if (exists) ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "That key exists already.");
        ImGui::BeginDisabled(s_newKey.empty() || exists);
        if (ImGui::Button("Add", ImVec2(120, 0)) || (ImGui::IsKeyPressed(ImGuiKey_Enter) && !s_newKey.empty() && !exists)) {
            const std::string before = t.Write();
            const std::string lang = EnsureLanguage(t);
            t.Add(s_newKey);
            t.Set(s_newKey, lang, FromCell(s_newText));
            Commit(ed, before);
            SelectKey(s_newKey);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopup("Rename Key##loc")) {
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        InputString("New name", s_renameTo);
        const bool clash = s_renameTo != s_renameFrom && t.Has(s_renameTo);
        if (clash) ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "That key exists already.");
        ImGui::TextDisabled("The scene's texts follow; scripts that name the key need it changed too.");
        ImGui::BeginDisabled(s_renameTo.empty() || clash || s_renameTo == s_renameFrom);
        if (ImGui::Button("Rename", ImVec2(120, 0))) {
            const std::string before = t.Write();
            if (t.Rename(s_renameFrom, s_renameTo)) {
                Commit(ed, before);
                const int n = RenameInScene(ed, s_renameFrom, s_renameTo);
                if (s_selected == s_renameFrom) SelectKey(s_renameTo);
                ConsoleLog("Localization: renamed \"" + s_renameFrom + "\" to \"" + s_renameTo + "\"" +
                           (n > 0 ? " (" + std::to_string(n) + " in the scene)" : std::string()));
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopup("Add Language##loc")) {
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        InputString("Code", s_newCode, "fr, de, ja, pt-BR");
        InputString("Name", s_newName, "Français");
        const bool exists = t.FindLanguage(s_newCode) != nullptr;
        if (exists) ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "The table has that language.");
        ImGui::BeginDisabled(s_newCode.empty() || exists || s_newCode == Localization::kPseudo);
        if (ImGui::Button("Add", ImVec2(120, 0))) {
            const std::string before = t.Write();
            const bool first = t.languages.empty();
            t.AddLanguage(s_newCode, s_newName.empty() ? s_newCode : s_newName);
            if (first) t.defaultLanguage = s_newCode;
            Commit(ed, before);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopup("Language##loc")) {
        LocLanguage* l = t.FindLanguage(s_langEdit);
        if (!l) { ImGui::CloseCurrentPopup(); ImGui::EndPopup(); return; }
        ImGui::TextDisabled("Language %s", l->code.c_str());
        std::string name = l->name;
        if (InputString("Name", name)) { BeginChange(); l->name = name; }
        if (ImGui::IsItemDeactivatedAfterEdit()) EndEdit(ed);
        std::string font = l->font;
        if (InputString("Font", font, "the one each text uses")) { BeginChange(); l->font = font; }
        if (ImGui::IsItemDeactivatedAfterEdit()) EndEdit(ed);
        ImGui::SameLine();
        if (ImGui::SmallButton("...")) {
            // May answer on a later frame (built-in browser): keep pointers to long-lived data only.
            const EditorState* edp = &ed; auto* lp = l;
            okay::filedialog::OpenOne("A font with this language's characters", "", okay::filedialog::Filters("Fonts", {"*.ttf", "*.otf", "*.ttc"}),
                [edp, lp](const std::string& p) {
                std::string path = p;
                std::error_code ec;   // inside the project: relative to it, so the build and other machines find it
                if (!edp->projectDir().empty()) {
                    const fs::path rel = fs::relative(fs::u8path(path), fs::u8path(edp->projectDir()), ec);
                    if (!ec && !rel.empty() && rel.native().find(fs::path("..").native()) != 0) path = rel.generic_u8string();
                }
                const std::string before = Localization::Table().Write();
                lp->font = path;
                Commit(*edp, before);
            });
        }
        Tip("Text in this language is drawn with this font when the font a text uses lacks its characters\n"
            "(Japanese, Chinese, Korean, Arabic...). Build Game ships it.");
        const bool isDefault = t.DefaultLanguage() == l->code;
        ImGui::BeginDisabled(isDefault);
        if (ImGui::Button(isDefault ? "The default language" : "Make it the default")) {
            const std::string before = t.Write();
            t.defaultLanguage = l->code;
            Commit(ed, before);
        }
        ImGui::EndDisabled();
        Tip("Keys without a translation show the default language's text.");
        ImGui::SameLine();
        if (ImGui::Button("Preview")) Localization::SetLanguage(l->code);
        ImGui::SameLine();
        if (ImGui::Button("Remove")) {
            const std::string before = t.Write();
            const std::string code = l->code;
            t.RemoveLanguage(code);
            Commit(ed, before);
            ConsoleLog("Localization: removed " + code + " and its texts (Ctrl+Z in the window brings them back)");
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void ImportCsvFrom(const EditorState& ed, const std::string& p);
void ExportCsvTo(const std::string& p);
void ImportCsv(const EditorState& ed) {
    const EditorState* edp = &ed;
    okay::filedialog::OpenOne("Import translations (CSV)", "", okay::filedialog::Filters("CSV", {"*.csv", "*.txt"}),
        [edp](const std::string& p) { ImportCsvFrom(*edp, p); });
}

void ImportCsvFrom(const EditorState& ed, const std::string& p) {
    std::ifstream f(fs::u8path(p), std::ios::binary);
    if (!f) { ConsoleLog("Localization: couldn't read " + p, 2); return; }
    std::stringstream ss;
    ss << f.rdbuf();
    StringTable& live = Localization::Table();
    StringTable after = live;
    std::string err;
    int rows = 0;
    if (!after.ImportCsv(ss.str(), &err, &rows)) { ConsoleLog("Localization: couldn't import " + p + ": " + err, 2); return; }
    const std::string before = live.Write();
    if (after.Write() == before) {
        s_status = "Import changed nothing";
        ConsoleLog("Localization: " + p + " changed nothing");
        return;
    }
    const LocDiff diff = LocDiffTables(live, after);
    live = std::move(after);
    Commit(ed, before);
    std::string note = "Imported " + std::to_string(rows) + " rows";
    auto add = [&](int n, const char* what) { if (n > 0) note += ", " + std::to_string(n) + " " + what; };
    add((int)diff.filled.size(), "filled");
    add((int)diff.changed.size(), "changed");
    add((int)diff.addedKeys.size(), "new keys");
    add((int)diff.addedLanguages.size(), "new languages");
    add((int)diff.cleared.size(), "cleared");
    if (diff.notesChanged) add(diff.notesChanged, "notes");
    if (s_status.rfind("Saved ", 0) == 0 || s_status.rfind("No project", 0) == 0) s_status = note;
    ConsoleLog("Localization: " + note + " from " + p);
}

void ExportCsv(const EditorState& ed) {
    const std::string suggest = ed.projectDir().empty() ? std::string("strings.csv")
                              : (fs::u8path(ed.projectDir()) / "strings.csv").u8string();
    okay::filedialog::SaveOne("Export for translators (CSV)", suggest, okay::filedialog::Filters("CSV", {"*.csv"}),
        [](const std::string& p) { ExportCsvTo(p); });
}

void ExportCsvTo(const std::string& p) {
    std::ofstream f(fs::u8path(p), std::ios::binary | std::ios::trunc);
    f << Localization::Table().ExportCsv();
    if (f) ConsoleLog("Localization: exported " + std::to_string(Localization::Table().Size()) + " keys to " + p);
    else ConsoleLog("Localization: couldn't write " + p, 2);
}

std::string FilterLabel() {
    switch (s_filter) {
        case LocWindowFilter::All: return "Every key";
        case LocWindowFilter::MissingIn: return "Missing in " + s_filterLang;
        case LocWindowFilter::PlaceholderMismatch: return "Placeholder mismatches";
        case LocWindowFilter::SameAsSource: return "Same as source";
        case LocWindowFilter::Unused: return "Unused";
        case LocWindowFilter::Issues: return "Reported issues";
    }
    return "Every key";
}

void JumpToKey(const LocWindowModel& model, const std::string& key) {
    SelectKey(key);
    for (const LocGridRow& row : model.grid.rows)
        if (row.key == key) return;
    s_filter = LocWindowFilter::All;
    const std::string needle = Lower(s_search);
    if (!needle.empty() && Lower(key).find(needle) == std::string::npos) s_search[0] = 0;
}

void DrawCheck(const LocWindowModel& model) {
    const std::string summary = LocWindowIssueSummary(model);
    const std::vector<LocWindowIssue> issues = LocWindowIssues(model);
    if (issues.empty()) ImGui::TextDisabled("%s", summary.c_str());
    else ImGui::TextUnformatted(summary.c_str());
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Missing translations and placeholder mismatches, from the table check.\n"
                          "Coloured cells match it: red is missing, amber is a placeholder mismatch,\n"
                          "blue is the same as the source, purple is extra text.");
    ImGui::SameLine();
    if (ImGui::SmallButton(s_haveScan ? "Rescan" : "Scan project")) s_rescan = true;
    Tip("Read scripts, scenes, prefabs, dialogue and quests. Unused keys can be filtered,\n"
        "and keys the project uses that the table lacks are added to this report.");
    if (issues.empty()) s_showReport = false;
    else {
        ImGui::SameLine();
        if (ImGui::SmallButton(s_showReport ? "Hide report" : "Show report")) s_showReport = !s_showReport;
    }
    if (!s_showReport) return;
    ImGui::BeginChild("##locreport", ImVec2(0, 120.0f), true);
    for (const LocMissing& missing : model.missing) {
        if (missing.keys.empty()) continue;
        ImGui::TextDisabled("%s: %d of %d keys missing (%.0f%% done)", missing.language.c_str(),
                            (int)missing.keys.size(), missing.total, (double)missing.completion * 100.0);
    }
    const int shown = std::min(300, (int)issues.size());
    for (int i = 0; i < shown; ++i) {
        const LocWindowIssue& issue = issues[(std::size_t)i];
        ImGui::PushID(i);
        const std::string where = issue.language.empty() ? std::string("project") : issue.language;
        const std::string label = std::string(LocSeverityName(issue.severity)) + "   " + where + "   " + issue.key + "   " + issue.message;
        if (ImGui::Selectable(label.c_str(), s_selected == issue.key)) JumpToKey(model, issue.key);
        ImGui::PopID();
    }
    if ((int)issues.size() > shown)
        ImGui::TextDisabled("And %d more. Use the filter to narrow the grid.", (int)issues.size() - shown);
    ImGui::EndChild();
}

} // namespace

// =====================================================================================
// The window
// =====================================================================================
void DrawLocalizationWindow(EditorState& ed) {
    if (!g_showLocalization) { s_focused = false; return; }
    ImGui::SetNextWindowSize(ImVec2(980, 560), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Localization", &g_showLocalization)) { s_focused = false; ImGui::End(); return; }
    s_focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    StringTable& t = Localization::Table();
    const std::string path = TablePath(ed);
    ImGui::TextDisabled("%s", path.empty() ? "No project open: the strings last until the editor closes."
                                           : (path + (s_status.empty() ? std::string() : "   " + s_status)).c_str());

    // Toolbar.
    if (ImGui::Button("+ Key")) { s_newKey.clear(); s_newText.clear(); s_openAddKey = true; }
    Tip("Add a key and its text in the default language.");
    ImGui::SameLine();
    if (ImGui::Button("Import CSV...")) ImportCsv(ed);
    Tip("Read a translator's spreadsheet (key, note, then a column per language code;\n"
        "comma, semicolon or tab separated). New keys and languages are added; empty cells keep the text.");
    ImGui::SameLine();
    if (ImGui::Button("Export CSV...")) ExportCsv(ed);
    Tip("Write every key and language as a spreadsheet for translators (UTF-8).");
    ImGui::SameLine();
    if (ImGui::Button("Add Dialogue & Quest Text")) {
        const int n = AddMissing(ed, StoryStrings(ed));
        ConsoleLog("Localization: added " + std::to_string(n) + " keys from the dialogue graphs and quests");
    }
    Tip("Add a key for every line, choice and speaker of the project's dialogue graphs, every quest title,\n"
        "description and objective, and the dialogue box's and journal's own words, with the text typed in them.\n"
        "Translate those keys and the game shows the translation; text without a key shows as typed.");
    ImGui::SameLine();
    const bool pseudo = Localization::Language() == Localization::kPseudo;
    if (AccentToggleButton("Pseudo-localize", pseudo)) {
        if (pseudo) Localization::SetLanguage(s_prevLanguage.empty() ? t.DefaultLanguage() : s_prevLanguage);
        else { s_prevLanguage = Localization::Language(); Localization::SetLanguage(Localization::kPseudo); }
    }
    Tip("Preview every localized text accented and about a third longer, in [brackets]:\n"
        "text that gets cut off, and text that never went through the table, stand out.\n"
        "The selected key also shows a marked pseudo-locale preview under the grid.");
    ImGui::SameLine();
    ImGui::TextUnformatted("Preview");
    ImGui::SameLine();
    PreviewCombo("##locpreview", 170.0f);

    if (s_scanDir != ed.projectDir()) {
        s_haveScan = false;
        s_scan = {};
        s_scanDir = ed.projectDir();
    }

    // Languages and their completion (LocTable stats).
    DrawLanguages();

    // Search and filter.
    SearchField("##locsearch", s_search, sizeof(s_search), "Search keys and text", 260.0f);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(210.0f);
    if (ImGui::BeginCombo("##locfilter", FilterLabel().c_str())) {
        if (ImGui::Selectable("Every key", s_filter == LocWindowFilter::All)) s_filter = LocWindowFilter::All;
        for (const LocLanguage& l : t.languages) {
            const std::string item = "Missing in " + l.code;
            if (ImGui::Selectable(item.c_str(), s_filter == LocWindowFilter::MissingIn && s_filterLang == l.code)) {
                s_filter = LocWindowFilter::MissingIn;
                s_filterLang = l.code;
            }
        }
        if (ImGui::Selectable("Placeholder mismatches", s_filter == LocWindowFilter::PlaceholderMismatch))
            s_filter = LocWindowFilter::PlaceholderMismatch;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Translations whose {0} or {name} placeholders differ from the source text.");
        if (ImGui::Selectable("Same as source", s_filter == LocWindowFilter::SameAsSource))
            s_filter = LocWindowFilter::SameAsSource;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Text copied from the source language. It may still need a translation.");
        if (ImGui::Selectable("Unused", s_filter == LocWindowFilter::Unused)) s_filter = LocWindowFilter::Unused;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Keys nothing in this scene, and nothing the last scan found, mentions.\n"
                              "Scan project reads scripts, scenes, dialogue and quests.");
        if (ImGui::Selectable("Reported issues", s_filter == LocWindowFilter::Issues))
            s_filter = LocWindowFilter::Issues;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Keys with a missing translation, a placeholder mismatch, or another check warning.");
        ImGui::EndCombo();
    }
    if (s_filter == LocWindowFilter::MissingIn && !t.FindLanguage(s_filterLang)) s_filter = LocWindowFilter::All;
    if (s_rescan || (s_filter == LocWindowFilter::Unused && !s_haveScan)) {
        RunScan(ed);
        s_rescan = false;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%d keys, %d languages", (int)t.Size(), (int)t.languages.size());

    // After the search box and the filter, so this frame's text is the one the grid uses.
    // A scan reads the project once; later frames only compare that result with the table.
    LocWindowQuery query;
    query.search = s_search;
    query.filter = s_filter;
    query.filterLanguage = s_filterLang;
    const LocWindowModel model = BuildLocWindowModel(t, query, s_haveScan ? &s_scan : nullptr);

    if (t.Size() > 0) DrawCheck(model);

    // The table, then the selected key's uses and preview.
    const bool previewLine = LocWindowShowsPreview(Localization::Language()) && !s_selected.empty() && t.Has(s_selected);
    const float detailsH = ImGui::GetFrameHeightWithSpacing() * (previewLine ? 2.0f : 1.0f) + ImGui::GetStyle().ItemSpacing.y;
    if (t.languages.empty() && t.Size() == 0) {
        ImGui::Spacing();
        ImGui::TextWrapped("No strings yet. Add a language (+ Language), then keys (+ Key); or tick Localized on a "
                           "Text, Button or Toggle in the Inspector to make a key from its text. Scripts read keys with "
                           "loc(\"key\") or Localization.Get(\"key\").");
    } else {
        DrawTable(ed, std::max(80.0f, ImGui::GetContentRegionAvail().y - detailsH), model.grid);
        DrawSelected(ed);
    }
    DrawPopups(ed);
    ImGui::End();
}

bool LocalizationFocused() { return s_focused; }

bool LocalizationUndo(bool redo) {
    auto& from = redo ? s_redo : s_undo;
    auto& to = redo ? s_undo : s_redo;
    if (from.empty()) return false;
    to.push_back(Localization::Table().Write());
    StringTable t;
    t.Parse(from.back());
    from.pop_back();
    const std::string path = Localization::TablePath();
    Localization::SetTable(t, path);
    if (!path.empty()) t.Save(path);
    return true;
}

// =====================================================================================
// The toolbar's preview language
// =====================================================================================
void DrawLanguageToolbar(EditorState& ed) {
    (void)ed;
    if (Localization::Table().languages.empty()) return;
    ImGui::SameLine(0, 8.0f);
    const std::string cur = Localization::Language();
    std::string label = cur == Localization::kPseudo ? std::string("PSEUDO") : cur;
    for (char& c : label) c = (char)std::toupper((unsigned char)c);
    const float w = ImGui::CalcTextSize(label.c_str()).x + ImGui::GetFrameHeight() + ImGui::GetStyle().FramePadding.x * 2.0f;
    ImGui::SetNextItemWidth(w);
    if (ImGui::BeginCombo("##toollang", label.c_str())) {
        for (const std::string& code : Localization::Languages()) {
            const std::string item = Localization::LanguageName(code) + " (" + code + ")";
            if (ImGui::Selectable(item.c_str(), code == cur)) Localization::SetLanguage(code);
        }
        ImGui::Separator();
        if (ImGui::Selectable("Pseudo-localized", cur == Localization::kPseudo)) {
            if (cur != Localization::kPseudo) s_prevLanguage = cur;
            Localization::SetLanguage(Localization::kPseudo);
        }
        if (ImGui::Selectable("Edit strings...")) g_showLocalization = true;
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Preview language: the Scene and Game views (and Play) show localized text in it");
}

void LocalizationTick(EditorState& ed) {
    // Outside Play the scene isn't ticked: keep its localized text in the preview
    // language (a no-op until the language, the table or the scene changes).
    LocalizeScene(ed.scene());
}

// =====================================================================================
// Inspector
// =====================================================================================
bool LocKeyInput(EditorState& ed, const char* label, std::string& key) {
    StringTable& t = Localization::Table();
    bool changed = false;
    ImGui::PushID(label);
    const float pick = ImGui::GetFrameHeight();
    ImGui::SetNextItemWidth(std::max(60.0f, ImGui::CalcItemWidth() - pick - ImGui::GetStyle().ItemInnerSpacing.x));
    std::string edit = key;
    const bool typed = InputString("##key", edit, "string-table key");
    static ImGuiID s_typing = 0;   // the key field being typed into: one scene undo step per edit
    const ImGuiID typingId = ImGui::GetItemID();
    if (typed) {
        if (s_typing != typingId) { ed.PushUndo(); s_typing = typingId; }
        key = edit;
        changed = true;
    }
    if (ImGui::IsItemDeactivated() && s_typing == typingId) s_typing = 0;
    ImGui::SameLine(0, ImGui::GetStyle().ItemInnerSpacing.x);
    if (ImGui::ArrowButton("##pick", ImGuiDir_Down)) ImGui::OpenPopup("##keypick");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Pick a key from the string table");
    if (const char* hidden = std::strstr(label, "##"); hidden != label) {   // the label, up to a "##" id part
        ImGui::SameLine(0, ImGui::GetStyle().ItemInnerSpacing.x);
        ImGui::TextUnformatted(label, hidden);
    }
    if (ImGui::BeginPopup("##keypick")) {
        static char find[96] = "";
        if (ImGui::IsWindowAppearing()) { find[0] = 0; ImGui::SetKeyboardFocusHere(); }
        ImGui::SetNextItemWidth(320.0f);
        ImGui::InputTextWithHint("##find", "Search", find, sizeof(find));
        const std::string needle = Lower(find), def = t.DefaultLanguage();
        ImGui::BeginChild("##keys", ImVec2(420.0f, 260.0f));
        for (const auto& kv : t.Entries()) {
            const std::string* txt = t.Text(kv.first, def);
            if (!needle.empty() && Lower(kv.first).find(needle) == std::string::npos &&
                (!txt || Lower(*txt).find(needle) == std::string::npos)) continue;
            if (ImGui::Selectable(kv.first.c_str(), kv.first == key)) {
                ed.PushUndo();
                key = kv.first;
                changed = true;
                ImGui::CloseCurrentPopup();
            }
            if (txt) { ImGui::SameLine(190.0f); ImGui::TextDisabled("%s", ToCell(*txt).c_str()); }
        }
        if (t.Size() == 0) ImGui::TextDisabled("The string table is empty (View > Editors & Tools > Localization).");
        ImGui::EndChild();
        ImGui::EndPopup();
    }
    const std::string def = t.DefaultLanguage();
    if (const std::string* txt = t.Text(key, def)) ImGui::TextDisabled("%s: %s", def.c_str(), ToCell(*txt).c_str());
    else if (!key.empty()) ImGui::TextColored(ImVec4(1.0f, 0.62f, 0.30f, 1.0f), "\"%s\" isn't in the string table (it shows as the key).", key.c_str());
    ImGui::PopID();
    if (changed) ed.dirty = true;
    return changed;
}

bool LocalizedTextField(EditorState& ed, const char* id, LocalizedText& loc, std::string& literal, GameObject* go) {
    ImGui::PushID(id);
    bool on = !loc.key.empty();
    if (ImGui::Checkbox("Localized", &on)) {
        ed.PushUndo();
        if (on) loc.key = KeyForText(ed, literal, "ui." + Localization::KeyPart(go ? go->name : std::string("text")));
        else loc.key.clear();
        if (go) LocalizeObject(*go);
        ed.dirty = true;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Show a string-table key's text in the player's language instead of this literal text.\n"
                          "Turning it on uses the key whose text this is, or adds one (View > Editors & Tools > Localization).");
    if (on && LocKeyInput(ed, "Key", loc.key) && go) LocalizeObject(*go);
    ImGui::PopID();
    return on;
}

bool LocalizedSwitch(EditorState& ed, Component* c) {
    if (auto* pm = dynamic_cast<PauseMenu*>(c)) {
        bool on = pm->localized;
        if (ImGui::Checkbox("Localized##pm", &on)) {
            ed.PushUndo();
            pm->localized = on;
            if (on) {
                std::vector<std::pair<std::string, std::string>> keys;
                for (const auto& k : PauseMenu::LocKeys()) keys.push_back({k.key, pm->LiteralFor(k)});
                AddMissing(ed, keys);
            }
            pm->EnsureBuilt();
            pm->ApplyLocalization();
            LocalizeScene(ed.scene(), true);
            ed.dirty = true;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("The menu's title, buttons, slider labels and hint show the pause.* keys of the string table\n"
                              "in the player's language. Turning it on adds those keys with the menu's current words.");
        return pm->localized;
    }
    if (auto* hp = dynamic_cast<HoldPrompt*>(c)) {
        bool on = hp->localized;
        if (ImGui::Checkbox("Localized##hp", &on)) {
            ed.PushUndo();
            hp->localized = on;
            if (on) AddMissing(ed, HoldPrompt::LocKeys());
            ed.dirty = true;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("The prompts come from the hold.pickup, hold.drop and hold.either keys ({0} is the key name).\n"
                              "Turning it on adds them with the English lines.");
        return hp->localized;
    }
    if (auto* ls = dynamic_cast<LoadingScreen*>(c)) {
        bool on = ls->localized;
        if (ImGui::Checkbox("Localized##ls", &on)) {
            ed.PushUndo();
            StringTable& t = Localization::Table();
            const std::string def = t.DefaultLanguage();
            if (on) {   // the words become keys (made from them, or the keys that already hold them)
                const std::string base = "loading." + Localization::KeyPart(ls->gameObject ? ls->gameObject->name : std::string("screen"));
                if (!ls->title.empty()) ls->title = KeyForText(ed, ls->title, base + ".title");
                if (!ls->subtitle.empty()) ls->subtitle = KeyForText(ed, ls->subtitle, base + ".subtitle");
                for (std::size_t i = 0; i < ls->tips.size(); ++i)
                    if (!ls->tips[i].empty()) ls->tips[i] = KeyForText(ed, ls->tips[i], base + ".tip" + std::to_string(i + 1));
            } else {    // the keys become their default-language words again
                auto words = [&](std::string& f) { if (const std::string* v = t.Text(f, def)) f = *v; };
                words(ls->title); words(ls->subtitle);
                for (auto& tip : ls->tips) words(tip);
            }
            ls->localized = on;
            ed.dirty = true;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("The title, subtitle and tips are string-table keys, shown in the player's language.\n"
                              "Turning it on makes keys from the current words; off puts the words back.");
        return ls->localized;
    }
    return false;
}

// =====================================================================================
// Fonts and Build Game
// =====================================================================================
void MergeLocalizationFonts(float sizePx) {
    // The UI font has Latin, Greek and Cyrillic: add the first installed font with
    // CJK characters behind it.
    for (const std::string& path : SystemFallbackFonts()) {
        const TtfFont* f = GetFont(path);
        if (!f || !f->Has(0x65E5)) continue;   // 日
        ImFontConfig fc;
        fc.MergeMode = true;
        if (ImGui::GetIO().Fonts->AddFontFromFileTTF(path.c_str(), sizePx, &fc)) return;
    }
}

void CopyLocalizationFiles(EditorState& ed, const std::string& dataDir) {
    StringTable t = Localization::Table();
    if (t.Empty()) return;
    const fs::path data = fs::u8path(dataDir);
    const std::string table = TablePath(ed);
    const fs::path tableDir = table.empty() ? fs::path() : fs::u8path(table).parent_path();
    for (LocLanguage& l : t.languages) {   // each language's font ships beside the table
        if (l.font.empty()) continue;
        const fs::path font = fs::u8path(l.font);
        const fs::path src = font.is_relative() ? tableDir / font : font;
        fs::path rel = font.is_relative() ? font.lexically_normal() : font.filename();
        if (std::find(rel.begin(), rel.end(), fs::path("..")) != rel.end()) rel = font.filename();   // never outside the build
        std::error_code fe;
        if (!fs::exists(src, fe)) { ConsoleLog("Build: the " + l.code + " font " + l.font + " is missing", 1); continue; }
        fs::create_directories((data / rel).parent_path(), fe);
        fs::copy_file(src, data / rel, fs::copy_options::overwrite_existing, fe);
        if (fe) ConsoleLog("Build: couldn't copy " + l.font + " (" + fe.message() + ")", 1);
        l.font = rel.generic_u8string();
    }
    if (!t.Save((data / "strings.okayloc").u8string())) ConsoleLog("Build: couldn't write strings.okayloc", 2);
}

void LocalizationDevHooks(EditorState& ed) {
    static int s_frame = 0;
    if (s_frame < 100000) ++s_frame;
    if (std::getenv("OKAY_UI_LOC") && s_frame == 31) g_showLocalization = true;
    if (const char* lang = std::getenv("OKAY_UI_LANG"); lang && (s_frame == 33 || s_frame == 70)) Localization::SetLanguage(lang);
    if (const char* key = std::getenv("OKAY_UI_LOCKEY"); key && s_frame == 34) SelectKey(key);
    (void)ed;
}

} // namespace oked
