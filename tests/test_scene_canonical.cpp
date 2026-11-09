// SceneText: the tolerant scene parser and the canonical form (Canonicalize).
#include "scene_text_testkit.hpp"

using namespace okay;
using namespace tk;

static bool Contains(const std::string& hay, const std::string& needle) { return hay.find(needle) != std::string::npos; }

static std::string Canon(const std::string& t) { return SceneText::Canonicalize(t); }

static const char* kSmall =
    "okayscene 3\n"
    "name \"Level\"\n"
    "gravity 0 -9.81\n"
    "gameobject 0 \"A\"\n"
    "  guid \"aaaa0000aaaa0000aaaa0000aaaa0000\"\n"
    "  active 1\n"
    "  parent -1\n"
    "  transform 1 2 3 0 0 0 1 1 1 1\n"
    "end\n"
    "gameobject 1 \"B\"\n"
    "  guid \"bbbb0000bbbb0000bbbb0000bbbb0000\"\n"
    "  active 1\n"
    "  parent 0\n"
    "  transform 0 0 0 0 0 0 1 1 1 1\n"
    "end\n";

static void TestBasics() {
    RUN_SUITE("canonical: whitespace and layout");
    // Already canonical text comes back byte for byte.
    CHECK(Canon(kSmall) == kSmall);

    // CRLF, trailing blanks, tabs, blank lines, spacing, missing final newline.
    const std::string messy =
        "okayscene   3  \r\n\r\n"
        "name   \"Level\"\t\r\n"
        "gravity 0    -9.81\r\n"
        "\r\n"
        "gameobject   0   \"A\"  \r\n"
        "\t\tactive 1\r\n"
        "  guid   \"aaaa0000aaaa0000aaaa0000aaaa0000\"  \r\n"
        "parent -1\r\n"
        "     transform\t1 2   3 0 0 0 1 1 1 1  \r\n"
        "end  \r\n"
        "gameobject 1 \"B\"\r\n"
        "  guid \"bbbb0000bbbb0000bbbb0000bbbb0000\"\r\n"
        "  active 1\r\n  parent 0\r\n  transform 0 0 0 0 0 0 1 1 1 1\r\nend";
    CHECK(Canon(messy) == kSmall);

    // Idempotent.
    CHECK(Canon(Canon(messy)) == Canon(messy));

    // A lone CR is a line break too.
    CHECK(Canon("okayscene 3\rname \"x\"\r") == "okayscene 3\nname \"x\"\n");

    // Header-only and empty-object scenes.
    CHECK(Canon("okayscene 3") == "okayscene 3\n");
    CHECK(Canon("okayscene 3\ngameobject 0 \"E\"\nend\n") == "okayscene 3\ngameobject 0 \"E\"\nend\n");

    SceneText::CanonicalReport rep;
    SceneText::Canonicalize(messy, SceneText::CanonicalOptions(), &rep);
    CHECK(rep.changed && !rep.skipped && rep.objects == 2);
    SceneText::Canonicalize(kSmall, SceneText::CanonicalOptions(), &rep);
    CHECK(!rep.changed && !rep.skipped);
}

static void TestStrings() {
    RUN_SUITE("canonical: quoted strings are never touched");
    // A script whose source has blank lines, trailing blanks, a tab and a CRLF inside the string.
    const std::string script =
        "okayscene 3\r\n"
        "gameobject 0 \"S\"\r\n"
        "  active 1\r\n"
        "  parent -1\r\n"
        "  script \"okayscript\" \"line1  \r\n\r\n\tline3   \nend\nok \\\" quote\" \"\" 0\r\n"
        "end\r\n";
    const std::string c = Canon(script);
    CHECK(Contains(c, "\"line1  \r\n\r\n\tline3   \nend\nok \\\" quote\""));
    // Outside the string the CRLFs are gone.
    CHECK(Contains(c, "okayscene 3\ngameobject 0 \"S\"\n  active 1\n  parent -1\n  script \"okayscript\""));
    CHECK(Contains(c, " \"\" 0\nend\n"));
    CHECK(Canon(c) == c);

    // The word `end` and a fake record inside a string do not close or add anything.
    SceneText::Document d;
    std::string err;
    CHECK(SceneText::Parse(c, d, &err));
    CHECK(d.objects.size() == 1 && d.objects[0].records.size() == 3);

    // A string with the number-looking text is not a number.
    const std::string t = "okayscene 3\ngameobject 0 \"N\"\n  active 1\n  tag \"1.50\"\n  parent -1\nend\n";
    CHECK(Canon(t) == t);
    const std::string n = "okayscene 3\ngameobject 0 \"2.0\"\n  active 1\n  parent -1\nend\n";
    CHECK(Canon(n) == n);   // a name is a name
}

