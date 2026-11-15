#pragma once
// Editor windows hosted by OkayUI (the opt-in "OkayUI (preview)" UI backend, docs/own_ui_toolkit.md section 6): the Crash Log and the
// Audio Mixer. Each keeps its Dear ImGui window (the dock tab, title and close box) and hands the free area to the OkayUI screen in
// okayui/screens/OkayUIPanels. Only compiled into the editor when OkayUI is (OKAY_HAVE_OKAYUI).
#include <cstdint>
#include <functional>
#include <string>

#ifdef OKAY_HAVE_OKAYUI
#include "okay/UI/OkayUI.hpp"
#include "okay/UI/OkayUICodeEdit.hpp"
#endif

namespace oked {

#ifdef OKAY_HAVE_OKAYUI
/// While one of these is alive, every OkayUI widget is drawn in the editor's current look (Dark or Light: neutral greys and the
/// one neutral accent, editor/ThemeModel.hpp) instead of the launcher / game look the OkayUI theme otherwise holds. The Game
/// view's game UI is drawn outside any scope, so it keeps looking exactly as the player draws it. Put one around the OkayUI
/// calls of an editor panel (the Console, Crash Log, Audio Mixer, the script editor's code area).
class OkayUILookScope {
public:
    OkayUILookScope();
    ~OkayUILookScope();
    OkayUILookScope(const OkayUILookScope&) = delete;
    OkayUILookScope& operator=(const OkayUILookScope&) = delete;
private:
    OkayUI::Theme saved_;
};
/// The script editor palette of the current look for OkayUI::CodeEdit (the same numbers the ImGui text editor gets).
OkayUI::CodePalette EditorCodePalette();
#endif

struct CrashLogActions {
    std::function<void()> refresh, openFolder, clear;   // the host reloads, reveals the folder, deletes the files
};

/// Draw the Crash Log window with OkayUI. Returns false, drawing nothing, when an ImGui popup covers the window this frame (OkayUI
/// draws over every ImGui window, so the caller draws the Dear ImGui version then). `revision` goes up whenever `text` changed.
bool DrawCrashLogOkayUI(bool* open, const std::string& text, const std::string& path, std::uint64_t revision, const CrashLogActions& actions);

/// Draw the Audio Mixer window with OkayUI, editing okay::AudioMixer in place. Same return value as above.
bool DrawAudioMixerOkayUI(bool* open);

} // namespace oked
