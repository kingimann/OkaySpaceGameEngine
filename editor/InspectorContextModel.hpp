#pragma once
// The right-click menus of the Inspector, as plain data, so the rules can be tested without a window.
//
// Each function takes a small state struct (what is selected, which kind of component, where it sits in the
// list, what the clipboard holds ...) and returns the menu as a list of Items: the label, the shortcut, whether
// it is enabled and, when it is not, a one-sentence reason (the drawing code shows it as a tooltip), plus
// separators, submenus and check marks. The ImGui code (InspectorPanel.cpp) only draws the list and carries out
// the action named by Item::id; it holds no rule of its own about order, labels, plurals or what is enabled when.
//
// Four menus:
//   ComponentMenu   the "..." button / right-click on a component header
//   TransformMenu   right-click the Transform header
//   PropertyMenu    right-click a field (one property of a component)
//   ObjectMenu      right-click the object header bar; BackgroundMenu: right-click empty space in the Inspector
//
// No ImGui, no engine, no <mutex> / <thread> (they cannot be built for the Windows programs).
// Written from the owner's request for Inspector context menus (2026-11-15); the rules are the tests'.
#include <cstddef>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace okay::inspectorctx {

/// One row of a menu.
struct Item {
    std::string id;         ///< what the row does ("comp.moveUp"); empty for a separator
    std::string label;
    std::string shortcut;   ///< "" = none
    bool enabled = true;
    std::string reason;     ///< why it is disabled and what to do instead; "" when enabled
    bool separator = false; ///< a divider line (no label)
    std::string submenu;    ///< not empty: the row opens the submenu with this id (see SubmenuItems)
    bool checked = false;   ///< draws a check mark (a switch)
    std::string tip;        ///< optional tooltip while enabled
};

namespace detail {
inline Item Sep() { Item i; i.separator = true; return i; }
inline Item Row(std::string id, std::string label) { Item i; i.id = std::move(id); i.label = std::move(label); return i; }
inline Item Off(Item i, std::string reason) { i.enabled = false; i.reason = std::move(reason); return i; }
inline Item Tip(Item i, std::string tip) { i.tip = std::move(tip); return i; }
inline std::string Num(double v) { char b[48]; std::snprintf(b, sizeof(b), "%g", v); return b; }
/// "Reset" for one object, "Reset (3 objects)" for several.
inline std::string Many(const std::string& label, int n) {
    return n > 1 ? label + " (" + std::to_string(n) + " objects)" : label;
}
/// Drop a separator that would come first, last or twice in a row (a group that came out empty leaves no stray line).
inline void Tidy(std::vector<Item>& v) {
    std::vector<Item> out;
    for (Item& it : v) {
        if (it.separator && (out.empty() || out.back().separator)) continue;
        out.push_back(std::move(it));
    }
    while (!out.empty() && out.back().separator) out.pop_back();
    v = std::move(out);
}
}   // namespace detail

/// Item ids that stand for something other than a plain action.
///   "prefab.slot": the drawing code puts the prefab rows (Revert / Apply) here; present only when the thing is changed from its prefab.
inline constexpr const char* kPrefabSlot = "prefab.slot";

// ---- Moving a component in the list -----------------------------------------------------------------------------------

enum class MoveKind { Up, Down, Top, Bottom };

/// The `delta` to give GameObject::MoveComponent. `index` is the component's slot after the Transform (0 = first),
/// `count` how many components there are after the Transform. 0 when it cannot move that way.
inline int MoveDelta(MoveKind kind, int index, int count) {
    if (index < 0 || index >= count) return 0;
    switch (kind) {
        case MoveKind::Up:     return index > 0 ? -1 : 0;
        case MoveKind::Down:   return index < count - 1 ? 1 : 0;
        case MoveKind::Top:    return -index;
        case MoveKind::Bottom: return count - 1 - index;
    }
    return 0;
}

// ---- The component menu -------------------------------------------------------------------------------------------------

enum class ComponentKind { Plain, Script, VisualScript };

