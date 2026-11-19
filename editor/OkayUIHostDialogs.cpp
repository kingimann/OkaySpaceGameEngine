// See OkayUIHostDialogs.hpp. No Dear ImGui.
#include "OkayUIHostDialogs.hpp"

#include <utility>

namespace oked {
namespace wm {

namespace {
const char* kErrName = "Something went wrong";
const char* kSaveName = "Save changes?";
}

void OkayUIDialogs::ShowError(const std::string& title, const std::string& text) {
    m_errTitle = title; m_errText = text; m_errOpen = true;
    if (log) log(title + ": " + text, 2);
}

void OkayUIDialogs::OpenScriptAt(const std::string& path, int line) {
    if (line < 1) line = 1;
    if (openScript) openScript(path, line);
    else if (log) log("Open Script (not available in window mode yet): " + path + ":" + std::to_string(line), 1);
}

void OkayUIDialogs::RunAfterSavePrompt(const std::string& doing, std::function<void()> action, std::function<void()> onCancel) {
    if (!action) return;
    if (ctx.inPrefabMode && ctx.inPrefabMode()) {   // a prefab is open: the scene is put aside, and replacing it would lose it
        ShowError("Go back to the scene first", "A prefab is open, so the scene is put aside. Use Back in the bar above the Scene view (or its Scene crumb), then try again (" + doing + ").");
        if (onCancel) onCancel();
        return;
    }
    if (m_promptOpen) { if (onCancel) onCancel(); return; }   // one question at a time
    if (!(ctx.hasUnsavedChanges && ctx.hasUnsavedChanges())) { action(); return; }
    m_what = doing; m_action = std::move(action); m_cancel = std::move(onCancel);
    m_promptError.clear(); m_promptOpen = true;
}

void OkayUIDialogs::Draw() {
    using namespace OkayUI;
    if (!m_errOpen && !m_promptOpen) return;
    // OkayUI popups need a layout to live in: a bare 1x1 window with nothing in it carries them (they draw in the overlay layer, over everything)
    SetNextWindowPos(Vec2{0.0f, 0.0f}, Cond_Always);
    SetNextWindowSize(Vec2{1.0f, 1.0f}, Cond_Always);
    if (Begin("##okayui_dialogs", nullptr, Window_NoTitleBar | Window_NoResize | Window_NoMove | Window_NoBackground | Window_NoScrollbar | Window_NoCollapse | Window_NoBringToFront | Window_NoFocusOnAppearing)) DrawPopups();
    End();
}

void OkayUIDialogs::DrawPopups() {
    using namespace OkayUI;
    const Input& in = GetFrameInput();
    const Vec2 centre{in.displayW * 0.5f, in.displayH * 0.5f};
    if (m_errOpen) {
        if (!IsPopupOpen(kErrName)) OpenPopup(kErrName);
        SetNextWindowPos(centre, Cond_Always, Vec2{0.5f, 0.5f});
        SetNextWindowSize(Vec2{520.0f, 0.0f}, Cond_Always);
        if (BeginPopupModal(kErrName, nullptr, Window_NoResize)) {
            PushStyleColor(Col_Text, 245, 140, 128);
            TextWrapped(m_errTitle.c_str());
            PopStyleColor();
            Dummy(Vec2{0.0f, 2.0f});
            TextWrapped(m_errText.c_str());
            Dummy(Vec2{0.0f, 6.0f});
            const bool ok = Button("OK", Vec2{110.0f, 0.0f});
            m_probe.errorOk = GetItemRect(); m_probe.errorTitle = m_errTitle;
            if (ok || IsKeyPressed(KeyId_Escape, false)) { m_errOpen = false; CloseCurrentPopup(); }
            EndPopup();
        } else m_errOpen = false;   // OkayUI closed it itself (Escape)
        return;   // the error first; a question waiting behind it is asked next
    }
    if (!m_promptOpen) return;
    if (!IsPopupOpen(kSaveName)) OpenPopup(kSaveName);
    SetNextWindowPos(centre, Cond_Always, Vec2{0.5f, 0.5f});
    SetNextWindowSize(Vec2{480.0f, 0.0f}, Cond_Always);
    if (!BeginPopupModal(kSaveName, nullptr, Window_NoResize)) {   // OkayUI closed it itself (Escape): that is Cancel
        auto cancel = std::move(m_cancel); m_action = nullptr; m_cancel = nullptr; m_promptOpen = false;
        if (cancel) cancel();
        return;
    }
    const std::string label = ctx.sceneLabel ? ctx.sceneLabel() : std::string("this new scene");
    m_probe.promptText = "Save changes to \"" + label + "\" before " + m_what + "?";
    TextWrapped(m_probe.promptText.c_str());
    Dummy(Vec2{0.0f, 2.0f});
    TextDisabled("If you don't save, the changes you made to this scene are lost.");
    m_probe.promptError = m_promptError;
    if (!m_promptError.empty()) {
        Dummy(Vec2{0.0f, 4.0f});
        PushStyleColor(Col_Text, 245, 140, 128);
        const std::string msg = "Couldn't save: " + m_promptError;
        TextWrapped(msg.c_str());
        PopStyleColor();
    }
    Dummy(Vec2{0.0f, 8.0f});
    auto finish = [&](bool run) {
        auto act = std::move(m_action); auto cancel = std::move(m_cancel);
        m_action = nullptr; m_cancel = nullptr; m_promptOpen = false;
        CloseCurrentPopup();
        if (run && act) act();
        else if (!run && cancel) cancel();
    };
    const bool save = Button("Save", Vec2{120.0f, 0.0f});
    m_probe.save = GetItemRect();
    if (save) {
        if (ctx.isPlaying && ctx.isPlaying() && ctx.stopPlay) ctx.stopPlay();   // save the edited scene, not the running copy
        std::string err;
        if (!ctx.save || ctx.save(err)) finish(true);
        else m_promptError = err.empty() ? "unknown error" : err;               // stay open: choose again
    }
    SameLine();
    const bool dont = Button("Don't Save", Vec2{120.0f, 0.0f});
    m_probe.dontSave = GetItemRect();
    if (dont) finish(true);
    SameLine();
    const bool cancel = Button("Cancel", Vec2{120.0f, 0.0f});
    m_probe.cancel = GetItemRect();
    if (cancel || IsKeyPressed(KeyId_Escape, false)) finish(false);
    EndPopup();
}

} // namespace wm
} // namespace oked
