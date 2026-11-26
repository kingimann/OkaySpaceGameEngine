// okay::js regular expressions. Written from ECMA-262 22.2: the pattern grammar of
// 22.2.1 (disjunctions, alternatives, assertions ^ $ \b \B and lookahead, quantifiers
// greedy and lazy with {n,m}, atoms, escapes, character classes, capturing,
// non-capturing and named groups, backreferences), the flags of 22.2.3 (d g i m s u
// y; d is accepted, match indices are not made), 22.2.7.2 RegExpBuiltinExec with
// lastIndex, and the String methods that take a RegExp (22.1.3 match, matchAll,
// replace, replaceAll, search, split with 22.1.3.19.1 GetSubstitution).
//
// Design (our own): the pattern is parsed into a small tree, then compiled to a
// program for a backtracking machine with an explicit stack (no C++ recursion per
// character, so long inputs can not overflow the stack). A loop whose body matched
// the empty string stops (22.2.2.3.1 RepeatMatcher's empty check). Every step of the
// machine is charged to the run budget, so a pattern that backtracks exponentially
// ("catastrophic backtracking") stops with the budget error instead of freezing.
// Lookbehind (22.2.2.4 with direction backward) is compiled with its body reversed: its characters,
// classes and backreferences step to the left of the position and its groups save their end first.
#include "JsInternal.hpp"
#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace okay::js {

namespace {

enum RxOp : std::uint8_t {
    RX_Char, RX_Any, RX_Class, RX_Split, RX_Jmp, RX_Save, RX_Bol, RX_Eol, RX_WordB, RX_NotWordB, RX_Look,
    RX_BackRef, RX_Match, RX_LoopStart, RX_LoopCheck,
};

struct RxInst {
    RxOp op;
    std::uint32_t a = 0;   ///< char / class index / jump target / save slot / register
    std::uint32_t b = 0;   ///< second target / negate flag
};

struct RxClass {
    std::vector<std::pair<std::uint32_t, std::uint32_t>> ranges;
    bool negate = false;
};

} // namespace

struct RegexProg {
    std::vector<RxInst> code;
    std::vector<RxClass> classes;
    std::vector<std::u16string> names;   ///< group names by group index ("" = unnamed)
    int groups = 1;                      ///< including group 0
    int regs = 0;                        ///< loop registers
    bool icase = false, multiline = false, dotAll = false, unicode = false, global = false, sticky = false;
    int firstChar = -1;                  ///< a literal first character to scan for, or -1
};

namespace {

// ---- the pattern tree
enum RxKind : std::uint8_t { NK_Empty, NK_Char, NK_Any, NK_Class, NK_Seq, NK_Alt, NK_Group, NK_Repeat, NK_Bol, NK_Eol, NK_WordB, NK_Look, NK_BackRef };

struct RxNode {
    RxKind k;
    std::uint32_t c = 0;      ///< char, class index, group index, backref number
    int min = 0, max = 0;     ///< repeat bounds (max -1 = no limit)
    bool flag = false;        ///< greedy / negate / capturing
    std::u16string name;      ///< a named backreference before it is resolved
    std::vector<std::unique_ptr<RxNode>> kids;
};

struct RxFail {
    std::string why;
};

constexpr std::size_t kMaxProgram = 60000;

struct PatternParser {
    const std::u16string& p;
    std::size_t i = 0;
    RegexProg& prog;
    int depth = 0;
    std::vector<std::pair<RxNode*, std::u16string>> namedRefs;

    PatternParser(const std::u16string& s, RegexProg& pr) : p(s), prog(pr) {}

    [[noreturn]] void Fail(const std::string& m) { throw RxFail{m}; }
    bool More() const { return i < p.size(); }
    char16_t Peek(std::size_t k = 0) const { return i + k < p.size() ? p[i + k] : 0; }
    bool Eat(char16_t c) {
        if (Peek() != c || !More()) return false;
        ++i;
        return true;
    }
    std::unique_ptr<RxNode> Node(RxKind k) {
        auto n = std::make_unique<RxNode>();
        n->k = k;
        return n;
    }

    std::unique_ptr<RxNode> Disjunction() {
        if (++depth > 200) Fail("the regular expression is nested too deeply");
        auto first = Alternative();
        if (Peek() != u'|') { --depth; return first; }
        auto alt = Node(NK_Alt);
        alt->kids.push_back(std::move(first));
        while (Eat(u'|')) alt->kids.push_back(Alternative());
        --depth;
        return alt;
    }

    std::unique_ptr<RxNode> Alternative() {
        auto seq = Node(NK_Seq);
        while (More() && Peek() != u'|' && Peek() != u')') {
            auto t = Term();
            if (t) seq->kids.push_back(std::move(t));
        }
        return seq;
    }

    std::uint32_t ReadCodePoint() {   // one pattern character, with the u flag a whole surrogate pair
        char16_t c = p[i++];
        if (prog.unicode && c >= 0xD800 && c <= 0xDBFF && More() && p[i] >= 0xDC00 && p[i] <= 0xDFFF)
            return 0x10000 + ((c - 0xD800) << 10) + (p[i++] - 0xDC00);
        return c;
    }

    bool Digits(int& out) {
        std::size_t s = i;
        long long v = 0;
        while (More() && Peek() >= u'0' && Peek() <= u'9') {
            v = v * 10 + (Peek() - u'0');
            if (v > 100000) v = 100000;
            ++i;
        }
        out = (int)v;
        return i > s;
    }

