#pragma once
// The Project panel's two layouts as plain decisions (no ImGui, no files):
//   Two columns   a folder tree on the left, the folder's contents on the right, a draggable divider between them
//   One column    a single list where folders open in place and show their contents beneath them; no right pane
// This file answers: how wide each side is and how far the divider may move (ClampTreeWidth, SplitWidths,
// DragTreeWidth), which rows an open-in-place list shows (FlattenOpen), where the arrow of a row is (RowIndent,
// OnDisclosureArrow) and where a key moves the selection (MoveSelection). The panel in main.cpp only measures,
// asks these functions and draws the answer. The choice itself (one or two columns) is saved with the editor layout
// settings (okay::edlayout::ViewPrefs::projectColumns in EditorLayoutMath.hpp); an old file without it means two.
// Written from the panel's own behaviour and the usual rules for tree lists; no outside code. No <mutex> / <thread>.
#include <algorithm>
#include <cstddef>
#include <set>
#include <string>
#include <vector>

namespace oked::projectlayout {

enum class Columns { One = 1, Two = 2 };

/// 1 means one column; anything else (0, junk, a missing key) means the default, two columns.
inline Columns ColumnsFromInt(int v) { return v == 1 ? Columns::One : Columns::Two; }
inline int ColumnsToInt(Columns c) { return c == Columns::One ? 1 : 2; }
inline Columns Other(Columns c) { return c == Columns::One ? Columns::Two : Columns::One; }
inline const char* ColumnsLabel(Columns c) { return c == Columns::One ? "One column" : "Two columns"; }

// ---- Widths (pixels at the panel's own scale) ------------------------------------------------------

inline constexpr float kMinTreeW = 140.0f;      ///< the folder tree is never narrower than this
inline constexpr float kMaxTreeW = 480.0f;      ///< ... nor wider
inline constexpr float kMinContentW = 200.0f;   ///< the contents side keeps at least this much
inline constexpr float kSplitterW = 7.0f;       ///< the grab strip between the two sides
inline constexpr float kDefaultTreeW = 200.0f;

/// The folder tree's width for a wanted width: kMinTreeW..kMaxTreeW, and the contents side keeps kMinContentW.
/// A panel too narrow for both minimums gives the tree 40% of it (never negative, never the whole panel).
inline float ClampTreeWidth(float total, float want) {
    if (total <= 0.0f) return 0.0f;
    const float hi = std::min(kMaxTreeW, total - kMinContentW - kSplitterW);
    if (hi < kMinTreeW) return total * 0.4f;
    return std::clamp(want, kMinTreeW, hi);
}

struct Split { float tree = 0, splitter = 0, content = 0; };

/// How the panel's width is shared. One column: everything is content.
inline Split SplitWidths(Columns c, float total, float wantTree) {
    Split s;
    if (c == Columns::One) { s.content = std::max(0.0f, total); return s; }
    s.tree = ClampTreeWidth(total, wantTree);
    s.splitter = total > s.tree ? std::min(kSplitterW, total - s.tree) : 0.0f;
    s.content = std::max(0.0f, total - s.tree - s.splitter);
    return s;
}

/// The tree width while the divider is dragged by `dx` pixels.
inline float DragTreeWidth(float total, float current, float dx) { return ClampTreeWidth(total, current + dx); }

// ---- Rows of the one-column list ---------------------------------------------------------------------

inline constexpr float kIndentStep = 18.0f;   ///< how far each folder level moves right
inline constexpr float kArrowW = 16.0f;       ///< the open / close arrow's slot at the start of a row

inline float RowIndent(int depth) { return depth <= 0 ? 0.0f : depth * kIndentStep; }

/// Is a click at `mouseX` on the arrow of a row that starts at `rowX0` and sits `depth` levels deep?
inline bool OnDisclosureArrow(int depth, float rowX0, float mouseX) {
    const float a = rowX0 + RowIndent(depth);
    return mouseX >= a && mouseX < a + kArrowW;
}

/// What a folder holds, as the caller found it (a file or a folder, with an id such as its path).
struct Item { std::string id; bool isFolder = false; };

/// One visible row.
struct Row {
    std::string id;
    int depth = 0;
    bool isFolder = false;
    bool open = false;   ///< a folder whose contents are shown beneath it
};

/// The rows to show: the contents of `rootId`, and under every folder in `expanded` its own contents, in order.
/// `childrenOf(id)` returns the items of a folder. A folder inside a closed folder is never asked for.
/// `maxDepth` stops a loop in the data (a folder that contains itself).
template <class ChildrenOf>
inline std::vector<Row> FlattenOpen(const std::string& rootId, const std::set<std::string>& expanded,
                                    ChildrenOf childrenOf, int maxDepth = 16) {
    std::vector<Row> out;
    struct Walk {
        const std::set<std::string>& ex; ChildrenOf& kids; std::vector<Row>& out; int maxDepth;
        void go(const std::string& id, int depth) {
            for (const Item& it : kids(id)) {
                Row r; r.id = it.id; r.depth = depth; r.isFolder = it.isFolder;
                r.open = it.isFolder && ex.count(it.id) != 0 && depth < maxDepth;
                out.push_back(r);
                if (r.open) go(it.id, depth + 1);
            }
        }
    };
    Walk w{expanded, childrenOf, out, maxDepth};
    w.go(rootId, 0);
    return out;
}

inline int IndexOfId(const std::vector<Row>& rows, const std::string& id) {
    for (std::size_t i = 0; i < rows.size(); ++i) if (rows[i].id == id) return (int)i;
    return -1;
}

/// The row of the folder that holds row `i` (the nearest row above with a smaller depth), or -1 at the top level.
inline int ParentRow(const std::vector<Row>& rows, int i) {
    if (i <= 0 || i >= (int)rows.size()) return -1;
    for (int j = i - 1; j >= 0; --j) if (rows[j].depth < rows[i].depth) return j;
    return -1;
}

/// Does the row have rows beneath it that belong to it?
inline bool HasChildrenShown(const std::vector<Row>& rows, int i) {
    return i >= 0 && i + 1 < (int)rows.size() && rows[i + 1].depth > rows[i].depth;
}

// ---- Keyboard ----------------------------------------------------------------------------------------

enum class Key { Up, Down, Left, Right, Home, End, Enter };

/// What a key press asks for. `select` is the row to select next (-1 = none). `expand` / `collapse` name a row whose folder
/// should open / close in place, `open` names a row to open (Enter). Each is -1 when not asked.
struct Move { int select = -1; int expand = -1; int collapse = -1; int open = -1; };

inline Move MoveSelection(const std::vector<Row>& rows, int cur, Key key) {
    Move m;
    const int n = (int)rows.size();
    if (n == 0) return m;
    m.select = (cur >= 0 && cur < n) ? cur : -1;
    const bool have = m.select >= 0;
    switch (key) {
        case Key::Down: m.select = have ? std::min(cur + 1, n - 1) : 0; break;
        case Key::Up:   m.select = have ? std::max(cur - 1, 0) : 0; break;
        case Key::Home: m.select = 0; break;
        case Key::End:  m.select = n - 1; break;
        case Key::Enter: if (have) m.open = cur; break;
        case Key::Right:
            if (!have) { m.select = 0; break; }
            if (rows[cur].isFolder) {
                if (!rows[cur].open) m.expand = cur;
                else if (HasChildrenShown(rows, cur)) m.select = cur + 1;
            }
            break;
        case Key::Left:
            if (!have) { m.select = 0; break; }
            if (rows[cur].isFolder && rows[cur].open) m.collapse = cur;
            else { const int p = ParentRow(rows, cur); if (p >= 0) m.select = p; }
            break;
    }
    return m;
}

}  // namespace oked::projectlayout
