#pragma once
// "What does this node do, what does it need, what does it give, and what does
// this pin connect to?" in plain words, for tooltips in the Visual Script editor.
// Everything here is computed from the node table and PinCompatible, so a new node
// or a changed wire rule shows up in the text without a second copy to edit.
// docs/visual_script_clarity_audit.md is the audit this comes from.
// Included by VisualScriptEditor.hpp, after the node table (Registry, VsDef) it reads.
#include "okay/VisualScript/VsTypes.hpp"

#include <cstring>
#include <string>
#include <vector>

namespace okay::editor::vsed {

/// The types a pin can carry, in the order the legend lists them (Any is separate:
/// it takes and gives everything and is never checked).
inline const std::vector<okay::vs::PinType>& VsConcretePinTypes() {
    using okay::vs::PinType;
    static const std::vector<PinType> t = {PinType::Number, PinType::Bool, PinType::Text, PinType::Vec3,
                                           PinType::Object, PinType::List, PinType::Map};
    return t;
}

/// What a wire from an output of type `from` into an input of type `to` does to the
/// value when the two types differ and PinCompatible accepts it. nullptr when nothing
/// changes (same type, or either end is Any).
inline const char* VsConversionNote(okay::vs::PinType from, okay::vs::PinType to) {
    using okay::vs::PinType;
    if (from == to || from == PinType::Any || to == PinType::Any) return nullptr;
    if (!okay::vs::PinCompatible(from, to)) return nullptr;
    if (to == PinType::Text) return "is written out as text";
    if (from == PinType::Number && to == PinType::Bool) return "0 is false, anything else is true";
    if (from == PinType::Bool && to == PinType::Number) return "false is 0, true is 1";
    if (from == PinType::Text && to == PinType::Object) return "is read as an object's name";
    if (from == PinType::Object && to == PinType::Bool) return "is true while the object still exists";
    return "is converted";
}

/// Concrete input types an output of type `from` can be wired into.
inline std::vector<okay::vs::PinType> VsFeedsTypes(okay::vs::PinType from) {
    std::vector<okay::vs::PinType> r;
    for (okay::vs::PinType t : VsConcretePinTypes())
        if (okay::vs::PinCompatible(from, t)) r.push_back(t);
    return r;
}

/// Concrete output types that can be wired into an input of type `to`.
inline std::vector<okay::vs::PinType> VsComesFromTypes(okay::vs::PinType to) {
    std::vector<okay::vs::PinType> r;
    for (okay::vs::PinType t : VsConcretePinTypes())
        if (okay::vs::PinCompatible(t, to)) r.push_back(t);
    return r;
}

inline std::string VsJoinTypeNames(const std::vector<okay::vs::PinType>& v) {
    std::string s;
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (i) s += (i + 1 == v.size()) ? " and " : ", ";
        s += okay::vs::PinTypeName(v[i]);
    }
    return s;
}

struct VsPinPartner {
    std::string node;   ///< the title shown on the node
    std::string pin;    ///< the pin on it
};

/// A few everyday nodes that have a pin this one can be wired to. `thisIsOutput`
/// means the pin is an output, so the partners are inputs; otherwise they are outputs.
/// Same-type pins come first, then ones that convert. Only hand-written nodes of the
/// Basic and Intermediate levels, one pin per node, never an Any pin or a Target.
inline std::vector<VsPinPartner> VsPinPartners(okay::vs::PinType t, bool thisIsOutput, std::size_t limit = 3) {
    using okay::vs::PinType;
    std::vector<VsPinPartner> out;
    for (int pass = 0; pass < 2 && out.size() < limit; ++pass) {
        for (int lvl = 0; lvl < 2 && out.size() < limit; ++lvl) {
            for (const VsDef& d : Registry()) {
                if (out.size() >= limit) break;
                if (std::strchr(d.type, '.') || (int)d.level != lvl || !d.title) continue;
                bool seen = false;
                for (const VsPinPartner& p : out) if (p.node == d.title) seen = true;
                if (seen) continue;
                const auto& pins = thisIsOutput ? d.dataIn : d.dataOut;
                for (int i = 0; i < (int)pins.size(); ++i) {
                    if (!pins[i] || !pins[i][0] || std::strcmp(pins[i], "Target") == 0) continue;
                    const PinType other = thisIsOutput ? okay::vs::InputType(d.type, i) : okay::vs::OutputType(d.type, i, "");
                    if (other == PinType::Any) continue;
                    if ((pass == 0) != (other == t)) continue;
                    if (!(thisIsOutput ? okay::vs::PinCompatible(t, other) : okay::vs::PinCompatible(other, t))) continue;
                    out.push_back({d.title, pins[i]});
                    break;
                }
            }
        }
    }
    return out;
}

