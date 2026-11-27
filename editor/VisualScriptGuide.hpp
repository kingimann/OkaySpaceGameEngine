#pragma once
// "I dropped a node and I do not know what it is for." The plain-words guide for the Visual Script editor,
// as pure functions (no ImGui) so tests can run them over every node:
//   VsPlainLine      one plain sentence for a node (shown under its title)
//   VsPinExample     an example value for a pin type ("5", "Hello!", "Player")
//   VsPinTip         a pin's hover tip in plain words, with an example value
//   VsPinNeedsInput  / VsMissingHint   which inputs a node cannot work without, and what to say about them
//   VsNextSteps      nodes that usually come next, type-compatible, with where to wire them
//   VsTryGraph       a ready-made little working graph for one node (the "Try this node" button)
//   VsHelpCardFor    the "What is this?" card: does, plug-ins, tiny example, common mistakes, next step
//   VsIntentFor      the "what do you want to do?" heading a node sits under in the Add Node list
//   (plain titles of the most confusing nodes: kVsNames in VisualScriptNames.hpp; old names stay search words)
// Written for this project from docs/scripting/visual_script_clarity.md. Included by VisualScriptModel.hpp after the
// graph model (VsEditGraph, VsDef) it reads. The graph file format and the runtime are not touched.
#include "VisualScriptClarity.hpp"
#include "VisualScriptGlossary.hpp"
#include "VisualScriptLevels.hpp"
#include "VisualScriptNodeDocs.hpp"
#include "VisualScriptNodeHelp.hpp"
#include "VisualScriptPlain.hpp"
#include "okay/VisualScript/VsTypes.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace okay::editor::vsed {

// ---- 1. One plain sentence ---------------------------------------------------------------------------------

/// What the node does, in one plain sentence that ends with a full stop. Never empty. The same sentence is shown
/// under the node's title, in the Add Node list and on the "What is this?" card.
inline std::string VsPlainLine(const VsDef& d) {
    std::string s;
    if (const char* help = VsNodeHelp(d.type)) s = help;
    if (s.empty()) {
        if (const VsNodeDoc* doc = VsNodeDocFor(d.type)) {
            std::string text = doc->description;
            text = VsStripTitleEcho(text, d.title);   // "Wait Seconds. Pauses..." names itself first
            s = VsFirstSentence(text, 110);
        }
    }
    if (s.empty()) s = std::string(d.title ? d.title : d.type);
    s = VsPlainText(s);
    if (!s.empty() && s.back() != '.' && s.back() != '!' && s.back() != '?') s += '.';
    return s;
}

/// VsPlainLine, kept per node type: it is drawn on every node every frame.
inline const std::string& VsPlainLineCached(const VsDef& d) {
    static std::unordered_map<std::string, std::string> cache;
    auto it = cache.find(d.type);
    if (it == cache.end()) it = cache.emplace(d.type, VsPlainLine(d)).first;
    return it->second;
}

// ---- 2. Example values and pin tips -------------------------------------------------------------------------

inline bool VsNameHas(const std::string& name, const char* word) {
    std::string low;
    for (char c : name) low += (char)std::tolower((unsigned char)c);
    return low.find(word) != std::string::npos;
}

/// A value a beginner could type into a pin of this type, chosen by the pin's name when that helps.
inline std::string VsPinExample(okay::vs::PinType t, const std::string& pinName) {
    using okay::vs::PinType;
    switch (t) {
        case PinType::Number:
            if (VsNameHas(pinName, "speed")) return "3";
            if (VsNameHas(pinName, "second") || VsNameHas(pinName, "time") || VsNameHas(pinName, "delay") || VsNameHas(pinName, "duration")) return "2";
            if (VsNameHas(pinName, "angle") || VsNameHas(pinName, "degree")) return "90";
            if (VsNameHas(pinName, "chance") || VsNameHas(pinName, "percent")) return "50";
            if (VsNameHas(pinName, "min") || VsNameHas(pinName, "smallest")) return "1";
            if (VsNameHas(pinName, "max") || VsNameHas(pinName, "largest")) return "10";
            if (pinName == "X" || pinName == "Y" || pinName == "Z" || VsNameHas(pinName, "left") || VsNameHas(pinName, "up")) return "1";
            return "5";
        case PinType::Bool: return "true";
        case PinType::Text:
            if (VsNameHas(pinName, "message") || VsNameHas(pinName, "text") || VsNameHas(pinName, "say")) return "Hello!";
            if (VsNameHas(pinName, "name") || VsNameHas(pinName, "tag")) return "Player";
            if (VsNameHas(pinName, "scene") || VsNameHas(pinName, "level")) return "Level2";
            return "Hello";
        case PinType::Vec3: return "(0, 1, 0)";
        case PinType::Object: return "Player";
        case PinType::List: return "1, 2, 3";
        case PinType::Map: return "score = 5";
        case PinType::Color: return "red";
        default: return "5";
    }
}

