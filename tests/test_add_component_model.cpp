#include "test_framework.hpp"
#include "AddComponentModel.hpp"
#include <cstdio>
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

    CHECK(PlainCategory("Physics 2D", "Rigidbody2D") == "Physics");
    CHECK(PlainCategory("Physics 3D", "Box Collider 3D") == "Physics");
    CHECK(PlainCategory("Camera", "Camera") == "Cameras");
    CHECK(PlainCategory("Lighting", "Directional Light") == "Rendering");
    CHECK(PlainCategory("Rendering", "Particle System") == "Effects");
    CHECK(PlainCategory("Gameplay", "Character Controller 3D") == "Controllers");
    CHECK(PlainCategory("Gameplay", "Stat: Health") == "Survival");
    CHECK(PlainCategory("Gameplay", "Network Manager") == "Multiplayer");
    CHECK(PlainCategory("Gameplay", "Inventory") == "Gameplay");
    CHECK(PlainCategory("No-Code Mechanics", "Mover") == "No-Code");
    CHECK(PlainCategory("RPG / Turn-Based", "Turn Manager") == "Gameplay");

    // every category the old menu used lands in one of the plain ones
    std::set<std::string> order(CategoryOrder().begin(), CategoryOrder().end());
    for (const char* raw : {"Rendering", "Animation", "Physics 2D", "Physics 3D", "Lighting", "Camera", "Scripts", "Audio",
                            "Gameplay", "No-Code Mechanics", "RPG / Turn-Based", "UI"})
        CHECK(order.count(PlainCategory(raw, "Something")) == 1);
    CHECK(CategoryOrder().size() <= 13);

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
    // same label in two raw categories keeps two keys
    CHECK(es[Index(es, "Sprite Animator")].key != es.back().key);
}

void TestSearch() {
    RUN_SUITE("search ranking");
    auto es = Sample();
    Prefs prefs; State s;
    auto run = [&](const char* q) { s.query = q; return Build(es, s, prefs); };

    View v = run("rigid");
    auto n = Names(es, v);
    CHECK(!n.empty());
    CHECK(n.size() >= 2);
    CHECK(n[0] == "Rigidbody2D");   // available beats the greyed Rigidbody3D
    CHECK(Has(n, "Rigidbody3D"));

    // name start > word start > substring > letters in order > description > other words
    std::vector<Entry> t;
    t.push_back(MakeEntry("UI", "Xa Light", true));       // word start
    t.push_back(MakeEntry("UI", "Light Box", true));      // name start
    t.push_back(MakeEntry("UI", "Daylight", true));       // substring
    t.push_back(MakeEntry("UI", "Lamp Is Great Hot", true)); // letters in order l..i..g..h..t
    t.push_back(MakeEntry("UI", "Zzz (makes light for rooms)", true)); // description word
    t.push_back(MakeEntry("UI", "Zzy", true));
    t.back().aliases = {"light bulb"};                    // other word (also in description of none)
    State st; st.query = "light";
    View vt = Build(t, st, prefs);
    auto tn = Names(t, vt);
    CHECK(tn.size() == 6);
    CHECK(tn[0] == "Light Box"); CHECK(tn[1] == "Xa Light"); CHECK(tn[2] == "Daylight");
    CHECK(tn[3] == "Lamp Is Great Hot"); CHECK(tn[4] == "Zzz"); CHECK(tn[5] == "Zzy");

    // highlights: the letters that matched, in the name
    Match m = MatchEntry(es[Index(es, "Box Collider 3D")], "coll");
    CHECK(m.score > 0);
    CHECK(m.hits == (std::vector<int>{4, 5, 6, 7}));
    m = MatchEntry(es[Index(es, "Rigidbody2D")], "rbd");   // letters in order
    CHECK(m.score > 0); CHECK(m.hits.size() == 3);

    // old names and other words (aliases): "sound" finds Audio Source, "hitbox" finds colliders, "sun" finds the light
    n = Names(es, run("sound"));  CHECK(Has(n, "Audio Source"));
    n = Names(es, run("hitbox")); CHECK(Has(n, "Box Collider 3D"));
    n = Names(es, run("sun"));    CHECK(Has(n, "Directional Light"));
    n = Names(es, run("camera rig")); CHECK(Has(n, "Camera Follow") && Has(n, "Virtual Camera"));
    // two words must both match
    n = Names(es, run("box collider")); CHECK(n.size() >= 1); CHECK(n[0] == "Box Collider 3D");
    n = Names(es, run("box zzzzz"));  CHECK(n.empty());
    // case does not matter and spaces around are ignored
    n = Names(es, run("  CANVAS ")); CHECK(!n.empty() && n[0] == "Canvas");
    // one card per component even when it is in two categories
    n = Names(es, run("sprite anim")); int count = 0; for (auto& x : n) if (x == "Sprite Animator") ++count; CHECK(count == 1);
    // empty query is not searching
    CHECK(!run("").searching); CHECK(run("a").searching);
    // favorites and recents break ties in their favour
    Prefs p2; p2.ToggleFavorite("Sphere Collider 3D");
    s.query = "collider"; View v2 = Build(es, s, p2);
    CHECK(es[v2.rows[0].entry].name == "Sphere Collider 3D");
}

