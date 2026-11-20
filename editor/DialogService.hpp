#pragma once
// The three "ask or tell the person" calls the editor's panels make, behind one interface (docs/ui/own_ui_toolkit.md 6.24 step 7): show an error they must read, ask
// "Save changes before ...?" before anything replaces the open scene, and open a script file at a line. The Dear ImGui editor implements it with its popups in
// main.cpp; window mode implements it with OkayUI popups (editor/OkayUIHostDialogs.cpp). Plain C++, no UI types, so panel code written once can call either.
// A host sets the service it runs with; one process runs one editor, so there is one current service.
#include <functional>
#include <string>

namespace oked {

class DialogService {
public:
    virtual ~DialogService() {}
    /// A modal with the problem in plain words and the reason (also logged to the Console).
    virtual void ShowError(const std::string& title, const std::string& text) = 0;
    /// Run `action` now when nothing is unsaved; otherwise ask Save / Don't Save / Cancel first. `doing` completes "before ...": "opening another scene".
    /// `onCancel` runs when the person cancels (or the question could not be asked).
    virtual void RunAfterSavePrompt(const std::string& doing, std::function<void()> action, std::function<void()> onCancel) = 0;
    /// Open a script file in the editor's script window and jump to `line` (1 = top).
    virtual void OpenScriptAt(const std::string& path, int line) = 0;
};

inline DialogService*& DialogSlot() { static DialogService* d = nullptr; return d; }
/// The service the running editor uses; null until a host sets one.
inline DialogService* CurrentDialogs() { return DialogSlot(); }
inline void SetDialogs(DialogService* d) { DialogSlot() = d; }

} // namespace oked
