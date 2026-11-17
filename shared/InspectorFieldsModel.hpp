#pragma once
// The Inspector, stage 2 (docs/own_ui_toolkit.md section 15): a component's body as a list of field rows. Part of
// shared/InspectorPanelModel.hpp (which includes it). No ImGui, no OkayUI, no engine types.
//
// The editor's component bodies are written with a small set of Dear ImGui widgets: drag and slider fields, check boxes, combos,
// colour fields, text fields, the editor's section headings, grey notes, small buttons, swatches, file fields with a picker and a
// thumbnail, and a fold-out. A body is described once as rows of those kinds (the host builds them every frame from the component,
// with pointers to the values it edits), and both versions draw the same rows: the Dear ImGui one (editor/EditorPanelViews.cpp,
// for the comparison test) and the OkayUI one (okayui/screens/OkayUIEditorPanels.cpp). A version edits the values in place and marks
// the row (changed, clicked); the host then does what the Dear ImGui code does for that field.
//
// Two ways a body is built: by hand in the editor for a component with its own Inspector (Light, Camera, Rigidbody 3D, the 3D
// colliders, the Script component, the rest of the Mesh Renderer; stage 3: the 2D physics, Sprite Renderer, Audio Source, Particle
// System, the character controllers, the animators, Nav Mesh Agent, docs/own_ui_toolkit.md section 16), and from the property registry for every other component
// (InspectorPanel.cpp: the generic card), so those come at once.
//
// Written from the editor's own component code (editor/main.cpp DrawInspector, editor/InspectorPanel.cpp PropertyField); no code from
// any other project.
#include "HierarchyPanelModel.hpp"   // PanelMenuEntry, HierCol

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace okaypanels {

enum InspRowKind : std::uint8_t {
    InspRowDrag = 0,      ///< n floats as drag cells (DragFloat, DragFloat2..4; axisBars: the Transform's coloured cells)
    InspRowSlider,        ///< one float on a slider
    InspRowDragInt,       ///< an int drag field
    InspRowSliderInt,     ///< an int slider
    InspRowCheck,         ///< a check box
    InspRowCombo,         ///< a list of `items` (the index in *i)
    InspRowColor,         ///< a colour field, n = 3 or 4 channels
    InspRowText,          ///< a one-line text field (*s), with a hint; with a pick, the editor's path field (a drop-down arrow at its
                          ///< right end and a right-click: "Choose from project..." and "Clear")
    InspRowSection,       ///< the editor's section heading (the accent tick, the larger face, a rule)
    InspRowGroup,         ///< the generic card's group title (Dear ImGui's SeparatorText)
    InspRowNote,          ///< grey text (TextDisabled); `wrap` wraps it
    InspRowColorText,     ///< text in `color` (a warning)
    InspRowLabel,         ///< plain text (a variable's name), `color` when set; `alignFrame` centres it on a frame; `wrap` wraps it
    InspRowSmallButton,   ///< a small button (one text line tall); `color` tints its text
    InspRowButton,        ///< a frame-tall button; `on` draws it in the accent (a toggle that is on)
    InspRowSwatch,        ///< a colour swatch button (fv), texW x texH (0: a frame-tall square)
    InspRowAsset,         ///< a reference field: the file or object name on a button (a click opens the picker), a clear button
    InspRowImage,         ///< a thumbnail (texture), its larger picture on hover, right-click "Show in Project"
    InspRowMask,          ///< a layer mask: a combo with Everything, Nothing and a check box per layer (*i holds the bits)
    InspRowTree,          ///< a fold-out (Dear ImGui's TreeNode): the rows up to its InspRowTreeEnd show while it is open
    InspRowTreeEnd,
    InspRowSpacing,       ///< Dear ImGui's Spacing()
    InspRowSeparator,     ///< Dear ImGui's Separator(): a one-pixel rule across the region (stage 3)
    InspRowBullet,        ///< Dear ImGui's BulletText: a bullet and the text (stage 3: the Animator's tracks)
};

/// What a click on a row opens (a small button, an asset field): a list with a search field, or a grid of thumbnails. The chosen
/// value goes into the row's *s (the field it fills), and the row is marked changed.
enum InspPick : std::uint8_t { InspPickNone = 0, InspPickList, InspPickGrid };

struct InspRow {
    InspRowKind kind = InspRowNote;
    std::uint8_t n = 1;                  ///< floats in a drag row, channels in a colour field
    std::uint8_t pick = InspPickNone;
    bool sameLine = false;               ///< on the previous row's line (Dear ImGui's SameLine)
    float sameLineGap = -1.0f;           ///< the gap before it (-1: the item spacing)
    float sameLineAt = -1.0f;            ///< >= 0: at this x from the region's left instead (SameLine(offset))
    bool sameLineWrap = false;           ///< on the previous row's line only when it fits there, else on a new line (SameLineOrWrap)
    bool disabled = false;
    bool mixed = false;                  ///< several objects selected and they differ: "-" or "Mixed"
    unsigned mixedAxes = 0;              ///< the parts of a vector that differ (bit 0 = x)
    bool axisBars = false;               ///< a 3-float drag row drawn as the Transform's (axis colours, shorter numbers when narrow)
    bool axisTags = false;               ///< the generic card's vector cells: "X 1.000", "Y -" (each part names its axis)
    bool logarithmic = false, clamp = false;
    bool on = false;                     ///< InspRowButton: drawn in the accent; InspRowTree: open at first
    bool wrap = false;                   ///< InspRowNote: wrapped at the region's right
    bool alignFrame = false;             ///< InspRowLabel: centred on a frame-tall row
    bool tipLookup = false;              ///< the field's tooltip comes from InspFieldList::hooks.tipFor(label), asked on hover only
    float width = 0.0f;                  ///< > 0: the frame this wide; < 0: the region left less -width; 0: the value column
    const char* label = "";              ///< with an optional "##id" part, not shown
    const char* fmt = nullptr;           ///< the number format (null: the kind's usual one)
    const char* tip = nullptr;           ///< a fixed tooltip
    const char* hint = nullptr;          ///< a text field's grey hint
    const char* pickKind = "";           ///< what the picker lists ("image", "model", "proc", "object", "layers" ...): passed to choices
    const char* dropKind = nullptr;      ///< not null: a file of this kind dragged from the Project panel can be dropped here ("" any);
                                         ///< "object" / "objectname": an object dragged from the Hierarchy (its guid / its name goes in)
    std::uint8_t dropState = 0;          ///< set by the host while a file is dragged: 1 this row takes it, 2 it does not (an outline)
    float speed = 1.0f, lo = 0.0f, hi = 0.0f;
    float* f = nullptr; int* i = nullptr; bool* b = nullptr; std::string* s = nullptr;   ///< the edited value (null: the row's own)
    float fv[4] = {0, 0, 0, 0};
    int iv = 0;
    bool bv = false;
    std::string sv;                      ///< the row's own text (a note's text; a field's value when s is null)
    const char* const* items = nullptr;  ///< InspRowCombo
    const char* preview = nullptr;       ///< InspRowCombo: the closed list's text instead of the chosen item's ("Choose a preset...")
    int count = 0;
    std::uint32_t color = 0;             ///< text colour (0: the usual one)
    void* texture = nullptr;             ///< InspRowImage: the host's texture (an SDL_Texture), drawn texW x texH
    float texW = 0.0f, texH = 0.0f;
    int tag = 0;                         ///< the host's own number for the row
    bool overridden = false;             ///< set by the host: differs from the prefab (a blue bar; right-click: Revert / Apply Property)

    // ---- what the version did this frame
    bool changed = false;                ///< the value changed (or a pick was made)
    bool clicked = false;                ///< a button was pressed; an image's "Show in Project" was chosen
    bool activated = false;              ///< a field became active (a drag or typing began)
    unsigned axes = 0;                   ///< which parts of a vector or colour changed (bit 0 = x / r)

    float* F() { return f ? f : fv; }
    int* I() { return i ? i : &iv; }
    bool* B() { return b ? b : &bv; }
    std::string* S() { return s ? s : &sv; }
    /// The text a note, a label or a heading shows: its own text when it has one (built at run time), else the label.
    const char* Text() const { return sv.empty() ? label : sv.c_str(); }
};

/// What a version asks the host for, only while it needs it.
struct InspFieldHooks {
    /// The registry's tooltip for a field drawn with this label ("" none): the editor's FieldTip. Asked while the field is hovered.
    std::function<std::string(const char* label)> tipFor;
    /// An overridden field's right-click menu: a heading ("Overridden from X"), the differences, Revert Property and Apply Property to
    /// Prefab. Asked only while the menu is open.
    std::function<void(const char* label, std::vector<PanelMenuEntry>& out)> prefabMenu;
    /// What a picker lists for a row (only while it is open): the values (written into the row's *s), the names shown, and for a grid
    /// the thumbnails (the host's textures, or null).
    std::function<void(const InspRow& row, std::vector<std::string>& values, std::vector<std::string>& names, std::vector<void*>& thumbs)> choices;
    /// A layer mask's right-click on a layer (the editor's LayerRenameMenu, stage 3): `layerName(i, text, hint)` gives the name to edit
    /// ("" for an unnamed layer) and the grey hint; `renameLayer(i, name, err)` renames it (the default name for "Reset to default")
    /// and false with `err` set when the name is refused. Null: no rename (the note is not shown either).
    std::function<void(int layer, std::string& text, std::string& hint, std::string& defaultName)> layerName;
    std::function<bool(int layer, const std::string& name, std::string& err)> renameLayer;
};

/// The prefab menu's ids.
inline const char* kInspPrefabRevert = "p.revert";
inline const char* kInspPrefabApply = "p.apply";

struct InspFieldList {
    std::vector<InspRow> rows;
    InspFieldHooks hooks;
    // ---- what the version did this frame, besides the rows' own marks
    std::string prefabField;             ///< the label of the field whose prefab menu row was chosen
    std::string prefabChosen;            ///< kInspPrefabRevert or kInspPrefabApply

    bool AnyChanged() const { for (const InspRow& r : rows) if (r.changed) return true; return false; }
    bool AnyActivated() const { for (const InspRow& r : rows) if (r.activated) return true; return false; }
    InspRow& operator[](int k) { return rows[(std::size_t)k]; }

    // ---- building, one call per Dear ImGui widget (each returns the row's index)
    int Add(InspRowKind kind, const char* label) {
        InspRow r;
        r.kind = kind; r.label = label ? label : "";
        if (pendingSame_) { r.sameLine = true; r.sameLineGap = pendingGap_; r.sameLineAt = pendingAt_; r.sameLineWrap = pendingWrap_; pendingSame_ = pendingWrap_ = false; }
        rows.push_back(std::move(r));
        return (int)rows.size() - 1;
    }
    /// The next row goes on this one's line: `gap` pixels after it (-1 the item spacing), or at `at` from the region's left.
    void SameLine(float gap = -1.0f, float at = -1.0f) { pendingSame_ = true; pendingGap_ = gap; pendingAt_ = at; }
    /// The next row goes on this one's line when it fits before the region's right edge, else it starts a new line (the editor's
    /// SameLineOrWrap: a row of buttons in a narrow Inspector wraps instead of running out of the window).
    void SameLineOrWrap(float gap = -1.0f) { SameLine(gap); pendingWrap_ = true; }
    InspRow& Last() { return rows.back(); }
    void Tip(const char* t) { if (!rows.empty()) rows.back().tip = t; }
    void TipLookup() { if (!rows.empty()) rows.back().tipLookup = true; }

    int Drag(const char* label, float* v, int n, float speed, float lo = 0.0f, float hi = 0.0f, const char* fmt = nullptr) {
        const int k = Add(InspRowDrag, label);
        InspRow& r = rows.back(); r.f = v; r.n = (std::uint8_t)n; r.speed = speed; r.lo = lo; r.hi = hi; r.fmt = fmt;
        return k;
    }
    int Slider(const char* label, float* v, float lo, float hi, const char* fmt = nullptr, bool logarithmic = false) {
        const int k = Add(InspRowSlider, label);
        InspRow& r = rows.back(); r.f = v; r.lo = lo; r.hi = hi; r.fmt = fmt; r.logarithmic = logarithmic;
        return k;
    }
    int DragInt(const char* label, int* v, float speed, int lo = 0, int hi = 0, const char* fmt = nullptr) {
        const int k = Add(InspRowDragInt, label);
        InspRow& r = rows.back(); r.i = v; r.speed = speed; r.lo = (float)lo; r.hi = (float)hi; r.fmt = fmt;
        return k;
    }
    int SliderInt(const char* label, int* v, int lo, int hi, const char* fmt = nullptr) {
        const int k = Add(InspRowSliderInt, label);
        InspRow& r = rows.back(); r.i = v; r.lo = (float)lo; r.hi = (float)hi; r.fmt = fmt;
        return k;
    }
    int Check(const char* label, bool* v) { const int k = Add(InspRowCheck, label); rows.back().b = v; return k; }
    int Combo(const char* label, int* v, const char* const* items, int count) {
        const int k = Add(InspRowCombo, label);
        InspRow& r = rows.back(); r.i = v; r.items = items; r.count = count;
        return k;
    }
    int Color(const char* label, float* v, int n) {
        const int k = Add(InspRowColor, label);
        InspRow& r = rows.back(); r.f = v; r.n = (std::uint8_t)n;
        return k;
    }
    int Text(const char* label, std::string* v, const char* hint = nullptr) {
        const int k = Add(InspRowText, label);
        InspRow& r = rows.back(); r.s = v; r.hint = hint;
        return k;
    }
    int Section(const char* label) { return Add(InspRowSection, label); }
    int Group(const char* label) { return Add(InspRowGroup, label); }
    /// Grey text; `text` is copied when `copy` (text built at run time).
    int Note(const char* text, bool copy = false, bool wrap = false) {
        const int k = Add(InspRowNote, copy ? "" : text);
        if (copy) rows.back().sv = text ? text : "";
        rows.back().wrap = wrap;
        return k;
    }
    int ColorText(std::uint32_t color, const char* text, bool copy = false) {
        const int k = Add(InspRowColorText, copy ? "" : text);
        if (copy) rows.back().sv = text ? text : "";
        rows.back().color = color;
        return k;
    }
    int Label(const char* text, std::uint32_t color = 0, bool alignFrame = false, bool copy = false) {
        const int k = Add(InspRowLabel, copy ? "" : text);
        if (copy) rows.back().sv = text ? text : "";
        rows.back().color = color; rows.back().alignFrame = alignFrame;
        return k;
    }
    int SmallButton(const char* label, std::uint32_t textColor = 0) { const int k = Add(InspRowSmallButton, label); rows.back().color = textColor; return k; }
    int Button(const char* label, bool on = false, float width = 0.0f) {
        const int k = Add(InspRowButton, label);
        rows.back().on = on; rows.back().width = width;
        return k;
    }
    int Swatch(const char* label, float r, float g, float b, float a = 1.0f, float w = 0.0f, float h = 0.0f) {
        const int k = Add(InspRowSwatch, label);
        InspRow& q = rows.back(); q.fv[0] = r; q.fv[1] = g; q.fv[2] = b; q.fv[3] = a; q.texW = w; q.texH = h;
        return k;
    }
    int Asset(const char* label, std::string* v, const char* pickKind, const char* dropKind) {
        const int k = Add(InspRowAsset, label);
        InspRow& r = rows.back(); r.s = v; r.pick = InspPickList; r.pickKind = pickKind; r.dropKind = dropKind;
        return k;
    }
    int Image(const char* id, void* texture, float w, float h) {
        const int k = Add(InspRowImage, id);
        InspRow& r = rows.back(); r.texture = texture; r.texW = w; r.texH = h;
        return k;
    }
    int Mask(const char* label, int* bits) { const int k = Add(InspRowMask, label); rows.back().i = bits; rows.back().pickKind = "layers"; return k; }
    int Tree(const char* label) { return Add(InspRowTree, label); }
    int TreeEnd() { return Add(InspRowTreeEnd, ""); }
    int Spacing() { return Add(InspRowSpacing, ""); }
    int Separator() { return Add(InspRowSeparator, ""); }
    int Bullet(const char* text, bool copy = false) {
        const int k = Add(InspRowBullet, copy ? "" : text);
        if (copy) rows.back().sv = text ? text : "";
        return k;
    }

private:
    bool pendingSame_ = false, pendingWrap_ = false;
    float pendingGap_ = -1.0f, pendingAt_ = -1.0f;
};

/// The label part a row shows ("Mass##rb3" -> "Mass"): the length before "##".
inline std::size_t InspVisibleLen(const char* label) {
    if (!label) return 0;
    const char* h = std::strstr(label, "##");
    return h ? (std::size_t)(h - label) : std::strlen(label);
}

/// A layer mask's text in its closed combo, as the Camera's Culling Mask shows it: "Everything", "Nothing", or the first two layer
/// names and "(+N)" for the rest.
template <class NameOf>
std::string InspMaskPreview(int mask, NameOf nameOf) {
    if (mask == ~0) return "Everything";
    if (mask == 0) return "Nothing";
    std::string out;
    int cnt = 0;
    for (int i = 0; i < 32; ++i)
        if (mask & (1 << i)) {
            if (cnt < 2) { if (cnt) out += ", "; out += nameOf(i); }
            ++cnt;
        }
    if (cnt > 2) out += " (+" + std::to_string(cnt - 2) + ")";
    return out;
}

/// The blue of a field changed from its prefab (the editor's PrefabFieldMark: a 3-pixel bar 5 pixels left of the region).
inline const std::uint32_t kInspPrefabMark = HierCol(90, 165, 255);
/// The colour of a script variable that differs from the script's default.
inline const std::uint32_t kInspOverriddenVar = HierCol(242, 217, 140);

} // namespace okaypanels
