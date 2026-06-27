#pragma once
// ---------------------------------------------------------------------------
// Bridge that lets game scripts draw immediate-mode UI without the engine knowing
// about any specific UI toolkit. The engine registers `ui_*` script builtins that
// forward to the active ScriptUIBridge (if a host installed one). A host that links
// a UI toolkit (e.g. OkayUI) implements this interface and calls SetScriptUI().
//
// The host is responsible for bracketing a frame's UI: latch input + start the UI
// frame BEFORE scripts run, and flush it AFTER. The script just issues widget calls.
//
// Values are returned (no pointers), which suits scripting: e.g.
//     hp = ui_slider("HP", hp, 0, 100)
//     if (ui_button("Reset")) { hp = 100 }
// ---------------------------------------------------------------------------
namespace okay {

struct ScriptUIBridge {
    virtual ~ScriptUIBridge() = default;
    // All have no-op/echo defaults so a host can implement only what it needs.
    virtual void  Begin(const char* title, float x, float y, float w, float h) { (void)title;(void)x;(void)y;(void)w;(void)h; }
    virtual void  End() {}
    virtual void  Text(const char* s) { (void)s; }
    virtual bool  Button(const char* label) { (void)label; return false; }
    virtual bool  Checkbox(const char* label, bool value) { (void)label; return value; }
    virtual float Slider(const char* label, float value, float lo, float hi) { (void)label;(void)lo;(void)hi; return value; }
    virtual void  ProgressBar(float t) { (void)t; }
    virtual void  SameLine() {}
    virtual void  Separator() {}
};

// The active bridge (null = no UI host installed; the ui_* builtins become no-ops).
inline ScriptUIBridge*& ScriptUIRef() { static ScriptUIBridge* p = nullptr; return p; }
inline void SetScriptUI(ScriptUIBridge* b) { ScriptUIRef() = b; }
inline ScriptUIBridge* GetScriptUI() { return ScriptUIRef(); }

} // namespace okay
