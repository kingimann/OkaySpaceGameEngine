// Variadic function templates ([temp.variadic]) by arity, done on the token stream before parsing.
//
// Written from ISO/IEC 14882 [temp.variadic] (packs and pack expansions), [expr.sizeof] p5
// (sizeof...) and [expr.prim.fold] (fold expressions), in our own structure. A function template
// with a template parameter pack is replaced by copies of itself, one per pack length 0..kMaxPack,
// most parameters first. In the copy for length k, every pack expansion "pattern..." becomes k
// copies of the pattern (with each pack name P renamed P__pk<j>), sizeof...(P) becomes k, and a
// fold expression is written out with explicit parentheses. The copies are ordinary function
// templates of one name, which the parser already keeps as overloads and the checker chooses
// between by argument count and deduction ([temp.over]). A class template partial specialization
// with a pack is copied the same way, each copy a partial specialization of its own arity; a primary
// class template with a type pack becomes a primary with defaulted parameters and one partial
// specialization per length (expandPrimaryClass).
#include "packs.hpp"

#include <map>
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

// The parameters of the template header toks[lt+1 .. gt): for each, [start, end) of its tokens.
std::vector<std::pair<std::size_t, std::size_t>> headerParams(const std::vector<Token>& d, std::size_t lt, std::size_t gt) {
    std::vector<std::pair<std::size_t, std::size_t>> out;
    int depth = 0;
    std::size_t start = lt + 1;
    for (std::size_t k = lt + 1; k < gt; ++k) {
        if (isOpen(d[k]) || isPunct(d[k], "<")) ++depth;
        else if (isClose(d[k]) || isPunct(d[k], ">")) --depth;
        else if (isPunct(d[k], ">>")) depth -= 2;
        else if (depth == 0 && isPunct(d[k], ",")) {
            out.push_back({start, k});
            start = k + 1;
        }
    }
    if (start < gt) out.push_back({start, gt});
    return out;
}

// The name a template parameter declares: the last identifier before its '=' (or its end).
std::string paramName(const std::vector<Token>& d, std::size_t a, std::size_t b) {
    std::string name;
    for (std::size_t k = a; k < b && !isPunct(d[k], "="); ++k)
        if (d[k].kind == Tok::Ident) name = d[k].text;
    return name;
}

const char* const kPackEnd = "__okay_pack_end";

// A copy that names a primary made by expandPrimaryClass with more arguments than it has
// parameters (tuple<H, T...> for the longest pack): that pack length cannot occur, so the copy is
// left out.
bool tooLong(const std::vector<Token>& d, const std::map<std::string, std::size_t>& arity) {
    if (arity.empty()) return false;
    for (std::size_t k = 0; k + 1 < d.size(); ++k) {
        if (d[k].kind != Tok::Ident || !isPunct(d[k + 1], "<")) continue;
        auto it = arity.find(d[k].text);
        if (it == arity.end()) continue;
        std::size_t close = angleEnd(d, k + 1);
        if (close && close > k + 2 && headerParams(d, k + 1, close).size() > it->second) return true;
    }
    return false;
}

