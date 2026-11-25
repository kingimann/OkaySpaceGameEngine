#pragma once
// OkayScript tokens: what the lexer (Lexer.hpp) turns source text into. Engine-independent (C++17 standard library only).
// Moved unchanged from engine/src/Scripting/OkayScriptVMInternal.hpp (the engine's OkayScriptVM uses these same types).
#include <string>

namespace okayscript {

enum class Tok {
    End, Number, String, InterpString, Ident,
    Var, If, Else, While, For, In, Function, Return, True, False, Break, Continue,
    Question, Colon, QuestionQuestion, QuestionQuestionEq, QuestionDot,
    Plus, Minus, Star, Slash, Percent,
    Assign, PlusEq, MinusEq, StarEq, SlashEq, Inc, Dec,
    Eq, Ne, Lt, Gt, Le, Ge, Not, And, Or,
    LParen, RParen, LBrace, RBrace, LBracket, RBracket, Comma, Semicolon,
    Dot,  // a '.' after ')' or ']' — member access on an expression: (a + b).magnitude
    PercentEq, BitAnd, BitOr, BitXor, Arrow   // Arrow: '=>' (lambdas)
};

struct Token {
    Tok type;
    std::string text;
    double number = 0.0;
    int line = 0;
    bool suffix = false;   ///< a number written with a C# suffix (5f, 2d)
    bool isLong = false;   ///< a whole number written with the suffix L (123L): a 64-bit integer
    int column = 0;        ///< 1-based column where this token starts
};

/// A short readable name for a token kind ("number", "'+'", "end of file"), for error messages and tests.
const char* TokName(Tok t);

} // namespace okayscript
