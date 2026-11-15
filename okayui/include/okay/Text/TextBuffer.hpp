#pragma once
// okay::text::Buffer: the data model of a code editor, with no drawing and no input in it. Pure standard C++ (no <mutex>,
// no <thread>, no SDL, no Dear ImGui, no OkayUI), so it can be tested headless and used by any front end.
//
// Own code. Written from general text-editor design (a line table, a position type, an edit log with groups, KMP for
// searching, Unicode's UTF-8 well-formedness rules from the Unicode standard chapter 3), in our own structure. See
// docs/own_code_editor.md.
//
// The model:
//  * The text is a list of lines. A line never contains '\n'. A text with N newlines has N+1 lines (an empty buffer has
//    one empty line). Line breaks are "\n" only: everything that goes in is normalised (CRLF and lone CR become "\n").
//  * Everything in the buffer is well-formed UTF-8. Input with invalid bytes is repaired on the way in: each bad byte
//    becomes U+FFFD. NUL also becomes U+FFFD. So no function here ever has to cope with a broken sequence.
//  * A position is {line, col}: a line number and a BYTE offset in that line. Positions given to any function are
//    clamped into the text and moved back to a character start, so hostile positions (huge numbers, the middle of a
//    character) are safe.
//  * An "offset" is a byte index in the whole text with lines joined by '\n'.
//  * Every change goes through the edit log. Changes can be grouped; Undo and Redo move a whole group at a time.
//  * Limits are explicit and overflow-safe: a change that would take the text over Options::maxBytes is refused.
//
// Not thread safe. A tokenizer callback must not call back into the buffer.
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace okay::text {

struct Pos {
    std::size_t line = 0;
    std::size_t col = 0;     // byte offset in the line
    friend bool operator==(const Pos& a, const Pos& b) { return a.line == b.line && a.col == b.col; }
    friend bool operator!=(const Pos& a, const Pos& b) { return !(a == b); }
    friend bool operator<(const Pos& a, const Pos& b) { return a.line != b.line ? a.line < b.line : a.col < b.col; }
    friend bool operator<=(const Pos& a, const Pos& b) { return !(b < a); }
};

struct Range {
    Pos begin, end;          // begin <= end for ranges the buffer returns
    bool Empty() const { return begin == end; }
    friend bool operator==(const Range& a, const Range& b) { return a.begin == b.begin && a.end == b.end; }
};

/// A caret with an optional selection: `head` is where the caret is, `anchor` the other end. Equal means no selection.
struct Selection {
    Pos anchor, head;
    bool Empty() const { return anchor == head; }
    Pos Lo() const { return anchor < head ? anchor : head; }
    Pos Hi() const { return anchor < head ? head : anchor; }
    friend bool operator==(const Selection& a, const Selection& b) { return a.anchor == b.anchor && a.head == b.head; }
};

struct Options {
    int tabSize = 4;                         // 1..16, used for display columns and indent steps
    bool insertSpaces = true;                // indent with spaces (true) or one tab per level (false)
    std::size_t maxBytes = 256u << 20;       // the text may not grow past this
    std::size_t undoGroups = 1000;           // oldest groups are dropped past this
    std::size_t undoBytes = 64u << 20;       // and past this many bytes held by the log (removed + inserted text)
    std::string badByte = "\xEF\xBF\xBD";    // what each invalid byte becomes (a front end may prefer "?")
    bool dropControls = false;               // drop C0 control characters other than tab and newline, and DEL, from input
};

enum class FindFlags : unsigned { None = 0, MatchCase = 1, WholeWord = 2 };
inline FindFlags operator|(FindFlags a, FindFlags b) { return FindFlags(unsigned(a) | unsigned(b)); }
inline bool Has(FindFlags a, FindFlags b) { return (unsigned(a) & unsigned(b)) != 0; }

/// A coloured piece of one line (byte range in the line). `kind` is the caller's own number.
struct Token { std::size_t begin = 0, end = 0; int kind = 0; };

/// Tokenizer hook: given one line and the state left by the line above (0 for the first line), append the line's tokens and
/// return the state to hand to the next line (so a block comment can run over lines). Must be a pure function of its input.
using Tokenizer = std::function<std::uint32_t(std::string_view line, std::uint32_t stateIn, std::vector<Token>& out)>;

/// Used by bracket matching: return true for a position that must not count (inside a string or comment).
using SkipFn = std::function<bool(const Pos&)>;

struct BracketPair { Pos open, close, at; };     // each is the position OF the bracket character; `at` is the one next to the caret

/// A line number that follows the text as it is edited (a breakpoint, an error mark). `tag` is the caller's own number.
struct LineMark { std::size_t line = 0; std::uint32_t tag = 0; };

class Buffer {
public:
    explicit Buffer(const Options& o = Options());

    // ---- options ----
    const Options& GetOptions() const { return opt_; }
    void SetOptions(const Options& o);       // clamps tabSize, trims the log if the limits shrank

    // ---- text ----
    /// Replace everything. Clears the undo log and selections. False (and no change) if over maxBytes.
    bool SetText(std::string_view s);
    std::string Text() const;
    std::size_t ByteSize() const { return bytes_; }
    std::size_t LineCount() const { return lines_.size(); }
    /// The line without its newline; an out-of-range line gives an empty view.
    std::string_view Line(std::size_t line) const;
    /// The same as a reference to the stored line (an empty string when out of range). Good until the next change.
    const std::string& LineRef(std::size_t line) const;
    std::size_t LineLength(std::size_t line) const { return Line(line).size(); }
    std::string Slice(Pos a, Pos b) const;   // text between two positions in either order
    std::uint64_t Version() const { return version_; }   // grows on every change, undo and redo included

    /// The repair every input goes through: CRLF and CR to "\n", invalid UTF-8 bytes and NUL to Options::badByte
    /// (U+FFFD by default); with Options::dropControls the other control characters are dropped too.
    std::string Repair(std::string_view s) const;
    /// The same with the default options.
    static std::string Sanitize(std::string_view s);
    static bool IsValidUtf8(std::string_view s);

    // ---- positions, offsets, columns ----
    Pos Clamp(Pos p) const;                  // into the text, on a character start
    std::size_t OffsetOf(Pos p) const;
    Pos PosOf(std::size_t offset) const;     // offsets past the end give the end
    Pos EndPos() const;
    Pos Prev(Pos p) const;                   // one character back (across a line break); the start stays
    Pos Next(Pos p) const;
    /// Display column of a byte offset: one column per character, a tab goes to the next multiple of tabSize.
    std::size_t DisplayColumn(std::size_t line, std::size_t byteCol) const;
    /// The byte offset of the character that covers (or starts at) a display column; past the end gives the line end.
    std::size_t ByteForDisplayColumn(std::size_t line, std::size_t displayCol) const;
    std::size_t CodepointCount(std::size_t line) const;

    // ---- editing (each is undoable) ----
    /// Insert text at a position. Returns the position after it, or nullopt if refused (over the size limit).
    std::optional<Pos> Insert(Pos at, std::string_view s);
    /// Remove [a, b) (either order). False if nothing was removed.
    bool Erase(Pos a, Pos b);
    /// Replace [a, b) with text. Returns the position after the new text, or nullopt if refused.
    std::optional<Pos> Replace(Pos a, Pos b, std::string_view s);

    /// Where the text ends up if `s` (already clean) is inserted at `at`.
    static Pos EndOfText(Pos at, std::string_view s);

    // ---- undo, redo, groups ----
    void BeginGroup();                       // nests; the outermost End closes one undo step
    void EndGroup();
    bool InGroup() const { return depth_ > 0; }
    bool CanUndo() const { return depth_ == 0 && !undo_.empty(); }
    bool CanRedo() const { return depth_ == 0 && !redo_.empty(); }
    bool Undo();                             // false while a group is open or nothing to undo; restores the selections
    bool Redo();
    std::size_t UndoDepth() const { return undo_.size(); }
    std::size_t RedoDepth() const { return redo_.size(); }
    void ClearHistory();
    /// Take the newest undo step back into an open group, so the next edits join it (typing a word is one step).
    /// False (nothing done) if a group is open or there is no step. Close it again with EndGroup().
    bool ReopenLastGroup();
    /// Make the newest undo step remember the current selections as "after" (what Redo restores).
    void RecordSelectionsInLastGroup();

    // ---- cursors and selections (several allowed) ----
    const std::vector<Selection>& Selections() const { return sel_; }
    /// Sorts, clamps and merges overlapping ones; at least one remains (an empty list becomes a caret at the start).
    void SetSelections(std::vector<Selection> s);
    void SetCursor(Pos p) { SetSelections({Selection{p, p}}); }
    void AddCursor(Pos p);
    std::string SelectedText() const;        // the selections' text joined with '\n'
    /// Replace every selection with text (typing; one undo step). Carets end after the text.
    bool TypeText(std::string_view s);
    /// Backspace: delete each non-empty selection, else the character before each caret. One undo step.
    bool Backspace();
    /// Delete: delete each non-empty selection, else the character after each caret.
    bool DeleteForward();
    /// Delete the words before / after each caret (selections are deleted as they are).
    bool DeleteWord(bool forward);

    // ---- line marks ----
    /// A set of line marks that move with the text: after an edit that replaces lines [a, b] by n lines, marks below b move by
    /// the difference and, when lines were removed, marks on the removed (last) lines of the range are dropped. Undo and redo
    /// move them the same way and do not bring dropped ones back. Marks are kept in the order given. Returns the set number.
    std::size_t NewMarkSet();
    void SetMarks(std::size_t set, std::vector<LineMark> marks);     // stored as given, even past the end
    const std::vector<LineMark>& Marks(std::size_t set) const;

    // ---- words ----
    /// Ctrl+Left: skip blanks, then a run of word characters or of punctuation; crosses to the previous line at column 0.
    Pos PrevWordStart(Pos p) const;
    /// Ctrl+Right: skip a run of word characters or punctuation, then blanks; crosses to the next line at the line end.
    Pos NextWordEnd(Pos p) const;
    Range WordAt(Pos p) const;               // the word under or next to p; empty range if none
    static bool IsWordByte(unsigned char c); // letters, digits, '_' and every byte of a non-ASCII character

    // ---- indent ----
    std::string IndentUnit() const;          // "    " (tabSize spaces) or "\t"
    std::size_t IndentBytes(std::size_t line) const;   // leading blanks of a line
    std::string IndentOf(std::size_t line) const;
    /// What to type for Enter at p: "\n" plus the line's indent, one level deeper after an opening bracket.
    std::string NewlineText(Pos p) const;
    /// Indent every non-empty line in [first, last] by one level (one undo step). False if nothing changed.
    bool IndentLines(std::size_t first, std::size_t last);
    /// Remove one level (a tab, or up to tabSize spaces) from each line in [first, last].
    bool OutdentLines(std::size_t first, std::size_t last);

    // ---- brackets ----
    /// If the character at p, or else the one before p, is one of ()[]{} give the pair (a bracket the skip function rejects
    /// gives no pair). Scans at most `maxScan` bytes and `maxLines` lines away from the bracket.
    std::optional<BracketPair> MatchBracket(Pos p, const SkipFn& skip = SkipFn(), std::size_t maxScan = 1u << 20,
                                            std::size_t maxLines = (std::size_t)-1) const;

    // ---- find and replace ----
    /// Next match starting at or after `from` (or the last one starting before it when `backward`). A needle may contain
    /// newlines. Case folding is ASCII only. Empty needles match nothing. Linear time in the text.
    std::optional<Range> Find(std::string_view needle, Pos from, FindFlags f = FindFlags::MatchCase, bool backward = false,
                              bool wrap = true) const;
    /// All non-overlapping matches in order, at most `maxCount`.
    std::vector<Range> FindAll(std::string_view needle, FindFlags f = FindFlags::MatchCase, std::size_t maxCount = 1000000) const;
    /// Start columns of the non-overlapping matches of a needle without a newline on one line (for drawing).
    std::vector<std::size_t> FindInLine(std::size_t line, std::string_view needle, FindFlags f = FindFlags::MatchCase) const;
    /// Replace all matches in one undo step. Returns how many; 0 also when refused (size limit).
    std::size_t ReplaceAll(std::string_view needle, std::string_view repl, FindFlags f = FindFlags::MatchCase);

    // ---- tokenizer hook ----
    void SetTokenizer(Tokenizer t);          // an empty function turns colouring off
    /// Tokens of a line. The tokenizer runs over the lines above it once (their end states are cached until a change) and then
    /// over the line itself; the last line asked for is kept, so asking for it again costs nothing. The reference is good until
    /// the next call or change.
    const std::vector<Token>& LineTokens(std::size_t line) const;

private:
    struct EditRec { Pos at; std::string removed, inserted; };
    struct Group { std::vector<EditRec> edits; std::vector<Selection> before, after; std::size_t bytes = 0; };

    Pos RawInsert(Pos at, const std::string& s);                 // s already sanitised, at already clamped
    std::string RawErase(Pos a, Pos b);                          // clamped and ordered
    Pos RawReplace(Pos a, Pos b, const std::string& ins, std::string* removed);   // erase, insert, move the line marks
    bool ApplyEdit(Pos a, Pos b, std::string_view s, std::optional<Pos>* endOut, bool adjustSel);
    void Touch(std::size_t firstLine);                           // version, caches
    void TrimHistory();
    void AdjustForErase(Pos& p, Pos a, Pos b) const;
    void AdjustForInsert(Pos& p, Pos at, Pos end) const;
    void EnsureStarts() const;
    bool EditEachSelection(int mode, std::string_view text);     // 0 type, 1 backspace, 2 delete, 3 word back, 4 word forward
    std::size_t RangeBytes(Pos a, Pos b) const;                  // bytes in [a, b), clamped and ordered, no offset table
    static constexpr std::size_t kMaxCursors = 10000;

    Options opt_;
    std::vector<std::string> lines_;
    std::size_t bytes_ = 0;
    std::uint64_t version_ = 0;
    std::vector<Selection> sel_;

    std::deque<Group> undo_, redo_;
    std::size_t logBytes_ = 0;
    Group open_;
    int depth_ = 0;

    mutable std::vector<std::size_t> starts_;                    // line start offsets, lazily rebuilt
    mutable bool startsOk_ = false;

    Tokenizer tok_;
    mutable std::vector<std::uint32_t> tokState_;                // tokState_[i]: tokenizer state at the start of line i, valid for i <= tokValid_
    mutable std::size_t tokValid_ = 0;
    mutable std::vector<Token> tokOut_;                          // the tokens of the last line asked for
    mutable std::size_t tokOutLine_ = (std::size_t)-1;
    std::vector<std::vector<LineMark>> marks_;
};

}  // namespace okay::text
