// OkayUI::CodeEdit: see include/okay/UI/OkayUICodeEdit.hpp and docs/own_ui_code_editor.md.
#include "okay/UI/OkayUICodeEdit.hpp"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace OkayUI {

// float to int without undefined behaviour (a NaN, an infinity or 1e30 must not reach a bare cast): NaN gives 0, anything outside
// [-1e9, 1e9] is clamped. Every float that comes from the caller, the pointer or a scroll offset goes through this.
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
inline okay::text::Pos ToPos(CodePos p) { return okay::text::Pos{p.line < 0 ? 0u : (std::size_t)p.line, p.col < 0 ? 0u : (std::size_t)p.col}; }
inline CodePos FromPos(okay::text::Pos p) { return CodePos{(int)p.line, (int)p.col}; }

// 0 blank, 1 word, 2 punctuation
inline int charClass(const std::string& s, int i) {
    const unsigned char c = (unsigned char)s[(size_t)i];
    if (c == ' ' || c == '\t') return 0;
    if (c >= 0x80 || c == '_' || (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) return 1;
    return 2;
}

char partnerOf(char c) {
    switch (c) { case '(': return ')'; case ')': return '('; case '[': return ']'; case ']': return '['; case '{': return '}'; default: return '{'; }
}
bool isOpener(char c) { return c == '(' || c == '[' || c == '{'; }

bool inSkipToken(const std::vector<okay::text::Token>& t, std::size_t j) {
    // tokens are sorted and disjoint: find the last one starting at or before j
    size_t lo = 0, hi = t.size();
    while (lo < hi) { const size_t m = (lo + hi) / 2; if (t[m].begin <= j) lo = m + 1; else hi = m; }
    if (lo == 0) return false;
    const okay::text::Token& k = t[lo - 1];
    return j < k.end && (k.kind == CodeColor_String || k.kind == CodeColor_Char || k.kind == CodeColor_Comment);
}

int digitsOf(int n) { int d = 1; while (n >= 10) { n /= 10; ++d; } return d; }

// Display columns of byte offsets that only grow, over one line, without starting from the line start each time (a long line with
// thousands of find matches must cost one pass, not one pass per match).
struct ColumnWalker {
    const std::string& s; int tab; int b = 0, c = 0;
    int at(int byte) {
        const int n = (int)s.size();
        if (byte > n) byte = n;
        while (b < byte) { c = s[(size_t)b] == '\t' ? nextTab(c, tab) : c + 1; b = nextIdx(s, b); }
        return c;
    }
};

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
    pal_ = DefaultCodePalette();
    bpSet_ = buf_.NewMarkSet();
    markSet_ = buf_.NewMarkSet();
    updateModelOptions();
}

// The model repairs input the way this editor always did: bad bytes become '?', control characters are dropped. Edits are
// unlimited in size, and the undo log keeps 2000 steps.
void CodeEdit::updateModelOptions() {
    okay::text::Options o = buf_.GetOptions();
    o.tabSize = tabSize_;
    o.insertSpaces = insertSpaces_;
    o.maxBytes = (std::size_t)-1;
    o.undoGroups = 2000;
    o.badByte = "?";
    o.dropControls = true;
    buf_.SetOptions(o);
}

const std::string& CodeEdit::Line(int i) const { return buf_.LineRef(i < 0 ? (std::size_t)-1 : (std::size_t)i); }

std::string CodeEdit::GetText() const { return buf_.Text(); }

void CodeEdit::SetText(const std::string& text) {
    buf_.SetText(text);
    caret_ = anchor_ = CodePos{};
    groupOpen_ = false; breakGroup_ = true; compound_ = false;
    desiredCol_ = -1; maxDirty_ = true;
    scrollX_ = scrollY_ = 0;
    breakpoints_.erase(std::remove_if(breakpoints_.begin(), breakpoints_.end(), [&](int l) { return l < 0 || l >= (int)buf_.LineCount(); }), breakpoints_.end());
    mirrorBreakpoints();
    mirrorMarkers();
}

CodePos CodeEdit::Clamp(CodePos p) const {
    if (p.line < 0) return CodePos{0, 0};
    return FromPos(buf_.Clamp(ToPos(p)));
}

std::string CodeEdit::SelectedText() const { return buf_.Slice(ToPos(SelectionStart()), ToPos(SelectionEnd())); }

// A line shorter than this is simply walked; a longer one is checked once per text version (one pass over its bytes, no tab and no byte
// above 127) so that its columns need no walk at all.
static const std::size_t kSimpleScanMin = 256;

