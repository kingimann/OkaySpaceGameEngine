#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace okaycc {

// A preprocessing token, plus a newline we keep so the preprocessor can see
// line boundaries (C17 6.10: a directive is a line that starts with #).
enum class Tok : unsigned {
    End,
    Newline,
    Ident,
    Keyword,
    Int,
    Float,
    CharLit,
    StringLit,
    Punct,
    Placemarker,  // empty macro argument next to ## (C17 6.10.3.3); not from the lexer
};

struct Token {
    Tok kind = Tok::End;
    std::string text;
    std::string decoded;    // string literal contents after escapes
    std::string rawInside;  // characters between the quotes, escapes not interpreted
    int line = 1;
    int column = 1;
    bool leadSpace = false;
    bool fromExpansion = false;
    std::string file;

    uint64_t uval = 0;
    bool isUnsigned = false;
    int longRank = 0;  // 0 none, 1 long, 2 long long
    int base = 10;

    double fval = 0;
    int floatRank = 0;  // 0 double, 1 float, 2 long double

    uint32_t charValue = 0;
    int litWidth = 1;  // 1 narrow or u8, 2 char16, 4 char32 / wide

    // Macro names that must not be expanded again (C17 6.10.3.4).
    std::vector<std::string> hide;

    bool isName() const { return kind == Tok::Ident || kind == Tok::Keyword; }
};

std::string formatToken(const Token& t);
bool isKeywordSpelling(const std::string& spelling);

}  // namespace okaycc