/// The "connects to / comes from" part of a pin tooltip. `exec` is a flow pin.
inline std::string VsPinConnectText(okay::vs::PinType t, bool isOutput, bool exec) {
    using okay::vs::PinType;
    if (exec) {
        return isOutput
            ? "Connects to: the flow input (triangle) of the step that should run next. A flow wire never goes into a value pin."
            : "Comes from: an event, or the flow output (triangle) of the step before this one. A flow wire never comes from a value pin.";
    }
    if (t == PinType::Any)
        return isOutput ? "Connects to: any value input. This pin's type is not checked."
                        : "Comes from: any value output. This pin's type is not checked.";
    std::string s;
    const std::vector<PinType> ok = isOutput ? VsFeedsTypes(t) : VsComesFromTypes(t);
    std::vector<PinType> no;
    for (PinType c : VsConcretePinTypes()) {
        bool in = false;
        for (PinType o : ok) if (o == c) in = true;
        if (!in) no.push_back(c);
    }
    s += isOutput ? "Connects to: " : "Comes from: ";
    if (ok.size() == VsConcretePinTypes().size()) {
        s += isOutput ? "any value input." : "any value output.";   // Text takes everything
    } else {
        s += VsJoinTypeNames(ok);
        s += isOutput ? " inputs, and any-type inputs." : " outputs, and any-type outputs.";
    }
    // What happens on the way. Types that share the same effect share a line ("A, B and C written out as text").
    struct Conv { std::string note; PinType to; std::vector<PinType> from; };
    std::vector<Conv> convs;
    for (PinType o : ok) {
        if (o == t) continue;
        const PinType from = isOutput ? t : o, to = isOutput ? o : t;
        const char* n = VsConversionNote(from, to);
        if (!n) continue;
        bool placed = false;
        for (Conv& c : convs)
            if (c.note == n && c.to == to) { c.from.push_back(from); placed = true; break; }
        if (!placed) convs.push_back({n, to, {from}});
    }
    for (const Conv& c : convs)
        s += "\n  " + VsJoinTypeNames(c.from) + " going into " + okay::vs::PinTypeName(c.to) + " " + c.note + ".";
    const std::vector<VsPinPartner> partners = VsPinPartners(t, isOutput);
    if (!partners.empty()) {
        s += isOutput ? "\nTry: " : "\nFor example: ";
        for (std::size_t i = 0; i < partners.size(); ++i) {
            if (i) s += ", ";
            s += partners[i].node + " (" + partners[i].pin + ")";
        }
    }
    if (!no.empty()) {
        s += std::string("\nNot ") + (isOutput ? "into: " : "from: ") + VsJoinTypeNames(no) + ".";
        for (PinType o : no) {
            const std::string h = isOutput ? okay::vs::ConversionHint(t, o) : okay::vs::ConversionHint(o, t);
            if (!h.empty()) {
                s += std::string("\n  ") + (isOutput ? okay::vs::PinTypeName(t) : okay::vs::PinTypeName(o)) + " to " +
                     (isOutput ? okay::vs::PinTypeName(o) : okay::vs::PinTypeName(t)) + ": " + h + ".";
                break;   // one tip is enough; the rest repeat it
            }
        }
    }
    return s;
}

/// What a node does, needs and gives, each as a short phrase.
struct VsNodeBrief {
    std::string does, needs, gives;
    /// "Does: ... Needs: ... Gives: ..." on one line.
    std::string Line() const { return "Does: " + does + " Needs: " + needs + " Gives: " + gives; }
};

