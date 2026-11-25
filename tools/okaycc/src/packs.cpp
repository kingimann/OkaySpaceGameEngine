// Variadic function templates ([temp.variadic]) by arity, done on the token stream before parsing.
//
// Written from ISO/IEC 14882 [temp.variadic] (packs and pack expansions), [expr.sizeof] p5
// (sizeof...) and [expr.prim.fold] (fold expressions), in our own structure. A function template
// with a template parameter pack is replaced by copies of itself, one per pack length 0..kMaxPack,
// most parameters first. In the copy for length k, every pack expansion "pattern..." becomes k
// copies of the pattern (with each pack name P renamed P__pk<j>), sizeof...(P) becomes k, and a
// fold expression is written out with explicit parentheses. The copies are ordinary function
// templates of one name, which the parser already keeps as overloads and the checker chooses
// between by argument count and deduction ([temp.over]). Class templates with a pack are left as
// they are (not supported yet).
#include "packs.hpp"

#include <set>
#include <string>

namespace okaycc {
namespace compiler {
namespace {

const int kMaxPack = 8;

bool isPunct(const Token& t, const char* s) { return t.kind == Tok::Punct && t.text == s; }
bool isWord(const Token& t, const char* s) { return t.isName() && t.text == s; }
bool isOpen(const Token& t) { return isPunct(t, "(") || isPunct(t, "[") || isPunct(t, "{"); }
bool isClose(const Token& t) { return isPunct(t, ")") || isPunct(t, "]") || isPunct(t, "}"); }

// A binary operator that may stand next to "..." in a fold expression ([expr.prim.fold] p1).
bool isFoldOp(const Token& t) {
    static const std::set<std::string> ops = {"+", "-", "*", "/", "%", "^", "&", "|", "<<", ">>", "+=", "-=",
                                              "*=", "/=", "%=", "^=", "&=", "|=", "<<=", ">>=", "=", "==",
                                              "!=", "<", ">", "<=", ">=", "&&", "||", ".*", "->*"};
    return t.kind == Tok::Punct && ops.count(t.text) > 0;
}

Token like(const Token& at, Tok kind, const std::string& text) {
    Token t = at;
    if (kind == Tok::Keyword && !isKeywordSpelling(text)) kind = Tok::Ident;
    t.kind = kind;
    t.text = text;
    t.hide.clear();
    if (kind == Tok::Int) {
        t.uval = std::stoull(text);
        t.isUnsigned = false;
        t.longRank = 0;
        t.base = 10;
    }
    return t;
}

// Index just after the declaration that starts at 'from': its body's closing '}' or its ';'.
std::size_t declEnd(const std::vector<Token>& d, std::size_t from) {
    int paren = 0;
    for (std::size_t k = from; k < d.size() && d[k].kind != Tok::End; ++k) {
        if (isPunct(d[k], "(")) ++paren;
        else if (isPunct(d[k], ")")) --paren;
        else if (isPunct(d[k], ";") && paren == 0) return k + 1;
        else if (isPunct(d[k], "{") && paren == 0) {
            int depth = 0;
            for (; k < d.size() && d[k].kind != Tok::End; ++k) {
                if (isPunct(d[k], "{")) ++depth;
                else if (isPunct(d[k], "}") && --depth == 0) return k + 1;
            }
            return k;
        }
    }
    return d.size();
}

// The closing '>' of the '<' at 'lt', or 0.
std::size_t angleEnd(const std::vector<Token>& d, std::size_t lt) {
    int depth = 0;
    int paren = 0;
    for (std::size_t k = lt; k < d.size() && d[k].kind != Tok::End; ++k) {
        if (isOpen(d[k])) ++paren;
        else if (isClose(d[k])) --paren;
        else if (paren == 0 && isPunct(d[k], "<")) ++depth;
        else if (paren == 0 && isPunct(d[k], ">") && --depth == 0) return k;
        else if (paren == 0 && isPunct(d[k], ">>")) {
            depth -= 2;
            if (depth <= 0) return k;
        }
    }
    return 0;
}

class Expander {
public:
    Expander(const std::set<std::string>& packs, int k) : packs_(packs), k_(k) {}

