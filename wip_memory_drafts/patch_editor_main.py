import sys
p = sys.argv[1]
s = open(p).read()
def rep(a, b):
    global s
    assert s.count(a) == 1, (a, s.count(a))
    s = s.replace(a, b)

# 1. idle throttle on by default; the old key (written for everyone) is not read
rep("""// View > Rendering: editor frame rate and cost switches (okay::editor::perf in EditorPerf.hpp). All off by default; remembered in okay_settings.txt.
int   g_fpsCapEditor = 0;       // 0 = no cap; else the editor sleeps out the rest of each frame
bool  g_idleThrottle = false;   // slow to g_idleFps while nothing is clicked, typed or playing""",
"""// View > Rendering: editor frame rate and cost switches (okay::editor::perf in EditorPerf.hpp); remembered in okay_settings.txt.
int   g_fpsCapEditor = 0;       // 0 = no cap; else the editor sleeps out the rest of each frame
bool  g_idleThrottle = okay::editor::perf::kIdleThrottleDefault;   // ON: slow to g_idleFps while nothing is clicked, typed, playing or previewing""")
rep("""        else if (k == "idlethrottle") g_idleThrottle = (v != 0);""",
"""        else if (k == "idle_throttle") g_idleThrottle = (v != 0);   // (the old "idlethrottle" line was written for everyone: not read, so everyone starts on)""")
rep("""      << "idlethrottle " << (g_idleThrottle ? 1 : 0) << "\\n\"""",
"""      << "idle_throttle " << (g_idleThrottle ? 1 : 0) << "\\n\"""")

# 2. the pacing: wake reasons and activity
rep("""            const okay::editor::perf::PacingSettings ps{g_fpsCapEditor, g_idleThrottle, g_idleFps, 0.75};
            const double target = okay::editor::perf::TargetFps(ps, ed.isPlaying(), ImGui::IsAnyMouseDown(), g_secSinceInput, winSeam->IsMinimized());""",
"""            const okay::editor::perf::PacingSettings ps{g_fpsCapEditor, g_idleThrottle, g_idleFps, 0.75};
            okay::editor::perf::IdleWake wake;
            wake.playing = ed.isPlaying();
            wake.pointerHeld = ImGui::IsAnyMouseDown();
            wake.preview = g_animPreview || g_livePrevCh || g_maScenePrev || TimelinePreviewPlaying();
            wake.debugger = okay::vs::VsDebugger::Get().Active();
            wake.splash = !StartSplash().finish.Done();
            // A new Console line (build output, a download, an import, a file reload) is activity: full speed for a moment.
            static unsigned long long s_serial = 0; static Uint64 s_activity = okay::platform::PerfCounter();
            if (ConsoleLineSerial() != s_serial) { s_serial = ConsoleLineSerial(); s_activity = okay::platform::PerfCounter(); }
            const double sinceActivity = (double)(okay::platform::PerfCounter() - s_activity) / (double)okay::platform::PerfFrequency();
            const double target = okay::editor::perf::TargetFps(ps, wake, okay::editor::perf::QuietSeconds(g_secSinceInput, sinceActivity), winSeam->IsMinimized());""")

# 3. the memory report at exit (OKAY_MEMORY_REPORT=1), before anything is freed
rep("""    DocRecovery().EndSession(true);   // clean exit: the user saved or chose not to""",
"""    if (okay::MemoryReportWanted()) { std::fputs(okay::MemoryReportText("[memory] editor").c_str(), stdout); std::fflush(stdout); }
    DocRecovery().EndSession(true);   // clean exit: the user saved or chose not to""")

# 4. the frame-time report (Copy button) carries the memory lines too
rep("""        ImGui::SetClipboardText(rep.c_str());
        ConsoleLog("Frame-time report copied to the clipboard.");""",
"""        ImGui::SetClipboardText((rep + okay::MemoryReportText("Memory")).c_str());
        ConsoleLog("Frame-time report copied to the clipboard.");""")

# 5. counters, registered once the fonts are in the atlas
rep("""        if (!g_codeFont) { ImFontConfig fb; fb.SizePixels = 13.0f; g_codeFont = io.Fonts->AddFontDefault(&fb); }
    }""",
"""        if (!g_codeFont) { ImFontConfig fb; fb.SizePixels = 13.0f; g_codeFont = io.Fonts->AddFontDefault(&fb); }
    }
    RegisterEditorMemoryCounters();   // the memory report's owners (okay/Core/Memory.hpp; OKAY_MEMORY_REPORT=1 prints it at exit)""")

