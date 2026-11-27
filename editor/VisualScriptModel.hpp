#pragma once
// Visual Script (docs/feature_map/14-visual-scripting.md 14.1, 14.9): the graph editor's model, with no drawing. The node registry (every block
// the editor can place, from the engine's tables), the editable graph (VsEditGraph: blocks, wires, comments, knots, variables;
// the .okayvs text it reads and writes), its OkayScript export, the checker's findings, a graph tab's state (VsEditState: the
// selection, the view, undo and redo) and the open tabs (VsSession). Both views of it draw this one model: the editor's Dear
// ImGui window (VisualScriptEditor.hpp) and the Visual Script app (shared/VisualScriptApp.hpp: the program okay-visual-script
// and the editor's Visual Script window). Moved out of VisualScriptEditor.hpp unchanged, so nothing here needs imgui.h.
// Written for this project.
#include "EditorTabs.hpp"   // okay::edtabs::UserFile: the per-user folder
#include "EditorState.hpp"
#include "okay/Components/VisualScriptComponent.hpp"
#include "okay/Components/ActionList.hpp"
#include "okay/VisualScript/ActionGraph.hpp"
#include "okay/VisualScript/VsTypes.hpp"
#include "okay/VisualScript/VsRefs.hpp"
#include "okay/Scene/SceneSerializer.hpp"
#include "okay/Animation/HumanoidActions.hpp"
#include "okay/Components/AnimControl.hpp"
#include "okay/Components/AnimStateMachine.hpp"
#include "okay/Components/Character.hpp"
#include "okay/Components/ModelAnimator.hpp"
#include "okay/Scene/Layers.hpp"
#include "okay/Scene/SceneManager.hpp"
#include "okay/VisualScript/VsApi.hpp"
#include "okay/VisualScript/VsBuiltins.hpp"
#include "okay/Scene/PropertyRegistry.hpp"
#include "okay/VisualScript/VsDebug.hpp"
#include "VisualScriptNodeHelp.hpp"
#include "ScriptingDensity.hpp"      // the Density choice: one table of sizes for the graph, palette and toolbar
#include "VisualScriptChoices.hpp"
#include "VisualScriptNames.hpp"
#include "VisualScriptCategories.hpp"
#include "VisualScriptLevels.hpp"
#include "VisualScriptTemplates.hpp"
#include "VsCanvasTools.hpp"
#include "VsPlayground.hpp"
#include "VisualScriptNames.hpp"
#include "VisualScriptLevels.hpp"
#include "VisualScriptCategories.hpp"
#include "VisualScriptFriendly.hpp"
#include "VisualScriptPlain.hpp"
#include "okay/VisualScript/QuickBlocks.hpp"
#include "VisualScriptRecipes.hpp"
#include "VisualScriptAutoConnect.hpp"
#include "VisualScriptAdvanced.hpp"
#include "VisualScriptPlainWire.hpp"
#include "VisualScriptPanelWords.hpp"
#include "DocRecovery.hpp"
#include <algorithm>
#include <cstdlib>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include <chrono>
#include <utility>

namespace okay::editor::vsed {

/// A colour as 0xAABBGGRR, the layout both UI toolkits use (Dear ImGui's IM_COL32 and OkayUI::RGBA).
constexpr unsigned VsRgba(unsigned r, unsigned g, unsigned b, unsigned a = 255u) {
    return (r & 0xffu) | ((g & 0xffu) << 8) | ((b & 0xffu) << 16) | ((a & 0xffu) << 24);
}
/// A point on the canvas (screen or stored units; the view state's pan, the palette's drop point).
struct VsPoint {
    float x = 0, y = 0;
    VsPoint() = default;
    constexpr VsPoint(float x_, float y_) : x(x_), y(y_) {}
    /// From any point type with x and y (a UI toolkit's own): `st.pan = ImVec2(...)`.
    template <class V, class = decltype(std::declval<const V&>().y)>
    VsPoint(const V& v) : x(v.x), y(v.y) {}
};
/// Seconds on a steady clock (the choice lists' scan cache).
inline double VsNowSeconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}


// ---- Node registry: describes each node type the editor can place ----------
enum VsCat { CAT_EVENT, CAT_FLOW, CAT_VAR, CAT_MATH, CAT_LOGIC, CAT_INPUT, CAT_XFORM, CAT_ACTION, CAT_DATA, CAT_NET, CAT_API, CAT_SCRIPT, CAT_COUNT };

inline unsigned CatColor(int cat) {
    // Calm, distinct header colours: readable at a glance, not neon.
    switch (cat) {
        case CAT_EVENT:  return VsRgba(158, 96, 102, 255);
        case CAT_FLOW:   return VsRgba(86, 112, 158, 255);
        case CAT_VAR:    return VsRgba(72, 138, 116, 255);
        case CAT_MATH:   return VsRgba(64, 132, 150, 255);
        case CAT_LOGIC:  return VsRgba(124, 104, 158, 255);
        case CAT_INPUT:  return VsRgba(168, 132, 78, 255);
        case CAT_XFORM:  return VsRgba(70, 146, 146, 255);
        case CAT_ACTION: return VsRgba(176, 114, 78, 255);
        case CAT_NET:    return VsRgba(84, 114, 170, 255);
        case CAT_API:    return VsRgba(122, 142, 88, 255);
        case CAT_SCRIPT: return VsRgba(150, 118, 72, 255);
        default:         return VsRgba(108, 114, 128, 255);
    }
}

struct VsDef {
    const char* type;                 // serialization name
    const char* title;                // shown in the node header
    int         cat;
    bool        execIn;
    std::vector<const char*> execOut; // exec output pin labels
    std::vector<const char*> dataIn;  // data input pin labels
    std::vector<const char*> dataOut; // data output pin labels
    int         param;                // 0 none, 1 text field, 2 compare-op combo
    const char* paramHint;
    // How hard this node is. Registry() fills it from VisualScriptLevels.hpp.
    VsLevel     level = VsLevel::Intermediate;
};

// Node types that act on an object get a last "Target" input (empty = this
// object). The engine's table decides which types and at which pin, so the
// editor and the runtime can't disagree.
inline std::vector<VsDef> VsWithTargetPins(std::vector<VsDef> defs) {
    for (VsDef& d : defs) {
        const int tp = okay::vs::TargetPinFor(d.type);
        if (tp >= 0) {
            if (tp == (int)d.dataIn.size()) d.dataIn.push_back("Target");
            else std::fprintf(stderr, "Visual Script: node %s expects its Target at pin %d but has %d inputs\n",
                              d.type, tp, (int)d.dataIn.size());
        }
        // The engine's pin-type table must describe exactly these pins.
        const okay::vs::NodeSignature* sig = okay::vs::SignatureFor(d.type);
        if (!sig || sig->execIn != d.execIn || sig->ins.size() != d.dataIn.size() ||
            (std::string(d.type) != "Const" && sig->outs.size() != d.dataOut.size()))
            std::fprintf(stderr, "Visual Script: node %s doesn't match the pin-type table\n", d.type);
    }
    return defs;
}

// Engine-API nodes, generated from the binding table C#-style scripts use (see
// okay/VisualScript/VsApi.hpp): one per property / method, pins included.
inline std::vector<VsDef> VsWithApiDefs(std::vector<VsDef> defs) {
    for (const okay::vs::VsApiEntry& e : okay::vs::VsApiEntries()) {
        VsDef d{e.type.c_str(), VsFriendlyApiTitle(e), CAT_API, e.exec, {}, {}, {}, 0, nullptr};
        if (e.exec) d.execOut.push_back("");
        VsFillFriendlyPins(e, d.dataIn, d.dataOut);
        defs.push_back(std::move(d));
    }
    return defs;
}
inline bool IsApiNode(const std::string& t) { return okay::vs::VsApiFind(t) != nullptr; }

// One node per text-script builtin, plus the escape hatches. Pointers stay valid:
// BuiltinNodes() keeps its strings for the process lifetime.
inline std::vector<VsDef> VsWithBuiltinDefs(std::vector<VsDef> defs) {
    for (const okay::vs::BuiltinNodeInfo& n : okay::vs::BuiltinNodes()) {
        VsDef d{n.type.c_str(), VsFriendlyBuiltinTitle(n.builtin), CAT_SCRIPT, true, {""},
                {"First", "Second", "Third", "Fourth"}, {"Result"}, 0, nullptr};
        defs.push_back(std::move(d));
    }
    defs.push_back({"GetProperty", "Get Property", CAT_DATA, true, {""}, {"Path","Target"}, {"Value"}, 1, "Component.property"});
    defs.push_back({"SetProperty", "Set Property", CAT_ACTION, true, {""}, {"Value","Path","Target"}, {}, 1, "Component.property"});
    defs.push_back({"CallMethod", "Call Method", CAT_ACTION, true, {""}, {"A","B","C","D","Method"}, {"Result"}, 1, "Type.Method"});
    defs.push_back({"GetComponent", "Get Component", CAT_DATA, true, {""}, {"Target","Type"}, {"Object","Has"}, 1, "component type"});
    defs.push_back({"AddComponent", "Add Component", CAT_ACTION, true, {""}, {"Target","Type"}, {}, 1, "component type"});
    defs.push_back({"FindByTag", "Find By Tag", CAT_DATA, false, {}, {"Tag"}, {"Object"}, 1, "tag"});
    defs.push_back({"SendMessage", "Send Message", CAT_ACTION, true, {""}, {"Message","Target"}, {}, 1, "message name"});
    defs.push_back({"Broadcast", "Broadcast", CAT_ACTION, true, {""}, {"Message"}, {}, 1, "message name"});
    defs.push_back({"RunSnippet", "Run Snippet", CAT_ACTION, true, {""}, {"Code"}, {"Result"}, 1, "a line of text script"});
    return defs;
}

// Titles and renamed pins come from VisualScriptNames.hpp. The strings in the
// list below stay the file token's fallback for nodes that are not renamed yet.
inline std::vector<VsDef> ApplyFriendlyNames(std::vector<VsDef> defs) {
    for (VsDef& d : defs) {
        const VsName* n = FindVsName(d.type);
        if (!n) continue;
        if (n->title && n->title[0]) d.title = n->title;
        auto apply = [](std::vector<const char*>& pins, const char* const* repl, int count) {
            if (!repl) return;
            for (int i = 0; i < count && i < (int)pins.size(); ++i)
                if (repl[i] && repl[i][0]) pins[(std::size_t)i] = repl[i];
        };
        apply(d.dataIn, n->inPins, n->inCount);
        apply(d.dataOut, n->outPins, n->outCount);
        for (const VsExecLabels& e : kVsExecLabels)
            if (std::strcmp(e.type, d.type) == 0) apply(d.execOut, e.pins, e.count);
    }
    return defs;
}

// Quick blocks (okay/VisualScript/QuickBlocks.hpp): one node each, the typed fields are its data inputs.
inline std::vector<VsDef> VsWithQuickBlockDefs(std::vector<VsDef> defs) {
    for (const okay::vs::QuickBlock& b : okay::vs::QuickBlocks()) {
        VsDef d{b.type, b.title, CAT_ACTION, b.flowIn, {}, {}, {}, 0, nullptr};
        if (b.thenOut) d.execOut.push_back("Then do");
        for (const okay::vs::QbField& f : okay::vs::QbAllFields(b)) d.dataIn.push_back(f.name);
        d.level = VsLevel::Basic;
        defs.push_back(d);
    }
    return defs;
}

// Plain words (VisualScriptPlain.hpp): a hand-picked name, else the banned words swapped for everyday ones.
inline std::vector<VsDef> ApplyPlainTitles(std::vector<VsDef> defs) {
    for (VsDef& d : defs) {
        if (const char* t = VsPlainTitleFor(d.type)) { d.title = t; continue; }
        if (d.title && !VsGlossaryViolation(d.title).empty()) d.title = VsIntern(VsPlainText(d.title, true));
    }
    return defs;
}

// Levels come from VisualScriptLevels.hpp, not from the big list below, so a
// new node (including one added on another branch) gets a level by name.
inline std::vector<VsDef> ApplyLevels(std::vector<VsDef> defs) {
    for (VsDef& d : defs) {
        const okay::vs::VsApiEntry* api = okay::vs::VsApiFind(d.type);
        d.level = api ? VsLevelForApi(api->apiType.c_str(), api->member.c_str())
                      : VsLevelForNode(d.type);
    }
    return defs;
}

