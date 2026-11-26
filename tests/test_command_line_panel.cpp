// The Command Line panel (shared/CommandLinePanel.hpp): okay typed into a window and run inside the program. Its model
// (Enter runs, `clear`, a leading "okay" dropped, history with Up and Down, Tab completion from okay's command table, a
// problem line kept apart from normal words, Copy of every line, the project folder as the working folder) and, driven
// headless through OkayUI like the editor draws it, typing + Enter, Tab and Up in the input line, and the golden picture
// of the panel. OKAY_SHOT_DIR=<folder> also writes the pictures for a person to look at (never committed). Written for
// this repository.
#include "test_framework.hpp"
#include "OkayUITestKit.hpp"
#include "CommandLinePanel.hpp"
#include "Cli.hpp"   // CreateEmptyProject

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <string>

using namespace okayuitest;
namespace UI = OkayUI;
namespace fs = std::filesystem;
using okaycli::CommandLinePanel;
using okaycli::OutLine;

namespace {

bool Has(const std::string& hay, const std::string& needle) { return hay.find(needle) != std::string::npos; }

// Pump until the running command ended (each step is a frame for the drawn tests).
bool WaitIdle(CommandLinePanel& p, const std::function<void()>& step, int limitMs = 20000) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(limitMs);
    while (std::chrono::steady_clock::now() < end) {
        step();
        if (!p.Running()) { step(); return true; }
    }
    return false;
}

void Shot(const char* name, const Bitmap& b) {
    const char* dir = std::getenv("OKAY_SHOT_DIR");
    if (!dir || !*dir) return;
    std::error_code ec;
    fs::create_directories(dir, ec);
    WritePng(std::string(dir) + "/" + name + ".png", b);
}

bool CountKind(const CommandLinePanel& p, OutLine::Kind kind, const std::string& needle) {
    for (const OutLine& l : p.Log().Lines()) if (l.kind == kind && Has(l.text, needle)) return true;
    return false;
}

void TestModel(const fs::path& tmp) {
    RUN_SUITE("command line panel: the model");
    std::string copied;
    CommandLinePanel::Config cfg;
    cfg.copy = [&copied](const std::string& t) { copied = t; };
    CommandLinePanel p(cfg);
    auto pump = [&p] { p.Pump(); };

    CHECK(!p.Submit(""));                            // nothing to run
    CHECK(p.Submit("okay help vc"));                 // a leading "okay" is dropped
    CHECK(p.Running());
    CHECK(!p.Submit("help"));                        // one at a time
    CHECK(Has(p.Log().Text(), "still running"));
    CHECK(WaitIdle(p, pump));
    CHECK(p.LastExitCode() == 0);
    CHECK(CountKind(p, OutLine::Note, "> okay help vc"));
    CHECK(CountKind(p, OutLine::Out, "okay vc <init|clone"));
    CHECK(CountKind(p, OutLine::Note, "Done."));

    CHECK(p.Submit("frobnicate"));                   // a problem: its own kind (drawn red), and the exit code
    CHECK(WaitIdle(p, pump));
    CHECK(p.LastExitCode() == 2);
    CHECK(CountKind(p, OutLine::Err, "There is no command called \"frobnicate\""));
    CHECK(CountKind(p, OutLine::Note, "Finished with exit code 2."));

    CHECK(!p.Submit("help \"vc"));                   // an unclosed quote: said, nothing runs
    CHECK(!p.Running());
    CHECK(Has(p.Log().Text(), "quote is not closed"));

    // History: Up goes back through what was run (the newest first), Down comes back to what was being typed.
    p.SetInput("half typed");
    CHECK(p.HistoryUp() && p.Input() == "help \"vc");
    CHECK(p.HistoryUp() && p.Input() == "frobnicate");
    CHECK(p.HistoryUp() && p.Input() == "help");             // refused while a command ran, still kept to type again
    CHECK(p.HistoryUp() && p.Input() == "okay help vc");
    CHECK(!p.HistoryUp());
    CHECK(p.HistoryDown() && p.Input() == "help");
    CHECK(p.HistoryDown() && p.HistoryDown() && p.HistoryDown() && p.Input() == "half typed");

    // Tab: command names, then sub-commands, from okay's command table.
    p.SetInput("he");
    p.CompleteInput();
    CHECK(p.Input() == "help ");
    p.SetInput("vc sa");
    p.CompleteInput();
    CHECK(p.Input() == "vc save ");
    p.SetInput("vc s");
    p.CompleteInput();
    CHECK(p.Choices().size() > 1);                   // several fit: listed under the line
    CHECK(p.Input().rfind("vc s", 0) == 0);

    // Copy: every line, to the host's clipboard.
    p.Copy();
    CHECK(copied == p.Log().Text() && Has(copied, "okay vc <init|clone"));

    // The project folder is the working folder.
    const fs::path proj = tmp / "PanelGame";
    std::string err;
    CHECK(okaycli::CreateEmptyProject(proj.string(), "PanelGame", "", err));
    p.SetFolder(proj.string());
    CHECK(CountKind(p, OutLine::Note, "Commands run in the project"));
    CHECK(p.Submit("scene list"));
    CHECK(WaitIdle(p, pump));
    CHECK(CountKind(p, OutLine::Out, "Assets/PanelGame.okayscene"));

    // clear empties the panel.
    CHECK(p.Submit("clear"));
    CHECK(p.Log().Lines().empty());
}

