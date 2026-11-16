#pragma once
// TEMPORARY: a straight copy of the script editor's current Find / Replace logic (editor/main.cpp), so the new test can
// show the bug before it is fixed. Replaced by the real matcher in the next step.
#include <cctype>
#include <cstddef>
#include <string>
#include <vector>

namespace okay::textfind {

inline bool MatchAt(const std::string& t, std::size_t i, const std::string& n, bool) {
    if (i + n.size() > t.size()) return false;
    for (std::size_t k = 0; k < n.size(); ++k) if (std::tolower((unsigned char)t[i + k]) != std::tolower((unsigned char)n[k])) return false;
    return true;
}

inline std::vector<std::size_t> FindAll(const std::string& text, const std::string& needle, bool matchCase = false) {
    (void)matchCase;
    std::vector<std::size_t> at;
    if (needle.empty()) return at;
    std::string hay = text, n = needle;
    for (auto& c : hay) c = (char)std::tolower((unsigned char)c);
    for (auto& c : n) c = (char)std::tolower((unsigned char)c);
    for (std::size_t p = hay.find(n); p != std::string::npos; p = hay.find(n, p + n.size())) at.push_back(p);
    return at;
}

inline int CurrentIndex(const std::vector<std::size_t>& at, std::size_t cursor) {
    if (at.empty()) return -1;
    int cur = 0;
    for (std::size_t p : at) if (p < cursor) ++cur;
    if (cur >= (int)at.size()) cur = (int)at.size() - 1;
    return cur;
}

inline long long ReplaceCurrent(std::string& text, std::size_t cursor, const std::string& from, const std::string& to,
                                bool matchCase = false) {
    (void)matchCase;
    if (from.empty()) return -1;
    std::size_t start = cursor > text.size() ? 0 : cursor;
    std::size_t p = text.find(from, start);
    if (p == std::string::npos) p = text.find(from);
    if (p == std::string::npos) return -1;
    text.replace(p, from.size(), to);
    return (long long)p;
}

inline std::size_t ReplaceAll(std::string& text, const std::string& from, const std::string& to, bool matchCase = false) {
    (void)matchCase;
    std::size_t n = 0;
    for (std::size_t p = text.find(from); p != std::string::npos && !from.empty(); p = text.find(from, p + to.size())) {
        text.replace(p, from.size(), to);
        ++n;
    }
    return n;
}

} // namespace okay::textfind
