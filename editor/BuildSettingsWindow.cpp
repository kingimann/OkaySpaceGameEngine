// The Build Settings window's drawing: the page list with search and changed dots, the page header, the Game and
// Advanced pages, the summary and checks, the profile extras and the history. The logic is BuildSettingsModel.hpp.
#include "BuildSettingsWindow.hpp"
#include "ConsolePanel.hpp"   // ConsoleLog

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace oked {
namespace {
using namespace bsm;

std::vector<HistoryEntry> s_hist;      // the project's history, loaded when the project changes or a build is recorded
std::string s_histFor;
bool s_histLoaded = false;
const std::vector<HistoryEntry>& History(const std::string& projectDir) {
    if (!s_histLoaded || s_histFor != projectDir) { s_hist = LoadHistory(projectDir); s_histFor = projectDir; s_histLoaded = true; }
    return s_hist;
}

std::vector<std::string> s_log;   // the last build's log lines
const ImVec4 kWarn(0.95f, 0.80f, 0.35f, 1), kErr(1.0f, 0.5f, 0.45f, 1), kInfo(0.6f, 0.75f, 0.95f, 1), kDot(0.35f, 0.75f, 1.0f, 1);

void Tip(const char* t) { if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", t); }
void Header(const char* t) { ImGui::Spacing(); ImGui::TextDisabled("%s", t); ImGui::Separator(); }

// A text box on a fixed char array.
template <std::size_t N> bool Text(const char* label, char (&buf)[N], const char* hint, float w = 300) {
    ImGui::SetNextItemWidth(w);
    return ImGui::InputTextWithHint(label, hint, buf, N);
}
}  // namespace

void DrawBuildNav(BuildWindowCtx& c, int& page) {
    static char search[64] = "";
    BuildSettings& b = *c.settings;
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##bsearch", "Search settings...", search, sizeof search);
    Tip("Type a word such as fps, shadow or volume to find a setting on any page.");
    ImGui::Spacing();
    const float rowH = ImGui::GetTextLineHeight() + 12;
    if (search[0]) {
        const std::vector<int> hits = Search(search, c.platform);
        if (hits.empty()) ImGui::TextDisabled("Nothing matches \"%s\".", search);
        for (int i : hits) {
            const SettingInfo& s = Registry()[i];
            std::string row = std::string(s.label) + "  (" + PageName(s.page) + ")";
            ImGui::PushID(i);
            if (ImGui::Selectable(row.c_str(), false, 0, ImVec2(0, ImGui::GetTextLineHeight() + 6))) { page = s.page; search[0] = '\0'; }
            ImGui::PopID();
            Tip("Click to go to this page.");
        }
        return;
    }
    for (int p : PagesFor(c.platform)) {
        const bool changed = p != kScenes && PageChanged(p, b);
        if (ImGui::Selectable((std::string(PageName(p)) + "##pg" + std::to_string(p)).c_str(), page == p, 0, ImVec2(0, rowH))) page = p;
        Tip(PageTip(p));
        if (changed) {   // a dot at the right edge: something on this page is not the default
            const ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
            ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(mx.x - 10, (mn.y + mx.y) * 0.5f), 3.5f, ImGui::GetColorU32(kDot));
        }
    }
    ImGui::Spacing();
    ImGui::TextDisabled("A dot means the page has\nsettings you changed.");
}

void DrawBuildPageHeader(BuildWindowCtx& c, int page) {
    BuildSettings& b = *c.settings;
    ImGui::TextDisabled("%s", PageTip(page));
    if (page == kScenes || page == kXbox) return;
    const int n = ChangedCount(page, b);
    ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - 170));
    ImGui::BeginDisabled(n == 0);
    if (ImGui::Button("Reset page to defaults")) ImGui::OpenPopup("##resetpage");
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        if (n == 0) ImGui::SetTooltip("Everything on this page already has its default.");
        else {
            std::string t = "Changed on this page:";
            for (const std::string& l : ChangedLabels(page, b)) t += "\n  " + l;
            ImGui::SetTooltip("%s", t.c_str());
        }
    }
    if (ImGui::BeginPopup("##resetpage")) {
        ImGui::Text("Put %d setting%s on this page back to the default?", n, n == 1 ? "" : "s");
        if (ImGui::Button("Reset")) { ResetPage(page, b); ImGui::CloseCurrentPopup(); }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

bool DrawBuildPageExtra(BuildWindowCtx& c, int page) {
    BuildSettings& b = *c.settings;
    if (page == kGame) {
        Header("Speed and physics");
        ImGui::SetNextItemWidth(220);
        ImGui::SliderFloat("Game speed", &b.timeScale, 0.1f, 4.0f, "%.2fx");
        Tip("How fast the whole game runs. 1 is normal, 0.5 is slow motion, 2 is twice as fast.");
        b.timeScale = std::max(0.0f, std::min(100.0f, b.timeScale));
        ImGui::SetNextItemWidth(220);
        ImGui::InputInt("Most physics steps per frame", &b.maxFixedSteps);
        b.maxFixedSteps = std::max(0, std::min(1000, b.maxFixedSteps));
        Tip("When the game falls behind, physics catches up with at most this many small steps in one frame.\n0 means the engine's own limit (5). A lower number keeps a slow computer from spiralling.");
        Header("Sound at the start");
        ImGui::SetNextItemWidth(220); ImGui::SliderFloat("Music volume", &b.musicVolume, 0.0f, 1.0f, "%.2f");
        Tip("How loud music starts. The player can change it later in the game's settings; their choice is kept.");
        ImGui::SetNextItemWidth(220); ImGui::SliderFloat("Sound effects volume", &b.sfxVolume, 0.0f, 1.0f, "%.2f");
        Tip("How loud sound effects start. The player can change it later in the game's settings.");
        Header("Language");
        Text("Starting language##dl", b.defaultLanguage, "computer's language (for example en, de, fr)", 120);
        Tip("The language code the game uses the first time it runs, if your strings table has it.\nA language the player picked earlier always wins. Leave empty to follow the player's computer.");
        Header("Log");
        {
            static const char* names[] = {"Default", "Everything (trace)", "Normal (info)", "Warnings", "Errors only"};
            static const char* words[] = {"", "trace", "info", "warn", "error"};
            int cur = 0;
            for (int i = 1; i < 5; ++i) if (std::strcmp(b.logLevel, words[i]) == 0) cur = i;
            ImGui::SetNextItemWidth(220);
            if (ImGui::Combo("Log detail", &cur, names, 5)) std::snprintf(b.logLevel, sizeof b.logLevel, "%s", words[cur]);
            Tip("How much the game writes to its log. Default leaves it as the engine decides.");
        }
        ImGui::Checkbox("Write a log file (game.log)", &b.logToFile);
        Tip("Saves the log in the game's save folder, so you can ask a tester to send it. It never grows without limit: old copies are rotated.");
        Header("About and environment");
        Text("Description##desc", b.description, "one line about your game", 420);
        Tip("Written into the game's config and its start-up log line.");
        Text("Environment variables##env", b.envVars, "NAME=VALUE;OTHER=VALUE", 420);
        Tip("Set before the game starts, separated by semicolons. Variables that change which programs or libraries load (PATH, LD_PRELOAD ...) are refused.");
        return true;
    }
    if (page == kAdvanced) {
        Header("Commands before and after the build");
        ImGui::Checkbox("Allow build commands", &b.allowBuildCommands);
        Tip("Off by default. The two commands below run on YOUR computer with your rights, so turn this on only for commands you wrote or trust.\nA Dry Run never runs them.");
        ImGui::BeginDisabled(!b.allowBuildCommands);
        Text("Before the build##pre", b.preBuildCommand, "for example: python tools/make_data.py", 460);
        Tip("Runs in the project folder before anything is built. If it fails (exit code not 0) the build stops.\nYou can use {output} {name} {version} {build} {project}.");
        Text("After the build##post", b.postBuildCommand, "for example: copy-to-share.bat {output}", 460);
        Tip("Runs in the project folder after a good build. You can use {output} {name} {version} {build} {project}.");
        ImGui::EndDisabled();
        if (!b.allowBuildCommands) ImGui::TextColored(kInfo, "Commands are switched off, so they will not run.");
        else ImGui::TextColored(kWarn, "These commands run on your computer when you press Build.");
        Header("Version control");
        ImGui::TextWrapped("Make a .gitignore in the project so build output, logs and editor scratch files stay out of version control. Lines you already have are kept.");
        ImGui::BeginDisabled(c.projectDir.empty());
        if (ImGui::Button("Create or update .gitignore")) {
            namespace fs = std::filesystem;
            std::error_code ec;
            std::string rel;
            if (c.dir && c.dir[0]) { fs::path r = fs::relative(fs::path(c.dir), fs::path(c.projectDir), ec); if (!ec) rel = r.generic_string(); }
            std::string existing; okayproj::ReadText((fs::path(c.projectDir) / ".gitignore").string(), existing);
            int added = 0;
            const std::string merged = MergeIgnore(existing, IgnoreLines(rel), &added);
            std::string err;
            if (added == 0) ConsoleLog(".gitignore already has everything.");
            else if (okayproj::WriteTextAtomic((fs::path(c.projectDir) / ".gitignore").string(), merged, &err)) ConsoleLog("Added " + std::to_string(added) + " line(s) to .gitignore");
            else ConsoleLog("Could not write .gitignore: " + err, 2);
        }
        ImGui::EndDisabled();
        Tip("Writes <project>/.gitignore. Nothing is removed from an existing one.");
        Header("Last build log");
        if (s_log.empty()) ImGui::TextDisabled("Nothing built yet in this session. The log of each build appears here.");
        else {
            ImGui::BeginChild("##blog", ImVec2(0, std::min(180.0f, 12.0f + 20.0f * (float)s_log.size())), ImGuiChildFlags_Borders);
            for (const std::string& l : s_log) {
                const LogKind k = ClassifyLogLine(l);
                const ImVec4 col = k == LogKind::Error ? kErr : k == LogKind::Warning ? kWarn : k == LogKind::Good ? ImVec4(0.5f, 0.9f, 0.55f, 1) : ImGui::GetStyleColorVec4(ImGuiCol_Text);
                ImGui::PushStyleColor(ImGuiCol_Text, col);
                ImGui::TextWrapped("%s", l.c_str());
                ImGui::PopStyleColor();
            }
            ImGui::EndChild();
            ImGui::BeginDisabled(!c.openFolder || !c.dir || !c.dir[0]);
            if (ImGui::Button("Open output folder##log")) c.openFolder(c.dir);
            ImGui::EndDisabled();
        }
        Header("Build history");
        const std::vector<HistoryEntry>& h = History(c.projectDir);
        if (h.empty()) ImGui::TextDisabled("No builds yet. Each finished build is listed here with its time and size.");
        else {
            ImGui::BeginChild("##bhist", ImVec2(0, std::min(200.0f, 24.0f + 20.0f * (float)h.size())), ImGuiChildFlags_Borders);
            for (auto it = h.rbegin(); it != h.rend(); ++it) {
                ImGui::PushStyleColor(ImGuiCol_Text, it->ok ? ImGui::GetStyleColorVec4(ImGuiCol_Text) : kErr);
                ImGui::Text("%s   v%s (build %d)   %s   %s%s", it->time.c_str(), it->version.c_str(), it->buildNumber, it->platform.c_str(), HumanSize(it->bytes).c_str(), it->ok ? "" : "   FAILED");
                ImGui::PopStyleColor();
            }
            ImGui::EndChild();
            ImGui::BeginDisabled(!c.openFolder || h.back().output.empty());
            if (ImGui::Button("Open output folder##hist")) c.openFolder(h.back().output);
            ImGui::EndDisabled();
        }
        return true;
    }
    return false;
}

void DrawBuildPageMore(BuildWindowCtx& c, int page) {
    BuildSettings& b = *c.settings;
    if (page == kGraphics) {
        Header("3D renderer and effects");
        static const char* names[] = {"Automatic (best for the computer)", "Direct3D 11 only", "OpenGL only", "Software only"};
        ImGui::SetNextItemWidth(300);
        ImGui::Combo("3D renderer", &b.renderer, names, 4);
        Tip("Automatic tries Direct3D 12 (if chosen above), Direct3D 11, then OpenGL, then software.\nThe other choices try only that one. Any choice that cannot start falls back to software, so the game always shows.");
        ImGui::SetNextItemWidth(220);
        ImGui::InputInt("Particle limit", &b.particleCap, 50, 500);
        b.particleCap = std::max(0, std::min(100000, b.particleCap));
        Tip("The most particles any one effect may keep alive. 0 means no extra limit. Lower it for weaker computers.");
    }
    if (page == kOutput) {
        Header("Files left out of the build");
        ImGui::TextWrapped("Right-click a file in the Project panel and choose \"Exclude from Build\", or type paths here separated by semicolons. A path ending in / leaves out a whole folder. Files here are never copied into the game.");
        Text("##exclude", b.excludeList, "Assets/Old/;Assets/Textures/big.png", 520);
        Tip("Paths are relative to the project folder. Not case sensitive.");
        ImGui::SameLine();
        if (ImGui::Button("Clear##excl")) b.excludeList[0] = '\0';
        const auto entries = ExcludeEntries(b.excludeList);
        if (!entries.empty()) ImGui::TextDisabled("%d entr%s. Use Dry Run to see the size of the build without them.", (int)entries.size(), entries.size() == 1 ? "y" : "ies");
    }
    if (page == kAudioInput) {
        Header("Controller and sound quality");
        ImGui::SetNextItemWidth(220);
        ImGui::SliderFloat("Gamepad dead zone", &b.gamepadDeadzone, 0.0f, 0.95f, "%.2f");
        b.gamepadDeadzone = std::max(0.0f, std::min(0.95f, b.gamepadDeadzone));
        Tip("How far a stick must move before the game notices. Raise it if sticks drift. 0.18 is the engine's usual value.");
        static const int rates[] = {0, 22050, 44100, 48000};
        static const char* rateNames[] = {"Default (44100 Hz)", "22050 Hz (smaller, duller)", "44100 Hz", "48000 Hz"};
        int cur = 0; for (int i = 0; i < 4; ++i) if (rates[i] == b.sampleRate) cur = i;
        ImGui::SetNextItemWidth(260);
        if (ImGui::Combo("Sound quality (sample rate)", &cur, rateNames, 4)) b.sampleRate = rates[cur];
        Tip("The rate the game asks the sound card for. The sound card may still use its own.");
    }
}

bool DrawBuildSummary(BuildWindowCtx& c) {
    BuildSettings& b = *c.settings;
    SummaryInput si; si.s = b; si.productName = c.name ? c.name : ""; si.outputDir = c.dir ? c.dir : ""; si.platform = c.platform; si.enabledScenes = c.enabledScenes;
    si.lastBytes = LastGoodSize(History(c.projectDir)); si.estimateBytes = c.estimateBytes;
    ValidateInput vi; vi.s = b; vi.productName = si.productName; vi.outputDir = si.outputDir; vi.platform = c.platform; vi.enabledScenes = c.enabledScenes; vi.openSceneSaved = c.openSceneSaved;
    {
        std::error_code ec;
        vi.iconMissing = b.icon[0] && !std::filesystem::exists(std::filesystem::path(b.icon), ec) && !std::filesystem::exists(std::filesystem::path(c.projectDir) / std::filesystem::path(b.icon), ec);
        vi.splashMissing = b.splash[0] && !std::filesystem::exists(std::filesystem::path(b.splash), ec) && !std::filesystem::exists(std::filesystem::path(c.projectDir) / std::filesystem::path(b.splash), ec);
    }
    std::vector<Issue> issues = Validate(vi);
    const bool errors = HasErrors(issues);
    ImGui::PushStyleColor(ImGuiCol_Text, errors ? kErr : issues.empty() ? ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled) : kWarn);
    const std::string head = std::string("What will happen") + (issues.empty() ? "" : "  -  " + std::to_string(issues.size()) + (issues.size() == 1 ? " thing to check" : " things to check"));
    const bool open = ImGui::CollapsingHeader((head + "###bsum").c_str(), ImGuiTreeNodeFlags_DefaultOpen);
    ImGui::PopStyleColor();
    if (!open) return errors;
    for (const std::string& l : BuildSummary(si)) ImGui::BulletText("%s", l.c_str());
    int fix = 0;
    for (const Issue& i : issues) {
        ImGui::PushID(fix++);
        const ImVec4 col = i.sev == Severity::Error ? kErr : i.sev == Severity::Warning ? kWarn : kInfo;
        ImGui::TextColored(col, "%s", i.sev == Severity::Error ? "Problem:" : i.sev == Severity::Warning ? "Check:" : "Note:");
        ImGui::SameLine();
        ImGui::PushTextWrapPos(ImGui::GetContentRegionMax().x - 150);
        ImGui::TextWrapped("%s", i.text.c_str());
        ImGui::PopTextWrapPos();
        if (i.fix != Fix::None) {
            ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetContentRegionMax().x - 140));
            if (ImGui::SmallButton((std::string("Fix: ") + i.fixLabel).c_str())) {
                std::string nm = c.name ? c.name : "";
                ApplyFix(i.fix, b, nm);
                if (c.name) std::snprintf(c.name, c.nameCap, "%s", nm.c_str());
            }
        }
        ImGui::PopID();
    }
    return errors;
}