// A curated, genuinely-useful set of nodes (all supported by the engine runtime),
// then the generated engine-API nodes.
inline const std::vector<VsDef>& Registry() {
    static const std::vector<VsDef> defs = ApplyLevels(ApplyPlainTitles(ApplyFriendlyNames(VsWithQuickBlockDefs(VsWithBuiltinDefs(VsWithApiDefs(VsWithTargetPins({
        // Events
        {"OnStart",  "On Start",  CAT_EVENT, false, {""}, {}, {}, 0, nullptr},
        {"OnUpdate", "On Update", CAT_EVENT, false, {""}, {}, {}, 0, nullptr},
        // Flow
        {"Branch",   "Branch",    CAT_FLOW, true, {"True","False"}, {"Cond"}, {}, 0, nullptr},
        {"Sequence", "Sequence",  CAT_FLOW, true, {"0","1"}, {}, {}, 0, nullptr},
        {"Once",     "Once",      CAT_FLOW, true, {""}, {"Reset"}, {}, 0, nullptr},
        {"Timer",    "Every (s)", CAT_FLOW, true, {""}, {}, {}, 1, "seconds"},
        // Variables
        {"GetVar",   "Get Var",   CAT_VAR, false, {}, {}, {"Value"}, 1, "name"},
        {"SetVar",   "Set Var",   CAT_VAR, true, {""}, {"Value"}, {}, 1, "name"},
        {"AddVar",   "Add Var",   CAT_VAR, true, {""}, {"Amount"}, {}, 1, "name"},
        {"Const",    "Const",     CAT_DATA, false, {}, {}, {"Value"}, 1, "value"},
        // Math
        {"Add", "Add (+)", CAT_MATH, false, {}, {"A","B"}, {"Out"}, 0, nullptr},
        {"Sub", "Sub (-)", CAT_MATH, false, {}, {"A","B"}, {"Out"}, 0, nullptr},
        {"Mul", "Mul (x)", CAT_MATH, false, {}, {"A","B"}, {"Out"}, 0, nullptr},
        {"Div", "Div (/)", CAT_MATH, false, {}, {"A","B"}, {"Out"}, 0, nullptr},
        {"Min", "Min",     CAT_MATH, false, {}, {"A","B"}, {"Out"}, 0, nullptr},
        {"Max", "Max",     CAT_MATH, false, {}, {"A","B"}, {"Out"}, 0, nullptr},
        {"Compare", "Compare", CAT_LOGIC, false, {}, {"A","B"}, {"Bool"}, 2, nullptr},
        // Logic
        {"And", "And", CAT_LOGIC, false, {}, {"A","B"}, {"Out"}, 0, nullptr},
        {"Or",  "Or",  CAT_LOGIC, false, {}, {"A","B"}, {"Out"}, 0, nullptr},
        {"Not", "Not", CAT_LOGIC, false, {}, {"In"}, {"Out"}, 0, nullptr},
        // Input
        {"Key",     "Key Held",  CAT_INPUT, false, {}, {}, {"Down"}, 1, "key"},
        {"KeyDown", "Key Down",  CAT_INPUT, false, {}, {}, {"Pressed"}, 1, "key"},
        {"AxisX",   "Axis X",    CAT_INPUT, false, {}, {}, {"X"}, 0, nullptr},
        {"AxisY",   "Axis Y",    CAT_INPUT, false, {}, {}, {"Y"}, 0, nullptr},
        {"Time",      "Time",      CAT_INPUT, false, {}, {}, {"t"}, 0, nullptr},
        {"DeltaTime", "Delta Time",CAT_INPUT, false, {}, {}, {"dt"}, 0, nullptr},
        // Transform
        {"GetX", "Get X", CAT_XFORM, false, {}, {}, {"X"}, 0, nullptr},
        {"GetY", "Get Y", CAT_XFORM, false, {}, {}, {"Y"}, 0, nullptr},
        {"Translate",   "Translate",   CAT_XFORM, true, {""}, {"X","Y","Z"}, {}, 0, nullptr},
        {"SetPosition", "Set Position", CAT_XFORM, true, {""}, {"X","Y","Z"}, {}, 0, nullptr},
        {"Rotate",      "Rotate",      CAT_XFORM, true, {""}, {"Deg"}, {}, 0, nullptr},
        // Actions
        {"Print",     "Print",     CAT_ACTION, true, {""}, {"Text", "Value"}, {}, 0, nullptr},
        {"PlaySound", "Play Sound",CAT_ACTION, true, {""}, {}, {}, 0, nullptr},
        {"Spawn",     "Spawn",     CAT_ACTION, true, {""}, {"X","Y","Z"}, {"Object"}, 1, "prefab"},
        {"Destroy",   "Destroy",   CAT_ACTION, true, {""}, {"Target"}, {}, 0, nullptr},
        // --- More math (data) ---
        {"Mod",   "Mod (%)",  CAT_MATH, false, {}, {"A","B"}, {"Out"}, 0, nullptr},
        {"Pow",   "Pow (^)",  CAT_MATH, false, {}, {"A","B"}, {"Out"}, 0, nullptr},
        {"Abs",   "Abs",      CAT_MATH, false, {}, {"In"}, {"Out"}, 0, nullptr},
        {"Neg",   "Negate",   CAT_MATH, false, {}, {"In"}, {"Out"}, 0, nullptr},
        {"Sqrt",  "Sqrt",     CAT_MATH, false, {}, {"In"}, {"Out"}, 0, nullptr},
        {"Sin",   "Sin",      CAT_MATH, false, {}, {"In"}, {"Out"}, 0, nullptr},
        {"Cos",   "Cos",      CAT_MATH, false, {}, {"In"}, {"Out"}, 0, nullptr},
        {"Floor", "Floor",    CAT_MATH, false, {}, {"In"}, {"Out"}, 0, nullptr},
        {"Round", "Round",    CAT_MATH, false, {}, {"In"}, {"Out"}, 0, nullptr},
        {"Sign",  "Sign",     CAT_MATH, false, {}, {"In"}, {"Out"}, 0, nullptr},
        {"Clamp", "Clamp",    CAT_MATH, false, {}, {"Value","Min","Max"}, {"Out"}, 0, nullptr},
        {"Lerp",  "Lerp",     CAT_MATH, false, {}, {"A","B","T"}, {"Out"}, 0, nullptr},
        {"Random","Random 0-1", CAT_MATH, false, {}, {}, {"Out"}, 0, nullptr},
        {"RandomRange","Random Range", CAT_MATH, false, {}, {"Min","Max"}, {"Out"}, 0, nullptr},
        // --- More logic ---
        {"Xor",       "Xor",        CAT_LOGIC, false, {}, {"A","B"}, {"Out"}, 0, nullptr},
        {"Equals",    "Equals",     CAT_LOGIC, false, {}, {"A","B"}, {"Bool"}, 0, nullptr},
        {"NotEquals", "Not Equals", CAT_LOGIC, false, {}, {"A","B"}, {"Bool"}, 0, nullptr},
        // --- More input ---
        {"KeyUp",  "Key Up",  CAT_INPUT, false, {}, {}, {"Released"}, 1, "key"},
        {"MouseX", "Mouse X", CAT_INPUT, false, {}, {}, {"X"}, 0, nullptr},
        {"MouseY", "Mouse Y", CAT_INPUT, false, {}, {}, {"Y"}, 0, nullptr},
        // --- Vectors / transform getters ---
        {"MakeVec3",    "Make Vec3",    CAT_DATA,  false, {}, {"X","Y","Z"}, {"Vec3"}, 0, nullptr},
        {"GetPosition", "Get Position", CAT_XFORM, false, {}, {}, {"Pos"}, 0, nullptr},
        {"GetZ",        "Get Z",        CAT_XFORM, false, {}, {}, {"Z"}, 0, nullptr},
        // --- More transform (exec) ---
        {"SetScale",    "Set Scale",    CAT_XFORM, true, {""}, {"X","Y","Z"}, {}, 0, nullptr},
        {"SetRotation", "Set Rotation", CAT_XFORM, true, {""}, {"Deg"}, {}, 0, nullptr},
        // --- More flow / actions ---
        {"Wait",     "Wait (s)",    CAT_FLOW,   true, {""}, {}, {}, 1, "seconds"},
        {"LogWarn",  "Log Warning", CAT_ACTION, true, {""}, {"Value"}, {}, 0, nullptr},
        {"LogError", "Log Error",   CAT_ACTION, true, {""}, {"Value"}, {}, 0, nullptr},
        {"AddForce", "Add Force",   CAT_ACTION, true, {""}, {"X","Y","Z"}, {}, 0, nullptr},
        {"SetVelocity","Set Velocity",CAT_ACTION, true, {""}, {"X","Y","Z"}, {}, 0, nullptr},
        {"AddImpulse","Add Impulse", CAT_ACTION, true, {""}, {"X","Y","Z"}, {}, 0, nullptr},
        // --- Logic / selection ---
        {"Approx",  "Approx Equal", CAT_LOGIC, false, {}, {"A","B"}, {"Bool"}, 0, nullptr},
        {"Between", "Between",      CAT_LOGIC, false, {}, {"Value","Min","Max"}, {"Bool"}, 0, nullptr},
        {"Chance",  "Chance",       CAT_LOGIC, false, {}, {}, {"Bool"}, 1, "0..1"},
        {"HasVar",  "Has Var",      CAT_LOGIC, false, {}, {}, {"Bool"}, 1, "name"},
        {"Select",  "Select",       CAT_LOGIC, false, {}, {"Cond","A","B"}, {"Out"}, 0, nullptr},
        {"Concat",  "Concat (text)",CAT_DATA,  false, {}, {"A","B"}, {"Out"}, 0, nullptr},
        // --- Vectors ---
        {"VecX", "Vec X", CAT_MATH, false, {}, {"Vec"}, {"X"}, 0, nullptr},
        {"VecY", "Vec Y", CAT_MATH, false, {}, {"Vec"}, {"Y"}, 0, nullptr},
        {"VecZ", "Vec Z", CAT_MATH, false, {}, {"Vec"}, {"Z"}, 0, nullptr},
        // --- Physics reads + tween ---
        {"VelX", "Velocity X", CAT_INPUT, false, {}, {}, {"X"}, 0, nullptr},
        {"VelY", "Velocity Y", CAT_INPUT, false, {}, {}, {"Y"}, 0, nullptr},
        {"VelZ", "Velocity Z", CAT_INPUT, false, {}, {}, {"Z"}, 0, nullptr},
        {"EaseIn",    "Ease In",     CAT_MATH, false, {}, {"T"}, {"Out"}, 0, nullptr},
        {"EaseOut",   "Ease Out",    CAT_MATH, false, {}, {"T"}, {"Out"}, 0, nullptr},
        {"EaseInOut", "Ease In-Out", CAT_MATH, false, {}, {"T"}, {"Out"}, 0, nullptr},
        // --- More events ---
        {"OnCollision", "On Collision", CAT_EVENT, false, {""}, {}, {}, 0, nullptr},
        {"OnTrigger",   "On Trigger",   CAT_EVENT, false, {""}, {}, {}, 0, nullptr},
        {"OnClick",     "On Click",     CAT_EVENT, false, {""}, {}, {}, 0, nullptr},
        {"OnAnimEvent", "On Anim Event", CAT_EVENT, false, {""}, {}, {}, 1, "event (blank = any)"},
        {"AnimEventName", "Anim Event Name", CAT_DATA, false, {}, {}, {"Name"}, 0, nullptr},
        {"OtherName",   "Other Name",   CAT_DATA,  false, {}, {}, {"Name"}, 0, nullptr},
        {"OtherTag",    "Other Tag",    CAT_DATA,  false, {}, {}, {"Tag"}, 0, nullptr},
        // --- More math ---
        {"Tan",        "Tan",         CAT_MATH, false, {}, {"In"}, {"Out"}, 0, nullptr},
        {"Atan2",      "Atan2",       CAT_MATH, false, {}, {"Y","X"}, {"Rad"}, 0, nullptr},
        {"Exp",        "Exp",         CAT_MATH, false, {}, {"In"}, {"Out"}, 0, nullptr},
        {"Log",        "Log (ln)",    CAT_MATH, false, {}, {"In"}, {"Out"}, 0, nullptr},
        {"InverseLerp","Inverse Lerp",CAT_MATH, false, {}, {"A","B","Value"}, {"T"}, 0, nullptr},
        {"Remap",      "Remap",       CAT_MATH, false, {}, {"Value","In Min","In Max","Out Min","Out Max"}, {"Out"}, 0, nullptr},
        {"SmoothStep", "Smooth Step", CAT_MATH, false, {}, {"Edge0","Edge1","X"}, {"Out"}, 0, nullptr},
        {"PingPong",   "Ping-Pong",   CAT_MATH, false, {}, {"T","Length"}, {"Out"}, 0, nullptr},
        {"Repeat01",   "Repeat (wrap)",CAT_MATH, false, {}, {"T","Length"}, {"Out"}, 0, nullptr},
        {"MoveTowardsValue","Move Towards (value)", CAT_MATH, false, {}, {"Current","Target","Max Step"}, {"Out"}, 0, nullptr},
        {"DeltaAngle", "Delta Angle", CAT_MATH, false, {}, {"From","To"}, {"Deg"}, 0, nullptr},
        // --- Vectors ---
        {"Vec3Add",       "Vec3 Add",       CAT_MATH, false, {}, {"A","B"}, {"Vec3"}, 0, nullptr},
        {"Vec3Sub",       "Vec3 Subtract",  CAT_MATH, false, {}, {"A","B"}, {"Vec3"}, 0, nullptr},
        {"Vec3Scale",     "Vec3 Scale",     CAT_MATH, false, {}, {"Vec","Scale"}, {"Vec3"}, 0, nullptr},
        {"Vec3Length",    "Vec3 Length",    CAT_MATH, false, {}, {"Vec"}, {"Length"}, 0, nullptr},
        {"Vec3Normalize", "Vec3 Normalize", CAT_MATH, false, {}, {"Vec"}, {"Vec3"}, 0, nullptr},
        {"Vec3Dot",       "Vec3 Dot",       CAT_MATH, false, {}, {"A","B"}, {"Dot"}, 0, nullptr},
        {"Vec3Distance",  "Vec3 Distance",  CAT_MATH, false, {}, {"A","B"}, {"Dist"}, 0, nullptr},
        // --- Text ---
        {"ToString",   "To Text",     CAT_DATA, false, {}, {"Value"}, {"Text"}, 0, nullptr},
        {"ToFixed",    "Format Number",CAT_DATA, false, {}, {"Value"}, {"Text"}, 1, "decimals"},
        {"TextLength", "Text Length", CAT_DATA, false, {}, {"Text"}, {"Len"}, 0, nullptr},
        {"Contains",   "Contains",    CAT_LOGIC, false, {}, {"Text","Part"}, {"Bool"}, 0, nullptr},
        {"Upper",      "Upper Case",  CAT_DATA, false, {}, {"Text"}, {"Text"}, 0, nullptr},
        {"Lower",      "Lower Case",  CAT_DATA, false, {}, {"Text"}, {"Text"}, 0, nullptr},
        // --- Objects ---
        {"ObjectExists",  "Object Exists",  CAT_XFORM, false, {}, {"Object"}, {"Bool"}, 1, "name"},
        {"ObjectPosition","Object Position",CAT_XFORM, false, {}, {"Object"}, {"Pos"}, 1, "name / self / other"},
        {"DistanceTo",    "Distance To",    CAT_XFORM, false, {}, {"Object"}, {"Dist"}, 1, "name / other"},
        {"SelfName",      "Self Name",      CAT_XFORM, false, {}, {}, {"Name"}, 0, nullptr},
        {"LookAt",        "Look At",        CAT_XFORM, true, {""}, {"Object"}, {}, 1, "name / other"},
        {"MoveTowards",   "Move Towards",   CAT_XFORM, true, {""}, {"Speed","Object"}, {}, 1, "name / other"},
        {"SetActive",     "Set Active",     CAT_ACTION, true, {""}, {"On","Object"}, {}, 1, "name / self / other"},
        {"SetText",       "Set Text",       CAT_ACTION, true, {""}, {"Text","Object"}, {}, 1, "text object name"},
        {"LoadScene",     "Load Scene",     CAT_ACTION, true, {""}, {}, {}, 1, "scene name"},
        // --- Input / flow ---
        {"MouseButton",     "Mouse Held",    CAT_INPUT, false, {}, {}, {"Down"}, 1, "0 L / 1 R / 2 M"},
        {"MouseButtonDown", "Mouse Pressed", CAT_INPUT, false, {}, {}, {"Pressed"}, 1, "0 L / 1 R / 2 M"},
        {"MouseGround",     "Mouse Ground",  CAT_INPUT, false, {}, {}, {"X", "Y", "Z", "Hit"}, 0, nullptr},
        {"Cooldown",        "Cooldown (s)",  CAT_FLOW,  true, {""}, {}, {}, 1, "seconds"},
        // --- Animation & IK ---
        {"AnimTrigger",  "Anim Trigger",   CAT_ACTION, true, {""}, {}, {}, 1, "parameter"},
        {"AnimSetFloat", "Anim Set Float", CAT_ACTION, true, {""}, {"Value"}, {}, 1, "parameter"},
        {"AnimSetBool",  "Anim Set Bool",  CAT_ACTION, true, {""}, {"Value"}, {}, 1, "parameter"},
        {"AnimState",    "Anim State",     CAT_DATA, false, {}, {}, {"State"}, 0, nullptr},
        {"IkLook",       "IK Look At",     CAT_ACTION, true, {""}, {"Point","Weight"}, {}, 0, nullptr},
        {"IkHand",       "IK Hand Reach",  CAT_ACTION, true, {""}, {"Point","Weight"}, {}, 1, "left / right"},
        // --- Look: weather, fog, post-processing (docs/rendering/rendering.md) ---
        {"SetWeather",    "Set Weather",     CAT_ACTION, true, {""}, {"Intensity"}, {}, 1, "none / rain / snow / storm"},
        {"SetFog",        "Set Fog",         CAT_ACTION, true, {""}, {"On","Start","End"}, {}, 0, nullptr},
        {"SetPostEffect", "Set Post Effect", CAT_ACTION, true, {""}, {"Value"}, {}, 1, "bloom / exposure / saturation / ..."},
        // --- Navigation (Nav Mesh Agent) ---
        {"NavMoveTo",    "Nav Move To",    CAT_ACTION, true, {""}, {"Point"}, {}, 0, nullptr},
        {"NavStop",      "Nav Stop",       CAT_ACTION, true, {""}, {}, {}, 0, nullptr},
        {"NavArrived",   "Nav Arrived",    CAT_DATA, false, {}, {}, {"Bool"}, 0, nullptr},
        {"GridMoveTo",   "Walk the Grid",  CAT_ACTION, true, {""}, {"X","Y"}, {}, 0, nullptr},
        {"GridStop",     "Stop Grid Walk", CAT_ACTION, true, {""}, {}, {}, 0, nullptr},
        {"GridArrived",  "Grid Arrived",   CAT_DATA, false, {}, {}, {"Arrived"}, 0, nullptr},
        // --- Behavior trees (Behavior Tree Runner) ---
        {"BtSet",        "Set Blackboard", CAT_ACTION, true, {""}, {"Value"}, {}, 1, "key"},
        {"BtGet",        "Get Blackboard", CAT_DATA, false, {}, {}, {"Value"}, 1, "key"},
        {"BtRun",        "Run Tree",       CAT_ACTION, true, {""}, {}, {}, 1, "tree file"},
        // --- Splines, lines and trails (the component on this object or the Target) ---
        {"GetSplinePoint", "Get Point On Spline", CAT_DATA, false, {}, {"T (0-1)"}, {"Point"}, 0, nullptr},
        {"SplineLength",   "Spline Length",       CAT_DATA, false, {}, {}, {"Length"}, 0, nullptr},
        {"SetLinePoints",  "Set Line Points",     CAT_ACTION, true, {""}, {"Points"}, {}, 0, nullptr},
        {"ClearTrail",     "Clear Trail",         CAT_ACTION, true, {""}, {}, {}, 0, nullptr},
        // --- Music, camera, particles ---
        {"MusicPlay",      "Play Music",          CAT_ACTION, true, {""}, {}, {}, 1, "track (file or number)"},
        {"MusicStop",      "Stop Music",          CAT_ACTION, true, {""}, {"Fade (s)"}, {}, 0, nullptr},
        {"MusicPause",     "Pause Music",         CAT_ACTION, true, {""}, {}, {}, 0, nullptr},
        {"MusicResume",    "Resume Music",        CAT_ACTION, true, {""}, {}, {}, 0, nullptr},
        {"MusicNext",      "Next Music Track",    CAT_ACTION, true, {""}, {}, {}, 0, nullptr},
        {"CameraShake",    "Camera Shake",        CAT_ACTION, true, {""}, {"Strength"}, {}, 0, nullptr},
        {"SetFov",         "Set Camera FOV",      CAT_ACTION, true, {""}, {"Degrees"}, {}, 0, nullptr},
        {"CameraFollow",   "Camera Follow",       CAT_ACTION, true, {""}, {"Object name","Smoothing"}, {}, 0, nullptr},
        {"ParticlePlay",   "Play Particles",      CAT_ACTION, true, {""}, {}, {}, 0, nullptr},
        {"ParticleStop",   "Stop Particles",      CAT_ACTION, true, {""}, {}, {}, 0, nullptr},
        {"ParticleBurst",  "Burst Particles",     CAT_ACTION, true, {""}, {"Count"}, {}, 0, nullptr},
        // High-level gameplay. Ready-made actions; the low-level nodes above stay as they are.
        {"RotateTowards", "Rotate Towards", CAT_XFORM, true, {""}, {"Speed","Target"}, {}, 0, nullptr},
        {"OrbitAround", "Orbit Around", CAT_XFORM, true, {""}, {"Centre","Radius","Speed"}, {}, 0, nullptr},
        {"FollowPath", "Follow Path", CAT_XFORM, true, {"Moving","Done"}, {"Speed","Points"}, {}, 1, "x,y,z; x,y,z"},
        {"Bob", "Bob", CAT_XFORM, true, {""}, {"Height","Speed"}, {}, 0, nullptr},
        {"Spin", "Spin", CAT_XFORM, true, {""}, {"Speed"}, {}, 0, nullptr},
        {"Knockback", "Knockback", CAT_XFORM, true, {""}, {"From","Force"}, {}, 0, nullptr},
        {"SmoothFollow", "Smooth Follow", CAT_XFORM, true, {""}, {"Target","Smoothing"}, {}, 0, nullptr},
        {"TweenPosition", "Tween Position", CAT_XFORM, true, {""}, {"To","Seconds"}, {}, 1, "linear, in, out, inout"},
        {"TweenRotation", "Tween Rotation", CAT_XFORM, true, {""}, {"Yaw","Seconds"}, {}, 1, "linear, in, out, inout"},
        {"TweenScale", "Tween Scale", CAT_XFORM, true, {""}, {"To","Seconds"}, {}, 1, "linear, in, out, inout"},
        {"TweenColor", "Tween Color", CAT_XFORM, true, {""}, {"R","G","B","A","Seconds"}, {}, 1, "linear, in, out, inout"},
        {"CrossfadeClip", "Crossfade Animation", CAT_ACTION, true, {""}, {"Blend"}, {}, 1, "clip name"},
        {"StopAnimation", "Stop Animation", CAT_ACTION, true, {""}, {}, {}, 0, nullptr},
        {"WaitAnimEnd", "Wait For Animation End", CAT_FLOW, true, {""}, {}, {}, 0, nullptr},
        {"OnInteract", "On Interact", CAT_EVENT, false, {""}, {}, {}, 0, nullptr},
        {"PickUp", "Pick Up", CAT_ACTION, true, {""}, {"Item"}, {}, 0, nullptr},
        {"Drop", "Drop", CAT_ACTION, true, {""}, {"Item"}, {}, 0, nullptr},
        {"DoorOpen", "Door Open", CAT_ACTION, true, {""}, {"Degrees","Seconds"}, {}, 0, nullptr},
        {"DoorClose", "Door Close", CAT_ACTION, true, {""}, {"Seconds"}, {}, 0, nullptr},
        {"Lever", "Lever", CAT_ACTION, true, {"On","Off"}, {}, {}, 0, nullptr},
        {"Collect", "Collect", CAT_ACTION, true, {""}, {"Amount"}, {}, 0, nullptr},
        {"OnTagEnter", "On Tag Enter", CAT_EVENT, false, {""}, {}, {}, 1, "tag"},
        {"OnTagStay", "On Tag Stay", CAT_EVENT, false, {""}, {}, {}, 1, "tag"},
        {"OnTagExit", "On Tag Exit", CAT_EVENT, false, {""}, {}, {}, 1, "tag"},
        {"Proximity", "Proximity", CAT_LOGIC, false, {}, {"Target","Distance"}, {"Near"}, 0, nullptr},
        {"Damage", "Damage", CAT_ACTION, true, {""}, {"Amount","Target","Type"}, {}, 0, nullptr},
        {"Heal", "Heal", CAT_ACTION, true, {""}, {"Amount","Target"}, {}, 0, nullptr},
        {"IsDead", "Is Dead", CAT_LOGIC, false, {}, {"Target"}, {"Dead"}, 0, nullptr},
        {"OnDeath", "On Death", CAT_EVENT, false, {""}, {}, {}, 0, nullptr},
        {"OnHealthChanged", "On Health Changed", CAT_EVENT, false, {""}, {}, {}, 0, nullptr},
        {"OnDamaged", "On Damaged", CAT_EVENT, false, {""}, {}, {"Amount","Type","Source"}, 0, nullptr},
        {"OnButtonClicked", "On Button Clicked", CAT_EVENT, false, {""}, {}, {}, 1, "button object name"},
        {"Respawn", "Respawn", CAT_ACTION, true, {""}, {"Target"}, {}, 0, nullptr},
        {"SpawnProjectile", "Spawn Projectile", CAT_ACTION, true, {""}, {"Speed"}, {"Object"}, 1, "prefab"},
        {"Shoot", "Shoot", CAT_ACTION, true, {""}, {"Max Dist","Damage"}, {"Hit","Object"}, 0, nullptr},
        {"Ammo", "Ammo", CAT_ACTION, true, {"Fired","Empty"}, {}, {"Left"}, 1, "shots"},
        {"Patrol", "Patrol Waypoints", CAT_ACTION, true, {""}, {"Speed","Points"}, {}, 1, "x,y,z; x,y,z"},
        {"Chase", "Chase Target", CAT_ACTION, true, {""}, {"Target","Speed"}, {}, 0, nullptr},
        {"Flee", "Flee", CAT_ACTION, true, {""}, {"Threat","Speed"}, {}, 0, nullptr},
        {"Wander", "Wander", CAT_ACTION, true, {""}, {"Radius","Speed"}, {}, 0, nullptr},
        {"CanSee", "Can See Target", CAT_LOGIC, false, {}, {"Target","Distance","Cone"}, {"Seen"}, 0, nullptr},
        {"SpawnEvery", "Spawn Every", CAT_ACTION, true, {""}, {"Seconds"}, {"Object"}, 1, "prefab"},
        {"DestroyAfter", "Destroy After", CAT_ACTION, true, {""}, {"Seconds","Target"}, {}, 0, nullptr},
        {"PoolGet", "Pool Get", CAT_ACTION, true, {""}, {}, {"Object"}, 1, "prefab"},
        {"PoolRelease", "Pool Release", CAT_ACTION, true, {""}, {"Item"}, {}, 1, "pool name"},
        {"ScoreAdd", "Score Add", CAT_VAR, true, {""}, {"Amount"}, {}, 0, nullptr},
        {"ScoreSet", "Score Set", CAT_VAR, true, {""}, {"Value"}, {}, 0, nullptr},
        {"ScoreGet", "Score Get", CAT_VAR, false, {}, {}, {"Score"}, 0, nullptr},
        {"Countdown", "Countdown", CAT_FLOW, true, {""}, {"Seconds"}, {}, 1, "seconds"},
        {"Win", "Win", CAT_ACTION, true, {""}, {}, {}, 0, nullptr},
        {"Lose", "Lose", CAT_ACTION, true, {""}, {}, {}, 0, nullptr},
        {"RestartScene", "Restart Scene", CAT_ACTION, true, {""}, {}, {}, 0, nullptr},
        {"CheckpointSave", "Checkpoint Save", CAT_ACTION, true, {""}, {}, {}, 0, nullptr},
        {"CheckpointLoad", "Checkpoint Load", CAT_ACTION, true, {"Loaded","Missing"}, {}, {}, 0, nullptr},
        {"SetProgress", "Set Progress Bar", CAT_ACTION, true, {""}, {"Value","Target"}, {}, 0, nullptr},
        {"ShowFloatingText", "Show Floating Text", CAT_ACTION, true, {""}, {"Text","Target"}, {}, 0, nullptr},
        {"FadeScreen", "Fade Screen", CAT_ACTION, true, {""}, {"Amount","Panel"}, {}, 0, nullptr},
        {"SetCameraTarget", "Set Camera Target", CAT_ACTION, true, {""}, {"Target","Smoothing"}, {}, 0, nullptr},
        {"Zoom", "Zoom", CAT_ACTION, true, {""}, {"Delta"}, {}, 0, nullptr},
        {"SwitchCamera", "Switch Camera", CAT_ACTION, true, {""}, {}, {}, 1, "camera name"},
        {"PlaySoundAt", "Play Sound At", CAT_ACTION, true, {""}, {"Source"}, {}, 0, nullptr},
        {"StopSound", "Stop Sound", CAT_ACTION, true, {""}, {"Source"}, {}, 0, nullptr},
        {"OnAction", "On Action Pressed", CAT_EVENT, false, {""}, {}, {}, 1, "interact, jump, fire"},
        {"OnActionUp", "On Action Released", CAT_EVENT, false, {""}, {}, {}, 1, "interact, jump, fire"},
        {"OnActionHeld", "On Action Held", CAT_EVENT, false, {""}, {}, {}, 1, "interact, jump, fire"},
        {"OnTap", "On Tap", CAT_EVENT, false, {""}, {}, {}, 0, nullptr},
        {"OnDoubleTap", "On Double Tap", CAT_EVENT, false, {""}, {}, {}, 0, nullptr},
        {"OnLongPress", "On Long Press", CAT_EVENT, false, {""}, {}, {}, 0, nullptr},
        {"OnSwipe", "On Swipe", CAT_EVENT, false, {""}, {}, {}, 1, "up, down, left, right, or blank for any"},
        {"OnPinch", "On Pinch", CAT_EVENT, false, {""}, {}, {}, 0, nullptr},
        {"OnRotate", "On Two Finger Rotate", CAT_EVENT, false, {""}, {}, {}, 0, nullptr},
        {"GestureSwipeDirection", "Get Swipe Direction", CAT_INPUT, false, {}, {}, {"Direction"}, 0, nullptr},
        {"GesturePinchScale", "Get Pinch Scale", CAT_INPUT, false, {}, {}, {"Scale"}, 0, nullptr},
        {"GestureTwist", "Get Twist Angle", CAT_INPUT, false, {}, {}, {"Degrees"}, 0, nullptr},
        {"GetMoveAxis", "Get Move Axis", CAT_INPUT, false, {}, {}, {"Move"}, 0, nullptr},
        {"RandomPick", "Random Pick", CAT_MATH, false, {}, {"List"}, {"Item"}, 0, nullptr},
        {"RandomPoint", "Random Point In Area", CAT_MATH, false, {}, {"Center","Size"}, {"Point"}, 0, nullptr},
        {"ShuffleList", "Shuffle", CAT_MATH, false, {}, {"List"}, {"List"}, 0, nullptr},
        {"ListMin", "Min Of List", CAT_MATH, false, {}, {"List"}, {"Min"}, 0, nullptr},
        {"ListMax", "Max Of List", CAT_MATH, false, {}, {"List"}, {"Max"}, 0, nullptr},
        {"OnCollisionTag", "On Collision With Tag", CAT_EVENT, false, {""}, {}, {}, 1, "tag"},
        // Logic and flow round. File tokens stay short; titles come from the name table.
        {"NotAnd", "Not and", CAT_LOGIC, false, {}, {"First","Second"}, {"Result"}, 0, nullptr},
        {"NotOr", "Not or", CAT_LOGIC, false, {}, {"First","Second"}, {"Result"}, 0, nullptr},
        {"AllTrue", "All True", CAT_LOGIC, false, {}, {"List"}, {"Result"}, 0, nullptr},
        {"AnyTrue", "Any True", CAT_LOGIC, false, {}, {"List"}, {"Result"}, 0, nullptr},
        {"IsEven", "Is Even", CAT_LOGIC, false, {}, {"Value"}, {"Result"}, 0, nullptr},
        {"IsOdd", "Is Odd", CAT_LOGIC, false, {}, {"Value"}, {"Result"}, 0, nullptr},
        {"PickByIndex", "Pick by Index", CAT_LOGIC, false, {}, {"Index","List"}, {"Item"}, 0, nullptr},
        {"FirstValid", "First Valid", CAT_LOGIC, false, {}, {"First","Second","Third","Fourth"}, {"Value"}, 0, nullptr},
        {"BranchInOrder", "Branch in Order", CAT_FLOW, true, {"First","Second","Third","Otherwise"}, {"First","Second","Third"}, {}, 0, nullptr},
        {"DoWhile", "Repeat While", CAT_FLOW, true, {"Body","Done"}, {"Condition"}, {}, 0, nullptr},
        {"ForStep", "Count With Step", CAT_FLOW, true, {"Body","Done"}, {"First","Last","Step"}, {"Index"}, 0, nullptr},
        {"WaitUntil", "Wait Until", CAT_FLOW, true, {""}, {"Condition"}, {}, 0, nullptr},
        {"WaitWhile", "Wait While", CAT_FLOW, true, {""}, {"Condition"}, {}, 0, nullptr},
        {"BecameTrue", "Became True", CAT_FLOW, true, {""}, {"Condition"}, {}, 0, nullptr},
        {"BecameFalse", "Became False", CAT_FLOW, true, {""}, {"Condition"}, {}, 0, nullptr},
        {"CountUntil", "Count Until", CAT_FLOW, true, {"Each","Reached"}, {"Target","Reset"}, {"Count"}, 0, nullptr},
        {"WeightedBranch", "Weighted Branch", CAT_FLOW, true, {"First","Second"}, {"First Weight","Second Weight"}, {}, 0, nullptr},
        {"WaitSteady", "Wait Until Steady", CAT_FLOW, true, {""}, {"Condition","Seconds"}, {}, 0, nullptr},
        {"StepThrough", "Step Through", CAT_FLOW, true, {"First","Second","Third"}, {}, {}, 0, nullptr},
        {"WaitLimit", "Wait With Limit", CAT_FLOW, true, {"Ready","Timed Out"}, {"Condition","Seconds"}, {}, 0, nullptr},
        {"SetState", "Set State", CAT_FLOW, true, {""}, {"Name"}, {}, 0, nullptr},
        {"GetState", "Get State", CAT_DATA, false, {}, {}, {"State"}, 0, nullptr},
        {"IsInState", "Is in State", CAT_LOGIC, false, {}, {"Name"}, {"Result"}, 0, nullptr},
        {"WhenStateBecomes", "When State Becomes", CAT_FLOW, true, {""}, {"Name"}, {}, 0, nullptr},
        {"WhenStateLeaves", "When State Leaves", CAT_FLOW, true, {""}, {"Name"}, {}, 0, nullptr},
        {"HeldLong", "Has Held Long Enough", CAT_LOGIC, false, {}, {"Condition","Seconds"}, {"Result"}, 0, nullptr},
        {"HappenedNear", "Has Happened Within", CAT_LOGIC, false, {}, {"First","Second","Seconds"}, {"Result"}, 0, nullptr},
        {"EveryWhile", "Every Few Seconds", CAT_FLOW, true, {""}, {"Condition","Seconds"}, {}, 0, nullptr},
        {"EaseToward", "Ease Toward", CAT_MATH, false, {}, {"Current","Target","Speed"}, {"Result"}, 0, nullptr},
        {"BlendTime", "Blend Over Time", CAT_FLOW, true, {""}, {"From","To","Seconds"}, {"Value"}, 0, nullptr},
        {"Stopwatch", "Run Stopwatch", CAT_FLOW, true, {""}, {"Reset"}, {"Seconds"}, 0, nullptr},
        {"IsSlightly", "Is Slightly True", CAT_LOGIC, false, {}, {"Value"}, {"Result"}, 0, nullptr},
        {"IsMostly", "Is Mostly True", CAT_LOGIC, false, {}, {"Value"}, {"Result"}, 0, nullptr},
        {"IsVery", "Is Very True", CAT_LOGIC, false, {}, {"Value"}, {"Result"}, 0, nullptr},
        {"PickBest", "Pick Best Score", CAT_LOGIC, false, {}, {"First","Second","Third","Fourth"}, {"Index"}, 0, nullptr},
        {"ShuffleBag", "Draw Without Repeating", CAT_MATH, false, {}, {"List"}, {"Item"}, 0, nullptr},
        {"ChanceEachSecond", "Chance This Second", CAT_MATH, false, {}, {"Rate"}, {"Result"}, 0, nullptr},
        {"TryFirst", "Try First", CAT_FLOW, true, {"First","Second","Third"}, {}, {}, 0, nullptr},
        {"AllSucceed", "All Must Succeed", CAT_FLOW, true, {"First","Second","Third"}, {}, {}, 0, nullptr},
        {"FlipOutcome", "Flip Result", CAT_FLOW, true, {"Child"}, {}, {}, 0, nullptr},
        {"AlwaysSucceed", "Always Succeed", CAT_FLOW, true, {"Child"}, {}, {}, 0, nullptr},
        {"RepeatUntilFail", "Repeat Until Fail", CAT_FLOW, true, {"Child"}, {}, {}, 0, nullptr},
        {"WaitBetween", "Wait Between Tries", CAT_FLOW, true, {"Child"}, {"Seconds"}, {}, 0, nullptr},
        {"SucceedNow", "Succeed Now", CAT_FLOW, true, {}, {}, {}, 0, nullptr},
        {"FailNow", "Fail Now", CAT_FLOW, true, {}, {}, {}, 0, nullptr},
        {"CheckOutcome", "Check Condition", CAT_FLOW, true, {"Success","Fail"}, {"Condition"}, {}, 0, nullptr},
        {"InCone", "Is in Cone", CAT_LOGIC, false, {}, {"Target","Degrees"}, {"Result"}, 0, nullptr},
        {"LineOfSight", "Has Line of Sight", CAT_LOGIC, false, {}, {"Target"}, {"Result"}, 0, nullptr},
        {"InsideArea", "Is Inside Area", CAT_LOGIC, false, {}, {"Center","Size"}, {"Result"}, 0, nullptr},
        {"CountNearby", "Count Nearby", CAT_LOGIC, false, {}, {"Radius","Tag"}, {"Count"}, 0, nullptr},
        {"NearestObject", "Get Nearest Object", CAT_DATA, false, {}, {"Radius","Tag"}, {"Object"}, 0, nullptr},
        {"FarthestObject", "Get Farthest Object", CAT_DATA, false, {}, {"Radius","Tag"}, {"Object"}, 0, nullptr},
        {"SortByDistance", "Sort by Distance", CAT_LOGIC, false, {}, {"List"}, {"Sorted"}, 0, nullptr},
        {"WhenChanged", "When Value Changes", CAT_FLOW, true, {""}, {"Value"}, {}, 0, nullptr},
        {"DoUnless", "Run Unless", CAT_FLOW, true, {"Then","Skipped"}, {"Condition","Unless"}, {}, 0, nullptr},
        {"RememberValue", "Remember Value", CAT_ACTION, true, {""}, {"Name","Value"}, {}, 0, nullptr},
        {"RecallValue", "Get Remembered Value", CAT_DATA, false, {}, {"Name"}, {"Value"}, 0, nullptr},
        {"HealthEmpty", "When Health Runs Out", CAT_FLOW, true, {""}, {}, {}, 0, nullptr},
        {"HealthFull", "When Health is Full", CAT_FLOW, true, {""}, {}, {}, 0, nullptr},
        {"AddBuff", "Add Timed Buff", CAT_ACTION, true, {""}, {"Base","Add","Multiply","Seconds"}, {"Current"}, 0, nullptr},
        {"AdvanceObjective", "Advance Objective", CAT_ACTION, true, {"Each","Completed"}, {"Steps","Reset"}, {"Progress"}, 0, nullptr},
        {"AssertThis", "Assert This", CAT_ACTION, true, {""}, {"Condition","Message"}, {}, 0, nullptr},
        {"LogOnce", "Log Once", CAT_ACTION, true, {""}, {"Value","Reset"}, {}, 0, nullptr},
        {"DrawDebugLine", "Draw Debug Line", CAT_ACTION, true, {""}, {"From","To"}, {}, 0, nullptr},
#include "generated/library_reg.inc"
#include "generated/gplib_reg.inc"
        // --- Tweens, springs and eases (the engine's Tween / Spring / Easing code) ---
        {"EaseCurve",      "Ease With Curve",     CAT_MATH, false, {}, {"Time"}, {"Value"}, 1, "ease (OutBack, cubic-bezier(..), steps(4))"},
        {"SpringStep",     "Spring Step",         CAT_MATH, true, {""}, {"Target","Stiffness","Damping"}, {"Value","Velocity"}, 1, "start value (blank: first target)"},
        {"TweenMove",      "Tween Move",          CAT_ACTION, true, {""}, {"X","Y","Z","Seconds","Ease"}, {}, 1, "tag"},
        {"TweenMoveSpeed", "Tween Move By Speed", CAT_ACTION, true, {""}, {"X","Y","Z","Speed","Ease"}, {}, 1, "tag"},
        {"TweenKillTag",   "Kill Tweens With Tag",  CAT_ACTION, true, {""}, {"Complete"}, {"Count"}, 1, "tag"},
        {"TweenPauseTag",  "Pause Tweens With Tag", CAT_ACTION, true, {""}, {}, {"Count"}, 1, "tag"},
        {"TweenPlayTag",   "Play Tweens With Tag",  CAT_ACTION, true, {""}, {}, {"Count"}, 1, "tag"},
        // --- Flow: loops, gates, switches ---
        {"ForLoop",   "For Loop",      CAT_FLOW, true, {"Body","Done"}, {"First","Last"}, {"Index"}, 0, nullptr},
        {"Repeat",    "Repeat N",      CAT_FLOW, true, {"Body"}, {}, {}, 1, "count"},
        {"FlipFlop",  "Flip-Flop",     CAT_FLOW, true, {"A","B"}, {}, {}, 0, nullptr},
        {"Gate",      "Gate",          CAT_FLOW, true, {""}, {"Open"}, {}, 0, nullptr},
        {"DoN",       "Do N Times",    CAT_FLOW, true, {""}, {"Reset"}, {"Count"}, 1, "N"},
        {"SwitchInt", "Switch (int)",  CAT_FLOW, true, {"0","1","2","Default"}, {"Value"}, {}, 0, nullptr},
        {"SwitchString", "Switch (name)", CAT_FLOW, true, {"Case 1","Case 2","Case 3","Case 4","Default"}, {"Value"}, {}, 1, "cases, comma separated"},
        {"SwitchCases", "Switch (cases)", CAT_FLOW, true, {"Case 1","Case 2","Case 3","Case 4","Default"}, {"Value"}, {}, 1, "numbers, comma separated"},
        {"LatchGate", "Latch Gate", CAT_FLOW, true, {"Exit"}, {"Open","Close","Toggle"}, {"Is Open"}, 1, "open to start open"},
        {"Toggle",    "Toggle Var",    CAT_VAR,  true, {""}, {}, {}, 1, "name"},
        // --- Math ---
        {"RandomInt", "Random Int",    CAT_MATH, false, {}, {"Min","Max"}, {"Out"}, 0, nullptr},
        {"Ceil",      "Ceil",          CAT_MATH, false, {}, {"In"}, {"Out"}, 0, nullptr},
        {"Frac",      "Fraction",      CAT_MATH, false, {}, {"In"}, {"Out"}, 0, nullptr},
        {"OneMinus",  "One Minus",     CAT_MATH, false, {}, {"In"}, {"Out"}, 0, nullptr},
        {"Saturate",  "Clamp 0-1",     CAT_MATH, false, {}, {"In"}, {"Out"}, 0, nullptr},
        {"Asin",      "Asin",          CAT_MATH, false, {}, {"In"}, {"Rad"}, 0, nullptr},
        {"Acos",      "Acos",          CAT_MATH, false, {}, {"In"}, {"Rad"}, 0, nullptr},
        {"Deg2Rad",   "Deg to Rad",    CAT_MATH, false, {}, {"Deg"}, {"Rad"}, 0, nullptr},
        {"Rad2Deg",   "Rad to Deg",    CAT_MATH, false, {}, {"Rad"}, {"Deg"}, 0, nullptr},
        {"Vec3Cross", "Vec3 Cross",    CAT_MATH, false, {}, {"A","B"}, {"Vec3"}, 0, nullptr},
        {"Vec3Lerp",  "Vec3 Lerp",     CAT_MATH, false, {}, {"A","B","T"}, {"Vec3"}, 0, nullptr},
        {"Vec3Negate","Vec3 Negate",   CAT_MATH, false, {}, {"Vec"}, {"Vec3"}, 0, nullptr},
        // --- Transform (3D) ---
        {"ForwardVector", "Forward Vector", CAT_XFORM, false, {}, {}, {"Vec3"}, 0, nullptr},
        {"RightVector",   "Right Vector",   CAT_XFORM, false, {}, {}, {"Vec3"}, 0, nullptr},
        {"UpVector",      "Up Vector",      CAT_XFORM, false, {}, {}, {"Vec3"}, 0, nullptr},
        {"GetScale",      "Get Scale",      CAT_XFORM, false, {}, {}, {"Scale"}, 0, nullptr},
        {"GetRotation",   "Get Rotation",   CAT_XFORM, false, {}, {}, {"Euler"}, 0, nullptr},
        {"SetPositionVec","Set Position (Vec3)", CAT_XFORM, true, {""}, {"Pos"}, {}, 0, nullptr},
        {"MoveBy",        "Move By (Vec3)", CAT_XFORM, true, {""}, {"Offset"}, {}, 0, nullptr},
        {"Rotate3",       "Rotate (3D)",    CAT_XFORM, true, {""}, {"X","Y","Z"}, {}, 0, nullptr},
        {"SetRotation3",  "Set Rotation (3D)", CAT_XFORM, true, {""}, {"X","Y","Z"}, {}, 0, nullptr},
        // --- Actions ---
        {"SetColor",      "Set Color",      CAT_ACTION, true, {""}, {"R","G","B","A"}, {}, 0, nullptr},
        {"PlayClip",      "Play Clip",      CAT_ACTION, true, {""}, {}, {}, 1, "clip name"},
        {"PlayAction",    "Play Action",    CAT_ACTION, true, {""}, {"Blend"}, {}, 1, "action"},
        {"IsInAction",    "Is In Action",   CAT_DATA, false, {}, {}, {"In Action"}, 0, nullptr},
        {"Die",           "Die",            CAT_ACTION, true, {""}, {}, {}, 1, "forward / back / left / right"},
        {"Revive",        "Revive",         CAT_ACTION, true, {""}, {}, {}, 0, nullptr},
        {"Dive",          "Dive",           CAT_ACTION, true, {""}, {}, {}, 0, nullptr},
        {"Sit",           "Sit",            CAT_ACTION, true, {""}, {}, {}, 1, "seat name, ground, knees, chair"},
        {"StandUp",       "Stand Up",       CAT_ACTION, true, {""}, {}, {}, 0, nullptr},
        {"IsSitting",     "Is Sitting",     CAT_DATA, false, {}, {}, {"Sitting"}, 0, nullptr},
        {"Interact",      "Interact",       CAT_ACTION, true, {""}, {}, {}, 0, nullptr},
        {"SeatPrompt",    "Seat Prompt",    CAT_DATA, false, {}, {}, {"Prompt"}, 0, nullptr},
        {"HitReact",      "Hit React",      CAT_ACTION, true, {""}, {"Heavy"}, {}, 1, "forward / back / left / right"},
        {"Roll",          "Roll",           CAT_ACTION, true, {""}, {}, {}, 1, "forward / back / left / right"},
        {"Dodge",         "Dodge",          CAT_ACTION, true, {""}, {}, {}, 1, "forward / back / left / right"},
        {"Vault",         "Vault",          CAT_ACTION, true, {""}, {}, {}, 0, nullptr},
        {"Mantle",        "Mantle",         CAT_ACTION, true, {""}, {}, {}, 0, nullptr},
        {"LedgeGrab",     "Ledge Grab",     CAT_ACTION, true, {""}, {}, {}, 0, nullptr},
        {"ClimbUp",       "Climb Up",       CAT_ACTION, true, {""}, {}, {}, 0, nullptr},
        {"LetGo",         "Let Go",         CAT_ACTION, true, {""}, {}, {}, 0, nullptr},
        {"IsHanging",     "Is Hanging",     CAT_DATA, false, {}, {}, {"Hanging"}, 0, nullptr},
        {"Shimmy",        "Shimmy",         CAT_ACTION, true, {""}, {"Amount"}, {}, 0, nullptr},
        {"SetTimeScale",  "Set Time Scale", CAT_ACTION, true, {""}, {"Scale"}, {}, 0, nullptr},
        {"SetBlendShape", "Set Blend Shape",CAT_ACTION, true, {""}, {"Weight"}, {}, 1, "shape name"},
        {"GetBlendShape", "Blend Shape",    CAT_DATA,  false, {}, {}, {"Weight"}, 1, "shape name"},
        // --- Input / engine ---
        {"Raycast",    "Raycast 2D",   CAT_INPUT, false, {}, {"Origin X","Origin Y","Dir X","Dir Y","Max Dist"}, {"Hit"}, 0, nullptr},
        {"MouseWheel", "Mouse Wheel",  CAT_INPUT, false, {}, {}, {"Delta"}, 0, nullptr},
        {"TimeScale",  "Time Scale",   CAT_INPUT, false, {}, {}, {"Scale"}, 0, nullptr},
        {"FrameCount", "Frame Count",  CAT_INPUT, false, {}, {}, {"Frame"}, 0, nullptr},
        // --- Multiplayer ---
        {"NetHost",        "Host Game",      CAT_NET, true, {""}, {}, {}, 1, "port (45000)"},
        {"NetJoin",        "Join Game",      CAT_NET, true, {""}, {}, {}, 1, "host [port]"},
        {"NetDisconnect",  "Disconnect",     CAT_NET, true, {""}, {}, {}, 0, nullptr},
        {"NetConnected",   "Is Connected",   CAT_NET, false, {}, {}, {"Bool"}, 0, nullptr},
        {"NetIsServer",    "Is Host",        CAT_NET, false, {}, {}, {"Bool"}, 0, nullptr},
        {"NetIsClient",    "Is Client",      CAT_NET, false, {}, {}, {"Bool"}, 0, nullptr},
        {"NetPeers",       "Peer Count",     CAT_NET, false, {}, {}, {"Count"}, 0, nullptr},
        {"NetId",          "My Player Id",   CAT_NET, false, {}, {}, {"Id"}, 0, nullptr},
        {"NetSend",        "Send Message",   CAT_NET, true, {""}, {"Data"}, {}, 1, "channel"},
        {"NetSetVar",      "Set Net Var",    CAT_NET, true, {""}, {"Value"}, {}, 1, "name"},
        {"NetGetVar",      "Get Net Var",    CAT_NET, false, {}, {}, {"Value"}, 1, "name"},
        {"NetChat",        "Send Chat",      CAT_NET, true, {""}, {"Text"}, {}, 0, nullptr},
        {"NetRpc",         "Call RPC",       CAT_NET, true, {""}, {"Data"}, {}, 1, "rpc name"},
        {"NetSpawnOwned",  "Net Spawn",      CAT_NET, true, {""}, {}, {}, 1, "prefab"},
        {"NetDespawn",     "Net Despawn",    CAT_NET, true, {""}, {"Net Id"}, {}, 0, nullptr},
        {"NetReady",       "Set Ready",      CAT_NET, true, {""}, {"Ready"}, {}, 0, nullptr},
        {"NetStartMatch",  "Start Match",    CAT_NET, true, {""}, {}, {}, 0, nullptr},
        {"NetMatchStarted","Match Started",  CAT_NET, false, {}, {}, {"Bool"}, 0, nullptr},
        {"NetAllReady",    "All Ready",      CAT_NET, false, {}, {}, {"Bool"}, 0, nullptr},
        {"NetObjRpc",      "Call Object RPC", CAT_NET, true, {""}, {"Data"}, {}, 1, "rpc name"},
        {"SyncVarSet",     "Set Sync Var",   CAT_NET, true, {""}, {"Value"}, {}, 1, "name"},
        {"SyncVarGet",     "Get Sync Var",   CAT_NET, false, {}, {}, {"Value"}, 1, "name"},
        {"OnNetRpc",       "On Object RPC",  CAT_NET, false, {""}, {}, {"Value"}, 1, "rpc name"},
        // The easy layer: the same on the host and every client.
        {"NetHostOrJoin",  "Host or Join",   CAT_NET, true, {""}, {}, {}, 1, "port (45000)"},
        {"OnPlayerJoined", "On Player Joined", CAT_NET, false, {""}, {}, {}, 0, nullptr},
        {"OnPlayerLeft",   "On Player Left", CAT_NET, false, {""}, {}, {}, 0, nullptr},
        {"NetSendEvent",   "Send Network Event", CAT_NET, true, {""}, {"Data"}, {}, 1, "event name"},
        {"NetHostOnline",  "Host Online",    CAT_NET, true, {""}, {"Room"}, {"Room Code"}, 0, nullptr},
        {"NetJoinCode",    "Join By Code",   CAT_NET, true, {""}, {"Room"}, {}, 0, nullptr},
        {"OnNetworkEvent", "On Network Event", CAT_NET, false, {""}, {}, {"Data"}, 1, "event name"},
        {"NetEventSender", "Event Sender",   CAT_NET, false, {}, {}, {"Player Id"}, 0, nullptr},
        {"NetPlayerCount", "Player Count",   CAT_NET, false, {}, {}, {"Count"}, 0, nullptr},
        {"NetPlayerIdAt",  "Player Id At",   CAT_NET, false, {}, {"Index"}, {"Id"}, 0, nullptr},
        {"NetPlayerName",  "Player Name",    CAT_NET, false, {}, {"Player Id"}, {"Name"}, 0, nullptr},
        {"NetLastError",   "Network Error",  CAT_NET, false, {}, {}, {"Text"}, 0, nullptr},
        {"NetTime",        "Network Time",   CAT_NET, false, {}, {}, {"Seconds"}, 0, nullptr},
        {"NetPing",        "Ping",           CAT_NET, false, {}, {}, {"Ms"}, 0, nullptr},
        {"NetIsMine",      "Is Mine",        CAT_NET, false, {}, {}, {"Bool"}, 0, nullptr},
        {"NetIsPredicted", "Is Locally Predicted", CAT_NET, false, {}, {}, {"Bool"}, 0, nullptr},
        {"NetGiveOwnership", "Give Ownership", CAT_NET, true, {""}, {"Player Id"}, {}, 0, nullptr},
        {"NetRequestOwnership", "Request Ownership", CAT_NET, true, {""}, {}, {}, 0, nullptr},
        {"NetOwner",       "Owner",          CAT_NET, false, {}, {}, {"Player Id"}, 0, nullptr},
        {"NetStats",       "Network Stats",  CAT_NET, false, {}, {}, {"Ping ms", "Loss %", "In B/s", "Out B/s"}, 0, nullptr},
        {"OnOwnershipChanged", "On Ownership Changed", CAT_NET, false, {""}, {}, {}, 0, nullptr},
        {"NetSimulate",    "Simulate Bad Network", CAT_NET, true, {""}, {"Latency ms", "Jitter ms", "Loss %"}, {}, 0, nullptr},
        {"NetShowStats",   "Show Network Stats", CAT_NET, true, {""}, {"Show"}, {}, 0, nullptr},
        {"OnHostMigrated", "On Host Migrated", CAT_NET, false, {""}, {}, {}, 0, nullptr},
        {"NetMigrating",   "Is Migrating",   CAT_NET, false, {}, {}, {"Bool"}, 0, nullptr},
        {"SteamName",      "Steam Name",     CAT_NET, false, {}, {}, {"Name"}, 0, nullptr},
        {"SteamIsUnlocked","Achievement Unlocked", CAT_NET, false, {}, {}, {"Bool"}, 1, "achievement id"},
        {"SteamUnlock",    "Unlock Achievement",   CAT_NET, true, {""}, {}, {}, 1, "achievement id"},
        {"SteamGetStat",   "Get Steam Stat", CAT_NET, false, {}, {}, {"Value"}, 1, "stat"},
        {"SteamSetStat",   "Set Steam Stat", CAT_NET, true, {""}, {"Value"}, {}, 1, "stat"},
        {"SteamStore",     "Store Stats",    CAT_NET, true, {""}, {}, {}, 0, nullptr},
        {"SteamLeaderboard","Submit Score",  CAT_NET, true, {""}, {"Score"}, {}, 1, "leaderboard"},
        // ---- Fourth wave ----
        // Events: keys, timers, custom events between objects, exits, hover.
        {"OnKey",           "On Key Pressed",   CAT_EVENT, false, {""}, {}, {}, 1, "key"},
        {"OnKeyUp",         "On Key Released",  CAT_EVENT, false, {""}, {}, {}, 1, "key"},
        {"OnTimer",         "On Timer",         CAT_EVENT, false, {""}, {}, {}, 1, "every N seconds"},
        {"OnEvent",         "On Custom Event",  CAT_EVENT, false, {""}, {}, {"Value"}, 1, "event name"},
        {"OnCollisionExit", "On Collision Exit", CAT_EVENT, false, {""}, {}, {}, 0, nullptr},
        {"OnTriggerExit",   "On Trigger Exit",  CAT_EVENT, false, {""}, {}, {}, 0, nullptr},
        {"OnMouseEnter",    "On Mouse Enter",   CAT_EVENT, false, {""}, {}, {}, 0, nullptr},
        {"OnMouseDown",     "On Mouse Down",    CAT_EVENT, false, {""}, {}, {}, 0, nullptr},
        {"OnMouseUp",       "On Mouse Up",      CAT_EVENT, false, {""}, {}, {}, 0, nullptr},
        {"OnMouseOver",     "On Mouse Over",    CAT_EVENT, false, {""}, {}, {}, 0, nullptr},
        {"OnCollisionStay", "On Collision Stay",CAT_EVENT, false, {""}, {}, {}, 0, nullptr},
        {"OnLateUpdate",    "On Late Update",   CAT_EVENT, false, {""}, {}, {}, 0, nullptr},
        {"OnFixedUpdate",   "On Fixed Update",  CAT_EVENT, false, {""}, {}, {}, 0, nullptr},
        {"OnEnable",        "On Enable",        CAT_EVENT, false, {""}, {}, {}, 0, nullptr},
        {"OnDisable",       "On Disable",       CAT_EVENT, false, {""}, {}, {}, 0, nullptr},
        {"OnDestroy",       "On Destroy",       CAT_EVENT, false, {""}, {}, {}, 0, nullptr},
        // The Actions step library as nodes (the arg is the encoded step / steps).
        {"Action",          "Action",           CAT_ACTION, true, {""}, {}, {}, 3, nullptr},
        {"Actions",         "Action Steps",     CAT_ACTION, true, {""}, {}, {}, 4, nullptr},
        {"Condition",       "Check",            CAT_LOGIC,  true, {"True", "False"}, {}, {}, 5, nullptr},
        {"Delay",           "Delay (s)",        CAT_FLOW,   true, {""}, {}, {}, 1, "seconds"},
        {"OnMouseExit",     "On Mouse Exit",    CAT_EVENT, false, {""}, {}, {}, 0, nullptr},
        {"SendEvent",       "Send Event",       CAT_ACTION, true, {""}, {"Target", "Value"}, {}, 1, "event name"},
        {"BroadcastEvent",  "Broadcast Event",  CAT_ACTION, true, {""}, {"Value"}, {}, 1, "event name"},
        {"EventValue",      "Event Value",      CAT_DATA, false, {}, {}, {"Value"}, 0, nullptr},
        {"EventName",       "Event Name",       CAT_DATA, false, {}, {}, {"Name"}, 0, nullptr},
        // In-game build mode (the player's Build Mode component) and logic links.
        {"OnObjectPlaced",  "On Object Placed", CAT_EVENT, false, {""}, {}, {}, 0, nullptr},
        {"OnObjectRemoved", "On Object Removed",CAT_EVENT, false, {""}, {}, {}, 0, nullptr},
        {"OnLogicSignal",   "On Logic Signal",  CAT_EVENT, false, {""}, {}, {}, 0, nullptr},
        // Inventory (Event Name = the item / slot / recipe, Event Value = the count / item).
        {"OnItemAdded", "On Item Added", CAT_EVENT, false, {""}, {}, {}, 0, nullptr},
        {"OnItemRemoved", "On Item Removed", CAT_EVENT, false, {""}, {}, {}, 0, nullptr},
        {"OnInventoryFull", "On Inventory Full", CAT_EVENT, false, {""}, {}, {}, 0, nullptr},
        {"OnItemUsed", "On Item Used", CAT_EVENT, false, {""}, {}, {}, 0, nullptr},
        {"OnEquip", "On Equip", CAT_EVENT, false, {""}, {}, {}, 0, nullptr},
        {"OnCraft", "On Craft", CAT_EVENT, false, {""}, {}, {}, 0, nullptr},
        // Cutscenes (a Sequence Player plays a .okayseq; Event Name = the event).
        {"OnSequenceEvent", "On Sequence Event", CAT_EVENT, false, {""}, {}, {}, 1, "event (blank = any)"},
        {"PlaySequence",    "Play Sequence",     CAT_ACTION, true, {""}, {}, {}, 1, "name"},
        {"StopSequence",    "Stop Sequence",     CAT_ACTION, true, {""}, {}, {}, 0, nullptr},
        // Dialogue, story variables and quests (Event Name = the event / quest id, Event Value = the objective / count).
        {"StartDialogue",     "Start Dialogue",     CAT_ACTION, true, {""}, {}, {}, 1, "object / graph name"},
        {"OnDialogueEvent",   "On Dialogue Event",  CAT_EVENT, false, {""}, {}, {}, 0, nullptr},
        {"OnDialogueEnd",     "On Dialogue End",    CAT_EVENT, false, {""}, {}, {}, 0, nullptr},
        {"SetStoryVar",       "Set Story Var",      CAT_VAR, true, {""}, {"Value"}, {}, 1, "variable"},
        {"GetStoryVar",       "Get Story Var",      CAT_VAR, false, {}, {}, {"Value"}, 1, "variable"},
        {"StartQuest",        "Start Quest",        CAT_ACTION, true, {""}, {}, {}, 1, "quest id"},
        {"CompleteObjective", "Complete Objective", CAT_ACTION, true, {""}, {"Index"}, {}, 1, "quest id"},
        {"QuestState",        "Quest State",        CAT_DATA, false, {}, {}, {"State"}, 1, "quest id"},
        {"OnQuestStarted",    "On Quest Started",   CAT_EVENT, false, {""}, {}, {}, 0, nullptr},
        {"OnQuestCompleted",  "On Quest Completed", CAT_EVENT, false, {""}, {}, {}, 0, nullptr},
        {"OnQuestFailed",     "On Quest Failed",    CAT_EVENT, false, {""}, {}, {}, 0, nullptr},
        {"OnObjectiveCompleted", "On Objective Completed", CAT_EVENT, false, {""}, {}, {}, 0, nullptr},
        {"OnQuestEvent",      "On Quest Event",     CAT_EVENT, false, {""}, {}, {}, 0, nullptr},
        // Anti-cheat (server; needs an Anti-Cheat Config in the scene).
        {"OnCheatDetected", "On Cheat Detected",CAT_EVENT, false, {""}, {}, {}, 0, nullptr},
        {"OnValidateRpc",   "On Validate Rpc",  CAT_EVENT, false, {""}, {}, {}, 0, nullptr},
        {"RejectRpc",       "Reject Rpc",       CAT_ACTION, true, {""}, {}, {}, 0, nullptr},
        {"OnValidateHit",   "On Validate Hit",  CAT_EVENT, false, {""}, {}, {}, 0, nullptr},
        {"RejectHit",       "Reject Hit",       CAT_ACTION, true, {""}, {}, {}, 0, nullptr},
        {"ServerBudget",    "Server Budget",    CAT_ACTION, true, {""}, {"Player", "Add"}, {"Left"}, 1, "rpc name"},
        {"EnterBuildMode",  "Enter Build Mode", CAT_ACTION, true, {""}, {}, {}, 0, nullptr},
        {"ExitBuildMode",   "Exit Build Mode",  CAT_ACTION, true, {""}, {}, {}, 0, nullptr},
        {"InBuildMode",     "In Build Mode?",   CAT_DATA, false, {}, {}, {"Bool"}, 0, nullptr},
        {"SetBuildPalette", "Set Build Palette",CAT_ACTION, true, {""}, {}, {}, 1, "Name=prefab|icon, ..."},
        {"SaveCreation",    "Save Creation",    CAT_ACTION, true, {""}, {}, {"Saved"}, 1, "creation name"},
        {"LoadCreation",    "Load Creation",    CAT_ACTION, true, {""}, {}, {"Loaded"}, 1, "creation name"},
        {"AddItem",         "Add Item",         CAT_ACTION, true, {""}, {"Count"}, {"Left"}, 1, "item"},
        {"RemoveItem",      "Remove Item",      CAT_ACTION, true, {""}, {"Count"}, {"Removed"}, 1, "item"},
        {"HasItem",         "Has Item?",        CAT_DATA, false, {}, {"Count"}, {"Bool"}, 1, "item"},
        {"ItemCount",       "Item Count",       CAT_DATA, false, {}, {}, {"Count"}, 1, "item"},
        {"EquipItem",       "Equip",            CAT_ACTION, true, {""}, {"Slot"}, {"Equipped"}, 1, "item"},
        {"CraftItem",       "Craft",            CAT_ACTION, true, {""}, {"Times"}, {"Crafted"}, 1, "recipe output item"},
        {"LootRoll",        "Loot Roll",        CAT_ACTION, true, {""}, {}, {"Rolled"}, 1, "loot table"},
        {"SpawnPickup",     "Spawn Pickup",     CAT_ACTION, true, {""}, {"Count", "X", "Y", "Z"}, {"Name"}, 1, "item"},
        {"SwingWeapon",     "Swing Weapon",     CAT_ACTION, true, {""}, {}, {"Swung"}, 0, nullptr},
        {"FireWeapon",      "Fire Weapon",      CAT_ACTION, true, {""}, {}, {"Fired"}, 0, nullptr},
        {"ReloadWeapon",    "Reload Weapon",    CAT_ACTION, true, {""}, {}, {"Started"}, 0, nullptr},
        {"WeaponAmmo",      "Get Ammo",         CAT_DATA, false, {}, {}, {"Rounds"}, 0, nullptr},
        {"SetWeapon",       "Set Weapon",       CAT_ACTION, true, {""}, {}, {"Set"}, 1, "weapon"},
        // Other objects' variables, save / load.
        {"SetObjectVar",    "Set Object Var",   CAT_VAR, true, {""}, {"Target", "Value"}, {}, 1, "variable"},
        {"GetObjectVar",    "Get Object Var",   CAT_VAR, false, {}, {"Target"}, {"Value"}, 1, "variable"},
        {"SaveValue",       "Save Value",       CAT_VAR, true, {""}, {"Value"}, {}, 1, "key"},
        {"LoadValue",       "Load Value",       CAT_VAR, false, {}, {"Default"}, {"Value"}, 1, "key"},
        // Text.
        {"Format",          "Format Text",      CAT_DATA, false, {}, {"{0}", "{1}", "{2}"}, {"Text"}, 1, "Score: {0}"},
        {"Substring",       "Substring",        CAT_DATA, false, {}, {"Text", "Start", "Length"}, {"Text"}, 0, nullptr},
        {"Replace",         "Replace Text",     CAT_DATA, false, {}, {"Text", "Find", "With"}, {"Text"}, 0, nullptr},
        {"ToNumber",        "To Number",        CAT_DATA, false, {}, {"Text"}, {"Number"}, 0, nullptr},
        {"StartsWith",      "Starts With",      CAT_DATA, false, {}, {"Text", "Prefix"}, {"Yes"}, 0, nullptr},
        {"EndsWith",        "Ends With",        CAT_DATA, false, {}, {"Text", "Suffix"}, {"Yes"}, 0, nullptr},
        {"Trim",            "Trim Spaces",      CAT_DATA, false, {}, {"Text"}, {"Text"}, 0, nullptr},
        {"IndexOf",         "Index Of",         CAT_DATA, false, {}, {"Text", "Find"}, {"Index"}, 0, nullptr},
        // Localization (the project's string table; On Language Changed: Event Name = the new language).
        {"Localize",          "Localize",            CAT_DATA, false, {}, {"{0}", "{1}", "{2}"}, {"Text"}, 1, "key"},
        {"GetLanguage",       "Get Language",        CAT_DATA, false, {}, {}, {"Code"}, 0, nullptr},
        {"SetLanguage",       "Set Language",        CAT_ACTION, true, {""}, {"Code"}, {}, 1, "code (fr, de...)"},
        {"OnLanguageChanged", "On Language Changed", CAT_EVENT, false, {""}, {}, {}, 0, nullptr},
        // Math.
        {"Noise",           "Perlin Noise",     CAT_MATH, false, {}, {"X", "Y"}, {"Value"}, 0, nullptr},
        {"Snap",            "Snap To Step",     CAT_MATH, false, {}, {"Value", "Step"}, {"Out"}, 0, nullptr},
        {"Wrap",            "Wrap",             CAT_MATH, false, {}, {"Value", "Min", "Max"}, {"Out"}, 0, nullptr},
        {"Average",         "Average",          CAT_MATH, false, {}, {"A", "B"}, {"Out"}, 0, nullptr},
        {"Distance2D",      "Distance 2D",      CAT_MATH, false, {}, {"X1", "Y1", "X2", "Y2"}, {"Distance"}, 0, nullptr},
        // Finding + moving objects.
        {"FindWithTag",     "Find With Tag",    CAT_XFORM, false, {}, {}, {"Object"}, 1, "tag"},
        {"CountWithTag",    "Count With Tag",   CAT_XFORM, false, {}, {}, {"Count"}, 1, "tag"},
        {"NearestWithTag",  "Nearest With Tag", CAT_XFORM, false, {}, {}, {"Object"}, 1, "tag"},
        {"Teleport",        "Teleport",         CAT_XFORM, true, {""}, {"X", "Y", "Z", "Object"}, {}, 1, "object (self)"},
        // Game flow.
        {"SetVisible",      "Set Visible",      CAT_ACTION, true, {""}, {"Visible"}, {}, 0, nullptr},
        {"PauseGame",       "Pause Game",       CAT_ACTION, true, {""}, {"Paused"}, {}, 0, nullptr},
        {"QuitGame",        "Quit Game",        CAT_ACTION, true, {""}, {}, {}, 0, nullptr},
        {"ShowCursor",      "Show Cursor",      CAT_INPUT,  true, {""}, {"Visible"}, {}, 0, nullptr},
        {"ChanceBranch",    "Random Branch",    CAT_FLOW,   true, {"True", "False"}, {"Chance"}, {}, 0, nullptr},
        {"Counter",         "Counter",          CAT_FLOW,   true, {""}, {"Reset"}, {"Count"}, 0, nullptr},
        {"EveryN",          "Every N Times",    CAT_FLOW,   true, {""}, {"N"}, {}, 0, nullptr},
        {"ShowUI",          "Show UI",          CAT_ACTION, true, {""}, {}, {}, 1, "UI object (self)"},
        {"HideUI",          "Hide UI",          CAT_ACTION, true, {""}, {}, {}, 1, "UI object (self)"},
        // Physics: trigger stay, 3D casts / overlaps (setting = layer list), contact details.
        {"OnTriggerStay",   "On Trigger Stay",  CAT_EVENT, false, {""}, {}, {}, 0, nullptr},
        {"Raycast3D",       "Raycast 3D",       CAT_INPUT, false, {}, {"Origin", "Direction", "Max Dist", "Ignore Triggers"},
                                                {"Hit", "Point", "Normal", "Distance", "Object"}, 1, "layers (blank = all)"},
        {"SphereCast3D",    "Sphere Cast",      CAT_INPUT, false, {}, {"Origin", "Direction", "Radius", "Max Dist"},
                                                {"Hit", "Point", "Normal", "Distance", "Object"}, 1, "layers (blank = all)"},
        {"RaycastAll3D",    "Raycast All 3D",   CAT_INPUT, false, {}, {"Origin", "Direction", "Max Dist"},
                                                {"Count", "Objects", "Nearest"}, 1, "layers (blank = all)"},
        {"OverlapSphere3D", "Overlap Sphere",   CAT_INPUT, false, {}, {"Center", "Radius"}, {"Count", "Objects"}, 1, "layers (blank = all)"},
        {"OverlapBox3D",    "Overlap Box",      CAT_INPUT, false, {}, {"Center", "Half Size"}, {"Count", "Objects"}, 1, "layers (blank = all)"},
        {"BoxCast2D",       "Box Cast",         CAT_INPUT, false, {}, {"Center", "Flat Half", "Heading", "Box Angle", "Reach"},
                                                {"Hit", "Point", "Normal", "Distance", "Object"}, 1, "layers (blank = all)"},
        {"CapsuleCast2D",   "Capsule Cast",     CAT_INPUT, false, {}, {"Segment Start", "Segment End", "Capsule Radius", "Heading", "Reach"},
                                                {"Hit", "Point", "Normal", "Distance", "Object"}, 1, "layers (blank = all)"},
        {"OverlapBox2D",    "Overlap Rectangle", CAT_INPUT, false, {}, {"Center", "Flat Half", "Box Angle"},
                                                {"Count", "Objects"}, 1, "layers (blank = all)"},
        {"OverlapCapsule2D","Overlap Capsule",  CAT_INPUT, false, {}, {"Segment Start", "Segment End", "Capsule Radius"},
                                                {"Count", "Objects"}, 1, "layers (blank = all)"},
        {"AddExplosionForce", "Add Explosion Force", CAT_ACTION, true, {""}, {"Blast Point", "Blast Force", "Blast Radius", "Upward Bias"}, {}, 0, nullptr},
        {"ContactPoint",    "Contact Point",    CAT_DATA,  false, {}, {}, {"Point"}, 0, nullptr},
        {"ContactNormal",   "Contact Normal",   CAT_DATA,  false, {}, {}, {"Normal"}, 0, nullptr},
        {"ImpactSpeed",     "Impact Speed",     CAT_DATA,  false, {}, {}, {"Speed"}, 0, nullptr},
        // Object references: wire these into any Target / Object input.
        {"Self",            "Self",             CAT_XFORM, false, {}, {}, {"Object"}, 0, nullptr},
        {"Other",           "Other Object",     CAT_XFORM, false, {}, {}, {"Object"}, 0, nullptr},
        {"FindObject",      "Find Object",      CAT_XFORM, false, {}, {}, {"Object"}, 1, "object name"},
        {"GetName",         "Get Name",         CAT_XFORM, false, {}, {}, {"Name"}, 0, nullptr},
        {"GetTag",          "Get Tag",          CAT_XFORM, false, {}, {}, {"Tag"}, 0, nullptr},
        // Functions: pins come from the Function node's signature (see VsDefFor).
        {"FunctionStart",   "Function",         CAT_FLOW,  false, {""}, {}, {}, 1, "Name(a: Number) -> out: Number"},
        {"Return",          "Return",           CAT_FLOW,  true, {}, {}, {}, 6, nullptr},
        {"CallFunction",    "Call Function",    CAT_FLOW,  true, {""}, {}, {}, 6, nullptr},
        {"SetLocal",        "Set Local",        CAT_VAR,   true, {""}, {"Value"}, {}, 1, "name"},
        {"GetLocal",        "Get Local",        CAT_VAR,   false, {}, {}, {"Value"}, 1, "name"},
        // Loops (Break / Continue act on the loop whose Body runs them).
        {"While",           "While",            CAT_FLOW,  true, {"Body","Done"}, {"Cond"}, {}, 0, nullptr},
        {"ForEach",         "For Each",         CAT_FLOW,  true, {"Body","Done"}, {"List"}, {"Item","Index"}, 0, nullptr},
        {"ForEachMap",      "For Each Map",     CAT_FLOW,  true, {"Body","Done"}, {"Map"}, {"Key","Value"}, 0, nullptr},
        {"Break",           "Break",            CAT_FLOW,  true, {}, {}, {}, 0, nullptr},
        {"Continue",        "Continue",         CAT_FLOW,  true, {}, {}, {}, 0, nullptr},
        // Lists
        {"NewList",         "New List",         CAT_DATA,  false, {}, {}, {"List"}, 1, "item type (optional)"},
        {"NewMap",          "New Map",          CAT_DATA,  false, {}, {}, {"Map"}, 1, "item type (optional)"},
        {"MapSet",          "Map Set",          CAT_DATA,  true, {""}, {"Map","Key","Value"}, {}, 0, nullptr},
        {"MapRemove",       "Map Remove",       CAT_DATA,  true, {""}, {"Map","Key"}, {}, 0, nullptr},
        {"MapGet",          "Map Get",          CAT_DATA,  false, {}, {"Map","Key"}, {"Value","Found"}, 0, nullptr},
        {"MapHas",          "Map Has",          CAT_DATA,  false, {}, {"Map","Key"}, {"Yes"}, 0, nullptr},
        {"MapKeys",         "Map Keys",         CAT_DATA,  false, {}, {"Map"}, {"Keys"}, 0, nullptr},
        {"MapValues",       "Map Values",       CAT_DATA,  false, {}, {"Map"}, {"Values"}, 0, nullptr},
        {"ListAdd",         "List Add",         CAT_DATA,  true, {""}, {"List","Item"}, {}, 0, nullptr},
        {"ListRemoveAt",    "List Remove At",   CAT_DATA,  true, {""}, {"List","Index"}, {}, 0, nullptr},
        {"ListGet",         "List Get",         CAT_DATA,  false, {}, {"List","Index"}, {"Item"}, 0, nullptr},
        {"ListLength",      "List Length",      CAT_DATA,  false, {}, {"List"}, {"Count"}, 0, nullptr},
        {"ListContains",    "List Contains",    CAT_DATA,  false, {}, {"List","Item"}, {"Bool"}, 0, nullptr},
        {"ListInsert",      "List Insert",      CAT_DATA,  true, {""}, {"List","Index","Item"}, {}, 0, nullptr},
        {"ListSet",         "List Set",         CAT_DATA,  true, {""}, {"List","Index","Item"}, {}, 0, nullptr},
        {"ListFind",        "List Find",        CAT_DATA,  false, {}, {"List","Item"}, {"Index","Found"}, 0, nullptr},
        {"ListRemoveItem",  "List Remove Item", CAT_DATA,  true, {""}, {"List","Item"}, {"Removed"}, 0, nullptr},
        {"ListSort",        "List Sort",        CAT_DATA,  true, {""}, {"List","Descending"}, {}, 0, nullptr},
        {"ListReverse",     "List Reverse",     CAT_DATA,  true, {""}, {"List"}, {}, 0, nullptr},
        {"ListClear",       "List Clear",       CAT_DATA,  true, {""}, {"List"}, {}, 0, nullptr},
        {"ListCopy",        "List Copy",        CAT_DATA,  false, {}, {"List","Deep"}, {"Copy"}, 0, nullptr},
        {"MapClear",        "Map Clear",        CAT_DATA,  true, {""}, {"Map"}, {}, 0, nullptr},
        {"MapCount",        "Map Count",        CAT_DATA,  false, {}, {"Map"}, {"Count"}, 0, nullptr},
        {"MapCopy",         "Map Copy",         CAT_DATA,  false, {}, {"Map","Deep"}, {"Copy"}, 0, nullptr},
        {"ListAppend",      "List Append",      CAT_DATA,  true, {""}, {"List","Other"}, {}, 0, nullptr},
        {"ListSlice",       "List Slice",       CAT_DATA,  false, {}, {"List","Start","Count"}, {"Slice"}, 0, nullptr},
        {"ListPop",         "List Pop",         CAT_DATA,  true, {""}, {"List"}, {"Item","Popped"}, 0, nullptr},
        {"MapGetOr",        "Map Get Or",       CAT_DATA,  false, {}, {"Map","Key","Default"}, {"Value"}, 0, nullptr},
        {"MapMerge",        "Map Merge",        CAT_DATA,  true, {""}, {"Into","From"}, {}, 0, nullptr},
        {"StructNew",       "New Struct",       CAT_DATA,  false, {}, {}, {"Struct"}, 1, "struct name"},
        {"StructGet",       "Get Field",        CAT_DATA,  false, {}, {"Struct"}, {"Value"}, 1, "field"},
        {"StructSet",       "Set Field",        CAT_DATA,  true, {""}, {"Struct","Value"}, {}, 1, "field"},
        {"MakeColor",       "Make Color",       CAT_DATA,  false, {}, {"R","G","B","A"}, {"Color"}, 0, nullptr},
        {"SplitColor",      "Split Color",      CAT_DATA,  false, {}, {"Color"}, {"R","G","B","A"}, 0, nullptr},
        {"AllWithTag",      "All With Tag",     CAT_XFORM, false, {}, {}, {"Objects"}, 1, "tag"},
        // Scene variables (every graph in the scene) and global ones (kept across scene loads).
        {"GetSceneVar",     "Get Scene Var",    CAT_VAR,   false, {}, {}, {"Value"}, 1, "name"},
        {"SetSceneVar",     "Set Scene Var",    CAT_VAR,   true, {""}, {"Value"}, {}, 1, "name"},
        {"AddSceneVar",     "Add Scene Var",    CAT_VAR,   true, {""}, {"Amount"}, {}, 1, "name"},
        {"GetGlobalVar",    "Get Global Var",   CAT_VAR,   false, {}, {}, {"Value"}, 1, "name"},
        {"SetGlobalVar",    "Set Global Var",   CAT_VAR,   true, {""}, {"Value"}, {}, 1, "name"},
        {"AddGlobalVar",    "Add Global Var",   CAT_VAR,   true, {""}, {"Amount"}, {}, 1, "name"},
        // Variable Changed events: the new Value and the Old one.
        {"OnVarChanged",       "On Var Changed",        CAT_EVENT, false, {""}, {}, {"Value","Old"}, 1, "variable"},
        {"OnSceneVarChanged",  "On Scene Var Changed",  CAT_EVENT, false, {""}, {}, {"Value","Old"}, 1, "variable"},
        {"OnGlobalVarChanged", "On Global Var Changed", CAT_EVENT, false, {""}, {}, {"Value","Old"}, 1, "variable"},
        {"IsValid",         "Is Valid",         CAT_LOGIC, false, {}, {"Value"}, {"Yes"}, 0, nullptr},
        {"CallScript",      "Call Script",      CAT_ACTION, true, {""}, {"Argument"}, {"Result"}, 1, "function"},
        {"RunGraph",        "Run Graph",        CAT_FLOW,  true, {""}, {}, {}, 1, "file.okayvs or file|Event"},
    })))))));
    return defs;
}

