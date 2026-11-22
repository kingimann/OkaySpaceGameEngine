// What Ctrl+S, Ctrl+Shift+S and Ctrl+Alt+S do, as plain data (no ImGui, header-only, so a test replays key presses).
// Written from the owner's report "Ctrl+S doesn't save anything" and the rules in the File menu, nothing copied.
//   Ctrl+S        saves the document you are in; if that has no document, saves everything that changed; if nothing
//                 changed: a scene or prefab is still rewritten (File > Save always did), any other focus says "Nothing to save". It works while a text field is active (saving is not a text key).
//   Ctrl+Shift+S  Save As for the scene or prefab, whatever window has focus (script windows save themselves with Ctrl+S).     Ctrl+Alt+S  Save All.
#pragma once
#include <string>

namespace oked { namespace savekeys {

enum class Context { Scene, Prefab, VisualScript, ScriptText, Other };   // Other: Material, Timeline, settings, a panel with no document
enum class Act { None, SaveScene, SavePrefab, SaveOwnWindow, SaveAll, SaveAs, StopPlayFirst, NothingToSave };

struct State {
    Context focus = Context::Scene;
    bool focusHasDoc = false;      // VisualScript / ScriptText: a document is open in that window (it saves itself)
    bool focusDirty = false;       // that document has changes (the Scene context uses sceneDirty)
    bool sceneDirty = false, sceneUntitled = false, prefabDirty = false;
    bool scriptsDirty = false, graphsDirty = false, otherDirty = false;   // anything else unsaved (settings, materials...)
    bool playing = false;
    bool textInputActive = false;  // an input box is being edited: never blocks a save
};
struct Keys { bool ctrl = false, shift = false, alt = false, s = false; };

struct Result { Act act = Act::None; std::string message; };   // message: what the status line says when the host cannot say it

inline bool AnyDirty(const State& s) { return s.sceneDirty || s.prefabDirty || s.scriptsDirty || s.graphsDirty || s.otherDirty; }

inline Result Decide(const State& s, const Keys& k) {
    Result r;
    if (!k.ctrl || !k.s) return r;
    if (s.playing && (!k.alt)) { r.act = Act::StopPlayFirst; r.message = "Stop Play first: saving now would save the running game, not your scene."; return r; }
    if (k.alt) { r.act = Act::SaveAll; return r; }                                   // Ctrl+Alt+S (also while playing: the host stops it)
    if (k.shift) { r.act = Act::SaveAs; return r; }
    // Ctrl+S: the focused document first.
    if ((s.focus == Context::VisualScript || s.focus == Context::ScriptText) && s.focusHasDoc) { r.act = Act::SaveOwnWindow; return r; }
    if (s.focus == Context::Prefab) { r.act = Act::SavePrefab; return r; }
    if (s.focus == Context::Scene) {
        if (s.sceneUntitled) { r.act = Act::SaveScene; return r; }                   // first save: the host shows Save As
        if (s.sceneDirty) { r.act = Act::SaveScene; return r; }
    }
    // The focused thing has nothing of its own to save: save everything that changed, else say so.
    if (AnyDirty(s)) { r.act = Act::SaveAll; return r; }
    if (s.focus == Context::Scene) { r.act = Act::SaveScene; return r; }             // clean scene: still rewrite it (File > Save always did)
    r.act = Act::NothingToSave; r.message = "Nothing to save";
    return r;
}

inline std::string Saved(const std::string& name) { return "Saved " + name; }
inline std::string Failed(const std::string& reason) { return "Could not save: " + (reason.empty() ? std::string("unknown problem") : reason); }
inline std::string FileName(const std::string& path) {
    const size_t p = path.find_last_of("/\\");
    return p == std::string::npos ? path : path.substr(p + 1);
}
inline const char* Tooltip(const char* which) {   // File menu tips, one wording
    const std::string w = which;
    if (w == "save") return "Save what you are working on (a scene, prefab, script or visual script).\nWorks while a text box is being edited.";
    if (w == "saveas") return "Save the scene or prefab under a new name.";
    return "Save everything that changed: scene, scripts, visual scripts, project and build settings.";
}

}}  // namespace oked::savekeys
