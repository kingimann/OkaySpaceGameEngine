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
#include "okay/Components/VisualScriptComponent.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/VisualScript/VsRefs.hpp"

#include <cmath>
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

    // the "What is this?" card of the selected block sits over the canvas's bottom-right corner; these steps drag to blocks that may
    // be under it, so it starts folded (the round-2 part below opens it)
    vsed::VsPrefs().helpCardOpen = false;
    vsed::VsPrefs().hideStart = false;

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
        // the everyday "wait" block first: a Quick block or a starter one ranks first, as the editor's old list ranked them
        CHECK(added == "Delay" || added == "QbWaitThenDo");
        const vsed::VsDef* addedDef = vsed::FindDef(added);
        CHECK(addedDef && addedDef->execIn && !addedDef->execOut.empty());
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
        app.Active().sel.clear(); app.Active().selected = -1;   // no help card over the canvas
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


    // ======================================================================================================================
    // Round 2: what moved from the editor's old Dear ImGui Visual Script window into the app (the window itself is deleted).
    // The editor is a real EditorState (its scene, the selected object, Play); the hooks the editor's main.cpp sets (Add
    // Component, the Actions step list, the earlier saves) are set here the same way.
    // ======================================================================================================================
    {
        vsed::Session() = vsed::VsSession{};
        vsed::VsPrefs() = vsed::VsUsagePrefs{};   // a person who has seen nothing yet: the start cards, the hints and the help card show
        vsed::VsStore() = vsed::VsPaletteStore{};
        vsed::VsStore().loaded = true;
        static int addCalls = 0;
        static std::string addedId;
        vsed::VsHasComponent = [](okay::GameObject*, const char* id) -> bool { return addedId == id; };
        vsed::VsAddComponentById = [](okay::GameObject*, const char* id) -> bool { ++addCalls; addedId = id; return true; };
        vsed::StepCatalog = [](bool cond) -> const std::vector<vsed::VsStepInfo>* {
            static const std::vector<vsed::VsStepInfo> acts = {{"log", "Print Message", "Debug", "Writes a line.", false, "text"},
                                                               {"move", "Move By", "Move", "Moves this object.", false, "x y z"}};
            static const std::vector<vsed::VsStepInfo> conds = {{"always", "Always", "Basic", "Always passes.", false, ""}};
            return cond ? &conds : &acts;
        };
        static std::string earlier;
        vsed::VsBackups = [](const std::string&) {
            return std::vector<std::pair<std::string, std::string>>{{"Earlier today", earlier}};
        };

        okay::editor::EditorState ed;
        ed.setProjectDir(proj.u8string());
        okay::GameObject* ship = ed.scene().CreateGameObject("Ship");
        okay::GameObject* rock = ed.scene().CreateGameObject("Rock");
        const float W = 1200, H = 760;
        Sim sim(W, H);
        vsapp::VisualScriptApp::Config rc;
        rc.idPrefix = "##test_vs_r2";
        rc.editor = [&ed]() { return &ed; };
        rc.liveGraph = [&](const std::string& path) -> const okay::vs::NodeGraph* {
            if (!ed.isPlaying()) return nullptr;
            for (auto* vc : ship->GetComponents<okay::VisualScriptComponent>()) if (vsed::VsSamePath(vc->Path(), path)) return vc->Graph();
            return nullptr;
        };
        vsapp::VisualScriptApp app(rc);
        bool externalOn = false, externalRelease = false;
        std::string externalType, externalData;
        auto ui = [&] {
            if (externalOn) UI::SetExternalDragDrop(externalType.c_str(), externalData.c_str(), (int)externalData.size() + 1, externalRelease);
            app.Frame(0, 0, W, H);
        };
        auto centre = [&](const std::string& id, float& x, float& y) {
            float a = 0, b = 0, c = 0, d = 0;
            if (!app.FieldOnScreen(id, a, b, c, d)) return false;
            x = (a + c) * 0.5f; y = (b + d) * 0.5f;
            return true;
        };
        auto dropOn = [&](const std::string& id, const std::string& type, const std::string& data) {
            float x = 0, y = 0;
            if (!centre(id, x, y)) return false;
            externalOn = true; externalRelease = false; externalType = type; externalData = data;
            sim.Move(x, y); sim.Frames(2, ui);
            externalRelease = true; sim.Frame(ui);
            externalOn = false; externalRelease = false; sim.Frame(ui);
            return true;
        };
        auto select = [&](int id) { app.Active().sel = {id}; app.Active().selected = id; sim.Frames(2, ui); };
        // the middle of a flow wire that no block covers (the curve's middle is the middle of its two plugs); false when none is free
        auto freeWireMiddle = [&](int from, float& wx, float& wy, int& wa, int& wb) {
            for (const auto& l : app.Active().g.execLinks) {
                if (from >= 0 && l.from != from) continue;
                float ax = 0, ay = 0, bx = 0, by = 0;
                if (!app.PlugOnScreen(l.from, true, true, l.pin, ax, ay) || !app.PlugOnScreen(l.to, true, false, 0, bx, by)) continue;
                const float x = (ax + bx) * 0.5f, y = (ay + by) * 0.5f;
                if (x < 20 || y < 70 || x > W - 340 || y > H - 60) continue;   // not under the Details pane
                bool covered = false;
                for (const auto& n : app.Active().g.nodes) {
                    float a = 0, b = 0, c = 0, d = 0;
                    if (app.BlockOnScreen(n.id, a, b, c, d) && x > a - 14 && x < c + 14 && y > b - 14 && y < d + 14) covered = true;
                }
                if (std::fabs(ax - bx) < 40.0f) covered = true;   // too short to hit the curve and not a plug
                if (!covered) { wx = x; wy = y; wa = l.from; wb = l.to; return true; }
            }
            return false;
        };
        CHECK(app.OpenFolder(proj.u8string()));

        // ---- the start cards over an empty graph: one click puts a small working graph on the canvas ----
        app.NewGraph(false);
        sim.Move(3, 3);
        sim.Frames(3, ui);
        CHECK(app.StartCardsShown());
        float c0 = 0, c1 = 0, c2 = 0, c3 = 0;
        CHECK(app.StartCardOnScreen(0, c0, c1, c2, c3));
        sim.Click((c0 + c2) * 0.5f, (c1 + c3) * 0.5f, ui);
        sim.Frames(2, ui);
        CHECK(!app.Active().g.nodes.empty());
        CHECK(!app.StartCardsShown());
        CHECK(!app.HintLine().empty());   // the next-step hint at the bottom of the canvas

        // ---- the "What is this?" card of the selected block: Next steps chips, Try this block ----
        const int firstBlock = app.Active().g.nodes.front().id;
        select(firstBlock);
        CHECK(app.HelpCardTitle().rfind("What is this?", 0) == 0);
        const std::size_t before = app.Active().g.nodes.size();
        if (!app.NextStepChips().empty()) {
            CHECK(app.AddNextStep(0));
            CHECK(app.Active().g.nodes.size() == before + 1);
        }
        select(firstBlock);
        const std::size_t beforeTry = app.Active().g.nodes.size();
        CHECK(app.TryThisBlock());
        CHECK(app.Active().g.nodes.size() > beforeTry);
        vsed::VsPrefs().helpCardOpen = false;   // folded from here on: the gestures below reach blocks and wires it would cover
        // a plain sentence under each block's title, and its badge
        sim.Frames(2, ui);
        CHECK(!app.BlockSentence(firstBlock).empty());
        const std::string badge = app.BlockBadge(firstBlock);
        CHECK(badge == "Ready" || badge == "Needs input" || badge == "Error");

        // ---- a graph on a file, attached to the selected object (Attach to Selected Object) ----
        const std::string shipGraph = (proj / "Assets/Graphs/ShipFields.okayvs").u8string();
        CHECK(app.SaveAs(shipGraph));
        ed.Select(ship);
        CHECK(app.AttachToSelected());
        bool attached = false;
        for (auto* vc : ship->GetComponents<okay::VisualScriptComponent>()) if (vsed::VsSamePath(vc->Path(), shipGraph)) attached = true;
        CHECK(attached);

        // ---- a block's fields: a key you press, an object dropped from the Hierarchy, a file dropped from the Project panel ----
        vsed::VsEditGraph& g = app.Active().g;
        const int onKey = g.add("OnKey", 900, 900);
        select(onKey);
        float kx = 0, ky = 0;
        CHECK(centre("key" + std::to_string(onKey), kx, ky));
        sim.Click(kx, ky, ui);
        Tap(sim, UI::KeyId_J, ui);
        CHECK(app.Active().g.find(onKey)->arg == "j");

        const int find = app.Active().g.add("FindObject", 900, 1100);
        select(find);
        CHECK(dropOn("arg" + std::to_string(find), "OBJECT_REF", okay::vs::VsGuidRef(rock->Guid())));
        CHECK(app.Active().g.find(find)->arg == okay::vs::VsGuidRef(rock->Guid()));

        const int spawn = app.Active().g.add("Spawn", 900, 1300);
        select(spawn);
        const std::string crate = (proj / "Assets/Prefabs/Crate.okayprefab").u8string();
        WriteFile(fs::u8path(crate), "prefab\n");
        CHECK(dropOn("arg" + std::to_string(spawn), "ASSET_PATH", crate));
        CHECK(app.Active().g.find(spawn)->arg.find("Crate.okayprefab") != std::string::npos);
        CHECK(dropOn("arg" + std::to_string(spawn), "ASSET_PATH", (proj / "Assets/boom.wav").u8string()));   // the wrong kind: refused, said
        CHECK(app.Active().g.find(spawn)->arg.find("Crate.okayprefab") != std::string::npos);
        CHECK(app.Active().refHint.find("Wrong file") == 0);

        // a Quick block's key field and number field
        const int jump = app.Active().g.add("QbJumpOnKey", 900, 1500);
        select(jump);
        CHECK(centre("qk" + std::to_string(jump) + "_0", kx, ky));
        sim.Click(kx, ky, ui);
        Tap(sim, UI::KeyId_K, ui);
        CHECK(app.Active().g.find(jump)->pinDefault[0] == "k");
        float nx = 0, ny = 0;
        CHECK(centre("q" + std::to_string(jump) + "_1", nx, ny));   // Strength: a number you drag
        const int flash = app.Active().g.add("QbFlashColour", 900, 1700);
        select(flash);
        CHECK(centre("q" + std::to_string(flash) + "_0", nx, ny));  // Colour: a swatch
        {
            float rgb[3] = {1.0f, 0.0f, 0.5f};
            CHECK(vsed::VsColourHex(rgb) == "#ff0080");
            float back[3];
            vsed::VsColourParts("#ff0080", back);
            CHECK(back[0] == 1.0f && back[1] == 0.0f && std::fabs(back[2] - 128.0f / 255.0f) < 1e-6f);
        }

        // the Action Steps editor inside a block, and Switch cases
        const int steps = app.Active().g.add("Actions", 900, 1900);
        select(steps);
        const std::size_t stepsBefore = okay::ActionList::DecodeItems(app.Active().g.find(steps)->arg).size();   // a new block has one step
        CHECK(centre("addstep" + std::to_string(steps), kx, ky));
        sim.Click(kx, ky, ui);
        CHECK(okay::ActionList::DecodeItems(app.Active().g.find(steps)->arg).size() == stepsBefore + 1);
        const int sw = app.Active().g.add("SwitchCases", 900, 2100);
        app.Active().g.find(sw)->arg = "1, 2";
        select(sw);
        CHECK(centre("case" + std::to_string(sw), kx, ky));
        sim.Click(kx, ky, ui);
        CHECK(vsed::VsEditorSwitchCases(app.Active().g.find(sw)->arg).size() == 3);

        // ---- the saved values panel (nothing selected): a name typed and Add ----
        app.Active().sel.clear(); app.Active().selected = -1;
        sim.Frames(2, ui);
        CHECK(centre("newvar", kx, ky));
        sim.Click(kx, ky, ui);
        sim.Type("lives");
        sim.Frames(2, ui);
        CHECK(centre("addvar", kx, ky));
        sim.Click(kx, ky, ui);
        bool declared = false;
        for (const auto& v : app.Active().g.vars) if (v.name == "lives") declared = true;
        CHECK(declared);

        // ---- the outline: events, breakpoints, saved values ----
        app.Settings().showOutline = true;
        sim.Frames(2, ui);
        okay::vs::VsDebugger::Get().ToggleBreakpoint(app.Active().g.path, onKey);
        bool evRow = false, bpRow = false;
        for (const std::string& r : app.OutlineRows()) {
            if (r.rfind(std::string(vsed::kPanelEvents) + ": ", 0) == 0) evRow = true;
            if (r.rfind(std::string(vsed::kPanelStopPoints) + ": ", 0) == 0) bpRow = true;
        }
        CHECK(evRow && bpRow);
        okay::vs::VsDebugger::Get().ClearBreakpoints();
        app.Settings().showOutline = false;

        // ---- a block's missing component (the object running the graph lacks it) and Add Component ----
        int needy = -1;
        for (const vsed::VsDef& d : vsed::Registry()) {
            const vsed::VsNodeDoc* doc = d.type ? vsed::VsNodeDocFor(d.type) : nullptr;
            if (!doc || doc->component.empty() || doc->component.find('|') != std::string::npos || vsed::VsPaletteHidden(d)) continue;
            needy = app.Active().g.add(d.type, 1400, 900);
            break;
        }
        CHECK(needy >= 0);
        sim.Frames(2, ui);
        const std::string missing = app.MissingComponent(needy);
        CHECK(!missing.empty());
        CHECK(app.AddMissingComponent(needy));
        CHECK(addCalls == 1 && addedId == missing);
        CHECK(app.MissingComponent(needy).empty());

        // ---- Connect to...: a block's plug, then the blocks that fit it, the pick wired ----
        const int pr = app.Active().g.add("Print", 1400, 1200);
        sim.Frames(2, ui);
        const std::vector<std::string> plugsRows = app.ConnectRows(pr);
        std::size_t flowOut = plugsRows.size();
        for (std::size_t i = 0; i < plugsRows.size(); ++i) if (plugsRows[i].rfind("Flow out", 0) == 0) { flowOut = i; break; }
        CHECK(flowOut < plugsRows.size());
        CHECK(app.ConnectFrom(pr, flowOut));
        CHECK(app.PaletteOpen());
        app.SetPaletteQuery("print");
        CHECK(app.PickPaletteRow(0));
        bool connected = false;
        for (const auto& l : app.Active().g.execLinks) if (l.from == pr) connected = true;
        CHECK(connected);

        // ---- a block put on a wire: double-click the wire, pick; Shift+double-click: a knot; drag it; right-click takes it away ----
        app.Active().focusNode = pr;   // the view on that block, readable (the whole graph is spread too wide to fit)
        sim.Frames(3, ui);
        int wireFrom = -1, wireTo = -1;
        float mx = 0, my = 0;
        CHECK(freeWireMiddle(pr, mx, my, wireFrom, wireTo));
        sim.Click(mx, my, ui);
        sim.Click(mx, my, ui);
        CHECK(app.PaletteOpen());
        app.SetPaletteQuery("wait");
        const std::size_t blocksBefore = app.Active().g.nodes.size();
        CHECK(app.PickPaletteRow(0));
        CHECK(app.Active().g.nodes.size() == blocksBefore + 1);
        bool direct = false;
        for (const auto& l : app.Active().g.execLinks) if (l.from == wireFrom && l.to == wireTo) direct = true;
        CHECK(!direct);   // the new block sits between them now
        app.Active().focusNode = pr;
        sim.Frames(3, ui);
        int ka = -1, kb = -1;
        CHECK(freeWireMiddle(-1, mx, my, ka, kb));
        sim.shift = true;
        sim.Click(mx, my, ui);
        sim.Click(mx, my, ui);
        sim.shift = false;
        CHECK(app.Active().g.knots.size() == 1);
        sim.Frames(2, ui);
        float kx0 = 0, ky0 = 0;
        CHECK(app.KnotOnScreen(0, kx0, ky0));
        const float knotX = app.Active().g.knots.empty() ? 0.0f : app.Active().g.knots[0].x;
        Drag(sim, kx0, ky0, kx0 + 60, ky0 + 30, ui);
        CHECK(app.Active().g.knots.size() == 1 && app.Active().g.knots[0].x > knotX + 10);
        CHECK(app.KnotOnScreen(0, kx0, ky0));
        sim.Move(kx0, ky0); sim.Frame(ui);
        sim.rdown = true; sim.Frame(ui); sim.rdown = false; sim.Frame(ui);
        CHECK(app.Active().g.knots.empty());

        // ---- a comment frame: resized by its corner, coloured from its menu ----
        app.Active().sel = {pr};
        CHECK(app.CommentSelection());
        const int cm = (int)app.Active().g.comments.size() - 1;   // the new frame
        app.Active().focusNode = pr;
        sim.Frames(3, ui);
        float gx = 0, gy = 0;
        CHECK(app.CommentGripOnScreen(cm, gx, gy));
        const float cw = app.Active().g.comments[(std::size_t)cm].w;
        Drag(sim, gx, gy, gx + 80, gy + 50, ui);
        CHECK(app.Active().g.comments[(std::size_t)cm].w > cw + 20);
        {
            float t0 = 0, t1 = 0, t2 = 0, t3 = 0;
            CHECK(app.CommentOnScreen(cm, t0, t1, t2, t3));
            const float titleX = t0 + 30.0f, titleY = t1 + 6.0f;   // its title bar
            sim.Move(titleX, titleY); sim.Frame(ui);
            sim.rdown = true; sim.Frame(ui); sim.rdown = false; sim.Frame(ui);
            const float lh = UI::GetTextLineHeight() + 6;
            sim.Click(titleX + 30, titleY + 4 + lh + 7 + lh + lh * 0.5f, ui);   // Rename, a line, Colour: Blue, then Colour: Green
            CHECK(app.Active().g.comments[(std::size_t)cm].color == 1);
        }

        // ---- Restore Previous Version (the editor's earlier saves), and Undo takes it back ----
        vsed::VsEditGraph one;
        one.add("OnStart", 40, 40);
        earlier = one.toText();
        CHECK(app.PreviousVersions().size() == 1);
        const std::size_t nowBlocks = app.Active().g.nodes.size();
        CHECK(app.RestoreVersion(0));
        CHECK(app.Active().g.nodes.size() == 1);
        CHECK(app.Undo());
        CHECK(app.Active().g.nodes.size() == nowBlocks);

        // ---- Pick a Recipe and Templates ----
        CHECK(app.RecipeNames().size() == vsed::VsRecipes().size() + vsed::VsExtraRecipes().size());
        const std::size_t r0 = app.Active().g.nodes.size();
        CHECK(app.PickRecipe(app.RecipeNames().size() - 1));   // one of the "More recipes"
        CHECK(app.Active().g.nodes.size() > r0);
        const std::vector<std::string> tpl = app.TemplateNames("", 0);
        CHECK(!tpl.empty());
        app.NewGraph(false);
        CHECK(app.ApplyTemplate(tpl.front()));
        CHECK(!app.Active().g.nodes.empty() && app.Active().g.path.empty() && app.Active().g.dirty);

        // ---- the live run: Play runs a graph on the ship; the block that ran says what it did ----
        {
            vsed::VsEditGraph lg;
            const int ev = lg.add("OnStart", 40, 40), said = lg.add("Print", 300, 40);
            lg.linkExec(ev, 0, said);
            lg.find(said)->pinDefault[0] = "hello";
            const std::string liveFile = (proj / "Assets/Graphs/Live.okayvs").u8string();
            WriteFile(fs::u8path(liveFile), lg.toText());
            auto* vc = ship->AddComponent<okay::VisualScriptComponent>();
            std::string err;
            CHECK(vc->LoadFromFile(liveFile, &err));
            CHECK(app.OpenFile(liveFile));
            int printBlock = -1;
            for (const auto& n : app.Active().g.nodes) if (n.type == "Print") printBlock = n.id;
            okay::vs::NodeGraph::Tracing() = true;
            ed.Play();
            for (int i = 0; i < 10; ++i) ed.Tick(1.0f / 60.0f);
            sim.Frames(3, ui);
            CHECK(printBlock >= 0 && !app.LiveLine(printBlock).empty());
            if (printBlock >= 0) std::cerr << "  live line: " << app.LiveLine(printBlock) << "\n";
            ed.Stop();
            sim.Frames(1, ui);
        }

        // ---- Close Others / Close All (a changed tab asks first) ----
        app.NewGraph(true);
        app.Active().g.dirty = true;
        CHECK(app.TabCount() >= 3);
        app.CloseOthers(app.ActiveTab());
        CHECK(app.ClosePromptOpen());
        app.AnswerClosePrompt(1);   // close them without saving
        CHECK(app.TabCount() == 1);
        app.CloseAll();
        CHECK(app.ClosePromptOpen());   // the one left is changed
        app.AnswerClosePrompt(2);       // Cancel
        CHECK(app.TabCount() == 1);

        // ---- Density: the blocks grow with Spacious ----
        app.FrameAll();
        sim.Frames(3, ui);
        int anyBlock = app.Active().g.nodes.empty() ? -1 : app.Active().g.nodes.front().id;
        float d0 = 0, d1 = 0, d2 = 0, d3 = 0, e0 = 0, e1 = 0, e2 = 0, e3 = 0;
        CHECK(app.BlockOnScreen(anyBlock, d0, d1, d2, d3));
        const float z = app.Active().zoom;
        oked::scriptui::SetDensity(2);
        sim.Frames(2, ui);
        app.Active().zoom = z;
        sim.Frames(1, ui);
        CHECK(app.BlockOnScreen(anyBlock, e0, e1, e2, e3));
        CHECK(e3 - e1 > d3 - d1);
        oked::scriptui::SetDensity(0);
        (void)oked::scriptui::TakeDensityChanged();

        vsed::VsHasComponent = nullptr;
        vsed::VsAddComponentById = nullptr;
        vsed::StepCatalog = nullptr;
        vsed::VsBackups = nullptr;
    }

    fs::remove_all(dir, ec);
    UI::UseLauncherLook(false);
    TEST_MAIN_RESULT();
}
