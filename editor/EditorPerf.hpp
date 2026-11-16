#pragma once
// Editor frame-time maths and settings (feature map 15.11): the logic behind View > Rendering & Performance and the
// frame-time overlay. No ImGui, no SDL and no globals here, so it is tested headless (tests/test_editor_perf.cpp).
//
// Written from the plain definitions: frame rate is 1000 / milliseconds, a percentile is read off a sorted list, and
// a frame limiter sleeps out the rest of a frame budget. It reads okay::Profiler::Frame (the editor's own profiler)
// and reuses okay::FrameStats (Core/FrameStats.hpp) for average / percentile / 1% low; nothing is copied from
// another project.
//
//   const perf::Breakdown b = perf::Break(Prof().Displayed());          // where the frame went, in ms, adding up to the total
//   const perf::HistoryStats s = perf::Stats(frameMsList);              // fps, average, 95th percentile, worst, 1% low
//   const double fps = perf::TargetFps(settings, playing, idleSeconds); // 0 = run flat out
//
// Windows build rule (CLAUDE.md 21a): only <algorithm>, <cmath>, <cstdio>, <string> and <vector> here.
#include "okay/Core/Profiler.hpp"
#include "okay/Core/FrameStats.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace okay::editor::perf {

// ---------------------------------------------------------------- where the frame went

enum class Section {
    Viewport3D = 0,   // the 3D view render (the GPU draw, or the software raster, and a read-back when there is one)
    ViewportUpload,   // putting a CPU-rendered picture into the texture the panel shows
    ViewportUI,       // the viewport panel's own work: gizmos, overlays, picking, the camera
    Panels,           // every other panel: Hierarchy, Inspector, Project, Console, the window menus ...
    EditorUpdate,     // the editor's own per-frame work outside the panels: input handling, the scene tick, services, autosave
    ImGuiDraw,        // Dear ImGui turning the frame into draw lists and submitting them
    Present,          // SDL_RenderPresent: the swap, the GPU wait and the vsync wait
    Play,             // the game running in Play mode: scripts, physics, animation, audio, game rendering
    Sleep,            // the frame limiter / idle throttle sleeping on purpose
    Untimed,          // what no scope covers: reading events, window work, waits between scopes
    Count
};

inline const char* SectionName(Section s) {
    switch (s) {
        case Section::Viewport3D:     return "3D view render (GPU draw or CPU raster)";
        case Section::ViewportUpload: return "3D view upload (CPU picture to texture)";
        case Section::ViewportUI:     return "Viewport gizmos and overlays";
        case Section::Panels:         return "Panels (Hierarchy, Inspector, Project ...)";
        case Section::EditorUpdate:   return "Editor update (input, scene tick, services)";
        case Section::ImGuiDraw:      return "ImGui draw lists and submit";
        case Section::Present:        return "Present (swap, GPU wait, vsync)";
        case Section::Play:           return "Play mode (scripts, physics, animation)";
        case Section::Sleep:          return "Frame limiter sleep (on purpose)";
        case Section::Untimed:        return "Not timed (events, waits between scopes)";
        default:                      return "";
    }
}

inline bool StartsWith(const std::string& s, const char* p) { return s.compare(0, std::strlen(p), p) == 0; }

/// Which section a profiler scope name belongs to. The editor's own scopes: "Render3D", "Render3D.Upload",
/// "UI.Viewport" / "UI.GameView", "UI.<panel>", "Editor.Update", "ImGui.NewFrame", "ImGui.Draw", "ImGui.Render", "Present", "Pacing.Sleep"; the engine's
/// Play scopes (Scripts, Physics, Animation, Audio, Render.* ...) are everything else.
inline Section Classify(const std::string& name) {
    if (name == "Render3D") return Section::Viewport3D;
    if (name == "Render3D.Upload") return Section::ViewportUpload;
    if (name == "UI.Viewport" || name == "UI.GameView") return Section::ViewportUI;
    if (StartsWith(name, "UI.")) return Section::Panels;
    if (name == "ImGui.NewFrame") return Section::EditorUpdate;
    if (StartsWith(name, "Editor.")) return Section::EditorUpdate;
    if (StartsWith(name, "ImGui.")) return Section::ImGuiDraw;
    if (name == "Present") return Section::Present;
    if (name == "Pacing.Sleep") return Section::Sleep;
    return Section::Play;
}

