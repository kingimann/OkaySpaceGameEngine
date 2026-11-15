#pragma once
// Which of the script editor's own key handling may run for the active code widget (editor/main.cpp). Pure logic: no Dear ImGui, no SDL,
// so it is tested headless (okayui/tests/test_okayui_script_adapter.cpp).
//
// editor/main.cpp wraps ImGuiColorTextEdit, which does none of the following itself, in a set of key helpers:
//   - an autocomplete popup that owns Tab, Enter, Up, Down and Esc while it is open (and accepts a suggestion on Tab or Enter);
//   - bracket and quote pairing: ( [ { " ' get their closer, a typed closer steps over the one already there, and Enter between { and } opens
//     a block;
//   - Tab with no suggestion and no selection inserts spaces up to the next four-column stop.
// They read ImGui's input queue and the ImGui editor's cursor, and they act around the widget's own key handling. OkayUI::CodeEdit has its
// own Enter (which opens a block between { and }), its own Tab, and, with SetAutoPair(true), its own pairing and step-over; it also takes
// typed text from OkayUI's input, not from ImGui's queue. With both running every key did two things: Enter after an identifier took a hidden
// completion instead of making a new line (the popup is drawn under the OkayUI editor, so nobody saw it), `{` then Enter opened the block
// twice, and `)` typed over a `)` doubled it. So with the OkayUI editor active the old helpers do not run at all.
namespace scripteditor {

struct LegacyKeyHandling {
    bool autocompleteOwnsKeys = false;   // the popup may open, and while it is open it takes Tab, Enter, Up, Down and Esc
    bool autoPair = false;               // main.cpp pairs brackets and quotes, steps over a typed closer and expands Enter between { }
    bool tabInsertsSpaces = false;       // main.cpp turns a plain Tab into spaces up to the next stop
};

/// What may run for the active widget. `okayEditorActive`: the OkayUI editor is the widget in use (ScriptEditorWidget::UsingOkayUI());
/// `autoPairSetting`: the user's "Auto-close Brackets" menu item. With the ImGui editor everything is as it always was.
inline LegacyKeyHandling PlanLegacyKeyHandling(bool okayEditorActive, bool autoPairSetting) {
    LegacyKeyHandling k;
    if (okayEditorActive) return k;      // OkayUI::CodeEdit does each of these itself, once
    k.autocompleteOwnsKeys = true;
    k.autoPair = autoPairSetting;
    k.tabInsertsSpaces = true;
    return k;
}

} // namespace scripteditor
