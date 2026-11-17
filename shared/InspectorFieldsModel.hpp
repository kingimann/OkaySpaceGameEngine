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
#include <cmath>
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
    // stage 4 (docs/own_ui_toolkit.md section 17)
    InspRowGradient,      ///< a colour gradient (the Particle System's ParticleGradientEditor): a bar with a marker per stop under it.
                          ///< f: count x (time, r, g, b, a); i: the stop count; sel: the selected stop; hi: the most stops
    InspRowCurve,         ///< a curve of multipliers over 0..1 (ParticleCurveEditor): a plot with a dot per key.
                          ///< f: count x (time, value); i: the key count; sel: the selected key; b: smooth; hi: the most keys
    InspRowAnchor,        ///< the editor's AnchorCombo: a button with the anchor's name and its little picture, a grid of anchors in a
                          ///< popup. i: the anchor (0..11); a pick with Shift held sets `axes` to 1 ("also move it there")
    InspRowSlot,          ///< the editor's AssetSlot: the file's name (or "None (<hint>)") on a button 28 short of the region, an "x"
                          ///< small button that clears it; only a drop from the Project panel (dropKind) sets it
    InspRowRadio,         ///< Dear ImGui's RadioButton: on when *i == iv; a click sets *i = iv
    InspRowInputInt,      ///< Dear ImGui's InputInt: a number field with - and + buttons (stage 6: the Voxel Terrain's Seed)
    InspRowProgress,      ///< Dear ImGui's ProgressBar the region wide: fv[0] the fraction (0..1), the label its text (stage 7: Stats' HP)
    InspRowRange,         ///< Dear ImGui's DragFloatRange2: two drag cells (f[0] low, f[1] high, each kept on its side of the other);
                          ///< fmt and hint are the two formats
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
    bool noInputs = false;               ///< InspRowColor: the swatch and the label only (ImGuiColorEditFlags_NoInputs)
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
    const char* const* heads = nullptr;  ///< InspRowCombo (count entries): not null at q: the editor's section heading before item q
    // InspRowCombo with a search field at the top of the open list (the editor's ItemPicker, stage 7): the rows whose text or key
    // (`searchKeys`, count entries, optional) holds the typed text, any case; a typed text that is no key adds a row 'Use "<text>"',
    // which when picked puts the text in the row's sv, sets *I() to -1 and marks the row changed.
    bool search = false;
    // InspRowSmallButton that is a drag handle (the Inventory's "::" per slot, stage 7): it can be dragged onto another handle of the same
    // `dragType`; the handle it is dropped on gets the dragged row's `tag` in `dropped` (-1: none) and `hint` is the text that follows
    // the pointer.
    const char* dragType = nullptr;
    int dropped = -1;
    // stage 8: InspRowTree / InspRowTreeEnd with `collapsing`: Dear ImGui's CollapsingHeader (a frame-tall bar the region wide, the rows
    // inside not indented; `on`: open at first). InspRowSection with `small`: the First Person Hands window's heading (a thin tick, the
    // label in the body face, a rule). InspRowSmallButton with `menu`: a click opens these entries as a menu; the picked entry's id goes in
    // `sv` and the row is marked changed.
    bool collapsing = false, small = false;
    bool alignRight = false;             ///< with sameLine: a small button put at the region's right edge (the Modifiers' "...")
    const std::vector<PanelMenuEntry>* menu = nullptr;
    const char* searchHint = nullptr;
    const char* const* searchKeys = nullptr;
    // InspRowMask with presets (the Nav Mesh Agent's Area Mask): small buttons on one line at the top of the open list instead of
    // Everything and Nothing; a click sets the bits. `count` bits are listed (0: 32), named by `items` when set.
    const char* const* presetNames = nullptr;
    const int* presetBits = nullptr;
    int presetCount = 0;
    int* sel = nullptr;                  ///< InspRowGradient / InspRowCurve: the selected stop or key (the host keeps it)
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
    int Header(const char* label, bool openAtFirst = false) { const int k = Add(InspRowTree, label); rows.back().collapsing = true; rows.back().on = openAtFirst; return k; }
    int HeaderEnd() { const int k = Add(InspRowTreeEnd, ""); rows.back().collapsing = true; return k; }
    int SmallSection(const char* label) { const int k = Add(InspRowSection, label); rows.back().small = true; return k; }
    int MenuButton(const char* label, const std::vector<PanelMenuEntry>* entries) { const int k = Add(InspRowSmallButton, label); rows.back().menu = entries; return k; }
    int Spacing() { return Add(InspRowSpacing, ""); }
    int Separator() { return Add(InspRowSeparator, ""); }
    /// A colour gradient: `stops` holds kMax x 5 floats (time, r, g, b, a), `count` how many are used, `sel` the selected one.
    int Gradient(const char* id, float* stops, int* count, int maxStops, int* selected) {
        const int k = Add(InspRowGradient, id);
        InspRow& r = rows.back(); r.f = stops; r.i = count; r.hi = (float)maxStops; r.sel = selected;
        return k;
    }
    /// A curve: `keys` holds kMax x 2 floats (time, value), `count` how many are used, `smooth` its easing, `sel` the selected key.
    int Curve(const char* id, float* keys, int* count, int maxKeys, bool* smooth, int* selected) {
        const int k = Add(InspRowCurve, id);
        InspRow& r = rows.back(); r.f = keys; r.i = count; r.hi = (float)maxKeys; r.b = smooth; r.sel = selected;
        return k;
    }
    int Anchor(const char* label, int* anchor) { const int k = Add(InspRowAnchor, label); rows.back().i = anchor; return k; }
    /// The editor's AssetSlot: `kind` names the file in "None (Font)", `dropKind` what a drop takes ("font", "image").
    int Slot(const char* id, std::string* v, const char* kind, const char* dropKind) {
        const int k = Add(InspRowSlot, id);
        InspRow& r = rows.back(); r.s = v; r.hint = kind; r.dropKind = dropKind;
        return k;
    }
    int InputInt(const char* label, int* v) { const int k = Add(InspRowInputInt, label); rows.back().i = v; return k; }
    int Progress(const char* text, float fraction, bool copy = false) {
        const int k = Add(InspRowProgress, copy ? "" : text);
        if (copy) rows.back().sv = text ? text : "";
        rows.back().fv[0] = fraction;
        return k;
    }
    int Radio(const char* label, int* v, int value) { const int k = Add(InspRowRadio, label); rows.back().i = v; rows.back().iv = value; return k; }
    int Range(const char* label, float* lowHigh, float speed, float lo, float hi, const char* fmtLow, const char* fmtHigh = nullptr) {
        const int k = Add(InspRowRange, label);
        InspRow& r = rows.back(); r.f = lowHigh; r.speed = speed; r.lo = lo; r.hi = hi; r.fmt = fmtLow; r.hint = fmtHigh;
        return k;
    }
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

