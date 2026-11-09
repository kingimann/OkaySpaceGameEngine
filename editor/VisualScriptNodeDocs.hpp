#pragma once
// One place for what every visual-script node does: a plain description, every
// pin (type, meaning, the value used when nothing is wired), requirements, a
// short example and the words search should find. Hand-written nodes live in
// VisualScriptNodeDocs.inc (scripts/gen_vs_node_docs.py). Engine-API nodes are
// filled from the binding table the first time they are asked for, so a new
// API entry is documented without a second copy of its text.
#include "okay/VisualScript/VsApi.hpp"
#include "okay/VisualScript/VsTypes.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace okay::editor::vsed {

struct VsPinDoc {
    bool input = true;
    bool exec = false;
    std::string name;
    std::string type;
    std::string meaning;
    std::string fallback;   ///< empty when the pin has no value (a flow pin)
};

struct VsNodeDoc {
    std::string type;
    std::string title;
    std::string description;
    std::string requirements;     ///< never empty: "No component..." is still info
    std::string component;        ///< ids the editor can add, separated by '|'; empty if none
    std::string example;
    std::string related;
    std::string keywords;
    std::string setting;          ///< the node's text field; empty when it has none
    std::string settingDefault;
    std::vector<VsPinDoc> pins;
};

#include "VisualScriptNodeDocs.inc"

inline std::unordered_map<std::string, VsNodeDoc>& VsHandDocs() {
    static std::unordered_map<std::string, VsNodeDoc> m = [] {
        std::unordered_map<std::string, VsNodeDoc> out;
        VsAddHandDocs(out);
        return out;
    }();
    return m;
}

inline std::string VsPinTypeLabel(okay::vs::PinType t) {
    using okay::vs::PinType;
    switch (t) {
        case PinType::Number: return "Number";
        case PinType::Bool:   return "Bool";
        case PinType::Text:   return "Text";
        case PinType::Vec3:   return "Vector3";
        case PinType::Object: return "Object";
        case PinType::List:   return "List";
        default:              return "Any";
    }
}

inline std::string VsDefaultFor(okay::vs::PinType t) {
    using okay::vs::PinType;
    switch (t) {
        case PinType::Number: return "0";
        case PinType::Bool:   return "false";
        case PinType::Text:   return "empty text";
        case PinType::Vec3:   return "(0, 0, 0)";
        case PinType::Object: return "no object";
        case PinType::List:   return "an empty list";
        default:              return "nothing";
    }
}

inline const VsNodeDoc* VsApiDoc(const okay::vs::VsApiEntry& e) {
    static std::unordered_map<std::string, VsNodeDoc> cache;
    auto it = cache.find(e.type);
    if (it != cache.end()) return &it->second;
    VsNodeDoc d;
    d.type = e.type;
    d.title = e.title;
    d.description = e.doc.empty()
        ? ("Does the same thing as " + e.code + " in a script.")
        : e.doc;
    if (d.description.size() < 24)
        d.description = e.title + " — " + d.description + (d.description.empty() || d.description.back() == '.' ? "" : ".");
    const bool plain = e.apiType == "Transform" || e.apiType == "GameObject" || e.apiType == "Vector3" ||
                       e.apiType == "Input" || e.apiType == "Time" || e.apiType == "Mathf" || e.apiType == "Debug" ||
                       e.apiType == "Random" || e.apiType == "Color" || e.apiType == "Quaternion";
    if (e.hasTarget && !plain)
        d.requirements = "Needs a " + e.apiType + " on the target object (this object when Target is empty). Runs in Play mode.";
    else if (e.hasTarget)
        d.requirements = "Uses this object's " + e.apiType + ". Leave Target empty to use this object. Runs in Play mode.";
    else
        d.requirements = "No component on this object is required. " + e.apiType + " is used directly. Runs in Play mode.";
    if (e.hasTarget && !plain) d.component = e.apiType == "Rigidbody" ? "Rigidbody3D|Rigidbody2D" : e.apiType;
    d.example = "In a script this is " + e.code + ".";
    d.keywords = e.apiType + " " + e.member + " " + e.title;
    d.related = "Other " + e.apiType + " nodes";
    if (e.exec) {
        d.pins.push_back({true, true, "", "Flow", "The flow arrives here. The call runs when a wire reaches this pin.", ""});
        d.pins.push_back({false, true, "", "Flow", "After the call finishes, the flow continues from here.", ""});
    }
    for (const auto& p : e.ins) {
        const std::string typ = VsPinTypeLabel(p.type);
        std::string meaning;
        std::string fb;
        if (p.name == "Target") {
            meaning = "The object whose " + e.apiType + " this call uses. Leave it empty to use this object. It is an Object.";
            fb = "this object";
        } else {
            meaning = p.name + " (" + typ + "), passed into " + e.code + ".";
            fb = VsDefaultFor(p.type);
            if (!fb.empty()) meaning += " Unwired, this is " + fb + ".";
        }
        d.pins.push_back({true, false, p.name, typ, meaning, fb});
    }
    for (const auto& p : e.outs) {
        const std::string typ = VsPinTypeLabel(p.type);
        d.pins.push_back({false, false, p.name, typ,
                          p.name + " (" + typ + "), the value " + e.code + " gives back.", ""});
    }
    auto placed = cache.emplace(e.type, std::move(d));
    return &placed.first->second;
}

