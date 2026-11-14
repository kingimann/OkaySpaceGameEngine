#include "OkayPanels.hpp"
#include "MainShared.hpp"

#ifdef OKAY_HAVE_OKAYUI
#include "imgui.h"
#include "imgui_internal.h"
#include "OkayUIHost.hpp"
#include "OkayUIHostImGui.hpp"
#include "OkayUIPanels.hpp"
#include "okay/Audio/AudioMixer.hpp"

#include <algorithm>

namespace oked {

namespace {

// The ImGui window stays: it is the dock tab and the close box, and hands its free area to OkayUI as one rectangle (the same
// arrangement as the Console panel). Returns false when an ImGui popup covers the window.
template <class Draw>
bool HostWindow(const char* title, bool* open, const char* itemId, Draw draw) {
    const ImGuiWindow* cw = ImGui::FindWindowByName(title);
    if (cw && okayuihost::PopupCoversRect(cw->Pos.x, cw->Pos.y, cw->Size.x, cw->Size.y)) return false;
    if (ImGui::Begin(title, open, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImVec2 a = ImGui::GetContentRegionAvail();
        a.x = std::max(a.x, 8.0f); a.y = std::max(a.y, 8.0f);
        ImGui::InvisibleButton(itemId, a);               // an ImGui item, so ImGui does not move or scroll the window under it
        ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);    // the wheel scrolls the OkayUI body, not the ImGui window
        OkayUIRegion().Offer(p.x, p.y, a.x, a.y, ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem));
        OkayUI::PushTextScale(g_uiScale);
        draw(p, a);
        OkayUI::PopTextScale();
    }
    ImGui::End();
    return true;
}

} // namespace

bool DrawCrashLogOkayUI(bool* open, const std::string& text, const std::string& path, std::uint64_t revision, const CrashLogActions& actions) {
    static okayuiscreens::CrashLogState state;
    okayuiscreens::CrashLogModel model;
    model.text = text; model.path = path; model.revision = revision;   // (a copy per frame: crash logs are small)
    okayuiscreens::CrashLogHost host;
    host.refresh = actions.refresh;
    host.openFolder = actions.openFolder;
    host.clear = actions.clear;
    host.copyAll = [](const std::string& s) { ImGui::SetClipboardText(s.c_str()); };
    return HostWindow("Crash Log", open, "##okayui_crashlog", [&](ImVec2 p, ImVec2 a) {
        okayuiscreens::DrawCrashLog(model, state, host, p.x, p.y, a.x, a.y);
    });
}

bool DrawAudioMixerOkayUI(bool* open) {
    using okay::AudioMixer;
    using okay::AudioGroup;
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
    return HostWindow("Audio Mixer", open, "##okayui_audiomixer", [&](ImVec2 p, ImVec2 a) {
        okayuiscreens::DrawAudioMixer(m, p.x, p.y, a.x, a.y);
    });
}

} // namespace oked

#else
namespace oked {
bool DrawCrashLogOkayUI(bool*, const std::string&, const std::string&, std::uint64_t, const CrashLogActions&) { return false; }
bool DrawAudioMixerOkayUI(bool*) { return false; }
}
#endif
