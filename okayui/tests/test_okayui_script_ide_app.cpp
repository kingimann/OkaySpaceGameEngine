// Script Editor (shared/ScriptIdeApp.hpp): the one script editor that the Script Editor program (scripteditor/main.cpp) draws over
// its window and the editor draws in a window of its own. Driven headless: a made-up project folder (an OkayScript file, a Lua
// file with a mistake, a text file that is not a script), the picture of the app over a whole window and inside a host's
// rectangle (goldens), opening the folder (the tree lists the scripts), a click on a tree row opening the file, typing and
// Ctrl+S writing the file (and a Local History copy), Find in Files across the scripts and opening a hit at its line, the
// engine's checker marking the Lua mistake, Restore of an older copy (one undo step), the settings file read back by a second
// app (the program and the editor share it), Ctrl+Shift+O with a typed folder, and AppSettings' text format with bad values.
// OKAY_SHOT_DIR=<folder> also writes pictures for a person to look at (never committed).
#include "test_framework.hpp"
#include "OkayUIAppTestKit.hpp"   // WriteFile, ReadFile, Picture (the apps' tests share them)
#include "ScriptIdeApp.hpp"
#include "OkayUI_SoftPainter.hpp"
#include "okay/Components/ScriptComponent.hpp"
#include "okay/Scripting/StarterScripts.hpp"

#include <map>   // the pictures go through a painter, as in the program: the code font is a font of its own

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using namespace okayuitest;
namespace UI = OkayUI;
namespace fs = std::filesystem;
namespace si = scriptide;

static const char* kPlayer =
    "// The player: moves with the arrow keys\n"
    "var speed = 6\n"
    "var health = 100\n"
    "\n"
    "void Update() {\n"
    "    var x = Input.GetAxis(\"Horizontal\")\n"
    "    transform.Translate(x * speed * Time.deltaTime, 0, 0)\n"
    "    if (health <= 0) {\n"
    "        print(\"game over\")\n"
    "    }\n"
    "}\n";
static const char* kEnemy =
    "-- An enemy that loses health\n"
    "local Script = {}\n"
    "Script.health = 30\n"
    "\n"
    "function Script:Update(dt)\n"
    "    self.health = self.health - dt\n"
    "    if self.health < 0 then\n"
    "        print(\"enemy down\")\n"
    "end\n"   // the `if` is closed but the function is not: the Lua checker says so
    "\n"
    "return Script\n";

