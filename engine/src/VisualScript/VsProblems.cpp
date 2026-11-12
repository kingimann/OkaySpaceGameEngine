#include "okay/VisualScript/VsProblems.hpp"
#include <algorithm>
#include <map>
#include <utility>

namespace okay::vs {
namespace {

struct Rule { const char* id; const char* fix; };

// One row per rule the checker (VsTypes.cpp) can report. The fix lines are what
// to do, in the words the editor uses.
const Rule kRules[] = {
    {"load",            "Open the file in a text editor and fix the line the message names, or undo the last edit. The graph can't run until it loads."},
    {"fn-signature",    "Edit the Function node's text to the form Name(a: Number, b: Text) -> result: Number."},
    {"fn-duplicate",    "Give each function its own name: rename one of the Function nodes."},
    {"fn-pin-missing",  "Delete the wire, or add the parameter / return value back to the function's signature."},
    {"fn-not-picked",   "Pick the function from the list on the node."},
    {"fn-unknown",      "Pick a function that exists, or add a Function node with that name."},
    {"type-mismatch",   "Insert the converter node the message suggests, or wire a value of the right type."},
    {"unreachable",     "Wire an event (or a function) into this node's flow pin, or delete the node."},
    {"event-empty",     "Wire the steps this event should run to its flow pin, or delete the event."},
    {"fn-unused",       "Add a Call Function node for it, or delete the function."},
    {"fn-empty",        "Wire the steps the function should run to its flow pin, or delete the function."},
    {"return-outside",  "Move the Return into the function's own flow, or delete it."},
    {"return-other-fn", "Pick the function this Return sits in, or move it to the function it returns from."},
    {"local-outside",   "Use Set Var / Get Var for graph variables; locals only exist inside a function."},
    {"param-outside",   "Use the parameter only inside its function, or store it in a variable first."},
    {"loop-control",    "Move the Break / Continue into a loop's Body flow, or delete it."},
    {"loop-no-body",    "Wire the steps to repeat to the loop's Body pin."},
    {"var-unset",       "Add a Set Var for it, declare it as a graph variable, or fix the name."},
    {"key-name",        "Use one letter or digit, space, enter, escape, tab, backspace, shift, ctrl or an arrow."},
    {"input-value",     "Type or wire a value the message says is accepted for that input."},
    {"required",        "Fill in the input the message names: type a value, pick one from the list, or wire one in."},
    {"prefab-unset",    "Pick a prefab file for the node, or drop one on it."},
    {"prefab-missing",  "Pick a prefab that exists in the project, or restore the missing file."},
    {"other",           "Select the node and read the message; fix the input or wire it names."},
};

bool Has(const std::string& s, const char* part) { return s.find(part) != std::string::npos; }
bool Starts(const std::string& s, const char* p) { return s.rfind(p, 0) == 0; }

} // namespace

const std::vector<std::string>& VsProblemRuleIds() {
    static const std::vector<std::string> ids = [] {
        std::vector<std::string> v;
        for (const Rule& r : kRules) v.push_back(r.id);
        return v;
    }();
    return ids;
}

std::string VsProblemRule(const VsIssue& is) {
    const std::string& m = is.message;
    if (Starts(m, "the graph can't be loaded")) return "load";
    if (Starts(m, "bad function signature")) return "fn-signature";
    if (Starts(m, "there's already a function named")) return "fn-duplicate";
    if (Has(m, "which it doesn't have (any more)")) return "fn-pin-missing";
    if (Starts(m, "no function is picked")) return "fn-not-picked";
    if (Starts(m, "there's no function named")) return "fn-unknown";
    if (Has(m, "is wired into an input that needs a")) return "type-mismatch";
    if (Starts(m, "nothing runs this node")) return "unreachable";
    if (Starts(m, "this event does nothing")) return "event-empty";
    if (Starts(m, "function '") && Has(m, "is never called")) return "fn-unused";
    if (Starts(m, "function '") && Has(m, "does nothing")) return "fn-empty";
    if (Starts(m, "Return is outside a function")) return "return-outside";
    if (Starts(m, "this Return is in function")) return "return-other-fn";
    if (Starts(m, "Set Local only works") || Starts(m, "Get Local only works")) return "local-outside";
    if (Starts(m, "a parameter of function")) return "param-outside";
    if (Has(m, "isn't inside the Body of a loop")) return "loop-control";
    if (Starts(m, "nothing is wired to Body")) return "loop-no-body";
    if (Starts(m, "variable '") && Has(m, "never sets it")) return "var-unset";
    if (Has(m, "isn't a key name")) return "key-name";
    if (Starts(m, "input '")) return "input-value";
    if (Starts(m, "Required:")) return "required";
    if (Starts(m, "no prefab file is set")) return "prefab-unset";
    if (Starts(m, "prefab file '") && Has(m, "doesn't exist")) return "prefab-missing";
    return "other";
}

std::string VsProblemFix(const std::string& id) {
    for (const Rule& r : kRules) if (id == r.id) return r.fix;
    return kRules[sizeof kRules / sizeof kRules[0] - 1].fix;
}

const char* VsSeverityName(int s) { return s <= 0 ? "Error" : s == 1 ? "Warning" : "Hint"; }
const char* VsSeverityGlyph(int s) { return s <= 0 ? "X" : s == 1 ? "!" : "i"; }

VsProblemCounts CountProblems(const std::vector<VsIssue>& issues) {
    VsProblemCounts c;
    for (const VsIssue& is : issues) (is.severity <= 0 ? c.errors : is.severity == 1 ? c.warnings : c.hints)++;
    return c;
}

std::string VsProblemSummary(const VsProblemCounts& c) {
    if (c.total() == 0) return "No problems";
    std::string s;
    auto add = [&](int n, const char* one, const char* many) {
        if (n <= 0) return;
        if (!s.empty()) s += ", ";
        s += std::to_string(n) + " " + (n == 1 ? one : many);
    };
    add(c.errors, "error", "errors");
    add(c.warnings, "warning", "warnings");
    add(c.hints, "hint", "hints");
    return s;
}

bool VsProblemFilter::shows(int severity) const { return severity <= 0 ? errors : severity == 1 ? warnings : hints; }

namespace {
int Rank(const std::vector<int>& order, int node) {
    if (node < 0) return -1;   // whole-graph findings lead their severity
    auto it = std::find(order.begin(), order.end(), node);
    return it == order.end() ? (int)order.size() + node : (int)(it - order.begin());
}
} // namespace

std::vector<VsProblemGroup> GroupProblems(const std::vector<VsIssue>& issues, const std::vector<int>& order, const VsProblemFilter& f) {
    std::vector<VsProblemGroup> out;
    std::map<std::pair<int, std::string>, std::size_t> at;
    for (const VsIssue& is : issues) {
        if (!f.shows(is.severity)) continue;
        const int sev = std::clamp(is.severity, 0, 2);
        auto key = std::make_pair(sev, is.message);
        auto it = at.find(key);
        if (it == at.end()) {
            VsProblemGroup g;
            g.severity = sev; g.message = is.message; g.rule = VsProblemRule(is); g.fix = VsProblemFix(g.rule);
            g.pin = is.pin; g.pinDir = is.pinDir;
            it = at.emplace(key, out.size()).first;
            out.push_back(std::move(g));
        }
        VsProblemGroup& g = out[it->second];
        ++g.count;
        if (is.node < 0) g.wholeGraph = true;
        else if (std::find(g.nodes.begin(), g.nodes.end(), is.node) == g.nodes.end()) g.nodes.push_back(is.node);
    }
    for (VsProblemGroup& g : out)
        std::stable_sort(g.nodes.begin(), g.nodes.end(), [&](int a, int b) { return Rank(order, a) < Rank(order, b); });
    std::stable_sort(out.begin(), out.end(), [&](const VsProblemGroup& a, const VsProblemGroup& b) {
        if (a.severity != b.severity) return a.severity < b.severity;
        const int ra = a.nodes.empty() ? -1 : Rank(order, a.nodes.front());
        const int rb = b.nodes.empty() ? -1 : Rank(order, b.nodes.front());
        return ra < rb;
    });
    return out;
}

std::vector<int> ProblemNodes(const std::vector<VsIssue>& issues, const std::vector<int>& order, const VsProblemFilter& f) {
    std::map<int, int> worst;   // node -> worst severity
    for (const VsIssue& is : issues) {
        if (is.node < 0 || !f.shows(is.severity)) continue;
        auto it = worst.emplace(is.node, std::clamp(is.severity, 0, 2)).first;
        it->second = std::min(it->second, std::clamp(is.severity, 0, 2));
    }
    std::vector<int> out;
    for (const auto& kv : worst) out.push_back(kv.first);
    std::stable_sort(out.begin(), out.end(), [&](int a, int b) {
        if (worst[a] != worst[b]) return worst[a] < worst[b];
        return Rank(order, a) < Rank(order, b);
    });
    return out;
}

int StepProblemNode(const std::vector<int>& nodes, int current, int dir) {
    if (nodes.empty()) return -1;
    const int n = (int)nodes.size();
    auto it = std::find(nodes.begin(), nodes.end(), current);
    if (it == nodes.end()) return dir >= 0 ? nodes.front() : nodes.back();
    const int i = (int)(it - nodes.begin());
    return nodes[(std::size_t)(((i + (dir >= 0 ? 1 : -1)) % n + n) % n)];
}

} // namespace okay::vs
