#include "okay/Components/DialogueGraph.hpp"
#include "StoryKv.hpp"
#include "okay/Core/Localization.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <set>
#include <sstream>

namespace okay {

using Kind = DialogueNode::Kind;

// ---------------------------------------------------------------------------
// Values and conditions
// ---------------------------------------------------------------------------
vs::VsValue DialogueValueFromText(const std::string& text) {
    if (text == "true") return vs::VsValue(true);
    if (text == "false") return vs::VsValue(false);
    if (!text.empty()) {
        char* end = nullptr;
        const float f = std::strtof(text.c_str(), &end);
        if (end && *end == 0 && end != text.c_str()) return vs::VsValue(f);
    }
    return vs::VsValue(text);
}

std::string DialogueValueText(const vs::VsValue& v) { return v.AsString(); }

bool DialogueCompare(const vs::VsValue& a, const std::string& op, const vs::VsValue& b) {
    if (a.IsString() || b.IsString()) {
        const std::string x = a.AsString(), y = b.AsString();
        if (op == "!=") return x != y;
        if (op == "<")  return x < y;
        if (op == "<=") return x <= y;
        if (op == ">")  return x > y;
        if (op == ">=") return x >= y;
        return x == y;
    }
    const float x = a.AsFloat(), y = b.AsFloat();
    if (op == "!=") return std::fabs(x - y) > 1e-6f;
    if (op == "<")  return x < y;
    if (op == "<=") return x <= y;
    if (op == ">")  return x > y;
    if (op == ">=") return x >= y;
    return std::fabs(x - y) <= 1e-6f;
}

std::string DialogueCondition::Text() const {
    switch (kind) {
        case Kind::None: return "always";
        case Kind::StoryVar: return name + " " + op + " " + value;
        case Kind::QuestState: return "quest " + name + " " + value;
        case Kind::HasItem: return "item " + name + " x" + std::to_string(count);
        case Kind::Flag: return (op == "!=" ? "not flag " : "flag ") + name;
    }
    return "?";
}

// ---------------------------------------------------------------------------
// Nodes
// ---------------------------------------------------------------------------
namespace {
const char* kKindNames[] = {"line", "choice", "cond", "set", "event", "give", "take", "quest", "objective", "jump", "end"};
const char* kKindLabels[] = {"Line", "Choice", "Condition", "Set", "Event", "Give Item", "Take Item",
                             "Start Quest", "Complete Objective", "Jump", "End"};
const char* kCondKinds[] = {"none", "var", "quest", "item", "flag"};
constexpr int kKindCount = 11;

const char* CondKindName(DialogueCondition::Kind k) { return kCondKinds[(int)k]; }
DialogueCondition::Kind CondKindFromName(const std::string& s) {
    for (int i = 0; i < 5; ++i) if (s == kCondKinds[i]) return (DialogueCondition::Kind)i;
    return DialogueCondition::Kind::None;
}
void WriteCond(storykv::Out& o, const std::string& p, const DialogueCondition& c) {
    if (c.Empty()) return;
    o.str((p + "ck").c_str(), CondKindName(c.kind)).str((p + "cn").c_str(), c.name)
     .str((p + "co").c_str(), c.op).str((p + "cv").c_str(), c.value).num((p + "cc").c_str(), c.count);
}
void ReadCond(const storykv::In& in, const std::string& p, DialogueCondition& c) {
    std::string k;
    in.str(p + "ck", k);
    c.kind = CondKindFromName(k);
    in.str(p + "cn", c.name); in.str(p + "co", c.op); in.str(p + "cv", c.value); in.i(p + "cc", c.count);
    if (c.op.empty()) c.op = "==";
}
} // namespace

const char* DialogueNode::KindName(Kind k) { return kKindNames[(int)k]; }
const char* DialogueNode::KindLabel(Kind k) { return kKindLabels[(int)k]; }
bool DialogueNode::KindFromName(const std::string& s, Kind& out) {
    for (int i = 0; i < kKindCount; ++i) if (s == kKindNames[i]) { out = (Kind)i; return true; }
    return false;
}

int DialogueNode::OutputCount() const {
    switch (kind) {
        case Kind::Choice: return (int)choices.size();
        case Kind::Condition: return 2;
        case Kind::End: return 0;
        default: return 1;
    }
}
int DialogueNode::Output(int i) const {
    switch (kind) {
        case Kind::Choice: return i >= 0 && i < (int)choices.size() ? choices[i].target : -1;
        case Kind::Condition: return i == 0 ? onTrue : onFalse;
        case Kind::End: return -1;
        default: return next;
    }
}
void DialogueNode::SetOutput(int i, int target) {
    switch (kind) {
        case Kind::Choice: if (i >= 0 && i < (int)choices.size()) choices[i].target = target; break;
        case Kind::Condition: (i == 0 ? onTrue : onFalse) = target; break;
        case Kind::End: break;
        default: next = target; break;
    }
}
const char* DialogueNode::OutputName(Kind k, int i) {
    if (k == Kind::Condition) return i == 0 ? "true" : "false";
    if (k == Kind::Choice) return "option";
    return "next";
}

// ---------------------------------------------------------------------------
// Graph
// ---------------------------------------------------------------------------
const DialogueNode* DialogueGraph::Find(int id) const {
    for (const auto& n : nodes) if (n.id == id) return &n;
    return nullptr;
}
DialogueNode* DialogueGraph::Find(int id) {
    for (auto& n : nodes) if (n.id == id) return &n;
    return nullptr;
}
int DialogueGraph::IndexOf(int id) const {
    for (std::size_t i = 0; i < nodes.size(); ++i) if (nodes[i].id == id) return (int)i;
    return -1;
}
int DialogueGraph::NewId() const {
    int id = 0;
    for (const auto& n : nodes) id = std::max(id, n.id + 1);
    return id;
}
DialogueNode& DialogueGraph::Add(Kind kind, float x, float y) {
    DialogueNode n;
    n.id = NewId(); n.kind = kind; n.x = x; n.y = y;
    if (kind == Kind::Choice) n.choices = {DialogueChoice{"Yes", {}, -1}, DialogueChoice{"No", {}, -1}};
    if (kind == Kind::Line && !speakers.empty()) n.speaker = speakers.front();
    nodes.push_back(n);
    return nodes.back();
}
void DialogueGraph::Remove(int id) {
    const int i = IndexOf(id);
    if (i < 0) return;
    nodes.erase(nodes.begin() + i);
    for (auto& n : nodes) {
        if (n.next == id) n.next = -1;
        if (n.onTrue == id) n.onTrue = -1;
        if (n.onFalse == id) n.onFalse = -1;
        for (auto& c : n.choices) if (c.target == id) c.target = -1;
    }
    if (start == id) start = nodes.empty() ? 0 : nodes.front().id;
}

bool DialogueGraph::HasErrors(const std::vector<Issue>& issues) {
    for (const auto& i : issues) if (i.severity >= 2) return true;
    return false;
}

std::vector<DialogueGraph::Issue> DialogueGraph::Validate(const std::vector<std::string>* knownQuests,
                                                          const std::vector<std::string>* knownItems) const {
    std::vector<Issue> out;
    auto add = [&](int node, int sev, std::string text) { out.push_back({node, sev, std::move(text)}); };
    if (nodes.empty()) { add(-1, 2, "The graph has no nodes."); return out; }
    if (!Find(start)) add(-1, 2, "The start node (id " + std::to_string(start) + ") does not exist.");
    auto knownQuest = [&](const std::string& q) {
        return !knownQuests || std::find(knownQuests->begin(), knownQuests->end(), q) != knownQuests->end();
    };
    auto knownItem = [&](const std::string& it) {
        return !knownItems || knownItems->empty() || std::find(knownItems->begin(), knownItems->end(), it) != knownItems->end();
    };
    auto checkTarget = [&](const DialogueNode& n, int target, const char* what, bool required) {
        if (target < 0) { if (required) add(n.id, 1, std::string(what) + " has no target (the conversation ends there)."); return; }
        if (!Find(target)) add(n.id, 2, std::string(what) + " points at a missing node (id " + std::to_string(target) + ").");
    };
    auto checkCond = [&](const DialogueNode& n, const DialogueCondition& c, const char* where) {
        if (c.kind == DialogueCondition::Kind::None) return;
        if (c.name.empty()) add(n.id, 2, std::string(where) + ": the condition names nothing.");
        if (c.kind == DialogueCondition::Kind::QuestState && !knownQuest(c.name)) add(n.id, 1, std::string(where) + ": unknown quest \"" + c.name + "\".");
        if (c.kind == DialogueCondition::Kind::HasItem && !knownItem(c.name)) add(n.id, 1, std::string(where) + ": unknown item \"" + c.name + "\".");
    };
    for (const auto& n : nodes) {
        switch (n.kind) {
            case Kind::Line:
                if (n.text.empty()) add(n.id, 1, "Empty line.");
                checkTarget(n, n.next, "Line", false);
                break;
            case Kind::Choice:
                if (n.choices.size() < 2) add(n.id, 2, "A choice needs at least 2 options.");
                if (n.choices.size() > 6) add(n.id, 2, "A choice can have at most 6 options.");
                for (std::size_t i = 0; i < n.choices.size(); ++i) {
                    const std::string what = "Option " + std::to_string(i + 1);
                    if (n.choices[i].text.empty()) add(n.id, 2, what + " has no text.");
                    checkTarget(n, n.choices[i].target, what.c_str(), false);
                    checkCond(n, n.choices[i].condition, what.c_str());
                }
                break;
            case Kind::Condition:
                if (n.condition.Empty()) add(n.id, 2, "The condition tests nothing.");
                checkCond(n, n.condition, "Condition");
                checkTarget(n, n.onTrue, "True", false);
                checkTarget(n, n.onFalse, "False", false);
                break;
            case Kind::Set:
                if (n.name.empty()) add(n.id, 2, "Set names no variable.");
                checkTarget(n, n.next, "Set", true);
                break;
            case Kind::Event:
                if (n.name.empty()) add(n.id, 2, "Event has no name.");
                checkTarget(n, n.next, "Event", true);
                break;
            case Kind::GiveItem:
            case Kind::TakeItem:
                if (n.item.empty()) add(n.id, 2, "No item set.");
                else if (!knownItem(n.item)) add(n.id, 1, "Unknown item \"" + n.item + "\".");
                if (n.count <= 0) add(n.id, 2, "The item count must be at least 1.");
                checkTarget(n, n.next, n.kind == Kind::GiveItem ? "Give Item" : "Take Item", true);
                break;
            case Kind::StartQuest:
            case Kind::CompleteObjective:
                if (n.name.empty()) add(n.id, 2, "No quest set.");
                else if (!knownQuest(n.name)) add(n.id, 1, "Unknown quest \"" + n.name + "\".");
                checkTarget(n, n.next, n.kind == Kind::StartQuest ? "Start Quest" : "Complete Objective", true);
                break;
            case Kind::Jump:
                if (n.next < 0) add(n.id, 2, "Jump has no target.");
                else checkTarget(n, n.next, "Jump", true);
                break;
            case Kind::End: break;
        }
    }
    // Reachability from the start node.
    std::set<int> seen;
    std::vector<int> stack{start};
    while (!stack.empty()) {
        const int id = stack.back(); stack.pop_back();
        const DialogueNode* n = Find(id);
        if (!n || !seen.insert(id).second) continue;
        for (int i = 0; i < n->OutputCount(); ++i) stack.push_back(n->Output(i));
    }
    for (const auto& n : nodes) if (!seen.count(n.id)) add(n.id, 1, "Unreachable from the start node.");
    return out;
}

// ---------------------------------------------------------------------------
// Text format
// ---------------------------------------------------------------------------
std::string DialogueGraph::Write() const {
    std::ostringstream o;
    o << "okaydialog 1\n";
    o << "name " << storykv::Quote(name) << "\n";
    o << "start " << start << "\n";
    for (const auto& s : speakers) o << "speaker " << storykv::Quote(s) << "\n";
    for (const auto& v : locals) o << "local " << storykv::Quote(v.name) << " " << storykv::Quote(v.value) << "\n";
    for (const auto& n : nodes) {
        storykv::Out k;
        k.num("id", n.id).str("kind", DialogueNode::KindName(n.kind)).num("x", n.x).num("y", n.y);
        switch (n.kind) {
            case Kind::Line:
                k.str("speaker", n.speaker).str("text", n.text);
                if (!n.portrait.empty()) k.str("portrait", n.portrait);
                if (!n.sound.empty()) k.str("sound", n.sound);
                if (n.autoAdvance > 0.0f) k.num("auto", n.autoAdvance);
                k.num("next", n.next);
                break;
            case Kind::Choice:
                k.num("n", (int)n.choices.size());
                for (std::size_t i = 0; i < n.choices.size(); ++i) {
                    const std::string p = "c" + std::to_string(i);
                    k.str((p + "t").c_str(), n.choices[i].text).num((p + "to").c_str(), n.choices[i].target);
                    WriteCond(k, p, n.choices[i].condition);
                }
                break;
            case Kind::Condition:
                WriteCond(k, "", n.condition);
                k.num("t", n.onTrue).num("f", n.onFalse);
                break;
            case Kind::Set:
                k.num("scope", n.setScope).str("name", n.name).str("value", n.value).num("next", n.next);
                break;
            case Kind::Event:
                k.str("name", n.name).num("next", n.next);
                break;
            case Kind::GiveItem:
            case Kind::TakeItem:
                k.str("item", n.item).num("count", n.count).num("next", n.next);
                break;
            case Kind::StartQuest:
                k.str("quest", n.name).num("next", n.next);
                break;
            case Kind::CompleteObjective:
                k.str("quest", n.name).num("objective", n.objective).num("next", n.next);
                break;
            case Kind::Jump:
                k.num("next", n.next);
                break;
            case Kind::End: break;
        }
        o << "node" << k.text() << "\n";
    }
    return o.str();
}

bool DialogueGraph::Parse(const std::string& text, std::string* err) {
    std::istringstream in(text);
    std::string line;
    if (!std::getline(in, line)) { if (err) *err = "empty file"; return false; }
    {
        std::istringstream h(line);
        std::string magic; int version = 0;
        h >> magic >> version;
        if (magic != "okaydialog" || version < 1) { if (err) *err = "not a dialogue file (expected 'okaydialog 1')"; return false; }
    }
    *this = DialogueGraph{};
    int lineNo = 1;
    while (std::getline(in, line)) {
        ++lineNo;
        std::string head, rest;
        if (!storykv::SplitHead(line, head, rest) || head[0] == '#') continue;
        if (head == "name") { std::istringstream r(rest); name = storykv::ReadQuoted(r); }
        else if (head == "start") { start = std::atoi(rest.c_str()); }
        else if (head == "speaker") { std::istringstream r(rest); speakers.push_back(storykv::ReadQuoted(r)); }
        else if (head == "local") {
            std::istringstream r(rest);
            DialogueVariable v; v.name = storykv::ReadQuoted(r); v.value = storykv::ReadQuoted(r);
            locals.push_back(v);
        } else if (head == "node") {
            storykv::In k(rest);
            DialogueNode n;
            std::string kind;
            k.i("id", n.id); k.str("kind", kind); k.f("x", n.x); k.f("y", n.y);
            if (!DialogueNode::KindFromName(kind, n.kind)) {
                if (err) *err = "line " + std::to_string(lineNo) + ": unknown node kind '" + kind + "'";
                return false;
            }
            k.i("next", n.next);
            switch (n.kind) {
                case Kind::Line:
                    k.str("speaker", n.speaker); k.str("text", n.text); k.str("portrait", n.portrait); k.str("sound", n.sound);
                    k.f("auto", n.autoAdvance);
                    break;
                case Kind::Choice: {
                    int cnt = 0; k.i("n", cnt);
                    cnt = std::max(0, std::min(cnt, 6));
                    for (int i = 0; i < cnt; ++i) {
                        const std::string p = "c" + std::to_string(i);
                        DialogueChoice c;
                        k.str(p + "t", c.text); k.i(p + "to", c.target);
                        ReadCond(k, p, c.condition);
                        n.choices.push_back(c);
                    }
                    break;
                }
                case Kind::Condition:
                    ReadCond(k, "", n.condition);
                    k.i("t", n.onTrue); k.i("f", n.onFalse);
                    break;
                case Kind::Set:
                    k.i("scope", n.setScope); k.str("name", n.name); k.str("value", n.value);
                    break;
                case Kind::Event: k.str("name", n.name); break;
                case Kind::GiveItem:
                case Kind::TakeItem: k.str("item", n.item); k.i("count", n.count); break;
                case Kind::StartQuest: k.str("quest", n.name); break;
                case Kind::CompleteObjective: k.str("quest", n.name); k.i("objective", n.objective); break;
                case Kind::Jump:
                case Kind::End: break;
            }
            if (Find(n.id)) { if (err) *err = "line " + std::to_string(lineNo) + ": duplicate node id " + std::to_string(n.id); return false; }
            nodes.push_back(n);
        } else {
            if (err) *err = "line " + std::to_string(lineNo) + ": unknown record '" + head + "'";
            return false;
        }
    }
    return true;
}

bool DialogueGraph::Load(const std::string& path, std::string* err) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { if (err) *err = "can't open " + path; return false; }
    std::stringstream ss; ss << f.rdbuf();
    return Parse(ss.str(), err);
}