struct ComponentMenuState {
    std::string typeName;               ///< as shown ("Light", "Rigidbody 2D")
    std::string typeKey;                ///< the name scripts and the scene file use ("Rigidbody2D"); "" = same as typeName
    ComponentKind kind = ComponentKind::Plain;
    int selectionCount = 1;             ///< objects selected; value actions reach the same component on each of them
    int index = 0;                      ///< slot after the Transform (0 = first)
    int count = 1;                      ///< components after the Transform
    bool enabled = true;                ///< the component's own on / off box
    bool removable = true;              ///< false: the header says it is required
    std::string neededBy;               ///< name of a component on this object that needs this one ("" = none)
    bool hasValues = false;             ///< the property registry knows this component: copy / paste / reset / duplicate work
    bool canAddAnother = true;          ///< a second one of this kind may be added (it can be made, and is not "only one per object")
    bool prefabChanged = false;         ///< changed from its prefab: the prefab rows go here
    std::string clipType;               ///< kind of component the clipboard holds ("" = nothing)
    bool clipCanAdd = false;            ///< "Paste As New Component" can add that kind to this object
    std::string clipBlockedReason;      ///< why not, when clipCanAdd is false and the clipboard holds something
    bool hasScriptFile = false;         ///< Script / Visual Script: the script is a file in the project
};

inline std::vector<Item> ComponentMenu(const ComponentMenuState& s) {
    using namespace detail;
    std::vector<Item> m;
    const bool multi = s.selectionCount > 1;

    // On / off.
    Item tog = Row("comp.toggleEnabled", Many(s.enabled ? "Disable Component" : "Enable Component", s.selectionCount));
    tog.tip = "Same as the check box in the header. A disabled component does nothing but keeps its values.";
    m.push_back(tog);
    m.push_back(Sep());

    // Order.
    const bool atTop = s.index <= 0, atBottom = s.index >= s.count - 1;
    const std::string up = "Already the first component after the Transform.";
    const std::string down = "Already the last component.";
    Item mu = Row("comp.moveUp", "Move Up");
    Item md = Row("comp.moveDown", "Move Down");
    Item mt = Row("comp.moveTop", "Move to Top");
    Item mb = Row("comp.moveBottom", "Move to Bottom");
    m.push_back(atTop ? Off(mu, up) : mu);
    m.push_back(atBottom ? Off(md, down) : md);
    m.push_back(atTop ? Off(mt, up) : mt);
    m.push_back(atBottom ? Off(mb, down) : mb);
    m.push_back(Sep());

    // Duplicate, copy, paste, reset.
    {
        Item dup = Row("comp.duplicate", Many("Duplicate Component", s.selectionCount));
        dup.tip = "Add another " + s.typeName + " with the same values, right below this one.";
        if (s.kind != ComponentKind::Plain)
            m.push_back(Off(dup, "Scripts are attached from files: drop the script file on the object to attach it again."));
        else if (!s.hasValues)
            m.push_back(Off(dup, "The editor cannot copy the values of this kind of component yet."));
        else if (!s.canAddAnother)
            m.push_back(Off(dup, "An object can hold only one " + s.typeName + "."));
        else
            m.push_back(dup);

        const std::string noValues = "The editor cannot copy the values of this kind of component yet.";
        Item cp = Row("comp.copy", "Copy Values");
        cp.tip = "Copy every value of this component. Paste Values and Paste As New Component use it.";
        m.push_back(s.hasValues ? cp : Off(cp, noValues));

        Item ps = Row("comp.paste", Many("Paste Values", s.selectionCount));
        ps.tip = "Write the copied values into this component.";
        if (!s.hasValues) m.push_back(Off(ps, noValues));
        else if (s.clipType.empty()) m.push_back(Off(ps, "Copy the values of another component of this kind first."));
        else if (s.clipType != s.typeName) m.push_back(Off(ps, "The copied values belong to a " + s.clipType + ", not a " + s.typeName + "."));
        else m.push_back(ps);

        Item pn = Row("comp.pasteNew", Many(s.clipType.empty() ? std::string("Paste As New Component") : "Paste As New " + s.clipType, s.selectionCount));
        pn.tip = "Add a new component of the copied kind, with the copied values.";
        if (s.clipType.empty()) m.push_back(Off(pn, "Copy a component's values first."));
        else if (!s.clipCanAdd) m.push_back(Off(pn, s.clipBlockedReason.empty() ? "That kind of component cannot be added here." : s.clipBlockedReason));
        else m.push_back(pn);

        Item rs = Row("comp.reset", Many("Reset", s.selectionCount));
        rs.tip = "Put every value back to what a new component has.";
        m.push_back(s.hasValues ? rs : Off(rs, noValues));
    }
    if (s.prefabChanged) { Item slot; slot.id = kPrefabSlot; m.push_back(slot); }
    m.push_back(Sep());

    // The whole list.
    m.push_back(Row("comp.expandAll", "Expand All Components"));
    m.push_back(Row("comp.collapseAll", "Collapse All Components"));

    // Scripts.
    if (s.kind != ComponentKind::Plain) {
        m.push_back(Sep());
        const bool vs = s.kind == ComponentKind::VisualScript;
        Item ed = Row("comp.editScript", vs ? "Open in Node Editor" : "Edit Script");
        ed.tip = vs ? "Open this graph in the Visual Script window." : "Open this script in the Script Editor.";
        m.push_back(vs && !s.hasScriptFile
                        ? Off(ed, "This graph is stored inside the scene. Use Open in node editor in the card to give it a file.")
                        : ed);
        Item rv = Row("comp.revealScript", vs ? "Reveal Graph in Project" : "Reveal Script in Project");
        rv.tip = "Find the file in the Project panel.";
        m.push_back(s.hasScriptFile ? rv : Off(rv, "This script is stored inside the scene, not in a file."));
        if (!vs) m.push_back(Row("comp.scriptDocs", "Scripting Reference"));
    }

    // Name and removal.
    m.push_back(Sep());
    Item cn = Row("comp.copyTypeName", "Copy Component Type Name");
    cn.tip = "Copy \"" + (s.typeKey.empty() ? s.typeName : s.typeKey) + "\" to the clipboard (the name scripts and Custom Actions use).";
    m.push_back(cn);
    Item rm = Row("comp.remove", multi ? "Remove Component from " + std::to_string(s.selectionCount) + " Objects" : "Remove Component");
    if (!s.removable) m.push_back(Off(rm, "This component is required and cannot be removed."));
    else if (!s.neededBy.empty()) m.push_back(Off(rm, s.neededBy + " needs this component. Remove " + s.neededBy + " first."));
    else m.push_back(rm);
    Tidy(m);
    return m;
}

