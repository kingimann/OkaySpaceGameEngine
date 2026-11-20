#pragma once
// Build-time script "compaction" for Build Game's "Protect script source" toggle (off by
// default; see docs/script_protection.md, Stage 2 and 3). This is NOT encryption or
// obfuscation of the running logic: it is a lossless strip of comments and extra
// whitespace from .okay / .lua text, so a shipped game's script files hold only what the
// interpreter needs to run. The toggle is read by the editor's Build Game step; nothing
// here depends on the editor, SDL or any scene type, only on text.
//
// SECURITY NOTE (see docs/script_protection.md): this defeats a casual read of the script
// in a text editor. It does not and cannot stop a determined person from running the
// (still fully functional) stripped script through the game's own interpreter to see what
// it does, because the shipped game has to be able to run the content it ships.
//
// GAP, stated honestly: identifier renaming (shortening local/private names) is NOT done
// here. OkayScript calls many of its top-level functions by name at runtime (event hooks
// such as on_menu/on_gui/on_update/on_collision, and engine API lookups by name in
// EngineApi.cpp); renaming safely needs full name-resolution analysis matching the
// interpreter's own rules, and getting it wrong silently breaks a shipped game. See
// docs/script_protection.md, "Not done", for the reasoning.
#include <cstddef>
#include <string>

namespace okay::scriptprotect {

/// Build Settings: one new toggle, default OFF. Kept as a plain struct so BuildPipeline and
/// the editor can carry it alongside the other Build Settings without any new dependency.
struct Options {
    bool protectScriptSource = false;   ///< "Protect script source" checkbox, default OFF
};

/// Strip `//` line comments and `/* ... */` block comments from OkayScript/Lua-style source
/// text, without touching anything inside a string literal — plain "..." or interpolated
/// $"..." (which can itself nest more string literals inside its { } holes, same nesting
/// rule as the real Lexer). A comment opener found inside a string is just text and is left
/// alone. An unterminated block comment strips to the end of the file (never crashes; the
/// Checker/Lexer still runs on the result and will report anything genuinely broken).
///
/// This is a textual pass, not a reparse: it does not understand the language beyond
/// "where do strings start and end" — which is exactly where comments can never appear.
/// Keeping it textual means it can never change what the script does, only how much of
/// its original text remains.
std::string StripComments(const std::string& src);

/// Collapse runs of whitespace (including blank lines) to a single space / single newline,
/// and trim trailing whitespace from each line. Never removes a newline that is needed to
/// separate two tokens that only a newline keeps apart in this language... in practice
/// OkayScript statements are separator-free at the brace level, so this is a conservative
/// pass: it keeps one newline per original line that had non-whitespace content, and drops
/// lines that were comment-only or blank after StripComments. It never merges two lines.
std::string CollapseWhitespace(const std::string& src);

/// The full Stage 2 pass used by Build Game when `protectScriptSource` is on:
/// StripComments then CollapseWhitespace. Identity (returns `src` unchanged) when
/// `opts.protectScriptSource` is false, so a build with the toggle off is byte-for-byte
/// unaffected — callers do not need to branch on the toggle themselves.
std::string ProtectSource(const std::string& src, const Options& opts);

// ---- implementation --------------------------------------------------------------------

namespace detail {
// Index just past the string literal starting at src[i] (src[i] is '"' or the '$' of $"),
// treating a $"..."{ expr }"..." hole as more code to scan (its own strings skipped the
// same way, recursively, to the same nesting depth the real Lexer allows) so that a '"'
// inside an expression hole doesn't end the outer string early. Returns src.size() if the
// string runs off the end of the text (never infinite-loops; Checker reports the error).
inline std::size_t SkipStringLiteral(const std::string& src, std::size_t i, int depth = 0) {
    constexpr int kMaxNesting = 16;
    const bool interp = src[i] == '$';
    std::size_t j = i + (interp ? 2 : 1);
    const std::size_t n = src.size();
    while (j < n) {
        const char c = src[j];
        if (c == '\\') { j += 2; continue; }
        if (c == '"') return j + 1;
        if (c == '\n') return j;   // unterminated on this line; stop here, like the Lexer
        if (interp && c == '{' && j + 1 < n && src[j + 1] == '{') { j += 2; continue; }
        if (interp && c == '{' && depth < kMaxNesting) {
            int braces = 0;
            ++j;
            while (j < n && src[j] != '\n') {
                if (src[j] == '"' || (src[j] == '$' && j + 1 < n && src[j + 1] == '"')) {
                    j = SkipStringLiteral(src, j, depth + 1);
                    continue;
                }
                if (src[j] == '{') { ++braces; ++j; }
                else if (src[j] == '}') { if (braces == 0) { ++j; break; } --braces; ++j; }
                else ++j;
            }
            continue;
        }
        ++j;
    }
    return n;
}
} // namespace detail

inline std::string StripComments(const std::string& src) {
    std::string out;
    out.reserve(src.size());
    const std::size_t n = src.size();
    std::size_t i = 0;
    while (i < n) {
        const char c = src[i];
        if (c == '"' || (c == '$' && i + 1 < n && src[i + 1] == '"')) {
            const std::size_t end = detail::SkipStringLiteral(src, i);
            out.append(src, i, end - i);
            i = end;
            continue;
        }
        if (c == '/' && i + 1 < n && src[i + 1] == '/') {
            i += 2;
            while (i < n && src[i] != '\n') ++i;
            continue;   // the '\n' itself is handled on the next loop iteration
        }
        if (c == '/' && i + 1 < n && src[i + 1] == '*') {
            i += 2;
            while (i < n && !(src[i] == '*' && i + 1 < n && src[i + 1] == '/')) ++i;
            i = (i < n) ? i + 2 : n;
            continue;
        }
        out += c;
        ++i;
    }
    return out;
}

inline std::string CollapseWhitespace(const std::string& src) {
    std::string out;
    out.reserve(src.size());
    std::size_t i = 0;
    const std::size_t n = src.size();
    bool pendingNewline = false;
    while (i < n) {
        // One line at a time, so a blank/whitespace-only line (e.g. where a comment used
        // to be) contributes nothing, and a real line keeps its content with trailing
        // whitespace trimmed.
        std::size_t lineEnd = src.find('\n', i);
        if (lineEnd == std::string::npos) lineEnd = n;
        std::size_t a = i, b = lineEnd;
        while (a < b && (src[a] == ' ' || src[a] == '\t' || src[a] == '\r')) ++a;
        std::size_t trimmedB = b;
        while (trimmedB > a && (src[trimmedB - 1] == ' ' || src[trimmedB - 1] == '\t' || src[trimmedB - 1] == '\r'))
            --trimmedB;
        if (trimmedB > a) {
            if (pendingNewline) out += '\n';
            out.append(src, a, trimmedB - a);
            pendingNewline = true;
        }
        i = (lineEnd < n) ? lineEnd + 1 : n;
    }
    if (pendingNewline) out += '\n';
    return out;
}

inline std::string ProtectSource(const std::string& src, const Options& opts) {
    if (!opts.protectScriptSource) return src;
    return CollapseWhitespace(StripComments(src));
}

} // namespace okay::scriptprotect
