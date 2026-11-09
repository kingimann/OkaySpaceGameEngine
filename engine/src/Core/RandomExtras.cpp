#include "okay/Core/RandomExtras.hpp"

#include <cmath>
#include <cstdio>

namespace okay {

namespace {

constexpr int kMaxLength = 256;
constexpr int kMaxDice   = 1000;
constexpr int kMaxSides  = 100000;
constexpr int kMaxTerms  = 16;
constexpr int kMaxConst  = 1000000;

// The character at byte `i` as something a log line or a label can show: a whole UTF-8
// character when the bytes form one, otherwise \xNN (a control character, half a character,
// a stray byte). A raw byte of a multi-byte character used to go into the message alone.
std::string Show(const std::string& s, std::size_t i) {
    const unsigned char c = (unsigned char)s[i];
    if (c >= 0x20 && c < 0x7F) return std::string(1, (char)c);
    int n = 0;
    if (c >= 0xC2 && c <= 0xDF) n = 2;
    else if (c >= 0xE0 && c <= 0xEF) n = 3;
    else if (c >= 0xF0 && c <= 0xF4) n = 4;
    if (n > 0 && i + (std::size_t)n <= s.size()) {
        bool ok = true;
        for (int k = 1; k < n; ++k) if (((unsigned char)s[i + (std::size_t)k] & 0xC0) != 0x80) ok = false;
        if (ok) return s.substr(i, (std::size_t)n);
    }
    char buf[8];
    std::snprintf(buf, sizeof buf, "\\x%02X", (unsigned)c);
    return buf;
}

struct Parser {
    const std::string& s;
    std::size_t i = 0;
    DiceParse out;

    explicit Parser(const std::string& text) : s(text) {}

    bool Fail(DiceError e, std::size_t at, const std::string& msg) {
        out.ok = false;
        out.expr = DiceExpression();
        out.error = e;
        out.position = (int)at;
        out.message = msg + " at position " + std::to_string(at);
        return false;
    }
    void Ws() { while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) ++i; }
    bool Digit() const { return i < s.size() && s[i] >= '0' && s[i] <= '9'; }

    // digits -> value; false (with the error filled in) when there are none or too many.
    bool Number(long long& v, DiceError missing, const char* what) {
        if (!Digit()) return Fail(missing, i, std::string("expected ") + what);
        const std::size_t start = i;
        v = 0;
        while (Digit()) {
            if (i - start >= 9) return Fail(DiceError::NumberTooLarge, start, "number is too large");
            v = v * 10 + (s[i] - '0');
            ++i;
        }
        return true;
    }

    bool Term(int sign, std::vector<DiceTerm>& terms) {
        DiceTerm t;
        t.sign = sign;
        const std::size_t termStart = i;
        long long first = 0;
        bool haveFirst = false;
        if (Digit()) { if (!Number(first, DiceError::ExpectedNumber, "a number")) return false; haveFirst = true; }
        const bool isDice = i < s.size() && (s[i] == 'd' || s[i] == 'D');
        if (!isDice) {
            if (!haveFirst) {
                if (i >= s.size()) return Fail(DiceError::ExpectedNumber, i, "expected a number or dice after the operator");
                return Fail(DiceError::UnexpectedCharacter, i, "unexpected '" + Show(s, i) + "'");
            }
            if (first > kMaxConst) return Fail(DiceError::NumberTooLarge, termStart, "constant is too large");
            t.count = (int)first;
            t.sides = 0;
            terms.push_back(t);
            return true;
        }
        const std::size_t countAt = termStart;
        long long count = haveFirst ? first : 1;
        ++i;   // the 'd'
        long long sides = 0;
        if (!Digit()) {
            if (i >= s.size() || s[i] != '%') return Fail(DiceError::ExpectedSides, i, "expected the number of sides after 'd'");
            ++i; sides = 100;   // "d%" is d100
        } else if (!Number(sides, DiceError::ExpectedSides, "the number of sides")) {
            return false;
        }
        if (count == 0) return Fail(DiceError::ZeroDice, countAt, "can't roll zero dice");
        if (count > kMaxDice) return Fail(DiceError::TooManyDice, countAt, "too many dice (the limit is 1000)");
        if (sides == 0) return Fail(DiceError::ZeroSides, countAt, "a die needs at least 1 side");
        if (sides > kMaxSides) return Fail(DiceError::TooManySides, countAt, "too many sides (the limit is 100000)");
        t.count = (int)count;
        t.sides = (int)sides;
        if (i < s.size() && (s[i] == 'k' || s[i] == 'K')) {
            const std::size_t keepAt = i;
            ++i;
            t.keepHighest = true;
            if (i < s.size() && (s[i] == 'h' || s[i] == 'H')) { ++i; }
            else if (i < s.size() && (s[i] == 'l' || s[i] == 'L')) { t.keepHighest = false; ++i; }
            long long keep = 0;
            if (!Digit()) return Fail(DiceError::BadKeep, i, "expected how many dice to keep after 'k'");
            if (!Number(keep, DiceError::BadKeep, "a number")) return false;
            if (keep < 1 || keep > count) return Fail(DiceError::BadKeep, keepAt, "can't keep that many dice");
            t.keep = (int)keep;
        }
        terms.push_back(t);
        return true;
    }

