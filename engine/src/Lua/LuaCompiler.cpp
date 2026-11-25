// okay::lua compiler: lexer, parser and code generator in one pass.
//
// Written from the Lua 5.4 Reference Manual: section 3.1 (lexical conventions),
// 3.2-3.5 (variables, statements, expressions, visibility rules), 3.4.8
// (operator precedence) and 9 (the complete syntax), in our own structure:
// statements are parsed by one function per statement form of section 9,
// expressions by an operator-stack reader over our own level table (see
// kBinaryOperators), and names are resolved by walking out through the
// enclosing functions in a loop. An expression is first recorded as an
// Operand and turned into stack code only when its use is known, so the
// same parse serves a read, an assignment target or a multi-value call.
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
// kKeywords (Lua.hpp) is in the order of T_AND .. T_WHILE.

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
    [[noreturn]] void Fail(const std::string& msg, int line, int col, const std::string& nearText) {
        std::string m = msg;
        if (!nearText.empty()) m += " near " + nearText;
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
//
// An Operand records what a parsed expression is (a constant, a local, an
// upvalue, a field of a table already on the stack, a call, ...) without
// emitting code for it yet; Materialize turns it into "one value on the stack"
// once its use is known, so the same parse serves a read, an assignment target
// or a call whose result count is set later.
enum class Opnd {
    Empty, Nil, True, False, Str, Int, Float,
    Local,       ///< ref = index into FuncState::vars
    Upvalue,     ///< ref = upvalue index
    Keyed,       ///< t[k]: table at tableSlot, key just above it
    NamedField,  ///< t.name: table at tableSlot, keyConst = the name's constant
    EnvGlobal,   ///< _ENV.name with _ENV an upvalue: ref = upvalue, keyConst = name
    Call,        ///< ref = pc of the CALL
    Varargs,     ///< ref = pc of the VARARG
    OnStack,     ///< already one value on the stack
};

struct Operand {
    Opnd kind = Opnd::Empty;
    int ref = 0;
    int keyConst = 0;
    int tableSlot = 0;
    std::int64_t intVal = 0;
    double numVal = 0;
    std::string strVal;
    std::string what;       ///< for error messages, e.g. "local 'x'"
    std::string baseWhat;   ///< Keyed / NamedField: what the indexed value was
    int concatPc = -1;      ///< the expression's last step was this CONCAT (to fuse a .. b .. c)
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

// ---------------------------------------------------------------- operator table
//
// Worked out from manual 3.4.8. Binary operators are grouped in levels, the
// loosest first; a higher level binds tighter. '..' and '^' group to the
// right, all others to the left. The unary operators (not # - ~) sit between
// the multiplicative level and '^', so -x^2 is -(x^2) and 2^-3 is 2^(-3).
struct BinaryOperator {
    int token;
    Op op;
    unsigned char level;
    bool groupsRight;
};
constexpr BinaryOperator kBinaryOperators[] = {
    {T_OR, ORJ, 1, false},
    {T_AND, ANDJ, 2, false},
    {'<', LT, 3, false}, {'>', GT, 3, false}, {T_LE, LE, 3, false}, {T_GE, GE, 3, false},
    {T_NE, NE, 3, false}, {T_EQ, EQ, 3, false},
    {'|', BOR, 4, false},
    {'~', BXOR, 5, false},
    {'&', BAND, 6, false},
    {T_SHL, SHL, 7, false}, {T_SHR, SHR, 7, false},
    {T_CONCAT, CONCAT, 8, true},
    {'+', ADD, 9, false}, {'-', SUB, 9, false},
    {'*', MUL, 10, false}, {'/', DIV, 10, false}, {T_IDIV, IDIV, 10, false}, {'%', MOD, 10, false},
    {'^', POW, 12, true},
};
constexpr unsigned char kUnaryLevel = 11;

const BinaryOperator* FindBinaryOperator(int token) {
    for (const BinaryOperator& b : kBinaryOperators)
        if (b.token == token) return &b;
    return nullptr;
}

bool IsUnaryToken(int t) { return t == T_NOT || t == '-' || t == '#' || t == '~'; }

// The tokens that end a block (manual 9: block is followed by one of them).
bool EndsBlock(int t) {
    return t == T_ELSE || t == T_ELSEIF || t == T_END || t == T_EOF || t == T_UNTIL;
}

constexpr int kMaxNesting = 190;   ///< nested blocks / parenthesised expressions before we give up

class Compiler {
public:
    Compiler(State& L, const std::string& src, const std::string& chunk, bool collectAll)
        : L_(L), lex_(src, chunk), chunk_(chunk), collect_(collectAll) {}
    Proto* Run(std::vector<SyntaxError>& errs);
private:
    // ---- tokens
    void Next() { prevLine_ = tok_.line; tok_ = ahead_.t != 0 ? ahead_ : lex_.Next(); ahead_ = Token{}; ahead_.t = 0; }
    const Token& PeekToken() { if (ahead_.t == 0) ahead_ = lex_.Next(); return ahead_; }
    bool Accept(int t) { if (tok_.t == t) { Next(); return true; } return false; }
    [[noreturn]] void ErrorAt(const std::string& msg, const Token& t) {
        std::string nearText;
        switch (t.t) {
            case T_NAME: case T_STRING: nearText = "'" + t.s.substr(0, 40) + "'"; break;
            case T_INT: case T_FLT: nearText = "'" + t.s + "'"; break;
            default: nearText = TokText(t.t);
        }
        throw CompileError{msg + " near " + nearText, t.line, t.col};
    }
    [[noreturn]] void Error(const std::string& msg) { ErrorAt(msg, tok_); }
    [[noreturn]] void ErrorPlain(const std::string& msg) { throw CompileError{msg, prevLine_, tok_.col}; }
    void Require(int t) { if (tok_.t != t) Error(TokText(t) + " expected"); }
    void Consume(int t) { Require(t); Next(); }
    // the closing token of a construct opened by `opener` on line `line`
    void ConsumeCloser(int closer, int opener, int line) {
        if (Accept(closer)) return;
        if (line == tok_.line) Error(TokText(closer) + " expected");
        Error(TokText(closer) + " expected (to close " + TokText(opener) + " at line " + std::to_string(line) + ")");
    }
    std::string RequireName() { Require(T_NAME); std::string s = tok_.s; Next(); return s; }

    // nesting guard for blocks and sub-expressions (protects the C++ stack)
    struct Nested {
        Compiler& c;
        explicit Nested(Compiler& cc) : c(cc) {
            if (++c.nesting_ > kMaxNesting) {
                --c.nesting_;
                c.Error("code is nested too deeply (more than " + std::to_string(kMaxNesting) + " levels)");
            }
        }
        ~Nested() { --c.nesting_; }
    };

    // ---- emission
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

    // ---- variables and values
    int AddLocal(const std::string& name, int attrib = 0);
    void ActivateLocal(int var);   // the var's slot is the current top - 1
    LocalVar& Var(int i) { return fs_->vars[(std::size_t)i]; }
    void Capture(FuncState* fs, int var);
    static int FindActiveLocal(FuncState* fs, const std::string& name);
    static int FindUpval(FuncState* fs, const std::string& name);
    bool ResolveName(const std::string& name, Operand& e);
    void NameOperand(const std::string& name, Operand& e);
    void Materialize(Operand& e);
    void SetResultCount(Operand& e, int n);   // Call / Varargs produce n results (-1 = all)
    static bool IsOpen(const Operand& e) { return e.kind == Opnd::Call || e.kind == Opnd::Varargs; }
    void StoreTop(Operand& target);          // assign the top value to target (pops it)
    void FitValueCount(int wanted, int given, Operand& last);
    void OpenBlock(BlockScope& bl, bool loop);
    void CloseBlock();
    void PopTo(int sp);   // drop operand / local slots down to sp (static)
    void ResolveGotos(BlockScope& bl, bool atEnd);
    void JumpTo(int pc, const Label& lb);
    void StartFunc(FuncState& fs, Proto* p);
    void EndFunc();

    // ---- statements (manual 3.3, 9)
    void ParseStatements();
    void ParseStatement();
    void ParseScopedBlock();
    int ParseConditionJump();
    void ParseIf(int line);
    void ParseWhile(int line);
    void ParseRepeat(int line);
    void ParseFor(int line);
    void EmitCountingLoop(const std::string& name);
    void EmitIteratorLoop(const std::vector<std::string>& names);
    void ParseFunctionStatement(int line);
    void ParseLocalFunction();
    void ParseLocalNames();
    void ParseReturn();
    void ParseBreak();
    void ParseGoto();
    void DefineLabel(const std::string& name, int line);
    void ParseCallOrAssignment();

    // ---- functions
    struct FunctionHeader { int line; bool isMethod; std::string name; };
    void ParseFunctionBody(Operand& e, const FunctionHeader& h);
    void ParseParameters();

    // ---- expressions (manual 3.4, 9)
    int ParseExpressionList(Operand& last);
    void ParseExpression(Operand& e);
    void ParseOperand(Operand& e);
    void ParsePostfix(Operand& e);
    void ParseBaseValue(Operand& e);
    void IndexByName(Operand& e);
    void IndexByExpression(Operand& e);
    void ParseMethodCall(Operand& e);
    void ParseCallArguments(Operand& callee, bool hasSelf);
    void ParseTableConstructor(Operand& e);
    void ApplyUnary(int token, int line, Operand& e);
    void ApplyBinary(const BinaryOperator& b, int line, const std::string& leftWhat, int jumpPc, Operand& right);

    State& L_;
    Lexer lex_;
    std::string chunk_;
    bool collect_;
    Token tok_, ahead_;
    int prevLine_ = 1;
    int nesting_ = 0;
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
    if (fs_->actives.size() >= 200) Error("a function can have at most 200 local variables in scope at once");
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

int Compiler::FindActiveLocal(FuncState* fs, const std::string& name) {
    for (std::size_t i = fs->actives.size(); i-- > 0;) {
        const int vi = fs->actives[i];
        if (fs->vars[(std::size_t)vi].name == name) return vi;
    }
    return -1;
}

int Compiler::FindUpval(FuncState* fs, const std::string& name) {
    auto& u = fs->p->upvals;
    for (std::size_t i = 0; i < u.size(); ++i)
        if (u[i].name == name) return (int)i;
    return -1;
}

// Find `name` from the current function outwards. A local of the current
// function is used directly. Found further out (as a local or an upvalue of an
// enclosing function), an upvalue is added to every function between that one
// and the current one, outermost first, each pointing at the one outside it.
bool Compiler::ResolveName(const std::string& name, Operand& e) {
    std::vector<FuncState*> path;   // path[0] is the current function
    int local = -1, upval = -1;
    for (FuncState* f = fs_; f && local < 0 && upval < 0; f = f->parent) {
        path.push_back(f);
        local = FindActiveLocal(f, name);
        if (local < 0) upval = FindUpval(f, name);
    }
    if (local < 0 && upval < 0) return false;
    const std::size_t owner = path.size() - 1;
    if (owner == 0) {
        e.kind = local >= 0 ? Opnd::Local : Opnd::Upvalue;
        e.ref = local >= 0 ? local : upval;
        e.what = (local >= 0 ? "local '" : "upvalue '") + name + "'";
        return true;
    }
    UpvalDesc link;
    link.name = name;
    if (local >= 0) {
        Capture(path[owner], local);
        link.fromLocal = true;
        link.index = path[owner]->vars[(std::size_t)local].slot;
    } else {
        link.fromLocal = false;
        link.index = upval;
    }
    for (std::size_t k = owner; k-- > 0;) {
        Proto* p = path[k]->p;
        if (p->upvals.size() >= 255) Error("a function can use at most 255 variables from enclosing functions");
        p->upvals.push_back(link);
        link.fromLocal = false;
        link.index = (int)p->upvals.size() - 1;
    }
    e.kind = Opnd::Upvalue;
    e.ref = link.index;
    e.what = "upvalue '" + name + "'";
    return true;
}

// A name in an expression: a local, an upvalue or a global (a field of _ENV).
void Compiler::NameOperand(const std::string& name, Operand& e) {
    if (ResolveName(name, e)) return;
    Operand env;
    ResolveName("_ENV", env);   // always found: the main chunk's upvalue 0
    const int key = ConstStr(name);
    if (env.kind == Opnd::Upvalue) {
        e.kind = Opnd::EnvGlobal;
        e.ref = env.ref;
    } else {
        Materialize(env);
        e.kind = Opnd::NamedField;
        e.tableSlot = fs_->sp - 1;
    }
    e.keyConst = key;
    e.what = "global '" + name + "'";
}

void Compiler::Materialize(Operand& e) {
    switch (e.kind) {
        case Opnd::Empty: Emit(PUSHNIL, 1, 0, 0, 1); break;   // should not happen; keeps the stack consistent
        case Opnd::Nil: Emit(PUSHNIL, 1, 0, 0, 1); break;
        case Opnd::True: Emit(PUSHTRUE, 0, 0, 0, 1); break;
        case Opnd::False: Emit(PUSHFALSE, 0, 0, 0, 1); break;
        case Opnd::Str: Emit(PUSHK, ConstStr(e.strVal), 0, 0, 1); break;
        case Opnd::Int:
            if (e.intVal >= INT32_MIN && e.intVal <= INT32_MAX) Emit(PUSHINT, (int)e.intVal, 0, 0, 1);
            else Emit(PUSHK, ConstNum(Value::Int(e.intVal)), 0, 0, 1);
            break;
        case Opnd::Float: Emit(PUSHK, ConstNum(Value::Num(e.numVal)), 0, 0, 1); break;
        case Opnd::Local: {
            LocalVar& v = Var(e.ref);
            int pc = Emit(v.captured ? GETBOX : GETLOCAL, v.slot, 0, 0, 1);
            if (!v.captured) v.accessPcs.push_back(pc);
            break;
        }
        case Opnd::Upvalue: Emit(GETUP, e.ref, 0, 0, 1); break;
        case Opnd::Keyed: Hint(Emit(GETINDEX, 0, 0, 0, -1), e.baseWhat); break;
        case Opnd::NamedField: Hint(Emit(GETFIELD, e.keyConst, 0, 0, 0), e.baseWhat); break;
        case Opnd::EnvGlobal: Emit(GETTABUPK, e.ref, e.keyConst, 0, 1); break;
        case Opnd::Call: case Opnd::Varargs: case Opnd::OnStack: break;   // already one value on the stack
    }
    e.kind = Opnd::OnStack;
}

void Compiler::SetResultCount(Operand& e, int n) {
    Instr& in = fs_->p->code[(std::size_t)e.ref];
    if (e.kind == Opnd::Call) {
        // the function sat at slot in.a; results start there
        in.c = n;
        fs_->sp = in.a + (n < 0 ? 0 : n);
    } else {
        in.a = n;
        fs_->sp = fs_->sp - 1 + (n < 0 ? 0 : n);
    }
    if (fs_->sp > fs_->p->maxStack) fs_->p->maxStack = fs_->sp;
}

void Compiler::StoreTop(Operand& t) {
    switch (t.kind) {
        case Opnd::Local: {
            LocalVar& v = Var(t.ref);
            if (v.attrib) ErrorPlain("attempt to assign to const variable '" + v.name + "'");
            int pc = Emit(v.captured ? SETBOX : SETLOCAL, v.slot, 0, 0, -1);
            if (!v.captured) v.accessPcs.push_back(pc);
            break;
        }
        case Opnd::Upvalue: {
            // is the captured variable a const one?
            FuncState* f = fs_;
            int idx = t.ref;
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
            Emit(SETUP, t.ref, 0, 0, -1);
            break;
        }
        case Opnd::EnvGlobal: Emit(SETTABUPK, t.ref, t.keyConst, 0, -1); break;
        case Opnd::Keyed: Hint(Emit(SETINDEXAT, t.tableSlot, 0, 0, -1), t.baseWhat); break;
        case Opnd::NamedField: Hint(Emit(SETFIELDAT, t.tableSlot, t.keyConst, 0, -1), t.baseWhat); break;
        default: Error("syntax error");
    }
}

// `given` expressions were parsed, all but the last one already on the stack;
// make them leave exactly `wanted` values. An open last one (a call or '...')
// supplies whatever is missing; otherwise nils pad or extra values are dropped.
void Compiler::FitValueCount(int wanted, int given, Operand& last) {
    const int surplus = given - wanted;
    if (IsOpen(last)) {
        if (surplus <= 0) { SetResultCount(last, 1 - surplus); return; }
        SetResultCount(last, 0);
        if (surplus > 1) Emit(POP, surplus - 1, 0, 0, 1 - surplus);
        return;
    }
    if (last.kind != Opnd::Empty) Materialize(last);
    if (surplus < 0) Emit(PUSHNIL, -surplus, 0, 0, -surplus);
    else if (surplus > 0) Emit(POP, surplus, 0, 0, -surplus);
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

// ---------------------------------------------------------------- statements

// block ::= {stat} [retstat]; a return statement must be the last one
void Compiler::ParseStatements() {
    while (!EndsBlock(tok_.t)) {
        const bool wasReturn = tok_.t == T_RETURN;
        ParseStatement();
        if (wasReturn) return;
    }
}

void Compiler::ParseScopedBlock() {
    BlockScope bl;
    OpenBlock(bl, false);
    ParseStatements();
    CloseBlock();
}

void Compiler::ParseStatement() {
    const int line = tok_.line;
    Nested guard(*this);
    switch (tok_.t) {
        case ';': Next(); return;
        case T_IF: ParseIf(line); return;
        case T_WHILE: ParseWhile(line); return;
        case T_DO: Next(); ParseScopedBlock(); ConsumeCloser(T_END, T_DO, line); return;
        case T_FOR: ParseFor(line); return;
        case T_REPEAT: ParseRepeat(line); return;
        case T_FUNCTION: ParseFunctionStatement(line); return;
        case T_LOCAL:
            Next();
            if (Accept(T_FUNCTION)) ParseLocalFunction();
            else ParseLocalNames();
            return;
        case T_DBCOLON: {
            Next();
            std::string name = RequireName();
            Consume(T_DBCOLON);
            DefineLabel(name, line);
            return;
        }
        case T_RETURN: Next(); ParseReturn(); return;
        case T_BREAK: ParseBreak(); return;
        case T_GOTO: Next(); ParseGoto(); return;
        default: ParseCallOrAssignment(); return;
    }
}

// a condition followed by the jump that skips what it guards when it is false
int Compiler::ParseConditionJump() {
    Operand cond;
    ParseExpression(cond);
    Materialize(cond);
    return Emit(JMPIFNOT, 0, 0, 0, -1);
}

// if exp then block {elseif exp then block} [else block] end: one arm per condition
void Compiler::ParseIf(int line) {
    std::vector<int> pastEnd;   // the jump at the end of each arm that has a later arm
    for (;;) {
        Next();   // 'if' or 'elseif'
        const int skipArm = ParseConditionJump();
        Consume(T_THEN);
        ParseScopedBlock();
        if (tok_.t == T_ELSE || tok_.t == T_ELSEIF) pastEnd.push_back(Emit(JMP, 0, -1, -1));
        Patch(skipArm, Pc());
        if (tok_.t != T_ELSEIF) break;
    }
    if (Accept(T_ELSE)) ParseScopedBlock();
    ConsumeCloser(T_END, T_IF, line);
    for (int pc : pastEnd) Patch(pc, Pc());
}

void Compiler::ParseWhile(int line) {
    Next();
    const int top = Pc();
    const int leave = ParseConditionJump();
    Consume(T_DO);
    BlockScope bl;
    OpenBlock(bl, true);
    ParseStatements();
    const int sp = fs_->blocks.back().sp;
    // the back jump drops the body's locals (and closes them); breaks go past the loop
    std::vector<int> breaks;
    breaks.swap(fs_->blocks.back().breaks);
    CloseBlock();
    Emit(JMP, top, sp, sp);
    ConsumeCloser(T_END, T_WHILE, line);
    Patch(leave, Pc());
    for (int pc : breaks) Patch(pc, Pc());
}

void Compiler::ParseRepeat(int line) {
    Next();
    const int top = Pc();
    BlockScope loop;
    OpenBlock(loop, true);      // breaks
    BlockScope body;
    OpenBlock(body, false);     // the body's locals are visible in the condition
    ParseStatements();
    ConsumeCloser(T_UNTIL, T_REPEAT, line);
    Operand cond;
    ParseExpression(cond);
    Materialize(cond);
    const int finished = Emit(JMPIF, 0, 0, 0, -1);
    // not finished: drop the body's locals and go round again
    const int sp = fs_->blocks.back().sp;
    ResolveGotos(fs_->blocks.back(), false);
    Emit(JMP, top, sp, sp);
    Patch(finished, Pc());
    CloseBlock();
    CloseBlock();
}

// for Name = exp, exp [, exp] do block end   |   for namelist in explist do block end
void Compiler::ParseFor(int line) {
    Next();
    std::vector<std::string> names{RequireName()};
    BlockScope bl;
    OpenBlock(bl, true);
    if (tok_.t == '=') {
        EmitCountingLoop(names[0]);
    } else {
        if (tok_.t != ',' && tok_.t != T_IN) Error("'=' or 'in' expected");
        while (Accept(',')) names.push_back(RequireName());
        EmitIteratorLoop(names);
    }
    ConsumeCloser(T_END, T_FOR, line);
    CloseBlock();
}

void Compiler::EmitCountingLoop(const std::string& name) {
    Next();   // '='
    const int base = fs_->sp;
    Operand e;
    ParseExpression(e); Materialize(e);
    Consume(',');
    ParseExpression(e); Materialize(e);
    if (Accept(',')) { ParseExpression(e); Materialize(e); }
    else Emit(PUSHINT, 1, 0, 0, 1);
    // three hidden locals hold start, limit and step
    for (int h = 0; h < 3; ++h) {
        int v = AddLocal("(for state)");
        Var(v).slot = base + h;
        fs_->actives.push_back(v);
    }
    Consume(T_DO);
    const int prep = Emit(FORPREP, base, 0, 0, 1);   // pushes the loop variable (on iteration)
    const int body = Pc();
    BlockScope inner;
    OpenBlock(inner, false);
    ActivateLocal(AddLocal(name));
    ParseStatements();
    CloseBlock();   // pops the body's locals; the loop variable's slot stays for FORLOOP
    Emit(FORLOOP, base, body, 0, -1);
    fs_->p->code[(std::size_t)prep].b = Pc();
}

void Compiler::EmitIteratorLoop(const std::vector<std::string>& names) {
    const int base = fs_->sp;
    Consume(T_IN);
    const int line = tok_.line;
    Operand last;
    const int given = ParseExpressionList(last);
    FitValueCount(4, given, last);   // iterator, state, control, closing value
    for (int i = 0; i < 4; ++i) {
        int v = AddLocal("(for state)");
        Var(v).slot = base + i;
        fs_->actives.push_back(v);
    }
    // the fourth value is closed when the loop ends
    Var(fs_->actives.back()).attrib = 2;
    ++fs_->tbcActive;
    Emit(TBC, base + 3);
    Consume(T_DO);
    const int top = Pc();
    Emit(GETLOCAL, base, 0, 0, 1);
    Emit(GETLOCAL, base + 1, 0, 0, 1);
    Emit(GETLOCAL, base + 2, 0, 0, 1);
    prevLine_ = line;
    const int call = Emit(CALL, base + 4, 2, (int)names.size(), 0);
    fs_->sp = base + 4 + (int)names.size();
    if (fs_->sp > fs_->p->maxStack) fs_->p->maxStack = fs_->sp;
    Hint(call, "for iterator 'for iterator'");
    const int test = Emit(TFORTEST, base, 0, 0, 0);
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
    ParseStatements();
    CloseBlock();
    Emit(JMP, top, base + 4, base + 4);
    fs_->p->code[(std::size_t)test].b = Pc();
    // the outer block's end closes the fourth value and pops the hidden slots
}

void Compiler::DefineLabel(const std::string& name, int line) {
    for (const Label& lb : fs_->labels)
        if (lb.name == name) ErrorPlain("label '" + name + "' already defined on line " + std::to_string(lb.line));
    while (tok_.t == ';') Next();   // empty statements after a label do not count
    Label lb;
    lb.name = name;
    lb.pc = Pc();
    lb.line = line;
    lb.block = fs_->blocks.back().id;
    lb.nactive = (int)fs_->actives.size();
    lb.sp = fs_->sp;
    if (EndsBlock(tok_.t) && tok_.t != T_UNTIL) {
        // at the end of its block the label is outside the block's locals; a jump arriving
        // from before them raises the top to sp with nils (CloseBlock's POP then drops them)
        lb.nactive = fs_->blocks.back().nactive;
    }
    fs_->labels.push_back(lb);
    // gotos earlier in this block (or moved here from nested blocks) may jump to it
    ResolveGotos(fs_->blocks.back(), false);
}

void Compiler::ParseGoto() {
    const int line = tok_.line, col = tok_.col;
    std::string name = RequireName();
    const int pc = Emit(JMP, 0, 0, 0);
    // a visible label already defined: a backward jump, resolved now
    for (std::size_t i = fs_->labels.size(); i-- > 0;) {
        if (fs_->labels[i].name != name) continue;
        JumpTo(pc, fs_->labels[i]);
        return;
    }
    fs_->gotos.push_back(PendingGoto{name, pc, (int)fs_->actives.size(), line, col, fs_->blocks.back().id});
}

void Compiler::ParseBreak() {
    const int line = tok_.line;
    Next();
    for (std::size_t i = fs_->blocks.size(); i-- > 0;) {
        BlockScope& bl = fs_->blocks[i];
        if (!bl.isLoop) continue;
        bl.breaks.push_back(Emit(JMP, 0, bl.sp, bl.sp));
        return;
    }
    throw CompileError{"break outside a loop at line " + std::to_string(line), line, 1};
}

void Compiler::ParseReturn() {
    const int first = fs_->sp;
    if (EndsBlock(tok_.t) || tok_.t == ';') {
        Emit(RETURN, first, 0);
    } else {
        Operand last;
        const int count = ParseExpressionList(last);
        if (IsOpen(last)) {
            // a lone call with nothing to close becomes a tail call
            if (count == 1 && last.kind == Opnd::Call && fs_->tbcActive == 0)
                fs_->p->code[(std::size_t)last.ref].op = TAILCALL;
            SetResultCount(last, -1);
            Emit(RETURN, first, -1);
        } else {
            Materialize(last);
            Emit(RETURN, first, count);
        }
    }
    Accept(';');
    fs_->sp = first;
    if (!EndsBlock(tok_.t)) Error("'<eof>' expected");
}

// function funcname body, funcname ::= Name {'.' Name} [':' Name]
void Compiler::ParseFunctionStatement(int line) {
    Next();
    std::string full = RequireName();
    Operand target;
    NameOperand(full, target);
    bool method = false;
    while (!method && (tok_.t == '.' || tok_.t == ':')) {
        method = tok_.t == ':';
        Next();
        const std::string key = RequireName();
        full += (method ? ":" : ".") + key;
        const std::string base = target.what;
        Materialize(target);
        target.baseWhat = base;
        target.kind = Opnd::NamedField;
        target.tableSlot = fs_->sp - 1;
        target.keyConst = ConstStr(key);
        target.what = "field '" + key + "'";
    }
    Operand fn;
    ParseFunctionBody(fn, FunctionHeader{line, method, full});
    if (target.kind == Opnd::NamedField) Hint(Emit(SETFIELD, target.keyConst, 0, 0, -2), target.baseWhat);
    else StoreTop(target);
}

void Compiler::ParseLocalFunction() {
    const std::string name = RequireName();
    const int var = AddLocal(name);
    Emit(PUSHNIL, 1, 0, 0, 1);
    ActivateLocal(var);   // in scope inside its own body, so it can call itself
    Operand fn;
    ParseFunctionBody(fn, FunctionHeader{tok_.line, false, name});
    LocalVar& v = Var(var);
    const int pc = Emit(v.captured ? SETBOX : SETLOCAL, v.slot, 0, 0, -1);
    if (!v.captured) v.accessPcs.push_back(pc);
}

// local attnamelist ['=' explist]
void Compiler::ParseLocalNames() {
    std::vector<int> vars;
    bool sawClose = false;
    do {
        const std::string name = RequireName();
        int attrib = 0;
        if (Accept('<')) {
            const std::string a = RequireName();
            if (a == "const") attrib = 1;
            else if (a == "close") attrib = 2;
            else Error("unknown attribute '" + a + "'");
            Consume('>');
            if (attrib == 2) {
                if (sawClose) Error("multiple to-be-closed variables in local list");
                sawClose = true;
            }
        }
        vars.push_back(AddLocal(name, attrib));
    } while (Accept(','));
    const int count = (int)vars.size();
    if (Accept('=')) {
        Operand last;
        const int given = ParseExpressionList(last);
        FitValueCount(count, given, last);
    } else {
        Emit(PUSHNIL, count, 0, 0, count);
    }
    // the values sit in the new locals' slots; now they come into scope
    fs_->sp -= count;
    for (int v : vars) {
        fs_->sp++;
        ActivateLocal(v);
        if (Var(v).attrib == 2) {
            Emit(TBC, Var(v).slot);
            ++fs_->tbcActive;
        }
    }
}

// varlist '=' explist   |   functioncall
void Compiler::ParseCallOrAssignment() {
    Operand first;
    ParsePostfix(first);
    if (tok_.t != '=' && tok_.t != ',') {
        if (first.kind != Opnd::Call) Error("syntax error");
        SetResultCount(first, 0);
        return;
    }
    auto assignable = [&](const Operand& t) {
        if (t.kind != Opnd::Local && t.kind != Opnd::Upvalue && t.kind != Opnd::Keyed &&
            t.kind != Opnd::NamedField && t.kind != Opnd::EnvGlobal)
            Error("syntax error");
    };
    std::vector<Operand> targets{first};
    assignable(first);
    while (Accept(',')) {
        Operand t;
        ParsePostfix(t);
        assignable(t);
        targets.push_back(t);
    }
    Consume('=');
    // the slots the targets left on the stack (tables and keys), dropped at the end
    int leftBelow = 0;
    for (const Operand& t : targets) leftBelow += t.kind == Opnd::Keyed ? 2 : t.kind == Opnd::NamedField ? 1 : 0;
    Operand last;
    const int given = ParseExpressionList(last);
    const int count = (int)targets.size();
    if (count == 1 && given == 1 && !IsOpen(last)) {
        // a single assignment stores straight into the table slots it left
        Materialize(last);
        Operand& t = targets[0];
        if (t.kind == Opnd::Keyed) { Hint(Emit(SETINDEX, 0, 0, 0, -3), t.baseWhat); return; }
        if (t.kind == Opnd::NamedField) { Hint(Emit(SETFIELD, t.keyConst, 0, 0, -2), t.baseWhat); return; }
        StoreTop(t);
        return;
    }
    FitValueCount(count, given, last);
    for (std::size_t i = targets.size(); i-- > 0;) StoreTop(targets[i]);
    if (leftBelow) Emit(POP, leftBelow, 0, 0, -leftBelow);
}

// ---------------------------------------------------------------- functions

void Compiler::ParseParameters() {
    Proto* p = fs_->p;
    int named = 0;
    if (tok_.t != ')') {
        do {
            if (tok_.t == T_DOTS) {
                Next();
                p->isVararg = true;
                break;
            }
            if (tok_.t != T_NAME) Error("<name> expected");
            const int v = AddLocal(tok_.s);
            Next();
            fs_->sp++;
            ActivateLocal(v);
            ++named;
        } while (Accept(','));
    }
    p->numParams = named;
}

// funcbody ::= '(' [parlist] ')' block end
void Compiler::ParseFunctionBody(Operand& e, const FunctionHeader& h) {
    Proto* p = L_.Alloc<Proto>();
    p->lineDefined = h.line;
    p->name = h.name.empty() ? "anonymous function" : "function '" + h.name + "'";
    // the new function is reachable from its parent's list, which is reachable from keep_
    fs_->p->protos.push_back(p);
    const int index = (int)fs_->p->protos.size() - 1;
    FuncState fs;
    StartFunc(fs, p);
    if (h.isMethod) {
        const int self = AddLocal("self");
        fs_->sp++;
        ActivateLocal(self);
    }
    Consume('(');
    ParseParameters();
    if (h.isMethod) p->numParams += 1;
    Consume(')');
    ParseStatements();
    ConsumeCloser(T_END, T_FUNCTION, h.line);
    EndFunc();
    Emit(CLOSURE, index, 0, 0, 1);
    e.kind = Opnd::OnStack;
}

// ---------------------------------------------------------------- expressions

// explist ::= exp {',' exp}; all but the last are put on the stack, the last is left open
int Compiler::ParseExpressionList(Operand& last) {
    int count = 1;
    ParseExpression(last);
    while (Accept(',')) {
        Materialize(last);
        last = Operand{};
        ParseExpression(last);
        ++count;
    }
    return count;
}

// The operand grammar (manual 9): simpleexp and the prefixexp chains.
void Compiler::ParseOperand(Operand& e) {
    e = Operand{};
    switch (tok_.t) {
        case T_FLT: e.kind = Opnd::Float; e.numVal = tok_.n; Next(); return;
        case T_INT: e.kind = Opnd::Int; e.intVal = tok_.i; Next(); return;
        case T_STRING: e.kind = Opnd::Str; e.strVal = tok_.s; Next(); return;
        case T_NIL: e.kind = Opnd::Nil; Next(); return;
        case T_TRUE: e.kind = Opnd::True; Next(); return;
        case T_FALSE: e.kind = Opnd::False; Next(); return;
        case T_DOTS:
            if (!fs_->p->isVararg) Error("cannot use '...' outside a vararg function");
            Next();
            e.kind = Opnd::Varargs;
            e.ref = Emit(VARARG, 1, 0, 0, 1);
            return;
        case '{': ParseTableConstructor(e); return;
        case T_FUNCTION: {
            const int line = tok_.line;
            Next();
            ParseFunctionBody(e, FunctionHeader{line, false, ""});
            return;
        }
        default: ParsePostfix(e); return;
    }
}

// Name or '(' exp ')': where every prefixexp starts
void Compiler::ParseBaseValue(Operand& e) {
    if (tok_.t == T_NAME) {
        const std::string name = tok_.s;
        Next();
        NameOperand(name, e);
        return;
    }
    if (tok_.t == '(') {
        const int line = tok_.line;
        Next();
        ParseExpression(e);
        ConsumeCloser(')', '(', line);
        Materialize(e);   // (f()) is one value
        e.what.clear();
        e.concatPc = -1;
        return;
    }
    Error("unexpected symbol");
}

// a base value followed by any number of .name, [exp], :name args and args
void Compiler::ParsePostfix(Operand& e) {
    ParseBaseValue(e);
    for (;;) {
        switch (tok_.t) {
            case '.': IndexByName(e); break;
            case '[': IndexByExpression(e); break;
            case ':': ParseMethodCall(e); break;
            case '(': case T_STRING: case '{':
                Materialize(e);
                ParseCallArguments(e, false);
                break;
            default: return;
        }
    }
}

void Compiler::IndexByName(Operand& e) {
    const std::string base = e.what;
    Materialize(e);
    Next();   // '.'
    const std::string key = RequireName();
    e.kind = Opnd::NamedField;
    e.tableSlot = fs_->sp - 1;
    e.keyConst = ConstStr(key);
    e.what = "field '" + key + "'";
    e.baseWhat = base;   // a failure to index reports what the indexed value was
}

void Compiler::IndexByExpression(Operand& e) {
    const std::string base = e.what;
    Materialize(e);
    const int slot = fs_->sp - 1;
    Next();   // '['
    Operand key;
    ParseExpression(key);
    Materialize(key);
    Consume(']');
    e.kind = Opnd::Keyed;
    e.tableSlot = slot;
    e.baseWhat = base;   // GETINDEX failing reports the table
    e.what.clear();
}

void Compiler::ParseMethodCall(Operand& e) {
    const std::string base = e.what;
    Next();   // ':'
    const std::string method = RequireName();
    Materialize(e);
    Hint(Emit(SELF, ConstStr(method), 0, 0, 1), base);
    Operand callee;
    callee.kind = Opnd::OnStack;
    callee.what = "method '" + method + "'";
    ParseCallArguments(callee, true);   // the function is below self
    e = callee;
}

// args ::= '(' [explist] ')' | tableconstructor | LiteralString
void Compiler::ParseCallArguments(Operand& callee, bool hasSelf) {
    // the function value (and self, for a method call) is already on the stack
    const int funcSlot = fs_->sp - (hasSelf ? 2 : 1);
    const int callLine = tok_.line;
    int argCount = hasSelf ? 1 : 0;
    bool openEnded = false;
    if (tok_.t == '(') {
        const int line = tok_.line;
        Next();
        if (tok_.t != ')') {
            Operand last;
            const int given = ParseExpressionList(last);
            if (IsOpen(last)) { SetResultCount(last, -1); openEnded = true; argCount += given - 1; }
            else { Materialize(last); argCount += given; }
        }
        ConsumeCloser(')', '(', line);
    } else if (tok_.t == '{') {
        Operand table;
        ParseTableConstructor(table);
        ++argCount;
    } else if (tok_.t == T_STRING) {
        Emit(PUSHK, ConstStr(tok_.s), 0, 0, 1);
        Next();
        ++argCount;
    } else {
        Error("function arguments expected");
    }
    prevLine_ = callLine;
    const int pc = Emit(CALL, funcSlot, openEnded ? -1 : argCount, 1, 0);
    fs_->sp = funcSlot + 1;
    if (fs_->sp > fs_->p->maxStack) fs_->p->maxStack = fs_->sp;
    Hint(pc, callee.what);
    callee.kind = Opnd::Call;
    callee.ref = pc;
    callee.what.clear();
}

// tableconstructor ::= '{' [field {sep field} [sep]] '}'
void Compiler::ParseTableConstructor(Operand& e) {
    const int line = tok_.line;
    Consume('{');
    const int tableSlot = fs_->sp;
    const int newPc = Emit(NEWTABLE, 0, 0, 0, 1);
    int waiting = 0, listItems = 0, keyedItems = 0;   // list items not yet stored, totals
    auto storeList = [&](bool openEnded) {
        Emit(SETLIST, tableSlot, openEnded ? -1 : waiting, listItems - waiting + 1, 0);
        fs_->sp = tableSlot + 1;
        waiting = 0;
    };
    while (tok_.t != '}') {
        if (tok_.t == T_NAME && PeekToken().t == '=') {          // name = exp
            const std::string key = tok_.s;
            Next(); Next();
            Operand v;
            ParseExpression(v);
            Materialize(v);
            Emit(SETFIELDAT, tableSlot, ConstStr(key), 0, -1);
            ++keyedItems;
        } else if (tok_.t == '[') {                               // [exp] = exp
            Next();
            Operand k;
            ParseExpression(k);
            Materialize(k);
            Consume(']');
            Consume('=');
            Operand v;
            ParseExpression(v);
            Materialize(v);
            Emit(SETKV, tableSlot, 0, 0, -2);
            ++keyedItems;
        } else {                                                  // exp (a list item)
            Operand v;
            ParseExpression(v);
            const bool separated = Accept(',') || Accept(';');
            ++listItems;
            ++waiting;
            if (IsOpen(v) && tok_.t == '}') {
                SetResultCount(v, -1);   // the last item gives all its values
                storeList(true);
                break;
            }
            Materialize(v);
            if (waiting >= 50) storeList(false);
            if (!separated) break;
            continue;
        }
        if (!Accept(',') && !Accept(';')) break;
    }
    ConsumeCloser('}', '{', line);
    if (waiting > 0) storeList(false);
    fs_->p->code[(std::size_t)newPc].a = listItems;
    fs_->p->code[(std::size_t)newPc].b = keyedItems;
    e.kind = Opnd::OnStack;
}

void Compiler::ApplyUnary(int token, int line, Operand& e) {
    if (token == '-' && (e.kind == Opnd::Int || e.kind == Opnd::Float)) {
        // fold a negative number literal (integer negation wraps around)
        if (e.kind == Opnd::Int) e.intVal = (std::int64_t)(0ull - (std::uint64_t)e.intVal);
        else e.numVal = -e.numVal;
        return;
    }
    const std::string what = e.what;
    Materialize(e);
    prevLine_ = line;
    const Op op = token == T_NOT ? NOT : token == '-' ? UNM : token == '#' ? LEN : BNOT;
    Hint(Emit(op, 0, 0, 0, 0), what);
    e = Operand{};
    e.kind = Opnd::OnStack;
}

// Combine the left value (already on the stack; for and / or its jump is at
// jumpPc) with `right`, the right operand.
void Compiler::ApplyBinary(const BinaryOperator& b, int line, const std::string& leftWhat, int jumpPc, Operand& right) {
    const std::string rightWhat = right.what;
    Materialize(right);
    int concatPc = -1;
    if (b.op == ANDJ || b.op == ORJ) {
        Patch(jumpPc, Pc());
    } else if (b.op == CONCAT) {
        prevLine_ = line;
        // a .. (b .. c): when the right side ended in its own CONCAT, widen that one
        if (right.concatPc >= 0 && right.concatPc == Pc() - 1) {
            fs_->p->code.back().a += 1;
            fs_->sp -= 1;
        } else {
            Emit(CONCAT, 2, 0, 0, -1);
        }
        concatPc = Pc() - 1;
    } else {
        prevLine_ = line;
        const int pc = Emit(b.op, 0, 0, 0, -1);
        if (!leftWhat.empty() || !rightWhat.empty()) Hint(pc, leftWhat + "\n" + rightWhat);
    }
    right = Operand{};
    right.kind = Opnd::OnStack;
    right.concatPc = concatPc;
}

// exp (manual 3.4): operands and operators are read left to right with a stack
// of operators still waiting for their right operand. Before an operator is
// pushed, the waiting ones that bind at least as tightly (and group to the
// left) are applied. Because the stack machine keeps every left operand on
// the stack, only the operand being read now is held as an Operand.
void Compiler::ParseExpression(Operand& e) {
    Nested guard(*this);
    struct Waiting {
        const BinaryOperator* binary;   ///< null for a unary operator
        int token, line, jumpPc;
        std::string leftWhat;
        unsigned char level() const { return binary ? binary->level : kUnaryLevel; }
    };
    std::vector<Waiting> waiting;
    auto applyTop = [&]() {
        Waiting w = std::move(waiting.back());
        waiting.pop_back();
        if (w.binary) ApplyBinary(*w.binary, w.line, w.leftWhat, w.jumpPc, e);
        else ApplyUnary(w.token, w.line, e);
    };
    for (;;) {
        while (IsUnaryToken(tok_.t)) {
            waiting.push_back(Waiting{nullptr, tok_.t, tok_.line, -1, std::string()});
            Next();
        }
        ParseOperand(e);
        const BinaryOperator* b = FindBinaryOperator(tok_.t);
        if (!b) break;
        while (!waiting.empty() &&
               (waiting.back().level() > b->level || (waiting.back().level() == b->level && !b->groupsRight)))
            applyTop();
        const int line = tok_.line;
        Next();
        Waiting w{b, b->token, line, -1, e.what};
        Materialize(e);
        if (b->op == ANDJ || b->op == ORJ) w.jumpPc = Emit(b->op, 0, 0, 0, -1);
        waiting.push_back(std::move(w));
    }
    while (!waiting.empty()) applyTop();
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
                ParseStatements();
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
                nesting_ = 0;
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