inline const VsNodeDoc* VsNodeDocFor(const std::string& type) {
    auto it = VsHandDocs().find(type);
    if (it != VsHandDocs().end()) return &it->second;
    if (const okay::vs::VsApiEntry* e = okay::vs::VsApiFind(type)) return VsApiDoc(*e);
    return nullptr;
}

inline const VsPinDoc* VsPinDocFor(const VsNodeDoc& doc, bool input, bool exec, const std::string& name) {
    for (const VsPinDoc& p : doc.pins)
        if (p.input == input && p.exec == exec && p.name == name) return &p;
    return nullptr;
}

// ---- Fuzzy search ----------------------------------------------------------
// Score a needle against a title (hits mark title letters) and a bag of extra
// words (category, type, keywords, description). Typos of one or two edits,
// including a swapped pair of letters, still match.
inline int VsEditDistance(const std::string& a, const std::string& b) {
    const int n = (int)a.size(), m = (int)b.size();
    if (n > 48 || m > 48) return 99;
    if (std::abs(n - m) > 3) return 99;
    // pp is the row before the previous one, so a swapped pair of letters is one edit.
    std::vector<int> pp(m + 1), p(m + 1), c(m + 1);
    for (int j = 0; j <= m; ++j) p[j] = j;
    for (int i = 1; i <= n; ++i) {
        c[0] = i;
        for (int j = 1; j <= m; ++j) {
            const int cost = a[i - 1] == b[j - 1] ? 0 : 1;
            c[j] = std::min(std::min(p[j] + 1, c[j - 1] + 1), p[j - 1] + cost);
            if (i > 1 && j > 1 && a[i - 1] == b[j - 2] && a[i - 2] == b[j - 1])
                c[j] = std::min(c[j], pp[j - 2] + 1);
        }
        pp.swap(p);
        p.swap(c);
    }
    return p[m];
}

inline std::string VsLower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