// ---- the editor's anchors (AnchorCombo) ------------------------------------------------------------------------------------------
inline const char* const kInspAnchorNames[12] = {
    "Top-Left", "Top", "Top-Right", "Left", "Center", "Right", "Bottom-Left", "Bottom", "Bottom-Right",
    "Stretch Horizontal", "Stretch Vertical", "Stretch (Fill)"};

/// The little picture of anchor `a` in the box (x0, y0)-(x1, y1), as the editor's AnchorCombo draws it: a rectangle, and inside it a
/// small filled square at the anchored point (0..8), or a filled band across it (9 horizontal, 10 vertical, 11 the whole box, 3 in).
/// `fill` gets the filled part. Both versions draw from this.
struct InspAnchorPicture { float fx0, fy0, fx1, fy1; };
inline InspAnchorPicture InspAnchorFill(int a, float x0, float y0, float x1, float y1) {
    const float w = x1 - x0, h = y1 - y0;
    if (a >= 0 && a <= 8) {
        const float cx = x0 + w * (0.18f + 0.32f * (float)(a % 3)), cy = y0 + h * (0.18f + 0.32f * (float)(a / 3));
        return {cx - w * 0.13f, cy - h * 0.13f, cx + w * 0.13f, cy + h * 0.13f};
    }
    if (a == 9) return {x0 + 3.0f, y0 + h * 0.36f, x1 - 3.0f, y1 - h * 0.36f};
    if (a == 10) return {x0 + w * 0.36f, y0 + 3.0f, x1 - w * 0.36f, y1 - 3.0f};
    return {x0 + 3.0f, y0 + 3.0f, x1 - 3.0f, y1 - 3.0f};
}