bool CodeEdit::simpleLine(int line) const {
    if (line < 0 || line >= (int)buf_.LineCount()) return false;
    const std::string& s = buf_.LineRef((size_t)line);
    auto scan = [&s] {
        unsigned char bad = 0;
        for (const char ch : s) { const unsigned char c = (unsigned char)ch; bad |= (unsigned char)((c >= 0x80) | (c == '\t')); }
        return bad == 0;
    };
    if (s.size() < kSimpleScanMin) return scan();
    const std::uint64_t v = buf_.Version();
    for (const SimpleLine& e : simple_) if (e.line == line && e.version == v) return e.simple;
    SimpleLine& e = simple_[simpleNext_]; simpleNext_ = (simpleNext_ + 1) & 3;
    e.line = line; e.version = v; e.simple = scan();
    return e.simple;
}

int CodeEdit::ByteToColumn(int line, int byte) const {
    if (line < 0) return 0;
    if (line < (int)buf_.LineCount() && buf_.LineRef((size_t)line).size() >= kSimpleScanMin && simpleLine(line))
        return std::min(std::max(byte, 0), (int)buf_.LineRef((size_t)line).size());
    return (int)buf_.DisplayColumn((std::size_t)line, byte < 0 ? 0u : (std::size_t)byte);
}

int CodeEdit::ColumnToByte(int line, float column) const {
    const std::string& s = Line(line);
    const int n = (int)s.size();
    if (n >= (int)kSimpleScanMin && simpleLine(line)) {
        if (column != column) return n;
        const float v = std::floor(column + 0.5f);       // the same rounding as the walk below: the first i with column < i + 0.5
        return v <= 0.0f ? 0 : (v >= (float)n ? n : (int)v);
    }
    int i = 0, col = 0;
    while (i < n) {
        const int next = s[(size_t)i] == '\t' ? nextTab(col, tabSize_) : col + 1;
        if (column < (float)col + (float)(next - col) * 0.5f) return i;
        col = next;
        i = nextIdx(s, i);
    }
    return n;
}

// The byte offset of the character whose cell (display column) is `cell`, or -1 when that cell is past the end of the line or is the
// filler of a tab.
int CodeEdit::byteAtCell(int line, int cell) const {
    if (cell < 0 || line < 0 || line >= (int)buf_.LineCount()) return -1;
    const std::string& s = buf_.LineRef((size_t)line);
    const int n = (int)s.size();
    if (n >= (int)kSimpleScanMin && simpleLine(line)) return cell < n ? cell : -1;
    int i = 0, col = 0;
    while (i < n) {
        const int next = s[(size_t)i] == '\t' ? nextTab(col, tabSize_) : col + 1;
        if (cell < next) return s[(size_t)i] == '\t' ? -1 : i;
        col = next;
        i = nextIdx(s, i);
    }
    return -1;
}

// ================================================================================================
// Edits and undo (the model keeps the log; this class decides what one undo step is)
// ================================================================================================
// The model's undo steps remember the selection before and after, so hand it the caret first and update "after" at the end.
void CodeEdit::syncCaretToModel() { buf_.SetSelections({okay::text::Selection{ToPos(anchor_), ToPos(caret_)}}); }

void CodeEdit::recordCaretInLastGroup() {
    if (!groupOpen_) return;
    syncCaretToModel();
    buf_.RecordSelectionsInLastGroup();
}

// Breakpoints and markers live in the model as line marks, so they move with edits; this copies the moved lines back.
void CodeEdit::mirrorBreakpoints() {
    std::vector<okay::text::LineMark> m;
    for (int l : breakpoints_) m.push_back(okay::text::LineMark{(std::size_t)l, 0});
    buf_.SetMarks(bpSet_, std::move(m));
}

void CodeEdit::mirrorMarkers() {
    std::vector<okay::text::LineMark> m;
    for (std::size_t i = 0; i < markers_.size(); ++i)
        if (markers_[i].line >= 0) m.push_back(okay::text::LineMark{(std::size_t)markers_[i].line, (std::uint32_t)i});
    buf_.SetMarks(markSet_, std::move(m));
}

void CodeEdit::refreshMarks() {
    if (breakpoints_.empty() && markers_.empty()) return;
    breakpoints_.clear();
    for (const okay::text::LineMark& m : buf_.Marks(bpSet_)) breakpoints_.push_back((int)m.line);
    std::vector<int> line(markers_.size(), -2);                 // -2: dropped, -1 or more: the new line
    for (const okay::text::LineMark& m : buf_.Marks(markSet_)) line[m.tag] = (int)m.line;
    std::vector<CodeMarker> nm;
    for (std::size_t i = 0; i < markers_.size(); ++i) {
        if (markers_[i].line < 0) { nm.push_back(std::move(markers_[i])); continue; }   // never on a line: stays as it was
        if (line[i] == -2) continue;
        markers_[i].line = line[i];
        nm.push_back(std::move(markers_[i]));
    }
    markers_.swap(nm);
    mirrorMarkers();
}

void CodeEdit::beginCompound() {
    syncCaretToModel();
    buf_.BeginGroup();
    compoundV0_ = buf_.Version();
    groupOpen_ = true; compound_ = true; curKind_ = Kind_Other;
}

