// Script Editor (shared/ScriptIdeApp.hpp): the one script editor that the Script Editor program (scripteditor/main.cpp) draws over
// its window and the editor draws in a window of its own. Driven headless: a made-up project folder (an OkayScript file, a Lua
// file with a mistake, a text file that is not a script), the picture of the app over a whole window and inside a host's
// rectangle (goldens), opening the folder (the tree lists the scripts), a click on a tree row opening the file, typing and
// Ctrl+S writing the file (and a Local History copy), Find in Files across the scripts and opening a hit at its line, the
// engine's checker marking the Lua mistake, Restore of an older copy (one undo step), the settings file read back by a second
// app (the program and the editor share it), Ctrl+Shift+O with a typed folder, and AppSettings' text format with bad values.
// OKAY_SHOT_DIR=<folder> also writes pictures for a person to look at (never committed).
#include "test_framework.hpp"
#include "OkayUITestKit.hpp"
#include "ScriptIdeApp.hpp"
#include "OkayUI_SoftPainter.hpp"
#include "okay/Components/ScriptComponent.hpp"   // the pictures go through a painter, as in the program: the code font is a font of its own

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using namespace okayuitest;
namespace UI = OkayUI;
namespace fs = std::filesystem;
namespace si = scriptide;

static void WriteFile(const fs::path& p, const std::string& text) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream f(p, std::ios::binary);
    f << text;
}
static std::string ReadFile(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream s;
    s << f.rdbuf();
    return s.str();
}

// The last frame drawn through the CPU painter (OkayUI::soft::FrameDrawer, what the program's window does): unlike the plain
// software rasteriser it draws text in fonts made with AddFontTtf, such as the code's JetBrains Mono.
static Bitmap Picture(const Sim& sim, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    Bitmap out((int)sim.W, (int)sim.H);
    std::unique_ptr<okay::platform::Painter> p = UI::soft::CreateBitmapPainter((int)sim.W, (int)sim.H);
    if (!p) return out;
    p->SetClipRect(nullptr);
    p->SetBlendMode(okay::platform::BlendMode::None);
    p->SetDrawColor(r, g, b, 255);
    p->Clear();
    {
        UI::soft::FrameDrawer fd(*p);
        fd.Draw(UI::GetDrawData());
    }
    std::vector<std::uint8_t> rgba;
    int w = 0, h = 0;
    if (p->ReadPixels(rgba, w, h) && w == out.w && h == out.h) out.rgba = rgba;
    return out;
}

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

        // ---- Ctrl+Shift+O: the Open Folder box; a typed folder and Enter open it ----
        const fs::path other = dir / "Other";
        WriteFile(other / "a.lua", "local a = 1\nreturn a\n");
        sim.ctrl = true; sim.shift = true; sim.Hold(UI::KeyId_O, true); sim.Frame(ui); sim.Hold(UI::KeyId_O, false); sim.Frame(ui);
        sim.ctrl = false; sim.shift = false;
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
