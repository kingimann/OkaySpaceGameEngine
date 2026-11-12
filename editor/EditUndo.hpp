#pragma once
// One undo step for an edit that is made over several frames (dragging a number,
// typing in a box, picking from a list). ImGui-free so it can be tested.
//
// Most Inspector and Hierarchy widgets change a value and set `dirty` without pushing
// an undo step. Instead of patching every widget, the editor watches the active
// widget: when one is pressed, Begin() keeps the scene text as it was; when it is
// let go, End() compares and records ONE undo step, only if something changed.
// Widgets that already push their own step (PushUndo before the change) are left
// alone: End() sees the undo stack moved and records nothing, so no edit gets two steps.
#include "EditorState.hpp"
#include <string>
#include <utility>

namespace oked {

class EditUndo {
public:
    bool IsOpen() const { return m_open; }
    /// Remember the scene as it is now. Does nothing if a session is already open.
    void Begin(okay::editor::EditorState& ed) {
        if (m_open) return;
        ed.CommitUndo();   // an earlier open step ends here, before this widget changes anything
        m_before = okay::SceneSerializer::Serialize(ed.scene());
        m_serial = ed.UndoSerial();
        m_open = true;
    }
    /// Finish the session. True when an undo step was recorded. `mergeKey` (the widget's id, 0 = none): the
    /// same widget edited again within a second, in the same place, joins the previous step.
    bool End(okay::editor::EditorState& ed, std::uint64_t mergeKey = 0) {
        if (!m_open) return false;
        m_open = false;
        std::string before = std::move(m_before);
        m_before.clear();
        if (ed.UndoSerial() != m_serial) return false;               // someone pushed their own step, or undo/redo ran
        if (okay::SceneSerializer::Serialize(ed.scene()) == before) return false;   // nothing changed
        ed.PushUndoText(std::move(before), "", mergeKey);
        ed.dirty = true;
        return true;
    }
    /// Forget the session without recording (the scene was replaced under it).
    void Cancel() { m_open = false; m_before.clear(); }

private:
    bool m_open = false;
    std::string m_before;
    std::uint64_t m_serial = 0;
};

} // namespace oked
