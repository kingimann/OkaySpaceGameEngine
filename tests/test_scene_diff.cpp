// SceneDiff: structured, deterministic diffs of scene text, and applying a diff.
#include "scene_text_testkit.hpp"
#include "okay/Scene/SceneDiff.hpp"

using namespace okay;
using namespace tk;

static const char* kLight = "light 1 1 1 1 0.3 1 0 10 45 0.5 0 6500 1 1 1 0";

static Doc Base() {
    Doc d;
    d.globals = {"name \"Level\"", "gravity 0 -9.81"};
    d.add("a1", "Level", -1, {"transform 0 0 0 0 0 0 1 1 1 1"});
    d.add("a2", "Player", 0, {"transform 1 2 0 0 0 0 1 1 1 1", kLight, "tag \"player\""});
    d.add("a3", "Arena", 0, {"transform 5 5 0 0 0 0 1 1 1 1"});
    d.add("a4", "Enemy", 2, {"transform 0 0 0 0 0 0 1 1 1 1", "boxcollider2d 1 1 0 0 0 0 0"});
    return d;
}

static void TestBasics() {
    RUN_SUITE("diff: what changed");
    const Doc b = Base();
    SceneDiff::Result same = SceneDiff::Compute(b.text(), b.text());
    CHECK(same.ok && same.Empty() && same.objects.empty());
    CHECK(same.ToText() == "scene diff: no differences\n");
    CHECK(ValidJson(same.ToJson()));

    // Layout noise alone is not a difference.
    TkRng r(5);
    CHECK(SceneDiff::Compute(b.text(), Mess(b.text(), r)).Empty());

    // A field of a record: Light.intensity 1 -> 2, Transform.px.
    {
        Doc o = b;
        o.objs[1].recs[1] = "light 1 1 1 1 0.3 2 0 10 45 0.5 0 6500 1 1 1 0";
        o.objs[1].recs[0] = "transform 7 2 0 0 0 0 1 1 1 1";
        auto d = SceneDiff::Compute(b.text(), o.text());
        CHECK(d.ok && d.modified == 1 && d.added == 0 && d.removed == 0);
        const std::string t = d.ToText();
        CHECK(Has(t, "Light.intensity 1 -> 2"));
        CHECK(Has(t, "Transform.px 1 -> 7"));
        CHECK(Has(t, "~ Level/Player [a2]"));
        CHECK(d.objects.size() == 1 && d.objects[0].records.size() == 2);
        CHECK(d.objects[0].records[1].fields.size() == 1 && d.objects[0].records[1].fields[0].name == "intensity");
        const std::string j = d.ToJson();
        CHECK(ValidJson(j) && Has(j, "\"field\":\"intensity\",\"before\":\"1\",\"after\":\"2\""));
        CHECK(Has(j, "\"modified\":1"));
    }
    // One-value records: Tag, and a scene setting.
    {
        Doc o = b;
        o.objs[1].recs[2] = "tag \"hero\"";
        o.globals[1] = "gravity 0 -20";
        auto d = SceneDiff::Compute(b.text(), o.text());
        const std::string t = d.ToText();
        CHECK(Has(t, "Tag \"player\" -> \"hero\""));
        CHECK(Has(t, "scene settings:") && Has(t, "gravity.y -9.81 -> -20"));
        CHECK(d.globals.size() == 1);
    }
    // Added and removed objects.
    {
        Doc o = b;
        o.add("a5", "Coin", 2, {"transform 3 3 0 0 0 0 1 1 1 1"});
        o.objs.erase(o.objs.begin() + 3);   // Enemy goes (it was last but one, no children)
        for (auto& ob : o.objs) if (ob.guid == "a5") ob.parent = 2;
        auto d = SceneDiff::Compute(b.text(), o.text());
        CHECK(d.added == 1 && d.removed == 1);
        const std::string t = d.ToText();
        CHECK(Has(t, "+ Level/Arena/Coin [a5]") && Has(t, "- Level/Arena/Enemy [a4]"));
        CHECK(Has(t, "1 added, 1 removed, 0 modified"));
        CHECK(ValidJson(d.ToJson()));
    }
    // Rename (same id), reparent, reorder.
    {
        Doc o = b;
        o.objs[1].name = "Hero";
        o.objs[3].parent = 1;                 // Enemy moves under Player
        auto d = SceneDiff::Compute(b.text(), o.text());
        CHECK(d.renamed == 1 && d.reparented == 1);
        const std::string t = d.ToText();
        CHECK(Has(t, "renamed \"Player\" -> \"Hero\""));
        CHECK(Has(t, "reparented Level/Arena -> Level/Hero"));
        CHECK(Has(t, "(1 renamed, 1 reparented)"));
        CHECK(d.objects.size() == 2);

        // Swap the two children of Level with sibling hints.
        Doc w = b;
        w.objs[1].sibling = 1;
        w.objs[2].sibling = 0;
        auto e = SceneDiff::Compute(b.text(), w.text());
        CHECK(e.ok && e.reordered >= 1 && !e.Empty());
        CHECK(Has(e.ToText(), "reordered among siblings"));
        bool found = false;
        for (const auto& c : e.objects) if (c.reordered) found = found || (c.oldIndex != c.newIndex);
        CHECK(found);
    }
    // Added and removed records, and a record that changed as a whole (script text).
    {
        Doc o = b;
        o.objs[1].recs.erase(o.objs[1].recs.begin() + 1);                  // light gone
        o.objs[1].recs.push_back("spinner 0 0 1");                         // spinner added
        auto d = SceneDiff::Compute(b.text(), o.text());
        const std::string t = d.ToText();
        CHECK(Has(t, "- Light  light 1 1 1 1") && Has(t, "+ Spinner  spinner 0 0 1"));
    }
    // Format version.
    {
        Doc o = b;
        o.version = 2;
        auto d = SceneDiff::Compute(o.text(), b.text());
        CHECK(!d.Empty() && Has(d.ToText(), "format version: 2 -> 3"));
    }
    // A file from a newer build is still compared, with a warning.
    {
        Doc f = b;
        f.version = 4;
        f.objs[1].recs.push_back("brandnew 1.0 \"x\"");
        auto d = SceneDiff::Compute(f.text(), f.text());
        CHECK(d.ok && d.Empty() && d.warnings.size() == 2 && Has(d.warnings[0], "newer"));
        Doc g = f;
        g.objs[1].recs.back() = "brandnew 2.0 \"x\"";
        auto e = SceneDiff::Compute(f.text(), g.text());
        CHECK(e.ok && Has(e.ToText(), "brandnew[0] 1.0 -> 2.0"));       // numbers of an unknown record are not tidied
    }
    // Errors.
    {
        auto d = SceneDiff::Compute("garbage", b.text());
        CHECK(!d.ok && Has(d.error, "base:") && Has(d.ToText(), "failed") && ValidJson(d.ToJson()));
        auto e = SceneDiff::Compute(b.text(), "okayscene 3\ngameobject 0 \"x\"\n");
        CHECK(!e.ok && Has(e.error, "other:"));
    }
}

