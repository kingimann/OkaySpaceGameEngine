// Project-level state: the project's settings (project.okayproj), its Build Settings and build profiles, the
// tag list and layer names. Every field is set to a value that is not its default, written, read back into a
// fresh object and compared, and the field list is read from the headers' struct text, so a field added later
// without being written (or without a line here) fails this test. Also: old files still load, text is stable,
// and each project's files are its own. Nothing sleeps, nothing reads the clock; temp folders only.
#include "test_framework.hpp"
#include "../editor/ProjectState.hpp"
#include "okay/Scene/Layers.hpp"
#include "okay/Scene/Tags.hpp"

#include <chrono>
#include <fstream>
#include <regex>
#include <set>

namespace fs = std::filesystem;
using namespace oked;
using namespace okay;

namespace {

// ---- comparing members of any shape ----------------------------------------------------------------------
template <std::size_t N> bool Same(const char (&a)[N], const char (&b)[N]) { return std::strcmp(a, b) == 0; }
template <std::size_t N> bool Same(const float (&a)[N], const float (&b)[N]) { for (std::size_t i = 0; i < N; ++i) if (a[i] != b[i]) return false; return true; }
bool Same(const std::vector<BuildScene>& a, const std::vector<BuildScene>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) if (a[i].path != b[i].path || a[i].on != b[i].on) return false;
    return true;
}
template <class T> bool Same(const T& a, const T& b) { return a == b; }
template <std::size_t N> void Put(char (&dst)[N], const char* s) { std::snprintf(dst, N, "%s", s); }

std::set<std::string> g_covered;
int g_fields = 0, g_values = 0;
// Check one member: it must differ from the default (the test really changed it) and survive the round trip.
#define COVER(a, b, d, field)                                           \
    do {                                                                \
        g_covered.insert(#field);                                       \
        ++g_fields;                                                     \
        CHECK(!Same((a).field, (d).field));                             \
        const bool same_ = Same((a).field, (b).field);                  \
        if (!same_) std::cerr << "    not saved: " #field "\n";         \
        CHECK(same_);                                                   \
    } while (0)

// The member names a struct declares, read from the header text (so the test cannot fall behind it).
std::set<std::string> DeclaredFields(const std::string& src, const std::string& structName) {
    std::set<std::string> out;
    const std::size_t a = src.find("struct " + structName + " {");
    if (a == std::string::npos) return out;
    const std::size_t b = src.find("\n};", a);
    std::istringstream in(src.substr(a, b - a));
    std::string line;
    std::getline(in, line);   // the struct line
    const std::regex typed("^\\s+(?:char|int|float|bool|std::string|std::vector<[A-Za-z]+>|BuildSettings)\\s+(.*)$");
    while (std::getline(in, line)) {
        const std::size_t c = line.find("//");
        if (c != std::string::npos) line.erase(c);
        std::smatch m;
        if (!std::regex_match(line, m, typed)) continue;
        std::string rest = m[1];
        if (!rest.empty() && rest.back() == ';') rest.pop_back();
        // split on commas that are not inside braces
        int depth = 0;
        std::string cur;
        std::vector<std::string> parts;
        for (char ch : rest) {
            if (ch == '{') ++depth;
            if (ch == '}') --depth;
            if (ch == ',' && depth == 0) { parts.push_back(cur); cur.clear(); } else cur += ch;
        }
        parts.push_back(cur);
        for (const std::string& p : parts) {
            std::smatch id;
            if (std::regex_search(p, id, std::regex("^\\s*([A-Za-z_][A-Za-z_0-9]*)"))) out.insert(id[1]);
        }
    }
    return out;
}

struct TempDir {
    fs::path path;
    TempDir() {
        static int n = 0;
        path = fs::temp_directory_path() / ("okay_projstate_" + std::to_string(++n) + "_" +
               std::to_string((long long)std::chrono::steady_clock::now().time_since_epoch().count()));
        std::error_code ec;
        fs::remove_all(path, ec);
        fs::create_directories(path, ec);
    }
    ~TempDir() { std::error_code ec; fs::remove_all(path, ec); }
};

void ResetLists() {
    Tags::Strict() = false;
    ProjectLists::Reset();
}

}  // namespace

