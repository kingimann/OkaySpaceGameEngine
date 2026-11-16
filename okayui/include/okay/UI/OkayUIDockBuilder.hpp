#pragma once
// A builder for dock layouts written as code, the way the editor's layout presets are written today: make a root node, split it into
// a side and an opposite part with a ratio, put windows into the parts, finish. The calls have the usual names (DockBuilderAddNode,
// DockBuilderSplitNode, DockBuilderDockWindow, DockBuilderFinish, ...) so a preset written for another toolkit's builder keeps its
// structure. Own code, written from how such presets are described (split a node, name the new parts, dock windows into them) and
// from OkayUI's own DockLayout; nothing is copied from another project. See docs/own_ui_toolkit.md section 4.37.
//
// Two layers, like the dock itself:
//  * DockBuilderTree is the pure model: a binary tree of nodes with windows in the leaves. It knows nothing about drawing, is tested
//    alone, and Build() turns it into a DockLayout (the model the dock space uses), dropping parts with no windows.
//  * The DockBuilder* functions keep one tree in the OkayUI context; DockBuilderFinish(root) builds it into GetDockLayout().
//
// Differences from the usual builder, said plainly: node ids are plain integers (GetID of a name is the usual way to pick the root's);
// SplitNode makes TWO new leaves (the side and the opposite) and moves what the split node held into the opposite one, so the id you
// split stays the container; a part with no windows is dropped by Build (the dock model has no empty leaves); node sizes are kept for
// queries but do not change the result (ratios do the work); there are no dock node flags (accepted and ignored).
#include "okay/UI/OkayUIDock.hpp"
#include "okay/UI/OkayUIImCompat.hpp"

#include <string>
#include <vector>

namespace OkayUI {

struct DockBuilderNode {
    unsigned id = 0;
    unsigned parent = 0;                  // 0 for a root
    unsigned child[2] = {0, 0};           // [0] the side part, [1] the opposite part; both 0 for a leaf
    int      dir = Dir_Right;             // split only: the side the child[0] part sits on (Dir_Left, Dir_Right, Dir_Up, Dir_Down)
    float    ratio = 0.5f;                // split only: child[0]'s share of the space, 0..1 exclusive
    Vec2     size;                        // SetNodeSize (informational)
    std::vector<std::string> windows;     // leaf only, in tab order
    bool IsSplit() const { return child[0] != 0; }
};

class DockBuilderTree {
public:
    static constexpr int kMaxNodes = 256;
    /// A new empty root leaf with this id (0: a fresh id). An existing node with that id is replaced (with its subtree). Returns the id,
    /// or 0 when the tree is full.
    unsigned AddNode(unsigned id = 0);
    /// Remove the node and everything under it. Its parent (if any) is dropped as well and the sibling takes its place.
    void RemoveNode(unsigned id);
    void SetNodeSize(unsigned id, Vec2 size);
    /// Split `node` along `dir`: a new leaf on that side takes `ratio` of the space (clamped to 0.05..0.95) and a new opposite leaf takes
    /// the rest and everything `node` held (windows or sub-parts). `node` becomes the split. Returns the side leaf's id (0 when it
    /// cannot be done: unknown node, bad direction, tree full); *outSide and *outOpposite (optional) get the two ids.
    unsigned SplitNode(unsigned node, int dir, float ratio, unsigned* outSide = nullptr, unsigned* outOpposite = nullptr);
    /// Put a window in a leaf (as its last tab); a window already somewhere else moves. False for an unknown or split node or an empty name.
    bool DockWindow(const std::string& window, unsigned node);

    const DockBuilderNode* GetNode(unsigned id) const;
    unsigned NodeOfWindow(const std::string& window) const;   // 0 when it is nowhere
    int NodeCount() const { return (int)nodes_.size(); }
    void Clear() { nodes_.clear(); next_ = 0x00100000u; }
    bool Validate(std::string* why = nullptr) const;

    /// Build the layout under `root` into `out` (replacing it): windows of each leaf in tab order with the first one chosen, splits with
    /// their ratios and sides, parts with no windows dropped. False (and `out` untouched) when `root` is unknown. The result passes
    /// DockLayout::Validate.
    bool Build(unsigned root, DockLayout& out) const;

private:
    DockBuilderNode* Mut(unsigned id);
    unsigned NewId();
    void RemoveRec(unsigned id);
    bool Realize(unsigned id, int layoutLeaf, DockLayout& out, int depth) const;
    std::vector<DockBuilderNode> nodes_;
    unsigned next_ = 0x00100000u;
};

// ---- the same calls over one tree kept in the OkayUI context (OkayUI::ResetState clears it) -----------------------------------------
/// The tree the calls below work on (tests inspect it).
DockBuilderTree& GetDockBuilderTree();
unsigned DockBuilderAddNode(unsigned id = 0, int flags = 0);
void DockBuilderRemoveNode(unsigned id);
void DockBuilderSetNodeSize(unsigned id, Vec2 size);
unsigned DockBuilderSplitNode(unsigned node, int dir, float ratio, unsigned* outSide = nullptr, unsigned* outOpposite = nullptr);
void DockBuilderDockWindow(const char* window, unsigned node);
const DockBuilderNode* DockBuilderGetNode(unsigned id);
/// Build the tree under `root` into the live dock layout (GetDockLayout). Windows the layout held and the tree does not mention are
/// dropped; DockBegin adds them again as floating windows.
void DockBuilderFinish(unsigned root);

} // namespace OkayUI
