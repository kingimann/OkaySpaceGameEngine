#include "test_framework.hpp"
#include "SceneAddMenu.hpp"
#include <set>
#include <string>
#include <vector>

// The Scene view's right-click "add here" menu as plain data (editor/SceneAddMenu.hpp, no ImGui, no scene): the click-versus-drag
// decision, where the new object goes (ray against a surface, else the ground, else in front of the camera, 2D world position,
// snapping) and the menu itself (groups, entries, search).
using namespace oked::sceneadd;
using oked::menubar::Entry;
using oked::menubar::Kind;

namespace {

bool HasId(const std::vector<Entry>& es, const std::string& id) {
    for (const Entry& e : es) {
        if (e.id == id) return true;
        if (HasId(e.children, id)) return true;
    }
    return false;
}
const Entry* FindById(const std::vector<Entry>& es, const std::string& id) {
    for (const Entry& e : es) {
        if (e.id == id) return &e;
        if (const Entry* c = FindById(e.children, id)) return c;
    }
    return nullptr;
}
bool SeparatorsOk(const std::vector<Entry>& es) {
    if (es.empty()) return true;
    if (es.front().kind == Kind::Separator || es.back().kind == Kind::Separator) return false;
    for (std::size_t i = 1; i < es.size(); ++i)
        if (es[i].kind == Kind::Separator && es[i - 1].kind == Kind::Separator) return false;
    for (const Entry& e : es) if (!SeparatorsOk(e.children)) return false;
    return true;
}
bool DisabledHaveReasons(const std::vector<Entry>& es) {
    for (const Entry& e : es) {
        if (!e.enabled && e.reason.empty()) return false;
        if (!DisabledHaveReasons(e.children)) return false;
    }
    return true;
}
bool ContainsLabel(const std::vector<SearchItem>& v, const std::string& label) {
    for (const SearchItem& s : v) if (s.label == label) return true;
    return false;
}

void ClickVersusDrag() {
    RUN_SUITE("click versus drag");
    CHECK(IsClick(0, 0, 0.10f));          // a plain press and release
    CHECK(IsClick(2, -2, 0.20f));         // a tiny wobble is still a click
    CHECK(IsClick(4, 0, 0.34f));          // exactly at the limits
    CHECK(!IsClick(5, 0, 0.10f));         // moved: that is a drag (orbit / look / pan)
    CHECK(!IsClick(0, -9, 0.10f));
    CHECK(!IsClick(30, 30, 0.10f));
    CHECK(!IsClick(0, 0, 0.60f));         // held a long time: that is fly mode, not a click
    CHECK(!IsClick(0, 0, 0.36f));
    CHECK(!IsClick(0, 0, -1.0f));         // nonsense time
    ClickLimits tight{1.0f, 0.1f};
    CHECK(IsClick(1, 0, 0.1f, tight));
    CHECK(!IsClick(2, 0, 0.05f, tight));
    // Diagonal wobble: the distance counts, not each axis.
    CHECK(!IsClick(3.5f, 3.5f, 0.1f));    // length 4.95
    CHECK(IsClick(2.8f, 2.8f, 0.1f));     // length 3.96
}

void Placement3D() {
    RUN_SUITE("placement in 3D");
    const Ray down{{0, 10, 0}, {0, -1, 0}};
    // A surface under the cursor wins over the ground.
    {
        Placement p = PlaceIn3D(down, true, {0, 2.5f, 0}, kDefaultFallbackDistance);
        CHECK(p.source == PlaceSource::Surface);
        CHECK_NEAR(p.pos.y, 2.5, 1e-5);
    }
    // No surface: the ground plane y = 0.
    {
        Placement p = PlaceIn3D(down, false, {}, kDefaultFallbackDistance);
        CHECK(p.source == PlaceSource::Ground);
        CHECK_NEAR(p.pos.x, 0, 1e-5); CHECK_NEAR(p.pos.y, 0, 1e-5); CHECK_NEAR(p.pos.z, 0, 1e-5);
    }
    // Slanted ray (45 degrees) lands 10 along x.
    {
        Placement p = PlaceIn3D(Ray{{0, 10, 0}, {0.70710678f, -0.70710678f, 0}}, false, {}, kDefaultFallbackDistance);
        CHECK(p.source == PlaceSource::Ground);
        CHECK_NEAR(p.pos.x, 10, 1e-3); CHECK_NEAR(p.pos.y, 0, 1e-3);
    }
    // Looking up from above the ground (never reaches it): a fixed distance in front.
    {
        Placement p = PlaceIn3D(Ray{{0, 10, 0}, {0, 1, 0}}, false, {}, 8.0f);
        CHECK(p.source == PlaceSource::InFront);
        CHECK_NEAR(p.pos.y, 18, 1e-4);
    }
    // Along the horizon (parallel to the ground): in front.
    {
        Placement p = PlaceIn3D(Ray{{0, 5, 0}, {0, 0, -1}}, false, {}, 6.0f);
        CHECK(p.source == PlaceSource::InFront);
        CHECK_NEAR(p.pos.z, -6, 1e-4); CHECK_NEAR(p.pos.y, 5, 1e-4);
    }
    // A ground hit absurdly far away (grazing the horizon) is not used.
    {
        Placement p = PlaceIn3D(Ray{{0, 1, 0}, {0.99999f, -0.0001f, 0}}, false, {}, 10.0f);
        CHECK(p.source == PlaceSource::InFront);
    }
    // Camera below the ground looking down: the plane is behind the ray, so in front.
    {
        Placement p = PlaceIn3D(Ray{{0, -5, 0}, {0, -1, 0}}, false, {}, 10.0f);
        CHECK(p.source == PlaceSource::InFront);
        CHECK_NEAR(p.pos.y, -15, 1e-4);
    }
    // Non-unit direction is normalised for the fallback distance.
    {
        Placement p = PlaceIn3D(Ray{{0, 0, 0}, {0, 0, -4}}, false, {}, 10.0f);
        CHECK_NEAR(p.pos.z, -10, 1e-4);
    }
    // A zero direction cannot crash and stays at the origin of the ray.
    {
        Placement p = PlaceIn3D(Ray{{1, 2, 3}, {0, 0, 0}}, false, {}, 10.0f);
        CHECK(p.source == PlaceSource::InFront);
        CHECK_NEAR(p.pos.x, 1, 1e-5); CHECK_NEAR(p.pos.y, 2, 1e-5); CHECK_NEAR(p.pos.z, 3, 1e-5);
    }
    // A custom ground height.
    {
        Placement p = PlaceIn3D(down, false, {}, 10.0f, 3.0f);
        CHECK(p.source == PlaceSource::Ground);
        CHECK_NEAR(p.pos.y, 3, 1e-5);
    }
}

void RayBox() {
    RUN_SUITE("ray against a box");
    float t = 0;
    const P3 lo{-1, -1, -1}, hi{1, 1, 1};
    CHECK(RayBoxEntry({0, 0, 10}, {0, 0, -1}, lo, hi, t)); CHECK_NEAR(t, 9, 1e-5);       // straight at the front face
    CHECK(RayBoxEntry({0, 10, 0}, {0, -2, 0}, lo, hi, t)); CHECK_NEAR(t, 4.5, 1e-5);      // direction need not be unit: t is in its units
    CHECK(!RayBoxEntry({3, 0, 10}, {0, 0, -1}, lo, hi, t));                               // passes beside it
    CHECK(!RayBoxEntry({0, 0, 10}, {0, 0, 1}, lo, hi, t));                                // points away
    CHECK(!RayBoxEntry({0, 0, 0}, {0, 0, -1}, lo, hi, t));                                // starts inside
    CHECK(RayBoxEntry({5, 0.5f, 0}, {-1, 0, 0}, lo, hi, t)); CHECK_NEAR(t, 4, 1e-5);      // parallel-axis components are fine
    CHECK(!RayBoxEntry({5, 2, 0}, {-1, 0, 0}, lo, hi, t));                                // parallel to a face but outside its slab
    CHECK(RayBoxEntry({-5, -5, -5}, {1, 1, 1}, lo, hi, t)); CHECK_NEAR(t, 4, 1e-5);       // a corner hit
}

void Snapping() {
    RUN_SUITE("snapping");
    CHECK_NEAR(SnapValue(1.4f, 1.0f, true), 1.0, 1e-5);
    CHECK_NEAR(SnapValue(1.6f, 1.0f, true), 2.0, 1e-5);
    CHECK_NEAR(SnapValue(-1.4f, 0.5f, true), -1.5, 1e-5);
    CHECK_NEAR(SnapValue(1.4f, 1.0f, false), 1.4, 1e-5);   // snapping off: untouched
    CHECK_NEAR(SnapValue(1.4f, 0.0f, true), 1.4, 1e-5);    // a zero step cannot divide
    CHECK_NEAR(SnapValue(1.4f, -1.0f, true), 1.4, 1e-5);
    // A surface hit keeps its height (so the object still sits on the surface); the ground snaps x and z only.
    Placement s{{1.4f, 2.3f, 3.6f}, PlaceSource::Surface};
    Placement ss = SnapPlacement(s, 1.0f, true);
    CHECK_NEAR(ss.pos.x, 1, 1e-5); CHECK_NEAR(ss.pos.y, 2.3, 1e-5); CHECK_NEAR(ss.pos.z, 4, 1e-5);
    Placement f{{1.4f, 2.3f, 3.6f}, PlaceSource::InFront};
    Placement sf = SnapPlacement(f, 1.0f, true);
    CHECK_NEAR(sf.pos.y, 2, 1e-5);                          // in the air there is no surface: all three axes snap
    Placement off = SnapPlacement(f, 1.0f, false);
    CHECK_NEAR(off.pos.x, 1.4, 1e-5); CHECK_NEAR(off.pos.y, 2.3, 1e-5);
    CHECK(sf.source == PlaceSource::InFront);               // the source is kept
}

void Placement2D() {
    RUN_SUITE("placement in 2D");
    Placement p = PlaceIn2D(3.2f, -1.7f);
    CHECK(p.source == PlaceSource::World2D);
    CHECK_NEAR(p.pos.x, 3.2, 1e-5); CHECK_NEAR(p.pos.y, -1.7, 1e-5); CHECK_NEAR(p.pos.z, 0, 1e-5);
    Placement q = SnapPlacement(p, 0.5f, true);
    CHECK_NEAR(q.pos.x, 3.0, 1e-5); CHECK_NEAR(q.pos.y, -1.5, 1e-5); CHECK_NEAR(q.pos.z, 0, 1e-5);   // both axes snap in 2D
    Placement r = SnapPlacement(p, 0.5f, false);
    CHECK_NEAR(r.pos.x, 3.2, 1e-5);
}

void LiftAboveSurface() {
    RUN_SUITE("resting on the surface");
    // An object whose pivot is in its middle (box half-height 0.5) is lifted so it sits ON a surface or the ground.
    CHECK_NEAR(RestOffsetY(PlaceSource::Surface, -0.5f), 0.5, 1e-5);
    CHECK_NEAR(RestOffsetY(PlaceSource::Ground, -0.5f), 0.5, 1e-5);
    CHECK_NEAR(RestOffsetY(PlaceSource::InFront, -0.5f), 0.0, 1e-5);   // in the air: the pivot is where you clicked
    CHECK_NEAR(RestOffsetY(PlaceSource::World2D, -0.5f), 0.0, 1e-5);
    CHECK_NEAR(RestOffsetY(PlaceSource::Ground, 0.0f), 0.0, 1e-5);     // pivot at the feet already
    CHECK_NEAR(RestOffsetY(PlaceSource::Ground, 0.3f), 0.0, 1e-5);     // bounds above the pivot: never push it down
}

void Menu() {
    RUN_SUITE("menu model");
    MenuState s3;
    s3.is2D = false;
    const std::vector<Entry> m = BuildMenu(s3);
    CHECK(SeparatorsOk(m));
    CHECK(DisabledHaveReasons(m));
    // Every group the owner asked for, in plain words.
    CHECK(HasId(m, "create.3d"));       // Shapes
    CHECK(HasId(m, "create.light"));
    CHECK(HasId(m, "create.camera"));
    CHECK(HasId(m, "create.player"));
    CHECK(HasId(m, "create.ui"));
    CHECK(HasId(m, "create.audio"));
    CHECK(HasId(m, "create.effects"));
    CHECK(HasId(m, kIdEmpty));
    CHECK(HasId(m, kIdPrefabs));
    // The existing Hierarchy create slots are reused, not copied: every create.* id is one DrawCreateSlot already knows.
    {
        const std::set<std::string> known = {"create.2d", "create.3d", "create.player", "create.light", "create.camera",
                                             "create.audio", "create.effects", "create.ui"};
        std::vector<const Entry*> stack;
        for (const Entry& e : m) stack.push_back(&e);
        while (!stack.empty()) {
            const Entry* e = stack.back(); stack.pop_back();
            for (const Entry& c : e->children) stack.push_back(&c);
            if (e->id.rfind("create.", 0) == 0) CHECK(known.count(e->id) == 1);
        }
    }
    // In a 3D scene the 3D shapes come first; no 2D group.
    CHECK(!HasId(m, "create.2d"));
    // Every top-level group has a plain name.
    for (const Entry& e : m) {
        if (e.kind == Kind::Separator || e.kind == Kind::Heading) continue;
        CHECK(!e.label.empty());
    }
    // Nothing under the cursor: no Duplicate / Delete / Focus / Rename.
    CHECK(!HasId(m, kIdDuplicate));
    CHECK(!HasId(m, kIdDelete));
    CHECK(!HasId(m, kIdFocus));
    CHECK(!HasId(m, kIdRename));

    MenuState s2;
    s2.is2D = true;
    const std::vector<Entry> m2 = BuildMenu(s2);
    CHECK(HasId(m2, "create.2d"));
    CHECK(!HasId(m2, "create.3d"));
    CHECK(HasId(m2, "create.light"));
    CHECK(SeparatorsOk(m2));

    // An object under the cursor adds its own actions, named after it.
    MenuState so = s3;
    so.hasObjectUnder = true;
    so.underName = "Crate";
    const std::vector<Entry> mo = BuildMenu(so);
    CHECK(SeparatorsOk(mo));
    CHECK(DisabledHaveReasons(mo));
    CHECK(HasId(mo, kIdDuplicate));
    CHECK(HasId(mo, kIdDelete));
    CHECK(HasId(mo, kIdFocus));
    CHECK(HasId(mo, kIdRename));
    CHECK(HasId(mo, kIdCopy));
    CHECK(HasId(mo, kIdPaste));
    {
        const Entry* paste = FindById(mo, kIdPaste);
        CHECK(paste && !paste->enabled && !paste->reason.empty());   // nothing copied yet
        const Entry* del = FindById(mo, kIdDelete);
        CHECK(del && del->label.find("Crate") != std::string::npos);
    }
    so.clipboardHasObject = true;
    {
        const std::vector<Entry> mp = BuildMenu(so);
        const Entry* paste = FindById(mp, kIdPaste);
        CHECK(paste && paste->enabled);
    }
    // Paste is also offered on empty space (it goes where you clicked) once something is copied.
    MenuState sp = s3; sp.clipboardHasObject = true;
    CHECK(HasId(BuildMenu(sp), kIdPaste));
    // Every id is unique (the host dispatches on it).
    {
        std::set<std::string> ids;
        std::vector<const Entry*> st;
        for (const Entry& e : mo) st.push_back(&e);
        while (!st.empty()) {
            const Entry* e = st.back(); st.pop_back();
            for (const Entry& c : e->children) st.push_back(&c);
            if (!e->id.empty()) CHECK(ids.insert(e->id).second);
        }
    }
}

void Prefabs() {
    RUN_SUITE("prefab list");
    MenuState s;
    // No prefabs: the submenu exists but says why it is empty.
    {
        const std::vector<Entry> m = BuildMenu(s);
        const Entry* pf = FindById(m, kIdPrefabs);
        CHECK(pf != nullptr);
        CHECK(pf && !pf->children.empty());
        CHECK(pf && !pf->children[0].enabled && !pf->children[0].reason.empty());
    }
    s.prefabs = {"Enemies/Goblin.okayprefab", "Props/Chest.okayprefab", "Tree.okayprefab"};
    {
        const std::vector<Entry> m = BuildMenu(s);
        const Entry* pf = FindById(m, kIdPrefabs);
        CHECK(pf && pf->children.size() == 3);
        if (pf && pf->children.size() == 3) {
            CHECK(pf->children[0].id == std::string(kIdPrefabPrefix) + "Enemies/Goblin.okayprefab");
            CHECK(pf->children[0].label == "Goblin");      // the file name without the folder or .okayprefab
            CHECK(PrefabPathFromId(pf->children[0].id) == "Enemies/Goblin.okayprefab");
            CHECK(PrefabPathFromId("scene.dup").empty());  // not a prefab id
        }
    }
    // A long list is cut so the menu stays usable; the search finds the rest.
    {
        MenuState big;
        for (int i = 0; i < 100; ++i) big.prefabs.push_back("P" + std::to_string(i) + ".okayprefab");
        const std::vector<Entry> m = BuildMenu(big);
        const Entry* pf = FindById(m, kIdPrefabs);
        CHECK(pf && (int)pf->children.size() <= kMaxPrefabsInMenu + 2);
        const std::vector<SearchItem> r = Search("P99", false, big.prefabs);
        CHECK(ContainsLabel(r, "P99"));
    }
}

void SearchBox() {
    RUN_SUITE("search");
    const std::vector<std::string> none;
    CHECK(Search("", false, none).empty());                 // nothing typed: no list (the groups are shown instead)
    CHECK(Search("   ", false, none).empty());
    {
        const auto r = Search("sphere", false, none);
        CHECK(!r.empty());
        CHECK(r.front().label == "Sphere");                 // the exact name comes first, before Hemisphere / Icosphere
        CHECK(ContainsLabel(r, "Icosphere"));
        CHECK(r.front().id == "mesh:Sphere");
    }
    CHECK(!Search("SPHERE", false, none).empty());          // any case
    CHECK(Search("  cube ", false, none).front().label == "Cube");   // spaces around are ignored
    CHECK(ContainsLabel(Search("point", false, none), "Point Light"));
    CHECK(ContainsLabel(Search("light", false, none), "Spot Light"));
    CHECK(ContainsLabel(Search("light", false, none), "Directional Light"));
    CHECK(ContainsLabel(Search("cam", false, none), "Camera"));
    CHECK(ContainsLabel(Search("empty", false, none), "Empty Object"));
    CHECK(ContainsLabel(Search("tree", false, none), "Tree"));
    CHECK(ContainsLabel(Search("palm", false, none), "PalmTree"));
    CHECK(ContainsLabel(Search("water", false, none), "Water"));
    CHECK(ContainsLabel(Search("terrain", false, none), "Terrain"));
    CHECK(ContainsLabel(Search("sound", false, none), "Audio Source"));      // a keyword, not only the name
    CHECK(ContainsLabel(Search("particle", false, none), "Particle System"));
    CHECK(ContainsLabel(Search("fire", false, none), "Particle System") || ContainsLabel(Search("fire", false, none), "Campfire"));
    CHECK(ContainsLabel(Search("player", false, none), "Player Controller"));
    CHECK(Search("zzzqqq", false, none).empty());
    // Words match in any order and all must match.
    CHECK(ContainsLabel(Search("light spot", false, none), "Spot Light"));
    CHECK(!ContainsLabel(Search("light spot", false, none), "Point Light"));
    // 2D scenes: the sprite shapes are offered, the 3D-only things are not.
    {
        const auto r2 = Search("sprite", true, none);
        CHECK(!r2.empty());
        CHECK(!ContainsLabel(Search("cube", true, none), "Cube"));
        CHECK(!Search("cube", false, none).empty());
    }
    // Prefabs are found by their file name and carry the group.
    {
        const std::vector<std::string> pf = {"Enemies/Goblin.okayprefab"};
        const auto r = Search("gob", false, pf);
        CHECK(!r.empty());
        if (!r.empty()) {
            CHECK(r[0].label == "Goblin");
            CHECK(r[0].id == std::string(kIdPrefabPrefix) + "Enemies/Goblin.okayprefab");
            CHECK(r[0].group == "Prefabs");
        }
    }
    // Every catalog item has a unique id, a label, a group and (for a mesh) a real mesh name.
    {
        std::set<std::string> ids;
        for (const SearchItem& it : Catalog(false)) {
            CHECK(!it.label.empty()); CHECK(!it.group.empty());
            CHECK(ids.insert(it.id).second);
        }
        for (const SearchItem& it : Catalog(true)) { CHECK(!it.label.empty()); }
    }
    // The result list is capped so a one-letter search does not build hundreds of rows.
    CHECK((int)Search("e", false, none).size() <= kMaxSearchResults);
    // Selection helpers: Enter picks the highlighted row; Up and Down wrap.
    CHECK(MoveSelection(0, +1, 5) == 1);
    CHECK(MoveSelection(4, +1, 5) == 0);
    CHECK(MoveSelection(0, -1, 5) == 4);
    CHECK(MoveSelection(0, +1, 0) == 0);
    CHECK(MoveSelection(7, 0, 3) == 2);                     // clamped when the list shrank
}

void Tables() {
    RUN_SUITE("shape tables");
    // The same tables the Hierarchy create menu draws: every primitive has a label and a mesh name, no duplicates.
    std::set<std::string> labels;
    for (const MeshEntry& e : kPrimitives) { CHECK(e.label && e.mesh); CHECK(labels.insert(e.label).second); }
    for (const MeshEntry& e : kNature)     { CHECK(labels.insert(e.label).second); }
    for (const MeshEntry& e : kProps)      { CHECK(labels.insert(e.label).second); }
    for (const MeshEntry& e : kStructures) { CHECK(labels.insert(e.label).second); }
    CHECK(labels.count("Cube") == 1);
    CHECK(labels.count("Sphere") == 1);
    CHECK(labels.count("Plane") == 1);
    CHECK(labels.count("Cylinder") == 1);
    CHECK(labels.count("Capsule") == 1);
}

}  // namespace

int main() {
    ClickVersusDrag();
    Placement3D();
    RayBox();
    Snapping();
    Placement2D();
    LiftAboveSurface();
    Menu();
    Prefabs();
    SearchBox();
    Tables();
    TEST_MAIN_RESULT();
}
