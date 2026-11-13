// SceneMerge: three-way merge of scene text. Hand-written cases for each kind of
// change and conflict, then seeded property tests over random scenes and random edits.
#include "scene_text_testkit.hpp"
#include "okay/Scene/SceneMerge.hpp"

#include <functional>
#include <map>

using namespace okay;
using namespace tk;

using Policy = SceneMerge::Policy;
using Kind = SceneMerge::Conflict::Kind;

static const char* kLight = "light 1 1 1 1 0.3 1 0 10 45 0.5 0 6500 1 1 1 0";
static std::string LightWith(int idx, const std::string& value) {
    auto toks = SceneText::Tokenize(kLight);
    toks[(std::size_t)idx + 1].text = value;
    std::string t;
    for (std::size_t i = 0; i < toks.size(); ++i) { if (i) t += ' '; t += toks[i].text; }
    return t;
}

static Doc Base() {
    Doc d;
    d.globals = {"name \"Level\"", "gravity 0 -9.81"};
    d.add("a1", "Level", -1, {"transform 0 0 0 0 0 0 1 1 1 1"});
    d.add("a2", "Player", 0, {"transform 1 2 0 0 0 0 1 1 1 1", kLight, "tag \"player\""});
    d.add("a3", "Arena", 0, {"transform 5 5 0 0 0 0 1 1 1 1"});
    d.add("a4", "Enemy", 2, {"transform 0 0 0 0 0 0 1 1 1 1", "boxcollider2d 1 1 0 0 0 0 0"});
    return d;
}

static bool Loads(const std::string& text) { std::string re; return Reserialize(text, &re); }

static int Count(const SceneMerge::Result& r, Kind k) {
    int n = 0;
    for (const auto& c : r.conflicts) if (c.kind == k) ++n;
    return n;
}

static std::vector<std::string> NamesIn(const std::string& text) {
    SceneText::Model m;
    SceneText::BuildModel(text, m);
    std::vector<std::string> n;
    for (const auto& o : m.objects) n.push_back(o.name);
    return n;
}

static std::string RecordOf(const std::string& text, const std::string& guid, const std::string& key) {
    SceneText::Model m;
    SceneText::BuildModel(text, m);
    const int i = FindByGuid(m, guid);
    if (i < 0) return "<no object>";
    for (const auto& r : m.objects[(std::size_t)i].records) if (r.key == key) return r.text;
    return "<no record>";
}

