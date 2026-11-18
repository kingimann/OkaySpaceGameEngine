#pragma once
// UI library slice 3, app parts. Each is a component you put on a UI object; the renderer draws it on the shared layers
// uilayers::K_AppParts and K_AppPopup (see UIElement.hpp and UIAppPartsDraw.cpp).
//
//   UIMenuBar     a bar of menus (File, Edit, ...); a click drops the entries down; keys walk them
//   UIToolbar     a row of buttons in groups: icons, toggles, one-of-a-group choices and separators
//   UITreeView    nested rows that open and close, one selected row, keyboard walking, scrolling
//   UIDataTable   columns and rows; a click on a header sorts; one selected row; only the rows on screen are drawn
//   UIWizard      steps with a progress strip and Back / Next buttons; a step can refuse Next until its form is valid
//   UIFormGroup   labelled input fields that check themselves with UIInputValidator and write their answers to variables
//   UIColorSquare the hue / saturation square the colour picker's advanced mode uses (UIColorPicker.hpp)
//
// Written for this engine from the plain ideas of each part (a list of menus, a row of buttons, an outline, a sortable grid,
// a sequence of steps); no other project's code, fonts or images were used. They reuse our own helpers only (UIVars,
// UIInputField, UIInputValidator, UIButton, the UI painter).
//
// Text formats (so a scene file stays one readable line per part):
//   menus     "File:New|Open|-|Quit;Edit:Undo|Redo"   menus split by ;  name before the colon  entries split by |  "-" is a line
//             an entry "~Save@Ctrl+S" is greyed out (~) and shows a shortcut (@ ...)
//   toolbar   "New@N|Open|-|Bold*|Italic*|-|Left=|Center=|Right="  | between items  "-" a separator  trailing * a toggle,
//             trailing = one-of-a-group (a run of = items between separators)  "@icon" an icon (an image path, or a letter)  ~ greyed
//   tree      "World|>Level 1|>>Boss|>Level 2|Settings"  one row per |  each leading > is one level deeper
//   table     columns "Name:160|Class|Level"  (a number after a colon is the width, the rest share what is left)
//             rows "Ava|Mage|42;Bo|Rogue|17"  rows split by ;  cells by |
//   wizard    steps "Account|Profile|Done"  pages "AccountPage|ProfilePage|DonePage" (objects shown on their step only)
//             validate "SignUpForm;;if:agreed" (per step: a form object, empty, or if:<condition> from the UIVisibleIf language)
//   form      "Name:text|Email:email|Age:int:13..120|Password:text:min8|Repeat:text:=Password|Nickname:text:?"
//             kinds text email int number letters alnum; options ? (optional) minN maxN a..b (a number range) =Label (must match)
#include "okay/Components/UIGameHud.hpp"
#include "okay/Components/UIInputField.hpp"
#include "okay/Components/UIInputValidator.hpp"
#include "okay/Components/UIButton.hpp"
#include "okay/Components/UIPanel.hpp"
#include "okay/Components/UIVars.hpp"
#include "okay/Render/Color.hpp"
#include "okay/Math/Mathf.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

namespace okay {

namespace uiapp {

/// Split on `sep`. With keepEmpty off, empty pieces are dropped.
inline std::vector<std::string> Split(const std::string& s, char sep, bool keepEmpty = true) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == sep) { if (keepEmpty || !cur.empty()) out.push_back(cur); cur.clear(); }
        else cur += c;
    }
    if (keepEmpty || !cur.empty()) out.push_back(cur);
    return out;
}

inline std::string Trim(const std::string& s) {
    std::size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t')) --b;
    return s.substr(a, b - a);
}

/// A whole text as a number ("12", "-3.5", "1e3"). No "nan", "inf" or hex.
inline bool ToNumber(const std::string& s, double& v) {
    if (s.empty()) return false;
    for (char c : s) if (!((c >= '0' && c <= '9') || c == '+' || c == '-' || c == '.' || c == 'e' || c == 'E')) return false;
    char* end = nullptr;
    v = std::strtod(s.c_str(), &end);
    return end && *end == '\0' && end != s.c_str();
}

inline std::string Lower(std::string s) {
    for (char& c : s) if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    return s;
}

/// "Last name" -> "last_name": a variable-safe key.
inline std::string Key(const std::string& label) {
    std::string out;
    for (char c : label) {
        if (c >= 'A' && c <= 'Z') out += (char)(c - 'A' + 'a');
        else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) out += c;
        else if (!out.empty() && out.back() != '_') out += '_';
    }
    while (!out.empty() && out.back() == '_') out.pop_back();
    return out;
}

/// Width of a string in the 8x8 bitmap font at `px` font pixels (the layout width; the drawing centres text inside it).
inline float TextWidth(const std::string& s, float px) {
    std::size_t n = 0;
    for (unsigned char c : s) if ((c & 0xC0) != 0x80) ++n;
    return (float)n * 8.0f * px;
}

} // namespace uiapp

/// What every app part has: a rectangle placed by an anchor, three colours (from UIHudBase), a text size, and a pointer
/// that reads the mouse in the part's own pixels. Keyboard keys are read only while the part is "active" (it was clicked).
class UIAppBase : public UIHudBase {
public:
    bool interactable = true;
    float fontScale = 1.75f;          ///< text size in font pixels (1 = 8 px tall letters)

    bool IsActive() const { return m_active; }
    void SetActive(bool a) { m_active = a; }

protected:
    struct Ptr {
        float x = 0.0f, y = 0.0f;     ///< the pointer in this part's pixels, from its top-left corner
        float w = 0.0f, h = 0.0f;     ///< the part's size on screen (the same space as x and y)
        bool inside = false;
        bool pressed = false;         ///< the button went down this frame (and no menu already took the click)
        bool held = false;
        float wheel = 0.0f;           ///< notches; positive is up
    };
    Ptr Pointer() const {
        Ptr p;
        if (!Input::GetMouseButton(0) && !Input::GetMouseButtonDown(0)) Claimed() = false;
        Vec2 o, sz;
        UIControlRect(anchor, position, size, o, sz);
        const Vec2 lm = UIToLocal(gameObject, o, sz, Input::MousePosition());
        p.x = lm.x - o.x; p.y = lm.y - o.y; p.w = sz.x; p.h = sz.y;
        p.inside = p.x >= 0.0f && p.y >= 0.0f && p.x <= sz.x && p.y <= sz.y;
        p.held = Input::GetMouseButton(0);
        p.pressed = Input::GetMouseButtonDown(0) && !Claimed();
        p.wheel = Input::MouseWheel();
        return p;
    }
    /// An open menu takes the click that lands on it, so a table or tree under the menu does not also see it.
    static bool& Claimed() { static thread_local bool c = false; return c; }
    static void Claim() { Claimed() = true; }
    float CharW() const { return 8.0f * fontScale; }
    float TextH() const { return 8.0f * fontScale; }
    bool m_active = false;
};

// ---- Menu bar -------------------------------------------------------------------------------------------------------

/// A bar of menus. A click on a name drops its entries down; moving over another name while one is open switches to it;
/// a click on an entry chooses it (on_select runs on the object's script; ChosenPath() is "File/Open"); a click anywhere
/// else, or Escape, closes the menu. Keys while it is open: Left and Right change menu, Up and Down walk the entries, Enter
/// chooses. `size.y` is the height of the bar; the dropdown hangs below it.
class UIMenuBar final : public UIAppBase {
public:
    std::string menus = "File:New|Open|Save@Ctrl+S|-|Quit;Edit:Undo|Redo|-|Cut|Copy|Paste;Help:About";
    float itemHeight = 28.0f;         ///< one entry in a dropdown
    float menuWidth = 170.0f;         ///< the least width of a dropdown (it grows for long entries)
    /// A variable that receives the chosen entry's number inside its menu; "<name>_text" receives "Menu/Entry".
    std::string chosenVar;

    struct Entry { std::string label, shortcut; bool separator = false, disabled = false; };
    struct Menu { std::string name; std::vector<Entry> entries; };

    UIMenuBar() { size = {640.0f, 32.0f}; anchor = UIAnchor::TopLeft; position = {0.0f, 0.0f}; }
    int ExecutionOrder() const override { return -100; }   // first: a menu that takes a click must do it before the parts under it look