/// What kind of thing goes into / comes out of a pin, in everyday words ("a number", "an object").
inline const char* VsPinKindWords(okay::vs::PinType t) {
    using okay::vs::PinType;
    switch (t) {
        case PinType::Number: return "a number";
        case PinType::Bool:   return "a yes-or-no value";
        case PinType::Text:   return "some text";
        case PinType::Vec3:   return "a position";
        case PinType::Object: return "an object";
        case PinType::List:   return "a list";
        case PinType::Map:    return "a word list";
        case PinType::Color:  return "a colour";
        default:              return "any value";
    }
}

/// The pin's own name, or a stand-in for an unnamed one.
inline std::string VsPinLabel(const char* name, bool exec, bool input) {
    if (name && name[0]) return name;
    return exec ? (input ? "Start" : "Then") : (input ? "Value" : "Result");
}

/// A pin's hover tip: what it is, what to put in it, and an example value. `idx` is the pin's number in its list.
inline std::string VsPinTip(const VsDef& d, bool input, bool exec, int idx) {
    using okay::vs::PinType;
    const char* raw = nullptr;
    if (exec && input) raw = "";
    else if (exec) raw = idx >= 0 && idx < (int)d.execOut.size() ? d.execOut[(std::size_t)idx] : "";
    else if (input) raw = idx >= 0 && idx < (int)d.dataIn.size() ? d.dataIn[(std::size_t)idx] : "";
    else raw = idx >= 0 && idx < (int)d.dataOut.size() ? d.dataOut[(std::size_t)idx] : "";
    const std::string name = VsPinLabel(raw, exec, input);
    if (exec && input)
        return name + ": the step before this one. Drag a wire here from an event like On Start, or from the Then triangle of another step.";
    if (exec)
        return name + ": what happens next. Drag a wire from here to the next step.";
    const PinType t = input ? okay::vs::InputType(d.type, idx) : okay::vs::OutputType(d.type, idx, "");
    std::string meaning;
    if (const VsNodeDoc* doc = VsNodeDocFor(d.type))
        if (const VsPinDoc* p = VsPinDocFor(*doc, input, false, raw ? raw : "")) meaning = VsPlainText(p->meaning);
    std::string out = name + ": ";
    if (!meaning.empty()) { out += meaning; if (out.back() != '.') out += '.'; out += ' '; }
    if (input) {
        if (name == "Target") out += "Leave it empty to use this object. To act on another object, plug it in or drop it here. ";
        else out += std::string("Plug in ") + VsPinKindWords(t) + ", or type one. ";
        out += "Example: " + VsPinExample(t, name) + ".";
    } else {
        out += std::string("This gives ") + VsPinKindWords(t) + ". Plug it into an input that wants " + VsPinKindWords(t) + ". Example: " + VsPinExample(t, name) + ".";
    }
    return out;
}

// ---- 3. Needs input -----------------------------------------------------------------------------------------

/// True when the node cannot do its job without something in this input pin (nothing wired, nothing typed).
/// The checks the editor already had (Branch's condition, Look At's object ...) plus any object input other than
/// Target: an empty one means "no object", and the node would do nothing.
inline bool VsPinNeedsInput(const VsDef& d, int pin) {
    if (pin < 0 || pin >= (int)d.dataIn.size()) return false;
    if (VsRequiredWhat(d.type, pin)) return true;
    const char* nm = d.dataIn[(std::size_t)pin];
    if (!nm || !nm[0] || std::strcmp(nm, "Target") == 0) return false;
    if (std::strncmp(d.type, "Fn.", 3) == 0) return false;
    if (okay::vs::InputType(d.type, pin) != okay::vs::PinType::Object) return false;
    if (const VsNodeDoc* doc = VsNodeDocFor(d.type))
        if (const VsPinDoc* p = VsPinDocFor(*doc, true, false, nm))
            return p->fallback == "no object";
    return false;
}

