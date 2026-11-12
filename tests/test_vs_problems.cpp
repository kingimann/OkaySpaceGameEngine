// The Problems list's data model: every finding the graph checker can make gets a
// rule id and a "how to fix" line, the same finding on many nodes is one row with a
// count, rows are sorted worst first, and the next / previous problem walk works.
#include "test_framework.hpp"
#include "okay/VisualScript/VsProblems.hpp"
#include <filesystem>
#include <set>
#include <string>
#include <vector>

using namespace okay::vs;

namespace {
VsIssue I(int sev, int node, const char* msg) { VsIssue i; i.severity = sev; i.node = node; i.message = msg; return i; }
struct Case { const char* rule; const char* graph; };
}

int main() {
    RUN_SUITE("vs_problems");

    // ---- Every rule id has a fix line; a new id without one fails here ----
    {
        const auto& ids = VsProblemRuleIds();
        CHECK(ids.size() > 20);
        std::set<std::string> seen;
        for (const std::string& id : ids) {
            CHECK(seen.insert(id).second);                       // ids are unique
            const std::string fix = VsProblemFix(id);
            if (fix.empty()) std::fprintf(stderr, "  rule '%s' has no fix text\n", id.c_str());
            CHECK(!fix.empty());
            CHECK(fix.size() < 160);                             // "short"
        }
        CHECK(ids.back() == "other");
        CHECK(!VsProblemFix("no-such-rule").empty());            // an unknown id still gets a line
    }

    // ---- Every message the checker makes lands on its own rule, not "other" ----
    {
        const Case cases[] = {
            {"load",           "node 0 Bogus\n"},
            {"fn-signature",   "node 10 FunctionStart \"F(\"\n"},
            {"fn-duplicate",   "node 10 FunctionStart \"F()\"\nnode 11 FunctionStart \"F()\"\n"},
            {"fn-pin-missing", "node 10 FunctionStart \"F(a: Number) -> r: Number\"\nnode 12 Print\ndata 12 0 10 1\n"},
            {"fn-not-picked",  "node 0 OnStart\nnode 1 CallFunction\nexec 0 0 1\nentry OnStart 0\n"},
            {"fn-unknown",     "node 0 OnStart\nnode 1 CallFunction Nope\nexec 0 0 1\nentry OnStart 0\n"},
            {"type-mismatch",  "node 0 OnStart\nnode 1 Transform.Translate/1\nnode 2 Const \"hello\"\ndata 1 0 2 0\nexec 0 0 1\nentry OnStart 0\n"},
            {"unreachable",    "node 1 Print\n"},
            {"event-empty",    "node 0 OnStart\nentry OnStart 0\n"},
            {"fn-unused",      "node 10 FunctionStart \"F()\"\nnode 11 Print\nexec 10 0 11\n"},
            {"fn-empty",       "node 10 FunctionStart \"F()\"\n"},
            {"return-outside", "node 0 OnStart\nnode 1 Return F\nexec 0 0 1\nentry OnStart 0\nnode 10 FunctionStart \"F()\"\n"},
            {"return-other-fn","node 10 FunctionStart \"F()\"\nnode 11 FunctionStart \"G()\"\nnode 12 Return G\nexec 10 0 12\n"},
            {"local-outside",  "node 0 OnStart\nnode 1 SetLocal x\nexec 0 0 1\nentry OnStart 0\n"},
            {"param-outside",  "node 10 FunctionStart \"F(a: Number)\"\nnode 0 OnStart\nnode 1 Print\ndata 1 0 10 0\nexec 0 0 1\nentry OnStart 0\n"},
            {"loop-control",   "node 0 OnStart\nnode 1 Break\nexec 0 0 1\nentry OnStart 0\n"},
            {"loop-no-body",   "node 0 OnStart\nnode 1 While\nexec 0 0 1\nentry OnStart 0\n"},
            {"var-unset",      "node 0 OnStart\nnode 1 Print\nnode 2 GetVar foo\ndata 1 0 2 0\nexec 0 0 1\nentry OnStart 0\n"},
            {"key-name",       "node 0 OnKey F13\nnode 1 Print\nexec 0 0 1\nentry OnKey:F13 0\n"},
            {"input-value",    "node 0 OnUpdate\nnode 1 SetVar a\nnode 2 Input.GetAxis/1\nnode 3 Const \"Sideways\"\ndata 2 0 3 0\ndata 1 0 2 0\nexec 0 0 1\nentry OnUpdate 0\n"},
            {"required",       "node 0 OnStart\nnode 1 Branch\nexec 0 0 1\nentry OnStart 0\n"},
            {"prefab-unset",   "node 0 OnStart\nnode 1 Spawn\nexec 0 0 1\nentry OnStart 0\n"},
            {"prefab-missing", "node 0 OnStart\nnode 1 Spawn nope/missing.okayprefab\nexec 0 0 1\nentry OnStart 0\n"},
        };
        
        const std::string dir = (std::filesystem::temp_directory_path() / "okay_vs_problems_nodir").string();
        std::set<std::string> covered;
        for (const Case& c : cases) {
            bool found = false;
            for (const VsIssue& is : ValidateGraphText(c.graph, dir))
                if (VsProblemRule(is) == c.rule) found = true;
            if (!found) std::fprintf(stderr, "  rule '%s': no finding classified to it\n", c.rule);
            CHECK(found);
            covered.insert(c.rule);
            for (const VsIssue& is : ValidateGraphText(c.graph, dir))
                if (VsProblemRule(is) == "other") std::fprintf(stderr, "  unclassified: %s\n", is.message.c_str());
        }
        for (const std::string& id : VsProblemRuleIds())    // a rule id nothing exercises is dead
            if (id != "other") { if (!covered.count(id)) std::fprintf(stderr, "  rule '%s' has no case\n", id.c_str()); CHECK(covered.count(id) > 0); }
        VsIssue odd; odd.message = "something new";
        CHECK(VsProblemRule(odd) == "other");
    }

    // ---- Counts and the header text ----
    {
        std::vector<VsIssue> v = {I(0, 1, "a"), I(1, 2, "b"), I(1, 3, "c"), I(0, 4, "d"), I(2, 5, "e")};
        const VsProblemCounts c = CountProblems(v);
        CHECK(c.errors == 2 && c.warnings == 2 && c.hints == 1 && c.total() == 5);
        CHECK(VsProblemSummary(c) == "2 errors, 2 warnings, 1 hint");
        CHECK(VsProblemSummary(CountProblems({I(0, 1, "a")})) == "1 error");
        CHECK(VsProblemSummary(CountProblems({I(1, 1, "a"), I(1, 2, "b")})) == "2 warnings");
        CHECK(VsProblemSummary(CountProblems({})) == "No problems");
        CHECK(std::string(VsSeverityName(0)) == "Error" && std::string(VsSeverityName(1)) == "Warning" && std::string(VsSeverityName(2)) == "Hint");
        CHECK(std::string(VsSeverityGlyph(0)) != VsSeverityGlyph(1) && std::string(VsSeverityGlyph(1)) != VsSeverityGlyph(2));
    }

    // ---- Grouping: same message on several nodes is one row with a count ----
    {
        const std::vector<int> order = {5, 3, 9, 1};
        std::vector<VsIssue> v = {
            I(1, 1, "Required: condition."), I(1, 5, "Required: condition."), I(1, 5, "Required: condition."),
            I(0, 9, "Required: scene."),     I(1, 3, "nothing runs this node: no event's chain leads to it"),
            I(0, -1, "the graph can't be loaded: x"),
        };
        auto g = GroupProblems(v, order);
        CHECK(g.size() == 4);
        // errors first; the whole-graph error leads, then node 9's; then warnings in graph order
        CHECK(g[0].severity == 0 && g[0].wholeGraph && g[0].nodes.empty() && g[0].rule == "load");
        CHECK(g[1].severity == 0 && g[1].nodes == std::vector<int>({9}));
        CHECK(g[2].severity == 1 && g[2].message == "Required: condition.");
        CHECK(g[2].count == 3);                                          // 3 findings...
        CHECK(g[2].nodes == std::vector<int>({5, 1}));                   // ...on 2 nodes, in graph order, no repeats
        CHECK(g[2].rule == "required" && !g[2].fix.empty());
        CHECK(g[3].rule == "unreachable" && g[3].nodes == std::vector<int>({3}));
        // The same text at two severities is two rows.
        auto two = GroupProblems({I(0, 1, "same"), I(1, 1, "same")}, order);
        CHECK(two.size() == 2 && two[0].severity == 0 && two[1].severity == 1);
        // A node missing from the order list goes last, ordered by id.
        auto late = GroupProblems({I(1, 77, "z"), I(1, 5, "y")}, order);
        CHECK(late.size() == 2 && late[0].nodes[0] == 5 && late[1].nodes[0] == 77);
        CHECK(GroupProblems({}, order).empty());
    }

    // ---- Filters ----
    {
        std::vector<VsIssue> v = {I(0, 1, "e"), I(1, 2, "w"), I(2, 3, "h")};
        VsProblemFilter f;
        CHECK(GroupProblems(v, {1, 2, 3}, f).size() == 3);
        f.warnings = false;
        auto g = GroupProblems(v, {1, 2, 3}, f);
        CHECK(g.size() == 2 && g[0].message == "e" && g[1].message == "h");
        f.errors = false; f.hints = false; f.warnings = true;
        g = GroupProblems(v, {1, 2, 3}, f);
        CHECK(g.size() == 1 && g[0].message == "w");
        f.warnings = false;
        CHECK(GroupProblems(v, {1, 2, 3}, f).empty());
        CHECK(CountProblems(v).total() == 3);                    // counts ignore the filter
    }

    // ---- Next / previous problem ----
    {
        const std::vector<int> order = {4, 2, 8, 6};
        std::vector<VsIssue> v = {I(1, 4, "w"), I(0, 8, "e"), I(1, 2, "w2"), I(0, 8, "e2"), I(0, -1, "g")};
        const std::vector<int> nodes = ProblemNodes(v, order);
        CHECK(nodes == std::vector<int>({8, 4, 2}));             // errors first, graph order after, each once
        CHECK(StepProblemNode(nodes, -1, 1) == 8);               // nothing selected: first
        CHECK(StepProblemNode(nodes, -1, -1) == 2);              // ...or last going back
        CHECK(StepProblemNode(nodes, 8, 1) == 4);
        CHECK(StepProblemNode(nodes, 2, 1) == 8);                // wraps
        CHECK(StepProblemNode(nodes, 8, -1) == 2);               // wraps back
        CHECK(StepProblemNode(nodes, 99, 1) == 8);               // a selection with no problem
        CHECK(StepProblemNode({}, 3, 1) == -1);
        VsProblemFilter onlyWarn; onlyWarn.errors = false;
        CHECK(ProblemNodes(v, order, onlyWarn) == std::vector<int>({4, 2}));
    }
    TEST_MAIN_RESULT();
}
