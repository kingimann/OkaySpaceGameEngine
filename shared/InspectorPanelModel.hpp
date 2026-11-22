#pragma once
// The editor's Inspector as data, stage 1 (docs/own_ui_toolkit.md section 14): the object header card, the component header bar
// that every component shares, the Transform, the shading part of the Mesh Renderer and the Add Component button. Part of
// shared/EditorPanelModels.hpp (which includes it). No ImGui, no OkayUI, no engine types: both versions draw from it, the Dear ImGui
// body (editor/EditorPanelViews.cpp) and the OkayUI port (okayui/screens/OkayUIEditorPanels.cpp), and the editor fills it from the
// selected object (editor/main.cpp DrawInspector) and carries out the intent that comes back (undo, the other selected objects,
// saving the project's tags).
//
// Values the person edits are plain copies here: a version changes them in place and says so in its intent, and the host writes
// them back to the object. Lists only a popup needs (the tags, the 32 layer names, a component's menu) are asked for only while
// that popup is open.
//
// Written from the Inspector's own behaviour (editor/main.cpp DrawInspector, CompHeader, DragVec3Axis); no code from any other
// project.
#include "HierarchyPanelModel.hpp"   // HierCol, HierIcon, PanelMenuEntry
#include "InspectorFieldsModel.hpp"  // stage 2: a component body as field rows (InspFieldList)

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace okaypanels {

// ---- the object header card --------------------------------------------------------------------------------------------------

constexpr int kInspTagBufSize = 132;   ///< okay::Tags::kMaxLength (32) bytes of UTF-8 at four bytes a character, and the terminator

struct InspectorHeaderModel {
    bool active = true;                  ///< the object's own active flag (the box toggles it and its children)
    char* name = nullptr;                ///< the host's buffer for the name, edited in place (intent.nameChanged)
    std::size_t nameSize = 0;
    bool isStatic = false, locked = false;
    int icon = HierIconDot;              ///< the type icon, as in the Hierarchy (HierIcon)
    std::string tag;                     ///< "" shows "Untagged"
    std::string layerName;               ///< the current layer's name (the combo's text)
    int layer = 0;
    /// The tag list, asked for only while it is open: the built-in tags, then the project's own (which can be renamed or removed).
    std::function<void(std::vector<std::string>& builtin, std::vector<std::string>& custom)> tags;
    /// A layer's name (0 to 31), asked for only while the layer list is open.
    std::function<std::string(int)> layerNameOf;
    /// A layer's rename box (right-click its row): the text it starts with ("" while the layer has its default name) and its hint.
    std::function<void(int layer, std::string& text, std::string& hint)> layerRenameInfo;
};

/// What the header keeps between frames (the add-tag field, the rename of a tag or a layer from its list, and the answers the
/// host gives: an error to show, or "close the list").
struct InspectorHeaderState {
    char newTag[kInspTagBufSize] = {};
    std::string tagError;                ///< set by the host when an added tag was refused
    bool closeTagList = false;           ///< set by the host when a tag was added: the list closes
    int renameKind = 0;                  ///< 0 nothing, 1 a custom tag, 2 a layer (opened by right-clicking its row)
    std::string renameFrom;              ///< the tag being renamed
    int renameLayer = -1;                ///< the layer being renamed
    char renameBuf[kInspTagBufSize] = {};
    std::string renameError;             ///< set by the host when a rename was refused
    bool closeRename = false;            ///< set by the host when a rename was done
};

struct InspectorHeaderIntent {
    bool toggleActive = false;
    bool nameChanged = false;            ///< model.name holds the new text
    bool toggleStatic = false;
    bool toggleLock = false;
    bool setTag = false;  std::string tag;          ///< "" = Untagged
    bool addTag = false;  std::string newTag;       ///< from the field at the bottom of the tag list (Enter or Add)
    int setLayer = -1;
    bool savePrefab = false;
    bool renameTag = false; std::string tagFrom, tagTo;
    bool removeTag = false; std::string removeTagName;
    bool renameLayer = false; int layerIndex = -1; std::string layerTo;   ///< Enter in a layer's rename box
    bool resetLayer = false;                                              ///< its "Reset to default" (layerIndex)
    bool Any() const {
        return toggleActive || nameChanged || toggleStatic || toggleLock || setTag || addTag || setLayer >= 0 || savePrefab || renameTag ||
               removeTag || renameLayer || resetLayer;
    }
};

/// The header's tooltips.
inline const char* kInspActiveTip = "Active in scene (also toggles children + the whole selection)";
inline const char* kInspStaticTip = "Marks the object as non-moving (saved with the scene)";
inline const char* kInspLockTip = "Lock this object in the Scene view: it (and its children) can't be click-selected there";
inline const char* kInspLayerListNote = "Right-click a layer to rename it";

// ---- the component header bar --------------------------------------------------------------------------------------------------

/// The small drawn mark after a component's enable box (editor/EditorWidgets.cpp CategoryGlyph): 0 dot, 1 square (rendering),
/// 2 circle (physics), 3 triangle (animation), 4 diamond (scripts), 5 speaker (audio), 6 ring (UI).
enum InspGlyph : int { InspGlyphDot = 0, InspGlyphSquare, InspGlyphCircle, InspGlyphTriangle, InspGlyphDiamond, InspGlyphSpeaker, InspGlyphRing };

struct CompHeaderModel {
    std::string label;                   ///< the component's name (cut with an ellipsis to fit the bar)
    bool enabled = true;
    bool open = true;                    ///< folded out
    std::uint32_t color = HierCol(107, 168, 250);   ///< the family colour (CategoryColor), the bar at its left edge and the mark
    int glyph = InspGlyphDot;
    bool prefabChanged = false;          ///< differs from its prefab: the left bar is blue and the bar says so on hover
    int diag = 0;                        ///< problems on it: 0 none, 1 warnings, 2 errors (a marker left of the "..." button)
    std::string diagTip;
    std::string tip;                     ///< hover text of the bar ("" none)
    std::string managed;                 ///< managed by the Character Model: the note under an open bar ("" none)
    std::string mixedNote;               ///< several objects selected: which values differ ("" none)
    float sectionGap = 0.0f;             ///< air above the bar (Appearance > Density)
    /// The "..." menu (also on a right-click of the bar), asked for only while it is open.
    std::function<void(std::vector<PanelMenuEntry>&)> buildMenu;
};

struct CompHeaderIntent {
    bool toggleEnabled = false;
    bool toggleOpen = false;
    std::string menuId;                  ///< the menu item chosen ("" none)
    bool Any() const { return toggleEnabled || toggleOpen || !menuId.empty(); }
};

/// The menu ids the editor's component menu uses (built by the host; the version only hands back the id).
inline const char* kCompMenuUp = "c.up";
inline const char* kCompMenuDown = "c.down";
inline const char* kCompMenuExpand = "c.expand";
inline const char* kCompMenuCollapse = "c.collapse";
inline const char* kCompMenuCopy = "c.copy";
inline const char* kCompMenuPaste = "c.paste";
inline const char* kCompMenuReset = "c.reset";
inline const char* kCompMenuPrefabRevert = "c.prefabrevert";
inline const char* kCompMenuPrefabApply = "c.prefabapply";
inline const char* kCompMenuRemove = "c.remove";

/// The part of the menu every component has (Move Up and Down, Expand and Collapse All); the host adds the values and prefab rows and
/// Remove (CompMenuTail).
inline void CompMenuHead(std::vector<PanelMenuEntry>& out) {
    auto item = [&](const char* id, const char* label) { PanelMenuEntry e; e.id = id; e.label = label; out.push_back(e); };
    item(kCompMenuUp, "Move Up");
    item(kCompMenuDown, "Move Down");
    PanelMenuEntry sep; sep.kind = PanelMenuSeparator; out.push_back(sep);
    item(kCompMenuExpand, "Expand All Components");
    item(kCompMenuCollapse, "Collapse All Components");
}
/// The end of the menu: Remove Component, or the note that it is required.
inline void CompMenuTail(std::vector<PanelMenuEntry>& out, bool removable) {
    PanelMenuEntry sep; sep.kind = PanelMenuSeparator; out.push_back(sep);
    PanelMenuEntry e;
    if (removable) { e.id = kCompMenuRemove; e.label = "Remove Component"; }
    else { e.kind = PanelMenuHeading; e.label = "(required component)"; }
    out.push_back(e);
}

/// The family mark after the enable box, in an `fh` square whose top-left is (x, y) (written from the editor's CompHeader: a tinted
/// rounded square with the family's shape in its colour, fainter while the component is off). The Pen is HierarchyPanelModel.hpp's.
template <class Pen>
void InspDrawGlyph(Pen& p, int glyph, std::uint32_t color, bool enabled, float x, float y, float fh) {
    const unsigned r8 = color & 0xffu, g8 = (color >> 8) & 0xffu, b8 = (color >> 16) & 0xffu;
    const float s = fh * 0.66f, off = (fh - s) * 0.5f;
    const float ax = x, ay = y + off, bx = x + s, by = y + off + s;
    p.rectFilled(ax, ay, bx, by, HierCol(r8, g8, b8, 46), 3.0f);                    // the colour at 0.18
    const std::uint32_t ic = HierCol(r8, g8, b8, enabled ? 255u : 140u);            // at 1.0, or 0.55 while off
    const float cx = (ax + bx) * 0.5f, cy = (ay + by) * 0.5f, r = s * 0.27f;
    switch (glyph) {
    case InspGlyphSquare: p.rect(cx - r, cy - r, cx + r, cy + r, ic, 1.5f, 2.0f); break;
    case InspGlyphCircle: p.circle(cx, cy, r, ic, 16, 2.0f); break;
    case InspGlyphTriangle: p.triangleFilled(cx - r, cy + r, cx + r, cy + r, cx, cy - r, ic); break;
    case InspGlyphDiamond:
        p.triangleFilled(cx, cy - r * 1.2f, cx + r * 1.2f, cy, cx, cy + r * 1.2f, ic);
        p.triangleFilled(cx, cy - r * 1.2f, cx, cy + r * 1.2f, cx - r * 1.2f, cy, ic);
        break;
    case InspGlyphSpeaker:
        p.rectFilled(cx - r, cy - r * 0.5f, cx - r * 0.2f, cy + r * 0.5f, ic, 1.0f);
        p.triangleFilled(cx - r * 0.3f, cy, cx + r, cy - r, cx + r, cy + r, ic);
        break;
    case InspGlyphRing: p.circle(cx, cy, r, ic, 16, 2.0f); p.circleFilled(cx, cy, r * 0.35f, ic, 10); break;
    default: p.circleFilled(cx, cy, r * 0.75f, ic, 12); break;
    }
}

/// The component's name cut to `room` pixels with an ellipsis (whole UTF-8 characters), as the bar shows it. `measure(text)` is the
/// version's text width. Nothing is cut when the room is under 20 pixels (the bar is too narrow to matter).
template <class Measure>
std::string CompHeaderTitle(const std::string& label, float room, Measure measure) {
    std::string nm = label;
    if (room > 20.0f && measure(nm.c_str()) > room) {
        while (nm.size() > 1 && measure((nm + "\xE2\x80\xA6").c_str()) > room) {
            // one whole character off the end: its continuation bytes, then its first byte (the editor's bar once left a first byte alone)
            while (!nm.empty() && (static_cast<unsigned char>(nm.back()) & 0xC0) == 0x80) nm.pop_back();
            if (!nm.empty()) nm.pop_back();
        }
        nm += "\xE2\x80\xA6";
    }
    return nm;
}

// ---- Transform ---------------------------------------------------------------------------------------------------------------

struct TransformModel {
    float pos[3] = {0, 0, 0};
    float rot[3] = {0, 0, 0};            ///< degrees (the editor keeps the euler angles it shows per object)
    float scale[3] = {1, 1, 1};
    bool open = true;
    bool isCamera = false;               ///< a camera's scale is shown but locked; no Reset Scale, no Uniform
    bool uniform = false;                ///< the axes are linked: dragging one scales all three by the same ratio
    int selectedCount = 1;               ///< more than one: "(editing N selected objects)", and edits go to all of them
    bool pasteValid = false;             ///< "Paste Transform Values" is offered
    const InspFieldHooks* hooks = nullptr;   ///< stage 2: the fields' tooltips and prefab marks (null: none)
    bool posOverridden = false, rotOverridden = false, scaleOverridden = false;   ///< changed from the prefab (a blue bar, right-click menu)
};

struct TransformIntent {
    bool posChanged = false, rotChanged = false, scaleChanged = false;   ///< the model's values hold the new ones
    bool activated = false;              ///< a drag or a typed edit began on one of the nine fields (the host records one undo step)
    bool resetPos = false, resetRot = false, resetScale = false;
    bool toggleUniform = false, toggleOpen = false;
    bool copy = false, paste = false;
    std::string prefabField, prefabChosen;   ///< a prefab menu row chosen on a field (kInspPrefabRevert / kInspPrefabApply)
    bool Any() const {
        return posChanged || rotChanged || scaleChanged || activated || resetPos || resetRot || resetScale || toggleUniform || toggleOpen ||
               copy || paste;
    }
};

/// The axis colours of a vector field (X red, Y green, Z blue), a 3-pixel bar down each cell's left edge.
inline const std::uint32_t kInspAxisCol[3] = {HierCol(206, 84, 84), HierCol(120, 194, 110), HierCol(88, 150, 235)};

/// Uniform scale: when one axis was dragged from `before` to `after`, the other two follow by the same ratio. Writes `after`.
inline void TransformUniformScale(const float before[3], float after[3]) {
    for (int ax = 0; ax < 3; ++ax) {
        if (after[ax] == before[ax]) continue;
        const float ratio = before[ax] != 0.0f ? after[ax] / before[ax] : 1.0f;
        for (int o = 0; o < 3; ++o) if (o != ax) after[o] = before[o] * ratio;
        break;
    }
}

/// The format a vector cell shows: in a narrow Inspector fewer decimals rather than a cut number (the value keeps full precision).
/// Starts at `fmt` when it is one of %.3f %.2f %.1f %.0f and steps down while the text is wider than `room`; never to a whole
/// number unless the value is one (0.2 must not read "0"). `measure(text)` is the version's text width.
template <class Measure>
const char* InspAxisFormat(const char* fmt, float v, float room, Measure measure) {
    static const char* kShorter[] = {"%.3f", "%.2f", "%.1f", "%.0f"};
    for (int k = 0; k < 4; ++k) {
        if (std::strcmp(fmt, kShorter[k]) != 0) continue;
        const int lastK = std::fabs(v - std::round(v)) < 0.05f ? 3 : 2;
        for (; k < lastK; ++k) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), kShorter[k], v);
            if (measure(buf) <= room) break;
        }
        return kShorter[k];
    }
    return fmt;
}

