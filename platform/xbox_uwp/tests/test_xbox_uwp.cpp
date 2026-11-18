// Tests for the experimental Xbox UWP export's pure parts (SDK decision, manifest, assets, package folder).
// Runs on Linux with fake folder listings. It proves our logic, NOT that a Windows PC or an Xbox accepts the result.
#include "XboxAppx.hpp"
#include "XboxExport.hpp"
#include "XboxExportTool.hpp"
#include "XboxGameConfig.hpp"
#include "XboxSdkCheck.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

using namespace okay::xbox;
static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++g_fail; } } while (0)

// A fake disk: a set of full paths (files and folders). list(dir) = the names directly under dir.
static FsView Fake(const std::set<std::string>& paths) {
    FsView v;
    v.list = [paths](const std::string& dir) {
        std::vector<std::string> out;
        const std::string pre = dir + "/";
        for (const std::string& p : paths)
            if (p.compare(0, pre.size(), pre) == 0) {
                const std::string rest = p.substr(pre.size());
                const std::string first = rest.substr(0, rest.find('/'));
                if (std::find(out.begin(), out.end(), first) == out.end()) out.push_back(first);
            }
        return out;
    };
    return v;
}
static std::set<std::string> Full() {
    const std::string vs = "C:/VS/2022/Community", k = "C:/Program Files (x86)/Windows Kits/10", v = "10.0.19041.0";
    return {vs + "/VC/Tools/MSVC/14.38.33130/lib/x64/store/vcruntime.lib",
            vs + "/VC/Tools/MSVC/14.29.30133/lib/x64/onecore/x.lib",   // an older toolset without the store folder
            k + "/Include/" + v + "/winrt/x.h", k + "/Include/" + v + "/cppwinrt/x.h", k + "/Include/" + v + "/um/x.h", k + "/Include/" + v + "/ucrt/x.h",
            k + "/Lib/" + v + "/um/x64/WindowsApp.lib", k + "/Lib/" + v + "/um/x64/d3d11.lib",
            k + "/bin/" + v + "/x64/makeappx.exe", k + "/bin/" + v + "/x64/signtool.exe"};
}
static SdkInputs In(const std::set<std::string>& p, bool vswhere = true) {
    SdkInputs i; i.fs = Fake(p); i.vswhereFound = vswhere;
    i.vswhereInstallPaths = {"C:\\VS\\2022\\Community\r\n"};
    return i;
}
static std::string Item(const SdkReport& r, Need n) { for (auto& i : r.items) if (i.need == n) return i.found ? "found" : "missing"; return "?"; }

