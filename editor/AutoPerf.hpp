// "Auto" for the Rendering & Performance settings: pick the best value for this PC, and say what was picked.
// Pure logic (no SDL, no ImGui) so tests/test_menu_bar_model.cpp can check it. Written from our own rules, not from another project.
#pragma once
#include <string>

namespace oked::autoperf {

/// What the editor knows about this PC. Zero means "unknown".
struct Hardware {
    bool gpuRenderer = false;   // the 3D view can be drawn by the GPU (an accelerated renderer exists)
    int  cpuCores = 0;
    int  ramMB = 0;
    int  displayHz = 0;
};

/// A setting's mode, saved in okay_settings.txt: 0 = Auto (the default), 1 = forced off, 2 = forced on.
enum Mode { Auto = 0, ForceOff = 1, ForceOn = 2 };
inline int ClampMode(int v) { return v < 0 || v > 2 ? Auto : v; }

/// 3D anti-aliasing (2x supersampling is costly): on only with a GPU, 4+ cores and 8 GB or more of memory.
/// Unknown cores or memory count as "not enough": Auto errs towards a smooth editor.
inline bool AutoAntiAliasing(const Hardware& h) { return h.gpuRenderer && h.cpuCores >= 4 && h.ramMB >= 8000; }

/// VSync: on when the GPU draws (no tearing, less heat); off for the software renderer, which would be pinned to the refresh rate.
inline bool AutoVSync(const Hardware& h) { return h.gpuRenderer; }

inline bool Resolve(int mode, bool autoValue) { mode = ClampMode(mode); return mode == Auto ? autoValue : mode == ForceOn; }

/// The menu text: "Auto (On)" / "Auto (Off)" for Auto, "On" / "Off" when the user chose.
inline std::string Label(int mode, bool effective) {
    mode = ClampMode(mode);
    const char* v = effective ? "On" : "Off";
    return mode == Auto ? std::string("Auto (") + v + ")" : std::string(v);
}

}  // namespace oked::autoperf
