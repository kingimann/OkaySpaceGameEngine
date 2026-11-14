// ExportReport (editor/ExportReport.hpp): the plain-English lines after a Web / Android / Server export.
// No GUI, no real machine: the environment is faked.
#include "test_framework.hpp"
#include "ExportReport.hpp"
#include <map>
#include <set>

using namespace okay::editor::exportreport;

static Env FakeEnv(std::map<std::string, std::string> vars, std::set<std::string> files) {
    Env e;
    e.getVar = [vars](const std::string& n) { auto it = vars.find(n); return it == vars.end() ? std::string() : it->second; };
    e.fileExists = [files](const std::string& p) { return files.count(p) > 0; };
    return e;
}
static bool Has(const std::vector<Line>& l, Severity s, const std::string& part) {
    for (const Line& x : l) if (x.severity == s && x.text.find(part) != std::string::npos) return true;
    return false;
}
static bool HasAny(const std::vector<Line>& l, const std::string& part) {
    for (const Line& x : l) if (x.text.find(part) != std::string::npos) return true;
    return false;
}
static int Count(const std::vector<Line>& l, Severity s) { int n = 0; for (const Line& x : l) n += x.severity == s; return n; }

static void TestOnPath() {
    Env e = FakeEnv({{"PATH", "/usr/bin:/opt/emsdk/upstream/emscripten"}}, {"/opt/emsdk/upstream/emscripten/emcc"});
    CHECK(OnPath("emcc", e));
    CHECK(!OnPath("gcc", e));
    Env w = FakeEnv({{"PATH", "C:\\Windows;C:\\emsdk\\emcc_dir"}}, {"C:\\emsdk\\emcc_dir/emcc.bat"});
    CHECK(OnPath("emcc", w));                      // drive letters are not split, .bat is found
    CHECK(!OnPath("emcc", FakeEnv({}, {})));       // no PATH at all
}

static void TestWeb() {
    Settings def;
    Env withEmcc = FakeEnv({{"PATH", "/e"}}, {"/e/emcc"});
    Env noEmcc = FakeEnv({{"PATH", "/e"}}, {});
    auto a = MakeReport(Target::Web, {}, def, withEmcc);
    CHECK(Has(a, Severity::Note, "emcc) was found"));
    CHECK(Count(a, Severity::Warning) == 0);
    CHECK(HasAny(a, "software renderer"));
    CHECK(HasAny(a, "No .wasm was built"));
    auto b = MakeReport(Target::Web, {}, def, noEmcc);
    CHECK(Has(b, Severity::Warning, "not found on PATH"));
    CHECK(HasAny(b, "EMSDK variable is not set"));
    auto c = MakeReport(Target::Web, {}, def, FakeEnv({{"PATH", "/e"}, {"EMSDK", "/x"}}, {}));
    CHECK(Has(c, Severity::Warning, "not found on PATH"));
    CHECK(!HasAny(c, "EMSDK variable"));
}

static void TestAndroid() {
    Settings def;
    auto both = MakeReport(Target::Android, {}, def, FakeEnv({{"ANDROID_NDK_ROOT", "/n"}, {"ANDROID_HOME", "/h"}}, {}));
    CHECK(Count(both, Severity::Warning) == 0);
    CHECK(HasAny(both, "no APK and no Gradle project"));
    auto none = MakeReport(Target::Android, {}, def, FakeEnv({}, {}));
    CHECK(Has(none, Severity::Warning, "ANDROID_NDK_ROOT is not set"));
    CHECK(Has(none, Severity::Warning, "ANDROID_HOME is not set"));
    auto ndkOnly = MakeReport(Target::Android, {}, def, FakeEnv({{"ANDROID_NDK_ROOT", "/n"}}, {}));
    CHECK(!HasAny(ndkOnly, "ANDROID_NDK_ROOT is not set"));
    CHECK(HasAny(ndkOnly, "ANDROID_HOME is not set"));
}

static void TestServerHasNoToolChecks() {
    auto r = MakeReport(Target::Server, {}, Settings{}, FakeEnv({}, {}));
    CHECK(r.empty());
}