static void TestAutoMerge() {
    RUN_SUITE("merge: changes that do not overlap");
    const Doc b = Base();
    // Different objects.
    {
        Doc o = b, t = b;
        o.objs[1].recs[1] = LightWith(5, "2");                        // Player's light intensity
        t.objs[3].recs[0] = "transform 9 0 0 0 0 0 1 1 1 1";          // Enemy moved
        auto m = SceneMerge::Merge(b.text(), o.text(), t.text());
        CHECK(m.ok && m.Clean() && !m.fellBack);
        CHECK(RecordOf(m.text, "a2", "light") == LightWith(5, "2"));
        CHECK(RecordOf(m.text, "a4", "transform") == "transform 9 0 0 0 0 0 1 1 1 1");
        CHECK(Loads(m.text) && SceneText::Validate(m.text).ok);
        // The other way round: the same scene.
        CHECK(SceneMerge::Merge(b.text(), t.text(), o.text()).text == m.text);
    }
    // Same record, different fields.
    {
        Doc o = b, t = b;
        o.objs[1].recs[1] = LightWith(5, "2");                        // intensity
        t.objs[1].recs[1] = LightWith(7, "20");                       // range
        auto m = SceneMerge::Merge(b.text(), o.text(), t.text());
        CHECK(m.Clean());
        auto toks = SceneText::Tokenize(RecordOf(m.text, "a2", "light"));
        CHECK(toks[6].text == "2" && toks[8].text == "20");
    }
    // Both sides made the same change.
    {
        Doc o = b, t = b;
        o.objs[1].recs[2] = "tag \"hero\"";
        t.objs[1].recs[2] = "tag \"hero\"";
        o.globals[1] = t.globals[1] = "gravity 0 -20";
        auto m = SceneMerge::Merge(b.text(), o.text(), t.text(), [] { SceneMerge::Options x; x.skipShortcuts = true; return x; }());
        CHECK(m.Clean() && RecordOf(m.text, "a2", "tag") == "tag \"hero\"");
        CHECK(Has(m.text, "gravity 0 -20"));
    }
    // Added on both sides, each its own object; a record each on different objects; settings.
    {
        Doc o = b, t = b;
        o.add("c1", "OursCoin", 2, {"transform 1 1 0 0 0 0 1 1 1 1"});
        t.add("d1", "TheirsGem", 0, {"transform 2 2 0 0 0 0 1 1 1 1"});
        o.objs[0].recs.push_back("spinner 0 0 1");
        t.objs[2].recs.push_back("lifetime 5");
        o.globals[1] = "gravity 0 -3";
        t.globals.push_back("fog 0 0.5 0.5 0.5 10 80");
        auto m = SceneMerge::Merge(b.text(), o.text(), t.text());
        CHECK(m.Clean());
        const auto names = NamesIn(m.text);
        CHECK(names.size() == 6);
        CHECK(std::count(names.begin(), names.end(), "OursCoin") == 1 && std::count(names.begin(), names.end(), "TheirsGem") == 1);
        CHECK(Has(m.text, "spinner 0 0 1") && Has(m.text, "lifetime 5") && Has(m.text, "gravity 0 -3") && Has(m.text, "fog 0 0.5"));
        CHECK(Loads(m.text));
        CHECK(SceneMerge::Merge(b.text(), t.text(), o.text()).text == m.text);   // symmetric
    }
    // One side deletes an object the other did not touch; the other renames a different one.
    {
        Doc o = b, t = b;
        o.objs.erase(o.objs.begin() + 3);                              // Enemy deleted
        t.objs[1].name = "Hero";
        auto m = SceneMerge::Merge(b.text(), o.text(), t.text());
        CHECK(m.Clean());
        const auto names = NamesIn(m.text);
        CHECK(names == (std::vector<std::string>{"Level", "Hero", "Arena"}));
    }
    // Reparent on one side, edit on the other.
    {
        Doc o = b, t = b;
        o.objs[3].parent = 1;                                          // Enemy under Player
        t.objs[3].recs[0] = "transform 4 4 0 0 0 0 1 1 1 1";
        auto m = SceneMerge::Merge(b.text(), o.text(), t.text());
        CHECK(m.Clean());
        SceneText::Model mm;
        SceneText::BuildModel(m.text, mm);
        const int e = FindByGuid(mm, "a4");
        CHECK(e >= 0 && mm.objects[(std::size_t)mm.objects[(std::size_t)e].parent].name == "Player");
        CHECK(RecordOf(m.text, "a4", "transform") == "transform 4 4 0 0 0 0 1 1 1 1");
    }
    // Both record lists change: one side adds a collider, the other a script (kept in writer-like order).
    {
        Doc o = b, t = b;
        o.objs[2].recs.push_back("boxcollider2d 2 2 0 0 0 0 0");
        t.objs[2].recs.push_back("script \"okayscript\" \"x\" \"\" 0");
        auto m = SceneMerge::Merge(b.text(), o.text(), t.text());
        CHECK(m.Clean() && Has(m.text, "boxcollider2d 2 2") && Has(m.text, "script \"okayscript\" \"x\""));
    }
    // Several instances: ours edits the first script, theirs the second.
    {
        Doc x;
        x.add("a1", "S", -1, {"script \"okayscript\" \"one\" \"\" 0", "script \"okayscript\" \"two\" \"\" 0"});
        Doc o = x, t = x;
        o.objs[0].recs[0] = "script \"okayscript\" \"ONE\" \"\" 0";
        t.objs[0].recs[1] = "script \"okayscript\" \"TWO\" \"\" 0";
        auto m = SceneMerge::Merge(x.text(), o.text(), t.text());
        CHECK(m.Clean() && Has(m.text, "\"ONE\"") && Has(m.text, "\"TWO\"") && !Has(m.text, "\"one\"") && !Has(m.text, "\"two\""));
    }
    // Key=value record: different keys merge.
    {
        Doc x;
        x.add("a1", "L", -1, {"linerenderer width=1 loop=0 tex=\"a.png\""});
        Doc o = x, t = x;
        o.objs[0].recs[0] = "linerenderer width=2 loop=0 tex=\"a.png\"";
        t.objs[0].recs[0] = "linerenderer width=1 loop=1 tex=\"a.png\" align=1";
        auto m = SceneMerge::Merge(x.text(), o.text(), t.text());
        CHECK(m.Clean() && Has(m.text, "linerenderer width=2 loop=1 tex=\"a.png\" align=1"));
    }
    // Prefab instances: overrides and disabled-component records merge by what they name.
    {
        Doc x;
        x.add("a1", "Instance", -1, {"prefab \"enemy.okayprefab\"", "override 0 \"transform 1 2 3 0 0 0 1 1 1 1\"",
                                    "override 1 \"tag \\\"boss\\\"\"", "compoff \"Light\" 0"});
        Doc o = x, t = x;
        o.objs[0].recs[1] = "override 0 \"transform 9 2 3 0 0 0 1 1 1 1\"";              // edit one override
        t.objs[0].recs.push_back("override 2 \"layer 3\"");                               // add another
        t.objs[0].recs.push_back("compoff \"SpriteRenderer\" 0");                         // and a switched off component
        o.objs[0].recs.erase(o.objs[0].recs.begin() + 3);                                  // ours switches the light back on
        auto m = SceneMerge::Merge(x.text(), o.text(), t.text());
        CHECK(m.Clean());
        CHECK(Has(m.text, "override 0 \"transform 9 2 3") && Has(m.text, "override 1 \"tag") && Has(m.text, "override 2 \"layer 3\""));
        CHECK(Has(m.text, "compoff \"SpriteRenderer\" 0") && !Has(m.text, "compoff \"Light\" 0"));
        CHECK(Loads(m.text) && SceneMerge::Merge(x.text(), t.text(), o.text()).text == m.text);
    }
    // File-level settings: both change gravity differently.
    {
        Doc o = b, t = b;
        o.globals[1] = "gravity 0 -3";
        t.globals[1] = "gravity 0 -20";
        auto m = SceneMerge::Merge(b.text(), o.text(), t.text());
        CHECK(m.conflicts.size() == 1 && m.conflicts[0].record == "gravity" && m.conflicts[0].field == "y" && m.conflicts[0].object.empty());
        CHECK(Has(m.text, "gravity 0 -3\n") && Loads(m.text));
        SceneMerge::Options pt;
        pt.policy = Policy::PreferTheirs;
        CHECK(Has(SceneMerge::Merge(b.text(), o.text(), t.text(), pt).text, "gravity 0 -20\n"));
    }
    // Hundreds of records of one kind: edits on the two sides far apart merge.
    {
        Doc x;
        std::vector<std::string> pts;
        for (int i = 0; i < 700; ++i) pts.push_back("splinept " + std::to_string(i) + " " + std::to_string(i * 2) + " 0");
        x.add("a1", "Road", -1, pts);
        Doc o = x, t = x;
        o.objs[0].recs[10] = "splinept 10 999 0";
        o.objs[0].recs.erase(o.objs[0].recs.begin() + 500);
        t.objs[0].recs[400] = "splinept 400 888 0";
        t.objs[0].recs.insert(t.objs[0].recs.begin() + 650, "splinept 7000 7000 0");
        auto m = SceneMerge::Merge(x.text(), o.text(), t.text());
        CHECK(m.Clean() && Has(m.text, "splinept 10 999 0") && Has(m.text, "splinept 400 888 0") && Has(m.text, "splinept 7000 7000 0"));
        CHECK(!Has(m.text, "splinept 500 1000 0"));
        SceneText::Model mm;
        SceneText::BuildModel(m.text, mm);
        CHECK(mm.objects[0].records.size() == 701);
    }
    // Files without ids: renaming an object on both sides is one conflict, not two objects.
    {
        Doc x;
        x.add("", "Foo", -1, {"transform 1 0 0 0 0 0 1 1 1 1"});
        x.add("", "Other", -1, {"transform 2 0 0 0 0 0 1 1 1 1", "tag \"o\""});
        Doc o = x, t = x;
        o.objs[0].name = "Bar";
        t.objs[0].name = "Baz";
        auto m = SceneMerge::Merge(x.text(), o.text(), t.text());
        CHECK(m.ok && Count(m, Kind::RenamedDifferently) == 1 && NamesIn(m.text) == (std::vector<std::string>{"Bar", "Other"}));
        Doc t2 = x;
        t2.objs[1].recs[0] = "transform 9 0 0 0 0 0 1 1 1 1";
        auto m2 = SceneMerge::Merge(x.text(), o.text(), t2.text());
        CHECK(m2.Clean() && NamesIn(m2.text) == (std::vector<std::string>{"Bar", "Other"}) && Has(m2.text, "transform 9 0 0"));
    }
    // Unknown records ride along untouched.
    {
        Doc x = Base();
        x.objs[1].recs.push_back("futurecomp 1.50 \"keep  me\" 7");
        Doc o = x, t = x;
        o.objs[1].recs[0] = "transform 8 2 0 0 0 0 1 1 1 1";
        t.objs[2].recs[0] = "transform 6 5 0 0 0 0 1 1 1 1";
        auto m = SceneMerge::Merge(x.text(), o.text(), t.text());
        CHECK(m.Clean() && Has(m.text, "  futurecomp 1.50 \"keep  me\" 7\n"));
    }
    // Layout noise on any input changes nothing.
    {
        Doc o = b, t = b;
        o.objs[1].recs[1] = LightWith(5, "2");
        t.objs[3].recs[0] = "transform 9 0 0 0 0 0 1 1 1 1";
        TkRng r(11);
        const auto clean = SceneMerge::Merge(b.text(), o.text(), t.text());
        const auto messy = SceneMerge::Merge(Mess(b.text(), r), Mess(o.text(), r), Mess(t.text(), r));
        CHECK(messy.Clean() && messy.text == clean.text);
    }
}

