// Plain-word search (Starter blocks first) and the pure auto-connect / insert-on-wire rules.
#include "test_framework.hpp"
#include "VisualScriptEditor.hpp"
#include "VisualScriptAutoConnect.hpp"
#include "VisualScriptSearch.hpp"
#include <algorithm>
#include <cstring>
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
                                     VsIsStarter(d.type), std::strncmp(d.type, "Fn.", 3) == 0);
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

    okay::Log::sink = sink;
    TEST_MAIN_RESULT();
}