struct Breakdown {
    double ms[(int)Section::Count] = {};
    double total = 0.0;   // the frame's wall time
    double Share(Section s) const { return total > 0.0 ? ms[(int)s] / total : 0.0; }
};

/// Split one profiler frame into sections by SELF time (time in a scope, not in the scopes inside it), so the parts
/// add up to the frame total and nothing is counted twice; what no scope covers goes to Untimed.
inline Breakdown Break(const Profiler::Frame& f) {
    Breakdown b;
    b.total = f.total > 0.0 ? f.total : 0.0;
    double covered = 0.0;
    for (const Profiler::Bucket& k : f.buckets) {
        const double s = k.self > 0.0 ? k.self : 0.0;
        b.ms[(int)Classify(k.name)] += s;
        covered += s;
    }
    b.ms[(int)Section::Untimed] += std::max(0.0, b.total - covered);
    return b;
}

// ---------------------------------------------------------------- numbers over the last frames

struct HistoryStats {
    std::size_t frames = 0;
    double avgMs = 0.0, p95Ms = 0.0, worstMs = 0.0, bestMs = 0.0;
    double fps = 0.0;        // 1000 / average
    double lowFps = 0.0;     // the "1% low": 1000 / the 99th percentile frame time
};

/// Frame-time numbers over `frameMs` (oldest first; non-finite or <= 0 entries are skipped), worked out by
/// okay::FrameStats (Core/FrameStats.hpp, the rolling window the game loop uses) so the editor and a running game agree.
inline HistoryStats Stats(const std::vector<double>& frameMs) {
    FrameStats fs(std::max<std::size_t>(1, frameMs.size()));
    for (double d : frameMs) if (std::isfinite(d) && d > 0.0) fs.Push(d);
    const FrameStatsSnapshot z = fs.Snapshot();
    HistoryStats h;
    h.frames = z.frames;
    h.avgMs = z.avgMs; h.p95Ms = z.p95Ms; h.worstMs = z.worstMs; h.bestMs = z.minMs;
    h.fps = z.fps; h.lowFps = z.low1PercentFps;
    return h;
}

/// The plain-text report the overlay's Copy button puts on the clipboard: what to send back.
inline std::string Report(const HistoryStats& s, const Breakdown& b, const char* renderer, bool playing, int viewW, int viewH,
                          float renderScale, bool perfMode, int fpsCap, bool idleThrottle) {
    char buf[256];
    std::string r = "OkaySpace editor frame-time report\n";
    std::snprintf(buf, sizeof(buf), "Renderer: %s\nPlaying: %s\nViewport: %dx%d at render scale %.2f, performance mode %s\n"
                  "Frame limiter: %s, idle throttle: %s\n",
                  renderer && *renderer ? renderer : "unknown", playing ? "yes" : "no", viewW, viewH, (double)renderScale,
                  perfMode ? "on" : "off", fpsCap > 0 ? (std::to_string(fpsCap) + " fps").c_str() : "off", idleThrottle ? "on" : "off");
    r += buf;
    std::snprintf(buf, sizeof(buf), "Last %zu frames: %.0f FPS (average %.2f ms), 95th percentile %.2f ms, worst %.2f ms, 1%% low %.0f FPS\n",
                  s.frames, s.fps, s.avgMs, s.p95Ms, s.worstMs, s.lowFps);
    r += buf;
    r += "Newest frame, by section (ms, share of the frame):\n";
    for (int i = 0; i < (int)Section::Count; ++i) {
        std::snprintf(buf, sizeof(buf), "  %-46s %7.2f ms  %5.1f%%\n", SectionName((Section)i), b.ms[i], b.Share((Section)i) * 100.0);
        r += buf;
    }
    return r;
}