    const std::vector<Menu>& Parsed() const {
        if (m_src != menus || m_parsed.empty()) { m_src = menus; Parse(); }
        return m_parsed;
    }
    int MenuCount() const { return (int)Parsed().size(); }
    int EntryCount(int menu) const { return menu >= 0 && menu < MenuCount() ? (int)Parsed()[(std::size_t)menu].entries.size() : 0; }

    float Pad() const { return 12.0f * (fontScale / 1.75f); }
    float MenuX(int i) const { float x = 4.0f; for (int k = 0; k < i && k < MenuCount(); ++k) x += MenuW(k); return x; }
    float MenuW(int i) const { return uiapp::TextWidth(Parsed()[(std::size_t)i].name, fontScale) + 2.0f * Pad(); }
    float SeparatorHeight() const { return 9.0f; }
    float DropWidth(int menu) const {
        float w = menuWidth;
        if (menu < 0 || menu >= MenuCount()) return w;
        for (const Entry& e : Parsed()[(std::size_t)menu].entries) {
            float ew = uiapp::TextWidth(e.label, fontScale) + 2.0f * Pad();
            if (!e.shortcut.empty()) ew += 24.0f + uiapp::TextWidth(e.shortcut, fontScale);
            w = std::max(w, ew);
        }
        return w;
    }
    /// Top of entry `e` of `menu`, in the bar's pixels (the dropdown starts just under the bar).
    float EntryY(int menu, int e) const {
        float y = size.y + 2.0f;
        const auto& es = Parsed()[(std::size_t)menu].entries;
        for (int k = 0; k < e && k < (int)es.size(); ++k) y += es[(std::size_t)k].separator ? SeparatorHeight() : itemHeight;
        return y;
    }
    float EntryH(int menu, int e) const { return Parsed()[(std::size_t)menu].entries[(std::size_t)e].separator ? SeparatorHeight() : itemHeight; }
    float DropHeight(int menu) const {
        float h = 0.0f;
        for (const Entry& e : Parsed()[(std::size_t)menu].entries) h += e.separator ? SeparatorHeight() : itemHeight;
        return h + 4.0f;
    }

    /// The menu name under (x, y) in the bar's pixels, or -1.
    int MenuAt(float x, float y) const {
        if (y < 0.0f || y >= size.y) return -1;
        for (int i = 0; i < MenuCount(); ++i) { const float x0 = MenuX(i); if (x >= x0 && x < x0 + MenuW(i)) return i; }
        return -1;
    }
    /// The entry (not a separator) of `menu` under (x, y), or -1.
    int EntryAt(int menu, float x, float y) const {
        if (menu < 0 || menu >= MenuCount()) return -1;
        const float x0 = MenuX(menu);
        if (x < x0 || x >= x0 + DropWidth(menu)) return -1;
        const auto& es = Parsed()[(std::size_t)menu].entries;
        for (int e = 0; e < (int)es.size(); ++e) {
            const float y0 = EntryY(menu, e);
            if (y >= y0 && y < y0 + EntryH(menu, e)) return es[(std::size_t)e].separator ? -1 : e;
        }
        return -1;
    }

    bool IsOpen() const { return m_open >= 0; }
    int OpenIndex() const { return m_open; }
    int Hovered() const { return m_hover; }
    void Open(int i) { if (i >= 0 && i < MenuCount()) { m_open = i; m_hover = -1; m_active = true; } }
    void Close() { m_open = -1; m_hover = -1; }
    int ChosenMenu() const { return m_chosenMenu; }
    int ChosenEntry() const { return m_chosenEntry; }
    /// "File/Open" for the entry chosen last ("" for none yet).
    std::string ChosenPath() const {
        if (m_chosenMenu < 0 || m_chosenMenu >= MenuCount()) return std::string();
        const Menu& m = Parsed()[(std::size_t)m_chosenMenu];
        if (m_chosenEntry < 0 || m_chosenEntry >= (int)m.entries.size()) return std::string();
        return m.name + "/" + m.entries[(std::size_t)m_chosenEntry].label;
    }
    /// Choose entry `e` of menu `m`. False for a separator, a greyed-out entry or one that does not exist.
    bool Choose(int m, int e) {
        if (m < 0 || m >= MenuCount()) return false;
        const auto& es = Parsed()[(std::size_t)m].entries;
        if (e < 0 || e >= (int)es.size() || es[(std::size_t)e].separator || es[(std::size_t)e].disabled) return false;
        m_chosenMenu = m; m_chosenEntry = e;
        Close();
        if (!chosenVar.empty()) {
            uivars::WriteNumber(gameObject, chosenVar, (float)e);
            uivars::WriteText(gameObject, chosenVar + "_text", ChosenPath());
        }
        FireUIEvent(gameObject, "on_select");
        return true;
    }
    /// Move the hover to the next enabled entry of the open menu (dir -1 or +1).
    void StepHover(int dir) {
        if (m_open < 0) return;
        const auto& es = Parsed()[(std::size_t)m_open].entries;
        const int n = (int)es.size();
        if (n == 0) return;
        int e = m_hover;
        for (int tries = 0; tries < n; ++tries) {
            e = e < 0 ? (dir > 0 ? 0 : n - 1) : ((e + dir) % n + n) % n;
            if (!es[(std::size_t)e].separator && !es[(std::size_t)e].disabled) { m_hover = e; return; }
        }
    }
    void StepMenu(int dir) {
        const int n = MenuCount();
        if (n == 0 || m_open < 0) return;
        m_open = ((m_open + dir) % n + n) % n; m_hover = -1;
    }

    void Update(float) override {
        if (!enabled || !interactable) return;
        const Ptr p = Pointer();
        if (m_open >= 0) {
            if (Input::GetKeyDown((char)27)) { Close(); return; }
            if (Input::GetKeyDown(Input::KeyRight)) StepMenu(1);
            else if (Input::GetKeyDown(Input::KeyLeft)) StepMenu(-1);
            else if (Input::GetKeyDown(Input::KeyDown)) StepHover(1);
            else if (Input::GetKeyDown(Input::KeyUp)) StepHover(-1);
            else if (Input::GetKeyDown('\r') && m_hover >= 0) { Choose(m_open, m_hover); return; }
        }
        if (p.pressed) {
            const int mi = MenuAt(p.x, p.y);
            if (mi >= 0) { Claim(); if (m_open == mi) Close(); else Open(mi); return; }
            if (m_open >= 0) {
                const int e = EntryAt(m_open, p.x, p.y);
                const float x0 = MenuX(m_open);
                const bool inDrop = p.x >= x0 && p.x < x0 + DropWidth(m_open) && p.y >= size.y && p.y < size.y + DropHeight(m_open) + 2.0f;
                if (inDrop) Claim();
                if (e >= 0) Choose(m_open, e); else if (!inDrop) Close();
                return;
            }
        } else if (m_open >= 0) {
            const int mi = MenuAt(p.x, p.y);
            if (mi >= 0 && mi != m_open) { Open(mi); }
            else { const int e = EntryAt(m_open, p.x, p.y); if (e >= 0) m_hover = e; }
        }
    }

private:
    void Parse() const {
        m_parsed.clear();
        for (const std::string& m : uiapp::Split(menus, ';', false)) {
            Menu menu;
            const std::size_t colon = m.find(':');
            menu.name = uiapp::Trim(colon == std::string::npos ? m : m.substr(0, colon));
            if (colon != std::string::npos) {
                for (std::string tok : uiapp::Split(m.substr(colon + 1), '|', false)) {
                    Entry e;
                    tok = uiapp::Trim(tok);
                    if (tok == "-") { e.separator = true; menu.entries.push_back(e); continue; }
                    if (!tok.empty() && tok[0] == '~') { e.disabled = true; tok.erase(0, 1); }
                    const std::size_t at = tok.find('@');
                    if (at != std::string::npos) { e.shortcut = tok.substr(at + 1); tok.erase(at); }
                    e.label = uiapp::Trim(tok);
                    if (!e.label.empty()) menu.entries.push_back(e);
                }
            }
            if (!menu.name.empty()) m_parsed.push_back(std::move(menu));
        }
    }
    mutable std::string m_src;
    mutable std::vector<Menu> m_parsed;
    int m_open = -1;
    int m_hover = -1;
    int m_chosenMenu = -1, m_chosenEntry = -1;
};

