#include "preproc.hpp"

#include "lexer.hpp"
#include "token.hpp"

#include <cstdint>
#include <deque>
#include <initializer_list>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace okaycc {
namespace {

constexpr int kMaxIncludeDepth = 64;
constexpr int kMaxExpandSteps = 100000;
constexpr int kMaxExpandNest = 256;

bool punct(const Token& t, const char* s) { return t.kind == Tok::Punct && t.text == s; }
bool isPasteTok(const Token& t) { return punct(t, "##") || punct(t, "%:%:"); }
bool isHashTok(const Token& t) { return punct(t, "#") || punct(t, "%:"); }

bool hidden(const Token& t, const std::string& name) {
    for (const std::string& h : t.hide)
        if (h == name) return true;
    return false;
}

Token makeNum(std::uint64_t v, const Token& at, bool uns = false) {
    Token t = at;
    t.kind = Tok::Int;
    t.text = std::to_string(v);
    t.uval = v;
    t.isUnsigned = uns;
    t.base = 10;
    t.longRank = 0;
    t.fval = 0;
    t.fromExpansion = true;
    return t;
}

std::string printTokens(const std::vector<Token>& toks) {
    std::string s;
    bool start = true;
    for (const Token& t : toks) {
        if (t.kind == Tok::Newline) {
            s.push_back('\n');
            start = true;
            continue;
        }
        if (t.kind == Tok::End || t.kind == Tok::Placemarker) continue;
        if (!start) s.push_back(' ');
        s += t.text;
        start = false;
    }
    return s;
}

struct Macro {
    std::string name;
    bool functionLike = false;
    bool variadic = false;
    std::vector<std::string> params;
    std::vector<Token> body;
    int line = 1;
    std::string file;
};

bool sameMacro(const Macro& a, const Macro& b) {
    if (a.functionLike != b.functionLike || a.variadic != b.variadic || a.params != b.params ||
        a.body.size() != b.body.size())
        return false;
    for (std::size_t i = 0; i < a.body.size(); ++i) {
        if (a.body[i].kind != b.body[i].kind || a.body[i].text != b.body[i].text) return false;
        if (i > 0 && a.body[i].leadSpace != b.body[i].leadSpace) return false;
    }
    return true;
}

// Integer constant expressions for #if / #elif. 64-bit two's complement, the
// stand-in for intmax_t / uintmax_t (C17 6.10.1). &&, || and ?: do not evaluate
// the arm they skip, so a skipped division by zero is not an error.
struct Val {
    std::uint64_t bits = 0;
    bool uns = false;
};

Val vSigned(std::int64_t s) {
    Val v;
    v.bits = static_cast<std::uint64_t>(s);
    v.uns = false;
    return v;
}
Val vUnsigned(std::uint64_t u) {
    Val v;
    v.bits = u;
    v.uns = true;
    return v;
}
std::int64_t asSigned(Val v) { return static_cast<std::int64_t>(v.bits); }
bool isTrue(Val v) { return v.bits != 0; }

class IfEval {
public:
    IfEval(const std::vector<Token>& toks, const Token& at, Diagnostics& diags)
        : toks_(toks), at_(at), diags_(diags) {}

    bool run(bool& value) {
        if (toks_.empty()) {
            err("expected a constant expression");
            return false;
        }
        Val v = parseCond(true);
        if (bad_) return false;
        if (i_ < toks_.size()) {
            err("extra tokens in #if expression");
            return false;
        }
        value = isTrue(v);
        return true;
    }

private:
    const std::vector<Token>& toks_;
    Token at_;
    Diagnostics& diags_;
    std::size_t i_ = 0;
    bool bad_ = false;

    void err(const std::string& msg) {
        if (bad_) return;
        bad_ = true;
        const Token& t = i_ < toks_.size() ? toks_[i_] : at_;
        diags_.error(t.file.empty() ? at_.file : t.file, t.line ? t.line : at_.line,
                     t.column ? t.column : at_.column, msg);
    }
    const Token* cur() const { return i_ < toks_.size() ? &toks_[i_] : nullptr; }
    bool eat(const char* s) {
        if (cur() && punct(*cur(), s)) {
            ++i_;
            return true;
        }
        return false;
    }

    Val parseCond(bool ev) {
        Val c = parseLor(ev);
        if (!eat("?")) return c;
        Val a = parseCond(ev && isTrue(c));
        if (!eat(":")) err("expected ':' in conditional expression");
        Val b = parseCond(ev && !isTrue(c));
        if (!ev || bad_) return {};
        return isTrue(c) ? a : b;
    }
    Val parseLor(bool ev) {
        Val l = parseLand(ev);
        while (eat("||")) {
            bool takeRight = ev && !isTrue(l);
            Val r = parseLand(takeRight);
            if (!ev || bad_) continue;
            if (isTrue(l)) l = vSigned(1);
            else l = vSigned(takeRight && isTrue(r) ? 1 : 0);
        }
        return l;
    }
    Val parseLand(bool ev) {
        Val l = parseBor(ev);
        while (eat("&&")) {
            bool right = ev && isTrue(l);
            Val r = parseBor(right);
            if (ev && !bad_) {
                if (!isTrue(l)) l = vSigned(0);
                else l = vSigned(isTrue(r) ? 1 : 0);
            }
        }
        return l;
    }
    Val parseBor(bool ev) { return parseBin(ev, &IfEval::parseXor, {"|"}); }
    Val parseXor(bool ev) { return parseBin(ev, &IfEval::parseBand, {"^"}); }
    Val parseBand(bool ev) { return parseBin(ev, &IfEval::parseEq, {"&"}); }
    Val parseEq(bool ev) { return parseBin(ev, &IfEval::parseRel, {"==", "!="}); }
    Val parseRel(bool ev) { return parseBin(ev, &IfEval::parseShift, {"<", ">", "<=", ">="}); }
    Val parseShift(bool ev) { return parseBin(ev, &IfEval::parseAdd, {"<<", ">>"}); }
    Val parseAdd(bool ev) { return parseBin(ev, &IfEval::parseMul, {"+", "-"}); }
    Val parseMul(bool ev) { return parseBin(ev, &IfEval::parseUnary, {"*", "/", "%"}); }

