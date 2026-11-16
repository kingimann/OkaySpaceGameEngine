// Fn.<builtin> nodes name their inputs and say what they do from the docs (#888 finding 34, part C).
// Before: 1407 of the 1438 Fn.* nodes only said "Calls this text-script builtin..." and every input was
// First / Second / Third, so Fn.raycast never said its order is origin, direction, distance.
// tools/gen_fn_pin_docs.py reads docs/scripting_reference.md, docs/scripting.md, the editor's Help
// reference and signature hints and the VM's comments into VsBuiltinDocs.gen.hpp. This test checks:
//   * Fn.raycast's inputs are named from the docs (ox, oy, dx, dy, maxDist), on the canvas, in the
//     tooltip and in the Add Node brief, and the tooltip says what it gives back;
//   * for every documented builtin: the table is sorted, it is a real Fn node, its pin count matches
//     the node, the documented arguments fit on the node, each label is the label of that input on the
//     canvas and in the tooltip, labels pass the naming check and are all different, and each argument's
//     name is in its input's tooltip line;
//   * builtins the docs do not cover keep First / Second and the old text;
//   * saved graphs do not change: pins are written by index, never by label;
//   * the coverage cannot drop below today's numbers;
//   * python3 tools/gen_fn_pin_docs.py --check passes (the header matches its sources).
#include "test_framework.hpp"
#include "VisualScriptEditor.hpp"
#include "okay/VisualScript/VsBuiltins.hpp"
#include <Okay.hpp>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>

using namespace okay::editor::vsed;

namespace {

bool Has(const std::string& hay, const std::string& needle) { return hay.find(needle) != std::string::npos; }

const VsPinDoc* InputPin(const VsNodeDoc& d, int index) {
    int i = 0;
    for (const VsPinDoc& p : d.pins) {
        if (!p.input || p.exec) continue;
        if (i++ == index) return &p;
    }
    return nullptr;
}

} // namespace