// ---- Toolbar --------------------------------------------------------------------------------------------------------

/// A row of buttons. A plain button fires on_select when pressed. A toggle (label ends in *) flips on and off. A radio
/// button (label ends in =) turns on and turns the others of its group off; a group is the run of = items between
/// separators. The state of every toggle is kept in `states` ('1' on, '0' off, one letter per item) so it is saved with
/// the scene. With `varPrefix` set, each toggle and radio writes 1 or 0 into the variable "<prefix><label>".
class UIToolbar final : public UIAppBase {
public:
    std::string items = "New@N|Open@O|Save@S|-|Bold@B*|Italic@I*|-|Left@L=|Center@C=|Right@R=";
    std::string states;               ///< '1' for each toggle or radio that is on
    bool showLabels = true;
    std::string chosenVar;            ///< receives the label of the button pressed last
    std::string varPrefix;            ///< toggles and radios write "<prefix><label as a key>" (1 or 0)

    struct Item { std::string label, icon; bool separator = false, toggle = false, radio = false, disabled = false; };

    UIToolbar() { size = {520.0f, 40.0f}; anchor = UIAnchor::TopLeft; position = {0.0f, 36.0f}; }

    const std::vector<Item>& Parsed() const {
        if (m_src != items || m_parsed.empty()) { m_src = items; Parse(); }
        return m_parsed;
    }
    int Count() const { return (int)Parsed().size(); }
    bool IsOn(int i) const { return i >= 0 && i < (int)states.size() && states[(std::size_t)i] == '1'; }
    void SetOn(int i, bool on) {
        if (i < 0 || i >= Count()) return;
        if ((int)states.size() < Count()) states.resize((std::size_t)Count(), '0');
        states[(std::size_t)i] = on ? '1' : '0';
        WriteVar(i);
    }
    int Chosen() const { return m_chosen; }
    int Hovered() const { return m_hover; }

    float ButtonSize() const { return std::max(16.0f, size.y - 8.0f); }
    float ItemWidth(int i) const {
        const Item& it = Parsed()[(std::size_t)i];
        if (it.separator) return 12.0f;
        const float b = ButtonSize();
        const float icon = it.icon.empty() ? 0.0f : b;
        const float label = (showLabels && !it.label.empty()) ? uiapp::TextWidth(it.label, fontScale) + 16.0f : 0.0f;
        return std::max(b, icon + label);
    }
    float ItemX(int i) const { float x = 6.0f; for (int k = 0; k < i && k < Count(); ++k) x += ItemWidth(k) + 3.0f; return x; }
    /// The button (not a separator) under (x, y) in the bar's pixels, or -1.
    int ItemAt(float x, float y) const {
        if (y < 4.0f || y > size.y - 4.0f) return -1;
        for (int i = 0; i < Count(); ++i) {
            const float x0 = ItemX(i);
            if (x >= x0 && x < x0 + ItemWidth(i)) return Parsed()[(std::size_t)i].separator ? -1 : i;
        }
        return -1;
    }
    /// Press button `i` as a click would. False for a separator, a greyed-out button or one that does not exist.
    bool Press(int i) {
        if (i < 0 || i >= Count()) return false;
        const Item& it = Parsed()[(std::size_t)i];
        if (it.separator || it.disabled) return false;
        if (it.toggle) SetOn(i, !IsOn(i));
        else if (it.radio) {
            int a = i, b = i;
            while (a > 0 && Parsed()[(std::size_t)a - 1].radio) --a;
            while (b + 1 < Count() && Parsed()[(std::size_t)b + 1].radio) ++b;
            for (int k = a; k <= b; ++k) SetOn(k, k == i);
        }
        m_chosen = i;
        if (!chosenVar.empty()) uivars::WriteText(gameObject, chosenVar, it.label);
        FireUIEvent(gameObject, "on_select");
        return true;
    }

    void Start() override { for (int i = 0; i < Count(); ++i) WriteVar(i); }
    void Update(float) override {
        if (!enabled || !interactable) return;
        const Ptr p = Pointer();
        m_hover = p.inside ? ItemAt(p.x, p.y) : -1;
        if (p.pressed && m_hover >= 0) { Claim(); Press(m_hover); }
    }

private:
    void WriteVar(int i) {
        if (varPrefix.empty() || i < 0 || i >= Count()) return;
        const Item& it = Parsed()[(std::size_t)i];
        if (it.toggle || it.radio) uivars::WriteNumber(gameObject, varPrefix + uiapp::Key(it.label), IsOn(i) ? 1.0f : 0.0f);
    }
    void Parse() const {
        m_parsed.clear();
        for (std::string tok : uiapp::Split(items, '|', false)) {
            Item it;
            tok = uiapp::Trim(tok);
            if (tok == "-") { it.separator = true; m_parsed.push_back(it); continue; }
            if (!tok.empty() && tok[0] == '~') { it.disabled = true; tok.erase(0, 1); }
            if (!tok.empty() && tok.back() == '*') { it.toggle = true; tok.pop_back(); }
            else if (!tok.empty() && tok.back() == '=') { it.radio = true; tok.pop_back(); }
            const std::size_t at = tok.find('@');
            if (at != std::string::npos) { it.icon = tok.substr(at + 1); tok.erase(at); }
            it.label = uiapp::Trim(tok);
            if (!it.label.empty() || !it.icon.empty()) m_parsed.push_back(it);
        }
    }
    mutable std::string m_src;
    mutable std::vector<Item> m_parsed;
    int m_hover = -1;
    int m_chosen = -1;
};

// ---- Tree view ------------------------------------------------------------------------------------------------------

/// Nested rows (a scene outline, a quest list, a file tree). A click on the little arrow opens or closes a row that has
/// children; a click on a row selects it (on_select). While it is active (clicked) the keys work: Up and Down move,
/// Right opens (or steps into the first child), Left closes (or steps to the parent), Home and End jump, Enter runs
/// on_activate. The mouse wheel scrolls. Which rows are open is kept in `expanded` so it is saved with the scene.
class UITreeView final : public UIAppBase {
public:
    std::string nodes = "World|>Level 1|>>Boss|>>Minions|>Level 2|Settings|>Audio|>Video";
    std::string expanded = "0";       ///< the numbers of the open rows, separated by commas
    int selected = -1;                ///< the number of the selected row (-1 none)
    float rowHeight = 26.0f;
    float indent = 18.0f;
    std::string selectedVar;          ///< receives the selected row's number; "<name>_text" its path ("World/Level 1")

    struct Node { std::string text; int depth = 0; int parent = -1; bool hasChildren = false; };

    UITreeView() { size = {300.0f, 240.0f}; anchor = UIAnchor::TopLeft; }

    int Count() const { Sync(); return (int)m_nodes.size(); }
    const std::string& Text(int i) const { Sync(); return m_nodes[(std::size_t)i].text; }
    int Depth(int i) const { Sync(); return m_nodes[(std::size_t)i].depth; }
    int Parent(int i) const { Sync(); return m_nodes[(std::size_t)i].parent; }
    bool HasChildren(int i) const { Sync(); return m_nodes[(std::size_t)i].hasChildren; }
    bool IsExpanded(int i) const { Sync(); return i >= 0 && i < (int)m_open.size() && m_open[(std::size_t)i]; }
    std::string Path(int i) const {
        Sync();
        std::string p;
        for (int k = i; k >= 0 && k < (int)m_nodes.size(); k = m_nodes[(std::size_t)k].parent) p = m_nodes[(std::size_t)k].text + (p.empty() ? "" : "/" + p);
        return p;
    }
    /// The rows you can see: those whose parents are all open. Numbers into the node list.
    const std::vector<int>& Visible() const { Sync(); return m_visible; }
    int VisibleCount() const { return (int)Visible().size(); }
    /// The place of node `i` among the visible rows (-1 when a closed parent hides it).
    int RowOf(int i) const { const auto& v = Visible(); for (int r = 0; r < (int)v.size(); ++r) if (v[(std::size_t)r] == i) return r; return -1; }

