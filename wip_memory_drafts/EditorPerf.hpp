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
// Windows build rule (CLAUDE.md 21a): only <algorithm>, <cmath>, <cstdio>, <cstdlib>, <cstring>, <string> and <vector> here.
#include "okay/Core/Profiler.hpp"
#include "okay/Core/FrameStats.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
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
    if (name == "Present" || StartsWith(name, "Present.")) return Section::Present;   // Present.Windows: the multi-window panels' own presents
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

/// The editor starts with the idle throttle ON (rule: nothing off by default). Settings files from before saved "idlethrottle 0"
/// for everyone, chosen or not, so that old key is not read; the person's own choice is saved as "idle_throttle".
constexpr bool kIdleThrottleDefault = true;

/// The rate the editor holds while its main window is minimized and nothing is playing, whatever the settings: nobody can see a
/// frame, and a minimized (occluded) window's present returns at once instead of waiting for the display, so without this the
/// loop ran flat out on a full core.
constexpr double kMinimizedFps = 10.0;

/// The frame rate to hold this frame, 0 = do not sleep. Idle throttle applies only when nothing is playing and the
/// last input was `secondsSinceInput` ago; the lower of the cap and the idle rate wins. A minimized window (not playing) holds
/// kMinimizedFps at most.
inline double TargetFps(const PacingSettings& p, bool playing, bool busy, double secondsSinceInput, bool minimized = false) {
    double target = p.fpsCap > 0 ? (double)p.fpsCap : 0.0;
    if (minimized && !playing) target = target > 0.0 ? std::min(target, kMinimizedFps) : kMinimizedFps;
    const bool idle = p.idleThrottle && !playing && !busy && std::isfinite(secondsSinceInput) &&
                      secondsSinceInput >= p.idleAfterSeconds;
    if (idle) {
        const double idleFps = (double)std::clamp(p.idleFps, 5, 240);
        target = target > 0.0 ? std::min(target, idleFps) : idleFps;
    }
    return target;
}

