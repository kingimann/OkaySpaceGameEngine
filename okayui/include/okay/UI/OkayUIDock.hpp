#pragma once
// OkayUI docking: a dock space inside the display, windows docked into the left / right / top / bottom / centre of a node,
// splitters, tabs when several windows share a node, drag a tab or a title bar to float it or to dock it somewhere else (with
// a drop-target preview), close a tab, and the layout saved to a text blob and back. Own code, off by default, not wired
// into any program. See docs/own_ui_toolkit.md section 4.30.
//
// Two layers:
//  * DockLayout is the data model: a binary tree of nodes (a split of two children, or a leaf holding tabs) plus the floating
//    windows. It knows nothing about drawing or input, so it is tested alone, including a fuzz that throws random operations and
//    random blobs at it. Its invariants (checked by Validate): the tree is a valid binary tree with unique ids, no empty leaf
//    except an empty root, every window name appears exactly once (in one leaf or in the floating list), ratios are finite in
//    (0, 1), the active tab is in range, the counts are within the limits.
//  * The immediate-mode calls (DockSpace, DockBegin, DockEnd) draw and drive a layout that lives in the OkayUI context:
//        OkayUI::DockSpace({0, 0, displayW, displayH});            // once per frame, before the windows
//        if (OkayUI::DockBegin("Inspector", &open)) { ...widgets... }  OkayUI::DockEnd();   // like Begin / End
//    A window the layout has not seen starts floating; drag it onto the dock space to dock it. A docked window whose tab is
//    not the chosen one makes DockBegin return false (still call DockEnd). Windows are keyed by title.
#include "okay/UI/OkayUI.hpp"

#include <string>
#include <vector>

namespace OkayUI {

enum DockSide : int { DockSide_Center = 0, DockSide_Left, DockSide_Right, DockSide_Top, DockSide_Bottom };

class DockLayout {
public:
    static constexpr int   kMaxNodes = 256;        // nodes (splits and leaves together)
    static constexpr int   kMaxWindows = 128;      // windows in the whole layout
    static constexpr int   kMaxNameLen = 96;       // bytes of a window name (longer names are cut)
    static constexpr int   kMaxDepth = 64;         // tree depth accepted from a blob
    static constexpr float kSplitter = 4.0f;       // thickness of the gap between two children
    static constexpr float kMinLeafW = 64.0f;      // smallest a leaf is made, unless a window asks for more
    static constexpr float kMinLeafH = 40.0f;      // smallest content height (the tab strip is extra)

    struct Node {
        int id = 0;
        int parent = 0;                   // 0: the root
        int child[2] = {0, 0};            // both non-zero for a split, both zero for a leaf
        bool horizontal = true;           // split only: true puts child 0 left of child 1, false puts it above
        float ratio = 0.5f;               // split only: the first child's share of the space (without the gap)
        std::vector<std::string> tabs;    // leaf only
        int active = 0;                   // leaf only: index of the chosen tab
        Rect rect;                        // from the last Compute
        bool IsSplit() const { return child[0] != 0; }
    };
    struct Floating { std::string name; Rect rect; bool dirty = true; };   // dirty: the host should (re)place the window at `rect`

    DockLayout();
    void Clear();                                    // one empty root leaf, nothing floating

    // ---- queries ----
    int Root() const { return root_; }
    const Node* Find(int id) const;
    const std::vector<Node>& Nodes() const { return nodes_; }
    const std::vector<Floating>& FloatingWindows() const { return floating_; }
    int NodeOf(const std::string& window) const;     // the leaf holding the window, 0 when it floats or is unknown
    bool Contains(const std::string& window) const;
    bool IsFloating(const std::string& window) const;
    const Floating* FindFloating(const std::string& window) const;
    std::vector<std::string> Windows() const;        // every window, docked first (tree order) then floating
    int WindowCount() const;
    int LeafAt(float x, float y) const;              // the leaf whose rect (from Compute) holds the point, 0 for none

