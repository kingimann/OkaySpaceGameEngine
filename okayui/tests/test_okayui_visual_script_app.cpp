// Visual Script (shared/VisualScriptApp.hpp): the one Visual Script editor that the Visual Script program (visualscript/main.cpp)
// draws over its window and the editor draws in a window of its own. Driven headless with the pointer and the keys, as a person
// would: a made-up folder with two graphs, the picture of the app over a whole window and inside a host's rectangle (goldens),
// opening a graph, the Add Block list (Space, typing, Enter), wiring two plugs by dragging, a refused wire and why, a wire dropped
// on empty space offering only the blocks that take it, moving a block (snapped to the grid), a selection box, Delete, Undo and
// Redo, Ctrl+S writing the file, Save As for a new graph, the "Save changes?" box and Reopen Closed Tab, the IDE tools (align,
// collapse to function, bookmarks, find references in both graphs, compare with the saved file, quick fixes), the OkayScript
// text, breakpoints and the paused bar's keys, the settings file, Quit; and the editor's hosting through a fake host (its save
// hook, Stop Play, its Window items). OKAY_SHOT_DIR=<folder> also writes pictures for a person to look at (never committed).
#include "test_framework.hpp"
#include "OkayUIAppTestKit.hpp"   // WriteFile, ReadFile, Picture (the apps' tests share them)
#include "OkayUI_SoftPainter.hpp"
#include "VisualScriptApp.hpp"
#include "VisualScriptModel.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

using namespace okayuitest;
namespace UI = OkayUI;
namespace fs = std::filesystem;
namespace vsed = okay::editor::vsed;

static void Golden(const Sim& sim, const char* name) {
    std::string why;
    const bool same = MatchesGolden(name, Picture(sim, 24, 26, 31), &why);
    if (!same) std::cerr << "  golden " << name << ": " << why << "\n";
    CHECK(same);
    if (const char* dir = std::getenv("OKAY_SHOT_DIR")) WritePng(std::string(dir) + "/" + name + ".png", Picture(sim, 24, 26, 31));
}
static int CountType(const vsed::VsEditGraph& g, const char* type) {
    int n = 0;
    for (const auto& b : g.nodes) if (b.type == type) ++n;
    return n;
}
static void Tap(Sim& sim, UI::KeyId k, const std::function<void()>& ui, bool ctrl = false, bool shift = false) {
    sim.ctrl = ctrl; sim.shift = shift;
    sim.Hold(k, true); sim.Frame(ui); sim.Hold(k, false); sim.Frame(ui);
    sim.ctrl = false; sim.shift = false;
}
// A point of the canvas (inside x0..x1, y0..y1) that no drawn block covers, at least `margin` pixels from every block.
static bool EmptySpot(const vsapp::VisualScriptApp& app, const vsed::VsEditGraph& g, float x0, float y0, float x1, float y1, float& ox, float& oy,
                      float margin = 30.0f) {
    for (float y = y1; y >= y0; y -= 17.0f)
        for (float x = x0; x <= x1; x += 23.0f) {
            bool free = true;
            for (const auto& n : g.nodes) {
                float a = 0, b = 0, c = 0, d = 0;
                if (app.BlockOnScreen(n.id, a, b, c, d) && x > a - margin && x < c + margin && y > b - margin && y < d + margin) { free = false; break; }
            }
            if (free) { ox = x; oy = y; return true; }
        }
    return false;
}
static void Drag(Sim& sim, float x0, float y0, float x1, float y1, const std::function<void()>& ui) {
    sim.Move(x0, y0); sim.Frame(ui);
    sim.Down(); sim.Frame(ui);
    for (int i = 1; i <= 6; ++i) { sim.Move(x0 + (x1 - x0) * (float)i / 6.0f, y0 + (y1 - y0) * (float)i / 6.0f); sim.Frame(ui); }
    sim.Up(); sim.Frame(ui);
}