/// The words in the empty box of a pin that needs something: "plug a number in here".
inline std::string VsMissingHint(const VsDef& d, int pin) {
    const okay::vs::PinType t = okay::vs::InputType(d.type, pin);
    std::string kind = VsPinKindWords(t);
    if (const char* what = VsRequiredWhat(d.type, pin)) kind = std::string("the ") + what;
    else if (t == okay::vs::PinType::Object) kind = "an object";
    return "plug " + kind + " in here";
}

// ---- 4. Next steps ------------------------------------------------------------------------------------------

/// One suggestion: a node to add after this one, and which pins to wire.
struct VsNextStep {
    std::string type, title, line;   ///< the node to add, its title, its plain sentence
    bool exec = true;                ///< true: this node's first Then pin goes into the new node's flow input
    int outPin = 0;                  ///< the pin of the node that is selected
    int inPin = 0;                   ///< for data: the input pin of the new node
};

/// A short list of what usually follows `from`, best first. Only nodes in `all` that really fit: a flow output only
/// goes into a flow input, a value only into an input whose type accepts it (the same rule as a dragged wire).
/// Order: hand-picked pairs, then the starter nodes, then other basic nodes.
inline std::vector<VsNextStep> VsNextSteps(const VsDef& from, const std::vector<VsDef>& all, std::size_t limit = 4) {
    struct Pair { const char* type; const char* follows; };   // space separated, best first
    static const Pair kPairs[] = {
        {"OnStart", "Print SetText Spawn PlaySound"},
        {"OnUpdate", "Translate Rotate SetPosition Branch"},
        {"OnKey", "Translate Rotate PlaySound Spawn"},
        {"OnClick", "PlaySound Spawn Destroy ScoreAdd"},
        {"Branch", "Print PlaySound Destroy SetActive"},
        {"Delay", "Print Spawn Destroy PlaySound"},
        {"Sequence", "Print Delay Spawn"},
        {"Const", "Print Translate SetText"},
        {"Key", "Branch Translate"},
        {"Compare", "Branch"},
        {"GetVar", "Print SetText Compare"},
        {"SetVar", "Print GetVar"},
        {"AddVar", "Print SetText GetVar"},
        {"DeltaTime", "Translate Rotate"},
        {"Add", "Print SetVar SetText"},
        {"GetPosition", "Print Compare SetPosition"},
        {"Spawn", "Print Delay Destroy"},
        {"Translate", "Print Delay PlaySound"},
    };
    std::vector<VsNextStep> out;
    auto find = [&](const char* type) -> const VsDef* {
        for (const VsDef& c : all) if (std::strcmp(c.type, type) == 0) return &c;
        return nullptr;
    };
    const bool fromEvent = !from.execIn && !from.execOut.empty();
    const bool flowOut = !from.execOut.empty();
    const bool valueOut = !flowOut && !from.dataOut.empty();
    auto tryAdd = [&](const VsDef& c) {
        if (out.size() >= limit) return;
        if (std::strcmp(c.type, from.type) == 0) return;
        for (const VsNextStep& s : out) if (s.type == c.type) return;
        if (!c.title || !c.title[0]) return;
        VsNextStep s;
        s.type = c.type; s.title = c.title; s.line = VsPlainLine(c);
        if (flowOut) {
            if (!c.execIn) return;                    // an event has no flow input
            s.exec = true; s.outPin = 0;
        } else if (valueOut) {
            s.exec = false; s.outPin = 0; s.inPin = -1;
            const okay::vs::PinType ot = okay::vs::OutputType(from.type, 0, "");
            for (int i = 0; i < (int)c.dataIn.size(); ++i) {
                if (c.dataIn[(std::size_t)i] && std::strcmp(c.dataIn[(std::size_t)i], "Target") == 0) continue;   // a value is rarely a Target
                if (okay::vs::PinCompatible(ot, okay::vs::InputType(c.type, i))) { s.inPin = i; break; }
            }
            if (s.inPin < 0) return;
        } else return;
        out.push_back(std::move(s));
    };
    for (const Pair& p : kPairs) {
        if (std::strcmp(p.type, from.type) != 0) continue;
        std::string rest = p.follows;
        while (!rest.empty()) {
            const auto sp = rest.find(' ');
            const std::string one = sp == std::string::npos ? rest : rest.substr(0, sp);
            rest = sp == std::string::npos ? std::string() : rest.substr(sp + 1);
            if (const VsDef* c = find(one.c_str())) tryAdd(*c);
        }
    }
    for (int i = 0; i < VsStarterCount() && out.size() < limit; ++i)
        if (const VsDef* c = find(kStarterNodes[i])) if (!fromEvent || c->execIn) tryAdd(*c);
    for (const VsDef& c : all) {
        if (out.size() >= limit) break;
        if (c.level == VsLevel::Basic && std::strncmp(c.type, "Fn.", 3) != 0 && !okay::vs::FindQuickBlock(c.type)) tryAdd(c);
    }
    return out;
}