/// What keeps the editor at full speed with no input, so the idle throttle never slows something that moves on its own.
/// Input itself is the other wake: any event resets the quiet time (QuietSeconds), and the throttle only lowers the frame
/// rate (every timer, file check, network and job result is still looked at each frame, just less often).
struct IdleWake {
    bool playing = false;       // Play mode, also while frozen at a breakpoint
    bool pointerHeld = false;   // a mouse button is down (a drag, a held slider)
    bool preview = false;       // an animation, timeline or model preview plays in a view
    bool debugger = false;      // the visual-script debugger is on (breakpoints, stepping)
    bool splash = false;        // the start-up loading screen or its fade
};
inline bool Awake(const IdleWake& w) { return w.pointerHeld || w.preview || w.debugger || w.splash; }   // (playing has its own rule)
/// TargetFps with the wake reasons above.
inline double TargetFps(const PacingSettings& p, const IdleWake& w, double quietSeconds, bool minimized = false) {
    return TargetFps(p, w.playing, Awake(w), quietSeconds, minimized);
}
/// The quiet time the throttle reads: since the last input or since the last sign of activity (a new Console line: build
/// output, a download, an import or a file reload reporting), whichever is newer. A non-number counts as "just now".
inline double QuietSeconds(double sinceInput, double sinceActivity) {
    if (!std::isfinite(sinceInput) || !std::isfinite(sinceActivity)) return 0.0;
    return std::max(0.0, std::min(sinceInput, sinceActivity));
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

// ---------------------------------------------------------------- the slow-frame log

/// The slow-frame log: one line for every editor frame that took at least `thresholdMs`, written to okay_editor_perf.log next to
/// the editor (the main loop does the writing). It names the part of the frame that took the time, so a freeze someone reports from
/// a Windows release can be traced without a debugger. Made from the profiler's frame by self time (Break above), nothing else.
struct SlowFrameLog {
    double thresholdMs = 250.0;   // 0 or less: off
    int maxLines = 300;           // per run, so a machine that is slow all the time does not fill the disk
    int written = 0;

    /// The threshold from the environment value OKAY_SLOW_FRAME_MS (null or unreadable: 250 ms; "0": off; at least 20 ms).
    static double ThresholdFrom(const char* env) {
        if (!env || !*env) return 250.0;
        char* end = nullptr;
        const double v = std::strtod(env, &end);
        if (end == env || !std::isfinite(v)) return 250.0;
        if (v <= 0.0) return 0.0;
        return std::max(20.0, v);
    }

    /// The line for a frame of `frameMs` (`f`: the profiler's buckets for it; none when the profiler had nothing), or "" when the
    /// frame was quick, the log is off or full. `frameNo` counts frames since start; `context` (what is switched on, edit or play)
    /// is added at the end as it is. Counts the line as written.
    std::string Line(const Profiler::Frame& f, double frameMs, long long frameNo, const std::string& context) {
        if (!(thresholdMs > 0.0) || !std::isfinite(frameMs) || frameMs < thresholdMs || written >= maxLines) return std::string();
        ++written;
        Profiler::Frame g;
        g.total = frameMs;
        g.buckets = f.buckets;
        const Breakdown b = Break(g);
        int top = 0;
        for (int i = 1; i < (int)Section::Count; ++i) if (b.ms[i] > b.ms[top]) top = i;
        char buf[256];
        std::snprintf(buf, sizeof(buf), "slow frame %lld: %.0f ms, mostly %s (%.0f ms)", frameNo, frameMs, SectionName((Section)top), b.ms[top]);
        std::string line = buf;
        std::vector<const Profiler::Bucket*> scopes;
        for (const Profiler::Bucket& k : f.buckets) if (k.self >= 1.0) scopes.push_back(&k);
        const std::size_t keep = std::min<std::size_t>(4, scopes.size());
        std::partial_sort(scopes.begin(), scopes.begin() + (std::ptrdiff_t)keep, scopes.end(),
                          [](const Profiler::Bucket* a, const Profiler::Bucket* c) { return a->self > c->self; });
        if (keep == 0) line += "; no timed scopes (the profiler saw nothing: events, a wait or a window move)";
        for (std::size_t i = 0; i < keep; ++i) {
            std::snprintf(buf, sizeof(buf), "%s%s %.0f ms", i == 0 ? "; scopes: " : ", ", scopes[i]->name.c_str(), scopes[i]->self);
            line += buf;
        }
        std::snprintf(buf, sizeof(buf), "; not timed %.0f ms", b.ms[(int)Section::Untimed]);
        line += buf;
        if (!context.empty()) line += " | " + context;
        if (written == maxLines) line += " (the log is full: no more slow frames are written this run)";
        return line;
    }
};

// ---------------------------------------------------------------- the once-a-minute FPS summary

/// One line in okay_editor_perf.log every `periodSeconds` (default 60), so a steady low frame rate shows up too, not only the frames
/// over the slow-frame threshold: frames, average and 1%-low FPS (okay::FrameStats, as the overlay), and the three scopes with the
/// most self time per frame on average. Fed every frame with the frame's time and the profiler's buckets (none: no scopes).
struct FpsSummary {
    double periodSeconds = 60.0;   // 0 or less: off
    std::vector<double> frames;
    std::vector<std::pair<std::string, double>> selfMs;   // scope name -> self ms summed over the period
    double elapsedMs = 0.0;

    /// The line when a period is complete ("" otherwise); the period then starts again.
    std::string Add(const Profiler::Frame& f, double frameMs) {
        if (!(periodSeconds > 0.0) || !std::isfinite(frameMs) || frameMs <= 0.0) return std::string();
        frames.push_back(frameMs);
        elapsedMs += frameMs;
        for (const Profiler::Bucket& b : f.buckets) {
            if (!(b.self > 0.0)) continue;
            bool found = false;
            for (auto& s : selfMs) if (s.first == b.name) { s.second += b.self; found = true; break; }
            if (!found) selfMs.emplace_back(b.name, b.self);
        }
        if (elapsedMs < periodSeconds * 1000.0) return std::string();
        const HistoryStats st = Stats(frames);
        std::sort(selfMs.begin(), selfMs.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
        char buf[192];
        std::snprintf(buf, sizeof(buf), "fps summary: %zu frames in %.0f s, average %.1f FPS (%.1f ms), 1%% low %.1f FPS, worst %.0f ms",
                      st.frames, elapsedMs / 1000.0, st.fps, st.avgMs, st.lowFps, st.worstMs);
        std::string line = buf;
        const std::size_t n = frames.size();
        for (std::size_t i = 0; i < selfMs.size() && i < 3; ++i) {
            std::snprintf(buf, sizeof(buf), "%s%s %.1f ms", i == 0 ? "; top scopes per frame: " : ", ", selfMs[i].first.c_str(), selfMs[i].second / (double)n);
            line += buf;
        }
        frames.clear(); selfMs.clear(); elapsedMs = 0.0;
        return line;
    }

    /// The period from OKAY_FPS_SUMMARY_S (null or unreadable: 60 s; "0": off; at least 5 s).
    static double PeriodFrom(const char* env) { return SecondsOr(env, 60.0, 5.0); }
    static double SecondsOr(const char* env, double def, double least) {
        if (!env || !*env) return def;
        char* end = nullptr;
        const double v = std::strtod(env, &end);
        if (end == env || !std::isfinite(v)) return def;
        if (v <= 0.0) return 0.0;
        return std::max(least, v);
    }
};

} // namespace okay::editor::perf