// ---- Key picker for the Key Held / Key Down / Key Up nodes ------------------
// Stores the friendly name the runtime resolves (Input::KeyFromName): "space",
// "shift", "a", "7"... Typing a name into a tiny box was guesswork; this is a
// button that captures the next key press, plus a list to pick from.
inline bool IsKeyNode(const std::string& t) { return t == "Key" || t == "KeyDown" || t == "KeyUp" || t == "OnKey" || t == "OnKeyUp"; }
struct VsKeyName { const char* name; const char* label; };
inline const std::vector<VsKeyName>& VsNamedKeys() {
    static const std::vector<VsKeyName> k = {
        {"space", "Space"}, {"enter", "Enter"}, {"escape", "Escape"}, {"tab", "Tab"},
        {"backspace", "Backspace"}, {"shift", "Shift"}, {"ctrl", "Ctrl"},
        {"up", "Up Arrow (same as W)"}, {"down", "Down Arrow (same as S)"},
        {"left", "Left Arrow (same as A)"}, {"right", "Right Arrow (same as D)"}};
    return k;
}
inline std::string VsKeyLabel(std::string arg) {
    for (auto& c : arg) c = (char)std::tolower((unsigned char)c);
    if (arg.empty() || arg == " ") return "Space";
    for (const auto& k : VsNamedKeys()) if (arg == k.name) {
        std::string l = k.label; auto p = l.find(" ("); return p == std::string::npos ? l : l.substr(0, p);
    }
    if (arg.size() == 1) return std::string(1, (char)std::toupper((unsigned char)arg[0]));
    return arg;
}
/// What a new Quick block fills itself in from: the last key picked, the selected object, the scene's objects.
inline okay::vs::QbContext& VsQuickContext() { static okay::vs::QbContext c; return c; }

inline const VsDef* FindDef(const std::string& type) {
    // Asked several times per node per frame by the canvas; the registry has hundreds of entries and never changes after it is
    // built, so an index (the first entry of a type wins, as the scan did) replaces a string compare against every entry.
    static const std::unordered_map<std::string, const VsDef*> index = [] {
        std::unordered_map<std::string, const VsDef*> m;
        for (const auto& d : Registry()) m.emplace(d.type, &d);
        return m;
    }();
    auto it = index.find(type);
    return it == index.end() ? nullptr : it->second;
}

// ---- Editable graph model --------------------------------------------------
struct VsEditNode {
    int id; std::string type; std::string arg; float x = 0, y = 0;
    /// Values typed into unconnected data inputs (pin -> text as typed).
    std::map<int, std::string> pinDefault;
};

// A typed value as it goes into the file / OkayScript: numbers and true/false
// as-is, anything else as a "quoted string".
inline bool VsIsNumberOrBool(const std::string& v) {
    if (v == "true" || v == "false") return true;
    if (v.empty()) return false;
    char* end = nullptr;
    std::strtod(v.c_str(), &end);
    return end && *end == '\0';
}
// Quoted for the .okayvs file: \" and \\ escapes (the runtime reads the same).
inline std::string VsQuoted(const std::string& v) { return okay::vs::QuoteText(v); }
// A Const's text is a literal: a number / bool stays bare, an already-"quoted"
// string keeps its text (escaped), anything else is quoted if it has to be.
inline std::string VsConstField(const std::string& a) {
    if (VsIsNumberOrBool(a)) return a;
    if (a.size() >= 2 && a.front() == '"' && a.back() == '"') return okay::vs::QuoteText(a.substr(1, a.size() - 2));
    return okay::vs::QuoteIfNeeded(a);
}
// A layout coordinate as an int for the file (NaN / huge values clamp: the cast
// would be undefined).
inline long long VsCoord(float v) {
    if (!(v == v)) return 0;
    if (v > 1.0e9f) return 1000000000;
    if (v < -1.0e9f) return -1000000000;
    return (long long)v;
}
// A layout value read from a file: any number text (1e40, nan, inf too) as a
// float in the same +-1e9 range VsCoord saves (NaN -> 0). False if unreadable.
inline bool VsReadCoord(std::istream& in, float& out) {
    std::string t;
    if (!(in >> t)) return false;
    char* end = nullptr;
    const double d = std::strtod(t.c_str(), &end);
    if (end == t.c_str() || *end) return false;
    out = d != d ? 0.0f : (float)std::max(-1.0e9, std::min(1.0e9, d));
    return true;
}
inline std::string VsLiteral(const std::string& v) { return VsIsNumberOrBool(v) ? v : VsQuoted(v); }
struct VsExecLink { int from, pin, to; };
struct VsDataLink { int consumer, inPin, producer, outPin; };