// ---- The Transform menu -------------------------------------------------------------------------------------------------

struct TransformMenuState {
    int selectionCount = 1;
    bool clipValid = false;      ///< a Transform has been copied
    bool scaleLocked = false;    ///< a camera: its scale is not editable
    bool atDefault = false;      ///< position 0, no rotation, scale 1 (on every selected object)
    bool posAtDefault = false, rotAtDefault = false, scaleAtDefault = false;
};

inline std::vector<Item> TransformMenu(const TransformMenuState& s) {
    using namespace detail;
    std::vector<Item> m;
    m.push_back(Tip(Row("xform.copy", "Copy Transform Values"), "Copy position, rotation and scale (relative to the parent)."));
    const std::string needClip = "Copy the Transform of an object first.";
    Item ps = Row("xform.paste", Many("Paste Transform Values", s.selectionCount));
    m.push_back(s.clipValid ? ps : Off(ps, needClip));
    Item only = Row("", "Paste Only");
    only.submenu = "xform.pasteOnly";
    m.push_back(s.clipValid ? only : Off(only, needClip));
    m.push_back(Sep());

    const std::string cam = "A camera cannot be scaled.";
    Item all = Row("xform.resetAll", Many("Reset Transform", s.selectionCount));
    all.tip = s.scaleLocked ? "Position to 0 and no rotation. The camera's scale stays as it is." : "Position to 0, no rotation, scale 1.";
    const bool allDone = s.scaleLocked ? (s.posAtDefault && s.rotAtDefault) : s.atDefault;
    m.push_back(allDone ? Off(all, "Already at the default.") : all);
    Item rp = Row("xform.resetPos", "Reset Position");
    Item rr = Row("xform.resetRot", "Reset Rotation");
    Item rsc = Row("xform.resetScale", "Reset Scale");
    m.push_back(s.posAtDefault ? Off(rp, "Position is already 0.") : rp);
    m.push_back(s.rotAtDefault ? Off(rr, "There is no rotation already.") : rr);
    m.push_back(s.scaleLocked ? Off(rsc, cam) : s.scaleAtDefault ? Off(rsc, "Scale is already 1.") : rsc);
    m.push_back(Sep());
    m.push_back(Row("comp.expandAll", "Expand All Components"));
    m.push_back(Row("comp.collapseAll", "Collapse All Components"));
    Tidy(m);
    return m;
}

/// The items of a submenu named by Item::submenu. Empty for an unknown id.
inline std::vector<Item> SubmenuItems(const std::string& id, const TransformMenuState& s) {
    using namespace detail;
    std::vector<Item> m;
    if (id == "xform.pasteOnly") {
        m.push_back(Row("xform.pastePos", "Position"));
        m.push_back(Row("xform.pasteRot", "Rotation"));
        Item sc = Row("xform.pasteScale", "Scale");
        m.push_back(s.scaleLocked ? Off(sc, "A camera cannot be scaled.") : sc);
    }
    return m;
}

// ---- The property menu --------------------------------------------------------------------------------------------------

