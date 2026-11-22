#include "token.hpp"

namespace okaycc {

bool isKeywordSpelling(const std::string& spelling) {
    // C17 6.4.1, plus the C++ words this subset accepts. Alphabetical.
    static const char* const kWords[] = {
        "_Alignas", "_Alignof", "_Atomic", "_Bool", "_Complex", "_Generic",
        "_Imaginary", "_Noreturn", "_Static_assert", "_Thread_local",
        "auto", "bool", "catch", "throw", "try", "break", "case", "char", "class", "const", "continue", "default", "delete", "do",
        "double", "else", "enum", "extern", "float", "for", "goto", "if",
        "inline", "int", "long", "namespace", "new", "nullptr", "private", "public", "register", "restrict",
        "return", "short", "signed", "sizeof", "static", "struct", "switch", "this", "typedef", "union",
        "unsigned", "void", "volatile", "while",
    };
    for (const char* w : kWords) {
        if (spelling == w) return true;
    }
    return false;
}

std::string formatToken(const Token& t) {
    std::string o;
    switch (t.kind) {
    case Tok::End: o = "end"; break;
    case Tok::Newline: o = "nl"; break;
    case Tok::Ident: o = "ident " + t.text; break;
    case Tok::Keyword: o = "kw " + t.text; break;
    case Tok::Int:
        o = "int " + t.text + " v=" + std::to_string(t.uval) + " b=" + std::to_string(t.base) +
            " u=" + (t.isUnsigned ? "1" : "0") + " w=" + std::to_string(t.longRank);
        break;
    case Tok::Float:
        o = "float " + t.text + " suf=" + std::to_string(t.floatRank);
        break;
    case Tok::CharLit:
        o = "char " + t.text + " v=" + std::to_string(t.charValue);
        break;
    case Tok::StringLit:
        o = "str " + t.text;
        break;
    case Tok::Punct:
        o = "punct " + t.text;
        break;
    case Tok::Placemarker:
        o = "mark";
        break;
    }
    o += " lead=";
    o += t.leadSpace ? "1" : "0";
    o += " ";
    o += std::to_string(t.line);
    o += ":";
    o += std::to_string(t.column);
    return o;
}

}  // namespace okaycc
