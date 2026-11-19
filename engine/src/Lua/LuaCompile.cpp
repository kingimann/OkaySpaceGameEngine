// Lexer, parser and bytecode compiler for our own Lua 5.4. Written from the Lua 5.4
// reference manual (lexical conventions, the syntax chapter and operator priorities).
#include "LuaCore.hpp"
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>

namespace okay::lua {
namespace {

struct CompileError { std::string msg; };

// ============================================================ lexer
enum Tk {
    TK_EOF, TK_NAME, TK_STRING, TK_INT, TK_NUM,
    TK_AND, TK_BREAK, TK_DO, TK_ELSE, TK_ELSEIF, TK_END, TK_FALSE, TK_FOR, TK_FUNCTION, TK_GOTO, TK_IF, TK_IN,
    TK_LOCAL, TK_NIL, TK_NOT, TK_OR, TK_REPEAT, TK_RETURN, TK_THEN, TK_TRUE, TK_UNTIL, TK_WHILE,
    TK_IDIV, TK_CONCAT, TK_DOTS, TK_EQ, TK_GE, TK_LE, TK_NE, TK_SHL, TK_SHR, TK_DBCOLON,
    TK_CHAR   // single-character token in `ch`
};

struct Token { Tk t = TK_EOF; std::string s; std::int64_t i = 0; double n = 0; char ch = 0; int line = 1; };

const char* const kKeywords[] = {"and","break","do","else","elseif","end","false","for","function","goto","if","in",
    "local","nil","not","or","repeat","return","then","true","until","while"};

struct Lexer {
    const std::string& src;
    std::string chunk;
    std::size_t p = 0;
    int line = 1;
    Lexer(const std::string& s, std::string c) : src(s), chunk(std::move(c)) {
        if (src.compare(0, 2, "#!") == 0) while (p < src.size() && src[p] != '\n') ++p;
    }
    [[noreturn]] void Fail(const std::string& m, const std::string& near = "") {
        throw CompileError{chunk + ":" + std::to_string(line) + ": " + m + (near.empty() ? "" : " near '" + near + "'")};
    }
    int Cur() const { return p < src.size() ? (unsigned char)src[p] : -1; }
    int Peek(std::size_t k) const { return p + k < src.size() ? (unsigned char)src[p + k] : -1; }
    static bool IsAlpha(int c) { return c == '_' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
    static bool IsDigit(int c) { return c >= '0' && c <= '9'; }
    static bool IsHex(int c) { return IsDigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }

    // long bracket level at p ('[' already at p): returns level or -1
    int LongLevel() const {
        std::size_t q = p + 1; int lv = 0;
        while (q < src.size() && src[q] == '=') { ++lv; ++q; }
        return (q < src.size() && src[q] == '[') ? lv : -1;
    }
    std::string ReadLong(int lv, const char* what) {
        int startLine = line;
        p += lv + 2;
        if (Cur() == '\r') { ++p; if (Cur() == '\n') ++p; ++line; }
        else if (Cur() == '\n') { ++p; if (Cur() == '\r') ++p; ++line; }
        std::string out;
        for (;;) {
            if (p >= src.size()) { line = startLine; Fail(std::string("unfinished long ") + what, "<eof>"); }
            char c = src[p];
            if (c == ']') {
                std::size_t q = p + 1; int l2 = 0;
                while (q < src.size() && src[q] == '=') { ++l2; ++q; }
                if (l2 == lv && q < src.size() && src[q] == ']') { p = q + 1; return out; }
                out += c; ++p;
            } else if (c == '\n') { out += '\n'; ++line; ++p; }
            else { out += c; ++p; }
        }
    }
    static void PutUtf8(std::string& o, unsigned long c) {
        if (c < 0x80) o += (char)c;
        else if (c < 0x800) { o += (char)(0xC0 | (c >> 6)); o += (char)(0x80 | (c & 0x3F)); }
        else if (c < 0x10000) { o += (char)(0xE0 | (c >> 12)); o += (char)(0x80 | ((c >> 6) & 0x3F)); o += (char)(0x80 | (c & 0x3F)); }
        else { o += (char)(0xF0 | (c >> 18)); o += (char)(0x80 | ((c >> 12) & 0x3F)); o += (char)(0x80 | ((c >> 6) & 0x3F)); o += (char)(0x80 | (c & 0x3F)); }
    }
    std::string ReadString(char q) {
        ++p;
        std::string out;
        for (;;) {
            int c = Cur();
            if (c < 0) Fail("unfinished string", "<eof>");
            if (c == '\n') Fail("unfinished string", std::string(1, q) + out);
            ++p;
            if (c == q) return out;
            if (c != '\\') { out += (char)c; continue; }
            c = Cur();
            switch (c) {
                case 'n': out += '\n'; ++p; break;
                case 't': out += '\t'; ++p; break;
                case 'r': out += '\r'; ++p; break;
                case 'a': out += '\a'; ++p; break;
                case 'b': out += '\b'; ++p; break;
                case 'f': out += '\f'; ++p; break;
                case 'v': out += '\v'; ++p; break;
                case '\\': out += '\\'; ++p; break;
                case '"': out += '"'; ++p; break;
                case '\'': out += '\''; ++p; break;
                case '\n': out += '\n'; ++line; ++p; break;
                case 'x': {
                    ++p; int v = 0;
                    for (int k = 0; k < 2; ++k) {
                        if (!IsHex(Cur())) Fail("hexadecimal digit expected", "\\x");
                        int d = Cur(); v = v * 16 + (IsDigit(d) ? d - '0' : (d | 32) - 'a' + 10); ++p;
                    }
                    out += (char)v; break;
                }
                case 'z': {
                    ++p;
                    while (p < src.size() && std::isspace((unsigned char)src[p])) { if (src[p] == '\n') ++line; ++p; }
                    break;
                }
                case 'u': {
                    ++p;
                    if (Cur() != '{') Fail("missing '{' in \\u{xxxx}", "\\u");
                    ++p; unsigned long v = 0; int n = 0;
                    while (IsHex(Cur())) { int d = Cur(); v = v * 16 + (IsDigit(d) ? d - '0' : (d | 32) - 'a' + 10); ++p; ++n; if (v > 0x10FFFF) Fail("UTF-8 value too large", "\\u"); }
                    if (n == 0 || Cur() != '}') Fail("hexadecimal digit expected", "\\u{");
                    ++p; PutUtf8(out, v); break;
                }
                default:
                    if (IsDigit(c)) {
                        int v = 0, n = 0;
                        while (n < 3 && IsDigit(Cur())) { v = v * 10 + (Cur() - '0'); ++p; ++n; }
                        if (v > 255) Fail("decimal escape too large", "\\" + std::to_string(v));
                        out += (char)v;
                    } else Fail("invalid escape sequence", std::string("\\") + (c >= 0 ? (char)c : ' '));
            }
        }
    }
    Token Next() {
        Token tk;
        for (;;) {
            int c = Cur();
            tk.line = line;
            if (c < 0) { tk.t = TK_EOF; return tk; }
            if (c == '\n') { ++line; ++p; continue; }
            if (c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\v') { ++p; continue; }
            if (c == '-' && Peek(1) == '-') {
                p += 2;
                if (Cur() == '[') { int lv = LongLevel(); if (lv >= 0) { ReadLong(lv, "comment"); continue; } }
                while (p < src.size() && src[p] != '\n') ++p;
                continue;
            }
            break;
        }
        int c = Cur();
        tk.line = line;
        if (IsAlpha(c)) {
            std::size_t s = p;
            while (IsAlpha(Cur()) || IsDigit(Cur())) ++p;
            tk.s = src.substr(s, p - s);
            for (int i = 0; i < 22; ++i) if (tk.s == kKeywords[i]) { tk.t = (Tk)(TK_AND + i); return tk; }
            tk.t = TK_NAME; return tk;
        }
        if (IsDigit(c) || (c == '.' && IsDigit(Peek(1)))) {
            std::size_t s = p;
            bool hex = (c == '0' && (Peek(1) == 'x' || Peek(1) == 'X'));
            if (hex) p += 2;
            for (;;) {
                int d = Cur();
                if (hex ? (d == 'p' || d == 'P') : (d == 'e' || d == 'E')) {
                    ++p; if (Cur() == '+' || Cur() == '-') ++p; continue;
                }
                if (IsHex(d) || d == '.') { ++p; continue; }
                break;
            }
            if (IsAlpha(Cur())) { while (IsAlpha(Cur()) || IsDigit(Cur())) ++p; Fail("malformed number", src.substr(s, p - s)); }
            std::string txt = src.substr(s, p - s);
            Value v;
            if (!ParseNumber(txt, v)) Fail("malformed number", txt);
            if (v.t == T::Int) { tk.t = TK_INT; tk.i = v.i; } else { tk.t = TK_NUM; tk.n = v.n; }
            return tk;
        }
        if (c == '"' || c == '\'') { tk.s = ReadString((char)c); tk.t = TK_STRING; return tk; }
        if (c == '[') {
            int lv = LongLevel();
            if (lv >= 0) { tk.s = ReadLong(lv, "string"); tk.t = TK_STRING; return tk; }
            if (Peek(1) == '=') Fail("invalid long string delimiter", "[=");
        }
        auto two = [&](char a, char b) { return c == a && Peek(1) == b; };
        if (two('=', '=')) { p += 2; tk.t = TK_EQ; return tk; }
        if (two('>', '=')) { p += 2; tk.t = TK_GE; return tk; }
        if (two('<', '=')) { p += 2; tk.t = TK_LE; return tk; }
        if (two('~', '=')) { p += 2; tk.t = TK_NE; return tk; }
        if (two('<', '<')) { p += 2; tk.t = TK_SHL; return tk; }
        if (two('>', '>')) { p += 2; tk.t = TK_SHR; return tk; }
        if (two('/', '/')) { p += 2; tk.t = TK_IDIV; return tk; }
        if (two(':', ':')) { p += 2; tk.t = TK_DBCOLON; return tk; }
        if (two('.', '.')) {
            if (Peek(2) == '.') { p += 3; tk.t = TK_DOTS; return tk; }
            p += 2; tk.t = TK_CONCAT; return tk;
        }
        if (std::strchr("+-*/%^#&~|<>=(){}[];:,.", c)) { ++p; tk.t = TK_CHAR; tk.ch = (char)c; return tk; }
        Fail("unexpected symbol", std::string(1, (char)c));
    }
};

// ============================================================ AST
struct VarInfo { std::string name; bool captured = false, isConst = false; int slot = -1; };
struct FuncBody;
struct Expr;
using EP = std::unique_ptr<Expr>;

enum class EK { Nil, True, False, Int, Num, Str, Vararg, Func, Local, Up, Global, Index, Call, Method, Bin, Un, And, Or, Table, Paren };
enum BinOp { B_ADD, B_SUB, B_MUL, B_DIV, B_MOD, B_IDIV, B_POW, B_BAND, B_BOR, B_BXOR, B_SHL, B_SHR, B_CONCAT, B_EQ, B_NE, B_LT, B_LE, B_GT, B_GE };
enum UnOp { U_NEG, U_NOT, U_LEN, U_BNOT };

struct TItem { int kind = 0; EP key, val; };   // 0 positional, 1 keyed
struct Expr {
    EK k; int line = 0;
    std::int64_t i = 0; double n = 0; std::string s;
    VarInfo* var = nullptr; int up = -1;
    int op = 0;
    EP a, b;
    std::vector<EP> args;
    std::vector<TItem> items;
    std::unique_ptr<FuncBody> fn;
    explicit Expr(EK kk, int ln) : k(kk), line(ln) {}
};

struct Stmt;
using SP = std::unique_ptr<Stmt>;
enum class SK { Local, Assign, CallS, Do, While, Repeat, If, NumFor, GenFor, LocalFunc, Return, Break };
struct Stmt {
    SK k; int line = 0;
    std::vector<VarInfo*> vars;
    std::vector<EP> targets, exprs;           // Assign: targets; Local/Return/etc: exprs
    std::vector<SP> body, body2;              // block / else block
    std::vector<EP> conds;                    // If: condition list
    std::vector<std::vector<SP>> blocks;      // If: blocks per cond
    std::unique_ptr<FuncBody> fn;
    Stmt(SK kk, int ln) : k(kk), line(ln) {}
};

struct UpInfo { bool fromLocal; VarInfo* var; int parentIdx; std::string name; };
struct FuncBody {
    std::vector<VarInfo*> params;
    bool vararg = false;
    std::vector<SP> body;
    std::vector<UpInfo> ups;
    std::string name;
    int line = 0;
};

// ============================================================ parser
struct FuncScope {
    FuncScope* parent = nullptr;
    FuncBody* body = nullptr;
    std::vector<std::vector<VarInfo*>> blocks;
};

struct Parser {
    Lexer lx;
    Token tok, ahead;
    bool hasAhead = false;
    std::vector<std::unique_ptr<VarInfo>> vars;
    FuncScope* fs = nullptr;
    int depth = 0;

    Parser(const std::string& s, const std::string& chunk) : lx(s, chunk) { Advance(); }

    [[noreturn]] void Fail(const std::string& m) {
        throw CompileError{lx.chunk + ":" + std::to_string(tok.line) + ": " + m + " near " + Near()};
    }
    std::string Near() {
        switch (tok.t) {
            case TK_EOF: return "<eof>";
            case TK_NAME: return "'" + tok.s + "'";
            case TK_STRING: return "'" + tok.s + "'";
            case TK_INT: return "'" + std::to_string(tok.i) + "'";
            case TK_NUM: return "'" + FormatNumber(Value::Num(tok.n)) + "'";
            case TK_CHAR: return std::string("'") + tok.ch + "'";
            case TK_EQ: return "'=='"; case TK_GE: return "'>='"; case TK_LE: return "'<='"; case TK_NE: return "'~='";
            case TK_SHL: return "'<<'"; case TK_SHR: return "'>>'"; case TK_IDIV: return "'//'";
            case TK_CONCAT: return "'..'"; case TK_DOTS: return "'...'"; case TK_DBCOLON: return "'::'";
            default: return std::string("'") + kKeywords[tok.t - TK_AND] + "'";
        }
    }
    void Advance() {
        if (hasAhead) { tok = std::move(ahead); hasAhead = false; }
        else tok = lx.Next();
    }
    const Token& Look() { if (!hasAhead) { ahead = lx.Next(); hasAhead = true; } return ahead; }
    bool IsCh(char c) const { return tok.t == TK_CHAR && tok.ch == c; }
    bool Accept(char c) { if (IsCh(c)) { Advance(); return true; } return false; }
    bool AcceptT(Tk t) { if (tok.t == t) { Advance(); return true; } return false; }
    void Expect(char c) { if (!Accept(c)) Fail(std::string("'") + c + "' expected"); }
    void ExpectT(Tk t, const char* what, int openLine = 0, const char* opener = "") {
        if (tok.t != t) {
            std::string m = std::string("'") + what + "' expected";
            if (openLine && openLine != tok.line) m += " (to close '" + std::string(opener) + "' at line " + std::to_string(openLine) + ")";
            Fail(m);
        }
        Advance();
    }
    std::string ExpectName() { if (tok.t != TK_NAME) Fail("<name> expected"); std::string s = tok.s; Advance(); return s; }

    // ---- scopes
    VarInfo* Declare(const std::string& name) {
        vars.push_back(std::make_unique<VarInfo>());
        VarInfo* v = vars.back().get(); v->name = name;
        fs->blocks.back().push_back(v);
        return v;
    }
    void OpenBlock() { fs->blocks.emplace_back(); }
    void CloseBlock() { fs->blocks.pop_back(); }
    static VarInfo* FindLocal(FuncScope* f, const std::string& name) {
        for (std::size_t b = f->blocks.size(); b-- > 0;)
            for (std::size_t i = f->blocks[b].size(); i-- > 0;)
                if (f->blocks[b][i]->name == name) return f->blocks[b][i];
        return nullptr;
    }
    static int ResolveUp(FuncScope* f, const std::string& name) {
        if (!f->parent) return -1;
        auto& ups = f->body->ups;
        if (VarInfo* v = FindLocal(f->parent, name)) {
            v->captured = true;
            for (std::size_t i = 0; i < ups.size(); ++i) if (ups[i].fromLocal && ups[i].var == v) return (int)i;
            ups.push_back({true, v, 0, name});
            return (int)ups.size() - 1;
        }
        int pi = ResolveUp(f->parent, name);
        if (pi < 0) return -1;
        for (std::size_t i = 0; i < ups.size(); ++i) if (!ups[i].fromLocal && ups[i].parentIdx == pi) return (int)i;
        ups.push_back({false, nullptr, pi, name});
        return (int)ups.size() - 1;
    }
    static VarInfo* UpVar(FuncScope* f, int idx) {   // the original variable behind upvalue idx (for const checks)
        const UpInfo& u = f->body->ups[idx];
        if (u.fromLocal) return u.var;
        return UpVar(f->parent, u.parentIdx);
    }
    EP NameExpr(const std::string& name, int line) {
        if (VarInfo* v = FindLocal(fs, name)) { auto e = std::make_unique<Expr>(EK::Local, line); e->var = v; e->s = name; return e; }
        int u = ResolveUp(fs, name);
        if (u >= 0) { auto e = std::make_unique<Expr>(EK::Up, line); e->up = u; e->s = name; return e; }
        auto e = std::make_unique<Expr>(EK::Global, line); e->s = name; return e;
    }

    // ---- expressions
    EP Simple() {
        int ln = tok.line;
        switch (tok.t) {
            case TK_INT: { auto e = std::make_unique<Expr>(EK::Int, ln); e->i = tok.i; Advance(); return e; }
            case TK_NUM: { auto e = std::make_unique<Expr>(EK::Num, ln); e->n = tok.n; Advance(); return e; }
            case TK_STRING: { auto e = std::make_unique<Expr>(EK::Str, ln); e->s = tok.s; Advance(); return e; }
            case TK_NIL: Advance(); return std::make_unique<Expr>(EK::Nil, ln);
            case TK_TRUE: Advance(); return std::make_unique<Expr>(EK::True, ln);
            case TK_FALSE: Advance(); return std::make_unique<Expr>(EK::False, ln);
            case TK_DOTS:
                if (!fs->body->vararg) Fail("cannot use '...' outside a vararg function");
                Advance(); return std::make_unique<Expr>(EK::Vararg, ln);
            case TK_FUNCTION: { Advance(); auto e = std::make_unique<Expr>(EK::Func, ln); e->fn = Body(false, "anonymous", ln); return e; }
            case TK_CHAR: if (tok.ch == '{') return TableCons(); break;
            default: break;
        }
        return Suffixed();
    }
    EP Primary() {
        int ln = tok.line;
        if (tok.t == TK_NAME) { std::string n = tok.s; Advance(); return NameExpr(n, ln); }
        if (IsCh('(')) {
            Advance();
            EP inner = Expr_(0);
            Expect(')');
            auto e = std::make_unique<Expr>(EK::Paren, ln); e->a = std::move(inner); return e;
        }
        Fail("unexpected symbol");
    }
    EP Suffixed() {
        EP e = Primary();
        int chain = 0;
        for (;;) {
            if (++chain > 600) Fail("expression too long (too many chained calls or fields)");
            int ln = tok.line;
            if (IsCh('.')) {
                Advance();
                auto ix = std::make_unique<Expr>(EK::Index, ln);
                ix->a = std::move(e);
                auto key = std::make_unique<Expr>(EK::Str, ln); key->s = ExpectName();
                ix->b = std::move(key); e = std::move(ix);
            } else if (IsCh('[')) {
                Advance();
                auto ix = std::make_unique<Expr>(EK::Index, ln);
                ix->a = std::move(e); ix->b = Expr_(0); Expect(']'); e = std::move(ix);
            } else if (IsCh(':')) {
                Advance();
                auto m = std::make_unique<Expr>(EK::Method, ln);
                m->s = ExpectName(); m->a = std::move(e); Args(*m); e = std::move(m);
            } else if (IsCh('(') || tok.t == TK_STRING || IsCh('{')) {
                auto c = std::make_unique<Expr>(EK::Call, ln);
                c->a = std::move(e); Args(*c); e = std::move(c);
            } else return e;
        }
    }
    void Args(Expr& call) {
        int ln = tok.line;
        if (tok.t == TK_STRING) { auto s = std::make_unique<Expr>(EK::Str, ln); s->s = tok.s; Advance(); call.args.push_back(std::move(s)); return; }
        if (IsCh('{')) { call.args.push_back(TableCons()); return; }
        if (!IsCh('(')) Fail("function arguments expected");
        Advance();
        if (!IsCh(')')) { call.args.push_back(Expr_(0)); while (Accept(',')) call.args.push_back(Expr_(0)); }
        if (!IsCh(')')) Fail("')' expected" + (ln != tok.line ? " (to close '(' at line " + std::to_string(ln) + ")" : std::string()));
        Advance();
    }
    EP TableCons() {
        int ln = tok.line;
        Expect('{');
        auto t = std::make_unique<Expr>(EK::Table, ln);
        while (!IsCh('}')) {
            TItem it;
            if (tok.t == TK_NAME && Look().t == TK_CHAR && Look().ch == '=') {
                it.kind = 1;
                auto k = std::make_unique<Expr>(EK::Str, tok.line); k->s = tok.s; it.key = std::move(k);
                Advance(); Advance(); it.val = Expr_(0);
            } else if (IsCh('[')) {
                Advance(); it.kind = 1; it.key = Expr_(0); Expect(']'); Expect('='); it.val = Expr_(0);
            } else it.val = Expr_(0);
            t->items.push_back(std::move(it));
            if (!Accept(',') && !Accept(';')) break;
        }
        if (!IsCh('}')) Fail("'}' expected" + (ln != tok.line ? " (to close '{' at line " + std::to_string(ln) + ")" : std::string()));
        Advance();
        return t;
    }
    std::unique_ptr<FuncBody> Body(bool isMethod, const std::string& name, int line) {
        auto fb = std::make_unique<FuncBody>();
        fb->name = name; fb->line = line;
        FuncScope sc; sc.parent = fs; sc.body = fb.get(); fs = &sc;
        OpenBlock();
        if (isMethod) fb->params.push_back(Declare("self"));
        Expect('(');
        if (!IsCh(')')) {
            do {
                if (tok.t == TK_DOTS) { Advance(); fb->vararg = true; break; }
                fb->params.push_back(Declare(ExpectName()));
            } while (Accept(','));
        }
        Expect(')');
        if (++depth > 180) Fail("chunk has too many syntax levels");
        fb->body = Block();
        --depth;
        ExpectT(TK_END, "end", line, "function");
        CloseBlock();
        fs = sc.parent;
        return fb;
    }
    static int Prio(Tk t, char ch, int& right, int& op, bool& isLogic) {
        isLogic = false;
        if (t == TK_OR) { right = 1; isLogic = true; op = 1; return 1; }
        if (t == TK_AND) { right = 2; isLogic = true; op = 0; return 2; }
        switch (t) {
            case TK_EQ: right = 3; op = B_EQ; return 3;
            case TK_NE: right = 3; op = B_NE; return 3;
            case TK_LE: right = 3; op = B_LE; return 3;
            case TK_GE: right = 3; op = B_GE; return 3;
            case TK_SHL: right = 7; op = B_SHL; return 7;
            case TK_SHR: right = 7; op = B_SHR; return 7;
            case TK_IDIV: right = 11; op = B_IDIV; return 11;
            case TK_CONCAT: right = 8; op = B_CONCAT; return 9;
            case TK_CHAR: break;
            default: return -1;
        }
        switch (ch) {
            case '<': right = 3; op = B_LT; return 3;
            case '>': right = 3; op = B_GT; return 3;
            case '|': right = 4; op = B_BOR; return 4;
            case '~': right = 5; op = B_BXOR; return 5;
            case '&': right = 6; op = B_BAND; return 6;
            case '+': right = 10; op = B_ADD; return 10;
            case '-': right = 10; op = B_SUB; return 10;
            case '*': right = 11; op = B_MUL; return 11;
            case '/': right = 11; op = B_DIV; return 11;
            case '%': right = 11; op = B_MOD; return 11;
            case '^': right = 13; op = B_POW; return 14;
        }
        return -1;
    }
    EP Expr_(int limit) {
        if (++depth > 180) Fail("chunk has too many syntax levels");
        EP left;
        int ln = tok.line;
        int uop = -1;
        if (tok.t == TK_NOT) uop = U_NOT;
        else if (tok.t == TK_CHAR && tok.ch == '-') uop = U_NEG;
        else if (tok.t == TK_CHAR && tok.ch == '#') uop = U_LEN;
        else if (tok.t == TK_CHAR && tok.ch == '~') uop = U_BNOT;
        if (uop >= 0) {
            Advance();
            EP operand = Expr_(12);
            if (uop == U_NEG && operand->k == EK::Int) { operand->i = (std::int64_t)(0 - (std::uint64_t)operand->i); left = std::move(operand); }
            else if (uop == U_NEG && operand->k == EK::Num) { operand->n = -operand->n; left = std::move(operand); }
            else { left = std::make_unique<Expr>(EK::Un, ln); left->op = uop; left->a = std::move(operand); }
        } else left = Simple();
        int chain = 0;
        for (;;) {
            int right, op; bool logic;
            int lp = Prio(tok.t, tok.ch, right, op, logic);
            if (lp < 0 || lp <= limit) break;
            if (++chain > 600) Fail("expression too long (too many chained operators)");
            int bl = tok.line;
            Advance();
            EP rhs = Expr_(right);
            EP node = std::make_unique<Expr>(logic ? (op == 1 ? EK::Or : EK::And) : EK::Bin, bl);
            node->op = op; node->a = std::move(left); node->b = std::move(rhs);
            left = std::move(node);
        }
        --depth;
        return left;
    }

    // ---- statements
    bool BlockEnd() const {
        return tok.t == TK_EOF || tok.t == TK_END || tok.t == TK_ELSE || tok.t == TK_ELSEIF || tok.t == TK_UNTIL;
    }
    std::vector<SP> Block() {
        std::vector<SP> out;
        while (!BlockEnd()) {
            if (tok.t == TK_RETURN) {
                int ln = tok.line; Advance();
                auto r = std::make_unique<Stmt>(SK::Return, ln);
                if (!BlockEnd() && !IsCh(';')) {
                    r->exprs.push_back(Expr_(0));
                    while (Accept(',')) r->exprs.push_back(Expr_(0));
                }
                Accept(';');
                out.push_back(std::move(r));
                if (!BlockEnd()) Fail("'<eof>' expected");
                break;
            }
            SP s = Statement();
            if (s) out.push_back(std::move(s));
        }
        return out;
    }
    std::vector<SP> ScopedBlock() { OpenBlock(); auto b = Block(); CloseBlock(); return b; }

    SP Statement() {
        int ln = tok.line;
        if (++depth > 180) Fail("chunk has too many syntax levels");
        struct D { int& d; ~D() { --d; } } guard{depth};
        switch (tok.t) {
            case TK_CHAR:
                if (tok.ch == ';') { Advance(); return nullptr; }
                break;
            case TK_DBCOLON: Fail("goto and labels are not supported by this Lua (see docs/scripting_migration.md)");
            case TK_GOTO: Fail("goto and labels are not supported by this Lua (see docs/scripting_migration.md)");
            case TK_IF: {
                Advance();
                auto s = std::make_unique<Stmt>(SK::If, ln);
                s->conds.push_back(Expr_(0)); ExpectT(TK_THEN, "then");
                s->blocks.push_back(ScopedBlock());
                while (tok.t == TK_ELSEIF) {
                    Advance(); s->conds.push_back(Expr_(0)); ExpectT(TK_THEN, "then");
                    s->blocks.push_back(ScopedBlock());
                }
                if (AcceptT(TK_ELSE)) s->body2 = ScopedBlock();
                ExpectT(TK_END, "end", ln, "if");
                return s;
            }
            case TK_WHILE: {
                Advance();
                auto s = std::make_unique<Stmt>(SK::While, ln);
                s->exprs.push_back(Expr_(0)); ExpectT(TK_DO, "do");
                s->body = ScopedBlock(); ExpectT(TK_END, "end", ln, "while");
                return s;
            }
            case TK_DO: {
                Advance();
                auto s = std::make_unique<Stmt>(SK::Do, ln);
                s->body = ScopedBlock(); ExpectT(TK_END, "end", ln, "do");
                return s;
            }
            case TK_FOR: return ForStat(ln);
            case TK_REPEAT: {
                Advance();
                auto s = std::make_unique<Stmt>(SK::Repeat, ln);
                OpenBlock();
                s->body = Block();
                ExpectT(TK_UNTIL, "until", ln, "repeat");
                s->exprs.push_back(Expr_(0));
                CloseBlock();
                return s;
            }
            case TK_FUNCTION: {
                Advance();
                // funcname: Name {'.' Name} [':' Name]
                int nl = tok.line;
                std::string n = ExpectName();
                EP target = NameExpr(n, nl);
                std::string full = n; bool method = false;
                while (IsCh('.') || IsCh(':')) {
                    bool colon = IsCh(':'); Advance();
                    auto ix = std::make_unique<Expr>(EK::Index, ln);
                    ix->a = std::move(target);
                    auto key = std::make_unique<Expr>(EK::Str, ln); key->s = ExpectName();
                    full += (colon ? ":" : ".") + key->s;
                    ix->b = std::move(key); target = std::move(ix);
                    if (colon) { method = true; break; }
                }
                auto fe = std::make_unique<Expr>(EK::Func, ln);
                fe->fn = Body(method, full, ln);
                CheckAssignable(*target);
                auto s = std::make_unique<Stmt>(SK::Assign, ln);
                s->targets.push_back(std::move(target)); s->exprs.push_back(std::move(fe));
                return s;
            }
            case TK_LOCAL: {
                Advance();
                if (AcceptT(TK_FUNCTION)) {
                    auto s = std::make_unique<Stmt>(SK::LocalFunc, ln);
                    std::string n = ExpectName();
                    s->vars.push_back(Declare(n));
                    s->fn = Body(false, n, ln);
                    return s;
                }
                auto s = std::make_unique<Stmt>(SK::Local, ln);
                std::vector<std::string> names; std::vector<int> attrs;
                do {
                    names.push_back(ExpectName());
                    int at = 0;
                    if (Accept('<')) {
                        std::string a = ExpectName();
                        if (a == "const") at = 1;
                        else if (a == "close") Fail("to-be-closed variables (<close>) are not supported by this Lua");
                        else Fail("unknown attribute '" + a + "'");
                        Expect('>');
                    }
                    attrs.push_back(at);
                } while (Accept(','));
                if (Accept('=')) {
                    s->exprs.push_back(Expr_(0));
                    while (Accept(',')) s->exprs.push_back(Expr_(0));
                }
                for (std::size_t i = 0; i < names.size(); ++i) { VarInfo* v = Declare(names[i]); v->isConst = attrs[i] == 1; s->vars.push_back(v); }
                return s;
            }
            case TK_BREAK: { Advance(); return std::make_unique<Stmt>(SK::Break, ln); }
            default: break;
        }
        // expression statement: call or assignment
        EP e = Suffixed();
        if (IsCh('=') || IsCh(',')) {
            auto s = std::make_unique<Stmt>(SK::Assign, ln);
            CheckAssignable(*e);
            s->targets.push_back(std::move(e));
            while (Accept(',')) { EP t = Suffixed(); CheckAssignable(*t); s->targets.push_back(std::move(t)); }
            Expect('=');
            s->exprs.push_back(Expr_(0));
            while (Accept(',')) s->exprs.push_back(Expr_(0));
            return s;
        }
        if (e->k != EK::Call && e->k != EK::Method) Fail("syntax error");
        auto s = std::make_unique<Stmt>(SK::CallS, ln);
        s->exprs.push_back(std::move(e));
        return s;
    }
    void CheckAssignable(Expr& e) {
        if (e.k == EK::Local && e.var->isConst) Fail("attempt to assign to const variable '" + e.var->name + "'");
        if (e.k == EK::Up && UpVar(fs, e.up)->isConst) Fail("attempt to assign to const variable '" + e.s + "'");
        if (e.k != EK::Local && e.k != EK::Up && e.k != EK::Global && e.k != EK::Index) Fail("syntax error");
    }
    SP ForStat(int ln) {
        Advance();
        std::string n1 = ExpectName();
        if (IsCh('=')) {
            Advance();
            auto s = std::make_unique<Stmt>(SK::NumFor, ln);
            s->exprs.push_back(Expr_(0)); Expect(','); s->exprs.push_back(Expr_(0));
            if (Accept(',')) s->exprs.push_back(Expr_(0));
            ExpectT(TK_DO, "do");
            OpenBlock();
            s->vars.push_back(Declare(n1));
            s->body = Block();
            CloseBlock();
            ExpectT(TK_END, "end", ln, "for");
            return s;
        }
        auto s = std::make_unique<Stmt>(SK::GenFor, ln);
        std::vector<std::string> names{n1};
        while (Accept(',')) names.push_back(ExpectName());
        ExpectT(TK_IN, "in");
        s->exprs.push_back(Expr_(0));
        while (Accept(',')) s->exprs.push_back(Expr_(0));
        ExpectT(TK_DO, "do");
        OpenBlock();
        for (auto& n : names) s->vars.push_back(Declare(n));
        s->body = Block();
        CloseBlock();
        ExpectT(TK_END, "end", ln, "for");
        return s;
    }

    std::unique_ptr<FuncBody> Chunk() {
        auto fb = std::make_unique<FuncBody>();
        fb->vararg = true; fb->name = "main chunk"; fb->line = 0;
        FuncScope sc; sc.body = fb.get(); fs = &sc;
        OpenBlock();
        fb->body = Block();
        if (tok.t != TK_EOF) Fail("'<eof>' expected");
        CloseBlock();
        fs = nullptr;
        return fb;
    }
};

// ============================================================ code generator
struct Gen {
    State& L;
    std::string chunk;
    Proto* P;
    FuncBody* fb;
    int nslots = 0, maxslots = 0;
    int curLine = 0;
    std::unordered_map<std::string, int> kmap;
    std::vector<std::vector<int>> breaks;

    Gen(State& l, const std::string& c, Proto* p, FuncBody* f) : L(l), chunk(c), P(p), fb(f) {}

    [[noreturn]] void Fail(const std::string& m, int line) { throw CompileError{chunk + ":" + std::to_string(line) + ": " + m}; }
    int Emit(Op op, int a = 0, int b = 0, int c = 0, const std::string& hint = std::string()) {
        Instr in; in.op = op; in.a = a; in.b = b; in.c = c;
        P->code.push_back(in); P->lines.push_back(curLine);
        if (!hint.empty()) { if (P->hints.size() <= P->code.size()) P->hints.resize(P->code.size()); P->hints[P->code.size() - 1] = hint; }
        return (int)P->code.size() - 1;
    }
    int Here() const { return (int)P->code.size(); }
    void Patch(int at, int target) { P->code[at].a = target; }
    int KStr(const std::string& s) {
        auto it = kmap.find("s" + s); if (it != kmap.end()) return it->second;
        P->k.push_back(L.NewStr(s)); return kmap["s" + s] = (int)P->k.size() - 1;
    }
    int KInt(std::int64_t i) {
        std::string key = "i" + std::to_string(i);
        auto it = kmap.find(key); if (it != kmap.end()) return it->second;
        P->k.push_back(Value::Int(i)); return kmap[key] = (int)P->k.size() - 1;
    }
    int KNum(double d) {
        std::uint64_t bits; std::memcpy(&bits, &d, 8);
        std::string key = "n" + std::to_string(bits);
        auto it = kmap.find(key); if (it != kmap.end()) return it->second;
        P->k.push_back(Value::Num(d)); return kmap[key] = (int)P->k.size() - 1;
    }
    int NewSlot() { int s = nslots++; if (nslots > maxslots) maxslots = nslots; return s; }

    static std::string Describe(const Expr& e) {
        switch (e.k) {
            case EK::Local: return "local '" + e.var->name + "'";
            case EK::Up: return "upvalue '" + e.s + "'";
            case EK::Global: return "global '" + e.s + "'";
            case EK::Index: if (e.b->k == EK::Str) return "field '" + e.b->s + "'"; return "";
            case EK::Method: return "method '" + e.s + "'";
            case EK::Str: return "constant";
            default: return "";
        }
    }

    // ---- expressions
    static bool IsMulti(const Expr& e) { return e.k == EK::Call || e.k == EK::Method || e.k == EK::Vararg; }

    void Load(const Expr& e) {   // exactly one value
        int saveLine = curLine; curLine = e.line;
        switch (e.k) {
            case EK::Nil: Emit(OP_NIL); break;
            case EK::True: Emit(OP_TRUE); break;
            case EK::False: Emit(OP_FALSE); break;
            case EK::Int: Emit(OP_K, KInt(e.i)); break;
            case EK::Num: Emit(OP_K, KNum(e.n)); break;
            case EK::Str: Emit(OP_K, KStr(e.s)); break;
            case EK::Vararg: Emit(OP_VARARG, 0, 1); break;
            case EK::Func: Closure_(*e.fn); break;
            case EK::Local: Emit(e.var->captured ? OP_GETCELL : OP_GETLOCAL, e.var->slot); break;
            case EK::Up: Emit(OP_GETUP, e.up); break;
            case EK::Global: Emit(OP_GETGLOBAL, KStr(e.s)); break;
            case EK::Index:
                Load(*e.a);
                if (e.b->k == EK::Str) Emit(OP_GETFIELD, KStr(e.b->s), 0, 0, Describe(*e.a));
                else { Load(*e.b); Emit(OP_GETTABLE, 0, 0, 0, Describe(*e.a)); }
                break;
            case EK::Call: case EK::Method: CallExpr(e, 1); break;
            case EK::Paren: Load(*e.a); break;
            case EK::Un:
                Load(*e.a);
                Emit(e.op == U_NEG ? OP_UNM : e.op == U_NOT ? OP_NOT : e.op == U_LEN ? OP_LEN : OP_BNOT, 0, 0, 0, Describe(*e.a));
                break;
            case EK::And: case EK::Or: {
                Load(*e.a);
                int j = Emit(e.k == EK::And ? OP_JMPFK : OP_JMPTK);
                Load(*e.b);
                Patch(j, Here());
                break;
            }
            case EK::Bin: BinExpr(e); break;
            case EK::Table: TableExpr(e); break;
        }
        curLine = saveLine;
    }
    void BinExpr(const Expr& e) {
        if (e.op == B_CONCAT) {
            std::vector<const Expr*> parts;
            const Expr* cur = &e;
            while (cur->k == EK::Bin && cur->op == B_CONCAT) { parts.push_back(cur->a.get()); cur = cur->b.get(); }
            parts.push_back(cur);
            for (auto* p : parts) Load(*p);
            Emit(OP_CONCAT, (int)parts.size(), 0, 0, Describe(*parts[0]) + "|" + Describe(*parts[1]));
            return;
        }
        // Walk the left spine with a loop, so a long chain (a + b + c + ...) does not recurse.
        static const Op map[] = {OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD, OP_IDIV, OP_POW, OP_BAND, OP_BOR, OP_BXOR, OP_SHL, OP_SHR,
                                 OP_CONCAT, OP_EQ, OP_NE, OP_LT, OP_LE, OP_GT, OP_GE};
        std::vector<const Expr*> spine;
        const Expr* cur = &e;
        while (cur->k == EK::Bin && cur->op != B_CONCAT) { spine.push_back(cur); cur = cur->a.get(); }
        Load(*cur);
        for (std::size_t i = spine.size(); i-- > 0;) {
            const Expr& b = *spine[i];
            Load(*b.b);
            int saveLine = curLine; curLine = b.line;
            Emit(map[b.op], 0, 0, 0, Describe(*b.a) + "|" + Describe(*b.b));
            curLine = saveLine;
        }
    }
    void TableExpr(const Expr& e) {
        int narr = 0;
        for (auto& it : e.items) if (it.kind == 0) ++narr;
        Emit(OP_NEWTABLE, narr, (int)e.items.size() - narr);
        int pending = 0, start = 1;
        for (std::size_t i = 0; i < e.items.size(); ++i) {
            const TItem& it = e.items[i];
            if (it.kind == 1) {
                if (pending) { Emit(OP_SETLIST, pending, start, 0); start += pending; pending = 0; }
                Load(*it.key); Load(*it.val);
                Emit(OP_INITKV);
            } else {
                bool last = (i + 1 == e.items.size());
                if (last && IsMulti(*it.val)) {
                    MultiTail(*it.val);
                    Emit(OP_SETLIST, pending, start, 1); pending = 0;
                    continue;
                }
                Load(*it.val); ++pending;
                if (pending >= 50) { Emit(OP_SETLIST, pending, start, 0); start += pending; pending = 0; }
            }
        }
        if (pending) Emit(OP_SETLIST, pending, start, 0);
    }

    void MultiTail(const Expr& e) {      // push all values (sets co->multi)
        int saveLine = curLine; curLine = e.line;
        if (e.k == EK::Vararg) Emit(OP_VARARG, 0, -1);
        else CallExpr(e, -1);
        curLine = saveLine;
    }
    void CallExpr(const Expr& e, int nret) {
        const Expr* fnE = e.a.get();
        int fixed = 0;
        std::string hint;
        if (e.k == EK::Method) {
            Load(*e.a);
            Emit(OP_SELF, KStr(e.s), 0, 0, Describe(*e.a));
            fixed = 1; hint = "method '" + e.s + "'";
        } else {
            Load(*fnE); hint = Describe(*fnE);
        }
        bool multi = false;
        for (std::size_t i = 0; i < e.args.size(); ++i) {
            if (i + 1 == e.args.size() && IsMulti(*e.args[i])) { MultiTail(*e.args[i]); multi = true; }
            else { Load(*e.args[i]); ++fixed; }
        }
        int saveLine = curLine; curLine = e.line;
        Emit(OP_CALL, fixed, nret, multi ? 1 : 0, hint);
        curLine = saveLine;
    }
    // push exactly n values from a list of expressions
    void PushN(const std::vector<EP>& list, int n) {
        int pushed = 0;
        for (std::size_t i = 0; i < list.size(); ++i) {
            const Expr& e = *list[i];
            bool last = (i + 1 == list.size());
            if (pushed >= n) {   // extra expressions: evaluate and drop
                if (IsMulti(e)) { if (e.k == EK::Vararg) continue; CallExpr(e, 0); }
                else { Load(e); Emit(OP_POP); }
                continue;
            }
            if (last && IsMulti(e) && pushed < n) {
                int want = n - pushed;
                if (e.k == EK::Vararg) { int sl = curLine; curLine = e.line; Emit(OP_VARARG, 0, want); curLine = sl; }
                else CallExpr(e, want);
                pushed += want;
            } else { Load(e); ++pushed; }
        }
        while (pushed < n) { Emit(OP_NIL); ++pushed; }
    }

    // ---- functions
    void Closure_(FuncBody& body) {
        Proto* np = nullptr;
        L.protos.push_back(std::make_unique<Proto>());
        np = L.protos.back().get();
        np->chunk = chunk; np->name = body.name; np->line = body.line;
        Gen g(L, chunk, np, &body);
        g.FunctionBody();
        for (auto& u : body.ups) np->ups.push_back({u.fromLocal, u.fromLocal ? u.var->slot : u.parentIdx});
        P->protos.push_back(np);
        Emit(OP_CLOSURE, (int)P->protos.size() - 1);
    }
    void FunctionBody() {
        P->nparams = (int)fb->params.size();
        P->vararg = fb->vararg;
        curLine = fb->line;
        for (VarInfo* v : fb->params) { v->slot = NewSlot(); }
        for (VarInfo* v : fb->params) if (v->captured) Emit(OP_CELLIFY, v->slot);
        Block(fb->body);
        curLine = fb->body.empty() ? fb->line : curLine;
        Emit(OP_RETURN, 0, 0, 0);
        P->nlocals = maxslots;
    }

    // ---- statements
    void Block(const std::vector<SP>& b) {
        int save = nslots;
        for (auto& s : b) Statement(*s);
        nslots = save;
    }
    void StoreLocal(VarInfo* v) { Emit(v->captured ? OP_NEWCELL : OP_SETLOCAL, v->slot); }

    void Statement(const Stmt& s) {
        curLine = s.line;
        switch (s.k) {
            case SK::Local: {
                PushN(s.exprs, (int)s.vars.size());
                for (VarInfo* v : s.vars) v->slot = NewSlot();   // slots after evaluating: names not visible in exprs
                for (std::size_t i = s.vars.size(); i-- > 0;) StoreLocal(s.vars[i]);
                break;
            }
            case SK::LocalFunc: {
                VarInfo* v = s.vars[0]; v->slot = NewSlot();
                if (v->captured) { Emit(OP_NIL); Emit(OP_NEWCELL, v->slot); }
                Closure_(*s.fn);
                Emit(v->captured ? OP_SETCELL : OP_SETLOCAL, v->slot);
                break;
            }
            case SK::Assign: Assign(s); break;
            case SK::CallS: {
                const Expr& e = *s.exprs[0];
                CallExpr(e, 0);
                break;
            }
            case SK::Do: Block(s.body); break;
            case SK::While: {
                int top = Here();
                Load(*s.exprs[0]);
                int j = Emit(OP_JMPF);
                breaks.emplace_back();
                Block(s.body);
                Emit(OP_JMP, top);
                Patch(j, Here());
                for (int b : breaks.back()) Patch(b, Here());
                breaks.pop_back();
                break;
            }
            case SK::Repeat: {
                int top = Here();
                breaks.emplace_back();
                int save = nslots;
                for (auto& st : s.body) Statement(*st);
                curLine = s.line;
                Load(*s.exprs[0]);
                Emit(OP_JMPF, top);
                nslots = save;
                for (int b : breaks.back()) Patch(b, Here());
                breaks.pop_back();
                break;
            }
            case SK::If: {
                std::vector<int> ends;
                for (std::size_t i = 0; i < s.conds.size(); ++i) {
                    Load(*s.conds[i]);
                    int j = Emit(OP_JMPF);
                    Block(s.blocks[i]);
                    if (i + 1 < s.conds.size() || !s.body2.empty()) ends.push_back(Emit(OP_JMP));
                    Patch(j, Here());
                }
                Block(s.body2);
                for (int e : ends) Patch(e, Here());
                break;
            }
            case SK::NumFor: {
                int save = nslots;
                int s0 = NewSlot(); NewSlot(); NewSlot();
                VarInfo* v = s.vars[0];
                v->slot = NewSlot();
                Load(*s.exprs[0]); Load(*s.exprs[1]);
                if (s.exprs.size() > 2) Load(*s.exprs[2]); else Emit(OP_K, KInt(1));
                Emit(OP_SETLOCAL, s0 + 2); Emit(OP_SETLOCAL, s0 + 1); Emit(OP_SETLOCAL, s0);
                int prep = Emit(OP_FORPREP, 0, s0);
                int body = Here();
                if (v->captured) { Emit(OP_GETLOCAL, v->slot); Emit(OP_NEWCELL, v->slot); }
                breaks.emplace_back();
                Block(s.body);
                curLine = s.line;
                Emit(OP_FORLOOP, body, s0);
                Patch(prep, Here());
                for (int b : breaks.back()) Patch(b, Here());
                breaks.pop_back();
                nslots = save;
                break;
            }
            case SK::GenFor: GenFor(s); break;
            case SK::Return: Return_(s); break;
            case SK::Break:
                if (breaks.empty()) Fail("break outside a loop at line " + std::to_string(s.line), s.line);
                breaks.back().push_back(Emit(OP_JMP));
                break;
        }
    }
    void Return_(const Stmt& s) {
        int fixed = 0; bool multi = false;
        for (std::size_t i = 0; i < s.exprs.size(); ++i) {
            if (i + 1 == s.exprs.size() && IsMulti(*s.exprs[i])) { MultiTail(*s.exprs[i]); multi = true; }
            else { Load(*s.exprs[i]); ++fixed; }
        }
        Emit(OP_RETURN, fixed, 0, multi ? 1 : 0);
    }
    void Assign(const Stmt& s) {
        if (s.targets.size() == 1 && s.exprs.size() == 1) {
            const Expr& t = *s.targets[0];
            if (t.k == EK::Index) {
                Load(*s.exprs[0]);
                Load(*t.a);
                if (t.b->k == EK::Str) Emit(OP_SETFIELD, KStr(t.b->s), 0, 0, Describe(*t.a));
                else { Load(*t.b); Emit(OP_SETTABLE, 0, 0, 0, Describe(*t.a)); }
            } else { Load(*s.exprs[0]); StoreTarget(t); }
            return;
        }
        PushN(s.exprs, (int)s.targets.size());
        for (std::size_t i = s.targets.size(); i-- > 0;) {
            const Expr& t = *s.targets[i];
            if (t.k == EK::Index) {
                Load(*t.a);
                if (t.b->k == EK::Str) Emit(OP_SETFIELD, KStr(t.b->s), 0, 0, Describe(*t.a));
                else { Load(*t.b); Emit(OP_SETTABLE, 0, 0, 0, Describe(*t.a)); }
            } else StoreTarget(t);
        }
    }
    void StoreTarget(const Expr& t) {
        switch (t.k) {
            case EK::Local: Emit(t.var->captured ? OP_SETCELL : OP_SETLOCAL, t.var->slot); break;
            case EK::Up: Emit(OP_SETUP, t.up); break;
            case EK::Global: Emit(OP_SETGLOBAL, KStr(t.s)); break;
            default: Fail("cannot assign", t.line);
        }
    }
    void GenFor(const Stmt& s) {
        int save = nslots;
        int g0 = NewSlot(); NewSlot(); NewSlot();     // iterator function, state, control
        PushN(s.exprs, 3);
        Emit(OP_SETLOCAL, g0 + 2); Emit(OP_SETLOCAL, g0 + 1); Emit(OP_SETLOCAL, g0);
        for (VarInfo* v : s.vars) v->slot = NewSlot();
        int top = Here();
        Emit(OP_GETLOCAL, g0); Emit(OP_GETLOCAL, g0 + 1); Emit(OP_GETLOCAL, g0 + 2);
        Emit(OP_CALL, 2, (int)s.vars.size(), 0, "for iterator 'for iterator'");
        for (std::size_t i = s.vars.size(); i-- > 1;) StoreLocal(s.vars[i]);
        int test = Emit(OP_GFORTEST, 0, g0 + 2);
        StoreLocal(s.vars[0]);
        breaks.emplace_back();
        Block(s.body);
        curLine = s.line;
        Emit(OP_JMP, top);
        Patch(test, Here());
        for (int b : breaks.back()) Patch(b, Here());
        breaks.pop_back();
        nslots = save;
    }
};

}  // namespace

Proto* State::Compile(const std::string& src, const std::string& chunk, std::string* err) {
    try {
        Parser ps(src, chunk);
        std::unique_ptr<FuncBody> fb = ps.Chunk();
        protos.push_back(std::make_unique<Proto>());
        Proto* p = protos.back().get();
        p->chunk = chunk; p->name = "main chunk";
        Gen g(*this, chunk, p, fb.get());
        g.FunctionBody();
        p->hints.resize(p->code.size());
        // hints vectors of nested protos are resized lazily; ensure all match their code
        std::vector<Proto*> stack{p};
        while (!stack.empty()) { Proto* q = stack.back(); stack.pop_back(); q->hints.resize(q->code.size()); for (Proto* c : q->protos) stack.push_back(c); }
        return p;
    } catch (const CompileError& e) {
        if (err) *err = e.msg;
        return nullptr;
    }
}

}  // namespace okay::lua
