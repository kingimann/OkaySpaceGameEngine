#pragma once
// Command Line panel (system "Command line (okay)", docs/feature_map/17-launcher-platform.md): okay, the engine's command
// line, typed into a window of a program instead of a terminal. ONE panel for every host:
//   * the editor: View > Panels > Command Line, a dockable window (editor/OkayPanels.cpp DrawCommandLineOkayUI);
//   * any other OkayUI host that links okay_cli_panel (the launcher keeps its own Command line page for now; it runs the
//     okay program and shares the typed-line rules below).
// The commands run inside this program through okay's own code (tools/okay-cli/CommandJob.hpp, the same okaycli::Main the
// okay program runs): only okay's commands, never a shell and never system(). A command runs on a worker thread and can be
// stopped. The project folder is the working folder (okay's --project). History (Up / Down), Tab completion of command and
// sub-command names (okay's command table), colours for problems, Copy (every line) and Clear come from
// tools/okay-cli/CommandInput.hpp, which the launcher's Command line page uses too. Drawn with OkayUI only (no Dear ImGui).
// Our own code.
#include "CommandInput.hpp"
#include "CommandJob.hpp"

#include <functional>
#include <string>
#include <vector>

namespace okaycli {

class CommandLinePanel {
public:
    struct Config {
        std::string idPrefix = "##cmdline";   ///< prefix of the OkayUI names (unique in the host's frame)
        /// Copy goes here (the host's clipboard: OkayUI's own holds 4 KB at most). Null: OkayUI::SetClipboardText.
        std::function<void(const std::string& text)> copy;
        std::size_t maxLines = 5000;
    };

    CommandLinePanel();
    explicit CommandLinePanel(Config config);

    /// The project the commands run in ("" = this program's own folder). Said once on the panel when it changes.
    void SetFolder(const std::string& folder);
    const std::string& Folder() const { return folder_; }
    /// The folder's own name ("My Game"), as the panel shows it (the whole path is its tooltip).
    std::string FolderName() const;

    // ---- what a person does (the drawing calls these; tests call them directly) ----
    /// Run a typed line: `clear` / `cls` empties the panel, a leading "okay" is dropped, anything else is an okay command.
    /// Added to the history. False (with a note) when nothing was run: an empty line, an unclosed quote, a command still running.
    bool Submit(const std::string& line);
    /// Read what the running command said since the last call. Once a frame (Frame calls it).
    void Pump();
    void Stop();
    bool Running() const { return job_.Running(); }
    void Clear() { log_.Clear(); }
    /// Copy every line (to Config::copy, else OkayUI's clipboard).
    void Copy();
    /// Up / Down in the input line: the older / newer line run before. False when there is none.
    bool HistoryUp();
    bool HistoryDown();
    /// Tab: complete the input line's last word from okay's command table. Several fits are listed (Choices).
    void CompleteInput();
    const std::string& Input() const { return input_; }
    void SetInput(const std::string& text);
    const std::vector<std::string>& Choices() const { return choices_; }
    /// A line from the host (a hint, why a command could not run).
    void Note(const std::string& text) { log_.Add(text, OutLine::Note); }

    const OutputLog& Log() const { return log_; }
    const History& GetHistory() const { return history_; }
    /// The exit code of the last command that finished (-1 before any).
    int LastExitCode() const { return lastExit_; }

    // ---- drawing ----
    /// Draw the panel in (x, y, w, h) and act on what the person did (calls Pump). Between OkayUI::BeginFrame and EndFrame.
    void Frame(float x, float y, float w, float h);

    /// Called by the input line's OkayUI callback (Up, Down, Tab). Not for hosts.
    int OnInputKey(int key, char* buf, int cap, int& len);

private:
    Config cfg_;
    CommandJob job_;
    OutputLog log_;
    History history_;
    std::string folder_, input_;
    std::vector<std::string> choices_;
    char buf_[1024] = {};
    unsigned seenRevision_ = 0;
    int follow_ = 0;           ///< frames left to keep the output scrolled to the end
    bool focusInput_ = true;   ///< give the input line the keyboard next frame
    int lastExit_ = -1;
};

} // namespace okaycli