static void TestNumbers() {
    RUN_SUITE("canonical: numbers");
    CHECK(SceneText::NormalizeNumber("2.0") == "2");
    CHECK(SceneText::NormalizeNumber("2.") == "2");
    CHECK(SceneText::NormalizeNumber("0.50") == "0.5");
    CHECK(SceneText::NormalizeNumber("-3.250") == "-3.25");
    CHECK(SceneText::NormalizeNumber("-0.0") == "-0");
    CHECK(SceneText::NormalizeNumber("0.0") == "0");
    CHECK(SceneText::NormalizeNumber("10.100") == "10.1");
    CHECK(SceneText::NormalizeNumber(".50") == ".5");
    CHECK(SceneText::NormalizeNumber(".0") == ".0");      // nothing sensible to write
    CHECK(SceneText::NormalizeNumber("1e5") == "1e5");
    CHECK(SceneText::NormalizeNumber("1.50e5") == "1.50e5");
    CHECK(SceneText::NormalizeNumber("007") == "007");
    CHECK(SceneText::NormalizeNumber("+1.0") == "+1.0");
    CHECK(SceneText::NormalizeNumber("0x10") == "0x10");
    CHECK(SceneText::NormalizeNumber("abc") == "abc");
    CHECK(SceneText::NormalizeNumber("") == "");
    CHECK(SceneText::NormalizeNumber("1.2.0") == "1.2.0");

    // In a known numeric record.
    const std::string in = "okayscene 3\ngravity 0.0 -9.810\ngameobject 0 \"A\"\n  parent -1\n  transform 1.0 2.50 -0.0 0 0 0 1.00 1 1 1\nend\n";
    const std::string want = "okayscene 3\ngravity 0 -9.81\ngameobject 0 \"A\"\n  parent -1\n  transform 1 2.5 -0 0 0 0 1 1 1 1\nend\n";
    CHECK(Canon(in) == want);

    // Off: the numbers stay.
    SceneText::CanonicalOptions o;
    o.normalizeNumbers = false;
    CHECK(SceneText::Canonicalize(in, o) == in);

    // key=value records: numbers tidied, strings not.
    const std::string kv = "okayscene 3\ngameobject 0 \"A\"\n  parent -1\n  linerenderer width=1.50 tex=\"a.0\" loop=1\nend\n";
    CHECK(Contains(Canon(kv), "linerenderer width=1.5 tex=\"a.0\" loop=1"));

    // A bare number in a record this build does not vouch for stays as written.
    const std::string un = "okayscene 3\ngameobject 0 \"A\"\n  parent -1\n  mystery 1.0 2.50\nend\n";
    CHECK(Contains(Canon(un), "  mystery 1.0 2.50\n"));
}

static void TestOrderAndNumbering() {
    RUN_SUITE("canonical: record order and object numbers");
    const std::string in =
        "okayscene 3\n"
        "weather 0 1 1 0 0 0 \"\"\n"
        "gravity 0 -9\n"
        "name \"L\"\n"
        "gameobject 10 \"Root\"\n"
        "  transform 0 0 0 0 0 0 1 1 1 1\n"
        "  parent -1\n"
        "  tag \"t\"\n"
        "  active 1\n"
        "  guid \"aaaa\"\n"
        "end\n"
        "gameobject 4 \"Kid\"\n"
        "  sibling 0\n"
        "  parent 10\n"
        "  active 1\n"
        "end\n"
        "gameobject 77 \"Lost\"\n"
        "  parent 3\n"
        "  active 1\n"
        "end\n";
    const std::string want =
        "okayscene 3\n"
        "name \"L\"\n"
        "gravity 0 -9\n"
        "weather 0 1 1 0 0 0 \"\"\n"
        "gameobject 0 \"Root\"\n"
        "  guid \"aaaa\"\n"
        "  active 1\n"
        "  tag \"t\"\n"
        "  parent -1\n"
        "  transform 0 0 0 0 0 0 1 1 1 1\n"
        "end\n"
        "gameobject 1 \"Kid\"\n"
        "  active 1\n"
        "  parent 0\n"
        "  sibling 0\n"
        "end\n"
        "gameobject 2 \"Lost\"\n"
        "  active 1\n"
        "  parent -1\n"   // there is no object 3: the loader treats it as no parent
        "end\n";
    CHECK(Canon(in) == want);
    CHECK(Canon(want) == want);

    // Off switches.
    SceneText::CanonicalOptions o;
    o.renumberObjects = false;
    CHECK(Contains(SceneText::Canonicalize(in, o), "gameobject 10 \"Root\""));
    o = SceneText::CanonicalOptions();
    o.sortHeaderRecords = false;
    const std::string ns = SceneText::Canonicalize(in, o);
    CHECK(ns.find("weather") < ns.find("gravity"));

    // An object with a record this build does not know keeps its record order.
    const std::string unk =
        "okayscene 3\ngameobject 0 \"U\"\n  transform 0 0 0 0 0 0 1 1 1 1\n  futurecomp 1 \"x\"\n  active 1\n  parent -1\nend\n";
    CHECK(Canon(unk) == unk);
    // ...and an unknown file-level record keeps the file-level order.
    const std::string ug = "okayscene 3\ngravity 0 -9\nfuture_setting 1.0 2\nname \"x\"\n";
    CHECK(Canon(ug) == ug);
}

