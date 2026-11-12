// The graph checker's findings, arranged for a person to read: each one gets a
// rule id and a short "how to fix" line, the same message on several nodes is
// listed once with a count, and the list is sorted worst first. No drawing here;
// the editor's Problems panel and the tests both use this.
#pragma once
#include "okay/VisualScript/VsTypes.hpp"
#include <string>
#include <vector>

namespace okay::vs {

/// Severities a row can have (VsIssue::severity): 0 error, 1 warning, 2 hint.
enum VsSeverity { kVsError = 0, kVsWarning = 1, kVsHint = 2 };

/// Every rule id the checker can report, in a stable order. "other" is last: it
/// catches a message no rule claims, so a new checker rule still shows up (with
/// a generic fix line) until it is given its own id.
const std::vector<std::string>& VsProblemRuleIds();

/// Which rule produced `issue`, as one of VsProblemRuleIds().
std::string VsProblemRule(const VsIssue& issue);

/// One or two sentences saying what to do about a rule. Never empty for an id in
/// VsProblemRuleIds(); a generic line for an unknown id.
std::string VsProblemFix(const std::string& ruleId);

/// "Error", "Warning" or "Hint".
const char* VsSeverityName(int severity);
/// A shape that tells the severities apart without colour: "X" error, "!" warning, "i" hint.
const char* VsSeverityGlyph(int severity);

struct VsProblemCounts { int errors = 0, warnings = 0, hints = 0; int total() const { return errors + warnings + hints; } };
VsProblemCounts CountProblems(const std::vector<VsIssue>& issues);
/// "2 errors, 3 warnings" (zero kinds are left out), "1 error", or "No problems".
std::string VsProblemSummary(const VsProblemCounts& c);

/// Which severities the list shows (the filter toggles).
struct VsProblemFilter { bool errors = true, warnings = true, hints = true; bool shows(int severity) const; };

/// Findings that say the same thing, shown as one row.
struct VsProblemGroup {
    int severity = 0;
    std::string rule;
    std::string message;
    std::string fix;
    std::vector<int> nodes;   // the nodes it is on (graph order, no repeats; -1 = the whole graph is left out)
    int count = 0;            // how many findings (>= nodes.size() when some are on the whole graph)
    int pin = -1, pinDir = 0; // the first finding's pin
    bool wholeGraph = false;  // some finding is about the graph, not a node
};

/// Filter, group by (severity, message) and sort: errors first, then warnings,
/// then hints; inside a severity by the first node's place in `nodeOrder` (the
/// node ids in the order the graph lists them; a node not in it goes last).
std::vector<VsProblemGroup> GroupProblems(const std::vector<VsIssue>& issues, const std::vector<int>& nodeOrder,
                                          const VsProblemFilter& filter = {});

/// The nodes that have a visible problem, worst first, graph order inside a
/// severity, each once. The next / previous problem commands walk this.
std::vector<int> ProblemNodes(const std::vector<VsIssue>& issues, const std::vector<int>& nodeOrder,
                              const VsProblemFilter& filter = {});

/// The node after (dir = 1) or before (dir = -1) `current` in `nodes`, wrapping
/// round. -1 for an empty list. A `current` not in the list gives the first
/// (dir 1) or last (dir -1) node.
int StepProblemNode(const std::vector<int>& nodes, int current, int dir);

} // namespace okay::vs
