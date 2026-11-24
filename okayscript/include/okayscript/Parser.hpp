#pragma once
// The OkayScript parser: tokens (Lexer.hpp) -> a Program (Ast.hpp). Throws ScriptError with the line and column of the
// token where reading stopped. The grammar is the engine OkayScriptVM's, C#-style and C-style both, lowest first:
//   lambda (x => e)  <  assignment (= += -= *= /= %= ??=)  <  ?:  <  ??  <  ||  <  &&  <  |  <  ^  <  &
//   <  == !=  <  < > <= >=  <  + -  <  * / %  <  unary (- ! ++ --) and casts  <  postfix (. [] ?. ++ --)
// Statements: var and typed declarations (const, static), functions (`function f(a)`, `f(a) { }`, `int F(int a)`),
// if/else, while, do-while, for (;;), for x in xs, foreach, switch, break, continue, return, throw, try/catch,
// yield return, enums, plain classes with `new`, and the C# wrappers that do nothing (using, namespace,
// `class Name : Base { }`, [attributes], { get; set; }). Semicolons are optional.
#include "okayscript/Ast.hpp"
#include "okayscript/Token.hpp"
#include <memory>
#include <string>
#include <vector>

namespace okayscript {

/// One problem found in a source: severity 0 = error (won't run as written), 1 = warning (runs, but probably not as
/// meant, or does nothing). Column is 1-based, 0 = not known.
struct Diagnostic {
    int line = 0;
    std::string message;
    int severity = 0;
    int column = 0;
};

class ParserImpl;

class Parser {
public:
    explicit Parser(std::vector<Token> toks);
    ~Parser();
    Parser(const Parser&) = delete;
    Parser& operator=(const Parser&) = delete;

    /// The whole token stream as a program (throws ScriptError at the first problem).
    void ParseProgram(Program& out);
    /// Keep going after errors: every syntax error, the type mistakes found while parsing, and the "does nothing"
    /// warnings, sorted by line. The parsed program is thrown away.
    std::vector<Diagnostic> ParseProgramRecovering();
    /// One expression (for tests); stops at the first token that cannot continue it.
    ExprPtr ParseExpression();
    /// True once every token but End has been read.
    bool AtEnd() const;

private:
    std::unique_ptr<ParserImpl> m_impl;
};

/// Lex and parse `source` into `out`. Throws SyntaxError (line and column) on a lexer or parser error.
void ParseSource(const std::string& source, Program& out);
/// Every problem in `source` (lexer error, syntax errors, type mistakes, "does nothing" warnings), never throws.
std::vector<Diagnostic> ValidateSource(const std::string& source);

} // namespace okayscript