// Visual Studio 2022 AND newer installs (fake vswhere output and folder layouts). The folders decide; versions are compared as numbers.
static std::set<std::string> SdkOnly() {
    std::set<std::string> f; const std::string k = "C:/Program Files (x86)/Windows Kits/10", v = "10.0.26100.0";
    for (const char* x : {"/winrt/x.h", "/cppwinrt/x.h", "/um/x.h", "/ucrt/x.h"}) f.insert(k + "/Include/" + v + x);
    f.insert(k + "/Lib/" + v + "/um/x64/WindowsApp.lib"); f.insert(k + "/Lib/" + v + "/um/x64/d3d11.lib");
    f.insert(k + "/bin/" + v + "/x64/signtool.exe");
    return f;
}
static VsInstall Vs(const char* path, const char* ver, const char* name) { VsInstall v; v.path = path; v.version = ver; v.displayName = name; v.vcComponent = true; return v; }
static void NewerVsTests() {
    // newer install (major 18), toolset 14.50 with x64 UWP libraries, plus ARM64 extras (lib/arm64, lib/arm64/store, Hostarm64)
    { auto p = SdkOnly(); const std::string vs = "C:/VS/18/Community";
      p.insert(vs + "/VC/Tools/MSVC/14.50.35717/lib/x64/store/vcruntime.lib"); p.insert(vs + "/VC/Tools/MSVC/14.50.35717/lib/arm64/store/vcruntime.lib");
      p.insert(vs + "/VC/Tools/MSVC/14.50.35717/bin/Hostarm64/arm64/cl.exe");
      SdkInputs i; i.fs = Fake(p); i.vswhereFound = true; i.installs = {Vs("C:\\VS\\18\\Community\r\n", "18.0.11111.22", "Visual Studio Community 2026")};
      SdkReport r = EvaluateSdk(i);
      CHECK(r.available); CHECK(r.toolsetVersion == "14.50.35717"); CHECK(r.visualStudioMajor == 18 && r.generator == "Visual Studio 18 2026"); CHECK(r.visualStudioPath == "C:/VS/18/Community");
      CHECK(r.sdkVersion == "10.0.26100.0"); CHECK(r.items[0].detail.find("2026") != std::string::npos); }
    // newer install with the UWP component missing, only ARM64 extras present: x64 UWP libraries are what count, so it is "missing" and the text is the newer one
    { auto p = SdkOnly(); const std::string vs = "C:/VS/18/Community";
      p.insert(vs + "/VC/Tools/MSVC/14.50.35717/lib/x64/x.lib"); p.insert(vs + "/VC/Tools/MSVC/14.50.35717/lib/arm64/store/vcruntime.lib");
      SdkInputs i; i.fs = Fake(p); i.vswhereFound = true; i.installs = {Vs("C:\\VS\\18\\Community", "18.0.1.1", "Visual Studio Community 2026")};
      SdkReport r = EvaluateSdk(i);
      CHECK(!r.available); CHECK(Item(r, Need::VisualStudio) == "found"); CHECK(Item(r, Need::UwpToolset) == "missing");
      const SdkItem* t = nullptr; for (auto& it : r.items) if (it.need == Need::UwpToolset) t = &it;
      CHECK(t && t->install.find("Individual components") != std::string::npos && t->install.find("C++ Universal Windows Platform support for") != std::string::npos);
      CHECK(t && t->install.find("workload \"Universal Windows Platform development\" does not exist") != std::string::npos);
      CHECK(r.message.find("C++ Universal Windows Platform tools") != std::string::npos); }
    // 2022 missing the UWP part gets the workload text
    { auto p = SdkOnly(); p.insert("C:/VS/2022/Community/VC/Tools/MSVC/14.38.33130/lib/x64/x.lib");
      SdkInputs i; i.fs = Fake(p); i.vswhereFound = true; i.installs = {Vs("C:\\VS\\2022\\Community", "17.9.34728.123", "Visual Studio Community 2022")};
      SdkReport r = EvaluateSdk(i); CHECK(!r.available && r.generator == "Visual Studio 17 2022");
      for (auto& it : r.items) if (it.need == Need::UwpToolset) CHECK(it.install.find("workload \"Universal Windows Platform development\"") != std::string::npos && it.install.find("Individual components") == std::string::npos); }
    // both 2022 and a newer install, both complete: the newer installationVersion wins whatever vswhere's order (numeric: 18 > 17, 17.10 > 17.9)
    { auto p = SdkOnly();
      p.insert("C:/VS/2022/Community/VC/Tools/MSVC/14.44.35207/lib/x64/store/a.lib"); p.insert("C:/VS/18/Community/VC/Tools/MSVC/14.50.1/lib/x64/store/a.lib");
      SdkInputs i; i.fs = Fake(p); i.vswhereFound = true; i.installs = {Vs("C:\\VS\\2022\\Community", "17.9.1.1", "2022"), Vs("C:\\VS\\18\\Community", "18.0.0.1", "2026")};
      CHECK(EvaluateSdk(i).visualStudioPath == "C:/VS/18/Community");
      i.installs = {Vs("C:\\VS\\2022\\Community", "17.9.1.1", "a"), Vs("C:\\VS\\2022\\Community", "17.10.1.1", "b")}; CHECK(EvaluateSdk(i).visualStudioVersion == "17.10.1.1"); }   // 17.10 > 17.9 numerically, not as text
    // the newer install lacks the UWP tools but 2022 has them: the install that can build wins
    { auto p = SdkOnly(); p.insert("C:/VS/2022/Community/VC/Tools/MSVC/14.44.35207/lib/x64/store/a.lib"); p.insert("C:/VS/18/Community/VC/Tools/MSVC/14.50.1/lib/x64/a.lib");
      SdkInputs i; i.fs = Fake(p); i.vswhereFound = true; i.installs = {Vs("C:\\VS\\18\\Community", "18.0.0.1", "2026"), Vs("C:\\VS\\2022\\Community", "17.9.1.1", "2022")};
      SdkReport r = EvaluateSdk(i); CHECK(r.available && r.visualStudioPath == "C:/VS/2022/Community"); }
    // toolset folders are never hard-coded: several, numeric order (14.9 < 14.44 < 14.100)
    { auto p = SdkOnly(); for (const char* t : {"14.9.1", "14.44.35207", "14.100.1"}) p.insert(std::string("C:/VS/18/Community/VC/Tools/MSVC/") + t + "/lib/x64/store/a.lib");
      SdkInputs i; i.fs = Fake(p); i.vswhereFound = true; i.installs = {Vs("C:\\VS\\18\\Community", "18.0.0.1", "")}; CHECK(EvaluateSdk(i).toolsetVersion == "14.100.1"); }
    // platform toolset NAME from the MSVC folder, and the toolset WITH the UWP libraries is the one picked when several are installed
    CHECK(PlatformToolsetFor("14.44.35207") == "v143" && PlatformToolsetFor("14.40.1") == "v143" && PlatformToolsetFor("14.38.33130") == "v143" && PlatformToolsetFor("14.50.35717") == "v145" && PlatformToolsetFor("14.29.1").empty() && PlatformToolsetFor("15.1").empty() && PlatformToolsetFor("x").empty());
    { auto p = SdkOnly(); const std::string vs = "C:/VS/18/Community";   // owner's layout: newest toolset 14.50 WITHOUT the UWP libs, 14.44 WITH them
      p.insert(vs + "/VC/Tools/MSVC/14.50.35717/lib/x64/x.lib"); p.insert(vs + "/VC/Tools/MSVC/14.44.35207/lib/x64/store/vcruntime.lib");
      SdkInputs i; i.fs = Fake(p); i.vswhereFound = true; i.installs = {Vs("C:\\VS\\18\\Community", "18.0.1.1", "Visual Studio Community 2026")};
      SdkReport r = EvaluateSdk(i); CHECK(r.available && r.toolsetVersion == "14.44.35207" && r.platformToolset == "v143"); }
    // an unknown major gets no generator (CMake picks), a missing version is tolerated
    CHECK(GeneratorForMajor(99).empty() && GeneratorForMajor(16) == "Visual Studio 16 2019" && MajorOf("") == 0 && MajorOf("17.9") == 17 && MajorOf("x") == 0);
    CHECK(!UwpComponentIds().empty() && std::string(kVcToolsComponentId()) == "Microsoft.VisualStudio.Component.VC.Tools.x86.x64");
}