/// Dear ImGui's split of one field width into `n` cells `spacing` apart (PushMultiItemsWidths): every cell but the last is the
/// same whole number of pixels; the last takes what is left.
inline void InspCellWidths(float full, int n, float spacing, float* out) {
    if (n < 1) return;
    const float one = std::max(1.0f, std::floor((full - spacing * (float)(n - 1)) / (float)n));
    for (int i = 0; i < n - 1; ++i) out[i] = one;
    out[n - 1] = std::max(1.0f, std::floor(full - (one + spacing) * (float)(n - 1)));
}

// ---- Mesh Renderer (the shading part) ------------------------------------------------------------------------------------------
// The Mesh Renderer's look, from its stats line down to the Primitive list. The rest of the component (the mesh file, the texture
// slots with their pickers and thumbnails, the maps, the mesh tools and the material preset) stays the editor's Dear ImGui code
// under it in this stage: those are asset fields (drop targets, the project picker, thumbnails) that the next stage ports.

inline const char* const kMeshShaderNames[10] = {"Standard", "Unlit", "Toon", "Gradient", "Fresnel", "Iridescent", "Hologram", "Posterize", "Velvet", "Custom"};
constexpr int kMeshShaderCount = 10;
enum MeshShader : int { MeshShaderStandard = 0, MeshShaderUnlit, MeshShaderToon, MeshShaderGradient, MeshShaderFresnel };
inline const char* const kMeshPrimitiveNames[23] = {"Cube", "Pyramid", "Wedge", "Quad", "Plane", "Sphere", "Cylinder", "Cone", "Tube", "Torus",
                                                    "TorusKnot", "Capsule", "Icosphere", "Grid", "Hemisphere", "Stairs", "Gear", "Prism",
                                                    "Octahedron", "Disc", "Tetrahedron", "Bipyramid", "RoundedBox"};
