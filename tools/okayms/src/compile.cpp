#include "okayms/compile.hpp"
#include "okayms/unwind.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <functional>
#include <map>
#include <set>

namespace okayms {
namespace {

enum Tok {
    TEnd, TIdent, TNumber, TString,
    TLParen, TRParen, TLBrace, TRBrace, TSemi, TComma, TStar, TAmp,
    TPlus, TMinus, TSlash, TPercent, TAssign, TEq, TNe, TLt, TGt, TLe, TGe,
    TPlusPlus, TMinusMinus, TPlusEq, TMinusEq, TStarEq, TSlashEq, TPercentEq, TQuestion,
    TAndAnd, TOrOr, TNot, TLBrack, TRBrack, TDot, TArrow, TEllipsis, TColon, TColonColon,
    TInt, TVoid, TChar, TIf, TElse, TWhile, TFor, TDo, TSwitch, TCase, TDefault, TBreak, TContinue, TReturn,
    TStruct, TUnion, TEnum, TTypedef, TUnsigned, TSigned, TLong, TShort, TDeclspec,
    TSizeof, TConst, TStatic, TGoto, TFloat, TDouble, TNamespace, TTilde,
    // Bit operators. A '>>' is two adjacent TGt tokens (so A<B<int>> still closes twice) and the parser
    // joins them into TShr / TShrEq where an operator is expected.
    TPipe, TCaret, TShl, TShr, TAndEq, TOrEq, TXorEq, TShlEq, TShrEq
};

struct Token {
    Tok kind = TEnd;
    std::string text;
    uint64_t number = 0;
    // 0 for an integer, 5 for a float literal, 6 for a double literal.
    int real = 0;
    // Line of the preprocessed text, counted from 1.
    int line = 0;
    // A '>' written directly after another '>' (no space), used to read '>>' and '>>='.
    bool adj = false;
};

struct Type {
    int pointers = 0;
    // 0 void, 1 int, 2 char, 3 struct or union, 4 short
    int base = 1;
    int array_len = -1;
    // Dimensions after the first. int a[2][3][4][5] stores 3, 4, 5 here.
    std::vector<int> more;
    int struct_id = -1;
    int func_id = -1;
    // A class template argument pattern while a template's parameter types are read (index into
    // the pattern table), or -2 for a name that cannot be deduced (typename T::type).
    int tpat = -1;
    int bits = -1;
    int bit_off = 0;
    bool is_unsigned = false;
    bool is_const = false;
    bool is_ref = false;
    // bool: one byte holding 0 or 1 (base 2, unsigned). A conversion to it compares with zero.
    bool is_bool = false;
    // A function parameter pack (Rest... rest) while a template's parameter types are read.
    bool pack = false;
    bool pointer() const { return pointers > 0; }
    bool is_array() const { return array_len >= 0; }
    bool is_func() const { return func_id >= 0; }
};

// A template argument: a type or a value. In a pattern (a specialization or a function template's
// parameter type) a value that is a template parameter has marker set to the parameter's index.
struct TArg {
    bool is_value = false;
    long long value = 0;
    Type type{};
    int marker = -1;
    // A parameter pack (typename... Ts): the types it stands for ([temp.variadic]).
    bool is_pack = false;
    std::vector<Type> pack;
    // In a specialization's pattern, a pack argument ending in `Rest...`: the index of the
    // specialization's own pack parameter Rest, which takes the arguments left over; else -1.
    int rest = -1;
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

// A member function. The first parameter of the compiled function is an
// implicit pointer to the object, named "this".
struct Method {
    std::string name;
    // Internal function name ("C::f/1") and the MSVC decorated symbol.
    std::string qname;
    std::string symbol;
    Type ret{};
    std::vector<Type> params;
    std::vector<std::string> param_names;
    bool is_virtual = false;
    bool pure = false;
    bool is_const = false;
    bool has_body = false;
    int slot = -1;
    // 'A' private, 'I' protected, 'Q' public: the MSVC access letters.
    char access = 'Q';
    // The class that declared the function.
    int owner = -1;
    // Token index of the body's '{' (or of the ':' that starts a constructor's
    // initializer list) until the body has been compiled.
    size_t body_at = 0;
    // Template parameter names of an out-of-class definition (template <typename U> U S<U>::f()).
    std::vector<std::string> ool_names;
    // A constructor or destructor the compiler made because a member or base
    // needs one. It has no tokens.
    bool implicit = false;
    // The implicit copy constructor: copies the base and each member.
    bool implicit_copy = false;
    // A static member function: no this, decorated with S/C/K instead of the Q/A/I letters.
    bool is_static = false;
    // The decorated spelling of a member function template instance's name ("$add@H"), else empty.
    std::string tsym;
};

// A static data member, or a constant (static const integral, an enumerator) of a class.
struct StaticMem {
    Type type{};
    std::string symbol;  // the global that holds it (not for a constant)
    bool has_const = false;
    long long cval = 0;
};

struct StructDef {
    std::string name;
    // The name in decorated symbols when it differs from name (a class template instance Box<H>
    // is ?$Box@H there).
    std::string sym_name;
    const std::string& symname() const { return sym_name.empty() ? name : sym_name; }
    std::vector<Field> fields;
    int size = 0;
    int align = 1;
    bool is_union = false;
    bool is_class = false;
    // True for a class, or any struct with a base class or member functions.
    bool cxx = false;
    int base = -1;
    size_t base_fields = 0;
    bool poly = false;
    std::vector<Method> methods;
    // Symbol of the function in each virtual slot. Empty for a pure virtual.
    std::vector<std::string> vtable;
    std::string vtable_sym;
    // typedef and using names declared inside the class (typename C::name).
    std::map<std::string, Type> tdefs;
    // Static data members and class-scope constants, by name (ISO/IEC 14882 [class.static.data]).
    std::map<std::string, StaticMem> statics;
    // The class template and the arguments of an instance, for deduction through Node<T>.
    std::string tmpl;
    std::vector<TArg> targs;
};

struct Global {
    std::string name;
    Type type{};
    bool bss = true;
    bool is_static = false;
    uint32_t init = 0;
    std::vector<uint8_t> init_bytes;
    // A virtual function table: read-only data, one ADDR64 slot per entry.
    bool vtable = false;
    std::vector<std::string> slots;
};

Type peel_array(const Type& type) {
    Type elem = type;
    if (elem.array_len >= 0) {
        if (elem.more.empty()) elem.array_len = -1;
        else {
            elem.array_len = elem.more.front();
            elem.more.erase(elem.more.begin());
        }
        return elem;
    }
    if (elem.pointers > 0) --elem.pointers;
    return elem;
}

struct Node {
    enum Kind {
        Block, Return, If, While, For, Do, Switch, Case, Break, Continue, ExprStmt,
        IntLit, StringLit, Name, Unary, Binary, Assign, Call, Index, Field,
        Ternary, Goto, Label
    } kind = Block;
    std::string text;
    uint64_t number = 0;
    Tok op = TEnd;
    Type type{};
    int32_t offset = 0;
    bool imported = false;
    // Line of the preprocessed text where a statement starts. 0 is unknown.
    int line = 0;
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
    std::string symbol;
    int func_id = -1;
    int sret_off = 0;
    int param_base = 0;
    Node body;
    std::vector<Local> locals;
    // Which parameters are references. In params they are pointers (that is what is passed);
    // the body sees them as references again.
    std::vector<bool> param_ref;
    // The function returns a reference: ret is then a pointer and a call is dereferenced.
    bool ret_ref = false;
    int nparams = 0;
    int max_args = 0;
    // First and last line of the body (the braces), for the line table.
    int line = 0;
    int end_line = 0;
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
            // Keep the newlines so a line number still names its source line.
            while (i + 1 < in.size() && !(in[i] == '*' && in[i + 1] == '/')) {
                if (in[i] == '\n') out.push_back('\n');
                ++i;
            }
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
    // File names (index 0 is the main source) and, for each line of the
    // output, the file and line it came from.
    std::vector<std::string> files;
    std::vector<std::pair<int, int>> origin;

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
                    (f == '<' && n == '=') || (f == '>' && n == '=') || (f == '<' && n == '<') || (f == '>' && n == '>')) {
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
                if (cur().kind == 3 && (cur().text == "!" || cur().text == "-" || cur().text == "+" || cur().text == "~")) {
                    const std::string op = cur().text;
                    next();
                    const int64_t value = primary();
                    if (op == "!") return value == 0 ? 1 : 0;
                    if (op == "~") return ~value;
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
            int64_t shift() {
                int64_t left = add();
                while (cur().kind == 3 && (cur().text == "<<" || cur().text == ">>")) {
                    const std::string op = cur().text;
                    next();
                    const int64_t right = add();
                    if (right < 0 || right > 63) {
                        *ok = false;
                        return 0;
                    }
                    if (op == "<<") left = static_cast<int64_t>(static_cast<uint64_t>(left) << right);
                    else left = left >> right;
                }
                return left;
            }
            int64_t rel() {
                int64_t left = shift();
                while (cur().kind == 3 && (cur().text == "<" || cur().text == ">" || cur().text == "<=" || cur().text == ">=")) {
                    const std::string op = cur().text;
                    next();
                    const int64_t right = shift();
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
            int64_t band() {
                int64_t left = eq();
                while (cur().kind == 3 && cur().text == "&") {
                    next();
                    left &= eq();
                }
                return left;
            }
            int64_t bxor() {
                int64_t left = band();
                while (cur().kind == 3 && cur().text == "^") {
                    next();
                    left ^= band();
                }
                return left;
            }
            int64_t bor() {
                int64_t left = bxor();
                while (cur().kind == 3 && cur().text == "|") {
                    next();
                    left |= bxor();
                }
                return left;
            }
            int64_t and_() {
                int64_t left = bor();
                while (cur().kind == 3 && cur().text == "&&") {
                    next();
                    const int64_t right = bor();
                    left = (left != 0 && right != 0) ? 1 : 0;
                }
                return left;
            }
            int64_t lor() {
                int64_t left = and_();
                while (cur().kind == 3 && cur().text == "||") {
                    next();
                    const int64_t right = and_();
                    left = (left != 0 || right != 0) ? 1 : 0;
                }
                return left;
            }
            int64_t expr() {
                const int64_t cond = lor();
                if (cur().kind == 3 && cur().text == "?") {
                    next();
                    const int64_t yes = expr();
                    if (cur().kind != 3 || cur().text != ":") {
                        *ok = false;
                        return 0;
                    }
                    next();
                    const int64_t no = expr();
                    return cond != 0 ? yes : no;
                }
                return cond;
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

    // Files named by '#pragma once' (by the path they were found at).
    std::set<std::string> once_files;
    // The path of each file being read, innermost last (the main source has none).
    std::vector<std::string> path_stack;

    // Quoted names are looked up next to the including file first ([cpp.include]), then in the
    // include paths. *found is the path the text was read from.
    std::string find_include(const std::string& name, bool quote, const std::string& dir, std::string* found) {
        if (quote && !dir.empty()) {
            const std::string path = dir + "/" + name;
            std::string text;
            if (read_file(path, &text)) {
                *found = path;
                return text;
            }
        }
        for (const std::string& inc : includes) {
            const std::string path = inc + "/" + name;
            std::string text;
            if (read_file(path, &text)) {
                *found = path;
                return text;
            }
        }
        return "";
    }

    // A backslash directly before a newline joins the two lines ([lex.phases] 2). The joined line is
    // followed by one empty line for each line it took in, so the line count stays the same.
    static std::string splice_lines(const std::string& in) {
        if (in.find('\\') == std::string::npos) return in;
        std::string out;
        size_t i = 0;
        int pending = 0;
        while (i < in.size()) {
            if (in[i] == '\\' && i + 1 < in.size() && in[i + 1] == '\n') {
                i += 2;
                ++pending;
                continue;
            }
            if (in[i] == '\\' && i + 2 < in.size() && in[i + 1] == '\r' && in[i + 2] == '\n') {
                i += 3;
                ++pending;
                continue;
            }
            out.push_back(in[i]);
            if (in[i] == '\n') {
                out.append(static_cast<size_t>(pending), '\n');
                pending = 0;
            }
            ++i;
        }
        out.append(static_cast<size_t>(pending), '\n');
        return out;
    }

    std::string run(const std::string& raw_text, const std::string& dir, int file = 0) {
        const std::string text = splice_lines(raw_text);
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
        int ln = 0;
        while (i < text.size()) {
            ++ln;
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
                // Every output line has an origin. A directive leaves a blank
                // line, except an include, which is replaced by the file's lines.
                if (!(word("include") && active())) {
                    out += "\n";
                    origin.push_back({file, ln});
                }
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
                    std::string found;
                    const std::string body = find_include(name, quote, dir, &found);
                    if (body.empty() && found.empty()) {
                        error = "cannot open include " + name;
                        --depth;
                        return "";
                    }
                    if (once_files.count(found) > 0) continue;
                    files.push_back(name);
                    const size_t slash = found.rfind('/');
                    path_stack.push_back(found);
                    out += run(strip_comments(body), slash == std::string::npos ? std::string() : found.substr(0, slash),
                               static_cast<int>(files.size()) - 1);
                    path_stack.pop_back();
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
                } else if (word("undef")) {
                    if (!active()) continue;
                    size_t b = 5;
                    while (b < rest.size() && (rest[b] == ' ' || rest[b] == '\t')) ++b;
                    size_t n = b;
                    while (n < rest.size() && (std::isalnum(static_cast<unsigned char>(rest[n])) || rest[n] == '_')) ++n;
                    macros.erase(rest.substr(b, n - b));
                } else if (word("error")) {
                    if (!active()) continue;
                    error = "#error" + rest.substr(5);
                    --depth;
                    return "";
                } else if (word("pragma")) {
                    if (!active()) continue;
                    size_t b = 6;
                    while (b < rest.size() && (rest[b] == ' ' || rest[b] == '\t')) ++b;
                    const std::string what = rest.substr(b);
                    if (what.compare(0, 4, "once") == 0) {
                        if (!path_stack.empty()) once_files.insert(path_stack.back());
                    } else if (what.compare(0, 4, "pack") == 0) {
                        // Ignoring it would change the layout of a struct without a word.
                        error = "#pragma pack is not supported";
                        --depth;
                        return "";
                    }
                    // Any other pragma (warning, comment, ...) has no effect on the code made here.
                } else if (rest.empty() || word("line")) {
                    // A null directive and #line change nothing.
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
                    error = "unknown preprocessor directive #" + rest.substr(0, rest.find_first_of(" \t("));
                    --depth;
                    return "";
                }
                continue;
            }
            out += active() ? expand(line, 0) + "\n" : std::string("\n");
            origin.push_back({file, ln});
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

// MSVC decorated names, from the public decoration rules: '?' name '@'
// namespace '@@YA' return-type argument-types '@Z'. H is int, X is void,
// A in front of a type is a reference. Written here, not copied.
std::string cxx_type(const Type& type, const std::vector<StructDef>* structs = nullptr) {
    if (type.is_ref) {
        // On x64 a reference is "AEA" (the E is the 64-bit pointer mark).
        Type inner = type;
        inner.is_ref = false;
        return "AEA" + cxx_type(inner, structs);
    }
    if (type.pointers > 0) {
        Type inner = type;
        --inner.pointers;
        return "PEA" + cxx_type(inner, structs);
    }
    if (type.base == 0) return "X";
    if (type.base == 2) return type.is_bool ? "_N" : type.is_unsigned ? "E" : "D";
    if (type.base == 4) return type.is_unsigned ? "G" : "F";
    if (type.base == 5) return "M";
    if (type.base == 6) return "N";
    if (type.base == 3 && structs != nullptr && type.struct_id >= 0 &&
        type.struct_id < static_cast<int>(structs->size())) {
        const StructDef& def = (*structs)[static_cast<size_t>(type.struct_id)];
        // U is struct, T is union, V is class; then the name and "@@".
        return std::string(def.is_union ? "T" : def.is_class ? "V" : "U") + def.symname() + "@@";
    }
    return type.is_unsigned ? "I" : "H";
}

// ?name@Class@@ access-letter this-qualifiers calling-convention return
// parameters @Z. The access letter is Q/A/I for public/private/protected
// and U/E/M for the same three when the function is virtual. "EAA" is a
// 64-bit non-const this pointer and the one calling convention; "EBA" is a
// const this pointer.
std::string cxx_method_symbol(const std::vector<StructDef>& structs, const StructDef& owner, const Method& m) {
    char letter = m.access;
    if (m.is_virtual) letter = m.access == 'A' ? 'E' : m.access == 'I' ? 'M' : 'U';
    if (m.is_static) {
        // ?g@P@@SAHH@Z: S, C or K for a public, private or protected static member, then the
        // calling convention, with no this qualifier.
        std::string st = "?" + (m.tsym.empty() ? m.name : m.tsym) + "@" + owner.symname() + "@@";
        st += m.access == 'A' ? 'C' : m.access == 'I' ? 'K' : 'S';
        st += "A";
        if (m.ret.base == 3 && m.ret.pointers == 0 && !m.ret.is_ref) st += "?A";
        st += cxx_type(m.ret, &structs);
        if (m.params.empty()) st += "X";
        else {
            for (const Type& param : m.params) st += cxx_type(param, &structs);
            st += "@";
        }
        return st + "Z";
    }
    if (m.name == "<ctor>" || m.name == "<dtor>") {
        // ??0 is a constructor and ??1 a destructor. There is no return type: '@' stands where it
        // would be.
        // A constructor template instance is ??$?0H@S@@...: the template arguments follow ??$?0.
        std::string special = (m.name == "<ctor>" && !m.tsym.empty() ? "?" + m.tsym + "@" :
                               std::string(m.name == "<ctor>" ? "??0" : "??1")) + owner.symname() + "@@";
        special += letter;
        special += "EAA@";
        if (m.params.empty()) special += "X";
        else {
            for (const Type& param : m.params) special += cxx_type(param, &structs);
            special += "@";
        }
        return special + "Z";
    }
    std::string out = "?" + (m.tsym.empty() ? m.name : m.tsym) + "@" + owner.symname() + "@@";
    out += letter;
    out += m.is_const ? "EBA" : "EAA";
    if (m.ret.base == 3 && m.ret.pointers == 0 && !m.ret.is_ref) out += "?A";
    out += cxx_type(m.ret, &structs);
    if (m.params.empty()) out += "X";
    else {
        for (const Type& param : m.params) out += cxx_type(param, &structs);
        out += "@";
    }
    out += "Z";
    return out;
}

// The scalar deleting destructor, ??_G: the function a virtual destructor's table slot holds. It
// takes this and a flag word, runs the destructor, frees the memory when the flag is non-zero and
// returns this. Written from the public MSVC decoration description.
std::string sdd_symbol(const StructDef& owner, const Method& m) {
    const char letter = m.access == 'A' ? 'E' : m.access == 'I' ? 'M' : 'U';
    return "??_G" + owner.symname() + "@@" + letter + "EAAPEAXI@Z";
}

std::string cxx_symbol(const std::string& ns, const std::string& name, const Type& ret, const std::vector<Type>& params) {
    std::string out = "?" + name + "@" + ns + "@@YA" + cxx_type(ret);
    if (params.empty()) out += "X";
    else {
        for (const Type& param : params) out += cxx_type(param);
        out += "@";
    }
    out += "Z";
    return out;
}

bool is_assign_op(Tok op) {
    return op == TAssign || op == TPlusEq || op == TMinusEq || op == TStarEq || op == TSlashEq || op == TPercentEq ||
           op == TAndEq || op == TOrEq || op == TXorEq || op == TShlEq || op == TShrEq;
}
bool is_compound_op(Tok op) { return op != TAssign && is_assign_op(op); }

struct Parser {
    std::vector<Token>& tokens;
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
    // The namespaces open at the top level: "a::b" and its decorated form "b@a" (innermost first).
    std::string current_ns;
    std::string ns_mangled{};
    std::vector<std::string> ns_parts{};
    std::vector<size_t> ns_open{};  // how many names each open brace pushed (0 for an unnamed namespace)
    std::set<std::string> namespaces{};
    void update_ns() {
        current_ns.clear();
        ns_mangled.clear();
        for (size_t i = 0; i < ns_parts.size(); ++i) current_ns += (i == 0 ? "" : "::") + ns_parts[i];
        for (size_t i = ns_parts.size(); i-- > 0;) ns_mangled += (ns_mangled.empty() ? "" : "@") + ns_parts[i];
    }
    // Spellings that only restate what the compiler already does are rewritten before parsing, each from
    // its rule in ISO/IEC 14882:
    //   constexpr -> const; inline, explicit, noexcept (with its operand), override, final, [[attributes]]
    //   are dropped; static_assert(...) is dropped (it is not checked); nullptr -> 0
    //   using Name = type; -> typedef type Name;  using namespace x; is dropped
    //   enum class E : T { } and enum E : T { } -> enum E { } (the underlying type is int)
    //   Scoped enumerators are renamed "E::X" (one token), so two enums may share an enumerator name
    //   static_cast<T>(x), reinterpret_cast and const_cast -> (T)(x)
    void lower_cxx_spellings() {
        const auto is_word = [this](size_t i, const char* w) {
            return i < tokens.size() && tokens[i].kind == TIdent && tokens[i].text == w;
        };
        std::set<std::string> scoped;
        for (size_t i = 0; i + 2 < tokens.size(); ++i)
            if (tokens[i].kind == TEnum && (tokens[i + 1].kind == TStruct) && tokens[i + 2].kind == TIdent)
                scoped.insert(tokens[i + 2].text);
        std::vector<Token> out;
        out.reserve(tokens.size());
        std::string in_scoped;  // the scoped enum whose braces are open
        int enum_depth = 0;
        bool want_name = false;
        for (size_t i = 0; i < tokens.size(); ++i) {
            const Token& t = tokens[i];
            if (t.kind == TIdent && t.text == "constexpr") {
                Token c = t;
                c.kind = TConst;
                c.text = "const";
                out.push_back(c);
                continue;
            }
            if (t.kind == TIdent && (t.text == "inline" || t.text == "explicit" || t.text == "override" ||
                                      t.text == "final" || t.text == "consteval" || t.text == "constinit"))
                continue;
            if (t.kind == TIdent && t.text == "nullptr") {
                Token z = t;
                z.kind = TNumber;
                z.number = 0;
                out.push_back(z);
                continue;
            }
            if (is_word(i, "noexcept")) {
                if (i + 1 < tokens.size() && tokens[i + 1].kind == TLParen) {
                    int depth = 0;
                    ++i;
                    do {
                        if (tokens[i].kind == TLParen) ++depth;
                        else if (tokens[i].kind == TRParen) --depth;
                        ++i;
                    } while (i < tokens.size() && depth > 0);
                    --i;
                }
                continue;
            }
            if (t.kind == TLBrack && i + 1 < tokens.size() && tokens[i + 1].kind == TLBrack) {
                size_t j = i + 2;
                while (j + 1 < tokens.size() && !(tokens[j].kind == TRBrack && tokens[j + 1].kind == TRBrack)) ++j;
                if (j + 1 < tokens.size()) {
                    i = j + 1;
                    continue;
                }
            }
            if (is_word(i, "static_assert") && i + 1 < tokens.size() && tokens[i + 1].kind == TLParen) {
                int depth = 0;
                ++i;
                do {
                    if (tokens[i].kind == TLParen) ++depth;
                    else if (tokens[i].kind == TRParen) --depth;
                    ++i;
                } while (i < tokens.size() && depth > 0);
                if (i < tokens.size() && tokens[i].kind == TSemi) ++i;
                --i;
                continue;
            }
            if (is_word(i, "using") && i + 1 < tokens.size()) {
                if (is_word(i + 1, "namespace")) {
                    while (i < tokens.size() && tokens[i].kind != TSemi && tokens[i].kind != TEnd) ++i;
                    continue;
                }
                if (tokens[i + 1].kind == TIdent && i + 2 < tokens.size() && tokens[i + 2].kind == TAssign) {
                    size_t j = i + 3;
                    while (j < tokens.size() && tokens[j].kind != TSemi && tokens[j].kind != TEnd) ++j;
                    Token td = t;
                    td.kind = TTypedef;
                    td.text = "typedef";
                    out.push_back(td);
                    for (size_t k = i + 3; k < j; ++k) out.push_back(tokens[k]);
                    out.push_back(tokens[i + 1]);
                    i = j - 1;
                    continue;
                }
            }
            if (t.kind == TEnum) {
                out.push_back(t);
                size_t j = i + 1;
                const bool is_scoped = j < tokens.size() && tokens[j].kind == TStruct;
                if (is_scoped) ++j;
                if (j < tokens.size() && tokens[j].kind == TIdent) {
                    out.push_back(tokens[j]);
                    if (is_scoped) in_scoped = tokens[j].text;
                    ++j;
                }
                if (j < tokens.size() && tokens[j].kind == TColon) {  // : underlying type
                    ++j;
                    while (j < tokens.size() && tokens[j].kind != TLBrace && tokens[j].kind != TSemi && tokens[j].kind != TEnd) ++j;
                }
                if (j < tokens.size() && tokens[j].kind == TLBrace) {
                    out.push_back(tokens[j]);
                    enum_depth = is_scoped ? 1 : 0;
                    want_name = is_scoped;
                    ++j;
                } else {
                    in_scoped.clear();
                }
                i = j - 1;
                continue;
            }
            if (enum_depth > 0) {
                if (t.kind == TRBrace) {
                    enum_depth = 0;
                    in_scoped.clear();
                } else if (t.kind == TComma) {
                    want_name = true;
                } else if (want_name && t.kind == TIdent) {
                    Token n = t;
                    n.text = in_scoped + "::" + t.text;
                    out.push_back(n);
                    want_name = false;
                    continue;
                } else {
                    want_name = false;
                }
            }
            if (t.kind == TIdent && scoped.count(t.text) > 0 && i + 2 < tokens.size() && tokens[i + 1].kind == TColonColon &&
                tokens[i + 2].kind == TIdent) {
                Token n = t;
                n.text = t.text + "::" + tokens[i + 2].text;
                out.push_back(n);
                i += 2;
                continue;
            }
            if (t.kind == TIdent && (t.text == "static_cast" || t.text == "reinterpret_cast" || t.text == "const_cast") &&
                i + 1 < tokens.size() && tokens[i + 1].kind == TLt) {
                int angle = 0;
                size_t j = i + 1;
                for (; j < tokens.size() && tokens[j].kind != TEnd; ++j) {
                    if (tokens[j].kind == TLt) ++angle;
                    else if (tokens[j].kind == TGt && --angle == 0) break;
                }
                if (j + 1 < tokens.size() && tokens[j].kind == TGt && tokens[j + 1].kind == TLParen) {
                    Token open = tokens[i + 1];
                    open.kind = TLParen;
                    open.text = "(";
                    Token close = tokens[j];
                    close.kind = TRParen;
                    close.text = ")";
                    out.push_back(open);
                    for (size_t k = i + 2; k < j; ++k) out.push_back(tokens[k]);
                    out.push_back(close);
                    i = j;
                    continue;
                }
            }
            out.push_back(t);
        }
        tokens = std::move(out);
    }

    // A name written with its namespaces in front (geo::unit::kScale, std::uint8_t) is read as the
    // bare name: types, variables and enumerators of a namespace share one table. A call keeps its
    // qualification, because functions are registered with it (and decorated with it).
    void strip_namespace_qualifiers() {
        for (size_t i = 0; i + 1 < tokens.size(); ++i) {
            if (tokens[i].kind != TNamespace) continue;
            for (size_t j = i + 1; j < tokens.size() && (tokens[j].kind == TIdent || tokens[j].kind == TColonColon); ++j)
                if (tokens[j].kind == TIdent) namespaces.insert(tokens[j].text);
        }
        if (namespaces.empty()) return;
        std::vector<Token> out;
        out.reserve(tokens.size());
        for (size_t i = 0; i < tokens.size(); ++i) {
            if (tokens[i].kind == TNamespace) {
                while (i < tokens.size() && tokens[i].kind != TLBrace && tokens[i].kind != TEnd) out.push_back(tokens[i++]);
                if (i < tokens.size()) out.push_back(tokens[i]);
                continue;
            }
            if (tokens[i].kind == TIdent && namespaces.count(tokens[i].text) > 0 && i + 1 < tokens.size() &&
                tokens[i + 1].kind == TColonColon && !(i > 0 && tokens[i - 1].kind == TColonColon)) {
                size_t k = i;
                while (k + 1 < tokens.size() && tokens[k].kind == TIdent && namespaces.count(tokens[k].text) > 0 &&
                       tokens[k + 1].kind == TColonColon)
                    k += 2;
                const bool call = k + 1 < tokens.size() && tokens[k].kind == TIdent && tokens[k + 1].kind == TLParen;
                if (!call && k < tokens.size() && tokens[k].kind == TIdent) {
                    i = k - 1;  // the leading namespace names and their '::' are dropped
                    continue;
                }
            }
            out.push_back(tokens[i]);
        }
        tokens = std::move(out);
    }
    // The class whose member function body is being compiled, or -1.
    int cur_class = -1;
    int scope_depth = 0;
    std::vector<int> scope_parent{0};
    // Objects with destructors, one list per open block. A block end, return, break and
    // continue run the destructors of the blocks they leave, innermost and latest first.
    struct DObj {
        std::string name;
        int sid = -1;
        int count = 0;  // above 0: an array of that many objects
        int kind = 0;   // 0 object, 1 try frame (popped), 2 handler (ended)
    };
    // A global or static object whose destructor runs when the program ends.
    struct GlobalObj {
        std::string name;
        int sid = -1;
        int count = 0;
        std::string guard;  // a static local: destroyed only if this flag is set
    };
    std::vector<Local> init_locals{};
    Node init_body{};
    bool init_used = false;
    std::vector<GlobalObj> global_objs{};
    std::vector<GlobalObj> static_objs{};
    std::vector<Node> global_init{};
    std::vector<std::vector<DObj>> dscopes{};
    size_t fn_dbase = 0;
    std::vector<size_t> brk_dbase{};
    std::vector<size_t> cont_dbase{};
    Type cur_ret{};
    int cur_special = 0;
    bool cur_ret_ref = false;
    bool cur_implicit_copy = false;
    int rv_count = 0;

    bool class_needs_ctor(const StructDef& d) const {
        for (const Method& m : d.methods)
            if (m.name == "<ctor>") return true;
        if (d.poly) return true;  // the constructor stores the table pointer
        if (d.base >= 0 && class_needs_ctor(structs[static_cast<size_t>(d.base)])) return true;
        for (size_t i = d.base_fields; i < d.fields.size(); ++i) {
            const Type& t = d.fields[i].type;
            if (t.base == 3 && t.pointers == 0 && !t.is_ref && t.struct_id >= 0 &&
                class_needs_ctor(structs[static_cast<size_t>(t.struct_id)]))
                return true;
        }
        return false;
    }
    bool class_needs_dtor(const StructDef& d) const {
        for (const Method& m : d.methods)
            if (m.name == "<dtor>") return true;
        if (d.base >= 0 && class_needs_dtor(structs[static_cast<size_t>(d.base)])) return true;
        for (size_t i = d.base_fields; i < d.fields.size(); ++i) {
            const Type& t = d.fields[i].type;
            if (t.base == 3 && t.pointers == 0 && !t.is_ref && t.struct_id >= 0 &&
                class_needs_dtor(structs[static_cast<size_t>(t.struct_id)]))
                return true;
        }
        return false;
    }
    bool obj_needs_ctor(const Type& t) const {
        return t.base == 3 && t.pointers == 0 && !t.is_ref && t.struct_id >= 0 &&
               class_needs_ctor(structs[static_cast<size_t>(t.struct_id)]);
    }
    bool obj_needs_dtor(const Type& t) const {
        return t.base == 3 && t.pointers == 0 && !t.is_ref && t.struct_id >= 0 &&
               class_needs_dtor(structs[static_cast<size_t>(t.struct_id)]);
    }

    // A call of a constructor or destructor on the object whose address is 'self'.
    bool special_call(int sid, const char* which, Node self, std::vector<Node> args, Node* out) {
        std::string tctor;
        if (which[1] == 'c' && !ensure_ctor_template(sid, args, &tctor)) return false;
        const Method* m = which[1] == 'c' ? find_ctor(sid, args) : find_method(sid, which, args.size());
        if (!tctor.empty()) m = find_ctor_instance(sid, tctor);
        if (m != nullptr && which[1] == 'c' && !adjust_args(m->params, &args)) return false;
        if (m == nullptr) {
            error = std::string("no matching ") + (which[1] == 'c' ? "constructor" : "destructor") + " in " +
                    structs[static_cast<size_t>(sid)].name;
            return false;
        }
        Node call;
        call.kind = Node::Call;
        call.text = m->qname;
        call.number = 1;
        call.type = m->ret;
        call.kids.push_back(std::move(self));
        for (Node& arg : args) call.kids.push_back(std::move(arg));
        Node stmt;
        stmt.kind = Node::ExprStmt;
        stmt.kids.push_back(std::move(call));
        *out = std::move(stmt);
        return true;
    }
    // A call of a function that returns a reference is the pointer it returns, dereferenced.
    static Node reference_result(Node call, bool is_ref) {
        if (!is_ref) return call;
        call.type.is_ref = false;
        call.type.pointers += 1;
        Node d;
        d.kind = Node::Unary;
        d.op = TStar;
        d.kids.push_back(std::move(call));
        return d;
    }
    Node node_name(const std::string& n) const {
        Node name;
        name.kind = Node::Name;
        name.text = n;
        return name;
    }
    Node node_addr(Node inner) const {
        Node a;
        a.kind = Node::Unary;
        a.op = TAmp;
        a.kids.push_back(std::move(inner));
        return a;
    }
    Node node_this_field(const std::string& field) const {
        Node f;
        f.kind = Node::Field;
        f.op = TArrow;
        f.text = field;
        f.kids.push_back(node_name("this"));
        return f;
    }
    // this->__vptr = (void **)&table;
    Node vptr_store_this(const std::string& table_sym) const {
        Node table_addr = node_addr(node_name(table_sym));
        Node as_ptr;
        as_ptr.kind = Node::Unary;
        as_ptr.type.base = 0;
        as_ptr.type.pointers = 2;
        as_ptr.kids.push_back(std::move(table_addr));
        Node store;
        store.kind = Node::Assign;
        store.op = TAssign;
        store.kids.push_back(node_this_field("__vptr"));
        store.kids.push_back(std::move(as_ptr));
        Node stmt;
        stmt.kind = Node::ExprStmt;
        stmt.kids.push_back(std::move(store));
        return stmt;
    }
    // for (i = 0; i < n; i = i + 1) ctor(&name[i]) (or, going down, for the destructors). The
    // counter is a hidden local of the function being compiled.
    bool element_loop(const std::string& name, int count, bool reverse, int sid, const char* which, Node* out,
                      const Node* count_expr = nullptr) {
        if (cur_locals == nullptr) {
            error = "an array of objects needs a function";
            return false;
        }
        Local hidden;
        hidden.name = "__ai" + std::to_string(rv_count++);
        hidden.type.base = 1;
        place_local(cur_locals, hidden);
        auto int_lit = [](int v) {
            Node n;
            n.kind = Node::IntLit;
            n.number = static_cast<uint64_t>(v);
            n.type.base = 1;
            return n;
        };
        auto assign = [&](Node value) {
            Node a;
            a.kind = Node::Assign;
            a.op = TAssign;
            a.kids.push_back(node_name(hidden.name));
            a.kids.push_back(std::move(value));
            return a;
        };
        Node start;
        if (count_expr != nullptr && reverse) {
            start.kind = Node::Binary;
            start.op = TMinus;
            start.kids.push_back(*count_expr);
            start.kids.push_back(int_lit(1));
        } else {
            start = int_lit(reverse ? count - 1 : 0);
        }
        Node init = assign(std::move(start));
        Node cond;
        cond.kind = Node::Binary;
        cond.op = reverse ? TGe : TLt;
        cond.kids.push_back(node_name(hidden.name));
        if (count_expr != nullptr && !reverse) cond.kids.push_back(*count_expr);
        else cond.kids.push_back(int_lit(reverse ? 0 : count));
        Node step_value;
        step_value.kind = Node::Binary;
        step_value.op = reverse ? TMinus : TPlus;
        step_value.kids.push_back(node_name(hidden.name));
        step_value.kids.push_back(int_lit(1));
        Node step = assign(std::move(step_value));
        Node element;
        element.kind = Node::Index;
        element.kids.push_back(node_name(name));
        element.kids.push_back(node_name(hidden.name));
        Node call;
        if (!special_call(sid, which, node_addr(std::move(element)), {}, &call)) return false;
        Node loop;
        loop.kind = Node::For;
        loop.kids.push_back(std::move(init));
        loop.kids.push_back(std::move(cond));
        loop.kids.push_back(std::move(step));
        loop.kids.push_back(std::move(call));
        *out = std::move(loop);
        return true;
    }

    // C a[N] = { x, { y, z } }: element i is built by a constructor given its item (a brace is a
    // list of arguments); elements without an item use the default constructor.
    bool array_init_calls(const std::string& name, const Type& type, Node& init, std::vector<Node>* calls) {
        const int count = type.array_len;
        const int sid = type.struct_id;
        if (init.kind != Node::Block || init.text != "init") {
            error = "an array of objects is initialized with a brace list";
            return false;
        }
        if (static_cast<int>(init.kids.size()) > count) {
            error = "too many initializers for the array";
            return false;
        }
        for (int i = 0; i < count; ++i) {
            std::vector<Node> args;
            if (i < static_cast<int>(init.kids.size())) {
                Node& item = init.kids[static_cast<size_t>(i)];
                if (item.kind == Node::Block && item.text == "init") {
                    for (Node& a : item.kids) args.push_back(std::move(a));
                } else {
                    args.push_back(std::move(item));
                }
            }
            Node element;
            element.kind = Node::Index;
            element.kids.push_back(node_name(name));
            Node index;
            index.kind = Node::IntLit;
            index.number = static_cast<uint64_t>(i);
            index.type.base = 1;
            element.kids.push_back(std::move(index));
            Node call;
            if (!special_call(sid, "<ctor>", node_addr(std::move(element)), std::move(args), &call)) return false;
            calls->push_back(std::move(call));
        }
        return true;
    }

    // Destructor calls for the open blocks from 'base' up, latest object first.
    bool dtor_nodes(size_t base, std::vector<Node>* out) {
        for (size_t sc = dscopes.size(); sc-- > base;) {
            const std::vector<DObj>& list = dscopes[sc];
            for (size_t i = list.size(); i-- > 0;) {
                Node call;
                if (list[i].kind != 0) {
                    out->push_back(eh_call(list[i].kind == 1 ? "__okay_eh_pop" : "__okay_eh_end", {}));
                    continue;
                }
                if (list[i].count > 0) {
                    if (!element_loop(list[i].name, list[i].count, true, list[i].sid, "<dtor>", &call)) return false;
                    out->push_back(std::move(call));
                    if (eh_) out->push_back(eh_call("__okay_eh_pop", {}));
                    continue;
                }
                if (!special_call(list[i].sid, "<dtor>", node_addr(node_name(list[i].name)), {}, &call)) return false;
                out->push_back(std::move(call));
                if (eh_) out->push_back(eh_call("__okay_eh_pop", {}));
            }
        }
        return true;
    }

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
        if (type.is_ref) return 8;
        if (type.array_len >= 0) return type.array_len * size_of(peel_array(type));
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
        if (type.is_ref) return 8;
        if (type.array_len >= 0) return align_of(peel_array(type));
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

    // After a class type: C::name, where name is a typedef or using declared in the class. While a
    // pattern is read, a name qualified by a template parameter cannot be deduced (tpat -2).
    // ISO/IEC 14882 [temp.res] (typename) and [temp.deduct.type] (non-deduced contexts).
    bool qualify(Type* t, bool must) {
        bool any = false;
        while (is(TColonColon) && at + 1 < tokens.size() && tokens[at + 1].kind == TIdent) {
            if (pat_values != nullptr && (t->tpat != -1 || (t->base >= 100 && t->pointers == 0))) {
                next();
                next();
                t->base = 3;
                t->struct_id = -1;
                t->tpat = -2;
                any = true;
                continue;
            }
            if (t->base == 3 && t->struct_id >= 0 && t->pointers == 0) {
                const StructDef& sd = structs[static_cast<size_t>(t->struct_id)];
                auto it = sd.tdefs.find(tokens[at + 1].text);
                if (it != sd.tdefs.end()) {
                    *t = it->second;
                    next();
                    next();
                    any = true;
                    continue;
                }
            }
            break;
        }
        if (must && !any) {
            error = "typename names no member type";
            return false;
        }
        return true;
    }

    // A type with the qualifiers around it ([dcl.type.cv]): const T and T const are read and the
    // const is dropped (the type does not record it). The name to look for is after them.
    bool parse_type(Type* type, bool* imported, bool* exported) {
        while (accept(TConst)) {
        }
        if (!parse_type_core(type, imported, exported)) return false;
        while (accept(TConst)) {
        }
        return true;
    }
    bool parse_type_core(Type* type, bool* imported, bool* exported) {
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
        bool had_typename = false;
        if (is(TIdent) && cur().text == "typename") {
            next();
            had_typename = true;
        }
        type->base = 1;
        type->pointers = 0;
        type->array_len = -1;
        type->struct_id = -1;
        type->func_id = -1;
        type->tpat = -1;
        type->bits = -1;
        type->bit_off = 0;
        type->is_unsigned = false;
        type->is_const = false;
        type->is_bool = false;
        if (is(TStruct) || is(TUnion)) {
            const bool as_union = is(TUnion);
            const bool as_class = is(TStruct) && cur().text == "class";
            next();
            std::string name;
            if (is(TIdent)) {
                name = cur().text;
                next();
            }
            int base_id = -1;
            if (accept(TColon)) {
                while (is(TIdent) && (cur().text == "public" || cur().text == "private" ||
                                      cur().text == "protected" || cur().text == "virtual"))
                    next();
                if (!is(TIdent) || find_struct(cur().text) < 0) {
                    error = "expected a base class";
                    return false;
                }
                base_id = find_struct(cur().text);
                next();
                if (!is(TLBrace)) {
                    error = "only one base class is supported";
                    return false;
                }
            }
            if (is(TLBrace)) {
                int id = -1;
                if (!parse_struct_body(as_union, name, &id, as_class, base_id)) return false;
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
            if (is(TIdent)) {
                // The enumeration's name is a type name too, as in C++ ([dcl.enum]); its values are int.
                Type enum_type;
                enum_type.base = 1;
                typedefs[cur().text] = enum_type;
                next();
            }
            if (!parse_enum_body()) return false;
            type->base = 1;
            return true;
        }
        if (is_template_name_at(at)) {
            const std::string tname = cur().text;
            next();
            std::vector<TArg> targs;
            if (!parse_template_args(class_templates[tname], &targs, false)) return false;
            if (pat_values != nullptr) {
                // Reading a pattern: keep the name and the arguments, make no instance.
                TPat pattern;
                pattern.name = tname;
                pattern.args = std::move(targs);
                tpats.push_back(std::move(pattern));
                type->base = 3;
                type->struct_id = -1;
                type->tpat = static_cast<int>(tpats.size()) - 1;
                return qualify(type, had_typename);
            }
            if (!instantiate_class(tname, targs, type)) return false;
            return qualify(type, had_typename);
        }
        if (is(TIdent) && typedefs.find(cur().text) != typedefs.end()) {
            *type = typedefs[cur().text];
            next();
            return qualify(type, had_typename);
        }
        if (is_class_name()) {
            type->base = 3;
            type->struct_id = find_struct(cur().text);
            next();
            return qualify(type, had_typename);
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
                if (!parse_const(&next_value, "an enumerator value")) return false;
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

    bool add_dim(Type* type, int length) {
        if (type->array_len < 0) type->array_len = length;
        else type->more.push_back(length);
        return true;
    }
    // A constant expression ([expr.const]): read as a conditional-expression and folded by the parser.
    bool parse_const(uint64_t* value, const char* what) {
        Node n;
        if (!parse_expr(2, &n)) return false;
        if (n.kind != Node::IntLit || n.type.base >= 5) {
            error = std::string("expected ") + what;
            return false;
        }
        *value = n.number;
        return true;
    }
    bool parse_brackets(Type* type) {
        while (accept(TLBrack)) {
            int length = 0;
            uint64_t folded = 0;
            if (!parse_const(&folded, "an array length")) return false;  // numbers, enumerators, template values
            length = static_cast<int>(folded);
            if (!expect(TRBrack, "']'")) return false;
            if (!add_dim(type, length)) return false;
        }
        return true;
    }
    bool parse_init(Node* out);
    bool flatten_init(const Type& type, const Node& node, std::vector<uint8_t>* bytes, int at);

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
            accept(TConst);
            const std::string type_name = is(TIdent) ? cur().text : std::string();
            if (!parse_type(&pt, &imp, &exp)) return false;
            accept(TConst);
            if (is(TLParen) && at + 1 < tokens.size() && tokens[at + 1].kind == TStar) {
                next();
                int stars = 0;
                while (accept(TStar)) ++stars;
                std::string pname;
                if (!is(TIdent)) {
                    error = "expected a name";
                    return false;
                }
                pname = cur().text;
                next();
                if (!expect(TRParen, "')'")) return false;
                if (!expect(TLParen, "'('")) return false;
                Declarator fp;
                if (!parse_param_list(&fp)) return false;
                FuncType ft;
                ft.ret = pt;
                ft.params = fp.params;
                ft.vararg = fp.vararg;
                pt.pointers += stars;
                pt.func_id = static_cast<int>(func_types.size());
                func_types.push_back(ft);
                out->params.push_back(pt);
                out->param_names.push_back(pname);
                continue;
            }
            while (accept(TStar)) ++pt.pointers;
            if (accept(TAmp)) pt.is_ref = true;
            if (is(TEllipsis) && !type_name.empty()) {
                // Rest... rest: a function parameter pack ([temp.variadic]).
                next();
                std::string pack_name;
                if (is(TIdent)) {
                    pack_name = cur().text;
                    next();
                }
                auto tp = type_packs.find(type_name);
                if (tp == type_packs.end()) {
                    pt.pack = true;  // reading the template's parameter types: one entry stands for them all
                    out->params.push_back(pt);
                    out->param_names.push_back(pack_name);
                    continue;
                }
                std::vector<std::string> names;
                for (size_t k = 0; k < tp->second.size(); ++k) {
                    Type elem = tp->second[k];
                    elem.pointers += pt.pointers;
                    if (pt.is_ref) elem.is_ref = true;
                    if (pt.is_const) elem.is_const = true;
                    out->params.push_back(elem);
                    names.push_back(pack_name + "#" + std::to_string(k));
                    out->param_names.push_back(names.back());
                }
                if (!pack_name.empty()) param_packs[pack_name] = names;
                continue;
            }
            std::string pname;
            if (is(TIdent)) {
                pname = cur().text;
                next();
            }
            if (!parse_brackets(&pt)) return false;
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
            if (!parse_brackets(&out->type)) return false;
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
        if (accept(TAmp)) out->type.is_ref = true;
        if (!is(TIdent)) {
            error = "expected a name";
            return false;
        }
        out->name = cur().text;
        next();
        if (!parse_brackets(&out->type)) return false;
        if (!is(TLParen)) return true;
        next();
        out->is_function = true;
        return parse_param_list(out);
    }

    // At a global declaration: R Box<args>::member( or Box<args>::member = ... where Box is a class
    // template, the definition of a member of an explicit or partial specialization.
    bool explicit_member_def_ahead() const {
        size_t k = at;
        for (; k + 3 < tokens.size() && k < at + 60; ++k) {
            const Tok kind = tokens[k].kind;
            if (kind == TLParen || kind == TAssign || kind == TLBrace || kind == TSemi || kind == TEnd) return false;
            if (kind != TColonColon || tokens[k - 1].kind != TGt) continue;
            size_t m = k + 1;
            if (tokens[m].kind == TTilde) ++m;
            if (tokens[m].kind != TIdent) return false;
            const Tok after = tokens[m + 1].kind;
            if (after != TLParen && after != TAssign && after != TSemi) return false;
            size_t lt = k - 1;
            int angle = 0;
            while (lt > at) {
                if (tokens[lt].kind == TGt) ++angle;
                else if (tokens[lt].kind == TLt && --angle == 0) break;
                --lt;
            }
            return tokens[lt].kind == TLt && lt > at && tokens[lt - 1].kind == TIdent &&
                   class_templates.count(tokens[lt - 1].text) > 0;
        }
        return false;
    }

    bool is_decl_start() const {
        if (qualified_expr_ahead()) return false;
        if (is(TInt) || is(TVoid) || is(TChar) || is(TUnsigned) || is(TSigned) || is(TLong) || is(TShort) ||
            is(TStruct) || is(TUnion) || is(TEnum) || is(TTypedef) || is(TDeclspec) || is(TStatic) || is(TConst) ||
            is(TFloat) || is(TDouble))
            return true;
        return (is(TIdent) && typedefs.find(cur().text) != typedefs.end()) || is_class_name() ||
               is_template_name_at(at) || (is(TIdent) && cur().text == "typename");
    }

    // A class or struct used by its bare name, as in C++ ("C c;").
    bool class_name_at(size_t i) const {
        if (i >= tokens.size() || tokens[i].kind != TIdent || typedefs.find(tokens[i].text) != typedefs.end())
            return false;
        const int id = find_struct(tokens[i].text);
        if (id < 0) return false;
        if (structs[static_cast<size_t>(id)].cxx) return true;
        // A plain struct named without the word struct, as C++ allows: only where a declaration
        // clearly follows (a name, '*' or '&').
        if (i >= 2 && tokens[i - 1].kind == TLParen && tokens[i - 2].kind == TSizeof && i + 1 < tokens.size() &&
            tokens[i + 1].kind == TRParen)
            return true;  // sizeof(S)
        return i + 1 < tokens.size() &&
               (tokens[i + 1].kind == TIdent || tokens[i + 1].kind == TStar || tokens[i + 1].kind == TAmp);
    }
    bool is_class_name() const {
        if (!is(TIdent) || typedefs.find(cur().text) != typedefs.end()) return false;
        return class_name_at(at);
    }

    bool local_visible(const std::string& name) const;
    bool static_type(const Node& node, Type* out) const;
    bool parse_call_args(std::vector<Node>* args);
    bool expand_pack_arg(Node arg, std::vector<Node>* args);
    std::vector<std::string> packs_in(size_t from, size_t to) const;
    bool parse_pack_element(size_t from, size_t to, size_t k, bool unary, Node* out);
    bool try_fold(Node* out, bool* folded);
    bool parse_new(Node* out);
    bool ensure_vector_helpers(int sid);
    bool parse_delete(Node* out);
    bool method_call(Node* out, Node object, bool arrow, const std::string& name, std::vector<TArg> targs = {});
    bool qualified_call(Node* out, int cls, const std::string& name, std::vector<TArg> targs = {});
    bool parse_expr(int min_prec, Node* out);
    bool parse_unary(Node* out);
    bool parse_postfix(Node* out);
    // The class a qualifier names: a class, or a typedef (a template parameter) of a class type.
    int scope_class(const std::string& text) const {
        auto td = typedefs.find(text);
        if (td != typedefs.end()) {
            const Type& t = td->second;
            if (t.base == 3 && t.pointers == 0 && !t.is_array() && t.struct_id >= 0) return t.struct_id;
            return -1;
        }
        const int id = find_struct(text);
        return id >= 0 && structs[static_cast<size_t>(id)].cxx ? id : -1;
    }
    // A static data member or class constant used as an expression: a constant is its value, a
    // data member names its global.
    bool static_member_expr(int sid, const std::string& name, Node* out) const {
        if (sid < 0) return false;
        const StructDef& def = structs[static_cast<size_t>(sid)];
        auto it = def.statics.find(name);
        if (it == def.statics.end()) return false;
        if (it->second.has_const) {
            out->kind = Node::IntLit;
            out->number = static_cast<uint64_t>(it->second.cval);
            out->type = Type{};
            out->type.base = it->second.type.base == 5 || it->second.type.base == 6 ? it->second.type.base : 1;
            return true;
        }
        out->kind = Node::Name;
        out->text = it->second.symbol;
        return true;
    }
    // At a name, an optional template argument list and '::' name, where what follows is not a
    // declarator: an expression such as Box<int>::count = 1; or P::n + 2; ([class.static.data]).
    bool qualified_expr_ahead() const {
        if (!is(TIdent) || cur().text == "typename") return false;
        size_t i = at + 1;
        if (i < tokens.size() && tokens[i].kind == TLt) {
            int depth = 0;
            for (; i < tokens.size() && tokens[i].kind != TEnd; ++i) {
                if (tokens[i].kind == TLt) ++depth;
                else if (tokens[i].kind == TGt && --depth == 0) break;
            }
            ++i;
        }
        if (i + 2 >= tokens.size() || tokens[i].kind != TColonColon || tokens[i + 1].kind != TIdent) return false;
        const Tok after = tokens[i + 2].kind;
        return after != TIdent && after != TStar && after != TAmp && after != TColonColon;
    }
    bool class_name_text(const std::string& text) const {
        if (typedefs.find(text) != typedefs.end()) return false;
        const int id = find_struct(text);
        return id >= 0 && structs[static_cast<size_t>(id)].cxx;
    }
    bool parse_stmt(Node* out, std::vector<Local>* locals);
    bool parse_stmt_core(Node* out, std::vector<Local>* locals);
    bool parse_stmt_inner(Node* out, std::vector<Local>* locals, int stmt_line);
    bool parse_block(Node* out, std::vector<Local>* locals);

    // ISO/IEC 14882 [expr]: the binary operators from the loosest to the tightest.
    int prec(Tok op) const {
        if (op == TComma) return 0;
        if (is_assign_op(op)) return 1;
        if (op == TQuestion) return 2;
        if (op == TOrOr) return 3;
        if (op == TAndAnd) return 4;
        if (op == TPipe) return 5;
        if (op == TCaret) return 6;
        if (op == TAmp) return 7;
        if (op == TEq || op == TNe) return 8;
        if (op == TLt || op == TGt || op == TLe || op == TGe) return 9;
        if (op == TShl || op == TShr) return 10;
        if (op == TPlus || op == TMinus) return 11;
        if (op == TStar || op == TSlash || op == TPercent) return 12;
        return -1;
    }

    // The binary operator starting at token i and how many tokens it takes. '>' '>' written together
    // is a right shift and '>' '>=' is '>>='; the lexer keeps them apart so that nested template
    // argument lists close.
    Tok op_at(size_t i, size_t* width) const {
        *width = 1;
        if (i >= tokens.size()) return TEnd;
        if (tokens[i].kind == TGt && i + 1 < tokens.size() && tokens[i + 1].adj) {
            if (tokens[i + 1].kind == TGt) {
                *width = 2;
                return TShr;
            }
            if (tokens[i + 1].kind == TGe) {
                *width = 2;
                return TShrEq;
            }
        }
        return tokens[i].kind;
    }
    // The operator whose last token is token i (the one written before a fold's '...').
    Tok op_ending_at(size_t i, size_t* width) const {
        *width = 1;
        if (i >= 1 && tokens[i].kind == TGt && tokens[i].adj && tokens[i - 1].kind == TGt) {
            *width = 2;
            return TShr;
        }
        return tokens[i].kind;
    }

    // Looks ahead from the current token: stars or '&', a name, then '('.
    bool method_ahead() const {
        size_t i = at;
        while (i < tokens.size() && (tokens[i].kind == TStar || tokens[i].kind == TAmp)) ++i;
        return i + 1 < tokens.size() && tokens[i].kind == TIdent && tokens[i + 1].kind == TLParen;
    }

    bool skip_body() {
        if (!is(TLBrace)) {
            error = "expected a function body";
            return false;
        }
        int depth = 0;
        while (!is(TEnd)) {
            if (is(TLBrace)) ++depth;
            if (is(TRBrace)) {
                --depth;
                if (depth == 0) {
                    next();
                    return true;
                }
            }
            next();
        }
        error = "unterminated function body";
        return false;
    }

    const Method* find_method(int sid, const std::string& name, size_t nargs) const {
        if (sid < 0 || sid >= static_cast<int>(structs.size())) return nullptr;
        const StructDef& def = structs[static_cast<size_t>(sid)];
        for (size_t i = def.methods.size(); i-- > 0;)
            if (def.methods[i].name == name && def.methods[i].params.size() == nargs) return &def.methods[i];
        return nullptr;
    }

    static bool same_shape(const Type& a, const Type& b) {
        return a.base == b.base && a.pointers == b.pointers && a.is_ref == b.is_ref && a.struct_id == b.struct_id;
    }
    static bool is_copy_param(const Method& m, int sid) {
        return m.name == "<ctor>" && m.params.size() == 1 && m.params[0].is_ref && m.params[0].base == 3 &&
               m.params[0].pointers == 0 && m.params[0].struct_id == sid;
    }
    const Method* copy_ctor(int sid) const {
        if (sid < 0 || sid >= static_cast<int>(structs.size())) return nullptr;
        for (const Method& m : structs[static_cast<size_t>(sid)].methods)
            if (is_copy_param(m, sid)) return &m;
        return nullptr;
    }
    // Chooses among constructors with this many arguments: an argument that is an object of the
    // class picks the copy constructor, anything else the others.
    const Method* find_ctor(int sid, const std::vector<Node>& args) const {
        const StructDef& def = structs[static_cast<size_t>(sid)];
        const Method* best = nullptr;
        for (const Method& m : def.methods) {
            if (m.name != "<ctor>" || m.params.size() != args.size()) continue;
            if (args.size() == 1) {
                Type at;
                bool is_obj = static_type(args[0], &at) && at.base == 3 && at.pointers == 0 && !at.is_array();
                if (is_obj) {
                    // The object is of the class or of a class derived from it.
                    bool related = false;
                    for (int walk = at.struct_id; walk >= 0; walk = structs[static_cast<size_t>(walk)].base)
                        if (walk == sid) related = true;
                    is_obj = related;
                }
                if (is_obj != is_copy_param(m, sid)) continue;
            }
            best = &m;
        }
        if (best == nullptr) return find_method(sid, "<ctor>", args.size());
        return best;
    }
    bool class_has_copy(int sid) const { return copy_ctor(sid) != nullptr; }
    // The object (not a pointer or array) is an lvalue expression of a class with a copy constructor.
    bool wants_copy(const Type& t, const Node& arg) const {
        if (!(t.base == 3 && t.pointers == 0 && !t.is_ref && t.struct_id >= 0 && !t.is_array())) return false;
        if (!class_has_copy(t.struct_id)) return false;
        Type at;
        if (!(arg.kind == Node::Name || arg.kind == Node::Field || arg.kind == Node::Index ||
              (arg.kind == Node::Unary && arg.op == TStar)))
            return false;
        return static_type(arg, &at) && at.base == 3 && at.pointers == 0 && at.struct_id == t.struct_id &&
               !at.is_array();
    }
    struct Temp {
        std::string name;
        int sid = -1;
    };
    std::vector<Temp> stmt_temps{};
    std::vector<Node> stmt_pre{};
    int tmp_count = 0;

    // Arguments for a call: a reference parameter gets the address of its argument, a class
    // parameter with a copy constructor gets a copy made by that constructor into a hidden local
    // (destroyed after the statement).
    bool adjust_args(const std::vector<Type>& params, std::vector<Node>* args, size_t first = 0) {
        for (size_t i = first; i < args->size() && i - first < params.size(); ++i) {
            const Type& pt = params[i - first];
            Node& arg = (*args)[i];
            if (pt.is_ref) {
                const bool object = arg.kind == Node::Name || arg.kind == Node::Field || arg.kind == Node::Index ||
                                    (arg.kind == Node::Unary && arg.op == TStar);
                if (!object && cur_locals != nullptr) {
                    // A reference bound to a temporary: the value goes into a hidden local first.
                    Local hidden;
                    hidden.name = "__bt" + std::to_string(tmp_count++);
                    hidden.type = pt;
                    hidden.type.is_ref = false;
                    place_local(cur_locals, hidden);
                    Node assign;
                    assign.kind = Node::Assign;
                    assign.op = TAssign;
                    assign.number = 1;
                    assign.kids.push_back(node_name(hidden.name));
                    assign.kids.push_back(std::move(arg));
                    Node stmt;
                    stmt.kind = Node::ExprStmt;
                    stmt.kids.push_back(std::move(assign));
                    stmt_pre.push_back(std::move(stmt));
                    if (hidden.type.base == 3 && hidden.type.pointers == 0 && class_needs_dtor(structs[static_cast<size_t>(hidden.type.struct_id)]))
                        stmt_temps.push_back(Temp{hidden.name, hidden.type.struct_id});
                    arg = node_addr(node_name(hidden.name));
                    continue;
                }
                Node a = node_addr(std::move(arg));
                arg = std::move(a);
            } else if (cur_locals != nullptr && wants_copy(pt, arg)) {
                Local hidden;
                hidden.name = "__tmp" + std::to_string(tmp_count++);
                hidden.type = pt;
                place_local(cur_locals, hidden);
                const Method* cm = copy_ctor(pt.struct_id);
                Node call;
                call.kind = Node::Call;
                call.text = cm->qname;
                call.number = 1;
                call.type = cm->ret;
                call.kids.push_back(node_addr(node_name(hidden.name)));
                call.kids.push_back(node_addr(std::move(arg)));
                Node deref;
                deref.kind = Node::Unary;
                deref.op = TStar;
                deref.kids.push_back(std::move(call));
                arg = std::move(deref);
                if (class_needs_dtor(structs[static_cast<size_t>(pt.struct_id)]))
                    stmt_temps.push_back(Temp{hidden.name, pt.struct_id});
            }
        }
        return true;
    }

    // Adds or replaces a member function. A function with the same name and
    // parameter count that a base class declared is replaced; if that one was
    // virtual, this one is virtual in the same slot.
    // ?count@?$Box@H@@2HA: the decorated name of a static data member.
    std::string static_symbol(const StructDef& owner, const std::string& name, const Type& t) const {
        std::string out = "?" + name + "@" + owner.symname() + "@@2" + cxx_type(t, &structs);
        if (t.pointers > 0 && !t.is_ref) out += "EA";
        out += t.is_const ? 'B' : 'A';
        return out;
    }

    bool add_method(StructDef* def, int self, Method m) {
        m.owner = self;
        size_t found = def->methods.size();
        for (size_t i = 0; i < def->methods.size(); ++i)
            if (def->methods[i].name == m.name && def->methods[i].params.size() == m.params.size()) {
                bool same = true;
                if (m.name == "<ctor>")
                    for (size_t k = 0; k < m.params.size(); ++k)
                        if (!same_shape(def->methods[i].params[k], m.params[k])) same = false;
                if (same) found = i;
            }
        if (found < def->methods.size() && def->methods[found].is_virtual) {
            m.is_virtual = true;
            m.slot = def->methods[found].slot;
        }
        if (m.is_virtual) {
            if (!def->poly) {
                if (def->base >= 0) {
                    error = "a virtual function in a class derived from a class without virtual functions is not supported";
                    return false;
                }
                Field vptr;
                vptr.name = "__vptr";
                vptr.type.base = 0;
                vptr.type.pointers = 2;
                def->fields.insert(def->fields.begin(), vptr);
                def->poly = true;
            }
            if (m.slot < 0) {
                m.slot = static_cast<int>(def->vtable.size());
                def->vtable.push_back("");
            }
        }
        m.qname = def->name + "::" + m.name + "/" + std::to_string(m.params.size());
        if (m.name == "<ctor>")
            for (const Type& param : m.params) m.qname += param.is_ref ? "r" : "v";  // constructors overload by type
        if (!m.tsym.empty()) m.qname += "<" + m.tsym + ">";  // a constructor template instance
        m.symbol = cxx_method_symbol(structs, *def, m);
        if (m.is_virtual)
            def->vtable[static_cast<size_t>(m.slot)] = m.pure ? "" : m.name == "<dtor>" ? sdd_symbol(*def, m) : m.symbol;
        if (found < def->methods.size()) def->methods[found] = std::move(m);
        else def->methods.push_back(std::move(m));
        return true;
    }

    bool parse_struct_body(bool as_union, std::string name, int* out_id, bool is_class = false, int base_id = -1) {
        if (name.empty()) name = "__anon" + std::to_string(structs.size());
        StructDef def;
        def.name = name;
        if (!pending_sym.empty()) {
            def.tmpl = inst_base_name;
            def.targs = inst_args;
        }
        def.sym_name = pending_sym;
        pending_sym.clear();
        const std::string ctor_name = inst_base_name.empty() ? name : inst_base_name;
        def.is_union = as_union;
        def.is_class = is_class;
        def.cxx = is_class;
        if (base_id >= 0) {
            const StructDef& base = structs[static_cast<size_t>(base_id)];
            def.base = base_id;
            def.cxx = true;
            def.fields = base.fields;
            def.base_fields = base.fields.size();
            def.poly = base.poly;
            def.methods = base.methods;
            // Constructors are not inherited.
            for (size_t i = def.methods.size(); i-- > 0;)
                if (def.methods[i].name == "<ctor>") def.methods.erase(def.methods.begin() + static_cast<std::ptrdiff_t>(i));
            for (Method& inherited : def.methods) inherited.body_at = 0;
            def.vtable = base.vtable;
            def.statics = base.statics;
        }
        const std::map<std::string, uint64_t> enums_before = enum_values;
        structs.push_back(def);
        const int self = static_cast<int>(structs.size()) - 1;
        if (!expect(TLBrace, "'{'")) return false;
        char access = is_class ? 'A' : 'Q';
        // typedef and using names of this class are visible to the members after them.
        struct ScopedTd {
            std::string name;
            bool had = false;
            Type old{};
        };
        std::vector<ScopedTd> scoped_tds;
        while (!is(TRBrace) && !is(TEnd)) {
            if (is(TTypedef) || (is(TIdent) && cur().text == "using" && at + 2 < tokens.size() &&
                                 tokens[at + 1].kind == TIdent && tokens[at + 2].kind == TAssign)) {
                const bool is_using = !is(TTypedef);
                next();
                std::string alias;
                Type aliased;
                bool imp = false;
                bool exp = false;
                if (is_using) {
                    alias = cur().text;
                    next();
                    next();  // =
                }
                if (!parse_type(&aliased, &imp, &exp)) return false;
                while (accept(TStar)) ++aliased.pointers;
                if (!is_using) {
                    if (!is(TIdent)) {
                        error = "expected a typedef name";
                        return false;
                    }
                    alias = cur().text;
                    next();
                }
                if (!expect(TSemi, "';'")) return false;
                def.tdefs[alias] = aliased;
                ScopedTd saved_td;
                saved_td.name = alias;
                auto old_td = typedefs.find(alias);
                if (old_td != typedefs.end()) {
                    saved_td.had = true;
                    saved_td.old = old_td->second;
                }
                scoped_tds.push_back(std::move(saved_td));
                typedefs[alias] = aliased;
                continue;
            }
            if (is(TIdent) && at + 1 < tokens.size() && tokens[at + 1].kind == TColon &&
                (cur().text == "public" || cur().text == "private" || cur().text == "protected")) {
                access = cur().text == "public" ? 'Q' : cur().text == "private" ? 'A' : 'I';
                next();
                next();
                continue;
            }
            if (is(TIdent) && cur().text == "template" && at + 1 < tokens.size() && tokens[at + 1].kind == TLt) {
                def.cxx = true;
                if (!parse_member_template(self)) return false;
                continue;
            }
            bool is_virtual = false;
            if (is(TIdent) && cur().text == "virtual") {
                is_virtual = true;
                next();
            }
            bool static_member = false;
            bool static_const = false;
            if (is(TStatic)) {
                static_member = true;
                next();
                if (is(TConst)) {
                    static_const = true;
                    next();
                }
            }
            const bool is_dtor = is(TTilde) && at + 2 < tokens.size() && tokens[at + 1].kind == TIdent &&
                                 tokens[at + 1].text == ctor_name && tokens[at + 2].kind == TLParen;
            const bool is_ctor = is(TIdent) && cur().text == ctor_name && at + 1 < tokens.size() &&
                                 tokens[at + 1].kind == TLParen;
            if (is_dtor || is_ctor) {
                if (is_dtor) next();
                next();
                next();
                Declarator decl;
                if (!parse_param_list(&decl)) return false;
                if (is_dtor && !decl.params.empty()) {
                    error = "a destructor takes no arguments";
                    return false;
                }
                Method m;
                m.name = is_dtor ? "<dtor>" : "<ctor>";
                m.ret.base = 0;
                if (is_ctor) {
                    // A constructor returns its object, so that "new C(args)" is one call.
                    m.ret.base = 3;
                    m.ret.struct_id = self;
                    m.ret.pointers = 1;
                }
                m.params = decl.params;
                m.param_names = decl.param_names;
                m.is_virtual = is_virtual && is_dtor;
                if (is_virtual && !is_dtor) {
                    error = "a constructor cannot be virtual";
                    return false;
                }
                m.access = access;
                if (is(TColon) && is_ctor) {
                    m.body_at = at;
                    m.has_body = true;
                    while (!is(TLBrace) && !is(TEnd) && !is(TSemi)) next();
                    if (!is(TLBrace)) {
                        error = "expected a constructor body";
                        return false;
                    }
                    if (!skip_body()) return false;
                } else if (is(TLBrace)) {
                    m.body_at = at;
                    m.has_body = true;
                    if (!skip_body()) return false;
                } else {
                    if (!expect(TSemi, "';'")) return false;
                    if (!attach_outside(&m, is_dtor ? "<dtor>" : "<ctor>")) return false;
                }
                def.cxx = true;
                if (!add_method(&def, self, std::move(m))) return false;
                continue;
            }
            Field field;
            bool imp = false;
            bool exp = false;
            if (!parse_type(&field.type, &imp, &exp)) return false;
            if (is(TSemi) && !static_member) {  // enum { E = 3 };
                next();
                continue;
            }
            if (method_ahead()) {
                Declarator decl;
                if (!parse_declarator(field.type, &decl)) return false;
                Method m;
                m.name = decl.name;
                m.ret = decl.type;
                m.params = decl.params;
                m.param_names = decl.param_names;
                m.is_virtual = is_virtual;
                m.is_static = static_member;
                m.access = access;
                if (accept(TConst)) m.is_const = true;
                if (accept(TAssign)) {
                    if (!is(TNumber) || cur().number != 0) {
                        error = "expected = 0 for a pure virtual function";
                        return false;
                    }
                    next();
                    m.pure = true;
                    if (!m.is_virtual) {
                        error = "only a virtual function can be pure";
                        return false;
                    }
                }
                if (is(TLBrace)) {
                    m.body_at = at;
                    m.has_body = true;
                    if (!skip_body()) return false;
                } else {
                    if (!expect(TSemi, "';'")) return false;
                    if (!m.pure && !attach_outside(&m, m.name)) return false;
                }
                def.cxx = true;
                if (!add_method(&def, self, std::move(m))) return false;
                continue;
            }
            while (accept(TStar)) ++field.type.pointers;
            if (!is(TIdent)) {
                error = "expected a field name";
                return false;
            }
            field.name = cur().text;
            next();
            if (!parse_brackets(&field.type)) return false;
            if (static_member) {
                // static T name; or static const T name = constant; ([class.static.data])
                StaticMem sm;
                sm.type = field.type;
                sm.type.is_const = static_const;
                def.cxx = true;
                if (accept(TAssign)) {
                    Node init;
                    if (!parse_init(&init)) return false;
                    std::vector<uint8_t> bytes;
                    if (!flatten_init(sm.type, init, &bytes, 0)) return false;
                    if (sm.type.pointers != 0 || sm.type.is_array() || sm.type.base == 3) {
                        error = "a static member with an initializer must be an integer constant";
                        return false;
                    }
                    long long v = 0;
                    for (size_t bi = bytes.size(); bi-- > 0;) v = (v << 8) | bytes[bi];
                    const size_t nb = bytes.size();
                    if (nb > 0 && nb < 8 && (bytes[nb - 1] & 0x80) != 0 && sm.type.base == 1) v -= (1LL << (8 * nb));
                    sm.has_const = true;
                    sm.cval = v;
                } else {
                    sm.symbol = static_symbol(def, field.name, sm.type);
                    Global g;
                    g.name = sm.symbol;
                    g.type = sm.type;
                    g.bss = true;
                    globals.push_back(g);
                }
                if (!expect(TSemi, "';'")) return false;
                def.statics[field.name] = sm;
                continue;
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
        for (const auto& ev : enum_values) {
            auto was = enums_before.find(ev.first);
            if (was != enums_before.end() && was->second == ev.second) continue;
            StaticMem sm;
            sm.type.base = 1;
            sm.has_const = true;
            sm.cval = static_cast<long long>(ev.second);
            def.statics[ev.first] = sm;
        }
        int offset = 0;
        int align = 1;
        int cursor_bits = 0;
        size_t first_field = 0;
        if (def.base >= 0) {
            // A derived class starts after the whole base object, as the
            // MSVC layout does. The base fields keep their offsets.
            const StructDef& base = structs[static_cast<size_t>(def.base)];
            offset = base.size;
            align = base.align;
            cursor_bits = offset * 8;
            first_field = def.base_fields;
        }
        for (size_t fi = first_field; fi < def.fields.size(); ++fi) {
            Field& field = def.fields[fi];
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
        // A class that has a base or a member needing construction or destruction, and declares no
        // such function itself, gets one that only runs those of its parts.
        {
            bool has_ctor = false;
            bool has_dtor = false;
            for (const Method& m : def.methods) {
                if (m.name == "<ctor>") has_ctor = true;
                if (m.name == "<dtor>" && m.owner == self) has_dtor = true;
            }
            structs[static_cast<size_t>(self)] = def;
            bool user_copy = false;
            for (const Method& m : def.methods)
                if (is_copy_param(m, self)) user_copy = true;
            bool part_copies = def.base >= 0 && class_has_copy(def.base);
            for (size_t fi = def.base_fields; fi < def.fields.size(); ++fi) {
                const Type& ft = def.fields[fi].type;
                if (ft.base == 3 && ft.pointers == 0 && !ft.is_ref && !ft.is_array() && ft.struct_id >= 0 &&
                    ft.struct_id != self && class_has_copy(ft.struct_id))
                    part_copies = true;
            }
            if (!has_ctor && class_needs_ctor(structs[static_cast<size_t>(self)])) {
                Method m;
                m.name = "<ctor>";
                m.ret.base = 3;
                m.ret.struct_id = self;
                m.ret.pointers = 1;
                m.implicit = true;
                m.has_body = true;
                m.body_at = 1;
                if (!add_method(&def, self, std::move(m))) return false;
            }
            if (!user_copy && part_copies) {
                // The implicit copy constructor: copies the base and each member.
                Method m;
                m.name = "<ctor>";
                m.ret.base = 3;
                m.ret.struct_id = self;
                m.ret.pointers = 1;
                Type self_ref;
                self_ref.base = 3;
                self_ref.struct_id = self;
                self_ref.is_ref = true;
                m.params.push_back(self_ref);
                m.param_names.push_back("__o");
                m.implicit = true;
                m.implicit_copy = true;
                m.has_body = true;
                m.body_at = 1;
                if (!add_method(&def, self, std::move(m))) return false;
            }
            if (!has_dtor && class_needs_dtor(structs[static_cast<size_t>(self)])) {
                Method m;
                m.name = "<dtor>";
                m.ret.base = 0;
                m.implicit = true;
                m.has_body = true;
                m.body_at = 1;
                if (!add_method(&def, self, std::move(m))) return false;
            }
        }
        if (def.poly) {
            def.vtable_sym = "??_7" + def.symname() + "@@6B@";
            Global table;
            table.name = def.vtable_sym;
            table.type.base = 0;
            table.type.pointers = 1;
            table.type.array_len = static_cast<int>(def.vtable.size());
            table.bss = false;
            table.vtable = true;
            table.slots = def.vtable;
            globals.push_back(std::move(table));
        }
        structs[static_cast<size_t>(self)] = def;
        *out_id = self;
        // Member function bodies are compiled now that the class is complete,
        // so a body may use members declared after it.
        for (size_t mi = 0; mi < def.methods.size(); ++mi)
            if (def.methods[mi].body_at != 0 && def.methods[mi].owner == self)
                if (!define_method(self, mi)) return false;
        for (size_t k = scoped_tds.size(); k-- > 0;) {
            typedefs.erase(scoped_tds[k].name);
            if (scoped_tds[k].had) typedefs[scoped_tds[k].name] = scoped_tds[k].old;
        }
        return true;
    }

    // A member declared in a class template instance and defined outside it:
    // template <typename T> T Box<T>::get() const { ... }. The definition's parameter names are
    // bound to the instance's arguments, its parameter names are kept and its body is compiled with
    // the others. Only the primary template has such definitions here.
    bool attach_outside(Method* m, const std::string& member) {
        if (inst_base_name.empty()) return true;
        auto defs = ool_defs.find(inst_base_name);
        if (defs == ool_defs.end()) return true;
        for (int pass = 0; pass < 2; ++pass)
        for (const OolDef& ool : defs->second) {
            if (ool.member != member) continue;
            if (pass == 0) {
                if (!ool.explicit_inst || !inst_primary || ool.eargs.size() != inst_args.size()) continue;
                bool same = true;
                for (size_t ai = 0; ai < inst_args.size(); ++ai)
                    if (arg_key(ool.eargs[ai]) != arg_key(inst_args[ai])) same = false;
                if (!same) continue;
            } else if (ool.explicit_inst || ool.spec_key != inst_spec_key) {
                continue;
            }
            const auto bound = bind_params(ool.td, inst_bind_args());
            const size_t saved_at = at;
            at = ool.paren + 1;
            Declarator decl;
            bool ok = parse_param_list(&decl);
            const bool is_const = ok && accept(TConst);
            const bool match = ok && decl.params.size() == m->params.size() && is_const == m->is_const;
            const size_t body = at;
            at = saved_at;
            unbind_params(bound);
            if (!ok) return false;
            if (!match) continue;
            if (m->has_body) {
                error = "member function " + member + " is defined twice";
                return false;
            }
            m->has_body = true;
            m->body_at = body;
            m->param_names = decl.param_names;
            for (const TParam& tp : ool.td.params) m->ool_names.push_back(tp.name);
            return true;
        }
        return true;
    }

    // Compiles the body of a member function from its saved token position.
    bool define_method(int sid, size_t mi) {
        const Method m = structs[static_cast<size_t>(sid)].methods[mi];
        FuncDecl fn;
        fn.name = m.qname;
        fn.symbol = m.symbol;
        fn.ret = m.ret;
        Type self_ptr;
        self_ptr.base = 3;
        self_ptr.struct_id = sid;
        self_ptr.pointers = 1;
        if (!m.is_static) {
            fn.params.push_back(self_ptr);
            fn.param_names.push_back("this");
        }
        for (size_t i = 0; i < m.params.size(); ++i) {
            fn.params.push_back(m.params[i]);
            fn.param_names.push_back(i < m.param_names.size() ? m.param_names[i] : std::string());
        }
        finish_signature(&fn);
        const size_t resume = at;
        at = m.body_at;
        cur_implicit_copy = m.implicit_copy;
        // An out-of-class definition may call the class template's parameters by other names.
        TDecl alias;
        for (const std::string& n : m.ool_names) {
            TParam tp;
            tp.name = n;
            alias.params.push_back(tp);
        }
        std::vector<TArg> alias_args;
        for (size_t i = 0; i < alias.params.size() && i < inst_bind_args().size(); ++i) {
            alias.params[i].is_type = !inst_bind_args()[i].is_value;
            alias_args.push_back(inst_bind_args()[i]);
        }
        const auto alias_bound = bind_params(alias, alias_args);
        const bool ok = parse_function_body(&fn, sid, m.name == "<ctor>" ? 1 : m.name == "<dtor>" ? 2 : 0, m.implicit);
        unbind_params(alias_bound);
        at = resume;
        if (!ok) return false;
        structs[static_cast<size_t>(sid)].methods[mi].body_at = 0;
        funcs.push_back(std::move(fn));
        if (m.name == "<dtor>" && m.is_virtual) return define_scalar_dtor(sid, m);
        return true;
    }

    // void *C::`scalar deleting destructor'(unsigned flags) { this->~C(); if (flags) free(this); return this; }
    bool define_scalar_dtor(int sid, const Method& m) {
        FuncDecl fn;
        fn.name = m.qname + "/sdd";
        fn.symbol = sdd_symbol(structs[static_cast<size_t>(sid)], m);
        fn.ret.base = 0;
        fn.ret.pointers = 1;
        Type self_ptr;
        self_ptr.base = 3;
        self_ptr.struct_id = sid;
        self_ptr.pointers = 1;
        fn.params.push_back(self_ptr);
        fn.param_names.push_back("this");
        Type flag;
        flag.base = 1;
        flag.is_unsigned = true;
        fn.params.push_back(flag);
        fn.param_names.push_back("flags");
        finish_signature(&fn);
        std::vector<Local> locals;
        for (size_t p = 0; p < fn.params.size(); ++p) {
            Local local;
            local.name = fn.param_names[p];
            local.type = fn.params[p];
            place_local(&locals, local);
        }
        fn.nparams = static_cast<int>(fn.params.size());
        Node block;
        block.kind = Node::Block;
        block.text = "scope";
        Node call;
        if (!special_call(sid, "<dtor>", node_name("this"), {}, &call)) return false;
        block.kids.push_back(std::move(call));
        Node release;
        release.kind = Node::Call;
        release.text = "free";
        release.kids.push_back(node_name("this"));
        Node release_stmt;
        release_stmt.kind = Node::ExprStmt;
        release_stmt.kids.push_back(std::move(release));
        Node guard;
        guard.kind = Node::If;
        guard.kids.push_back(node_name("flags"));
        guard.kids.push_back(std::move(release_stmt));
        block.kids.push_back(std::move(guard));
        Node ret;
        ret.kind = Node::Return;
        ret.kids.push_back(node_name("this"));
        block.kids.push_back(std::move(ret));
        fn.body = std::move(block);
        fn.defined = true;
        fn.locals = std::move(locals);
        funcs.push_back(std::move(fn));
        return true;
    }

    // Parses a function body at the current token with the parameters
    // already in fn. cls is the class whose members are visible, or -1.
    // The body of a constructor (initializer list, then the block) or a destructor. A
    // constructor runs the base constructor, stores the table pointer, then each member in
    // declaration order; a destructor stores the table pointer, runs its block, then destroys the
    // members in reverse order and the base. Written from ISO/IEC 14882 [class.base.init] and
    // [class.dtor].
    bool parse_special_body(FuncDecl* fn, int cls, int special, bool implicit, std::vector<Local>* locals) {
        const StructDef def = structs[static_cast<size_t>(cls)];
        std::vector<Node> pre;
        std::vector<Node> post;
        int eh_held = 0;  // sub-objects recorded for unwinding while the constructor runs
        struct Init {
            std::string name;
            std::vector<Node> args;
        };
        std::vector<Init> inits;
        if (special == 1 && !implicit && accept(TColon)) {
            do {
                if (!is(TIdent)) {
                    error = "expected a member or base class name";
                    return false;
                }
                Init init;
                init.name = cur().text;
                next();
                if (!expect(TLParen, "'('")) return false;
                if (!parse_call_args(&init.args)) return false;
                inits.push_back(std::move(init));
            } while (accept(TComma));
        }
        if (special == 1 && implicit && cur_implicit_copy) {
            // Copy constructor made by the compiler: the base (its copy constructor if it has one,
            // else its fields one by one), the table pointer, then each member.
            auto other_field = [&](const std::string& f) {
                Node o;
                o.kind = Node::Field;
                o.op = TDot;
                o.text = f;
                o.kids.push_back(node_name("__o"));
                return o;
            };
            auto assign_field = [&](const std::string& f) {
                Node assign;
                assign.kind = Node::Assign;
                assign.op = TAssign;
                assign.number = 1;
                assign.kids.push_back(node_this_field(f));
                assign.kids.push_back(other_field(f));
                Node stmt;
                stmt.kind = Node::ExprStmt;
                stmt.kids.push_back(std::move(assign));
                return stmt;
            };
            if (def.base >= 0 && class_has_copy(def.base)) {
                std::vector<Node> args;
                args.push_back(node_name("__o"));
                Node call;
                if (!special_call(def.base, "<ctor>", node_name("this"), std::move(args), &call)) return false;
                pre.push_back(std::move(call));
            } else {
                for (size_t fi = 0; fi < def.base_fields; ++fi)
                    if (def.fields[fi].name != "__vptr") pre.push_back(assign_field(def.fields[fi].name));
            }
            if (def.poly) pre.push_back(vptr_store_this(def.vtable_sym));
            for (size_t fi = def.base_fields; fi < def.fields.size(); ++fi) {
                const Field& field = def.fields[fi];
                if (field.name == "__vptr") continue;
                const Type& ft = field.type;
                if (ft.base == 3 && ft.pointers == 0 && !ft.is_ref && !ft.is_array() && ft.struct_id >= 0 &&
                    class_has_copy(ft.struct_id)) {
                    std::vector<Node> args;
                    args.push_back(other_field(field.name));
                    Node call;
                    if (!special_call(ft.struct_id, "<ctor>", node_addr(node_this_field(field.name)),
                                      std::move(args), &call))
                        return false;
                    pre.push_back(std::move(call));
                } else if (obj_needs_ctor(ft) && !class_has_copy(ft.struct_id)) {
                    // A member with only a non-copy constructor is copied bytewise.
                    pre.push_back(assign_field(field.name));
                } else {
                    pre.push_back(assign_field(field.name));
                }
            }
        } else if (special == 1) {
            if (def.base >= 0 && class_needs_ctor(structs[static_cast<size_t>(def.base)])) {
                std::vector<Node> args;
                for (Init& init : inits)
                    if (init.name == structs[static_cast<size_t>(def.base)].name) args = std::move(init.args);
                Node call;
                if (!special_call(def.base, "<ctor>", node_name("this"), std::move(args), &call)) return false;
                pre.push_back(std::move(call));
                if (eh_ && class_needs_dtor(structs[static_cast<size_t>(def.base)])) {
                    eh_push_object(&pre, node_name("this"), def.base, 0, locals);
                    ++eh_held;
                }
            }
            if (def.poly) pre.push_back(vptr_store_this(def.vtable_sym));
            for (size_t fi = def.base_fields; fi < def.fields.size(); ++fi) {
                const Field& field = def.fields[fi];
                if (field.name == "__vptr") continue;
                Init* init = nullptr;
                for (Init& candidate : inits)
                    if (candidate.name == field.name) init = &candidate;
                if (obj_needs_ctor(field.type)) {
                    std::vector<Node> args;
                    if (init != nullptr) args = std::move(init->args);
                    Node call;
                    if (!special_call(field.type.struct_id, "<ctor>", node_addr(node_this_field(field.name)),
                                      std::move(args), &call))
                        return false;
                    pre.push_back(std::move(call));
                    if (eh_ && obj_needs_dtor(field.type)) {
                        eh_push_object(&pre, node_addr(node_this_field(field.name)), field.type.struct_id, 0, locals);
                        ++eh_held;
                    }
                } else if (init != nullptr) {
                    if (init->args.size() != 1) {
                        error = "member " + field.name + " takes one initializer";
                        return false;
                    }
                    Node assign;
                    assign.kind = Node::Assign;
                    assign.op = TAssign;
                    assign.kids.push_back(node_this_field(field.name));
                    assign.kids.push_back(std::move(init->args[0]));
                    Node stmt;
                    stmt.kind = Node::ExprStmt;
                    stmt.kids.push_back(std::move(assign));
                    pre.push_back(std::move(stmt));
                }
            }
            for (const Init& init : inits) {
                bool known = init.name == (def.base >= 0 ? structs[static_cast<size_t>(def.base)].name : std::string());
                for (size_t fi = def.base_fields; fi < def.fields.size(); ++fi)
                    if (def.fields[fi].name == init.name) known = true;
                if (!known) {
                    error = "no member or base class named " + init.name;
                    return false;
                }
            }
        } else {
            if (def.poly) pre.push_back(vptr_store_this(def.vtable_sym));
            for (size_t fi = def.fields.size(); fi-- > def.base_fields;) {
                const Field& field = def.fields[fi];
                if (!obj_needs_dtor(field.type)) continue;
                Node call;
                if (!special_call(field.type.struct_id, "<dtor>", node_addr(node_this_field(field.name)), {}, &call))
                    return false;
                post.push_back(std::move(call));
            }
            if (def.base >= 0 && class_needs_dtor(structs[static_cast<size_t>(def.base)])) {
                Node call;
                if (!special_call(def.base, "<dtor>", node_name("this"), {}, &call)) return false;
                post.push_back(std::move(call));
            }
        }
        Node block;
        block.kind = Node::Block;
        block.text = "scope";
        for (Node& n : pre) block.kids.push_back(std::move(n));
        if (!implicit) {
            Node user;
            if (!parse_block(&user, locals)) return false;
            if (eh_held > 0 && contains_return(user)) {
                error = "a return in a constructor body is not supported with exceptions";
                return false;
            }
            block.kids.push_back(std::move(user));
        }
        for (int k = 0; k < eh_held; ++k) block.kids.push_back(eh_call("__okay_eh_pop", {}));
        for (Node& n : post) block.kids.push_back(std::move(n));
        if (special == 1) {
            Node ret;
            ret.kind = Node::Return;
            ret.kids.push_back(node_name("this"));
            block.kids.push_back(std::move(ret));
        }
        fn->body = std::move(block);
        return true;
    }

    // special is 1 for a constructor and 2 for a destructor: the parts of the object are
    // constructed before the body and destroyed after it. An implicit one has no body.
    bool parse_function_body(FuncDecl* fn, int cls, int special = 0, bool implicit = false) {
        std::vector<Local>* saved_locals = cur_locals;
        const std::vector<std::vector<DObj>> saved_dscopes = dscopes;
        const size_t saved_dbase = fn_dbase;
        const Type saved_ret = cur_ret;
        dscopes.clear();
        fn_dbase = 0;
        cur_ret = fn->ret;
        const int saved_special = cur_special;
        cur_special = special;
        const bool saved_ret_ref = cur_ret_ref;
        cur_ret_ref = fn->ret_ref;
        const std::string saved_fn = current_fn;
        const int saved_depth = scope_depth;
        const std::vector<int> saved_parent = scope_parent;
        const int saved_class = cur_class;
        scope_depth = 0;
        scope_parent.clear();
        scope_parent.push_back(0);
        cur_class = cls;
        std::vector<Local> locals;
        if (fn->sret) {
            Local hidden;
            hidden.name = "__sret";
            hidden.type.pointers = 1;
            hidden.type.base = 3;
            hidden.type.struct_id = fn->ret.struct_id;
            place_local(&locals, hidden);
            fn->sret_off = locals.back().offset;
        }
        fn->param_base = static_cast<int>(locals.size());
        for (size_t p = 0; p < fn->param_names.size(); ++p) {
            Local local;
            local.name = fn->param_names[p];
            local.type = fn->params[p];
            if (p < fn->param_ref.size() && fn->param_ref[p]) {
                local.type.pointers -= 1;
                local.type.is_ref = true;
            }
            place_local(&locals, local);
        }
        fn->nparams = static_cast<int>(fn->params.size());
        current_fn = fn->name;
        cur_locals = &locals;
        fn->line = cur().line;
        bool ok = true;
        if (special == 0) {
            ok = parse_block(&fn->body, &locals);
        } else {
            ok = parse_special_body(fn, cls, special, implicit, &locals);
        }
        fn->end_line = at > 0 ? tokens[at - 1].line : 0;
        dscopes = saved_dscopes;
        fn_dbase = saved_dbase;
        cur_ret = saved_ret;
        cur_special = saved_special;
        cur_ret_ref = saved_ret_ref;
        cur_locals = saved_locals;  // a template instance is parsed in the middle of another function
        current_fn = saved_fn;
        scope_depth = saved_depth;
        scope_parent = saved_parent;
        cur_class = saved_class;
        if (!ok) return false;
        fn->defined = true;
        fn->locals = std::move(locals);
        return true;
    }

    void finish_signature(FuncDecl* fn) {
        if (fn->ret.is_ref) {
            fn->ret_ref = true;
            fn->ret.is_ref = false;
            fn->ret.pointers += 1;
        }
        fn->param_ref.assign(fn->params.size(), false);
        for (size_t i = 0; i < fn->params.size(); ++i) {
            if (!fn->params[i].is_ref) continue;
            fn->param_ref[i] = true;
            fn->params[i].is_ref = false;
            fn->params[i].pointers += 1;
        }
        FuncType ft;
        ft.ret = fn->ret;
        ft.params = fn->params;
        ft.vararg = fn->vararg;
        fn->func_id = static_cast<int>(func_types.size());
        func_types.push_back(ft);
        fn->sret = fn->ret.base == 3 && fn->ret.pointers == 0 && !fits_register(fn->ret);
    }


    // ---- templates (ISO/IEC 14882 [temp]) ----
    // A template is kept as a range of tokens and parsed again with its parameters bound: a type
    // parameter is a typedef, a value parameter an enumerator. An instance is named
    // Box<H> (H is the decorated spelling of int) and its symbols use ?$Box@H, the public MSVC
    // decoration of a template instance; a function instance is ??$name@args@@YA<ret><params>@Z.
    struct TParam {
        std::string name;
        bool is_type = true;
        bool has_def = false;
        size_t def_at = 0;
        bool is_pack = false;
    };
    struct TDecl {
        std::vector<TParam> params;
        size_t start = 0;
        size_t end = 0;
        std::vector<Type> proto;  // function templates: parameter types, a parameter T is base 100 + index
    };
    struct TPat {
        std::string name;
        std::vector<TArg> args;
    };
    struct ClassSpec {
        TDecl td;
        std::vector<TArg> pattern;
        std::string key;  // the argument list spelled with parameters numbered, to pair definitions
    };
    struct OolDef {
        TDecl td;
        std::string member;  // a name, "<ctor>" or "<dtor>"
        size_t paren = 0;
        std::string spec_key;  // empty: the primary template; else the specialization it belongs to
        // template <> int Box<int>::get() { ... }: a member of the instance Box<int> of the primary
        // template, replacing the primary's definition for those arguments only.
        bool explicit_inst = false;
        std::vector<TArg> eargs;
    };
    struct OolStatic {
        TDecl td;
        std::string member;
        size_t init_at = 0;  // the token after '=', or 0 when there is no initializer
        std::string spec_key;
    };
    std::map<std::string, std::vector<OolStatic>> ool_statics{};
    // template <typename U> U conv(U x) { ... } declared inside a class ([temp.mem]): kept as a
    // token range with the class's own parameter values at that point, instantiated at a call.
    struct MemberTmpl {
        TDecl td;
        TDecl ctx;
        std::vector<TArg> ctx_args;
        bool is_static = false;
        std::string owner;      // the class (or class template) name the definition is written with
        bool bodyless = false;  // declared in the class, defined outside it
    };
    // template <typename U> U S::add(U x) { ... } or template <typename T> template <typename U>
    // U B<T>::add(U x) { ... }: the definition of a member function template outside its class.
    struct OolMemberTmpl {
        TDecl outer;  // the class template's parameters (empty for a plain class)
        TDecl td;     // the member template's own parameters; start/end is the definition
    };
    std::map<std::pair<std::string, std::string>, OolMemberTmpl> ool_member_tmpls{};
    std::map<std::pair<int, std::string>, MemberTmpl> member_tmpls{};
    TDecl inst_td{};
    std::vector<TPat> tpats{};
    std::map<std::string, std::vector<ClassSpec>> class_specs{};
    std::map<std::string, std::vector<OolDef>> ool_defs{};
    std::map<std::string, size_t> func_specs{};  // key of an explicit specialization -> its declaration
    const std::map<std::string, size_t>* pat_values = nullptr;  // set while a pattern is read
    bool pat_rest_ok = false;  // a class specialization's pattern may end in `Rest...`
    std::vector<TArg> inst_args{};
    std::vector<TArg> inst_spec_args{};
    std::string inst_spec_key{};
    const std::vector<TArg>& inst_bind_args() const { return inst_primary ? inst_args : inst_spec_args; }
    // The tokens from the '<' at lt to its '>', a parameter name replaced by its number, so a
    // specialization and the out-of-class definitions of its members can be matched ([temp.spec]).
    std::string pattern_key(size_t lt, const TDecl& td) const {
        std::string out;
        int depth = 0;
        for (size_t i = lt; i < tokens.size() && tokens[i].kind != TEnd; ++i) {
            if (tokens[i].kind == TLt) ++depth;
            else if (tokens[i].kind == TGt) --depth;
            std::string piece = tokens[i].kind == TIdent ? tokens[i].text : std::to_string(static_cast<int>(tokens[i].kind));
            if (tokens[i].kind == TNumber) piece += "#" + std::to_string(tokens[i].number);
            for (size_t k = 0; k < td.params.size(); ++k)
                if (tokens[i].kind == TIdent && tokens[i].text == td.params[k].name) piece = "$" + std::to_string(k);
            out += piece + " ";
            if (depth == 0) break;
        }
        return out;
    }
    bool match_loose = false;  // deducing from call arguments: a later argument may convert
    bool inst_primary = false;
    std::map<std::string, TDecl> class_templates{};
    std::map<std::string, TDecl> func_templates{};
    // Further templates of the same name (overloads), told apart by the number of arguments: one
    // with a parameter pack takes any number from its fixed parameters up, and loses to a template
    // with exactly the number of parameters ([temp.func.order], simplified).
    std::map<std::string, std::vector<TDecl>> func_template_extra{};
    const TDecl& pick_func_template(const std::string& name, size_t nargs) {
        const TDecl* best = &func_templates[name];
        bool best_exact = false;
        auto consider = [&](const TDecl& cand) {
            size_t fixed = 0;
            bool has_pack = false;
            for (const Type& pt : cand.proto) {
                if (pt.pack) has_pack = true;
                else ++fixed;
            }
            const bool fits = has_pack ? nargs >= fixed : nargs == cand.proto.size();
            if (!fits) return;
            if (!has_pack && !best_exact) {
                best = &cand;
                best_exact = true;
            } else if (!best_exact && best == &func_templates[name]) {
                best = &cand;
            }
        };
        consider(func_templates[name]);
        auto extra = func_template_extra.find(name);
        if (extra != func_template_extra.end())
            for (const TDecl& cand : extra->second) consider(cand);
        return *best;
    }
    std::string pending_sym{};
    std::string inst_base_name{};

    static std::string value_code(long long v) {
        // $0 and the value: 1..10 is one digit, 0 is A@, larger values are hex digits A-P ending in @.
        std::string out = "$0";
        if (v >= 1 && v <= 10) return out + static_cast<char>('0' + (v - 1));
        if (v == 0) return out + "A@";
        std::string hex;
        unsigned long long u = static_cast<unsigned long long>(v);
        while (u > 0) {
            hex.insert(hex.begin(), static_cast<char>('A' + (u & 15)));
            u >>= 4;
        }
        return out + hex + "@";
    }
    std::string arg_code(const TArg& a) const {
        if (a.is_pack) {  // MSVC writes the elements of a pack one after the other
            std::string out;
            for (const Type& t : a.pack) out += cxx_type(t, &structs);
            return out;
        }
        return a.is_value ? value_code(a.value) : cxx_type(a.type, &structs);
    }
    std::string arg_key(const TArg& a) const {
        if (a.is_pack) {
            std::string out = "{";
            for (const Type& t : a.pack) out += cxx_type(t, &structs) + ";";
            return out + "}";
        }
        return a.is_value ? "$" + std::to_string(a.value) : cxx_type(a.type, &structs);
    }
    // Packs in scope: typename... Ts is a list of types, a function parameter pack names the
    // parameters it was expanded to.
    std::map<std::string, std::vector<Type>> type_packs{};
    std::map<std::string, std::vector<std::string>> param_packs{};

    bool is_template_name_at(size_t i) const {
        return i + 1 < tokens.size() && tokens[i].kind == TIdent && tokens[i + 1].kind == TLt &&
               class_templates.count(tokens[i].text) > 0;
    }

    // At '<': the arguments up to '>', then the defaults. 'partial' lets a function template be
    // given only its first arguments.
    bool parse_template_args(const TDecl& td, std::vector<TArg>* out, bool partial) {
        if (!expect(TLt, "'<'")) return false;
        size_t idx = 0;
        while (!is(TGt) && !is(TEnd)) {
            if (idx >= td.params.size()) {
                error = "too many template arguments";
                return false;
            }
            if (td.params[idx].is_pack) {
                // The rest of the arguments belong to the pack; Rest... splices another pack in.
                TArg pa;
                pa.is_pack = true;
                while (!is(TGt) && !is(TEnd)) {
                    if (is(TIdent) && type_packs.count(cur().text) > 0 && at + 1 < tokens.size() &&
                        tokens[at + 1].kind == TEllipsis) {
                        for (const Type& t : type_packs[cur().text]) pa.pack.push_back(t);
                        next();
                        next();
                    } else if (pat_rest_ok && is(TIdent) && at + 2 < tokens.size() && tokens[at + 1].kind == TEllipsis &&
                               tokens[at + 2].kind == TGt && typedefs.count(cur().text) > 0 &&
                               typedefs[cur().text].base >= 100) {
                        // A specialization's pattern ends in its own pack parameter: Box<T, Rest...>.
                        pa.rest = typedefs[cur().text].base - 100;
                        next();
                        next();
                    } else {
                        TArg one;
                        if (!parse_one_arg(td.params[idx], &one)) return false;
                        pa.pack.push_back(one.type);
                    }
                    if (!accept(TComma)) break;
                }
                out->push_back(std::move(pa));
                ++idx;
                break;
            }
            TArg a;
            if (!parse_one_arg(td.params[idx], &a)) return false;
            out->push_back(std::move(a));
            ++idx;
            if (!accept(TComma)) break;
        }
        if (!expect(TGt, "'>'")) return false;
        for (; idx < td.params.size(); ++idx) {
            if (td.params[idx].is_pack) {
                TArg empty;
                empty.is_pack = true;
                out->push_back(std::move(empty));
                continue;
            }
            if (!td.params[idx].has_def) {
                if (partial) break;
                error = "too few template arguments";
                return false;
            }
            const size_t saved = at;
            at = td.params[idx].def_at;
            TArg a;
            const bool ok = parse_one_arg(td.params[idx], &a);
            at = saved;
            if (!ok) return false;
            out->push_back(std::move(a));
        }
        return true;
    }
    bool parse_one_arg(const TParam& tp, TArg* a) {
        if (tp.is_type) {
            bool imp = false;
            bool exp = false;
            if (!parse_type(&a->type, &imp, &exp)) return false;
            while (accept(TStar)) ++a->type.pointers;
            return true;
        }
        if (pat_values != nullptr && is(TIdent) && pat_values->count(cur().text) > 0 && at + 1 < tokens.size() &&
            (tokens[at + 1].kind == TComma || tokens[at + 1].kind == TGt)) {
            a->is_value = true;
            a->marker = static_cast<int>(pat_values->at(cur().text));
            next();
            return true;
        }
        Node value;
        if (!parse_expr(11, &value)) return false;
        if (value.kind != Node::IntLit) {
            error = "a template argument must be a constant";
            return false;
        }
        a->is_value = true;
        a->value = static_cast<long long>(value.number);
        return true;
    }

    struct Binding {
        std::string name;
        bool had_typedef = false;
        Type old_typedef{};
        bool had_enum = false;
        uint64_t old_enum = 0;
        bool had_pack = false;
        std::vector<Type> old_pack;
    };
    std::vector<Binding> bind_params(const TDecl& td, const std::vector<TArg>& args) {
        std::vector<Binding> saved;
        for (size_t i = 0; i < td.params.size() && i < args.size(); ++i) {
            Binding b;
            b.name = td.params[i].name;
            auto t = typedefs.find(b.name);
            if (t != typedefs.end()) {
                b.had_typedef = true;
                b.old_typedef = t->second;
            }
            auto e = enum_values.find(b.name);
            if (e != enum_values.end()) {
                b.had_enum = true;
                b.old_enum = e->second;
            }
            auto pk = type_packs.find(b.name);
            if (pk != type_packs.end()) {
                b.had_pack = true;
                b.old_pack = pk->second;
            }
            if (args[i].is_pack) {
                type_packs[b.name] = args[i].pack;
                typedefs[b.name] = Type{};
                enum_values.erase(b.name);
            } else if (args[i].is_value) {
                enum_values[b.name] = static_cast<uint64_t>(args[i].value);
                typedefs.erase(b.name);
                type_packs.erase(b.name);
            } else {
                type_packs.erase(b.name);
                typedefs[b.name] = args[i].type;
                enum_values.erase(b.name);
            }
            saved.push_back(std::move(b));
        }
        return saved;
    }
    void unbind_params(const std::vector<Binding>& saved) {
        for (size_t i = saved.size(); i-- > 0;) {
            const Binding& b = saved[i];
            typedefs.erase(b.name);
            enum_values.erase(b.name);
            if (b.had_typedef) typedefs[b.name] = b.old_typedef;
            if (b.had_enum) enum_values[b.name] = b.old_enum;
            type_packs.erase(b.name);
            if (b.had_pack) type_packs[b.name] = b.old_pack;
        }
    }

    // Deduction by matching a pattern (a parameter type or a specialization's argument) against an
    // actual type or argument; ISO/IEC 14882 [temp.deduct.type]. A template parameter is a marker
    // (base 100 + index); Node<T> is a pattern naming the template. got[i] says parameter i is bound.
    bool match_arg(const TArg& pat, const TArg& act, std::vector<TArg>* args, std::vector<bool>* got) const {
        if (pat.is_pack || act.is_pack) {
            // Box<T, Rest...> against the arguments of a pack: the leading patterns match one type
            // each and Rest takes the others ([temp.class.spec.match], [temp.deduct.type]).
            if (!pat.is_pack || !act.is_pack) return false;
            if (pat.rest < 0 ? act.pack.size() != pat.pack.size() : act.pack.size() < pat.pack.size()) return false;
            for (size_t i = 0; i < pat.pack.size(); ++i)
                if (!match_type(pat.pack[i], act.pack[i], args, got)) return false;
            if (pat.rest >= 0) {
                const size_t idx = static_cast<size_t>(pat.rest);
                if (idx >= args->size()) return false;
                TArg rest;
                rest.is_pack = true;
                rest.pack.assign(act.pack.begin() + static_cast<std::ptrdiff_t>(pat.pack.size()), act.pack.end());
                if ((*got)[idx]) return arg_key((*args)[idx]) == arg_key(rest);
                (*args)[idx] = std::move(rest);
                (*got)[idx] = true;
            }
            return true;
        }
        if (pat.is_value != act.is_value) return false;
        if (pat.is_value) {
            if (pat.marker < 0) return pat.value == act.value;
            const size_t idx = static_cast<size_t>(pat.marker);
            if (idx >= args->size()) return false;
            if ((*got)[idx]) return (*args)[idx].is_value && (*args)[idx].value == act.value;
            (*args)[idx] = act;
            (*got)[idx] = true;
            return true;
        }
        return match_type(pat.type, act.type, args, got);
    }
    bool match_type(const Type& pat, const Type& act, std::vector<TArg>* args, std::vector<bool>* got) const {
        if (pat.tpat == -2) return true;
        if (pat.tpat >= 0) {
            if (act.base != 3 || act.struct_id < 0 || act.pointers != pat.pointers) return false;
            const StructDef& sd = structs[static_cast<size_t>(act.struct_id)];
            const TPat& tp = tpats[static_cast<size_t>(pat.tpat)];
            if (sd.tmpl != tp.name || sd.targs.size() != tp.args.size()) return false;
            for (size_t i = 0; i < tp.args.size(); ++i)
                if (!match_arg(tp.args[i], sd.targs[i], args, got)) return false;
            return true;
        }
        if (pat.base >= 100) {
            const size_t idx = static_cast<size_t>(pat.base - 100);
            if (idx >= args->size() || act.pointers < pat.pointers) return false;
            Type bound = act;
            bound.pointers -= pat.pointers;
            bound.is_ref = false;
            if (bound.is_array()) return false;
            if ((*got)[idx])
                return match_loose || (!(*args)[idx].is_value && arg_key((*args)[idx]) == cxx_type(bound, &structs));
            (*args)[idx].is_value = false;
            (*args)[idx].type = bound;
            (*got)[idx] = true;
            return true;
        }
        return pat.base == act.base && pat.pointers == act.pointers && (pat.base != 3 || pat.struct_id == act.struct_id);
    }

    // Box<int>: the class instance, made the first time it is named.
    // template <typename T> int Box<T>::count = 10; gives the instance's static data member its
    // initial value; the parameter names of the definition are bound to the instance's arguments.
    bool apply_ool_statics(const std::string& name, int id) {
        auto defs = ool_statics.find(name);
        if (defs == ool_statics.end()) return true;
        for (const OolStatic& os : defs->second) {
            auto it = structs[static_cast<size_t>(id)].statics.find(os.member);
            if (os.spec_key != inst_spec_key) continue;
            if (it == structs[static_cast<size_t>(id)].statics.end() || it->second.has_const || os.init_at == 0) continue;
            const auto bound = bind_params(os.td, inst_bind_args());
            const size_t saved_at = at;
            at = os.init_at;
            Node init;
            const bool ok = parse_init(&init);
            at = saved_at;
            unbind_params(bound);
            if (!ok) return false;
            for (Global& g : globals)
                if (g.name == it->second.symbol) {
                    g.bss = false;
                    g.init_bytes.clear();
                    if (!flatten_init(g.type, init, &g.init_bytes, 0)) return false;
                }
        }
        return true;
    }

    bool instantiate_class(const std::string& name, const std::vector<TArg>& args_in, Type* out) {
        const TDecl* tdp = &class_templates[name];
        std::vector<TArg> args = args_in;
        // A specialization that matches the arguments replaces the primary template: the one with
        // the fewest parameters is the most specialized here.
        bool primary = true;
        std::string spec_key_now;
        std::vector<TArg> spec_args_now;
        {
            const ClassSpec* best = nullptr;
            std::vector<TArg> best_args;
            auto specs = class_specs.find(name);
            if (specs != class_specs.end())
                for (const ClassSpec& spec : specs->second) {
                    if (spec.pattern.size() != args.size()) continue;
                    std::vector<TArg> deduced(spec.td.params.size());
                    std::vector<bool> got(spec.td.params.size(), false);
                    bool fits = true;
                    for (size_t i = 0; i < args.size() && fits; ++i)
                        fits = match_arg(spec.pattern[i], args[i], &deduced, &got);
                    for (size_t i = 0; i < got.size() && fits; ++i) fits = got[i];
                    if (!fits) continue;
                    if (best == nullptr || spec.td.params.size() < best->td.params.size()) {
                        best = &spec;
                        best_args = deduced;
                    }
                }
            if (best != nullptr) {
                tdp = &best->td;
                primary = false;
                spec_args_now = best_args;
                spec_key_now = best->key;
            }
        }
        const TDecl& td = *tdp;
        const std::vector<TArg> bind_args = primary ? args : spec_args_now;
        std::string key = name + "<";
        std::string sym = "?$" + name + "@";
        for (size_t i = 0; i < args.size(); ++i) {
            if (i) key += ",";
            key += arg_key(args[i]);
            sym += arg_code(args[i]);
        }
        key += ">";
        int id = find_struct(key);
        if (id < 0) {
            const size_t saved_at = at;
            const std::string saved_base = inst_base_name;
            const std::vector<TArg> saved_inst_args = inst_args;
            const bool saved_primary = inst_primary;
            const std::string saved_spec_key = inst_spec_key;
            const std::vector<TArg> saved_spec_args = inst_spec_args;
            const auto saved = bind_params(td, bind_args);
            inst_base_name = name;
            inst_args = args;
            inst_primary = primary;
            inst_spec_key = spec_key_now;
            inst_spec_args = bind_args;
            const TDecl saved_inst_td = inst_td;
            inst_td = td;
            pending_sym = sym;
            Type self;
            self.base = 3;
            self.struct_id = static_cast<int>(structs.size());
            auto old = typedefs.find(name);
            const bool had = old != typedefs.end();
            const Type old_type = had ? old->second : Type{};
            typedefs[name] = self;  // the class names itself inside its body
            at = td.start;
            bool ok = true;
            const bool as_class = is(TStruct) && cur().text == "class";
            next();
            if (is(TIdent)) next();
            if (is(TLt)) {  // the argument list of a specialization
                int angle = 0;
                while (!is(TEnd)) {
                    if (is(TLt)) ++angle;
                    else if (is(TGt) && --angle == 0) {
                        next();
                        break;
                    }
                    next();
                }
            }
            int base_id = -1;
            if (accept(TColon)) {
                while (is(TIdent) && (cur().text == "public" || cur().text == "private" ||
                                      cur().text == "protected" || cur().text == "virtual"))
                    next();
                Type bt;
                bool imp = false;
                bool exp = false;
                if (!parse_type(&bt, &imp, &exp) || bt.base != 3) {
                    if (error.empty()) error = "expected a base class";
                    ok = false;
                } else {
                    base_id = bt.struct_id;
                }
            }
            if (ok && !is(TLBrace)) {
                error = "expected a class template body";
                ok = false;
            }
            if (ok) ok = parse_struct_body(false, key, &id, as_class, base_id);
            if (ok) ok = apply_ool_statics(name, id);
            if (had) typedefs[name] = old_type;
            else typedefs.erase(name);
            inst_args = saved_inst_args;
            inst_primary = saved_primary;
            inst_spec_key = saved_spec_key;
            inst_spec_args = saved_spec_args;
            inst_td = saved_inst_td;
            inst_base_name = saved_base;
            unbind_params(saved);
            at = saved_at;
            pending_sym.clear();
            if (!ok) return false;
        }
        out->base = 3;
        out->struct_id = id;
        return true;
    }

    // template <typename T, int N = 3> ... : remember the declaration as a token range.
    // The parameters of template <...>, from after '<' up to (not including) '>'.
    bool parse_tparams(TDecl* tdp) {
        TDecl& td = *tdp;
        while (!is(TGt) && !is(TEnd)) {
            TParam tp;
            if (is(TStruct) && cur().text == "class") {
                next();
            } else if (is(TIdent) && cur().text == "typename") {
                next();
            } else {
                tp.is_type = false;
                while (!is(TIdent) && !is(TComma) && !is(TGt) && !is(TEnd)) next();
            }
            if (tp.is_type && accept(TEllipsis)) tp.is_pack = true;
            if (!is(TIdent)) {
                error = "expected a template parameter name";
                return false;
            }
            tp.name = cur().text;
            next();
            if (accept(TAssign)) {
                tp.has_def = true;
                tp.def_at = at;
                int depth = 0;
                while (!is(TEnd)) {
                    if (is(TLt)) ++depth;
                    else if (is(TGt)) {
                        if (depth == 0) break;
                        --depth;
                    } else if (is(TComma) && depth == 0) break;
                    next();
                }
            }
            td.params.push_back(tp);
            if (!accept(TComma)) break;
        }
        return true;
    }

    // 'headless': a member of an explicitly specialized class defined outside it, which is written
    // without the template<> prefix (int Box<char>::get() const { ... }, [temp.expl.spec]).
    bool parse_template_decl(bool headless = false) {
        TDecl td;
        if (!headless) {
            next();  // template
            if (!expect(TLt, "'<'")) return false;
        }
        if (!headless && !parse_tparams(&td)) return false;
        if (!headless && !expect(TGt, "'>'")) return false;
        // template <typename T> template <typename U> ... B<T>::f(...): the class's parameters, then
        // the member template's own ([temp.mem]).
        TDecl outer;
        bool has_outer = false;
        if (!headless && is(TIdent) && cur().text == "template" && at + 1 < tokens.size() && tokens[at + 1].kind == TLt) {
            next();
            next();
            outer = td;
            td = TDecl();
            if (!parse_tparams(&td)) return false;
            if (!expect(TGt, "'>'")) return false;
            has_outer = true;
        }
        td.start = at;
        // The declaration ends after its first top-level body or semicolon.
        size_t k = at;
        int depth = 0;
        bool body = false;
        while (k < tokens.size() && tokens[k].kind != TEnd) {
            if (tokens[k].kind == TLBrace) {
                ++depth;
                body = true;
            } else if (tokens[k].kind == TRBrace) {
                --depth;
                if (depth == 0) {
                    ++k;
                    if (k < tokens.size() && tokens[k].kind == TSemi && (tokens[td.start].kind == TStruct)) ++k;
                    break;
                }
            } else if (tokens[k].kind == TSemi && depth == 0 && !body) {
                ++k;
                break;
            }
            ++k;
        }
        td.end = k;
        if (tokens[td.start].kind == TStruct) {
            if (td.start + 1 >= tokens.size() || tokens[td.start + 1].kind != TIdent) {
                error = "expected a class template name";
                return false;
            }
            const std::string cname = tokens[td.start + 1].text;
            if (td.start + 2 < tokens.size() && tokens[td.start + 2].kind == TLt) {
                // template <> struct Box<char> { ... } or template <typename T> struct Box<T *> { ... }
                auto primary = class_templates.find(cname);
                if (primary == class_templates.end()) {
                    error = "specialization of " + cname + ", which is not a class template";
                    return false;
                }
                ClassSpec spec;
                spec.td = td;
                spec.key = pattern_key(td.start + 2, td);
                std::map<std::string, size_t> values;
                for (size_t i = 0; i < td.params.size(); ++i)
                    if (!td.params[i].is_type) values[td.params[i].name] = i;
                const auto bound = bind_params(td, make_markers(td));
                const size_t was_at = at;
                at = td.start + 2;
                pat_values = &values;
                pat_rest_ok = true;
                const bool ok = parse_template_args(primary->second, &spec.pattern, false);
                pat_rest_ok = false;
                pat_values = nullptr;
                at = was_at;
                unbind_params(bound);
                if (!ok) return false;
                class_specs[cname].push_back(std::move(spec));
                at = td.end;
                return true;
            }
            class_templates[cname] = td;
            at = td.end;
            return true;
        }
        // A function template: its name is the identifier before the first '('. The parameter
        // types are read once with each type parameter standing for itself (base 100 + index), so
        // a call can deduce them.
        {
            // template <typename T> int Box<T>::count = 10; a static data member of a class template.
            size_t cc = td.start;
            while (cc < td.end && tokens[cc].kind != TColonColon && tokens[cc].kind != TLParen &&
                   tokens[cc].kind != TLBrace)
                ++cc;
            if (cc < td.end && tokens[cc].kind != TColonColon) cc = td.end;
            if (cc > td.start + 2 && cc + 2 < td.end && tokens[cc - 1].kind == TGt && tokens[cc + 1].kind == TIdent &&
                (tokens[cc + 2].kind == TAssign || tokens[cc + 2].kind == TSemi)) {
                size_t lt = cc - 1;
                int angle = 0;
                while (lt > td.start) {
                    if (tokens[lt].kind == TGt) ++angle;
                    else if (tokens[lt].kind == TLt && --angle == 0) break;
                    --lt;
                }
                if (tokens[lt].kind == TLt && lt > td.start && tokens[lt - 1].kind == TIdent &&
                    class_templates.count(tokens[lt - 1].text) > 0) {
                    OolStatic os;
                    os.td = td;
                    os.member = tokens[cc + 1].text;
                    {
                        bool plain = (cc - 1 - lt - 1) == td.params.size() * 2 - (td.params.empty() ? 0 : 1);
                        for (size_t i = 0; plain && i < td.params.size(); ++i)
                            plain = tokens[lt + 1 + i * 2].kind == TIdent &&
                                    tokens[lt + 1 + i * 2].text == td.params[i].name;
                        if (!plain) os.spec_key = pattern_key(lt, td);
                    }
                    if (tokens[cc + 2].kind == TAssign) os.init_at = cc + 3;
                    ool_statics[tokens[lt - 1].text].push_back(std::move(os));
                    at = td.end;
                    return true;
                }
            }
        }
        size_t paren = td.start;
        while (paren < td.end && tokens[paren].kind != TLParen) ++paren;
        if (paren == td.start || paren >= td.end) {
            error = "expected a function template";
            return false;
        }
        // template <typename U> R S::name(...) { ... }: the definition of a member function template
        // declared in a class (a class template when the class's own parameters came first).
        if (paren >= td.start + 3 && tokens[paren - 1].kind == TIdent && tokens[paren - 2].kind == TColonColon) {
            std::string owner;
            if (has_outer && tokens[paren - 3].kind == TGt) {
                size_t lt = paren - 3;
                int angle = 0;
                while (lt > td.start) {
                    if (tokens[lt].kind == TGt) ++angle;
                    else if (tokens[lt].kind == TLt && --angle == 0) break;
                    --lt;
                }
                if (tokens[lt].kind == TLt && lt > td.start && tokens[lt - 1].kind == TIdent) owner = tokens[lt - 1].text;
            } else if (!has_outer && tokens[paren - 3].kind == TIdent && class_templates.count(tokens[paren - 3].text) == 0) {
                owner = tokens[paren - 3].text;
            }
            if (!owner.empty()) {
                OolMemberTmpl om;
                om.outer = outer;
                om.td = td;
                ool_member_tmpls[std::make_pair(owner, tokens[paren - 1].text)] = std::move(om);
                at = td.end;
                return true;
            }
        }
        // template <typename T> R Box<T>::member(...) { ... }: a member of a class template.
        {
            size_t m = paren - 1;
            bool dtor = false;
            if (tokens[m].kind == TIdent) {
                size_t q = m;
                if (q >= 1 && tokens[q - 1].kind == TTilde) {
                    dtor = true;
                    --q;
                }
                if (q >= 3 && tokens[q - 1].kind == TColonColon && tokens[q - 2].kind == TGt) {
                    size_t gt = q - 2;
                    size_t lt = gt;
                    int angle = 0;
                    while (lt > td.start) {
                        if (tokens[lt].kind == TGt) ++angle;
                        else if (tokens[lt].kind == TLt && --angle == 0) break;
                        --lt;
                    }
                    if (tokens[lt].kind == TLt && lt > td.start && tokens[lt - 1].kind == TIdent &&
                        class_templates.count(tokens[lt - 1].text) > 0) {
                        const std::string cname = tokens[lt - 1].text;
                        // The argument list must be the definition's own parameters, in order.
                        bool plain = (gt - lt - 1) == td.params.size() * 2 - (td.params.empty() ? 0 : 1);
                        for (size_t i = 0; plain && i < td.params.size(); ++i)
                            plain = tokens[lt + 1 + i * 2].kind == TIdent &&
                                    tokens[lt + 1 + i * 2].text == td.params[i].name;
                        OolDef ool;
                        ool.td = td;
                        if (!plain) ool.spec_key = pattern_key(lt, td);  // int Box<char>::get()
                        if (!plain && td.params.empty()) {
                            bool is_spec = false;
                            for (const ClassSpec& cs : class_specs[cname])
                                if (cs.key == ool.spec_key) is_spec = true;
                            if (!is_spec) {
                                const size_t was_at = at;
                                at = lt;
                                const bool args_ok = parse_template_args(class_templates[cname], &ool.eargs, false);
                                at = was_at;
                                if (!args_ok) return false;
                                ool.explicit_inst = true;
                            }
                        }
                        ool.member = dtor ? "<dtor>" : tokens[m].text == cname ? "<ctor>" : tokens[m].text;
                        ool.paren = paren;
                        ool_defs[cname].push_back(std::move(ool));
                        at = td.end;
                        return true;
                    }
                }
            }
        }
        if (tokens[paren - 1].kind != TIdent && tokens[paren - 1].kind != TGt) {
            error = "expected a function template";
            return false;
        }
        // template <> int twice<int>(int x) { ... } or template <> int twice(int x): an explicit
        // specialization, used instead of the instance the template would make.
        if (td.params.empty()) {
            std::string fname;
            std::vector<TArg> given_args;
            size_t erased = 0;
            if (tokens[paren - 1].kind == TGt) {
                size_t lt = paren - 1;
                int angle = 0;
                while (lt > td.start) {
                    if (tokens[lt].kind == TGt) ++angle;
                    else if (tokens[lt].kind == TLt && --angle == 0) break;
                    --lt;
                }
                if (tokens[lt].kind != TLt || lt == td.start || tokens[lt - 1].kind != TIdent) {
                    error = "expected a function template";
                    return false;
                }
                fname = tokens[lt - 1].text;
                if (func_templates.count(fname) == 0) {
                    error = "specialization of " + fname + ", which is not a function template";
                    return false;
                }
                at = lt;
                if (!parse_template_args(func_templates[fname], &given_args, true)) return false;
                erased = paren - lt;
                tokens.erase(tokens.begin() + static_cast<std::ptrdiff_t>(lt),
                             tokens.begin() + static_cast<std::ptrdiff_t>(paren));
                paren = lt;
            } else {
                fname = tokens[paren - 1].text;
                if (func_templates.count(fname) == 0) {
                    error = "specialization of " + fname + ", which is not a function template";
                    return false;
                }
            }
            td.end -= erased;
            const TDecl& primary = func_templates[fname];
            std::vector<TArg> deduced(primary.params.size());
            std::vector<bool> got(primary.params.size(), false);
            for (size_t i = 0; i < given_args.size() && i < deduced.size(); ++i) {
                deduced[i] = given_args[i];
                got[i] = true;
            }
            at = paren + 1;
            Declarator pd;
            if (!parse_param_list(&pd)) return false;
            for (size_t i = 0; i < primary.proto.size() && i < pd.params.size(); ++i) {
                Type actual = pd.params[i];
                actual.is_ref = false;
                if (!match_type(primary.proto[i], actual, &deduced, &got)) {
                    error = "specialization does not match the template " + fname;
                    return false;
                }
            }
            std::string key = fname + "<";
            for (size_t i = 0; i < deduced.size(); ++i) {
                if (!got[i]) {
                    error = "cannot deduce template argument " + primary.params[i].name;
                    return false;
                }
                if (i) key += ",";
                key += arg_key(deduced[i]);
            }
            key += ">";
            func_specs[key] = td.start;
            at = td.end;
            return true;
        }
        if (tokens[paren - 1].kind != TIdent) {
            error = "expected a function template";
            return false;
        }
        const std::string fname = tokens[paren - 1].text;
        std::vector<Binding> saved;
        std::map<std::string, size_t> values;
        for (size_t i = 0; i < td.params.size(); ++i)
            if (!td.params[i].is_type) values[td.params[i].name] = i;
        saved = bind_params(td, make_markers(td));
        const size_t resume = td.end;
        const size_t was_at = at;
        at = paren + 1;
        Declarator pd;
        pat_values = &values;
        const bool ok = parse_param_list(&pd);
        pat_values = nullptr;
        td.proto = pd.params;
        at = was_at;
        unbind_params(saved);
        if (!ok) return false;
        if (func_templates.count(fname) > 0) func_template_extra[fname].push_back(std::move(td));  // an overload
        else func_templates[fname] = td;
        at = resume;
        return true;
    }

    // In a class body, at 'template': a member function template. Its declaration is kept as a token
    // range together with the class's own template arguments at this point ([temp.mem]).
    bool parse_member_template(int self) {
        next();  // template
        if (!expect(TLt, "'<'")) return false;
        MemberTmpl mt;
        if (!parse_tparams(&mt.td)) return false;
        if (!expect(TGt, "'>'")) return false;
        mt.td.start = at;
        size_t k = at;
        int depth = 0;
        bool body = false;
        while (k < tokens.size() && tokens[k].kind != TEnd) {
            if (tokens[k].kind == TLBrace) {
                ++depth;
                body = true;
            } else if (tokens[k].kind == TRBrace) {
                if (--depth == 0) {
                    ++k;
                    break;
                }
            } else if (tokens[k].kind == TSemi && depth == 0 && !body) {
                ++k;
                break;
            }
            ++k;
        }
        mt.td.end = k;
        size_t paren = mt.td.start;
        while (paren < mt.td.end && tokens[paren].kind != TLParen) ++paren;
        if (paren == mt.td.start || paren >= mt.td.end || tokens[paren - 1].kind != TIdent) {
            error = "expected a member function template";
            return false;
        }
        const std::string mname = tokens[paren - 1].text;
        mt.is_static = tokens[mt.td.start].kind == TStatic;
        mt.bodyless = tokens[mt.td.end - 1].kind == TSemi;
        mt.owner = inst_base_name.empty() ? structs[static_cast<size_t>(self)].name : inst_base_name;
        if (!inst_base_name.empty()) {
            mt.ctx = inst_td;
            mt.ctx_args = inst_bind_args();
        }
        std::map<std::string, size_t> values;
        for (size_t i = 0; i < mt.td.params.size(); ++i)
            if (!mt.td.params[i].is_type) values[mt.td.params[i].name] = i;
        const auto saved = bind_params(mt.td, make_markers(mt.td));
        const size_t was_at = at;
        at = paren + 1;
        Declarator pd;
        pat_values = &values;
        const bool ok = parse_param_list(&pd);
        pat_values = nullptr;
        mt.td.proto = pd.params;
        at = was_at;
        unbind_params(saved);
        if (!ok) return false;
        at = mt.td.end;
        member_tmpls[std::make_pair(self, mname)] = std::move(mt);
        return true;
    }

    // The member function template name of class sid or of a base of it.
    const MemberTmpl* find_member_tmpl(int sid, const std::string& name, int* owner) const {
        while (sid >= 0) {
            auto it = member_tmpls.find(std::make_pair(sid, name));
            if (it != member_tmpls.end()) {
                *owner = sid;
                return &it->second;
            }
            sid = structs[static_cast<size_t>(sid)].base;
        }
        return nullptr;
    }

    const Method* find_ctor_instance(int sid, const std::string& tsym) const {
        for (const Method& m : structs[static_cast<size_t>(sid)].methods)
            if (m.name == "<ctor>" && m.tsym == tsym) return &m;
        return nullptr;
    }

    // A constructor template (template <typename T> S(T x) { ... }, [temp.mem]) is instantiated
    // when a constructor call has its number of arguments, is not a copy of the class, and no
    // ordinary constructor takes that many arguments. Returns the instance's tsym, or empty.
    bool ensure_ctor_template(int sid, const std::vector<Node>& args, std::string* tsym_out) {
        tsym_out->clear();
        int owner = -1;
        const StructDef& def = structs[static_cast<size_t>(sid)];
        const MemberTmpl* found = nullptr;
        for (auto it = member_tmpls.begin(); it != member_tmpls.end(); ++it)
            if (it->first.first == sid && it->first.second == it->second.owner) found = &it->second;
        (void)owner;
        if (found == nullptr || found->td.proto.size() != args.size()) return true;
        if (args.size() == 1) {
            Type at;
            if (static_type(args[0], &at) && at.base == 3 && at.pointers == 0 && !at.is_array()) {
                for (int walk = at.struct_id; walk >= 0; walk = structs[static_cast<size_t>(walk)].base)
                    if (walk == sid) return true;  // a copy
            }
        }
        for (const Method& m : def.methods)
            if (m.name == "<ctor>" && m.tsym.empty() && m.params.size() == args.size()) return true;
        const MemberTmpl mt = *found;
        std::vector<TArg> targs;
        if (!deduce_args(mt.td, mt.owner, &targs, args)) return false;
        std::string codes;
        for (const TArg& a : targs) codes += arg_code(a);
        const std::string tsym = "?$?0" + codes;
        *tsym_out = tsym;
        if (find_ctor_instance(sid, tsym) != nullptr) return true;
        const auto ctx_bound = bind_params(mt.ctx, mt.ctx_args);
        const auto bound = bind_params(mt.td, targs);
        const size_t saved_at = at;
        at = mt.td.start;
        bool ok = true;
        Method m;
        m.name = "<ctor>";
        m.ret.base = 3;
        m.ret.struct_id = sid;
        m.ret.pointers = 1;
        next();  // the class name
        next();  // (
        Declarator decl;
        ok = parse_param_list(&decl);
        if (ok) {
            m.params = decl.params;
            m.param_names = decl.param_names;
            if (is(TColon)) {
                m.body_at = at;
                m.has_body = true;
                while (!is(TLBrace) && !is(TEnd) && !is(TSemi)) next();
                if (!is(TLBrace)) {
                    error = "expected a constructor body";
                    ok = false;
                }
            } else if (is(TLBrace)) {
                m.body_at = at;
                m.has_body = true;
            } else {
                error = "a constructor template needs a body in the class";
                ok = false;
            }
        }
        size_t mi = 0;
        if (ok) {
            m.tsym = tsym;
            ok = add_method(&structs[static_cast<size_t>(sid)], sid, std::move(m));
            const StructDef& od = structs[static_cast<size_t>(sid)];
            for (size_t i = 0; i < od.methods.size(); ++i)
                if (od.methods[i].name == "<ctor>" && od.methods[i].tsym == tsym) mi = i;
        }
        at = saved_at;
        if (ok) ok = define_method(sid, mi);
        unbind_params(bound);
        unbind_params(ctx_bound);
        return ok;
    }

    // obj.name<args>(call_args): makes the instance of a member function template the first time
    // it is called, as a member named name<args> of the class that declared the template. Its
    // symbol follows the public MSVC decoration ??$name@args@@QEAA... Returns the member's name.
    bool instantiate_member(int sid, const std::string& name, std::vector<TArg> targs,
                            const std::vector<Node>& call_args, std::string* out, int* owner_out) {
        int owner = -1;
        const MemberTmpl* found = find_member_tmpl(sid, name, &owner);
        if (found == nullptr) {
            error = "no member function template " + name;
            return false;
        }
        const MemberTmpl mt = *found;  // the map may change while the instance is compiled
        if (!deduce_args(mt.td, name, &targs, call_args)) return false;
        std::string key = name + "<";
        std::string codes;
        for (size_t i = 0; i < targs.size(); ++i) {
            if (i) key += ",";
            key += arg_key(targs[i]);
            codes += arg_code(targs[i]);
        }
        key += ">";
        *out = key;
        *owner_out = owner;
        for (const Method& m : structs[static_cast<size_t>(owner)].methods)
            if (m.name == key) return true;
        TDecl ctx_decl = mt.ctx;
        TDecl body_decl = mt.td;
        if (mt.bodyless) {
            auto def = ool_member_tmpls.find(std::make_pair(mt.owner, name));
            if (def == ool_member_tmpls.end()) {
                error = "the member function template " + name + " has no definition";
                return false;
            }
            body_decl = def->second.td;
            if (!def->second.outer.params.empty()) ctx_decl = def->second.outer;
        }
        const auto ctx_bound = bind_params(ctx_decl, mt.ctx_args);
        const auto bound = bind_params(body_decl, targs);
        const size_t saved_at = at;
        at = body_decl.start;
        bool ok = true;
        if (is(TStatic)) next();
        bool imported = false;
        bool exported = false;
        Type type;
        ok = parse_type(&type, &imported, &exported);
        if (ok && mt.bodyless && is(TIdent)) {
            // S :: or B<T> :: of the definition outside the class
            size_t q = at + 1;
            if (q < tokens.size() && tokens[q].kind == TLt) {
                int angle = 0;
                for (; q < tokens.size() && tokens[q].kind != TEnd; ++q) {
                    if (tokens[q].kind == TLt) ++angle;
                    else if (tokens[q].kind == TGt && --angle == 0) {
                        ++q;
                        break;
                    }
                }
            }
            if (q < tokens.size() && tokens[q].kind == TColonColon) at = q + 1;
        }
        Declarator decl;
        if (ok) ok = parse_declarator(type, &decl);
        if (ok && !decl.is_function) {
            error = "expected a member function template";
            ok = false;
        }
        size_t mi = 0;
        if (ok) {
            Method m;
            m.name = key;
            m.tsym = "?$" + name + "@" + codes;
            m.ret = decl.type;
            m.params = decl.params;
            m.param_names = decl.param_names;
            m.is_static = mt.is_static;
            if (accept(TConst)) m.is_const = true;
            if (!is(TLBrace)) {
                error = "a member function template needs a body in the class";
                ok = false;
            } else {
                m.body_at = at;
                m.has_body = true;
                ok = add_method(&structs[static_cast<size_t>(owner)], owner, std::move(m));
                if (ok) {
                    const StructDef& od = structs[static_cast<size_t>(owner)];
                    for (size_t i = 0; i < od.methods.size(); ++i)
                        if (od.methods[i].name == key) mi = i;
                }
            }
        }
        at = saved_at;
        if (ok) ok = define_method(owner, mi);
        unbind_params(bound);
        unbind_params(ctx_bound);
        return ok;
    }

    // Each type parameter standing for itself (base 100 + index), each value parameter for 0.
    static std::vector<TArg> make_markers(const TDecl& td) {
        std::vector<TArg> markers;
        for (size_t i = 0; i < td.params.size(); ++i) {
            TArg m;
            if (td.params[i].is_type) {
                m.type.base = static_cast<int>(100 + i);
            } else {
                m.is_value = true;
                m.value = 0;
            }
            markers.push_back(m);
        }
        return markers;
    }

    // Fills the template arguments that were not given from the types of the call arguments
    // ([temp.deduct.call]); args holds the explicitly given ones on entry.
    bool deduce_args(const TDecl& td, const std::string& name, std::vector<TArg>* args_io,
                     const std::vector<Node>& call_args) {
        std::vector<TArg>& args = *args_io;
        std::vector<bool> given(td.params.size(), false);
        for (size_t i = 0; i < args.size(); ++i) given[i] = true;
        const std::vector<bool> explicit_given = given;
        args.resize(td.params.size());
        for (size_t i = 0; i < td.proto.size() && i < call_args.size() + 1; ++i) {
            const Type& pt = td.proto[i];
            if (pt.pack && pt.tpat < 0 && pt.base >= 100) {
                // Rest... rest takes every remaining argument ([temp.deduct.call], [temp.variadic]).
                const size_t pi = static_cast<size_t>(pt.base - 100);
                if (!explicit_given[pi]) {
                    TArg pa;
                    pa.is_pack = true;
                    for (size_t j = i; j < call_args.size(); ++j) {
                        Type t;
                        if (!static_type(call_args[j], &t)) {
                            error = "cannot deduce template arguments of " + name;
                            return false;
                        }
                        if (t.is_array()) {
                            t = peel_array(t);
                            t.pointers += 1;
                            t.array_len = -1;
                        }
                        t.is_ref = false;
                        pa.pack.push_back(t);
                    }
                    args[pi] = std::move(pa);
                    given[pi] = true;
                }
                break;
            }
            if (i >= call_args.size()) break;
            if (pt.tpat == -2 || (pt.base < 100 && pt.tpat < 0)) continue;
            if (pt.tpat < 0 && explicit_given[static_cast<size_t>(pt.base - 100)]) continue;
            Type at_type;
            if (!static_type(call_args[i], &at_type)) {
                error = "cannot deduce template arguments of " + name;
                return false;
            }
            if (at_type.is_array()) {
                at_type = peel_array(at_type);
                at_type.pointers += 1;
                at_type.array_len = -1;
            }
            at_type.is_ref = false;
            match_loose = true;
            const bool fits = match_type(pt, at_type, &args, &given);
            match_loose = false;
            if (!fits) {
                error = "cannot deduce template arguments of " + name + " from argument " + std::to_string(i + 1);
                return false;
            }
        }
        for (size_t i = 0; i < td.params.size(); ++i)
            if (!given[i] && td.params[i].is_pack) {
                args[i].is_pack = true;  // a pack with no arguments
                given[i] = true;
            }
        for (size_t i = 0; i < td.params.size(); ++i)
            if (!given[i]) {
                error = "cannot deduce template argument " + td.params[i].name;
                return false;
            }
        return true;
    }

    // name<args>(...) or name(...): the instance of a function template, made the first time it is
    // called. Returns its name through *out.
    bool instantiate_function(const std::string& name, std::vector<TArg> args, const std::vector<Node>& call_args,
                              std::string* out) {
        const TDecl& td = pick_func_template(name, call_args.size());
        if (!deduce_args(td, name, &args, call_args)) return false;
        std::string key = name + "<";
        std::string codes;
        for (size_t i = 0; i < args.size(); ++i) {
            if (i) key += ",";
            key += arg_key(args[i]);
            codes += arg_code(args[i]);
        }
        key += ">";
        *out = key;
        for (const FuncDecl& fn : funcs)
            if (fn.name == key) return true;
        if (in_instance.count(key)) return true;  // a call inside its own body
        in_instance.insert(key);
        const size_t saved_at = at;
        // An explicit specialization is an ordinary function: no parameters are bound.
        const auto spec = func_specs.find(key);
        const auto saved = bind_params(td, spec != func_specs.end() ? std::vector<TArg>() : args);
        at = spec != func_specs.end() ? spec->second : td.start;
        bool imported = false;
        bool exported = false;
        Type type;
        bool ok = parse_type(&type, &imported, &exported);
        Declarator decl;
        if (ok) ok = parse_declarator(type, &decl);
        if (ok && !decl.is_function) {
            error = "expected a function template";
            ok = false;
        }
        if (ok) {
            FuncDecl fn;
            fn.name = key;
            fn.symbol = "??$" + name + "@" + codes + "@@YA" + cxx_type(decl.type, &structs);
            if (decl.params.empty()) fn.symbol += "X";
            else {
                for (const Type& param : decl.params) fn.symbol += cxx_type(param, &structs);
                fn.symbol += "@";
            }
            fn.symbol += "Z";
            fn.ret = decl.type;
            fn.params = decl.params;
            fn.param_names = decl.param_names;
            fn.vararg = decl.vararg;
            finish_signature(&fn);
            if (!parse_function_body(&fn, -1)) ok = false;
            else funcs.push_back(std::move(fn));
        }
        unbind_params(saved);
        at = saved_at;
        in_instance.erase(key);
        return ok;
    }
    std::set<std::string> in_instance{};


    // ---- exceptions (ISO/IEC 14882 [except]; not the MSVC C++ exception model) ----
    // A stack of records in the runtime (runtime/eh.c), the same scheme as tools/okaycc: an object
    // with a destructor pushes a record when it is built and pops it when it is destroyed; a try
    // block pushes a frame holding the registers saved by __okay_setjmp; throw copies the object into
    // runtime memory and unwinds the records, destroying objects, until a try frame is entered; its
    // handlers test the type name and continue the unwinding when none fits. It does not use .pdata
    // and cannot interoperate with code that throws through SEH or __CxxFrameHandler.
    bool eh_ = false;
    int eh_count = 0;

    Node eh_call(const std::string& fn, std::vector<Node> args) const {
        Node call;
        call.kind = Node::Call;
        call.text = fn;
        for (Node& a : args) call.kids.push_back(std::move(a));
        Node stmt;
        stmt.kind = Node::ExprStmt;
        stmt.kids.push_back(std::move(call));
        return stmt;
    }
    Node eh_call_value(const std::string& fn, std::vector<Node> args, const Type& ret) const {
        Node call;
        call.kind = Node::Call;
        call.text = fn;
        call.type = ret;
        for (Node& a : args) call.kids.push_back(std::move(a));
        return call;
    }
    static Node int_node(long long v) {
        Node n;
        n.kind = Node::IntLit;
        n.number = static_cast<uint64_t>(v);
        n.type.base = 1;
        return n;
    }
    static Type void_ptr() {
        Type t;
        t.base = 0;
        t.pointers = 1;
        return t;
    }
    static Node cast_node(const Type& t, Node inner) {
        Node c;
        c.kind = Node::Unary;
        c.type = t;
        c.kids.push_back(std::move(inner));
        return c;
    }
    // A hidden record (14 pointers) in the current function; returns its name.
    std::string eh_record_local(std::vector<Local>* locals) {
        Local rec;
        rec.name = "__eh" + std::to_string(eh_count++);
        rec.type = void_ptr();
        rec.type.array_len = 16;
        place_local(locals, rec);
        return rec.name;
    }
    Node rec_slot(const std::string& rec, int i) const {
        Node ix;
        ix.kind = Node::Index;
        ix.kids.push_back(node_name(rec));
        ix.kids.push_back(int_node(i));
        return ix;
    }
    Node rec_store(const std::string& rec, int i, Node value) const {
        Node a;
        a.kind = Node::Assign;
        a.op = TAssign;
        a.kids.push_back(rec_slot(rec, i));
        a.kids.push_back(cast_node(void_ptr(), std::move(value)));
        Node st;
        st.kind = Node::ExprStmt;
        st.kids.push_back(std::move(a));
        return st;
    }
    // After an object of class sid is built (count elements when count > 0): record it so that
    // unwinding destroys it. The nodes go to 'out'.
    bool eh_push_object(std::vector<Node>* out, Node obj_addr, int sid, int count, std::vector<Local>* locals) {
        if (!eh_ || locals == nullptr) return true;
        const Method* dm = find_method(sid, "<dtor>", 0);
        if (dm == nullptr) return true;
        const std::string rec = eh_record_local(locals);
        out->push_back(rec_store(rec, 1, int_node(count > 0 ? 1 : 0)));
        out->push_back(rec_store(rec, 2, std::move(obj_addr)));
        out->push_back(rec_store(rec, 3, node_name(dm->qname)));
        if (count > 0) {
            out->push_back(rec_store(rec, 4, int_node(count)));
            Type elem;
            elem.base = 3;
            elem.struct_id = sid;
            out->push_back(rec_store(rec, 5, int_node(size_of(elem))));
        }
        out->push_back(eh_call("__okay_eh_push", {node_name(rec)}));
        return true;
    }
    // The names a thrown type can be caught as: a class and then its bases, or the decorated type.
    std::string eh_type_names(const Type& t) const {
        if (t.base == 3 && t.pointers == 0 && t.struct_id >= 0) {
            std::string out;
            for (int walk = t.struct_id; walk >= 0; walk = structs[static_cast<size_t>(walk)].base) {
                Type one;
                one.base = 3;
                one.struct_id = walk;
                out += cxx_type(one, &structs) + "\n";
            }
            return out;
        }
        Type plain = t;
        plain.is_ref = false;
        plain.is_const = false;
        return cxx_type(plain, &structs);
    }
    void declare_eh_builtins() {
        auto add = [&](const std::string& name, Type ret, std::vector<Type> params) {
            for (const FuncDecl& fn : funcs)
                if (fn.name == name) return;
            FuncDecl fn;
            fn.name = name;
            fn.ret = ret;
            fn.params = params;
            fn.param_names.assign(params.size(), "");
            finish_signature(&fn);
            funcs.push_back(std::move(fn));
        };
        Type v;
        v.base = 0;
        Type i;
        i.base = 1;
        Type p = void_ptr();
        Type cp;
        cp.base = 2;
        cp.pointers = 1;
        add("__okay_eh_push", v, {p});
        add("__okay_eh_pop", v, {});
        add("__okay_eh_begin", v, {p});
        add("__okay_eh_end", v, {});
        add("__okay_eh_match", i, {cp});
        add("__okay_eh_obj", p, {});
        add("__okay_eh_resume", v, {});
        add("__okay_eh_rethrow", v, {});
        add("__okay_throw", v, {p, i, cp, p});
        add("__okay_eh_alloc", p, {i});
        add("__okay_throw_built", v, {p, i, cp, p});
    }

    static bool contains_return(const Node& n) {
        if (n.kind == Node::Return) return true;
        for (const Node& k : n.kids)
            if (contains_return(k)) return true;
        return false;
    }

    // try { } catch (T e) { } catch (...) { }
    bool parse_try(Node* out, std::vector<Local>* locals, int stmt_line) {
        next();  // try
        const std::string tr = eh_record_local(locals);
        Node block;
        block.kind = Node::Block;
        block.line = stmt_line;
        block.kids.push_back(rec_store(tr, 1, int_node(2)));
        block.kids.push_back(eh_call("__okay_eh_push", {node_name(tr)}));
        dscopes.emplace_back();
        DObj frame;
        frame.kind = 1;
        dscopes.back().push_back(frame);
        Node body;
        const bool body_ok = parse_block(&body, locals);
        dscopes.pop_back();
        if (!body_ok) return false;
        body.kids.push_back(eh_call("__okay_eh_pop", {}));
        struct Handler {
            bool all = false;
            std::string name;
            Node block;
        };
        std::vector<Handler> handlers;
        if (!(is(TIdent) && cur().text == "catch")) {
            error = "a try block needs a handler";
            return false;
        }
        while (is(TIdent) && cur().text == "catch") {
            next();
            if (!expect(TLParen, "'('")) return false;
            Handler h;
            dscopes.emplace_back();
            DObj hv;
            hv.kind = 2;
            dscopes.back().push_back(hv);
            Node hb;
            hb.kind = Node::Block;
            const std::string cr = eh_record_local(locals);
            hb.kids.push_back(rec_store(cr, 1, int_node(3)));
            hb.kids.push_back(eh_call("__okay_eh_begin", {node_name(cr)}));
            if (accept(TEllipsis)) {
                h.all = true;
            } else {
                accept(TConst);
                Type t;
                bool imp = false;
                bool exp = false;
                if (!parse_type(&t, &imp, &exp)) return false;
                accept(TConst);
                while (accept(TStar)) ++t.pointers;
                if (accept(TAmp)) t.is_ref = true;
                h.name = eh_type_names(t);
                if (!h.name.empty() && h.name.back() == '\n') h.name.pop_back();
                // Only the first name (the class itself) is the handler's type.
                const size_t nl = h.name.find('\n');
                if (nl != std::string::npos) h.name = h.name.substr(0, nl);
                if (is(TIdent)) {
                    Local var;
                    var.name = cur().text;
                    var.type = t;
                    next();
                    place_local(locals, var);
                    Type ptr = t;
                    ptr.is_ref = false;
                    ptr.pointers += 1;
                    Node obj = cast_node(ptr, eh_call_value("__okay_eh_obj", {}, void_ptr()));
                    Node deref;
                    deref.kind = Node::Unary;
                    deref.op = TStar;
                    deref.kids.push_back(std::move(obj));
                    Node assign;
                    assign.kind = Node::Assign;
                    assign.op = TAssign;
                    assign.number = 1;
                    assign.kids.push_back(node_name(var.name));
                    assign.kids.push_back(std::move(deref));
                    Node st;
                    st.kind = Node::ExprStmt;
                    st.kids.push_back(std::move(assign));
                    hb.kids.push_back(std::move(st));
                    if (obj_needs_dtor(t) && !t.is_ref) {
                        std::vector<Node> rec_nodes;
                        eh_push_object(&rec_nodes, node_addr(node_name(var.name)), t.struct_id, 0, locals);
                        for (Node& n : rec_nodes) hb.kids.push_back(std::move(n));
                        DObj obj_d;
                        obj_d.name = var.name;
                        obj_d.sid = t.struct_id;
                        dscopes.back().push_back(std::move(obj_d));
                    }
                }
            }
            if (!expect(TRParen, "')'")) return false;
            Node hbody;
            if (!parse_block(&hbody, locals)) return false;
            hb.kids.push_back(std::move(hbody));
            std::vector<Node> dt;
            if (!dtor_nodes(dscopes.size() - 1, &dt)) return false;
            for (Node& n : dt) hb.kids.push_back(std::move(n));
            dscopes.pop_back();
            h.block = std::move(hb);
            handlers.push_back(std::move(h));
        }
        // The chain: the first handler whose type fits, else keep unwinding.
        Node chain = eh_call("__okay_eh_resume", {});
        for (size_t i = handlers.size(); i-- > 0;) {
            Node iff;
            iff.kind = Node::If;
            if (handlers[i].all) {
                iff.kids.push_back(int_node(1));
            } else {
                Node str;
                str.kind = Node::StringLit;
                str.text = handlers[i].name;
                str.type.pointers = 1;
                str.type.base = 2;
                Type ir;
                ir.base = 1;
                iff.kids.push_back(eh_call_value("__okay_eh_match", {std::move(str)}, ir));
            }
            iff.kids.push_back(std::move(handlers[i].block));
            iff.kids.push_back(std::move(chain));
            chain = std::move(iff);
        }
        Node top;
        top.kind = Node::If;
        {
            Type ir;
            ir.base = 1;
            Node sj = eh_call_value("__okay_setjmp", {node_addr(rec_slot(tr, 6))}, ir);
            Node not_c;
            not_c.kind = Node::Unary;
            not_c.op = TNot;
            not_c.kids.push_back(std::move(sj));
            top.kids.push_back(std::move(not_c));
        }
        top.kids.push_back(std::move(body));
        top.kids.push_back(std::move(chain));
        block.kids.push_back(std::move(top));
        *out = std::move(block);
        return true;
    }

    // throw e;  throw;
    bool parse_throw(Node* out, std::vector<Local>* locals) {
        next();  // throw
        if (accept(TSemi)) {
            *out = eh_call("__okay_eh_rethrow", {});
            return true;
        }
        Node value;
        if (!parse_expr(0, &value)) return false;
        if (!expect(TSemi, "';'")) return false;
        return build_throw(std::move(value), locals, out);
    }

    // The statements that throw 'value': the exception object is a copy of it ([except.throw]).
    // A named class object with a copy constructor is copied by that constructor into storage from
    // the runtime; anything else is copied byte by byte (a temporary is built where it is used, as
    // C++17 requires, so it is not copied twice).
    bool build_throw(Node value, std::vector<Local>* locals, Node* out, bool force_prvalue = false) {
        Type t;
        if (!static_type(value, &t)) {
            error = "cannot tell the type of the thrown value";
            return false;
        }
        if (t.is_array()) {
            t = peel_array(t);
            t.pointers += 1;
            t.array_len = -1;
        }
        t.is_ref = false;
        Node block;
        block.kind = Node::Block;
        const bool named = value.kind == Node::Name || value.kind == Node::Field || value.kind == Node::Index ||
                           (value.kind == Node::Unary && value.op == TStar);
        const bool is_obj = t.base == 3 && t.pointers == 0 && t.struct_id >= 0;
        Node str;
        str.kind = Node::StringLit;
        str.text = eh_type_names(t);
        str.type.pointers = 1;
        str.type.base = 2;
        Node dtor = int_node(0);
        if (is_obj) {
            const Method* dm = find_method(t.struct_id, "<dtor>", 0);
            if (dm != nullptr) dtor = node_name(dm->qname);
        }
        // A temporary made by C(args) (hidden name __bt) is the exception object itself: no copy.
        const bool prvalue = force_prvalue || (value.kind == Node::Name && value.text.compare(0, 4, "__bt") == 0);
        if (named && !prvalue && is_obj && class_has_copy(t.struct_id)) {
            // __okay_eh_alloc, then the copy constructor builds the object in that storage.
            Type obj_ptr = t;
            obj_ptr.pointers = 1;
            Local mem;
            mem.name = "__thm" + std::to_string(eh_count++);
            mem.type = obj_ptr;
            place_local(locals, mem);
            Node assign;
            assign.kind = Node::Assign;
            assign.op = TAssign;
            assign.number = 1;
            assign.kids.push_back(node_name(mem.name));
            assign.kids.push_back(cast_node(obj_ptr, eh_call_value("__okay_eh_alloc", {int_node(std::max(1, size_of(t)))}, void_ptr())));
            Node st;
            st.kind = Node::ExprStmt;
            st.kids.push_back(std::move(assign));
            block.kids.push_back(std::move(st));
            Node ctor_call;
            std::vector<Node> ctor_args;
            ctor_args.push_back(std::move(value));
            if (!special_call(t.struct_id, "<ctor>", node_name(mem.name), std::move(ctor_args), &ctor_call)) return false;
            block.kids.push_back(std::move(ctor_call));
            block.kids.push_back(eh_call("__okay_throw_built", {cast_node(void_ptr(), node_name(mem.name)),
                                                                int_node(std::max(1, size_of(t))), std::move(str),
                                                                cast_node(void_ptr(), std::move(dtor))}));
            *out = std::move(block);
            return true;
        }
        // The object to copy: an object that has a name is used where it is, anything else goes
        // into a hidden local first.
        Node addr;
        if (named) {
            addr = node_addr(std::move(value));
        } else {
            Local tmp;
            tmp.name = "__th" + std::to_string(eh_count++);
            tmp.type = t;
            place_local(locals, tmp);
            Node assign;
            assign.kind = Node::Assign;
            assign.op = TAssign;
            assign.number = 1;
            assign.kids.push_back(node_name(tmp.name));
            assign.kids.push_back(std::move(value));
            Node st;
            st.kind = Node::ExprStmt;
            st.kids.push_back(std::move(assign));
            block.kids.push_back(std::move(st));
            addr = node_addr(node_name(tmp.name));
        }
        block.kids.push_back(eh_call("__okay_throw", {cast_node(void_ptr(), std::move(addr)),
                                                      int_node(std::max(1, size_of(t))), std::move(str),
                                                      cast_node(void_ptr(), std::move(dtor))}));
        *out = std::move(block);
        return true;
    }

    // throw as an expression (c ? 1 : throw E(); [expr.throw]): a call of a small function made once
    // per thrown type that throws its argument. A class object is passed by reference, so the copy
    // that becomes the exception object is made once.
    bool ensure_throw_helper(const Type& t, bool prvalue, std::string* name) {
        const bool is_obj = t.base == 3 && t.pointers == 0 && t.struct_id >= 0;
        *name = "__throwx_" + std::to_string(t.base) + "_" + std::to_string(t.struct_id) + "_" +
                std::to_string(t.pointers) + (t.is_unsigned ? "u" : "") + (prvalue ? "p" : "");
        for (const FuncDecl& known : funcs)
            if (known.name == *name) return true;
        FuncDecl fn;
        fn.name = *name;
        fn.ret.base = 0;
        Type pt = t;
        if (is_obj) pt.is_ref = true;
        fn.params.push_back(pt);
        fn.param_names.push_back("v");
        finish_signature(&fn);
        std::vector<Local> locals;
        Local lv;
        lv.name = "v";
        lv.type = fn.params[0];
        if (fn.param_ref[0]) {
            lv.type.pointers -= 1;
            lv.type.is_ref = true;
        }
        const int saved_depth = scope_depth;
        std::vector<Local>* saved_locals = cur_locals;
        scope_depth = 0;
        place_local(&locals, lv);
        cur_locals = &locals;
        Node body;
        body.kind = Node::Block;
        Node th;
        const bool ok = build_throw(node_name("v"), &locals, &th, prvalue);
        cur_locals = saved_locals;
        scope_depth = saved_depth;
        if (!ok) return false;
        body.kids.push_back(std::move(th));
        fn.body = std::move(body);
        fn.locals = std::move(locals);
        fn.nparams = static_cast<int>(fn.params.size());
        fn.defined = true;
        funcs.push_back(std::move(fn));
        return true;
    }

    // The constructor calls of the global objects, in declaration order, go into __okay_init; the
    // destructors (globals, then static locals, latest first) into __okay_fini. main (or entry)
    // calls them. Written from ISO/IEC 14882 [basic.start.init] and [basic.start.term].
    bool register_global_object(const std::string& name, const Type& type, std::vector<Node> args,
                                Node* array_init = nullptr) {
        std::vector<Local>* saved_locals = cur_locals;
        const int saved_depth = scope_depth;
        cur_locals = &init_locals;
        scope_depth = 0;
        const int sid = type.struct_id;
        bool ok = true;
        if (array_init != nullptr && ok) {
            std::vector<Node> calls;
            ok = array_init_calls(name, type, *array_init, &calls);
            if (ok) {
                for (Node& c : calls) init_body.kids.push_back(std::move(c));
                init_body.kind = Node::Block;
                init_used = true;
            }
        } else if (obj_needs_ctor(type)) {
            Node call;
            if (type.is_array()) {
                if (!type.more.empty() || !args.empty()) {
                    error = "a global array of objects needs one dimension and no arguments";
                    ok = false;
                } else {
                    ok = element_loop(name, type.array_len, false, sid, "<ctor>", &call);
                }
            } else {
                ok = special_call(sid, "<ctor>", node_addr(node_name(name)), std::move(args), &call);
            }
            if (ok) {
                if (init_body.kind != Node::Block) init_body.kind = Node::Block;
                init_body.kids.push_back(std::move(call));
                init_used = true;
            }
        }
        cur_locals = saved_locals;
        scope_depth = saved_depth;
        if (ok && obj_needs_dtor(type)) {
            GlobalObj g;
            g.name = name;
            g.sid = sid;
            g.count = type.is_array() ? type.array_len : 0;
            global_objs.push_back(std::move(g));
        }
        return ok;
    }

    // Wraps every return of fn so that __okay_fini runs after the value is computed.
    void rewrite_returns(Node& n, FuncDecl& fn) {
        if (n.kind == Node::Return) {
            Node call;
            call.kind = Node::Call;
            call.text = "__okay_fini";
            Node call_stmt;
            call_stmt.kind = Node::ExprStmt;
            call_stmt.kids.push_back(std::move(call));
            Node block;
            block.kind = Node::Block;
            block.line = n.line;
            if (!n.kids.empty()) {
                Local hidden;
                hidden.name = "__rv" + std::to_string(rv_count++);
                hidden.type = fn.ret;
                const int saved_depth = scope_depth;
                scope_depth = 0;
                place_local(&fn.locals, hidden);
                scope_depth = saved_depth;
                Node assign;
                assign.kind = Node::Assign;
                assign.op = TAssign;
                assign.number = 1;
                assign.kids.push_back(node_name(hidden.name));
                assign.kids.push_back(std::move(n.kids[0]));
                Node assign_stmt;
                assign_stmt.kind = Node::ExprStmt;
                assign_stmt.kids.push_back(std::move(assign));
                block.kids.push_back(std::move(assign_stmt));
                block.kids.push_back(std::move(call_stmt));
                Node ret;
                ret.kind = Node::Return;
                ret.kids.push_back(node_name(hidden.name));
                block.kids.push_back(std::move(ret));
            } else {
                block.kids.push_back(std::move(call_stmt));
                Node ret;
                ret.kind = Node::Return;
                block.kids.push_back(std::move(ret));
            }
            n = std::move(block);
            return;
        }
        for (Node& kid : n.kids) rewrite_returns(kid, fn);
    }

    bool finish_program() {
        for (GlobalObj& g : static_objs) global_objs.push_back(std::move(g));
        static_objs.clear();
        const bool need_fini = !global_objs.empty();
        if (!init_used && !need_fini) return true;
        auto make_fn = [&](const std::string& name, Node body, std::vector<Local> locals) {
            FuncDecl fn;
            fn.name = name;
            fn.ret.base = 0;
            finish_signature(&fn);
            fn.body = std::move(body);
            fn.locals = std::move(locals);
            fn.defined = true;
            funcs.push_back(std::move(fn));
        };
        if (init_used) {
            init_body.kind = Node::Block;
            make_fn("__okay_init", std::move(init_body), init_locals);
        }
        if (need_fini) {
            std::vector<Local> fini_locals;
            std::vector<Local>* saved_locals = cur_locals;
            const int saved_depth = scope_depth;
            cur_locals = &fini_locals;
            scope_depth = 0;
            Node body;
            body.kind = Node::Block;
            for (size_t i = global_objs.size(); i-- > 0;) {
                const GlobalObj& g = global_objs[i];
                Node call;
                if (g.count > 0) {
                    if (!element_loop(g.name, g.count, true, g.sid, "<dtor>", &call)) return false;
                } else if (!special_call(g.sid, "<dtor>", node_addr(node_name(g.name)), {}, &call)) {
                    return false;
                }
                if (!g.guard.empty()) {
                    Node test;
                    test.kind = Node::If;
                    test.kids.push_back(node_name(g.guard));
                    test.kids.push_back(std::move(call));
                    call = std::move(test);
                }
                body.kids.push_back(std::move(call));
            }
            cur_locals = saved_locals;
            scope_depth = saved_depth;
            make_fn("__okay_fini", std::move(body), std::move(fini_locals));
        }
        // main (or entry when there is no main) runs them.
        FuncDecl* start = nullptr;
        for (FuncDecl& fn : funcs)
            if (fn.name == "main" && fn.defined) start = &fn;
        if (start == nullptr)
            for (FuncDecl& fn : funcs)
                if (fn.name == "entry" && fn.defined) start = &fn;
        if (start == nullptr) return true;
        if (need_fini) {
            rewrite_returns(start->body, *start);
            Node call;
            call.kind = Node::Call;
            call.text = "__okay_fini";
            Node call_stmt;
            call_stmt.kind = Node::ExprStmt;
            call_stmt.kids.push_back(std::move(call));
            start->body.kids.push_back(std::move(call_stmt));
        }
        if (init_used) {
            Node call;
            call.kind = Node::Call;
            call.text = "__okay_init";
            Node call_stmt;
            call_stmt.kind = Node::ExprStmt;
            call_stmt.kids.push_back(std::move(call));
            start->body.kids.insert(start->body.kids.begin(), std::move(call_stmt));
        }
        return true;
    }

    bool parse() {
        lower_cxx_spellings();
        strip_namespace_qualifiers();
        for (const Token& t : tokens)
            if (t.kind == TIdent && (t.text == "try" || t.text == "throw")) eh_ = true;
        while (!is(TEnd)) {
            if (!ns_open.empty() && accept(TRBrace)) {
                for (size_t n = 0; n < ns_open.back(); ++n) ns_parts.pop_back();
                ns_open.pop_back();
                update_ns();
                continue;
            }
            if (is(TNamespace)) {
                next();
                size_t pushed = 0;
                if (is(TIdent)) {
                    do {  // namespace a::b { } ([namespace.def])
                        if (!is(TIdent)) {
                            error = "expected a namespace name";
                            return false;
                        }
                        ns_parts.push_back(cur().text);
                        ++pushed;
                        next();
                    } while (accept(TColonColon));
                }
                if (!expect(TLBrace, "'{'")) return false;
                ns_open.push_back(pushed);
                update_ns();
                continue;
            }
            if (is(TIdent) && cur().text == "template" && at + 1 < tokens.size() && tokens[at + 1].kind == TLt) {
                if (!parse_template_decl()) return false;
                continue;
            }
            if (explicit_member_def_ahead()) {
                if (!parse_template_decl(true)) return false;
                continue;
            }
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
            while (!is_typedef && is(TStar) && at + 2 < tokens.size() && tokens[at + 1].kind == TIdent &&
                   tokens[at + 2].kind == TColonColon) {
                ++type.pointers;
                next();
            }
            if (!is_typedef && is(TIdent) && at + 3 < tokens.size() && tokens[at + 1].kind == TColonColon &&
                class_name_at(at)) {
                // int C::f(int x) { ... } for a function declared in class C.
                const int sid = find_struct(cur().text);
                next();
                next();
                if (!is(TIdent)) {
                    error = "expected a member function name";
                    return false;
                }
                const std::string mname = cur().text;
                next();
                if (!is(TLParen)) {
                    // int P::n = 7; the definition of a static data member.
                    auto sm = structs[static_cast<size_t>(sid)].statics.find(mname);
                    if (sm == structs[static_cast<size_t>(sid)].statics.end()) {
                        error = "no static member " + mname + " in the class";
                        return false;
                    }
                    Node sinit;
                    bool has_init = false;
                    if (accept(TAssign)) {
                        if (!parse_init(&sinit)) return false;
                        has_init = true;
                    }
                    if (!expect(TSemi, "';'")) return false;
                    if (sm->second.has_const) continue;  // const P::k; the value is already known
                    for (Global& g : globals)
                        if (g.name == sm->second.symbol && has_init) {
                            g.bss = false;
                            g.init_bytes.clear();
                            if (!flatten_init(g.type, sinit, &g.init_bytes, 0)) return false;
                        }
                    continue;
                }
                next();
                Declarator decl;
                if (!parse_param_list(&decl)) return false;
                accept(TConst);
                const Method* found = find_method(sid, mname, decl.params.size());
                if (found == nullptr || found->owner != sid) {
                    error = "no member function " + mname + " in the class";
                    return false;
                }
                size_t mi = 0;
                StructDef& owner = structs[static_cast<size_t>(sid)];
                for (size_t i = 0; i < owner.methods.size(); ++i)
                    if (&owner.methods[i] == found) mi = i;
                if (owner.methods[mi].has_body) {
                    error = "member function " + mname + " is defined twice";
                    return false;
                }
                owner.methods[mi].has_body = true;
                owner.methods[mi].body_at = at;
                owner.methods[mi].param_names = decl.param_names;
                if (!skip_body()) return false;
                const size_t resume = at;
                if (!define_method(sid, mi)) return false;
                at = resume;
                continue;
            }
            if (!is_typedef && is(TSemi)) {
                next();
                continue;
            }
            Declarator decl;
            std::vector<Node> gargs;
            bool gctor_args = false;
            if (!is_typedef && (obj_needs_ctor(type) || obj_needs_dtor(type)) && is(TIdent) && at + 2 < tokens.size() &&
                tokens[at + 1].kind == TLParen) {
                const Tok after = tokens[at + 2].kind;
                const bool type_start = after == TRParen || after == TInt || after == TVoid || after == TChar ||
                                        after == TUnsigned || after == TSigned || after == TLong || after == TShort ||
                                        after == TStruct || after == TUnion || after == TEnum || after == TFloat ||
                                        after == TDouble || after == TConst;
                if (!type_start) {
                    // C g(args); a global object, the arguments go to a constructor.
                    decl.name = cur().text;
                    decl.type = type;
                    next();
                    next();
                    if (!parse_call_args(&gargs)) return false;
                    gctor_args = true;
                }
            }
            if (!gctor_args && !parse_declarator(type, &decl)) return false;
            if (is_typedef) {
                decl.type.is_const = is_const;
                typedefs[decl.name] = decl.type;
                if (!expect(TSemi, "';'")) return false;
                continue;
            }
            decl.type.is_const = is_const;
            if (!decl.is_function && (obj_needs_ctor(decl.type) || obj_needs_dtor(decl.type))) {
                Node ginit;
                bool has_ginit = false;
                if (is_static) {
                    error = "a global object with a constructor is not static";
                    return false;
                }
                if (accept(TAssign)) {
                    if (!decl.type.is_array() || !decl.type.more.empty() || !parse_init(&ginit)) {
                        error = error.empty() ? "a global object with a constructor takes arguments in parentheses" : error;
                        return false;
                    }
                    has_ginit = true;
                }
                if (!expect(TSemi, "';'")) return false;
                Global global;
                global.name = decl.name;
                global.type = decl.type;
                global.bss = true;
                globals.push_back(global);
                if (!register_global_object(decl.name, decl.type, std::move(gargs), has_ginit ? &ginit : nullptr))
                    return false;
                continue;
            }
            if (!decl.is_function) {
                Global global;
                global.name = decl.name;
                global.type = decl.type;
                global.is_static = is_static;
                if (accept(TAssign)) {
                    Node init;
                    if (!parse_init(&init)) return false;
                    global.bss = false;
                    if (!flatten_init(decl.type, init, &global.init_bytes, 0)) return false;
                }
                if (!expect(TSemi, "';'")) return false;
                globals.push_back(global);
                continue;
            }
            FuncDecl fn;
            fn.name = current_ns.empty() ? decl.name : current_ns + "::" + decl.name;
            if (!current_ns.empty()) fn.symbol = cxx_symbol(ns_mangled, decl.name, decl.type, decl.params);
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
            if (!parse_function_body(&fn, -1)) return false;
            funcs.push_back(std::move(fn));
        }
        if (!error.empty()) return false;
        if (eh_) declare_eh_builtins();
        return finish_program();
    }
};

bool Parser::parse_init(Node* out) {
    if (is(TLBrace)) {
        next();
        out->kind = Node::Block;
        out->text = "init";
        while (!is(TRBrace) && !is(TEnd)) {
            Node item;
            if (!parse_init(&item)) return false;
            out->kids.push_back(std::move(item));
            if (!accept(TComma)) break;
        }
        return expect(TRBrace, "'}'");
    }
    return parse_expr(1, out);
}

bool Parser::flatten_init(const Type& type, const Node& node, std::vector<uint8_t>* bytes, int at) {
    auto ensure = [&](int n) {
        if (static_cast<int>(bytes->size()) < n) bytes->resize(static_cast<size_t>(n), 0);
    };
    if (node.kind == Node::Block && node.text == "init") {
        if (type.array_len >= 0) {
            const Type elem = peel_array(type);
            const int step = std::max(1, size_of(elem));
            for (size_t i = 0; i < node.kids.size(); ++i) {
                if (!flatten_init(elem, node.kids[i], bytes, at + static_cast<int>(i) * step)) return false;
            }
            ensure(at + type.array_len * step);
            return true;
        }
        if (type.base == 3 && type.pointers == 0 && type.struct_id >= 0 &&
            type.struct_id < static_cast<int>(structs.size())) {
            const StructDef& def = structs[static_cast<size_t>(type.struct_id)];
            for (size_t i = 0; i < node.kids.size() && i < def.fields.size(); ++i) {
                const Field& field = def.fields[i];
                if (!flatten_init(field.type, node.kids[i], bytes, at + field.offset)) return false;
            }
            ensure(at + def.size);
            return true;
        }
        error = "initializer does not match the type";
        return false;
    }
    if (node.kind != Node::IntLit) {
        error = "global initializer must be a constant";
        return false;
    }
    const int width = std::max(1, size_of(type));
    ensure(at + width);
    const uint64_t value = node.number;
    for (int i = 0; i < width && i < 8; ++i)
        (*bytes)[static_cast<size_t>(at + i)] = static_cast<uint8_t>(value >> (8 * i));
    return true;
}

bool Parser::parse_block(Node* out, std::vector<Local>* locals) {
    if (!expect(TLBrace, "'{'")) return false;
    out->kind = Node::Block;
    out->text = "scope";
    push_scope();
    dscopes.emplace_back();
    while (!is(TRBrace) && !is(TEnd)) {
        Node stmt;
        if (!parse_stmt(&stmt, locals)) return false;
        out->kids.push_back(std::move(stmt));
    }
    {
        // Destroy this block's objects, latest first.
        std::vector<Node> dt;
        if (!dtor_nodes(dscopes.size() - 1, &dt)) return false;
        for (Node& n : dt) out->kids.push_back(std::move(n));
        dscopes.pop_back();
    }
    pop_scope();
    return expect(TRBrace, "'}'");
}

// A statement, preceded by the assignments that give hidden locals their values for references
// bound to temporaries (a reference parameter given an expression that is not an object).
bool Parser::parse_stmt(Node* out, std::vector<Local>* locals) {
    std::vector<Node> outer = std::move(stmt_pre);
    stmt_pre.clear();
    const bool loop = is(TWhile) || is(TFor) || is(TDo);
    const bool ok = parse_stmt_core(out, locals);
    std::vector<Node> mine = std::move(stmt_pre);
    stmt_pre = std::move(outer);
    if (!ok || mine.empty()) return ok;
    if (loop) {
        error = "a reference bound to a temporary in a loop condition is not supported";
        return false;
    }
    Node block;
    block.kind = Node::Block;
    block.line = out->line;
    for (Node& n : mine) block.kids.push_back(std::move(n));
    block.kids.push_back(std::move(*out));
    *out = std::move(block);
    return true;
}

bool Parser::parse_stmt_core(Node* out, std::vector<Local>* locals) {
    const int stmt_line = cur().line;
    std::vector<Temp> outer = std::move(stmt_temps);
    stmt_temps.clear();
    const bool ok = parse_stmt_inner(out, locals, stmt_line);
    out->line = stmt_line;
    std::vector<Temp> mine = std::move(stmt_temps);
    stmt_temps = std::move(outer);
    if (!ok || mine.empty()) return ok;
    if ((out->kind == Node::If && out->kids.size() >= 2) || (out->kind == Node::While && out->kids.size() == 2)) {
        // The copies made in a condition die once the condition is evaluated: the value is kept
        // in a hidden local, the copies are destroyed, then the branch (or, for a loop, the body
        // or the exit) is chosen. A loop becomes while (1) { c = cond; dtors; if (!c) break; body }.
        Local hidden;
        hidden.name = "__cv" + std::to_string(rv_count++);
        Type ct;
        ct.base = 1;
        if (!static_type(out->kids[0], &ct) || ct.base == 3 || ct.is_array()) {
            ct = Type{};
            ct.base = 1;
        }
        ct.is_ref = false;
        hidden.type = ct;
        place_local(locals, hidden);
        Node assign;
        assign.kind = Node::Assign;
        assign.op = TAssign;
        assign.kids.push_back(node_name(hidden.name));
        assign.kids.push_back(std::move(out->kids[0]));
        Node assign_stmt;
        assign_stmt.kind = Node::ExprStmt;
        assign_stmt.kids.push_back(std::move(assign));
        Node block;
        block.kind = Node::Block;
        block.line = stmt_line;
        block.kids.push_back(std::move(assign_stmt));
        for (size_t i = mine.size(); i-- > 0;) {
            Node call;
            if (!special_call(mine[i].sid, "<dtor>", node_addr(node_name(mine[i].name)), {}, &call)) return false;
            block.kids.push_back(std::move(call));
        }
        if (out->kind == Node::If) {
            out->kids[0] = node_name(hidden.name);
            block.kids.push_back(std::move(*out));
        } else {
            Node inner;
            inner.kind = Node::Block;
            Node first;
            first = std::move(block);
            // first holds: assign, dtors. Add the exit test and the original body.
            Node not_c;
            not_c.kind = Node::Unary;
            not_c.op = TNot;
            not_c.kids.push_back(node_name(hidden.name));
            Node stop;
            stop.kind = Node::If;
            stop.kids.push_back(std::move(not_c));
            Node brk;
            brk.kind = Node::Break;
            stop.kids.push_back(std::move(brk));
            first.kids.push_back(std::move(stop));
            first.kids.push_back(std::move(out->kids[1]));
            Node forever;
            forever.kind = Node::While;
            Node one;
            one.kind = Node::IntLit;
            one.number = 1;
            one.type.base = 1;
            forever.kids.push_back(std::move(one));
            forever.kids.push_back(std::move(first));
            forever.line = stmt_line;
            *out = std::move(forever);
            return true;
        }
        *out = std::move(block);
        return true;
    }
    // Hidden copies made for this statement's arguments are destroyed after it, latest first.
    Node wrapped;
    wrapped.kind = Node::Block;
    wrapped.line = stmt_line;
    wrapped.kids.push_back(std::move(*out));
    for (size_t i = mine.size(); i-- > 0;) {
        Node call;
        if (!special_call(mine[i].sid, "<dtor>", node_addr(node_name(mine[i].name)), {}, &call)) return false;
        wrapped.kids.push_back(std::move(call));
    }
    *out = std::move(wrapped);
    return true;
}

bool Parser::parse_stmt_inner(Node* out, std::vector<Local>* locals, int stmt_line) {
    cur_locals = locals;
    if (is(TLBrace)) return parse_block(out, locals);
    if (is(TIdent) && cur().text == "try" && at + 1 < tokens.size() && tokens[at + 1].kind == TLBrace)
        return parse_try(out, locals, stmt_line);
    if (is(TIdent) && cur().text == "throw") return parse_throw(out, locals);
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
        brk_dbase.push_back(dscopes.size());
        cont_dbase.push_back(dscopes.size());
        const bool body_ok = parse_stmt(&body, locals);
        brk_dbase.pop_back();
        cont_dbase.pop_back();
        if (!body_ok) return false;
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
        brk_dbase.push_back(dscopes.size());
        cont_dbase.push_back(dscopes.size());
        const bool body_ok = parse_stmt(&body, locals);
        brk_dbase.pop_back();
        cont_dbase.pop_back();
        if (!body_ok) return false;
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
        brk_dbase.push_back(dscopes.size());
        cont_dbase.push_back(dscopes.size());
        const bool body_ok = parse_stmt(&body, locals);
        brk_dbase.pop_back();
        cont_dbase.pop_back();
        if (!body_ok) return false;
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
        brk_dbase.push_back(dscopes.size());
        struct PopBrk {
            std::vector<size_t>& v;
            ~PopBrk() { v.pop_back(); }
        } pop_brk{brk_dbase};
        while (!is(TRBrace) && !is(TEnd)) {
            Node arm;
            arm.kind = Node::Case;
            if (is(TCase)) {
                next();
                if (!parse_const(&arm.number, "a case value")) return false;
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
    if (is(TIdent) && cur().text == "delete") return parse_delete(out);
    if (is(TBreak) || is(TContinue)) {
        const bool is_break = is(TBreak);
        next();
        std::vector<Node> dt;
        const std::vector<size_t>& bases = is_break ? brk_dbase : cont_dbase;
        if (!bases.empty() && !dtor_nodes(bases.back(), &dt)) return false;
        Node jump;
        jump.kind = is_break ? Node::Break : Node::Continue;
        if (dt.empty()) {
            *out = std::move(jump);
        } else {
            out->kind = Node::Block;
            for (Node& n : dt) out->kids.push_back(std::move(n));
            out->kids.push_back(std::move(jump));
        }
        return expect(TSemi, "';'");
    }
    if (is(TReturn)) {
        next();
        std::vector<Node> dt;
        if (!dtor_nodes(fn_dbase, &dt)) return false;
        Node ret;
        ret.kind = Node::Return;
        Node value;
        bool has_value = false;
        if (!accept(TSemi)) {
            if (!parse_expr(0, &value)) return false;
            has_value = true;
            if (!expect(TSemi, "';'")) return false;
            if (cur_ret_ref) value = node_addr(std::move(value));  // a reference result is the address
            // Hidden argument copies of the return expression go first.
            std::vector<Node> tdt;
            for (size_t i = stmt_temps.size(); i-- > 0;) {
                Node call;
                if (!special_call(stmt_temps[i].sid, "<dtor>", node_addr(node_name(stmt_temps[i].name)), {}, &call))
                    return false;
                tdt.push_back(std::move(call));
            }
            stmt_temps.clear();
            dt.insert(dt.begin(), std::make_move_iterator(tdt.begin()), std::make_move_iterator(tdt.end()));
        } else if (cur_special == 1) {
            value = node_name("this");
            has_value = true;
        }
        if (dt.empty()) {
            if (has_value) ret.kids.push_back(std::move(value));
            *out = std::move(ret);
            return true;
        }
        // The value is computed first, then the objects are destroyed, then the function returns.
        out->kind = Node::Block;
        if (has_value) {
            Local hidden;
            hidden.name = "__rv" + std::to_string(rv_count++);
            hidden.type = cur_ret;
            place_local(locals, hidden);
            Node assign;
            assign.kind = Node::Assign;
            assign.op = TAssign;
            assign.number = 1;
            assign.kids.push_back(node_name(hidden.name));
            assign.kids.push_back(std::move(value));
            Node stmt;
            stmt.kind = Node::ExprStmt;
            stmt.kids.push_back(std::move(assign));
            out->kids.push_back(std::move(stmt));
            for (Node& n : dt) out->kids.push_back(std::move(n));
            ret.kids.push_back(node_name(hidden.name));
        } else {
            for (Node& n : dt) out->kids.push_back(std::move(n));
        }
        out->kids.push_back(std::move(ret));
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
        std::vector<Node> ctor_args;
        bool have_ctor_args = false;
        if (!is_typedef && obj_needs_ctor(type) && is(TIdent) && at + 1 < tokens.size() &&
            tokens[at + 1].kind == TLParen) {
            // C x(args); the arguments go to a constructor.
            decl.name = cur().text;
            decl.type = type;
            next();
            next();
            if (!parse_call_args(&ctor_args)) return false;
            have_ctor_args = true;
        } else if (!parse_declarator(type, &decl)) {
            return false;
        }
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
            if (!parse_init(&init)) return false;
            has_init = true;
            if (!is_static && wants_copy(decl.type, init)) {
                // C x = y; with y an object of the class: the copy constructor.
                ctor_args.push_back(std::move(init));
                have_ctor_args = true;
                has_init = false;
            }
        }
        if (is_static) {
            Global global;
            global.name = current_fn + "_" + decl.name + "_" + std::to_string(globals.size());
            global.type = decl.type;
            global.is_static = true;
            global.bss = true;
            if (has_init) {
                global.bss = false;
                if (!flatten_init(decl.type, init, &global.init_bytes, 0)) return false;
                has_init = false;
            }
            local.symbol = global.name;
            globals.push_back(global);
        }
        place_local(locals, local);
        out->kind = Node::Block;
        if (decl.type.base == 3 && decl.type.pointers == 0 && !decl.type.is_ref && decl.type.struct_id >= 0 &&
            structs[static_cast<size_t>(decl.type.struct_id)].poly) {
            // The table pointer is set where the object is declared (an array or static object gets
            // it from its constructors).
            if (!decl.type.is_array() && !is_static) {
            const StructDef& cls = structs[static_cast<size_t>(decl.type.struct_id)];
            Node table_name;
            table_name.kind = Node::Name;
            table_name.text = cls.vtable_sym;
            Node table_addr;
            table_addr.kind = Node::Unary;
            table_addr.op = TAmp;
            table_addr.kids.push_back(std::move(table_name));
            Node as_ptr;
            as_ptr.kind = Node::Unary;
            as_ptr.type.base = 0;
            as_ptr.type.pointers = 2;
            as_ptr.kids.push_back(std::move(table_addr));
            Node object;
            object.kind = Node::Name;
            object.text = decl.name;
            Node field;
            field.kind = Node::Field;
            field.op = TDot;
            field.text = "__vptr";
            field.kids.push_back(std::move(object));
            Node store;
            store.kind = Node::Assign;
            store.op = TAssign;
            store.kids.push_back(std::move(field));
            store.kids.push_back(std::move(as_ptr));
            Node stmt;
            stmt.kind = Node::ExprStmt;
            stmt.line = stmt_line;
            stmt.kids.push_back(std::move(store));
            out->kids.push_back(std::move(stmt));
            }
        }
        if (obj_needs_ctor(decl.type) || obj_needs_dtor(decl.type)) {
            const int sid = decl.type.struct_id;
            if (decl.type.is_array() && !decl.type.more.empty()) {
                error = "an array of objects needs one dimension";
                return false;
            }
            if (is_static) {
                // A static local object: built the first time control reaches the declaration (a
                // guard flag in a hidden global), destroyed by the program end function.
                if (has_init || have_ctor_args || decl.type.is_array()) {
                    error = "a static local object takes no initializer and is not an array";
                    return false;
                }
                Global guard;
                guard.name = local.symbol + "_guard";
                guard.type.base = 1;
                guard.bss = true;
                guard.is_static = true;
                globals.push_back(guard);
                Node set;
                set.kind = Node::Assign;
                set.op = TAssign;
                set.kids.push_back(node_name(guard.name));
                Node one;
                one.kind = Node::IntLit;
                one.number = 1;
                one.type.base = 1;
                set.kids.push_back(std::move(one));
                Node set_stmt;
                set_stmt.kind = Node::ExprStmt;
                set_stmt.kids.push_back(std::move(set));
                Node then_block;
                then_block.kind = Node::Block;
                then_block.kids.push_back(std::move(set_stmt));
                if (obj_needs_ctor(decl.type)) {
                    Node call;
                    if (!special_call(sid, "<ctor>", node_addr(node_name(local.symbol)), {}, &call)) return false;
                    then_block.kids.push_back(std::move(call));
                }
                Node not_g;
                not_g.kind = Node::Unary;
                not_g.op = TNot;
                not_g.kids.push_back(node_name(guard.name));
                Node test;
                test.kind = Node::If;
                test.kids.push_back(std::move(not_g));
                test.kids.push_back(std::move(then_block));
                out->kids.push_back(std::move(test));
                if (obj_needs_dtor(decl.type)) {
                    GlobalObj g;
                    g.name = local.symbol;
                    g.sid = sid;
                    g.guard = guard.name;
                    static_objs.push_back(std::move(g));
                }
                return expect(TSemi, "';'");
            }
            if (decl.type.is_array()) {
                if (has_init) {
                    std::vector<Node> calls;
                    if (!array_init_calls(decl.name, decl.type, init, &calls)) return false;
                    for (Node& c : calls) out->kids.push_back(std::move(c));
                    has_init = false;
                } else if (obj_needs_ctor(decl.type)) {
                    Node loop;
                    if (!element_loop(decl.name, decl.type.array_len, false, sid, "<ctor>", &loop)) return false;
                    out->kids.push_back(std::move(loop));
                }
                if (obj_needs_dtor(decl.type)) {
                    std::vector<Node> rec_nodes;
                    eh_push_object(&rec_nodes, node_name(decl.name), sid, decl.type.array_len, locals);
                    for (Node& n : rec_nodes) out->kids.push_back(std::move(n));
                    DObj obj;
                    obj.name = decl.name;
                    obj.sid = sid;
                    obj.count = decl.type.array_len;
                    if (dscopes.empty()) dscopes.emplace_back();
                    dscopes.back().push_back(std::move(obj));
                }
                return expect(TSemi, "';'");
            }
            if (obj_needs_ctor(decl.type) && !has_init) {
                Node call;
                if (!special_call(decl.type.struct_id, "<ctor>", node_addr(node_name(decl.name)),
                                  std::move(ctor_args), &call))
                    return false;
                call.line = stmt_line;
                out->kids.push_back(std::move(call));
            } else if (have_ctor_args) {
                error = "constructor arguments together with an initializer";
                return false;
            }
            if (obj_needs_dtor(decl.type)) {
                std::vector<Node> rec_nodes;
                eh_push_object(&rec_nodes, node_addr(node_name(decl.name)), decl.type.struct_id, 0, locals);
                for (Node& n : rec_nodes) out->kids.push_back(std::move(n));
                DObj obj;
                obj.name = decl.name;
                obj.sid = decl.type.struct_id;
                if (dscopes.empty()) dscopes.emplace_back();
                dscopes.back().push_back(std::move(obj));
            }
        }
        if (has_init) {
            Node assign;
            assign.kind = Node::Assign;
            assign.number = 1;
            Node name;
            name.kind = Node::Name;
            name.text = decl.name;
            assign.kids.push_back(std::move(name));
            assign.kids.push_back(std::move(init));
            Node stmt;
            stmt.kind = Node::ExprStmt;
            stmt.line = stmt_line;
            stmt.kids.push_back(std::move(assign));
            out->kids.push_back(std::move(stmt));
        }
        return expect(TSemi, "';'");
    }
    out->kind = Node::ExprStmt;
    Node expr;
    if (!parse_expr(0, &expr)) return false;
    out->kids.push_back(std::move(expr));
    return expect(TSemi, "';'");
}


bool Parser::local_visible(const std::string& name) const {
    if (cur_locals == nullptr) return false;
    for (int i = static_cast<int>(cur_locals->size()) - 1; i >= 0; --i) {
        const Local& local = (*cur_locals)[static_cast<size_t>(i)];
        if (local.name == name && scope_visible(local.scope)) return true;
    }
    return false;
}

// The static type of an expression, for the forms that name an object:
// names, fields, elements, dereferences, address-of, casts and calls.
bool Parser::static_type(const Node& node, Type* out) const {
    switch (node.kind) {
    case Node::Name:
        if (cur_locals != nullptr) {
            for (int i = static_cast<int>(cur_locals->size()) - 1; i >= 0; --i) {
                const Local& local = (*cur_locals)[static_cast<size_t>(i)];
                if (local.name == node.text && scope_visible(local.scope)) {
                    *out = local.type;
                    return true;
                }
            }
        }
        for (const Global& global : globals)
            if (global.name == node.text) {
                *out = global.type;
                return true;
            }
        return false;
    case Node::Field: {
        Type base;
        if (node.kids.empty() || !static_type(node.kids[0], &base)) return false;
        if (node.op == TArrow) {
            if (base.pointers < 1) return false;
            base = peel_array(base);
        }
        if (base.base != 3 || base.struct_id < 0 || base.struct_id >= static_cast<int>(structs.size())) return false;
        for (const Field& field : structs[static_cast<size_t>(base.struct_id)].fields)
            if (field.name == node.text) {
                *out = field.type;
                return true;
            }
        return false;
    }
    case Node::Index: {
        Type base;
        if (node.kids.empty() || !static_type(node.kids[0], &base)) return false;
        *out = peel_array(base);
        return true;
    }
    case Node::Unary: {
        if (node.op == TEnd) {
            *out = node.type;
            return true;
        }
        Type inner;
        if (node.kids.empty() || !static_type(node.kids[0], &inner)) return false;
        if (node.op == TStar) {
            inner.is_ref = false;
            *out = peel_array(inner);
            return true;
        }
        if (node.op == TAmp) {
            inner.is_ref = false;
            if (inner.is_array()) {
                inner = peel_array(inner);
            }
            ++inner.pointers;
            *out = inner;
            return true;
        }
        return false;
    }
    case Node::IntLit:
    case Node::StringLit:
        *out = node.type;
        return true;
    case Node::Binary: {
        if (node.kids.size() != 2) return false;
        if (node.op == TEq || node.op == TNe || node.op == TLt || node.op == TGt || node.op == TLe ||
            node.op == TGe || node.op == TAndAnd || node.op == TOrOr) {
            *out = Type{};
            out->base = 1;
            return true;
        }
        Type left;
        if (!static_type(node.kids[0], &left)) return false;
        *out = left;
        return true;
    }
    case Node::Ternary: {
        if (node.kids.size() != 3 || !static_type(node.kids[1], out)) return false;
        if (out->base == 0 && out->pointers == 0) return static_type(node.kids[2], out);  // c ? throw x : y
        return true;
    }
    case Node::Call:
        if (node.number == 1) {
            *out = node.type;
            return true;
        }
        if (!node.text.empty()) {
            for (const FuncDecl& fn : funcs)
                if (fn.name == node.text) {
                    *out = fn.ret;
                    return true;
                }
        }
        return false;
    default:
        return false;
    }
}

// After an argument: 'rest...' stands for every parameter the pack was expanded to.
bool Parser::expand_pack_arg(Node arg, std::vector<Node>* args) {
    if (is(TEllipsis) && arg.kind == Node::Name && param_packs.count(arg.text) > 0) {
        next();
        for (const std::string& n : param_packs[arg.text]) args->push_back(node_name(n));
        return true;
    }
    args->push_back(std::move(arg));
    return true;
}

// The parameter packs named by the tokens in [from, to).
std::vector<std::string> Parser::packs_in(size_t from, size_t to) const {
    std::vector<std::string> names;
    for (size_t i = from; i < to && i < tokens.size(); ++i)
        if (tokens[i].kind == TIdent && param_packs.count(tokens[i].text) > 0) {
            bool seen = false;
            for (const std::string& n : names)
                if (n == tokens[i].text) seen = true;
            if (!seen) names.push_back(tokens[i].text);
        }
    return names;
}

// Reads the tokens [from, to) as one expression with each pack named in them replaced by its
// k-th element ([temp.variadic]: the pattern is written once per element). k == npos reads the
// range as it is. 'unary' reads a cast-expression (a fold's operand), otherwise an assignment-expression.
bool Parser::parse_pack_element(size_t from, size_t to, size_t k, bool unary, Node* out) {
    const std::vector<std::string> names = packs_in(from, to);
    std::vector<Token> saved(tokens.begin() + static_cast<std::ptrdiff_t>(from),
                             tokens.begin() + static_cast<std::ptrdiff_t>(to));
    if (k != static_cast<size_t>(-1)) {
        for (size_t i = from; i < to; ++i)
            if (tokens[i].kind == TIdent && param_packs.count(tokens[i].text) > 0)
                tokens[i].text = param_packs[tokens[i].text][k];
    }
    const size_t was_at = at;
    at = from;
    const bool ok = unary ? parse_unary(out) : parse_expr(1, out);
    const bool whole = at == to;
    at = was_at;
    std::copy(saved.begin(), saved.end(), tokens.begin() + static_cast<std::ptrdiff_t>(from));
    if (!ok) return false;
    if (!whole) {
        error = "expected the pack expansion pattern to end before '...'";
        return false;
    }
    return true;
}

// A fold expression ([expr.prim.fold]) at '(': (xs op ...), (... op xs), (init op ... op xs) or
// (xs op ... op init). *folded tells whether one was read.
bool Parser::try_fold(Node* out, bool* folded) {
    *folded = false;
    const size_t open = at;
    size_t close = open;
    size_t e = 0;
    int depth = 0;
    for (size_t i = open; i < tokens.size() && tokens[i].kind != TEnd; ++i) {
        const Tok kind = tokens[i].kind;
        if (kind == TLParen || kind == TLBrack || kind == TLBrace) ++depth;
        else if (kind == TRParen || kind == TRBrack || kind == TRBrace) {
            if (--depth == 0) {
                close = i;
                break;
            }
        } else if (kind == TEllipsis && depth == 1 && e == 0 && !(i > open && tokens[i - 1].kind == TSizeof)) {
            e = i;
        }
    }
    if (close == open || e == 0) return true;
    const auto foldable = [this](Tok op) { return op == TComma || (prec(op) >= 3 && !is_assign_op(op)); };
    Tok op = TEnd;
    size_t pat_from = 0, pat_to = 0, init_from = 0, init_to = 0;
    bool left = false;
    bool has_init = false;
    // The operator after '...' (it may be two tokens, '>' '>') and the one before it.
    size_t after_w = 1, before_w = 1;
    const Tok after_op = e + 1 < close ? op_at(e + 1, &after_w) : TEnd;
    const Tok before_op = e >= 1 ? op_ending_at(e - 1, &before_w) : TEnd;
    if (e == open + 1 && e + 1 + after_w < close && foldable(after_op)) {
        op = after_op;
        left = true;
        pat_from = e + 1 + after_w;
        pat_to = close;
    } else if (e + 1 == close && e >= open + 2 + before_w && foldable(before_op)) {
        op = before_op;
        pat_from = open + 1;
        pat_to = e - before_w;
    } else if (e > open + 1 + before_w && e + 1 + after_w < close && foldable(before_op) && before_op == after_op) {
        op = before_op;
        has_init = true;
        if (!packs_in(e + 2, close).empty()) {
            left = true;
            init_from = open + 1;
            init_to = e - before_w;
            pat_from = e + 1 + after_w;
            pat_to = close;
        } else {
            pat_from = open + 1;
            pat_to = e - before_w;
            init_from = e + 1 + after_w;
            init_to = close;
        }
    } else {
        return true;
    }
    const std::vector<std::string> names = packs_in(pat_from, pat_to);
    if (names.empty()) {
        error = "a fold expression needs a parameter pack";
        return false;
    }
    const size_t count = param_packs[names[0]].size();
    for (const std::string& n : names)
        if (param_packs[n].size() != count) {
            error = "the packs of a fold expression differ in length";
            return false;
        }
    std::vector<Node> elems;
    for (size_t k = 0; k < count; ++k) {
        Node n;
        if (!parse_pack_element(pat_from, pat_to, k, true, &n)) return false;
        elems.push_back(std::move(n));
    }
    Node init;
    if (has_init && !parse_pack_element(init_from, init_to, static_cast<size_t>(-1), true, &init)) return false;
    const auto combine = [op](Node a, Node b) {
        Node bin;
        bin.kind = Node::Binary;
        bin.op = op;
        bin.kids.push_back(std::move(a));
        bin.kids.push_back(std::move(b));
        return bin;
    };
    if (elems.empty()) {
        if (has_init) *out = std::move(init);
        else if (op == TAndAnd || op == TOrOr) {
            out->kind = Node::IntLit;
            out->number = op == TAndAnd ? 1 : 0;
            out->type.base = 1;
        } else {
            error = "a fold over an empty pack needs an initial value";
            return false;
        }
    } else if (left) {
        Node acc = has_init ? combine(std::move(init), std::move(elems[0])) : std::move(elems[0]);
        for (size_t k = 1; k < elems.size(); ++k) acc = combine(std::move(acc), std::move(elems[k]));
        *out = std::move(acc);
    } else {
        Node acc = has_init ? combine(std::move(elems.back()), std::move(init)) : std::move(elems.back());
        for (size_t k = elems.size() - 1; k-- > 0;) acc = combine(std::move(elems[k]), std::move(acc));
        *out = std::move(acc);
    }
    at = close + 1;
    *folded = true;
    return true;
}

bool Parser::parse_call_args(std::vector<Node>* args) {
    if (accept(TRParen)) return true;
    do {
        {
            // f(g(xs)...): a pattern that is not a bare pack name is written once per element.
            const size_t start = at;
            size_t e = start;
            int depth = 0;
            for (; e < tokens.size() && tokens[e].kind != TEnd; ++e) {
                const Tok kind = tokens[e].kind;
                if (kind == TLParen || kind == TLBrack || kind == TLBrace) ++depth;
                else if (kind == TRParen || kind == TRBrack || kind == TRBrace) {
                    if (depth == 0) break;
                    --depth;
                } else if (kind == TComma && depth == 0) break;
            }
            const bool bare = e == start + 2 && tokens[start].kind == TIdent;
            if (e > start + 1 && tokens[e - 1].kind == TEllipsis && !bare) {
                const std::vector<std::string> names = packs_in(start, e - 1);
                if (!names.empty()) {
                    const size_t count = param_packs[names[0]].size();
                    for (const std::string& n : names)
                        if (param_packs[n].size() != count) {
                            error = "the packs of an expansion differ in length";
                            return false;
                        }
                    for (size_t k = 0; k < count; ++k) {
                        Node element;
                        if (!parse_pack_element(start, e - 1, k, false, &element)) return false;
                        args->push_back(std::move(element));
                    }
                    at = e;
                    continue;
                }
            }
        }
        Node arg;
        if (!parse_expr(1, &arg)) return false;
        if (!expand_pack_arg(std::move(arg), args)) return false;
    } while (accept(TComma));
    return expect(TRParen, "')'");
}

static bool has_effects(const Node& node) {
    if (node.kind == Node::Call || node.kind == Node::Assign) return true;
    if (node.kind == Node::Unary && (node.op == TPlusPlus || node.op == TMinusMinus)) return true;
    for (const Node& kid : node.kids)
        if (has_effects(kid)) return true;
    return false;
}

// "Base::f(args)": a direct call of that class's function on this.
bool Parser::qualified_call(Node* out, int cls, const std::string& name, std::vector<TArg> targs) {
    if (!expect(TLParen, "'('")) return false;
    std::vector<Node> args;
    if (!parse_call_args(&args)) return false;
    std::string method_name = name;
    int method_owner = cls;
    {
        int tmpl_owner = -1;
        if (find_member_tmpl(cls, name, &tmpl_owner) != nullptr && find_method(cls, name, args.size()) == nullptr) {
            if (!instantiate_member(cls, name, std::move(targs), args, &method_name, &method_owner)) return false;
        }
    }
    const Method* m = find_method(method_owner, method_name, args.size());
    if (m == nullptr) {
        error = "no member function " + name;
        return false;
    }
    if (!adjust_args(m->params, &args)) return false;
    if (cur_class < 0 && !m->is_static) {
        error = "a qualified member call needs this";
        return false;
    }
    Node call;
    call.kind = Node::Call;
    call.text = m->qname;
    call.number = 1;
    call.type = m->ret;
    if (!m->is_static) {
        Node self;
        self.kind = Node::Name;
        self.text = "this";
        call.kids.push_back(std::move(self));
    }
    for (Node& arg : args) call.kids.push_back(std::move(arg));
    *out = reference_result(std::move(call), m->ret.is_ref);
    return true;
}

// object.name(args) or object->name(args), with '(' already read. A virtual
// function reached through a pointer or reference is called through the
// object's table: the table pointer is the first field, the slot is an index.
bool Parser::method_call(Node* out, Node object, bool arrow, const std::string& name, std::vector<TArg> targs) {
    Type ot;
    if (!static_type(object, &ot)) {
        error = "cannot tell the class of the object for " + name;
        return false;
    }
    int sid = -1;
    if (arrow && ot.pointers == 1) sid = ot.struct_id;
    if (!arrow && ot.pointers == 0 && !ot.is_array()) sid = ot.struct_id;
    if (sid < 0 || ot.base != 3) {
        error = "member call on something that is not a class object: " + name;
        return false;
    }
    if (!arrow && object.kind == Node::Call && cur_locals != nullptr) {
        // A member call on a returned object (f().g()): the object goes into a hidden local first.
        Local hidden;
        hidden.name = "__bt" + std::to_string(tmp_count++);
        hidden.type = ot;
        place_local(cur_locals, hidden);
        Node assign;
        assign.kind = Node::Assign;
        assign.op = TAssign;
        assign.number = 1;
        assign.kids.push_back(node_name(hidden.name));
        assign.kids.push_back(std::move(object));
        Node stmt;
        stmt.kind = Node::ExprStmt;
        stmt.kids.push_back(std::move(assign));
        stmt_pre.push_back(std::move(stmt));
        if (class_needs_dtor(structs[static_cast<size_t>(sid)])) stmt_temps.push_back(Temp{hidden.name, sid});
        object = node_name(hidden.name);
    }
    std::vector<Node> args;
    if (!parse_call_args(&args)) return false;
    std::string method_name = name;
    int method_owner = sid;
    {
        int tmpl_owner = -1;
        if (find_member_tmpl(sid, name, &tmpl_owner) != nullptr && find_method(sid, name, args.size()) == nullptr) {
            if (!instantiate_member(sid, name, std::move(targs), args, &method_name, &method_owner)) return false;
        }
    }
    const Method* m = find_method(method_owner, method_name, args.size());
    if (m == nullptr) {
        error = "no member function " + name + " with " + std::to_string(args.size()) + " arguments";
        return false;
    }
    if (!adjust_args(m->params, &args)) return false;
    const bool dynamic = m->is_virtual && (arrow || ot.is_ref || (object.kind == Node::Unary && object.op == TStar));
    Node self;
    if (arrow) {
        self = std::move(object);
    } else {
        self.kind = Node::Unary;
        self.op = TAmp;
        self.kids.push_back(std::move(object));
    }
    Node call;
    call.kind = Node::Call;
    call.number = 1;
    call.type = m->ret;
    if (!dynamic) {
        call.text = m->qname;
    } else {
        if (has_effects(self)) {
            error = "a virtual call needs an object expression without side effects";
            return false;
        }
        FuncType ft;
        ft.ret = m->ret;
        Type self_ptr;
        self_ptr.base = 3;
        self_ptr.struct_id = m->owner;
        self_ptr.pointers = 1;
        ft.params.push_back(self_ptr);
        for (const Type& param : m->params) ft.params.push_back(param);
        Type fp;
        fp.base = 1;
        fp.pointers = 1;
        fp.func_id = static_cast<int>(func_types.size());
        func_types.push_back(ft);
        Node table;
        table.kind = Node::Field;
        table.op = TArrow;
        table.text = "__vptr";
        table.kids.push_back(self);
        Node slot;
        slot.kind = Node::IntLit;
        slot.number = static_cast<uint64_t>(m->slot);
        slot.type.base = 1;
        Node entry_addr;
        entry_addr.kind = Node::Binary;
        entry_addr.op = TPlus;
        entry_addr.kids.push_back(std::move(table));
        entry_addr.kids.push_back(std::move(slot));
        Node entry;
        entry.kind = Node::Unary;
        entry.op = TStar;
        entry.kids.push_back(std::move(entry_addr));
        Node target;
        target.kind = Node::Unary;
        target.type = fp;
        target.kids.push_back(std::move(entry));
        call.kids.push_back(std::move(target));
    }
    if (!m->is_static) call.kids.push_back(std::move(self));
    for (Node& arg : args) call.kids.push_back(std::move(arg));
    *out = reference_result(std::move(call), m->ret.is_ref);
    return true;
}

// __vnew_C(n) and __vdel_C(p): new C[n] and delete[] p for a class with a constructor or
// destructor. The block has an 8-byte header holding the count when the destructor must run per
// element. Built as syntax trees, once per class.
bool Parser::ensure_vector_helpers(int sid) {
    const StructDef& def = structs[static_cast<size_t>(sid)];
    const std::string new_name = "__vnew_" + def.name;
    for (const FuncDecl& fn : funcs)
        if (fn.name == new_name) return true;
    const bool cookie = class_needs_dtor(def);
    Type elem;
    elem.base = 3;
    elem.struct_id = sid;
    Type elem_ptr = elem;
    elem_ptr.pointers = 1;
    Type char_ptr;
    char_ptr.base = 2;
    char_ptr.pointers = 1;
    Type int_t;
    int_t.base = 1;
    auto lit = [](int v) {
        Node n;
        n.kind = Node::IntLit;
        n.number = static_cast<uint64_t>(v);
        n.type.base = 1;
        return n;
    };
    auto cast_to = [](const Type& t, Node inner) {
        Node c;
        c.kind = Node::Unary;
        c.type = t;
        c.kids.push_back(std::move(inner));
        return c;
    };
    auto assign_stmt = [](Node l, Node r) {
        Node a;
        a.kind = Node::Assign;
        a.op = TAssign;
        a.kids.push_back(std::move(l));
        a.kids.push_back(std::move(r));
        Node st;
        st.kind = Node::ExprStmt;
        st.kids.push_back(std::move(a));
        return st;
    };
    auto bin = [](Tok op, Node l, Node r) {
        Node b;
        b.kind = Node::Binary;
        b.op = op;
        b.kids.push_back(std::move(l));
        b.kids.push_back(std::move(r));
        return b;
    };
    auto push_fn = [&](FuncDecl fn, Node body, std::vector<Local> locals) {
        finish_signature(&fn);
        fn.body = std::move(body);
        fn.locals = std::move(locals);
        fn.nparams = static_cast<int>(fn.params.size());
        fn.defined = true;
        funcs.push_back(std::move(fn));
    };
    std::vector<Local>* saved_locals = cur_locals;
    const int saved_depth = scope_depth;
    scope_depth = 0;
    {
        FuncDecl fn;
        fn.name = new_name;
        fn.ret = elem_ptr;
        fn.params.push_back(int_t);
        fn.param_names.push_back("n");
        std::vector<Local> locals;
        Local ln;
        ln.name = "n";
        ln.type = int_t;
        place_local(&locals, ln);
        Local lr;
        lr.name = "raw";
        lr.type = char_ptr;
        place_local(&locals, lr);
        Local lq;
        lq.name = "q";
        lq.type = elem_ptr;
        place_local(&locals, lq);
        cur_locals = &locals;
        Node body;
        body.kind = Node::Block;
        Node alloc;
        alloc.kind = Node::Call;
        alloc.text = "malloc";
        Node bytes = bin(TStar, node_name("n"), lit(size_of(elem)));
        if (cookie) bytes = bin(TPlus, std::move(bytes), lit(8));
        alloc.kids.push_back(std::move(bytes));
        body.kids.push_back(assign_stmt(node_name("raw"), cast_to(char_ptr, std::move(alloc))));
        if (cookie) {
            Type int_ptr = int_t;
            int_ptr.pointers = 1;
            Node count_at;
            count_at.kind = Node::Unary;
            count_at.op = TStar;
            count_at.kids.push_back(cast_to(int_ptr, node_name("raw")));
            body.kids.push_back(assign_stmt(std::move(count_at), node_name("n")));
        }
        Node base = node_name("raw");
        if (cookie) base = bin(TPlus, std::move(base), lit(8));
        body.kids.push_back(assign_stmt(node_name("q"), cast_to(elem_ptr, std::move(base))));
        if (class_needs_ctor(def)) {
            Node loop;
            const Node count_node = node_name("n");
            if (!element_loop("q", 0, false, sid, "<ctor>", &loop, &count_node)) {
                cur_locals = saved_locals;
                scope_depth = saved_depth;
                return false;
            }
            body.kids.push_back(std::move(loop));
        }
        Node ret;
        ret.kind = Node::Return;
        ret.kids.push_back(node_name("q"));
        body.kids.push_back(std::move(ret));
        push_fn(std::move(fn), std::move(body), std::move(locals));
    }
    if (class_needs_dtor(def)) {
        FuncDecl fn;
        fn.name = "__vdel_" + def.name;
        fn.ret.base = 0;
        fn.params.push_back(elem_ptr);
        fn.param_names.push_back("p");
        std::vector<Local> locals;
        Local lp;
        lp.name = "p";
        lp.type = elem_ptr;
        place_local(&locals, lp);
        Local ln;
        ln.name = "n";
        ln.type = int_t;
        place_local(&locals, ln);
        cur_locals = &locals;
        Type int_ptr = int_t;
        int_ptr.pointers = 1;
        Node header = bin(TMinus, cast_to(char_ptr, node_name("p")), lit(8));
        Node count_at;
        count_at.kind = Node::Unary;
        count_at.op = TStar;
        count_at.kids.push_back(cast_to(int_ptr, std::move(header)));
        Node work;
        work.kind = Node::Block;
        work.kids.push_back(assign_stmt(node_name("n"), std::move(count_at)));
        Node loop;
        const Node count_node = node_name("n");
        if (!element_loop("p", 0, true, sid, "<dtor>", &loop, &count_node)) {
            cur_locals = saved_locals;
            scope_depth = saved_depth;
            return false;
        }
        work.kids.push_back(std::move(loop));
        Node release;
        release.kind = Node::Call;
        release.text = "free";
        release.kids.push_back(bin(TMinus, cast_to(char_ptr, node_name("p")), lit(8)));
        Node release_stmt;
        release_stmt.kind = Node::ExprStmt;
        release_stmt.kids.push_back(std::move(release));
        work.kids.push_back(std::move(release_stmt));
        Node guard;
        guard.kind = Node::If;
        guard.kids.push_back(node_name("p"));
        guard.kids.push_back(std::move(work));
        Node body;
        body.kind = Node::Block;
        body.kids.push_back(std::move(guard));
        push_fn(std::move(fn), std::move(body), std::move(locals));
    }
    cur_locals = saved_locals;
    scope_depth = saved_depth;
    return true;
}

// new T, new T(args): malloc, then the constructor (which returns its object). The program
// declares malloc and free itself (the runtime provides them). Written from ISO/IEC 14882
// [expr.new].
bool Parser::parse_new(Node* out) {
    next();
    Type ty;
    bool imp = false;
    bool exp = false;
    if (!parse_type(&ty, &imp, &exp)) return false;
    while (accept(TStar)) ++ty.pointers;
    if (accept(TLBrack)) {
        // new T[n]: n objects. A class with a constructor or destructor goes through __vnew_C
        // (a count in front of the block when destructors must run per element).
        Node count;
        if (!parse_expr(0, &count)) return false;
        if (!expect(TRBrack, "']'")) return false;
        const bool vobj = ty.base == 3 && ty.pointers == 0 && ty.struct_id >= 0;
        if (vobj && (class_needs_ctor(structs[static_cast<size_t>(ty.struct_id)]) ||
                     class_needs_dtor(structs[static_cast<size_t>(ty.struct_id)]))) {
            if (!ensure_vector_helpers(ty.struct_id)) return false;
            Node call;
            call.kind = Node::Call;
            call.text = "__vnew_" + structs[static_cast<size_t>(ty.struct_id)].name;
            call.type = ty;
            call.type.pointers = 1;
            call.kids.push_back(std::move(count));
            *out = std::move(call);
            return true;
        }
        Node size;
        size.kind = Node::IntLit;
        size.number = static_cast<uint64_t>(size_of(ty));
        size.type.base = 1;
        Node bytes;
        bytes.kind = Node::Binary;
        bytes.op = TStar;
        bytes.kids.push_back(std::move(count));
        bytes.kids.push_back(std::move(size));
        Node alloc;
        alloc.kind = Node::Call;
        alloc.text = "malloc";
        alloc.kids.push_back(std::move(bytes));
        Node cast;
        cast.kind = Node::Unary;
        cast.type = ty;
        cast.type.pointers += 1;
        cast.kids.push_back(std::move(alloc));
        *out = std::move(cast);
        return true;
    }
    std::vector<Node> args;
    if (accept(TLParen)) {
        if (!parse_call_args(&args)) return false;
    }
    const bool obj = ty.base == 3 && ty.pointers == 0 && ty.struct_id >= 0;
    const bool ctor = obj && class_needs_ctor(structs[static_cast<size_t>(ty.struct_id)]);
    if (!args.empty() && !ctor) {
        error = "new with arguments needs a class with a constructor";
        return false;
    }
    Node size;
    size.kind = Node::IntLit;
    size.number = static_cast<uint64_t>(size_of(ty));
    size.type.base = 1;
    Node alloc;
    alloc.kind = Node::Call;
    alloc.text = "malloc";
    alloc.kids.push_back(std::move(size));
    Node cast;
    cast.kind = Node::Unary;
    cast.type = ty;
    cast.type.pointers += 1;
    cast.kids.push_back(std::move(alloc));
    if (!ctor) {
        *out = std::move(cast);
        return true;
    }
    std::string tctor;
    if (!ensure_ctor_template(ty.struct_id, args, &tctor)) return false;
    const Method* m = find_ctor(ty.struct_id, args);
    if (!tctor.empty()) m = find_ctor_instance(ty.struct_id, tctor);
    if (m == nullptr) {
        error = "no matching constructor in " + structs[static_cast<size_t>(ty.struct_id)].name;
        return false;
    }
    if (!adjust_args(m->params, &args)) return false;
    Node call;
    call.kind = Node::Call;
    call.text = m->qname;
    call.number = 1;
    call.type = m->ret;
    call.kids.push_back(std::move(cast));
    for (Node& arg : args) call.kids.push_back(std::move(arg));
    *out = std::move(call);
    return true;
}

// delete p; the destructor, then free. A virtual destructor is reached through the table slot of
// the scalar deleting destructor, which frees the memory itself. A null pointer does nothing.
bool Parser::parse_delete(Node* out) {
    next();
    bool vec = false;
    if (is(TLBrack) && at + 1 < tokens.size() && tokens[at + 1].kind == TRBrack) {
        next();
        next();
        vec = true;
    }
    Node target;
    if (!parse_unary(&target)) return false;
    if (!expect(TSemi, "';'")) return false;
    Type ot;
    if (!static_type(target, &ot) || ot.pointers != 1) {
        error = "delete needs a pointer";
        return false;
    }
    if (has_effects(target)) {
        error = "delete needs a pointer expression without side effects";
        return false;
    }
    if (vec && ot.base == 3 && ot.struct_id >= 0 && class_needs_dtor(structs[static_cast<size_t>(ot.struct_id)])) {
        // delete[] p for objects with destructors: __vdel_C runs them (last to first) and frees.
        Node call;
        call.kind = Node::Call;
        call.text = "__vdel_" + structs[static_cast<size_t>(ot.struct_id)].name;
        call.kids.push_back(std::move(target));
        out->kind = Node::ExprStmt;
        out->kids.push_back(std::move(call));
        return true;
    }
    Node then_block;
    then_block.kind = Node::Block;
    const bool cls = ot.base == 3 && !vec && ot.struct_id >= 0 && class_needs_dtor(structs[static_cast<size_t>(ot.struct_id)]);
    const Method* dm = cls ? find_method(ot.struct_id, "<dtor>", 0) : nullptr;
    if (dm != nullptr && dm->is_virtual) {
        FuncType ft;
        ft.ret.base = 0;
        ft.ret.pointers = 1;
        Type self_ptr;
        self_ptr.base = 3;
        self_ptr.struct_id = dm->owner;
        self_ptr.pointers = 1;
        ft.params.push_back(self_ptr);
        Type flag;
        flag.base = 1;
        flag.is_unsigned = true;
        ft.params.push_back(flag);
        Type fp;
        fp.base = 1;
        fp.pointers = 1;
        fp.func_id = static_cast<int>(func_types.size());
        func_types.push_back(ft);
        Node table;
        table.kind = Node::Field;
        table.op = TArrow;
        table.text = "__vptr";
        table.kids.push_back(target);
        Node slot;
        slot.kind = Node::IntLit;
        slot.number = static_cast<uint64_t>(dm->slot);
        slot.type.base = 1;
        Node entry_addr;
        entry_addr.kind = Node::Binary;
        entry_addr.op = TPlus;
        entry_addr.kids.push_back(std::move(table));
        entry_addr.kids.push_back(std::move(slot));
        Node entry;
        entry.kind = Node::Unary;
        entry.op = TStar;
        entry.kids.push_back(std::move(entry_addr));
        Node fn_target;
        fn_target.kind = Node::Unary;
        fn_target.type = fp;
        fn_target.kids.push_back(std::move(entry));
        Node call;
        call.kind = Node::Call;
        call.number = 1;
        call.type = ft.ret;
        call.kids.push_back(std::move(fn_target));
        call.kids.push_back(target);
        Node one;
        one.kind = Node::IntLit;
        one.number = 1;
        one.type.base = 1;
        call.kids.push_back(std::move(one));
        Node stmt;
        stmt.kind = Node::ExprStmt;
        stmt.kids.push_back(std::move(call));
        then_block.kids.push_back(std::move(stmt));
    } else {
        if (dm != nullptr) {
            Node call;
            if (!special_call(ot.struct_id, "<dtor>", target, {}, &call)) return false;
            then_block.kids.push_back(std::move(call));
        }
        Node release;
        release.kind = Node::Call;
        release.text = "free";
        release.kids.push_back(target);
        Node stmt;
        stmt.kind = Node::ExprStmt;
        stmt.kids.push_back(std::move(release));
        then_block.kids.push_back(std::move(stmt));
    }
    out->kind = Node::If;
    out->kids.push_back(std::move(target));
    out->kids.push_back(std::move(then_block));
    return true;
}

bool Parser::parse_unary(Node* out) {
    if (is(TIdent) && cur().text == "new") {
        if (!parse_new(out)) return false;
        return parse_postfix(out);
    }
    if (is(TSizeof) && at + 1 < tokens.size() && tokens[at + 1].kind == TEllipsis) {
        // sizeof...(Ts) or sizeof...(rest): the number of elements of a pack ([expr.sizeof]).
        next();
        next();
        if (!expect(TLParen, "'('")) return false;
        if (!is(TIdent)) {
            error = "expected a pack name";
            return false;
        }
        long long count = -1;
        auto tp = type_packs.find(cur().text);
        if (tp != type_packs.end()) count = static_cast<long long>(tp->second.size());
        auto pp = param_packs.find(cur().text);
        if (count < 0 && pp != param_packs.end()) count = static_cast<long long>(pp->second.size());
        if (count < 0) {
            error = "sizeof... of something that is not a parameter pack";
            return false;
        }
        next();
        if (!expect(TRParen, "')'")) return false;
        out->kind = Node::IntLit;
        out->number = static_cast<uint64_t>(count);
        out->type.base = 1;
        return true;
    }
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
            if (!parse_brackets(&ty)) return false;
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
                // sizeof of any other expression: the size of its type, if the parser can tell it.
                Type et;
                if (static_type(inner, &et)) bytes = size_of(et);
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
    if (is(TIdent) && cur().text == "throw" && eh_) {
        next();
        out->kind = Node::Call;
        out->number = 1;
        out->type = Type{};
        out->type.base = 0;
        if (is(TSemi) || is(TColon) || is(TRParen) || is(TComma)) {  // throw;
            out->text = "__okay_eh_rethrow";
            return true;
        }
        Node value;
        if (!parse_expr(1, &value)) return false;
        Type vt;
        if (!static_type(value, &vt)) {
            error = "cannot tell the type of the thrown value";
            return false;
        }
        if (vt.is_array()) {
            vt = peel_array(vt);
            vt.pointers += 1;
            vt.array_len = -1;
        }
        vt.is_ref = false;
        std::string helper;
        const bool temp_operand = value.kind == Node::Name && value.text.compare(0, 4, "__bt") == 0;
        if (!ensure_throw_helper(vt, temp_operand, &helper)) return false;
        out->text = helper;
        out->kids.push_back(std::move(value));
        for (const FuncDecl& known : funcs) {
            if (known.name != helper) continue;
            std::vector<Type> source = known.params;
            for (size_t i = 0; i < source.size() && i < known.param_ref.size(); ++i)
                if (known.param_ref[i]) {
                    source[i].pointers -= 1;
                    source[i].is_ref = true;
                }
            if (!adjust_args(source, &out->kids)) return false;
            break;
        }
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
        std::string name = cur().text;
        next();
        int scope_sid = -1;
        if (is(TLt) && class_templates.count(name) > 0 && !local_visible(name)) {
            // Box<int>::member in an expression.
            std::vector<TArg> targs;
            if (!parse_template_args(class_templates[name], &targs, false)) return false;
            Type inst;
            if (!instantiate_class(name, targs, &inst)) return false;
            if (!is(TColonColon) || at + 1 >= tokens.size() || tokens[at + 1].kind != TIdent) {
                error = "expected :: and a member name after a class template instance";
                return false;
            }
            next();
            scope_sid = inst.struct_id;
            name = "<inst>::" + cur().text;
            next();
        } else {
            while (accept(TColonColon)) {  // a::b::c: namespaces, then a name
                if (!is(TIdent)) {
                    error = "expected a name after ::";
                    return false;
                }
                name += "::" + cur().text;
                next();
            }
        }
        const size_t colons = name.find("::");
        if (scope_sid < 0 && colons != std::string::npos) scope_sid = scope_class(name.substr(0, colons));
        if (scope_sid >= 0 && colons != std::string::npos) {
            const std::string member = name.substr(colons + 2);
            if (!is(TLParen) && static_member_expr(scope_sid, member, out)) return parse_postfix(out);
            std::vector<TArg> qtargs;
            if (is(TLt)) {
                int tmpl_owner = -1;
                const MemberTmpl* mt = find_member_tmpl(scope_sid, member, &tmpl_owner);
                if (mt != nullptr && !parse_template_args(mt->td, &qtargs, true)) return false;
            }
            return qualified_call(out, scope_sid, member, std::move(qtargs)) && parse_postfix(out);
        }
        if (cur_class >= 0 && !local_visible(name) && colons == std::string::npos) {
            const StructDef& def = structs[static_cast<size_t>(cur_class)];
            if (!is(TLParen) && static_member_expr(cur_class, name, out)) return parse_postfix(out);
            bool is_field = false;
            for (const Field& field : def.fields)
                if (field.name == name) is_field = true;
            Node self;
            self.kind = Node::Name;
            self.text = "this";
            if (is_field) {
                out->kind = Node::Field;
                out->op = TArrow;
                out->text = name;
                out->kids.push_back(std::move(self));
                return parse_postfix(out);
            }
            if (is(TLParen) && !def.methods.empty()) {
                bool is_method = false;
                for (const Method& m : def.methods)
                    if (m.name == name) is_method = true;
                int tmpl_owner = -1;
                if (find_member_tmpl(cur_class, name, &tmpl_owner) != nullptr) is_method = true;
                if (is_method) {
                    next();
                    return method_call(out, std::move(self), true, name) && parse_postfix(out);
                }
            }
        }
        if (is(TLParen) && colons == std::string::npos && !local_visible(name) && cur_locals != nullptr) {
            // C(args): a temporary object of a class built by a constructor ([expr.type.conv]). It is
            // made before the statement that uses it and destroyed after it.
            int csid = -1;
            auto td = typedefs.find(name);
            if (td != typedefs.end()) {
                if (td->second.base == 3 && td->second.pointers == 0 && td->second.struct_id >= 0)
                    csid = td->second.struct_id;
            } else if (class_name_text(name)) {
                csid = find_struct(name);
            }
            bool is_function = false;
            for (const FuncDecl& known : funcs)
                if (known.name == name) is_function = true;
            if (csid >= 0 && !is_function) {
                next();
                std::vector<Node> cargs;
                if (!parse_call_args(&cargs)) return false;
                Type ct;
                ct.base = 3;
                ct.struct_id = csid;
                Local hidden;
                hidden.name = "__bt" + std::to_string(tmp_count++);
                hidden.type = ct;
                place_local(cur_locals, hidden);
                Node ctor_stmt;
                if (!special_call(csid, "<ctor>", node_addr(node_name(hidden.name)), std::move(cargs), &ctor_stmt))
                    return false;
                stmt_pre.push_back(std::move(ctor_stmt));
                if (class_needs_dtor(structs[static_cast<size_t>(csid)])) stmt_temps.push_back(Temp{hidden.name, csid});
                *out = node_name(hidden.name);
                return parse_postfix(out);
            }
        }
        if (func_templates.count(name) > 0 && (is(TLt) || is(TLParen))) {
            std::vector<TArg> targs;
            if (is(TLt) && !parse_template_args(func_templates[name], &targs, true)) return false;
            if (!expect(TLParen, "'('")) return false;
            std::vector<Node> cargs;
            if (!parse_call_args(&cargs)) return false;
            std::string inst;
            if (!instantiate_function(name, std::move(targs), cargs, &inst)) return false;
            out->kind = Node::Call;
            out->text = inst;
            out->kids = std::move(cargs);
            for (const FuncDecl& known : funcs) {
                if (known.name != inst) continue;
                std::vector<Type> source = known.params;
                for (size_t i = 0; i < source.size() && i < known.param_ref.size(); ++i)
                    if (known.param_ref[i]) {
                        source[i].pointers -= 1;
                        source[i].is_ref = true;
                    }
                if (!adjust_args(source, &out->kids)) return false;
                if (known.ret_ref) *out = reference_result(std::move(*out), true);
                break;
            }
            return parse_postfix(out);
        }
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
                    if (!expand_pack_arg(std::move(arg), &out->kids)) return false;
                } while (accept(TComma));
                if (!expect(TRParen, "')'")) return false;
            }
            for (const FuncDecl& known : funcs) {
                if (known.name != name) continue;
                std::vector<Type> source = known.params;
                for (size_t i = 0; i < source.size() && i < known.param_ref.size(); ++i)
                    if (known.param_ref[i]) {
                        source[i].pointers -= 1;
                        source[i].is_ref = true;
                    }
                if (!adjust_args(source, &out->kids)) return false;
                if (known.ret_ref) *out = reference_result(std::move(*out), true);
                break;
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
                                (ahead == TIdent && typedefs.find(tokens[at + 1].text) != typedefs.end()) ||
                                (ahead == TIdent && class_name_at(at + 1));
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
            bool folded = false;
            if (!try_fold(out, &folded)) return false;
            if (!folded) {
                next();
                if (!parse_expr(0, out)) return false;
                if (!expect(TRParen, "')'")) return false;
            }
        }
    } else if (is(TStar) || is(TAmp) || is(TMinus) || is(TNot) || is(TTilde)) {
        out->kind = Node::Unary;
        out->op = cur().kind;
        next();
        Node inner;
        if (!parse_unary(&inner)) return false;
        const Tok uop = out->op;
        if ((uop == TMinus || uop == TTilde || uop == TNot) && inner.kind == Node::IntLit && inner.type.base < 5) {
            // A constant operand is folded, so -16, ~0 and !1 can be used where a constant is needed.
            const uint64_t v = inner.number;
            out->kind = Node::IntLit;
            out->op = TEnd;
            out->number = uop == TMinus ? 0 - v : uop == TTilde ? ~v : (v == 0 ? 1 : 0);
            out->type = Type{};
            out->type.base = 1;
            return parse_postfix(out);
        }
        out->kids.push_back(std::move(inner));
    } else {
        error = "expected an expression";
        return false;
    }
    return parse_postfix(out);
}

bool Parser::parse_postfix(Node* out) {
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
            // s.template add<int>(1): 'template' only says that a '<' after the name opens arguments.
            if (is(TIdent) && cur().text == "template" && at + 1 < tokens.size() && tokens[at + 1].kind == TIdent) next();
            if (!is(TIdent)) {
                error = "expected a field name";
                return false;
            }
            Node wrapped;
            wrapped.kind = Node::Field;
            wrapped.op = op;
            wrapped.text = cur().text;
            next();
            if (!is(TLParen)) {
                Type object_type;
                if (static_type(*out, &object_type) && object_type.base == 3 && object_type.struct_id >= 0 &&
                    static_member_expr(object_type.struct_id, wrapped.text, &wrapped)) {
                    *out = std::move(wrapped);  // obj.count: the object is not evaluated
                    continue;
                }
            }
            std::vector<TArg> member_targs;
            if (is(TLt) || is(TLParen)) {
                Type object_type;
                if (static_type(*out, &object_type) && object_type.base == 3 && object_type.struct_id >= 0) {
                    const int sid = object_type.struct_id;
                    bool is_method = false;
                    for (const Method& m : structs[static_cast<size_t>(sid)].methods)
                        if (m.name == wrapped.text) is_method = true;
                    int tmpl_owner = -1;
                    const MemberTmpl* mt = find_member_tmpl(sid, wrapped.text, &tmpl_owner);
                    if (mt != nullptr) {
                        is_method = true;
                        if (is(TLt) && !parse_template_args(mt->td, &member_targs, true)) return false;
                    }
                    if (is_method && is(TLParen)) {
                        next();
                        Node object = std::move(*out);
                        if (!method_call(out, std::move(object), op == TArrow, wrapped.text, std::move(member_targs)))
                            return false;
                        continue;
                    }
                }
            }
            wrapped.kids.push_back(std::move(*out));
            *out = std::move(wrapped);
        }
    }
    return true;
}

bool Parser::parse_expr(int min_prec, Node* out) {
    if (!parse_unary(out)) return false;
    while (true) {
        size_t op_width = 1;
        const Tok op = op_at(at, &op_width);
        const int p = prec(op);
        if (p < min_prec) break;
        for (size_t w = 0; w < op_width; ++w) next();
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
        const int next_min = is_assign_op(op) ? p : p + 1;
        if (!parse_expr(next_min, &right)) return false;
        Node bin;
        const bool compound = is_compound_op(op);
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
            } else if (op == TAmp) result = left & right_n;
            else if (op == TPipe) result = left | right_n;
            else if (op == TCaret) result = left ^ right_n;
            else if (op == TShl || op == TShr) {
                // A shift count is taken as an int; the left operand is an int, so a negative
                // value (all high bits set) shifts right with its sign ([expr.shift]).
                if (right_n >= 64) {
                    error = "shift count is too large";
                    return false;
                }
                if (op == TShl) result = left << right_n;
                else if ((left >> 32) == 0xFFFFFFFFull) result = static_cast<uint64_t>(static_cast<int64_t>(left) >> right_n);
                else result = left >> right_n;
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
    // First byte of the caller-made copies of big struct arguments.
    int struct_copy = 0;
    const std::vector<StructDef>* structs = nullptr;
    const std::vector<Global>* globals = nullptr;
    const std::vector<FuncType>* func_types = nullptr;
    const FuncDecl* current = nullptr;
    bool sret_dest_set = false;
    int sret_dest_off = 0;
    bool save_nv = false;
    bool optimize = false;
    // Offset and line (of the preprocessed text) for each statement of the
    // function being generated, when line records are wanted.
    bool debug_lines = false;
    std::vector<std::pair<uint32_t, int>> lines;
    std::vector<uint8_t> extra_pops;
    std::map<std::string, size_t> labels;
    std::vector<std::pair<std::string, size_t>> gotos;
    std::vector<std::vector<size_t>> break_stack;
    std::vector<std::vector<size_t>> continue_stack;
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
    void reload_r10(int off) {
        // The left operand of an integer operation lives in r10, so rcx can
        // hold a local. r10 is not given to a local.
        b(0x44);
        b(0x8B);
        b(0x95);
        disp(off);
        --temp_depth;
    }

    bool gen_expr(const Node& node);
    bool store_init(const Type& type, int offset, const Node& node);
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
    void mov_eax_from(int reg) {
        const int low = reg & 7;
        if (reg >= 8) b(0x44);
        b(0x89);
        b(static_cast<uint8_t>(0xC0 | (low << 3)));
    }
    void mov_reg_eax(int reg) {
        const int low = reg & 7;
        if (reg >= 8) b(0x41);
        b(0x89);
        b(static_cast<uint8_t>(0xC0 | low));
    }
    void mov_regs(int dest, int src) {
        uint8_t rex = 0;
        if (src >= 8) rex = static_cast<uint8_t>(rex | 0x44);
        if (dest >= 8) rex = static_cast<uint8_t>(rex | 0x41);
        if (rex != 0) b(rex);
        b(0x89);
        b(static_cast<uint8_t>(0xC0 | ((src & 7) << 3) | (dest & 7)));
    }
    static bool caller_saved(int reg) { return reg == 1 || reg == 2 || (reg >= 8 && reg <= 11); }
    void store_reg_local(int reg, int offset) {
        if (reg >= 8) b(0x44);
        b(0x89);
        b(static_cast<uint8_t>(0x85 | ((reg & 7) << 3)));
        disp(offset);
    }
    void load_reg_local(int reg, int offset) {
        if (reg >= 8) b(0x44);
        b(0x8B);
        b(static_cast<uint8_t>(0x85 | ((reg & 7) << 3)));
        disp(offset);
    }
    void spill_caller() {
        for (const Local& local : locals)
            if (caller_saved(local.reg)) store_reg_local(local.reg, local.offset);
    }
    void reload_caller() {
        for (const Local& local : locals)
            if (caller_saved(local.reg)) load_reg_local(local.reg, local.offset);
    }
    void spill_reg(int reg) {
        for (const Local& local : locals)
            if (local.reg == reg) store_reg_local(reg, local.offset);
    }
    void reload_reg(int reg) {
        for (const Local& local : locals)
            if (local.reg == reg) load_reg_local(reg, local.offset);
    }
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
    // eax = r10d op eax for & | ^ << >>. The left operand is in r10d and the right in eax, as for the
    // other integer operators. A shift count goes through cl, so rcx is saved around it (a local may
    // live there). A right shift of an unsigned left operand is logical, otherwise arithmetic.
    void emit_bit_op(Tok op, bool left_unsigned) {
        if (op == TAmp || op == TAndEq || op == TPipe || op == TOrEq || op == TCaret || op == TXorEq) {
            b(0x44);
            b(op == TAmp || op == TAndEq ? 0x21 : op == TPipe || op == TOrEq ? 0x09 : 0x31);
            b(0xD0);
            return;
        }
        spill_reg(1);
        b(0x89);  // mov ecx, eax
        b(0xC1);
        b(0x44);  // mov eax, r10d
        b(0x89);
        b(0xD0);
        b(0xD3);  // shl / shr / sar eax, cl
        b(op == TShl || op == TShlEq ? 0xE0 : left_unsigned ? 0xE8 : 0xF8);
        reload_reg(1);
    }
    static bool bit_op(Tok op) {
        return op == TAmp || op == TPipe || op == TCaret || op == TShl || op == TShr || op == TAndEq || op == TOrEq ||
               op == TXorEq || op == TShlEq || op == TShrEq;
    }

    // eax = (eax != 0): the value a conversion to bool gives ([conv.bool]).
    void normalize_bool() {
        b(0x85);  // test eax, eax
        b(0xC0);
        b(0x0F);  // setne al
        b(0x95);
        b(0xC0);
        b(0x0F);  // movzx eax, al
        b(0xB6);
        b(0xC0);
    }
    static bool is_bool_type(const Type& t) { return t.is_bool && t.pointers == 0 && !t.is_ref && !t.is_array(); }

    void cmp_set(uint8_t setcc) {
        b(0x41);
        b(0x39);
        b(0xC2);
        b(0x0F);
        b(setcc);
        b(0xC0);
        b(0x0F);
        b(0xB6);
        b(0xC0);
    }
};

int Gen::size_of(const Type& type) const {
    if (type.is_ref) return 8;
    if (type.array_len >= 0) return type.array_len * size_of(peel_array(type));
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

Type Gen::element_type(const Type& type) const { return peel_array(type); }

bool Gen::gen_addr(const Node& node) {
    if (node.kind == Node::Name) {
        if (Local* local = find_local(node.text)) {
            if (local->type.is_ref) {
                load_slot(local->offset, true);
                last_type = local->type;
                last_type.is_ref = false;
                return true;
            }
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
        const Node& inner = node.kids[0];
        const bool lvalue = inner.kind == Node::Name || inner.kind == Node::Field || inner.kind == Node::Index ||
                            (inner.kind == Node::Unary && inner.op == TStar);
        if (from_pointer || !lvalue) {
            if (!gen_expr(inner)) return false;
        } else {
            if (!gen_addr(inner)) return false;
            if (last_type.pointers > 0 && !last_type.is_array()) {
                // p[i] where p is a pointer field or a pointer element: the pointer is read first.
                b(0x48);
                b(0x8B);
                b(0x00);
            }
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
        type->is_bool = false;
        type->is_unsigned = false;
    }
}

bool Gen::store_init(const Type& type, int offset, const Node& node) {
    if (node.kind == Node::Block && node.text == "init") {
        if (type.array_len >= 0) {
            const Type elem = peel_array(type);
            const int step = std::max(1, size_of(elem));
            for (size_t i = 0; i < node.kids.size(); ++i) {
                if (!store_init(elem, offset + static_cast<int>(i) * step, node.kids[i])) return false;
            }
            return true;
        }
        if (type.base == 3 && type.pointers == 0 && structs != nullptr && type.struct_id >= 0 &&
            type.struct_id < static_cast<int>(structs->size())) {
            const StructDef& def = (*structs)[static_cast<size_t>(type.struct_id)];
            for (size_t i = 0; i < node.kids.size() && i < def.fields.size(); ++i) {
                const Field& field = def.fields[i];
                if (!store_init(field.type, offset + field.offset, node.kids[i])) return false;
            }
            return true;
        }
        error = "initializer does not match the type";
        return false;
    }
    if (node.kind != Node::IntLit) {
        error = "initializer must be a constant";
        return false;
    }
    const int width = type.pointers > 0 ? 8 : std::max(1, size_of(type));
    const uint32_t value = is_bool_type(type) ? (node.number != 0 ? 1u : 0u) : static_cast<uint32_t>(node.number);
    if (width == 1) {
        b(0xC6);
        b(0x85);
        disp(offset);
        b(static_cast<uint8_t>(value));
    } else if (width == 2) {
        b(0x66);
        b(0xC7);
        b(0x85);
        disp(offset);
        b(static_cast<uint8_t>(value));
        b(static_cast<uint8_t>(value >> 8));
    } else {
        b(0xC7);
        b(0x85);
        disp(offset);
        u32(value);
    }
    return true;
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
            if (local->type.is_ref) {
                load_slot(local->offset, true);
                Type referred = local->type;
                referred.is_ref = false;
                const int size = std::max(1, size_of(referred));
                load_from_rax(size >= 8 ? 8 : size, referred.is_unsigned);
                last_type = referred;
                promote(&last_type);
                return true;
            }
            if (local->reg != 0) {
                mov_eax_from(local->reg);
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
            relocs.push_back({0, static_cast<uint32_t>(text.size()), fn->symbol.empty() ? fn->name : fn->symbol, kRelAmd64Rel32});
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
            if (is_bool_type(to) && !from_fp) normalize_bool();
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
        if (node.op == TTilde) {
            if (last_type.pointers > 0 || last_type.base == 3 || last_type.base == 5 || last_type.base == 6) {
                error = "operator ~ needs an integer";
                return false;
            }
            b(0xF7);  // not eax
            b(0xD0);
            promote(&last_type);
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
        if (node.kids.size() == 2 && node.kids[1].kind == Node::Block && node.kids[1].text == "init" &&
            node.kids[0].kind == Node::Name) {
            if (Local* local = find_local(node.kids[0].text)) return store_init(local->type, local->offset, node.kids[1]);
            error = "initializer is not a local";
            return false;
        }
        if (node.kids[0].kind == Node::Name && (node.op == TAssign || node.op == TEnd)) {
            if (Local* local = find_local(node.kids[0].text)) {
                if (local->type.is_ref && node.number == 1) {
                    if (!gen_addr(node.kids[1])) return false;
                    const int addr = spill();
                    if (!error.empty()) return false;
                    b(0x48);
                    b(0x8B);
                    b(0x85);
                    disp(addr);
                    b(0x48);
                    b(0x89);
                    b(0x85);
                    disp(local->offset);
                    --temp_depth;
                    last_type = local->type;
                    last_type.is_ref = false;
                    return true;
                }
                if (local->reg != 0 && !is_bool_type(local->type)) {
                    if (!gen_expr(node.kids[1])) return false;
                    mov_reg_eax(local->reg);
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
        if (is_compound_op(node.op)) {
            if (!gen_addr(node.kids[0])) return false;
            const bool dest_unsigned = last_type.is_unsigned;
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
            reload_r10(left);
            if (bit_op(node.op)) {
                emit_bit_op(node.op, dest_unsigned);
            } else if (node.op == TPlusEq) {
                b(0x44);
                b(0x01);
                b(0xD0);
            } else if (node.op == TMinusEq) {
                b(0x41);
                b(0x29);
                b(0xC2);
                b(0x44);
                b(0x89);
                b(0xD0);
            } else if (node.op == TStarEq) {
                b(0x41);
                b(0x0F);
                b(0xAF);
                b(0xC2);
            } else {
                spill_reg(2);
                b(0x41);
                b(0x92);
                b(0x99);
                b(0x41);
                b(0xF7);
                b(0xFA);
                reload_reg(2);
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
        if (is_bool_type(last_type)) normalize_bool();
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
        if (node.text == "__okay_setjmp" && node.kids.size() == 1) {
            // The registers a restore must bring back go to the buffer: rbx rbp rdi rsi r12 to r15
            // rsp and the address after this sequence (the Windows x64 callee-saved set). The
            // sequence is entered again by __okay_longjmp with eax = 1.
            if (!gen_expr(node.kids[0])) return false;
            static const uint8_t save[] = {0x48, 0x89, 0xC1,                          // mov rcx, rax
                                           0x48, 0x89, 0x19,                          // mov [rcx], rbx
                                           0x48, 0x89, 0x69, 0x08,                    // mov [rcx+8], rbp
                                           0x48, 0x89, 0x79, 0x10,                    // mov [rcx+16], rdi
                                           0x48, 0x89, 0x71, 0x18,                    // mov [rcx+24], rsi
                                           0x4C, 0x89, 0x61, 0x20,                    // mov [rcx+32], r12
                                           0x4C, 0x89, 0x69, 0x28,                    // mov [rcx+40], r13
                                           0x4C, 0x89, 0x71, 0x30,                    // mov [rcx+48], r14
                                           0x4C, 0x89, 0x79, 0x38,                    // mov [rcx+56], r15
                                           0x48, 0x89, 0x61, 0x40,                    // mov [rcx+64], rsp
                                           0x48, 0x8D, 0x05, 0x06, 0x00, 0x00, 0x00,  // lea rax, [rip+6]
                                           0x48, 0x89, 0x41, 0x48,                    // mov [rcx+72], rax
                                           0x31, 0xC0};                               // xor eax, eax
            for (uint8_t c : save) b(c);
            last_type = Type{};
            last_type.base = 1;
            return true;
        }
        if (node.text == "__okay_longjmp" && node.kids.size() == 1) {
            if (!gen_expr(node.kids[0])) return false;
            static const uint8_t restore[] = {0x48, 0x89, 0xC1,                    // mov rcx, rax
                                              0xB8, 0x01, 0x00, 0x00, 0x00,        // mov eax, 1
                                              0x48, 0x8B, 0x19,                    // mov rbx, [rcx]
                                              0x48, 0x8B, 0x69, 0x08,              // mov rbp, [rcx+8]
                                              0x48, 0x8B, 0x79, 0x10,              // mov rdi, [rcx+16]
                                              0x48, 0x8B, 0x71, 0x18,              // mov rsi, [rcx+24]
                                              0x4C, 0x8B, 0x61, 0x20,              // mov r12, [rcx+32]
                                              0x4C, 0x8B, 0x69, 0x28,              // mov r13, [rcx+40]
                                              0x4C, 0x8B, 0x71, 0x30,              // mov r14, [rcx+48]
                                              0x4C, 0x8B, 0x79, 0x38,              // mov r15, [rcx+56]
                                              0x48, 0x8B, 0x61, 0x40,              // mov rsp, [rcx+64]
                                              0xFF, 0x61, 0x48};                   // jmp [rcx+72]
            for (uint8_t c : restore) b(c);
            last_type = Type{};
            return true;
        }
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
        Local* fp_local = node.text.empty() ? nullptr : find_local(node.text);
        if (fp_local != nullptr && fp_local->type.func_id < 0) fp_local = nullptr;
        const bool indirect = node.text.empty() || fp_local != nullptr;
        FuncDecl* fn = nullptr;
        FuncType sig;
        int callee_slot = 0;
        const int depth_before = temp_depth;
        size_t arg0 = 0;
        if (fp_local != nullptr) {
            load_slot(fp_local->offset, true);
            last_type = fp_local->type;
            if (func_types == nullptr || static_cast<size_t>(fp_local->type.func_id) >= func_types->size()) {
                error = "call target is not a function";
                return false;
            }
            sig = (*func_types)[static_cast<size_t>(fp_local->type.func_id)];
            callee_slot = spill();
            if (!error.empty()) return false;
        } else if (indirect) {
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
            } else if (named && is_bool_type(param) && !(last_type.pointers == 0 && (last_type.base == 5 || last_type.base == 6))) {
                normalize_bool();
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
        spill_caller();
        // rep movsb in the struct-argument copies below uses rsi and rdi, so
        // a local kept there is stored first and loaded back after the copies.
        spill_reg(6);
        spill_reg(7);
        int copy_at = struct_copy;
        for (size_t arg_i = 0; arg_i < nargs; ++arg_i) {
            const bool named = arg_i < sig.params.size();
            const Type param = named ? sig.params[arg_i] : Type{};
            if (!(named && param.base == 3 && param.pointers == 0 && !fits_register(param))) continue;
            const int bytes = std::max(1, size_of(param));
            const int step = (bytes + 7) & ~7;
            b(0x48);
            b(0x8B);
            b(0x85);
            disp(saved[arg_i]);
            b(0x48);
            b(0x89);
            b(0xC6);
            b(0x48);
            b(0x8D);
            b(0xBD);
            disp(copy_at);
            b(0xB9);
            u32(static_cast<uint32_t>(bytes));
            b(0xF3);
            b(0xA4);
            b(0x48);
            b(0x8D);
            b(0x85);
            disp(copy_at);
            b(0x48);
            b(0x89);
            b(0x85);
            disp(saved[arg_i]);
            copy_at += step;
        }
        reload_reg(6);
        reload_reg(7);
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
            relocs.push_back({0, static_cast<uint32_t>(text.size()), fn->symbol.empty() ? fn->name : fn->symbol, kRelAmd64Rel32});
            u32(0);
        }
        reload_caller();
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
        if ((left_type.pointers > 0 || left_type.is_array()) && node.op == TMinus && last_type.pointers == 0 &&
            !last_type.is_array()) {
            // pointer - integer: the count is scaled by the element size and the result is a
            // 64-bit pointer (ISO/IEC 14882 [expr.add]).
            const int scale = std::max(1, size_of(element_type(left_type)));
            b(0x48);
            b(0x69);
            b(0xC0);
            u32(static_cast<uint32_t>(scale));
            b(0x49);  // mov r10, rax
            b(0x89);
            b(0xC2);
            b(0x48);  // mov rax, [rbp+off]
            b(0x8B);
            b(0x85);
            disp(off);
            b(0x4C);  // sub rax, r10
            b(0x29);
            b(0xD0);
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
        reload_r10(off);
        if (node.op == TComma) {
            last_type = last_type;
            return true;
        }
        if (node.op == TPlus) {
            b(0x44);
            b(0x01);
            b(0xD0);
            last_type = left_type;
            return true;
        }
        if (node.op == TMinus) {
            b(0x41);
            b(0x29);
            b(0xC2);
            b(0x44);
            b(0x89);
            b(0xD0);
            return true;
        }
        if (node.op == TStar) {
            b(0x41);
            b(0x0F);
            b(0xAF);
            b(0xC2);
            return true;
        }
        if (node.op == TSlash || node.op == TPercent) {
            spill_reg(2);
            b(0x41);
            b(0x92);
            b(0x99);
            b(0x41);
            b(0xF7);
            b(0xFA);
            reload_reg(2);
            if (node.op == TPercent) {
                b(0x89);
                b(0xD0);
            }
            last_type = left_type;
            return true;
        }
        if (bit_op(node.op)) {
            if (left_type.pointers > 0 || left_type.base == 3 || last_type.pointers > 0 || last_type.base == 3) {
                error = "a bit operator needs integers";
                return false;
            }
            emit_bit_op(node.op, left_type.is_unsigned);
            const bool shift = node.op == TShl || node.op == TShr;
            // The result of a shift has the promoted type of the left operand; the others use the
            // usual arithmetic conversions, where unsigned wins.
            const bool uns = shift ? left_type.is_unsigned : left_type.is_unsigned || last_type.is_unsigned;
            last_type = Type{};
            last_type.base = 1;
            last_type.is_unsigned = uns;
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
    if (debug_lines && node.line > 0 && node.kind != Node::Block)
        lines.push_back({static_cast<uint32_t>(text.size()), node.line});
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
        if (current != nullptr && is_bool_type(current->ret) && !node.kids.empty() &&
            !(last_type.pointers == 0 && (last_type.base == 5 || last_type.base == 6)))
            normalize_bool();
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
        continue_stack.emplace_back();
        const size_t start = text.size();
        if (!gen_expr(node.kids[0])) return false;
        b(0x85);
        b(0xC0);
        const size_t jz = emit_jcc(0x84);
        if (!gen_stmt(node.kids[1])) return false;
        for (size_t site : continue_stack.back()) patch(site, start);
        const size_t jmp = emit_jmp();
        patch(jmp, start);
        const size_t end = text.size();
        patch(jz, end);
        for (size_t site : break_stack.back()) patch(site, end);
        break_stack.pop_back();
        continue_stack.pop_back();
        return true;
    }
    if (node.kind == Node::For) {
        break_stack.emplace_back();
        continue_stack.emplace_back();
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
        const size_t step_at = text.size();
        if (!skipped(node.kids[2]) && !gen_expr(node.kids[2])) return false;
        for (size_t site : continue_stack.back()) patch(site, step_at);
        const size_t jmp = emit_jmp();
        patch(jmp, start);
        const size_t end = text.size();
        if (has_cond) patch(jz, end);
        for (size_t site : break_stack.back()) patch(site, end);
        break_stack.pop_back();
        continue_stack.pop_back();
        return true;
    }
    if (node.kind == Node::Do) {
        break_stack.emplace_back();
        continue_stack.emplace_back();
        const size_t start = text.size();
        if (!gen_stmt(node.kids[0])) return false;
        const size_t cond_at = text.size();
        if (!gen_expr(node.kids[1])) return false;
        b(0x85);
        b(0xC0);
        const size_t jne = emit_jcc(0x85);
        patch(jne, start);
        for (size_t site : continue_stack.back()) patch(site, cond_at);
        const size_t end = text.size();
        for (size_t site : break_stack.back()) patch(site, end);
        break_stack.pop_back();
        continue_stack.pop_back();
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
        int case_min = 0;
        int case_max = 0;
        int case_count = 0;
        for (size_t i = 1; i < node.kids.size(); ++i) {
            if (node.kids[i].op == TDefault) {
                has_default = true;
                continue;
            }
            const int value = static_cast<int>(node.kids[i].number);
            if (case_count == 0) {
                case_min = value;
                case_max = value;
            } else {
                if (value < case_min) case_min = value;
                if (value > case_max) case_max = value;
            }
            ++case_count;
        }
        const int span = case_max - case_min;
        // A dense switch is a table of jmp instructions, one per value.
        if (case_count >= 4 && span >= 0 && span <= 8) {
            b(0x8B);
            b(0x85);
            disp(slot);
            if (case_min != 0) {
                b(0x2D);
                u32(static_cast<uint32_t>(case_min));
            }
            b(0x3D);
            u32(static_cast<uint32_t>(span));
            const size_t ja = emit_jcc(0x87);
            const size_t lea_disp = text.size() + 3;
            b(0x48);
            b(0x8D);
            b(0x0D);
            u32(0);
            b(0x6B);
            b(0xC0);
            b(0x05);
            b(0x48);
            b(0x01);
            b(0xC8);
            b(0xFF);
            b(0xE0);
            std::map<int, size_t> case_at;
            size_t default_at = 0;
            for (size_t i = 1; i < node.kids.size(); ++i) {
                if (node.kids[i].op == TDefault) default_at = text.size();
                else case_at[static_cast<int>(node.kids[i].number)] = text.size();
                for (const Node& stmt : node.kids[i].kids)
                    if (!gen_stmt(stmt)) return false;
            }
            const size_t table = text.size();
            const size_t hole = has_default ? default_at : 0;
            for (int v = case_min; v <= case_max; ++v) {
                size_t target = hole;
                const auto found = case_at.find(v);
                if (found != case_at.end()) target = found->second;
                b(0xE9);
                const size_t rel_at = text.size();
                u32(0);
                if (target != 0) patch(rel_at, target);
            }
            const size_t end = text.size();
            if (hole == 0) {
                for (int v = case_min; v <= case_max; ++v) {
                    if (case_at.count(v) != 0) continue;
                    // The hole jmp was left at 0. Point it at the end.
                }
                size_t cursor = table;
                for (int v = case_min; v <= case_max; ++v) {
                    if (case_at.count(v) == 0) patch(cursor + 1, end);
                    cursor += 5;
                }
            }
            patch(ja, has_default ? default_at : end);
            const int32_t lea_rel = static_cast<int32_t>(table - (lea_disp + 4));
            for (int i = 0; i < 4; ++i)
                text[lea_disp + static_cast<size_t>(i)] = static_cast<uint8_t>(static_cast<uint32_t>(lea_rel) >> (8 * i));
            for (size_t site : break_stack.back()) patch(site, end);
            break_stack.pop_back();
            --temp_depth;
            return true;
        }
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
    if (node.kind == Node::Continue) {
        if (continue_stack.empty()) {
            error = "continue is outside a loop";
            return false;
        }
        continue_stack.back().push_back(emit_jmp());
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

void kill_assigned(const Node& node, std::map<std::string, uint64_t>* known) {
    if (node.kind == Node::Assign && !node.kids.empty() && node.kids[0].kind == Node::Name) known->erase(node.kids[0].text);
    for (const Node& kid : node.kids) kill_assigned(kid, known);
}

void prop_node(Node* node, std::map<std::string, uint64_t>* known, const std::set<std::string>* shadowed) {
    if (node->kind == Node::If || node->kind == Node::While || node->kind == Node::For || node->kind == Node::Do) {
        std::map<std::string, uint64_t> inner = *known;
        for (Node& kid : node->kids) prop_node(&kid, &inner, shadowed);
        kill_assigned(*node, known);
        return;
    }
    if (node->kind == Node::Block || node->kind == Node::Label) {
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

bool contains_call(const Node& node) {
    if (node.kind == Node::Call) return true;
    for (const Node& kid : node.kids)
        if (contains_call(kid)) return true;
    return false;
}

const Node* single_return(const Node& body) {
    if (body.kind != Node::Block || body.kids.size() != 1) return nullptr;
    const Node& stmt = body.kids[0];
    if (stmt.kind != Node::Return || stmt.kids.size() != 1) return nullptr;
    if (contains_call(stmt.kids[0])) return nullptr;
    return &stmt.kids[0];
}

void subst_names(Node* node, const std::map<std::string, Node>& repl) {
    if (node->kind == Node::Name) {
        const auto found = repl.find(node->text);
        if (found != repl.end()) *node = found->second;
        return;
    }
    for (Node& kid : node->kids) subst_names(&kid, repl);
}

void inline_node(Node* node, const std::vector<FuncDecl>& funcs) {
    if (node->kind == Node::Call && !node->text.empty()) {
        const FuncDecl* fn = nullptr;
        for (const FuncDecl& item : funcs)
            if (item.name == node->text && item.defined) fn = &item;
        const bool simple = fn != nullptr && !fn->vararg && !fn->sret &&
                            (fn->param_names.empty() || fn->param_names[0] != "this") &&
                            static_cast<int>(node->kids.size()) == fn->nparams &&
                            static_cast<int>(fn->locals.size()) == fn->nparams;
        bool args_ok = simple;
        if (simple) {
            for (const Node& arg : node->kids) {
                if (arg.kind != Node::IntLit && arg.kind != Node::Name) args_ok = false;
            }
        }
        const Node* expr = simple && args_ok ? single_return(fn->body) : nullptr;
        if (expr != nullptr) {
            std::map<std::string, Node> repl;
            for (int i = 0; i < fn->nparams; ++i) repl[fn->param_names[static_cast<size_t>(i)]] = node->kids[static_cast<size_t>(i)];
            Node next = *expr;
            subst_names(&next, repl);
            *node = std::move(next);
            inline_node(node, funcs);
            return;
        }
    }
    for (Node& kid : node->kids) inline_node(&kid, funcs);
}

bool no_loop(const Node& node) {
    if (node.kind == Node::While || node.kind == Node::For || node.kind == Node::Do || node.kind == Node::Switch)
        return false;
    for (const Node& kid : node.kids)
        if (!no_loop(kid)) return false;
    return true;
}

void rename_names(Node* node, const std::map<std::string, std::string>& ren) {
    if (node->kind == Node::Name) {
        const auto found = ren.find(node->text);
        if (found != ren.end()) node->text = found->second;
    }
    for (Node& kid : node->kids) rename_names(&kid, ren);
}

void place_extra(FuncDecl* fn, Local local, const Gen& gen) {
    const int used = fn->locals.empty() ? 0 : -fn->locals.back().offset;
    int size = std::max(1, gen.size_of(local.type));
    int align = size >= 8 ? 8 : size >= 4 ? 4 : size >= 2 ? 2 : 1;
    const int aligned = (used + align - 1) & ~(align - 1);
    local.offset = -(aligned + size);
    local.scope = 0;
    local.reg = 0;
    fn->locals.push_back(local);
}

Node assign_name(const std::string& dest, Node rhs) {
    Node as;
    as.kind = Node::Assign;
    Node lhs;
    lhs.kind = Node::Name;
    lhs.text = dest;
    as.kids.push_back(std::move(lhs));
    as.kids.push_back(std::move(rhs));
    Node stmt;
    stmt.kind = Node::ExprStmt;
    stmt.kids.push_back(std::move(as));
    return stmt;
}

bool try_inline_stmt(Node* stmt, FuncDecl* caller, const std::vector<FuncDecl>& funcs, Gen& gen, int* seq) {
    Node* call = nullptr;
    std::string dest;
    bool is_return = false;
    bool drop = false;
    if (stmt->kind == Node::Return && stmt->kids.size() == 1 && stmt->kids[0].kind == Node::Call) {
        call = &stmt->kids[0];
        is_return = true;
    } else if (stmt->kind == Node::ExprStmt && !stmt->kids.empty() && stmt->kids[0].kind == Node::Assign &&
               stmt->kids[0].kids.size() == 2 && stmt->kids[0].kids[1].kind == Node::Call &&
               stmt->kids[0].kids[0].kind == Node::Name) {
        call = &stmt->kids[0].kids[1];
        dest = stmt->kids[0].kids[0].text;
    } else if (stmt->kind == Node::ExprStmt && !stmt->kids.empty() && stmt->kids[0].kind == Node::Call) {
        call = &stmt->kids[0];
        drop = true;
    } else {
        return false;
    }
    if (call->text.empty() || call->text == caller->name) return false;
    const FuncDecl* fn = nullptr;
    for (const FuncDecl& item : funcs)
        if (item.name == call->text && item.defined) fn = &item;
    if (fn == nullptr || fn->vararg || fn->sret || static_cast<int>(call->kids.size()) != fn->nparams) return false;
    // Member functions are not inlined: their body names this->field.
    if (!fn->param_names.empty() && fn->param_names[0] == "this") return false;
    if (!no_loop(fn->body) || contains_call(fn->body)) return false;
    std::map<std::string, std::string> ren;
    Node block;
    block.kind = Node::Block;
    for (int i = 0; i < fn->nparams; ++i) {
        const std::string tmp = "_arg" + std::to_string((*seq)++);
        Local local;
        local.name = tmp;
        local.type = fn->params[static_cast<size_t>(i)];
        place_extra(caller, local, gen);
        block.kids.push_back(assign_name(tmp, call->kids[static_cast<size_t>(i)]));
        if (i < static_cast<int>(fn->param_names.size())) ren[fn->param_names[static_cast<size_t>(i)]] = tmp;
    }
    for (const Local& local : fn->locals) {
        bool param = false;
        for (const std::string& pn : fn->param_names)
            if (pn == local.name) param = true;
        if (param || local.name.empty() || local.name[0] == '_') continue;
        const std::string tmp = "_loc" + std::to_string((*seq)++);
        Local copy = local;
        copy.name = tmp;
        copy.is_static = false;
        copy.symbol.clear();
        place_extra(caller, copy, gen);
        ren[local.name] = tmp;
    }
    const std::function<void(const Node&)> copy_stmts = [&](const Node& node) {
        if (node.kind == Node::Block) {
            for (const Node& kid : node.kids) copy_stmts(kid);
            return;
        }
        if (node.kind == Node::Return) {
            if (drop || node.kids.empty()) return;
            Node expr = node.kids[0];
            rename_names(&expr, ren);
            if (is_return) {
                Node ret;
                ret.kind = Node::Return;
                ret.kids.push_back(std::move(expr));
                block.kids.push_back(std::move(ret));
            } else {
                block.kids.push_back(assign_name(dest, std::move(expr)));
            }
            return;
        }
        Node cloned = node;
        rename_names(&cloned, ren);
        block.kids.push_back(std::move(cloned));
    };
    copy_stmts(fn->body);
    *stmt = std::move(block);
    return true;
}

void walk_inline(Node* node, FuncDecl* caller, const std::vector<FuncDecl>& funcs, Gen& gen, int* seq) {
    if (node->kind != Node::Block && node->kind != Node::If && node->kind != Node::While && node->kind != Node::For &&
        node->kind != Node::Do && node->kind != Node::Label)
        return;
    for (size_t i = 0; i < node->kids.size(); ++i) {
        if (!try_inline_stmt(&node->kids[i], caller, funcs, gen, seq)) walk_inline(&node->kids[i], caller, funcs, gen, seq);
        else walk_inline(&node->kids[i], caller, funcs, gen, seq);
    }
}

int struct_arg_space(const Node& node, const std::vector<FuncDecl>& funcs, const Gen& gen) {
    int best = 0;
    if (node.kind == Node::Call && !node.text.empty()) {
        const FuncDecl* fn = nullptr;
        for (const FuncDecl& item : funcs)
            if (item.name == node.text) fn = &item;
        if (fn != nullptr) {
            int sum = 0;
            const int n = std::min(static_cast<int>(node.kids.size()), static_cast<int>(fn->params.size()));
            for (int i = 0; i < n; ++i) {
                const Type& param = fn->params[static_cast<size_t>(i)];
                if (param.base == 3 && param.pointers == 0 && !gen.fits_register(param))
                    sum += (std::max(1, gen.size_of(param)) + 7) & ~7;
            }
            best = sum;
        }
    }
    for (const Node& kid : node.kids) best = std::max(best, struct_arg_space(kid, funcs, gen));
    return best;
}

// Where the lines of the preprocessed text came from, for the line table.
struct LineSource {
    bool enabled = false;
    const std::vector<std::pair<int, int>>* origin = nullptr;
    const std::vector<std::string>* files = nullptr;
};

void put32(std::vector<uint8_t>* out, uint32_t value) {
    for (int i = 0; i < 4; ++i) out->push_back(static_cast<uint8_t>(value >> (8 * i)));
}

bool emit_functions(std::vector<FuncDecl>& funcs, const std::vector<StructDef>& structs,
                    const std::vector<Global>& globals, const std::vector<FuncType>& func_types, bool optimize,
                    int opt_level, const LineSource& line_source, std::string* error, CompileOutput* out) {
    Gen gen(funcs);
    gen.debug_lines = line_source.enabled;
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
        std::vector<std::pair<uint32_t, int>> lines;
    };
    std::vector<PlacedFn> placed;
    std::vector<uint32_t> unwind_offs;
    if (opt_level >= 2) {
        int seq = 0;
        for (int pass = 0; pass < 4; ++pass) {
            for (FuncDecl& fn : funcs)
                if (fn.defined) walk_inline(&fn.body, &fn, funcs, gen, &seq);
            for (FuncDecl& fn : funcs)
                if (fn.defined) inline_node(&fn.body, funcs);
        }
    }
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
        const int copy_bytes = struct_arg_space(fn.body, funcs, gen);
        int frame = local_end + 64 + copy_bytes + outgoing;
        frame = (frame + 15) & ~15;
        if (frame < 0x20) frame = 0x20;
        bool needs_nv = false;
        for (const Local& local : fn.locals) {
            if (local.type.base == 3 && local.type.pointers == 0 && !gen.fits_register(local.type)) needs_nv = true;
        }
        {
            // A function with a try block keeps its locals in memory: a restore after a throw only
            // brings back the registers saved when the try block was entered.
            const std::function<bool(const Node&)> has_setjmp = [&](const Node& n) {
                if (n.kind == Node::Call && n.text == "__okay_setjmp") return true;
                for (const Node& k : n.kids)
                    if (has_setjmp(k)) return true;
                return false;
            };
            if (has_setjmp(fn.body)) needs_nv = true;
        }
        std::vector<int> extra_regs;
        if ((optimize || opt_level >= 1) && !needs_nv) {
            std::map<std::string, std::pair<int, int>> range;
            int stamp = 0;
            const std::function<void(const Node&)> walk = [&](const Node& node) {
                if (node.kind == Node::Name) {
                    const auto found = range.find(node.text);
                    if (found == range.end()) range[node.text] = {stamp, stamp};
                    else found->second.second = stamp;
                    ++stamp;
                }
                for (const Node& kid : node.kids) walk(kid);
            };
            walk(fn.body);
            struct Span {
                int index;
                int start;
                int end;
            };
            std::vector<Span> spans;
            for (int i = 0; i < static_cast<int>(fn.locals.size()); ++i) {
                Local& local = fn.locals[static_cast<size_t>(i)];
                if (local.is_static || local.name.empty() || local.name[0] == '_' || local.type.is_ref) continue;
                if (shadowed.count(local.name) != 0) continue;
                if (local.type.is_array() || local.type.pointers != 0 || local.type.base == 3 || local.type.base == 5 ||
                    local.type.base == 6)
                    continue;
                if (address_taken(fn.body, local.name)) continue;
                const auto found = range.find(local.name);
                if (found == range.end()) continue;
                spans.push_back({i, found->second.first, found->second.second});
            }
            std::sort(spans.begin(), spans.end(), [](const Span& a, const Span& b) { return a.start < b.start; });
            // Callee-saved first, then caller-saved. r10 stays the scratch
            // register for integer expressions, so it is not in this list.
            std::vector<int> pool = {3, 6, 7, 12, 13, 14, 15, 8, 9, 11, 1, 2};
            struct Active {
                int end;
                int reg;
            };
            std::vector<Active> active;
            auto callee_saved = [](int reg) { return reg == 3 || reg == 6 || reg == 7 || reg >= 12; };
            for (const Span& span : spans) {
                std::vector<Active> still;
                for (const Active& item : active) {
                    if (item.end < span.start) pool.insert(pool.begin(), item.reg);
                    else still.push_back(item);
                }
                active.swap(still);
                if (pool.empty()) continue;
                const int reg = pool.front();
                pool.erase(pool.begin());
                fn.locals[static_cast<size_t>(span.index)].reg = reg;
                if (callee_saved(reg)) {
                    bool already = false;
                    for (int have : extra_regs)
                        if (have == reg) already = true;
                    if (!already) extra_regs.push_back(reg);
                }
                active.push_back({span.end, reg});
            }
        }
        if (needs_nv) extra_regs = {3, 6, 7};
        if (((static_cast<int>(extra_regs.size()) + 1) % 2) == 0) frame += 8;
        gen.save_nv = !extra_regs.empty();
        gen.extra_pops.clear();
        gen.locals = fn.locals;
        gen.temp_base = -(((local_end + 7) & ~7) + 8);
        gen.struct_copy = -((((local_end + 7) & ~7) + 64 + copy_bytes));
        gen.frame = frame;
        const uint32_t start = static_cast<uint32_t>(gen.text.size());
        gen.lines.clear();
        if (gen.debug_lines) gen.lines.push_back({start, fn.line});
        uint8_t cursor = 0;
        auto mark = [&](uint8_t bytes) { cursor = static_cast<uint8_t>(cursor + bytes); return cursor; };
        std::vector<UnwindPush> pushes;
        for (int reg : extra_regs) {
            if (reg >= 12) {
                const uint8_t n = static_cast<uint8_t>(reg - 12);
                gen.b(0x41);
                gen.b(static_cast<uint8_t>(0x54 + n));
                pushes.push_back({static_cast<uint8_t>(reg), mark(2)});
                gen.extra_pops.insert(gen.extra_pops.begin(), static_cast<uint8_t>(0x5C + n));
                gen.extra_pops.insert(gen.extra_pops.begin(), 0x41);
            } else {
                const uint8_t push_op = reg == 3 ? 0x53 : reg == 6 ? 0x56 : 0x57;
                const uint8_t pop_op = reg == 3 ? 0x5B : reg == 6 ? 0x5E : 0x5F;
                gen.b(push_op);
                pushes.push_back({static_cast<uint8_t>(reg), mark(1)});
                gen.extra_pops.insert(gen.extra_pops.begin(), pop_op);
            }
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
        gen.continue_stack.clear();
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
            const int machine = src == 0 ? 1 : src == 1 ? 2 : src == 2 ? 8 : 9;
            gen.mov_regs(local.reg, machine);
        }
        if (!gen.gen_stmt(fn.body)) {
            *error = gen.error.empty() ? "codegen failed" : gen.error;
            return false;
        }
        if (gen.debug_lines) gen.lines.push_back({static_cast<uint32_t>(gen.text.size()), fn.end_line});
        gen.epilogue();
        placed.push_back({fn.symbol.empty() ? fn.name : fn.symbol, start, static_cast<uint32_t>(gen.text.size()) - start, fn.exported, fn.is_static, {}});
        placed.back().lines = gen.lines;
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
        const std::string unw = "unwind_" + placed.back().name;
        // pdata: begin, end (addend = size), unwind info.
        const uint32_t pat = static_cast<uint32_t>(gen.pdata.size());
        for (int i = 0; i < 3; ++i) gen.pdata.push_back(0), gen.pdata.push_back(0), gen.pdata.push_back(0), gen.pdata.push_back(0);
        gen.pdata[pat + 4] = static_cast<uint8_t>(placed.back().size);
        gen.pdata[pat + 5] = static_cast<uint8_t>(placed.back().size >> 8);
        gen.pdata[pat + 6] = static_cast<uint8_t>(placed.back().size >> 16);
        gen.pdata[pat + 7] = static_cast<uint8_t>(placed.back().size >> 24);
        gen.relocs.push_back({2, pat, placed.back().name, kRelAmd64Addr32Nb});
        gen.relocs.push_back({2, pat + 4, placed.back().name, kRelAmd64Addr32Nb});
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
        if (global.vtable) {
            // Read-only: one 8-byte slot per virtual function, filled by an
            // ADDR64 relocation. A pure virtual slot stays zero.
            while (gen.rdata.size() % 8 != 0) gen.rdata.push_back(0);
            const uint32_t at = static_cast<uint32_t>(gen.rdata.size());
            for (size_t i = 0; i < global.slots.size(); ++i) {
                if (!global.slots[i].empty())
                    gen.relocs.push_back({1, at + static_cast<uint32_t>(i * 8), global.slots[i], kRelAmd64Addr64});
                for (int k = 0; k < 8; ++k) gen.rdata.push_back(0);
            }
            add_sym(global.name, at, 0, kClassExternal, 0);
            continue;
        }
        const int size = std::max(1, gen.size_of(global.type));
        const int align = global.type.pointer() ? 8 : 4;
        if (global.bss) {
            bss_size = align_to(bss_size, static_cast<uint32_t>(align));
            add_sym(global.name, bss_size, 0, global.is_static ? kClassStatic : kClassExternal, 0);
            bss_size += static_cast<uint32_t>(size);
        } else {
            const uint32_t at = align_to(static_cast<uint32_t>(data_bytes.size()), static_cast<uint32_t>(align));
            data_bytes.resize(at, 0);
            if (!global.init_bytes.empty()) {
                data_bytes.insert(data_bytes.end(), global.init_bytes.begin(), global.init_bytes.end());
                if (static_cast<int>(global.init_bytes.size()) < size)
                    data_bytes.resize(at + static_cast<uint32_t>(size), 0);
            } else {
                const uint32_t value = global.init;
                data_bytes.push_back(static_cast<uint8_t>(value));
                data_bytes.push_back(static_cast<uint8_t>(value >> 8));
                data_bytes.push_back(static_cast<uint8_t>(value >> 16));
                data_bytes.push_back(static_cast<uint8_t>(value >> 24));
                if (size > 4) data_bytes.resize(at + static_cast<uint32_t>(size), 0);
            }
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
            sym.section = static_cast<int16_t>(global.vtable ? rdata_section : global.bss ? bss_section : data_section);
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
    int debug_index = -1;
    if (line_source.enabled && line_source.origin != nullptr && line_source.files != nullptr) {
        // CodeView C13 subsections in .debug$S: signature 4, then one
        // DEBUG_S_LINES (0xF2) per function, DEBUG_S_STRINGTABLE (0xF3) and
        // DEBUG_S_FILECHKSMS (0xF4). A LINES header names its code range by a
        // SECREL and a SECTION relocation against the function symbol.
        std::vector<uint8_t> cv;
        std::vector<Relocation> cv_relocs;
        std::vector<std::string> used_files;
        auto table_index = [&](int file) {
            for (size_t k = 0; k < used_files.size(); ++k)
                if (used_files[k] == (*line_source.files)[static_cast<size_t>(file)]) return static_cast<uint32_t>(k);
            used_files.push_back((*line_source.files)[static_cast<size_t>(file)]);
            return static_cast<uint32_t>(used_files.size() - 1);
        };
        put32(&cv, 4);
        for (const PlacedFn& fn : placed) {
            // (file table index, offset in function, line) with same-line and same-offset entries merged.
            struct Entry {
                uint32_t file;
                uint32_t offset;
                uint32_t line;
            };
            std::vector<Entry> entries;
            for (const auto& item : fn.lines) {
                const int out_line = item.second;
                if (out_line <= 0 || static_cast<size_t>(out_line) > line_source.origin->size()) continue;
                const auto& where = (*line_source.origin)[static_cast<size_t>(out_line) - 1];
                if (where.second <= 0) continue;
                Entry entry{table_index(where.first), item.first - fn.start, static_cast<uint32_t>(where.second)};
                if (!entries.empty() && entries.back().offset == entry.offset) entries.pop_back();
                if (!entries.empty() && entries.back().file == entry.file && entries.back().line == entry.line) continue;
                entries.push_back(entry);
            }
            if (entries.empty()) continue;
            std::vector<uint8_t> payload;
            put32(&payload, 0);
            payload.push_back(0);
            payload.push_back(0);
            payload.push_back(0);
            payload.push_back(0);
            put32(&payload, fn.size);
            for (size_t k = 0; k < entries.size();) {
                size_t end = k;
                while (end < entries.size() && entries[end].file == entries[k].file) ++end;
                put32(&payload, entries[k].file * 8u);
                put32(&payload, static_cast<uint32_t>(end - k));
                put32(&payload, 12u + static_cast<uint32_t>(end - k) * 8u);
                for (size_t e = k; e < end; ++e) {
                    put32(&payload, entries[e].offset);
                    // 24 bits of line, 7 bits of extra lines, then the statement bit.
                    put32(&payload, (entries[e].line & 0x00FFFFFFu) | 0x80000000u);
                }
                k = end;
            }
            put32(&cv, 0xF2);
            put32(&cv, static_cast<uint32_t>(payload.size()));
            const auto sym = index.find(fn.name);
            if (sym == index.end()) {
                *error = "missing symbol " + fn.name;
                return false;
            }
            Relocation secrel;
            secrel.offset = static_cast<uint32_t>(cv.size());
            secrel.symbol = sym->second;
            secrel.type = kRelAmd64Secrel;
            Relocation section_rel = secrel;
            section_rel.offset += 4;
            section_rel.type = kRelAmd64Section;
            cv_relocs.push_back(secrel);
            cv_relocs.push_back(section_rel);
            cv.insert(cv.end(), payload.begin(), payload.end());
        }
        if (!used_files.empty()) {
            std::vector<uint8_t> names(1, 0);
            std::vector<uint8_t> sums;
            for (const std::string& file : used_files) {
                put32(&sums, static_cast<uint32_t>(names.size()));
                sums.push_back(0);
                sums.push_back(0);
                sums.push_back(0);
                sums.push_back(0);
                names.insert(names.end(), file.begin(), file.end());
                names.push_back(0);
            }
            while (names.size() % 4 != 0) names.push_back(0);
            put32(&cv, 0xF4);
            put32(&cv, static_cast<uint32_t>(sums.size()));
            cv.insert(cv.end(), sums.begin(), sums.end());
            put32(&cv, 0xF3);
            put32(&cv, static_cast<uint32_t>(names.size()));
            cv.insert(cv.end(), names.begin(), names.end());
            Section debug;
            debug.name = ".debug$S";
            debug.virtual_size = static_cast<uint32_t>(cv.size());
            debug.data = std::move(cv);
            debug.relocations = std::move(cv_relocs);
            // Initialized data, readable, discardable, 1-byte alignment.
            debug.characteristics = kScnCntInitData | kScnMemRead | 0x02000000u | kScnAlign1;
            object.sections.push_back(std::move(debug));
            debug_index = static_cast<int>(object.sections.size()) - 1;
        }
    }
    (void)debug_index;
    for (const Gen::NamedReloc& named : gen.relocs) {
        // section 0 text, 1 rdata (unused), 2 pdata in the Gen numbering.
        int sec = 0;
        if (named.section == 2) {
            sec = pdata_section - 1;
        } else if (named.section == 1) {
            sec = rdata_section - 1;
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
    pp.files.push_back(input.name);
    const std::string expanded = pp.run(stripped, "");
    if (!pp.error.empty()) return Result<CompileOutput>::fail(pp.error);
    std::string lex_error;
    std::vector<Token> tokens = lex(expanded, &lex_error);
    // "file:line: " for a line of the preprocessed text, from the origin table.
    const auto where = [&pp](int line) {
        if (line <= 0 || static_cast<size_t>(line) > pp.origin.size()) return std::string();
        const auto& o = pp.origin[static_cast<size_t>(line) - 1];
        const std::string name = o.first >= 0 && static_cast<size_t>(o.first) < pp.files.size() ? pp.files[static_cast<size_t>(o.first)] : "?";
        return name + ":" + std::to_string(o.second) + ": ";
    };
    if (!lex_error.empty()) {
        const size_t at_line = lex_error.rfind("at line ");
        const int line = at_line == std::string::npos ? 0 : std::atoi(lex_error.c_str() + at_line + 8);
        return Result<CompileOutput>::fail(where(line) + lex_error);
    }
    Parser parser{tokens, 0, {}, {}, {}, {}, {}, {}, {}, {}, nullptr, {}, {}};
    {
        // bool is a one-byte unsigned type whose stored value is 0 or 1 ([basic.fundamental]).
        Type boolean;
        boolean.base = 2;
        boolean.is_unsigned = true;
        boolean.is_bool = true;
        parser.typedefs["bool"] = boolean;
    }
    if (!parser.parse()) {
        const size_t pos = std::min(parser.at, parser.tokens.size() - 1);
        return Result<CompileOutput>::fail(where(parser.tokens[pos].line) + (parser.error.empty() ? "parse failed" : parser.error));
    }
    LineSource line_source;
    line_source.enabled = input.debug_lines;
    line_source.origin = &pp.origin;
    line_source.files = &pp.files;
    CompileOutput output;
    std::string error;
    if (!emit_functions(parser.funcs, parser.structs, parser.globals, parser.func_types, input.optimize, input.opt_level,
                        line_source, &error, &output))
        return Result<CompileOutput>::fail(error);
    return Result<CompileOutput>::success(std::move(output));
}

}  // namespace okayms