    using ParseFn = Val (IfEval::*)(bool);
    Val parseBin(bool ev, ParseFn next, std::initializer_list<const char*> ops) {
        Val l = (this->*next)(ev);
        for (;;) {
            const char* op = nullptr;
            for (const char* s : ops) {
                if (eat(s)) {
                    op = s;
                    break;
                }
            }
            if (!op) break;
            Val r = (this->*next)(ev);
            if (ev && !bad_) l = apply(op, l, r);
        }
        return l;
    }

    Val apply(const char* op, Val a, Val b) {
        bool uns = a.uns || b.uns;
        std::string o = op;
        if (o == "+" || o == "-" || o == "*" || o == "/" || o == "%" || o == "&" || o == "|" || o == "^" ||
            o == "<<" || o == ">>") {
            if (o == "/" || o == "%") {
                if (b.bits == 0) {
                    err(o == "/" ? "division by zero in #if" : "remainder by zero in #if");
                    return {};
                }
                if (!uns && asSigned(a) == std::numeric_limits<std::int64_t>::min() && asSigned(b) == -1) {
                    err("integer overflow in #if");
                    return {};
                }
                if (uns) {
                    std::uint64_t r = o == "/" ? a.bits / b.bits : a.bits % b.bits;
                    return vUnsigned(r);
                }
                std::int64_t x = asSigned(a), y = asSigned(b);
                std::int64_t r = o == "/" ? x / y : x % y;
                return vSigned(r);
            }
            if (o == "<<") return shift(a, b, true);
            if (o == ">>") return shift(a, b, false);
            std::uint64_t r = 0;
            if (o == "+") r = a.bits + b.bits;
            else if (o == "-") r = a.bits - b.bits;
            else if (o == "*") r = a.bits * b.bits;
            else if (o == "&") r = a.bits & b.bits;
            else if (o == "|") r = a.bits | b.bits;
            else r = a.bits ^ b.bits;
            return uns ? vUnsigned(r) : vSigned(static_cast<std::int64_t>(r));
        }
        bool cmp = false;
        if (uns) {
            if (o == "<") cmp = a.bits < b.bits;
            else if (o == ">") cmp = a.bits > b.bits;
            else if (o == "<=") cmp = a.bits <= b.bits;
            else if (o == ">=") cmp = a.bits >= b.bits;
            else if (o == "==") cmp = a.bits == b.bits;
            else cmp = a.bits != b.bits;
        } else {
            std::int64_t x = asSigned(a), y = asSigned(b);
            if (o == "<") cmp = x < y;
            else if (o == ">") cmp = x > y;
            else if (o == "<=") cmp = x <= y;
            else if (o == ">=") cmp = x >= y;
            else if (o == "==") cmp = x == y;
            else cmp = x != y;
        }
        return vSigned(cmp ? 1 : 0);
    }

    Val shift(Val a, Val b, bool left) {
        std::int64_t nsigned = b.uns ? static_cast<std::int64_t>(b.bits) : asSigned(b);
        if (nsigned < 0 || nsigned >= 64) {
            err("shift count out of range in #if");
            return {};
        }
        int n = static_cast<int>(nsigned);
        if (n == 0) return a.uns ? vUnsigned(a.bits) : vSigned(asSigned(a));
        if (left) {
            std::uint64_t r = a.bits << static_cast<unsigned>(n);
            return a.uns ? vUnsigned(r) : vSigned(static_cast<std::int64_t>(r));
        }
        if (a.uns) return vUnsigned(a.bits >> static_cast<unsigned>(n));
        std::int64_t s = asSigned(a);
        if (s >= 0) return vSigned(s >> static_cast<unsigned>(n));
        std::uint64_t u = static_cast<std::uint64_t>(s);
        std::uint64_t r = (u >> static_cast<unsigned>(n)) | (~std::uint64_t{0} << (64 - n));
        return vSigned(static_cast<std::int64_t>(r));
    }

    Val parseUnary(bool ev) {
        if (eat("+")) {
            Val v = parseUnary(ev);
            return ev ? v : Val{};
        }
        if (eat("-")) {
            Val v = parseUnary(ev);
            if (!ev || bad_) return {};
            if (!v.uns && asSigned(v) == std::numeric_limits<std::int64_t>::min()) {
                err("integer overflow in #if");
                return {};
            }
            if (v.uns) return vUnsigned(0 - v.bits);
            return vSigned(-asSigned(v));
        }
        if (eat("!")) {
            Val v = parseUnary(ev);
            if (!ev || bad_) return {};
            return vSigned(isTrue(v) ? 0 : 1);
        }
        if (eat("~")) {
            Val v = parseUnary(ev);
            if (!ev || bad_) return {};
            std::uint64_t r = ~v.bits;
            return v.uns ? vUnsigned(r) : vSigned(static_cast<std::int64_t>(r));
        }
        return parsePrimary(ev);
    }