rep("""#include "EditorPerf.hpp"            // frame-time maths, idle throttle, viewport scale, performance mode (tests/test_editor_perf.cpp)
""", """#include "EditorPerf.hpp"            // frame-time maths, idle throttle, viewport scale, performance mode (tests/test_editor_perf.cpp)
#include "okay/Core/Memory.hpp"       // the memory report (OKAY_MEMORY_REPORT=1)
#include "okay/Graphics/TtfFont.hpp"  // FontCacheBytes, for the memory report
""")
# GetThumb: count what it uploads
rep("""    if (g_sdlRenderer && img.Width() > 0) {
        tex = okay::pb::CreateTexture(g_sdlRenderer, img.Width(), img.Height(), okay::pb::Access::Static, true, false,
                                      (const std::uint8_t*)img.Pixels().data());
    }
    cache[path] = tex;   // cache failures (nullptr) too, so we don't retry""",
"""    if (g_sdlRenderer && img.Width() > 0) {
        tex = okay::pb::CreateTexture(g_sdlRenderer, img.Width(), img.Height(), okay::pb::Access::Static, true, false,
                                      (const std::uint8_t*)img.Pixels().data());
        if (tex) g_thumbTextureBytes += (std::size_t)img.Width() * (std::size_t)img.Height() * 4;   // the memory report
    }
    cache[path] = tex;   // cache failures (nullptr) too, so we don't retry""")
rep("""SDL_Texture* GetThumb(const std::string& path) {
    if (!path.empty() && path[0] == '@') return nullptr;""",
"""static std::size_t g_thumbTextureBytes = 0;   // RGBA bytes GetThumb uploaded (the textures stay for the session)
SDL_Texture* GetThumb(const std::string& path) {
    if (!path.empty() && path[0] == '@') return nullptr;""")

rep("""int RunVsGraphShot(int argc, char** argv);
""",
"""int RunVsGraphShot(int argc, char** argv);

// OKAY_MEMORY_REPORT=1: the private memory at each start-up step, so the report shows which step costs what.
static void MemoryPhase(const char* step) {
    if (!okay::MemoryReportWanted()) return;
    const okay::ProcessMemory m = okay::QueryProcessMemory();
    std::printf("[memory] editor before \"%s\": private %.1f MB, resident %.1f MB\n", step, (double)m.privateBytes / 1048576.0, (double)m.residentBytes / 1048576.0);
    std::fflush(stdout);
}

// The memory report's named owners (okay/Core/Memory.hpp): what the editor holds that it can count itself.
static void RegisterEditorMemoryCounters() {
    okay::AddMemoryCounter("Fonts: TrueType cache (engine and OkayUI text)", [] { return okay::FontCacheBytes(); });
#ifndef OKAY_ON_OKAYUI
    okay::AddMemoryCounter("Fonts: Dear ImGui font files", [] {
        std::size_t n = 0;
        for (const ImFontConfig& c : ImGui::GetIO().Fonts->Sources) if (c.FontDataOwnedByAtlas) n += (std::size_t)c.FontDataSize;
        return n;
    });
    okay::AddMemoryCounter("Fonts: Dear ImGui atlas pixels", [] {
        std::size_t n = 0;
        for (ImTextureData* t : ImGui::GetIO().Fonts->TexList) if (t && t->Pixels) n += (std::size_t)t->GetSizeInBytes();
        return n;
    });
#endif
    okay::AddMemoryCounter("Pictures: Project / asset thumbnails uploaded", [] { return g_thumbTextureBytes; });
}
""")
rep("""        if (step) StartSplash().steps.Begin(step);""",
"""        if (step) { StartSplash().steps.Begin(step); MemoryPhase(step); }""")
rep("""            wake.splash = !StartSplash().finish.Done();""",
"""            wake.splash = !StartSplash().finish.Done();
            { static bool s_shown = false; if (!wake.splash && !s_shown) { s_shown = true; MemoryPhase("the editor is shown"); } }""")
open(p, 'w').write(s)
print("patched")
