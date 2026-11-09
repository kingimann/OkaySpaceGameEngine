#pragma once
// A focus graph for keyboard and gamepad menu navigation (docs/accessibility.md).
//
// It knows nothing about rendering or scenes: it holds rectangles (where each focusable thing is
// on screen) and answers "what gets focus when the player presses Down?". Directional moves
// find the nearest rectangle that way by geometry, so grids and side-by-side buttons navigate
// the way they look; Tab and Shift+Tab walk a tab order; edges wrap; a focus trap (a modal)
// keeps focus on its own controls and gives it back when it closes; an explicit link
// overrides the geometry for one direction.
//
// UINavigation.hpp's NavigateUI uses the same scoring (FocusScore); BuildFocusGraph there
// fills a graph from a scene's widgets.
#include <cstddef>
#include <vector>

namespace okay {

enum class FocusDir { Up, Down, Left, Right, Next, Previous };

/// One focusable thing.
struct FocusNode {
    int id = 0;                   ///< the caller's id, 1 or more (0 = FocusGraph::Add assigns one)
    float x = 0, y = 0, w = 0, h = 0;   ///< its rectangle on screen, pixels, y growing down
    bool enabled = true;          ///< disabled nodes never take focus
    int group = 0;                ///< which focus trap it belongs to (0 = the page behind every trap)
    /// Tab order: positive values come first, lowest first; 0 follows in reading order (rows top
    /// to bottom, left to right); negative values are skipped by Tab but still reached by arrows.
    int tabIndex = 0;
    /// Explicit neighbours, in the order Up, Down, Left, Right: a node id to go to, -1 to stay
    /// put, 0 to use the geometry.
    int link[4] = {0, 0, 0, 0};

    float CenterX() const { return x + w * 0.5f; }
    float CenterY() const { return y + h * 0.5f; }
};

/// What happens at the edge of the layout.
enum class FocusWrap {
    None,           ///< focus stays
    Edge,           ///< comes out the opposite side, in the same row or column
    ReadingOrder,   ///< Up from the first goes to the last, Down from the last to the first (reading order)
};

struct FocusOptions {
    FocusWrap vertical = FocusWrap::Edge;
    FocusWrap horizontal = FocusWrap::Edge;
    /// How much a gap sideways counts against a candidate compared with distance ahead.
    float sidewaysWeight = 2.0f;
    /// Tab and Shift+Tab wrap from the last to the first (and back) when either direction wraps.
    bool tabWraps = true;
};

/// How good `to` is as the next focus when moving from `from` in direction (dx, dy), a unit axis
/// vector: the distance ahead between the rectangles' centres plus `sidewaysWeight` times the
/// sideways gap between the rectangles (0 when they overlap across the direction), so the widget
/// straight below a left-aligned column entry beats a closer one off to the side. Smaller is
/// better; negative when `to` isn't ahead.
inline float FocusScore(const FocusNode& from, const FocusNode& to, float dx, float dy, float sidewaysWeight = 2.0f) {
    const float along = (to.CenterX() - from.CenterX()) * dx + (to.CenterY() - from.CenterY()) * dy;
    if (along < 1.0f) return -1.0f;
    auto overlapGap = [](float a, float alen, float b, float blen) {
        const float g = b - (a + alen) > a - (b + blen) ? b - (a + alen) : a - (b + blen);
        return g > 0.0f ? g : 0.0f;
    };
    const float gap = dx != 0.0f ? overlapGap(from.y, from.h, to.y, to.h) : overlapGap(from.x, from.w, to.x, to.w);
    return along + sidewaysWeight * gap;
}

class FocusGraph {
public:
    // ---- Nodes ----
    void Clear();
    /// Add a node; returns its id (assigned when `node.id` is 0). An existing id is replaced.
    int Add(FocusNode node);
    /// Convenience: a node with a rectangle.
    int Add(float x, float y, float w, float h, int group = 0);
    bool Remove(int id);
    FocusNode* Find(int id);
    const FocusNode* Find(int id) const;
    const std::vector<FocusNode>& Nodes() const { return m_nodes; }
    std::size_t Size() const { return m_nodes.size(); }
    bool SetEnabled(int id, bool enabled);
    /// Make `target` the neighbour in `dir` (Up, Down, Left, Right), -1 to block it, 0 for the geometry.
    bool SetLink(int id, FocusDir dir, int target);

    // ---- Focus traps (modals) ----
    /// Only nodes of `group` take focus until the matching PopTrap. Focus moves to the group's
    /// first node unless it is already in it; the previous focus is remembered.
    void PushTrap(int group);
    /// Release the innermost trap and return focus to where it was before it opened (the first
    /// node when that is gone). Returns the new focus, 0 for none.
    int PopTrap();
    bool Trapped() const { return !m_traps.empty(); }
    /// The group focus is trapped in; -1 when it isn't.
    int ActiveTrap() const { return m_traps.empty() ? -1 : m_traps.back().group; }

    // ---- Queries (pure; they don't move focus) ----
    /// Ids a focus move may land on now: enabled, in the active trap.
    std::vector<int> Candidates() const;
    /// Ids in tab order (see FocusNode::tabIndex), within the active trap.
    std::vector<int> TabOrder() const;
    /// Ids in reading order (rows top to bottom, left to right), ignoring tabIndex.
    std::vector<int> ReadingOrder() const;
    int First() const;   ///< the first node in tab order, 0 when none
    int Last() const;
    /// The node a move from `from` in `dir` lands on, or 0 when nothing can take focus. When there
    /// is no node that way and wrapping is off, `from` itself. A `from` that can't take focus (0,
    /// disabled, outside the trap) starts from First (Last for Up and Previous).
    int Move(int from, FocusDir dir, const FocusOptions& options = {}) const;

    // ---- The focused node ----
    int Focus() const { return m_focus; }
    /// Focus a node. False (focus unchanged) when it doesn't exist, is disabled or is outside the trap.
    bool SetFocus(int id);
    void ClearFocus() { m_focus = 0; }
    /// Move focus one step like a key press and return the new focus (0 when nothing can take it).
    int Navigate(FocusDir dir, const FocusOptions& options = {});

private:
    struct Trap { int group = 0; int restore = 0; };
    bool Usable(const FocusNode& n) const { return n.enabled && (m_traps.empty() || n.group == m_traps.back().group); }
    int Directional(const FocusNode& from, FocusDir dir, const FocusOptions& o) const;

    std::vector<FocusNode> m_nodes;
    std::vector<Trap> m_traps;
    int m_focus = 0;
    int m_nextId = 1;
};

} // namespace okay
