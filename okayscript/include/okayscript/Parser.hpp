#pragma once
// The OkayScript parser: tokens (Lexer.hpp) -> a Program (Ast.hpp). Throws ScriptError with the line and column of the
// token where reading stopped. Grammar and precedence are the engine OkayScriptVM's (engine/src/Scripting/
// OkayScriptVM.cpp), lowest first:
//   lambda (x => e)  <  assignment (= += -= *= /= %= ??=)  <  ?:  <  ??  <  ||  <  &&  <  |  <  ^  <  &
//   <  == !=  <  < > <= >=  <  + -  <  * / %  <  unary (- ! ++ --) and casts  <  postfix (. [] ?. ++ --)
// Statements: var / typed declarations, function definitions (`function f(a)`, `f(a) { }`, `int F(int a)`), if/else,
// while, do-while, for (;;), for x in xs, foreach (var x in xs), switch, break, continue, return, throw, try/catch,
// enums. Semicolons are optional.
#include "okayscript/Ast.hpp"
#include "okayscript/Token.hpp"
#include <cstddef>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace okayscript {

class Parser {
public:
    explicit Parser(std::vector<Token> toks);

    /// The whole token stream as a program.
    void ParseProgram(Program& out);
    /// One expression (for tests and interpolated strings); stops at the first token that cannot continue it.
    ExprPtr ParseExpression();
    /// True once every token but End has been read.
    bool AtEnd() const;

    /// Offset added to every line this parser reports (an interpolated string's expression starts on its line).
    void SetLineOffset(int offset) { m_lineOffset = offset; }

private:
    void ScanEnums();
    bool SkipEnumDecl();
    bool IsEnumAhead() const;
    void ParseMember(Program& prog);
    bool IsBareFunctionAhead() const;
    bool IsTypedFunctionAhead() const;
    bool IsTypedVarDeclAhead() const;
    std::size_t SkipTypeSuffix(std::size_t i) const;
    std::size_t ScanIdentRun(std::size_t i, int& idents) const;
    std::string ConsumeIdentRun(const char* what);
    unsigned char DeclKindAhead() const;
    FunctionDecl ParseFunction();
    FunctionDecl ParseTypedFunction();
    FunctionDecl ParseParamsAndBody(FunctionDecl decl);
    StmtList ParseBlock();
    StmtList ParseBlockOrStmt();
    StmtList SingleStmt();
    StmtPtr ParseStatement();
    StmtPtr ParseStatementInner();
    ExprPtr ParseAssignment();
    ExprPtr ParseTernary();
    bool IsLambdaAhead() const;
    ExprPtr ParseLambda();
    ExprPtr ParseCoalesce();
    ExprPtr ParseOr();
    ExprPtr ParseAnd();
    ExprPtr ParseBitOr();
    ExprPtr ParseBitXor();
    ExprPtr ParseBitAnd();
    ExprPtr ParseEquality();
    ExprPtr ParseComparison();
    ExprPtr ParseTerm();
    ExprPtr ParseFactor();
    ExprPtr ParseUnary();
    ExprPtr ParsePostfix();
    ExprPtr ParseMemberStep(ExprPtr e, bool safe);
    ExprPtr ParseBraceList();
    ExprPtr ParseNew();
    ExprPtr BuildInterp(const std::string& tpl, int line);
    void ParseCallArguments(std::vector<ExprPtr>& args, std::vector<std::string>& names);
    ExprPtr ParsePrimary();
    void CountOperator();

    const Token& Peek() const { return m_toks[m_pos]; }
    const Token& Prev() const { return m_toks[m_pos - 1]; }
    bool Check(Tok t) const { return Peek().type == t; }
    bool CheckWord(const char* w) const { return Peek().type == Tok::Ident && Peek().text == w; }
    bool Match(Tok t) { if (Check(t)) { ++m_pos; return true; } return false; }
    const Token& Expect(Tok t, const char* what);
    [[noreturn]] void Fail(const std::string& msg) const;
    int Line() const { return Peek().line + m_lineOffset; }
    template <class T> std::unique_ptr<T> At(std::unique_ptr<T> n, int line) { n->line = line; return n; }

    std::vector<Token> m_toks;
    std::size_t m_pos = 0;
    int m_lineOffset = 0;
    int m_nesting = 0;
    int m_operators = 0;
    std::set<std::string> m_enums;
    std::unordered_map<std::string, double> m_enumValues;
};

/// Lex and parse `source` into `out`. Throws SyntaxError (line and column) on a lexer or parser error.
void ParseSource(const std::string& source, Program& out);

} // namespace okayscript