    Val parsePrimary(bool ev) {
        const Token* t = cur();
        if (!t) {
            err("expected a constant expression");
            return {};
        }
        if (punct(*t, "(")) {
            ++i_;
            Val v = parseCond(ev);
            if (!eat(")")) err("expected ')'");
            return v;
        }
        if (t->kind == Tok::Int) {
            ++i_;
            if (!ev) return {};
            if (t->isUnsigned || t->uval > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
                return vUnsigned(t->uval);
            return vSigned(static_cast<std::int64_t>(t->uval));
        }
        if (t->kind == Tok::CharLit) {
            ++i_;
            if (!ev) return {};
            return vSigned(static_cast<std::int64_t>(t->charValue));
        }
        if (t->kind == Tok::Float) {
            err("floating constant in #if");
            ++i_;
            return {};
        }
        if (t->kind == Tok::StringLit) {
            err("string literal in #if");
            ++i_;
            return {};
        }
        if (t->isName()) {
            err("identifier in #if was not replaced");
            ++i_;
            return {};
        }
        err("token is not allowed in #if");
        ++i_;
        return {};
    }
};

class Preprocessor {
public:
    Preprocessor(FileStore& files, Diagnostics& diags, std::vector<std::string> includes)
        : files_(files), diags_(diags), includes_(std::move(includes)) {
        Macro builtin;
        builtin.name = "__okaycc__";
        Token one;
        one.kind = Tok::Int;
        one.text = "1";
        one.uval = 1;
        builtin.body.push_back(one);
        macros_["__okaycc__"] = std::move(builtin);
        // The target: x86-64 Linux, LP64 (System V AMD64 ABI). __GNUC__ is not defined: okay-cc
        // has none of GCC's extensions.
        for (const char* name : {"__x86_64__", "__x86_64", "__linux__", "__linux", "__LP64__", "__ELF__"})
            define(name, 1);
    }
    void define(const char* name, unsigned long value) {
        Macro m;
        m.name = name;
        Token t;
        t.kind = Tok::Int;
        t.text = std::to_string(value);
        t.uval = value;
        m.body.push_back(t);
        macros_[name] = std::move(m);
    }

    std::vector<Token> run(const std::string& path) {
        auto ends = [&](const char* sfx) {
            std::string s(sfx);
            return path.size() >= s.size() && path.compare(path.size() - s.size(), s.size(), s) == 0;
        };
        if (ends(".cpp") || ends(".cc") || ends(".hpp") || ends(".cxx")) define("__cplusplus", 201703);  // C++17
        if (!pushFile(path, nullptr)) {
            diags_.error(path, 1, 1, "cannot open " + path);
            return {};
        }
        lineStart_ = true;
        std::vector<Token> out;
        for (;;) {
            Token t = nextAfterDirectives();
            if (t.kind == Tok::End) break;
            if (t.kind == Tok::Newline) {
                out.push_back(std::move(t));
                continue;
            }
            // The step limit guards one expansion and its rescans, not the whole file: a token
            // read straight from the source starts a new count.
            if (!t.fromExpansion) steps_ = 0;
            Token e = expandToken(std::move(t));
            if (e.kind == Tok::End || e.kind == Tok::Placemarker) continue;
            if (e.kind == Tok::Newline) {
                out.push_back(std::move(e));
                continue;
            }
            out.push_back(std::move(e));
        }
        if (!conds_.empty()) {
            const Token& at = directiveAt_;
            diags_.error(at.file.empty() ? path : at.file, at.line ? at.line : 1, at.column ? at.column : 1,
                         "unclosed #if");
        }
        return out;
    }

private:
    struct Frame {
        std::unique_ptr<Lexer> lexer;
        std::string file;
    };
    struct Cond {
        bool parentActive = true;
        bool anyTaken = false;
        bool active = true;
        bool sawElse = false;
    };

    FileStore& files_;
    Diagnostics& diags_;
    std::vector<std::string> includes_;
    std::vector<Frame> stack_;
    std::deque<Token> pending_;
    std::map<std::string, Macro> macros_;
    std::vector<Cond> conds_;
    std::set<std::string> once_;
    bool isolate_ = false;
    bool lineStart_ = true;
    int steps_ = 0;
    int nest_ = 0;
    Token directiveAt_;

    bool active() const { return conds_.empty() || conds_.back().active; }

    bool isDefinedName(const std::string& name) const {
        if (name == "__LINE__" || name == "__FILE__") return true;
        return macros_.find(name) != macros_.end();
    }

    void unread(const std::vector<Token>& toks) {
        for (std::size_t i = toks.size(); i-- > 0;) pending_.push_front(toks[i]);
    }
    void unreadOne(Token t) { pending_.push_front(std::move(t)); }

    Token endTok() const {
        Token t;
        t.kind = Tok::End;
        return t;
    }

    Token nextRaw() {
        if (!pending_.empty()) {
            Token t = std::move(pending_.front());
            pending_.pop_front();
            return t;
        }
        if (isolate_) return endTok();
        while (!stack_.empty()) {
            Token t = stack_.back().lexer->next();
            if (t.kind != Tok::End) {
                if (t.file.empty()) t.file = stack_.back().file;
                return t;
            }
            stack_.pop_back();
        }
        return endTok();
    }

    Token nextAfterDirectives() {
        for (;;) {
            Token t = nextRaw();
            if (t.kind == Tok::End) return t;
            if (!t.fromExpansion && lineStart_ && isHashTok(t)) {
                directiveAt_ = t;
                handleDirective();
                lineStart_ = true;
                continue;
            }
            if (t.kind == Tok::Newline) {
                lineStart_ = true;
                if (!active()) continue;
                return t;
            }
            lineStart_ = false;
            if (!active()) continue;
            return t;
        }
    }