static void ExportToolTests() {
    namespace fs = std::filesystem;
    ExportToolOptions o; std::string err;
    CHECK(ParseExportArgs({"--out", "x"}, o, err) && o.outDir == "x" && o.in.company == "OkaySpaceTest" && o.in.version == "1.0.0.0" && o.in.gameName == "OkaySpace Test");
    CHECK(ParseExportArgs({"--out", "x", "--name", "My Game", "--version", "2.1.0.0", "--company", "Me", "--bundle-id", "a.b", "--icon", "i.png"}, o, err) && o.in.gameName == "My Game" && o.in.version == "2.1.0.0" && o.in.company == "Me" && o.in.bundleId == "a.b" && o.in.iconPath == "i.png");
    CHECK(!ParseExportArgs({}, o, err) && err.find("--out") != std::string::npos);
    CHECK(!ParseExportArgs({"--out"}, o, err) && err.find("missing value") != std::string::npos);
    CHECK(!ParseExportArgs({"--out", "--name"}, o, err));
    CHECK(!ParseExportArgs({"--out", "x", "--bogus"}, o, err) && err.find("unknown") != std::string::npos);
    CHECK(ParseExportArgs({"--help"}, o, err) && o.help);
    // the folder it writes, into a folder that does not exist yet
    const fs::path tmp = fs::temp_directory_path() / "okay_xbox_tool_test" / "does" / "not" / "exist";
    fs::remove_all(fs::temp_directory_path() / "okay_xbox_tool_test");
    ExportToolOptions e; CHECK(ParseExportArgs({"--out", tmp.string()}, e, err));
    std::string msg; CHECK(WriteTestExport(e, msg));
    CHECK(fs::exists(tmp / "package" / "AppxManifest.xml") && fs::exists(tmp / "README_XBOX.txt") && fs::exists(tmp / "PLACEHOLDER_ASSETS.txt") && fs::exists(tmp / "package" / "Data" / "README.txt") && fs::exists(tmp / "xbox-export.json"));
    int pngs = 0; for (auto& f : fs::directory_iterator(tmp / "package" / "Assets")) if (f.path().extension() == ".png") ++pngs; CHECK(pngs == 14);
    { std::ifstream f(tmp / "package" / "AppxManifest.xml"); std::stringstream ss; ss << f.rdbuf(); const std::string m = ss.str();
      CHECK(m.find("CN=OkaySpaceTest") != std::string::npos && m.find("Version=\"1.0.0.0\"") != std::string::npos && m.find("Executable=\"OkaySpaceXbox.exe\"") != std::string::npos); }
    { ExportToolOptions bad = e; bad.in.version = "not a version"; std::string m2; CHECK(!WriteTestExport(bad, m2)); CHECK(!m2.empty()); }
    CHECK(!fs::exists(tmp / "package" / "Data" / "game.okayscene"));   // no scene unless asked
    // --with-scene: a config and a tiny scene the game runtime can resolve
    { ExportToolOptions s; CHECK(ParseExportArgs({"--out", (tmp.parent_path() / "scene").string(), "--with-scene"}, s, err) && s.withScene); std::string m3; CHECK(WriteTestExport(s, m3));
      const fs::path d = tmp.parent_path() / "scene" / "package" / "Data";
      CHECK(fs::exists(d / "game.okayconfig") && fs::exists(d / "game.okayscene"));
      std::ifstream cf(d / "game.okayconfig"); std::stringstream cs; cs << cf.rdbuf(); const GameConfig gc = ParseGameConfig(cs.str());
      CHECK(gc.title == "OkaySpace Test" && gc.startup == "game.okayscene" && gc.width == 1280);
      CHECK(ResolveStartupScene(gc, [&](const std::string& r) { return fs::exists(d / r); }) == "game.okayscene");
      std::ifstream sf(d / "game.okayscene"); std::stringstream ss; ss << sf.rdbuf(); const std::string sc = ss.str();
      CHECK(sc.rfind("okayscene 3\n", 0) == 0 && sc.find("gameobject 0 \"Main Camera\"") != std::string::npos && sc.find("  camera ") != std::string::npos && sc.find("mesh \"Cube\"") != std::string::npos);
      int gos = 0, ends = 0; std::istringstream ls(sc); std::string l; while (std::getline(ls, l)) { if (l.rfind("gameobject ", 0) == 0) ++gos; if (l == "end") ++ends; } CHECK(gos == 4 && ends == 4 && sc.find("uipanel ") != std::string::npos && sc.find("Hello Xbox") != std::string::npos);   // every object is closed
      // the field counts match the example scenes in examples/ (transform = 10 numbers; the camera line is the example's own)
      std::istringstream ls2(sc); while (std::getline(ls2, l)) if (l.rfind("  transform ", 0) == 0) { std::istringstream f(l.substr(12)); int n = 0; double v; while (f >> v) ++n; CHECK(n == 10); } }
    fs::remove_all(fs::temp_directory_path() / "okay_xbox_tool_test");
}

