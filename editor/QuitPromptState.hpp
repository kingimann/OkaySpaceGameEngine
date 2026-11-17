#pragma once
// The "Unsaved Changes" quit prompt's state, one step per frame (ImGui-free so tests can run it).
// Issue #888 finding 142: a quit that arrived while another modal was open opened the prompt where it could
// not show (or the other modal took it down), so `open` stayed true and every later Exit or close was ignored.
// Now `open` follows whether the prompt really shows, and a quit request waits while another modal is up.

namespace oked {

struct QuitPromptState {
    bool open = false;   // the prompt was opened and has not been answered
};

enum class QuitStep { None, QuitNow, OpenPrompt };

/// One frame. `quitRequested`: File > Exit, the close button or SIGTERM asked to quit (it stays set until it
/// is answered). `unsavedWork`: something is unsaved. `promptShowing`: the "Unsaved Changes" popup really is
/// open. `otherModalOpen`: a different modal is up.
inline QuitStep StepQuitPrompt(QuitPromptState& s, bool quitRequested, bool unsavedWork, bool promptShowing,
                               bool otherModalOpen) {
    // The prompt was opened but is gone without an answer (another modal closed it, or it never showed):
    // forget it, so the pending request is shown again below.
    if (s.open && !promptShowing) s.open = false;
    if (!quitRequested || s.open) return QuitStep::None;
    if (!unsavedWork) return QuitStep::QuitNow;
    if (otherModalOpen) return QuitStep::None;   // wait: show the prompt once that modal closes
    s.open = true;
    return QuitStep::OpenPrompt;
}

/// The prompt was answered (Save, Cancel or Don't Save).
inline void QuitPromptAnswered(QuitPromptState& s) { s.open = false; }

}  // namespace oked