enum class ValueKind { None, Bool, Int, Float, String, Vec2, Vec3, Vec4, Quat, Color, Enum, Asset, Object };

inline bool IsNumeric(ValueKind k) {
    return k == ValueKind::Int || k == ValueKind::Float || k == ValueKind::Vec2 || k == ValueKind::Vec3 || k == ValueKind::Vec4;
}

struct PropertyMenuState {
    std::string path;                   ///< "Light.intensity"; what Copy Property Path copies
    std::string name;                   ///< "Intensity"
    ValueKind kind = ValueKind::None;
    int selectionCount = 1;             ///< objects the write reaches
    bool readOnly = false;
    bool hasDefault = false;            ///< the value of a new component is known
    bool atDefault = false;             ///< every selected object already holds it
    bool clipboardFits = false;         ///< the text on the clipboard reads as this kind of value
    bool hasRange = false;
    double rangeMin = 0, rangeMax = 0;
    bool valueEmpty = false;            ///< String / Asset / Object: nothing set
    bool overridden = false;            ///< a prefab instance's property that differs from the prefab
    bool objectResolves = false;        ///< Object: the referenced object exists in the scene
};

inline std::vector<Item> PropertyMenu(const PropertyMenuState& s) {
    using namespace detail;
    std::vector<Item> m;
    const std::string ro = "This value is read-only.";
    auto write = [&](Item it) { return s.readOnly ? Off(std::move(it), ro) : it; };

    if (s.overridden) { Item slot; slot.id = kPrefabSlot; m.push_back(slot); m.push_back(Sep()); }

    Item rs = Row("prop.reset", Many("Reset to Default", s.selectionCount));
    rs.tip = "Put " + s.name + " back to what a new component has.";
    if (s.readOnly) m.push_back(Off(rs, ro));
    else if (!s.hasDefault) m.push_back(Off(rs, "The editor does not know the default of this value."));
    else if (s.atDefault) m.push_back(Off(rs, "Already at its default."));
    else m.push_back(rs);
    m.push_back(Sep());

    Item cp = Row("prop.copy", "Copy Value");
    cp.tip = "Copy " + s.name + " as text. It can be pasted into another field of the same kind.";
    m.push_back(cp);
    Item pv = Row("prop.paste", Many("Paste Value", s.selectionCount));
    if (s.readOnly) m.push_back(Off(pv, ro));
    else if (!s.clipboardFits) m.push_back(Off(pv, "The clipboard does not hold a value this field can take. Use Copy Value on a field of the same kind."));
    else m.push_back(pv);
    Item path = Row("prop.copyPath", "Copy Property Path");
    path.tip = "Copy \"" + s.path + "\": the name scripts, the visual script and Custom Actions use for this value.";
    m.push_back(path);

    // Quick values for the kind.
    std::vector<Item> quick;
    if (IsNumeric(s.kind)) {
        Item z = Row("prop.zero", Many("Set to Zero", s.selectionCount));
        Item o = Row("prop.one", Many("Set to One", s.selectionCount));
        const auto outside = [&](double v) { return s.hasRange && (v < s.rangeMin || v > s.rangeMax); };
        const std::string span = s.hasRange ? " (" + Num(s.rangeMin) + " to " + Num(s.rangeMax) + ")" : std::string();
        quick.push_back(write(outside(0.0) ? Off(z, "Zero is outside this value's range" + span + ".") : z));
        quick.push_back(write(outside(1.0) ? Off(o, "One is outside this value's range" + span + ".") : o));
    } else if (s.kind == ValueKind::Quat) {
        quick.push_back(write(Row("prop.identity", Many("Set to Identity (no rotation)", s.selectionCount))));
    } else if (s.kind == ValueKind::Color) {
        quick.push_back(write(Row("prop.white", Many("Set to White", s.selectionCount))));
    }
    if (s.kind == ValueKind::String || s.kind == ValueKind::Asset || s.kind == ValueKind::Object) {
        Item cl = Row("prop.clear", Many("Clear", s.selectionCount));
        quick.push_back(write(s.valueEmpty ? Off(cl, "It is already empty.") : cl));
    }
    if (s.kind == ValueKind::Asset) {
        Item sh = Row("prop.showAsset", "Show in Project");
        sh.tip = "Select the file in the Project panel.";
        quick.push_back(s.valueEmpty ? Off(sh, "No file is set.") : sh);
    }
    if (s.kind == ValueKind::Object) {
        Item so = Row("prop.selectObject", "Select Object");
        so.tip = "Select the referenced object.";
        quick.push_back(s.valueEmpty ? Off(so, "No object is set.") : !s.objectResolves ? Off(so, "The referenced object is not in this scene.") : so);
    }
    if (!quick.empty()) {
        m.push_back(Sep());
        for (Item& q : quick) m.push_back(std::move(q));
    }
    Tidy(m);
    return m;
}

