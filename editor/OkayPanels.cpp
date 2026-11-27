#include "OkayPanels.hpp"
#include "MainShared.hpp"

// What the Crash Log, Audio Mixer and History windows show. These windows are drawn by OkayUI's screens.
#include "imgui.h"
#include "OkayUIPanels.hpp"
#include "OkayUIEditorPanels.hpp"
#include "AudioMixerModel.hpp"   // FillAudioMixerModel
#ifdef OKAY_HAVE_CLI_PANEL
#include "CommandLinePanel.hpp"   // shared/: the Command Line panel (okay commands run inside the editor)
#endif

#include <algorithm>
#include <functional>
#include <vector>

namespace oked {

namespace {
okayuiscreens::CrashLogHost CrashLogHostOf(const CrashLogActions& actions) {
    okayuiscreens::CrashLogHost host;
    host.refresh = actions.refresh;
    host.openFolder = actions.openFolder;
    host.clear = actions.clear;
    host.copyAll = [](const std::string& s) { ImGui::SetClipboardText(s.c_str()); };
    return host;
}
// "Copy name" in a History row's menu: the row's label to the clipboard.
void CopyHistoryRow(const okaypanels::HistoryModel& model, const okaypanels::HistoryIntent& intent) {
    if (intent.copy < 0) return;
    const std::vector<okaypanels::HistoryRow> rows = okaypanels::HistoryRows(model);
    if ((std::size_t)intent.copy < rows.size()) ImGui::SetClipboardText(rows[(std::size_t)intent.copy].label.c_str());
}
} // namespace

} // namespace oked

namespace oked {
// The editor's windows are OkayUI windows (it is compiled against OkayUI's Dear ImGui names, docs/ui/own_ui_toolkit.md 6.26 and 33), so the
// body is drawn straight into the window.
namespace {
template <class Draw>
bool PanelWindow(const char* title, bool* open, const char*, Draw draw, float firstW = 440.0f, float firstH = 340.0f,
                 ImGuiWindowFlags flags = 0, const ImVec2* fit = nullptr) {
    if (open && !*open) return false;
    if (!fit) ImGui::SetNextWindowSize(ImVec2(firstW * g_uiScale, firstH * g_uiScale), ImGuiCond_FirstUseEver);
    const bool shown = ImGui::Begin(title, open, flags);
    if (shown) {
        const ImVec2 p = ImGui::GetCursorScreenPos(), a = fit ? *fit : ImGui::GetContentRegionAvail();
        draw(p, ImVec2(std::max(a.x, 8.0f), std::max(a.y, 8.0f)));
        if (fit) ImGui::Dummy(*fit);   // the window fits itself around the body
    }
    ImGui::End();
    return shown;
}
// A section: the body drawn at the cursor, then a placeholder as tall as it drew.
template <class Draw>
bool PanelSection(const char*, Draw draw) {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = std::max(ImGui::GetContentRegionAvail().x, 8.0f);
    OkayUI::PushTextScale(okayuiscreens::kEditorTextScale);
    const float h = draw(p.x, p.y, w, 20000.0f);
    OkayUI::PopTextScale();
    ImGui::Dummy(ImVec2(w, (h > 1.0f && h < 20000.0f) ? h : 8.0f));
    return true;
}
} // namespace
}

