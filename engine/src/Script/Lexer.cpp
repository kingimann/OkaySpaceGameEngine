#include "ScriptInternal.hpp"
#include <cctype>

namespace okay::script {

namespace {
const std::unordered_map<std::string, T>& Keywords() {
    static const std::unordered_map<std::string, T> k = {
        {"class", T::KwClass}, {"public", T::KwPublic}, {"private", T::KwPrivate}, {"void", T::KwVoid},
        {"int", T::KwInt}, {"float", T::KwFloat}, {"bool", T::KwBool}, {"string", T::KwString},
        {"var", T::KwVar}, {"if", T::KwIf}, {"else", T::KwElse}, {"while", T::KwWhile}, {"for", T::KwFor},
        {"return", T::KwReturn}, {"break", T::KwBreak}, {"continue", T::KwContinue}, {"true", T::KwTrue},
        {"false", T::KwFalse}, {"null", T::KwNull}, {"new", T::KwNew}, {"this", T::KwThis},
        {"const", T::KwConst}};
    return k;
}
} // namespace

const char* TokenName(T t) {
    switch (t) {
        case T::End: return "end of file"; case T::Ident: return "a name"; case T::IntLit: return "a number";
        case T::FloatLit: return "a number"; case T::StringLit: return "text";
        case T::LParen: return "'('"; case T::RParen: return "')'"; case T::LBrace: return "'{'";
        case T::RBrace: return "'}'"; case T::LBracket: return "'['"; case T::RBracket: return "']'";
        case T::Comma: return "','"; case T::Dot: return "'.'"; case T::Semicolon: return "';'";
        case T::Colon: return "':'"; case T::Question: return "'?'";
        case T::Plus: return "'+'"; case T::Minus: return "'-'"; case T::Star: return "'*'"; case T::Slash: return "'/'";
        case T::Percent: return "'%'"; case T::Not: return "'!'"; case T::Lt: return "'<'"; case T::Gt: return "'>'";
        case T::Le: return "'<='"; case T::Ge: return "'>='"; case T::EqEq: return "'=='"; case T::NotEq: return "'!='";
        case T::AndAnd: return "'&&'"; case T::OrOr: return "'||'"; case T::Assign: return "'='";
        case T::PlusEq: return "'+='"; case T::MinusEq: return "'-='"; case T::StarEq: return "'*='";
        case T::SlashEq: return "'/='"; case T::PercentEq: return "'%='"; case T::PlusPlus: return "'++'";
        case T::MinusMinus: return "'--'";
        default: return "a keyword";
    }
}

std::vector<Token> Lex(const std::string& src, std::vector<ScriptDiagnostic>& diags) {
    std::vector<Token> out;
    std::size_t i = 0;
    int line = 1, col = 1;
    auto peek = [&](std::size_t o = 0) -> char { return i + o < src.size() ? src[i + o] : '\0'; };
    auto adv = [&]() -> char {
        char c = src[i++];
        if (c == '\n') { ++line; col = 1; } else ++col;
        return c;
    };
    auto err = [&](SrcLoc l, const std::string& m) { diags.push_back({l.line, m, 0}); };
    while (i < src.size()) {
        char c = peek();
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') { adv(); continue; }
        if (c == '/' && peek(1) == '/') { while (i < src.size() && peek() != '\n') adv(); continue; }
        if (c == '/' && peek(1) == '*') {
            SrcLoc at{line, col};
            adv(); adv();
            while (i < src.size() && !(peek() == '*' && peek(1) == '/')) adv();
            if (i >= src.size()) { err(at, "This comment is never closed: add */"); break; }
            adv(); adv();
            continue;
        }
        Token t;
        t.loc = {line, col};
        if (std::isalpha((unsigned char)c) || c == '_') {
            while (std::isalnum((unsigned char)peek()) || peek() == '_') t.text += adv();
            auto k = Keywords().find(t.text);
            t.type = k != Keywords().end() ? k->second : T::Ident;
            out.push_back(std::move(t));
            continue;
        }
        if (std::isdigit((unsigned char)c) || (c == '.' && std::isdigit((unsigned char)peek(1)))) {
            bool isFloat = false;
            while (std::isdigit((unsigned char)peek())) t.text += adv();
            if (peek() == '.' && std::isdigit((unsigned char)peek(1))) {
                isFloat = true;
                t.text += adv();
                while (std::isdigit((unsigned char)peek())) t.text += adv();
            }
            if (peek() == 'f' || peek() == 'F') { adv(); isFloat = true; }   // 5f is a float, like 5.0
            if (std::isalpha((unsigned char)peek()) || peek() == '_') {
                std::string rest;
                while (std::isalnum((unsigned char)peek()) || peek() == '_') rest += adv();
                err(t.loc, "'" + t.text + rest + "' isn't a number. Numbers are written like 5, 2.5 or 2.5f");
            }
            t.type = isFloat ? T::FloatLit : T::IntLit;
            out.push_back(std::move(t));
            continue;
        }
        if (c == '"') {
            adv();
            bool closed = false;
            while (i < src.size()) {
                char d = adv();
                if (d == '"') { closed = true; break; }
                if (d == '\n') break;
                if (d == '\\' && i < src.size()) {
                    char e = adv();
                    switch (e) {
                        case 'n': t.text += '\n'; break; case 't': t.text += '\t'; break;
                        case '"': t.text += '"'; break; case '\\': t.text += '\\'; break;
                        default: err({line, col - 2}, std::string("'\\") + e + "' isn't an escape OkayScript knows (use \\n, \\t, \\\" or \\\\)");
                    }
                    continue;
                }
                t.text += d;
            }
            if (!closed) err(t.loc, "This text is never closed: add a closing \"");
            t.type = T::StringLit;
            out.push_back(std::move(t));
            continue;
        }
        auto two = [&](char a, char b) { return peek() == a && peek(1) == b; };
        struct Op { const char* s; T t; };
        static const Op ops[] = {
            {"++", T::PlusPlus}, {"--", T::MinusMinus}, {"+=", T::PlusEq}, {"-=", T::MinusEq}, {"*=", T::StarEq},
            {"/=", T::SlashEq}, {"%=", T::PercentEq}, {"==", T::EqEq}, {"!=", T::NotEq}, {"<=", T::Le},
            {">=", T::Ge}, {"&&", T::AndAnd}, {"||", T::OrOr}};
        bool matched = false;
        for (const Op& o : ops)
            if (two(o.s[0], o.s[1])) { adv(); adv(); t.type = o.t; t.text = o.s; matched = true; break; }
        if (!matched) {
            switch (c) {
                case '(': t.type = T::LParen; break; case ')': t.type = T::RParen; break;
                case '{': t.type = T::LBrace; break; case '}': t.type = T::RBrace; break;
                case '[': t.type = T::LBracket; break; case ']': t.type = T::RBracket; break;
                case ',': t.type = T::Comma; break; case '.': t.type = T::Dot; break;
                case ';': t.type = T::Semicolon; break; case ':': t.type = T::Colon; break;
                case '?': t.type = T::Question; break;
                case '+': t.type = T::Plus; break; case '-': t.type = T::Minus; break;
                case '*': t.type = T::Star; break; case '/': t.type = T::Slash; break;
                case '%': t.type = T::Percent; break; case '!': t.type = T::Not; break;
                case '<': t.type = T::Lt; break; case '>': t.type = T::Gt; break;
                case '=': t.type = T::Assign; break;
                default:
                    err(t.loc, std::string("'") + c + "' isn't used in OkayScript");
                    adv();
                    continue;
            }
            t.text = std::string(1, adv());
        }
        out.push_back(std::move(t));
    }
    Token end; end.type = T::End; end.loc = {line, col};
    out.push_back(end);
    return out;
}

} // namespace okay::script