// A primary class template whose last parameter is a type pack (template <class H, class... T>
// struct tuple, [temp.variadic] p1, [temp.class]) at toks[i..end). There can be only one primary
// template of a name, so it becomes one with kMaxPack defaulted parameters in place of the pack
// (each default the type ::__okay_pack_end, a type no program names), and every pack length
// 0..kMaxPack-1 becomes a partial specialization ([temp.spec.partial]) whose missing arguments are
// those defaults: tuple<int, char> is tuple<int, char, __okay_pack_end, ...> and picks the copy of
// length 2. A pack may so hold at most kMaxPack - 1 types. A declaration without a body declares
// only the primary; the partial specializations a program writes for it are then copied per length as before. Returns false (left alone) for a
// value or template pack or a pack that is not last.
bool expandPrimaryClass(const std::vector<Token>& toks, std::size_t i, std::size_t gt, std::size_t end,
                        const std::set<std::string>& packs, std::vector<Token>& out, std::size_t& arity) {
    auto params = headerParams(toks, i + 1, gt);
    if (params.empty() || packs.size() != 1) return false;
    const auto& last = params.back();
    if (last.second - last.first != 3 || !(isWord(toks[last.first], "typename") || isWord(toks[last.first], "class")) ||
        !isPunct(toks[last.first + 1], "...") || toks[last.first + 2].kind != Tok::Ident)
        return false;
    std::string pack = toks[last.first + 2].text;
    std::size_t nameAt = gt + 2;
    if (nameAt >= end || toks[nameAt].kind != Tok::Ident) return false;
    bool hasBody = false;
    for (std::size_t k = nameAt + 1; k < end; ++k)
        if (isPunct(toks[k], "{")) {
            hasBody = true;
            break;
        }
    std::vector<std::string> fixedNames;
    for (std::size_t p = 0; p + 1 < params.size(); ++p) {
        fixedNames.push_back(paramName(toks, params[p].first, params[p].second));
        if (fixedNames.back().empty()) return false;
    }
    arity = fixedNames.size() + kMaxPack;
    const Token& at = toks[i];
    auto packName = [&](int j) { return like(at, Tok::Ident, pack + "__pk" + std::to_string(j)); };
    // The primary: the header with the pack's place taken by kMaxPack defaulted parameters.
    std::vector<Token> head(toks.begin() + static_cast<long>(i), toks.begin() + static_cast<long>(last.first));
    for (int j = 0; j < kMaxPack; ++j) {
        if (j > 0) head.push_back(like(at, Tok::Punct, ","));
        head.push_back(like(at, Tok::Keyword, "class"));
        head.push_back(packName(j));
        head.push_back(like(at, Tok::Punct, "="));
        head.push_back(like(at, Tok::Punct, "::"));
        head.push_back(like(at, Tok::Ident, kPackEnd));
    }
    out.insert(out.end(), head.begin(), head.end());
    out.push_back(toks[gt]);
    out.push_back(toks[gt + 1]);
    out.push_back(toks[nameAt]);
    if (!hasBody) {
        out.push_back(like(at, Tok::Punct, ";"));
        return true;
    }
    out.push_back(like(at, Tok::Punct, ";"));
    // One partial specialization per pack length. A pack place never takes __okay_pack_end in
    // matching or deduction (packPlaceGetsEnd), so only the copy of the right length matches.
    std::vector<Token> decl(toks.begin() + static_cast<long>(i), toks.begin() + static_cast<long>(end));
    for (int k = 0; k < kMaxPack; ++k) {
        Expander ex(packs, k);
        std::vector<Token> copy = ex.run(decl);
        // The copy's header ends at the '>' before the class key; its name follows the key.
        std::size_t cgt = angleEnd(copy, 1);
        if (cgt == 0 || cgt + 2 >= copy.size()) return false;
        // Default arguments may not appear in a partial specialization's header: drop them.
        std::vector<Token> chead;
        chead.push_back(copy[0]);
        chead.push_back(copy[1]);
        auto cparams = headerParams(copy, 1, cgt);
        for (std::size_t p = 0; p < cparams.size(); ++p) {
            if (p > 0) chead.push_back(like(at, Tok::Punct, ","));
            for (std::size_t t = cparams[p].first; t < cparams[p].second && !isPunct(copy[t], "="); ++t)
                chead.push_back(copy[t]);
        }
        chead.push_back(copy[cgt]);
        std::vector<Token> args;
        args.push_back(like(at, Tok::Punct, "<"));
        bool first = true;
        for (const std::string& n : fixedNames) {
            if (!first) args.push_back(like(at, Tok::Punct, ","));
            args.push_back(like(at, Tok::Ident, n));
            first = false;
        }
        for (int j = 0; j < k; ++j) {
            if (!first) args.push_back(like(at, Tok::Punct, ","));
            args.push_back(packName(j));
            first = false;
        }
        args.push_back(like(at, Tok::Punct, ">"));
        out.insert(out.end(), chead.begin(), chead.end());
        out.push_back(copy[cgt + 1]);
        out.push_back(copy[cgt + 2]);
        out.insert(out.end(), args.begin(), args.end());
        out.insert(out.end(), copy.begin() + static_cast<long>(cgt + 3), copy.end());
    }
    return true;
}

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
    bool sentinel = false;
    std::map<std::string, std::size_t> primaryArity;  // primaries made by expandPrimaryClass
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
        // A class template partial specialization with a pack (function<R(A...)>,
        // [temp.spec.partial]) is copied per pack length like a function template: each copy is
        // an ordinary partial specialization of its own arity. A primary class template with a
        // pack (tuple<T...>) was handled above.
        bool isPartial = isClass && gt + 3 < toks.size() && toks[gt + 2].kind == Tok::Ident &&
                         isPunct(toks[gt + 3], "<");
        if (!packs.empty() && isClass && !isPartial && gt != 0) {
            std::size_t end = declEnd(toks, gt + 1);
            std::vector<Token> made;
            std::size_t arity = 0;
            if (expandPrimaryClass(toks, i, gt, end, packs, made, arity)) {
                primaryArity[toks[gt + 2].text] = arity;
                if (!sentinel) {
                    // The type that fills an unused pack place, declared once at global scope.
                    Token at = toks[0];
                    at.file = toks[i].file;
                    std::vector<Token> decl = {like(at, Tok::Keyword, "struct"), like(at, Tok::Ident, kPackEnd),
                                               like(at, Tok::Punct, ";")};
                    out.insert(out.begin(), decl.begin(), decl.end());
                    sentinel = true;
                }
                out.insert(out.end(), made.begin(), made.end());
                i = end;
                continue;
            }
        }
        if (packs.empty() || (isClass && !isPartial) || gt == 0) {
            out.push_back(toks[i++]);
            continue;
        }
        std::size_t end = declEnd(toks, gt + 1);
        std::vector<Token> decl(toks.begin() + static_cast<long>(i), toks.begin() + static_cast<long>(end));
        for (int k = kMaxPack; k >= 0; --k) {
            Expander ex(packs, k);
            std::vector<Token> copy = ex.run(decl);
            if (tooLong(copy, primaryArity)) continue;
            out.insert(out.end(), copy.begin(), copy.end());
        }
        i = end;
    }
    return out;
}

}  // namespace compiler
}  // namespace okaycc
