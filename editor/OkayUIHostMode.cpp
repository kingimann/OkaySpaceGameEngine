// Window mode (shared/OkayUIHostMode.hpp): OkayUI owns the window and the frame. Step 1: the frame loop (shared/OkayUIFrameLoop.hpp). Step 2: this entry, run
// from main() before any Dear ImGui or SDL setup of the editor when the three switches are set. What it shows today is the Audio Mixer and a status panel;
// everything else of the editor still needs its port (the numbered list in docs/own_ui_toolkit.md 6.23). Own code, written for this purpose.
#include "OkayUIHostMode.hpp"

#ifdef OKAY_HAVE_OKAYUI
#define OKAYUI_HOST_PLATFORM 1
#include "OkayUIFrameLoop.hpp"
#include "OkayUIPanels.hpp"
#include "okay/Audio/AudioMixer.hpp"
#include "okay/UI/OkayUIDock.hpp"
#include "okay/UI/OkayUIDockBuilder.hpp"
#include "okay/Platform/Painter.hpp"

#include <cstdio>

namespace oked {

namespace {
constexpr unsigned kSpaceId = 0x0A0E5001u;
}

void DrawHostModeUi(HostModeState& st) {
    using namespace OkayUI;
    const Input& in = GetFrameInput();
    const Rect area{0.0f, 0.0f, in.displayW, in.displayH};
    if (!st.docked && area.w > 8.0f && area.h > 8.0f) {   // first frame: the status panel on the right, the mixer on the left
        DockBuilderRemoveNode(kSpaceId);
        DockBuilderAddNode(kSpaceId, DockNode_DockSpace);
        DockBuilderSetNodeSize(kSpaceId, Vec2{area.w, area.h});
        DockId left = kSpaceId, right = 0;
        DockBuilderSplitNode(left, DockSide_Right, 0.38f, &right, &left);
        DockBuilderDockWindow("Audio Mixer", left);
        DockBuilderDockWindow("Window mode", right);
        DockBuilderFinish(kSpaceId);
        st.docked = true;
    }
    DockSpace(area);
    if (st.showMixer) {
        if (DockBegin("Audio Mixer", &st.showMixer)) {
            using okay::AudioMixer; using okay::AudioGroup;
            okayuiscreens::AudioMixerModel m;
            m.peakLeft = AudioMixer::peak[0]; m.peakRight = AudioMixer::peak[1];
            m.masterVolume = &AudioMixer::masterVolume; m.masterMuted = &AudioMixer::muted;
            for (int i = 0; i < (int)AudioGroup::Count; ++i) {
                m.groupNames.push_back(AudioMixer::GroupName((AudioGroup)i));
                m.groupVolume.push_back(&AudioMixer::groupVolume[i]);
                m.groupMuted.push_back(&AudioMixer::groupMuted[i]);
            }
            m.duckMusic = &AudioMixer::duckMusicUnderVoice; m.duckLevel = &AudioMixer::duckLevel;
            m.duckAttack = &AudioMixer::duckAttack; m.duckRelease = &AudioMixer::duckRelease; m.duckGain = AudioMixer::duckGain;
            const Vec2 p = GetCursorScreenPos(), a = GetContentRegionAvail();
            okayuiscreens::DrawAudioMixer(m, p.x, p.y, a.x, a.y);
        }
        DockEnd();
    }
    if (st.showStatus) {
        if (DockBegin("Window mode", &st.showStatus)) {
            Text("OkayUI owns this window.");
            Text("No Dear ImGui is running.");
            Separator();
            char line[64];
            std::snprintf(line, sizeof line, "Frames drawn: %d", st.frame); Text(line);
            std::snprintf(line, sizeof line, "Display: %d x %d", (int)in.displayW, (int)in.displayH); Text(line);
            Separator();
            TextDisabled("Only the Audio Mixer is ported to this mode so far.");
            TextDisabled("Close the window to leave.");
        }
        DockEnd();
    }
    ++st.frame;
}

int RunOkayUIHostMode(int, char**) {
    okay::platform::Backend* be = okay::platform::Default();
    if (!be || !be->Init(okay::platform::InitFlags())) { std::fprintf(stderr, "[okayui-host] no platform backend: %s\n", be ? be->LastError().c_str() : "none"); return 1; }
    okay::platform::WindowDesc d; d.title = "OkaySpace Editor (OkayUI window mode)"; d.width = 1280; d.height = 800; d.resizable = true;
    std::unique_ptr<okay::platform::Window> win = be->MakeWindow(d);
    if (!win) { std::fprintf(stderr, "[okayui-host] no window: %s\n", be->LastError().c_str()); be->Shutdown(); return 1; }
    std::string err;
    std::unique_ptr<okay::platform::Painter> painter = okay::platform::CreateSdl2Painter(*win, okay::platform::PainterDesc(), &err);
    if (!painter) { std::fprintf(stderr, "[okayui-host] no painter: %s\n", err.c_str()); be->Shutdown(); return 1; }
    OkayUI::UseLauncherLook(true);
    std::fprintf(stderr, "[okayui-host] window mode on (OKAY_UI_HOST=okayui): OkayUI owns the window; Dear ImGui is not started\n");
    okayuihost::platform::FrameLoop loop(*be, *painter);
    HostModeState state;
    while (loop.Frame([&] { DrawHostModeUi(state); })) {}
    painter.reset(); win.reset();
    be->Shutdown();
    return 0;
}

} // namespace oked
#else
namespace oked {
void DrawHostModeUi(HostModeState&) {}
int RunOkayUIHostMode(int, char**) { return 1; }   // not compiled in: HostModeRequested is never true without OKAY_HAVE_OKAYUI in main()
}
#endif