// A comment frame: a titled, tinted box that groups nodes (moves them with it).
struct VsComment { float x = 0, y = 0, w = 300, h = 200; std::string text = "Comment"; int color = 0; };

// A knot (reroute point): a dot a wire passes through, so long wires can go around blocks. Only where the wire is drawn
// changes: the runtime ignores `knot` lines. A flow wire is named by from / pin / to, a value wire by consumer / inPin
// (an input has one wire). A wire's knots are passed in the order they are listed.
struct VsKnot { bool exec = true; int a = 0, b = 0, c = 0; float x = 0, y = 0; };

// The runtime entry name an event node registers under ("" = not an event):
// "OnStart", "OnKey:space", "OnTimer:2", "OnEvent:Hit", "OnAnimEvent:step"...
// Spaces become '_' so the name stays one token (the runtime does the same).
/// A quick block that starts on its own event (no flow input) counts as the start of a script.
inline bool VsStartsOnItsOwn(const std::string& type) {
    const okay::vs::QuickBlock* b = okay::vs::FindQuickBlock(type);
    return b && !b->flowIn;
}
inline std::string VsEntryName(const std::string& type, const std::string& arg) {
    auto tok = [](std::string v, const char* d) {
        if (v.empty()) v = d;
        for (char& c : v) if (c == ' ' || c == '\t') c = '_';
        return v;
    };
    if (type == "OnStart" || type == "OnUpdate" || type == "OnCollision" || type == "OnTrigger" ||
        type == "OnClick" || type == "OnCollisionExit" || type == "OnTriggerExit" ||
        type == "OnMouseEnter" || type == "OnMouseExit" || type == "OnMouseDown" || type == "OnMouseUp" ||
        type == "OnMouseOver" || type == "OnCollisionStay" || type == "OnLateUpdate" ||
        type == "OnTriggerStay" ||
        type == "OnFixedUpdate" || type == "OnDestroy" || type == "OnEnable" || type == "OnDisable" ||
        type == "OnObjectPlaced" || type == "OnObjectRemoved" || type == "OnLogicSignal" ||
        type == "OnCheatDetected" || type == "OnValidateRpc" || type == "OnValidateHit" ||
        type == "OnItemAdded" || type == "OnItemRemoved" || type == "OnInventoryFull" ||
        type == "OnItemUsed" || type == "OnEquip" || type == "OnCraft" ||
        type == "OnDialogueEvent" || type == "OnDialogueEnd" || type == "OnQuestStarted" || type == "OnQuestCompleted" ||
        type == "OnQuestFailed" || type == "OnObjectiveCompleted" || type == "OnQuestEvent" ||
        type == "OnPlayerJoined" || type == "OnPlayerLeft" || type == "OnOwnershipChanged" || type == "OnHostMigrated") return type;
    if (type == "OnAnimEvent") return arg.empty() ? std::string("OnAnimEvent") : "OnAnimEvent:" + tok(arg, "");
    if (type == "OnSequenceEvent") return arg.empty() ? std::string("OnSequenceEvent") : "OnSequenceEvent:" + tok(arg, "");
    if (type == "OnKey")   return "OnKey:" + tok(arg, "space");
    if (type == "OnKeyUp") return "OnKeyUp:" + tok(arg, "space");
    if (type == "OnTimer") return "OnTimer:" + tok(arg, "1");
    if (type == "OnEvent") return "OnEvent:" + tok(arg, "Event");
    if (type == "OnNetRpc") return "OnNetRpc:" + tok(arg, "rpc");
    if (type == "OnNetworkEvent") return "OnNetworkEvent:" + tok(arg, "event");
    if (type == "OnVarChanged" || type == "OnSceneVarChanged" || type == "OnGlobalVarChanged") return type + ":" + tok(arg, "x");
    if (type == "OnLanguageChanged" || type == "OnInteract" || type == "OnDeath" || type == "OnHealthChanged" || type == "OnDamaged") return type;
    if (type == "OnButtonClicked") return "OnButtonClicked:" + tok(arg, "Button");
    if (type == "OnTagEnter" || type == "OnTagStay" || type == "OnTagExit" || type == "OnCollisionTag")
        return type + ":" + tok(arg, "Player");
    if (type == "OnTap" || type == "OnDoubleTap" || type == "OnLongPress" || type == "OnPinch" || type == "OnRotate")
        return type;
    if (type == "OnSwipe") return arg.empty() ? std::string("OnSwipe") : "OnSwipe:" + tok(arg, "");
    if (type == "OnAction") return "OnAction:" + tok(arg, "interact");
    if (type == "OnActionUp") return "OnActionUp:" + tok(arg, "interact");
    if (type == "OnActionHeld") return "OnActionHeld:" + tok(arg, "interact");
    return {};
}

inline bool IsSwitchNode(const std::string& t);
inline std::vector<std::string> VsEditorSwitchCases(const std::string& arg);

struct VsEditGraph {
    std::vector<VsEditNode> nodes;
    std::vector<VsExecLink> execLinks;
    std::vector<VsDataLink> dataLinks;
    std::vector<VsComment> comments;
    std::vector<VsKnot> knots;       // reroute points on wires (layout only)
    // Declared variables: `decl name Type scope literal` lines (typed) and old
    // `var name value` lines (typed = false: object scope, type from the value).
    // The defaults are what the Inspector edits. Kept across save.
    struct Exposed {
        std::string name;
        std::string value;               // the default: a literal for typed rows, the old plain text otherwise
        std::string type = "Number";     // Number, Bool, Text, Vector3, Color, Object, List, List<T>, Map, Map<T>, or a struct
        std::string scope = "object";    // object (this graph's own), scene, global
        bool typed = false;
    };
    std::vector<Exposed> vars;
    /// `struct Name field:Type=literal ...` lines: record types a variable can have.
    struct StructFieldEd { std::string name, type = "Number", value; };
    struct StructEd { std::string name; std::vector<StructFieldEd> fields; };
    std::vector<StructEd> structs;
    int nextId = 1;
    std::string path;      // .okayvs file (empty = unsaved)
    bool dirty = false;
private:
    mutable std::string m_declKey, m_declError;
    mutable std::vector<okay::vs::VarDecl> m_decls;
    mutable std::vector<okay::vs::StructDef> m_structDefs;
    void refreshDecls() const {
        if (vars.empty() && structs.empty()) {   // asked for every wire's pin types every frame: no text to build for no declarations
            if (!m_declKey.empty() || !m_decls.empty() || !m_structDefs.empty() || !m_declError.empty()) {
                m_declKey.clear(); m_decls.clear(); m_structDefs.clear(); m_declError.clear();
            }
            return;
        }
        const std::string key = declText();
        if (key == m_declKey && !key.empty()) return;
        if (key.empty()) { m_declKey.clear(); m_decls.clear(); m_structDefs.clear(); m_declError.clear(); return; }
        m_declKey = key; m_decls.clear(); m_structDefs.clear(); m_declError.clear();
        std::string err;
        if (auto g = okay::vs::NodeGraph::Parse(key, &err)) { m_decls = g->Decls(); m_structDefs = g->Structs(); }
        else {
            m_declError = err;
            // Struct names alone still let a type be picked while a default is wrong.
            for (const auto& st : structs) m_structDefs.push_back({st.name, {}});
        }
    }
public:

    VsEditNode* find(int id) { for (auto& n : nodes) if (n.id == id) return &n; return nullptr; }
    int add(const std::string& type, float x, float y) {
        VsEditNode n; n.id = nextId++; n.type = type; n.x = x; n.y = y;
        const VsDef* d = FindDef(type);
        // Sensible starting values for nodes that need a name / number.
        static const std::map<std::string, std::string> kArg = {
            {"Compare", ">"}, {"Timer", "1"}, {"Const", "0"}, {"OnKey", "space"}, {"OnKeyUp", "space"},
            {"OnTimer", "1"}, {"OnEvent", "Event"}, {"OnNetRpc", "rpc"}, {"SendEvent", "Event"}, {"BroadcastEvent", "Event"},
            {"NetSendEvent", "event"}, {"OnNetworkEvent", "event"},
            {"NetObjRpc", "rpc"}, {"SyncVarSet", "var"}, {"SyncVarGet", "var"},
            {"SetObjectVar", "hp"}, {"GetObjectVar", "hp"}, {"SaveValue", "score"}, {"LoadValue", "score"},
            {"Format", "Score: {0}"}, {"FindWithTag", "Player"}, {"CountWithTag", "Enemy"}, {"FindObject", "Player"},
            {"NearestWithTag", "Enemy"}, {"Teleport", "self"},
            {"ShowUI", "self"}, {"HideUI", "self"}, {"Delay", "1"}, {"Action", "log Hello"},
            {"FollowPath", "0,0,0; 2,0,0"}, {"Patrol", "0,0,0; 3,0,0; 3,0,3"},
            {"OnTagEnter", "Player"}, {"OnTagStay", "Player"}, {"OnTagExit", "Player"}, {"OnCollisionTag", "Player"},
            {"OnAction", "interact"}, {"OnActionUp", "interact"}, {"OnActionHeld", "interact"},
            {"Countdown", "5"}, {"Ammo", "6"}, {"CrossfadeClip", "Idle"}, {"TweenPosition", "inout"},
            {"TweenRotation", "inout"}, {"TweenScale", "inout"}, {"TweenColor", "inout"},
            {"Condition", "always"}, {"Actions", "log Hello"},
            {"SetBuildPalette", "Block=Assets/block.okayprefab"}, {"SaveCreation", "creation"}, {"LoadCreation", "creation"},
            {"AddItem", "item"}, {"RemoveItem", "item"}, {"HasItem", "item"}, {"ItemCount", "item"}, {"EquipItem", "item"},
            {"CraftItem", "item"}, {"LootRoll", "loot"}, {"SpawnPickup", "item"}, {"SetWeapon", "sword"}};
        if (d) if (auto it = kArg.find(d->type); it != kArg.end()) n.arg = it->second;
        // A quick block starts with the values its fields show, so it works the moment it is dropped.
        if (const okay::vs::QuickBlock* qb = okay::vs::FindQuickBlock(type)) {
            int pin = 0;
            for (const okay::vs::QbField& f : okay::vs::QbAllFields(*qb)) n.pinDefault[pin++] = okay::vs::QbSmartDefault(f, VsQuickContext());
        }
        nodes.push_back(n); dirty = true; return n.id;
    }
    void removeNode(int id) {
        nodes.erase(std::remove_if(nodes.begin(), nodes.end(),
                    [&](const VsEditNode& n){ return n.id == id; }), nodes.end());
        execLinks.erase(std::remove_if(execLinks.begin(), execLinks.end(),
                    [&](const VsExecLink& l){ return l.from == id || l.to == id; }), execLinks.end());
        dataLinks.erase(std::remove_if(dataLinks.begin(), dataLinks.end(),
                    [&](const VsDataLink& l){ return l.consumer == id || l.producer == id; }), dataLinks.end());
        dirty = true;
    }
    /// Remove every wire on one pin (kind 0 exec / 1 data, side 0 in / 1 out). Returns how many.
    int unlinkPin(int node, int kind, int side, int pin) {
        std::size_t before = execLinks.size() + dataLinks.size();
        if (kind == 0)
            execLinks.erase(std::remove_if(execLinks.begin(), execLinks.end(), [&](const VsExecLink& l) {
                return side == 1 ? (l.from == node && l.pin == pin) : l.to == node; }), execLinks.end());
        else
            dataLinks.erase(std::remove_if(dataLinks.begin(), dataLinks.end(), [&](const VsDataLink& l) {
                return side == 1 ? (l.producer == node && l.outPin == pin) : (l.consumer == node && l.inPin == pin); }), dataLinks.end());
        int n = (int)(before - execLinks.size() - dataLinks.size());
        if (n) dirty = true;
        return n;
    }
    /// Add a flow wire. An exec output may have any number of wires: they run in the
    /// order they were added (the order of the `exec` lines in the file). The same wire
    /// twice is one wire.
    void linkExec(int from, int pin, int to) {
        for (const auto& l : execLinks) if (l.from == from && l.pin == pin && l.to == to) return;
        execLinks.push_back({from, pin, to}); dirty = true;
    }
    /// Remove one flow wire, leaving the other wires on the same pin. False if there was none.
    bool unlinkExec(int from, int pin, int to) {
        const auto it = std::find_if(execLinks.begin(), execLinks.end(),
                    [&](const VsExecLink& l){ return l.from == from && l.pin == pin && l.to == to; });
        if (it == execLinks.end()) return false;
        execLinks.erase(it); dirty = true; return true;
    }
    void linkData(int consumer, int inPin, int producer, int outPin) {
        dataLinks.erase(std::remove_if(dataLinks.begin(), dataLinks.end(),
                    [&](const VsDataLink& l){ return l.consumer == consumer && l.inPin == inPin; }), dataLinks.end());
        dataLinks.push_back({consumer, inPin, producer, outPin}); dirty = true;
    }

    /// The struct and variable lines of the file (what a Parse of just the declarations needs).
    std::string declText() const {
        std::ostringstream o;
        for (const auto& st : structs) {
            o << "struct " << st.name;
            for (const auto& f : st.fields) {
                std::string tok = f.name + ":" + f.type;
                if (!f.value.empty()) tok += "=" + f.value;
                o << ' ' << okay::vs::QuoteIfNeeded(tok);
            }
            o << "\n";
        }
        for (const auto& v : vars) {
            if (v.typed) {
                o << "decl " << v.name << ' ' << okay::vs::QuoteIfNeeded(v.type) << ' ' << v.scope;
                if (!v.value.empty()) o << ' ' << okay::vs::QuoteText(v.value);   // blank = the type's default
                o << "\n";
            }
            else o << "var " << v.name << ' ' << okay::vs::QuoteIfNeeded(v.value) << "\n";
        }
        return o.str();
    }
    /// The declarations as the runtime reads them. Empty (with `declError()` saying why) when
    /// the declaration lines don't load.
    const std::vector<okay::vs::VarDecl>& typedDecls() const { refreshDecls(); return m_decls; }
    const std::vector<okay::vs::StructDef>& structDefs() const { refreshDecls(); return m_structDefs; }
    const std::string& declError() const { refreshDecls(); return m_declError; }
    /// Add a typed variable at its type's default. False (with `err`) for a bad or repeated name or type.
    bool declare(const std::string& name, const std::string& type, const std::string& scope, std::string* err = nullptr) {
        auto bad = [&](const std::string& m) { if (err) *err = m; return false; };
        if (!okay::vs::VsValidIdent(name)) return bad("'" + name + "' isn't a valid variable name (letters, digits and _, not starting with a digit)");
        okay::vs::VarScope sc;
        if (!okay::vs::VarScopeFromName(scope, sc)) return bad("the scope must be object, scene or global");
        for (const auto& v : vars) if (v.name == name && v.scope == scope) return bad("there is already a " + scope + " variable named '" + name + "'");
        std::string terr;
        okay::vs::VarType vt;
        if (!okay::vs::ParseVarType(type, structDefs(), vt, &terr)) return bad(terr);
        Exposed e; e.name = name; e.type = okay::vs::VarTypeText(vt); e.scope = okay::vs::VarScopeName(sc); e.typed = true;
        e.value = okay::vs::VsToLiteral(okay::vs::VsDefaultFor(vt, structDefs()));
        vars.push_back(e); dirty = true; m_declKey.clear();
        return true;
    }
    /// Change a variable's type; the default is converted when it fits, else reset.
    bool setVarType(std::size_t i, const std::string& type, std::string* err = nullptr) {
        if (i >= vars.size()) return false;
        okay::vs::VarType vt;
        if (!okay::vs::ParseVarType(type, structDefs(), vt, err)) return false;
        okay::vs::VsValue cur;
        const bool had = okay::vs::VsParseTyped(vt, vars[i].value, structDefs(), cur);
        vars[i].type = okay::vs::VarTypeText(vt); vars[i].typed = true;
        vars[i].value = okay::vs::VsToLiteral(had ? cur : okay::vs::VsDefaultFor(vt, structDefs()));
        dirty = true; m_declKey.clear();
        return true;
    }
    /// Set a variable's default from literal text. False (with `err`) when it doesn't fit the type.
    bool setVarDefault(std::size_t i, const std::string& literal, std::string* err = nullptr) {
        if (i >= vars.size()) return false;
        okay::vs::VarType vt;
        if (!okay::vs::ParseVarType(vars[i].type, structDefs(), vt, err)) return false;
        okay::vs::VsValue v;
        if (!okay::vs::VsParseTyped(vt, literal, structDefs(), v, err)) return false;
        vars[i].value = okay::vs::VsToLiteral(v); vars[i].typed = true; dirty = true; m_declKey.clear();
        return true;
    }
    bool setVarScope(std::size_t i, const std::string& scope, std::string* err = nullptr) {
        if (i >= vars.size()) return false;
        okay::vs::VarScope sc;
        if (!okay::vs::VarScopeFromName(scope, sc)) { if (err) *err = "the scope must be object, scene or global"; return false; }
        const std::string name = okay::vs::VarScopeName(sc);
        for (std::size_t k = 0; k < vars.size(); ++k)
            if (k != i && vars[k].name == vars[i].name && vars[k].scope == name) { if (err) *err = "there is already a " + name + " variable named '" + vars[i].name + "'"; return false; }
        vars[i].scope = name; vars[i].typed = true; dirty = true; m_declKey.clear();
        return true;
    }
    bool renameVar(std::size_t i, const std::string& name, std::string* err = nullptr) {
        if (i >= vars.size()) return false;
        if (!okay::vs::VsValidIdent(name)) { if (err) *err = "'" + name + "' isn't a valid variable name"; return false; }
        for (std::size_t k = 0; k < vars.size(); ++k)
            if (k != i && vars[k].name == name && vars[k].scope == vars[i].scope) { if (err) *err = "there is already a variable named '" + name + "'"; return false; }
        const std::string old = vars[i].name;
        vars[i].name = name; dirty = true; m_declKey.clear();
        // Nodes naming the old variable follow the rename.
        for (auto& n : nodes) {
            if (n.arg != old) continue;
            const std::string& t = n.type;
            const bool obj = t == "GetVar" || t == "SetVar" || t == "AddVar" || t == "ToggleVar" || t == "HasVar" || t == "OnVarChanged";
            const bool scn = t == "GetSceneVar" || t == "SetSceneVar" || t == "AddSceneVar" || t == "OnSceneVarChanged";
            const bool glb = t == "GetGlobalVar" || t == "SetGlobalVar" || t == "AddGlobalVar" || t == "OnGlobalVarChanged";
            if ((obj && vars[i].scope == "object") || (scn && vars[i].scope == "scene") || (glb && vars[i].scope == "global")) n.arg = name;
        }
        return true;
    }
    void removeVar(std::size_t i) { if (i < vars.size()) { vars.erase(vars.begin() + (long)i); dirty = true; m_declKey.clear(); } }
    bool declareStruct(const std::string& name, std::string* err = nullptr) {
        if (!okay::vs::VsValidIdent(name)) { if (err) *err = "'" + name + "' isn't a valid struct name"; return false; }
        for (const auto& st : structs) if (st.name == name) { if (err) *err = "there is already a struct named '" + name + "'"; return false; }
        structs.push_back({name, {}}); dirty = true; m_declKey.clear(); return true;
    }
    bool addStructField(std::size_t si, const std::string& name, const std::string& type, std::string* err = nullptr) {
        if (si >= structs.size()) return false;
        if (!okay::vs::VsValidIdent(name)) { if (err) *err = "'" + name + "' isn't a valid field name"; return false; }
        for (const auto& f : structs[si].fields) if (f.name == name) { if (err) *err = "the struct already has a field '" + name + "'"; return false; }
        // A field may use only structs listed before this one (a struct can't hold itself).
        std::vector<okay::vs::StructDef> before;
        for (std::size_t k = 0; k < si; ++k) before.push_back({structs[k].name, {}});
        okay::vs::VarType vt;
        if (!okay::vs::ParseVarType(type, before, vt, err)) return false;
        structs[si].fields.push_back({name, okay::vs::VarTypeText(vt), ""}); dirty = true; m_declKey.clear();
        return true;
    }
    void removeStructField(std::size_t si, std::size_t fi) {
        if (si < structs.size() && fi < structs[si].fields.size()) { structs[si].fields.erase(structs[si].fields.begin() + (long)fi); dirty = true; m_declKey.clear(); }
    }
    /// Remove a struct; variables of that type go back to Any-typed maps.
    void removeStruct(std::size_t si) {
        if (si >= structs.size()) return;
        const std::string n = structs[si].name;
        structs.erase(structs.begin() + (long)si);
        for (auto& v : vars) if (v.type == n) { v.type = "Map"; }
        dirty = true; m_declKey.clear();
    }
    /// Call after editing `vars` / `structs` fields directly.
    void declsChanged() { m_declKey.clear(); dirty = true; }

    std::string toText() const {
        std::ostringstream o;
        o << "# OkayVS graph (edited in the Visual Script editor)\n";
        o << declText();
        for (const auto& n : nodes) {
            o << "node " << n.id << ' ' << n.type;
            if (!n.arg.empty()) o << ' ' << (n.type == "Const" ? VsConstField(n.arg) : okay::vs::QuoteIfNeeded(n.arg));
            o << "\n";
            for (const auto& kv : n.pinDefault)
                if (!kv.second.empty()) o << "default " << n.id << ' ' << kv.first << ' ' << VsLiteral(kv.second) << "\n";
            o << "pos " << n.id << ' ' << VsCoord(n.x) << ' ' << VsCoord(n.y) << "\n";
            // Event nodes register their entry (an On Anim Event / Key / Timer /
            // Custom Event carries its name: "OnKey:space", "OnEvent:Hit").
            std::string en = VsEntryName(n.type, n.arg);
            if (!en.empty()) o << "entry " << okay::vs::QuoteIfNeeded(en) << ' ' << n.id << "\n";
        }
        for (const auto& l : execLinks) o << "exec " << l.from << ' ' << l.pin << ' ' << l.to << "\n";
        for (const auto& l : dataLinks) o << "data " << l.consumer << ' ' << l.inPin << ' ' << l.producer << ' ' << l.outPin << "\n";
        for (const auto& c : comments)
            o << "comment " << VsCoord(c.x) << ' ' << VsCoord(c.y) << ' ' << VsCoord(c.w) << ' ' << VsCoord(c.h) << ' ' << c.color << ' ' << VsQuoted(c.text) << "\n";
        for (const auto& k : knots)
            if (knotWireExists(k)) o << "knot " << (k.exec ? 'e' : 'd') << ' ' << k.a << ' ' << k.b << ' ' << k.c << ' ' << VsCoord(k.x) << ' ' << VsCoord(k.y) << "\n";
        return o.str();
    }
    bool save(const std::string& p) {
        std::ofstream f(p, std::ios::binary);
        if (!f) return false;
        f << toText();
        path = p; dirty = false; return true;
    }
    /// True while the wire a knot sits on is still there.
    bool knotWireExists(const VsKnot& k) const {
        if (k.exec) { for (const auto& l : execLinks) if (l.from == k.a && l.pin == k.b && l.to == k.c) return true; return false; }
        for (const auto& l : dataLinks) if (l.consumer == k.a && l.inPin == k.b) return true;
        return false;
    }
    /// Forget knots whose wire is gone.
    void pruneKnots() {
        knots.erase(std::remove_if(knots.begin(), knots.end(), [&](const VsKnot& k) { return !knotWireExists(k); }), knots.end());
    }
    void clear() { nodes.clear(); execLinks.clear(); dataLinks.clear(); comments.clear(); knots.clear(); vars.clear(); structs.clear(); m_declKey.clear(); nextId = 1; path.clear(); dirty = false; }
    bool load(const std::string& p) {
        std::ifstream f(p, std::ios::binary);
        if (!f) return false;
        std::stringstream ss; ss << f.rdbuf();
        fromText(ss.str());
        path = p; dirty = false;
        return true;
    }
    /// Replace the graph with OkayVS text (keeps the path). Used by load, undo and paste.
    void fromText(const std::string& text) {
        std::string keepPath = path;
        clear(); path = keepPath;
        std::istringstream f(text);
        std::string line;
        int maxId = 0;
        while (std::getline(f, line)) {
            // A '#' starts a comment line; inside quotes it's text (Format "#{0}").
            if (!line.empty() && line.find_first_not_of(" \t") != std::string::npos &&
                line[line.find_first_not_of(" \t")] == '#') continue;
            std::istringstream ls(line); std::string kind; ls >> kind;
            auto restOf = [&](std::istringstream& is) {   // the rest of the line, unquoted
                std::string r; std::getline(is, r);
                std::size_t a = r.find_first_not_of(" \t"); if (a == std::string::npos) return std::string();
                r = r.substr(a);
                while (!r.empty() && (r.back() == ' ' || r.back() == '\t' || r.back() == '\r')) r.pop_back();
                std::string un;
                if (okay::vs::ParseQuoted(r, un)) return un;   // one "quoted" field: unescape it
                if (r.size() >= 2 && r.front() == '"' && r.back() == '"') r = r.substr(1, r.size() - 2);
                return r;
            };
            if (kind == "node") {
                VsEditNode n;
                if (!(ls >> n.id >> n.type) || n.id < 0 || n.id == std::numeric_limits<int>::max()) continue;   // no id to give the next node
                n.arg = restOf(ls);
                nodes.push_back(n); maxId = std::max(maxId, n.id);
            } else if (kind == "default") {
                int id = 0, pin = 0; ls >> id >> pin;
                std::string v = restOf(ls);
                if (auto* n = find(id)) n->pinDefault[pin] = v;
            } else if (kind == "pos") {
                int id = 0; float x = 0, y = 0;
                if (!(ls >> id) || !VsReadCoord(ls, x) || !VsReadCoord(ls, y)) continue;
                if (auto* n = find(id)) { n->x = x; n->y = y; }
            } else if (kind == "exec") {
                VsExecLink l{};
                if (ls >> l.from >> l.pin >> l.to) execLinks.push_back(l);
            } else if (kind == "data") {
                VsDataLink l{};
                if (ls >> l.consumer >> l.inPin >> l.producer >> l.outPin) dataLinks.push_back(l);
            } else if (kind == "var") {
                std::string name;
                ls >> name;
                std::string v = restOf(ls);
                if (!name.empty()) {
                    Exposed e; e.name = name; e.value = v;
                    float probe = 0;
                    e.type = (v == "true" || v == "false") ? "Bool" : okay::vs::VsParseFinite(v, probe) ? "Number" : "Text";
                    vars.push_back(e);
                }
            } else if (kind == "decl" || kind == "struct") {
                const std::vector<std::string> t = okay::vs::TokenizeLine(line);
                auto unq = [](std::string x) { if (x.size() >= 2 && x.front() == '"' && x.back() == '"') x = x.substr(1, x.size() - 2); return x; };
                if (kind == "decl" && t.size() >= 4) {
                    Exposed e; e.name = unq(t[1]); e.type = unq(t[2]); e.scope = unq(t[3]); e.typed = true;
                    e.value = t.size() > 4 ? unq(t[4]) : std::string();
                    if (!e.name.empty()) vars.push_back(e);
                } else if (kind == "struct" && t.size() >= 2) {
                    StructEd st; st.name = unq(t[1]);
                    for (std::size_t k = 2; k < t.size(); ++k) {
                        const std::string f = unq(t[k]);
                        const auto colon = f.find(':');
                        if (colon == std::string::npos) continue;
                        StructFieldEd sf; sf.name = f.substr(0, colon);
                        std::string ty = f.substr(colon + 1);
                        const auto eq = ty.find('=');
                        if (eq != std::string::npos) { sf.value = ty.substr(eq + 1); ty = ty.substr(0, eq); }
                        sf.type = ty;
                        st.fields.push_back(sf);
                    }
                    if (!st.name.empty()) structs.push_back(st);
                }
            } else if (kind == "comment") {
                VsComment c;
                if (!VsReadCoord(ls, c.x) || !VsReadCoord(ls, c.y) || !VsReadCoord(ls, c.w) || !VsReadCoord(ls, c.h)) continue;
                ls >> c.color; ls.clear();
                c.text = restOf(ls);
                comments.push_back(c);
            } else if (kind == "knot") {
                VsKnot k; std::string what;
                if (!(ls >> what >> k.a >> k.b >> k.c) || (what != "e" && what != "d") || !VsReadCoord(ls, k.x) || !VsReadCoord(ls, k.y)) continue;
                k.exec = what == "e";
                knots.push_back(k);
            }
        }
        // Wires to nodes that weren't loaded (skipped ids, or never there) would
        // make the next save fail to load: drop them.
        auto has = [&](int id) { return find(id) != nullptr; };
        execLinks.erase(std::remove_if(execLinks.begin(), execLinks.end(),
                        [&](const VsExecLink& l) { return !has(l.from) || !has(l.to); }), execLinks.end());
        dataLinks.erase(std::remove_if(dataLinks.begin(), dataLinks.end(),
                        [&](const VsDataLink& l) { return !has(l.consumer) || !has(l.producer); }), dataLinks.end());
        pruneKnots();
        // Switch Default used to be pin 4. With fewer cases it is the pin after the last case.
        for (const auto& n : nodes) {
            if (!IsSwitchNode(n.type)) continue;
            const int ncases = (int)VsEditorSwitchCases(n.arg).size();
            if (ncases >= 4) continue;
            bool dest = false;
            for (const auto& l : execLinks) if (l.from == n.id && l.pin == ncases) dest = true;
            if (dest) continue;
            for (auto& l : execLinks) if (l.from == n.id && l.pin == 4) l.pin = ncases;
        }
        // Quick blocks saved by an earlier release: bring their typed values up to the current fields.
        for (auto& n : nodes) if (okay::vs::FindQuickBlock(n.type)) okay::vs::QbUpgradeFields(n.type, n.pinDefault);
        // Old Show / Hide, Move and Turn nodes with only typed values become the one merged block (nothing wired into them).
        for (auto& n : nodes) {
            bool wired = false;
            for (const auto& l : dataLinks) if (l.consumer == n.id) wired = true;
            std::string newType; std::map<int, std::string> newPins;
            if (!wired && okay::vs::QbConvertOldNode(n.type, n.arg, n.pinDefault, newType, newPins)) {
                n.type = newType; n.arg.clear(); n.pinDefault = newPins;
            }
        }
        nextId = maxId + 1;
    }
};

/// The names of the saved values a graph already uses (typed on Get / Set / Add Saved Value blocks, or in the Name field
/// of a Quick block), each once, in the order first used. The Name field offers these, so a typo cannot make a new one.
inline std::vector<std::string> VsUsedValueNames(const VsEditGraph& g) {
    std::vector<std::string> out;
    auto add = [&](const std::string& name) {
        if (name.empty()) return;
        for (const std::string& o : out) if (o == name) return;
        out.push_back(name);
    };
    for (const VsEditNode& n : g.nodes) {
        if (n.type == "GetVar" || n.type == "SetVar" || n.type == "AddVar" || n.type == "Toggle" || n.type == "HasVar") add(n.arg);
        if (const okay::vs::QuickBlock* qb = okay::vs::FindQuickBlock(n.type)) {
            const auto fields = okay::vs::QbAllFields(*qb);
            for (std::size_t i = 0; i < fields.size(); ++i) {
                if (fields[i].kind != okay::vs::QbKind::Name) continue;
                auto it = n.pinDefault.find((int)i);
                add(okay::vs::QbNameBare(it != n.pinDefault.end() && !it->second.empty() ? it->second : std::string(fields[i].def)));
            }
        }
    }
    return out;
}

// The graph a new visual script starts with: the same thing the starter text
// script does, so every node is doing a job. On Start prints that the object is
// ready; On Update moves it with WASD / the arrow keys at `speed` units a second
// (Axis x speed x Delta Time on each axis).
inline void SeedStarterGraph(VsEditGraph& g) {
    g.clear();
    const int start = g.add("OnStart", 40, 40);
    const int print = g.add("Print", 300, 40);
    if (auto* n = g.find(print)) n->pinDefault[0] = "Ready! Move with WASD or the arrow keys";
    const int update = g.add("OnUpdate", 40, 200);
    const int move = g.add("Translate", 820, 200);
    const int speed = g.add("Const", 40, 340);
    if (auto* n = g.find(speed)) n->arg = "5";
    const int ax = g.add("AxisX", 40, 440), ay = g.add("AxisY", 40, 540), dt = g.add("DeltaTime", 40, 640);
    const int mx = g.add("Mul", 300, 380), my = g.add("Mul", 300, 540);
    const int mxdt = g.add("Mul", 560, 380), mydt = g.add("Mul", 560, 540);
    g.linkExec(start, 0, print);
    g.linkExec(update, 0, move);
    g.linkData(mx, 0, ax, 0);   g.linkData(mx, 1, speed, 0);
    g.linkData(my, 0, ay, 0);   g.linkData(my, 1, speed, 0);
    g.linkData(mxdt, 0, mx, 0); g.linkData(mxdt, 1, dt, 0);
    g.linkData(mydt, 0, my, 0); g.linkData(mydt, 1, dt, 0);
    g.linkData(move, 0, mxdt, 0);
    g.linkData(move, 1, mydt, 0);
    if (auto* n = g.find(move)) n->pinDefault[2] = "0";
}

