// The lexer: keywords, number and character literals, tokens.
// Moved out of compile.cpp unchanged (docs/engine/modularization_plan.md, batch T1).
#include "compile_internal.hpp"

namespace okayms {
namespace compiler {
Tok keyword(const std::string& text) {
    if (text == "int") return TInt;
    if (text == "void") return TVoid;
    if (text == "char") return TChar;
    if (text == "if") return TIf;
    if (text == "else") return TElse;
    if (text == "while") return TWhile;
    if (text == "for") return TFor;
    if (text == "do") return TDo;
    if (text == "switch") return TSwitch;
    if (text == "case") return TCase;
    if (text == "default") return TDefault;
    if (text == "break") return TBreak;
    if (text == "continue") return TContinue;
    if (text == "struct" || text == "class") return TStruct;
    if (text == "union") return TUnion;
    if (text == "enum") return TEnum;
    if (text == "typedef") return TTypedef;
    if (text == "return") return TReturn;
    if (text == "unsigned") return TUnsigned;
    if (text == "signed") return TSigned;
    if (text == "long") return TLong;
    if (text == "short") return TShort;
    if (text == "__declspec") return TDeclspec;
    if (text == "sizeof") return TSizeof;
    if (text == "const") return TConst;
    if (text == "static") return TStatic;
    if (text == "goto") return TGoto;
    if (text == "float") return TFloat;
    if (text == "double") return TDouble;
    if (text == "namespace") return TNamespace;
    return TIdent;
}

// value = sig * 10^pow10, written as an IEEE-754 double. A float is that
// value rounded to 24 significant bits.
uint64_t ieee_real(unsigned __int128 sig, int pow10, bool neg, bool single) {
    unsigned __int128 mag = sig;
    int bexp = 0;
    if (sig == 0) return neg ? (single ? 0x80000000ull : 0x8000000000000000ull) : 0;
    if (pow10 >= 0) {
        // Multiplying by ten 300 times does not fit 128 bits: keep the value below 2^123 by moving low bits into the
        // binary exponent first (the bits dropped are below the 53 kept; rounding can differ in the last place).
        for (int i = 0; i < pow10; ++i) {
            while ((mag >> 123) != 0) {
                mag >>= 1;
                ++bexp;
            }
            mag *= 10u;
        }
    } else {
        for (int i = 0; i < -pow10; ++i) {
            while (mag < (static_cast<unsigned __int128>(1) << 120)) {
                mag <<= 1;
                --bexp;
            }
            mag /= 10u;
        }
    }
    int hibit = 127;
    while (hibit > 0 && ((mag >> hibit) & 1u) == 0) --hibit;
    const int keep = single ? 23 : 52;
    const int renorm = hibit - keep;
    if (renorm > 0) {
        const unsigned __int128 rest = mag & ((static_cast<unsigned __int128>(1) << renorm) - 1u);
        mag >>= renorm;
        bexp += renorm;
        const unsigned __int128 half = static_cast<unsigned __int128>(1) << (renorm - 1);
        if (rest > half || (rest == half && (mag & 1u))) ++mag;
        if ((mag >> (keep + 1)) != 0) {
            mag >>= 1;
            ++bexp;
        }
    } else if (renorm < 0) {
        mag <<= -renorm;
        bexp += renorm;
    }
    const int bias = single ? 127 : 1023;
    int encoded = bexp + keep + bias;
    if (encoded <= 0) return neg ? (single ? 0x80000000ull : 0x8000000000000000ull) : 0;
    if (encoded >= (single ? 255 : 2047)) {  // too big for the format: infinity (IEEE 754 overflow)
        if (single) return (neg ? 0x80000000ull : 0) | 0x7F800000ull;
        return (neg ? 0x8000000000000000ull : 0) | 0x7FF0000000000000ull;
    }
    const uint64_t mant_mask = single ? 0x7FFFFFull : 0xFFFFFFFFFFFFFull;
    const uint64_t mant = static_cast<uint64_t>(mag) & mant_mask;
    if (single) return (neg ? 0x80000000ull : 0) | (static_cast<uint64_t>(encoded) << 23) | mant;
    return (neg ? 0x8000000000000000ull : 0) | (static_cast<uint64_t>(encoded) << 52) | mant;
}

int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// i points at the character after the backslash. On success it points past the escape.
bool decode_escape(const std::string& text, size_t* i, unsigned char* out) {
    if (*i >= text.size()) return false;
    const char c = text[*i];
    if (c == 'n') *out = '\n';
    else if (c == 't') *out = '\t';
    else if (c == 'r') *out = '\r';
    else if (c == '0') *out = 0;
    else if (c == 'x') {
        ++*i;
        int value = 0;
        int count = 0;
        while (count < 2 && *i < text.size()) {
            const int digit = hex_digit(text[*i]);
            if (digit < 0) break;
            value = value * 16 + digit;
            ++*i;
            ++count;
        }
        if (count == 0) return false;
        *out = static_cast<unsigned char>(value);
        return true;
    } else *out = static_cast<unsigned char>(c);
    ++*i;
    return true;
}

std::vector<Token> lex(const std::string& text, std::string* error) {
    std::vector<Token> tokens;
    int line = 1;
    for (size_t i = 0; i < text.size();) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (std::isspace(c)) {
            if (c == '\n') ++line;
            ++i;
            continue;
        }
        Token tok;
        tok.line = line;
        tok.adj = c == '>' && i > 0 && text[i - 1] == '>';
        if (std::isalpha(c) || c == '_') {
            size_t j = i + 1;
            while (j < text.size() && (std::isalnum(static_cast<unsigned char>(text[j])) || text[j] == '_')) ++j;
            tok.text = text.substr(i, j - i);
            tok.kind = keyword(tok.text);
            if (tok.text == "true" || tok.text == "false") {
                tok.kind = TNumber;
                tok.number = tok.text == "true" ? 1 : 0;
                tok.ity = 6;  // a bool literal: its value is an int here, its type for overloading is bool
            }
            i = j;
            tokens.push_back(tok);
            continue;
        }
        if (std::isdigit(c)) {
            int base = 10;
            size_t j = i;
            if (text[i] == '0' && i + 1 < text.size() && (text[i + 1] == 'x' || text[i + 1] == 'X')) {
                base = 16;
                j = i + 2;
            } else if (text[i] == '0' && i + 1 < text.size() && (text[i + 1] == 'b' || text[i + 1] == 'B')) {
                base = 2;
                j = i + 2;
            }
            // A leading 0 followed by digits only is octal ([lex.icon]); a '.', an exponent or an f after
            // the digits makes it a decimal floating literal again.
            size_t digits_end = i;
            while (digits_end < text.size() && std::isdigit(static_cast<unsigned char>(text[digits_end]))) ++digits_end;
            const char after_digits = digits_end < text.size() ? text[digits_end] : '\0';
            if (base == 10 && text[i] == '0' && digits_end > i + 1 && after_digits != '.' && after_digits != 'e' &&
                after_digits != 'E' && after_digits != 'f' && after_digits != 'F')
                base = 8;
            uint64_t value = 0;
            bool any = false;
            while (j < text.size()) {
                const char d = text[j];
                // 50'000'000: a ' between digits only separates them ([lex.icon] 1, C++14).
                if (d == '\'' && any && j + 1 < text.size() && std::isxdigit(static_cast<unsigned char>(text[j + 1]))) {
                    ++j;
                    continue;
                }
                int digit = -1;
                if (d >= '0' && d <= '9') digit = d - '0';
                else if (base == 16 && d >= 'a' && d <= 'f') digit = d - 'a' + 10;
                else if (base == 16 && d >= 'A' && d <= 'F') digit = d - 'A' + 10;
                else break;
                if (digit >= base) break;
                any = true;
                value = value * static_cast<uint64_t>(base) + static_cast<uint64_t>(digit);
                ++j;
            }
            // Integer suffixes: u, l and ll in any order and case ([lex.icon]); i64 is the Microsoft spelling.
            bool suf_u = false;
            int suf_l = 0;
            while (j < text.size() && (text[j] == 'u' || text[j] == 'U' || text[j] == 'l' || text[j] == 'L')) {
                if (text[j] == 'u' || text[j] == 'U') suf_u = true;
                else ++suf_l;
                ++j;
            }
            if (suf_l == 0 && j + 2 < text.size() && text[j] == 'i' && text[j + 1] == '6' && text[j + 2] == '4') {
                suf_l = 2;
                j += 3;
            }
            bool real = false;
            uint64_t frac = 0;
            int frac_digits = 0;
            int exp10 = 0;
            bool exp_neg = false;
            if (base == 10 && j < text.size() && text[j] == '.' && j + 1 < text.size() &&
                std::isdigit(static_cast<unsigned char>(text[j + 1]))) {
                real = true;
                ++j;
                while (j < text.size() && std::isdigit(static_cast<unsigned char>(text[j]))) {
                    frac = frac * 10u + static_cast<uint64_t>(text[j] - '0');
                    ++frac_digits;
                    ++j;
                }
            }
            if (base == 10 && j < text.size() && (text[j] == 'e' || text[j] == 'E')) {
                real = true;
                ++j;
                if (j < text.size() && (text[j] == '+' || text[j] == '-')) {
                    exp_neg = text[j] == '-';
                    ++j;
                }
                int e = 0;
                while (j < text.size() && std::isdigit(static_cast<unsigned char>(text[j]))) {
                    e = e * 10 + (text[j] - '0');
                    ++j;
                }
                exp10 = exp_neg ? -e : e;
            }
            bool as_float = false;
            if (j < text.size() && (text[j] == 'f' || text[j] == 'F')) {
                real = true;
                as_float = true;
                ++j;
            }
            if (!any && !real) {
                *error = "bad number";
                return {};
            }
            tok.kind = TNumber;
            if (real) {
                unsigned __int128 sig = value;
                for (int n = 0; n < frac_digits; ++n) sig *= 10u;
                sig += frac;
                tok.number = ieee_real(sig, exp10 - frac_digits, false, as_float);
                tok.real = as_float ? 5 : 6;
            } else {
                tok.number = value;
                // [lex.icc] with a 32-bit long: a decimal number without u goes int, long long; a
                // hexadecimal, octal or binary one goes int, unsigned int, long long, unsigned long long.
                const bool dec = base == 10;
                const bool fits_int = value <= 0x7FFFFFFFull;
                const bool fits_uint = value <= 0xFFFFFFFFull;
                const bool fits_ll = value <= 0x7FFFFFFFFFFFFFFFull;
                if (suf_l >= 2) tok.ity = suf_u ? 3 : (fits_ll ? 2 : 3);
                else if (!suf_u) tok.ity = fits_int ? 0 : (!dec && fits_uint) ? 1 : fits_ll ? 2 : 3;
                else tok.ity = fits_uint ? 1 : 3;
            }
            i = j;
            tokens.push_back(tok);
            continue;
        }
        if (c == '\'') {
            ++i;
            if (i >= text.size()) {
                *error = "unterminated character at line " + std::to_string(line);
                return {};
            }
            unsigned char value = 0;
            if (text[i] == '\\') {
                ++i;
                if (!decode_escape(text, &i, &value)) {
                    *error = "bad character escape";
                    return {};
                }
            } else {
                value = static_cast<unsigned char>(text[i]);
                ++i;
            }
            if (i >= text.size() || text[i] != '\'') {
                *error = "unterminated character at line " + std::to_string(line);
                return {};
            }
            ++i;
            tok.kind = TNumber;
            tok.number = value;
            tok.ity = 5;  // a character literal: its value is an int here, its type for overloading is char
            tokens.push_back(tok);
            continue;
        }
        if (c == '"') {
            std::string value;
            ++i;
            while (i < text.size() && text[i] != '"') {
                if (text[i] == '\\') {
                    ++i;
                    unsigned char escaped = 0;
                    if (!decode_escape(text, &i, &escaped)) {
                        *error = "bad string escape";
                        return {};
                    }
                    value.push_back(static_cast<char>(escaped));
                } else {
                    value.push_back(text[i]);
                    ++i;
                }
            }
            if (i >= text.size()) {
                *error = "unterminated string";
                return {};
            }
            ++i;
            tok.kind = TString;
            tok.text = value;
            tokens.push_back(tok);
            continue;
        }
        auto two = [&](char n, Tok k) {
            if (i + 1 < text.size() && text[i + 1] == n) {
                tok.kind = k;
                i += 2;
                tokens.push_back(tok);
                return true;
            }
            return false;
        };
        if (c == '-' && i + 1 < text.size() && text[i + 1] == '>') {
            tok.kind = TArrow;
            i += 2;
            tokens.push_back(tok);
            continue;
        }
        if (c == '.' && i + 2 < text.size() && text[i + 1] == '.' && text[i + 2] == '.') {
            tok.kind = TEllipsis;
            i += 3;
            tokens.push_back(tok);
            continue;
        }
        if (c == '+' && two('+', TPlusPlus)) continue;
        if (c == '+' && two('=', TPlusEq)) continue;
        if (c == '-' && two('-', TMinusMinus)) continue;
        if (c == '-' && two('=', TMinusEq)) continue;
        if (c == '*' && two('=', TStarEq)) continue;
        if (c == '/' && two('=', TSlashEq)) continue;
        if (c == '%' && two('=', TPercentEq)) continue;
        if (c == '=' && two('=', TEq)) continue;
        if (c == '!' && two('=', TNe)) continue;
        if (c == '<' && two('=', TLe)) continue;
        if (c == '>' && two('=', TGe)) continue;
        if (c == '&' && two('&', TAndAnd)) continue;
        if (c == '|' && two('|', TOrOr)) continue;
        if (c == '<' && i + 2 < text.size() && text[i + 1] == '<' && text[i + 2] == '=') {
            tok.kind = TShlEq;
            i += 3;
            tokens.push_back(tok);
            continue;
        }
        if (c == '<' && two('<', TShl)) continue;
        if (c == '&' && two('=', TAndEq)) continue;
        if (c == '|' && two('=', TOrEq)) continue;
        if (c == '^' && two('=', TXorEq)) continue;
        tok.kind = TEnd;
        if (c == '(') tok.kind = TLParen;
        else if (c == ')') tok.kind = TRParen;
        else if (c == '{') tok.kind = TLBrace;
        else if (c == '}') tok.kind = TRBrace;
        else if (c == ';') tok.kind = TSemi;
        else if (c == ',') tok.kind = TComma;
        else if (c == '*') tok.kind = TStar;
        else if (c == '&') tok.kind = TAmp;
        else if (c == '+') tok.kind = TPlus;
        else if (c == '-') tok.kind = TMinus;
        else if (c == '/') tok.kind = TSlash;
        else if (c == '%') tok.kind = TPercent;
        else if (c == '=') tok.kind = TAssign;
        else if (c == '?') tok.kind = TQuestion;
        else if (c == '<') tok.kind = TLt;
        else if (c == '>') tok.kind = TGt;
        else if (c == '!') tok.kind = TNot;
        else if (c == '~') tok.kind = TTilde;
        else if (c == '|') tok.kind = TPipe;
        else if (c == '^') tok.kind = TCaret;
        else if (c == '[') tok.kind = TLBrack;
        else if (c == ']') tok.kind = TRBrack;
        else if (c == '.') tok.kind = TDot;
        else if (c == ':' && i + 1 < text.size() && text[i + 1] == ':') {
            tok.kind = TColonColon;
            i += 2;
            tokens.push_back(tok);
            continue;
        } else if (c == ':') tok.kind = TColon;
        else {
            *error = std::string("unexpected character '") + static_cast<char>(c) + "' at line " + std::to_string(line);
            return {};
        }
        ++i;
        tokens.push_back(tok);
    }
    Token end_token;
    end_token.line = line;
    tokens.push_back(end_token);
    return tokens;
}

bool is_assign_op(Tok op) {
    return op == TAssign || op == TPlusEq || op == TMinusEq || op == TStarEq || op == TSlashEq || op == TPercentEq ||
           op == TAndEq || op == TOrEq || op == TXorEq || op == TShlEq || op == TShrEq;
}
bool is_compound_op(Tok op) { return op != TAssign && is_assign_op(op); }


}  // namespace compiler
}  // namespace okayms
