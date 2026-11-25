#pragma once
// The OkayScript lexer: source text -> tokens (Token.hpp). Throws ScriptError (with line and column) on an
// unterminated string or character literal and on a character that starts no token.
// Moved unchanged from engine/src/Scripting/OkayScriptVMInternal.hpp; the engine's OkayScriptVM uses this class.
#include "okayscript/Error.hpp"
#include "okayscript/Token.hpp"
#include <cstddef>
#include <string>
#include <vector>

namespace okayscript {

class Lexer {
public:
    /// `src` must outlive the Lexer (it is read in place).
    explicit Lexer(const std::string& src) : m_src(src) {}

    /// Every token in the source, ending with one Tok::End.
    std::vector<Token> Scan();

private:
    bool AtEnd() const;
    char Peek(int o = 0) const;
    char Advance();
    void SkipTrivia();
    Token Number();
    Token String();
    Token CharLiteral();
    Token InterpString();
    /// """text""" (and $"""..."""): many lines, no escapes; a line break right after the opening quotes is dropped.
    Token TripleString(bool interp);
    /// @"text": C#'s verbatim string, many lines, no escapes, "" is one quote.
    Token VerbatimString();
    Token Ident();
    Token Operator();

    const std::string& m_src;
    std::size_t m_pos = 0;
    int m_line = 1;
    int m_col = 1;
};

} // namespace okayscript
