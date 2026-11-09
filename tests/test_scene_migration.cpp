// SceneMigration: the registry of `okayscene N -> N+1` steps, MigrateToCurrent, the
// refusal of newer files, old-format fixtures for every version the repo history shows,
// and (clearly marked SYNTHETIC) steps registered only inside this test to exercise the
// framework past the current version.
#include "scene_text_testkit.hpp"
#include "okay/Scene/SceneMigration.hpp"

#include <fstream>

using namespace okay;
using namespace tk;

// ---- fixtures: the formats the repository history shows ------------------------------
//
// Format 1 is the original. `git log -S"okayscene 2"` finds PR #346 (a Character body gets
// a visible Animator; the loader adds one to every Character in a file older than 2), and
// `git log -S"okayscene 3"` finds PR #510 (a switchable first / third person player gets
// both view controllers; the loader completes one in a file older than 3). The guid,
// sibling, compoff, override and layermatrix records came later without a version bump, so
// old files simply do not have them.

// A small hand-written version 1 file: the record shapes of that era, no ids, no
// optional trailing fields.
static const char* kV1 =
    "okayscene 1\n"
    "name \"Old Level\"\n"
    "gravity 0 -9.81\n"
    "rendersettings 1 0.27 0.47 0.78 0.58 0.72 0.88 0.47 0.47 0.5 0.15\n"
    "fog 0 0.58 0.72 0.88 20 90\n"
    "gameobject 0 \"Hero\"\n"
    "  active 1\n"
    "  parent -1\n"
    "  transform 1 2 0 0 0 0 1 1 1 1\n"
    "  sprite 64 1 0 0 1 1 1\n"
    "  rigidbody2d 1 1 1 0 0\n"
    "  boxcollider2d 1 1 0 0 0\n"
    "end\n"
    "gameobject 1 \"Cam\"\n"
    "  active 1\n"
    "  parent 0\n"
    "  transform 0 0 -10 0 0 0 1 1 1 1\n"
    "  camera 0 5 60 0.1 0.1 0.1 1 1\n"
    "end\n"
    "gameobject 2 \"Label\"\n"
    "  active 1\n"
    "  tag \"ui\"\n"
    "  parent -1\n"
    "  transform 0 3 0 0 0 0 1 1 1 1\n"
    "end\n";

// Version 2: the same shapes, written by a build that already gave bodies their Animator;
// the sprite carries the uv rectangle that was added later.
static const char* kV2 =
    "okayscene 2\n"
    "name \"Level\"\n"
    "gravity 0 -9.81\n"
    "gameobject 0 \"Hero\"\n"
    "  active 1\n"
    "  parent -1\n"
    "  transform 1 2 0 0 0 0 1 1 1 1\n"
    "  sprite 64 1 0 0 1 1 1 \"hero.png\" 0 0 1 1 3 0 0\n"
    "end\n"
    "gameobject 1 \"Coin\"\n"
    "  active 0\n"
    "  parent 0\n"
    "  transform 0 1 0 0 0 0 1 0.5 0.5 1\n"
    "end\n";

static std::string ReadFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static std::string WithVersion(const std::string& text, int version) {
    const std::size_t nl = text.find('\n');
    return "okayscene " + std::to_string(version) + text.substr(nl);
}

static bool Same(const std::string& a, const std::string& b) {
    std::string ra, rb;
    return Reserialize(a, &ra) && Reserialize(b, &rb) && StripGuids(ra) == StripGuids(rb);
}

static void TestRegistry() {
    RUN_SUITE("migration: the registry");
    const auto steps = SceneMigration::Steps();
    CHECK(SceneMigration::CurrentVersion() == 3);
    CHECK(steps.size() == 2 && steps[0].from == 1 && steps[1].from == 2);
    CHECK(steps[0].name == "character-animator" && steps[1].name == "view-toggle-controllers");
    CHECK(steps[0].needsEngine && steps[1].needsEngine);
    const std::string d = SceneMigration::Describe();
    CHECK(Has(d, "okayscene 1 -> 2") && Has(d, "okayscene 2 -> 3") && Has(d, "#346") && Has(d, "#510"));

    SceneMigration::Step bad;
    bad.from = 0;
    CHECK(!SceneMigration::Register(bad));              // below 1
    bad.from = 1;
    CHECK(!SceneMigration::Register(bad));              // already there
    CHECK(!SceneMigration::Unregister(77));
    CHECK(SceneMigration::Steps().size() == 2);
}