int main() {
    RUN_SUITE("project_state");

    std::string header;
    const fs::path hdrPath = fs::path(__FILE__).parent_path().parent_path() / "editor" / "ProjectState.hpp";
    const bool haveHeader = okayproj::ReadText(hdrPath.string(), header);
    CHECK(haveHeader);

    // ---- ProjectSettings: every field -----------------------------------------------------------------------
    {
        RUN_SUITE("project settings: each field, non-default, written and read back");
        ResetLists();
        const ProjectSettings d;
        ProjectSettings a;
        Put(a.company, "Acme Games & Co");
        Put(a.version, "3.14.15-beta");
        a.defaultWidth = 1920; a.defaultHeight = 1080;
        a.gravity2D[0] = 1.5f; a.gravity2D[1] = -20.25f;
        a.gravity3D[0] = 0.1f; a.gravity3D[1] = -1.0f / 3.0f; a.gravity3D[2] = 123456.789f;
        a.ambient = 0.123456789f;
        a.skybox = false;
        Put(a.splashLogo, "Assets/Textures/logo with spaces.png");
        a.splashBgTop[0] = 0.9f; a.splashBgTop[1] = 0.8f; a.splashBgTop[2] = 0.7f;
        a.splashBgBot[0] = 0.01f; a.splashBgBot[1] = 0.02f; a.splashBgBot[2] = 0.03f;
        a.splashAccentOverride = true;
        a.splashAccent[0] = 1.0f; a.splashAccent[1] = 0.333333343f; a.splashAccent[2] = 0.0f;
        a.splashShowBar = false;
        // The tag list and layer names travel in the same file.
        CHECK(Tags::Add("Boss") && Tags::Add("Pickup Spot"));
        Tags::Strict() = true;
        CHECK(Layers::SetName(8, "Enemy") && Layers::SetName(9, "Fx Layer"));
        const std::size_t tagsBefore = Tags::Custom().size();

        const std::string text = WriteProjectSettings(a);
        ResetLists();
        ProjectSettings b;
        ParseProjectSettings(b, text);

        COVER(a, b, d, company);
        COVER(a, b, d, version);
        COVER(a, b, d, defaultWidth);
        COVER(a, b, d, defaultHeight);
        COVER(a, b, d, gravity2D);
        COVER(a, b, d, gravity3D);
        COVER(a, b, d, ambient);
        COVER(a, b, d, skybox);
        COVER(a, b, d, splashLogo);
        COVER(a, b, d, splashBgTop);
        COVER(a, b, d, splashBgBot);
        COVER(a, b, d, splashAccentOverride);
        COVER(a, b, d, splashAccent);
        COVER(a, b, d, splashShowBar);
        g_values += 23;
        CHECK(haveHeader && g_covered == DeclaredFields(header, "ProjectSettings"));
        if (g_covered != DeclaredFields(header, "ProjectSettings")) {
            for (const auto& f : DeclaredFields(header, "ProjectSettings")) if (!g_covered.count(f)) std::cerr << "    ProjectSettings." << f << " has no line in this test\n";
        }
        CHECK(sizeof(ProjectSettings) == 500);   // a new field changes this: add it to the writer, the reader and this test

        // The lists came back too.
        CHECK(Tags::Custom().size() == tagsBefore && Tags::Contains("Boss") && Tags::Contains("Pickup Spot"));
        CHECK(Tags::Strict());
        CHECK(Layers::Name(8) == "Enemy" && Layers::Name(9) == "Fx Layer");
        g_values += 2 + 1 + 2;

        // Writing what was read gives the same text (stable), and a default project writes and reads as itself.
        CHECK(WriteProjectSettings(b) == text);
        ResetLists();
        ProjectSettings def2, back;
        ParseProjectSettings(back, WriteProjectSettings(def2));
        CHECK(WriteProjectSettings(back) == WriteProjectSettings(def2));
        ResetLists();
    }

    // ---- older files, hand edits, damage -------------------------------------------------------------------
    {
        RUN_SUITE("project settings: files an older editor wrote still load");
        ResetLists();
        const std::string old =   // exactly what the previous editor wrote: six-digit numbers, no lists
            "company=Old Co\nversion=1.2.3\nwidth=800\nheight=600\ngravity2d=0 -9.81\ngravity3d=0 -9.81 0\nambient=0.3\nskybox=1\n"
            "splash_logo=\nsplash_bar=1\nsplash_accent_on=0\nsplash_bg_top=0.094 0.094 0.106\nsplash_bg_bot=0.051 0.051 0.059\n"
            "splash_accent=0.235 0.475 0.8\n";
        ProjectSettings p;
        ParseProjectSettings(p, old);
        CHECK(std::string(p.company) == "Old Co" && std::string(p.version) == "1.2.3" && p.defaultWidth == 800 && p.defaultHeight == 600);
        CHECK(p.gravity2D[1] == -9.81f && p.gravity3D[1] == -9.81f && p.ambient == 0.3f && p.skybox && p.splashShowBar);
        CHECK(p.splashBgTop[2] == 0.106f && p.splashAccent[1] == 0.475f);
        CHECK(Tags::Custom().empty() && Layers::Name(8) == "Layer 8");   // no list lines: the defaults

        // CRLF, unknown keys, a line with no '=', a value that is not a number: nothing breaks, the rest still loads.
        ProjectSettings q;
        ParseProjectSettings(q, "company=Win Co\r\nfuture_setting=7\r\nthis is not a setting\r\nwidth=abc\r\nheight=480\r\nambient=0.5\r\n");
        CHECK(std::string(q.company) == "Win Co" && q.defaultHeight == 480 && q.ambient == 0.5f);
        CHECK(q.defaultWidth == 0);   // atoi("abc"): what the editor always did
        // A value longer than its box is cut, not overrun.
        ProjectSettings r;
        ParseProjectSettings(r, "company=" + std::string(500, 'x') + "\nversion=" + std::string(100, 'v') + "\n");
        CHECK(std::strlen(r.company) == sizeof(r.company) - 1 && std::strlen(r.version) == sizeof(r.version) - 1);
        // An empty file is the defaults.
        ProjectSettings e;
        ParseProjectSettings(e, "");
        CHECK(WriteProjectSettings(e) == WriteProjectSettings(ProjectSettings{}));
        ResetLists();
    }

    // ---- Build Settings: every field ------------------------------------------------------------------------
    {
        RUN_SUITE("build settings: each field, non-default, in a profile");
        g_covered.clear();
        const BuildSettings d;
        BuildProfile pa;
        BuildSettings& a = pa.settings;
        pa.name = "Release with spaces";
        pa.productName = "My Great Game";
        pa.outputDir = "build/My Great Game/win64";
        pa.platform = 2;
        Put(a.company, "Acme"); Put(a.version, "9.9.9");
        a.width = 2560; a.height = 1440;
        a.fullscreen = true; a.borderless = true; a.resizable = false; a.vsync = false; a.smoothDt = false; a.hideCursor = true;
        a.fpsCap = 144; a.quitOnEscape = false; a.masterVolume = 0.35f; a.showFps = true;
        a.includeAllProjectScenes = true; a.developmentBuild = true; a.cleanOutput = false; a.dataFolder = false; a.encryptData = true;
        a.antiCheat = 2;
        Put(a.icon, "Assets/Textures/app icon.png"); Put(a.bundleId, "com.acme.great-game");
        a.runInBackground = false; a.muteOnFocusLoss = true; a.startMaximized = true;
        a.minWidth = 640; a.minHeight = 360; a.saveToUserDir = false;
        Put(a.splash, "Assets/Textures/splash.png");
        a.splashTime = 3.75f; a.splashBg[0] = 0.1f; a.splashBg[1] = 0.2f; a.splashBg[2] = 0.3f;
        a.lockCursor = true; a.cursorLockMode = 2; a.perPixelLighting = true; a.shadows = false;
        a.shadowDistance = 150.5f; a.shadowSoftness = 4.25f; a.shadowCascades = 4; a.shadowResolution = 4096;
        a.bloom = true; a.ssao = true; a.fxaa = false; a.antialias = 2; a.gpuRenderer = false; a.preferD3D12 = true;
        a.openFolderAfter = false; a.zipAfter = true; a.fullscreenToggle = false;
        a.layout = 2; a.stripAssets = false; a.compress = false; a.buildNumber = 41; a.autoBuildNumber = false;
        Put(a.archiveName, "{name}_b{build}");
        a.scenes = {{"Assets/Level 1.okayscene", true}, {"Assets/Menu.okayscene", false}, {"C:/Games/x y/Final.okayscene", true}};
        a.srvTarget = 1; Put(a.srvName, "Acme Server"); a.srvPort = 45123; a.srvMaxPlayers = 64; a.srvTick = 60;
        Put(a.srvPassword, "hunter2"); a.srvAuth = true; a.srvAdvertise = true;
        Put(a.srvPublicAddr, "play.acme.example"); Put(a.srvRegion, "eu-west");
        a.srvMaxSpeed = 12.5f; a.srvRateLimit = 200; a.srvKickAfter = 5;

        const std::string text = WriteBuildProfiles({pa});
        const std::vector<BuildProfile> back = ParseBuildProfiles(text);
        CHECK(back.size() == 1);
        if (back.size() == 1) {
            const BuildSettings& b = back[0].settings;
            CHECK(back[0].name == pa.name && back[0].productName == pa.productName && back[0].outputDir == pa.outputDir && back[0].platform == pa.platform);
            COVER(a, b, d, company); COVER(a, b, d, version); COVER(a, b, d, width); COVER(a, b, d, height);
            COVER(a, b, d, fullscreen); COVER(a, b, d, borderless); COVER(a, b, d, resizable); COVER(a, b, d, vsync);
            COVER(a, b, d, smoothDt); COVER(a, b, d, hideCursor); COVER(a, b, d, fpsCap); COVER(a, b, d, quitOnEscape);
            COVER(a, b, d, masterVolume); COVER(a, b, d, showFps); COVER(a, b, d, includeAllProjectScenes);
            COVER(a, b, d, developmentBuild); COVER(a, b, d, cleanOutput); COVER(a, b, d, dataFolder); COVER(a, b, d, encryptData);
            COVER(a, b, d, antiCheat); COVER(a, b, d, icon); COVER(a, b, d, bundleId); COVER(a, b, d, runInBackground);
            COVER(a, b, d, muteOnFocusLoss); COVER(a, b, d, startMaximized); COVER(a, b, d, minWidth); COVER(a, b, d, minHeight);
            COVER(a, b, d, saveToUserDir); COVER(a, b, d, splash); COVER(a, b, d, splashTime); COVER(a, b, d, splashBg);
            COVER(a, b, d, lockCursor); COVER(a, b, d, cursorLockMode); COVER(a, b, d, perPixelLighting); COVER(a, b, d, shadows); COVER(a, b, d, shadowDistance);
            COVER(a, b, d, shadowSoftness); COVER(a, b, d, shadowCascades); COVER(a, b, d, shadowResolution);
            COVER(a, b, d, bloom); COVER(a, b, d, ssao); COVER(a, b, d, fxaa); COVER(a, b, d, antialias);
            COVER(a, b, d, gpuRenderer); COVER(a, b, d, preferD3D12); COVER(a, b, d, openFolderAfter); COVER(a, b, d, zipAfter);
            COVER(a, b, d, fullscreenToggle); COVER(a, b, d, scenes);
            COVER(a, b, d, layout); COVER(a, b, d, stripAssets); COVER(a, b, d, compress); COVER(a, b, d, buildNumber);
            COVER(a, b, d, autoBuildNumber); COVER(a, b, d, archiveName);
            COVER(a, b, d, srvTarget); COVER(a, b, d, srvName); COVER(a, b, d, srvPort); COVER(a, b, d, srvMaxPlayers);
            COVER(a, b, d, srvTick); COVER(a, b, d, srvPassword); COVER(a, b, d, srvAuth); COVER(a, b, d, srvAdvertise);
            COVER(a, b, d, srvPublicAddr); COVER(a, b, d, srvRegion); COVER(a, b, d, srvMaxSpeed); COVER(a, b, d, srvRateLimit);
            COVER(a, b, d, srvKickAfter);
            g_values += 68 - 1 + 3 + 4;   // scenes count as three entries, plus the profile's own four fields
            const std::set<std::string> declared = DeclaredFields(header, "BuildSettings");
            CHECK(g_covered == declared);
            for (const auto& f : declared) if (!g_covered.count(f)) std::cerr << "    BuildSettings." << f << " has no line in this test\n";
            for (const auto& f : g_covered) if (!declared.count(f)) std::cerr << "    " << f << " is not a BuildSettings member\n";
            CHECK(declared.size() == 68);
            CHECK(WriteBuildProfiles(back) == text);                      // stable
        }
        {   // the profile's own members
            const std::set<std::string> declared = DeclaredFields(header, "BuildProfile");
            const std::set<std::string> have = {"name", "settings", "productName", "outputDir", "platform"};
            CHECK(declared == have);
        }

        // Several profiles keep their order and their own values; a default profile is itself.
        BuildProfile dev; dev.name = "Dev"; dev.settings.developmentBuild = true; dev.productName = "G Dev"; dev.outputDir = "build/dev";
        BuildProfile rel; rel.name = "Release"; rel.settings.encryptData = true; rel.platform = 1;
        const auto many = ParseBuildProfiles(WriteBuildProfiles({dev, pa, rel}));
        CHECK(many.size() == 3 && many[0].name == "Dev" && many[1].name == pa.name && many[2].name == "Release");
        CHECK(many[0].settings.developmentBuild && !many[2].settings.developmentBuild && many[2].settings.encryptData && many[2].platform == 1);
        CHECK(ParseBuildProfiles("").empty() && ParseBuildProfiles("# only a comment\n").empty());

        // A file an older editor wrote (six-digit floats, CRLF) loads.
        const auto oldp = ParseBuildProfiles("# OkaySpace build profiles\r\n@profile Old\r\nproduct=Game\r\ndir=build/Game\r\nplatform=0\r\nmasterVolume=0.5\r\nscene=1|Assets/a.okayscene\r\nfuture=1\r\n");
        CHECK(oldp.size() == 1 && oldp[0].name == "Old" && oldp[0].settings.masterVolume == 0.5f && oldp[0].settings.scenes.size() == 1);
    }

    // ---- the dialog as it was left (works with or without a profile) -------------------------------------------
    {
        RUN_SUITE("build settings as left in the dialog");
        BuildProfile live;
        live.settings.width = 777; live.settings.zipAfter = true; live.settings.masterVolume = 0.2f;
        live.settings.scenes = {{"Assets/x.okayscene", true}};
        live.productName = "Live Game"; live.outputDir = "out/live"; live.platform = 3;
        const std::string text = WriteBuildLive(live, "Release");
        BuildProfile back; std::string sel;
        CHECK(ParseBuildLive(text, back, sel));
        CHECK(back.settings.width == 777 && back.settings.zipAfter && back.settings.masterVolume == 0.2f && back.settings.scenes.size() == 1);
        CHECK(back.productName == "Live Game" && back.outputDir == "out/live" && back.platform == 3 && sel == "Release");
        CHECK(ParseBuildLive(WriteBuildLive(live, ""), back, sel) && sel.empty());   // not from a profile
        CHECK(!ParseBuildLive("", back, sel) && !ParseBuildLive("garbage\n", back, sel));
        // An editor that only knows profiles never mixes it in: a different file.
        CHECK(BuildLiveFile("p") != BuildProfilesFile("p"));
    }

    // ---- where the files are, per project ---------------------------------------------------------------------
    {
        RUN_SUITE("each project has its own files");
        CHECK(ProjectSettingsFile("/games/A") == (fs::path("/games/A") / "project.okayproj").string());
        CHECK(ProjectSettingsFile("/games/A") != ProjectSettingsFile("/games/B"));
        CHECK(ProjectSettingsFile("") == "project.okayproj");                    // no project: the working folder's, as before
        CHECK(BuildProfilesFile("/games/A") == (fs::path("/games/A") / ".okay" / "build_profiles.cfg").string());
        CHECK(BuildLiveFile("/games/A") == (fs::path("/games/A") / ".okay" / "build_live.cfg").string());
        CHECK(BuildProfilesFile("").empty() && BuildLiveFile("").empty());

        // Two projects, two settings: writing one never shows up in the other.
        TempDir t;
        const fs::path pa = t.path / "A", pb = t.path / "B";
        fs::create_directories(pa); fs::create_directories(pb);
        ResetLists();
        ProjectSettings sa, sb;
        Put(sa.company, "Alpha"); sa.defaultWidth = 111; Tags::Add("OnlyInA");
        std::string err;
        CHECK(okayproj::WriteTextAtomic(ProjectSettingsFile(pa.string()), WriteProjectSettings(sa), &err));
        ResetLists();
        Put(sb.company, "Beta"); sb.defaultWidth = 222; Layers::SetName(5, "OnlyInB");
        CHECK(okayproj::WriteTextAtomic(ProjectSettingsFile(pb.string()), WriteProjectSettings(sb), &err));
        ResetLists();
        std::string ta, tb;
        CHECK(okayproj::ReadText(ProjectSettingsFile(pa.string()), ta) && okayproj::ReadText(ProjectSettingsFile(pb.string()), tb));
        ProjectSettings ra, rb;
        ParseProjectSettings(ra, ta);
        CHECK(std::string(ra.company) == "Alpha" && ra.defaultWidth == 111 && Tags::Contains("OnlyInA") && Layers::Name(5) != "OnlyInB");
        ParseProjectSettings(rb, tb);   // reading B starts from the defaults: A's tag is gone
        CHECK(std::string(rb.company) == "Beta" && rb.defaultWidth == 222 && !Tags::Contains("OnlyInA") && Layers::Name(5) == "OnlyInB");
        ResetLists();
    }

    std::cout << "  covered " << g_fields << " struct members (" << g_values << " values) with non-default values\n";
    TEST_MAIN_RESULT();
}