    void SetExpanded(int i, bool open) {
        Sync();
        if (i < 0 || i >= (int)m_nodes.size() || !m_nodes[(std::size_t)i].hasChildren) return;
        if (m_open[(std::size_t)i] == open) return;
        m_open[(std::size_t)i] = open;
        WriteExpanded();
        if (!open && selected >= 0 && IsHiddenUnder(selected, i)) Select(i);   // the row you were on folded away: stay with its parent
        FireUIEvent(gameObject, open ? "on_expand" : "on_collapse");
    }
    void Toggle(int i) { SetExpanded(i, !IsExpanded(i)); }
    void ExpandAll() { Sync(); for (int i = 0; i < (int)m_nodes.size(); ++i) if (m_nodes[(std::size_t)i].hasChildren) m_open[(std::size_t)i] = true; WriteExpanded(); }
    void CollapseAll() { Sync(); for (std::size_t i = 0; i < m_open.size(); ++i) m_open[i] = false; WriteExpanded(); if (selected >= 0) { int top = selected; while (m_nodes[(std::size_t)top].parent >= 0) top = m_nodes[(std::size_t)top].parent; Select(top); } }

    /// Select node `i` (opens the rows above it so it shows, scrolls it into view, runs on_select). False when `i` does not exist.
    bool Select(int i) {
        Sync();
        if (i < 0 || i >= (int)m_nodes.size()) return false;
        for (int p = m_nodes[(std::size_t)i].parent; p >= 0; p = m_nodes[(std::size_t)p].parent) m_open[(std::size_t)p] = true;
        WriteExpanded();
        const bool changed = selected != i;
        selected = i;
        ScrollIntoView();
        if (!selectedVar.empty()) {
            uivars::WriteNumber(gameObject, selectedVar, (float)i);
            uivars::WriteText(gameObject, selectedVar + "_text", Path(i));
        }
        if (changed) FireUIEvent(gameObject, "on_select");
        return true;
    }
    /// The visible row `dir` rows above (-1) or below (+1) the selected one.
    void MoveSelection(int dir) {
        const auto& v = Visible();
        if (v.empty()) return;
        int r = selected < 0 ? -1 : RowOf(selected);
        r = r < 0 ? (dir > 0 ? 0 : (int)v.size() - 1) : std::min(std::max(r + dir, 0), (int)v.size() - 1);
        Select(v[(std::size_t)r]);
    }

    float Scroll() const { return m_scroll; }
    void SetScroll(float s) { m_scroll = std::min(std::max(s, 0.0f), MaxScroll()); }
    float ContentHeight() const { return (float)VisibleCount() * rowHeight; }
    float MaxScroll() const { return std::max(0.0f, ContentHeight() - size.y); }
    /// The visible rows that touch the viewport: [first, last] (last < first when there is nothing to draw).
    void VisibleRange(int& first, int& last) const {
        const int n = VisibleCount();
        first = std::max(0, (int)std::floor(m_scroll / rowHeight));
        last = std::min(n - 1, (int)std::floor((m_scroll + size.y) / rowHeight));
    }
    void ScrollIntoView() {
        const int r = RowOf(selected);
        if (r < 0) return;
        const float top = (float)r * rowHeight, bottom = top + rowHeight;
        if (top < m_scroll) SetScroll(top);
        else if (bottom > m_scroll + size.y) SetScroll(bottom - size.y);
        else SetScroll(m_scroll);
    }
    /// The visible row at height `y` (in the part's pixels), or -1.
    int RowAtY(float y) const {
        if (y < 0.0f || y >= size.y) return -1;
        const int r = (int)std::floor((y + m_scroll) / rowHeight);
        return r >= 0 && r < VisibleCount() ? r : -1;
    }
    /// True when x lies on the open/close arrow of visible row r.
    bool OnArrow(int r, float x) const {
        const int n = Visible()[(std::size_t)r];
        const float x0 = (float)m_nodes[(std::size_t)n].depth * indent;
        return m_nodes[(std::size_t)n].hasChildren && x >= x0 && x < x0 + 22.0f;
    }

    void Update(float) override {
        if (!enabled || !interactable) return;
        const Ptr p = Pointer();
        if (p.pressed) {
            if (p.inside) {
                m_active = true;
                const int r = RowAtY(p.y);
                if (r >= 0) {
                    const int n = Visible()[(std::size_t)r];
                    if (OnArrow(r, p.x)) Toggle(n); else Select(n);
                }
            } else m_active = false;
        }
        if (p.inside && p.wheel != 0.0f) SetScroll(m_scroll - p.wheel * rowHeight * 3.0f);
        if (m_active) {
            if (Input::GetKeyDown(Input::KeyDown)) MoveSelection(1);
            else if (Input::GetKeyDown(Input::KeyUp)) MoveSelection(-1);
            else if (Input::GetKeyDown(Input::KeyHome)) { if (!Visible().empty()) Select(Visible().front()); }
            else if (Input::GetKeyDown(Input::KeyEnd)) { if (!Visible().empty()) Select(Visible().back()); }
            else if (Input::GetKeyDown(Input::KeyRight) && selected >= 0) {
                if (HasChildren(selected) && !IsExpanded(selected)) SetExpanded(selected, true);
                else if (HasChildren(selected)) MoveSelection(1);
            } else if (Input::GetKeyDown(Input::KeyLeft) && selected >= 0) {
                if (HasChildren(selected) && IsExpanded(selected)) SetExpanded(selected, false);
                else if (m_nodes[(std::size_t)selected].parent >= 0) Select(m_nodes[(std::size_t)selected].parent);
            } else if ((Input::GetKeyDown('\r') || Input::GetKeyDown(' ')) && selected >= 0) {
                if (HasChildren(selected)) Toggle(selected);
                FireUIEvent(gameObject, "on_activate");
            }
        }
    }

private:
    bool IsHiddenUnder(int node, int closed) const {
        for (int p = m_nodes[(std::size_t)node].parent; p >= 0; p = m_nodes[(std::size_t)p].parent) if (p == closed) return true;
        return false;
    }
    void WriteExpanded() {
        std::string s;
        for (int i = 0; i < (int)m_open.size(); ++i) if (m_open[(std::size_t)i]) { if (!s.empty()) s += ','; s += std::to_string(i); }
        expanded = s;
        m_expSrc = s;
        RebuildVisible();
    }
    void Sync() const {
        if (m_nodesSrc != nodes || (m_nodes.empty() && !nodes.empty())) {
            m_nodesSrc = nodes;
            m_nodes.clear();
            std::vector<int> stack;   // the last node at each depth
            for (const std::string& raw : uiapp::Split(nodes, '|', false)) {
                std::size_t d = 0;
                while (d < raw.size() && raw[d] == '>') ++d;
                Node n;
                n.text = uiapp::Trim(raw.substr(d));
                if (n.text.empty()) continue;
                n.depth = std::min((int)d, (int)stack.size());   // a row cannot be more than one level below the one above it
                n.parent = n.depth > 0 ? stack[(std::size_t)n.depth - 1] : -1;
                if (n.parent >= 0) m_nodes[(std::size_t)n.parent].hasChildren = true;
                stack.resize((std::size_t)n.depth);
                stack.push_back((int)m_nodes.size());
                m_nodes.push_back(std::move(n));
            }
            m_expSrc.clear(); m_open.assign(m_nodes.size(), false); m_expDirty = true;
        }
        if (m_expDirty || m_expSrc != expanded) {
            m_expSrc = expanded; m_expDirty = false;
            m_open.assign(m_nodes.size(), false);
            for (const std::string& t : uiapp::Split(expanded, ',', false)) {
                const int i = std::atoi(t.c_str());
                if (i >= 0 && i < (int)m_nodes.size() && m_nodes[(std::size_t)i].hasChildren) m_open[(std::size_t)i] = true;
            }
            const_cast<UITreeView*>(this)->RebuildVisible();
        }
        if (selected >= (int)m_nodes.size()) const_cast<UITreeView*>(this)->selected = -1;
    }
    void RebuildVisible() const {
        m_visible.clear();
        int hideBelow = -1;   // while >= 0, skip nodes deeper than this depth
        for (int i = 0; i < (int)m_nodes.size(); ++i) {
            const Node& n = m_nodes[(std::size_t)i];
            if (hideBelow >= 0) { if (n.depth > hideBelow) continue; hideBelow = -1; }
            m_visible.push_back(i);
            if (n.hasChildren && !m_open[(std::size_t)i]) hideBelow = n.depth;
        }
    }
    mutable std::string m_nodesSrc, m_expSrc;
    mutable std::vector<Node> m_nodes;
    mutable std::vector<char> m_open;
    mutable std::vector<int> m_visible;
    mutable bool m_expDirty = true;
    float m_scroll = 0.0f;
};