void TestDidYouMean() {
    RUN_SUITE("no results: did you mean");
    auto es = Sample();
    Prefs prefs; State s;
    View v = Build(es, s, prefs);
    s.query = "cammera";
    v = Build(es, s, prefs);
    CHECK(v.rows.empty());
    CHECK(!v.didYouMean.empty());
    CHECK(v.didYouMean[0] == "Camera");
    CHECK(v.didYouMean.size() <= 3);
    s.query = "qqqqqqqq";
    v = Build(es, s, prefs);
    CHECK(v.rows.empty()); CHECK(v.didYouMean.empty());
    CHECK(EditDistance("kitten", "sitting") == 3);
    CHECK(EditDistance("", "abc") == 3);
}

void TestPrefs() {
    RUN_SUITE("recent and favorites persist");
    Prefs p;
    for (int i = 0; i < 12; ++i) p.NoteUsed("C" + std::to_string(i));
    CHECK(p.recent.size() == 8); CHECK(p.recent[0] == "C11");
    p.NoteUsed("C5"); CHECK(p.recent[0] == "C5"); CHECK(p.recent.size() == 8);
    p.ToggleFavorite("Camera"); p.ToggleFavorite("Canvas"); p.ToggleFavorite("Camera");
    CHECK(!p.IsFavorite("Camera")); CHECK(p.IsFavorite("Canvas"));

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
    RUN_SUITE("categories, counts, sections");
    auto es = Sample();
    Prefs prefs; State s;
    View v = Build(es, s, prefs);
    CHECK(v.cats.front().name == kCatForObject);   // Rigidbody3D is on the object: colliders are suggested first
    { std::vector<Entry> one; one.push_back(MakeEntry("Rendering", "Sprite Renderer", true));
      CHECK(Build(one, State(), Prefs()).cats.front().name == "Rendering"); }   // nothing special until there is something to show
    // the Physics count only counts what can be added now: Rigidbody3D and Tilemap Collider are greyed
    int physics = -1; for (auto& c : v.cats) if (c.name == "Physics") physics = c.count;
    CHECK(physics == 3);   // Box 3D, Sphere 3D, Rigidbody2D
    // the same card is not counted twice in a category
    s.category = "Physics"; v = Build(es, s, prefs);
    CHECK(v.rows.size() == 5);                       // all five, greyed ones last
    CHECK(es[v.rows[3].entry].available == false && es[v.rows[4].entry].available == false);
    CHECK(es[v.rows[2].entry].available);

    // suggestions: Rigidbody3D is on the object -> colliders; Box Collider is there? no -> suggested
    CHECK(DefaultCategory(es, prefs) == kCatForObject);
    s.category = kCatForObject; v = Build(es, s, prefs);
    auto n = Names(es, v);
    CHECK(Has(n, "Box Collider 3D")); CHECK(Has(n, "Sphere Collider 3D"));
    CHECK(!Has(n, "Rigidbody3D"));
    CHECK(!v.rows.empty() && !v.rows[0].note.empty());
    CHECK(v.cats.front().name == kCatForObject);

    // Recent and Favorites appear once they have something, in that order after the suggestions
    prefs.NoteUsed("Canvas"); prefs.ToggleFavorite("Camera");
    s.category = kCatRecent; v = Build(es, s, prefs);
    CHECK(Names(es, v) == std::vector<std::string>{"Canvas"});
    s.category = kCatFavorites; v = Build(es, s, prefs);
    CHECK(Names(es, v) == std::vector<std::string>{"Camera"});
    CHECK(v.cats[0].name == kCatForObject); CHECK(v.cats[1].name == kCatRecent); CHECK(v.cats[2].name == kCatFavorites);
    // a remembered name that is not offered any more is skipped quietly
    prefs.NoteUsed("Gone Component"); s.category = kCatRecent; v = Build(es, s, prefs);
    CHECK(v.rows.size() == 1);

    // no suggestions without the right pieces on the object
    std::vector<Entry> plain; plain.push_back(MakeEntry("Rendering", "Sprite Renderer", true));
    CHECK(DefaultCategory(plain, Prefs()) == "Rendering");
    Prefs rp; rp.NoteUsed("Sprite Renderer");
    CHECK(DefaultCategory(plain, rp) == kCatRecent);

    // a camera suggests a camera rig; a sprite suggests a collider
    auto sg = Suggest({"Camera"});
    bool follow = false, vcam = false; for (auto& x : sg) { if (x.name == "Camera Follow") follow = true; if (x.name == "Virtual Camera") vcam = true; }
    CHECK(follow && vcam);
    CHECK(Suggest({}).empty());
    CHECK(Suggest({"Rigidbody3D", "Box Collider 3D", "Sphere Collider 3D"}).size() == 1);   // only a capsule left
}