// ---- 5. Common mistakes, the card, the next-step line ------------------------------------------------------

/// The line that tells a person what to do after dropping this node. Never empty.
inline std::string VsNextStepLine(const VsDef& d) {
    const bool event = !d.execIn && !d.execOut.empty();
    const bool flowOut = !d.execOut.empty();
    std::string s;
    if (event) s = "Next: drag from its Then triangle to the step that should happen.";
    else if (d.execIn) {
        s = "Next: plug the step before it into its first triangle";
        if (!d.dataIn.empty()) s += ", and fill in its inputs";
        s += flowOut ? ", then wire its Then triangle onward." : ".";
    } else if (!d.dataOut.empty()) s = "Next: drag its output into an input of another block that wants the same kind of value.";
    else s = "Next: plug it into the blocks around it.";
    return s;
}

/// Mistakes people make with this kind of node, in plain words. Always at least one.
inline std::vector<std::string> VsCommonMistakes(const VsDef& d) {
    std::vector<std::string> m;
    const bool event = !d.execIn && !d.execOut.empty();
    if (event) m.push_back("Nothing happens until you wire its Then triangle to a step.");
    else if (d.execIn) m.push_back("Nothing runs this block until a wire reaches its first triangle from an event such as On Start.");
    if (!d.execIn && !d.execOut.empty() && false) m.clear();
    if (!event && !d.execIn && !d.dataOut.empty())
        m.push_back("This only gives a value. It does nothing until you plug its output into another block.");
    for (int i = 0; i < (int)d.dataIn.size(); ++i) {
        const char* nm = d.dataIn[(std::size_t)i];
        if (nm && std::strcmp(nm, "Target") == 0) { m.push_back("Target empty means this object. Plug an object in to act on a different one."); break; }
    }
    for (int i = 0; i < (int)d.dataIn.size(); ++i)
        if (VsPinNeedsInput(d, i)) { m.push_back(std::string("Leaving ") + d.dataIn[(std::size_t)i] + " empty. This block needs it, so it does nothing without it."); break; }
    if (!d.dataIn.empty()) m.push_back("Plugging the wrong colour of pin. A number cannot go into an object input. The editor tells you why.");
    if (const VsNodeDoc* doc = VsNodeDocFor(d.type))
        if (!doc->component.empty()) {
            std::string c = doc->component;
            for (char& ch : c) if (ch == '|') ch = '/';
            m.push_back("Forgetting the " + c + " part on the object. The node says Needs it and offers a button to add it.");
        }
    if (d.param && d.paramHint && d.paramHint[0]) m.push_back(std::string("Leaving the text box empty or mistyped (") + d.paramHint + ").");
    if (m.empty()) m.push_back("Dropping it and not wiring it. Connect it to the blocks before and after it.");
    for (std::string& one : m) one = VsPlainText(one);
    return m;
}

/// The "What is this?" card for one node.
struct VsHelpCard {
    std::string title, does;
    std::vector<std::pair<std::string, std::string>> plugs;   ///< input pin name -> what to plug in
    std::vector<std::string> chain;                           ///< a tiny example graph: step titles in order
    std::string example;                                      ///< the example in words
    std::vector<std::string> mistakes;
    std::string next;
};