// ---- Graph -> OkayScript compiler ------------------------------------------
// Turns a node/flow graph into readable OkayScript so the two scripting
// front-ends describe the same thing: data pins become expressions, the exec
// chain from On Start / On Update becomes start()/update(dt) statements. The
// output is plain OkayScript the engine already runs — people can keep wiring
// nodes, or drop into text, whichever they prefer.
inline const VsEditNode* VsFind(const VsEditGraph& g, int id) {
    // Asked several times per wire per frame (pin types, wire checks): a scan of every node each time made a big graph's frame grow
    // with nodes x wires. An id -> position index, checked on every use and rebuilt when the nodes changed (added, removed, reloaded).
    static const VsEditGraph* s_graph = nullptr;
    static std::unordered_map<int, std::size_t> s_index;
    if (s_graph == &g) {
        auto it = s_index.find(id);
        if (it != s_index.end() && it->second < g.nodes.size() && g.nodes[it->second].id == id) return &g.nodes[it->second];
        if (it == s_index.end() && s_index.size() == g.nodes.size()) {   // probably an id that is gone: a plain scan, no rebuild
            bool present = false;
            for (const auto& n : g.nodes) if (n.id == id) { present = true; break; }
            if (!present) return nullptr;
        }
    }
    s_graph = &g;
    s_index.clear();
    const VsEditNode* hit = nullptr;
    for (std::size_t i = 0; i < g.nodes.size(); ++i) {
        s_index[g.nodes[i].id] = i;
        if (g.nodes[i].id == id) hit = &g.nodes[i];
    }
    return hit;
}
// ---- Functions: the signature a Function / Call / Return node uses ----------
inline bool IsFunctionNode(const std::string& t) { return t == "FunctionStart" || t == "CallFunction" || t == "Return"; }
/// The signature of the function `n` is (Function node) or uses (Call / Return).
inline bool VsSigFor(const VsEditGraph& g, const VsEditNode& n, okay::vs::FunctionSig& out) {
    if (n.type == "FunctionStart") return okay::vs::ParseFunctionSignature(n.arg, out);
    const std::string want = okay::vs::FunctionNameOf(n.arg);
    if (want.empty()) return false;
    for (const auto& m : g.nodes)
        if (m.type == "FunctionStart" && okay::vs::FunctionNameOf(m.arg) == want) return okay::vs::ParseFunctionSignature(m.arg, out);
    return false;
}
/// The node's pins: the registry entry, or for function nodes one built from
/// the signature (parameters / return values become data pins).
inline bool IsSwitchNode(const std::string& t) { return t == "SwitchString" || t == "SwitchCases"; }
/// Cases in a Switch node's text field (comma separated). Empty pieces are dropped.
inline std::vector<std::string> VsEditorSwitchCases(const std::string& arg) {
    std::vector<std::string> out;
    std::string cur;
    auto flush = [&] {
        std::size_t b = 0, e = cur.size();
        while (b < e && std::isspace(static_cast<unsigned char>(cur[b]))) ++b;
        while (e > b && std::isspace(static_cast<unsigned char>(cur[e - 1]))) --e;
        if (e > b) out.push_back(cur.substr(b, e - b));
        cur.clear();
    };
    for (char c : arg) { if (c == ',') flush(); else cur.push_back(c); }
    if (!cur.empty()) flush();
    if (out.size() > 63) out.resize(63);
    return out;
}
/// Default is the pin after the last case. Moving the list moves that wire with it.
inline void VsMoveSwitchDefault(VsEditGraph& g, int node, int oldDef, int newDef) {
    if (oldDef == newDef) return;
    for (auto& l : g.execLinks)
        if (l.from == node && l.pin == oldDef) l.pin = newDef;
}
inline const VsDef* VsDefFor(const VsEditGraph& g, const VsEditNode& n) {
    const VsDef* base = FindDef(n.type);
    if (!base || (!IsFunctionNode(n.type) && !IsSwitchNode(n.type))) return base;
    struct Dyn { std::string key; std::vector<std::string> labels; VsDef def; };
    static std::map<int, std::unique_ptr<Dyn>> cache;   // node id -> its pins
    okay::vs::FunctionSig sig;
    const bool ok = VsSigFor(g, n, sig);
    std::string key = n.type + "|" + n.arg + "|";
    if (ok) {
        for (const auto& p : sig.params) key += p.name + ",";
        key += "|";
        for (const auto& p : sig.returns) key += p.name + ",";
    }
    if (IsSwitchNode(n.type)) key = n.type + "|sw|" + n.arg;
    auto& slot = cache[n.id];
    if (slot && slot->key == key) return &slot->def;
    slot = std::make_unique<Dyn>();
    slot->key = key;
    slot->def = *base;
    if (IsSwitchNode(n.type)) {
        const auto cases = VsEditorSwitchCases(n.arg);
        slot->labels.reserve(cases.size() + 1);
        for (const auto& c : cases) slot->labels.push_back(c);
        slot->labels.push_back("Default");
        slot->def.execOut.clear();
        for (const auto& s : slot->labels) slot->def.execOut.push_back(s.c_str());
        return &slot->def;
    }
    if (ok) {
        const auto& ins  = n.type == "FunctionStart" ? std::vector<okay::vs::FunctionSig::Pin>{} : n.type == "Return" ? sig.returns : sig.params;
        const auto& outs = n.type == "FunctionStart" ? sig.params : n.type == "Return" ? std::vector<okay::vs::FunctionSig::Pin>{} : sig.returns;
        slot->labels.reserve(ins.size() + outs.size());
        for (const auto& p : ins) slot->labels.push_back(p.name);
        for (const auto& p : outs) slot->labels.push_back(p.name);
        slot->def.dataIn.clear(); slot->def.dataOut.clear();
        for (std::size_t i = 0; i < ins.size(); ++i) slot->def.dataIn.push_back(slot->labels[i].c_str());
        for (std::size_t i = 0; i < outs.size(); ++i) slot->def.dataOut.push_back(slot->labels[ins.size() + i].c_str());
    }
    return &slot->def;
}
inline okay::vs::PinType VsOutPinType(const VsEditGraph& g, int nodeId, int pin) {
    const VsEditNode* n = VsFind(g, nodeId);
    if (!n) return okay::vs::PinType::Any;
    if (n->type == "FunctionStart" || n->type == "CallFunction") {
        okay::vs::FunctionSig sig;
        if (!VsSigFor(g, *n, sig)) return okay::vs::PinType::Any;
        const auto& pins = n->type == "FunctionStart" ? sig.params : sig.returns;
        return pin >= 0 && pin < (int)pins.size() ? pins[(std::size_t)pin].type : okay::vs::PinType::Any;
    }
    if (const auto dt = okay::vs::DeclOutType(n->type, n->arg, pin, g.typedDecls()); dt != okay::vs::PinType::Any) return dt;
    return okay::vs::OutputType(n->type, pin, n->arg);
}
inline okay::vs::PinType VsInPinType(const VsEditGraph& g, int nodeId, int pin) {
    const VsEditNode* n = VsFind(g, nodeId);
    if (!n) return okay::vs::PinType::Any;
    if (n->type == "Return" || n->type == "CallFunction") {
        okay::vs::FunctionSig sig;
        if (!VsSigFor(g, *n, sig)) return okay::vs::PinType::Any;
        const auto& pins = n->type == "Return" ? sig.returns : sig.params;
        return pin >= 0 && pin < (int)pins.size() ? pins[(std::size_t)pin].type : okay::vs::PinType::Any;
    }
    if (const auto dt = okay::vs::DeclInType(n->type, n->arg, pin, g.typedDecls()); dt != okay::vs::PinType::Any) return dt;
    return okay::vs::InputType(n->type, pin);
}
inline int VsExecTarget(const VsEditGraph& g, int fromNode, int pin) {
    for (const auto& l : g.execLinks) if (l.from == fromNode && l.pin == pin) return l.to;
    return -1;
}
/// Every node an exec output leads to, in the order they run.
inline std::vector<int> VsExecTargets(const VsEditGraph& g, int fromNode, int pin) {
    std::vector<int> out;
    for (const auto& l : g.execLinks) if (l.from == fromNode && l.pin == pin) out.push_back(l.to);
    return out;
}
inline std::string VsDataExpr(const VsEditGraph& g, int nodeId, int outPin = 0);
inline const VsEditNode* VsFind(const VsEditGraph& g, int id);
inline std::string VsInput(const VsEditGraph& g, int consumer, int inPin, const char* dflt = "0") {
    for (const auto& l : g.dataLinks)
        if (l.consumer == consumer && l.inPin == inPin)
            return VsDataExpr(g, l.producer, l.outPin);
    if (const VsEditNode* n = VsFind(g, consumer)) {
        auto it = n->pinDefault.find(inPin);
        if (it != n->pinDefault.end() && !it->second.empty()) return VsLiteral(it->second);
    }
    return dflt;
}
inline bool VsInputLinkedOrSet(const VsEditGraph& g, int nodeId, int pin) {
    for (const auto& l : g.dataLinks) if (l.consumer == nodeId && l.inPin == pin) return true;
    const VsEditNode* n = VsFind(g, nodeId);
    if (!n) return false;
    auto it = n->pinDefault.find(pin);
    return it != n->pinDefault.end() && !it->second.empty();
}
// ---- Literal / name helpers for the generated OkayScript ----------------------
// A string literal: backslashes, quotes and line breaks escaped (whatever was typed).
inline std::string VsStrLit(const std::string& s) {
    std::string o = "\"";
    for (char c : s) {
        if (c == '\\' || c == '"') { o += '\\'; o += c; }
        else if (c == '\n') o += "\\n";
        else if (c == '\t') o += "\\t";
        else if (c == '\r') {}
        else o += c;
    }
    return o + "\"";
}
inline bool VsIsNumber(const std::string& v) { return VsIsNumberOrBool(v) && v != "true" && v != "false"; }
// A number field (seconds, counts, ports): the text if it's a number, else `def`
// (the node is also listed as NOT TRANSLATED, see VsBadField).
inline std::string VsNum(const std::string& v, const char* def) { return VsIsNumber(v) ? v : std::string(def); }
// A variable name as an identifier: other characters become '_', a leading
// digit or a keyword gets a '_' in front ("my var" -> my_var, "if" -> _if).
inline std::string VsIdent(const std::string& v) {
    static const std::set<std::string> kWords = {
        "if", "else", "while", "for", "foreach", "in", "do", "return", "break", "continue", "var", "let", "const",
        "func", "function", "fn", "void", "int", "float", "double", "string", "bool", "true", "false", "null",
        "new", "class", "struct", "this", "self", "and", "or", "not", "switch", "case", "default", "public",
        "private", "static", "static", "using", "import", "export", "try", "catch", "throw", "yield", "is", "as"};
    std::string id;
    for (char c : v) id += (std::isalnum((unsigned char)c) || c == '_') ? c : '_';
    if (id.empty()) id = "x";
    if (std::isdigit((unsigned char)id[0]) || kWords.count(id)) id = "_" + id;
    return id;
}
// A Const node's value: numbers / bools as they are, anything else as text.
inline std::string VsConstExpr(const std::string& a) {
    if (a.empty()) return "0";
    if (VsIsNumberOrBool(a)) return a;
    if (a.size() >= 2 && a.front() == '"' && a.back() == '"') return VsStrLit(a.substr(1, a.size() - 2));
    return VsStrLit(a);
}
// Text going into a // comment: one line.
inline std::string VsCommentText(std::string v) {
    for (char& c : v) if (c == '\n' || c == '\r') c = ' ';
    return v;
}
inline bool VsIsCompareOp(const std::string& op) {
    return op == ">" || op == "<" || op == ">=" || op == "<=" || op == "==" || op == "!=";
}
// A field the generated code can't use as typed (a number field that isn't a
// number, an unknown comparison): the node is listed as NOT TRANSLATED.
inline bool VsBadField(const VsEditNode& n) {
    static const std::set<std::string> kNumber = {"OnTimer", "Timer", "Wait", "Delay", "Repeat", "DoN", "Chance",
                                                  "ToFixed", "MouseButton", "MouseButtonDown", "NetHost", "Cooldown", "EveryN"};
    if (n.arg.empty()) return false;
    if (kNumber.count(n.type)) return !VsIsNumber(n.arg);
    if (n.type == "Compare") return !VsIsCompareOp(n.arg);
    if (n.type == "NetJoin") {
        const std::size_t cut = n.arg.find_first_of(" :");
        return cut != std::string::npos && !VsIsNumber(n.arg.substr(cut + 1));
    }
    return false;
}

inline std::string VsDataExpr(const VsEditGraph& g, int nodeId, int outPin) {
    const VsEditNode* n = VsFind(g, nodeId);
    if (!n) return "0";
    const std::string& t = n->type;
    auto A = [&]{ return VsInput(g, nodeId, 0); };
    auto B = [&]{ return VsInput(g, nodeId, 1); };
    auto C = [&]{ return VsInput(g, nodeId, 2); };
    auto lit = [&](const char* d){ return n->arg.empty() ? std::string(d) : n->arg; };
    auto q   = [&](const std::string& s){ return VsStrLit(s); };

    if (t == "Const")      return VsConstExpr(n->arg);
    if (t == "GetVar")     return VsIdent(lit("x"));
    if (t == "Add")        return "(" + A() + " + " + B() + ")";
    if (t == "Sub")        return "(" + A() + " - " + B() + ")";
    if (t == "Mul")        return "(" + A() + " * " + B() + ")";
    if (t == "Div")        return "(" + A() + " / " + B() + ")";
    if (t == "Mod")        return "(" + A() + " % " + B() + ")";
    if (t == "Pow")        return "pow(" + A() + ", " + B() + ")";
    if (t == "Min")        return "min(" + A() + ", " + B() + ")";
    if (t == "Max")        return "max(" + A() + ", " + B() + ")";
    if (t == "Abs")        return "abs(" + A() + ")";
    if (t == "Neg")        return "(-" + A() + ")";
    if (t == "Sqrt")       return "sqrt(" + A() + ")";
    if (t == "Sin")        return "sin(" + A() + ")";
    if (t == "Cos")        return "cos(" + A() + ")";
    if (t == "Floor")      return "floor(" + A() + ")";
    if (t == "Round")      return "round(" + A() + ")";
    if (t == "Sign")       return "sign(" + A() + ")";
    if (t == "Clamp")      return "clamp(" + A() + ", " + B() + ", " + C() + ")";
    if (t == "Lerp")       return "lerp(" + A() + ", " + B() + ", " + C() + ")";
    if (t == "Random")     return "random()";
    if (t == "RandomRange")return "random_range(" + A() + ", " + B() + ")";
    if (t == "Compare")    return "(" + A() + " " + (VsIsCompareOp(lit(">")) ? lit(">") : std::string(">")) + " " + B() + ")";
    if (t == "Equals")     return "(" + A() + " == " + B() + ")";
    if (t == "NotEquals")  return "(" + A() + " != " + B() + ")";
    if (t == "And")        return "(" + A() + " && " + B() + ")";
    if (t == "Or")         return "(" + A() + " || " + B() + ")";
    if (t == "Xor")        return "(!" + A() + " != !" + B() + ")";
    if (t == "Not")        return "(!" + A() + ")";
    if (t == "Approx")     return "(abs(" + A() + " - " + B() + ") < 0.0001)";
    if (t == "Between")    return "(" + A() + " >= " + B() + " && " + A() + " <= " + C() + ")";
    if (t == "Chance")     return "(random() < " + VsNum(lit("0.5"), "0.5") + ")";
    if (t == "HasVar")     return "has_var(" + q(lit("x")) + ")";
    if (t == "Select")     return "(" + A() + " ? " + B() + " : " + C() + ")";
    if (t == "Concat")     return "(str(" + A() + ") + str(" + B() + "))";
    if (t == "Key")        return "key_down(" + q(lit("space")) + ")";
    if (t == "KeyDown")    return "key_pressed(" + q(lit("space")) + ")";
    if (t == "KeyUp")      return "key_up(" + q(lit("space")) + ")";
    if (t == "AxisX")      return "axis_x()";
    if (t == "AxisY")      return "axis_y()";
    if (t == "MouseX")     return "mouse_x()";
    if (t == "MouseY")     return "mouse_y()";
    if (t == "Time")       return "time()";
    if (t == "DeltaTime")  return "Time.deltaTime";
    if (t == "VelX")       return "velocity_x()";
    if (t == "VelY")       return "velocity_y()";
    if (t == "VelZ")       return "velocity_z()";
    if (t == "GetX")       return "transform.position.x";
    if (t == "GetY")       return "transform.position.y";
    if (t == "GetZ")       return "transform.position.z";
    if (t == "GetPosition")return "transform.position";
    if (t == "MakeVec3")   return "vec3(" + A() + ", " + B() + ", " + C() + ")";
    if (t == "VecX")       return "vec_x(" + A() + ")";
    if (t == "VecY")       return "vec_y(" + A() + ")";
    if (t == "VecZ")       return "vec_z(" + A() + ")";
    if (t == "EaseIn")     return "(" + A() + " * " + A() + ")";
    if (t == "EaseOut")  { std::string a = A(); return "(1 - (1-" + a + ")*(1-" + a + "))"; }
    if (t == "EaseInOut"){ std::string a = A(); return "(" + a + "*" + a + "*(3 - 2*" + a + "))"; }
    auto D = [&]{ return VsInput(g, nodeId, 3); };
    auto E = [&]{ return VsInput(g, nodeId, 4); };
    if (t == "Tan")        return "tan(" + A() + ")";
    if (t == "Atan2")      return "atan2(" + A() + ", " + B() + ")";
    if (t == "Exp")        return "exp(" + A() + ")";
    if (t == "Log")        return "log(" + A() + ")";
    if (t == "InverseLerp")return "inverse_lerp(" + A() + ", " + B() + ", " + C() + ")";
    if (t == "Remap")      return "remap(" + A() + ", " + B() + ", " + C() + ", " + D() + ", " + E() + ")";
    if (t == "SmoothStep") return "smoothstep(" + A() + ", " + B() + ", " + C() + ")";
    if (t == "PingPong")   return "ping_pong(" + A() + ", " + B() + ")";
    if (t == "Repeat01")   return "repeat(" + A() + ", " + B() + ")";
    if (t == "MoveTowardsValue") return "move_towards(" + A() + ", " + B() + ", " + C() + ")";
    if (t == "DeltaAngle") return "delta_angle(" + A() + ", " + B() + ")";
    if (t == "Vec3Add")    return "(" + A() + " + " + B() + ")";
    if (t == "Vec3Sub")    return "(" + A() + " - " + B() + ")";
    if (t == "Vec3Scale")  return "(" + A() + " * " + B() + ")";
    if (t == "Vec3Length") return "vec_length(" + A() + ")";
    if (t == "Vec3Normalize") return "vec_normalize(" + A() + ")";
    if (t == "Vec3Dot")    return "dot(" + A() + ", " + B() + ")";
    if (t == "Vec3Distance") return "distance(" + A() + ", " + B() + ")";
    if (t == "ToString")   return "str(" + A() + ")";
    if (t == "ToFixed")    return "to_fixed(" + A() + ", " + VsNum(lit("2"), "2") + ")";
    if (t == "TextLength") return "len(" + A() + ")";
    if (t == "Contains")   return "str_contains(" + A() + ", " + B() + ")";
    if (t == "Upper")      return "upper(" + A() + ")";
    if (t == "Lower")      return "lower(" + A() + ")";
    if (t == "ObjectExists")   return "exists(" + q(lit("")) + ")";
    if (t == "ObjectPosition") return "position_of(" + q(lit("self")) + ")";
    if (t == "DistanceTo")     return "distance_to(" + q(lit("")) + ")";
    if (t == "SelfName")       return "gameObject.name";
    if (t == "OtherName")      return "other";
    if (t == "AnimEventName")  return "anim_event";
    if (t == "OtherTag")       return "tag_of(other)";
    if (t == "MouseButton")    return "mouse_held(" + VsNum(lit("0"), "0") + ")";
    if (t == "MouseButtonDown")return "mouse_down(" + VsNum(lit("0"), "0") + ")";
    if (t == "AnimState")      return "anim_state()";
    if (t == "NavArrived")     return "nav_arrived()";
    if (t == "GetStoryVar")    return "get_story(" + q(lit("")) + ")";
    if (t == "QuestState")     return "quest_state(" + q(lit("")) + ")";
    if (t == "BtGet")          return "bt_get(" + q(lit("x")) + ")";
    if (t == "GetSplinePoint") return "spline_point(" + A() + ")";
    if (t == "SplineLength")   return "spline_length()";
    if (t == "IsInAction")     return "is_in_action()";
    if (t == "IsSitting")      return "is_sitting()";
    if (t == "SeatPrompt")     return "seat_prompt()";
    if (t == "IsHanging")      return "is_hanging()";
    // Third wave.
    const std::string id = std::to_string(nodeId);
    if (t == "ForLoop")    return "_i" + id;
    if (t == "DoN")        return "_don_" + id;
    if (t == "RandomInt")  return "randi(" + VsInput(g, nodeId, 0, "0") + ", " + VsInput(g, nodeId, 1, "10") + ")";
    if (t == "Ceil")       return "ceil(" + A() + ")";
    if (t == "Frac")       return "fract(" + A() + ")";
    if (t == "OneMinus")   return "(1 - " + A() + ")";
    if (t == "Saturate")   return "clamp01(" + A() + ")";
    if (t == "Asin")       return "asin(" + A() + ")";
    if (t == "Acos")       return "acos(" + A() + ")";
    if (t == "Deg2Rad")    return "deg2rad(" + A() + ")";
    if (t == "Rad2Deg")    return "rad2deg(" + A() + ")";
    if (t == "Vec3Cross")  return "vec_cross(" + A() + ", " + B() + ")";
    if (t == "Vec3Lerp")   return "vec_lerp(" + A() + ", " + B() + ", " + VsInput(g, nodeId, 2, "0.5") + ")";
    if (t == "Vec3Negate") return "(" + A() + " * -1)";
    if (t == "ForwardVector") return "transform.forward";
    if (t == "RightVector")   return "transform.right";
    if (t == "UpVector")      return "transform.up";
    if (t == "GetScale")      return "transform.localScale";
    if (t == "GetRotation")   return "rotation_euler()";
    if (t == "GetBlendShape") return "blend_shape_get(" + q(lit("")) + ")";
    if (t == "Raycast")       return "raycast_hit(" + A() + ", " + B() + ", " + C() + ", " + D() + ", " + VsInput(g, nodeId, 4, "1000000") + ")";
    // 3D casts / overlaps: one ray3_cast / overlap3 call per wired output pin
    // (they skip this object, like the nodes). Unwired Origin / Direction start
    // at this object, looking forward.
    if (t == "Raycast3D" || t == "SphereCast3D" || t == "RaycastAll3D") {
        static const char* kWhat[] = {"hit", "point", "normal", "distance", "object"};
        const std::string o = VsInput(g, nodeId, 0, "transform.position");
        const std::string d = VsInput(g, nodeId, 1, "transform.forward");
        const std::string layers = q(lit(""));
        if (t == "RaycastAll3D") {
            std::string all = "ray3_cast(\"all\", " + o + ", " + d + ", " + VsInput(g, nodeId, 2, "100") + ", " + layers + ")";
            if (outPin == 0) return "count(" + all + ")";
            if (outPin == 1) return all;
            return "ray3_cast(\"object\", " + o + ", " + d + ", " + VsInput(g, nodeId, 2, "100") + ", " + layers + ")";
        }
        const std::string what = q(kWhat[outPin >= 0 && outPin < 5 ? outPin : 0]);
        if (t == "Raycast3D")
            return "ray3_cast(" + what + ", " + o + ", " + d + ", " + VsInput(g, nodeId, 2, "100") + ", " +
                   layers + ", " + VsInput(g, nodeId, 3, "false") + ")";
        return "ray3_cast(" + what + ", " + o + ", " + d + ", " + VsInput(g, nodeId, 3, "100") + ", " +
               layers + ", true, " + VsInput(g, nodeId, 2, "0.5") + ")";
    }
    if (t == "OverlapSphere3D" || t == "OverlapBox3D") {
        const bool box = t == "OverlapBox3D";
        std::string call = "overlap3(" + q(box ? "box" : "sphere") + ", " + VsInput(g, nodeId, 0, "transform.position") +
                           ", " + VsInput(g, nodeId, 1, box ? "vec3(0.5, 0.5, 0.5)" : "1") + ", " + q(lit("")) + ")";
        return outPin == 0 ? "count(" + call + ")" : call;
    }
    // Flat casts. A leading word selects the output, the same way ray3_cast does.
    if (t == "BoxCast2D" || t == "CapsuleCast2D") {
        auto xy = [&](int pin, const char* def, char axis) {
            return std::string("vec_") + axis + "(" + VsInput(g, nodeId, pin, def) + ")";
        };
        static const char* kWhat[] = {"hit", "point", "normal", "distance", "object"};
        const int w = outPin >= 0 && outPin < 5 ? outPin : 0;
        const std::string layers = q(lit(""));
        if (t == "BoxCast2D")
            return "box_cast(" + q(kWhat[w]) + ", " + xy(0, "transform.position", 'x') + ", " +
                   xy(0, "transform.position", 'y') + ", " + xy(1, "vec3(0.5, 0.5, 0)", 'x') + ", " +
                   xy(1, "vec3(0.5, 0.5, 0)", 'y') + ", " + xy(2, "vec3(1, 0, 0)", 'x') + ", " +
                   xy(2, "vec3(1, 0, 0)", 'y') + ", " + VsInput(g, nodeId, 4, "100") + ", " +
                   layers + ", true, " + VsInput(g, nodeId, 3, "0") + ")";
        return "capsule_cast(" + q(kWhat[w]) + ", " + xy(0, "vec3(pos_x(), pos_y() - 0.5, 0)", 'x') + ", " +
               xy(0, "vec3(pos_x(), pos_y() - 0.5, 0)", 'y') + ", " +
               xy(1, "vec3(pos_x(), pos_y() + 0.5, 0)", 'x') + ", " +
               xy(1, "vec3(pos_x(), pos_y() + 0.5, 0)", 'y') + ", " +
               VsInput(g, nodeId, 2, "0.5") + ", " + xy(3, "vec3(1, 0, 0)", 'x') + ", " +
               xy(3, "vec3(1, 0, 0)", 'y') + ", " + VsInput(g, nodeId, 4, "100") + ", " + layers + ", true)";
    }
    if (t == "OverlapBox2D" || t == "OverlapCapsule2D") {
        auto xy = [&](int pin, const char* def, char axis) {
            return std::string("vec_") + axis + "(" + VsInput(g, nodeId, pin, def) + ")";
        };
        std::string call = t == "OverlapBox2D"
            ? "overlap_box(" + xy(0, "transform.position", 'x') + ", " + xy(0, "transform.position", 'y') + ", " +
              xy(1, "vec3(0.5, 0.5, 0)", 'x') + ", " + xy(1, "vec3(0.5, 0.5, 0)", 'y') + ", \"\", false, " +
              VsInput(g, nodeId, 2, "0") + ")"
            : "overlap_capsule(" + xy(0, "vec3(pos_x(), pos_y() - 0.5, 0)", 'x') + ", " +
              xy(0, "vec3(pos_x(), pos_y() - 0.5, 0)", 'y') + ", " +
              xy(1, "vec3(pos_x(), pos_y() + 0.5, 0)", 'x') + ", " +
              xy(1, "vec3(pos_x(), pos_y() + 0.5, 0)", 'y') + ", " +
              VsInput(g, nodeId, 2, "0.5") + ")";
        return outPin == 0 ? "count(" + call + ")" : call;
    }
    if (t == "ContactPoint")  return "contact_point";
    if (t == "ContactNormal") return "contact_normal";
    if (t == "ImpactSpeed")   return "impact_speed";
    if (t == "MouseWheel")    return "mouse_wheel()";
    if (t == "TimeScale")     return "timescale()";
    if (t == "FrameCount")    return "frame_count()";
    if (t == "NetConnected")  return "net_connected()";
    if (t == "NetIsServer")   return "net_is_server()";
    if (t == "NetIsClient")   return "net_is_client()";
    if (t == "NetPeers")      return "net_peers()";
    if (t == "NetId")         return "net_id()";
    if (t == "NetGetVar")     return "net_get(" + q(lit("x")) + ")";
    if (t == "SyncVarGet")    return "net_syncvar_get(" + q(lit("var")) + ")";
    if (t == "OnNetRpc")      return "value";
    if (t == "OnNetworkEvent") return "data";
    if (t == "NetEventSender") return "sender";
    if (t == "NetPlayerCount") return "net.player_count()";
    if (t == "NetPlayerIdAt") return "net.players()[" + VsInput(g, nodeId, 0, "0") + "]";
    if (t == "NetPlayerName") return "net.player_name(" + VsInput(g, nodeId, 0, "0") + ")";
    if (t == "NetLastError")  return "net.error()";
    if (t == "NetHostOnline") return "net.room_code()";
    if (t == "NetTime")       return "net.time()";
    if (t == "NetPing")       return "net.ping()";
    if (t == "NetIsMine")     return "net.is_mine()";
    if (t == "NetIsPredicted") return "net.is_predicted()";
    if (t == "NetOwner")      return "net.owner()";
    if (t == "NetMigrating")  return "net.migrating()";
    if (t == "NetMatchStarted") return "net_match_started()";
    if (t == "NetAllReady")   return "net_all_ready()";
    if (t == "SteamName")     return "steam_name()";
    if (t == "SteamIsUnlocked") return "steam_is_unlocked(" + q(lit("")) + ")";
    if (t == "SteamGetStat")  return "steam_get_stat(" + q(lit("stat")) + ")";
    // ---- Fourth wave ----
    auto In = [&](int p, const char* d) { return VsInput(g, nodeId, p, d); };
    if (t == "OnEvent" || t == "EventValue") return "value";
    if (t == "EventName")      return "event_name";
    if (t == "InBuildMode")    return "build_active()";
    if (t == "SaveCreation" || t == "LoadCreation") return "build_exists(" + q(lit("creation")) + ")";
    if (t == "HasItem")        return "inv_has(" + q(lit("item")) + ", " + In(0, "1") + ")";
    if (t == "ItemCount")      return "inv_count(" + q(lit("item")) + ")";
    if (t == "GetObjectVar")   return "get_object_var(" + In(0, "\"self\"") + ", " + q(lit("hp")) + ")";
    if (t == "LoadValue")      return "load(" + q(lit("score")) + ", " + In(0, "0") + ")";
    if (t == "Format") {       // "Score: {0} / {1}" -> "Score: " + str(a) + " / " + str(b)
        std::string tmpl = lit("{0}"), expr, chunk;
        auto flush = [&] { if (!chunk.empty()) { expr += (expr.empty() ? "" : " + ") + q(chunk); chunk.clear(); } };
        for (std::size_t i = 0; i < tmpl.size(); ++i) {
            if (tmpl[i] == '{' && i + 2 < tmpl.size() && tmpl[i + 2] == '}' && tmpl[i + 1] >= '0' && tmpl[i + 1] <= '2') {
                flush();
                expr += (expr.empty() ? "" : " + ") + std::string("str(") + In(tmpl[i + 1] - '0', "\"\"") + ")";
                i += 2;
            } else if (tmpl[i] == '"' || tmpl[i] == '\\') { chunk += '\\'; chunk += tmpl[i]; }
            else chunk += tmpl[i];
        }
        flush();
        return expr.empty() ? std::string("\"\"") : "(" + expr + ")";
    }
    if (t == "Localize") {     // loc("key", {0}, {1}, {2}) up to the last input in use
        int last = -1;
        for (int i = 0; i < 3; ++i) if (VsInputLinkedOrSet(g, nodeId, i)) last = i;
        std::string e = "loc(" + q(lit(""));
        for (int i = 0; i <= last; ++i) e += ", " + In(i, "\"\"");
        return e + ")";
    }
    if (t == "GetLanguage")    return "get_language()";
    if (t == "Substring")  return "str(" + In(0, "\"\"") + ").Substring(" + In(1, "0") + (VsInputLinkedOrSet(g, nodeId, 2) ? ", " + In(2, "1") : std::string()) + ")";
    if (t == "Replace")    return "str(" + In(0, "\"\"") + ").Replace(" + In(1, "\"\"") + ", " + In(2, "\"\"") + ")";
    if (t == "ToNumber")   return "float.Parse(str(" + In(0, "\"0\"") + "))";
    if (t == "StartsWith") return "str(" + In(0, "\"\"") + ").StartsWith(" + In(1, "\"\"") + ")";
    if (t == "EndsWith")   return "str(" + In(0, "\"\"") + ").EndsWith(" + In(1, "\"\"") + ")";
    if (t == "Trim")       return "str(" + In(0, "\"\"") + ").Trim()";
    if (t == "IndexOf")    return "str(" + In(0, "\"\"") + ").IndexOf(" + In(1, "\"\"") + ")";
    if (t == "Noise")      return "(noise(" + A() + ", " + B() + ") * 2 - 1)";
    if (t == "Snap")       return "(round(" + A() + " / " + In(1, "1") + ") * " + In(1, "1") + ")";
    if (t == "Wrap") {
        std::string lo = In(1, "0"), hi = In(2, "1");
        return "(" + lo + " + (((" + A() + " - " + lo + ") % (" + hi + " - " + lo + ")) + (" + hi + " - " + lo + ")) % (" + hi + " - " + lo + "))";
    }
    if (t == "Average")    return "((" + A() + " + " + B() + ") / 2)";
    if (t == "Distance2D") return "sqrt((" + C() + " - " + A() + ") * (" + C() + " - " + A() + ") + (" + In(3, "0") + " - " + B() + ") * (" + In(3, "0") + " - " + B() + "))";
    if (t == "FindWithTag")    return "GameObject.FindWithTag(" + q(lit("Player")) + ")";
    if (t == "CountWithTag")   return "count_with_tag(" + q(lit("Enemy")) + ")";
    if (t == "NearestWithTag") return "nearest_with_tag(" + q(lit("Enemy")) + ")";
    if (t == "Counter")        return "_cnt_" + std::to_string(nodeId);
    // Tweens and eases. Ease With Curve is a script call; the tag nodes keep their count in a field; Spring Step has no
    // script equivalent (it keeps a velocity between frames), so it is reported as not translated and reads 0 here.
    if (t == "EaseCurve")      return "ease_curve(" + VsInput(g, nodeId, 0, "0") + ", " + q(lit("")) + ")";
    if (t == "TweenKillTag" || t == "TweenPauseTag" || t == "TweenPlayTag") return "_tw_" + std::to_string(nodeId);
    return "0";
}