    std::unique_ptr<RxNode> Term() {
        std::size_t start = i;
        char16_t c = Peek();
        std::unique_ptr<RxNode> atom;
        if (c == u'^') { ++i; return Node(NK_Bol); }
        if (c == u'$') { ++i; return Node(NK_Eol); }
        if (c == u'\\' && (Peek(1) == u'b' || Peek(1) == u'B')) {
            auto n = Node(NK_WordB);
            n->flag = Peek(1) == u'B';
            i += 2;
            return n;
        }
        if (c == u'(' && Peek(1) == u'?' && (Peek(2) == u'=' || Peek(2) == u'!')) {
            bool neg = Peek(2) == u'!';
            i += 3;
            auto n = Node(NK_Look);
            n->flag = neg;
            n->kids.push_back(Disjunction());
            if (!Eat(u')')) Fail("a lookahead is missing its ')'");
            atom = std::move(n);   // a lookahead may be quantified in Annex B; we treat it as an atom
        } else if (c == u'(' && Peek(1) == u'?' && Peek(2) == u'<' && (Peek(3) == u'=' || Peek(3) == u'!')) {
            bool neg = Peek(3) == u'!';
            i += 4;
            auto n = Node(NK_Look);
            n->flag = neg;
            n->min = 1;   // backward
            n->kids.push_back(Disjunction());
            if (!Eat(u')')) Fail("a lookbehind is missing its ')'");
            return n;   // 22.2.1: a lookbehind can not be quantified
        } else if (c == u'(') {
            ++i;
            auto g = Node(NK_Group);
            if (Eat(u'?')) {
                if (Eat(u':')) g->flag = false;
                else if (Eat(u'<')) {
                    std::u16string name;
                    while (More() && Peek() != u'>') name += p[i++];
                    if (!Eat(u'>') || name.empty()) Fail("a group name must be written (?<name>...)");
                    for (const auto& n : prog.names)
                        if (n == name) Fail("the group name '" + Utf16To8(name) + "' is used twice");
                    g->flag = true;
                    g->c = (std::uint32_t)prog.groups++;
                    prog.names.resize((std::size_t)prog.groups);
                    prog.names[g->c] = name;
                } else Fail("unknown group kind after '(?'");
            } else {
                g->flag = true;
                g->c = (std::uint32_t)prog.groups++;
                prog.names.resize((std::size_t)prog.groups);
            }
            g->kids.push_back(Disjunction());
            if (!Eat(u')')) Fail("a group is missing its ')'");
            atom = std::move(g);
        } else if (c == u'.') {
            ++i;
            atom = Node(NK_Any);
        } else if (c == u'[') {
            ++i;
            atom = Node(NK_Class);
            atom->c = ClassBody();
        } else if (c == u'\\') {
            ++i;
            atom = AtomEscape();
        } else if (c == u'*' || c == u'+' || c == u'?' || (c == u'{' && QuantifierAhead())) {
            Fail("nothing to repeat before '" + Utf16To8(std::u16string(1, c)) + "'");
        } else if (c == u')' ) {
            Fail("unmatched ')'");
        } else {
            if ((c == u']' || c == u'{' || c == u'}') && prog.unicode) Fail("a lone '" + Utf16To8(std::u16string(1, c)) + "' needs a backslash with the u flag");
            atom = Node(NK_Char);
            atom->c = ReadCodePoint();
        }
        (void)start;
        return Quantified(std::move(atom));
    }

    bool QuantifierAhead() const {   // '{' digits [, [digits]] '}'
        std::size_t k = i + 1;
        bool d = false;
        while (k < p.size() && p[k] >= u'0' && p[k] <= u'9') { ++k; d = true; }
        if (!d) return false;
        if (k < p.size() && p[k] == u',') {
            ++k;
            while (k < p.size() && p[k] >= u'0' && p[k] <= u'9') ++k;
        }
        return k < p.size() && p[k] == u'}';
    }

    std::unique_ptr<RxNode> Quantified(std::unique_ptr<RxNode> atom) {
        int mn, mx;
        char16_t c = Peek();
        if (c == u'*') { mn = 0; mx = -1; ++i; }
        else if (c == u'+') { mn = 1; mx = -1; ++i; }
        else if (c == u'?') { mn = 0; mx = 1; ++i; }
        else if (c == u'{' && QuantifierAhead()) {
            ++i;
            Digits(mn);
            mx = mn;
            if (Eat(u',')) {
                if (!Digits(mx)) mx = -1;
            }
            Eat(u'}');
            if (mx != -1 && mx < mn) Fail("the numbers in {n,m} are out of order");
        } else return atom;
        if (atom->k == NK_Bol || atom->k == NK_Eol || atom->k == NK_WordB) Fail("an assertion can not be repeated");
        auto r = Node(NK_Repeat);
        r->min = mn;
        r->max = mx;
        r->flag = !Eat(u'?');   // greedy unless followed by ?
        r->kids.push_back(std::move(atom));
        return r;
    }

    int Hex(char16_t c) const {
        if (c >= u'0' && c <= u'9') return c - u'0';
        if (c >= u'a' && c <= u'f') return c - u'a' + 10;
        if (c >= u'A' && c <= u'F') return c - u'A' + 10;
        return -1;
    }

    // \d \D \s \S \w \W as ranges (into `cls`); false when c is not one of them.
    bool ClassEscape(char16_t c, RxClass& cls) {
        std::vector<std::pair<std::uint32_t, std::uint32_t>> r;
        char16_t lower = c | 0x20;
        if (lower == u'd') r = {{'0', '9'}};
        else if (lower == u'w') r = {{'0', '9'}, {'A', 'Z'}, {'_', '_'}, {'a', 'z'}};
        else if (lower == u's') {
            for (std::uint32_t x : {0x09u, 0x0Au, 0x0Bu, 0x0Cu, 0x0Du, 0x20u, 0xA0u, 0x1680u, 0x2028u, 0x2029u, 0x202Fu, 0x205Fu, 0x3000u, 0xFEFFu})
                r.push_back({x, x});
            r.push_back({0x2000, 0x200A});
            std::sort(r.begin(), r.end());
        } else return false;
        if (c >= u'A' && c <= u'Z') {   // the complement
            std::vector<std::pair<std::uint32_t, std::uint32_t>> comp;
            std::uint32_t next = 0;
            for (auto& x : r) {
                if (x.first > next) comp.push_back({next, x.first - 1});
                next = x.second + 1;
            }
            comp.push_back({next, 0x10FFFF});
            r = comp;
        }
        cls.ranges.insert(cls.ranges.end(), r.begin(), r.end());
        return true;
    }

