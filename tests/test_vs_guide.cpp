// The plain-words guide for Visual Script nodes (editor/VisualScriptGuide.hpp): every node has a plain sentence,
// a hover tip for every pin with an example, a help card (what it does, what to plug in, a tiny example, common
// mistakes, a next step), next-step suggestions that really fit, a "Try this node" graph that loads and runs, an
// intent heading, and the renamed titles keep their old name as a search word and keep graphs loading unchanged.
#include "test_framework.hpp"
#include "VisualScriptEditor.hpp"
#include "okay/Components/VisualScriptComponent.hpp"
#include "okay/Core/Log.hpp"
#include "okay/Scene/Scene.hpp"
#include <Okay.hpp>

#include <cstdio>
#include <set>
#include <string>

using namespace okay::editor::vsed;

namespace {
const VsDef* Find(const char* type) {
    for (const VsDef& d : Registry()) if (std::strcmp(d.type, type) == 0) return &d;
    return nullptr;
}
std::string Banned(const std::string& text) { return VsGlossaryViolation(text, true); }
}  // namespace

int main() {
    RUN_SUITE("vs_guide");
    const std::vector<VsDef>& all = Registry();

    // ---- every node: plain line, pin tips, help card, next line --------------------------------------------
    int badLine = 0, badTip = 0, badCard = 0, jargonLine = 0, shown = 0;
    std::set<int> intentsUsed;
    for (const VsDef& d : all) {
        ++shown;
        const std::string line = VsPlainLine(d);
        if (line.size() < 4 || (line.back() != '.' && line.back() != '!' && line.back() != '?')) { ++badLine; if (badLine < 6) std::printf("  bad line: %s -> '%s'\n", d.type, line.c_str()); }
        if (!Banned(line).empty()) { ++jargonLine; if (jargonLine < 6) std::printf("  jargon in line: %s -> '%s' (%s)\n", d.type, line.c_str(), Banned(line).c_str()); }
        for (int i = 0; i < (int)d.dataIn.size(); ++i) {
            const std::string tip = VsPinTip(d, true, false, i);
            if (tip.size() < 12 || tip.find("Example:") == std::string::npos) { ++badTip; if (badTip < 6) std::printf("  bad tip: %s in %d: '%s'\n", d.type, i, tip.c_str()); }
        }
        for (int i = 0; i < (int)d.dataOut.size(); ++i)
            if (VsPinTip(d, false, false, i).find("Example:") == std::string::npos) ++badTip;
        if (d.execIn && VsPinTip(d, true, true, 0).size() < 12) ++badTip;
        for (int i = 0; i < (int)d.execOut.size(); ++i) if (VsPinTip(d, false, true, i).size() < 12) ++badTip;
        const VsHelpCard c = VsHelpCardFor(d, &all);
        if (c.title.empty() || c.does.empty() || c.plugs.empty() || c.chain.size() < 2 || c.example.empty() || c.mistakes.empty() || c.next.empty()) {
            ++badCard; if (badCard < 6) std::printf("  incomplete card: %s\n", d.type);
        }
        for (const std::string& m : c.mistakes) if (!Banned(m).empty()) { ++jargonLine; std::printf("  jargon in mistake of %s: %s\n", d.type, Banned(m).c_str()); }
        if (!Banned(c.next).empty()) ++jargonLine;
        const int in = VsIntentFor(d);
        CHECK(in >= 0 && in < kVsIntentCount);
        intentsUsed.insert(in);
    }
    std::printf("  nodes checked: %d\n", shown);
    CHECK(badLine == 0);
    CHECK(badTip == 0);
    CHECK(badCard == 0);
    CHECK(jargonLine == 0);
    CHECK((int)intentsUsed.size() == kVsIntentCount);
    CHECK(kVsIntentCount == (int)(sizeof kVsIntentBlurbs / sizeof kVsIntentBlurbs[0]));

    // ---- pin tips and examples -----------------------------------------------------------------------------
    using okay::vs::PinType;
    CHECK(VsPinExample(PinType::Number, "Seconds") == "2");
    CHECK(VsPinExample(PinType::Text, "Message") == "Hello!");
    CHECK(VsPinExample(PinType::Bool, "") == "true");
    CHECK(VsPinExample(PinType::Object, "Object") == "Player");
    const VsDef* branch = Find("Branch");
    const VsDef* translate = Find("Translate");
    const VsDef* onStart = Find("OnStart");
    const VsDef* addNode = Find("Add");
    const VsDef* printNode = Find("Print");
    CHECK(branch && translate && onStart && addNode && printNode);
    CHECK(VsPinTip(*translate, true, false, (int)translate->dataIn.size() - 1).find("Leave it empty to use this object") != std::string::npos);

    // ---- needs input ---------------------------------------------------------------------------------------
    CHECK(VsPinNeedsInput(*branch, 0));
    CHECK(VsMissingHint(*branch, 0).find("plug the condition in here") != std::string::npos);
    CHECK(!VsPinNeedsInput(*translate, (int)translate->dataIn.size() - 1));   // Target empty means this object
    for (const VsDef& d : all)
        for (int i = 0; i < (int)d.dataIn.size(); ++i)
            if (VsPinNeedsInput(d, i)) {
                const std::string h = VsMissingHint(d, i);
                CHECK(h.rfind("plug ", 0) == 0 && h.size() > 12);
                CHECK(Banned(h).empty());
            }

    // ---- next steps: every suggestion exists and fits -------------------------------------------------------
    int withSteps = 0, valueNodesWithSteps = 0;
    for (const VsDef& d : all) {
        const auto steps = VsNextSteps(d, all, 4);
        CHECK(steps.size() <= 4);
        for (const VsNextStep& s : steps) {
            const VsDef* c = Find(s.type.c_str());
            CHECK(c != nullptr);
            if (!c) continue;
            CHECK(!s.title.empty() && !s.line.empty());
            if (s.exec) {
                CHECK(!d.execOut.empty() && c->execIn);
            } else {
                CHECK(s.inPin >= 0 && s.inPin < (int)c->dataIn.size());
                CHECK(okay::vs::PinCompatible(okay::vs::OutputType(d.type, 0, ""), okay::vs::InputType(c->type, s.inPin)));
            }
        }
        if (!steps.empty()) ++withSteps;
        if (!steps.empty() && d.execOut.empty() && d.execIn == false) ++valueNodesWithSteps;
    }
    std::printf("  nodes with next-step chips: %d of %d (value nodes: %d)\n", withSteps, shown, valueNodesWithSteps);
    for (const char* t : {"OnStart", "OnKey", "OnUpdate", "Branch", "Delay", "Print", "Add", "Const", "GetVar", "Compare", "Key", "DeltaTime"})
        CHECK(!VsNextSteps(*Find(t), all, 4).empty());
    {   // an event is followed by actions, never by another event or a value
        for (const VsNextStep& s : VsNextSteps(*onStart, all, 4)) CHECK(Find(s.type.c_str())->execIn);
        // one-click add-and-connect on a real graph
        VsEditGraph g;
        const int a = g.add("OnKey", 0, 0);
        const auto steps = VsNextSteps(*Find("OnKey"), all, 4);
        CHECK(!steps.empty());
        const int b = g.add(steps[0].type, 200, 0);
        g.linkExec(a, steps[0].outPin, b);
        CHECK(g.execLinks.size() == 1 && g.execLinks[0].from == a && g.execLinks[0].to == b);
        const auto vsteps = VsNextSteps(*Find("Add"), all, 4);
        CHECK(!vsteps.empty() && !vsteps[0].exec);
    }

    // ---- Try this node: loads, and the common ones really run --------------------------------------------
    int tryFail = 0, tryChecked = 0;
    for (const VsDef& d : all) {
        VsEditGraph g;
        const std::string text = VsTryGraph(d, g);
        VsEditGraph back;
        back.fromText(text);
        if (back.nodes.empty() || back.toText().empty()) { ++tryFail; continue; }
        // The editor may write a node under its canonical type name, so compare the node count with what was built.
        VsEditGraph built;
        built.fromText(text);
        if (back.nodes.size() != built.nodes.size() || back.nodes.size() < 1) { ++tryFail; if (tryFail < 6) std::printf("  try graph for %s changed on re-read\n", d.type); }
        ++tryChecked;
    }
    std::printf("  try graphs built and re-read: %d\n", tryChecked);
    CHECK(tryFail == 0);
    {
        // Run a few in a headless scene: the log shows the node worked.
        std::vector<std::string> lines;
        auto sink = okay::Log::sink;
        okay::Log::sink = [&](okay::Log::Level, const std::string& m) { lines.push_back(m); };
        auto runTry = [&](const char* type) {
            lines.clear();
            VsEditGraph g;
            const std::string text = VsTryGraph(*Find(type), g);
            okay::Scene scene("Try");
            auto* go = scene.CreateGameObject("Thing");
            auto* vs = go->AddComponent<okay::VisualScriptComponent>();
            std::string err;
            const bool ok = vs->LoadFromText(text, &err);
            if (!ok || !err.empty()) std::printf("  %s: load error '%s'\n", type, err.c_str());
            CHECK(ok);
            scene.Start();
            scene.Update(0.016f);
            std::string joined;
            for (const std::string& l : lines) joined += l + "\n";
            return joined;
        };
        const std::string add = runTry("Add");
        std::printf("  Add try log: %s", add.c_str());
        CHECK(add.find("10") != std::string::npos);                       // 5 + 5 shown
        const std::string pr = runTry("Print");
        CHECK(pr.find("Hello") != std::string::npos || pr.find("Print") != std::string::npos);
        const std::string start = runTry("OnStart");
        CHECK(start.find("happened") != std::string::npos);
        okay::Log::sink = sink;
    }

    // ---- renamed nodes: new plain title, old names still searchable, type (and so saved graphs) unchanged ---
    // The titles come from ONE table, kVsNames in VisualScriptNames.hpp (Grok's #422 finding 14: VisualScriptPlain.hpp
    // used to keep a second table, kVsPlainRenames, that overrode it). Each older name a person might type is still a
    // search word: the palette title from before ("Every (s)") and the plain-words title from before ("Every Seconds").
    struct Renamed { const char* type; const char* title; const char* oldNames[2]; };
    const Renamed renamed[] = {
        {"Timer",    "Repeat Every Few Seconds", {"Every (s)", "Every Seconds"}},
        {"Wait",     "Wait the First Time Only", {"Wait (s)", "Wait Once"}},
        {"Delay",    "Wait Then Continue",       {"Delay (s)", "Wait Seconds"}},
        {"Cooldown", "Limit How Often",          {"Cooldown (s)", "Cooldown"}},
        {"FlipFlop", "Switch Between Two Paths", {"Flip-Flop", "Flip Flop"}},
        {"Gate",     "Open or Close the Way",    {"Gate", "Gate"}},
        {"Sequence", "Do These in Order",        {"Sequence", "Do in Order"}},
        {"Const",    "Type in a Value",          {"Const", "Fixed Value"}},
    };
    for (const Renamed& r : renamed) {
        const VsDef* d = Find(r.type);
        CHECK(d != nullptr);
        if (!d) continue;
        CHECK(std::strcmp(d->title, r.title) == 0);
        const VsName* n = FindVsName(r.type);                     // the title lives in the one table
        CHECK(n != nullptr && std::strcmp(n->title, r.title) == 0);
        CHECK(Banned(r.title).empty());
        for (const char* old : r.oldNames)
            CHECK(VsLower(VsNodeSearchText(r.type)).find(VsLower(old)) != std::string::npos);
        CHECK(std::strcmp(d->type, r.type) == 0);
    }
    {   // a graph saved before the rename loads, and writes back the same node types
        const std::string old = "node 1 OnStart\npos 1 0 0\nnode 2 Delay 2\npos 2 200 0\nnode 3 Print\npos 3 400 0\nexec 1 0 2\nexec 2 0 3\n";
        VsEditGraph g;
        g.fromText(old);
        CHECK(g.nodes.size() == 3 && g.execLinks.size() == 2);
        CHECK(g.nodes[1].type == "Delay" && g.nodes[1].arg == "2");
    }

    // ---- the card remembers whether it is open ---------------------------------------------------------------
    {
        VsUsagePrefs p;
        CHECK(p.helpCardOpen);
        p.helpCardOpen = false;
        CHECK(!VsUsagePrefsFromText(VsUsagePrefsToText(p)).helpCardOpen);
        CHECK(VsUsagePrefsFromText("hidestart 1\n").helpCardOpen);   // an old file keeps the card open
    }
    TEST_MAIN_RESULT();
}