static void TestConflicts() {
    RUN_SUITE("merge: conflicts and policies");
    const Doc b = Base();
    // Same field, two values.
    {
        Doc o = b, t = b;
        o.objs[1].recs[1] = LightWith(5, "2");
        t.objs[1].recs[1] = LightWith(5, "3");
        auto mk = SceneMerge::Merge(b.text(), o.text(), t.text());
        CHECK(mk.ok && !mk.Clean() && mk.conflicts.size() == 1 && Count(mk, Kind::FieldChanged) == 1);
        const auto& c = mk.conflicts[0];
        CHECK(c.record == "Light" && c.field == "intensity" && c.base == "1" && c.ours == "2" && c.theirs == "3" && c.resolution == "ours");
        CHECK(c.object == "g:a2" && c.name == "Player" && c.path == "Level/Player");
        CHECK(RecordOf(mk.text, "a2", "light") == LightWith(5, "2"));
        CHECK(Loads(mk.text));
        SceneMerge::Options po, pt;
        po.policy = Policy::PreferOurs;
        pt.policy = Policy::PreferTheirs;
        CHECK(RecordOf(SceneMerge::Merge(b.text(), o.text(), t.text(), po).text, "a2", "light") == LightWith(5, "2"));
        const auto rt = SceneMerge::Merge(b.text(), o.text(), t.text(), pt);
        CHECK(RecordOf(rt.text, "a2", "light") == LightWith(5, "3") && rt.conflicts[0].resolution == "theirs");
        // The report.
        const std::string text = mk.ReportText(), json = mk.ReportJson();
        CHECK(Has(text, "field-changed at Level/Player, Light.intensity: base 1, ours 2, theirs 3 -> ours"));
        CHECK(ValidJson(json) && Has(json, "\"kind\":\"field-changed\"") && Has(json, "\"policy\":\"mark\""));
        CHECK(SceneMerge::Merge(b.text(), o.text(), b.text()).ReportText() == "merged cleanly\n");
    }
    // A field both changed plus a field only one changed: only the first is a conflict.
    {
        Doc o = b, t = b;
        o.objs[1].recs[1] = LightWith(7, "20");
        t.objs[1].recs[1] = LightWith(7, "30");
        auto toks = SceneText::Tokenize(t.objs[1].recs[1]);
        toks[2].text = "0.5";    // color.g, theirs only
        std::string tt;
        for (std::size_t i = 0; i < toks.size(); ++i) { if (i) tt += ' '; tt += toks[i].text; }
        t.objs[1].recs[1] = tt;
        auto m = SceneMerge::Merge(b.text(), o.text(), t.text());
        CHECK(m.conflicts.size() == 1 && m.conflicts[0].field == "range");
        const auto rt = SceneText::Tokenize(RecordOf(m.text, "a2", "light"));
        CHECK(rt[2].text == "0.5" && rt[8].text == "20");
    }
    // A one-value record and a record merged as a whole (different token counts).
    {
        Doc o = b, t = b;
        o.objs[1].recs[2] = "tag \"ours\"";
        t.objs[1].recs[2] = "tag \"theirs\"";
        auto m = SceneMerge::Merge(b.text(), o.text(), t.text());
        CHECK(m.conflicts.size() == 1 && m.conflicts[0].record == "Tag" && m.conflicts[0].field.empty());
        Doc x;
        x.add("a1", "A", -1, {"mystery 1 2 3"});
        Doc xo = x, xt = x;
        xo.objs[0].recs[0] = "mystery 1 2 3 4";
        xt.objs[0].recs[0] = "mystery 1 9 3";
        auto m2 = SceneMerge::Merge(x.text(), xo.text(), xt.text());
        CHECK(m2.conflicts.size() == 1 && m2.conflicts[0].kind == Kind::RecordChanged && Has(m2.text, "mystery 1 2 3 4"));
    }
    // Deleted on one side, edited on the other (both directions, all policies).
    {
        Doc o = b, t = b;
        o.objs.erase(o.objs.begin() + 3);                              // ours deletes Enemy
        t.objs[3].recs[0] = "transform 9 0 0 0 0 0 1 1 1 1";            // theirs edits it
        auto mk = SceneMerge::Merge(b.text(), o.text(), t.text());
        CHECK(mk.conflicts.size() == 1 && mk.conflicts[0].kind == Kind::ObjectDeletedAndEdited && mk.conflicts[0].ours == "(deleted)");
        CHECK(FindByGuid([&] { SceneText::Model m; SceneText::BuildModel(mk.text, m); return m; }(), "a4") < 0);
        CHECK(Has(mk.conflicts[0].theirs, "transform 9 0 0 0 0 0 1 1 1 1"));      // the edit is in the report
        SceneMerge::Options pt;
        pt.policy = Policy::PreferTheirs;
        auto rt = SceneMerge::Merge(b.text(), o.text(), t.text(), pt);
        CHECK(RecordOf(rt.text, "a4", "transform") == "transform 9 0 0 0 0 0 1 1 1 1" && Loads(rt.text));
        // Reverse roles.
        auto mr = SceneMerge::Merge(b.text(), t.text(), o.text());
        CHECK(mr.conflicts.size() == 1 && RecordOf(mr.text, "a4", "transform") == "transform 9 0 0 0 0 0 1 1 1 1");
        SceneMerge::Options po;
        po.policy = Policy::PreferTheirs;
        CHECK(RecordOf(SceneMerge::Merge(b.text(), t.text(), o.text(), po).text, "a4", "transform") == "<no object>");
    }
    // A record deleted on one side and edited on the other.
    {
        Doc o = b, t = b;
        o.objs[1].recs.erase(o.objs[1].recs.begin() + 1);              // light removed
        t.objs[1].recs[1] = LightWith(5, "4");
        auto m = SceneMerge::Merge(b.text(), o.text(), t.text());
        CHECK(m.conflicts.size() == 1 && m.conflicts[0].kind == Kind::RecordDeletedAndEdited);
        CHECK(RecordOf(m.text, "a2", "light") == "<no record>");
        // Deleted on one side, untouched on the other: just gone.
        Doc u = b;
        CHECK(SceneMerge::Merge(b.text(), o.text(), u.text()).Clean());
    }
    // Both added a record of one kind, differently; both added scripts: both kept.
    {
        Doc o = b, t = b;
        o.objs[2].recs.push_back("rigidbody2d 0 1 2 0 0 0.4 0.05 1 1 0");
        t.objs[2].recs.push_back("rigidbody2d 0 1 9 0 0 0.4 0.05 1 1 0");
        o.objs[3].recs.push_back("script \"okayscript\" \"a\" \"\" 0");
        t.objs[3].recs.push_back("script \"okayscript\" \"b\" \"\" 0");
        auto m = SceneMerge::Merge(b.text(), o.text(), t.text());
        CHECK(m.conflicts.size() == 1 && m.conflicts[0].kind == Kind::RecordAddedDifferently);
        CHECK(Has(m.text, "rigidbody2d 0 1 2") && !Has(m.text, "rigidbody2d 0 1 9"));
        CHECK(Has(m.text, "\"a\" \"\" 0") && Has(m.text, "\"b\" \"\" 0"));
        CHECK(Loads(m.text));
    }
    // Both rename the same object; both reparent it.
    {
        Doc o = b, t = b;
        o.objs[1].name = "Hero";
        t.objs[1].name = "Knight";
        o.objs[3].parent = 0;
        t.objs[3].parent = 1;
        auto m = SceneMerge::Merge(b.text(), o.text(), t.text());
        CHECK(Count(m, Kind::RenamedDifferently) == 1 && Count(m, Kind::ParentChanged) == 1);
        CHECK(Count(m, Kind::RenamedDifferently) == 1);
        const auto* pc = [&]() -> const SceneMerge::Conflict* { for (const auto& c : m.conflicts) if (c.kind == Kind::ParentChanged) return &c; return nullptr; }();
        CHECK(pc && pc->base == "Level/Arena" && pc->ours == "Level" && pc->theirs == "Level/Knight");
        CHECK(Has(m.text, "\"Hero\"") && !Has(m.text, "\"Knight\""));
        SceneMerge::Options pt;
        pt.policy = Policy::PreferTheirs;
        auto m2 = SceneMerge::Merge(b.text(), o.text(), t.text(), pt);
        CHECK(Has(m2.text, "\"Knight\"") && !Has(m2.text, "\"Hero\"") && Loads(m2.text));
    }
    // Two reparentings that together make a loop.
    {
        Doc x;
        x.add("a1", "X", -1, {"tag \"x\""});
        x.add("a2", "Y", -1, {"tag \"y\""});
        Doc o = x, t = x;
        o.objs[0].parent = 1;                                          // X under Y
        t.objs[1].parent = 0;                                          // Y under X
        auto m = SceneMerge::Merge(x.text(), o.text(), t.text());
        CHECK(m.ok && !m.fellBack && Count(m, Kind::ParentCycle) == 1);
        CHECK(SceneText::Validate(m.text).ok && Loads(m.text));
        SceneText::Model mm;
        SceneText::BuildModel(m.text, mm);
        CHECK(mm.roots.size() == 1);
        // Ours kept its move; theirs lost theirs.
        CHECK(mm.objects[(std::size_t)FindByGuid(mm, "a1")].parent == FindByGuid(mm, "a2"));
        SceneMerge::Options pt;
        pt.policy = Policy::PreferTheirs;
        auto m2 = SceneMerge::Merge(x.text(), o.text(), t.text(), pt);
        SceneText::Model m2m;
        SceneText::BuildModel(m2.text, m2m);
        CHECK(m2m.objects[(std::size_t)FindByGuid(m2m, "a2")].parent == FindByGuid(m2m, "a1") && m2m.roots.size() == 1);
    }
    // One side deletes a parent, the other adds a child under it.
    {
        Doc o = b, t = b;
        o.objs.erase(o.objs.begin() + 3);                              // Enemy
        o.objs.erase(o.objs.begin() + 2);                              // Arena
        t.add("d1", "Loot", 2, {"transform 1 1 0 0 0 0 1 1 1 1"});     // new child of Arena
        auto m = SceneMerge::Merge(b.text(), o.text(), t.text());
        CHECK(m.ok && Count(m, Kind::ParentDeleted) == 1 && m.conflicts[0].resolution == "dropped");
        CHECK(Has(m.conflicts[0].theirs, "transform 1 1 0 0 0 0 1 1 1 1"));     // theirs' object is in the report
        CHECK(NamesIn(m.text) == (std::vector<std::string>{"Level", "Player"}));
        SceneMerge::Options pt;
        pt.policy = Policy::PreferTheirs;
        auto m2 = SceneMerge::Merge(b.text(), o.text(), t.text(), pt);
        CHECK(Count(m2, Kind::ParentDeleted) == 1 && Has(m2.conflicts[0].resolution, "kept under Level"));
        CHECK(NamesIn(m2.text) == (std::vector<std::string>{"Level", "Player", "Loot"}) && Loads(m2.text));
        SceneText::Model mm;
        SceneText::BuildModel(m2.text, mm);
        CHECK(mm.objects[2].parent == 0);
        // And the other way: ours adds a child under what theirs deleted.
        auto m3 = SceneMerge::Merge(b.text(), t.text(), o.text());
        CHECK(m3.ok && Count(m3, Kind::ParentDeleted) == 1 && Has(m3.conflicts[0].resolution, "kept under Level"));
        CHECK(NamesIn(m3.text) == (std::vector<std::string>{"Level", "Player", "Loot"}));
        auto m4 = SceneMerge::Merge(b.text(), t.text(), o.text(), pt);
        CHECK(NamesIn(m4.text) == (std::vector<std::string>{"Level", "Player"}));
    }
    // Both created the same object differently (no base).
    {
        Doc o, t;
        o.add("e1", "Door", -1, {"transform 1 0 0 0 0 0 1 1 1 1"});
        t.add("e1", "Door", -1, {"transform 2 0 0 0 0 0 1 1 1 1"});
        auto m = SceneMerge::Merge("", o.text(), t.text());
        CHECK(m.ok && m.conflicts.size() == 1 && m.conflicts[0].kind == Kind::RecordAddedDifferently && Has(m.text, "transform 1 0 0"));
        CHECK(SceneMerge::Merge("", o.text(), o.text()).Clean());
        Doc t2 = o;
        t2.add("e2", "Wall", -1, {"transform 5 0 0 0 0 0 1 1 1 1"});
        auto m2 = SceneMerge::Merge("  \n", o.text(), t2.text());
        CHECK(m2.Clean() && NamesIn(m2.text).size() == 2);
    }
}