bool DialogueGraph::Save(const std::string& path) const {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f << Write();
    return (bool)f;
}

// ---------------------------------------------------------------------------
// Localization keys
// ---------------------------------------------------------------------------
std::string DialogueLineKey(const std::string& graph, int node) {
    return "dlg." + Localization::KeyPart(graph) + "." + std::to_string(node);
}
std::string DialogueChoiceKey(const std::string& graph, int node, int choice) {
    return DialogueLineKey(graph, node) + "." + std::to_string(choice);
}
std::string DialogueSpeakerKey(const std::string& speaker) { return "speaker." + Localization::KeyPart(speaker); }

std::vector<std::pair<std::string, std::string>> DialogueStrings(const DialogueGraph& graph, const std::string& graphName) {
    std::vector<std::pair<std::string, std::string>> out;
    std::set<std::string> speakers;
    for (const DialogueNode& n : graph.nodes) {
        if (n.kind == Kind::Line) {
            if (!n.text.empty()) out.push_back({DialogueLineKey(graphName, n.id), n.text});
            if (!n.speaker.empty() && speakers.insert(n.speaker).second) out.push_back({DialogueSpeakerKey(n.speaker), n.speaker});
        } else if (n.kind == Kind::Choice) {
            for (std::size_t i = 0; i < n.choices.size(); ++i)
                if (!n.choices[i].text.empty()) out.push_back({DialogueChoiceKey(graphName, n.id, (int)i + 1), n.choices[i].text});
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// Session
// ---------------------------------------------------------------------------
void DialogueSession::Begin(const DialogueGraph* graph, DialogueHost* host, int startId) {
    m_graph = graph; m_host = host;
    m_node = nullptr; m_choices.clear(); m_trail.clear(); m_locals.clear();
    m_active = false; m_ended = false;
    if (!graph) { m_ended = true; return; }
    for (const auto& v : graph->locals) m_locals[v.name] = DialogueValueFromText(v.value);
    m_active = true;
    Run(startId >= 0 ? startId : graph->start);
}

void DialogueSession::Stop() {
    if (!m_active) return;
    Finish();
}

void DialogueSession::Finish() {
    m_active = false; m_ended = true;
    m_node = nullptr; m_choices.clear();
}

vs::VsValue DialogueSession::Var(const std::string& name) {
    auto it = m_locals.find(name);
    if (it != m_locals.end()) return it->second;
    return m_host ? m_host->GetVar(name) : vs::VsValue{};
}

bool DialogueSession::Test(const DialogueCondition& c) {
    switch (c.kind) {
        case DialogueCondition::Kind::None: return true;
        case DialogueCondition::Kind::StoryVar: return DialogueCompare(Var(c.name), c.op, DialogueValueFromText(c.value));
        case DialogueCondition::Kind::QuestState: {
            const std::string st = m_host ? m_host->QuestState(c.name) : "inactive";
            return c.op == "!=" ? st != c.value : st == c.value;
        }
        case DialogueCondition::Kind::HasItem: return m_host && m_host->ItemCount(c.name) >= std::max(1, c.count);
        case DialogueCondition::Kind::Flag: {
            const bool on = m_host && m_host->HasFlag(c.name);
            return c.op == "!=" ? !on : on;
        }
    }
    return false;
}

void DialogueSession::Present(const DialogueNode* n) {
    m_node = n;
    m_trail.push_back(n->id);
    m_choices.clear();
    if (n->kind == Kind::Choice)
        for (const auto& c : n->choices) if (Test(c.condition)) m_choices.push_back(c);
    if (n->kind == Kind::Line && m_host && !n->sound.empty()) m_host->Sound(n->sound);
}

void DialogueSession::Run(int id) {
    if (!m_graph || !m_active) return;
    for (int guard = 0; guard < 1000; ++guard) {
        const DialogueNode* n = m_graph->Find(id);
        if (!n) { Finish(); return; }
        switch (n->kind) {
            case Kind::Line: Present(n); return;
            case Kind::Choice:
                Present(n);
                if (m_choices.empty()) { Finish(); }   // nothing to pick: the conversation ends
                return;
            case Kind::Condition:
                m_trail.push_back(n->id);
                id = Test(n->condition) ? n->onTrue : n->onFalse;
                break;
            case Kind::Set: {
                m_trail.push_back(n->id);
                if (n->setScope == 2) m_locals[n->name] = DialogueValueFromText(n->value);
                else if (m_host) {
                    if (n->setScope == 1) m_host->SetFlag(n->name, DialogueValueFromText(n->value).AsBool());
                    else m_host->SetVar(n->name, DialogueValueFromText(n->value));
                }
                id = n->next;
                break;
            }
            case Kind::Event: m_trail.push_back(n->id); if (m_host) m_host->Event(n->name); id = n->next; break;
            case Kind::GiveItem: m_trail.push_back(n->id); if (m_host) m_host->GiveItem(n->item, n->count); id = n->next; break;
            case Kind::TakeItem: m_trail.push_back(n->id); if (m_host) m_host->TakeItem(n->item, n->count); id = n->next; break;
            case Kind::StartQuest: m_trail.push_back(n->id); if (m_host) m_host->StartQuest(n->name); id = n->next; break;
            case Kind::CompleteObjective: m_trail.push_back(n->id); if (m_host) m_host->CompleteObjective(n->name, n->objective); id = n->next; break;
            case Kind::Jump: m_trail.push_back(n->id); id = n->next; break;
            case Kind::End: m_trail.push_back(n->id); Finish(); return;
        }
    }
    Finish();   // a loop of silent nodes never reaches a line
}

void DialogueSession::Advance() {
    if (!m_active || !m_node) return;
    if (m_node->kind == Kind::Choice) return;
    Run(m_node->next);
}

bool DialogueSession::Choose(int index) {
    if (!m_active || !m_node || m_node->kind != Kind::Choice) return false;
    if (index < 0 || index >= (int)m_choices.size()) return false;
    const int target = m_choices[index].target;
    Run(target);
    return true;
}

} // namespace okay