static void TestRecordsAndFields() {
    RUN_SUITE("diff: records with several instances, key=value fields");
    Doc b;
    b.add("a1", "Boss", -1, {"transform 0 0 0 0 0 0 1 1 1 1", "script \"okayscript\" \"one\" \"\" 0", "script \"okayscript\" \"two\" \"\" 0",
                            "linerenderer width=1.5 loop=0 tex=\"a.png\""});
    Doc o = b;
    o.objs[0].recs[2] = "script \"okayscript\" \"TWO\" \"\" 0";                     // only the second script
    o.objs[0].recs[3] = "linerenderer width=2 loop=0 tex=\"a.png\" align=1";         // kv: width changed, align new
    auto d = SceneDiff::Compute(b.text(), o.text());
    CHECK(d.ok && d.objects.size() == 1);
    const auto& recs = d.objects[0].records;
    CHECK(recs.size() == 2);
    bool script = false, line = false;
    for (const auto& rc : recs) {
        if (rc.label == "Script#1") script = rc.fields.size() == 1 && rc.fields[0].name == "source" && rc.fields[0].after == "\"TWO\"";
        if (rc.label == "linerenderer") {
            line = rc.fields.size() == 2;
            for (const auto& f : rc.fields) if (f.name == "width") line = line && f.before == "1.5" && f.after == "2";
            for (const auto& f : rc.fields) if (f.name == "align") line = line && !f.hadBefore && f.hasAfter && f.after == "1";
        }
    }
    CHECK(script && line);
    const std::string t = d.ToText();
    CHECK(Has(t, "linerenderer.width 1.5 -> 2") && Has(t, "linerenderer.align (none) -> 1") && Has(t, "Script#1.source \"two\" -> \"TWO\""));

    // Removing the first of two scripts is one removal, not two edits.
    Doc o2 = b;
    o2.objs[0].recs.erase(o2.objs[0].recs.begin() + 1);
    auto d2 = SceneDiff::Compute(b.text(), o2.text());
    CHECK(d2.objects.size() == 1 && d2.objects[0].records.size() == 1);
    CHECK(d2.objects[0].records[0].kind == SceneDiff::RecordChange::Kind::Removed);
    CHECK(d2.objects[0].records[0].before.find("\"one\"") != std::string::npos);

    // A long value is shortened in the readable form and complete in JSON.
    Doc o3 = b;
    std::string big(500, 'x');
    o3.objs[0].recs[1] = "script \"okayscript\" \"" + big + "\" \"\" 0";
    auto d3 = SceneDiff::Compute(b.text(), o3.text());
    CHECK(d3.ToText().size() < 400 && Has(d3.ToText(), "..."));
    CHECK(Has(d3.ToJson(), big) && ValidJson(d3.ToJson()));

    // Awkward text in values: quotes, backslashes, newlines, a stray byte.
    Doc o4 = b;
    o4.objs[0].name = "Bo\"ss\\\n\x01 caf\xc3\xa9 \xff";
    auto d4 = SceneDiff::Compute(b.text(), o4.text());
    CHECK(d4.ok && d4.renamed == 1 && ValidJson(d4.ToJson()));
}

