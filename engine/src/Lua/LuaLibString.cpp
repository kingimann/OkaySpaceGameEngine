// okay::lua string library and pattern matching.
//
// Written from the Lua 5.4 Reference Manual, section 6.4 (string
// manipulation), 6.4.1 (patterns) and 6.4.2 (format strings for pack and
// unpack), in our own design and words. The pattern code (PatternBuilder,
// PatternWalker) first turns a pattern into a step list and then walks it
// with backtracking; see the comment above PatternBuilder. All its work is
// charged to the run budget, so a hostile pattern can not hang the game.
// Character classes are ASCII only (independent of the C locale). The pack
// functions use this build's type sizes (C int 4, long / lua_Integer /
// size_t 8, float 4, double / lua_Number 8, native order little-endian,
// native maximum alignment 8). string.dump is not provided (no binary chunks).
// Error messages are our own wording.
#include "LuaCode.hpp"
#include <algorithm>
#include <cmath>
#include <vector>
#include <cstdio>
#include <cstring>

namespace okay::lua {

namespace {

constexpr std::size_t kMaxStringResult = std::size_t(1) << 30;

// The run budget for byte work: one unit per 16 bytes read, written or compared
// (plus one), so a loop over a big string pays for the bytes, not only the call.
void ChargeBytes(State& L, std::size_t bytes) { L.Charge(bytes / 16 + 1); }

// ---------------------------------------------------------------- ASCII classes
bool IsAlpha(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
bool IsDigit(int c) { return c >= '0' && c <= '9'; }
bool IsLower(int c) { return c >= 'a' && c <= 'z'; }
bool IsUpper(int c) { return c >= 'A' && c <= 'Z'; }
bool IsSpace(int c) { return c == ' ' || (c >= '\t' && c <= '\r'); }
bool IsCntrl(int c) { return (c >= 0 && c < 32) || c == 127; }
bool IsPunct(int c) { return c > 32 && c < 127 && !IsAlpha(c) && !IsDigit(c); }
bool IsXDigit(int c) { return IsDigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
bool IsGraph(int c) { return c > 32 && c < 127; }
int ToLower(int c) { return IsUpper(c) ? c + 32 : c; }
int ToUpper(int c) { return IsLower(c) ? c - 32 : c; }

// ---------------------------------------------------------------- patterns (manual 6.4.1)
//
// Two stages. PatternBuilder reads the pattern text once and turns it into a
// flat list of PatStep records (a literal byte, any byte, a %class, a [set],
// %b, %f, a %n back reference, the capture marks and the '$' end mark), each
// with its repeat mark. Sets become a run of SetPart records (one byte, a
// byte range or a %class). All mistakes in the pattern are reported here,
// before any matching starts. PatternWalker then runs the step list against
// the subject with backtracking only where a repeat mark leaves a choice.
//
// Every unit of work is metered and charged to the run budget: one unit per
// step taken, per subject byte tested, per set part looked at, per byte a
// back reference compares and per byte a %b scan passes. A hostile pattern
// therefore stops with the budget error instead of freezing a frame.

constexpr int kMaxPatternCaptures = 32;   ///< the most captures one pattern may hold
constexpr int kMaxChoiceNesting = 200;    ///< the deepest chain of open repeat choices
constexpr std::size_t kNoMatch = static_cast<std::size_t>(-1);

enum class StepKind : unsigned char {
    Byte,         ///< one literal byte (`lit`)
    AnyByte,      ///< '.'
    Class,        ///< %a, %d, ... (`lit` is the class letter)
    Set,          ///< [...] (`firstPart`, `partCount`, `negated`)
    Balanced,     ///< %bxy (`lit` = x, `lit2` = y)
    Frontier,     ///< %f[...] (set fields as for Set)
    SameAs,       ///< %1 .. %9 (`index`)
    CaptureOpen,  ///< '(' (`index`)
    CapturePos,   ///< '()' (`index`)
    CaptureClose, ///< ')' (`index` of the capture it closes)
    SubjectEnd,   ///< '$' as the last pattern byte
};

enum class Repeat : unsigned char { One, Any, AtLeastOne, AnyFewest, Maybe };

struct SetPart {
    unsigned char from = 0, to = 0;   ///< a byte range (a single byte has from == to)
    unsigned char classLetter = 0;    ///< nonzero: a %class instead of a range
};

struct PatStep {
    StepKind kind = StepKind::Byte;
    Repeat repeat = Repeat::One;
    bool negated = false;
    unsigned char lit = 0, lit2 = 0;
    int index = 0;
    std::uint32_t firstPart = 0, partCount = 0;
};

struct CompiledPattern {
    std::vector<PatStep> steps;
    std::vector<SetPart> parts;
    bool anchored = false;   ///< a leading '^' (find, match, gsub only)
    int captureCount = 0;
};

// The class letters of manual 6.4.1; an upper-case letter is the complement.
bool IsClassLetter(unsigned char c) {
    switch (ToLower(c)) {
        case 'a': case 'c': case 'd': case 'g': case 'l': case 'p': case 's': case 'u': case 'w': case 'x': return true;
        default: return false;
    }
}

bool InClass(unsigned char letter, int c) {
    bool yes = false;
    switch (ToLower(letter)) {
        case 'a': yes = IsAlpha(c); break;
        case 'c': yes = IsCntrl(c); break;
        case 'd': yes = IsDigit(c); break;
        case 'g': yes = IsGraph(c); break;
        case 'l': yes = IsLower(c); break;
        case 'p': yes = IsPunct(c); break;
        case 's': yes = IsSpace(c); break;
        case 'u': yes = IsUpper(c); break;
        case 'w': yes = IsAlpha(c) || IsDigit(c); break;
        case 'x': yes = IsXDigit(c); break;
        default: break;
    }
    return IsUpper(letter) ? !yes : yes;
}

class PatternBuilder {
public:
    PatternBuilder(State& L, const std::string& text, CompiledPattern& out) : L_(L), text_(text), out_(out) {}

    void Build(bool caretAnchors) {
        L_.Charge(text_.size() / 4 + 1);
        std::size_t at = 0;
        if (caretAnchors && !text_.empty() && text_[0] == '^') { out_.anchored = true; at = 1; }
        while (at < text_.size()) at = ReadOne(at);
        if (!openCaptures_.empty()) Bad("a '(' in the pattern has no matching ')'");
    }

private:
    [[noreturn]] void Bad(const std::string& why) { L_.Error("bad pattern: " + why); }
    unsigned char At(std::size_t i) const { return (unsigned char)text_[i]; }
    bool Has(std::size_t i) const { return i < text_.size(); }

    // one pattern item starting at text_[at]; returns where the next one starts
    std::size_t ReadOne(std::size_t at) {
        PatStep st;
        const unsigned char c = At(at);
        if (c == '(') {
            if (out_.captureCount >= kMaxPatternCaptures)
                Bad("more than " + std::to_string(kMaxPatternCaptures) + " captures");
            st.index = out_.captureCount++;
            finished_.push_back(false);
            if (Has(at + 1) && At(at + 1) == ')') {
                st.kind = StepKind::CapturePos;
                finished_.back() = true;
                out_.steps.push_back(st);
                return at + 2;
            }
            st.kind = StepKind::CaptureOpen;
            openCaptures_.push_back(st.index);
            out_.steps.push_back(st);
            return at + 1;
        }
        if (c == ')') {
            if (openCaptures_.empty()) Bad("a ')' with no '(' before it");
            st.kind = StepKind::CaptureClose;
            st.index = openCaptures_.back();
            openCaptures_.pop_back();
            finished_[(std::size_t)st.index] = true;
            out_.steps.push_back(st);
            return at + 1;
        }
        if (c == '$' && at + 1 == text_.size()) {
            st.kind = StepKind::SubjectEnd;
            out_.steps.push_back(st);
            return at + 1;
        }
        if (c == '%' && Has(at + 1)) {
            const unsigned char d = At(at + 1);
            if (d == 'b') {
                if (!Has(at + 3)) Bad("'%b' needs two characters after it");
                st.kind = StepKind::Balanced;
                st.lit = At(at + 2);
                st.lit2 = At(at + 3);
                out_.steps.push_back(st);
                return at + 4;
            }
            if (d == 'f') {
                if (!Has(at + 2) || At(at + 2) != '[') Bad("'%f' must be followed by a set in '[ ]'");
                st.kind = StepKind::Frontier;
                std::size_t next = ReadSet(at + 2, st);
                out_.steps.push_back(st);
                return next;
            }
            if (IsDigit(d)) {
                const int want = d - '1';
                if (d == '0' || want >= out_.captureCount || !finished_[(std::size_t)want])
                    Bad("'%" + std::string(1, (char)d) + "' names a capture that is not complete at that point");
                st.kind = StepKind::SameAs;
                st.index = want;
                out_.steps.push_back(st);
                return at + 2;
            }
        }
        std::size_t next = ReadSingle(at, st);
        if (Has(next)) {
            switch (At(next)) {
                case '*': st.repeat = Repeat::Any; ++next; break;
                case '+': st.repeat = Repeat::AtLeastOne; ++next; break;
                case '-': st.repeat = Repeat::AnyFewest; ++next; break;
                case '?': st.repeat = Repeat::Maybe; ++next; break;
                default: break;
            }
        }
        out_.steps.push_back(st);
        return next;
    }

    // a single character class (manual 6.4.1): x, '.', %x, [set]
    std::size_t ReadSingle(std::size_t at, PatStep& st) {
        const unsigned char c = At(at);
        if (c == '.') { st.kind = StepKind::AnyByte; return at + 1; }
        if (c == '[') { st.kind = StepKind::Set; return ReadSet(at, st); }
        if (c == '%') {
            if (!Has(at + 1)) Bad("it ends with a '%' that escapes nothing");
            const unsigned char d = At(at + 1);
            st.kind = IsClassLetter(d) ? StepKind::Class : StepKind::Byte;
            st.lit = d;
            return at + 2;
        }
        st.kind = StepKind::Byte;
        st.lit = c;
        return at + 1;
    }

    // text_[at] is '['; fills the set fields of st, returns the index after the closing ']'
    std::size_t ReadSet(std::size_t at, PatStep& st) {
        ++at;
        if (Has(at) && At(at) == '^') { st.negated = true; ++at; }
        st.firstPart = (std::uint32_t)out_.parts.size();
        bool first = true;   // a ']' right after '[' or '[^' is a member, not the end
        for (;;) {
            if (!Has(at)) Bad("a '[' set has no closing ']'");
            const unsigned char c = At(at);
            if (c == ']' && !first) { ++at; break; }
            first = false;
            SetPart part;
            if (c == '%') {
                if (!Has(at + 1)) Bad("a '[' set has no closing ']'");
                const unsigned char d = At(at + 1);
                if (IsClassLetter(d)) part.classLetter = d;
                else part.from = part.to = d;
                at += 2;
            } else if (Has(at + 2) && At(at + 1) == '-' && At(at + 2) != ']' && At(at + 2) != '%') {
                part.from = c;
                part.to = At(at + 2);
                at += 3;
            } else {
                part.from = part.to = c;
                at += 1;
            }
            out_.parts.push_back(part);
        }
        st.partCount = (std::uint32_t)out_.parts.size() - st.firstPart;
        return at;
    }

    State& L_;
    const std::string& text_;
    CompiledPattern& out_;
    std::vector<int> openCaptures_;   ///< indexes of '(' still waiting for their ')'
    std::vector<bool> finished_;      ///< per capture: its ')' (or '()') has been read
};

void CompilePattern(State& L, const std::string& text, bool caretAnchors, CompiledPattern& out) {
    PatternBuilder(L, text, out).Build(caretAnchors);
}

class PatternWalker {
public:
    PatternWalker(State& L, const CompiledPattern& pat, const char* subject, std::size_t size)
        : L_(L), pat_(pat), subj_(subject), size_(size) {}
    ~PatternWalker() = default;

    /// Try a match starting exactly at `start`; the end of the match, or kNoMatch.
    std::size_t TryAt(std::size_t start) {
        nesting_ = 0;
        std::size_t end = Walk(0, start);
        Settle();
        return end;
    }

    /// Settle the metered work with the run budget.
    void Settle() {
        if (pending_) { L_.Charge(pending_); pending_ = 0; }
    }

    int CaptureCount() const { return pat_.captureCount; }

    /// Capture `i` (0-based) of the match [from, to) as a Lua value. With no
    /// captures in the pattern, capture 0 is the whole match.
    Value CaptureValue(int i, std::size_t from, std::size_t to) {
        if (pat_.captureCount == 0) {
            if (i != 0) L_.Error("capture %" + std::to_string(i + 1) + " does not exist (the pattern has no captures)");
            return Value::Obj(L_.NewString(subj_ + from, to - from));
        }
        if (i < 0 || i >= pat_.captureCount)
            L_.Error("capture %" + std::to_string(i + 1) + " does not exist (the pattern has " +
                     std::to_string(pat_.captureCount) + ")");
        const Slot& s = slots_[i];
        if (s.isPosition) return Value::Int((std::int64_t)s.start + 1);
        return Value::Obj(L_.NewString(subj_ + s.start, s.length));
    }

    /// Push the captures of the match [from, to); the whole match when there are none and `wholeIfNone`.
    int PushCaptures(std::size_t from, std::size_t to, bool wholeIfNone) {
        const int count = pat_.captureCount == 0 ? (wholeIfNone ? 1 : 0) : pat_.captureCount;
        L_.CheckStack(count);
        for (int i = 0; i < count; ++i) L_.Push(CaptureValue(i, from, to));
        return count;
    }

private:
    struct Slot {
        std::size_t start = 0, length = 0;
        bool isPosition = false;
    };

    void Meter(std::uint64_t units) {
        pending_ += units;
        if (pending_ >= 4096) Settle();
    }

    bool SetHas(const PatStep& st, int c) {
        const SetPart* part = pat_.parts.data() + st.firstPart;
        bool hit = false;
        std::uint32_t looked = 0;
        while (looked < st.partCount) {
            const SetPart& p = part[looked++];
            if (p.classLetter ? InClass(p.classLetter, c) : (c >= p.from && c <= p.to)) { hit = true; break; }
        }
        Meter(looked);
        return hit != st.negated;
    }

    // does the single-byte step st accept the subject byte at `at`?
    bool Accepts(const PatStep& st, std::size_t at) {
        Meter(1);
        if (at >= size_) return false;
        const unsigned char c = (unsigned char)subj_[at];
        switch (st.kind) {
            case StepKind::Byte: return c == st.lit;
            case StepKind::AnyByte: return true;
            case StepKind::Class: return InClass(st.lit, c);
            case StepKind::Set: return SetHas(st, c);
            default: return false;
        }
    }

    std::size_t BalancedEnd(const PatStep& st, std::size_t at) {
        if (at >= size_ || (unsigned char)subj_[at] != st.lit) return kNoMatch;
        std::size_t depth = 1;
        for (std::size_t k = at + 1; k < size_; ++k) {
            Meter(1);
            const unsigned char c = (unsigned char)subj_[k];
            if (c == st.lit2) {
                if (--depth == 0) return k + 1;
            } else if (c == st.lit) {
                ++depth;
            }
        }
        return kNoMatch;
    }

    // run the rest of the pattern from step i at subject position `at`, as a new choice point
    std::size_t Choice(std::size_t i, std::size_t at) {
        if (++nesting_ > kMaxChoiceNesting) L_.Error("pattern too deeply nested to match (too many open repeats)");
        std::size_t r = Walk(i, at);
        --nesting_;
        return r;
    }

    std::size_t Walk(std::size_t i, std::size_t at) {
        const std::size_t count = pat_.steps.size();
        for (;;) {
            Meter(1);
            if (i == count) return at;
            const PatStep& st = pat_.steps[i];
            switch (st.kind) {
                case StepKind::CaptureOpen:
                    slots_[st.index] = Slot{at, 0, false};
                    ++i;
                    continue;
                case StepKind::CapturePos:
                    slots_[st.index] = Slot{at, 0, true};
                    ++i;
                    continue;
                case StepKind::CaptureClose:
                    slots_[st.index].length = at - slots_[st.index].start;
                    ++i;
                    continue;
                case StepKind::SubjectEnd:
                    if (at != size_) return kNoMatch;
                    ++i;
                    continue;
                case StepKind::Balanced:
                    at = BalancedEnd(st, at);
                    if (at == kNoMatch) return kNoMatch;
                    ++i;
                    continue;
                case StepKind::Frontier: {
                    const int before = at == 0 ? 0 : (unsigned char)subj_[at - 1];
                    const int after = at < size_ ? (unsigned char)subj_[at] : 0;
                    if (SetHas(st, before) || !SetHas(st, after)) return kNoMatch;
                    ++i;
                    continue;
                }
                case StepKind::SameAs: {
                    const Slot& s = slots_[st.index];
                    if (s.isPosition || size_ - at < s.length) return kNoMatch;
                    Meter(s.length);
                    if (s.length && std::memcmp(subj_ + s.start, subj_ + at, s.length) != 0) return kNoMatch;
                    at += s.length;
                    ++i;
                    continue;
                }
                default: break;
            }
            switch (st.repeat) {
                case Repeat::One:
                    if (!Accepts(st, at)) return kNoMatch;
                    ++at;
                    ++i;
                    continue;
                case Repeat::Maybe:
                    if (Accepts(st, at)) {
                        std::size_t r = Choice(i + 1, at + 1);
                        if (r != kNoMatch) return r;
                    }
                    ++i;
                    continue;
                case Repeat::Any:
                case Repeat::AtLeastOne: {
                    std::size_t run = 0;
                    while (Accepts(st, at + run)) ++run;
                    const std::size_t fewest = st.repeat == Repeat::AtLeastOne ? 1 : 0;
                    if (run < fewest) return kNoMatch;
                    if (i + 1 == count) return at + run;   // nothing after it: take the longest run
                    for (std::size_t take = run + 1; take-- > fewest;) {
                        std::size_t r = Choice(i + 1, at + take);
                        if (r != kNoMatch) return r;
                    }
                    return kNoMatch;
                }
                case Repeat::AnyFewest:
                    for (;;) {
                        std::size_t r = Choice(i + 1, at);
                        if (r != kNoMatch) return r;
                        if (!Accepts(st, at)) return kNoMatch;
                        ++at;
                    }
            }
        }
    }

    State& L_;
    const CompiledPattern& pat_;
    const char* subj_;
    std::size_t size_;
    Slot slots_[kMaxPatternCaptures];
    std::uint64_t pending_ = 0;
    int nesting_ = 0;
};
std::int64_t PosRelStart(std::int64_t pos, std::size_t len) {
    if (pos > 0) return pos;
    if (pos == 0) return 1;
    if (pos < -(std::int64_t)len) return 1;
    return (std::int64_t)len + pos + 1;
}
std::int64_t PosRelEnd(std::int64_t pos, std::size_t len) {
    if (pos > (std::int64_t)len) return (std::int64_t)len;
    if (pos >= 0) return pos;
    if (pos < -(std::int64_t)len) return 0;
    return (std::int64_t)len + pos + 1;
}

bool HasSpecials(const std::string& p) {
    return p.find_first_of("^$*+?.([%-") != std::string::npos;
}

// ---------------------------------------------------------------- functions
int S_len(State& L, int n) {
    (void)n;
    L.Push(Value::Int((std::int64_t)L.CheckStr(0, "len")->s.size()));
    return 1;
}

int S_sub(State& L, int n) {
    LString* s = L.CheckStr(0, "sub");
    std::size_t len = s->s.size();
    std::int64_t i = PosRelStart(L.OptInt(1, "sub", 1), len);
    std::int64_t j = PosRelEnd(n >= 3 ? L.OptInt(2, "sub", -1) : -1, len);
    if (i > j) { L.PushString(""); return 1; }
    ChargeBytes(L, (std::size_t)(j - i + 1));
    L.Push(Value::Obj(L.NewString(s->s.data() + i - 1, (std::size_t)(j - i + 1))));
    return 1;
}

int S_upper(State& L, int n) {
    (void)n;
    std::string s = L.CheckStr(0, "upper")->s;
    ChargeBytes(L, s.size());
    for (char& c : s) c = (char)ToUpper((unsigned char)c);
    L.PushString(s);
    return 1;
}

int S_lower(State& L, int n) {
    (void)n;
    std::string s = L.CheckStr(0, "lower")->s;
    ChargeBytes(L, s.size());
    for (char& c : s) c = (char)ToLower((unsigned char)c);
    L.PushString(s);
    return 1;
}

int S_reverse(State& L, int n) {
    (void)n;
    std::string s = L.CheckStr(0, "reverse")->s;
    ChargeBytes(L, s.size());
    std::reverse(s.begin(), s.end());
    L.PushString(s);
    return 1;
}

int S_rep(State& L, int n) {
    LString* s = L.CheckStr(0, "rep");
    std::int64_t count = L.CheckInt(1, "rep");
    std::string sep = n >= 3 && !L.Arg(2).IsNil() ? L.CheckStr(2, "rep")->s : std::string();
    if (count <= 0) { L.PushString(""); return 1; }
    // The work is one step per copy plus the bytes written. Charge it before doing it, so a
    // huge count of an empty string ends with the budget error instead of a long empty loop.
    const std::uint64_t copies = (std::uint64_t)count;
    const std::uint64_t piece = (std::uint64_t)s->s.size() + sep.size();
    if (piece != 0 && copies > kMaxStringResult / piece)
        L.Error("string.rep: the result would be longer than " + std::to_string(kMaxStringResult) + " bytes");
    L.Charge(std::min<std::uint64_t>(copies, kMaxStringResult) / 8 + 1);
    const std::size_t total = (std::size_t)(piece * copies);
    L.Charge(total / 64);
    if (total > L.memoryLimit) L.Error("not enough memory");
    std::string out;
    if (piece == 0) { L.PushString(out); return 1; }
    out.reserve(total);
    for (std::uint64_t k = 0; k < copies; ++k) {
        if (k) out += sep;
        out += s->s;
    }
    L.PushString(out);
    return 1;
}

int S_byte(State& L, int n) {
    LString* s = L.CheckStr(0, "byte");
    std::size_t len = s->s.size();
    std::int64_t i = PosRelStart(L.OptInt(1, "byte", 1), len);
    std::int64_t j = PosRelEnd(n >= 3 ? L.OptInt(2, "byte", i) : i, len);
    if (i > j) return 0;
    if (j - i >= 1000000) L.Error("string.byte: asked for more than a million bytes at once");
    int cnt = (int)(j - i + 1);
    L.Charge((std::uint64_t)cnt);   // one unit per value pushed
    L.CheckStack(cnt);
    for (int k = 0; k < cnt; ++k) L.Push(Value::Int((unsigned char)s->s[(std::size_t)(i - 1 + k)]));
    return cnt;
}

int S_char(State& L, int n) {
    std::string out;
    for (int i = 0; i < n; ++i) {
        std::int64_t c = L.CheckInt(i, "char");
        if (c < 0 || c > 255) L.ArgError(i, "char", "byte value must be from 0 to 255");
        out += (char)c;
    }
    L.PushString(out);
    return 1;
}

// Find `needle` in `hay` from `from` on; kNoMatch when it is not there. Each place where the
// first byte matches costs the bytes compared there, so a needle that almost matches
// everywhere pays for all the comparing instead of hanging inside one call.
std::size_t PlainFind(State& L, const std::string& hay, const std::string& needle, std::size_t from) {
    const std::size_t m = needle.size();
    if (m == 0) return from <= hay.size() ? from : kNoMatch;
    if (from >= hay.size() || hay.size() - from < m) { ChargeBytes(L, 0); return kNoMatch; }
    const char* base = hay.data();
    const char* p = base + from;
    const char* last = base + (hay.size() - m);   // the last place a match can start
    std::size_t scanned = 0;
    while (p <= last) {
        const void* hit = std::memchr(p, needle[0], (std::size_t)(last - p) + 1);
        if (!hit) break;
        const char* h = static_cast<const char*>(hit);
        scanned += (std::size_t)(h - p);
        L.Charge(m / 16 + 1);
        if (std::memcmp(h, needle.data(), m) == 0) { ChargeBytes(L, scanned); return (std::size_t)(h - base); }
        p = h + 1;
    }
    ChargeBytes(L, scanned + (std::size_t)(p <= last ? last - p + 1 : 0));
    return kNoMatch;
}

// string.find and string.match (manual 6.4): the same search, different results.
int SearchCommon(State& L, int n, bool wantPositions) {
    const char* fname = wantPositions ? "find" : "match";
    LString* subject = L.CheckStr(0, fname);
    LString* pattern = L.CheckStr(1, fname);
    const std::size_t size = subject->s.size();
    const std::int64_t init = PosRelStart(L.OptInt(2, fname, 1), size);
    if (init > (std::int64_t)size + 1) { L.PushNil(); return 1; }
    const bool plain = wantPositions && n >= 4 && L.Arg(3).Truthy();
    if (wantPositions && (plain || !HasSpecials(pattern->s))) {
        // a plain substring search
        const std::size_t at = PlainFind(L, subject->s, pattern->s, (std::size_t)(init - 1));
        if (at == kNoMatch) { L.PushNil(); return 1; }
        L.Push(Value::Int((std::int64_t)at + 1));
        L.Push(Value::Int((std::int64_t)(at + pattern->s.size())));
        return 2;
    }
    CompiledPattern compiled;
    CompilePattern(L, pattern->s, true, compiled);
    PatternWalker walker(L, compiled, subject->s.data(), size);
    for (std::size_t start = (std::size_t)(init - 1); start <= size; ++start) {
        const std::size_t end = walker.TryAt(start);
        if (end != kNoMatch) {
            if (!wantPositions) return walker.PushCaptures(start, end, true);
            L.Push(Value::Int((std::int64_t)start + 1));
            L.Push(Value::Int((std::int64_t)end));
            return 2 + walker.PushCaptures(start, end, false);
        }
        if (compiled.anchored) break;
    }
    L.PushNil();
    return 1;
}
int S_find(State& L, int n) { return SearchCommon(L, n, true); }
int S_match(State& L, int n) { return SearchCommon(L, n, false); }

// The gmatch iterator. Its upvalues: the subject, the pattern, the next start
// (0-based) and where the last match ended (-1 before the first), so an empty
// match right where the previous one ended is skipped.
int GmatchStep(State& L, int n) {
    (void)n;
    NClosure* self = L.Current()->frames.back().ncl;
    LString* subject = self->up[0].Str();
    LString* pattern = self->up[1].Str();
    CompiledPattern compiled;
    CompilePattern(L, pattern->s, false, compiled);   // '^' is an ordinary byte here (manual 6.4)
    const std::size_t size = subject->s.size();
    PatternWalker walker(L, compiled, subject->s.data(), size);
    const std::int64_t previousEnd = self->up[3].i;
    for (std::size_t start = (std::size_t)self->up[2].i; start <= size; ++start) {
        const std::size_t end = walker.TryAt(start);
        if (end == kNoMatch || (std::int64_t)end == previousEnd) continue;
        self->up[2] = Value::Int((std::int64_t)end);
        self->up[3] = Value::Int((std::int64_t)end);
        return walker.PushCaptures(start, end, true);
    }
    self->up[2] = Value::Int((std::int64_t)size + 1);
    return 0;
}

int S_gmatch(State& L, int n) {
    LString* subject = L.CheckStr(0, "gmatch");
    LString* pattern = L.CheckStr(1, "gmatch");
    const std::size_t size = subject->s.size();
    std::int64_t init = PosRelStart(n >= 3 ? L.OptInt(2, "gmatch", 1) : 1, size);
    if (init > (std::int64_t)size + 1) init = (std::int64_t)size + 1;
    NClosure* c = L.NewNative(GmatchStep, "gmatch_iterator",
                              {Value::Obj(subject), Value::Obj(pattern), Value::Int(init - 1), Value::Int(-1)});
    L.Push(Value::Obj(c));
    return 1;
}

void CheckGsubSize(State& L, const std::string& out) {
    if (out.size() > kMaxStringResult) L.Error("gsub: the result would be longer than " + std::to_string(kMaxStringResult) + " bytes");
}

void AppendValueText(State& L, std::string& out, const Value& v) {
    if (v.IsString()) {
        ChargeBytes(L, v.Str()->s.size());
        out += v.Str()->s;
    } else {
        out += State::NumberToString(v);
    }
    CheckGsubSize(L, out);
}

// Add the replacement for the match [from, to) to `out` (manual 6.4, string.gsub).
void Replace(State& L, PatternWalker& walker, std::string& out, const char* subject, std::size_t from,
             std::size_t to, const Value& repl) {
    if (repl.IsString() || repl.IsNumber()) {
        const std::string text = repl.IsString() ? repl.Str()->s : State::NumberToString(repl);
        L.Charge(text.size() / 16 + 1);
        for (std::size_t i = 0; i < text.size(); ++i) {
            if (text[i] != '%') { out += text[i]; continue; }
            if (++i >= text.size() || (text[i] != '%' && !IsDigit((unsigned char)text[i])))
                L.Error("gsub: in the replacement string a '%' must be followed by a digit or another '%'");
            if (text[i] == '%') { out += '%'; continue; }
            if (text[i] == '0') {
                ChargeBytes(L, to - from);
                out.append(subject + from, to - from);
                CheckGsubSize(L, out);
            }
            else AppendValueText(L, out, walker.CaptureValue(text[i] - '1', from, to));
        }
        return;
    }
    Value got;
    if (repl.IsTable()) {
        Value key = walker.CaptureValue(0, from, to);
        L.Push(key);   // keep it reachable during the lookup
        got = L.Index(repl, key);
        L.Pop();
    } else {
        const int func = L.Current()->top;
        L.Push(repl);
        walker.PushCaptures(from, to, true);
        L.CallAt(func, 1);
        got = L.Pop();
    }
    if (!got.Truthy()) { ChargeBytes(L, to - from); out.append(subject + from, to - from); return; }
    if (!got.IsString() && !got.IsNumber())
        L.Error(std::string("gsub: the replacement table or function gave a ") + L.TypeName(got) +
                ", but only a string, a number, false or nil can be used");
    AppendValueText(L, out, got);
}

int S_gsub(State& L, int n) {
    LString* subject = L.CheckStr(0, "gsub");
    LString* pattern = L.CheckStr(1, "gsub");
    const Value repl = L.Arg(2);
    if (!(repl.IsString() || repl.IsNumber() || repl.IsTable() || repl.IsFunction()))
        L.TypeError(2, "gsub", "string/function/table");
    const std::size_t size = subject->s.size();
    const std::int64_t limit = n >= 4 && !L.Arg(3).IsNil() ? L.CheckInt(3, "gsub") : (std::int64_t)size + 1;
    CompiledPattern compiled;
    CompilePattern(L, pattern->s, true, compiled);
    const char* text = subject->s.data();
    PatternWalker walker(L, compiled, text, size);
    std::string out;
    std::size_t at = 0;
    std::int64_t done = 0;
    while (done < limit) {
        const std::size_t end = walker.TryAt(at);
        if (end != kNoMatch) {
            ++done;
            Replace(L, walker, out, text, at, end, repl);
        }
        if (end != kNoMatch && end > at) at = end;
        else if (at < size) out += text[at++];
        else break;
        if (compiled.anchored) break;
    }
    if (at < size) out.append(text + at, size - at);
    ChargeBytes(L, out.size());
    L.PushString(out);
    L.Push(Value::Int(done));
    return 2;
}

// ---------------------------------------------------------------- format
void AddQuoted(State& L, std::string& out, const Value& v) {
    if (v.IsString()) {
        const std::string& s = v.Str()->s;
        out += '"';
        for (std::size_t i = 0; i < s.size(); ++i) {
            unsigned char c = (unsigned char)s[i];
            if (c == '"' || c == '\\' || c == '\n') { out += '\\'; out += (char)c; }
            else if (c == '\r') out += "\\r";
            else if (c == 0 || IsCntrl(c)) {
                char buf[8];
                bool nextDigit = i + 1 < s.size() && IsDigit((unsigned char)s[i + 1]);
                std::snprintf(buf, sizeof buf, nextDigit ? "\\%03d" : "\\%d", c);
                out += buf;
            } else out += (char)c;
        }
        out += '"';
        return;
    }
    if (v.t == Type::Int) {
        char buf[32];
        if (v.i == INT64_MIN) std::snprintf(buf, sizeof buf, "0x%llx", (unsigned long long)v.i);
        else std::snprintf(buf, sizeof buf, "%lld", (long long)v.i);
        out += buf;
        return;
    }
    if (v.t == Type::Float) {
        if (std::isinf(v.n)) { out += v.n > 0 ? "1e9999" : "-1e9999"; return; }
        if (std::isnan(v.n)) { out += "(0/0)"; return; }
        char buf[64];
        if (v.n == std::floor(v.n) && std::fabs(v.n) < 1e15) std::snprintf(buf, sizeof buf, "%.1f", v.n);
        else std::snprintf(buf, sizeof buf, "%a", v.n);
        out += buf;
        return;
    }
    if (v.IsNil() || v.t == Type::Bool) { out += L.ToStringBasic(v); return; }
    L.Error(std::string("%q can not write a ") + L.TypeName(v) + " as Lua source");
}

int S_format(State& L, int n) {
    const std::string fmt = L.CheckStr(0, "format")->s;
    std::string out;
    int arg = 0;
    for (std::size_t i = 0; i < fmt.size(); ++i) {
        char c = fmt[i];
        if (c != '%') { out += c; continue; }
        if (++i >= fmt.size()) L.Error("format string ends with a lone '%'");
        if (fmt[i] == '%') { out += '%'; continue; }
        // flags, width, precision (at most two digits each)
        std::string spec = "%";
        std::size_t start = i;
        while (i < fmt.size() && std::strchr("-+ #0", fmt[i]) && fmt[i]) spec += fmt[i++];
        int digits = 0;
        while (i < fmt.size() && IsDigit((unsigned char)fmt[i])) { spec += fmt[i++]; ++digits; }
        bool badWidth = digits > 2;
        bool hasPrec = false;
        if (i < fmt.size() && fmt[i] == '.') {
            hasPrec = true;
            spec += fmt[i++];
            digits = 0;
            while (i < fmt.size() && IsDigit((unsigned char)fmt[i])) { spec += fmt[i++]; ++digits; }
            if (digits > 2) badWidth = true;
        }
        if (i >= fmt.size() || badWidth || spec.size() - 1 > 6)
            L.Error("format item '%" + fmt.substr(start, std::min<std::size_t>(i - start + 1, 10)) + "' is not supported (too long or unfinished)");
        char conv = fmt[i];
        ++arg;
        if (arg >= n && conv != '%') L.ArgError(arg, "format", "no value");
        char buf[512];
        switch (conv) {
            case 'c': {
                std::int64_t v = L.CheckInt(arg, "format");
                out += (char)(unsigned char)v;
                break;
            }
            case 'd': case 'i': case 'u': {
                std::int64_t v;
                Value a = L.Arg(arg), num;
                if (!State::ToNumber(a, num)) L.TypeError(arg, "format", "number");
                if (!State::ToInteger(num, v)) L.ArgError(arg, "format", "number has no integer representation");
                std::string sp = spec + "ll" + (conv == 'u' ? 'u' : 'd');
                std::snprintf(buf, sizeof buf, sp.c_str(), (long long)v);
                out += buf;
                break;
            }
            case 'x': case 'X': case 'o': {
                std::int64_t v;
                Value a = L.Arg(arg), num;
                if (!State::ToNumber(a, num)) L.TypeError(arg, "format", "number");
                if (!State::ToInteger(num, v)) L.ArgError(arg, "format", "number has no integer representation");
                std::string sp = spec + "ll" + conv;
                std::snprintf(buf, sizeof buf, sp.c_str(), (unsigned long long)v);
                out += buf;
                break;
            }
            case 'e': case 'E': case 'f': case 'g': case 'G': case 'a': case 'A': {
                double v = L.CheckNum(arg, "format");
                std::string sp = spec + conv;
                if (conv == 'f' && std::fabs(v) >= 1e100 && !std::isinf(v)) {
                    // %f of a huge number: still bounded (99 digits of precision at most, 310 before the point)
                    std::vector<char> big(512);
                    std::snprintf(big.data(), big.size(), sp.c_str(), v);
                    out += big.data();
                } else {
                    std::snprintf(buf, sizeof buf, sp.c_str(), v);
                    out += buf;
                }
                break;
            }
            case 's': {
                std::string s = L.ToStringMeta(L.Arg(arg));
                if (spec.size() == 1) { out += s; break; }
                if (!hasPrec && s.size() >= 100) { out += s; break; }   // too long to pad: as is
                std::string sp = spec + 's';
                std::snprintf(buf, sizeof buf, sp.c_str(), s.c_str());
                out += buf;
                break;
            }
            case 'q': {
                if (spec.size() != 1) L.Error("format item '%q' takes no flags, width or precision");
                AddQuoted(L, out, L.Arg(arg));
                break;
            }
            case 'p': {
                Value a = L.Arg(arg);
                const void* ptr = a.IsGc() ? (const void*)a.o : nullptr;
                if (!ptr) { out += "(null)"; break; }
                std::snprintf(buf, sizeof buf, "%p", ptr);
                out += buf;
                break;
            }
            default:
                L.Error("format item '%" + fmt.substr(start, i - start + 1) + "' is not supported");
        }
        if (out.size() > kMaxStringResult) L.Error("the result would be longer than " + std::to_string(kMaxStringResult) + " bytes");
    }
    ChargeBytes(L, out.size());
    L.PushString(out);
    return 1;
}

// ---------------------------------------------------------------- pack / unpack (manual 6.4.2)
enum class PackKind { Int, Uint, Float, Double, Char, String, Zstr, Pad, PadAlign, None };

struct PackItem {
    PackKind kind = PackKind::None;
    int size = 0;     ///< bytes the item takes (for String: the length prefix)
    int align = 0;    ///< padding bytes before it
};

struct PackReader {
    State& L;
    const char* fname;
    const std::string& fmt;
    std::size_t pos = 0;
    bool little = true;
    int maxAlign = 1;

    bool Digit(char c) const { return c >= '0' && c <= '9'; }
    // an optional size after an option letter; `def` when there is none
    int Size(int def) {
        if (pos >= fmt.size() || !Digit(fmt[pos])) return def;
        std::int64_t n = 0;
        while (pos < fmt.size() && Digit(fmt[pos])) {
            n = n * 10 + (fmt[pos++] - '0');
            if (n > 0x7fffffff / 10) L.ArgError(0, fname, "a size in the format is too big");
        }
        return (int)n;
    }
    int IntSize(int def) {
        int n = Size(def);
        if (n < 1 || n > 16) L.Error("integer size " + std::to_string(n) + " in the format must be from 1 to 16");
        return n;
    }
    // the next option without alignment; returns false at the end of the format
    bool Option(PackItem& it) {
        for (;;) {
            if (pos >= fmt.size()) return false;
            char c = fmt[pos++];
            it = PackItem();
            switch (c) {
                case ' ': continue;
                case '<': little = true; continue;
                case '>': little = false; continue;
                case '=': little = true; continue;   // this build's native order
                case '!': {
                    int n = Size(8);
                    if (n < 1 || n > 16) L.Error("integer size " + std::to_string(n) + " in the format must be from 1 to 16");
                    maxAlign = n;
                    continue;
                }
                case 'b': it.kind = PackKind::Int; it.size = 1; return true;
                case 'B': it.kind = PackKind::Uint; it.size = 1; return true;
                case 'h': it.kind = PackKind::Int; it.size = 2; return true;
                case 'H': it.kind = PackKind::Uint; it.size = 2; return true;
                case 'l': case 'j': it.kind = PackKind::Int; it.size = 8; return true;
                case 'L': case 'J': case 'T': it.kind = PackKind::Uint; it.size = 8; return true;
                case 'i': it.kind = PackKind::Int; it.size = IntSize(4); return true;
                case 'I': it.kind = PackKind::Uint; it.size = IntSize(4); return true;
                case 'f': it.kind = PackKind::Float; it.size = 4; return true;
                case 'd': case 'n': it.kind = PackKind::Double; it.size = 8; return true;
                case 's': it.kind = PackKind::String; it.size = IntSize(8); return true;
                case 'z': it.kind = PackKind::Zstr; return true;
                case 'x': it.kind = PackKind::Pad; it.size = 1; return true;
                case 'c': {
                    if (pos >= fmt.size() || !Digit(fmt[pos])) L.Error("format option 'c' needs a size after it");
                    it.kind = PackKind::Char;
                    it.size = Size(0);
                    return true;
                }
                case 'X': it.kind = PackKind::PadAlign; return true;
                default: L.Error(std::string("unknown format option '") + c + "'");
            }
        }
    }
    // the next item with its alignment padding worked out (`total` = bytes so far)
    bool Next(PackItem& it, std::size_t total) {
        if (!Option(it)) return false;
        int align = it.size;
        if (it.kind == PackKind::PadAlign) {
            // Xop: align to op's size; op itself is not packed
            PackItem op;
            std::size_t save = pos;
            if (!Option(op) || op.kind == PackKind::Char || op.kind == PackKind::Zstr || op.kind == PackKind::PadAlign || op.size == 0)
                L.ArgError(0, fname, "format option 'X' must be followed by a fixed-size option");
            (void)save;
            align = op.size;
            it.size = 0;
        }
        const bool alignable = it.kind == PackKind::Int || it.kind == PackKind::Uint || it.kind == PackKind::Float ||
                               it.kind == PackKind::Double || it.kind == PackKind::PadAlign;
        if (alignable && align > 1) {
            if (align > maxAlign) align = maxAlign;
            if ((align & (align - 1)) != 0) L.ArgError(0, fname, "the format asks for an alignment that is not a power of two");
            it.align = (int)((std::size_t)(align - (int)(total & (std::size_t)(align - 1))) & (std::size_t)(align - 1));
        }
        return true;
    }
};

void PackInt(std::string& out, std::uint64_t v, int size, bool little, bool negative) {
    unsigned char b[16];
    for (int i = 0; i < size; ++i) b[i] = i < 8 ? (unsigned char)(v >> (8 * i)) : (negative ? 0xff : 0);
    for (int i = 0; i < size; ++i) out += (char)b[little ? i : size - 1 - i];
}

std::uint64_t ReadUInt(State& L, const char* p, int size, bool little, bool isSigned) {
    std::uint64_t v = 0;
    int limit = size < 8 ? size : 8;
    for (int i = 0; i < limit; ++i) {
        unsigned char c = (unsigned char)p[little ? i : size - 1 - i];
        v |= (std::uint64_t)c << (8 * i);
    }
    if (size < 8) {
        if (isSigned) {
            std::uint64_t signBit = std::uint64_t(1) << (size * 8 - 1);
            if (v & signBit) v |= ~((signBit << 1) - 1);
        }
    } else if (size > 8) {
        // the extra bytes must be a plain sign (or zero) extension
        unsigned char fill = (isSigned && (std::int64_t)v < 0) ? 0xff : 0;
        for (int i = 8; i < size; ++i)
            if ((unsigned char)p[little ? i : size - 1 - i] != fill)
                L.Error("a " + std::to_string(size) + "-byte integer in the data is too big for a Lua integer");
    }
    return v;
}

int S_pack(State& L, int n) {
    const std::string fmt = L.CheckStr(0, "pack")->s;
    PackReader rd{L, "pack", fmt};
    std::string out;
    int arg = 1;
    PackItem it;
    while (rd.Next(it, out.size())) {
        out.append((std::size_t)it.align, '\0');
        switch (it.kind) {
            case PackKind::Int: case PackKind::Uint: {
                std::int64_t v = L.CheckInt(arg, "pack");
                if (it.size < 8) {
                    if (it.kind == PackKind::Int) {
                        std::int64_t lim = std::int64_t(1) << (it.size * 8 - 1);
                        if (v < -lim || v >= lim) L.ArgError(arg, "pack", "integer does not fit in the size (overflow)");
                    } else if ((std::uint64_t)v >= (std::uint64_t(1) << (it.size * 8))) {
                        L.ArgError(arg, "pack", "unsigned integer does not fit in the size (overflow)");
                    }
                }
                PackInt(out, (std::uint64_t)v, it.size, rd.little, v < 0);
                ++arg;
                break;
            }
            case PackKind::Float: {
                float f = (float)L.CheckNum(arg++, "pack");
                std::uint32_t bits;
                std::memcpy(&bits, &f, 4);
                PackInt(out, bits, 4, rd.little, false);
                break;
            }
            case PackKind::Double: {
                double d = L.CheckNum(arg++, "pack");
                std::uint64_t bits;
                std::memcpy(&bits, &d, 8);
                PackInt(out, bits, 8, rd.little, false);
                break;
            }
            case PackKind::Char: {
                const std::string& str = L.CheckStr(arg, "pack")->s;
                if (str.size() > (std::size_t)it.size) L.ArgError(arg, "pack", "string is longer than the size of the 'c' option");
                out += str;
                out.append((std::size_t)it.size - str.size(), '\0');
                ++arg;
                break;
            }
            case PackKind::String: {
                const std::string& str = L.CheckStr(arg, "pack")->s;
                if (it.size < 8 && (std::uint64_t)str.size() >= (std::uint64_t(1) << (it.size * 8)))
                    L.ArgError(arg, "pack", "the string's length does not fit in the length field");
                PackInt(out, (std::uint64_t)str.size(), it.size, rd.little, false);
                out += str;
                ++arg;
                break;
            }
            case PackKind::Zstr: {
                const std::string& str = L.CheckStr(arg, "pack")->s;
                if (str.find('\0') != std::string::npos) L.ArgError(arg, "pack", "a 'z' string can not hold a zero byte");
                out += str;
                out += '\0';
                ++arg;
                break;
            }
            case PackKind::Pad: out += '\0'; break;
            case PackKind::PadAlign: case PackKind::None: break;
        }
        if (out.size() > kMaxStringResult) L.Error("the result would be longer than " + std::to_string(kMaxStringResult) + " bytes");
    }
    ChargeBytes(L, out.size());
    (void)n;
    L.Charge(out.size() / 64);
    L.PushString(out);
    return 1;
}

int S_packsize(State& L, int n) {
    (void)n;
    const std::string fmt = L.CheckStr(0, "packsize")->s;
    PackReader rd{L, "packsize", fmt};
    std::size_t total = 0;
    PackItem it;
    while (rd.Next(it, total)) {
        if (it.kind == PackKind::String || it.kind == PackKind::Zstr) L.ArgError(0, "packsize", "the format has an option with no fixed size ('s' or 'z')");
        total += (std::size_t)it.align + (std::size_t)it.size;
        if (total > 0x7fffffff) L.ArgError(0, "packsize", "the packed size would be over 2 GB");
    }
    L.Push(Value::Int((std::int64_t)total));
    return 1;
}

int S_unpack(State& L, int n) {
    const std::string fmt = L.CheckStr(0, "unpack")->s;
    LString* data = L.CheckStr(1, "unpack");
    const std::string& d = data->s;
    std::int64_t init = n >= 3 ? L.OptInt(2, "unpack", 1) : 1;
    if (init < 0) init = (std::int64_t)d.size() + init + 1;
    if (init < 1 || init - 1 > (std::int64_t)d.size()) L.ArgError(2, "unpack", "start position is outside the data");
    std::size_t pos = (std::size_t)(init - 1);
    PackReader rd{L, "unpack", fmt};
    PackItem it;
    int results = 0;
    auto need = [&](std::size_t bytes) {
        if (bytes > d.size() || pos > d.size() - bytes) L.ArgError(1, "unpack", "the data ends before the format does");
    };
    while (rd.Next(it, pos)) {
        need((std::size_t)it.align + (std::size_t)it.size);
        pos += (std::size_t)it.align;
        L.CheckStack(2);
        switch (it.kind) {
            case PackKind::Int: case PackKind::Uint: {
                std::uint64_t v = ReadUInt(L, d.data() + pos, it.size, rd.little, it.kind == PackKind::Int);
                L.Push(Value::Int((std::int64_t)v));
                ++results;
                break;
            }
            case PackKind::Float: {
                std::uint32_t bits = (std::uint32_t)ReadUInt(L, d.data() + pos, 4, rd.little, false);
                float f;
                std::memcpy(&f, &bits, 4);
                L.Push(Value::Num((double)f));
                ++results;
                break;
            }
            case PackKind::Double: {
                std::uint64_t bits = ReadUInt(L, d.data() + pos, 8, rd.little, false);
                double v;
                std::memcpy(&v, &bits, 8);
                L.Push(Value::Num(v));
                ++results;
                break;
            }
            case PackKind::Char:
                L.Push(Value::Obj(L.NewString(d.data() + pos, (std::size_t)it.size)));
                ++results;
                break;
            case PackKind::String: {
                std::uint64_t len = ReadUInt(L, d.data() + pos, it.size, rd.little, false);
                pos += (std::size_t)it.size;
                if (len > d.size() - pos) L.ArgError(1, "unpack", "the data ends before the format does");
                L.Push(Value::Obj(L.NewString(d.data() + pos, (std::size_t)len)));
                pos += (std::size_t)len;
                it.size = 0;
                ++results;
                break;
            }
            case PackKind::Zstr: {
                std::size_t end = d.find('\0', pos);
                if (end == std::string::npos) L.ArgError(1, "unpack", "a 'z' string has no zero byte at its end");
                L.Push(Value::Obj(L.NewString(d.data() + pos, end - pos)));
                pos = end + 1;
                ++results;
                break;
            }
            case PackKind::Pad: case PackKind::PadAlign: case PackKind::None: break;
        }
        pos += (std::size_t)it.size;
        if (results > 200) L.CheckStack(results);
    }
    ChargeBytes(L, pos - (std::size_t)(init - 1));   // the data read
    L.Charge((std::uint64_t)results);
    L.Push(Value::Int((std::int64_t)pos + 1));
    return results + 1;
}

} // namespace

void OpenString(State& L) {
    Table* t = L.NewTable();
    L.SetGlobal("string", Value::Obj(t));
    const struct { const char* n; NativeFn f; } fns[] = {
        {"len", S_len}, {"sub", S_sub}, {"upper", S_upper}, {"lower", S_lower}, {"reverse", S_reverse},
        {"rep", S_rep}, {"byte", S_byte}, {"char", S_char}, {"find", S_find}, {"match", S_match},
        {"gmatch", S_gmatch}, {"gsub", S_gsub}, {"format", S_format}, {"pack", S_pack},
        {"packsize", S_packsize}, {"unpack", S_unpack},
    };
    for (const auto& f : fns) L.Register(t, f.n, f.f);
    Table* mt = L.NewTable();
    mt->Set(L, Value::Obj(L.tmName[TM_INDEX]), Value::Obj(t));
    L.stringMeta = mt;
}

} // namespace okay::lua
