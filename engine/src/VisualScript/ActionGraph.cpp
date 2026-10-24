#include "okay/VisualScript/ActionGraph.hpp"
#include "okay/Components/ActionList.hpp"

#include <sstream>
#include <unordered_set>
#include <vector>

namespace okay::vs {

void ActionTriggerToEvent(int trigger, const std::string& key, std::string& type, std::string& arg) {
    using T = ActionList::Trigger;
    arg.clear();
    switch ((T)trigger) {
        case T::OnStart:          type = "OnStart"; break;
        case T::OnUpdate:         type = "OnUpdate"; break;
        case T::OnKey:            type = "OnKey";   arg = key.empty() ? "e" : key; break;
        case T::OnKeyUp:          type = "OnKeyUp"; arg = key.empty() ? "e" : key; break;
        case T::OnCollision:      type = "OnCollision"; break;
        case T::OnClick:          type = "OnClick"; break;
        case T::OnMessage:        type = "OnEvent"; arg = key.empty() ? "Event" : key; break;
        case T::OnTriggerEnter:   type = "OnTrigger"; break;
        case T::OnTriggerExit:    type = "OnTriggerExit"; break;
        case T::OnMouseEnter:     type = "OnMouseEnter"; break;
        case T::OnMouseExit:      type = "OnMouseExit"; break;
        case T::OnMouseDown:      type = "OnMouseDown"; break;
        case T::OnMouseUp:        type = "OnMouseUp"; break;
        case T::OnMouseOver:      type = "OnMouseOver"; break;
        case T::OnCollisionExit:  type = "OnCollisionExit"; break;
        case T::OnCollisionStay:  type = "OnCollisionStay"; break;
        case T::OnInterval:       type = "OnTimer"; arg = key.empty() ? "1" : key; break;
        case T::OnLateUpdate:     type = "OnLateUpdate"; break;
        case T::OnTriggerStay:    type = "OnTriggerStay"; break;
        default:                 type = "OnStart"; break;
    }
}

namespace {

std::string Quoted(std::string v) {
    for (char& c : v) if (c == '"') c = '\'';
    return "\"" + v + "\"";
}
std::string Token(std::string v) {   // entry names are one token
    for (char& c : v) if (c == ' ' || c == '\t') c = '_';
    return v;
}

struct Writer {
    std::ostringstream o;
    int next = 1;
    int node(const std::string& type, const std::string& arg, float x, float y) {
        int id = next++;
        o << "node " << id << ' ' << type;
        if (!arg.empty()) o << ' ' << (arg.find_first_of(" \t") != std::string::npos ? Quoted(arg) : arg);
        o << "\npos " << id << ' ' << (int)x << ' ' << (int)y << "\n";
        return id;
    }
    void exec(int from, int pin, int to) { o << "exec " << from << ' ' << pin << ' ' << to << "\n"; }
};

// Steps whose behaviour depends on running as one list (jumps, blocks, waits that
// must hold the whole handler) stay together in one Actions block.
bool NeedsBlock(const std::vector<ActionList::Item>& ins, bool runsOnce) {
    static const std::unordered_set<std::string> kBlock = {
        "goto", "if_goto", "label", "repeat", "end_repeat", "while", "end_while", "if", "else",
        "end_if", "for_each", "end_for", "for_each_tag", "end_for_tag", "break", "continue",
        "gosub", "return_sub", "wait_until"};
    for (const auto& it : ins) {
        if (kBlock.count(it.op)) return true;
        if (it.op == "wait" && !runsOnce) return true;   // a repeating trigger must not overlap itself
    }
    return false;
}

void EmitHandler(Writer& w, int trigger, const std::string& key, bool once,
                 const std::vector<ActionList::Item>& conds, const std::vector<ActionList::Item>& ins,
                 float y) {
    std::string type, arg;
    ActionTriggerToEvent(trigger, key, type, arg);
    const float dx = 250.0f;
    float x = 40.0f;
    int ev = w.node(type, arg, x, y);
    if (type == "OnStart" || type == "OnUpdate" || type == "OnCollision" || type == "OnTrigger" ||
        type == "OnClick" || type == "OnCollisionExit" || type == "OnTriggerExit" ||
        type == "OnMouseEnter" || type == "OnMouseExit" || type == "OnMouseDown" || type == "OnMouseUp" ||
        type == "OnMouseOver" || type == "OnCollisionStay" || type == "OnLateUpdate" ||
        type == "OnTriggerStay")
        w.o << "entry " << type << ' ' << ev << "\n";
    else if (type == "OnEvent") w.o << "entry OnEvent:" << Token(arg) << ' ' << ev << "\n";
    else w.o << "entry " << type << ':' << Token(arg) << ' ' << ev << "\n";

    int prev = ev;
    auto chain = [&](int id) { w.exec(prev, 0, id); prev = id; };
    if (once) { x += dx; chain(w.node("Once", "", x, y)); }
    for (const auto& c : conds) {
        if (c.op == "always" || c.op.empty()) continue;
        x += dx; chain(w.node("Condition", ActionList::EncodeItem(c), x, y));
    }
    if (ins.empty()) return;
    const bool runsOnce = once || type == "OnStart";
    if (NeedsBlock(ins, runsOnce)) {
        x += dx; chain(w.node("Actions", ActionList::EncodeItems(ins), x, y));
        return;
    }
    for (const auto& it : ins) {
        if (it.op == "comment") continue;
        x += dx;
        if (it.op == "wait") chain(w.node("Delay", it.args.empty() ? std::string("1") : it.args[0], x, y));
        else                 chain(w.node("Action", ActionList::EncodeItem(it), x, y));
        if (it.op == "stop") break;
    }
}

} // namespace

std::string ActionListToGraph(const ActionList& al) {
    Writer w;
    w.o << "# OkayVS graph (converted from the Actions component";
    if (!al.name.empty()) w.o << " '" << al.name << "'";
    w.o << ")\n";
    float y = 40.0f;
    if (!al.variables.empty()) {   // declared variables: seeded first thing
        int ev = w.node("OnStart", "", 40.0f, y);
        w.o << "entry OnStart " << ev << "\n";
        int prev = ev; float x = 40.0f;
        for (const auto& v : al.variables) {
            ActionList::Item it{"declare_var", {v.name, std::to_string(v.type), v.value}};
            x += 250.0f;
            int id = w.node("Action", ActionList::EncodeItem(it), x, y);
            w.exec(prev, 0, id); prev = id;
        }
        y += 180.0f;
    }
    EmitHandler(w, (int)al.trigger, al.triggerKey, al.once, al.conditions, al.instructions, y);
    for (const auto& h : al.extraHandlers) {
        y += 180.0f;
        EmitHandler(w, (int)h.trigger, h.triggerKey, h.once, h.conditions, h.instructions, y);
    }
    return w.o.str();
}

} // namespace okay::vs