    Token nextForExpand() {
        if (!isolate_) return nextAfterDirectives();
        if (pending_.empty()) return endTok();
        Token t = std::move(pending_.front());
        pending_.pop_front();
        return t;
    }

    void skipRestOfLine() {
        for (;;) {
            Token t = nextRaw();
            if (t.kind == Tok::Newline || t.kind == Tok::End) return;
        }
    }

    std::vector<Token> restOfLine() {
        std::vector<Token> v;
        for (;;) {
            Token t = nextRaw();
            if (t.kind == Tok::Newline || t.kind == Tok::End) break;
            v.push_back(std::move(t));
        }
        return v;
    }

    void handleDirective() {
        bool live = active();
        Token name = nextRaw();
        if (name.kind == Tok::Newline || name.kind == Tok::End) return;
        if (!name.isName()) {
            if (live) diags_.error(name.file, name.line, name.column, "invalid preprocessing directive");
            if (name.kind != Tok::Newline && name.kind != Tok::End) skipRestOfLine();
            return;
        }
        const std::string& n = name.text;
        if (n == "if") doIf(live, 0);
        else if (n == "ifdef") doIf(live, 1);
        else if (n == "ifndef") doIf(live, 2);
        else if (n == "elif") doElif();
        else if (n == "else") doElse();
        else if (n == "endif") doEndif();
        else if (!live) skipRestOfLine();
        else if (n == "define") doDefine();
        else if (n == "undef") doUndef();
        else if (n == "include") doInclude();
        else if (n == "error") doError();
        else if (n == "pragma") doPragma();
        else {
            diags_.error(name.file, name.line, name.column, "unsupported directive #" + n);
            skipRestOfLine();
        }
    }

    void doPragma() {
        std::vector<Token> line = restOfLine();
        if (!line.empty() && line[0].isName() && line[0].text == "once") {
            if (!stack_.empty()) once_.insert(normalizePath(stack_.back().file));
            return;
        }
        std::string text = "ignoring unknown pragma";
        if (!line.empty()) {
            text += " ";
            text += line[0].text;
        }
        const Token& at = line.empty() ? directiveAt_ : line[0];
        diags_.warning(at.file.empty() ? directiveAt_.file : at.file, at.line, at.column, text);
    }

    void doIf(bool live, int kind) {
        bool cond = false;
        if (live) {
            std::vector<Token> line = restOfLine();
            if (kind == 0) {
                bool v = false;
                if (evalIf(line, v)) cond = v;
            } else {
                if (line.size() != 1 || !line[0].isName()) {
                    errorAt(directiveAt_, "#ifdef needs one identifier");
                } else {
                    cond = isDefinedName(line[0].text);
                    if (kind == 2) cond = !cond;
                }
            }
        } else {
            skipRestOfLine();
        }
        Cond c;
        c.parentActive = live;
        c.anyTaken = live && cond;
        c.active = live && cond;
        conds_.push_back(c);
    }

    void doElif() {
        if (conds_.empty() || conds_.back().sawElse) {
            errorAt(directiveAt_, "#elif without #if");
            skipRestOfLine();
            return;
        }
        Cond& c = conds_.back();
        if (c.parentActive && !c.anyTaken) {
            std::vector<Token> line = restOfLine();
            bool v = false;
            if (evalIf(line, v)) {
                c.active = v;
                if (v) c.anyTaken = true;
            } else {
                c.active = false;
            }
        } else {
            skipRestOfLine();
            c.active = false;
        }
    }

    void doElse() {
        std::vector<Token> extra = restOfLine();
        if (conds_.empty() || conds_.back().sawElse) {
            errorAt(directiveAt_, "#else without #if");
            return;
        }
        Cond& c = conds_.back();
        if (!extra.empty()) errorAt(extra[0], "extra tokens after #else");
        c.sawElse = true;
        if (c.parentActive && !c.anyTaken) {
            c.active = true;
            c.anyTaken = true;
        } else {
            c.active = false;
        }
    }

    void doEndif() {
        std::vector<Token> extra = restOfLine();
        if (conds_.empty()) {
            errorAt(directiveAt_, "#endif without #if");
            return;
        }
        if (!extra.empty()) errorAt(extra[0], "extra tokens after #endif");
        conds_.pop_back();
    }

    void doError() {
        std::vector<Token> line = restOfLine();
        std::string msg;
        for (const Token& t : line) {
            if (t.leadSpace && !msg.empty()) msg.push_back(' ');
            msg += t.text;
        }
        if (msg.empty()) msg = "#error";
        errorAt(directiveAt_, msg);
    }

    void doUndef() {
        std::vector<Token> line = restOfLine();
        if (line.size() != 1 || !line[0].isName()) {
            errorAt(directiveAt_, "#undef needs one identifier");
            return;
        }
        const std::string& n = line[0].text;
        if (n == "defined" || n == "__LINE__" || n == "__FILE__") {
            errorAt(line[0], "cannot #undef \"" + n + "\"");
            return;
        }
        macros_.erase(n);
    }

