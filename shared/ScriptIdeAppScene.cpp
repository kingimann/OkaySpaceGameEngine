// Script Editor (ScriptIdeApp.hpp): the host's scene scripts in the tree, New Script, "Save changes?" when a changed tab is closed
// or the program quits, reopening closed tabs, and a document's language. Written from general editor behaviour and the OkayUI
// interface; no code, names, icons or look from any other editor (rule 19a).
#include "ScriptIdeAppImpl.hpp"

#include "okay/Scripting/ScriptVM.hpp"   // okay::AvailableScriptLanguages
#include "okay/Scripting/StarterScripts.hpp"

namespace scriptide {

namespace {
std::string LanguageTitle(const std::string& l) {
    return l == "okayscript" ? "OkayScript (.okay)" : l == "lua" ? "Lua (.lua)" : l == "javascript" ? "JavaScript (.js)"
         : l == "csharp" ? "C# (.cs)" : l;
}
} // namespace

std::vector<std::string> ScriptIdeApp::Impl::languages() const {
    std::vector<std::string> l = cfg.languages.empty() ? okay::AvailableScriptLanguages() : cfg.languages;
    if (l.empty()) l.push_back("okayscript");
    return l;
}

bool ScriptIdeApp::Impl::beginDialog(const std::string& id, float X, float Y, float W, float H, float bw, float bh, float& bx, float& by) {
    return okayuiapp::BeginDialog(id, X, Y, W, H, bw, bh, bx, by);
}

// ---- closing, quitting, reopening ---------------------------------------------------------------------------------------------

void ScriptIdeApp::Impl::requestClose(int index) {
    if (index < 0 || index >= (int)docs.size()) return;
    if (!dirty(*docs[(std::size_t)index])) { close(index); return; }
    active = index;
    promptOpen = true;
    promptQuit = false;
    promptKey = docs[(std::size_t)index]->key;
}

void ScriptIdeApp::Impl::requestQuit() {
    for (const auto& d : docs)
        if (dirty(*d)) { promptOpen = true; promptQuit = true; promptKey.clear(); return; }
    quit = true;
}

void ScriptIdeApp::Impl::answerPrompt(int answer) {
    if (!promptOpen) return;
    promptOpen = false;
    if (answer == 2) { promptQuit = false; promptKey.clear(); return; }   // Cancel
    if (promptQuit) {
        promptQuit = false;
        if (answer == 0 && !saveAll()) return;   // a file could not be saved: stay (the status line says which)
        quit = true;
        return;
    }
    const std::string key = promptKey;
    promptKey.clear();
    for (std::size_t i = 0; i < docs.size(); ++i) {
        if (docs[i]->key != key) continue;
        if (answer == 0 && !save(*docs[i])) return;   // not saved: the tab stays open
        close((int)i);
        return;
    }
}

bool ScriptIdeApp::Impl::reopenClosed() {
    while (!closed.empty()) {
        const Closed c = closed.back();
        closed.pop_back();
        if (byKey(c.key)) { showKey(c.key, 0); return true; }
        if (!c.path.empty() && c.key == c.path) { if (openFile(c.path, c.line)) return true; continue; }
        DocumentSpec spec;
        if (cfg.documentFor && cfg.documentFor(c.key, spec) && openDocument(spec, c.line)) return true;
    }
    notice = "No closed tab to reopen";
    return false;
}

// ---- the language ----------------------------------------------------------------------------------------------------------

bool ScriptIdeApp::Impl::setLanguage(Doc& d, const std::string& language) {
    if (language.empty() || language == d.language) return false;
    const std::string old = d.language;
    UI::CodeEdit& e = d.code.Edit();
    const std::string text = e.GetText();
    const std::string stem = d.path.empty() ? std::string() : fs::u8path(d.path).stem().u8string();
    std::string oldStarter = okay::StarterScriptSource(old.empty() ? std::string("okayscript") : old);
    const bool starter = text.empty() || text == oldStarter || (!stem.empty() && text == StarterScriptNamed(old, stem));
    d.language = language;
    d.code.SetLanguage(wordsFor(language));
    if (starter) {   // nothing written yet: the new language's starter, as one undo step
        const std::string fresh = stem.empty() ? std::string(okay::StarterScriptSource(language)) : StarterScriptNamed(language, stem);
        e.SelectAll();
        e.BreakUndoGroup();
        e.InsertText(fresh.c_str());
        e.BreakUndoGroup();
        d.seenVersion = e.Version();
    }
    check(d);
    if (cfg.languageChanged) cfg.languageChanged(d.key, language);
    notice = d.label + " is now " + LanguageTitle(language);
    return true;
}

void ScriptIdeApp::Impl::drawLanguageMenu(Doc* d) {
    if (!UI::BeginMenu("Script Language")) return;
    for (const std::string& l : languages())
        if (UI::MenuItem(LanguageTitle(l).c_str(), nullptr, d && d->language == l, d != nullptr) && d) setLanguage(*d, l);
    UI::EndMenu();
}

// ---- New Script ------------------------------------------------------------------------------------------------------------

void ScriptIdeApp::Impl::openNewScript() {
    newOpen = true;
    newFocus = true;
    newAttach = false;
    newTpl = 0;
    std::snprintf(newName, sizeof newName, "%s", "NewScript");
    const std::vector<std::string> langs = languages();
    if (newLang < 0 || newLang >= (int)langs.size()) newLang = 0;
}

bool ScriptIdeApp::Impl::createScript(const std::string& typed, const std::string& language, std::size_t tpl, bool attach,
                                      std::string* outPath) {
    if (folder.empty()) { notice = "Open a folder first: New Script puts the file in its Assets/Scripts"; return false; }
    const std::string stem = ScriptStem(typed);
    const std::string path = DefaultScriptPath(folder, stem, language);
    std::error_code ec;
    const bool exists = fs::exists(fs::u8path(path), ec);
    if (!exists) {
        fs::create_directories(fs::u8path(path).parent_path(), ec);
        const std::vector<ScriptTemplate> tpls = TemplatesFor(language);
        const std::string text = (tpl == 0 || tpl >= tpls.size()) ? StarterScriptNamed(language, stem)
                                                                  : FillTemplate(tpls[tpl].body, ClassIdentifier(stem));
        if (!WriteText(path, text)) { notice = "Could not write " + path; return false; }
        scan();
    }
    if (outPath) *outPath = path;
    std::string key;
    bool shown = false;
    if (attach && cfg.attachScript && cfg.attachScript(path, language, key)) {
        DocumentSpec spec;
        if (cfg.documentFor && cfg.documentFor(key, spec)) shown = openDocument(spec, 0);
    }
    if (!shown) shown = openFile(path, 0);
    notice = (exists ? "Opened " : "Created ") + relative(path);
    return shown;
}

void ScriptIdeApp::Impl::drawNewScript(float X, float Y, float W, float H) {
    float bx = 0, by = 0;
    const std::string target = cfg.attachTarget ? cfg.attachTarget() : std::string();
    if (beginDialog(cfg.idPrefix + "_newscript", X, Y, W, H, 460.0f, target.empty() ? 300.0f : 340.0f, bx, by)) {
        const std::vector<std::string> langs = languages();
        if (newLang < 0 || newLang >= (int)langs.size()) newLang = 0;
        const std::string lang = langs[(std::size_t)newLang];
        UI::SetCursorScreenPos(UI::Vec2(bx, by));
        UI::TextUnformatted("New Script: a script file you can attach to any object");
        UI::SetCursorScreenPos(UI::Vec2(bx, UI::GetCursorScreenPos().y + 6));
        UI::SetNextItemWidth(300);
        if (newFocus) { UI::SetKeyboardFocusHere(); newFocus = false; }
        const bool enter = UI::InputText("Name##newscript", newName, (int)sizeof newName, UI::InputText_EnterReturnsTrue);
        std::vector<std::string> titles;
        for (const std::string& l : langs) titles.push_back(LanguageTitle(l));
        std::vector<const char*> items;
        for (const std::string& t : titles) items.push_back(t.c_str());
        UI::SetCursorScreenPos(UI::Vec2(bx, UI::GetCursorScreenPos().y));
        UI::SetNextItemWidth(300);
        if (UI::Combo("Language##newscript", items.data(), (int)items.size(), &newLang)) newTpl = 0;
        const std::vector<ScriptTemplate> tpls = TemplatesFor(langs[(std::size_t)std::min(newLang, (int)langs.size() - 1)]);
        std::vector<const char*> tplTitles;
        for (const ScriptTemplate& t : tpls) tplTitles.push_back(t.title.c_str());
        if (newTpl < 0 || newTpl >= (int)tpls.size()) newTpl = 0;
        if (!tplTitles.empty()) {
            UI::SetCursorScreenPos(UI::Vec2(bx, UI::GetCursorScreenPos().y));
            UI::SetNextItemWidth(300);
            UI::Combo("Template##newscript", tplTitles.data(), (int)tplTitles.size(), &newTpl);
        }
        const std::string stem = ScriptStem(newName);
        const std::string path = folder.empty() ? std::string() : DefaultScriptPath(folder, stem, lang);
        std::error_code ec;
        const bool exists = !path.empty() && fs::exists(fs::u8path(path), ec);
        UI::SetCursorScreenPos(UI::Vec2(bx, UI::GetCursorScreenPos().y));
        UI::TextDisabled(folder.empty() ? "Open a folder first" : ("Assets/Scripts/" + stem + "." + ExtensionFor(lang)).c_str());
        if (exists) { UI::SetCursorScreenPos(UI::Vec2(bx, UI::GetCursorScreenPos().y)); UI::TextColored(226, 182, 76, "That file exists: it will be opened."); }
        if (!target.empty()) {
            UI::SetCursorScreenPos(UI::Vec2(bx, UI::GetCursorScreenPos().y));
            UI::Checkbox(("Also attach it to " + target).c_str(), &newAttach);
        }
        UI::SetCursorScreenPos(UI::Vec2(bx, UI::GetCursorScreenPos().y + 6));
        const bool create = UI::Button(exists ? "Open" : "Create") || enter;
        UI::SameLine();
        if (UI::Button("Cancel##newscript") || UI::IsKeyPressed(UI::KeyId_Escape, false)) newOpen = false;
        else if (create && !folder.empty()) {
            createScript(newName, lang, (std::size_t)newTpl, newAttach && !target.empty(), nullptr);
            newOpen = false;
        }
    }
    UI::End();
}

// ---- "Save changes?" ---------------------------------------------------------------------------------------------------------

void ScriptIdeApp::Impl::drawClosePrompt(float X, float Y, float W, float H) {
    float bx = 0, by = 0;
    if (beginDialog(cfg.idPrefix + "_closeprompt", X, Y, W, H, 460.0f, 104.0f, bx, by)) {
        std::string question;
        if (promptQuit) {
            int n = 0;
            for (const auto& d : docs) if (dirty(*d)) ++n;
            question = std::to_string(n) + (n == 1 ? " file has" : " files have") + " changes that are not saved.";
        } else {
            const Doc* d = byKey(promptKey);
            question = "Save the changes to " + (d ? d->label : promptKey) + " before closing it?";
        }
        UI::SetCursorScreenPos(UI::Vec2(bx, by));
        UI::TextUnformatted(Fit(question, 430).c_str());
        UI::SetCursorScreenPos(UI::Vec2(bx, UI::GetCursorScreenPos().y + 10));
        int answer = -1;
        if (UI::Button(promptQuit ? "Save All and Quit" : "Save")) answer = 0;
        UI::SameLine();
        if (UI::Button(promptQuit ? "Quit Without Saving" : "Don't Save")) answer = 1;
        UI::SameLine();
        if (UI::Button("Cancel##prompt") || UI::IsKeyPressed(UI::KeyId_Escape, false)) answer = 2;
        if (answer >= 0) answerPrompt(answer);
    }
    UI::End();
}

// ---- the host's scene scripts in the tree ---------------------------------------------------------------------------------------

void ScriptIdeApp::Impl::drawSceneScripts(float X, float W, bool modal) {
    if (!cfg.sceneScripts) return;
    const std::vector<SceneScript> list = cfg.sceneScripts();
    if (list.empty()) return;
    UI::SetCursorScreenPos(UI::Vec2(X + 8, UI::GetCursorScreenPos().y + 2));
    UI::TextDisabled("Scene scripts");
    const std::string activeKey = activeDoc() ? activeDoc()->key : std::string();
    std::string pick;
    for (std::size_t i = 0; i < list.size() && i < 200; ++i) {
        const SceneScript& sc = list[i];
        std::string label = sc.object.empty() ? sc.label : sc.object + ": " + sc.label;
        if (sc.path.empty()) label += "  (in the scene only)";
        UI::SetCursorScreenPos(UI::Vec2(X + 6, UI::GetCursorScreenPos().y));
        if (UI::Selectable((Fit(label, W - 24) + "##scene" + std::to_string(i)).c_str(), sc.key == activeKey) && !modal) pick = sc.key;
    }
    if (!pick.empty() && !showKey(pick, 0)) {
        DocumentSpec spec;
        if (cfg.documentFor && cfg.documentFor(pick, spec)) openDocument(spec, 0);
        else notice = "That script is not in the scene any more";
    }
    UI::SetCursorScreenPos(UI::Vec2(X + 8, UI::GetCursorScreenPos().y + 4));
}

// ---- public -------------------------------------------------------------------------------------------------------------------

void ScriptIdeApp::ShowDocuments(const std::vector<DocumentSpec>& specs) {
    int first = -1;
    for (const DocumentSpec& s : specs) {
        if (!m->openDocument(s, 0)) continue;
        if (first < 0) first = m->active;
    }
    if (first >= 0) m->active = first;
}
void ScriptIdeApp::OpenNewScript() { m->openNewScript(); }
bool ScriptIdeApp::NewScriptOpen() const { return m->newOpen; }
bool ScriptIdeApp::CreateScript(const std::string& typedName, const std::string& language, std::size_t templateIndex, bool attach,
                                std::string* path) {
    return m->createScript(typedName, language, templateIndex, attach, path);
}
void ScriptIdeApp::RequestClose(const std::string& key) {
    for (std::size_t i = 0; i < m->docs.size(); ++i) if (m->docs[i]->key == key) { m->requestClose((int)i); return; }
}
bool ScriptIdeApp::ClosePromptOpen() const { return m->promptOpen; }
void ScriptIdeApp::AnswerClosePrompt(int answer) { m->answerPrompt(answer); }
bool ScriptIdeApp::ReopenClosed() { return m->reopenClosed(); }
bool ScriptIdeApp::SetLanguage(const std::string& key, const std::string& language) {
    Impl::Doc* d = m->byKey(key);
    return d && m->setLanguage(*d, language);
}
std::string ScriptIdeApp::ActiveLanguage() const { const Impl::Doc* d = m->activeDoc(); return d ? d->language : std::string(); }

} // namespace scriptide