inline void VsEmitChain(const VsEditGraph& g, int nodeId, std::string& out,
                        const std::string& ind, std::set<int>& visited);
// Nodes this export could not turn into script. The editor shows each one in
// Problems and on the node; the preview does not offer a script that skips them.
struct VsExportReport { std::vector<okay::vs::VsIssue> issues; };
inline VsExportReport*& VsExportReportSink() { static VsExportReport* p = nullptr; return p; }
inline void VsExportFail(int nodeId, const std::string& type) {
    VsExportReport* r = VsExportReportSink();
    if (!r) return;
    for (const auto& e : r->issues) if (e.node == nodeId) return;
    okay::vs::VsIssue is;
    is.severity = 0;
    is.node = nodeId;
    is.message = "Cannot export node " + type + " (#" + std::to_string(nodeId) + "): this node has no script.";
    r->issues.push_back(std::move(is));
}
// Functions generated for Wait / Delay during one CompileToOkayScript call.
// The pointer is set for that call and restored when it returns.
struct VsExportFns { std::vector<std::string> bodies; };
inline VsExportFns*& VsExportFnSink() { static VsExportFns* p = nullptr; return p; }
struct VsExportFnScope {
    VsExportFns fns;
    VsExportFns* prev;
    VsExportFnScope() : prev(VsExportFnSink()) { VsExportFnSink() = &fns; }
    ~VsExportFnScope() { VsExportFnSink() = prev; }
    VsExportFnScope(const VsExportFnScope&) = delete;
    VsExportFnScope& operator=(const VsExportFnScope&) = delete;
};
// Wait (once) and Delay (every time) park the chain. The nodes that follow run
// in `fn` after the seconds, via after(seconds, "fn"). Zero, negative, and NaN
// do not park: `fn` runs at once, the same as the nodes. A later reach of Wait
// runs at once; a reach of Delay while one is still counting does nothing.
inline void VsEmitLatentDelay(const VsEditGraph& g, int nodeId, bool once,
                              const std::string& seconds, std::string& out,
                              const std::string& ind, std::set<int>& visited) {
    const std::string id = std::to_string(nodeId);
    const std::string fn = "_vs_after_" + id;
    const double sec = std::strtod(seconds.c_str(), nullptr);
    const bool now = !(sec > 0.0);
    if (once) {
        const std::string done = "_vs_wait_done_" + id, busy = "_vs_wait_busy_" + id;
        if (now) out += ind + fn + "()\n";
        else {
            out += ind + "if (" + done + ") {\n";
            out += ind + "    " + fn + "()\n";
            out += ind + "} else if (!" + busy + ") {\n";
            out += ind + "    " + busy + " = true\n";
            out += ind + "    after(" + seconds + ", " + VsStrLit(fn) + ")\n";
            out += ind + "}\n";
        }
    } else if (now) {
        out += ind + fn + "()\n";
    } else {
        const std::string busy = "_vs_delay_busy_" + id;
        out += ind + "if (!" + busy + ") {\n";
        out += ind + "    " + busy + " = true\n";
        out += ind + "    after(" + seconds + ", " + VsStrLit(fn) + ")\n";
        out += ind + "}\n";
    }
    std::string body;
    if (once) {
        body += "        _vs_wait_busy_" + id + " = false\n";
        body += "        _vs_wait_done_" + id + " = true\n";
    } else {
        body += "        _vs_delay_busy_" + id + " = false\n";
    }
    std::set<int> vis = visited;
    for (int nxt : VsExecTargets(g, nodeId, 0)) VsEmitChain(g, nxt, body, "        ", vis);
    if (VsExportFns* sink = VsExportFnSink())
        sink->bodies.push_back("\n    void " + fn + "()\n    {\n" + body + "    }\n");
}
inline void VsEmitNode(const VsEditGraph& g, int nodeId, std::string& out,
                       const std::string& ind, std::set<int>& visited) {
    const VsEditNode* n = VsFind(g, nodeId); if (!n) return;
    const std::string& t = n->type;
    auto in  = [&](int p, const char* d = "0"){ return VsInput(g, nodeId, p, d); };
    auto arg = [&](const char* d){ return n->arg.empty() ? std::string(d) : n->arg; };
    auto q   = [&](const std::string& s){ return VsStrLit(s); };

    if      (t=="SetVar")      out += ind + VsIdent(arg("x")) + " = " + in(0) + "\n";
    else if (t=="AddVar")      out += ind + VsIdent(arg("x")) + " += " + in(0) + "\n";
    else if (t=="Print")       out += ind + "Debug.Log(" + (VsInputLinkedOrSet(g,nodeId,0) && VsInputLinkedOrSet(g,nodeId,1)
                       ? "(" + in(0,"\"\"") + " + \": \" + " + in(1,"\"\"") + ")"
                       : VsInputLinkedOrSet(g,nodeId,1) ? in(1,"\"\"") : in(0,"\"\"")) + ")\n";
    else if (t=="LogWarn")     out += ind + "Debug.LogWarning(" + in(0,"\"\"") + ")\n";
    else if (t=="LogError")    out += ind + "Debug.LogError(" + in(0,"\"\"") + ")\n";
    else if (t=="Translate")   out += ind + "move3(" + in(0)+", "+in(1)+", "+in(2)+")\n";
    else if (t=="SetPosition") out += ind + "set_pos3(" + in(0)+", "+in(1)+", "+in(2, "transform.localPosition.z")+")\n";
    else if (t=="Rotate")      out += ind + "rotate(" + in(0) + ")\n";
    else if (t=="SetRotation") out += ind + "transform.eulerAngles.z = " + in(0) + "\n";
    else if (t=="SetScale")    out += ind + "set_scale3(" + in(0,"1")+", "+in(1,"1")+", "+in(2,"1")+")\n";
    else if (t=="PlaySound")   out += ind + "play_sound(" + q(arg("")) + ")\n";
    else if (t=="Spawn")       out += ind + "spawn3(" + q(arg("")) + ", " + in(0, "transform.position.x") + ", " + in(1, "transform.position.y") + ", " + in(2, "transform.position.z") + ")\n";
    else if (t=="Destroy")     out += ind + "destroy()\n";
    else if (t=="AddForce")    out += ind + "add_force(" + in(0)+", "+in(1)+", "+in(2)+")\n";
    else if (t=="AddExplosionForce")
        out += ind + "add_explosion_force(vec_x(" + in(0, "vec3(0, 0, 0)") + "), vec_y(" + in(0, "vec3(0, 0, 0)") +
               "), " + in(1, "10") + ", " + in(2, "5") + ", " + in(3, "0") + ")\n";
    else if (t=="SetVelocity") out += ind + "set_velocity(" + in(0)+", "+in(1)+", "+in(2)+")\n";
    else if (t=="AddImpulse")  out += ind + "add_impulse(" + in(0)+", "+in(1)+", "+in(2)+")\n";
    else if (t=="Branch") {
        out += ind + "if (" + in(0) + ") {\n";
        int tn = VsExecTarget(g, nodeId, 0); if (tn>=0) VsEmitChain(g, tn, out, ind+"    ", visited);
        int fn = VsExecTarget(g, nodeId, 1);
        if (fn>=0) { out += ind + "} else {\n"; VsEmitChain(g, fn, out, ind+"    ", visited); }
        out += ind + "}\n";
    }
    else if (t=="Sequence") {
        for (int p=0;p<2;++p){ int s=VsExecTarget(g,nodeId,p); if(s>=0) VsEmitChain(g,s,out,ind,visited); }
    }
    else if (t=="Once") {
        out += ind + "if (!_once_"+std::to_string(nodeId)+") {\n";
        out += ind + "    _once_"+std::to_string(nodeId)+" = true\n";
        int s=VsExecTarget(g,nodeId,0); if(s>=0) VsEmitChain(g,s,out,ind+"    ",visited);
        out += ind + "}\n";
    }
    else if (t=="LookAt")      out += ind + "face(" + q(arg("")) + ")\n";
    else if (t=="MoveTowards") out += ind + "move_toward_object(" + q(arg("")) + ", " + in(0, "2") + ")\n";
    else if (t=="SetActive")   out += ind + "set_active_of(" + q(arg("self")) + ", " + in(0, "true") + ")\n";
    else if (t=="SetText")     out += ind + "set_text_of(" + q(arg("self")) + ", " + in(0, "\"\"") + ")\n";
    else if (t=="LoadScene")   out += ind + "load_scene_name(" + q(arg("")) + ")\n";
    else if (t=="SetLanguage") out += ind + "set_language(" + (VsInputLinkedOrSet(g, nodeId, 0) ? in(0) : q(arg("fr"))) + ")\n";
    else if (t=="PlaySequence") out += ind + "play_sequence(" + q(arg("")) + ")\n";
    else if (t=="StopSequence") out += ind + "stop_sequence()\n";
    else if (t=="AnimTrigger") out += ind + "anim_trigger(" + q(arg("")) + ")\n";
    else if (t=="AnimSetFloat")out += ind + "anim_set_float(" + q(arg("")) + ", " + in(0) + ")\n";
    else if (t=="AnimSetBool") out += ind + "anim_set_bool(" + q(arg("")) + ", " + in(0) + ")\n";
    else if (t=="StartDialogue")     out += ind + "start_dialogue(" + q(arg("")) + ")\n";
    else if (t=="SetStoryVar")       out += ind + "set_story(" + q(arg("")) + ", " + in(0, "0") + ")\n";
    else if (t=="StartQuest")        out += ind + "start_quest(" + q(arg("")) + ")\n";
    else if (t=="CompleteObjective") out += ind + "complete_objective(" + q(arg("")) + ", " + in(0, "0") + ")\n";
    else if (t=="IkLook")      out += ind + "ik_look(vec_x(" + in(0) + "), vec_y(" + in(0) + "), vec_z(" + in(0) + "), " + in(1, "1") + ")\n";
    else if (t=="IkHand")      out += ind + "ik_hand(" + q(arg("right")) + ", vec_x(" + in(0) + "), vec_y(" + in(0) + "), vec_z(" + in(0) + "), " + in(1, "1") + ")\n";
    else if (t=="SetWeather")  out += ind + "weather_set(" + q(arg("rain")) + ", " + in(0, "1") + ")\n";
    else if (t=="SetFog")      out += ind + "fog_set(" + in(0, "true") + ", " + in(1, "20") + ", " + in(2, "90") + ")\n";
    else if (t=="SetPostEffect") out += ind + "post_set(" + q(arg("bloom")) + ", " + in(0, "1") + ")\n";
    else if (t=="NavMoveTo")   out += ind + "nav_move_to(vec_x(" + in(0) + "), vec_y(" + in(0) + "), vec_z(" + in(0) + "))\n";
    else if (t=="NavStop")     out += ind + "nav_stop()\n";
    else if (t=="GridMoveTo")  out += ind + "grid_move_to(" + in(0, "0") + ", " + in(1, "0") + ")\n";
    else if (t=="GridStop")    out += ind + "grid_stop()\n";
    else if (t=="BtSet")       out += ind + "bt_set(" + q(arg("x")) + ", " + in(0) + ")\n";
    else if (t=="BtRun")       out += ind + "bt_run(" + q(arg("")) + ")\n";
    else if (t=="SetLinePoints") out += ind + "line_set_points(" + in(0, "[]") + ")\n";
    else if (t=="ClearTrail")  out += ind + "trail_clear()\n";
    else if (t=="MusicPlay") {
        const bool num = !n->arg.empty() && std::all_of(n->arg.begin(), n->arg.end(), [](unsigned char c) { return std::isdigit(c) != 0; });
        out += ind + "music_play(" + (n->arg.empty() ? std::string() : num ? n->arg : q(n->arg)) + ")\n";
    }
    else if (t=="MusicStop")   out += ind + "music_stop(" + in(0, "1") + ")\n";
    else if (t=="MusicPause")  out += ind + "music_pause()\n";
    else if (t=="MusicResume") out += ind + "music_resume()\n";
    else if (t=="MusicNext")   out += ind + "music_next()\n";
    else if (t=="CameraShake") out += ind + "camera_shake(" + in(0, "0.5") + ")\n";
    else if (t=="SetFov")      out += ind + "camera_fov(" + in(0, "60") + ")\n";
    else if (t=="CameraFollow") out += ind + "camera_follow(" + in(0, "\"\"") + ", " + in(1, "5") + ")\n";
    else if (t=="ParticlePlay") out += ind + "particles_on(true)\n";
    else if (t=="ParticleStop") out += ind + "particles_on(false)\n";
    else if (t=="ParticleBurst") out += ind + "emit(" + in(0, "20") + ")\n";
    else if (t=="TweenMove" || t=="TweenMoveSpeed") {
        // tween_move3(x, y, z, seconds[, ease]) goes to the local position; an axis nobody feeds stays where it is.
        auto ax = [&](int p, const char* cur) { return VsInputLinkedOrSet(g, nodeId, p) ? in(p) : std::string(cur); };
        std::string call = "tween_move3(" + ax(0, "transform.localPosition.x") + ", " + ax(1, "transform.localPosition.y") + ", " +
                           ax(2, "transform.localPosition.z") + ", " + in(3, t=="TweenMoveSpeed" ? "5" : "1");
        if (VsInputLinkedOrSet(g, nodeId, 4)) call += ", " + in(4, "\"\"");
        call += ")";
        if (t=="TweenMoveSpeed") call = "tween_speed_based(" + call + ")";
        if (!n->arg.empty()) call = "tween_tag(" + call + ", " + q(n->arg) + ")";
        out += ind + call + "\n";
    }
    else if (t=="TweenKillTag")  out += ind + "_tw_" + std::to_string(nodeId) + " = tween_kill_tag(" + q(arg("")) + ", " + in(0, "false") + ")\n";
    else if (t=="TweenPauseTag") out += ind + "_tw_" + std::to_string(nodeId) + " = tween_pause_tag(" + q(arg("")) + ")\n";
    else if (t=="TweenPlayTag")  out += ind + "_tw_" + std::to_string(nodeId) + " = tween_play_tag(" + q(arg("")) + ")\n";
    else if (t=="Cooldown")    out += ind + "// Cooldown (" + VsCommentText(arg("1")) + "s): gate the actions after this with a timer\n";
    else if (t=="ForLoop") {
        const std::string v = "_i" + std::to_string(nodeId);
        out += ind + "for (var " + v + " = " + in(0, "0") + "; " + v + " <= " + in(1, "9") + "; " + v + " += 1) {\n";
        int b = VsExecTarget(g, nodeId, 0); if (b >= 0) { std::set<int> vb = visited; VsEmitChain(g, b, out, ind + "    ", vb); }
        out += ind + "}\n";
        int d = VsExecTarget(g, nodeId, 1); if (d >= 0) VsEmitChain(g, d, out, ind, visited);
    }
    else if (t=="Repeat") {
        const std::string v = "_r" + std::to_string(nodeId);
        out += ind + "for (var " + v + " = 0; " + v + " < " + VsNum(arg("1"), "1") + "; " + v + " += 1) {\n";
        int b = VsExecTarget(g, nodeId, 0); if (b >= 0) VsEmitChain(g, b, out, ind + "    ", visited);
        out += ind + "}\n";
    }
    else if (t=="While") {
        out += ind + "while (" + in(0, "false") + ") {\n";
        int b = VsExecTarget(g, nodeId, 0); if (b >= 0) VsEmitChain(g, b, out, ind + "    ", visited);
        out += ind + "}\n";
        int d = VsExecTarget(g, nodeId, 1); if (d >= 0) VsEmitChain(g, d, out, ind, visited);
    }
    else if (t=="Break")    out += ind + "break\n";
    else if (t=="Continue") out += ind + "continue\n";
    else if (t=="FlipFlop") {
        const std::string v = "_ff_" + std::to_string(nodeId);
        out += ind + v + " = !" + v + "\n";
        out += ind + "if (" + v + ") {\n";
        int a = VsExecTarget(g, nodeId, 0); if (a >= 0) VsEmitChain(g, a, out, ind + "    ", visited);
        int b = VsExecTarget(g, nodeId, 1);
        if (b >= 0) { out += ind + "} else {\n"; VsEmitChain(g, b, out, ind + "    ", visited); }
        out += ind + "}\n";
    }
    else if (t=="Gate") {
        out += ind + "if (" + in(0, "true") + ") {\n";
        int a = VsExecTarget(g, nodeId, 0); if (a >= 0) VsEmitChain(g, a, out, ind + "    ", visited);
        out += ind + "}\n";
    }
    else if (t=="DoN") {
        const std::string v = "_don_" + std::to_string(nodeId);
        out += ind + "if (" + v + " < " + VsNum(arg("1"), "1") + ") {\n" + ind + "    " + v + " += 1\n";
        int a = VsExecTarget(g, nodeId, 0); if (a >= 0) VsEmitChain(g, a, out, ind + "    ", visited);
        out += ind + "}\n";
    }
    else if (t=="SwitchInt") {
        out += ind + "switch (round(" + in(0) + ")) {\n";
        static const char* kCase[4] = {"case 0:", "case 1:", "case 2:", "default:"};
        for (int p = 0; p < 4; ++p) {
            int c = VsExecTarget(g, nodeId, p);
            if (c < 0) continue;
            out += ind + "    " + kCase[p] + "\n";
            VsEmitChain(g, c, out, ind + "        ", visited);
            out += ind + "        break\n";
        }
        out += ind + "}\n";
    }
    else if (t=="Toggle")        out += ind + VsIdent(arg("x")) + " = !" + VsIdent(arg("x")) + "\n";
    else if (t=="SetPositionVec"){ std::string p = in(0, "vec3(0, 0, 0)"); out += ind + "transform.localPosition = " + p + "\n"; }
    else if (t=="MoveBy")        out += ind + "transform.localPosition = transform.localPosition + " + in(0, "vec3(0, 0, 0)") + "\n";
    else if (t=="Rotate3")       out += ind + "rotate3(" + in(0)+", "+in(1)+", "+in(2)+")\n";
    else if (t=="SetRotation3")  out += ind + "set_rot3(" + in(0)+", "+in(1)+", "+in(2)+")\n";
    else if (t=="SetColor")      out += ind + "set_color(" + in(0,"1")+", "+in(1,"1")+", "+in(2,"1")+", "+in(3,"1")+")\n";
    else if (t=="PlayClip")      out += ind + "play_clip(" + q(arg("")) + ")\n";
    else if (t=="PlayAction")    out += ind + "play_action(" + q(arg("Death")) + ", " + in(0, "0.2") + ")\n";
    else if (t=="Die")           out += ind + "die(" + q(arg("forward")) + ")\n";
    else if (t=="Revive")        out += ind + "revive()\n";
    else if (t=="Dive")          out += ind + "dive()\n";
    else if (t=="Sit") {
        if (n->arg.empty()) out += ind + "sit()\n";
        else out += ind + "sit(" + q(n->arg) + ")\n";
    }
    else if (t=="StandUp")     out += ind + "stand_up()\n";
    else if (t=="Interact")    out += ind + "interact()\n";
    else if (t=="HitReact")      out += ind + "hit_react(" + q(arg("forward")) + ", " + in(0, "0") + ")\n";
    else if (t=="Roll")          out += ind + "roll(" + q(arg("forward")) + ")\n";
    else if (t=="Dodge")         out += ind + "dodge(" + q(arg("forward")) + ")\n";
    else if (t=="Vault")         out += ind + "vault()\n";
    else if (t=="Mantle")        out += ind + "mantle()\n";
    else if (t=="LedgeGrab")     out += ind + "ledge_grab()\n";
    else if (t=="ClimbUp")       out += ind + "climb_up()\n";
    else if (t=="LetGo")         out += ind + "let_go()\n";
    else if (t=="Shimmy")        out += ind + "shimmy(" + in(0, "0") + ")\n";
    else if (t=="SetTimeScale")  out += ind + "set_timescale(" + in(0, "1") + ")\n";
    else if (t=="SetBlendShape") out += ind + "blend_shape(" + q(arg("")) + ", " + in(0, "1") + ")\n";
    else if (t=="NetHost")       out += ind + "net_host(" + VsNum(arg("45000"), "45000") + ")\n";
    else if (t=="NetJoin") {
        std::string h = arg("127.0.0.1"), port = "45000";
        size_t cut = h.find_first_of(" :");
        if (cut != std::string::npos) { port = h.substr(cut + 1); h = h.substr(0, cut); }
        out += ind + "net_join(" + q(h) + ", " + VsNum(port, "45000") + ")\n";
    }
    else if (t=="NetDisconnect") out += ind + "net_disconnect()\n";
    else if (t=="NetHostOrJoin") out += ind + "net.host_or_join(" + VsNum(arg("45000"), "45000") + ")\n";
    else if (t=="NetHostOnline") out += ind + "net.host_online(" + in(0, "\"\"") + ")\n";
    else if (t=="NetJoinCode")   out += ind + "net.join_code(" + in(0, "\"\"") + ")\n";
    else if (t=="NetGiveOwnership") out += ind + "net.give(\"\", " + in(0, "0") + ")\n";
    else if (t=="NetRequestOwnership") out += ind + "net.request()\n";
    else if (t=="NetSimulate")   out += ind + "net.simulate(" + in(0, "0") + ", " + in(1, "0") + ", " + in(2, "0") + ")\n";
    else if (t=="NetShowStats")  out += ind + "net.show_stats(" + in(0, "true") + ")\n";
    else if (t=="NetSendEvent")  out += ind + "net.send(" + q(arg("event")) + ", " + in(0, "\"\"") + ")\n";
    else if (t=="NetSend")       out += ind + "net_send(" + q(arg("")) + ", str(" + in(0, "\"\"") + "))\n";
    else if (t=="NetSetVar")     out += ind + "net_set(" + q(arg("x")) + ", str(" + in(0, "\"\"") + "))\n";
    else if (t=="NetChat")       out += ind + "net_chat(" + in(0, "\"\"") + ")\n";
    else if (t=="NetRpc")        out += ind + "net_rpc(" + q(arg("")) + ", str(" + in(0, "\"\"") + "))\n";
    else if (t=="NetObjRpc")     out += ind + "net_obj_rpc(" + q(arg("rpc")) + ", str(" + in(0, "\"\"") + "))\n";
    else if (t=="SyncVarSet")    out += ind + "net_syncvar_set(" + q(arg("var")) + ", str(" + in(0, "\"\"") + "))\n";
    else if (t=="NetSpawnOwned") out += ind + "net_spawn_owned(" + q(arg("")) + ", transform.position.x, transform.position.y, transform.position.z)\n";
    else if (t=="NetDespawn")    out += ind + "net_despawn(" + in(0, "\"\"") + ")\n";
    else if (t=="NetReady")      out += ind + "net_ready(" + in(0, "true") + ")\n";
    else if (t=="NetStartMatch") out += ind + "net_start_match()\n";
    else if (t=="SteamUnlock")   out += ind + "steam_unlock(" + q(arg("")) + ")\n";
    else if (t=="SteamSetStat")  out += ind + "steam_set_stat(" + q(arg("stat")) + ", " + in(0) + ")\n";
    else if (t=="SteamStore")    out += ind + "steam_store()\n";
    else if (t=="SteamLeaderboard") out += ind + "steam_leaderboard(" + q(arg("")) + ", " + in(0) + ")\n";
    else if (t=="SendEvent")     out += ind + "send_event(" + in(0, "\"self\"") + ", " + q(arg("Event")) + ", " + in(1, "0") + ")\n";
    else if (t=="BroadcastEvent")out += ind + "broadcast_event(" + q(arg("Event")) + ", " + in(0, "0") + ")\n";
    else if (t=="SetObjectVar")  out += ind + "set_object_var(" + in(0, "\"self\"") + ", " + q(arg("hp")) + ", " + in(1, "0") + ")\n";
    else if (t=="SaveValue")     out += ind + "save(" + q(arg("score")) + ", " + in(0, "0") + ")\n";
    else if (t=="EnterBuildMode") out += ind + "build_enter()\n";
    else if (t=="ExitBuildMode")  out += ind + "build_exit()\n";
    else if (t=="SaveCreation")   out += ind + "build_save(" + q(arg("creation")) + ")\n";
    else if (t=="LoadCreation")   out += ind + "build_load(" + q(arg("creation")) + ")\n";
    else if (t=="AddItem")        out += ind + "inv_add(" + q(arg("item")) + ", " + in(0, "1") + ")\n";
    else if (t=="RemoveItem")     out += ind + "inv_remove(" + q(arg("item")) + ", " + in(0, "1") + ")\n";
    else if (t=="EquipItem")      out += ind + "equip(" + in(0, "\"\"") + ", " + q(arg("item")) + ")\n";
    else if (t=="CraftItem")      out += ind + "craft(" + q(arg("item")) + ", " + in(0, "1") + ")\n";
    else if (t=="LootRoll")       out += ind + "loot_roll(" + q(arg("loot")) + ")\n";
    else if (t=="SwingWeapon")    out += ind + "weapon_swing()\n";
    else if (t=="FireWeapon")     out += ind + "weapon_fire()\n";
    else if (t=="ReloadWeapon")   out += ind + "weapon_reload()\n";
    else if (t=="SetWeapon")      out += ind + "weapon_set(" + q(arg("sword")) + ")\n";
    else if (t=="SpawnPickup")    out += ind + "spawn_pickup(" + q(arg("item")) + ", " + in(0, "1") + ", " + in(1, "0") + ", " + in(2, "0") + ", " + in(3, "0") + ")\n";
    else if (t=="SetBuildPalette") {   // "Name=prefab|icon, ..." -> one build_palette_add per item
        out += ind + "build_palette_clear()\n";
        auto trim = [](const std::string& v) { size_t a = v.find_first_not_of(" \t"), b = v.find_last_not_of(" \t");
                                               return a == std::string::npos ? std::string() : v.substr(a, b - a + 1); };
        std::stringstream ss(arg(""));
        for (std::string item; std::getline(ss, item, ',');) {
            item = trim(item);
            if (item.empty()) continue;
            std::string name = item, prefab = item, icon;
            size_t eq = item.find('=');
            if (eq != std::string::npos) { name = trim(item.substr(0, eq)); prefab = trim(item.substr(eq + 1)); }
            size_t bar = prefab.find('|');
            if (bar != std::string::npos) { icon = trim(prefab.substr(bar + 1)); prefab = trim(prefab.substr(0, bar)); }
            out += ind + "build_palette_add(" + q(name) + ", " + q(prefab) + ", " + q(icon) + ")\n";
        }
    }
    else if (t=="Teleport") {
        auto ax = [&](int p) { return VsInputLinkedOrSet(g, nodeId, p) ? in(p) : std::string("\"keep\""); };
        out += ind + "set_object_position(" + q(arg("self")) + ", " + ax(0) + ", " + ax(1) + ", " + ax(2) + ")\n";
    }
    else if (t=="SetVisible")    out += ind + "set_visible(" + in(0, "true") + ")\n";
    else if (t=="PauseGame")     out += ind + "pause_game(" + in(0, "true") + ")\n";
    else if (t=="QuitGame")      out += ind + "quit()\n";
    else if (t=="ShowUI")        out += ind + "ui_show(" + q(arg("self")) + ")\n";
    else if (t=="HideUI")        out += ind + "ui_hide(" + q(arg("self")) + ")\n";
    else if (t=="ShowCursor")    out += ind + "show_cursor(" + in(0, "true") + ")\n";
    else if (t=="ChanceBranch") {
        out += ind + "if (random() < " + in(0, "0.5") + ") {\n";
        int a = VsExecTarget(g, nodeId, 0); if (a >= 0) VsEmitChain(g, a, out, ind + "    ", visited);
        int b = VsExecTarget(g, nodeId, 1);
        if (b >= 0) { out += ind + "} else {\n"; VsEmitChain(g, b, out, ind + "    ", visited); }
        out += ind + "}\n";
    }
    else if (t=="Counter") {
        const std::string v = "_cnt_" + std::to_string(nodeId);
        if (VsInputLinkedOrSet(g, nodeId, 0)) out += ind + "if (" + in(0, "false") + ") {\n" + ind + "    " + v + " = 0\n" + ind + "}\n";
        out += ind + v + " += 1\n";
    }
    else if (t=="EveryN") {
        const std::string v = "_evn_" + std::to_string(nodeId);
        out += ind + v + " += 1\n";
        out += ind + "if (" + v + " % round(" + in(0, "2") + ") == 0) {\n";
        int a = VsExecTarget(g, nodeId, 0); if (a >= 0) VsEmitChain(g, a, out, ind + "    ", visited);
        out += ind + "}\n";
    }
    else if (t=="Timer") {
        // Same rule as TimerNode: pass at most once every `seconds`, keeping the
        // leftover (clamped to four intervals). Zero, negative, and NaN pass every time.
        const std::string seconds = VsNum(arg("1"), "1");
        const double sec = std::strtod(seconds.c_str(), nullptr);
        auto follow = [&](const std::string& bodyInd) {
            for (int s : VsExecTargets(g, nodeId, 0)) VsEmitChain(g, s, out, bodyInd, visited);
        };
        if (!(sec > 0.0)) {
            follow(ind);
        } else {
            const std::string acc = "_vs_timer_" + std::to_string(nodeId);
            out += ind + acc + " += Time.deltaTime\n";
            out += ind + "if (" + acc + " >= " + seconds + ") {\n";
            out += ind + "    " + acc + " -= " + seconds + "\n";
            out += ind + "    if (" + acc + " > (" + seconds + " * 4)) " + acc + " = (" + seconds + " * 4)\n";
            follow(ind + "    ");
            out += ind + "}\n";
        }
    }
    else if (t=="Wait" || t=="Delay") VsEmitLatentDelay(g, nodeId, t=="Wait", VsNum(arg("1"), "1"), out, ind, visited);
    else if (t=="Action" || t=="Actions") out += ind + "run_action(" + q(n->arg) + ")\n";
    else if (t=="Condition") {
        out += ind + "if (check_action(" + q(n->arg) + ")) {\n";
        int tn = VsExecTarget(g, nodeId, 0); if (tn>=0) VsEmitChain(g, tn, out, ind+"    ", visited);
        int fn = VsExecTarget(g, nodeId, 1);
        if (fn>=0) { out += ind + "} else {\n"; VsEmitChain(g, fn, out, ind+"    ", visited); }
        out += ind + "}\n";
    }
    else                 VsExportFail(nodeId, t);
}
inline void VsEmitChain(const VsEditGraph& g, int nodeId, std::string& out,
                        const std::string& ind, std::set<int>& visited) {
    int cur = nodeId;
    while (cur >= 0) {
        if (visited.count(cur)) break;
        visited.insert(cur);
        const VsEditNode* n = VsFind(g, cur); if (!n) break;
        VsEmitNode(g, cur, out, ind, visited);
        static const std::set<std::string> kSelfContinued = {
            "Branch", "Sequence", "Once", "ForLoop", "Repeat", "FlipFlop", "Gate", "DoN", "SwitchInt",
            "ChanceBranch", "EveryN", "Condition", "While", "Wait", "Delay", "Timer"};
        if (kSelfContinued.count(n->type)) break;
        const std::vector<int> next = VsExecTargets(g, cur, 0);
        if (next.size() > 1) { for (int t : next) VsEmitChain(g, t, out, ind, visited); break; }
        cur = next.empty() ? -1 : next[0];
    }
}
inline std::string CompileToOkayScript(const VsEditGraph& g, std::vector<okay::vs::VsIssue>* exportIssues = nullptr) {
    VsExportReport report;
    struct ReportScope {
        VsExportReport* prev;
        explicit ReportScope(VsExportReport* r) : prev(VsExportReportSink()) { VsExportReportSink() = r; }
        ~ReportScope() { VsExportReportSink() = prev; }
    } reportScope(&report);
    VsExportFnScope latent;   // Wait / Delay callbacks collected while the chains are emitted
    // The graph's events become Start / Update / OnCollisionEnter / OnTriggerEnter /
    // OnMouseDown functions; its state becomes fields. A script's fields and
    // functions are the component, so there's no class wrapper. (Built indented
    // one level, then dedented at the end.)
    std::string out = "    // Generated from the visual graph: edit it freely, or keep wiring nodes;\n"
                      "    // both drive the same runtime.\n\n";
    // Object references (Target inputs, Self / Other / Find Object, Raycast 3D,
    // Spawn's object) have no text-script equivalent yet: say exactly which nodes
    // this code can't reproduce instead of quietly generating something else.
    {
        std::vector<std::string> untranslated;
        auto pinUsed = [&](int nodeId, int pin) {
            for (const auto& l : g.dataLinks) if (l.consumer == nodeId && l.inPin == pin) return true;
            for (const auto& n : g.nodes)
                if (n.id == nodeId) { auto it = n.pinDefault.find(pin); return it != n.pinDefault.end() && !it->second.empty(); }
            return false;
        };
        for (const auto& n : g.nodes) {
            const int tp = okay::vs::TargetPinFor(n.type);
            bool objectUse = n.type.rfind("Qb", 0) == 0 || n.type == "Self" || n.type == "Other" || n.type == "FindObject" || n.type == "Raycast3D" || n.type == "SpringStep" ||
                             IsFunctionNode(n.type) || n.type == "SetLocal" || n.type == "GetLocal" || IsApiNode(n.type) ||
                             n.type == "ForEach" || n.type == "ForEachMap" || n.type.rfind("List", 0) == 0 || n.type == "NewList" ||
                             n.type == "NewMap" || n.type.rfind("Map", 0) == 0 || n.type == "RunGraph" || n.type == "CallScript" ||
                             n.type == "AllWithTag" ||
                             n.type.find("SceneVar") != std::string::npos || n.type.find("GlobalVar") != std::string::npos ||
                             n.type == "OnVarChanged" ||
                             (tp >= 0 && pinUsed(n.id, tp)) || (n.type == "Destroy" && pinUsed(n.id, 0));
            if (n.type == "Spawn")
                for (const auto& l : g.dataLinks) if (l.producer == n.id) objectUse = true;
            if (objectUse || VsBadField(n)) untranslated.push_back(n.type + " #" + std::to_string(n.id));
        }
        if (!untranslated.empty()) {
            out += "    // NOT TRANSLATED - these nodes use object references, functions, engine-API nodes,\n"
                   "    // lists, scene / global variables or a spring, which this generated code doesn't support yet\n"
                   "    // (the graph itself runs them;\n"
                   "    // an engine-API node's tooltip shows the C#-style line it equals):\n    //  ";
            for (std::size_t i = 0; i < untranslated.size(); ++i) out += (i ? ", " : "") + untranslated[i];
            out += "\n\n";
        }
    }
    // Every event node of a kind contributes to the one method for that event
    // (a graph may have several On Update nodes; they all run, in order).
    auto ofType = [&](const char* type) {
        std::vector<const VsEditNode*> v;
        for (const auto& n : g.nodes) if (n.type == type) v.push_back(&n);
        return v;
    };
    auto sanitize = [](std::string v) {
        std::string id;
        for (char c : v) id += (std::isalnum((unsigned char)c) || c == '_') ? c : '_';
        return id.empty() ? std::string("Event") : id;
    };
    std::string fields;
    for (const auto& n : g.nodes) {
        if (n.type=="Once")     fields += "    bool _once_"+std::to_string(n.id)+" = false;\n";
        if (n.type=="FlipFlop") fields += "    bool _ff_"+std::to_string(n.id)+" = false;\n";
        if (n.type=="DoN")      fields += "    int _don_"+std::to_string(n.id)+" = 0;\n";
        if (n.type=="Counter")  fields += "    int _cnt_"+std::to_string(n.id)+" = 0;\n";
        if (n.type=="TweenKillTag" || n.type=="TweenPauseTag" || n.type=="TweenPlayTag") fields += "    int _tw_"+std::to_string(n.id)+" = 0;\n";
        if (n.type=="EveryN")   fields += "    int _evn_"+std::to_string(n.id)+" = 0;\n";
        if (n.type=="OnTimer")  fields += "    float _timer_"+std::to_string(n.id)+" = 0;\n";
        if (n.type=="Timer")    fields += "    float _vs_timer_"+std::to_string(n.id)+" = 0;\n";
        if (n.type=="Wait") {
            fields += "    bool _vs_wait_done_"+std::to_string(n.id)+" = false;\n";
            fields += "    bool _vs_wait_busy_"+std::to_string(n.id)+" = false;\n";
        }
        if (n.type=="Delay") fields += "    bool _vs_delay_busy_"+std::to_string(n.id)+" = false;\n";
    }
    if (!fields.empty()) out += fields + "\n";
    bool first = true, anyEvent = false;
    auto chain = [&](int entry, const std::string& ind) {
        std::set<int> v; for (int s : VsExecTargets(g, entry, 0)) VsEmitChain(g, s, out, ind, v);
    };
    auto open = [&](const std::string& sig) {
        out += std::string(first ? "" : "\n") + "    " + sig + "\n    {\n";
        first = false; anyEvent = true;
    };
    // Start: every On Start.
    if (auto v = ofType("OnStart"); !v.empty()) {
        open("void Start()");
        for (auto* n : v) chain(n->id, "        ");
        out += "    }\n";
    }
    // Update: every On Update, then the key and timer events (polled per frame).
    {
        auto upd = ofType("OnUpdate"), keys = ofType("OnKey"), keysUp = ofType("OnKeyUp"), timers = ofType("OnTimer");
        if (!upd.empty() || !keys.empty() || !keysUp.empty() || !timers.empty()) {
            open("void Update()");
            for (auto* n : upd) chain(n->id, "        ");
            for (auto* n : keys) {
                out += "        if (key_pressed(" + VsStrLit(n->arg.empty() ? std::string("space") : n->arg) + ")) {\n";
                chain(n->id, "            ");
                out += "        }\n";
            }
            for (auto* n : keysUp) {
                out += "        if (key_up(" + VsStrLit(n->arg.empty() ? std::string("space") : n->arg) + ")) {\n";
                chain(n->id, "            ");
                out += "        }\n";
            }
            for (auto* n : timers) {
                const std::string v = "_timer_" + std::to_string(n->id), per = VsNum(n->arg, "1");
                out += "        " + v + " += Time.deltaTime\n";
                out += "        if (" + v + " >= " + per + ") {\n";
                out += "            " + v + " -= " + per + "\n";
                chain(n->id, "            ");
                out += "        }\n";
            }
            out += "    }\n";
        }
    }
    // Collision / trigger / mouse events become the matching handlers.
    static const std::pair<const char*, const char*> kEvents[] = {
        {"OnCollision",     "void OnCollisionEnter(string other)"},   // other = the other object's name
        {"OnCollisionExit", "void OnCollisionExit(string other)"},
        {"OnTrigger",       "void OnTriggerEnter(string other)"},
        {"OnTriggerExit",   "void OnTriggerExit(string other)"},
        {"OnClick",         "void OnMouseDown()"},
        {"OnMouseEnter",    "void OnMouseEnter()"},
        {"OnMouseExit",     "void OnMouseExit()"},
        {"OnMouseDown",     "void OnMouseDown()"},
        {"OnMouseUp",       "void OnMouseUp()"},
        {"OnMouseOver",     "void OnMouseOver()"},
        {"OnCollisionStay", "void OnCollisionStay(string other)"},
        {"OnTriggerStay",   "void OnTriggerStay(string other)"},
        {"OnLateUpdate",    "void LateUpdate()"},
        {"OnFixedUpdate",   "void FixedUpdate()"},
        {"OnEnable",        "void OnEnable()"},
        {"OnDisable",       "void OnDisable()"},
        {"OnDestroy",       "void OnDestroy()"},
        {"OnObjectPlaced",  "void OnObjectPlaced(string name)"},    // name = the placed object's name
        {"OnObjectRemoved", "void OnObjectRemoved(string name)"},
        {"OnLogicSignal",   "void OnLogicSignal(string name)"},
        // Inventory: the Event Name / Event Value nodes read these parameters.
        {"OnItemAdded", "void OnItemAdded(string event_name, float value)"},
        {"OnItemRemoved", "void OnItemRemoved(string event_name, float value)"},
        {"OnInventoryFull", "void OnInventoryFull(string event_name, float value)"},
        {"OnItemUsed", "void OnItemUsed(string event_name)"},
        {"OnEquip", "void OnEquip(string event_name, string value)"},
        {"OnCraft", "void OnCraft(string event_name)"},
        // Dialogue and quests: Event Name is the event / quest id, Event Value the objective or count.
        {"OnDialogueEvent", "void OnDialogueEvent(string event_name)"},
        {"OnDialogueEnd", "void OnDialogueEnd(string event_name)"},
        {"OnQuestStarted", "void OnQuestStarted(string event_name)"},
        {"OnQuestCompleted", "void OnQuestCompleted(string event_name, float value)"},
        {"OnQuestFailed", "void OnQuestFailed(string event_name)"},
        {"OnObjectiveCompleted", "void OnObjectiveCompleted(string event_name, float value)"},
        {"OnQuestEvent", "void OnQuestEvent(string event_name)"}};
    // Events sharing a handler (On Click + On Mouse Down) go into one method.
    std::set<std::string> doneSig;
    for (const auto& ev : kEvents) {
        if (doneSig.count(ev.second)) continue;
        std::vector<const VsEditNode*> v;
        for (const auto& ev2 : kEvents)
            if (std::strcmp(ev2.second, ev.second) == 0) for (auto* n : ofType(ev2.first)) v.push_back(n);
        if (v.empty()) continue;
        doneSig.insert(ev.second);
        open(ev.second);
        for (auto* n : v) chain(n->id, "        ");
        out += "    }\n";
    }
    // Custom events: one OnEvent_<name>(value) per name (send_event / Send Event call it).
    {
        std::map<std::string, std::vector<const VsEditNode*>> byName;
        for (auto* n : ofType("OnEvent")) byName[sanitize(n->arg)].push_back(n);
        for (auto& kv : byName) {
            open("void OnEvent_" + kv.first + "(value)");
            for (auto* n : kv.second) chain(n->id, "        ");
            out += "    }\n";
        }
    }
    // The easy layer's events: the engine calls on_player_joined(id, name),
    // on_player_left(id) and on_network_event(name, data, from) on every script.
    if (!ofType("OnPlayerJoined").empty()) {
        open("void on_player_joined(value, event_name)");
        for (auto* n : ofType("OnPlayerJoined")) chain(n->id, "        ");
        out += "    }\n";
    }
    if (!ofType("OnPlayerLeft").empty()) {
        open("void on_player_left(value)");
        for (auto* n : ofType("OnPlayerLeft")) chain(n->id, "        ");
        out += "    }\n";
    }
    if (!ofType("OnHostMigrated").empty()) {   // on_host_migrated(previous id of the new host)
        open("void on_host_migrated(value)");
        for (auto* n : ofType("OnHostMigrated")) chain(n->id, "        ");
        out += "    }\n";
    }
    if (!ofType("OnNetworkEvent").empty()) {
        std::map<std::string, std::vector<const VsEditNode*>> byName;
        for (auto* n : ofType("OnNetworkEvent")) byName[n->arg.empty() ? std::string("event") : n->arg].push_back(n);
        open("void on_network_event(event_name, data, sender)");
        for (auto& kv : byName) {
            std::string nm;   // the name as a string literal
            for (char c : kv.first) { if (c == '"' || c == '\\') nm += '\\'; nm += c; }
            out += "        if (event_name == \"" + nm + "\") {\n";
            for (auto* n : kv.second) chain(n->id, "            ");
            out += "        }\n";
        }
        out += "    }\n";
    }
    // Object RPCs: one OnNetRpc_<name>(value) per name (net_obj_rpc delivers it).
    {
        std::map<std::string, std::vector<const VsEditNode*>> byName;
        for (auto* n : ofType("OnNetRpc")) byName[sanitize(n->arg)].push_back(n);
        for (auto& kv : byName) {
            open("void OnNetRpc_" + kv.first + "(value)");
            for (auto* n : kv.second) chain(n->id, "        ");
            out += "    }\n";
        }
    }
    // Clip events: one OnAnimEvent() handler; named nodes check anim_event.
    {
        bool opened = false;
        for (const auto& n : g.nodes) {
            if (n.type != "OnAnimEvent") continue;
            if (!opened) { open("void on_anim_event()"); opened = true; }
            if (n.arg.empty()) chain(n.id, "        ");
            else {
                out += "        if (anim_event == " + VsStrLit(n.arg) + ") {\n";   // brace on this line: the terminator pass would put a ; after a bare if (...)
                chain(n.id, "            ");
                out += "        }\n";
            }
        }
        if (opened) out += "    }\n";
    }
    // A change of language: OnLanguageChanged(event_name), event_name = the new language's code.
    if (auto v = ofType("OnLanguageChanged"); !v.empty()) {
        open("void OnLanguageChanged(string event_name)");
        for (auto* n : v) chain(n->id, "        ");
        out += "    }\n";
    }
    // Sequence events: one OnSequenceEvent(event_name) handler; named nodes check the name.
    {
        bool opened = false;
        for (const auto& n : g.nodes) {
            if (n.type != "OnSequenceEvent") continue;
            if (!opened) { open("void OnSequenceEvent(string event_name)"); opened = true; }
            if (n.arg.empty()) chain(n.id, "        ");
            else {
                out += "        if (event_name == " + VsStrLit(n.arg) + ") {\n";
                chain(n.id, "            ");
                out += "        }\n";
            }
        }
        if (opened) out += "    }\n";
    }
    if (!anyEvent)
        out += "    // Add an On Start / On Update event node to generate runnable code.\n";
    for (const std::string& b : latent.fns.bodies) out += b;
    // C# statement terminators inside method bodies (8+ spaces of indent), then
    // dedent everything one level.
    std::string fin; fin.reserve(out.size() + 64);
    std::size_t a = 0;
    while (a < out.size()) {
        std::size_t e = out.find('\n', a); if (e == std::string::npos) e = out.size();
        std::string line = out.substr(a, e - a);
        const std::size_t ind = line.find_first_not_of(' ');
        if (ind != std::string::npos && ind >= 8) {
            const char last = line.back();
            const bool comment = line.compare(ind, 2, "//") == 0;
            if (!comment && last != '{' && last != '}' && last != ':' && last != ';') line += ';';
        }
        if (line.compare(0, 4, "    ") == 0) line.erase(0, 4);
        fin += line; if (e < out.size()) fin += '\n';
        a = e + 1;
    }
    if (exportIssues) *exportIssues = std::move(report.issues);
    return fin;
}

