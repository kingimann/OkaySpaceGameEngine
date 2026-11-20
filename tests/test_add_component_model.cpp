#include "test_framework.hpp"
#include "AddComponentModel.hpp"
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <filesystem>
#include <typeinfo>
#include <set>
#include <string>
#include <vector>

// The Add Component browser's logic (editor/AddComponentModel.*, no ImGui): plain categories, descriptions, search ranking,
// Recent / Favorites kept in a file, suggestions, greyed-out reasons, the keyboard state machine and the "Also added" text.
using namespace oked::addcomp;

namespace {

std::vector<Entry> Sample() {
    std::vector<Entry> v;
    auto add = [&](const char* cat, const char* label, bool absent = true) { v.push_back(MakeEntry(cat, label, absent)); };
    add("Rendering", "Sprite Renderer");
    add("Rendering", "Mesh Renderer (3D)");
    add("Rendering", "LOD Group (swap in simpler models when far away)");
    add("Rendering", "Particle System");
    add("Animation", "Animator (keyframes)");
    add("Animation", "Animator (states + transitions)");
    add("Animation", "Sprite Animator");
    add("Rendering", "Sprite Animator");
    add("Physics 3D", "Rigidbody3D", false);          // already on the object
    add("Physics 3D", "Box Collider 3D");
    add("Physics 3D", "Sphere Collider 3D");
    add("Physics 3D", "Physics Surface (bounce and slide)");
    add("Gameplay", "Jump Pad (launch on touch)");
    add("Physics 2D", "Rigidbody2D");
    add("Physics 2D", "Tilemap Collider 2D", false);  // needs a Tilemap
    add("Lighting", "Directional Light");
    add("Camera", "Camera");
    add("Camera", "Camera Follow");
    add("Camera", "Virtual Camera");
    add("Audio", "Audio Source");
    add("Audio", "Music Player");
    add("Scripts", "New Script...");
    add("Gameplay", "Character Controller 3D");
    add("Gameplay", "Stat: Health");
    add("Gameplay", "Network Manager");
    add("UI", "Canvas");
    add("UI", "UI Button");
    return v;
}

int Index(const std::vector<Entry>& es, const std::string& label) {
    for (int i = 0; i < (int)es.size(); ++i) if (es[i].label == label) return i;
    return -1;
}
std::vector<std::string> Names(const std::vector<Entry>& es, const View& v) {
    std::vector<std::string> out;
    for (const Row& r : v.rows) out.push_back(es[r.entry].name);
    return out;
}
bool Has(const std::vector<std::string>& v, const std::string& s) { for (auto& x : v) if (x == s) return true; return false; }

void TestWords() {
    RUN_SUITE("labels, categories, descriptions");
    std::string n, h;
    SplitLabel("Joint 3D (distance/spring/pin/hinge/slider)", n, h);
    CHECK(n == "Joint 3D"); CHECK(h == "distance/spring/pin/hinge/slider");
    SplitLabel("Mesh Renderer (3D)", n, h);
    CHECK(n == "Mesh Renderer (3D)"); CHECK(h.empty());
    SplitLabel("Hand (legacy)", n, h);
    CHECK(n == "Hand (legacy)");
    SplitLabel("New Script...", n, h);
    CHECK(n == "New Script...");

    CHECK(PlainCategory("x", "Rigidbody2D") == "Physics");
    CHECK(PlainCategory("x", "Box Collider 3D") == "Physics");
    CHECK(PlainCategory("x", "Camera") == "Rendering");
    CHECK(PlainCategory("x", "Directional Light") == "Rendering");
    CHECK(PlainCategory("x", "Particle System") == "Rendering");
    CHECK(PlainCategory("x", "Character Controller 3D") == "Gameplay");
    CHECK(PlainCategory("x", "Stat: Health") == "Gameplay");
    CHECK(PlainCategory("x", "Network Manager") == "Networking");
    CHECK(PlainCategory("x", "UI Button") == "UI");
    CHECK(PlainCategory("x", "New Script...") == "Scripting");
    CHECK(PlainCategory("x", "Foot IK (plant feet)") == "Animation");
    CHECK(PlainCategory("x", "Voxel Terrain (diggable)") == "World");
    CHECK(PlainCategory("x", "Zzz Unknown Thing") == kUncategorised);
    CHECK(KeyOf("Box Collider 3D (a box)") == "boxcollider3d");
    CHECK(KeyOf("BoxCollider3D") == "boxcollider3d");
    CHECK(KeyOf("Stat: Health") == "stathealth");
    CHECK(KeyOf("Animator (states + transitions)") == "animatorstates");
    CHECK(KeyOf("Animator (keyframes)") == "animator");
    CHECK(Canon("light") == "directionallight");
    // places
    CHECK(Classify("boxcollider3d").group == "Colliders 3D");
    CHECK(Classify("cylindercollider3d").group == "Colliders 3D");
    CHECK(Classify("conecollider3d").group == "Colliders 3D");          // a new collider type needs no edit
    CHECK(Classify("compositecollider2d").group == "Colliders 2D");
    CHECK(Classify("joint3d").group == "Joints");
    CHECK(Classify("buoyancy").group == "Water");
    CHECK(Classify("conveyorbelt").group == "Forces & Toys");
    CHECK(Classify("vehiclecontroller").group == "Vehicles");
    CHECK(Classify("camerazone").group == "Camera");
    CHECK(Classify("windzone").group == "Sky & Weather");
    CHECK(Classify("stathealth").group == "Health & Combat");
    CHECK(Classify("stathunger").group == "Survival Stats");
    CHECK(Classify("statuseffects").group == "Survival Rules");
    CHECK(Classify("uiconstraints").group == "Setup");
    CHECK(Classify("uisomenewthing").group == "More");
    CHECK(Classify("fullbodyik").group == "IK");
    CHECK(Classify("myscriptsfooku").category == kUncategorised);
    CHECK(Classify("foobarokay").category == "Scripting");
    CHECK(CategoryOrder().size() == 10);
    for (const std::string& c : CategoryOrder()) CHECK(c != kUncategorised);
    CHECK(!GroupOrder("Physics").empty() && GroupOrder("Physics")[0] == "Bodies");
    CHECK(GroupOrder("Audio").empty());
    { std::vector<std::string> ex = RuleExactKeys(); std::set<std::string> u(ex.begin(), ex.end()); CHECK(u.size() == ex.size()); }   // no key claimed twice
    CHECK(PlainCategory("x", "Rigidbody3D") == "Physics");

    CHECK(Describe("Rigidbody3D", "", "Physics").find("fall") != std::string::npos);
    CHECK(Describe("Foo", "does a thing", "Gameplay") == "Does a thing.");
    CHECK(!Describe("Foo", "", "Gameplay").empty());
    CHECK(Describe("Animator", "keyframes", "Animation") != Describe("Animator", "states + transitions", "Animation"));

    CHECK(Badge("Box Collider 3D") == "BC");
    CHECK(Badge("Rigidbody3D") == "R");
    CHECK(Badge("3D") == "+");
    CHECK(PrettyTypeName("okay::MeshRenderer") == "Mesh Renderer");
    CHECK(PrettyTypeName("class okay::LODGroup") == "LOD Group");
    CHECK(PrettyTypeName("okay::Rigidbody3D") == "Rigidbody3D");
    CHECK(PrettyTypeNameFromTypeInfo(typeid(Entry).name()) == "Entry");
}

void TestEntries() {
    RUN_SUITE("entries and reasons");
    auto es = Sample();
    const Entry& rb = es[Index(es, "Rigidbody3D")];
    CHECK(!rb.available); CHECK(rb.present); CHECK(rb.reason.find("Already on this object") == 0);
    const Entry& tm = es[Index(es, "Tilemap Collider 2D")];
    CHECK(!tm.available); CHECK(!tm.present); CHECK(tm.reason.find("Tilemap") != std::string::npos);
    CHECK(es[Index(es, "Box Collider 3D")].reason.empty());
    for (const Entry& e : es) { CHECK(!e.description.empty()); CHECK(!e.category.empty()); CHECK(!e.key.empty()); }
    // same label in two raw categories keeps two keys, one nkey
    CHECK(es[Index(es, "Box Collider 3D")].group == "Colliders 3D");
    CHECK(es[Index(es, "Box Collider 3D")].nkey == "boxcollider3d");
}

void TestSearch() {
    RUN_SUITE("search ranking");
    auto es = Sample();
    Prefs prefs; State s;
    auto run = [&](const char* q) { s.query = q; return Build(es, s, prefs); };

    auto n = Names(es, run("rigid"));
    CHECK(n.size() >= 2);
    CHECK(n[0] == "Rigidbody2D");   // available beats the greyed Rigidbody3D
    CHECK(Has(n, "Rigidbody3D"));

    // name start > word start > substring > letters in order > plain words > description
    std::vector<Entry> t;
    t.push_back(MakeEntry("UI", "Xa Light", true));            // word start
    t.push_back(MakeEntry("UI", "Light Box", true));           // name start
    t.push_back(MakeEntry("UI", "Daylight", true));            // substring
    t.push_back(MakeEntry("UI", "Lamp Is Great Hot", true));   // letters in order
    t.push_back(MakeEntry("UI", "Zzz (makes light for rooms)", true));   // description word
    t.push_back(MakeEntry("UI", "Zzy", true));
    t.back().aliases = {"light bulb"};                         // a plain word people use
    State st; st.query = "light";
    auto tn = Names(t, Build(t, st, prefs));
    CHECK(tn.size() == 6);
    CHECK(tn[0] == "Light Box"); CHECK(tn[1] == "Xa Light"); CHECK(tn[2] == "Daylight");
    CHECK(tn[3] == "Lamp Is Great Hot");
    CHECK(tn[4] == "Zzy");   // plain words rank above the description sentence
    CHECK(tn[5] == "Zzz");

    Match m = MatchEntry(es[Index(es, "Box Collider 3D")], "coll");
    CHECK(m.score > 0); CHECK(m.hits == (std::vector<int>{4, 5, 6, 7}));
    m = MatchEntry(es[Index(es, "Rigidbody2D")], "rbd");
    CHECK(m.score > 0); CHECK(m.hits.size() == 3);

    // plain words and synonyms
    n = Names(es, run("sound"));  CHECK(Has(n, "Audio Source"));
    n = Names(es, run("hitbox")); CHECK(Has(n, "Box Collider 3D"));
    n = Names(es, run("sun"));    CHECK(Has(n, "Directional Light"));
    n = Names(es, run("hit points")); CHECK(!n.empty() && n[0] == "Stat: Health");
    n = Names(es, run("hp"));     CHECK(!n.empty() && n[0] == "Stat: Health");
    n = Names(es, run("bounce")); CHECK(Has(n, "Rigidbody3D") || Has(n, "Rigidbody2D"));
    n = Names(es, run("bounce")); CHECK(Has(n, "Physics Surface"));
    n = Names(es, run("jump"));   CHECK(n.size() >= 2 && (Has(n, "Character Controller 3D")));
    n = Names(es, run("camera rig")); CHECK(Has(n, "Camera Follow") && Has(n, "Virtual Camera"));
    n = Names(es, run("box collider")); CHECK(n.size() >= 1); CHECK(n[0] == "Box Collider 3D");
    n = Names(es, run("box zzzzz"));  CHECK(n.empty());
    n = Names(es, run("  CANVAS ")); CHECK(!n.empty() && n[0] == "Canvas");
    n = Names(es, run("sprite anim")); int count = 0; for (auto& x : n) if (x == "Sprite Animator") ++count; CHECK(count == 1);
    CHECK(!run("").searching); CHECK(run("a").searching);
    // each result shows its category (the window prints it as a grey tag)
    { View v = run("collider"); CHECK(!v.rows.empty()); for (const Row& r : v.rows) CHECK(!es[r.entry].category.empty()); }
    Prefs p2; p2.ToggleFavorite("spherecollider3d");
    s.query = "collider"; View v2 = Build(es, s, p2);
    CHECK(es[v2.rows[0].entry].name == "Sphere Collider 3D");
}

void TestDidYouMean() {
    RUN_SUITE("no results: did you mean");
    auto es = Sample();
    Prefs prefs; State s;
    s.query = "cammera";
    View v = Build(es, s, prefs);
    CHECK(v.rows.empty()); CHECK(!v.didYouMean.empty()); CHECK(v.didYouMean[0] == "Camera"); CHECK(v.didYouMean.size() <= 3);
    s.query = "qqqqqqqq";
    v = Build(es, s, prefs);
    CHECK(v.rows.empty()); CHECK(v.didYouMean.empty());
    CHECK(EditDistance("kitten", "sitting") == 3);
    CHECK(EditDistance("", "abc") == 3);
}

void TestPrefs() {
    RUN_SUITE("recent and favourites persist");
    Prefs p;
    for (int i = 0; i < 12; ++i) p.NoteUsed("c" + std::to_string(i));
    CHECK(p.recent.size() == 8); CHECK(p.recent[0] == "c11");
    p.NoteUsed("c5"); CHECK(p.recent[0] == "c5"); CHECK(p.recent.size() == 8);
    p.ToggleFavorite("camera"); p.ToggleFavorite("canvas"); p.ToggleFavorite("camera");
    CHECK(!p.IsFavorite("camera")); CHECK(p.IsFavorite("canvas"));
    Prefs q = Prefs::Parse(p.Serialize());
    CHECK(q.recent == p.recent); CHECK(q.favorites == p.favorites);
    const std::string path = "test_add_component_prefs.tmp";
    CHECK(SavePrefs(path, p));
    Prefs r; CHECK(LoadPrefs(path, r));
    CHECK(r.recent == p.recent); CHECK(r.favorites == p.favorites);
    std::remove(path.c_str());
    Prefs none; CHECK(!LoadPrefs("no_such_file_for_add_component.tmp", none));
    Prefs bad = Prefs::Parse("junk\nrecent \nfav \nrecent A\r\nfav B\nfav B\n");
    CHECK(bad.recent.size() == 1 && bad.recent[0] == "A"); CHECK(bad.favorites.size() == 1);
}

void TestCategoriesAndSections() {
    RUN_SUITE("categories, counts, sections, groups");
    auto es = Sample();
    Prefs prefs; State s;
    View v = Build(es, s, prefs);
    CHECK(v.cats.front().name == kCatForObject);   // Rigidbody3D is on the object: colliders are suggested first
    { std::vector<Entry> one; one.push_back(MakeEntry("Rendering", "Sprite Renderer", true));
      CHECK(Build(one, State(), Prefs()).cats.front().name == "Rendering"); }
    // The Physics count only counts what can be added now (Box 3D, Sphere 3D, Rigidbody2D, Box Collider 2D is not in the sample)
    int physics = -1; for (auto& c : v.cats) if (c.name == "Physics") physics = c.count;
    CHECK(physics == 4);
    s.category = "Physics"; v = Build(es, s, prefs);
    CHECK(v.rows.size() == 6);                       // Rigidbody2D, Box 3D, Sphere 3D, Physics Surface, then the greyed Rigidbody3D, Tilemap Collider
    { bool greySeen = false; std::string g; for (const Row& r : v.rows) {      // inside a group the ones that can be added come first
        if (r.group != g) { g = r.group; greySeen = false; }
        if (!es[r.entry].available) greySeen = true; else CHECK(!greySeen); } }
    // group headings: the first card of each group carries it, in group order (Bodies, Colliders 3D, Colliders 2D ...)
    int heads = 0; for (const Row& r : v.rows) if (r.groupStart) ++heads;
    CHECK(heads >= 2);
    CHECK(v.rows[0].groupStart && v.rows[0].group == "Bodies");
    // one card per component: Sprite Animator is listed twice by the menu code but counts once
    int sa = 0; s.category = "Animation"; v = Build(es, s, prefs);
    for (const Row& r : v.rows) if (es[r.entry].name == "Sprite Animator") ++sa;
    CHECK(sa == 1);

    CHECK(DefaultCategory(es, prefs) == kCatForObject);
    s.category = kCatForObject; v = Build(es, s, prefs);
    auto n = Names(es, v);
    CHECK(Has(n, "Box Collider 3D")); CHECK(Has(n, "Sphere Collider 3D"));
    CHECK(!Has(n, "Rigidbody3D"));
    CHECK(!v.rows.empty() && !v.rows[0].note.empty());

    prefs.NoteUsed("canvas"); prefs.ToggleFavorite("camera");
    s.category = kCatRecent; v = Build(es, s, prefs);
    CHECK(Names(es, v) == std::vector<std::string>{"Canvas"});
    s.category = kCatFavorites; v = Build(es, s, prefs);
    CHECK(Names(es, v) == std::vector<std::string>{"Camera"});
    CHECK(v.cats[0].name == kCatForObject); CHECK(v.cats[1].name == kCatRecent); CHECK(v.cats[2].name == kCatFavorites);
    prefs.NoteUsed("gone"); s.category = kCatRecent; v = Build(es, s, prefs);
    CHECK(v.rows.size() == 1);

    std::vector<Entry> plain; plain.push_back(MakeEntry("Rendering", "Sprite Renderer", true));
    CHECK(DefaultCategory(plain, Prefs()) == "Rendering");
    Prefs rp; rp.NoteUsed("spriterenderer");
    CHECK(DefaultCategory(plain, rp) == kCatRecent);

    // suggestions: a mesh suggests a collider and a rigidbody; a collider suggests a rigidbody; a light suggests nothing
    auto has = [](const std::vector<Suggestion>& v, const char* k) { for (auto& x : v) if (x.name == k) return true; return false; };
    auto sg = Suggest({"meshrenderer"});
    CHECK(has(sg, "boxcollider3d") && has(sg, "rigidbody3d"));
    sg = Suggest({"boxcollider3d"}); CHECK(has(sg, "rigidbody3d") && sg.size() == 1);
    sg = Suggest({"camera"}); CHECK(has(sg, "camerafollow") && has(sg, "virtualcamera"));
    CHECK(Suggest({"directionallight"}).empty());
    CHECK(Suggest({}).empty());
    // one collider is enough: no second kind is suggested
    sg = Suggest({"rigidbody3d", "boxcollider3d"}); CHECK(sg.empty());
}

void TestKeyboard() {
    RUN_SUITE("keyboard");
    auto es = Sample();
    Prefs prefs; State s; s.category = "Rendering";
    View v = Build(es, s, prefs);
    const int N = (int)v.rows.size();
    CHECK(N >= 6);
    Outcome o = HandleKey(s, v, es, Key::Down); CHECK(s.cursor == 1); CHECK(o.activate < 0 && !o.close);
    HandleKey(s, v, es, Key::Up); CHECK(s.cursor == 0);
    HandleKey(s, v, es, Key::Up);   CHECK(s.cursor == N - 1);      // wraps
    HandleKey(s, v, es, Key::Down); CHECK(s.cursor == 0);
    HandleKey(s, v, es, Key::End);  CHECK(s.cursor == N - 1);
    HandleKey(s, v, es, Key::Home); CHECK(s.cursor == 0);
    HandleKey(s, v, es, Key::PageDown); CHECK(s.cursor == 5);
    HandleKey(s, v, es, Key::PageUp); CHECK(s.cursor == 0);

    s.cursor = 1;
    o = HandleKey(s, v, es, Key::Enter);
    CHECK(o.activate == v.rows[1].entry);
    o = HandleKey(s, v, es, Key::Escape); CHECK(o.close);

    o = HandleKey(s, v, es, Key::Tab); CHECK(s.column == Column::Categories);
    std::string before = s.category;
    HandleKey(s, v, es, Key::Down); CHECK(s.category != before); CHECK(s.cursor == 0);
    View v2 = Build(es, s, prefs);
    o = HandleKey(s, v2, es, Key::Enter); CHECK(o.activate < 0); CHECK(s.column == Column::Cards);
    HandleKey(s, v2, es, Key::Tab); HandleKey(s, v2, es, Key::Tab); CHECK(s.column == Column::Cards);
    s.SelectCategory(v.cats.front().name); s.column = Column::Categories;
    View v3 = Build(es, s, prefs);
    HandleKey(s, v3, es, Key::Up); CHECK(s.category == v3.cats.back().name);

    // typing resets to the first result; Enter adds the highlighted one; Up/Down keep working while typing
    s.column = Column::Categories; s.cursor = 3;
    s.SetQuery("canv");
    CHECK(s.cursor == 0); CHECK(s.column == Column::Cards);
    View vs = Build(es, s, prefs);
    o = HandleKey(s, vs, es, Key::Enter);
    CHECK(o.activate >= 0 && es[o.activate].name == "Canvas");
    s.SetQuery("collider"); vs = Build(es, s, prefs);
    CHECK(vs.rows.size() >= 3);
    HandleKey(s, vs, es, Key::Down); CHECK(s.cursor == 1);
    o = HandleKey(s, vs, es, Key::Enter); CHECK(o.activate == vs.rows[1].entry);
    HandleKey(s, vs, es, Key::Tab);
    HandleKey(s, vs, es, Key::Down); CHECK(s.query.empty());

    State e; e.query = "zzzzzz"; View ve = Build(es, e, prefs);
    o = HandleKey(e, ve, es, Key::Enter); CHECK(o.activate < 0); CHECK(!e.notice.empty());
    State c; c.category = "Rendering"; c.cursor = 99; View vc = Build(es, c, prefs); ClampCursor(c, vc); CHECK(c.cursor == (int)vc.rows.size() - 1);
}

void TestDisabled() {
    RUN_SUITE("greyed-out components explain themselves");
    auto es = Sample();
    State s; s.category = "Physics";
    Prefs prefs; View v = Build(es, s, prefs);
    Outcome o = Choose(s, es, Index(es, "Rigidbody3D"));
    CHECK(o.activate < 0);
    CHECK(s.notice.find("Rigidbody3D") != std::string::npos);
    CHECK(s.notice.find("Already on this object") != std::string::npos);
    s.notice.clear();
    o = Choose(s, es, Index(es, "Tilemap Collider 2D"));
    CHECK(o.activate < 0); CHECK(s.notice.find("Tilemap") != std::string::npos);
    s.notice.clear(); s.query = "rigidbody3d"; v = Build(es, s, prefs);
    o = HandleKey(s, v, es, Key::Enter);
    CHECK(o.activate < 0); CHECK(!s.notice.empty());
    o = Choose(s, es, Index(es, "Box Collider 3D")); CHECK(o.activate == Index(es, "Box Collider 3D"));
    CHECK(Choose(s, es, -1).activate < 0); CHECK(Choose(s, es, 999).activate < 0);

    // needs another component first: greyed with the reason, until it is on the object
    std::vector<Entry> pre;
    pre.push_back(MakeEntry("Physics 3D", "Buoyancy (floats on Water)", true));
    pre.push_back(MakeEntry("Physics 3D", "Rigidbody3D", true));
    ApplyPrereqs(pre);
    CHECK(!pre[0].available); CHECK(pre[0].reason == "Needs a Rigidbody3D first.");
    CHECK(pre[1].available);
    pre[1] = MakeEntry("Physics 3D", "Rigidbody3D", false);
    pre[0] = MakeEntry("Physics 3D", "Buoyancy (floats on Water)", true);
    ApplyPrereqs(pre);
    CHECK(pre[0].available);
    CHECK(PrereqOf("joint2d") == "rigidbody2d"); CHECK(PrereqOf("canvas").empty());
}

void TestAddAndFix() {
    RUN_SUITE("add and fix");
    CHECK(AddedNotice("Rigidbody3D", {}) == "Added Rigidbody3D.");
    CHECK(AddedNotice("LOD Group", {"Mesh Renderer"}) == "Added LOD Group. It needs Mesh Renderer, so that was added too.");
    CHECK(AddedNotice("X", {"A", "B"}) == "Added X. It needs A and B, so that was added too.");
    CHECK(AddedNotice("X", {"A", "B", "C"}) == "Added X. It needs A, B and C, so that was added too.");
    CHECK(AlsoAdds("LOD Group") == std::vector<std::string>{"Mesh Renderer"});
    CHECK(AlsoAdds("Camera").empty());
}

// ---- every component in the source lands in exactly one category ---------------------------------------------------------
std::string ReadFile(const std::filesystem::path& p) { std::ifstream f(p, std::ios::binary); std::stringstream ss; ss << f.rdbuf(); return ss.str(); }

// The text of the first string literal at or after `i` (and its end), stopping at ';' '{' or '}' first.
bool NextLiteral(const std::string& t, std::size_t i, std::string& out, std::size_t limit) {
    for (std::size_t k = i; k < t.size() && k < i + limit; ++k) {
        if (t[k] == ';' || t[k] == '{' || t[k] == '}') return false;
        if (t[k] == '"') {
            std::string v;
            for (++k; k < t.size() && t[k] != '"'; ++k) { if (t[k] == '\\' && k + 1 < t.size()) ++k; v += t[k]; }
            out = v; return true;
        }
    }
    return false;
}

void TestEveryComponentHasAPlace() {
    RUN_SUITE("every component has exactly one place");
    namespace fs = std::filesystem;
    const fs::path root = OKAY_SOURCE_DIR;
    std::map<std::string, std::string> found;   // nkey -> where it came from
    auto note = [&](const std::string& label, const std::string& from) { found.insert({Canon(KeyOf(label)), label + "  (" + from + ")"}); };
    // (a) the rows the editor menus offer: item(<test>, "Label")
    for (const auto& f : fs::directory_iterator(root / "editor")) {
        const std::string ext = f.path().extension().string();
        if (ext != ".cpp" && ext != ".hpp") continue;
        static const char* const kMenuFiles[] = {"main.cpp", "LevelTools.cpp", "SplineTools.cpp", "NavigationWindow.cpp", "BehaviorTreeWindow.cpp", "WindTools.cpp", "VolumeTools.cpp"};
        bool menuFile = false; for (const char* m : kMenuFiles) if (f.path().filename() == m) menuFile = true;
        if (!menuFile) continue;
        const std::string t = ReadFile(f.path());
        for (std::size_t i = t.find("item("); i != std::string::npos; i = t.find("item(", i + 1)) {
            if (i > 0 && (std::isalnum((unsigned char)t[i - 1]) || t[i - 1] == '_')) continue;
            std::string lab;
            if (NextLiteral(t, i + 5, lab, 400) && !lab.empty() && std::isalnum((unsigned char)lab[0]) && lab.find('\n') == std::string::npos) note(lab, f.path().filename().string());
        }
        if (f.path().filename() == "VolumeTools.cpp")
            for (std::size_t i = t.find("\"Volume: "); i != std::string::npos; i = t.find("\"Volume: ", i + 1)) { std::string lab; NextLiteral(t, i, lab, 100); note(lab, "VolumeTools.cpp"); }
    }
    // (b) every component type the engine registers (PropertyBuilder<T> x("TypeName", ...)); a new one shows up here
    for (const auto& f : fs::recursive_directory_iterator(root / "engine" / "src")) {
        if (f.path().extension() != ".cpp") continue;
        const std::string t = ReadFile(f.path());
        for (std::size_t i = t.find("PropertyBuilder<"); i != std::string::npos; i = t.find("PropertyBuilder<", i + 1)) {
            const std::size_t gt = t.find('>', i);
            std::string lab;
            if (gt == std::string::npos || !NextLiteral(t, gt, lab, 60) || lab.empty()) continue;
            if (lab == "GameObject" || lab == "Transform") continue;
            note(lab, "registry: " + f.path().filename().string());
        }
    }
    CHECK(found.size() > 150);   // the scan really found the menu rows and the registry
    std::map<std::string, int> perGroup;
    std::vector<std::string> bad;
    for (const auto& kv : found) {
        const std::string key = kv.first;
        if (key.size() > 3 && key.compare(key.size() - 4, 4, "okay") == 0) continue;   // script files are read from the project at run time
        const Place p = Classify(key);
        if (p.category == kUncategorised) { bad.push_back(kv.second); continue; }
        const auto& cats = CategoryOrder();
        CHECK(std::find(cats.begin(), cats.end(), p.category) != cats.end());
        if (!p.group.empty()) { const auto g = GroupOrder(p.category); CHECK(std::find(g.begin(), g.end(), p.group) != g.end()); }
        ++perGroup[p.category + " / " + p.group];
    }
    for (const auto& b : bad) std::cerr << "  NO CATEGORY: " << b << "  -> add its key to a rule in editor/AddComponentTaxonomy.cpp\n";
    CHECK(bad.empty());
    // No dumping ground and no giant lists: a group holds at most 12; a category without sub-groups too; Misc at most 5
    for (const auto& kv : perGroup) {
        if (kv.second > 12) std::cerr << "  TOO MANY in " << kv.first << ": " << kv.second << "\n";
        CHECK(kv.second <= 12);
    }
    int misc = 0; for (const auto& kv : perGroup) if (kv.first.rfind("Misc", 0) == 0) misc += kv.second;
    CHECK(misc <= 5);
    // the keys named in the rules are real components (a typo in a rule would silently match nothing)
    int unknown = 0;
    for (const std::string& k : RuleExactKeys()) if (!found.count(k)) { ++unknown; std::cerr << "  rule key not found in the source: " << k << "\n"; }
    CHECK(unknown <= 12);   // a few are only on other branches (camera rig and zone, ui designer parts); more means typos
}

}  // namespace

int main() {
    TestWords(); TestEntries(); TestSearch(); TestDidYouMean(); TestPrefs(); TestCategoriesAndSections();
    TestKeyboard(); TestDisabled(); TestAddAndFix(); TestEveryComponentHasAPlace();
    TEST_MAIN_RESULT();
}