static void SdkTests() {
    SdkReport r = EvaluateSdk(In(Full()));
    CHECK(r.available);
    CHECK(r.toolsetVersion == "14.38.33130");
    CHECK(r.sdkVersion == "10.0.19041.0");
    CHECK(r.makeAppxPath.find("makeappx.exe") != std::string::npos);
    CHECK(r.items.size() == 6);

    { auto p = Full(); SdkReport x = EvaluateSdk(In(p, false)); CHECK(!x.available); CHECK(Item(x, Need::VisualStudio) == "missing");
      CHECK(x.items[0].install.find("vswhere") != std::string::npos); }   // the vswhere hint
    { auto p = Full(); p.erase("C:/VS/2022/Community/VC/Tools/MSVC/14.38.33130/lib/x64/store/vcruntime.lib");
      SdkReport x = EvaluateSdk(In(p)); CHECK(!x.available); CHECK(Item(x, Need::VisualStudio) == "found"); CHECK(Item(x, Need::UwpToolset) == "missing");
      CHECK(x.message.find("C++ Universal Windows Platform tools") != std::string::npos); }
    { auto p = Full(); p.erase("C:/Program Files (x86)/Windows Kits/10/bin/10.0.19041.0/x64/signtool.exe");
      SdkReport x = EvaluateSdk(In(p)); CHECK(!x.available); CHECK(Item(x, Need::SignTool) == "missing"); CHECK(Item(x, Need::MakeAppx) == "found"); }
    { auto p = Full(); p.erase("C:/Program Files (x86)/Windows Kits/10/Lib/10.0.19041.0/um/x64/WindowsApp.lib");
      SdkReport x = EvaluateSdk(In(p)); CHECK(!x.available); CHECK(Item(x, Need::WindowsSdk) == "found"); CHECK(Item(x, Need::UwpHeadersLibs) == "missing"); }
    // MakeAppx is optional: our own packer replaces it
    { auto p = Full(); p.erase("C:/Program Files (x86)/Windows Kits/10/bin/10.0.19041.0/x64/makeappx.exe");
      SdkReport x = EvaluateSdk(In(p)); CHECK(x.available); CHECK(Item(x, Need::MakeAppx) == "missing"); }
    // too old an SDK (16299) counts as no SDK
    { std::set<std::string> p; for (auto s : Full()) { auto q = s; size_t at; while ((at = q.find("10.0.19041.0")) != std::string::npos) q.replace(at, 12, "10.0.16299.0"); p.insert(q); }
      SdkReport x = EvaluateSdk(In(p)); CHECK(!x.available); CHECK(Item(x, Need::WindowsSdk) == "missing"); }
    // the newest SDK that is complete wins; a newer but empty one is ignored for headers/libs
    { auto p = Full(); p.insert("C:/Program Files (x86)/Windows Kits/10/Include/10.0.22621.0/um/x.h");
      SdkReport x = EvaluateSdk(In(p)); CHECK(x.available); CHECK(x.sdkVersion == "10.0.19041.0"); }
    { auto p = Full();   // both complete: the numerically newest (22621 > 19041, and 100 > 99 style ordering is numeric, not text)
      for (auto s : std::set<std::string>(p)) { auto q = s; size_t at; while ((at = q.find("10.0.19041.0")) != std::string::npos) q.replace(at, 12, "10.0.22621.0"); p.insert(q); }
      SdkReport x = EvaluateSdk(In(p)); CHECK(x.sdkVersion == "10.0.22621.0"); }
    // case: Windows file names are case-insensitive
    { auto p = Full(); p.erase("C:/Program Files (x86)/Windows Kits/10/bin/10.0.19041.0/x64/makeappx.exe"); p.insert("C:/Program Files (x86)/Windows Kits/10/bin/10.0.19041.0/x64/MakeAppx.exe");
      CHECK(EvaluateSdk(In(p)).available); }
    // vswhere lists an install that has no VC folder, then a good one: the good one is chosen
    { SdkInputs i = In(Full()); i.vswhereInstallPaths = {"C:\\VS\\Broken", "C:\\VS\\2022\\Community"}; CHECK(EvaluateSdk(i).available); }
    { SdkInputs i = In(Full()); i.vswhereInstallPaths.clear(); CHECK(!EvaluateSdk(i).available); }
    CHECK(ParseDottedVersion("10.0.19041.0").size() == 4);
    CHECK(ParseDottedVersion("10.0.x").empty());
    CHECK(ParseDottedVersion("").empty());
    CHECK(VersionLess(ParseDottedVersion("10.0.9.0"), ParseDottedVersion("10.0.10.0")));
}

