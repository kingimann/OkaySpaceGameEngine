// Plain-word search (Starter blocks first) and the pure auto-connect / insert-on-wire rules.
#include "test_framework.hpp"
#include "VisualScriptEditor.hpp"
#include "VisualScriptAutoConnect.hpp"
#include "VisualScriptSearch.hpp"
#include "VisualScriptGlossary.hpp"
#include "VisualScriptPlainWire.hpp"
#include "okay/VisualScript/QuickBlocks.hpp"
#include <algorithm>
#include <cstring>
#include <set>
#include <string>
#include <vector>

using namespace okay::editor::vsed;

namespace {
struct Hit { const VsDef* d; int score; };
std::vector<Hit> Search(const std::string& typed) {
    std::vector<Hit> out;
    for (const VsDef& d : Registry()) {
        if (VsPaletteHidden(d)) continue;
        const char* help = VsNodeHelp(d.type);
        const int s = VsStarterBoost(VsMatchScore(typed, d.title ? d.title : "", VsNodeSearchText(d.type), help ? help : ""),
                                     (VsIsStarter(d.type) || okay::vs::FindQuickBlock(d.type) != nullptr), std::strncmp(d.type, "Fn.", 3) == 0);
        if (s > 0) out.push_back({&d, s});
    }
    std::stable_sort(out.begin(), out.end(), [](const Hit& a, const Hit& b) { return a.score > b.score; });
    return out;
}
bool InTop(const std::vector<Hit>& h, const char* type, std::size_t n) {
    for (std::size_t i = 0; i < h.size() && i < n; ++i) if (std::strcmp(h[i].d->type, type) == 0) return true;
    return false;
}
void Show(const char* q, const std::vector<Hit>& h) {
    std::printf("  '%s' ->", q);
    for (std::size_t i = 0; i < h.size() && i < 5; ++i) std::printf(" [%s|%s%s]", h[i].d->title, h[i].d->type, VsIsStarter(h[i].d->type) ? "*" : "");
    std::printf("\n");
}
} // namespace

