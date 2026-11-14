// OkayUI::CodeEdit: see include/okay/UI/OkayUICodeEdit.hpp and docs/own_ui_code_editor.md.
#include "okay/UI/OkayUICodeEdit.hpp"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace OkayUI {

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
            bool lc = false;
            for (const std::string& p : g.lineComments)
                if (!p.empty() && i + (int)p.size() <= n && std::memcmp(s + i, p.data(), p.size()) == 0) { lc = true; break; }
            if (lc) { emit(i, n, CodeColor_Comment); return 0; }
            if (!g.blockStart.empty() && i + (int)g.blockStart.size() <= n && std::memcmp(s + i, g.blockStart.data(), g.blockStart.size()) == 0) {
                const int e = findStr(s, n, i + (int)g.blockStart.size(), g.blockEnd);
                if (e < 0) { emit(i, n, CodeColor_Comment); return 1; }
                const int end = e + (int)g.blockEnd.size();
                emit(i, end, CodeColor_Comment);
                i = end;
                continue;
            }
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
                else if (g.known.count(w)) col = CodeColor_Known;
                else {
                    int k = j; while (k < n && (s[k] == ' ' || s[k] == '\t')) ++k;
                    int b = i - 1; while (b >= 0 && (s[b] == ' ' || s[b] == '\t')) --b;
                    if (k < n && s[k] == '(') col = CodeColor_Function;
                    else if (b >= 0 && s[b] == '.') col = CodeColor_Member;
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
    p.currentLine = RGBA(255, 255, 255, 14); p.currentLineEdge = RGBA(255, 255, 255, 40);
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
    undo_.clear(); redo_.clear(); groupOpen_ = false; breakGroup_ = true; compound_ = false;
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
}

void CodeEdit::beginCompound() {
    redo_.clear();
    UndoGroup g; g.caretBefore = caret_; g.anchorBefore = anchor_; g.kind = Kind_Other;
    undo_.push_back(std::move(g));
    groupOpen_ = true; compound_ = true; curKind_ = Kind_Other;
}

void CodeEdit::endCompound() {
    compound_ = false;
    breakGroup_ = true;
    if (!undo_.empty() && undo_.back().edits.empty()) { undo_.pop_back(); groupOpen_ = false; }
}

