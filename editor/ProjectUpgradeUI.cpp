// The editor's windows for "upgrade project": the version question, the progress and the result, File > Project Upgrade...
// (re-run and restore from a backup). The decisions are okayup::ProjectGate's and the work okayup::UpgradeJob's
// (ProjectUpgrade.hpp, no ImGui); this file only draws them and wires them into the editor. See docs/project_upgrade.md.
#include "ProjectUpgradeUI.hpp"
#include "EditorCommon.hpp"
#include "EditorWidgets.hpp"
#include "ConsolePanel.hpp"
#include "ProjectUpgrade.hpp"
#include "ProjectState.hpp"
#include "AppRestart.hpp"

#include <optional>

namespace oked {

// defined in main.cpp
void ShowErrorPopup(const std::string& title, const std::string& text);
void RunAfterSavePrompt(okay::editor::EditorState& ed, const std::string& doing, std::function<void()> action, std::function<void()> onCancel = {});
namespace extide { void RevealInFiles(const std::string& path); }

ProjectUpgradeHooks g_upgradeHooks;

namespace {
namespace fs = std::filesystem;

okayup::ProjectGate g_gate;
bool g_keepFlag = false;                          // --keep-version was on this process's command line
std::optional<okay::restart::Request> g_exitLaunch;   // an editor to start once this process is gone
bool g_openWindow = false;                        // File > Project Upgrade... asked for
std::string g_restoreSummary;                     // the "Restored" window's text ("" = not showing)
okayup::BackupInfo g_restoreAsk;                  // the backup the confirm window is about
bool g_restoreAskOpen = false;
bool g_exitNow = false;                           // the editor leaves now (Keep started another one, or a restore asked for a restart)

const ImVec4 kRed(0.96f, 0.55f, 0.50f, 1.0f), kAmber(0.95f, 0.80f, 0.35f, 1.0f), kGreen(0.45f, 0.82f, 0.56f, 1.0f);

std::string Plural(int n, const char* one, const char* many) { return std::to_string(n) + " " + (n == 1 ? one : many); }

// A long path keeps its end (the part that differs).
std::string FitEnd(const std::string& s, float maxW) {
    if (ImGui::CalcTextSize(s.c_str()).x <= maxW) return s;
    const std::string dots = "\xE2\x80\xA6";
    std::size_t cut = 0;
    while (cut < s.size()) {
        for (int n = 0; n < 3 && cut < s.size(); ++n) {
            ++cut;
            while (cut < s.size() && ((unsigned char)s[cut] & 0xC0) == 0x80) ++cut;
        }
        if (ImGui::CalcTextSize((dots + s.substr(cut)).c_str()).x <= maxW) break;
    }
    return dots + s.substr(cut);
}

bool Accent(const char* label, float w = 0.0f) {
    ImGui::PushStyleColor(ImGuiCol_Button,        AccentCol(0.85f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, AccentCol(1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  AccentCol(0.75f));
    ImGui::PushStyleColor(ImGuiCol_Text,          ImVec4(1, 1, 1, 1));
    const bool b = ImGui::Button(label, ImVec2(w, 0));
    ImGui::PopStyleColor(4);
    return b;
}

// A button sized to its text, put on the same line when it fits and on the next one when it doesn't.
bool Btn(const char* label, bool& first, bool primary = false, bool disabled = false) {
    const float w = ImGui::CalcTextSize(label, nullptr, true).x + ImGui::GetStyle().FramePadding.x * 2.0f + 14.0f;
    if (!first) {
        const float sp = ImGui::GetStyle().ItemSpacing.x;
        const float right = ImGui::GetWindowPos().x + ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x;
        if (ImGui::GetItemRectMax().x + sp + w <= right) ImGui::SameLine(0, sp);
    }
    first = false;
    ImGui::BeginDisabled(disabled);
    const bool pressed = primary ? Accent(label, w) : ImGui::Button(label, ImVec2(w, 0));
    ImGui::EndDisabled();
    return pressed;
}

void OpenFolder(const std::string& dir) { if (!dir.empty()) extide::RevealInFiles(dir); }

std::vector<std::string> OutputDirs(const std::string& projectDir) {
    std::vector<std::string> out;
    auto add = [&](const std::string& d) {
        if (d.empty()) return;
        fs::path p(d);
        out.push_back(p.is_absolute() ? p.string() : (fs::path(projectDir) / p).string());
        if (!p.is_absolute()) out.push_back(okayproj::Absolute(p).string());   // the builder resolves it against the working folder
    };
    std::string text;
    if (okayproj::ReadText(BuildProfilesFile(projectDir), text)) for (const BuildProfile& p : ParseBuildProfiles(text)) add(p.outputDir);
    BuildProfile live; std::string sel;
    if (okayproj::ReadText(BuildLiveFile(projectDir), text) && ParseBuildLive(text, live, sel)) add(live.outputDir);
    return out;
}

okayup::GateEnv MakeEnv(const std::vector<std::string>& args) {
    okayup::GateEnv env;
    env.running = RunningVersion();
    env.launcherDir = [] {
        const fs::path self = okay::restart::CurrentExecutable();
        return self.empty() ? fs::path() : okayver::LauncherDirOf(self.parent_path());
    };
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--launcher" && i + 1 < args.size()) { env.forwardArgs.push_back(args[i]); env.forwardArgs.push_back(args[i + 1]); ++i; }
        else if (args[i].rfind("--launcher=", 0) == 0 || args[i] == "--dev") env.forwardArgs.push_back(args[i]);
    }
    env.legacySettings = [] { std::string t; okayproj::ReadText(okayproj::SettingsFileName(), t); return t; };
    env.outputDirs = OutputDirs;
    return env;
}

// The editor to start after this one has closed. `args` are the arguments after the program.
void QueueLaunch(const std::string& exe, const std::vector<std::string>& args, const std::string& workDir) {
    okay::restart::Request req;
    req.executable = fs::path(exe);
    req.arguments = args;
    req.workingDirectory = fs::path(workDir);
    req.waitForPid = okay::restart::CurrentPid();
    std::error_code ec;
    req.logFile = fs::temp_directory_path(ec) / "okayspace_editor_launch.log";
    g_exitLaunch = std::move(req);
}

// The launcher's exe when this editor sits in a launcher's folder (Tools/, or a kept copy under Versions/<v>/Tools); "" when there is none.
fs::path LauncherExe() {
    const fs::path self = okay::restart::CurrentExecutable();
    if (self.empty()) return {};
#if defined(_WIN32)
    const fs::path p = okayver::LauncherDirOf(self.parent_path()) / "OkaySpace.exe";
#else
    const fs::path p = okayver::LauncherDirOf(self.parent_path()) / "OkaySpace";
#endif
    std::error_code ec;
    return fs::is_regular_file(p, ec) ? p : fs::path();
}
// Show the launcher's Versions page with `version` highlighted (a launcher that is already running comes forward instead of a second one).
void OpenLauncherVersions(const std::string& version) {
    const fs::path exe = LauncherExe();
    if (exe.empty()) return;
    okay::restart::Request req;
    req.executable = exe;
    req.arguments = {"--versions"};
    if (!okayver::NormalizeVersion(version).empty()) req.arguments.push_back(okayver::NormalizeVersion(version));
    req.workingDirectory = exe.parent_path();
    std::error_code ec;
    req.logFile = fs::temp_directory_path(ec) / "okayspace_editor_launch.log";
    std::string err;
    if (!okay::restart::StartHelper(req, err)) std::fprintf(stderr, "Couldn't open the launcher: %s\n", err.c_str());
}

std::string FirstSceneOf(const std::string& projectDir) {
    std::error_code ec;
    for (fs::recursive_directory_iterator it(fs::path(projectDir) / "Assets", ec), end; !ec && it != end; it.increment(ec))
        if (it->is_regular_file(ec) && it->path().extension() == ".okayscene") return it->path().string();
    return {};
}

// ---- the question, the progress, the result ------------------------------------------------------------------------------

void Heading(const char* text, const ImVec4& col) {
    ImGui::PushStyleColor(ImGuiCol_Text, col);
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(0, 2));
}

void DrawGatePrompt() {
    using okayup::PromptKind;
    using okayup::Choice;
    const okayup::Prompt& pr = g_gate.prompt();
    const bool wanted = pr.kind != PromptKind::None;
    if (wanted && !ImGui::IsPopupOpen("Project version")) ImGui::OpenPopup("Project version");
    if (!wanted && !ImGui::IsPopupOpen("Project version")) return;
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(620, 0), ImGuiCond_Always);
    if (!ImGui::BeginPopupModal("Project version", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoTitleBar)) return;
    const std::string pv = pr.projectVersion, rv = pr.runningVersion;
    const std::string pvLabel = pv.empty() ? std::string() : "v" + pv;
    const bool esc = ImGui::IsKeyPressed(ImGuiKey_Escape, false);
    bool first = true;
    const float textW = ImGui::GetContentRegionAvail().x;

