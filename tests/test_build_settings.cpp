// The Build Settings window's logic (editor/BuildSettingsModel.hpp) and the game.okayconfig extra lines the
// player reads (okay/Core/BuildConfigExtra.hpp): registry coverage, changed-from-default, page reset, search,
// presets, summary, validation and fixes, history, build commands, ignore file, profile files, and that the
// default settings write nothing new into the built game's config (old projects build as before).
#include "test_framework.hpp"
#include "../editor/BuildSettingsModel.hpp"

#include <fstream>
#include <regex>
#include <set>

namespace fs = std::filesystem;
using namespace oked;
using namespace oked::bsm;
namespace bp = okay::buildpipe;

namespace {
std::string WriteBuildProfileBlockText(const BuildSettings& b) { BuildProfile p; p.name = "x"; p.settings = b; std::ostringstream o; WriteBuildProfileBlock(o, p); return o.str(); }
std::set<std::string> Declared(const std::string& src) {
    std::set<std::string> out;
    const std::size_t a = src.find("struct BuildSettings {");
    const std::size_t b = src.find("\n};", a);
    std::istringstream in(src.substr(a, b - a));
    std::string line; std::getline(in, line);
    const std::regex typed("^\\s+(?:char|int|float|bool|std::string|std::vector<[A-Za-z]+>)\\s+(.*)$");
    const std::regex ident("([A-Za-z_][A-Za-z0-9_]*)");
    while (std::getline(in, line)) {
        const std::size_t c = line.find("//"); if (c != std::string::npos) line.erase(c);
        std::smatch m; if (!std::regex_match(line, m, typed)) continue;
        // "width = 1280, height = 720;" or "splashBg[3] = {0.05f, ...};": the names are the first word of each declarator
        std::string rest = m[1]; int depth = 0; std::string cur;
        for (char ch : rest) {
            if (ch == '{') ++depth;
            if (ch == '}') --depth;
            if (ch == ',' && depth == 0) { std::smatch w; if (std::regex_search(cur, w, ident)) out.insert(w[1]); cur.clear(); } else cur.push_back(ch);
        }
        std::smatch w; if (std::regex_search(cur, w, ident)) out.insert(w[1]);
    }
    return out;
}
}  // namespace

