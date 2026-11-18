#pragma once
// A tiny reader for the UI library's data files (assets/ui/*.okaydata), written for this engine: plain text, no outside parser.
//
//   # a comment (a line starting with #)
//   [kind name with spaces]          starts a section
//   key = value                      a setting of the section (the key is one word; the value is the rest of the line)
//   anything else                    a body line, kept in order (screens and icons put their drawing lines here)
//
// The files are embedded into the engine at build time (engine/CMakeLists.txt reads assets/ui/*.okaydata), so every program has
// them without shipping the files. EmbeddedData() returns the text of one of them by name ("ui_screens", "ui_themes", ...).
#include <cctype>
#include <cstddef>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace okay { namespace uilib {

const char* EmbeddedData(const char* name);   // generated into the build tree; "" for an unknown name

struct DataSection {
    std::string kind, name;
    std::vector<std::pair<std::string, std::string>> kv;   // in file order
    std::vector<std::string> body;
    const std::string* Get(const std::string& key) const {
        for (const auto& p : kv) if (p.first == key) return &p.second;
        return nullptr;
    }
    std::string Str(const std::string& key, const std::string& def = std::string()) const {
        const std::string* v = Get(key); return v ? *v : def;
    }
    float Num(const std::string& key, float def = 0.0f) const {
        const std::string* v = Get(key);
        if (!v || v->empty()) return def;
        char* end = nullptr; const float f = std::strtof(v->c_str(), &end);
        return end == v->c_str() ? def : f;
    }
};

inline std::string Trim(const std::string& s) {
    std::size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) --b;
    return s.substr(a, b - a);
}

inline std::vector<DataSection> ParseData(const std::string& text) {
    std::vector<DataSection> out;
    std::size_t pos = 0;
    while (pos <= text.size()) {
        std::size_t nl = text.find('\n', pos);
        if (nl == std::string::npos) nl = text.size();
        std::string raw = text.substr(pos, nl - pos);
        pos = nl + 1;
        // keep leading spaces of body lines (screens use the indent), drop the trailing ones
        while (!raw.empty() && (raw.back() == '\r' || raw.back() == ' ' || raw.back() == '\t')) raw.pop_back();
        const std::string t = Trim(raw);
        if (t.empty() || t[0] == '#') continue;
        if (t[0] == '[' && t.back() == ']') {
            DataSection s;
            const std::string inner = t.substr(1, t.size() - 2);
            const std::size_t sp = inner.find(' ');
            s.kind = sp == std::string::npos ? inner : inner.substr(0, sp);
            s.name = sp == std::string::npos ? std::string() : Trim(inner.substr(sp + 1));
            out.push_back(std::move(s));
            continue;
        }
        if (out.empty()) continue;
        // "key = value": a word, then '='
        std::size_t i = 0;
        while (i < t.size() && (std::isalnum((unsigned char)t[i]) || t[i] == '_' || t[i] == '.')) ++i;
        std::size_t j = i;
        while (j < t.size() && t[j] == ' ') ++j;
        if (i > 0 && j < t.size() && t[j] == '=') {
            out.back().kv.emplace_back(t.substr(0, i), Trim(t.substr(j + 1)));
        } else {
            out.back().body.push_back(raw);
        }
    }
    return out;
}

/// Split "a|b|c" (or any separator) into pieces; an empty string gives no pieces.
inline std::vector<std::string> Split(const std::string& s, char sep) {
    std::vector<std::string> v;
    if (s.empty()) return v;
    std::size_t a = 0;
    for (;;) {
        const std::size_t b = s.find(sep, a);
        v.push_back(Trim(s.substr(a, b == std::string::npos ? std::string::npos : b - a)));
        if (b == std::string::npos) break;
        a = b + 1;
    }
    return v;
}

}} // namespace okay::uilib