void CodeEdit::endCompound() {
    compound_ = false;
    breakGroup_ = true;
    buf_.EndGroup();
    if (buf_.Version() == compoundV0_) groupOpen_ = false;      // an empty compound leaves no step behind
    refreshMarks();
}

CodePos CodeEdit::edit(int kind, CodePos from, CodePos to, const std::string& ins) {
    from = Clamp(from); to = Clamp(to);
    const okay::text::Pos a = ToPos(from), b = ToPos(to);
    if (a == b && buf_.Repair(ins).empty()) return from;
    const bool newGroup = !groupOpen_ || (!compound_ && (breakGroup_ || kind != curKind_ || kind == Kind_Other));
    for (int l = from.line; l <= to.line; ++l) if ((int)Line(l).size() >= maxBytes_) { maxDirty_ = true; break; }
    std::optional<okay::text::Pos> end;
    if (compound_) {
        end = buf_.Replace(a, b, ins);
    } else {
        bool reopened = false;
        if (newGroup) { syncCaretToModel(); groupOpen_ = true; curKind_ = kind; }
        else reopened = buf_.ReopenLastGroup();
        if (!reopened) buf_.BeginGroup();
        end = buf_.Replace(a, b, ins);
        buf_.EndGroup();
    }
    breakGroup_ = false;
    const CodePos e = end ? FromPos(*end) : from;
    for (int l = from.line; l <= e.line && l < (int)buf_.LineCount(); ++l) maxBytes_ = std::max(maxBytes_, (int)Line(l).size());
    if (!compound_) refreshMarks();
    return e;
}

// Record where the caret ended up, so Redo can put it back.
#define CE_FINISH() recordCaretInLastGroup()

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
    const std::string s = text ? buf_.Repair(text) : std::string();
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
    const CodePos from = FromPos(word ? buf_.PrevWordStart(ToPos(caret_)) : buf_.Prev(ToPos(caret_)));
    edit(word ? Kind_Other : Kind_Backspace, from, caret_, "");
    caret_ = anchor_ = from; reveal_ = true; desiredCol_ = -1;
    CE_FINISH();
    return true;
}