// ---- The panels drawn by OkayUI only (docs/ui/own_ui_toolkit.md sections 24 and 25): no Dear ImGui version exists. -------------
namespace oked {

bool DrawCrashLogOkayUI(bool* open, const std::string& text, const std::string& path, std::uint64_t revision, const CrashLogActions& actions) {
    static okayuiscreens::CrashLogState state;
    okayuiscreens::CrashLogModel model;
    model.text = text; model.path = path; model.revision = revision;   // (a copy per frame: crash logs are small)
    const okayuiscreens::CrashLogHost host = CrashLogHostOf(actions);
    return PanelWindow("Crash Log", open, "##okayui_crashlog", [&](ImVec2 p, ImVec2 a) {
        okayuiscreens::DrawCrashLog(model, state, host, p.x, p.y, a.x, a.y);
    });
}

bool DrawAudioMixerOkayUI(bool* open) {
    okayuiscreens::AudioMixerModel m;
    FillAudioMixerModel(m);
    return PanelWindow("Audio Mixer", open, "##okayui_audiomixer", [&](ImVec2 p, ImVec2 a) {
        okayuiscreens::DrawAudioMixer(m, p.x, p.y, a.x, a.y);
    });
}

// The Problems, Stats, History and Profiler panels: the editor's 18-pixel text on top of the UI scale (the size their Dear ImGui
// versions had).
bool DrawHistoryOkayUI(bool* open, const std::function<void(okaypanels::HistoryModel&)>& fill, okaypanels::HistoryIntent& intent) {
    intent = okaypanels::HistoryIntent();
    return PanelWindow("History", open, "##okayui_history", [&](ImVec2 p, ImVec2 a) {
        okaypanels::HistoryModel model;
        fill(model);
        OkayUI::PushTextScale(okayuiscreens::kEditorTextScale);
        intent = okayuiscreens::DrawHistory(model, p.x, p.y, a.x, a.y).intent;
        OkayUI::PopTextScale();
        CopyHistoryRow(model, intent);
    });
}

bool DrawProblemsOkayUI(const char* title, bool* open, const okaypanels::ProblemsModel& model, okaypanels::ProblemsState& state,
                        okaypanels::ProblemsIntent& intent, ProblemsRects* rects) {
    intent = okaypanels::ProblemsIntent();
    if (rects) *rects = ProblemsRects();
    return PanelWindow(title, open, "##okayui_problems", [&](ImVec2 p, ImVec2 a) {
        OkayUI::PushTextScale(okayuiscreens::kEditorTextScale);
        const okayuiscreens::ProblemsResult r = okayuiscreens::DrawProblems(model, state, p.x, p.y, a.x, a.y);
        OkayUI::PopTextScale();
        intent = r.intent;
        if (rects) {
            rects->rowIndex = r.rowIndex;
            for (const OkayUI::Rect& q : r.rows) rects->rows.push_back(PanelRect{q.x, q.y, q.w, q.h});
            for (const OkayUI::Rect& q : r.fixButtons) rects->fixButtons.push_back(PanelRect{q.x, q.y, q.w, q.h});
        }
    });
}

bool DrawStatsOkayUI(bool* open, const std::function<void(okaypanels::StatsModel&)>& fill) {
    return PanelWindow("Stats", open, "##okayui_stats", [&](ImVec2 p, ImVec2 a) {
        okaypanels::StatsModel model;
        fill(model);
        OkayUI::PushTextScale(okayuiscreens::kEditorTextScale);
        const okaypanels::StatsIntent intent = okayuiscreens::DrawStats(model, p.x, p.y, a.x, a.y).intent;
        OkayUI::PopTextScale();
        if (intent.copy) ImGui::SetClipboardText(okaypanels::StatsCopyText(model).c_str());
    });
}

bool DrawProfilerOkayUI(bool* open, const std::function<void(okaypanels::ProfilerModel&)>& fill, okaypanels::ProfilerIntent& intent) {
    intent = okaypanels::ProfilerIntent();
    return PanelWindow("Profiler", open, "##okayui_profiler", [&](ImVec2 p, ImVec2 a) {
        okaypanels::ProfilerModel model;
        fill(model);
        OkayUI::PushTextScale(okayuiscreens::kEditorTextScale);
        intent = okayuiscreens::DrawProfiler(model, p.x, p.y, a.x, a.y).intent;
        OkayUI::PopTextScale();
        if (intent.copyCsv) ImGui::SetClipboardText(okaypanels::ProfilerCsv(model).c_str());
    });
}

// Project Settings' Player, Physics, Rendering and Tags and Layers sections (docs/ui/own_ui_toolkit.md section 26): OkayUI only, each
// in its place under its collapsing header inside the Dear ImGui window.
bool DrawTagsLayersOkayUI(const okaypanels::TagsLayersModel& model, okaypanels::TagsLayersState& state, okaypanels::TagsLayersIntent& intent) {
    intent = okaypanels::TagsLayersIntent();
    return PanelSection("##okayui_tagslayers", [&](float x, float y, float w, float h) {
        const okayuiscreens::TagsLayersResult r = okayuiscreens::DrawTagsLayers(model, state, x, y, w, h);
        intent = r.intent;
        return r.contentHeight;
    });
}

bool DrawPhysicsSettingsOkayUI(okaypanels::PhysicsSettingsModel& model, okaypanels::PhysicsSettingsIntent& intent) {
    intent = okaypanels::PhysicsSettingsIntent();
    return PanelSection("##okayui_physics", [&](float x, float y, float w, float h) {
        const okayuiscreens::PhysicsSettingsResult r = okayuiscreens::DrawPhysicsSettings(model, x, y, w, h);
        intent = r.intent;
        return r.contentHeight;
    });
}

bool DrawPlayerSettingsOkayUI(okaypanels::PlayerSettingsModel& model, okaypanels::PlayerSettingsIntent& intent) {
    intent = okaypanels::PlayerSettingsIntent();
    return PanelSection("##okayui_player", [&](float x, float y, float w, float h) {
        const okayuiscreens::PlayerSettingsResult r = okayuiscreens::DrawPlayerSettings(model, x, y, w, h);
        intent = r.intent;
        return r.contentHeight;
    });
}

bool DrawRenderingSettingsOkayUI(okaypanels::RenderingSettingsModel& model, okaypanels::RenderingSettingsIntent& intent) {
    intent = okaypanels::RenderingSettingsIntent();
    return PanelSection("##okayui_rendering", [&](float x, float y, float w, float h) {
        const okayuiscreens::RenderingSettingsResult r = okayuiscreens::DrawRenderingSettings(model, x, y, w, h);
        intent = r.intent;
        return r.contentHeight;
    });
}

bool DrawLoadingScreenOkayUI(okaypanels::LoadingScreenModel& model, okaypanels::LoadingScreenIntent& intent) {
    intent = okaypanels::LoadingScreenIntent();
    return PanelSection("##okayui_loadingscreen", [&](float x, float y, float w, float h) {
        const okayuiscreens::LoadingScreenResult r = okayuiscreens::DrawLoadingScreen(model, x, y, w, h);
        intent = r.intent;
        return r.contentHeight;
    });
}

// The Project panel's import settings (shown in the Project panel and in the Inspector for the selected asset; `id` keeps the two
// apart) and Lighting > Post-processing (docs/ui/own_ui_toolkit.md section 27): OkayUI only, like the Project Settings sections.
bool DrawImportSettingsOkayUI(const char* id, okaypanels::ImportSettingsModel& model, okaypanels::ImportSettingsState& state,
                              okaypanels::ImportSettingsIntent& intent) {
    intent = okaypanels::ImportSettingsIntent();
    return PanelSection(id, [&](float x, float y, float w, float h) {
        const okayuiscreens::ImportSettingsResult r = okayuiscreens::DrawImportSettings(model, state, x, y, w, h);
        intent = r.intent;
        return r.contentHeight;
    });
}

bool DrawPostProcessOkayUI(okaypanels::PostProcessModel& model, okaypanels::PostProcessIntent& intent) {
    intent = okaypanels::PostProcessIntent();
    return PanelSection("##okayui_post", [&](float x, float y, float w, float h) {
        const okayuiscreens::PostProcessResult r = okayuiscreens::DrawPostProcess(model, x, y, w, h);
        intent = r.intent;
        return r.contentHeight;
    });
}

// The small tool windows (docs/ui/own_ui_toolkit.md section 28): OkayUI only, each a whole window like Stats. The titles are the
// ones the Dear ImGui windows had, so saved layouts keep their place.
bool DrawShortcutsOkayUI(bool* open) {
    static okaypanels::ShortcutsState state;
    return PanelWindow("Keyboard Shortcuts", open, "##okayui_shortcuts", [&](ImVec2 p, ImVec2 a) {
        OkayUI::PushTextScale(okayuiscreens::kEditorTextScale);
        okayuiscreens::DrawShortcuts(okaypanels::KeyboardShortcuts(), state, p.x, p.y, a.x, a.y);
        OkayUI::PopTextScale();
    }, 520.0f, 560.0f);
}

bool DrawNetSimOkayUI(bool* open, okaypanels::NetSimModel& model, okaypanels::NetSimIntent& intent) {
    intent = okaypanels::NetSimIntent();
    return PanelWindow("Net###Net", open, "##okayui_net", [&](ImVec2 p, ImVec2 a) {
        OkayUI::PushTextScale(okayuiscreens::kEditorTextScale);
        intent = okayuiscreens::DrawNetSim(model, p.x, p.y, a.x, a.y).intent;
        OkayUI::PopTextScale();
    });
}

bool DrawFindInFilesOkayUI(bool* open, const okaypanels::FindInFilesModel& model, okaypanels::FindInFilesState& state,
                           okaypanels::FindInFilesIntent& intent) {
    intent = okaypanels::FindInFilesIntent();
    return PanelWindow("Find in Files", open, "##okayui_findinfiles", [&](ImVec2 p, ImVec2 a) {
        OkayUI::PushTextScale(okayuiscreens::kEditorTextScale);
        intent = okayuiscreens::DrawFindInFiles(model, state, p.x, p.y, a.x, a.y).intent;
        OkayUI::PopTextScale();
    }, 600.0f, 420.0f);
}

bool DrawDataAssetOkayUI(bool* open, okaypanels::DataAssetModel& model, okaypanels::DataAssetIntent& intent) {
    intent = okaypanels::DataAssetIntent();
    return PanelWindow("Data Asset", open, "##okayui_dataasset", [&](ImVec2 p, ImVec2 a) {
        OkayUI::PushTextScale(okayuiscreens::kEditorTextScale);
        intent = okayuiscreens::DrawDataAsset(model, p.x, p.y, a.x, a.y).intent;
        OkayUI::PopTextScale();
    });
}

bool DrawPluginManagerOkayUI(bool* open, const okaypanels::PluginManagerModel& model, okaypanels::PluginManagerIntent& intent) {
    intent = okaypanels::PluginManagerIntent();
    return PanelWindow("Plugin Manager", open, "##okayui_plugins", [&](ImVec2 p, ImVec2 a) {
        OkayUI::PushTextScale(okayuiscreens::kEditorTextScale);
        intent = okayuiscreens::DrawPluginManager(model, p.x, p.y, a.x, a.y).intent;
        OkayUI::PopTextScale();
    }, 560.0f, 360.0f);
}

// More tool windows (docs/ui/own_ui_toolkit.md section 29): OkayUI only, each a whole window. The titles are the Dear ImGui ones.
bool DrawBoneMapOkayUI(bool* open, okaypanels::BoneMapModel& model, okaypanels::BoneMapIntent& intent) {
    intent = okaypanels::BoneMapIntent();
    return PanelWindow("Map Humanoid Bones", open, "##okayui_bonemap", [&](ImVec2 p, ImVec2 a) {
        OkayUI::PushTextScale(okayuiscreens::kEditorTextScale);
        intent = okayuiscreens::DrawBoneMap(model, p.x, p.y, a.x, a.y).intent;
        OkayUI::PopTextScale();
    }, 460.0f, 560.0f);
}

bool DrawSaveManagerOkayUI(bool* open, okaypanels::SaveManagerModel& model, const std::function<void(okaypanels::SaveManagerModel&)>& fill,
                           okaypanels::SaveManagerState& state, okaypanels::SaveManagerIntent& intent) {
    intent = okaypanels::SaveManagerIntent();
    return PanelWindow("Save Manager", open, "##okayui_savemanager", [&](ImVec2 p, ImVec2 a) {
        fill(model);
        OkayUI::PushTextScale(okayuiscreens::kEditorTextScale);
        intent = okayuiscreens::DrawSaveManager(model, state, p.x, p.y, a.x, a.y).intent;
        OkayUI::PopTextScale();
    }, 560.0f, 480.0f);
}

bool DrawMaterialOkayUI(bool* open, okaypanels::MaterialModel& model, okaypanels::MaterialIntent& intent) {
    intent = okaypanels::MaterialIntent();
    return PanelWindow("Material", open, "##okayui_material", [&](ImVec2 p, ImVec2 a) {
        OkayUI::PushTextScale(okayuiscreens::kEditorTextScale);
        const okayuiscreens::MaterialResult r = okayuiscreens::DrawMaterial(model, p.x, p.y, a.x, a.y);
        OkayUI::PopTextScale();
        intent = r.intent;
    });
}

bool DrawPerfOverlayOkayUI(bool* open, okaypanels::PerfOverlayModel& model, const std::function<void(okaypanels::PerfOverlayModel&)>& fill,
                           okaypanels::PerfOverlayIntent& intent) {
    intent = okaypanels::PerfOverlayIntent();
    static ImVec2 fit(400.0f, 300.0f);   // the body's size on the last frame (the window fits itself around it)
    if (open && !*open) return false;    // (before the next window's place is set: it would go to another window)
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x - 12.0f, vp->WorkPos.y + 36.0f), ImGuiCond_FirstUseEver, ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowBgAlpha(0.88f);
    return PanelWindow("Frame time###PerfOverlay", open, "##okayui_perfoverlay", [&](ImVec2 p, ImVec2 a) {
        fill(model);
        OkayUI::PushTextScale(okayuiscreens::kEditorTextScale);
        const okayuiscreens::PerfOverlayResult r = okayuiscreens::DrawPerfOverlay(model, p.x, p.y, a.x, a.y);
        OkayUI::PopTextScale();
        intent = r.intent;
        if (r.contentW > 1.0f && r.contentH > 1.0f) fit = ImVec2(r.contentW, r.contentH);
    }, 400.0f, 300.0f, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav, &fit);
}

