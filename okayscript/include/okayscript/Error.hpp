#pragma once
// OkayScript errors. Every error carries the source line (and column when known; 0 = unknown).
// ScriptError was moved unchanged from engine/src/Scripting/OkayScriptVMInternal.hpp; the engine still throws and
// catches it. SyntaxError (lexer and parser) and RuntimeError (while running) are the two kinds the standalone
// interpreter reports; both are ScriptErrors, so code that catches ScriptError sees them too.
#include <stdexcept>
#include <string>

namespace okayscript {

struct ScriptError : std::runtime_error {
    using std::runtime_error::runtime_error;
    int line = 0;                                  // source line (0 = unknown)
    int column = 0;                                // 1-based column (0 = unknown)
    ScriptError(const std::string& msg, int ln, int col = 0) : std::runtime_error(msg), line(ln), column(col) {}
};

/// The source could not be read: a bad character, an unterminated string, a parse error.
struct SyntaxError : ScriptError {
    using ScriptError::ScriptError;
    explicit SyntaxError(const ScriptError& e) : ScriptError(e.what(), e.line, e.column) {}
};

/// Something went wrong while the program ran: an undefined function, a wrong argument, a `throw`.
struct RuntimeError : ScriptError {
    using ScriptError::ScriptError;
    explicit RuntimeError(const ScriptError& e) : ScriptError(e.what(), e.line, e.column) {}
};

/// "file:line: message" (the CLI's format). `file` may be empty; line 0 is left out.
inline std::string FormatError(const std::string& file, const ScriptError& e) {
    std::string s = file;
    if (e.line > 0) s += (s.empty() ? "" : ":") + std::to_string(e.line);
    if (!s.empty()) s += ": ";
    return s + e.what();
}

} // namespace okayscript