    void doDefine() {
        Token name = nextRaw();
        if (!name.isName()) {
            errorAt(name.kind == Tok::End ? directiveAt_ : name, "#define needs a name");
            if (name.kind != Tok::Newline && name.kind != Tok::End) skipRestOfLine();
            return;
        }
        if (name.text == "defined" || name.text == "__LINE__" || name.text == "__FILE__") {
            errorAt(name, "cannot #define \"" + name.text + "\"");
            if (name.kind != Tok::Newline) skipRestOfLine();
            return;
        }
        Macro m;
        m.name = name.text;
        m.line = name.line;
        m.file = name.file;
        Token nxt = nextRaw();
        bool func = punct(nxt, "(") && !nxt.leadSpace;
        bool bad = false;
        if (func) {
            m.functionLike = true;
            bad = !parseParams(m);
            if (!bad) {
                for (;;) {
                    Token t = nextRaw();
                    if (t.kind == Tok::Newline || t.kind == Tok::End) break;
                    m.body.push_back(std::move(t));
                }
            }
        } else if (nxt.kind != Tok::Newline && nxt.kind != Tok::End) {
            m.body.push_back(std::move(nxt));
            for (;;) {
                Token t = nextRaw();
                if (t.kind == Tok::Newline || t.kind == Tok::End) break;
                m.body.push_back(std::move(t));
            }
        }
        if (bad) return;
        if (!m.body.empty()) m.body.front().leadSpace = false;
        if (!checkBody(m)) return;
        auto it = macros_.find(m.name);
        if (it != macros_.end()) {
            if (!sameMacro(it->second, m)) {
                errorAt(name, "macro \"" + m.name + "\" redefined");
            }
            return;
        }
        macros_.emplace(m.name, std::move(m));
    }

    bool parseParams(Macro& m) {
        Token first = nextRaw();
        if (punct(first, ")")) return true;
        unreadOne(std::move(first));
        while (true) {
            Token a = nextRaw();
            if (punct(a, "...")) {
                m.variadic = true;
                Token c = nextRaw();
                if (!punct(c, ")")) {
                    errorAt(a, "expected ')' after ...");
                    if (c.kind != Tok::Newline && c.kind != Tok::End) skipRestOfLine();
                    return false;
                }
                return true;
            }
            if (!a.isName()) {
                errorAt(a.kind == Tok::End ? directiveAt_ : a, "expected a parameter name");
                if (a.kind != Tok::Newline && a.kind != Tok::End) skipRestOfLine();
                return false;
            }
            if (a.text == "__VA_ARGS__") {
                errorAt(a, "__VA_ARGS__ is not a parameter name");
                skipRestOfLine();
                return false;
            }
            for (const std::string& p : m.params) {
                if (p == a.text) {
                    errorAt(a, "duplicate parameter \"" + a.text + "\"");
                    skipRestOfLine();
                    return false;
                }
            }
            m.params.push_back(a.text);
            Token s = nextRaw();
            if (punct(s, ")")) return true;
            if (punct(s, ",")) continue;
            errorAt(s.kind == Tok::End ? a : s, "expected ',' or ')' in parameter list");
            if (s.kind != Tok::Newline && s.kind != Tok::End) skipRestOfLine();
            return false;
        }
    }

    int paramIndex(const Macro& m, const Token& t) const {
        if (!m.functionLike || !t.isName()) return -1;
        for (std::size_t i = 0; i < m.params.size(); ++i)
            if (m.params[i] == t.text) return static_cast<int>(i);
        if (m.variadic && t.text == "__VA_ARGS__") return static_cast<int>(m.params.size());
        return -1;
    }

    bool checkBody(const Macro& m) {
        if (m.body.empty()) return true;
        if (isPasteTok(m.body.front()) || isPasteTok(m.body.back())) {
            errorAt(m.body.front(), "## at the end of a macro body");
            return false;
        }
        for (std::size_t i = 0; i < m.body.size(); ++i) {
            if (!m.functionLike || !isHashTok(m.body[i]) || isPasteTok(m.body[i])) continue;
            if (i + 1 >= m.body.size() || paramIndex(m, m.body[i + 1]) < 0) {
                errorAt(m.body[i], "# must be followed by a parameter");
                return false;
            }
        }
        return true;
    }

    void doInclude() {
        Token n = nextRaw();
        if (n.kind == Tok::StringLit) {
            std::string header = n.rawInside.empty() ? n.decoded : n.rawInside;
            Token rest = nextRaw();
            if (rest.kind != Tok::Newline && rest.kind != Tok::End) {
                errorAt(rest, "extra tokens after #include");
                if (rest.kind != Tok::Newline) skipRestOfLine();
            }
            openInclude(header, true, n);
            return;
        }
        if (punct(n, "<")) {
            std::string header;
            for (;;) {
                Token p = nextRaw();
                if (p.kind == Tok::Newline || p.kind == Tok::End) {
                    errorAt(n, "unterminated #include <...>");
                    return;
                }
                if (punct(p, ">")) break;
                if (p.leadSpace && !header.empty()) header.push_back(' ');
                header += p.text;
            }
            Token rest = nextRaw();
            if (rest.kind != Tok::Newline && rest.kind != Tok::End) {
                errorAt(rest, "extra tokens after #include");
                if (rest.kind != Tok::Newline) skipRestOfLine();
            }
            openInclude(header, false, n);
            return;
        }
        std::vector<Token> toks;
        if (n.kind != Tok::Newline && n.kind != Tok::End) toks.push_back(std::move(n));
        for (;;) {
            Token p = nextRaw();
            if (p.kind == Tok::Newline || p.kind == Tok::End) break;
            toks.push_back(std::move(p));
        }
        steps_ = 0;  // a directive line is one expansion
        std::vector<Token> exp = expandList(std::move(toks));
        if (exp.size() == 1 && exp[0].kind == Tok::StringLit) {
            std::string header = exp[0].rawInside.empty() ? exp[0].decoded : exp[0].rawInside;
            openInclude(header, true, exp[0]);
            return;
        }
        if (!exp.empty() && punct(exp[0], "<")) {
            std::string header;
            bool closed = false;
            for (std::size_t i = 1; i < exp.size(); ++i) {
                if (punct(exp[i], ">")) {
                    closed = true;
                    if (i + 1 != exp.size()) errorAt(exp[i + 1], "extra tokens after #include");
                    break;
                }
                if (exp[i].leadSpace && !header.empty()) header.push_back(' ');
                header += exp[i].text;
            }
            if (!closed) {
                errorAt(directiveAt_, "macro in #include did not expand to a header name");
                return;
            }
            openInclude(header, false, exp[0]);
            return;
        }
        errorAt(directiveAt_, "macro in #include did not expand to a header name");
    }