    switch (wanted ? pr.kind : PromptKind::None) {
        case PromptKind::Older: {
            ImGui::TextDisabled("PROJECT VERSION");
            Heading(("This project was made with " + pvLabel + ". This editor is v" + rv + ".").c_str(), ImVec4(1, 1, 1, 1));
            ImGui::TextDisabled("%s", FitEnd(pr.root, textW).c_str());
            ImGui::Dummy(ImVec2(0, 4));
            ImGui::TextWrapped("Upgrading brings the project's scenes and prefabs up to date for v%s. A complete backup copy of the project is made "
                               "first, next to it, so you can always go back.", rv.c_str());
            ImGui::Dummy(ImVec2(0, 2));
            ImGui::TextWrapped("Keeping %s does not touch the project at all: it opens with that version's editor instead.", okayver::LineLabel(pv).c_str());
            if (!pr.keepTarget.empty()) ImGui::TextColored(kGreen, "v%s is installed and would open it.", pr.keepTarget.c_str());
            else ImGui::TextColored(kAmber, "%s isn't installed on this computer.", okayver::LineLabel(pv).c_str());
            ImGui::Dummy(ImVec2(0, 8));
            if (Btn(("Upgrade project to v" + rv).c_str(), first, true)) g_gate.Answer(Choice::Upgrade);
            if (Btn(("Keep " + okayver::LineLabel(pv)).c_str(), first)) g_gate.Answer(Choice::Keep);
            if (Btn("Cancel", first) || esc) g_gate.Answer(Choice::Cancel);
            break;
        }
        case PromptKind::Unknown: {
            ImGui::TextDisabled("PROJECT VERSION");
            Heading(("This project has no version recorded (made before this feature). This editor is v" + rv + ".").c_str(), ImVec4(1, 1, 1, 1));
            ImGui::TextDisabled("%s", FitEnd(pr.root, textW).c_str());
            ImGui::Dummy(ImVec2(0, 4));
            ImGui::TextWrapped("There is no way to tell which version made it, so only upgrading is offered. A complete backup copy of the project is made first, "
                               "next to it, then its scenes and prefabs are brought up to date for v%s and the project is marked with that version.", rv.c_str());
            ImGui::Dummy(ImVec2(0, 8));
            if (Btn(("Upgrade project to v" + rv).c_str(), first, true)) g_gate.Answer(Choice::Upgrade);
            if (Btn("Cancel", first) || esc) g_gate.Answer(Choice::Cancel);
            break;
        }
        case PromptKind::Newer: {
            ImGui::TextDisabled("PROJECT VERSION");
            Heading(("This project was last saved by " + pvLabel + ", which is newer than this editor (v" + rv + ").").c_str(), kAmber);
            ImGui::TextDisabled("%s", FitEnd(pr.root, textW).c_str());
            ImGui::Dummy(ImVec2(0, 4));
            ImGui::TextWrapped("This editor may drop data it does not understand when it saves. Update OkaySpace to open it safely, or open it anyway: "
                               "a complete backup copy is made first, next to the project, and nothing in the project is changed by that.");
            ImGui::Dummy(ImVec2(0, 8));
            if (Btn("Cancel", first) || esc) g_gate.Answer(Choice::Cancel);
            if (Btn("Open anyway (a backup is made first)", first)) g_gate.Answer(Choice::OpenAnyway);
            if (Btn("Update OkaySpace", first, true)) g_gate.Answer(Choice::UpdateEditor);
            break;
        }
        case PromptKind::NotInstalled: {
            ImGui::TextDisabled("KEEP THE PROJECT'S VERSION");
            Heading(pr.message.c_str(), kAmber);
            ImGui::TextWrapped("Nothing was changed. The launcher keeps earlier versions side by side, so a project can be opened with the editor it was made with.");
            ImGui::Dummy(ImVec2(0, 8));
            if (Btn("Back", first, true)) g_gate.Answer(Choice::Back);
            if (!LauncherExe().empty() && Btn("Open launcher", first)) OpenLauncherVersions(pr.projectVersion);   // the launcher's Versions page, that version highlighted
            if (Btn("Cancel", first) || esc) g_gate.Answer(Choice::Cancel);
            break;
        }
        case PromptKind::Working: {
            const okayup::Progress& p = pr.progress;
            ImGui::TextDisabled(pr.backupOnly ? "BACKING UP" : "UPGRADING");
            Heading(((pr.backupOnly ? "Making a backup of " : "Upgrading ") + pr.projectName).c_str(), ImVec4(1, 1, 1, 1));
            std::string line = okayup::PhaseName(p.phase);
            if (p.total > 0) line += "   " + std::to_string(p.done) + " / " + std::to_string(p.total) + " files";
            ImGui::TextUnformatted(line.c_str());
            ImGui::ProgressBar((float)p.fraction, ImVec2(-1, 0));
            ImGui::TextDisabled("%s", p.file.empty() ? " " : FitEnd(p.file, textW).c_str());
            ImGui::Dummy(ImVec2(0, 4));
            ImGui::TextWrapped(pr.backupOnly ? "Nothing in the project is changed." :
                               "Nothing in the project is changed until the backup is complete and checked. If anything fails, everything is put back.");
            ImGui::Dummy(ImVec2(0, 8));
            if (Btn("Cancel", first, false, p.phase == okayup::Phase::Applying)) g_gate.Answer(Choice::Cancel);
            break;
        }
        case PromptKind::Done: {
            const okayup::Result& r = pr.result;
            ImGui::TextDisabled(pr.backupOnly ? "BACKUP MADE" : "UPGRADED");
            if (pr.backupOnly) Heading("The backup is made. The project is opened as it is.", kGreen);
            else Heading(("Upgraded " + Plural(r.scenes, "scene", "scenes") + ", " + Plural(r.prefabs, "prefab", "prefabs") + "; " +
                          (r.rewritten == 0 ? std::string("nothing needed rewriting") : Plural(r.rewritten, "file", "files") + " rewritten") + ".").c_str(), kGreen);
            ImGui::TextWrapped("Backup at %s", r.backupDir.c_str());
            ImGui::TextDisabled("%s in the backup: a complete copy of the project.", Plural((int)r.files, "file", "files").c_str());
            if (!pr.backupOnly) ImGui::TextDisabled("The project is now marked as made with v%s.", rv.c_str());
            for (const std::string& w : r.warnings) { ImGui::TextColored(kAmber, "%s", FitEnd(w, textW).c_str()); }
            ImGui::Dummy(ImVec2(0, 8));
            if (Btn("Open project", first, true)) g_gate.Answer(Choice::Continue);
            if (Btn("Open backup folder", first)) OpenFolder(r.backupDir);
            break;
        }
        case PromptKind::Failed: {
            const okayup::Result& r = pr.result;
            ImGui::TextDisabled(pr.backupOnly ? "BACKUP FAILED" : "UPGRADE STOPPED");
            Heading(pr.backupOnly ? "The backup could not be made, so the project was not opened." : "The upgrade could not be finished.", kRed);
            ImGui::PushStyleColor(ImGuiCol_Text, kRed);
            ImGui::TextWrapped("%s", r.error.c_str());
            ImGui::PopStyleColor();
            ImGui::Dummy(ImVec2(0, 4));
            if (!r.restoreNote.empty()) ImGui::TextWrapped("%s", r.restoreNote.c_str());
            if (!r.backupDir.empty()) ImGui::TextWrapped("The backup is at %s", r.backupDir.c_str());
            ImGui::Dummy(ImVec2(0, 8));
            if (Btn("Close", first, true) || esc) g_gate.Answer(Choice::Close);
            if (!r.backupDir.empty() && Btn("Open backup folder", first)) OpenFolder(r.backupDir);
            break;
        }
        case PromptKind::None: break;
    }
    if (g_gate.prompt().kind == PromptKind::None) ImGui::CloseCurrentPopup();   // answered (or finished by itself): the window goes with it
    ImGui::EndPopup();
}

// ---- File > Project Upgrade... ------------------------------------------------------------------------------------------

void DoRestore(okay::editor::EditorState& ed, const okayup::BackupInfo& b) {
    const std::string dir = ed.projectDir();
    const okayup::RestoreResult r = okayup::RestoreBackup(b, dir);
    if (!r.ok) { ShowErrorPopup("Couldn't restore the backup", r.error); return; }
    // The files on disk are the old ones now: nothing in memory may be written over them, and the editor starts again on the
    // project so it asks the version question afresh.
    if (g_upgradeHooks.reloadProjectFiles) g_upgradeHooks.reloadProjectFiles();
    ed.MarkClean();
    ConsoleLog("Restored " + b.name + ": " + std::to_string(r.restored) + " files.");
    std::string scene = ed.path();
    std::error_code ec;
    if (scene.empty() || !fs::exists(scene, ec)) scene = FirstSceneOf(dir);
    std::vector<std::string> args;
    const std::vector<std::string>& cur = okay::restart::CurrentArguments();
    for (std::size_t i = 0; i < cur.size(); ++i) {
        const std::string& a = cur[i];
        if ((a == "--open" || a == "--new-project" || a == "--template") && i + 1 < cur.size()) { ++i; continue; }
        if (a.rfind("--open=", 0) == 0 || a.rfind("--new-project=", 0) == 0 || a.rfind("--template=", 0) == 0 || a.rfind("--keep-version", 0) == 0) continue;
        args.push_back(a);
    }
    if (!scene.empty()) { args.push_back("--open"); args.push_back(scene); }
    const fs::path self = okay::restart::CurrentExecutable();
    QueueLaunch(self.string(), args, fs::current_path(ec).string());
    g_restoreSummary = r.note;
}

void DrawUpgradeWindow(okay::editor::EditorState& ed) {
    if (g_openWindow) { g_openWindow = false; ImGui::OpenPopup("Project Upgrade"); }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(660, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSizeConstraints(ImVec2(660, 0), ImVec2(660, ImGui::GetMainViewport()->Size.y - 24.0f));
    if (!ImGui::BeginPopupModal("Project Upgrade", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings)) return;
    const std::string dir = ed.projectDir();
    const float textW = ImGui::GetContentRegionAvail().x;
    const std::string running = RunningVersion();
    const std::string rv = okayver::NormalizeVersion(running);
    const okayver::Stamp st = okayver::ReadProjectStamp(dir);
    const okayver::Decision d = okayver::Decide(st.version, running);
    ImGui::TextDisabled("PROJECT");
    ImGui::TextUnformatted(fs::path(dir).filename().string().c_str());
    ImGui::TextDisabled("%s", FitEnd(dir, textW).c_str());
    ImGui::Dummy(ImVec2(0, 4));
    ImGui::TextDisabled("Last saved by"); ImGui::SameLine(130); ImGui::TextUnformatted(st.version.empty() ? "no version recorded" : ("v" + st.version).c_str());
    ImGui::TextDisabled("This editor");   ImGui::SameLine(130); ImGui::TextUnformatted(rv.empty() ? "a development build (no version)" : ("v" + rv).c_str());
    switch (d) {
        case okayver::Decision::Same: ImGui::TextColored(kGreen, "Up to date."); break;
        case okayver::Decision::SamePatchLine: ImGui::TextColored(kGreen, "Same release line (%s): nothing to upgrade. The next save marks it v%s.", okayver::LineLabel(rv).c_str(), rv.c_str()); break;
        case okayver::Decision::Older: ImGui::TextColored(kAmber, "Made with an older version."); break;
        case okayver::Decision::Newer: ImGui::TextColored(kAmber, "Last saved by a newer version than this editor."); break;
        case okayver::Decision::Unknown: ImGui::TextColored(kAmber, "No version recorded."); break;
        case okayver::Decision::DevBuild: ImGui::TextDisabled("A development build never asks, and has no version to upgrade to."); break;
    }
    ImGui::Dummy(ImVec2(0, 6));
    ImGui::TextWrapped("Upgrading makes a complete backup copy of the project next to it, checks every scene and prefab, rewrites only the files that "
                       "need it, and marks the project with this version. It does it all or none of it.");
    ImGui::Dummy(ImVec2(0, 4));
    const bool canUpgrade = !rv.empty() && d != okayver::Decision::Newer && !g_gate.Busy();
    ImGui::BeginDisabled(!canUpgrade);
    const bool upgrade = Accent((std::string("Upgrade project") + (rv.empty() ? "" : " to v" + rv) + "...").c_str());
    ImGui::EndDisabled();
    if (upgrade) {
        const std::string scene = ed.path().empty() ? (fs::path(dir) / "Assets" / "project.okayscene").string() : ed.path();   // only its folder matters here
        ImGui::CloseCurrentPopup();
        RunAfterSavePrompt(ed, "upgrading the project", [&ed, scene] {
            g_gate.Upgrade(scene, [&ed] {   // the scene on disk may have been rewritten: open it again
                const std::string open = ed.path();
                std::error_code ec;
                std::string err;
                if (!open.empty() && fs::exists(open, ec) && !ed.Load(open, &err)) ShowErrorPopup("Couldn't reopen the scene", err);
            });
        });
    }
    if (!canUpgrade && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip(rv.empty() ? "A development build has no version number to upgrade to." : "A project from a newer version is not upgraded by an older editor.");

    ImGui::Dummy(ImVec2(0, 8));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 4));
    ImGui::TextDisabled("BACKUPS NEXT TO THIS PROJECT");
    const std::vector<okayup::BackupInfo> list = okayup::ListBackups(dir);
    if (list.empty()) ImGui::TextDisabled("  None yet. One is made every time you upgrade.");
    ImGui::BeginChild("##backups", ImVec2(0, list.empty() ? 0.0f : std::min(206.0f, (float)list.size() * 64.0f + 6.0f)));
    const float rowW = ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ScrollbarSize;
    for (std::size_t i = 0; i < list.size(); ++i) {
        const okayup::BackupInfo& b = list[i];
        ImGui::PushID((int)i);
        ImGui::BeginGroup();
        ImGui::TextUnformatted(FitEnd(b.name, rowW - 224).c_str());
        std::string sub;
        if (!b.restorable) sub = "Not made by this editor: open the folder to copy files by hand.";
        else if (b.mode == "backup") sub = "A safety copy made before opening a newer project.  " + Plural((int)b.files, "file", "files");
        else sub = (b.fromVersion.empty() ? std::string("unknown version") : "v" + b.fromVersion) + " to v" + b.toVersion + "  \xC2\xB7  " +
                   Plural((int)b.files, "file", "files") + "  \xC2\xB7  " + Plural((int)(b.changed.size()), "file", "files") + " rewritten";
        ImGui::TextDisabled("%s", FitEnd(sub, rowW - 224).c_str());
        ImGui::EndGroup();
        ImGui::SameLine(rowW - 212);
        if (ImGui::Button("Open folder", ImVec2(104, 0))) OpenFolder(b.dir.string());
        ImGui::SameLine();
        ImGui::BeginDisabled(!b.restorable || b.mode == "backup" || g_gate.Busy());
        if (ImGui::Button("Restore...", ImVec2(96, 0))) { g_restoreAsk = b; g_restoreAskOpen = true; }
        ImGui::EndDisabled();
        ImGui::Dummy(ImVec2(0, 6));
        ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::Dummy(ImVec2(0, 6));
    if (ImGui::Button("Close", ImVec2(110, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) ImGui::CloseCurrentPopup();

    // the confirmation, on top
    if (g_restoreAskOpen) { g_restoreAskOpen = false; ImGui::OpenPopup("Restore from backup?"); }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(580, 0), ImGuiCond_Always);
    if (ImGui::BeginPopupModal("Restore from backup?", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings)) {
        const okayup::BackupInfo& b = g_restoreAsk;
        ImGui::TextWrapped("Put back the %s the upgrade rewrote (scenes, prefabs and the project file) from \"%s\"?", Plural((int)b.changed.size(), "file", "files").c_str(), b.name.c_str());
        ImGui::Dummy(ImVec2(0, 2));
        ImGui::TextWrapped("Changes you made to those files since the upgrade are not lost: each one is kept in the project's .okay/backups folder first. "
                           "Files you made since are left alone. The editor then restarts on the project and asks about its version again.");
        ImGui::Dummy(ImVec2(0, 8));
        bool first = true;
        if (Btn("Restore", first, true)) {
            ImGui::CloseCurrentPopup();
            const okayup::BackupInfo pick = b;
            RunAfterSavePrompt(ed, "restoring the backup", [&ed, pick] { DoRestore(ed, pick); });
        }
        if (Btn("Cancel", first) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::EndPopup();
}

void DrawRestored() {
    if (g_restoreSummary.empty()) return;
    if (!ImGui::IsPopupOpen("Backup restored")) ImGui::OpenPopup("Backup restored");
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(580, 0), ImGuiCond_Always);
    if (!ImGui::BeginPopupModal("Backup restored", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings)) return;
    Heading("The backup is restored.", kGreen);
    ImGui::TextWrapped("%s", g_restoreSummary.c_str());
    ImGui::Dummy(ImVec2(0, 4));
    ImGui::TextWrapped("The editor restarts now to open the project again.");
    ImGui::Dummy(ImVec2(0, 8));
    if (Accent("Restart editor", 160)) { g_restoreSummary.clear(); g_exitNow = true; ImGui::CloseCurrentPopup(); }
    ImGui::EndPopup();
}

}  // namespace

// ---- the calls main.cpp makes ---------------------------------------------------------------------------------------------

std::string RunningVersion() {
    const char* dev = std::getenv("OKAY_DEV");
    if (dev && dev[0] && dev[0] != '0')
        if (const char* f = std::getenv("OKAY_FAKE_VERSION"); f && *f) return f;   // development builds only: pretend to be another version
    return OKAY_ENGINE_VERSION;
}

void ProjectUpgradeInit(int argc, char** argv) {
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) if (argv[i]) args.emplace_back(argv[i]);
    g_gate.SetEnv(MakeEnv(args));
    std::string open, keepFor;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];
        if (a == "--open" && i + 1 < args.size()) open = args[++i];
        else if (a.rfind("--open=", 0) == 0) open = a.substr(7);
        else if (a == okayver::KeepVersionFlag()) g_keepFlag = true;
        else if (a.rfind(std::string(okayver::KeepVersionFlag()) + "=", 0) == 0) { g_keepFlag = true; keepFor = a.substr(std::strlen(okayver::KeepVersionFlag()) + 1); }
    }
    if (g_keepFlag && !open.empty()) g_gate.NoteKeepRequest(open, keepFor);
    okay::editor::EditorState::beforeLoad = [](const std::string& path, std::string* why) { return g_gate.MayLoad(path, why); };
}

