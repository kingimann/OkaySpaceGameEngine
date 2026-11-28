#pragma once
// Automation on the editor's state: the host the automation commands (okay/Automation/Automation.hpp) run in, the
// commands that need the editor (scenes, objects and the selection, undo and redo, Play and runtime testing, modeling,
// UI widgets, visual script graphs), and the editor's local automation link (okay/Automation/Link.hpp).
//
// One host for both places: `okay mcp` / `okay automation` make an EditorState with no window and wrap it in an
// EditorHost ("headless"); the running editor wraps its own EditorState ("editor"), so an agent works on exactly what
// the person sees and every edit lands in the editor's own Undo history. ImGui-free, so it is tested headless
// (tests/test_automation.cpp). Our own code.
#include "EditorState.hpp"
#include "okay/Automation/Automation.hpp"

#include <string>

namespace oked::automation {

class EditorHost : public okay::automation::Host {
public:
    explicit EditorHost(okay::editor::EditorState& ed, std::string kind = "editor") : m_ed(ed), m_kind(std::move(kind)) {}
    okay::editor::EditorState& ed() { return m_ed; }

    okay::Scene* scene() override { return &m_ed.scene(); }
    std::string projectDir() const override { return m_ed.projectDir(); }
    std::string kind() const override { return m_kind; }
    bool hasEditor() const override { return true; }
    bool isPlaying() const override { return m_ed.isPlaying(); }
    /// The editor's own undo: PushUndo opens the step, CommitUndo closes it, so one command is one entry in Edit > Undo.
    void BeginEdit(const std::string& name) override;
    void EndEdit(bool ok) override;
    bool EditorView(float eye[3], float target[3], bool& orthographic, float& size) const override;

private:
    okay::editor::EditorState& m_ed;
    std::string m_kind;
    std::uint64_t m_revision = 0;
    int m_depth = 0;
};

/// Hand the editor's commands to the automation table (once; later calls do nothing). The hosts call it before they
/// first use okay::automation::Commands().
void RegisterEditorCommands();

/// The visual script graph commands (AutomationVs.cpp), added by RegisterEditorCommands.
void AddVsCommands(okay::automation::Registry& r);

/// The editor state behind a command's host (null for a host that is not an EditorHost).
okay::editor::EditorState* EditorOf(okay::automation::Call& c);
using EditorHandler = std::function<okay::automation::Result(okay::automation::Call&, okay::editor::EditorState&)>;
/// A command that needs the editor: the NeedsEditor flag added, the handler handed the EditorState.
okay::automation::Command EditorCommand(const char* name, const char* summary, std::vector<okay::automation::Param> params,
                                        const char* result, unsigned flags, EditorHandler handler,
                                        std::vector<std::string> errors = {}, const char* undoName = "");
/// A file a command names: as given when it exists, else inside the project, else inside its Assets folder; for a name
/// found nowhere, `fallbackInAssets` gives the path it would have inside Assets (a file to create), else it is returned as given.
std::string ProjectFile(const okay::editor::EditorState& ed, const std::string& given, bool fallbackInAssets = false);

// ---- The running editor's automation link (View > Automation Link) -----------------------------------------------------

/// The menu toggle's flag. Read from (and written back to) the editor's per-user file automation_link.txt the first time
/// it is used, so the choice is remembered; off by default.
bool& LinkFlag();
/// Once a frame on the main thread: start or stop the link to follow LinkFlag(), then answer what clients sent (each
/// line is run through an McpServer on `ed`). Never waits.
void LinkFrame(okay::editor::EditorState& ed);
/// The link itself, without the menu flag: `on` starts it (once) and answers clients, off stops it. False when it is not
/// running (off, or it could not start: the log says why). `kind` is what automation.host reports ("editor").
/// LinkFrame calls it; `okay automation link` runs it on a windowless editor state.
bool LinkPoll(okay::editor::EditorState& ed, bool on, const std::string& kind = "editor");
/// "Automation link on (port 51234, 1 client)" / "" when off: for the status bar or the menu tip.
std::string LinkStatus();
/// Stop the link now (the editor is closing).
void LinkShutdown();

} // namespace oked::automation