void TestKeyboard() {
    RUN_SUITE("keyboard");
    auto es = Sample();
    Prefs prefs; State s; s.category = "Cameras";
    View v = Build(es, s, prefs);
    CHECK(v.rows.size() == 3);
    Outcome o = HandleKey(s, v, es, Key::Down); CHECK(s.cursor == 1); CHECK(o.activate < 0 && !o.close);
    HandleKey(s, v, es, Key::Down); CHECK(s.cursor == 2);
    HandleKey(s, v, es, Key::Down); CHECK(s.cursor == 0);      // wraps
    HandleKey(s, v, es, Key::Up);   CHECK(s.cursor == 2);
    HandleKey(s, v, es, Key::Home); CHECK(s.cursor == 0);
    HandleKey(s, v, es, Key::End);  CHECK(s.cursor == 2);
    HandleKey(s, v, es, Key::PageUp); CHECK(s.cursor == 0);
    HandleKey(s, v, es, Key::PageDown); CHECK(s.cursor == 2);

    s.cursor = 1;
    o = HandleKey(s, v, es, Key::Enter);
    CHECK(o.activate == v.rows[1].entry); CHECK(es[o.activate].name == "Camera Follow");
    o = HandleKey(s, v, es, Key::Escape); CHECK(o.close);

    // Tab switches to the category column: Down picks the next category, Enter goes back to the cards
    o = HandleKey(s, v, es, Key::Tab); CHECK(s.column == Column::Categories);
    std::string before = s.category;
    HandleKey(s, v, es, Key::Down); CHECK(s.category != before); CHECK(s.cursor == 0);
    View v2 = Build(es, s, prefs);
    o = HandleKey(s, v2, es, Key::Enter); CHECK(o.activate < 0); CHECK(s.column == Column::Cards);
    HandleKey(s, v2, es, Key::Tab); HandleKey(s, v2, es, Key::Tab); CHECK(s.column == Column::Cards);
    // Up from the first category wraps to the last one
    s.SelectCategory(v.cats.front().name); s.column = Column::Categories;
    View v3 = Build(es, s, prefs);
    HandleKey(s, v3, es, Key::Up); CHECK(s.category == v3.cats.back().name);

    // typing resets to the first result and to the cards; Enter adds the top result
    s.column = Column::Categories; s.cursor = 3;
    s.SetQuery("canv");
    CHECK(s.cursor == 0); CHECK(s.column == Column::Cards);
    View vs = Build(es, s, prefs);
    o = HandleKey(s, vs, es, Key::Enter);
    CHECK(o.activate >= 0 && es[o.activate].name == "Canvas");
    // moving in the category column during a search ends the search
    HandleKey(s, vs, es, Key::Tab);
    HandleKey(s, vs, es, Key::Down); CHECK(s.query.empty());

    // Enter with no results explains, does not add
    State e; e.query = "zzzzzz"; View ve = Build(es, e, prefs);
    o = HandleKey(e, ve, es, Key::Enter); CHECK(o.activate < 0); CHECK(!e.notice.empty());
    // a cursor left past the end after the list shrank is pulled back in
    State c; c.category = "Cameras"; c.cursor = 99; View vc = Build(es, c, prefs); ClampCursor(c, vc); CHECK(c.cursor == 2);
}

void TestDisabled() {
    RUN_SUITE("greyed-out components explain themselves");
    auto es = Sample();
    State s; s.category = "Physics";
    Prefs prefs; View v = Build(es, s, prefs);
    int rb = Index(es, "Rigidbody3D");
    Outcome o = Choose(s, es, rb);
    CHECK(o.activate < 0);
    CHECK(s.notice.find("Rigidbody3D") != std::string::npos);
    CHECK(s.notice.find("Already on this object") != std::string::npos);
    s.notice.clear();
    o = Choose(s, es, Index(es, "Tilemap Collider 2D"));
    CHECK(o.activate < 0); CHECK(s.notice.find("Tilemap") != std::string::npos);
    // Enter on a greyed row explains it too
    s.notice.clear(); s.query = "rigidbody3d"; v = Build(es, s, prefs);
    o = HandleKey(s, v, es, Key::Enter);
    CHECK(o.activate < 0); CHECK(!s.notice.empty());
    // a free one runs
    o = Choose(s, es, Index(es, "Box Collider 3D")); CHECK(o.activate == Index(es, "Box Collider 3D"));
    CHECK(Choose(s, es, -1).activate < 0); CHECK(Choose(s, es, 999).activate < 0);
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

}  // namespace

int main() {
    TestWords(); TestEntries(); TestSearch(); TestDidYouMean(); TestPrefs(); TestCategoriesAndSections();
    TestKeyboard(); TestDisabled(); TestAddAndFix();
    TEST_MAIN_RESULT();
}
