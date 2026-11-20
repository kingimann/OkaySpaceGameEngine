// Ctrl+S / Ctrl+Shift+S / Ctrl+Alt+S decisions (editor/SaveShortcutModel.hpp): every context, text input active, untitled scene.
#include "test_framework.hpp"
#include "SaveShortcutModel.hpp"
using namespace oked::savekeys;

static Keys CtrlS() { Keys k; k.ctrl = true; k.s = true; return k; }

int main() {
    RUN_SUITE("save_shortcut_model");
    { Keys k; k.s = true; CHECK(Decide(State{}, k).act == Act::None); }                  // plain S is typing
    { Keys k = CtrlS(); k.s = false; CHECK(Decide(State{}, k).act == Act::None); }
    { State s; s.sceneDirty = true; CHECK(Decide(s, CtrlS()).act == Act::SaveScene); }
    { State s; s.sceneDirty = true; s.textInputActive = true; CHECK(Decide(s, CtrlS()).act == Act::SaveScene); }   // the old bug: a text box blocked it
    { State s; s.sceneUntitled = true; CHECK(Decide(s, CtrlS()).act == Act::SaveScene); }
    { State s; s.focus = Context::Other; s.sceneDirty = true; s.otherDirty = true; CHECK(Decide(s, CtrlS()).act == Act::SaveAll); }
    { State s; s.focus = Context::Other; CHECK(Decide(s, CtrlS()).act == Act::NothingToSave); CHECK(Decide(s, CtrlS()).message == "Nothing to save"); }
    { State s; s.focus = Context::VisualScript; s.focusHasDoc = true; s.textInputActive = true; CHECK(Decide(s, CtrlS()).act == Act::SaveOwnWindow); }
    { State s; s.focus = Context::ScriptText; s.focusHasDoc = true; CHECK(Decide(s, CtrlS()).act == Act::SaveOwnWindow); }
    { State s; s.focus = Context::ScriptText; s.focusHasDoc = false; s.scriptsDirty = true; CHECK(Decide(s, CtrlS()).act == Act::SaveAll); }   // empty script window: saved nothing before
    { State s; s.focus = Context::VisualScript; s.focusHasDoc = false; CHECK(Decide(s, CtrlS()).act == Act::NothingToSave); }
    { State s; s.focus = Context::Prefab; CHECK(Decide(s, CtrlS()).act == Act::SavePrefab); }
    { State s; s.playing = true; CHECK(Decide(s, CtrlS()).act == Act::StopPlayFirst); }
    { Keys k = CtrlS(); k.shift = true; CHECK(Decide(State{}, k).act == Act::SaveAs); }
    { Keys k = CtrlS(); k.alt = true; State s; s.playing = true; CHECK(Decide(s, k).act == Act::SaveAll); }
    { State s; CHECK(Decide(s, CtrlS()).act == Act::SaveScene); }                          // clean scene is still rewritten
    { State s; s.focus = Context::Other; s.otherDirty = true; CHECK(Decide(s, CtrlS()).act == Act::SaveAll); }
    { State s; s.focus = Context::Prefab; s.prefabDirty = true; CHECK(Decide(s, CtrlS()).act == Act::SavePrefab); }
    { Keys k = CtrlS(); k.shift = true; State s; s.focus = Context::ScriptText; s.focusHasDoc = true; CHECK(Decide(s, k).act == Act::SaveAs); }   // scene Save As, documented
    { State s; s.playing = true; CHECK(Decide(s, CtrlS()).message.find("Stop Play") == 0); }
    CHECK(Saved(FileName("a/b\\Level1.okayscene")) == "Saved Level1.okayscene");
    CHECK(Failed("read-only folder") == "Could not save: read-only folder");
    TEST_MAIN_RESULT();
}
