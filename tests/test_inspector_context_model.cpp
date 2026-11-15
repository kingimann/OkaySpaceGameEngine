// The Inspector's right-click menus as data (editor/InspectorContextModel.hpp, no ImGui, no engine): the order,
// labels, plurals and what is enabled when, for the component header, the Transform header, a field, the object
// header and the empty space. The drawing code only draws these lists.
#include "test_framework.hpp"
#include "InspectorContextModel.hpp"
#include <set>
#include <string>
#include <vector>

using namespace okay::inspectorctx;

namespace {

std::vector<std::string> Ids(const std::vector<Item>& m) {
    std::vector<std::string> out;
    for (const Item& it : m) if (!it.separator) out.push_back(it.id.empty() ? "submenu:" + it.submenu : it.id);
    return out;
}
std::size_t IndexOf(const std::vector<Item>& m, const std::string& id) {
    for (std::size_t i = 0; i < m.size(); ++i) if (m[i].id == id) return i;
    return (std::size_t)-1;
}
bool Has(const std::vector<Item>& m, const std::string& id) { return Find(m, id) != nullptr; }
bool On(const std::vector<Item>& m, const std::string& id) { const Item* i = Find(m, id); return i && i->enabled; }
bool Off(const std::vector<Item>& m, const std::string& id) { const Item* i = Find(m, id); return i && !i->enabled; }
std::string Label(const std::vector<Item>& m, const std::string& id) { const Item* i = Find(m, id); return i ? i->label : "<missing>"; }
std::string Why(const std::vector<Item>& m, const std::string& id) { const Item* i = Find(m, id); return i ? i->reason : "<missing>"; }
bool Contains(const std::string& s, const std::string& what) { return s.find(what) != std::string::npos; }

// Rules every menu must follow, whatever the state.
void CheckInvariants(const std::vector<Item>& m, const char* where) {
    bool prevSep = true;   // a separator may not be first
    for (const Item& it : m) {
        if (it.separator) {
            CHECK(!prevSep);                                   // no double separator, none first
            CHECK(it.id.empty() && it.label.empty());          // a divider has no text
        } else {
            CHECK(!it.label.empty() || it.id == kPrefabSlot);  // every row says what it is (the prefab slot is drawn by the panel)
            if (it.id != kPrefabSlot) {
                CHECK(!it.id.empty() || !it.submenu.empty());  // and does something
                CHECK(it.enabled == it.reason.empty());        // disabled <=> there is a reason
            }
        }
        prevSep = it.separator;
    }
    CHECK(m.empty() || !m.back().separator);                   // none last
    std::set<std::string> seen;
    for (const Item& it : m) {
        if (it.separator || it.id.empty()) continue;
        CHECK(seen.insert(it.id).second);                      // each action once
    }
    (void)where;
}

ComponentMenuState Middle() {
    ComponentMenuState s;
    s.typeName = "Light"; s.index = 1; s.count = 3; s.hasValues = true; s.canAddAnother = true;
    return s;
}

void TestMoveDelta() {
    RUN_SUITE("move deltas");
    // 4 components after the Transform: slots 0..3
    CHECK(MoveDelta(MoveKind::Up, 2, 4) == -1);
    CHECK(MoveDelta(MoveKind::Down, 2, 4) == 1);
    CHECK(MoveDelta(MoveKind::Top, 2, 4) == -2);
    CHECK(MoveDelta(MoveKind::Bottom, 2, 4) == 1);
    CHECK(MoveDelta(MoveKind::Up, 0, 4) == 0);              // already first
    CHECK(MoveDelta(MoveKind::Top, 0, 4) == 0);
    CHECK(MoveDelta(MoveKind::Down, 3, 4) == 0);            // already last
    CHECK(MoveDelta(MoveKind::Bottom, 3, 4) == 0);
    CHECK(MoveDelta(MoveKind::Top, 3, 4) == -3);
    CHECK(MoveDelta(MoveKind::Bottom, 0, 4) == 3);
    CHECK(MoveDelta(MoveKind::Up, -1, 4) == 0);             // not on the object
    CHECK(MoveDelta(MoveKind::Down, 4, 4) == 0);
    CHECK(MoveDelta(MoveKind::Top, 0, 1) == 0 && MoveDelta(MoveKind::Bottom, 0, 1) == 0);   // the only one
}

void TestComponentMenuOrderAndMoves() {
    RUN_SUITE("component menu: order and moves");
    const std::vector<Item> m = ComponentMenu(Middle());
    CheckInvariants(m, "middle");
    CHECK(Ids(m) == (std::vector<std::string>{
        "comp.toggleEnabled",
        "comp.moveUp", "comp.moveDown", "comp.moveTop", "comp.moveBottom",
        "comp.duplicate", "comp.copy", "comp.paste", "comp.pasteNew", "comp.reset",
        "comp.expandAll", "comp.collapseAll",
        "comp.copyTypeName", "comp.remove"}));
    CHECK(Label(m, "comp.toggleEnabled") == "Disable Component");
    for (const char* id : {"comp.moveUp", "comp.moveDown", "comp.moveTop", "comp.moveBottom"}) CHECK(On(m, id));

    ComponentMenuState first = Middle(); first.index = 0;
    const std::vector<Item> mf = ComponentMenu(first);
    CHECK(Off(mf, "comp.moveUp") && Off(mf, "comp.moveTop"));
    CHECK(On(mf, "comp.moveDown") && On(mf, "comp.moveBottom"));
    CHECK(Contains(Why(mf, "comp.moveUp"), "first"));
    CheckInvariants(mf, "first");

    ComponentMenuState last = Middle(); last.index = 2;
    const std::vector<Item> ml = ComponentMenu(last);
    CHECK(Off(ml, "comp.moveDown") && Off(ml, "comp.moveBottom"));
    CHECK(On(ml, "comp.moveUp") && On(ml, "comp.moveTop"));
    CHECK(Contains(Why(ml, "comp.moveDown"), "last"));

    ComponentMenuState only = Middle(); only.index = 0; only.count = 1;
    const std::vector<Item> mo = ComponentMenu(only);
    for (const char* id : {"comp.moveUp", "comp.moveDown", "comp.moveTop", "comp.moveBottom"}) CHECK(Off(mo, id));

    // The on / off row follows the state.
    ComponentMenuState off = Middle(); off.enabled = false;
    CHECK(Label(ComponentMenu(off), "comp.toggleEnabled") == "Enable Component");
}

void TestComponentMenuValues() {
    RUN_SUITE("component menu: values, duplicate, paste as new");
    ComponentMenuState s = Middle();
    // Nothing on the clipboard.
    std::vector<Item> m = ComponentMenu(s);
    CHECK(On(m, "comp.copy") && On(m, "comp.reset") && On(m, "comp.duplicate"));
    CHECK(Off(m, "comp.paste") && Contains(Why(m, "comp.paste"), "Copy the values"));
    CHECK(Off(m, "comp.pasteNew") && Label(m, "comp.pasteNew") == "Paste As New Component");

    // A Light on the clipboard.
    s.clipType = "Light"; s.clipCanAdd = true;
    m = ComponentMenu(s);
    CHECK(On(m, "comp.paste"));
    CHECK(On(m, "comp.pasteNew") && Label(m, "comp.pasteNew") == "Paste As New Light");

    // A different kind on the clipboard: Paste Values says so, Paste As New still works.
    s.clipType = "Rigidbody 2D";
    m = ComponentMenu(s);
    CHECK(Off(m, "comp.paste") && Contains(Why(m, "comp.paste"), "Rigidbody 2D") && Contains(Why(m, "comp.paste"), "Light"));
    CHECK(On(m, "comp.pasteNew") && Label(m, "comp.pasteNew") == "Paste As New Rigidbody 2D");

    // The clipboard kind cannot be added here (only one per object): the reason is the caller's.
    s.clipCanAdd = false; s.clipBlockedReason = "This object already has a Rigidbody 2D, and only one is allowed.";
    m = ComponentMenu(s);
    CHECK(Off(m, "comp.pasteNew") && Why(m, "comp.pasteNew") == s.clipBlockedReason);
    s.clipBlockedReason.clear();
    m = ComponentMenu(s);
    CHECK(Off(m, "comp.pasteNew") && !Why(m, "comp.pasteNew").empty());   // a default reason, never an empty tooltip

    // A kind that is only allowed once cannot be duplicated.
    ComponentMenuState one = Middle(); one.canAddAnother = false;
    m = ComponentMenu(one);
    CHECK(Off(m, "comp.duplicate") && Contains(Why(m, "comp.duplicate"), "only one Light"));
    CHECK(On(m, "comp.copy"));

    // No registered values: nothing to copy, paste, reset or duplicate, and each says why.
    ComponentMenuState bare = Middle(); bare.hasValues = false;
    m = ComponentMenu(bare);
    for (const char* id : {"comp.duplicate", "comp.copy", "comp.paste", "comp.reset"}) CHECK(Off(m, id) && !Why(m, id).empty());
    CHECK(On(m, "comp.moveUp") && On(m, "comp.copyTypeName") && On(m, "comp.remove"));   // the rest still works
    CheckInvariants(m, "bare");

    // Several selected objects: the value actions say how many they reach; copy and order do not.
    ComponentMenuState many = Middle(); many.selectionCount = 3; many.clipType = "Light"; many.clipCanAdd = true;
    m = ComponentMenu(many);
    CHECK(Label(m, "comp.toggleEnabled") == "Disable Component (3 objects)");
    CHECK(Label(m, "comp.duplicate") == "Duplicate Component (3 objects)");
    CHECK(Label(m, "comp.paste") == "Paste Values (3 objects)");
    CHECK(Label(m, "comp.pasteNew") == "Paste As New Light (3 objects)");
    CHECK(Label(m, "comp.reset") == "Reset (3 objects)");
    CHECK(Label(m, "comp.copy") == "Copy Values");
    CHECK(Label(m, "comp.moveUp") == "Move Up");
    CHECK(Label(m, "comp.remove") == "Remove Component from 3 Objects");
    CheckInvariants(m, "many");
    CHECK(Label(ComponentMenu(Middle()), "comp.reset") == "Reset");   // one object: no suffix
}

void TestComponentMenuRemove() {
    RUN_SUITE("component menu: remove");
    ComponentMenuState s = Middle();
    CHECK(On(ComponentMenu(s), "comp.remove") && Label(ComponentMenu(s), "comp.remove") == "Remove Component");

    s.removable = false;
    std::vector<Item> m = ComponentMenu(s);
    CHECK(Off(m, "comp.remove") && Contains(Why(m, "comp.remove"), "required"));

    s.removable = true; s.neededBy = "Rigidbody 2D";
    m = ComponentMenu(s);
    CHECK(Off(m, "comp.remove"));
    CHECK(Contains(Why(m, "comp.remove"), "Rigidbody 2D needs this component"));
    CHECK(Contains(Why(m, "comp.remove"), "Remove Rigidbody 2D first"));
    CHECK(On(m, "comp.moveUp"));   // only Remove is blocked
    CheckInvariants(m, "needed");

    // Remove is always the last row, after the type name.
    CHECK(IndexOf(m, "comp.remove") == m.size() - 1);
    CHECK(IndexOf(m, "comp.copyTypeName") < IndexOf(m, "comp.remove"));
}

void TestComponentMenuScripts() {
    RUN_SUITE("component menu: scripts and prefab slot");
    ComponentMenuState plain = Middle();
    std::vector<Item> m = ComponentMenu(plain);
    CHECK(!Has(m, "comp.editScript") && !Has(m, "comp.revealScript") && !Has(m, "comp.scriptDocs"));   // only for scripts

    ComponentMenuState sc = Middle(); sc.typeName = "Script"; sc.kind = ComponentKind::Script; sc.hasValues = false; sc.hasScriptFile = true;
    m = ComponentMenu(sc);
    CHECK(Label(m, "comp.editScript") == "Edit Script" && On(m, "comp.editScript"));
    CHECK(Label(m, "comp.revealScript") == "Reveal Script in Project" && On(m, "comp.revealScript"));
    CHECK(On(m, "comp.scriptDocs") && Label(m, "comp.scriptDocs") == "Scripting Reference");
    CHECK(Off(m, "comp.duplicate") && Contains(Why(m, "comp.duplicate"), "drop the script file"));
    CheckInvariants(m, "script");

    sc.hasScriptFile = false;   // an inline script: it still opens in the editor, but there is no file to reveal
    m = ComponentMenu(sc);
    CHECK(On(m, "comp.editScript"));
    CHECK(Off(m, "comp.revealScript") && Contains(Why(m, "comp.revealScript"), "inside the scene"));

    ComponentMenuState vs = Middle(); vs.typeName = "Visual Script"; vs.kind = ComponentKind::VisualScript; vs.hasScriptFile = true;
    m = ComponentMenu(vs);
    CHECK(Label(m, "comp.editScript") == "Open in Node Editor" && On(m, "comp.editScript"));
    CHECK(Label(m, "comp.revealScript") == "Reveal Graph in Project");
    CHECK(!Has(m, "comp.scriptDocs"));
    vs.hasScriptFile = false;   // an embedded graph needs a file first
    m = ComponentMenu(vs);
    CHECK(Off(m, "comp.editScript") && !Why(m, "comp.editScript").empty() && Off(m, "comp.revealScript"));

    // The prefab rows sit right after Reset, and only for a changed component.
    CHECK(!Has(ComponentMenu(plain), kPrefabSlot));
    ComponentMenuState pf = Middle(); pf.prefabChanged = true;
    m = ComponentMenu(pf);
    CHECK(Has(m, kPrefabSlot));
    CHECK(IndexOf(m, kPrefabSlot) == IndexOf(m, "comp.reset") + 1);
    CheckInvariants(m, "prefab");
}

void TestTransformMenu() {
    RUN_SUITE("transform menu");
    TransformMenuState s;
    std::vector<Item> m = TransformMenu(s);
    CheckInvariants(m, "transform");
    CHECK(Label(m, "xform.copy") == "Copy Transform Values" && On(m, "xform.copy"));
    CHECK(Label(m, "xform.paste") == "Paste Transform Values");
    CHECK(Off(m, "xform.paste") && Contains(Why(m, "xform.paste"), "Copy the Transform"));
    const Item* only = nullptr;
    for (const Item& it : m) if (it.submenu == "xform.pasteOnly") only = &it;
    CHECK(only && !only->enabled);                          // nothing copied yet
    CHECK(On(m, "xform.resetAll") && On(m, "xform.resetPos") && On(m, "xform.resetRot") && On(m, "xform.resetScale"));

    s.clipValid = true;
    m = TransformMenu(s);
    CHECK(On(m, "xform.paste"));
    only = nullptr;
    for (const Item& it : m) if (it.submenu == "xform.pasteOnly") only = &it;
    CHECK(only && only->enabled);
    const std::vector<Item> sub = SubmenuItems("xform.pasteOnly", s);
    CHECK(Ids(sub) == (std::vector<std::string>{"xform.pastePos", "xform.pasteRot", "xform.pasteScale"}));
    CHECK(On(sub, "xform.pasteScale"));
    CHECK(SubmenuItems("nope", s).empty());

    // Already at the default: the resets say so.
    TransformMenuState def; def.atDefault = def.posAtDefault = def.rotAtDefault = def.scaleAtDefault = true;
    m = TransformMenu(def);
    for (const char* id : {"xform.resetAll", "xform.resetPos", "xform.resetRot", "xform.resetScale"}) CHECK(Off(m, id) && !Why(m, id).empty());
    CheckInvariants(m, "transform default");

    // Only the position differs: Reset Position and Reset Transform work, the others do not.
    TransformMenuState p; p.posAtDefault = false; p.rotAtDefault = p.scaleAtDefault = true;
    m = TransformMenu(p);
    CHECK(On(m, "xform.resetAll") && On(m, "xform.resetPos") && Off(m, "xform.resetRot") && Off(m, "xform.resetScale"));

    // A camera: scale is locked, and Reset Transform is done once position and rotation are.
    TransformMenuState cam; cam.scaleLocked = true; cam.clipValid = true; cam.posAtDefault = cam.rotAtDefault = true; cam.atDefault = false;
    m = TransformMenu(cam);
    CHECK(Off(m, "xform.resetScale") && Contains(Why(m, "xform.resetScale"), "camera"));
    CHECK(Off(m, "xform.resetAll"));                         // the camera's own scale is not its business
    CHECK(Off(SubmenuItems("xform.pasteOnly", cam), "xform.pasteScale"));
    cam.rotAtDefault = false;
    CHECK(On(TransformMenu(cam), "xform.resetAll"));

    // Several objects.
    TransformMenuState many; many.selectionCount = 4; many.clipValid = true;
    m = TransformMenu(many);
    CHECK(Label(m, "xform.resetAll") == "Reset Transform (4 objects)" && Label(m, "xform.paste") == "Paste Transform Values (4 objects)");
    CHECK(Label(m, "xform.copy") == "Copy Transform Values");
}

PropertyMenuState Prop(ValueKind k) {
    PropertyMenuState s;
    s.path = "Light.intensity"; s.name = "Intensity"; s.kind = k; s.hasDefault = true;
    return s;
}

void TestPropertyMenu() {
    RUN_SUITE("property menu");
    PropertyMenuState f = Prop(ValueKind::Float);
    std::vector<Item> m = PropertyMenu(f);
    CheckInvariants(m, "float");
    CHECK(Ids(m) == (std::vector<std::string>{"prop.reset", "prop.copy", "prop.paste", "prop.copyPath", "prop.zero", "prop.one"}));
    CHECK(On(m, "prop.reset") && On(m, "prop.copy") && On(m, "prop.copyPath") && On(m, "prop.zero") && On(m, "prop.one"));
    CHECK(Off(m, "prop.paste") && Contains(Why(m, "prop.paste"), "clipboard"));
    CHECK(Contains(Find(m, "prop.copyPath")->tip, "Light.intensity"));

    f.clipboardFits = true;
    CHECK(On(PropertyMenu(f), "prop.paste"));

    // Reset: unknown default, already default, read-only.
    f.hasDefault = false;
    m = PropertyMenu(f);
    CHECK(Off(m, "prop.reset") && Contains(Why(m, "prop.reset"), "default"));
    f.hasDefault = true; f.atDefault = true;
    CHECK(Off(PropertyMenu(f), "prop.reset") && Contains(Why(PropertyMenu(f), "prop.reset"), "Already"));

    // Range: Set to Zero / One only where the value may be that.
    PropertyMenuState r = Prop(ValueKind::Float); r.hasRange = true; r.rangeMin = 0.5; r.rangeMax = 8;
    m = PropertyMenu(r);
    CHECK(Off(m, "prop.zero") && Contains(Why(m, "prop.zero"), "0.5 to 8"));
    CHECK(On(m, "prop.one"));
    r.rangeMin = 2;
    CHECK(Off(PropertyMenu(r), "prop.one") && Contains(Why(PropertyMenu(r), "prop.one"), "outside"));
    r.rangeMin = 0; r.rangeMax = 1;
    CHECK(On(PropertyMenu(r), "prop.zero") && On(PropertyMenu(r), "prop.one"));   // the ends count

    // Read-only: nothing writes, copying still works.
    PropertyMenuState ro = Prop(ValueKind::Vec3); ro.readOnly = true; ro.clipboardFits = true;
    m = PropertyMenu(ro);
    for (const char* id : {"prop.reset", "prop.paste", "prop.zero", "prop.one"}) CHECK(Off(m, id) && Contains(Why(m, id), "read-only"));
    CHECK(On(m, "prop.copy") && On(m, "prop.copyPath"));
    CheckInvariants(m, "read-only");

    // Vectors get both quick values; a Quat gets Identity; a Color gets White; a Bool gets none.
    CHECK(Has(PropertyMenu(Prop(ValueKind::Vec2)), "prop.zero") && Has(PropertyMenu(Prop(ValueKind::Vec4)), "prop.one"));
    PropertyMenuState q = Prop(ValueKind::Quat);
    m = PropertyMenu(q);
    CHECK(Has(m, "prop.identity") && !Has(m, "prop.zero") && !Has(m, "prop.one"));
    CHECK(Label(m, "prop.identity") == "Set to Identity (no rotation)");
    CHECK(Has(PropertyMenu(Prop(ValueKind::Color)), "prop.white"));
    for (ValueKind k : {ValueKind::Bool, ValueKind::Enum}) {
        m = PropertyMenu(Prop(k));
        CHECK(!Has(m, "prop.zero") && !Has(m, "prop.identity") && !Has(m, "prop.clear") && !Has(m, "prop.white"));
    }

    // Asset and object references.
    PropertyMenuState a = Prop(ValueKind::Asset); a.valueEmpty = true;
    m = PropertyMenu(a);
    CHECK(Off(m, "prop.clear") && Off(m, "prop.showAsset") && Contains(Why(m, "prop.showAsset"), "No file"));
    a.valueEmpty = false;
    m = PropertyMenu(a);
    CHECK(On(m, "prop.clear") && On(m, "prop.showAsset"));
    CheckInvariants(m, "asset");

    PropertyMenuState o = Prop(ValueKind::Object); o.valueEmpty = false; o.objectResolves = false;
    m = PropertyMenu(o);
    CHECK(Off(m, "prop.selectObject") && Contains(Why(m, "prop.selectObject"), "not in this scene"));
    o.objectResolves = true;
    CHECK(On(PropertyMenu(o), "prop.selectObject"));
    o.valueEmpty = true;
    CHECK(Off(PropertyMenu(o), "prop.selectObject") && Contains(Why(PropertyMenu(o), "prop.selectObject"), "No object"));
    CHECK(Has(PropertyMenu(Prop(ValueKind::String)), "prop.clear") && !Has(PropertyMenu(Prop(ValueKind::String)), "prop.showAsset"));

    // Prefab instance: the prefab rows go first, then a divider.
    PropertyMenuState pf = Prop(ValueKind::Float); pf.overridden = true;
    m = PropertyMenu(pf);
    CHECK(!m.empty() && m[0].id == kPrefabSlot && m[1].separator);
    CHECK(!Has(PropertyMenu(Prop(ValueKind::Float)), kPrefabSlot));
    CheckInvariants(m, "prefab");

    // Several objects.
    PropertyMenuState many = Prop(ValueKind::Vec3); many.selectionCount = 5; many.clipboardFits = true;
    m = PropertyMenu(many);
    CHECK(Label(m, "prop.reset") == "Reset to Default (5 objects)");
    CHECK(Label(m, "prop.paste") == "Paste Value (5 objects)");
    CHECK(Label(m, "prop.zero") == "Set to Zero (5 objects)");
    CHECK(Label(m, "prop.copy") == "Copy Value");
}

void TestObjectAndBackgroundMenus() {
    RUN_SUITE("object header and empty-space menus");
    ObjectMenuState s;
    std::vector<Item> m = ObjectMenu(s);
    CheckInvariants(m, "object");
    CHECK(Ids(m) == (std::vector<std::string>{"obj.copyAll", "obj.pasteAll", "obj.resetTransform", "obj.selectParent",
                                              "obj.copyName", "obj.copyPath", "obj.addComponent", "comp.expandAll", "comp.collapseAll"}));
    CHECK(On(m, "obj.copyAll") && On(m, "obj.resetTransform") && On(m, "obj.addComponent"));
    CHECK(Off(m, "obj.pasteAll") && Contains(Why(m, "obj.pasteAll"), "Copy All Component Values"));
    CHECK(Off(m, "obj.selectParent") && Contains(Why(m, "obj.selectParent"), "top level"));

    s.hasParent = true; s.parentName = "Level"; s.hasObjectClip = true; s.clipObjectName = "Orc";
    m = ObjectMenu(s);
    CHECK(On(m, "obj.selectParent") && Contains(Find(m, "obj.selectParent")->tip, "Level"));
    CHECK(On(m, "obj.pasteAll"));

    // A prefab instance: root, then a part of it, then a missing asset.
    CHECK(!Has(m, "obj.selectPrefabAsset") && !Has(m, "obj.selectPrefabRoot"));
    ObjectMenuState root = s; root.inPrefab = true; root.isPrefabRoot = true; root.prefabName = "Orc.okayprefab";
    m = ObjectMenu(root);
    CHECK(On(m, "obj.selectPrefabAsset") && !Has(m, "obj.selectPrefabRoot"));
    CHECK(Contains(Find(m, "obj.selectPrefabAsset")->tip, "Orc.okayprefab"));
    ObjectMenuState part = root; part.isPrefabRoot = false;
    m = ObjectMenu(part);
    CHECK(On(m, "obj.selectPrefabRoot") && On(m, "obj.selectPrefabAsset"));
    CHECK(IndexOf(m, "obj.selectPrefabRoot") < IndexOf(m, "obj.selectPrefabAsset"));
    part.prefabMissing = true;
    m = ObjectMenu(part);
    CHECK(Off(m, "obj.selectPrefabAsset") && Contains(Why(m, "obj.selectPrefabAsset"), "missing"));
    CheckInvariants(m, "prefab part");

    ObjectMenuState many = s; many.selectionCount = 2;
    m = ObjectMenu(many);
    CHECK(Label(m, "obj.pasteAll") == "Paste All Component Values (2 objects)");
    CHECK(Label(m, "obj.resetTransform") == "Reset Transform (2 objects)");
    CHECK(Label(m, "obj.copyAll") == "Copy All Component Values");

    // Empty space.
    ObjectMenuState bg; bg.componentCount = 2;
    m = BackgroundMenu(bg);
    CheckInvariants(m, "background");
    CHECK(Ids(m) == (std::vector<std::string>{"obj.addComponent", "comp.pasteNew", "obj.pasteAll", "comp.expandAll", "comp.collapseAll"}));
    CHECK(On(m, "obj.addComponent") && Off(m, "comp.pasteNew") && Off(m, "obj.pasteAll"));
    CHECK(On(m, "comp.expandAll") && On(m, "comp.collapseAll"));
    bg.clipType = "Light"; bg.clipCanAdd = true; bg.hasObjectClip = true; bg.selectionCount = 3;
    m = BackgroundMenu(bg);
    CHECK(Label(m, "comp.pasteNew") == "Paste As New Light (3 objects)" && On(m, "comp.pasteNew"));
    CHECK(On(m, "obj.pasteAll"));
    bg.clipCanAdd = false; bg.clipBlockedReason = "This object already has a Light, and only one is allowed.";
    CHECK(Why(BackgroundMenu(bg), "comp.pasteNew") == bg.clipBlockedReason);
    ObjectMenuState bare; bare.componentCount = 0;
    m = BackgroundMenu(bare);
    CHECK(Off(m, "comp.expandAll") && Off(m, "comp.collapseAll") && Contains(Why(m, "comp.expandAll"), "no components"));
}

// Walk every on / off combination of the states and check the rules that hold for all of them.
void TestInvariantSweep() {
    RUN_SUITE("every state obeys the menu rules");
    int menus = 0;
    for (int bits = 0; bits < (1 << 9); ++bits) {
        for (int pos = 0; pos < 3; ++pos) {
            for (int kind = 0; kind < 3; ++kind) {
                ComponentMenuState s;
                s.typeName = "Thing"; s.count = 3; s.index = pos; s.kind = (ComponentKind)kind;
                s.enabled = bits & 1; s.removable = bits & 2; s.hasValues = bits & 4; s.canAddAnother = bits & 8;
                s.prefabChanged = bits & 16; s.hasScriptFile = bits & 32; s.clipCanAdd = bits & 64;
                if (bits & 128) s.clipType = "Thing"; else if (bits & 256) s.clipType = "Other";
                if (bits & 256) s.neededBy = "Dependant";
                s.selectionCount = 1 + (bits % 3);
                CheckInvariants(ComponentMenu(s), "component sweep");
                ++menus;
            }
        }
    }
    for (int bits = 0; bits < (1 << 8); ++bits) {
        TransformMenuState t;
        t.clipValid = bits & 1; t.scaleLocked = bits & 2; t.atDefault = bits & 4; t.posAtDefault = bits & 8; t.rotAtDefault = bits & 16;
        t.scaleAtDefault = bits & 32; t.selectionCount = 1 + (bits >> 6);
        CheckInvariants(TransformMenu(t), "transform sweep");
        CheckInvariants(SubmenuItems("xform.pasteOnly", t), "transform submenu sweep");
        ++menus;
    }
    const ValueKind kinds[] = {ValueKind::None, ValueKind::Bool, ValueKind::Int, ValueKind::Float, ValueKind::String, ValueKind::Vec2,
                               ValueKind::Vec3, ValueKind::Vec4, ValueKind::Quat, ValueKind::Color, ValueKind::Enum, ValueKind::Asset, ValueKind::Object};
    for (ValueKind k : kinds) {
        for (int bits = 0; bits < (1 << 8); ++bits) {
            PropertyMenuState p = Prop(k);
            p.readOnly = bits & 1; p.hasDefault = bits & 2; p.atDefault = bits & 4; p.clipboardFits = bits & 8;
            p.hasRange = bits & 16; p.rangeMin = (bits & 32) ? 2 : 0; p.rangeMax = (bits & 32) ? 9 : 1;
            p.valueEmpty = bits & 64; p.overridden = bits & 128; p.objectResolves = (bits % 3) == 0;
            CheckInvariants(PropertyMenu(p), "property sweep");
            ++menus;
        }
    }
    for (int bits = 0; bits < (1 << 8); ++bits) {
        ObjectMenuState o;
        o.hasParent = bits & 1; o.inPrefab = bits & 2; o.isPrefabRoot = bits & 4; o.prefabMissing = bits & 8; o.hasObjectClip = bits & 16;
        o.clipCanAdd = bits & 32; o.componentCount = (bits & 64) ? 2 : 0; if (bits & 128) o.clipType = "Light";
        o.parentName = "P"; o.prefabName = "X.okayprefab";
        CheckInvariants(ObjectMenu(o), "object sweep");
        CheckInvariants(BackgroundMenu(o), "background sweep");
        ++menus;
    }
    CHECK(menus > 4000);
}

}   // namespace

int main() {
    TestMoveDelta();
    TestComponentMenuOrderAndMoves();
    TestComponentMenuValues();
    TestComponentMenuRemove();
    TestComponentMenuScripts();
    TestTransformMenu();
    TestPropertyMenu();
    TestObjectAndBackgroundMenus();
    TestInvariantSweep();
    TEST_MAIN_RESULT();
}