    // A CharacterEscape after '\' (i is at the letter): the code point it stands for.
    std::uint32_t CharEscape(bool inClass) {
        char16_t c = p[i++];
        switch (c) {
            case u't': return 9;
            case u'n': return 10;
            case u'v': return 11;
            case u'f': return 12;
            case u'r': return 13;
            case u'0':
                if (More() && Peek() >= u'0' && Peek() <= u'9') Fail("octal escapes are not allowed in a regular expression");
                return 0;
            case u'c': {
                char16_t l = Peek();
                if ((l >= u'a' && l <= u'z') || (l >= u'A' && l <= u'Z')) { ++i; return (std::uint32_t)(l % 32); }
                Fail("\\c must be followed by a letter");
            }
            case u'x': {
                if (Hex(Peek()) < 0 || Hex(Peek(1)) < 0) Fail("\\x needs two hex digits");
                std::uint32_t v = (std::uint32_t)(Hex(Peek()) * 16 + Hex(Peek(1)));
                i += 2;
                return v;
            }
            case u'u': {
                if (Peek() == u'{' && prog.unicode) {
                    ++i;
                    std::uint32_t v = 0;
                    int n = 0;
                    while (More() && Peek() != u'}') {
                        int h = Hex(p[i++]);
                        if (h < 0) Fail("bad \\u{...} escape");
                        v = v * 16 + (std::uint32_t)h;
                        if (v > 0x10FFFF) Fail("\\u{...} is past the last code point");
                        ++n;
                    }
                    if (!Eat(u'}') || !n) Fail("bad \\u{...} escape");
                    return v;
                }
                std::uint32_t v = 0;
                for (int k = 0; k < 4; ++k) {
                    int h = Hex(Peek(k));
                    if (h < 0) Fail("\\u needs four hex digits");
                    v = v * 16 + (std::uint32_t)h;
                }
                i += 4;
                return v;
            }
            default:
                if (c == u'-' && inClass) return u'-';
                if (prog.unicode && !(c == u'^' || c == u'$' || c == u'\\' || c == u'.' || c == u'*' || c == u'+' || c == u'?' ||
                                      c == u'(' || c == u')' || c == u'[' || c == u']' || c == u'{' || c == u'}' || c == u'|' || c == u'/'))
                    Fail("unknown escape with the u flag");
                --i;
                return ReadCodePoint();   // an identity escape
        }
    }

    std::unique_ptr<RxNode> AtomEscape() {
        if (!More()) Fail("a pattern can not end with '\\'");
        char16_t c = Peek();
        if (c >= u'1' && c <= u'9') {
            int n;
            Digits(n);
            auto b = Node(NK_BackRef);
            b->c = (std::uint32_t)n;
            return b;
        }
        if (c == u'k' && Peek(1) == u'<') {
            i += 2;
            std::u16string name;
            while (More() && Peek() != u'>') name += p[i++];
            if (!Eat(u'>') || name.empty()) Fail("a named backreference must be written \\k<name>");
            auto b = Node(NK_BackRef);
            b->name = name;
            namedRefs.push_back({b.get(), name});
            return b;
        }
        RxClass cls;
        if (ClassEscape(c, cls)) {
            ++i;
            prog.classes.push_back(cls);
            auto n = Node(NK_Class);
            n->c = (std::uint32_t)prog.classes.size() - 1;
            return n;
        }
        auto n = Node(NK_Char);
        n->c = CharEscape(false);
        return n;
    }

    std::uint32_t ClassBody() {   // after '['
        RxClass cls;
        if (Eat(u'^')) cls.negate = true;
        while (true) {
            if (!More()) Fail("a character class is missing its ']'");
            if (Eat(u']')) break;
            std::uint32_t lo;
            bool isSet = false;
            if (Peek() == u'\\') {
                ++i;
                if (!More()) Fail("a character class is missing its ']'");
                if (ClassEscape(Peek(), cls)) { ++i; isSet = true; lo = 0; }
                else if (Peek() == u'b') { ++i; lo = 8; }
                else lo = CharEscape(true);
            } else lo = ReadCodePoint();
            if (isSet) continue;
            if (Peek() == u'-' && Peek(1) != u']' && Peek(1) != 0) {
                ++i;
                std::uint32_t hi;
                if (Peek() == u'\\') {
                    ++i;
                    RxClass tmp;
                    if (ClassEscape(Peek(), tmp)) Fail("a class range can not end with \\d, \\w or \\s");
                    hi = Peek() == u'b' ? (++i, 8u) : CharEscape(true);
                } else hi = ReadCodePoint();
                if (hi < lo) Fail("a class range is out of order");
                cls.ranges.push_back({lo, hi});
            } else cls.ranges.push_back({lo, lo});
        }
        std::sort(cls.ranges.begin(), cls.ranges.end());
        prog.classes.push_back(cls);
        return (std::uint32_t)prog.classes.size() - 1;
    }

    void ResolveNames(RxNode* n) {
        for (auto& [node, name] : namedRefs) {
            bool found = false;
            for (std::size_t g = 0; g < prog.names.size(); ++g)
                if (prog.names[g] == name) { node->c = (std::uint32_t)g; found = true; }
            if (!found) Fail("there is no group named '" + Utf16To8(name) + "'");
        }
        (void)n;
    }
};

// ---- compiling the tree
struct Emitter {
    RegexProg& prog;
    void Emit(RxOp op, std::uint32_t a = 0, std::uint32_t b = 0) {
        if (prog.code.size() >= kMaxProgram) throw RxFail{"the regular expression is too big"};
        prog.code.push_back(RxInst{op, a, b});
    }
    std::uint32_t Here() const { return (std::uint32_t)prog.code.size(); }
    bool back = false;   ///< inside a lookbehind: match right to left (b = 1 on the consuming instructions)

