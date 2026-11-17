// The beginner layer of the Modeling window (editor/ModelingEasy.hpp): plain names, no jargon, hints, quick actions,
// recent tools and the click-count table. Pure logic, no ImGui.
#include "ModelingEasy.hpp"
#include <cstdio>
#include <set>
using namespace oked::easymodel;
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while (0)

static int Words(const std::string& s) { int n = 0; bool in = false; for (char c : s) { if (c == ' ') in = false; else if (!in) { in = true; ++n; } } return n; }

int main() {
    // Catalogue shape.
    std::set<std::string> ids;
    static const char* verbs[] = {"Add", "Move", "Make", "Turn", "Squash", "Smooth", "Copy", "Cut", "Round", "Push", "Delete", "Paint", "Fix", "Put", "Save"};
    for (const Tool& t : Tools()) {
        CHECK(ids.insert(t.id).second);
        const int w = Words(t.name);
        CHECK(w >= 1 && w <= 4);
        bool verb = false; for (const char* v : verbs) if (std::string(t.name).rfind(v, 0) == 0) verb = true;
        CHECK(verb);
        CHECK(std::string(t.tip).size() > 10 && std::string(t.example).size() > 5);
        CHECK(t.tab != Tab::Advanced);
        CHECK(TooltipFor(t).find("For example") != std::string::npos);
    }
    for (int i = 0; i < kBeginnerTabCount; ++i) CHECK(!ToolsOnTab((Tab)i).empty());
    CHECK(kBeginnerTabCount <= 5);
    CHECK(std::string(TabName(Tab::Shape)) == "Shape" && std::string(TabName(Tab::Finish)) == "Finish");

    // The jargon check: names, tips, examples, tab text, hints, errors, reports.
    for (const Tool& t : Tools()) {
        CHECK(FindBannedWord(t.name).empty());
        CHECK(FindBannedWord(t.tip).empty());
        CHECK(FindBannedWord(t.example).empty());
        if (!FindBannedWord(t.tip).empty()) std::printf("  jargon '%s' in tip of %s\n", FindBannedWord(t.tip).c_str(), t.id);
    }
    for (int i = 0; i <= kBeginnerTabCount; ++i) CHECK(FindBannedWord(TabHelp((Tab)i)).empty());
    CHECK(FindBannedWord("Extrude the vertex").size() > 0);
    CHECK(FindBannedWord("Normals are flipped") == "normals");
    CHECK(FindBannedWord("a mesh").size() > 0);
    CHECK(FindBannedWord("extruded").empty());      // whole words only
    CHECK(FindBannedWord("Push out a face").empty());

    // Hints follow the state.
    EditState s;
    CHECK(NextHint(s) == "Pick a shape to start");
    s.haveObject = true;
    CHECK(NextHint(s).find("Tab") != std::string::npos);
    s.editMode = true; s.selMode = 2;
    CHECK(NextHint(s).find("Click a face") == 0);
    s.selMode = 0; CHECK(NextHint(s).find("Click a corner") == 0);
    s.selMode = 2; s.selCount = 1;
    CHECK(NextHint(s).find("Drag the arrows to move it") == 0);
    CHECK(NextHint(s).find("Press Tab to leave edit mode") != std::string::npos);
    s.selCount = 4; CHECK(NextHint(s).find("all faces") != std::string::npos);
    s.modalActive = true; CHECK(NextHint(s).find("Esc") != std::string::npos);
    EditState hs[3]; hs[0].haveObject = true; hs[1] = hs[0]; hs[1].editMode = true; hs[2] = hs[1]; hs[2].selCount = 2;
    for (const EditState& e : hs) CHECK(FindBannedWord(NextHint(e)).empty());

    // Greyed-out tools say why, in plain words.
    EditState none;
    CHECK(WhyNot(*FindTool("add.ball"), none).empty());               // adding needs nothing
    CHECK(WhyNot(*FindTool("smooth"), none).find("Pick a shape first") == 0);
    EditState obj; obj.haveObject = true;
    CHECK(WhyNot(*FindTool("smooth"), obj).empty());
    CHECK(WhyNot(*FindTool("push.out"), obj).find("Select at least one face first") == 0);
    EditState face = obj; face.editMode = true; face.selMode = 2; face.selCount = 2;
    CHECK(WhyNot(*FindTool("push.out"), face).empty());
    CHECK(WhyNot(*FindTool("round.corners"), face).find("corner") != std::string::npos);
    for (const Tool& t : Tools()) for (const EditState* e : {&none, &obj, &face}) CHECK(FindBannedWord(WhyNot(t, *e)).empty());

    // Quick actions: at most six, all real tools, matched to what is selected, and cheap (no mesh is read).
    CHECK(QuickActions(none).empty());
    auto qa = QuickActions(face);
    CHECK(qa.size() == 6 && qa[0] == "push.out");
    EditState corner = obj; corner.editMode = true; corner.selMode = 0; corner.selCount = 1;
    CHECK(QuickActions(corner)[0] == "move");
    CHECK(QuickActions(obj)[0] == "bigger" && QuickActions(obj).size() == 6);
    for (const EditState* e : {&obj, &face, &corner}) for (const std::string& id : QuickActions(*e)) {
        CHECK(FindTool(id) != nullptr);
        CHECK(WhyNot(*FindTool(id), *e).empty());   // never offers something that is greyed out
    }

    // Recent strip: newest first, no repeats, four at most, survives saving.
    std::vector<std::string> r;
    for (const char* id : {"smooth", "mirror", "hollow", "round.it", "smooth", "bigger"}) NoteRecent(r, id);
    CHECK(r.size() == 4 && r[0] == "bigger" && r[1] == "smooth" && r[2] == "round.it" && r[3] == "hollow");
    CHECK(RepeatId(r) == "bigger");
    CHECK(RecentFromText(RecentToText(r)) == r);
    CHECK(RecentFromText("nonsense,smooth,smooth,,mirror") == (std::vector<std::string>{"smooth", "mirror"}));   // unknown ids dropped
    CHECK(RepeatId({}).empty());

    // Plain-words reports.
    CHECK(SizeLine(1, 2, 0.5f) == "1.0 wide, 2.0 tall, 0.5 deep");
    CHECK(FixReport(0, 0, 0).find("No problems") == 0);
    CHECK(FixReport(3, 0, 2) == "Fixed: 3 gaps closed, 2 loose corners joined");
    CHECK(FixReport(1, 1, 0).find("1 gap left") != std::string::npos);
    CHECK(FindBannedWord(FixReport(3, 1, 1)).empty());

    // Fewer clicks: every one of the eight jobs at least halves.
    CHECK(Jobs().size() == 8);
    int b = 0, a = 0;
    for (const Job& j : Jobs()) {
        CHECK(!j.after.empty());
        CHECK(j.after.size() * 2 <= j.before.size());
        if (j.after.size() * 2 > j.before.size()) std::printf("  job not halved: %s\n", j.name);
        b += (int)j.before.size(); a += (int)j.after.size();
        for (const char* st : j.after) if (std::string(st).find('.') != std::string::npos && std::string(st).find(' ') == std::string::npos) CHECK(FindTool(st) != nullptr);
    }
    std::printf("clicks, all 8 jobs: before %d, after %d\n", b, a);

    EasyPrefs p; CHECK(!p.showAdvanced && p.mirrorOnNew);   // beginner mode by default
    if (fails) { std::printf("%d failure(s)\n", fails); return 1; }
    std::printf("modeling_easy: all passed\n");
    return 0;
}
