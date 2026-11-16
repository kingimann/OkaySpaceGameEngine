#pragma once
// Editor windows hosted by OkayUI (the opt-in "OkayUI (preview)" UI backend, docs/own_ui_toolkit.md section 6): the Crash Log, the
// Audio Mixer and the Stats window. Each keeps its Dear ImGui window (the dock tab, title and close box) and hands the free area to the OkayUI screen in
// okayui/screens/OkayUIPanels. Only compiled into the editor when OkayUI is (OKAY_HAVE_OKAYUI).
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace oked {

struct CrashLogActions {
    std::function<void()> refresh, openFolder, clear;   // the host reloads, reveals the folder, deletes the files
};

/// Draw the Crash Log window with OkayUI. Returns false, drawing nothing, when an ImGui popup covers the window this frame (OkayUI
/// draws over every ImGui window, so the caller draws the Dear ImGui version then). `revision` goes up whenever `text` changed.
bool DrawCrashLogOkayUI(bool* open, const std::string& text, const std::string& path, std::uint64_t revision, const CrashLogActions& actions);

/// Draw the Audio Mixer window with OkayUI, editing okay::AudioMixer in place. Same return value as above.
bool DrawAudioMixerOkayUI(bool* open);

/// What the Stats window shows (the Dear ImGui window reads the same data): the frame rate and its history ring (zeros are unused), the scene
/// counts, and the host's culling and batching switches, edited in place.
struct StatsHostData {
    float framerate = 0.0f;
    const float* history = nullptr; int historyCount = 0; int historyOffset = 0;
    std::vector<std::pair<std::string, int>> rows;
    bool* frustumCulling = nullptr; bool* occlusionCulling = nullptr; bool* staticBatching = nullptr; bool* dynamicBatching = nullptr;
    std::string skinning = "CPU";
};

/// Draw the Stats window with OkayUI. Same return value as above.
bool DrawStatsOkayUI(bool* open, const StatsHostData& data);

} // namespace oked