static void TestHeaderTailGuid() {
    RUN_SUITE("merge: a guid in the object header tail is the object's identity");
    // The header form the loader also accepts: `gameobject N "Name" guid "g"`.
    // BuildModel must lift it into the model guid (out of headerTail) so diff and
    // merge key on it, and the validator must still check it.
    const std::string headerForm =
        "okayscene 3\n"
        "name \"Level\"\n"
        "gameobject 0 \"Root\" guid \"b1\"\n  active 1\n  parent -1\n  transform 0 0 0 0 0 0 1 1 1 1\nend\n"
        "gameobject 1 \"Child\" guid \"b2\"\n  active 1\n  parent 0\n  transform 1 0 0 0 0 0 1 1 1 1\nend\n";
    {
        SceneText::Model m;
        std::string err;
        CHECK(SceneText::BuildModel(headerForm, m, &err));
        CHECK(m.objects[0].guid == "b1" && m.objects[1].guid == "b2");
        CHECK(m.objects[0].headerTail.empty() && m.objects[1].headerTail.empty());
        CHECK(SceneText::Validate(headerForm).ok);
        CHECK(Loads(headerForm));
    }
    // Same scene written as records: identical model, so the two forms interoperate
    // in merge (an edit to the record form pairs with the header-tail base).
    const std::string recordForm =
        "okayscene 3\n"
        "name \"Level\"\n"
        "gameobject 0 \"Root\"\n  guid \"b1\"\n  active 1\n  parent -1\n  transform 0 0 0 0 0 0 1 1 1 1\nend\n"
        "gameobject 1 \"Child\"\n  guid \"b2\"\n  active 1\n  parent 0\n  transform 1 0 0 0 0 0 1 1 1 1\nend\n";
    {
        SceneText::Model h, r;
        CHECK(SceneText::BuildModel(headerForm, h) && SceneText::BuildModel(recordForm, r));
        CHECK(h.objects[0].guid == r.objects[0].guid && h.objects[1].guid == r.objects[1].guid);
    }
    // Three-way merge: ours renamed Root in the record form, theirs moved Child;
    // the header-tail guids keep the pairing by identity.
    {
        SceneText::Model b;
        CHECK(SceneText::BuildModel(headerForm, b));
        b.objects[0].name = "Hero";
        const std::string ours = SceneText::Emit(b);
        SceneText::Model t;
        CHECK(SceneText::BuildModel(headerForm, t));
        t.objects[1].records.push_back({"tag", "tag \"mob\""});
        const std::string theirs = SceneText::Emit(t);
        const auto m = SceneMerge::Merge(headerForm, ours, theirs);
        CHECK(m.ok && m.Clean() && !m.fellBack);
        CHECK(Has(m.text, "Hero") && RecordOf(m.text, "b2", "tag") == "tag \"mob\"");
        CHECK(SceneText::Validate(m.text).ok && Loads(m.text));
    }
    // Malformed and duplicated header-tail guids are reported, like the record form.
    {
        const std::string badGuid =
            "okayscene 3\n"
            "gameobject 0 \"A\" guid \"ZZ\"\n  active 1\n  parent -1\nend\n";
        CHECK(!SceneText::Validate(badGuid).ok);
        const std::string dupGuid =
            "okayscene 3\n"
            "gameobject 0 \"A\" guid \"aa\"\n  active 1\n  parent -1\nend\n"
            "gameobject 1 \"B\"\n  guid \"aa\"\n  active 1\n  parent -1\nend\n";
        CHECK(!SceneText::Validate(dupGuid).ok);
    }
}