    void openInclude(const std::string& header, bool quote, const Token& at) {
        if (header.empty()) {
            errorAt(at, "empty header name");
            return;
        }
        std::string found = findInclude(header, quote);
        if (found.empty()) {
            errorAt(at, "cannot find header \"" + header + "\"");
            return;
        }
        if (static_cast<int>(stack_.size()) >= kMaxIncludeDepth) {
            errorAt(at, "include nested too deeply");
            return;
        }
        if (once_.count(normalizePath(found))) return;
        if (!pushFile(found, &at)) errorAt(at, "cannot find header \"" + header + "\"");
    }

    std::string findInclude(const std::string& header, bool quote) const {
        auto exists = [&](const std::string& path) {
            std::string tmp;
            std::string n = normalizePath(path);
            return files_.read(n, tmp) ? n : std::string();
        };
        if (quote && !stack_.empty()) {
            std::string hit = exists(joinPath(dirOf(stack_.back().file), header));
            if (!hit.empty()) return hit;
        }
        for (const std::string& inc : includes_) {
            std::string hit = exists(joinPath(inc, header));
            if (!hit.empty()) return hit;
        }
        return {};
    }

    bool pushFile(const std::string& path, const Token* at) {
        std::string contents;
        std::string n = normalizePath(path);
        if (!files_.read(n, contents)) return false;
        // A file that does not end in a new-line is read as if it did ([lex.phases] p1.2), so the
        // line after its #include in the includer is still a directive.
        if (!contents.empty() && contents.back() != '\n') contents += '\n';
        stack_.push_back(Frame{std::make_unique<Lexer>(n, std::move(contents), diags_), n});
        (void)at;
        return true;
    }

    bool evalIf(std::vector<Token> line, bool& value) {
        line = resolveDefined(std::move(line));
        steps_ = 0;  // a directive line is one expansion
        line = expandList(std::move(line));
        for (Token& t : line) {
            if (!t.isName()) continue;
            if (t.text == "defined") {
                errorAt(t, "defined after macro expansion is not allowed");
                return false;
            }
            t = makeNum(0, t);
        }
        IfEval ev(line, directiveAt_, diags_);
        return ev.run(value);
    }

    std::vector<Token> resolveDefined(std::vector<Token> in) {
        std::vector<Token> out;
        for (std::size_t i = 0; i < in.size();) {
            if (in[i].isName() && in[i].text == "defined") {
                if (i + 1 < in.size() && in[i + 1].isName()) {
                    out.push_back(makeNum(isDefinedName(in[i + 1].text) ? 1 : 0, in[i]));
                    i += 2;
                    continue;
                }
                if (i + 3 < in.size() && punct(in[i + 1], "(") && in[i + 2].isName() && punct(in[i + 3], ")")) {
                    out.push_back(makeNum(isDefinedName(in[i + 2].text) ? 1 : 0, in[i]));
                    i += 4;
                    continue;
                }
                errorAt(in[i], "defined needs a macro name");
                ++i;
                continue;
            }
            out.push_back(std::move(in[i]));
            ++i;
        }
        return out;
    }

    Token expandSpecial(const Token& t) {
        if (t.text == "__LINE__") return makeNum(static_cast<std::uint64_t>(t.line), t);
        std::string body;
        for (char c : t.file) {
            if (c == '\\' || c == '"') body.push_back('\\');
            body.push_back(c);
        }
        Token n = t;
        n.kind = Tok::StringLit;
        n.text = "\"" + body + "\"";
        n.decoded = t.file;
        n.fromExpansion = true;
        return n;
    }

    Token expandToken(Token t) {
        for (;;) {
            if (++steps_ > kMaxExpandSteps) {
                errorAt(t, "macro expansion is too long");
                return t;
            }
            if (!t.isName()) return t;
            if (hidden(t, t.text)) return t;
            if (t.text == "__LINE__" || t.text == "__FILE__") return expandSpecial(t);
            auto it = macros_.find(t.text);
            if (it == macros_.end()) return t;
            const Macro& m = it->second;
            std::vector<std::vector<Token>> args;
            if (m.functionLike) {
                if (!peekLParen()) return t;
                bool ok = true;
                args = collectArgs(m, ok);
                if (!ok) {
                    Token skip;
                    skip.kind = Tok::Placemarker;
                    return skip;
                }
            }
            if (++nest_ > kMaxExpandNest) {
                --nest_;
                errorAt(t, "macro nested too deeply");
                return t;
            }
            std::vector<Token> repl = substitute(m, args, t);
            --nest_;
            for (Token& r : repl) r.fromExpansion = true;
            if (!repl.empty()) repl.front().leadSpace = t.leadSpace;
            unread(repl);
            t = nextForExpand();
            if (t.kind == Tok::End || t.kind == Tok::Newline) return t;
        }
    }