/// `hits` receives the title character indexes that should be drawn highlighted.
/// 0 means no match.
inline int VsFuzzyScore(const std::string& needleRaw, const std::string& titleRaw, const std::string& extraRaw,
                        std::vector<int>& hits) {
    hits.clear();
    const std::string needle = VsLower(needleRaw);
    if (needle.empty()) return 1;
    const std::string title = VsLower(titleRaw);
    if (title == needle) {
        for (int i = 0; i < (int)title.size(); ++i) hits.push_back(i);
        return 1000;
    }
    auto mark = [&](std::size_t at, std::size_t n) {
        for (std::size_t i = 0; i < n && at + i < title.size(); ++i) hits.push_back((int)(at + i));
    };
    std::size_t p = title.find(needle);
    if (p != std::string::npos) {
        mark(p, needle.size());
        if (p == 0) return 800;
        if (title[p - 1] == ' ' || title[p - 1] == '(') return 650;
        return 500 - (int)std::min<std::size_t>(p, 200);
    }
    int run = 0, sc = 0;
    std::size_t j = 0;
    for (std::size_t i = 0; i < title.size() && j < needle.size(); ++i) {
        if (title[i] == needle[j]) {
            hits.push_back((int)i); ++j; ++run;
            sc += 3 + run * 2 + (i == 0 || title[i - 1] == ' ' ? 6 : 0);
        } else run = 0;
    }
    if (j == needle.size()) return 100 + sc;
    hits.clear();
    const int limit = needle.size() <= 4 ? 1 : 2;
    if ((int)needle.size() >= 3 && VsEditDistance(needle, title) <= limit) {
        for (int i = 0; i < (int)title.size(); ++i) hits.push_back(i);
        return 240;
    }
    std::size_t start = 0;
    while (start < title.size()) {
        std::size_t sp = title.find(' ', start);
        if (sp == std::string::npos) sp = title.size();
        const std::string word = title.substr(start, sp - start);
        if (word.size() >= 2 && (int)needle.size() >= 3 && VsEditDistance(needle, word) <= 1) {
            for (std::size_t i = start; i < sp; ++i) hits.push_back((int)i);
            return 180;
        }
        start = sp + 1;
    }
    const std::string extra = VsLower(extraRaw);
    if (extra.find(needle) != std::string::npos) return 40;
    start = 0;
    while (start < extra.size()) {
        std::size_t sp = extra.find_first_of(" \t,.;:", start);
        if (sp == std::string::npos) sp = extra.size();
        const std::string word = extra.substr(start, sp - start);
        if (word.size() >= 3 && (int)needle.size() >= 4 && VsEditDistance(needle, word) <= 1) return 30;
        if (sp >= extra.size()) break;
        start = sp + 1;
    }
    return 0;
}

inline std::string VsSearchBlob(const VsNodeDoc& d) {
    std::string s = d.type + " " + d.keywords + " " + d.description + " " + d.requirements;
    for (const VsPinDoc& p : d.pins) s += " " + p.name + " " + p.type + " " + p.meaning;
    return s;
}

// ---- The reference page ----------------------------------------------------
inline void VsAppendDocMarkdown(std::ostringstream& o, const VsNodeDoc& d) {
    o << "### " << (d.title.empty() ? d.type : d.title) << "\n\n";
    o << "`" << d.type << "`\n\n";
    o << d.description << "\n\n";
    o << "**Requires:** " << d.requirements << "\n\n";
    if (!d.setting.empty()) {
        o << "**Setting:** " << d.setting;
        if (!d.settingDefault.empty()) o << " Default: " << d.settingDefault << ".";
        o << "\n\n";
    }
    if (!d.pins.empty()) {
        o << "| Pin | Direction | Type | Meaning | Default |\n";
        o << "| --- | --- | --- | --- | --- |\n";
        for (const VsPinDoc& p : d.pins) {
            const std::string name = p.name.empty() ? (p.exec ? "flow" : "value") : p.name;
            o << "| " << name << " | " << (p.input ? "in" : "out") << " | " << p.type
              << " | " << p.meaning << " | " << (p.fallback.empty() ? "—" : p.fallback) << " |\n";
        }
        o << "\n";
    }
    if (!d.example.empty()) o << "**Example:** " << d.example << "\n\n";
    if (!d.related.empty()) o << "**Related:** " << d.related << "\n\n";
}

/// `orderedTypes` is every registered node, in palette order. API nodes that
/// are not in the hand table are filled from the binding.
inline std::string VsNodeDocsMarkdown(const std::vector<std::string>& orderedTypes) {
    std::ostringstream o;
    o << "# Visual scripting nodes\n\n";
    o << "What every node in the Visual Script palette does, which pins it has, and what it needs.\n";
    o << "Generated from the same table the editor shows in the node search and on node tooltips.\n";
    o << "Hand-written nodes: `editor/VisualScriptNodeDocs.inc`. Engine API nodes: the binding table in `VisualScript/VsApi`.\n\n";
    std::string lastGroup;
    for (const std::string& type : orderedTypes) {
        const VsNodeDoc* d = VsNodeDocFor(type);
        if (!d) continue;
        std::string group = "Nodes";
        const auto dot = type.find('.');
        if (dot != std::string::npos) group = "Engine API / " + type.substr(0, dot);
        if (group != lastGroup) {
            o << "## " << group << "\n\n";
            lastGroup = group;
        }
        VsAppendDocMarkdown(o, *d);
    }
    return o.str();
}

}  // namespace okay::editor::vsed