/// The first sentence of `s`; a very short one ("This object.") takes the next one too.
inline std::string VsFirstSentence(const std::string& s, std::size_t maxLen = 120) {
    auto end = [&](std::size_t from) -> std::size_t {
        for (std::size_t i = from; i + 1 < s.size(); ++i)
            if ((s[i] == '.' || s[i] == '!' || s[i] == '?') && s[i + 1] == ' ') return i + 1;
        return s.size();
    };
    std::size_t e = end(0);
    if (e < 16 && e < s.size()) e = end(e + 1);
    std::string r = s.substr(0, e);
    if (r.size() > maxLen) { r.resize(maxLen - 3); while (!r.empty() && r.back() == ' ') r.pop_back(); r += "..."; }
    return r;
}

inline VsNodeBrief VsNodeBriefFor(const VsDef& d) {
    VsNodeBrief b;
    const VsNodeDoc* doc = VsNodeDocFor(d.type);
    const bool event = !d.execIn && !d.execOut.empty();
    const bool builtin = std::strncmp(d.type, "Fn.", 3) == 0;
    // Does
    if (builtin) {
        b.does = std::string("Runs the text-script function ") + (d.type + 3) + ".";
    } else if (doc && !doc->description.empty()) {
        std::string text = doc->description;
        const std::string echo = std::string(d.title ? d.title : "") + ". ";   // "Wait Seconds. Pauses..." names itself first
        if (echo.size() > 2 && text.compare(0, echo.size(), echo) == 0 && text.size() > echo.size()) text = text.substr(echo.size());
        b.does = VsFirstSentence(text);
    } else {
        b.does = std::string(d.title ? d.title : d.type) + ".";
    }
    if (b.does.empty() || b.does.back() != '.') b.does += ".";
    // Needs
    std::vector<std::string> need;
    if (event) need.push_back("nothing wired in; it starts the flow");
    if (d.execIn) need.push_back("a flow wire from an event or the step before");
    if (const char* w = VsRequiredWhat(d.type, -1)) need.push_back(std::string("a ") + w + " in its text box");
    for (int i = 0; i < (int)d.dataIn.size(); ++i) {
        const std::string name = d.dataIn[i] ? d.dataIn[i] : "";
        if (name.empty()) continue;
        std::string one = name;
        if (builtin) { need.push_back(name + " (optional)"); continue; }
        if (name == "Target") { need.push_back("Target (optional: empty means this object)"); continue; }
        if (const char* w = VsRequiredWhat(d.type, i)) one += std::string(" (required: ") + w + ")";
        else one += std::string(" (") + okay::vs::PinTypeName(okay::vs::InputType(d.type, i)) + ")";
        need.push_back(one);
    }
    if (d.param && d.paramHint && d.paramHint[0] && !VsRequiredWhat(d.type, -1))
        need.push_back(std::string("its text box: ") + d.paramHint);
    if (doc && !doc->component.empty() && doc->component.find('|') == std::string::npos)
        need.push_back("a " + doc->component + " on the object");
    else if (doc && !doc->component.empty()) {
        std::string c = doc->component;
        for (char& ch : c) if (ch == '|') ch = '/';
        need.push_back("a " + c + " on the object");
    }
    if (need.empty()) need.push_back("nothing");
    for (std::size_t i = 0; i < need.size(); ++i) b.needs += (i ? "; " : "") + need[i];
    b.needs += ".";
    // Gives
    std::vector<std::string> give;
    for (int i = 0; i < (int)d.dataOut.size(); ++i)
        if (d.dataOut[i] && d.dataOut[i][0])
            give.push_back(std::string(d.dataOut[i]) + " (" + okay::vs::PinTypeName(okay::vs::OutputType(d.type, i, "")) + ")");
    std::vector<std::string> flows;
    for (const char* e : d.execOut) if (e && e[0]) flows.push_back(e);
    if (!flows.empty()) {
        std::string f = "flow out: ";
        for (std::size_t i = 0; i < flows.size(); ++i) f += (i ? ", " : "") + flows[i];
        give.push_back(f);
    } else if (!d.execOut.empty()) {
        give.push_back(event ? "the flow it starts" : "the flow continues");
    }
    if (give.empty()) give.push_back(d.execIn ? "nothing back; it only acts" : "nothing");
    for (std::size_t i = 0; i < give.size(); ++i) b.gives += (i ? "; " : "") + give[i];
    b.gives += ".";
    return b;
}

}  // namespace okay::editor::vsed
