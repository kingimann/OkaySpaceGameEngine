#pragma once
// Editor windows hosted by OkayUI (the opt-in "OkayUI (preview)" UI backend, docs/own_ui_toolkit.md sections 6 and 9): the Crash Log, the
// Audio Mixer, and the Problems, Stats and History panels. Each keeps its Dear ImGui window (the dock tab, title and close box) and hands the free area to the OkayUI screen in
// okayui/screens/OkayUIPanels. Only compiled into the editor when OkayUI is (OKAY_HAVE_OKAYUI).
#include "EditorPanelModels.hpp"

#include <cstdint>
#include <functional>
#include <string>

namespace oked {

struct CrashLogActions {
    std::function<void()> refresh, openFolder, clear;   // the host reloads, reveals the folder, deletes the files
};

/// Draw the Crash Log window with OkayUI. Returns false, drawing nothing, when an ImGui popup covers the window this frame (OkayUI
/// draws over every ImGui window, so the caller draws the Dear ImGui version then). `revision` goes up whenever `text` changed.
bool DrawCrashLogOkayUI(bool* open, const std::string& text, const std::string& path, std::uint64_t revision, const CrashLogActions& actions);

/// Draw the Audio Mixer window with OkayUI, editing okay::AudioMixer in place. Same return value as above.
bool DrawAudioMixerOkayUI(bool* open);

/// The Problems, Stats and History panels with OkayUI (okayui/screens/OkayUIEditorPanels): the same models the Dear ImGui bodies in
/// EditorPanelViews draw, the same intents back. Same return value as above (false: draw the Dear ImGui version this frame). `title`
/// is the ImGui window name (Problems carries its error count in it).
bool DrawProblemsOkayUI(const char* title, bool* open, const okaypanels::ProblemsModel& model, okaypanels::ProblemsState& state,
                        okaypanels::ProblemsIntent& intent);
/// Stats fills its model only while the window is visible, through `fill`; "Copy stats" goes to the clipboard here.
bool DrawStatsOkayUI(bool* open, const std::function<void(okaypanels::StatsModel&)>& fill);
bool DrawHistoryOkayUI(bool* open, const okaypanels::HistoryModel& model, okaypanels::HistoryIntent& intent);

} // namespace oked