bool DrawInputPanelOkayUI(bool* open, okaypanels::InputPanelModel& model, const std::function<void(okaypanels::InputPanelModel&)>& fill,
                          okaypanels::InputPanelState& state, okaypanels::InputPanelIntent& intent) {
    intent = okaypanels::InputPanelIntent();
    return PanelWindow("Input", open, "##okayui_input", [&](ImVec2 p, ImVec2 a) {
        fill(model);
        OkayUI::PushTextScale(okayuiscreens::kEditorTextScale);
        intent = okayuiscreens::DrawInputPanel(model, state, p.x, p.y, a.x, a.y).intent;
        OkayUI::PopTextScale();
    }, 520.0f, 560.0f);
}

// Timers and Input Actions (docs/ui/own_ui_toolkit.md section 30): OkayUI only, each a whole window, titled as before.
bool DrawTimersOkayUI(bool* open, const std::function<std::string()>& text) {
    return PanelWindow("Timers", open, "##okayui_timers", [&](ImVec2 p, ImVec2 a) {
        const std::string t = text ? text() : std::string();
        OkayUI::PushTextScale(okayuiscreens::kEditorTextScale);
        okayuiscreens::DrawTimers(t, p.x, p.y, a.x, a.y);
        OkayUI::PopTextScale();
    }, 560.0f, 360.0f);
}