constexpr int kMeshPrimitiveCount = 23;

struct MeshRendererValues {
    float color[4] = {1, 1, 1, 1};
    bool wireframe = false;
    int shader = MeshShaderStandard;     ///< kMeshShaderNames (the old Unlit flag shows as Unlit)
    int toonBands = 3;
    float gradientTop[3] = {1, 1, 1}, gradientBottom[3] = {0, 0, 0};
    float rimStrength = 0.0f, rimPower = 3.0f, rimColor[3] = {1, 1, 1};
    bool outline = false;
    float outlineWidth = 0.02f, outlineColor[3] = {0, 0, 0};
    bool castShadows = true, receiveShadows = true, groundShadow = false;
    float groundShadowY = 0.0f, groundShadowStrength = 0.5f;
    float emissive[3] = {0, 0, 0};
    float specular = 0.0f, shininess = 32.0f;
    int primitive = -1;                  ///< kMeshPrimitiveNames, -1 for a mesh that is none of them
};

struct MeshRendererModel {
    MeshRendererValues* v = nullptr;     ///< edited in place
    int verts = 0, tris = 0;             ///< the stats line (none when there are no vertices)
    bool uvs = false;
    const InspFieldHooks* hooks = nullptr;   ///< stage 2: the fields' tooltips and prefab marks (null: none)
    /// stage 2: is the field with this label changed from the prefab? Null: none is (asked per field only for a changed component).
    std::function<bool(const char* label)> overridden;
};