static void TestIgnoredSettings() {
    Env e = FakeEnv({{"PATH", "/e"}}, {"/e/emcc"});
    for (Target t : {Target::Web, Target::Android, Target::Server}) {
        CHECK(!HasAny(MakeReport(t, {}, Settings{}, e), "ignores these Build Settings"));   // defaults: nothing to say
        Settings s; s.layout = 2; s.stripAssets = false; s.compress = false; s.buildNumber = 7;
        auto r = MakeReport(t, {}, s, e);
        CHECK(Has(r, Severity::Warning, std::string(TargetName(t)) + " export ignores"));
        CHECK(HasAny(r, "Layout (Zip)"));
        CHECK(HasAny(r, "Strip unused assets"));
        CHECK(HasAny(r, "Compress"));
        CHECK(HasAny(r, "Build number (7)"));
    }
    Settings one; one.layout = 1;
    auto r = MakeReport(Target::Server, {}, one, FakeEnv({}, {}));
    CHECK(HasAny(r, "Layout (Compact)"));
    CHECK(!HasAny(r, "Compress"));
}

static void TestMissing() {
    Env e = FakeEnv({}, {});
    std::vector<MissingAsset> one{{"art/hero.png", "Player"}};
    auto r = MakeReport(Target::Server, one, Settings{}, e);
    CHECK(Has(r, Severity::Warning, "1 asset is missing"));
    CHECK(HasAny(r, "art/hero.png (used by Player)"));
    std::vector<MissingAsset> noRef{{"a.png", ""}};
    CHECK(!HasAny(MakeReport(Target::Server, noRef, Settings{}, e), "used by"));

    std::vector<MissingAsset> many;
    for (int i = 0; i < 25; ++i) many.push_back({"f" + std::to_string(i) + ".png", "the scene"});
    auto m = MakeReport(Target::Server, many, Settings{}, e);
    CHECK(Has(m, Severity::Warning, "25 assets are missing"));
    CHECK(HasAny(m, "f19.png"));
    CHECK(!HasAny(m, "f20.png"));
    CHECK(HasAny(m, "and 5 more"));
    std::vector<MissingAsset> exactly(many.begin(), many.begin() + 20);
    CHECK(!HasAny(MakeReport(Target::Server, exactly, Settings{}, e), " more"));
}

static void TestCombinationsAndText() {
    Env ok = FakeEnv({{"PATH", "/e"}, {"ANDROID_NDK_ROOT", "/n"}, {"ANDROID_HOME", "/h"}}, {"/e/emcc"});
    Env bad = FakeEnv({}, {});
    Settings s; s.compress = false;
    std::vector<MissingAsset> miss{{"x.wav", "Door"}};
    for (Target t : {Target::Web, Target::Android, Target::Server}) {
        for (const Env* env : {&ok, &bad}) {
            for (int withMiss = 0; withMiss < 2; ++withMiss) {
                for (int withIgn = 0; withIgn < 2; ++withIgn) {
                    auto r = MakeReport(t, withMiss ? miss : std::vector<MissingAsset>{}, withIgn ? s : Settings{}, *env);
                    CHECK(HasAny(r, "x.wav") == (withMiss == 1));
                    CHECK(HasAny(r, "ignores these") == (withIgn == 1));
                    CHECK(!HasErrors(r));
                }
            }
        }
    }
    auto r = MakeReport(Target::Android, miss, Settings{}, bad);
    std::string txt = ToText(r);
    CHECK(txt.find("[Warning] 1 asset is missing") == 0);
    CHECK(txt.find("[Note] This export wrote assets") != std::string::npos);
    std::vector<Line> err{{Severity::Error, "boom"}};
    CHECK(HasErrors(err));
    CHECK(ToText(err) == "[Error] boom");
}

int main() {
    TestOnPath();
    TestWeb();
    TestAndroid();
    TestServerHasNoToolChecks();
    TestIgnoredSettings();
    TestMissing();
    TestCombinationsAndText();
    TEST_MAIN_RESULT();
}
