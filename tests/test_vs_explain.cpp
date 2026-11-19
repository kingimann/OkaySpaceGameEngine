// "Explain this graph" and "Why isn't it working?": pure functions on the edit graph.
#include "test_framework.hpp"
#include "VisualScriptEditor.hpp"
#include "VisualScriptGlossary.hpp"
#include <string>

using namespace okay::editor::vsed;

namespace {
bool Has(const std::vector<VsWhyItem>& v, VsWhyKind k) { for (auto& i : v) if (i.kind == k) return true; return false; }
}

int main() {
    RUN_SUITE("vs_explain");
    auto sink = okay::Log::sink;
    okay::Log::sink = [](okay::Log::Level, const std::string&) {};

    // Explain: empty, no start, a chain, a self-starting Quick block.
    {
        VsEditGraph g;
        CHECK(VsExplainGraph(g).find("empty") != std::string::npos);
        const int a = g.add("Print", 40, 40);
        (void)a;
        CHECK(VsExplainGraph(g).find("Nothing starts") != std::string::npos);
    }
    {
        VsEditGraph g = VsStarterGraph();
        const std::string s = VsExplainGraph(g);
        CHECK(s.rfind("When ", 0) == 0);
        CHECK(s.find("the game starts") != std::string::npos);
        CHECK(s.find("it does:") != std::string::npos);
        CHECK(VsGlossaryViolation(s).empty());
    }
    {
        VsEditGraph g;
        const int k = g.add("OnKey", 0, 0);
        const int p = g.add("Print", 200, 0);
        g.linkExec(k, 0, p);
        const std::string s = VsExplainGraph(g);
        CHECK(s.find("you press space") != std::string::npos);
        CHECK(VsExplainGraph(g, 0).find("and more") != std::string::npos);
    }
    {
        VsEditGraph g;
        VsInsertRecipe(g, VsRecipes()[0].graph);
        const std::string s = VsExplainGraph(g);
        CHECK(!s.empty() && s.find("Nothing starts") == std::string::npos);
    }
    // Every recipe explains itself without saying "does nothing".
    for (const VsRecipe& r : VsRecipes()) {
        VsEditGraph g; VsInsertRecipe(g, r.graph);
        const std::string s = VsExplainGraph(g);
        CHECK(!s.empty());
        CHECK(s.find("Nothing starts") == std::string::npos);
    }

    // Checklist.
    {
        VsWhyFacts f;
        VsEditGraph g;
        CHECK(Has(VsWhyNotWorking(g, f), VsWhyKind::EmptyGraph));
        VsEditGraph ok = VsStarterGraph();
        CHECK(VsWhyNotWorking(ok, f).empty());
        f.attached = false;
        CHECK(Has(VsWhyNotWorking(ok, f), VsWhyKind::NotAttached));
        f.attached = true; f.playing = true; f.anyEventRan = false;
        CHECK(Has(VsWhyNotWorking(ok, f), VsWhyKind::NeverTriggered));
        f.anyEventRan = true;
        CHECK(VsWhyNotWorking(ok, f).empty());
    }
    {   // No start block: the fix adds one and joins the loose block.
        VsEditGraph g; VsWhyFacts f;
        const int p = g.add("Print", 40, 40);
        auto items = VsWhyNotWorking(g, f);
        CHECK(Has(items, VsWhyKind::NoStart) && Has(items, VsWhyKind::NotConnected));
        for (auto& it : items) if (it.kind == VsWhyKind::NoStart) { CHECK(VsApplyWhyFix(g, it)); break; }
        CHECK(!Has(VsWhyNotWorking(g, f), VsWhyKind::NoStart));
        CHECK(!Has(VsWhyNotWorking(g, f), VsWhyKind::NotConnected));
        bool fed = false; for (auto& l : g.execLinks) if (l.to == p) fed = true;
        CHECK(fed);
    }
    {   // A loose block with a start block: the Connect fix joins it.
        VsEditGraph g; VsWhyFacts f;
        const int s = g.add("OnStart", 0, 0);
        const int p = g.add("Print", 300, 0);
        (void)s;
        auto items = VsWhyNotWorking(g, f);
        bool fixed = false;
        for (auto& it : items) if (it.kind == VsWhyKind::NotConnected && it.node == p) fixed = VsApplyWhyFix(g, it);
        CHECK(fixed);
        CHECK(VsWhyNotWorking(g, f).empty());
    }
    {   // A needed input left empty.
        VsEditGraph g; VsWhyFacts f;
        const int s = g.add("OnStart", 0, 0);
        const int b = g.add("Branch", 300, 0);
        g.linkExec(s, 0, b);
        auto items = VsWhyNotWorking(g, f);
        CHECK(Has(items, VsWhyKind::InputEmpty));
        for (auto& it : items) { CHECK(!it.text.empty()); CHECK(VsGlossaryViolation(it.text).empty()); }
    }
    okay::Log::sink = sink;
    TEST_MAIN_RESULT();
}
