#include "OkayPanels.hpp"
#include "MainShared.hpp"

#ifdef OKAY_HAVE_OKAYUI
#include "imgui.h"
#include "imgui_internal.h"
#include "OkayUIHost.hpp"
#include "OkayUIHostImGui.hpp"
#include "OkayUIPanels.hpp"
#include "okay/Audio/AudioMixer.hpp"
#include "EditorWidgets.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace oked {

// ---- the editor's look for hosted OkayUI panels --------------------------------------------------------------------------
namespace {
unsigned char Byte(float v) { return (unsigned char)(std::fmin(1.0f, std::fmax(0.0f, v)) * 255.0f + 0.5f); }
void SetCol(unsigned char* c, const ImVec4& v) { c[0] = Byte(v.x); c[1] = Byte(v.y); c[2] = Byte(v.z); c[3] = Byte(v.w); }
ImVec4 Mixv(const ImVec4& a, const ImVec4& b, float t) { return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t); }
}

OkayUILookScope::OkayUILookScope() : saved_(OkayUI::Style()) {
    OkayUI::Theme& t = OkayUI::Style();
    const ImVec4 control = SurfaceCol(5), base = SurfaceCol(2), field = SurfaceCol(4), deep = SurfaceCol(0);
    const ImVec4 ink = InkCol(1.0f), black(0, 0, 0, 1);
    const bool light = ThemeIsLight();
    SetCol(t.bg, control);
    SetCol(t.bgHover, Mixv(control, ink, 0.08f));
    SetCol(t.bgDown, Mixv(control, black, 0.14f));
    SetCol(t.border, InkCol(light ? 0.16f : 0.09f));
    SetCol(t.text, TextCol());
    SetCol(t.panel, ImVec4(base.x, base.y, base.z, 0.98f));
    SetCol(t.track, light ? Mixv(base, black, 0.12f) : field);
    SetCol(t.accent, AccentCol());
    SetCol(t.textDim, DimCol());
    SetCol(t.titleBg, deep);
    SetCol(t.field, field);
    SetCol(t.colors[OkayUI::Col_TableRowBgAlt], InkCol(light ? 0.04f : 0.043f));
    SetCol(t.colors[OkayUI::Col_PopupBg], Mixv(SurfaceCol(3), base, 0.35f));   // tooltips
    SetCol(t.colors[OkayUI::Col_ChildBg], SurfaceCol(3));                       // cards
    t.colorSet |= (1ull << OkayUI::Col_TableRowBgAlt) | (1ull << OkayUI::Col_PopupBg) | (1ull << OkayUI::Col_ChildBg);
}
OkayUILookScope::~OkayUILookScope() {
    OkayUI::Theme& t = OkayUI::Style();
    std::memcpy(t.bg, saved_.bg, sizeof(t.bg));           std::memcpy(t.bgHover, saved_.bgHover, sizeof(t.bgHover));
    std::memcpy(t.bgDown, saved_.bgDown, sizeof(t.bgDown)); std::memcpy(t.border, saved_.border, sizeof(t.border));
    std::memcpy(t.text, saved_.text, sizeof(t.text));     std::memcpy(t.panel, saved_.panel, sizeof(t.panel));
    std::memcpy(t.track, saved_.track, sizeof(t.track));  std::memcpy(t.accent, saved_.accent, sizeof(t.accent));
    std::memcpy(t.textDim, saved_.textDim, sizeof(t.textDim)); std::memcpy(t.titleBg, saved_.titleBg, sizeof(t.titleBg));
    std::memcpy(t.field, saved_.field, sizeof(t.field));
    std::memcpy(t.colors, saved_.colors, sizeof(t.colors)); t.colorSet = saved_.colorSet;
}

OkayUI::CodePalette EditorCodePalette() {
    using namespace OkayUI;
    namespace et = okay::edtheme;
    const et::CodePalette& c = ThemeCodePalette();
    auto P = [](const et::Rgba& v) -> Color32 { return et::PackAbgr(v); };
    CodePalette p = DefaultCodePalette();
    p.text[CodeColor_Default] = P(c.text);       p.text[CodeColor_Keyword] = P(c.keyword);   p.text[CodeColor_Type] = P(c.typeName);
    p.text[CodeColor_Function] = P(c.function);  p.text[CodeColor_Known] = P(c.known);       p.text[CodeColor_Member] = P(c.member);
    p.text[CodeColor_Number] = P(c.number);      p.text[CodeColor_String] = P(c.string);     p.text[CodeColor_Char] = P(c.string);
    p.text[CodeColor_Comment] = P(c.comment);    p.text[CodeColor_Punct] = P(c.punct);       p.text[CodeColor_Preproc] = P(c.preproc);
    p.background = P(c.background); p.gutter = P(c.gutter); p.lineNumber = P(c.lineNumber); p.lineNumberCurrent = P(c.lineNumberCurrent);
    p.caret = P(c.caret); p.selection = P(c.selection); p.selectionInactive = P(c.selectionInactive);
    p.currentLine = P(c.currentLine); p.currentLineEdge = P(c.currentLineEdge);
    p.findMatch = P(c.findMatch); p.bracketMatch = P(c.bracketMatch);
    p.errorLine = P(c.errorLine); p.errorUnderline = P(c.errorMark); p.breakpoint = P(c.breakpoint);
    p.scrollTrack = P(c.scrollTrack); p.scrollThumb = P(c.scrollThumb); p.scrollThumbHot = P(c.scrollThumbHot);
    return p;
}

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
        OkayUILookScope look;   // the editor's Dark / Light colours for the hosted panel
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
