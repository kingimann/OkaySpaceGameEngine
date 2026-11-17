#include "okayms/compile.hpp"
#include "okayms/unwind.hpp"

#include <cctype>
#include <cstring>
#include <fstream>
#include <map>

namespace okayms {
namespace {

enum Tok {
    TEnd, TIdent, TNumber, TString,
    TLParen, TRParen, TLBrace, TRBrace, TSemi, TComma, TStar, TAmp,
    TPlus, TMinus, TSlash, TPercent, TAssign, TEq, TNe, TLt, TGt, TLe, TGe,
    TAndAnd, TOrOr, TNot, TLBrack, TRBrack, TDot, TColon,
    TInt, TVoid, TChar, TIf, TElse, TWhile, TFor, TDo, TSwitch, TCase, TDefault, TBreak, TReturn,
    TStruct, TUnsigned, TSigned, TLong, TShort, TDeclspec
};

struct Token {
    Tok kind = TEnd;
    std::string text;
    uint64_t number = 0;
};

struct Type {
    int pointers = 0;
    int base = 1;  // 0 void, 1 int, 2 char, 3 struct
    int array_len = -1;
    int struct_id = -1;
    bool pointer() const { return pointers > 0; }
    bool is_array() const { return array_len >= 0; }
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
};

struct Global {
    std::string name;
    Type type{};
    bool bss = true;
    uint32_t init = 0;
};

struct Node {
    enum Kind {
        Block, Return, If, While, For, Do, Switch, Case, Break, ExprStmt,
        IntLit, StringLit, Name, Unary, Binary, Assign, Call, Index, Field
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
};

struct FuncDecl {
    std::string name;
    Type ret{};
    std::vector<Type> params;
    std::vector<std::string> param_names;
    bool imported = false;
    bool exported = false;
    bool defined = false;
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
                    stack.push_back({parent, taken, false});
                } else if (word("else")) {
                    if (stack.empty() || stack.back().saw_else) {
                        error = "else without if";
                        --depth;
                        return "";
                    }
                    stack.back().saw_else = true;
                    stack.back().taken = stack.back().parent && !stack.back().taken;
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
    if (text == "return") return TReturn;
    if (text == "unsigned") return TUnsigned;
    if (text == "signed") return TSigned;
    if (text == "long") return TLong;
    if (text == "short") return TShort;
    if (text == "__declspec") return TDeclspec;
    return TIdent;
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
            if (!any) {
                *error = "bad number";
                return {};
            }
            tok.kind = TNumber;
            tok.number = value;
            i = j;
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
        if (is(TStruct)) {
            next();
            if (!is(TIdent)) {
                error = "expected a struct name";
                return false;
            }
            const int id = find_struct(cur().text);
            if (id < 0) {
                error = "unknown struct " + cur().text;
                return false;
            }
            type->base = 3;
            type->struct_id = id;
            next();
            return true;
        }
        while (is(TUnsigned) || is(TSigned) || is(TLong) || is(TShort) || is(TInt) || is(TVoid) || is(TChar)) {
            if (is(TVoid)) type->base = 0;
            else if (is(TChar)) type->base = 2;
            else type->base = 1;
            saw = true;
            next();
        }
        if (!saw) {
            error = "expected a type";
            return false;
        }
        return true;
    }

    bool parse_expr(int min_prec, Node* out);
    bool parse_unary(Node* out);
    bool parse_stmt(Node* out, std::vector<Local>* locals);
    bool parse_block(Node* out, std::vector<Local>* locals);

    int prec(Tok op) const {
        if (op == TAssign) return 1;
        if (op == TOrOr) return 2;
        if (op == TAndAnd) return 3;
        if (op == TEq || op == TNe) return 4;
        if (op == TLt || op == TGt || op == TLe || op == TGe) return 5;
        if (op == TPlus || op == TMinus) return 6;
        if (op == TStar || op == TSlash || op == TPercent) return 7;
        return -1;
    }