bool CodeEdit::DeleteForward(bool word) {
    if (readOnly_) return false;
    if (HasSelection()) return replaceSelection("", Kind_Other);
    const CodePos to = FromPos(word ? buf_.NextWordEnd(ToPos(caret_)) : buf_.Next(ToPos(caret_)));
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
    std::string ins = "\n";
    if (autoIndent_) {
        const std::string& L = Line(a.line);
        ins = buf_.NewlineText(ToPos(a));
        int bi = a.col - 1; while (bi >= 0 && (L[(size_t)bi] == ' ' || L[(size_t)bi] == '\t')) --bi;
        const char before = bi >= 0 ? L[(size_t)bi] : 0;
        int ai = a.col; while (ai < (int)L.size() && (L[(size_t)ai] == ' ' || L[(size_t)ai] == '\t')) ++ai;
        const char after = ai < (int)L.size() ? L[(size_t)ai] : 0;
        const CodePos at = FromPos(okay::text::Buffer::EndOfText(ToPos(a), ins));
        if (isOpener(before) && after == partnerOf(before)) ins += "\n" + buf_.IndentOf((std::size_t)a.line).substr(0, (size_t)std::min(a.col, (int)buf_.IndentBytes((std::size_t)a.line)));
        edit(Kind_Other, a, a, ins);
        endCompound();
        caret_ = anchor_ = at;
    } else {
        edit(Kind_Other, a, a, ins);
        endCompound();
        caret_ = anchor_ = CodePos{a.line + 1, 0};
    }
    reveal_ = true; desiredCol_ = -1;
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
    const unsigned v0 = Version();
    beginCompound();
    const std::string unit = insertSpaces_ ? std::string((size_t)tabSize_, ' ') : std::string("\t");
    for (int l = l1; l >= l0; --l) {
        const std::string& s = buf_.LineRef((size_t)l);
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
    if (Version() == v0) return false;
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
    if (s.size() >= kClipboardStoreMax) return false;   // the clipboard could not hold it all: never delete what was not copied
    SetClipboardText(s.c_str());
    return replaceSelection("", Kind_Other);
}

bool CodeEdit::Paste() {
    if (readOnly_) return false;
    const char* c = GetClipboardText();
    if (!c || !*c) return false;
    const std::string s = buf_.Repair(c);
    return replaceSelection(s, Kind_Other);
}

bool CodeEdit::Undo() {
    if (!buf_.Undo()) return false;
    const okay::text::Selection& sel = buf_.Selections().front();
    caret_ = Clamp(FromPos(sel.head)); anchor_ = Clamp(FromPos(sel.anchor));
    groupOpen_ = false; breakGroup_ = true; reveal_ = true; desiredCol_ = -1; maxDirty_ = true;
    refreshMarks();
    return true;
}

bool CodeEdit::Redo() {
    if (!buf_.Redo()) return false;
    const okay::text::Selection& sel = buf_.Selections().front();
    caret_ = Clamp(FromPos(sel.head)); anchor_ = Clamp(FromPos(sel.anchor));
    groupOpen_ = false; breakGroup_ = true; reveal_ = true; desiredCol_ = -1; maxDirty_ = true;
    refreshMarks();
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
    caret_ = CodePos{(int)buf_.LineCount() - 1, (int)buf_.LineRef(buf_.LineCount() - 1).size()};
    desiredCol_ = -1; breakGroup_ = true; reveal_ = true;
}

void CodeEdit::SelectLine(int line) {
    if (line < 0 || line >= (int)buf_.LineCount()) return;
    anchor_ = CodePos{line, 0};
    caret_ = line + 1 < (int)buf_.LineCount() ? CodePos{line + 1, 0} : CodePos{line, (int)buf_.LineRef((size_t)line).size()};
    desiredCol_ = -1; breakGroup_ = true; reveal_ = true;
}

bool CodeEdit::wordAt(CodePos p, CodePos& a, CodePos& b) const {
    p = Clamp(p);
    const std::string& s = buf_.LineRef((size_t)p.line);
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

void CodeEdit::Move(CodeMove m, bool extend) {
    const int last = (int)buf_.LineCount() - 1;
    CodePos p = caret_;
    switch (m) {
        case CodeMove_Left:
            if (HasSelection() && !extend) { moveCaret(SelectionStart(), false); return; }
            moveCaret(FromPos(buf_.Prev(ToPos(p))), extend); return;
        case CodeMove_Right:
            if (HasSelection() && !extend) { moveCaret(SelectionEnd(), false); return; }
            moveCaret(FromPos(buf_.Next(ToPos(p))), extend); return;
        case CodeMove_Up: case CodeMove_Down: case CodeMove_PageUp: case CodeMove_PageDown: {
            const int n = (m == CodeMove_PageUp || m == CodeMove_PageDown) ? std::max(1, visLines_ - 1) : 1;
            const int dir = (m == CodeMove_Up || m == CodeMove_PageUp) ? -1 : 1;
            if (desiredCol_ < 0) desiredCol_ = ByteToColumn(p.line, p.col);
            const int target = p.line + dir * n;
            if (target < 0) p = CodePos{0, 0};
            else if (target > last) p = CodePos{last, (int)buf_.LineRef((size_t)last).size()};
            else p = CodePos{target, ColumnToByte(target, (float)desiredCol_)};
            moveCaret(p, extend, true);
            if (n > 1) { scrollY_ += (float)(dir * n) * lineH_; clampScroll(); }
            return;
        }
        case CodeMove_WordLeft: moveCaret(FromPos(buf_.PrevWordStart(ToPos(p))), extend); return;
        case CodeMove_WordRight: moveCaret(FromPos(buf_.NextWordEnd(ToPos(p))), extend); return;
        case CodeMove_LineStart: {
            const std::string& s = buf_.LineRef((size_t)p.line);
            int first = 0; while (first < (int)s.size() && (s[(size_t)first] == ' ' || s[(size_t)first] == '\t')) ++first;
            p.col = p.col != first ? first : 0;
            moveCaret(p, extend); return;
        }
        case CodeMove_LineEnd: p.col = (int)buf_.LineRef((size_t)p.line).size(); moveCaret(p, extend); return;
        case CodeMove_DocStart: moveCaret(CodePos{0, 0}, extend); return;
        case CodeMove_DocEnd: moveCaret(CodePos{last, (int)buf_.LineRef((size_t)last).size()}, extend); return;
    }
}

// ================================================================================================
// Highlighter state cache
// ================================================================================================
// The highlighter is handed to the model as its tokenizer (the model caches the states and re-runs from the first changed line).
// The tokenizer holds its own copy of the highlighter, so a copied CodeEdit never points at the original.
void CodeEdit::installTokenizer() {
    if (!highlighter_) { buf_.SetTokenizer(nullptr); return; }
    buf_.SetTokenizer([h = highlighter_, tmp = std::vector<CodeToken>()](std::string_view line, std::uint32_t in, std::vector<okay::text::Token>& out) mutable -> std::uint32_t {
        tmp.clear();
        const int st = h(line.data(), (int)line.size(), (int)in, tmp);
        for (const CodeToken& t : tmp) out.push_back(okay::text::Token{t.start < 0 ? 0u : (std::size_t)t.start, t.end < 0 ? 0u : (std::size_t)t.end, (int)t.color});
        return (std::uint32_t)st;
    });
}

void CodeEdit::SetHighlighter(CodeHighlighter h) { highlighter_ = std::move(h); installTokenizer(); haveLanguage_ = false; ++langRev_; }

// The highlighter for the language plus the names the caller added: user types are types, documented identifiers are known words.
void CodeEdit::rebuildLanguage() {
    CodeLanguage l = language_;
    for (const std::string& n : userTypes_) l.types.insert(n);
    for (const auto& d : docs_) l.known.insert(d.first);
    highlighter_ = MakeCLikeHighlighter(l);
    installTokenizer();
    haveLanguage_ = true;
    ++langRev_;
}
void CodeEdit::SetLanguage(const CodeLanguage& lang) { language_ = lang; rebuildLanguage(); }
void CodeEdit::SetUserTypes(const std::vector<std::string>& names) {
    std::unordered_set<std::string> set(names.begin(), names.end());
    if (set == userTypes_) return;                        // nothing new: do not throw away the colouring of the whole text
    userTypes_ = std::move(set);
    if (haveLanguage_) rebuildLanguage();
}
void CodeEdit::SetIdentifierDocs(const std::unordered_map<std::string, std::string>& docs) {
    std::unordered_map<std::string, std::string> d;
    for (const auto& kv : docs) if (!kv.first.empty()) d.insert(kv);
    if (d == docs_) return;
    docs_ = std::move(d);
    if (haveLanguage_) rebuildLanguage();
}

// The identifier under a screen point: its cell must hold one of its characters (not a blank, not a tab's filler, not past the end).
std::string CodeEdit::IdentifierAt(float x, float y) const {
    if (!std::isfinite(x) || !std::isfinite(y) || !(lineH_ >= 1.0f) || !(charW_ >= 1.0f)) return std::string();
    if (!(x >= view_.x && x < view_.x + view_.w && y >= view_.y && y < view_.y + view_.h)) return std::string();
    const float rowF = std::floor((y - view_.y + scrollY_) / lineH_);
    if (!(rowF >= 0.0f && rowF < (float)buf_.LineCount())) return std::string();
    const int row = (int)rowF;
    const float cellF = std::floor((x - textLeft_ + scrollX_) / charW_);
    if (!(cellF >= 0.0f && cellF < 2.0e9f)) return std::string();
    const int at = byteAtCell(row, (int)cellF);
    if (at < 0) return std::string();
    const std::string& s = buf_.LineRef((size_t)row);
    auto isWord = [](unsigned char c) { return c == '_' || c >= 0x80 || (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); };
    if (!isWord((unsigned char)s[(size_t)at])) return std::string();
    int lo = at, hi = at + 1;
    while (lo > 0 && isWord((unsigned char)s[(size_t)lo - 1])) --lo;
    while (hi < (int)s.size() && isWord((unsigned char)s[(size_t)hi])) ++hi;
    return s.substr((size_t)lo, (size_t)(hi - lo));
}

void CodeEdit::LineTokens(int i, std::vector<CodeToken>& out) {
    out.clear();
    if (!highlighter_ || i < 0 || i >= (int)buf_.LineCount()) return;
    for (const okay::text::Token& t : buf_.LineTokens((std::size_t)i)) out.push_back(CodeToken{(int)t.begin, (int)t.end, (unsigned char)t.kind});
}

// ================================================================================================
// Find
// ================================================================================================
static inline char lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }

void CodeEdit::SetFind(const std::string& needle, bool matchCase) {
    find_ = needle.find('\n') == std::string::npos ? needle : std::string();
    findCase_ = matchCase;
}

bool CodeEdit::selectMatch(const std::optional<okay::text::Range>& r) {
    if (!r) return false;
    anchor_ = FromPos(r->begin); caret_ = FromPos(r->end);
    reveal_ = true; desiredCol_ = -1; breakGroup_ = true;
    return true;
}

bool CodeEdit::FindNext() {
    if (find_.empty()) return false;
    return selectMatch(buf_.Find(find_, ToPos(HasSelection() ? SelectionEnd() : caret_), findFlags(), false, true));
}

bool CodeEdit::FindPrev() {
    if (find_.empty()) return false;
    return selectMatch(buf_.Find(find_, ToPos(SelectionStart()), findFlags(), true, true));
}

int CodeEdit::FindCount() const {
    if (find_.empty()) return 0;
    return (int)buf_.FindAll(find_, findFlags()).size();
}

bool CodeEdit::ReplaceCurrent(const std::string& with) {
    if (readOnly_ || find_.empty() || !HasSelection()) return false;
    const std::string sel = SelectedText();
    bool eq = sel.size() == find_.size();
    for (size_t i = 0; eq && i < sel.size(); ++i) eq = findCase_ ? sel[i] == find_[i] : lower(sel[i]) == lower(find_[i]);
    if (!eq) return false;
    replaceSelection(buf_.Repair(with), Kind_Other);
    FindNext();
    return true;
}

int CodeEdit::ReplaceAll(const std::string& with) {
    if (readOnly_ || find_.empty()) return 0;
    syncCaretToModel();
    const std::size_t count = buf_.ReplaceAll(find_, with, findFlags());
    if (!count) return 0;
    groupOpen_ = true; breakGroup_ = true; compound_ = false; curKind_ = Kind_Other; maxDirty_ = true;
    caret_ = Clamp(caret_); anchor_ = Clamp(anchor_);
    refreshMarks();
    CE_FINISH();
    return (int)count;
}

// ================================================================================================
// Brackets
// ================================================================================================
bool CodeEdit::matchBracketImpl(CodePos& ra, CodePos& rb) {
    // brackets inside strings, characters and comments do not count; the model asks which positions those are
    const auto skip = [this](const okay::text::Pos& q) { return inSkipToken(buf_.LineTokens(q.line), q.col); };
    const std::optional<okay::text::BracketPair> m = buf_.MatchBracket(ToPos(caret_), skip, (std::size_t)-1, 2000);
    if (!m) return false;
    const bool caretOnOpen = m->at == m->open;
    ra = FromPos(caretOnOpen ? m->open : m->close);
    rb = FromPos(caretOnOpen ? m->close : m->open);
    return true;
}

bool CodeEdit::MatchBracket(CodePos& a, CodePos& b) {
    if (bVersion_ != Version() || bCaret_ != caret_) {
        bVersion_ = Version(); bCaret_ = caret_;
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
    mirrorBreakpoints();
}
bool CodeEdit::HasBreakpoint(int line) const { return std::binary_search(breakpoints_.begin(), breakpoints_.end(), line); }
void CodeEdit::ToggleBreakpoint(int line) {
    if (line < 0) return;
    auto it = std::lower_bound(breakpoints_.begin(), breakpoints_.end(), line);
    if (it != breakpoints_.end() && *it == line) breakpoints_.erase(it); else breakpoints_.insert(it, line);
    mirrorBreakpoints();
}

void CodeEdit::SetMarkers(const std::vector<CodeMarker>& m) {
    markers_ = m;
    mirrorMarkers();
}

// ================================================================================================
// View
// ================================================================================================
void CodeEdit::recomputeMax() {
    maxBytes_ = 0;
    for (std::size_t i = 0; i < buf_.LineCount(); ++i) maxBytes_ = std::max(maxBytes_, (int)buf_.LineRef(i).size());
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
    line = std::min(std::max(line, 0), (int)buf_.LineCount() - 1);
    moveCaret(CodePos{line, 0}, false);
    scrollY_ = (float)line * lineH_ - (view_.h - lineH_) * 0.5f;
    clampScroll();
}

Vec2 CodeEdit::PosToScreen(CodePos p) const {
    p = Clamp(p);
    return Vec2{textLeft_ + (float)ByteToColumn(p.line, p.col) * charW_ - scrollX_, view_.y + (float)p.line * lineH_ - scrollY_};
}

CodePos CodeEdit::ScreenToPos(float x, float y) const {
    int row = SafeInt(std::floor((y - view_.y + scrollY_) / lineH_));
    row = std::min(std::max(row, 0), (int)buf_.LineCount() - 1);
    return CodePos{row, ColumnToByte(row, (x - textLeft_ + scrollX_) / charW_)};
}

void CodeEdit::Focus() { /* needs the widget id: taken on the next Draw */ wantFocus_ = true; }

void CodeEdit::layout(const Rect& item) {
    if (maxDirty_) recomputeMax();
    const int caretCols = ByteToColumn(caret_.line, (int)buf_.LineRef((size_t)caret_.line).size());
    gutterW_ = (showBreakpoints_ ? lineH_ : 0.0f) + (showLineNumbers_ ? (float)(digitsOf((int)buf_.LineCount()) + 2) * charW_ : 4.0f);
    if (gutterW_ > item.w * 0.5f) gutterW_ = item.w * 0.5f;
    const float sb = 10.0f;
    contentH_ = (float)buf_.LineCount() * lineH_;
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
    firstLine_ = std::min(std::max(0, SafeInt(std::floor(scrollY_ / lineH_))), (int)buf_.LineCount() - 1);
    visLines_ = std::max(1, SafeInt(view_.h / lineH_));
}

bool CodeEdit::Focused() const { return wid_ != 0 && GetKeyboardFocusId() == wid_; }

static bool inRect(const Rect& r, float x, float y) { return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h; }

void CodeEdit::handleKeys(bool& changed) {
    const Input& in = GetFrameInput();
    const IO& io = GetIO();
    const bool ctrl = io.keyCtrl, shift = io.keyShift;
    bool any = false;
    const unsigned v0 = Version();
    if (in.commit && in.commit[0]) {   // what the input method finished: inserted like typed text, over the selection
        std::string t = buf_.Repair(in.commit ? in.commit : "");
        t.erase(std::remove_if(t.begin(), t.end(), [](char c) { return c == '\n' || c == '\t'; }), t.end());
        if (!t.empty()) { InsertText(t.c_str()); any = true; }
    }
    if (in.text && in.text[0] && !ctrl) {
        std::string t = buf_.Repair(in.text ? in.text : "");
        t.erase(std::remove_if(t.begin(), t.end(), [](char c) { return c == '\n' || c == '\t'; }), t.end());
        if (!t.empty()) { InsertText(t.c_str()); any = true; }
    }
    if (comp_.active) {   // composing: the input method owns Backspace, the arrows, Enter and Escape
        if (any) lastActivity_ = GetTime();
        if (Version() != v0) changed = true;
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
    if (Version() != v0) changed = true;
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
            const float lineF = std::floor((my - view_.y + scrollY_) / lineH_);
            const int line = (lineF >= 0.0f && lineF < 2.0e9f) ? (int)lineF : -1;     // NaN, infinity or a huge value: no line
            if (line >= 0 && line < (int)buf_.LineCount()) {
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
    const unsigned v0 = Version();
    hoverTip_.clear();
    const Vec2 avail = GetContentRegionAvail();
    if (w <= 0.0f) w = avail.x;
    if (h <= 0.0f) h = avail.y;
    if (!(w >= 1.0f) || !(h >= 1.0f) || w > 1e6f || h > 1e6f) return false;
    lineH_ = std::floor(GetTextLineHeight() * lineSpacing_ + 0.5f);
    if (!(lineH_ >= 1.0f)) lineH_ = 1.0f;
    charW_ = CalcTextSize("0").x;
    if (!(charW_ >= 1.0f)) charW_ = 1.0f;
    // A proportional font (the launcher look's Roboto) cannot be drawn as runs of text: the caret, the selection and the columns count
    // cells of one width. Then every glyph is drawn in its own cell (centred), and the widths are measured once per frame.
    grid_ = std::fabs(CalcTextSize("i").x - CalcTextSize("m").x) > 0.5f;
    if (grid_) {
        glyphW_.assign(95, 0.0f);
        for (int c = 32; c < 127; ++c) { const char one[2] = {(char)c, 0}; glyphW_[(size_t)(c - 32)] = CalcTextSize(one).x; }
    }
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
        compCells_ = SafeInt(std::ceil(CalcTextSize(comp_.text).x / charW_ - 0.001f));
        const std::string pre(comp_.text, (size_t)comp_.caret);
        compCaretCells_ = pre.empty() ? 0 : SafeInt(std::ceil(CalcTextSize(pre.c_str()).x / charW_ - 0.001f));
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
    return Version() != v0;
}

// Draw `text` with each character in its own cell starting at cell `firstCell` of a row (centred in the cell; a blank leaves its cell empty).
void CodeEdit::drawGlyphs(int firstCell, const std::string& text, Color32 color, float y) {
    const DrawList& dl = GetWindowDrawList();
    int cell = firstCell;
    for (std::size_t i = 0; i < text.size();) {
        const std::size_t j = (std::size_t)nextIdx(text, (int)i);
        const unsigned char c = (unsigned char)text[i];
        if (c != ' ') {
            const std::string one = text.substr(i, j - i);
            float w = (c >= 32 && c < 127 && (std::size_t)(c - 32) < glyphW_.size()) ? glyphW_[(size_t)(c - 32)] : CalcTextSize(one.c_str()).x;
            if (!(w >= 0.0f)) w = charW_;
            dl.AddText(Vec2{textLeft_ + (float)cell * charW_ - scrollX_ + (charW_ - w) * 0.5f, y}, color, one.c_str());
        }
        ++cell;
        i = j;
    }
}

static unsigned colorFor(const CodePalette& p, unsigned char id) { return p.text[id < (unsigned char)CodeColor_COUNT ? (int)id : (int)CodeColor_Default]; }

void CodeEdit::drawAll(const Rect& item, bool hovered) {
    const DrawList& dl = GetWindowDrawList();
    const bool focused = Focused();
    const Input& in = GetFrameInput();
    const Vec2 i0{item.x, item.y}, i1{item.x + item.w, item.y + item.h};
    const int n = (int)buf_.LineCount();
    const int first = firstLine_;
    const int last = std::min(n - 1, SafeInt(std::floor((scrollY_ + view_.h) / lineH_)));
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
    const int c0 = std::max(0, SafeInt(std::floor(scrollX_ / charW_)) - 1), c1 = c0 + SafeInt(view_.w / charW_) + 4;
    auto xOf = [&](int col) { return textLeft_ + (float)col * charW_ - scrollX_; };
    std::string run;
    for (int l = first; l <= last; ++l) {
        const std::string& s = buf_.LineRef((size_t)l);
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
        const bool longSimple = s.size() >= kSimpleScanMin && simpleLine(l);   // byte == column: no walk needed
        if (!find_.empty()) {
            ColumnWalker walk{s, tabSize_};
            const int needleLen = (int)find_.size();
            for (const std::size_t st : buf_.FindInLine((std::size_t)l, find_, findFlags())) {
                const int b0 = (int)st, b1 = b0 + needleLen;
                const int a = longSimple ? b0 : walk.at(b0), b = longSimple ? b1 : walk.at(b1);
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
        static const std::vector<okay::text::Token> kNoTok;
        const std::vector<okay::text::Token>& toks = highlighter_ ? buf_.LineTokens((size_t)l) : kNoTok;   // good until the next LineTokens call
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
                if (!run.empty()) {
                    if (grid_) drawGlyphs(runStart, run, runColor, y + dy);
                    else dl.AddText(Vec2{xOf(runStart), y + dy}, runColor, run.c_str());
                }
                run.clear(); inRun = false;
            }
        };
        const int len = (int)s.size();
        int p0 = 0;
        if (longSimple && !compHere && c0l > 0) {          // a long plain line: start at the first visible column, not at the line start
            p0 = std::min(c0l, len); col = p0;
            std::size_t lo = 0, hi = toks.size();
            while (lo < hi) { const std::size_t m = (lo + hi) / 2; if (toks[m].end <= (std::size_t)p0) lo = m + 1; else hi = m; }
            ti = lo;
        }
        for (int p = p0; p < len;) {
            const int q = nextIdx(s, p);
            if (compHere && !insDone && col >= insCol) { flush(); drawPreedit(); insDone = true; colShift = compCells_ - (hideTo - insCol); }
            if (compHere && col >= insCol && col < hideTo) {   // replaced by the preedit
                col = s[(size_t)p] == '\t' ? nextTab(col, tabSize_) : col + 1; p = q;
                continue;
            }
            if (s[(size_t)p] == '\t') { flush(); col = nextTab(col, tabSize_); p = q; continue; }
            if (col >= c1) break;
            if (col >= c0l) {
                while (ti < toks.size() && toks[ti].end <= (std::size_t)p) ++ti;
                const unsigned char cid = (ti < toks.size() && toks[ti].begin <= (std::size_t)p) ? (unsigned char)toks[ti].kind : (unsigned char)CodeColor_Default;
                const Color32 cc = colorFor(pal_, cid);
                if (inRun && (cc != runColor || col + colShift != runStart + runCells)) flush();
                if (!inRun) {
                    if (s[(size_t)p] == ' ') { ++col; p = q; continue; }
                    inRun = true; runStart = col + colShift; runCells = 0; runColor = cc;
                }
                run.append(s, (size_t)p, (size_t)(q - p)); ++runCells;
            } else {
                while (ti < toks.size() && toks[ti].end <= (std::size_t)p) ++ti;
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
        // the first message of the line, dimmed, after the end of its text
        if (inlineMsgs_) {
            for (const CodeMarker& m : markers_) {
                if (m.line != l || m.message.empty()) continue;
                std::string txt = "<  ";
                int chars = 0;
                for (std::size_t i = 0; i < m.message.size() && chars < 90; ++chars) {          // one line, at most 90 characters, whole characters
                    const std::size_t j = (std::size_t)nextIdx(m.message, (int)i);
                    const char c = m.message[i];
                    txt += (c == '\n' || c == '\r' || c == '\t') ? std::string(" ") : m.message.substr(i, j - i);
                    i = j;
                }
                if (chars >= 90) txt += "...";
                const float tx = xOf(ByteToColumn(l, len) + 2);
                if (tx < vend.x) dl.AddText(Vec2{tx, y + dy}, ColorMulAlpha(m.color ? m.color : pal_.errorUnderline, 0.65f), txt.c_str());
                break;
            }
        }
    }
    // ---- tooltip: the message of a marker on the line under the pointer, then the doc of the identifier under it
    hoverTip_.clear();
    if (hovered && !in.blocked && !dragging_ && !dragVBar_ && !dragHBar_ && !comp_.active && std::isfinite(in.mouseX) && std::isfinite(in.mouseY) &&
        in.mouseX >= item.x && in.mouseX < vend.x && in.mouseY >= view_.y && in.mouseY < vend.y) {
        const float rowF = std::floor((in.mouseY - view_.y + scrollY_) / lineH_);
        if (rowF >= 0.0f && rowF < (float)n) {
            const int row = (int)rowF;
            std::string tip;
            for (const CodeMarker& m : markers_) if (m.line == row && !m.message.empty()) { tip = m.message; break; }
            if (!docs_.empty()) {
                const std::string w = IdentifierAt(in.mouseX, in.mouseY);
                const auto it = w.empty() ? docs_.end() : docs_.find(w);
                if (it != docs_.end() && !it->second.empty()) { if (!tip.empty()) tip += "\n\n"; tip += it->second; }
            }
            if (tip.size() > 1000) { std::size_t cut = 1000; while (cut > 0 && ((unsigned char)tip[cut] & 0xC0) == 0x80) --cut; tip.resize(cut); }   // the tooltip box holds 1023 bytes
            hoverTip_ = std::move(tip);
            if (!hoverTip_.empty()) SetTooltip("%s", hoverTip_.c_str());
        }
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
