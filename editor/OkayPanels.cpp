#include "OkayPanels.hpp"
#include "MainShared.hpp"

#ifdef OKAY_HAVE_OKAYUI
#include "imgui.h"
#include "imgui_internal.h"
#include "OkayUIHost.hpp"
#include "OkayUIHostImGui.hpp"
#include "OkayUIPanels.hpp"
#include "OkayUIEditorPanels.hpp"
#include "okay/Audio/AudioMixer.hpp"

#include <algorithm>
#include <cmath>

namespace oked {

namespace {

// The ImGui window stays: it is the dock tab and the close box, and hands its free area to OkayUI as one rectangle (the same
// arrangement as the Console panel). Returns false when an ImGui popup covers the window.
template <class Draw>
bool HostWindow(const char* title, bool* open, const char* itemId, Draw draw) {
    const ImGuiWindow* cw = ImGui::FindWindowByName(title);
    if (cw && okayuihost::PopupCoversRect(cw->Pos.x, cw->Pos.y, cw->Size.x, cw->Size.y)) return false;
    if (okayuihost::OutsideMainWindow(cw)) return false;
    // The body is one placeholder as big as the free area, so ImGui cannot fit a new floating window to its content (the Dear
    // ImGui versions are fitted): without a size of its own it would open as a tiny empty window.
    ImGui::SetNextWindowSize(ImVec2(440.0f * g_uiScale, 340.0f * g_uiScale), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(title, open, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        const ImVec2 p = okayuihost::ToMainWindowPixels(ImGui::GetCursorScreenPos());   // OkayUI's pixels, not ImGui's desktop ones
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

// An OkayUI body inside part of an ImGui window (a section of Project Settings): a placeholder item as tall as the body drew on the
// last frame, the part of it the window shows offered to OkayUI and used as the clip (the window scrolls it like any item), and
// the popup the body has open offered too. `draw(x, y, w, h)` returns the body's height and its popup rectangle.
struct SectionDrawn { float height = 0.0f; OkayUI::Rect popup; };
template <class Draw>
bool HostSection(const char* itemId, Draw draw) {
    ImGuiWindow* win = ImGui::GetCurrentWindow();
    if (!win || okayuihost::PopupCoversRect(win->Pos.x, win->Pos.y, win->Size.x, win->Size.y)) return false;
    if (okayuihost::OutsideMainWindow(win)) return false;
    float* height = ImGui::GetStateStorage()->GetFloatRef(ImGui::GetID(itemId), -1.0f);   // -1: not measured yet
    const ImVec2 p = okayuihost::ToMainWindowPixels(ImGui::GetCursorScreenPos());   // OkayUI's pixels, not ImGui's desktop ones
    const float w = std::max(ImGui::GetContentRegionAvail().x, 8.0f);
    if (*height < 0.0f) {
        // The first frame: draw the body into an empty clip only to learn its height, and place a placeholder that tall. A new
        // window fits itself to this frame's content (with a guessed height Project Settings opened far too tall, and so in its own
        // OS window, where OkayUI cannot draw). The body shows from the next frame.
        OkayUI::PushTextScale(g_uiScale * okayuiscreens::kEditorTextScale);
        OkayUI::PushClipRect(p.x, p.y, 0.0f, 0.0f);
        const SectionDrawn d = draw(p.x, p.y, w, 20000.0f);
        OkayUI::PopClipRect();
        OkayUI::PopTextScale();
        *height = (d.height > 1.0f && std::isfinite(d.height)) ? std::min(d.height, 20000.0f) : 300.0f;
        ImGui::InvisibleButton(itemId, ImVec2(w, std::max(*height, 8.0f)));
        return true;
    }
    const float h = std::max(*height, 8.0f);
    ImGui::InvisibleButton(itemId, ImVec2(w, h));
    const bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    const ImVec2 c0 = okayuihost::ToMainWindowPixels(win->InnerClipRect.Min), c1 = okayuihost::ToMainWindowPixels(win->InnerClipRect.Max);
    const ImRect clip(c0, c1);
    const float x0 = std::max(p.x, clip.Min.x), y0 = std::max(p.y, clip.Min.y);
    const float x1 = std::min(p.x + w, clip.Max.x), y1 = std::min(p.y + h, clip.Max.y);
    if (x1 <= x0 || y1 <= y0) return true;            // scrolled out of view: the space stays, nothing is drawn
    OkayUIRegion().Offer(x0, y0, x1 - x0, y1 - y0, hovered);
    OkayUI::PushTextScale(g_uiScale * okayuiscreens::kEditorTextScale);
    OkayUI::PushClipRect(x0, y0, x1 - x0, y1 - y0);
    const SectionDrawn d = draw(p.x, p.y, w, h + 400.0f);   // room below: the height is measured, not cut
    OkayUI::PopClipRect();
    OkayUI::PopTextScale();
    if (d.height > 1.0f && std::isfinite(d.height)) *height = std::min(d.height, 20000.0f);
    if (d.popup.w > 0.0f && d.popup.h > 0.0f) OkayUIRegion().Offer(d.popup.x, d.popup.y, d.popup.w, d.popup.h, true);   // it is drawn over everything
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

// The Problems, Stats and History panels: the editor's 18-pixel text on top of the UI scale (the Dear ImGui versions draw at that size).
bool DrawProblemsOkayUI(const char* title, bool* open, const okaypanels::ProblemsModel& model, okaypanels::ProblemsState& state,
                        okaypanels::ProblemsIntent& intent) {
    return HostWindow(title, open, "##okayui_problems", [&](ImVec2 p, ImVec2 a) {
        OkayUI::PushTextScale(okayuiscreens::kEditorTextScale);
        intent = okayuiscreens::DrawProblems(model, state, p.x, p.y, a.x, a.y).intent;
        OkayUI::PopTextScale();
    });
}

bool DrawStatsOkayUI(bool* open, const std::function<void(okaypanels::StatsModel&)>& fill) {
    return HostWindow("Stats", open, "##okayui_stats", [&](ImVec2 p, ImVec2 a) {
        okaypanels::StatsModel model;
        fill(model);
        OkayUI::PushTextScale(okayuiscreens::kEditorTextScale);
        const okaypanels::StatsIntent intent = okayuiscreens::DrawStats(model, p.x, p.y, a.x, a.y).intent;
        OkayUI::PopTextScale();
        if (intent.copy) ImGui::SetClipboardText(okaypanels::StatsCopyText(model).c_str());
    });
}

bool DrawHistoryOkayUI(bool* open, const okaypanels::HistoryModel& model, okaypanels::HistoryIntent& intent) {
    return HostWindow("History", open, "##okayui_history", [&](ImVec2 p, ImVec2 a) {
        OkayUI::PushTextScale(okayuiscreens::kEditorTextScale);
        intent = okayuiscreens::DrawHistory(model, p.x, p.y, a.x, a.y).intent;
        OkayUI::PopTextScale();
    });
}

bool DrawTagsLayersOkayUI(const okaypanels::TagsLayersModel& model, okaypanels::TagsLayersState& state, okaypanels::TagsLayersIntent& intent) {
    return HostSection("##okayui_tagslayers", [&](float x, float y, float w, float h) {
        const okayuiscreens::TagsLayersResult r = okayuiscreens::DrawTagsLayers(model, state, x, y, w, h);
        intent = r.intent;
        return SectionDrawn{r.contentHeight, r.popup};
    });
}

bool DrawPhysicsSettingsOkayUI(okaypanels::PhysicsSettingsModel& model, okaypanels::PhysicsSettingsIntent& intent) {
    return HostSection("##okayui_physics", [&](float x, float y, float w, float h) {
        const okayuiscreens::PhysicsSettingsResult r = okayuiscreens::DrawPhysicsSettings(model, x, y, w, h);
        intent = r.intent;
        return SectionDrawn{r.contentHeight, r.popup};
    });
}

bool DrawImportSettingsOkayUI(const char* id, okaypanels::ImportSettingsModel& model, okaypanels::ImportSettingsState& state,
                              okaypanels::ImportSettingsIntent& intent) {
    return HostSection(id, [&](float x, float y, float w, float h) {
        const okayuiscreens::ImportSettingsResult r = okayuiscreens::DrawImportSettings(model, state, x, y, w, h);
        intent = r.intent;
        return SectionDrawn{r.contentHeight, r.popup};
    });
}

bool DrawPostProcessOkayUI(okaypanels::PostProcessModel& model, okaypanels::PostProcessIntent& intent) {
    return HostSection("##okayui_post", [&](float x, float y, float w, float h) {
        const okayuiscreens::PostProcessResult r = okayuiscreens::DrawPostProcess(model, x, y, w, h);
        intent = r.intent;
        return SectionDrawn{r.contentHeight, r.popup};
    });
}

bool DrawPlayerSettingsOkayUI(okaypanels::PlayerSettingsModel& model, okaypanels::PlayerSettingsIntent& intent) {
    return HostSection("##okayui_player", [&](float x, float y, float w, float h) {
        const okayuiscreens::PlayerSettingsResult r = okayuiscreens::DrawPlayerSettings(model, x, y, w, h);
        intent = r.intent;
        return SectionDrawn{r.contentHeight, OkayUI::Rect{}};
    });
}

bool DrawRenderingSettingsOkayUI(okaypanels::RenderingSettingsModel& model, okaypanels::RenderingSettingsIntent& intent) {
    return HostSection("##okayui_rendering", [&](float x, float y, float w, float h) {
        const okayuiscreens::RenderingSettingsResult r = okayuiscreens::DrawRenderingSettings(model, x, y, w, h);
        intent = r.intent;
        return SectionDrawn{r.contentHeight, OkayUI::Rect{}};
    });
}

bool DrawProfilerOkayUI(bool* open, const std::function<void(okaypanels::ProfilerModel&)>& fill, okaypanels::ProfilerIntent& intent) {
    return HostWindow("Profiler", open, "##okayui_profiler", [&](ImVec2 p, ImVec2 a) {
        okaypanels::ProfilerModel model;
        fill(model);
        OkayUI::PushTextScale(okayuiscreens::kEditorTextScale);
        intent = okayuiscreens::DrawProfiler(model, p.x, p.y, a.x, a.y).intent;
        OkayUI::PopTextScale();
        if (intent.copyCsv) ImGui::SetClipboardText(okaypanels::ProfilerCsv(model).c_str());
    });
}

} // namespace oked

#else
namespace oked {
bool DrawCrashLogOkayUI(bool*, const std::string&, const std::string&, std::uint64_t, const CrashLogActions&) { return false; }
bool DrawAudioMixerOkayUI(bool*) { return false; }
bool DrawProblemsOkayUI(const char*, bool*, const okaypanels::ProblemsModel&, okaypanels::ProblemsState&, okaypanels::ProblemsIntent&) { return false; }
bool DrawStatsOkayUI(bool*, const std::function<void(okaypanels::StatsModel&)>&) { return false; }
bool DrawHistoryOkayUI(bool*, const okaypanels::HistoryModel&, okaypanels::HistoryIntent&) { return false; }
bool DrawTagsLayersOkayUI(const okaypanels::TagsLayersModel&, okaypanels::TagsLayersState&, okaypanels::TagsLayersIntent&) { return false; }
bool DrawPhysicsSettingsOkayUI(okaypanels::PhysicsSettingsModel&, okaypanels::PhysicsSettingsIntent&) { return false; }
bool DrawProfilerOkayUI(bool*, const std::function<void(okaypanels::ProfilerModel&)>&, okaypanels::ProfilerIntent&) { return false; }
bool DrawImportSettingsOkayUI(const char*, okaypanels::ImportSettingsModel&, okaypanels::ImportSettingsState&, okaypanels::ImportSettingsIntent&) { return false; }
bool DrawPostProcessOkayUI(okaypanels::PostProcessModel&, okaypanels::PostProcessIntent&) { return false; }
bool DrawPlayerSettingsOkayUI(okaypanels::PlayerSettingsModel&, okaypanels::PlayerSettingsIntent&) { return false; }
bool DrawRenderingSettingsOkayUI(okaypanels::RenderingSettingsModel&, okaypanels::RenderingSettingsIntent&) { return false; }
}
#endif