bool ProjectMayOpenSilently(const std::string& scene) { return g_gate.MayLoad(scene, nullptr); }

void RunAfterProjectCheck(okay::editor::EditorState&, const std::string& scene, std::function<void()> go, std::function<void()> cancel) {
    g_gate.Open(scene, std::move(go), std::move(cancel));
}

void NoteProjectCreated(const std::string& root) { g_gate.MarkCleared(root); }

std::string StampedNewProjectText(const std::string& generated) {
    return okayver::ComposeProjectFile(generated, "", okayver::NormalizeVersion(RunningVersion()), okayup::IsKnownProjectKey);
}

std::string ProjectFileTextForSave(const std::string& path, const std::string& generated) {
    return okayup::ProjectFileText(path, generated, RunningVersion());
}

void StampProjectAfterSave(const std::string& projectDir) {
    std::string err;
    if (!okayup::StampProject(projectDir, RunningVersion(), &err)) ConsoleLog("Couldn't mark the project with this version: " + err, 1);
}

std::string EditorVersionLabel(const std::string& projectDir) {
    static std::string cachedDir, cachedLabel;      // asked every frame: the disk is only looked at when the project changes
    static unsigned cachedRevision = ~0u;
    if (cachedRevision == g_gate.revision() && cachedDir == projectDir && !cachedLabel.empty()) return cachedLabel;
    std::string s = okayver::DisplayVersion(RunningVersion());
    if (!projectDir.empty() && g_gate.IsKept(projectDir)) s += " (kept)";
    cachedDir = projectDir; cachedLabel = s; cachedRevision = g_gate.revision();
    return s;
}