struct MeshRendererIntent {
    bool changed = false;                ///< some value changed (the host writes them all back)
    bool shaderChanged = false;          ///< the old Unlit flag follows the shader
    bool primitiveChanged = false;       ///< a new mesh from the list (the mesh file is forgotten)
    std::string prefabField, prefabChosen;   ///< a prefab menu row chosen on a field
};

/// The stats line: "117 verts  •  192 tris  •  UVs" ("" when there are no vertices).
inline std::string MeshStatsText(const MeshRendererModel& m) {
    if (m.verts <= 0) return std::string();
    char buf[96];
    std::snprintf(buf, sizeof(buf), "%d verts  \xE2\x80\xA2  %d tris%s", m.verts, m.tris, m.uvs ? "  \xE2\x80\xA2  UVs" : "");
    return buf;
}

inline const char* kMeshCastTip = "This mesh blocks light (sun, spot and point lights).\nTurn off for skyboxes, decals or floors that never occlude anything.";
inline const char* kMeshReceiveTip = "Shadows from other meshes darken this one.";
inline const char* kMeshGroundTip = "Fallback: a flat contact shadow on the ground plane, drawn only\nwhile real shadows are off (Rendering > Shadows, or the sun's Cast Shadows).";
inline const char* kMeshFresnelNote = "Edge glow uses the Rim settings below.";

/// A colour field's channels as Dear ImGui's ColorEdit shows them: whole numbers 0 to 255.
inline int ColorByte(float c) { return (int)std::lround(std::min(1.0f, std::max(0.0f, c)) * 255.0f); }

/// The colour markers Dear ImGui draws down the left of a colour field's R, G, B and A cells.
inline const std::uint32_t kColorMarker[4] = {HierCol(240, 20, 20), HierCol(20, 240, 20), HierCol(20, 20, 240), HierCol(140, 140, 140)};

// ---- Add Component -------------------------------------------------------------------------------------------------------------

inline const char* kAddComponentLabel = "+  Add Component";
/// The button's width: 260 pixels, centred, on a panel wider than 300; otherwise the whole width.
inline float AddComponentWidth(float avail) { return avail > 300.0f ? 260.0f : avail; }

} // namespace okaypanels
