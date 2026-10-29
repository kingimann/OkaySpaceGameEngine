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
    /// A sliding on/off switch. Defaults to a checkbox for hosts that don't draw one.
    virtual bool  Switch(const char* label, bool value) { return Checkbox(label, value); }
    virtual float Slider(const char* label, float value, float lo, float hi) { (void)label;(void)lo;(void)hi; return value; }
    virtual void  ProgressBar(float t) { (void)t; }
    virtual void  SameLine() {}
    virtual void  Separator() {}
    // Launcher-style building blocks (see OkayUI). Defaults degrade to plain text/buttons.
    virtual void  Heading(const char* s) { Text(s); }
    virtual void  Caption(const char* s, int r, int g, int b) { (void)r;(void)g;(void)b; Text(s); }
    virtual void  TextDim(const char* s) { Text(s); }
    virtual void  TextWrapped(const char* s) { Text(s); }
    virtual bool  PrimaryButton(const char* label) { return Button(label); }
    virtual void  Badge(const char* s, int r, int g, int b) { (void)r;(void)g;(void)b; Text(s); }
    virtual void  Avatar(const char* name, float size) { (void)name; (void)size; }
    virtual void  BeginCard(const char* id) { (void)id; }
    virtual void  EndCard() {}
    virtual bool  ListItem(const char* title, const char* subtitle, const char* right, bool selected) { (void)subtitle;(void)right;(void)selected; return Button(title); }
    virtual void  KeyValue(const char* k, const char* v) { (void)v; Text(k); }
    virtual void  Spinner() {}
    virtual void  Toast(const char* s, int kind, float seconds) { (void)s;(void)kind;(void)seconds; }
    /// Pill tabs; `labels` separated by '|'. Returns the selected index.
    virtual int   Tabs(const char* labels, int current) { (void)labels; return current; }
    /// A text field; returns the (possibly edited) text.
    virtual const char* Input(const char* label, const char* text) { (void)label; return text; }
    virtual void  Spacing(float h) { (void)h; }
    /// "launcher" (the OkaySpace look) or "classic".
    virtual void  Style(const char* name) { (void)name; }
    /// An image file at the cursor, `w` x `h` pixels. False when the host can't
    /// draw images (the caller then draws something else, e.g. an Avatar).
    virtual bool  Image(const char* path, float w, float h) { (void)path; (void)w; (void)h; return false; }
    /// A window pinned at (x, y) every frame (not draggable): the built-in overlays
    /// anchored to the screen. Hosts without pinning get an ordinary Begin().
    virtual void  BeginAt(const char* title, float x, float y, float w, float h) { Begin(title, x, y, w, h); }
};

// Where the game's canvas sits on the host window: (0, 0) in the player, the
// Game view's top-left in the editor. The built-in overlays (dialogue box, quest
// tracker, journal) add it to their canvas positions so they land on the game.
inline float& ScriptUIOriginX() { static float x = 0.0f; return x; }
inline float& ScriptUIOriginY() { static float y = 0.0f; return y; }
// The size of that canvas when the host draws the game inside a larger window
// (the editor's Game view); 0 means the UI canvas size (UICanvas) applies.
inline float& ScriptUICanvasW() { static float w = 0.0f; return w; }
inline float& ScriptUICanvasH() { static float h = 0.0f; return h; }
inline float ScriptUIWidth(float uiCanvasW) { return ScriptUICanvasW() > 0.0f ? ScriptUICanvasW() : uiCanvasW; }
inline float ScriptUIHeight(float uiCanvasH) { return ScriptUICanvasH() > 0.0f ? ScriptUICanvasH() : uiCanvasH; }

// The active bridge (null = no UI host installed; the ui_* builtins become no-ops).
inline ScriptUIBridge*& ScriptUIRef() { static ScriptUIBridge* p = nullptr; return p; }
inline void SetScriptUI(ScriptUIBridge* b) { ScriptUIRef() = b; }
inline ScriptUIBridge* GetScriptUI() { return ScriptUIRef(); }

} // namespace okay