// View > Panels > Command Line: the one Command Line panel (shared/CommandLinePanel.hpp), commands run inside the editor.
bool DrawCommandLineOkayUI(bool* open, const std::string& projectDir) {
#ifdef OKAY_HAVE_CLI_PANEL
    static okaycli::CommandLinePanel panel([] {
        okaycli::CommandLinePanel::Config c;
        c.idPrefix = "##okayui_cmdline";
        c.copy = [](const std::string& t) { ImGui::SetClipboardText(t.c_str()); };   // the whole text (OkayUI's clipboard keeps 4 KB)
        return c;
    }());
    panel.SetFolder(projectDir);
    return PanelWindow("Command Line", open, "##okayui_cmdline", [&](ImVec2 p, ImVec2 a) {
        OkayUI::PushTextScale(okayuiscreens::kEditorTextScale);
        panel.Frame(p.x, p.y, a.x, a.y);
        OkayUI::PopTextScale();
    }, 640.0f, 320.0f);
#else
    (void)open; (void)projectDir;
    return false;
#endif
}

bool DrawInputActionsOkayUI(bool* open, okaypanels::InputActionsModel& model, const std::function<void(okaypanels::InputActionsModel&)>& fill,
                            okaypanels::InputActionsState& state, okaypanels::InputActionsIntent& intent) {
    intent = okaypanels::InputActionsIntent();
    return PanelWindow("Input Actions", open, "##okayui_inputactions", [&](ImVec2 p, ImVec2 a) {
        fill(model);
        OkayUI::PushTextScale(okayuiscreens::kEditorTextScale);
        intent = okayuiscreens::DrawInputActions(model, state, p.x, p.y, a.x, a.y).intent;
        OkayUI::PopTextScale();
    }, 900.0f, 600.0f);
}

// A whole OkayUI app in a window (the Script Editor's shared app): the free area is the app's.
bool DrawAppWindowOkayUI(const char* title, bool* open, const char* itemId, const std::function<void(float x, float y, float w, float h)>& frame,
                         float firstW, float firstH, const std::function<void(const std::string& path)>& drop) {
    return PanelWindow(title, open, itemId, [&](ImVec2 p, ImVec2 a) {
        if (frame) frame(p.x, p.y, a.x, a.y);
    }, firstW, firstH);
}

} // namespace oked
