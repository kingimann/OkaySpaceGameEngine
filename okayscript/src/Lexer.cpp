// The OkayScript lexer. Moved unchanged from engine/src/Scripting/OkayScriptVMInternal.hpp (written for this
// project; no other project's code). Only the class split into Lexer.hpp (declarations) and this file changed.
#include "okayscript/Lexer.hpp"
#include <cctype>
#include <string>
#include <unordered_map>

namespace okayscript {

const char* TokName(Tok t) {
    switch (t) {
        case Tok::End: return "end of file"; case Tok::Number: return "number"; case Tok::String: return "string";
        case Tok::InterpString: return "interpolated string"; case Tok::Ident: return "identifier";
        case Tok::Var: return "'var'"; case Tok::If: return "'if'"; case Tok::Else: return "'else'";
        case Tok::While: return "'while'"; case Tok::For: return "'for'"; case Tok::In: return "'in'";
        case Tok::Function: return "'function'"; case Tok::Return: return "'return'"; case Tok::True: return "'true'";
        case Tok::False: return "'false'"; case Tok::Break: return "'break'"; case Tok::Continue: return "'continue'";
        case Tok::Question: return "'?'"; case Tok::Colon: return "':'"; case Tok::QuestionQuestion: return "'?\?'";
        case Tok::QuestionQuestionEq: return "'?\?='"; case Tok::QuestionDot: return "'?.'";
        case Tok::Plus: return "'+'"; case Tok::Minus: return "'-'"; case Tok::Star: return "'*'";
        case Tok::Slash: return "'/'"; case Tok::Percent: return "'%'"; case Tok::Assign: return "'='";
        case Tok::PlusEq: return "'+='"; case Tok::MinusEq: return "'-='"; case Tok::StarEq: return "'*='";
        case Tok::SlashEq: return "'/='"; case Tok::Inc: return "'++'"; case Tok::Dec: return "'--'";
        case Tok::Eq: return "'=='"; case Tok::Ne: return "'!='"; case Tok::Lt: return "'<'"; case Tok::Gt: return "'>'";
        case Tok::Le: return "'<='"; case Tok::Ge: return "'>='"; case Tok::Not: return "'!'"; case Tok::And: return "'&&'";
        case Tok::Or: return "'||'"; case Tok::LParen: return "'('"; case Tok::RParen: return "')'";
        case Tok::LBrace: return "'{'"; case Tok::RBrace: return "'}'"; case Tok::LBracket: return "'['";
        case Tok::RBracket: return "']'"; case Tok::Comma: return "','"; case Tok::Semicolon: return "';'";
        case Tok::Dot: return "'.'"; case Tok::PercentEq: return "'%='"; case Tok::BitAnd: return "'&'";
        case Tok::BitOr: return "'|'"; case Tok::BitXor: return "'^'"; case Tok::Arrow: return "'=>'";
    }
    return "token";
}


std::vector<Token> Lexer::Scan() {
    std::vector<Token> out;
    while (!AtEnd()) {
        SkipTrivia();
        if (AtEnd()) break;
        const int line = m_line, col = m_col;
        char c = Peek();
        Token t = (std::isdigit((unsigned char)c) || (c == '.' && std::isdigit((unsigned char)Peek(1)))) ? Number()
                : (std::isalpha((unsigned char)c) || c == '_') ? Ident()
                : (c == '"' && Peek(1) == '"' && Peek(2) == '"') ? TripleString(false)
                : c == '"' ? String()
                : c == '\'' ? CharLiteral()
                : (c == '$' && Peek(1) == '"' && Peek(2) == '"' && Peek(3) == '"') ? TripleString(true)
                : (c == '$' && Peek(1) == '"') ? InterpString()
                : (c == '@' && Peek(1) == '"') ? VerbatimString()
                : Operator();
        t.line = line;
        t.column = col;
        out.push_back(std::move(t));
    }
    Token end{Tok::End, "", 0, m_line};
    end.column = m_col;
    out.push_back(std::move(end));
    return out;
}

bool Lexer::AtEnd() const { return m_pos >= m_src.size(); }
char Lexer::Peek(int o) const { return (m_pos + o < m_src.size()) ? m_src[m_pos + o] : '\0'; }
char Lexer::Advance() {
    char c = m_src[m_pos++];
    if (c == '\n') { ++m_line; m_col = 1; }
    else if (c != '\r') ++m_col;
    return c;
}

void Lexer::SkipTrivia() {
    while (!AtEnd()) {
        char c = Peek();
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') { Advance(); }
        else if (c == '/' && Peek(1) == '/') { while (!AtEnd() && Peek() != '\n') Advance(); }
        else if (c == '/' && Peek(1) == '*') {           // C#-style /* block comment */
            Advance(); Advance();
            while (!AtEnd() && !(Peek() == '*' && Peek(1) == '/')) Advance();
            if (!AtEnd()) { Advance(); Advance(); }
        }
        else if (c == '#') { while (!AtEnd() && Peek() != '\n') Advance(); }
        else break;
    }
}

Token Lexer::Number() {
    std::string s;
    while (std::isdigit((unsigned char)Peek()) || Peek() == '.') s += Advance();
    // Accept C# numeric suffixes (5f, 1.0f, 2d) so pasted code runs; every
    // number is already a decimal, so the editor flags them as unnecessary.
    bool suffix = false, isLong = false;
    if (Peek() == 'f' || Peek() == 'F' || Peek() == 'd' || Peek() == 'D') { Advance(); suffix = true; }
    else if ((Peek() == 'L' || Peek() == 'l') && !std::isalnum((unsigned char)Peek(1)) && Peek(1) != '_') {
        // 123L: a 64-bit whole number (where the value type has them; else the same number as 123)
        if (s.find('.') != std::string::npos) throw ScriptError("a number with a fraction can't be a whole number (L)", m_line, m_col);
        Advance();
        isLong = true;
    }
    Token t{Tok::Number, s, std::stod(s), m_line};
    t.suffix = suffix;
    t.isLong = isLong;
    return t;
}

Token Lexer::String() {
    const int sl = m_line, sc = m_col;
    Advance(); // opening quote
    std::string s;
    while (!AtEnd() && Peek() != '"') {
        char c = Advance();
        if (c == '\\' && !AtEnd()) {
            char e = Advance();
            switch (e) { case 'n': c = '\n'; break; case 't': c = '\t'; break;
                         case '"': c = '"'; break; case '\\': c = '\\'; break;
                         default: c = e; }
        }
        s += c;
    }
    if (AtEnd()) throw ScriptError("unterminated string", sl, sc);
    Advance(); // closing quote
    return {Tok::String, s, 0, m_line};
}

// C# char literal 'x' (or '\n') — a one-character string.
Token Lexer::CharLiteral() {
    const int sl = m_line, sc = m_col;
    Advance();   // opening quote
    std::string s;
    while (!AtEnd() && Peek() != '\'' && Peek() != '\n') {
        char c = Advance();
        if (c == '\\' && !AtEnd()) {
            char e = Advance();
            switch (e) { case 'n': c = '\n'; break; case 't': c = '\t'; break; case '0': c = '\0'; break; default: c = e; }
        }
        s += c;
    }
    if (AtEnd() || Peek() != '\'') throw ScriptError("unterminated character literal", sl, sc);
    Advance();   // closing quote
    return {Tok::String, s, 0, m_line};
}

// C#-style interpolated string: $"Score: {score}". The raw inner text (with
// {expr} markers preserved) is stored; the parser splits and compiles it.
Token Lexer::InterpString() {
    const int sl = m_line, sc = m_col;
    Advance(); // '$'
    Advance(); // opening quote
    std::string s;
    while (!AtEnd() && Peek() != '"') {
        char c = Advance();
        if (c == '\\' && !AtEnd()) {
            char e = Advance();
            switch (e) { case 'n': c = '\n'; break; case 't': c = '\t'; break;
                         case '"': c = '"'; break; case '\\': c = '\\'; break;
                         default: c = e; }
        }
        s += c;
    }
    if (AtEnd()) throw ScriptError("unterminated string", sl, sc);
    Advance(); // closing quote
    return {Tok::InterpString, s, 0, m_line};
}

Token Lexer::TripleString(bool interp) {
    const int sl = m_line, sc = m_col;
    if (interp) Advance();                    // '$'
    Advance(); Advance(); Advance();          // the opening quotes
    if (Peek() == '\r' && Peek(1) == '\n') { Advance(); Advance(); }
    else if (Peek() == '\n') Advance();
    std::string s;
    while (!AtEnd() && !(Peek() == '"' && Peek(1) == '"' && Peek(2) == '"')) {
        const char c = Advance();
        if (c != '\r') s += c;
    }
    if (AtEnd()) throw ScriptError("unterminated string (\"\"\" with no closing \"\"\")", sl, sc);
    Advance(); Advance(); Advance();
    return {interp ? Tok::InterpString : Tok::String, s, 0, m_line};
}

Token Lexer::VerbatimString() {
    const int sl = m_line, sc = m_col;
    Advance(); Advance();   // @ and the opening quote
    std::string s;
    for (;;) {
        if (AtEnd()) throw ScriptError("unterminated string", sl, sc);
        const char c = Advance();
        if (c == '"') {
            if (Peek() == '"') { Advance(); s += '"'; continue; }
            break;
        }
        if (c != '\r') s += c;
    }
    return {Tok::String, s, 0, m_line};
}

Token Lexer::Ident() {
    std::string s;
    while (std::isalnum((unsigned char)Peek()) || Peek() == '_') s += Advance();
    // dotted names are a single token: Input.GetKeyDown,
    // Time.deltaTime, transform.position.x, gameObject.SetActive.
    while (Peek() == '.' && (std::isalpha((unsigned char)Peek(1)) || Peek(1) == '_')) {
        s += Advance();                                   // the '.'
        while (std::isalnum((unsigned char)Peek()) || Peek() == '_') s += Advance();
    }
    static const std::unordered_map<std::string, Tok> kw = {
        {"var", Tok::Var}, {"if", Tok::If}, {"else", Tok::Else},
        {"while", Tok::While}, {"for", Tok::For}, {"in", Tok::In},
        {"function", Tok::Function}, {"return", Tok::Return},
        {"true", Tok::True}, {"false", Tok::False},
        {"break", Tok::Break}, {"continue", Tok::Continue}};
    // `this.hp` stays one token. With no plain-class object in play it still
    // means the global `hp` (a component's fields are its globals). Inside a
    // method, `this.name` is that object's field, not a parameter of the same name.
    auto it = kw.find(s);
    return {it != kw.end() ? it->second : Tok::Ident, s, 0, m_line};
}

Token Lexer::Operator() {
    const int sl = m_line, sc = m_col;
    char c = Advance();
    auto two = [&](char n, Tok t) { if (Peek() == n) { Advance(); return Token{t, "", 0, m_line}; } return Token{Tok::End, "", 0, m_line}; };
    switch (c) {
        case '+': { if (Peek() == '+') { Advance(); return {Tok::Inc, "", 0, m_line}; }
                    auto t = two('=', Tok::PlusEq);  return t.type != Tok::End ? t : Token{Tok::Plus, "", 0, m_line}; }
        case '-': { if (Peek() == '-') { Advance(); return {Tok::Dec, "", 0, m_line}; }
                    auto t = two('=', Tok::MinusEq); return t.type != Tok::End ? t : Token{Tok::Minus, "", 0, m_line}; }
        case '*': { auto t = two('=', Tok::StarEq);  return t.type != Tok::End ? t : Token{Tok::Star, "", 0, m_line}; }
        case '/': { auto t = two('=', Tok::SlashEq); return t.type != Tok::End ? t : Token{Tok::Slash, "", 0, m_line}; }
        case '%': { auto t = two('=', Tok::PercentEq); return t.type != Tok::End ? t : Token{Tok::Percent, "", 0, m_line}; }
        case '^': return {Tok::BitXor, "", 0, m_line};
        case '(': return {Tok::LParen, "", 0, m_line};
        case ')': return {Tok::RParen, "", 0, m_line};
        case '{': return {Tok::LBrace, "", 0, m_line};
        case '}': return {Tok::RBrace, "", 0, m_line};
        case '[': return {Tok::LBracket, "", 0, m_line};
        case ']': return {Tok::RBracket, "", 0, m_line};
        case '?':
            if (Peek() == '?') {
                Advance();
                if (Peek() == '=') { Advance(); return {Tok::QuestionQuestionEq, "", 0, m_line}; }
                return {Tok::QuestionQuestion, "", 0, m_line};
            }
            if (Peek() == '.') { Advance(); return {Tok::QuestionDot, "", 0, m_line}; }
            return {Tok::Question, "", 0, m_line};
        case ':': return {Tok::Colon, "", 0, m_line};
        case ',': return {Tok::Comma, "", 0, m_line};
        case ';': return {Tok::Semicolon, "", 0, m_line};
        case '.': return {Tok::Dot, "", 0, m_line};
        case '=': { if (Peek() == '>') { Advance(); return {Tok::Arrow, "", 0, m_line}; }
                    auto t = two('=', Tok::Eq); return t.type != Tok::End ? t : Token{Tok::Assign, "", 0, m_line}; }
        case '!': { auto t = two('=', Tok::Ne); return t.type != Tok::End ? t : Token{Tok::Not, "", 0, m_line}; }
        case '<': { auto t = two('=', Tok::Le); return t.type != Tok::End ? t : Token{Tok::Lt, "", 0, m_line}; }
        case '>': { auto t = two('=', Tok::Ge); return t.type != Tok::End ? t : Token{Tok::Gt, "", 0, m_line}; }
        case '&': if (Peek() == '&') { Advance(); return {Tok::And, "", 0, m_line}; } return {Tok::BitAnd, "", 0, m_line};
        case '|': if (Peek() == '|') { Advance(); return {Tok::Or, "", 0, m_line}; } return {Tok::BitOr, "", 0, m_line};
    }
    throw ScriptError(std::string("unexpected character '") + c + "'", sl, sc);
}

} // namespace okayscript
