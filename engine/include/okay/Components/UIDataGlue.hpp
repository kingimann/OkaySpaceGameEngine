#pragma once
// UI library slice 2, the data-driven glue. None of these draw anything; they connect a UI object to data.
//
//   UIBind         two-way binding: a slider, stepper, toggle, dropdown, range slider or input field writes a variable
//                  when the player changes it, and shows the variable when something else changes it.
//   UIVisibleIf    conditional visibility: shows or hides an object from a variable or a small expression.
//   UIListBind     an array variable fills the sibling UIListView; the list's first child is the row template.
//   UIHotReload    watches a file (a UIDocument's markup file or an .okaytheme) and applies it when it changes.
//   UIFocusHint    tab order and explicit neighbours for NavigateUI (the FocusGraph rules, per widget).
//   UIRichLinks    makes the [url=...] links of the sibling rich text clickable.
//
// Written for this engine from the plain ideas (a binding, a visibility rule, a list template, a file watcher, a focus
// order); they reuse only our own helpers (uivars, UIListView, UIDocument, UITheme, FocusGraph). No other project's code.
#include "okay/Scene/Component.hpp"
#include <map>
#include <string>
#include <vector>

namespace okay {

class GameObject;
class Scene;

/// Keeps a widget and a variable the same. The widget is the sibling UISlider, UIStepper, UIToggle (on = 1), UIDropdown
/// (the picked index), UIRangeSlider (`var` is the low handle, `varHigh` the high one) or UIInputField (its text).
/// When the player changes the widget the variable is written; when a script or anything else changes the variable the widget
/// follows. The first frame the variable wins if it exists, otherwise the widget's value is written into it.
class UIBind final : public Behaviour {
public:
    enum class Mode { TwoWay = 0, ToVariable = 1, FromVariable = 2 };
    std::string var;        ///< the variable name
    std::string varHigh;    ///< a UIRangeSlider's high handle (empty = only the low handle is bound)
    Mode mode = Mode::TwoWay;

    /// One step of the sync (the engine calls it every frame; tests call it by hand).
    void Sync();
    /// True when a widget this binding can drive is on the object.
    bool HasWidget() const;

    void Start() override { Sync(); }
    void Update(float) override { Sync(); }

private:
    struct Channel { bool init = false; float widget = 0.0f; float variable = 0.0f; };
    Channel m_ch[2];
    bool m_textInit = false;
    std::string m_widgetText, m_varText;
    template <class Get, class Set> void SyncNumber(Channel& c, const std::string& name, Get get, Set set);
    void SyncText();
};

/// Shows or hides this object (or another one, by name) from a condition on variables, see okay/Components/UIVars.hpp.
/// "hp < 30" shows a low-health warning; "!hasKey" shows a "find the key" hint. Hidden means the object is switched off, so
/// leave it switched on in the scene: the condition does the rest while the game runs.
class UIVisibleIf final : public Behaviour {
public:
    std::string condition = "";
    std::string target;     ///< the object to show or hide (empty = this object)
    bool invert = false;    ///< show when the condition is false

    /// The condition's answer now (with `invert` applied): true means "show".
    bool ShouldShow() const;
    /// Evaluate and show or hide the target. True when it changed.
    bool Tick();
    /// A target that is hidden stops getting Update, so the layout pass of the scene calls this for the ones that hid.
    static void TickHidden(Scene& scene);

    void Start() override { Tick(); }
    void Update(float) override { Tick(); }

private:
    GameObject* Resolved() const;
    bool m_hid = false;   ///< this component switched its target off (so the layout pass keeps it up to date)
};

/// Fills the sibling UIListView from an array variable (a visual-script list or an Actions array). `format` makes the first
/// text of each row (a template text of just "{item}" counts as no template text; "{item}" is the entry, "{index}" its 1-based number; a list of maps also gives "{field}" for each
/// key, e.g. "{name}  x{count}"). A text deeper in the row template that contains "{...}" is filled the same way, so a
/// row can have a name, a count and a price in three text objects. The list scrolls and recycles rows as usual.
class UIListBind final : public Behaviour {
public:
    std::string var;                ///< the array variable
    std::string format = "{item}";  ///< the first text of a row
    std::string emptyText;          ///< shown as the only row when the array is empty (empty = no rows)

    /// Re-read the array and tell the list when it changed. True when the list was refreshed.
    bool Sync();
    int Count() const { return (int)m_rows.size(); }
    /// The fields of row `i` (item, index, and each map key).
    const std::map<std::string, std::string>& Row(int i) const;

    void Start() override;
    void Update(float) override { Sync(); }

private:
    std::vector<std::map<std::string, std::string>> m_rows;
    std::string m_signature;
    bool m_hooked = false;
    void Hook();
    void FillRow(GameObject* row, int index);
};

/// Watches a text file while the game runs and applies it the moment it changes: a UIDocument's markup file, or an
/// .okaytheme loaded into this object's UITheme. Save the file in any editor and the screen updates, no restart.
class UIHotReload final : public Behaviour {
public:
    std::string file;           ///< the file to watch (project-relative or absolute)
    std::string target;         ///< the object holding the UIDocument or UITheme (empty = this object)
    float interval = 0.5f;      ///< seconds between looks at the file
    bool loadAtStart = true;    ///< apply the file once when the game starts

    /// Look at the file now; reload when it changed since the last look. True when something was applied.
    bool Poll();
    /// Apply the file now whether it changed or not. True on success; the reason is in LastError() otherwise.
    bool ReloadNow();
    int Reloads() const { return m_reloads; }
    const std::string& LastError() const { return m_error; }

    void Start() override;
    void Update(float dt) override;

private:
    std::string Resolved() const;
    GameObject* Target() const;
    long long Stamp() const;
    long long m_stamp = -1;
    float m_wait = 0.0f;
    int m_reloads = 0;
    std::string m_error;
};

/// Tab order and explicit neighbours for the keyboard and gamepad menu (NavigateUI). Tab and Shift+Tab walk the tab order
/// (lowest tabIndex first; 0 follows in reading order; negative is skipped by Tab). A link names the object focus goes to
/// when that arrow is pressed ("-" keeps focus where it is); an empty link uses the geometry as before.
class UIFocusHint final : public Behaviour {
public:
    int tabIndex = 0;
    std::string up, down, left, right;
};

/// Makes the [url=target]words[/url] links in this object's rich text clickable (a TextRenderer with Rich Text on). A click
/// on a link runs on_link() on the object's script and stores the target in the Prefs value "ui_link" (so a visual script
/// can read it). The links are found from where the text was last drawn, so it needs the game to be drawing the text.
class UIRichLinks final : public Behaviour {
public:
    bool fireEvent = true;        ///< run on_link() on the object's script
    bool storeInPrefs = true;     ///< write the target to Prefs "ui_link"

    /// The target of the link under the point (window coordinates) from the last draw; "" when there is none.
    std::string LinkAt(float x, float y) const;
    const std::string& LastLink() const { return m_last; }
    /// Click handling for one press at (x, y) (Update calls it; tests call it by hand). True when a link was hit.
    bool Click(float x, float y);

    void Update(float) override;

private:
    std::string m_last;
};

} // namespace okay