CodePos CodeEdit::edit(int kind, CodePos from, CodePos to, const std::string& ins) {
    std::string removed = textIn(from, to);
    if (removed.empty() && ins.empty()) return from;
    const bool newGroup = !groupOpen_ || (!compound_ && (breakGroup_ || kind != curKind_ || kind == Kind_Other));
    if (newGroup) {
        redo_.clear();
        UndoGroup g; g.caretBefore = caret_; g.anchorBefore = anchor_; g.kind = kind;
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
#define CE_FINISH() do { if (groupOpen_ && !undo_.empty()) { undo_.back().caretAfter = caret_; undo_.back().anchorAfter = anchor_; } } while (0)

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

bool CodeEdit::InsertText(const char* text) {
    if (readOnly_) return false;
    const std::string s = sanitize(text);
    if (s.empty() && !HasSelection()) return false;
    int kind = Kind_Other;
    if (!s.empty() && s.size() <= 4 && s.find('\n') == std::string::npos && s.find('\t') == std::string::npos && nextIdx(s, 0) == (int)s.size())
        kind = s[0] == ' ' ? Kind_TypeSpace : (charClass(s, 0) == 1 ? Kind_TypeWord : Kind_TypePunct);
    return replaceSelection(s, kind);
}

bool CodeEdit::Backspace(bool word) {
    if (readOnly_) return false;
    if (HasSelection()) return replaceSelection("", Kind_Other);
    if (caret_.line == 0 && caret_.col == 0) return false;
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
        ins += indent;
        if (opener) ins += insertSpaces_ ? std::string((size_t)tabSize_, ' ') : std::string("\t");
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

bool CodeEdit::Copy() const {
    if (!HasSelection()) return false;
    SetClipboardText(SelectedText().c_str());
    return true;
}

bool CodeEdit::Cut() {
    if (readOnly_ || !HasSelection()) return false;
    const std::string s = SelectedText();
    if ((int)s.size() >= kClipboardMax) return false;   // the clipboard could not hold it all: never delete what was not copied
    SetClipboardText(s.c_str());
    return replaceSelection("", Kind_Other);
}

bool CodeEdit::Paste() {
    if (readOnly_) return false;
    const char* c = GetClipboardText();
    if (!c || !*c) return false;
    const std::string s = sanitize(c);
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

void CodeEdit::SetCaret(CodePos p, bool extend) { moveCaret(p, extend); }

void CodeEdit::SetSelection(CodePos anchor, CodePos caret) {
    anchor_ = Clamp(anchor); caret_ = Clamp(caret);
    desiredCol_ = -1; breakGroup_ = true; reveal_ = true;
}

void CodeEdit::SelectAll() {
    anchor_ = CodePos{0, 0};
    caret_ = CodePos{(int)lines_.size() - 1, (int)lines_.back().size()};
    desiredCol_ = -1; breakGroup_ = true; reveal_ = true;
}

void CodeEdit::SelectLine(int line) {
    if (line < 0 || line >= (int)lines_.size()) return;
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
    const int last = (int)lines_.size() - 1;
    CodePos p = caret_;
    switch (m) {
        case CodeMove_Left:
            if (HasSelection() && !extend) { moveCaret(SelectionStart(), false); return; }
            if (p.col > 0) p.col = prevIdx(lines_[(size_t)p.line], p.col); else if (p.line > 0) { p.line--; p.col = (int)lines_[(size_t)p.line].size(); }
            moveCaret(p, extend); return;
        case CodeMove_Right:
            if (HasSelection() && !extend) { moveCaret(SelectionEnd(), false); return; }
            if (p.col < (int)lines_[(size_t)p.line].size()) p.col = nextIdx(lines_[(size_t)p.line], p.col); else if (p.line < last) { p.line++; p.col = 0; }
            moveCaret(p, extend); return;
        case CodeMove_Up: case CodeMove_Down: case CodeMove_PageUp: case CodeMove_PageDown: {
            const int n = (m == CodeMove_PageUp || m == CodeMove_PageDown) ? std::max(1, visLines_ - 1) : 1;
            const int dir = (m == CodeMove_Up || m == CodeMove_PageUp) ? -1 : 1;
            if (desiredCol_ < 0) desiredCol_ = ByteToColumn(p.line, p.col);
            const int target = p.line + dir * n;
            if (target < 0) p = CodePos{0, 0};
            else if (target > last) p = CodePos{last, (int)lines_[(size_t)last].size()};
            else p = CodePos{target, ColumnToByte(target, (float)desiredCol_)};
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

bool CodeEdit::findIn(int line, int from, int& start) const {
    const std::string& s = lines_[(size_t)line];
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
    while (from <= (int)lines_[(size_t)line].size() && findIn(line, from, st) && st < limit) { best = st; from = st + (int)find_.size(); }
    if (best < 0) return false;
    start = best;
    return true;
}

void CodeEdit::SetFind(const std::string& needle, bool matchCase) {
    find_ = needle.find('\n') == std::string::npos ? needle : std::string();
    findCase_ = matchCase;
}

bool CodeEdit::FindNext() {
    if (find_.empty()) return false;
    const CodePos s = HasSelection() ? SelectionEnd() : caret_;
    const int n = (int)lines_.size(), len = (int)find_.size();
    int st;
    for (int k = 0; k <= n; ++k) {
        const int l = (s.line + k) % n;
        const int from = k == 0 ? s.col : 0;
        if (k == n) { if (findIn(l, 0, st) && st < s.col) { anchor_ = CodePos{l, st}; caret_ = CodePos{l, st + len}; reveal_ = true; desiredCol_ = -1; breakGroup_ = true; return true; } break; }
        if (findIn(l, from, st)) { anchor_ = CodePos{l, st}; caret_ = CodePos{l, st + len}; reveal_ = true; desiredCol_ = -1; breakGroup_ = true; return true; }
    }
    return false;
}

bool CodeEdit::FindPrev() {
    if (find_.empty()) return false;
    const CodePos s = SelectionStart();
    const int n = (int)lines_.size(), len = (int)find_.size();
    int st;
    for (int k = 0; k <= n; ++k) {
        const int l = ((s.line - k) % n + n) % n;
        const int limit = k == 0 ? s.col : (k == n ? INT_MAX : INT_MAX);
        if (k == n) { // back on the start line: matches at or after the selection start (wrapped round)
            int best = -1, from = s.col, q;
            while (findIn(l, from, q)) { best = q; from = q + len; }
            if (best >= 0) { st = best; anchor_ = CodePos{l, st}; caret_ = CodePos{l, st + len}; reveal_ = true; desiredCol_ = -1; breakGroup_ = true; return true; }
            break;
        }
        if (findBefore(l, limit, st)) { anchor_ = CodePos{l, st}; caret_ = CodePos{l, st + len}; reveal_ = true; desiredCol_ = -1; breakGroup_ = true; return true; }
    }
    return false;
}

int CodeEdit::FindCount() const {
    if (find_.empty()) return 0;
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
    beginCompound();
    for (int l = (int)lines_.size() - 1; l >= 0; --l) {
        std::vector<int> hits;
        for (int from = 0; findIn(l, from, st); from = st + len) hits.push_back(st);
        for (size_t k = hits.size(); k-- > 0;) { edit(Kind_Other, CodePos{l, hits[k]}, CodePos{l, hits[k] + len}, w); ++count; }
    }
    endCompound();
    caret_ = Clamp(caret_); anchor_ = Clamp(anchor_);
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
    moveCaret(CodePos{line, 0}, false);
    scrollY_ = (float)line * lineH_ - (view_.h - lineH_) * 0.5f;
    clampScroll();
}

Vec2 CodeEdit::PosToScreen(CodePos p) const {
    p = Clamp(p);
    return Vec2{textLeft_ + (float)ByteToColumn(p.line, p.col) * charW_ - scrollX_, view_.y + (float)p.line * lineH_ - scrollY_};
}

CodePos CodeEdit::ScreenToPos(float x, float y) const {
    int row = (int)std::floor((y - view_.y + scrollY_) / lineH_);
    row = std::min(std::max(row, 0), (int)lines_.size() - 1);
    return CodePos{row, ColumnToByte(row, (x - textLeft_ + scrollX_) / charW_)};
}

void CodeEdit::Focus() { /* needs the widget id: taken on the next Draw */ wantFocus_ = true; }

void CodeEdit::layout(const Rect& item) {
    if (maxDirty_) recomputeMax();
    const int caretCols = ByteToColumn(caret_.line, (int)lines_[(size_t)caret_.line].size());
    gutterW_ = (showBreakpoints_ ? lineH_ : 0.0f) + (showLineNumbers_ ? (float)(digitsOf((int)lines_.size()) + 2) * charW_ : 4.0f);
    if (gutterW_ > item.w * 0.5f) gutterW_ = item.w * 0.5f;
    const float sb = 10.0f;
    contentH_ = (float)lines_.size() * lineH_;
    contentW_ = (float)(std::max(maxBytes_, caretCols) + 3) * charW_ + 8.0f;
    const float vw = item.w - gutterW_, vh = item.h;
    needV_ = contentH_ > vh;
    needH_ = contentW_ > vw - (needV_ ? sb : 0.0f);
    if (needH_ && !needV_) needV_ = contentH_ > vh - sb;
    view_ = Rect{item.x + gutterW_, item.y, std::max(0.0f, vw - (needV_ ? sb : 0.0f)), std::max(0.0f, vh - (needH_ ? sb : 0.0f))};
    textLeft_ = view_.x + 4.0f;
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
    firstLine_ = std::min(std::max(0, (int)std::floor(scrollY_ / lineH_)), (int)lines_.size() - 1);
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
    if (in.commit && in.commit[0]) {   // what the input method finished: inserted like typed text, over the selection
        std::string t = sanitize(in.commit);
        t.erase(std::remove_if(t.begin(), t.end(), [](char c) { return c == '\n' || c == '\t'; }), t.end());
        if (!t.empty()) { InsertText(t.c_str()); any = true; }
    }
    if (in.text && in.text[0] && !ctrl) {
        std::string t = sanitize(in.text);
        t.erase(std::remove_if(t.begin(), t.end(), [](char c) { return c == '\n' || c == '\t'; }), t.end());
        if (!t.empty()) { InsertText(t.c_str()); any = true; }
    }
    if (comp_.active) {   // composing: the input method owns Backspace, the arrows, Enter and Escape
        if (any) lastActivity_ = GetTime();
        if (version_ != v0) changed = true;
        return;
    }
    if (in.selectAll) { SelectAll(); any = true; }
    if (in.copy) { Copy(); any = true; }
    if (in.cut) { Cut(); any = true; }
    if (in.paste) { Paste(); any = true; }
    if (in.enter) { NewLine(); any = true; }
    if (in.backspace) { Backspace(ctrl); any = true; }
    if (in.del) { DeleteForward(ctrl); any = true; }
    if (in.left) { Move(ctrl ? CodeMove_WordLeft : CodeMove_Left, shift); any = true; }
    if (in.right) { Move(ctrl ? CodeMove_WordRight : CodeMove_Right, shift); any = true; }
    if (in.up) { Move(CodeMove_Up, shift); any = true; }
    if (in.down) { Move(CodeMove_Down, shift); any = true; }
    if (in.home) { Move(ctrl ? CodeMove_DocStart : CodeMove_LineStart, shift); any = true; }
    if (in.end) { Move(ctrl ? CodeMove_DocEnd : CodeMove_LineEnd, shift); any = true; }
    if (IsKeyPressed(KeyId_PageUp)) { Move(CodeMove_PageUp, shift); any = true; }
    if (IsKeyPressed(KeyId_PageDown)) { Move(CodeMove_PageDown, shift); any = true; }
    if (!ctrl && IsKeyPressed(KeyId_Tab)) { Tab(shift); any = true; }
    if (ctrl && IsKeyPressed(KeyId_Z)) { if (shift) Redo(); else Undo(); any = true; }
    if (ctrl && IsKeyPressed(KeyId_Y)) { Redo(); any = true; }
    if (in.escape && HasSelection()) { moveCaret(caret_, false); any = true; }
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
            const int line = (int)std::floor((my - view_.y + scrollY_) / lineH_);
            if (line >= 0 && line < (int)lines_.size()) {
                if (showBreakpoints_ && mx < item.x + lineH_) {
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
            if (clicks_ == 1) moveCaret(p, GetIO().keyShift);
            else if (clicks_ == 2) SelectWordAt(p);
            else SelectLine(p.line);
            dragging_ = true;
        }
    }
    if (!held) { dragging_ = dragVBar_ = dragHBar_ = false; }
    else if (dragging_ && clicks_ == 1) {
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
    layout(item_);
    if (comp_.active) reveal_ = true;   // keep the preedit's caret in view while composing
    if (reveal_) {
        reveal_ = false;
        const CodePos rp = comp_.active ? SelectionStart() : caret_;
        const float top = (float)rp.line * lineH_;
        if (top < scrollY_) scrollY_ = top;
        else if (top + lineH_ > scrollY_ + view_.h) scrollY_ = top + lineH_ - view_.h;
        const float cx = (float)(ByteToColumn(rp.line, rp.col) + (comp_.active ? compCaretCells_ : 0)) * charW_;
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
    const int first = firstLine_;
    const int last = std::min(n - 1, (int)std::floor((scrollY_ + view_.h) / lineH_));
    auto rowY = [&](int l) { return view_.y + (float)l * lineH_ - scrollY_; };
    dl.PushClipRect(i0, i1);
    dl.AddRectFilled(i0, i1, pal_.background);

    // ---- gutter
    const Vec2 vend{view_.x + view_.w, view_.y + view_.h};
    dl.AddRectFilled(i0, Vec2{view_.x, vend.y}, pal_.gutter);
    dl.PushClipRect(i0, Vec2{view_.x, vend.y});
    for (int l = first; l <= last; ++l) {
        const float y = rowY(l);
        if (showLineNumbers_) {
            char num[16]; std::snprintf(num, sizeof num, "%d", l + 1);
            const float tw = (float)std::strlen(num) * charW_;
            const bool cur = focused && l == caret_.line;
            dl.AddText(Vec2{view_.x - charW_ - tw, y + (lineH_ - GetTextLineHeight()) * 0.5f}, cur ? pal_.lineNumberCurrent : pal_.lineNumber, num);
        }
        for (const CodeMarker& m : markers_) if (m.line == l) { dl.AddRectFilled(Vec2{view_.x - 3.0f, y}, Vec2{view_.x, y + lineH_}, m.color ? m.color : pal_.errorUnderline); break; }
        if (showBreakpoints_ && HasBreakpoint(l)) dl.AddCircleFilled(Vec2{item.x + lineH_ * 0.5f, y + lineH_ * 0.5f}, lineH_ * 0.3f, pal_.breakpoint);
    }
    dl.PopClipRect();

    // ---- text
    dl.PushClipRect(Vec2{view_.x, view_.y}, vend);
    CodePos ba, bb; const bool haveBracket = focused && MatchBracket(ba, bb);
    const CodePos sa = SelectionStart(), sb = SelectionEnd();
    const bool sel = HasSelection();
    const float textH = GetTextLineHeight(), dy = (lineH_ - textH) * 0.5f;
    const int c0 = std::max(0, (int)std::floor(scrollX_ / charW_) - 1), c1 = c0 + (int)(view_.w / charW_) + 4;
    auto xOf = [&](int col) { return textLeft_ + (float)col * charW_ - scrollX_; };
    std::vector<CodeToken> toks;
    std::string run;
    for (int l = first; l <= last; ++l) {
        const std::string& s = lines_[(size_t)l];
        const float y = rowY(l);
        const Vec2 r0{view_.x, y}, r1{vend.x, y + lineH_};
        if (focused && !sel && l == caret_.line) dl.AddRectFilled(r0, r1, pal_.currentLine);
        bool hasMarker = false;
        for (const CodeMarker& m : markers_) if (m.line == l) { if (!hasMarker) dl.AddRectFilled(r0, r1, pal_.errorLine); hasMarker = true; }
        if (sel && !comp_.active && l >= sa.line && l <= sb.line) {
            const int from = l == sa.line ? ByteToColumn(l, sa.col) : 0;
            const int to = l == sb.line ? ByteToColumn(l, sb.col) : ByteToColumn(l, (int)s.size()) + 1;
            if (to > from) dl.AddRectFilled(Vec2{xOf(from), y}, Vec2{xOf(to), y + lineH_}, focused ? pal_.selection : pal_.selectionInactive);
        }
        if (!find_.empty()) {
            int st;
            for (int from = 0; findIn(l, from, st); from = st + (int)find_.size()) {
                const int a = ByteToColumn(l, st), b = ByteToColumn(l, st + (int)find_.size());
                if (xOf(b) < view_.x) continue;
                if (xOf(a) > vend.x) break;
                dl.AddRectFilled(Vec2{xOf(a), y}, Vec2{xOf(b), y + lineH_}, pal_.findMatch);
            }
        }
        if (haveBracket) {
            for (const CodePos& bp : {ba, bb}) if (bp.line == l) {
                const int a = ByteToColumn(l, bp.col);
                dl.AddRectFilled(Vec2{xOf(a), y}, Vec2{xOf(a + 1), y + lineH_}, pal_.bracketMatch);
            }
        }
        // the text, in runs of one colour
        LineTokens(l, toks);
        size_t ti = 0; int col = 0, runStart = 0, runCells = 0; Color32 runColor = 0; bool inRun = false;
        // composing: the preedit goes where the selection starts, in place of the selected part of this line; the rest moves right
        const bool compHere = comp_.active && focused && l == sa.line;
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
        const int len = (int)s.size();
        for (int p = 0; p < len;) {
            const int q = nextIdx(s, p);
            if (compHere && !insDone && col >= insCol) { flush(); drawPreedit(); insDone = true; colShift = compCells_ - (hideTo - insCol); }
            if (compHere && col >= insCol && col < hideTo) {   // replaced by the preedit
                col = s[(size_t)p] == '\t' ? nextTab(col, tabSize_) : col + 1; p = q;
                continue;
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
        // error underlines
        for (const CodeMarker& m : markers_) {
            if (m.line != l) continue;
            int b0 = m.colStart, b1 = m.colEnd;
            int fnb = 0; while (fnb < len && (s[(size_t)fnb] == ' ' || s[(size_t)fnb] == '\t')) ++fnb;
            int lnb = len; while (lnb > fnb && (s[(size_t)lnb - 1] == ' ' || s[(size_t)lnb - 1] == '\t')) --lnb;
            if (b1 < 0) { b1 = lnb; if (b0 <= 0) b0 = fnb; }
            b0 = std::min(std::max(b0, 0), len); b1 = std::min(std::max(b1, b0), len);
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
        if (hovered && !in.blocked && in.mouseY >= y && in.mouseY < y + lineH_ && in.mouseX >= item.x && in.mouseX < vend.x)
            for (const CodeMarker& m : markers_) if (m.line == l && !m.message.empty()) { SetTooltip("%s", m.message.c_str()); break; }
    }
    // the caret (while composing: the caret inside the preedit) and where the host should put the candidate window
    imeCaret_ = imeComp_ = Rect{};
    if (focused) {
        const CodePos cp = comp_.active ? SelectionStart() : caret_;
        const int ccol = ByteToColumn(cp.line, cp.col) + (comp_.active ? compCaretCells_ : 0);
        const float cxp = xOf(ccol), cyp = rowY(cp.line);
        if (cp.line >= first && cp.line <= last) {
            const double t = GetTime() - lastActivity_;
            const bool vis = !Style().caretBlink || t < 0.0 || std::fmod(t, 1.0) < 0.5;
            if (vis) dl.AddRectFilled(Vec2{cxp, cyp + 1.0f}, Vec2{cxp + 2.0f, cyp + lineH_ - 1.0f}, pal_.caret);
        }
        const float lx = std::min(std::max(cxp, view_.x), std::max(view_.x, vend.x - 2.0f));
        const float ly = std::min(std::max(cyp, view_.y), std::max(view_.y, vend.y - lineH_));
        imeCaret_ = Rect{lx, ly, 2.0f, lineH_};
        if (comp_.active) imeComp_ = Rect{xOf(ByteToColumn(cp.line, cp.col)), ly, (float)compCells_ * charW_, lineH_};
        else imeComp_ = Rect{lx, ly, 0.0f, lineH_};
        ImeRect ir; ir.caret = imeCaret_; ir.composition = imeComp_;
        SetImeRect(ir);
    }
    dl.PopClipRect();

    // ---- scroll bars
    if (needV_) {
        dl.AddRectFilled(Vec2{vTrack_.x, vTrack_.y}, Vec2{vTrack_.x + vTrack_.w, vTrack_.y + vTrack_.h}, pal_.scrollTrack);
        const bool hot = dragVBar_ || inRect(vThumb_, in.mouseX, in.mouseY);
        dl.AddRectFilled(Vec2{vThumb_.x, vThumb_.y}, Vec2{vThumb_.x + vThumb_.w, vThumb_.y + vThumb_.h}, hot ? pal_.scrollThumbHot : pal_.scrollThumb, 3.0f);
    }
    if (needH_) {
        dl.AddRectFilled(Vec2{hTrack_.x, hTrack_.y}, Vec2{hTrack_.x + hTrack_.w, hTrack_.y + hTrack_.h}, pal_.scrollTrack);
        const bool hot = dragHBar_ || inRect(hThumb_, in.mouseX, in.mouseY);
        dl.AddRectFilled(Vec2{hThumb_.x, hThumb_.y}, Vec2{hThumb_.x + hThumb_.w, hThumb_.y + hThumb_.h}, hot ? pal_.scrollThumbHot : pal_.scrollThumb, 3.0f);
    }
    dl.PopClipRect();
}

} // namespace OkayUI