static void TestOrder() {
    RUN_SUITE("merge: object and sibling order");
    // Both add after the same sibling: stable, symmetric.
    {
        Doc b;
        b.add("a1", "P", -1);
        b.add("a2", "k1", 0);
        b.add("a3", "k2", 0);
        Doc o = b, t = b;
        o.add("c1", "ours", 0);
        t.add("d1", "theirs", 0);
        auto m1 = SceneMerge::Merge(b.text(), o.text(), t.text());
        auto m2 = SceneMerge::Merge(b.text(), t.text(), o.text());
        CHECK(m1.Clean() && m1.text == m2.text);
        CHECK(NamesIn(m1.text).size() == 5);
    }
    // One side reorders children, the other adds one: both kept.
    {
        Doc b;
        b.add("a1", "P", -1);
        b.add("a2", "k1", 0);
        b.add("a3", "k2", 0);
        b.add("a4", "k3", 0);
        Doc o = b, t = b;
        o.objs[1].sibling = 2; o.objs[2].sibling = 0; o.objs[3].sibling = 1;     // k2, k3, k1
        t.add("d1", "new", 0);
        auto m = SceneMerge::Merge(b.text(), o.text(), t.text());
        CHECK(m.Clean() && Loads(m.text));
        SceneText::Model mm;
        SceneText::BuildModel(m.text, mm);
        std::vector<std::string> kids;
        for (int c : mm.children[0]) kids.push_back(mm.objects[(std::size_t)c].name);
        CHECK((kids == std::vector<std::string>{"k2", "k3", "k1", "new"}));
        CHECK(SceneMerge::Merge(b.text(), t.text(), o.text()).text == m.text);
    }
    // Both reorder the same child differently: a conflict, ours wins.
    {
        Doc b;
        b.add("a1", "P", -1);
        b.add("a2", "k1", 0);
        b.add("a3", "k2", 0);
        b.add("a4", "k3", 0);
        Doc o = b, t = b;
        o.objs[1].sibling = 1; o.objs[2].sibling = 0; o.objs[3].sibling = 2;     // k2, k1, k3
        t.objs[1].sibling = 2; t.objs[2].sibling = 0; t.objs[3].sibling = 1;     // k2, k3, k1
        auto m = SceneMerge::Merge(b.text(), o.text(), t.text());
        CHECK(m.ok && Loads(m.text));
        SceneText::Model mm;
        SceneText::BuildModel(m.text, mm);
        std::vector<std::string> kids;
        for (int c : mm.children[0]) kids.push_back(mm.objects[(std::size_t)c].name);
        CHECK(kids.size() == 3 && kids[0] == "k2");
    }
}

static void TestInputs() {
    RUN_SUITE("merge: bad inputs never produce bad text");
    const std::string good = Base().text();
    const char* bad = "okayscene 3\ngameobject 0 \"A\"\n  active 1\n";   // cut short
    auto a = SceneMerge::Merge(good, bad, good);
    CHECK(!a.ok && a.text == bad && Has(a.error, "ours") && Has(a.ReportText(), "merge failed") && ValidJson(a.ReportJson()));
    auto b = SceneMerge::Merge(good, good, "nonsense");
    CHECK(!b.ok && b.text == good && Has(b.error, "theirs"));
    auto c = SceneMerge::Merge("nonsense", [&] { Doc d = Base(); d.objs[1].name = "X"; return d.text(); }(), good);
    CHECK(!c.ok && Has(c.error, "base"));
    // A newer format is not merged (and not damaged).
    Doc fut = Base();
    fut.version = 9;
    auto d = SceneMerge::Merge(good, good, fut.text());
    CHECK(!d.ok && Has(d.error, "newer") && d.text == good);
    // Two different versions: refused with a clear message.
    Doc old = Base();
    old.version = 2;
    old.objs[1].name = "Edited";
    auto e = SceneMerge::Merge(good, old.text(), good);
    CHECK(!e.ok && Has(e.error, "one version"));
    // Both sides moved to the same newer version: fine.
    Doc o2 = Base(), t2 = Base(), b2 = Base();
    b2.version = 2;
    o2.objs[1].name = "Hero";
    auto f = SceneMerge::Merge(b2.text(), o2.text(), t2.text());
    CHECK(f.ok && f.Clean() && Has(f.text, "okayscene 3\n") && Has(f.text, "Hero"));
    // A side the loader would warn about is written back repaired, not passed through.
    {
        const std::string irregular =
            "okayscene 3\n"
            "gameobject 0 \"A\"\n  guid \"aa\"\n  active 1\n  parent 7\nend\n"            // a parent that does not exist
            "gameobject 1 \"B\"\n  guid \"aa\"\n  active 1\n  parent -1\nend\n"            // a repeated id
            "gameobject 2 \"C\"\n  guid \"ZZ\"\n  active 1\n  parent 0\nend\n";             // a malformed id
        CHECK(!SceneText::Validate(irregular).ok);
        for (int full = 0; full < 2; ++full) {
            SceneMerge::Options o;
            o.skipShortcuts = full == 1;
            const auto m = SceneMerge::Merge(good, irregular, irregular, o);
            CHECK(m.ok && m.Clean() && SceneText::Validate(m.text).ok && Loads(m.text));
            CHECK(NamesIn(m.text) == (std::vector<std::string>{"A", "B", "C"}));
            const auto m2 = SceneMerge::Merge(irregular, irregular, [&] { Doc dd = Base(); return dd.text(); }());
            CHECK(m2.ok && SceneText::Validate(m2.text).ok);
        }
    }
    // The validator can veto a merge: it falls back to ours.
    Doc o = Base(), t = Base();
    o.objs[1].name = "Hero";
    t.objs[3].recs[0] = "transform 9 0 0 0 0 0 1 1 1 1";
    SceneMerge::Options veto;
    veto.validator = [](const std::string&, std::string* err) { *err = "refused for the test"; return false; };
    auto g = SceneMerge::Merge(good, o.text(), t.text(), veto);
    CHECK(g.ok && g.fellBack && !g.Clean() && g.text == SceneText::Canonicalize(o.text()));
    CHECK(Count(g, Kind::Invalid) == 1 && Has(g.conflicts.back().resolution, "refused for the test"));
    // The real loader as the validator.
    SceneMerge::Options real;
    real.validator = [](const std::string& text, std::string* err) { return Reserialize(text, nullptr, err); };
    auto h = SceneMerge::Merge(good, o.text(), t.text(), real);
    CHECK(h.Clean());
}

