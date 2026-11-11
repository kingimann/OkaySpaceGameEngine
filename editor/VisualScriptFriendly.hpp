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
inline void VsExpandToken(const std::string& low, std::vector<std::string>& out) {
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
    if (!repl && low.size() == 1) {
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

inline std::vector<std::string> VsWordsOf(const std::string& ident) {
    std::vector<std::string> words;
    std::string cur;
    auto flush = [&]() {
        if (cur.empty()) return;
        VsExpandToken(VsLowerCopy(cur), words);
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

inline std::string VsFriendlyBuiltinTitleString(const std::string& builtin) {
    // is_ / get_ / on_ stay questions, readings, and events. The node also has a
    // flow pin, and the title is still the kind of value it gives back.
    return VsJoinWords(VsWordsOf(builtin));
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

// Old titles and raw identifiers. The palette searches this as well as the title.
inline std::string VsNodeSearchText(const char* type) {
    std::string s = VsSearchKeywords(type);
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

// One node per text-script builtin (Fn.print ...): four optional arguments, one result.
inline const VsNodeDoc* VsBuiltinDocFor(const std::string& type) {
    if (!okay::vs::IsBuiltinNode(type)) return nullptr;
    static std::unordered_map<std::string, VsNodeDoc> cache;
    auto it = cache.find(type);
    if (it != cache.end()) return &it->second;
    const std::string name = type.substr(3);
    VsNodeDoc d;
    d.type = type;
    d.title = VsFriendlyBuiltinTitleString(name);
    const char* help = VsNodeHelp(type.c_str());
    d.description = help && help[0] ? std::string(help)
        : "Calls the text-script function " + name + " with the values wired into First to Fourth, and gives back its result.";
    d.requirements = "No component is required by the node itself. What " + name + " touches depends on the function. Runs in Play mode.";
    d.example = "In a text script this is " + name + "(First, Second). Wire the values you would pass into First and Second.";
    d.related = "Run Text Snippet, Call Script";
    d.keywords = name + " " + d.title + " text script builtin";
    d.pins.push_back({true, true, "", "Flow", "The flow arrives here. The function runs when a wire reaches this pin.", ""});
    d.pins.push_back({false, true, "", "Flow", "After the function finishes, the flow continues from here.", ""});
    static const char* const argNames[4] = {"First", "Second", "Third", "Fourth"};
    for (const char* a : argNames)
        d.pins.push_back({true, false, a, "Any", std::string("An argument for ") + name + ". Leave it empty when the function takes fewer.", "nothing"});
    d.pins.push_back({false, false, "Result", "Any", "What " + name + " gives back, or nothing when it returns no value.", ""});
    return &cache.emplace(type, std::move(d)).first->second;
}

inline const bool kVsDocHooks = [] {
    VsApiDocFixup = &VsFriendlyApiDocFixup;
    VsExtraDocFor = &VsBuiltinDocFor;
    return true;
}();

}  // namespace okay::editor::vsed
