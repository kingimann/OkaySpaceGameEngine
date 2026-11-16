// SceneMerge on a scene whose base has no object ids (issue #888, finding 44).
//
// The loader gives every id-less object a fresh random guid on each load, so two people
// who open and save the same id-less file always write different guid lines. Those added
// guids are not edits, and a pure rename on one side must still pair with its base object
// (and keep its children) instead of becoming a deletion plus an addition.
#include "scene_text_testkit.hpp"
#include "okay/Scene/SceneMerge.hpp"

#include <fstream>
#include <functional>
#include <map>

using namespace okay;
using namespace tk;

using Kind = SceneMerge::Conflict::Kind;

static std::string ReadExample() {
    std::ifstream f(std::string(OKAY_SOURCE_DIR) + "/examples/physics/step_and_parent.okayscene", std::ios::binary);
    std::string s((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return s;
}

// Load `text` with the real loader, change the scene, write it back out: what the editor's
// open, edit, Ctrl+S does.
static std::string Edited(const std::string& text, const std::function<void(Scene&)>& edit) {
    Scene s("edit");
    std::string err;
    std::vector<std::string> warn;
    if (!SceneSerializer::Deserialize(s, text, &err, &warn)) return std::string();
    edit(s);
    s.FlushDestroyed();
    return SceneSerializer::Serialize(s);
}

struct Loaded {
    bool ok = false;
    int count = 0;
    std::map<std::string, int> names;              // name -> how many objects carry it
    std::map<std::string, std::string> parentOf;   // name -> parent's name ("" for a root)
    std::map<std::string, float> x;                // name -> local position x
};

static Loaded Load(const std::string& text) {
    Loaded l;
    Scene s("probe");
    std::string err;
    std::vector<std::string> warn;
    if (!SceneSerializer::Deserialize(s, text, &err, &warn)) return l;
    l.ok = true;
    SceneText::Model m;
    SceneText::BuildModel(SceneText::Canonicalize(text), m);
    l.count = (int)m.objects.size();
    for (const auto& o : m.objects) {
        ++l.names[o.name];
        l.parentOf[o.name] = o.parent >= 0 ? m.objects[(std::size_t)o.parent].name : std::string();
        if (GameObject* g = s.FindByPath(o.path)) l.x[o.name] = g->transform->localPosition.x;
    }
    return l;
}

static int Count(const SceneMerge::Result& r, Kind k) {
    int n = 0;
    for (const auto& c : r.conflicts) if (c.kind == k) ++n;
    return n;
}

static bool WarnsAboutIds(const SceneMerge::Result& r) {
    for (const auto& w : r.warnings) if (w.find("different ids") != std::string::npos) return true;
    return false;
}

static void Print(const char* label, const SceneMerge::Result& r) {
    Loaded l = Load(r.text);
    std::cout << "  " << label << ": ok=" << r.ok << " fellBack=" << r.fellBack << " objects=" << l.count
              << " conflicts=" << r.conflicts.size() << " warnings=" << r.warnings.size() << "\n   ";
    for (const auto& kv : l.names) std::cout << " " << kv.first << (kv.second > 1 ? "x" + std::to_string(kv.second) : "");
    std::cout << "\n";
    for (const auto& c : r.conflicts) std::cout << "    conflict " << c.KindName() << " " << c.object << "\n";
    for (const auto& w : r.warnings) std::cout << "    warn " << w << "\n";
}

static void MoveX(Scene& s, const char* name, float dx) {
    if (GameObject* g = s.Find(name)) g->transform->localPosition.x += dx;
}
static void Rename(Scene& s, const char* name, const char* to) {
    if (GameObject* g = s.Find(name)) g->name = to;
}

// The renamed scene, checked: every base object once, Platform renamed with both its children.
static void CheckRenamed(const SceneMerge::Result& r, float crateX, float platformX) {
    Loaded l = Load(r.text);
    CHECK(r.ok && !r.fellBack);
    CHECK(l.ok);
    CHECK((l.count) == (8));
    CHECK(((int)r.conflicts.size()) == (0));
    CHECK(l.names.count("Platform") == 0);
    CHECK(l.names["Platform_renamed"] == 1);
    for (const char* n : {"Floor", "Crate", "Rider", "Floor Mesh", "Platform Mesh", "Directional Light", "Main Camera"})
        CHECK(l.names[n] == 1);
    CHECK(l.parentOf["Rider"] == "Platform_renamed");
    CHECK(l.parentOf["Platform Mesh"] == "Platform_renamed");
    CHECK(l.parentOf["Floor Mesh"] == "Floor");
    CHECK(l.x.count("Crate") && l.x["Crate"] == crateX);
    CHECK(l.x.count("Platform_renamed") && l.x["Platform_renamed"] == platformX);
}

static void TestGrokCase() {
    RUN_SUITE("id-less base: a move on one side, a rename on the other");
    const std::string base = ReadExample();
    CHECK(!base.empty());
    CHECK(base.find("guid ") == std::string::npos);
    const std::string ours = Edited(base, [](Scene& s) { MoveX(s, "Crate", 5); });
    const std::string theirs = Edited(base, [](Scene& s) { Rename(s, "Platform", "Platform_renamed"); });
    CHECK(ours.find("guid ") != std::string::npos);                 // the loader added ids
    const auto r = SceneMerge::Merge(base, ours, theirs);
    Print("merge(base, move, rename)", r);
    CheckRenamed(r, 5.0f, 0.0f);
    CHECK(!WarnsAboutIds(r));
    // The other way round gives the same scene.
    const auto s = SceneMerge::Merge(base, theirs, ours);
    Print("merge(base, rename, move)", s);
    CheckRenamed(s, 5.0f, 0.0f);
    CHECK(!WarnsAboutIds(s));
    CHECK(StripGuids(r.text) == StripGuids(s.text));
    // The merged guid is the preferred side's (ours by default), so the result is deterministic.
    SceneText::Model mr, mo;
    SceneText::BuildModel(r.text, mr);
    SceneText::BuildModel(SceneText::Canonicalize(ours), mo);
    int same = 0;
    for (const auto& a : mr.objects) for (const auto& b : mo.objects) if (!a.guid.empty() && a.guid == b.guid) ++same;
    CHECK((same) == (8));
}

static void TestRealDeleteStillConflicts() {
    RUN_SUITE("id-less base: a real deletion against a real edit still conflicts");
    const std::string base = ReadExample();
    const std::string ours = Edited(base, [](Scene& s) { if (GameObject* g = s.Find("Crate")) s.DestroyImmediate(g); });
    const std::string theirs = Edited(base, [](Scene& s) { MoveX(s, "Crate", 2); });
    const auto r = SceneMerge::Merge(base, ours, theirs);
    Print("merge(base, delete Crate, move Crate)", r);
    CHECK(r.ok);
    CHECK((Count(r, Kind::ObjectDeletedAndEdited)) == (1));
    CHECK(((int)r.conflicts.size()) == (1));
    // A deletion against a side that only re-saved (and so only added ids) is not a conflict.
    // The shipped example is not in the current writer's layout (a re-save reorders its
    // components and adds fields, a real change of Crate's records), so this uses the writer's
    // own text with the ids taken out: what an older build's save of the scene looks like.
    std::string written;
    CHECK(Reserialize(base, &written));
    const std::string idless = StripGuids(written);
    CHECK(idless.find("guid ") == std::string::npos);
    const std::string ours2 = Edited(idless, [](Scene& s) { if (GameObject* g = s.Find("Crate")) s.DestroyImmediate(g); });
    const std::string resaved = Edited(idless, [](Scene&) {});
    const auto d = SceneMerge::Merge(idless, ours2, resaved);
    Print("merge(base, delete Crate, re-save)", d);
    Loaded l = Load(d.text);
    CHECK(d.ok && d.conflicts.empty());
    CHECK((l.count) == (7));
    CHECK(l.names.count("Crate") == 0);
    CHECK(!WarnsAboutIds(d));
}

static void TestRenameAgainstContentEdit() {
    RUN_SUITE("id-less base: a rename on one side, a content edit of the same object on the other");
    const std::string base = ReadExample();
    const std::string ours = Edited(base, [](Scene& s) { MoveX(s, "Platform", 3); });
    const std::string theirs = Edited(base, [](Scene& s) { Rename(s, "Platform", "Platform_renamed"); });
    const auto r = SceneMerge::Merge(base, ours, theirs);
    Print("merge(base, move Platform, rename Platform)", r);
    CheckRenamed(r, 0.0f, 3.0f);
    const auto s = SceneMerge::Merge(base, theirs, ours);
    Print("merge(base, rename Platform, move Platform)", s);
    CheckRenamed(s, 0.0f, 3.0f);
}

static void TestBaseWithIds() {
    RUN_SUITE("base with ids: the same edits merge as before");
    std::string base;
    CHECK(Reserialize(ReadExample(), &base));
    CHECK(base.find("guid ") != std::string::npos);
    const std::string ours = Edited(base, [](Scene& s) { MoveX(s, "Crate", 5); });
    const std::string theirs = Edited(base, [](Scene& s) { Rename(s, "Platform", "Platform_renamed"); });
    const auto r = SceneMerge::Merge(base, ours, theirs);
    Print("merge(base with ids, move, rename)", r);
    CheckRenamed(r, 5.0f, 0.0f);
    CHECK(r.warnings.empty());
    SceneText::Model mb, mr;
    SceneText::BuildModel(SceneText::Canonicalize(base), mb);
    SceneText::BuildModel(r.text, mr);
    int same = 0;
    for (const auto& a : mr.objects) for (const auto& b : mb.objects) if (a.guid == b.guid) ++same;
    CHECK((same) == (8));                                               // every id kept
}

int main() {
    TestGrokCase();
    TestRealDeleteStillConflicts();
    TestRenameAgainstContentEdit();
    TestBaseWithIds();
    TEST_MAIN_RESULT();
}