// ---- property tests -------------------------------------------------------------

using Edit = std::function<void(SceneText::Model&)>;

static bool Mine(const std::string& guid, int part) {
    std::uint64_t h = 1469598103934665603ull;
    for (char c : guid) { h ^= (unsigned char)c; h *= 1099511628211ull; }
    return (int)((h >> 17) & 1u) == part;
}
static bool OwnChain(const SceneText::Model& m, int i, int part) {
    for (int p = i; p >= 0; p = m.objects[(std::size_t)p].parent)
        if (!Mine(m.objects[(std::size_t)p].guid, part)) return false;
    return true;
}

// One random edit that only touches objects of partition `part` (and parents chains made of
// them), so two sides using different parts never conflict. Applies it to `m` and records a
// replayable version addressed by guid.
static bool PartEdit(SceneText::Model& m, TkRng& r, int part, std::vector<Edit>& log) {
    using M = SceneText::Model;
    const int n = (int)m.objects.size();
    std::vector<int> mine;
    for (int i = 0; i < n; ++i) if (Mine(m.objects[(std::size_t)i].guid, part)) mine.push_back(i);
    if (mine.empty()) return false;
    const int pick = mine[(std::size_t)r.range((int)mine.size())];
    const std::string g = m.objects[(std::size_t)pick].guid;
    Edit e;
    switch (r.range(11)) {
        case 0: case 1: case 2: {   // a number
            static const struct { const char* key; int first, last; } f[] = {{"transform", 0, 9}, {"light", 0, 5}, {"boxcollider2d", 0, 3}, {"rigidbody2d", 1, 5}};
            std::vector<std::pair<std::string, int>> cands;
            for (const auto& rec : m.objects[(std::size_t)pick].records)
                for (const auto& fl : f) if (rec.key == fl.key) cands.push_back({fl.key, r.range(fl.last - fl.first + 1) + fl.first});
            if (cands.empty()) return false;
            const auto c = cands[(std::size_t)r.range((int)cands.size())];
            const std::string v = RandFloatText(r, 0.25f, 40.0f);
            e = [g, c, v](M& mm) {
                const int i = FindByGuid(mm, g);
                if (i < 0) return;
                for (auto& rec : mm.objects[(std::size_t)i].records) {
                    if (rec.key != c.first) continue;
                    auto toks = SceneText::Tokenize(rec.text);
                    if ((std::size_t)c.second + 1 >= toks.size()) return;
                    toks[(std::size_t)c.second + 1].text = v;
                    std::string t;
                    for (std::size_t k = 0; k < toks.size(); ++k) { if (k) t += ' '; t += toks[k].text; }
                    rec.text = t;
                    return;
                }
            };
            break;
        }
        case 3: {   // rename
            const std::string name = "Renamed" + std::to_string(r.range(1000));
            e = [g, name](M& mm) { const int i = FindByGuid(mm, g); if (i >= 0) mm.objects[(std::size_t)i].name = name; };
            break;
        }
        case 4: {   // tag
            const std::string t = "tag \"t" + std::to_string(r.range(50)) + "\"";
            const bool remove = r.chance(0.3);
            e = [g, t, remove](M& mm) {
                const int i = FindByGuid(mm, g);
                if (i < 0) return;
                auto& recs = mm.objects[(std::size_t)i].records;
                for (std::size_t k = 0; k < recs.size(); ++k) if (recs[k].key == "tag") {
                    if (remove) recs.erase(recs.begin() + (std::ptrdiff_t)k); else recs[k].text = t;
                    return;
                }
                if (!remove) recs.insert(recs.begin() + (recs.empty() ? 0 : 1), {"tag", t});
            };
            break;
        }
        case 5: {   // collider on / off
            const std::string t = "boxcollider2d " + RandFloatText(r, 0.5f, 5.0f) + " 1 0 0 0 0 0";
            e = [g, t](M& mm) {
                const int i = FindByGuid(mm, g);
                if (i < 0) return;
                auto& recs = mm.objects[(std::size_t)i].records;
                for (std::size_t k = 0; k < recs.size(); ++k) if (recs[k].key == "boxcollider2d") { recs.erase(recs.begin() + (std::ptrdiff_t)k); return; }
                recs.push_back({"boxcollider2d", t});
            };
            break;
        }
        case 6: {   // new object under one of my chains (or the root)
            std::string parent;
            if (r.chance(0.7)) {
                const int cand = mine[(std::size_t)r.range((int)mine.size())];
                if (OwnChain(m, cand, part)) parent = m.objects[(std::size_t)cand].guid;
            }
            std::string ng;
            do { ng = NewGuid(r); } while (!Mine(ng, part));
            const std::string name = "New" + std::to_string(r.range(1000));
            const std::string px = RandFloatText(r), py = RandFloatText(r);
            e = [parent, ng, name, px, py](M& mm) {
                SceneText::ModelObject o;
                o.name = name;
                o.guid = ng;
                o.parent = parent.empty() ? -1 : FindByGuid(mm, parent);
                o.records.push_back({"active", "active 1"});
                o.records.push_back({"transform", "transform " + px + " " + py + " 0 0 0 0 1 1 1 1"});
                mm.objects.push_back(o);
                FixChildren(mm);
            };
            break;
        }
        case 7: {   // delete a subtree that is entirely mine
            const auto sub = Subtree(m, pick);
            for (int x : sub) if (!Mine(m.objects[(std::size_t)x].guid, part)) return false;
            std::vector<std::string> gs;
            for (int x : sub) gs.push_back(m.objects[(std::size_t)x].guid);
            e = [gs](M& mm) {
                std::vector<char> drop(mm.objects.size(), 0);
                for (const auto& gg : gs) { const int i = FindByGuid(mm, gg); if (i >= 0) drop[(std::size_t)i] = 1; }
                DropObjects(mm, drop);
            };
            break;
        }
        case 8: {   // reparent my object under my chain or the root
            std::string np;
            if (r.chance(0.8)) {
                const int cand = mine[(std::size_t)r.range((int)mine.size())];
                if (!OwnChain(m, cand, part) || InSubtree(m, cand, pick)) return false;
                np = m.objects[(std::size_t)cand].guid;
            }
            if ((m.objects[(std::size_t)pick].parent >= 0 ? m.objects[(std::size_t)m.objects[(std::size_t)pick].parent].guid : std::string()) == np) return false;
            e = [g, np](M& mm) {
                const int i = FindByGuid(mm, g);
                if (i < 0) return;
                mm.objects[(std::size_t)i].parent = np.empty() ? -1 : FindByGuid(mm, np);
                FixChildren(mm);
            };
            break;
        }
        case 9: {   // reorder the children of a parent of mine
            if (m.children[(std::size_t)pick].size() < 2) return false;
            const auto& kids = m.children[(std::size_t)pick];
            const std::string child = m.objects[(std::size_t)kids[(std::size_t)r.range((int)kids.size())]].guid;
            const int to = r.range((int)kids.size());
            e = [g, child, to](M& mm) {
                const int p = FindByGuid(mm, g), c = FindByGuid(mm, child);
                if (p < 0 || c < 0) return;
                auto& list = mm.children[(std::size_t)p];
                auto it = std::find(list.begin(), list.end(), c);
                if (it == list.end()) return;
                list.erase(it);
                list.insert(list.begin() + std::min<int>(to, (int)list.size()), c);
            };
            break;
        }
        default: {   // a scene setting, one per partition
            const std::string v = std::to_string(1 + r.range(30));
            e = [part, v](M& mm) {
                for (auto& rec : mm.globals) {
                    if (part == 0 && rec.key == "gravity") rec.text = "gravity 0 -" + v;
                    if (part == 1 && rec.key == "fog") rec.text = "fog 0 0.5 0.5 0.5 " + v + " 90";
                }
            };
        }
    }
    e(m);
    log.push_back(e);
    return true;
}

