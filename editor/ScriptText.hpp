#pragma once
// ---------------------------------------------------------------------------
// ScriptText — the Script Editor's view of an OkayScript file as tokens, read
// the way the language reads it: `//`, `#` and `/* */` comments are skipped,
// strings ("…", $"…", 'c') are one token, identifiers keep their dots
// (transform.position). Pure text, no GUI, so --selftest covers it.
//
// Outline() finds what the VM treats as functions and types (it mirrors the
// parser's IsTypedFunctionAhead / IsBareFunctionAhead / IsClassAhead):
//   function name(...)          the `function` keyword
//   update(dt) { ... }          a bare name, its parameters, then a '{'
//   public void Fire() { ... }  modifiers + return type + name + '('
//   public class Gun { ... }    class / struct / interface / record / enum
// Only top-level and class-body declarations count; calls inside function
// bodies never do. Each entry is {line, "f  name" | "C  name" | "E  name"}.
// ---------------------------------------------------------------------------
#include <cctype>
#include <string>
#include <utility>
#include <vector>

namespace scripttext {

struct Tok {
    std::string text;
    int  line = 1;
    char kind = 'p';   ///< 'i' identifier, 'n' number, 's' string, 'p' punctuation
};

inline std::vector<Tok> Tokenize(const std::string& src) {
    std::vector<Tok> out;
    const std::size_t n = src.size();
    std::size_t i = 0;
    int line = 1;
    auto at = [&](std::size_t k) { return k < n ? src[k] : '\0'; };
    while (i < n) {
        const char c = src[i];
        if (c == '\n') { ++line; ++i; continue; }
        if (c == ' ' || c == '\t' || c == '\r') { ++i; continue; }
        if ((c == '/' && at(i + 1) == '/') || c == '#') {            // line comment
            while (i < n && src[i] != '\n') ++i;
            continue;
        }
        if (c == '/' && at(i + 1) == '*') {                           // block comment
            i += 2;
            while (i < n && !(src[i] == '*' && at(i + 1) == '/')) { if (src[i] == '\n') ++line; ++i; }
            i = i < n ? i + 2 : n;
            continue;
        }
        if (c == '"' || (c == '$' && at(i + 1) == '"')) {             // string / interpolated string
            const int startLine = line;
            i += c == '$' ? 2 : 1;
            while (i < n && src[i] != '"') {
                if (src[i] == '\\' && i + 1 < n) ++i;
                if (src[i] == '\n') ++line;
                ++i;
            }
            if (i < n) ++i;
            out.push_back({"\"", startLine, 's'});
            continue;
        }
        if (c == '\'') {                                              // char literal: one line only
            std::size_t j = i + 1;
            while (j < n && src[j] != '\'' && src[j] != '\n') { if (src[j] == '\\' && j + 1 < n) ++j; ++j; }
            if (j < n && src[j] == '\'') { out.push_back({"'", line, 's'}); i = j + 1; continue; }
            out.push_back({"'", line, 'p'}); ++i;                     // a stray quote isn't a string
            continue;
        }
        if (std::isdigit((unsigned char)c)) {
            std::size_t j = i;
            while (j < n && (std::isalnum((unsigned char)src[j]) || src[j] == '.')) ++j;
            out.push_back({src.substr(i, j - i), line, 'n'}); i = j;
            continue;
        }
        if (std::isalpha((unsigned char)c) || c == '_') {
            std::size_t j = i;
            while (j < n && (std::isalnum((unsigned char)src[j]) || src[j] == '_')) ++j;
            while (j + 1 < n && src[j] == '.' && (std::isalpha((unsigned char)src[j + 1]) || src[j + 1] == '_')) {
                ++j;
                while (j < n && (std::isalnum((unsigned char)src[j]) || src[j] == '_')) ++j;
            }
            out.push_back({src.substr(i, j - i), line, 'i'}); i = j;
            continue;
        }
        if (c == '=' && at(i + 1) == '>') { out.push_back({"=>", line, 'p'}); i += 2; continue; }
        out.push_back({std::string(1, c), line, 'p'}); ++i;
    }
    return out;
}

// For each character of `src`: 1 when it is code, 0 when it is part of a
// comment or a string literal. The `{...}` holes of an interpolated string
// $"HP {hp}" are code (the braces around them are not). A quote with no
// closing quote on its line isn't a char literal, so the apostrophe in
// `# don't` never swallows the code after it.
inline std::vector<char> CodeMask(const std::string& src) {
    const std::size_t n = src.size();
    std::vector<char> mask(n, 0);
    struct Frame { bool interp; int depth; };       // interp: inside $"…" text; else code (depth: braces in a hole)
    std::vector<Frame> st{{false, 0}};
    auto at = [&](std::size_t k) { return k < n ? src[k] : '\0'; };
    for (std::size_t i = 0; i < n;) {
        const char c = src[i];
        if (st.back().interp) {
            if (c == '\\') { i += 2; continue; }
            if (c == '"') { st.pop_back(); ++i; continue; }
            if (c == '{' && at(i + 1) == '{') { i += 2; continue; }
            if (c == '{') { st.push_back({false, 0}); ++i; continue; }
            ++i; continue;
        }
        if ((c == '/' && at(i + 1) == '/') || c == '#') { while (i < n && src[i] != '\n') ++i; continue; }
        if (c == '/' && at(i + 1) == '*') {
            i += 2;
            while (i < n && !(src[i] == '*' && at(i + 1) == '/')) ++i;
            i = i < n ? i + 2 : n;
            continue;
        }
        if (c == '$' && at(i + 1) == '"') { st.push_back({true, 0}); i += 2; continue; }
        if (c == '"') {
            ++i;
            while (i < n && src[i] != '"') { if (src[i] == '\\') ++i; ++i; }
            if (i < n) ++i;
            continue;
        }
        if (c == '\'') {
            std::size_t j = i + 1;
            while (j < n && src[j] != '\'' && src[j] != '\n') { if (src[j] == '\\' && j + 1 < n) ++j; ++j; }
            if (j < n && src[j] == '\'') { i = j + 1; continue; }
            mask[i++] = 1;
            continue;
        }
        if (st.size() > 1) {                                     // in an interpolation hole
            if (c == '{') ++st.back().depth;
            else if (c == '}') {
                if (st.back().depth == 0) { st.pop_back(); ++i; continue; }
                --st.back().depth;
            }
        }
        mask[i++] = 1;
    }
    return mask;
}

inline bool IsIdentChar(char c) { return std::isalnum((unsigned char)c) || c == '_'; }

// A name Rename may give a symbol: an identifier that isn't a keyword.
inline bool IsValidNewName(const std::string& s) {
    if (s.empty() || std::isdigit((unsigned char)s[0])) return false;
    for (char c : s) if (!IsIdentChar(c)) return false;
    static const char* kw[] = {"var", "if", "else", "while", "for", "foreach", "in", "function", "return", "true",
                               "false", "null", "break", "continue", "class", "struct", "enum", "new", "this",
                               "public", "private", "protected", "static", "void", "do", "switch", "case"};
    for (const char* k : kw) if (s == k) return false;
    return true;
}

// Whole-word uses of identifier `sym` in code (not in comments or strings,
// but inside $"…{sym}…" holes): {offset, 1-based line}.
inline std::vector<std::pair<std::size_t, int>> FindIdent(const std::string& src, const std::string& sym) {
    std::vector<std::pair<std::size_t, int>> out;
    if (sym.empty()) return out;
    const std::vector<char> code = CodeMask(src);
    int line = 1;
    for (std::size_t i = 0; i < src.size();) {
        if (src[i] == '\n') { ++line; ++i; continue; }
        if (code[i] && IsIdentChar(src[i]) && (i == 0 || !IsIdentChar(src[i - 1]))) {
            std::size_t j = i;
            while (j < src.size() && IsIdentChar(src[j])) ++j;
            if (j - i == sym.size() && src.compare(i, j - i, sym) == 0) out.push_back({i, line});
            i = j;
            continue;
        }
        ++i;
    }
    return out;
}

namespace detail {
inline bool Is(const std::vector<Tok>& t, std::size_t i, const char* s) { return i < t.size() && t[i].text == s; }

// Skip a type's generic arguments and array ranks just past its identifier:
// List<int>, Dictionary<string, int>, int[], float[,]. Returns `i` unchanged
// when what follows isn't type syntax (so `a < b` stays an expression).
inline std::size_t SkipTypeSuffix(const std::vector<Tok>& t, std::size_t i) {
    if (Is(t, i, "<")) {
        int depth = 0; std::size_t j = i;
        for (; j < t.size(); ++j) {
            const std::string& s = t[j].text;
            if (s == "<") ++depth;
            else if (s == ">") { if (--depth == 0) { ++j; break; } }
            else if (t[j].kind != 'i' && s != "," && s != "[" && s != "]" && s != "?") return i;
        }
        if (depth != 0) return i;
        i = j;
    }
    if (Is(t, i, "?")) ++i;                                   // int?
    while (Is(t, i, "[")) {
        std::size_t j = i + 1;
        while (Is(t, j, ",")) ++j;
        if (!Is(t, j, "]")) break;
        i = j + 1;
    }
    return i;
}

// Index just past the ')' matching the '(' at `i` (or t.size()).
inline std::size_t PastParens(const std::vector<Tok>& t, std::size_t i) {
    int depth = 0;
    for (; i < t.size(); ++i) {
        if (t[i].text == "(") ++depth;
        else if (t[i].text == ")" && --depth == 0) return i + 1;
    }
    return t.size();
}

inline bool IsStatementWord(const std::string& s) {
    static const char* w[] = {"if", "else", "for", "foreach", "while", "do", "switch", "case", "return", "new",
                              "var", "yield", "await", "throw", "using", "lock", "fixed", "catch", "try",
                              "in", "out", "ref", "typeof", "sizeof", "nameof", "function", "break", "continue"};
    for (const char* k : w) if (s == k) return true;
    return false;
}
// Tokens a statement can't end on: a new line after one of these continues it.
inline bool Continues(const std::string& s) {
    static const char* w[] = {"=", ",", "(", "[", "+", "-", "*", "/", "%", "&", "|", "!", "<", ">", "?", ":", ".", "=>"};
    for (const char* k : w) if (s == k) return true;
    return false;
}
} // namespace detail

inline std::vector<std::pair<int, std::string>> Outline(const std::string& src) {
    using namespace detail;
    const std::vector<Tok> t = Tokenize(src);
    std::vector<std::pair<int, std::string>> out;
    std::vector<char> ctx;        // 'T' type body, 'F' function body, 'B' any other block
    char pending = 0;             // what the next '{' opens
    for (std::size_t i = 0; i < t.size(); ++i) {
        const std::string& s = t[i].text;
        if (s == "{") { ctx.push_back(pending ? pending : 'B'); pending = 0; continue; }
        if (s == "}") { if (!ctx.empty()) ctx.pop_back(); pending = 0; continue; }
        if (s == ";") { pending = 0; continue; }
        if (!ctx.empty() && ctx.back() != 'T') continue;       // inside a function or a plain block
        const bool start = i == 0 || Is(t, i - 1, ";") || Is(t, i - 1, "{") || Is(t, i - 1, "}") ||
                           (t[i - 1].line < t[i].line && !Continues(t[i - 1].text)) || Is(t, i - 1, "]");
        if (!start) continue;
        std::size_t j = i;
        while (Is(t, j, "[")) {                                // [Header("Stats")] attributes
            int depth = 0;
            do { if (t[j].text == "[") ++depth; else if (t[j].text == "]") --depth; ++j; } while (depth > 0 && j < t.size());
        }
        if (j >= t.size() || t[j].kind != 'i') continue;

        if (t[j].text == "function" && j + 1 < t.size() && t[j + 1].kind == 'i') {
            out.push_back({t[j + 1].line, "f  " + t[j + 1].text});
            const std::size_t k = Is(t, j + 2, "(") ? PastParens(t, j + 2) : j + 2;
            pending = Is(t, k, "{") ? 'F' : 0;
            i = k - 1;
            continue;
        }
        // A run of identifiers: modifiers, a (generic / array) type, a name.
        std::vector<std::size_t> run;
        std::size_t k = j;
        while (k < t.size() && t[k].kind == 'i') { run.push_back(k); k = SkipTypeSuffix(t, k + 1); }

        bool typeDecl = false;
        for (std::size_t r = 0; r + 1 < run.size(); ++r) {
            const std::string& w = t[run[r]].text;
            if (w == "class" || w == "struct" || w == "interface" || w == "record" || w == "enum") {
                const Tok& nm = t[run[r + 1]];
                out.push_back({nm.line, (w == "enum" ? "E  " : "C  ") + nm.text});
                std::size_t b = k;                             // past `: Base, IFace`
                while (b < t.size() && !Is(t, b, "{") && !Is(t, b, ";")) ++b;
                pending = Is(t, b, "{") ? (w == "enum" ? 'B' : 'T') : 0;
                i = b - 1;
                typeDecl = true;
                break;
            }
        }
        if (typeDecl || !Is(t, k, "(") || IsStatementWord(t[run[0]].text)) continue;
        const Tok& name = t[run.back()];
        if (name.text.find('.') != std::string::npos || IsStatementWord(name.text)) continue;
        const std::size_t after = PastParens(t, k);
        if (run.size() >= 2) {                                 // public void Fire() / int F() => 1;
            out.push_back({name.line, "f  " + name.text});
            pending = Is(t, after, "{") ? 'F' : 0;
            i = after - 1;
        } else if (Is(t, after, "{")) {                        // update(dt) { ... }
            out.push_back({name.line, "f  " + name.text});
            pending = 'F';
            i = after - 1;
        }
    }
    return out;
}

} // namespace scripttext