    bool parse_struct_def() {
        next();
        if (!is(TIdent)) {
            error = "expected a struct name";
            return false;
        }
        StructDef def;
        def.name = cur().text;
        next();
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
            if (!expect(TSemi, "';'")) return false;
            def.fields.push_back(field);
        }
        if (!expect(TRBrace, "'}'")) return false;
        if (!expect(TSemi, "';'")) return false;
        int offset = 0;
        int align = 1;
        for (Field& field : def.fields) {
            int field_align = align_of(field.type);
            int field_size = size_of(field.type);
            if (field_align < 1) field_align = 1;
            if (field_size < 1) field_size = 1;
            offset = (offset + field_align - 1) & ~(field_align - 1);
            field.offset = offset;
            offset += field_size;
            if (field_align > align) align = field_align;
        }
        def.align = align;
        def.size = (offset + align - 1) & ~(align - 1);
        structs.push_back(std::move(def));
        return true;
    }

    bool parse() {
        while (!is(TEnd)) {
            if (is(TStruct) && at + 2 < tokens.size() && tokens[at + 1].kind == TIdent && tokens[at + 2].kind == TLBrace) {
                if (!parse_struct_def()) return false;
                continue;
            }
            bool imported = false;
            bool exported = false;
            Type type;
            if (!parse_type(&type, &imported, &exported)) return false;
            while (accept(TStar)) ++type.pointers;
            if (!is(TIdent)) {
                error = "expected a name";
                return false;
            }
            FuncDecl fn;
            fn.name = cur().text;
            fn.ret = type;
            fn.imported = imported;
            fn.exported = exported;
            next();
            if (accept(TLBrack)) {
                if (!is(TNumber)) {
                    error = "expected an array length";
                    return false;
                }
                type.array_len = static_cast<int>(cur().number);
                next();
                if (!expect(TRBrack, "']'")) return false;
            }
            if (!is(TLParen)) {
                Global global;
                global.name = fn.name;
                global.type = type;
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
            next();
            if (!(is(TVoid) && tokens[at + 1].kind == TRParen)) {
                if (!is(TRParen)) {
                    do {
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
                        fn.params.push_back(pt);
                        fn.param_names.push_back(pname);
                    } while (accept(TComma));
                }
            } else {
                next();
            }
            if (!expect(TRParen, "')'")) return false;
            if (accept(TSemi)) {
                fn.defined = false;
                funcs.push_back(std::move(fn));
                continue;
            }
            std::vector<Local> locals;
            for (size_t p = 0; p < fn.param_names.size(); ++p) {
                if (fn.param_names[p].empty()) continue;
                Local local;
                local.name = fn.param_names[p];
                local.type = fn.params[p];
                place_local(&locals, local);
            }
            fn.nparams = static_cast<int>(locals.size());
            if (!parse_block(&fn.body, &locals)) return false;
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
    while (!is(TRBrace) && !is(TEnd)) {
        Node stmt;
        if (!parse_stmt(&stmt, locals)) return false;
        out->kids.push_back(std::move(stmt));
    }
    return expect(TRBrace, "'}'");
}

bool Parser::parse_stmt(Node* out, std::vector<Local>* locals) {
    if (is(TLBrace)) return parse_block(out, locals);
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
    if (is(TInt) || is(TVoid) || is(TChar) || is(TUnsigned) || is(TSigned) || is(TLong) || is(TShort) || is(TStruct)) {
        Type type;
        bool imp = false;
        bool exp = false;
        if (!parse_type(&type, &imp, &exp)) return false;
        while (accept(TStar)) ++type.pointers;
        if (!is(TIdent)) {
            error = "expected a local name";
            return false;
        }
        Local local;
        local.name = cur().text;
        local.type = type;
        next();
        if (accept(TLBrack)) {
            if (!is(TNumber)) {
                error = "expected an array length";
                return false;
            }
            local.type.array_len = static_cast<int>(cur().number);
            next();
            if (!expect(TRBrack, "']'")) return false;
        }
        place_local(locals, local);
        return expect(TSemi, "';'");
    }
    out->kind = Node::ExprStmt;
    Node expr;
    if (!parse_expr(0, &expr)) return false;
    out->kids.push_back(std::move(expr));
    return expect(TSemi, "';'");
}

bool Parser::parse_unary(Node* out) {
    if (is(TNumber)) {
        out->kind = Node::IntLit;
        out->number = cur().number;
        out->type.base = 1;
        next();
    } else if (is(TString)) {
        out->kind = Node::StringLit;
        out->text = cur().text;
        out->type.pointers = 1;
        out->type.base = 2;
        next();
    } else if (is(TIdent)) {
        out->text = cur().text;
        next();
        if (accept(TLParen)) {
            out->kind = Node::Call;
            if (!accept(TRParen)) {
                do {
                    Node arg;
                    if (!parse_expr(0, &arg)) return false;
                    out->kids.push_back(std::move(arg));
                } while (accept(TComma));
                if (!expect(TRParen, "')'")) return false;
            }
        } else {
            out->kind = Node::Name;
        }
    } else if (is(TLParen)) {
        next();
        if (!parse_expr(0, out)) return false;
        if (!expect(TRParen, "')'")) return false;
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
    while (is(TLBrack) || is(TDot)) {
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
            next();
            if (!is(TIdent)) {
                error = "expected a field name";
                return false;
            }
            Node wrapped;
            wrapped.kind = Node::Field;
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
        Node right;
        const int next_min = op == TAssign ? p : p + 1;
        if (!parse_expr(next_min, &right)) return false;
        Node bin;
        bin.kind = op == TAssign ? Node::Assign : Node::Binary;
        bin.op = op;
        bin.kids.push_back(std::move(*out));
        bin.kids.push_back(std::move(right));
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
    std::vector<std::vector<size_t>> break_stack;
    Type last_type{};

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
        for (Local& local : locals)
            if (local.name == name) return &local;
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
    void load_from_rax(int size);
    void epilogue() {
        b(0x48);
        b(0x89);
        b(0xEC);
        b(0x5D);
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
        if (!gen_addr(node.kids[0])) return false;
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
        if (!gen_addr(node.kids[0])) return false;
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

void Gen::load_from_rax(int size) {
    if (size >= 8) {
        b(0x48);
        b(0x8B);
        b(0x00);
    } else if (size == 1) {
        b(0x0F);
        b(0xB6);
        b(0x00);
    } else {
        b(0x8B);
        b(0x00);
    }
}

bool Gen::gen_expr(const Node& node) {
    if (!error.empty()) return false;
    if (node.kind == Node::IntLit) {
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
            if (local->type.is_array()) {
                if (!gen_addr(node)) return false;
                last_type = element_type(local->type);
                last_type.pointers += 1;
                return true;
            }
            load_slot(local->offset, local->type.pointer() || size_of(local->type) == 8);
            last_type = local->type;
            return true;
        }
        if (Global* global = find_global(node.text)) {
            const bool wide = global->type.pointer() || size_of(global->type) == 8;
            if (wide) b(0x48);
            b(0x8B);
            b(0x05);
            relocs.push_back({0, static_cast<uint32_t>(text.size()), global->name, kRelAmd64Rel32});
            u32(0);
            last_type = global->type;
            return true;
        }
        error = "unknown name " + node.text;
        return false;
    }
    if (node.kind == Node::Index || node.kind == Node::Field) {
        if (!gen_addr(node)) return false;
        const int size = std::max(1, size_of(last_type));
        load_from_rax(size);
        return true;
    }
    if (node.kind == Node::Unary) {
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
            load_from_rax(std::max(1, size_of(elem)));
            last_type = elem;
            return true;
        }
        error = "bad unary operator";
        return false;
    }
    if (node.kind == Node::Assign) {
        if (!gen_expr(node.kids[1])) return false;
        const int slot = spill();
        if (!error.empty()) return false;
        if (!gen_addr(node.kids[0])) return false;
        const int size = std::max(1, size_of(last_type));
        b(0x48);
        b(0x89);
        b(0xC1);
        b(0x48);
        b(0x8B);
        b(0x85);
        disp(slot);
        --temp_depth;
        if (size >= 8) {
            b(0x48);
            b(0x89);
            b(0x01);
        } else if (size == 1) {
            b(0x88);
            b(0x01);
        } else {
            b(0x89);
            b(0x01);
        }
        return true;
    }
    if (node.kind == Node::Call) {
        FuncDecl* fn = find_func(node.text);
        if (!fn) {
            error = "unknown function " + node.text;
            return false;
        }
        if (static_cast<int>(node.kids.size()) != static_cast<int>(fn->params.size()) && !fn->params.empty()) {
            // A declaration with zero params accepts the call's arguments only
            // when the declaration also has zero. Checked below.
        }
        if (node.kids.size() != fn->params.size()) {
            error = "wrong number of arguments to " + node.text;
            return false;
        }
        const int depth_before = temp_depth;
        std::vector<int> saved;
        for (size_t arg_i = 0; arg_i < node.kids.size(); ++arg_i) {
            const Type param = fn->params[arg_i];
            if (param.base == 3 && param.pointers == 0 && !fits_register(param)) {
                if (!gen_addr(node.kids[arg_i])) return false;
            } else if (param.base == 3 && param.pointers == 0) {
                if (!gen_addr(node.kids[arg_i])) return false;
                b(0x48);
                b(0x8B);
                b(0x00);
            } else if (!gen_expr(node.kids[arg_i])) {
                return false;
            }
            saved.push_back(spill());
            if (!error.empty()) return false;
        }
        for (int i = static_cast<int>(saved.size()) - 1; i >= 4; --i) {
            b(0x48);
            b(0x8B);
            b(0x85);
            disp(saved[i]);
            b(0x48);
            b(0x89);
            b(0x84);
            b(0x24);
            u32(static_cast<uint32_t>(32 + (i - 4) * 8));
        }
        auto load_arg = [&](int index, uint8_t rex, uint8_t modrm) {
            if (index >= static_cast<int>(saved.size()) || index >= 4) return;
            b(rex);
            b(0x8B);
            b(modrm);
            disp(saved[index]);
        };
        // 64-bit loads: the spill stored the whole register, and the
        // calling convention passes each argument in a 64-bit register.
        if (saved.size() > 3) load_arg(3, 0x4C, 0x8D);
        if (saved.size() > 2) load_arg(2, 0x4C, 0x85);
        if (saved.size() > 1) load_arg(1, 0x48, 0x95);
        if (saved.size() > 0) load_arg(0, 0x48, 0x8D);
        temp_depth = depth_before;
        if (fn->imported) {
            b(0xFF);
            b(0x15);
            relocs.push_back({0, static_cast<uint32_t>(text.size()), "__imp_" + fn->name, kRelAmd64Rel32});
            u32(0);
        } else {
            b(0xE8);
            relocs.push_back({0, static_cast<uint32_t>(text.size()), fn->name, kRelAmd64Rel32});
            u32(0);
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
        reload_ecx(off);
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
        uint8_t setcc = 0x94;
        if (node.op == TEq) setcc = 0x94;
        else if (node.op == TNe) setcc = 0x95;
        else if (node.op == TLt) setcc = 0x9C;
        else if (node.op == TGt) setcc = 0x9F;
        else if (node.op == TLe) setcc = 0x9E;
        else if (node.op == TGe) setcc = 0x9D;
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
        for (const Node& kid : node.kids)
            if (!gen_stmt(kid)) return false;
        return true;
    }
    if (node.kind == Node::Return) {
        if (!node.kids.empty() && !gen_expr(node.kids[0])) return false;
        epilogue();
        return true;
    }
    if (node.kind == Node::ExprStmt) return gen_expr(node.kids[0]);
    if (node.kind == Node::If) {
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

bool emit_functions(std::vector<FuncDecl>& funcs, const std::vector<StructDef>& structs,
                    const std::vector<Global>& globals, std::string* error, CompileOutput* out) {
    Gen gen(funcs);
    gen.structs = &structs;
    gen.globals = &globals;
    struct PlacedFn {
        std::string name;
        uint32_t start = 0;
        uint32_t size = 0;
        bool exported = false;
    };
    std::vector<PlacedFn> placed;
    std::vector<uint32_t> unwind_offs;
    for (FuncDecl& fn : funcs) {
        if (!fn.defined) continue;
        int max_args = count_args(fn.body);
        const int outgoing = max_args > 0 ? 32 + 8 * std::max(0, max_args - 4) : 0;
        const int local_end = fn.locals.empty() ? 0 : -fn.locals.back().offset;
        int frame = local_end + 64 + outgoing;
        frame = (frame + 15) & ~15;
        if (frame < 0x20) frame = 0x20;
        gen.locals = fn.locals;
        gen.temp_base = -(((local_end + 7) & ~7) + 8);
        gen.frame = frame;
        const uint32_t start = static_cast<uint32_t>(gen.text.size());
        gen.b(0x55);
        gen.b(0x48);
        gen.b(0x89);
        gen.b(0xE5);
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
        for (int p = fn.nparams - 1; p >= 0 && p < 4; --p) {
            const Type& param = fn.locals[p].type;
            const int param_size = gen.size_of(param);
            const bool big = param.base == 3 && param.pointers == 0 && !gen.fits_register(param);
            const bool wide = param.pointer() || param_size == 8;
            if (big) {
                if (p == 0) { gen.b(0x48); gen.b(0x89); gen.b(0xCE); }
                else if (p == 1) { gen.b(0x48); gen.b(0x89); gen.b(0xD6); }
                else if (p == 2) { gen.b(0x4C); gen.b(0x89); gen.b(0xC6); }
                else { gen.b(0x4C); gen.b(0x89); gen.b(0xCE); }
                gen.b(0x48); gen.b(0x8D); gen.b(0xBD); gen.disp(fn.locals[p].offset);
                gen.b(0xB9); gen.u32(static_cast<uint32_t>(param_size));
                gen.b(0xF3); gen.b(0xA4);
            } else {
                const uint8_t rex = p >= 2 ? (wide ? 0x4C : 0x44) : (wide ? 0x48 : 0);
                const uint8_t modrm = p == 0 || p == 3 ? 0x8D : (p == 1 ? 0x95 : 0x85);
                if (rex) gen.b(rex);
                gen.b(0x89);
                gen.b(modrm);
                gen.disp(fn.locals[p].offset);
            }
        }
        if (!gen.gen_stmt(fn.body)) {
            *error = gen.error.empty() ? "codegen failed" : gen.error;
            return false;
        }
        gen.epilogue();
        placed.push_back({fn.name, start, static_cast<uint32_t>(gen.text.size()) - start, fn.exported});
        if (fn.exported) out->exports.push_back(fn.name);
        UnwindPrologue prolog;
        prolog.size = large_alloc ? 11 : 8;
        prolog.frame_register = 5;
        prolog.frame_at = 4;
        prolog.alloc = static_cast<uint32_t>(frame);
        prolog.alloc_at = prolog.size;
        prolog.pushes.push_back({5, 1});
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
    for (const PlacedFn& fn : placed) add_sym(fn.name, fn.start, 1, kClassExternal, 0x20);
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
            add_sym(global.name, bss_size, 0, kClassExternal, 0);
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
            add_sym(global.name, at, 0, kClassExternal, 0);
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
    Parser parser{tokens, 0, {}, {}, {}, {}, {}};
    if (!parser.parse()) return Result<CompileOutput>::fail(parser.error.empty() ? "parse failed" : parser.error);
    CompileOutput output;
    std::string error;
    if (!emit_functions(parser.funcs, parser.structs, parser.globals, &error, &output))
        return Result<CompileOutput>::fail(error);
    return Result<CompileOutput>::success(std::move(output));
}

}  // namespace okayms