bool ProjectGateBusy() { return g_gate.Busy(); }

void NoteKeepVersionHandoff(const std::string& scene, const std::string& version) { g_gate.NoteKeepRequest(scene, version); }

bool KeepVersionRequested() { return g_keepFlag; }

void ProjectUpgradeDraw(okay::editor::EditorState& ed) {
    g_gate.Tick(std::chrono::milliseconds(10));
    DrawGatePrompt();
    DrawUpgradeWindow(ed);
    DrawRestored();
    // what the answers asked for (taken after the windows, so an answer is acted on in the frame it was given)
    if (auto l = g_gate.TakeLaunch()) QueueLaunch(l->exe, l->args, l->workDir);
    if (g_gate.TakeQuitRequest()) g_exitNow = true;
    if (g_gate.TakeUpdateRequest() && g_upgradeHooks.openUpdateDialog) g_upgradeHooks.openUpdateDialog();
}

bool ProjectUpgradeExitRequested() { return g_exitNow; }

void ProjectUpgradeMenuItem(okay::editor::EditorState& ed) {
    if (ImGui::MenuItem("Project Upgrade...", nullptr, false, !ed.projectDir().empty() && !ed.isPlaying())) g_openWindow = true;
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Upgrade this project to the version of this editor (a backup is made first),\nor restore it from one of its backups.");
}

void ProjectUpgradeAtExit() {
    if (!g_exitLaunch) return;
    std::string err;
    if (!okay::restart::StartHelper(*g_exitLaunch, err)) std::fprintf(stderr, "Couldn't start %s: %s\n", g_exitLaunch->executable.string().c_str(), err.c_str());
    g_exitLaunch.reset();
}

}  // namespace oked