// ---------------------------------------------------------------- frame pacing

struct PacingSettings {
    int    fpsCap = 0;              // 0 = no cap
    bool   idleThrottle = false;    // slow down while nothing is happening
    int    idleFps = 30;            // the rate while idle
    double idleAfterSeconds = 0.75; // how long without a click, key or scroll counts as idle
};

/// The frame rate to hold this frame, 0 = do not sleep. Idle throttle applies only when nothing is playing and the
/// last input was `secondsSinceInput` ago; the lower of the cap and the idle rate wins.
inline double TargetFps(const PacingSettings& p, bool playing, bool busy, double secondsSinceInput) {
    double target = p.fpsCap > 0 ? (double)p.fpsCap : 0.0;
    const bool idle = p.idleThrottle && !playing && !busy && std::isfinite(secondsSinceInput) &&
                      secondsSinceInput >= p.idleAfterSeconds;
    if (idle) {
        const double idleFps = (double)std::clamp(p.idleFps, 5, 240);
        target = target > 0.0 ? std::min(target, idleFps) : idleFps;
    }
    return target;
}

/// Seconds to sleep so a frame that took `elapsedSeconds` lasts 1 / target. 0 when there is no target or the frame ran long.
inline double SleepSeconds(double targetFps, double elapsedSeconds) {
    if (!(targetFps > 0.0) || !std::isfinite(targetFps)) return 0.0;
    const double budget = 1.0 / targetFps;
    return elapsedSeconds < budget ? budget - elapsedSeconds : 0.0;
}

// ---------------------------------------------------------------- viewport cost: scale and "performance mode"

/// The scale the 3D view is rendered at: the user's slider, never above 1, never below 0.25, and (performance mode)
/// capped at 0.75 so the panel is drawn from a smaller picture. A non-number counts as 1.
inline float EffectiveScale(float userScale, bool performanceMode) {
    float s = std::isfinite(userScale) ? userScale : 1.0f;
    s = std::clamp(s, 0.25f, 1.0f);
    if (performanceMode) s = std::min(s, 0.75f);
    return s;
}

/// The size of the picture rendered for a panel of w x h: the scale applied and the long edge held to maxEdge. Never 0.
inline void RenderSize(int w, int h, float scale, int maxEdge, int& outW, int& outH) {
    float fw = std::max(1.0f, (float)w * scale), fh = std::max(1.0f, (float)h * scale);
    const float longEdge = std::max(fw, fh);
    if (maxEdge > 0 && longEdge > (float)maxEdge) { const float k = (float)maxEdge / longEdge; fw = std::max(1.0f, fw * k); fh = std::max(1.0f, fh * k); }
    outW = (int)fw; outH = (int)fh;
}

/// The renderer switches that cost the most in the Scene view.
struct ViewKnobs {
    bool shadows = true;
    int  shadowResolution = 1024;
    int  shadowCascades = 3;
    bool ssao = false, bloom = false, fxaa = false;
    int  supersample = 1;   // 1 = off, 2 = 2x2
};

/// The same switches made cheaper, for the editor's Scene view only (the Game view and a built game keep the scene's
/// own look): one shadow cascade at no more than 1024 texels, no ambient occlusion, bloom or FXAA, no supersampling.
/// Nothing is ever made MORE expensive, and a switch that is already off or low stays as it is.
/// `changeShadowSize` false leaves the shadow map size and cascade count alone: the Direct3D renderer reallocates its shadow
/// texture whenever the size changes, so when the Scene view and the Game view render in the same frame (different
/// sizes would make it reallocate twice a frame) only the cheap switches are changed.
inline ViewKnobs Cheaper(const ViewKnobs& k, bool changeShadowSize = true) {
    ViewKnobs c = k;
    if (changeShadowSize) {
        c.shadowResolution = std::min(k.shadowResolution, 1024);
        c.shadowCascades = std::min(k.shadowCascades, 1);
    }
    c.ssao = false; c.bloom = false; c.fxaa = false;
    c.supersample = 1;
    return c;
}

} // namespace okay::editor::perf
