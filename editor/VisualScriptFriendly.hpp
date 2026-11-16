#pragma once
// Display names for nodes this branch adds on top of editor/VisualScriptNames.hpp:
// text-script builtins (Fn.*) and engine bindings (Transform.Translate/3).
// The file token stays the raw identifier. People see a title that passes
// LintVsTitle, and search still matches the raw name (docs/visual_scripting_naming.md).
#include "VisualScriptNames.hpp"
#include "VisualScriptNodeDocs.hpp"
#include "VisualScriptNodeHelp.hpp"
#include "okay/VisualScript/VsBuiltins.hpp"
#include "okay/VisualScript/VsApi.hpp"
#include <cctype>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

namespace okay::editor::vsed {

inline const char* VsIntern(std::string s) {
    static std::deque<std::string> keep;
    keep.push_back(std::move(s));
    return keep.back().c_str();
}

inline std::string VsLowerCopy(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

inline bool VsStarts(const std::string& s, const char* p) { return s.rfind(p, 0) == 0; }

// One identifier token, lowercase, into ordinary words. Digits never survive.
// `pin` is true for a pin name: there a, b, c, d are the first to fourth input (lerp(a, b, t)). In a node title
// they are not (#888 finding 10: the "d" of "Physics2D" made "Physics Fourth Raycast"), so a title drops them.
inline void VsExpandToken(const std::string& low, std::vector<std::string>& out, bool pin = true) {
    const char* repl = nullptr;
    struct Rep { const char* from; const char* to; };
    static const Rep k[] = {
        {"abs", "absolute value"}, {"amt", "amount"}, {"ang", "angle"}, {"anim", "animation"},
        {"api", "script"}, {"arg", "argument"}, {"args", "arguments"}, {"atan", "arc tangent"},
        {"bg", "background"}, {"bool", "true or false"}, {"bt", "behavior tree"}, {"btn", "button"},
        {"cam", "camera"}, {"cfg", "settings"}, {"colour", "color"}, {"cos", "cosine"},
        {"cur", "current"}, {"deg", "degrees"}, {"dest", "destination"}, {"dir", "direction"},
        {"dist", "distance"}, {"dmg", "damage"}, {"dst", "distance"}, {"dt", "seconds"},
        {"euler", "rotation"}, {"fg", "foreground"}, {"float", "number"}, {"fov", "field of view"},
        {"fps", "frames per second"}, {"gameobject", "object"}, {"hp", "health"}, {"id", "identifier"},
        {"idx", "index"}, {"ik", "inverse kinematics"}, {"int", "number"}, {"len", "length"},
        {"lerp", "blend between"}, {"mag", "length"}, {"magnitude", "length"}, {"max", "largest"},
        {"min", "smallest"}, {"msg", "message"}, {"nan", "not a number"}, {"nav", "navigation"}, {"no", "not"},
        {"norm", "unit"}, {"num", "number"}, {"obj", "object"}, {"ok", "okay"}, {"old", "previous"},
        {"pix", "pixels"}, {"pos", "position"}, {"pow", "power"}, {"prefab", "saved object"},
        {"prev", "previous"}, {"px", "pixels"}, {"quat", "rotation"}, {"rad", "radians"},
        {"ret", "result"}, {"rgb", "color"}, {"rigidbody", "physics body"}, {"rot", "rotation"},
        {"rpc", "network message"}, {"sfx", "sound"}, {"sin", "sine"}, {"spd", "speed"},
        {"sqr", "square"}, {"sqrt", "square root"}, {"src", "source"}, {"str", "text"},
        {"string", "text"}, {"tan", "tangent"}, {"tmp", "temporary"}, {"transform", "object"},
        {"txt", "text"}, {"ui", "screen"}, {"uv", "coordinates"}, {"val", "value"},
        {"var", "value"}, {"vec", "vector"}, {"vec3", "vector"}, {"vel", "velocity"},
    };
    for (const Rep& r : k) if (low == r.from) { repl = r.to; break; }
    if (!repl && low.size() == 1 && !(!pin && low[0] >= 'a' && low[0] <= 'd')) {
        switch (low[0]) {
            case 'a': repl = "first"; break; case 'b': repl = "second"; break;
            case 'c': repl = "third"; break; case 'd': repl = "fourth"; break;
            case 'e': repl = "ease"; break; case 'f': repl = "force"; break;
            case 'g': repl = "green"; break; case 'h': repl = "height"; break;
            case 'i': repl = "index"; break; case 'k': repl = "key"; break;
            case 'm': repl = "amount"; break; case 'n': repl = "count"; break;
            case 'o': repl = "object"; break; case 'p': repl = "point"; break;
            case 'r': repl = "red"; break; case 's': repl = "seconds"; break;
            case 't': repl = "time"; break; case 'u': repl = "upward"; break;
            case 'v': repl = "value"; break; case 'w': repl = "width"; break;
            case 'x': repl = "right"; break; case 'y': repl = "vertical"; break;
            case 'z': repl = "forward"; break;
            default: break;
        }
    }
    if (repl) {
        std::string cur;
        for (const char* p = repl; ; ++p) {
            if (*p == ' ' || *p == '\0') {
                if (!cur.empty()) { out.push_back(cur); cur.clear(); }
                if (*p == '\0') break;
            } else cur += *p;
        }
        return;
    }
    if (low.size() < 3 && !VsSmallWord(low)) return;
    if (VsBannedWord(low)) { out.push_back("value"); return; }
    out.push_back(low);
}

// Splits `ident` into words and appends them (nothing when it has no words).
inline void VsSplitWords(const std::string& ident, std::vector<std::string>& words, bool pin = true) {
    std::string cur;
    auto flush = [&]() {
        if (cur.empty()) return;
        VsExpandToken(VsLowerCopy(cur), words, pin);
        cur.clear();
    };
    for (std::size_t i = 0; i < ident.size(); ++i) {
        const unsigned char c = (unsigned char)ident[i];
        if (c == '_' || c == '.' || c == '-' || c == ' ') { flush(); continue; }
        if (std::isdigit(c)) { flush(); continue; }
        if (std::isupper(c) && !cur.empty()) {
            const unsigned char prev = (unsigned char)cur.back();
            const unsigned char next = (i + 1 < ident.size()) ? (unsigned char)ident[i + 1] : 0;
            if (std::islower(prev) || (std::isupper(prev) && std::islower(next))) flush();
        }
        if (std::isalpha(c)) cur += (char)c;
        else flush();
    }
    flush();
}

inline std::vector<std::string> VsWordsOf(const std::string& ident, bool pin = true) {
    std::vector<std::string> words;
    VsSplitWords(ident, words, pin);
    if (words.empty()) words.push_back("value");
    return words;
}

inline std::string VsJoinWords(const std::vector<std::string>& words) {
    std::string out;
    for (std::size_t i = 0; i < words.size(); ++i) {
        const std::string low = VsLowerCopy(words[i]);
        if (low.empty()) continue;
        if (!out.empty()) out += ' ';
        if (i > 0 && VsSmallWord(low)) { out += low; continue; }
        std::string w = low;
        w[0] = (char)std::toupper((unsigned char)w[0]);
        out += w;
    }
    return out;
}

inline void VsDropLead(std::vector<std::string>& words, const char* lead) {
    if (!words.empty() && VsLowerCopy(words[0]) == lead) words.erase(words.begin());
    if (words.empty()) words.push_back("value");
}

// A few members read better than the generic split. Titles already follow the page.
inline const char* VsSpecialMemberTitle(const std::string& member, bool setter) {
    struct Spec { const char* member; bool setter; const char* title; };
    static const Spec k[] = {
        {"up", false, "Get Upward Direction"},
        {"down", false, "Get Down Direction"},
        {"left", false, "Get Left Direction"},
        {"right", false, "Get Right Direction"},
        {"forward", false, "Get Forward Direction"},
        {"back", false, "Get Back Direction"},
        {"zero", false, "Get Zero Vector"},
        {"one", false, "Get One Vector"},
        {"magnitude", false, "Get Length"},
        {"sqrMagnitude", false, "Get Square Length"},
        {"normalized", false, "Get Unit Direction"},
        {"eulerAngles", false, "Get Rotation Angles"},
        {"eulerAngles", true, "Set Rotation Angles"},
        {"localEulerAngles", false, "Get Local Rotation Angles"},
        {"localEulerAngles", true, "Set Local Rotation Angles"},
        {"deltaTime", false, "Get Frame Time"},
        {"fixedDeltaTime", false, "Get Physics Frame Time"},
        {"timeScale", false, "Get Game Speed"},
        {"timeScale", true, "Set Game Speed"},
        {"gameObject", false, "Get Object"},
        {"LookAt", false, "Look at Point"},
        {"Min", false, "Smaller of Two"},
        {"Max", false, "Larger of Two"},
        {"Abs", false, "Absolute Value"},
        {"Lerp", false, "Blend Between"},
        {"Clamp", false, "Keep Inside Range"},
        {"Clamp01", false, "Keep Between Zero and One"},
        {"Floor", false, "Round Down"},
        {"Ceil", false, "Round Upward"},
        {"Round", false, "Round to Nearest"},
        {"Sqrt", false, "Square Root"},
        {"Pow", false, "Raise to a Power"},
        {"Sign", false, "Sign of Number"},
        {"Atan2", false, "Angle From Axes"},
    };
    for (const Spec& s : k)
        if (s.setter == setter && member == s.member) return s.title;
    return nullptr;
}

inline VsNameKind VsKindOfTitle(const std::string& title, bool exec) {
    if (VsStarts(title, "On ")) return VsNameKind::Event;
    if (VsStarts(title, "Is ") || VsStarts(title, "Has ")) return VsNameKind::Question;
    if (VsStarts(title, "Get ")) return VsNameKind::Reading;
    return exec ? VsNameKind::Action : VsNameKind::Calculation;
}

inline std::string VsFriendlyApiTitleString(const okay::vs::VsApiEntry& e) {
    const bool setter = !e.type.empty() && e.type.back() == '=';
    const bool isMethod = e.type.find('/') != std::string::npos;
    const bool prop = !isMethod && !setter;
    const bool boolOut = e.outs.size() == 1 && e.outs[0].type == okay::vs::PinType::Bool;
    if (const char* special = VsSpecialMemberTitle(e.member, setter)) return special;

    std::vector<std::string> words = VsWordsOf(e.member);
    const std::string lead = words.empty() ? std::string() : VsLowerCopy(words[0]);
    std::string title;
    if (setter) {
        if (lead == "is" || lead == "has" || lead == "get" || lead == "on") VsDropLead(words, lead.c_str());
        else VsDropLead(words, "set");
        title = "Set " + VsJoinWords(words);
    } else if (lead == "on") {
        title = VsJoinWords(words);
    } else if (!e.exec && (lead == "is" || lead == "has")) {
        title = VsJoinWords(words);
    } else if (!e.exec && prop && boolOut) {
        title = "Is " + VsJoinWords(words);
    } else if (!e.exec && (lead == "get" || prop)) {
        VsDropLead(words, "get");
        title = "Get " + VsJoinWords(words);
    } else if (e.exec) {
        if (lead == "get") { VsDropLead(words, "get"); title = "Read " + VsJoinWords(words); }
        else if (lead == "is") { VsDropLead(words, "is"); title = "Check If " + VsJoinWords(words); }
        else if (lead == "has") { VsDropLead(words, "has"); title = "Check for " + VsJoinWords(words); }
        else if (lead == "on") { VsDropLead(words, "on"); title = "React to " + VsJoinWords(words); }
        else title = VsJoinWords(words);
    } else {
        title = VsJoinWords(words);
    }
    return title;
}

inline const char* VsFriendlyApiTitle(const okay::vs::VsApiEntry& e) {
    return VsIntern(VsFriendlyApiTitleString(e));
}

// The title words of a text-script builtin name, keeping what its digits mean (#888 finding 10). Before, digits
// were dropped, so box_cast3 got the title of box_cast and the palette hid it as a duplicate.
//   * A name part spelled with digits that mean words (min3 = smallest of three numbers, deg2rad), or a short part
//     that would otherwise be dropped (nx), uses this table.
//   * A 2 or 3 that ends a name part, alone or followed by a single d ("Physics2D", "create_object_3d", "box_cast3",
//     "ray3_x"), says how many dimensions: `dims` is set to it, and the caller says "in Two/Three Dimensions".
//   * Any other digit is dropped, as before.
// Single letters are title words here, not pin words (VsExpandToken with pin = false).
inline std::vector<std::string> VsBuiltinTitleWords(const std::string& builtin, int& dims) {
    struct Num { const char* part; const char* words; };   // a whole name part and its words
    static const Num kNum[] = {
        {"atan2", "angle from axes"}, {"deg2rad", "degrees to radians"}, {"rad2deg", "radians to degrees"},
        {"min3", "smallest of three"}, {"max3", "largest of three"}, {"clamp01", "keep between zero and one"},
        {"log10", "logarithm base ten"},
        // A ray's hit normal (ray3_nx): two letters would be dropped, so ray3_nx, ray3_ny and ray3_nz shared a title.
        {"nx", "normal right"}, {"ny", "normal vertical"}, {"nz", "normal forward"},
    };
    dims = 0;
    std::vector<std::string> words;
    std::string part;
    auto flushPart = [&]() {
        if (part.empty()) return;
        const std::string low = VsLowerCopy(part);
        for (const Num& n : kNum) {
            if (low != n.part) continue;
            std::string w;
            for (const char* c = n.words; ; ++c) {
                if (*c == ' ' || *c == '\0') { if (!w.empty()) words.push_back(w); w.clear(); if (!*c) break; }
                else w += *c;
            }
            part.clear();
            return;
        }
        // Take the dimension marks out of the part, then split what is left as before.
        std::string rest;
        for (std::size_t i = 0; i < part.size();) {
            const unsigned char c = (unsigned char)part[i];
            if (!std::isdigit(c)) { rest += part[i++]; continue; }
            std::size_t j = i;
            while (j < part.size() && std::isdigit((unsigned char)part[j])) ++j;
            const std::string run = part.substr(i, j - i);
            const bool endsHere = j == part.size();
            const bool endsWithD = j + 1 == part.size() && (part[j] == 'd' || part[j] == 'D');
            if ((run == "2" || run == "3") && (endsHere || endsWithD)) {
                dims = run[0] - '0';
                i = endsWithD ? j + 1 : j;
                if (!rest.empty()) rest += '_';
                continue;
            }
            rest += '_';   // another number: dropped, but it still ends a word
            i = j;
        }
        VsSplitWords(rest, words, false);   // a part that was only a dimension mark ("3d") adds nothing
        part.clear();
    };
    for (char c : builtin) {
        if (c == '_' || c == '.' || c == '-' || c == ' ') flushPart();
        else part += c;
    }
    flushPart();
    if (words.empty()) words.push_back("value");
    return words;
}

inline std::string VsFriendlyBuiltinTitleString(const std::string& builtin) {
    // is_ / get_ / on_ stay questions, readings, and events. The node also has a
    // flow pin, and the title is still the kind of value it gives back.
    int dims = 0;
    std::string title = VsJoinWords(VsBuiltinTitleWords(builtin, dims));
    if (dims == 2) title += " in Two Dimensions";
    else if (dims == 3) title += " in Three Dimensions";
    return title;
}

inline const char* VsFriendlyBuiltinTitle(const std::string& builtin) {
    return VsIntern(VsFriendlyBuiltinTitleString(builtin));
}

inline bool VsApiPinsAreColor(const okay::vs::VsApiEntry& e) {
    const std::string member = VsLowerCopy(e.member);
    if (member.find("color") != std::string::npos || member.find("colour") != std::string::npos ||
        member.find("rgb") != std::string::npos)
        return true;
    int n = 0;
    for (const okay::vs::VsApiPin& p : e.ins) {
        const std::string low = VsLowerCopy(p.name);
        if (low == "r" || low == "g" || low == "b") ++n;
    }
    return n >= 2;
}

inline std::string VsFriendlyPin(const std::string& raw, bool color) {
    const std::string low = VsLowerCopy(raw);
    if (low == "target") return "Target";
    if (low == "result") return "Result";
    if (color) {
        if (low == "r") return "Red";
        if (low == "g") return "Green";
        if (low == "b") return "Blue";
        if (low == "a") return "Opacity";
    }
    if (low == "a") return "First";
    if (low == "b") return "Second";
    if (low == "c") return "Third";
    if (low == "d") return "Fourth";
    if (low == "x") return "Right";
    if (low == "y") return "Up";
    if (low == "z") return "Forward";
    if (low == "t") return "Time";
    if (low == "s") return "Seconds";
    if (low == "n") return "Count";
    if (low == "i") return "Index";
    if (low == "r") return "Red";
    if (low == "g") return "Green";
    if (low == "u") return "Up";
    if (low == "v") return "Value";
    if (low == "w") return "Width";
    if (low == "h") return "Height";
    if (low == "p") return "Point";
    if (low == "o") return "Object";
    if (low == "m") return "Amount";
    if (low == "k") return "Key";
    if (low == "f") return "Force";
    const std::string title = VsJoinWords(VsWordsOf(raw));
    if (!title.empty() && LintVsPin(title.c_str()).empty()) return title;
    return "Value";
}

inline void VsMakePinsUnique(std::vector<std::string>& pins) {
    static const char* kExtra[] = {"Extra", "Other", "More", "Again", "Last", "Also"};
    for (std::size_t i = 0; i < pins.size(); ++i) {
        if (pins[i].empty() || !LintVsPin(pins[i].c_str()).empty()) pins[i] = "Value";
        for (int n = 0; n < 6; ++n) {
            bool dup = false;
            for (std::size_t j = 0; j < i; ++j) if (pins[j] == pins[i]) dup = true;
            if (!dup && LintVsPin(pins[i].c_str()).empty()) break;
            const std::string base = pins[i];
            pins[i] = base + " " + kExtra[n];
        }
    }
}

inline void VsFillFriendlyPins(const okay::vs::VsApiEntry& e,
                               std::vector<const char*>& ins,
                               std::vector<const char*>& outs) {
    const bool color = VsApiPinsAreColor(e);
    std::vector<std::string> inNames, outNames;
    for (const okay::vs::VsApiPin& p : e.ins) inNames.push_back(VsFriendlyPin(p.name, color));
    for (const okay::vs::VsApiPin& p : e.outs) outNames.push_back(VsFriendlyPin(p.name, color));
    VsMakePinsUnique(inNames);
    VsMakePinsUnique(outNames);
    for (std::string& n : inNames) ins.push_back(VsIntern(std::move(n)));
    for (std::string& n : outNames) outs.push_back(VsIntern(std::move(n)));
}

// Fn.* rows the Add Node menu does not list because another row is the same function under another name (the VM
// registers one as an alias of the other). The menu shows one row per title; searching the hidden name finds the
// listed row, because VsNodeSearchText adds it there. Graphs that use the hidden type still load and run.
struct VsBuiltinAliasRow { const char* hidden; const char* listed; const char* reason; };
inline const VsBuiltinAliasRow kVsBuiltinAliasRows[] = {
    {"Fn.vec3", "Fn.Vector3", "the same function: alias(\"vec3\", \"Vector3\") in OkayScriptVM.cpp"},
    {"Fn.vec2", "Fn.Vector2", "the same function: alias(\"vec2\", \"Vector2\") in OkayScriptVM.cpp"},
};

// Old titles and raw identifiers. The palette searches this as well as the title.
inline std::string VsNodeSearchText(const char* type) {
    std::string s = VsSearchKeywords(type);
    for (const VsBuiltinAliasRow& a : kVsBuiltinAliasRows)
        if (type && std::strcmp(type, a.listed) == 0) {
            if (!s.empty()) s += ' ';
            s += a.hidden + 3;
            s += ' ';
            s += a.hidden;
        }
    if (type && std::strncmp(type, "Fn.", 3) == 0) {
        if (!s.empty()) s += ' ';
        s += type + 3;
        s += ' ';
        s += type;
    }
    if (const okay::vs::VsApiEntry* e = okay::vs::VsApiFind(type ? type : "")) {
        if (!s.empty()) s += ' ';
        s += e->title;
        s += ' ';
        s += e->code;
        s += ' ';
        s += e->member;
        s += ' ';
        s += e->type;
    }
    return s;
}

} // namespace okay::editor::vsed

// ---- Docs for the nodes that are not in the hand-written table ---------------
namespace okay::editor::vsed {

inline void VsFriendlyApiDocFixup(const okay::vs::VsApiEntry& e, VsNodeDoc& d) {
    std::vector<const char*> ins, outs;
    VsFillFriendlyPins(e, ins, outs);
    d.title = VsFriendlyApiTitleString(e);
    std::size_t i = 0, o = 0;
    for (VsPinDoc& p : d.pins) {
        if (p.exec) continue;
        if (p.input) { if (i < ins.size()) p.name = ins[i]; ++i; }
        else         { if (o < outs.size()) p.name = outs[o]; ++o; }
    }
}

// One node per text-script builtin (Fn.print ...): one optional input per argument (at least four), one result.
// When the docs name the builtin's arguments (okay::vs::BuiltinDocFor, generated by tools/gen_fn_pin_docs.py),
// the tooltip says what it does, what goes in each input and what comes out. Otherwise it keeps the
// general "Calls the text-script function ..." text.
inline std::string VsBuiltinCallText(const std::string& name, const okay::vs::BuiltinDoc* doc) {
    std::string call = name + "(";
    if (doc) {
        for (int i = 0; i < doc->argc; ++i) {
            const bool opt = (doc->optional >> i) & 1u;
            if (i) call += opt ? " [, " : ", ";
            else if (opt) call += "[";
            call += doc->args[i];
            if (opt) call += "]";
        }
    }
    return call + ")";
}

inline const VsNodeDoc* VsBuiltinDocFor(const std::string& type) {
    if (!okay::vs::IsBuiltinNode(type)) return nullptr;
    static std::unordered_map<std::string, VsNodeDoc> cache;
    auto it = cache.find(type);
    if (it != cache.end()) return &it->second;
    const std::string name = type.substr(3);
    const okay::vs::BuiltinDoc* doc = okay::vs::BuiltinDocFor(name);
    const int argPins = okay::vs::BuiltinPinCount(name);
    VsNodeDoc d;
    d.type = type;
    d.title = VsFriendlyBuiltinTitleString(name);
    const char* help = VsNodeHelp(type.c_str());
    const std::string fallback = "Calls the text-script function " + name + " with the values wired into " +
        okay::vs::BuiltinPinName(name, 0) + " to " + okay::vs::BuiltinPinName(name, argPins - 1) + ", and gives back its result.";
    // VsNodeHelp gives every Fn.* node the same general line ("Calls this text-script builtin...") unless it
    // has its own; the docs' summary is more useful than that general line.
    const char* general = VsNodeHelp("Fn.");
    const bool ownHelp = help && help[0] && !(general && std::strcmp(help, general) == 0);
    if (ownHelp || (help && help[0] && !doc)) d.description = help;
    else if (doc && doc->summary[0]) d.description = std::string(doc->summary) + " (text script: " + VsBuiltinCallText(name, doc) + ")";
    else if (doc) d.description = "Calls the text-script function " + VsBuiltinCallText(name, doc) +
                                  " with the values wired into its inputs, and gives back its result.";
    else d.description = fallback;
    if (doc && doc->returns[0]) {
        if (!d.description.empty() && d.description.back() != '.') d.description += '.';
        d.description += " Gives back: " + std::string(doc->returns) + ".";
    }
    d.requirements = "No component is required by the node itself. What " + name + " touches depends on the function. Runs in Play mode.";
    if (doc) {
        d.example = "In a text script this is " + VsBuiltinCallText(name, doc) + ".";
        if (doc->argc > 0)
            d.example += " Wire what you would pass as " + std::string(doc->args[0]) + " into " + okay::vs::BuiltinPinName(name, 0) +
                         (doc->argc > 1 ? ", " + std::string(doc->args[1]) + " into " + okay::vs::BuiltinPinName(name, 1) + ", and so on." : ".");
    } else {
        d.example = "In a text script this is " + name + "(First, Second). Wire the values you would pass into First and Second.";
    }
    d.related = "Run Text Snippet, Call Script";
    d.keywords = name + " " + d.title + " text script builtin";
    if (doc)
        for (int i = 0; i < doc->argc; ++i) d.keywords += std::string(" ") + doc->args[i];
    d.pins.push_back({true, true, "", "Flow", "The flow arrives here. The function runs when a wire reaches this pin.", ""});
    d.pins.push_back({false, true, "", "Flow", "After the function finishes, the flow continues from here.", ""});
    // One input per argument the builtin reads (at least four, so old graphs keep their pins).
    for (int i = 0; i < argPins; ++i) {
        std::string meaning;
        if (doc && i < doc->argc) {
            meaning = "Argument " + std::to_string(i + 1) + " of " + VsBuiltinCallText(name, doc) + ": " + doc->args[i] + ".";
            if ((doc->optional >> i) & 1u) meaning += " Optional: leave it empty to use the default.";
        } else if (doc) {
            meaning = "The docs list " + std::to_string(doc->argc) + " argument" + (doc->argc == 1 ? "" : "s") + " for " + name +
                      ". The function's code can read this input too; it is passed only when you wire or fill it.";
        } else {
            meaning = std::string("An argument for ") + name + ". Leave it empty when the function takes fewer.";
        }
        d.pins.push_back({true, false, okay::vs::BuiltinPinName(name, i), "Any", meaning, "nothing"});
    }
    d.pins.push_back({false, false, "Result", "Any",
                      doc && doc->returns[0] ? std::string("What ") + name + " gives back: " + doc->returns + "."
                                             : "What " + name + " gives back, or nothing when it returns no value.", ""});
    return &cache.emplace(type, std::move(d)).first->second;
}

inline const bool kVsDocHooks = [] {
    VsApiDocFixup = &VsFriendlyApiDocFixup;
    VsExtraDocFor = &VsBuiltinDocFor;
    return true;
}();

}  // namespace okay::editor::vsed
