#pragma once
// okay::textfind: the one text matcher behind a code editor's Find bar. Find, the "k of M" counter, the match
// highlight, Next / Previous, Replace and Replace All all ask this, so they always agree on what a match is
// (issue #888 finding 70: Find used to ignore case while Replace matched case exactly).
//
//   - Match case off (the default): ASCII letters A-Z and a-z compare equal; every other byte compares exactly.
//     UTF-8 text is safe: multi-byte characters only ever match themselves.
//   - A match never starts on a UTF-8 continuation byte (the middle of a character).
//   - Matches never overlap and are found left to right ("aaaa" holds two "aa").
//   - An empty needle matches nothing.
//
// Pure functions over std::string, no UI. Written for this engine from the plain description above.
#include <cstddef>
#include <string>
#include <vector>

namespace okay::textfind {

inline char FoldAscii(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c; }
inline bool IsUtf8Continuation(char c) { return ((unsigned char)c & 0xC0u) == 0x80u; }

/// True when `needle` matches `text` starting at byte `pos`.
inline bool MatchAt(const std::string& text, std::size_t pos, const std::string& needle, bool matchCase) {
    const std::size_t n = needle.size();
    if (n == 0 || pos > text.size() || text.size() - pos < n) return false;
    if (IsUtf8Continuation(text[pos])) return false;
    if (matchCase) return text.compare(pos, n, needle) == 0;
    for (std::size_t k = 0; k < n; ++k)
        if (FoldAscii(text[pos + k]) != FoldAscii(needle[k])) return false;
    return true;
}

/// The first match at or after byte `from`; std::string::npos when there is none.
inline std::size_t FindFrom(const std::string& text, const std::string& needle, std::size_t from, bool matchCase) {
    if (needle.empty() || needle.size() > text.size()) return std::string::npos;
    for (std::size_t i = from; i + needle.size() <= text.size(); ++i)
        if (MatchAt(text, i, needle, matchCase)) return i;
    return std::string::npos;
}

/// Every match, left to right, not overlapping.
inline std::vector<std::size_t> FindAll(const std::string& text, const std::string& needle, bool matchCase = false) {
    std::vector<std::size_t> at;
    for (std::size_t p = FindFrom(text, needle, 0, matchCase); p != std::string::npos;
         p = FindFrom(text, needle, p + needle.size(), matchCase))
        at.push_back(p);
    return at;
}

/// The index of the "current" match for a cursor: the first match that starts at or after `cursor`, or the last match
/// when the cursor is past them all; -1 when there are none. The "k of M" counter shows this index + 1, and
/// ReplaceCurrent changes exactly this match.
inline int CurrentIndex(const std::vector<std::size_t>& matches, std::size_t cursor) {
    if (matches.empty()) return -1;
    for (std::size_t i = 0; i < matches.size(); ++i)
        if (matches[i] >= cursor) return (int)i;
    return (int)matches.size() - 1;
}

/// Replace the current match (see CurrentIndex) with `with`. Returns where it was, or -1 when nothing matched.
inline long long ReplaceCurrent(std::string& text, std::size_t cursor, const std::string& needle, const std::string& with,
                                bool matchCase = false) {
    const std::vector<std::size_t> at = FindAll(text, needle, matchCase);
    const int i = CurrentIndex(at, cursor);
    if (i < 0) return -1;
    const std::size_t p = at[(std::size_t)i];
    text.replace(p, needle.size(), with);
    return (long long)p;
}

/// Replace every match (the same ones FindAll returns) with `with`. Text that `with` brings in is never matched again.
/// Returns how many were replaced.
inline std::size_t ReplaceAll(std::string& text, const std::string& needle, const std::string& with, bool matchCase = false) {
    const std::vector<std::size_t> at = FindAll(text, needle, matchCase);
    if (at.empty()) return 0;
    std::string out;
    out.reserve(text.size() + at.size() * with.size());
    std::size_t last = 0;
    for (std::size_t p : at) {
        out.append(text, last, p - last);
        out += with;
        last = p + needle.size();
    }
    out.append(text, last, std::string::npos);
    text.swap(out);
    return at.size();
}

} // namespace okay::textfind