static void TestOldFormats() {
    RUN_SUITE("migration: old format fixtures");
    // Version 1, hand written.
    {
        const auto r = SceneMigration::MigrateToCurrent(kV1);
        CHECK(r.ok && r.complete && r.changed && !r.needsEngine && r.fromVersion == 1 && r.toVersion == 3);
        CHECK(r.text.compare(0, 12, "okayscene 3\n") == 0);
        CHECK(r.log.size() == 2 && Has(r.log[0], "okayscene 1 -> 2") && Has(r.log[1], "okayscene 2 -> 3"));
        CHECK(Has(r.log[0], "nothing in this scene needs it"));
        CHECK(SceneText::Canonicalize(r.text) == r.text);
        CHECK(Same(kV1, r.text));                                 // loads as the same scene
        // The records are all still there.
        SceneText::Model a, b;
        CHECK(SceneText::BuildModel(kV1, a) && SceneText::BuildModel(r.text, b));
        CHECK(a.objects.size() == b.objects.size());
        for (std::size_t i = 0; i < a.objects.size(); ++i) CHECK(a.objects[i].records.size() == b.objects[i].records.size());
        // Idempotent.
        const auto again = SceneMigration::MigrateToCurrent(r.text);
        CHECK(again.ok && !again.changed && again.complete && again.text == r.text && again.log.empty());
    }
    // Version 2, hand written.
    {
        const auto r = SceneMigration::MigrateToCurrent(kV2);
        CHECK(r.ok && r.complete && r.fromVersion == 2 && r.toVersion == 3 && r.log.size() == 1 && Has(r.log[0], "2 -> 3"));
        CHECK(Same(kV2, r.text));
        CHECK(Has(r.text, "sprite 64 1 0 0 1 1 1 \"hero.png\" 0 0 1 1 3 0 0"));
    }
    // A real version 1 file from the repository (the OkayVS export fixture).
    {
        const std::string real = ReadFile(std::string(OKAY_SOURCE_DIR) + "/tests/export/vs_export.okayscene");
        CHECK(real.compare(0, 12, "okayscene 1\n") == 0);
        const auto r = SceneMigration::MigrateToCurrent(real);
        CHECK(r.ok && r.complete && r.toVersion == 3 && Has(r.text, "gameobject 9 \"Tower\""));
        CHECK(Same(real, r.text));
        CHECK(Has(r.text, "visualscriptpath \"hero.okayvs\""));
    }
    // A file that is already current is not touched, even when it is messy.
    {
        TkRng r(9);
        const std::string x = LiveSceneText(r, 15);
        const std::string messy = Mess(x, r);
        const auto m = SceneMigration::MigrateToCurrent(messy);
        CHECK(m.ok && m.complete && !m.changed && m.text == messy && m.fromVersion == 3 && m.log.empty());
    }
    // Random scenes saved as version 1 or 2 come out as the same scenes at version 3.
    for (std::uint64_t seed = 1; seed <= (std::uint64_t)Seeds(40); ++seed) {
        TkRng r(seed + 500);
        const std::string x = LiveSceneText(r, 1 + r.range(40), r.chance(0.7));
        const int v = 1 + r.range(2);
        const std::string old = WithVersion(x, v);
        const auto m = SceneMigration::MigrateToCurrent(old);
        CHECK(m.ok && m.complete && !m.needsEngine && m.toVersion == 3);
        CHECK(m.text == SceneText::Canonicalize(x));
        CHECK(Same(old, m.text));
    }
}