void TestDrawn(const fs::path& tmp) {
    RUN_SUITE("command line panel: drawn with OkayUI (keys, golden)");
    Reset();
    UI::UseLauncherLook(true);
    UI::Style().caretBlink = false;
    const float X = 20, Y = 16, W = 600, H = 300;
    Sim sim(X + W + 20, Y + H + 16);
    CommandLinePanel p;
    const fs::path proj = tmp / "Drawn Game";
    std::string err;
    CHECK(okaycli::CreateEmptyProject(proj.string(), "Drawn Game", "", err));
    p.SetFolder(proj.string());
    auto ui = [&] { p.Frame(X, Y, W, H); };
    sim.Frames(2, ui);                                 // the input line takes the keyboard on the first frame

    sim.Type("help new");
    sim.Frame(ui);
    CHECK(p.Input() == "help new");
    sim.Tap(Key_Enter, ui);
    CHECK(p.Input().empty());                          // Enter ran it and emptied the line
    CHECK(WaitIdle(p, [&] { sim.Frame(ui); }));
    CHECK(CountKind(p, OutLine::Out, "okay new"));

    sim.Type("open --bogus");                          // a problem (short and the same on every version: no version line)
    sim.Tap(Key_Enter, ui);
    CHECK(WaitIdle(p, [&] { sim.Frame(ui); }));
    CHECK(p.LastExitCode() == 2);
    CHECK(CountKind(p, OutLine::Err, "bogus"));

    sim.Press(Key_Up);                                 // Up brings back the last line
    sim.Frame(ui);
    CHECK(p.Input() == "open --bogus");
    sim.Press(Key_SelectAll);
    sim.Frame(ui);
    sim.Press(Key_Backspace);
    sim.Frame(ui);
    sim.Type("vc s");
    sim.Frame(ui);
    sim.Hold(UI::KeyId_Tab, true);                     // Tab completes from okay's command table
    sim.Frame(ui);
    sim.Hold(UI::KeyId_Tab, false);
    sim.Frames(4, ui);
    CHECK(p.Choices().size() > 1);
    CHECK(p.Input().rfind("vc s", 0) == 0);

    std::string why;
    const Bitmap& b = sim.Draw(18, 20, 26);
    Shot("command_line_panel", b);
    const bool same = MatchesGolden("command_line_panel", b, &why);
    if (!same) std::cerr << "  golden command_line_panel: " << why << "\n";
    CHECK(same);
}

} // namespace

int main() {
    std::error_code ec;
    const fs::path tmp = fs::temp_directory_path(ec) / ("okay_cli_panel_test_" +
        std::to_string((unsigned long long)std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(tmp, ec);
    TestModel(tmp);
    TestDrawn(tmp);
    fs::remove_all(tmp, ec);
    TEST_MAIN_RESULT();
}