// ---- gradients and curves (the Particle System's over-life editors) ----------------------------------------------------------------
/// The colour of a gradient at t (0..1): the stops before and after t (in any order) blended, as okay::ParticleGradient::Evaluate.
/// `stops` is count x (time, r, g, b, a); with no stop, white.
inline void InspGradientAt(const float* stops, int count, float t, float out[4]) {
    if (count <= 0) { out[0] = out[1] = out[2] = out[3] = 1.0f; return; }
    int lo = -1, hi = -1;
    for (int i = 0; i < count; ++i) {
        const float st = stops[i * 5];
        if (st <= t && (lo < 0 || st > stops[lo * 5])) lo = i;
        if (st >= t && (hi < 0 || st < stops[hi * 5])) hi = i;
    }
    const float* a = stops + (lo < 0 ? hi : lo) * 5;
    const float* b = stops + (hi < 0 ? lo : hi) * 5;
    float u = 0.0f;
    if (lo >= 0 && hi >= 0) { const float span = b[0] - a[0]; u = span > 1e-6f ? (t - a[0]) / span : 0.0f; }
    for (int c = 0; c < 4; ++c) out[c] = a[c + 1] + (b[c + 1] - a[c + 1]) * u;
}
/// A curve's value at t: the keys before and after t blended, straight or eased (smoothstep), as okay::ParticleCurve::Evaluate.
/// `keys` is count x (time, value); with no key, 1.
inline float InspCurveAt(const float* keys, int count, bool smooth, float t) {
    if (count <= 0) return 1.0f;
    int lo = -1, hi = -1;
    for (int i = 0; i < count; ++i) {
        const float kt = keys[i * 2];
        if (kt <= t && (lo < 0 || kt > keys[lo * 2])) lo = i;
        if (kt >= t && (hi < 0 || kt < keys[hi * 2])) hi = i;
    }
    if (lo < 0) return keys[hi * 2 + 1];
    if (hi < 0) return keys[lo * 2 + 1];
    const float span = keys[hi * 2] - keys[lo * 2];
    if (span <= 1e-6f) return keys[lo * 2 + 1];
    float u = (t - keys[lo * 2]) / span;
    if (smooth) u = u * u * (3.0f - 2.0f * u);
    return keys[lo * 2 + 1] + (keys[hi * 2 + 1] - keys[lo * 2 + 1]) * u;
}
/// The top of a curve's plot: 1.15 x its largest key, at least 1 (ParticleCurveEditor).
inline float InspCurveTop(const float* keys, int count) {
    float m = 1.0f;
    for (int i = 0; i < count; ++i) if (keys[i * 2 + 1] > m) m = keys[i * 2 + 1];
    m *= 1.15f;
    return m < 1.0f ? 1.0f : m;
}
/// The stop or key nearest to x (gradient: within 7 pixels in x) or to (x, y) (curve: within 8 pixels), or -1.
inline int InspNearestStop(const float* stops, int count, float x0, float w, float mx) {
    int best = -1; float bd = 7.0f;
    for (int i = 0; i < count; ++i) {
        const float d = std::fabs(x0 + stops[i * 5] * w - mx);
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}
inline int InspNearestKey(const float* keys, int count, float x0, float y1, float w, float h, float top, float mx, float my) {
    int best = -1; float bd = 64.0f;
    for (int i = 0; i < count; ++i) {
        const float kx = x0 + keys[i * 2] * w, ky = y1 - keys[i * 2 + 1] / top * h;
        const float d = (kx - mx) * (kx - mx) + (ky - my) * (ky - my);
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}
/// Add a stop (or key) at the end, when there is room: its index, or -1.
inline int InspAddStop(float* stops, int& count, int maxStops, float t, const float rgba[4]) {
    if (count >= maxStops) return -1;
    float* s = stops + count * 5;
    s[0] = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
    for (int c = 0; c < 4; ++c) s[c + 1] = rgba[c];
    return count++;
}
inline int InspAddKey(float* keys, int& count, int maxKeys, float t, float v) {
    if (count >= maxKeys) return -1;
    keys[count * 2] = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
    keys[count * 2 + 1] = v;
    return count++;
}
/// Remove stop / key i, the ones after it moving down.
inline void InspRemoveAt(float* data, int& count, int stride, int i) {
    if (i < 0 || i >= count) return;
    for (int k = i; k + 1 < count; ++k) for (int c = 0; c < stride; ++c) data[k * stride + c] = data[(k + 1) * stride + c];
    --count;
}

/// A combo's closed text cut to fit `maxW` with "..." at its end (whole characters), as both versions draw it (stage 6: "Bool false" and
/// "CrouchWalk" ran under the arrow in Dear ImGui and were cut mid-letter in OkayUI).
template <class Measure>
std::string InspFitText(const char* text, float maxW, Measure measure) {
    std::string s(text ? text : "");
    if (s.empty() || measure(s.c_str()) <= maxW) return s;
    while (!s.empty()) {
        do { s.pop_back(); } while (!s.empty() && ((unsigned char)s.back() & 0xC0) == 0x80);   // a whole character
        if (!s.empty() && ((unsigned char)s.back() & 0x80)) s.pop_back();                    // the lead byte of a cut character
        const std::string t = s + "...";
        if (measure(t.c_str()) <= maxW) return t;
    }
    return "...";
}

/// The blue of a field changed from its prefab (the editor's PrefabFieldMark: a 3-pixel bar 5 pixels left of the region).
inline const std::uint32_t kInspPrefabMark = HierCol(90, 165, 255);
/// The colour of a script variable that differs from the script's default.
inline const std::uint32_t kInspOverriddenVar = HierCol(242, 217, 140);

} // namespace okaypanels