/// What the Problems button lists: the graph checker's findings only. Nodes the OkayScript preview cannot write are
/// not problems with the graph (it still runs in the game), so they go to `exportOut` for the Script preview and are
/// never shown as red errors in Problems or on the nodes.
/// How many times the checker ran (tests/test_vs_check_cost.cpp counts it).
inline int& VsCheckRuns() { static int n = 0; return n; }
inline std::vector<okay::vs::VsIssue> VsCheckGraph(const VsEditGraph& g, const std::string& text, const std::string& baseDir,
                                                   std::vector<okay::vs::VsIssue>* exportOut = nullptr) {
    ++VsCheckRuns();
    std::vector<okay::vs::VsIssue> issues = okay::vs::ValidateGraphText(text, baseDir);
    std::vector<okay::vs::VsIssue> exp;
    CompileToOkayScript(g, &exp);
    if (exportOut) *exportOut = std::move(exp);
    return issues;
}

// ---- Interaction state -----------------------------------------------------
struct VsEditState {
    VsEditGraph g;
    VsPoint pan{40, 40};
    int    selected = -1;            // primary selection (inspector-ish focus)
    std::set<int> sel;               // every selected node (box select / Ctrl+click)
    // Pending wire drag: a pin the user grabbed.
    bool   dragging = false;
    bool   dragExec = false;   // true = exec pin, false = data pin
    bool   dragFromOutput = false;
    int    dragNode = -1, dragPin = -1;
    bool   openPalette = false;
    VsPoint paletteWorld{0, 0};
    // A wire dropped on empty canvas: the palette opens and the node you pick
    // gets wired to this pin.
    bool   linkPending = false, linkExec = false, linkFromOutput = false;
    int    linkNode = -1, linkPin = -1;
    bool   showScript = false;   // "View as OkayScript" side panel open
    float  zoom = 1.0f;          // canvas zoom (mouse wheel / toolbar), 0.4 .. 2
    bool   frameAll = false;     // fit every node in view on the next frame
    // Box selection (left-drag on empty canvas), in screen space.
    bool   boxing = false;
    VsPoint boxStart{0, 0};
    // Undo / redo: whole-graph snapshots (OkayVS text). A change is committed once
    // the mouse is up and nothing is being typed, so a drag or an edit is one step.
    std::vector<std::string> undo, redo;
    std::string lastText, pendingBase;
    bool   trackInit = false;
    // Comment frames being dragged / resized / renamed.
    int    dragComment = -1, resizeComment = -1, renameComment = -1;
    std::vector<int> commentNodes;   // nodes carried by the comment being dragged
    char   renameBuf[128] = "";
    // Live debugging (Play mode): last seen exec counts + flash timers per node.
    std::map<int, unsigned> seenExec;
    std::map<int, float> flash;
    bool   showMinimap = true;
    bool   liveValues = true;
    bool   showOutline = true;   // events, variables and parameters on the left
    bool   showDetails = false;  // the selected node's description, on the right
    bool   snapGrid = true;      // snap a moved node to the canvas grid on release
    bool   wasNodeDrag = false;
    bool   showHint = true;      // the one-line canvas hint
    bool   panTool = false;      // the hand "Pan" button: plain left-drag pans
    bool   scrollPans = false;   // trackpad mode: plain scroll pans (Ctrl+scroll zooms)
    bool   panLeftNow = false;   // a left-drag pan is running this frame (nodes must not move)
    bool   spaceDown = false; float spaceT = 0.0f, spaceMoved = 0.0f; bool spaceOpen = false;   // Space tap vs Space pan
    float  rightT = 0.0f;        // how long the right button has been down (click vs pan)
    bool   startedEmpty = false; // "Start empty" on the start card: no cards for this graph
    std::size_t valueSig = 0; int sigNodes = 0;   // a fingerprint of the typed values: a change means a number was edited
    int    lastUsed = -1;        // the block added last: the next added block joins it when nothing is selected
    // A wire that was double-clicked: the block picked in the popup is put on it. (exec: from, pin, to; value: consumer, inPin)
    bool   wireInsOn = false, wireInsExec = true; int wireInsA = 0, wireInsB = 0, wireInsC = 0;
    int    focusNode = -1;       // centre the view on this node next frame
    bool   addAtCenter = false;  // "+ Add" in the toolbar: palette, node placed mid-view
    bool   pasteTemplate = false; std::string templateText;   // a chosen template, placed mid-view
    bool   openTemplates = false;   // dev hook and the Templates menu: open the picker
    const char* templateAdd = "";   // components to add on the selected object when the template lands
    // Objects whose graph this editor is editing (embedded graphs have no file path).
    std::vector<std::string> boundGuids;
    // The graph text last written into those objects. A later edit replaces it.
    std::string publishedText;
    // A drop the slot refused, shown on the canvas until the next good drop.
    std::string refHint;
    // A refused wire that has a safe converter: the button inserts that node.
    bool   offerOn = false;
    int    offerCons = -1, offerIn = -1, offerProd = -1, offerOut = -1;
    std::string offerType, offerWhy;
    bool   frameSel = false;     // fit the selected nodes (F)
    bool   needsEvent = false;   // nodes but no event node: hint on the canvas
    // The graph checker's findings for the graph on screen (recomputed when it changes; VsRefreshChecks).
    std::string checkedText; std::vector<okay::vs::VsIssue> issues;   // checkedText: VsCheckKey of the graph last checked
    std::vector<okay::vs::VsIssue> exportIssues;   // nodes the OkayScript preview cannot write (not graph problems)
    bool   showIssues = false;
    std::string refuseMsg; float refuseTime = 0.0f;   // why a wire was refused (shown a few seconds)
    // Text last saved (or the text a new graph was created with). Dirty is true
    // only while the graph differs from this, including after undo and redo.
    std::string cleanText;
    bool baselineSet = false;
    bool missing = false;     // the file in `g.path` could not be read
    int tabId = 0;
};

/// What the graph checker reads, out of the graph's text: everything but where the nodes and comment frames sit on the canvas.
/// Dragging a node changes its "pos" line every frame; the checker (ValidateGraphText: a full parse, and a look on disk for every
/// prefab a node names; then the whole OkayScript export) ran in every frame of a drag, which is where the owner's log saw the
/// Visual Script panel at 263-352 ms a frame (2026-11-25). Neither the checks nor the export depend on positions.
inline std::string VsCheckKey(const std::string& text) {
    std::string key;
    key.reserve(text.size());
    std::size_t a = 0;
    while (a < text.size()) {
        std::size_t e = text.find('\n', a);
        if (e == std::string::npos) e = text.size();
        if (text.compare(a, 4, "pos ") != 0 && text.compare(a, 8, "comment ") != 0 && text.compare(a, 5, "knot ") != 0) key.append(text, a, e - a + (e < text.size() ? 1 : 0));
        a = e + 1;
    }
    return key;
}
/// Run the checker when what it reads changed since the last run (`text` is g.toText()); true when it ran.
inline bool VsRefreshChecks(VsEditState& st, const VsEditGraph& g, const std::string& text, const std::string& baseDir) {
    std::string key = VsCheckKey(text);
    if (key == st.checkedText) return false;
    st.issues = VsCheckGraph(g, text, baseDir, &st.exportIssues);
    st.checkedText = std::move(key);
    return true;
}

inline void VsRefreshDirty(VsEditState& st) {
    if (!st.baselineSet) {
        st.baselineSet = true;
        st.cleanText = st.g.dirty ? std::string() : st.g.toText();
    }
    st.g.dirty = st.g.toText() != st.cleanText;
}
inline void VsMarkClean(VsEditState& st) {
    st.cleanText = st.g.toText();
    st.baselineSet = true;
    st.g.dirty = false;
    st.missing = false;
}

// ---- Undo and redo: whole-graph snapshots (the graph's text) --------------------
/// Step back / forward one change. False when there is nothing to undo / redo. The selection is cleared (the blocks may be gone).
inline bool VsUndo(VsEditState& st) {
    if (st.undo.empty()) return false;
    st.redo.push_back(st.g.toText());
    st.g.fromText(st.undo.back()); st.undo.pop_back();
    st.lastText = st.g.toText(); st.pendingBase.clear();
    VsRefreshDirty(st);
    st.sel.clear(); st.selected = -1;
    return true;
}
inline bool VsRedo(VsEditState& st) {
    if (st.redo.empty()) return false;
    st.undo.push_back(st.g.toText());
    st.g.fromText(st.redo.back()); st.redo.pop_back();
    st.lastText = st.g.toText(); st.pendingBase.clear();
    VsRefreshDirty(st);
    st.sel.clear(); st.selected = -1;
    return true;
}
/// Once a frame, after the frame's edits: a change is noted, and committed as one undo step once `settled` (the mouse is up and
/// nothing is being typed), so a drag or a typed value is one step. At most 200 steps are kept.
inline void VsTrackUndo(VsEditState& st, bool settled) {
    std::string now = st.g.toText();
    if (now != st.lastText) {
        if (st.pendingBase.empty()) st.pendingBase = st.lastText;
        st.lastText = std::move(now);
    }
    if (!st.pendingBase.empty() && settled) {
        if (st.g.toText() != st.pendingBase) {
            st.undo.push_back(std::move(st.pendingBase));
            st.redo.clear();
            if (st.undo.size() > 200) st.undo.erase(st.undo.begin());
        }
        st.pendingBase.clear();
        VsRefreshDirty(st);
    }
}

// ---- Pin types (from the engine's table) -------------------------------------
inline okay::vs::PinType VsOutPinType(const VsEditGraph& g, int nodeId, int pin);
inline okay::vs::PinType VsInPinType(const VsEditGraph& g, int nodeId, int pin);
inline unsigned VsPinColor(okay::vs::PinType t) {
    using okay::vs::PinType;
    switch (t) {
        case PinType::Number: return VsRgba(120, 200, 140, 255);
        case PinType::Bool:   return VsRgba(225, 95, 95, 255);
        case PinType::Text:   return VsRgba(235, 130, 205, 255);
        case PinType::Vec3:   return VsRgba(235, 200, 80, 255);
        case PinType::Object: return VsRgba(95, 165, 255, 255);
        case PinType::List:   return VsRgba(90, 215, 220, 255);
        case PinType::Map:    return VsRgba(180, 140, 230, 255);
        case PinType::Color:  return VsRgba(250, 150, 60, 255);
        default:              return VsRgba(185, 185, 195, 255);
    }
}
/// Can output `outPin` of `producer` feed input `inPin` of `consumer`? `why` says
/// what's wrong (and what to use instead) when it can't.
inline bool VsWireOk(const VsEditGraph& g, int consumer, int inPin, int producer, int outPin, std::string* why = nullptr) {
    const okay::vs::PinType from = VsOutPinType(g, producer, outPin), to = VsInPinType(g, consumer, inPin);
    if (okay::vs::PinCompatible(from, to)) return true;
    if (why) *why = VsPlainWireRefusal(from, to);
    return false;
}

// Several graphs at once. Each tab is its own graph, undo list, selection, pan and zoom.
struct VsSession {
    std::vector<std::unique_ptr<VsEditState>> tabs;
    int active = 0;
    int nextId = 1;
    bool restored = false;
    enum class Ask { None, CloseTab, CloseOthers, CloseAll, CloseWindow, NewGraph } ask = Ask::None;
    int askTab = -1;
    bool newStarter = true;   ///< New graph vs New empty graph, once the save prompt is answered
    bool forceSelect = false; ///< select `active` once; not every frame (that blocked tab clicks)
};
inline VsSession& Session() { static VsSession s; return s; }

inline VsEditState& State() {
    VsSession& s = Session();
    if (s.tabs.empty()) {
        auto t = std::make_unique<VsEditState>();
        t->tabId = s.nextId++;
        VsMarkClean(*t);
        s.tabs.push_back(std::move(t));
        s.active = 0;
    }
    if (s.active < 0 || s.active >= (int)s.tabs.size()) s.active = 0;
    return *s.tabs[(std::size_t)s.active];
}
inline int VsFindTabByPath(const std::string& path) {
    if (path.empty()) return -1;
    const VsSession& s = Session();
    for (int i = 0; i < (int)s.tabs.size(); ++i)
        if (s.tabs[(std::size_t)i]->g.path == path) return i;
    return -1;
}
inline VsEditState& VsAddTab() {
    VsSession& s = Session();
    auto t = std::make_unique<VsEditState>();
    t->tabId = s.nextId++;
    s.tabs.push_back(std::move(t));
    s.active = (int)s.tabs.size() - 1;
    s.forceSelect = true;
    return *s.tabs.back();
}
inline bool VsAnyUnsaved() {
    for (const auto& t : Session().tabs) if (t && t->baselineSet && t->g.dirty) return true;
    return false;
}
inline void VsCollectUnsaved(std::vector<okay::editor::docrec::Buffer>& out) {
    for (const auto& t : Session().tabs) {
        if (!t || !t->g.dirty) continue;
        okay::editor::docrec::Buffer b;
        b.kind = "graph";
        b.key = t->g.path.empty() ? "untitled:" + std::to_string(t->tabId) : t->g.path;
        b.text = t->g.toText();
        out.push_back(std::move(b));
    }
    for (const auto& c : okay::editor::docrec::ClosedStack()) {
        if (c.kind != "graph") continue;
        okay::editor::docrec::Buffer b;
        b.kind = "closed"; b.key = c.key; b.text = c.text;
        out.push_back(std::move(b));
    }
}

// Open a graph in this window and frame one node. Diagnostics uses this when a
// Problems row names a graph; the node browser does not.
inline void ShowGraphText(const std::string& text, int nodeId) {
    VsEditState& st = State();
    st.g.fromText(text);
    st.g.dirty = false;
    if (nodeId >= 0) { st.focusNode = nodeId; st.sel = {nodeId}; st.selected = nodeId; }
    else st.frameAll = true;
}
inline void ShowGraphFile(const std::string& path, int nodeId) {
    VsEditState& st = State();
    if (!st.g.load(path)) return;
    if (nodeId >= 0) { st.focusNode = nodeId; st.sel = {nodeId}; st.selected = nodeId; }
    else st.frameAll = true;
}

// ---- Debugger (Play mode) ----------------------------------------------------
// Filled by the editor when the game freezes at a breakpoint or a step: where it
// stopped and what the running graph could see right then. The game stays frozen
// until `resume` is set (Continue / Step); the editor then carries on the tick.
struct VsDebugView {
    bool stopped = false;
    std::string key;               // the graph file it stopped in
    int node = -1;                 // that node's id
    std::string nodeType, object, event;
    std::vector<okay::vs::NodeGraph::DebugFrame> frames;   // running function calls, outermost first
    std::vector<std::pair<std::string, okay::vs::VsValue>> vars, sceneVars, globalVars;
    okay::vs::NodeGraph* graph = nullptr;                 // the frozen graph (valid while stopped)
    bool focusPending = false;     // centre the canvas on the stopped node once
    bool resume = false;           // carry on (the editor's main loop resumes the tick)
};
inline VsDebugView& DebugView() { static VsDebugView v; return v; }
inline std::string VsDebugText(const okay::vs::VsValue& v) {
    // Every kind reads on one line: lists and maps show their size and first items, a colour its
    // hex, a vector its three numbers.
    return okay::vs::VsDescribe(v);
}

// Optional log sink (the host wires this to its console); no hard dependency on
// the host's logging so this header can be included at global scope.
inline void (*LogFn)(const std::string&, int) = nullptr;
inline void Log(const std::string& m, int lv = 0) { if (LogFn) LogFn(m, lv); }
/// When set, saving a graph goes through this (backups + atomic write). Otherwise ofstream.
inline bool (*SaveTextFile)(const std::string& path, const std::string& content) = nullptr;
/// Host draws "Restore previous version" entries for this path (BackupManager).
inline void (*VsBackupMenu)(const std::string& path) = nullptr;

// ---- The Actions step library (the editor fills these in) -------------------
// One graph editor covers both styles: wired nodes, and the no-code steps
// (Move, Play Sound, Set Variable, Has Tag...) that the Actions component had.
struct VsStepInfo { std::string op, label, group, desc; bool listOnly = false; };   // listOnly: only inside Action Steps
/// Every instruction (cond = false) or condition (cond = true) step, grouped.
inline const std::vector<VsStepInfo>* (*StepCatalog)(bool cond) = nullptr;
/// Draw the friendly editor row for one step. Returns 0, or 1 remove / 2 up / 3 down.
inline int (*StepEditor)(okay::ActionList::Item& it, bool cond, int id, bool& changed) = nullptr;
/// "score += 1", "toward Player @ 3": a one-line reading of a step's arguments.
inline std::string (*StepSummary)(const okay::ActionList::Item& it) = nullptr;
/// Ready-made behaviours: draw the picker popup `popupId`; fills `out` when one is chosen.
inline bool (*TemplatePicker)(const char* popupId, okay::ActionList& out) = nullptr;

inline std::string StepLabel(const std::string& op, bool cond) {
    if (op == "declare_var") return "Starting Value";
    if (StepCatalog) if (const auto* cat = StepCatalog(cond))
        for (const auto& si : *cat) if (si.op == op) return si.label;
    return op;
}
/// The header title a node shows: steps read as what they do ("Move", "If Has Tag").
inline std::string VsNodeTitle(const VsEditNode& n, const VsDef& d) {
    if (n.type == "Action") {
        okay::ActionList::Item it = okay::ActionList::DecodeItem(n.arg);
        return it.op.empty() ? std::string(d.title) : StepLabel(it.op, false);
    }
    if (n.type == "Condition") {
        okay::ActionList::Item it = okay::ActionList::DecodeItem(n.arg);
        return "If " + (it.op.empty() ? std::string("...") : StepLabel(it.op, true));
    }
    if (n.type == "Actions") {
        std::size_t c = okay::ActionList::DecodeItems(n.arg).size();
        return "Steps (" + std::to_string(c) + ")";
    }
    return d.title;
}
/// The grey line under a step node's title (its arguments, read as a sentence).
inline std::string VsStepLine(const VsEditNode& n) {
    if (n.type == "Actions") {
        auto list = okay::ActionList::DecodeItems(n.arg);
        std::string s;
        for (std::size_t i = 0; i < list.size() && i < 3; ++i) { if (i) s += ", "; s += StepLabel(list[i].op, false); }
        if (list.size() > 3) s += ", ...";
        return s.empty() ? std::string("(no steps)") : s;
    }
    okay::ActionList::Item it = okay::ActionList::DecodeItem(n.arg);
    if (it.op == "declare_var")   // name type value
        return (it.args.empty() ? std::string("?") : it.args[0]) + " = " + (it.args.size() > 2 ? it.args[2] : std::string());
    std::string s = StepSummary ? StepSummary(it) : std::string();
    if (s.empty()) for (const auto& a : it.args) { if (!s.empty()) s += ' '; s += a; }
    return s.empty() ? std::string("(click to set up)") : s;
}

