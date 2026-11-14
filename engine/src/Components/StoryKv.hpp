#pragma once
// key=value text lines for the dialogue and quest files: order-free, quoted
// strings with \" \\ \n escapes, unknown keys skipped, so fields can be added
// without breaking files an older build wrote.
#include <cctype>
#include <cstdlib>
#include <sstream>
#include <string>
#include <unordered_map>

namespace okay::storykv {

inline std::string Quote(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '"') out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') continue;
        else out += c;
    }
    return out + "\"";
}

/// Read a quoted string at the stream position (the opening quote is next).
inline std::string ReadQuoted(std::istream& in) {
    std::string out;
    char c = 0;
    in >> std::ws;
    if (in.peek() != '"') { in >> out; return out; }
    in.get(c);
    while (in.get(c)) {
        if (c == '\\') {
            char e = 0;
            if (!in.get(e)) break;
            out += e == 'n' ? '\n' : e;
        } else if (c == '"') break;
        else out += c;
    }
    return out;
}

struct Out {
    std::ostringstream o;
    Out& str(const char* k, const std::string& v) { o << ' ' << k << '=' << Quote(v); return *this; }
    Out& num(const char* k, float v) { o << ' ' << k << '=' << v; return *this; }
    Out& num(const char* k, int v) { o << ' ' << k << '=' << v; return *this; }
    Out& b(const char* k, bool v) { o << ' ' << k << '=' << (v ? 1 : 0); return *this; }
    std::string text() const { return o.str(); }
};

struct In {
    std::unordered_map<std::string, std::string> m;
    explicit In(const std::string& line) {
        std::istringstream ls(line);
        while (true) {
            ls >> std::ws;
            if (!ls || ls.peek() == EOF) break;
            std::string k; char c = 0;
            while (ls.get(c) && c != '=' && !std::isspace((unsigned char)c)) k += c;
            if (c != '=') continue;
            if (ls.peek() == '"') m[k] = ReadQuoted(ls);
            else { std::string v; ls >> v; m[k] = v; }
        }
    }
    const std::string* find(const char* k) const { auto it = m.find(k); return it == m.end() ? nullptr : &it->second; }
    const std::string* find(const std::string& k) const { auto it = m.find(k); return it == m.end() ? nullptr : &it->second; }
    void f(const char* k, float& v) const { if (auto* s = find(k)) v = std::strtof(s->c_str(), nullptr); }
    void i(const char* k, int& v) const { if (auto* s = find(k)) v = std::atoi(s->c_str()); }
    void b(const char* k, bool& v) const { if (auto* s = find(k)) v = std::atoi(s->c_str()) != 0; }
    void str(const char* k, std::string& v) const { if (auto* s = find(k)) v = *s; }
    void i(const std::string& k, int& v) const { if (auto* s = find(k)) v = std::atoi(s->c_str()); }
    void b(const std::string& k, bool& v) const { if (auto* s = find(k)) v = std::atoi(s->c_str()) != 0; }
    void str(const std::string& k, std::string& v) const { if (auto* s = find(k)) v = *s; }
};

/// The first word of a line and the rest after it.
inline bool SplitHead(const std::string& line, std::string& head, std::string& rest) {
    std::size_t i = 0;
    while (i < line.size() && std::isspace((unsigned char)line[i])) ++i;
    std::size_t j = i;
    while (j < line.size() && !std::isspace((unsigned char)line[j])) ++j;
    head = line.substr(i, j - i);
    rest = j < line.size() ? line.substr(j) : std::string();
    return !head.empty();
}

} // namespace okay::storykv