    void Gen(const RxNode* n) {
        const std::uint32_t dir = back ? 1 : 0;
        switch (n->k) {
            case NK_Empty: return;
            case NK_Char: Emit(RX_Char, n->c, dir); return;
            case NK_Any: Emit(RX_Any, 0, dir); return;
            case NK_Class: Emit(RX_Class, n->c, dir); return;
            case NK_Bol: Emit(RX_Bol); return;
            case NK_Eol: Emit(RX_Eol); return;
            case NK_WordB: Emit(n->flag ? RX_NotWordB : RX_WordB); return;
            case NK_BackRef: Emit(RX_BackRef, n->c, dir); return;
            case NK_Seq:
                if (back)
                    for (std::size_t k = n->kids.size(); k-- > 0;) Gen(n->kids[k].get());
                else
                    for (const auto& k : n->kids) Gen(k.get());
                return;
            case NK_Alt: {
                std::vector<std::uint32_t> ends;
                for (std::size_t k = 0; k < n->kids.size(); ++k) {
                    if (k + 1 < n->kids.size()) {
                        std::uint32_t split = Here();
                        Emit(RX_Split, split + 1, 0);
                        Gen(n->kids[k].get());
                        ends.push_back(Here());
                        Emit(RX_Jmp);
                        prog.code[split].b = Here();
                    } else Gen(n->kids[k].get());
                }
                for (std::uint32_t e : ends) prog.code[e].a = Here();
                return;
            }
            case NK_Group:
                if (n->flag) Emit(RX_Save, n->c * 2 + (back ? 1 : 0));
                Gen(n->kids[0].get());
                if (n->flag) Emit(RX_Save, n->c * 2 + (back ? 0 : 1));
                return;
            case NK_Look: {
                std::uint32_t at = Here();
                Emit(RX_Look, 0, n->flag ? 1 : 0);
                bool saved = back;
                back = n->min == 1;   // a lookbehind's body runs right to left, a lookahead's left to right
                Gen(n->kids[0].get());
                back = saved;
                Emit(RX_Match);
                prog.code[at].a = Here();   // where matching continues after the lookaround
                return;
            }
            case NK_Repeat: {
                const RxNode* body = n->kids[0].get();
                for (int k = 0; k < n->min; ++k) Gen(body);
                if (n->max == -1) {
                    std::uint32_t reg = (std::uint32_t)prog.regs++;
                    std::uint32_t loop = Here();
                    Emit(RX_Split);
                    std::uint32_t bodyAt = Here();
                    Emit(RX_LoopStart, reg);
                    Gen(body);
                    Emit(RX_LoopCheck, reg);
                    Emit(RX_Jmp, loop);
                    std::uint32_t exit = Here();
                    prog.code[loop].a = n->flag ? bodyAt : exit;
                    prog.code[loop].b = n->flag ? exit : bodyAt;
                } else {
                    std::vector<std::uint32_t> splits;
                    for (int k = n->min; k < n->max; ++k) {
                        splits.push_back(Here());
                        Emit(RX_Split);
                        std::uint32_t bodyAt = Here();
                        Gen(body);
                        prog.code[splits.back()].a = bodyAt;   // fixed up below for laziness
                    }
                    std::uint32_t exit = Here();
                    for (std::uint32_t s : splits) {
                        std::uint32_t bodyAt = prog.code[s].a;
                        prog.code[s].a = n->flag ? bodyAt : exit;
                        prog.code[s].b = n->flag ? exit : bodyAt;
                    }
                }
                return;
            }
        }
    }
};

bool IsLineTerm(std::uint32_t c) { return c == 0x0A || c == 0x0D || c == 0x2028 || c == 0x2029; }
bool IsWordChar(std::uint32_t c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'; }

std::uint32_t Canon(const RegexProg& p, std::uint32_t c) {
    if (!p.icase || c > 0xFFFF) return c;
    return JsUpper((char16_t)c);
}

bool InClass(const RegexProg& p, const RxClass& cls, std::uint32_t c) {
    auto has = [&](std::uint32_t x) {
        for (const auto& r : cls.ranges)
            if (x >= r.first && x <= r.second) return true;
        return false;
    };
    bool in = has(c);
    if (!in && p.icase && c <= 0xFFFF) in = has(JsUpper((char16_t)c)) || has(JsLower((char16_t)c));
    return in != cls.negate;
}

// ---- the backtracking machine
struct Machine {
    Runtime& rt;
    const RegexProg& p;
    const std::u16string& s;
    std::vector<int> caps;   ///< 2 per group: start, end (-1 = not matched)
    std::vector<int> regs;
    std::uint64_t steps = 0;

    struct Entry {
        std::uint8_t kind;   ///< 0 branch, 1 restore capture, 2 restore register
        std::uint32_t pc;
        int pos;
        std::uint32_t idx;
        int old;
    };

    Machine(Runtime& r, const RegexProg& pr, const std::u16string& str) : rt(r), p(pr), s(str) {
        caps.assign((std::size_t)p.groups * 2, -1);
        regs.assign((std::size_t)std::max(p.regs, 1), -1);
    }

    // The character at pos and its length in code units (a code point with the u flag).
    std::uint32_t At(int pos, int& len) const {
        char16_t c = s[(std::size_t)pos];
        if (p.unicode && c >= 0xD800 && c <= 0xDBFF && (std::size_t)pos + 1 < s.size() && s[(std::size_t)pos + 1] >= 0xDC00 &&
            s[(std::size_t)pos + 1] <= 0xDFFF) {
            len = 2;
            return 0x10000 + ((c - 0xD800) << 10) + (s[(std::size_t)pos + 1] - 0xDC00);
        }
        len = 1;
        return c;
    }

    // The character just before pos and its length (a whole surrogate pair with the u flag).
    std::uint32_t Before(int pos, int& len) const {
        char16_t c = s[(std::size_t)pos - 1];
        if (p.unicode && c >= 0xDC00 && c <= 0xDFFF && pos >= 2 && s[(std::size_t)pos - 2] >= 0xD800 && s[(std::size_t)pos - 2] <= 0xDBFF) {
            len = 2;
            return 0x10000 + ((s[(std::size_t)pos - 2] - 0xD800) << 10) + (c - 0xDC00);
        }
        len = 1;
        return c;
    }
    // The next character in the instruction's direction; false at the edge of the input.
    bool Step(const RxInst& in, int pos, std::uint32_t& c, int& len) const {
        if (in.b) {
            if (pos <= 0) return false;
            c = Before(pos, len);
            len = -len;
            return true;
        }
        if (pos >= (int)s.size()) return false;
        c = At(pos, len);
        return true;
    }

    void Tick() {
        if ((++steps & 63) == 0) rt.Charge(16);
    }

    // Run from pc at pos; on success `end` is where the match ended.
    bool Run(std::uint32_t pc, int pos, int& end, int depth) {
        if (depth > 100) rt.ThrowRange("the regular expression's lookaheads are nested too deeply");
        std::vector<Entry> stack;
        auto fail = [&]() -> bool {
            while (!stack.empty()) {
                Entry e = stack.back();
                stack.pop_back();
                if (e.kind == 1) caps[e.idx] = e.old;
                else if (e.kind == 2) regs[e.idx] = e.old;
                else {
                    pc = e.pc;
                    pos = e.pos;
                    return true;
                }
            }
            return false;
        };
        const int n = (int)s.size();
        while (true) {
            Tick();
            const RxInst& in = p.code[pc];
            bool ok = true;
            switch (in.op) {
                case RX_Char: {
                    int len;
                    std::uint32_t c;
                    if (!Step(in, pos, c, len)) { ok = false; break; }
                    if (Canon(p, c) != Canon(p, in.a)) { ok = false; break; }
                    pos += len;
                    ++pc;
                    break;
                }
                case RX_Any: {
                    int len;
                    std::uint32_t c;
                    if (!Step(in, pos, c, len)) { ok = false; break; }
                    if (!p.dotAll && IsLineTerm(c)) { ok = false; break; }
                    pos += len;
                    ++pc;
                    break;
                }
                case RX_Class: {
                    int len;
                    std::uint32_t c;
                    if (!Step(in, pos, c, len)) { ok = false; break; }
                    if (!InClass(p, p.classes[in.a], c)) { ok = false; break; }
                    pos += len;
                    ++pc;
                    break;
                }
                case RX_Split:
                    if (stack.size() > 4000000) rt.ThrowRange("the regular expression needs too much backtracking memory");
                    stack.push_back(Entry{0, in.b, pos, 0, 0});
                    pc = in.a;
                    break;
                case RX_Jmp: pc = in.a; break;
                case RX_Save:
                    stack.push_back(Entry{1, 0, 0, in.a, caps[in.a]});
                    caps[in.a] = pos;
                    ++pc;
                    break;
                case RX_Bol:
                    if (!(pos == 0 || (p.multiline && IsLineTerm(s[(std::size_t)pos - 1])))) ok = false;
                    else ++pc;
                    break;
                case RX_Eol:
                    if (!(pos == n || (p.multiline && IsLineTerm(s[(std::size_t)pos])))) ok = false;
                    else ++pc;
                    break;
                case RX_WordB: case RX_NotWordB: {
                    bool a = pos > 0 && IsWordChar(s[(std::size_t)pos - 1]);
                    bool b = pos < n && IsWordChar(s[(std::size_t)pos]);
                    if ((a != b) != (in.op == RX_WordB)) ok = false;
                    else ++pc;
                    break;
                }
                case RX_Look: {
                    std::vector<int> savedCaps = caps, savedRegs = regs;
                    int subEnd;
                    bool matched = Run(pc + 1, pos, subEnd, depth + 1);
                    if (in.b) {   // negative: captures inside never count
                        caps = savedCaps;
                        regs = savedRegs;
                        if (matched) ok = false;
                        else pc = in.a;
                    } else {
                        regs = savedRegs;
                        if (!matched) {
                            caps = savedCaps;
                            ok = false;
                        } else {
                            for (std::size_t k = 0; k < caps.size(); ++k)   // undo the captures on backtracking
                                if (caps[k] != savedCaps[k]) stack.push_back(Entry{1, 0, 0, (std::uint32_t)k, savedCaps[k]});
                            pc = in.a;
                        }
                    }
                    break;
                }
                case RX_BackRef: {
                    std::size_t g = in.a;
                    if (g * 2 + 1 >= caps.size()) { ++pc; break; }   // a reference to a group that does not exist matches empty
                    int a = caps[g * 2], b = caps[g * 2 + 1];
                    if (a < 0 || b < 0) { ++pc; break; }
                    int len = b - a;
                    int from = in.b ? pos - len : pos;   // backward: the text just before pos must equal the group
                    if (from < 0 || from + len > n) { ok = false; break; }
                    for (int k = 0; k < len && ok; ++k)
                        if (Canon(p, s[(std::size_t)(a + k)]) != Canon(p, s[(std::size_t)(from + k)])) ok = false;
                    if (ok) { pos = in.b ? from : pos + len; ++pc; }
                    break;
                }
                case RX_LoopStart:
                    stack.push_back(Entry{2, 0, 0, in.a, regs[in.a]});
                    regs[in.a] = pos;
                    ++pc;
                    break;
                case RX_LoopCheck:
                    if (regs[in.a] == pos) ok = false;   // the body matched nothing: no more turns (22.2.2.3.1)
                    else ++pc;
                    break;
                case RX_Match:
                    end = pos;
                    return true;
            }
            if (!ok && !fail()) return false;
        }
    }
};

std::shared_ptr<RegexProg> CompilePattern(const std::u16string& pattern, const std::u16string& flags, std::string& err) {
    auto prog = std::make_shared<RegexProg>();
    for (char16_t f : flags) {
        bool* slot = f == u'i' ? &prog->icase : f == u'm' ? &prog->multiline : f == u's' ? &prog->dotAll : f == u'u' ? &prog->unicode
                   : f == u'g' ? &prog->global : f == u'y' ? &prog->sticky : nullptr;
        if (f == u'd') continue;
        if (!slot) { err = "unknown regular expression flag '" + Utf16To8(std::u16string(1, f)) + "'"; return nullptr; }
        if (*slot) { err = "the flag '" + Utf16To8(std::u16string(1, f)) + "' is given twice"; return nullptr; }
        *slot = true;
    }
    try {
        PatternParser pp(pattern, *prog);
        prog->names.resize(1);
        auto tree = pp.Disjunction();
        if (pp.More()) pp.Fail("unmatched ')'");
        pp.ResolveNames(tree.get());
        Emitter em{*prog};
        em.Emit(RX_Save, 0);
        em.Gen(tree.get());
        em.Emit(RX_Save, 1);
        em.Emit(RX_Match);
        // a literal first character lets the search skip ahead
        if (tree->k == NK_Seq && !tree->kids.empty() && tree->kids[0]->k == NK_Char && !prog->icase && tree->kids[0]->c < 0xD800)
            prog->firstChar = (int)tree->kids[0]->c;
    } catch (const RxFail& f) {
        err = f.why;
        return nullptr;
    }
    return prog;
}

} // namespace

bool RegexSyntaxOk(const std::u16string& pattern, const std::u16string& flags, std::string& err) {
    return CompilePattern(pattern, flags, err) != nullptr;
}

namespace {

std::shared_ptr<RegexProg> Compiled(Runtime& rt, const std::u16string& pattern, const std::u16string& flags) {
    // a literal evaluated every frame compiles once (programs are never changed after compiling)
    auto& cache = rt.vm->regexCache;
    std::u16string key = flags + u"/" + pattern;
    auto it = cache.find(key);
    if (it != cache.end()) return it->second;
    std::string err;
    auto p = CompilePattern(pattern, flags, err);
    if (!p) rt.ThrowError(ErrorType::SyntaxError, "bad regular expression /" + Utf16To8(pattern) + "/: " + err);
    rt.Charge(p->code.size() / 8 + 1);
    if (cache.size() >= 256) cache.clear();
    if (pattern.size() < 4096) cache.emplace(std::move(key), p);
    return p;
}

RegExpObj* ThisRegExp(Runtime& rt, const Value& v, const char* m) {
    if (!v.IsObj() || v.O()->cls != Cls::RegExpLike) rt.ThrowType(std::string("RegExp.prototype.") + m + " needs a RegExp");
    return static_cast<RegExpObj*>(v.O());
}

RegExpObj* NewRegExp(Runtime& rt, const std::u16string& pattern, const std::u16string& flags, Object* proto) {
    std::shared_ptr<RegexProg> p = Compiled(rt, pattern, flags);
    RegExpObj* r = rt.Alloc<RegExpObj>();
    r->proto = proto ? proto : rt.regexpProto;
    r->prog = p;
    r->source = rt.NewString(pattern.empty() ? u"(?:)" : pattern);
    std::u16string f;
    for (char16_t c : u"dgimsuy")   // the canonical order of 22.2.6.4 get flags
        if (c && flags.find(c) != std::u16string::npos) f += c;
    r->flags = rt.NewString(f);
    r->AddRaw(rt.atoms->lastIndex, Value::Num(0), kWritable);
    return r;
}

// One match attempt from `from`, scanning forward unless sticky. Caps get the groups.
bool MatchFrom(Runtime& rt, const RegexProg& p, const std::u16string& s, std::size_t from, bool sticky, std::vector<int>& caps) {
    Machine m(rt, p, s);
    for (std::size_t at = from; at <= s.size(); ++at) {
        if (!sticky && p.firstChar >= 0) {
            std::size_t f = s.find((char16_t)p.firstChar, at);
            rt.Charge(((f == std::u16string::npos ? s.size() : f) - at) / 32 + 1);
            if (f == std::u16string::npos) return false;
            at = f;
        }
        std::fill(m.caps.begin(), m.caps.end(), -1);
        std::fill(m.regs.begin(), m.regs.end(), -1);
        int end;
        rt.Charge(1);
        if (m.Run(0, (int)at, end, 0)) {
            caps = m.caps;
            return true;
        }
        if (sticky) return false;
        if (p.unicode && at < s.size() && s[at] >= 0xD800 && s[at] <= 0xDBFF && at + 1 < s.size() && s[at + 1] >= 0xDC00 && s[at + 1] <= 0xDFFF) ++at;
    }
    return false;
}

std::size_t LastIndexOf(Runtime& rt, RegExpObj* r) {
    Value li = rt.Get(Value::Obj(r), rt.atoms->lastIndex);
    double d = rt.ToIntegerOrInfinity(li);
    return d <= 0 ? 0 : d > 9e15 ? (std::size_t)9e15 : (std::size_t)d;
}
void SetLastIndex(Runtime& rt, RegExpObj* r, std::size_t v) { rt.Put(Value::Obj(r), rt.atoms->lastIndex, Value::Num((double)v)); }

// 22.2.7.2 RegExpBuiltinExec: the match array or null.
Value Exec(Runtime& rt, RegExpObj* r, String* str) {
    const RegexProg& p = *r->prog;
    const std::u16string& s = str->s;
    bool gy = p.global || p.sticky;
    std::size_t last = gy ? LastIndexOf(rt, r) : 0;
    if (last > s.size()) {
        if (gy) SetLastIndex(rt, r, 0);
        return Value::Null();
    }
    std::vector<int> caps;
    if (!MatchFrom(rt, p, s, last, p.sticky, caps)) {
        if (gy) SetLastIndex(rt, r, 0);
        return Value::Null();
    }
    if (gy) SetLastIndex(rt, r, (std::size_t)caps[1]);
    ArrayObj* a = rt.NewArray((std::size_t)p.groups);
    Value av = Value::Obj(a);
    rt.Keep(av);
    Object* groups = nullptr;
    for (int g = 0; g < p.groups; ++g) {
        int b = caps[(std::size_t)g * 2], e = caps[(std::size_t)g * 2 + 1];
        Value v = (b < 0 || e < 0) ? Value() : Value::Str(rt.NewString(s.substr((std::size_t)b, (std::size_t)(e - b))));
        a->elems.push_back(v);
        if (g < (int)p.names.size() && !p.names[(std::size_t)g].empty()) {
            if (!groups) groups = rt.NewObject(nullptr);
            rt.DefineData(groups, rt.Intern(p.names[(std::size_t)g]), v);
        }
    }
    a->length = (std::uint32_t)a->elems.size();
    rt.DefineData(a, rt.atoms->index, Value::Num(caps[0]));
    rt.DefineData(a, rt.atoms->input, Value::Str(str));
    rt.DefineData(a, rt.Intern("groups"), groups ? Value::Obj(groups) : Value());
    return av;
}

// 22.1.3.19.1 GetSubstitution with the captures of a RegExp match (caps: 2 positions per group)
std::u16string Substitution(const std::u16string& str, const std::vector<int>& caps, const std::u16string& rep, const RegexProg& p) {
    std::u16string out;
    auto cap = [&](std::size_t g) -> std::u16string {
        if (g * 2 + 1 >= caps.size() || caps[g * 2] < 0 || caps[g * 2 + 1] < 0) return u"";
        return str.substr((std::size_t)caps[g * 2], (std::size_t)(caps[g * 2 + 1] - caps[g * 2]));
    };
    const std::size_t pos = (std::size_t)caps[0], end = (std::size_t)caps[1];
    for (std::size_t i = 0; i < rep.size(); ++i) {
        char16_t c = rep[i];
        if (c != u'$' || i + 1 >= rep.size()) { out += c; continue; }
        char16_t n = rep[i + 1];
        if (n == u'$') { out += u'$'; ++i; continue; }
        if (n == u'&') { out += cap(0); ++i; continue; }
        if (n == u'`') { out += str.substr(0, pos); ++i; continue; }
        if (n == u'\'') { if (end < str.size()) out += str.substr(end); ++i; continue; }
        if (n >= u'0' && n <= u'9') {
            std::size_t g = (std::size_t)(n - u'0');
            std::size_t used = 1;
            if (i + 2 < rep.size() && rep[i + 2] >= u'0' && rep[i + 2] <= u'9') {
                std::size_t two = g * 10 + (std::size_t)(rep[i + 2] - u'0');
                if (two >= 1 && two < (std::size_t)p.groups) { g = two; used = 2; }
            }
            if (g >= 1 && g < (std::size_t)p.groups) {
                out += cap(g);
                i += used;
                continue;
            }
        }
        if (n == u'<') {
            std::size_t close = rep.find(u'>', i + 2);
            bool hasNames = std::any_of(p.names.begin(), p.names.end(), [](const std::u16string& x) { return !x.empty(); });
            if (hasNames && close != std::u16string::npos) {
                std::u16string name = rep.substr(i + 2, close - i - 2);
                for (std::size_t g = 0; g < p.names.size(); ++g)
                    if (p.names[g] == name) out += cap(g);
                i = close;
                continue;
            }
        }
        out += c;
    }
    return out;
}

} // namespace

bool IsRegExp(const Value& v) { return v.IsObj() && v.O()->cls == Cls::RegExpLike; }

void RegExpObj::Trace(Runtime& rt) {
    Object::Trace(rt);
    if (source) rt.Mark(source);
    if (flags) rt.Mark(flags);
}

Value RegExpReplace(Runtime& rt, const Value& rx, String* str, const Value& replacement, bool all) {
    RegExpObj* r = static_cast<RegExpObj*>(rx.O());
    const RegexProg& p = *r->prog;
    if (all && !p.global) rt.ThrowType("replaceAll needs a RegExp with the g flag");
    const bool global = p.global;
    const bool fn = rt.IsCallable(replacement);
    std::u16string rep = fn ? u"" : rt.ToU16(replacement);
    const std::u16string& s = str->s;
    // 22.1.3.19 with 22.2.6.11 @@replace: the matches from lastIndex (0 for a global search), each replaced
    std::size_t at = global ? 0 : (p.sticky ? LastIndexOf(rt, r) : 0);
    std::u16string out;
    std::size_t last = 0;
    std::vector<int> caps;
    Runtime::RootedVector keep(rt);
    while (at <= s.size() && MatchFrom(rt, p, s, at, p.sticky, caps)) {
        std::size_t pos = (std::size_t)caps[0], end = (std::size_t)caps[1];
        out += s.substr(last, pos - last);
        if (fn) {
            Runtime::ScratchScope ss(rt);
            keep.v.clear();
            Object* groups = nullptr;
            for (int g = 0; g < p.groups; ++g) {
                int b = caps[(std::size_t)g * 2], e = caps[(std::size_t)g * 2 + 1];
                Value v = (b < 0 || e < 0) ? Value() : Value::Str(rt.NewString(s.substr((std::size_t)b, (std::size_t)(e - b))));
                keep.v.push_back(v);
                if (g < (int)p.names.size() && !p.names[(std::size_t)g].empty()) {
                    if (!groups) { groups = rt.NewObject(nullptr); }
                    rt.DefineData(groups, rt.Intern(p.names[(std::size_t)g]), v);
                }
            }
            keep.v.push_back(Value::Num((double)pos));
            keep.v.push_back(Value::Str(str));
            if (groups) keep.v.push_back(Value::Obj(groups));
            out += rt.ToU16(rt.Call(replacement, Value(), keep.v.data(), (int)keep.v.size()));
        } else out += Substitution(s, caps, rep, p);
        last = end;
        if (out.size() > (1u << 28)) rt.ThrowRange("replace: the result is too long");
        if (!global) break;
        at = end == pos ? end + 1 : end;   // step over an empty match
    }
    if (global || p.sticky) SetLastIndex(rt, r, global ? 0 : (caps.empty() ? 0 : (std::size_t)caps[1]));
    if (last < s.size()) out += s.substr(last);
    rt.Charge(out.size() / 32);
    return Value::Str(rt.NewString(std::move(out)));
}

Value RegExpSplit(Runtime& rt, const Value& rx, String* str, std::uint32_t limit) {
    RegExpObj* r = static_cast<RegExpObj*>(rx.O());
    const RegexProg& p = *r->prog;
    const std::u16string& s = str->s;
    ArrayObj* out = rt.NewArray();
    Value outV = Value::Obj(out);
    rt.Keep(outV);
    if (limit == 0) return outV;
    if (s.empty()) {   // 22.2.6.14 step 16: an empty string splits into [""] unless the pattern matches it
        std::vector<int> caps;
        if (!MatchFrom(rt, p, s, 0, true, caps)) rt.DefineIndex(out, 0, Value::Str(str));
        return outV;
    }
    // 22.2.6.14 steps 19-24: p is where the current piece starts, q where a separator is tried
    std::size_t start = 0, q = 0;
    while (q < s.size()) {
        std::vector<int> caps;
        if (!MatchFrom(rt, p, s, q, true, caps)) { ++q; continue; }   // a sticky match at q
        std::size_t e = std::min((std::size_t)caps[1], s.size());
        if (e == start) { ++q; continue; }   // an empty separator right where the piece starts
        rt.DefineIndex(out, out->length, Value::Str(rt.NewString(s.substr(start, q - start))));
        if (out->length >= limit) return outV;
        for (int g = 1; g < p.groups; ++g) {
            int b = caps[(std::size_t)g * 2], ce = caps[(std::size_t)g * 2 + 1];
            rt.DefineIndex(out, out->length, (b < 0 || ce < 0) ? Value() : Value::Str(rt.NewString(s.substr((std::size_t)b, (std::size_t)(ce - b)))));
            if (out->length >= limit) return outV;
        }
        start = e;
        q = start;
    }
    rt.DefineIndex(out, out->length, Value::Str(rt.NewString(s.substr(start))));
    return outV;
}

Value RegExpMatch(Runtime& rt, const Value& rx, String* str, int mode) {   // 0 match, 1 matchAll, 2 search
    RegExpObj* r = static_cast<RegExpObj*>(rx.O());
    const RegexProg& p = *r->prog;
    if (mode == 2) {
        std::vector<int> caps;
        if (!MatchFrom(rt, p, str->s, 0, p.sticky, caps)) return Value::Num(-1);
        return Value::Num(caps[0]);
    }
    if (mode == 0 && !p.global) return Exec(rt, r, str);
    if (mode == 1 && !p.global) rt.ThrowType("matchAll needs a RegExp with the g flag");
    SetLastIndex(rt, r, 0);
    if (mode == 0) {   // match with g: only the matched strings (no match arrays to collect)
        ArrayObj* out = rt.NewArray();
        Value outV = Value::Obj(out);
        rt.Keep(outV);
        std::vector<int> caps;
        const std::u16string& s = str->s;
        for (std::size_t at = 0; at <= s.size() && MatchFrom(rt, p, s, at, p.sticky, caps);) {
            std::size_t b = (std::size_t)caps[0], e = (std::size_t)caps[1];
            rt.DefineIndex(out, out->length, Value::Str(rt.NewString(s.substr(b, e - b))));
            at = e == b ? e + 1 : e;
        }
        return out->length ? outV : Value::Null();
    }
    ArrayObj* out = rt.NewArray();
    Value outV = Value::Obj(out);
    rt.Keep(outV);
    while (true) {
        Runtime::ScratchScope ss(rt);
        Value m = Exec(rt, r, str);
        if (m.IsNull()) break;
        ArrayObj* ma = static_cast<ArrayObj*>(m.O());
        rt.DefineIndex(out, out->length, mode == 0 ? ma->elems[0] : m);
        if (ma->elems[0].S()->s.empty()) SetLastIndex(rt, r, LastIndexOf(rt, r) + 1);
    }
    if (mode == 0 && out->length == 0) return Value::Null();
    if (mode == 1) {
        Value values = rt.Get(outV, rt.symIterator);
        return rt.Call(values, outV, nullptr, 0);
    }
    return outV;
}

void InstallRegExpLib(Runtime& rt) {
    rt.regexpProto = rt.NewObject();
    NativeFunc* ctor = DefCtor(rt, "RegExp", [](Runtime& rt, CallArgs& a) {
        Value pat = a[0];
        std::u16string pattern, flags;
        if (IsRegExp(pat)) {
            RegExpObj* src = static_cast<RegExpObj*>(pat.O());
            if (!a.newTarget && a[1].IsUndef()) return pat;
            pattern = src->source->s == u"(?:)" ? u"" : src->source->s;
            flags = a[1].IsUndef() ? src->flags->s : rt.ToU16(a[1]);
        } else {
            pattern = pat.IsUndef() ? u"" : rt.ToU16(pat);
            flags = a[1].IsUndef() ? u"" : rt.ToU16(a[1]);
        }
        Object* proto = nullptr;
        if (a.newTarget) {
            Value pr = rt.Get(Value::Obj(a.newTarget), rt.atoms->prototype);
            if (pr.IsObj()) proto = pr.O();
        }
        return Value::Obj(NewRegExp(rt, pattern, flags, proto));
    }, 2, rt.regexpProto);
    (void)ctor;
    Object* rp = rt.regexpProto;
    DefNative(rt, rp, "exec", [](Runtime& rt, CallArgs& a) {
        RegExpObj* r = ThisRegExp(rt, a.thisv, "exec");
        String* s = rt.ToString(a[0]);
        rt.Keep(Value::Str(s));
        return Exec(rt, r, s);
    }, 1);
    DefNative(rt, rp, "test", [](Runtime& rt, CallArgs& a) {
        RegExpObj* r = ThisRegExp(rt, a.thisv, "test");
        String* s = rt.ToString(a[0]);
        rt.Keep(Value::Str(s));
        return Value::Bool(!Exec(rt, r, s).IsNull());
    }, 1);
    DefNative(rt, rp, "toString", [](Runtime& rt, CallArgs& a) {
        RegExpObj* r = ThisRegExp(rt, a.thisv, "toString");
        return Value::Str(rt.NewString(u"/" + r->source->s + u"/" + r->flags->s));
    }, 0);
    DefGetter(rt, rp, "source", [](Runtime& rt, CallArgs& a) { return Value::Str(ThisRegExp(rt, a.thisv, "source")->source); });
    DefGetter(rt, rp, "flags", [](Runtime& rt, CallArgs& a) { return Value::Str(ThisRegExp(rt, a.thisv, "flags")->flags); });
    DefGetter(rt, rp, "global", [](Runtime& rt, CallArgs& a) { return Value::Bool(ThisRegExp(rt, a.thisv, "global")->prog->global); });
    DefGetter(rt, rp, "ignoreCase", [](Runtime& rt, CallArgs& a) { return Value::Bool(ThisRegExp(rt, a.thisv, "ignoreCase")->prog->icase); });
    DefGetter(rt, rp, "multiline", [](Runtime& rt, CallArgs& a) { return Value::Bool(ThisRegExp(rt, a.thisv, "multiline")->prog->multiline); });
    DefGetter(rt, rp, "dotAll", [](Runtime& rt, CallArgs& a) { return Value::Bool(ThisRegExp(rt, a.thisv, "dotAll")->prog->dotAll); });
    DefGetter(rt, rp, "unicode", [](Runtime& rt, CallArgs& a) { return Value::Bool(ThisRegExp(rt, a.thisv, "unicode")->prog->unicode); });
    DefGetter(rt, rp, "sticky", [](Runtime& rt, CallArgs& a) { return Value::Bool(ThisRegExp(rt, a.thisv, "sticky")->prog->sticky); });
}

Value MakeRegExpLiteral(Runtime& rt, String* pattern, String* flags) {
    return Value::Obj(NewRegExp(rt, pattern->s, flags->s, nullptr));
}

} // namespace okay::js
