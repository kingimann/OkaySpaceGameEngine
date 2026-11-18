// The sentences the Visual Script window says when something is wrong: plain words only, and the one-click fixes.
#include "test_framework.hpp"
#include "VisualScriptEditor.hpp"
#include "VisualScriptPlainWords.hpp"
#include "VisualScriptGlossary.hpp"
#include "okay/VisualScript/VsTypes.hpp"
#include <string>
#include <vector>

using namespace okay::editor::vsed;
using okay::vs::PinType;

namespace {
void Plain(const std::string& s) {
    const std::string bad = VsGlossaryViolation(s);
    if (!bad.empty()) std::printf("  banned word '%s' in: %s\n", bad.c_str(), s.c_str());
    CHECK(bad.empty());
    CHECK(!s.empty());
}
} // namespace

int main() {
    RUN_SUITE("vs_plain_words");
    auto sink = okay::Log::sink;
    okay::Log::sink = [](okay::Log::Level, const std::string&) {};

    // The two example sentences from the brief, word for word.
    CHECK(VsPlainMismatch(PinType::Text, PinType::Number) == "This plug needs a number, but you gave it text.");
    CHECK(std::string(VsPlainNotConnected()) == "This block is not connected to anything yet.");

    // Every fixed sentence, and every pair of plug kinds, uses plain words only.
    const PinType all[] = {PinType::Any, PinType::Number, PinType::Bool, PinType::Text, PinType::Vec3,
                           PinType::Object, PinType::List, PinType::Map, PinType::Color};
    for (PinType a : all) {
        Plain(VsPlainKind(a));
        for (PinType b : all) {
            Plain(VsPlainMismatch(a, b));
            const std::string tip = VsPlainMismatchTip(a, b);
            if (!tip.empty()) Plain(tip);
        }
    }
    for (const char* s : {VsPlainStartIntoStart(), VsPlainWouldLoop(), VsPlainFlowToValue(), VsPlainValueToFlow(),
                          VsPlainDoesNotFitWire(), VsPlainNotConnected(), VsPlainNothingAfterStart(), VsPlainFixIt()})
        Plain(s);

    // The checker's own words are rewritten; the ones it does not know stay as they were.
    CHECK(VsPlainIssue("nothing runs this node: no event's chain leads to it") == VsPlainNotConnected());
    CHECK(VsPlainIssue("this event does nothing: nothing is wired to it") == VsPlainNothingAfterStart());
    CHECK(VsPlainIssue("a Text from Say is wired into an input that needs a Number - use To Number") ==
          "This plug needs a number, but you gave it text.");
    CHECK(VsPlainIssue("a Vector3 from Where is wired into an input that needs a Bool") ==
          "This plug needs true or false, but you gave it a position.");
    CHECK(VsPlainIssue("some message we do not know") == "some message we do not know");
    for (const char* m : {"nothing runs this node: no event's chain leads to it", "this event does nothing: nothing is wired to it",
                          "a Text from Say is wired into an input that needs a Number - use To Number",
                          "Required: clip name. Pick one from the list, or drop a clip file.",
                          "Required: condition. Wire a true/false value into Condition.",
                          "variable 'score' is read but this graph never sets it (it reads 0 unless another graph or a script sets it)",
                          "'banana' isn't a key name OkaySpace knows - it would act as 'b'. Use one letter",
                          "no prefab file is set"}) {
        const std::string out = VsPlainIssue(m);
        CHECK(out != m);
        Plain(out);
    }
    // The real checker, run on a real graph: its two most common findings come out plain.
    {
        VsEditGraph g;
        g.add("OnStart", 40, 40);                       // an event with nothing after it
        g.add("ScoreAdd", 400, 300);                    // a step nothing reaches
        const auto issues = okay::vs::ValidateGraphText(g.toText());
        bool a = false, b = false;
        for (const auto& i : issues) {
            const std::string p = VsPlainIssue(i.message);
            if (p == VsPlainNotConnected()) a = true;
            if (p == VsPlainNothingAfterStart()) b = true;
        }
        CHECK(a && b);
    }

    // Fix it for me: a block nothing reaches is plugged in after the nearest free block.
    {
        VsEditGraph g;
        const int ev = g.add("OnStart", 40, 40);
        const int lost = g.add("ScoreAdd", 400, 40);
        CHECK(VsFixNotConnected(g, lost));
        CHECK(g.execLinks.size() == 1 && g.execLinks[0].from == ev && g.execLinks[0].to == lost);
        CHECK(!VsFixNotConnected(g, lost));             // already connected: nothing to fix
        CHECK(!VsFixNotConnected(g, ev));               // an event has no "then" input
        // Never wired back into its own chain.
        VsEditGraph h;
        const int e2 = h.add("OnStart", 40, 40), a = h.add("ScoreAdd", 300, 40), b = h.add("Print", 600, 40);
        h.linkExec(a, 0, b);                            // a -> b, and a has nothing before it
        CHECK(VsFixNotConnected(h, a));
        CHECK(h.execLinks.size() == 2 && h.execLinks[1].from == e2 && h.execLinks[1].to == a);
        VsEditGraph none;
        const int only = none.add("ScoreAdd", 0, 0);
        CHECK(!VsFixNotConnected(none, only));          // nothing to connect it to
    }
    // The fix is offered for a real finding of the checker, and applying it makes that finding go away.
    {
        VsEditGraph g;
        g.add("OnStart", 40, 40);
        const int lost = g.add("ScoreAdd", 400, 300);
        std::string msg;
        for (const auto& i : okay::vs::ValidateGraphText(g.toText())) if (i.node == lost) msg = i.message;
        CHECK(!msg.empty());
        CHECK(VsFixAvailable(g, msg, lost, -1, 0));
        CHECK(g.execLinks.empty());                     // asking did not change the graph
        CHECK(VsFixIssue(g, msg, lost, -1, 0));
        bool still = false;
        for (const auto& i : okay::vs::ValidateGraphText(g.toText())) if (i.node == lost) still = true;
        CHECK(!still);
        CHECK(!VsFixIssue(g, "some message we do not know", lost, -1, 0));
    }
    // Green or red plug while dragging.
    {
        VsEditGraph g;
        const int add = g.add("Add", 0, 0), tr = g.add("Translate", 300, 0), fmt = g.add("Format", 0, 200), sc = g.add("ScoreAdd", 300, 200);
        CHECK(VsPlugAccepts(g, false, true, add, 0, tr, 0));       // a number into a number plug: green
        CHECK(!VsPlugAccepts(g, false, true, fmt, 0, tr, 0));      // text into a number plug: red
        CHECK(!VsPlugAccepts(g, false, true, add, 0, add, 0));     // never onto its own block
        CHECK(VsPlugAccepts(g, true, true, add, 0, sc, 0));        // flow into a step: green
        const int ev = g.add("OnStart", 0, 400);
        CHECK(!VsPlugAccepts(g, true, true, sc, 0, ev, 0));        // flow into a starting block: red
        const int mul = g.add("Mul", 600, 0);
        g.linkData(mul, 0, add, 0);                                // Add feeds Mul ...
        CHECK(!VsPlugAccepts(g, false, true, mul, 0, add, 1));     // ... so Mul may not feed Add back: red
    }
    // Fix it for me: text going into a number gets a convert block in between.
    {
        VsEditGraph h;
        const int fmt = h.add("Format", 40, 40), mv = h.add("Translate", 500, 40);   // Format makes text
        h.linkData(mv, 0, fmt, 0);
        CHECK(!VsWireOk(h, mv, 0, fmt, 0));
        CHECK(VsFixMismatch(h, mv, 0));
        CHECK(h.nodes.size() == 3 && h.dataLinks.size() == 2);
        CHECK(VsWireOk(h, mv, 0, h.dataLinks[1].producer, h.dataLinks[1].outPin) || VsWireOk(h, mv, 0, h.dataLinks[0].producer, h.dataLinks[0].outPin));
        for (const auto& l : h.dataLinks) CHECK(VsWireOk(h, l.consumer, l.inPin, l.producer, l.outPin));
        CHECK(!VsFixMismatch(h, mv, 2));                // no wire on that plug
    }
    okay::Log::sink = sink;
    TEST_MAIN_RESULT();
}