// ---- Data table -----------------------------------------------------------------------------------------------------

/// Columns and rows. Click a header to sort by it (ascending, then descending, then ascending again); a column of
/// numbers sorts as numbers, anything else as words without regard to capitals. Click a row to select it (on_select);
/// the selection follows the row when the order changes. The mouse wheel scrolls; while active, Up, Down, PageUp,
/// PageDown, Home and End move the selection. Only the rows on screen are drawn, so thousands of rows cost no more
/// than a screenful.
class UIDataTable final : public UIAppBase {
public:
    std::string columns = "Name:150|Class|Level";
    std::string rows = "Ava|Mage|42;Bo|Rogue|17;Cy|Knight|30;Di|Mage|8;Eli|Rogue|25";
    float rowHeight = 26.0f;
    float headerHeight = 30.0f;
    int sortColumn = -1;              ///< the column the rows are sorted by (-1 = the order they were written in)
    bool ascending = true;
    bool sortable = true;
    bool zebra = true;
    int selected = -1;                ///< the number of the selected row in `rows` (not its place after sorting)
    std::string selectedVar;          ///< receives the selected row's number; "<name>_text" its first cell

    struct Column { std::string title; float width = 0.0f; };   // width 0 = share the rest

    UIDataTable() { size = {520.0f, 220.0f}; anchor = UIAnchor::TopLeft; }

    int ColumnCount() const { Sync(); return (int)m_cols.size(); }
    const std::string& ColumnTitle(int c) const { Sync(); return m_cols[(std::size_t)c].title; }
    int RowCount() const { Sync(); return (int)m_rows.size(); }
    /// The text of the cell at `displayRow` (the place after sorting) and column `c`; "" past the end of a short row.
    const std::string& Cell(int displayRow, int c) const {
        Sync();
        static const std::string none;
        const auto& r = m_rows[(std::size_t)m_order[(std::size_t)displayRow]];
        return c >= 0 && c < (int)r.size() ? r[(std::size_t)c] : none;
    }
    /// The place in `rows` of the row shown at `displayRow`, and the other way round (-1 when there is none).
    int SourceRow(int displayRow) const { Sync(); return displayRow >= 0 && displayRow < (int)m_order.size() ? m_order[(std::size_t)displayRow] : -1; }
    int DisplayRow(int source) const { Sync(); for (int d = 0; d < (int)m_order.size(); ++d) if (m_order[(std::size_t)d] == source) return d; return -1; }
    /// Width of column c in the part's pixels.
    float ColumnWidth(int c) const {
        Sync();
        float fixed = 0.0f; int open = 0;
        for (const Column& k : m_cols) { if (k.width > 0.0f) fixed += k.width; else ++open; }
        const float room = std::max(0.0f, size.x - ScrollbarWidth() - fixed);
        const Column& k = m_cols[(std::size_t)c];
        return k.width > 0.0f ? k.width : (open > 0 ? room / (float)open : 0.0f);
    }
    float ColumnX(int c) const { float x = 0.0f; for (int k = 0; k < c; ++k) x += ColumnWidth(k); return x; }
    float ScrollbarWidth() const { return 10.0f; }
    int ColumnAt(float x) const { for (int c = 0; c < ColumnCount(); ++c) { const float x0 = ColumnX(c); if (x >= x0 && x < x0 + ColumnWidth(c)) return c; } return -1; }

    /// Sort by column `c` (-1 for the written order).
    void SortBy(int c, bool asc) { Sync(); sortColumn = c < ColumnCount() ? c : -1; ascending = asc; Resort(); }
    /// What a click on a header does: a new column sorts ascending, the same column flips the direction.
    void ClickHeader(int c) {
        if (!sortable || c < 0 || c >= ColumnCount()) return;
        if (sortColumn == c) SortBy(c, !ascending); else SortBy(c, true);
        FireUIEvent(gameObject, "on_sort");
    }
    bool Select(int source) {
        Sync();
        if (source < 0 || source >= (int)m_rows.size()) return false;
        const bool changed = selected != source;
        selected = source;
        ScrollIntoView();
        if (!selectedVar.empty()) {
            uivars::WriteNumber(gameObject, selectedVar, (float)source);
            uivars::WriteText(gameObject, selectedVar + "_text", m_rows[(std::size_t)source].empty() ? std::string() : m_rows[(std::size_t)source][0]);
        }
        if (changed) FireUIEvent(gameObject, "on_select");
        return true;
    }
    /// Move the selection `dir` rows down (negative up), in the order shown.
    void MoveSelection(int dir) {
        Sync();
        if (m_order.empty()) return;
        int d = selected < 0 ? (dir > 0 ? -1 : (int)m_order.size()) : DisplayRow(selected);
        d = std::min(std::max(d + dir, 0), (int)m_order.size() - 1);
        Select(m_order[(std::size_t)d]);
    }
    void SetRows(const std::string& text) { rows = text; selected = -1; }
    /// Add one row ("Ava|Mage|42") at the end.
    void AddRow(const std::string& cells) { if (!rows.empty()) rows += ';'; rows += cells; }

    float Scroll() const { return m_scroll; }
    float ViewportHeight() const { return std::max(0.0f, size.y - headerHeight); }
    int ViewportRows() const { return (int)std::floor(ViewportHeight() / rowHeight); }
    float MaxScroll() const { return std::max(0.0f, (float)RowCount() * rowHeight - ViewportHeight()); }
    void SetScroll(float s) { m_scroll = std::min(std::max(s, 0.0f), MaxScroll()); }
    /// The rows (in the order shown) that touch the viewport: [first, last]; last < first for an empty table.
    void VisibleRange(int& first, int& last) const {
        first = std::max(0, (int)std::floor(m_scroll / rowHeight));
        last = std::min(RowCount() - 1, (int)std::floor((m_scroll + ViewportHeight() - 0.01f) / rowHeight));
    }
    void ScrollIntoView() {
        const int d = DisplayRow(selected);
        if (d < 0) return;
        const float top = (float)d * rowHeight, bottom = top + rowHeight;
        if (top < m_scroll) SetScroll(top);
        else if (bottom > m_scroll + ViewportHeight()) SetScroll(bottom - ViewportHeight());
        else SetScroll(m_scroll);
    }
    /// The row (in the order shown) under height y in the part's pixels, or -1 (the header and the empty space give -1).
    int RowAtY(float y) const {
        if (y < headerHeight || y >= size.y) return -1;
        const int d = (int)std::floor((y - headerHeight + m_scroll) / rowHeight);
        return d >= 0 && d < RowCount() ? d : -1;
    }