static void ManifestTests() {
    CHECK(ValidateManifest(ManifestOptions{}).empty());
    CHECK(NormalizeVersion("1.2.3") == "1.2.3.0");
    CHECK(NormalizeVersion("1") == "1.0.0.0");
    CHECK(NormalizeVersion("1.2.3.4") == "1.2.3.4");
    CHECK(NormalizeVersion("1.2.3.4.5").empty());
    CHECK(NormalizeVersion("65536.0").empty());
    CHECK(NormalizeVersion("1.a").empty());
    CHECK(NormalizeVersion("").empty());
    CHECK(NormalizeVersion("1..2").empty());
    auto bad = [](auto edit) { ManifestOptions m; edit(m); return !ValidateManifest(m).empty(); };
    CHECK(bad([](ManifestOptions& m) { m.identityName = "ab"; }));
    CHECK(bad([](ManifestOptions& m) { m.identityName = std::string(51, 'a'); }));
    CHECK(!bad([](ManifestOptions& m) { m.identityName = std::string(50, 'a'); }));
    CHECK(bad([](ManifestOptions& m) { m.identityName = "my game"; }));
    CHECK(bad([](ManifestOptions& m) { m.identityName = "game."; }));
    CHECK(bad([](ManifestOptions& m) { m.identityName = "CON"; }));
    CHECK(bad([](ManifestOptions& m) { m.identityName = "com1"; }));
    CHECK(!bad([](ManifestOptions& m) { m.identityName = "Studio.My-Game2"; }));
    CHECK(bad([](ManifestOptions& m) { m.publisher = "OkaySpace"; }));
    CHECK(bad([](ManifestOptions& m) { m.publisher = ""; }));
    CHECK(bad([](ManifestOptions& m) { m.publisher = "CN=A\"B"; }));
    CHECK(bad([](ManifestOptions& m) { m.publisher = "O=Org"; }));   // no CN
    CHECK(!bad([](ManifestOptions& m) { m.publisher = "CN=Studio, O=Org, C=US"; }));
    CHECK(bad([](ManifestOptions& m) { m.version = "1.2.3.4.5"; }));
    CHECK(bad([](ManifestOptions& m) { m.executable = "dir/game.exe"; }));
    CHECK(bad([](ManifestOptions& m) { m.executable = "game.dll"; }));
    CHECK(bad([](ManifestOptions& m) { m.displayName = ""; }));
    CHECK(bad([](ManifestOptions& m) { m.capabilities = {"runFullTrust"}; }));   // full trust is not allowed for this target
    CHECK(bad([](ManifestOptions& m) { m.minVersion = "10.0"; }));
    CHECK(bad([](ManifestOptions& m) { m.backgroundColor = "red"; }));
    CHECK(bad([](ManifestOptions& m) { m.applicationId = "1App"; }));
    { std::vector<std::string> w; ManifestOptions m; m.version = "1.2.3.4"; ValidateManifest(m, &w); CHECK(w.size() == 1); }

    ManifestOptions m; m.displayName = "Tom & \"Jerry\" <1>"; m.identityName = "Studio.Game";
    const std::string x = GenerateManifest(m);
    CHECK(x.find("Name=\"Studio.Game\"") != std::string::npos);
    CHECK(x.find("ProcessorArchitecture=\"x64\"") != std::string::npos);
    CHECK(x.find("Name=\"Windows.Universal\"") != std::string::npos);
    CHECK(x.find("Version=\"1.0.0.0\"") != std::string::npos);
    CHECK(x.find("Tom &amp; &quot;Jerry&quot; &lt;1&gt;") != std::string::npos);
    CHECK(x.find("Tom & \"Jerry\"") == std::string::npos);   // never raw
    CHECK(x.find("<Capability Name=\"internetClient\"") != std::string::npos);
    CHECK(x.find("runFullTrust") == std::string::npos);
    CHECK(x.find("UNVERIFIED") != std::string::npos);
    CHECK(x.find("Microsoft.VCLibs.140.00") != std::string::npos);
    for (const AssetSpec& a : RequiredAssets()) {   // every image the manifest names is in the required list, and vice versa
        const std::string nm = std::string("Assets\\") + a.file;
        if (std::string(a.file) != "SplashScreen.png" || true) CHECK(x.find(nm) != std::string::npos);
    }
    CHECK(RequiredAssets().size() == 7);
    // a tag-balanced check: every '<' opens something and the document has one Package root
    int depth = 0; bool wellFormed = true;
    for (size_t i = 0; i < x.size(); ++i) {
        if (x.compare(i, 4, "<!--") == 0) { i = x.find("-->", i); if (i == std::string::npos) { wellFormed = false; break; } continue; }
        if (x.compare(i, 2, "<?") == 0) { i = x.find("?>", i); continue; }
        if (x[i] != '<') continue;
        const size_t e = x.find('>', i); if (e == std::string::npos) { wellFormed = false; break; }
        if (x[i + 1] == '/') --depth; else if (x[e - 1] != '/') ++depth;
        if (depth < 0) wellFormed = false;
        i = e;
    }
    CHECK(wellFormed && depth == 0);

    CHECK(IdentityNameFrom("com.my studio.Game 2") == "com.my-studio.Game-2");
    CHECK(IdentityNameFrom("").size() >= 3);
    CHECK(ValidateManifest([] { ManifestOptions o; o.identityName = IdentityNameFrom("..//..//"); return o; }()).empty());
    CHECK(PublisherFrom("Acme, Inc.") == "CN=Acme Inc.");
    CHECK(PublisherFrom("") == "CN=OkaySpaceDev");
    CHECK(PublisherLooksValid(PublisherFrom("a=b;c+d\"e")));
}