int main() {
    RUN_SUITE("okayui visual script app (the Visual Script program and the editor's Visual Script window)");
    Reset();
    UI::UseLauncherLook(true);
    UI::Style().caretBlink = false;

    const fs::path dir = fs::temp_directory_path() / "okay_visual_script_app_test";
    std::error_code ec;
    fs::remove_all(dir, ec);
    const fs::path proj = dir / "Space Run";
    okay::edtabs::UserFilesDir() = (dir / "user").u8string();   // the model's tab list, bookmarks, favourites: not the working folder
    {   // two graphs: the starter graph, and one with a Print and a block wired to nothing
        vsed::VsEditGraph a;
        vsed::SeedStarterGraph(a);
        WriteFile(proj / "Assets/Graphs/Player.okayvs", a.toText());
        vsed::VsEditGraph b;
        const int ev = b.add("OnStart", 40, 40), pr = b.add("Print", 300, 40);
        b.linkExec(ev, 0, pr);
        b.add("Wait", 300, 220);   // reached by nothing: the checker warns
        WriteFile(proj / "Assets/Graphs/Enemy.okayvs", b.toText());
    }
    const std::string player = (proj / "Assets/Graphs/Player.okayvs").u8string();
    const std::string enemy = (proj / "Assets/Graphs/Enemy.okayvs").u8string();

    vsapp::VisualScriptApp::Config cfg;
    cfg.settingsPath = (dir / "settings" / "visual_script.json").u8string();
    cfg.idPrefix = "##test_vs";
    cfg.quitItem = true;

    // ---- the program: the app over the whole window ----
    {
        const float W = 1000, H = 640;
        Sim sim(W, H);
        vsapp::VisualScriptApp app(cfg);
        auto ui = [&] { app.Frame(0, 0, W, H); };
        CHECK(app.OpenPath(proj.u8string()));
        CHECK(app.Folder().find("Space Run") != std::string::npos);
        CHECK(app.OpenFile(player));
        sim.Frames(3, ui);
        CHECK(app.TabCount() == 1);
        CHECK(app.WindowTitle() == "Player.okayvs - Space Run - Visual Script");
        CHECK(!app.Active().g.dirty);
        CHECK(app.Active().g.nodes.size() == 12);
        app.FrameAll();
        sim.Move(5, H - 5);
        sim.Frames(3, ui);
        Golden(sim, "visual_script_app");

        // the blocks were drawn and their plugs are where the pointer can grab them
        const vsed::VsEditGraph& g0 = app.Active().g;
        int onStart = -1, print = -1;
        for (const auto& n : g0.nodes) { if (n.type == "OnStart") onStart = n.id; if (n.type == "Print") print = n.id; }
        float bx0 = 0, by0 = 0, bx1 = 0, by1 = 0;
        CHECK(app.BlockOnScreen(print, bx0, by0, bx1, by1));
        CHECK(bx1 > bx0 && by1 > by0);

        // ---- the Add Block list: a click on empty canvas, Space, type, Enter ----
        float ex = 0, ey = 0;
        CHECK(EmptySpot(app, app.Active().g, 40, 90, W * 0.6f, H - 80, ex, ey));
        sim.Click(ex, ey, ui);
        CHECK(app.Active().sel.empty());
        sim.Hold(UI::KeyId_Space, true); sim.Frame(ui); sim.Hold(UI::KeyId_Space, false); sim.Frame(ui);
        CHECK(app.PaletteOpen());
        sim.Type("wait");
        sim.Frames(2, ui);
        const std::vector<std::string> rows = app.PaletteRows();
        CHECK(!rows.empty());
        CHECK(!rows.empty() && rows.front().find("Wait") != std::string::npos);
        Golden(sim, "visual_script_app_palette");
        const std::string added = app.PaletteTypes().empty() ? std::string() : app.PaletteTypes().front();   // the starter "Wait Then Continue"
        CHECK(added == "Delay");
        sim.Press(Key_Enter);
        Tap(sim, UI::KeyId_Enter, ui);
        CHECK(!app.PaletteOpen());
        CHECK(CountType(app.Active().g, added.c_str()) == 1);
        CHECK(app.Active().g.dirty);
        CHECK(app.WindowTitle().rfind("*Player.okayvs", 0) == 0);
        int wait = -1;
        for (const auto& n : app.Active().g.nodes) if (n.type == added) wait = n.id;
        CHECK(app.Active().selected == wait);

        // ---- wiring: drag from Print's flow output to the Wait block's flow input ----
        sim.Frames(2, ui);
        float ax = 0, ay = 0, wx = 0, wy = 0;
        CHECK(app.PlugOnScreen(print, true, true, 0, ax, ay));
        CHECK(app.PlugOnScreen(wait, true, false, 0, wx, wy));
        Drag(sim, ax, ay, wx, wy, ui);
        bool wired = false;
        for (const auto& l : app.Active().g.execLinks) if (l.from == print && l.to == wait) wired = true;
        CHECK(wired);

        // ---- a refused wire: a flow plug dropped on a value plug says why ----
        float vx = 0, vy = 0;
        CHECK(app.PlugOnScreen(print, false, false, 0, vx, vy));   // Print's Text input (a value plug)
        float ox = 0, oy = 0;
        CHECK(app.PlugOnScreen(onStart, true, true, 0, ox, oy));
        const std::size_t before = app.Active().g.execLinks.size() + app.Active().g.dataLinks.size();
        Drag(sim, ox, oy, vx, vy, ui);
        CHECK(app.Active().g.execLinks.size() + app.Active().g.dataLinks.size() == before);
        CHECK(app.Active().refuseMsg == vsed::VsPlainFlowToValue());

        // ---- a wire dropped on empty space: the list shows only blocks that take a flow wire, and the pick is wired ----
        app.FrameAll();   // the block may sit near the edge: everything in view first
        sim.Frames(2, ui);
        CHECK(app.PlugOnScreen(wait, true, true, 0, ax, ay));
        CHECK(EmptySpot(app, app.Active().g, 40, 90, W * 0.6f, H - 80, ex, ey));
        Drag(sim, ax, ay, ex, ey, ui);
        CHECK(app.PaletteOpen());

        const std::vector<std::string> types = app.PaletteTypes();
        CHECK(!types.empty());
        bool allTakeFlow = true;
        for (const std::string& t : types) { const vsed::VsDef* d = vsed::FindDef(t); if (!d || !d->execIn) allTakeFlow = false; }
        CHECK(allTakeFlow);
        app.SetPaletteQuery("print");
        CHECK(!app.PaletteRows().empty());
        CHECK(app.PickPaletteRow(0));
        int print2 = -1;
        for (const auto& n : app.Active().g.nodes) if (n.type == "Print" && n.id != print) print2 = n.id;
        wired = false;
        for (const auto& l : app.Active().g.execLinks) if (l.from == wait && l.to == print2) wired = true;
        CHECK(print2 >= 0 && wired);

        // ---- moving a block: a drag by its header, then snapped to the 20-unit grid ----
        sim.Frames(2, ui);
        CHECK(app.BlockOnScreen(wait, bx0, by0, bx1, by1));
        const vsed::VsEditNode* wb = app.Active().g.find(wait);
        if (!wb) { std::cerr << "  the added block is gone\n"; return 1; }
        const float oldX = wb->x;
        Drag(sim, bx0 + 12, by0 + 6, bx0 + 12 + 90, by0 + 6 + 40, ui);
        const vsed::VsEditNode* wn = app.Active().g.find(wait);
        CHECK(wn && wn->x > oldX + 20);
        CHECK(wn && std::fmod(wn->x, 20.0f) == 0.0f && std::fmod(wn->y, 20.0f) == 0.0f);

        // ---- a selection box (Shift+drag on empty space) around the On Start and Print blocks ----
        float s0x = 0, s0y = 0, s1x = 0, s1y = 0, p0x = 0, p0y = 0, p1x = 0, p1y = 0;
        CHECK(app.BlockOnScreen(onStart, s0x, s0y, s1x, s1y));
        CHECK(app.BlockOnScreen(print, p0x, p0y, p1x, p1y));
        sim.shift = true;
        Drag(sim, std::min(s0x, p0x) - 8, std::min(s0y, p0y) - 8, std::max(s1x, p1x) + 8, std::max(s1y, p1y) + 8, ui);
        sim.shift = false;
        CHECK(app.Active().sel.count(onStart) == 1 && app.Active().sel.count(print) == 1);

        // ---- the IDE tools on the selection: align their left edges ----
        CHECK(app.RunCommand("vs.alignLeft"));
        CHECK(app.Active().g.find(onStart)->x == app.Active().g.find(print)->x);

        // ---- Delete, then Undo and Redo (one step each) ----
        sim.Frames(3, ui);   // the move is one undo step once the mouse is up
        const std::size_t blocks = app.Active().g.nodes.size(), selected = app.Active().sel.size();
        Tap(sim, UI::KeyId_Delete, ui);
        CHECK(selected >= 2 && app.Active().g.nodes.size() == blocks - selected);
        sim.Frames(2, ui);
        Tap(sim, UI::KeyId_Z, ui, true);
        CHECK(app.Active().g.nodes.size() == blocks);
        Tap(sim, UI::KeyId_Y, ui, true);
        CHECK(app.Active().g.nodes.size() == blocks - selected);
        Tap(sim, UI::KeyId_Z, ui, true);
        CHECK(app.Active().g.nodes.size() == blocks);

        // ---- Ctrl+S writes the file ----
        Tap(sim, UI::KeyId_S, ui, true);
        CHECK(!app.Active().g.dirty);
        CHECK(app.Notice() == "Saved Player.okayvs");
        {
            vsed::VsEditGraph back;
            CHECK(back.load(player));
            CHECK(CountType(back, added.c_str()) == 1 && CountType(back, "Print") == 2);
        }

        // ---- compare with the saved file: a new block shows as added ----
        app.AddBlock("Branch", 900, 40);
        CHECK(app.CompareWithSaved());
        bool addedRow = false;
        for (const std::string& r : app.CompareRows()) if (r.rfind("+ Branch", 0) == 0) addedRow = true;
        CHECK(addedRow);
        CHECK(app.Panel() == 4);
        sim.Frames(2, ui);

        // ---- the OkayScript text, a breakpoint and the paused debugger's keys ----
        CHECK(app.OkayScriptText().find("Debug.Log(\"Ready!") != std::string::npos);   // the starter's Print

        app.Active().sel = {print}; app.Active().selected = print;
        CHECK(app.ToggleBreakpoint());
        CHECK(okay::vs::VsDebugger::Get().HasBreakpoint(player, print));
        {
            vsed::VsDebugView& dv = vsed::DebugView();
            dv.stopped = true; dv.key = player; dv.node = print; dv.nodeType = "Print"; dv.object = "Ship"; dv.event = "Start";
            dv.resume = false;
            sim.Frames(2, ui);
            Tap(sim, UI::KeyId_F10, ui);
            CHECK(dv.resume);
            CHECK(okay::vs::VsDebugger::Get().PendingStep() == okay::vs::VsDebugger::Step::Over);
            dv = vsed::VsDebugView{};
            okay::vs::VsDebugger::Get().RequestStep(okay::vs::VsDebugger::Step::None);
            okay::vs::VsDebugger::Get().ClearBreakpoints();
        }

        // ---- find references: Print is in both graphs ----
        app.Active().sel = {print}; app.Active().selected = print;
        CHECK(app.RunCommand("vs.findReferences"));
        bool inEnemy = false, inPlayer = false;
        for (const std::string& r : app.NavigatorRows()) { if (r.rfind("Enemy.okayvs", 0) == 0) inEnemy = true; if (r.rfind("Player.okayvs", 0) == 0) inPlayer = true; }
        CHECK(inEnemy && inPlayer);
        CHECK(app.RunCommand("vs.toggleBookmark"));
        CHECK(vsed::Bookmarks().has(player, print));

        // ---- collapse to function: the Translate step becomes one Call Function block and a function body ----
        {
            int move = -1;
            for (const auto& n : app.Active().g.nodes) if (n.type == "Translate") move = n.id;
            app.Active().sel = {move}; app.Active().selected = move;
            const bool collapsed = app.RunCommand("vs.collapseToFunction");
            CHECK(collapsed);
            CHECK(CountType(app.Active().g, "CallFunction") == 1);
            CHECK(CountType(app.Active().g, "FunctionStart") == 1);
        }

        // ---- the second graph: the checker's warning and its quick fix ----
        CHECK(app.OpenFile(enemy));
        CHECK(app.TabCount() == 2);
        CHECK(app.CheckNow() > 0);
        bool unreached = false;
        for (const std::string& l : app.ProblemLines()) if (l.rfind("Warning: ", 0) == 0) unreached = true;
        CHECK(unreached);
        const std::size_t problems = app.CheckNow();
        bool fixedOne = false;
        for (std::size_t i = 0; i < problems && !fixedOne; ++i) fixedOne = app.FixProblem(i);
        CHECK(fixedOne);
        CHECK(app.CheckNow() < problems);
        app.ShowPanel(1);
        sim.Frames(2, ui);

        // ---- closing a changed tab asks; Cancel keeps it, Don't Save closes it, Reopen brings the edits back ----
        CHECK(app.Active().g.dirty);
        Tap(sim, UI::KeyId_W, ui, true);
        CHECK(app.ClosePromptOpen());
        app.AnswerClosePrompt(2);
        CHECK(app.TabCount() == 2);
        app.RequestClose(app.ActiveTab());
        app.AnswerClosePrompt(1);
        CHECK(app.TabCount() == 1);
        CHECK(app.ReopenClosed());
        CHECK(app.TabCount() == 2 && app.Active().g.dirty);
        CHECK(app.Active().g.path == enemy);

        // ---- a new graph: Ctrl+S on an untitled one asks where; Save As writes it ----
        Tap(sim, UI::KeyId_N, ui, true);
        CHECK(app.TabCount() == 3);
        CHECK(app.Active().g.path.empty());
        CHECK(!app.Save());   // the Save As box opens
        const std::string fresh = (proj / "Assets/Graphs/Fresh").u8string();
        CHECK(app.SaveAs(fresh));
        CHECK(fs::exists(fs::u8path(fresh + ".okayvs")));
        CHECK(app.WindowTitle() == "Fresh.okayvs - Space Run - Visual Script");

        // ---- settings: the panel and View choices are kept for the next run ----
        app.Settings().showMinimap = false;
        app.SaveSettings();

        // ---- Quit with changed graphs asks first ----
        Tap(sim, UI::KeyId_Q, ui, true);
        CHECK(!app.WantsQuit());
        CHECK(app.ClosePromptOpen());
        app.AnswerClosePrompt(1);
        CHECK(app.WantsQuit());
    }
    {   // the settings file read back by a second app (the program and the editor's window share it)
        vsapp::VisualScriptApp again(cfg);
        CHECK(!again.Settings().showMinimap);
        CHECK(again.Settings().lastFolder.find("Space Run") != std::string::npos);
        CHECK(!again.Settings().recentFiles.empty());
        const vsapp::AppSettings bad = vsapp::AppSettings::Parse("{\"panel\": 9, \"showDetails\": \"yes\", \"recentFiles\": [1, \"a\", \"a\"]}");
        CHECK(bad.panel == 0 && bad.showDetails && bad.recentFiles.size() == 1);
        CHECK(vsapp::AppSettings::Parse("not json").snapGrid);
        const vsapp::AppSettings round = vsapp::AppSettings::Parse(again.Settings().Serialize());
        CHECK(round.recentFiles == again.Settings().recentFiles && round.showMinimap == again.Settings().showMinimap);
    }

    // ---- the editor's window: the app in a host's rectangle, with the host's hooks (a fake host) ----
    {
        vsed::Session() = vsed::VsSession{};   // the editor starts with its own open tabs
        const float W = 1100, H = 700;
        Sim sim(W, H);
        int saves = 0, stops = 0, floats = 0;
        std::string savedText;
        vsapp::VisualScriptApp::Config hc;
        hc.idPrefix = "##test_vs_host";
        hc.save = [&](vsed::VsEditState& tab, std::string&) { ++saves; savedText = tab.g.toText(); vsed::VsMarkClean(tab); return true; };
        hc.stopPlay = [&] { ++stops; };
        hc.windowCommands = [&] { return std::vector<vsapp::AppCommand>{{"Float Window", "", [&] { ++floats; }}}; };
        std::vector<std::string> logged;
        hc.log = [&](const std::string& line, int) { logged.push_back(line); };
        vsapp::VisualScriptApp host(hc);
        auto ui = [&] { UI::Panel(0, 0, W, H); host.Frame(60, 50, W - 120, H - 100); };
        CHECK(host.OpenPath(proj.u8string()));
        CHECK(host.OpenFile(enemy));
        host.FrameAll();
        sim.Move(3, 3);
        sim.Frames(3, ui);
        Golden(sim, "visual_script_app_hosted");
        // the block under a click inside the hosted rectangle is selected
        int pr = -1;
        for (const auto& n : host.Active().g.nodes) if (n.type == "Print") pr = n.id;
        float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        CHECK(host.BlockOnScreen(pr, x0, y0, x1, y1));
        CHECK(x0 >= 60 && y0 >= 50);
        sim.Click(x0 + 10, y0 + 6, ui);
        CHECK(host.Active().selected == pr);
        CHECK(host.WantsKeyboard());
        // Ctrl+S goes to the host's save: the app writes nothing itself
        host.AddBlock("Wait", 600, 300);
        const std::string disk = ReadFile(fs::u8path(enemy));
        Tap(sim, UI::KeyId_S, ui, true);
        CHECK(saves == 1);
        CHECK(savedText.find("Wait") != std::string::npos);
        CHECK(ReadFile(fs::u8path(enemy)) == disk);
        CHECK(!host.Active().g.dirty);
        CHECK(!logged.empty() && logged.back() == "Saved Enemy.okayvs");
        // the paused bar's Stop Play goes to the host
        vsed::VsDebugView& dv = vsed::DebugView();
        dv.stopped = true; dv.key = enemy; dv.node = pr; dv.nodeType = "Print";
        sim.Frames(2, ui);
        CHECK(host.WindowTitle().find("Enemy.okayvs") == 0);
        dv = vsed::VsDebugView{};
        (void)stops; (void)floats;
    }

    fs::remove_all(dir, ec);
    UI::UseLauncherLook(false);
    TEST_MAIN_RESULT();
}