    void Update(float) override {
        if (!enabled || !interactable) return;
        const Ptr p = Pointer();
        if (p.pressed) {
            if (p.inside) {
                m_active = true;
                if (p.y < headerHeight) ClickHeader(ColumnAt(p.x));
                else { const int d = RowAtY(p.y); if (d >= 0) Select(SourceRow(d)); }
            } else m_active = false;
        }
        if (p.inside && p.wheel != 0.0f) SetScroll(m_scroll - p.wheel * rowHeight * 3.0f);
        if (m_active) {
            const int page = std::max(1, ViewportRows() - 1);
            if (Input::GetKeyDown(Input::KeyDown)) MoveSelection(1);
            else if (Input::GetKeyDown(Input::KeyUp)) MoveSelection(-1);
            else if (Input::GetKeyDown(Input::KeyPageDown)) MoveSelection(page);
            else if (Input::GetKeyDown(Input::KeyPageUp)) MoveSelection(-page);
            else if (Input::GetKeyDown(Input::KeyHome)) { if (RowCount() > 0) Select(SourceRow(0)); }
            else if (Input::GetKeyDown(Input::KeyEnd)) { if (RowCount() > 0) Select(SourceRow(RowCount() - 1)); }
            else if (Input::GetKeyDown('\r') && selected >= 0) FireUIEvent(gameObject, "on_activate");
        }
    }

private:
    void Sync() const {
        if (m_colSrc != columns || m_cols.empty()) {
            m_colSrc = columns;
            m_cols.clear();
            for (const std::string& t : uiapp::Split(columns, '|', false)) {
                Column c;
                const std::size_t colon = t.find(':');
                c.title = uiapp::Trim(colon == std::string::npos ? t : t.substr(0, colon));
                double w = 0.0;
                if (colon != std::string::npos && uiapp::ToNumber(uiapp::Trim(t.substr(colon + 1)), w) && w > 0.0) c.width = (float)w;
                m_cols.push_back(std::move(c));
            }
            m_sortKey = -2;
        }
        if (m_rowSrc != rows) {
            m_rowSrc = rows;
            m_rows.clear();
            std::string cur;
            std::vector<std::string> row;
            auto endCell = [&] { row.push_back(cur); cur.clear(); };
            auto endRow = [&] { endCell(); m_rows.push_back(std::move(row)); row.clear(); };
            bool any = false;
            for (char ch : rows) {
                if (ch == ';') { endRow(); any = false; }
                else if (ch == '|') { endCell(); any = true; }
                else { cur += ch; any = true; }
            }
            if (any || !cur.empty() || !row.empty()) endRow();
            m_sortKey = -2;
        }
        if (m_sortKey != sortColumn * 2 + (ascending ? 1 : 0) || m_order.size() != m_rows.size()) Resort();
        if (selected >= (int)m_rows.size()) const_cast<UIDataTable*>(this)->selected = -1;
        const float maxS = std::max(0.0f, (float)m_rows.size() * rowHeight - ViewportHeight());
        if (m_scroll > maxS) m_scroll = maxS;
    }
    void Resort() const {
        m_order.resize(m_rows.size());
        for (std::size_t i = 0; i < m_order.size(); ++i) m_order[i] = (int)i;
        m_sortKey = sortColumn * 2 + (ascending ? 1 : 0);
        if (sortColumn < 0 || sortColumn >= (int)m_cols.size()) return;
        const std::size_t n = m_rows.size();
        std::vector<char> isNum(n);
        std::vector<double> num(n, 0.0);
        std::vector<std::string> text(n);
        for (std::size_t i = 0; i < n; ++i) {
            const std::string& c = (std::size_t)sortColumn < m_rows[i].size() ? m_rows[i][(std::size_t)sortColumn] : std::string();
            double v = 0.0;
            isNum[i] = uiapp::ToNumber(uiapp::Trim(c), v) ? 1 : 0;
            num[i] = v;
            text[i] = uiapp::Lower(c);
        }
        const bool asc = ascending;
        // Numbers come before words; numbers by value, words without regard to capitals; ties keep the written order.
        std::stable_sort(m_order.begin(), m_order.end(), [&](int a, int b) {
            const std::size_t x = (std::size_t)a, y = (std::size_t)b;
            if (isNum[x] != isNum[y]) return asc ? isNum[x] > isNum[y] : isNum[x] < isNum[y];
            if (isNum[x]) return asc ? num[x] < num[y] : num[x] > num[y];
            return asc ? text[x] < text[y] : text[x] > text[y];
        });
    }
    mutable std::string m_colSrc, m_rowSrc;
    mutable std::vector<Column> m_cols;
    mutable std::vector<std::vector<std::string>> m_rows;
    mutable std::vector<int> m_order;
    mutable int m_sortKey = -2;
    mutable float m_scroll = 0.0f;
};

// ---- Form group -----------------------------------------------------------------------------------------------------

/// Labelled input fields that check themselves. The group makes one child object per field (a UI Input Field and a UI
/// Input Validator on each), lays them out under their labels and keeps them in step with `fields`. With `valuesVar` set,
/// each answer is written into the variable "<valuesVar><label as a key>" as the player types. Submit() marks every field as
/// touched; when all are valid it runs on_submit, otherwise it runs on_invalid and the red messages show. `validVar` always
/// holds 1 (everything valid) or 0.
class UIFormGroup final : public UIAppBase {
public:
    std::string fields = "Name:text|Email:email|Age:int:13..120";
    float labelWidth = 130.0f;
    float rowHeight = 58.0f;          ///< one field with its message line
    float fieldHeight = 36.0f;
    std::string submitLabel = "Submit";   ///< a Submit button under the fields (empty = none)
    std::string valuesVar;            ///< prefix of the variables the answers are written to as they change ("" = write none)
    std::string validVar;             ///< a variable that holds 1 while every field is valid, else 0
    bool showSummary = true;          ///< "2 problems" under the fields
    Color fieldColor = Color::FromBytes(41, 46, 59);
    Color fieldFocusColor = Color::FromBytes(56, 62, 78);

    struct Spec {
        std::string label, key;
        UIInputValidator::Kind kind = UIInputValidator::Kind::Any;
        bool required = true;
        int minLength = 0, maxLength = 0;
        bool useRange = false; float minNumber = 0.0f, maxNumber = 100.0f;
        std::string matchLabel;
    };

    UIFormGroup() { size = {520.0f, 260.0f}; anchor = UIAnchor::TopLeft; }

    const std::vector<Spec>& Specs() const {
        if (m_src != fields || m_specs.empty()) { m_src = fields; ParseSpecs(); }
        return m_specs;
    }
    int Count() const { return (int)Specs().size(); }

    /// The child object holding field `i` (null until Update or Rebuild has made it).
    GameObject* FieldObject(int i) const {
        if (i < 0 || i >= Count() || !gameObject || !gameObject->transform) return nullptr;
        const std::string tail = "_" + Specs()[(std::size_t)i].key;
        for (Transform* c : gameObject->transform->Children()) {
            GameObject* g = c ? c->gameObject : nullptr;
            if (g && g->name.size() > tail.size() && g->name.compare(g->name.size() - tail.size(), tail.size(), tail) == 0 && g->GetComponent<UIInputField>()) return g;
        }
        return nullptr;
    }
    UIInputField* InputOf(int i) const { GameObject* g = FieldObject(i); return g ? g->GetComponent<UIInputField>() : nullptr; }
    UIInputValidator* Validator(int i) const { GameObject* g = FieldObject(i); return g ? g->GetComponent<UIInputValidator>() : nullptr; }
    int IndexOfLabel(const std::string& label) const {
        for (int i = 0; i < Count(); ++i) if (Specs()[(std::size_t)i].label == label || Specs()[(std::size_t)i].key == uiapp::Key(label)) return i;
        return -1;
    }
    std::string Value(int i) const { UIInputField* f = InputOf(i); return f ? f->text : std::string(); }
    void SetValue(int i, const std::string& v) { if (UIInputField* f = InputOf(i)) f->text = v; }
    /// The reason field `i` is not acceptable now ("" when it is), whether or not the player has touched it yet.
    std::string ErrorOf(int i) const {
        UIInputField* f = InputOf(i); UIInputValidator* v = Validator(i);
        if (!f || !v) return std::string();
        std::string other;
        if (!v->mustMatch.empty() && gameObject && gameObject->scene())
            if (GameObject* o = gameObject->scene()->Find(v->mustMatch)) if (auto* oi = o->GetComponent<UIInputField>()) other = oi->text;
        return v->Validate(f->text, other);
    }
    int ErrorCount() const { int n = 0; for (int i = 0; i < Count(); ++i) if (!ErrorOf(i).empty()) ++n; return n; }
    bool IsValid() const { return ErrorCount() == 0; }
    /// Show every field's message now (as if the player had visited them all).
    void Touch() { for (int i = 0; i < Count(); ++i) if (auto* v = Validator(i)) { v->MarkTouched(); v->Check(); } }
    /// Check everything. When valid, write the answers to the variables and run on_submit; else run on_invalid. True when valid.
    bool Submit() {
        Touch();
        const bool ok = IsValid();
        WriteVars();
        if (ok) {
            m_submitted = true;
            FireUIEvent(gameObject, "on_submit");
        } else FireUIEvent(gameObject, "on_invalid");
        return ok;
    }
    bool Submitted() const { return m_submitted; }
    /// Empty every field and hide the messages.
    void Reset() {
        for (int i = 0; i < Count(); ++i) { if (auto* f = InputOf(i)) f->text.clear(); }
        Rebuild();
        m_submitted = false;
    }
    /// Make the children and set every one up from `fields` again (a new Add Component, or after the fields text changed).
    void Rebuild() { m_built = false; Ensure(true); Place(); }