static void AssetTests() {
    // resize: a 4x4 image of two halves shrinks to 2x1 keeping the colours; alpha counts
    okay::Image im(4, 4);
    for (int y = 0; y < 4; ++y) for (int x = 0; x < 4; ++x) { auto* p = im.Data() + (y * 4 + x) * 4; p[0] = x < 2 ? 200 : 0; p[1] = 0; p[2] = x < 2 ? 0 : 100; p[3] = 255; }
    okay::Image s = ResizeBox(im, 2, 1);
    CHECK(s.Data()[0] == 200 && s.Data()[2] == 0 && s.Data()[3] == 255);
    CHECK(s.Data()[4] == 0 && s.Data()[6] == 100);
    okay::Image t = ResizeBox(im, 1, 1);   // mix of both halves
    CHECK(t.Data()[0] == 100 && t.Data()[2] == 50);
    okay::Image tr(2, 2);   // fully transparent stays transparent (no NaN, no colour)
    CHECK(ResizeBox(tr, 1, 1).Data()[3] == 0);
    okay::Image up = ResizeBox(im, 8, 8);   // enlarging keeps every output pixel defined
    CHECK(up.Valid() && up.Data()[3] == 255 && up.Data()[(7 * 8 + 7) * 4 + 3] == 255);
    okay::Image fit = FitOnBackground(MakePlaceholderIcon(64), 310, 150, ParseBackground("#102030"));
    CHECK(fit.Width() == 310 && fit.Height() == 150);
    CHECK(fit.Data()[0] == 0x10 && fit.Data()[1] == 0x20 && fit.Data()[2] == 0x30 && fit.Data()[3] == 255);   // corner = background
    CHECK(ParseBackground("#FFa000").g == 0xa0);
}