inline VsHelpCard VsHelpCardFor(const VsDef& d, const std::vector<VsDef>* all = nullptr) {
    VsHelpCard c;
    c.title = d.title ? d.title : d.type;
    c.does = VsPlainLine(d);
    for (int i = 0; i < (int)d.dataIn.size(); ++i) {
        const char* nm = d.dataIn[(std::size_t)i];
        if (!nm || !nm[0]) continue;
        const okay::vs::PinType t = okay::vs::InputType(d.type, i);
        std::string what;
        if (std::strcmp(nm, "Target") == 0) what = "An object to act on. Leave it empty to use this object.";
        else what = std::string("Plug in ") + VsPinKindWords(t) + ", or type one. Example: " + VsPinExample(t, nm) + ".";
        if (VsPinNeedsInput(d, i)) what += " Needed.";
        c.plugs.push_back({nm, what});
    }
    if (d.param && d.paramHint && d.paramHint[0]) c.plugs.push_back({"Text box", std::string("Type ") + d.paramHint + "."});
    if (c.plugs.empty()) c.plugs.push_back({"Inputs", d.execIn ? "Only the flow wire from the step before." : (d.execOut.empty() ? "Nothing. It just gives a value." : "Nothing. It starts a flow.")});
    // The tiny example: what comes before, this node, what usually comes after.
    const bool event = !d.execIn && !d.execOut.empty();
    if (d.execIn) c.chain.push_back("On Start");
    c.chain.push_back(c.title);
    if (all) {
        const auto next = VsNextSteps(d, *all, 1);
        if (!next.empty()) c.chain.push_back(next[0].title);
    } else if (event) c.chain.push_back("Say Text");
    const char* ex = VsNodeExample(d.type);
    c.example = ex ? ex : "";
    if (c.example.empty()) {
        if (const VsNodeDoc* doc = VsNodeDocFor(d.type)) c.example = VsPlainText(doc->example);
    }
    if (c.example.empty()) c.example = "Press Try this node to see it work.";
    while (c.chain.size() < 2) c.chain.push_back("Say Text");
    c.mistakes = VsCommonMistakes(d);
    c.next = VsPlainText(VsNextStepLine(d));
    return c;
}

// ---- 6. Try this node ---------------------------------------------------------------------------------------

/// A small working graph (the same text a saved .okayvs file holds) for one node: an event, the node with sensible
/// values typed in, and a Say Text step so Play shows something in the log. `build` is the editor's graph type so the
/// text is written by the same code that saves a graph. `GraphT` is VsEditGraph.
template <typename GraphT>
inline std::string VsTryGraph(const VsDef& d, GraphT& g) {
    using okay::vs::PinType;
    g.clear();
    const bool event = !d.execIn && !d.execOut.empty();
    const bool value = !d.execIn && d.execOut.empty() && !d.dataOut.empty();
    int mainId;
    auto fillInputs = [&](int id) {
        for (int i = 0; i < (int)d.dataIn.size(); ++i) {
            const char* nm = d.dataIn[(std::size_t)i];
            if (!nm || !nm[0] || std::strcmp(nm, "Target") == 0) continue;
            const PinType t = okay::vs::InputType(d.type, i);
            if (t == PinType::Object || t == PinType::Vec3 || t == PinType::List || t == PinType::Map || t == PinType::Color) continue;
            if (std::strncmp(d.type, "Fn.", 3) == 0) continue;
            for (auto& n : g.nodes) if (n.id == id && !n.pinDefault.count(i)) n.pinDefault[i] = VsPinExample(t, nm);
        }
    };
    if (event) {
        mainId = g.add(d.type, 40.0f, 40.0f);
        const int say = g.add("Print", 340.0f, 40.0f);
        for (auto& n : g.nodes) if (n.id == say) n.pinDefault[0] = std::string(d.title ? d.title : d.type) + " happened!";
        g.linkExec(mainId, 0, say);
    } else if (d.execIn) {
        const int ev = g.add("OnStart", 40.0f, 40.0f);
        mainId = g.add(d.type, 300.0f, 40.0f);
        fillInputs(mainId);
        g.linkExec(ev, 0, mainId);
        if (!d.execOut.empty()) {
            const int say = g.add("Print", 620.0f, 40.0f);
            for (auto& n : g.nodes) if (n.id == say) n.pinDefault[0] = std::string(d.title ? d.title : d.type) + " finished.";
            g.linkExec(mainId, 0, say);
        }
    } else if (value) {
        const int ev = g.add("OnStart", 40.0f, 40.0f);
        const int say = g.add("Print", 620.0f, 40.0f);
        mainId = g.add(d.type, 300.0f, 160.0f);
        fillInputs(mainId);
        for (auto& n : g.nodes) if (n.id == say) n.pinDefault[0] = std::string(d.title ? d.title : d.type) + " gives:";
        g.linkExec(ev, 0, say);
        g.linkData(say, 1, mainId, 0);
    } else {
        mainId = g.add(d.type, 40.0f, 40.0f);
    }
    return g.toText();
}