static void TestLargeGroups() {
    RUN_SUITE("diff: an object with hundreds of records of one kind");
    Doc b;
    std::vector<std::string> pts;
    for (int i = 0; i < 700; ++i) pts.push_back("splinept " + std::to_string(i) + " " + std::to_string(i * 2) + " 0");
    b.add("a1", "Road", -1, pts);
    Doc o = b;
    o.objs[0].recs[10] = "splinept 10 999 0";                           // edited
    o.objs[0].recs[400] = "splinept 400 888 0";                          // edited
    o.objs[0].recs.erase(o.objs[0].recs.begin() + 300);                  // removed
    o.objs[0].recs.insert(o.objs[0].recs.begin() + 600, "splinept 5000 5000 0");   // added
    auto d = SceneDiff::Compute(b.text(), o.text());
    CHECK(d.ok && d.objects.size() == 1);
    int added = 0, removed = 0, modified = 0;
    for (const auto& c : d.objects[0].records) {
        using K = SceneDiff::RecordChange::Kind;
        added += c.kind == K::Added; removed += c.kind == K::Removed; modified += c.kind == K::Modified;
    }
    CHECK(added == 1 && removed == 1 && modified == 2);
    std::string out;
    CHECK(SceneDiff::Apply(b.text(), d, &out) && out == SceneText::Canonicalize(o.text()));
}

static void TestNoGuids() {
    RUN_SUITE("diff: files without object ids");
    Doc b;
    b.add("", "Root", -1, {"transform 0 0 0 0 0 0 1 1 1 1"});
    b.add("", "Kid", 0, {"transform 1 0 0 0 0 0 1 1 1 1"});
    b.add("", "Kid", 0, {"transform 2 0 0 0 0 0 1 1 1 1"});          // same name twice: #1
    b.add("", "Spare", -1, {"transform 3 0 0 0 0 0 1 1 1 1", "tag \"s\""});
    Doc o = b;
    o.objs[2].recs[0] = "transform 9 0 0 0 0 0 1 1 1 1";                // the second Kid moved
    o.objs[3].name = "Renamed";                                         // content kept: a rename
    auto d = SceneDiff::Compute(b.text(), o.text());
    CHECK(d.ok && d.added == 0 && d.removed == 0 && d.modified == 2);
    CHECK(d.renamed == 1);
    const std::string t = d.ToText();
    CHECK(Has(t, "~ Root/Kid#1") && Has(t, "Transform.px 2 -> 9"));
    CHECK(Has(t, "renamed \"Spare\" -> \"Renamed\""));

    SceneDiff::Options no;
    no.detectRenames = false;
    auto e = SceneDiff::Compute(b.text(), o.text(), no);
    CHECK(e.added == 1 && e.removed == 1 && e.renamed == 0);

    // One side gained ids: still the same objects.
    Doc g = b;
    for (std::size_t i = 0; i < g.objs.size(); ++i) g.objs[i].guid = "b" + std::to_string(i);
    auto f = SceneDiff::Compute(b.text(), g.text());
    CHECK(f.ok && f.added == 0 && f.removed == 0 && f.modified == 4);
    for (const auto& c : f.objects) CHECK(c.guidChanged);
    std::string patched;
    CHECK(SceneDiff::Apply(b.text(), f, &patched) && patched == SceneText::Canonicalize(g.text()));
}