    // ---- edits (each returns false and changes nothing when it cannot be done) ----
    /// Put the window in the layout as a floating window (no-op when it is already somewhere).
    bool AddFloating(const std::string& window, Rect rect);
    /// Move the window (new or already present) next to / into leaf `node`. Center adds a tab; a side splits the leaf and gives
    /// the new half to the window. A source leaf left empty disappears and its sibling takes the place of their parent.
    bool Dock(const std::string& window, int node, DockSide side);
    /// Like Dock with a side, but against the whole tree: the window gets a strip along that side of the dock space.
    bool DockRoot(const std::string& window, DockSide side);
    /// Take the window out of its leaf and let it float at `rect` (an already floating window just moves).
    bool Float(const std::string& window, Rect rect);
    /// Remove the window from the layout altogether (a closed tab).
    bool Close(const std::string& window);
    bool SetActive(const std::string& window);       // choose its tab
    bool MoveTab(const std::string& window, int index);   // reorder inside its leaf
    bool SetFloatingRect(const std::string& window, Rect rect, bool markDirty = false);
    bool TakeDirty(const std::string& window, Rect* rect);   // true once after the host should place a floating window
    void SetMinSize(const std::string& window, float w, float h);   // a docked window's smallest content size
    bool SetRatio(int splitNode, float ratio);       // clamped to the minimum sizes when the layout has been computed

    // ---- geometry ----
    /// Assign every node a rectangle inside `area`. `tabH` is the height of a leaf's tab strip. Children never get less than
    /// their minimum size unless the area is too small for both, then the space is shared in proportion to the minimums.
    void Compute(Rect area, float tabH);
    /// Leaf content (below the tab strip) of a node's rectangle.
    Rect ContentRect(int leaf) const;
    Rect SplitterRect(int splitNode) const;          // the gap between the children
    /// Move the divider of a split so the first child ends at `edge` (screen pixels along the split axis), within the minimums.
    bool SetSplitterPos(int splitNode, float edge);
    Vec2 MinSizeOf(int node) const;

    // ---- checking, repair, text ----
    bool Validate(std::string* why = nullptr) const;
    /// Rebuild the layout from what it holds, dropping what is broken (bad links, cycles, empty leaves, repeated names,
    /// non-finite numbers) and putting windows that were in broken parts into the floating list. Returns the number of fixes.
    int Repair();
    std::string Save() const;
    /// Replace the layout with the one in `blob`. The blob may be damaged: it is repaired as it is read (see Repair), `*repairs`
    /// gets the number of fixes. Returns false and leaves the layout alone when the blob is not a layout at all (no header).
    bool Load(const std::string& blob, int* repairs = nullptr);

    unsigned Version() const { return version_; }

private:
    struct Raw;
    int FindIndex(int id) const;
    Node* Mut(int id);
    int NewId();
    bool RemoveWindowFromTree(const std::string& window, int* sourceLeaf);
    void CollapseIfEmpty(int leaf);
    bool Rebuild(Raw& raw, int* repairs);
    float MinWOf(const std::string& w) const;
    float MinHOf(const std::string& w) const;
    void ComputeNode(int id, Rect r, float tabH, int depth);
    Vec2 MinSizeRec(int id, float tabH, int depth) const;

    std::vector<Node> nodes_;
    std::vector<Floating> floating_;
    struct MinSize { std::string name; float w, h; };
    std::vector<MinSize> mins_;
    int root_ = 1;
    int nextId_ = 2;
    float tabH_ = 24.0f;
    unsigned version_ = 0;
};

// ---- the dock space in the OkayUI context ---------------------------------------------------------------------------
/// The layout the calls below drive (and that tests inspect or edit).
DockLayout& GetDockLayout();
/// Forget the layout and any drag in progress (OkayUI::ResetState calls this).
void DockReset();
/// Lay the dock out in `area` (clipped to the display when the host gave Input::displayW/H), draw the node backgrounds, tab strips
/// and splitters and handle the mouse: select / reorder / close / tear off tabs, drag splitters, and while a window is being dragged show
/// the drop targets and a preview of where it would land, docking it on release. Call it once per frame before the DockBegin calls.
void DockSpace(Rect area);
/// Like Begin(title, p_open, flags). A docked window has no title bar of its own and fills its node's content area; only the
/// chosen tab of a node returns true. A floating window is an ordinary window whose title bar can be dragged onto the dock space.
/// `*p_open` is set false when its close button or tab is clicked, and the window leaves the layout. Always call DockEnd.
bool DockBegin(const char* title, bool* p_open = nullptr, int flags = 0);
void DockEnd();
/// Layout text for saving and loading (see DockLayout::Save / Load).
std::string DockSaveLayout();
bool DockLoadLayout(const std::string& blob, int* repairs = nullptr);
/// What a drag in progress would do now (for hosts and tests): the node, the side and the preview rectangle.
struct DockDropInfo { bool active = false; bool rootEdge = false; int node = 0; DockSide side = DockSide_Center; Rect preview; std::string window; };
DockDropInfo GetDockDrop();

} // namespace OkayUI
