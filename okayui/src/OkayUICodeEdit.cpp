// OkayUI::CodeEdit: see include/okay/UI/OkayUICodeEdit.hpp and docs/ui/own_ui_code_editor.md.
#include "okay/UI/OkayUICodeEdit.hpp"

#include <algorithm>
#include <cctype>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace OkayUI {

// float to int without undefined behaviour: NaN gives 0, anything outside [-1e9, 1e9] is clamped.
static int SafeInt(float v) { return v != v ? 0 : (v < -1.0e9f ? -1000000000 : (v > 1.0e9f ? 1000000000 : (int)v)); }

// ================================================================================================
// Small text helpers
// ================================================================================================
namespace {

inline bool isCont(unsigned char c) { return (c & 0xC0) == 0x80; }
inline int nextIdx(const std::string& s, int i) {
    const int n = (int)s.size();
    if (i >= n) return n;
    ++i;
    while (i < n && isCont((unsigned char)s[(size_t)i])) ++i;
    return i;
}
inline int prevIdx(const std::string& s, int i) {
    if (i <= 0) return 0;
    --i;
    while (i > 0 && isCont((unsigned char)s[(size_t)i])) --i;
    return i;
}
inline int nextTab(int col, int tab) { return (col / tab + 1) * tab; }

// 0 blank, 1 word, 2 punctuation
inline int charClass(const std::string& s, int i) {
    const unsigned char c = (unsigned char)s[(size_t)i];
    if (c == ' ' || c == '\t') return 0;
    if (c >= 0x80 || c == '_' || (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) return 1;
    return 2;
}

// Valid UTF-8 only; "\r\n" and "\r" become "\n"; other control characters are dropped; bad bytes become '?'.
std::string sanitize(const char* s, size_t n) {
    std::string out;
    if (!s) return out;
    out.reserve(n);
    for (size_t i = 0; i < n;) {
        const unsigned char c = (unsigned char)s[i];
        if (c == '\r') { out += '\n'; ++i; if (i < n && s[i] == '\n') ++i; continue; }
        if (c == '\n' || c == '\t') { out += (char)c; ++i; continue; }
        if (c < 0x20 || c == 0x7f) { ++i; continue; }
        if (c < 0x80) { out += (char)c; ++i; continue; }
        int len = 0;
        if (c >= 0xC2 && c <= 0xDF) len = 2; else if (c >= 0xE0 && c <= 0xEF) len = 3; else if (c >= 0xF0 && c <= 0xF4) len = 4;
        bool ok = len > 0 && i + (size_t)len <= n;
        if (ok) for (int k = 1; k < len; ++k) if (!isCont((unsigned char)s[i + (size_t)k])) { ok = false; break; }
        if (ok) {
            const unsigned char b1 = (unsigned char)s[i + 1];
            if ((c == 0xE0 && b1 < 0xA0) || (c == 0xED && b1 > 0x9F) || (c == 0xF0 && b1 < 0x90) || (c == 0xF4 && b1 > 0x8F)) ok = false;
        }
        if (ok) { out.append(s + i, (size_t)len); i += (size_t)len; } else { out += '?'; ++i; }
    }
    return out;
}
std::string sanitize(const char* s) { return s ? sanitize(s, std::strlen(s)) : std::string(); }

bool isBracket(char c) { return c == '(' || c == ')' || c == '[' || c == ']' || c == '{' || c == '}'; }
char partnerOf(char c) {
    switch (c) { case '(': return ')'; case ')': return '('; case '[': return ']'; case ']': return '['; case '{': return '}'; default: return '{'; }
}
bool isOpener(char c) { return c == '(' || c == '[' || c == '{'; }

bool inSkipToken(const std::vector<CodeToken>& t, int j) {
    // tokens are sorted and disjoint: find the last one starting at or before j
    size_t lo = 0, hi = t.size();
    while (lo < hi) { const size_t m = (lo + hi) / 2; if (t[m].start <= j) lo = m + 1; else hi = m; }
    if (lo == 0) return false;
    const CodeToken& k = t[lo - 1];
    return j < k.end && (k.color == CodeColor_String || k.color == CodeColor_Char || k.color == CodeColor_Comment);
}

int digitsOf(int n) { int d = 1; while (n >= 10) { n /= 10; ++d; } return d; }

} // namespace

// ================================================================================================
// Highlighter
// ================================================================================================
CodeLanguage CLikeLanguage() {
    CodeLanguage l;
    for (const char* k : {"if", "else", "for", "while", "do", "return", "break", "continue", "switch", "case", "default", "goto", "struct", "class",
                          "enum", "union", "typedef", "namespace", "using", "template", "typename", "public", "private", "protected", "virtual", "override",
                          "const", "constexpr", "static", "extern", "inline", "new", "delete", "this", "try", "catch", "throw", "sizeof", "operator",
                          "true", "false", "nullptr", "auto", "volatile", "explicit", "friend", "final"})
        l.keywords.insert(k);
    for (const char* t : {"void", "int", "float", "double", "char", "bool", "short", "long", "unsigned", "signed", "size_t", "string"}) l.types.insert(t);
    l.dollarAtStrings = false;
    return l;
}

CodeLanguage OkayScriptLanguage() {
    CodeLanguage l;
    for (const char* k : {"if", "else", "for", "while", "do", "return", "break", "continue", "switch", "case", "default", "var", "let", "const", "function",
                          "func", "def", "class", "struct", "enum", "interface", "new", "public", "private", "protected", "internal", "static", "readonly",
                          "override", "virtual", "abstract", "sealed", "ref", "out", "is", "as", "typeof", "base", "get", "set", "yield", "using", "namespace",
                          "void", "int", "float", "double", "bool", "string", "char", "long", "byte", "uint", "object", "true", "false", "null", "this",
                          "foreach", "in", "and", "or", "not", "try", "catch", "finally", "throw", "short", "ushort", "ulong", "sbyte", "decimal", "async",
                          "await", "params", "delegate", "event", "operator", "nameof", "when", "where", "partial"})
        l.keywords.insert(k);
    for (const char* t : {"GameObject", "Transform", "Vector3", "Vector2", "Quaternion", "Color", "Mathf", "Time", "Input", "KeyCode", "Debug", "Random",
                          "Space", "Screen", "List", "Dictionary", "KeyValuePair", "HashSet", "Queue", "Stack", "IEnumerator", "Coroutine", "WaitForSeconds",
                          "WaitForEndOfFrame", "Rigidbody", "Rigidbody2D", "SpriteRenderer", "MeshRenderer", "Renderer", "Animator", "AudioSource",
                          "Collider", "Collider2D", "Collision", "Collision2D", "ForceMode2D", "SerializeField", "Header", "Range", "Tooltip", "Action",
                          "Func", "String", "Text", "SortedDictionary", "IEnumerable"})
        l.types.insert(t);
    l.lineComments = {"//", "#"};
    return l;
}

namespace {
int findStr(const char* s, int n, int from, const std::string& pat) {
    const int m = (int)pat.size();
    if (m == 0) return -1;
    for (int i = from; i + m <= n; ++i)
        if (s[i] == pat[0] && std::memcmp(s + i, pat.data(), (size_t)m) == 0) return i;
    return -1;
}
inline bool identStart(unsigned char c) { return c == '_' || c >= 0x80 || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
inline bool identChar(unsigned char c) { return identStart(c) || (c >= '0' && c <= '9'); }
inline bool isDigit(unsigned char c) { return c >= '0' && c <= '9'; }
}

CodeHighlighter MakeCLikeHighlighter(const CodeLanguage& lang) {
    auto L = std::make_shared<CodeLanguage>(lang);
    return [L](const char* s, int n, int state, std::vector<CodeToken>& out) -> int {
        const CodeLanguage& g = *L;
        auto emit = [&](int a, int b, unsigned char c) {
            if (b <= a) return;
            if (!out.empty() && out.back().color == c && out.back().end == a) out.back().end = b; else out.push_back(CodeToken{a, b, c});
        };
        int i = 0;
        if (state == 1) {   // inside a block comment from an earlier line
            const int e = findStr(s, n, 0, g.blockEnd);
            if (e < 0) { emit(0, n, CodeColor_Comment); return 1; }
            i = e + (int)g.blockEnd.size();
            emit(0, i, CodeColor_Comment);
            state = 0;
        }
        while (i < n) {
            const unsigned char c = (unsigned char)s[i];
            if (c == ' ' || c == '\t') { ++i; continue; }
            // A block comment first: Lua's "--[[" begins with its line comment "--".
            if (!g.blockStart.empty() && i + (int)g.blockStart.size() <= n && std::memcmp(s + i, g.blockStart.data(), g.blockStart.size()) == 0) {
                const int e = findStr(s, n, i + (int)g.blockStart.size(), g.blockEnd);
                if (e < 0) { emit(i, n, CodeColor_Comment); return 1; }
                const int end = e + (int)g.blockEnd.size();
                emit(i, end, CodeColor_Comment);
                i = end;
                continue;
            }
            bool lc = false;
            for (const std::string& p : g.lineComments)
                if (!p.empty() && i + (int)p.size() <= n && std::memcmp(s + i, p.data(), p.size()) == 0) { lc = true; break; }
            if (lc) { emit(i, n, CodeColor_Comment); return 0; }
            int q = -1;   // the opening quote of a string, if one starts here
            if (c == '"') q = i;
            else if (g.dollarAtStrings && (c == '$' || c == '@') && i + 1 < n && s[i + 1] == '"') q = i + 1;
            if (q >= 0) {
                int j = q + 1;
                while (j < n && s[j] != '"') { if (s[j] == '\\' && j + 1 < n) ++j; ++j; }
                j = j < n ? j + 1 : n;
                emit(i, j, CodeColor_String);
                i = j;
                continue;
            }
            if (c == '\'') {
                int j = i + 1;
                if (j < n && s[j] == '\\' && j + 1 < n) j += 2; else if (j < n && s[j] != '\'') j = nextIdx(std::string(s + j, (size_t)std::min(n - j, 4)), 0) + j;
                else j = n + 1;
                if (j < n && s[j] == '\'') { emit(i, j + 1, CodeColor_Char); i = j + 1; continue; }
                emit(i, i + 1, CodeColor_Punct); ++i; continue;
            }
            if (isDigit(c) || (c == '.' && i + 1 < n && isDigit((unsigned char)s[i + 1]))) {
                int j = i + 1;
                while (j < n) {
                    const unsigned char d = (unsigned char)s[j];
                    if (identChar(d) || d == '.') { ++j; if ((d == 'e' || d == 'E') && j < n && (s[j] == '+' || s[j] == '-') && !(s[i] == '0' && i + 1 < n && (s[i + 1] == 'x' || s[i + 1] == 'X'))) ++j; }
                    else break;
                }
                emit(i, j, CodeColor_Number);
                i = j;
                continue;
            }
            if (identStart(c)) {
                int j = i + 1;
                while (j < n && identChar((unsigned char)s[j])) ++j;
                const std::string w(s + i, (size_t)(j - i));
                unsigned char col = CodeColor_Default;
                if (g.keywords.count(w)) col = CodeColor_Keyword;
                else if (g.types.count(w)) col = CodeColor_Type;
                else if (g.known.count(w) && !g.callBeforeKnown) col = CodeColor_Known;
                else {
                    int k = j; while (k < n && (s[k] == ' ' || s[k] == '\t')) ++k;
                    int b = i - 1; while (b >= 0 && (s[b] == ' ' || s[b] == '\t')) --b;
                    if (k < n && s[k] == '(') col = CodeColor_Function;
                    else if (b >= 0 && s[b] == '.') col = CodeColor_Member;
                    else if (g.known.count(w)) col = CodeColor_Known;   // callBeforeKnown: a bare built-in
                }
                if (col != CodeColor_Default) emit(i, j, col);
                i = j;
                continue;
            }
            if (std::strchr("[]{}!%^&*()-+=~|<>?/;:,.", (int)c)) { emit(i, i + 1, CodeColor_Punct); ++i; continue; }
            ++i;   // anything else (other symbols, non-ASCII bytes that are not letters) stays default
        }
        return state;
    };
}

CodePalette DefaultCodePalette() {
    CodePalette p;
    p.text[CodeColor_Default] = 0xffc6b7a9u;  p.text[CodeColor_Keyword] = 0xff3278ccu; p.text[CodeColor_Type] = 0xffb0c94eu;
    p.text[CodeColor_Function] = 0xff6dc6ffu; p.text[CodeColor_Known] = 0xffaa7698u;   p.text[CodeColor_Member] = 0xffc49ab0u;
    p.text[CodeColor_Number] = 0xffbb9768u;   p.text[CodeColor_String] = 0xff59876au;  p.text[CodeColor_Char] = 0xff59876au;
    p.text[CodeColor_Comment] = 0xff808a80u;  p.text[CodeColor_Punct] = 0xffc8c8c8u;   p.text[CodeColor_Preproc] = 0xff808080u;
    p.background = 0xff241f1cu; p.gutter = RGBA(30, 33, 39); p.lineNumber = 0xff8a8070u; p.lineNumberCurrent = 0xffc4bbb0u;
    p.caret = 0xffe8e6e2u; p.selection = 0x8c915c3au; p.selectionInactive = RGBA(90, 100, 120, 90);
    p.currentLine = RGBA(255, 255, 255, 14); p.currentLineEdge = 0; p.currentLineInactive = 0; p.breakpointLine = 0;
    p.findMatch = RGBA(230, 180, 60, 100); p.bracketMatch = RGBA(255, 255, 255, 60);
    p.errorLine = RGBA(200, 60, 60, 50); p.errorUnderline = RGBA(240, 80, 80);
    p.breakpoint = RGBA(92, 162, 236);
    p.scrollTrack = RGBA(0, 0, 0, 40); p.scrollThumb = RGBA(120, 130, 150, 110); p.scrollThumbHot = RGBA(150, 160, 180, 170);
    return p;
}

// ================================================================================================
// Text, positions
// ================================================================================================
enum { Kind_Other = 0, Kind_TypeWord, Kind_TypeSpace, Kind_TypePunct, Kind_Backspace, Kind_Delete };

CodeEdit::CodeEdit() {
    lines_.assign(1, std::string());
    pal_ = DefaultCodePalette();
}

const std::string& CodeEdit::Line(int i) const {
    static const std::string empty;
    return (i >= 0 && i < (int)lines_.size()) ? lines_[(size_t)i] : empty;
}

std::string CodeEdit::GetText() const {
    std::string s;
    for (size_t i = 0; i < lines_.size(); ++i) { if (i) s += '\n'; s += lines_[i]; }
    return s;
}

void CodeEdit::SetText(const std::string& text) {
    const std::string t = sanitize(text.data(), text.size());
    lines_.clear();
    size_t a = 0;
    for (;;) {
        const size_t e = t.find('\n', a);
        if (e == std::string::npos) { lines_.push_back(t.substr(a)); break; }
        lines_.push_back(t.substr(a, e - a));
        a = e + 1;
    }
    caret_ = anchor_ = CodePos{};
    undo_.clear(); redo_.clear(); groupOpen_ = false; breakGroup_ = true; compound_ = false; compoundDepth_ = 0;
    extras_.clear(); folds_.clear(); ++foldsVersion_; dedentLine_ = -1; rowsDirty_ = true;
    desiredCol_ = -1; validTo_ = 0; maxDirty_ = true; ++version_;
    scrollX_ = scrollY_ = 0;
    breakpoints_.erase(std::remove_if(breakpoints_.begin(), breakpoints_.end(), [&](int l) { return l < 0 || l >= (int)lines_.size(); }), breakpoints_.end());
}

CodePos CodeEdit::Clamp(CodePos p) const {
    if (p.line < 0) return CodePos{0, 0};
    if (p.line >= (int)lines_.size()) return CodePos{(int)lines_.size() - 1, (int)lines_.back().size()};
    const std::string& l = lines_[(size_t)p.line];
    if (p.col < 0) p.col = 0;
    if (p.col > (int)l.size()) p.col = (int)l.size();
    while (p.col > 0 && p.col < (int)l.size() && isCont((unsigned char)l[(size_t)p.col])) --p.col;
    return p;
}

std::string CodeEdit::textIn(CodePos a, CodePos b) const {
    if (b < a) std::swap(a, b);
    if (a.line == b.line) return lines_[(size_t)a.line].substr((size_t)a.col, (size_t)(b.col - a.col));
    std::string s = lines_[(size_t)a.line].substr((size_t)a.col);
    for (int l = a.line + 1; l < b.line; ++l) { s += '\n'; s += lines_[(size_t)l]; }
    s += '\n';
    s += lines_[(size_t)b.line].substr(0, (size_t)b.col);
    return s;
}

std::string CodeEdit::SelectedText() const { return textIn(SelectionStart(), SelectionEnd()); }

CodePos CodeEdit::endOf(CodePos from, const std::string& text) {
    int nl = 0; size_t last = 0;
    for (size_t i = 0; i < text.size(); ++i) if (text[i] == '\n') { ++nl; last = i + 1; }
    if (nl == 0) return CodePos{from.line, from.col + (int)text.size()};
    return CodePos{from.line + nl, (int)(text.size() - last)};
}

int CodeEdit::ByteToColumn(int line, int byte) const {
    const std::string& s = Line(line);
    int col = 0;
    const int n = (int)s.size();
    for (int i = 0; i < byte && i < n;) {
        col = s[(size_t)i] == '\t' ? nextTab(col, tabSize_) : col + 1;
        i = nextIdx(s, i);
    }
    return col;
}

int CodeEdit::ColumnToByte(int line, float column) const {
    const std::string& s = Line(line);
    const int n = (int)s.size();
    int i = 0, col = 0;
    while (i < n) {
        const int next = s[(size_t)i] == '\t' ? nextTab(col, tabSize_) : col + 1;
        if (column < (float)col + (float)(next - col) * 0.5f) return i;
        col = next;
        i = nextIdx(s, i);
    }
    return n;
}

// ================================================================================================
// Low-level edit and undo
// ================================================================================================
void CodeEdit::invalidateFrom(int line) { if (validTo_ > line) validTo_ = line < 0 ? 0 : line; }

void CodeEdit::applyRaw(CodePos from, CodePos to, const std::string& ins) {
    const int oldN = to.line - from.line + 1;
    for (int l = from.line; l <= to.line; ++l) if ((int)lines_[(size_t)l].size() >= maxBytes_) { maxDirty_ = true; break; }
    const std::string prefix = lines_[(size_t)from.line].substr(0, (size_t)from.col);
    const std::string suffix = lines_[(size_t)to.line].substr((size_t)to.col);
    std::vector<std::string> parts;
    size_t a = 0;
    for (;;) {
        const size_t e = ins.find('\n', a);
        if (e == std::string::npos) { parts.push_back(ins.substr(a)); break; }
        parts.push_back(ins.substr(a, e - a));
        a = e + 1;
    }
    parts.front().insert(0, prefix);
    parts.back() += suffix;
    const int newN = (int)parts.size();
    if (newN == oldN) {
        for (int k = 0; k < newN; ++k) lines_[(size_t)(from.line + k)] = std::move(parts[(size_t)k]);
    } else if (newN < oldN) {
        for (int k = 0; k < newN; ++k) lines_[(size_t)(from.line + k)] = std::move(parts[(size_t)k]);
        lines_.erase(lines_.begin() + from.line + newN, lines_.begin() + from.line + oldN);
    } else {
        for (int k = 0; k < oldN; ++k) lines_[(size_t)(from.line + k)] = std::move(parts[(size_t)k]);
        lines_.insert(lines_.begin() + from.line + oldN, std::make_move_iterator(parts.begin() + oldN), std::make_move_iterator(parts.end()));
    }
    for (int k = 0; k < newN; ++k) maxBytes_ = std::max(maxBytes_, (int)lines_[(size_t)(from.line + k)].size());
    if (newN != oldN) shiftLines(from.line, to.line, newN - oldN);
    // The other carets (and, during an edit at many carets, every caret's place) move with the text: before the edit they stay,
    // inside what was removed they go to its start, after it they shift.
    if (tracking_ || !extras_.empty()) {
        const CodePos endIns = endOf(from, ins);
        const int dl = newN - oldN;
        auto adj = [&](CodePos& q) {
            if (q < from) return;
            if (q < to) { q = from; return; }
            if (q.line == to.line) q = CodePos{endIns.line, endIns.col + (q.col - to.col)};
            else q.line += dl;
        };
        if (tracking_) for (Sel& t : track_) { adj(t.caret); adj(t.anchor); }
        for (Sel& t : extras_) { adj(t.caret); adj(t.anchor); }
    }
    invalidateFrom(from.line);
    ++version_;
}

// Lines moved: those after `removedTo` shift by delta; breakpoints on lines that vanished are dropped.
void CodeEdit::shiftLines(int firstChanged, int removedTo, int delta) {
    auto fix = [&](int l, int& out) {
        if (l > removedTo) { out = l + delta; return true; }
        if (delta < 0 && l >= removedTo + delta + 1) return false;   // one of the removed lines
        out = l;
        return true;
    };
    (void)firstChanged;
    std::vector<int> nb;
    for (int l : breakpoints_) { int o; if (fix(l, o)) nb.push_back(o); }
    std::sort(nb.begin(), nb.end());
    nb.erase(std::unique(nb.begin(), nb.end()), nb.end());
    breakpoints_.swap(nb);
    std::vector<CodeMarker> nm;
    for (CodeMarker& m : markers_) { int o; if (fix(m.line, o)) { m.line = o; nm.push_back(std::move(m)); } }
    markers_.swap(nm);
    if (!folds_.empty()) {   // folds move with their header line (their end is worked out again before the next draw)
        std::vector<FoldRange> nf;
        for (FoldRange f : folds_) { int o; if (fix(f.header, o)) { f.last = std::max(o, f.last + (f.last > removedTo ? delta : 0)); f.header = o; nf.push_back(f); } }
        folds_.swap(nf);
        ++foldsVersion_;
    }
}

// A compound edit is one undo step. They nest (an edit at many carets runs the one-caret edit inside its own): only the outer
// one opens and closes the group. `kind` is a kind of typing: a run of the same typing at many carets stays one step too.
void CodeEdit::beginCompound(int kind) {
    if (compoundDepth_++ > 0) return;
    compound_ = true;
    if (kind != Kind_Other && groupOpen_ && !breakGroup_ && curKind_ == kind && !undo_.empty()) { pushedGroup_ = false; return; }
    UndoGroup g; g.caretBefore = caret_; g.anchorBefore = anchor_; g.kind = kind; g.extrasBefore = extras_;
    undo_.push_back(std::move(g));
    if (undo_.size() > 2000) undo_.erase(undo_.begin());
    groupOpen_ = true; curKind_ = kind; pushedGroup_ = true;
}

void CodeEdit::endCompound() {
    if (compoundDepth_ <= 0 || --compoundDepth_ > 0) return;
    compound_ = false;
    breakGroup_ = curKind_ == Kind_Other;
    if (pushedGroup_ && !undo_.empty()) {
        if (undo_.back().edits.empty()) { undo_.pop_back(); groupOpen_ = false; }
        else redo_.clear();   // (only an edit drops the redo steps: moving many carets does not)
    }
}

CodePos CodeEdit::edit(int kind, CodePos from, CodePos to, const std::string& ins) {
    std::string removed = textIn(from, to);
    if (removed.empty() && ins.empty()) return from;
    const bool newGroup = !groupOpen_ || (!compound_ && (breakGroup_ || kind != curKind_ || kind == Kind_Other));
    if (newGroup) {
        redo_.clear();
        UndoGroup g; g.caretBefore = caret_; g.anchorBefore = anchor_; g.kind = kind; g.extrasBefore = extras_;
        undo_.push_back(std::move(g));
        if (undo_.size() > 2000) undo_.erase(undo_.begin());
        groupOpen_ = true; curKind_ = kind;
    }
    breakGroup_ = false;
    undo_.back().edits.push_back(UndoEdit{from, std::move(removed), ins});
    applyRaw(from, to, ins);
    return endOf(from, ins);
}

// Record where the caret ended up, so Redo can put it back.
#define CE_FINISH() do { if (groupOpen_ && !undo_.empty()) { undo_.back().caretAfter = caret_; undo_.back().anchorAfter = anchor_; undo_.back().extrasAfter = extras_; } } while (0)

bool CodeEdit::replaceSelection(const std::string& ins, int kind) {
    if (readOnly_) return false;
    const CodePos a = SelectionStart(), b = SelectionEnd();
    if (a == b && ins.empty()) return false;
    const bool sel = a != b;
    if (sel) beginCompound();
    const CodePos e = edit(sel ? Kind_Other : kind, a, b, ins);
    if (sel) endCompound();
    caret_ = anchor_ = e; reveal_ = true; desiredCol_ = -1;
    CE_FINISH();
    return true;
}

// ================================================================================================
// Many carets
// ================================================================================================
// Run a one-caret edit at every caret as one undo step. The carets are taken one by one (bit 0 of `order`: from the top,
// else from the bottom); every caret's place is kept up to date while the others edit (applyRaw moves track_). Bit 1: a line
// command, done once per line (a caret whose lines a caret before it already did, before or after its edit, is skipped).
// While the edit runs the extra carets are empty, so the public edit called inside takes its one-caret path.
template <class F> bool CodeEdit::forEachCaret(int kind, int order, F&& op) {
    if (extras_.empty()) return op();
    if (readOnly_) return false;
    normalizeSels();
    if (extras_.empty()) return op();
    std::vector<Sel> all;
    all.reserve(extras_.size() + 1);
    all.push_back(Sel{caret_, anchor_, desiredCol_});
    for (const Sel& e : extras_) all.push_back(e);
    std::vector<int> idx(all.size());
    for (size_t i = 0; i < idx.size(); ++i) idx[i] = (int)i;
    auto startOf = [&](int i) { return all[(size_t)i].caret < all[(size_t)i].anchor ? all[(size_t)i].caret : all[(size_t)i].anchor; };
    std::sort(idx.begin(), idx.end(), [&](int a, int b) { return startOf(a) < startOf(b); });
    std::vector<int> rank(all.size());
    for (size_t k = 0; k < idx.size(); ++k) rank[(size_t)idx[k]] = (int)k;   // the caret's place in text order (Paste uses it)
    if (!(order & 1)) std::reverse(idx.begin(), idx.end());
    const unsigned v0 = version_;
    beginCompound(kind);
    track_ = all; tracking_ = true; extras_.clear();
    std::vector<std::pair<int, int>> done;   // line ranges a line command already did
    bool any = false;
    for (int i : idx) {
        caret_ = track_[(size_t)i].caret; anchor_ = track_[(size_t)i].anchor; desiredCol_ = track_[(size_t)i].desired;
        int l0 = 0, l1 = 0;
        if (order & 2) {
            lineRange(l0, l1);
            bool skip = false;
            for (const auto& d : done) if (l0 <= d.second && d.first <= l1) { skip = true; break; }
            if (skip) continue;
            done.emplace_back(l0, l1);
        }
        opRank_ = rank[(size_t)i];
        if (op()) any = true;
        track_[(size_t)i].caret = caret_; track_[(size_t)i].anchor = anchor_; track_[(size_t)i].desired = desiredCol_;
        if (order & 2) { int n0, n1; lineRange(n0, n1); done.emplace_back(n0, n1); }
    }
    tracking_ = false;
    caret_ = Clamp(track_[0].caret); anchor_ = Clamp(track_[0].anchor); desiredCol_ = track_[0].desired;
    extras_.assign(track_.begin() + 1, track_.end());
    track_.clear();
    opRank_ = 0;
    endCompound();
    normalizeSels();
    if (version_ != v0) CE_FINISH();
    reveal_ = true;
    return any;
}

// Clamp every caret, sort them, merge the ones that overlap (or sit on the same place); the primary stays primary.
void CodeEdit::normalizeSels() {
    if (extras_.empty()) return;
    struct E { Sel s; bool primary; };
    std::vector<E> v;
    v.reserve(extras_.size() + 1);
    v.push_back(E{Sel{Clamp(caret_), Clamp(anchor_), desiredCol_}, true});
    for (const Sel& e : extras_) v.push_back(E{Sel{Clamp(e.caret), Clamp(e.anchor), e.desired}, false});
    auto lo = [](const Sel& x) { return x.caret < x.anchor ? x.caret : x.anchor; };
    auto hi = [](const Sel& x) { return x.caret < x.anchor ? x.anchor : x.caret; };
    std::stable_sort(v.begin(), v.end(), [&](const E& a, const E& b) { return lo(a.s) < lo(b.s); });
    std::vector<E> out;
    for (const E& e : v) {
        if (!out.empty()) {
            E& b = out.back();
            const CodePos bs = lo(b.s), be = hi(b.s), es = lo(e.s), ee = hi(e.s);
            if (es < be || (es == be && (b.s.caret == b.s.anchor || e.s.caret == e.s.anchor))) {
                const CodePos ns = bs, ne = be < ee ? ee : be;
                const bool fwd = !(b.s.caret < b.s.anchor);
                b.s.anchor = fwd ? ns : ne; b.s.caret = fwd ? ne : ns;
                if (e.primary) { b.primary = true; b.s.desired = e.s.desired; }
                continue;
            }
        }
        out.push_back(e);
    }
    extras_.clear();
    for (const E& e : out) {
        if (e.primary) { caret_ = e.s.caret; anchor_ = e.s.anchor; desiredCol_ = e.s.desired; }
        else extras_.push_back(e.s);
    }
}

std::vector<CodeSelection> CodeEdit::Selections() const {
    std::vector<CodeSelection> v;
    v.push_back(CodeSelection{caret_, anchor_});
    for (const Sel& e : extras_) v.push_back(CodeSelection{e.caret, e.anchor});
    std::sort(v.begin(), v.end(), [](const CodeSelection& a, const CodeSelection& b) { return a.Start() < b.Start(); });
    return v;
}

void CodeEdit::AddCaret(CodePos caret, CodePos anchor) {
    extras_.push_back(Sel{caret_, anchor_, desiredCol_});
    caret_ = Clamp(caret); anchor_ = Clamp(anchor); desiredCol_ = -1;
    breakGroup_ = true; reveal_ = true;
    unfoldAround(caret_.line);
    normalizeSels();
}

bool CodeEdit::RemoveCaretAt(CodePos p) {
    if (extras_.empty()) return false;
    p = Clamp(p);
    for (size_t i = 0; i < extras_.size(); ++i)
        if (extras_[i].caret == p) { extras_.erase(extras_.begin() + (long)i); return true; }
    if (caret_ == p) {   // the primary goes: the last added extra one takes its place
        caret_ = extras_.back().caret; anchor_ = extras_.back().anchor; desiredCol_ = extras_.back().desired;
        extras_.pop_back();
        return true;
    }
    return false;
}

void CodeEdit::ClearExtraCarets() { extras_.clear(); }

// The selection is exactly a word: letters, digits and '_' only, with no word character just before or after it.
bool CodeEdit::selectionIsWord() const {
    const CodePos a = SelectionStart(), b = SelectionEnd();
    if (a.line != b.line || a == b) return false;
    const std::string& s = lines_[(size_t)a.line];
    for (int i = a.col; i < b.col; ++i) if (charClass(s, i) != 1) return false;
    if (a.col > 0 && charClass(s, prevIdx(s, a.col)) == 1) return false;
    if (b.col < (int)s.size() && charClass(s, b.col) == 1) return false;
    return true;
}

bool CodeEdit::SelectNextOccurrence() {
    if (!HasSelection()) {
        CodePos a, b;
        if (!wordAt(caret_, a, b) || charClass(lines_[(size_t)a.line], a.col) != 1) return false;
        anchor_ = a; caret_ = b; desiredCol_ = -1; breakGroup_ = true; reveal_ = true;
        normalizeSels();   // (an extra caret inside the word merges with it)
        return true;
    }
    const std::string needle = SelectedText();
    const bool whole = selectionIsWord();
    std::vector<std::string> parts;
    for (size_t a = 0;;) { const size_t e = needle.find('\n', a); if (e == std::string::npos) { parts.push_back(needle.substr(a)); break; } parts.push_back(needle.substr(a, e - a)); a = e + 1; }
    const int n = (int)lines_.size();
    auto taken = [&](CodePos a) {
        if (SelectionStart() == a) return true;
        for (const Sel& e : extras_) if ((e.caret < e.anchor ? e.caret : e.anchor) == a) return true;
        return false;
    };
    // a match of the needle (case-sensitive) starting at (l, i): its end
    auto matchHere = [&](int l, int i, CodePos& end) {
        const std::string& s = lines_[(size_t)l];
        if (parts.size() == 1) {
            if (s.compare((size_t)i, parts[0].size(), parts[0]) != 0) return false;
            end = CodePos{l, i + (int)parts[0].size()};
            if (whole && ((i > 0 && charClass(s, prevIdx(s, i)) == 1) || (end.col < (int)s.size() && charClass(s, end.col) == 1))) return false;
            return true;
        }
        if ((int)s.size() - i != (int)parts[0].size() || s.compare((size_t)i, std::string::npos, parts[0]) != 0) return false;
        const int k = (int)parts.size();
        if (l + k - 1 >= n) return false;
        for (int m = 1; m + 1 < k; ++m) if (lines_[(size_t)(l + m)] != parts[(size_t)m]) return false;
        if (lines_[(size_t)(l + k - 1)].compare(0, parts.back().size(), parts.back()) != 0) return false;
        end = CodePos{l + k - 1, (int)parts.back().size()};
        return true;
    };
    const CodePos from = SelectionEnd();
    for (int k = 0; k <= n; ++k) {
        const int l = (from.line + k) % n;
        const std::string& s = lines_[(size_t)l];
        const int c0 = k == 0 ? from.col : 0;
        const int c1 = k == n ? std::min(from.col, (int)s.size()) : (int)s.size();
        for (int i = c0; i <= c1; i = nextIdx(s, i)) {
            CodePos end;
            if (i < (int)s.size() + (parts.size() > 1 ? 1 : 0) && matchHere(l, i, end) && !taken(CodePos{l, i})) {
                AddCaret(end, CodePos{l, i});
                return true;
            }
            if (i >= (int)s.size()) break;
        }
    }
    return false;
}

void CodeEdit::setBox(int l0, float c0, int l1, float c1) {
    const int n = (int)lines_.size();
    l0 = std::min(std::max(l0, 0), n - 1); l1 = std::min(std::max(l1, 0), n - 1);
    extras_.clear();
    const int step = l0 <= l1 ? 1 : -1;
    for (int l = l0;; l += step) {
        const Sel s{CodePos{l, ColumnToByte(l, c1)}, CodePos{l, ColumnToByte(l, c0)}, -1};
        if (l == l1) { caret_ = s.caret; anchor_ = s.anchor; break; }
        extras_.push_back(s);
    }
    desiredCol_ = -1; breakGroup_ = true; reveal_ = true;
}

void CodeEdit::SetColumnSelection(CodePos from, CodePos to) {
    from = Clamp(from); to = Clamp(to);
    setBox(from.line, (float)ByteToColumn(from.line, from.col), to.line, (float)ByteToColumn(to.line, to.col));
}

bool CodeEdit::MoveSelectionTo(CodePos at, bool copy) {
    if (readOnly_ || !HasSelection()) return false;
    const CodePos a = SelectionStart(), b = SelectionEnd();
    at = Clamp(at);
    if (a <= at && at <= b) return false;
    const std::string text = SelectedText();
    extras_.clear();
    beginCompound();
    CodePos na = at;
    if (copy) edit(Kind_Other, at, at, text);
    else if (at < a) { edit(Kind_Other, a, b, ""); edit(Kind_Other, at, at, text); }
    else {   // after the selection: insert first (the selection does not move), then remove it; `at` moves up with the text
        edit(Kind_Other, at, at, text);
        edit(Kind_Other, a, b, "");
        na = at.line == b.line ? CodePos{a.line, a.col + (at.col - b.col)} : CodePos{at.line - (b.line - a.line), at.col};
    }
    endCompound();
    anchor_ = na; caret_ = endOf(na, text); reveal_ = true; desiredCol_ = -1;
    CE_FINISH();
    return true;
}

bool CodeEdit::InsertText(const char* text) {
    if (readOnly_) return false;
    const std::string s = sanitize(text);
    if (s.empty() && !HasSelection() && extras_.empty()) return false;
    int kind = Kind_Other;
    if (!s.empty() && s.size() <= 4 && s.find('\n') == std::string::npos && s.find('\t') == std::string::npos && nextIdx(s, 0) == (int)s.size())
        kind = s[0] == ' ' ? Kind_TypeSpace : (charClass(s, 0) == 1 ? Kind_TypeWord : Kind_TypePunct);
    return forEachCaret(kind, 0, [&] { return (s.empty() && !HasSelection()) ? false : replaceSelection(s, kind); });
}

// ================================================================================================
// Typing: auto-close and auto-dedent
// ================================================================================================
namespace {
char closerOf(char c) { switch (c) { case '(': return ')'; case '[': return ']'; case '{': return '}'; case '"': return '"'; case '\'': return '\''; default: return 0; } }
bool isCloser(char c) { return c == ')' || c == ']' || c == '}' || c == '"' || c == '\''; }
bool wordByte(unsigned char c) { return c == '_' || c >= 0x80 || (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
}

// The caret is in code: not inside a string, a character literal or a comment.
bool CodeEdit::inCodeText(CodePos p) {
    if (!highlighter_) return true;
    std::vector<CodeToken> tok;
    LineTokens(p.line, tok);
    const int len = (int)lines_[(size_t)p.line].size();
    for (const CodeToken& t : tok) {
        if (t.color != CodeColor_String && t.color != CodeColor_Char && t.color != CodeColor_Comment) continue;
        if (t.start < p.col && p.col < t.end) return false;
        // at the end of the line, still inside a comment or a string that is not closed
        if (t.start < p.col && p.col == t.end && t.end == len) {
            if (t.color == CodeColor_Comment) return false;
            const char q = lines_[(size_t)p.line][(size_t)(t.end - 1)];
            if (t.end - t.start < 2 || (q != '"' && q != '\'')) return false;
        }
    }
    return true;
}

bool CodeEdit::Type(const char* text) {
    if (readOnly_) return false;
    const std::string s = sanitize(text);
    if (s.empty()) return false;
    bool any = false;
    for (int i = 0; i < (int)s.size();) {
        const int j = nextIdx(s, i);
        const std::string ch = s.substr((size_t)i, (size_t)(j - i));
        if (ch == "\n") { if (NewLine()) any = true; }
        else {
            const int kind = ch == "\t" ? Kind_Other : (ch == " " ? Kind_TypeSpace : (charClass(ch, 0) == 1 ? Kind_TypeWord : Kind_TypePunct));
            if (forEachCaret(kind, 0, [&] { return typeOne(ch); })) any = true;
        }
        i = j;
    }
    return any;
}

bool CodeEdit::typeOne(const std::string& ch) {
    const char c = ch.size() == 1 ? ch[0] : 0;
    const int kind = ch == "\t" ? Kind_Other : (ch == " " ? Kind_TypeSpace : (charClass(ch, 0) == 1 ? Kind_TypeWord : Kind_TypePunct));
    // "end" became the start of a longer word ("ending"): put back the indent the dedent took
    if (dedentLine_ == caret_.line && dedentVersion_ == version_ && !HasSelection() && c && wordByte((unsigned char)c)) {
        const std::string& L = lines_[(size_t)caret_.line];
        int fnb = 0; while (fnb < (int)L.size() && (L[(size_t)fnb] == ' ' || L[(size_t)fnb] == '\t')) ++fnb;
        edit(kind, CodePos{caret_.line, 0}, CodePos{caret_.line, fnb}, dedentText_);
        caret_.col += (int)dedentText_.size() - fnb; anchor_ = caret_;
    }
    dedentLine_ = -1;
    if (autoClose_ && c) {
        const std::string& L = lines_[(size_t)caret_.line];
        const bool sel = HasSelection();
        const int at = caret_.col;
        const char next = (!sel && at < (int)L.size()) ? L[(size_t)at] : 0;
        const CodePos ss = SelectionStart();
        const char prev = ss.col > 0 ? lines_[(size_t)ss.line][(size_t)ss.col - 1] : 0;
        const char close = closerOf(c);
        if (!sel && isCloser(c) && next == c) {   // type over the closer that is already there
            caret_.col = at + 1; anchor_ = caret_; reveal_ = true; desiredCol_ = -1; breakGroup_ = true;
            return true;
        }
        if (close && sel) {   // wrap the selection: (selection), "selection"
            const CodePos a = SelectionStart(), b = SelectionEnd();
            const bool fwd = !(caret_ < anchor_);
            beginCompound();
            edit(Kind_Other, b, b, std::string(1, close));
            edit(Kind_Other, a, a, ch);
            endCompound();
            const CodePos na{a.line, a.col + 1}, nb{b.line, b.line == a.line ? b.col + 1 : b.col};
            anchor_ = fwd ? na : nb; caret_ = fwd ? nb : na; reveal_ = true; desiredCol_ = -1;
            CE_FINISH();
            return true;
        }
        if (close && !sel && inCodeText(caret_)) {
            const bool roomAfter = next == 0 || next == ' ' || next == '\t' || std::strchr(")]};,", next) != nullptr;
            const bool quote = c == '"' || c == '\'';
            if (roomAfter && !(quote && wordByte((unsigned char)prev))) {
                edit(kind, caret_, caret_, ch + std::string(1, close));
                caret_ = anchor_ = CodePos{caret_.line, at + 1}; reveal_ = true; desiredCol_ = -1;
                CE_FINISH();
                return true;
            }
        }
    }
    const bool r = replaceSelection(ch, kind);
    if (!r || !autoDedent_ || !c) return r;
    // auto-dedent: '}' or a dedent word as the first thing on the line
    const int ln = caret_.line;
    const std::string L = lines_[(size_t)ln];
    int fnb = 0; while (fnb < (int)L.size() && (L[(size_t)fnb] == ' ' || L[(size_t)fnb] == '\t')) ++fnb;
    if (fnb == 0) return r;
    std::string target;
    bool wordDedent = false;
    if (c == '}' && fnb == caret_.col - 1) {
        // the line of the matching '{' gives the indent
        const CodePos sc = caret_;
        caret_.col -= 1;
        CodePos a, b;
        const bool found = matchBracketImpl(a, b);
        caret_ = sc;
        if (found) {
            const std::string& M = lines_[(size_t)b.line];
            int m = 0; while (m < (int)M.size() && (M[(size_t)m] == ' ' || M[(size_t)m] == '\t')) ++m;
            target = M.substr(0, (size_t)m);
        } else {
            target = L.substr(0, (size_t)fnb);
            if (!target.empty() && target.back() == '\t') target.pop_back();
            else target.erase(0, std::min((size_t)tabSize_, target.size()));
        }
    } else if (wordByte((unsigned char)c) && haveLanguage_ && !language_.dedentWords.empty() &&
               (caret_.col >= (int)L.size() || !wordByte((unsigned char)L[(size_t)caret_.col]))) {
        const std::string w = L.substr((size_t)fnb, (size_t)(caret_.col - fnb));
        if (std::find(language_.dedentWords.begin(), language_.dedentWords.end(), w) == language_.dedentWords.end()) return r;
        // only once: not when the line is already left of the line above it
        int pl = ln - 1;
        while (pl >= 0 && lines_[(size_t)pl].find_first_not_of(" \t") == std::string::npos) --pl;
        if (pl >= 0 && indentOf(ln) < indentOf(pl)) return r;
        target = L.substr(0, (size_t)fnb);
        if (!target.empty() && target.back() == '\t') target.pop_back();
        else { int k = 0; while (k < tabSize_ && !target.empty() && target.back() == ' ') { target.pop_back(); ++k; } }
        wordDedent = true;
    } else return r;
    if (target == L.substr(0, (size_t)fnb)) return r;
    edit(kind, CodePos{ln, 0}, CodePos{ln, fnb}, target);
    caret_.col += (int)target.size() - fnb; anchor_ = caret_;
    CE_FINISH();
    if (wordDedent) { dedentLine_ = ln; dedentText_ = L.substr(0, (size_t)fnb); dedentVersion_ = version_; }
    return r;
}

// Display columns of a line's leading blanks (-1 for a blank line).
int CodeEdit::indentOf(int line) const {
    const std::string& s = Line(line);
    int col = 0;
    for (char ch : s) {
        if (ch == ' ') ++col;
        else if (ch == '\t') col = nextTab(col, tabSize_);
        else return col;
    }
    return -1;
}

bool CodeEdit::Backspace(bool word) {
    if (readOnly_) return false;
    if (!extras_.empty()) return forEachCaret(word ? Kind_Other : Kind_Backspace, 0, [&] { return Backspace(word); });
    if (HasSelection()) return replaceSelection("", Kind_Other);
    if (caret_.line == 0 && caret_.col == 0) return false;
    if (autoClose_ && !word && caret_.col > 0) {   // between a pair: "(|)" loses both
        const std::string& L = lines_[(size_t)caret_.line];
        if (caret_.col < (int)L.size()) {
            const char p = L[(size_t)caret_.col - 1], q = L[(size_t)caret_.col];
            if (closerOf(p) && closerOf(p) == q) {
                const CodePos from{caret_.line, caret_.col - 1};
                edit(Kind_Backspace, from, CodePos{caret_.line, caret_.col + 1}, "");
                caret_ = anchor_ = from; reveal_ = true; desiredCol_ = -1;
                CE_FINISH();
                return true;
            }
        }
    }
    CodePos from = caret_;
    if (word) from = wordLeft(caret_);
    else if (from.col > 0) from.col = prevIdx(lines_[(size_t)from.line], from.col);
    else { from.line--; from.col = (int)lines_[(size_t)from.line].size(); }
    edit(word ? Kind_Other : Kind_Backspace, from, caret_, "");
    caret_ = anchor_ = from; reveal_ = true; desiredCol_ = -1;
    CE_FINISH();
    return true;
}

bool CodeEdit::DeleteForward(bool word) {
    if (readOnly_) return false;
    if (!extras_.empty()) return forEachCaret(word ? Kind_Other : Kind_Delete, 0, [&] { return DeleteForward(word); });
    if (HasSelection()) return replaceSelection("", Kind_Other);
    const std::string& l = lines_[(size_t)caret_.line];
    CodePos to = caret_;
    if (word) to = wordRight(caret_);
    else if (to.col < (int)l.size()) to.col = nextIdx(l, to.col);
    else if (to.line + 1 < (int)lines_.size()) { to.line++; to.col = 0; }
    else return false;
    if (to == caret_) return false;
    edit(word ? Kind_Other : Kind_Delete, caret_, to, "");
    anchor_ = caret_; reveal_ = true; desiredCol_ = -1;
    CE_FINISH();
    return true;
}

bool CodeEdit::NewLine() {
    if (readOnly_) return false;
    if (!extras_.empty()) return forEachCaret(Kind_Other, 0, [&] { return NewLine(); });
    beginCompound();
    const CodePos a = SelectionStart(), b = SelectionEnd();
    if (a != b) edit(Kind_Other, a, b, "");
    const std::string L = lines_[(size_t)a.line];
    std::string ins = "\n";
    CodePos at;
    if (autoIndent_) {
        int n = 0;
        while (n < (int)L.size() && (L[(size_t)n] == ' ' || L[(size_t)n] == '\t')) ++n;
        n = std::min(n, a.col);
        const std::string indent = L.substr(0, (size_t)n);
        int bi = a.col - 1; while (bi >= 0 && (L[(size_t)bi] == ' ' || L[(size_t)bi] == '\t')) --bi;
        const char before = bi >= 0 ? L[(size_t)bi] : 0;
        int ai = a.col; while (ai < (int)L.size() && (L[(size_t)ai] == ' ' || L[(size_t)ai] == '\t')) ++ai;
        const char after = ai < (int)L.size() ? L[(size_t)ai] : 0;
        const bool opener = isOpener(before);
        // a block word before the caret also opens a level (Lua: `then`, `do`, `function (...)`)
        int oc = 0;
        const bool wordOpen = !opener && haveLanguage_ && !language_.blockOpenWords.empty() && lineOpens(a.line, oc, a.col, true);
        ins += indent;
        if (opener || wordOpen) ins += insertSpaces_ ? std::string((size_t)tabSize_, ' ') : std::string("\t");
        at = endOf(a, ins);
        if (opener && after == partnerOf(before)) ins += "\n" + indent;
    } else at = endOf(a, ins);
    edit(Kind_Other, a, a, ins);
    endCompound();
    caret_ = anchor_ = at; reveal_ = true; desiredCol_ = -1;
    CE_FINISH();
    return true;
}

bool CodeEdit::Tab(bool outdent) {
    if (readOnly_) return false;
    if (!extras_.empty()) {
        bool lines = outdent;
        for (const CodeSelection& s : Selections()) if (s.Start().line != s.End().line) lines = true;
        return forEachCaret(Kind_Other, lines ? 2 : 0, [&] { return Tab(outdent); });
    }
    const CodePos a = SelectionStart(), b = SelectionEnd();
    const bool multi = a.line != b.line;
    if (!outdent && !multi) {
        std::string unit = "\t";
        if (insertSpaces_) {
            const int col = ByteToColumn(a.line, a.col);
            unit.assign((size_t)(tabSize_ - col % tabSize_), ' ');
        }
        return replaceSelection(unit, Kind_Other);
    }
    int l0 = a.line, l1 = b.line;
    if (multi && b.col == 0) --l1;
    std::vector<int> delta((size_t)(l1 - l0 + 1), 0);
    const unsigned v0 = version_;
    beginCompound();
    const std::string unit = insertSpaces_ ? std::string((size_t)tabSize_, ' ') : std::string("\t");
    for (int l = l1; l >= l0; --l) {
        const std::string& s = lines_[(size_t)l];
        if (!outdent) {
            if (s.empty()) continue;
            edit(Kind_Other, CodePos{l, 0}, CodePos{l, 0}, unit);
            delta[(size_t)(l - l0)] = (int)unit.size();
        } else {
            int r = 0;
            if (!s.empty() && s[0] == '\t') r = 1; else while (r < tabSize_ && r < (int)s.size() && s[(size_t)r] == ' ') ++r;
            if (r == 0) continue;
            edit(Kind_Other, CodePos{l, 0}, CodePos{l, r}, "");
            delta[(size_t)(l - l0)] = -r;
        }
    }
    endCompound();
    if (version_ == v0) return false;
    auto adj = [&](CodePos p) {
        if (p.line >= l0 && p.line <= l1) {
            const int d = delta[(size_t)(p.line - l0)];
            if (d > 0 && p.col > 0) p.col += d; else if (d < 0) p.col -= std::min(-d, p.col);
        }
        return p;
    };
    caret_ = Clamp(adj(caret_)); anchor_ = Clamp(adj(anchor_)); reveal_ = true; desiredCol_ = -1;
    CE_FINISH();
    return true;
}

void CodeEdit::lineRange(int& l0, int& l1) const {
    const CodePos a = SelectionStart(), b = SelectionEnd();
    l0 = a.line; l1 = b.line;
    if (l1 > l0 && b.col == 0) --l1;
}

bool CodeEdit::ToggleLineComment(const std::string& prefix) {
    if (readOnly_) return false;
    if (!extras_.empty()) return forEachCaret(Kind_Other, 2, [&] { return ToggleLineComment(prefix); });
    std::string pre = prefix;
    if (pre.empty()) pre = (haveLanguage_ && !language_.lineComments.empty()) ? language_.lineComments.front() : std::string("//");
    int l0, l1;
    lineRange(l0, l1);
    const int n = (int)pre.size();
    auto first = [&](int l) { const std::string& s = lines_[(size_t)l]; size_t f = s.find_first_not_of(" \t"); return f == std::string::npos ? -1 : (int)f; };
    bool any = false, all = true;
    for (int l = l0; l <= l1; ++l) {
        const int f = first(l);
        if (f < 0) continue;
        any = true;
        if (lines_[(size_t)l].compare((size_t)f, (size_t)n, pre) != 0) all = false;
    }
    if (!any) return false;
    std::vector<int> delta((size_t)(l1 - l0 + 1), 0), at((size_t)(l1 - l0 + 1), 0);
    beginCompound();
    for (int l = l1; l >= l0; --l) {
        const int f = first(l);
        if (f < 0) continue;
        at[(size_t)(l - l0)] = f;
        if (all) {
            const bool sp = f + n < (int)lines_[(size_t)l].size() && lines_[(size_t)l][(size_t)(f + n)] == ' ';
            const int r = n + (sp ? 1 : 0);
            edit(Kind_Other, CodePos{l, f}, CodePos{l, f + r}, "");
            delta[(size_t)(l - l0)] = -r;
        } else {
            edit(Kind_Other, CodePos{l, f}, CodePos{l, f}, pre + " ");
            delta[(size_t)(l - l0)] = n + 1;
        }
    }
    endCompound();
    auto adj = [&](CodePos p) {
        if (p.line < l0 || p.line > l1) return p;
        const int d = delta[(size_t)(p.line - l0)], f = at[(size_t)(p.line - l0)];
        if (d > 0 && p.col >= f) p.col += d; else if (d < 0 && p.col > f) p.col = std::max(f, p.col + d);
        return p;
    };
    caret_ = Clamp(adj(caret_)); anchor_ = Clamp(adj(anchor_)); reveal_ = true; desiredCol_ = -1;
    CE_FINISH();
    return true;
}

bool CodeEdit::DuplicateLines() {
    if (readOnly_) return false;
    if (!extras_.empty()) return forEachCaret(Kind_Other, 2, [&] { return DuplicateLines(); });
    int l0, l1;
    lineRange(l0, l1);
    std::string block;
    for (int l = l0; l <= l1; ++l) { block += '\n'; block += lines_[(size_t)l]; }
    const int count = l1 - l0 + 1;
    beginCompound();
    edit(Kind_Other, CodePos{l1, (int)lines_[(size_t)l1].size()}, CodePos{l1, (int)lines_[(size_t)l1].size()}, block);
    endCompound();
    caret_.line += count; anchor_.line += count;
    caret_ = Clamp(caret_); anchor_ = Clamp(anchor_); reveal_ = true; desiredCol_ = -1;
    CE_FINISH();
    return true;
}

bool CodeEdit::DeleteLines() {
    if (readOnly_) return false;
    if (!extras_.empty()) return forEachCaret(Kind_Other, 2, [&] { return DeleteLines(); });
    int l0, l1;
    lineRange(l0, l1);
    if (l0 == 0 && l1 == (int)lines_.size() - 1 && lines_[0].empty() && l1 == 0) return false;
    CodePos from{l0, 0}, to{l1 + 1, 0};
    if (l1 + 1 >= (int)lines_.size()) {
        to = CodePos{l1, (int)lines_[(size_t)l1].size()};
        if (l0 > 0) from = CodePos{l0 - 1, (int)lines_[(size_t)l0 - 1].size()};
    }
    beginCompound();
    edit(Kind_Other, from, to, "");
    endCompound();
    const int line = std::min(l0, (int)lines_.size() - 1);
    caret_ = anchor_ = CodePos{line, 0}; reveal_ = true; desiredCol_ = -1;
    CE_FINISH();
    return true;
}

bool CodeEdit::MoveLines(bool up) {
    if (readOnly_) return false;
    if (!extras_.empty()) return forEachCaret(Kind_Other, up ? 3 : 2, [&] { return MoveLines(up); });
    int l0, l1;
    lineRange(l0, l1);
    const int last = (int)lines_.size() - 1;
    if (up ? l0 == 0 : l1 == last) return false;
    beginCompound();
    if (up) {
        const std::string s = lines_[(size_t)l0 - 1];
        edit(Kind_Other, CodePos{l0 - 1, 0}, CodePos{l0, 0}, "");
        const int e = l1 - 1;
        edit(Kind_Other, CodePos{e, (int)lines_[(size_t)e].size()}, CodePos{e, (int)lines_[(size_t)e].size()}, "\n" + s);
    } else {
        const std::string s = lines_[(size_t)l1 + 1];
        edit(Kind_Other, CodePos{l1, (int)lines_[(size_t)l1].size()}, CodePos{l1 + 1, (int)s.size()}, "");
        edit(Kind_Other, CodePos{l0, 0}, CodePos{l0, 0}, s + "\n");
    }
    endCompound();
    const int d = up ? -1 : 1;
    caret_.line += d; anchor_.line += d;
    caret_ = Clamp(caret_); anchor_ = Clamp(anchor_); reveal_ = true; desiredCol_ = -1;
    CE_FINISH();
    return true;
}

// With many carets: the selected texts in text order, one per line.
static std::string joinedSelections(const CodeEdit& e) {
    std::string s;
    bool first = true;
    for (const CodeSelection& c : e.Selections()) {
        if (c.Empty()) continue;
        if (!first) s += '\n';
        first = false;
        const CodePos a = c.Start(), b = c.End();
        for (int l = a.line; l <= b.line; ++l) {
            const std::string& t = e.Line(l);
            const int c0 = l == a.line ? a.col : 0, c1 = l == b.line ? b.col : (int)t.size();
            s.append(t, (size_t)c0, (size_t)(c1 - c0));
            if (l != b.line) s += '\n';
        }
    }
    return s;
}

bool CodeEdit::Copy() const {
    bool any = HasSelection();
    for (const Sel& e : extras_) if (e.caret != e.anchor) any = true;
    if (!any) return false;
    const std::string t = extras_.empty() ? SelectedText() : joinedSelections(*this);
    if (clipSet_) { clipSet_(t); return true; }   // the host's clipboard (no size limit)
    SetClipboardText(t.c_str());
    return true;
}

bool CodeEdit::Cut() {
    if (readOnly_) return false;
    if (!extras_.empty()) {
        const std::string t = joinedSelections(*this);
        if (t.empty()) return false;
        if (clipSet_) clipSet_(t);
        else { if ((int)t.size() >= kClipboardMax) return false; SetClipboardText(t.c_str()); }
        return forEachCaret(Kind_Other, 0, [&] { return HasSelection() ? replaceSelection("", Kind_Other) : false; });
    }
    if (!HasSelection()) return false;
    const std::string s = SelectedText();
    if (clipSet_) clipSet_(s);
    else {
        if ((int)s.size() >= kClipboardMax) return false;   // OkayUI's clipboard could not hold it all: never delete what was not copied
        SetClipboardText(s.c_str());
    }
    return replaceSelection("", Kind_Other);
}

bool CodeEdit::Paste() {
    if (readOnly_) return false;
    std::string held;
    const char* c = nullptr;
    if (clipGet_) { held = clipGet_(); c = held.c_str(); }
    else c = GetClipboardText();
    if (!c || !*c) return false;
    const std::string s = sanitize(c);
    if (!extras_.empty()) {   // as many lines as carets: one line each, else the whole text at every caret
        std::vector<std::string> parts;
        for (size_t a = 0;;) { const size_t e = s.find('\n', a); if (e == std::string::npos) { parts.push_back(s.substr(a)); break; } parts.push_back(s.substr(a, e - a)); a = e + 1; }
        const bool spread = (int)parts.size() == CaretCount();
        return forEachCaret(Kind_Other, 0, [&] { return replaceSelection(spread ? parts[(size_t)opRank_] : s, Kind_Other); });
    }
    return replaceSelection(s, Kind_Other);
}

bool CodeEdit::Undo() {
    if (undo_.empty()) return false;
    UndoGroup g = std::move(undo_.back());
    undo_.pop_back();
    for (size_t i = g.edits.size(); i-- > 0;) {
        const UndoEdit& e = g.edits[i];
        applyRaw(e.from, endOf(e.from, e.inserted), e.removed);
    }
    caret_ = Clamp(g.caretBefore); anchor_ = Clamp(g.anchorBefore);
    extras_ = g.extrasBefore;
    for (Sel& e : extras_) { e.caret = Clamp(e.caret); e.anchor = Clamp(e.anchor); }
    redo_.push_back(std::move(g));
    groupOpen_ = false; breakGroup_ = true; reveal_ = true; desiredCol_ = -1;
    return true;
}

bool CodeEdit::Redo() {
    if (redo_.empty()) return false;
    UndoGroup g = std::move(redo_.back());
    redo_.pop_back();
    for (const UndoEdit& e : g.edits) applyRaw(e.from, endOf(e.from, e.removed), e.inserted);
    caret_ = Clamp(g.caretAfter); anchor_ = Clamp(g.anchorAfter);
    extras_ = g.extrasAfter;
    for (Sel& e : extras_) { e.caret = Clamp(e.caret); e.anchor = Clamp(e.anchor); }
    undo_.push_back(std::move(g));
    groupOpen_ = false; breakGroup_ = true; reveal_ = true; desiredCol_ = -1;
    return true;
}

// ================================================================================================
// Caret movement
// ================================================================================================
void CodeEdit::moveCaret(CodePos p, bool extend, bool keepDesired) {
    caret_ = Clamp(p);
    if (!extend) anchor_ = caret_;
    if (!keepDesired) desiredCol_ = -1;
    breakGroup_ = true; reveal_ = true;
}

void CodeEdit::SetCaret(CodePos p, bool extend) { extras_.clear(); moveCaret(p, extend); unfoldAround(caret_.line); }

void CodeEdit::SetSelection(CodePos anchor, CodePos caret) {
    extras_.clear();
    anchor_ = Clamp(anchor); caret_ = Clamp(caret);
    desiredCol_ = -1; breakGroup_ = true; reveal_ = true;
    unfoldAround(caret_.line);
}

void CodeEdit::SelectAll() {
    extras_.clear();
    anchor_ = CodePos{0, 0};
    caret_ = CodePos{(int)lines_.size() - 1, (int)lines_.back().size()};
    desiredCol_ = -1; breakGroup_ = true; reveal_ = true;
}

void CodeEdit::SelectLine(int line) {
    if (line < 0 || line >= (int)lines_.size()) return;
    extras_.clear();
    anchor_ = CodePos{line, 0};
    caret_ = line + 1 < (int)lines_.size() ? CodePos{line + 1, 0} : CodePos{line, (int)lines_[(size_t)line].size()};
    desiredCol_ = -1; breakGroup_ = true; reveal_ = true;
}

bool CodeEdit::wordAt(CodePos p, CodePos& a, CodePos& b) const {
    p = Clamp(p);
    const std::string& s = lines_[(size_t)p.line];
    if (s.empty()) return false;
    int i = p.col;
    if (i >= (int)s.size()) i = prevIdx(s, (int)s.size());
    const int c = charClass(s, i);
    int lo = i, hi = nextIdx(s, i);
    while (lo > 0) { const int q = prevIdx(s, lo); if (charClass(s, q) != c) break; lo = q; }
    while (hi < (int)s.size() && charClass(s, hi) == c) hi = nextIdx(s, hi);
    a = CodePos{p.line, lo}; b = CodePos{p.line, hi};
    return true;
}

void CodeEdit::SelectWordAt(CodePos p) {
    extras_.clear();
    CodePos a, b;
    if (!wordAt(p, a, b)) { moveCaret(p, false); return; }
    anchor_ = a; caret_ = b; desiredCol_ = -1; breakGroup_ = true; reveal_ = true;
}

CodePos CodeEdit::wordLeft(CodePos p) const {
    p = Clamp(p);
    if (p.col == 0) { if (p.line > 0) { p.line--; p.col = (int)lines_[(size_t)p.line].size(); } return p; }
    const std::string& s = lines_[(size_t)p.line];
    int i = p.col;
    while (i > 0) { const int q = prevIdx(s, i); if (charClass(s, q) != 0) break; i = q; }
    if (i > 0) {
        const int c = charClass(s, prevIdx(s, i));
        while (i > 0) { const int q = prevIdx(s, i); if (charClass(s, q) != c) break; i = q; }
    }
    p.col = i;
    return p;
}

CodePos CodeEdit::wordRight(CodePos p) const {
    p = Clamp(p);
    const std::string& s = lines_[(size_t)p.line];
    const int n = (int)s.size();
    if (p.col >= n) { if (p.line + 1 < (int)lines_.size()) { p.line++; p.col = 0; } return p; }
    int i = p.col;
    const int c = charClass(s, i);
    if (c != 0) while (i < n && charClass(s, i) == c) i = nextIdx(s, i);
    while (i < n && charClass(s, i) == 0) i = nextIdx(s, i);
    p.col = i;
    return p;
}

void CodeEdit::Move(CodeMove m, bool extend) {
    if (extras_.empty()) { moveOne(m, extend); return; }
    // every caret moves; the view scrolls (page moves) once, for the primary one
    std::vector<Sel> all;
    all.push_back(Sel{caret_, anchor_, desiredCol_});
    for (const Sel& e : extras_) all.push_back(e);
    const float sy = scrollY_;
    float primarySy = sy;
    for (size_t i = 0; i < all.size(); ++i) {
        caret_ = all[i].caret; anchor_ = all[i].anchor; desiredCol_ = all[i].desired;
        scrollY_ = sy;
        moveOne(m, extend);
        all[i] = Sel{caret_, anchor_, desiredCol_};
        if (i == 0) primarySy = scrollY_;
    }
    scrollY_ = primarySy;
    caret_ = all[0].caret; anchor_ = all[0].anchor; desiredCol_ = all[0].desired;
    extras_.assign(all.begin() + 1, all.end());
    normalizeSels();
}

void CodeEdit::moveOne(CodeMove m, bool extend) {
    const int last = (int)lines_.size() - 1;
    CodePos p = caret_;
    ensureRows();
    switch (m) {
        case CodeMove_Left:
            if (HasSelection() && !extend) { moveCaret(SelectionStart(), false); return; }
            if (p.col > 0) p.col = prevIdx(lines_[(size_t)p.line], p.col);
            else if (p.line > 0) {   // the end of the line above (a folded block is stepped over)
                p.line--;
                while (p.line > 0 && IsLineHidden(p.line)) p.line--;
                p.col = (int)lines_[(size_t)p.line].size();
            }
            moveCaret(p, extend); return;
        case CodeMove_Right:
            if (HasSelection() && !extend) { moveCaret(SelectionEnd(), false); return; }
            if (p.col < (int)lines_[(size_t)p.line].size()) p.col = nextIdx(lines_[(size_t)p.line], p.col);
            else if (p.line < last) {
                int l = p.line + 1;
                while (l < last && IsLineHidden(l)) ++l;
                if (!IsLineHidden(l)) { p.line = l; p.col = 0; }
            }
            moveCaret(p, extend); return;
        case CodeMove_Up: case CodeMove_Down: case CodeMove_PageUp: case CodeMove_PageDown: {
            // by rows: what is drawn (folded lines are skipped, a wrapped line is several rows)
            const int n = (m == CodeMove_PageUp || m == CodeMove_PageDown) ? std::max(1, visLines_ - 1) : 1;
            const int dir = (m == CodeMove_Up || m == CodeMove_PageUp) ? -1 : 1;
            const int r = rowOf(p), rows = rowCount();
            if (desiredCol_ < 0) desiredCol_ = ByteToColumn(p.line, p.col) - rowCol(r);
            const int target = r + dir * n;
            if (target < 0) p = CodePos{0, 0};
            else if (target >= rows) p = CodePos{last, (int)lines_[(size_t)last].size()};
            else p = posInRow(target, (float)desiredCol_);
            moveCaret(p, extend, true);
            if (n > 1) { scrollY_ += (float)(dir * n) * lineH_; clampScroll(); }
            return;
        }
        case CodeMove_WordLeft: moveCaret(wordLeft(p), extend); return;
        case CodeMove_WordRight: moveCaret(wordRight(p), extend); return;
        case CodeMove_LineStart: {
            const std::string& s = lines_[(size_t)p.line];
            int first = 0; while (first < (int)s.size() && (s[(size_t)first] == ' ' || s[(size_t)first] == '\t')) ++first;
            p.col = p.col != first ? first : 0;
            moveCaret(p, extend); return;
        }
        case CodeMove_LineEnd: p.col = (int)lines_[(size_t)p.line].size(); moveCaret(p, extend); return;
        case CodeMove_DocStart: moveCaret(CodePos{0, 0}, extend); return;
        case CodeMove_DocEnd: moveCaret(CodePos{last, (int)lines_[(size_t)last].size()}, extend); return;
    }
}

// ================================================================================================
// Highlighter state cache
// ================================================================================================
void CodeEdit::SetHighlighter(CodeHighlighter h) { highlighter_ = std::move(h); validTo_ = 0; haveLanguage_ = false; }
void CodeEdit::SetLanguage(const CodeLanguage& lang) { SetHighlighter(MakeCLikeHighlighter(lang)); language_ = lang; haveLanguage_ = true; }
void CodeEdit::SetUserTypes(const std::vector<std::string>& names) {
    if (!haveLanguage_) return;
    CodeLanguage l = language_;
    for (const std::string& n : names) l.types.insert(n);
    highlighter_ = MakeCLikeHighlighter(l);
    validTo_ = 0;
}

void CodeEdit::ensureStates(int line) {
    if (!highlighter_) return;
    if (stateIn_.size() != lines_.size() + 1) stateIn_.resize(lines_.size() + 1, 0);
    if (line >= (int)lines_.size()) line = (int)lines_.size() - 1;
    while (validTo_ < line) {
        scratch_.clear();
        const std::string& s = lines_[(size_t)validTo_];
        stateIn_[(size_t)validTo_ + 1] = highlighter_(s.c_str(), (int)s.size(), stateIn_[(size_t)validTo_], scratch_);
        ++validTo_;
    }
}

void CodeEdit::LineTokens(int i, std::vector<CodeToken>& out) {
    out.clear();
    if (!highlighter_ || i < 0 || i >= (int)lines_.size()) return;
    ensureStates(i);
    const std::string& s = lines_[(size_t)i];
    highlighter_(s.c_str(), (int)s.size(), stateIn_[(size_t)i], out);
}

// ================================================================================================
// Find
// ================================================================================================
static inline char lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }

// A needle across lines: its first part ends line `line` at `start`, the middle parts are whole lines, the last part starts the
// last line. `end` is where the match ends. (A one-line needle: the match at `start`.)
bool CodeEdit::matchAt(int line, int start, CodePos& end) const {
    auto eq = [&](const std::string& a, int at, const std::string& b) {
        if (at < 0 || at + (int)b.size() > (int)a.size()) return false;
        for (size_t i = 0; i < b.size(); ++i) {
            const char x = a[(size_t)at + i], y = b[i];
            if (findCase_ ? x != y : lower(x) != lower(y)) return false;
        }
        return true;
    };
    const std::string& s = lines_[(size_t)line];
    if (findParts_.size() <= 1) {
        if (!eq(s, start, find_)) return false;
        end = CodePos{line, start + (int)find_.size()};
        return true;
    }
    const int k = (int)findParts_.size();
    if (line + k - 1 >= (int)lines_.size()) return false;
    if ((int)s.size() - start != (int)findParts_[0].size() || !eq(s, start, findParts_[0])) return false;
    for (int m = 1; m + 1 < k; ++m) {
        const std::string& t = lines_[(size_t)(line + m)];
        if (t.size() != findParts_[(size_t)m].size() || !eq(t, 0, findParts_[(size_t)m])) return false;
    }
    if (!eq(lines_[(size_t)(line + k - 1)], 0, findParts_.back())) return false;
    end = CodePos{line + k - 1, (int)findParts_.back().size()};
    return true;
}

// The first match at or after `from` (no wrapping round).
bool CodeEdit::nextMatch(CodePos from, CodePos& a, CodePos& b) const {
    int st;
    for (int l = from.line; l < (int)lines_.size(); ++l)
        if (findIn(l, l == from.line ? from.col : 0, st)) { a = CodePos{l, st}; matchAt(l, st, b); return true; }
    return false;
}

void CodeEdit::setFound(int line, int st) {
    extras_.clear();
    CodePos e;
    if (!matchAt(line, st, e)) e = CodePos{line, st};
    anchor_ = CodePos{line, st}; caret_ = e; reveal_ = true; desiredCol_ = -1; breakGroup_ = true;
    for (int l = line; l <= e.line; ++l) unfoldAround(l);
}

bool CodeEdit::findIn(int line, int from, int& start) const {
    const std::string& s = lines_[(size_t)line];
    if (findParts_.size() > 1) {   // across lines: only one place on the line can start it
        const int st = (int)s.size() - (int)findParts_[0].size();
        if (st < 0 || st < from || (st < (int)s.size() && isCont((unsigned char)s[(size_t)st]))) return false;
        CodePos e;
        if (!matchAt(line, st, e)) return false;
        start = st;
        return true;
    }
    const int n = (int)find_.size(), len = (int)s.size();
    if (n == 0 || from < 0) from = from < 0 ? 0 : from;
    for (int i = from; i + n <= len; ++i) {
        if (isCont((unsigned char)s[(size_t)i])) continue;
        int k = 0;
        if (findCase_) while (k < n && s[(size_t)(i + k)] == find_[(size_t)k]) ++k;
        else while (k < n && lower(s[(size_t)(i + k)]) == lower(find_[(size_t)k])) ++k;
        if (k == n) { start = i; return true; }
    }
    return false;
}

// The last match on the line that starts before `limit`.
bool CodeEdit::findBefore(int line, int limit, int& start) const {
    int best = -1, from = 0, st;
    const int step = findParts_.size() > 1 ? 1 : (int)find_.size();
    while (from <= (int)lines_[(size_t)line].size() && findIn(line, from, st) && st < limit) { best = st; from = st + step; }
    if (best < 0) return false;
    start = best;
    return true;
}

void CodeEdit::SetFind(const std::string& needle, bool matchCase) {
    find_ = sanitize(needle.data(), needle.size());
    findCase_ = matchCase;
    findParts_.clear();
    if (find_.empty()) return;
    for (size_t a = 0;;) {
        const size_t e = find_.find('\n', a);
        if (e == std::string::npos) { findParts_.push_back(find_.substr(a)); break; }
        findParts_.push_back(find_.substr(a, e - a));
        a = e + 1;
    }
}

bool CodeEdit::FindNext() {
    if (find_.empty()) return false;
    const CodePos s = HasSelection() ? SelectionEnd() : caret_;
    const int n = (int)lines_.size();
    int st;
    for (int k = 0; k <= n; ++k) {
        const int l = (s.line + k) % n;
        const int from = k == 0 ? s.col : 0;
        if (k == n) { if (findIn(l, 0, st) && st < s.col) { setFound(l, st); return true; } break; }
        if (findIn(l, from, st)) { setFound(l, st); return true; }
    }
    return false;
}

bool CodeEdit::FindPrev() {
    if (find_.empty()) return false;
    const CodePos s = SelectionStart();
    const int n = (int)lines_.size(), len = findParts_.size() > 1 ? 1 : (int)find_.size();
    int st;
    for (int k = 0; k <= n; ++k) {
        const int l = ((s.line - k) % n + n) % n;
        const int limit = k == 0 ? s.col : (k == n ? INT_MAX : INT_MAX);
        if (k == n) { // back on the start line: matches at or after the selection start (wrapped round)
            int best = -1, from = s.col, q;
            while (findIn(l, from, q)) { best = q; from = q + len; }
            if (best >= 0) { setFound(l, best); return true; }
            break;
        }
        if (findBefore(l, limit, st)) { setFound(l, st); return true; }
    }
    return false;
}

int CodeEdit::FindCount() const {
    if (find_.empty()) return 0;
    if (findParts_.size() > 1) {   // across lines: one after the other, from the top
        int c = 0;
        CodePos at{0, 0}, a, b;
        while (nextMatch(at, a, b)) { ++c; at = b == a ? CodePos{a.line + 1, 0} : b; if (at.line >= (int)lines_.size()) break; }
        return c;
    }
    int c = 0, st;
    for (int l = 0; l < (int)lines_.size(); ++l)
        for (int from = 0; findIn(l, from, st); from = st + (int)find_.size()) ++c;
    return c;
}

bool CodeEdit::ReplaceCurrent(const std::string& with) {
    if (readOnly_ || find_.empty() || !HasSelection()) return false;
    const std::string sel = SelectedText();
    bool eq = sel.size() == find_.size();
    for (size_t i = 0; eq && i < sel.size(); ++i) eq = findCase_ ? sel[i] == find_[i] : lower(sel[i]) == lower(find_[i]);
    if (!eq) return false;
    replaceSelection(sanitize(with.c_str()), Kind_Other);
    FindNext();
    return true;
}

int CodeEdit::ReplaceAll(const std::string& with) {
    if (readOnly_ || find_.empty()) return 0;
    const std::string w = sanitize(with.c_str());
    const int len = (int)find_.size();
    int count = 0, st;
    if (findParts_.size() > 1) {   // across lines: find them all from the top, then replace from the bottom
        std::vector<std::pair<CodePos, CodePos>> hits;
        CodePos at{0, 0}, a, b;
        while (nextMatch(at, a, b)) { hits.emplace_back(a, b); at = b; if (at.line >= (int)lines_.size()) break; }
        if (hits.empty()) return 0;
        beginCompound();
        for (size_t k = hits.size(); k-- > 0;) { edit(Kind_Other, hits[k].first, hits[k].second, w); ++count; }
        endCompound();
        caret_ = Clamp(caret_); anchor_ = Clamp(anchor_);
        normalizeSels();
        CE_FINISH();
        return count;
    }
    beginCompound();
    for (int l = (int)lines_.size() - 1; l >= 0; --l) {
        std::vector<int> hits;
        for (int from = 0; findIn(l, from, st); from = st + len) hits.push_back(st);
        for (size_t k = hits.size(); k-- > 0;) { edit(Kind_Other, CodePos{l, hits[k]}, CodePos{l, hits[k] + len}, w); ++count; }
    }
    endCompound();
    caret_ = Clamp(caret_); anchor_ = Clamp(anchor_);
    normalizeSels();
    CE_FINISH();
    return count;
}

// ================================================================================================
// Brackets
// ================================================================================================
bool CodeEdit::matchBracketImpl(CodePos& ra, CodePos& rb) {
    CodePos p = caret_;
    const std::string& cl = lines_[(size_t)p.line];
    if (p.col < (int)cl.size() && isBracket(cl[(size_t)p.col])) {}
    else if (p.col > 0 && isBracket(cl[(size_t)p.col - 1])) p.col--;
    else return false;
    const char c = cl[(size_t)p.col], t = partnerOf(c);
    std::vector<CodeToken> tok;
    LineTokens(p.line, tok);
    if (inSkipToken(tok, p.col)) return false;
    int depth = 0;
    const int last = (int)lines_.size() - 1;
    if (isOpener(c)) {
        for (int l = p.line; l <= last && l <= p.line + 2000; ++l) {
            if (l != p.line) LineTokens(l, tok);
            const std::string& s = lines_[(size_t)l];
            for (int j = l == p.line ? p.col + 1 : 0; j < (int)s.size(); ++j) {
                if (s[(size_t)j] != c && s[(size_t)j] != t) continue;
                if (inSkipToken(tok, j)) continue;
                if (s[(size_t)j] == c) ++depth; else if (depth-- == 0) { ra = p; rb = CodePos{l, j}; return true; }
            }
        }
    } else {
        for (int l = p.line; l >= 0 && l >= p.line - 2000; --l) {
            if (l != p.line) LineTokens(l, tok);
            const std::string& s = lines_[(size_t)l];
            for (int j = (l == p.line ? p.col : (int)s.size()) - 1; j >= 0; --j) {
                if (s[(size_t)j] != c && s[(size_t)j] != t) continue;
                if (inSkipToken(tok, j)) continue;
                if (s[(size_t)j] == c) ++depth; else if (depth-- == 0) { ra = p; rb = CodePos{l, j}; return true; }
            }
        }
    }
    return false;
}

bool CodeEdit::MatchBracket(CodePos& a, CodePos& b) {
    if (bVersion_ != version_ || bCaret_ != caret_) {
        bVersion_ = version_; bCaret_ = caret_;
        bOk_ = matchBracketImpl(bA_, bB_);
    }
    if (bOk_) { a = bA_; b = bB_; }
    return bOk_;
}

// ================================================================================================
// Breakpoints
// ================================================================================================
void CodeEdit::SetBreakpoints(const std::vector<int>& lines) {
    breakpoints_ = lines;
    breakpoints_.erase(std::remove_if(breakpoints_.begin(), breakpoints_.end(), [](int l) { return l < 0; }), breakpoints_.end());
    std::sort(breakpoints_.begin(), breakpoints_.end());
    breakpoints_.erase(std::unique(breakpoints_.begin(), breakpoints_.end()), breakpoints_.end());
}
bool CodeEdit::HasBreakpoint(int line) const { return std::binary_search(breakpoints_.begin(), breakpoints_.end(), line); }
void CodeEdit::ToggleBreakpoint(int line) {
    if (line < 0) return;
    auto it = std::lower_bound(breakpoints_.begin(), breakpoints_.end(), line);
    if (it != breakpoints_.end() && *it == line) breakpoints_.erase(it); else breakpoints_.insert(it, line);
}

// ================================================================================================
// Folding
// ================================================================================================
// Does the line open a block that stays open at its end (before byte `limit`, -1: the whole line)? `openCol` is where the first
// such opener starts. Braces count, and the language's block words (a word in both lists closes one block and opens the next);
// strings and comments do not. `wordsOnly`: only the words.
bool CodeEdit::lineOpens(int line, int& openCol, int limit, bool wordsOnly) {
    if (line < 0 || line >= (int)lines_.size()) return false;
    const std::string& s = lines_[(size_t)line];
    const int n = limit < 0 ? (int)s.size() : std::min(limit, (int)s.size());
    std::vector<CodeToken> tok;
    LineTokens(line, tok);
    const bool words = haveLanguage_ && !language_.blockOpenWords.empty();
    auto has = [](const std::vector<std::string>& v, const std::string& w) { return std::find(v.begin(), v.end(), w) != v.end(); };
    std::vector<int> stack;
    for (int j = 0; j < n;) {
        const char c = s[(size_t)j];
        if ((c == '{' || c == '}' || wordByte((unsigned char)c)) && inSkipToken(tok, j)) { ++j; continue; }
        if (!wordsOnly && c == '{') { stack.push_back(j); ++j; continue; }
        if (!wordsOnly && c == '}') { if (!stack.empty()) stack.pop_back(); ++j; continue; }
        if (wordByte((unsigned char)c)) {
            int k = j;
            while (k < (int)s.size() && wordByte((unsigned char)s[(size_t)k])) ++k;
            if (words && (j == 0 || s[(size_t)j - 1] != '.')) {
                const std::string w = s.substr((size_t)j, (size_t)(k - j));
                if (has(language_.blockCloseWords, w) && !stack.empty()) stack.pop_back();
                if (has(language_.blockOpenWords, w)) stack.push_back(j);
            }
            j = k;
            continue;
        }
        ++j;
    }
    if (stack.empty()) return false;
    openCol = stack.front();
    return true;
}

// The line of what closes the block opened at (line, openCol): the matching '}' or closing word; -1 when it is not found.
int CodeEdit::blockEnd(int line, int openCol) {
    const std::string& s0 = lines_[(size_t)line];
    const bool brace = s0[(size_t)openCol] == '{';
    int j0 = openCol + 1;
    if (!brace) while (j0 < (int)s0.size() && wordByte((unsigned char)s0[(size_t)j0])) ++j0;
    auto has = [](const std::vector<std::string>& v, const std::string& w) { return std::find(v.begin(), v.end(), w) != v.end(); };
    const bool words = haveLanguage_ && !language_.blockOpenWords.empty();
    std::vector<CodeToken> tok;
    int depth = 0;
    const int last = std::min((int)lines_.size() - 1, line + 100000);
    for (int l = line; l <= last; ++l) {
        LineTokens(l, tok);
        const std::string& s = lines_[(size_t)l];
        for (int j = l == line ? j0 : 0; j < (int)s.size();) {
            const char c = s[(size_t)j];
            if ((c == '{' || c == '}' || wordByte((unsigned char)c)) && inSkipToken(tok, j)) { ++j; continue; }
            if (c == '{') { ++depth; ++j; continue; }
            if (c == '}') { if (depth == 0) return brace ? l : -1; --depth; ++j; continue; }
            if (wordByte((unsigned char)c)) {
                int k = j;
                while (k < (int)s.size() && wordByte((unsigned char)s[(size_t)k])) ++k;
                if (words && (j == 0 || s[(size_t)j - 1] != '.')) {
                    const std::string w = s.substr((size_t)j, (size_t)(k - j));
                    if (has(language_.blockCloseWords, w)) { if (depth == 0) return brace ? -1 : l; --depth; }
                    if (has(language_.blockOpenWords, w)) ++depth;
                }
                j = k;
                continue;
            }
            ++j;
        }
    }
    return -1;
}

int CodeEdit::FoldEnd(int header) {
    if (header < 0 || header >= (int)lines_.size()) return -1;
    int oc = 0;
    if (lineOpens(header, oc)) {
        const int e = blockEnd(header, oc);
        return e > header ? e : -1;
    }
    if (!(haveLanguage_ && language_.foldByIndent)) return -1;
    const int ind = indentOf(header);
    if (ind < 0) return -1;
    int last = -1;
    for (int l = header + 1; l < (int)lines_.size(); ++l) {
        const int i = indentOf(l);
        if (i < 0) continue;
        if (i <= ind) break;
        last = l;
    }
    return last;
}

// The last line a fold of `header` hides (-1: nothing to hide). A block's closing line stays in view when it starts with the
// closer ("}" or "end", so the fold reads `if x then ... end`); lines that are only more indented are all hidden.
int CodeEdit::hiddenLast(int header) {
    const int e = FoldEnd(header);
    if (e <= header) return -1;
    int oc = 0;
    if (lineOpens(header, oc)) {
        int ec = -1;
        const std::string& s = lines_[(size_t)e];
        const size_t f = s.find_first_not_of(" \t");
        // the closer is the first thing on its line: keep that line visible
        if (f != std::string::npos) {
            if (s[f] == '}') ec = (int)f;
            else if (wordByte((unsigned char)s[f]) && haveLanguage_) {
                size_t k = f; while (k < s.size() && wordByte((unsigned char)s[k])) ++k;
                const std::string w = s.substr(f, k - f);
                if (std::find(language_.blockCloseWords.begin(), language_.blockCloseWords.end(), w) != language_.blockCloseWords.end()) ec = (int)f;
            }
        }
        const int last = ec >= 0 ? e - 1 : e;
        return last > header ? last : -1;
    }
    return e;
}

void CodeEdit::SetFolding(bool on) {
    folding_ = on;
    if (!on && !folds_.empty()) { folds_.clear(); ++foldsVersion_; }
}

bool CodeEdit::IsFolded(int header) const {
    for (const FoldRange& f : folds_) if (f.header == header) return true;
    return false;
}

bool CodeEdit::IsLineHidden(int line) const {
    if (folds_.empty() || line < 0 || line >= (int)lines_.size()) return false;
    ensureRows();
    return !identity_ && line < (int)hidden_.size() && hidden_[(size_t)line] != 0;
}

bool CodeEdit::Fold(int line) {
    if (!folding_ || line < 0 || line >= (int)lines_.size()) return false;
    int h = line, last = hiddenLast(line);
    if (last < 0) {   // the innermost block around the line
        last = -1;
        for (int k = line - 1; k >= 0 && k >= line - 5000; --k) {
            const int e = hiddenLast(k);
            if (e >= line) { h = k; last = e; break; }
        }
        if (last < 0) return false;
    }
    if (IsFolded(h)) return false;
    folds_.push_back(FoldRange{h, last});
    std::sort(folds_.begin(), folds_.end(), [](const FoldRange& a, const FoldRange& b) { return a.header < b.header; });
    ++foldsVersion_;
    foldsCheckedAt_ = version_;
    // carets inside go to the end of the header line
    const CodePos hp{h, (int)lines_[(size_t)h].size()};
    auto out = [&](CodePos& p) { if (p.line > h && p.line <= last) p = hp; };
    out(caret_); out(anchor_);
    for (Sel& e : extras_) { out(e.caret); out(e.anchor); }
    normalizeSels();
    return true;
}

bool CodeEdit::Unfold(int line) {
    const size_t before = folds_.size();
    folds_.erase(std::remove_if(folds_.begin(), folds_.end(), [&](const FoldRange& f) { return f.header == line || (line > f.header && line <= f.last); }), folds_.end());
    if (folds_.size() == before) return false;
    ++foldsVersion_;
    return true;
}

bool CodeEdit::ToggleFold(int line) { return IsFolded(line) ? Unfold(line) : Fold(line); }

int CodeEdit::FoldAll() {
    if (!folding_) return 0;
    int c = 0;
    for (int l = 0; l < (int)lines_.size(); ++l) {
        if (IsFolded(l)) continue;
        const int last = hiddenLast(l);
        if (last > l) { folds_.push_back(FoldRange{l, last}); ++c; }
    }
    if (c) {
        std::sort(folds_.begin(), folds_.end(), [](const FoldRange& a, const FoldRange& b) { return a.header < b.header; });
        ++foldsVersion_;
        foldsCheckedAt_ = version_;
        // the carets: out of the hidden lines, onto the end of the outermost header
        auto out = [&](CodePos& p) {
            for (const FoldRange& f : folds_) if (p.line > f.header && p.line <= f.last) { p = CodePos{f.header, (int)lines_[(size_t)f.header].size()}; break; }
        };
        out(caret_); out(anchor_);
        for (Sel& e : extras_) { out(e.caret); out(e.anchor); }
        normalizeSels();
    }
    return c;
}

void CodeEdit::UnfoldAll() { if (!folds_.empty()) { folds_.clear(); ++foldsVersion_; } }

void CodeEdit::unfoldAround(int line) {
    if (folds_.empty()) return;
    const size_t before = folds_.size();
    folds_.erase(std::remove_if(folds_.begin(), folds_.end(), [&](const FoldRange& f) { return line > f.header && line <= f.last; }), folds_.end());
    if (folds_.size() != before) ++foldsVersion_;
}

// After an edit: each fold's header must still open a block (else it goes) and its end is worked out again; a caret in a
// hidden line unfolds what hides it.
void CodeEdit::fixFolds() {
    foldsCheckedAt_ = version_;
    if (folds_.empty()) return;
    std::vector<FoldRange> nf;
    for (FoldRange f : folds_) {
        if (f.header < 0 || f.header >= (int)lines_.size()) continue;
        const int last = hiddenLast(f.header);
        if (last <= f.header) continue;
        f.last = last;
        nf.push_back(f);
    }
    folds_.swap(nf);
    ++foldsVersion_;
    unfoldAround(caret_.line);
    unfoldAround(anchor_.line);
}

// ================================================================================================
// Rows: what is drawn, line by line. Without folds or word wrap a row is a line (no tables at all).
// ================================================================================================
void CodeEdit::ensureRows() const {
    const int wcols = wrap_ ? std::max(8, wrapCols_) : 0;
    if (!rowsDirty_ && rowsVersion_ == version_ && rowsFolds_ == foldsVersion_ && rowsWrap_ == wcols && rowsTab_ == tabSize_) return;
    rowsDirty_ = false; rowsVersion_ = version_; rowsFolds_ = foldsVersion_; rowsWrap_ = wcols; rowsTab_ = tabSize_;
    identity_ = !wrap_ && folds_.empty();
    rowLine_.clear(); rowByte_.clear(); rowCol_.clear();
    if (identity_) { lineRow_.clear(); hidden_.clear(); return; }
    const int n = (int)lines_.size();
    hidden_.assign((size_t)n, 0);
    for (const FoldRange& f : folds_)
        for (int l = std::max(f.header + 1, 0); l <= std::min(f.last, n - 1); ++l) hidden_[(size_t)l] = 1;
    lineRow_.assign((size_t)n, 0);
    rowLine_.reserve((size_t)n); rowByte_.reserve((size_t)n); rowCol_.reserve((size_t)n);
    for (int l = 0; l < n; ++l) {
        if (hidden_[(size_t)l]) { lineRow_[(size_t)l] = std::max(0, (int)rowLine_.size() - 1); continue; }
        lineRow_[(size_t)l] = (int)rowLine_.size();
        rowLine_.push_back(l); rowByte_.push_back(0); rowCol_.push_back(0);
        if (!wrap_) continue;
        // wrap: break after the last space that leaves the row at least half full, else at the column limit
        const std::string& s = lines_[(size_t)l];
        const int len = (int)s.size();
        int col = 0, segByte = 0, segCol = 0, brByte = -1, brCol = 0;
        for (int i = 0; i < len;) {
            const char c = s[(size_t)i];
            const int next = c == '\t' ? nextTab(col, tabSize_) : col + 1;
            if (next - segCol > wcols && i > segByte) {
                int bb = i, bc = col;
                if (brByte > segByte && brByte <= i && brCol - segCol >= wcols / 2) { bb = brByte; bc = brCol; }
                rowLine_.push_back(l); rowByte_.push_back(bb); rowCol_.push_back(bc);
                segByte = bb; segCol = bc; brByte = -1;
                continue;   // the same character again, against the new row
            }
            col = next;
            i = nextIdx(s, i);
            if (c == ' ') { brByte = i; brCol = col; }
        }
    }
}

int CodeEdit::rowCount() const { ensureRows(); return identity_ ? (int)lines_.size() : (int)rowLine_.size(); }
int CodeEdit::rowLine(int r) const { ensureRows(); return identity_ ? r : rowLine_[(size_t)r]; }
int CodeEdit::rowStart(int r) const { ensureRows(); return identity_ ? 0 : rowByte_[(size_t)r]; }
int CodeEdit::rowCol(int r) const { ensureRows(); return identity_ ? 0 : rowCol_[(size_t)r]; }
int CodeEdit::rowEnd(int r) const {
    ensureRows();
    if (identity_) return (int)lines_[(size_t)r].size();
    if (r + 1 < (int)rowLine_.size() && rowLine_[(size_t)r + 1] == rowLine_[(size_t)r]) return rowByte_[(size_t)r + 1];
    return (int)lines_[(size_t)rowLine_[(size_t)r]].size();
}
int CodeEdit::rowOf(CodePos p) const {
    ensureRows();
    const int n = (int)lines_.size();
    const int l = std::min(std::max(p.line, 0), n - 1);
    if (identity_) return l;
    if (hidden_[(size_t)l]) return lineRow_[(size_t)l];
    int r = lineRow_[(size_t)l];
    const int rows = (int)rowLine_.size();
    while (r + 1 < rows && rowLine_[(size_t)r + 1] == l && rowByte_[(size_t)r + 1] <= p.col) ++r;
    return r;
}
int CodeEdit::VisualRowCount() const { return rowCount(); }

// The position in row `r` nearest a display column counted from the row's start. On a row that word wrap ended early, the
// caret stays before the row's last character (the break itself belongs to the next row).
CodePos CodeEdit::posInRow(int r, float visualCol) const {
    const int rows = rowCount();
    r = std::min(std::max(r, 0), rows - 1);
    const int l = rowLine(r), s0 = rowStart(r), s1 = rowEnd(r);
    const std::string& s = lines_[(size_t)l];
    int b = ColumnToByte(l, (float)rowCol(r) + std::max(0.0f, visualCol));
    b = std::min(std::max(b, s0), s1);
    if (s1 < (int)s.size() && b >= s1) b = std::max(s0, prevIdx(s, s1));
    return CodePos{l, b};
}

// ================================================================================================
// View
// ================================================================================================
void CodeEdit::recomputeMax() {
    maxBytes_ = 0;
    for (const std::string& s : lines_) maxBytes_ = std::max(maxBytes_, (int)s.size());
    maxDirty_ = false;
}

void CodeEdit::clampScroll() {
    if (!std::isfinite(scrollX_)) scrollX_ = 0;
    if (!std::isfinite(scrollY_)) scrollY_ = 0;
    const float maxY = std::max(0.0f, contentH_ - view_.h), maxX = std::max(0.0f, contentW_ - view_.w);
    scrollY_ = std::min(std::max(scrollY_, 0.0f), maxY);
    scrollX_ = std::min(std::max(scrollX_, 0.0f), maxX);
}

void CodeEdit::GoToLine(int line) {
    line = std::min(std::max(line, 0), (int)lines_.size() - 1);
    extras_.clear();
    unfoldAround(line);
    moveCaret(CodePos{line, 0}, false);
    scrollY_ = padY_ + (float)rowOf(CodePos{line, 0}) * lineH_ - (view_.h - lineH_) * 0.5f;
    clampScroll();
}

Vec2 CodeEdit::PosToScreen(CodePos p) const {
    p = Clamp(p);
    const int r = rowOf(p);
    return Vec2{textLeft_ + (float)(ByteToColumn(p.line, p.col) - rowCol(r)) * charW_ - scrollX_, view_.y + padY_ + (float)r * lineH_ - scrollY_};
}

CodePos CodeEdit::ScreenToPos(float x, float y) const {
    int row = SafeInt(std::floor((y - view_.y - padY_ + scrollY_) / lineH_));
    row = std::min(std::max(row, 0), rowCount() - 1);
    const float c = (x - textLeft_ + scrollX_) / charW_;
    if (identity_) return CodePos{row, ColumnToByte(row, c)};
    return posInRow(row, c != c ? 0.0f : c);
}

std::string CodeEdit::HoverText(float x, float y) const {
    if (!(x >= item_.x && x < item_.x + item_.w && y >= item_.y && y < item_.y + item_.h)) return std::string();
    const int vrow = SafeInt(std::floor((y - view_.y - padY_ + scrollY_) / lineH_));
    if (vrow < 0 || vrow >= rowCount()) return std::string();
    const int row = rowLine(vrow);
    for (const CodeMarker& m : markers_) if (m.line == row && !m.message.empty()) return m.message;
    if (!hoverInfo_ || x < view_.x || x >= view_.x + view_.w || y >= view_.y + view_.h) return std::string();
    // the cell under the point (not the nearest caret position): its display column, then the character that covers it
    const float colF = (x - textLeft_ + scrollX_) / charW_ + (float)rowCol(vrow);
    if (colF < (float)rowCol(vrow)) return std::string();
    const std::string& s = lines_[(size_t)row];
    const int n = std::min((int)s.size(), rowEnd(vrow));
    int i = 0, col = 0, hit = -1;
    while (i < n) {
        const int next = s[(size_t)i] == '\t' ? nextTab(col, tabSize_) : col + 1;
        if (colF < (float)next) { hit = i; break; }
        col = next;
        i = nextIdx(s, i);
    }
    auto isWord = [&](int k) { const unsigned char c = (unsigned char)s[(size_t)k]; return std::isalnum(c) || c == '_' || c >= 0x80; };
    if (hit < 0 || !isWord(hit)) return std::string();
    int a = hit, b = hit;
    while (a > 0 && isWord(a - 1)) --a;
    while (b < n && isWord(b)) ++b;
    return hoverInfo_(s.substr((size_t)a, (size_t)(b - a)));
}

void CodeEdit::Focus() { /* needs the widget id: taken on the next Draw */ wantFocus_ = true; }

void CodeEdit::layout(const Rect& item) {
    if (maxDirty_) recomputeMax();
    const int caretCols = ByteToColumn(caret_.line, (int)lines_[(size_t)caret_.line].size());
    foldW_ = folding_ ? std::floor(lineH_ * 0.9f) : 0.0f;
    gutterW_ = padX_ + (showBreakpoints_ ? lineH_ : 0.0f) + (showLineNumbers_ ? (float)(digitsOf((int)lines_.size()) + 2) * charW_ : 4.0f) + foldW_;
    if (gutterW_ > item.w * 0.5f) gutterW_ = item.w * 0.5f;
    const float sb = sbSize_;
    // word wrap: the rows are as wide as the view (the vertical bar's room is always kept, so the width does not depend on it)
    if (wrap_) wrapCols_ = std::max(8, SafeInt((item.w - gutterW_ - sb - textMargin_ - 8.0f - padX_) / charW_));
    if (folding_ && foldsCheckedAt_ != version_) fixFolds();
    const int rows = rowCount();
    contentH_ = (float)rows * lineH_ + 2.0f * padY_;
    contentW_ = wrap_ ? 0.0f : (float)(std::max(maxBytes_, caretCols) + 3) * charW_ + 8.0f + padX_;
    const float vw = item.w - gutterW_, vh = item.h;
    needV_ = contentH_ > vh;
    needH_ = !wrap_ && (alwaysH_ || contentW_ > vw - (needV_ ? sb : 0.0f));
    if (needH_ && !needV_) needV_ = contentH_ > vh - sb;
    if (wrap_) scrollX_ = 0.0f;
    view_ = Rect{item.x + gutterW_, item.y, std::max(0.0f, vw - (needV_ ? sb : 0.0f)), std::max(0.0f, vh - (needH_ ? sb : 0.0f))};
    textLeft_ = view_.x + textMargin_;
    clampScroll();
    vTrack_ = Rect{view_.x + view_.w, item.y, sb, view_.h};
    hTrack_ = Rect{view_.x, item.y + view_.h, view_.w, sb};
    const float maxY = std::max(0.0f, contentH_ - view_.h), maxX = std::max(0.0f, contentW_ - view_.w);
    {
        const float th = std::min(vTrack_.h, std::max(20.0f, vTrack_.h * (contentH_ > 0 ? view_.h / contentH_ : 1.0f)));
        vThumb_ = Rect{vTrack_.x + 1, vTrack_.y + (maxY > 0 ? scrollY_ / maxY * (vTrack_.h - th) : 0.0f), sb - 2, th};
        const float tw = std::min(hTrack_.w, std::max(20.0f, hTrack_.w * (contentW_ > 0 ? view_.w / contentW_ : 1.0f)));
        hThumb_ = Rect{hTrack_.x + (maxX > 0 ? scrollX_ / maxX * (hTrack_.w - tw) : 0.0f), hTrack_.y + 1, tw, sb - 2};
    }
    firstRow_ = std::min(std::max(0, SafeInt(std::floor((scrollY_ - padY_) / lineH_))), rows - 1);
    firstLine_ = rowLine(firstRow_);
    visLines_ = std::max(1, (int)(view_.h / lineH_));
}

bool CodeEdit::Focused() const { return wid_ != 0 && GetKeyboardFocusId() == wid_; }

static bool inRect(const Rect& r, float x, float y) { return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h; }

void CodeEdit::handleKeys(bool& changed) {
    const Input& in = GetFrameInput();
    const IO& io = GetIO();
    const bool ctrl = io.keyCtrl, shift = io.keyShift;
    bool any = false;
    const unsigned v0 = version_;
    if (in.selectAll && !comp_.active) { SelectAll(); any = true; }   // before the text: Ctrl+A and the text typed after it in one frame replace the old text
    if (in.commit && in.commit[0]) {   // what the input method finished: inserted like typed text, over the selection
        std::string t = sanitize(in.commit);
        t.erase(std::remove_if(t.begin(), t.end(), [](char c) { return c == '\n' || c == '\t'; }), t.end());
        if (!t.empty()) { InsertText(t.c_str()); any = true; }
    }
    if (in.text && in.text[0] && !ctrl) {
        std::string t = sanitize(in.text);
        t.erase(std::remove_if(t.begin(), t.end(), [](char c) { return c == '\n' || c == '\t'; }), t.end());
        if (!t.empty()) { Type(t.c_str()); any = true; }   // typed: auto-close and auto-dedent
    }
    if (comp_.active) {   // composing: the input method owns Backspace, the arrows, Enter and Escape
        if (any) lastActivity_ = GetTime();
        if (version_ != v0) changed = true;
        return;
    }
    if (in.copy) { Copy(); any = true; }
    if (in.cut) { Cut(); any = true; }
    if (in.paste) { Paste(); any = true; }
    if (in.enter) { NewLine(); any = true; }
    if (in.backspace) { Backspace(ctrl); any = true; }
    if (in.del) { DeleteForward(ctrl); any = true; }
    if (in.left) { Move(ctrl ? CodeMove_WordLeft : CodeMove_Left, shift); any = true; }
    if (in.right) { Move(ctrl ? CodeMove_WordRight : CodeMove_Right, shift); any = true; }
    // Alt+Up and Alt+Down move the lines (the caret goes with them) instead of moving the caret.
    const bool moveLines = lineKeys_ && io.keyAlt && !ctrl;
    if (in.up) { if (moveLines) MoveLines(true); else Move(CodeMove_Up, shift); any = true; }
    if (in.down) { if (moveLines) MoveLines(false); else Move(CodeMove_Down, shift); any = true; }
    if (ctrl && !shift && !io.keyAlt && IsKeyPressed(KeyId_D)) { SelectNextOccurrence(); any = true; }
    if (lineKeys_ && ctrl && !io.keyAlt) {
        if (!shift && IsKeyPressed(KeyId_Slash)) { ToggleLineComment(); any = true; }
        if (shift && IsKeyPressed(KeyId_D)) { DuplicateLines(); any = true; }
        if (shift && IsKeyPressed(KeyId_K)) { DeleteLines(); any = true; }
    }
    if (folding_ && ctrl) {   // Ctrl+Shift+[ fold, Ctrl+Shift+] unfold, Ctrl+Alt+[ fold all, Ctrl+Alt+] unfold all
        if (shift && !io.keyAlt && IsKeyPressed(KeyId_LeftBracket)) { Fold(caret_.line); any = true; }
        if (shift && !io.keyAlt && IsKeyPressed(KeyId_RightBracket)) { Unfold(caret_.line); any = true; }
        if (io.keyAlt && !shift && IsKeyPressed(KeyId_LeftBracket)) { FoldAll(); any = true; }
        if (io.keyAlt && !shift && IsKeyPressed(KeyId_RightBracket)) { UnfoldAll(); any = true; }
    }
    if (in.home) { Move(ctrl ? CodeMove_DocStart : CodeMove_LineStart, shift); any = true; }
    if (in.end) { Move(ctrl ? CodeMove_DocEnd : CodeMove_LineEnd, shift); any = true; }
    if (IsKeyPressed(KeyId_PageUp)) { Move(CodeMove_PageUp, shift); any = true; }
    if (IsKeyPressed(KeyId_PageDown)) { Move(CodeMove_PageDown, shift); any = true; }
    if (!ctrl && IsKeyPressed(KeyId_Tab)) { Tab(shift); any = true; }
    if (ctrl && IsKeyPressed(KeyId_Z)) { if (shift) Redo(); else Undo(); any = true; }
    if (ctrl && IsKeyPressed(KeyId_Y)) { Redo(); any = true; }
    if (in.escape && !extras_.empty()) { extras_.clear(); any = true; }
    else if (in.escape && HasSelection()) { moveCaret(caret_, false); any = true; }
    if (any) lastActivity_ = GetTime();
    if (version_ != v0) changed = true;
}

void CodeEdit::handleMouse(const Rect& item, int wid, bool hovered) {
    const Input& in = GetFrameInput();
    const float mx = in.mouseX, my = in.mouseY;
    const bool held = IsItemActive();
    if (IsItemActivated() && !in.blocked) {
        SetKeyboardFocusId(wid);
        lastActivity_ = GetTime();
        if (needV_ && inRect(vTrack_, mx, my)) {
            if (inRect(vThumb_, mx, my)) { dragVBar_ = true; dragOff_ = my - vThumb_.y; }
            else { scrollY_ += (my < vThumb_.y ? -1.0f : 1.0f) * view_.h; clampScroll(); }
        } else if (needH_ && inRect(hTrack_, mx, my)) {
            if (inRect(hThumb_, mx, my)) { dragHBar_ = true; dragOff_ = mx - hThumb_.x; }
            else { scrollX_ += (mx < hThumb_.x ? -1.0f : 1.0f) * view_.w; clampScroll(); }
        } else if (mx < view_.x) {
            const float lineF = std::floor((my - view_.y - padY_ + scrollY_) / lineH_);
            // NaN, infinity or a huge value: no line (the cast would be undefined).
            const int vrow = (lineF >= 0.0f && lineF < 2.0e9f) ? (int)lineF : -1;
            const int line = (vrow >= 0 && vrow < rowCount()) ? rowLine(vrow) : -1;
            if (line >= 0 && line < (int)lines_.size()) {
                if (mx < item.x + padX_) {
                    // the padding left of the gutter: nothing
                } else if (folding_ && mx >= view_.x - foldW_) {
                    ToggleFold(line);   // the fold column
                } else if (showBreakpoints_ && mx < item.x + padX_ + lineH_) {
                    if (autoToggle_) ToggleBreakpoint(line);
                    if (onBreakpoint_) onBreakpoint_(line, HasBreakpoint(line));
                } else if (showLineNumbers_) {
                    SelectLine(line);
                }
            }
        } else if (inRect(view_, mx, my)) {
            const CodePos p = ScreenToPos(mx, my);
            const double now = GetTime();
            const float dx = mx - lastClickX_, dy = my - lastClickY_;
            clicks_ = (now - lastClickT_ < 0.35 && dx * dx + dy * dy < 36.0f) ? std::min(clicks_ + 1, 3) : 1;
            lastClickT_ = now; lastClickX_ = mx; lastClickY_ = my;
            const IO& io = GetIO();
            const int vrow = std::min(std::max(0, SafeInt(std::floor((my - view_.y - padY_ + scrollY_) / lineH_))), rowCount() - 1);
            const float vcol = std::max(0.0f, (mx - textLeft_ + scrollX_) / charW_) + (float)rowCol(vrow);
            bool onFoldBox = false;
            if (clicks_ == 1 && !io.keyCtrl && !io.keyAlt && IsFolded(p.line) && rowEnd(vrow) == (int)lines_[(size_t)p.line].size()) {
                const float bx = PosToScreen(CodePos{p.line, (int)lines_[(size_t)p.line].size()}).x + charW_ * 0.5f;
                onFoldBox = mx >= bx && mx < bx + charW_ * 3.0f + 4.0f;   // the "..." box of a folded block: unfold it
            }
            const CodePos ss = SelectionStart(), se = SelectionEnd();
            if (onFoldBox) {
                Unfold(p.line);
            } else if (clicks_ == 1 && io.keyAlt && !io.keyCtrl) {   // Alt+drag: a column selection
                boxSel_ = true; boxLine_ = rowLine(vrow); boxCol_ = vcol;
                setBox(boxLine_, boxCol_, boxLine_, boxCol_);
            } else if (clicks_ == 1 && io.keyCtrl && !io.keyShift && !readOnly_) {   // Ctrl+click: one more caret (or one less)
                if (!RemoveCaretAt(p)) AddCaret(p);
            } else if (clicks_ == 1 && !io.keyShift && !readOnly_ && extras_.empty() && HasSelection() && ss <= p && p <= se && !(p == ss || p == se)) {
                dropPending_ = true; pressPos_ = p; pressX_ = mx; pressY_ = my;   // in the selection: maybe a drag of the text
            } else {
                extras_.clear();
                if (clicks_ == 1) moveCaret(p, io.keyShift);
                else if (clicks_ == 2) SelectWordAt(p);
                else SelectLine(p.line);
                dragging_ = true;
            }
        }
    }
    if (!held) {
        if (dropping_) {
            // drop the dragged text (Ctrl: a copy); dropped back inside the selection: nothing moves
            if (!MoveSelectionTo(dropAt_, GetIO().keyCtrl)) moveCaret(dropAt_, false);
        } else if (dropPending_) moveCaret(pressPos_, false);   // a click in the selection, no drag: the caret goes there
        dragging_ = dragVBar_ = dragHBar_ = boxSel_ = dropPending_ = dropping_ = false;
    } else if (dropPending_ || dropping_) {
        const float ddx = mx - pressX_, ddy = my - pressY_;
        if (!dropping_ && ddx * ddx + ddy * ddy > 16.0f) dropping_ = true;
        if (dropping_) dropAt_ = ScreenToPos(mx, my);
    } else if (boxSel_) {
        const int vrow = std::min(std::max(0, SafeInt(std::floor((my - view_.y - padY_ + scrollY_) / lineH_))), rowCount() - 1);
        const float vcol = std::max(0.0f, (mx - textLeft_ + scrollX_) / charW_) + (float)rowCol(vrow);
        setBox(boxLine_, boxCol_, rowLine(vrow), vcol);
    } else if (dragging_ && clicks_ == 1) {
        moveCaret(ScreenToPos(mx, my), true);
    } else if (dragVBar_) {
        const float maxY = std::max(0.0f, contentH_ - view_.h), span = std::max(1.0f, vTrack_.h - vThumb_.h);
        scrollY_ = (my - dragOff_ - vTrack_.y) / span * maxY; clampScroll();
    } else if (dragHBar_) {
        const float maxX = std::max(0.0f, contentW_ - view_.w), span = std::max(1.0f, hTrack_.w - hThumb_.w);
        scrollX_ = (mx - dragOff_ - hTrack_.x) / span * maxX; clampScroll();
    }
    if (hovered && in.wheel != 0.0f && std::isfinite(in.wheel)) {
        if (GetIO().keyShift) scrollX_ -= in.wheel * 3.0f * charW_; else scrollY_ -= in.wheel * 3.0f * lineH_;
        clampScroll();
        reveal_ = false;
    }
    if (hovered && inRect(view_, mx, my)) SetMouseCursor(MouseCursor_TextInput);
}

bool CodeEdit::Draw(const char* id, float w, float h) {
    if (!font_) return drawImpl(id, w, h);
    // The code font for this Draw only: SetFont makes the text scale the font's own size, PushTextScale takes it to fontPx_;
    // both are put back so the rest of the window keeps its font.
    const Font* prev = GetFont();
    OkayUI::SetFont(font_);   // (qualified: CodeEdit::SetFont is the member)
    const bool scaled = fontPx_ > 0.0f && font_->px > 0.0f;
    if (scaled) PushTextScale(fontPx_ / font_->px);
    const bool changed = drawImpl(id, w, h);
    if (scaled) PopTextScale();
    OkayUI::SetFont(prev);
    return changed;
}

bool CodeEdit::drawImpl(const char* id, float w, float h) {
    const unsigned v0 = version_;
    const Vec2 avail = GetContentRegionAvail();
    if (w <= 0.0f) w = avail.x;
    if (h <= 0.0f) h = avail.y;
    if (!(w >= 1.0f) || !(h >= 1.0f) || w > 1e6f || h > 1e6f) return false;
    lineH_ = std::floor(GetTextLineHeight() * lineSpacing_ + 0.5f);
    if (!(lineH_ >= 1.0f)) lineH_ = 1.0f;
    charW_ = CalcTextSize("0").x;
    if (!(charW_ >= 1.0f)) charW_ = 1.0f;
    InvisibleButton(id ? id : "codeedit", w, h);
    const int wid = (int)GetItemID();
    if (!wid) return false;
    wid_ = wid;
    item_ = GetItemRect();
    const bool hovered = IsItemHovered();
    if (wantFocus_) { SetKeyboardFocusId(wid); wantFocus_ = false; }
    comp_ = (Focused() && !readOnly_) ? GetComposition() : Composition();
    compCells_ = compCaretCells_ = 0;
    if (comp_.active) {
        compCells_ = (int)std::ceil(CalcTextSize(comp_.text).x / charW_ - 0.001f);
        const std::string pre(comp_.text, (size_t)comp_.caret);
        compCaretCells_ = pre.empty() ? 0 : (int)std::ceil(CalcTextSize(pre.c_str()).x / charW_ - 0.001f);
        if (compCells_ < 1) compCells_ = 1;
    }
    layout(item_);
    handleMouse(item_, wid, hovered);
    bool changed = false;
    if (Focused() && handleKeys_) handleKeys(changed);
    normalizeSels();   // (a drag that grows the selection over another caret merges them)
    layout(item_);
    if (comp_.active) reveal_ = true;   // keep the preedit's caret in view while composing
    if (reveal_) {
        reveal_ = false;
        const CodePos rp = comp_.active ? SelectionStart() : caret_;
        const int rr = rowOf(rp);
        const float top = padY_ + (float)rr * lineH_;
        if (top < scrollY_ + (rr == 0 ? padY_ : 0.0f)) scrollY_ = rr == 0 ? 0.0f : top;
        else if (top + lineH_ > scrollY_ + view_.h) scrollY_ = top + lineH_ - view_.h + (rr == rowCount() - 1 ? padY_ : 0.0f);
        const float cx = (float)(ByteToColumn(rp.line, rp.col) - rowCol(rr) + (comp_.active ? compCaretCells_ : 0)) * charW_;
        if (cx < scrollX_) scrollX_ = cx;
        else if (cx + charW_ + 8.0f > scrollX_ + view_.w) scrollX_ = cx + charW_ + 8.0f - view_.w;
        clampScroll();
        layout(item_);
    }
    drawAll(item_, hovered);
    return version_ != v0;
}

static unsigned colorFor(const CodePalette& p, unsigned char id) { return p.text[id < (unsigned char)CodeColor_COUNT ? (int)id : (int)CodeColor_Default]; }

void CodeEdit::drawAll(const Rect& item, bool hovered) {
    const DrawList& dl = GetWindowDrawList();
    const bool focused = Focused();
    const Input& in = GetFrameInput();
    const Vec2 i0{item.x, item.y}, i1{item.x + item.w, item.y + item.h};
    const int n = (int)lines_.size();
    const int rows = rowCount();
    const int first = firstRow_;
    const int last = std::min(rows - 1, SafeInt(std::floor((scrollY_ + view_.h - padY_) / lineH_)));
    auto rowY = [&](int r) { return view_.y + padY_ + (float)r * lineH_ - scrollY_; };
    const bool sel = HasSelection();
    dl.PushClipRect(i0, i1);
    dl.AddRectFilled(i0, i1, pal_.background);

    // ---- gutter
    const Vec2 vend{view_.x + view_.w, view_.y + view_.h};
    dl.AddRectFilled(i0, Vec2{view_.x, vend.y}, pal_.gutter);
    // the caret's line: a box across the gutter and the text (while focused, or with an inactive colour), and the breakpoint wash
    dl.PushClipRect(i0, vend);
    for (int r = first; r <= last; ++r) {
        const int l = rowLine(r);
        const float y = rowY(r);
        if (showBreakpoints_ && (pal_.breakpointLine >> 24) != 0 && HasBreakpoint(l)) dl.AddRectFilled(Vec2{item.x + padX_, y}, Vec2{vend.x, y + lineH_}, pal_.breakpointLine);
        if (l == caret_.line && !sel && (focused || (pal_.currentLineInactive >> 24) != 0)) {
            dl.AddRectFilled(Vec2{item.x + padX_, y}, Vec2{vend.x, y + lineH_}, focused ? pal_.currentLine : pal_.currentLineInactive);
            if ((pal_.currentLineEdge >> 24) != 0) dl.AddRect(Vec2{item.x + padX_, y}, Vec2{vend.x, y + lineH_}, pal_.currentLineEdge, 0.0f, 1.0f);   // (AddRect keeps the stroke inside)
        }
    }
    dl.PopClipRect();
    const float numRight = view_.x - foldW_;   // the line numbers end left of the fold column
    const Color32 foldCol = (pal_.fold >> 24) != 0 ? pal_.fold : pal_.lineNumber;
    dl.PushClipRect(i0, Vec2{view_.x, vend.y});
    for (int r = first; r <= last; ++r) {
        if (rowStart(r) != 0) continue;   // the later rows of a wrapped line: no number and no marks
        const int l = rowLine(r);
        const float y = rowY(r);
        if (showLineNumbers_) {
            char num[16]; std::snprintf(num, sizeof num, "%d", l + 1);
            const float tw = (float)std::strlen(num) * charW_;
            const bool cur = focused && l == caret_.line;
            dl.AddText(Vec2{numRight - charW_ - tw, y + (lineH_ - GetTextLineHeight()) * 0.5f}, cur ? pal_.lineNumberCurrent : pal_.lineNumber, num);
        }
        for (const CodeMarker& m : markers_) if (m.line == l) { dl.AddRectFilled(Vec2{view_.x - 3.0f, y}, Vec2{view_.x, y + lineH_}, m.color ? m.color : pal_.errorUnderline); break; }
        if (showBreakpoints_ && HasBreakpoint(l)) dl.AddCircleFilled(Vec2{item.x + padX_ + lineH_ * 0.5f, y + lineH_ * 0.5f}, lineH_ * 0.3f, pal_.breakpoint);
        if (folding_) {   // the fold arrow: pointing right on a folded block, down on one that can fold
            const bool folded = IsFolded(l);
            int oc = 0;
            bool header = folded || lineOpens(l, oc);
            if (!header && haveLanguage_ && language_.foldByIndent) {
                const int ind = indentOf(l);
                for (int k = l + 1; ind >= 0 && k < n && k <= l + 50; ++k) { const int i = indentOf(k); if (i < 0) continue; header = i > ind; break; }
            }
            if (header) {
                const float cx = view_.x - foldW_ * 0.5f - 1.0f, cy = y + lineH_ * 0.5f, k = std::max(2.0f, foldW_ * 0.22f);
                if (folded) dl.AddTriangleFilled(Vec2{cx - k * 0.6f, cy - k}, Vec2{cx - k * 0.6f, cy + k}, Vec2{cx + k * 0.8f, cy}, foldCol);
                else dl.AddTriangleFilled(Vec2{cx - k, cy - k * 0.6f}, Vec2{cx + k, cy - k * 0.6f}, Vec2{cx, cy + k * 0.8f}, ColorMulAlpha(foldCol, 0.6f));
            }
        }
    }
    dl.PopClipRect();

    // ---- text
    dl.PushClipRect(Vec2{view_.x, view_.y}, vend);
    CodePos ba, bb; const bool haveBracket = focused && MatchBracket(ba, bb);
    const CodePos sa = SelectionStart(), sb = SelectionEnd();
    const float textH = GetTextLineHeight(), dy = (lineH_ - textH) * 0.5f;
    const int vc0 = std::max(0, SafeInt(std::floor(scrollX_ / charW_)) - 1), vcN = SafeInt(view_.w / charW_) + 4;
    std::vector<CodeToken> toks;
    std::string run;
    // the word under the caret (or the selected word): its other places are tinted
    std::string word; CodePos wa;
    if (focused && (pal_.wordMatch >> 24) != 0 && extras_.empty()) {
        if (!sel) {
            const std::string& cl = lines_[(size_t)caret_.line];
            CodePos q = caret_, a, b;
            if (!(q.col < (int)cl.size() && charClass(cl, q.col) == 1) && q.col > 0 && charClass(cl, prevIdx(cl, q.col)) == 1) q.col = prevIdx(cl, q.col);
            if (q.col < (int)cl.size() && charClass(cl, q.col) == 1 && wordAt(q, a, b)) { word = textIn(a, b); wa = a; }
        } else if (selectionIsWord()) { word = textIn(sa, sb); wa = sa; }
    }
    // a find text across lines: its matches that reach into the view
    std::vector<std::pair<CodePos, CodePos>> spans;
    if (findParts_.size() > 1 && rows > 0) {
        int st;
        for (int l = std::max(0, rowLine(first) - (int)findParts_.size() + 1); l <= rowLine(last); ++l)
            if (findIn(l, 0, st)) { CodePos e; matchAt(l, st, e); spans.emplace_back(CodePos{l, st}, e); }
    }
    // the part of [a, b) on row r, in display columns (the newline counts as a cell when the span goes on)
    auto spanRow = [&](int r, CodePos a, CodePos b, int& from, int& to) {
        const int l = rowLine(r);
        if (l < a.line || l > b.line) return false;
        const int s0 = rowStart(r), s1 = rowEnd(r), len = (int)lines_[(size_t)l].size();
        const bool lastSeg = s1 == len;
        int fb = l == a.line ? a.col : 0, tb = l == b.line ? b.col : len;
        if (l == a.line && (fb > s1 || (fb == s1 && !lastSeg))) return false;
        if (l == b.line && tb < s0) return false;
        fb = std::max(fb, s0); tb = std::min(tb, s1);
        from = ByteToColumn(l, fb); to = ByteToColumn(l, tb);
        if (l != b.line && lastSeg) to += 1;
        return to > from;
    };
    const Color32 wsCol = (pal_.whitespace >> 24) != 0 ? pal_.whitespace : ColorMulAlpha(pal_.text[CodeColor_Comment], 0.5f);
    for (int r = first; r <= last; ++r) {
        const int l = rowLine(r);
        const std::string& s = lines_[(size_t)l];
        const int s0 = rowStart(r), s1 = rowEnd(r), sc = rowCol(r);
        const bool lastSeg = s1 == (int)s.size();
        const float y = rowY(r);
        auto xOf = [&](int col) { return textLeft_ + (float)(col - sc) * charW_ - scrollX_; };
        const int c0 = sc + vc0, c1 = c0 + vcN;
        const Vec2 r0{view_.x, y}, r1{vend.x, y + lineH_};
        bool hasMarker = false;
        for (const CodeMarker& m : markers_) if (m.line == l) { if (!hasMarker) dl.AddRectFilled(r0, r1, m.lineColor ? m.lineColor : pal_.errorLine); hasMarker = true; }
        int from = 0, to = 0;
        if (sel && !comp_.active && spanRow(r, sa, sb, from, to)) dl.AddRectFilled(Vec2{xOf(from), y}, Vec2{xOf(to), y + lineH_}, focused ? pal_.selection : pal_.selectionInactive);
        for (const Sel& e : extras_) {
            if (e.caret == e.anchor) continue;
            const CodePos ea = e.caret < e.anchor ? e.caret : e.anchor, eb = e.caret < e.anchor ? e.anchor : e.caret;
            if (spanRow(r, ea, eb, from, to)) dl.AddRectFilled(Vec2{xOf(from), y}, Vec2{xOf(to), y + lineH_}, focused ? pal_.selection : pal_.selectionInactive);
        }
        if (!find_.empty() && findParts_.size() <= 1) {
            int st;
            const int fl = (int)find_.size();
            for (int f = 0; findIn(l, f, st); f = st + fl) {
                if (st + fl <= s0 && !(st == s0)) continue;
                if (st >= s1 && !(lastSeg && st == s1 && fl == 0)) break;
                const int a = ByteToColumn(l, std::max(st, s0)), b = ByteToColumn(l, std::min(st + fl, s1));
                if (xOf(b) < view_.x) continue;
                if (xOf(a) > vend.x) break;
                dl.AddRectFilled(Vec2{xOf(a), y}, Vec2{xOf(b), y + lineH_}, pal_.findMatch);
            }
        }
        for (const auto& sp : spans) if (spanRow(r, sp.first, sp.second, from, to)) dl.AddRectFilled(Vec2{xOf(from), y}, Vec2{xOf(to), y + lineH_}, pal_.findMatch);
        if (!word.empty()) {
            const int wl = (int)word.size();
            for (size_t f = (size_t)s0; (f = s.find(word, f)) != std::string::npos && (int)f + wl <= s1; f += (size_t)wl) {
                const int i = (int)f;
                if ((i > 0 && charClass(s, prevIdx(s, i)) == 1) || (i + wl < (int)s.size() && charClass(s, i + wl) == 1)) continue;
                if (l == wa.line && i == wa.col) continue;   // the word at the caret itself
                dl.AddRectFilled(Vec2{xOf(ByteToColumn(l, i)), y}, Vec2{xOf(ByteToColumn(l, i + wl)), y + lineH_}, pal_.wordMatch);
            }
        }
        if ((pal_.indentGuide >> 24) != 0 && s0 == 0) {   // indent guides: a thin line at every indent level left of the text
            int ind = indentOf(l);
            if (ind < 0) {   // a blank line: the smaller indent of the lines around it
                int up = -1, dn = -1;
                for (int k = l - 1; k >= 0 && k >= l - 200; --k) { const int i = indentOf(k); if (i >= 0) { up = i; break; } }
                for (int k = l + 1; k < n && k <= l + 200; ++k) { const int i = indentOf(k); if (i >= 0) { dn = i; break; } }
                ind = (up < 0 || dn < 0) ? 0 : std::min(up, dn);
            }
            for (int g = 0; g < ind; g += tabSize_) {
                const float gx = std::floor(xOf(g));
                if (gx >= view_.x) dl.AddRectFilled(Vec2{gx, y}, Vec2{gx + 1.0f, y + lineH_}, pal_.indentGuide);
            }
        }
        if (haveBracket) {
            for (const CodePos& bp : {ba, bb}) if (bp.line == l && bp.col >= s0 && (bp.col < s1 || (lastSeg && bp.col == s1))) {
                const int a = ByteToColumn(l, bp.col);
                dl.AddRectFilled(Vec2{xOf(a), y}, Vec2{xOf(a + 1), y + lineH_}, pal_.bracketMatch);
            }
        }
        // the text, in runs of one colour
        LineTokens(l, toks);
        size_t ti = 0; int col = sc, runStart = 0, runCells = 0; Color32 runColor = 0; bool inRun = false;
        // composing: the preedit goes where the selection starts, in place of the selected part of this line; the rest moves right
        const bool compHere = comp_.active && focused && l == sa.line && rowOf(sa) == r;
        int insCol = 0, hideTo = 0, colShift = 0; bool insDone = false;
        if (compHere) {
            insCol = ByteToColumn(l, sa.col);
            hideTo = insCol;
            if (sel) hideTo = sb.line == l ? ByteToColumn(l, sb.col) : ByteToColumn(l, (int)s.size());
        }
        const int c0l = compHere ? std::max(0, c0 - compCells_) : c0;
        auto drawPreedit = [&] {
            const float px = xOf(insCol), pw = (float)compCells_ * charW_;
            const Color32 wash = ColorMulAlpha(pal_.selection, 0.5f);
            dl.AddRectFilled(Vec2{px, y}, Vec2{px + pw, y + lineH_}, wash);
            dl.AddText(Vec2{px, y + dy}, pal_.text[CodeColor_Default], comp_.text);
            dl.AddRectFilled(Vec2{px, y + lineH_ - 2.0f}, Vec2{px + pw, y + lineH_}, pal_.text[CodeColor_Default]);   // the preedit underline
        };
        auto flush = [&] {
            if (inRun) {
                while (!run.empty() && run.back() == ' ') run.pop_back();
                if (!run.empty()) dl.AddText(Vec2{xOf(runStart), y + dy}, runColor, run.c_str());
                run.clear(); inRun = false;
            }
        };
        for (int p = s0; p < s1;) {
            const int q = nextIdx(s, p);
            if (compHere && !insDone && col >= insCol) { flush(); drawPreedit(); insDone = true; colShift = compCells_ - (hideTo - insCol); }
            if (compHere && col >= insCol && col < hideTo) {   // replaced by the preedit
                col = s[(size_t)p] == '\t' ? nextTab(col, tabSize_) : col + 1; p = q;
                continue;
            }
            if (showWs_ && (s[(size_t)p] == ' ' || s[(size_t)p] == '\t') && col >= c0l && col < c1) {   // whitespace marks
                const float wx = xOf(col + colShift), wy = y + lineH_ * 0.5f;
                if (s[(size_t)p] == ' ') dl.AddRectFilled(Vec2{std::floor(wx + charW_ * 0.5f) - 1.0f, std::floor(wy) - 1.0f}, Vec2{std::floor(wx + charW_ * 0.5f) + 1.0f, std::floor(wy) + 1.0f}, wsCol);
                else {
                    const float we = xOf(nextTab(col, tabSize_) + colShift) - 2.0f;
                    dl.AddLine(Vec2{wx + 2.0f, wy}, Vec2{we, wy}, wsCol, 1.0f);
                    dl.AddLine(Vec2{we - 3.0f, wy - 3.0f}, Vec2{we, wy}, wsCol, 1.0f);
                    dl.AddLine(Vec2{we - 3.0f, wy + 3.0f}, Vec2{we, wy}, wsCol, 1.0f);
                }
            }
            if (s[(size_t)p] == '\t') { flush(); col = nextTab(col, tabSize_); p = q; continue; }
            if (col >= c1) break;
            if (col >= c0l) {
                while (ti < toks.size() && toks[ti].end <= p) ++ti;
                const unsigned char cid = (ti < toks.size() && toks[ti].start <= p) ? toks[ti].color : (unsigned char)CodeColor_Default;
                const Color32 cc = colorFor(pal_, cid);
                if (inRun && (cc != runColor || col + colShift != runStart + runCells)) flush();
                if (!inRun) {
                    if (s[(size_t)p] == ' ') { ++col; p = q; continue; }
                    inRun = true; runStart = col + colShift; runCells = 0; runColor = cc;
                }
                run.append(s, (size_t)p, (size_t)(q - p)); ++runCells;
            } else {
                while (ti < toks.size() && toks[ti].end <= p) ++ti;
            }
            ++col; p = q;
        }
        flush();
        if (compHere && !insDone) { drawPreedit(); insDone = true; }
        // a folded block: a box with "..." after its first line (a click on it unfolds)
        if (folding_ && lastSeg && IsFolded(l)) {
            const float bx = xOf(ByteToColumn(l, (int)s.size())) + charW_ * 0.5f, bw = charW_ * 3.0f + 4.0f;
            dl.AddRect(Vec2{bx, y + 1.0f}, Vec2{bx + bw, y + lineH_ - 1.0f}, foldCol, 2.0f, 1.0f);
            dl.AddText(Vec2{bx + 2.0f, y + dy}, foldCol, "...");
        }
        // error underlines
        const int len = (int)s.size();
        for (const CodeMarker& m : markers_) {
            if (m.line != l) continue;
            int b0 = m.colStart, b1 = m.colEnd;
            int fnb = 0; while (fnb < len && (s[(size_t)fnb] == ' ' || s[(size_t)fnb] == '\t')) ++fnb;
            int lnb = len; while (lnb > fnb && (s[(size_t)lnb - 1] == ' ' || s[(size_t)lnb - 1] == '\t')) --lnb;
            if (b1 < 0) { b1 = lnb; if (b0 <= 0) b0 = fnb; }
            b0 = std::min(std::max(b0, 0), len); b1 = std::min(std::max(b1, b0), len);
            if (s0 > 0 || !lastSeg) {   // word wrap: only the part on this row
                if (b1 < s0 || (b0 >= s1 && !lastSeg)) continue;
                b0 = std::max(b0, s0); b1 = std::min(b1, s1);
            }
            float x0 = xOf(ByteToColumn(l, b0)), x1 = xOf(ByteToColumn(l, b1));
            if (x1 - x0 < charW_) x1 = x0 + charW_;
            x0 = std::max(x0, view_.x); x1 = std::min(x1, vend.x);
            const Color32 uc = m.color ? m.color : pal_.errorUnderline;
            const float yy = y + lineH_ - 3.0f;
            int k = 0;
            for (float x = x0; x < x1; x += 2.0f, ++k) {
                const float xe = std::min(x + 2.0f, x1);
                dl.AddLine(Vec2{x, yy + ((k & 1) ? 0.0f : 2.0f)}, Vec2{xe, yy + ((k & 1) ? 2.0f : 0.0f)}, uc, 1.0f);
            }
        }
    }
    // the tooltip under the pointer: an error's message on its line, else what the host says about the word (SetHoverInfo)
    if (hovered && !in.blocked && !dragging_ && !dropping_) {
        const std::string tip = HoverText(in.mouseX, in.mouseY);
        if (!tip.empty()) SetTooltip("%s", tip.c_str());
    }
    // the caret (while composing: the caret inside the preedit) and where the host should put the candidate window
    imeCaret_ = imeComp_ = Rect{};
    if (focused) {
        const CodePos cp = comp_.active ? SelectionStart() : caret_;
        const int cr = rowOf(cp);
        const int ccol = ByteToColumn(cp.line, cp.col) - rowCol(cr) + (comp_.active ? compCaretCells_ : 0);
        const float cxp = textLeft_ + (float)ccol * charW_ - scrollX_, cyp = rowY(cr);
        const double t = GetTime() - lastActivity_;
        const bool vis = !Style().caretBlink || t < 0.0 || std::fmod(t, 1.0) < 0.5;
        if (cr >= first && cr <= last && vis) dl.AddRectFilled(Vec2{cxp, cyp + 1.0f}, Vec2{cxp + 2.0f, cyp + lineH_ - 1.0f}, pal_.caret);
        for (const Sel& e : extras_) {   // the other carets
            const int er = rowOf(e.caret);
            if (er < first || er > last || !vis) continue;
            const float ex = textLeft_ + (float)(ByteToColumn(e.caret.line, e.caret.col) - rowCol(er)) * charW_ - scrollX_, ey = rowY(er);
            dl.AddRectFilled(Vec2{ex, ey + 1.0f}, Vec2{ex + 2.0f, ey + lineH_ - 1.0f}, pal_.caret);
        }
        if (dropping_) {   // where dragged text would land
            const int dr = rowOf(dropAt_);
            const float ex = textLeft_ + (float)(ByteToColumn(dropAt_.line, dropAt_.col) - rowCol(dr)) * charW_ - scrollX_, ey = rowY(dr);
            dl.AddRectFilled(Vec2{ex, ey}, Vec2{ex + 2.0f, ey + lineH_}, ColorMulAlpha(pal_.caret, 0.7f));
        }
        const float lx = std::min(std::max(cxp, view_.x), std::max(view_.x, vend.x - 2.0f));
        const float ly = std::min(std::max(cyp, view_.y), std::max(view_.y, vend.y - lineH_));
        imeCaret_ = Rect{lx, ly, 2.0f, lineH_};
        if (comp_.active) imeComp_ = Rect{textLeft_ + (float)(ByteToColumn(cp.line, cp.col) - rowCol(cr)) * charW_ - scrollX_, ly, (float)compCells_ * charW_, lineH_};
        else imeComp_ = Rect{lx, ly, 0.0f, lineH_};
        ImeRect ir; ir.caret = imeCaret_; ir.composition = imeComp_;
        SetImeRect(ir);
    }
    dl.PopClipRect();

    // ---- scroll bars
    if (needV_) {
        dl.AddRectFilled(Vec2{vTrack_.x, vTrack_.y}, Vec2{vTrack_.x + vTrack_.w, vTrack_.y + vTrack_.h}, pal_.scrollTrack);
        const bool hot = dragVBar_ || inRect(vThumb_, in.mouseX, in.mouseY);
        dl.AddRectFilled(Vec2{vThumb_.x, vThumb_.y}, Vec2{vThumb_.x + vThumb_.w, vThumb_.y + vThumb_.h}, hot ? pal_.scrollThumbHot : pal_.scrollThumb, sbRound_);
    }
    if (needH_) {
        dl.AddRectFilled(Vec2{hTrack_.x, hTrack_.y}, Vec2{hTrack_.x + hTrack_.w, hTrack_.y + hTrack_.h}, pal_.scrollTrack);
        const bool hot = dragHBar_ || inRect(hThumb_, in.mouseX, in.mouseY);
        dl.AddRectFilled(Vec2{hThumb_.x, hThumb_.y}, Vec2{hThumb_.x + hThumb_.w, hThumb_.y + hThumb_.h}, hot ? pal_.scrollThumbHot : pal_.scrollThumb, sbRound_);
    }
    dl.PopClipRect();
}

} // namespace OkayUI