    void Start() override { Ensure(false); Place(); WriteVars(); }
    void Update(float) override {
        if (!enabled) return;
        if (m_specSeen != fields) { const bool first = m_specSeen.empty(); m_specSeen = fields; if (!first) { Ensure(true); } }
        Ensure(false);
        Place();
        WriteVars();
    }

private:
    void WriteVars() {
        if (!gameObject) return;
        if (!validVar.empty()) uivars::WriteNumber(gameObject, validVar, IsValid() ? 1.0f : 0.0f);
        if (!valuesVar.empty()) {   // the answers follow the boxes as the player types, so a text elsewhere can show them
            std::string all;
            for (int i = 0; i < Count(); ++i) { all += Value(i); all += '\x1f'; }
            if (all != m_lastValues) {
                m_lastValues = all;
                for (int i = 0; i < Count(); ++i) uivars::WriteText(gameObject, valuesVar + Specs()[(std::size_t)i].key, Value(i));
            }
        }
    }
    std::string m_lastValues;
    void ParseSpecs() const {
        m_specs.clear();
        for (const std::string& raw : uiapp::Split(fields, '|', false)) {
            const std::vector<std::string> parts = uiapp::Split(raw, ':');
            Spec s;
            s.label = uiapp::Trim(parts[0]);
            if (s.label.empty()) continue;
            s.key = uiapp::Key(s.label);
            if (s.key.empty()) s.key = "field" + std::to_string(m_specs.size());
            if (parts.size() > 1) {
                const std::string k = uiapp::Lower(uiapp::Trim(parts[1]));
                if (k == "email") s.kind = UIInputValidator::Kind::Email;
                else if (k == "int" || k == "integer") s.kind = UIInputValidator::Kind::Integer;
                else if (k == "number") s.kind = UIInputValidator::Kind::Number;
                else if (k == "letters") s.kind = UIInputValidator::Kind::Letters;
                else if (k == "alnum") s.kind = UIInputValidator::Kind::LettersAndDigits;
            }
            for (std::size_t i = 2; i < parts.size(); ++i) {
                const std::string o = uiapp::Trim(parts[i]);
                if (o == "?") s.required = false;
                else if (o.rfind("min", 0) == 0) s.minLength = std::atoi(o.c_str() + 3);
                else if (o.rfind("max", 0) == 0) s.maxLength = std::atoi(o.c_str() + 3);
                else if (!o.empty() && o[0] == '=') s.matchLabel = uiapp::Trim(o.substr(1));
                else {
                    const std::size_t dots = o.find("..");
                    double a = 0, b = 0;
                    if (dots != std::string::npos && uiapp::ToNumber(o.substr(0, dots), a) && uiapp::ToNumber(o.substr(dots + 2), b)) {
                        s.useRange = true; s.minNumber = (float)a; s.maxNumber = (float)b;
                    }
                }
            }
            m_specs.push_back(std::move(s));
        }
    }
    GameObject* Named(const std::string& name) const {
        if (!gameObject || !gameObject->transform) return nullptr;
        for (Transform* c : gameObject->transform->Children()) if (c && c->gameObject && c->gameObject->name == name) return c->gameObject;
        return nullptr;
    }
    /// Make a child for every field (and the Submit button), drop the ones no field names. `configure` also sets the
    /// validators and look from the spec (on creation, and when `fields` changes); otherwise a child you restyled stays.
    void Ensure(bool configure) {
        if (!gameObject || !gameObject->scene()) return;
        Scene* sc = gameObject->scene();
        const int n = Count();
        if (!m_built) configure = true;
        std::vector<GameObject*> keep;
        for (int i = 0; i < n; ++i) {
            GameObject* g = FieldObject(i);
            bool fresh = false;
            if (!g) {
                g = sc->CreateGameObject(gameObject->name + "_" + Specs()[(std::size_t)i].key);
                g->transform->SetParent(gameObject->transform, false);
                g->AddComponent<UIInputField>();
                g->AddComponent<UIInputValidator>();
                fresh = true;
            }
            if (!g->GetComponent<UIInputValidator>()) { g->AddComponent<UIInputValidator>(); fresh = true; }
            keep.push_back(g);
            if (configure || fresh) Configure(g, Specs()[(std::size_t)i]);
        }
        if (!m_specs.empty() || n == 0) {
            std::vector<GameObject*> drop;
            for (Transform* c : gameObject->transform->Children()) {
                GameObject* g = c ? c->gameObject : nullptr;
                if (!g || !g->GetComponent<UIInputField>() || g->GetComponent<UIInputValidator>() == nullptr) continue;
                if (std::find(keep.begin(), keep.end(), g) == keep.end() && g->name.rfind(gameObject->name + "_", 0) == 0) drop.push_back(g);
            }
            for (GameObject* g : drop) sc->Destroy(g);
        }
        GameObject* sub = Named("Submit");
        if (submitLabel.empty()) { if (sub) sc->Destroy(sub); }
        else {
            if (!sub) {
                sub = sc->CreateGameObject("Submit");
                sub->transform->SetParent(gameObject->transform, false);
                sub->AddComponent<UIButton>();
                configure = true;
            }
            if (configure) {
                auto* b = sub->GetComponent<UIButton>();
                b->label = submitLabel; b->clickTarget = gameObject->name; b->clickFunction = "Submit";
            }
        }
        m_built = true;
    }
    void Configure(GameObject* g, const Spec& s) {
        auto* in = g->GetComponent<UIInputField>();
        auto* v = g->GetComponent<UIInputValidator>();
        in->placeholder = s.label;
        in->color = fieldColor; in->focusColor = fieldFocusColor; in->borderWidth = 1.0f;
        v->kind = s.kind; v->required = s.required; v->minLength = s.minLength; v->maxLength = s.maxLength;
        v->useRange = s.useRange; v->minNumber = s.minNumber; v->maxNumber = s.maxNumber;
        v->mustMatch.clear();
        if (!s.matchLabel.empty()) {
            const int j = IndexOfLabel(s.matchLabel);
            if (j >= 0 && j != (int)(&s - Specs().data())) {
                if (GameObject* o = FieldObject(j)) v->mustMatch = o->name;
                else v->mustMatch = gameObject->name + "_" + Specs()[(std::size_t)j].key;   // made a moment later, same name
            }
        }
    }
    void Place() {
        for (int i = 0; i < Count(); ++i) {
            GameObject* g = FieldObject(i);
            auto* in = g ? g->GetComponent<UIInputField>() : nullptr;
            if (!in) continue;
            in->anchor = UIAnchor::TopLeft;
            in->position = {labelWidth, (float)i * rowHeight};
            in->size = {std::max(40.0f, size.x - labelWidth), fieldHeight};
        }
        if (GameObject* sub = Named("Submit")) {
            if (auto* b = sub->GetComponent<UIButton>()) {
                b->anchor = UIAnchor::TopLeft;
                b->position = {labelWidth, (float)Count() * rowHeight + 2.0f};
                b->size = {160.0f, 36.0f};
                b->label = submitLabel;
            }
        }
    }
    mutable std::string m_src;
    mutable std::vector<Spec> m_specs;
    std::string m_specSeen;
    bool m_built = false;
    bool m_submitted = false;
};

// ---- Wizard ---------------------------------------------------------------------------------------------------------

/// A sequence of steps. A strip at the top shows the steps and how far you are; Back and Next sit at the bottom (Next
/// reads Finish on the last step). Next refuses to go on while the step's `validate` entry says no: an entry is the name
/// of an object with a UI Form Group (it must be valid), or `if:<condition>` (a UI Visible If condition over variables).
/// `pages` names one object per step; only the current step's page is shown. Fires on_next, on_back, on_invalid (Next was
/// refused) and on_finish (Next on the last step).
class UIWizard final : public UIAppBase {
public:
    std::string steps = "Account|Profile|Done";
    std::string pages;                ///< one object name per step, separated by |
    std::string validate;             ///< one entry per step, separated by ; (see above)
    int step = 0;
    bool showButtons = true;
    bool freeNavigation = false;      ///< a click on any step number jumps to it (else only back to earlier ones)
    std::string progressVar;          ///< receives how far along you are, 0 to 100
    std::string stepVar;              ///< receives the step number (0 is the first)

