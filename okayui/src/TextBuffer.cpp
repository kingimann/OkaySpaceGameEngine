// okay::text::Buffer. Own code, written from general editor design and the Unicode well-formed UTF-8 table; see
// okayui/include/okay/Text/TextBuffer.hpp and docs/own_code_editor.md.
#include "okay/Text/TextBuffer.hpp"

#include <algorithm>

namespace okay::text {

namespace {

inline bool IsCont(unsigned char c) { return (c & 0xC0) == 0x80; }

// Length of the well-formed UTF-8 sequence at p (n bytes left), or 0 if it is not well formed (Unicode table 3-7).
std::size_t SeqLen(const unsigned char* p, std::size_t n) {
    const unsigned char c = p[0];
    if (c < 0x80) return 1;
    if (c >= 0xC2 && c <= 0xDF) return (n >= 2 && IsCont(p[1])) ? 2 : 0;
    if (c >= 0xE0 && c <= 0xEF) {
        if (n < 3 || !IsCont(p[2])) return 0;
        unsigned char lo = 0x80, hi = 0xBF;
        if (c == 0xE0) lo = 0xA0;
        if (c == 0xED) hi = 0x9F;
        return (p[1] >= lo && p[1] <= hi) ? 3 : 0;
    }
    if (c >= 0xF0 && c <= 0xF4) {
        if (n < 4 || !IsCont(p[2]) || !IsCont(p[3])) return 0;
        unsigned char lo = 0x80, hi = 0xBF;
        if (c == 0xF0) lo = 0x90;
        if (c == 0xF4) hi = 0x8F;
        return (p[1] >= lo && p[1] <= hi) ? 4 : 0;
    }
    return 0;
}

// Length of the character starting at s[i] in text already known to be valid (bounded by the line end).
inline std::size_t CharLen(std::string_view s, std::size_t i) {
    const unsigned char c = (unsigned char)s[i];
    std::size_t n = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
    return std::min(n, s.size() - i);
}

inline bool Blank(char c) { return c == ' ' || c == '\t'; }
inline int Class(char c) { return Blank(c) ? 0 : Buffer::IsWordByte((unsigned char)c) ? 1 : 2; }
inline char Fold(char c) { return (c >= 'A' && c <= 'Z') ? char(c + 32) : c; }

// Knuth-Morris-Pratt: linear time whatever the needle and the text look like.
struct Matcher {
    std::string n;
    std::vector<std::size_t> fail;
    bool fold, whole;
    Matcher(std::string_view needle, FindFlags f) : fold(!Has(f, FindFlags::MatchCase)), whole(Has(f, FindFlags::WholeWord)) {
        n.assign(needle);
        if (fold) for (char& c : n) c = Fold(c);
        fail.assign(n.size(), 0);
        for (std::size_t i = 1, k = 0; i < n.size(); ++i) {
            while (k > 0 && n[i] != n[k]) k = fail[k - 1];
            if (n[i] == n[k]) ++k;
            fail[i] = k;
        }
    }
    static bool WordOk(std::string_view h, std::size_t s, std::size_t e) {
        return (s == 0 || !Buffer::IsWordByte((unsigned char)h[s - 1])) && (e >= h.size() || !Buffer::IsWordByte((unsigned char)h[e]));
    }
    // Calls cb(start) for every match (overlapping ones too) in order; cb returns true to stop.
    template <class CB> void Run(std::string_view h, CB cb) const {
        if (n.empty()) return;
        std::size_t k = 0;
        for (std::size_t i = 0; i < h.size(); ++i) {
            const char c = fold ? Fold(h[i]) : h[i];
            while (k > 0 && c != n[k]) k = fail[k - 1];
            if (c == n[k]) ++k;
            if (k == n.size()) {
                const std::size_t start = i + 1 - k;
                if (!whole || WordOk(h, start, i + 1)) { if (cb(start)) return; }
                k = fail[k - 1];
            }
        }
    }
};

const std::vector<Token> kNoTokens;

inline std::size_t CountNewlines(std::string_view s) { return (std::size_t)std::count(s.begin(), s.end(), '\n'); }

}  // namespace

// ---------------------------------------------------------------------------------------------------------------------
// text in

bool Buffer::IsValidUtf8(std::string_view s) {
    const unsigned char* p = (const unsigned char*)s.data();
    for (std::size_t i = 0; i < s.size();) {
        const std::size_t n = SeqLen(p + i, s.size() - i);
        if (!n) return false;
        i += n;
    }
    return true;
}

std::string Buffer::Sanitize(std::string_view s) { return Buffer().Repair(s); }

std::string Buffer::Repair(std::string_view s) const {
    std::string out;
    out.reserve(s.size());
    const unsigned char* p = (const unsigned char*)s.data();
    for (std::size_t i = 0; i < s.size();) {
        const unsigned char c = p[i];
        if (c == '\r') { out.push_back('\n'); i += (i + 1 < s.size() && p[i + 1] == '\n') ? 2 : 1; continue; }
        if (opt_.dropControls && c != '\n' && c != '\t' && (c < 0x20 || c == 0x7f)) { ++i; continue; }
        if (c == 0) { out += opt_.badByte; ++i; continue; }
        const std::size_t n = SeqLen(p + i, s.size() - i);
        if (!n) { out += opt_.badByte; ++i; continue; }
        out.append((const char*)p + i, n);
        i += n;
    }
    return out;
}

Buffer::Buffer(const Options& o) : opt_(o) {
    opt_.tabSize = std::clamp(opt_.tabSize, 1, 16);
    opt_.maxLines = std::max<std::size_t>(opt_.maxLines, 1);     // a buffer always has one line
    lines_.assign(1, std::string());
    sel_.assign(1, Selection{});
}

void Buffer::SetOptions(const Options& o) {
    opt_ = o;
    opt_.tabSize = std::clamp(opt_.tabSize, 1, 16);
    opt_.maxLines = std::max<std::size_t>(opt_.maxLines, 1);
    TrimHistory();
}

bool Buffer::SetText(std::string_view s) {
    std::string t = Repair(s);
    if (t.size() > opt_.maxBytes) return false;
    if (CountNewlines(t) >= opt_.maxLines) return false;          // N newlines make N+1 lines
    lines_.clear();
    std::size_t start = 0;
    for (;;) {
        const std::size_t nl = t.find('\n', start);
        if (nl == std::string::npos) { lines_.emplace_back(t, start, std::string::npos); break; }
        lines_.emplace_back(t, start, nl - start);
        start = nl + 1;
    }
    bytes_ = t.size();
    undo_.clear(); redo_.clear(); logBytes_ = 0; open_ = Group(); depth_ = 0;
    sel_.assign(1, Selection{});
    tokValid_ = 0;
    startsOk_ = false;
    tokOutLine_ = (std::size_t)-1;
    ++version_;
    return true;
}

std::string Buffer::Text() const {
    std::string r;
    r.reserve(bytes_);
    for (std::size_t i = 0; i < lines_.size(); ++i) { if (i) r.push_back('\n'); r += lines_[i]; }
    return r;
}

std::string_view Buffer::Line(std::size_t line) const {
    return line < lines_.size() ? std::string_view(lines_[line]) : std::string_view();
}

const std::string& Buffer::LineRef(std::size_t line) const {
    static const std::string kEmpty;
    return line < lines_.size() ? lines_[line] : kEmpty;
}

// ---------------------------------------------------------------------------------------------------------------------
// positions

Pos Buffer::Clamp(Pos p) const {
    if (p.line >= lines_.size()) { p.line = lines_.size() - 1; p.col = lines_[p.line].size(); return p; }
    const std::string& s = lines_[p.line];
    if (p.col >= s.size()) { p.col = s.size(); return p; }
    while (p.col > 0 && IsCont((unsigned char)s[p.col])) --p.col;
    return p;
}

Pos Buffer::EndPos() const { return Pos{lines_.size() - 1, lines_.back().size()}; }

void Buffer::EnsureStarts() const {
    if (startsOk_) return;
    starts_.resize(lines_.size());
    std::size_t acc = 0;
    for (std::size_t i = 0; i < lines_.size(); ++i) { starts_[i] = acc; acc += lines_[i].size() + 1; }
    startsOk_ = true;
}

std::size_t Buffer::OffsetOf(Pos p) const {
    p = Clamp(p);
    EnsureStarts();
    return starts_[p.line] + p.col;
}

Pos Buffer::PosOf(std::size_t offset) const {
    if (offset >= bytes_) return EndPos();
    EnsureStarts();
    const std::size_t idx = std::size_t(std::upper_bound(starts_.begin(), starts_.end(), offset) - starts_.begin()) - 1;
    return Clamp(Pos{idx, offset - starts_[idx]});
}

Pos Buffer::Prev(Pos p) const {
    p = Clamp(p);
    if (p.col > 0) {
        const std::string& s = lines_[p.line];
        std::size_t i = p.col - 1;
        while (i > 0 && IsCont((unsigned char)s[i])) --i;
        return Pos{p.line, i};
    }
    if (p.line > 0) return Pos{p.line - 1, lines_[p.line - 1].size()};
    return p;
}

Pos Buffer::Next(Pos p) const {
    p = Clamp(p);
    const std::string& s = lines_[p.line];
    if (p.col < s.size()) return Pos{p.line, p.col + CharLen(s, p.col)};
    if (p.line + 1 < lines_.size()) return Pos{p.line + 1, 0};
    return p;
}

std::size_t Buffer::DisplayColumn(std::size_t line, std::size_t byteCol) const {
    const std::string_view s = Line(line);
    const std::size_t ts = (std::size_t)opt_.tabSize;
    std::size_t col = 0;
    for (std::size_t i = 0; i < s.size() && i < byteCol; i += CharLen(s, i)) col = s[i] == '\t' ? (col / ts + 1) * ts : col + 1;
    return col;
}

std::size_t Buffer::ByteForDisplayColumn(std::size_t line, std::size_t displayCol) const {
    const std::string_view s = Line(line);
    const std::size_t ts = (std::size_t)opt_.tabSize;
    std::size_t col = 0;
    for (std::size_t i = 0; i < s.size(); i += CharLen(s, i)) {
        const std::size_t next = s[i] == '\t' ? (col / ts + 1) * ts : col + 1;
        if (displayCol < next) return i;
        col = next;
    }
    return s.size();
}

std::size_t Buffer::CodepointCount(std::size_t line) const {
    std::size_t n = 0;
    for (char c : Line(line)) if (!IsCont((unsigned char)c)) ++n;
    return n;
}

std::string Buffer::Slice(Pos a, Pos b) const {
    a = Clamp(a); b = Clamp(b);
    if (b < a) std::swap(a, b);
    if (a.line == b.line) return lines_[a.line].substr(a.col, b.col - a.col);
    std::string r = lines_[a.line].substr(a.col);
    for (std::size_t l = a.line + 1; l < b.line; ++l) { r.push_back('\n'); r += lines_[l]; }
    r.push_back('\n');
    r.append(lines_[b.line], 0, b.col);
    return r;
}

std::size_t Buffer::RangeBytes(Pos a, Pos b) const {
    a = Clamp(a); b = Clamp(b);
    if (b < a) std::swap(a, b);
    if (a.line == b.line) return b.col - a.col;
    std::size_t n = lines_[a.line].size() - a.col + 1;
    for (std::size_t l = a.line + 1; l < b.line; ++l) n += lines_[l].size() + 1;
    return n + b.col;
}

Pos Buffer::EndOfText(Pos at, std::string_view s) {
    std::size_t nl = 0, lastNl = 0;
    for (std::size_t i = 0; i < s.size(); ++i) if (s[i] == '\n') { ++nl; lastNl = i + 1; }
    if (!nl) return Pos{at.line, at.col + s.size()};
    return Pos{at.line + nl, s.size() - lastNl};
}

// ---------------------------------------------------------------------------------------------------------------------
// raw changes (no log)

void Buffer::Touch(std::size_t firstLine) {
    ++version_;
    startsOk_ = false;
    tokValid_ = std::min(tokValid_, firstLine);
    tokOutLine_ = (std::size_t)-1;
}

Pos Buffer::RawInsert(Pos at, const std::string& s) {
    Touch(at.line);
    bytes_ += s.size();
    const std::size_t first = s.find('\n');
    if (first == std::string::npos) { lines_[at.line].insert(at.col, s); return Pos{at.line, at.col + s.size()}; }
    std::string tail = lines_[at.line].substr(at.col);
    lines_[at.line].resize(at.col);
    lines_[at.line].append(s, 0, first);
    std::vector<std::string> add;
    std::size_t start = first + 1;
    for (;;) {
        const std::size_t nl = s.find('\n', start);
        if (nl == std::string::npos) { add.emplace_back(s, start, std::string::npos); break; }
        add.emplace_back(s, start, nl - start);
        start = nl + 1;
    }
    const std::size_t lastLen = add.back().size();
    add.back() += tail;
    const std::size_t k = add.size();
    lines_.insert(lines_.begin() + (std::ptrdiff_t)at.line + 1, std::make_move_iterator(add.begin()), std::make_move_iterator(add.end()));
    return Pos{at.line + k, lastLen};
}

std::string Buffer::RawErase(Pos a, Pos b) {
    std::string removed = Slice(a, b);
    Touch(a.line);
    bytes_ -= removed.size();
    if (a.line == b.line) { lines_[a.line].erase(a.col, b.col - a.col); return removed; }
    lines_[a.line].resize(a.col);
    lines_[a.line].append(lines_[b.line], b.col, std::string::npos);
    lines_.erase(lines_.begin() + (std::ptrdiff_t)a.line + 1, lines_.begin() + (std::ptrdiff_t)b.line + 1);
    return removed;
}

Pos Buffer::RawReplace(Pos a, Pos b, const std::string& ins, std::string* removed) {
    std::string r;
    if (a != b) r = RawErase(a, b);
    Pos end = a;
    if (!ins.empty()) end = RawInsert(a, ins);
    // line marks: lines [a.line, b.line] became end.line - a.line + 1 lines
    const long long oldN = (long long)(b.line - a.line) + 1, newN = (long long)(end.line - a.line) + 1, delta = newN - oldN;
    if (delta != 0) {
        for (std::vector<LineMark>& set : marks_) {
            std::size_t w = 0;
            for (std::size_t i = 0; i < set.size(); ++i) {
                LineMark m = set[i];
                if (m.line > b.line) m.line = (std::size_t)((long long)m.line + delta);
                else if (delta < 0 && m.line >= a.line + (std::size_t)newN) continue;      // one of the removed lines
                set[w++] = m;
            }
            set.resize(w);
        }
    }
    if (removed) *removed = std::move(r);
    return end;
}

std::size_t Buffer::NewMarkSet() { marks_.emplace_back(); return marks_.size() - 1; }
void Buffer::SetMarks(std::size_t set, std::vector<LineMark> marks) { if (set < marks_.size()) marks_[set] = std::move(marks); }
const std::vector<LineMark>& Buffer::Marks(std::size_t set) const {
    static const std::vector<LineMark> kNone;
    return set < marks_.size() ? marks_[set] : kNone;
}

void Buffer::AdjustForErase(Pos& p, Pos a, Pos b) const {
    if (p <= a) return;
    if (p < b) { p = a; return; }
    if (p.line == b.line) p = Pos{a.line, a.col + (p.col - b.col)};
    else p.line -= (b.line - a.line);
}

void Buffer::AdjustForInsert(Pos& p, Pos at, Pos end) const {
    if (p < at) return;
    const std::size_t k = end.line - at.line;
    if (p.line == at.line) {
        if (k == 0) p.col += end.col - at.col;
        else p = Pos{end.line, end.col + (p.col - at.col)};
    } else p.line += k;
}

// ---------------------------------------------------------------------------------------------------------------------
// logged edits

// The size rule for a change that removes `removedBytes` (over `removedLines` line breaks) and inserts `ins` (already repaired).
// A change that only removes text never grows it, so it always fits. All arithmetic is overflow-safe.
bool Buffer::FitsAfter(std::size_t removedBytes, std::size_t removedLines, std::string_view ins) const {
    if (ins.empty()) return true;
    const std::size_t baseBytes = bytes_ - std::min(removedBytes, bytes_);
    if (ins.size() > opt_.maxBytes || baseBytes > opt_.maxBytes - ins.size()) return false;
    const std::size_t newLines = CountNewlines(ins);
    if (newLines) {
        const std::size_t baseLines = lines_.size() - std::min(removedLines, lines_.size() - 1);
        if (baseLines > opt_.maxLines || newLines > opt_.maxLines - baseLines) return false;
    }
    return true;
}

bool Buffer::WouldFit(Pos a, Pos b, std::string_view s) const {
    a = Clamp(a); b = Clamp(b);
    if (b < a) std::swap(a, b);
    return FitsAfter(RangeBytes(a, b), b.line - a.line, Repair(s));
}

void Buffer::BeginGroup() {
    if (depth_++ == 0) { open_ = Group(); open_.before = sel_; }
}

void Buffer::EndGroup() {
    if (depth_ <= 0) return;
    if (--depth_ > 0) return;
    if (open_.edits.empty()) return;
    open_.after = sel_;
    logBytes_ += open_.bytes;
    undo_.push_back(std::move(open_));
    open_ = Group();
    redo_.clear();
    TrimHistory();
}

void Buffer::TrimHistory() {
    while (!undo_.empty() && (undo_.size() > opt_.undoGroups || logBytes_ > opt_.undoBytes)) {
        logBytes_ -= undo_.front().bytes;
        undo_.pop_front();
    }
}

void Buffer::ClearHistory() {
    if (depth_ > 0) return;
    undo_.clear(); redo_.clear(); logBytes_ = 0;
}

bool Buffer::ReopenLastGroup() {
    if (depth_ > 0 || undo_.empty()) return false;
    open_ = std::move(undo_.back());
    undo_.pop_back();
    logBytes_ -= open_.bytes;
    redo_.clear();
    depth_ = 1;
    return true;
}

void Buffer::RecordSelectionsInLastGroup() {
    if (depth_ == 0 && !undo_.empty()) undo_.back().after = sel_;
}

bool Buffer::ApplyEdit(Pos a, Pos b, std::string_view s, std::optional<Pos>* endOut, bool adjustSel) {
    a = Clamp(a); b = Clamp(b);
    if (b < a) std::swap(a, b);
    std::string ins = Repair(s);
    const std::size_t removedLen = RangeBytes(a, b);
    if (removedLen == 0 && ins.empty()) return false;
    if (!FitsAfter(removedLen, b.line - a.line, ins)) return false;   // over the size or line limit: nothing changes
    BeginGroup();
    EditRec r;
    r.at = a;
    const Pos end = RawReplace(a, b, ins, &r.removed);
    if (adjustSel) {
        for (Selection& x : sel_) {
            AdjustForErase(x.anchor, a, b); AdjustForErase(x.head, a, b);
            AdjustForInsert(x.anchor, a, end); AdjustForInsert(x.head, a, end);
        }
    }
    open_.bytes += r.removed.size() + ins.size();
    r.inserted = std::move(ins);
    open_.edits.push_back(std::move(r));
    EndGroup();
    if (endOut) *endOut = end;
    return true;
}

std::optional<Pos> Buffer::Insert(Pos at, std::string_view s) {
    std::optional<Pos> end;
    at = Clamp(at);
    if (s.empty()) return at;
    if (!ApplyEdit(at, at, s, &end, true)) return std::nullopt;
    return end;
}

bool Buffer::Erase(Pos a, Pos b) { return ApplyEdit(a, b, std::string_view(), nullptr, true); }

std::optional<Pos> Buffer::Replace(Pos a, Pos b, std::string_view s) {
    std::optional<Pos> end;
    if (!ApplyEdit(a, b, s, &end, true)) {
        // nothing to do (empty both) is not a refusal; a refusal is a size-limit failure
        a = Clamp(a);
        return (s.empty() && RangeBytes(a, b) == 0) ? std::optional<Pos>(a) : std::nullopt;
    }
    return end;
}

bool Buffer::Undo() {
    if (depth_ > 0 || undo_.empty()) return false;
    Group g = std::move(undo_.back());
    undo_.pop_back();
    logBytes_ -= g.bytes;
    for (std::size_t i = g.edits.size(); i-- > 0;) {
        const EditRec& e = g.edits[i];
        RawReplace(e.at, EndOfText(e.at, e.inserted), e.removed, nullptr);
    }
    sel_ = g.before;
    for (Selection& s : sel_) { s.anchor = Clamp(s.anchor); s.head = Clamp(s.head); }
    redo_.push_back(std::move(g));
    return true;
}

bool Buffer::Redo() {
    if (depth_ > 0 || redo_.empty()) return false;
    Group g = std::move(redo_.back());
    redo_.pop_back();
    for (const EditRec& e : g.edits) {
        RawReplace(e.at, EndOfText(e.at, e.removed), e.inserted, nullptr);
    }
    sel_ = g.after;
    for (Selection& s : sel_) { s.anchor = Clamp(s.anchor); s.head = Clamp(s.head); }
    logBytes_ += g.bytes;
    undo_.push_back(std::move(g));
    TrimHistory();
    return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// selections

void Buffer::SetSelections(std::vector<Selection> s) {
    if (s.size() > kMaxCursors) s.resize(kMaxCursors);
    for (Selection& x : s) { x.anchor = Clamp(x.anchor); x.head = Clamp(x.head); }
    if (s.empty()) s.push_back(Selection{});
    std::stable_sort(s.begin(), s.end(), [](const Selection& a, const Selection& b) { return a.Lo() < b.Lo(); });
    std::vector<Selection> out;
    for (const Selection& x : s) {
        if (!out.empty()) {
            Selection& p = out.back();
            if (x.Lo() < p.Hi() || (x.Empty() && p.Empty() && x.Lo() == p.Lo())) {
                const Pos lo = p.Lo(), hi = std::max(p.Hi(), x.Hi());
                const bool backward = p.head < p.anchor;
                p = backward ? Selection{hi, lo} : Selection{lo, hi};
                continue;
            }
        }
        out.push_back(x);
    }
    sel_ = std::move(out);
}

void Buffer::AddCursor(Pos p) {
    std::vector<Selection> s = sel_;
    s.push_back(Selection{p, p});
    SetSelections(std::move(s));
}

std::string Buffer::SelectedText() const {
    std::string r;
    bool first = true;
    for (const Selection& s : sel_) {
        if (s.Empty()) continue;
        if (!first) r.push_back('\n');
        first = false;
        r += Slice(s.Lo(), s.Hi());
    }
    return r;
}

bool Buffer::EditEachSelection(int mode, std::string_view text) {
    const std::string ins = mode == 0 ? Repair(text) : std::string();
    const std::vector<Selection> s = sel_;
    const std::size_t n = s.size();
    std::vector<Range> rg(n);
    for (std::size_t i = 0; i < n; ++i) {
        const Selection& x = s[i];
        if (!x.Empty() || mode == 0) { rg[i] = Range{x.Lo(), x.Hi()}; continue; }
        switch (mode) {
            case 1: rg[i] = Range{Prev(x.head), x.head}; break;
            case 2: rg[i] = Range{x.head, Next(x.head)}; break;
            case 3: rg[i] = Range{PrevWordStart(x.head), x.head}; break;
            default: rg[i] = Range{x.head, NextWordEnd(x.head)}; break;
        }
    }
    std::size_t removed = 0;
    for (std::size_t i = 0; i < n; ++i) {
        if (i && rg[i].begin < rg[i - 1].end) rg[i].begin = std::min(rg[i - 1].end, rg[i].end);   // never overlap the one before
        removed += RangeBytes(rg[i].begin, rg[i].end);
    }
    if (!ins.empty()) {                                       // size limit, overflow-safe
        if (ins.size() > opt_.maxBytes || n > opt_.maxBytes / ins.size()) return false;
        const std::size_t add = n * ins.size();
        const std::size_t base = bytes_ - std::min(removed, bytes_);
        if (base > opt_.maxBytes || add > opt_.maxBytes - base) return false;
        if (const std::size_t insLines = CountNewlines(ins)) {   // line limit: every caret adds the new lines
            std::size_t removedLines = 0;
            for (std::size_t i = 0; i < n; ++i) removedLines += rg[i].end.line - rg[i].begin.line;
            const std::size_t baseLines = lines_.size() - std::min(removedLines, lines_.size() - 1);
            if (insLines > opt_.maxLines / n || baseLines > opt_.maxLines || n * insLines > opt_.maxLines - baseLines) return false;
        }
    }
    std::vector<Selection> cur = s;
    bool any = false;
    BeginGroup();
    for (std::size_t i = n; i-- > 0;) {
        const Range& r = rg[i];
        if (r.Empty() && ins.empty()) continue;
        std::optional<Pos> end;
        if (!ApplyEdit(r.begin, r.end, ins, &end, false)) continue;
        any = true;
        for (std::size_t j = i + 1; j < n; ++j) {
            for (Pos* p : {&cur[j].anchor, &cur[j].head}) { AdjustForErase(*p, r.begin, r.end); AdjustForInsert(*p, r.begin, *end); }
        }
        cur[i] = Selection{*end, *end};
    }
    if (any) {
        sel_ = cur;
        SetSelections(sel_);
    }
    EndGroup();
    return any;
}

bool Buffer::TypeText(std::string_view s) { return EditEachSelection(0, s); }
bool Buffer::Backspace() { return EditEachSelection(1, std::string_view()); }
bool Buffer::DeleteForward() { return EditEachSelection(2, std::string_view()); }
bool Buffer::DeleteWord(bool forward) { return EditEachSelection(forward ? 4 : 3, std::string_view()); }

// ---------------------------------------------------------------------------------------------------------------------
// words

bool Buffer::IsWordByte(unsigned char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c >= 0x80;
}

Pos Buffer::PrevWordStart(Pos p) const {
    p = Clamp(p);
    if (p.col == 0) return p.line > 0 ? Pos{p.line - 1, lines_[p.line - 1].size()} : p;
    const std::string& s = lines_[p.line];
    std::size_t i = p.col;
    while (i > 0 && Blank(s[i - 1])) --i;
    if (i == 0) return Pos{p.line, 0};
    const int c = Class(s[i - 1]);
    while (i > 0 && Class(s[i - 1]) == c) --i;
    return Pos{p.line, i};
}

Pos Buffer::NextWordEnd(Pos p) const {
    p = Clamp(p);
    const std::string& s = lines_[p.line];
    if (p.col >= s.size()) return p.line + 1 < lines_.size() ? Pos{p.line + 1, 0} : p;
    std::size_t i = p.col;
    const int c = Class(s[i]);
    if (c != 0) while (i < s.size() && Class(s[i]) == c) ++i;
    while (i < s.size() && Blank(s[i])) ++i;
    return Pos{p.line, i};
}

Range Buffer::WordAt(Pos p) const {
    p = Clamp(p);
    const std::string& s = lines_[p.line];
    std::size_t l = p.col, r = p.col;
    while (l > 0 && IsWordByte((unsigned char)s[l - 1])) --l;
    while (r < s.size() && IsWordByte((unsigned char)s[r])) ++r;
    return Range{Pos{p.line, l}, Pos{p.line, r}};
}

// ---------------------------------------------------------------------------------------------------------------------
// indent

std::string Buffer::IndentUnit() const { return opt_.insertSpaces ? std::string((std::size_t)opt_.tabSize, ' ') : std::string("\t"); }

std::size_t Buffer::IndentBytes(std::size_t line) const {
    const std::string_view s = Line(line);
    std::size_t i = 0;
    while (i < s.size() && Blank(s[i])) ++i;
    return i;
}

std::string Buffer::IndentOf(std::size_t line) const { return std::string(Line(line).substr(0, IndentBytes(line))); }

std::string Buffer::NewlineText(Pos p) const {
    p = Clamp(p);
    const std::string_view s = Line(p.line);
    std::string r = "\n";
    r.append(s.substr(0, std::min(IndentBytes(p.line), p.col)));
    std::size_t i = p.col;
    while (i > 0 && Blank(s[i - 1])) --i;                     // the last real character before the caret
    if (i > 0 && (s[i - 1] == '{' || s[i - 1] == '(' || s[i - 1] == '[')) r += IndentUnit();
    return r;
}

bool Buffer::IndentLines(std::size_t first, std::size_t last) {
    if (first >= lines_.size()) return false;
    last = std::min(last, lines_.size() - 1);
    if (first > last) return false;
    const std::string unit = IndentUnit();
    bool any = false;
    BeginGroup();
    for (std::size_t l = first; l <= last; ++l)
        if (!lines_[l].empty() && ApplyEdit(Pos{l, 0}, Pos{l, 0}, unit, nullptr, true)) any = true;
    EndGroup();
    return any;
}

bool Buffer::OutdentLines(std::size_t first, std::size_t last) {
    if (first >= lines_.size()) return false;
    last = std::min(last, lines_.size() - 1);
    if (first > last) return false;
    bool any = false;
    BeginGroup();
    for (std::size_t l = first; l <= last; ++l) {
        const std::string& s = lines_[l];
        std::size_t n = 0;
        if (!s.empty() && s[0] == '\t') n = 1;
        else while (n < s.size() && n < (std::size_t)opt_.tabSize && s[n] == ' ') ++n;
        if (n && ApplyEdit(Pos{l, 0}, Pos{l, n}, std::string_view(), nullptr, true)) any = true;
    }
    EndGroup();
    return any;
}

// ---------------------------------------------------------------------------------------------------------------------
// brackets

std::optional<BracketPair> Buffer::MatchBracket(Pos p, const SkipFn& skip, std::size_t maxScan, std::size_t maxLines) const {
    p = Clamp(p);
    auto kindAt = [&](Pos q, char& open, char& close, bool& isOpen) {
        const std::string& s = lines_[q.line];
        if (q.col >= s.size()) return false;
        switch (s[q.col]) {
            case '(': open = '('; close = ')'; isOpen = true; return true;
            case '[': open = '['; close = ']'; isOpen = true; return true;
            case '{': open = '{'; close = '}'; isOpen = true; return true;
            case ')': open = '('; close = ')'; isOpen = false; return true;
            case ']': open = '['; close = ']'; isOpen = false; return true;
            case '}': open = '{'; close = '}'; isOpen = false; return true;
            default: return false;
        }
    };
    Pos cand[2] = {p, p};
    int nc = 1;
    if (p.col > 0) { cand[1] = Pos{p.line, p.col - 1}; nc = 2; }
    for (int ci = 0; ci < nc; ++ci) {
        const Pos q = cand[ci];
        char open = 0, close = 0; bool isOpen = false;
        if (!kindAt(q, open, close, isOpen)) continue;
        if (skip && skip(q)) return std::nullopt;                // the bracket by the caret decides, even if it is rejected
        std::size_t depth = 1, scanned = 0;
        if (isOpen) {
            std::size_t l = q.line, c = q.col + 1;
            for (;; ) {
                const std::string& s = lines_[l];
                for (; c < s.size(); ++c) {
                    if (++scanned > maxScan) return std::nullopt;
                    if (s[c] != open && s[c] != close) continue;
                    if (skip && skip(Pos{l, c})) continue;
                    if (s[c] == open) ++depth;
                    else if (--depth == 0) return BracketPair{q, Pos{l, c}, q};
                }
                if (++l >= lines_.size() || l - q.line > maxLines) return std::nullopt;
                c = 0;
                if (++scanned > maxScan) return std::nullopt;
            }
        } else {
            std::size_t l = q.line, c = q.col;
            for (;;) {
                const std::string& s = lines_[l];
                while (c > 0) {
                    --c;
                    if (++scanned > maxScan) return std::nullopt;
                    if (s[c] != open && s[c] != close) continue;
                    if (skip && skip(Pos{l, c})) continue;
                    if (s[c] == close) ++depth;
                    else if (--depth == 0) return BracketPair{Pos{l, c}, q, q};
                }
                if (l == 0 || q.line - l >= maxLines) return std::nullopt;
                --l;
                c = lines_[l].size();
                if (++scanned > maxScan) return std::nullopt;
            }
        }
    }
    return std::nullopt;
}

// ---------------------------------------------------------------------------------------------------------------------
// find and replace

std::optional<Range> Buffer::Find(std::string_view needleIn, Pos from, FindFlags f, bool backward, bool wrap) const {
    const std::string needle = Repair(needleIn);
    if (needle.empty()) return std::nullopt;
    from = Clamp(from);
    const Matcher m(needle, f);
    if (needle.find('\n') != std::string::npos) {             // spans lines: search the joined text
        const std::string flat = Text();
        const std::size_t off = OffsetOf(from);
        std::optional<std::size_t> hit;
        auto scan = [&](std::size_t lo, std::size_t hi, bool last) {   // matches starting in [lo, hi)
            std::optional<std::size_t> r;
            m.Run(std::string_view(flat), [&](std::size_t st) {
                if (st < lo) return false;
                if (st >= hi) return true;
                r = st;
                return !last;
            });
            return r;
        };
        if (!backward) { hit = scan(off, flat.size() + 1, false); if (!hit && wrap) hit = scan(0, off, false); }
        else { hit = scan(0, off, true); if (!hit && wrap) hit = scan(off, flat.size() + 1, true); }
        if (!hit) return std::nullopt;
        return Range{PosOf(*hit), PosOf(*hit + needle.size())};
    }
    auto scanLine = [&](std::size_t l, std::size_t lo, std::size_t hi, bool last) {
        std::optional<std::size_t> r;
        m.Run(Line(l), [&](std::size_t st) {
            if (st < lo) return false;
            if (st >= hi) return true;
            r = st;
            return !last;
        });
        return r;
    };
    const std::size_t inf = (std::size_t)-1;
    auto mk = [&](std::size_t l, std::size_t st) { return Range{Pos{l, st}, Pos{l, st + needle.size()}}; };
    if (!backward) {
        for (std::size_t l = from.line; l < lines_.size(); ++l)
            if (auto r = scanLine(l, l == from.line ? from.col : 0, inf, false)) return mk(l, *r);
        if (wrap)
            for (std::size_t l = 0; l <= from.line; ++l)
                if (auto r = scanLine(l, 0, l == from.line ? from.col : inf, false)) return mk(l, *r);
    } else {
        for (std::size_t l = from.line + 1; l-- > 0;)
            if (auto r = scanLine(l, 0, l == from.line ? from.col : inf, true)) return mk(l, *r);
        if (wrap)
            for (std::size_t l = lines_.size(); l-- > 0 && l >= from.line;)
                if (auto r = scanLine(l, l == from.line ? from.col : 0, inf, true)) return mk(l, *r);
    }
    return std::nullopt;
}

std::vector<Range> Buffer::FindAll(std::string_view needleIn, FindFlags f, std::size_t maxCount) const {
    std::vector<Range> out;
    const std::string needle = Repair(needleIn);
    if (needle.empty() || maxCount == 0) return out;
    const Matcher m(needle, f);
    if (needle.find('\n') != std::string::npos) {
        const std::string flat = Text();
        std::size_t lastEnd = 0;
        m.Run(std::string_view(flat), [&](std::size_t st) {
            if (st < lastEnd) return false;
            lastEnd = st + needle.size();
            out.push_back(Range{PosOf(st), PosOf(lastEnd)});
            return out.size() >= maxCount;
        });
        return out;
    }
    for (std::size_t l = 0; l < lines_.size() && out.size() < maxCount; ++l) {
        std::size_t lastEnd = 0;
        m.Run(Line(l), [&](std::size_t st) {
            if (st < lastEnd) return false;
            lastEnd = st + needle.size();
            out.push_back(Range{Pos{l, st}, Pos{l, lastEnd}});
            return out.size() >= maxCount;
        });
    }
    return out;
}

std::vector<std::size_t> Buffer::FindInLine(std::size_t line, std::string_view needleIn, FindFlags f) const {
    std::vector<std::size_t> out;
    const std::string needle = Repair(needleIn);
    if (needle.empty() || needle.find('\n') != std::string::npos) return out;
    const Matcher m(needle, f);
    std::size_t lastEnd = 0;
    m.Run(Line(line), [&](std::size_t st) {
        if (st >= lastEnd) { out.push_back(st); lastEnd = st + needle.size(); }
        return false;
    });
    return out;
}

std::size_t Buffer::ReplaceAll(std::string_view needleIn, std::string_view replIn, FindFlags f) {
    const std::string needle = Repair(needleIn);
    const std::string repl = Repair(replIn);
    if (needle.empty()) return 0;
    // One pass over the joined text (a needle without a newline cannot match across a line break, so these are the same
    // non-overlapping matches FindAll reports, and there is no cap and no list of ranges to hold), building the replacement of the span
    // from the first match to the last: one linear edit whatever the number of matches.
    const Matcher m(needle, f);
    const std::string flat = Text();
    std::string out;
    std::size_t count = 0, first = 0, resume = 0;           // resume: where the copy of the text between matches continues
    bool tooBig = false;
    m.Run(std::string_view(flat), [&](std::size_t st) {
        if (count && st < resume) return false;                // overlaps the match before it
        if (!count) { first = st; resume = st; }
        out.append(flat, resume, st - resume);
        out += repl;
        resume = st + needle.size();
        ++count;
        if (out.size() > opt_.maxBytes) { tooBig = true; return true; }   // stop early instead of building a huge string
        return false;
    });
    if (!count || tooBig) return 0;
    if (!ApplyEdit(PosOf(first), PosOf(resume), out, nullptr, true)) return 0;
    return count;
}

// ---------------------------------------------------------------------------------------------------------------------
// tokenizer

void Buffer::SetTokenizer(Tokenizer t) {
    tok_ = std::move(t);
    tokValid_ = 0;
    tokOutLine_ = (std::size_t)-1;
}

const std::vector<Token>& Buffer::LineTokens(std::size_t line) const {
    if (!tok_ || line >= lines_.size()) return kNoTokens;
    if (tokState_.size() != lines_.size() + 1) tokState_.resize(lines_.size() + 1, 0);
    tokValid_ = std::min(tokValid_, lines_.size());
    while (tokValid_ < line) {                                  // the end states of the lines above, each computed once
        tokOut_.clear();
        tokOutLine_ = (std::size_t)-1;
        tokState_[tokValid_ + 1] = tok_(lines_[tokValid_], tokState_[tokValid_], tokOut_);
        ++tokValid_;
    }
    if (tokOutLine_ == line) return tokOut_;
    tokOut_.clear();
    const std::string& s = lines_[line];
    tok_(s, tokState_[line], tokOut_);
    for (Token& k : tokOut_) {                                  // a careless tokenizer must not be able to break callers
        k.end = std::min(k.end, s.size());
        k.begin = std::min(k.begin, k.end);
    }
    tokOutLine_ = line;
    return tokOut_;
}

}  // namespace okay::text