int main() {
    auto sink = okay::Log::sink;
    okay::Log::sink = [](okay::Log::Level, const std::string&) {};

    // ---- Fn.raycast: origin, direction, distance ----
    {
        const okay::vs::BuiltinDoc* rc = okay::vs::BuiltinDocFor("raycast");
        CHECK(rc != nullptr);
        if (rc) {
            CHECK(rc->argc == 5);
            const char* args[] = {"ox", "oy", "dx", "dy", "maxDist"};
            const char* labels[] = {"Origin Across", "Origin Height", "Direction Across", "Direction Height", "Distance Limit"};
            for (int i = 0; i < 5; ++i) {
                CHECK(std::strcmp(rc->args[i], args[i]) == 0);
                CHECK(std::string(okay::vs::BuiltinPinName("raycast", i)) == labels[i]);
            }
            CHECK(((rc->optional >> 4) & 1u) == 1u);      // [, maxDist]
            CHECK(((rc->optional >> 0) & 1u) == 0u);
            CHECK(Has(rc->returns, "name of the object hit"));
            std::printf("  Fn.raycast inputs:");
            for (int i = 0; i < okay::vs::BuiltinPinCount("raycast"); ++i) std::printf(" [%s]", okay::vs::BuiltinPinName("raycast", i));
            std::printf("\n  Fn.raycast gives back: %s\n", rc->returns);
        }
        const VsDef* def = FindDef("Fn.raycast");
        CHECK(def != nullptr);
        if (def) {
            CHECK(def->dataIn.size() == (std::size_t)okay::vs::BuiltinPinCount("raycast"));
            CHECK(!def->dataIn.empty() && std::string(def->dataIn[0]) == "Origin Across");
            CHECK(def->dataIn.size() > 4 && std::string(def->dataIn[4]) == "Distance Limit");
            const VsNodeBrief brief = VsNodeBriefFor(*def);
            CHECK(Has(brief.needs, "Origin Across (optional)"));
            CHECK(Has(brief.does, "text-script function raycast"));
            CHECK(Has(brief.does, "2D ray"));
            CHECK(Has(brief.gives, "Result (Any): the name of the object hit"));
        }
        const VsNodeDoc* doc = VsNodeDocFor("Fn.raycast");
        CHECK(doc != nullptr);
        if (doc) {
            CHECK(!Has(doc->description, "Calls the text-script function"));
            CHECK(!Has(doc->description, "Calls this text-script builtin"));   // the old general line
            CHECK(Has(doc->description, "raycast(ox, oy, dx, dy [, maxDist])"));
            CHECK(Has(doc->description, "Gives back: the name of the object hit"));
            const VsPinDoc* p0 = InputPin(*doc, 0);
            const VsPinDoc* p4 = InputPin(*doc, 4);
            CHECK(p0 && p0->name == "Origin Across" && Has(p0->meaning, ": ox."));
            CHECK(p4 && p4->name == "Distance Limit" && Has(p4->meaning, "maxDist") && Has(p4->meaning, "Optional"));
            bool resultSays = false;
            for (const VsPinDoc& p : doc->pins)
                if (!p.input && !p.exec && p.name == "Result" && Has(p.meaning, "name of the object hit")) resultSays = true;
            CHECK(resultSays);
            std::printf("  tooltip: %s\n", doc->description.c_str());
        }
    }

    // ---- every documented builtin ----
    int documented = 0, withWords = 0, wordedPins = 0, docArgs = 0, withReturn = 0, withSummary = 0;
    {
        const std::set<std::string> ordinals = {"First", "Second", "Third", "Fourth", "Fifth", "Sixth", "Seventh",
                                                "Eighth", "Ninth", "Tenth", "Eleventh", "Twelfth", "Thirteenth"};
        int bad = 0;
        const okay::vs::BuiltinDoc* prev = nullptr;
        for (const okay::vs::BuiltinDoc* d = okay::vs::BuiltinDocsBegin(); d != okay::vs::BuiltinDocsEnd(); ++d) {
            ++documented;
            const std::string name = d->name;
            bool ok = true;
            if (prev && std::strcmp(prev->name, d->name) >= 0) ok = false;   // sorted, no duplicates
            prev = d;
            if (!okay::vs::IsBuiltinNode("Fn." + name)) ok = false;
            const int pins = okay::vs::BuiltinPinCount(name);
            if (d->pinCount != pins || d->argc < 0 || d->argc > pins) ok = false;
            if (okay::vs::BuiltinDocFor(name) != d) ok = false;
            const VsDef* def = FindDef("Fn." + name);
            const VsNodeDoc* doc = VsNodeDocFor("Fn." + name);
            if (!def || !doc || def->dataIn.size() != (std::size_t)pins) ok = false;
            std::set<std::string> seen;
            bool worded = false;
            for (int i = 0; ok && i < pins; ++i) {
                const std::string label = d->labels[i] ? d->labels[i] : "";
                if (label.empty() || okay::vs::BuiltinPinName(name, i) != label) ok = false;
                if (def && std::string(def->dataIn[(std::size_t)i]) != label) ok = false;   // the canvas shows it
                const VsPinDoc* pd = doc ? InputPin(*doc, i) : nullptr;
                if (!pd || pd->name != label) ok = false;                                  // the tooltip shows it
                if (!LintVsPin(label.c_str()).empty()) ok = false;
                if (!seen.insert(label).second) ok = false;
                if (i < d->argc) {
                    if (!d->args[i] || !d->args[i][0]) ok = false;
                    else if (pd && !Has(pd->meaning, std::string(": ") + d->args[i] + ".")) ok = false;   // the arg's own name, in order
                    if (!ordinals.count(label) && !Has(label, " Value")) { worded = true; ++wordedPins; }
                }
            }
            // A node's own help line may not point at "First" / "Second" once that input has another name.
            const char* help = VsNodeHelp(("Fn." + name).c_str());
            const char* general = VsNodeHelp("Fn.");
            if (help && general && std::strcmp(help, general) != 0)
                for (int i = 0; i < pins && i < 4; ++i)
                    if (Has(help, std::string(okay::vs::BuiltinPinName(i))) && okay::vs::BuiltinPinName(name, i) != std::string(okay::vs::BuiltinPinName(i)))
                        ok = false;
            docArgs += d->argc;
            if (worded) ++withWords;
            if (d->returns[0]) ++withReturn;
            if (d->summary[0]) ++withSummary;
            if (!ok) {
                ++bad;
                if (bad <= 10) std::printf("  bad row: %s (argc %d, pins %d/%d)\n", d->name, d->argc, d->pinCount, pins);
            }
        }
        CHECK(bad == 0);
    }

    // ---- undocumented builtins keep the old names and text ----
    {
        int undocumented = 0, keepsOld = 0;
        for (const okay::vs::BuiltinNodeInfo& n : okay::vs::BuiltinNodes()) {
            if (okay::vs::BuiltinDocFor(n.builtin)) continue;
            ++undocumented;
            bool ok = std::string(okay::vs::BuiltinPinName(n.builtin, 0)) == "First" &&
                      std::string(okay::vs::BuiltinPinName(n.builtin, 1)) == "Second";
            const VsNodeDoc* doc = VsNodeDocFor(n.type);
            if (!doc) ok = false;
            else if (const char* help = VsNodeHelp(n.type.c_str()); help && help[0])
                ok = ok && doc->description == help;                     // the old text, unchanged
            else
                ok = ok && Has(doc->description, "Calls the text-script function " + n.builtin);
            if (ok) ++keepsOld;
        }
        CHECK(undocumented == keepsOld);
        const int total = (int)okay::vs::BuiltinNodes().size();
        CHECK(documented + undocumented == total);
        std::printf("  coverage: %d of %d Fn nodes documented (%d with at least one input in words, %d of %d documented "
                    "arguments in words); %d with a description, %d say what they give back; %d keep First/Second\n",
                    documented, total, withWords, wordedPins, docArgs, withSummary, withReturn, undocumented);
        // Floors: today's numbers (2026-11-16). Raise them when the docs grow; they must not drop.
        CHECK(documented >= 1053);
        CHECK(withWords >= 786);
        CHECK(withReturn >= 24);
    }

    // ---- saved graphs do not change: pins are saved by index ----
    {
        const std::string text =
            "# OkayVS graph (edited in the Visual Script editor)\n"
            "node 1 OnStart\npos 1 0 0\nentry OnStart 1\n"
            "node 2 Fn.raycast\ndefault 2 0 1\ndefault 2 2 0\ndefault 2 3 -1\ndefault 2 4 25\npos 2 200 0\n"
            "node 3 SetVar hit\npos 3 400 0\n"
            "exec 1 0 2\nexec 2 0 3\ndata 3 0 2 0\n";
        VsEditGraph g;
        g.fromText(text);
        const std::string again = g.toText();
        CHECK(again == text);
        if (again != text) std::printf("  round trip changed:\n%s\n", again.c_str());
        CHECK(!Has(again, "Origin") && !Has(again, "Distance") && !Has(again, "ox"));
        const VsEditNode* n = g.find(2);
        CHECK(n && n->pinDefault.count(4) && n->pinDefault.at(4) == "25");
    }

    // ---- the generator's --check ----
    {
        const std::string cmd = std::string("python3 \"") + OKAY_SOURCE_DIR + "/tools/gen_fn_pin_docs.py\" --check";
        const int rc = std::system(cmd.c_str());
        CHECK(rc == 0);
    }

    okay::Log::sink = sink;
    TEST_MAIN_RESULT();
}