    std::vector<Token> run(std::vector<Token> d) {
        for (std::size_t i = 0; i < d.size(); ++i) {
            if (isWord(d[i], "sizeof") && i + 4 < d.size() && isPunct(d[i + 1], "...") && isPunct(d[i + 2], "(") &&
                isPunct(d[i + 4], ")")) {
                // sizeof...(P) ([expr.sizeof] p5): the length of the pack.
                Token n = like(d[i], Tok::Int, std::to_string(k_));
                d.erase(d.begin() + static_cast<long>(i), d.begin() + static_cast<long>(i + 5));
                d.insert(d.begin() + static_cast<long>(i), n);
                continue;
            }
            if (!isPunct(d[i], "...")) continue;
            std::size_t next = expandAt(d, i);
            if (next != 0) i = next - 1;
        }
        return d;
    }

private:
    const std::set<std::string>& packs_;
    int k_;
    std::set<std::string> fnPacks_;  // function parameter packs declared so far ([temp.variadic] p3)

    bool isPackName(const Token& t) const {
        return t.kind == Tok::Ident && (packs_.count(t.text) || fnPacks_.count(t.text));
    }
    bool hasPack(const std::vector<Token>& d, std::size_t a, std::size_t b) const {
        for (std::size_t k = a; k < b; ++k)
            if (isPackName(d[k])) return true;
        return false;
    }

    // The list element around d[i]: [a, b), stopping at a ',' or an unmatched bracket at depth 0.
    std::size_t elemStart(const std::vector<Token>& d, std::size_t i) const {
        int depth = 0;
        std::size_t j = i;
        while (j > 0) {
            const Token& t = d[j - 1];
            if (isClose(t) || isPunct(t, ">")) ++depth;
            else if (isPunct(t, ">>")) depth += 2;
            else if (isOpen(t) || isPunct(t, "<")) {
                if (depth == 0) break;
                --depth;
            } else if (depth == 0 && (isPunct(t, ",") || isPunct(t, ";"))) break;
            --j;
        }
        return j;
    }
    std::size_t elemEnd(const std::vector<Token>& d, std::size_t i) const {
        int depth = 0;
        std::size_t j = i + 1;
        while (j < d.size()) {
            const Token& t = d[j];
            if (isOpen(t) || (isPunct(t, "<") && j > 0 && d[j - 1].kind == Tok::Ident)) ++depth;
            else if (isClose(t) || isPunct(t, ">") || isPunct(t, ">>")) {
                if (depth == 0) break;
                --depth;
            } else if (depth == 0 && (isPunct(t, ",") || isPunct(t, ";"))) break;
            ++j;
        }
        return j;
    }

    // Copy j of the tokens [a, b) without the '...' at 'dots', pack names renamed.
    void copyPattern(const std::vector<Token>& d, std::size_t a, std::size_t b, std::size_t dots, int j,
                     std::vector<Token>& out) const {
        for (std::size_t k = a; k < b; ++k) {
            if (k == dots) continue;
            Token t = d[k];
            bool member = k > a && (isPunct(d[k - 1], ".") || isPunct(d[k - 1], "->"));
            if (isPackName(t) && !member) t.text += "__pk" + std::to_string(j);
            out.push_back(std::move(t));
        }
    }

    std::size_t replace(std::vector<Token>& d, std::size_t a, std::size_t b, const std::vector<Token>& with) {
        d.erase(d.begin() + static_cast<long>(a), d.begin() + static_cast<long>(b));
        d.insert(d.begin() + static_cast<long>(a), with.begin(), with.end());
        return a + with.size();
    }

