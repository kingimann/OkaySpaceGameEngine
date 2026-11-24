// okay::lua compiler: lexer, parser and code generator in one pass.
//
// Written from the Lua 5.4 Reference Manual: section 3.1 (lexical conventions),
// 3.2-3.5 (variables, statements, expressions, visibility rules), 3.4.8
// (operator precedence) and 9 (the complete syntax). The code generator uses
// the general "expression descriptor" technique (an expression is described
// first and turned into code only when its use is known), so the same parse
// can produce a read, an assignment target or a multi-value call.
#include "LuaCode.hpp"
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <charconv>
#include <memory>

namespace okay::lua {

namespace {

// ---------------------------------------------------------------- tokens
enum Tok : int {
    T_EOF = 256, T_NAME, T_STRING, T_INT, T_FLT,
    // keywords
    T_AND, T_BREAK, T_DO, T_ELSE, T_ELSEIF, T_END, T_FALSE, T_FOR, T_FUNCTION, T_GOTO, T_IF, T_IN,
    T_LOCAL, T_NIL, T_NOT, T_OR, T_REPEAT, T_RETURN, T_THEN, T_TRUE, T_UNTIL, T_WHILE,
    // multi-char symbols
    T_IDIV, T_CONCAT, T_DOTS, T_EQ, T_GE, T_LE, T_NE, T_SHL, T_SHR, T_DBCOLON,
};
const char* const kKeywords[] = {"and", "break", "do", "else", "elseif", "end", "false", "for", "function",
                                 "goto", "if", "in", "local", "nil", "not", "or", "repeat", "return", "then",
                                 "true", "until", "while"};

struct Token {
    int t = T_EOF;
    std::string s;        ///< name / string contents / source text of the token
    std::int64_t i = 0;
    double n = 0;
    int line = 1, col = 1;
};

struct CompileError {
    std::string msg;
    int line, col;
};

std::string TokText(int t) {
    if (t < 256) {
        std::string s = "'";
        s += (char)t;
        return s + "'";
    }
    switch (t) {
        case T_EOF: return "<eof>";
        case T_NAME: return "<name>";
        case T_STRING: return "<string>";
        case T_INT: case T_FLT: return "<number>";
        case T_IDIV: return "'//'"; case T_CONCAT: return "'..'"; case T_DOTS: return "'...'";
        case T_EQ: return "'=='"; case T_GE: return "'>='"; case T_LE: return "'<='"; case T_NE: return "'~='";
        case T_SHL: return "'<<'"; case T_SHR: return "'>>'"; case T_DBCOLON: return "'::'";
        default:
            if (t >= T_AND && t <= T_WHILE) return std::string("'") + kKeywords[t - T_AND] + "'";
            return "?";
    }
}

class Lexer {
public:
    Lexer(const std::string& src, const std::string& chunk) : s_(src), chunk_(chunk) {
        // A first line starting with '#' (a Unix "shebang") is skipped.
        if (!s_.empty() && s_[0] == '#')
            while (p_ < s_.size() && s_[p_] != '\n') ++p_;
    }
    Token Next();
    int line() const { return line_; }
    [[noreturn]] void Fail(const std::string& msg, int line, int col, const std::string& near) {
        std::string m = msg;
        if (!near.empty()) m += " near " + near;
        throw CompileError{m, line, col};
    }
private:
    int Peek(std::size_t k = 0) const { return p_ + k < s_.size() ? (unsigned char)s_[p_ + k] : -1; }
    void Adv() {
        if (p_ < s_.size()) {
            if (s_[p_] == '\n') { ++line_; col_ = 1; } else ++col_;
            ++p_;
        }
    }
    void Newline() {   // \n, \r, \n\r or \r\n count as one line break
        int c = Peek();
        Adv();
        if ((Peek() == '\n' || Peek() == '\r') && Peek() != c) {
            if (c == '\r') { ++p_; ++col_; } else Adv();
        }
        if (c == '\r') { ++line_; col_ = 1; }
    }
    int LongBracketLevel();   // at '[': returns level (count of '=') if a long bracket opens, else -1
    std::string ReadLong(int level, bool isComment, int line0, int col0);
    std::string ReadString(int quote, int line0, int col0);
    Token ReadNumber();
    const std::string& s_;
    std::string chunk_;
    std::size_t p_ = 0;
    int line_ = 1, col_ = 1;
};

int Lexer::LongBracketLevel() {
    std::size_t k = 1;
    int level = 0;
    while (Peek(k) == '=') { ++level; ++k; }
    return Peek(k) == '[' ? level : (level == 0 ? -1 : -2);
}

std::string Lexer::ReadLong(int level, bool isComment, int line0, int col0) {
    for (int i = 0; i < level + 2; ++i) Adv();   // [==[
    if (Peek() == '\r' || Peek() == '\n') Newline();   // a first line break is skipped
    std::string out;
    for (;;) {
        int c = Peek();
        if (c < 0) Fail(isComment ? "unfinished long comment" : "unfinished long string", line0, col0, "<eof>");
        if (c == ']') {
            std::size_t k = 1;
            int l = 0;
            while (Peek(k) == '=') { ++l; ++k; }
            if (l == level && Peek(k) == ']') {
                for (std::size_t j = 0; j <= k; ++j) Adv();
                return out;
            }
            out += ']';
            Adv();
        } else if (c == '\n' || c == '\r') {
            out += '\n';
            Newline();
        } else {
            if (!isComment) out += (char)c;
            Adv();
        }
    }
}

void AppendUtf8(std::string& out, std::uint32_t cp) {
    // The manual allows code points up to 2^31 using the original UTF-8 scheme (up to 6 bytes).
    if (cp < 0x80) { out += (char)cp; return; }
    int n;
    if (cp < 0x800) n = 2;
    else if (cp < 0x10000) n = 3;
    else if (cp < 0x200000) n = 4;
    else if (cp < 0x4000000) n = 5;
    else n = 6;
    static const unsigned char first[] = {0, 0, 0xC0, 0xE0, 0xF0, 0xF8, 0xFC};
    char buf[6];
    for (int i = n - 1; i > 0; --i) { buf[i] = (char)(0x80 | (cp & 0x3F)); cp >>= 6; }
    buf[0] = (char)(first[n] | cp);
    out.append(buf, (std::size_t)n);
}

int HexVal(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

std::string Lexer::ReadString(int quote, int line0, int col0) {
    Adv();
    std::string out;
    for (;;) {
        int c = Peek();
        if (c < 0) Fail("unfinished string", line0, col0, "<eof>");
        if (c == '\n' || c == '\r') Fail("unfinished string", line0, col0, "'" + std::string(1, (char)quote) + out + "'");
        if (c == quote) { Adv(); return out; }
        if (c != '\\') { out += (char)c; Adv(); continue; }
        int el = line_, ec = col_;
        Adv();
        c = Peek();
        switch (c) {
            case 'a': out += '\a'; Adv(); break;
            case 'b': out += '\b'; Adv(); break;
            case 'f': out += '\f'; Adv(); break;
            case 'n': out += '\n'; Adv(); break;
            case 'r': out += '\r'; Adv(); break;
            case 't': out += '\t'; Adv(); break;
            case 'v': out += '\v'; Adv(); break;
            case '\\': out += '\\'; Adv(); break;
            case '"': out += '"'; Adv(); break;
            case '\'': out += '\''; Adv(); break;
            case '\n': case '\r': out += '\n'; Newline(); break;
            case 'x': {
                Adv();
                int h1 = HexVal(Peek()), h2 = h1 >= 0 ? HexVal(Peek(1)) : -1;
                if (h1 < 0 || h2 < 0) Fail("hexadecimal digit expected", el, ec, "'\\x'");
                out += (char)(h1 * 16 + h2);
                Adv(); Adv();
                break;
            }
            case 'z': {
                Adv();
                while (Peek() >= 0 && std::isspace(Peek())) {
                    if (Peek() == '\n' || Peek() == '\r') Newline(); else Adv();
                }
                break;
            }
            case 'u': {
                Adv();
                if (Peek() != '{') Fail("missing '{' in \\u{xxxx}", el, ec, "'\\u'");
                Adv();
                std::uint64_t cp = 0;
                int digits = 0;
                while (HexVal(Peek()) >= 0) {
                    cp = cp * 16 + (std::uint64_t)HexVal(Peek());
                    if (cp > 0x7FFFFFFFu) Fail("UTF-8 value too large", el, ec, "'\\u'");
                    ++digits;
                    Adv();
                }
                if (digits == 0) Fail("hexadecimal digit expected", el, ec, "'\\u{'");
                if (Peek() != '}') Fail("missing '}' in \\u{xxxx}", el, ec, "'\\u'");
                Adv();
                AppendUtf8(out, (std::uint32_t)cp);
                break;
            }
            default: {
                if (c >= '0' && c <= '9') {
                    int v = 0;
                    for (int k = 0; k < 3 && Peek() >= '0' && Peek() <= '9'; ++k) { v = v * 10 + (Peek() - '0'); Adv(); }
                    if (v > 255) Fail("decimal escape too large", el, ec, "'\\" + std::to_string(v) + "'");
                    out += (char)v;
                    break;
                }
                if (c < 0) Fail("unfinished string", line0, col0, "<eof>");
                Fail("invalid escape sequence", el, ec, "'\\" + std::string(1, (char)c) + "'");
            }
        }
    }
}

Token Lexer::ReadNumber() {
    Token t;
    t.line = line_;
    t.col = col_;
    std::string text;
    bool hex = Peek() == '0' && (Peek(1) == 'x' || Peek(1) == 'X');
    const char* expChars = hex ? "Pp" : "Ee";
    if (hex) { text += (char)Peek(); Adv(); text += (char)Peek(); Adv(); }
    for (;;) {
        int c = Peek();
        if (c < 0) break;
        if (std::strchr(expChars, c) && c != 0) {
            text += (char)c;
            Adv();
            if (Peek() == '+' || Peek() == '-') { text += (char)Peek(); Adv(); }
            continue;
        }
        if (std::isalnum(c) || c == '.' || c == '_') { text += (char)c; Adv(); continue; }
        break;
    }
    Value v;
    if (!State::StrToNumber(text, v)) Fail("malformed number", t.line, t.col, "'" + text + "'");
    if (v.t == Type::Int) { t.t = T_INT; t.i = v.i; } else { t.t = T_FLT; t.n = v.n; }
    t.s = text;
    return t;
}

Token Lexer::Next() {
    for (;;) {
        int c = Peek();
        Token t;
        t.line = line_;
        t.col = col_;
        switch (c) {
            case -1: t.t = T_EOF; return t;
            case '\n': case '\r': Newline(); continue;
            case ' ': case '\t': case '\f': case '\v': Adv(); continue;
            case '-':
                if (Peek(1) != '-') { Adv(); t.t = '-'; return t; }
                Adv(); Adv();
                if (Peek() == '[') {
                    int lvl = LongBracketLevel();
                    if (lvl >= 0) { ReadLong(lvl, true, t.line, t.col); continue; }
                }
                while (Peek() >= 0 && Peek() != '\n' && Peek() != '\r') Adv();
                continue;
            case '[': {
                int lvl = LongBracketLevel();
                if (lvl >= 0) { t.s = ReadLong(lvl, false, t.line, t.col); t.t = T_STRING; return t; }
                if (lvl == -2) Fail("invalid long string delimiter", t.line, t.col, "'[='");
                Adv(); t.t = '['; return t;
            }
            case '=': Adv(); if (Peek() == '=') { Adv(); t.t = T_EQ; } else t.t = '='; return t;
            case '<':
                Adv();
                if (Peek() == '=') { Adv(); t.t = T_LE; } else if (Peek() == '<') { Adv(); t.t = T_SHL; } else t.t = '<';
                return t;
            case '>':
                Adv();
                if (Peek() == '=') { Adv(); t.t = T_GE; } else if (Peek() == '>') { Adv(); t.t = T_SHR; } else t.t = '>';
                return t;
            case '/': Adv(); if (Peek() == '/') { Adv(); t.t = T_IDIV; } else t.t = '/'; return t;
            case '~': Adv(); if (Peek() == '=') { Adv(); t.t = T_NE; } else t.t = '~'; return t;
            case ':': Adv(); if (Peek() == ':') { Adv(); t.t = T_DBCOLON; } else t.t = ':'; return t;
            case '"': case '\'': t.s = ReadString(c, t.line, t.col); t.t = T_STRING; return t;
            case '.':
                if (Peek(1) == '.') {
                    Adv(); Adv();
                    if (Peek() == '.') { Adv(); t.t = T_DOTS; } else t.t = T_CONCAT;
                    return t;
                }
                if (Peek(1) >= '0' && Peek(1) <= '9') return ReadNumber();
                Adv(); t.t = '.'; return t;
            default:
                if (c >= '0' && c <= '9') return ReadNumber();
                if (std::isalpha(c) || c == '_') {
                    while (Peek() >= 0 && (std::isalnum(Peek()) || Peek() == '_')) { t.s += (char)Peek(); Adv(); }
                    t.t = T_NAME;
                    for (int k = 0; k < (int)(sizeof(kKeywords) / sizeof(kKeywords[0])); ++k)
                        if (t.s == kKeywords[k]) { t.t = T_AND + k; break; }
                    return t;
                }
                if (std::strchr("+*%^#&|(){}];,", c)) { Adv(); t.t = c; return t; }
                {
                    char buf[16];
                    if (c >= 32 && c < 127) std::snprintf(buf, sizeof buf, "'%c'", c);
                    else std::snprintf(buf, sizeof buf, "'<\\%d>'", c);
                    Fail("unexpected symbol", t.line, t.col, buf);
                }
        }
    }
}

// ---------------------------------------------------------------- code generation
enum EK { VVOID, VNIL, VTRUE, VFALSE, VKSTR, VINT, VFLT, VLOCAL, VUPVAL, VINDEXED, VINDEXEDK, VINDEXEDUPK, VCALL, VVARARG, VPUSHED };

struct ExpDesc {
    EK k = VVOID;
    int info = 0;        ///< local var index / upvalue / constant / pc of CALL or VARARG
    int aux = 0;         ///< VINDEXEDK / VINDEXEDUPK: constant index of the key
    int tpos = 0;        ///< VINDEXED(K): stack slot of the table
    std::int64_t ival = 0;
    double nval = 0;
    std::string sval;    ///< VKSTR
    std::string desc;    ///< for error messages
    std::string odesc;   ///< VINDEXED(K): what the indexed value was
    int concatPc = -1;   ///< the expression's last step was this CONCAT (for fusing a .. b .. c)
};

struct LocalVar {
    std::string name;
    int slot = 0;
    int attrib = 0;              ///< 0 plain, 1 <const>, 2 <close>
    bool captured = false;
    int boxPc = -1;              ///< placeholder NOP that becomes MKBOX when captured
    std::vector<int> accessPcs;
};

struct Label { std::string name; int pc; int nactive; int sp; int line; int block; };
struct PendingGoto { std::string name; int pc; int nactive; int line; int col; int block; };

struct BlockScope {
    int nactive = 0;     ///< locals active when the block started
    int sp = 0;          ///< stack depth when the block started
    bool isLoop = false;
    std::vector<int> breaks;   ///< JMP pcs to patch to the loop exit
    int id = 0;
    std::size_t firstLabel = 0, firstGoto = 0;
};

struct FuncState {
    FuncState* parent = nullptr;
    Proto* p = nullptr;
    std::vector<LocalVar> vars;     ///< every local declared (indexes are stable)
    std::vector<int> actives;       ///< indexes into vars, in scope order
    std::vector<BlockScope> blocks;
    std::vector<Label> labels;
    std::vector<PendingGoto> gotos;
    int sp = 0;
    int nextBlockId = 0;
    int tbcActive = 0;
};

class Compiler {
public:
    Compiler(State& L, const std::string& src, const std::string& chunk, bool collectAll)
        : L_(L), lex_(src, chunk), chunk_(chunk), collect_(collectAll) {}
    Proto* Run(std::vector<SyntaxError>& errs);
private:
    // tokens
    void Next() { prevLine_ = tok_.line; tok_ = ahead_.t != 0 ? ahead_ : lex_.Next(); ahead_ = Token{}; ahead_.t = 0; }
    const Token& Lookahead() { if (ahead_.t == 0) ahead_ = lex_.Next(); return ahead_; }
    bool Test(int t) { if (tok_.t == t) { Next(); return true; } return false; }
    [[noreturn]] void ErrorAt(const std::string& msg, const Token& t) {
        std::string near;
        switch (t.t) {
            case T_NAME: case T_STRING: near = "'" + t.s.substr(0, 40) + "'"; break;
            case T_INT: case T_FLT: near = "'" + t.s + "'"; break;
            default: near = TokText(t.t);
        }
        throw CompileError{msg + " near " + near, t.line, t.col};
    }
    [[noreturn]] void Error(const std::string& msg) { ErrorAt(msg, tok_); }
    [[noreturn]] void ErrorPlain(const std::string& msg) { throw CompileError{msg, prevLine_, tok_.col}; }
    void Check(int t) { if (tok_.t != t) Error(TokText(t) + " expected"); }
    void CheckNext(int t) { Check(t); Next(); }
    void CheckMatch(int what, int who, int line) {
        if (tok_.t == what) { Next(); return; }
        if (line == tok_.line) Error(TokText(what) + " expected");
        Error(TokText(what) + " expected (to close " + TokText(who) + " at line " + std::to_string(line) + ")");
    }
    std::string CheckName() { Check(T_NAME); std::string s = tok_.s; Next(); return s; }
    void EnterLevel() { if (++depth_ > 190) Error("chunk has too many syntax levels"); }
    void LeaveLevel() { --depth_; }

    // emission
    int Emit(Op op, int a = 0, int b = 0, int c = 0, int delta = 0) {
        Proto* p = fs_->p;
        p->code.push_back(Instr{op, a, b, c});
        p->lines.push_back(prevLine_);
        fs_->sp += delta;
        if (fs_->sp > p->maxStack) p->maxStack = fs_->sp;
        if (p->code.size() > 16'000'000) Error("function or expression too complex");
        return (int)p->code.size() - 1;
    }
    int Pc() const { return (int)fs_->p->code.size(); }
    void Patch(int pc, int target) { fs_->p->code[(std::size_t)pc].a = target; }
    int ConstStr(const std::string& s);
    int ConstNum(const Value& v);
    void Hint(int pc, const std::string& d) { if (!d.empty()) fs_->p->hints[pc] = d; }

    // variables
    int AddLocal(const std::string& name, int attrib = 0);
    void ActivateLocal(int var);   // the var's slot is the current top - 1
    LocalVar& Var(int i) { return fs_->vars[(std::size_t)i]; }
    void Capture(FuncState* fs, int var);
    int FindUpval(FuncState* fs, const std::string& name);
    bool SingleVarAux(FuncState* fs, const std::string& name, ExpDesc& e, bool base);
    void SingleVar(const std::string& name, ExpDesc& e);
    void Discharge(ExpDesc& e);
    void SetMulti(ExpDesc& e, int n);   // VCALL / VVARARG produce n results (-1 = all)
    bool IsMulti(const ExpDesc& e) const { return e.k == VCALL || e.k == VVARARG; }
    void StoreVar(ExpDesc& target);     // assign the top value to target (pops it)
    void AdjustAssign(int nvars, int nexps, ExpDesc& last);
    void OpenBlock(BlockScope& bl, bool loop);
    void CloseBlock();
    void PopTo(int sp);   // drop operand / local slots down to sp (static)

    // grammar
    bool BlockFollow(bool withUntil) const;
    void StatList();
    void Statement();
    void Block();
    void IfStat(int line);
    void WhileStat(int line);
    void RepeatStat(int line);
    void ForStat(int line);
    void ForNum(const std::string& name, int line);
    void ForList(const std::string& first);
    void FuncStat(int line);
    void LocalFunc();
    void LocalStat();
    void ReturnStat();
    void BreakStat();
    void GotoStat();
    void LabelStat(const std::string& name, int line);
    void ExprStat();
    void Body(ExpDesc& e, bool isMethod, int line, const std::string& name);
    void ParList();
    int ExpList(ExpDesc& e);
    void Expr(ExpDesc& e) { SubExpr(e, 0); }
    int SubExpr(ExpDesc& e, int limit);
    void SimpleExp(ExpDesc& e);
    void PrimaryExp(ExpDesc& e);
    void SuffixedExp(ExpDesc& e);
    void FieldSel(ExpDesc& e);
    void FuncArgs(ExpDesc& f, bool isSelf);
    void Constructor(ExpDesc& e);
    void ResolveGotos(BlockScope& bl, bool atEnd);
    void JumpTo(int pc, const Label& lb);
    void StartFunc(FuncState& fs, Proto* p);
    void EndFunc();

    State& L_;
    Lexer lex_;
    std::string chunk_;
    bool collect_;
    Token tok_, ahead_;
    int prevLine_ = 1;
    int depth_ = 0;
    FuncState* fs_ = nullptr;
    std::vector<Proto*> keep_;   // protos made so far (kept reachable while compiling)
};

int Compiler::ConstStr(const std::string& s) {
    auto& k = fs_->p->k;
    for (std::size_t i = 0; i < k.size(); ++i)
        if (k[i].t == Type::String && k[i].Str()->s == s) return (int)i;
    k.push_back(Value::Obj(L_.NewString(s)));
    return (int)k.size() - 1;
}

int Compiler::ConstNum(const Value& v) {
    auto& k = fs_->p->k;
    for (std::size_t i = 0; i < k.size(); ++i)
        if (k[i].t == v.t && (v.t == Type::Int ? k[i].i == v.i : std::memcmp(&k[i].n, &v.n, sizeof(double)) == 0)) return (int)i;
    k.push_back(v);
    return (int)k.size() - 1;
}

int Compiler::AddLocal(const std::string& name, int attrib) {
    if (fs_->actives.size() >= 200) Error("too many local variables (limit is 200) in function");
    LocalVar v;
    v.name = name;
    v.attrib = attrib;
    fs_->vars.push_back(std::move(v));
    return (int)fs_->vars.size() - 1;
}

void Compiler::ActivateLocal(int var) {
    LocalVar& v = Var(var);
    // the slot is the next one after the active locals: locals sit at the bottom of the frame in order
    int slot = 0;
    for (int a : fs_->actives) slot = std::max(slot, Var(a).slot + 1);
    // hidden for-loop slots are locals too, so this is the running count
    v.slot = slot;
    fs_->actives.push_back(var);
    v.boxPc = Emit(NOP, v.slot);
}

void Compiler::Capture(FuncState* fs, int var) {
    LocalVar& v = fs->vars[(std::size_t)var];
    if (v.captured) return;
    v.captured = true;
    Proto* p = fs->p;
    if (v.boxPc >= 0) p->code[(std::size_t)v.boxPc].op = MKBOX;
    for (int pc : v.accessPcs) {
        Instr& in = p->code[(std::size_t)pc];
        if (in.op == GETLOCAL) in.op = GETBOX;
        else if (in.op == SETLOCAL) in.op = SETBOX;
    }
    v.accessPcs.clear();
}

int Compiler::FindUpval(FuncState* fs, const std::string& name) {
    auto& u = fs->p->upvals;
    for (std::size_t i = 0; i < u.size(); ++i)
        if (u[i].name == name) return (int)i;
    return -1;
}

// Resolve `name` in fs: a local (VLOCAL), an upvalue (VUPVAL), or not found (false).
bool Compiler::SingleVarAux(FuncState* fs, const std::string& name, ExpDesc& e, bool base) {
    if (!fs) return false;
    for (int i = (int)fs->actives.size() - 1; i >= 0; --i) {
        int vi = fs->actives[(std::size_t)i];
        if (fs->vars[(std::size_t)vi].name == name) {
            if (!base) Capture(fs, vi);
            e.k = VLOCAL;
            e.info = vi;
            e.desc = "local '" + name + "'";
            return true;
        }
    }
    int u = FindUpval(fs, name);
    if (u < 0) {
        ExpDesc pe;
        if (!SingleVarAux(fs->parent, name, pe, false)) return false;
        UpvalDesc d;
        d.name = name;
        if (pe.k == VLOCAL) { d.fromLocal = true; d.index = fs->parent->vars[(std::size_t)pe.info].slot; }
        else { d.fromLocal = false; d.index = pe.info; }
        if (fs->p->upvals.size() >= 255) Error("too many upvalues (limit is 255)");
        fs->p->upvals.push_back(d);
        u = (int)fs->p->upvals.size() - 1;
    }
    e.k = VUPVAL;
    e.info = u;
    e.desc = "upvalue '" + name + "'";
    return true;
}

void Compiler::SingleVar(const std::string& name, ExpDesc& e) {
    if (SingleVarAux(fs_, name, e, true)) return;
    // a global: _ENV.name
    ExpDesc env;
    SingleVarAux(fs_, "_ENV", env, true);   // always found: the main chunk's upvalue 0
    int k = ConstStr(name);
    if (env.k == VUPVAL) {
        e.k = VINDEXEDUPK;
        e.info = env.info;
        e.aux = k;
    } else {
        Discharge(env);
        e.k = VINDEXEDK;
        e.tpos = fs_->sp - 1;
        e.aux = k;
    }
    e.desc = "global '" + name + "'";
}

void Compiler::Discharge(ExpDesc& e) {
    switch (e.k) {
        case VVOID: Emit(PUSHNIL, 1, 0, 0, 1); break;   // should not happen; keeps the stack consistent
        case VNIL: Emit(PUSHNIL, 1, 0, 0, 1); break;
        case VTRUE: Emit(PUSHTRUE, 0, 0, 0, 1); break;
        case VFALSE: Emit(PUSHFALSE, 0, 0, 0, 1); break;
        case VKSTR: Emit(PUSHK, ConstStr(e.sval), 0, 0, 1); break;
        case VINT:
            if (e.ival >= INT32_MIN && e.ival <= INT32_MAX) Emit(PUSHINT, (int)e.ival, 0, 0, 1);
            else Emit(PUSHK, ConstNum(Value::Int(e.ival)), 0, 0, 1);
            break;
        case VFLT: Emit(PUSHK, ConstNum(Value::Num(e.nval)), 0, 0, 1); break;
        case VLOCAL: {
            LocalVar& v = Var(e.info);
            int pc = Emit(v.captured ? GETBOX : GETLOCAL, v.slot, 0, 0, 1);
            if (!v.captured) v.accessPcs.push_back(pc);
            break;
        }
        case VUPVAL: Emit(GETUP, e.info, 0, 0, 1); break;
        case VINDEXED: Hint(Emit(GETINDEX, 0, 0, 0, -1), e.odesc); break;
        case VINDEXEDK: Hint(Emit(GETFIELD, e.aux, 0, 0, 0), e.odesc); break;
        case VINDEXEDUPK: Emit(GETTABUPK, e.info, e.aux, 0, 1); break;
        case VCALL: case VVARARG: case VPUSHED: break;   // already one value on the stack
    }
    e.k = VPUSHED;
}

void Compiler::SetMulti(ExpDesc& e, int n) {
    Instr& in = fs_->p->code[(std::size_t)e.info];
    if (e.k == VCALL) {
        // the function sat at slot in.a; results start there
        in.c = n;
        fs_->sp = in.a + (n < 0 ? 0 : n);
    } else {
        in.a = n;
        fs_->sp = fs_->sp - 1 + (n < 0 ? 0 : n);
    }
    if (fs_->sp > fs_->p->maxStack) fs_->p->maxStack = fs_->sp;
}

void Compiler::StoreVar(ExpDesc& t) {
    switch (t.k) {
        case VLOCAL: {
            LocalVar& v = Var(t.info);
            if (v.attrib) ErrorPlain("attempt to assign to const variable '" + v.name + "'");
            int pc = Emit(v.captured ? SETBOX : SETLOCAL, v.slot, 0, 0, -1);
            if (!v.captured) v.accessPcs.push_back(pc);
            break;
        }
        case VUPVAL: {
            // is the captured variable a const one?
            FuncState* f = fs_;
            int idx = t.info;
            while (f && f->parent) {
                const UpvalDesc& d = f->p->upvals[(std::size_t)idx];
                if (d.fromLocal) {
                    for (auto& v : f->parent->vars)
                        if (v.captured && v.slot == d.index && v.name == d.name && v.attrib)
                            ErrorPlain("attempt to assign to const variable '" + v.name + "'");
                    break;
                }
                idx = d.index;
                f = f->parent;
            }
            Emit(SETUP, t.info, 0, 0, -1);
            break;
        }
        case VINDEXEDUPK: Emit(SETTABUPK, t.info, t.aux, 0, -1); break;
        case VINDEXED: Hint(Emit(SETINDEXAT, t.tpos, 0, 0, -1), t.odesc); break;
        case VINDEXEDK: Hint(Emit(SETFIELDAT, t.tpos, t.aux, 0, -1), t.odesc); break;
        default: Error("syntax error");
    }
}

void Compiler::AdjustAssign(int nvars, int nexps, ExpDesc& e) {
    if (IsMulti(e)) {
        int extra = nvars - nexps + 1;   // values wanted from the call / ...
        if (extra >= 0) { SetMulti(e, extra); return; }
        SetMulti(e, 0);                  // more expressions than variables
        Emit(POP, -extra, 0, 0, extra);
        return;
    }
    if (e.k != VVOID) Discharge(e);
    int needed = nvars - nexps;
    if (needed > 0) Emit(PUSHNIL, needed, 0, 0, needed);
    else if (needed < 0) Emit(POP, -needed, 0, 0, needed);
}

void Compiler::PopTo(int sp) {
    if (fs_->sp > sp) Emit(POP, fs_->sp - sp, 0, 0, sp - fs_->sp);
}

void Compiler::OpenBlock(BlockScope& bl, bool loop) {
    bl.nactive = (int)fs_->actives.size();
    bl.sp = fs_->sp;
    bl.isLoop = loop;
    bl.id = fs_->nextBlockId++;
    bl.firstLabel = fs_->labels.size();
    bl.firstGoto = fs_->gotos.size();
    fs_->blocks.push_back(bl);
}

void Compiler::JumpTo(int pc, const Label& lb) {
    Instr& in = fs_->p->code[(std::size_t)pc];
    in.a = lb.pc;
    in.b = lb.sp;
    in.c = lb.sp;
}

// Match the block's pending gotos against its labels. At block end the
// unmatched ones move to the enclosing block (their scope level drops).
void Compiler::ResolveGotos(BlockScope& bl, bool atEnd) {
    auto& gotos = fs_->gotos;
    for (std::size_t i = bl.firstGoto; i < gotos.size();) {
        PendingGoto& g = gotos[i];
        bool done = false;
        for (std::size_t j = bl.firstLabel; j < fs_->labels.size(); ++j) {
            const Label& lb = fs_->labels[j];
            if (lb.name != g.name) continue;
            if (g.nactive < lb.nactive) {
                const LocalVar& v = Var(fs_->actives.size() > (std::size_t)g.nactive
                                            ? fs_->actives[(std::size_t)g.nactive]
                                            : fs_->actives.back());
                throw CompileError{"<goto " + g.name + "> at line " + std::to_string(g.line) +
                                       " jumps into the scope of local '" + v.name + "'",
                                   g.line, g.col};
            }
            JumpTo(g.pc, lb);
            done = true;
            break;
        }
        if (done) gotos.erase(gotos.begin() + (std::ptrdiff_t)i);
        else ++i;
    }
    if (atEnd)
        for (std::size_t i = bl.firstGoto; i < gotos.size(); ++i)
            if (gotos[i].nactive > bl.nactive) gotos[i].nactive = bl.nactive;
}

void Compiler::CloseBlock() {
    BlockScope bl = fs_->blocks.back();
    ResolveGotos(bl, true);
    // locals go out of scope
    bool hadTbc = false;
    for (std::size_t i = (std::size_t)bl.nactive; i < fs_->actives.size(); ++i)
        if (Var(fs_->actives[i]).attrib == 2) { hadTbc = true; --fs_->tbcActive; }
    if (hadTbc) Emit(CLOSE, bl.sp);
    fs_->actives.resize((std::size_t)bl.nactive);
    PopTo(bl.sp);
    fs_->labels.resize(bl.firstLabel);
    int exit = Pc();
    for (int pc : bl.breaks) {
        Instr& in = fs_->p->code[(std::size_t)pc];
        in.a = exit;
    }
    fs_->blocks.pop_back();
    if (fs_->blocks.empty() && !fs_->gotos.empty()) {
        const PendingGoto& g = fs_->gotos.front();
        throw CompileError{"no visible label '" + g.name + "' for <goto> at line " + std::to_string(g.line), g.line, g.col};
    }
}

bool Compiler::BlockFollow(bool withUntil) const {
    switch (tok_.t) {
        case T_ELSE: case T_ELSEIF: case T_END: case T_EOF: return true;
        case T_UNTIL: return withUntil;
        default: return false;
    }
}

void Compiler::StatList() {
    while (!BlockFollow(true)) {
        if (tok_.t == T_RETURN) { Statement(); return; }
        Statement();
    }
}

void Compiler::Block() {
    BlockScope bl;
    OpenBlock(bl, false);
    StatList();
    CloseBlock();
}

void Compiler::Statement() {
    int line = tok_.line;
    EnterLevel();
    switch (tok_.t) {
        case ';': Next(); break;
        case T_IF: IfStat(line); break;
        case T_WHILE: WhileStat(line); break;
        case T_DO: Next(); Block(); CheckMatch(T_END, T_DO, line); break;
        case T_FOR: ForStat(line); break;
        case T_REPEAT: RepeatStat(line); break;
        case T_FUNCTION: FuncStat(line); break;
        case T_LOCAL:
            Next();
            if (Test(T_FUNCTION)) LocalFunc(); else LocalStat();
            break;
        case T_DBCOLON: {
            Next();
            std::string name = CheckName();
            CheckNext(T_DBCOLON);
            LabelStat(name, line);
            break;
        }
        case T_RETURN: Next(); ReturnStat(); break;
        case T_BREAK: BreakStat(); break;
        case T_GOTO: Next(); GotoStat(); break;
        default: ExprStat(); break;
    }
    LeaveLevel();
}

void Compiler::IfStat(int line) {
    std::vector<int> ends;
    do {
        Next();   // 'if' or 'elseif'
        ExpDesc c;
        Expr(c);
        Discharge(c);
        int jf = Emit(JMPIFNOT, 0, 0, 0, -1);
        CheckNext(T_THEN);
        Block();
        if (tok_.t == T_ELSE || tok_.t == T_ELSEIF) ends.push_back(Emit(JMP, 0, -1, -1));
        Patch(jf, Pc());
    } while (tok_.t == T_ELSEIF);
    if (Test(T_ELSE)) Block();
    CheckMatch(T_END, T_IF, line);
    for (int pc : ends) Patch(pc, Pc());
}

void Compiler::WhileStat(int line) {
    Next();
    int start = Pc();
    ExpDesc c;
    Expr(c);
    Discharge(c);
    int exitJ = Emit(JMPIFNOT, 0, 0, 0, -1);
    CheckNext(T_DO);
    BlockScope bl;
    OpenBlock(bl, true);
    StatList();
    int sp = fs_->blocks.back().sp;
    // the back jump drops the body's locals (and closes them)
    std::vector<int> brk = fs_->blocks.back().breaks;
    fs_->blocks.back().breaks.clear();
    CloseBlock();
    Emit(JMP, start, sp, sp);
    CheckMatch(T_END, T_WHILE, line);
    Patch(exitJ, Pc());
    for (int pc : brk) Patch(pc, Pc());
}

void Compiler::RepeatStat(int line) {
    Next();
    int start = Pc();
    BlockScope loop;
    OpenBlock(loop, true);      // breaks
    BlockScope scope;
    OpenBlock(scope, false);    // the body's locals are visible in the condition
    StatList();
    CheckMatch(T_UNTIL, T_REPEAT, line);
    ExpDesc c;
    Expr(c);
    Discharge(c);
    int done = Emit(JMPIF, 0, 0, 0, -1);
    // not done: drop the body's locals and go round again
    int sp = fs_->blocks.back().sp;
    {
        BlockScope& bl = fs_->blocks.back();
        ResolveGotos(bl, false);
    }
    Emit(JMP, start, sp, sp);
    Patch(done, Pc());
    CloseBlock();
    CloseBlock();
}

void Compiler::ForStat(int line) {
    Next();
    std::string name = CheckName();
    BlockScope bl;
    OpenBlock(bl, true);
    if (tok_.t == '=') ForNum(name, line);
    else if (tok_.t == ',' || tok_.t == T_IN) ForList(name);
    else Error("'=' or 'in' expected");
    CheckMatch(T_END, T_FOR, line);
    CloseBlock();
}

void Compiler::ForNum(const std::string& name, int line) {
    (void)line;
    Next();   // '='
    int base = fs_->sp;
    ExpDesc e;
    Expr(e); Discharge(e);
    CheckNext(',');
    Expr(e); Discharge(e);
    if (Test(',')) { Expr(e); Discharge(e); }
    else Emit(PUSHINT, 1, 0, 0, 1);
    // three hidden locals hold start, limit and step
    for (int h = 0; h < 3; ++h) {
        int v = AddLocal("(for state)");
        Var(v).slot = base + h;
        fs_->actives.push_back(v);
    }
    CheckNext(T_DO);
    int prep = Emit(FORPREP, base, 0, 0, 1);   // pushes the loop variable (on iteration)
    int body = Pc();
    BlockScope inner;
    OpenBlock(inner, false);
    int var = AddLocal(name);
    ActivateLocal(var);
    StatList();
    CloseBlock();   // pops the body's locals; the loop variable's slot stays for FORLOOP
    Emit(FORLOOP, base, body, 0, -1);
    fs_->p->code[(std::size_t)prep].b = Pc();
}

void Compiler::ForList(const std::string& first) {
    int base = fs_->sp;
    std::vector<std::string> names{first};
    while (Test(',')) names.push_back(CheckName());
    CheckNext(T_IN);
    int line = tok_.line;
    ExpDesc e;
    int n = ExpList(e);
    AdjustAssign(4, n, e);
    for (int i = 0; i < 4; ++i) {
        int v = AddLocal("(for state)");
        Var(v).slot = base + i;
        fs_->actives.push_back(v);
    }
    // the fourth value is closed when the loop ends
    Var(fs_->actives.back()).attrib = 2;
    ++fs_->tbcActive;
    Emit(TBC, base + 3);
    CheckNext(T_DO);
    int start = Pc();
    Emit(GETLOCAL, base, 0, 0, 1);
    Emit(GETLOCAL, base + 1, 0, 0, 1);
    Emit(GETLOCAL, base + 2, 0, 0, 1);
    prevLine_ = line;
    int call = Emit(CALL, base + 4, 2, (int)names.size(), 0);
    fs_->sp = base + 4 + (int)names.size();
    if (fs_->sp > fs_->p->maxStack) fs_->p->maxStack = fs_->sp;
    Hint(call, "for iterator 'for iterator'");
    int test = Emit(TFORTEST, base, 0, 0, 0);
    BlockScope inner;
    OpenBlock(inner, false);
    fs_->blocks.back().sp = base + 4;
    fs_->sp = base + 4;
    for (const auto& nm : names) {
        int v = AddLocal(nm);
        fs_->sp++;
        ActivateLocal(v);
    }
    fs_->sp = base + 4 + (int)names.size();
    StatList();
    CloseBlock();
    Emit(JMP, start, base + 4, base + 4);
    fs_->p->code[(std::size_t)test].b = Pc();
    // the outer block's end closes the fourth value and pops the hidden slots
}

void Compiler::LabelStat(const std::string& name, int line) {
    for (std::size_t i = 0; i < fs_->labels.size(); ++i)
        if (fs_->labels[i].name == name)
            ErrorPlain("label '" + name + "' already defined on line " + std::to_string(fs_->labels[i].line));
    // skip other no-op statements: a label at the end of a block is outside the block's locals
    while (tok_.t == ';' || tok_.t == T_DBCOLON) {
        if (tok_.t == ';') { Next(); continue; }
        break;
    }
    Label lb;
    lb.name = name;
    lb.pc = Pc();
    lb.line = line;
    lb.block = fs_->blocks.back().id;
    lb.nactive = (int)fs_->actives.size();
    lb.sp = fs_->sp;
    if (BlockFollow(false)) {
        // at the end of its block the label is outside the block's locals; a jump arriving
        // from before them raises the top to sp with nils (CloseBlock's POP then drops them)
        lb.nactive = fs_->blocks.back().nactive;
    }
    fs_->labels.push_back(lb);
    // backward-looking: pending gotos in this block (or nested, already moved here)
    ResolveGotos(fs_->blocks.back(), false);
}

void Compiler::GotoStat() {
    int line = tok_.line, col = tok_.col;
    std::string name = CheckName();
    // a visible label already defined (backward jump)?
    for (int i = (int)fs_->labels.size() - 1; i >= 0; --i) {
        const Label& lb = fs_->labels[(std::size_t)i];
        if (lb.name != name) continue;
        int pc = Emit(JMP, 0, 0, 0);
        JumpTo(pc, lb);
        return;
    }
    int pc = Emit(JMP, 0, 0, 0);
    fs_->gotos.push_back(PendingGoto{name, pc, (int)fs_->actives.size(), line, col, fs_->blocks.back().id});
}

void Compiler::BreakStat() {
    int line = tok_.line;
    Next();
    for (int i = (int)fs_->blocks.size() - 1; i >= 0; --i) {
        BlockScope& bl = fs_->blocks[(std::size_t)i];
        if (!bl.isLoop) continue;
        bl.breaks.push_back(Emit(JMP, 0, bl.sp, bl.sp));
        return;
    }
    throw CompileError{"break outside a loop at line " + std::to_string(line), line, 1};
}

void Compiler::ReturnStat() {
    int first = fs_->sp;
    if (BlockFollow(true) || tok_.t == ';') {
        Emit(RETURN, first, 0);
    } else {
        ExpDesc e;
        int n = ExpList(e);
        if (IsMulti(e)) {
            if (n == 1 && e.k == VCALL && fs_->tbcActive == 0) {
                fs_->p->code[(std::size_t)e.info].op = TAILCALL;
                SetMulti(e, -1);
                Emit(RETURN, first, -1);
            } else {
                SetMulti(e, -1);
                Emit(RETURN, first, -1);
            }
        } else {
            Discharge(e);
            Emit(RETURN, first, n);
        }
    }
    Test(';');
    fs_->sp = first;
    if (!BlockFollow(true)) Error("'<eof>' expected");
}

void Compiler::FuncStat(int line) {
    Next();
    // funcname: Name {'.' Name} [':' Name]
    std::string name = CheckName();
    std::string full = name;
    ExpDesc v;
    SingleVar(name, v);
    bool method = false;
    while (tok_.t == '.' || tok_.t == ':') {
        method = tok_.t == ':';
        Next();
        std::string key = CheckName();
        full += (method ? ":" : ".") + key;
        std::string od = v.desc;
        Discharge(v);
        v.odesc = od;
        v.k = VINDEXEDK;
        v.tpos = fs_->sp - 1;
        v.aux = ConstStr(key);
        v.desc = "field '" + key + "'";
        if (method) break;
    }
    ExpDesc b;
    Body(b, method, line, full);
    if (v.k == VINDEXEDK) {
        Hint(Emit(SETFIELD, v.aux, 0, 0, -2), v.odesc);
    } else {
        StoreVar(v);
    }
}

void Compiler::LocalFunc() {
    std::string name = CheckName();
    int var = AddLocal(name);
    Emit(PUSHNIL, 1, 0, 0, 1);
    ActivateLocal(var);
    ExpDesc b;
    Body(b, false, tok_.line, name);
    LocalVar& v = Var(var);
    int pc = Emit(v.captured ? SETBOX : SETLOCAL, v.slot, 0, 0, -1);
    if (!v.captured) v.accessPcs.push_back(pc);
}

void Compiler::LocalStat() {
    std::vector<int> vars;
    int closeCount = 0;
    do {
        std::string name = CheckName();
        int attrib = 0;
        if (Test('<')) {
            std::string a = CheckName();
            if (a == "const") attrib = 1;
            else if (a == "close") attrib = 2;
            else Error("unknown attribute '" + a + "'");
            CheckNext('>');
            if (attrib == 2 && ++closeCount > 1) Error("multiple to-be-closed variables in local list");
        }
        vars.push_back(AddLocal(name, attrib));
    } while (Test(','));
    int nvars = (int)vars.size();
    if (Test('=')) {
        ExpDesc e;
        int n = ExpList(e);
        AdjustAssign(nvars, n, e);
    } else {
        Emit(PUSHNIL, nvars, 0, 0, nvars);
    }
    // the values sit in the new locals' slots; now they come into scope
    int top = fs_->sp;
    fs_->sp = top - nvars;
    for (int v : vars) {
        fs_->sp++;
        ActivateLocal(v);
        if (Var(v).attrib == 2) {
            Emit(TBC, Var(v).slot);
            ++fs_->tbcActive;
        }
    }
}

void Compiler::ExprStat() {
    ExpDesc e;
    SuffixedExp(e);
    if (tok_.t == '=' || tok_.t == ',') {
        std::vector<ExpDesc> targets{e};
        int firstSp = -1;
        auto check = [&](const ExpDesc& t) {
            if (t.k != VLOCAL && t.k != VUPVAL && t.k != VINDEXED && t.k != VINDEXEDK && t.k != VINDEXEDUPK)
                Error("syntax error");
        };
        check(e);
        while (Test(',')) {
            ExpDesc t;
            SuffixedExp(t);
            check(t);
            targets.push_back(t);
        }
        (void)firstSp;
        CheckNext('=');
        // how many slots the targets left on the stack (tables and keys)
        int below = 0;
        for (auto& t : targets) below += t.k == VINDEXED ? 2 : t.k == VINDEXEDK ? 1 : 0;
        ExpDesc v;
        int n = ExpList(v);
        int nt = (int)targets.size();
        if (nt == 1 && n == 1 && !IsMulti(v)) {
            Discharge(v);
            ExpDesc& t = targets[0];
            if (t.k == VINDEXED) { Hint(Emit(SETINDEX, 0, 0, 0, -3), t.odesc); return; }
            if (t.k == VINDEXEDK) { Hint(Emit(SETFIELD, t.aux, 0, 0, -2), t.odesc); return; }
            StoreVar(t);
            return;
        }
        AdjustAssign(nt, n, v);
        for (int i = nt - 1; i >= 0; --i) StoreVar(targets[(std::size_t)i]);
        if (below) Emit(POP, below, 0, 0, -below);
        return;
    }
    if (e.k != VCALL) Error("syntax error");
    SetMulti(e, 0);
}

void Compiler::ParList() {
    Proto* p = fs_->p;
    int np = 0;
    if (tok_.t != ')') {
        do {
            if (tok_.t == T_NAME) {
                int v = AddLocal(tok_.s);
                Next();
                fs_->sp++;
                ActivateLocal(v);
                ++np;
            } else if (tok_.t == T_DOTS) {
                Next();
                p->isVararg = true;
                break;
            } else {
                Error("<name> expected");
            }
        } while (Test(','));
    }
    p->numParams = np;
}

void Compiler::StartFunc(FuncState& fs, Proto* p) {
    fs.parent = fs_;
    fs.p = p;
    p->chunk = chunk_;
    fs_ = &fs;
    keep_.push_back(p);
    BlockScope bl;
    OpenBlock(bl, false);
}

void Compiler::EndFunc() {
    Emit(RETURN, 0, 0);
    CloseBlock();
    fs_ = fs_->parent;
}

void Compiler::Body(ExpDesc& e, bool isMethod, int line, const std::string& name) {
    Proto* p = L_.Alloc<Proto>();
    p->lineDefined = line;
    p->name = name.empty() ? "anonymous function" : "function '" + name + "'";
    // the new function is reachable from its parent's list, which is reachable from keep_
    fs_->p->protos.push_back(p);
    int idx = (int)fs_->p->protos.size() - 1;
    FuncState fs;
    StartFunc(fs, p);
    if (isMethod) {
        int v = AddLocal("self");
        fs_->sp++;
        ActivateLocal(v);
    }
    CheckNext('(');
    ParList();
    if (isMethod) p->numParams += 1;
    CheckNext(')');
    StatList();
    CheckMatch(T_END, T_FUNCTION, line);
    EndFunc();
    Emit(CLOSURE, idx, 0, 0, 1);
    e.k = VPUSHED;
}

int Compiler::ExpList(ExpDesc& e) {
    int n = 1;
    Expr(e);
    while (Test(',')) {
        Discharge(e);
        Expr(e);
        ++n;
    }
    return n;
}

void Compiler::PrimaryExp(ExpDesc& e) {
    switch (tok_.t) {
        case T_NAME: {
            std::string n = tok_.s;
            Next();
            SingleVar(n, e);
            return;
        }
        case '(': {
            int line = tok_.line;
            Next();
            Expr(e);
            CheckMatch(')', '(', line);
            Discharge(e);   // (f()) is one value
            e.desc.clear();
            e.concatPc = -1;
            return;
        }
        default: Error("unexpected symbol");
    }
}

void Compiler::FieldSel(ExpDesc& e) {
    std::string d = e.desc;
    Discharge(e);
    Next();   // '.'
    std::string key = CheckName();
    e.k = VINDEXEDK;
    e.tpos = fs_->sp - 1;
    e.aux = ConstStr(key);
    e.desc = "field '" + key + "'";
    e.odesc = d;   // a failure to index reports what the indexed value was
}

void Compiler::SuffixedExp(ExpDesc& e) {
    PrimaryExp(e);
    for (;;) {
        switch (tok_.t) {
            case '.': FieldSel(e); break;
            case '[': {
                std::string d = e.desc;
                Discharge(e);
                int tpos = fs_->sp - 1;
                Next();
                ExpDesc k;
                Expr(k);
                Discharge(k);
                CheckNext(']');
                e.k = VINDEXED;
                e.tpos = tpos;
                e.odesc = d;   // GETINDEX failing reports the table
                e.desc.clear();
                break;
            }
            case ':': {
                std::string d = e.desc;
                Next();
                std::string m = CheckName();
                Discharge(e);
                Hint(Emit(SELF, ConstStr(m), 0, 0, 1), d);
                ExpDesc f;
                f.k = VPUSHED;
                f.desc = "method '" + m + "'";
                FuncArgs(f, true);   // the function is below self
                e = f;
                break;
            }
            case '(': case T_STRING: case '{': {
                Discharge(e);
                FuncArgs(e, false);
                break;
            }
            default: return;
        }
    }
}

void Compiler::FuncArgs(ExpDesc& f, bool isSelf) {
    // the function value (and self, for a method call) is already on the stack
    int funcPos = fs_->sp - (isSelf ? 2 : 1);
    int callLine = tok_.line;
    int nargs = isSelf ? 1 : 0;
    bool open = false;
    switch (tok_.t) {
        case '(': {
            int l = tok_.line;
            Next();
            if (tok_.t != ')') {
                ExpDesc a;
                int n = ExpList(a);
                if (IsMulti(a)) { SetMulti(a, -1); open = true; nargs += n - 1; }
                else { Discharge(a); nargs += n; }
            }
            CheckMatch(')', '(', l);
            break;
        }
        case '{': {
            ExpDesc t;
            Constructor(t);
            ++nargs;
            break;
        }
        case T_STRING: {
            Emit(PUSHK, ConstStr(tok_.s), 0, 0, 1);
            Next();
            ++nargs;
            break;
        }
        default: Error("function arguments expected");
    }
    prevLine_ = callLine;
    int pc = Emit(CALL, funcPos, open ? -1 : nargs, 1, 0);
    fs_->sp = funcPos + 1;
    if (fs_->sp > fs_->p->maxStack) fs_->p->maxStack = fs_->sp;
    Hint(pc, f.desc);
    f.k = VCALL;
    f.info = pc;
    f.desc.clear();
}

void Compiler::Constructor(ExpDesc& e) {
    int line = tok_.line;
    CheckNext('{');
    int tpos = fs_->sp;
    int newPc = Emit(NEWTABLE, 0, 0, 0, 1);
    int pending = 0, arrayCount = 0, hashCount = 0;
    auto flush = [&](bool open) {
        Emit(SETLIST, tpos, open ? -1 : pending, arrayCount - pending + 1, 0);
        fs_->sp = tpos + 1;
        pending = 0;
    };
    while (tok_.t != '}') {
        if (tok_.t == T_NAME && Lookahead().t == '=') {
            std::string key = tok_.s;
            Next(); Next();
            ExpDesc v;
            Expr(v);
            Discharge(v);
            Emit(SETFIELDAT, tpos, ConstStr(key), 0, -1);
            ++hashCount;
        } else if (tok_.t == '[') {
            Next();
            ExpDesc k;
            Expr(k);
            Discharge(k);
            CheckNext(']');
            CheckNext('=');
            ExpDesc v;
            Expr(v);
            Discharge(v);
            Emit(SETKV, tpos, 0, 0, -2);
            ++hashCount;
        } else {
            ExpDesc v;
            Expr(v);
            bool sep = Test(',') || Test(';');
            ++arrayCount;
            ++pending;
            if (IsMulti(v) && tok_.t == '}') {
                SetMulti(v, -1);   // the last item gives all its values
                flush(true);
                break;
            }
            Discharge(v);
            if (pending >= 50) flush(false);
            if (!sep) break;
            continue;
        }
        if (!Test(',') && !Test(';')) break;
    }
    CheckMatch('}', '{', line);
    if (pending > 0) flush(false);
    fs_->p->code[(std::size_t)newPc].a = arrayCount;
    fs_->p->code[(std::size_t)newPc].b = hashCount;
    e.k = VPUSHED;
}

void Compiler::SimpleExp(ExpDesc& e) {
    switch (tok_.t) {
        case T_FLT: e = ExpDesc{}; e.k = VFLT; e.nval = tok_.n; Next(); return;
        case T_INT: e = ExpDesc{}; e.k = VINT; e.ival = tok_.i; Next(); return;
        case T_STRING: e = ExpDesc{}; e.k = VKSTR; e.sval = tok_.s; Next(); return;
        case T_NIL: e = ExpDesc{}; e.k = VNIL; Next(); return;
        case T_TRUE: e = ExpDesc{}; e.k = VTRUE; Next(); return;
        case T_FALSE: e = ExpDesc{}; e.k = VFALSE; Next(); return;
        case T_DOTS: {
            if (!fs_->p->isVararg) Error("cannot use '...' outside a vararg function");
            Next();
            e = ExpDesc{};
            e.k = VVARARG;
            e.info = Emit(VARARG, 1, 0, 0, 1);
            return;
        }
        case '{': Constructor(e); return;
        case T_FUNCTION: {
            int line = tok_.line;
            Next();
            Body(e, false, line, "");
            return;
        }
        default: SuffixedExp(e); return;
    }
}

struct BinPri { int left, right; Op op; };
bool BinaryOp(int t, BinPri& out) {
    switch (t) {
        case '+': out = {10, 10, ADD}; return true;
        case '-': out = {10, 10, SUB}; return true;
        case '*': out = {11, 11, MUL}; return true;
        case '%': out = {11, 11, MOD}; return true;
        case '^': out = {14, 13, POW}; return true;
        case '/': out = {11, 11, DIV}; return true;
        case T_IDIV: out = {11, 11, IDIV}; return true;
        case '&': out = {6, 6, BAND}; return true;
        case '|': out = {4, 4, BOR}; return true;
        case '~': out = {5, 5, BXOR}; return true;
        case T_SHL: out = {7, 7, SHL}; return true;
        case T_SHR: out = {7, 7, SHR}; return true;
        case T_CONCAT: out = {9, 8, CONCAT}; return true;
        case T_EQ: out = {3, 3, EQ}; return true;
        case T_NE: out = {3, 3, NE}; return true;
        case '<': out = {3, 3, LT}; return true;
        case T_LE: out = {3, 3, LE}; return true;
        case '>': out = {3, 3, GT}; return true;
        case T_GE: out = {3, 3, GE}; return true;
        case T_AND: out = {2, 2, ANDJ}; return true;
        case T_OR: out = {1, 1, ORJ}; return true;
        default: return false;
    }
}
constexpr int kUnaryPri = 12;

int Compiler::SubExpr(ExpDesc& e, int limit) {
    EnterLevel();
    int ut = tok_.t;
    if (ut == T_NOT || ut == '-' || ut == '#' || ut == '~') {
        int line = tok_.line;
        Next();
        SubExpr(e, kUnaryPri);
        if (ut == '-' && (e.k == VINT || e.k == VFLT)) {
            // fold a negative number literal (integer negation wraps around)
            if (e.k == VINT) e.ival = (std::int64_t)(0ull - (std::uint64_t)e.ival);
            else e.nval = -e.nval;
        } else {
            std::string d = e.desc;
            Discharge(e);
            prevLine_ = line;
            int pc = Emit(ut == T_NOT ? NOT : ut == '-' ? UNM : ut == '#' ? LEN : BNOT, 0, 0, 0, 0);
            Hint(pc, d);
            e = ExpDesc{};
            e.k = VPUSHED;
        }
    } else {
        SimpleExp(e);
    }
    BinPri bp;
    while (BinaryOp(tok_.t, bp) && bp.left > limit) {
        int line = tok_.line;
        Next();
        if (bp.op == ANDJ || bp.op == ORJ) {
            Discharge(e);
            int j = Emit(bp.op, 0, 0, 0, -1);
            ExpDesc e2;
            SubExpr(e2, bp.right);
            Discharge(e2);
            Patch(j, Pc());
            e = ExpDesc{};
            e.k = VPUSHED;
            continue;
        }
        std::string d1 = e.desc;
        Discharge(e);
        ExpDesc e2;
        SubExpr(e2, bp.right);
        std::string d2 = e2.desc;
        Discharge(e2);
        prevLine_ = line;
        int cpc = -1;
        if (bp.op == CONCAT) {
            // fuse a .. (b .. c): the right side's last step was its own CONCAT
            if (e2.concatPc >= 0 && e2.concatPc == Pc() - 1) {
                fs_->p->code.back().a += 1;
                fs_->sp -= 1;
            } else {
                Emit(CONCAT, 2, 0, 0, -1);
            }
            cpc = Pc() - 1;
        } else {
            int pc = Emit(bp.op, 0, 0, 0, -1);
            if (!d1.empty() || !d2.empty()) Hint(pc, d1 + "\n" + d2);
        }
        e = ExpDesc{};
        e.k = VPUSHED;
        e.concatPc = cpc;
    }
    LeaveLevel();
    return 0;
}

Proto* Compiler::Run(std::vector<SyntaxError>& errs) {
    Proto* main = L_.Alloc<Proto>();
    main->name = "main chunk";
    main->isVararg = true;
    UpvalDesc env;
    env.name = "_ENV";
    env.fromLocal = true;
    env.index = 0;
    main->upvals.push_back(env);
    FuncState fs;
    // keep the protos reachable for the collector: it only runs at safe points, which
    // compiling never reaches, so nothing is freed meanwhile.
    try {
        ahead_.t = 0;
        tok_ = lex_.Next();
        StartFunc(fs, main);
        for (;;) {
            try {
                StatList();
                if (tok_.t != T_EOF) Error("'<eof>' expected");
                break;
            } catch (const CompileError& e) {
                SyntaxError se;
                se.bare = e.msg;
                se.line = e.line;
                se.column = e.col;
                se.message = chunk_ + ":" + std::to_string(e.line) + ": " + e.msg;
                errs.push_back(se);
                if (!collect_ || errs.size() >= 20) return nullptr;
                // resync: back to the main function, skip to a token that starts a statement
                fs_ = &fs;
                fs.actives.clear();
                fs.blocks.resize(1);
                fs.labels.clear();
                fs.gotos.clear();
                fs.sp = 0;
                fs.tbcActive = 0;
                depth_ = 0;
                bool moved = false;
                for (;;) {
                    try {
                        if (moved && (tok_.t == T_LOCAL || tok_.t == T_FUNCTION || tok_.t == T_IF || tok_.t == T_WHILE ||
                                      tok_.t == T_FOR || tok_.t == T_REPEAT || tok_.t == T_RETURN || tok_.t == T_EOF))
                            break;
                        if (tok_.t == T_EOF) break;
                        Next();
                        moved = true;
                    } catch (const CompileError&) {
                        // a lexical error while skipping: drop the rest
                        tok_.t = T_EOF;
                        break;
                    }
                }
                if (tok_.t == T_EOF) break;
            }
        }
        if (!errs.empty()) return nullptr;
        EndFunc();
    } catch (const CompileError& e) {
        SyntaxError se;
        se.bare = e.msg;
        se.line = e.line;
        se.column = e.col;
        se.message = chunk_ + ":" + std::to_string(e.line) + ": " + e.msg;
        errs.push_back(se);
        return nullptr;
    }
    return main;
}

} // namespace

Proto* Compile(State& L, const std::string& src, const std::string& chunk, std::vector<SyntaxError>& errs,
               bool collectAll) {
    Compiler c(L, src, chunk, collectAll);
    return c.Run(errs);
}

} // namespace okay::lua