static void TestUntouched() {
    RUN_SUITE("canonical: input it should not rewrite");
    SceneText::CanonicalReport rep;
    const std::string future = "okayscene 4  \r\nname   \"x\"\r\nbrandnew 1.0   2\r\ngameobject 0 \"F\"\r\n  active 1\r\n  parent -1\r\nend\r\n";
    CHECK(SceneText::Canonicalize(future, SceneText::CanonicalOptions(), &rep) == future);
    CHECK(rep.skipped && !rep.changed && Contains(rep.note, "newer"));

    const char* bad[] = {
        "", "   \n", "hello", "okayscene", "okayscene x\n", "okayscene 0\n", "okayscene -3\n", "okayscene 3 extra\n",
        "okayscene 3\ngameobject 0 \"A\"\n  active 1\n",                                // no end
        "okayscene 3\ngameobject 0 \"A\"\ngameobject 1 \"B\"\nend\n",                 // cut short
        "okayscene 3\nend\n",
        "okayscene 3\ngameobject 0 \"A\"\nend\ngameobject 0 \"B\"\nend\n",           // duplicate index
        "okayscene 3\ngameobject x \"A\"\nend\n",
        "okayscene 3\ngameobject 0\nend\n",
        "okayscene 3\nname \"open\n",                                                    // unterminated string
        "okayscene 3\ngameobject 0 \"A\"\n  tag \"x\n  end\n",
    };
    for (const char* b : bad) {
        rep = SceneText::CanonicalReport();
        CHECK(SceneText::Canonicalize(b, SceneText::CanonicalOptions(), &rep) == b);
        CHECK(rep.skipped && !rep.changed);
        SceneText::Document d;
        std::string err;
        CHECK(!SceneText::Parse(b, d, &err) && !err.empty());
    }
}