// A scene as a map (guid -> name, parent guid, records) plus the settings, order ignored.
static std::map<std::string, std::string> Sig(const std::string& text) {
    SceneText::Model m;
    std::string err;
    std::map<std::string, std::string> out;
    if (!SceneText::BuildModel(text, m, &err)) { out["<error>"] = err; return out; }
    for (std::size_t i = 0; i < m.objects.size(); ++i) {
        const auto& o = m.objects[i];
        std::string s = o.name + "|" + (o.parent >= 0 ? m.objects[(std::size_t)o.parent].guid : std::string("-"));
        for (const auto& r : o.records) s += "\n" + r.text;
        std::vector<std::string> kids;
        for (int c : m.children[i]) kids.push_back(m.objects[(std::size_t)c].guid);
        std::sort(kids.begin(), kids.end());
        for (const auto& k : kids) s += "\n#" + k;
        out[o.guid.empty() ? "?" + o.path : o.guid] = s;
    }
    std::string gl;
    for (const auto& r : m.globals) gl += r.text + "\n";
    out["<globals>"] = gl;
    return out;
}

static SceneMerge::Options Full() {
    SceneMerge::Options o;
    o.skipShortcuts = true;
    return o;
}

static void TestIdentities() {
    RUN_SUITE("merge: merge(base,x,x) == x, merge(base,base,x) == x, merge(base,x,base) == x");
    int cases = 0;
    for (std::uint64_t seed = 1; seed <= (std::uint64_t)Seeds(150); ++seed) {
        TkRng r(seed * 13 + 1);
        const std::string base = LiveSceneText(r, 2 + r.range(70), r.chance(0.9));
        SceneText::Model m;
        SceneText::BuildModel(base, m);
        for (int i = 0, n = r.range(25); i < n; ++i) RandomEdit(m, r);
        if (r.chance(0.2) && m.objects.size() >= 2) SwapObjects(m, 0, (int)m.objects.size() - 1);
        const std::string x = SceneText::Emit(m);
        const std::string cx = SceneText::Canonicalize(x);
        for (int full = 0; full < 2; ++full) {
            SceneMerge::Options o;
            o.skipShortcuts = full == 1;
            const auto a = SceneMerge::Merge(base, x, x, o);
            const auto b = SceneMerge::Merge(base, base, x, o);
            const auto c = SceneMerge::Merge(base, x, base, o);
            CHECK(a.Clean() && a.text == cx);
            CHECK(b.Clean() && b.text == cx);
            CHECK(c.Clean() && c.text == cx);
            if (a.text != cx || b.text != cx || c.text != cx)
                std::cerr << "  seed " << seed << (full ? " (full)" : "") << ": a " << (a.text == cx) << " b " << (b.text == cx) << " c " << (c.text == cx)
                          << " conflicts " << a.conflicts.size() << "/" << b.conflicts.size() << "/" << c.conflicts.size() << "\n";
        }
        ++cases;
    }
    CHECK(cases == Seeds(150));
}

static void TestPartitioned() {
    RUN_SUITE("merge: non-overlapping random edits merge cleanly, symmetrically, completely");
    int cases = 0, withDelete = 0, withReorder = 0;
    for (std::uint64_t seed = 1; seed <= (std::uint64_t)Seeds(200); ++seed) {
        TkRng r(seed * 101 + 5);
        const std::string base = LiveSceneText(r, 4 + r.range(60), true);
        SceneText::Model bm, a, b;
        SceneText::BuildModel(base, bm);
        a = bm;
        b = bm;
        std::vector<Edit> logA, logB;
        for (int i = 0, n = 1 + r.range(15); i < n; ++i) PartEdit(a, r, 0, logA);
        for (int i = 0, n = 1 + r.range(15); i < n; ++i) PartEdit(b, r, 1, logB);
        SceneText::Model expected = a;
        for (auto& e : logB) e(expected);
        const std::string ta = SceneText::Emit(a), tb = SceneText::Emit(b);

        const auto m1 = SceneMerge::Merge(base, ta, tb, Full());
        const auto m2 = SceneMerge::Merge(base, tb, ta, Full());
        CHECK(m1.ok && !m1.fellBack);
        if (!m1.conflicts.empty()) {
            std::cerr << "  seed " << seed << " unexpected conflicts:\n" << m1.ReportText();
            CHECK(false);
        }
        CHECK(m2.conflicts.empty());
        CHECK(m1.text == m2.text);                              // symmetry
        CHECK(Sig(m1.text) == Sig(SceneText::Emit(expected)));  // everything from both sides is there
        if (Sig(m1.text) != Sig(SceneText::Emit(expected))) std::cerr << "  seed " << seed << " content differs from the replayed edits\n";
        CHECK(SceneText::Validate(m1.text).ok);
        std::string re;
        CHECK(Reserialize(m1.text, &re));
        // With the shortcuts on, the same answer.
        CHECK(SceneMerge::Merge(base, ta, tb).text == m1.text);
        // Layout noise changes nothing.
        CHECK(SceneMerge::Merge(Mess(base, r), Mess(ta, r), Mess(tb, r), Full()).text == m1.text);
        ++cases;
        for (auto& e : logA) { (void)e; }
        withDelete += (a.objects.size() < bm.objects.size() || b.objects.size() < bm.objects.size()) ? 1 : 0;
        withReorder += (SceneText::Emit(a).find("sibling") != std::string::npos) ? 1 : 0;
    }
    CHECK(cases == Seeds(200));
    std::cout << "  " << cases << " cases, " << withDelete << " with deletions, " << withReorder << " with reordered siblings\n";
    CHECK(withDelete > cases / 20 && withReorder > cases / 50);
}

