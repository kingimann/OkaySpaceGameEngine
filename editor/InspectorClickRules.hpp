#pragma once
// Which Inspector right-click menu a click opens, as plain data, so the rule can be tested without a window.
//
// The Inspector has two menus that open on a right click over its window: a field's own menu (Reset to Default, Copy Value ...) and the
// menu of the empty space (Add Component, Paste As New ...). Both ask ImGui to open a popup on the frame the button goes up, and when two
// popups are opened in one frame at the same level, the one asked for last replaces the other. The field menus are asked for while the
// cards are drawn, the empty-space menu at the end of the window, so on a label or in a gap (text has no widget id, so ImGui does not
// count it as "an item under the mouse") the empty-space menu used to win and the field menu never showed.
//
// The rule, in ClickRouter: a field whose row holds the mouse opens its menu and CLAIMS the click for that frame; the empty-space menu
// opens only when nobody claimed the click and the mouse is not over a widget. So exactly one menu opens, or none.
//
// The row is the whole width of the card, from the top of the field to the bottom of its last widget, so a click on the label, in the
// gap or on the widget all reach the same menu (FieldRowRect).
//
// No ImGui, no engine, no <mutex> / <thread> (they cannot be built for the Windows programs).
// Written for the Inspector menus review of 2026-11-15 (finding 3).

#include <type_traits>

namespace okay::inspectorctx {

/// A rectangle in screen coordinates, y growing downwards. Contains() is half open, the way ImGui tests whether the mouse is over a rectangle.
struct Rect {
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
};
inline bool Contains(const Rect& r, float x, float y) { return x >= r.x0 && x < r.x1 && y >= r.y0 && y < r.y1; }

/// The area of a field's row: the full width of the window's content (`contentLeft` .. `contentRight`), from the top of the row
/// (`rowTop`, where the cursor stood before the field was drawn, or the top of the last widget if that is higher) to the bottom of the
/// last widget, with `pad` above and below (half the gap between rows) so no sliver between two rows belongs to neither.
inline Rect FieldRowRect(float contentLeft, float contentRight, float rowTop, float lastTop, float lastBottom, float pad) {
    Rect r;
    r.x0 = contentLeft;
    r.x1 = contentRight;
    const float lastTopPadded = lastTop - pad;
    r.y0 = rowTop < lastTopPadded ? rowTop : lastTopPadded;
    r.y1 = lastBottom + pad;
    return r;
}

/// What the mouse did this frame.
struct RightClick {
    bool released = false;   ///< the right button went up this frame
    float x = 0, y = 0;      ///< where the mouse is (screen)
};

/// Hands the right click of one frame to at most one field, and tells the empty-space menu whether it may open.
/// One of these lives for the whole run; it only remembers the frame in which a field claimed a click.
class ClickRouter {
public:
    /// A field asks: should my menu open? `hot`: the mouse is over this field (its last widget); `hasMenu`: the field has something to
    /// show (a registered property, or a changed-from-prefab mark): a bool, or a function that answers when it is needed (it is only
    /// called for a click that could open this menu, so looking a property up costs nothing on the other frames). True for the first
    /// field that qualifies in a frame, and only for that one; it also claims the click.
    template <class HasMenu>
    bool FieldOpens(int frame, const RightClick& click, bool hot, HasMenu&& hasMenu) {
        if (!click.released || !hot || m_claimFrame == frame) return false;   // not this frame, not here, or another field has the click already
        if (!Truth(hasMenu)) return false;
        m_claimFrame = frame;
        return true;
    }

    /// The same for a field whose hot area is its whole row: the mouse must be inside `row` (FieldRowRect) and the Inspector window itself
    /// must be under it (`windowHovered`).
    template <class HasMenu>
    bool FieldOpensInRow(int frame, const RightClick& click, bool windowHovered, const Rect& row, HasMenu&& hasMenu) {
        return FieldOpens(frame, click, windowHovered && Contains(row, click.x, click.y), hasMenu);
    }

    /// Something else opened a menu on this click (the object header bar): the empty-space menu stays closed this frame.
    void Claim(int frame) { m_claimFrame = frame; }

    /// Has a menu taken this frame's click?
    bool Claimed(int frame) const { return m_claimFrame == frame; }

    /// The empty-space menu asks: may I open? `windowHovered`: the Inspector window itself is under the mouse (not a window on top of
    /// it, not a child window); `anyItemHovered`: a widget with an id is under the mouse (it has a menu of its own, or none).
    bool BackgroundOpens(int frame, const RightClick& click, bool windowHovered, bool anyItemHovered) const {
        return click.released && windowHovered && !anyItemHovered && !Claimed(frame);
    }

private:
    template <class F>
    static bool Truth(F&& f) {
        if constexpr (std::is_invocable_v<F>) return static_cast<bool>(f());
        else return static_cast<bool>(f);
    }
    int m_claimFrame = -1;
};

}   // namespace okay::inspectorctx