int main() {
    RUN_SUITE("okayui script editor app (the Script Editor program and the editor's Script Editor window)");
    Reset();
    UI::UseLauncherLook(true);   // the editor's look and the program's: the proportional UI font, the code in JetBrains Mono
    UI::Style().caretBlink = false;

    const fs::path dir = fs::temp_directory_path() / "okay_script_ide_app_test";
    std::error_code ec;
    fs::remove_all(dir, ec);
    const fs::path proj = dir / "Space Run";
    WriteFile(proj / "Assets/Scripts/Player.okay", kPlayer);
    WriteFile(proj / "Assets/Scripts/Enemy.lua", kEnemy);
    WriteFile(proj / "Assets/notes.txt", "health is not a script word here\n");
    WriteFile(proj / ".okay/hidden.okay", "var health = 1\n");   // hidden folders are not the project's scripts

    std::int64_t clock = 1764000000000;   // 2025-11-24: Local History's copies are named by this pinned time
    si::ScriptIdeApp::Config cfg;
    cfg.settingsPath = (dir / "settings" / "script_editor.json").u8string();
    cfg.idPrefix = "##test_se";
    cfg.quitItem = true;
    cfg.nowMs = [&clock] { return clock; };

    // ---- the program: the app over the whole window ----
    {
        const float W = 900, H = 560;
        Sim sim(W, H);
        si::ScriptIdeApp app(cfg);
        auto ui = [&] { app.Frame(0, 0, W, H); };
        CHECK(app.OpenPath(proj.u8string()));
        CHECK(app.ProjectScripts().size() == 2);   // Player.okay and Enemy.lua; not notes.txt, not the hidden folder
        CHECK(app.Folder().find("Space Run") != std::string::npos);
        sim.Frames(3, ui);
        CHECK(app.OpenFiles().empty());
        // the tree lists Assets > Scripts > Enemy.lua, Player.okay; a click on the Player.okay row (the last one: found from the
        // bottom up, so no folder row is clicked on the way) opens it
        {
            const float rowH = UI::GetTextLineHeight();
            bool opened = false;
            for (float y = 240; y > 60 && !opened; y -= rowH * 0.5f) {
                sim.Click(60, y, ui);
                opened = app.ActivePath().find("Player.okay") != std::string::npos;
            }
            CHECK(opened);
        }
        CHECK(app.OpenFiles().size() == 1);
        CHECK(app.ActiveText() == kPlayer);
        CHECK(!app.ActiveDirty());
        CHECK(app.ActiveProblems().empty());
        for (const auto& p : app.ActiveProblems()) std::cerr << "  problem: line " << p.line << ": " << p.message << "\n";
        CHECK(app.WindowTitle() == "Player.okay - Space Run - Script Editor");
        sim.Move(5, H - 5);
        sim.Frames(3, ui);
        {
            std::string why;
            const bool same = MatchesGolden("script_ide_app", Picture(sim, 30, 32, 37), &why);
            if (!same) std::cerr << "  golden script_ide_app: " << why << "\n";
            CHECK(same);
        }

        // ---- edit and save: the caret to the end of line 3, type, Ctrl+S ----
        UI::CodeEdit* e = app.ActiveEdit();
        CHECK(e != nullptr);
        if (e) {
            e->Focus();
            e->SetCaret(UI::CodePos{2, (int)e->Line(2).size()});
            sim.Frame(ui);
            sim.Type(" // full");
            sim.Frame(ui);
            CHECK(app.ActiveDirty());
            CHECK(app.WindowTitle().rfind("*Player.okay", 0) == 0);
            sim.ctrl = true; sim.Hold(UI::KeyId_S, true); sim.Frame(ui); sim.Hold(UI::KeyId_S, false); sim.Frame(ui); sim.ctrl = false;
            CHECK(!app.ActiveDirty());
            const std::string disk = ReadFile(proj / "Assets/Scripts/Player.okay");
            CHECK(disk.find("var health = 100 // full\n") != std::string::npos);
            CHECK(app.Notice().find("Saved") == 0);
            CHECK(app.HistoryOfActive().size() == 2);   // Local History: the file as it was opened, and the saved text
        }

        // ---- Find in Files: "health" is in both scripts (and in notes.txt, which is not a script) ----
        const std::size_t hits = app.FindInFiles("health");
        CHECK(hits == 7);   // Player.okay: 2 lines; Enemy.lua: 5 matches on 4 lines (one result per match)
        bool enemyHit = false;
        std::size_t enemyIndex = 0;
        for (std::size_t i = 0; i < app.FindResults().size(); ++i)
            if (app.FindResults()[i].file.find("Enemy.lua") != std::string::npos && app.FindResults()[i].match.line == 6) { enemyHit = true; enemyIndex = i; break; }
        CHECK(enemyHit);
        for (const auto& r : app.FindResults()) CHECK(r.file.find("notes.txt") == std::string::npos && r.file.find("hidden") == std::string::npos);
        CHECK(app.OpenResult(enemyIndex));
        sim.Frames(2, ui);
        CHECK(app.ActivePath().find("Enemy.lua") != std::string::npos);
        if (UI::CodeEdit* le = app.ActiveEdit()) CHECK(le->Caret().line == 5);   // line 6, 0-based 5
        // the engine's Lua checker found the missing `end`
        CHECK(!app.ActiveProblems().empty());
        bool error = false;
        for (const auto& p : app.ActiveProblems()) if (p.severity == 0) error = true;
        CHECK(error);
        sim.Move(5, H - 5);
        sim.Frames(2, ui);
        {
            std::string why;
            const bool same = MatchesGolden("script_ide_app_find", Picture(sim, 30, 32, 37), &why);
            if (!same) std::cerr << "  golden script_ide_app_find: " << why << "\n";
            CHECK(same);
        }

        // ---- Local History: a second save, then Restore the first copy (one undo step) ----
        CHECK(app.OpenFile((proj / "Assets/Scripts/Player.okay").u8string()));
        if (UI::CodeEdit* pe = app.ActiveEdit()) {
            clock += 60000;
            pe->SetCaret(UI::CodePos{1, 0});
            pe->InsertText("var lives = 3\n");
            CHECK(app.Save());
            const auto versions = app.HistoryOfActive();
            CHECK(versions.size() == 3);
            if (versions.size() == 3) {
                CHECK(versions[2].label == "Opened" && versions[0].label == "Saved");
                CHECK(app.RestoreVersion(versions[2]));   // the oldest: the file as it was opened
                CHECK(app.ActiveText() == kPlayer);
                CHECK(app.ActiveDirty());
                pe->Undo();
                CHECK(app.ActiveText().find("var lives = 3") != std::string::npos);
            }
        }

        // ---- a host changes the settings (the editor's shared Density): ApplySettings puts them on the open tabs at once ----
        if (UI::CodeEdit* se = app.ActiveEdit()) {
            const float was = se->LineSpacing();
            app.Settings().density = 2;
            CHECK(se->LineSpacing() == was);   // not before ApplySettings
            app.ApplySettings();
            CHECK(se->LineSpacing() > was);    // Spacious: more room between the lines than Comfortable
            app.Settings().density = 1;
            app.ApplySettings();
            CHECK(se->LineSpacing() == was);
        } else {
            CHECK(false);
        }

        // ---- Ctrl+W closes the tab; Ctrl+Q asks the program to quit ----
        const std::size_t before = app.OpenFiles().size();
        sim.ctrl = true; sim.Hold(UI::KeyId_W, true); sim.Frame(ui); sim.Hold(UI::KeyId_W, false); sim.Frame(ui); sim.ctrl = false;
        CHECK(app.OpenFiles().size() + 1 == before);
        CHECK(!app.WantsQuit());
        sim.ctrl = true; sim.Hold(UI::KeyId_Q, true); sim.Frame(ui); sim.Hold(UI::KeyId_Q, false); sim.Frame(ui); sim.ctrl = false;
        CHECK(app.WantsQuit());
        app.Settings().wordWrap = true;
    }   // the app writes its settings here

    // ---- the settings file: a second app (the editor's window, say) reads the folder and the view back ----
    {
        const std::string text = ReadFile(cfg.settingsPath);
        CHECK(text.find("\"lastFolder\": ") != std::string::npos && text.find("Space Run") != std::string::npos);
        si::ScriptIdeApp other(cfg);
        CHECK(other.Settings().wordWrap);
        CHECK(!other.Settings().recentFolders.empty() && other.Settings().recentFolders[0].find("Space Run") != std::string::npos);
    }

    // ---- the editor's window: the same app inside a host rectangle (right of a 180 px panel, under a 40 px bar) ----
    {
        Reset();
        UI::UseLauncherLook(true);
        UI::Style().caretBlink = false;
        const float X = 180, Y = 40, W = 720, H = 460;
        Sim sim(X + W, Y + H);
        si::ScriptIdeApp::Config hc = cfg;
        hc.settingsPath.clear();
        hc.quitItem = false;
        hc.idPrefix = "##test_se_hosted";
        hc.hostName = "Scene Script Editor";
        int shown = 0;
        hc.showInHost = [&shown](const std::string&, int) { ++shown; };
        std::vector<std::string> savedPaths;
        hc.saved = [&savedPaths](const std::string& p, const std::string&) { savedPaths.push_back(p); };
        hc.moreProblems = [](const std::string&, const std::string&, const std::string&, std::vector<si::Diagnostic> own) {
            si::Diagnostic d;   // what an editor plugin's lint adds
            d.line = 1; d.severity = 1; d.message = "plugin: say what the script is for"; d.source = "plugin";
            own.push_back(d);
            return own;
        };
        si::ScriptIdeApp app(hc);
        auto ui = [&] { app.Frame(X, Y, W, H); };
        CHECK(app.OpenFolder(proj.u8string()));
        CHECK(app.OpenFile((proj / "Assets/Scripts/Player.okay").u8string(), 5));
        sim.Frames(3, ui);
        bool plugin = false;
        for (const auto& p : app.ActiveProblems()) if (p.source == "plugin") plugin = true;
        CHECK(plugin);   // the host's problems are added to the checker's
        if (UI::CodeEdit* e = app.ActiveEdit()) CHECK(e->Caret().line == 4);
        sim.Move(5, 5);
        sim.Frames(2, ui);
        {
            std::string why;
            const bool same = MatchesGolden("script_ide_app_hosted", Picture(sim, 18, 20, 26), &why);
            if (!same) std::cerr << "  golden script_ide_app_hosted: " << why << "\n";
            CHECK(same);
        }
        CHECK(app.Save());
        CHECK(savedPaths.size() == 1);
        CHECK(!app.WantsQuit());   // no Quit in the editor's window
        sim.ctrl = true; sim.Hold(UI::KeyId_Q, true); sim.Frame(ui); sim.Hold(UI::KeyId_Q, false); sim.Frame(ui); sim.ctrl = false;
        CHECK(!app.WantsQuit());

        // ---- Ctrl+O: the Open Folder box; a typed folder and Enter open it ----
        const fs::path other = dir / "Other";
        WriteFile(other / "a.lua", "local a = 1\nreturn a\n");
        sim.ctrl = true; sim.Hold(UI::KeyId_O, true); sim.Frame(ui); sim.Hold(UI::KeyId_O, false); sim.Frame(ui);
        sim.ctrl = false;
        sim.Press(Key_SelectAll); sim.Frame(ui);
        sim.Type(other.u8string()); sim.Frame(ui);
        sim.Press(Key_Enter); sim.Frames(2, ui);
        CHECK(app.Folder().find("Other") != std::string::npos);
        if (app.Folder().find("Other") == std::string::npos) std::cerr << "  folder: " << app.Folder() << ", notice: " << app.Notice() << "\n";
        CHECK(app.ProjectScripts().size() == 1);
        CHECK(app.OpenFiles().size() == 1);   // the open file stays open
    }

    // ---- the editor's Script components with a fake host: a component's script as a document the host saves, completion,
    // breakpoints and a paused line with Continue / Step ----
    {
        Reset();
        UI::UseLauncherLook(true);
        UI::Style().caretBlink = false;
        const float W = 900, H = 560;
        Sim sim(W, H);
        si::ScriptIdeApp::Config hc = cfg;
        hc.settingsPath.clear();
        hc.idPrefix = "##test_se_scene";
        std::string hostCopy;                                   // what the fake editor keeps as the component's source
        std::vector<std::pair<std::string, std::vector<int>>> bpCalls;
        std::vector<std::string> debugCalls;
        hc.breakpointsChanged = [&bpCalls](const std::string& key, const std::vector<int>& lines) { bpCalls.push_back({key, lines}); };
        hc.debug = [&debugCalls](const std::string& c) { debugCalls.push_back(c); };
        si::ScriptIdeApp app(hc);
        auto ui = [&] { app.Frame(0, 0, W, H); };
        si::DocumentSpec spec;
        spec.key = "scene:ship-guid/0";
        spec.label = "Enemy.lua (Ship)";
        spec.language = "lua";
        spec.text = kEnemy;
        spec.save = [&hostCopy](const std::string& text, std::string& why) {
            if (text.find("FORBIDDEN") != std::string::npos) { why = "the host said no"; return false; }
            hostCopy = text;
            return true;
        };
        CHECK(app.OpenDocument(spec));
        CHECK(app.ActiveKey() == "scene:ship-guid/0" && app.ActivePath().empty());
        CHECK(app.HasDocument("scene:ship-guid/0") && !app.DocumentDirty("scene:ship-guid/0"));
        CHECK(!app.ActiveProblems().empty());   // the Lua checker runs on a host's document too
        CHECK(app.HistoryOfActive().empty());   // no file: no Local History
        // a second OpenDocument with the same key shows the same tab and keeps its text
        si::DocumentSpec again = spec;
        again.text = "-- other\n";
        CHECK(app.OpenDocument(again) && app.OpenFiles().size() == 1 && app.DocumentText(spec.key) == kEnemy);
        sim.Frames(2, ui);

        // completion: on a new line after line 3, type "Scr": the list offers the file's word "Script"; Enter takes it
        UI::CodeEdit* e = app.ActiveEdit();
        CHECK(e != nullptr);
        if (e) {
            e->Focus();
            e->SetCaret(UI::CodePos{2, (int)e->Line(2).size()});
            e->InsertText("\n");
            sim.Frame(ui);
            sim.Type("Scr");
            sim.Frame(ui);
            CHECK(app.CompletionOpen());
            bool offered = false;
            for (const auto& it : app.CompletionList()) if (it.label == "Script") offered = true;
            CHECK(offered);
            CHECK(!app.CompletionList().empty() && app.CompletionList()[0].label == "Script");   // the best match first
            sim.Hold(UI::KeyId_Enter, true); sim.Frame(ui); sim.Hold(UI::KeyId_Enter, false); sim.Frame(ui);
            CHECK(!app.CompletionOpen());
            CHECK(e->Line(3) == "Script");
            CHECK(e->LineCount() == 13);   // Enter took the item and did not also break the line
            // Esc closes the list and leaves the text
            sim.Type(".hea"); sim.Frame(ui);
            CHECK(app.CompletionOpen());
            sim.Hold(UI::KeyId_Escape, true); sim.Frame(ui); sim.Hold(UI::KeyId_Escape, false); sim.Frame(ui);
            CHECK(!app.CompletionOpen() && e->Line(3) == "Script.hea");
            CHECK(app.ReloadDocument(spec.key, kEnemy));   // back to the text as opened (the host's copy changed)
            CHECK(!app.DocumentDirty(spec.key) && app.DocumentText(spec.key) == kEnemy);
        }
        // Ctrl+S: the host saves it (the component gets the text); a refusal leaves it changed with the host's reason
        if (e) {
            e->SetCaret(UI::CodePos{0, 0});
            e->InsertText("-- saved by the host\n");
            sim.ctrl = true; sim.Hold(UI::KeyId_S, true); sim.Frame(ui); sim.Hold(UI::KeyId_S, false); sim.Frame(ui); sim.ctrl = false;
            CHECK(hostCopy.rfind("-- saved by the host\n", 0) == 0);
            CHECK(!app.DocumentDirty(spec.key));
            e->InsertText("FORBIDDEN ");
            CHECK(!app.Save());
            CHECK(app.DocumentDirty(spec.key) && app.Notice().find("the host said no") != std::string::npos);
            e->Undo();
        }
        // breakpoints: set by the host (not reported back), F9 on the caret's line (reported with 1-based lines)
        CHECK(app.SetBreakpoints(spec.key, {4}));
        CHECK(app.Breakpoints(spec.key) == std::vector<int>{4});
        sim.Frame(ui);
        CHECK(bpCalls.empty());
        if (e) {
            e->SetCaret(UI::CodePos{6, 0});
            sim.Hold(UI::KeyId_F9, true); sim.Frame(ui); sim.Hold(UI::KeyId_F9, false); sim.Frame(ui);
            CHECK(app.Breakpoints(spec.key) == (std::vector<int>{4, 7}));
            CHECK(bpCalls.size() == 1 && bpCalls[0].first == spec.key && bpCalls[0].second == (std::vector<int>{4, 7}));
        }
        // a paused script: the line is shown and marked; F8 continues and F10 steps through the host's debugger
        CHECK(!app.SetExecutionLine("scene:nope", 3));
        CHECK(app.SetExecutionLine(spec.key, 7));
        sim.Frames(2, ui);
        if (e) CHECK(e->Caret().line == 6);
        bool marked = false;
        if (e) for (const auto& mk : e->Markers()) if (mk.line == 6 && mk.message.find("Paused") != std::string::npos) marked = true;
        CHECK(marked);
        sim.Hold(UI::KeyId_F8, true); sim.Frame(ui); sim.Hold(UI::KeyId_F8, false); sim.Frame(ui);
        sim.Hold(UI::KeyId_F10, true); sim.Frame(ui); sim.Hold(UI::KeyId_F10, false); sim.Frame(ui);
        CHECK(debugCalls == (std::vector<std::string>{"continue", "step"}));
        // the picture: the component's tab, two breakpoints, the paused line, the completion list open after "Scr"
        CHECK(app.ReloadDocument(spec.key, kEnemy));
        CHECK(app.SetExecutionLine(spec.key, 6));
        sim.Frames(2, ui);   // the paused line is shown (the caret goes there)
        if (e) {
            e->Focus();
            e->SetCaret(UI::CodePos{2, 3});   // after "Scr" of "Script.health"
            sim.Frame(ui);
            sim.ctrl = true; sim.Hold(UI::KeyId_Space, true); sim.Frame(ui); sim.Hold(UI::KeyId_Space, false); sim.Frame(ui); sim.ctrl = false;
            CHECK(app.CompletionOpen());   // Ctrl+Space asks at once
        }
        sim.Move(5, H - 5);
        sim.Frame(ui);
        {
            std::string why;
            const bool same = MatchesGolden("script_ide_app_scene", Picture(sim, 30, 32, 37), &why);
            if (!same) std::cerr << "  golden script_ide_app_scene: " << why << "\n";
            CHECK(same);
        }
        app.SetExecutionLine(spec.key, 0);
        bool still = false;
        if (e) for (const auto& mk : e->Markers()) if (mk.message.find("Paused") != std::string::npos) still = true;
        CHECK(!still);
        app.Close(spec.key);
        CHECK(!app.HasDocument(spec.key));
    }

    // ---- a host's completion list replaces the default one ----
    {
        si::ScriptIdeApp::Config hc = cfg;
        hc.settingsPath.clear();
        hc.complete = [](const std::string& language, const std::string&, int line, int, const std::string& prefix) {
            return std::vector<si::CompletionItem>{{"GetComponent", "GetComponent(\"\")", language + " line " + std::to_string(line) + " " + prefix}};
        };
        Reset();
        UI::UseLauncherLook(true);
        Sim sim(700, 400);
        si::ScriptIdeApp app(hc);
        auto ui = [&] { app.Frame(0, 0, 700, 400); };
        si::DocumentSpec spec;
        spec.key = "k"; spec.label = "a"; spec.language = "okayscript"; spec.text = "\n";
        spec.save = [](const std::string&, std::string&) { return true; };
        CHECK(app.OpenDocument(spec));
        sim.Frame(ui);
        if (UI::CodeEdit* e = app.ActiveEdit()) {
            e->Focus();
            sim.Frame(ui);
            sim.Type("Ge"); sim.Frame(ui);
            CHECK(app.CompletionOpen() && app.CompletionList().size() == 1 && app.CompletionList()[0].detail == "okayscript line 1 Ge");
            sim.Hold(UI::KeyId_Tab, true); sim.Frame(ui); sim.Hold(UI::KeyId_Tab, false); sim.Frame(ui);
            CHECK(e->Line(0) == "GetComponent(\"\")");
        }
        // the default list from the words: keywords, known names and the text's words; fuzzy, best first, the word itself left out
        scripteditor::LanguageWords w;
        w.keywords = {"local", "function"}; w.known = {"print", "pairs"}; w.docs["print"] = "print(...)\nwrites a line";
        const auto items = si::DefaultCompletions(w, "local position = 1 pos", "pos");
        CHECK(!items.empty() && items[0].label == "position");
        bool printOffered = false, selfOffered = false;
        for (const auto& it : si::DefaultCompletions(w, "", "pr")) { if (it.label == "print") printOffered = it.detail == "print(...)"; if (it.label == "pr") selfOffered = true; }
        CHECK(printOffered && !selfOffered);
        CHECK(si::DefaultCompletions(w, "", "zzz").empty());
    }

    // ---- batch A: the host's scene scripts, a selected object's scripts, the language, "Save changes?", reopening, New Script ----
    {
        Reset();
        UI::UseLauncherLook(true);
        UI::Style().caretBlink = false;
        const fs::path bproj = dir / "Batch A";
        WriteFile(bproj / "Assets/Scripts/Door.lua", "local Door = {}\nreturn Door\n");
        const float W = 900, H = 560;
        Sim sim(W, H);
        si::ScriptIdeApp::Config hc = cfg;
        hc.settingsPath.clear();
        hc.quitItem = true;
        hc.idPrefix = "##test_se_batch_a";
        // the fake editor: two objects with scripts, one of them only in the scene (no file)
        std::map<std::string, std::string> source = {{"scene:ship/0", kEnemy}, {"scene:ship/1", okay::StarterScriptSource("okayscript")},
                                                     {"scene:door/0", "local Door = {}\nreturn Door\n"}};
        std::map<std::string, std::string> langOf = {{"scene:ship/0", "lua"}, {"scene:ship/1", "okayscript"}, {"scene:door/0", "lua"}};
        std::vector<std::string> attached, langCalls;
        auto specFor = [&](const std::string& key) {
            si::DocumentSpec sp;
            sp.key = key;
            sp.language = langOf[key];
            sp.text = source[key];
            sp.label = key == "scene:ship/0" ? "Enemy.lua" : key == "scene:ship/1" ? "(no file)" : "Door.lua";
            if (key == "scene:door/0") sp.path = (bproj / "Assets/Scripts/Door.lua").u8string();
            sp.save = [&source, key](const std::string& t, std::string&) { source[key] = t; return true; };
            return sp;
        };
        hc.sceneScripts = [] {
            return std::vector<si::SceneScript>{{"scene:ship/0", "Enemy.lua", "Ship", ""}, {"scene:ship/1", "(no file)", "Ship", ""},
                                                {"scene:door/0", "Door.lua", "Door", "Assets/Scripts/Door.lua"}};
        };
        hc.documentFor = [&](const std::string& key, si::DocumentSpec& out) {
            if (!source.count(key)) return false;
            out = specFor(key);
            return true;
        };
        hc.languageChanged = [&langCalls](const std::string& key, const std::string& l) { langCalls.push_back(key + "=" + l); };
        hc.attachTarget = [] { return std::string("Ship"); };
        hc.attachScript = [&](const std::string& path, const std::string& language, std::string& key) {
            attached.push_back(path);
            key = "scene:ship/2";
            source[key] = "-- attached\n";
            langOf[key] = language;
            return true;
        };
        si::ScriptIdeApp app(hc);
        auto ui = [&] { app.Frame(0, 0, W, H); };
        CHECK(app.OpenFolder(bproj.u8string()));
        // selecting the ship: both of its scripts open, the first shown
        app.ShowDocuments({specFor("scene:ship/0"), specFor("scene:ship/1")});
        CHECK(app.OpenFiles().size() == 2 && app.ActiveKey() == "scene:ship/0");
        {   // what the editor's save prompts, Save Project and recovery read: the documents, changed or not, and saving one
            const auto docs = app.Documents();
            CHECK(docs.size() == 2 && docs[0].key == "scene:ship/0" && !docs[0].dirty && docs[1].language == "okayscript");
            if (UI::CodeEdit* e0 = app.ActiveEdit()) e0->InsertText("-- x\n");
            CHECK(app.Documents()[0].dirty && app.Documents()[0].version != docs[0].version);
            CHECK(app.SaveDocument("scene:ship/0") && !app.Documents()[0].dirty && source["scene:ship/0"].rfind("-- x\n", 0) == 0);
            CHECK(!app.SaveDocument("nope"));
            CHECK(app.ReloadDocument("scene:ship/0", kEnemy));
            source["scene:ship/0"] = kEnemy;
        }
        sim.Frames(2, ui);
        // the tree's "Scene scripts" section: a click on the door's row opens it (found from the top down)
        {
            bool opened = false;
            for (float y = 40; y < 300 && !opened; y += UI::GetTextLineHeight() * 0.5f) {
                sim.Click(60, y, ui);
                opened = app.ActiveKey() == "scene:door/0";
            }
            CHECK(opened);
        }
        // the language: the scene-only OkayScript starter becomes the JavaScript starter, the host is told; Undo brings it back
        CHECK(app.SetLanguage("scene:ship/1", "javascript"));
        CHECK(app.DocumentText("scene:ship/1") == okay::StarterScriptSource("javascript"));
        CHECK(langCalls == std::vector<std::string>{"scene:ship/1=javascript"});
        CHECK(!app.SetLanguage("scene:ship/1", "javascript"));   // the same language: nothing to do
        CHECK(app.SetLanguage("scene:ship/0", "okayscript"));   // written code stays as it is
        CHECK(app.DocumentText("scene:ship/0") == kEnemy);
        CHECK(app.SetLanguage("scene:ship/0", "lua"));
        // "Save changes?": Ctrl+W on a changed tab asks; Cancel keeps it; Save saves through the host and closes it
        app.RequestClose("scene:door/0");   // not changed: closed at once
        CHECK(!app.HasDocument("scene:door/0") && !app.ClosePromptOpen());
        app.ShowDocuments({specFor("scene:ship/0")});
        if (UI::CodeEdit* e = app.ActiveEdit()) {
            e->SetCaret(UI::CodePos{0, 0});
            e->InsertText("-- edited\n");
        }
        sim.ctrl = true; sim.Hold(UI::KeyId_W, true); sim.Frame(ui); sim.Hold(UI::KeyId_W, false); sim.Frame(ui); sim.ctrl = false;
        CHECK(app.ClosePromptOpen() && app.HasDocument("scene:ship/0"));
        sim.Move(5, H - 5);
        sim.Frame(ui);
        {
            std::string why;
            const bool same = MatchesGolden("script_ide_app_close_prompt", Picture(sim, 30, 32, 37), &why);
            if (!same) std::cerr << "  golden script_ide_app_close_prompt: " << why << "\n";
            CHECK(same);
        }
        sim.Hold(UI::KeyId_Escape, true); sim.Frame(ui); sim.Hold(UI::KeyId_Escape, false); sim.Frame(ui);   // Cancel
        CHECK(!app.ClosePromptOpen() && app.HasDocument("scene:ship/0"));
        app.RequestClose("scene:ship/0");
        CHECK(app.ClosePromptOpen());
        app.AnswerClosePrompt(0);   // Save
        CHECK(!app.HasDocument("scene:ship/0") && source["scene:ship/0"].rfind("-- edited\n", 0) == 0);
        // Ctrl+Shift+T reopens it from the host (the saved text), then the door file
        sim.ctrl = true; sim.shift = true; sim.Hold(UI::KeyId_T, true); sim.Frame(ui); sim.Hold(UI::KeyId_T, false); sim.Frame(ui);
        sim.ctrl = false; sim.shift = false;
        CHECK(app.ActiveKey() == "scene:ship/0" && app.DocumentText("scene:ship/0").rfind("-- edited\n", 0) == 0);
        CHECK(app.ReopenClosed() && app.ActiveKey() == "scene:door/0");
        CHECK(!app.ReopenClosed());   // nothing more
        // Don't Save: closed, the host's copy unchanged
        if (UI::CodeEdit* e = app.ActiveEdit()) e->InsertText("x");
        app.RequestClose("scene:door/0");
        app.AnswerClosePrompt(1);
        CHECK(!app.HasDocument("scene:door/0") && source["scene:door/0"] == "local Door = {}\nreturn Door\n");
        // Quit with a changed tab asks first; Quit Without Saving quits
        if (UI::CodeEdit* e = app.ActiveEdit()) e->InsertText("y");
        sim.ctrl = true; sim.Hold(UI::KeyId_Q, true); sim.Frame(ui); sim.Hold(UI::KeyId_Q, false); sim.Frame(ui); sim.ctrl = false;
        CHECK(app.ClosePromptOpen() && !app.WantsQuit());
        app.AnswerClosePrompt(2);
        CHECK(!app.WantsQuit());
        app.AnswerClosePrompt(1);   // the box is closed: nothing happens
        CHECK(!app.WantsQuit());

        // New Script: the box, a typed name, the template list, Enter creates Assets/Scripts/MoverBot.okay and opens it
        app.OpenNewScript();
        CHECK(app.NewScriptOpen());
        sim.Frame(ui);
        sim.Press(Key_SelectAll); sim.Frame(ui);
        sim.Type("Mover Bot"); sim.Frame(ui);
        sim.Move(5, H - 5);
        sim.Frame(ui);
        {
            std::string why;
            const bool same = MatchesGolden("script_ide_app_new_script", Picture(sim, 30, 32, 37), &why);
            if (!same) std::cerr << "  golden script_ide_app_new_script: " << why << "\n";
            CHECK(same);
        }
        sim.Press(Key_Enter); sim.Frames(2, ui);
        CHECK(!app.NewScriptOpen());
        const fs::path mover = bproj / "Assets/Scripts/MoverBot.okay";
        CHECK(fs::exists(mover));
        CHECK(app.ActivePath().find("MoverBot.okay") != std::string::npos);
        CHECK(ReadFile(mover) == si::StarterScriptNamed("okayscript", "MoverBot"));
        bool listed = false;
        for (const auto& p : app.ProjectScripts()) if (p.find("MoverBot.okay") != std::string::npos) listed = true;
        CHECK(listed);
        // the same name again opens it, it is not written over
        WriteFile(mover, "// mine\n");
        CHECK(app.CreateScript("MoverBot", "okayscript", 0, false));
        CHECK(ReadFile(mover) == "// mine\n");
        // attach: the host attaches the new Lua file to the selected object and the component's document opens
        std::string made;
        CHECK(app.CreateScript("2 fast", "lua", 0, true, &made));
        CHECK(made.find("Assets/Scripts/Script2fast.lua") != std::string::npos);
        CHECK(attached.size() == 1 && attached[0] == made && app.ActiveKey() == "scene:ship/2");
        // no folder open: New Script says so and makes nothing
        si::ScriptIdeApp none(hc);
        CHECK(!none.CreateScript("X", "lua", 0, false) && none.Notice().find("Open a folder") == 0);
    }

    // ---- batch B: the find bar and Replace, Replace in All Files, Go to Line / Symbol / Definition (F12 and Ctrl+click), the
    // references, Back / Forward, the tab switcher, bookmarks, Search Everywhere and the command list with a host's command ----
    {
        Reset();
        UI::UseLauncherLook(true);
        UI::Style().caretBlink = false;
        const fs::path nproj = dir / "Batch B";
        static const char* kUtil = "local Util = {}\nfunction Util.Clamp(x, a, b)\n    if x < a then return a end\n    return x\nend\nreturn Util\n";
        static const char* kPlay = "local Util = require(\"Util\")\nlocal speed = 3\nfunction Update(dt)\n    speed = Util.Clamp(speed + dt, 0, 10)\n    print(speed)\nend\nreturn {}\n";
        WriteFile(nproj / "Assets/Scripts/Util.lua", kUtil);
        WriteFile(nproj / "Assets/Scripts/Player.lua", kPlay);
        WriteFile(nproj / "Assets/Scripts/Hud.lua", "local Util = require(\"Util\")\nreturn Util\n");
        const float W = 900, H = 560;
        Sim sim(W, H);
        si::ScriptIdeApp::Config hc = cfg;
        hc.settingsPath.clear();
        hc.idPrefix = "##test_se_batch_b";
        int hello = 0;
        hc.commands = [&hello] { return std::vector<si::AppCommand>{{"Plugin: Say Hello", "", [&hello] { ++hello; }}}; };
        si::ScriptIdeApp app(hc);
        auto ui = [&] { app.Frame(0, 0, W, H); };
        auto chord = [&](UI::KeyId k, bool ctrl, bool shift = false, bool alt = false) {
            sim.ctrl = ctrl; sim.shift = shift; if (alt) sim.Hold(UI::KeyId_Alt, true);
            sim.Hold(k, true); sim.Frame(ui); sim.Hold(k, false); sim.Frame(ui);
            sim.ctrl = false; sim.shift = false; if (alt) sim.Hold(UI::KeyId_Alt, false);
            sim.Frame(ui);
        };
        CHECK(app.OpenFolder(nproj.u8string()));
        const std::string util = (nproj / "Assets/Scripts/Util.lua").u8string(), play = (nproj / "Assets/Scripts/Player.lua").u8string();
        CHECK(app.OpenFile(util));
        const std::string utilKey = app.ActiveKey();
        CHECK(app.OpenFile(play));
        const std::string playKey = app.ActiveKey();
        sim.Frames(2, ui);
        UI::CodeEdit* e = app.ActiveEdit();
        CHECK(e != nullptr);
        if (!e) { TEST_MAIN_RESULT(); }
        e->Focus();
        e->SetCaret(UI::CodePos{0, 0});

        // the find bar: Ctrl+F, typed text, Enter finds the first, F3 the next, Shift+F3 the one before, round the end
        chord(UI::KeyId_F, true);
        CHECK(app.FindBarOpen());
        sim.Type("speed"); sim.Frame(ui);
        sim.Press(Key_Enter); sim.Frames(2, ui);
        CHECK(app.FindCount() == 4 && app.FindCurrent() == 0);
        if (app.FindCount() != 4 || app.FindCurrent() != 0)
            std::cerr << "  find bar: count " << app.FindCount() << ", current " << app.FindCurrent() << ", text:\n" << app.ActiveText() << "\n";
        chord(UI::KeyId_F3, false);
        CHECK(app.FindCurrent() == 1);
        chord(UI::KeyId_F3, false, true);
        CHECK(app.FindCurrent() == 0);
        CHECK(app.FindNext(false) && app.FindCurrent() == 3);   // before the first: the last
        sim.Move(5, H - 5);
        sim.Frame(ui);
        // Replace: the selected match, then all of them in one undo step; a regex with $1
        app.OpenFindBar("speed", true, si::FindOptions(), "pace");
        CHECK(app.FindNext() && app.ReplaceNext());
        CHECK(app.FindCount() == 3);
        CHECK(app.ReplaceAllInDocument() == 3 && app.FindCount() == 0 && app.ActiveText().find("speed") == std::string::npos);
        e->Undo();
        CHECK(app.FindCount() == 3);
        e->Undo();
        CHECK(app.ActiveText() == kPlay);
        si::FindOptions rx; rx.regex = true;
        app.OpenFindBar("(\\w+)\\.Clamp", true, rx, "$1.Limit");
        CHECK(app.ReplaceAllInDocument() == 1 && app.ActiveText().find("Util.Limit(") != std::string::npos);
        e->Undo();
        CHECK(app.ActiveText() == kPlay);
        app.OpenFindBar("speed");
        e->SetCaret(UI::CodePos{0, 0});
        CHECK(app.FindNext() && app.FindCurrent() == 0);
        sim.Frames(2, ui);
        {
            std::string why;
            const bool same = MatchesGolden("script_ide_app_find_bar", Picture(sim, 30, 32, 37), &why);
            if (!same) std::cerr << "  golden script_ide_app_find_bar: " << why << "\n";
            CHECK(same);
        }
        chord(UI::KeyId_Escape, false);
        CHECK(!app.FindBarOpen());

        // Go to Line (Ctrl+G, "5", Enter), then Back returns to where the caret was
        e->SetCaret(UI::CodePos{1, 0});
        chord(UI::KeyId_G, true);
        CHECK(app.PickerOpen() == 1);
        sim.Type("5"); sim.Frame(ui);
        sim.Press(Key_Enter); sim.Frames(2, ui);
        CHECK(app.PickerOpen() == 0 && e->Caret().line == 4);
        chord(UI::KeyId_Left, false, false, true);   // Alt+Left
        CHECK(app.ActiveKey() == playKey && e->Caret().line == 1);
        // Go to Symbol (Ctrl+Shift+O): the file's functions and tables
        chord(UI::KeyId_O, true, true);
        CHECK(app.PickerOpen() == 2);
        {
            const auto rows = app.PickerRows();
            bool update = false;
            for (const auto& r : rows) if (r.find("Update") != std::string::npos) update = true;
            CHECK(update);
        }
        app.OpenPicker(2, "upd");
        CHECK(app.PickRow(0));
        sim.Frame(ui);
        CHECK(e->Caret().line == 2);

        // Go to Definition: F12 on "Clamp" in Player.lua goes to Util.lua line 2; Back and Forward
        e->SetCaret(UI::CodePos{3, 18});
        chord(UI::KeyId_F12, false);
        CHECK(app.ActiveKey() == utilKey);
        if (UI::CodeEdit* ue = app.ActiveEdit()) CHECK(ue->Caret().line == 1);
        CHECK(app.GoBack());
        sim.Frame(ui);
        CHECK(app.ActiveKey() == playKey && e->Caret().line == 3);
        CHECK(app.GoForward() && app.ActiveKey() == utilKey);
        CHECK(app.GoBack());
        sim.Frame(ui);
        CHECK(!app.GoToDefinition("NoSuchName") && app.Notice().find("No definition") == 0);
        // Ctrl+click on "Clamp" does the same
        sim.Frame(ui);
        {
            const UI::Vec2 at = e->PosToScreen(UI::CodePos{3, 19});
            sim.ctrl = true;
            sim.Click(at.x + 2, at.y + e->LineHeight() * 0.5f, ui);
            sim.ctrl = false;
            sim.Frame(ui);
            CHECK(app.ActiveKey() == utilKey);
            CHECK(app.GoBack() && app.ActiveKey() == playKey);
            sim.Frame(ui);
        }

        // Find All References (Shift+F12 on "Clamp"): the definition and the use; a click on a row goes there
        e->SetCaret(UI::CodePos{3, 18});
        chord(UI::KeyId_F12, false, true);
        CHECK(app.References().size() == 2);
        sim.Move(5, H - 5);
        sim.Frames(2, ui);
        {
            std::string why;
            const bool same = MatchesGolden("script_ide_app_references", Picture(sim, 30, 32, 37), &why);
            if (!same) std::cerr << "  golden script_ide_app_references: " << why << "\n";
            CHECK(same);
        }
        CHECK(app.FindReferences("Util") == 7);   // in code only (not in "Util" strings); Hud.lua is not open: read from disk

        // the switcher (Ctrl+E): the tab shown before is picked first
        chord(UI::KeyId_E, true);
        CHECK(app.PickerOpen() == 3);
        sim.Press(Key_Enter); sim.Frames(2, ui);
        CHECK(app.PickerOpen() == 0 && app.ActiveKey() == utilKey);
        CHECK(app.OpenFile(play));
        sim.Frame(ui);
        // bookmarks: F11 marks the caret's line, Shift+F11 lists them, a pick goes there
        e->SetCaret(UI::CodePos{5, 0});
        chord(UI::KeyId_F11, false);
        CHECK(app.Breakpoints(playKey) == std::vector<int>{6});
        e->SetCaret(UI::CodePos{0, 0});
        chord(UI::KeyId_F11, false, true);
        CHECK(app.PickerOpen() == 4 && app.PickerRows().size() == 1 && app.PickerRows()[0].find("Player.lua:6") == 0);
        CHECK(app.PickRow(0));
        sim.Frame(ui);
        CHECK(e->Caret().line == 5);

        // Search Everywhere (Shift twice): files, symbols in every script, commands
        sim.Frames(2, ui);
        sim.shift = true; sim.Frame(ui); sim.shift = false; sim.Frame(ui);
        sim.shift = true; sim.Frame(ui); sim.shift = false; sim.Frame(ui);
        CHECK(app.PickerOpen() == 5);
        sim.Type("clamp"); sim.Frames(2, ui);
        {
            std::string why;
            const bool same = MatchesGolden("script_ide_app_everywhere", Picture(sim, 30, 32, 37), &why);
            if (!same) std::cerr << "  golden script_ide_app_everywhere: " << why << "\n";
            CHECK(same);
        }
        {
            const auto rows = app.PickerRows();
            CHECK(!rows.empty() && rows[0] == "Util.Clamp");
        }
        chord(UI::KeyId_Escape, false);
        CHECK(app.PickerOpen() == 0);
        app.OpenPicker(5, "hud");
        CHECK(!app.PickerRows().empty() && app.PickerRows()[0].find("Hud.lua") != std::string::npos);
        CHECK(app.PickRow(0) && app.ActivePath().find("Hud.lua") != std::string::npos);
        // the command list (Ctrl+Shift+P) with the host's command
        chord(UI::KeyId_P, true, true);
        CHECK(app.PickerOpen() == 6);
        sim.Type("hello"); sim.Frame(ui);
        CHECK(app.PickerRows().size() == 1);
        sim.Press(Key_Enter); sim.Frames(2, ui);
        CHECK(hello == 1);
        bool saveAll = false;
        for (const auto& c : app.Commands()) if (c.title == "Save All" && c.shortcut == "Ctrl+Shift+S") saveAll = true;
        CHECK(saveAll);

        // Replace in All Files: the open tabs change (not saved), a closed file is written
        app.Close(utilKey);
        const std::size_t n = app.ReplaceInFiles("Util", "Tools");
        CHECK(n == 9);   // plain text: the "Util" strings too
        CHECK(ReadFile(nproj / "Assets/Scripts/Util.lua").find("local Tools = {}") == 0);   // closed: written
        CHECK(app.DocumentDirty(playKey) && ReadFile(nproj / "Assets/Scripts/Player.lua") == kPlay);   // open: changed in its tab only
    }

    // ---- batch C: Format Document, Rename with a preview, quick fixes, Join Lines, Expand Selection, the completion's details
    // pane and "()" insert, signature help ----
    {
        Reset();
        UI::UseLauncherLook(true);
        UI::Style().caretBlink = false;
        const fs::path cproj = dir / "Batch C";
        static const char* kMove = "local M = {}\nfunction Move(x)\n    return x + 1\nend\nreturn M\n";
        static const char* kUse = "local n = Move(2)\nprnt(n)\nreturn {}\n";
        WriteFile(cproj / "Assets/Scripts/Mover.lua", kMove);
        WriteFile(cproj / "Assets/Scripts/User.lua", kUse);
        const float W = 900, H = 560;
        Sim sim(W, H);
        si::ScriptIdeApp::Config hc = cfg;
        hc.settingsPath.clear();
        hc.idPrefix = "##test_se_batch_c";
        hc.signature = [](const std::string&, const std::string& name, std::string& doc) {
            if (name != "Move") return std::string();
            doc = "Moves x one step on.";
            return std::string("Move(x)");
        };
        hc.complete = [](const std::string& language, const std::string& text, int, int, const std::string& prefix) {
            std::vector<si::CompletionItem> items = si::DefaultCompletions(si::DefaultWordsFor(language), text, prefix);
            for (auto& it : items)
                if (it.label == "Move") { it.insert = "Move()"; it.caretBack = 1; it.kind = 'f'; it.detail = "(x)";
                                          it.doc = "Move(x)\nMoves x one step on."; it.example = "local y = Move(1)"; }
            return items;
        };
        si::ScriptIdeApp app(hc);
        auto ui = [&] { app.Frame(0, 0, W, H); };
        auto chord = [&](UI::KeyId k, bool ctrl, bool shift = false, bool alt = false) {
            sim.ctrl = ctrl; sim.shift = shift; if (alt) sim.Hold(UI::KeyId_Alt, true);
            sim.Hold(k, true); sim.Frame(ui); sim.Hold(k, false); sim.Frame(ui);
            sim.ctrl = false; sim.shift = false; if (alt) sim.Hold(UI::KeyId_Alt, false);
            sim.Frame(ui);
        };
        CHECK(app.OpenFolder(cproj.u8string()));
        const std::string mover = (cproj / "Assets/Scripts/Mover.lua").u8string(), user = (cproj / "Assets/Scripts/User.lua").u8string();
        CHECK(app.OpenFile(user));
        const std::string userKey = app.ActiveKey();
        sim.Frames(2, ui);
        UI::CodeEdit* e = app.ActiveEdit();
        CHECK(e != nullptr);
        if (!e) { TEST_MAIN_RESULT(); }
        e->Focus();

        // quick fixes: "prnt" is not defined, the nearest engine name is "print"; Alt+Enter lists the fix, Enter applies it
        e->SetCaret(UI::CodePos{1, 1});
        sim.Frame(ui);
        {
            const auto fixes = app.FixesAtCaret();
            bool toPrint = false;
            for (const auto& f : fixes) if (f.title.find("print") != std::string::npos) toPrint = true;
            CHECK(toPrint);
        }
        chord(UI::KeyId_Enter, false, false, true);   // Alt+Enter
        CHECK(app.QuickFixesOpen());
        CHECK(e->LineCount() == 4);   // Alt+Enter did not also break the line
        sim.Press(Key_Enter); sim.Hold(UI::KeyId_Enter, true); sim.Frame(ui); sim.Hold(UI::KeyId_Enter, false); sim.Frames(2, ui);
        CHECK(!app.QuickFixesOpen() && e->Line(1) == "print(n)");
        e->Undo();
        CHECK(e->Line(1) == "prnt(n)");

        // completion with the host's items: the details pane, "Move()" with the caret inside; then signature help inside the call
        e->SetCaret(UI::CodePos{2, 0});
        e->InsertText("local k = \n");
        e->SetCaret(UI::CodePos{2, 10});
        sim.Frame(ui);
        sim.Type("Mo"); sim.Frame(ui);
        CHECK(app.CompletionOpen() && !app.CompletionList().empty() && app.CompletionList()[0].label == "Move");
        sim.Move(5, H - 5);
        sim.Frame(ui);
        {
            std::string why;
            const bool same = MatchesGolden("script_ide_app_completion_details", Picture(sim, 30, 32, 37), &why);
            if (!same) std::cerr << "  golden script_ide_app_completion_details: " << why << "\n";
            CHECK(same);
        }
        sim.Hold(UI::KeyId_Tab, true); sim.Frame(ui); sim.Hold(UI::KeyId_Tab, false); sim.Frame(ui);
        CHECK(e->Line(2) == "local k = Move()" && e->Caret().col == 15);
        sim.Frame(ui);
        CHECK(app.SignatureShown() == "Move(x)");
        e->SetCaret(UI::CodePos{0, 0});
        sim.Frame(ui);
        CHECK(app.SignatureShown().empty());
        CHECK(app.ReloadDocument(userKey, kUse));

        // Join Lines (Ctrl+Shift+J): the next line's indent becomes one space
        CHECK(app.ReloadDocument(userKey, "local s = 1 +\n      2\nreturn s\n"));
        e->SetCaret(UI::CodePos{0, 3});
        chord(UI::KeyId_J, true, true);
        CHECK(app.ActiveText() == "local s = 1 + 2\nreturn s\n");
        // Expand Selection (Alt+Shift+Up): the word, the line, everything; Ctrl+W still closes tabs (tested above)
        e->SetCaret(UI::CodePos{0, 6});
        chord(UI::KeyId_Up, false, true, true);
        CHECK(e->SelectedText() == "s");
        chord(UI::KeyId_Up, false, true, true);
        CHECK(e->SelectedText() == "local s = 1 + 2");
        chord(UI::KeyId_Up, false, true, true);
        CHECK(e->SelectedText() == app.ActiveText());
        CHECK(e->LineCount() == 3);   // Alt+Shift+Up moved no lines

        // Format Document (Ctrl+Alt+L): OkayScript by braces, Lua by its blocks; one undo step; a host's formatter first
        si::DocumentSpec os;
        os.key = "fmt"; os.label = "fmt.okay"; os.language = "okayscript";
        os.text = "void Update() {\nif (a) {\nb = 1\n}\n    }\n";
        os.save = [](const std::string&, std::string&) { return true; };
        CHECK(app.OpenDocument(os));
        sim.Frame(ui);
        chord(UI::KeyId_L, true, false, true);
        CHECK(app.ActiveText() == "void Update() {\n    if (a) {\n        b = 1\n    }\n}\n");
        CHECK(!app.FormatDocument());   // already formatted
        if (UI::CodeEdit* fe = app.ActiveEdit()) { fe->Undo(); CHECK(app.ActiveText() == os.text); }
        CHECK(app.ReloadDocument(userKey, "local t = {}\nfunction t.f()\nreturn 1\nend\nreturn t\n"));
        CHECK(app.OpenFile(user));
        CHECK(app.FormatDocument() && app.ActiveText().find("\n    return 1\n") != std::string::npos);
        CHECK(app.ReloadDocument(userKey, kUse));
        CHECK(si::ReindentByBraces("a {\n// }\n\"{\"\n}\n") == "a {\n    // }\n    \"{\"\n}\n");
        CHECK(si::JoinLines("f(\n  x\n)\n", 1, 2) == "f(x)\n");

        // Rename (F2 on Move): every script, a preview of each line, then Apply; open tabs change, closed files are written
        e->SetCaret(UI::CodePos{0, 11});
        chord(UI::KeyId_F2, false);
        CHECK(app.RenameOpen());
        sim.Press(Key_SelectAll); sim.Frame(ui);
        sim.Type("Walk"); sim.Frames(2, ui);
        sim.Move(5, H - 5);
        sim.Frame(ui);
        {
            std::string why;
            const bool same = MatchesGolden("script_ide_app_rename", Picture(sim, 30, 32, 37), &why);
            if (!same) std::cerr << "  golden script_ide_app_rename: " << why << "\n";
            CHECK(same);
        }
        {
            const si::RenamePlan plan = app.PlanRenameOf("Move", "Walk", true);
            CHECK(plan.error.empty() && plan.count == 2 && plan.changes.size() == 2);
            CHECK(app.PlanRenameOf("Move", "Walk", false).count == 1);   // this file only
            CHECK(!app.PlanRenameOf("Move", "local", true).error.empty());   // a keyword is refused
        }
        sim.Press(Key_Enter); sim.Frames(2, ui);
        CHECK(!app.RenameOpen());
        CHECK(app.ActiveText().find("Walk(2)") != std::string::npos && app.DocumentDirty(userKey));
        CHECK(ReadFile(cproj / "Assets/Scripts/Mover.lua").find("function Walk(x)") != std::string::npos);
        (void)mover;
    }

    // ---- batch D: Insert (behaviour templates, API commands, C# snippets), Help (C# quick reference), Reload from Disk, outside
    // changes loaded or asked about, the changed-line bars, the pinned header, the breadcrumb, the view settings ----
    {
        Reset();
        UI::UseLauncherLook(true);
        UI::Style().caretBlink = false;
        const fs::path dproj = dir / "Batch D";
        std::string big = "local Big = {}\n\nfunction Big.Run(dt)\n";
        for (int i = 0; i < 60; ++i) big += "    Big.n = dt * " + std::to_string(i) + "\n";
        big += "    pirnt(dt)\nend\n\nreturn Big\n";
        WriteFile(dproj / "Assets/Scripts/Big.lua", big);
        const float W = 900, H = 560;
        Sim sim(W, H);
        si::ScriptIdeApp::Config hc = cfg;
        hc.settingsPath = (dir / "settings_d.json").u8string();
        hc.idPrefix = "##test_se_batch_d";
        si::ScriptIdeApp app(hc);
        auto ui = [&] { app.Frame(0, 0, W, H); };
        CHECK(app.OpenFolder(dproj.u8string()));
        const fs::path bigPath = dproj / "Assets/Scripts/Big.lua";
        CHECK(app.OpenFile(bigPath.u8string()));
        const std::string bigKey = app.ActiveKey();
        sim.Frames(2, ui);
        UI::CodeEdit* e = app.ActiveEdit();
        CHECK(e != nullptr);
        if (!e) { TEST_MAIN_RESULT(); }

        // Insert > API Command: Lua's bound names with their docs; a pick puts the call at the caret
        e->SetCaret(UI::CodePos{1, 0});
        app.OpenPicker(8, "Debug.Log");
        CHECK(!app.PickerRows().empty());
        CHECK(app.PickRow(0) && e->Line(1).find("Debug.Log(") == 0);
        e->Undo();
        // Insert > Behaviour Template for an OkayScript document; the C# snippets and the quick reference
        si::DocumentSpec os;
        os.key = "tpl"; os.label = "tpl.okay"; os.language = "okayscript"; os.text = "\n";
        os.save = [](const std::string&, std::string&) { return true; };
        CHECK(app.OpenDocument(os));
        app.OpenPicker(7, "pause toggle");
        CHECK(!app.PickerRows().empty() && app.PickerRows()[0].find("Pause toggle") != std::string::npos);
        CHECK(app.PickRow(0) && app.ActiveText().find("key_down(\"p\")") != std::string::npos);
        int cs = 0;
        for (const auto& sn : si::BuiltinSnippets()) if (sn.group == "C#") ++cs;
        CHECK(cs == 12 && si::BuiltinSnippets().size() > 60 && si::CSharpQuickReference().size() == 15);
        CHECK(si::CallTemplate("move", "move(x, y)") == "move(x, y)" && si::CallTemplate("f", "f(a | b)") == "f(");
        app.ShowQuickReference();
        CHECK(app.QuickReferenceOpen());
        sim.Frame(ui);
        sim.Hold(UI::KeyId_Escape, true); sim.Frame(ui); sim.Hold(UI::KeyId_Escape, false); sim.Frame(ui);
        CHECK(!app.QuickReferenceOpen());
        app.Close("tpl");

        // the changed lines since the save, the pinned header while scrolled into Big.Run, the breadcrumb
        CHECK(app.OpenFile(bigPath.u8string()));
        sim.Frame(ui);
        CHECK(app.ChangedLines().empty());
        e->SetCaret(UI::CodePos{5, 4});
        e->InsertText("-- note ");
        CHECK(app.ChangedLines() == std::vector<int>{6});
        CHECK(app.Breadcrumb() == "Big.Run");
        e->SetScroll(0, e->LineHeight() * 20);
        sim.Frames(2, ui);
        CHECK(app.StickyHeader() == "function Big.Run(dt)");
        sim.Move(5, H - 5);
        sim.Frame(ui);
        {
            std::string why;
            const bool same = MatchesGolden("script_ide_app_view", Picture(sim, 30, 32, 37), &why);
            if (!same) std::cerr << "  golden script_ide_app_view: " << why << "\n";
            CHECK(same);
        }
        e->Undo();
        CHECK(app.ChangedLines().empty() && !app.ActiveDirty());

        // the disk: an outside change to a tab with no edits is loaded; with edits, the tab asks
        auto touch = [&](const std::string& text, int secs) {
            WriteFile(bigPath, text);
            std::error_code tec;
            fs::last_write_time(bigPath, fs::file_time_type::clock::now() + std::chrono::seconds(secs), tec);
        };
        touch("local Big = {}\nreturn Big\n", 5);
        app.CheckDiskNow();
        CHECK(app.ActiveText() == "local Big = {}\nreturn Big\n" && !app.ActiveDirty() && !app.ConflictOpen());
        e->SetCaret(UI::CodePos{0, 0});
        e->InsertText("-- mine\n");
        touch("local Big = { outside = true }\nreturn Big\n", 10);
        app.CheckDiskNow();
        CHECK(app.ConflictOpen() && app.ActiveText().find("-- mine") == 0);
        app.ResolveConflict(true);
        CHECK(!app.ConflictOpen() && app.ActiveText().find("outside = true") != std::string::npos && !app.ActiveDirty());
        // Reload from Disk (one undo step brings the tab's text back)
        e->InsertText("x");
        touch(big, 15);
        CHECK(app.ReloadFromDisk() && app.ActiveText() == big);
        e->Undo();
        CHECK(app.ActiveText() != big);

        // the view settings are kept
        app.Settings().showMinimap = false; app.Settings().density = 2; app.Settings().syntaxColors = false;
        app.SaveSettings();
        const si::AppSettings back = si::AppSettings::Parse(ReadFile(dir / "settings_d.json"));
        CHECK(!back.showMinimap && back.density == 2 && !back.syntaxColors && back.showRuler && back.liveSync);
    }

    // ---- the new-script helpers of the model ----
    CHECK(si::ScriptStem("Mover Bot!") == "MoverBot" && si::ScriptStem("Player.okay") == "Player" && si::ScriptStem("9lives") == "Script9lives" && si::ScriptStem("") == "Script");
    CHECK(si::DefaultScriptPath("/p/Game/", "Door", "lua") == "/p/Game/Assets/Scripts/Door.lua");
    CHECK(si::DefaultScriptPath("", "Door", "javascript") == "Assets/Scripts/Door.js");
    CHECK(si::ExtensionFor("csharp") == "cs" && si::ExtensionFor("okayscript") == "okay" && si::ClassIdentifier("2 a") == "_2_a");
    CHECK(si::RenameScriptClass("class Foo {}", "Bar") == "class Bar {}" && si::RenameScriptClass("var x", "Bar") == "var x");

    // ---- Run without the editor: the script in a sandbox scene (Start and three frames); the compile error when it does not load ----
    {
        const si::TestRunResult good = si::TestRunScript("lua", "local S = { speed = 2 }\nfunction S:Update(dt) self.speed = self.speed + dt end\nreturn S\n");
        CHECK(good.ok && good.message.find("Test run OK") == 0);
        const si::TestRunResult bad = si::TestRunScript("lua", kEnemy);   // the missing `end`
        CHECK(!bad.ok && bad.message.find("Compile error") == 0);
        bool prepared = false;
        si::TestRunScript("lua", "return {}\n", [&prepared](okay::ScriptComponent&) { prepared = true; });
        CHECK(prepared);
    }

    // ---- the settings text: bad values are skipped or clamped, unknown keys ignored ----
    {
        const si::AppSettings s = si::AppSettings::Parse(
            "{\"tabSize\": 999, \"wordWrap\": true, \"codeFontPx\": \"abc\", \"nope\": 1, \"recentFolders\": [\"/a\", \"/a\", 7, \"/b\"]}");
        CHECK(s.tabSize == 16);
        CHECK(s.wordWrap);
        CHECK(s.codeFontPx == 15.0f);
        CHECK(s.recentFolders.size() == 2);
        si::AppSettings t = s;
        t.NoteFolder("/b");
        CHECK(t.recentFolders[0] == "/b" && t.recentFolders.size() == 2 && t.lastFolder == "/b");
        CHECK(si::AppSettings::Parse(t.Serialize()).Serialize() == t.Serialize());
        t.lastFolder = "C:\\Games\\Say \"hi\"";   // a Windows path with quotes survives the round trip
        CHECK(si::AppSettings::Parse(t.Serialize()).lastFolder == t.lastFolder);
        CHECK(si::AppSettings::Parse("tabSize=8\n").tabSize == 4);   // not JSON: the defaults
        CHECK(si::AppSettings::Parse("{\"tabSize\": -3, \"codeFontPx\": 2.5}").tabSize == 4);
    }

    // ---- OKAY_SHOT_DIR: pictures for a person to look at (software render; never committed) ----
    if (const char* shots = std::getenv("OKAY_SHOT_DIR"); shots && *shots) {
        Reset();
        UI::UseLauncherLook(true);
        UI::Style().caretBlink = false;
        fs::create_directories(fs::u8path(shots), ec);
        const float SW = 1200, SH = 720;
        Sim ss(SW, SH);
        si::ScriptIdeApp::Config sc = cfg;
        sc.settingsPath.clear();
        si::ScriptIdeApp app(sc);
        auto ui = [&] { app.Frame(0, 0, SW, SH); };
        app.OpenFolder(proj.u8string());
        app.OpenFile((proj / "Assets/Scripts/Player.okay").u8string());
        app.OpenFile((proj / "Assets/Scripts/Enemy.lua").u8string(), 6);
        ss.Frames(3, ui);
        auto shot = [&](const std::string& name) {
            const std::string path = (fs::u8path(shots) / ("script_editor_" + name + ".png")).u8string();
            WritePng(path, Picture(ss, 30, 32, 37));
            std::cout << "  picture " << path << "\n";
        };
        shot("1_program");
        app.FindInFiles("health");
        ss.Frames(2, ui);
        shot("2_find");
    }

    fs::remove_all(dir, ec);
    UI::UseLauncherLook(false);
    TEST_MAIN_RESULT();
}