static void ExportTests() {
    namespace fs = std::filesystem;
    const fs::path tmp = fs::temp_directory_path() / "okay_xbox_uwp_test";
    std::error_code ec; fs::remove_all(tmp, ec);
    ExportInputs in; in.gameName = "Star <Arena>"; in.company = "Acme, Inc."; in.version = "1.4.2"; in.bundleId = "com.acme.star arena";
    ExportResult r = WritePackage(tmp.string(), in);
    CHECK(r.ok); CHECK(r.placeholderAssets);
    CHECK(fs::exists(tmp / "package" / "AppxManifest.xml"));
    CHECK(fs::exists(tmp / "package" / "Assets" / "Square44x44Logo.png"));
    CHECK(fs::exists(tmp / "package" / "Assets" / "SplashScreen.scale-200.png"));
    CHECK(fs::exists(tmp / "PLACEHOLDER_ASSETS.txt"));
    CHECK(fs::exists(tmp / "README_XBOX.txt"));
    int pngs = 0; for (auto& e : fs::directory_iterator(tmp / "package" / "Assets")) { (void)e; ++pngs; }
    CHECK(pngs == 14);
    std::ifstream f((tmp / "package" / "AppxManifest.xml").string()); std::stringstream ss; ss << f.rdbuf();
    CHECK(ss.str().find("Publisher=\"CN=Acme Inc.\"") != std::string::npos);
    CHECK(ss.str().find("Version=\"1.4.2.0\"") != std::string::npos);
    CHECK(ss.str().find("Star &lt;Arena&gt;") != std::string::npos);
    std::ifstream rd((tmp / "README_XBOX.txt").string()); std::stringstream rs; rs << rd.rdbuf();
    CHECK(rs.str().find("UNVERIFIED") != std::string::npos);
    CHECK(rs.str().find("does not load Data/") != std::string::npos);
    CHECK(rs.str().find("Engine mode") != std::string::npos && rs.str().find("software-painted") != std::string::npos);   // says honestly what runs
    CHECK(rs.str().find("one administrator prompt") != std::string::npos);
    // a real icon: the placeholder flag stays off and the placeholder note is not written
    fs::remove_all(tmp, ec);
    okay::Image icon = MakePlaceholderIcon(32); fs::create_directories(tmp, ec);
    CHECK(icon.SavePNG((tmp / "icon.png").string()));
    ExportInputs in2 = in; in2.iconPath = (tmp / "icon.png").string();
    ExportResult r2 = WritePackage((tmp / "out").string(), in2);
    CHECK(r2.ok); CHECK(!r2.placeholderAssets); CHECK(!fs::exists(tmp / "out" / "PLACEHOLDER_ASSETS.txt"));
    // bad input stops the export and writes nothing
    ExportInputs in3 = in; in3.version = "1.x";
    ExportResult r3 = WritePackage((tmp / "bad").string(), in3);
    CHECK(!r3.ok); CHECK(!fs::exists(tmp / "bad" / "package"));
    fs::remove_all(tmp, ec);
}