static void TestParseModel() {
    RUN_SUITE("scene model");
    SceneText::Model m;
    std::string err;
    CHECK(SceneText::BuildModel(kSmall, m, &err));
    CHECK(m.objects.size() == 2 && m.roots.size() == 1 && m.children[0].size() == 1);
    CHECK(m.objects[0].guid == "aaaa0000aaaa0000aaaa0000aaaa0000" && m.objects[1].parent == 0);
    CHECK(m.objects[1].path == "A/B" && m.objects[0].path == "A");
    // The model carries no guid/parent/sibling records.
    for (const auto& r : m.objects[0].records) CHECK(r.key != "guid" && r.key != "parent");
    CHECK(SceneText::Emit(m) == kSmall);

    // Same names get #n.
    CHECK(SceneText::BuildModel("okayscene 3\ngameobject 0 \"X\"\n  parent -1\nend\ngameobject 1 \"X\"\n  parent -1\nend\n"
                                "gameobject 2 \"X\"\n  parent 1\nend\n", m, &err));
    CHECK(m.objects[0].path == "X" && m.objects[1].path == "X#1" && m.objects[2].path == "X#1/X");

    // Cycles, malformed parent: refused. Dangling and bad guids: tolerated with a warning.
    CHECK(!SceneText::BuildModel("okayscene 3\ngameobject 0 \"A\"\n  parent 1\nend\ngameobject 1 \"B\"\n  parent 0\nend\n", m, &err));
    CHECK(Contains(err, "cycle"));
    CHECK(!SceneText::BuildModel("okayscene 3\ngameobject 0 \"A\"\n  parent 0\nend\n", m, &err));
    CHECK(!SceneText::BuildModel("okayscene 3\ngameobject 0 \"A\"\n  parent x\nend\n", m, &err));
    CHECK(SceneText::BuildModel("okayscene 3\ngameobject 0 \"A\"\n  guid \"XYZ\"\n  parent 9\nend\n", m, &err));
    CHECK(m.warnings.size() == 2 && m.objects[0].guid.empty() && m.objects[0].parent == -1);
    CHECK(SceneText::BuildModel("okayscene 3\ngameobject 0 \"A\"\n  guid \"aa\"\n  parent -1\nend\ngameobject 1 \"B\"\n  guid \"aa\"\n  parent -1\nend\n", m, &err));
    CHECK(m.objects[0].guid == "aa" && m.objects[1].guid.empty() && m.warnings.size() == 1);

    // Validate.
    CHECK(SceneText::Validate(kSmall).ok);
    CHECK(!SceneText::Validate("okayscene 3\ngameobject 0 \"A\"\n  parent 4\nend\n").ok);
    CHECK(!SceneText::Validate("nonsense").ok);

    // Sibling order: the loader's rules.
    const std::string sib =
        "okayscene 3\n"
        "gameobject 0 \"P\"\n  parent -1\nend\n"
        "gameobject 1 \"a\"\n  parent 0\n  sibling 2\nend\n"
        "gameobject 2 \"b\"\n  parent 0\n  sibling 0\nend\n"
        "gameobject 3 \"c\"\n  parent 0\n  sibling 1\nend\n";
    CHECK(SceneText::BuildModel(sib, m, &err));
    CHECK((m.children[0] == std::vector<int>{2, 3, 1}));
    CHECK(SceneText::Emit(m) == sib);
    // An unhinted child fills the remaining slot.
    const std::string sib2 =
        "okayscene 3\n"
        "gameobject 0 \"P\"\n  parent -1\nend\n"
        "gameobject 1 \"a\"\n  parent 0\n  sibling 1\nend\n"
        "gameobject 2 \"b\"\n  parent 0\nend\n";
    CHECK(SceneText::BuildModel(sib2, m, &err));
    CHECK((m.children[0] == std::vector<int>{2, 1}));
}

// The proof of "never changes what the scene means": the canonical text loads as the
// scene the original loads as, whatever the layout noise.
static void TestMeaningPreserved() {
    RUN_SUITE("canonical: same scene, same re-serialization");
    int scenes = 0;
    for (std::uint64_t seed = 1; seed <= (std::uint64_t)Seeds(60); ++seed) {
        TkRng r(seed);
        const std::string x = LiveSceneText(r, 1 + r.range(60));
        // The writer's own output is canonical already.
        CHECK(Canon(x) == x);
        std::string base;
        CHECK(Reserialize(x, &base));
        for (int k = 0; k < 3; ++k) {
            const std::string m = Mess(x, r);
            std::string re, err;
            const bool loadsMess = Reserialize(m, &re, &err);
            CHECK(loadsMess);
            if (!loadsMess) { std::cerr << "  seed " << seed << " mess does not load: " << err << "\n"; continue; }
            CHECK(re == base);                              // the mess itself means the same
            const std::string c = Canon(m);
            CHECK(c == x);                                  // and comes back to the writer's text
            CHECK(Canon(c) == c);                           // idempotent
            std::string rc;
            CHECK(Reserialize(c, &rc) && rc == base);       // canonical loads as the same scene
            // Without guids (older files): the scene is the same apart from the fresh ids.
            const std::string ng = StripGuids(m);
            std::string rn, rn2;
            CHECK(Reserialize(ng, &rn) && Reserialize(Canon(ng), &rn2));
            CHECK(StripGuids(rn) == StripGuids(rn2));
        }
        ++scenes;
    }
    CHECK(scenes == Seeds(60));

    // Hand-edited numbers: 1.0 for 1 and 0.50 for 0.5 load the same value.
    TkRng r(99);
    std::string x = LiveSceneText(r, 25);
    std::string hand;
    {
        SceneText::Document d;
        SceneText::Parse(x, d);
        for (auto& o : d.objects)
            for (auto& rec : o.records)
                if (rec.key == "transform") {
                    const auto toks = SceneText::Tokenize(rec.text);
                    std::string t = "transform";
                    for (std::size_t i = 1; i < toks.size(); ++i) {
                        std::string v = toks[i].text;
                        if (v.find('.') == std::string::npos && v.find('e') == std::string::npos) v += ".0";
                        else v += "00";
                        t += " " + v;
                    }
                    rec.text = t;
                }
        hand = SceneText::Write(d);
    }
    CHECK(hand != x);
    std::string a, b;
    CHECK(Reserialize(hand, &a) && Reserialize(Canon(hand), &b) && a == b);
    CHECK(Canon(hand) == x);

    // Object numbers with gaps: a transient object is not written, so the writer leaves a gap.
    {
        Scene s("gap");
        GameObject* p = s.CreateGameObject("P");
        GameObject* tmp = s.CreateGameObject("T");
        tmp->transient = true;
        GameObject* q = s.CreateGameObject("Q");
        q->transform->SetParent(p->transform, false);
        const std::string t = SceneSerializer::Serialize(s);
        CHECK(Contains(t, "gameobject 2 \"Q\"") && Contains(t, "parent 0"));
        const std::string c = Canon(t);
        CHECK(Contains(c, "gameobject 1 \"Q\""));
        std::string ra, rb;
        CHECK(Reserialize(t, &ra) && Reserialize(c, &rb) && StripGuids(ra) == StripGuids(rb));
    }
}

