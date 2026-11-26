#pragma once
// The engine's one place for small text helpers (CLAUDE.md rule 19c, docs/engine/shared_code.md "string helpers").
// Add to this file instead of writing another Trim/Lower/Split next to your code. Header-only, std only.
// Written from scratch for this engine.
#include <string>

namespace okay::str {

// ASCII-only lowercase: bytes 'A'..'Z' become 'a'..'z', every other byte (UTF-8 included) is left as it is.
// Locale-independent, unlike std::tolower with a non-"C" locale.
inline std::string LowerAscii(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

// ASCII-only uppercase: bytes 'a'..'z' become 'A'..'Z', every other byte is left as it is (the DOM's tagName).
inline std::string UpperAscii(std::string s) {
    for (char& c : s)
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    return s;
}

// One byte of LowerAscii (for readers that fold as they scan).
inline char LowerAsciiChar(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

// ASCII-only case-blind compare: "Jump" equals "jUMP" (no copy, no allocation).
inline bool EqualsNoCaseAscii(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        char x = a[i], y = b[i];
        if (x >= 'A' && x <= 'Z') x = static_cast<char>(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z') y = static_cast<char>(y - 'A' + 'a');
        if (x != y) return false;
    }
    return true;
}

// True when `s` ends with `tail`, ignoring ASCII letter case ("Game.OKAYPKG" ends with ".okaypkg").
inline bool EndsWithNoCaseAscii(const std::string& s, const std::string& tail) {
    return s.size() >= tail.size() && EqualsNoCaseAscii(s.substr(s.size() - tail.size()), tail);
}

// `s` without the spaces, tabs, carriage returns and newlines at its two ends (ASCII whitespace only).
inline std::string Trim(const std::string& s) {
    std::size_t b = 0, e = s.size();
    auto space = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f'; };
    while (b < e && space(s[b])) ++b;
    while (e > b && space(s[e - 1])) --e;
    return s.substr(b, e - b);
}

// The value of one hexadecimal digit (0-9, a-f, A-F), or -1 for any other character.
inline int HexDigitValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

}  // namespace okay::str