// Forget selection / undo history (a different graph was loaded).
inline void ResetEditState(VsEditState& st) {
    st.selected = -1; st.sel.clear(); st.undo.clear(); st.redo.clear();
    st.pendingBase.clear(); st.trackInit = false; st.seenExec.clear(); st.flash.clear();
    st.dragging = st.linkPending = st.boxing = false;
    st.dragComment = st.resizeComment = st.renameComment = -1;
    st.lastUsed = -1; st.wireInsOn = false;
    st.startedEmpty = false;   // a graph that was just opened or reset shows the start cards again if it is empty
}

inline void VsSaveTabList();

// Remember that `go` runs the graph currently open, even when the graph is
// embedded (no file yet). Saving and Play push the editor text onto it.
inline void BindObject(okay::GameObject* go) {
    if (!go || go->Guid().empty()) return;
    auto& b = State().boundGuids;
    if (std::find(b.begin(), b.end(), go->Guid()) == b.end()) b.push_back(go->Guid());
}

// Same file, whether one path is relative ("Assets/a.okayvs") and the other absolute.
inline bool VsSamePath(const std::string& a, const std::string& b) {
    if (a.empty() || b.empty()) return false;
    if (a == b) return true;
    auto norm = [](std::string s) {
        for (char& c : s) if (c == '\\') c = '/';
        while (!s.empty() && s.back() == '/') s.pop_back();
        return s;
    };
    const std::string na = norm(a), nb = norm(b);
    if (na == nb) return true;
    namespace fs = std::filesystem;
    std::error_code ec;
    const std::string ca = fs::weakly_canonical(fs::absolute(na, ec), ec).generic_string();
    const std::string cb = fs::weakly_canonical(fs::absolute(nb, ec), ec).generic_string();
    if (!ca.empty() && ca == cb) return true;
    auto ends = [](const std::string& full, const std::string& rel) {
        return rel.size() < full.size() && full.compare(full.size() - rel.size(), rel.size(), rel) == 0
            && full[full.size() - rel.size() - 1] == '/';
    };
    return ends(na, nb) || ends(nb, na);
}

// Write the open graph to disk when it has edits, then load that text into every
// object that uses it (same file, an object this editor is bound to, or an
// embedded copy of the text we last published). Play also re-reads every
// file-backed graph so a save is what runs, without removing the component.
// False only when a file write was needed and failed.
inline bool PublishOpenGraph(EditorState& ed, bool forPlay) {
    VsEditState& st = State();
    VsEditGraph& g = st.g;
    const bool editorHasGraph = !g.nodes.empty() || !g.path.empty() || !st.boundGuids.empty();
    const std::string text = editorHasGraph ? g.toText() : std::string();
    const bool editorChanged = editorHasGraph && (g.dirty || text != st.publishedText);
    const bool wantFile = editorHasGraph && (editorChanged || (!forPlay && !g.path.empty()) || (!forPlay && !g.nodes.empty()));
    if (wantFile) {
        namespace fs = std::filesystem;
        if (g.path.empty()) {
            std::string stem = "Script";
            if (!st.boundGuids.empty())
                if (okay::GameObject* o = ed.scene().FindByGuid(st.boundGuids.front())) {
                    stem.clear();
                    for (char c : o->name)
                        stem.push_back((std::isalnum((unsigned char)c) || c == '_') ? c : '_');
                    if (stem.empty()) stem = "Object";
                    stem += "Graph";
                }
            fs::path dir = ed.projectDir().empty() ? fs::path("Assets") : fs::path(ed.projectDir()) / "Assets";
            std::error_code mk; fs::create_directories(dir, mk);
            fs::path p = dir / (stem + ".okayvs");
            for (int k = 2; fs::exists(p, mk); ++k) p = dir / (stem + std::to_string(k) + ".okayvs");
            g.path = p.string();
        }
        bool ok = false;
        if (SaveTextFile) ok = SaveTextFile(g.path, g.toText());
        else ok = g.save(g.path);
        if (!ok) return false;
        VsMarkClean(st);
        VsSaveTabList();
    }
    const std::string now = editorHasGraph ? g.toText() : std::string();
    if (editorHasGraph && (!g.path.empty() || !st.boundGuids.empty() || editorChanged)) {
        for (const auto& up : ed.scene().Objects()) {
            if (!up) continue;
            const bool bound = std::find(st.boundGuids.begin(), st.boundGuids.end(), up->Guid()) != st.boundGuids.end();
            for (auto* vc : up->GetComponents<okay::VisualScriptComponent>()) {
                const bool sameFile = !g.path.empty() && VsSamePath(vc->Path(), g.path);
                const bool sameEmbed = vc->Path().empty() && !st.publishedText.empty() && vc->Source() == st.publishedText;
                if (!sameFile && !bound && !sameEmbed) continue;
                std::string err;
                vc->LoadFromText(now, &err);
                if (!g.path.empty()) vc->SetPath(g.path);
            }
        }
        st.publishedText = now;
    }
    if (forPlay) {
        for (const auto& up : ed.scene().Objects()) {
            if (!up) continue;
            for (auto* vc : up->GetComponents<okay::VisualScriptComponent>())
                if (!vc->Path().empty()) vc->Reload();
        }
        okay::SceneSerializer::ClearPrefabCache();
    }
    return true;
}

inline std::string VsTabTitle(const VsEditState& st) {
    if (st.g.path.empty()) return "Untitled";
    return std::filesystem::path(st.g.path).filename().string();
}
inline void VsDirtyTitles(std::vector<std::string>& out) {
    for (const auto& t : Session().tabs)
        if (t && t->g.dirty) out.push_back(std::string("Graph: ") + VsTabTitle(*t));
}
inline void VsSaveTabList() {
    std::ofstream f(okay::edtabs::UserFile("okay_vs_tabs.txt"));   // the per-user folder, not the working folder
    const VsSession& s = Session();
    f << s.active << "\n";
    for (const auto& t : s.tabs)
        if (t && !t->g.path.empty()) f << t->g.path << "\n";
}
/// Open `path` in its own tab, or focus the tab that already has it.
/// A missing file becomes an error tab instead of throwing away the current graph.
inline void OpenFile(const std::string& path) {
    VsSession& s = Session();
    const int have = VsFindTabByPath(path);
    if (have >= 0) { s.active = have; s.forceSelect = true; return; }
    VsEditState& st = VsAddTab();
    ResetEditState(st);
    if (!st.g.load(path)) {
        st.g.clear();
        st.g.path = path;
        VsMarkClean(st);
        st.missing = true;
    } else {
        VsMarkClean(st);
        st.frameAll = true;
    }
    VsSaveTabList();
}
inline void NewGraph(bool starter) {
    VsEditState& st = VsAddTab();
    ResetEditState(st);
    if (starter) SeedStarterGraph(st.g);
    else st.g.clear();
    VsMarkClean(st);
    st.frameAll = true;
    VsSaveTabList();
}
/// New keeps the current graph. When that graph is dirty, ask first, with Save
/// as the focused button, so Enter does not skip the question.
inline void RequestNewGraph(bool starter) {
    VsSession& s = Session();
    VsEditState* cur = (s.active >= 0 && s.active < (int)s.tabs.size()) ? s.tabs[(std::size_t)s.active].get() : nullptr;
    if (cur && cur->baselineSet && cur->g.dirty) {
        s.ask = VsSession::Ask::NewGraph;
        s.askTab = s.active;
        s.newStarter = starter;
        return;
    }
    NewGraph(starter);
}
inline void VsCloseTab(int index, bool remember) {
    VsSession& s = Session();
    if (index < 0 || index >= (int)s.tabs.size()) return;
    VsEditState& st = *s.tabs[(std::size_t)index];
    if (remember && st.g.dirty) {
        okay::editor::docrec::ClosedDoc c;
        c.kind = "graph";
        c.key = st.g.path.empty() ? "untitled:" + std::to_string(st.tabId) : st.g.path;
        c.path = st.g.path;
        c.text = st.g.toText();
        c.baseline = st.cleanText;
        okay::editor::docrec::RememberClosed(std::move(c));
    }
    s.tabs.erase(s.tabs.begin() + index);
    if (s.tabs.empty()) State();
    if (s.active >= (int)s.tabs.size()) s.active = std::max(0, (int)s.tabs.size() - 1);
    if (s.askTab == index) s.ask = VsSession::Ask::None;
    VsSaveTabList();
}
inline bool VsReopenClosedGraph() {
    auto& stack = okay::editor::docrec::ClosedStack();
    for (std::size_t i = 0; i < stack.size(); ++i) {
        if (stack[i].kind != "graph") continue;
        okay::editor::docrec::ClosedDoc c = std::move(stack[i]);
        stack.erase(stack.begin() + (std::ptrdiff_t)i);
        VsEditState& st = VsAddTab();
        ResetEditState(st);
        st.g.fromText(c.text);
        st.g.path = c.path;
        st.cleanText = c.baseline;
        st.baselineSet = true;
        st.g.dirty = st.g.toText() != st.cleanText;
        st.frameAll = true;
        VsSaveTabList();
        return true;
    }
    return false;
}
inline void VsRestoreBuffer(const okay::editor::docrec::Buffer& b) {
    if (b.kind != "graph" && b.kind != "closed") return;
    const std::string path = (b.key.rfind("untitled:", 0) == 0) ? std::string() : b.key;
    const int have = path.empty() ? -1 : VsFindTabByPath(path);
    VsEditState* st = nullptr;
    if (have >= 0) { Session().active = have; st = Session().tabs[(std::size_t)have].get(); }
    else st = &VsAddTab();
    ResetEditState(*st);
    st->g.fromText(b.text);
    st->g.path = path;
    st->cleanText.clear();
    st->baselineSet = true;
    st->missing = false;
    if (!path.empty()) {
        std::ifstream f(path, std::ios::binary);
        if (f) { std::stringstream ss; ss << f.rdbuf(); st->cleanText = ss.str(); }
    }
    st->g.dirty = st->g.toText() != st->cleanText;
    st->frameAll = true;
}
inline void VsRestoreTabs() {
    VsSession& s = Session();
    if (s.restored) return;
    s.restored = true;
    std::ifstream f(okay::edtabs::UserFile("okay_vs_tabs.txt"));
    if (!f) return;
    int active = 0;
    f >> active;
    std::string line;
    std::getline(f, line);
    std::vector<std::string> paths;
    while (std::getline(f, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (!line.empty()) paths.push_back(line);
    }
    if (paths.empty()) return;
    if (s.tabs.size() == 1 && s.tabs[0]->g.nodes.empty() && s.tabs[0]->g.path.empty() && !s.tabs[0]->g.dirty)
        s.tabs.clear();
    for (const std::string& p : paths) OpenFile(p);
    if (active >= 0 && active < (int)s.tabs.size()) s.active = active;
    s.forceSelect = true;
}
/// Save every dirty tab. Untitled graphs land in Assets with a free name.
inline int VsSaveAllTabs(const std::string& projectDir) {
    int n = 0;
    namespace fs = std::filesystem;
    for (auto& tp : Session().tabs) {
        if (!tp || !tp->g.dirty || tp->missing) continue;
        VsEditState& st = *tp;
        std::string p = st.g.path;
        if (p.empty()) {
            const fs::path dir = projectDir.empty() ? fs::path("Assets") : fs::path(projectDir) / "Assets";
            std::error_code ec; fs::create_directories(dir, ec);
            p = (dir / "Untitled.okayvs").string();
            for (int k = 2; fs::exists(p, ec); ++k) p = (dir / ("Untitled" + std::to_string(k) + ".okayvs")).string();
        }
        bool ok = false;
        if (SaveTextFile) { ok = SaveTextFile(p, st.g.toText()); if (ok) st.g.path = p; }
        else ok = st.g.save(p);
        if (ok) { VsMarkClean(st); ++n; }
    }
    if (n) VsSaveTabList();
    return n;
}

// File > Examples and "Try it" on a node. The editor (main.cpp) fills this in.
inline void (*OpenPlayground)(EditorState& ed, const char* demoId) = nullptr;

// Pin geometry helpers (screen positions computed during draw).
struct PinKey { int node; int kind; int side; int index; };  // kind:0 exec,1 data; side:0 in,1 out

// The selected nodes (plus the wires between them) as OkayVS text: what Copy puts
// on the clipboard and Duplicate re-pastes.
inline std::string VsSelectionText(const VsEditGraph& g, const std::set<int>& sel) {
    VsEditGraph part;
    for (const auto& n : g.nodes) if (sel.count(n.id)) part.nodes.push_back(n);
    for (const auto& l : g.execLinks) if (sel.count(l.from) && sel.count(l.to)) part.execLinks.push_back(l);
    for (const auto& l : g.dataLinks) if (sel.count(l.consumer) && sel.count(l.producer)) part.dataLinks.push_back(l);
    // The variables these nodes name (Get Var total, Set Var boss ...) and the record types those variables use go
    // along, so the pasted nodes still know their variable in another graph.
    std::set<std::string> names;
    for (const auto& n : part.nodes) if (!n.arg.empty()) names.insert(n.arg);
    std::set<std::string> types;
    for (const auto& v : g.vars) if (names.count(v.name)) { part.vars.push_back(v); types.insert(v.type); }
    for (const auto& st : g.structs) {
        bool used = types.count(st.name) > 0;
        for (const auto& t : types) if (t.find("<" + st.name + ">") != std::string::npos) used = true;
        if (used) part.structs.push_back(st);
    }
    return "# OkayVS clipboard\n" + part.toText();
}
// Paste OkayVS text into the graph with fresh ids, its top-left at (wx, wy)
// (world units). Returns the new node ids.
/// A node just added from the popup joins the flow: the last node before it when that node still has a free
/// "then" plug (the new node is placed beside it), else a nearby node with a free plug (VisualScriptAutoConnect.hpp).
/// Returns the node it was connected after, or -1. The node must have a flow input.
inline int VsAutoHook(VsEditGraph& g, int newId) {
    VsEditNode* n = g.find(newId);
    if (!n) return -1;
    const VsDef* d = FindDef(n->type);
    if (!d || !d->execIn) return -1;
    std::vector<VsLayoutNode> layout;
    int last = -1;
    for (const VsEditNode& m : g.nodes) {
        if (m.id == newId) continue;
        const VsDef* md = FindDef(m.type);
        if (!md) continue;
        VsLayoutNode ln;
        ln.id = m.id; ln.x = m.x; ln.y = m.y;
        ln.hasFlowOut = !md->execOut.empty();
        for (const VsExecLink& l : g.execLinks) if (l.from == m.id && l.pin == 0) ln.flowOutUsed = true;
        layout.push_back(ln);
        last = m.id;
    }
    int from = VsAutoConnectFrom(layout, n->x, n->y, true);
    if (from < 0 && last >= 0)
        for (const VsLayoutNode& ln : layout)
            if (ln.id == last && ln.hasFlowOut && !ln.flowOutUsed) {
                from = last;
                n->x = ln.x + 260.0f; n->y = ln.y;   // put it beside the node it follows
            }
    if (from < 0) return -1;
    g.linkExec(from, 0, newId);
    return from;
}

/// A node dropped on a flow wire splits it: from -> node -> to. The node needs a flow input and a flow output and
/// no flow wires of its own yet. `wireIndex` indexes g.execLinks. Returns true when it was inserted.
inline bool VsInsertOnWire(VsEditGraph& g, int nodeId, int wireIndex) {
    if (wireIndex < 0 || wireIndex >= (int)g.execLinks.size()) return false;
    const VsEditNode* n = g.find(nodeId);
    const VsDef* d = n ? FindDef(n->type) : nullptr;
    if (!d || !d->execIn || d->execOut.empty()) return false;
    for (const VsExecLink& l : g.execLinks) if (l.from == nodeId || l.to == nodeId) return false;
    const VsExecLink wire = g.execLinks[(std::size_t)wireIndex];
    if (wire.from == nodeId || wire.to == nodeId) return false;
    for (const VsFlowLink& k : VsInsertOnWire(VsFlowLink{wire.from, wire.pin, wire.to}, nodeId, 0)) (void)k;
    g.unlinkExec(wire.from, wire.pin, wire.to);
    g.linkExec(wire.from, wire.pin, nodeId);
    g.linkExec(nodeId, 0, wire.to);
    return true;
}

inline std::set<int> VsPasteText(VsEditGraph& g, const std::string& text, float wx, float wy) {
    VsEditGraph part; part.fromText(text);
    std::set<int> made;
    if (part.nodes.empty()) return made;
    float minX = 1e9f, minY = 1e9f;
    for (const auto& n : part.nodes) { minX = std::min(minX, n.x); minY = std::min(minY, n.y); }
    std::map<int, int> remap;
    for (auto n : part.nodes) {
        int nid = g.nextId++;
        remap[n.id] = nid;
        n.id = nid; n.x = n.x - minX + wx; n.y = n.y - minY + wy;
        g.nodes.push_back(n);
        made.insert(nid);
    }
    // Only wires whose both ends were pasted (an unknown id would become node 0).
    auto has = [&](int id) { return remap.count(id) != 0; };
    for (const auto& l : part.execLinks)
        if (has(l.from) && has(l.to)) g.execLinks.push_back({remap[l.from], l.pin, remap[l.to]});
    for (const auto& l : part.dataLinks)
        if (has(l.consumer) && has(l.producer)) g.dataLinks.push_back({remap[l.consumer], l.inPin, remap[l.producer], l.outPin});
    for (const auto& v : part.vars) {
        bool have = false;
        for (const auto& e : g.vars) if (e.name == v.name) have = true;
        if (!have) g.vars.push_back(v);
    }
    for (const auto& st : part.structs) {
        bool have = false;
        for (const auto& e : g.structs) if (e.name == st.name) have = true;
        if (!have) g.structs.push_back(st);
    }
    if (!part.vars.empty() || !part.structs.empty()) g.declsChanged();
    for (auto c : part.comments) { c.x = c.x - minX + wx; c.y = c.y - minY + wy; g.comments.push_back(c); }
    g.dirty = true;
    return made;
}

// A component or field dragged out of the Inspector. Payload is
// "component\nSprite Renderer" or "field\nposition". Drops a node that reads
// or writes that thing. An unknown name becomes a Const labelled with it.
inline void VsPlaceInspectorDrop(VsEditGraph& g, VsEditState& st, float wx, float wy, const char* raw) {
    std::string s = raw ? raw : "";
    const auto nl = s.find('\n');
    const std::string kind = nl == std::string::npos ? std::string("component") : s.substr(0, nl);
    const std::string name = nl == std::string::npos ? s : s.substr(nl + 1);
    const char* type = "Const";
    std::string arg = name;
    auto is = [&](const char* a) { return name == a; };
    if (kind == "field") {
        if (is("position") || is("localPosition")) { type = "GetPosition"; arg.clear(); }
        else if (is("scale") || is("localScale")) { type = "GetScale"; arg.clear(); }
        else if (is("rotation") || is("eulerAngles") || is("localEulerAngles")) { type = "GetRotation"; arg.clear(); }
        else if (is("color")) { type = "SetColor"; arg.clear(); }
    } else {
        if (is("Transform") || is("Camera") || is("Light")) { type = "GetPosition"; arg.clear(); }
        else if (is("Sprite Renderer") || is("Mesh Renderer") || is("Text Renderer")) { type = "SetColor"; arg.clear(); }
        else if (is("Rigidbody2D") || is("Rigidbody3D")) { type = "SetVelocity"; arg.clear(); }
        else if (is("Audio Source")) { type = "PlaySound"; arg.clear(); }
    }
    const int nid = g.add(type, wx, wy);
    if (VsEditNode* nn = g.find(nid)) nn->arg = arg;
    st.selected = nid;
    st.sel = {nid};
    g.dirty = true;
}

// Can a node of type `d` take a wire from the pending pin? A data pin also has
// to be a type that pin can feed (Number into a Vector3 input stays hidden).
inline bool VsCompatible(const VsDef& d, bool exec, bool fromOutput,
                         okay::vs::PinType pin = okay::vs::PinType::Any) {
    if (exec) return fromOutput ? d.execIn : !d.execOut.empty();
    if (fromOutput) {
        for (int i = 0; i < (int)d.dataIn.size(); ++i)
            if (okay::vs::PinCompatible(pin, okay::vs::InputType(d.type, i))) return true;
        return false;
    }
    for (int i = 0; i < (int)d.dataOut.size(); ++i)
        if (okay::vs::PinCompatible(okay::vs::OutputType(d.type, i, ""), pin)) return true;
    return false;
}

/// How well a node of type `d` fits the wire being dragged, for ranking the wire-drop search (lower is better):
/// 0 a pin of exactly the dragged type (or any flow pin that fits), 1 a pin that takes anything (Any),
/// 2 only a pin that converts (Number into Bool, anything into Text...), -1 it does not fit at all.
/// The same rule picks which pin of the new node the wire goes to (VsPickWirePin, VisualScriptEasyOps.hpp).
inline int VsPinFitRank(okay::vs::PinType have, okay::vs::PinType pinType, bool haveIsOutput) {
    using okay::vs::PinType;
    const PinType from = haveIsOutput ? have : pinType, to = haveIsOutput ? pinType : have;
    if (!okay::vs::PinCompatible(from, to)) return -1;
    if (have != PinType::Any && pinType == have) return 0;
    if (pinType == PinType::Any || have == PinType::Any) return 1;
    return 2;
}
inline int VsPinFitRank(const VsDef& d, bool exec, bool fromOutput, okay::vs::PinType pin = okay::vs::PinType::Any) {
    if (exec) return VsCompatible(d, true, fromOutput) ? 0 : -1;
    int best = -1;
    const int n = fromOutput ? (int)d.dataIn.size() : (int)d.dataOut.size();
    for (int i = 0; i < n; ++i) {
        const int r = VsPinFitRank(pin, fromOutput ? okay::vs::InputType(d.type, i) : okay::vs::OutputType(d.type, i, ""), fromOutput);
        if (r >= 0 && (best < 0 || r < best)) best = r;
    }
    return best;
}

// Node text fields that name an object or a file. Variable names, keys and
// numbers stay plain text.
inline okay::vs::VsRefKind VsArgKind(const std::string& type) {
    if (type == "FindObject" || type == "ObjectExists" || type == "ObjectPosition" ||
        type == "DistanceTo" || type == "LookAt" || type == "MoveTowards" ||
        type == "SetActive" || type == "SetText" || type == "Teleport" ||
        type == "ShowUI" || type == "HideUI") return okay::vs::VsRefKind::Object;
    if (type == "Spawn" || type == "NetSpawnOwned") return okay::vs::VsRefKind::Prefab;
    if (type == "LoadScene") return okay::vs::VsRefKind::Scene;
    if (type == "MusicPlay") return okay::vs::VsRefKind::Audio;
    if (type == "BtRun") return okay::vs::VsRefKind::Graph;
    return okay::vs::VsRefKind::None;
}
inline bool VsArgDefaultsSelf(const std::string& type) {
    return type == "Teleport" || type == "ShowUI" || type == "HideUI" || type == "SetActive" || type == "ObjectPosition";
}
inline okay::vs::VsRefKind VsKindFromWords(const char* words) {
    if (!words || !words[0]) return okay::vs::VsRefKind::None;
    std::string s = words;
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    if (s.find("prefab") != std::string::npos) return okay::vs::VsRefKind::Prefab;
    if (s.find("material") != std::string::npos) return okay::vs::VsRefKind::Material;
    if (s.find("sound") != std::string::npos || s.find("audio") != std::string::npos || s.find("music") != std::string::npos)
        return okay::vs::VsRefKind::Audio;
    if (s.find("scene") != std::string::npos) return okay::vs::VsRefKind::Scene;
    if (s.find("graph") != std::string::npos || s.find("tree file") != std::string::npos) return okay::vs::VsRefKind::Graph;
    if (s.find("anim") != std::string::npos || s.find("clip") != std::string::npos) return okay::vs::VsRefKind::Animation;
    return okay::vs::VsRefKind::None;
}
inline okay::vs::VsRefKind VsPinKind(const std::string& type, int pin, okay::vs::PinType pt, const char* label) {
    if (pt == okay::vs::PinType::Object) return okay::vs::VsRefKind::Object;
    (void)type; (void)pin;
    return VsKindFromWords(label);
}
inline bool VsUpgradeGraph(VsEditGraph& g, okay::Scene& scene) {
    bool changed = false;
    for (auto& n : g.nodes) {
        const okay::vs::VsRefKind ak = VsArgKind(n.type);
        if (ak != okay::vs::VsRefKind::None && okay::vs::VsUpgradeRefString(n.arg, ak, &scene)) changed = true;
        const VsDef* d = FindDef(n.type);
        if (!d) continue;
        for (auto it = n.pinDefault.begin(); it != n.pinDefault.end(); ++it) {
            const char* label = (it->first >= 0 && it->first < (int)d->dataIn.size()) ? d->dataIn[(std::size_t)it->first] : "";
            const okay::vs::VsRefKind pk = VsPinKind(n.type, it->first, okay::vs::InputType(n.type, it->first), label);
            if (pk != okay::vs::VsRefKind::None && okay::vs::VsUpgradeRefString(it->second, pk, &scene)) changed = true;
        }
    }
    return changed;
}
inline std::string VsProjectRelative(const std::string& full, const std::string& project) {
    namespace fs = std::filesystem;
    if (!project.empty()) {
        std::error_code ec;
        fs::path rel = fs::relative(fs::path(full), fs::path(project), ec);
        if (!ec) {
            const std::string s = rel.generic_string();
            if (s.find("..") == std::string::npos) return s;
        }
    }
    return fs::path(full).generic_string();
}
inline std::string VsRefLabel(const std::string& value, okay::vs::VsRefKind kind, bool defaultSelf, okay::Scene& scene) {
    if (value.empty()) return (kind == okay::vs::VsRefKind::Object && defaultSelf) ? std::string("Self") : std::string("None");
    if (value == "@self" || value == "self") return "Self";
    if (value == "@other" || value == "other") return "Other";
    if (okay::vs::VsStartsWith(value, "@guid:")) {
        if (okay::GameObject* o = scene.FindByGuid(value.substr(6))) return o->name;
        return "Missing object";
    }
    if (kind == okay::vs::VsRefKind::Object) {
        if (okay::GameObject* o = scene.Find(value)) return o->name;
        return value;
    }
    const std::string bare = okay::vs::VsBareAsset(value);
    if (bare.empty()) return "None";
    return std::filesystem::path(bare).filename().string();
}
inline const char* VsRefIcon(okay::vs::VsRefKind kind) {
    switch (kind) {
        case okay::vs::VsRefKind::Object: return "Obj";
        case okay::vs::VsRefKind::Prefab: return "Pre";
        case okay::vs::VsRefKind::Material: return "Mat";
        case okay::vs::VsRefKind::Audio: return "Snd";
        case okay::vs::VsRefKind::Animation: return "Anm";
        case okay::vs::VsRefKind::Scene: return "Scn";
        case okay::vs::VsRefKind::Graph: return "Grf";
        default: return "File";
    }
}



/// How many times a choice list walked the Assets folder (tests/test_vs_check_cost.cpp counts it).
inline int& VsChoiceScans() { static int n = 0; return n; }
/// The stems of the files with extension `ext` (lower case, with the dot) under `assets`, walked at most once every `maxAgeSeconds`
/// per folder and extension. A Play Clip, Load Scene or Play Music node draws its dropdown every frame, and each one walked the
/// whole Assets folder every frame (the owner's log, 2026-11-25: the Visual Script panel at 263-352 ms a frame). `now` in seconds.
inline const std::vector<std::string>& VsAssetStems(const std::filesystem::path& assets, const char* ext, double now, double maxAgeSeconds = 2.0) {
    struct Entry { double at = -1e30; std::vector<std::string> stems; };
    static std::map<std::string, Entry> cache;
    Entry& e = cache[assets.generic_string() + "|" + ext];
    if (now - e.at < maxAgeSeconds && now >= e.at) return e.stems;
    e.at = now;
    e.stems.clear();
    ++VsChoiceScans();
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::exists(assets, ec)) return e.stems;
    for (auto it = fs::recursive_directory_iterator(assets, ec); it != fs::recursive_directory_iterator() && !ec; it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        std::string x = it->path().extension().string();
        for (char& c : x) c = (char)std::tolower((unsigned char)c);
        if (x == ext) e.stems.push_back(it->path().stem().string());
    }
    return e.stems;
}

// Clips, tags, scenes and the rest of the named lists a dropdown shows.
inline VsChoiceLists VsGatherChoices(EditorState& ed, const VsEditGraph& g, VsChoiceKind kind) {
    VsChoiceLists lists;
    okay::GameObject* target = ed.selected();
    if (!target)
        for (const auto& id : State().boundGuids)
            if (okay::GameObject* o = ed.scene().FindByGuid(id)) { target = o; break; }
    auto addTarget = [&](const std::string& s) { VsPushUnique(lists.onTarget, s); };
    auto addFall = [&](const std::string& s) { VsPushUnique(lists.fallback, s); };
    namespace fs = std::filesystem;
    fs::path assets = ed.projectDir().empty() ? fs::path("Assets") : fs::path(ed.projectDir()) / "Assets";
    auto scanExt = [&](const char* ext) { for (const std::string& st : VsAssetStems(assets, ext, VsNowSeconds())) addFall(st); };
    switch (kind) {
        case VsChoiceKind::Clip:
            if (target) {
                if (auto* ch = okay::FindAnimComponent<okay::Character>(target))
                    for (const auto& n : ch->ClipNames()) addTarget(n);
                if (auto* ma = okay::FindAnimComponent<okay::ModelAnimator>(target))
                    for (const auto& n : ma->ClipNames()) addTarget(n);
                if (auto* sm = okay::FindAnimComponent<okay::AnimStateMachine>(target))
                    for (const auto& stt : sm->states) if (!stt.clip.empty()) addTarget(stt.clip);
            }
            for (const char* c : {"idle", "walk", "run", "jump", "attack", "hit", "die", "Wave", "Point"}) addFall(c);
            scanExt(".okayanim");
            break;
        case VsChoiceKind::AnimState:
            if (target) if (auto* sm = okay::FindAnimComponent<okay::AnimStateMachine>(target))
                for (const auto& stt : sm->states) addTarget(stt.name);
            break;
        case VsChoiceKind::AnimParam:
            if (target) if (auto* sm = okay::FindAnimComponent<okay::AnimStateMachine>(target))
                for (const auto& p : sm->params) addTarget(p.name);
            for (const auto& n : g.nodes)
                if (n.type == "AnimTrigger" || n.type == "AnimSetFloat" || n.type == "AnimSetBool") addFall(n.arg);
            break;
        case VsChoiceKind::Tag:
            for (const auto& up : ed.scene().Objects())
                if (up && !up->tag.empty()) addTarget(up->tag);
            break;
        case VsChoiceKind::Layer:
            for (int i = 0; i < okay::Layers::kCount; ++i) {
                const std::string nm = okay::Layers::Name(i);
                if (!nm.empty() && nm.rfind("Layer ", 0) != 0) addTarget(nm);
            }
            break;
        case VsChoiceKind::Scene:
            for (int i = 0; i < okay::SceneManager::SceneCount(); ++i) addTarget(okay::SceneManager::SceneName(i));
            scanExt(".okayscene");
            break;
        case VsChoiceKind::Variable:
            for (const auto& v : g.vars) addTarget(v.name);
            for (const auto& n : g.nodes)
                if (VsArgChoice(n.type) == VsChoiceKind::Variable) addFall(n.arg);
            break;
        case VsChoiceKind::Event:
            for (const auto& n : g.nodes)
                if (n.type == "OnEvent" || n.type == "SendEvent" || n.type == "BroadcastEvent") addTarget(n.arg);
            break;
        case VsChoiceKind::Sound:
            scanExt(".wav"); scanExt(".ogg"); scanExt(".mp3");
            break;
        default: break;
    }
    return lists;
}

}  // namespace okay::editor::vsed
#include "VisualScriptClarity.hpp"
#include "VisualScriptGuide.hpp"       // plain sentences, pin tips, next steps, Try this node, the help card
#include "VisualScriptPan.hpp"
#include "VisualScriptPanModel.hpp"
#include "VisualScriptPaletteModel.hpp" // the Add Block list's rules: which blocks it hides, the nodes that fit a wire, favourites
#include "VisualScriptEasyOps.hpp"   // graph-in, graph-out rules for the canvas (needs the graph model above)
#include "VsDebugWatch.hpp"          // the debugger's watch list, wire values and the lit path
#include "VsCollapse.hpp"            // Collapse to Function
#include "VsProjectRefs.hpp"         // find references in every graph of the project; bookmarks
#include "VsGraphDiff.hpp"           // a graph against an earlier save
#include "VsIdeProviders.hpp"        // what Visual Script gives the script IDE's Search Everywhere / quick fixes / plugins
#include "VisualScriptPlainWords.hpp" // every "something is wrong" sentence, in plain words
#include "VisualScriptWhatHappened.hpp" // "Moved 3 steps right": the line under a block that ran
#include "VisualScriptHints.hpp"      // the one-line next-step bar
#include "VsIdeModel.hpp"            // the IDE commands on a graph tab (arrange, collapse, bookmarks, references, watches, fixes)