// ---- The object header menu and the empty-space menu -------------------------------------------------------------------------

struct ObjectMenuState {
    int selectionCount = 1;
    bool hasParent = false;
    std::string parentName;
    bool inPrefab = false;              ///< the object is, or is part of, a prefab instance
    bool isPrefabRoot = false;          ///< ... and it is the instance's root
    bool prefabMissing = false;         ///< ... whose prefab file cannot be found
    std::string prefabName;
    bool hasObjectClip = false;         ///< all component values of an object have been copied
    std::string clipObjectName;         ///< ... from this object
    int componentCount = 0;             ///< components after the Transform
    std::string clipType;               ///< kind of component on the component clipboard ("" = none), for Paste As New
    bool clipCanAdd = false;
    std::string clipBlockedReason;
};

inline std::vector<Item> ObjectMenu(const ObjectMenuState& s) {
    using namespace detail;
    std::vector<Item> m;
    Item ca = Row("obj.copyAll", "Copy All Component Values");
    ca.tip = "Copy the values of every component of this object, the Transform's position, rotation and scale included (not its name, tag or place in the Hierarchy).";
    m.push_back(ca);
    Item pa = Row("obj.pasteAll", Many("Paste All Component Values", s.selectionCount));
    pa.tip = "Write the copied values into the components this object has of the same kinds.";
    m.push_back(s.hasObjectClip ? pa : Off(pa, "Use Copy All Component Values on an object first."));
    m.push_back(Row("obj.resetTransform", Many("Reset Transform", s.selectionCount)));
    m.push_back(Sep());

    Item sp = Row("obj.selectParent", "Select Parent");
    m.push_back(s.hasParent ? Tip(sp, "Select " + s.parentName + ".") : Off(sp, "This object is at the top level."));
    if (s.inPrefab) {
        if (!s.isPrefabRoot) m.push_back(Row("obj.selectPrefabRoot", "Select Prefab Root"));
        Item sa = Row("obj.selectPrefabAsset", "Select Prefab Asset");
        sa.tip = "Select " + s.prefabName + " in the Project panel.";
        m.push_back(s.prefabMissing ? Off(sa, s.prefabName + " is missing from the project.") : sa);
    }
    m.push_back(Sep());
    m.push_back(Row("obj.copyName", "Copy Object Name"));
    m.push_back(Row("obj.copyPath", "Copy Object Path"));
    m.push_back(Sep());
    m.push_back(Row("obj.addComponent", "Add Component..."));
    m.push_back(Row("comp.expandAll", "Expand All Components"));
    m.push_back(Row("comp.collapseAll", "Collapse All Components"));
    Tidy(m);
    return m;
}

inline std::vector<Item> BackgroundMenu(const ObjectMenuState& s) {
    using namespace detail;
    std::vector<Item> m;
    m.push_back(Row("obj.addComponent", "Add Component..."));
    Item pn = Row("comp.pasteNew", Many(s.clipType.empty() ? std::string("Paste As New Component") : "Paste As New " + s.clipType, s.selectionCount));
    pn.tip = "Add a new component of the copied kind, with the copied values.";
    if (s.clipType.empty()) m.push_back(Off(pn, "Copy a component's values first."));
    else if (!s.clipCanAdd) m.push_back(Off(pn, s.clipBlockedReason.empty() ? "That kind of component cannot be added here." : s.clipBlockedReason));
    else m.push_back(pn);
    Item pa = Row("obj.pasteAll", Many("Paste All Component Values", s.selectionCount));
    m.push_back(s.hasObjectClip ? pa : Off(pa, "Use Copy All Component Values on an object first."));
    m.push_back(Sep());
    Item ex = Row("comp.expandAll", "Expand All Components");
    Item co = Row("comp.collapseAll", "Collapse All Components");
    m.push_back(s.componentCount > 0 ? ex : Off(ex, "This object has no components besides the Transform."));
    m.push_back(s.componentCount > 0 ? co : Off(co, "This object has no components besides the Transform."));
    Tidy(m);
    return m;
}

/// Find a row by id (tests and the drawing code). Null when absent.
inline const Item* Find(const std::vector<Item>& items, const std::string& id) {
    for (const Item& it : items) if (it.id == id) return &it;
    return nullptr;
}

}   // namespace okay::inspectorctx