static void TestDeterminism() {
    RUN_SUITE("diff: deterministic");
    TkRng r(77);
    const std::string a = LiveSceneText(r, 80);
    SceneText::Model m;
    SceneText::BuildModel(a, m);
    for (int i = 0; i < 25; ++i) RandomEdit(m, r);
    const std::string b = SceneText::Emit(m);
    const auto d1 = SceneDiff::Compute(a, b), d2 = SceneDiff::Compute(a, b);
    CHECK(d1.ok && d1.ToText() == d2.ToText() && d1.ToJson() == d2.ToJson());
    CHECK(ValidJson(d1.ToJson()));
    CHECK(d1.ToText() == SceneDiff::Compute(Mess(a, r), Mess(b, r)).ToText());   // layout does not matter either
    // Reversing swaps added and removed.
    const auto rev = SceneDiff::Compute(b, a);
    CHECK(rev.added == d1.removed && rev.removed == d1.added);
}

static void TestApplyProperty() {
    RUN_SUITE("diff: applying a diff reproduces the other side");
    int differing = 0, total = 0;
    for (std::uint64_t seed = 1; seed <= (std::uint64_t)Seeds(120); ++seed) {
        TkRng r(seed * 31 + 7);
        const std::string a = LiveSceneText(r, 2 + r.range(60), r.chance(0.85));
        SceneText::Model m;
        std::string err;
        CHECK(SceneText::BuildModel(a, m, &err));
        const int edits = r.range(20);
        for (int i = 0; i < edits; ++i) RandomEdit(m, r);
        if (r.chance(0.2) && m.objects.size() >= 2) SwapObjects(m, 0, (int)m.objects.size() - 1);
        const std::string b = SceneText::Emit(m);
        const SceneDiff::Result d = SceneDiff::Compute(a, b);
        CHECK(d.ok);
        std::string out, e2;
        const bool applied = SceneDiff::Apply(a, d, &out, &e2);
        CHECK(applied);
        if (!applied) { std::cerr << "  seed " << seed << ": " << e2 << "\n"; continue; }
        CHECK(out == SceneText::Canonicalize(b));
        if (out != SceneText::Canonicalize(b)) std::cerr << "  seed " << seed << " mismatch\n";
        // A messy copy of the base applies the same diff.
        std::string out2;
        std::string e3;
        const bool ap2 = SceneDiff::Apply(Mess(a, r), d, &out2, &e3);
        CHECK(ap2 && out2 == out);
        if (!ap2 || out2 != out) std::cerr << "  seed " << seed << " messy base: " << e3 << "\n";
        // The patched file loads, and is the scene `b` loads as.
        std::string rp, rb;
        CHECK(Reserialize(out, &rp) && Reserialize(b, &rb) && StripGuids(rp) == StripGuids(rb));
        CHECK(d.Empty() == (SceneText::Canonicalize(a) == SceneText::Canonicalize(b)));
        if (!d.Empty()) ++differing;
        ++total;
    }
    CHECK(total == Seeds(120) && differing > total / 2);

    // A diff does not apply to the wrong base.
    TkRng r(3);
    const std::string a = LiveSceneText(r, 20), a2 = LiveSceneText(r, 25);
    SceneText::Model m;
    SceneText::BuildModel(a, m);
    RandomEdit(m, r); RandomEdit(m, r); RandomEdit(m, r);
    const auto d = SceneDiff::Compute(a, SceneText::Emit(m));
    std::string out, err;
    CHECK(!SceneDiff::Apply(a2, d, &out, &err) && !err.empty());
    SceneDiff::Result broken;
    CHECK(!SceneDiff::Apply(a, broken, &out, &err));
}