// Version 1 and 2 files whose upgrade the loader performs: the text step stops, the engine
// path writes the upgraded scene.
static void TestEngineSteps() {
    RUN_SUITE("migration: upgrades that need the engine");
    {
        Scene s("Chars");
        GameObject* p = s.CreateGameObject("Player");
        p->AddComponent<Character>()->Apply();
        std::string v1 = WithVersion(SceneSerializer::Serialize(s), 1);
        CHECK(Has(v1, "  character "));
        CHECK(!Has(v1, "animsm"));                               // a version 1 body had no Animator
        const auto t = SceneMigration::MigrateToCurrent(v1);
        CHECK(t.ok && t.needsEngine && !t.complete && t.toVersion == 1 && t.text == v1 && !t.changed);
        CHECK(Has(t.log[0], "character-animator") && Has(t.log[0], "only the engine's loader"));

        const auto e = SceneMigration::MigrateThroughEngine(v1);
        CHECK(e.ok && e.complete && !e.needsEngine && e.toVersion == 3 && e.changed);
        CHECK(e.text.compare(0, 12, "okayscene 3\n") == 0);
        CHECK(Has(e.text, "animsm"));                            // the Animator the loader adds
        CHECK(Has(e.log.back(), "wrote it back out") || Has(e.log[e.log.size() - 1], "okayscene 3"));
        // Stable: loading the upgraded file again changes nothing, and it needs no more upgrades.
        std::string again;
        CHECK(Reserialize(e.text, &again) && StripGuids(again) == StripGuids(e.text));
        const auto m = SceneMigration::MigrateThroughEngine(e.text);
        CHECK(m.ok && !m.changed && m.complete && m.text == e.text);
        // The same upgrade the loader gives the old file.
        std::string viaLoader;
        CHECK(Reserialize(v1, &viaLoader) && StripGuids(viaLoader) == StripGuids(e.text));
    }
    {
        Scene s("Toggle");
        GameObject* p = s.CreateGameObject("Player");
        p->AddComponent<FirstPersonController>();
        Templates::AddViewToggle(p, PlayerView::View::First, 0.45f);
        const std::string v2 = WithVersion(SceneSerializer::Serialize(s), 2);
        const auto t = SceneMigration::MigrateToCurrent(v2);
        CHECK(Has(v2, "  playerview "));
        CHECK(t.ok && t.needsEngine && t.toVersion == 2 && Has(t.log[0], "view-toggle-controllers"));
        const auto e = SceneMigration::MigrateThroughEngine(v2);
        // The old one-controller player came out with both view controllers (under a Player Controller).
        CHECK(!Has(e.text, "  playerview ") && Has(e.text, "  pcfirst ") && Has(e.text, "  pcthird "));
        CHECK(e.changed && !Has(v2, "  pcthird "));
        CHECK(e.ok && e.complete && e.toVersion == 3);
        std::string viaLoader;
        CHECK(Reserialize(v2, &viaLoader) && StripGuids(viaLoader) == StripGuids(e.text));
        const auto m = SceneMigration::MigrateThroughEngine(e.text);
        CHECK(m.ok && !m.changed);
    }
    // MigrateThroughEngine on a file that has nothing for the loader: plain text migration.
    {
        const auto r = SceneMigration::MigrateThroughEngine(kV1);
        CHECK(r.ok && r.complete && r.text == SceneMigration::MigrateToCurrent(kV1).text);
    }
    // A scene the loader refuses is reported, not rewritten.
    {
        const std::string broken = "okayscene 1\ngameobject 0 \"A\"\n  character 1 2 3\n  parent 0\nend\n";   // parent cycle
        const auto r = SceneMigration::MigrateThroughEngine(broken);
        CHECK(!r.ok && r.text == broken && !r.error.empty());
    }
}

static void TestRefusals() {
    RUN_SUITE("migration: files it must not touch");
    const std::string newer = "okayscene 4\nname \"Future\"\nfuturething 1 2 3\ngameobject 0 \"F\"\n  active 1\n  parent -1\nend\n";
    auto r = SceneMigration::MigrateToCurrent(newer);
    CHECK(!r.ok && r.text == newer && !r.changed && Has(r.error, "newer") && Has(r.error, "okayscene 4") && r.log.empty());
    r = SceneMigration::MigrateToCurrent("okayscene 99\n");
    CHECK(!r.ok && Has(r.error, "newer"));
    const auto e = SceneMigration::MigrateThroughEngine(newer);
    CHECK(!e.ok && e.text == newer);
    const char* junk[] = {"", "hello", "okayscene\n", "okayscene 0\n", "okayscene x\n", "okayscene -2\n",
                          "okayscene 1\ngameobject 0 \"A\"\n  active 1\n", "okayscene 2\nname \"unterminated\n"};
    for (const char* j : junk) {
        const auto x = SceneMigration::MigrateToCurrent(j);
        CHECK(!x.ok && x.text == j && !x.error.empty() && !x.changed);
    }
    // Asking for an older target than the file's version is the same refusal.
    r = SceneMigration::MigrateTo(WithVersion(newer, 3), 2);
    CHECK(!r.ok && Has(r.error, "newer"));
}