    bool peekLParen() {
        bool savedLine = lineStart_;
        std::vector<Token> skipped;
        for (;;) {
            Token n = nextForExpand();
            if (n.kind == Tok::End) {
                unread(skipped);
                lineStart_ = savedLine;
                return false;
            }
            if (n.kind == Tok::Newline) {
                skipped.push_back(std::move(n));
                continue;
            }
            if (punct(n, "(")) return true;
            skipped.push_back(std::move(n));
            unread(skipped);
            lineStart_ = savedLine;
            return false;
        }
    }

    Token takeArg() {
        bool space = false;
        for (;;) {
            Token n = nextForExpand();
            if (n.kind == Tok::Newline) {
                space = true;
                continue;
            }
            if (space && n.kind != Tok::End) n.leadSpace = true;
            return n;
        }
    }

    std::vector<std::vector<Token>> collectArgs(const Macro& m, bool& ok) {
        // The opening '(' was already consumed. Commas split arguments only at
        // parenthesis depth 0, and only before the variadic argument starts.
        ok = true;
        std::vector<std::vector<Token>> args;
        std::vector<Token> cur;
        int depth = 0;
        int complete = 0;
        bool any = false;
        while (true) {
            Token n = takeArg();
            if (n.kind == Tok::End) {
                errorAt(directiveAt_, "unterminated macro invocation");
                ok = false;
                return {};
            }
            if (punct(n, "(")) {
                ++depth;
                any = true;
                cur.push_back(std::move(n));
                continue;
            }
            if (punct(n, ")") && depth > 0) {
                --depth;
                any = true;
                cur.push_back(std::move(n));
                continue;
            }
            if (punct(n, ")") && depth == 0) {
                if (!any && complete == 0) {
                    if (m.params.empty() && !m.variadic) return {};
                    if (m.variadic && m.params.empty()) return {{}};
                    args.push_back({});
                } else {
                    args.push_back(std::move(cur));
                }
                break;
            }
            if (punct(n, ",") && depth == 0 &&
                (!m.variadic || complete < static_cast<int>(m.params.size()))) {
                args.push_back(std::move(cur));
                cur.clear();
                ++complete;
                any = true;
                continue;
            }
            any = true;
            cur.push_back(std::move(n));
        }
        if (!finishArgCount(m, args)) ok = false;
        return args;
    }

    bool finishArgCount(const Macro& m, std::vector<std::vector<Token>>& args) {
        if (m.variadic) {
            if (static_cast<int>(args.size()) < static_cast<int>(m.params.size())) {
                errorAt(directiveAt_, "wrong number of arguments for macro \"" + m.name + "\"");
                return false;
            }
            if (args.size() == m.params.size()) args.push_back({});
            if (args.size() > m.params.size() + 1) {
                // Variadic commas stay inside the last argument; this is a logic error.
                errorAt(directiveAt_, "wrong number of arguments for macro \"" + m.name + "\"");
                return false;
            }
            return true;
        }
        if (args.size() != m.params.size()) {
            errorAt(directiveAt_, "wrong number of arguments for macro \"" + m.name + "\"");
            return false;
        }
        return true;
    }

    std::vector<Token> expandList(std::vector<Token> in) {
        if (++nest_ > kMaxExpandNest) {
            --nest_;
            errorAt(in.empty() ? directiveAt_ : in.front(), "macro nested too deeply");
            return in;
        }
        auto savedPending = std::move(pending_);
        bool savedIso = isolate_;
        bool savedLine = lineStart_;
        pending_.clear();
        isolate_ = true;
        for (Token& t : in) pending_.push_back(std::move(t));
        std::vector<Token> out;
        while (!pending_.empty()) {
            Token t = std::move(pending_.front());
            pending_.pop_front();
            Token e = expandToken(std::move(t));
            if (e.kind == Tok::End) {
                if (pending_.empty()) break;
                continue;
            }
            if (e.kind == Tok::Newline || e.kind == Tok::Placemarker) continue;
            out.push_back(std::move(e));
        }
        pending_ = std::move(savedPending);
        isolate_ = savedIso;
        lineStart_ = savedLine;
        --nest_;
        return out;
    }

    Token placemarkerAt(const Token& at) const {
        Token t = at;
        t.kind = Tok::Placemarker;
        t.text.clear();
        t.fromExpansion = true;
        return t;
    }

    void addHide(Token& t, const Token& invoc, const std::string& name) const {
        for (const std::string& h : invoc.hide) {
            if (!hidden(t, h)) t.hide.push_back(h);
        }
        if (!hidden(t, name)) t.hide.push_back(name);
        t.fromExpansion = true;
    }

    Token retarget(Token body, const Token& invoc, const std::string& name) const {
        body.line = invoc.line;
        body.column = invoc.column;
        body.file = invoc.file;
        addHide(body, invoc, name);
        return body;
    }

    Token stringize(const std::vector<Token>& arg, const Token& at, const Token& invoc,
                    const std::string& name) const {
        std::string body;
        bool first = true;
        for (const Token& t : arg) {
            if (t.kind == Tok::Placemarker) continue;
            if (!first && t.leadSpace) body.push_back(' ');
            first = false;
            for (unsigned char c : t.text) {
                if (c == '\\' || c == '"') body.push_back('\\');
                body.push_back(static_cast<char>(c));
            }
        }
        Token s = at;
        s.kind = Tok::StringLit;
        s.text = "\"" + body + "\"";
        s.decoded = body;
        s.line = invoc.line;
        s.column = invoc.column;
        s.file = invoc.file;
        addHide(s, invoc, name);
        return s;
    }