static void TestFuzz() {
    RUN_SUITE("canonical: garbage and truncated input");
    TkRng r(4242);
    int parsed = 0, checks = 0;
    const std::string x = LiveSceneText(r, 14);
    // Every truncation point.
    for (std::size_t cut = 0; cut <= x.size(); ++cut) {
        const std::string t = x.substr(0, cut);
        SceneText::CanonicalReport rep;
        const std::string c = SceneText::Canonicalize(t, SceneText::CanonicalOptions(), &rep);
        SceneText::Document d;
        const bool ok = SceneText::Parse(t, d);
        if (!ok) { if (c != t || !rep.skipped) CHECK(false); }
        else { ++parsed; if (Canon(c) != c) CHECK(false); }
        ++checks;
    }
    CHECK(checks == (int)x.size() + 1);
    CHECK(parsed > 0);
    // Random byte damage.
    for (int i = 0; i < 400; ++i) {
        std::string t = x;
        const int edits = 1 + r.range(6);
        for (int k = 0; k < edits; ++k) {
            const std::size_t at = (std::size_t)r.range((int)t.size());
            switch (r.range(4)) {
                case 0: t[at] = (char)r.range(256); break;
                case 1: t.erase(at, (std::size_t)(1 + r.range(20))); break;
                case 2: t.insert(at, "\"\\\r\n end gameobject "); break;
                default: t[at] = "\"\\\n =0x"[r.range(7)];
            }
            if (t.empty()) t = "x";
        }
        SceneText::CanonicalReport rep;
        const std::string c = SceneText::Canonicalize(t, SceneText::CanonicalOptions(), &rep);
        SceneText::Document d;
        const bool ok = SceneText::Parse(t, d);
        if (!ok) CHECK(c == t);
        else CHECK(Canon(c) == c);
        SceneText::Model m;
        SceneText::BuildModel(t, m);   // must not crash either
        SceneText::Validate(t);
    }
    // Pure noise.
    for (int i = 0; i < 300; ++i) {
        std::string t;
        const int len = r.range(200);
        for (int k = 0; k < len; ++k) t += (char)r.range(256);
        CHECK(SceneText::Canonicalize(t) == t);
        SceneText::Validate(t);
    }
    // Hostile but parseable: huge numbers, odd tokens.
    const std::string weird =
        "okayscene 3\ngameobject 2147483647 \"\"\n  parent 99999999999999\n  transform 1e999 nan inf -\nend\n"
        "gameobject -5 \"neg\"\n  parent -5\nend\n";
    SceneText::Model m;
    std::string err;
    SceneText::BuildModel(weird, m, &err);
    SceneText::Canonicalize(weird);
    CHECK(true);
}

int main() {
    TestBasics();
    TestStrings();
    TestNumbers();
    TestOrderAndNumbering();
    TestUntouched();
    TestParseModel();
    TestMeaningPreserved();
    TestFuzz();
    TEST_MAIN_RESULT();
}