    // A fold expression ([expr.prim.fold]): ( E op ... ), ( ... op E ), ( E op ... op I ), ( I op ... op E ).
    // Returns the index after the expansion, or 0 if d[i] is not in one.
    std::size_t fold(std::vector<Token>& d, std::size_t i) {
        std::size_t a = elemStart(d, i);
        std::size_t b = elemEnd(d, i);
        bool commaRight = false;
        if (a == i && a > 0 && isPunct(d[a - 1], ",") && b == i + 1 && b < d.size() && isPunct(d[b], ")")) {
            // ( E , ... ): the comma fold; the element before the comma is E.
            std::size_t ea = elemStart(d, a - 1);
            if (ea == 0 || !isPunct(d[ea - 1], "(") || !hasPack(d, ea, a - 1)) return 0;
            a = ea;
            commaRight = true;
        }
        if (a == 0 || !isPunct(d[a - 1], "(") || b >= d.size() || !isPunct(d[b], ")")) return 0;
        bool opBefore = i > a && (isFoldOp(d[i - 1]) || commaRight);
        bool opAfter = i + 1 < b && isFoldOp(d[i + 1]);
        if (!opBefore && !opAfter) return 0;
        // "Args&&... args" and "Args&&..." declare a parameter pack; they are not folds.
        if (!opAfter && i + 1 < b) return 0;
        if (!opAfter && !commaRight && i >= 2 && d[i - 2].kind == Tok::Ident && packs_.count(d[i - 2].text) &&
            (d[i - 1].text == "&&" || d[i - 1].text == "&" || d[i - 1].text == "*"))
            return 0;
        Token op = opBefore ? d[i - 1] : d[i + 1];
        std::vector<Token> out;
        auto emptyValue = [&]() {
            // An empty unary fold ([expr.prim.fold] p3): && is true, || is false, the comma is void().
            if (op.text == "&&") out.push_back(like(op, Tok::Keyword, "true"));
            else if (op.text == "||") out.push_back(like(op, Tok::Keyword, "false"));
            else {
                out.push_back(like(op, Tok::Punct, "("));
                out.push_back(like(op, Tok::Keyword, "void"));
                out.push_back(like(op, Tok::Punct, ")"));
                out.push_back(like(op, Tok::Int, "0"));
            }
        };
        auto pattern = [&](std::size_t pa, std::size_t pb, int j) {
            out.push_back(like(op, Tok::Punct, "("));
            copyPattern(d, pa, pb, d.size(), j, out);
            out.push_back(like(op, Tok::Punct, ")"));
        };
        bool left = i == a || (opAfter && !opBefore) || (opAfter && opBefore && hasPack(d, i + 2, b));
        if (left) {
            // Left fold: ( ... op E ) or ( I op ... op E ), written (((I op E0) op E1) ...).
            std::size_t ea = i + 2;
            bool hasInit = i > a;
            if (hasInit && !isPunct(d[i - 1], op.text.c_str())) return 0;
            if (k_ == 0) {
                if (hasInit) copyPattern(d, a, i - 1, d.size(), 0, out);
                else emptyValue();
            } else {
                for (int j = 0; j < k_; ++j) out.push_back(like(op, Tok::Punct, "("));
                if (hasInit) {
                    out.push_back(like(op, Tok::Punct, "("));
                    copyPattern(d, a, i - 1, d.size(), 0, out);
                    out.push_back(like(op, Tok::Punct, ")"));
                }
                for (int j = 0; j < k_; ++j) {
                    if (j > 0 || hasInit) out.push_back(op);
                    pattern(ea, b, j);
                    out.push_back(like(op, Tok::Punct, ")"));
                }
            }
        } else {
            // Right fold: ( E op ... ) or ( E op ... op I ), written (E0 op (E1 op (... I))).
            std::size_t pe = i - 1;
            bool hasInit = i + 1 < b;
            if (hasInit && !isPunct(d[i + 1], op.text.c_str())) return 0;
            if (k_ == 0) {
                if (hasInit) copyPattern(d, i + 2, b, d.size(), 0, out);
                else emptyValue();
            } else {
                for (int j = 0; j < k_; ++j) {
                    if (j > 0) {
                        out.push_back(op);
                        out.push_back(like(op, Tok::Punct, "("));
                    }
                    pattern(a, pe, j);
                }
                if (hasInit) {
                    out.push_back(op);
                    out.push_back(like(op, Tok::Punct, "("));
                    copyPattern(d, i + 2, b, d.size(), 0, out);
                    out.push_back(like(op, Tok::Punct, ")"));
                }
                for (int j = 1; j < k_; ++j) out.push_back(like(op, Tok::Punct, ")"));
            }
        }
        return replace(d, a, b, out);
    }

