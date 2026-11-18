// Easier canvas use: auto-connect a new block, place it beside, split a wire, delete and rejoin, add a recipe.
// All on plain graphs (no window). Graph in, graph out.
#include "test_framework.hpp"
#include "VisualScriptEditor.hpp"
#include <set>
#include <string>

using namespace okay::editor::vsed;

namespace {
int ExecCount(const VsEditGraph& g, int from, int to) {
    int n = 0;
    for (const auto& l : g.execLinks) if (l.from == from && l.to == to) ++n;
    return n;
}
bool Overlaps(const VsEditGraph& g) {
    for (std::size_t i = 0; i < g.nodes.size(); ++i)
        for (std::size_t j = i + 1; j < g.nodes.size(); ++j)
            if (VsBoxesOverlap(VsEstBox(g, g.nodes[i]), VsEstBox(g, g.nodes[j]), 0.0f)) return true;
    return false;
}
} // namespace

int main() {
    RUN_SUITE("vs_easy_ops");
    auto sink = okay::Log::sink;
    okay::Log::sink = [](okay::Log::Level, const std::string&) {};

    // (a) A new block joins the selected one and sits beside it.
    {
        VsEditGraph g;
        const int ev = g.add("OnStart", 40, 40);
        const int sc = g.add("ScoreAdd", 900, 900);   // dropped far away, as if placed by the cursor
        const VsWireMade m = VsConnectAndPlace(g, sc, ev, -1);
        CHECK(m.exec && !m.data);
        CHECK(ExecCount(g, ev, sc) == 1);
        const VsEditNode* n = g.find(sc);
        CHECK(n && n->x > 40 + 150 && n->x < 500);   // moved next to the event, not left at 900
        CHECK(!Overlaps(g));
        // A second block after the first one goes after it, not on top of it.
        const int p = g.add("Print", 0, 0);
        const VsWireMade m2 = VsConnectAndPlace(g, p, sc, ev);
        CHECK(m2.exec && ExecCount(g, sc, p) == 1);
        CHECK(!Overlaps(g));
    }
    // The last-used block is the fallback when nothing is selected; with neither, nothing is connected.
    {
        VsEditGraph g;
        const int ev = g.add("OnStart", 40, 40);
        const int sc = g.add("ScoreAdd", 500, 500);
        CHECK(VsPickAnchor(g, -1, ev) == ev);
        CHECK(VsPickAnchor(g, 999, ev) == ev);
        CHECK(VsPickAnchor(g, sc, ev) == sc);
        const VsWireMade none = VsConnectAndPlace(g, sc, -1, -1);
        CHECK(!none.any());
        CHECK(g.find(sc)->x == 500);                  // a node that did not connect stays where it was put
    }
    // An event cannot be joined after something; a block whose "then" is taken is not joined either.
    {
        VsEditGraph g;
        const int a = g.add("ScoreAdd", 40, 40);
        const int ev = g.add("OnUpdate", 300, 40);
        CHECK(!VsAutoWire(g, a, ev).any());
        const int b = g.add("Print", 300, 200);
        const int c = g.add("Print", 300, 400);
        g.linkExec(a, 0, b);
        CHECK(!VsAutoWire(g, a, c).any());
    }
    // A Branch: the first free "then" plug (True), then False.
    {
        VsEditGraph g;
        const int br = g.add("Branch", 40, 40);
        const int a = g.add("Print", 0, 0), b = g.add("Print", 0, 0);
        CHECK(VsAutoWire(g, br, a).exec);
        CHECK(VsAutoWire(g, br, b).exec);
        bool t = false, f = false;
        for (const auto& l : g.execLinks) { if (l.to == a && l.pin == 0) t = true; if (l.to == b && l.pin == 1) f = true; }
        CHECK(t && f);
    }
    // Values: a value block feeds the step beside it, and a step takes a value from the block before it.
    {
        VsEditGraph g;
        const int tr = g.add("Translate", 300, 40);
        const int k = g.add("Const", 0, 0);
        const VsWireMade m = VsAutoWire(g, tr, k);
        CHECK(m.data && !m.exec);
        CHECK(g.dataLinks.size() == 1 && g.dataLinks[0].consumer == tr && g.dataLinks[0].producer == k);
        VsEditGraph h;
        const int add = h.add("Add", 40, 40);
        const int mul = h.add("Mul", 0, 0);
        CHECK(VsAutoWire(h, add, mul).data);          // Add's result goes into Mul's first input
        CHECK(h.dataLinks.size() == 1 && h.dataLinks[0].producer == add && h.dataLinks[0].consumer == mul);
    }
    // Placement never overlaps, even in a crowded column.
    {
        VsEditGraph g;
        const int ev = g.add("OnStart", 40, 40);
        for (int i = 0; i < 8; ++i) {
            const int id = g.add("Print", 0, 0);
            const auto p = VsPlaceBeside(g, ev, "Print", id);
            g.find(id)->x = p.first; g.find(id)->y = p.second;
        }
        CHECK(!Overlaps(g));
        CHECK(VsPlaceBeside(g, 12345, "Print").first == 40.0f);   // unknown anchor: the starting corner
    }

    // (b) A block dropped on a flow wire splits it, keeping the order of the wires.
    {
        VsEditGraph g;
        const int ev = g.add("OnStart", 40, 40);
        const int a = g.add("ScoreAdd", 300, 40), z = g.add("Print", 600, 40);
        g.linkExec(ev, 0, a);
        g.linkExec(a, 0, z);
        const int mid = g.add("Delay", 450, 200);
        CHECK(VsIsLoose(g, mid));
        CHECK(VsInsertOnExecWire(g, 1, mid));
        CHECK(g.execLinks.size() == 3);
        CHECK(ExecCount(g, a, mid) == 1 && ExecCount(g, mid, z) == 1 && ExecCount(g, a, z) == 0);
        CHECK(ExecCount(g, ev, a) == 1);
        CHECK(!VsIsLoose(g, mid));
        CHECK(!VsInsertOnExecWire(g, 7, mid));         // no such wire
        const int ev2 = g.add("OnUpdate", 0, 0);       // an event has no "then" input: refused, graph untouched
        const auto before = g.execLinks.size();
        CHECK(!VsInsertOnExecWire(g, 0, ev2) && g.execLinks.size() == before);
    }
    // A block dropped on a value wire splits it when the types fit; refuses when they do not.
    {
        VsEditGraph g;
        const int add = g.add("Add", 40, 40), tr = g.add("Translate", 400, 40);
        g.linkData(tr, 0, add, 0);
        const int mul = g.add("Mul", 200, 200);
        CHECK(VsInsertOnDataWire(g, 0, mul));
        CHECK(g.dataLinks.size() == 2);
        bool in = false, out = false;
        for (const auto& l : g.dataLinks) {
            if (l.consumer == mul && l.producer == add) in = true;
            if (l.consumer == tr && l.producer == mul) out = true;
        }
        CHECK(in && out);
        VsEditGraph h;
        const int cmp = h.add("FindObject", 40, 40), br = h.add("Teleport", 400, 40);
        h.linkData(br, 3, cmp, 0);
        const int mul2 = h.add("Mul", 200, 200);       // a number block cannot carry an object
        CHECK(!VsInsertOnDataWire(h, 0, mul2));
        CHECK(h.dataLinks.size() == 1 && h.dataLinks[0].producer == cmp);
    }
    // Finding the wire under a point, and naming a wire so it can be found again.
    {
        std::vector<VsWireEnds> w = {{true, 0, 100, 100, 300, 100}, {false, 0, 100, 300, 300, 300}};
        CHECK(VsNearestWire(w, 200, 104, 1.0f, 14.0f) == 0);
        CHECK(VsNearestWire(w, 200, 296, 1.0f, 14.0f) == 1);
        CHECK(VsNearestWire(w, 200, 200, 1.0f, 14.0f) == -1);
        VsEditGraph g;
        const int ev = g.add("OnStart", 0, 0), a = g.add("ScoreAdd", 0, 0), z = g.add("Print", 0, 0);
        g.linkExec(ev, 0, a); g.linkExec(a, 0, z);
        const VsWireRef ref = VsRefOfWire(g, true, 1);
        g.execLinks.erase(g.execLinks.begin());          // the list changed: the reference still finds its wire
        const int mid = g.add("Delay", 0, 0);
        CHECK(VsSplitWire(g, ref, mid));
        CHECK(ExecCount(g, a, mid) == 1 && ExecCount(g, mid, z) == 1);
        CHECK(!VsSplitWire(g, ref, g.add("Delay", 0, 0)));   // that wire is gone now
        CHECK(!VsRefOfWire(g, true, 99).on);
    }
    {
        CHECK(VsCurveDistance(150, 100, 100, 100, 200, 100) < 20.0f);
        CHECK(VsCurveDistance(150, 400, 100, 100, 200, 100) > 100.0f);
    }

    // (e) Delete in the middle of a chain joins the neighbours; Shift+Delete cuts.
    {
        VsEditGraph g;
        const int ev = g.add("OnStart", 40, 40);
        const int a = g.add("ScoreAdd", 300, 40), b = g.add("Delay", 600, 40), c = g.add("Print", 900, 40);
        g.linkExec(ev, 0, a); g.linkExec(a, 0, b); g.linkExec(b, 0, c);
        VsEditGraph cutCopy = g;
        CHECK(VsDeleteBlock(g, b, false));
        CHECK(g.nodes.size() == 3 && ExecCount(g, a, c) == 1 && ExecCount(g, ev, a) == 1);
        CHECK(!VsDeleteBlock(cutCopy, b, true));
        CHECK(cutCopy.nodes.size() == 3 && ExecCount(cutCopy, a, c) == 0 && cutCopy.execLinks.size() == 1);
        // The first block of a chain: nothing before it, nothing to join, nothing breaks.
        VsEditGraph h = g;
        CHECK(!VsDeleteBlock(h, ev, false));
        CHECK(h.nodes.size() == 2 && ExecCount(h, a, c) == 1);
        // Two in a row (both selected): the ends are joined.
        VsEditGraph k;
        const int e2 = k.add("OnStart", 0, 0), x = k.add("ScoreAdd", 0, 0), y = k.add("Delay", 0, 0), w = k.add("Print", 0, 0);
        k.linkExec(e2, 0, x); k.linkExec(x, 0, y); k.linkExec(y, 0, w);
        CHECK(VsDeleteBlocks(k, {x, y}, false) >= 1);
        CHECK(k.nodes.size() == 2 && ExecCount(k, e2, w) == 1);
        // A Branch (two ways out) is not guessed at: its neighbours are left apart.
        VsEditGraph m;
        const int e3 = m.add("OnStart", 0, 0), br = m.add("Branch", 0, 0), t = m.add("Print", 0, 0), f = m.add("Print", 0, 0);
        m.linkExec(e3, 0, br); m.linkExec(br, 0, t); m.linkExec(br, 1, f);
        CHECK(!VsDeleteBlock(m, br, false));
        CHECK(m.execLinks.empty());
    }
    // A value block in the middle hands its producer on to its readers.
    {
        VsEditGraph g;
        const int add = g.add("Add", 0, 0), mul = g.add("Mul", 0, 0), tr = g.add("Translate", 0, 0);
        g.linkData(mul, 0, add, 0);
        g.linkData(tr, 0, mul, 0);
        CHECK(VsDeleteBlock(g, mul, false));
        CHECK(g.dataLinks.size() == 1 && g.dataLinks[0].consumer == tr && g.dataLinks[0].producer == add);
    }

    // (1) A recipe lands under what is there, already wired.
    {
        VsEditGraph g;
        g.add("OnStart", 40, 40);
        g.add("Print", 300, 40);
        const auto before = g.nodes.size();
        const auto made = VsInsertRecipe(g, VsRecipes()[1].graph);   // Jump with Space
        CHECK(made.size() == 2 && g.nodes.size() == before + 2);
        CHECK(g.execLinks.size() == 1);
        for (int id : made) CHECK(g.find(id) && g.find(id)->y > 40);
        CHECK(!Overlaps(g));
        VsEditGraph e;                                              // an empty graph: the starting corner
        const auto made2 = VsInsertRecipe(e, VsRecipes()[0].graph);
        CHECK((int)made2.size() == VsGraphNodeCount(VsRecipes()[0].graph));
    }
    okay::Log::sink = sink;
    TEST_MAIN_RESULT();
}