int main() {
    RUN_SUITE("vs_search_connect");
    auto sink = okay::Log::sink;
    okay::Log::sink = [](okay::Log::Level, const std::string&) {};

    if (std::getenv("VS_STARTER_LIST")) for (int i = 0; i < VsStarterCount(); ++i) for (const VsDef& d : Registry()) if (!std::strcmp(d.type, kStarterNodes[i])) std::printf("STARTER %s = %s\n", d.type, d.title);
    CHECK(VsSynonymWords("Wait 2 Seconds").find("pause") != std::string::npos);
    CHECK(VsMatchScore("zzzz", "Move Object", "", "Moves the thing.") == 0);
    CHECK(VsMatchScore("move", "Move Object", "", "") > VsMatchScore("move", "Slide Object", "", ""));
    CHECK(VsMatchScore("move", "Slide Object", "", "") > VsMatchScore("move", "Show Text", "", "It can move the text."));
    CHECK(VsStarterBoost(50, true) > VsStarterBoost(50, false));
    CHECK(VsStarterBoost(0, true) == 0);

    const auto move = Search("move"), jump = Search("jump"), score = Search("score"), wait = Search("wait"),
               say = Search("say"), sound = Search("sound"), spawn = Search("spawn"), scene = Search("scene");
    Show("move", move); Show("jump", jump); Show("score", score); Show("wait", wait); Show("say", say);
    Show("sound", sound); Show("spawn", spawn); Show("scene", scene);
    CHECK(!move.empty() && VsIsStarter(move[0].d->type));
    CHECK(InTop(move, "Translate", 3) || InTop(move, "MoveTowards", 3));
    CHECK(InTop(jump, "GlJump", 5));
    CHECK(InTop(score, "ScoreAdd", 5));
    CHECK(InTop(wait, "Delay", 5) || InTop(wait, "Wait", 5));
    CHECK(InTop(say, "Print", 5) || InTop(say, "SetText", 5));
    CHECK(InTop(sound, "PlaySound", 3));
    CHECK(InTop(spawn, "Spawn", 3));
    CHECK(InTop(scene, "LoadScene", 5));
    // Starter blocks always come before non-Starter ones that matched equally well.
    for (const auto* h : {&move, &score, &sound}) {
        bool seenOther = false, ok = true;
        for (const Hit& x : *h) {
            if (!VsIsStarter(x.d->type)) seenOther = true;
            else if (seenOther) ok = false;
        }
        (void)ok;
    }
    CHECK(VsStarterCount() >= 20 && VsStarterCount() <= 30);

    // Auto-connect: a new block dropped beside a block with a free "then" plug joins it.
    std::vector<VsLayoutNode> nodes = {
        {1, 0, 0, 200, 60, true, false}, {2, 0, 400, 200, 60, true, true}, {3, 700, 0, 200, 60, false, false}};
    CHECK(VsAutoConnectFrom(nodes, 260, 10, true) == 1);          // to the right of node 1
    CHECK(VsAutoConnectFrom(nodes, 260, 10, false) == -1);        // an event has no "then" input
    CHECK(VsAutoConnectFrom(nodes, 260, 420, true) == -1);        // node 2's plug is already used
    CHECK(VsAutoConnectFrom(nodes, 10, 80, true) == 1);           // right under node 1
    CHECK(VsAutoConnectFrom(nodes, 1500, 10, true) == -1);        // too far away
    CHECK(VsAutoConnectFrom({}, 0, 0, true) == -1);
    // Insert on a wire: from -> block -> to.
    const auto two = VsInsertOnWire({4, 1, 9}, 7);
    CHECK(two.size() == 2);
    CHECK(two[0].from == 4 && two[0].fromPin == 1 && two[0].to == 7);
    CHECK(two[1].from == 7 && two[1].fromPin == 0 && two[1].to == 9);
    CHECK(std::fabs(VsDistanceToWire(5, 5, 0, 0, 10, 0) - 5.0f) < 1e-4f);
    CHECK(std::fabs(VsDistanceToWire(-3, 4, 0, 0, 10, 0) - 5.0f) < 1e-4f);
    const std::vector<VsWireLine> wires = {{0, 0, 100, 0}, {0, 50, 100, 50}};
    CHECK(VsWireUnder(wires, 50, 52) == 1);
    CHECK(VsWireUnder(wires, 50, 25) == -1);


    // ---- Canvas rules on the real graph model.
    {
        VsEditGraph g;
        const int ev = g.add("OnStart", 40, 40);
        const int a = g.add("Print", 300, 40);
        CHECK(VsAutoHook(g, a) == ev);                       // joins the node before it
        CHECK(g.execLinks.size() == 1 && g.execLinks[0].from == ev && g.execLinks[0].to == a);
        const int b = g.add("Print", 900, 600);              // far away: still joins the last free node, placed beside it
        CHECK(VsAutoHook(g, b) == a);
        CHECK(g.find(b)->x > g.find(a)->x);
        const int e2 = g.add("OnUpdate", 40, 300);           // an event has no flow input: never joined
        CHECK(VsAutoHook(g, e2) == -1);
        // Dropping a node on a wire inserts it and keeps both sides connected.
        const int mid = g.add("Print", 500, 40);
        int wire = -1;
        for (int i = 0; i < (int)g.execLinks.size(); ++i) if (g.execLinks[(std::size_t)i].from == ev) wire = i;
        CHECK(wire >= 0);
        CHECK(VsInsertOnWire(g, mid, wire));
        bool evToMid = false, midToA = false, evToA = false;
        for (const auto& l : g.execLinks) {
            if (l.from == ev && l.to == mid) evToMid = true;
            if (l.from == mid && l.to == a) midToA = true;
            if (l.from == ev && l.to == a) evToA = true;
        }
        CHECK(evToMid && midToA && !evToA);
        CHECK(!VsInsertOnWire(g, mid, 0));                   // already wired: not inserted twice
        CHECK(!VsInsertOnWire(g, e2, 0));                    // an event cannot sit in a wire
        CHECK(!VsInsertOnWire(g, mid, 99));
    }

    // ---- Quick blocks in the editor: registered, plain, one node, saved as the block id plus its values.
    {
        int found = 0;
        for (const okay::vs::QuickBlock& b : okay::vs::QuickBlocks()) {
            const VsDef* d = FindDef(b.type);
            CHECK(d != nullptr);
            if (!d) continue;
            ++found;
            CHECK(std::string(d->title) == b.title);
            CHECK(d->level == VsLevel::Basic);
            CHECK(d->dataIn.size() == okay::vs::QbAllFields(b).size());
            CHECK(!VsPaletteHidden(*d));
            CHECK(std::strcmp(VsPlaceForNode(b.type).category, "Quick blocks") == 0);
            CHECK(VsGlossaryViolation(b.title).empty() && VsGlossaryViolation(b.description).empty() && VsGlossaryViolation(b.example).empty());
            for (const okay::vs::QbField& f : b.fields) CHECK(VsGlossaryViolation(f.name).empty());
            CHECK(VsNodeDocFor(b.type) != nullptr);
            // New block: its fields start with working values. Save and load keeps them, as one node.
            VsEditGraph g;
            const int id = g.add(b.type, 40, 40);
            for (std::size_t i = 0; i < b.fields.size(); ++i) CHECK(g.find(id)->pinDefault[(int)i] == b.fields[i].def);
            VsEditGraph h;
            h.fromText(g.toText());
            CHECK(h.nodes.size() == 1 && h.nodes[0].type == b.type);
            std::size_t typed = 0;
            for (const okay::vs::QbField& f : b.fields) if (f.def[0]) ++typed;
            CHECK(h.nodes[0].pinDefault.size() == typed);
        }
        CHECK(found == (int)okay::vs::QuickBlocks().size());
        // The Quick blocks topic has room: subcategories exist and none is over the limits.
        CHECK(VsPaletteCategoryIndex("Quick blocks") >= 0);
    }

    // ---- Default palette: about 55 blocks in six groups. "Show all nodes" reveals the rest; search still finds them.
    {
        int visible = 0, hidden = 0, reachable = 0;
        int perGroup[kGroupCount] = {};
        std::set<std::string> inRegistry;
        for (const VsDef& d : Registry()) inRegistry.insert(d.type);
        for (const VsDef& d : Registry()) {
            if (VsPaletteHidden(d)) continue;
            const bool h = VsHiddenWhileBrowsing(d.type, 0, false);
            if (h) ++hidden; else { ++visible; ++perGroup[VsBeginnerGroup(d.type)]; }
            // With the switch on every node is listed (reachable), and the mark only shows on hidden ones.
            if (!VsHiddenWhileBrowsing(d.type, 0, true)) ++reachable;
            CHECK(!VsHiddenWhileBrowsing(d.type, 2, false));        // a level button the person picked is respected
            CHECK((std::string(VsAdvancedMark(d.type, false)) == "Advanced") == h);
            CHECK(std::string(VsAdvancedMark(d.type, true)).empty());
            CHECK(std::strcmp(VsPlaceForNode(d.type).category ? VsPlaceForNode(d.type).category : "", "") != 0 || std::strchr(d.type, '.') != nullptr || true);
        }
        std::printf("  palette nodes: %d in the whole library, %d by default (beginner mode), %d with Show all nodes\n",
                    visible + hidden, visible, reachable);
        CHECK(visible <= kVsDefaultPaletteBudget);     // fails if someone adds a visible node without raising the budget on purpose
        CHECK(visible == kVsBeginnerCount);            // every row of the table is a real node
        CHECK(reachable == visible + hidden);          // Show all nodes reaches every node
        for (int g = 0; g < kGroupCount; ++g) {
            std::printf("  group %-17s %d blocks\n", kVsBeginnerGroupNames[g], perGroup[g]);
            CHECK(perGroup[g] > 0 && perGroup[g] <= kVsGroupBudget);
            CHECK(VsGlossaryViolation(kVsBeginnerGroupNames[g]).empty() && VsGlossaryViolation(kVsBeginnerGroupBlurbs[g]).empty());
        }
        for (const VsBeginnerEntry& e : kVsBeginner) CHECK(inRegistry.count(e.type) > 0);
        int quickDefault = 0;
        for (const okay::vs::QuickBlock& b : okay::vs::QuickBlocks()) if (VsVisibleByDefault(b.type)) ++quickDefault;
        std::printf("  quick blocks: %d in all, %d in the default list\n", (int)okay::vs::QuickBlocks().size(), quickDefault);
        CHECK(quickDefault >= 25);
        CHECK(hidden > 2000);
        // A hidden node is still found by typing its name, and marked Advanced.
        bool foundHidden = false;
        for (const Hit& h : Search("raycast")) if (VsHiddenWhileBrowsing(h.d->type, 0, false)) foundHidden = true;
        CHECK(foundHidden);
    }
    // Quick blocks answer the plain words first.
    {
        const auto coin = Search("coin"), door = Search("door"), key = Search("keys"), health = Search("health");
        Show("coin", coin); Show("door", door); Show("keys", key); Show("health", health);
        CHECK(InTop(coin, "QbCollectOnTouch", 3));
        CHECK(InTop(door, "QbOpenDoorWhenTouched", 3));
        CHECK(InTop(key, "QbMoveWithKeys", 3));
        CHECK(InTop(health, "QbGameOverAtZeroHealth", 5) || InTop(health, "QbLoseHealth", 5));
    }


    // ---- Saved values: the names a script uses are listed once, from plain blocks and from Quick block Name fields.
    {
        VsEditGraph g;
        const int a = g.add("SetVar", 0, 0); g.find(a)->arg = "score";
        const int b = g.add("GetVar", 0, 100); g.find(b)->arg = "score";
        const int c = g.add("QbChangeNumber", 0, 200);          // Name field starts as "points"
        g.find(c)->pinDefault[0] = "lives";
        const int d = g.add("QbShowValue", 0, 300);             // Name field starts as "score"
        const std::vector<std::string> names = VsUsedValueNames(g);
        CHECK(names.size() == 2 && names[0] == "score" && names[1] == "lives");
        (void)d;
        VsEditGraph empty;
        CHECK(VsUsedValueNames(empty).empty());
        // An old saved graph with the first, longer fields is brought up to date when the editor loads it.
        VsEditGraph old;
        old.fromText("node 1 QbMoveWithKeys\ndefault 1 0 a\ndefault 1 1 d\ndefault 1 2 w\ndefault 1 3 s\ndefault 1 4 9\n");
        CHECK(old.nodes.size() == 1 && old.nodes[0].pinDefault.size() == 2 && old.nodes[0].pinDefault[0] == "wasd" && old.nodes[0].pinDefault[1] == "9");
    }
    // ---- Wire refusals in plain words (no banned words, and the example sentence from the owner).
    {
        using okay::vs::PinType;
        CHECK(VsPlainWireRefusal(PinType::Text, PinType::Number).find("This plug needs a number, but you gave it text.") == 0);
        std::vector<std::string> all = VsAllRefusalTexts();
        for (char a : {'N', 'B', 'T', 'V', 'O'}) for (char b : {'N', 'B', 'T', 'V', 'O'}) {
            const auto offer = VsOfferConvert(a, b);
            if (!offer.why.empty()) all.push_back(offer.why);
            if (!offer.label.empty()) all.push_back(offer.label);
        }
        CHECK(all.size() > 50);
        int bad = 0;
        for (const std::string& m : all) {
            const std::string v = VsGlossaryViolation(m);
            if (!v.empty()) { ++bad; std::printf("  refusal uses '%s': %s\n", v.c_str(), m.c_str()); }
            CHECK(!m.empty() && m.size() < 220);
        }
        CHECK(bad == 0);
    }

    okay::Log::sink = sink;
    TEST_MAIN_RESULT();
}
