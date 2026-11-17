#pragma once
// The text helpers every scene record uses: Quote / ReadQuoted for strings, KvOut / KvIn for `key=value` records
// (order-free; keys a build does not know are skipped). Shared by SceneSerializer.cpp and by the modules that write and
// read their own records from register.cpp. Moved here unchanged from SceneSerializer.cpp.
#include "okay/Render/Color.hpp"
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <istream>
#include <ostream>
#include <sstream>
#include <string>
#include <unordered_map>

namespace okay {
namespace rec {

/// The folder of the scene being loaded (a baked file is looked for next to it too). Empty outside a load.
const std::string& LoadingSceneDir();

// Quote a string for the line-based format (escapes quotes/backslashes).
inline std::string Quote(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    out += '"';
    return out;
}

// Read a quoted token from a stream (assumes leading quote already trimmed by >>).
inline std::string ReadQuoted(std::istream& in) {
    std::string tok;
    in >> std::ws;
    if (in.peek() != '"') { in >> tok; return tok; }
    in.get(); // opening quote
    std::string out;
    char c;
    while (in.get(c)) {
        if (c == '\\') { if (in.get(c)) out += c; }
        else if (c == '"') break;
        else out += c;
    }
    return out;
}

// key=value component lines (used by newer components): order-free, and keys a
// build doesn't know are skipped, so fields can be added without breaking saves.
struct KvOut {
    std::ostream& o;
    template <class T> KvOut& operator()(const char* k, const T& v) { o << ' ' << k << '=' << v; return *this; }
    KvOut& b(const char* k, bool v) { o << ' ' << k << '=' << (v ? 1 : 0); return *this; }
    KvOut& key(const char* k, char v) { o << ' ' << k << '=' << (int)(unsigned char)v; return *this; }
    KvOut& str(const char* k, const std::string& v) { o << ' ' << k << '=' << Quote(v); return *this; }
    KvOut& col(const char* k, const Color& c) {
        o << ' ' << k << "R=" << c.r << ' ' << k << "G=" << c.g << ' ' << k << "B=" << c.b << ' ' << k << "A=" << c.a;
        return *this;
    }
};
// Read the rest of a record's line. A quoted value may itself contain line breaks (a quest description typed
// into a multi-line box is written as it is, Quote escapes only the quote and the backslash), so a line that ends
// inside a quote goes on to the next one until the quote closes. When it never closes (a hand-edited or damaged
// file) the stream is put back and only the first line is taken, as before.
inline std::string ReadRecordLine(std::istream& in) {
    std::string line;
    // Ask the buffer, not tellg(): tellg() fails once the stream has hit the end.
    const std::streamoff start = in.rdbuf()->pubseekoff(0, std::ios::cur, std::ios::in);
    std::getline(in, line);
    auto endsInQuote = [](const std::string& l) {
        bool open = false;
        for (std::size_t i = 0; i < l.size(); ++i) {
            if (open && l[i] == '\\') ++i;   // an escaped character
            else if (l[i] == '"') open = !open;
        }
        return open;
    };
    if (!endsInQuote(line) || start < 0) return line;
    std::string joined = line;
    std::string next;
    while (std::getline(in, next)) {
        joined += '\n';
        joined += next;
        if (!endsInQuote(joined)) return joined;
    }
    in.clear();
    in.rdbuf()->pubseekpos(start, std::ios::in);   // the quote never closed: only this line belongs to the record
    std::getline(in, line);
    return line;
}
struct KvIn {
    std::unordered_map<std::string, std::string> m;
    explicit KvIn(std::istream& in) {
        const std::string line = ReadRecordLine(in);
        std::istringstream ls(line);
        while (true) {
            ls >> std::ws;
            if (!ls || ls.peek() == EOF) break;
            std::string k; char c;
            while (ls.get(c) && c != '=' && !std::isspace((unsigned char)c)) k += c;
            if (c != '=') continue;
            if (ls.peek() == '"') m[k] = ReadQuoted(ls);
            else { std::string v; ls >> v; m[k] = v; }
        }
    }
    const std::string* find(const char* k) const { auto it = m.find(k); return it == m.end() ? nullptr : &it->second; }
    void f(const char* k, float& v) const { if (auto* s = find(k)) v = std::strtof(s->c_str(), nullptr); }
    void i(const char* k, int& v) const { if (auto* s = find(k)) v = std::atoi(s->c_str()); }
    void u(const char* k, std::uint32_t& v) const { if (auto* s = find(k)) v = (std::uint32_t)std::strtoul(s->c_str(), nullptr, 10); }
    void b(const char* k, bool& v) const { if (auto* s = find(k)) v = std::atoi(s->c_str()) != 0; }
    void key(const char* k, char& v) const { if (auto* s = find(k)) v = (char)std::atoi(s->c_str()); }
    void str(const char* k, std::string& v) const { if (auto* s = find(k)) v = *s; }
    void col(const char* k, Color& c) const {
        std::string b = k;
        f((b + "R").c_str(), c.r); f((b + "G").c_str(), c.g); f((b + "B").c_str(), c.b); f((b + "A").c_str(), c.a);
    }
};

} // namespace rec
} // namespace okay
