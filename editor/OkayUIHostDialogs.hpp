#pragma once
// The OkayUI implementation of oked::DialogService for window mode (docs/own_ui_toolkit.md 6.24 step 7): the error modal and the Save / Don't Save / Cancel
// question as OkayUI popups, no Dear ImGui. Same words and the same rules as the Dear ImGui editor's popups in main.cpp (one question at a time, a prefab open
// means the scene is put aside and replacing it is refused, a failed save keeps the question open and shows why). The editor state it needs comes in as
// callbacks, so it is tested without the engine. Written from the behaviour of main.cpp's ShowErrorPopup / RunAfterSavePrompt / DrawSavePrompt.
#include "DialogService.hpp"
#include "okay/UI/OkayUI.hpp"

namespace oked {
namespace wm {

/// What the save question asks the editor. Every callback may be empty (then: nothing unsaved, no prefab, not playing, save succeeds).
struct SaveContext {
    std::function<bool()> inPrefabMode;                    // a prefab is open (the scene is put aside)
    std::function<bool()> hasUnsavedChanges;
    std::function<std::string()> sceneLabel;               // "level1.okayscene", or "this new scene"
    std::function<bool()> isPlaying;
    std::function<void()> stopPlay;                        // stop the running game before saving (the edited scene is saved, not the running copy)
    std::function<bool(std::string& error)> save;          // save the scene (to its path, or the default one); false with the reason
};

class OkayUIDialogs : public DialogService {
public:
    SaveContext ctx;
    std::function<void(const std::string& path, int line)> openScript;   // null: the line is logged and nothing opens
    std::function<void(const std::string& line, int level)> log;         // the Console (0 info, 1 warning, 2 error); null: no logging

    void ShowError(const std::string& title, const std::string& text) override;
    void RunAfterSavePrompt(const std::string& doing, std::function<void()> action, std::function<void()> onCancel) override;
    void OpenScriptAt(const std::string& path, int line) override;

    /// Draw whichever popup is open. Call once a frame, inside the frame, after the windows.
    void Draw();

    bool ErrorOpen() const { return m_errOpen; }
    bool PromptOpen() const { return m_promptOpen; }
    /// Where the buttons were the last frame they were drawn (window pixels), for tests and tools.
    struct Probe { OkayUI::Rect errorOk, save, dontSave, cancel; std::string errorTitle, promptText, promptError; };
    const Probe& GetProbe() const { return m_probe; }

private:
    void DrawPopups();
    std::string m_errTitle, m_errText;
    bool m_errOpen = false;
    std::string m_what, m_promptError;
    std::function<void()> m_action, m_cancel;
    bool m_promptOpen = false;
    Probe m_probe;
};

} // namespace wm
} // namespace oked