static void TestOverlapping() {
    RUN_SUITE("merge: arbitrary overlapping edits always load");
    int conflicted = 0, cases = 0;
    SceneMerge::Options real;
    real.validator = [](const std::string& text, std::string* err) { return Reserialize(text, nullptr, err); };
    for (std::uint64_t seed = 1; seed <= (std::uint64_t)Seeds(250); ++seed) {
        TkRng r(seed * 7919 + 3);
        const std::string base = LiveSceneText(r, 3 + r.range(50), r.chance(0.9));
        SceneText::Model bm, a, b;
        SceneText::BuildModel(base, bm);
        a = bm;
        b = bm;
        for (int i = 0, n = r.range(20); i < n; ++i) RandomEdit(a, r);
        for (int i = 0, n = r.range(20); i < n; ++i) RandomEdit(b, r);
        const std::string ta = SceneText::Emit(a), tb = SceneText::Emit(b);
        for (Policy pol : {Policy::Mark, Policy::PreferOurs, Policy::PreferTheirs}) {
            SceneMerge::Options o = real;
            o.policy = pol;
            const auto m = SceneMerge::Merge(base, ta, tb, o);
            CHECK(m.ok);
            CHECK(!m.fellBack);
            if (m.fellBack) std::cerr << "  seed " << seed << " fell back:\n" << m.ReportText() << "\n";
            std::string re, err;
            CHECK(Reserialize(m.text, &re, &err));
            CHECK(SceneText::Validate(m.text).ok);
            CHECK(SceneText::Canonicalize(m.text) == m.text);
            // No object is lost or invented: every object of the result is in one of the inputs.
            const auto sig = Sig(m.text), sa = Sig(ta), sb = Sig(tb);
            for (const auto& kv : sig)
                if (kv.first[0] != '<' && kv.first[0] != '?') CHECK(sa.count(kv.first) || sb.count(kv.first));
            // Deterministic.
            CHECK(SceneMerge::Merge(base, ta, tb, o).text == m.text);
            // Ours-wins and theirs-wins agree when there is nothing to decide.
            if (m.conflicts.empty()) {
                SceneMerge::Options o2 = o;
                o2.policy = Policy::PreferTheirs;
                CHECK(SceneMerge::Merge(base, ta, tb, o2).text == m.text);
            }
            if (pol == Policy::Mark) { ++cases; conflicted += m.conflicts.empty() ? 0 : 1; CHECK(ValidJson(m.ReportJson())); }
        }
    }
    std::cout << "  " << cases << " merges, " << conflicted << " with conflicts\n";
    CHECK(cases == Seeds(250) && conflicted > cases / 8 && conflicted < cases);
}

static void TestScale() {
    RUN_SUITE("merge: 5000 objects");
    TkRng r(555);
    const std::string base = LiveSceneText(r, 5000);
    SceneText::Model bm, a, b;
    SceneText::BuildModel(base, bm);
    a = bm;
    b = bm;
    std::vector<Edit> la, lb;
    for (int i = 0; i < 200; ++i) { PartEdit(a, r, 0, la); PartEdit(b, r, 1, lb); }
    const std::string ta = SceneText::Emit(a), tb = SceneText::Emit(b);
    const auto t0 = std::chrono::steady_clock::now();
    const auto m = SceneMerge::Merge(base, ta, tb);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    std::cout << "  5000 objects, " << la.size() << "+" << lb.size() << " edits: merge in " << ms << " ms\n";
    CHECK(m.ok && m.Clean() && ms < 5000.0);
    SceneText::Model expected = a;
    for (auto& e : lb) e(expected);
    CHECK(Sig(m.text) == Sig(SceneText::Emit(expected)));
    CHECK(SceneText::Validate(m.text).ok);
    // The full algorithm on identical sides is quick too.
    const auto t1 = std::chrono::steady_clock::now();
    const auto same = SceneMerge::Merge(base, ta, ta, Full());
    const double ms2 = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t1).count();
    std::cout << "  identical sides, full algorithm: " << ms2 << " ms\n";
    CHECK(same.Clean() && same.text == SceneText::Canonicalize(ta) && ms2 < 5000.0);
}

static void TestFuzz() {
    RUN_SUITE("merge: garbage and truncated input");
    TkRng r(31337);
    const std::string base = LiveSceneText(r, 10);
    SceneText::Model m;
    SceneText::BuildModel(base, m);
    RandomEdit(m, r); RandomEdit(m, r);
    const std::string ours = SceneText::Emit(m);
    RandomEdit(m, r); RandomEdit(m, r);
    const std::string theirs = SceneText::Emit(m);
    int okCount = 0, failCount = 0;
    auto check = [&](const std::string& b, const std::string& o, const std::string& t) {
        for (Policy pol : {Policy::Mark, Policy::PreferTheirs}) {
            SceneMerge::Options opt;
            opt.policy = pol;
            const auto res = SceneMerge::Merge(b, o, t, opt);
            if (res.ok) {
                ++okCount;
                CHECK(SceneText::Validate(res.text).ok);
                // The inputs are only checked as text, so only inputs the real loader accepts promise a loadable result.
                if (Reserialize(o, nullptr) && Reserialize(t, nullptr)) CHECK(Reserialize(res.text, nullptr));
            } else {
                ++failCount;
                CHECK(res.text == o && !res.error.empty());
            }
            CHECK(ValidJson(res.ReportJson()));
            (void)res.ReportText();
        }
    };
    for (std::size_t cut = 0; cut <= theirs.size(); cut += 5) {
        check(base, ours, theirs.substr(0, cut));
        check(base, ours.substr(0, cut), theirs);
        check(base.substr(0, cut), ours, theirs);
    }
    for (int i = 0; i < 200; ++i) {
        std::string t = theirs, o = ours, b = base;
        std::string* which[3] = {&t, &o, &b};
        std::string& s = *which[r.range(3)];
        for (int k = 0, n = 1 + r.range(5); k < n; ++k) {
            const std::size_t at = (std::size_t)r.range((int)s.size());
            switch (r.range(3)) { case 0: s[at] = (char)r.range(256); break; case 1: s.erase(at, (std::size_t)(1 + r.range(12))); break; default: s.insert(at, "\n  parent 99\nend\n\""); }
            if (s.empty()) s = "x";
        }
        check(b, o, t);
    }
    for (int i = 0; i < 100; ++i) {
        std::string t;
        for (int k = 0, n = r.range(120); k < n; ++k) t += (char)r.range(256);
        check(t, ours, theirs);
        check(base, t, theirs);
        check(base, ours, t);
    }
    std::cout << "  " << okCount << " merged, " << failCount << " refused\n";
    CHECK(okCount > 0 && failCount > 0);
}

int main() {
    TestAutoMerge();
    TestConflicts();
    TestOrder();
    TestHeaderTailGuid();
    TestInputs();
    TestIdentities();
    TestPartitioned();
    TestOverlapping();
    TestScale();
    TestFuzz();
    TEST_MAIN_RESULT();
}