// ---- 7. What do you want to do? -----------------------------------------------------------------------------

inline const char* const kVsIntents[] = {
    "Start something", "React to input", "Move things", "Show messages", "Play sounds",
    "Make choices", "Wait and repeat", "Remember things", "Do maths", "Create and remove things",
    "Score and health", "Other things",
};
inline constexpr int kVsIntentCount = (int)(sizeof kVsIntents / sizeof kVsIntents[0]);

/// One-line help for each heading (a tooltip in the list).
inline const char* const kVsIntentBlurbs[] = {
    "Blocks that begin a script: when the game starts, every frame, on a timer.",
    "Blocks for keys, the mouse, touch and buttons.",
    "Move, turn, follow, jump and change where things are.",
    "Show words, numbers and screen items.",
    "Play a sound or music.",
    "Check something and choose a path: if, compare, and, or, not.",
    "Wait, do something later, or do it again and again.",
    "Keep a number or word and read it back later.",
    "Add, subtract, random numbers and distances.",
    "Make a copy of something, hide it, or take it away.",
    "Score, health, winning and losing.",
    "Everything else.",
};

/// The heading for a node (or an action step) from its type name, its category and whether it is an event.
/// Decided from the words in the name, so a new node lands somewhere sensible without a table row.
inline int VsIntentForName(const std::string& t, int cat, bool event) {
    auto has = [&](const char* w) { return VsNameHas(t, w); };
    auto is = [&](const char* w) { return t == w; };
    if (has("score") || has("health") || has("damage") || has("gameover") || has("lives") || has("win") || has("lose")) return 10;
    if (event && (has("key") || has("click") || has("mouse") || has("tap") || has("swipe") || has("pinch") || has("action") || has("press") || has("touch"))) return 1;
    if (cat == CAT_INPUT) return 1;
    if (event && (is("OnStart") || is("OnUpdate") || is("OnLateUpdate") || is("OnFixedUpdate") || is("OnTimer") || is("OnEnable") || is("OnDisable") || is("OnDestroy"))) return 0;
    if (cat == CAT_EVENT) return event ? 0 : 11;
    if (has("sound") || has("audio") || has("music") || has("clip") || has("volume")) return 4;
    if (has("print") || has("text") || has("showui") || has("hideui") || has("label") || has("message") || has("format") || has("say")) return 3;
    if (has("translate") || has("move") || has("rotate") || has("position") || has("teleport") || has("follow") || has("patrol") || has("chase") ||
        has("flee") || has("jump") || has("velocity") || has("force") || has("look") || has("scale") || has("spin") || has("bob") || cat == CAT_XFORM) return 2;
    if (has("spawn") || has("destroy") || has("setactive") || has("clone") || has("instantiate")) return 9;
    if (cat == CAT_VAR || has("var") || has("savevalue") || has("loadvalue")) return 7;
    if (cat == CAT_LOGIC || is("Branch") || has("switch") || has("compare") || has("chance")) return 5;
    if (is("Delay") || is("Wait") || is("Timer") || is("Repeat") || is("ForLoop") || is("Once") || is("Cooldown") || is("DoN") ||
        is("Sequence") || is("FlipFlop") || is("Gate") || has("loop") || cat == CAT_FLOW) return 6;
    if (cat == CAT_MATH) return 8;
    return 11;
}

inline int VsIntentFor(const VsDef& d) { return VsIntentForName(d.type, d.cat, !d.execIn && !d.execOut.empty()); }

// ---- 8. Plain titles: they live in kVsNames (VisualScriptNames.hpp), the one title table.

}  // namespace okay::editor::vsed