static void TestScale() {
    RUN_SUITE("diff: 5000 objects");
    TkRng r(2024);
    const std::string a = LiveSceneText(r, 5000);
    SceneText::Model m;
    CHECK(SceneText::BuildModel(a, m));
    CHECK(m.objects.size() == 5000);
    int applied = 0;
    for (int i = 0; i < 300; ++i) applied += RandomEdit(m, r) ? 1 : 0;
    const std::string b = SceneText::Emit(m);
    const auto t0 = std::chrono::steady_clock::now();
    const SceneDiff::Result d = SceneDiff::Compute(a, b);
    const std::string text = d.ToText(), json = d.ToJson();
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    std::cout << "  5000 objects, " << applied << " edits: diff + both outputs in " << ms << " ms, "
              << d.objects.size() << " changed objects, " << a.size() / 1024 << " KiB\n";
    CHECK(d.ok && !d.Empty() && ms < 5000.0);
    CHECK(ValidJson(json));
    std::string out;
    const auto t1 = std::chrono::steady_clock::now();
    CHECK(SceneDiff::Apply(a, d, &out) && out == SceneText::Canonicalize(b));
    const double ms2 = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t1).count();
    std::cout << "  apply in " << ms2 << " ms\n";
    CHECK(ms2 < 5000.0);
    // The same scene against itself, and a flat 5000-root scene with its order reversed.
    CHECK(SceneDiff::Compute(a, a).Empty());
    SceneText::Model flat;
    SceneText::BuildModel("okayscene 3\n", flat);
    for (int i = 0; i < 5000; ++i) {
        SceneText::ModelObject o;
        o.name = "Flat" + std::to_string(i);
        o.guid = NewGuid(r);
        o.records.push_back({"active", "active 1"});
        flat.objects.push_back(o);
    }
    SceneText::SetFileChildOrder(flat);
    const std::string f1 = SceneText::Emit(flat);
    std::reverse(flat.objects.begin(), flat.objects.end());
    for (auto& o : flat.objects) o.parent = -1;
    SceneText::SetFileChildOrder(flat);
    const std::string f2 = SceneText::Emit(flat);
    const auto t2 = std::chrono::steady_clock::now();
    const auto fd = SceneDiff::Compute(f1, f2);
    const double ms3 = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t2).count();
    std::cout << "  flat 5000 roots reversed: " << ms3 << " ms, " << fd.objects.size() << " moved\n";
    CHECK(fd.ok && ms3 < 5000.0 && fd.objects.size() >= 4990);
    CHECK(SceneDiff::Apply(f1, fd, &out) && out == f2);
}

static void TestFuzz() {
    RUN_SUITE("diff: garbage and truncated input");
    TkRng r(8);
    const std::string a = LiveSceneText(r, 12);
    SceneText::Model m;
    SceneText::BuildModel(a, m);
    RandomEdit(m, r); RandomEdit(m, r);
    const std::string b = SceneText::Emit(m);
    for (std::size_t cut = 0; cut <= a.size(); cut += 3) {
        const auto d = SceneDiff::Compute(a.substr(0, cut), b);
        CHECK(!d.ok || d.ToText().size() > 0);
        const auto e = SceneDiff::Compute(a, b.substr(0, cut));
        CHECK(!e.ok || e.ToJson().size() > 0);
        (void)d.ToJson();
    }
    for (int i = 0; i < 300; ++i) {
        std::string t = a;
        for (int k = 0, n = 1 + r.range(5); k < n; ++k) {
            const std::size_t at = (std::size_t)r.range((int)t.size());
            switch (r.range(3)) { case 0: t[at] = (char)r.range(256); break; case 1: t.erase(at, (std::size_t)(1 + r.range(15))); break; default: t.insert(at, "\"\n end "); }
            if (t.empty()) t = "x";
        }
        const auto d = SceneDiff::Compute(t, b);
        const auto e = SceneDiff::Compute(a, t);
        CHECK(ValidJson(d.ToJson()) && ValidJson(e.ToJson()));
        std::string out;
        if (d.ok) SceneDiff::Apply(t, d, &out);
    }
    for (int i = 0; i < 200; ++i) {
        std::string t;
        for (int k = 0, n = r.range(150); k < n; ++k) t += (char)r.range(256);
        const auto d = SceneDiff::Compute(t, a);
        CHECK(!d.ok);
        CHECK(!SceneDiff::Compute(a, t).ok);
    }
}

int main() {
    TestBasics();
    TestRecordsAndFields();
    TestLargeGroups();
    TestNoGuids();
    TestDeterminism();
    TestApplyProperty();
    TestScale();
    TestFuzz();
    TEST_MAIN_RESULT();
}
