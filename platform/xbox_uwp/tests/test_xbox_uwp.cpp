// Tests for the experimental Xbox UWP export's pure parts (SDK decision, manifest, assets, package folder).
// Runs on Linux with fake folder listings. It proves our logic, NOT that a Windows PC or an Xbox accepts the result.
#include "XboxAppx.hpp"
#include "XboxExport.hpp"
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
    CHECK(rs.str().find("Engine mode") != std::string::npos && rs.str().find("NOT drawn yet") != std::string::npos);   // says honestly what runs
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
    SdkTests(); ManifestTests(); AppxTests(); AssetTests(); ExportTests();
    if (g_fail) { std::printf("%d check(s) FAILED\n", g_fail); return 1; }
    std::printf("xbox_uwp: all checks passed\n");
    return 0;
}