// ---- SYNTHETIC steps: registered only here, to exercise the framework beyond version 3 ----

static void RegisterSynthetic() {
    SceneMigration::Step a;
    a.from = 3;
    a.name = "SYNTHETIC-TEST-rename-record";
    a.description = "test only: rename the record `oldthing` to `newthing`";
    a.affects = [](const SceneText::Document& d) {
        for (const auto& o : d.objects) for (const auto& r : o.records) if (r.key == "oldthing") return true;
        return false;
    };
    a.apply = [](SceneText::Document& d, std::vector<std::string>& log, std::string&) {
        int n = 0;
        for (auto& o : d.objects)
            for (auto& r : o.records)
                if (r.key == "oldthing") { r.key = "newthing"; r.text = "newthing" + r.text.substr(8); ++n; }
        log.push_back("renamed " + std::to_string(n) + " oldthing record(s)");
        return true;
    };
    CHECK(SceneMigration::Register(a));
    SceneMigration::Step b;
    b.from = 4;
    b.name = "SYNTHETIC-TEST-add-tag";
    b.description = "test only: untagged objects get the tag \"migrated\"";
    b.apply = [](SceneText::Document& d, std::vector<std::string>& log, std::string&) {
        int n = 0;
        for (auto& o : d.objects) {
            bool has = false;
            for (const auto& r : o.records) has = has || r.key == "tag";
            if (!has) { o.records.push_back({"tag", "tag \"migrated\""}); ++n; }
        }
        log.push_back("tagged " + std::to_string(n) + " object(s)");
        return true;
    };
    CHECK(SceneMigration::Register(b));
}

static void UnregisterSynthetic() {
    SceneMigration::Unregister(3);
    SceneMigration::Unregister(4);
    SceneMigration::Unregister(5);
}