    Token paste(const Token& a, const Token& b) {
        if (a.kind == Tok::Placemarker && b.kind == Tok::Placemarker) return a;
        if (a.kind == Tok::Placemarker) {
            Token r = b;
            r.leadSpace = a.leadSpace;
            return r;
        }
        if (b.kind == Tok::Placemarker) return a;
        std::string spelling = a.text + b.text;
        Diagnostics tmp;
        Lexer lx("<paste>", spelling, tmp);
        Token t = lx.next();
        Token n = lx.next();
        if (!tmp.ok() || t.kind == Tok::End || t.kind == Tok::Newline || n.kind != Tok::End) {
            diags_.error(a.file, a.line, a.column,
                         "pasting \"" + a.text + "\" and \"" + b.text + "\" does not give a valid preprocessing token");
            Token bad = a;
            bad.text = spelling;
            bad.kind = Tok::Ident;
            bad.fromExpansion = true;
            return bad;
        }
        t.line = a.line;
        t.column = a.column;
        t.file = a.file;
        t.leadSpace = a.leadSpace;
        t.hide = a.hide;
        for (const std::string& h : b.hide) t.hide.push_back(h);
        t.fromExpansion = true;
        return t;
    }

    std::vector<Token> substitute(const Macro& m, const std::vector<std::vector<Token>>& args, const Token& invoc) {
        const int vaIndex = m.variadic ? static_cast<int>(m.params.size()) : -1;
        auto idxOf = [&](const Token& t) { return paramIndex(m, t); };
        std::vector<char> need(args.size(), 0);
        for (std::size_t i = 0; i < m.body.size(); ++i) {
            int idx = idxOf(m.body[i]);
            if (idx < 0) continue;
            bool prevPaste = i > 0 && isPasteTok(m.body[i - 1]);
            bool nextPaste = i + 1 < m.body.size() && isPasteTok(m.body[i + 1]);
            bool prevHash = i > 0 && isHashTok(m.body[i - 1]) && !isPasteTok(m.body[i - 1]);
            if (!prevPaste && !nextPaste && !prevHash && static_cast<std::size_t>(idx) < need.size())
                need[static_cast<std::size_t>(idx)] = 1;
        }
        std::vector<std::vector<Token>> expanded(args.size());
        for (std::size_t i = 0; i < args.size(); ++i) {
            if (!need[i]) continue;
            expanded[i] = expandList(args[i]);
            for (Token& t : expanded[i]) addHide(t, invoc, m.name);
        }
        std::vector<Token> mid;
        for (std::size_t i = 0; i < m.body.size(); ++i) {
            if (m.functionLike && isHashTok(m.body[i]) && !isPasteTok(m.body[i])) {
                if (i + 1 >= m.body.size()) break;
                int idx = idxOf(m.body[i + 1]);
                ++i;
                if (idx < 0 || static_cast<std::size_t>(idx) >= args.size()) {
                    errorAt(m.body[i], "# is not followed by a parameter");
                    continue;
                }
                mid.push_back(stringize(args[static_cast<std::size_t>(idx)], m.body[i], invoc, m.name));
                continue;
            }
            if (isPasteTok(m.body[i])) {
                mid.push_back(retarget(m.body[i], invoc, m.name));
                continue;
            }
            int idx = idxOf(m.body[i]);
            if (idx >= 0 && static_cast<std::size_t>(idx) < args.size()) {
                bool prevPaste = !mid.empty() && isPasteTok(mid.back());
                bool nextPaste = i + 1 < m.body.size() && isPasteTok(m.body[i + 1]);
                const std::vector<Token>& src = (prevPaste || nextPaste) ? args[static_cast<std::size_t>(idx)]
                                                                         : expanded[static_cast<std::size_t>(idx)];
                if (src.empty()) {
                    if (prevPaste || nextPaste) mid.push_back(placemarkerAt(invoc));
                } else {
                    for (Token t : src) {
                        addHide(t, invoc, m.name);
                        mid.push_back(std::move(t));
                    }
                }
                (void)vaIndex;
                continue;
            }
            mid.push_back(retarget(m.body[i], invoc, m.name));
        }
        std::vector<Token> out;
        bool pasteNext = false;
        for (const Token& t : mid) {
            if (isPasteTok(t)) {
                if (out.empty() || pasteNext) {
                    errorAt(invoc, "## at the end of a macro body");
                    pasteNext = false;
                    continue;
                }
                pasteNext = true;
                continue;
            }
            if (pasteNext) {
                out.back() = paste(out.back(), t);
                pasteNext = false;
            } else {
                out.push_back(t);
            }
        }
        if (pasteNext) errorAt(invoc, "## at the end of a macro body");
        std::vector<Token> cleaned;
        for (Token& t : out) {
            if (t.kind != Tok::Placemarker) cleaned.push_back(std::move(t));
        }
        return cleaned;
    }

    void errorAt(const Token& t, const std::string& msg) { diags_.error(t.file, t.line, t.column, msg); }
};

}  // namespace

std::vector<Token> preprocessTokens(FileStore& files, const std::string& path,
                                    const std::vector<std::string>& includePaths, Diagnostics& diags) {
    Preprocessor pp(files, diags, includePaths);
    return pp.run(path);
}

std::string preprocess(FileStore& files, const std::string& path, const std::vector<std::string>& includePaths,
                       Diagnostics& diags) {
    return printTokens(preprocessTokens(files, path, includePaths, diags));
}

}  // namespace okaycc