    DiceParse Run() {
        if (s.size() > (std::size_t)kMaxLength) { Fail(DiceError::TooLong, (std::size_t)kMaxLength, "dice text is too long"); return std::move(out); }
        Ws();
        if (i >= s.size()) { Fail(DiceError::Empty, 0, "no dice given"); return std::move(out); }
        std::vector<DiceTerm> terms;
        int sign = 1;
        if (s[i] == '+' || s[i] == '-') { sign = s[i] == '-' ? -1 : 1; ++i; Ws(); }
        for (;;) {
            if ((int)terms.size() >= kMaxTerms) { Fail(DiceError::TooManyTerms, i, "too many terms (the limit is 16)"); return std::move(out); }
            if (!Term(sign, terms)) return std::move(out);
            Ws();
            if (i >= s.size()) break;
            if (s[i] != '+' && s[i] != '-') { Fail(DiceError::UnexpectedCharacter, i, "unexpected '" + Show(s, i) + "'"); return std::move(out); }
            sign = s[i] == '-' ? -1 : 1;
            ++i;
            Ws();
        }
        out.ok = true;
        out.error = DiceError::None;
        out.message.clear();
        out.position = 0;
        out.expr = DiceExpression(std::move(terms));
        return std::move(out);
    }
};

}  // namespace

DiceParse ParseDice(const std::string& text) {
    Parser p(text);
    return p.Run();
}

int DiceExpression::Min() const {
    long long v = 0;
    for (const DiceTerm& t : m_terms) {
        if (t.sides == 0) { v += (long long)t.sign * t.count; continue; }
        const long long kept = (t.keep > 0 && t.keep < t.count) ? t.keep : t.count;
        v += t.sign > 0 ? kept : -(kept * (long long)t.sides);
    }
    return (int)(v > INT_MAX ? INT_MAX : v < INT_MIN ? INT_MIN : v);
}

int DiceExpression::Max() const {
    long long v = 0;
    for (const DiceTerm& t : m_terms) {
        if (t.sides == 0) { v += (long long)t.sign * t.count; continue; }
        const long long kept = (t.keep > 0 && t.keep < t.count) ? t.keep : t.count;
        v += t.sign > 0 ? kept * (long long)t.sides : -kept;
    }
    return (int)(v > INT_MAX ? INT_MAX : v < INT_MIN ? INT_MIN : v);
}

namespace {
// Expected sum of the `keep` highest (or lowest) of `count` dice with `sides` sides.
// E[k-th smallest] = sum over x of P(at least count-k+1 dice are >= x), a binomial tail
// per x, worked out exactly. Past 4 million (count x sides) steps it falls back to the
// continuous order-statistic mean, which is close for large dice.
double KeptAverage(int count, int sides, int keep, bool high) {
    if ((long long)count * (long long)sides > 4000000LL) {
        double sum = 0.0;
        for (int k = 1; k <= count; ++k) {
            const bool counted = high ? (k > count - keep) : (k <= keep);
            if (counted) sum += (double)k / (double)(count + 1) * (double)(sides + 1);
        }
        return sum;
    }
    std::vector<double> logC((std::size_t)count + 1), pmf((std::size_t)count + 1), suf((std::size_t)count + 2);
    for (int j = 0; j <= count; ++j) logC[(std::size_t)j] = std::lgamma((double)count + 1.0) - std::lgamma((double)j + 1.0) - std::lgamma((double)(count - j) + 1.0);
    double total = 0.0;
    for (int x = 1; x <= sides; ++x) {
        const double p = (double)(sides - x + 1) / (double)sides;   // P(one die >= x)
        if (p >= 1.0) {
            for (double& v : pmf) v = 0.0;
            pmf[(std::size_t)count] = 1.0;
        } else {
            const double lp = std::log(p), lq = std::log1p(-p);
            for (int j = 0; j <= count; ++j) pmf[(std::size_t)j] = std::exp(logC[(std::size_t)j] + j * lp + (count - j) * lq);
        }
        suf[(std::size_t)count + 1] = 0.0;
        for (int j = count; j >= 0; --j) suf[(std::size_t)j] = suf[(std::size_t)j + 1] + pmf[(std::size_t)j];
        if (high) for (int m = 1; m <= keep; ++m) total += suf[(std::size_t)m];
        else      for (int m = count - keep + 1; m <= count; ++m) total += suf[(std::size_t)m];
    }
    return total;
}
}  // namespace

double DiceExpression::Average() const {
    double v = 0.0;
    for (const DiceTerm& t : m_terms) {
        if (t.sides == 0) { v += (double)t.sign * t.count; continue; }
        const bool keeps = t.keep > 0 && t.keep < t.count;
        const double e = keeps ? KeptAverage(t.count, t.sides, t.keep, t.keepHighest)
                               : (double)t.count * ((double)t.sides + 1.0) / 2.0;
        v += t.sign * e;
    }
    return v;
}

std::string DiceExpression::ToString() const {
    std::string out;
    for (std::size_t k = 0; k < m_terms.size(); ++k) {
        const DiceTerm& t = m_terms[k];
        if (t.sign < 0) out += '-';
        else if (k > 0) out += '+';
        if (t.sides == 0) { out += std::to_string(t.count); continue; }
        out += std::to_string(t.count) + "d" + std::to_string(t.sides);
        if (t.keep > 0 && t.keep < t.count) out += std::string(t.keepHighest ? "kh" : "kl") + std::to_string(t.keep);
    }
    return out;
}

}  // namespace okay