    UIWizard() { size = {640.0f, 360.0f}; anchor = UIAnchor::TopLeft; }

    std::vector<std::string> StepNames() const { return uiapp::Split(steps, '|', false); }
    int Count() const { return (int)StepNames().size(); }
    bool Finished() const { return m_finished; }
    bool IsLast() const { return step >= Count() - 1; }
    /// 0 on the first step, 1 on the last.
    float Progress() const { return Count() > 1 ? (float)std::min(std::max(step, 0), Count() - 1) / (float)(Count() - 1) : (m_finished ? 1.0f : 0.0f); }

    float HeaderHeight() const { return 64.0f; }
    float FooterHeight() const { return showButtons ? 56.0f : 0.0f; }
    float ButtonW() const { return 110.0f; }
    float ButtonH() const { return 36.0f; }
    /// The Back and Next buttons, in the part's pixels (x, y, w, h).
    void BackRect(float& x, float& y, float& w, float& h) const { x = 16.0f; y = size.y - FooterHeight() + 10.0f; w = ButtonW(); h = ButtonH(); }
    void NextRect(float& x, float& y, float& w, float& h) const { x = size.x - 16.0f - ButtonW(); y = size.y - FooterHeight() + 10.0f; w = ButtonW(); h = ButtonH(); }
    /// The centre of step i's marker in the strip (x) and its y.
    float MarkerX(int i) const { const int n = Count(); const float a = 48.0f, b = size.x - 48.0f; return n <= 1 ? size.x * 0.5f : a + (b - a) * (float)i / (float)(n - 1); }
    float MarkerY() const { return 24.0f; }
    int MarkerAt(float x, float y) const {
        for (int i = 0; i < Count(); ++i) { const float dx = x - MarkerX(i), dy = y - MarkerY(); if (dx * dx + dy * dy <= 14.0f * 14.0f) return i; }
        return -1;
    }

    /// Why the current step may not be left ("" when it may).
    std::string Blocker() const { return BlockerOf(step); }
    std::string BlockerOf(int s) const {
        const std::vector<std::string> v = uiapp::Split(validate, ';');
        if (s < 0 || s >= (int)v.size()) return std::string();
        const std::string e = uiapp::Trim(v[(std::size_t)s]);
        if (e.empty()) return std::string();
        Scene* sc = gameObject ? gameObject->scene() : nullptr;
        if (e.rfind("if:", 0) == 0) return uivars::Evaluate(gameObject, e.substr(3)) ? std::string() : std::string("Not ready yet");
        GameObject* g = sc ? sc->Find(e) : nullptr;
        if (!g) return std::string();
        if (auto* f = g->GetComponent<UIFormGroup>()) return f->IsValid() ? std::string() : std::to_string(f->ErrorCount()) + " to fix";
        if (auto* v = g->GetComponent<UIInputValidator>()) { v->Check(); return v->IsValid() ? std::string() : v->Error(); }
        return std::string();
    }
    bool CanGoNext() const { return Blocker().empty(); }

    /// Go to the next step. When the step is not valid this shows the form's messages and returns false. On the last step it
    /// finishes instead (on_finish). True when it moved or finished.
    bool Next() {
        if (Count() == 0) return false;
        const std::string why = Blocker();
        if (!why.empty()) {
            ShowProblems(step);
            FireUIEvent(gameObject, "on_invalid");
            return false;
        }
        if (IsLast()) { if (!m_finished) { m_finished = true; Publish(); FireUIEvent(gameObject, "on_finish"); } return true; }
        ++step;
        Publish();
        FireUIEvent(gameObject, "on_next");
        return true;
    }
    bool Back() {
        if (step <= 0) return false;
        --step; m_finished = false;
        Publish();
        FireUIEvent(gameObject, "on_back");
        return true;
    }
    /// Jump to step i. Earlier steps are always allowed; later ones only with freeNavigation and when every step before is valid.
    bool GoTo(int i) {
        if (i < 0 || i >= Count() || i == step) return false;
        if (i > step) {
            if (!freeNavigation) return false;
            for (int s = step; s < i; ++s) if (!BlockerOf(s).empty()) return false;
        }
        step = i; m_finished = false;
        Publish();
        return true;
    }
    void Restart() { step = 0; m_finished = false; Publish(); }

    void Start() override { Publish(); }
    void Update(float) override {
        if (!enabled) return;
        if (step < 0) step = 0;
        if (step >= Count() && Count() > 0) step = Count() - 1;
        const Ptr p = Pointer();
        if (interactable && p.pressed && p.inside) {
            float x, y, w, h;
            if (showButtons) {
                BackRect(x, y, w, h);
                if (p.x >= x && p.x < x + w && p.y >= y && p.y < y + h) { Claim(); Back(); ApplyPages(); return; }
                NextRect(x, y, w, h);
                if (p.x >= x && p.x < x + w && p.y >= y && p.y < y + h) { Claim(); Next(); ApplyPages(); return; }
            }
            const int m = MarkerAt(p.x, p.y);
            if (m >= 0) { Claim(); GoTo(m); }
        }
        ApplyPages();
    }

private:
    void ShowProblems(int s) {
        const std::vector<std::string> v = uiapp::Split(validate, ';');
        if (s < 0 || s >= (int)v.size()) return;
        const std::string e = uiapp::Trim(v[(std::size_t)s]);
        Scene* sc = gameObject ? gameObject->scene() : nullptr;
        GameObject* g = (sc && e.rfind("if:", 0) != 0) ? sc->Find(e) : nullptr;
        if (!g) return;
        if (auto* f = g->GetComponent<UIFormGroup>()) f->Touch();
        else if (auto* iv = g->GetComponent<UIInputValidator>()) iv->MarkTouched();
    }
    void Publish() {
        if (!progressVar.empty()) uivars::WriteNumber(gameObject, progressVar, Progress() * 100.0f);
        if (!stepVar.empty()) uivars::WriteNumber(gameObject, stepVar, (float)step);
        ApplyPages();
    }
    void ApplyPages() {
        if (pages.empty() || !gameObject || !gameObject->scene()) return;
        const std::vector<std::string> names = uiapp::Split(pages, '|');
        for (int i = 0; i < (int)names.size(); ++i) {
            const std::string n = uiapp::Trim(names[(std::size_t)i]);
            if (n.empty()) continue;
            GameObject* g = gameObject->scene()->Find(n);
            if (g && g->active != (i == step)) g->active = (i == step);
        }
    }
    bool m_finished = false;
};

// ---- Colour square (the picker's hue / saturation field) ------------------------------------------------------------

/// A square where the left-to-right position is the hue (0 to 360 degrees) and top-to-bottom is the saturation (full at
/// the top, none at the bottom), shown at the full value set by `brightness`. It is made and driven by the UI Color
/// Picker's advanced mode, but it also works on its own: read `hue` and `sat`, or listen for on_change.
class UIColorSquare final : public UIAppBase {
public:
    float hue = 210.0f;
    float sat = 0.8f;
    float brightness = 1.0f;          ///< the value the square is drawn at (the picker keeps it equal to its value slider)
    bool dragging = false;

    UIColorSquare() { size = {140.0f, 140.0f}; anchor = UIAnchor::TopLeft; }

    /// Put the handle where the pointer (x, y in the square's pixels) is.
    void PickAt(float x, float y) {
        const float nh = Mathf::Clamp01(size.x > 0.0f ? x / size.x : 0.0f) * 360.0f;
        const float ns = 1.0f - Mathf::Clamp01(size.y > 0.0f ? y / size.y : 0.0f);
        if (nh == hue && ns == sat) return;
        hue = nh; sat = ns;
        FireUIEvent(gameObject, "on_change");
    }
    void Update(float) override {
        if (!enabled || !interactable) { dragging = false; return; }
        const Ptr p = Pointer();
        if (p.pressed && p.inside) { dragging = true; Claim(); }
        if (dragging && p.held) PickAt(p.x, p.y);
        if (!p.held) dragging = false;
    }
};

} // namespace okay