    // The '...' at d[i]. Returns the index after what replaced it, or 0 when it is left alone (the
    // C variable argument list, or a pattern with no pack).
    std::size_t expandAt(std::vector<Token>& d, std::size_t i) {
        if (std::size_t f = fold(d, i)) return f;
        std::size_t a = elemStart(d, i);
        std::size_t b = elemEnd(d, i);
        if (i + 1 < b && d[i + 1].kind == Tok::Ident && !isPackName(d[i + 1]) && hasPack(d, a, i)) {
            // A function parameter pack: "Args&&... args" declares args ([dcl.fct] p17).
            fnPacks_.insert(d[i + 1].text);
        }
        if (!hasPack(d, a, b)) return 0;
        std::vector<Token> out;
        for (int j = 0; j < k_; ++j) {
            if (j > 0) out.push_back(like(d[i], Tok::Punct, ","));
            copyPattern(d, a, b, i, j, out);
        }
        if (k_ == 0) {
            // An empty expansion leaves no element: drop one comma next to it.
            if (a > 0 && isPunct(d[a - 1], ",")) --a;
            else if (b < d.size() && isPunct(d[b], ",")) ++b;
        }
        std::size_t r = replace(d, a, b, out);
        return r == 0 ? 1 : r;
    }
};

}  // namespace

std::vector<Token> expandPacks(const std::vector<Token>& toks) {
    bool any = false;
    for (std::size_t i = 0; i + 1 < toks.size() && !any; ++i)
        if (isPunct(toks[i], "...") && i > 0 && (isWord(toks[i - 1], "typename") || isWord(toks[i - 1], "class")))
            any = true;
    if (!any) return toks;
    std::vector<Token> out;
    out.reserve(toks.size());
    std::size_t i = 0;
    while (i < toks.size()) {
        if (!(isWord(toks[i], "template") && i + 1 < toks.size() && isPunct(toks[i + 1], "<"))) {
            out.push_back(toks[i++]);
            continue;
        }
        std::size_t gt = angleEnd(toks, i + 1);
        std::set<std::string> packs;
        for (std::size_t k = i + 2; gt && k + 1 < gt; ++k)
            if (isPunct(toks[k], "...") && toks[k + 1].kind == Tok::Ident) packs.insert(toks[k + 1].text);
        bool isClass = gt && gt + 1 < toks.size() &&
                       (isWord(toks[gt + 1], "class") || isWord(toks[gt + 1], "struct") || isWord(toks[gt + 1], "union"));
        if (packs.empty() || isClass || gt == 0) {
            out.push_back(toks[i++]);
            continue;
        }
        std::size_t end = declEnd(toks, gt + 1);
        std::vector<Token> decl(toks.begin() + static_cast<long>(i), toks.begin() + static_cast<long>(end));
        for (int k = kMaxPack; k >= 0; --k) {
            Expander ex(packs, k);
            std::vector<Token> copy = ex.run(decl);
            out.insert(out.end(), copy.begin(), copy.end());
        }
        i = end;
    }
    return out;
}

}  // namespace compiler
}  // namespace okaycc
