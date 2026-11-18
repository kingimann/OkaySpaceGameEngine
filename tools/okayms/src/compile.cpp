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
    // Type of an integer literal ([lex.icc]): 0 int, 1 unsigned int, 2 long long, 3 unsigned long long.
    int ity = 0;
    // Line of the preprocessed text, counted from 1.
    int line = 0;
    // A '>' written directly after another '>' (no space), used to read '>>' and '>>='.
    bool adj = false;
    // The word `inline` stood in front of this token (the word itself is dropped): the function that follows is an inline
    // function, one copy per file.
    bool inl = false;
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
    // A parameter or return type written `const T&` or `const T*`: the target is const. Only decorated names and
    // overload choice look at it.
    bool pconst = false;
    bool is_ref = false;
    // `T&&` with a class (or template) T: an rvalue reference; is_ref is set too. It binds to a temporary or to the
    // result of std::move, and takes part in the choice of a move constructor or a move assignment.
    bool rref = false;
    // bool: one byte holding 0 or 1 (base 2, unsigned). A conversion to it compares with zero.
    bool is_bool = false;
    // long long, unsigned long long, __int64: a base 1 integer that is 8 bytes wide and lives in rax.
    bool wide = false;
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
    // params[i] is the address of a reference parameter (the function was declared with T& there).
    std::vector<bool> param_ref;
    bool vararg = false;
};

struct Field {
    std::string name;
    Type type{};
    int offset = 0;
    // A default member initializer ([class.mem]): the token where its expression starts, or -1.
    // `T x{};` has no expression and gives zero (init_zero).
    int init_at = -1;
    // `T x{a, b};`: init_at is the first argument, and the list ends at the '}'.
    bool init_braced = false;
    bool init_zero = false;
    // `T x = {a, b};`: init_at is the '{' itself; an array or aggregate member is filled element by element.
    bool init_aggr = false;
    // `T& x;` (a reference member): the field holds the address (type.pointers is one more than the declared type) and
    // every read of the name goes through it.
    bool ref = false;
};

struct Node;

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
    // Defined outside its class (`T C::f() {}`): one definition for the program. A member defined inside the class body is
    // an inline function: every file that includes the class gets its own copy (internal symbol).
    bool out_of_class = false;
    int slot = -1;
    // 'A' private, 'I' protected, 'Q' public: the MSVC access letters.
    char access = 'Q';
    // The class that declared the function.
    int owner = -1;
    // Token index of the body's '{' (or of the ':' that starts a constructor's
    // initializer list) until the body has been compiled.
    size_t body_at = 0;
    // Where the body's '{' was, kept after the body is compiled (body_at is then cleared).
    size_t tok_body = 0;
    // Template parameter names of an out-of-class definition (template <typename U> U S<U>::f()).
    std::vector<std::string> ool_names;
    // A constructor or destructor the compiler made because a member or base
    // needs one. It has no tokens.
    bool implicit = false;
    // The implicit copy constructor: copies the base and each member.
    bool implicit_copy = false;
    // The copy assignment the compiler writes for a class with a member whose own assignment is not a plain copy.
    bool implicit_assign = false;
    // The result type is `auto` or `decltype(auto)`: found from the body's first return when the body is compiled.
    bool auto_ret = false;
    // A static member function: no this, decorated with S/C/K instead of the Q/A/I letters.
    bool is_static = false;
    // The decorated spelling of a member function template instance's name ("$add@H"), else empty.
    std::string tsym;
    // Default arguments ([dcl.fct.default]): defaults[i] is the expression for parameter i, or an empty Block.
    std::vector<Node> defaults;
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
    // A lambda's closure class: the captured variables kept by reference. The field holds the address; a use of
    // the name reads through it.
    std::set<std::string> ref_caps;
    // The class template and the arguments of an instance, for deduction through Node<T>.
    std::string tmpl;
    std::vector<TArg> targs;
    // The class has a constructor template (`template <typename T> C(T x)`): it is not an aggregate.
    bool ctor_tmpl = false;
    // The class this one is declared inside (-1 for none): its statics can be named from the nested class.
    int outer = -1;
    // Named by `struct X;` or used by pointer before its definition; the definition fills this entry in.
    bool incomplete = false;
    // A generic lambda ([expr.prim.lambda]/5, `auto` parameters): the closure keeps where its parameter list and body are;
    // each call with new argument types makes one operator() from them.
    bool generic = false;
    int this_class = -1;  // a lambda in a member function that uses its class's members: the class (the closure holds `__this`)
    size_t gen_params_at = 0;
    size_t gen_body_at = 0;
    size_t gen_body_end = 0;
    bool gen_has_ret = false;
    Type gen_ret{};
    bool gen_returns_value = false;
    std::set<std::string> gen_done;
};

struct Global {
    std::string name;
    Type type{};
    // `extern T x;`: declared here, made in another file (an undefined symbol).
    bool is_extern = false;
    bool bss = true;
    bool is_static = false;
    uint32_t init = 0;
    std::vector<uint8_t> init_bytes;
    // A virtual function table: read-only data, one ADDR64 slot per entry.
    bool vtable = false;
    std::string rtti_sym;  // the type_info object of the class: stored in the slot just before the table (vtable[-1])
    std::vector<std::string> slots;
    // Pointers in the initial data (a string literal's address): offset in the data and the
    // symbol it points to, filled by an ADDR64 relocation.
    std::vector<std::pair<uint32_t, std::string>> addr_relocs;
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
    // A const integer local with a constant initializer is itself a constant ([expr.const]): int a[n];
    bool has_cval = false;
    uint64_t cval = 0;
    std::string symbol;
    int reg = 0;
    // 0 is the parameter scope. A nested block uses a higher number.
    int scope = 0;
};

// Options of the compile in progress (compile_c sets them; a compile is single-threaded).
bool g_comdat_inline = false;   // inline functions go into COMDAT sections (the linker keeps one copy)
bool g_crt_init_table = false;  // global constructors run from a .CRT$XCU table instead of a call at the start of main

struct FuncDecl {
    std::string name;
    // An inline function or a member defined in its class: with g_comdat_inline it is a COMDAT function (external symbol, one copy).
    bool comdat = false;
    Type ret{};
    std::vector<Type> params;
    std::vector<std::string> param_names;
    bool imported = false;
    bool exported = false;
    bool defined = false;
    bool is_static = false;
    bool vararg = false;
    bool sret = false;
    // Default arguments, as for Method::defaults.
    std::vector<Node> defaults;
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
    bool variadic = false;  // the last parameter is `...`, named __VA_ARGS__ in the body
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

    // The line of the file that is being expanded: what __LINE__ stands for.
    int current_line = 0;
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
                if (word == "__LINE__" && macros.find(word) == macros.end()) {
                    out += std::to_string(current_line);
                    i = j;
                    continue;
                }
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
                if (it->second.variadic && !it->second.params.empty()) {
                    // The arguments from the `...` on are one argument, joined with their commas ([cpp.replace]/__VA_ARGS__).
                    const size_t fixed = it->second.params.size() - 1;
                    if (args.size() > fixed) {
                        std::string joined = args[fixed];
                        for (size_t a2 = fixed + 1; a2 < args.size(); ++a2) joined += "," + args[a2];
                        args.resize(fixed);
                        args.push_back(joined);
                    } else if (args.size() == fixed) {
                        args.push_back("");
                    }
                }
                // Every parameter is replaced in one pass over the body, so an argument that happens to contain another
                // parameter's name (CHECK_NEAR(f(a, b), 1, 2) with parameters a and b) is not substituted a second time.
                std::string substituted;
                {
                    const std::string& body = it->second.body;
                    for (size_t s = 0; s < body.size();) {
                        if (body[s] == '#' && s + 1 < body.size() && body[s + 1] == '#') {
                            substituted.push_back('\x01');  // token pasting: the spaces around it are removed below
                            s += 2;
                            continue;
                        }
                        if (body[s] == '#') {
                            // #param: the argument as a string literal ([cpp.stringize])
                            size_t e = s + 1;
                            while (e < body.size() && (body[e] == ' ' || body[e] == '\t')) ++e;
                            size_t f = e;
                            while (f < body.size() && (std::isalnum(static_cast<unsigned char>(body[f])) || body[f] == '_')) ++f;
                            const std::string pname = body.substr(e, f - e);
                            bool done = false;
                            for (size_t p = 0; p < it->second.params.size() && p < args.size() && !done; ++p)
                                if (!pname.empty() && it->second.params[p] == pname) {
                                    std::string text = args[p];
                                    size_t b0 = 0;
                                    while (b0 < text.size() && (text[b0] == ' ' || text[b0] == '\t')) ++b0;
                                    size_t b1 = text.size();
                                    while (b1 > b0 && (text[b1 - 1] == ' ' || text[b1 - 1] == '\t')) --b1;
                                    substituted.push_back('"');
                                    for (size_t q = b0; q < b1; ++q) {
                                        if (text[q] == '"' || text[q] == '\\') substituted.push_back('\\');
                                        substituted.push_back(text[q]);
                                    }
                                    substituted.push_back('"');
                                    s = f;
                                    done = true;
                                }
                            if (done) continue;
                        }
                        if (std::isalpha(static_cast<unsigned char>(body[s])) || body[s] == '_') {
                            size_t e = s + 1;
                            while (e < body.size() && (std::isalnum(static_cast<unsigned char>(body[e])) || body[e] == '_')) ++e;
                            const std::string word = body.substr(s, e - s);
                            bool replaced = false;
                            for (size_t p = 0; p < it->second.params.size() && p < args.size(); ++p) {
                                if (it->second.params[p] == word) {
                                    substituted += args[p];
                                    replaced = true;
                                    break;
                                }
                            }
                            if (!replaced) substituted += word;
                            s = e;
                        } else substituted.push_back(body[s++]);
                    }
                }
                for (size_t pz = substituted.find('\x01'); pz != std::string::npos; pz = substituted.find('\x01')) {
                    size_t from = pz;
                    while (from > 0 && (substituted[from - 1] == ' ' || substituted[from - 1] == '\t')) --from;
                    size_t to = pz + 1;
                    while (to < substituted.size() && (substituted[to] == ' ' || substituted[to] == '\t')) ++to;
                    substituted.erase(from, to - from);
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
                            else if (rest.compare(p, 3, "...") == 0) {
                                macro.variadic = true;
                                macro.params.push_back("__VA_ARGS__");
                                e = p + 3;
                            } else if (p < rest.size() && rest[p] != ')') {
                                e = p + 1;  // a stray character: skipped (it used to stop the scan and loop forever)
                            }
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
            current_line = ln;
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
uint64_t ieee_real(unsigned __int128 sig, int pow10, bool neg, bool single) {
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
        if (std::isdigit(c) || (c == '.' && i + 1 < text.size() && std::isdigit(static_cast<unsigned char>(text[i + 1])))) {
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
            // A '.' after the digits starts the fraction when a digit follows, or when nothing that would make `.` a member
            // access or an ellipsis does: 2.f, 3., 1.e2 ([lex.fcon]: the digit sequence before or after the point may be empty).
            const bool point_follows = base == 10 && any && j < text.size() && text[j] == '.' && j + 1 < text.size();
            const char after_point = point_follows ? text[j + 1] : '\0';
            const bool bare_point = point_follows && !std::isdigit(static_cast<unsigned char>(after_point)) && after_point != '.' &&
                                    (after_point == 'f' || after_point == 'F' || after_point == 'e' || after_point == 'E' ||
                                     !(std::isalpha(static_cast<unsigned char>(after_point)) || after_point == '_'));
            if (base == 10 && j < text.size() && text[j] == '.' && j + 1 < text.size() &&
                (std::isdigit(static_cast<unsigned char>(text[j + 1])) || bare_point)) {
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
        inner.rref = false;
        if (type.rref && type.pointers == 0) return std::string("$$QEA") + cxx_type(inner, structs);  // T&&
        return std::string(type.pconst && type.pointers == 0 ? "AEB" : "AEA") + cxx_type(inner, structs);
    }
    if (type.pointers > 0) {
        Type inner = type;
        --inner.pointers;
        return std::string(type.pconst && type.pointers == 1 ? "PEB" : "PEA") + cxx_type(inner, structs);
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
    if (type.wide) return type.is_unsigned ? "_K" : "_J";
    return type.is_unsigned ? "I" : "H";
}

// The name part of a decorated name: "f@" for a function f, and the operator code for an operator function
// ("??H" for operator+, from the public MSVC decoration table: ?4 = ?5 >> ?6 << ?7 ! ?8 == ?9 != ?A [] ?B
// conversion ?C -> ?D * ?E ++ ?F -- ?G - ?H + ?I & ?K / ?L % ?M < ?N <= ?O > ?P >= ?Q , ?R () ?S ~ ?T ^ ?U |
// ?V && ?W || ?X *= ?Y += ?Z -= ?_0 /= ?_1 %= ?_2 >>= ?_3 <<= ?_4 &= ?_5 |= ?_6 ^=). The result of an operator
// is "?" and the code, with no '@' after it.
std::string name_part(const std::string& name) {
    if (name.compare(0, 8, "operator") != 0 || name.size() == 8) return name + "@";
    static const char* const table[][2] = {
        {"=", "?4"}, {">>", "?5"}, {"<<", "?6"}, {"!", "?7"}, {"==", "?8"}, {"!=", "?9"}, {"[]", "?A"},
        {"->", "?C"}, {"*", "?D"}, {"++", "?E"}, {"--", "?F"}, {"-", "?G"}, {"+", "?H"}, {"&", "?I"},
        {"/", "?K"}, {"%", "?L"}, {"<", "?M"}, {"<=", "?N"}, {">", "?O"}, {">=", "?P"}, {",", "?Q"},
        {"()", "?R"}, {"~", "?S"}, {"^", "?T"}, {"|", "?U"}, {"&&", "?V"}, {"||", "?W"}, {"*=", "?X"},
        {"+=", "?Y"}, {"-=", "?Z"}, {"/=", "?_0"}, {"%=", "?_1"}, {">>=", "?_2"}, {"<<=", "?_3"},
        {"&=", "?_4"}, {"|=", "?_5"}, {"^=", "?_6"}};
    const std::string rest = name.substr(8);
    if (rest.compare(0, 5, " conv") == 0) return "?B";
    for (const auto& entry : table)
        if (rest == entry[0]) return entry[1];
    return name + "@";
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
        std::string st = "?" + (m.tsym.empty() ? name_part(m.name) : m.tsym + "@") + owner.symname() + "@@";
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
    std::string out = "?" + (m.tsym.empty() ? name_part(m.name) : m.tsym + "@") + owner.symname() + "@@";
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

std::string cxx_symbol(const std::string& ns, const std::string& name, const Type& ret, const std::vector<Type>& params,
                       const std::vector<StructDef>* structs = nullptr) {
    std::string out = "?" + name_part(name) + (ns.empty() ? "" : ns + "@") + "@YA";
    if (ret.base == 3 && ret.pointers == 0 && !ret.is_ref) out += "?A";
    out += cxx_type(ret, structs);
    if (params.empty()) out += "X";
    else {
        for (const Type& param : params) out += cxx_type(param, structs);
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

// Constant folding of integers ([expr.const], [expr.arith.conv]). The value of an integer literal node is kept
// as 64 bits in the form its type gives: sign-extended for int and long long, zero-extended for unsigned int.
uint64_t const_norm(uint64_t v, const Type& t) {
    if (t.wide) return v;
    if (t.is_unsigned) return v & 0xFFFFFFFFull;
    return static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(static_cast<uint32_t>(v))));
}

// The type of an arithmetic operation on two integer types after the usual arithmetic conversions: the wide
// operand wins; an unsigned operand wins over a signed one of the same width.
Type const_common(const Type& a, const Type& b) {
    Type r;
    r.base = 1;
    if (a.wide || b.wide) {
        r.wide = true;
        r.is_unsigned = (a.wide && b.wide) ? (a.is_unsigned || b.is_unsigned) : (a.wide ? a.is_unsigned : b.is_unsigned);
    } else {
        r.is_unsigned = a.is_unsigned || b.is_unsigned;
    }
    return r;
}

// The bytes a constant takes when it is stored in an object of arithmetic type `to` ([conv.integral],
// [conv.fpint], [conv.double]): an integer literal given to a float, a float literal given to an int, and so
// on. The low bytes of *bits are the object's value. Returns false for a type that is not arithmetic.
// The value of a numeric literal node as a double (a float literal holds float bits).
double lit_double(const Node& n) {
    if (n.type.base == 5) {
        float f = 0;
        const uint32_t low = static_cast<uint32_t>(n.number);
        std::memcpy(&f, &low, 4);
        return f;
    }
    if (n.type.base == 6) {
        double d = 0;
        std::memcpy(&d, &n.number, 8);
        return d;
    }
    if (n.type.is_unsigned && n.type.wide) return static_cast<double>(n.number);
    return static_cast<double>(static_cast<int64_t>(n.number));
}

bool const_convert(const Node& lit, const Type& to, uint64_t* bits) {
    if (to.pointers > 0) {
        *bits = lit.number;
        return true;
    }
    if (to.array_len >= 0 || to.is_ref || (to.base != 1 && to.base != 2 && to.base != 4 && to.base != 5 && to.base != 6))
        return false;
    const int from = lit.type.base;  // 1 integer, 5 float literal, 6 double literal
    double value = 0;
    bool value_is_int = false;
    int64_t as_int = 0;
    if (from == 5) {
        float f = 0;
        const uint32_t low = static_cast<uint32_t>(lit.number);
        std::memcpy(&f, &low, 4);
        value = f;
    } else if (from == 6) {
        std::memcpy(&value, &lit.number, 8);
    } else {
        value_is_int = true;
        as_int = static_cast<int64_t>(lit.number);
        value = lit.type.is_unsigned && lit.type.wide ? static_cast<double>(lit.number) : static_cast<double>(as_int);
    }
    if (to.base == 5) {
        float f;
        if (value_is_int) f = lit.type.is_unsigned && lit.type.wide ? static_cast<float>(lit.number) : static_cast<float>(as_int);
        else f = static_cast<float>(value);
        uint32_t low = 0;
        std::memcpy(&low, &f, 4);
        *bits = low;
        return true;
    }
    if (to.base == 6) {
        std::memcpy(bits, &value, 8);
        return true;
    }
    int64_t v = as_int;
    if (!value_is_int) {
        if (value != value) v = 0;
        else if (value >= 9223372036854775807.0) v = INT64_MAX;
        else if (value <= -9223372036854775808.0) v = INT64_MIN;
        else v = static_cast<int64_t>(value);
    }
    if (to.is_bool) {
        *bits = value_is_int ? (lit.number != 0 ? 1u : 0u) : (value != 0 ? 1u : 0u);
        return true;
    }
    *bits = static_cast<uint64_t>(v);
    return true;
}

// Folds `left op right` when at least one operand is a float or double literal ([expr.arith.conv],
// [expr.mul], [expr.add], [expr.rel]). Two floats are computed as floats, anything with a double as a double.
// Returns false for an operator that does not fold.
bool fold_fp_binary(Tok op, const Node& l, const Node& r, Node* out) {
    const bool use_double = l.type.base == 6 || r.type.base == 6;
    const bool cmp = op == TEq || op == TNe || op == TLt || op == TGt || op == TLe || op == TGe;
    const double a = lit_double(l);
    const double c = lit_double(r);
    *out = Node{};
    out->kind = Node::IntLit;
    if (op == TAndAnd || op == TOrOr) {
        out->type.base = 1;
        out->number = op == TAndAnd ? (a != 0 && c != 0) : (a != 0 || c != 0);
        return true;
    }
    if (cmp) {
        const float fa = static_cast<float>(a);
        const float fc = static_cast<float>(c);
        const bool both_float = !use_double;
        bool v = false;
        if (op == TEq) v = both_float ? fa == fc : a == c;
        else if (op == TNe) v = both_float ? fa != fc : a != c;
        else if (op == TLt) v = both_float ? fa < fc : a < c;
        else if (op == TGt) v = both_float ? fa > fc : a > c;
        else if (op == TLe) v = both_float ? fa <= fc : a <= c;
        else v = both_float ? fa >= fc : a >= c;
        out->type.base = 1;
        out->number = v ? 1 : 0;
        return true;
    }
    if (op != TPlus && op != TMinus && op != TStar && op != TSlash) return false;
    if (use_double) {
        double v = 0;
        if (op == TPlus) v = a + c;
        else if (op == TMinus) v = a - c;
        else if (op == TStar) v = a * c;
        else v = a / c;
        out->type.base = 6;
        std::memcpy(&out->number, &v, 8);
        return true;
    }
    const float fa = static_cast<float>(a);
    const float fc = static_cast<float>(c);
    volatile float v = 0;
    if (op == TPlus) v = fa + fc;
    else if (op == TMinus) v = fa - fc;
    else if (op == TStar) v = fa * fc;
    else v = fa / fc;
    const float res = v;
    uint32_t low = 0;
    std::memcpy(&low, &res, 4);
    out->type.base = 5;
    out->number = low;
    return true;
}

// Folds `left op right` for two integer literal nodes into *out. Returns false when the operator is not one
// that folds; *err is set for an error (division by zero, a shift count that is too large).
bool fold_int_binary(Tok op, const Node& l, const Node& r, Node* out, std::string* err) {
    const bool shift = op == TShl || op == TShr;
    const bool logic = op == TAndAnd || op == TOrOr;
    Type rt = shift ? Type{} : const_common(l.type, r.type);
    if (shift) {
        rt.base = 1;
        rt.wide = l.type.wide;
        rt.is_unsigned = l.type.is_unsigned;
    }
    const uint64_t a = const_norm(l.number, shift ? rt : rt);
    const uint64_t c = shift ? r.number : const_norm(r.number, rt);
    const bool uns = rt.is_unsigned;
    uint64_t result = 0;
    bool int_result = false;  // the result is an int 0 or 1 (a comparison or && ||)
    if (logic) {
        result = op == TAndAnd ? (l.number != 0 && r.number != 0) : (l.number != 0 || r.number != 0);
        int_result = true;
    } else if (op == TPlus) result = a + c;
    else if (op == TMinus) result = a - c;
    else if (op == TStar) result = a * c;
    else if (op == TSlash || op == TPercent) {
        if (c == 0) {
            *err = "division by zero";
            return false;
        }
        if (uns) result = op == TSlash ? a / c : a % c;
        else {
            const int64_t sa = static_cast<int64_t>(a);
            const int64_t sc = static_cast<int64_t>(c);
            if (sc == -1) result = op == TSlash ? 0 - a : 0;
            else result = static_cast<uint64_t>(op == TSlash ? sa / sc : sa % sc);
        }
    } else if (op == TAmp) result = a & c;
    else if (op == TPipe) result = a | c;
    else if (op == TCaret) result = a ^ c;
    else if (shift) {
        if (c >= 64) {
            *err = "shift count is too large";
            return false;
        }
        if (op == TShl) result = a << c;
        else if (uns) result = a >> c;
        else result = static_cast<uint64_t>(static_cast<int64_t>(a) >> c);
    } else if (op == TEq || op == TNe || op == TLt || op == TGt || op == TLe || op == TGe) {
        int_result = true;
        const bool lt = uns ? a < c : static_cast<int64_t>(a) < static_cast<int64_t>(c);
        const bool gt = uns ? a > c : static_cast<int64_t>(a) > static_cast<int64_t>(c);
        if (op == TEq) result = a == c;
        else if (op == TNe) result = a != c;
        else if (op == TLt) result = lt;
        else if (op == TGt) result = gt;
        else if (op == TLe) result = !gt;
        else result = !lt;
    } else {
        return false;
    }
    *out = Node{};
    out->kind = Node::IntLit;
    if (int_result) {
        out->number = result;
        out->type.base = 1;
    } else {
        out->type = rt;
        out->number = const_norm(result, rt);
    }
    return true;
}

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
    // The namespaces named by `using namespace`, in the order written.
    std::vector<std::string> using_namespaces{};
    std::string ns_mangled{};
    std::vector<std::string> ns_parts{};
    std::vector<size_t> ns_open{};  // how many names each open brace pushed (0 for an unnamed namespace)
    // extern "C" ([dcl.link]): a function declared under it keeps its plain name as the symbol, also inside a namespace.
    std::map<std::string, std::string> global_alias{};  // variable name -> the internal name of its latest declaration
    std::vector<bool> ns_is_c{};
    std::vector<bool> ns_is_anon{};
    int anon_depth = 0;
    int c_linkage = 0;
    bool c_single = false;
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
    // `operator` and the operator symbol become one identifier token ("operator+", "operator[]", "operator()"),
    // so a declaration or a call of an operator function reads like any other function. A conversion function
    // `operator T()` is written the way a function with a return type is read: T moves in front and the name is
    // "operator conv" and T's spelling ([class.conv.fct], [over.oper]).
    void collapse_operators() {
        std::vector<Token> out;
        out.reserve(tokens.size());
        for (size_t i = 0; i < tokens.size(); ++i) {
            const Token& t = tokens[i];
            if (t.kind != TIdent || t.text != "operator" || i + 1 >= tokens.size()) {
                out.push_back(t);
                continue;
            }
            const Token& n = tokens[i + 1];
            std::string name;
            size_t used = 1;
            switch (n.kind) {
            case TPlus: name = "+"; break;
            case TMinus: name = "-"; break;
            case TStar: name = "*"; break;
            case TSlash: name = "/"; break;
            case TPercent: name = "%"; break;
            case TAmp: name = "&"; break;
            case TPipe: name = "|"; break;
            case TCaret: name = "^"; break;
            case TTilde: name = "~"; break;
            case TNot: name = "!"; break;
            case TAssign: name = "="; break;
            case TLt: name = "<"; break;
            case TLe: name = "<="; break;
            case TGe: name = ">="; break;
            case TEq: name = "=="; break;
            case TNe: name = "!="; break;
            case TPlusPlus: name = "++"; break;
            case TMinusMinus: name = "--"; break;
            case TPlusEq: name = "+="; break;
            case TMinusEq: name = "-="; break;
            case TStarEq: name = "*="; break;
            case TSlashEq: name = "/="; break;
            case TPercentEq: name = "%="; break;
            case TAndEq: name = "&="; break;
            case TOrEq: name = "|="; break;
            case TXorEq: name = "^="; break;
            case TShl: name = "<<"; break;
            case TShlEq: name = "<<="; break;
            case TAndAnd: name = "&&"; break;
            case TOrOr: name = "||"; break;
            case TArrow: name = "->"; break;
            case TComma: name = ","; break;
            case TGt:
                name = ">";
                if (i + 2 < tokens.size() && tokens[i + 2].adj && tokens[i + 2].kind == TGt) {
                    name = ">>";
                    used = 2;
                } else if (i + 2 < tokens.size() && tokens[i + 2].adj && tokens[i + 2].kind == TGe) {
                    name = ">>=";
                    used = 2;
                }
                break;
            case TLParen:
                if (i + 2 < tokens.size() && tokens[i + 2].kind == TRParen) {
                    name = "()";
                    used = 2;
                }
                break;
            case TLBrack:
                if (i + 2 < tokens.size() && tokens[i + 2].kind == TRBrack) {
                    name = "[]";
                    used = 2;
                }
                break;
            default: break;
            }
            if (!name.empty()) {
                Token op = t;
                op.text = "operator" + name;
                out.push_back(op);
                i += used;
                continue;
            }
            if (n.kind == TIdent && (n.text == "new" || n.text == "delete")) {
                out.push_back(t);
                continue;
            }
            // A conversion function: the type runs up to the '('.
            size_t j = i + 1;
            std::string key;
            while (j < tokens.size() && tokens[j].kind != TLParen && tokens[j].kind != TEnd) {
                key += std::to_string(static_cast<int>(tokens[j].kind)) + tokens[j].text + ",";
                ++j;
            }
            if (j >= tokens.size() || tokens[j].kind != TLParen || j == i + 1 ||
                (!out.empty() && out.back().kind == TColonColon)) {
                out.push_back(t);
                continue;
            }
            for (size_t k = i + 1; k < j; ++k) out.push_back(tokens[k]);
            Token op = t;
            op.text = "operator conv:" + key;
            out.push_back(op);
            i = j - 1;
        }
        tokens = std::move(out);
    }

    // std::function<R(A, B)> is read as function<R, A, B>: okay-ms has no function types as template arguments, so
    // <functional> declares `template <typename R, typename... Args> class function` ([func.wrap.func] with the signature
    // split into its return type and its parameter types). This runs first, so it also reaches a `using` alias.
    void rewrite_function_signatures() {
        std::vector<Token> out;
        out.reserve(tokens.size());
        for (size_t i = 0; i < tokens.size(); ++i) {
            const Token& t = tokens[i];
            if (t.kind == TIdent && t.text == "function" && i + 2 < tokens.size() && tokens[i + 1].kind == TLt) {
                size_t j = i + 2;
                int angle = 1;
                int paren = 0;
                size_t open = 0;
                size_t close = 0;
                for (; j < tokens.size() && tokens[j].kind != TEnd; ++j) {
                    const Tok k = tokens[j].kind;
                    if (k == TLParen) {
                        if (paren == 0 && angle == 1 && open == 0) open = j;
                        ++paren;
                    } else if (k == TRParen) {
                        if (--paren == 0 && angle == 1 && close == 0 && open != 0) close = j;
                    } else if (k == TLt && paren == 0) {
                        ++angle;
                    } else if (k == TGt && paren == 0) {
                        if (--angle == 0) break;
                    } else if (k == TSemi || k == TLBrace) {
                        break;
                    }
                }
                if (open != 0 && close != 0 && j < tokens.size() && tokens[j].kind == TGt && close + 1 == j &&
                    open > i + 2) {
                    for (size_t k2 = i; k2 < open; ++k2) out.push_back(tokens[k2]);
                    if (close > open + 1) {
                        Token comma = tokens[open];
                        comma.kind = TComma;
                        comma.text = ",";
                        out.push_back(comma);
                        // The parameter names are dropped: `function<bool(const std::string& path, int id)>` is
                        // `function<bool, const std::string&, int>`. A trailing identifier after '&', '*', a type word or
                        // another identifier is a name (an identifier after '::' or '<' is part of the type).
                        size_t pstart = open + 1;
                        int pdepth = 0;
                        for (size_t k2 = open + 1; k2 <= close; ++k2) {
                            const Tok kk = tokens[k2].kind;
                            if (kk == TLt || kk == TLParen) ++pdepth;
                            else if ((kk == TGt || kk == TRParen) && k2 != close) --pdepth;
                            if ((kk == TComma && pdepth == 0) || k2 == close) {
                                size_t pend = k2;
                                if (pend - pstart >= 2 && tokens[pend - 1].kind == TIdent) {
                                    const Tok before = tokens[pend - 2].kind;
                                    if (before == TAmp || before == TStar || before == TAndAnd || before == TIdent ||
                                        before == TInt || before == TChar || before == TFloat || before == TDouble ||
                                        before == TLong || before == TShort || before == TUnsigned || before == TSigned ||
                                        before == TGt)
                                        --pend;
                                }
                                for (size_t k3 = pstart; k3 < pend; ++k3) out.push_back(tokens[k3]);
                                if (k2 != close) out.push_back(tokens[k2]);
                                pstart = k2 + 1;
                            }
                        }
                    }
                    out.push_back(tokens[j]);
                    i = j;
                    continue;
                }
            }
            out.push_back(t);
        }
        tokens = std::move(out);
    }
    // `namespace fs = std::filesystem;` ([namespace.alias]): the declaration is dropped and each `fs::` is written out in full.
    void expand_namespace_aliases() {
        std::map<std::string, std::vector<Token>> aliases;
        std::vector<Token> out;
        out.reserve(tokens.size());
        for (size_t i = 0; i < tokens.size(); ++i) {
            const Token& t = tokens[i];
            if (t.kind == TNamespace && i + 3 < tokens.size() && tokens[i + 1].kind == TIdent && tokens[i + 2].kind == TAssign) {
                size_t j = i + 3;
                std::vector<Token> target;
                while (j < tokens.size() && tokens[j].kind != TSemi && tokens[j].kind != TEnd) target.push_back(tokens[j++]);
                aliases[tokens[i + 1].text] = target;
                i = j;
                continue;
            }
            if (t.kind == TIdent && !aliases.empty() && i + 1 < tokens.size() && tokens[i + 1].kind == TColonColon &&
                aliases.count(t.text) > 0 && !(i > 0 && tokens[i - 1].kind == TColonColon)) {
                for (const Token& a : aliases[t.text]) out.push_back(a);
                continue;
            }
            out.push_back(t);
        }
        tokens = std::move(out);
    }
    void lower_cxx_spellings() {
        expand_namespace_aliases();
        rewrite_function_signatures();
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
        std::set<size_t> wrap_close;  // the ')' that ends the operand of a rewritten cast: the cast is closed after it
        for (size_t i = 0; i < tokens.size(); ++i) {
            const Token& t = tokens[i];
            if (wrap_close.count(i) > 0) {
                out.push_back(t);
                Token rp = t;
                rp.kind = TRParen;
                rp.text = ")";
                out.push_back(rp);
                continue;
            }
            if (t.kind == TIdent && t.text == "constexpr" && !out.empty() && out.back().kind == TIf)
                continue;  // `if constexpr (c)` is read as `if (c)`: both branches are compiled
            if (t.kind == TIdent && t.text == "constexpr") {
                Token c = t;
                c.kind = TConst;
                c.text = "const";
                out.push_back(c);
                continue;
            }
            if (t.kind == TIdent && t.text == "thread_local") {
                // One copy for the whole program (no thread-local storage yet): read as `static`.
                if (i > 0 && tokens[i - 1].kind == TStatic) continue;
                Token c = t;
                c.kind = TStatic;
                c.text = "static";
                out.push_back(c);
                continue;
            }
            if (t.kind == TIdent && t.text == "mutable" && !out.empty() &&
                (out.back().kind == TSemi || out.back().kind == TLBrace || out.back().kind == TRBrace ||
                 out.back().kind == TColon))
                continue;  // a mutable data member: constness is not checked
            if (t.kind == TIdent && (t.text == "inline" || t.text == "explicit" || t.text == "override" ||
                                      t.text == "final" || t.text == "consteval" || t.text == "constinit" ||
                                      t.text == "volatile")) {
                if (t.text == "inline" && i + 1 < tokens.size()) tokens[i + 1].inl = true;
                continue;
            }
            if (t.kind == TConst && !out.empty() && out.back().kind == TStar) continue;  // int *const p
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
                if (tokens[i + 1].kind == TNamespace) {
                    // using namespace a::b; : the functions of that namespace can be called by their bare names
                    // (names of types and variables are in one table already). The directive is remembered for
                    // the whole file, not only for the block it is written in.
                    std::string ns;
                    size_t j = i + 2;
                    for (; j < tokens.size() && tokens[j].kind != TSemi && tokens[j].kind != TEnd; ++j) {
                        if (tokens[j].kind == TIdent) ns += tokens[j].text;
                        else if (tokens[j].kind == TColonColon) ns += "::";
                    }
                    if (!ns.empty()) using_namespaces.push_back(ns);
                    i = j;
                    continue;
                }
                if (tokens[i + 1].kind == TIdent && i + 3 < tokens.size() && tokens[i + 2].kind == TColonColon) {
                    // using a::b::name; ([namespace.udecl]): read as a directive for the namespace a::b, which is a
                    // little wider than the declaration (every function of it can be called by its bare name).
                    std::string ns;
                    size_t j = i + 1;
                    std::string last;
                    for (; j < tokens.size() && tokens[j].kind != TSemi && tokens[j].kind != TEnd; ++j) {
                        if (tokens[j].kind == TIdent) {
                            if (!last.empty()) ns += (ns.empty() ? "" : "::") + last;
                            last = tokens[j].text;
                        }
                    }
                    if (!ns.empty()) using_namespaces.push_back(ns);
                    i = j;
                    continue;
                }
                if (tokens[i + 1].kind == TIdent && i + 2 < tokens.size() && tokens[i + 2].kind == TAssign) {
                    size_t j = i + 3;
                    while (j < tokens.size() && tokens[j].kind != TSemi && tokens[j].kind != TEnd) ++j;
                    Token td = t;
                    td.kind = TTypedef;
                    td.text = "typedef";
                    out.push_back(td);
                    // using F = R (*)(A);  is  typedef R (*F)(A);  : the name goes inside the parentheses.
                    size_t fp = 0;
                    for (size_t k = i + 3; k + 2 < j; ++k)
                        if (tokens[k].kind == TLParen && tokens[k + 1].kind == TStar && tokens[k + 2].kind == TRParen) {
                            fp = k;
                            break;
                        }
                    if (fp != 0) {
                        for (size_t k = i + 3; k < fp + 2; ++k) out.push_back(tokens[k]);
                        out.push_back(tokens[i + 1]);
                        for (size_t k = fp + 2; k < j; ++k) out.push_back(tokens[k]);
                    } else {
                        for (size_t k = i + 3; k < j; ++k) out.push_back(tokens[k]);
                        out.push_back(tokens[i + 1]);
                    }
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
                    // static_cast<T>(x) becomes ((T)(x)): the whole cast is one operand, so `static_cast<T*>(p)->f()` applies
                    // `->f()` to the converted value.
                    {
                        size_t m = j + 1;
                        int depth = 0;
                        for (; m < tokens.size() && tokens[m].kind != TEnd; ++m) {
                            if (tokens[m].kind == TLParen) ++depth;
                            else if (tokens[m].kind == TRParen && --depth == 0) break;
                        }
                        if (m < tokens.size() && tokens[m].kind == TRParen) {
                            Token wrap = tokens[i + 1];
                            wrap.kind = TLParen;
                            wrap.text = "(";
                            out.push_back(wrap);
                            wrap_close.insert(m);
                        }
                    }
                    Token open = tokens[i + 1];
                    open.kind = TLParen;
                    open.text = "(";
                    Token close = tokens[j];
                    close.kind = TRParen;
                    close.text = ")";
                    out.push_back(open);
                    for (size_t k = i + 2; k < j; ++k) {
                        // volatile is dropped here too; `T *const` inside the cast loses its const as above.
                        if (tokens[k].kind == TIdent && tokens[k].text == "volatile") continue;
                        if (tokens[k].kind == TConst && !out.empty() && out.back().kind == TStar) continue;
                        out.push_back(tokens[k]);
                    }
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
        size_t dropped_colons = static_cast<size_t>(-1);
        for (size_t i = 0; i < tokens.size(); ++i) {
            if (tokens[i].kind == TColonColon && i + 2 < tokens.size() && tokens[i + 1].kind == TIdent &&
                namespaces.count(tokens[i + 1].text) > 0 && tokens[i + 2].kind == TColonColon &&
                !(i > 0 && (tokens[i - 1].kind == TIdent || tokens[i - 1].kind == TGt))) {
                dropped_colons = i;
                continue;  // ::okay::Name: the leading '::' (global scope) is dropped; namespaces are stripped next
            }
            if (tokens[i].kind == TNamespace) {
                while (i < tokens.size() && tokens[i].kind != TLBrace && tokens[i].kind != TEnd) out.push_back(tokens[i++]);
                if (i < tokens.size()) out.push_back(tokens[i]);
                continue;
            }
            if (tokens[i].kind == TIdent && namespaces.count(tokens[i].text) > 0 && i + 1 < tokens.size() &&
                tokens[i + 1].kind == TColonColon && !(i > 0 && tokens[i - 1].kind == TColonColon && i - 1 != dropped_colons)) {
                size_t k = i;
                while (k + 1 < tokens.size() && tokens[k].kind == TIdent && namespaces.count(tokens[k].text) > 0 &&
                       tokens[k + 1].kind == TColonColon)
                    k += 2;
                const bool call = k + 1 < tokens.size() && tokens[k].kind == TIdent && tokens[k + 1].kind == TLParen;
                const bool after_new = i > 0 && tokens[i - 1].kind == TIdent && tokens[i - 1].text == "new";  // new std::string(x): a type
                if ((!call || after_new) && k < tokens.size() && tokens[k].kind == TIdent) {
                    if (tokens[i].inl) tokens[k].inl = true;  // `inline std::uint32_t f()`: the mark moves to the type name
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
    // While a global's initial data is read: where the addresses it holds are recorded.
    std::vector<std::pair<uint32_t, std::string>>* init_relocs = nullptr;
    int gstr_count = 0;
    Node init_body{};
    bool init_used = false;
    std::vector<GlobalObj> global_objs{};
    std::vector<GlobalObj> static_objs{};
    std::vector<Node> global_init{};
    std::vector<std::vector<DObj>> dscopes{};
    // The local that `return name;` hands to the caller as the result: its destructor does not run ([class.copy.elision]).
    std::string dtor_skip{};
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
        if (d.ctor_tmpl) return true;
        if (d.poly) return true;  // the constructor stores the table pointer
        if (d.base >= 0 && class_needs_ctor(structs[static_cast<size_t>(d.base)])) return true;
        for (size_t i = d.base_fields; i < d.fields.size(); ++i) {
            const Type& t = d.fields[i].type;
            if (d.fields[i].init_at >= 0 || d.fields[i].init_zero) return true;
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

    // A class with no constructor written by the programmer, none of its own bases, and a braced list of members:
    // an aggregate whose members have default initializers.
    bool aggregate_with_defaults(int sid, const std::vector<Node>& args) const {
        const StructDef& sd = structs[static_cast<size_t>(sid)];
        if (sd.poly || sd.base >= 0 || sd.fields.empty()) return false;
        for (const Method& m : sd.methods)
            if (m.name == "<ctor>" && !m.implicit) return false;
        for (auto it = member_tmpls.begin(); it != member_tmpls.end(); ++it)
            if (it->first.first == sid && it->first.second == it->second.owner) return false;  // a constructor template
        if (args.size() == 1) {
            Type at;
            if (static_type(args[0], &at) && at.base == 3 && at.pointers == 0 && !at.is_array() && at.struct_id == sid)
                return false;  // a copy
        }
        return true;
    }
    // A call of a constructor or destructor on the object whose address is 'self'.
    bool special_call(int sid, const char* which, Node self, std::vector<Node> args, Node* out) {
        std::string tctor;
        if (which[1] == 'c' && !args.empty() && aggregate_with_defaults(sid, args)) {
            // C{a, b} for a struct with default member initializers and no constructor written: the default
            // constructor runs (it stores the defaults), then the members are set in order ([dcl.init.aggr]).
            std::vector<Node> steps;
            const Method* dflt = find_method(sid, "<ctor>", 0);
            if (dflt != nullptr) {
                Node call;
                call.kind = Node::Call;
                call.text = dflt->qname;
                call.number = 1;
                call.type = dflt->ret;
                call.kids.push_back(self);
                Node st;
                st.kind = Node::ExprStmt;
                st.kids.push_back(std::move(call));
                steps.push_back(std::move(st));
            }
            const StructDef& sd = structs[static_cast<size_t>(sid)];
            size_t k = 0;
            for (const Field& f : sd.fields) {
                if (f.name == "__vptr") continue;
                if (k >= args.size()) break;
                Node fn;
                fn.kind = Node::Field;
                fn.op = TArrow;
                fn.text = f.name;
                fn.kids.push_back(self);
                Node assign;
                assign.kind = Node::Assign;
                assign.op = TAssign;
                assign.number = obj_needs_ctor(f.type) || obj_needs_dtor(f.type) ? 0 : 1;  // an object: its operator=
                assign.kids.push_back(std::move(fn));
                assign.kids.push_back(std::move(args[k]));
                ++k;
                Node st;
                st.kind = Node::ExprStmt;
                st.kids.push_back(std::move(assign));
                steps.push_back(std::move(st));
            }
            Node block;
            block.kind = Node::Block;
            block.kids = std::move(steps);
            *out = std::move(block);
            return true;
        }
        if (which[1] == 'c' && !ensure_ctor_template(sid, args, &tctor)) return false;
        const Method* m = which[1] == 'c' ? find_ctor(sid, args) : find_method(sid, which, args.size());
        if (!tctor.empty()) m = find_ctor_instance(sid, tctor);
        if (m != nullptr && which[1] == 'c' && !pad_defaults(m->defaults, m->params.size(), &args)) return false;
        if (m != nullptr && which[1] == 'c' && !adjust_args(m->params, &args)) return false;
        if (m == nullptr && which[1] == 'c' && args.size() == 1 && !class_has_copy(sid)) {
            // C x(other) with no copy constructor written or implied: the members are copied as they are ([class.copy]).
            Type from;
            if (static_type(args[0], &from) && from.base == 3 && from.pointers == 0 && !from.is_array() && from.struct_id == sid) {
                if (has_move_ctor(sid)) {
                    // A class that declares a move constructor and no copy constructor has its copy constructor deleted
                    // ([class.copy.ctor]/8). Only a use in a function that is reached is an error: it is recorded here
                    // and reported when the program is finished (vector<unique_ptr<T>>'s copy constructor, never called,
                    // must not stop a file).
                    deleted_uses[current_fn] = "use of deleted function: the copy constructor of " +
                                               structs[static_cast<size_t>(sid)].name +
                                               " is deleted because the class declares a move constructor";
                    Node nothing;
                    nothing.kind = Node::Block;
                    nothing.text = "skip";
                    *out = std::move(nothing);
                    return true;
                }
                Node target;
                target.kind = Node::Unary;
                target.op = TStar;
                target.kids.push_back(std::move(self));
                Node copy;
                copy.kind = Node::Assign;
                copy.op = TAssign;
                copy.number = 1;
                copy.kids.push_back(std::move(target));
                copy.kids.push_back(std::move(args[0]));
                Node stmt;
                stmt.kind = Node::ExprStmt;
                stmt.kids.push_back(std::move(copy));
                *out = std::move(stmt);
                return true;
            }
        }
        if (m == nullptr) {
            error = std::string("no matching ") + (which[1] == 'c' ? "constructor" : "destructor") + " in " +
                    structs[static_cast<size_t>(sid)].name;
            if (!current_fn.empty()) error += " (in " + current_fn + ", " + std::to_string(args.size()) + " arguments)";
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
    static Node reference_result(Node call, bool is_ref, bool rvalue = false) {
        if (!is_ref) return call;
        call.type.is_ref = false;
        call.type.rref = false;
        call.type.pointers += 1;
        Node d;
        d.kind = Node::Unary;
        d.op = TStar;
        if (rvalue) d.text = "xval";  // std::move(x) and the like: an rvalue
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
    // Fills `target` (an lvalue of `type`) from a braced list, element by element; what the list does not name is
    // zero ([dcl.init.aggr]). `item` is null for "nothing named".
    bool lower_aggr(const Type& type, const Node& target, const Node* item, std::vector<Node>* out) {
        const bool list = item != nullptr && item->kind == Node::Block && item->text == "init";
        if (type.is_array()) {
            if (item != nullptr && !list) {
                error = "an array member needs a braced initializer";
                return false;
            }
            const Type elem = peel_array(type);
            for (int i = 0; i < type.array_len; ++i) {
                Node idx;
                idx.kind = Node::IntLit;
                idx.type.base = 1;
                idx.number = static_cast<uint64_t>(i);
                Node at_i;
                at_i.kind = Node::Index;
                at_i.kids.push_back(target);
                at_i.kids.push_back(std::move(idx));
                const Node* kid = list && static_cast<size_t>(i) < item->kids.size() ? &item->kids[static_cast<size_t>(i)] : nullptr;
                if (!lower_aggr(elem, at_i, kid, out)) return false;
            }
            return true;
        }
        if (type.base == 3 && type.pointers == 0 && !type.is_ref && type.struct_id >= 0 && (item == nullptr || list)) {
            const StructDef& sd = structs[static_cast<size_t>(type.struct_id)];
            size_t k = 0;
            for (const Field& f : sd.fields) {
                if (f.name == "__vptr") continue;
                Node fn;
                fn.kind = Node::Field;
                fn.op = TDot;
                fn.text = f.name;
                fn.kids.push_back(target);
                const Node* kid = list && k < item->kids.size() ? &item->kids[k] : nullptr;
                ++k;
                if (!lower_aggr(f.type, fn, kid, out)) return false;
            }
            return true;
        }
        Node value;
        if (list) {
            if (item->kids.empty()) {
                value.kind = Node::IntLit;
                value.type.base = 1;
            } else {
                value = item->kids[0];
            }
        } else if (item != nullptr) {
            value = *item;
        } else {
            value.kind = Node::IntLit;
            value.type.base = 1;
        }
        Node assign;
        assign.kind = Node::Assign;
        assign.op = TAssign;
        assign.kids.push_back(target);
        assign.kids.push_back(std::move(value));
        Node stmt;
        stmt.kind = Node::ExprStmt;
        stmt.kids.push_back(std::move(assign));
        out->push_back(std::move(stmt));
        return true;
    }
    bool field_is_aggr(const Field& field) const {
        if (!(field.init_aggr || field.init_braced || field.init_zero)) return false;
        if (field.type.is_array()) return !obj_needs_ctor(peel_array(field.type));
        return field.type.base == 3 && field.type.pointers == 0 && !field.type.is_ref && field.type.struct_id >= 0 &&
               !obj_needs_ctor(field.type);
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
                if (list[i].name != dtor_skip) {
                    if (!special_call(list[i].sid, "<dtor>", node_addr(node_name(list[i].name)), {}, &call)) return false;
                    out->push_back(std::move(call));
                }
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
    // How many scopes up from the current one the scope `local_scope` is (0 is the current scope), or -1 when it is not an
    // enclosing scope. A name means the declaration in the nearest scope, so a variable declared later in an outer scope does
    // not hide an earlier one in an inner block.
    int scope_distance(int local_scope) const {
        int walk = scope_depth;
        int dist = 0;
        while (walk != local_scope) {
            if (walk == 0) return -1;
            walk = scope_parent[static_cast<size_t>(walk)];
            ++dist;
        }
        return dist;
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
        if (error.empty()) {
            error = std::string("expected ") + what + (current_fn.empty() ? std::string() : " (in " + current_fn + ")");
            error += ", found '" + (is(TEnd) ? std::string("end") : cur().text) + "' after '" + (at > 0 ? tokens[at - 1].text : std::string()) +
                     "' (token kinds " + std::to_string(static_cast<int>(cur().kind)) + "/" + std::to_string(at > 0 ? static_cast<int>(tokens[at - 1].kind) : -1) + ")";
        }
        return false;
    }

    // The class a name means: first one nested in the class being read (or whose member function is being read) or in a class
    // around it ([basic.lookup.unqual]), so two classes can each have a nested `State`; then one at namespace scope; then any.
    int find_struct(const std::string& name) const {
        const int ctx = !class_ids.empty() ? class_ids.back() : cur_class;
        if (ctx >= 0 && ctx < static_cast<int>(structs.size())) {
            for (int c = ctx; c >= 0; c = structs[static_cast<size_t>(c)].outer)
                for (size_t i = 0; i < structs.size(); ++i)
                    if (structs[i].outer == c && structs[i].name == name) return static_cast<int>(i);
        }
        for (size_t i = 0; i < structs.size(); ++i)
            if (structs[i].outer < 0 && structs[i].name == name) return static_cast<int>(i);
        for (size_t i = 0; i < structs.size(); ++i)
            if (structs[i].name == name) return static_cast<int>(i);
        return -1;
    }
    int find_nested(int outer, const std::string& name) const {
        for (size_t i = 0; i < structs.size(); ++i)
            if (structs[i].outer == outer && structs[i].name == name) return static_cast<int>(i);
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
        return type.wide ? 8 : 4;
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
        return type.wide ? 8 : 4;
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
            if (t->base == 3 && t->struct_id >= 0 && t->pointers == 0 && tokens[at + 1].kind == TIdent &&
                typedefs.find(tokens[at + 1].text) == typedefs.end()) {
                // Outer::Nested: nested classes are kept under their plain name.
                int nested = find_nested(t->struct_id, tokens[at + 1].text);
                if (nested < 0) nested = find_struct(tokens[at + 1].text);
                if (nested >= 0 && nested != t->struct_id &&
                    !(at + 2 < tokens.size() && tokens[at + 2].kind == TLParen)) {
                    t->struct_id = nested;
                    next();
                    next();
                    any = true;
                    continue;
                }
            }
            if (t->base == 3 && t->struct_id >= 0 && t->pointers == 0 && tokens[at + 1].kind == TIdent &&
                !(at + 2 < tokens.size() && tokens[at + 2].kind == TLParen)) {
                // Class::Enum: an enumeration declared in a class is kept under its plain name (a typedef of int).
                auto en = typedefs.find(tokens[at + 1].text);
                if (en != typedefs.end() && en->second.base == 1 && en->second.pointers == 0) {
                    *t = en->second;
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
        if (is(TIdent) && cur().text == "auto" && auto_idx < auto_types.size()) {
            // An `auto` parameter of a generic lambda being instantiated: the type of the argument.
            *type = auto_types[auto_idx++];
            type->is_ref = false;
            next();
            return true;
        }
        if (is(TIdent) && cur().text == "auto" && at + 1 < tokens.size() &&
            (tokens[at + 1].kind == TIdent || tokens[at + 1].kind == TLParen || tokens[at + 1].kind == TAmp ||
             tokens[at + 1].kind == TStar)) {
            // A deduced result type: auto f() { return x; } ([dcl.spec.auto]).
            saw_auto_ret = true;
            next();
            type->base = 1;
            type->pointers = 0;
            return true;
        }
        if (is(TIdent) && cur().text == "decltype" && at + 3 < tokens.size() && tokens[at + 1].kind == TLParen &&
            tokens[at + 2].kind == TIdent && tokens[at + 2].text == "auto" && tokens[at + 3].kind == TRParen) {
            saw_auto_ret = true;  // decltype(auto): the type of the returned expression, kept as a value
            next();
            next();
            next();
            next();
            type->base = 1;
            type->pointers = 0;
            return true;
        }
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
        type->wide = false;
        if (is(TStruct) || is(TUnion)) {
            const bool as_union = is(TUnion);
            const bool as_class = is(TStruct) && cur().text == "class";
            next();
            std::string name;
            if (is(TIdent)) {
                name = cur().text;
                next();
                // struct Outer::Inner { ... }; a nested class defined outside its class is kept under its plain name.
                while (is(TColonColon) && at + 1 < tokens.size() && tokens[at + 1].kind == TIdent) {
                    next();
                    name = cur().text;
                    next();
                }
            }
            int base_id = -1;
            if (!name.empty() && is(TColon) && find_struct(name) < 0) {
                // class D : public Base<D> { ... } (the curiously recurring template pattern): D is named in its own base list,
                // so it is an incomplete class from here; its definition fills this entry in.
                StructDef placeholder;
                placeholder.name = name;
                placeholder.incomplete = true;
                placeholder.is_union = as_union;
                structs.push_back(std::move(placeholder));
            }
            if (accept(TColon)) {
                while (is(TIdent) && (cur().text == "public" || cur().text == "private" ||
                                      cur().text == "protected" || cur().text == "virtual"))
                    next();
                // The base is a class, an alias of a class (`using Behaviour = Component;`) or a class template instance.
                Type base_type;
                bool base_imp = false;
                bool base_exp = false;
                const size_t base_at = at;
                if (is(TIdent) && typedefs.find(cur().text) == typedefs.end() && find_struct(cur().text) >= 0 &&
                    at + 1 < tokens.size() && (tokens[at + 1].kind == TLBrace || tokens[at + 1].kind == TComma)) {
                    // A plain struct named as a base (no member functions yet, so it is not a "class name" by its look).
                    base_type.base = 3;
                    base_type.struct_id = find_struct(cur().text);
                    next();
                } else if (!is(TIdent) || !parse_type_core(&base_type, &base_imp, &base_exp) || base_type.base != 3 ||
                    base_type.pointers != 0 || base_type.struct_id < 0) {
                    if (error.empty() || at == base_at) error = "expected a base class";
                    return false;
                }
                base_id = base_type.struct_id;
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
            int id = find_struct(name);
            if (id < 0) {
                // `struct X;` and `X* p` before X is defined ([class.name]): an incomplete class; a later definition of X
                // fills in this entry, so pointers made before it already mean the final type.
                StructDef placeholder;
                placeholder.name = name;
                placeholder.incomplete = true;
                placeholder.is_union = as_union;
                if (inst_base_name.empty() && !class_ids.empty()) {
                    placeholder.outer = class_ids.back();
                    placeholder.sym_name = name + "@" + structs[static_cast<size_t>(class_ids.back())].symname();
                }
                structs.push_back(std::move(placeholder));
                id = static_cast<int>(structs.size()) - 1;
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
        int longs = 0;
        bool int64_word = false;
        while (is(TUnsigned) || is(TSigned) || is(TLong) || is(TShort) || is(TInt) || is(TVoid) || is(TChar) ||
               is(TFloat) || is(TDouble) || (is(TIdent) && (cur().text == "__int64" || cur().text == "__int32"))) {
            if (is(TIdent)) {
                // __int64 is long long and __int32 is int (the Microsoft spellings).
                if (cur().text == "__int64") int64_word = true;
                saw = true;
                next();
                continue;
            }
            if (is(TLong)) ++longs;
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
        if ((longs >= 2 || int64_word) && type->base == 1) type->wide = true;
        if (!saw) {
            error = "expected a type (in " + current_fn + ", token " + (is(TEnd) ? std::string("end") : cur().text) + ", after " + (at > 0 ? tokens[at - 1].text : std::string()) + ")";
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
        std::vector<Node> param_defaults;
    };

    // Default arguments ([dcl.fct.default]): defaults[i] is a real expression only when parameter i has one.
    static bool has_default(const std::vector<Node>& defaults, size_t i) {
        return i < defaults.size() && !(defaults[i].kind == Node::Block && defaults[i].text.empty());
    }
    // How many leading parameters have no default: the fewest arguments a call needs.
    static size_t required_args(const std::vector<Node>& defaults, size_t nparams) {
        for (size_t i = 0; i < nparams; ++i)
            if (has_default(defaults, i)) return i;
        return nparams;
    }
    // The same for a free function: a declaration may give a default that a later definition leaves out.
    bool pad_func_defaults(const std::string& name, std::vector<Node>* args) {
        size_t nparams = 0;
        bool seen = false;
        for (const FuncDecl& f : funcs)
            if (f.name == name) {
                nparams = f.params.size();
                seen = true;
                break;
            }
        if (!seen || args->size() >= nparams) return true;
        std::vector<Node> merged;
        for (const FuncDecl& f : funcs) {
            if (f.name != name) continue;
            for (size_t i = 0; i < f.defaults.size(); ++i) {
                if (!has_default(f.defaults, i)) continue;
                if (merged.size() <= i) merged.resize(i + 1);
                if (!has_default(merged, i)) merged[i] = f.defaults[i];
            }
        }
        return pad_defaults(merged, nparams, args);
    }
    // Appends the default of every parameter a call left out. Returns false (with an error) if one has none.
    bool pad_defaults(const std::vector<Node>& defaults, size_t nparams, std::vector<Node>* args) {
        while (args->size() < nparams) {
            if (!has_default(defaults, args->size())) {
                {
                    std::string around;
                    for (size_t b = at >= 8 ? at - 8 : 0; b < at && b < tokens.size(); ++b) around += tokens[b].text + " ";
                    error = "too few arguments (in " + current_fn + ", after " + around + ")";
                }
                return false;
            }
            const Node& d = defaults[args->size()];
            if (d.kind == Node::Block && d.text == "tokdef") {
                const size_t saved = at;
                at = static_cast<size_t>(d.number);
                Node made;
                const bool ok = tokens[at].kind == TLBrace ? braced_temp(d.type.struct_id, &made) : parse_expr(1, &made);
                at = saved;
                if (!ok) return false;
                args->push_back(std::move(made));
            } else {
                args->push_back(d);
            }
        }
        return true;
    }

    bool add_dim(Type* type, int length) {
        if (type->array_len < 0) type->array_len = length;
        else type->more.push_back(length);
        return true;
    }
    // A constant expression ([expr.const]): read as a conditional-expression and folded by the parser.
    bool parse_const(uint64_t* value, const char* what) {
        Node n;
        const bool saved_const_ctx = const_ctx;
        const_ctx = true;  // a const local with a constant initializer counts as a constant here
        const bool parsed = parse_expr(2, &n);
        const_ctx = saved_const_ctx;
        if (!parsed) return false;
        if (n.kind != Node::IntLit || n.type.base >= 5) {
            error = std::string("expected ") + what;
            return false;
        }
        *value = n.number;
        return true;
    }
    // [] is read as length 0 ("unsized"): the initializer gives the length (fix_unsized), a
    // parameter becomes a pointer, anything else is an error where it is used.
    bool parse_brackets(Type* type) {
        while (accept(TLBrack)) {
            int length = 0;
            uint64_t folded = 0;
            if (is(TRBrack)) {
                if (type->array_len >= 0) {
                    error = "only the first array length may be left out";
                    return false;
                }
            } else {
                if (!parse_const(&folded, "an array length")) return false;  // numbers, enumerators, template values
            }
            length = static_cast<int>(folded);
            if (!expect(TRBrack, "']'")) return false;
            if (!add_dim(type, length)) return false;
        }
        return true;
    }
    // T a[] = {...}; and char s[] = "..."; the length comes from the initializer ([dcl.array]).
    bool fix_unsized(Type* type, const Node& init) {
        if (type->array_len != 0) return true;
        if (init.kind == Node::Block && init.text == "init") type->array_len = static_cast<int>(init.kids.size());
        else if (init.kind == Node::StringLit && type->base == 2 && type->pointers == 0)
            type->array_len = static_cast<int>(init.text.size()) + 1;
        else {
            error = "an array without a length needs an initializer list";
            return false;
        }
        return true;
    }
    // Brace elision ([dcl.init.aggr]): in `std::array<int, 4> a = {1, 2, 3, 4};` the list names the elements of the
    // array inside the struct, not the struct's one member. Returns the list written with every level braced.
    bool is_aggregate_type(const Type& t) const {
        if (t.is_ref || t.pointers > 0) return false;
        if (t.is_array()) return true;
        return t.base == 3 && t.struct_id >= 0 && !structs[static_cast<size_t>(t.struct_id)].is_union &&
               !class_needs_ctor(structs[static_cast<size_t>(t.struct_id)]);
    }
    Node fill_aggregate(const Type& t, const std::vector<Node>& items, size_t* pos) const {
        Node list;
        list.kind = Node::Block;
        list.text = "init";
        list.number = 7;  // already regular
        std::vector<Type> parts;
        if (t.is_array()) {
            const Type elem = peel_array(t);
            for (int i = 0; i < t.array_len; ++i) parts.push_back(elem);
        } else {
            for (const Field& f : structs[static_cast<size_t>(t.struct_id)].fields)
                if (f.name != "__vptr") parts.push_back(f.type);
        }
        for (const Type& part : parts) {
            if (*pos >= items.size()) break;
            const Node& item = items[*pos];
            const bool braced = item.kind == Node::Block && item.text == "init";
            if (braced) {
                ++*pos;
                list.kids.push_back(is_aggregate_type(part) ? elide_braces(part, item) : item);
            } else if (is_aggregate_type(part) && item.kind != Node::StringLit) {
                list.kids.push_back(fill_aggregate(part, items, pos));
            } else {
                list.kids.push_back(item);
                ++*pos;
            }
        }
        return list;
    }
    Node elide_braces(const Type& t, const Node& list) const {
        if (list.number == 7 || !is_aggregate_type(t)) return list;
        size_t pos = 0;
        return fill_aggregate(t, list.kids, &pos);
    }
    bool parse_init(Node* out);
    bool flatten_init(const Type& type, const Node& node, std::vector<uint8_t>* bytes, int at);

    // At `(`: is it `( Class :: * ...`, the start of a pointer to a member function declarator?
    bool member_for_pointer(const std::string& cls_name, const std::string& m) const {
        const int cls = scope_class(cls_name);
        if (cls < 0) return false;
        for (const Method& cand : structs[static_cast<size_t>(cls)].methods)
            if (cand.name == m && !cand.is_static) return true;
        return false;
    }
    bool member_ptr_ahead() const {
        return is(TLParen) && at + 3 < tokens.size() && tokens[at + 1].kind == TIdent && tokens[at + 2].kind == TColonColon &&
               tokens[at + 3].kind == TStar && scope_class(tokens[at + 1].text) >= 0;
    }
    // `R (C::*name)(P...)` after the result type R: the type is a pointer to the function that takes `C*` (the object) and the
    // parameters P, and it is called as (obj->*name)(args) ([dcl.mptr]). A non-virtual member is held as the address of the
    // function; a virtual one as slot * 8 + 1 (odd), which the call turns into a table lookup. The name may be left out.
    bool parse_member_ptr(const Type& ret, Type* out, std::string* name) {
        next();  // (
        const int cls = scope_class(cur().text);
        next();
        next();  // ::
        next();  // *
        if (is(TIdent)) {
            *name = cur().text;
            next();
        }
        if (!expect(TRParen, "')'")) return false;
        if (!expect(TLParen, "'('")) return false;
        Declarator fp;
        if (!parse_param_list(&fp)) return false;
        const bool is_const_member = accept(TConst);
        (void)is_const_member;
        FuncDecl fd;
        fd.ret = ret;
        Type self;
        self.base = 3;
        self.struct_id = cls;
        self.pointers = 1;
        fd.params.push_back(self);
        for (const Type& pt : fp.params) fd.params.push_back(pt);
        fd.vararg = fp.vararg;
        finish_signature(&fd);
        *out = ret;
        out->pointers = 1;
        out->array_len = -1;
        out->is_ref = false;
        out->func_id = fd.func_id;
        return true;
    }

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
            bool lead_const = accept(TConst);
            const std::string type_name = is(TIdent) ? cur().text : std::string();
            if (!parse_type(&pt, &imp, &exp)) return false;
            if (accept(TConst)) lead_const = true;
            pt.pconst = lead_const;
            if (member_ptr_ahead()) {
                Type mp;
                std::string mname;
                if (!parse_member_ptr(pt, &mp, &mname)) return false;
                out->params.push_back(mp);
                out->param_names.push_back(mname);
                continue;
            }
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
            else if (accept(TAndAnd)) {
                pt.is_ref = true;
                if (pt.pointers == 0) pt.rref = true;
            }
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
            if (pt.array_len >= 0 && pt.more.empty()) {  // T a[] and T a[16] as a parameter are T *a ([dcl.fct])
                pt.array_len = -1;
                ++pt.pointers;
            }
            const size_t param_index = out->params.size();
            if (accept(TAssign)) {
                // A default argument: read where it is declared, so its names mean what they mean here.
                Node def_expr;
                if ((is(TLBrace) && pt.base == 3 && pt.pointers == 0 && pt.struct_id >= 0) ||
                    (is(TIdent) && find_struct(cur().text) >= 0 && typedefs.find(cur().text) == typedefs.end() &&
                     at + 1 < tokens.size() && (tokens[at + 1].kind == TLParen || tokens[at + 1].kind == TLBrace))) {
                    // `= {}`, `= {a, b}` or `= Color(1, 1, 1, 1)`: a class object made for each call. The text is
                    // kept as a position in the token stream and read again at each call, where the temporary
                    // can be made.
                    def_expr.kind = Node::Block;
                    def_expr.text = "tokdef";
                    def_expr.number = at;
                    def_expr.type = pt;
                    int depth = 0;
                    while (!is(TEnd)) {
                        if (is(TLBrace) || is(TLParen) || is(TLBrack)) ++depth;
                        else if (is(TRBrace) || is(TRParen) || is(TRBrack)) {
                            if (depth == 0) break;
                            --depth;
                        } else if (is(TComma) && depth == 0) break;
                        next();
                    }
                } else if (!parse_expr(1, &def_expr)) {
                    return false;
                }
                out->param_defaults.resize(param_index + 1);
                out->param_defaults[param_index] = std::move(def_expr);
            }
            out->params.push_back(pt);
            out->param_names.push_back(pname);
        } while (accept(TComma));
        return expect(TRParen, "')'");
    }

    bool parse_declarator(Type base, Declarator* out) {
        out->type = base;
        if (member_ptr_ahead()) {
            Type mp;
            if (!parse_member_ptr(base, &mp, &out->name)) return false;
            out->type = mp;
            return true;
        }
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
        else if (accept(TAndAnd)) {
            out->type.is_ref = true;
            if (out->type.pointers == 0) out->type.rref = true;
        }
        if (!is(TIdent)) {
            error = "expected a name (in " + current_fn + ", token " + (is(TEnd) ? std::string("end") : cur().text) + ", after " + (at > 0 ? tokens[at - 1].text : std::string()) + ")";
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

    // C::name where C is a class and name is not a type of it (a static member or constant): an expression,
    // so a '(' in front of it does not start a cast.
    bool qualified_value_at(size_t i) const {
        if (i + 2 >= tokens.size() || tokens[i].kind != TIdent || tokens[i + 1].kind != TColonColon ||
            tokens[i + 2].kind != TIdent)
            return false;
        int id = find_struct(tokens[i].text);
        if (id < 0) {
            // A typedef (a template parameter bound to a class) names the class too.
            const auto td = typedefs.find(tokens[i].text);
            if (td != typedefs.end() && td->second.base == 3 && td->second.pointers == 0) id = td->second.struct_id;
        }
        if (id < 0) return false;
        const std::string& member = tokens[i + 2].text;
        if (structs[static_cast<size_t>(id)].tdefs.count(member) > 0) return false;
        const auto etd = typedefs.find(member);
        if (etd != typedefs.end() && etd->second.base == 1 && etd->second.pointers == 0 && enum_values.find(member) == enum_values.end())
            return false;  // Class::Enum: an enumeration declared in the class (kept as a typedef of int under its plain name)
        return find_struct(member) < 0;
    }

    bool is_decl_start() const {
        if (qualified_expr_ahead()) return false;
        if (is(TIdent) && at + 1 < tokens.size() && tokens[at + 1].kind == TLParen && cur_class >= 0 &&
            cur_class < static_cast<int>(structs.size())) {
            // `Bounds(lo, hi);` inside a class that has a member function Bounds: a call, not a declaration of a Bounds object.
            for (const Method& hm : structs[static_cast<size_t>(cur_class)].methods)
                if (hm.name == cur().text) return false;
        }
        if (is(TInt) || is(TVoid) || is(TChar) || is(TUnsigned) || is(TSigned) || is(TLong) || is(TShort) ||
            is(TStruct) || is(TUnion) || is(TEnum) || is(TTypedef) || is(TDeclspec) || is(TStatic) || is(TConst) ||
            is(TFloat) || is(TDouble))
            return true;
        if (is(TIdent) && at + 1 < tokens.size() && (tokens[at + 1].kind == TDot || tokens[at + 1].kind == TArrow) &&
            local_visible(cur().text))
            return false;  // a variable named like a class (`path.clear()`) is the variable
        if (is(TIdent) && variable_named(cur().text) && typedefs.find(cur().text) != typedefs.end() &&
            !(at + 1 < tokens.size() && tokens[at + 1].kind == TColonColon))
            return false;  // a variable named like a type (a parameter `seconds` next to std::chrono::seconds) is the variable
        return (is(TIdent) && typedefs.find(cur().text) != typedefs.end()) || is_class_name() ||
               is_template_name_at(at) || (is(TIdent) && cur().text == "typename") ||
               (is(TIdent) && cur().text == "auto" && at + 1 < tokens.size() &&
                (tokens[at + 1].kind == TIdent || tokens[at + 1].kind == TAmp || tokens[at + 1].kind == TStar ||
                 tokens[at + 1].kind == TConst));
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
        if (i + 2 < tokens.size() && tokens[i + 1].kind == TColonColon && tokens[i + 2].kind == TIdent &&
            (typedefs.find(tokens[i + 2].text) != typedefs.end() ||
             structs[static_cast<size_t>(id)].tdefs.count(tokens[i + 2].text) > 0 ||
             find_nested(id, tokens[i + 2].text) >= 0 ||
             (find_struct(tokens[i + 2].text) >= 0 && structs[static_cast<size_t>(id)].statics.count(tokens[i + 2].text) == 0)))
            return true;  // Struct::Enum, Struct::member_typedef, Struct::Nested
        return i + 1 < tokens.size() &&
               (tokens[i + 1].kind == TIdent || tokens[i + 1].kind == TStar || tokens[i + 1].kind == TAmp ||
                tokens[i + 1].kind == TAndAnd ||
                ((tokens[i + 1].kind == TComma || tokens[i + 1].kind == TGt) && i >= 1 &&
                 (tokens[i - 1].kind == TLt || tokens[i - 1].kind == TComma ||
                  (tokens[i - 1].kind == TConst && i >= 2 && (tokens[i - 2].kind == TLt || tokens[i - 2].kind == TComma)))));  // a template argument: Box<P, 3>, Box<const P>
    }
    bool is_class_name() const {
        if (!is(TIdent) || typedefs.find(cur().text) != typedefs.end()) return false;
        return class_name_at(at);
    }

    bool local_visible(const std::string& name) const;
    bool local_const_value(const std::string& name, uint64_t* value) const {
        if (cur_locals == nullptr) return false;
        for (int i = static_cast<int>(cur_locals->size()) - 1; i >= 0; --i) {
            const Local& local = (*cur_locals)[static_cast<size_t>(i)];
            if (local.name == name && scope_visible(local.scope)) {
                if (!local.has_cval) return false;
                *value = local.cval;
                return true;
            }
        }
        return false;
    }
    bool static_type(const Node& node, Type* out) const;
    bool parse_call_args(std::vector<Node>* args);
    bool expand_pack_arg(Node arg, std::vector<Node>* args);
    std::vector<std::string> packs_in(size_t from, size_t to) const;
    bool parse_pack_element(size_t from, size_t to, size_t k, bool unary, Node* out);
    bool try_fold(Node* out, bool* folded);
    bool parse_new(Node* out);
    bool ensure_vector_helpers(int sid);
    bool parse_delete(Node* out);
    bool method_call(Node* out, Node object, bool arrow, const std::string& name, std::vector<TArg> targs = {},
                     std::vector<Node>* preparsed = nullptr);
    bool operator_binary(Tok op, Node* left, Node* right, bool* done);
    bool operator_unary(Tok op, bool postfix, Node* operand, bool* done);
    bool operator_call(Node* object, std::vector<Node> args, bool* done);
    bool operator_index(Node* object, Node index, bool* done);
    bool free_call(const std::string& name, std::vector<Node> args, Node* out, bool* found);
    bool convert_class(Node* node, const Type& target);
    bool class_object_type(const Node& node, Type* out) const;
    bool operator_name(Tok op, std::string* name) const;
    static Type bool_type() {
        Type t;
        t.base = 2;
        t.is_bool = true;
        t.is_unsigned = true;
        return t;
    }
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
            const_node(it->second, out);
            return true;
        }
        out->kind = Node::Name;
        out->text = it->second.symbol;
        return true;
    }
    // The literal a class constant stands for: integers keep their width and signedness, a float or double
    // its bits.
    static void const_node(const StaticMem& sm, Node* out) {
        out->kind = Node::IntLit;
        out->number = static_cast<uint64_t>(sm.cval);
        out->type = Type{};
        out->type.base = sm.type.base == 5 || sm.type.base == 6 ? sm.type.base : 1;
        if (out->type.base == 1) {
            out->type.wide = sm.type.wide;
            out->type.is_unsigned = sm.type.is_unsigned;
            out->number = const_norm(out->number, out->type);
        }
    }
    // The scalar type a name stands for when it is written like a call, T(x): a typedef of an arithmetic type
    // (also as std::uint64_t); base is -1 when the name is not such a type or a function of that name exists.
    Type functional_cast_type(const std::string& name) const {
        Type none;
        none.base = -1;
        for (const FuncDecl& fd : funcs)
            if (fd.name == name) return none;
        const size_t cc = name.rfind("::");
        const std::string bare = cc == std::string::npos ? name : name.substr(cc + 2);
        const auto td = typedefs.find(bare);
        if (td == typedefs.end()) return none;
        const Type& t = td->second;
        if (t.array_len >= 0 || t.is_ref) return none;
        if (t.pointers != 0) return t.func_id < 0 ? t : none;  // T(x) for a pointer type T
        if (t.base != 1 && t.base != 2 && t.base != 4 && t.base != 5 && t.base != 6) return none;
        return t;
    }
    // True while the operand of & is read: a name there stays a name so its address can be taken.
    bool no_const_subst = false;
    // A global declared const (or constexpr) with a constant initial value, as a literal node.
    bool global_const_node(const std::string& name, Node* out) const {
        for (const Global& g : globals) {
            if (g.name != name) continue;
            const Type& t = g.type;
            if (!t.is_const || g.bss || g.vtable || t.pointers != 0 || t.array_len >= 0 || t.is_ref) return false;
            if (t.base != 1 && t.base != 2 && t.base != 4 && t.base != 5 && t.base != 6) return false;
            const int width = size_of(t);
            if (width < 1 || width > 8 || g.init_bytes.size() < static_cast<size_t>(width)) return false;
            uint64_t v = 0;
            for (int i = width; i-- > 0;) v = (v << 8) | g.init_bytes[static_cast<size_t>(i)];
            *out = Node{};
            out->kind = Node::IntLit;
            if (t.base == 5 || t.base == 6) {
                out->type.base = t.base;
                out->number = v;
                return true;
            }
            out->type.base = 1;
            out->type.wide = t.wide;
            if (t.is_bool) {
                out->number = v != 0 ? 1 : 0;
                return true;
            }
            if (t.base == 2) v = t.is_unsigned ? (v & 0xFFu) : static_cast<uint64_t>(static_cast<int64_t>(static_cast<int8_t>(v)));
            else if (t.base == 4) v = t.is_unsigned ? (v & 0xFFFFu) : static_cast<uint64_t>(static_cast<int64_t>(static_cast<int16_t>(v)));
            else {
                out->type.is_unsigned = t.is_unsigned;
                v = const_norm(v, out->type);
            }
            out->number = v;
            return true;
        }
        return false;
    }
    // A call of a static member function without parameters whose body is just `return <constant>;` is that
    // constant ([expr.const]: a constexpr function called with constant arguments), so
    // std::numeric_limits<float>::infinity() can start a constant initializer.
    bool try_const_call(const Method& m, Node* out) {
        if (!m.params.empty() || !m.has_body || m.tok_body == 0 || m.tok_body + 3 >= tokens.size()) return false;
        if (tokens[m.tok_body].kind != TLBrace || tokens[m.tok_body + 1].kind != TReturn) return false;
        const size_t saved_at = at;
        const std::string saved_error = error;
        // The names in the body mean what they mean in the class: a member of a class template instance is read with
        // the instance's own template arguments bound, not the ones of the code that calls it.
        std::vector<Binding> bound;
        if (m.owner >= 0 && m.owner < static_cast<int>(structs.size()) && !structs[static_cast<size_t>(m.owner)].tmpl.empty()) {
            const StructDef& owner = structs[static_cast<size_t>(m.owner)];
            auto tmpl = class_templates.find(owner.tmpl);
            if (tmpl != class_templates.end()) bound = bind_params(tmpl->second, owner.targs);
        }
        at = m.tok_body + 2;
        Node value;
        bool ok = parse_expr(0, &value) && is(TSemi) && at + 1 < tokens.size() && tokens[at + 1].kind == TRBrace;
        unbind_params(bound);
        at = saved_at;
        error = saved_error;
        if (!ok || value.kind != Node::IntLit) return false;
        uint64_t bits = 0;
        if (!const_convert(value, m.ret, &bits)) return false;
        *out = Node{};
        out->kind = Node::IntLit;
        out->type.base = m.ret.base >= 5 ? m.ret.base : 1;
        out->number = bits;
        if (out->type.base == 1) {
            out->type.wide = m.ret.wide;
            out->type.is_unsigned = m.ret.is_unsigned;
            out->number = const_norm(bits, out->type);
        }
        return true;
    }
    // The StructDef being read by parse_struct_body: a constant declared earlier in the class body can be
    // used by the initializers that follow it.
    StructDef* building = nullptr;
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
        {
            // Outer::Nested (sizeof(Outer::Nested), a cast) names a type when Nested is a class or typedef of Outer.
            int oid = typedefs.find(cur().text) == typedefs.end() ? find_struct(cur().text) : -1;
            if (oid >= 0 && i == at + 1 && typedefs.count(tokens[i + 1].text) > 0 && tokens[i + 2].kind == TRParen) return false;  // (Class::Enum)x
            size_t q = i;
            while (oid >= 0 && i == at + 1 && q + 1 < tokens.size() && tokens[q].kind == TColonColon &&
                   tokens[q + 1].kind == TIdent) {
                if (structs[static_cast<size_t>(oid)].tdefs.count(tokens[q + 1].text) > 0) return false;
                int nested = find_nested(oid, tokens[q + 1].text);
                if (nested < 0) nested = find_struct(tokens[q + 1].text);
                if (nested < 0) break;
                if (!(q + 2 < tokens.size() && tokens[q + 2].kind == TColonColon)) return false;  // the last name is a class
                oid = nested;
                q += 2;
            }
        }
        const Tok after = tokens[i + 2].kind;
        return after != TIdent && after != TStar && after != TAmp && after != TAndAnd && after != TColonColon;
    }
    bool class_name_text(const std::string& text) const {
        if (typedefs.find(text) != typedefs.end()) return false;
        const int id = find_struct(text);
        return id >= 0 && structs[static_cast<size_t>(id)].cxx;
    }
    bool parse_stmt(Node* out, std::vector<Local>* locals);
    bool parse_stmt_core(Node* out, std::vector<Local>* locals);
    bool parse_stmt_inner(Node* out, std::vector<Local>* locals, int stmt_line);
    bool parse_local_decl_one(const Type& type, bool is_typedef, bool is_static, bool is_const, Node* out,
                              std::vector<Local>* locals, int stmt_line, bool* more);
    // The end of a declarator: ',' means another follows, otherwise ';'.
    // `auto x = e;` and `for (T x : array)`: the initializer was read before the declaration, to learn its type;
    // the declaration then takes it from here, where it would have read `= e` (or `: e`).
    bool has_preset = false;
    Node preset_init{};
    size_t preset_end = 0;
    bool range_decl = false;
    int range_count = 0;
    bool end_decl(bool* more) {
        if (range_decl) {
            *more = false;
            return true;
        }
        if (accept(TComma)) {
            *more = true;
            return true;
        }
        *more = false;
        return expect(TSemi, "';'");
    }
    bool parse_block(Node* out, std::vector<Local>* locals);
    // for (declaration : range): a ':' before any ';' at the top level of the parentheses ([stmt.ranged]).
    bool range_for_ahead() const {
        int depth = 0;
        for (size_t i = at; i < tokens.size() && tokens[i].kind != TEnd; ++i) {
            const Tok k = tokens[i].kind;
            if (k == TLParen || k == TLBrack || k == TLBrace) ++depth;
            else if (k == TRParen || k == TRBrack || k == TRBrace) {
                if (depth == 0) return false;
                --depth;
            } else if (depth == 0 && k == TSemi) return false;
            else if (depth == 0 && k == TColon) return true;
        }
        return false;
    }
    bool parse_range_for(Node* out, std::vector<Local>* locals, int stmt_line);
    bool parse_lambda(Node* out);
    // The type_info object of a type: a global with the address of the type's name; the name text is what equality compares
    // (one object per file, so the pointers differ between files). Returns its symbol.
    std::string typeinfo_global(const Type& t) {
        const std::string name = cxx_type(t, &structs);
        const std::string sym = "__ti_" + name;
        for (const Global& g : globals)
            if (g.name == sym) return sym;
        Global text;
        text.name = "__gstr" + std::to_string(gstr_count++);
        text.is_static = true;
        text.bss = false;
        text.type.base = 2;
        text.type.array_len = static_cast<int>(name.size()) + 1;
        text.init_bytes.assign(name.begin(), name.end());
        text.init_bytes.push_back(0);
        globals.push_back(text);
        // The record of the single base class, for dynamic_cast (runtime/rtti.c): { name, base }.
        std::string base_sym;
        if (t.base == 3 && t.pointers == 0 && t.struct_id >= 0 && structs[static_cast<size_t>(t.struct_id)].base >= 0) {
            Type bt;
            bt.base = 3;
            bt.struct_id = structs[static_cast<size_t>(t.struct_id)].base;
            base_sym = typeinfo_global(bt);
        }
        Global ti;
        ti.name = sym;
        ti.is_static = true;
        ti.bss = false;
        ti.type.base = 2;
        ti.type.array_len = 16;
        ti.init_bytes.assign(16, 0);
        ti.addr_relocs.push_back({0, text.name});
        if (!base_sym.empty()) ti.addr_relocs.push_back({8, base_sym});
        globals.push_back(ti);
        return sym;
    }
    bool parse_typeid(Node* out);
    bool parse_dynamic_cast(Node* out);
    bool parse_type_trait(Node* out);
    bool instantiate_generic_lambda(int sid, const std::vector<Node>& args);
    std::vector<Type> auto_types{};
    size_t auto_idx = 0;
    bool braced_temp(int bsid, Node* out);
    // At a '{': is there another '{' inside it, before its matching '}'?
    bool has_nested_braces_ahead() const {
        int depth = 0;
        for (size_t i = at; i < tokens.size() && tokens[i].kind != TEnd; ++i) {
            if (tokens[i].kind == TLBrace) {
                if (depth >= 1) return true;
                ++depth;
            } else if (tokens[i].kind == TRBrace && --depth == 0) return false;
        }
        return false;
    }
    bool parse_condition(Node* cond, Node* decl, bool* scoped, std::vector<Local>* locals);
    void close_condition_scope(Node* stmt, Node decl, bool scoped);
    bool init_list_temp(const Type& list_type, Node* out);
    bool is_init_list_class(const Type& t) const {
        return t.base == 3 && t.pointers == 0 && t.struct_id >= 0 && structs[static_cast<size_t>(t.struct_id)].tmpl == "initializer_list";
    }
    bool has_init_list_ctor(int sid, Type* list_type) const {
        for (const Method& m : structs[static_cast<size_t>(sid)].methods)
            if (m.name == "<ctor>" && m.params.size() == 1 && is_init_list_class(m.params[0])) {
                *list_type = m.params[0];
                return true;
            }
        return false;
    }
    int lambda_count = 0;
    // While a lambda body is read once only to learn the type of its first return.
    bool lam_probe = false;
    bool saw_auto_ret = false;
    bool const_ctx = false;
    bool lam_probe_seen = false;
    Type lam_probe_type{};

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
        while (i < tokens.size() && (tokens[i].kind == TStar || tokens[i].kind == TAmp || tokens[i].kind == TAndAnd)) ++i;
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

    // ---- overload choice (ISO/IEC 14882 [over.match]) ----
    // The rank of converting the argument to a parameter of type p: 0 exact, 1 promotion, 2 conversion, 3 a
    // user-defined conversion; -1 when it cannot be converted. An argument whose type is not known counts as 1.
    bool derives_from(int sid, int base) const {
        for (int walk = sid; walk >= 0; walk = structs[static_cast<size_t>(walk)].base)
            if (walk == base) return true;
        return false;
    }
    static bool arith_type(const Type& t) {
        return t.pointers == 0 && !t.is_array() && t.base != 3 && t.base != 0 && t.func_id < 0;
    }
    // The conversion function of class sid that yields an arithmetic type like p (or a pointer, for bool), or null.
    const Method* conv_for(int sid, const Type& p) const {
        if (sid < 0 || sid >= static_cast<int>(structs.size())) return nullptr;
        const Method* any = nullptr;
        for (const Method& m : structs[static_cast<size_t>(sid)].methods) {
            if (m.name.compare(0, 13, "operator conv") != 0) continue;
            Type r = m.ret;
            r.is_ref = false;
            if (arith_type(p) ? arith_type(r) : (p.pointers == r.pointers && p.base == r.base)) {
                if (r.base == p.base && r.wide == p.wide && r.is_unsigned == p.is_unsigned) return &m;
                if (any == nullptr) any = &m;
            } else if (p.is_bool && r.pointers > 0 && any == nullptr) {
                any = &m;
            }
        }
        return any;
    }
    // An rvalue ([basic.lval]): a call that returns a class by value, the result of std::move, or a hidden temporary.
    bool is_rvalue(const Node& arg) const {
        if (arg.kind == Node::Unary && arg.op == TStar && arg.text == "xval") return true;
        if (arg.kind == Node::Name && (arg.text.compare(0, 4, "__bt") == 0 || arg.text.compare(0, 5, "__tmp") == 0 ||
                                       arg.text.compare(0, 4, "__br") == 0 || arg.text.compare(0, 4, "__il") == 0))
            return true;
        if (arg.kind == Node::Call) {
            Type t;
            return static_type(arg, &t) && t.base == 3 && t.pointers == 0 && !t.is_ref && !t.is_array();
        }
        return false;
    }
    int arg_score(const Node& arg, Type p) const {
        Type a;
        if (!static_type(arg, &a)) return 1;
        const bool want_ref = p.is_ref;
        const bool want_rref = p.rref;
        a.is_ref = false;
        p.is_ref = false;
        if (want_rref && p.pointers == 0 && p.base == 3 && !is_rvalue(arg)) return -1;
        if (a.is_array()) {
            a = peel_array(a);
            a.pointers += 1;
        }
        if (p.pointers > 0) {
            if (a.pointers == p.pointers && a.base == p.base && (p.base != 3 || a.struct_id == p.struct_id) &&
                (a.func_id >= 0) == (p.func_id >= 0))
                return 0;
            if (p.pointers == 1 && p.base == 0 && a.pointers >= 1) return 2;
            if (a.pointers == 1 && p.pointers == 1 && a.base == 3 && p.base == 3 && derives_from(a.struct_id, p.struct_id))
                return 2;
            if (arg.kind == Node::IntLit && arg.number == 0 && a.pointers == 0 && a.base < 5) return 2;
            return -1;
        }
        if (p.base == 3) {
            if (a.pointers != 0 || a.base != 3) {
                // A converting constructor of the parameter's class takes the argument (a string literal for a path or
                // a std::string): a user-defined conversion, ranked after the standard ones ([over.ics.rank]).
                if (p.struct_id >= 0 && p.pointers == 0 && !p.is_array()) {
                    std::vector<Node> probe;
                    probe.push_back(arg);
                    const Method* c = find_ctor(p.struct_id, probe);
                    if (c != nullptr && c->params.size() == 1 && !is_copy_param(*c, p.struct_id) &&
                        !is_move_param(*c, p.struct_id) && call_score(c->params, probe) >= 0)
                        return 4;
                }
                return -1;
            }
            if (a.struct_id == p.struct_id) return want_ref && !want_rref && is_rvalue(arg) ? 1 : 0;
            return derives_from(a.struct_id, p.struct_id) ? 2 : -1;
        }
        if (a.pointers > 0) return p.is_bool && p.base == 2 ? 3 : -1;
        if (a.base == 3) return conv_for(a.struct_id, p) != nullptr ? 3 : -1;
        if (a.base == 0 || p.base == 0) return -1;
        if (a.base == p.base && a.wide == p.wide && a.is_bool == p.is_bool && (a.base >= 5 || a.is_unsigned == p.is_unsigned))
            return 0;
        if (p.base == 6 && a.base == 5) return 1;
        if (p.base == 1 && !p.wide && !p.is_unsigned && (a.base == 2 || a.base == 4)) return 1;
        return 2;
    }
    // The total rank of a call with these arguments, or -1 when one cannot convert.
    int call_score(const std::vector<Type>& params, const std::vector<Node>& args) const {
        int total = 0;
        for (size_t i = 0; i < args.size() && i < params.size(); ++i) {
            const int r = arg_score(args[i], params[i]);
            if (r < 0) return -1;
            total += r;
        }
        return total;
    }

    // The member function name taking these arguments. Among several of the same name the best match by the
    // ranks above wins; obj_const is 1 for a const object, 0 for a non-const one, -1 when not known.
    const Method* find_method(int sid, const std::string& name, size_t nargs, const std::vector<Node>* args = nullptr,
                              int obj_const = -1) const {
        if (sid < 0 || sid >= static_cast<int>(structs.size())) return nullptr;
        const StructDef& def = structs[static_cast<size_t>(sid)];
        if (args != nullptr) {
            const Method* best = nullptr;
            int best_score = 1 << 30;
            const Method* fallback = nullptr;
            for (size_t i = def.methods.size(); i-- > 0;) {
                const Method& m = def.methods[i];
                if (m.name != name) continue;
                const bool exact = m.params.size() == nargs;
                if (!exact && !(nargs < m.params.size() && nargs >= required_args(m.defaults, m.params.size()))) continue;
                if (fallback == nullptr || (exact && fallback->params.size() != nargs)) fallback = &m;
                int score = call_score(m.params, *args);
                if (score < 0) continue;
                if (!exact) score += 100;
                if (obj_const == 1 && !m.is_const) score += 50;
                if (obj_const == 0 && m.is_const) score += 1;
                if (score < best_score) {
                    best_score = score;
                    best = &m;
                }
            }
            return best != nullptr ? best : fallback;
        }
        for (size_t i = def.methods.size(); i-- > 0;)
            if (def.methods[i].name == name && def.methods[i].params.size() == nargs) return &def.methods[i];
        // A function whose last parameters have defaults takes fewer arguments than it has parameters.
        for (size_t i = def.methods.size(); i-- > 0;) {
            const Method& m = def.methods[i];
            if (m.name == name && nargs < m.params.size() && nargs >= required_args(m.defaults, m.params.size())) return &m;
        }
        return nullptr;
    }

    // Free functions that share a name, by the full name ("ns::f"). Every free function is entered, so that an
    // operator function or an overload can be found by its arguments; the first signature keeps the plain
    // internal name, a later one is "name#decorated-parameters".
    struct OvFn {
        std::string sig;
        std::string internal;
        std::string symbol;
        std::vector<Type> params;
        size_t required = 0;
        bool vararg = false;
    };
    std::map<std::string, std::vector<OvFn>> ovl{};

    // The best of these overloads for the arguments: its internal name, or "" when there are none.
    std::string pick_from(const std::vector<const OvFn*>& list, const std::vector<Node>& args, bool strict = false) const {
        if (list.empty()) return std::string();
        if (list.size() == 1 && !strict) return list[0]->internal;
        const OvFn* best = nullptr;
        const OvFn* fallback = nullptr;
        int best_score = 1 << 30;
        for (const OvFn* f : list) {
            const bool exact = f->params.size() == args.size();
            if (!exact && !(args.size() < f->params.size() && args.size() >= f->required) &&
                !(f->vararg && args.size() > f->params.size()))
                continue;
            if (fallback == nullptr || (exact && fallback->params.size() != args.size())) fallback = f;
            int score = call_score(f->params, args);
            if (score < 0) continue;
            if (!exact) score += 100;
            if (score < best_score) {
                best_score = score;
                best = f;
            }
        }
        if (strict) return best != nullptr ? best->internal : std::string();
        return best != nullptr ? best->internal : fallback != nullptr ? fallback->internal : list[0]->internal;
    }
    // The best overload of `base` (the full name) for these arguments, or "" when base has no entry.
    std::string pick_free(const std::string& base, const std::vector<Node>& args) const {
        const auto it = ovl.find(base);
        if (it == ovl.end()) return std::string();
        std::vector<const OvFn*> list;
        for (const OvFn& f : it->second) list.push_back(&f);
        return pick_from(list, args);
    }
    // The same over every namespace: the functions called `name` (an operator is found in the namespaces of
    // its operands in the standard; here every namespace is looked at).
    std::string pick_any_ns(const std::string& name, const std::vector<Node>& args, bool strict = false) const {
        std::vector<const OvFn*> list;
        for (const auto& entry : ovl) {
            const std::string& key = entry.first;
            if (key == name || (key.size() > name.size() + 2 && key.compare(key.size() - name.size(), name.size(), name) == 0 &&
                                key.compare(key.size() - name.size() - 2, 2, "::") == 0))
                for (const OvFn& f : entry.second) list.push_back(&f);
        }
        return pick_from(list, args, strict);
    }

    static bool same_shape(const Type& a, const Type& b) {
        return a.base == b.base && a.pointers == b.pointers && a.is_ref == b.is_ref && a.rref == b.rref &&
               a.struct_id == b.struct_id;
    }
    static bool is_copy_param(const Method& m, int sid) {
        return m.name == "<ctor>" && m.params.size() == 1 && m.params[0].is_ref && !m.params[0].rref &&
               m.params[0].base == 3 && m.params[0].pointers == 0 && m.params[0].struct_id == sid;
    }
    static bool is_move_param(const Method& m, int sid) {
        return m.name == "<ctor>" && m.params.size() == 1 && m.params[0].is_ref && m.params[0].rref &&
               m.params[0].base == 3 && m.params[0].pointers == 0 && m.params[0].struct_id == sid;
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
        int best_score = -1;
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
                if (is_obj != (is_copy_param(m, sid) || is_move_param(m, sid))) continue;
            }
            // Of several constructors of one arity the best match by the ranks of [over.match] wins; a tie, or no
            // viable one, goes to the one declared last (the order used before).
            const int score = args.empty() ? 0 : call_score(m.params, args);
            if (score < 0 && args.size() == 1 && is_move_param(m, sid)) continue;  // an lvalue does not bind to T&&
            if (best == nullptr || best_score < 0 || (score >= 0 && score <= best_score)) {
                best = &m;
                best_score = score;
            }
        }
        if (best == nullptr && args.size() == 1) {
            // An object of the class itself and no copy constructor: not a call of some other one-argument constructor.
            Type at;
            if (static_type(args[0], &at) && at.base == 3 && at.pointers == 0 && !at.is_array() && at.struct_id == sid)
                return nullptr;
        }
        if (best == nullptr) return find_method(sid, "<ctor>", args.size());
        return best;
    }
    bool class_has_copy(int sid) const { return copy_ctor(sid) != nullptr; }
    // The class has an assignment from an object of its own class (written, or made by the compiler).
    bool class_has_assign(int sid) const {
        if (sid < 0 || sid >= static_cast<int>(structs.size())) return false;
        for (const Method& m : structs[static_cast<size_t>(sid)].methods)
            if (m.name == "operator=" && m.params.size() == 1 && m.params[0].base == 3 && m.params[0].struct_id == sid)
                return true;
        return false;
    }
    const Method* move_ctor(int sid) const {
        if (sid < 0 || sid >= static_cast<int>(structs.size())) return nullptr;
        for (const Method& m : structs[static_cast<size_t>(sid)].methods)
            if (is_move_param(m, sid)) return &m;
        return nullptr;
    }
    bool has_move_ctor(int sid) const { return move_ctor(sid) != nullptr; }
    // The object (not a pointer or array) is an lvalue expression of a class with a copy constructor.
    bool wants_copy(const Type& t, const Node& arg) const {
        if (!(t.base == 3 && t.pointers == 0 && !t.is_ref && t.struct_id >= 0 && !t.is_array())) return false;
        if (!class_has_copy(t.struct_id) && !has_move_ctor(t.struct_id)) return false;
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
        int count = 0;  // more than 0: an array of this many objects
    };
    std::vector<Temp> stmt_temps{};
    bool temp_dtor(const Temp& t, Node* call) {
        if (t.count > 0) return element_loop(t.name, t.count, true, t.sid, "<dtor>", call);
        return special_call(t.sid, "<dtor>", node_addr(node_name(t.name)), {}, call);
    }
    std::vector<Node> stmt_pre{};
    int tmp_count = 0;

    // Arguments for a call: a reference parameter gets the address of its argument, a class
    // parameter with a copy constructor gets a copy made by that constructor into a hidden local
    // (destroyed after the statement).
    bool adjust_args(const std::vector<Type>& params, std::vector<Node>* args, size_t first = 0) {
        for (size_t i = first; i < args->size() && i - first < params.size(); ++i) {
            const Type& pt = params[i - first];
            Node& arg = (*args)[i];
            if (arg.kind == Node::Block && arg.text == "tokbrace") {
                const size_t saved = at;
                at = static_cast<size_t>(arg.number);
                Node made;
                bool ok;
                if (is_init_list_class(pt)) {
                    ok = init_list_temp(pt, &made);
                } else if (pt.base == 3 && pt.pointers == 0 && pt.struct_id >= 0) {
                    ok = braced_temp(pt.struct_id, &made);
                } else if (tokens[at + 1].kind == TRBrace) {
                    made.kind = Node::IntLit;
                    made.number = 0;
                    made.type.base = 1;
                    ok = true;
                } else {
                    next();
                    ok = parse_expr(1, &made);
                }
                at = saved;
                if (!ok) return false;
                arg = std::move(made);
            }
            if (pt.base == 3 && pt.pointers == 0 && pt.struct_id >= 0 && !pt.is_array() && cur_locals != nullptr &&
                class_needs_ctor(structs[static_cast<size_t>(pt.struct_id)]) && !(arg.kind == Node::Block)) {
                // An argument that is not an object of the parameter's class (a string literal for a std::string, a lambda
                // for a std::function) is converted by a converting constructor into a temporary first ([over.best.ics],
                // [class.conv.ctor]); the temporary lives to the end of the statement.
                Type from;
                bool convert = false;
                if (static_type(arg, &from) && !from.is_array()) {
                    const bool is_class = from.base == 3 && from.pointers == 0;
                    bool related = false;
                    if (is_class)
                        for (int walk = from.struct_id; walk >= 0; walk = structs[static_cast<size_t>(walk)].base)
                            if (walk == pt.struct_id) related = true;
                    if (!related && !(is_class && conv_for(from.struct_id, pt) != nullptr)) {
                        std::vector<Node> probe;
                        probe.push_back(arg);
                        const Method* c = find_ctor(pt.struct_id, probe);
                        convert = (c != nullptr && c->params.size() == 1 && !is_copy_param(*c, pt.struct_id) &&
                                   !is_move_param(*c, pt.struct_id) && call_score(c->params, probe) >= 0) ||
                                  structs[static_cast<size_t>(pt.struct_id)].ctor_tmpl;
                    }
                }
                if (convert) {
                    Local hidden;
                    hidden.name = "__cv" + std::to_string(tmp_count++);
                    hidden.type = pt;
                    hidden.type.is_ref = false;
                    hidden.type.rref = false;
                    place_local(cur_locals, hidden);
                    std::vector<Node> cargs;
                    cargs.push_back(std::move(arg));
                    Node ctor_stmt;
                    if (!special_call(pt.struct_id, "<ctor>", node_addr(node_name(hidden.name)), std::move(cargs), &ctor_stmt))
                        return false;
                    stmt_pre.push_back(std::move(ctor_stmt));
                    if (class_needs_dtor(structs[static_cast<size_t>(pt.struct_id)]))
                        stmt_temps.push_back(Temp{hidden.name, pt.struct_id});
                    arg = node_name(hidden.name);
                }
            }
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
            } else if (cur_locals != nullptr && arg.kind == Node::Call && pt.base == 3 && pt.pointers == 0 && !pt.is_array() &&
                       pt.struct_id >= 0 && !class_needs_dtor(structs[static_cast<size_t>(pt.struct_id)])) {
                // A call that returns an object, passed by value: the result goes into a hidden local first (it has no address
                // of its own), and that local is what the parameter is copied from.
                Type rt_call;
                if (static_type(arg, &rt_call) && rt_call.base == 3 && rt_call.pointers == 0 && rt_call.struct_id == pt.struct_id) {
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
                    arg = node_name(hidden.name);
                }
            } else if (cur_locals != nullptr && !pt.is_ref && pt.base == 3 && pt.pointers == 0 && pt.struct_id >= 0 &&
                       !pt.is_array() && arg.kind == Node::Call && !wants_copy(pt, arg)) {
                // A by-value class parameter given the result of a call (take(mk())): the result goes into a hidden local,
                // which is passed as the argument and destroyed after the statement (the copy is elided, [class.copy.elision]).
                Type rt;
                if (static_type(arg, &rt) && rt.base == 3 && rt.pointers == 0 && !rt.is_ref && rt.struct_id == pt.struct_id) {
                    Local held;
                    held.name = "__bt" + std::to_string(tmp_count++);
                    held.type = rt;
                    place_local(cur_locals, held);
                    Node hold_assign;
                    hold_assign.kind = Node::Assign;
                    hold_assign.op = TAssign;
                    hold_assign.number = 1;
                    hold_assign.kids.push_back(node_name(held.name));
                    hold_assign.kids.push_back(std::move(arg));
                    Node hold_stmt;
                    hold_stmt.kind = Node::ExprStmt;
                    hold_stmt.kids.push_back(std::move(hold_assign));
                    stmt_pre.push_back(std::move(hold_stmt));
                    if (class_needs_dtor(structs[static_cast<size_t>(rt.struct_id)]))
                        stmt_temps.push_back(Temp{held.name, rt.struct_id});
                    arg = node_name(held.name);
                }
            } else if (cur_locals != nullptr && wants_copy(pt, arg)) {
                Local hidden;
                hidden.name = "__tmp" + std::to_string(tmp_count++);
                hidden.type = pt;
                place_local(cur_locals, hidden);
                const Method* cm = is_rvalue(arg) && has_move_ctor(pt.struct_id) ? move_ctor(pt.struct_id) : copy_ctor(pt.struct_id);
                if (cm == nullptr) {
                    error = "this class cannot be copied";
                    return false;
                }
                if (arg.kind == Node::Call) {
                    // The argument is the result of a call: it is not in memory until it goes into a hidden local, and
                    // the copy or move is then made from that.
                    Type rt;
                    if (static_type(arg, &rt) && rt.base == 3 && rt.pointers == 0 && !rt.is_ref && rt.struct_id >= 0) {
                        Local held;
                        held.name = "__bt" + std::to_string(tmp_count++);
                        held.type = rt;
                        place_local(cur_locals, held);
                        Node hold_assign;
                        hold_assign.kind = Node::Assign;
                        hold_assign.op = TAssign;
                        hold_assign.number = 1;
                        hold_assign.kids.push_back(node_name(held.name));
                        hold_assign.kids.push_back(std::move(arg));
                        Node hold_stmt;
                        hold_stmt.kind = Node::ExprStmt;
                        hold_stmt.kids.push_back(std::move(hold_assign));
                        stmt_pre.push_back(std::move(hold_stmt));
                        if (class_needs_dtor(structs[static_cast<size_t>(rt.struct_id)]))
                            stmt_temps.push_back(Temp{held.name, rt.struct_id});
                        arg = node_name(held.name);
                    }
                }
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

    // The parameter types of a function as decorated-name text: two functions of one name are the same function
    // when this and the const-ness agree ([over.load]).
    std::string param_sig(const std::vector<Type>& params) const {
        std::string out;
        for (const Type& t : params) out += cxx_type(t, &structs);
        return out;
    }
    bool add_method(StructDef* def, int self, Method m) {
        m.owner = self;
        size_t found = def->methods.size();
        const std::string new_sig = param_sig(m.params);
        bool collide = false;
        for (size_t i = 0; i < def->methods.size(); ++i)
            if (def->methods[i].name == m.name && def->methods[i].params.size() == m.params.size()) {
                bool same = true;
                if (m.name == "<ctor>") {
                    for (size_t k = 0; k < m.params.size(); ++k)
                        if (!same_shape(def->methods[i].params[k], m.params[k])) same = false;
                } else if (m.name != "<dtor>") {
                    same = param_sig(def->methods[i].params) == new_sig && def->methods[i].is_const == m.is_const;
                }
                if (same) found = i;
                else collide = true;
            }
        if (found < def->methods.size() && def->methods[found].is_virtual) {
            m.is_virtual = true;
            m.slot = def->methods[found].slot;
        }
        if (m.is_virtual) {
            if (!def->poly) {
                // The table pointer goes first in a class without a base. In a class derived from a class without virtual
                // functions it goes after the base's members (the base keeps its place at the start of the object, so a
                // pointer to the derived class is also a pointer to the base); typeid of such an object is not supported.
                Field vptr;
                vptr.name = "__vptr";
                vptr.type.base = 0;
                vptr.type.pointers = 2;
                def->fields.insert(def->fields.begin() + static_cast<std::ptrdiff_t>(def->base >= 0 ? def->base_fields : 0), vptr);
                def->poly = true;
            }
            if (m.slot < 0) {
                m.slot = static_cast<int>(def->vtable.size());
                def->vtable.push_back("");
            }
        }
        m.qname = def->name + "::" + m.name + "/" + std::to_string(m.params.size());
        if (collide) m.qname += "~" + new_sig + (m.is_const ? "K" : "");
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

    // `= expr` or `{expr}` or `{}` after a member name. The expression is parsed again, where it is
    // used, in the constructor ([class.base.init]); here its tokens are only stepped over.
    bool read_member_init(Field* field) {
        const bool braced = is(TLBrace);
        if (!braced && !accept(TAssign)) return true;
        if (is(TLBrace) && !braced) {
            // `T x = {...};` ([dcl.init.list]): the list is read again where it is used.
            field->init_at = static_cast<int>(at);
            field->init_aggr = true;
            int brace_depth = 0;
            while (!is(TEnd)) {
                const Tok k = cur().kind;
                if (k == TLParen || k == TLBrack || k == TLBrace) ++brace_depth;
                else if (k == TRParen || k == TRBrack || k == TRBrace) {
                    if (--brace_depth == 0) {
                        next();
                        break;
                    }
                }
                next();
            }
            return true;
        }
        if (braced) {
            next();
            if (accept(TRBrace)) {
                field->init_zero = true;
                return true;
            }
        }
        field->init_at = static_cast<int>(at);
        field->init_braced = braced;
        int depth = 0;
        while (!is(TEnd)) {
            const Tok k = cur().kind;
            if (k == TLParen || k == TLBrack || k == TLBrace) ++depth;
            else if (k == TRParen || k == TRBrack || k == TRBrace) {
                if (depth == 0) break;
                --depth;
            } else if (depth == 0 && (k == TSemi || (k == TComma && !braced))) break;
            next();
        }
        if (braced && !expect(TRBrace, "'}'")) return false;
        return true;
    }

    int struct_nest = 0;
    // From the ':' of a constructor's member-initializer list (or after it) to the '{' of its body: a '{' that follows a name or a
    // '>' opens the braces of one initializer (`a{}`, `b{1, 2}`) and is skipped with its contents.
    void skip_ctor_init_list() {
        while (!is(TEnd) && !is(TSemi)) {
            if (is(TLBrace)) {
                const bool member_brace = at > 0 && (tokens[at - 1].kind == TIdent || tokens[at - 1].kind == TGt);
                if (!member_brace) return;
                int depth = 0;
                do {
                    if (is(TLBrace)) ++depth;
                    else if (is(TRBrace)) --depth;
                    next();
                } while (depth > 0 && !is(TEnd));
                continue;
            }
            next();
        }
    }
    bool pending_extern = false;
    std::vector<int> class_ids{};  // the classes whose bodies are being read, innermost last
    std::vector<std::pair<int, size_t>> deferred_bodies{};
    bool parse_struct_body(bool as_union, std::string name, int* out_id, bool is_class = false, int base_id = -1) {
        struct NestGuard {
            int& n;
            explicit NestGuard(int& x) : n(x) { ++n; }
            ~NestGuard() { --n; }
        } nest_guard(struct_nest);
        if (name.empty()) name = "__anon" + std::to_string(structs.size());
        StructDef def;
        struct BuildGuard {
            StructDef** slot;
            StructDef* old;
            ~BuildGuard() { *slot = old; }
        } build_guard{&building, building};
        building = &def;
        def.name = name;
        if (!pending_sym.empty()) {
            def.tmpl = inst_base_name;
            def.targs = inst_args;
        }
        const bool is_instance_body = !pending_sym.empty();
        def.sym_name = pending_sym;
        pending_sym.clear();
        // A class nested in a template instance has its own name for its constructors and destructor.
        const std::string ctor_name = inst_base_name.empty() || !is_instance_body ? name : inst_base_name;
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
            def.tdefs = base.tdefs;  // the base's typedefs (size_type, iterator) are names of the derived class too
        }
        const std::map<std::string, uint64_t> enums_before = enum_values;
        if (inst_base_name.empty() && !class_ids.empty()) def.outer = class_ids.back();
        // A class nested in another is decorated with its outer class (?AUCell@Pathfinding@@), as MSVC does; two nested classes
        // of the same name in different classes are then different types (and different template arguments).
        if (def.outer >= 0 && def.sym_name.empty() && !is_instance_body)
            def.sym_name = name + "@" + structs[static_cast<size_t>(def.outer)].symname();
        int self = -1;
        if (inst_base_name.empty() && !is_instance_body) {
            const int earlier = find_struct(name);
            if (earlier >= 0 && structs[static_cast<size_t>(earlier)].incomplete &&
                (structs[static_cast<size_t>(earlier)].outer == def.outer || def.outer < 0)) {
                self = earlier;
                if (def.outer < 0) {  // struct Outer::Inner { ... } written outside Outer: it is the placeholder's class
                    def.outer = structs[static_cast<size_t>(earlier)].outer;
                    def.sym_name = structs[static_cast<size_t>(earlier)].sym_name;
                }
            }
        }
        if (self >= 0) {
            structs[static_cast<size_t>(self)] = def;  // the incomplete entry becomes this class
        } else {
            structs.push_back(def);
            self = static_cast<int>(structs.size()) - 1;
        }
        class_ids.push_back(self);
        struct ClassIdGuard {
            std::vector<int>& v;
            ~ClassIdGuard() { v.pop_back(); }
        } class_id_guard{class_ids};
        if (!expect(TLBrace, "'{'")) return false;
        char access = is_class ? 'A' : 'Q';
        // typedef and using names of this class are visible to the members after them.
        struct ScopedTd {
            std::string name;
            bool had = false;
            Type old{};
        };
        std::vector<ScopedTd> scoped_tds;
        for (const auto& inherited_td : def.tdefs) {
            ScopedTd saved_td;
            saved_td.name = inherited_td.first;
            auto old_td = typedefs.find(inherited_td.first);
            if (old_td != typedefs.end()) {
                saved_td.had = true;
                saved_td.old = old_td->second;
            }
            scoped_tds.push_back(std::move(saved_td));
            typedefs[inherited_td.first] = inherited_td.second;
        }
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
                bool declared = false;
                if (!is_using && is(TLParen) && at + 1 < tokens.size() && tokens[at + 1].kind == TStar) {
                    // typedef R (*F)(A);  inside a class: a pointer to function, read like any declarator.
                    Declarator fdecl;
                    if (!parse_declarator(aliased, &fdecl)) return false;
                    alias = fdecl.name;
                    aliased = fdecl.type;
                    declared = true;
                }
                while (!declared && accept(TStar)) ++aliased.pointers;
                if (declared) {
                } else if (accept(TAmp)) aliased.is_ref = true;  // typedef T& reference;
                else if (accept(TAndAnd)) {
                    aliased.is_ref = true;
                    if (aliased.pointers == 0) aliased.rref = true;
                }
                if (!is_using && !declared) {
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
            if (is(TIdent) && cur().text == "friend") {
                // friend class X; and friend struct X; ([class.friend]): access is not checked here, so nothing to record.
                // (A friend function with a body is not supported.)
                size_t k = at;
                while (k < tokens.size() && tokens[k].kind != TSemi && tokens[k].kind != TLBrace && tokens[k].kind != TEnd) ++k;
                if (k < tokens.size() && tokens[k].kind == TSemi) {
                    at = k + 1;
                    continue;
                }
            }
            if (is(TStatic)) {
                static_member = true;
                next();
                if (is(TConst)) {
                    static_const = true;
                    next();
                }
            }
            // constexpr (read as const) in front of a constructor means nothing here.
            if (is(TConst) && at + 2 < tokens.size() && tokens[at + 1].kind == TIdent && tokens[at + 1].text == ctor_name &&
                tokens[at + 2].kind == TLParen)
                next();
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
                if (is(TAssign) && at + 2 < tokens.size() &&
                    (tokens[at + 1].kind == TDefault || (tokens[at + 1].kind == TIdent && tokens[at + 1].text == "delete")) &&
                    tokens[at + 2].kind == TSemi) {
                    // `= default` and `= delete` ([dcl.fct.def.default], [dcl.fct.def.delete]). A defaulted
                    // default constructor is the one the compiler makes (it runs the member initializers); a
                    // defaulted copy constructor or destructor is what the compiler makes anyway; a deleted
                    // function is simply not declared (a call of it then finds no such function).
                    const bool want_default = tokens[at + 1].kind == TDefault;
                    next();
                    next();
                    next();
                    def.cxx = true;
                    if (want_default && is_ctor && decl.params.empty()) {
                        Method dm;
                        dm.name = "<ctor>";
                        dm.ret.base = 3;
                        dm.ret.struct_id = self;
                        dm.ret.pointers = 1;
                        dm.implicit = true;
                        dm.has_body = true;
                        dm.body_at = 1;
                        dm.access = access;
                        if (!add_method(&def, self, std::move(dm))) return false;
                    }
                    continue;
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
                m.defaults = decl.param_defaults;
                m.is_virtual = is_virtual && is_dtor;
                if (is_virtual && !is_dtor) {
                    error = "a constructor cannot be virtual";
                    return false;
                }
                m.access = access;
                if (is(TColon) && is_ctor) {
                    m.body_at = at;
                    m.has_body = true;
                    skip_ctor_init_list();
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
            {
                // `T& operator=(const T&) = default;` and `= delete;`: the compiler's own copy assignment (a
                // plain member copy) is used, or none is declared. Other operators are not read.
                size_t semi = at;
                int paren_depth = 0;
                bool has_operator = false;
                for (; semi < tokens.size() && tokens[semi].kind != TEnd; ++semi) {
                    if (tokens[semi].kind == TIdent && tokens[semi].text.compare(0, 8, "operator") == 0) has_operator = true;
                    if (tokens[semi].kind == TLParen) ++paren_depth;
                    else if (tokens[semi].kind == TRParen) --paren_depth;
                    else if (paren_depth == 0 && (tokens[semi].kind == TSemi || tokens[semi].kind == TLBrace ||
                                                  tokens[semi].kind == TRBrace))
                        break;
                }
                if (has_operator && semi >= at + 3 && semi < tokens.size() && tokens[semi].kind == TSemi &&
                    tokens[semi - 2].kind == TAssign &&
                    (tokens[semi - 1].kind == TDefault || (tokens[semi - 1].kind == TIdent && tokens[semi - 1].text == "delete"))) {
                    at = semi + 1;
                    def.cxx = true;
                    continue;
                }
            }
            Field field;
            bool imp = false;
            bool exp = false;
            saw_auto_ret = false;
            if (!parse_type(&field.type, &imp, &exp)) return false;
            const bool member_auto_ret = saw_auto_ret;
            saw_auto_ret = false;
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
                m.defaults = decl.param_defaults;
                m.is_virtual = is_virtual;
                m.is_static = static_member;
                m.access = access;
                m.auto_ret = member_auto_ret;
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
            const Type base_type = field.type;
            while (accept(TStar)) ++field.type.pointers;
            if (!static_member && (accept(TAmp) || accept(TAndAnd))) {
                ++field.type.pointers;
                field.ref = true;
            }
            if (!static_member && ((is(TLParen) && at + 2 < tokens.size() && tokens[at + 1].kind == TStar &&
                                    tokens[at + 2].kind == TIdent) ||
                                   member_ptr_ahead())) {
                // R (*name)(A, B); a pointer-to-function member, read like any declarator.
                Declarator fdecl;
                if (!parse_declarator(base_type, &fdecl)) return false;
                field.name = fdecl.name;
                field.type = fdecl.type;
            } else {
            if (!is(TIdent)) {
                error = "expected a field name";
                return false;
            }
            field.name = cur().text;
            next();
            if (!parse_brackets(&field.type)) return false;
            }
            if (field.type.array_len == 0 && !static_member) {
                error = "an array member needs a length";
                return false;
            }
            if (static_member) {
                // static T name; or static const T name = constant; ([class.static.data]); `static int a = 0, b = 1;`
                auto more_statics = [&]() -> int {
                    if (!accept(TComma)) return 0;
                    field.type = base_type;
                    while (accept(TStar)) ++field.type.pointers;
                    if (!is(TIdent)) {
                        error = "expected a field name";
                        return -1;
                    }
                    field.name = cur().text;
                    next();
                    return parse_brackets(&field.type) ? 1 : -1;
                };
                for (;;) {
                StaticMem sm;
                sm.type = field.type;
                sm.type.is_const = static_const;
                def.cxx = true;
                if (accept(TAssign) || is(TLBrace)) {
                    Node init;
                    if (!parse_init(&init)) return false;
                    const bool constant_value = static_const && sm.type.pointers == 0 && !sm.type.is_array() &&
                                                sm.type.base != 3;
                    if (!constant_value) {
                        // `static inline float x = 0.0f;`, a mutable `static inline int n = 0;`, an array or a pointer: a
                        // variable of the program with its initial value (one copy per file here).
                        if (!fix_unsized(&sm.type, init)) return false;
                        sm.symbol = static_symbol(def, field.name, sm.type);
                        if (!sm.type.is_array() && sm.type.base == 3 && sm.type.pointers == 0 && sm.type.struct_id >= 0 &&
                            (obj_needs_ctor(sm.type) || obj_needs_dtor(sm.type)) && init.kind != Node::Block) {
                            // `static inline Vec3 listener = Vec3::Zero;`: an object with a constructor, made at startup
                            // from the initializer (copy or converting constructor), like a global object.
                            Global g;
                            g.name = sm.symbol;
                            g.type = sm.type;
                            g.bss = true;
                            g.is_static = true;
                            globals.push_back(g);
                            std::vector<Node> gargs;
                            gargs.push_back(std::move(init));
                            if (!register_global_object(sm.symbol, sm.type, std::move(gargs), nullptr)) return false;
                            def.statics[field.name] = sm;
                            const int more_o = more_statics();
                            if (more_o < 0) return false;
                            if (more_o > 0) continue;
                            if (!expect(TSemi, "';'")) return false;
                            break;
                        }
                        Global g;
                        g.name = sm.symbol;
                        g.type = sm.type;
                        g.bss = false;
                        g.is_static = true;
                        init_relocs = &g.addr_relocs;
                        const bool flat_ok = flatten_init(sm.type, init, &g.init_bytes, 0);
                        init_relocs = nullptr;
                        if (!flat_ok) return false;
                        globals.push_back(std::move(g));
                        def.statics[field.name] = sm;
                        const int more_g = more_statics();
                        if (more_g < 0) return false;
                        if (more_g > 0) continue;
                        if (!expect(TSemi, "';'")) return false;
                        break;
                    }
                    std::vector<uint8_t> bytes;
                    if (!flatten_init(sm.type, init, &bytes, 0)) return false;
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
                def.statics[field.name] = sm;
                const int more_s = more_statics();
                if (more_s < 0) return false;
                if (more_s > 0) continue;
                if (!expect(TSemi, "';'")) return false;
                break;
                }
                continue;
            }
            for (;;) {  // float x, y, z; every name gets the type read before the first one
                if (accept(TColon)) {
                    if (!is(TNumber)) {
                        error = "expected a bitfield width";
                        return false;
                    }
                    field.type.bits = static_cast<int>(cur().number);
                    next();
                }
                if (!read_member_init(&field)) return false;
                if (field.type.base == 3 && field.type.pointers == 0 && field.type.array_len < 0 && field.type.struct_id == self) {
                    error = "struct field is incomplete";
                    return false;
                }
                def.fields.push_back(field);
                if (!accept(TComma)) break;
                field = Field{};
                field.type = base_type;
                while (accept(TStar)) ++field.type.pointers;
                if (accept(TAmp)) {
                    ++field.type.pointers;
                    field.ref = true;
                }
                if (!is(TIdent)) {
                    error = "expected a field name";
                    return false;
                }
                field.name = cur().text;
                next();
                if (!parse_brackets(&field.type)) return false;
            }
            if (!expect(TSemi, "';'")) return false;
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
            {
                // The implicit copy assignment ([class.copy.assign]): a member that is a class with an assignment of its
                // own (std::string, a container) is assigned by it, not copied byte by byte.
                bool user_assign = false;
                for (const Method& m : def.methods)
                    if (m.name == "operator=" && m.owner == self && m.params.size() == 1 && m.params[0].base == 3 &&
                        m.params[0].struct_id == self)
                        user_assign = true;
                bool member_assigns = false;
                for (size_t fi = 0; fi < def.fields.size(); ++fi) {
                    const Type& ft = def.fields[fi].type;
                    if (ft.base == 3 && ft.pointers == 0 && !ft.is_ref && !ft.is_array() && ft.struct_id >= 0 &&
                        ft.struct_id != self && !def.fields[fi].ref && class_has_assign(ft.struct_id))
                        member_assigns = true;
                }
                if (!user_assign && member_assigns) {
                    Method m;
                    m.name = "operator=";
                    m.ret.base = 0;
                    Type self_ref;
                    self_ref.base = 3;
                    self_ref.struct_id = self;
                    self_ref.is_ref = true;
                    m.params.push_back(self_ref);
                    m.param_names.push_back("__o");
                    m.implicit = true;
                    m.implicit_assign = true;
                    m.has_body = true;
                    m.body_at = 1;
                    if (!add_method(&def, self, std::move(m))) return false;
                }
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
            {
                Type self_type;
                self_type.base = 3;
                self_type.struct_id = self;
                table.rtti_sym = typeinfo_global(self_type);
            }
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
        // so a body may use members declared after it. A nested class is complete before its enclosing class is, and
        // its bodies may use the enclosing class's members declared later ([class.mem] complete-class context), so
        // they wait until the outermost class ends.
        if (struct_nest > 1 && inst_base_name.empty()) {
            for (size_t mi = 0; mi < def.methods.size(); ++mi)
                if (def.methods[mi].body_at != 0 && def.methods[mi].owner == self)
                    deferred_bodies.push_back({self, mi});
        } else {
            for (size_t mi = 0; mi < def.methods.size(); ++mi)
                if (def.methods[mi].body_at != 0 && def.methods[mi].owner == self)
                    if (!define_method(self, mi)) return false;
            if (struct_nest == 1) {
                while (!deferred_bodies.empty()) {
                    const std::vector<std::pair<int, size_t>> batch = std::move(deferred_bodies);
                    deferred_bodies.clear();
                    for (const auto& item : batch)
                        if (!define_method(item.first, item.second)) return false;
                }
            }
        }
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
    // Compile the body once with the result type left open to learn the type of its first `return expr;`. The function the
    // probe made is dropped; `at` is at the body's '{' and is left changed.
    bool probe_auto_ret(FuncDecl fn, int cls, Type* found) {
        const bool saved_probe = lam_probe;
        const bool saved_seen = lam_probe_seen;
        const Type saved_type = lam_probe_type;
        lam_probe = true;
        lam_probe_seen = false;
        lam_probe_type = Type{};
        lam_probe_type.base = 0;
        fn.ret = Type{};
        fn.ret.base = 1;
        std::vector<Node> keep_pre = std::move(stmt_pre);
        stmt_pre.clear();
        std::vector<Temp> keep_temps = std::move(stmt_temps);
        stmt_temps.clear();
        const bool ok = parse_function_body(&fn, cls);
        stmt_pre = std::move(keep_pre);
        stmt_temps = std::move(keep_temps);
        *found = lam_probe_seen ? lam_probe_type : Type{};
        lam_probe = saved_probe;
        lam_probe_seen = saved_seen;
        lam_probe_type = saved_type;
        return ok;
    }
    bool define_method(int sid, size_t mi) {
        const Method m = structs[static_cast<size_t>(sid)].methods[mi];
        // The typedefs and aliases of the class mean what they mean in the class while its member bodies are read (a body
        // compiled after the class, or a member template instance, no longer has them in scope otherwise).
        struct ScopeTd {
            std::map<std::string, Type>& table;
            std::vector<std::pair<std::string, std::pair<bool, Type>>> saved;
            ~ScopeTd() {
                for (size_t k = saved.size(); k-- > 0;) {
                    if (saved[k].second.first) table[saved[k].first] = saved[k].second.second;
                    else table.erase(saved[k].first);
                }
            }
        } scope_td{typedefs, {}};
        for (const auto& td : structs[static_cast<size_t>(sid)].tdefs) {
            auto had = typedefs.find(td.first);
            scope_td.saved.push_back({td.first, {had != typedefs.end(), had != typedefs.end() ? had->second : Type{}}});
            typedefs[td.first] = td.second;
        }
        FuncDecl fn;
        fn.name = m.qname;
        fn.symbol = m.symbol;
        fn.ret = m.ret;
        fn.is_static = !m.out_of_class;  // a member written in its class is an inline function: an internal symbol
        fn.comdat = !m.out_of_class && anon_depth == 0 && structs[static_cast<size_t>(sid)].name.compare(0, 2, "__") != 0 &&
                    !m.symbol.empty() && !m.implicit;
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
        if (m.auto_ret) {
            Type found;
            const bool probe_ok = probe_auto_ret(fn, sid, &found);
            at = m.body_at;
            if (!probe_ok) {
                unbind_params(alias_bound);
                at = resume;
                return false;
            }
            Method& mm = structs[static_cast<size_t>(sid)].methods[mi];
            mm.ret = found;
            mm.auto_ret = false;
            mm.symbol = cxx_method_symbol(structs, structs[static_cast<size_t>(sid)], mm);
            fn.ret = found;
            fn.symbol = mm.symbol;
            finish_signature(&fn);
        }
        const bool ok = parse_function_body(&fn, sid, m.name == "<ctor>" ? 1 : m.name == "<dtor>" ? 2 : m.implicit_assign ? 3 : 0, m.implicit);
        unbind_params(alias_bound);
        at = resume;
        if (!ok) return false;
        structs[static_cast<size_t>(sid)].methods[mi].tok_body = m.body_at;
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
        if (special == 3) {
            // The implicit copy assignment: each member in order, by its own assignment when it has one.
            Node block;
            block.kind = Node::Block;
            block.text = "scope";
            for (size_t fi = 0; fi < def.fields.size(); ++fi) {
                const Field& field = def.fields[fi];
                if (field.name == "__vptr") continue;
                Node theirs;
                theirs.kind = Node::Field;
                theirs.op = TDot;
                theirs.text = field.name;
                theirs.kids.push_back(node_name("__o"));
                const Type& ft = field.type;
                if (ft.base == 3 && ft.pointers == 0 && !ft.is_ref && !ft.is_array() && ft.struct_id >= 0 &&
                    !field.ref && class_has_assign(ft.struct_id)) {
                    std::vector<Node> args;
                    args.push_back(std::move(theirs));
                    Node call;
                    if (!method_call(&call, node_this_field(field.name), false, "operator=", {}, &args)) return false;
                    Node stmt;
                    stmt.kind = Node::ExprStmt;
                    stmt.kids.push_back(std::move(call));
                    block.kids.push_back(std::move(stmt));
                } else {
                    Node assign;
                    assign.kind = Node::Assign;
                    assign.op = TAssign;
                    assign.number = 1;
                    assign.kids.push_back(node_this_field(field.name));
                    assign.kids.push_back(std::move(theirs));
                    Node stmt;
                    stmt.kind = Node::ExprStmt;
                    stmt.kids.push_back(std::move(assign));
                    block.kids.push_back(std::move(stmt));
                }
            }
            fn->body = std::move(block);
            return true;
        }
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
                while (is(TColonColon) && at + 1 < tokens.size() && tokens[at + 1].kind == TIdent) {
                    next();  // std::string(s): the base class named with its namespace
                    init.name = cur().text;
                    next();
                }
                const bool braced_init = is(TLBrace);  // `member{a, b}` and `member{}` mean what `member(a, b)` and `member()` mean
                if (braced_init) next();
                else if (!expect(TLParen, "'('")) return false;
                const size_t hoist_from = stmt_pre.size();
                if (braced_init) {
                    if (!accept(TRBrace)) {
                        do {
                            Node a;
                            if (!parse_expr(1, &a)) return false;
                            init.args.push_back(std::move(a));
                        } while (accept(TComma));
                        if (!expect(TRBrace, "'}'")) return false;
                    }
                } else if (!parse_call_args(&init.args)) {
                    return false;
                }
                // Statements an argument needs before it (`p(new int(k))`: the allocation and the store) run before
                // the members are made.
                for (size_t h = hoist_from; h < stmt_pre.size(); ++h) pre.push_back(std::move(stmt_pre[h]));
                stmt_pre.resize(hoist_from);
                inits.push_back(std::move(init));
            } while (accept(TComma));
        }
        int delegating = -1;
        if (special == 1 && !implicit) {
            std::string plain = def.name;
            const size_t lt = plain.find('<');
            if (lt != std::string::npos) plain.resize(lt);
            for (size_t ii = 0; ii < inits.size(); ++ii)
                if (inits[ii].name == def.name || inits[ii].name == plain) delegating = static_cast<int>(ii);
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
        } else if (special == 1 && delegating >= 0) {
            // A delegating constructor `C() : C(args) {}` ([class.base.init]/6): the other constructor makes the whole
            // object; nothing else is initialised here.
            std::vector<Node> args = std::move(inits[static_cast<size_t>(delegating)].args);
            Node call;
            if (!special_call(cls, "<ctor>", node_name("this"), std::move(args), &call)) return false;
            pre.push_back(std::move(call));
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
                if (init == nullptr && field_is_aggr(field)) {
                    // `float m[16] = {...};` or `P p = {1, 2};`: element by element, then the rest is zero.
                    Node list;
                    const Node* item = nullptr;
                    if (field.init_at >= 0) {
                        const size_t saved_at = at;
                        at = static_cast<size_t>(field.init_at) - (field.init_aggr ? 0 : 1);
                        const bool ok = parse_init(&list);
                        at = saved_at;
                        if (!ok) return false;
                        if (list.kind == Node::Block && list.text == "init") list = elide_braces(field.type, list);
                        item = &list;
                    }
                    if (!lower_aggr(field.type, node_this_field(field.name), item, &pre)) return false;
                    continue;
                }
                if (init == nullptr && field.init_aggr && field.init_at >= 0 && field.type.is_array() &&
                    field.type.more.empty() && peel_array(field.type).base == 3 && peel_array(field.type).pointers == 0 &&
                    obj_needs_ctor(peel_array(field.type))) {
                    // `V uv[3] = {{0, 0}, {1, 1}};` for elements that have constructors: each element is made by the
                    // constructor that takes its braced items; an element without an item uses the default constructor.
                    Node list;
                    const size_t saved_at = at;
                    at = static_cast<size_t>(field.init_at);
                    const bool ok = parse_init(&list);
                    at = saved_at;
                    if (!ok) return false;
                    const Type elem = peel_array(field.type);
                    for (int i = 0; i < field.type.array_len; ++i) {
                        std::vector<Node> eargs;
                        if (list.kind == Node::Block && static_cast<size_t>(i) < list.kids.size()) {
                            Node& kid = list.kids[static_cast<size_t>(i)];
                            if (kid.kind == Node::Block && kid.text == "init") eargs = std::move(kid.kids);
                            else eargs.push_back(std::move(kid));
                        }
                        Node idx;
                        idx.kind = Node::IntLit;
                        idx.type.base = 1;
                        idx.number = static_cast<uint64_t>(i);
                        Node slot;
                        slot.kind = Node::Index;
                        slot.kids.push_back(node_this_field(field.name));
                        slot.kids.push_back(std::move(idx));
                        Node call;
                        if (!special_call(elem.struct_id, "<ctor>", node_addr(std::move(slot)), std::move(eargs), &call))
                            return false;
                        pre.push_back(std::move(call));
                    }
                    continue;
                }
                if (obj_needs_ctor(field.type)) {
                    std::vector<Node> args;
                    if (init != nullptr) args = std::move(init->args);
                    else if (field.init_at >= 0) {
                        // A default member initializer of a class member: `T x{a, b};` or `T x = e;`.
                        const size_t saved_at = at;
                        Type list_type_field;
                        if ((field.init_braced || field.init_aggr) && !field.type.is_array() &&
                            has_init_list_ctor(field.type.struct_id, &list_type_field)) {
                            // `std::vector<std::string> v = {"a", "b"};` / `T v{...}` with an initializer-list constructor: the
                            // braced list is one argument, made by init_list_temp ([over.match.list]).
                            at = static_cast<size_t>(field.init_at) - (field.init_aggr ? 0 : 1);
                            Node list_arg;
                            const size_t hoist_from_f = stmt_pre.size();
                            const bool list_ok = tokens[at].kind == TLBrace && tokens[at + 1].kind != TRBrace && init_list_temp(list_type_field, &list_arg);
                            if (list_ok) args.push_back(std::move(list_arg));
                            for (size_t h = hoist_from_f; h < stmt_pre.size(); ++h) pre.push_back(std::move(stmt_pre[h]));
                            if (stmt_pre.size() > hoist_from_f) stmt_pre.resize(hoist_from_f);
                            at = saved_at;
                            if (!list_ok && !error.empty()) return false;
                        }
                        if (args.empty()) {
                        at = static_cast<size_t>(field.init_at);
                        if (field.init_aggr) ++at;  // `T x = {a, b};`: past the '{'
                        bool ok = true;
                        do {
                            Node arg;
                            ok = parse_expr(1, &arg);
                            if (ok) args.push_back(std::move(arg));
                        } while (ok && (field.init_braced || field.init_aggr) && accept(TComma));
                        at = saved_at;
                        if (!ok) return false;
                        }
                    }
                    Node call;
                    {
                        // A member made from an object of its own class that has no copy constructor: the bytes are copied.
                        Type at_arg;
                        if (args.size() == 1 && !field.type.is_array() && static_type(args[0], &at_arg) &&
                            at_arg.base == 3 && at_arg.pointers == 0 && !at_arg.is_array() &&
                            at_arg.struct_id == field.type.struct_id && !class_has_copy(field.type.struct_id)) {
                            Node assign;
                            assign.kind = Node::Assign;
                            assign.op = TAssign;
                            assign.kids.push_back(node_this_field(field.name));
                            assign.kids.push_back(std::move(args[0]));
                            Node stmt;
                            stmt.kind = Node::ExprStmt;
                            stmt.kids.push_back(std::move(assign));
                            pre.push_back(std::move(stmt));
                            continue;
                        }
                    }
                    if (!special_call(field.type.struct_id, "<ctor>", node_addr(node_this_field(field.name)),
                                      std::move(args), &call))
                        return false;
                    pre.push_back(std::move(call));
                    if (eh_ && obj_needs_dtor(field.type)) {
                        eh_push_object(&pre, node_addr(node_this_field(field.name)), field.type.struct_id, 0, locals);
                        ++eh_held;
                    }
                } else if (init == nullptr && (field.init_at >= 0 || field.init_zero)) {
                    // The default member initializer of a member the constructor does not name.
                    Node value;
                    if (field.init_at >= 0) {
                        const size_t saved_at = at;
                        at = static_cast<size_t>(field.init_at);
                        const bool ok = parse_expr(1, &value);
                        at = saved_at;
                        if (!ok) return false;
                    } else {
                        value.kind = Node::IntLit;
                        value.number = 0;
                        value.type.base = 1;
                    }
                    Node assign;
                    assign.kind = Node::Assign;
                    assign.op = TAssign;
                    assign.kids.push_back(node_this_field(field.name));
                    assign.kids.push_back(std::move(value));
                    Node stmt;
                    stmt.kind = Node::ExprStmt;
                    stmt.kids.push_back(std::move(assign));
                    pre.push_back(std::move(stmt));
                } else if (init != nullptr && init->args.empty()) {
                    // `m()`: value-initialisation, zero for a scalar or an aggregate ([dcl.init]).
                    if (!lower_aggr(field.type, node_this_field(field.name), nullptr, &pre)) return false;
                } else if (init != nullptr) {
                    if (init->args.size() != 1) {
                        error = "member " + field.name + " takes one initializer";
                        return false;
                    }
                    Node assign;
                    assign.kind = Node::Assign;
                    assign.op = TAssign;
                    assign.kids.push_back(node_this_field(field.name));
                    assign.kids.push_back(field.ref ? node_addr(std::move(init->args[0])) : std::move(init->args[0]));
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
            // The wrapper block above is a scope of its own when the code is generated: the numbering of the scopes the
            // body opens must count it too, or a name placed in a loop of the body is looked up in the wrong scope.
            push_scope();
            const bool user_ok = parse_block(&user, locals);
            pop_scope();
            if (!user_ok) return false;
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
        ft.param_ref = fn->param_ref;
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

    // At the '<' of `(Name<...>`: the matching '>' is followed by '*' or '&' and then ')' or more of them, which makes the
    // parentheses a cast to a pointer or reference to the class template instance.
    bool template_cast_close_ahead(size_t lt) const {
        if (lt >= tokens.size() || tokens[lt].kind != TLt) return false;
        int depth = 0;
        size_t k = lt;
        for (; k < tokens.size() && tokens[k].kind != TEnd; ++k) {
            if (tokens[k].kind == TLt) ++depth;
            else if (tokens[k].kind == TGt && --depth == 0) break;
            else if (tokens[k].kind == TSemi || tokens[k].kind == TLBrace) return false;
        }
        if (k + 2 >= tokens.size() || tokens[k].kind != TGt) return false;
        size_t m = k + 1;
        if (tokens[m].kind != TStar && tokens[m].kind != TAmp) return false;
        while (m < tokens.size() && (tokens[m].kind == TStar || tokens[m].kind == TAmp)) ++m;
        return m < tokens.size() && tokens[m].kind == TRParen;
    }
    bool is_template_name_at(size_t i) const {
        if (!(i + 1 < tokens.size() && tokens[i].kind == TIdent && tokens[i + 1].kind == TLt &&
              class_templates.count(tokens[i].text) > 0))
            return false;
        // A variable or data member named like a class template (`duration < limit` with a member duration next to
        // std::chrono::duration) is the variable: `<` is a comparison.
        return !variable_named(tokens[i].text);
    }
    // A local variable, parameter or data member of the class being compiled has this name.
    bool variable_named(const std::string& name) const {
        if (local_visible(name)) return true;
        if (cur_class >= 0 && cur_class < static_cast<int>(structs.size()))
            for (const Field& f : structs[static_cast<size_t>(cur_class)].fields)
                if (f.name == name) return true;
        return false;
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
            // A default may name the parameters before it (`typename H = hash<K>`): they are bound while it is read.
            const std::vector<Binding> earlier = bind_params(td, *out);
            const bool ok = parse_one_arg(td.params[idx], &a);
            unbind_params(earlier);
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
            if (accept(TAmp)) a->type.is_ref = true;  // Box<const int&>: a reference type as an argument
            else if (accept(TAndAnd)) {
                a->type.is_ref = true;
                a->type.rref = true;
            }
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
            const TPat& tp = tpats[static_cast<size_t>(pat.tpat)];
            // Base<U>* from a D*: the argument may be a class derived from an instance of the template ([temp.deduct.call]/4.3).
            int walk = act.struct_id;
            while (walk >= 0 && structs[static_cast<size_t>(walk)].tmpl != tp.name) walk = structs[static_cast<size_t>(walk)].base;
            if (walk < 0) return false;
            const StructDef& sd = structs[static_cast<size_t>(walk)];
            if (sd.targs.size() != tp.args.size()) return false;
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
                // The parameter's type (int, unsigned, size_t, std::size_t, bool ...) is skipped; its name is the last
                // identifier before ',', '=' or '>'.
                size_t k = at;
                size_t last_ident = 0;
                bool seen_ident = false;
                while (k < tokens.size() && tokens[k].kind != TComma && tokens[k].kind != TGt && tokens[k].kind != TAssign &&
                       tokens[k].kind != TEnd) {
                    if (tokens[k].kind == TIdent) {
                        last_ident = k;
                        seen_ident = true;
                    }
                    ++k;
                }
                if (seen_ident && tokens[last_ident].text != "typename") {
                    while (at < last_ident) next();
                } else {
                    while (!is(TIdent) && !is(TComma) && !is(TGt) && !is(TEnd)) next();
                }
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
        int head_parens = 0;  // a `{}` inside the parameter list (`T x = {}`) is not the body
        while (k < tokens.size() && tokens[k].kind != TEnd) {
            if (depth == 0 && tokens[k].kind == TLParen) ++head_parens;
            else if (depth == 0 && tokens[k].kind == TRParen && head_parens > 0) --head_parens;
            if (head_parens > 0 && (tokens[k].kind == TLBrace || tokens[k].kind == TRBrace)) {
                ++k;
                continue;
            }
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
        paren = skip_decltype_parens(paren, td.end);
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
        const bool saved_rest = pat_rest_ok;
        pat_rest_ok = true;  // a parameter like `variant<Ts...>&` names the template's own pack
        const bool ok = parse_param_list(&pd);
        pat_rest_ok = saved_rest;
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
    // The first '(' from `from` that is not the one of a decltype(...) result type.
    size_t skip_decltype_parens(size_t from, size_t to) const {
        size_t k = from;
        while (k < to && tokens[k].kind != TLParen) ++k;
        if (k < to && k > from && tokens[k - 1].kind == TIdent && tokens[k - 1].text == "decltype") {
            int depth = 0;
            for (; k < to; ++k) {
                if (tokens[k].kind == TLParen) ++depth;
                else if (tokens[k].kind == TRParen && --depth == 0) {
                    ++k;
                    break;
                }
            }
            while (k < to && tokens[k].kind != TLParen) ++k;
        }
        return k;
    }
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
        int head_parens = 0;
        while (k < tokens.size() && tokens[k].kind != TEnd) {
            if (depth == 0 && tokens[k].kind == TLParen) ++head_parens;
            else if (depth == 0 && tokens[k].kind == TRParen && head_parens > 0) --head_parens;
            if (head_parens > 0 && (tokens[k].kind == TLBrace || tokens[k].kind == TRBrace)) {
                ++k;
                continue;
            }
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
        paren = skip_decltype_parens(paren, mt.td.end);
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
        if (building != nullptr && mname == mt.owner) building->ctor_tmpl = true;
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
        for (const Method& m : def.methods) {
            if (m.name != "<ctor>" || !m.tsym.empty() || m.params.size() != args.size()) continue;
            // The copy and move constructors take an object of this class: they do not stop a constructor template from
            // taking something else.
            if (args.size() == 1 && m.params[0].base == 3 && m.params[0].struct_id == sid && m.params[0].pointers <= 1)
                continue;
            if (call_score(m.params, args) < 0) continue;  // this one cannot take the arguments: the template may
            return true;
        }
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
            m.defaults = decl.param_defaults;
            if (is(TColon)) {
                m.body_at = at;
                m.has_body = true;
                skip_ctor_init_list();
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
        saw_auto_ret = false;
        ok = parse_type(&type, &imported, &exported);
        const bool tmpl_auto_ret = saw_auto_ret;
        saw_auto_ret = false;
        if (ok && mt.bodyless && (is(TStar) || is(TAmp) || is(TAndAnd))) {
            // `T* GameObject::GetComponent() const`: the pointer or reference marks come before the class name.
            size_t look = at;
            while (look < tokens.size() && (tokens[look].kind == TStar || tokens[look].kind == TAmp || tokens[look].kind == TAndAnd))
                ++look;
            if (look + 1 < tokens.size() && tokens[look].kind == TIdent) {
                while (is(TStar)) {
                    ++type.pointers;
                    next();
                }
                if (is(TAmp) || is(TAndAnd)) {
                    if (is(TAndAnd) && type.pointers == 0) type.rref = true;
                    type.is_ref = true;
                    next();
                }
            }
        }
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
            m.defaults = decl.param_defaults;
            m.is_static = mt.is_static;
            m.auto_ret = tmpl_auto_ret;
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
        // An empty pack that parse_template_args filled in after the written arguments was not written: it is deduced.
        for (size_t i = 0; i < args.size() && i < given.size(); ++i) given[i] = !(args[i].is_pack && args[i].pack.empty());
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
                            error = "cannot deduce template arguments of " + name + " (in " + current_fn + ")";
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
                error = "cannot deduce template arguments of " + name + " (in " + current_fn + ")";
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
        saw_auto_ret = false;
        bool ok = parse_type(&type, &imported, &exported);
        const bool inst_auto_ret = saw_auto_ret;
        saw_auto_ret = false;
        Declarator decl;
        if (ok) ok = parse_declarator(type, &decl);
        if (ok && !decl.is_function) {
            error = "expected a function template";
            ok = false;
        }
        if (ok && inst_auto_ret && is(TLBrace)) {
            FuncDecl pf;
            pf.name = key;
            pf.params = decl.params;
            pf.param_names = decl.param_names;
            pf.defaults = decl.param_defaults;
            pf.vararg = decl.vararg;
            pf.ret = decl.type;
            finish_signature(&pf);
            const size_t body_start = at;
            Type found;
            ok = probe_auto_ret(pf, -1, &found);
            at = body_start;
            decl.type = found;
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
            fn.defaults = decl.param_defaults;
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

    // A member function that is declared in its class and defined in another file (the engine's Sha512::Update): a call of
    // it needs the function's signature and an undefined symbol with its decorated name for the linker to find.
    void declare_external_methods() {
        for (size_t sid = 0; sid < structs.size(); ++sid) {
            for (size_t mi = 0; mi < structs[sid].methods.size(); ++mi) {
                const Method& m = structs[sid].methods[mi];
                if (m.has_body || m.pure || m.implicit || m.owner != static_cast<int>(sid) || m.qname.empty()) continue;
                bool known = false;
                for (const FuncDecl& f : funcs)
                    if (f.name == m.qname) known = true;
                if (known) continue;
                FuncDecl fn;
                fn.name = m.qname;
                fn.symbol = m.symbol;
                fn.ret = m.ret;
                Type self_ptr;
                self_ptr.base = 3;
                self_ptr.struct_id = static_cast<int>(sid);
                self_ptr.pointers = 1;
                if (!m.is_static) {
                    fn.params.push_back(self_ptr);
                    fn.param_names.push_back("this");
                }
                for (size_t i = 0; i < m.params.size(); ++i) {
                    fn.params.push_back(m.params[i]);
                    fn.param_names.push_back(std::string());
                }
                finish_signature(&fn);
                fn.defined = false;
                funcs.push_back(std::move(fn));
            }
        }
    }

    std::map<std::string, std::string> deleted_uses{};  // function name -> a use of a deleted function in it
    // Reports a use of a deleted function in a function that is reached: from every function with an external symbol, the
    // virtual tables and the addresses in global data, through every name found in a reached function.
    bool check_deleted_uses() {
        if (deleted_uses.empty()) return true;
        std::map<std::string, std::vector<size_t>> by_name;
        for (size_t i = 0; i < funcs.size(); ++i) {
            if (!funcs[i].name.empty()) by_name[funcs[i].name].push_back(i);
            if (!funcs[i].symbol.empty()) by_name[funcs[i].symbol].push_back(i);
        }
        std::vector<bool> reach(funcs.size(), false);
        std::vector<size_t> work;
        auto mark = [&](const std::string& name) {
            auto it = by_name.find(name);
            if (it == by_name.end()) return;
            for (size_t i : it->second)
                if (!reach[i]) {
                    reach[i] = true;
                    work.push_back(i);
                }
        };
        for (size_t i = 0; i < funcs.size(); ++i)
            if (funcs[i].defined && (!funcs[i].is_static || funcs[i].exported)) {
                reach[i] = true;
                work.push_back(i);
            }
        for (const Global& g : globals) {
            for (const std::string& slot : g.slots) mark(slot);
            for (const auto& reloc : g.addr_relocs) mark(reloc.second);
        }
        while (!work.empty()) {
            const size_t i = work.back();
            work.pop_back();
            std::vector<const Node*> stack;
            stack.push_back(&funcs[i].body);
            while (!stack.empty()) {
                const Node* n = stack.back();
                stack.pop_back();
                if (!n->text.empty()) mark(n->text);
                for (const Node& k : n->kids) stack.push_back(&k);
            }
        }
        for (size_t i = 0; i < funcs.size(); ++i) {
            if (!reach[i] || !funcs[i].defined) continue;
            auto bad = deleted_uses.find(funcs[i].name);
            if (bad != deleted_uses.end()) {
                error = bad->second + " (in " + funcs[i].name + ")";
                return false;
            }
        }
        return true;
    }

    bool finish_program() {
        if (!finish_program_core()) return false;
        return check_deleted_uses();
    }

    bool finish_program_core() {
        declare_external_methods();
        // What a header can make in every file that includes it has an internal symbol here (each file keeps its own copy,
        // so two files can both use vector<int> or the same inline function): template instances and virtual tables.
        for (FuncDecl& f : funcs)
            if (f.defined && f.symbol.find("?$") != std::string::npos) f.is_static = true;
        for (Global& g : globals)
            if (g.vtable || g.name.find("?$") != std::string::npos) g.is_static = true;
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
            if (g_crt_init_table) funcs.back().is_static = true;  // one per file: reached through the .CRT$XCU table
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
        if (init_used && !g_crt_init_table) {
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
        collapse_operators();
        lower_cxx_spellings();
        strip_namespace_qualifiers();
        for (const Token& t : tokens)
            if (t.kind == TIdent && (t.text == "try" || t.text == "throw")) eh_ = true;
        while (!is(TEnd)) {
            if (!ns_open.empty() && accept(TRBrace)) {
                for (size_t n = 0; n < ns_open.back(); ++n) ns_parts.pop_back();
                ns_open.pop_back();
                if (!ns_is_c.empty()) {
                    if (ns_is_c.back()) --c_linkage;
                    ns_is_c.pop_back();
                }
                if (!ns_is_anon.empty()) {
                    if (ns_is_anon.back()) --anon_depth;
                    ns_is_anon.pop_back();
                }
                update_ns();
                continue;
            }
            if (is(TIdent) && cur().text == "extern" && at + 1 < tokens.size() && tokens[at + 1].kind == TString) {
                // extern "C" { ... } and extern "C" declaration ([dcl.link]).
                const bool c_lang = tokens[at + 1].text == "C";
                next();
                next();
                if (is(TLBrace)) {
                    next();
                    ns_open.push_back(0);
                    ns_is_c.push_back(c_lang);
                    ns_is_anon.push_back(false);
                    if (c_lang) ++c_linkage;
                } else if (c_lang) {
                    ++c_linkage;
                    c_single = true;
                    pending_extern = true;  // `extern "C" int x;` is a declaration when it is a variable
                }
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
                ns_is_c.push_back(false);
                ns_is_anon.push_back(pushed == 0);
                if (pushed == 0) ++anon_depth;
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
            bool is_extern_decl = pending_extern;
            pending_extern = false;  // `extern int x;` and `extern "C" int x;`: the object is elsewhere
            const size_t decl_begin = at;
            while (is(TTypedef) || is(TStatic) || is(TConst) || (is(TIdent) && cur().text == "extern")) {
                if (accept(TTypedef)) is_typedef = true;
                else if (accept(TStatic)) is_static = true;
                else if (accept(TConst)) is_const = true;
                else {
                    next();
                    is_extern_decl = true;
                }
            }
            bool imported = false;
            bool exported = false;
            Type type;
            saw_auto_ret = false;
            if (!parse_type(&type, &imported, &exported)) return false;
            const bool top_auto_ret = saw_auto_ret;
            saw_auto_ret = false;
            while (!is_typedef && is(TStar) && at + 2 < tokens.size() && tokens[at + 1].kind == TIdent &&
                   tokens[at + 2].kind == TColonColon) {
                ++type.pointers;
                next();
            }
            if (!is_typedef && (is(TAmp) || is(TAndAnd)) && at + 3 < tokens.size() && tokens[at + 1].kind == TIdent &&
                tokens[at + 2].kind == TColonColon && class_name_at(at + 1)) {
                // `const T& C::f() { ... }`: a function returning a reference, defined outside its class.
                if (is(TAndAnd) && type.pointers == 0) type.rref = true;
                type.is_ref = true;
                next();
            }
            if (!is_typedef && type.base == 3 && type.pointers == 0 && !type.is_ref && type.struct_id >= 0 && is(TColonColon) &&
                at + 3 < tokens.size() &&
                ((tokens[at + 1].kind == TIdent && tokens[at + 2].kind == TLParen) ||
                 (tokens[at + 1].kind == TTilde && tokens[at + 2].kind == TIdent && tokens[at + 3].kind == TLParen))) {
                // C::C(args) : members { ... } and C::~C() { ... }: a constructor or destructor declared in the
                // class and defined here.
                const int sid = type.struct_id;
                next();
                const bool is_dtor = accept(TTilde);
                if (cur().text != structs[static_cast<size_t>(sid)].name &&
                    cur().text != (structs[static_cast<size_t>(sid)].tmpl.empty() ? std::string() : structs[static_cast<size_t>(sid)].tmpl)) {
                    error = "expected the class name after ::";
                    return false;
                }
                next();
                next();
                Declarator decl;
                if (!parse_param_list(&decl)) return false;
                StructDef& owner = structs[static_cast<size_t>(sid)];
                size_t mi = owner.methods.size();
                for (size_t i = 0; i < owner.methods.size(); ++i) {
                    const Method& cand = owner.methods[i];
                    if (cand.name != (is_dtor ? "<dtor>" : "<ctor>") || cand.owner != sid || cand.has_body ||
                        cand.params.size() != decl.params.size())
                        continue;
                    bool same = true;
                    for (size_t k = 0; k < cand.params.size(); ++k)
                        if (!same_shape(cand.params[k], decl.params[k])) same = false;
                    if (same) mi = i;
                }
                if (mi == owner.methods.size()) {
                    error = std::string("no matching ") + (is_dtor ? "destructor" : "constructor") + " declared in the class";
                    return false;
                }
                owner.methods[mi].has_body = true;
                owner.methods[mi].out_of_class = true;
                owner.methods[mi].body_at = at;
                owner.methods[mi].param_names = decl.param_names;
                if (is(TAssign) && at + 2 < tokens.size() && (tokens[at + 1].kind == TDefault || (tokens[at + 1].kind == TIdent && tokens[at + 1].text == "default")) &&
                    tokens[at + 2].kind == TSemi) {
                    // C::~C() = default; (or C::C() = default;) defined outside the class: the compiler's own special
                    // function, the same as when it is defaulted in the class.
                    at += 3;
                    owner.methods[mi].implicit = true;
                    const size_t resume_default = at;
                    if (!define_method(sid, mi)) return false;
                    at = resume_default;
                    continue;
                }
                skip_ctor_init_list();
                if (!is(TLBrace)) {
                    error = "expected a constructor body";
                    return false;
                }
                if (!skip_body()) return false;
                const size_t resume = at;
                if (!define_method(sid, mi)) return false;
                at = resume;
                continue;
            }
            if (!is_typedef && is(TIdent) && at + 3 < tokens.size() && tokens[at + 1].kind == TColonColon &&
                class_name_at(at)) {
                // int C::f(int x) { ... } for a function declared in class C.
                const int sid = find_struct(cur().text);
                next();
                next();
                // After `C::` the names of C's scope are visible: its typedefs and aliases mean what they mean in the class.
                struct ClassScopeTypedefs {
                    std::map<std::string, Type>& table;
                    std::vector<std::pair<std::string, std::pair<bool, Type>>> saved;
                    ~ClassScopeTypedefs() {
                        for (size_t k = saved.size(); k-- > 0;) {
                            if (saved[k].second.first) table[saved[k].first] = saved[k].second.second;
                            else table.erase(saved[k].first);
                        }
                    }
                } class_scope{typedefs, {}};
                for (const auto& td : structs[static_cast<size_t>(sid)].tdefs) {
                    auto had = typedefs.find(td.first);
                    class_scope.saved.push_back({td.first, {had != typedefs.end(), had != typedefs.end() ? had->second : Type{}}});
                    typedefs[td.first] = td.second;
                }
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
                    bool inline_def = false;  // `inline T C::x{...};` in a header: every file has its own copy
                    for (size_t k = decl_begin; k < at && k < tokens.size(); ++k)
                        if (tokens[k].inl) inline_def = true;
                    if (inline_def)
                        for (Global& g : globals)
                            if (g.name == sm->second.symbol) g.is_static = true;
                    if (is(TLBrace) && !sm->second.has_const && obj_needs_ctor(sm->second.type)) {
                        // inline const V V::Zero {0, 0}; the braces are the constructor's arguments.
                        next();
                        std::vector<Node> sargs;
                        if (!is(TRBrace)) {
                            do {
                                Node a;
                                if (!parse_expr(1, &a)) return false;
                                sargs.push_back(std::move(a));
                            } while (accept(TComma));
                        }
                        if (!expect(TRBrace, "'}'") || !expect(TSemi, "';'")) return false;
                        if (!register_global_object(sm->second.symbol, sm->second.type, std::move(sargs))) return false;
                        continue;
                    }
                    if (accept(TAssign) || is(TLBrace)) {
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
                const bool def_const = accept(TConst);
                const Method* found = nullptr;
                {
                    const std::string def_sig = param_sig(decl.params);
                    for (const Method& cand : structs[static_cast<size_t>(sid)].methods)
                        if (cand.name == mname && cand.owner == sid && !cand.has_body && cand.is_const == def_const &&
                            param_sig(cand.params) == def_sig)
                            found = &cand;
                    if (found == nullptr)
                        for (const Method& cand : structs[static_cast<size_t>(sid)].methods)
                            if (cand.name == mname && cand.owner == sid && !cand.has_body &&
                                cand.params.size() == decl.params.size())
                                found = &cand;
                    if (found == nullptr) found = find_method(sid, mname, decl.params.size());
                }
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
                owner.methods[mi].out_of_class = true;
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
                // `Ease e`, `Vec3 v`, `const Name& n`: a type name and a parameter name are a parameter list.
                const bool named_type_start =
                    after == TIdent && at + 3 < tokens.size() &&
                    (tokens[at + 3].kind == TIdent ||
                     ((tokens[at + 3].kind == TAmp || tokens[at + 3].kind == TStar) && at + 4 < tokens.size() &&
                      tokens[at + 4].kind == TIdent &&
                      (typedefs.count(tokens[at + 2].text) > 0 || find_struct(tokens[at + 2].text) >= 0)));
                const bool template_type_start = after == TIdent && is_template_name_at(at + 2);  // `Foo(vector<int>& n)`
                if (!type_start && !named_type_start && !template_type_start) {
                    // C g(args); a global object, the arguments go to a constructor.
                    decl.name = cur().text;
                    decl.type = type;
                    next();
                    next();
                    if (!parse_call_args(&gargs)) return false;
                    gctor_args = true;
                }
            }
            if (!gctor_args && !is_typedef && obj_needs_ctor(type) && type.pointers == 0 && is(TIdent) && at + 1 < tokens.size() &&
                tokens[at + 1].kind == TLBrace) {
                // C g{args}; a global object made by a constructor ([dcl.init.list]).
                decl.name = cur().text;
                decl.type = type;
                next();
                next();
                if (!accept(TRBrace)) {
                    do {
                        Node arg;
                        if (!parse_expr(1, &arg)) return false;
                        gargs.push_back(std::move(arg));
                    } while (accept(TComma));
                    if (!expect(TRBrace, "'}'")) return false;
                }
                gctor_args = true;
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
                if (accept(TAssign)) {
                    if (!decl.type.is_array()) {
                        // T g = expr;  at namespace scope: the constructor (copy or converting) runs at startup with the
                        // value as its argument ([dcl.init], copy-initialisation).
                        Node value;
                        if (!parse_expr(1, &value)) return false;
                        gargs.clear();
                        gargs.push_back(std::move(value));
                    } else if (!decl.type.more.empty() || !parse_init(&ginit)) {
                        error = error.empty() ? "a global object with a constructor takes arguments in parentheses" : error;
                        return false;
                    } else {
                        has_ginit = true;
                    }
                }
                if (!expect(TSemi, "';'")) return false;
                Global global;
                global.name = decl.name;
                global.type = decl.type;
                global.bss = true;
                global.is_static = is_static || anon_depth > 0;  // `static T x(args);` in a header: one copy per file
                globals.push_back(global);
                if (!register_global_object(decl.name, decl.type, std::move(gargs), has_ginit ? &ginit : nullptr))
                    return false;
                continue;
            }
            if (!decl.is_function) {
                if (c_single) {  // extern "C" int x;
                    --c_linkage;
                    c_single = false;
                }
                for (;;) {  // int a = 1, *b, c[2]; : every name gets the type read before the first one
                    Global global;
                    global.name = decl.name;
                    global.type = decl.type;
                    if (is_extern_decl && !is(TAssign)) {
                        bool known = false;
                        for (const Global& earlier : globals)
                            if (earlier.name == global.name) known = true;
                        if (!known) {
                            global.is_extern = true;
                            global.bss = false;
                            globals.push_back(global);
                        }
                        if (!accept(TComma)) break;
                        decl = Declarator{};
                        if (!parse_declarator(type, &decl)) return false;
                        decl.type.is_const = is_const;
                        continue;
                    }
                    // A const object at namespace scope and anything in an unnamed namespace has internal linkage
                    // ([basic.link]): every file keeps its own copy, so two files may use the same name.
                    global.is_static = is_static || decl.type.is_const || anon_depth > 0;
                    if (accept(TAssign) || (is(TLBrace) && decl.type.base == 3 && decl.type.pointers == 0)) {  // also `P g{1, 2};`
                        Node init;
                        if (!parse_init(&init)) return false;
                        global.bss = false;
                        if (!fix_unsized(&decl.type, init)) return false;
                        global.type = decl.type;
                        init_relocs = &global.addr_relocs;
                        const bool flat_ok = flatten_init(decl.type, init, &global.init_bytes, 0);
                        init_relocs = nullptr;
                        if (!flat_ok) return false;
                    }
                    {
                        // The same variable name in two namespaces (Sha256.hpp's kK and Sha512.cpp's kK): names of
                        // variables are not kept apart by namespace here, so the later one gets its own internal name
                        // and what is read after it means that one.
                        bool taken = false;
                        for (size_t gi = globals.size(); gi-- > 0;)
                            if (globals[gi].name == global.name && globals[gi].is_extern) {
                                globals.erase(globals.begin() + static_cast<std::ptrdiff_t>(gi));  // now defined here
                                break;
                            }
                        for (const Global& earlier : globals)
                            if (earlier.name == global.name) taken = true;
                        if (taken) {
                            global.name = decl.name + "@" + std::to_string(globals.size());
                            global_alias[decl.name] = global.name;
                        } else {
                            global_alias.erase(decl.name);
                        }
                    }
                    globals.push_back(global);
                    if (!accept(TComma)) break;
                    decl = Declarator{};
                    if (!parse_declarator(type, &decl)) return false;
                    decl.type.is_const = is_const;
                    if (decl.is_function || obj_needs_ctor(decl.type) || obj_needs_dtor(decl.type)) {
                        error = "only plain data can follow a comma in a global declaration";
                        return false;
                    }
                }
                if (!expect(TSemi, "';'")) return false;
                continue;
            }
            if (top_auto_ret && is(TLBrace)) {
                // auto f(...) { return x; }: the result type is the type of the first return.
                FuncDecl pf;
                pf.name = decl.name;
                pf.params = decl.params;
                pf.param_names = decl.param_names;
                pf.defaults = decl.param_defaults;
                pf.vararg = decl.vararg;
                pf.ret = decl.type;
                finish_signature(&pf);
                const size_t body_start = at;
                Type found;
                const bool probe_ok = probe_auto_ret(pf, -1, &found);
                at = body_start;
                if (!probe_ok) return false;
                decl.type = found;
            }
            FuncDecl fn;
            fn.name = current_ns.empty() ? decl.name : current_ns + "::" + decl.name;
            if (!current_ns.empty()) fn.symbol = cxx_symbol(ns_mangled, decl.name, decl.type, decl.params, &structs);
            if (c_linkage > 0) fn.symbol = decl.name;  // extern "C": the plain name
            {
                // Overloads ([over.load]): a second signature under one name gets its own internal name, and
                // every function of an overloaded name is decorated (?f@@YAHH@Z), as an operator always is.
                const std::string base_name = fn.name;
                OvFn entry;
                entry.sig = param_sig(decl.params) + (decl.vararg ? "Z" : "");
                entry.params = decl.params;
                entry.required = required_args(decl.param_defaults, decl.params.size());
                entry.vararg = decl.vararg;
                entry.symbol = c_linkage > 0 ? decl.name : cxx_symbol(ns_mangled, decl.name, decl.type, decl.params, &structs);
                std::vector<OvFn>& list = ovl[base_name];
                OvFn* hit = nullptr;
                for (OvFn& e : list)
                    if (e.sig == entry.sig) hit = &e;
                if (hit != nullptr) {
                    hit->required = std::min(hit->required, entry.required);
                    fn.name = hit->internal;
                } else {
                    entry.internal = list.empty() ? base_name : base_name + "#" + entry.sig;
                    fn.name = entry.internal;
                    list.push_back(entry);
                    hit = &list.back();
                }
                const bool is_op = decl.name.compare(0, 8, "operator") == 0;
                if ((list.size() > 1 || is_op) && !imported) {
                    fn.symbol = hit->symbol;
                    for (const OvFn& e : list)
                        for (FuncDecl& earlier : funcs)
                            if (earlier.name == e.internal && earlier.symbol.empty() && !earlier.imported)
                                earlier.symbol = e.symbol;
                }
            }
            fn.ret = decl.type;
            fn.params = decl.params;
            fn.param_names = decl.param_names;
            fn.defaults = decl.param_defaults;
            fn.vararg = decl.vararg;
            fn.imported = imported;
            fn.exported = exported;
            bool inline_fn = false;
            for (size_t k = decl_begin; k < at && k < tokens.size(); ++k)
                if (tokens[k].inl) inline_fn = true;
            fn.is_static = is_static || anon_depth > 0 || inline_fn;
            fn.comdat = inline_fn && !is_static && anon_depth == 0 && c_linkage == 0 && !fn.symbol.empty() && !decl.name.empty();
            finish_signature(&fn);
            if (c_single) {
                --c_linkage;
                c_single = false;
            }
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

bool Parser::flatten_init(const Type& type, const Node& node_given, std::vector<uint8_t>* bytes, int at) {
    auto ensure = [&](int n) {
        if (static_cast<int>(bytes->size()) < n) bytes->resize(static_cast<size_t>(n), 0);
    };
    const Node regular = node_given.kind == Node::Block && node_given.text == "init" && type.array_len != 0
                             ? elide_braces(type, node_given)
                             : node_given;
    const Node& node = regular;
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
    if (node.kind == Node::StringLit && type.array_len < 0 && type.pointers == 1 && type.base == 2 && init_relocs != nullptr) {
        // const char *p = "text": the text becomes a hidden global and the pointer an ADDR64 slot.
        Global text;
        text.name = "__gstr" + std::to_string(gstr_count++);
        text.is_static = true;
        text.bss = false;
        text.type.base = 2;
        text.type.array_len = static_cast<int>(node.text.size()) + 1;
        text.init_bytes.assign(node.text.begin(), node.text.end());
        text.init_bytes.push_back(0);
        globals.push_back(text);
        ensure(at + 8);
        init_relocs->push_back({static_cast<uint32_t>(at), globals.back().name});
        return true;
    }
    if (node.kind == Node::StringLit && type.array_len >= 0 && type.base == 2 && type.pointers == 0) {
        // char s[N] = "text"
        ensure(at + type.array_len);
        for (size_t i = 0; i < node.text.size() && static_cast<int>(i) < type.array_len; ++i)
            (*bytes)[static_cast<size_t>(at) + i] = static_cast<uint8_t>(node.text[i]);
        return true;
    }
    if (node.kind != Node::IntLit) {
        error = "global initializer must be a constant";
        return false;
    }
    const int width = std::max(1, size_of(type));
    ensure(at + width);
    uint64_t value = type.is_bool && type.pointers == 0 ? (node.number != 0 ? 1u : 0u) : node.number;
    if (!(type.is_bool && type.pointers == 0)) const_convert(node, type, &value);
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
    bool loop = is(TWhile) || is(TFor) || is(TDo);
    if (is(TFor) && at + 1 < tokens.size() && tokens[at + 1].kind == TLParen) {
        // A range for evaluates its range once, before the loop: what that needs can go before the statement.
        const size_t save = at;
        at += 2;
        if (range_for_ahead()) loop = false;
        at = save;
    }
    const bool ok = parse_stmt_core(out, locals);
    std::vector<Node> mine = std::move(stmt_pre);
    stmt_pre = std::move(outer);
    if (!ok || mine.empty()) return ok;
    Node* loop_node = out;  // a `for` with its own variables is a scope block around the loop
    if (loop && loop_node->kind == Node::Block)
        for (Node& k : loop_node->kids)
            if (k.kind == Node::While || k.kind == Node::For) loop_node = &k;
    if (loop && (loop_node->kind == Node::While || loop_node->kind == Node::For) && loop_node->kids.size() >= 2) {
        // The temporaries the condition needs are made again in every pass, before the condition is tested:
        //   while (c) body   is   while (true) { make; if (!c) break; body }
        //   for (i; c; s) body   is   for (i; ; s) { make; if (!c) break; body }
        // (their destructors still run once, after the loop).
        const size_t ci = loop_node->kind == Node::While ? 0 : 1;
        const size_t bi = loop_node->kind == Node::While ? 1 : 3;
        if (bi < loop_node->kids.size() && !(loop_node->kids[ci].kind == Node::Block && loop_node->kids[ci].text == "skip")) {
            Node cond = std::move(loop_node->kids[ci]);
            Node body = std::move(loop_node->kids[bi]);
            Node negated;
            negated.kind = Node::Unary;
            negated.op = TNot;
            negated.kids.push_back(std::move(cond));
            Node jump;
            jump.kind = Node::Break;
            Node test;
            test.kind = Node::If;
            test.kids.push_back(std::move(negated));
            test.kids.push_back(std::move(jump));
            Node pass;
            pass.kind = Node::Block;
            for (Node& n : mine) pass.kids.push_back(std::move(n));
            pass.kids.push_back(std::move(test));
            pass.kids.push_back(std::move(body));
            Node always;
            if (loop_node->kind == Node::While) {
                always.kind = Node::IntLit;
                always.number = 1;
                always.type.base = 1;
            } else {
                always.kind = Node::Block;
                always.text = "skip";
            }
            loop_node->kids[ci] = std::move(always);
            loop_node->kids[bi] = std::move(pass);
            return true;
        }
    }
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
            if (!temp_dtor(mine[i], &call)) return false;
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
        if (!temp_dtor(mine[i], &call)) return false;
        wrapped.kids.push_back(std::move(call));
    }
    *out = std::move(wrapped);
    return true;
}

bool Parser::parse_stmt_inner(Node* out, std::vector<Local>* locals, int stmt_line) {
    cur_locals = locals;
    if (is(TLBrace)) return parse_block(out, locals);
    if (is(TSemi)) {
        // An empty statement ([stmt.expr]).
        next();
        out->kind = Node::Block;
        return true;
    }
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
        Node cond_decl;
        bool cond_scoped = false;
        if (!parse_condition(&cond, &cond_decl, &cond_scoped, locals)) return false;
        Node then_s;
        if (!parse_stmt(&then_s, locals)) return false;
        out->kids.push_back(std::move(cond));
        out->kids.push_back(std::move(then_s));
        if (accept(TElse)) {
            Node else_s;
            if (!parse_stmt(&else_s, locals)) return false;
            out->kids.push_back(std::move(else_s));
        }
        close_condition_scope(out, std::move(cond_decl), cond_scoped);
        return true;
    }
    if (is(TWhile)) {
        next();
        out->kind = Node::While;
        if (!expect(TLParen, "'('")) return false;
        Node cond;
        Node cond_decl;
        bool cond_scoped = false;
        if (!parse_condition(&cond, &cond_decl, &cond_scoped, locals)) return false;
        Node body;
        // With a declaration in the condition the variable belongs to each pass: break and continue destroy it too.
        const size_t loop_base = cond_scoped ? dscopes.size() - 1 : dscopes.size();
        brk_dbase.push_back(loop_base);
        cont_dbase.push_back(loop_base);
        const bool body_ok = parse_stmt(&body, locals);
        brk_dbase.pop_back();
        cont_dbase.pop_back();
        if (!body_ok) return false;
        if (cond_scoped) {
            // while (T x = f()) body  is read as  while (true) { T x = f(); if (!x) break; body }  ([stmt.while]): the
            // variable is made again, tested and destroyed in every pass.
            std::vector<Node> dt;
            if (!dtor_nodes(dscopes.size() - 1, &dt)) return false;
            Node jump;
            jump.kind = Node::Break;
            Node leave;
            if (dt.empty()) {
                leave = std::move(jump);
            } else {
                leave.kind = Node::Block;
                for (Node& n : dt) leave.kids.push_back(std::move(n));
                leave.kids.push_back(std::move(jump));
            }
            Node negated;
            negated.kind = Node::Unary;
            negated.op = TNot;
            negated.kids.push_back(std::move(cond));
            Node test;
            test.kind = Node::If;
            test.kids.push_back(std::move(negated));
            test.kids.push_back(std::move(leave));
            Node pass;
            pass.kind = Node::Block;
            pass.kids.push_back(std::move(test));
            pass.kids.push_back(std::move(body));
            close_condition_scope(&pass, std::move(cond_decl), true);
            Node always;
            always.kind = Node::IntLit;
            always.number = 1;
            always.type.base = 1;
            out->kids.push_back(std::move(always));
            out->kids.push_back(std::move(pass));
            return true;
        }
        out->kids.push_back(std::move(cond));
        out->kids.push_back(std::move(body));
        return true;
    }
    if (is(TFor)) {
        next();
        out->kind = Node::For;
        if (!expect(TLParen, "'('")) return false;
        if (range_for_ahead()) return parse_range_for(out, locals, stmt_line);
        Node init;
        init.kind = Node::Block;
        init.text = "skip";
        // for (int i = 0; ...): the declaration lives in a scope around the whole loop ([stmt.for]).
        Node decl_init;
        bool decl_scope = false;
        if (is_decl_start() && !is(TStatic)) {
            decl_scope = true;
            push_scope();
            dscopes.emplace_back();
            if (!parse_stmt(&decl_init, locals)) return false;
        } else {
            if (!is(TSemi)) {
                if (!parse_expr(0, &init)) return false;
            }
            if (!expect(TSemi, "';'")) return false;
        }
        Node cond;
        cond.kind = Node::Block;
        cond.text = "skip";
        if (!is(TSemi)) {
            if (!parse_expr(0, &cond)) return false;
            if (!convert_class(&cond, bool_type())) return false;
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
        if (decl_scope) {
            Node loop = std::move(*out);
            *out = Node{};
            out->kind = Node::Block;
            out->text = "scope";
            out->kids.push_back(std::move(decl_init));
            out->kids.push_back(std::move(loop));
            std::vector<Node> dt;
            if (!dtor_nodes(dscopes.size() - 1, &dt)) return false;
            for (Node& n : dt) out->kids.push_back(std::move(n));
            dscopes.pop_back();
            pop_scope();
        }
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
        if (!convert_class(&cond, bool_type())) return false;
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
        Node ret;
        ret.kind = Node::Return;
        Node value;
        bool has_value = false;
        bool braced_ret = false;
        bool dt_ready = false;
        if (is(TLBrace) && !cur_ret_ref && cur_ret.base == 3 && cur_ret.pointers == 0 && !cur_ret.is_ref &&
            cur_ret.struct_id >= 0) {
            // return {a, b}; ([stmt.return], [dcl.init.list]): the object is made in a hidden local, by a
            // constructor taking the list, or member by member for an aggregate, and that local is returned.
            Local hidden;
            hidden.name = "__br" + std::to_string(rv_count++);
            hidden.type = cur_ret;
            place_local(locals, hidden);
            if (obj_needs_ctor(cur_ret)) {
                next();
                std::vector<Node> cargs;
                if (!is(TRBrace)) {
                    do {
                        Node a;
                        if (!parse_expr(1, &a)) return false;
                        cargs.push_back(std::move(a));
                    } while (accept(TComma));
                }
                if (!expect(TRBrace, "'}'")) return false;
                Node call;
                if (!special_call(cur_ret.struct_id, "<ctor>", node_addr(node_name(hidden.name)), std::move(cargs), &call))
                    return false;
                stmt_pre.push_back(std::move(call));
            } else {
                Node init;
                if (!parse_init(&init)) return false;
                Node assign;
                assign.kind = Node::Assign;
                assign.op = TAssign;
                assign.number = 1;
                assign.kids.push_back(node_name(hidden.name));
                assign.kids.push_back(std::move(init));
                Node stmt;
                stmt.kind = Node::ExprStmt;
                stmt.kids.push_back(std::move(assign));
                stmt_pre.push_back(std::move(stmt));
            }
            value = node_name(hidden.name);
            braced_ret = true;
        }
        if (braced_ret || !accept(TSemi)) {
            if (!braced_ret && !parse_expr(0, &value)) return false;
            if (lam_probe && !lam_probe_seen && !braced_ret) {
                Type seen;
                if (static_type(value, &seen)) {
                    seen.is_ref = false;
                    seen.pconst = false;
                    if (seen.is_array()) {
                        seen = peel_array(seen);
                        seen.pointers += 1;
                    }
                    lam_probe_type = seen;
                    lam_probe_seen = true;
                }
            }
            if (!braced_ret && cur_ret.base != 3 && cur_ret.base != 0 && !cur_ret_ref && !convert_class(&value, cur_ret))
                return false;
            if (!braced_ret && cur_ret.base == 3 && cur_ret.pointers == 0 && cur_ret.struct_id >= 0 && !cur_ret_ref &&
                cur_locals != nullptr && class_needs_ctor(structs[static_cast<size_t>(cur_ret.struct_id)])) {
                // return nullptr; / return "text"; / return 5; for a function returning a class: a converting constructor makes
                // the result ([stmt.return], copy-initialisation of the result object).
                Type vt;
                if (static_type(value, &vt) && !(vt.base == 3 && vt.pointers == 0 && !vt.is_array()) && !vt.is_array()) {
                    std::vector<Node> one;
                    one.push_back(std::move(value));
                    Type want = cur_ret;
                    want.is_ref = false;
                    if (!adjust_args({want}, &one)) return false;
                    value = std::move(one[0]);
                }
            }
            has_value = true;
            if (!expect(TSemi, "';'")) return false;
            dtor_skip.clear();
            if (value.kind == Node::Name && !cur_ret_ref && obj_needs_dtor(cur_ret)) {
                // return x; where x is a local object of the result class, or a temporary made for this
                // statement: the object itself becomes the result, so its destructor does not run here.
                if (value.text.compare(0, 4, "__bt") == 0 || value.text.compare(0, 4, "__br") == 0) {
                    for (size_t ti = 0; ti < stmt_temps.size(); ++ti)
                        if (stmt_temps[ti].name == value.text) {
                            stmt_temps.erase(stmt_temps.begin() + static_cast<std::ptrdiff_t>(ti));
                            break;
                        }
                } else {
                    for (const auto& scope : dscopes)
                        for (const DObj& d : scope)
                            if (d.kind == 0 && d.count == 0 && d.name == value.text && d.sid == cur_ret.struct_id)
                                dtor_skip = value.text;
                }
            }
            if (!dtor_nodes(fn_dbase, &dt)) return false;
            dtor_skip.clear();
            dt_ready = true;
            if (cur_ret_ref) value = node_addr(std::move(value));  // a reference result is the address
            // Hidden argument copies of the return expression go first.
            std::vector<Node> tdt;
            for (size_t i = stmt_temps.size(); i-- > 0;) {
                Node call;
                if (!temp_dtor(stmt_temps[i], &call)) return false;
                tdt.push_back(std::move(call));
            }
            stmt_temps.clear();
            dt.insert(dt.begin(), std::make_move_iterator(tdt.begin()), std::make_move_iterator(tdt.end()));
        } else if (cur_special == 1) {
            value = node_name("this");
            has_value = true;
        }
        if (!dt_ready && !dtor_nodes(fn_dbase, &dt)) return false;
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
        if (!is_typedef && is(TIdent) && cur().text == "auto") {
            // auto x = e; ([dcl.spec.auto]): the type of e, without a reference, a top-level const or an array.
            next();
            const size_t at_declarator = at;
            size_t k = at;
            while (k < tokens.size() && (tokens[k].kind == TAmp || tokens[k].kind == TStar || tokens[k].kind == TConst)) ++k;
            if (!(k + 1 < tokens.size() && tokens[k].kind == TIdent && tokens[k + 1].kind == TAssign)) {
                error = "auto needs a name and an initializer";
                return false;
            }
            at = k + 2;
            Node init;
            if (!parse_init(&init)) return false;
            const size_t after_init = at;
            Type t;
            if (!static_type(init, &t)) {
                error = "cannot work out the type of the auto variable " + tokens[k].text + " (in " + current_fn + ", init node " +
                        std::to_string(static_cast<int>(init.kind)) + " '" + init.text + "')";
                return false;
            }
            t.is_ref = false;
            t.pconst = false;
            if (t.is_array()) {
                t = peel_array(t);
                t.pointers += 1;
            }
            if (tokens[at_declarator].kind == TStar && t.pointers > 0) t.pointers -= 1;
            at = at_declarator;
            type = t;
            has_preset = true;
            preset_init = std::move(init);
            preset_end = after_init;
        } else if (!parse_type(&type, &imp, &exp)) {
            return false;
        }
        if (!is_typedef && is(TSemi)) {
            next();
            out->kind = Node::Block;
            return true;
        }
        Type declared_type = type;
        bool more = true;
        Node combined;
        combined.kind = Node::Block;
        size_t count = 0;
        while (more) {
            Node one;
            if (!parse_local_decl_one(declared_type, is_typedef, is_static, is_const, &one, locals, stmt_line, &more))
                return false;
            ++count;
            if (!more && count == 1) {
                *out = std::move(one);
                return true;
            }
            combined.kids.push_back(std::move(one));
        }
        *out = std::move(combined);
        return true;
    }
    out->kind = Node::ExprStmt;
    Node expr;
    if (!parse_expr(0, &expr)) return false;
    {
        // f(); where f returns a class in memory: the result goes into a hidden local that is destroyed after the statement.
        Type result;
        if (expr.kind == Node::Call && cur_locals != nullptr && static_type(expr, &result) && result.base == 3 &&
            result.pointers == 0 && !result.is_ref && !result.is_array() && result.struct_id >= 0 && !fits_register(result)) {
            Local hidden;
            hidden.name = "__bt" + std::to_string(tmp_count++);
            hidden.type = result;
            place_local(cur_locals, hidden);
            Node assign;
            assign.kind = Node::Assign;
            assign.op = TAssign;
            assign.number = 1;
            assign.kids.push_back(node_name(hidden.name));
            assign.kids.push_back(std::move(expr));
            expr = std::move(assign);
            if (class_needs_dtor(structs[static_cast<size_t>(result.struct_id)]))
                stmt_temps.push_back(Temp{hidden.name, result.struct_id});
        }
    }
    out->kids.push_back(std::move(expr));
    return expect(TSemi, "';'");
}

// One declarator of a local declaration `T a = 1, *b;` ([dcl.dcl]): type is the part before the first
// declarator. *more tells whether a ',' followed, so another declarator of the same type comes next.
bool Parser::parse_local_decl_one(const Type& type, bool is_typedef, bool is_static, bool is_const, Node* out,
                                  std::vector<Local>* locals, int stmt_line, bool* more) {
    Declarator decl;
    std::vector<Node> ctor_args;
    bool have_ctor_args = false;
    Node paren_init;
    bool have_paren_init = false;
    if (!is_typedef && obj_needs_ctor(type) && is(TIdent) && at + 1 < tokens.size() &&
        (tokens[at + 1].kind == TLParen || tokens[at + 1].kind == TLBrace)) {
        // C x(args); and C x{args}; the arguments go to a constructor.
        decl.name = cur().text;
        decl.type = type;
        next();
        const bool braced_args = is(TLBrace);
        Type list_type;
        if (braced_args && tokens[at + 1].kind != TRBrace && has_init_list_ctor(type.struct_id, &list_type)) {
            Node list;
            if (!init_list_temp(list_type, &list)) return false;
            ctor_args.push_back(std::move(list));
            have_ctor_args = true;
        } else {
        next();
        if (braced_args) {
            if (!is(TRBrace)) {
                do {
                    Node a;
                    if (!parse_expr(1, &a)) return false;
                    ctor_args.push_back(std::move(a));
                } while (accept(TComma));
            }
            if (!expect(TRBrace, "'}'")) return false;
        } else if (!parse_call_args(&ctor_args)) {
            return false;
        }
        have_ctor_args = true;
        }
    } else if (!is_typedef && !obj_needs_ctor(type) && !type.is_array() &&
               [&]() {
                   // `int x(5);` and `int* p(&v);` ([dcl.init]): direct-initialisation of a scalar or an aggregate, when
                   // what is in the parentheses is an expression and not a parameter list.
                   size_t k = at;
                   while (k < tokens.size() && tokens[k].kind == TStar) ++k;
                   if (!(k + 2 < tokens.size() && tokens[k].kind == TIdent && tokens[k + 1].kind == TLParen &&
                         tokens[k + 2].kind != TRParen))
                       return false;
                   const size_t save = at;
                   at = k + 2;
                   const bool is_type = is_decl_start();
                   at = save;
                   return !is_type;
               }()) {
        decl.type = type;
        while (accept(TStar)) ++decl.type.pointers;
        decl.name = cur().text;
        next();
        next();
        if (!parse_expr(1, &paren_init)) return false;
        if (!expect(TRParen, "')'")) return false;
        have_paren_init = true;
    } else if (!parse_declarator(type, &decl)) {
        return false;
    }
    if (is_typedef) {
        decl.type.is_const = is_const;
        typedefs[decl.name] = decl.type;
        return end_decl(more);
    }
    if (decl.is_function) {
        error = "a function cannot be nested";
        return false;
    }
    decl.type.is_const = is_const;
    Local local;
    local.name = decl.name;
    local.type = decl.type;
    local.is_const = is_const && decl.type.pointers == 0;  // `const T* p` is a pointer to const: p itself may change
    local.is_static = is_static;
    Node init;
    bool has_init = false;
    const bool preset = has_preset && (is(TAssign) || is(TColon));
    Type list_type_local;
    if (preset || have_paren_init || accept(TAssign) || (is(TLBrace) && !have_ctor_args)) {
        if (preset) {
            init = std::move(preset_init);
            at = preset_end;
            has_preset = false;
        } else if (have_paren_init) {
            init = std::move(paren_init);
        } else if (is(TLBrace) && !decl.type.is_array() && obj_needs_ctor(decl.type) &&
                   tokens[at + 1].kind != TRBrace && has_init_list_ctor(decl.type.struct_id, &list_type_local)) {
            // C x = {a, b}; for a class with an initializer-list constructor.
            Node list;
            if (!init_list_temp(list_type_local, &list)) return false;
            ctor_args.push_back(std::move(list));
            have_ctor_args = true;
            has_init = false;
            init = Node{};
            init.kind = Node::Block;
            init.text = "skipped";
        } else if (!decl.type.is_array() && obj_needs_ctor(decl.type) && is(TIdent) && at + 1 < tokens.size() &&
                   tokens[at + 1].kind == TLParen && !local_visible(cur().text) &&
                   structs[static_cast<size_t>(decl.type.struct_id)].name == cur().text &&
                   [&]() {
                       size_t k = at + 1;
                       int depth = 0;
                       for (; k < tokens.size() && tokens[k].kind != TEnd; ++k) {
                           if (tokens[k].kind == TLParen) ++depth;
                           else if (tokens[k].kind == TRParen && --depth == 0) break;
                       }
                       return k + 1 < tokens.size() && (tokens[k + 1].kind == TSemi || tokens[k + 1].kind == TComma);
                   }()) {
            // T x = T(args); is made in place by the constructor, with no temporary and no copy ([dcl.init]/17.6.1, the
            // C++17 rule: the initializer is a prvalue of the same class).
            next();
            next();
            if (!parse_call_args(&ctor_args)) return false;
            have_ctor_args = true;
            has_init = false;
            init = Node{};
            init.kind = Node::Block;
            init.text = "skipped";
        } else if (is(TLBrace) && !decl.type.is_array() && decl.type.base == 3 && decl.type.pointers == 0 && !decl.type.is_ref &&
                   decl.type.struct_id >= 0 && obj_needs_ctor(decl.type) && tokens[at + 1].kind != TRBrace &&
                   aggregate_with_defaults(decl.type.struct_id, std::vector<Node>(2))) {
            // `std::array<V, 4> a = {c, c, c, c};` for an aggregate with members that need construction: made as the
            // temporary `T{...}` (default construction, then each member set from its item) and then copied in.
            if (!braced_temp(decl.type.struct_id, &init)) return false;
        } else if (!parse_init(&init)) {
            return false;
        }
        if (init.kind == Node::Block && init.text == "init" && !decl.type.is_array() && decl.type.base == 3 &&
            decl.type.pointers == 0 && decl.type.struct_id >= 0 && !obj_needs_ctor(decl.type) && !is_static) {
            // An aggregate with a reference member (`struct { V& r; } x{v};`): the members are set one by one, a reference
            // member from the address of its item. (The statements run just before this declaration's statement.)
            const StructDef& agg = structs[static_cast<size_t>(decl.type.struct_id)];
            bool has_ref = false;
            for (const Field& f : agg.fields)
                if (f.ref) has_ref = true;
            if (has_ref) {
                size_t item = 0;
                for (const Field& f : agg.fields) {
                    if (f.name == "__vptr") continue;
                    if (item >= init.kids.size()) break;
                    Node target;
                    target.kind = Node::Field;
                    target.op = TDot;
                    target.text = f.name;
                    target.kids.push_back(node_name(decl.name));
                    Node assign;
                    assign.kind = Node::Assign;
                    assign.op = TAssign;
                    assign.number = f.ref || !obj_needs_ctor(f.type) ? 1 : 0;
                    assign.kids.push_back(std::move(target));
                    assign.kids.push_back(f.ref ? node_addr(std::move(init.kids[item])) : std::move(init.kids[item]));
                    Node st;
                    st.kind = Node::ExprStmt;
                    st.kids.push_back(std::move(assign));
                    stmt_pre.push_back(std::move(st));
                    ++item;
                }
                init = Node{};
                init.kind = Node::Block;
                init.text = "skipped";
            }
        }
        if (init.kind == Node::Block && init.text == "init" && !decl.type.is_array() && decl.type.base != 3 &&
            init.kids.size() <= 1) {
            // `int x{};` and `float f{1.5f};` ([dcl.init.list]/3): a scalar from a list of at most one element.
            if (init.kids.empty()) {
                Node zero;
                zero.kind = Node::IntLit;
                zero.number = 0;
                zero.type.base = 1;
                init = std::move(zero);
            } else {
                Node only = std::move(init.kids[0]);
                init = std::move(only);
            }
        }
        if (init.kind == Node::Block && init.text == "skipped") has_init = false; else
        has_init = true;
        if (!decl.type.is_array() && decl.type.base != 3 && decl.type.base != 0 && !convert_class(&init, decl.type))
            return false;
        if (init.kind == Node::Block && init.text == "init" && decl.type.array_len != 0) init = elide_braces(decl.type, init);
        if (!fix_unsized(&decl.type, init)) return false;
        local.type = decl.type;
        if (wants_copy(decl.type, init)) {
            // C x = y; with y an object of the class: the copy constructor.
            ctor_args.push_back(std::move(init));
            have_ctor_args = true;
            has_init = false;
        }        else if (!decl.type.is_array() && obj_needs_ctor(decl.type) && init.kind != Node::Block) {
            // C x = value; with a value that is not an object of a class (std::string s = "abc"; V v = 5;): a converting
            // constructor makes x from it ([dcl.init], copy-initialisation, with the copy elided).
            Type from;
            bool unrelated_class = false;
            if (static_type(init, &from) && from.base == 3 && from.pointers == 0 && !from.is_array() && from.struct_id >= 0) {
                unrelated_class = true;  // an object of another class (a lambda): a converting constructor takes it
                for (int walk = from.struct_id; walk >= 0; walk = structs[static_cast<size_t>(walk)].base)
                    if (walk == decl.type.struct_id) unrelated_class = false;
            }
            if (static_type(init, &from) && (unrelated_class || !(from.base == 3 && from.pointers == 0 && !from.is_array()))) {
                ctor_args.push_back(std::move(init));
                have_ctor_args = true;
                has_init = false;
            }
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
            init_relocs = &global.addr_relocs;  // a static array of string pointers: the addresses are relocations
            const bool flat_ok = flatten_init(decl.type, init, &global.init_bytes, 0);
            init_relocs = nullptr;
            if (!flat_ok) {
                if (error != "global initializer must be a constant") return false;
                const bool aggregate = decl.type.is_array() || init.kind == Node::Block;
                if (aggregate && decl.type.base == 3 && decl.type.pointers == 0 && obj_needs_ctor(decl.type)) return false;
                // `static int n = f();`, `static const E tab[] = {{"a", K(1)}}`: not constant, so it is computed the first
                // time control reaches the declaration (a guard flag in a hidden global, [stmt.dcl]/4; no lock).
                error.clear();
                global.init_bytes.clear();
                global.addr_relocs.clear();
                global.bss = true;
                Global guard;
                guard.name = global.name + "_guard";
                guard.type.base = 1;
                guard.bss = true;
                guard.is_static = true;
                globals.push_back(guard);
                local.symbol = global.name;
                globals.push_back(global);
                place_local(locals, local);
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
                if (aggregate) {
                    std::vector<Node> element_stmts;
                    if (!lower_aggr(decl.type, node_name(decl.name), &init, &element_stmts)) return false;
                    for (Node& e : element_stmts) {
                        e.line = stmt_line;
                        then_block.kids.push_back(std::move(e));
                    }
                } else {
                    Node store;
                    store.kind = Node::Assign;
                    store.number = 1;
                    store.kids.push_back(node_name(decl.name));
                    store.kids.push_back(std::move(init));
                    Node store_stmt;
                    store_stmt.kind = Node::ExprStmt;
                    store_stmt.line = stmt_line;
                    store_stmt.kids.push_back(std::move(store));
                    then_block.kids.push_back(std::move(store_stmt));
                }
                Node not_g;
                not_g.kind = Node::Unary;
                not_g.op = TNot;
                not_g.kids.push_back(node_name(guard.name));
                Node test;
                test.kind = Node::If;
                test.kids.push_back(std::move(not_g));
                test.kids.push_back(std::move(then_block));
                out->kind = Node::Block;
                out->kids.push_back(std::move(test));
                if (obj_needs_dtor(decl.type)) {
                    GlobalObj g;
                    g.name = local.symbol;
                    g.sid = decl.type.struct_id;
                    g.guard = guard.name;
                    static_objs.push_back(std::move(g));
                }
                return end_decl(more);
            }
            has_init = false;
        }
        local.symbol = global.name;
        globals.push_back(global);
    }
    if (is_const && !is_static && has_init && init.kind == Node::IntLit && init.type.base < 5 && decl.type.pointers == 0 &&
        !decl.type.is_array() && !decl.type.is_ref && (decl.type.base == 1 || decl.type.base == 2 || decl.type.base == 4)) {
        local.has_cval = true;
        local.cval = init.number;
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
            if (has_init || decl.type.is_array()) {
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
                if (!special_call(sid, "<ctor>", node_addr(node_name(local.symbol)), std::move(ctor_args), &call))
                    return false;
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
            return end_decl(more);
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
            return end_decl(more);
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
    return end_decl(more);
}



// The condition of `if` and `while`, after the '(' and up to and including the ')'. A declaration with an initializer
// (`if (Finger* f = Find(id))`, [stmt.select]) is made in a scope around the statement and its name is the condition.
bool Parser::parse_condition(Node* cond, Node* decl, bool* scoped, std::vector<Local>* locals) {
    *scoped = false;
    if (is_decl_start() && !is(TStatic) && !is(TTypedef)) {
        size_t rp = at;
        int depth = 0;
        size_t assign = 0;
        for (; rp < tokens.size() && tokens[rp].kind != TEnd; ++rp) {
            const Tok k = tokens[rp].kind;
            if (k == TLParen || k == TLBrack || k == TLBrace) ++depth;
            else if (k == TRParen || k == TRBrack || k == TRBrace) {
                if (depth == 0) break;
                --depth;
            } else if (depth == 0 && k == TAssign && assign == 0) assign = rp;
        }
        if (assign > at && tokens[assign - 1].kind == TIdent && rp < tokens.size() && tokens[rp].kind == TRParen) {
            const std::string name = tokens[assign - 1].text;
            *scoped = true;
            push_scope();
            dscopes.emplace_back();
            tokens[rp].kind = TSemi;  // the declaration ends at the ')'
            const bool ok = parse_stmt(decl, locals);
            tokens[rp].kind = TRParen;
            if (!ok) return false;
            *cond = node_name(name);
            return convert_class(cond, bool_type());
        }
    }
    if (!parse_expr(0, cond)) return false;
    if (!convert_class(cond, bool_type())) return false;
    return expect(TRParen, "')'");
}

void Parser::close_condition_scope(Node* stmt, Node decl, bool scoped) {
    if (!scoped) return;
    Node inner = std::move(*stmt);
    *stmt = Node{};
    stmt->kind = Node::Block;
    stmt->text = "scope";
    stmt->kids.push_back(std::move(decl));
    stmt->kids.push_back(std::move(inner));
    std::vector<Node> dt;
    if (dtor_nodes(dscopes.size() - 1, &dt))
        for (Node& n : dt) stmt->kids.push_back(std::move(n));
    dscopes.pop_back();
    pop_scope();
}

// for (T x : array) body ([stmt.ranged]) over an array whose length is known: an index runs over it and the
// variable is declared, in each pass, from the element (a reference for T&).
bool Parser::parse_range_for(Node* out, std::vector<Local>* locals, int stmt_line) {
    const size_t decl_start = at;
    size_t colon = at;
    int depth = 0;
    for (; colon < tokens.size() && tokens[colon].kind != TEnd; ++colon) {
        const Tok k = tokens[colon].kind;
        if (k == TLParen || k == TLBrack || k == TLBrace) ++depth;
        else if (k == TRParen || k == TRBrack || k == TRBrace) --depth;
        else if (depth == 0 && k == TColon) break;
    }
    at = colon + 1;
    Node range;
    Node list_init;
    bool braced_range = false;
    Type range_type;
    if (is(TLBrace)) {
        // for (T x : {a, b, c}): the list is stored in a hidden array that the loop runs over ([stmt.ranged]).
        if (!parse_init(&list_init)) return false;
        if (!expect(TRParen, "')'")) return false;
        if (list_init.kids.empty()) {
            error = "a range for over an empty list is not supported";
            return false;
        }
        const size_t after_list = at;
        at = decl_start;
        while (accept(TConst)) {}
        Type elem_type;
        if (is(TIdent) && cur().text == "auto") {
            if (!static_type(list_init.kids[0], &elem_type)) {
                error = "cannot work out the type of the range elements";
                return false;
            }
            elem_type.is_ref = false;
            if (elem_type.is_array()) {
                elem_type = peel_array(elem_type);
                elem_type.pointers += 1;
            }
        } else {
            bool imp = false;
            bool exp = false;
            if (!parse_type(&elem_type, &imp, &exp)) return false;
            while (accept(TStar)) ++elem_type.pointers;
        }
        elem_type.is_ref = false;
        at = after_list;
        Local array_local;
        array_local.name = "__rl" + std::to_string(range_count++);
        array_local.type = elem_type;
        array_local.type.array_len = static_cast<int>(list_init.kids.size());
        place_local(locals, array_local);
        range = node_name(array_local.name);
        range_type = array_local.type;
        braced_range = true;
    } else if (!parse_expr(0, &range)) {
        return false;
    } else if (!expect(TRParen, "')'")) {
        return false;
    }
    const size_t after_range = at;
    if (!braced_range && !static_type(range, &range_type)) {
        error = "a range for needs an array of known length";
        return false;
    }
    if (!braced_range && range.kind == Node::Call && range_type.base == 3 && range_type.pointers == 0 &&
        !range_type.is_array() && range_type.struct_id >= 0 && cur_locals != nullptr) {
        // for (x : f()): the object the call returns goes into a hidden local before the loop and is destroyed after it.
        Local hidden;
        hidden.name = "__bt" + std::to_string(tmp_count++);
        hidden.type = range_type;
        place_local(cur_locals, hidden);
        Node assign;
        assign.kind = Node::Assign;
        assign.op = TAssign;
        assign.number = 1;
        assign.kids.push_back(node_name(hidden.name));
        assign.kids.push_back(std::move(range));
        Node stmt;
        stmt.kind = Node::ExprStmt;
        stmt.kids.push_back(std::move(assign));
        stmt_pre.push_back(std::move(stmt));
        if (class_needs_dtor(structs[static_cast<size_t>(range_type.struct_id)]))
            stmt_temps.push_back(Temp{hidden.name, range_type.struct_id});
        range = node_name(hidden.name);
    }
    // A class with begin() and end() that return pointers: the pointer runs from one to the other ([stmt.ranged]).
    bool class_range = false;
    bool iter_class = false;
    Type iter_deref;
    Type iter_type;
    Node begin_call, end_call;
    if (range_type.base == 3 && range_type.pointers == 0 && !range_type.is_array() && range_type.struct_id >= 0 &&
        (range.kind == Node::Name || range.kind == Node::Field || range.kind == Node::Index ||
         (range.kind == Node::Unary && range.op == TStar))) {
        const Method* mb = find_method(range_type.struct_id, "begin", 0);
        const Method* me = find_method(range_type.struct_id, "end", 0);
        if (mb != nullptr && me != nullptr && mb->ret.base == 3 && mb->ret.pointers == 0 && !mb->ret.is_ref &&
            me->ret.base == 3 && me->ret.pointers == 0 && me->ret.struct_id == mb->ret.struct_id) {
            // begin() and end() return an iterator class: the loop uses its operator!=, prefix operator++ and operator*
            // ([stmt.ranged]). The iterator objects are copied from the calls; their destructors are not run.
            const int isid = mb->ret.struct_id;
            const Method* m_ne = find_method(isid, "operator!=", 1);
            const Method* m_inc = find_method(isid, "operator++", 0);
            const Method* m_deref = find_method(isid, "operator*", 0);
            if (m_ne == nullptr || m_inc == nullptr || m_deref == nullptr) {
                error = "a range for over a class needs an iterator with !=, ++ and *";
                return false;
            }
            iter_class = true;
            iter_deref = m_deref->ret;
            iter_deref.is_ref = false;
            iter_deref.rref = false;
            class_range = true;
            iter_type = mb->ret;
            for (int w = 0; w < 2; ++w) {
                const Method* m = w == 0 ? mb : me;
                Node call;
                call.kind = Node::Call;
                call.text = m->qname;
                call.number = 1;
                call.type = m->ret;
                call.kids.push_back(node_addr(range));
                (w == 0 ? begin_call : end_call) = std::move(call);
            }
        } else if (mb == nullptr || me == nullptr || mb->ret.pointers < 1 || me->ret.pointers != mb->ret.pointers) {
            error = "a range for over a class needs begin() and end() that return pointers";
            return false;
        }
        if (!iter_class) {
        class_range = true;
        iter_type = mb->ret;
        for (int w = 0; w < 2; ++w) {
            const Method* m = w == 0 ? mb : me;
            Node call;
            call.kind = Node::Call;
            call.text = m->qname;
            call.number = 1;
            call.type = m->ret;
            call.kids.push_back(node_addr(range));
            (w == 0 ? begin_call : end_call) = std::move(call);
        }
        }
    } else if (!range_type.is_array() || range_type.array_len < 0) {
        error = "a range for needs an array of known length";
        return false;
    }
    const int count = class_range ? 0 : range_type.array_len;
    Type elem = class_range ? iter_type : peel_array(range_type);
    if (iter_class) elem = iter_deref;
    else if (class_range) elem.pointers -= 1;
    elem.is_const = false;
    at = decl_start;
    bool is_const = false;
    while (accept(TConst)) is_const = true;
    Type declared;
    if (is(TIdent) && cur().text == "auto") {
        next();
        declared = elem;
        if (is(TStar) && declared.pointers > 0) declared.pointers -= 1;
    } else {
        bool imp = false;
        bool exp = false;
        if (!parse_type(&declared, &imp, &exp)) return false;
    }
    const int saved_depth = scope_depth;
    push_scope();
    // The iterator objects of a range for over a class (with a destructor) are registered in a scope of their own around the
    // loop's scope: a `return` in the body destroys them ([stmt.return]), a `break` or `continue` of this loop does not (the code
    // after the loop does), a `break` of an enclosing loop does.
    const bool iter_needs_dtor = iter_class && class_needs_dtor(structs[static_cast<size_t>(iter_type.struct_id)]);
    if (iter_needs_dtor) dscopes.emplace_back();
    dscopes.emplace_back();
    Local index;
    index.name = "__ri" + std::to_string(range_count++);
    index.type.base = 1;
    Local range_end;
    range_end.name = "__re" + std::to_string(range_count++);
    if (class_range) {
        index.type = iter_type;
        range_end.type = iter_type;
    }
    place_local(locals, index);
    if (class_range) place_local(locals, range_end);
    if (iter_needs_dtor) {
        DObj dend;
        dend.name = range_end.name;
        dend.sid = iter_type.struct_id;
        DObj dindex;
        dindex.name = index.name;
        dindex.sid = iter_type.struct_id;
        dscopes[dscopes.size() - 2].push_back(dend);
        dscopes[dscopes.size() - 2].push_back(dindex);
    }
    Node element;
    if (iter_class) {
        bool done = false;
        element = node_name(index.name);
        if (!operator_unary(TStar, false, &element, &done) || !done) {
            if (error.empty()) error = "the iterator of a range for has no operator*";
            return false;
        }
    } else if (class_range) {
        element.kind = Node::Unary;
        element.op = TStar;
        element.kids.push_back(node_name(index.name));
    } else {
        element.kind = Node::Index;
        element.kids.push_back(range);
        element.kids.push_back(node_name(index.name));
    }
    has_preset = true;
    preset_init = std::move(element);
    preset_end = after_range;
    range_decl = true;
    Node decl_node;
    bool more = false;
    const bool decl_ok = parse_local_decl_one(declared, false, false, is_const, &decl_node, locals, stmt_line, &more);
    range_decl = false;
    has_preset = false;
    if (!decl_ok) return false;
    at = after_range;
    Node body;
    brk_dbase.push_back(dscopes.size());
    cont_dbase.push_back(dscopes.size());
    const bool body_ok = parse_stmt(&body, locals);
    brk_dbase.pop_back();
    cont_dbase.pop_back();
    if (!body_ok) return false;
    Node init;
    init.kind = Node::Assign;
    init.op = TAssign;
    init.number = 1;
    init.kids.push_back(node_name(index.name));
    if (class_range) {
        init.kids.push_back(std::move(begin_call));
    } else {
        Node zero;
        zero.kind = Node::IntLit;
        zero.type.base = 1;
        init.kids.push_back(std::move(zero));
    }
    Node cond;
    if (iter_class) {
        Node left = node_name(index.name);
        Node right = node_name(range_end.name);
        bool done = false;
        if (!operator_binary(TNe, &left, &right, &done) || !done) {
            if (error.empty()) error = "the iterator of a range for has no operator!=";
            return false;
        }
        cond = std::move(left);
    } else {
    cond.kind = Node::Binary;
    cond.op = class_range ? TNe : TLt;
    cond.kids.push_back(node_name(index.name));
    if (class_range) {
        cond.kids.push_back(node_name(range_end.name));
    } else {
        Node limit;
        limit.kind = Node::IntLit;
        limit.number = static_cast<uint64_t>(count);
        limit.type.base = 1;
        cond.kids.push_back(std::move(limit));
    }
    }
    Node step;
    if (iter_class) {
        bool done = false;
        step = node_name(index.name);
        if (!operator_unary(TPlusPlus, false, &step, &done) || !done) {
            if (error.empty()) error = "the iterator of a range for has no prefix operator++";
            return false;
        }
    } else {
    step.kind = Node::Unary;
    step.op = TPlusPlus;
    step.number = 0;
    step.kids.push_back(node_name(index.name));
    }
    Node iteration;
    iteration.kind = Node::Block;
    iteration.kids.push_back(std::move(decl_node));
    iteration.kids.push_back(std::move(body));
    std::vector<Node> dt;
    if (!dtor_nodes(dscopes.size() - 1, &dt)) return false;
    for (Node& n : dt) iteration.kids.push_back(std::move(n));
    dscopes.pop_back();
    if (iter_needs_dtor) dscopes.pop_back();
    pop_scope();
    scope_depth = saved_depth;
    Node loop;
    loop.kind = Node::For;
    loop.kids.push_back(std::move(init));
    loop.kids.push_back(std::move(cond));
    loop.kids.push_back(std::move(step));
    loop.kids.push_back(std::move(iteration));
    // The loop's variables live in a scope around it, as for `for (int i = ...)`.
    *out = Node{};
    out->kind = Node::Block;
    out->text = "scope";
    if (braced_range) {
        Node store;
        store.kind = Node::Assign;
        store.number = 1;
        store.kids.push_back(range);
        store.kids.push_back(std::move(list_init));
        Node store_stmt;
        store_stmt.kind = Node::ExprStmt;
        store_stmt.kids.push_back(std::move(store));
        out->kids.push_back(std::move(store_stmt));
    }
    if (class_range) {
        // The end pointer is taken once, before the loop.
        Node assign;
        assign.kind = Node::Assign;
        assign.op = TAssign;
        assign.number = 1;
        assign.kids.push_back(node_name(range_end.name));
        assign.kids.push_back(std::move(end_call));
        Node stmt;
        stmt.kind = Node::ExprStmt;
        stmt.kids.push_back(std::move(assign));
        out->kids.push_back(std::move(stmt));
    }
    out->kids.push_back(std::move(loop));
    if (iter_class && class_needs_dtor(structs[static_cast<size_t>(iter_type.struct_id)])) {
        // The two iterator objects are destroyed when the loop ends; a `return` or a `break` of an enclosing loop inside the body
        // destroys them too (they are registered in their own scope).
        for (const std::string& nm : {range_end.name, index.name}) {
            Node dcall;
            if (!special_call(iter_type.struct_id, "<dtor>", node_addr(node_name(nm)), {}, &dcall)) return false;
            out->kids.push_back(std::move(dcall));
        }
    }
    return true;
}

// `{a, b, c}` for a parameter of type std::initializer_list<T> ([dcl.init.list]): the elements go into a hidden array of
// T in this function's frame and the list is a hidden object that points at the array. The current token is the '{'.
bool Parser::init_list_temp(const Type& list_type, Node* out) {
    const StructDef& lsd = structs[static_cast<size_t>(list_type.struct_id)];
    if (lsd.targs.empty() || lsd.targs[0].is_value || cur_locals == nullptr) {
        error = "cannot make an initializer_list here";
        return false;
    }
    const Type elem = lsd.targs[0].type;
    const bool elem_obj = elem.base == 3 && elem.pointers == 0 && elem.struct_id >= 0 &&
                          class_needs_dtor(structs[static_cast<size_t>(elem.struct_id)]);
    next();  // '{'
    std::vector<Node> items;
    while (!is(TRBrace) && !is(TEnd)) {
        Node item;
        if (is(TLBrace) && elem.base == 3 && elem.pointers == 0 && elem.struct_id >= 0) {
            if (!braced_temp(elem.struct_id, &item)) return false;
        } else {
            if (!parse_expr(1, &item)) return false;
            if (elem.base != 3 && elem.base != 0 && elem.pointers == 0 && !convert_class(&item, elem)) return false;
        }
        items.push_back(std::move(item));
        if (!accept(TComma)) break;
    }
    if (!expect(TRBrace, "'}'")) return false;
    Local array;
    array.name = "__il" + std::to_string(tmp_count++);
    array.type = elem;
    array.type.array_len = items.empty() ? 1 : static_cast<int>(items.size());
    place_local(cur_locals, array);
    if (elem_obj && !items.empty()) stmt_temps.push_back(Temp{array.name, elem.struct_id, static_cast<int>(items.size())});
    for (size_t i = 0; i < items.size(); ++i) {
        Node idx;
        idx.kind = Node::IntLit;
        idx.type.base = 1;
        idx.number = i;
        Node slot;
        slot.kind = Node::Index;
        slot.kids.push_back(node_name(array.name));
        slot.kids.push_back(std::move(idx));
        if (elem_obj) {
            // The element is made in place by a constructor (copy or converting) and destroyed after the statement.
            std::vector<Node> eargs;
            eargs.push_back(std::move(items[i]));
            Node ector;
            if (!special_call(elem.struct_id, "<ctor>", node_addr(std::move(slot)), std::move(eargs), &ector)) return false;
            stmt_pre.push_back(std::move(ector));
            continue;
        }
        Node assign;
        assign.kind = Node::Assign;
        assign.op = TAssign;
        assign.number = 1;
        assign.kids.push_back(std::move(slot));
        assign.kids.push_back(std::move(items[i]));
        Node stmt;
        stmt.kind = Node::ExprStmt;
        stmt.kids.push_back(std::move(assign));
        stmt_pre.push_back(std::move(stmt));
    }
    Node zero;
    zero.kind = Node::IntLit;
    zero.type.base = 1;
    Node first;
    first.kind = Node::Index;
    first.kids.push_back(node_name(array.name));
    first.kids.push_back(std::move(zero));
    Node count;
    count.kind = Node::IntLit;
    count.type.base = 1;
    count.type.wide = true;
    count.number = items.size();
    Local hidden;
    hidden.name = "__ilo" + std::to_string(tmp_count++);
    hidden.type = list_type;
    hidden.type.is_ref = false;
    place_local(cur_locals, hidden);
    std::vector<Node> cargs;
    cargs.push_back(node_addr(std::move(first)));
    cargs.push_back(std::move(count));
    Node ctor_stmt;
    if (!special_call(list_type.struct_id, "<ctor>", node_addr(node_name(hidden.name)), std::move(cargs), &ctor_stmt))
        return false;
    stmt_pre.push_back(std::move(ctor_stmt));
    *out = node_name(hidden.name);
    return true;
}

// C{args} ([expr.type.conv], [dcl.init.list]): a temporary object of class bsid made from the braced list at the
// current token, by a constructor, or member by member for an aggregate. Its construction goes before the
// statement and its destructor runs after it.
bool Parser::braced_temp(int bsid, Node* out) {
        Type bt;
        bt.base = 3;
        bt.struct_id = bsid;
        Local hidden;
        hidden.name = "__bt" + std::to_string(tmp_count++);
        hidden.type = bt;
        place_local(cur_locals, hidden);
        Type list_type;
        if (obj_needs_ctor(bt) && tokens[at + 1].kind != TRBrace && has_init_list_ctor(bsid, &list_type)) {
            // An initializer-list constructor is preferred when the list is not empty ([over.match.list]).
            Node list;
            if (!init_list_temp(list_type, &list)) return false;
            std::vector<Node> bargs;
            bargs.push_back(std::move(list));
            Node ctor_stmt;
            if (!special_call(bsid, "<ctor>", node_addr(node_name(hidden.name)), std::move(bargs), &ctor_stmt))
                return false;
            stmt_pre.push_back(std::move(ctor_stmt));
        } else if (obj_needs_ctor(bt) && tokens[at + 1].kind != TRBrace && has_nested_braces_ahead() &&
                   aggregate_with_defaults(bsid, std::vector<Node>(2))) {
            // C{a, {b, c}} for an aggregate with members that need construction: the default constructor, then each
            // member set from its item (a nested list makes the member's own temporary).
            next();  // '{'
            const StructDef& agg = structs[static_cast<size_t>(bsid)];
            const Method* dflt = find_method(bsid, "<ctor>", 0);
            if (dflt != nullptr) {
                Node call;
                call.kind = Node::Call;
                call.text = dflt->qname;
                call.number = 1;
                call.type = dflt->ret;
                call.kids.push_back(node_addr(node_name(hidden.name)));
                Node st;
                st.kind = Node::ExprStmt;
                st.kids.push_back(std::move(call));
                stmt_pre.push_back(std::move(st));
            }
            size_t k = 0;
            for (const Field& f : agg.fields) {
                if (f.name == "__vptr") continue;
                if (is(TRBrace)) break;
                Node item;
                if (is(TLBrace) && f.type.base == 3 && f.type.pointers == 0 && !f.type.is_array() && f.type.struct_id >= 0) {
                    if (!braced_temp(f.type.struct_id, &item)) return false;
                } else if (is(TLBrace)) {
                    error = "a nested list for a member of this type is not supported";
                    return false;
                } else {
                    if (!parse_expr(1, &item)) return false;
                    if (f.type.base != 3 && f.type.base != 0 && f.type.pointers == 0 && !f.type.is_array() &&
                        !convert_class(&item, f.type))
                        return false;
                }
                Node fn;
                fn.kind = Node::Field;
                fn.op = TDot;
                fn.text = f.name;
                fn.kids.push_back(node_name(hidden.name));
                Node assign;
                assign.kind = Node::Assign;
                assign.op = TAssign;
                assign.number = obj_needs_ctor(f.type) || obj_needs_dtor(f.type) ? 0 : 1;
                assign.kids.push_back(std::move(fn));
                assign.kids.push_back(std::move(item));
                Node st;
                st.kind = Node::ExprStmt;
                st.kids.push_back(std::move(assign));
                stmt_pre.push_back(std::move(st));
                ++k;
                if (!accept(TComma)) break;
            }
            if (!expect(TRBrace, "'}'")) return false;
        } else if (obj_needs_ctor(bt)) {
            next();
            std::vector<Node> bargs;
            if (!is(TRBrace)) {
                do {
                    Node a;
                    if (!parse_expr(1, &a)) return false;
                    bargs.push_back(std::move(a));
                } while (accept(TComma));
            }
            if (!expect(TRBrace, "'}'")) return false;
            Node ctor_stmt;
            if (!special_call(bsid, "<ctor>", node_addr(node_name(hidden.name)), std::move(bargs), &ctor_stmt))
                return false;
            stmt_pre.push_back(std::move(ctor_stmt));
        } else {
            Node binit;
            if (!parse_init(&binit)) return false;
            if (binit.kind == Node::Block && binit.text == "init") binit = elide_braces(bt, binit);  // std::array<int, 3>{a, b, c}
            Node assign;
            assign.kind = Node::Assign;
            assign.op = TAssign;
            assign.number = 1;
            assign.kids.push_back(node_name(hidden.name));
            assign.kids.push_back(std::move(binit));
            Node stmt;
            stmt.kind = Node::ExprStmt;
            stmt.kids.push_back(std::move(assign));
            stmt_pre.push_back(std::move(stmt));
        }
        if (class_needs_dtor(structs[static_cast<size_t>(bsid)])) stmt_temps.push_back(Temp{hidden.name, bsid});
    *out = node_name(hidden.name);
    return true;
}

// A lambda expression ([expr.prim.lambda]) becomes a class with an operator(). One that captures nothing is
// built with a static operator() and its value is the function's address (a function pointer); one that
// captures gets a field for each captured variable (a copy, or its address for &x), and its value is a hidden
// local of that class whose fields are filled before the statement. The return type is the written one, or the
// type of the first `return e;` (the body is read once to find it), or void.
bool Parser::parse_lambda(Node* out) {
    next();  // '['
    struct Cap {
        std::string name;
        bool ref = false;
    };
    std::vector<Cap> caps;
    bool default_value = false;
    bool default_ref = false;
    bool explicit_this = false;
    while (!is(TRBrack)) {
        if (is(TAssign)) {
            default_value = true;
            next();
        } else if (is(TAmp)) {
            next();
            if (is(TIdent)) {
                caps.push_back(Cap{cur().text, true});
                next();
            } else {
                default_ref = true;
            }
        } else if (is(TIdent) && cur().text == "this") {
            next();
            explicit_this = true;
        } else if (is(TIdent)) {
            Cap c;
            c.name = cur().text;
            next();
            if (is(TAssign)) {
                error = "an init-capture is not supported";
                return false;
            }
            caps.push_back(c);
        } else {
            error = "unsupported lambda capture";
            return false;
        }
        if (!accept(TComma)) break;
    }
    if (!expect(TRBrack, "']'")) return false;
    Declarator decl;
    bool generic = false;
    size_t generic_params_at = 0;
    if (is(TLParen)) {
        // `auto` among the parameters makes a generic lambda: its parameter list is read again for each set of argument types.
        size_t k = at + 1;
        int depth = 1;
        for (; k < tokens.size() && tokens[k].kind != TEnd; ++k) {
            if (tokens[k].kind == TLParen) ++depth;
            else if (tokens[k].kind == TRParen && --depth == 0) break;
            else if (depth == 1 && tokens[k].kind == TIdent && tokens[k].text == "auto") generic = true;
        }
        if (generic) {
            generic_params_at = at + 1;
            at = k + 1;
        }
    }
    if (!generic && accept(TLParen) && !parse_param_list(&decl)) return false;
    if (is(TIdent) && cur().text == "mutable") next();
    Type ret;
    ret.base = 0;
    bool has_ret = false;
    if (accept(TArrow)) {
        bool imp = false;
        bool exp = false;
        if (!parse_type(&ret, &imp, &exp)) return false;
        while (accept(TStar)) ++ret.pointers;
        if (accept(TAmp) || accept(TAndAnd)) ret.is_ref = true;
        has_ret = true;
    }
    if (!is(TLBrace)) {
        error = "expected the lambda body";
        return false;
    }
    const size_t body_at = at;
    size_t body_end = at;
    {
        int depth = 0;
        for (; body_end < tokens.size() && tokens[body_end].kind != TEnd; ++body_end) {
            if (tokens[body_end].kind == TLBrace) ++depth;
            else if (tokens[body_end].kind == TRBrace && --depth == 0) break;
        }
    }
    // The captured variables: the named ones, then (for = or &) every visible local the body mentions.
    std::vector<Cap> list;
    for (const Cap& c : caps)
        if (local_visible(c.name)) list.push_back(c);
    if (default_value || default_ref) {
        for (size_t i = body_at; i < body_end; ++i) {
            if (tokens[i].kind != TIdent || !local_visible(tokens[i].text)) continue;
            if (i > 0 && (tokens[i - 1].kind == TDot || tokens[i - 1].kind == TArrow || tokens[i - 1].kind == TColonColon))
                continue;
            bool is_param = false;
            for (const std::string& pn : decl.param_names)
                if (pn == tokens[i].text) is_param = true;
            bool known = false;
            for (const Cap& c : list)
                if (c.name == tokens[i].text) known = true;
            if (!is_param && !known) list.push_back(Cap{tokens[i].text, default_ref});
        }
    }
    bool uses_this = explicit_this;
    if (!uses_this && (default_value || default_ref) && cur_class >= 0 && structs[static_cast<size_t>(cur_class)].this_class < 0) {
        // The body names a member of the enclosing class (not a local, not after . -> ::): `this` is captured too.
        const StructDef& ed = structs[static_cast<size_t>(cur_class)];
        for (size_t i = body_at; i < body_end && !uses_this; ++i) {
            if (tokens[i].kind != TIdent || local_visible(tokens[i].text)) continue;
            if (i > 0 && (tokens[i - 1].kind == TDot || tokens[i - 1].kind == TArrow || tokens[i - 1].kind == TColonColon))
                continue;
            for (const Field& f : ed.fields)
                if (f.name == tokens[i].text) uses_this = true;
            for (const Method& m : ed.methods)
                if (m.name == tokens[i].text) uses_this = true;
            if (ed.statics.count(tokens[i].text) > 0) uses_this = true;
        }
    }
    const bool names_members = uses_this;
    if (uses_this && local_visible("this") && cur_class >= 0 && structs[static_cast<size_t>(cur_class)].this_class < 0) {
        Cap c;
        c.name = "this";
        c.ref = false;
        list.push_back(c);
    } else {
        uses_this = false;  // in a static member function there is no `this`: only static members can be named
    }
    if (cur_locals == nullptr && !list.empty()) {
        error = "a lambda that captures needs a function around it";
        return false;
    }
    StructDef def;
    def.name = "__lam" + std::to_string(lambda_count++);
    def.cxx = true;
    if (names_members) def.this_class = cur_class;
    int offset = 0;
    int align = 1;
    for (const Cap& c : list) {
        Node probe = node_name(c.name);
        Type vt;
        if (!static_type(probe, &vt)) {
            error = "cannot tell the type of the captured variable " + c.name;
            return false;
        }
        Field field;
        field.name = c.name == "this" ? std::string("__this") : c.name;
        field.type = vt;
        field.type.is_ref = false;
        field.type.pconst = false;
        if (c.ref) {
            if (vt.is_array()) {
                field.type = peel_array(field.type);
                field.type.pointers += 1;
            } else {
                field.type.pointers += 1;
                def.ref_caps.insert(c.name);
            }
        } else if (vt.is_array()) {
            error = "a lambda cannot capture an array by value";
            return false;
        }
        const int fa = std::max(1, align_of(field.type));
        const int fs = std::max(1, size_of(field.type));
        offset = (offset + fa - 1) & ~(fa - 1);
        field.offset = offset;
        offset += fs;
        align = std::max(align, fa);
        def.fields.push_back(field);
    }
    def.align = align;
    def.size = std::max(1, (offset + align - 1) & ~(align - 1));
    const int sid = static_cast<int>(structs.size());
    if (generic) {
        def.generic = true;
        def.gen_params_at = generic_params_at;
        def.gen_body_at = body_at;
        def.gen_body_end = body_end;
        def.gen_has_ret = has_ret;
        def.gen_ret = ret;
        for (size_t i = body_at; i < body_end; ++i)
            if (tokens[i].kind == TReturn && i + 1 < tokens.size() && tokens[i + 1].kind != TSemi) def.gen_returns_value = true;
        structs.push_back(def);
        at = body_end + 1;
        if (list.empty() && cur_locals == nullptr) {
            error = "a generic lambda needs a function around it";
            return false;
        }
        Local hidden;
        hidden.name = "__bt" + std::to_string(tmp_count++);
        hidden.type.base = 3;
        hidden.type.struct_id = sid;
        place_local(cur_locals, hidden);
        for (const Cap& c : list) {
            Node target;
            target.kind = Node::Field;
            target.op = TDot;
            target.text = c.name == "this" ? std::string("__this") : c.name;
            target.kids.push_back(node_name(hidden.name));
            Node value = node_name(c.name);
            Type vt;
            static_type(value, &vt);
            if (c.ref && !vt.is_array()) value = node_addr(std::move(value));
            Node assign;
            assign.kind = Node::Assign;
            assign.op = TAssign;
            assign.number = 1;
            assign.kids.push_back(std::move(target));
            assign.kids.push_back(std::move(value));
            Node stmt;
            stmt.kind = Node::ExprStmt;
            stmt.kids.push_back(std::move(assign));
            stmt_pre.push_back(std::move(stmt));
        }
        *out = node_name(hidden.name);
        return true;
    }
    structs.push_back(def);
    Method m;
    m.name = "operator()";
    m.ret = ret;
    if (!has_ret) m.ret.base = 0;
    m.params = decl.params;
    m.param_names = decl.param_names;
    m.defaults = decl.param_defaults;
    m.is_static = list.empty();
    m.has_body = true;
    m.body_at = body_at;
    // Does the body return a value?
    bool returns_value = false;
    for (size_t i = body_at; i < body_end; ++i)
        if (tokens[i].kind == TReturn && i + 1 < tokens.size() && tokens[i + 1].kind != TSemi) returns_value = true;
    if (!add_method(&structs[static_cast<size_t>(sid)], sid, m)) return false;
    const size_t mi = structs[static_cast<size_t>(sid)].methods.size() - 1;
    const size_t resume = body_end + 1;
    if (!has_ret && returns_value) {
        const bool saved_probe = lam_probe;
        const bool saved_seen = lam_probe_seen;
        const Type saved_type = lam_probe_type;
        lam_probe = true;
        lam_probe_seen = false;
        lam_probe_type = Type{};
        lam_probe_type.base = 1;
        structs[static_cast<size_t>(sid)].methods[mi].ret = Type{};
        structs[static_cast<size_t>(sid)].methods[mi].ret.base = 1;
        const size_t funcs_before = funcs.size();
        std::vector<Node> keep_pre = std::move(stmt_pre);
        stmt_pre.clear();
        std::vector<Temp> keep_temps = std::move(stmt_temps);
        stmt_temps.clear();
        const size_t probe_at = at;
        const bool probe_ok = define_method(sid, mi);
        at = probe_at;
        stmt_pre = std::move(keep_pre);
        stmt_temps = std::move(keep_temps);
        const Type found = lam_probe_type;
        lam_probe = saved_probe;
        lam_probe_seen = saved_seen;
        lam_probe_type = saved_type;
        if (!probe_ok) return false;
        {
            // Only the function made for the probe (the lambda's own operator()) is dropped: instances of templates and
            // class members that the body needed are kept, because their classes now count as defined.
            const std::string probe_name = structs[static_cast<size_t>(sid)].methods[mi].qname;
            for (size_t fi = funcs.size(); fi-- > funcs_before;)
                if (funcs[fi].name == probe_name) funcs.erase(funcs.begin() + static_cast<std::ptrdiff_t>(fi));
        }
        Method& mm = structs[static_cast<size_t>(sid)].methods[mi];
        mm.ret = found;
        mm.body_at = body_at;
        mm.symbol = cxx_method_symbol(structs, structs[static_cast<size_t>(sid)], mm);
    }
    {
        std::vector<Node> keep_pre = std::move(stmt_pre);
        stmt_pre.clear();
        std::vector<Temp> keep_temps = std::move(stmt_temps);
        stmt_temps.clear();
        const size_t keep_at = at;
        const bool ok = define_method(sid, mi);
        at = keep_at;
        stmt_pre = std::move(keep_pre);
        stmt_temps = std::move(keep_temps);
        if (!ok) return false;
    }
    at = resume;
    const std::string qname = structs[static_cast<size_t>(sid)].methods[mi].qname;
    if (list.empty()) {
        *out = node_name(qname);
        return true;
    }
    Local hidden;
    hidden.name = "__bt" + std::to_string(tmp_count++);
    hidden.type.base = 3;
    hidden.type.struct_id = sid;
    place_local(cur_locals, hidden);
    for (const Cap& c : list) {
        Node target;
        target.kind = Node::Field;
        target.op = TDot;
        target.text = c.name == "this" ? std::string("__this") : c.name;
        target.kids.push_back(node_name(hidden.name));
        Node value = node_name(c.name);
        Type vt;
        static_type(value, &vt);
        if (c.ref && !vt.is_array()) value = node_addr(std::move(value));
        Node assign;
        assign.kind = Node::Assign;
        assign.op = TAssign;
        assign.number = 1;
        assign.kids.push_back(std::move(target));
        assign.kids.push_back(std::move(value));
        Node stmt;
        stmt.kind = Node::ExprStmt;
        stmt.kids.push_back(std::move(assign));
        stmt_pre.push_back(std::move(stmt));
    }
    *out = node_name(hidden.name);
    return true;
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
            const Local* best = nullptr;
            int best_dist = 1 << 30;
            for (int i = static_cast<int>(cur_locals->size()) - 1; i >= 0; --i) {
                const Local& local = (*cur_locals)[static_cast<size_t>(i)];
                if (local.name != node.text) continue;
                const int dist = scope_distance(local.scope);
                if (dist >= 0 && dist < best_dist) {
                    best = &local;
                    best_dist = dist;
                }
            }
            if (best != nullptr) {
                *out = best->type;
                return true;
            }
        }
        for (const Global& global : globals)
            if (global.name == node.text) {
                *out = global.type;
                return true;
            }
        for (const FuncDecl& fn : funcs)
            if (fn.name == node.text) {
                // The name of a function is a pointer to it.
                *out = Type{};
                out->base = 1;
                out->pointers = 1;
                out->func_id = fn.func_id;
                return true;
            }
        return false;
    case Node::Assign:
        return !node.kids.empty() && static_type(node.kids[0], out);
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
        Type right;
        const bool have_right = static_type(node.kids[1], &right);
        left.is_ref = false;
        const bool left_ptr = left.pointers > 0 || left.is_array();
        const bool right_ptr = have_right && (right.pointers > 0 || right.is_array());
        if (left_ptr && right_ptr && node.op == TMinus) {
            *out = Type{};
            out->base = 1;
            out->wide = true;
            return true;
        }
        const auto arith = [](const Type& t) { return t.pointers == 0 && !t.is_array() && t.base != 3 && t.base != 0; };
        if (!left_ptr && have_right && !right_ptr && arith(left) && arith(right)) {
            right.is_ref = false;
            if (left.base == 6 || right.base == 6) {
                *out = Type{};
                out->base = 6;
                return true;
            }
            if (left.base == 5 || right.base == 5) {
                *out = Type{};
                out->base = 5;
                return true;
            }
            Type pl = left;
            pl.base = 1;
            pl.is_bool = false;
            if (left.base == 1) pl.is_unsigned = left.is_unsigned;
            else pl.is_unsigned = false;
            Type pr = right;
            pr.base = 1;
            pr.is_bool = false;
            if (right.base != 1) pr.is_unsigned = false;
            if (node.op == TShl || node.op == TShr) {
                *out = pl;
                out->bits = -1;
                return true;
            }
            *out = const_common(pl, pr);
            return true;
        }
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
            // A call through a variable that holds a function pointer.
            Node callee;
            callee.kind = Node::Name;
            callee.text = node.text;
            Type ct;
            if (static_type(callee, &ct) && ct.func_id >= 0 && ct.func_id < static_cast<int>(func_types.size())) {
                *out = func_types[static_cast<size_t>(ct.func_id)].ret;
                return true;
            }
        } else if (!node.kids.empty()) {
            Type ct;
            if (static_type(node.kids[0], &ct) && ct.func_id >= 0 && ct.func_id < static_cast<int>(func_types.size())) {
                *out = func_types[static_cast<size_t>(ct.func_id)].ret;
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
        if (is(TLBrace)) {
            // A braced list as an argument, f({1, 2}) or f({}): what it makes depends on the parameter, so its
            // position is kept and adjust_args reads it once the parameter is known.
            arg.kind = Node::Block;
            arg.text = "tokbrace";
            arg.number = at;
            int depth = 0;
            do {
                if (is(TLBrace)) ++depth;
                else if (is(TRBrace)) --depth;
                else if (is(TEnd)) {
                    error = "expected '}'";
                    return false;
                }
                next();
            } while (depth > 0);
            args->push_back(std::move(arg));
            continue;
        }
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
        if (find_member_tmpl(cls, name, &tmpl_owner) != nullptr && find_method(cls, name, args.size(), &args) == nullptr) {
            if (!instantiate_member(cls, name, std::move(targs), args, &method_name, &method_owner)) return false;
        }
    }
    const Method* m = find_method(method_owner, method_name, args.size(), &args);
    if (m == nullptr) {
        error = "no member function " + name;
        return false;
    }
    if (!pad_defaults(m->defaults, m->params.size(), &args)) return false;
    if (!adjust_args(m->params, &args)) return false;
    if (args.empty() && m->is_static && try_const_call(*m, out)) return true;
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
    *out = reference_result(std::move(call), m->ret.is_ref, m->ret.rref);
    return true;
}

// object.name(args) or object->name(args), with '(' already read. A virtual
// function reached through a pointer or reference is called through the
// object's table: the table pointer is the first field, the slot is an index.
bool Parser::method_call(Node* out, Node object, bool arrow, const std::string& name, std::vector<TArg> targs,
                         std::vector<Node>* preparsed) {
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
    if (preparsed != nullptr) args = std::move(*preparsed);
    else if (!parse_call_args(&args)) return false;
    std::string method_name = name;
    int method_owner = sid;
    {
        int tmpl_owner = -1;
        if (find_member_tmpl(sid, name, &tmpl_owner) != nullptr && find_method(sid, name, args.size(), &args) == nullptr) {
            if (!instantiate_member(sid, name, std::move(targs), args, &method_name, &method_owner)) return false;
        }
    }
    const Method* m = find_method(method_owner, method_name, args.size(), &args, ot.is_const || ot.pconst ? 1 : 0);
    if (m == nullptr) {
        error = "no member function " + name + " with " + std::to_string(args.size()) + " arguments";
        return false;
    }
    if (!pad_defaults(m->defaults, m->params.size(), &args)) return false;
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
            // The object expression is used twice (to find the table and as `this`): it is evaluated once, into a hidden
            // pointer, before the statement.
            Type self_type;
            if (cur_locals == nullptr || !static_type(self, &self_type)) {
                error = "a virtual call needs an object expression without side effects";
                return false;
            }
            Local hidden;
            hidden.name = "__vo" + std::to_string(tmp_count++);
            hidden.type = self_type;
            place_local(cur_locals, hidden);
            Node assign;
            assign.kind = Node::Assign;
            assign.op = TAssign;
            assign.number = 1;
            assign.kids.push_back(node_name(hidden.name));
            assign.kids.push_back(std::move(self));
            Node stmt;
            stmt.kind = Node::ExprStmt;
            stmt.kids.push_back(std::move(assign));
            stmt_pre.push_back(std::move(stmt));
            self = node_name(hidden.name);
        }
        FuncType ft;
        ft.ret = m->ret;
        Type self_ptr;
        self_ptr.base = 3;
        self_ptr.struct_id = m->owner;
        self_ptr.pointers = 1;
        ft.params.push_back(self_ptr);
        for (const Type& param : m->params) {
            Type as_passed = param;
            if (as_passed.is_ref) {  // a reference parameter is passed as a pointer
                as_passed.is_ref = false;
                as_passed.rref = false;
                as_passed.pointers += 1;
            }
            ft.params.push_back(as_passed);
        }
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
    *out = reference_result(std::move(call), m->ret.is_ref, m->ret.rref);
    return true;
}

// ---- operator functions and conversion functions (ISO/IEC 14882 [over.oper], [over.match.oper], [class.conv.fct]) ----

bool Parser::operator_name(Tok op, std::string* name) const {
    const char* spelling = nullptr;
    switch (op) {
    case TPlus: spelling = "+"; break;
    case TMinus: spelling = "-"; break;
    case TStar: spelling = "*"; break;
    case TSlash: spelling = "/"; break;
    case TPercent: spelling = "%"; break;
    case TAmp: spelling = "&"; break;
    case TPipe: spelling = "|"; break;
    case TCaret: spelling = "^"; break;
    case TTilde: spelling = "~"; break;
    case TNot: spelling = "!"; break;
    case TAssign: spelling = "="; break;
    case TLt: spelling = "<"; break;
    case TGt: spelling = ">"; break;
    case TLe: spelling = "<="; break;
    case TGe: spelling = ">="; break;
    case TEq: spelling = "=="; break;
    case TNe: spelling = "!="; break;
    case TPlusPlus: spelling = "++"; break;
    case TMinusMinus: spelling = "--"; break;
    case TPlusEq: spelling = "+="; break;
    case TMinusEq: spelling = "-="; break;
    case TStarEq: spelling = "*="; break;
    case TSlashEq: spelling = "/="; break;
    case TPercentEq: spelling = "%="; break;
    case TAndEq: spelling = "&="; break;
    case TOrEq: spelling = "|="; break;
    case TXorEq: spelling = "^="; break;
    case TShl: spelling = "<<"; break;
    case TShr: spelling = ">>"; break;
    case TShlEq: spelling = "<<="; break;
    case TShrEq: spelling = ">>="; break;
    case TAndAnd: spelling = "&&"; break;
    case TOrOr: spelling = "||"; break;
    default: return false;
    }
    *name = std::string("operator") + spelling;
    return true;
}

// The node is an object of a class (not a pointer, reference-to-pointer or array).
bool Parser::class_object_type(const Node& node, Type* out) const {
    Type t;
    if (!static_type(node, &t)) return false;
    if (t.base != 3 || t.pointers != 0 || t.is_array() || t.struct_id < 0 || t.func_id >= 0) return false;
    *out = t;
    return true;
}

// A call of the free function `name` (found in any namespace) with these arguments, when one fits.
bool Parser::free_call(const std::string& name, std::vector<Node> args, Node* out, bool* found) {
    *found = false;
    const std::string picked = pick_any_ns(name, args, true);
    if (picked.empty()) return true;
    for (const FuncDecl& known : funcs) {
        if (known.name != picked) continue;
        *found = true;
        Node call;
        call.kind = Node::Call;
        call.text = picked;
        call.kids = std::move(args);
        if (!pad_func_defaults(picked, &call.kids)) return false;
        std::vector<Type> source = known.params;
        for (size_t i = 0; i < source.size() && i < known.param_ref.size(); ++i)
            if (known.param_ref[i]) {
                source[i].pointers -= 1;
                source[i].is_ref = true;
            }
        if (!adjust_args(source, &call.kids)) return false;
        *out = known.ret_ref ? reference_result(std::move(call), true, known.ret.rref) : std::move(call);
        return true;
    }
    return true;
}

// An object of a class used where a number or a pointer is wanted: its conversion function is called.
bool Parser::convert_class(Node* node, const Type& target) {
    Type t;
    if (!class_object_type(*node, &t)) return true;
    const Method* m = conv_for(t.struct_id, target);
    if (m == nullptr) return true;
    std::vector<Node> none;
    Node result;
    if (!method_call(&result, std::move(*node), false, m->name, {}, &none)) return false;
    *node = std::move(result);
    return true;
}

// left op right where an operand is an object of a class: a member of the left class, else a free function.
// `done` tells that *left now holds the call. Where no operator function fits, an operand that has a
// conversion function is converted and the built-in operator is left to the caller.
bool Parser::operator_binary(Tok op, Node* left, Node* right, bool* done) {
    *done = false;
    Type lt;
    Type rt;
    const bool lc = class_object_type(*left, &lt);
    bool rc = class_object_type(*right, &rt);
    std::string name;
    if (!operator_name(op, &name)) return true;
    if (!lc && !rc) return true;
    if (op == TAssign && lc && (!rc ? structs[static_cast<size_t>(lt.struct_id)].ctor_tmpl : rt.struct_id != lt.struct_id) &&
        cur_locals != nullptr) {
        // `f = lambda;` where the left class has a constructor that takes the right object: the right side is first made
        // into a temporary of the left class ([over.match.copy], [class.conv.ctor]).
        bool related = false;
        if (rc) {
            for (int walk = rt.struct_id; walk >= 0; walk = structs[static_cast<size_t>(walk)].base)
                if (walk == lt.struct_id) related = true;
            for (int walk = lt.struct_id; walk >= 0 && !related; walk = structs[static_cast<size_t>(walk)].base)
                if (walk == rt.struct_id) related = true;
            for (const Method& m : structs[static_cast<size_t>(lt.struct_id)].methods)
                if (m.name == name && !m.implicit && m.params.size() == 1 && m.params[0].base == 3 &&
                    m.params[0].struct_id == rt.struct_id)
                    related = true;  // an operator= for exactly this class: no conversion
        }
        if (!related) {
            Local hidden;
            hidden.name = "__cv" + std::to_string(tmp_count++);
            hidden.type = lt;
            hidden.type.is_const = false;
            place_local(cur_locals, hidden);
            std::vector<Node> cargs;
            cargs.push_back(std::move(*right));
            Node ctor_stmt;
            if (!special_call(lt.struct_id, "<ctor>", node_addr(node_name(hidden.name)), std::move(cargs), &ctor_stmt))
                return false;
            stmt_pre.push_back(std::move(ctor_stmt));
            if (class_needs_dtor(structs[static_cast<size_t>(lt.struct_id)])) stmt_temps.push_back(Temp{hidden.name, lt.struct_id});
            *right = node_name(hidden.name);
            rt = lt;
            rc = true;
        }
    }
    if (op == TAssign && lc) {
        // Only a user-written operator= is called; without one the members are copied as before.
        std::vector<Node> args;
        args.push_back(*right);
        const Method* m = find_method(lt.struct_id, name, 1, &args);
        if (m == nullptr || (m->implicit && !m->implicit_assign) || m->is_static) return true;
        Node result;
        if (!method_call(&result, std::move(*left), false, name, {}, &args)) return false;
        *left = std::move(result);
        *done = true;
        return true;
    }
    if (op == TAssign) {
        Type target;
        if (static_type(*left, &target) && arith_type(target)) return convert_class(right, target);
        return true;
    }
    if (lc) {
        std::vector<Node> args;
        args.push_back(*right);
        const Method* m = find_method(lt.struct_id, name, 1, &args, lt.is_const || lt.pconst ? 1 : 0);
        if (m != nullptr && !m->is_static && call_score(m->params, args) >= 0) {
            Node result;
            if (!method_call(&result, std::move(*left), false, name, {}, &args)) return false;
            *left = std::move(result);
            *done = true;
            return true;
        }
    }
    {
        std::vector<Node> args;
        args.push_back(*left);
        args.push_back(*right);
        Node result;
        bool found = false;
        if (!free_call(name, std::move(args), &result, &found)) return false;
        if (found) {
            *left = std::move(result);
            *done = true;
            return true;
        }
    }
    if (is_compound_op(op)) {
        Type target;
        if (rc && static_type(*left, &target) && arith_type(target)) return convert_class(right, target);
        return true;
    }
    Type target;
    target.base = 1;
    if (op == TAndAnd || op == TOrOr) {
        target.base = 2;
        target.is_bool = true;
        target.is_unsigned = true;
    }
    if (lc) {
        Type other = target;
        Type rtype;
        if (static_type(*right, &rtype) && arith_type(rtype) && op != TAndAnd && op != TOrOr && (rtype.base >= 5 || rtype.wide)) other = rtype;
        if (!convert_class(left, other)) return false;
    }
    if (rc) {
        Type other = target;
        Type ltype;
        if (static_type(*left, &ltype) && arith_type(ltype) && op != TAndAnd && op != TOrOr && (ltype.base >= 5 || ltype.wide)) other = ltype;
        if (!convert_class(right, other)) return false;
    }
    return true;
}

bool Parser::operator_unary(Tok op, bool postfix, Node* operand, bool* done) {
    *done = false;
    Type t;
    if (!class_object_type(*operand, &t)) return true;
    std::string name;
    if (!operator_name(op, &name)) return true;
    std::vector<Node> args;
    if (postfix) {
        Node zero;
        zero.kind = Node::IntLit;
        zero.number = 0;
        zero.type.base = 1;
        args.push_back(zero);
    }
    const Method* m = find_method(t.struct_id, name, args.size(), &args, t.is_const || t.pconst ? 1 : 0);
    if (m != nullptr && !m->is_static) {
        Node result;
        if (!method_call(&result, std::move(*operand), false, name, {}, &args)) return false;
        *operand = std::move(result);
        *done = true;
        return true;
    }
    std::vector<Node> fargs;
    fargs.push_back(*operand);
    if (postfix) fargs.push_back(args[0]);
    Node result;
    bool found = false;
    if (!free_call(name, std::move(fargs), &result, &found)) return false;
    if (found) {
        *operand = std::move(result);
        *done = true;
        return true;
    }
    if (op == TMinus || op == TNot || op == TTilde) {
        Type target;
        target.base = op == TNot ? 2 : 1;
        target.is_bool = op == TNot;
        target.is_unsigned = op == TNot;
        return convert_class(operand, target);
    }
    return true;
}

// A call of a generic lambda: the operator() for these argument types is made from the saved parameter list and body (the
// same steps as parse_lambda: the result type is found by a probe of the body when it is not written).
bool Parser::instantiate_generic_lambda(int sid, const std::vector<Node>& args) {
    if (!structs[static_cast<size_t>(sid)].generic) return true;
    std::vector<Type> types;
    std::string key;
    for (const Node& a : args) {
        Type t;
        if (!static_type(a, &t)) {
            error = "cannot tell the type of an argument of a generic lambda";
            return false;
        }
        t.is_ref = false;
        t.rref = false;
        t.pconst = false;
        if (t.is_array()) {
            t = peel_array(t);
            t.pointers += 1;
            t.array_len = -1;
        }
        key += cxx_type(t, &structs) + ";";
        types.push_back(t);
    }
    if (structs[static_cast<size_t>(sid)].gen_done.count(key) > 0) return true;
    structs[static_cast<size_t>(sid)].gen_done.insert(key);
    const size_t params_at = structs[static_cast<size_t>(sid)].gen_params_at;
    const size_t body_at = structs[static_cast<size_t>(sid)].gen_body_at;
    const bool has_ret = structs[static_cast<size_t>(sid)].gen_has_ret;
    const Type ret = structs[static_cast<size_t>(sid)].gen_ret;
    const bool returns_value = structs[static_cast<size_t>(sid)].gen_returns_value;
    const size_t keep_at = at;
    auto_types = types;
    auto_idx = 0;
    at = params_at;
    Declarator decl;
    const bool params_ok = parse_param_list(&decl);
    auto_types.clear();
    auto_idx = 0;
    at = keep_at;
    if (!params_ok) return false;
    Method m;
    m.name = "operator()";
    m.ret = ret;
    if (!has_ret) m.ret.base = 0;
    m.params = decl.params;
    m.param_names = decl.param_names;
    m.defaults = decl.param_defaults;
    m.is_static = false;
    m.has_body = true;
    m.body_at = body_at;
    if (!add_method(&structs[static_cast<size_t>(sid)], sid, m)) return false;
    const size_t mi = structs[static_cast<size_t>(sid)].methods.size() - 1;
    if (!has_ret && returns_value) {
        const bool saved_probe = lam_probe;
        const bool saved_seen = lam_probe_seen;
        const Type saved_type = lam_probe_type;
        lam_probe = true;
        lam_probe_seen = false;
        lam_probe_type = Type{};
        lam_probe_type.base = 1;
        structs[static_cast<size_t>(sid)].methods[mi].ret = Type{};
        structs[static_cast<size_t>(sid)].methods[mi].ret.base = 1;
        const size_t funcs_before = funcs.size();
        std::vector<Node> keep_pre = std::move(stmt_pre);
        stmt_pre.clear();
        std::vector<Temp> keep_temps = std::move(stmt_temps);
        stmt_temps.clear();
        const size_t probe_at = at;
        const bool probe_ok = define_method(sid, mi);
        at = probe_at;
        stmt_pre = std::move(keep_pre);
        stmt_temps = std::move(keep_temps);
        const Type found = lam_probe_type;
        lam_probe = saved_probe;
        lam_probe_seen = saved_seen;
        lam_probe_type = saved_type;
        if (!probe_ok) return false;
        {
            // Only the function made for the probe (the lambda's own operator()) is dropped: instances of templates and
            // class members that the body needed are kept, because their classes now count as defined.
            const std::string probe_name = structs[static_cast<size_t>(sid)].methods[mi].qname;
            for (size_t fi = funcs.size(); fi-- > funcs_before;)
                if (funcs[fi].name == probe_name) funcs.erase(funcs.begin() + static_cast<std::ptrdiff_t>(fi));
        }
        Method& mm = structs[static_cast<size_t>(sid)].methods[mi];
        mm.ret = found;
        mm.body_at = body_at;
        mm.symbol = cxx_method_symbol(structs, structs[static_cast<size_t>(sid)], mm);
    }
    std::vector<Node> keep_pre = std::move(stmt_pre);
    stmt_pre.clear();
    std::vector<Temp> keep_temps = std::move(stmt_temps);
    stmt_temps.clear();
    const size_t keep_at2 = at;
    const bool ok = define_method(sid, mi);
    at = keep_at2;
    stmt_pre = std::move(keep_pre);
    stmt_temps = std::move(keep_temps);
    return ok;
}

bool Parser::operator_call(Node* object, std::vector<Node> args, bool* done) {
    *done = false;
    Type t;
    if (!class_object_type(*object, &t)) return true;
    if (!instantiate_generic_lambda(t.struct_id, args)) return false;
    const Method* m = find_method(t.struct_id, "operator()", args.size(), &args, t.is_const || t.pconst ? 1 : 0);
    int call_tmpl_owner = -1;
    if (m == nullptr && find_member_tmpl(t.struct_id, "operator()", &call_tmpl_owner) == nullptr) return true;  // a member template instantiates in method_call
    Node result;
    if (!method_call(&result, std::move(*object), false, "operator()", {}, &args)) return false;
    *object = std::move(result);
    *done = true;
    return true;
}

bool Parser::operator_index(Node* object, Node index, bool* done) {
    *done = false;
    Type t;
    if (!class_object_type(*object, &t)) return true;
    std::vector<Node> args;
    args.push_back(std::move(index));
    const Method* m = find_method(t.struct_id, "operator[]", 1, &args, t.is_const || t.pconst ? 1 : 0);
    if (m == nullptr) {
        error = "no operator[] in the class";
        return false;
    }
    Node result;
    if (!method_call(&result, std::move(*object), false, "operator[]", {}, &args)) return false;
    *object = std::move(result);
    *done = true;
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
    // new (place) T(args) ([expr.new]): construct in storage that is already there; nothing is allocated.
    bool has_place = false;
    Node place;
    if (is(TLParen)) {
        const size_t save = at;
        next();
        if (!is_decl_start()) {
            if (!parse_expr(1, &place)) return false;
            if (!expect(TRParen, "')'")) return false;
            has_place = true;
        } else {
            at = save;
        }
    }
    Type ty;
    bool imp = false;
    bool exp = false;
    if (!parse_type(&ty, &imp, &exp)) return false;
    while (accept(TStar)) ++ty.pointers;
    if (has_place && is(TLBrack)) {
        error = "placement new of an array is not supported";
        return false;
    }
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
    bool plain_copy = false;  // new C(other) where C has constructors but no copy constructor: the bytes are copied
    if (obj && args.size() == 1 && class_needs_ctor(structs[static_cast<size_t>(ty.struct_id)]) &&
        !class_has_copy(ty.struct_id)) {
        Type from;
        plain_copy = static_type(args[0], &from) && from.base == 3 && from.pointers == 0 && !from.is_array() &&
                     from.struct_id == ty.struct_id;
    }
    const bool ctor = obj && class_needs_ctor(structs[static_cast<size_t>(ty.struct_id)]) && !plain_copy;
    if (!args.empty() && !ctor && !(args.size() == 1 && (has_place || plain_copy || !obj))) {
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
    cast.kids.push_back(has_place ? std::move(place) : std::move(alloc));
    if (!ctor && !args.empty()) {
        // new (p) int(v): the value is stored in a statement before this one, and the pointer is the result.
        Local hidden;
        hidden.name = "__pn" + std::to_string(tmp_count++);
        hidden.type = cast.type;
        place_local(cur_locals, hidden);
        Node set_ptr;
        set_ptr.kind = Node::Assign;
        set_ptr.op = TAssign;
        set_ptr.kids.push_back(node_name(hidden.name));
        set_ptr.kids.push_back(std::move(cast));
        Node target;
        target.kind = Node::Unary;
        target.op = TStar;
        target.kids.push_back(node_name(hidden.name));
        Node store;
        store.kind = Node::Assign;
        store.op = TAssign;
        store.kids.push_back(std::move(target));
        store.kids.push_back(std::move(args[0]));
        Node set_stmt;
        set_stmt.kind = Node::ExprStmt;
        set_stmt.kids.push_back(std::move(set_ptr));
        Node store_stmt;
        store_stmt.kind = Node::ExprStmt;
        store_stmt.kids.push_back(std::move(store));
        stmt_pre.push_back(std::move(set_stmt));
        stmt_pre.push_back(std::move(store_stmt));
        *out = node_name(hidden.name);
        return true;
    }
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
    if (!pad_defaults(m->defaults, m->params.size(), &args)) return false;
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

// typeid(type) and typeid(expression) ([expr.typeid]): the value is a `const type_info&`. For an object of a class with virtual
// functions the dynamic type is found through vtable[-1]; otherwise the static type is used. Needs <typeinfo>.
bool Parser::parse_typeid(Node* out) {
    next();
    next();  // typeid (
    const int tid = find_struct("type_info");
    if (tid < 0) {
        error = "typeid needs <typeinfo>";
        return false;
    }
    Type ptr_type;
    ptr_type.base = 3;
    ptr_type.struct_id = tid;
    ptr_type.pointers = 1;
    Type ty;
    if (is_decl_start()) {
        bool imp = false;
        bool exp = false;
        if (!parse_type(&ty, &imp, &exp)) return false;
        while (accept(TStar)) ++ty.pointers;
        if (accept(TAmp) || accept(TAndAnd)) ty.is_ref = false;
        if (!expect(TRParen, "')'")) return false;
        ty.is_const = false;
        const std::string sym = typeinfo_global(ty);
        Node inner = cast_node(ptr_type, node_addr(node_name(sym)));
        out->kind = Node::Unary;
        out->op = TStar;
        out->kids.push_back(std::move(inner));
        return parse_postfix(out);
    }
    Node e;
    if (!parse_expr(0, &e)) return false;
    if (!expect(TRParen, "')'")) return false;
    Type et;
    if (!static_type(e, &et)) {
        error = "cannot tell the type of the typeid operand";
        return false;
    }
    et.is_ref = false;
    et.is_const = false;
    if (et.base == 3 && et.pointers == 0 && et.struct_id >= 0 && structs[static_cast<size_t>(et.struct_id)].poly) {
        Node obj_ptr = node_addr(std::move(e));
        Node vptr;
        vptr.kind = Node::Field;
        vptr.op = TArrow;
        vptr.text = "__vptr";
        vptr.kids.push_back(std::move(obj_ptr));
        Node minus_one;
        minus_one.kind = Node::IntLit;
        minus_one.number = static_cast<uint64_t>(-1);
        minus_one.type.base = 1;
        Node slot;
        slot.kind = Node::Index;
        slot.kids.push_back(std::move(vptr));
        slot.kids.push_back(std::move(minus_one));
        out->kind = Node::Unary;
        out->op = TStar;
        out->kids.push_back(cast_node(ptr_type, std::move(slot)));
        return parse_postfix(out);
    }
    const std::string sym = typeinfo_global(et);
    out->kind = Node::Unary;
    out->op = TStar;
    out->kids.push_back(cast_node(ptr_type, node_addr(node_name(sym))));
    return parse_postfix(out);
}

// dynamic_cast<T*>(e) and dynamic_cast<T&>(e) ([expr.dynamic.cast]) for a class with virtual functions and single inheritance:
// a call of the runtime function __okay_dyncast(pointer, type record of T), which walks from the object's real type up its
// base chain. The pointer is returned unchanged (the base starts at the start of the object). A failed reference cast gives a
// null reference here (there is no bad_cast).
bool Parser::parse_dynamic_cast(Node* out) {
    next();
    next();  // dynamic_cast <
    Type ty;
    bool imp = false;
    bool exp = false;
    if (!parse_type(&ty, &imp, &exp)) return false;
    int stars = 0;
    while (accept(TStar)) ++stars;
    bool by_ref = false;
    if (accept(TAmp) || accept(TAndAnd)) by_ref = true;
    if (!expect(TGt, "'>'")) return false;
    if (!expect(TLParen, "'('")) return false;
    Node e;
    if (!parse_expr(0, &e)) return false;
    if (!expect(TRParen, "')'")) return false;
    if (ty.base != 3 || ty.struct_id < 0 || (!by_ref && stars != 1)) {
        error = "dynamic_cast supports a pointer or reference to a class";
        return false;
    }
    Type target = ty;
    target.is_const = false;
    target.pointers = 0;
    target.is_ref = false;
    const std::string sym = typeinfo_global(target);
    bool have = false;
    for (const FuncDecl& fn : funcs)
        if (fn.name == "__okay_dyncast") have = true;
    if (!have) {
        FuncDecl fn;
        fn.name = "__okay_dyncast";
        fn.symbol = "__okay_dyncast";
        fn.ret.base = 0;
        fn.ret.pointers = 1;
        Type vp;
        vp.base = 0;
        vp.pointers = 1;
        fn.params.push_back(vp);
        fn.params.push_back(vp);
        fn.param_names = {"p", "t"};
        fn.defined = false;
        finish_signature(&fn);
        funcs.push_back(std::move(fn));
    }
    Type vp;
    vp.base = 0;
    vp.pointers = 1;
    Type src;
    Node arg0;
    if (by_ref) {
        arg0 = node_addr(std::move(e));
    } else {
        arg0 = std::move(e);
    }
    Node call;
    call.kind = Node::Call;
    call.text = "__okay_dyncast";
    call.kids.push_back(cast_node(vp, std::move(arg0)));
    call.kids.push_back(cast_node(vp, node_addr(node_name(sym))));
    Type result = target;
    result.pointers = 1;
    Node casted = cast_node(result, std::move(call));
    if (by_ref) {
        out->kind = Node::Unary;
        out->op = TStar;
        out->kids.push_back(std::move(casted));
    } else {
        *out = std::move(casted);
    }
    return parse_postfix(out);
}

// __is_base_of(B, D), __is_class(T), __is_polymorphic(T), __is_default_constructible(T): the compiler's answers for the
// <type_traits> templates of the same names ([meta.rel], [meta.unary.prop]); an integer constant 0 or 1.
bool Parser::parse_type_trait(Node* out) {
    const std::string which = cur().text;
    next();
    next();  // name (
    Type a;
    bool imp = false;
    bool exp = false;
    if (!parse_type(&a, &imp, &exp)) return false;
    while (accept(TStar)) ++a.pointers;
    Type b2;
    if (which == "__is_base_of") {
        if (!expect(TComma, "','")) return false;
        if (!parse_type(&b2, &imp, &exp)) return false;
        while (accept(TStar)) ++b2.pointers;
    }
    if (!expect(TRParen, "')'")) return false;
    const auto is_class = [](const Type& t) { return t.base == 3 && t.pointers == 0 && !t.is_array() && t.struct_id >= 0; };
    bool answer = false;
    if (which == "__is_base_of") {
        if (is_class(a) && is_class(b2))
            for (int walk = b2.struct_id; walk >= 0; walk = structs[static_cast<size_t>(walk)].base)
                if (walk == a.struct_id) answer = true;
    } else if (which == "__is_class") {
        answer = is_class(a);
    } else if (which == "__is_polymorphic") {
        answer = is_class(a) && structs[static_cast<size_t>(a.struct_id)].poly;
    } else {
        // Default-constructible: a scalar, or a class with no constructors or one that can be called with no arguments.
        answer = true;
        if (is_class(a)) {
            bool any_ctor = false;
            bool callable = false;
            for (const Method& m : structs[static_cast<size_t>(a.struct_id)].methods) {
                if (m.name != "<ctor>" || m.owner != a.struct_id) continue;
                any_ctor = true;
                if (required_args(m.defaults, m.params.size()) == 0) callable = true;
            }
            answer = !any_ctor || callable;
        }
    }
    out->kind = Node::IntLit;
    out->number = answer ? 1 : 0;
    out->type = Type{};
    out->type.base = 1;
    return parse_postfix(out);
}

bool Parser::parse_unary(Node* out) {
    if (is(TIdent) && cur().text == "typeid" && at + 1 < tokens.size() && tokens[at + 1].kind == TLParen) return parse_typeid(out);
    if (is(TIdent) && cur().text == "dynamic_cast" && at + 1 < tokens.size() && tokens[at + 1].kind == TLt) return parse_dynamic_cast(out);
    if (is(TIdent) && at + 1 < tokens.size() && tokens[at + 1].kind == TLParen &&
        (cur().text == "__is_base_of" || cur().text == "__is_class" || cur().text == "__is_polymorphic" ||
         cur().text == "__is_default_constructible"))
        return parse_type_trait(out);
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
        // sizeof unary-expression has no parentheses ([expr.sizeof]); sizeof ( type ) has them.
        const bool sizeof_paren = is(TLParen);
        if (sizeof_paren) next();
        int bytes = 0;
        if (sizeof_paren && is_decl_start() && !is(TStatic)) {
            Type ty;
            bool imp = false;
            bool exp = false;
            if (!parse_type(&ty, &imp, &exp)) return false;
            while (accept(TStar)) ++ty.pointers;
            if (!parse_brackets(&ty)) return false;
            bytes = size_of(ty);
        } else {
            Node inner;
            if (sizeof_paren ? !parse_expr(1, &inner) : !parse_unary(&inner)) return false;
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
                bytes = inner.type.base == 6 ? 8 : inner.type.wide ? 8 : 4;
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
        if (sizeof_paren && !expect(TRParen, "')'")) return false;
        out->kind = Node::IntLit;
        out->number = static_cast<uint64_t>(bytes);
        // sizeof gives a size_t: unsigned and 64 bits wide ([expr.sizeof]).
        out->type.base = 1;
        out->type.wide = true;
        out->type.is_unsigned = true;
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
        {
            bool done = false;
            if (!operator_unary(out->op, false, &inner, &done)) return false;
            if (done) {
                *out = std::move(inner);
                return parse_postfix(out);
            }
        }
        out->kids.push_back(std::move(inner));
        return true;
    }
    if (is(TNumber)) {
        out->kind = Node::IntLit;
        out->number = cur().number;
        out->type.base = cur().real != 0 ? cur().real : 1;
        if (cur().real == 0) {
            out->type.wide = cur().ity >= 2;
            out->type.is_unsigned = cur().ity == 1 || cur().ity == 3;
            // A number is kept in the form its type gives: sign-extended int, zero-extended unsigned int.
            if (cur().ity == 0) out->number = static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(out->number)));
        }
        next();
    } else if (is(TString)) {
        out->kind = Node::StringLit;
        out->text = cur().text;
        out->type.pointers = 1;
        out->type.base = 2;
        next();
    } else if (is(TIdent) || (is(TColonColon) && at + 1 < tokens.size() && tokens[at + 1].kind == TIdent)) {
        const bool from_global = is(TColonColon);
        if (from_global) next();  // ::name: looked up at global scope, not in the namespaces around the call
        std::string name = cur().text;
        next();
        if (!global_alias.empty() && !is(TLParen) && !local_visible(name)) {
            const auto alias = global_alias.find(name);
            if (alias != global_alias.end()) name = alias->second;
        }
        int scope_sid = -1;
        if (is(TLt) && class_templates.count(name) > 0 && !variable_named(name)) {
            // Box<int>::member in an expression.
            std::vector<TArg> targs;
            if (!parse_template_args(class_templates[name], &targs, false)) return false;
            Type inst;
            if (!instantiate_class(name, targs, &inst)) return false;
            if ((is(TLParen) || is(TLBrace)) && inst.base == 3 && inst.struct_id >= 0) {
                // Box<int>(args): a temporary of the instance, made like any class name followed by an argument list.
                name = structs[static_cast<size_t>(inst.struct_id)].name;
            } else if (!is(TColonColon) || at + 1 >= tokens.size() || tokens[at + 1].kind != TIdent) {
                error = "expected :: and a member name after a class template instance";
                return false;
            } else {
                next();
                scope_sid = inst.struct_id;
                name = "<inst>::" + cur().text;
                next();
            }
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
        bool exact_function = false;
        if (name.find("::") != std::string::npos)
            for (const FuncDecl& fd : funcs)
                if (fd.name == name) exact_function = true;
        bool member_hides = false;  // a member function of the class being compiled hides a namespace function of its name
        if (cur_class >= 0 && name.find("::") == std::string::npos)
            for (const Method& hm : structs[static_cast<size_t>(cur_class)].methods)
                if (hm.name == name) member_hides = true;
        if (!exact_function && !from_global && !current_ns.empty() && is(TLParen) && !local_visible(name) && !member_hides) {
            // A bare name inside a namespace finds the functions of that namespace and of the ones around
            // it ([basic.lookup.unqual]), innermost first.
            std::string ns = current_ns;
            for (;;) {
                const std::string candidate = ns + "::" + name;
                bool known = false;
                for (const FuncDecl& fd : funcs)
                    if (fd.name == candidate) known = true;
                if (known) {
                    name = candidate;
                    break;
                }
                const size_t cut = ns.rfind("::");
                if (cut == std::string::npos) break;
                ns = ns.substr(0, cut);
            }
        }
        if (name.find("::") == std::string::npos && is(TLParen) && !local_visible(name) && !using_namespaces.empty()) {
            // A bare call that names no function of its own: look in the namespaces of `using namespace`.
            bool own = false;
            for (const FuncDecl& fd : funcs)
                if (fd.name == name) own = true;
            if (!own) {
                bool found_using = false;
                for (const std::string& ns : using_namespaces) {
                    // `using namespace ac;` names ac as seen from where the directive is: also inside each enclosing
                    // namespace of the code being compiled (okay::ac).
                    std::string open = current_ns;
                    for (;;) {
                        const std::string candidate = (open.empty() ? std::string() : open + "::") + ns + "::" + name;
                        bool known = false;
                        for (const FuncDecl& fd : funcs)
                            if (fd.name == candidate) known = true;
                        if (known) {
                            name = candidate;
                            found_using = true;
                            break;
                        }
                        if (open.empty()) break;
                        const size_t cut = open.rfind("::");
                        open = cut == std::string::npos ? std::string() : open.substr(0, cut);
                    }
                    if (found_using) break;
                }
            }
        }
        if (name.find("::") != std::string::npos && is(TLParen) && !local_visible(name)) {
            // Easing::Linear(x) where Easing is a namespace inside one that a `using namespace` names (or that is open
            // here): the function is registered with the whole path.
            bool own = false;
            for (const FuncDecl& fd : funcs)
                if (fd.name == name) own = true;
            if (!own) {
                std::vector<std::string> outer = using_namespaces;
                std::string open = current_ns;
                while (!open.empty()) {
                    outer.push_back(open);
                    const size_t cut = open.rfind("::");
                    open = cut == std::string::npos ? std::string() : open.substr(0, cut);
                }
                for (const std::string& ns : outer) {
                    const std::string candidate = ns + "::" + name;
                    bool known = false;
                    for (const FuncDecl& fd : funcs)
                        if (fd.name == candidate) known = true;
                    if (known) {
                        name = candidate;
                        break;
                    }
                }
            }
        }
        bool from_ns_call = false;  // okay::Clamp(...): the namespace's function, not a member of the same name
        if (name.find("::") != std::string::npos && (is(TLParen) || is(TLt))) {
            // std::min(a, b): a function template of a namespace is registered by its bare name.
            const size_t first = name.find("::");
            const size_t last = name.rfind("::");
            const std::string suffix = name.substr(last + 2);
            if (namespaces.count(name.substr(0, first)) > 0 && func_templates.count(suffix) > 0) {
                name = suffix;
                from_ns_call = true;
            }
        }
        if (name.find("::") != std::string::npos && (is(TLParen) || is(TLBrace))) {
            // std::string("abc"): a class of a namespace is registered by its bare name, as in a declaration.
            const size_t first = name.find("::");
            const std::string suffix = name.substr(name.rfind("::") + 2);
            bool is_function = false;
            for (const FuncDecl& fd : funcs)
                if (fd.name == name) is_function = true;
            if (!is_function && namespaces.count(name.substr(0, first)) > 0 && scope_class(name.substr(0, first)) < 0 &&
                (class_name_text(suffix) || (typedefs.count(suffix) > 0 && typedefs[suffix].base == 3)))
                name = suffix;
        }
        if (name.find("::") != std::string::npos && (is(TLBrace) || is(TLParen))) {
            // Outer::Nested{...} and Outer::Nested(...): a nested class named with its outer class is the class itself.
            const size_t last = name.rfind("::");
            int oid = scope_class(name.substr(0, last));
            if (oid < 0 && typedefs.find(name.substr(0, last)) == typedefs.end()) oid = find_struct(name.substr(0, last));
            const std::string member = name.substr(last + 2);
            if (oid >= 0) {
                int nested = find_nested(oid, member);
                if (nested >= 0 && structs[static_cast<size_t>(nested)].name == member) {
                    bool clash = structs[static_cast<size_t>(oid)].statics.count(member) > 0;
                    for (const Method& hm : structs[static_cast<size_t>(oid)].methods)
                        if (hm.name == member) clash = true;
                    if (!clash) name = member;
                }
            }
        }
        const size_t colons = name.find("::");
        if (building != nullptr && colons == std::string::npos && !is(TLParen) && !local_visible(name)) {
            const auto found = building->statics.find(name);
            if (found != building->statics.end() && found->second.has_const) {
                const_node(found->second, out);
                return parse_postfix(out);
            }
        }
        if (scope_sid < 0 && colons != std::string::npos) scope_sid = scope_class(name.substr(0, colons));
        if (scope_sid < 0 && colons != std::string::npos && !is(TLParen)) {
            // Plain::Enumerator for a class without member functions: its enumerators are global names here.
            const std::string prefix = name.substr(0, colons);
            const std::string suffix = name.substr(name.rfind("::") + 2);
            auto ev = enum_values.find(suffix);
            if (ev == enum_values.end()) {
                // `using S = Shape; S::Capsule`: the alias keeps no enumeration name, so the scoped enumerator Shape::Capsule is
                // found by its last part when exactly one enumeration has it.
                const auto atd = typedefs.find(prefix);
                if (atd != typedefs.end() && atd->second.base == 1 && atd->second.pointers == 0) {
                    int hits = 0;
                    auto found = enum_values.end();
                    for (auto it = enum_values.begin(); it != enum_values.end(); ++it) {
                        const std::string& key = it->first;
                        if (key.size() > suffix.size() + 2 && key.compare(key.size() - suffix.size() - 2, std::string::npos, "::" + suffix) == 0) {
                            ++hits;
                            found = it;
                        }
                    }
                    if (hits == 1) ev = found;
                }
            }
            const auto ptd = typedefs.find(prefix);
            const bool enum_alias = ptd != typedefs.end() && ptd->second.base == 1 && ptd->second.pointers == 0;
            if ((find_struct(prefix) >= 0 || enum_alias) && ev != enum_values.end()) {
                out->kind = Node::IntLit;
                out->number = ev->second;
                out->type = Type{};
                out->type.base = 1;
                return parse_postfix(out);
            }
        }
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
        if (cur_class >= 0 && structs[static_cast<size_t>(cur_class)].this_class >= 0 && !local_visible(name) &&
            colons == std::string::npos) {
            // In a lambda that captured `this`: a member of the enclosing class is reached through `this->__this`.
            const int ec = structs[static_cast<size_t>(cur_class)].this_class;
            const StructDef& edef = structs[static_cast<size_t>(ec)];
            bool e_field = false;
            bool e_method = false;
            for (const Field& field : edef.fields)
                if (field.name == name) e_field = true;
            for (const Method& m : edef.methods)
                if (m.name == name) e_method = true;
            Node outer_this;
            outer_this.kind = Node::Field;
            outer_this.op = TArrow;
            outer_this.text = "__this";
            outer_this.kids.push_back(node_name("this"));
            if (e_field && !is(TLParen)) {
                out->kind = Node::Field;
                out->op = TArrow;
                out->text = name;
                out->kids.push_back(std::move(outer_this));
                for (const Field& field : edef.fields)
                    if (field.name == name && field.ref) {
                        Node field_node = std::move(*out);
                        *out = Node{};
                        out->kind = Node::Unary;
                        out->op = TStar;
                        out->kids.push_back(std::move(field_node));
                    }
                return parse_postfix(out);
            }
            if (is(TLParen) && e_method) {
                bool any_object_fn = false;
                for (const Method& m : edef.methods)
                    if (m.name == name && !m.is_static) any_object_fn = true;
                if (!any_object_fn) return qualified_call(out, ec, name, {}) && parse_postfix(out);
                next();
                return method_call(out, std::move(outer_this), true, name) && parse_postfix(out);
            }
            if (!is(TLParen) && static_member_expr(ec, name, out)) return parse_postfix(out);
        }
        if (cur_class >= 0 && !local_visible(name) && colons == std::string::npos && !from_ns_call) {
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
                bool ref_field = false;
                for (const Field& field : def.fields)
                    if (field.name == name && field.ref) ref_field = true;
                if (def.ref_caps.count(name) > 0 || ref_field) {
                    // A variable a lambda captured by reference: the field holds its address.
                    Node field = std::move(*out);
                    *out = Node{};
                    out->kind = Node::Unary;
                    out->op = TStar;
                    out->kids.push_back(std::move(field));
                }
                return parse_postfix(out);
            }
            if (is(TLt)) {
                // f<int>(args) inside a member function, naming a member function template of the class.
                int mt_owner = -1;
                const MemberTmpl* mt = find_member_tmpl(cur_class, name, &mt_owner);
                if (mt != nullptr) {
                    std::vector<TArg> mtargs;
                    if (!parse_template_args(mt->td, &mtargs, true)) return false;
                    bool any_object_fn = false;
                    for (const Method& m : def.methods)
                        if (m.name == name && !m.is_static) any_object_fn = true;
                    if (mt->is_static || (!any_object_fn && mt->is_static)) return qualified_call(out, cur_class, name, std::move(mtargs)) && parse_postfix(out);
                    if (!expect(TLParen, "'('")) return false;
                    Node self2;
                    self2.kind = Node::Name;
                    self2.text = "this";
                    return method_call(out, std::move(self2), true, name, std::move(mtargs)) && parse_postfix(out);
                }
            }
            if (is(TLParen) && !def.methods.empty()) {
                bool is_method = false;
                for (const Method& m : def.methods)
                    if (m.name == name) is_method = true;
                int tmpl_owner = -1;
                if (find_member_tmpl(cur_class, name, &tmpl_owner) != nullptr) is_method = true;
                bool any_object_fn = false;
                for (const Method& m : def.methods)
                    if (m.name == name && !m.is_static) any_object_fn = true;
                if (is_method && !any_object_fn && find_member_tmpl(cur_class, name, &tmpl_owner) == nullptr) {
                    // Only static functions have this name: there is no object to call it on.
                    return qualified_call(out, cur_class, name, {}) && parse_postfix(out);
                }
                if (is_method) {
                    next();
                    return method_call(out, std::move(self), true, name) && parse_postfix(out);
                }
            }
            // A static member or static function of an enclosing class, named from a nested class.
            for (int outer = def.outer; outer >= 0; outer = structs[static_cast<size_t>(outer)].outer) {
                const StructDef& od = structs[static_cast<size_t>(outer)];
                if (!is(TLParen) && static_member_expr(outer, name, out)) return parse_postfix(out);
                if (is(TLParen)) {
                    bool only_static = false;
                    for (const Method& m : od.methods)
                        if (m.name == name) only_static = true;
                    for (const Method& m : od.methods)
                        if (m.name == name && !m.is_static) only_static = false;
                    if (only_static) return qualified_call(out, outer, name, {}) && parse_postfix(out);
                }
            }
        }
        if (is(TLBrace) && colons == std::string::npos && !local_visible(name) && cur_locals != nullptr) {
            // C{args}: a temporary object of a class made from a braced list ([expr.type.conv], [dcl.init.list]):
            // by a constructor, or member by member for an aggregate.
            int bsid = -1;
            auto btd = typedefs.find(name);
            if (btd != typedefs.end()) {
                if (btd->second.base == 3 && btd->second.pointers == 0 && btd->second.struct_id >= 0)
                    bsid = btd->second.struct_id;
            } else if (class_name_text(name) || find_struct(name) >= 0) {
                bsid = find_struct(name);
            }
            bool named_function = false;
            for (const FuncDecl& known : funcs)
                if (known.name == name) named_function = true;
            if (bsid >= 0 && !named_function) {
                if (!braced_temp(bsid, out)) return false;
                return parse_postfix(out);
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
            } else if (class_name_text(name) || find_struct(name) >= 0) {
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
                if (!class_needs_ctor(structs[static_cast<size_t>(csid)]) && cargs.size() <= 1) {
                    // P() and P(other) for a class without constructors: zero, or a copy ([dcl.init]).
                    if (cargs.empty()) {
                        if (!lower_aggr(ct, node_name(hidden.name), nullptr, &stmt_pre)) return false;
                    } else {
                        Node copy;
                        copy.kind = Node::Assign;
                        copy.op = TAssign;
                        copy.number = 1;
                        copy.kids.push_back(node_name(hidden.name));
                        copy.kids.push_back(std::move(cargs[0]));
                        Node copy_stmt;
                        copy_stmt.kind = Node::ExprStmt;
                        copy_stmt.kids.push_back(std::move(copy));
                        stmt_pre.push_back(std::move(copy_stmt));
                    }
                    *out = node_name(hidden.name);
                    return parse_postfix(out);
                }
                Node ctor_stmt;
                if (!special_call(csid, "<ctor>", node_addr(node_name(hidden.name)), std::move(cargs), &ctor_stmt))
                    return false;
                stmt_pre.push_back(std::move(ctor_stmt));
                if (class_needs_dtor(structs[static_cast<size_t>(csid)])) stmt_temps.push_back(Temp{hidden.name, csid});
                *out = node_name(hidden.name);
                return parse_postfix(out);
            }
        }
        if (func_templates.count(name) > 0 && (is(TLt) || is(TLParen)) && !local_visible(name)) {
            std::vector<TArg> targs;
            if (is(TLt) && !parse_template_args(func_templates[name], &targs, true)) return false;
            if (!expect(TLParen, "'('")) return false;
            std::vector<Node> cargs;
            if (!parse_call_args(&cargs)) return false;
            std::string inst;
            bool braced_overload = false;
            for (const Node& a : cargs)
                if (a.kind == Node::Block && a.text == "tokbrace") {
                    // A braced list argument (`std::max({a, b, c})`): the type cannot be deduced here, so an ordinary
                    // overload of the same name (taking an initializer_list) is chosen when there is one.
                    inst = pick_free(name, cargs);
                    if (inst.empty()) inst = pick_any_ns(name, cargs);
                    if (!inst.empty()) braced_overload = true;
                    if (braced_overload) {
                        // Choose by the type of the first element of the list ([over.match.list], simplified): read it once
                        // as an expression and take the overload whose initializer_list has that element type.
                        const size_t keep = at;
                        const size_t pre_n = stmt_pre.size();
                        const size_t tmp_n = stmt_temps.size();
                        at = static_cast<size_t>(a.number) + 1;
                        Node first;
                        Type ft;
                        const bool got = parse_expr(1, &first) && static_type(first, &ft);
                        at = keep;
                        stmt_pre.resize(pre_n);
                        stmt_temps.resize(tmp_n);
                        error.clear();
                        if (got) {
                            auto list_it = ovl.find(name);
                            if (list_it == ovl.end())
                                for (auto it = ovl.begin(); it != ovl.end(); ++it)
                                    if (it->first.size() >= name.size() &&
                                        it->first.compare(it->first.size() - name.size(), name.size(), name) == 0)
                                        list_it = it;
                            if (list_it != ovl.end())
                                for (const OvFn& cand : list_it->second)
                                    if (cand.params.size() == 1 && cand.params[0].base == 3 && cand.params[0].struct_id >= 0) {
                                        const StructDef& isd = structs[static_cast<size_t>(cand.params[0].struct_id)];
                                        if (!isd.targs.empty() && !isd.targs[0].is_value && isd.targs[0].type.base == ft.base &&
                                            isd.targs[0].type.pointers == ft.pointers)
                                            inst = cand.internal;
                                    }
                        }
                    }
                }
            if (!braced_overload && !instantiate_function(name, std::move(targs), cargs, &inst)) {
                // A template that cannot be deduced from these arguments is not a candidate: an ordinary function of the
                // same name ([over.match.funcs]) takes the call when one fits.
                std::string ordinary = error.compare(0, 6, "cannot") == 0 ? pick_free(name, cargs) : std::string();
                if (ordinary.empty() && error.compare(0, 6, "cannot") == 0) ordinary = pick_any_ns(name, cargs);
                if (ordinary.empty()) return false;
                error.clear();
                inst = ordinary;
            }
            out->kind = Node::Call;
            out->text = inst;
            out->kids = std::move(cargs);
            if (!pad_func_defaults(inst, &out->kids)) return false;
            for (const FuncDecl& known : funcs) {
                if (known.name != inst) continue;
                std::vector<Type> source = known.params;
                for (size_t i = 0; i < source.size() && i < known.param_ref.size(); ++i)
                    if (known.param_ref[i]) {
                        source[i].pointers -= 1;
                        source[i].is_ref = true;
                    }
                if (!adjust_args(source, &out->kids)) return false;
                if (known.ret_ref) *out = reference_result(std::move(*out), true, known.ret.rref);
                break;
            }
            return parse_postfix(out);
        }
        bool var_object = false;
        if (is(TLParen) && colons == std::string::npos) {
            // obj(args) where obj is a variable of a class: its operator() (read in parse_postfix).
            Type vt;
            Node probe;
            probe.kind = Node::Name;
            probe.text = name;
            var_object = static_type(probe, &vt) && vt.base == 3 && vt.pointers == 0 && !vt.is_array() && vt.struct_id >= 0 &&
                         (local_visible(name) || [&]() {
                             for (const FuncDecl& fd : funcs)
                                 if (fd.name == name) return false;
                             return true;
                         }());
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
        } else if (is(TLParen) && !local_visible(name) && functional_cast_type(name).base >= 0) {
            // T(x) for a type name T of a scalar type is the cast (T)(x) ([expr.type.conv]).
            Type ty = functional_cast_type(name);
            next();
            Node inner;
            if (is(TRParen)) {
                // T(): a value-initialised scalar is zero ([expr.type.conv]).
                next();
                inner.kind = Node::IntLit;
                inner.type.base = 1;
            } else {
                if (!parse_expr(0, &inner)) return false;
                if (!expect(TRParen, "')'")) return false;
            }
            if (!convert_class(&inner, ty)) return false;
            if (inner.kind == Node::IntLit && ty.pointers == 0 && ty.base == 1 && !ty.is_bool) {
                inner.number = const_norm(inner.number, ty);
                inner.type = Type{};
                inner.type.base = 1;
                inner.type.wide = ty.wide;
                inner.type.is_unsigned = ty.is_unsigned;
                *out = std::move(inner);
                return parse_postfix(out);
            }
            out->kind = Node::Unary;
            out->type = ty;
            out->kids.push_back(std::move(inner));
        } else if (!var_object && accept(TLParen)) {
            out->kind = Node::Call;
            out->text = name;
            if (!parse_call_args(&out->kids)) return false;
            if (!local_visible(name)) {
                const std::string picked = pick_free(name, out->kids);
                if (!picked.empty()) {
                    name = picked;
                    out->text = picked;
                }
            }
            if (!pad_func_defaults(name, &out->kids)) return false;
            if (local_visible(name)) {
                // A call through a variable that holds a function pointer (a captureless lambda): a parameter the function
                // takes by reference gets the address of its argument, a temporary going into a hidden local first.
                Node callee;
                callee.kind = Node::Name;
                callee.text = name;
                Type ct;
                if (static_type(callee, &ct) && ct.func_id >= 0 && ct.func_id < static_cast<int>(func_types.size())) {
                    const FuncType& ft = func_types[static_cast<size_t>(ct.func_id)];
                    std::vector<Type> source = ft.params;
                    bool any_ref = false;
                    for (size_t i = 0; i < source.size(); ++i) {
                        if (i < ft.param_ref.size() && ft.param_ref[i]) {
                            source[i].pointers -= 1;
                            source[i].is_ref = true;
                        }
                        if (source[i].is_ref || (source[i].base == 3 && source[i].pointers == 0 && source[i].struct_id >= 0)) any_ref = true;
                    }
                    if (any_ref && !adjust_args(source, &out->kids)) return false;
                }
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
                if (known.ret_ref) *out = reference_result(std::move(*out), true, known.ret.rref);
                break;
            }
        } else if (enum_values.find(name) != enum_values.end()) {
            out->kind = Node::IntLit;
            out->number = enum_values[name];
            out->type.base = 1;
        } else if (const_ctx && !no_const_subst && !is_assign_op(cur().kind) && !is(TPlusPlus) && !is(TMinusMinus) &&
                   local_const_value(name, &out->number)) {
            out->kind = Node::IntLit;
            out->type = Type{};
            out->type.base = 1;
        } else if (!no_const_subst && !local_visible(name) && !is_assign_op(cur().kind) && !is(TPlusPlus) &&
                   !is(TMinusMinus) && global_const_node(name, out)) {
            // A const global with a constant initializer is a constant ([expr.const]).
        } else {
            out->kind = Node::Name;
            out->text = name;
        }
    } else if (is(TLParen)) {
        const Tok ahead = at + 1 < tokens.size() ? tokens[at + 1].kind : TEnd;
        const bool type_ahead = ahead == TInt || ahead == TConst || ahead == TChar || ahead == TVoid || ahead == TShort || ahead == TUnsigned ||
                                ahead == TSigned || ahead == TLong || ahead == TFloat || ahead == TDouble || ahead == TStruct ||
                                ahead == TUnion || ahead == TEnum ||
                                (ahead == TIdent && typedefs.find(tokens[at + 1].text) != typedefs.end() && !qualified_value_at(at + 1) &&
                                 !variable_named(tokens[at + 1].text)) ||
                                (ahead == TIdent && class_name_at(at + 1) && !qualified_value_at(at + 1)) ||
                                (ahead == TIdent && is_template_name_at(at + 1) && template_cast_close_ahead(at + 2));  // (vector<T*>*)p
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
            if (ty.base == 0 && ty.pointers == 0 && inner.kind == Node::Call && cur_locals != nullptr) {
                // (void)f(): a call whose class result does not fit a register still needs somewhere to put it.
                Type st;
                if (static_type(inner, &st) && st.base == 3 && st.pointers == 0 && !st.is_ref && !st.is_array() &&
                    st.struct_id >= 0 && !class_needs_dtor(structs[static_cast<size_t>(st.struct_id)])) {
                    const int sz = size_of(st);
                    if (sz != 1 && sz != 2 && sz != 4 && sz != 8) {
                        Local hidden;
                        hidden.name = "__dv" + std::to_string(tmp_count++);
                        hidden.type = st;
                        place_local(cur_locals, hidden);
                        Node assign;
                        assign.kind = Node::Assign;
                        assign.op = TAssign;
                        assign.number = 1;
                        assign.kids.push_back(node_name(hidden.name));
                        assign.kids.push_back(std::move(inner));
                        inner = std::move(assign);
                    }
                }
            }
            if (ty.base != 3 && ty.base != 0 && !ty.is_array() && !convert_class(&inner, ty)) return false;
            if (inner.kind == Node::IntLit && ty.pointers == 0 && !ty.is_array() && !ty.is_ref && ty.base >= 5 &&
                ty.base <= 6) {
                // (float)3 and (double)1.5f are constants: the literal is converted at compile time.
                uint64_t bits = 0;
                if (const_convert(inner, ty, &bits)) {
                    out->kind = Node::IntLit;
                    out->number = bits;
                    out->type = Type{};
                    out->type.base = ty.base;
                    return parse_postfix(out);
                }
            }
            if (inner.kind == Node::IntLit && inner.type.base >= 5 && ty.pointers == 0 && !ty.is_array() && !ty.is_ref &&
                (ty.base == 1 || ty.base == 2 || ty.base == 4) && !ty.is_bool) {
                // (int)2.5f is a constant: truncated toward zero.
                uint64_t bits = 0;
                if (const_convert(inner, ty, &bits)) {
                    Type nt = Type{};
                    nt.base = 1;
                    if (ty.base == 1) {
                        nt.wide = ty.wide;
                        nt.is_unsigned = ty.is_unsigned;
                    }
                    uint64_t v = bits;
                    if (ty.base == 2) v = ty.is_unsigned ? (v & 0xFFu) : static_cast<uint64_t>(static_cast<int64_t>(static_cast<int8_t>(v)));
                    else if (ty.base == 4) v = ty.is_unsigned ? (v & 0xFFFFu) : static_cast<uint64_t>(static_cast<int64_t>(static_cast<int16_t>(v)));
                    else v = const_norm(v, nt);
                    out->kind = Node::IntLit;
                    out->number = v;
                    out->type = nt;
                    return parse_postfix(out);
                }
            }
            if (inner.kind == Node::IntLit && inner.type.base < 5 && ty.pointers == 0 && !ty.is_array() && !ty.is_ref &&
                (ty.base == 1 || ty.base == 2 || ty.base == 4) && ty.bits < 0) {
                // A cast of a constant to an integer type is a constant ([expr.const]): the value is
                // cut to the width of the type and, for a signed type, extended again.
                uint64_t v = inner.number;
                if (ty.is_bool) v = v != 0 ? 1 : 0;
                else if (ty.base == 2) v = ty.is_unsigned ? (v & 0xFFu) : static_cast<uint64_t>(static_cast<int64_t>(static_cast<int8_t>(v)));
                else if (ty.base == 4) v = ty.is_unsigned ? (v & 0xFFFFu) : static_cast<uint64_t>(static_cast<int64_t>(static_cast<int16_t>(v)));
                else if (!ty.wide) v = ty.is_unsigned ? (v & 0xFFFFFFFFu) : static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(v)));
                out->kind = Node::IntLit;
                out->number = v;
                out->type = Type{};
                out->type.base = 1;
                if (ty.base == 1) {
                    out->type.wide = ty.wide;
                    out->type.is_unsigned = ty.is_unsigned;
                }
                return parse_postfix(out);
            }
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
    } else if (is(TPlus)) {
        // Unary plus ([expr.unary.op]/7): the value of the operand (after the usual conversions, which the use does).
        next();
        return parse_unary(out);
    } else if (is(TAmp) && at + 3 < tokens.size() && tokens[at + 1].kind == TIdent && tokens[at + 2].kind == TColonColon &&
               tokens[at + 3].kind == TIdent && scope_class(tokens[at + 1].text) >= 0 &&
               !(at + 4 < tokens.size() && tokens[at + 4].kind == TLParen) && member_for_pointer(tokens[at + 1].text, tokens[at + 3].text)) {
        // &C::f ([expr.unary.op]/4): a pointer to the member function f of C.
        const int cls = scope_class(tokens[at + 1].text);
        const std::string mname = tokens[at + 3].text;
        next();
        next();
        next();
        next();
        const Method* mm = nullptr;
        for (const Method& cand : structs[static_cast<size_t>(cls)].methods)
            if (cand.name == mname && !cand.is_static) {
                mm = &cand;
                break;
            }
        FuncDecl fd;
        fd.ret = mm->ret;
        Type self;
        self.base = 3;
        self.struct_id = cls;
        self.pointers = 1;
        fd.params.push_back(self);
        for (const Type& pt : mm->params) fd.params.push_back(pt);
        finish_signature(&fd);
        Type pmt = mm->ret;
        pmt.pointers = 1;
        pmt.array_len = -1;
        pmt.is_ref = false;
        pmt.func_id = fd.func_id;
        if (mm->is_virtual && mm->slot >= 0) {
            Node lit;
            lit.kind = Node::IntLit;
            lit.number = static_cast<uint64_t>(mm->slot) * 8 + 1;
            lit.type.base = 1;
            lit.type.wide = true;
            *out = cast_node(pmt, std::move(lit));
        } else {
            *out = cast_node(pmt, node_name(mm->qname));
        }
        return parse_postfix(out);
    } else if (is(TStar) || is(TAmp) || is(TMinus) || is(TNot) || is(TTilde)) {
        out->kind = Node::Unary;
        out->op = cur().kind;
        next();
        Node inner;
        const bool saved_subst = no_const_subst;
        if (out->op == TAmp) no_const_subst = true;
        const bool inner_ok = parse_unary(&inner);
        no_const_subst = saved_subst;
        if (!inner_ok) return false;
        const Tok uop = out->op;
        if (uop == TMinus || uop == TNot || uop == TTilde || uop == TStar) {
            bool done = false;
            if (!operator_unary(uop, false, &inner, &done)) return false;
            if (done) {
                *out = std::move(inner);
                return parse_postfix(out);
            }
        }
        if ((uop == TMinus || uop == TTilde || uop == TNot) && inner.kind == Node::IntLit && inner.type.base < 5) {
            // A constant operand is folded, so -16, ~0 and !1 can be used where a constant is needed.
            const uint64_t v = inner.number;
            out->kind = Node::IntLit;
            out->op = TEnd;
            out->type = Type{};
            out->type.base = 1;
            if (uop == TNot) out->number = v == 0 ? 1 : 0;
            else {
                out->type.wide = inner.type.wide;
                out->type.is_unsigned = inner.type.is_unsigned;
                out->number = const_norm(uop == TMinus ? 0 - v : ~v, out->type);
            }
            return parse_postfix(out);
        }
        if ((uop == TMinus || uop == TNot) && inner.kind == Node::IntLit && inner.type.base >= 5) {
            // -1.5f and !0.0 are constants too: the sign bit flips, or the value is compared with zero.
            const int fbase = inner.type.base;
            const uint64_t v = inner.number;
            out->kind = Node::IntLit;
            out->op = TEnd;
            out->type = Type{};
            if (uop == TMinus) {
                out->type.base = fbase;
                out->number = fbase == 5 ? ((v ^ 0x80000000ull) & 0xFFFFFFFFull) : (v ^ 0x8000000000000000ull);
            } else {
                out->type.base = 1;
                out->number = lit_double(inner) == 0 ? 1 : 0;
            }
            return parse_postfix(out);
        }
        out->kids.push_back(std::move(inner));
    } else if ((is(TInt) || is(TFloat) || is(TDouble) || is(TChar) || is(TShort) || is(TUnsigned) || is(TSigned) ||
                is(TLong)) &&
               [&]() {
                   // int(x), unsigned(x), float(x): a type name of keywords written like a call.
                   size_t k = at;
                   while (k < tokens.size() && (tokens[k].kind == TInt || tokens[k].kind == TFloat || tokens[k].kind == TDouble ||
                                                tokens[k].kind == TChar || tokens[k].kind == TShort ||
                                                tokens[k].kind == TUnsigned || tokens[k].kind == TSigned ||
                                                tokens[k].kind == TLong))
                       ++k;
                   return k < tokens.size() && tokens[k].kind == TLParen;
               }()) {
        Type ty;
        bool imp = false;
        bool exp = false;
        if (!parse_type(&ty, &imp, &exp)) return false;
        if (!expect(TLParen, "'('")) return false;
        Node inner;
        if (!parse_expr(0, &inner)) return false;
        if (!expect(TRParen, "')'")) return false;
        if (!convert_class(&inner, ty)) return false;
        if (inner.kind == Node::IntLit && inner.type.base < 5 && ty.base == 1) {
            inner.number = const_norm(inner.number, ty);
            inner.type = Type{};
            inner.type.base = 1;
            inner.type.wide = ty.wide;
            inner.type.is_unsigned = ty.is_unsigned;
            *out = std::move(inner);
            return parse_postfix(out);
        }
        out->kind = Node::Unary;
        out->type = ty;
        out->kids.push_back(std::move(inner));
    } else if (is(TLBrack)) {
        if (!parse_lambda(out)) return false;
    } else {
        error = "expected an expression" + (current_fn.empty() ? std::string() : " (in " + current_fn + ", after kinds " + (at >= 3 ? std::to_string((int)tokens[at - 3].kind) + " " + tokens[at - 3].text + "/" + std::to_string((int)tokens[at - 2].kind) + " " + tokens[at - 2].text + "/" + std::to_string((int)tokens[at - 1].kind) + " " + tokens[at - 1].text : std::string()) + ", line " + std::to_string(cur().line) + ", token " + (is(TEnd) ? std::string("end") : cur().text) + ")");
        return false;
    }
    return parse_postfix(out);
}

bool Parser::parse_postfix(Node* out) {
    while (is(TLBrack) || is(TDot) || is(TArrow) || is(TLParen) || is(TPlusPlus) || is(TMinusMinus)) {
        if (is(TPlusPlus) || is(TMinusMinus)) {
            {
                bool done = false;
                Node operand = std::move(*out);
                if (!operator_unary(cur().kind, true, &operand, &done)) return false;
                *out = std::move(operand);
                if (done) {
                    next();
                    continue;
                }
            }
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
            {
                Node probe;
                Type ct;
                if (!(out->kind == Node::Binary && out->text == "->*") && class_object_type(*out, &ct)) {
                    std::vector<Node> cargs;
                    if (!parse_call_args(&cargs)) return false;
                    bool done = false;
                    if (!operator_call(out, std::move(cargs), &done)) return false;
                    if (!done) {
                        error = "no operator() in the class";
                        return false;
                    }
                    continue;
                }
            }
            if (out->kind == Node::Binary && out->text == "->*" && cur_locals != nullptr) {
                // (obj->*pm)(args): the object and the pointer go into hidden locals, then a call through the function the
                // pointer stands for: its own address, or for an odd value (virtual) the table entry numbered value / 8.
                Node obj = std::move(out->kids[0]);
                Node pm = std::move(out->kids[1]);
                const bool arrow = out->op == TArrow;
                Type pmt;
                Type objt;
                if (!static_type(pm, &pmt) || pmt.func_id < 0 || !static_type(obj, &objt) || objt.base != 3) {
                    error = "cannot tell the type of the member pointer or the object";
                    return false;
                }
                Type self_ptr = objt;
                if (arrow) self_ptr.pointers -= 1;
                self_ptr.pointers = 1;
                self_ptr.is_ref = false;
                Local lo;
                lo.name = "__po" + std::to_string(tmp_count++);
                lo.type = self_ptr;
                place_local(cur_locals, lo);
                Local lf;
                lf.name = "__pf" + std::to_string(tmp_count++);
                lf.type = pmt;
                place_local(cur_locals, lf);
                auto assign_pre = [&](const std::string& name, Node value) {
                    Node as;
                    as.kind = Node::Assign;
                    as.op = TAssign;
                    as.number = 1;
                    as.kids.push_back(node_name(name));
                    as.kids.push_back(std::move(value));
                    Node st;
                    st.kind = Node::ExprStmt;
                    st.kids.push_back(std::move(as));
                    stmt_pre.push_back(std::move(st));
                };
                assign_pre(lo.name, arrow ? std::move(obj) : node_addr(std::move(obj)));
                assign_pre(lf.name, std::move(pm));
                // odd: ((fn)table[value >> 3]); even: ((fn)value)
                Node odd_test;
                odd_test.kind = Node::Binary;
                odd_test.op = TAmp;
                odd_test.kids.push_back(node_name(lf.name));
                Node one;
                one.kind = Node::IntLit;
                one.number = 1;
                one.type.base = 1;
                one.type.wide = true;
                odd_test.kids.push_back(std::move(one));
                Type ulong_t;
                ulong_t.base = 1;
                ulong_t.wide = true;
                Node as_long = cast_node(ulong_t, node_name(lf.name));
                odd_test.kids[0] = std::move(as_long);
                Node shift;
                shift.kind = Node::Binary;
                shift.op = TShr;
                shift.kids.push_back(cast_node(ulong_t, node_name(lf.name)));
                Node three;
                three.kind = Node::IntLit;
                three.number = 3;
                three.type.base = 1;
                shift.kids.push_back(std::move(three));
                Node vptr;
                vptr.kind = Node::Field;
                vptr.op = TArrow;
                vptr.text = "__vptr";
                vptr.kids.push_back(node_name(lo.name));
                Node entry;
                entry.kind = Node::Index;
                entry.kids.push_back(std::move(vptr));
                entry.kids.push_back(std::move(shift));
                Node tern;
                tern.kind = Node::Ternary;
                tern.kids.push_back(std::move(odd_test));
                tern.kids.push_back(cast_node(pmt, std::move(entry)));
                tern.kids.push_back(cast_node(pmt, node_name(lf.name)));
                Node call;
                call.kind = Node::Call;
                call.kids.push_back(cast_node(pmt, std::move(tern)));
                call.kids.push_back(node_name(lo.name));
                std::vector<Node> pargs;
                if (!parse_call_args(&pargs)) return false;
                const FuncType& ft = func_types[static_cast<size_t>(pmt.func_id)];
                std::vector<Type> source;
                for (size_t i = 1; i < ft.params.size(); ++i) {
                    Type pt = ft.params[i];
                    if (i < ft.param_ref.size() && ft.param_ref[i]) {
                        pt.pointers -= 1;
                        pt.is_ref = true;
                    }
                    source.push_back(pt);
                }
                if (!adjust_args(source, &pargs)) return false;
                for (Node& a2 : pargs) call.kids.push_back(std::move(a2));
                *out = std::move(call);
                continue;
            }
            Node call;
            call.kind = Node::Call;
            Type callee_type;
            const bool callee_known = static_type(*out, &callee_type);
            call.kids.push_back(std::move(*out));
            {
                std::vector<Node> pargs;  // read like any call: a pack `a...` is expanded here too
                if (!parse_call_args(&pargs)) return false;
                if (callee_known && callee_type.func_id >= 0 && callee_type.func_id < static_cast<int>(func_types.size())) {
                    // A call through a function pointer (a captureless lambda reached through a capture): a parameter the
                    // function takes by reference gets the address of its argument, a temporary going into a hidden local.
                    const FuncType& ft = func_types[static_cast<size_t>(callee_type.func_id)];
                    std::vector<Type> source = ft.params;
                    bool any_ref = false;
                    for (size_t i = 0; i < source.size(); ++i) {
                        if (i < ft.param_ref.size() && ft.param_ref[i]) {
                            source[i].pointers -= 1;
                            source[i].is_ref = true;
                        }
                        if (source[i].is_ref || (source[i].base == 3 && source[i].pointers == 0 && source[i].struct_id >= 0)) any_ref = true;
                    }
                    if (any_ref && !adjust_args(source, &pargs)) return false;
                }
                for (Node& a : pargs) call.kids.push_back(std::move(a));
            }
            *out = std::move(call);
            continue;
        }
        if (is(TLBrack)) {
            next();
            Node index;
            if (is(TLBrace)) {
                // m[{a, b}]: a braced list as the index; what it makes depends on the operator[] parameter, so its position
                // is kept (like a braced call argument) and read when the parameter is known.
                index.kind = Node::Block;
                index.text = "tokbrace";
                index.number = at;
                int depth = 0;
                do {
                    if (is(TLBrace)) ++depth;
                    else if (is(TRBrace)) --depth;
                    else if (is(TEnd)) {
                        error = "expected '}'";
                        return false;
                    }
                    next();
                } while (depth > 0);
            } else if (!parse_expr(0, &index)) {
                return false;
            }
            if (!expect(TRBrack, "']'")) return false;
            {
                bool done = false;
                Node probe;
                Type ct;
                if (class_object_type(*out, &ct)) {
                    if (!operator_index(out, std::move(index), &done)) return false;
                    continue;
                }
            }
            Node wrapped;
            wrapped.kind = Node::Index;
            wrapped.kids.push_back(std::move(*out));
            wrapped.kids.push_back(std::move(index));
            *out = std::move(wrapped);
        } else {
            const Tok op = cur().kind;
            next();
            if ((op == TDot || op == TArrow) && is(TStar)) {
                // obj.*pm and obj->*pm ([expr.mptr.oper]): the call parenthesis after it is read next.
                next();
                Node pm;
                if (!parse_unary(&pm)) return false;
                Node bound;
                bound.kind = Node::Binary;
                bound.op = op;
                bound.text = "->*";
                bound.kids.push_back(std::move(*out));
                bound.kids.push_back(std::move(pm));
                *out = std::move(bound);
                continue;
            }
            if (op == TArrow) {
                // obj->member for a class object with operator->: its result (a pointer) is what -> then applies to.
                Type object_type;
                if (class_object_type(*out, &object_type) && find_method(object_type.struct_id, "operator->", 0) != nullptr) {
                    Node result;
                    std::vector<Node> no_args;
                    if (!method_call(&result, std::move(*out), false, "operator->", {}, &no_args)) return false;
                    *out = std::move(result);
                }
            }
            // s.template add<int>(1): 'template' only says that a '<' after the name opens arguments.
            if (is(TIdent) && cur().text == "template" && at + 1 < tokens.size() && tokens[at + 1].kind == TIdent) next();
            if (is(TTilde) && at + 1 < tokens.size() && tokens[at + 1].kind == TIdent) {
                // p->~T() and o.~T(): an explicit destructor call ([class.dtor]); for a type without a destructor
                // (a scalar, a plain struct) it does nothing ([expr.prim.id.dtor]).
                next();
                next();
                if (!expect(TLParen, "'('") || !expect(TRParen, "')'")) return false;
                Type ot;
                Node call_expr;
                call_expr.kind = Node::IntLit;
                call_expr.type.base = 1;
                if (static_type(*out, &ot) && ot.base == 3 && ot.struct_id >= 0 && ot.pointers == (op == TArrow ? 1 : 0) &&
                    class_needs_dtor(structs[static_cast<size_t>(ot.struct_id)])) {
                    Node self = op == TArrow ? std::move(*out) : node_addr(std::move(*out));
                    Node stmt;
                    if (!special_call(ot.struct_id, "<dtor>", std::move(self), {}, &stmt)) return false;
                    call_expr = std::move(stmt.kids[0]);
                }
                *out = std::move(call_expr);
                continue;
            }
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
            {
                // f().x: the object a call returns is not in memory yet; it goes into a hidden local first
                // ([expr.ref], [class.temporary]) and is destroyed after the statement.
                Type call_type;
                if (op == TDot && out->kind == Node::Call && cur_locals != nullptr && static_type(*out, &call_type) &&
                    call_type.base == 3 && call_type.pointers == 0 && !call_type.is_array() && call_type.struct_id >= 0) {
                    Local hidden;
                    hidden.name = "__bt" + std::to_string(tmp_count++);
                    hidden.type = call_type;
                    place_local(cur_locals, hidden);
                    Node assign;
                    assign.kind = Node::Assign;
                    assign.op = TAssign;
                    assign.number = 1;
                    assign.kids.push_back(node_name(hidden.name));
                    assign.kids.push_back(std::move(*out));
                    Node stmt;
                    stmt.kind = Node::ExprStmt;
                    stmt.kids.push_back(std::move(assign));
                    stmt_pre.push_back(std::move(stmt));
                    if (class_needs_dtor(structs[static_cast<size_t>(call_type.struct_id)]))
                        stmt_temps.push_back(Temp{hidden.name, call_type.struct_id});
                    *out = node_name(hidden.name);
                }
            }
            wrapped.kids.push_back(std::move(*out));
            *out = std::move(wrapped);
            {
                // A reference member: the field holds an address, the name means the object it refers to.
                Type object_type;
                if (static_type(out->kids[0], &object_type)) {
                    if (out->op == TArrow && object_type.pointers > 0) object_type = peel_array(object_type);
                    if (object_type.base == 3 && object_type.struct_id >= 0 &&
                        object_type.struct_id < static_cast<int>(structs.size())) {
                        for (const Field& field : structs[static_cast<size_t>(object_type.struct_id)].fields)
                            if (field.name == out->text && field.ref) {
                                Node field_node = std::move(*out);
                                *out = Node{};
                                out->kind = Node::Unary;
                                out->op = TStar;
                                out->kids.push_back(std::move(field_node));
                                break;
                            }
                    }
                }
            }
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
            if (!convert_class(out, bool_type())) return false;
            tern.kids.push_back(std::move(*out));
            if (cur_locals != nullptr) {
                // c ? obj : "text": the branch that is not an object of the class is made into one by a converting
                // constructor ([expr.cond]/4, a temporary made before the statement).
                Type ma, ea;
                if (static_type(mid, &ma) && static_type(els, &ea)) {
                    const auto is_obj = [](const Type& t) {
                        return t.base == 3 && t.pointers == 0 && !t.is_array() && t.struct_id >= 0;
                    };
                    if (is_obj(ma) && !is_obj(ea) && !ea.is_array() && class_needs_ctor(structs[static_cast<size_t>(ma.struct_id)])) {
                        std::vector<Node> one;
                        one.push_back(std::move(els));
                        Type want = ma;
                        want.is_ref = false;
                        if (!adjust_args({want}, &one)) return false;
                        els = std::move(one[0]);
                    } else if (is_obj(ea) && !is_obj(ma) && !ma.is_array() && class_needs_ctor(structs[static_cast<size_t>(ea.struct_id)])) {
                        std::vector<Node> one;
                        one.push_back(std::move(mid));
                        Type want = ea;
                        want.is_ref = false;
                        if (!adjust_args({want}, &one)) return false;
                        mid = std::move(one[0]);
                    }
                }
            }
            {
                // c ? a : b with objects of one class where a branch is not an lvalue (a call result, a temporary):
                // a hidden local gets the chosen value, in a statement before this one ([expr.cond]).
                Type ta, tb;
                const auto lvalue = [](const Node& n) {
                    return n.kind == Node::Name || n.kind == Node::Field || n.kind == Node::Index ||
                           (n.kind == Node::Unary && n.op == TStar);
                };
                if (cur_locals != nullptr && static_type(mid, &ta) && static_type(els, &tb) && ta.base == 3 &&
                    ta.pointers == 0 && !ta.is_ref && !ta.is_array() && tb.base == 3 && tb.pointers == 0 && !tb.is_ref &&
                    ta.struct_id >= 0 && ta.struct_id == tb.struct_id && !(lvalue(mid) && lvalue(els))) {
                    const bool owns = class_needs_dtor(structs[static_cast<size_t>(ta.struct_id)]) ||
                                      class_needs_ctor(structs[static_cast<size_t>(ta.struct_id)]);
                    Local hidden;
                    hidden.name = "__tn" + std::to_string(tmp_count++);
                    hidden.type = ta;
                    hidden.type.is_const = false;
                    place_local(cur_locals, hidden);
                    Node chosen;
                    chosen.kind = Node::If;
                    chosen.kids.push_back(std::move(tern.kids[0]));
                    for (Node* branch : {&mid, &els}) {
                        if (owns && lvalue(*branch)) {
                            // An object that owns something is copied by its copy constructor, not byte by byte.
                            std::vector<Node> cargs;
                            cargs.push_back(std::move(*branch));
                            Node ctor_stmt;
                            if (!special_call(ta.struct_id, "<ctor>", node_addr(node_name(hidden.name)), std::move(cargs),
                                              &ctor_stmt))
                                return false;
                            chosen.kids.push_back(std::move(ctor_stmt));
                            continue;
                        }
                        Node assign;
                        assign.kind = Node::Assign;
                        assign.op = TAssign;
                        assign.number = 1;
                        assign.kids.push_back(node_name(hidden.name));
                        assign.kids.push_back(std::move(*branch));
                        Node stmt;
                        stmt.kind = Node::ExprStmt;
                        stmt.kids.push_back(std::move(assign));
                        chosen.kids.push_back(std::move(stmt));
                    }
                    if (class_needs_dtor(structs[static_cast<size_t>(ta.struct_id)]))
                        stmt_temps.push_back(Temp{hidden.name, ta.struct_id});
                    stmt_pre.push_back(std::move(chosen));
                    *out = node_name(hidden.name);
                    continue;
                }
            }
            tern.kids.push_back(std::move(mid));
            tern.kids.push_back(std::move(els));
            *out = std::move(tern);
            continue;
        }
        Node right;
        const int next_min = is_assign_op(op) ? p : p + 1;
        Type assigned;
        if (op == TAssign && is(TLBrace) && cur_locals != nullptr && class_object_type(*out, &assigned)) {
            // x = {a, b}; assigns a temporary made from the list ([expr.ass]).
            if (!braced_temp(assigned.struct_id, &right)) return false;
        } else if (!parse_expr(next_min, &right)) {
            return false;
        }
        if (op == TAssign && right.kind == Node::Assign && !right.kids.empty() && cur_locals != nullptr) {
            // a = b = value with objects of a class: the inner assignment is done first, as its own statement, and the outer
            // one takes the inner's left side ([expr.ass]: the value of an assignment is its left operand).
            Type rt_chain;
            if (static_type(right.kids[0], &rt_chain) && rt_chain.base == 3 && rt_chain.pointers == 0 && !rt_chain.is_array()) {
                Node inner_left = right.kids[0];
                Node st;
                st.kind = Node::ExprStmt;
                st.kids.push_back(std::move(right));
                stmt_pre.push_back(std::move(st));
                right = std::move(inner_left);
            }
        }
        {
            bool handled = false;
            if (!operator_binary(op, out, &right, &handled)) return false;
            if (handled) continue;
        }
        Node bin;
        const bool compound = is_compound_op(op);
        bin.kind = op == TAssign || compound ? Node::Assign : Node::Binary;
        bin.op = op;
        bin.kids.push_back(std::move(*out));
        bin.kids.push_back(std::move(right));
        if (bin.kind == Node::Binary && bin.kids[0].kind == Node::IntLit && bin.kids[1].kind == Node::IntLit &&
            (bin.kids[0].type.base >= 5 || bin.kids[1].type.base >= 5)) {
            Node folded_node;
            if (fold_fp_binary(op, bin.kids[0], bin.kids[1], &folded_node)) bin = std::move(folded_node);
        } else if (bin.kind == Node::Binary && bin.kids[0].kind == Node::IntLit && bin.kids[1].kind == Node::IntLit &&
            bin.kids[0].type.base < 5 && bin.kids[1].type.base < 5) {
            Node folded_node;
            std::string fold_error;
            if (fold_int_binary(op, bin.kids[0], bin.kids[1], &folded_node, &fold_error)) bin = std::move(folded_node);
            else if (!fold_error.empty()) {
                error = fold_error;
                return false;
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
    int err_line = 0;  // the preprocessed-text line of the statement being generated, for error messages
    bool sret_dest_set = false;
    int sret_dest_off = 0;
    // The destination slot holds a pointer (the function's own hidden result pointer) instead of being the object.
    bool sret_dest_indirect = false;
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
    // How many scopes up from the current one the scope `local_scope` is (0 is the current scope), or -1 when it is not an
    // enclosing scope. A name means the declaration in the nearest scope, so a variable declared later in an outer scope does
    // not hide an earlier one in an inner block.
    int scope_distance(int local_scope) const {
        int walk = scope_depth;
        int dist = 0;
        while (walk != local_scope) {
            if (walk == 0) return -1;
            walk = scope_parent[static_cast<size_t>(walk)];
            ++dist;
        }
        return dist;
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
        Local* best = nullptr;
        int best_dist = 1 << 30;
        for (int i = static_cast<int>(locals.size()) - 1; i >= 0; --i) {
            Local& local = locals[static_cast<size_t>(i)];
            if (local.name != name) continue;
            const int dist = scope_distance(local.scope);
            if (dist >= 0 && dist < best_dist) {
                best = &local;
                best_dist = dist;
            }
        }
        return best;
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
        if (temp_depth >= 16) {
            error = "expression is too deep";
            return 0;
        }
        const int off = temp_base - temp_depth * 8;
        ++temp_depth;
        if (is_fp_t(last_type)) {
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
    // Spills the address in rax (the last value was an lvalue of any type, possibly a float).
    int spill_addr() {
        const Type keep = last_type;
        last_type = Type{};
        last_type.pointers = 1;
        const int off = spill();
        last_type = keep;
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
    // A statement that is not generated (a branch with a constant condition) still takes the scope numbers its blocks
    // would have taken, so the scopes of what follows keep the numbers the parser gave them.
    void skip_scopes(const Node& node) {
        const bool scoped = node.kind == Node::Block && node.text == "scope";
        if (scoped) push_scope();
        for (const Node& kid : node.kids) skip_scopes(kid);
        if (scoped) pop_scope();
    }
    bool gen_addr(const Node& node);
    int size_of(const Type& type) const;
    bool fits_register(const Type& type) const;
    Global* find_global(const std::string& name);
    Type element_type(const Type& type) const;
    void load_from_rax(int size, bool is_unsigned);
    // A float or double at [rax] goes to xmm0 (where a floating value lives), anything else to eax/rax.
    bool load_float_from_rax(const Type& t) {
        if (t.pointers != 0 || t.array_len >= 0 || (t.base != 5 && t.base != 6)) return false;
        b(t.base == 6 ? 0xF2 : 0xF3);
        b(0x0F);
        b(0x10);
        b(0x00);
        return true;
    }
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
    // The operator an assignment form stands for (+= is +).
    static Tok plain_op(Tok op) {
        switch (op) {
        case TPlusEq: return TPlus;
        case TMinusEq: return TMinus;
        case TStarEq: return TStar;
        case TSlashEq: return TSlash;
        case TPercentEq: return TPercent;
        case TAndEq: return TAmp;
        case TOrEq: return TPipe;
        case TXorEq: return TCaret;
        case TShlEq: return TShl;
        case TShrEq: return TShr;
        default: return op;
        }
    }
    // rax = r10 op rax for + - * / % & | ^ << >>, 64 bits wide when W, else 32. The left operand is in r10
    // and the right in rax, as for every integer operator. `uns` picks unsigned division and remainder, and
    // for >> a logical (rather than arithmetic) shift. A shift count goes through cl, so rcx is saved around
    // it (a local may live there); the count is the low bits of the right operand.
    void emit_int_op(Tok raw, bool W, bool uns) {
        const Tok op = plain_op(raw);
        const uint8_t rex_reg = W ? 0x4C : 0x44;  // REX.R (+W): r10 in the reg field
        const uint8_t rex_rm = W ? 0x49 : 0x41;   // REX.B (+W): r10 in the r/m field
        if (op == TPlus) {
            b(rex_reg);
            b(0x01);
            b(0xD0);
        } else if (op == TMinus) {
            b(rex_rm);
            b(0x29);
            b(0xC2);
            b(rex_reg);
            b(0x89);
            b(0xD0);
        } else if (op == TStar) {
            b(rex_rm);
            b(0x0F);
            b(0xAF);
            b(0xC2);
        } else if (op == TSlash || op == TPercent) {
            spill_reg(2);
            b(rex_rm);  // xchg rax, r10: rax = the left operand, r10 = the divisor
            b(0x92);
            if (uns) {
                b(0x31);  // xor edx, edx
                b(0xD2);
            } else {
                if (W) b(0x48);
                b(0x99);  // cdq / cqo
            }
            b(rex_rm);
            b(0xF7);
            b(uns ? 0xF2 : 0xFA);  // div / idiv r10
            if (op == TPercent) {
                if (W) b(0x48);
                b(0x89);  // mov rax, rdx
                b(0xD0);
            }
            reload_reg(2);
        } else if (op == TAmp || op == TPipe || op == TCaret) {
            b(rex_reg);
            b(op == TAmp ? 0x21 : op == TPipe ? 0x09 : 0x31);
            b(0xD0);
        } else {  // TShl, TShr
            spill_reg(1);
            b(0x89);  // mov ecx, eax
            b(0xC1);
            b(rex_reg);  // mov rax, r10
            b(0x89);
            b(0xD0);
            if (W) b(0x48);
            b(0xD3);  // shl / shr / sar rax, cl
            b(op == TShl ? 0xE0 : uns ? 0xE8 : 0xF8);
            reload_reg(1);
        }
    }
    // The floating-point binary operators. The left operand was spilled at `off` with type lt, the right is
    // in xmm0 or rax with type last_type. Both go to the wider floating type; a comparison gives an int.
    bool fp_binary(Tok op, const Type& lt, int off) {
        const Type rt = last_type;
        Type ct;
        ct.base = ((is_fp_t(lt) && lt.base == 6) || (is_fp_t(rt) && rt.base == 6)) ? 6 : 5;
        const bool use_double = ct.base == 6;
        const bool cmp = op == TEq || op == TNe || op == TLt || op == TGt || op == TLe || op == TGe;
        if (!cmp && op != TPlus && op != TMinus && op != TStar && op != TSlash) {
            error = "operator is not supported";
            return false;
        }
        convert(rt, ct);
        b(use_double ? 0xF2 : 0xF3);  // movss/movsd xmm1, xmm0
        b(0x0F);
        b(0x10);
        b(0xC8);
        if (is_fp_t(lt)) {
            b(lt.base == 6 ? 0xF2 : 0xF3);
            b(0x0F);
            b(0x10);
            b(0x85);
            disp(off);
        } else {
            b(0x48);
            b(0x8B);
            b(0x85);
            disp(off);
        }
        --temp_depth;
        convert(lt, ct);
        if (cmp) {
            if (use_double) b(0x66);
            b(0x0F);
            b(0x2E);
            b(0xC1);
            uint8_t setcc = 0x94;
            if (op == TNe) setcc = 0x95;
            else if (op == TLt) setcc = 0x92;
            else if (op == TGt) setcc = 0x97;
            else if (op == TLe) setcc = 0x96;
            else if (op == TGe) setcc = 0x93;
            b(0x0F);
            b(setcc);
            b(0xC0);
            // An unordered compare (a NaN) sets ZF, PF and CF: == < <= must be false and != true ([expr.eq],
            // [expr.rel], IEEE-754 unordered).
            if (op == TEq || op == TLt || op == TLe || op == TNe) {
                b(0x41);  // setnp r10b / setp r10b
                b(0x0F);
                b(op == TNe ? 0x9A : 0x9B);
                b(0xC2);
                b(0x44);  // and / or al, r10b
                b(op == TNe ? 0x08 : 0x20);
                b(0xD0);
            }
            b(0x0F);
            b(0xB6);
            b(0xC0);
            last_type = Type{};
            last_type.base = 1;
        } else {
            b(use_double ? 0xF2 : 0xF3);
            b(0x0F);
            b(op == TMinus ? 0x5C : op == TStar ? 0x59 : op == TSlash ? 0x5E : 0x58);
            b(0xC1);
            last_type = ct;
        }
        return true;
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
    // What a value in a register is. A float or double lives in xmm0; a long long, a pointer or an array
    // address lives in all of rax; anything else in eax.
    static bool is_fp_t(const Type& t) {
        return t.pointers == 0 && !t.is_array() && !t.is_ref && (t.base == 5 || t.base == 6);
    }
    static bool is_wide_t(const Type& t) { return t.pointers == 0 && !t.is_array() && !t.is_ref && t.base == 1 && t.wide; }
    static bool is_ptr_t(const Type& t) { return (t.pointers > 0 || t.is_array()) && !t.is_ref; }
    static bool v64(const Type& t) { return is_wide_t(t) || is_ptr_t(t); }
    static bool is_int_t(const Type& t) {
        return t.pointers == 0 && !t.is_array() && !t.is_ref && (t.base == 1 || t.base == 2 || t.base == 4);
    }
    // The type of an arithmetic operation on two integer or pointer values (the usual arithmetic
    // conversions, [expr.arith.conv]): the 64-bit operand wins, unsigned wins over signed of the same width.
    static Type common_int(const Type& a, const Type& b) {
        Type r;
        r.base = 1;
        const bool wa = v64(a);
        const bool wb = v64(b);
        const bool ua = is_ptr_t(a) || a.is_unsigned;
        const bool ub = is_ptr_t(b) || b.is_unsigned;
        if (wa || wb) {
            r.wide = true;
            r.is_unsigned = (wa && wb) ? (ua || ub) : (wa ? ua : ub);
        } else {
            r.is_unsigned = ua || ub;
        }
        return r;
    }
    // rax = the 64-bit value of the int in eax (sign-extended, or zero-extended when it is unsigned).
    void ext_rax(bool uns) {
        if (uns) {
            b(0x89);
            b(0xC0);
        } else {
            b(0x48);
            b(0x63);
            b(0xC0);
        }
    }
    void ext_r10(bool uns) {
        if (uns) {
            b(0x45);
            b(0x89);
            b(0xD2);
        } else {
            b(0x4D);
            b(0x63);
            b(0xD2);
        }
    }
    void reload_r10_64(int off) {
        b(0x4C);
        b(0x8B);
        b(0x95);
        disp(off);
        --temp_depth;
    }
    // Sets the flags so that jz is taken when the value in last_type is zero (a condition).
    void test_cond() {
        if (is_fp_t(last_type)) {
            // A NaN is true in a condition ([conv.bool]: it is not equal to zero): cmpneqss gives all ones when the
            // value is unordered or not equal to zero, and the result is tested as an integer.
            b(0x0F);  // xorps xmm1, xmm1
            b(0x57);
            b(0xC9);
            b(last_type.base == 6 ? 0xF2 : 0xF3);  // cmpneqss / cmpneqsd xmm0, xmm1
            b(0x0F);
            b(0xC2);
            b(0xC1);
            b(0x04);
            if (last_type.base == 6) {  // movq rax, xmm0 ; test rax, rax
                b(0x66);
                b(0x48);
                b(0x0F);
                b(0x7E);
                b(0xC0);
                b(0x48);
            } else {  // movd eax, xmm0 ; test eax, eax
                b(0x66);
                b(0x0F);
                b(0x7E);
                b(0xC0);
            }
            b(0x85);
            b(0xC0);
            return;
        }
        if (v64(last_type)) b(0x48);
        b(0x85);
        b(0xC0);
    }
    void normalize_bool64() {
        b(0x48);
        b(0x85);
        b(0xC0);
        b(0x0F);
        b(0x95);
        b(0xC0);
        b(0x0F);
        b(0xB6);
        b(0xC0);
    }
    // Converts the value in rax or xmm0 from type `from` to type `to` ([conv.integral], [conv.fpint],
    // [conv.double], [conv.bool]). Only r10 is used besides rax and xmm0.
    void convert(const Type& from, const Type& to) {
        const bool from_fp = is_fp_t(from);
        const bool to_fp = is_fp_t(to);
        if (is_bool_type(to) && !from_fp) {
            if (v64(from)) normalize_bool64();
            else normalize_bool();
            return;
        }
        if (from_fp && to_fp) {
            if (from.base != to.base) {
                b(to.base == 6 ? 0xF3 : 0xF2);
                b(0x0F);
                b(0x5A);
                b(0xC0);
            }
            return;
        }
        if (!from_fp && to_fp) {
            if (!is_int_t(from)) return;
            const uint8_t prefix = to.base == 6 ? 0xF2 : 0xF3;
            if (is_wide_t(from) && from.is_unsigned) {
                // Above 2^63 the value is halved (keeping the low bit so rounding stays right), converted
                // and doubled.
                b(0x48);  // test rax, rax
                b(0x85);
                b(0xC0);
                const size_t js = emit_jcc(0x88);
                b(prefix);
                b(0x48);
                b(0x0F);
                b(0x2A);
                b(0xC0);
                const size_t jmp = emit_jmp();
                patch(js, text.size());
                b(0x49);  // mov r10, rax
                b(0x89);
                b(0xC2);
                b(0x48);  // shr rax, 1
                b(0xD1);
                b(0xE8);
                b(0x41);  // and r10d, 1
                b(0x83);
                b(0xE2);
                b(0x01);
                b(0x4C);  // or rax, r10
                b(0x09);
                b(0xD0);
                b(prefix);
                b(0x48);
                b(0x0F);
                b(0x2A);
                b(0xC0);
                b(prefix);  // addss / addsd xmm0, xmm0
                b(0x0F);
                b(0x58);
                b(0xC0);
                patch(jmp, text.size());
                return;
            }
            if (is_wide_t(from)) {
                b(prefix);
                b(0x48);
                b(0x0F);
                b(0x2A);
                b(0xC0);
                return;
            }
            if (from.is_unsigned) {
                b(0x89);  // mov eax, eax: an unsigned int is converted as a 64-bit value
                b(0xC0);
                b(prefix);
                b(0x48);
                b(0x0F);
                b(0x2A);
                b(0xC0);
                return;
            }
            b(prefix);
            b(0x0F);
            b(0x2A);
            b(0xC0);
            return;
        }
        if (from_fp && !to_fp) {
            if (!is_int_t(to)) return;
            const bool w = is_wide_t(to) || (to.base == 1 && to.is_unsigned);
            b(from.base == 6 ? 0xF2 : 0xF3);
            if (w) b(0x48);
            b(0x0F);
            b(0x2C);
            b(0xC0);
            if (is_bool_type(to)) normalize_bool();
            return;
        }
        if (is_wide_t(to) && is_int_t(from) && !is_wide_t(from)) ext_rax(from.is_unsigned);
    }

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
    return type.wide ? 8 : 4;
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
        if (!v64(last_type) && !is_fp_t(last_type)) ext_rax(last_type.is_unsigned);
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
    if (node.kind == Node::Ternary) {
        // c ? a : b as an lvalue (both are lvalues of one type): the address of the chosen one.
        if (!gen_expr(node.kids[0])) return false;
        test_cond();
        const size_t jz = emit_jcc(0x84);
        if (!gen_addr(node.kids[1])) return false;
        const Type first = last_type;
        const size_t jmp = emit_jmp();
        patch(jz, text.size());
        if (!gen_addr(node.kids[2])) return false;
        patch(jmp, text.size());
        last_type = first;
        return true;
    }
    error = "not an address (preprocessed line " + std::to_string(err_line) + ", node kind " + std::to_string(static_cast<int>(node.kind)) + ")";
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
        // The elements not named are zero ([dcl.init.aggr]).
        Node zero;
        zero.kind = Node::IntLit;
        zero.type.base = 1;
        Node empty;
        empty.kind = Node::Block;
        empty.text = "init";
        if (type.array_len >= 0) {
            const Type elem = peel_array(type);
            const int step = std::max(1, size_of(elem));
            for (int i = 0; i < type.array_len; ++i) {
                const Node& item = static_cast<size_t>(i) < node.kids.size() ? node.kids[static_cast<size_t>(i)]
                                   : (elem.array_len >= 0 || (elem.base == 3 && elem.pointers == 0)) ? empty : zero;
                if (!store_init(elem, offset + i * step, item)) return false;
            }
            return true;
        }
        if (type.base == 3 && type.pointers == 0 && structs != nullptr && type.struct_id >= 0 &&
            type.struct_id < static_cast<int>(structs->size())) {
            const StructDef& def = (*structs)[static_cast<size_t>(type.struct_id)];
            for (size_t i = 0; i < def.fields.size(); ++i) {
                const Field& field = def.fields[i];
                if (field.name == "__vptr") continue;
                const bool nested = field.type.array_len >= 0 || (field.type.base == 3 && field.type.pointers == 0);
                const Node& item = i < node.kids.size() ? node.kids[i] : nested ? empty : zero;
                if (!store_init(field.type, offset + field.offset, item)) return false;
            }
            return true;
        }
        error = "initializer does not match the type";
        return false;
    }
    if (node.kind != Node::IntLit) {
        // A value computed at run time: evaluated, converted to the element type and stored.
        if ((type.base == 3 && type.pointers == 0) || type.array_len >= 0) {
            error = "initializer must be a constant";
            return false;
        }
        if (!gen_expr(node)) return false;
        convert(last_type, type);
        if (is_fp_t(type)) {
            b(type.base == 6 ? 0xF2 : 0xF3);
            b(0x0F);
            b(0x11);
            b(0x85);
            disp(offset);
            return true;
        }
        const int bytes = type.pointers > 0 ? 8 : std::max(1, size_of(type));
        if (bytes >= 8) b(0x48);
        else if (bytes == 2) b(0x66);
        b(bytes == 1 ? 0x88 : 0x89);
        b(0x85);
        disp(offset);
        return true;
    }
    const int width = type.pointers > 0 ? 8 : std::max(1, size_of(type));
    uint64_t value = 0;
    if (!const_convert(node, type, &value)) value = node.number;
    if (is_bool_type(type)) value = node.number != 0 ? 1u : 0u;
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
        u32(static_cast<uint32_t>(value));
        if (width >= 8) {
            b(0xC7);
            b(0x85);
            disp(offset + 4);
            u32(static_cast<uint32_t>(value >> 32));
        }
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
        last_type = Type{};
        last_type.base = 1;
        last_type.is_unsigned = node.type.is_unsigned;
        if (node.type.wide) {
            b(0x48);  // mov rax, imm64
            b(0xB8);
            for (int i = 0; i < 8; ++i) b(static_cast<uint8_t>(node.number >> (8 * i)));
            last_type.wide = true;
            return true;
        }
        mov_eax_imm(static_cast<uint32_t>(node.number));
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
                last_type = referred;
                if (load_float_from_rax(referred)) return true;
                load_from_rax(size >= 8 ? 8 : size, referred.is_unsigned);
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
                if (local->type.is_array()) {  // a static array used as a value is the address of its first element
                    if (!gen_addr(node)) return false;
                    last_type = element_type(local->type);
                    last_type.pointers += 1;
                    return true;
                }
                const int size = size_of(local->type);
                const bool uns = local->type.is_unsigned;
                if (is_fp_t(local->type)) {
                    b(local->type.base == 6 ? 0xF2 : 0xF3);  // movss/movsd xmm0, [rip+symbol]
                    b(0x0F);
                    b(0x10);
                    b(0x05);
                    relocs.push_back({0, static_cast<uint32_t>(text.size()), local->symbol, kRelAmd64Rel32});
                    u32(0);
                    last_type = local->type;
                    return true;
                }
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
            if (is_fp_t(global->type)) {
                b(global->type.base == 6 ? 0xF2 : 0xF3);  // movss/movsd xmm0, [rip+symbol]
                b(0x0F);
                b(0x10);
                b(0x05);
                relocs.push_back({0, static_cast<uint32_t>(text.size()), global->name, kRelAmd64Rel32});
                u32(0);
                last_type = global->type;
                return true;
            }
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
        if (last_type.is_array()) {
            // An array member (or a row of a 2-D array) used as a value is the address of its first element.
            last_type = element_type(last_type);
            last_type.pointers += 1;
            return true;
        }
        const int size = std::max(1, size_of(last_type));
        const bool uns = last_type.is_unsigned;
        const int bits = last_type.bits;
        const int bit_off = last_type.bit_off;
        if (load_float_from_rax(last_type)) return true;
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
        test_cond();
        const size_t jz = emit_jcc(0x84);
        if (!gen_expr(node.kids[1])) return false;
        const Type first = last_type;
        const size_t jmp = emit_jmp();
        patch(jz, text.size());
        if (!gen_expr(node.kids[2])) return false;
        const Type second = last_type;
        // The two branches have one type after the usual arithmetic conversions ([expr.cond]); the
        // first branch is converted by a stub placed after the second.
        Type unified = first;
        std::vector<uint8_t> stub;
        if (is_fp_t(first) || is_fp_t(second)) {
            if (is_fp_t(first) && is_fp_t(second)) unified.base = first.base == 6 || second.base == 6 ? 6 : 5;
            else {
                unified = Type{};
                unified.base = (is_fp_t(first) ? first.base : second.base);
            }
        } else if (is_ptr_t(second) && is_int_t(first)) {
            unified = second;
        } else if (is_int_t(first) && is_int_t(second)) {
            unified = common_int(first, second);
            if (!is_wide_t(unified) && first.base == second.base && first.base != 1) unified = first;
        }
        if (is_int_t(first) || is_fp_t(first)) {
            std::vector<uint8_t> keep;
            keep.swap(text);
            convert(first, unified);
            stub.swap(text);
            text.swap(keep);
        }
        convert(second, unified);
        if (stub.empty()) {
            patch(jmp, text.size());
        } else {
            const size_t jend = emit_jmp();
            patch(jmp, text.size());
            text.insert(text.end(), stub.begin(), stub.end());
            patch(jend, text.size());
        }
        last_type = unified;
        return true;
    }
    if (node.kind == Node::Unary) {
        if (node.op == TEnd && !node.kids.empty()) {
            if (!gen_expr(node.kids[0])) return false;
            const Type from = last_type;
            const Type to = node.type;
            convert(from, to);
            if (is_int_t(to) && !is_bool_type(to) && (to.base == 2 || to.base == 4) && !is_fp_t(from)) {
                // The value is cut to the width of the type and extended again (movsx / movzx eax, al or ax).
                b(0x0F);
                b(to.base == 2 ? (to.is_unsigned ? 0xB6 : 0xBE) : (to.is_unsigned ? 0xB7 : 0xBF));
                b(0xC0);
            }
            last_type = to;
            if (is_int_t(to)) promote(&last_type);
            return true;
        }
        if (node.op == TPlusPlus || node.op == TMinusMinus) {
            if (!gen_addr(node.kids[0])) return false;
            const Type lvalue_type = last_type;
            const bool pointer = last_type.pointers > 0;
            const int step = pointer ? std::max(1, size_of(element_type(last_type))) : 1;
            const int width = pointer ? 8 : std::max(1, size_of(last_type));
            const bool w64 = pointer || is_wide_t(lvalue_type);
            const int addr = spill_addr();
            if (!error.empty()) return false;
            if (is_fp_t(lvalue_type)) {
                // f++ and f-- on a float or double: one is added to or taken from the value in memory.
                const bool dbl = lvalue_type.base == 6;
                b(0x48);  // mov rax, [addr]
                b(0x8B);
                b(0x85);
                disp(addr);
                b(dbl ? 0xF2 : 0xF3);  // movss/movsd xmm0, [rax]
                b(0x0F);
                b(0x10);
                b(0x00);
                int old_slot = 0;
                if (node.number != 0) {
                    last_type = lvalue_type;
                    old_slot = spill();
                    if (!error.empty()) return false;
                }
                b(0x48);  // mov rax, 1.0
                b(0xB8);
                const uint64_t one = dbl ? 0x3FF0000000000000ull : 0x3F800000ull;
                for (int i = 0; i < 8; ++i) b(static_cast<uint8_t>(one >> (8 * i)));
                b(0x66);  // movd/movq xmm1, eax/rax
                if (dbl) b(0x48);
                b(0x0F);
                b(0x6E);
                b(0xC8);
                b(dbl ? 0xF2 : 0xF3);  // addss/subss xmm0, xmm1
                b(0x0F);
                b(node.op == TPlusPlus ? 0x58 : 0x5C);
                b(0xC1);
                b(0x48);  // mov rcx, [addr]
                b(0x8B);
                b(0x8D);
                disp(addr);
                b(dbl ? 0xF2 : 0xF3);  // movss/movsd [rcx], xmm0
                b(0x0F);
                b(0x11);
                b(0x01);
                if (node.number != 0) {
                    b(dbl ? 0xF2 : 0xF3);
                    b(0x0F);
                    b(0x10);
                    b(0x85);
                    disp(old_slot);
                    --temp_depth;
                }
                --temp_depth;
                last_type = lvalue_type;
                return true;
            }
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
            if (w64) b(0x48);
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
            last_type = lvalue_type;
            promote(&last_type);
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
            if (is_wide_t(last_type)) b(0x48);
            b(0xF7);
            b(0xD8);
            return true;
        }
        if (node.op == TTilde) {
            if (last_type.pointers > 0 || last_type.base == 3 || last_type.base == 5 || last_type.base == 6) {
                error = "operator ~ needs an integer";
                return false;
            }
            if (is_wide_t(last_type)) b(0x48);
            b(0xF7);  // not eax
            b(0xD0);
            promote(&last_type);
            return true;
        }
        if (node.op == TNot) {
            test_cond();
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
            last_type = elem;
            if (load_float_from_rax(elem)) return true;
            load_from_rax(std::max(1, size_of(elem)), elem.is_unsigned);
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
                    const int addr = spill_addr();
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
                    convert(last_type, local->type);
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
                if (global->type.is_const && global->type.pointers == 0) {
                    error = "assignment to const " + global->name;
                    return false;
                }
            }
        }
        if (is_compound_op(node.op)) {
            if (!gen_addr(node.kids[0])) return false;
            const Type lt = last_type;
            const int width = std::max(1, size_of(lt));
            const int addr = spill_addr();
            if (!error.empty()) return false;
            b(0x48);
            b(0x8B);
            b(0x85);
            disp(addr);
            if (is_fp_t(lt)) {
                b(lt.base == 6 ? 0xF2 : 0xF3);
                b(0x0F);
                b(0x10);
                b(0x00);
            } else {
                load_from_rax(width >= 8 ? 8 : width, lt.is_unsigned);
            }
            Type value_type = lt;
            value_type.bits = -1;
            last_type = value_type;
            const int left = spill();
            if (!error.empty()) return false;
            if (!gen_expr(node.kids[1])) return false;
            const Type rt = last_type;
            const Tok op = plain_op(node.op);
            if (is_fp_t(lt) || is_fp_t(rt)) {
                if (!fp_binary(op, lt, left)) return false;
                convert(last_type, lt);
            } else {
                const bool shift = op == TShl || op == TShr;
                const bool lptr = is_ptr_t(lt);
                Type ct = lt;
                bool W;
                if (lptr) {
                    // p += n scales n by the size of the element ([expr.add]).
                    if (!v64(rt)) ext_rax(rt.is_unsigned);
                    b(0x48);
                    b(0x69);
                    b(0xC0);
                    u32(static_cast<uint32_t>(std::max(1, size_of(element_type(lt)))));
                    W = true;
                } else if (shift) {
                    W = is_wide_t(lt);
                    ct = lt;
                    if (!W) ct.wide = false;
                } else {
                    ct = common_int(lt, rt);
                    W = ct.wide;
                    if (W && !v64(rt)) ext_rax(rt.is_unsigned);
                }
                if (W) reload_r10_64(left);
                else reload_r10(left);
                if (W && !v64(lt)) ext_r10(lt.is_unsigned);
                emit_int_op(op, W, shift ? lt.is_unsigned : ct.is_unsigned);
                if (is_bool_type(lt)) {
                    if (W) normalize_bool64();
                    else normalize_bool();
                }
            }
            b(0x48);
            b(0x8B);
            b(0x8D);
            disp(addr);
            if (is_fp_t(lt)) {
                b(lt.base == 6 ? 0xF2 : 0xF3);
                b(0x0F);
                b(0x11);
                b(0x01);
            } else {
                store_mem(width);
            }
            --temp_depth;
            last_type = value_type;
            promote(&last_type);
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
        if (node.kids[0].kind == Node::Field || node.kids[0].kind == Node::Index ||
            (node.kids[0].kind == Node::Unary && node.kids[0].op == TStar) ||
            (node.kids[0].kind == Node::Name && find_local(node.kids[0].text) != nullptr &&
             find_local(node.kids[0].text)->type.is_ref)) {
            // A big struct stored into a member, an element or through a pointer: the bytes are copied, or the
            // callee writes the result straight there. The address is worked out first; when the target turns
            // out to be something else, the code is dropped and the general path below runs.
            const size_t mark_text = text.size();
            const size_t mark_relocs = relocs.size();
            const int mark_depth = temp_depth;
            if (!gen_addr(node.kids[0])) return false;
            const Type target = last_type;
            if (target.base == 3 && target.pointers == 0 && !target.is_array() && !fits_register(target)) {
                const int dest = spill_addr();
                if (!error.empty()) return false;
                if (node.kids[1].kind == Node::Call) {
                    sret_dest_set = true;
                    sret_dest_off = dest;
                    sret_dest_indirect = true;
                    if (!gen_expr(node.kids[1])) return false;
                } else {
                    if (!gen_addr(node.kids[1])) return false;
                    spill_reg(6);
                    spill_reg(7);
                    b(0x48);  // mov rsi, rax
                    b(0x89);
                    b(0xC6);
                    b(0x48);  // mov rdi, [rbp+dest]
                    b(0x8B);
                    b(0xBD);
                    disp(dest);
                    b(0xB9);
                    u32(static_cast<uint32_t>(size_of(target)));
                    b(0xF3);
                    b(0xA4);
                    reload_reg(6);
                    reload_reg(7);
                }
                --temp_depth;
                last_type = target;
                return true;
            }
            text.resize(mark_text);
            relocs.resize(mark_relocs);
            temp_depth = mark_depth;
        }
        if (!gen_expr(node.kids[1])) return false;
        const Type rhs_type = last_type;
        const bool rhs_fp = is_fp_t(rhs_type);
        const int slot = spill();
        if (!error.empty()) return false;
        if (!gen_addr(node.kids[0])) return false;
        const Type lt = last_type;
        b(0x48);  // mov rcx, rax: the address
        b(0x89);
        b(0xC1);
        if (rhs_fp) {
            b(rhs_type.base == 6 ? 0xF2 : 0xF3);
            b(0x0F);
            b(0x10);
            b(0x85);
            disp(slot);
        } else {
            b(0x48);
            b(0x8B);
            b(0x85);
            disp(slot);
        }
        --temp_depth;
        convert(rhs_type, lt);
        if (is_fp_t(lt)) {
            b(lt.base == 6 ? 0xF2 : 0xF3);
            b(0x0F);
            b(0x11);
            b(0x01);
            last_type = lt;
            return true;
        }
        const int size = std::max(1, size_of(lt));
        if (lt.bits >= 0) {
            const int bits = lt.bits;
            const int bit_off = lt.bit_off;
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
        last_type = lt;
        last_type.bits = -1;
        promote(&last_type);
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
        bool hidden_ptr = false;
        if (sret) {
            if (sret_dest_set) {
                hidden = sret_dest_off;
                hidden_ptr = sret_dest_indirect;
                sret_dest_set = false;
                sret_dest_indirect = false;
            } else if (current != nullptr && current->sret) {
                hidden = current->sret_off;
                hidden_ptr = true;
            } else {
                error = "large struct return has no destination (preprocessed line " + std::to_string(err_line) + ")";
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
            } else if (named) {
                convert(last_type, param);
                // The argument now has the parameter's type (a float parameter is passed in an xmm register).
                if (param.pointers == 0 && !param.is_array() && (param.base == 5 || param.base == 6)) last_type = param;
                else if (is_fp_t(last_type) && !is_fp_t(param) && param.pointers == 0 && param.base != 3) {
                    last_type = param;
                    last_type.is_ref = false;
                }
            } else if (is_fp_t(last_type) && last_type.base == 5) {
                Type promoted;  // a float passed through ... becomes a double ([expr.call])
                promoted.base = 6;
                convert(last_type, promoted);
                last_type = promoted;
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
            b(hidden_ptr ? 0x8B : 0x8D);
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
            b(hidden_ptr ? 0x8B : 0x8D);
            b(0x85);
            disp(hidden);
        } else {
            promote(&last_type);
        }
        return true;
    }
    if (node.kind == Node::Binary) {
        if (node.op == TComma) {
            // a, b ([expr.comma]): a is evaluated and dropped, the value and type are those of b.
            if (!gen_expr(node.kids[0])) return false;
            return gen_expr(node.kids[1]);
        }
        if (node.op == TAndAnd || node.op == TOrOr) {
            if (!gen_expr(node.kids[0])) return false;
            test_cond();
            const size_t j1 = emit_jcc(node.op == TAndAnd ? 0x84 : 0x85);
            if (!gen_expr(node.kids[1])) return false;
            test_cond();
            const size_t j2 = emit_jcc(node.op == TAndAnd ? 0x84 : 0x85);
            mov_eax_imm(node.op == TAndAnd ? 1 : 0);
            const size_t jmp = emit_jmp();
            const size_t other = text.size();
            mov_eax_imm(node.op == TAndAnd ? 0 : 1);
            const size_t end = text.size();
            patch(j1, other);
            patch(j2, other);
            patch(jmp, end);
            last_type = Type{};
            last_type.base = 1;
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
        const bool left_fp = is_fp_t(left_type);
        const bool right_fp = is_fp_t(last_type);
        if (left_fp || right_fp) return fp_binary(node.op, left_type, off);
        const Type right_type = last_type;
        if (node.op == TComma) {
            --temp_depth;
            return true;
        }
        const bool shift = node.op == TShl || node.op == TShr;
        const bool left_ptr = is_ptr_t(left_type);
        const bool right_ptr = is_ptr_t(right_type);
        if (shift || node.op == TAmp || node.op == TPipe || node.op == TCaret) {
            if (left_ptr || right_ptr || left_type.base == 3 || right_type.base == 3) {
                error = "a bit operator needs integers";
                return false;
            }
        }
        const Type ct = shift ? left_type : common_int(left_type, right_type);
        const bool W = shift ? is_wide_t(left_type) : (v64(left_type) || v64(right_type));
        if (W && !shift && !v64(right_type)) ext_rax(right_type.is_unsigned);
        if (W) reload_r10_64(off);
        else reload_r10(off);
        if (W && !v64(left_type)) ext_r10(left_type.is_unsigned);
        const bool cmp = node.op == TEq || node.op == TNe || node.op == TLt || node.op == TGt || node.op == TLe ||
                         node.op == TGe;
        if (cmp) {
            const bool unsign = left_ptr || right_ptr || ct.is_unsigned;
            uint8_t setcc = 0x94;
            if (node.op == TEq) setcc = 0x94;
            else if (node.op == TNe) setcc = 0x95;
            else if (node.op == TLt) setcc = unsign ? 0x92 : 0x9C;
            else if (node.op == TGt) setcc = unsign ? 0x97 : 0x9F;
            else if (node.op == TLe) setcc = unsign ? 0x96 : 0x9E;
            else setcc = unsign ? 0x93 : 0x9D;
            if (W) b(0x49);
            else b(0x41);
            b(0x39);
            b(0xC2);
            b(0x0F);
            b(setcc);
            b(0xC0);
            b(0x0F);
            b(0xB6);
            b(0xC0);
            last_type = Type{};
            last_type.base = 1;
            return true;
        }
        if (node.op == TPlus && !left_ptr && right_ptr) {
            // n + p: n is scaled by the size of the element.
            b(0x4D);  // imul r10, r10, scale
            b(0x69);
            b(0xD2);
            u32(static_cast<uint32_t>(std::max(1, size_of(element_type(right_type)))));
        }
        if (node.op == TMinus && left_ptr && right_ptr) {
            // pointer - pointer: the byte difference divided by the element size, a 64-bit count.
            emit_int_op(TMinus, true, false);
            const int scale = std::max(1, size_of(element_type(left_type)));
            if (scale > 1) {
                spill_reg(2);
                b(0x49);  // mov r10, scale
                b(0xC7);
                b(0xC2);
                u32(static_cast<uint32_t>(scale));
                b(0x48);  // cqo
                b(0x99);
                b(0x49);  // idiv r10
                b(0xF7);
                b(0xFA);
                reload_reg(2);
            }
            last_type = Type{};
            last_type.base = 1;
            last_type.wide = true;
            return true;
        }
        if (node.op != TPlus && node.op != TMinus && node.op != TStar && node.op != TSlash && node.op != TPercent &&
            !bit_op(node.op)) {
            error = "operator is not supported";
            return false;
        }
        emit_int_op(node.op, W, shift ? left_type.is_unsigned : ct.is_unsigned);
        if (node.op == TPlus && !left_ptr && right_ptr) last_type = right_type;
        else if ((node.op == TPlus || node.op == TMinus) && left_ptr) last_type = left_type;
        else {
            last_type = ct;
            if (!shift) last_type.bits = -1;
        }
        return true;
    }
    error = "bad expression";
    return false;
}

bool Gen::gen_stmt(const Node& node) {
    temp_depth = 0;
    if (node.line > 0) err_line = node.line;
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
            if (node.kids[0].kind == Node::Ternary && node.kids[0].kids.size() == 3) {
                // return c ? a : b; with a big struct: each branch returns its own value.
                Node branch;
                branch.kind = Node::If;
                branch.kids.push_back(node.kids[0].kids[0]);
                for (size_t side = 1; side <= 2; ++side) {
                    Node ret;
                    ret.kind = Node::Return;
                    ret.kids.push_back(node.kids[0].kids[side]);
                    branch.kids.push_back(std::move(ret));
                }
                return gen_stmt(branch);
            }
            if (node.kids[0].kind == Node::Call) {
                sret_dest_set = true;
                sret_dest_off = current->sret_off;
                sret_dest_indirect = true;
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
        if (current != nullptr && !node.kids.empty()) convert(last_type, current->ret);
        epilogue();
        return true;
    }
    if (node.kind == Node::ExprStmt) return gen_expr(node.kids[0]);
    if (node.kind == Node::If) {
        if (optimize && node.kids[0].kind == Node::IntLit) {
            if (node.kids[0].number != 0) {
                const bool ok = gen_stmt(node.kids[1]);
                if (node.kids.size() == 3) skip_scopes(node.kids[2]);
                return ok;
            }
            skip_scopes(node.kids[1]);
            if (node.kids.size() == 3) return gen_stmt(node.kids[2]);
            return true;
        }
        if (!gen_expr(node.kids[0])) return false;
        test_cond();
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
        test_cond();
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
            test_cond();
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
        test_cond();
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
        // A long long selector is compared as 64 bits (a value above 2^32 must not match a small case).
        const bool wide_selector = is_wide_t(last_type);
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
        if (!wide_selector && case_count >= 4 && span >= 0 && span <= 8) {
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
            if (wide_selector) {
                b(0x48);  // mov rax, [slot]
                b(0x8B);
                b(0x85);
                disp(slot);
                b(0x49);  // mov r10, imm64
                b(0xBA);
                for (int k = 0; k < 8; ++k) b(static_cast<uint8_t>(node.kids[i].number >> (8 * k)));
                b(0x4C);  // cmp rax, r10
                b(0x39);
                b(0xD0);
            } else {
                b(0x8B);
                b(0x85);
                disp(slot);
                b(0x3D);
                u32(static_cast<uint32_t>(node.kids[i].number));
            }
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

// True when the name is incremented, decremented or changed by a compound assignment: those go through the
// variable's address, so it cannot live in a register.
bool modified_in_place(const Node& node, const std::string& name) {
    if (node.kind == Node::Unary && (node.op == TPlusPlus || node.op == TMinusMinus) && !node.kids.empty() &&
        node.kids[0].kind == Node::Name && node.kids[0].text == name)
        return true;
    if (node.kind == Node::Assign && node.op != TAssign && node.op != TEnd && !node.kids.empty() &&
        node.kids[0].kind == Node::Name && node.kids[0].text == name)
        return true;
    for (const Node& kid : node.kids)
        if (modified_in_place(kid, name)) return true;
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

// Forgets every name that is assigned, incremented or decremented anywhere inside the node.
void kill_assigned(const Node& node, std::map<std::string, uint64_t>* known) {
    if (node.kind == Node::Assign && !node.kids.empty() && node.kids[0].kind == Node::Name) known->erase(node.kids[0].text);
    if (node.kind == Node::Unary && (node.op == TPlusPlus || node.op == TMinusMinus) && !node.kids.empty() &&
        node.kids[0].kind == Node::Name)
        known->erase(node.kids[0].text);
    for (const Node& kid : node.kids) kill_assigned(kid, known);
}

// /O2 constant propagation: a local int or long long that was given a constant keeps that value until
// anything can change it. A loop forgets what its body assigns before the loop is read (the second round
// sees the new value), a branch works on a copy, a label or a case forgets everything (it can be reached
// from elsewhere), and the right side of && || ?: only runs sometimes.
void prop_node(Node* node, std::map<std::string, uint64_t>* known, const std::set<std::string>* shadowed,
               const std::map<std::string, Type>* ltypes) {
    using Known = std::map<std::string, uint64_t>;
    if (node->kind == Node::While || node->kind == Node::Do) {
        kill_assigned(*node, known);
        Known inner = *known;
        for (Node& kid : node->kids) prop_node(&kid, &inner, shadowed, ltypes);
        return;
    }
    if (node->kind == Node::For && node->kids.size() == 4) {
        prop_node(&node->kids[0], known, shadowed, ltypes);
        kill_assigned(*node, known);
        Known inner = *known;
        for (size_t i = 1; i < node->kids.size(); ++i) prop_node(&node->kids[i], &inner, shadowed, ltypes);
        return;
    }
    if (node->kind == Node::If && !node->kids.empty()) {
        prop_node(&node->kids[0], known, shadowed, ltypes);
        for (size_t i = 1; i < node->kids.size(); ++i) {
            Known inner = *known;
            prop_node(&node->kids[i], &inner, shadowed, ltypes);
        }
        kill_assigned(*node, known);
        return;
    }
    if (node->kind == Node::Ternary || (node->kind == Node::Binary && (node->op == TAndAnd || node->op == TOrOr))) {
        if (!node->kids.empty()) prop_node(&node->kids[0], known, shadowed, ltypes);
        for (size_t i = 1; i < node->kids.size(); ++i) {
            Known inner = *known;
            prop_node(&node->kids[i], &inner, shadowed, ltypes);
        }
        kill_assigned(*node, known);
        return;
    }
    if (node->kind == Node::Switch) {
        if (!node->kids.empty()) prop_node(&node->kids[0], known, shadowed, ltypes);
        kill_assigned(*node, known);
        Known inner = *known;
        for (size_t i = 1; i < node->kids.size(); ++i) prop_node(&node->kids[i], &inner, shadowed, ltypes);
        return;
    }
    if (node->kind == Node::Case || node->kind == Node::Label) known->clear();
    if (node->kind == Node::Block || node->kind == Node::Label || node->kind == Node::Case) {
        if (node->kind == Node::Block) {
            for (size_t i = 0; i + 1 < node->kids.size(); ++i) {
                Node* cur = statement_value(&node->kids[i]);
                Node* nxt = statement_value(&node->kids[i + 1]);
                // x = a; x = b; : the first store is dead unless b reads x or a does something else.
                if (cur->kind == Node::Assign && nxt->kind == Node::Assign && cur->kids.size() == 2 && nxt->kids.size() == 2 &&
                    cur->kids[0].kind == Node::Name && nxt->kids[0].kind == Node::Name && cur->kids[0].text == nxt->kids[0].text &&
                    (cur->op == TAssign || cur->op == TEnd) && (nxt->op == TAssign || nxt->op == TEnd) &&
                    !expr_uses(nxt->kids[1], cur->kids[0].text) && !has_effects(cur->kids[1])) {
                    node->kids[i].kind = Node::Block;
                    node->kids[i].kids.clear();
                }
            }
        }
        for (Node& kid : node->kids) prop_node(&kid, known, shadowed, ltypes);
        return;
    }
    if (node->kind == Node::Assign && !node->kids.empty() && node->kids[0].kind == Node::Name &&
        (node->op == TAssign || node->op == TEnd)) {
        prop_node(&node->kids[1], known, shadowed, ltypes);
        if (shadowed != nullptr && shadowed->count(node->kids[0].text) != 0) {
            known->erase(node->kids[0].text);
            return;
        }
        // Only an int or long long local keeps a known value, cut to its type (a char or a bool would need
        // its own conversion, a float its own bits).
        const auto lt = ltypes != nullptr ? ltypes->find(node->kids[0].text) : std::map<std::string, Type>::const_iterator();
        const bool plain = ltypes != nullptr && lt != ltypes->end() && lt->second.base == 1 && lt->second.pointers == 0 &&
                           !lt->second.is_array() && !lt->second.is_ref && !lt->second.is_bool && lt->second.bits < 0;
        if (plain && node->kids[1].kind == Node::IntLit && node->kids[1].type.base < 5)
            (*known)[node->kids[0].text] = const_norm(node->kids[1].number, lt->second);
        else known->erase(node->kids[0].text);
        return;
    }
    if (node->kind == Node::Assign && !node->kids.empty() && node->kids[0].kind == Node::Name) {
        // x += e: x changes, and is not an operand that can be replaced by its old value.
        for (size_t i = 1; i < node->kids.size(); ++i) prop_node(&node->kids[i], known, shadowed, ltypes);
        known->erase(node->kids[0].text);
        return;
    }
    if (node->kind == Node::Unary && (node->op == TPlusPlus || node->op == TMinusMinus) && !node->kids.empty() &&
        node->kids[0].kind == Node::Name) {
        known->erase(node->kids[0].text);
        return;
    }
    if (node->kind == Node::Unary && node->op == TAmp && !node->kids.empty() && node->kids[0].kind == Node::Name) return;
    if (node->kind == Node::Name && known->count(node->text) != 0 &&
        (shadowed == nullptr || shadowed->count(node->text) == 0)) {
        const uint64_t value = (*known)[node->text];
        const auto lt = ltypes != nullptr ? ltypes->find(node->text) : std::map<std::string, Type>::const_iterator();
        *node = Node{};
        node->kind = Node::IntLit;
        node->number = value;
        node->type.base = 1;
        if (ltypes != nullptr && lt != ltypes->end()) {
            node->type.wide = lt->second.wide;
            node->type.is_unsigned = lt->second.is_unsigned;
        }
        return;
    }
    for (Node& kid : node->kids) prop_node(&kid, known, shadowed, ltypes);
    if (node->kind == Node::Binary && node->kids.size() == 2 && node->kids[0].kind == Node::IntLit &&
        node->kids[1].kind == Node::IntLit && node->kids[0].type.base < 5 && node->kids[1].type.base < 5) {
        Node folded_node;
        std::string fold_error;
        if (fold_int_binary(node->op, node->kids[0], node->kids[1], &folded_node, &fold_error)) *node = std::move(folded_node);
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
            // The call converts each argument to its parameter type and the result to the return type;
            // the inlined expression keeps that with casts ([expr.call], [stmt.return]).
            const auto arithmetic = [](const Type& t) {
                return t.pointers == 0 && !t.is_array() && !t.is_ref && (t.base == 1 || t.base == 2 || t.base == 4 || t.base == 5 || t.base == 6);
            };
            const auto cast_to = [](const Type& t, Node inner) {
                Node cast;
                cast.kind = Node::Unary;
                cast.op = TEnd;
                cast.type = t;
                cast.kids.push_back(std::move(inner));
                return cast;
            };
            std::map<std::string, Node> repl;
            for (int i = 0; i < fn->nparams; ++i) {
                Node arg = node->kids[static_cast<size_t>(i)];
                if (arithmetic(fn->params[static_cast<size_t>(i)])) arg = cast_to(fn->params[static_cast<size_t>(i)], std::move(arg));
                repl[fn->param_names[static_cast<size_t>(i)]] = std::move(arg);
            }
            Node next = *expr;
            subst_names(&next, repl);
            if (arithmetic(fn->ret)) next = cast_to(fn->ret, std::move(next));
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
    // A reference parameter or result is not a plain value: the body would read the address as the value.
    if (fn->ret_ref) return false;
    for (bool is_ref : fn->param_ref)
        if (is_ref) return false;
    // A static local keeps its value between calls: it cannot become a local of the caller.
    for (const Local& static_local : fn->locals)
        if (static_local.is_static) return false;
    // A hidden local (__rv, __br, __tmp) is not copied into the caller, so such a body is not inlined.
    for (const Local& hidden_local : fn->locals) {
        bool is_param_name = false;
        for (const std::string& pn : fn->param_names)
            if (pn == hidden_local.name) is_param_name = true;
        if (!is_param_name && !hidden_local.name.empty() && hidden_local.name[0] == '_') return false;
    }
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
        // The callee's blocks are not blocks of the caller: a "scope" block would take a scope number that the
        // caller's own locals (numbered when the caller was parsed) do not expect. Its locals were renamed and
        // placed in the caller already.
        const std::function<void(Node*)> flatten_scopes = [&](Node* n) {
            if (n->kind == Node::Block && n->text == "scope") n->text.clear();
            for (Node& kid : n->kids) flatten_scopes(&kid);
        };
        flatten_scopes(&cloned);
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
        bool comdat = false;
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
            std::map<std::string, Type> ltypes;
            std::set<std::string> no_prop = shadowed;
            for (const Local& local : fn.locals) {
                ltypes[local.name] = local.type;
                if (address_taken(fn.body, local.name)) no_prop.insert(local.name);
            }
            const std::function<bool(const Node&)> sets_jmp = [&](const Node& n) {
                if (n.kind == Node::Call && n.text == "__okay_setjmp") return true;
                for (const Node& k : n.kids)
                    if (sets_jmp(k)) return true;
                return false;
            };
            if (!sets_jmp(fn.body)) prop_node(&fn.body, &known, &no_prop, &ltypes);
        }
        int max_args = count_args(fn.body);
        const int outgoing = max_args > 0 ? 32 + 8 * std::max(0, max_args - 4) : 0;
        const int local_end = fn.locals.empty() ? 0 : -fn.locals.back().offset;
        const int copy_bytes = struct_arg_space(fn.body, funcs, gen);
        int frame = local_end + 128 + copy_bytes + outgoing;
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
                    local.type.base == 6 || local.type.wide || local.type.is_bool || local.type.bits >= 0)
                    continue;
                if (address_taken(fn.body, local.name) || modified_in_place(fn.body, local.name)) continue;
                const auto found = range.find(local.name);
                if (found == range.end()) continue;
                // A parameter arrives in its register-home before the first use in the body.
                const bool is_param = i >= fn.param_base && i < fn.param_base + fn.nparams;
                spans.push_back({i, is_param ? -1 : found->second.first, found->second.second});
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
        for (int p = 0; p < fn.nparams; ++p) gen.locals[static_cast<size_t>(fn.param_base + p)].scope = 0;  // a parameter is in the outermost scope
        gen.temp_base = -(((local_end + 7) & ~7) + 8);
        gen.struct_copy = -((((local_end + 7) & ~7) + 128 + copy_bytes));
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
            if (reg >= 4) {
                // The fifth and later arguments are on the caller's stack, above the return address, the 32-byte
                // home area and the registers this function pushed first ([x64 calling convention]).
                const Local& slot = fn.locals[static_cast<size_t>(fn.param_base + p)];
                const int from = 16 + 8 * static_cast<int>(extra_regs.size()) + 8 * reg;
                const int size = gen.size_of(slot.type);
                gen.b(0x48);  // mov rax, [rbp+from]
                gen.b(0x8B);
                gen.b(0x85);
                gen.disp(from);
                if (slot.type.base == 3 && slot.type.pointers == 0 && !gen.fits_register(slot.type)) {
                    gen.b(0x48);  // mov rsi, rax; lea rdi, [rbp+off]; mov ecx, size; rep movsb
                    gen.b(0x89);
                    gen.b(0xC6);
                    gen.b(0x48);
                    gen.b(0x8D);
                    gen.b(0xBD);
                    gen.disp(slot.offset);
                    gen.b(0xB9);
                    gen.u32(static_cast<uint32_t>(size));
                    gen.b(0xF3);
                    gen.b(0xA4);
                } else {
                    const bool wide = slot.type.pointer() || size == 8;
                    if (size == 2) gen.b(0x66);
                    else if (wide) gen.b(0x48);
                    gen.b(size == 1 ? 0x88 : 0x89);
                    gen.b(0x85);
                    gen.disp(slot.offset);
                }
                continue;
            }
            const Local& local = fn.locals[static_cast<size_t>(fn.param_base + p)];
            const Type& param = local.type;
            const int param_size = gen.size_of(param);
            const bool big = param.base == 3 && param.pointers == 0 && !gen.fits_register(param);
            const bool fp = param.pointers == 0 && !param.is_ref && (param.base == 5 || param.base == 6);
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
            if (src >= 4) {
                gen.load_reg_local(local.reg, local.offset);  // a parameter kept in a register: from its stack copy
                continue;
            }
            const int machine = src == 0 ? 1 : src == 1 ? 2 : src == 2 ? 8 : 9;
            gen.mov_regs(local.reg, machine);
        }
        if (!gen.gen_stmt(fn.body)) {
            *error = (gen.error.empty() ? std::string("codegen failed") : gen.error) + " (in " + fn.name + ")";
            return false;
        }
        if (gen.debug_lines) gen.lines.push_back({static_cast<uint32_t>(gen.text.size()), fn.end_line});
        gen.epilogue();
        placed.push_back({fn.symbol.empty() ? fn.name : fn.symbol, start, static_cast<uint32_t>(gen.text.size()) - start, fn.exported, fn.is_static, g_comdat_inline && fn.comdat, {}});
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
        add_sym(fn.name, fn.start, 1, fn.is_static && !fn.comdat ? kClassStatic : kClassExternal, 0x20);
    for (const FuncDecl& fn : funcs) {
        if (fn.defined) continue;
        // A call names the decorated symbol when the function has one (a namespace function).
        const std::string extern_name = fn.symbol.empty() ? fn.name : fn.symbol;
        if (fn.imported) add_sym("__imp_" + extern_name, 0, 0, kClassExternal, 0);
        else add_sym(extern_name, 0, 0, kClassExternal, 0);
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
            // The slot before the table holds the address of the class's type_info (typeid of an object looks at vtable[-1]).
            if (!global.rtti_sym.empty())
                gen.relocs.push_back({1, static_cast<uint32_t>(gen.rdata.size()), global.rtti_sym, kRelAmd64Addr64});
            for (int k = 0; k < 8; ++k) gen.rdata.push_back(0);
            const uint32_t at = static_cast<uint32_t>(gen.rdata.size());
            for (size_t i = 0; i < global.slots.size(); ++i) {
                if (!global.slots[i].empty())
                    gen.relocs.push_back({1, at + static_cast<uint32_t>(i * 8), global.slots[i], kRelAmd64Addr64});
                for (int k = 0; k < 8; ++k) gen.rdata.push_back(0);
            }
            add_sym(global.name, at, 0, kClassExternal, 0);
            continue;
        }
        if (global.is_extern) {
            bool used = false;
            for (const auto& reloc : gen.relocs)
                if (reloc.symbol == global.name) used = true;
            if (used) add_sym(global.name, 0, 0, kClassExternal, 0);
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
            for (const auto& slot : global.addr_relocs) gen.relocs.push_back({3, at + slot.first, slot.second, kRelAmd64Addr64});
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
            if (sym.name != global.name || global.is_extern) continue;
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
        } else if (named.section == 3) {
            sec = data_section - 1;
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
    if (g_comdat_inline) {
        std::vector<std::pair<uint32_t, uint32_t>> comdat_cuts;  // (start, size) of what left .text
        // Every function marked comdat moves into a section of its own (.text$mn, COMDAT, "pick any"): the part of .text it
        // occupied, its relocations and its symbol go with it. The linker keeps the first copy of each and drops the others.
        for (const PlacedFn& fn : placed) {
            if (!fn.comdat) continue;
            Section part;
            part.name = ".text$mn";
            part.virtual_size = fn.size;
            part.data.assign(object.sections[0].data.begin() + fn.start, object.sections[0].data.begin() + fn.start + fn.size);
            part.characteristics = kScnCntCode | kScnMemExecute | kScnMemRead | kScnAlign16 | kScnLnkComdat;
            const uint32_t new_index = static_cast<uint32_t>(object.sections.size()) + 1;
            std::vector<Relocation> keep;
            for (const Relocation& r : object.sections[0].relocations) {
                if (r.offset >= fn.start && r.offset < fn.start + fn.size) {
                    Relocation moved = r;
                    moved.offset = r.offset - fn.start;
                    part.relocations.push_back(moved);
                } else {
                    keep.push_back(r);
                }
            }
            object.sections[0].relocations = std::move(keep);
            comdat_cuts.push_back({fn.start, fn.size});
            for (Symbol& sym : object.symbols)
                if (sym.name == fn.name && sym.section == 1) {
                    sym.section = static_cast<int16_t>(new_index);
                    sym.value = 0;
                }
            Symbol sec_sym;
            sec_sym.name = ".text$mn";
            sec_sym.section = static_cast<int16_t>(new_index);
            sec_sym.storage = kClassStatic;
            sec_sym.aux.assign(18, 0);
            sec_sym.aux[0] = static_cast<uint8_t>(fn.size);
            sec_sym.aux[1] = static_cast<uint8_t>(fn.size >> 8);
            sec_sym.aux[2] = static_cast<uint8_t>(fn.size >> 16);
            sec_sym.aux[3] = static_cast<uint8_t>(fn.size >> 24);
            sec_sym.aux[4] = static_cast<uint8_t>(part.relocations.size());
            sec_sym.aux[5] = static_cast<uint8_t>(part.relocations.size() >> 8);
            sec_sym.aux[14] = 2;  // IMAGE_COMDAT_SELECT_ANY
            object.sections.push_back(std::move(part));
            object.symbols.push_back(std::move(sec_sym));
        }
        std::sort(comdat_cuts.begin(), comdat_cuts.end());
        for (size_t ci = comdat_cuts.size(); ci-- > 0;) {
            const uint32_t cut_at = comdat_cuts[ci].first;
            const uint32_t cut_size = comdat_cuts[ci].second;
            Section& text_section = object.sections[0];
            text_section.data.erase(text_section.data.begin() + cut_at, text_section.data.begin() + cut_at + cut_size);
            text_section.virtual_size = static_cast<uint32_t>(text_section.data.size());
            for (Relocation& r : text_section.relocations)
                if (r.offset >= cut_at + cut_size) r.offset -= cut_size;
            for (Symbol& sym : object.symbols)
                if (sym.section == 1 && sym.value >= cut_at + cut_size) sym.value -= cut_size;
        }
    }
    if (g_crt_init_table) {
        // .CRT$XCU: one pointer to this file's __okay_init, which the startup code calls with the other files' ones.
        uint32_t init_symbol = 0;
        bool have_init = false;
        for (size_t k = 0; k < object.symbols.size(); ++k)
            if (object.symbols[k].name == "__okay_init" && object.symbols[k].section > 0) {
                init_symbol = static_cast<uint32_t>(k);
                have_init = true;
            }
        if (have_init) {
            Section table;
            table.name = ".CRT$XCU";
            table.virtual_size = 8;
            table.data.assign(8, 0);
            table.characteristics = kScnCntInitData | kScnMemRead | kScnAlign8;
            Relocation r;
            r.offset = 0;
            r.symbol = init_symbol;
            r.type = kRelAmd64Addr64;
            table.relocations.push_back(r);
            object.sections.push_back(std::move(table));
        }
    }
    out->object = std::move(object);
    (void)xdata_section;
    return true;
}

}  // namespace

Result<CompileOutput> compile_c(const CompileInput& input) {
    g_comdat_inline = input.comdat_inline;
    g_crt_init_table = input.crt_init_table;
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
    // A quoted include in the main file is looked up next to that file first ([cpp.include]).
    std::string main_dir;
    {
        const size_t slash = input.name.find_last_of("/\\");
        if (slash != std::string::npos) main_dir = input.name.substr(0, slash);
    }
    const std::string expanded = pp.run(stripped, main_dir);
    if (!pp.error.empty()) return Result<CompileOutput>::fail(pp.error);
    if (const char* dump = std::getenv("OKAY_PREPROCESS_TO")) {
        // A debugging aid: the text after preprocessing (the line numbers in parse errors that name a "line" refer to it).
        std::FILE* f = std::fopen(dump, "wb");
        if (f != nullptr) {
            std::fwrite(expanded.data(), 1, expanded.size(), f);
            std::fclose(f);
        }
    }
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
        // __int64 is a 64-bit signed integer, __int32 an int (the Microsoft spellings).
        Type wide64;
        wide64.base = 1;
        wide64.wide = true;
        parser.typedefs["__int64"] = wide64;
        Type int32;
        int32.base = 1;
        parser.typedefs["__int32"] = int32;
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
