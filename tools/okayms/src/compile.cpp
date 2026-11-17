#include "okayms/compile.hpp"
#include "okayms/unwind.hpp"

#include <cctype>
#include <cstring>
#include <fstream>
#include <map>
#include <set>

namespace okayms {
namespace {

enum Tok {
    TEnd, TIdent, TNumber, TString,
    TLParen, TRParen, TLBrace, TRBrace, TSemi, TComma, TStar, TAmp,
    TPlus, TMinus, TSlash, TPercent, TAssign, TEq, TNe, TLt, TGt, TLe, TGe,
    TPlusPlus, TMinusMinus, TPlusEq, TMinusEq, TStarEq, TSlashEq, TPercentEq, TQuestion,
    TAndAnd, TOrOr, TNot, TLBrack, TRBrack, TDot, TArrow, TEllipsis, TColon,
    TInt, TVoid, TChar, TIf, TElse, TWhile, TFor, TDo, TSwitch, TCase, TDefault, TBreak, TReturn,
    TStruct, TUnion, TEnum, TTypedef, TUnsigned, TSigned, TLong, TShort, TDeclspec,
    TSizeof, TConst, TStatic, TGoto, TFloat, TDouble
};

struct Token {
    Tok kind = TEnd;
    std::string text;
    uint64_t number = 0;
    // 0 for an integer, 5 for a float literal, 6 for a double literal.
    int real = 0;
};

struct Type {
    int pointers = 0;
    // 0 void, 1 int, 2 char, 3 struct or union, 4 short
    int base = 1;
    int array_len = -1;
    int struct_id = -1;
    int func_id = -1;
    int bits = -1;
    int bit_off = 0;
    bool is_unsigned = false;
    bool is_const = false;
    bool pointer() const { return pointers > 0; }
    bool is_array() const { return array_len >= 0; }
    bool is_func() const { return func_id >= 0; }
};

struct FuncType {
    Type ret{};
    std::vector<Type> params;
    bool vararg = false;
};

struct Field {
    std::string name;
    Type type{};
    int offset = 0;
};

struct StructDef {
    std::string name;
    std::vector<Field> fields;
    int size = 0;
    int align = 1;
    bool is_union = false;
};

struct Global {
    std::string name;
    Type type{};
    bool bss = true;
    bool is_static = false;
    uint32_t init = 0;
};

struct Node {
    enum Kind {
        Block, Return, If, While, For, Do, Switch, Case, Break, ExprStmt,
        IntLit, StringLit, Name, Unary, Binary, Assign, Call, Index, Field,
        Ternary, Goto, Label
    } kind = Block;
    std::string text;
    uint64_t number = 0;
    Tok op = TEnd;
    Type type{};
    int32_t offset = 0;
    bool imported = false;
    std::vector<Node> kids;
};

struct Local {
    std::string name;
    Type type{};
    int32_t offset = 0;
    bool is_static = false;
    bool is_const = false;
    std::string symbol;
    int reg = 0;
    // 0 is the parameter scope. A nested block uses a higher number.
    int scope = 0;
};

struct FuncDecl {
    std::string name;
    Type ret{};
    std::vector<Type> params;
    std::vector<std::string> param_names;
    bool imported = false;
    bool exported = false;
    bool defined = false;
    bool is_static = false;
    bool vararg = false;
    bool sret = false;
    int func_id = -1;
    int sret_off = 0;
    int param_base = 0;
    Node body;
    std::vector<Local> locals;
    int nparams = 0;
    int max_args = 0;
};

std::string strip_comments(const std::string& in) {
    std::string out;
    for (size_t i = 0; i < in.size();) {
        if (in[i] == '"' || in[i] == '\'') {
            const char quote = in[i];
            out.push_back(in[i++]);
            while (i < in.size() && in[i] != quote) {
                if (in[i] == '\\' && i + 1 < in.size()) out.push_back(in[i++]);
                out.push_back(in[i++]);
            }
            if (i < in.size()) out.push_back(in[i++]);
            continue;
        }
        if (in[i] == '/' && i + 1 < in.size() && in[i + 1] == '/') {
            i += 2;
            while (i < in.size() && in[i] != '\n') ++i;
            continue;
        }
        if (in[i] == '/' && i + 1 < in.size() && in[i + 1] == '*') {
            i += 2;
            while (i + 1 < in.size() && !(in[i] == '*' && in[i + 1] == '/')) ++i;
            if (i + 1 < in.size()) i += 2;
            continue;
        }
        out.push_back(in[i++]);
    }
    return out;
}

bool read_file(const std::string& path, std::string* text) {
    std::ifstream in(path.c_str(), std::ios::binary);
    if (!in) return false;
    text->assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

struct Macro {
    bool function_like = false;
    std::vector<std::string> params;
    std::string body;
};

struct Preprocessor {
    std::map<std::string, Macro> macros;
    std::vector<std::string> includes;
    std::string error;
    int depth = 0;

    bool defined(const std::string& name) const { return macros.find(name) != macros.end(); }

    // Integer constant expression for #if / #elif. An undefined name is 0.
    // defined(name) is replaced before macro expansion, as C requires.
    int64_t eval_pp(const std::string& raw, bool* ok) {
        *ok = true;
        std::string step;
        for (size_t i = 0; i < raw.size();) {
            const unsigned char c = static_cast<unsigned char>(raw[i]);
            if (std::isalpha(c) || c == '_') {
                size_t j = i + 1;
                while (j < raw.size() && (std::isalnum(static_cast<unsigned char>(raw[j])) || raw[j] == '_')) ++j;
                const std::string word = raw.substr(i, j - i);
                if (word == "defined") {
                    while (j < raw.size() && std::isspace(static_cast<unsigned char>(raw[j]))) ++j;
                    bool paren = false;
                    if (j < raw.size() && raw[j] == '(') {
                        paren = true;
                        ++j;
                        while (j < raw.size() && std::isspace(static_cast<unsigned char>(raw[j]))) ++j;
                    }
                    size_t k = j;
                    while (k < raw.size() && (std::isalnum(static_cast<unsigned char>(raw[k])) || raw[k] == '_')) ++k;
                    const std::string name = raw.substr(j, k - j);
                    if (paren) {
                        while (k < raw.size() && std::isspace(static_cast<unsigned char>(raw[k]))) ++k;
                        if (k < raw.size() && raw[k] == ')') ++k;
                    }
                    step += defined(name) ? "1" : "0";
                    i = k;
                    continue;
                }
                step += word;
                i = j;
                continue;
            }
            step.push_back(raw[i++]);
        }
        const std::string expanded = expand(step, 0);
        if (!error.empty()) {
            *ok = false;
            return 0;
        }
        struct Tok {
            int kind = 0;
            int64_t num = 0;
            std::string text;
        };
        std::vector<Tok> toks;
        for (size_t i = 0; i < expanded.size();) {
            const unsigned char c = static_cast<unsigned char>(expanded[i]);
            if (std::isspace(c)) {
                ++i;
                continue;
            }
            if (std::isalpha(c) || c == '_') {
                size_t j = i + 1;
                while (j < expanded.size() && (std::isalnum(static_cast<unsigned char>(expanded[j])) || expanded[j] == '_')) ++j;
                Tok tok;
                tok.kind = 2;
                tok.text = expanded.substr(i, j - i);
                toks.push_back(tok);
                i = j;
                continue;
            }
            if (std::isdigit(c)) {
                int base = 10;
                size_t j = i;
                if (expanded[i] == '0' && j + 1 < expanded.size() && (expanded[j + 1] == 'x' || expanded[j + 1] == 'X')) {
                    base = 16;
                    j += 2;
                }
                int64_t value = 0;
                bool any = false;
                while (j < expanded.size()) {
                    const char d = expanded[j];
                    int digit = -1;
                    if (d >= '0' && d <= '9') digit = d - '0';
                    else if (base == 16 && d >= 'a' && d <= 'f') digit = d - 'a' + 10;
                    else if (base == 16 && d >= 'A' && d <= 'F') digit = d - 'A' + 10;
                    else break;
                    if (digit >= base) break;
                    any = true;
                    value = value * base + digit;
                    ++j;
                }
                if (!any) {
                    *ok = false;
                    return 0;
                }
                Tok tok;
                tok.kind = 1;
                tok.num = value;
                toks.push_back(tok);
                i = j;
                continue;
            }
            Tok tok;
            tok.kind = 3;
            tok.text.assign(1, expanded[i]);
            if (i + 1 < expanded.size()) {
                const char n = expanded[i + 1];
                const char f = expanded[i];
                if ((f == '&' && n == '&') || (f == '|' && n == '|') || (f == '=' && n == '=') || (f == '!' && n == '=') ||
                    (f == '<' && n == '=') || (f == '>' && n == '=')) {
                    tok.text.push_back(n);
                    ++i;
                }
            }
            ++i;
            toks.push_back(tok);
        }
        Tok end;
        toks.push_back(end);
        struct Ev {
            const std::vector<Tok>& t;
            size_t at = 0;
            bool* ok = nullptr;
            const Tok& cur() const { return t[at]; }
            void next() { if (at + 1 < t.size()) ++at; }
            int64_t primary() {
                if (cur().kind == 1) {
                    const int64_t value = cur().num;
                    next();
                    return value;
                }
                if (cur().kind == 2) {
                    next();
                    return 0;
                }
                if (cur().kind == 3 && cur().text == "(") {
                    next();
                    const int64_t value = expr();
                    if (cur().kind != 3 || cur().text != ")") *ok = false;
                    else next();
                    return value;
                }
                if (cur().kind == 3 && (cur().text == "!" || cur().text == "-" || cur().text == "+")) {
                    const std::string op = cur().text;
                    next();
                    const int64_t value = primary();
                    if (op == "!") return value == 0 ? 1 : 0;
                    if (op == "-") return -value;
                    return value;
                }
                *ok = false;
                return 0;
            }
            int64_t mul() {
                int64_t left = primary();
                while (cur().kind == 3 && (cur().text == "*" || cur().text == "/" || cur().text == "%")) {
                    const std::string op = cur().text;
                    next();
                    const int64_t right = primary();
                    if (op != "*" && right == 0) {
                        *ok = false;
                        return 0;
                    }
                    if (op == "*") left *= right;
                    else if (op == "/") left /= right;
                    else left %= right;
                }
                return left;
            }
            int64_t add() {
                int64_t left = mul();
                while (cur().kind == 3 && (cur().text == "+" || cur().text == "-")) {
                    const std::string op = cur().text;
                    next();
                    const int64_t right = mul();
                    if (op == "+") left += right;
                    else left -= right;
                }
                return left;
            }
            int64_t rel() {
                int64_t left = add();
                while (cur().kind == 3 && (cur().text == "<" || cur().text == ">" || cur().text == "<=" || cur().text == ">=")) {
                    const std::string op = cur().text;
                    next();
                    const int64_t right = add();
                    if (op == "<") left = left < right;
                    else if (op == ">") left = left > right;
                    else if (op == "<=") left = left <= right;
                    else left = left >= right;
                }
                return left;
            }
            int64_t eq() {
                int64_t left = rel();
                while (cur().kind == 3 && (cur().text == "==" || cur().text == "!=")) {
                    const std::string op = cur().text;
                    next();
                    const int64_t right = rel();
                    left = op == "==" ? left == right : left != right;
                }
                return left;
            }
            int64_t and_() {
                int64_t left = eq();
                while (cur().kind == 3 && cur().text == "&&") {
                    next();
                    const int64_t right = eq();
                    left = (left != 0 && right != 0) ? 1 : 0;
                }
                return left;
            }
            int64_t expr() {
                int64_t left = and_();
                while (cur().kind == 3 && cur().text == "||") {
                    next();
                    const int64_t right = and_();
                    left = (left != 0 || right != 0) ? 1 : 0;
                }
                return left;
            }
        };
        Ev ev{toks, 0, ok};
        const int64_t value = ev.expr();
        if (ev.cur().kind != 0) *ok = false;
        return value;
    }

    std::string expand(const std::string& line, int nest) {
        if (nest > 20) {
            error = "macro expansion is too deep";
            return line;
        }
        std::string out;
        bool in_string = false;
        char quote = 0;
        for (size_t i = 0; i < line.size();) {
            const char c = line[i];
            if (in_string) {
                out.push_back(c);
                if (c == '\\' && i + 1 < line.size()) out.push_back(line[++i]);
                else if (c == quote) in_string = false;
                ++i;
                continue;
            }
            if (c == '"' || c == '\'') {
                in_string = true;
                quote = c;
                out.push_back(c);
                ++i;
                continue;
            }
            if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
                size_t j = i + 1;
                while (j < line.size() && (std::isalnum(static_cast<unsigned char>(line[j])) || line[j] == '_')) ++j;
                const std::string word = line.substr(i, j - i);
                const auto it = macros.find(word);
                if (it == macros.end()) {
                    out += word;
                    i = j;
                    continue;
                }
                if (!it->second.function_like) {
                    out += expand(it->second.body, nest + 1);
                    i = j;
                    continue;
                }
                size_t k = j;
                while (k < line.size() && std::isspace(static_cast<unsigned char>(line[k]))) ++k;
                if (k >= line.size() || line[k] != '(') {
                    out += word;
                    i = j;
                    continue;
                }
                ++k;
                std::vector<std::string> args;
                std::string cur_arg;
                int depth_paren = 0;
                bool in_arg_string = false;
                char arg_quote = 0;
                for (; k < line.size(); ++k) {
                    const char ch = line[k];
                    if (in_arg_string) {
                        cur_arg.push_back(ch);
                        if (ch == '\\' && k + 1 < line.size()) cur_arg.push_back(line[++k]);
                        else if (ch == arg_quote) in_arg_string = false;
                        continue;
                    }
                    if (ch == '"' || ch == '\'') {
                        in_arg_string = true;
                        arg_quote = ch;
                        cur_arg.push_back(ch);
                        continue;
                    }
                    if (ch == '(') {
                        ++depth_paren;
                        cur_arg.push_back(ch);
                        continue;
                    }
                    if (ch == ')' && depth_paren == 0) {
                        args.push_back(cur_arg);
                        ++k;
                        break;
                    }
                    if (ch == ')' ) {
                        --depth_paren;
                        cur_arg.push_back(ch);
                        continue;
                    }
                    if (ch == ',' && depth_paren == 0) {
                        args.push_back(cur_arg);
                        cur_arg.clear();
                        continue;
                    }
                    cur_arg.push_back(ch);
                }
                std::string substituted = it->second.body;
                for (size_t p = 0; p < it->second.params.size() && p < args.size(); ++p) {
                    const std::string& param = it->second.params[p];
                    std::string next;
                    for (size_t s = 0; s < substituted.size();) {
                        if (std::isalpha(static_cast<unsigned char>(substituted[s])) || substituted[s] == '_') {
                            size_t e = s + 1;
                            while (e < substituted.size() &&
                                   (std::isalnum(static_cast<unsigned char>(substituted[e])) || substituted[e] == '_'))
                                ++e;
                            if (substituted.substr(s, e - s) == param) next += args[p];
                            else next += substituted.substr(s, e - s);
                            s = e;
                        } else next.push_back(substituted[s++]);
                    }
                    substituted = next;
                }
                out += expand(substituted, nest + 1);
                i = k;
                continue;
            }
            out.push_back(c);
            ++i;
        }
        return out;
    }

    std::string find_include(const std::string& name, bool quote, const std::string& dir) {
        if (quote && !dir.empty()) {
            const std::string path = dir + "/" + name;
            std::string text;
            if (read_file(path, &text)) return text;
        }
        for (const std::string& inc : includes) {
            const std::string path = inc + "/" + name;
            std::string text;
            if (read_file(path, &text)) return text;
        }
        return "";
    }

    std::string run(const std::string& text, const std::string& dir) {
        if (depth > 16) {
            error = "include nesting is too deep";
            return "";
        }
        ++depth;
        struct Frame {
            bool parent;
            bool taken;
            bool any;
            bool saw_else;
        };
        std::vector<Frame> stack;
        auto active = [&]() {
            for (const Frame& frame : stack) {
                if (!frame.parent || !frame.taken) return false;
            }
            return true;
        };
        std::string out;
        size_t i = 0;
        while (i < text.size()) {
            size_t end = text.find('\n', i);
            if (end == std::string::npos) end = text.size();
            std::string line = text.substr(i, end - i);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            i = end < text.size() ? end + 1 : end;
            size_t s = 0;
            while (s < line.size() && (line[s] == ' ' || line[s] == '\t')) ++s;
            if (s < line.size() && line[s] == '#') {
                std::string rest = line.substr(s + 1);
                size_t a = 0;
                while (a < rest.size() && (rest[a] == ' ' || rest[a] == '\t')) ++a;
                rest = rest.substr(a);
                auto word = [&](const char* w) { return rest.compare(0, std::strlen(w), w) == 0; };
                if (word("include")) {
                    if (!active()) continue;
                    const size_t q1 = rest.find_first_of("\"<");
                    const size_t q2 = rest.find_last_of("\">");
                    if (q1 == std::string::npos || q2 <= q1) {
                        error = "bad include";
                        --depth;
                        return "";
                    }
                    const bool quote = rest[q1] == '"';
                    const std::string name = rest.substr(q1 + 1, q2 - q1 - 1);
                    const std::string body = find_include(name, quote, dir);
                    if (body.empty()) {
                        error = "cannot open include " + name;
                        --depth;
                        return "";
                    }
                    out += run(strip_comments(body), dir);
                    if (!error.empty()) {
                        --depth;
                        return "";
                    }
                } else if (word("define")) {
                    if (!active()) continue;
                    std::string body = rest.substr(6);
                    size_t b = 0;
                    while (b < body.size() && (body[b] == ' ' || body[b] == '\t')) ++b;
                    body = body.substr(b);
                    size_t n = 0;
                    while (n < body.size() && (std::isalnum(static_cast<unsigned char>(body[n])) || body[n] == '_')) ++n;
                    if (n == 0) {
                        error = "bad define";
                        --depth;
                        return "";
                    }
                    Macro macro;
                    std::string rest = n < body.size() ? body.substr(n) : "";
                    if (!rest.empty() && rest[0] == '(') {
                        macro.function_like = true;
                        size_t p = 1;
                        while (p < rest.size() && rest[p] != ')') {
                            while (p < rest.size() && (rest[p] == ' ' || rest[p] == '\t' || rest[p] == ',')) ++p;
                            size_t e = p;
                            while (e < rest.size() && (std::isalnum(static_cast<unsigned char>(rest[e])) || rest[e] == '_')) ++e;
                            if (e > p) macro.params.push_back(rest.substr(p, e - p));
                            p = e;
                        }
                        if (p < rest.size() && rest[p] == ')') ++p;
                        while (p < rest.size() && (rest[p] == ' ' || rest[p] == '\t')) ++p;
                        macro.body = rest.substr(p);
                    } else {
                        size_t v = 0;
                        while (v < rest.size() && (rest[v] == ' ' || rest[v] == '\t')) ++v;
                        macro.body = rest.substr(v);
                    }
                    macros[body.substr(0, n)] = macro;
                } else if (word("ifdef") || word("ifndef")) {
                    const bool neg = word("ifndef");
                    std::string name = rest.substr(neg ? 6 : 5);
                    size_t b = 0;
                    while (b < name.size() && (name[b] == ' ' || name[b] == '\t')) ++b;
                    name = name.substr(b);
                    size_t n = 0;
                    while (n < name.size() && (std::isalnum(static_cast<unsigned char>(name[n])) || name[n] == '_')) ++n;
                    name = name.substr(0, n);
                    const bool parent = active();
                    bool taken = parent && defined(name);
                    if (neg) taken = parent && !defined(name);
                    stack.push_back({parent, taken, taken, false});
                } else if (word("elif") || (rest.compare(0, 2, "if") == 0 &&
                                            (rest.size() == 2 || rest[2] == ' ' || rest[2] == '\t' || rest[2] == '('))) {
                    const bool is_elif = word("elif");
                    std::string expr = rest.substr(is_elif ? 4 : 2);
                    size_t b = 0;
                    while (b < expr.size() && (expr[b] == ' ' || expr[b] == '\t')) ++b;
                    expr = expr.substr(b);
                    bool good = true;
                    const int64_t value = eval_pp(expr, &good);
                    if (!good) {
                        error = error.empty() ? "bad preprocessor expression" : error;
                        --depth;
                        return "";
                    }
                    if (is_elif) {
                        if (stack.empty() || stack.back().saw_else) {
                            error = "elif without if";
                            --depth;
                            return "";
                        }
                        const bool taken = stack.back().parent && !stack.back().any && value != 0;
                        stack.back().taken = taken;
                        if (taken) stack.back().any = true;
                    } else {
                        const bool parent = active();
                        const bool taken = parent && value != 0;
                        stack.push_back({parent, taken, taken, false});
                    }
                } else if (word("else")) {
                    if (stack.empty() || stack.back().saw_else) {
                        error = "else without if";
                        --depth;
                        return "";
                    }
                    stack.back().saw_else = true;
                    stack.back().taken = stack.back().parent && !stack.back().any;
                    stack.back().any = true;
                } else if (word("endif")) {
                    if (stack.empty()) {
                        error = "endif without if";
                        --depth;
                        return "";
                    }
                    stack.pop_back();
                } else if (active()) {
                    error = "unknown preprocessor directive";
                    --depth;
                    return "";
                }
                continue;
            }
            if (active()) out += expand(line, 0) + "\n";
        }
        --depth;
        return out;
    }
};

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
    if (text == "struct") return TStruct;
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
    return TIdent;
}

// value = sig * 10^pow10, written as an IEEE-754 double. A float is that
// value rounded to 24 significant bits.
uint64_t ieee_real(uint64_t sig, int pow10, bool neg, bool single) {
    unsigned __int128 mag = sig;
    int bexp = 0;
    if (sig == 0) return neg ? (single ? 0x80000000ull : 0x8000000000000000ull) : 0;
    if (pow10 >= 0) {
        for (int i = 0; i < pow10; ++i) mag *= 10u;
        bexp = 0;
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
    const uint64_t mant_mask = single ? 0x7FFFFFull : 0xFFFFFFFFFFFFFull;
    const uint64_t mant = static_cast<uint64_t>(mag) & mant_mask;
    if (single) return (neg ? 0x80000000ull : 0) | (static_cast<uint64_t>(encoded) << 23) | mant;
    return (neg ? 0x8000000000000000ull : 0) | (static_cast<uint64_t>(encoded) << 52) | mant;
}

std::vector<Token> lex(const std::string& text, std::string* error) {
    std::vector<Token> tokens;
    for (size_t i = 0; i < text.size();) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (std::isspace(c)) {
            ++i;
            continue;
        }
        Token tok;
        if (std::isalpha(c) || c == '_') {
            size_t j = i + 1;
            while (j < text.size() && (std::isalnum(static_cast<unsigned char>(text[j])) || text[j] == '_')) ++j;
            tok.text = text.substr(i, j - i);
            tok.kind = keyword(tok.text);
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
            }
            uint64_t value = 0;
            bool any = false;
            while (j < text.size()) {
                const char d = text[j];
                int digit = -1;
                if (d >= '0' && d <= '9') digit = d - '0';
                else if (base == 16 && d >= 'a' && d <= 'f') digit = d - 'a' + 10;
                else if (base == 16 && d >= 'A' && d <= 'F') digit = d - 'A' + 10;
                else break;
                any = true;
                value = value * static_cast<uint64_t>(base) + static_cast<uint64_t>(digit);
                ++j;
            }
            if (j < text.size() && (text[j] == 'u' || text[j] == 'U' || text[j] == 'l' || text[j] == 'L')) ++j;
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
            if (!real && j < text.size() && (text[j] == 'u' || text[j] == 'U' || text[j] == 'l' || text[j] == 'L')) ++j;
            if (!any && !real) {
                *error = "bad number";
                return {};
            }
            tok.kind = TNumber;
            if (real) {
                uint64_t sig = value;
                for (int n = 0; n < frac_digits; ++n) sig *= 10u;
                sig += frac;
                tok.number = ieee_real(sig, exp10 - frac_digits, false, as_float);
                tok.real = as_float ? 5 : 6;
            } else {
                tok.number = value;
            }
            i = j;
            tokens.push_back(tok);
            continue;
        }
        if (c == '\'') {
            ++i;
            if (i >= text.size()) {
                *error = "unterminated character";
                return {};
            }
            unsigned char value = 0;
            if (text[i] == '\\' && i + 1 < text.size()) {
                ++i;
                if (text[i] == 'n') value = '\n';
                else if (text[i] == 't') value = '\t';
                else if (text[i] == 'r') value = '\r';
                else if (text[i] == '0') value = 0;
                else value = static_cast<unsigned char>(text[i]);
                ++i;
            } else {
                value = static_cast<unsigned char>(text[i]);
                ++i;
            }
            if (i >= text.size() || text[i] != '\'') {
                *error = "unterminated character";
                return {};
            }
            ++i;
            tok.kind = TNumber;
            tok.number = value;
            tokens.push_back(tok);
            continue;
        }
        if (c == '"') {
            std::string value;
            ++i;
            while (i < text.size() && text[i] != '"') {
                if (text[i] == '\\' && i + 1 < text.size()) {
                    ++i;
                    if (text[i] == 'n') value.push_back('\n');
                    else if (text[i] == 't') value.push_back('\t');
                    else if (text[i] == 'r') value.push_back('\r');
                    else if (text[i] == '0') value.push_back('\0');
                    else value.push_back(text[i]);
                } else value.push_back(text[i]);
                ++i;
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
        else if (c == '[') tok.kind = TLBrack;
        else if (c == ']') tok.kind = TRBrack;
        else if (c == '.') tok.kind = TDot;
        else if (c == ':') tok.kind = TColon;
        else {
            *error = "unexpected character";
            return {};
        }
        ++i;
        tokens.push_back(tok);
    }
    tokens.push_back(Token{});
    return tokens;
}

struct Parser {
    const std::vector<Token>& tokens;
    size_t at = 0;
    std::string error;
    std::vector<FuncDecl> funcs;
    std::vector<std::string> strings;
    std::vector<StructDef> structs;
    std::vector<Global> globals;
    std::vector<FuncType> func_types;
    std::map<std::string, Type> typedefs;
    std::map<std::string, uint64_t> enum_values;
    std::vector<Local>* cur_locals = nullptr;
    std::string current_fn;
    int scope_depth = 0;
    std::vector<int> scope_parent{0};

    void push_scope() {
        const int id = static_cast<int>(scope_parent.size());
        scope_parent.push_back(scope_depth);
        scope_depth = id;
    }
    void pop_scope() { scope_depth = scope_parent[static_cast<size_t>(scope_depth)]; }
    bool scope_visible(int local_scope) const {
        int walk = scope_depth;
        while (walk != local_scope) {
            if (walk == 0) return false;
            walk = scope_parent[static_cast<size_t>(walk)];
        }
        return true;
    }

    const Token& cur() const { return tokens[at]; }
    bool is(Tok k) const { return cur().kind == k; }
    void next() { if (at + 1 < tokens.size()) ++at; }
    bool accept(Tok k) {
        if (!is(k)) return false;
        next();
        return true;
    }
    bool expect(Tok k, const char* what) {
        if (accept(k)) return true;
        if (error.empty()) error = std::string("expected ") + what;
        return false;
    }

    int find_struct(const std::string& name) const {
        for (size_t i = 0; i < structs.size(); ++i)
            if (structs[i].name == name) return static_cast<int>(i);
        return -1;
    }
    int size_of(const Type& type) const {
        if (type.array_len >= 0) {
            Type elem = type;
            elem.array_len = -1;
            return type.array_len * size_of(elem);
        }
        if (type.pointers > 0) return 8;
        if (type.base == 2) return 1;
        if (type.base == 4) return 2;
        if (type.base == 5) return 4;
        if (type.base == 6) return 8;
        if (type.base == 3 && type.struct_id >= 0 && type.struct_id < static_cast<int>(structs.size()))
            return structs[type.struct_id].size;
        if (type.base == 0) return 0;
        return 4;
    }
    int align_of(const Type& type) const {
        if (type.array_len >= 0) {
            Type elem = type;
            elem.array_len = -1;
            return align_of(elem);
        }
        if (type.pointers > 0) return 8;
        if (type.base == 2) return 1;
        if (type.base == 4) return 2;
        if (type.base == 5) return 4;
        if (type.base == 6) return 8;
        if (type.base == 3 && type.struct_id >= 0 && type.struct_id < static_cast<int>(structs.size()))
            return std::max(1, structs[type.struct_id].align);
        return 4;
    }
    bool fits_register(const Type& type) const {
        if (type.array_len >= 0 || type.pointers > 0) return true;
        const int size = size_of(type);
        return size == 1 || size == 2 || size == 4 || size == 8;
    }
    void place_local(std::vector<Local>* locals, Local local) {
        const int used = locals->empty() ? 0 : -locals->back().offset;
        int align = align_of(local.type);
        int size = size_of(local.type);
        if (align < 1) align = 1;
        if (size < 1) size = 1;
        const int aligned = (used + align - 1) & ~(align - 1);
        local.offset = -(aligned + size);
        local.scope = scope_depth;
        locals->push_back(local);
    }

    bool parse_type(Type* type, bool* imported, bool* exported) {
        *imported = false;
        *exported = false;
        while (is(TDeclspec)) {
            next();
            if (!expect(TLParen, "'('")) return false;
            if (!is(TIdent)) {
                error = "expected dllimport or dllexport";
                return false;
            }
            if (cur().text == "dllimport") *imported = true;
            else if (cur().text == "dllexport") *exported = true;
            else {
                error = "unknown declspec " + cur().text;
                return false;
            }
            next();
            if (!expect(TRParen, "')'")) return false;
        }
        bool saw = false;
        type->base = 1;
        type->pointers = 0;
        type->array_len = -1;
        type->struct_id = -1;
        type->func_id = -1;
        type->bits = -1;
        type->bit_off = 0;
        type->is_unsigned = false;
        type->is_const = false;
        if (is(TStruct) || is(TUnion)) {
            const bool as_union = is(TUnion);
            next();
            std::string name;
            if (is(TIdent)) {
                name = cur().text;
                next();
            }
            if (is(TLBrace)) {
                int id = -1;
                if (!parse_struct_body(as_union, name, &id)) return false;
                type->base = 3;
                type->struct_id = id;
                return true;
            }
            if (name.empty()) {
                error = "expected a struct name";
                return false;
            }
            const int id = find_struct(name);
            if (id < 0) {
                error = "unknown struct " + name;
                return false;
            }
            type->base = 3;
            type->struct_id = id;
            return true;
        }
        if (is(TEnum)) {
            next();
            if (is(TIdent) && tokens[at + 1].kind != TLBrace) {
                next();
                type->base = 1;
                return true;
            }
            if (is(TIdent)) next();
            if (!parse_enum_body()) return false;
            type->base = 1;
            return true;
        }
        if (is(TIdent) && typedefs.find(cur().text) != typedefs.end()) {
            *type = typedefs[cur().text];
            next();
            return true;
        }
        bool unsign = false;
        bool sign = false;
        while (is(TUnsigned) || is(TSigned) || is(TLong) || is(TShort) || is(TInt) || is(TVoid) || is(TChar) ||
               is(TFloat) || is(TDouble)) {
            if (is(TVoid)) type->base = 0;
            else if (is(TChar)) type->base = 2;
            else if (is(TShort)) type->base = 4;
            else if (is(TFloat)) type->base = 5;
            else if (is(TDouble)) type->base = 6;
            else if (is(TInt) || is(TLong)) {
                if (type->base != 2 && type->base != 4) type->base = 1;
            }
            if (is(TUnsigned)) unsign = true;
            if (is(TSigned)) sign = true;
            saw = true;
            next();
        }
        type->is_unsigned = unsign && !sign;
        if (!saw) {
            error = "expected a type";
            return false;
        }
        return true;
    }

    bool parse_enum_body() {
        if (!expect(TLBrace, "'{'")) return false;
        uint64_t next_value = 0;
        if (is(TRBrace)) return expect(TRBrace, "'}'");
        while (!is(TEnd)) {
            if (!is(TIdent)) {
                error = "expected an enumerator";
                return false;
            }
            const std::string name = cur().text;
            next();
            if (accept(TAssign)) {
                if (!is(TNumber)) {
                    error = "expected an enumerator value";
                    return false;
                }
                next_value = cur().number;
                next();
            }
            enum_values[name] = next_value;
            ++next_value;
            if (!accept(TComma)) break;
            if (is(TRBrace)) break;
        }
        return expect(TRBrace, "'}'");
    }

    struct Declarator {
        std::string name;
        Type type{};
        bool is_function = false;
        bool vararg = false;
        std::vector<Type> params;
        std::vector<std::string> param_names;
    };

    bool parse_param_list(Declarator* out) {
        if (is(TVoid) && at + 1 < tokens.size() && tokens[at + 1].kind == TRParen) {
            next();
            next();
            return true;
        }
        if (is(TRParen)) {
            next();
            return true;
        }
        do {
            if (accept(TEllipsis)) {
                out->vararg = true;
                break;
            }
            Type pt;
            bool imp = false;
            bool exp = false;
            if (!parse_type(&pt, &imp, &exp)) return false;
            while (accept(TStar)) ++pt.pointers;
            std::string pname;
            if (is(TIdent)) {
                pname = cur().text;
                next();
            }
            if (accept(TLBrack)) {
                if (!is(TNumber)) {
                    error = "expected an array length";
                    return false;
                }
                pt.array_len = static_cast<int>(cur().number);
                next();
                if (!expect(TRBrack, "']'")) return false;
            }
            out->params.push_back(pt);
            out->param_names.push_back(pname);
        } while (accept(TComma));
        return expect(TRParen, "')'");
    }

    bool parse_declarator(Type base, Declarator* out) {
        out->type = base;
        if (is(TLParen) && at + 1 < tokens.size() && tokens[at + 1].kind == TStar) {
            next();
            int stars = 0;
            while (accept(TStar)) ++stars;
            if (!is(TIdent)) {
                error = "expected a name";
                return false;
            }
            out->name = cur().text;
            next();
            if (accept(TLBrack)) {
                if (!is(TNumber)) {
                    error = "expected an array length";
                    return false;
                }
                out->type.array_len = static_cast<int>(cur().number);
                next();
                if (!expect(TRBrack, "']'")) return false;
            }
            if (!expect(TRParen, "')'")) return false;
            if (!expect(TLParen, "'('")) return false;
            if (!parse_param_list(out)) return false;
            FuncType ft;
            ft.ret = base;
            ft.params = out->params;
            ft.vararg = out->vararg;
            out->type.pointers += stars;
            out->type.func_id = static_cast<int>(func_types.size());
            func_types.push_back(ft);
            out->params.clear();
            out->param_names.clear();
            out->vararg = false;
            return true;
        }
        while (accept(TStar)) ++out->type.pointers;
        if (!is(TIdent)) {
            error = "expected a name";
            return false;
        }
        out->name = cur().text;
        next();
        if (accept(TLBrack)) {
            if (!is(TNumber)) {
                error = "expected an array length";
                return false;
            }
            out->type.array_len = static_cast<int>(cur().number);
            next();
            if (!expect(TRBrack, "']'")) return false;
        }
        if (!is(TLParen)) return true;
        next();
        out->is_function = true;
        return parse_param_list(out);
    }

    bool is_decl_start() const {
        if (is(TInt) || is(TVoid) || is(TChar) || is(TUnsigned) || is(TSigned) || is(TLong) || is(TShort) ||
            is(TStruct) || is(TUnion) || is(TEnum) || is(TTypedef) || is(TDeclspec) || is(TStatic) || is(TConst) ||
            is(TFloat) || is(TDouble))
            return true;
        return is(TIdent) && typedefs.find(cur().text) != typedefs.end();
    }

    bool parse_expr(int min_prec, Node* out);
    bool parse_unary(Node* out);
    bool parse_stmt(Node* out, std::vector<Local>* locals);
    bool parse_block(Node* out, std::vector<Local>* locals);

    int prec(Tok op) const {
        if (op == TComma) return 0;
        if (op == TAssign || op == TPlusEq || op == TMinusEq || op == TStarEq || op == TSlashEq || op == TPercentEq)
            return 1;
        if (op == TQuestion) return 2;
        if (op == TOrOr) return 3;
        if (op == TAndAnd) return 4;
        if (op == TEq || op == TNe) return 5;
        if (op == TLt || op == TGt || op == TLe || op == TGe) return 6;
        if (op == TPlus || op == TMinus) return 7;
        if (op == TStar || op == TSlash || op == TPercent) return 8;
        return -1;
    }

    bool parse_struct_body(bool as_union, std::string name, int* out_id) {
        if (name.empty()) name = "__anon" + std::to_string(structs.size());
        StructDef def;
        def.name = name;
        def.is_union = as_union;
        structs.push_back(def);
        const int self = static_cast<int>(structs.size()) - 1;
        if (!expect(TLBrace, "'{'")) return false;
        while (!is(TRBrace) && !is(TEnd)) {
            Field field;
            bool imp = false;
            bool exp = false;
            if (!parse_type(&field.type, &imp, &exp)) return false;
            while (accept(TStar)) ++field.type.pointers;
            if (!is(TIdent)) {
                error = "expected a field name";
                return false;
            }
            field.name = cur().text;
            next();
            if (accept(TLBrack)) {
                if (!is(TNumber)) {
                    error = "expected an array length";
                    return false;
                }
                field.type.array_len = static_cast<int>(cur().number);
                next();
                if (!expect(TRBrack, "']'")) return false;
            }
            if (accept(TColon)) {
                if (!is(TNumber)) {
                    error = "expected a bitfield width";
                    return false;
                }
                field.type.bits = static_cast<int>(cur().number);
                next();
            }
            if (!expect(TSemi, "';'")) return false;
            if (field.type.base == 3 && field.type.pointers == 0 && field.type.array_len < 0 && field.type.struct_id == self) {
                error = "struct field is incomplete";
                return false;
            }
            def.fields.push_back(field);
        }
        if (!expect(TRBrace, "'}'")) return false;
        int offset = 0;
        int align = 1;
        int cursor_bits = 0;
        for (Field& field : def.fields) {
            int field_align = align_of(field.type);
            int field_size = size_of(field.type);
            if (field_align < 1) field_align = 1;
            if (field_size < 1) field_size = 1;
            if (field.type.bits >= 0 && !as_union) {
                const int container = field_size * 8;
                if (field.type.bits > container) {
                    error = "bitfield is wider than its type";
                    return false;
                }
                if (container > 0 && (cursor_bits % container) + field.type.bits > container) {
                    cursor_bits = ((cursor_bits + container - 1) / container) * container;
                }
                field.offset = (cursor_bits / 8) & ~(field_size - 1);
                field.type.bit_off = cursor_bits % container;
                cursor_bits += field.type.bits;
                const int end_byte = (cursor_bits + 7) / 8;
                if (end_byte > offset) offset = end_byte;
                if (field_align > align) align = field_align;
                continue;
            }
            if (as_union) {
                field.offset = 0;
                if (field_size > offset) offset = field_size;
            } else {
                if (cursor_bits > offset * 8) offset = (cursor_bits + 7) / 8;
                offset = (offset + field_align - 1) & ~(field_align - 1);
                field.offset = offset;
                offset += field_size;
                cursor_bits = offset * 8;
            }
            if (field_align > align) align = field_align;
        }
        if (cursor_bits > offset * 8) offset = (cursor_bits + 7) / 8;
        def.align = align;
        def.size = (offset + align - 1) & ~(align - 1);
        structs[static_cast<size_t>(self)] = def;
        *out_id = self;
        return true;
    }

    void finish_signature(FuncDecl* fn) {
        FuncType ft;
        ft.ret = fn->ret;
        ft.params = fn->params;
        ft.vararg = fn->vararg;
        fn->func_id = static_cast<int>(func_types.size());
        func_types.push_back(ft);
        fn->sret = fn->ret.base == 3 && fn->ret.pointers == 0 && !fits_register(fn->ret);
    }

    bool parse() {
        while (!is(TEnd)) {
            bool is_typedef = false;
            bool is_static = false;
            bool is_const = false;
            while (is(TTypedef) || is(TStatic) || is(TConst)) {
                if (accept(TTypedef)) is_typedef = true;
                else if (accept(TStatic)) is_static = true;
                else if (accept(TConst)) is_const = true;
            }
            bool imported = false;
            bool exported = false;
            Type type;
            if (!parse_type(&type, &imported, &exported)) return false;
            if (!is_typedef && is(TSemi)) {
                next();
                continue;
            }
            Declarator decl;
            if (!parse_declarator(type, &decl)) return false;
            if (is_typedef) {
                decl.type.is_const = is_const;
                typedefs[decl.name] = decl.type;
                if (!expect(TSemi, "';'")) return false;
                continue;
            }
            decl.type.is_const = is_const;
            if (!decl.is_function) {
                Global global;
                global.name = decl.name;
                global.type = decl.type;
                global.is_static = is_static;
                if (accept(TAssign)) {
                    if (!is(TNumber)) {
                        error = "global initializer must be a number";
                        return false;
                    }
                    global.bss = false;
                    global.init = static_cast<uint32_t>(cur().number);
                    next();
                }
                if (!expect(TSemi, "';'")) return false;
                globals.push_back(global);
                continue;
            }
            FuncDecl fn;
            fn.name = decl.name;
            fn.ret = decl.type;
            fn.params = decl.params;
            fn.param_names = decl.param_names;
            fn.vararg = decl.vararg;
            fn.imported = imported;
            fn.exported = exported;
            fn.is_static = is_static;
            finish_signature(&fn);
            if (accept(TSemi)) {
                fn.defined = false;
                funcs.push_back(std::move(fn));
                continue;
            }
            scope_depth = 0;
            scope_parent.clear();
            scope_parent.push_back(0);
            std::vector<Local> locals;
            if (fn.sret) {
                Local hidden;
                hidden.name = "__sret";
                hidden.type.pointers = 1;
                hidden.type.base = 3;
                hidden.type.struct_id = fn.ret.struct_id;
                place_local(&locals, hidden);
                fn.sret_off = locals.back().offset;
            }
            fn.param_base = static_cast<int>(locals.size());
            for (size_t p = 0; p < fn.param_names.size(); ++p) {
                Local local;
                local.name = fn.param_names[p];
                local.type = fn.params[p];
                place_local(&locals, local);
            }
            fn.nparams = static_cast<int>(fn.params.size());
            current_fn = fn.name;
            cur_locals = &locals;
            if (!parse_block(&fn.body, &locals)) return false;
            cur_locals = nullptr;
            fn.defined = true;
            fn.locals = std::move(locals);
            funcs.push_back(std::move(fn));
        }
        return error.empty();
    }
};

bool Parser::parse_block(Node* out, std::vector<Local>* locals) {
    if (!expect(TLBrace, "'{'")) return false;
    out->kind = Node::Block;
    out->text = "scope";
    push_scope();
    while (!is(TRBrace) && !is(TEnd)) {
        Node stmt;
        if (!parse_stmt(&stmt, locals)) return false;
        out->kids.push_back(std::move(stmt));
    }
    pop_scope();
    return expect(TRBrace, "'}'");
}

bool Parser::parse_stmt(Node* out, std::vector<Local>* locals) {
    cur_locals = locals;
    if (is(TLBrace)) return parse_block(out, locals);
    if (is(TGoto)) {
        next();
        if (!is(TIdent)) {
            error = "expected a label";
            return false;
        }
        out->kind = Node::Goto;
        out->text = cur().text;
        next();
        return expect(TSemi, "';'");
    }
    if (is(TIdent) && at + 1 < tokens.size() && tokens[at + 1].kind == TColon &&
        typedefs.find(cur().text) == typedefs.end()) {
        out->kind = Node::Label;
        out->text = cur().text;
        next();
        next();
        Node stmt;
        if (!parse_stmt(&stmt, locals)) return false;
        out->kids.push_back(std::move(stmt));
        return true;
    }
    if (is(TIf)) {
        next();
        out->kind = Node::If;
        if (!expect(TLParen, "'('")) return false;
        Node cond;
        if (!parse_expr(0, &cond)) return false;
        if (!expect(TRParen, "')'")) return false;
        Node then_s;
        if (!parse_stmt(&then_s, locals)) return false;
        out->kids.push_back(std::move(cond));
        out->kids.push_back(std::move(then_s));
        if (accept(TElse)) {
            Node else_s;
            if (!parse_stmt(&else_s, locals)) return false;
            out->kids.push_back(std::move(else_s));
        }
        return true;
    }
    if (is(TWhile)) {
        next();
        out->kind = Node::While;
        if (!expect(TLParen, "'('")) return false;
        Node cond;
        if (!parse_expr(0, &cond)) return false;
        if (!expect(TRParen, "')'")) return false;
        Node body;
        if (!parse_stmt(&body, locals)) return false;
        out->kids.push_back(std::move(cond));
        out->kids.push_back(std::move(body));
        return true;
    }
    if (is(TFor)) {
        next();
        out->kind = Node::For;
        if (!expect(TLParen, "'('")) return false;
        Node init;
        init.kind = Node::Block;
        init.text = "skip";
        if (!is(TSemi)) {
            if (!parse_expr(0, &init)) return false;
        }
        if (!expect(TSemi, "';'")) return false;
        Node cond;
        cond.kind = Node::Block;
        cond.text = "skip";
        if (!is(TSemi)) {
            if (!parse_expr(0, &cond)) return false;
        }
        if (!expect(TSemi, "';'")) return false;
        Node step;
        step.kind = Node::Block;
        step.text = "skip";
        if (!is(TRParen)) {
            if (!parse_expr(0, &step)) return false;
        }
        if (!expect(TRParen, "')'")) return false;
        Node body;
        if (!parse_stmt(&body, locals)) return false;
        out->kids.push_back(std::move(init));
        out->kids.push_back(std::move(cond));
        out->kids.push_back(std::move(step));
        out->kids.push_back(std::move(body));
        return true;
    }
    if (is(TDo)) {
        next();
        out->kind = Node::Do;
        Node body;
        if (!parse_stmt(&body, locals)) return false;
        if (!is(TWhile)) {
            error = "expected while";
            return false;
        }
        next();
        if (!expect(TLParen, "'('")) return false;
        Node cond;
        if (!parse_expr(0, &cond)) return false;
        if (!expect(TRParen, "')'")) return false;
        if (!expect(TSemi, "';'")) return false;
        out->kids.push_back(std::move(body));
        out->kids.push_back(std::move(cond));
        return true;
    }
    if (is(TSwitch)) {
        next();
        out->kind = Node::Switch;
        if (!expect(TLParen, "'('")) return false;
        Node cond;
        if (!parse_expr(0, &cond)) return false;
        if (!expect(TRParen, "')'")) return false;
        if (!expect(TLBrace, "'{'")) return false;
        out->kids.push_back(std::move(cond));
        while (!is(TRBrace) && !is(TEnd)) {
            Node arm;
            arm.kind = Node::Case;
            if (is(TCase)) {
                next();
                if (!is(TNumber)) {
                    error = "expected a case value";
                    return false;
                }
                arm.number = cur().number;
                next();
                if (!expect(TColon, "':'")) return false;
            } else if (is(TDefault)) {
                next();
                arm.op = TDefault;
                if (!expect(TColon, "':'")) return false;
            } else {
                error = "expected case or default";
                return false;
            }
            while (!is(TCase) && !is(TDefault) && !is(TRBrace) && !is(TEnd)) {
                Node stmt;
                if (!parse_stmt(&stmt, locals)) return false;
                arm.kids.push_back(std::move(stmt));
            }
            out->kids.push_back(std::move(arm));
        }
        return expect(TRBrace, "'}'");
    }
    if (is(TBreak)) {
        next();
        out->kind = Node::Break;
        return expect(TSemi, "';'");
    }
    if (is(TReturn)) {
        next();
        out->kind = Node::Return;
        if (!accept(TSemi)) {
            Node value;
            if (!parse_expr(0, &value)) return false;
            out->kids.push_back(std::move(value));
            return expect(TSemi, "';'");
        }
        return true;
    }
    if (is_decl_start()) {
        bool is_typedef = false;
        bool is_static = false;
        bool is_const = false;
        while (is(TTypedef) || is(TStatic) || is(TConst)) {
            if (accept(TTypedef)) is_typedef = true;
            else if (accept(TStatic)) is_static = true;
            else if (accept(TConst)) is_const = true;
        }
        Type type;
        bool imp = false;
        bool exp = false;
        if (!parse_type(&type, &imp, &exp)) return false;
        if (!is_typedef && is(TSemi)) {
            next();
            out->kind = Node::Block;
            return true;
        }
        Declarator decl;
        if (!parse_declarator(type, &decl)) return false;
        if (is_typedef) {
            decl.type.is_const = is_const;
            typedefs[decl.name] = decl.type;
            return expect(TSemi, "';'");
        }
        if (decl.is_function) {
            error = "a function cannot be nested";
            return false;
        }
        decl.type.is_const = is_const;
        Local local;
        local.name = decl.name;
        local.type = decl.type;
        local.is_const = is_const;
        local.is_static = is_static;
        Node init;
        bool has_init = false;
        if (accept(TAssign)) {
            if (!parse_expr(1, &init)) return false;
            has_init = true;
        }
        if (is_static) {
            Global global;
            global.name = current_fn + "_" + decl.name + "_" + std::to_string(globals.size());
            global.type = decl.type;
            global.is_static = true;
            global.bss = true;
            if (has_init && init.kind == Node::IntLit) {
                global.bss = false;
                global.init = static_cast<uint32_t>(init.number);
                has_init = false;
            }
            local.symbol = global.name;
            globals.push_back(global);
        }
        place_local(locals, local);
        out->kind = Node::Block;
        if (has_init) {
            Node assign;
            assign.kind = Node::Assign;
            assign.number = 1;
            Node name;
            name.kind = Node::Name;
            name.text = decl.name;
            assign.kids.push_back(std::move(name));
            assign.kids.push_back(std::move(init));
            out->kids.push_back(std::move(assign));
        }
        return expect(TSemi, "';'");
    }
    out->kind = Node::ExprStmt;
    Node expr;
    if (!parse_expr(0, &expr)) return false;
    out->kids.push_back(std::move(expr));
    return expect(TSemi, "';'");
}

bool Parser::parse_unary(Node* out) {
    if (is(TSizeof)) {
        next();
        if (!expect(TLParen, "'('")) return false;
        int bytes = 0;
        if (is_decl_start() && !is(TStatic)) {
            Type ty;
            bool imp = false;
            bool exp = false;
            if (!parse_type(&ty, &imp, &exp)) return false;
            while (accept(TStar)) ++ty.pointers;
            if (accept(TLBrack)) {
                if (!is(TNumber)) {
                    error = "expected an array length";
                    return false;
                }
                ty.array_len = static_cast<int>(cur().number);
                next();
                if (!expect(TRBrack, "']'")) return false;
            }
            bytes = size_of(ty);
        } else {
            Node inner;
            if (!parse_expr(1, &inner)) return false;
            if (inner.kind == Node::Name) {
                if (cur_locals != nullptr) {
                    for (int i = static_cast<int>(cur_locals->size()) - 1; i >= 0; --i) {
                        const Local& local = (*cur_locals)[static_cast<size_t>(i)];
                        if (local.name == inner.text && scope_visible(local.scope)) {
                            bytes = size_of(local.type);
                            break;
                        }
                    }
                }
                if (bytes == 0) {
                    for (const Global& global : globals) {
                        if (global.name == inner.text) bytes = size_of(global.type);
                    }
                }
            } else if (inner.kind == Node::StringLit) {
                bytes = static_cast<int>(inner.text.size()) + 1;
            } else if (inner.kind == Node::IntLit) {
                bytes = 4;
            }
            if (bytes == 0) {
                error = "sizeof needs a type or a name";
                return false;
            }
        }
        if (!expect(TRParen, "')'")) return false;
        out->kind = Node::IntLit;
        out->number = static_cast<uint64_t>(bytes);
        out->type.base = 1;
        return true;
    }
    if (is(TPlusPlus) || is(TMinusMinus)) {
        out->kind = Node::Unary;
        out->op = cur().kind;
        out->number = 0;
        next();
        Node inner;
        if (!parse_unary(&inner)) return false;
        out->kids.push_back(std::move(inner));
        return true;
    }
    if (is(TNumber)) {
        out->kind = Node::IntLit;
        out->number = cur().number;
        out->type.base = cur().real != 0 ? cur().real : 1;
        next();
    } else if (is(TString)) {
        out->kind = Node::StringLit;
        out->text = cur().text;
        out->type.pointers = 1;
        out->type.base = 2;
        next();
    } else if (is(TIdent)) {
        const std::string name = cur().text;
        next();
        if (name == "va_arg" && accept(TLParen)) {
            Node ap;
            if (!parse_expr(1, &ap)) return false;
            if (!expect(TComma, "','")) return false;
            Type ty;
            bool imp = false;
            bool exp = false;
            if (!parse_type(&ty, &imp, &exp)) return false;
            while (accept(TStar)) ++ty.pointers;
            if (!expect(TRParen, "')'")) return false;
            out->kind = Node::Call;
            out->text = "va_arg";
            out->type = ty;
            out->kids.push_back(std::move(ap));
        } else if (accept(TLParen)) {
            out->kind = Node::Call;
            out->text = name;
            if (!accept(TRParen)) {
                do {
                    Node arg;
                    if (!parse_expr(1, &arg)) return false;
                    out->kids.push_back(std::move(arg));
                } while (accept(TComma));
                if (!expect(TRParen, "')'")) return false;
            }
        } else if (enum_values.find(name) != enum_values.end()) {
            out->kind = Node::IntLit;
            out->number = enum_values[name];
            out->type.base = 1;
        } else {
            out->kind = Node::Name;
            out->text = name;
        }
    } else if (is(TLParen)) {
        const Tok ahead = at + 1 < tokens.size() ? tokens[at + 1].kind : TEnd;
        const bool type_ahead = ahead == TInt || ahead == TChar || ahead == TVoid || ahead == TShort || ahead == TUnsigned ||
                                ahead == TSigned || ahead == TLong || ahead == TFloat || ahead == TDouble || ahead == TStruct ||
                                ahead == TUnion || ahead == TEnum ||
                                (ahead == TIdent && typedefs.find(tokens[at + 1].text) != typedefs.end());
        if (type_ahead) {
            next();
            Type ty;
            bool imp = false;
            bool exp = false;
            if (!parse_type(&ty, &imp, &exp)) return false;
            while (accept(TStar)) ++ty.pointers;
            if (!expect(TRParen, "')'")) return false;
            Node inner;
            if (!parse_unary(&inner)) return false;
            out->kind = Node::Unary;
            out->type = ty;
            out->kids.push_back(std::move(inner));
        } else {
            next();
            if (!parse_expr(0, out)) return false;
            if (!expect(TRParen, "')'")) return false;
        }
    } else if (is(TStar) || is(TAmp) || is(TMinus) || is(TNot)) {
        out->kind = Node::Unary;
        out->op = cur().kind;
        next();
        Node inner;
        if (!parse_unary(&inner)) return false;
        out->kids.push_back(std::move(inner));
    } else {
        error = "expected an expression";
        return false;
    }
    while (is(TLBrack) || is(TDot) || is(TArrow) || is(TLParen) || is(TPlusPlus) || is(TMinusMinus)) {
        if (is(TPlusPlus) || is(TMinusMinus)) {
            Node wrapped;
            wrapped.kind = Node::Unary;
            wrapped.op = cur().kind;
            wrapped.number = 1;
            next();
            wrapped.kids.push_back(std::move(*out));
            *out = std::move(wrapped);
            continue;
        }
        if (is(TLParen)) {
            next();
            Node call;
            call.kind = Node::Call;
            call.kids.push_back(std::move(*out));
            if (!accept(TRParen)) {
                do {
                    Node arg;
                    if (!parse_expr(1, &arg)) return false;
                    call.kids.push_back(std::move(arg));
                } while (accept(TComma));
                if (!expect(TRParen, "')'")) return false;
            }
            *out = std::move(call);
            continue;
        }
        if (is(TLBrack)) {
            next();
            Node index;
            if (!parse_expr(0, &index)) return false;
            if (!expect(TRBrack, "']'")) return false;
            Node wrapped;
            wrapped.kind = Node::Index;
            wrapped.kids.push_back(std::move(*out));
            wrapped.kids.push_back(std::move(index));
            *out = std::move(wrapped);
        } else {
            const Tok op = cur().kind;
            next();
            if (!is(TIdent)) {
                error = "expected a field name";
                return false;
            }
            Node wrapped;
            wrapped.kind = Node::Field;
            wrapped.op = op;
            wrapped.text = cur().text;
            next();
            wrapped.kids.push_back(std::move(*out));
            *out = std::move(wrapped);
        }
    }
    return true;
}

bool Parser::parse_expr(int min_prec, Node* out) {
    if (!parse_unary(out)) return false;
    while (true) {
        const int p = prec(cur().kind);
        if (p < min_prec) break;
        const Tok op = cur().kind;
        next();
        if (op == TQuestion) {
            Node mid;
            Node els;
            if (!parse_expr(1, &mid)) return false;
            if (!expect(TColon, "':'")) return false;
            if (!parse_expr(p, &els)) return false;
            Node tern;
            tern.kind = Node::Ternary;
            tern.kids.push_back(std::move(*out));
            tern.kids.push_back(std::move(mid));
            tern.kids.push_back(std::move(els));
            *out = std::move(tern);
            continue;
        }
        Node right;
        const int next_min = op == TAssign || op == TPlusEq || op == TMinusEq || op == TStarEq || op == TSlashEq ||
                                     op == TPercentEq
                                 ? p
                                 : p + 1;
        if (!parse_expr(next_min, &right)) return false;
        Node bin;
        const bool compound = op == TPlusEq || op == TMinusEq || op == TStarEq || op == TSlashEq || op == TPercentEq;
        bin.kind = op == TAssign || compound ? Node::Assign : Node::Binary;
        bin.op = op;
        bin.kids.push_back(std::move(*out));
        bin.kids.push_back(std::move(right));
        if (bin.kind == Node::Binary && bin.kids[0].kind == Node::IntLit && bin.kids[1].kind == Node::IntLit &&
            bin.kids[0].type.base < 5 && bin.kids[1].type.base < 5) {
            const uint64_t left = bin.kids[0].number;
            const uint64_t right_n = bin.kids[1].number;
            bool folded = true;
            uint64_t result = 0;
            if (op == TPlus) result = left + right_n;
            else if (op == TMinus) result = left - right_n;
            else if (op == TStar) result = left * right_n;
            else if (op == TSlash || op == TPercent) {
                if (right_n == 0) {
                    error = "division by zero";
                    return false;
                }
                result = op == TSlash ? left / right_n : left % right_n;
            } else {
                folded = false;
            }
            if (folded) {
                bin = Node{};
                bin.kind = Node::IntLit;
                bin.number = result;
                bin.type.base = 1;
            }
        }
        *out = std::move(bin);
    }
    return true;
}

struct Gen {
    std::vector<FuncDecl>& funcs;
    std::string error;
    std::vector<uint8_t> text;
    std::vector<uint8_t> rdata;
    std::vector<uint8_t> xdata;
    std::vector<uint8_t> pdata;
    struct NamedReloc {
        int section;
        uint32_t offset;
        std::string symbol;
        uint16_t type;
    };
    std::vector<NamedReloc> relocs;
    std::vector<Local> locals;
    std::vector<std::pair<std::string, uint32_t>> string_at;
    int temp_depth = 0;
    int temp_base = 0;
    int frame = 0x20;
    const std::vector<StructDef>* structs = nullptr;
    const std::vector<Global>* globals = nullptr;
    const std::vector<FuncType>* func_types = nullptr;
    const FuncDecl* current = nullptr;
    bool sret_dest_set = false;
    int sret_dest_off = 0;
    bool save_nv = false;
    bool optimize = false;
    std::vector<uint8_t> extra_pops;
    std::map<std::string, size_t> labels;
    std::vector<std::pair<std::string, size_t>> gotos;
    std::vector<std::vector<size_t>> break_stack;
    Type last_type{};
    int scope_depth = 0;
    std::vector<int> scope_parent{0};

    void push_scope() {
        const int id = static_cast<int>(scope_parent.size());
        scope_parent.push_back(scope_depth);
        scope_depth = id;
    }
    void pop_scope() { scope_depth = scope_parent[static_cast<size_t>(scope_depth)]; }
    bool scope_visible(int local_scope) const {
        int walk = scope_depth;
        while (walk != local_scope) {
            if (walk == 0) return false;
            walk = scope_parent[static_cast<size_t>(walk)];
        }
        return true;
    }

    explicit Gen(std::vector<FuncDecl>& functions) : funcs(functions) {}

    void b(uint8_t v) { text.push_back(v); }
    void u32(uint32_t v) {
        for (int i = 0; i < 4; ++i) text.push_back(static_cast<uint8_t>(v >> (8 * i)));
    }
    void disp(int32_t offset) { u32(static_cast<uint32_t>(offset)); }

    FuncDecl* find_func(const std::string& name) {
        for (FuncDecl& fn : funcs)
            if (fn.name == name) return &fn;
        return nullptr;
    }
    Local* find_local(const std::string& name) {
        for (int i = static_cast<int>(locals.size()) - 1; i >= 0; --i) {
            Local& local = locals[static_cast<size_t>(i)];
            if (local.name == name && scope_visible(local.scope)) return &local;
        }
        return nullptr;
    }

    void load_slot(int32_t offset, bool pointer) {
        if (pointer) {
            b(0x48);
            b(0x8B);
        } else b(0x8B);
        b(0x85);
        disp(offset);
    }
    void store_slot(int32_t offset, bool pointer) {
        if (pointer) {
            b(0x48);
            b(0x89);
        } else b(0x89);
        b(0x85);
        disp(offset);
    }
    void mov_eax_imm(uint32_t value) {
        b(0xB8);
        u32(value);
    }
    int spill() {
        if (temp_depth >= 8) {
            error = "expression is too deep";
            return 0;
        }
        const int off = temp_base - temp_depth * 8;
        ++temp_depth;
        if (last_type.pointers == 0 && (last_type.base == 5 || last_type.base == 6)) {
            b(last_type.base == 6 ? 0xF2 : 0xF3);
            b(0x0F);
            b(0x11);
            b(0x85);
            disp(off);
            return off;
        }
        store_slot(off, true);
        return off;
    }
    void reload_ecx(int off) {
        b(0x8B);
        b(0x8D);
        disp(off);
        --temp_depth;
    }

    bool gen_expr(const Node& node);
    bool gen_stmt(const Node& node);
    bool gen_addr(const Node& node);
    int size_of(const Type& type) const;
    bool fits_register(const Type& type) const;
    Global* find_global(const std::string& name);
    Type element_type(const Type& type) const;
    void load_from_rax(int size, bool is_unsigned);
    void promote(Type* type) const;
    void load_typed(int32_t offset, const Type& type);
    void store_mem(int size);
    void epilogue() {
        b(0x48);
        b(0x89);
        b(0xEC);
        b(0x5D);
        for (uint8_t op : extra_pops) b(op);
        b(0xC3);
    }
    size_t emit_jcc(uint8_t cc) {
        b(0x0F);
        b(cc);
        const size_t at = text.size();
        u32(0);
        return at;
    }
    size_t emit_jmp() {
        b(0xE9);
        const size_t at = text.size();
        u32(0);
        return at;
    }
    void patch(size_t at, size_t target) {
        const int32_t rel = static_cast<int32_t>(target - (at + 4));
        for (int i = 0; i < 4; ++i) text[at + i] = static_cast<uint8_t>(static_cast<uint32_t>(rel) >> (8 * i));
    }
    void cmp_set(uint8_t setcc) {
        b(0x39);
        b(0xC1);
        b(0x0F);
        b(setcc);
        b(0xC0);
        b(0x0F);
        b(0xB6);
        b(0xC0);
    }
};

int Gen::size_of(const Type& type) const {
    if (type.array_len >= 0) {
        Type elem = type;
        elem.array_len = -1;
        return type.array_len * size_of(elem);
    }
    if (type.pointers > 0) return 8;
    if (type.base == 2) return 1;
    if (type.base == 4) return 2;
    if (type.base == 5) return 4;
    if (type.base == 6) return 8;
    if (type.base == 3 && structs != nullptr && type.struct_id >= 0 &&
        type.struct_id < static_cast<int>(structs->size()))
        return (*structs)[type.struct_id].size;
    if (type.base == 0) return 0;
    return 4;
}

bool Gen::fits_register(const Type& type) const {
    if (type.array_len >= 0 || type.pointers > 0) return true;
    const int size = size_of(type);
    return size == 1 || size == 2 || size == 4 || size == 8;
}

Global* Gen::find_global(const std::string& name) {
    if (globals == nullptr) return nullptr;
    for (Global& global : *const_cast<std::vector<Global>*>(globals))
        if (global.name == name) return &global;
    return nullptr;
}

Type Gen::element_type(const Type& type) const {
    Type elem = type;
    if (elem.array_len >= 0) {
        elem.array_len = -1;
        return elem;
    }
    if (elem.pointers > 0) --elem.pointers;
    return elem;
}

bool Gen::gen_addr(const Node& node) {
    if (node.kind == Node::Name) {
        if (Local* local = find_local(node.text)) {
            if (local->is_static) {
                b(0x48);
                b(0x8D);
                b(0x05);
                relocs.push_back({0, static_cast<uint32_t>(text.size()), local->symbol, kRelAmd64Rel32});
                u32(0);
                last_type = local->type;
                return true;
            }
            b(0x48);
            b(0x8D);
            b(0x85);
            disp(local->offset);
            last_type = local->type;
            return true;
        }
        if (Global* global = find_global(node.text)) {
            b(0x48);
            b(0x8D);
            b(0x05);
            relocs.push_back({0, static_cast<uint32_t>(text.size()), global->name, kRelAmd64Rel32});
            u32(0);
            last_type = global->type;
            return true;
        }
        error = "unknown name " + node.text;
        return false;
    }
    if (node.kind == Node::StringLit) {
        if (!gen_expr(node)) return false;
        last_type.pointers = 1;
        last_type.base = 2;
        last_type.array_len = -1;
        return true;
    }
    if (node.kind == Node::Index) {
        bool from_pointer = false;
        if (node.kids[0].kind == Node::Name) {
            if (Local* local = find_local(node.kids[0].text))
                from_pointer = local->type.pointers > 0 && !local->type.is_array();
            else if (Global* global = find_global(node.kids[0].text))
                from_pointer = global->type.pointers > 0 && !global->type.is_array();
        }
        if (from_pointer) {
            if (!gen_expr(node.kids[0])) return false;
        } else if (!gen_addr(node.kids[0])) {
            return false;
        }
        const Type base = last_type;
        const Type elem = element_type(base);
        const int elem_size = std::max(1, size_of(elem));
        const int slot = spill();
        if (!error.empty()) return false;
        if (!gen_expr(node.kids[1])) return false;
        b(0x48);
        b(0x69);
        b(0xC0);
        u32(static_cast<uint32_t>(elem_size));
        b(0x48);
        b(0x03);
        b(0x85);
        disp(slot);
        --temp_depth;
        last_type = elem;
        return true;
    }
    if (node.kind == Node::Field) {
        if (node.op == TArrow) {
            if (!gen_expr(node.kids[0])) return false;
            if (last_type.pointers < 1) {
                error = "arrow needs a pointer";
                return false;
            }
            last_type = element_type(last_type);
        } else if (!gen_addr(node.kids[0])) {
            return false;
        }
        if (last_type.base != 3 || structs == nullptr || last_type.struct_id < 0) {
            error = "field access on a non-struct";
            return false;
        }
        const StructDef& def = (*structs)[last_type.struct_id];
        const Field* field = nullptr;
        for (const Field& item : def.fields)
            if (item.name == node.text) field = &item;
        if (field == nullptr) {
            error = "unknown field " + node.text;
            return false;
        }
        if (field->offset != 0) {
            b(0x48);
            b(0x05);
            u32(static_cast<uint32_t>(field->offset));
        }
        last_type = field->type;
        return true;
    }
    if (node.kind == Node::Unary && node.op == TStar) {
        if (!gen_expr(node.kids[0])) return false;
        last_type = element_type(last_type);
        return true;
    }
    error = "not an address";
    return false;
}

void Gen::load_from_rax(int size, bool is_unsigned) {
    if (size >= 8) {
        b(0x48);
        b(0x8B);
        b(0x00);
    } else if (size == 1) {
        b(0x0F);
        b(is_unsigned ? 0xB6 : 0xBE);
        b(0x00);
    } else if (size == 2) {
        b(0x0F);
        b(is_unsigned ? 0xB7 : 0xBF);
        b(0x00);
    } else {
        b(0x8B);
        b(0x00);
    }
}

void Gen::load_typed(int32_t offset, const Type& type) {
    if (type.pointers == 0 && (type.base == 5 || type.base == 6)) {
        b(type.base == 6 ? 0xF2 : 0xF3);
        b(0x0F);
        b(0x10);
        b(0x85);
        disp(offset);
        return;
    }
    const int size = size_of(type);
    if (type.pointer() || size >= 8) load_slot(offset, true);
    else if (size == 4) load_slot(offset, false);
    else if (size == 2) {
        b(0x0F);
        b(type.is_unsigned ? 0xB7 : 0xBF);
        b(0x85);
        disp(offset);
    } else {
        b(0x0F);
        b(type.is_unsigned ? 0xB6 : 0xBE);
        b(0x85);
        disp(offset);
    }
}

void Gen::store_mem(int size) {
    if (size >= 8) {
        b(0x48);
        b(0x89);
        b(0x01);
    } else if (size == 1) {
        b(0x88);
        b(0x01);
    } else if (size == 2) {
        b(0x66);
        b(0x89);
        b(0x01);
    } else {
        b(0x89);
        b(0x01);
    }
}

void Gen::promote(Type* type) const {
    if (type->pointers > 0 || type->is_array() || type->base == 3) return;
    if (size_of(*type) < 4) {
        type->base = 1;
        type->is_unsigned = false;
    }
}

bool Gen::gen_expr(const Node& node) {
    if (!error.empty()) return false;
    if (node.kind == Node::IntLit) {
        if (node.type.base == 5 || node.type.base == 6) {
            b(0x48);
            b(0xB8);
            for (int i = 0; i < 8; ++i) b(static_cast<uint8_t>(node.number >> (8 * i)));
            b(0x66);
            if (node.type.base == 6) b(0x48);
            b(0x0F);
            b(0x6E);
            b(0xC0);
            last_type = Type{};
            last_type.base = node.type.base;
            return true;
        }
        mov_eax_imm(static_cast<uint32_t>(node.number));
        last_type = Type{};
        last_type.base = 1;
        return true;
    }
    if (node.kind == Node::StringLit) {
        const uint32_t at = static_cast<uint32_t>(rdata.size());
        rdata.insert(rdata.end(), node.text.begin(), node.text.end());
        rdata.push_back(0);
        const std::string sym = "str_" + std::to_string(string_at.size());
        string_at.push_back({sym, at});
        b(0x48);
        b(0x8D);
        b(0x05);
        relocs.push_back({0, static_cast<uint32_t>(text.size()), sym, kRelAmd64Rel32});
        u32(0);
        last_type = Type{};
        last_type.pointers = 1;
        last_type.base = 2;
        return true;
    }
    if (node.kind == Node::Name) {
        if (Local* local = find_local(node.text)) {
            if (local->reg != 0) {
                if (local->reg == 3) {
                    b(0x89);
                    b(0xD8);
                } else if (local->reg == 6) {
                    b(0x89);
                    b(0xF0);
                } else {
                    b(0x89);
                    b(0xF8);
                }
                last_type = local->type;
                promote(&last_type);
                return true;
            }
            if (local->is_static) {
                const int size = size_of(local->type);
                const bool uns = local->type.is_unsigned;
                if (local->type.pointer() || size >= 8) {
                    b(0x48);
                    b(0x8B);
                    b(0x05);
                } else if (size == 4) {
                    b(0x8B);
                    b(0x05);
                } else if (size == 2) {
                    b(0x0F);
                    b(uns ? 0xB7 : 0xBF);
                    b(0x05);
                } else {
                    b(0x0F);
                    b(uns ? 0xB6 : 0xBE);
                    b(0x05);
                }
                relocs.push_back({0, static_cast<uint32_t>(text.size()), local->symbol, kRelAmd64Rel32});
                u32(0);
                last_type = local->type;
                promote(&last_type);
                return true;
            }
            if (local->type.is_array()) {
                if (!gen_addr(node)) return false;
                last_type = element_type(local->type);
                last_type.pointers += 1;
                return true;
            }
            load_typed(local->offset, local->type);
            last_type = local->type;
            promote(&last_type);
            return true;
        }
        if (Global* global = find_global(node.text)) {
            if (global->type.is_array()) {
                if (!gen_addr(node)) return false;
                last_type = element_type(global->type);
                last_type.pointers += 1;
                return true;
            }
            const int size = size_of(global->type);
            const bool uns = global->type.is_unsigned;
            if (global->type.pointer() || size >= 8) {
                b(0x48);
                b(0x8B);
                b(0x05);
            } else if (size == 4) {
                b(0x8B);
                b(0x05);
            } else if (size == 2) {
                b(0x0F);
                b(uns ? 0xB7 : 0xBF);
                b(0x05);
            } else {
                b(0x0F);
                b(uns ? 0xB6 : 0xBE);
                b(0x05);
            }
            relocs.push_back({0, static_cast<uint32_t>(text.size()), global->name, kRelAmd64Rel32});
            u32(0);
            last_type = global->type;
            promote(&last_type);
            return true;
        }
        if (FuncDecl* fn = find_func(node.text)) {
            b(0x48);
            b(0x8D);
            b(0x05);
            relocs.push_back({0, static_cast<uint32_t>(text.size()), fn->name, kRelAmd64Rel32});
            u32(0);
            last_type = Type{};
            last_type.pointers = 1;
            last_type.func_id = fn->func_id;
            return true;
        }
        error = "unknown name " + node.text;
        return false;
    }
    if (node.kind == Node::Index || node.kind == Node::Field) {
        if (!gen_addr(node)) return false;
        const int size = std::max(1, size_of(last_type));
        const bool uns = last_type.is_unsigned;
        const int bits = last_type.bits;
        const int bit_off = last_type.bit_off;
        load_from_rax(size, uns || bits >= 0);
        if (bits >= 0) {
            if (bit_off != 0) {
                b(0xC1);
                b(0xE8);
                b(static_cast<uint8_t>(bit_off));
            }
            const uint32_t mask = bits >= 32 ? 0xFFFFFFFFu : (1u << bits) - 1u;
            b(0x25);
            u32(mask);
        }
        promote(&last_type);
        return true;
    }
    if (node.kind == Node::Ternary) {
        if (!gen_expr(node.kids[0])) return false;
        b(0x85);
        b(0xC0);
        const size_t jz = emit_jcc(0x84);
        if (!gen_expr(node.kids[1])) return false;
        const size_t jmp = emit_jmp();
        patch(jz, text.size());
        if (!gen_expr(node.kids[2])) return false;
        patch(jmp, text.size());
        return true;
    }
    if (node.kind == Node::Unary) {
        if (node.op == TEnd && !node.kids.empty()) {
            if (!gen_expr(node.kids[0])) return false;
            const Type from = last_type;
            const Type to = node.type;
            const bool from_fp = from.pointers == 0 && (from.base == 5 || from.base == 6);
            const bool to_fp = to.pointers == 0 && (to.base == 5 || to.base == 6);
            if (!from_fp && to_fp) {
                b(to.base == 6 ? 0xF2 : 0xF3);
                b(0x0F);
                b(0x2A);
                b(0xC0);
            } else if (from_fp && !to_fp) {
                b(from.base == 6 ? 0xF2 : 0xF3);
                b(0x0F);
                b(0x2C);
                b(0xC0);
            } else if (from_fp && to_fp && from.base != to.base) {
                b(to.base == 6 ? 0xF3 : 0xF2);
                b(0x0F);
                b(0x5A);
                b(0xC0);
            }
            last_type = to;
            return true;
        }
        if (node.op == TPlusPlus || node.op == TMinusMinus) {
            if (!gen_addr(node.kids[0])) return false;
            const bool pointer = last_type.pointers > 0;
            const int step = pointer ? std::max(1, size_of(element_type(last_type))) : 1;
            const int width = pointer ? 8 : std::max(1, size_of(last_type));
            const int addr = spill();
            if (!error.empty()) return false;
            b(0x48);
            b(0x8B);
            b(0x85);
            disp(addr);
            if (width >= 8) {
                b(0x48);
                b(0x8B);
                b(0x00);
            } else if (width == 1) {
                b(0x0F);
                b(0xBE);
                b(0x00);
            } else if (width == 2) {
                b(0x0F);
                b(0xBF);
                b(0x00);
            } else {
                b(0x8B);
                b(0x00);
            }
            int old = 0;
            if (node.number != 0) {
                old = spill();
                if (!error.empty()) return false;
            }
            if (pointer) b(0x48);
            b(0x83);
            b(node.op == TPlusPlus ? 0xC0 : 0xE8);
            b(static_cast<uint8_t>(step));
            b(0x48);
            b(0x8B);
            b(0x8D);
            disp(addr);
            store_mem(width);
            if (node.number != 0) {
                load_slot(old, true);
                --temp_depth;
            }
            --temp_depth;
            return true;
        }
        if (node.op == TAmp) {
            if (!gen_addr(node.kids[0])) return false;
            Type pointed = last_type;
            last_type = Type{};
            last_type.pointers = 1;
            last_type.base = pointed.base;
            last_type.struct_id = pointed.struct_id;
            if (pointed.is_array()) {
                last_type = element_type(pointed);
                last_type.pointers += 1;
            }
            return true;
        }
        if (!gen_expr(node.kids[0])) return false;
        if (node.op == TMinus) {
            if (last_type.pointers == 0 && (last_type.base == 5 || last_type.base == 6)) {
                const bool dbl = last_type.base == 6;
                b(0x48);
                b(0xB8);
                const uint64_t sign = dbl ? 0x8000000000000000ull : 0x80000000ull;
                for (int i = 0; i < 8; ++i) b(static_cast<uint8_t>(sign >> (8 * i)));
                b(0x66);
                if (dbl) b(0x48);
                b(0x0F);
                b(0x6E);
                b(0xC8);
                if (dbl) b(0x66);
                b(0x0F);
                b(0x57);
                b(0xC1);
                return true;
            }
            b(0xF7);
            b(0xD8);
            return true;
        }
        if (node.op == TNot) {
            b(0x85);
            b(0xC0);
            b(0x0F);
            b(0x94);
            b(0xC0);
            b(0x0F);
            b(0xB6);
            b(0xC0);
            last_type = Type{};
            return true;
        }
        if (node.op == TStar) {
            const Type elem = element_type(last_type);
            load_from_rax(std::max(1, size_of(elem)), elem.is_unsigned);
            last_type = elem;
            promote(&last_type);
            return true;
        }
        error = "bad unary operator";
        return false;
    }
    if (node.kind == Node::Assign) {
        if (node.kids[0].kind == Node::Name && (node.op == TAssign || node.op == TEnd)) {
            if (Local* local = find_local(node.kids[0].text)) {
                if (local->reg != 0) {
                    if (!gen_expr(node.kids[1])) return false;
                    if (local->reg == 3) {
                        b(0x89);
                        b(0xC3);
                    } else if (local->reg == 6) {
                        b(0x89);
                        b(0xC6);
                    } else {
                        b(0x89);
                        b(0xC7);
                    }
                    return true;
                }
            }
        }
        if (node.number != 1 && node.kids[0].kind == Node::Name) {
            if (Local* local = find_local(node.kids[0].text)) {
                if (local->is_const) {
                    error = "assignment to const " + local->name;
                    return false;
                }
            }
            if (Global* global = find_global(node.kids[0].text)) {
                if (global->type.is_const) {
                    error = "assignment to const " + global->name;
                    return false;
                }
            }
        }
        if (node.op == TPlusEq || node.op == TMinusEq || node.op == TStarEq || node.op == TSlashEq || node.op == TPercentEq) {
            if (!gen_addr(node.kids[0])) return false;
            const int width = std::max(1, size_of(last_type));
            const int addr = spill();
            if (!error.empty()) return false;
            b(0x48);
            b(0x8B);
            b(0x85);
            disp(addr);
            load_from_rax(width >= 8 ? 8 : width, false);
            const int left = spill();
            if (!error.empty()) return false;
            if (!gen_expr(node.kids[1])) return false;
            reload_ecx(left);
            if (node.op == TPlusEq) {
                b(0x01);
                b(0xC8);
            } else if (node.op == TMinusEq) {
                b(0x29);
                b(0xC1);
                b(0x89);
                b(0xC8);
            } else if (node.op == TStarEq) {
                b(0x0F);
                b(0xAF);
                b(0xC1);
            } else {
                b(0x91);
                b(0x99);
                b(0xF7);
                b(0xF9);
                if (node.op == TPercentEq) {
                    b(0x89);
                    b(0xD0);
                }
            }
            b(0x48);
            b(0x8B);
            b(0x8D);
            disp(addr);
            store_mem(width);
            --temp_depth;
            return true;
        }
        if (node.kids[0].kind == Node::Name) {
            if (Local* local = find_local(node.kids[0].text)) {
                const bool big = local->type.base == 3 && local->type.pointers == 0 && !fits_register(local->type);
                if (big && node.kids[1].kind == Node::Call) {
                    sret_dest_set = true;
                    sret_dest_off = local->offset;
                    if (!gen_expr(node.kids[1])) return false;
                    last_type = local->type;
                    return true;
                }
                if (big) {
                    if (!gen_addr(node.kids[1])) return false;
                    b(0x48);
                    b(0x89);
                    b(0xC6);
                    b(0x48);
                    b(0x8D);
                    b(0xBD);
                    disp(local->offset);
                    b(0xB9);
                    u32(static_cast<uint32_t>(size_of(local->type)));
                    b(0xF3);
                    b(0xA4);
                    last_type = local->type;
                    return true;
                }
            }
        }
        if (!gen_expr(node.kids[1])) return false;
        const bool rhs_fp = last_type.pointers == 0 && (last_type.base == 5 || last_type.base == 6);
        const int rhs_base = last_type.base;
        const int slot = spill();
        if (!error.empty()) return false;
        if (!gen_addr(node.kids[0])) return false;
        if (rhs_fp) {
            b(0x48);
            b(0x89);
            b(0xC1);
            b(rhs_base == 6 ? 0xF2 : 0xF3);
            b(0x0F);
            b(0x10);
            b(0x85);
            disp(slot);
            b(rhs_base == 6 ? 0xF2 : 0xF3);
            b(0x0F);
            b(0x11);
            b(0x01);
            --temp_depth;
            return true;
        }
        const int size = std::max(1, size_of(last_type));
        b(0x48);
        b(0x89);
        b(0xC1);
        b(0x48);
        b(0x8B);
        b(0x85);
        disp(slot);
        --temp_depth;
        if (last_type.pointers == 0 && (last_type.base == 5 || last_type.base == 6) && !rhs_fp) {
            b(last_type.base == 6 ? 0xF2 : 0xF3);
            b(0x0F);
            b(0x2A);
            b(0xC0);
            b(last_type.base == 6 ? 0xF2 : 0xF3);
            b(0x0F);
            b(0x11);
            b(0x01);
            return true;
        }
        if (last_type.bits >= 0) {
            const int bits = last_type.bits;
            const int bit_off = last_type.bit_off;
            const uint32_t mask = bits >= 32 ? 0xFFFFFFFFu : (1u << static_cast<unsigned>(bits)) - 1u;
            b(0x89);
            b(0xC2);
            b(0x81);
            b(0xE2);
            u32(mask);
            if (bit_off != 0) {
                b(0xC1);
                b(0xE2);
                b(static_cast<uint8_t>(bit_off));
            }
            b(0x8B);
            b(0x01);
            b(0x25);
            u32(~(mask << bit_off));
            b(0x09);
            b(0xD0);
            b(0x89);
            b(0x01);
        } else {
            store_mem(size);
        }
        last_type = Type{};
        last_type.base = 1;
        return true;
    }
    if (node.kind == Node::Call) {
        if (node.text == "va_end") {
            mov_eax_imm(0);
            return true;
        }
        if (node.text == "va_start") {
            if (current == nullptr || node.kids.size() != 2 || node.kids[1].kind != Node::Name) {
                error = "va_start needs the last named parameter";
                return false;
            }
            int index = -1;
            for (int i = 0; i < current->nparams; ++i) {
                if (current->param_names[static_cast<size_t>(i)] == node.kids[1].text) index = i;
            }
            if (index < 0) {
                error = "va_start parameter was not found";
                return false;
            }
            const int slot = index + (current->sret ? 1 : 0);
            const int home = 16 + (slot + 1) * 8;
            if (!gen_addr(node.kids[0])) return false;
            b(0x48);
            b(0x89);
            b(0xC1);
            b(0x48);
            b(0x8D);
            b(0x85);
            disp(home);
            b(0x48);
            b(0x89);
            b(0x01);
            return true;
        }
        if (node.text == "va_arg") {
            if (node.kids.size() != 1 || node.kids[0].kind != Node::Name) {
                error = "va_arg list must be a name";
                return false;
            }
            Local* ap = find_local(node.kids[0].text);
            if (ap == nullptr) {
                error = "va_arg list must be a local";
                return false;
            }
            load_typed(ap->offset, ap->type);
            const int size = std::max(1, size_of(node.type));
            b(0x48);
            b(0x89);
            b(0xC1);
            load_from_rax(size >= 8 ? 8 : size, node.type.is_unsigned || node.type.pointer());
            // The bits are in rax. A previous va_arg may have left last_type as
            // float or double, and spill would then store the old xmm0.
            last_type = Type{};
            last_type.pointers = 1;
            const int slot = spill();
            if (!error.empty()) return false;
            b(0x48);
            b(0x83);
            b(0x85);
            disp(ap->offset);
            b(0x08);
            load_slot(slot, true);
            --temp_depth;
            if (node.type.pointers == 0 && (node.type.base == 5 || node.type.base == 6)) {
                b(0x66);
                if (node.type.base == 6) b(0x48);
                b(0x0F);
                b(0x6E);
                b(0xC0);
            }
            last_type = node.type;
            promote(&last_type);
            return true;
        }
        const bool indirect = node.text.empty();
        FuncDecl* fn = nullptr;
        FuncType sig;
        int callee_slot = 0;
        const int depth_before = temp_depth;
        size_t arg0 = 0;
        if (indirect) {
            if (node.kids.empty() || !gen_expr(node.kids[0])) return false;
            if (last_type.func_id < 0 || func_types == nullptr ||
                static_cast<size_t>(last_type.func_id) >= func_types->size()) {
                error = "call target is not a function";
                return false;
            }
            sig = (*func_types)[static_cast<size_t>(last_type.func_id)];
            callee_slot = spill();
            if (!error.empty()) return false;
            arg0 = 1;
        } else {
            fn = find_func(node.text);
            if (!fn) {
                error = "unknown function " + node.text;
                return false;
            }
            sig.ret = fn->ret;
            sig.params = fn->params;
            sig.vararg = fn->vararg;
        }
        const size_t nargs = node.kids.size() - arg0;
        if (!sig.vararg && nargs != sig.params.size()) {
            error = "wrong number of arguments to " + (node.text.empty() ? std::string("function pointer") : node.text);
            return false;
        }
        if (sig.vararg && nargs < sig.params.size()) {
            error = "too few arguments to " + node.text;
            return false;
        }
        const bool sret = sig.ret.base == 3 && sig.ret.pointers == 0 && !fits_register(sig.ret);
        int hidden = 0;
        if (sret) {
            if (sret_dest_set) {
                hidden = sret_dest_off;
                sret_dest_set = false;
            } else if (current != nullptr && current->sret) {
                hidden = current->sret_off;
            } else {
                error = "large struct return has no destination";
                return false;
            }
        }
        std::vector<int> saved;
        std::vector<Type> saved_types;
        for (size_t arg_i = 0; arg_i < nargs; ++arg_i) {
            const bool named = arg_i < sig.params.size();
            const Type param = named ? sig.params[arg_i] : Type{};
            if (named && param.base == 3 && param.pointers == 0 && !fits_register(param)) {
                if (!gen_addr(node.kids[arg0 + arg_i])) return false;
            } else if (named && param.base == 3 && param.pointers == 0) {
                if (!gen_addr(node.kids[arg0 + arg_i])) return false;
                b(0x48);
                b(0x8B);
                b(0x00);
            } else if (!gen_expr(node.kids[arg0 + arg_i])) {
                return false;
            }
            saved_types.push_back(last_type);
            saved.push_back(spill());
            if (!error.empty()) return false;
        }
        const int reg_base = sret ? 1 : 0;
        for (int i = static_cast<int>(saved.size()) - 1; i >= 0; --i) {
            const int reg = i + reg_base;
            if (reg < 4) continue;
            b(0x48);
            b(0x8B);
            b(0x85);
            disp(saved[static_cast<size_t>(i)]);
            b(0x48);
            b(0x89);
            b(0x84);
            b(0x24);
            u32(static_cast<uint32_t>(32 + (reg - 4) * 8));
        }
        auto load_reg = [&](int reg, int spill_off) {
            if (reg == 0) {
                b(0x48);
                b(0x8B);
                b(0x8D);
            } else if (reg == 1) {
                b(0x48);
                b(0x8B);
                b(0x95);
            } else if (reg == 2) {
                b(0x4C);
                b(0x8B);
                b(0x85);
            } else if (reg == 3) {
                b(0x4C);
                b(0x8B);
                b(0x8D);
            } else {
                return;
            }
            disp(spill_off);
        };
        if (sret) {
            b(0x48);
            b(0x8D);
            b(0x8D);
            disp(hidden);
        }
        for (int i = 0; i < static_cast<int>(saved.size()); ++i) {
            const int reg = i + reg_base;
            const Type& at = saved_types[static_cast<size_t>(i)];
            const bool fp = at.pointers == 0 && (at.base == 5 || at.base == 6);
            if (fp && reg >= 0 && reg < 4) {
                const uint8_t modrm = reg == 0 ? 0x85 : reg == 1 ? 0x8D : reg == 2 ? 0x95 : 0x9D;
                b(at.base == 6 ? 0xF2 : 0xF3);
                b(0x0F);
                b(0x10);
                b(modrm);
                disp(saved[static_cast<size_t>(i)]);
            } else {
                load_reg(reg, saved[static_cast<size_t>(i)]);
            }
        }
        if (sig.vararg) {
            for (int i = 0; i < static_cast<int>(saved.size()); ++i) {
                const int reg = i + reg_base;
                const Type& at = saved_types[static_cast<size_t>(i)];
                if (at.pointers != 0 || (at.base != 5 && at.base != 6) || reg < 0 || reg >= 4) continue;
                b(0x66);
                // movq r64, xmm. REX.B selects r8/r9; REX.R would select xmm8+ instead.
                b(reg >= 2 ? 0x49 : 0x48);
                b(0x0F);
                b(0x7E);
                const uint8_t modrm = reg == 0 ? 0xC1 : reg == 1 ? 0xCA : reg == 2 ? 0xD0 : 0xD9;
                b(modrm);
            }
        }
        if (sig.vararg) {
            // The caller of a varargs function writes the register arguments
            // into the 32-byte home area so the callee can walk them.
            b(0x48);
            b(0x89);
            b(0x0C);
            b(0x24);
            b(0x48);
            b(0x89);
            b(0x54);
            b(0x24);
            b(0x08);
            b(0x4C);
            b(0x89);
            b(0x44);
            b(0x24);
            b(0x10);
            b(0x4C);
            b(0x89);
            b(0x4C);
            b(0x24);
            b(0x18);
        }
        temp_depth = depth_before;
        if (indirect) {
            b(0x48);
            b(0x8B);
            b(0x85);
            disp(callee_slot);
            b(0xFF);
            b(0xD0);
        } else if (fn->imported) {
            b(0xFF);
            b(0x15);
            relocs.push_back({0, static_cast<uint32_t>(text.size()), "__imp_" + fn->name, kRelAmd64Rel32});
            u32(0);
        } else {
            b(0xE8);
            relocs.push_back({0, static_cast<uint32_t>(text.size()), fn->name, kRelAmd64Rel32});
            u32(0);
        }
        last_type = sig.ret;
        if (sret) {
            b(0x48);
            b(0x8D);
            b(0x85);
            disp(hidden);
        } else {
            promote(&last_type);
        }
        return true;
    }
    if (node.kind == Node::Binary) {
        if (node.op == TAndAnd || node.op == TOrOr) {
            if (!gen_expr(node.kids[0])) return false;
            b(0x85);
            b(0xC0);
            const size_t j1 = emit_jcc(node.op == TAndAnd ? 0x84 : 0x85);
            if (!gen_expr(node.kids[1])) return false;
            b(0x85);
            b(0xC0);
            const size_t j2 = emit_jcc(node.op == TAndAnd ? 0x84 : 0x85);
            mov_eax_imm(node.op == TAndAnd ? 1 : 0);
            const size_t jmp = emit_jmp();
            const size_t other = text.size();
            mov_eax_imm(node.op == TAndAnd ? 0 : 1);
            const size_t end = text.size();
            patch(j1, other);
            patch(j2, other);
            patch(jmp, end);
            return true;
        }
        if (!gen_expr(node.kids[0])) return false;
        const Type left_type = last_type;
        const int off = spill();
        if (!error.empty()) return false;
        if (!gen_expr(node.kids[1])) return false;
        if ((left_type.pointers > 0 || left_type.is_array()) && node.op == TPlus) {
            const int scale = std::max(1, size_of(element_type(left_type)));
            b(0x48);
            b(0x69);
            b(0xC0);
            u32(static_cast<uint32_t>(scale));
            b(0x48);
            b(0x03);
            b(0x85);
            disp(off);
            --temp_depth;
            last_type = left_type;
            if (last_type.is_array()) {
                last_type = element_type(last_type);
                last_type.pointers += 1;
                last_type.array_len = -1;
            }
            return true;
        }
        const bool left_fp = left_type.pointers == 0 && (left_type.base == 5 || left_type.base == 6);
        const bool right_fp = last_type.pointers == 0 && (last_type.base == 5 || last_type.base == 6);
        if (left_fp || right_fp) {
            const bool use_double = (left_fp && left_type.base == 6) || (right_fp && last_type.base == 6);
            auto as_xmm = [&](const Type& have) {
                if (have.pointers == 0 && (have.base == 5 || have.base == 6)) {
                    if (use_double && have.base == 5) {
                        b(0xF3);
                        b(0x0F);
                        b(0x5A);
                        b(0xC0);
                    } else if (!use_double && have.base == 6) {
                        b(0xF2);
                        b(0x0F);
                        b(0x5A);
                        b(0xC0);
                    }
                } else {
                    b(use_double ? 0xF2 : 0xF3);
                    b(0x0F);
                    b(0x2A);
                    b(0xC0);
                }
            };
            as_xmm(last_type);
            b(use_double ? 0xF2 : 0xF3);
            b(0x0F);
            b(0x10);
            b(0xC8);
            if (left_fp) {
                b(left_type.base == 6 ? 0xF2 : 0xF3);
                b(0x0F);
                b(0x10);
                b(0x85);
                disp(off);
            } else {
                b(0x8B);
                b(0x85);
                disp(off);
            }
            --temp_depth;
            as_xmm(left_type);
            uint8_t op = 0x58;
            if (node.op == TMinus) op = 0x5C;
            else if (node.op == TStar) op = 0x59;
            else if (node.op == TSlash) op = 0x5E;
            const bool cmp = node.op == TEq || node.op == TNe || node.op == TLt || node.op == TGt || node.op == TLe ||
                             node.op == TGe;
            if (!cmp && node.op != TPlus && node.op != TMinus && node.op != TStar && node.op != TSlash) {
                error = "operator is not supported";
                return false;
            }
            if (cmp && use_double) b(0x66);
            if (cmp) {
                b(0x0F);
                b(0x2E);
                b(0xC1);
            } else {
                b(use_double ? 0xF2 : 0xF3);
                b(0x0F);
                b(op);
                b(0xC1);
            }
            if (cmp) {
                uint8_t setcc = 0x94;
                if (node.op == TNe) setcc = 0x95;
                else if (node.op == TLt) setcc = 0x92;
                else if (node.op == TGt) setcc = 0x97;
                else if (node.op == TLe) setcc = 0x96;
                else if (node.op == TGe) setcc = 0x93;
                b(0x0F);
                b(setcc);
                b(0xC0);
                b(0x0F);
                b(0xB6);
                b(0xC0);
                last_type = Type{};
                last_type.base = 1;
            } else {
                last_type = Type{};
                last_type.base = use_double ? 6 : 5;
            }
            return true;
        }
        reload_ecx(off);
        if (node.op == TComma) {
            last_type = last_type;
            return true;
        }
        if (node.op == TPlus) {
            b(0x01);
            b(0xC8);
            last_type = left_type;
            return true;
        }
        if (node.op == TMinus) {
            b(0x29);
            b(0xC1);
            b(0x89);
            b(0xC8);
            return true;
        }
        if (node.op == TStar) {
            b(0x0F);
            b(0xAF);
            b(0xC1);
            return true;
        }
        if (node.op == TSlash || node.op == TPercent) {
            b(0x91);
            b(0x99);
            b(0xF7);
            b(0xF9);
            if (node.op == TPercent) {
                b(0x89);
                b(0xD0);
            }
            last_type = left_type;
            return true;
        }
        const bool unsign = left_type.is_unsigned || last_type.is_unsigned;
        uint8_t setcc = 0x94;
        if (node.op == TEq) setcc = 0x94;
        else if (node.op == TNe) setcc = 0x95;
        else if (node.op == TLt) setcc = unsign ? 0x92 : 0x9C;
        else if (node.op == TGt) setcc = unsign ? 0x97 : 0x9F;
        else if (node.op == TLe) setcc = unsign ? 0x96 : 0x9E;
        else if (node.op == TGe) setcc = unsign ? 0x93 : 0x9D;
        else {
            error = "operator is not supported";
            return false;
        }
        cmp_set(setcc);
        return true;
    }
    error = "bad expression";
    return false;
}

bool Gen::gen_stmt(const Node& node) {
    temp_depth = 0;
    if (node.kind == Node::Block) {
        const bool scoped = node.text == "scope";
        if (scoped) push_scope();
        for (const Node& kid : node.kids)
            if (!gen_stmt(kid)) return false;
        if (scoped) pop_scope();
        return true;
    }
    if (node.kind == Node::Return) {
        if (current != nullptr && current->sret) {
            if (node.kids.empty()) {
                error = "missing return value";
                return false;
            }
            if (node.kids[0].kind == Node::Call) {
                sret_dest_set = true;
                sret_dest_off = current->sret_off;
                if (!gen_expr(node.kids[0])) return false;
            } else {
                if (!gen_addr(node.kids[0])) return false;
                b(0x48);
                b(0x89);
                b(0xC3);
                b(0x48);
                b(0x89);
                b(0xDE);
                b(0x48);
                b(0x8B);
                b(0xBD);
                disp(current->sret_off);
                b(0xB9);
                u32(static_cast<uint32_t>(size_of(current->ret)));
                b(0xF3);
                b(0xA4);
            }
            b(0x48);
            b(0x8B);
            b(0x85);
            disp(current->sret_off);
            epilogue();
            return true;
        }
        if (!node.kids.empty() && !gen_expr(node.kids[0])) return false;
        epilogue();
        return true;
    }
    if (node.kind == Node::ExprStmt) return gen_expr(node.kids[0]);
    if (node.kind == Node::If) {
        if (optimize && node.kids[0].kind == Node::IntLit) {
            if (node.kids[0].number != 0) return gen_stmt(node.kids[1]);
            if (node.kids.size() == 3) return gen_stmt(node.kids[2]);
            return true;
        }
        if (!gen_expr(node.kids[0])) return false;
        b(0x85);
        b(0xC0);
        const size_t jz = emit_jcc(0x84);
        if (!gen_stmt(node.kids[1])) return false;
        if (node.kids.size() == 3) {
            const size_t jmp = emit_jmp();
            patch(jz, text.size());
            if (!gen_stmt(node.kids[2])) return false;
            patch(jmp, text.size());
        } else patch(jz, text.size());
        return true;
    }
    if (node.kind == Node::While) {
        break_stack.emplace_back();
        const size_t start = text.size();
        if (!gen_expr(node.kids[0])) return false;
        b(0x85);
        b(0xC0);
        const size_t jz = emit_jcc(0x84);
        if (!gen_stmt(node.kids[1])) return false;
        const size_t jmp = emit_jmp();
        patch(jmp, start);
        const size_t end = text.size();
        patch(jz, end);
        for (size_t site : break_stack.back()) patch(site, end);
        break_stack.pop_back();
        return true;
    }
    if (node.kind == Node::For) {
        break_stack.emplace_back();
        auto skipped = [](const Node& item) { return item.kind == Node::Block && item.text == "skip"; };
        if (!skipped(node.kids[0]) && !gen_expr(node.kids[0])) return false;
        const size_t start = text.size();
        size_t jz = 0;
        bool has_cond = !skipped(node.kids[1]);
        if (has_cond) {
            if (!gen_expr(node.kids[1])) return false;
            b(0x85);
            b(0xC0);
            jz = emit_jcc(0x84);
        }
        if (!gen_stmt(node.kids[3])) return false;
        if (!skipped(node.kids[2]) && !gen_expr(node.kids[2])) return false;
        const size_t jmp = emit_jmp();
        patch(jmp, start);
        const size_t end = text.size();
        if (has_cond) patch(jz, end);
        for (size_t site : break_stack.back()) patch(site, end);
        break_stack.pop_back();
        return true;
    }
    if (node.kind == Node::Do) {
        break_stack.emplace_back();
        const size_t start = text.size();
        if (!gen_stmt(node.kids[0])) return false;
        if (!gen_expr(node.kids[1])) return false;
        b(0x85);
        b(0xC0);
        const size_t jne = emit_jcc(0x85);
        patch(jne, start);
        const size_t end = text.size();
        for (size_t site : break_stack.back()) patch(site, end);
        break_stack.pop_back();
        return true;
    }
    if (node.kind == Node::Switch) {
        if (!gen_expr(node.kids[0])) return false;
        const int slot = spill();
        if (!error.empty()) return false;
        break_stack.emplace_back();
        std::vector<size_t> case_jumps;
        size_t default_jump = 0;
        bool has_default = false;
        for (size_t i = 1; i < node.kids.size(); ++i) {
            if (node.kids[i].op == TDefault) {
                has_default = true;
                continue;
            }
            b(0x8B);
            b(0x85);
            disp(slot);
            b(0x3D);
            u32(static_cast<uint32_t>(node.kids[i].number));
            case_jumps.push_back(emit_jcc(0x84));
        }
        const size_t fall = emit_jmp();
        size_t case_i = 0;
        size_t default_at = 0;
        for (size_t i = 1; i < node.kids.size(); ++i) {
            if (node.kids[i].op == TDefault) default_at = text.size();
            else patch(case_jumps[case_i++], text.size());
            for (const Node& stmt : node.kids[i].kids)
                if (!gen_stmt(stmt)) return false;
        }
        const size_t end = text.size();
        patch(fall, has_default ? default_at : end);
        for (size_t site : break_stack.back()) patch(site, end);
        break_stack.pop_back();
        --temp_depth;
        (void)default_jump;
        return true;
    }
    if (node.kind == Node::Goto) {
        gotos.push_back({node.text, emit_jmp()});
        return true;
    }
    if (node.kind == Node::Label) {
        if (labels.count(node.text) != 0) {
            error = "duplicate label " + node.text;
            return false;
        }
        labels[node.text] = text.size();
        if (!node.kids.empty() && !gen_stmt(node.kids[0])) return false;
        return true;
    }
    if (node.kind == Node::Break) {
        if (break_stack.empty()) {
            error = "break is outside a loop or switch";
            return false;
        }
        break_stack.back().push_back(emit_jmp());
        return true;
    }
    error = "bad statement";
    return false;
}

int count_args(const Node& node) {
    int m = 0;
    if (node.kind == Node::Call) m = static_cast<int>(node.kids.size());
    for (const Node& kid : node.kids) m = std::max(m, count_args(kid));
    return m;
}

void collect_locals(const Node& node, std::vector<Local>* locals) {
    if (node.kind == Node::Block || node.kind == Node::If || node.kind == Node::While || node.kind == Node::Return ||
        node.kind == Node::ExprStmt) {
        for (const Node& kid : node.kids) collect_locals(kid, locals);
    }
}

struct Sym {
    std::string name;
    uint32_t value = 0;
    int16_t section = 0;
    uint16_t type = 0;
    uint8_t storage = kClassExternal;
};

bool address_taken(const Node& node, const std::string& name) {
    if (node.kind == Node::Unary && node.op == TAmp && !node.kids.empty() && node.kids[0].kind == Node::Name &&
        node.kids[0].text == name)
        return true;
    for (const Node& kid : node.kids)
        if (address_taken(kid, name)) return true;
    return false;
}

bool expr_uses(const Node& node, const std::string& name) {
    if (node.kind == Node::Name && node.text == name) return true;
    for (const Node& kid : node.kids)
        if (expr_uses(kid, name)) return true;
    return false;
}

Node* statement_value(Node* node) {
    if (node->kind == Node::ExprStmt && node->kids.size() == 1) return &node->kids[0];
    return node;
}

void prop_node(Node* node, std::map<std::string, uint64_t>* known, const std::set<std::string>* shadowed) {
    if (node->kind == Node::Block || node->kind == Node::If || node->kind == Node::While || node->kind == Node::For ||
        node->kind == Node::Do || node->kind == Node::Label) {
        if (node->kind == Node::Block) {
            for (size_t i = 0; i + 1 < node->kids.size(); ++i) {
                Node* cur = statement_value(&node->kids[i]);
                Node* nxt = statement_value(&node->kids[i + 1]);
                if (cur->kind == Node::Assign && nxt->kind == Node::Assign && cur->kids.size() == 2 && nxt->kids.size() == 2 &&
                    cur->kids[0].kind == Node::Name && nxt->kids[0].kind == Node::Name && cur->kids[0].text == nxt->kids[0].text &&
                    !expr_uses(nxt->kids[1], cur->kids[0].text)) {
                    node->kids[i].kind = Node::Block;
                    node->kids[i].kids.clear();
                }
            }
        }
        for (Node& kid : node->kids) prop_node(&kid, known, shadowed);
        return;
    }
    if (node->kind == Node::Assign && !node->kids.empty() && node->kids[0].kind == Node::Name &&
        (node->op == TAssign || node->op == TEnd)) {
        prop_node(&node->kids[1], known, shadowed);
        if (shadowed != nullptr && shadowed->count(node->kids[0].text) != 0) {
            known->erase(node->kids[0].text);
            return;
        }
        if (node->kids[1].kind == Node::IntLit && node->kids[1].type.base < 5) (*known)[node->kids[0].text] = node->kids[1].number;
        else known->erase(node->kids[0].text);
        return;
    }
    if (node->kind == Node::Name && known->count(node->text) != 0 &&
        (shadowed == nullptr || shadowed->count(node->text) == 0)) {
        const uint64_t value = (*known)[node->text];
        *node = Node{};
        node->kind = Node::IntLit;
        node->number = value;
        node->type.base = 1;
        return;
    }
    for (Node& kid : node->kids) prop_node(&kid, known, shadowed);
    if (node->kind == Node::Binary && node->kids.size() == 2 && node->kids[0].kind == Node::IntLit &&
        node->kids[1].kind == Node::IntLit && node->kids[0].type.base < 5 && node->kids[1].type.base < 5) {
        const uint64_t left = node->kids[0].number;
        const uint64_t right = node->kids[1].number;
        uint64_t value = 0;
        bool folded = true;
        if (node->op == TPlus) value = left + right;
        else if (node->op == TMinus) value = left - right;
        else if (node->op == TStar) value = left * right;
        else folded = false;
        if (folded) {
            *node = Node{};
            node->kind = Node::IntLit;
            node->number = value;
            node->type.base = 1;
        }
    }
}

bool emit_functions(std::vector<FuncDecl>& funcs, const std::vector<StructDef>& structs,
                    const std::vector<Global>& globals, const std::vector<FuncType>& func_types, bool optimize,
                    int opt_level, std::string* error, CompileOutput* out) {
    Gen gen(funcs);
    gen.structs = &structs;
    gen.globals = &globals;
    gen.func_types = &func_types;
    gen.optimize = optimize;
    struct PlacedFn {
        std::string name;
        uint32_t start = 0;
        uint32_t size = 0;
        bool exported = false;
        bool is_static = false;
    };
    std::vector<PlacedFn> placed;
    std::vector<uint32_t> unwind_offs;
    for (FuncDecl& fn : funcs) {
        if (!fn.defined) continue;
        std::map<std::string, int> name_count;
        for (const Local& local : fn.locals) ++name_count[local.name];
        std::set<std::string> shadowed;
        for (const auto& item : name_count)
            if (item.second > 1) shadowed.insert(item.first);
        if (opt_level >= 2) {
            std::map<std::string, uint64_t> known;
            prop_node(&fn.body, &known, &shadowed);
        }
        int max_args = count_args(fn.body);
        const int outgoing = max_args > 0 ? 32 + 8 * std::max(0, max_args - 4) : 0;
        const int local_end = fn.locals.empty() ? 0 : -fn.locals.back().offset;
        int frame = local_end + 64 + outgoing;
        frame = (frame + 15) & ~15;
        if (frame < 0x20) frame = 0x20;
        bool needs_nv = false;
        for (const Local& local : fn.locals) {
            if (local.type.base == 3 && local.type.pointers == 0 && !gen.fits_register(local.type)) needs_nv = true;
        }
        std::vector<int> extra_regs;
        if ((optimize || opt_level >= 1) && !needs_nv) {
            const int pool[] = {3, 6, 7};
            int used = 0;
            for (Local& local : fn.locals) {
                if (used >= 3) break;
                if (local.is_static || local.name.empty() || local.name[0] == '_') continue;
                if (shadowed.count(local.name) != 0) continue;
                if (local.type.is_array() || local.type.pointers != 0 || local.type.base == 3 || local.type.base == 5 ||
                    local.type.base == 6)
                    continue;
                if (address_taken(fn.body, local.name)) continue;
                local.reg = pool[used++];
                extra_regs.push_back(local.reg);
            }
        }
        if (needs_nv) extra_regs = {3, 6, 7};
        if (((static_cast<int>(extra_regs.size()) + 1) % 2) == 0) frame += 8;
        gen.save_nv = !extra_regs.empty();
        gen.extra_pops.clear();
        gen.locals = fn.locals;
        gen.temp_base = -(((local_end + 7) & ~7) + 8);
        gen.frame = frame;
        const uint32_t start = static_cast<uint32_t>(gen.text.size());
        uint8_t cursor = 0;
        auto mark = [&](uint8_t bytes) { cursor = static_cast<uint8_t>(cursor + bytes); return cursor; };
        std::vector<UnwindPush> pushes;
        for (int reg : extra_regs) {
            const uint8_t push_op = reg == 3 ? 0x53 : reg == 6 ? 0x56 : 0x57;
            const uint8_t pop_op = reg == 3 ? 0x5B : reg == 6 ? 0x5E : 0x5F;
            gen.b(push_op);
            pushes.push_back({static_cast<uint8_t>(reg), mark(1)});
            gen.extra_pops.insert(gen.extra_pops.begin(), pop_op);
        }
        gen.b(0x55);
        pushes.push_back({5, mark(1)});
        gen.b(0x48);
        gen.b(0x89);
        gen.b(0xE5);
        const uint8_t frame_at = mark(3);
        const bool large_alloc = frame > 127;
        if (large_alloc) {
            gen.b(0x48);
            gen.b(0x81);
            gen.b(0xEC);
            gen.u32(static_cast<uint32_t>(frame));
        } else {
            gen.b(0x48);
            gen.b(0x83);
            gen.b(0xEC);
            gen.b(static_cast<uint8_t>(frame));
        }
        if (fn.sret) {
            gen.b(0x48);
            gen.b(0x89);
            gen.b(0x8D);
            gen.disp(fn.sret_off);
        }
        if (fn.vararg) {
            gen.b(0x48);
            gen.b(0x89);
            gen.b(0x4D);
            gen.b(0x10);
            gen.b(0x48);
            gen.b(0x89);
            gen.b(0x55);
            gen.b(0x18);
            gen.b(0x4C);
            gen.b(0x89);
            gen.b(0x45);
            gen.b(0x20);
            gen.b(0x4C);
            gen.b(0x89);
            gen.b(0x4D);
            gen.b(0x28);
        }
        gen.scope_depth = 0;
        gen.scope_parent.clear();
        gen.scope_parent.push_back(0);
        gen.current = &fn;
        for (int p = 0; p < fn.nparams; ++p) {
            const int reg = p + (fn.sret ? 1 : 0);
            if (reg >= 4) continue;
            const Local& local = fn.locals[static_cast<size_t>(fn.param_base + p)];
            const Type& param = local.type;
            const int param_size = gen.size_of(param);
            const bool big = param.base == 3 && param.pointers == 0 && !gen.fits_register(param);
            const bool fp = param.pointers == 0 && (param.base == 5 || param.base == 6);
            const bool wide = param.pointer() || param_size == 8;
            if (fp) {
                const uint8_t modrm = reg == 0 ? 0x85 : reg == 1 ? 0x8D : reg == 2 ? 0x95 : 0x9D;
                gen.b(param.base == 6 ? 0xF2 : 0xF3);
                gen.b(0x0F);
                gen.b(0x11);
                gen.b(modrm);
                gen.disp(local.offset);
                continue;
            }
            if (big) {
                if (reg == 0) { gen.b(0x48); gen.b(0x89); gen.b(0xCB); }
                else if (reg == 1) { gen.b(0x48); gen.b(0x89); gen.b(0xD3); }
                else if (reg == 2) { gen.b(0x4C); gen.b(0x89); gen.b(0xC3); }
                else { gen.b(0x4C); gen.b(0x89); gen.b(0xCB); }
                gen.b(0x48); gen.b(0x89); gen.b(0xDE);
                gen.b(0x48); gen.b(0x8D); gen.b(0xBD); gen.disp(local.offset);
                gen.b(0xB9); gen.u32(static_cast<uint32_t>(param_size));
                gen.b(0xF3); gen.b(0xA4);
            } else if (param_size == 1) {
                const uint8_t rex = reg >= 2 ? 0x44 : 0;
                const uint8_t modrm = reg == 0 || reg == 3 ? 0x8D : (reg == 1 ? 0x95 : 0x85);
                if (rex) gen.b(rex);
                gen.b(0x88);
                gen.b(modrm);
                gen.disp(local.offset);
            } else if (param_size == 2) {
                const uint8_t rex = reg >= 2 ? 0x44 : 0;
                const uint8_t modrm = reg == 0 || reg == 3 ? 0x8D : (reg == 1 ? 0x95 : 0x85);
                gen.b(0x66);
                if (rex) gen.b(rex);
                gen.b(0x89);
                gen.b(modrm);
                gen.disp(local.offset);
            } else {
                const uint8_t rex = reg >= 2 ? (wide ? 0x4C : 0x44) : (wide ? 0x48 : 0);
                const uint8_t modrm = reg == 0 || reg == 3 ? 0x8D : (reg == 1 ? 0x95 : 0x85);
                if (rex) gen.b(rex);
                gen.b(0x89);
                gen.b(modrm);
                gen.disp(local.offset);
            }
        }
        for (int p = 0; p < fn.nparams; ++p) {
            const Local& local = fn.locals[static_cast<size_t>(fn.param_base + p)];
            if (local.reg == 0) continue;
            const int src = p + (fn.sret ? 1 : 0);
            if (src >= 4) continue;
            const uint8_t modrm = local.reg == 3 ? (src == 0 ? 0xCB : src == 1 ? 0xD3 : src == 2 ? 0xC3 : 0xCB)
                                                 : local.reg == 6 ? (src == 0 ? 0xCE : src == 1 ? 0xD6 : 0xC6)
                                                                  : (src == 0 ? 0xCF : src == 1 ? 0xD7 : 0xC7);
            if (src >= 2) gen.b(0x44);
            gen.b(0x89);
            gen.b(modrm);
        }
        if (!gen.gen_stmt(fn.body)) {
            *error = gen.error.empty() ? "codegen failed" : gen.error;
            return false;
        }
        gen.epilogue();
        placed.push_back({fn.name, start, static_cast<uint32_t>(gen.text.size()) - start, fn.exported, fn.is_static});
        for (const auto& site : gen.gotos) {
            const auto found = gen.labels.find(site.first);
            if (found == gen.labels.end()) {
                *error = "unknown label " + site.first;
                return false;
            }
            gen.patch(site.second, found->second);
        }
        gen.labels.clear();
        gen.gotos.clear();
        if (fn.exported) out->exports.push_back(fn.name);
        UnwindPrologue prolog;
        prolog.size = static_cast<uint8_t>(frame_at + (large_alloc ? 7 : 4));
        prolog.frame_register = 5;
        prolog.frame_at = frame_at;
        prolog.alloc = static_cast<uint32_t>(frame);
        prolog.alloc_at = prolog.size;
        prolog.pushes = pushes;
        const auto unwind = encode_unwind(prolog);
        if (!unwind.ok()) {
            *error = unwind.error;
            return false;
        }
        const uint32_t xat = static_cast<uint32_t>(gen.xdata.size());
        unwind_offs.push_back(xat);
        gen.xdata.insert(gen.xdata.end(), unwind.value.begin(), unwind.value.end());
        const std::string unw = "unwind_" + fn.name;
        // pdata: begin, end (addend = size), unwind info.
        const uint32_t pat = static_cast<uint32_t>(gen.pdata.size());
        for (int i = 0; i < 3; ++i) gen.pdata.push_back(0), gen.pdata.push_back(0), gen.pdata.push_back(0), gen.pdata.push_back(0);
        gen.pdata[pat + 4] = static_cast<uint8_t>(placed.back().size);
        gen.pdata[pat + 5] = static_cast<uint8_t>(placed.back().size >> 8);
        gen.pdata[pat + 6] = static_cast<uint8_t>(placed.back().size >> 16);
        gen.pdata[pat + 7] = static_cast<uint8_t>(placed.back().size >> 24);
        gen.relocs.push_back({2, pat, fn.name, kRelAmd64Addr32Nb});
        gen.relocs.push_back({2, pat + 4, fn.name, kRelAmd64Addr32Nb});
        gen.relocs.push_back({2, pat + 8, unw, kRelAmd64Addr32Nb});
        (void)xat;
        (void)unw;
    }
    if (!gen.error.empty()) {
        *error = gen.error;
        return false;
    }

    std::vector<Sym> symbols;
    auto add_sym = [&](const std::string& name, uint32_t value, int16_t section, uint8_t storage, uint16_t type) {
        symbols.push_back({name, value, section, type, storage});
    };
    for (const PlacedFn& fn : placed)
        add_sym(fn.name, fn.start, 1, fn.is_static ? kClassStatic : kClassExternal, 0x20);
    for (const FuncDecl& fn : funcs) {
        if (fn.defined) continue;
        if (fn.imported) add_sym("__imp_" + fn.name, 0, 0, kClassExternal, 0);
        else add_sym(fn.name, 0, 0, kClassExternal, 0);
    }
    std::vector<uint8_t> data_bytes;
    uint32_t bss_size = 0;
    auto align_to = [](uint32_t value, uint32_t align) {
        if (align == 0) return value;
        return (value + align - 1u) & ~(align - 1u);
    };
    int next_section = 2;
    for (const Global& global : globals) {
        const int size = std::max(1, gen.size_of(global.type));
        const int align = global.type.pointer() ? 8 : 4;
        if (global.bss) {
            bss_size = align_to(bss_size, static_cast<uint32_t>(align));
            add_sym(global.name, bss_size, 0, global.is_static ? kClassStatic : kClassExternal, 0);
            bss_size += static_cast<uint32_t>(size);
        } else {
            const uint32_t at = align_to(static_cast<uint32_t>(data_bytes.size()), static_cast<uint32_t>(align));
            data_bytes.resize(at, 0);
            const uint32_t value = global.init;
            data_bytes.push_back(static_cast<uint8_t>(value));
            data_bytes.push_back(static_cast<uint8_t>(value >> 8));
            data_bytes.push_back(static_cast<uint8_t>(value >> 16));
            data_bytes.push_back(static_cast<uint8_t>(value >> 24));
            if (size > 4) data_bytes.resize(at + static_cast<uint32_t>(size), 0);
            add_sym(global.name, at, 0, global.is_static ? kClassStatic : kClassExternal, 0);
        }
    }
    int data_section = 0;
    int bss_section = 0;
    int rdata_section = 0;
    int xdata_section = 0;
    int pdata_section = 0;
    if (!data_bytes.empty()) data_section = next_section++;
    if (bss_size != 0) bss_section = next_section++;
    if (!gen.rdata.empty()) rdata_section = next_section++;
    if (!gen.xdata.empty()) xdata_section = next_section++;
    if (!gen.pdata.empty()) pdata_section = next_section++;
    for (Sym& sym : symbols) {
        for (const Global& global : globals) {
            if (sym.name != global.name) continue;
            sym.section = static_cast<int16_t>(global.bss ? bss_section : data_section);
        }
    }
    for (const auto& str : gen.string_at) add_sym(str.first, str.second, static_cast<int16_t>(rdata_section), kClassStatic, 0);
    for (size_t i = 0; i < placed.size(); ++i) {
        add_sym("unwind_" + placed[i].name, unwind_offs[i], static_cast<int16_t>(xdata_section), kClassStatic, 0);
    }

    std::map<std::string, uint32_t> index;
    for (size_t i = 0; i < symbols.size(); ++i) index[symbols[i].name] = static_cast<uint32_t>(i);

    ObjectFile object;
    Section text;
    text.name = ".text";
    text.virtual_size = static_cast<uint32_t>(gen.text.size());
    text.data = gen.text;
    text.characteristics = kScnCntCode | kScnMemExecute | kScnMemRead | kScnAlign16;
    object.sections.push_back(std::move(text));
    auto add_data = [&](const char* name, std::vector<uint8_t> data) {
        if (data.empty()) return;
        Section section;
        section.name = name;
        section.virtual_size = static_cast<uint32_t>(data.size());
        section.data = std::move(data);
        section.characteristics = kScnCntInitData | kScnMemRead;
        object.sections.push_back(std::move(section));
    };
    if (!data_bytes.empty()) {
        Section data;
        data.name = ".data";
        data.virtual_size = static_cast<uint32_t>(data_bytes.size());
        data.data = data_bytes;
        data.characteristics = kScnCntInitData | kScnMemRead | kScnMemWrite;
        object.sections.push_back(std::move(data));
    }
    if (bss_size != 0) {
        Section bss;
        bss.name = ".bss";
        bss.virtual_size = bss_size;
        bss.characteristics = kScnCntUninitData | kScnMemRead | kScnMemWrite;
        object.sections.push_back(std::move(bss));
    }
    add_data(".rdata", gen.rdata);
    add_data(".xdata", gen.xdata);
    add_data(".pdata", gen.pdata);
    for (const Gen::NamedReloc& named : gen.relocs) {
        // section 0 text, 1 rdata (unused), 2 pdata in the Gen numbering.
        int sec = 0;
        if (named.section == 2) {
            sec = pdata_section - 1;
        }
        const auto found = index.find(named.symbol);
        if (found == index.end()) {
            *error = "missing symbol " + named.symbol;
            return false;
        }
        Relocation reloc;
        reloc.offset = named.offset;
        reloc.symbol = found->second;
        reloc.type = named.type;
        object.sections[sec].relocations.push_back(reloc);
    }
    for (const Sym& sym : symbols) {
        Symbol out_sym;
        out_sym.name = sym.name;
        out_sym.value = sym.value;
        out_sym.section = sym.section;
        out_sym.type = sym.type;
        out_sym.storage = sym.storage;
        object.symbols.push_back(std::move(out_sym));
    }
    out->object = std::move(object);
    (void)xdata_section;
    return true;
}

}  // namespace

Result<CompileOutput> compile_c(const CompileInput& input) {
    Preprocessor pp;
    pp.includes = input.include_paths;
    for (const std::string& def : input.defines) {
        const size_t eq = def.find('=');
        Macro macro;
        if (eq == std::string::npos) macro.body = "1";
        else macro.body = def.substr(eq + 1);
        if (eq == std::string::npos) pp.macros[def] = macro;
        else pp.macros[def.substr(0, eq)] = macro;
    }
    const std::string stripped = strip_comments(input.source);
    const std::string expanded = pp.run(stripped, "");
    if (!pp.error.empty()) return Result<CompileOutput>::fail(pp.error);
    std::string lex_error;
    const std::vector<Token> tokens = lex(expanded, &lex_error);
    if (!lex_error.empty()) return Result<CompileOutput>::fail(lex_error);
    Parser parser{tokens, 0, {}, {}, {}, {}, {}, {}, {}, {}, nullptr, {}};
    if (!parser.parse()) return Result<CompileOutput>::fail(parser.error.empty() ? "parse failed" : parser.error);
    CompileOutput output;
    std::string error;
    if (!emit_functions(parser.funcs, parser.structs, parser.globals, parser.func_types, input.optimize, input.opt_level,
                        &error, &output))
        return Result<CompileOutput>::fail(error);
    return Result<CompileOutput>::success(std::move(output));
}

}  // namespace okayms