static void TestSyntheticSteps() {
    RUN_SUITE("migration: framework with SYNTHETIC steps");
    const std::string v3 =
        "okayscene 3\ngameobject 0 \"A\"\n  active 1\n  parent -1\n  oldthing 1 2\nend\n"
        "gameobject 1 \"B\"\n  active 1\n  tag \"keep\"\n  parent -1\nend\n";
    RegisterSynthetic();
    CHECK(SceneMigration::Steps().size() == 4);
    // The current-version entry point is not affected by steps above the current version.
    {
        const auto r = SceneMigration::MigrateToCurrent(v3);
        CHECK(r.ok && r.complete && !r.changed && r.text == v3);
    }
    // Two steps in a row, with their log lines.
    {
        const auto r = SceneMigration::MigrateTo(v3, 5);
        CHECK(r.ok && r.complete && r.fromVersion == 3 && r.toVersion == 5 && r.changed);
        CHECK(r.text.compare(0, 12, "okayscene 5\n") == 0);
        CHECK(Has(r.text, "  newthing 1 2\n") && !Has(r.text, "oldthing"));
        CHECK(Has(r.text, "gameobject 0 \"A\"") && Has(r.text, "  tag \"migrated\"\n") && Has(r.text, "  tag \"keep\"\n"));
        CHECK(r.log.size() == 4);
        CHECK(Has(r.log[0], "renamed 1 oldthing") && Has(r.log[1], "okayscene 3 -> 4") && Has(r.log[1], "applied"));
        CHECK(Has(r.log[2], "tagged 1 object") && Has(r.log[3], "okayscene 4 -> 5"));
        // A version 5 file is refused by the current entry point, untouched.
        const auto cur = SceneMigration::MigrateToCurrent(r.text);
        CHECK(!cur.ok && cur.text == r.text && Has(cur.error, "newer"));
    }
    // Only the first step.
    {
        const auto r = SceneMigration::MigrateTo(v3, 4);
        CHECK(r.ok && r.complete && r.toVersion == 4 && Has(r.text, "newthing") && !Has(r.text, "migrated"));
    }
    // A step with nothing to do just raises the number.
    {
        const std::string none = "okayscene 3\ngameobject 0 \"A\"\n  active 1\n  parent -1\nend\n";
        const auto r = SceneMigration::MigrateTo(none, 4);
        CHECK(r.ok && r.complete && Has(r.log[0], "nothing in this scene needs it") && r.text.compare(0, 12, "okayscene 4\n") == 0);
        CHECK(!Has(r.text, "newthing"));
    }
    // A file from a build before the target migrates through the real steps and the synthetic ones.
    {
        const auto r = SceneMigration::MigrateTo(kV1, 5);
        CHECK(r.ok && r.complete && r.fromVersion == 1 && r.toVersion == 5 && r.log.size() == 5);
    }
    // The engine-needing step still stops the chain on the way.
    {
        Scene s("Chars");
        s.CreateGameObject("P")->AddComponent<Character>()->Apply();
        const std::string v1 = WithVersion(SceneSerializer::Serialize(s), 1);
        const auto r = SceneMigration::MigrateTo(v1, 5);
        CHECK(r.ok && r.needsEngine && !r.complete && r.toVersion == 1 && r.text == v1);
    }
    // A step that refuses leaves the text as it was.
    UnregisterSynthetic();
    {
        SceneMigration::Step f;
        f.from = 3;
        f.name = "SYNTHETIC-TEST-failing";
        f.apply = [](SceneText::Document&, std::vector<std::string>& log, std::string& err) { log.push_back("half done"); err = "cannot do it"; return false; };
        CHECK(SceneMigration::Register(f));
        const auto r = SceneMigration::MigrateTo(v3, 4);
        CHECK(!r.ok && r.text == v3 && !r.changed && r.toVersion == 3 && r.log.empty());
        CHECK(Has(r.error, "SYNTHETIC-TEST-failing") && Has(r.error, "cannot do it"));
        UnregisterSynthetic();
    }
    // A gap: no step from 4.
    {
        SceneMigration::Step a;
        a.from = 3;
        a.name = "SYNTHETIC-TEST-only-first";
        CHECK(SceneMigration::Register(a));
        const auto r = SceneMigration::MigrateTo(v3, 5);
        CHECK(!r.ok && r.text == v3 && Has(r.error, "okayscene 4 -> 5") && Has(r.error, "no migration"));
        UnregisterSynthetic();
    }
    CHECK(SceneMigration::Steps().size() == 2);
    CHECK(!SceneMigration::Unregister(3));
    // Registered out of order, kept in order.
    {
        SceneMigration::Step a, b;
        a.from = 4; a.name = "SYNTHETIC-TEST-b";
        b.from = 3; b.name = "SYNTHETIC-TEST-a";
        CHECK(SceneMigration::Register(a) && SceneMigration::Register(b));
        const auto s = SceneMigration::Steps();
        CHECK(s.size() == 4 && s[2].from == 3 && s[3].from == 4);
        UnregisterSynthetic();
    }
    CHECK(SceneMigration::Steps().size() == 2);
}

static void TestFuzz() {
    RUN_SUITE("migration: garbage and truncated input");
    TkRng r(21);
    const std::string x = WithVersion(LiveSceneText(r, 10), 1);
    int ok = 0, refused = 0;
    for (std::size_t cut = 0; cut <= x.size(); cut += 2) {
        const std::string t = x.substr(0, cut);
        const auto m = SceneMigration::MigrateToCurrent(t);
        if (m.ok) { ++ok; SceneText::Document d; CHECK(SceneText::Parse(m.text, d)); CHECK(m.toVersion == 3); }
        else { ++refused; CHECK(m.text == t && !m.error.empty()); }
        const auto e = SceneMigration::MigrateThroughEngine(t);
        if (!e.ok) CHECK(e.text == t);
    }
    for (int i = 0; i < 300; ++i) {
        std::string t = x;
        for (int k = 0, n = 1 + r.range(5); k < n; ++k) {
            const std::size_t at = (std::size_t)r.range((int)t.size());
            switch (r.range(3)) { case 0: t[at] = (char)r.range(256); break; case 1: t.erase(at, (std::size_t)(1 + r.range(10))); break; default: t.insert(at, "\"\nend\n"); }
            if (t.empty()) t = "x";
        }
        const auto m = SceneMigration::MigrateToCurrent(t);
        if (!m.ok) CHECK(m.text == t);
        else { SceneText::Document d; CHECK(SceneText::Parse(m.text, d)); }
    }
    CHECK(ok > 0 && refused > 0);
}

int main() {
    TestRegistry();
    TestOldFormats();
    TestEngineSteps();
    TestRefusals();
    TestSyntheticSteps();
    TestFuzz();
    TEST_MAIN_RESULT();
}