static std::vector<appx::PackItem> Items() {
    std::vector<appx::PackItem> v;
    auto add = [&](const std::string& n, const std::string& d) { appx::PackItem i; i.name = n; i.data = d; v.push_back(i); };
    add("AppxManifest.xml", GenerateManifest(ManifestOptions{}));
    add("Assets/Logo.png", std::string("\x89PNG fake", 9));
    std::string big(130000, 'x'); for (size_t i = 0; i < big.size(); ++i) big[i] = char(i * 7 + (i >> 8));
    add("Data/big file.bin", big);            // 2 blocks, a space in the name
    add("Data/empty.txt", "");                // no blocks
    add("Data/no_extension", "abc");          // needs an Override, not a Default
    add("OkaySpaceXbox.exe", "MZ-not-really");
    return v;
}
static bool HasProblem(const appx::VerifyResult& v, const char* part) {
    for (auto& p : v.problems) if (p.find(part) != std::string::npos) return true;
    return false;
}
static void AppxTests() {
    using namespace appx;
    // known answers: CRC-32 of "123456789" and RFC 4648 base64 vectors
    CHECK(appx::detail::Crc32(reinterpret_cast<const std::uint8_t*>("123456789"), 9) == 0xCBF43926u);
    auto b64 = [](const char* s) { return appx::detail::Base64(reinterpret_cast<const std::uint8_t*>(s), std::strlen(s)); };
    CHECK(b64("") == ""); CHECK(b64("f") == "Zg=="); CHECK(b64("fo") == "Zm8="); CHECK(b64("foo") == "Zm9v"); CHECK(b64("foob") == "Zm9vYg==");
    CHECK(b64("fooba") == "Zm9vYmE="); CHECK(b64("foobar") == "Zm9vYmFy");
    // SHA-256("abc") through the block hash helper, base64 of ba7816bf...15ad
    CHECK(appx::detail::BlockHashB64(reinterpret_cast<const std::uint8_t*>("abc"), 3) == "ungWv48Bz+pBQUDeXa4iI7ADYaOWF3qctBD/YfIAFa0=");

    PackResult r = Pack(Items());
    CHECK(r.ok); CHECK(r.files == 6); CHECK(r.blocks == 6);
    VerifyResult v = Verify(r.bytes);
    for (auto& p : v.problems) std::printf("  problem: %s\n", p.c_str());
    CHECK(v.ok); CHECK(v.files == 6); CHECK(v.blocks == 6);   // manifest 1, logo 1, big 2 (130000 bytes), empty 0, no_ext 1, exe 1
    CHECK(!v.signedPackage);
    CHECK(Pack(Items()).bytes == r.bytes);   // deterministic
    CHECK(r.bytes.compare(30, 16, "AppxManifest.xml") == 0);   // the manifest is the first entry
    CHECK(r.bytes.find("Data/big%20file.bin") != std::string::npos);   // percent-encoded in the ZIP
    CHECK(r.bytes.find("Data\\big file.bin") != std::string::npos);    // plain with '\\' in the block map
    CHECK(r.bytes.find("<Override PartName=\"/Data/no_extension\"") != std::string::npos);
    CHECK(r.bytes.find("<Default Extension=\"png\" ContentType=\"image/png\"/>") != std::string::npos);
    // corruption is caught: a payload byte, a block hash, a missing block map, a truncated file
    { std::string b = r.bytes; size_t at = b.find("MZ-not-really"); b[at] ^= 1; VerifyResult x = Verify(b); CHECK(!x.ok); CHECK(HasProblem(x, "CRC-32")); }
    { std::string b = r.bytes; size_t at = b.find("Block Hash=\"") + 12; b[at] = b[at] == 'A' ? 'B' : 'A'; VerifyResult x = Verify(b); CHECK(!x.ok); CHECK(HasProblem(x, "hash mismatch")); }
    { std::string b = r.bytes; size_t at = b.find("AppxBlockMap.xml"); b[at] = 'X'; VerifyResult x = Verify(b); CHECK(!x.ok); }
    { std::string b = r.bytes.substr(0, r.bytes.size() - 10); CHECK(!Verify(b).ok); }
    CHECK(!Verify("").ok);
    CHECK(!Verify(std::string(100, 'a')).ok);
    // name rules
    auto one = [](const char* name) { auto it = Items(); appx::PackItem i; i.name = name; i.data = "x"; it.push_back(i); return Pack(it).ok; };
    CHECK(one("Data/ok.bin")); CHECK(!one("Data/../evil")); CHECK(!one("/abs")); CHECK(!one("a\\b")); CHECK(!one("AppxBlockMap.xml"));
    CHECK(!one("[Content_Types].xml")); CHECK(!one("data/x:y")); CHECK(!one("trail.")); CHECK(!one("")); CHECK(!one("Data/EMPTY.TXT"));   // same as Data/empty.txt ignoring case
    CHECK(!one("a//b"));
    { auto it = Items(); it.erase(it.begin()); CHECK(!Pack(it).ok); }   // no manifest
    // a ZIP read by anyone else: write it out for tools/ci_appx_crosscheck (python zipfile) when asked
    if (const char* d = std::getenv("OKAY_APPX_TEST_OUT")) { std::ofstream(std::string(d), std::ios::binary) << r.bytes; }
    // the folder form
    namespace fs = std::filesystem;
    const fs::path tmp = fs::temp_directory_path() / "okay_appx_folder_test";
    std::error_code ec; fs::remove_all(tmp, ec); fs::create_directories(tmp / "Assets", ec);
    { std::ofstream(tmp / "AppxManifest.xml") << GenerateManifest(ManifestOptions{}); std::ofstream(tmp / "Assets" / "a.png") << "png"; std::ofstream(tmp / "AppxBlockMap.xml") << "stale"; }
    PackResult f = PackFolder(tmp.string());
    CHECK(f.ok); CHECK(f.files == 2); CHECK(Verify(f.bytes).ok);
    fs::remove_all(tmp, ec);
    CHECK(!PackFolder("/definitely/not/here").ok);
}

int main() {
    SdkTests();
    NewerVsTests(); ExportToolTests(); ManifestTests(); AppxTests(); AssetTests(); ExportTests();
    if (g_fail) { std::printf("%d check(s) FAILED\n", g_fail); return 1; }
    std::printf("xbox_uwp: all checks passed\n");
    return 0;
}