void DrawBuildProfileExtras(BuildWindowCtx& c, std::vector<BuildProfile>& profiles, int& selected, const std::function<void()>& onChanged) {
    BuildSettings& b = *c.settings;
    ImGui::TextDisabled("Start from");
    ImGui::SameLine();
    for (BuildPreset p : {BuildPreset::Debug, BuildPreset::Release}) {
        if (ImGui::SmallButton(PresetName(p))) ApplyBuildPreset(b, p);
        Tip(PresetTip(p));
        ImGui::SameLine();
    }
    ImGui::SameLine(0, 16);
    ImGui::BeginDisabled(selected < 0 || selected >= (int)profiles.size());
    if (ImGui::SmallButton("Duplicate profile")) {
        const int n = DuplicateProfile(profiles, selected);
        if (n >= 0) { selected = n; if (onChanged) onChanged(); }
    }
    Tip("Make a copy of the chosen profile under a new name.");
    ImGui::SameLine();
    if (ImGui::SmallButton("Save as file")) {
        BuildProfile p = profiles[selected];
        p.settings = b; p.productName = c.name ? c.name : ""; p.outputDir = c.dir ? c.dir : ""; p.platform = c.platform;
        std::string err;
        if (SaveProfileFile(c.projectDir, p, &err)) ConsoleLog("Saved " + ProfileFileStem(p.name) + ".okaybuild in the project's BuildProfiles folder.");
        else ConsoleLog("Could not save the profile file: " + err, 2);
    }
    Tip("Writes the profile to <project>/BuildProfiles/<name>.okaybuild so it can be shared or kept in version control.");
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(c.projectDir.empty());
    if (ImGui::SmallButton("Load file...")) ImGui::OpenPopup("##loadokaybuild");
    ImGui::EndDisabled();
    Tip("Pick a .okaybuild file from the project's BuildProfiles folder.");
    if (ImGui::BeginPopup("##loadokaybuild")) {
        const std::vector<std::string> files = ListProfileFiles(c.projectDir);
        if (files.empty()) ImGui::TextDisabled("No .okaybuild files in BuildProfiles yet.");
        for (const std::string& f : files) {
            if (ImGui::Selectable(f.c_str())) {
                BuildProfile p;
                if (LoadProfileFile(c.projectDir, f, p)) {
                    const std::string base = p.name.empty() ? ProfileFileStem(f) : p.name;
                    p.name = base;
                    for (int i = 2; std::any_of(profiles.begin(), profiles.end(), [&](const BuildProfile& q) { return q.name == p.name; }); ++i) p.name = base + " " + std::to_string(i);
                    profiles.push_back(p); selected = (int)profiles.size() - 1;
                    b = p.settings;
                    if (c.name) std::snprintf(c.name, c.nameCap, "%s", p.productName.c_str());
                    if (c.dir) std::snprintf(c.dir, c.dirCap, "%s", p.outputDir.c_str());
                    if (onChanged) onChanged();
                } else ConsoleLog("Could not read " + f, 2);
            }
        }
        ImGui::EndPopup();
    }
}

void SetBuildLog(const std::string& status, const std::vector<std::string>& warnings) { s_log = BuildLogLines(status, warnings); }

void RecordBuildInHistory(const BuildWindowCtx& c, int buildNumber, std::uintmax_t bytes, bool ok) {
    HistoryEntry e;
    e.time = NowText(); e.version = c.settings->version; e.buildNumber = buildNumber;
    static const char* plat[] = {"Desktop", "Web", "Android", "Server", "Xbox"};
    e.platform = plat[std::max(0, std::min(4, c.platform))]; e.bytes = bytes; e.output = c.dir ? c.dir : ""; e.ok = ok;
    std::string err;
    if (!RecordBuild(c.projectDir, e, &err) && !c.projectDir.empty()) ConsoleLog("Could not save the build history: " + err, 1);
    s_histLoaded = false;
}

}  // namespace oked