int main() {
    {
        RUN_SUITE("registry: every BuildSettings member is listed once or is on the unlisted list");
        std::string src; { std::ifstream f(fs::path(__FILE__).parent_path().parent_path() / "editor" / "ProjectState.hpp"); std::stringstream ss; ss << f.rdbuf(); src = ss.str(); }
        const std::set<std::string> decl = Declared(src);
        CHECK(decl.size() > 70);
        const BuildSettings d;
        std::set<std::string> seen(UnlistedMembers().begin(), UnlistedMembers().end());
        for (const SettingInfo& s : Registry()) {
            CHECK(!s.differs(d, d));
            CHECK(s.page >= 0 && s.page < kPageCount && s.label && s.label[0]);
            CHECK(seen.insert(s.member).second);                     // listed once
        }
        for (const auto& m : decl) if (!seen.count(m)) std::cerr << "    BuildSettings." << m << " is not in the registry\n";
        for (const auto& m : seen) if (!decl.count(m)) std::cerr << "    " << m << " is not a BuildSettings member\n";
        CHECK(seen == decl);
    }
    {
        RUN_SUITE("changed-from-default and reset page");
        BuildSettings b;
        for (int p = 0; p < kPageCount; ++p) CHECK(!PageChanged(p, b));
        b.width = 1920; b.vsync = false; b.timeScale = 0.5f; std::snprintf(b.logLevel, sizeof b.logLevel, "warn");
        CHECK(PageChanged(kDisplay, b) && PageChanged(kGame, b) && !PageChanged(kGraphics, b));
        CHECK(ChangedCount(kDisplay, b) == 2 && ChangedCount(kGame, b) == 2);
        const std::vector<std::string> lab = ChangedLabels(kDisplay, b);
        CHECK(lab.size() == 2);
        b.scenes.push_back({"a.okayscene", true}); b.buildNumber = 9;
        CHECK(ResetPage(kDisplay, b) == 2);
        CHECK(b.width == 1280 && b.vsync && b.timeScale == 0.5f);          // other pages untouched
        CHECK(b.scenes.size() == 1 && b.buildNumber == 9);                 // the list and the counter are never reset
        CHECK(ResetPage(kGame, b) == 2 && b.timeScale == 1.0f && !b.logLevel[0]);
        // Each page: change every one of its settings by a byte-level poke, reset, compare to the defaults.
        BuildSettings all; std::memset(all.company, 'x', 10); all.company[10] = 0; all.fpsCap = 99; all.antiCheat = 2; all.srvPort = 1;
        for (int p = 0; p < kPageCount; ++p) ResetPage(p, all);
        CHECK(WriteBuildProfileBlockText(all) == WriteBuildProfileBlockText(BuildSettings{}));
    }
    {
        RUN_SUITE("search");
        CHECK(Search("", 0).empty());
        auto labels = [](const std::vector<int>& v) { std::string s; for (int i : v) s += std::string(Registry()[i].label) + ";"; return s; };
        CHECK(labels(Search("fps", 0)).find("Frame rate cap") != std::string::npos);
        CHECK(labels(Search("FPS COUNTER", 0)).find("Show FPS counter") != std::string::npos);
        CHECK(Search("shadow", 0).size() >= 4);
        CHECK(Search("shadow distance", 0).size() == 1);
        CHECK(Search("shadow", 3).empty());                 // a server has no graphics page
        CHECK(Search("udp", 3).size() == 1 && Search("udp", 0).empty());
        CHECK(Search("zzzznothing", 0).empty());
    }
    {
        RUN_SUITE("pages per platform");
        CHECK(PagesFor(0).size() == 8 && PageShown(kGame, 0) && PageShown(kAdvanced, 0));
        CHECK(!PageShown(kGame, 1) && !PageShown(kGame, 3) && PageShown(kServer, 3) && !PageShown(kDisplay, 3));
        CHECK(PageShown(kXbox, 4) && !PageShown(kXbox, 0));
        for (int p = 0; p < kPageCount; ++p) CHECK(PageName(p)[0] && PageTip(p)[0]);
    }
    {
        RUN_SUITE("presets");
        BuildSettings b; b.width = 1000;
        ApplyBuildPreset(b, BuildPreset::Debug);
        CHECK(b.developmentBuild && b.showFps && !b.stripAssets && b.logToFile && std::string(b.logLevel) == "info" && b.width == 1000);
        ApplyBuildPreset(b, BuildPreset::Release);
        CHECK(!b.developmentBuild && !b.showFps && b.stripAssets && b.compress && !b.logToFile && !b.logLevel[0]);
        ApplyQuality(b, 0); CHECK(!b.shadows && !b.ssao && !b.bloom && b.antialias == 1);
        ApplyQuality(b, 3); CHECK(b.shadows && b.ssao && b.bloom && b.antialias == 2 && b.shadowResolution == 4096 && b.particleCap == 0);
        ApplyQuality(b, 0); CHECK(b.particleCap == 100);
        ApplyQuality(b, 99); CHECK(b.shadowResolution == 4096);
    }
    {
        RUN_SUITE("summary");
        SummaryInput in; in.productName = "Moon Hop"; in.outputDir = "C:/builds/moon"; in.enabledScenes = 3; in.lastBytes = 5u * 1024 * 1024;
        std::string all; for (const auto& l : BuildSummary(in)) all += l + "\n";
        CHECK(all.find("Moon Hop") != std::string::npos && all.find("3 scenes") != std::string::npos && all.find("5.0 MB") != std::string::npos);
        CHECK(all.find("C:/builds/moon") != std::string::npos);
        in.s.timeScale = 0.5f; std::snprintf(in.s.defaultLanguage, 16, "fr"); in.s.allowBuildCommands = true; std::snprintf(in.s.postBuildCommand, 260, "upload.bat");
        all.clear(); for (const auto& l : BuildSummary(in)) all += l + "\n";
        CHECK(all.find("0.5 times") != std::string::npos && all.find("\"fr\"") != std::string::npos && all.find("upload.bat") != std::string::npos);
        in.platform = 3; all.clear(); for (const auto& l : BuildSummary(in)) all += l + "\n";
        CHECK(all.find("game server") != std::string::npos);
        // wording rule: no engine brand names in plain text
        CHECK(all.find("Unity") == std::string::npos && all.find("Unreal") == std::string::npos);
    }
    {
        RUN_SUITE("validation and fixes");
        ValidateInput in; in.productName = "Good Game"; in.outputDir = "out"; in.enabledScenes = 1;
        CHECK(Validate(in).empty() || !HasErrors(Validate(in)));
        in.productName = "Bad:Name?"; in.s.width = 300; in.s.fullscreen = in.s.borderless = true; in.s.minWidth = 2000; in.s.minHeight = 100;
        std::snprintf(in.s.version, 32, "one"); in.s.timeScale = 0.0f; std::snprintf(in.s.logLevel, 16, "loud"); in.s.layout = 2; in.s.zipAfter = true;
        in.iconMissing = true; std::snprintf(in.s.envVars, 512, "OK=1;PATH=bad;9x=no");
        const std::vector<Issue> v = Validate(in);
        CHECK(HasErrors(v));
        int fixes = 0; for (const Issue& i : v) if (i.fix != Fix::None) ++fixes;
        CHECK(fixes >= 9);
        for (const Issue& i : v) ApplyFix(i.fix, in.s, in.productName);
        CHECK(in.productName == "Bad_Name_" && std::string(in.s.version) == "1.0.0" && !in.s.borderless && in.s.timeScale == 1.0f && !in.s.logLevel[0] && !in.s.zipAfter && !in.s.icon[0]);
        CHECK(in.s.width == 1280 && in.s.minWidth <= in.s.width && std::string(in.s.envVars) == "OK=1");
        in.iconMissing = false;
        const std::vector<Issue> v2 = Validate(in);
        CHECK(!HasErrors(v2));
        for (const Issue& i : v2) CHECK(i.fix == Fix::None || i.sev != Severity::Error);
        ValidateInput e; e.openSceneSaved = false;   // no name, no output, no scenes
        CHECK(Validate(e).size() >= 3 && HasErrors(Validate(e)));
        ValidateInput c; c.productName = "G"; c.outputDir = "o"; c.enabledScenes = 1; std::snprintf(c.s.preBuildCommand, 260, "echo hi");
        bool noteAboutCommands = false; for (const Issue& i : Validate(c)) if (i.text.find("will not run") != std::string::npos) noteAboutCommands = true;
        CHECK(noteAboutCommands);
        CHECK(!NameHasBadChars("Fine Name 2") && NameHasBadChars("a/b") && CleanName("a/b ") == "a_b");
    }
    {
        RUN_SUITE("files left out of the build");
        CHECK(IsExcluded("Assets/a.png", "Assets/a.png") && IsExcluded("assets\\A.PNG", "Assets/a.png"));
        CHECK(!IsExcluded("Assets/a.png2", "Assets/a.png") && !IsExcluded("Assets/b/a.png", "Assets/a.png") && !IsExcluded("Assets/a.png", ""));
        CHECK(IsExcluded("Assets/Old/x/y.png", "Assets/Old/") && !IsExcluded("Assets/Older/y.png", "Assets/Old/") && !IsExcluded("Assets/Old", "Assets/Old/"));
        CHECK(ExcludeEntries(" ; Assets\\a.png ;; b/ ").size() == 2 && ExcludeEntries(" ; Assets\\a.png ;; b/ ")[0] == "Assets/a.png");
        bool ok = false;
        std::string l = ToggleExcluded("", "Assets/a.png", 64, &ok); CHECK(ok && l == "Assets/a.png");
        l = ToggleExcluded(l, "Assets\\B.png", 64, &ok); CHECK(l == "Assets/a.png;Assets/B.png");
        l = ToggleExcluded(l, "assets/a.PNG", 64, &ok); CHECK(l == "Assets/B.png");           // again = put back
        CHECK(ToggleExcluded(l, "Assets/a-very-long-name.png", 12, &ok) == l && !ok);       // does not fit: unchanged
        SummaryInput si; std::snprintf(si.s.excludeList, 1024, "a;b/"); si.estimateBytes = 2048;
        std::string all; for (const auto& x : BuildSummary(si)) all += x + "\n";
        CHECK(all.find("2 file or folder entries") != std::string::npos && all.find("Dry Run said") != std::string::npos);
    }
    {
        RUN_SUITE("drag reorder of the scene list");
        std::vector<BuildScene> v = {{"a", true}, {"b", false}, {"c", true}, {"d", true}};
        auto names = [&]() { std::string s; for (auto& x : v) s += x.path; return s; };
        CHECK(MoveItem(v, 0, 2) && names() == "bcad" && !v[0].on);          // the tick travels with the scene
        CHECK(MoveItem(v, 3, 0) && names() == "dbca");
        CHECK(MoveItem(v, 1, 1) && names() == "dbca");
        CHECK(!MoveItem(v, -1, 0) && !MoveItem(v, 0, 4) && !MoveItem(v, 9, 1) && names() == "dbca");
    }
    {
        RUN_SUITE("build log colours");
        CHECK(ClassifyLogLine("The command before the build failed (exit code 2). Nothing was built.") == LogKind::Error);
        CHECK(ClassifyLogLine("Couldn't copy x") == LogKind::Error && ClassifyLogLine("Refusing to build into your home folder.") == LogKind::Error);
        CHECK(ClassifyLogLine("Warning: 'a' is an absolute path") == LogKind::Warning && ClassifyLogLine("WARNING: 2 referenced asset(s) not found") == LogKind::Warning);
        CHECK(ClassifyLogLine("Built 'Moon' to C:/out") == LogKind::Good && ClassifyLogLine("12 files, 3 MB") == LogKind::Normal);
        const auto lines = BuildLogLines("Built 'G' to out  [3 MB total]  (2 scenes)  WARNING: 1 referenced asset(s) not found and not copied.", {"w1"});
        CHECK(lines.size() == 5 && lines[0] == "Built 'G' to out" && lines[3].rfind("WARNING", 0) == 0 && lines[4] == "Warning: w1");
        CHECK(BuildLogLines("", {}).empty());
    }
    {
        RUN_SUITE("history");
        std::vector<HistoryEntry> h;
        for (int i = 0; i < 35; ++i) { HistoryEntry e; e.time = "2026-11-19 10:" + std::to_string(10 + i % 50); e.version = "1.0." + std::to_string(i); e.buildNumber = i + 1; e.platform = "Desktop"; e.bytes = (std::uintmax_t)(i + 1) * 1000; e.output = "C:/a|b/out\nx"; e.ok = i != 34; h = AddToHistory(h, e); }
        CHECK(h.size() == 30);
        const std::string text = WriteHistory(h);
        const std::vector<HistoryEntry> back = ParseHistory(text);
        CHECK(back.size() == 30 && back[0].buildNumber == 6 && back[29].buildNumber == 35 && !back[29].ok && back[3].bytes == 9000);
        CHECK(back[0].output == "C:/a b/out x");
        CHECK(LastGoodSize(back) == 34000);
        CHECK(WriteHistory(back) == text);
        CHECK(ParseHistory("garbage\n# c\n|||\n").empty());
    }
    {
        RUN_SUITE("build commands");
        CommandVars v; v.output = "C:/o"; v.name = "G"; v.version = "1.2.3"; v.build = "7"; v.project = "P";
        CHECK(ExpandCommand("zip {output} {name}-{version}-{build} {unknown} {", v) == "zip C:/o G-1.2.3-7 {unknown} {");
        CHECK(CommandToRun("x {name}", false, false, v).empty());                 // not allowed
        CHECK(CommandToRun("x {name}", true, true, v).empty());                   // dry run
        CHECK(CommandToRun("   ", true, false, v).empty() && CommandToRun("", true, false, v).empty());
        CHECK(CommandToRun("x {name}", true, false, v) == "x G");
        CHECK(RunCommandLine("exit 0", "") == 0);
        CHECK(RunCommandLine("exit 3", "") == 3);
        const fs::path before = fs::current_path();
        CHECK(RunCommandLine("exit 0", fs::temp_directory_path().string()) == 0 && fs::current_path() == before);   // the folder is put back
#if !defined(_WIN32)
        {   // the command runs in the folder it is given, and the editor's folder never changes
            const fs::path wd = fs::temp_directory_path() / "okay bs cmd's dir";
            fs::create_directories(wd);
            { std::ofstream(wd / "marker.txt") << "x"; }
            CHECK(RunCommandLine("test -f marker.txt", wd.string()) == 0);
            CHECK(RunCommandLine("test -f marker.txt", "") != 0 || fs::exists(before / "marker.txt"));
            CHECK(fs::current_path() == before);
            fs::remove_all(wd);
        }
#endif
    }
    {
        RUN_SUITE("output folder: a relative folder is inside the project");
        const fs::path proj = fs::temp_directory_path() / "okay_bs_proj";
        CHECK(fs::path(ResolveOutputDir("build/MyGame", proj.string())) == proj / "build" / "MyGame");
        CHECK(fs::path(ResolveOutputDir("./out/../Builds/Win", proj.string())) == proj / "Builds" / "Win");
        const std::string abs = (fs::temp_directory_path() / "elsewhere").string();
        CHECK(ResolveOutputDir(abs, proj.string()) == abs);            // an absolute folder stays
        CHECK(ResolveOutputDir("build/MyGame", "") == "build/MyGame"); // no project: as written
        CHECK(ResolveOutputDir("", proj.string()).empty());
    }
    {
        RUN_SUITE("version control file");
        const auto lines = IgnoreLines("Builds\\Win/");
        CHECK(std::find(lines.begin(), lines.end(), "Builds/Win/") != lines.end());
        { const auto outside = IgnoreLines("C:/x"); CHECK(std::find(outside.begin(), outside.end(), "C:/x/") == outside.end()); }   // outside the project: not listed
        CHECK(IgnoreLines("../out").size() == IgnoreLines("").size());
        int added = 0;
        const std::string a = MergeIgnore("node_modules/\n*.log", lines, &added);
        CHECK(added == (int)lines.size() - 1 && a.find("node_modules/\n*.log\n# added by") != std::string::npos);
        int again = -1;
        CHECK(MergeIgnore(a, lines, &again) == a && again == 0);                   // running it twice changes nothing
        CHECK(MergeIgnore("", lines, &added).rfind("# added by", 0) == 0);
    }
    {
        RUN_SUITE("profile files (.okaybuild) and duplicate");
        std::vector<BuildProfile> list(2);
        list[0].name = "Debug"; list[1].name = "Release"; list[0].settings.width = 800; list[0].productName = "G";
        CHECK(DuplicateProfile(list, 0) == 2 && list[2].name == "Debug copy" && list[2].settings.width == 800);
        CHECK(DuplicateProfile(list, 0) == 3 && list[3].name == "Debug copy 2");
        CHECK(DuplicateProfile(list, 9) == -1 && DuplicateProfile(list, -1) == -1);
        CHECK(ProfileFileStem("My: build/1?") == "My build1" && ProfileFileStem("???") == "profile" && ProfileFileStem(" .x. ") == "x");
        BuildProfile back;
        CHECK(ReadProfileFile(WriteProfileFile(list[0]), back) && back.name == "Debug" && back.settings.width == 800 && back.productName == "G");
        CHECK(!ReadProfileFile("nothing here", back));
    }
    {
        RUN_SUITE("files in a project: history and .okaybuild profiles");
        const fs::path dir = fs::temp_directory_path() / "okay_bs_test_project";
        std::error_code ec; fs::remove_all(dir, ec); fs::create_directories(dir / ".okay", ec);
        const std::string pd = dir.string();
        CHECK(LoadHistory(pd).empty() && LoadHistory("").empty());
        HistoryEntry e; e.time = NowText(); e.version = "2.0.0"; e.buildNumber = 3; e.platform = "Desktop"; e.bytes = 123456; e.output = "out";
        CHECK(NowText().size() == 16 && NowText()[4] == '-');
        CHECK(RecordBuild(pd, e)); e.buildNumber = 4; CHECK(RecordBuild(pd, e));
        const auto h = LoadHistory(pd);
        CHECK(h.size() == 2 && h[1].buildNumber == 4 && h[0].bytes == 123456 && h[0].version == "2.0.0");
        CHECK(!RecordBuild("", e));
        BuildProfile p; p.name = "Shop: demo"; p.settings.height = 480; p.settings.timeScale = 2.0f; p.productName = "G";
        CHECK(ListProfileFiles(pd).empty());
        CHECK(SaveProfileFile(pd, p));
        const auto files = ListProfileFiles(pd);
        CHECK(files.size() == 1 && files[0] == "Shop demo.okaybuild");
        BuildProfile q;
        CHECK(LoadProfileFile(pd, files[0], q) && q.settings.height == 480 && q.settings.timeScale == 2.0f && q.productName == "G");
        CHECK(!LoadProfileFile(pd, "../x.okaybuild", q) && !LoadProfileFile(pd, "missing.okaybuild", q) && !LoadProfileFile("", files[0], q));
        fs::remove_all(dir, ec);
    }
    {
        RUN_SUITE("game.okayconfig extra lines: the player's reader");
        // Defaults write nothing, so an old project's built config is unchanged.
        CHECK(bp::WriteExtraConfig(ExtraFrom(BuildSettings{})).empty());
        BuildSettings s;
        s.timeScale = 0.5f; s.maxFixedSteps = 6; s.musicVolume = 0.25f; s.sfxVolume = 0.75f;
        std::snprintf(s.defaultLanguage, 16, "de"); std::snprintf(s.logLevel, 16, "Warning"); s.logToFile = true;
        std::snprintf(s.description, 256, "A moon game\nsecond line"); std::snprintf(s.envVars, 512, "OKAY_X=1;BAD=ok;PATH=nope");
        const std::string text = bp::WriteExtraConfig(ExtraFrom(s));
        CHECK(text.find("time_scale=0.5\n") != std::string::npos && text.find("log_level=warn\n") != std::string::npos && text.find("log_file=game.log\n") != std::string::npos);
        CHECK(text.find("PATH") == std::string::npos && text.find("env=OKAY_X=1\n") != std::string::npos);
        CHECK(text.find("description=A moon game second line\n") != std::string::npos);
        // What the player does: read every line through ApplyExtraKey.
        bp::ExtraConfig c; std::istringstream in(text);
        for (std::string line; std::getline(in, line);) { const auto eq = line.find('='); CHECK(bp::ApplyExtraKey(c, line.substr(0, eq), line.substr(eq + 1))); }
        CHECK(c.timeScale == 0.5f && c.maxFixedSteps == 6 && c.musicVolume == 0.25f && c.sfxVolume == 0.75f && c.defaultLanguage == "de");
        CHECK(c.logLevel == "warn" && c.logFile == "game.log" && c.env.size() == 2 && c.env[1].first == "BAD" && c.description == "A moon game second line");
        CHECK(bp::WriteExtraConfig(c) == text);
        s.renderer = 2; s.particleCap = 200; s.gamepadDeadzone = 0.4f; s.sampleRate = 48000;
        { const std::string t2 = bp::WriteExtraConfig(ExtraFrom(s)); bp::ExtraConfig r; std::istringstream in2(t2);
          for (std::string line; std::getline(in2, line);) { const auto eq = line.find('='); bp::ApplyExtraKey(r, line.substr(0, eq), line.substr(eq + 1)); }
          CHECK(r.renderer == 2 && r.particleCap == 200 && r.gamepadDeadzone == 0.4f && r.sampleRate == 48000); }
        { bp::ExtraConfig r; bp::ApplyExtraKey(r, "renderer", "9"); bp::ApplyExtraKey(r, "sample_rate", "12345"); bp::ApplyExtraKey(r, "gamepad_deadzone", "5"); bp::ApplyExtraKey(r, "particle_cap", "-3");
          CHECK(r.renderer == 0 && r.sampleRate == 0 && r.gamepadDeadzone == 0.95f && r.particleCap == 0); }
        s.renderer = 0; s.particleCap = 0; s.gamepadDeadzone = 0.18f; s.sampleRate = 0;
        // Hostile or broken lines are tamed, and other keys are left to the player's own reader.
        bp::ExtraConfig h;
        CHECK(!bp::ApplyExtraKey(h, "width", "10") && !bp::ApplyExtraKey(h, "title", "x"));
        bp::ApplyExtraKey(h, "time_scale", "nan"); CHECK(h.timeScale == 0.0f);
        bp::ApplyExtraKey(h, "time_scale", "1e30"); CHECK(h.timeScale == 100.0f);
        bp::ApplyExtraKey(h, "music_volume", "7"); CHECK(h.musicVolume == 1.0f);
        bp::ApplyExtraKey(h, "max_fixed_steps", "-4"); CHECK(h.maxFixedSteps == 0);
        bp::ApplyExtraKey(h, "log_file", "../../evil.txt"); CHECK(h.logFile.empty());
        bp::ApplyExtraKey(h, "log_file", "a\\b"); CHECK(h.logFile.empty());
        bp::ApplyExtraKey(h, "log_level", "shout"); CHECK(h.logLevel.empty());
        bp::ApplyExtraKey(h, "default_language", std::string(40, 'x')); CHECK(h.defaultLanguage.empty());
        bp::ApplyExtraKey(h, "env", "LD_PRELOAD=/x.so"); bp::ApplyExtraKey(h, "env", "1BAD=2"); bp::ApplyExtraKey(h, "env", "dyld_insert_libraries=x"); CHECK(h.env.empty());
        CHECK(bp::IsValidEnvName("_ok1") && !bp::IsValidEnvName("") && !bp::IsValidEnvName("a b") && !bp::IsValidEnvName("a=b"));
        // The settings reach the profile file the project keeps (round trip is also covered by test_project_state).
        s.description[0] = 'x'; s.description[1] = 0;   // a one-line value: the profile file is line based
        BuildProfile p; p.settings = s;
        BuildProfile q; CHECK(ReadProfileFile(WriteProfileFile(p), q));
        CHECK(WriteBuildProfileBlockText(q.settings) == WriteBuildProfileBlockText(s));
    }
    TEST_MAIN_RESULT();
}
