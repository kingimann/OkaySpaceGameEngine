#pragma once
// A node-based visual scripting editor for OkayVS graphs (.okayvs). You drag
// nodes onto a canvas, wire execution (triangles) and data (circles) pins, edit
// per-node values, and save a .okayvs file the engine's VisualScriptComponent
// runs at play time. Self-contained: it keeps its own editable model and writes
// the same text format the engine parses (node/pos/exec/data/entry lines).
#include "imgui.h"
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
#include "VisualScriptChoices.hpp"
#include "VisualScriptNames.hpp"
#include "VisualScriptCategories.hpp"
#include "VisualScriptLevels.hpp"
#include "VisualScriptTemplates.hpp"
#include "VsCanvasTools.hpp"
#include "VsVariablePanel.hpp"
#include "VsPlayground.hpp"
#include "VisualScriptNames.hpp"
#include "VisualScriptLevels.hpp"
#include "VisualScriptCategories.hpp"
#include "VisualScriptFriendly.hpp"
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
#include <vector>

namespace okay::editor::vsed {

// ---- Node registry: describes each node type the editor can place ----------
enum VsCat { CAT_EVENT, CAT_FLOW, CAT_VAR, CAT_MATH, CAT_LOGIC, CAT_INPUT, CAT_XFORM, CAT_ACTION, CAT_DATA, CAT_NET, CAT_API, CAT_SCRIPT, CAT_COUNT };

inline ImU32 CatColor(int cat) {
    // Calm, distinct header colours: readable at a glance, not neon.
    switch (cat) {
        case CAT_EVENT:  return IM_COL32(158, 96, 102, 255);
        case CAT_FLOW:   return IM_COL32(86, 112, 158, 255);
        case CAT_VAR:    return IM_COL32(72, 138, 116, 255);
        case CAT_MATH:   return IM_COL32(64, 132, 150, 255);
        case CAT_LOGIC:  return IM_COL32(124, 104, 158, 255);
        case CAT_INPUT:  return IM_COL32(168, 132, 78, 255);
        case CAT_XFORM:  return IM_COL32(70, 146, 146, 255);
        case CAT_ACTION: return IM_COL32(176, 114, 78, 255);
        case CAT_NET:    return IM_COL32(84, 114, 170, 255);
        case CAT_API:    return IM_COL32(122, 142, 88, 255);
        case CAT_SCRIPT: return IM_COL32(150, 118, 72, 255);
        default:         return IM_COL32(108, 114, 128, 255);
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
    static const std::vector<VsDef> defs = ApplyLevels(ApplyFriendlyNames(VsWithBuiltinDefs(VsWithApiDefs(VsWithTargetPins({
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
        // --- Look: weather, fog, post-processing (docs/rendering.md) ---
        {"SetWeather",    "Set Weather",     CAT_ACTION, true, {""}, {"Intensity"}, {}, 1, "none / rain / snow / storm"},
        {"SetFog",        "Set Fog",         CAT_ACTION, true, {""}, {"On","Start","End"}, {}, 0, nullptr},
        {"SetPostEffect", "Set Post Effect", CAT_ACTION, true, {""}, {"Value"}, {}, 1, "bloom / exposure / saturation / ..."},
        // --- Navigation (Nav Mesh Agent) ---
        {"NavMoveTo",    "Nav Move To",    CAT_ACTION, true, {""}, {"Point"}, {}, 0, nullptr},
        {"NavStop",      "Nav Stop",       CAT_ACTION, true, {""}, {}, {}, 0, nullptr},
        {"NavArrived",   "Nav Arrived",    CAT_DATA, false, {}, {}, {"Bool"}, 0, nullptr},
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
        {"Damage", "Damage", CAT_ACTION, true, {""}, {"Amount","Target"}, {}, 0, nullptr},
        {"Heal", "Heal", CAT_ACTION, true, {""}, {"Amount","Target"}, {}, 0, nullptr},
        {"IsDead", "Is Dead", CAT_LOGIC, false, {}, {"Target"}, {"Dead"}, 0, nullptr},
        {"OnDeath", "On Death", CAT_EVENT, false, {""}, {}, {}, 0, nullptr},
        {"OnHealthChanged", "On Health Changed", CAT_EVENT, false, {""}, {}, {}, 0, nullptr},
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
        {"NetIsServer",    "Is Server",      CAT_NET, false, {}, {}, {"Bool"}, 0, nullptr},
        {"NetIsClient",    "Is Client",      CAT_NET, false, {}, {}, {"Bool"}, 0, nullptr},
        {"NetPeers",       "Peer Count",     CAT_NET, false, {}, {}, {"Count"}, 0, nullptr},
        {"NetId",          "My Net Id",      CAT_NET, false, {}, {}, {"Id"}, 0, nullptr},
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
    })))));
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
// The key pressed this frame, as a runtime name ("" if none).
inline std::string VsCapturedKey() {
    for (int i = 0; i < 26; ++i)
        if (ImGui::IsKeyPressed((ImGuiKey)(ImGuiKey_A + i), false)) return std::string(1, (char)('a' + i));
    for (int i = 0; i < 10; ++i)
        if (ImGui::IsKeyPressed((ImGuiKey)(ImGuiKey_0 + i), false) ||
            ImGui::IsKeyPressed((ImGuiKey)(ImGuiKey_Keypad0 + i), false)) return std::string(1, (char)('0' + i));
    struct M { ImGuiKey k; const char* n; };
    const M named[] = {{ImGuiKey_Space, "space"}, {ImGuiKey_Enter, "enter"}, {ImGuiKey_KeypadEnter, "enter"},
                       {ImGuiKey_Tab, "tab"}, {ImGuiKey_Backspace, "backspace"},
                       {ImGuiKey_LeftShift, "shift"}, {ImGuiKey_RightShift, "shift"},
                       {ImGuiKey_LeftCtrl, "ctrl"}, {ImGuiKey_RightCtrl, "ctrl"},
                       {ImGuiKey_UpArrow, "up"}, {ImGuiKey_DownArrow, "down"},
                       {ImGuiKey_LeftArrow, "left"}, {ImGuiKey_RightArrow, "right"}};
    for (const M& m : named) if (ImGui::IsKeyPressed(m.k, false)) return m.n;
    return {};
}
inline bool KeyPicker(std::string& arg, float width) {
    bool changed = false;
    std::string lbl = "Key: " + VsKeyLabel(arg) + "##keypick";
    if (ImGui::Button(lbl.c_str(), ImVec2(width, 0))) ImGui::OpenPopup("##keypickpop");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Click, then press the key you want (or pick it from the list).");
    if (ImGui::BeginPopup("##keypickpop")) {
        ImGui::TextColored(ImVec4(0.55f, 0.8f, 1.0f, 1.0f), "Press any key...");
        ImGui::TextDisabled("or pick one below (Esc closes)");
        if (!ImGui::IsWindowAppearing()) {   // not the click that opened it
            std::string got = VsCapturedKey();
            if (!got.empty()) { arg = got; changed = true; ImGui::CloseCurrentPopup(); }
        }
        ImGui::Separator();
        ImGui::BeginChild("##keylist", ImVec2(250, 230));
        for (const auto& k : VsNamedKeys())
            if (ImGui::Selectable(k.label, arg == k.name)) { arg = k.name; changed = true; ImGui::CloseCurrentPopup(); }
        ImGui::Separator();
        auto grid = [&](char from, char to) {
            int col = 0;
            for (char c = from; c <= to; ++c) {
                char nm[2] = {c, 0}, shown[2] = {(char)std::toupper((unsigned char)c), 0};
                if (col++ % 8) ImGui::SameLine();
                ImGui::PushID(c);
                if (ImGui::Selectable(shown, arg == nm, 0, ImVec2(20, 20))) { arg = nm; changed = true; ImGui::CloseCurrentPopup(); }
                ImGui::PopID();
            }
        };
        grid('a', 'z');
        ImGui::Separator();
        grid('0', '9');
        ImGui::EndChild();
        ImGui::EndPopup();
    }
    return changed;
}

inline const VsDef* FindDef(const std::string& type) {
    for (const auto& d : Registry()) if (type == d.type) return &d;
    return nullptr;
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

// The runtime entry name an event node registers under ("" = not an event):
// "OnStart", "OnKey:space", "OnTimer:2", "OnEvent:Hit", "OnAnimEvent:step"...
// Spaces become '_' so the name stays one token (the runtime does the same).
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
        type == "OnQuestFailed" || type == "OnObjectiveCompleted" || type == "OnQuestEvent") return type;
    if (type == "OnAnimEvent") return arg.empty() ? std::string("OnAnimEvent") : "OnAnimEvent:" + tok(arg, "");
    if (type == "OnSequenceEvent") return arg.empty() ? std::string("OnSequenceEvent") : "OnSequenceEvent:" + tok(arg, "");
    if (type == "OnKey")   return "OnKey:" + tok(arg, "space");
    if (type == "OnKeyUp") return "OnKeyUp:" + tok(arg, "space");
    if (type == "OnTimer") return "OnTimer:" + tok(arg, "1");
    if (type == "OnEvent") return "OnEvent:" + tok(arg, "Event");
    if (type == "OnNetRpc") return "OnNetRpc:" + tok(arg, "rpc");
    if (type == "OnVarChanged" || type == "OnSceneVarChanged" || type == "OnGlobalVarChanged") return type + ":" + tok(arg, "x");
    if (type == "OnLanguageChanged" || type == "OnInteract" || type == "OnDeath" || type == "OnHealthChanged") return type;
    if (type == "OnTagEnter" || type == "OnTagStay" || type == "OnTagExit" || type == "OnCollisionTag")
        return type + ":" + tok(arg, "Player");
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
            {"CraftItem", "item"}, {"LootRoll", "loot"}, {"SpawnPickup", "item"}};
        if (d) if (auto it = kArg.find(d->type); it != kArg.end()) n.arg = it->second;
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
        return o.str();
    }
    bool save(const std::string& p) {
        std::ofstream f(p, std::ios::binary);
        if (!f) return false;
        f << toText();
        path = p; dirty = false; return true;
    }
    void clear() { nodes.clear(); execLinks.clear(); dataLinks.clear(); comments.clear(); vars.clear(); structs.clear(); m_declKey.clear(); nextId = 1; path.clear(); dirty = false; }
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
            }
        }
        // Wires to nodes that weren't loaded (skipped ids, or never there) would
        // make the next save fail to load: drop them.
        auto has = [&](int id) { return find(id) != nullptr; };
        execLinks.erase(std::remove_if(execLinks.begin(), execLinks.end(),
                        [&](const VsExecLink& l) { return !has(l.from) || !has(l.to); }), execLinks.end());
        dataLinks.erase(std::remove_if(dataLinks.begin(), dataLinks.end(),
                        [&](const VsDataLink& l) { return !has(l.consumer) || !has(l.producer); }), dataLinks.end());
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
        nextId = maxId + 1;
    }
};

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
    for (const auto& n : g.nodes) if (n.id == id) return &n;
    return nullptr;
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
            bool objectUse = n.type == "Self" || n.type == "Other" || n.type == "FindObject" || n.type == "Raycast3D" || n.type == "SpringStep" ||
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

// ---- Interaction state -----------------------------------------------------
struct VsEditState {
    VsEditGraph g;
    ImVec2 pan{40, 40};
    int    selected = -1;            // primary selection (inspector-ish focus)
    std::set<int> sel;               // every selected node (box select / Ctrl+click)
    // Pending wire drag: a pin the user grabbed.
    bool   dragging = false;
    bool   dragExec = false;   // true = exec pin, false = data pin
    bool   dragFromOutput = false;
    int    dragNode = -1, dragPin = -1;
    bool   openPalette = false;
    ImVec2 paletteWorld{0, 0};
    // A wire dropped on empty canvas: the palette opens and the node you pick
    // gets wired to this pin.
    bool   linkPending = false, linkExec = false, linkFromOutput = false;
    int    linkNode = -1, linkPin = -1;
    bool   showScript = false;   // "View as OkayScript" side panel open
    float  zoom = 1.0f;          // canvas zoom (mouse wheel / toolbar), 0.4 .. 2
    bool   frameAll = false;     // fit every node in view on the next frame
    // Box selection (left-drag on empty canvas), in screen space.
    bool   boxing = false;
    ImVec2 boxStart{0, 0};
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
    // The graph checker's findings for the graph on screen (recomputed when it changes).
    std::string checkedText; std::vector<okay::vs::VsIssue> issues;
    bool   showIssues = false;
    std::string refuseMsg; float refuseTime = 0.0f;   // why a wire was refused (shown a few seconds)
    // Text last saved (or the text a new graph was created with). Dirty is true
    // only while the graph differs from this, including after undo and redo.
    std::string cleanText;
    bool baselineSet = false;
    bool missing = false;     // the file in `g.path` could not be read
    int tabId = 0;
};

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

// ---- Pin types (from the engine's table) -------------------------------------
inline okay::vs::PinType VsOutPinType(const VsEditGraph& g, int nodeId, int pin);
inline okay::vs::PinType VsInPinType(const VsEditGraph& g, int nodeId, int pin);
inline ImU32 VsPinColor(okay::vs::PinType t) {
    using okay::vs::PinType;
    switch (t) {
        case PinType::Number: return IM_COL32(120, 200, 140, 255);
        case PinType::Bool:   return IM_COL32(225, 95, 95, 255);
        case PinType::Text:   return IM_COL32(235, 130, 205, 255);
        case PinType::Vec3:   return IM_COL32(235, 200, 80, 255);
        case PinType::Object: return IM_COL32(95, 165, 255, 255);
        case PinType::List:   return IM_COL32(90, 215, 220, 255);
        case PinType::Map:    return IM_COL32(180, 140, 230, 255);
        case PinType::Color:  return IM_COL32(250, 150, 60, 255);
        default:              return IM_COL32(185, 185, 195, 255);
    }
}
/// Can output `outPin` of `producer` feed input `inPin` of `consumer`? `why` says
/// what's wrong (and what to use instead) when it can't.
inline bool VsWireOk(const VsEditGraph& g, int consumer, int inPin, int producer, int outPin, std::string* why = nullptr) {
    const okay::vs::PinType from = VsOutPinType(g, producer, outPin), to = VsInPinType(g, consumer, inPin);
    if (okay::vs::PinCompatible(from, to)) return true;
    if (why) {
        auto article = [](const char* w) {
            const char c = w && w[0] ? (char)std::tolower((unsigned char)w[0]) : 'x';
            return (c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u') ? "an " : "a ";
        };
        *why = std::string(article(okay::vs::PinTypeName(from))) + okay::vs::PinTypeName(from)
             + " can't go into " + article(okay::vs::PinTypeName(to)) + okay::vs::PinTypeName(to) + " input";
        const std::string h = okay::vs::ConversionHint(from, to);
        if (!h.empty()) *why += " - " + h;
    }
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
    std::ofstream f("okay_vs_tabs.txt");
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
    std::ifstream f("okay_vs_tabs.txt");
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
    return "# OkayVS clipboard\n" + part.toText();
}
// Paste OkayVS text into the graph with fresh ids, its top-left at (wx, wy)
// (world units). Returns the new node ids.
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
// Read a Hierarchy / Scene object or a Project file dropped on a slot.
// True when `value` changed. A wrong type sets st.refHint and leaves `value`.
inline bool VsApplyDrop(EditorState& ed, VsEditState& st, okay::vs::VsRefKind kind, std::string& value) {
    using okay::vs::VsRefKind;
    if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("GO_PTR")) {
        okay::GameObject* go = p->Data ? *(okay::GameObject**)p->Data : nullptr;
        if (kind != VsRefKind::Object) { st.refHint = "That is an object. This slot wants a file from the Project window."; return false; }
        if (!go) return false;
        value = go->Guid().empty() ? go->name : okay::vs::VsGuidRef(go->Guid());
        st.refHint.clear();
        return true;
    }
    if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("ASSET_PATH")) {
        const std::string path = p->Data ? std::string((const char*)p->Data) : std::string();
        if (kind == VsRefKind::Object) { st.refHint = "That is a file. Drag an object from the Hierarchy or Scene view."; return false; }
        if (!okay::vs::VsExtOk(kind, path)) {
            st.refHint = std::string("Wrong file. This slot wants ") + okay::vs::VsRefKindName(kind) + ".";
            return false;
        }
        value = okay::vs::VsAssetRef(kind, VsProjectRelative(path, ed.projectDir()));
        st.refHint.clear();
        return true;
    }
    return false;
}

// One reference slot: name, picker, clear, and drag-drop. True when `value` changed.
inline bool VsDrawRefSlot(EditorState& ed, VsEditState& st, std::string& value, okay::vs::VsRefKind kind,
                          bool defaultSelf, float width, const char* id) {
    using okay::vs::VsRefKind;
    bool changed = false;
    const std::string label = VsRefLabel(value, kind, defaultSelf, ed.scene());
    std::string shown = std::string(VsRefIcon(kind)) + " " + label;
    const float xw = 22.0f;
    ImGui::PushID(id);
    if (ImGui::Button(shown.c_str(), ImVec2(std::max(40.0f, width - xw - 4.0f), 0)))
        ImGui::OpenPopup("pick");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(kind == VsRefKind::Object
            ? "Drag an object from the Hierarchy or the Scene view, or click to pick.\nSelf is this object."
            : "Drag a file from the Project window, or click to pick.");
    if (ImGui::BeginDragDropTarget()) {
        if (VsApplyDrop(ed, st, kind, value)) changed = true;
        ImGui::EndDragDropTarget();
    }
    ImGui::SameLine(0, 4);
    if (ImGui::Button("x", ImVec2(xw, 0))) { value.clear(); changed = true; }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(defaultSelf ? "Clear (back to Self)" : "Clear");
    if (ImGui::BeginPopup("pick")) {
        static char q[64] = "";
        ImGui::SetNextItemWidth(220);
        ImGui::InputTextWithHint("##q", "Search", q, sizeof(q));
        std::string needle = q;
        for (char& c : needle) c = (char)std::tolower((unsigned char)c);
        auto row = [&](const char* name, const std::string& stored) {
            std::string h = name;
            for (char& c : h) c = (char)std::tolower((unsigned char)c);
            if (!needle.empty() && h.find(needle) == std::string::npos) return;
            if (ImGui::Selectable(name)) { value = stored; changed = true; ImGui::CloseCurrentPopup(); }
        };
        if (kind == VsRefKind::Object) {
            row("Self", "@self");
            row("Other", "@other");
            for (const auto& up : ed.scene().Objects()) if (up) row(up->name.c_str(), okay::vs::VsGuidRef(up->Guid()));
        } else {
            namespace fs = std::filesystem;
            fs::path assets = ed.projectDir().empty() ? fs::path("Assets") : fs::path(ed.projectDir()) / "Assets";
            std::error_code ec; int shownN = 0;
            if (fs::exists(assets, ec))
                for (auto it = fs::recursive_directory_iterator(assets, ec); it != fs::recursive_directory_iterator(); it.increment(ec)) {
                    if (ec || !it->is_regular_file(ec)) continue;
                    const std::string full = it->path().string();
                    if (!okay::vs::VsExtOk(kind, full)) continue;
                    const std::string rel = VsProjectRelative(full, ed.projectDir());
                    row(rel.c_str(), okay::vs::VsAssetRef(kind, rel));
                    if (++shownN > 400) break;
                }
            if (!shownN) ImGui::TextDisabled("Nothing of that type in Assets.");
        }
        ImGui::EndPopup();
    }
    ImGui::PopID();
    return changed;
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
    auto scanExt = [&](const char* ext) {
        std::error_code ec;
        if (!fs::exists(assets, ec)) return;
        for (auto it = fs::recursive_directory_iterator(assets, ec); it != fs::recursive_directory_iterator() && !ec; it.increment(ec)) {
            if (!it->is_regular_file(ec)) continue;
            std::string e = it->path().extension().string();
            for (char& c : e) c = (char)std::tolower((unsigned char)c);
            if (e == ext) addFall(it->path().stem().string());
        }
    };
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

inline bool VsDrawChoice(EditorState& ed, const VsEditGraph& g, std::string& value, VsChoiceKind kind, float width) {
    const VsChoiceLists lists = VsGatherChoices(ed, g, kind);
    const std::vector<std::string> rows = VsChoiceRows(kind, lists);
    const bool missing = VsChoiceMissing(value, rows);
    bool changed = false;
    const std::string preview = value.empty() ? std::string("(choose a ") + VsChoiceKindName(kind) + ")" : value;
    ImGui::SetNextItemWidth(width);
    if (missing) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.55f, 0.45f, 1.0f));
    bool colorPushed = missing;
    if (ImGui::BeginCombo("##choice", preview.c_str())) {
        if (colorPushed) { ImGui::PopStyleColor(); colorPushed = false; }   // the open list uses the normal text colour
        auto section = [&](const char* title, const std::vector<std::string>& items) {
            if (items.empty()) return;
            ImGui::TextDisabled("%s", title);
            for (const auto& c : items)
                if (ImGui::Selectable(c.c_str(), c == value)) { value = c; changed = true; }
        };
        if (kind == VsChoiceKind::Clip && !lists.onTarget.empty()) {
            section("On this object", lists.onTarget);
            section("Other clips", lists.fallback);
        } else {
            section(kind == VsChoiceKind::Clip ? "Clips" : "Choices", rows);
        }
        if (rows.empty()) ImGui::TextDisabled("Nothing to pick yet. You can still drop a file.");
        ImGui::EndCombo();
    }
    if (colorPushed) ImGui::PopStyleColor();
    if (ImGui::IsItemHovered()) {
        if (missing) ImGui::SetTooltip("\"%s\" is not in the list any more. Pick another, or drop a file.", value.c_str());
        else ImGui::SetTooltip("Pick a %s, or drop one from the Project window.", VsChoiceKindName(kind));
    }
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("ASSET_PATH")) {
            const std::string path = p->Data ? (const char*)p->Data : "";
            const bool clip = kind == VsChoiceKind::Clip && (okay::vs::VsExtOk(okay::vs::VsRefKind::Animation, path));
            const bool sound = kind == VsChoiceKind::Sound && okay::vs::VsExtOk(okay::vs::VsRefKind::Audio, path);
            const bool scene = kind == VsChoiceKind::Scene && okay::vs::VsExtOk(okay::vs::VsRefKind::Scene, path);
            if (clip || sound || scene) {
                value = std::filesystem::path(path).stem().string();
                changed = true;
            } else {
                State().refHint = std::string("That file is not a ") + VsChoiceKindName(kind) + ".";
            }
        }
        ImGui::EndDragDropTarget();
    }
    if (missing) {   // drawn on the canvas under the box (a widget here would land at the window's left edge)
        const ImVec2 r0 = ImGui::GetItemRectMin(), r1 = ImGui::GetItemRectMax();
        ImGui::GetWindowDrawList()->AddText(ImVec2(r0.x, r1.y + 1.0f), IM_COL32(255, 140, 115, 255), "Not in the list");
    }
    return changed;
}
}  // namespace okay::editor::vsed
#include "VisualScriptClarity.hpp"
#include "VisualScriptPalette.hpp"
namespace okay::editor::vsed {

// ---- The editor window -----------------------------------------------------
// Returns nothing; manages its own state. `ed` is used for "Attach to selected".
inline void Draw(EditorState& ed, bool* open) {
    ImGui::SetNextWindowSize(ImVec2(1180, 760), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Visual Script", open)) { ImGui::End(); return; }
    // A floating window remembered tiny (or squashed to its toolbar) has no room
    // for the canvas: give it a usable size.
    if (!ImGui::IsWindowDocked() && (ImGui::GetWindowSize().y < 300.0f || ImGui::GetWindowSize().x < 500.0f))
        ImGui::SetWindowSize(ImVec2(std::max(ImGui::GetWindowSize().x, 980.0f), 600.0f));
    VsRestoreTabs();
    // Closes are applied on the next frame, before any reference to a tab is taken.
    static std::vector<int> s_closeIds;
    static bool s_closeRemember = false;
    if (!s_closeIds.empty()) {
        std::sort(s_closeIds.begin(), s_closeIds.end(), std::greater<int>());
        for (int id : s_closeIds) VsCloseTab(id, s_closeRemember);
        s_closeIds.clear();
    }
    for (auto& tab : Session().tabs) if (tab) VsRefreshDirty(*tab);
    if (open && !*open && VsAnyUnsaved()) { *open = true; Session().ask = VsSession::Ask::CloseWindow; }
    // Tabs first, so a click this frame selects that graph before the canvas is drawn.
    // SetSelected every frame kept the old tab and the canvas never followed the click.
    ImGuiIO& io = ImGui::GetIO();
    const bool winFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    const bool typing = io.WantTextInput;
    {
        VsSession& ses = Session();
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(14.0f, 8.0f));
        if (ImGui::BeginTabBar("##vstabs", ImGuiTabBarFlags_FittingPolicyScroll)) {
            for (int i = 0; i < (int)ses.tabs.size(); ++i) {
                VsEditState& tab = *ses.tabs[(std::size_t)i];
                std::string title = VsTabTitle(tab);
                if (tab.missing) title += " (missing)";
                if (tab.g.dirty) title += " \xE2\x97\x8F";
                title += "###vstab" + std::to_string(tab.tabId);
                bool openTab = true;
                ImGuiTabItemFlags fl = (ses.forceSelect && i == ses.active) ? ImGuiTabItemFlags_SetSelected : 0;
                if (ImGui::BeginTabItem(title.c_str(), &openTab, fl)) {
                    ses.active = i;
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("%s", tab.g.path.empty() ? "Not saved yet" : tab.g.path.c_str());
                        if (ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) {
                            if (tab.g.dirty) { ses.ask = VsSession::Ask::CloseTab; ses.askTab = i; }
                            else s_closeIds.push_back(i);
                        }
                        if (ImGui::IsMouseDragging(ImGuiMouseButton_Left, 12.0f)) {
                            const float dx = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left).x;
                            if (dx > 56.0f && i + 1 < (int)ses.tabs.size()) {
                                std::swap(ses.tabs[(std::size_t)i], ses.tabs[(std::size_t)i + 1]);
                                ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);
                                VsSaveTabList();
                            } else if (dx < -56.0f && i > 0) {
                                std::swap(ses.tabs[(std::size_t)i], ses.tabs[(std::size_t)i - 1]);
                                ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);
                                VsSaveTabList();
                            }
                        }
                    }
                    if (ImGui::BeginPopupContextItem()) {
                        if (ImGui::MenuItem("Close")) {
                            if (tab.g.dirty) { ses.ask = VsSession::Ask::CloseTab; ses.askTab = i; }
                            else s_closeIds.push_back(i);
                        }
                        if (ImGui::MenuItem("Close others")) { ses.ask = VsSession::Ask::CloseOthers; ses.askTab = i; }
                        if (ImGui::MenuItem("Close all")) ses.ask = VsSession::Ask::CloseAll;
                        if (ImGui::MenuItem("Reopen closed tab", "Ctrl+Shift+T", false, !okay::editor::docrec::ClosedStack().empty()))
                            VsReopenClosedGraph();
                        ImGui::EndPopup();
                    }
                    ImGui::EndTabItem();
                }
                if (!openTab) {
                    if (tab.g.dirty) { ses.ask = VsSession::Ask::CloseTab; ses.askTab = i; }
                    else { s_closeRemember = false; s_closeIds.push_back(i); }
                }
            }
            ses.forceSelect = false;
            if (ImGui::TabItemButton("+", ImGuiTabItemFlags_Trailing | ImGuiTabItemFlags_NoTooltip)) RequestNewGraph(true);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("New graph");
            ImGui::EndTabBar();
        }
        ImGui::PopStyleVar();
        if (winFocused && !typing && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Tab)) {
            const int n = (int)ses.tabs.size();
            if (n > 1) { ses.active = (ses.active + (io.KeyShift ? n - 1 : 1)) % n; ses.forceSelect = true; }
        }
        if (winFocused && !typing && io.KeyCtrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_T)) VsReopenClosedGraph();
        if (winFocused && !typing && io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_W)) {
            if (ses.active >= 0 && ses.active < (int)ses.tabs.size() && ses.tabs[(std::size_t)ses.active]->g.dirty) {
                ses.ask = VsSession::Ask::CloseTab; ses.askTab = ses.active;
            } else if (ses.tabs.size() > 1) { s_closeRemember = false; s_closeIds.push_back(ses.active); }
        }
    }
    VsEditState& st = State();
    VsEditGraph& g = st.g;
    // Names and paths already in the file become references when the scene
    // (or the file extension) tells us what they point at. Older files still run.
    if (VsUpgradeGraph(g, ed.scene())) g.dirty = true;
    // Record exec counts + last values so a running graph can be watched live.
    okay::vs::NodeGraph::Tracing() = true;
    if (!st.trackInit) { st.lastText = g.toText(); st.trackInit = true; }

    auto doUndo = [&] {
        if (st.undo.empty()) return;
        st.redo.push_back(g.toText());
        g.fromText(st.undo.back()); st.undo.pop_back();
        st.lastText = g.toText(); st.pendingBase.clear();
        VsRefreshDirty(st);
        st.sel.clear(); st.selected = -1;
    };
    auto doRedo = [&] {
        if (st.redo.empty()) return;
        st.undo.push_back(g.toText());
        g.fromText(st.redo.back()); st.redo.pop_back();
        st.lastText = g.toText(); st.pendingBase.clear();
        VsRefreshDirty(st);
        st.sel.clear(); st.selected = -1;
    };

    // ---- Toolbar: File / Edit / + Add / Templates ... Fit / zoom / View --------------
    // Kept short on purpose: everything else is a shortcut, the right-click menu,
    // or one of these menus.
    auto doSave = [&]() {
        // Saving writes the file and reloads every object that runs this graph,
        // including ones whose path was stored differently and embedded copies.
        if (!PublishOpenGraph(ed, false)) { Log("Save failed", 2); return; }
        if (!st.issues.empty()) st.showIssues = true;
        int n = 0;
        for (const auto& up : ed.scene().Objects())
            for (auto* vc : up->GetComponents<VisualScriptComponent>())
                if (!g.path.empty() && vc->Path() == g.path) ++n;
        Log("Saved visual script " + (g.path.empty() ? std::string("(empty)") : g.path)
            + (n ? " (updated " + std::to_string(n) + " object(s))" : std::string()));
    };
    auto doAttach = [&](GameObject* o) {
        // Save what's on screen first: attaching loads the file, and an unsaved
        // edit would otherwise silently not be what runs.
        BindObject(o);
        if (!PublishOpenGraph(ed, false)) { Log("Couldn't save the graph", 2); return; }
        auto* vc = o->GetComponent<VisualScriptComponent>();
        if (!vc) vc = o->AddComponent<VisualScriptComponent>();
        if (vc->Path() != g.path) vc->LoadFromFile(g.path);
        ed.dirty = true;
        Log("Attached visual script to " + o->name);
    };
    bool addComment = false;
    bool openTpl = false;
    int alignReq = 0;   // 1 left, 2 right, 3 top, 4 bottom
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12.0f, 8.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 7.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 8.0f));
    if (ImGui::Button("Graph")) ImGui::OpenPopup("##vsfile");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("New, open, save, undo, align, and the panels");
    if (ImGui::BeginPopup("##vsfile")) {
        if (ImGui::MenuItem("New graph")) RequestNewGraph(true);
        if (ImGui::MenuItem("New empty graph")) RequestNewGraph(false);
        if (ImGui::MenuItem("Save all graphs")) VsSaveAllTabs(ed.projectDir());
        if (ImGui::BeginMenu("Restore previous version", !g.path.empty())) {
            // Filled by the host when backups are wired. The menu stays so the
            // command is visible; the host draws the versions via VsBackupMenu.
            if (VsBackupMenu) VsBackupMenu(g.path);
            else ImGui::TextDisabled("No backups yet.");
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Open")) {
            namespace fs = std::filesystem;
            fs::path assets = ed.projectDir().empty() ? fs::path("Assets") : fs::path(ed.projectDir()) / "Assets";
            std::error_code ec; int shown = 0;
            if (fs::exists(assets, ec))
                for (auto& e : fs::recursive_directory_iterator(assets, ec)) {
                    if (!e.is_regular_file() || e.path().extension() != ".okayvs") continue;
                    ++shown;
                    if (ImGui::MenuItem(fs::relative(e.path(), assets, ec).string().c_str())) OpenFile(e.path().string());
                }
            if (!shown) ImGui::TextDisabled("No graphs in Assets yet.");
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Save", "Ctrl+S")) doSave();
        ImGui::Separator();
        GameObject* selObj = ed.selected();
        std::string attachLbl = selObj ? "Attach to " + selObj->name : std::string("Attach to selected object");
        if (ImGui::MenuItem(attachLbl.c_str(), nullptr, false, selObj != nullptr)) doAttach(selObj);
        ImGui::Separator();
        if (ImGui::MenuItem("Undo", "Ctrl+Z", false, !st.undo.empty())) doUndo();
        if (ImGui::MenuItem("Redo", "Ctrl+Y", false, !st.redo.empty())) doRedo();
        if (ImGui::MenuItem("Select all", "Ctrl+A")) { st.sel.clear(); for (const auto& n : g.nodes) st.sel.insert(n.id); }
        if (ImGui::MenuItem("Comment around selection", "C", false, !st.sel.empty())) addComment = true;
        ImGui::MenuItem("Snap to grid", nullptr, &st.snapGrid);
        if (ImGui::BeginMenu("Align selection", st.sel.size() >= 2)) {
            if (ImGui::MenuItem("Left")) alignReq = 1;
            if (ImGui::MenuItem("Right")) alignReq = 2;
            if (ImGui::MenuItem("Top")) alignReq = 3;
            if (ImGui::MenuItem("Bottom")) alignReq = 4;
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Templates...")) st.openTemplates = true;
        ImGui::Separator();
        ImGui::MenuItem("Outline", nullptr, &st.showOutline);
        ImGui::MenuItem("Node details", nullptr, &st.showDetails);
        ImGui::MenuItem("Minimap", nullptr, &st.showMinimap);
        ImGui::MenuItem("Live values while playing", nullptr, &st.liveValues);
        ImGui::MenuItem("Show as OkayScript", nullptr, &st.showScript);
        ImGui::MenuItem("Canvas hint", nullptr, &st.showHint);
        ImGui::Separator();
        ImGui::TextDisabled("Right-click or Space adds a node");
        ImGui::TextDisabled("Scroll zooms. Home fits everything. F fits the selection.");
        ImGui::EndPopup();
    }
    if (st.openTemplates) { ImGui::OpenPopup("##vsgraphtemplates"); st.openTemplates = false; }
    ImGui::SetNextWindowSize(ImVec2(560, 620), ImGuiCond_Appearing);
    if (ImGui::BeginPopup("##vsgraphtemplates")) {
        ImGui::TextUnformatted("Templates");
        ImGui::TextDisabled("Replaces this graph. If an object is selected, it is attached and missing parts are added.");
        static char tf[64] = "";
        static int tlevel = 0;
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##tplfind", "Search templates", tf, sizeof(tf));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 2));
        for (int i = 0; i < 4; ++i) {
            if (i) ImGui::SameLine();
            const bool on = tlevel == i;
            if (on) ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(70, 110, 160, 255));
            if (ImGui::Button(VsLevelFilterName((VsLevelFilter)i), ImVec2(120, 0))) tlevel = i;
            if (on) ImGui::PopStyleColor();
        }
        ImGui::PopStyleVar();
        std::string needle = tf;
        for (char& c : needle) c = (char)std::tolower((unsigned char)c);
        ImGui::BeginChild("##tpllist", ImVec2(0, 460), ImGuiChildFlags_Borders);
        const char* group = nullptr;
        for (const VsTemplate& t : VsTemplates()) {
            if (!VsLevelPasses(t.level, (VsLevelFilter)tlevel)) continue;
            std::string hay = std::string(t.name) + " " + t.blurb + " " + t.category;
            for (char& c : hay) c = (char)std::tolower((unsigned char)c);
            if (!needle.empty() && hay.find(needle) == std::string::npos) continue;
            if (!group || std::strcmp(group, t.category) != 0) {
                group = t.category;
                ImGui::Spacing();
                ImGui::TextDisabled("%s", t.category);
            }
            ImGui::PushID(t.name);
            const bool pick = ImGui::Selectable("##tpl", false, 0, ImVec2(0, ImGui::GetTextLineHeight() + 8));
            const ImVec2 p = ImGui::GetItemRectMin();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddText(ImVec2(p.x + 4, p.y + 2), ImGui::GetColorU32(ImGuiCol_Text), t.name);
            const char* badge = VsLevelBadge(t.level);
            const float bw = ImGui::CalcTextSize(badge).x + 10;
            const ImU32 bg = t.level == VsLevel::Basic ? IM_COL32(46, 110, 78, 255)
                           : t.level == VsLevel::Advanced ? IM_COL32(96, 72, 120, 255)
                           : IM_COL32(52, 78, 120, 255);
            dl->AddRectFilled(ImVec2(p.x + 220, p.y + 3), ImVec2(p.x + 220 + bw, p.y + ImGui::GetTextLineHeight() + 4), bg, 3);
            dl->AddText(ImVec2(p.x + 225, p.y + 3), IM_COL32(230, 236, 244, 255), badge);
            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::PushTextWrapPos(320);
                ImGui::TextUnformatted(t.blurb);
                if (t.needs && t.needs[0]) ImGui::TextDisabled("%s", t.needs);
                // A small sketch of the graph: one block per node, in file order.
                int shown = 0;
                float x = 0;
                for (std::size_t i = 0; i + 5 < t.graph.size() && shown < 8; ++i) {
                    if (t.graph.compare(i, 5, "node ") != 0) continue;
                    dl = ImGui::GetWindowDrawList();
                    const ImVec2 a = ImGui::GetCursorScreenPos();
                    ImGui::Dummy(ImVec2(36, 16));
                    ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(a.x + x, a.y), ImVec2(a.x + x + 28, a.y + 14),
                                                              IM_COL32(80, 120, 160, 255), 2);
                    x += 32; ++shown; i += 4;
                }
                ImGui::PopTextWrapPos();
                ImGui::EndTooltip();
            }
            if (pick) {
                g.fromText(t.graph);
                g.path.clear();   // don't overwrite whichever file was open before
                ResetEditState(st);
                st.boundGuids.clear();
                st.frameAll = true;
                g.dirty = true;
                st.publishedText = g.toText();
                if (GameObject* go = ed.selected()) {
                    auto* vs = go->GetComponent<VisualScriptComponent>();
                    if (!vs) vs = go->AddComponent<VisualScriptComponent>();
                    std::string err;
                    vs->LoadFromText(g.toText(), &err);
                    vs->SetPath("");
                    BindObject(go);
                    VsAddTemplateComponents(go, t.add);
                    ed.dirty = true;
                }
                ImGui::CloseCurrentPopup();
            }
            ImGui::PopID();
        }
        ImGui::EndChild();
        ImGui::EndPopup();
    }
    if (openTpl) ImGui::OpenPopup("##vstemplates");
    if (TemplatePicker) {
        okay::ActionList tmp;
        if (TemplatePicker("##vstemplates", tmp)) {
            st.templateText = okay::vs::ActionListToGraph(tmp);
            st.pasteTemplate = true;
        }
    }
    if (winFocused && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) doSave();
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.22f, 0.40f, 0.34f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.28f, 0.50f, 0.42f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.18f, 0.34f, 0.28f, 1.0f));
    if (ImGui::Button("Add node")) st.addAtCenter = true;
    ImGui::PopStyleColor(3);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Add a node in the middle of the view.\nRight-click the canvas, or press Space, to add one where you click.");
    ImGui::SameLine();
    if (ImGui::Button("Save")) doSave();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Save this graph (Ctrl+S)");
    ImGui::SameLine();
    if (ImGui::Button("Recipes")) ImGui::OpenPopup("##vsrecipes");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Drop in a ready-made graph: coins, a chasing enemy, a door, a health pickup, a countdown.");
    if (ImGui::BeginPopup("##vsrecipes")) {
        struct Rec { const char* name; const char* text; };
        static const Rec recs[] = {
            {"Collect coins and score",
             "node 1 OnStart\npos 1 40 40\nentry OnStart 1\nnode 2 ScoreAdd\npos 2 280 40\ndefault 2 0 1\nexec 1 0 2\n"
             "node 3 OnTagEnter coin\npos 3 40 220\nentry OnTagEnter:coin 3\nnode 4 Collect\npos 4 300 220\ndefault 4 0 1\nexec 3 0 4\n"},
            {"Enemy chases and damages",
             "node 1 OnUpdate\npos 1 40 40\nentry OnUpdate 1\nnode 2 FindWithTag Player\npos 2 40 200\nnode 3 Chase\npos 3 300 40\n"
             "exec 1 0 3\ndata 3 0 2 0\nnode 4 OnTagEnter Player\npos 4 40 380\nentry OnTagEnter:Player 4\n"
             "node 5 Damage\npos 5 320 380\ndefault 5 0 10\nexec 4 0 5\n"},
            {"Door with a key",
             "node 1 OnStart\npos 1 40 40\nentry OnStart 1\nnode 2 Const true\npos 2 40 200\nnode 3 SetVar has_key\npos 3 280 40\nexec 1 0 3\ndata 3 0 2 0\n"
             "node 4 OnInteract\npos 4 40 400\nentry OnInteract 4\nnode 5 GetVar has_key\npos 5 40 560\nnode 6 Branch\npos 6 280 400\n"
             "exec 4 0 6\ndata 6 0 5 0\nnode 7 DoorOpen\npos 7 540 400\ndefault 7 0 90\ndefault 7 1 0.35\nexec 6 0 7\n"},
            {"Health pickup",
             "node 1 OnTagEnter Player\npos 1 40 40\nentry OnTagEnter:Player 1\nnode 2 Heal\npos 2 300 40\ndefault 2 0 25\nexec 1 0 2\n"
             "node 3 Destroy\npos 3 540 40\nexec 2 0 3\n"},
            {"Countdown then win",
             "node 1 OnStart\npos 1 40 40\nentry OnStart 1\nnode 2 Countdown 0.4\npos 2 300 40\nexec 1 0 2\nnode 3 Win\npos 3 560 40\nexec 2 0 3\n"},
        };
        for (const Rec& r : recs)
            if (ImGui::MenuItem(r.name)) { st.templateText = r.text; st.pasteTemplate = true; }
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    {
        const std::string nm = g.path.empty() ? std::string("Untitled graph")
                                              : std::filesystem::path(g.path).filename().string();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(nm.c_str());
        if (g.dirty) { ImGui::SameLine(); ImGui::TextColored(ImVec4(0.93f, 0.78f, 0.45f, 1.0f), "Unsaved"); }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", g.path.empty() ? "Not saved yet. Save puts it in Assets." : g.path.c_str());
    }
    // The graph checker: re-run whenever the graph changes (wires that can't
    // work, nodes nothing runs, unset variables, bad key names, missing prefabs).
    {
        std::string now = g.toText();
        if (now != st.checkedText) {
            st.issues = okay::vs::ValidateGraphText(now, ed.projectDir());
            std::vector<okay::vs::VsIssue> exp;
            CompileToOkayScript(g, &exp);
            if (!exp.empty()) {
                st.issues.insert(st.issues.end(), exp.begin(), exp.end());
                st.showIssues = true;
            }
            st.checkedText = std::move(now);
        }
    }
    // Right side of the one toolbar: problems, fit, zoom, the two panels.
    {
        int nErr = 0, nWarn = 0;
        for (const auto& is : st.issues) (is.severity == 0 ? nErr : nWarn)++;
        const float rightW = 520.0f;
        const float avail = ImGui::GetWindowContentRegionMax().x;
        ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 16.0f, avail - rightW));
        {
            char lbl[64];
            if (nErr + nWarn == 0) std::snprintf(lbl, sizeof lbl, "No problems##vsissues");
            else std::snprintf(lbl, sizeof lbl, "%d problem%s##vsissues", nErr + nWarn, nErr + nWarn == 1 ? "" : "s");
            const ImVec4 tc = nErr ? ImVec4(1.0f, 0.55f, 0.48f, 1) : nWarn ? ImVec4(0.95f, 0.78f, 0.42f, 1) : ImVec4(0.62f, 0.82f, 0.68f, 1);
            ImGui::PushStyleColor(ImGuiCol_Text, tc);
            if (ImGui::Button(lbl) && nErr + nWarn > 0) st.showIssues = !st.showIssues;
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(nErr + nWarn == 0 ? "The graph checker found nothing wrong." :
                                  "%d error(s), %d warning(s). Click to list them. Click a row to jump to its node.", nErr, nWarn);
            ImGui::SameLine();
        }
        if (ImGui::Button("Fit")) st.frameAll = true;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Show every node (Home). F frames the selection.");
        ImGui::SameLine();
        if (ImGui::Button("-##vszoomout")) st.zoom = std::clamp(st.zoom / 1.12f, 0.28f, 2.2f);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Zoom out");
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::Text("%d%%", (int)std::lround(st.zoom * 100.0f));
        if (ImGui::IsItemClicked()) st.zoom = 1.0f;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Scroll to zoom. Click for 100%%.");
        ImGui::SameLine();
        if (ImGui::Button("+##vszoomin")) st.zoom = std::clamp(st.zoom * 1.12f, 0.28f, 2.2f);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Zoom in");
        ImGui::SameLine();
        if (ImGui::Button(st.showOutline ? "Outline" : "Outline##off")) st.showOutline = !st.showOutline;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Events, variables and parameters");
        ImGui::SameLine();
        if (ImGui::Button(st.showDetails ? "Details" : "Details##off")) st.showDetails = !st.showDetails;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("What the selected node does");
    }
    ImGui::PopStyleVar(3);
    if (alignReq && st.sel.size() >= 2) {
        std::vector<float> xs, ys, ws, hs;
        std::vector<int> ids;
        for (auto& n : g.nodes) if (st.sel.count(n.id)) {
            ids.push_back(n.id); xs.push_back(n.x); ys.push_back(n.y);
            ws.push_back(210.0f); hs.push_back(130.0f);
        }
        const VsAlign how = alignReq == 1 ? VsAlign::Left : alignReq == 2 ? VsAlign::Right
                          : alignReq == 3 ? VsAlign::Top : VsAlign::Bottom;
        VsAlignNodes(xs, ys, ws, hs, how);
        for (std::size_t i = 0; i < ids.size(); ++i)
            if (VsEditNode* n = g.find(ids[i])) { n->x = xs[i]; n->y = ys[i]; }
        g.dirty = true;
    }
    // ---- Debugger: breakpoints, and the bar while the game is frozen at one --------
    VsDebugView& dv = DebugView();
    auto& dbg = okay::vs::VsDebugger::Get();
    auto toggleBreakpoint = [&](int id) {
        const VsEditNode* n = g.find(id);
        if (!n) return;
        const VsDef* d = FindDef(n->type);
        if (!d || !(d->execIn || !VsEntryName(n->type, n->arg).empty() || n->type == "FunctionStart")) {
            Log("Breakpoints go on steps (nodes the flow runs through). Value nodes run when a step reads them.", 1);
            return;
        }
        if (g.path.empty()) { Log("Save the graph to a file first: breakpoints are kept per graph file.", 1); return; }
        dbg.ToggleBreakpoint(g.path, id);
    };
    auto debugGo = [&](okay::vs::VsDebugger::Step s) {
        if (!dv.stopped) return;
        dbg.RequestStep(s);
        dv.resume = true;
    };
    if (dv.stopped) {
        using S = okay::vs::VsDebugger::Step;
        if (ImGui::IsKeyPressed(ImGuiKey_F5, false)) debugGo(S::None);
        if (ImGui::IsKeyPressed(ImGuiKey_F10, false)) debugGo(S::Over);
        if (ImGui::IsKeyPressed(ImGuiKey_F11, false)) debugGo(io.KeyShift ? S::Out : S::Into);
        if (dv.focusPending && dv.key == g.path) { st.focusNode = dv.node; st.sel = {dv.node}; dv.focusPending = false; }
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.30f, 0.24f, 0.08f, 1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 6.0f));
        ImGui::BeginChild("vsdbgbar", ImVec2(0, ImGui::GetFrameHeightWithSpacing() + 18.0f), true, ImGuiWindowFlags_NoScrollbar);
        const VsDef* sd = FindDef(dv.nodeType);
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(ImVec4(1.0f, 0.82f, 0.30f, 1.0f), "Paused");
        ImGui::SameLine();
        ImGui::Text("at %s (#%d) on '%s', %s", sd ? sd->title : dv.nodeType.c_str(), dv.node, dv.object.c_str(),
                    dv.event.empty() ? "?" : dv.event.c_str());
        if (dv.key != g.path) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Open its graph")) {
                if (g.dirty) Log("Save or undo your changes to this graph first: opening " + dv.key + " would replace them.", 1);
                else { OpenFile(dv.key); dv.focusPending = true; }
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", dv.key.c_str());
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Continue (F5)")) debugGo(S::None);
        ImGui::SameLine();
        if (ImGui::SmallButton("Step (F10)")) debugGo(S::Over);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Run this step and stop at the next one (a Call Function runs whole).");
        ImGui::SameLine();
        if (ImGui::SmallButton("Step Into (F11)")) debugGo(S::Into);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Stop at the very next step, even inside a function it calls.");
        ImGui::SameLine();
        if (ImGui::SmallButton("Step Out (Shift+F11)")) debugGo(S::Out);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Run until this function returns, then stop.");
        ImGui::SameLine();
        // This window keeps Ctrl+P for itself and may cover the toolbar's Stop.
        if (ImGui::SmallButton("Stop Play")) { ed.Stop(); Log("Stop"); }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Stop the game (the paused frame finishes first, without stopping again).");
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
    }
    // Lightweight validation + live status.
    {
        bool hasEvent = false;
        for (const auto& n : g.nodes)
            if (!VsEntryName(n.type, n.arg).empty()) { hasEvent = true; break; }
        st.needsEvent = !g.nodes.empty() && !hasEvent;   // shown on the canvas
    }
    // The running instance of this graph (Play mode): the selected object's if it
    // uses this file, else the first object that does.
    okay::vs::NodeGraph* live = nullptr;
    std::string liveName;
    if (ed.isPlaying() && !g.path.empty()) {
        auto usesThis = [&](GameObject* o) -> VisualScriptComponent* {
            if (!o) return nullptr;
            for (auto* vc : o->GetComponents<VisualScriptComponent>())
                if (vc->Path() == g.path && vc->Graph()) return vc;
            return nullptr;
        };
        VisualScriptComponent* vc = usesThis(ed.selected());
        if (!vc) for (const auto& up : ed.scene().Objects()) if ((vc = usesThis(up.get()))) break;
        if (vc) { live = vc->Graph(); liveName = vc->gameObject ? vc->gameObject->name : std::string(); }
        // Frozen at a breakpoint in this graph: show that object's instance.
        if (dv.stopped && dv.graph && dv.key == g.path) { live = dv.graph; liveName = dv.object; }
    }

    // ---- Keyboard shortcuts (canvas focused, not typing) -------------------------
    if (winFocused && !typing) {
        const bool ctrl = io.KeyCtrl;
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Z, false)) { if (io.KeyShift) doRedo(); else doUndo(); }
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Y, false)) doRedo();
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_A, false)) { st.sel.clear(); for (const auto& n : g.nodes) st.sel.insert(n.id); }
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_C, false) && !st.sel.empty())
            ImGui::SetClipboardText(VsSelectionText(g, st.sel).c_str());
        if (!ctrl && ImGui::IsKeyPressed(ImGuiKey_C, false)) addComment = true;
        if (ImGui::IsKeyPressed(ImGuiKey_F9, false)) for (int id : std::vector<int>(st.sel.begin(), st.sel.end())) toggleBreakpoint(id);
    }

    // ---- Events list: every event chain in the graph; click one to jump to it ----
    if (st.showOutline) {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f, 12.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 6.0f));
        ImGui::BeginChild("vsoutline", ImVec2(268, 0), true);
        ImGui::TextUnformatted("Events");
        ImGui::Separator();
        int shown = 0;
        for (const auto& n : g.nodes) {
            if (VsEntryName(n.type, n.arg).empty()) continue;
            const VsDef* d = FindDef(n.type);
            std::string lbl = d ? d->title : n.type;
            if (!n.arg.empty() && n.type != "OnStart" && n.type != "OnUpdate") lbl += ": " + (IsKeyNode(n.type) ? VsKeyLabel(n.arg) : n.arg);
            // How many steps hang off it (follow the first exec pin).
            int steps = 0;
            for (int cur = VsExecTarget(g, n.id, 0); cur >= 0 && steps < 99; cur = VsExecTarget(g, cur, 0)) ++steps;
            ImGui::PushID(n.id);
            const bool on = st.sel.count(n.id) > 0;
            if (ImGui::Selectable(lbl.c_str(), on, 0, ImVec2(0, 30))) { st.sel = {n.id}; st.selected = n.id; st.focusNode = n.id; }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%d step(s) after it. Click to jump there.", steps);
            ImGui::PopID();
            ++shown;
        }
        if (!shown) ImGui::TextWrapped("No events yet. Use Add node and pick an event such as On Start.");
        // The graph's functions (Function nodes), each with how often it's called.
        bool fnHeader = false;
        for (const auto& n : g.nodes) {
            if (n.type != "FunctionStart") continue;
            if (!fnHeader) { ImGui::Spacing(); ImGui::TextUnformatted("Functions"); ImGui::Separator(); fnHeader = true; }
            const std::string nm = okay::vs::FunctionNameOf(n.arg);
            int calls = 0;
            for (const auto& m : g.nodes) if (m.type == "CallFunction" && okay::vs::FunctionNameOf(m.arg) == nm) ++calls;
            ImGui::PushID(n.id);
            std::string lbl = "\xC6\x92 " + (nm.empty() ? std::string("(unnamed)") : nm);
            if (ImGui::Selectable(lbl.c_str(), st.sel.count(n.id) > 0, 0, ImVec2(0, 30))) { st.sel = {n.id}; st.selected = n.id; st.focusNode = n.id; }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s\nCalled from %d place(s). Click to jump there.", n.arg.c_str(), calls);
            ImGui::PopID();
        }
        // Breakpoints in this graph: the game stops before these steps in Play.
        if (!g.path.empty()) {
            bool bpHeader = false;
            int removeBp = -1;
            for (const auto& bp : dbg.Breakpoints()) {
                if (bp.first != g.path) continue;
                const VsEditNode* bn = g.find(bp.second);
                if (!bpHeader) { ImGui::Spacing(); ImGui::TextUnformatted("Breakpoints"); ImGui::Separator(); bpHeader = true; }
                const VsDef* bd = bn ? FindDef(bn->type) : nullptr;
                std::string lbl = "\xE2\x97\x8F " + std::string(bd ? bd->title : bn ? bn->type.c_str() : "(deleted node)") +
                                  " #" + std::to_string(bp.second);
                ImGui::PushID(bp.second + 100000);
                if (ImGui::Selectable(lbl.c_str(), st.sel.count(bp.second) > 0) && bn) { st.sel = {bp.second}; st.selected = bp.second; st.focusNode = bp.second; }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Click to jump there. Right-click to remove.");
                if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) removeBp = bp.second;
                ImGui::PopID();
            }
            if (removeBp >= 0) dbg.SetBreakpoint(g.path, removeBp, false);
            if (bpHeader && ImGui::SmallButton("Remove all")) {
                std::vector<int> ids;
                for (const auto& bp : dbg.Breakpoints()) if (bp.first == g.path) ids.push_back(bp.second);
                for (int id : ids) dbg.SetBreakpoint(g.path, id, false);
            }
        }
        // Variables and function parameters named in this graph. Click a row to
        // jump to the node that declares or first uses it.
        {
            std::vector<VsVarNode> refs;
            refs.reserve(g.nodes.size());
            for (const auto& n : g.nodes) refs.push_back({n.type, n.arg, n.id});
            const std::vector<VsVarRef> vars = VsCollectVariables(refs);
            const std::vector<VsParamRef> params = VsCollectParameters(refs);
            VsDrawVariableDecls(g);
            ImGui::Spacing();
            ImGui::TextUnformatted("Variables used");
            ImGui::Separator();
            if (vars.empty()) ImGui::TextWrapped("Names you type on Get Var, Set Var, scene and global variable nodes show up here.");
            VsVarScope last = VsVarScope::Object;
            bool anyScope = false;
            for (const auto& v : vars) {
                if (!anyScope || v.scope != last) {
                    ImGui::TextDisabled("%s", VsVarScopeName(v.scope));
                    last = v.scope; anyScope = true;
                }
                ImGui::PushID(v.nodeId + 500000 + (int)v.scope);
                if (ImGui::Selectable(v.name.c_str(), st.sel.count(v.nodeId) > 0, 0, ImVec2(0, 28))) {
                    st.sel = {v.nodeId}; st.selected = v.nodeId; st.focusNode = v.nodeId;
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Click to jump to this variable's node.");
                ImGui::PopID();
            }
            ImGui::Spacing();
            ImGui::TextUnformatted("Parameters");
            ImGui::Separator();
            if (params.empty()) ImGui::TextWrapped("A Function node's signature lists its parameters here.");
            for (const auto& p : params) {
                std::string row = p.function;
                if (!p.name.empty()) row += "  " + p.name + (p.type.empty() ? "" : " : " + p.type);
                else row += "  (no parameters)";
                ImGui::PushID(p.nodeId + 800000);
                if (ImGui::Selectable(row.c_str(), st.sel.count(p.nodeId) > 0, 0, ImVec2(0, 28))) {
                    st.sel = {p.nodeId}; st.selected = p.nodeId; st.focusNode = p.nodeId;
                }
                ImGui::PopID();
            }
        }
        ImGui::EndChild();
        ImGui::PopStyleVar(2);
        ImGui::SameLine();
    }

    // Canvas (leaves room on the right for the OkayScript preview when open).
    const float scriptPanelW = 420.0f;
    const float detailsW = 340.0f;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.102f, 0.110f, 0.135f, 1.0f));
    const float watchW = 340.0f;   // the debugger's watch panel replaces the preview while paused
    const float rightW = dv.stopped ? watchW : st.showScript ? scriptPanelW : st.showDetails ? detailsW : 0.0f;
    ImGui::BeginChild("vscanvas", ImVec2(rightW > 0.0f ? -rightW : 0.0f, 0), true,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoMove);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImVec2 canvasMin = origin, canvasMax = ImVec2(origin.x + ImGui::GetContentRegionAvail().x,
                                                  origin.y + ImGui::GetContentRegionAvail().y);
    // The full-canvas catch-all button handles panning, box select and the
    // right-click palette, but it MUST allow overlap: without this it greedily
    // latches the mouse-press (it's submitted before the nodes), so the pin/header
    // buttons drawn on top of it never activate.
    ImGui::SetNextItemAllowOverlap();
    ImGui::InvisibleButton("vscbg", ImVec2(canvasMax.x - canvasMin.x, canvasMax.y - canvasMin.y),
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
    bool canvasHovered = ImGui::IsItemHovered();
    if (!st.refHint.empty()) {
        ImGui::SetCursorScreenPos(ImVec2(canvasMin.x + 8, canvasMin.y + 8));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.55f, 0.45f, 1.0f));
        ImGui::TextUnformatted(st.refHint.c_str());
        ImGui::PopStyleColor();
        if (ImGui::IsItemClicked()) st.refHint.clear();
    }
    if (ImGui::BeginDragDropTarget()) {
        const float kL = 1.4f;
        const float wx = (io.MousePos.x - canvasMin.x - st.pan.x) / (st.zoom * kL);
        const float wy = (io.MousePos.y - canvasMin.y - st.pan.y) / (st.zoom * kL);
        if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("VS_INSPECTOR"))
            VsPlaceInspectorDrop(g, st, wx, wy, (const char*)p->Data);
        if (const ImGuiPayload* gp = ImGui::AcceptDragDropPayload("GO_PTR")) {
            okay::GameObject* go = gp->Data ? *(okay::GameObject**)gp->Data : nullptr;
            if (go) {
                const int nid = g.add("FindObject", wx, wy);
                if (VsEditNode* nn = g.find(nid))
                    nn->arg = go->Guid().empty() ? go->name : okay::vs::VsGuidRef(go->Guid());
                st.selected = nid;
                st.sel = {nid};
                g.dirty = true;
            }
        }
        ImGui::EndDragDropTarget();
    }
    const bool bgClicked = ImGui::IsItemActivated() && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    const bool canvasWindowHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);

    // ---- Zoom: wheel zooms about the mouse; Home / Frame All fits the graph ----
    float& z = st.zoom;
    // Node positions are stored in the original (compact) layout units; spread
    // them by the same factor the nodes grew, so saved graphs keep their gaps.
    const float kLayout = 1.4f;
    const ImVec2 mouse = io.MousePos;
    if (canvasWindowHovered && io.MouseWheel != 0.0f && !ImGui::IsAnyItemActive() &&
        !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) {
        float wx = (mouse.x - canvasMin.x - st.pan.x) / z, wy = (mouse.y - canvasMin.y - st.pan.y) / z;
        z = std::clamp(z * (io.MouseWheel > 0 ? 1.12f : 1.0f / 1.12f), 0.28f, 2.2f);
        st.pan = ImVec2(mouse.x - canvasMin.x - wx * z, mouse.y - canvasMin.y - wy * z);
    }
    if (winFocused && !typing && ImGui::IsKeyPressed(ImGuiKey_Home, false)) st.frameAll = true;
    if (winFocused && !typing && !io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F, false) && !st.sel.empty()) st.frameSel = true;
    if (st.focusNode >= 0) {   // centre on one node, keeping the zoom readable
        if (const VsEditNode* fn = g.find(st.focusNode)) {
            z = std::max(z, 0.8f);
            st.pan = ImVec2((canvasMax.x - canvasMin.x) * 0.35f - fn->x * kLayout * z,
                            (canvasMax.y - canvasMin.y) * 0.45f - fn->y * kLayout * z);
        }
        st.focusNode = -1;
    }
    if (st.frameAll || st.frameSel) {
        const bool onlySel = st.frameSel && !st.frameAll;
        st.frameAll = st.frameSel = false;
        bool any = false;
        for (const auto& n : g.nodes) if (!onlySel || st.sel.count(n.id)) { any = true; break; }
        if (any) {
            float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
            for (const auto& n : g.nodes) {   // rough node extents in world units
                if (onlySel && !st.sel.count(n.id)) continue;
                x0 = std::min(x0, n.x * kLayout); y0 = std::min(y0, n.y * kLayout);
                x1 = std::max(x1, n.x * kLayout + 340.0f); y1 = std::max(y1, n.y * kLayout + 240.0f);
            }
            float cw = canvasMax.x - canvasMin.x - 60.0f, chh = canvasMax.y - canvasMin.y - 60.0f;
            z = std::clamp(std::min(cw / std::max(1.0f, x1 - x0), chh / std::max(1.0f, y1 - y0)), 0.28f, 1.0f);
            st.pan = ImVec2((canvasMax.x - canvasMin.x) * 0.5f - (x0 + x1) * 0.5f * z,
                            (canvasMax.y - canvasMin.y) * 0.5f - (y0 + y1) * 0.5f * z);
        }
    }
    // Text and widgets follow the zoom.
    ImGui::SetWindowFontScale(z);
    ImFont* font = ImGui::GetFont();
    const float fs = ImGui::GetFontSize();

    // Grid: minor lines + a major line every 4 cells.
    const float grid = 32.0f * z;
    const ImU32 gridMinor = IM_COL32(255, 255, 255, 12), gridMajor = IM_COL32(180, 196, 220, 28);
    {
        int i = (int)std::floor(-st.pan.x / grid);
        for (float x = std::fmod(st.pan.x, grid); x < canvasMax.x - canvasMin.x; x += grid, ++i)
            dl->AddLine(ImVec2(canvasMin.x + x, canvasMin.y), ImVec2(canvasMin.x + x, canvasMax.y), (i % 4 == 0) ? gridMajor : gridMinor);
        int j = (int)std::floor(-st.pan.y / grid);
        for (float y = std::fmod(st.pan.y, grid); y < canvasMax.y - canvasMin.y; y += grid, ++j)
            dl->AddLine(ImVec2(canvasMin.x, canvasMin.y + y), ImVec2(canvasMax.x, canvasMin.y + y), (j % 4 == 0) ? gridMajor : gridMinor);
    }

    // Pan with middle or right drag (anywhere on the canvas); a right CLICK
    // without dragging still opens the add menu.
    if (canvasWindowHovered && (ImGui::IsMouseDragging(ImGuiMouseButton_Middle) || ImGui::IsMouseDragging(ImGuiMouseButton_Right, 6.0f))) {
        st.pan.x += io.MouseDelta.x; st.pan.y += io.MouseDelta.y;
    }

    auto W2S = [&](float wx, float wy) { return ImVec2(canvasMin.x + st.pan.x + wx * kLayout * z, canvasMin.y + st.pan.y + wy * kLayout * z); };
    auto S2W = [&](ImVec2 p) { return ImVec2((p.x - canvasMin.x - st.pan.x) / (z * kLayout), (p.y - canvasMin.y - st.pan.y) / (z * kLayout)); };

    // Compute pin screen positions this frame.
    std::map<long long, ImVec2> pinPos;
    std::map<int, std::pair<ImVec2, ImVec2>> nodeRect;   // screen rect per node (box select, minimap)
    auto pk = [](int node, int kind, int side, int idx) -> long long {
        return ((long long)node << 20) | ((long long)kind << 18) | ((long long)side << 16) | idx;
    };

    // Roomy layout (world units, scaled by the zoom).
    const float NODE_W_BASE = 292.0f * z, HEADER_H = 64.0f * z, ROW_H = 40.0f * z, PIN_R = 9.0f * z;
    const float BOX_W = 136.0f * z;   // inline value box on an unconnected data input
    const float PAD = 16.0f * z, ROUND = 12.0f * z;
    auto inputLinked = [&](int node, int pin) {
        for (const auto& l : g.dataLinks) if (l.consumer == node && l.inPin == pin) return true;
        return false;
    };
    auto text = [&](ImVec2 p, ImU32 c, const char* t) { dl->AddText(font, fs, p, c, t); };

    // Layers: 0 = comment frames, 1 = wires, 2 = nodes.
    dl->ChannelsSplit(3);

    // ---- Comment frames -----------------------------------------------------------
    dl->ChannelsSetCurrent(0);
    static const ImU32 kCommentCols[6] = {IM_COL32(90, 110, 140, 255), IM_COL32(80, 130, 90, 255), IM_COL32(150, 110, 60, 255),
                                          IM_COL32(140, 70, 80, 255), IM_COL32(110, 80, 150, 255), IM_COL32(100, 100, 106, 255)};
    int pendingCommentDelete = -1;
    for (int ci = 0; ci < (int)g.comments.size(); ++ci) {
        VsComment& c = g.comments[ci];
        ImVec2 a = W2S(c.x, c.y), b = W2S(c.x + c.w, c.y + c.h);
        ImU32 col = kCommentCols[((c.color % 6) + 6) % 6];
        ImVec4 cv = ImGui::ColorConvertU32ToFloat4(col);
        dl->AddRectFilled(a, b, ImGui::ColorConvertFloat4ToU32(ImVec4(cv.x, cv.y, cv.z, 0.16f)), ROUND);
        dl->AddRect(a, b, ImGui::ColorConvertFloat4ToU32(ImVec4(cv.x, cv.y, cv.z, 0.75f)), ROUND, 0, 1.5f);
        const float th = HEADER_H * 1.05f;
        dl->AddRectFilled(a, ImVec2(b.x, a.y + th), ImGui::ColorConvertFloat4ToU32(ImVec4(cv.x, cv.y, cv.z, 0.55f)), ROUND, ImDrawFlags_RoundCornersTop);
        dl->AddText(font, fs * 1.08f, ImVec2(a.x + PAD, a.y + (th - fs * 1.08f) * 0.5f), IM_COL32(240, 242, 248, 255), c.text.c_str());
        ImGui::PushID(100000 + ci);
        ImGui::SetCursorScreenPos(a);
        ImGui::SetNextItemAllowOverlap();
        ImGui::InvisibleButton("##chdr", ImVec2(std::max(10.0f, b.x - a.x), th));
        if (ImGui::IsItemActivated()) {
            st.dragComment = ci;
            st.commentNodes.clear();   // carry the nodes that sit inside the frame
            for (const auto& n : g.nodes)
                if (n.x >= c.x && n.y >= c.y && n.x <= c.x + c.w && n.y <= c.y + c.h) st.commentNodes.push_back(n.id);
        }
        if (st.dragComment == ci && ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            float dx = io.MouseDelta.x / (z * kLayout), dy = io.MouseDelta.y / (z * kLayout);
            c.x += dx; c.y += dy;
            for (int id : st.commentNodes) if (VsEditNode* n = g.find(id)) { n->x += dx; n->y += dy; }
            g.dirty = true;
        }
        if (ImGui::IsItemDeactivated() && st.dragComment == ci) st.dragComment = -1;
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            st.renameComment = ci; std::snprintf(st.renameBuf, sizeof st.renameBuf, "%s", c.text.c_str());
            ImGui::OpenPopup("##crename");
        }
        if (ImGui::BeginPopupContextItem("##cctx")) {
            if (ImGui::MenuItem("Rename")) { st.renameComment = ci; std::snprintf(st.renameBuf, sizeof st.renameBuf, "%s", c.text.c_str()); }
            if (ImGui::BeginMenu("Color")) {
                static const char* names[6] = {"Blue", "Green", "Orange", "Red", "Purple", "Grey"};
                for (int k = 0; k < 6; ++k) if (ImGui::MenuItem(names[k], nullptr, c.color == k)) { c.color = k; g.dirty = true; }
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("Delete Comment")) pendingCommentDelete = ci;
            ImGui::EndPopup();
        }
        // Resize grip (bottom-right corner).
        const float gs = 14.0f * z;
        dl->AddTriangleFilled(ImVec2(b.x - gs, b.y - 2), ImVec2(b.x - 2, b.y - gs), ImVec2(b.x - 2, b.y - 2),
                              ImGui::ColorConvertFloat4ToU32(ImVec4(cv.x, cv.y, cv.z, 0.9f)));
        ImGui::SetCursorScreenPos(ImVec2(b.x - gs, b.y - gs));
        ImGui::InvisibleButton("##cgrip", ImVec2(gs, gs));
        if (ImGui::IsItemHovered() || ImGui::IsItemActive()) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNWSE);
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            c.w = std::max(80.0f, c.w + io.MouseDelta.x / (z * kLayout));
            c.h = std::max(50.0f, c.h + io.MouseDelta.y / (z * kLayout));
            g.dirty = true;
        }
        ImGui::PopID();
    }
    if (st.renameComment >= 0 && st.renameComment < (int)g.comments.size()) {
        if (!ImGui::IsPopupOpen("##crename")) ImGui::OpenPopup("##crename");
        if (ImGui::BeginPopup("##crename")) {
            if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
            ImGui::SetNextItemWidth(260);
            if (ImGui::InputText("##ctext", st.renameBuf, sizeof st.renameBuf, ImGuiInputTextFlags_EnterReturnsTrue)) {
                g.comments[st.renameComment].text = st.renameBuf; g.dirty = true;
                st.renameComment = -1; ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("OK")) { g.comments[st.renameComment].text = st.renameBuf; g.dirty = true; st.renameComment = -1; ImGui::CloseCurrentPopup(); }
            ImGui::EndPopup();
        } else st.renameComment = -1;
    }
    if (pendingCommentDelete >= 0) { g.comments.erase(g.comments.begin() + pendingCommentDelete); g.dirty = true; }
    if (addComment) {
        VsComment c;
        if (!st.sel.empty()) {   // frame the selection
            float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
            for (const auto& n : g.nodes) if (st.sel.count(n.id)) {
                x0 = std::min(x0, n.x); y0 = std::min(y0, n.y);
                x1 = std::max(x1, n.x + 230.0f); y1 = std::max(y1, n.y + 140.0f);
            }
            c.x = x0 - 24.0f; c.y = y0 - 42.0f; c.w = (x1 - x0) + 48.0f; c.h = (y1 - y0) + 64.0f;
        } else {
            ImVec2 w = S2W(ImVec2((canvasMin.x + canvasMax.x) * 0.5f, (canvasMin.y + canvasMax.y) * 0.5f));
            c.x = w.x - 110.0f; c.y = w.y - 70.0f; c.w = 220.0f; c.h = 140.0f;
        }
        g.comments.push_back(c); g.dirty = true;
        st.renameComment = (int)g.comments.size() - 1;
        std::snprintf(st.renameBuf, sizeof st.renameBuf, "%s", c.text.c_str());
    }

    // ---- Live debugging: which nodes ran, and their latest values --------------
    const float dtSec = io.DeltaTime;
    for (auto& kv : st.flash) kv.second = std::max(0.0f, kv.second - dtSec * 2.5f);
    auto liveNode = [&](int fileId) -> okay::vs::VsNode* {
        if (!live) return nullptr;
        int rid = live->NodeByFileId(fileId);
        return rid >= 0 ? live->Node(rid) : nullptr;
    };

    // A function node's pins follow its signature. Wires to pins it doesn't
    // have right now (mid-edit of the signature) are kept, not deleted: they come
    // back when the pin does, and the checker reports them meanwhile.
    struct VsBadge { ImVec2 c; ImU32 col; std::string tips; };
    std::vector<VsBadge> badges;   // checker findings, drawn over the nodes
    // The object this graph is attached to, so a node can warn about a missing component.
    GameObject* vsOwner = nullptr;
    if (!g.path.empty()) {
        for (const auto& up : ed.scene().Objects()) {
            if (!up) continue;
            for (auto* vc : up->GetComponents<VisualScriptComponent>())
                if (vc->Path() == g.path) {
                    if (up.get() == ed.selected()) vsOwner = up.get();
                    else if (!vsOwner) vsOwner = up.get();
                }
        }
    }
    // ---- Nodes ----------------------------------------------------------------------
    dl->ChannelsSetCurrent(2);
    int pendingDelete = -1, pendingDup = -1;
    bool startNodeDrag = false;
    // "Connect to..." from a node's right-click menu: add this node (def) wired to one pin, or open the full list for it.
    struct PendingConnect { bool on = false, openAll = false, exec = false, fromOutput = false; int node = -1, pin = -1; const VsDef* def = nullptr; ImVec2 at{0, 0}; } pconn;
    for (auto& n : g.nodes) {
        const VsDef* d = VsDefFor(g, n);
        if (!d) continue;
        int leftRows  = (d->execIn ? 1 : 0) + (int)d->dataIn.size();
        int rightRows = (int)d->execOut.size() + (int)d->dataOut.size();
        int rows = std::max(leftRows, rightRows);
        std::string inlineMsg;
        bool inlineErr = false;
        for (const auto& is : st.issues) if (is.node == n.id) {
            if (is.severity == 0) { inlineMsg = is.message; inlineErr = true; break; }
            if (inlineMsg.empty()) inlineMsg = is.message;
        }
        const bool isSwitch = IsSwitchNode(n.type);
        bool targetWired = false;
        for (int ti = 0; ti < (int)d->dataIn.size(); ++ti)
            if (d->dataIn[ti] && std::strcmp(d->dataIn[ti], "Target") == 0) {
                for (const auto& l : g.dataLinks) if (l.consumer == n.id && l.inPin == ti) targetWired = true;
                auto pit = n.pinDefault.find(ti);
                if (pit != n.pinDefault.end() && !pit->second.empty()) targetWired = true;
            }
        const VsReqGap reqGap = VsRequirementGap(vsOwner, VsNodeDocFor(n.type), targetWired);
        const bool showAdd = reqGap.missing && inlineMsg.empty();
        if (showAdd) inlineMsg = "Needs " + reqGap.addId;
        const float msgH = inlineMsg.empty() ? 0.0f : (showAdd ? ROW_H * 2.4f : ROW_H * 1.35f);
        float bodyH = rows * ROW_H + (d->param ? ROW_H + 10.0f * z : 0) + (isSwitch ? ROW_H : 0.0f) + PAD;
        float h = HEADER_H + bodyH + msgH;
        // Wider when it has data inputs: their value boxes sit between the input
        // labels and the output labels.
        float inLabelW = 0.0f, outLabelW = 0.0f;
        for (const char* lbl : d->dataIn) inLabelW = std::max(inLabelW, ImGui::CalcTextSize(lbl).x);
        for (const char* lbl : d->dataOut) outLabelW = std::max(outLabelW, ImGui::CalcTextSize(lbl).x);
        for (const char* lbl : d->execOut) outLabelW = std::max(outLabelW, ImGui::CalcTextSize(lbl).x);
        const std::string title = VsNodeTitle(n, *d);
        const bool isStep = d->param >= 3 && d->param <= 5;   // Action / Action Steps / Check
        const std::string stepLine = isStep ? VsStepLine(n) : std::string();
        float titleW = ImGui::CalcTextSize(title.c_str()).x + PAD * 2.0f;
        if (isStep) titleW = std::max(titleW, std::min(ImGui::CalcTextSize(stepLine.c_str()).x, 320.0f * z) + PAD * 2.0f);
        const float NODE_W = std::max({NODE_W_BASE, titleW,
            d->dataIn.empty() ? 0.0f : PAD + inLabelW + 8.0f * z + BOX_W + PAD + outLabelW + PAD});
        ImVec2 p0 = W2S(n.x, n.y), p1 = ImVec2(p0.x + NODE_W, p0.y + h);
        nodeRect[n.id] = {p0, p1};
        const bool hovered = ImGui::IsMouseHoveringRect(p0, p1) && canvasWindowHovered;
        const bool isSel = st.sel.count(n.id) > 0 || st.selected == n.id;

        // Live: flash when it ran since the last frame.
        okay::vs::VsNode* ln = liveNode(n.id);
        if (ln) {
            unsigned& seen = st.seenExec[n.id];
            if (ln->execCount != seen) { st.flash[n.id] = 1.0f; seen = ln->execCount; }
        }
        const float fl = (live && st.flash.count(n.id)) ? st.flash[n.id] : 0.0f;

        // Shadow, body, header.
        dl->AddRectFilled(ImVec2(p0.x + 3 * z, p0.y + 5 * z), ImVec2(p1.x + 3 * z, p1.y + 5 * z), IM_COL32(0, 0, 0, 90), ROUND);
        if (fl > 0.0f)
            dl->AddRect(ImVec2(p0.x - 4 * z, p0.y - 4 * z), ImVec2(p1.x + 4 * z, p1.y + 4 * z),
                        IM_COL32(255, 200, 80, (int)(220 * fl)), ROUND + 4 * z, 0, 3.0f * z);
        // Checker findings for this node: red (error) / amber (warning) dot on the
        // header, the messages on hover.
        {
            int worst = 2; std::string tips;
            for (const auto& is : st.issues)
                if (is.node == n.id) { worst = std::min(worst, is.severity); tips += (tips.empty() ? "" : "\n") + std::string(is.severity == 0 ? "Error: " : "Warning: ") + is.message; }
            if (worst < 2) {
                const ImVec2 bc(p1.x - 12.0f * z, p0.y + HEADER_H * 0.5f);
                const ImU32 col = worst == 0 ? IM_COL32(255, 85, 75, 255) : IM_COL32(240, 185, 70, 255);
                dl->AddRect(ImVec2(p0.x - 2 * z, p0.y - 2 * z), ImVec2(p1.x + 2 * z, p1.y + 2 * z), col, ROUND + 2 * z, 0, 2.0f * z);
                badges.push_back({bc, col, tips});
            }
        }
        const bool pausedHere = dv.stopped && dv.key == g.path && dv.node == n.id;
        if (pausedHere)
            dl->AddRect(ImVec2(p0.x - 7 * z, p0.y - 7 * z), ImVec2(p1.x + 7 * z, p1.y + 7 * z),
                        IM_COL32(255, 205, 70, 255), ROUND + 7 * z, 0, 4.0f * z);
        const bool errNode = live && live->LastErrorNode() == n.id && !live->LastError().empty();
        if (errNode)
            dl->AddRect(ImVec2(p0.x - 5 * z, p0.y - 5 * z), ImVec2(p1.x + 5 * z, p1.y + 5 * z),
                        IM_COL32(255, 90, 80, 255), ROUND + 5 * z, 0, 3.0f * z);
        dl->AddRectFilled(p0, p1, IM_COL32(30, 33, 42, 248), ROUND);
        ImU32 hc = CatColor(d->cat);
        dl->AddRectFilled(p0, ImVec2(p1.x, p0.y + HEADER_H), hc, ROUND, ImDrawFlags_RoundCornersTop);
        dl->AddLine(ImVec2(p0.x, p0.y + HEADER_H), ImVec2(p1.x, p0.y + HEADER_H), IM_COL32(0, 0, 0, 120), 1.0f);
        if (isSel) {
            dl->AddRect(ImVec2(p0.x - 2 * z, p0.y - 2 * z), ImVec2(p1.x + 2 * z, p1.y + 2 * z), IM_COL32(120, 170, 255, 255), ROUND + 2 * z, 0, 2.5f);
        } else {
            dl->AddRect(p0, p1, hovered ? IM_COL32(150, 154, 166, 200) : IM_COL32(0, 0, 0, 170), ROUND, 0, hovered ? 1.5f : 1.0f);
        }
        text(ImVec2(p0.x + PAD, p0.y + 6.0f * z), IM_COL32(244, 246, 250, 255), title.c_str());
        {
            const char* help = VsNodeHelp(d->type);
            std::string hint = help ? help : (d->paramHint ? std::string("Needs: ") + d->paramHint : std::string());
            const float maxW = NODE_W - PAD * 2.0f - 78.0f * z;
            while (hint.size() > 8 && ImGui::CalcTextSize(hint.c_str()).x > maxW) hint.pop_back();
            if (help && hint.size() < std::strlen(help)) hint += "...";
            if (!hint.empty())
                text(ImVec2(p0.x + PAD, p0.y + 8.0f * z + fs), IM_COL32(230, 236, 244, 210), hint.c_str());
            bool hasErr = false;
            std::string miss;
            for (const auto& is : st.issues) if (is.node == n.id && is.severity == 0) hasErr = true;
            const VsReady ready = VsNodeReadiness(n.type, !n.arg.empty(), [&](int pin) {
                if (inputLinked(n.id, pin)) return true;
                auto it = n.pinDefault.find(pin);
                return it != n.pinDefault.end() && !it->second.empty();
            }, hasErr, &miss);
            const char* badge = VsReadyName(ready);
            const ImVec2 bts = ImGui::CalcTextSize(badge);
            const ImVec2 br(p1.x - PAD - bts.x - 10.0f * z, p0.y + 5.0f * z);
            const ImU32 bg = ready == VsReady::Error ? IM_COL32(150, 48, 44, 255)
                           : ready == VsReady::Missing ? IM_COL32(140, 96, 32, 255)
                           : IM_COL32(36, 110, 72, 255);
            dl->AddRectFilled(br, ImVec2(br.x + bts.x + 10.0f * z, br.y + bts.y + 4.0f * z), bg, 4.0f * z);
            text(ImVec2(br.x + 5.0f * z, br.y + 2.0f * z), IM_COL32(255, 248, 240, 255), badge);
            if (ready == VsReady::Missing && !miss.empty())
                text(ImVec2(p0.x + PAD, p0.y + HEADER_H - fs - 2.0f * z), IM_COL32(255, 196, 140, 255), miss.c_str());
        }
        if (!g.path.empty() && dbg.HasBreakpoint(g.path, n.id)) {   // breakpoint: a red dot left of the header
            const ImVec2 bc(p0.x - 11.0f * z, p0.y + HEADER_H * 0.5f);
            dl->AddCircleFilled(bc, 6.5f * z, IM_COL32(225, 60, 55, 255));
            dl->AddCircle(bc, 6.5f * z, IM_COL32(20, 20, 20, 220), 0, 1.5f * z);
        }
        if (pausedHere) {   // the game is frozen right before this step
            const float ay = p0.y + HEADER_H * 0.5f, ax = p0.x - 24.0f * z;
            dl->AddTriangleFilled(ImVec2(ax - 10 * z, ay - 8 * z), ImVec2(ax - 10 * z, ay + 8 * z), ImVec2(ax + 2 * z, ay),
                                  IM_COL32(255, 205, 70, 255));
        }

        // Node drag handle: an invisible button over the header.
        ImGui::SetCursorScreenPos(p0);
        ImGui::PushID(n.id);
        ImGui::InvisibleButton("##hdr", ImVec2(NODE_W, HEADER_H));
        if (ImGui::IsItemActivated()) {
            if (io.KeyCtrl || io.KeyShift) {   // toggle in the selection
                if (st.sel.count(n.id)) st.sel.erase(n.id); else st.sel.insert(n.id);
            } else if (!st.sel.count(n.id)) { st.sel.clear(); st.sel.insert(n.id); }
            st.selected = n.id;
        }
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            startNodeDrag = true;   // move every selected node together (after the loop)
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal) && !ImGui::IsItemActive())
            VsNodeTooltip(title.c_str(), VsNodeDocFor(n.type), d);
        if (ImGui::BeginPopupContextItem("##nctx")) {
            if (!st.sel.count(n.id)) { st.sel.clear(); st.sel.insert(n.id); st.selected = n.id; }
            // Applied after the loop: the node list mustn't change while it's iterated.
            if (ImGui::MenuItem("Duplicate", "Ctrl+D")) pendingDup = n.id;
            if (ImGui::MenuItem("Copy", "Ctrl+C")) ImGui::SetClipboardText(VsSelectionText(g, st.sel).c_str());
            if (ImGui::MenuItem("Frame in Comment", "C")) addComment = true;
            const bool hasBp = !g.path.empty() && dbg.HasBreakpoint(g.path, n.id);
            if (ImGui::MenuItem(hasBp ? "Remove Breakpoint" : "Add Breakpoint", "F9")) toggleBreakpoint(n.id);
            if (ImGui::MenuItem("Break All Wires")) {
                for (int k = 0; k < 2; ++k) for (int sd = 0; sd < 2; ++sd) for (int pi = 0; pi < 8; ++pi) g.unlinkPin(n.id, k, sd, pi);
            }
            // What can plug into this node: one entry per pin, listing the nodes that fit it (the same rule as a wire
            // dropped in empty space). Pick one to add it already wired, or open the full list for that pin.
            if (ImGui::BeginMenu("Connect to...")) {
                const ImVec2 outAt = S2W(ImVec2(p0.x + NODE_W + 50.0f * z, p0.y));
                ImVec2 inAt = S2W(ImVec2(p0.x - NODE_W_BASE - 50.0f * z, p0.y));
                inAt.x += 170.0f;   // a new node feeding an input is shifted left by the same amount when it is added
                auto pinMenu = [&](const std::string& label, bool exec, bool fromOutput, int pin, okay::vs::PinType pt) {
                    if (!ImGui::BeginMenu(label.c_str())) return;
                    const std::vector<const VsDef*>& fit = VsNodesFittingPin(exec, fromOutput, pt);
                    if (fit.empty()) ImGui::TextDisabled("Nothing fits this pin.");
                    const int shown = std::min((int)fit.size(), 16);
                    for (int i = 0; i < shown; ++i) {
                        if (ImGui::MenuItem(fit[(std::size_t)i]->title && fit[(std::size_t)i]->title[0] ? fit[(std::size_t)i]->title : fit[(std::size_t)i]->type)) {
                            pconn.on = true; pconn.def = fit[(std::size_t)i];
                            pconn.exec = exec; pconn.fromOutput = fromOutput; pconn.node = n.id; pconn.pin = pin;
                            pconn.at = fromOutput ? outAt : inAt;
                        }
                    }
                    if (!fit.empty()) {
                        ImGui::Separator();
                        const std::string all = "Search all " + std::to_string(fit.size()) + " that fit...";
                        if (ImGui::MenuItem(all.c_str())) {
                            pconn.on = true; pconn.openAll = true; pconn.def = nullptr;
                            pconn.exec = exec; pconn.fromOutput = fromOutput; pconn.node = n.id; pconn.pin = pin;
                            pconn.at = fromOutput ? outAt : inAt;
                        }
                    }
                    ImGui::EndMenu();
                };
                ImGui::TextDisabled("Pick a pin to see what fits it");
                bool any = false;
                if (d->execIn) { pinMenu("Flow in  <-  what runs this node", true, false, 0, okay::vs::PinType::Any); any = true; }
                for (int j = 0; j < (int)d->execOut.size(); ++j) {
                    const char* lbl = d->execOut[(std::size_t)j];
                    pinMenu(std::string("Flow out  ->  ") + (lbl && *lbl ? lbl : "Next"), true, true, j, okay::vs::PinType::Any); any = true;
                }
                for (int i = 0; i < (int)d->dataIn.size(); ++i) {
                    const okay::vs::PinType pt = VsInPinType(g, n.id, i);
                    pinMenu(std::string("Value in  <-  ") + d->dataIn[(std::size_t)i] + " (" + okay::vs::PinTypeName(pt) + ")", false, false, i, pt); any = true;
                }
                for (int o = 0; o < (int)d->dataOut.size(); ++o) {
                    const okay::vs::PinType pt = VsOutPinType(g, n.id, o);
                    pinMenu(std::string("Value out  ->  ") + d->dataOut[(std::size_t)o] + " (" + okay::vs::PinTypeName(pt) + ")", false, true, o, pt); any = true;
                }
                if (!any) ImGui::TextDisabled("This node has no pins.");
                ImGui::EndMenu();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Delete", "Del")) pendingDelete = n.id;
            ImGui::EndPopup();
        }

        const ImU32 labelCol = IM_COL32(205, 209, 216, 255);
        const ImU32 execCol = IM_COL32(235, 236, 240, 255), dataCol = IM_COL32(120, 200, 140, 255);
        float ly = p0.y + HEADER_H + PAD * 0.5f;
        auto execTri = [&](ImVec2 pp) {
            float r = PIN_R * 1.15f;
            dl->AddTriangleFilled(ImVec2(pp.x - r * 0.8f, pp.y - r), ImVec2(pp.x - r * 0.8f, pp.y + r), ImVec2(pp.x + r, pp.y), execCol);
        };
        auto dataDot = [&](ImVec2 pp, bool linked, ImU32 col = 0) {
            if (!col) col = dataCol;
            dl->AddCircleFilled(pp, PIN_R, linked ? col : IM_COL32(38, 39, 44, 255), 16);
            dl->AddCircle(pp, PIN_R, col, 16, 2.0f * z);
        };
        // Left pins (exec-in then data-in).
        int li = 0;
        if (d->execIn) {
            ImVec2 pp(p0.x, ly + li * ROW_H + ROW_H * 0.5f);
            execTri(pp);
            pinPos[pk(n.id, 0, 0, 0)] = pp;
            ++li;
        }
        for (int i = 0; i < (int)d->dataIn.size(); ++i, ++li) {
            ImVec2 pp(p0.x, ly + li * ROW_H + ROW_H * 0.5f);
            const bool linked = inputLinked(n.id, i);
            const okay::vs::PinType pt = VsInPinType(g, n.id, i);
            bool badPin = false;
            for (const auto& l : g.dataLinks)
                if (l.consumer == n.id && l.inPin == i && !VsWireOk(g, l.consumer, l.inPin, l.producer, l.outPin)) badPin = true;
            for (const auto& is : st.issues)
                if (is.node == n.id && is.pin == i && is.pinDir == 0 && is.severity == 0) badPin = true;
            dataDot(pp, linked || badPin, badPin ? IM_COL32(255, 72, 64, 255) : VsPinColor(pt));
            const bool req = VsRequiredWhat(n.type, i) && !linked && n.pinDefault.count(i) == 0;
            if (req) dl->AddCircle(pp, PIN_R + 3.0f * z, IM_COL32(255, 80, 70, 255), 16, 2.0f * z);
            text(ImVec2(p0.x + PAD, pp.y - fs * 0.5f), labelCol, d->dataIn[i]);
            pinPos[pk(n.id, 1, 0, i)] = pp;
            // Nothing wired in: type the value right here (text, number, true/false).
            if (!linked) {
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(5 * z, 3 * z));
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4 * z);
                const float boxH = fs + 6 * z;
                ImGui::SetCursorScreenPos(ImVec2(p0.x + PAD + inLabelW + 8 * z, pp.y - boxH * 0.5f));
                const okay::vs::VsRefKind rk = VsPinKind(n.type, i, pt, d->dataIn[i]);
                std::string& dv = n.pinDefault[i];
                if (rk != okay::vs::VsRefKind::None) {
                    const bool selfDefault = okay::vs::TargetPinFor(n.type) == i;
                    char bid[16]; std::snprintf(bid, sizeof(bid), "pd%d", i);
                    if (VsDrawRefSlot(ed, st, dv, rk, selfDefault, BOX_W, bid)) g.dirty = true;
                    if (dv.empty()) n.pinDefault.erase(i);
                } else {
                    ImGui::SetNextItemWidth(BOX_W);
                    char vb[128]; std::snprintf(vb, sizeof(vb), "%s", dv.c_str());
                    char bid[16]; std::snprintf(bid, sizeof(bid), "##pd%d", i);
                    if (ImGui::InputTextWithHint(bid, "value", vb, sizeof(vb))) { dv = vb; g.dirty = true; }
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("%s: type a value (text, a number, true/false), or wire a node in", d->dataIn[i]);
                    if (dv.empty()) n.pinDefault.erase(i);
                }
                ImGui::PopStyleVar(2);
            }
        }
        // Right pins (exec-out then data-out).
        int ri = 0;
        for (int i = 0; i < (int)d->execOut.size(); ++i, ++ri) {
            ImVec2 pp(p1.x, ly + ri * ROW_H + ROW_H * 0.5f);
            execTri(pp);
            if (d->execOut[i][0]) {
                ImVec2 ts = ImGui::CalcTextSize(d->execOut[i]);
                text(ImVec2(p1.x - PAD - ts.x, pp.y - fs * 0.5f), labelCol, d->execOut[i]);
            }
            pinPos[pk(n.id, 0, 1, i)] = pp;
        }
        for (int i = 0; i < (int)d->dataOut.size(); ++i, ++ri) {
            ImVec2 pp(p1.x, ly + ri * ROW_H + ROW_H * 0.5f);
            bool used = false, badPin = false;
            for (const auto& l : g.dataLinks) if (l.producer == n.id && l.outPin == i) {
                used = true;
                if (!VsWireOk(g, l.consumer, l.inPin, l.producer, l.outPin)) badPin = true;
            }
            for (const auto& is : st.issues)
                if (is.node == n.id && is.pin == i && is.pinDir == 1 && is.severity == 0) badPin = true;
            const okay::vs::PinType pt = VsOutPinType(g, n.id, i);
            dataDot(pp, used || badPin, badPin ? IM_COL32(255, 72, 64, 255) : VsPinColor(pt));
            ImVec2 ts = ImGui::CalcTextSize(d->dataOut[i]);
            text(ImVec2(p1.x - PAD - ts.x, pp.y - fs * 0.5f), labelCol, d->dataOut[i]);
            pinPos[pk(n.id, 1, 1, i)] = pp;
            // Live value bubble beside the output pin.
            if (st.liveValues && ln && i < (int)ln->lastOut.size()) {
                std::string v = ln->lastOut[i].AsString();
                if (ln->lastOut[i].IsVec3()) { Vec3 q3 = ln->lastOut[i].AsVec3(); char b3[64]; std::snprintf(b3, sizeof b3, "(%.2f, %.2f, %.2f)", q3.x, q3.y, q3.z); v = b3; }
                else if (ln->lastOut[i].IsObject()) {   // an object: its name, or say it's gone
                    if (!ln->lastOut[i].AsObjectId()) v = "(no object)";
                    else if (v.empty()) v = "(destroyed)";
                }
                else if (!ln->lastOut[i].IsString()) {
                    char bf[32]; float f = ln->lastOut[i].AsFloat();
                    if (v == "true" || v == "false") {} else { std::snprintf(bf, sizeof bf, "%.3g", f); v = bf; }
                }
                if (v.size() > 22) v = v.substr(0, 21) + "\xE2\x80\xA6";
                ImVec2 vs2 = ImGui::CalcTextSize(v.c_str());
                ImVec2 bp(pp.x + PIN_R + 6 * z, pp.y - vs2.y * 0.5f - 2 * z);
                dl->AddRectFilled(bp, ImVec2(bp.x + vs2.x + 10 * z, bp.y + vs2.y + 4 * z), IM_COL32(20, 60, 34, 230), 4 * z);
                text(ImVec2(bp.x + 5 * z, bp.y + 2 * z), IM_COL32(150, 240, 170, 255), v.c_str());
            }
        }

        // Param editor (inline) in the body.
        if (d->param) {
            float py = p0.y + HEADER_H + PAD * 0.5f + std::max(leftRows, rightRows) * ROW_H + 4 * z;
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6 * z, 4 * z));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4 * z);
            ImGui::SetCursorScreenPos(ImVec2(p0.x + PAD, py));
            ImGui::SetNextItemWidth(NODE_W - PAD * 2);
            char buf[96]; std::snprintf(buf, sizeof(buf), "%s", n.arg.c_str());
            if (isStep) {
                // A step node: its arguments as a sentence; click to edit with the
                // same friendly fields the Actions component had (no typing code).
                const bool cond = d->param == 5;
                std::string shown = stepLine;
                const float maxW = NODE_W - PAD * 2 - 12 * z;
                while (shown.size() > 4 && ImGui::CalcTextSize(shown.c_str()).x > maxW) shown = shown.substr(0, shown.size() - 4) + "..";
                if (ImGui::Button((shown + "##stepbtn").c_str(), ImVec2(NODE_W - PAD * 2, 0))) ImGui::OpenPopup("##stepedit");
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Click to edit");
                ImGui::SetNextWindowSizeConstraints(ImVec2(440, 0), ImVec2(720, 520));
                if (ImGui::BeginPopup("##stepedit")) {
                    ImGui::SetWindowFontScale(1.0f);
                    if (d->param == 4) {   // a list of steps
                        auto list = okay::ActionList::DecodeItems(n.arg);
                        ImGui::TextColored(ImVec4(0.6f, 0.85f, 1.0f, 1.0f), "Steps (run top to bottom; Wait pauses)");
                        ImGui::Separator();
                        bool ch = false; int act = 0, actAt = -1;
                        for (std::size_t i = 0; i < list.size(); ++i) {
                            ImGui::TextDisabled("%d", (int)i + 1); ImGui::SameLine(26);
                            int a = StepEditor ? StepEditor(list[i], false, (int)i, ch) : 0;
                            if (a) { act = a; actAt = (int)i; }
                        }
                        if (act == 1 && actAt >= 0) { list.erase(list.begin() + actAt); ch = true; }
                        if (act == 2 && actAt > 0) { std::swap(list[actAt], list[actAt - 1]); ch = true; }
                        if (act == 3 && actAt >= 0 && actAt + 1 < (int)list.size()) { std::swap(list[actAt], list[actAt + 1]); ch = true; }
                        if (ImGui::Button("+ Add step")) { list.push_back({"log", {"Hello"}}); ch = true; }
                        if (ch) { n.arg = okay::ActionList::EncodeItems(list); g.dirty = true; }
                    } else {
                        okay::ActionList::Item it = okay::ActionList::DecodeItem(n.arg);
                        if (it.op.empty()) it.op = cond ? "always" : "log";
                        ImGui::TextColored(ImVec4(0.6f, 0.85f, 1.0f, 1.0f), cond ? "Check" : "Action");
                        ImGui::Separator();
                        bool ch = false;
                        if (StepEditor) StepEditor(it, cond, 0, ch);
                        if (ch) { n.arg = okay::ActionList::EncodeItem(it); g.dirty = true; }
                    }
                    ImGui::SetWindowFontScale(1.0f);
                    ImGui::EndPopup();
                }
            } else if (d->param == 6) {   // pick one of this graph's functions
                const std::string cur = okay::vs::FunctionNameOf(n.arg);
                if (ImGui::BeginCombo("##fn", cur.empty() ? "(pick a function)" : cur.c_str())) {
                    bool any = false;
                    for (const auto& m : g.nodes) {
                        if (m.type != "FunctionStart") continue;
                        const std::string nm = okay::vs::FunctionNameOf(m.arg);
                        if (nm.empty()) continue;
                        any = true;
                        if (ImGui::Selectable(nm.c_str(), nm == cur)) { n.arg = nm; g.dirty = true; }
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", m.arg.c_str());
                    }
                    if (!any) ImGui::TextDisabled("No functions yet: add a Function node first");
                    ImGui::EndCombo();
                }
            } else if (n.type == "FunctionStart") {   // the signature (may be long)
                char sbuf[256]; std::snprintf(sbuf, sizeof(sbuf), "%s", n.arg.c_str());
                if (ImGui::InputTextWithHint("##sig", d->paramHint, sbuf, sizeof(sbuf))) {
                    // Renaming the function renames the Calls and Returns that use it.
                    const std::string was = okay::vs::FunctionNameOf(n.arg), now = okay::vs::FunctionNameOf(sbuf);
                    if (!was.empty() && !now.empty() && was != now)
                        for (auto& m : g.nodes)
                            if ((m.type == "CallFunction" || m.type == "Return") && okay::vs::FunctionNameOf(m.arg) == was) m.arg = now;
                    n.arg = sbuf; g.dirty = true;
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Name(param: Type, ...) -> result: Type\nTypes: Number, Bool, Text, Vector3, Object, List, Any");
            } else if (d->param == 2) {   // compare op combo
                const char* ops[] = {">", "<", ">=", "<=", "==", "!="};
                if (ImGui::BeginCombo("##op", n.arg.c_str())) {
                    for (const char* o : ops) if (ImGui::Selectable(o, n.arg == o)) { n.arg = o; g.dirty = true; }
                    ImGui::EndCombo();
                }
            } else if (IsKeyNode(n.type)) {
                if (KeyPicker(n.arg, NODE_W - PAD * 2)) g.dirty = true;
            } else if (n.type == "PlayAction") {
                const auto& names = okay::HumanoidActions::ActionNames();
                const char* preview = n.arg.empty() ? "(action)" : n.arg.c_str();
                if (ImGui::BeginCombo("##act", preview)) {
                    for (const auto& nm : names)
                        if (ImGui::Selectable(nm.c_str(), n.arg == nm)) { n.arg = nm; g.dirty = true; }
                    if (!n.arg.empty()) {
                        bool known = false;
                        for (const auto& nm : names) if (nm == n.arg) known = true;
                        if (!known && ImGui::Selectable(n.arg.c_str(), true)) { /* keep a saved name */ }
                    }
                    ImGui::EndCombo();
                }
            } else if (n.type == "Die") {
                const char* dirs[] = {"forward", "back", "left", "right"};
                int cur = 0;
                for (int i = 0; i < 4; ++i) if (n.arg == dirs[i]) cur = i;
                if (ImGui::Combo("##diedir", &cur, dirs, 4)) { n.arg = dirs[cur]; g.dirty = true; }
            } else if (n.type == "MouseButton" || n.type == "MouseButtonDown") {
                const char* btns[] = {"Left button", "Right button", "Middle button"};
                int b = std::atoi(n.arg.c_str());
                if (n.arg == "right") b = 1; else if (n.arg == "middle") b = 2;
                if (b < 0 || b > 2) b = 0;
                if (ImGui::Combo("##mbtn", &b, btns, 3)) { n.arg = std::to_string(b); g.dirty = true; }
            } else if (isSwitch) {
                char sbuf[512]; std::snprintf(sbuf, sizeof(sbuf), "%s", n.arg.c_str());
                if (ImGui::InputTextWithHint("##sw", d->paramHint ? d->paramHint : "", sbuf, sizeof(sbuf))) {
                    const int oldN = (int)VsEditorSwitchCases(n.arg).size();
                    const int newN = (int)VsEditorSwitchCases(sbuf).size();
                    VsMoveSwitchDefault(g, n.id, oldN, newN);
                    n.arg = sbuf; g.dirty = true;
                }
                // InputText returns the cursor to the window flow. Put + case
                // back under the field, inside this node.
                const ImVec2 swField = ImGui::GetItemRectMax();
                ImGui::SetCursorScreenPos(ImVec2(p0.x + PAD, swField.y + 6.0f * z));
                if (ImGui::SmallButton("+ case")) {
                    auto cases = VsEditorSwitchCases(n.arg);
                    if ((int)cases.size() < 63) {
                        const int oldN = (int)cases.size();
                        if (n.type == "SwitchCases") {
                            int next = oldN;
                            if (!cases.empty()) {
                                char* end = nullptr;
                                const long v = std::strtol(cases.back().c_str(), &end, 10);
                                if (end && *end == '\0') next = (int)v + 1;
                            }
                            cases.push_back(std::to_string(next));
                        } else {
                            cases.push_back("case" + std::to_string(oldN + 1));
                        }
                        std::string joined;
                        for (const auto& c : cases) { if (!joined.empty()) joined += ", "; joined += c; }
                        VsMoveSwitchDefault(g, n.id, oldN, oldN + 1);
                        n.arg = joined; g.dirty = true;
                    }
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Add another case. Default stays the last pin.");
            } else if (VsChoiceKind ck = VsArgChoice(n.type); ck != VsChoiceKind::None) {
                if (VsDrawChoice(ed, g, n.arg, ck, NODE_W - PAD * 2)) g.dirty = true;
            } else if (okay::vs::VsRefKind ak = VsArgKind(n.type); ak != okay::vs::VsRefKind::None) {
                if (VsDrawRefSlot(ed, st, n.arg, ak, VsArgDefaultsSelf(n.type), NODE_W - PAD * 2, "argref"))
                    g.dirty = true;
            } else {
                if (ImGui::InputTextWithHint("##arg", d->paramHint ? d->paramHint : "", buf, sizeof(buf)))
                    { n.arg = buf; g.dirty = true; }
            }
            ImGui::PopStyleVar(2);
        }

        // Start a wire from a pin: an invisible button over each pin (generous hit
        // area). Alt+click a pin breaks its wires instead.
        auto pinButton = [&](long long key, int kind, int side, int idx) {
            auto it = pinPos.find(key); if (it == pinPos.end()) return;
            ImVec2 pp = it->second;
            const float hit = std::max(26.0f, 32.0f * z);
            ImGui::SetCursorScreenPos(ImVec2(pp.x - hit * 0.5f, pp.y - hit * 0.5f));
            char id[24]; std::snprintf(id, sizeof(id), "p%d%d%d", kind, side, idx);
            ImGui::InvisibleButton(id, ImVec2(hit, hit));
            if (st.dragging && n.id != st.dragNode) {
                const bool kindOk = kind == (st.dragExec ? 0 : 1);
                const bool sideOk = side == (st.dragFromOutput ? 0 : 1);
                bool fit = false;
                std::string why;
                if (kindOk && sideOk) {
                    if (st.dragExec) {
                        fit = !(side == 0 && d && !d->execIn);
                        if (!fit) why = "Events start a chain. Wire this into a step, not into another event.";
                    } else {
                        const int cons = st.dragFromOutput ? n.id : st.dragNode;
                        const int inP = st.dragFromOutput ? idx : st.dragPin;
                        const int prod = st.dragFromOutput ? st.dragNode : n.id;
                        const int outP = st.dragFromOutput ? st.dragPin : idx;
                        fit = VsWireOk(g, cons, inP, prod, outP, &why);
                        if (fit) {
                            std::vector<std::pair<int, int>> edges;
                            for (const auto& l : g.dataLinks) edges.push_back({l.producer, l.consumer});
                            if (VsDataWouldCycle(edges, prod, cons)) { fit = false; why = "That would loop: this value already depends on the node you are wiring into."; }
                        }
                    }
                } else if (!kindOk) {
                    why = st.dragExec ? "This wire carries flow (the triangle), not a value. Drop it on a white triangle."
                                      : "This wire carries a value. Drop it on a coloured circle, not a flow triangle.";
                }
                if (fit) dl->AddCircle(pp, PIN_R + 5.0f * z, IM_COL32(140, 230, 160, 240), 18, 2.5f * z);
                else dl->AddCircleFilled(pp, PIN_R + 2.0f * z, IM_COL32(10, 10, 14, 160), 16);
                if (!fit && !why.empty() && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", why.c_str());
            }
            if (kind == 1 && side == 0 && ImGui::BeginDragDropTarget()) {
                const char* label = (idx >= 0 && idx < (int)d->dataIn.size()) ? d->dataIn[(std::size_t)idx] : "";
                const okay::vs::VsRefKind rk = VsPinKind(n.type, idx, VsInPinType(g, n.id, idx), label);
                if (rk != okay::vs::VsRefKind::None) {
                    std::string slot = n.pinDefault.count(idx) ? n.pinDefault[idx] : std::string();
                    if (VsApplyDrop(ed, st, rk, slot)) {
                        g.unlinkPin(n.id, 1, 0, idx);
                        n.pinDefault[idx] = slot;
                        g.dirty = true;
                    }
                }
                ImGui::EndDragDropTarget();
            }
            if (ImGui::IsItemActivated()) {
                if (io.KeyAlt) g.unlinkPin(n.id, kind, side, idx);
                else {
                    st.dragging = true; st.dragExec = (kind == 0);
                    st.dragFromOutput = (side == 1); st.dragNode = n.id; st.dragPin = idx;
                }
            }
            if (ImGui::IsItemHovered()) {
                dl->AddCircle(pp, PIN_R + 3 * z, IM_COL32(255, 255, 255, 210), 18, 2.0f);
                if (!st.dragging) {
                    const bool input = side == 0, exec = kind == 0;
                    std::string pname;
                    if (!exec) pname = input ? (idx < (int)d->dataIn.size() ? d->dataIn[idx] : "") : (idx < (int)d->dataOut.size() ? d->dataOut[idx] : "");
                    else if (!input && idx < (int)d->execOut.size()) pname = d->execOut[idx];
                    const VsNodeDoc* nd = VsNodeDocFor(n.type);
                    const VsPinDoc* pd = nd ? VsPinDocFor(*nd, input, exec, pname) : nullptr;
                    const char* tn = exec ? "Flow" : okay::vs::PinTypeName(input ? VsInPinType(g, n.id, idx) : VsOutPinType(g, n.id, idx));
                    VsPinTooltip(pd, pname.c_str(), tn,
                                 exec ? okay::vs::PinType::Any : (input ? VsInPinType(g, n.id, idx) : VsOutPinType(g, n.id, idx)), !input, exec,
                                 (input && !exec && !inputLinked(n.id, idx) && n.pinDefault.count(idx) == 0) ? VsRequiredWhat(n.type, idx) : nullptr);
                }
            }
        };
        if (d->execIn) pinButton(pk(n.id, 0, 0, 0), 0, 0, 0);
        for (int i = 0; i < (int)d->dataIn.size(); ++i)  pinButton(pk(n.id, 1, 0, i), 1, 0, i);
        for (int i = 0; i < (int)d->execOut.size(); ++i) pinButton(pk(n.id, 0, 1, i), 0, 1, i);
        for (int i = 0; i < (int)d->dataOut.size(); ++i) pinButton(pk(n.id, 1, 1, i), 1, 1, i);

        if (!inlineMsg.empty()) {
            const float my = p1.y - msgH;
            const ImU32 band = inlineErr ? IM_COL32(120, 48, 46, 230) : IM_COL32(120, 96, 40, 220);
            dl->AddRectFilled(ImVec2(p0.x + 2.0f * z, my), ImVec2(p1.x - 2.0f * z, p1.y - 2.0f * z), band, ROUND, ImDrawFlags_RoundCornersBottom);
            std::string shown = inlineMsg;
            const float maxW = NODE_W - PAD * 2.0f;
            while (shown.size() > 4 && ImGui::CalcTextSize(shown.c_str()).x > maxW) shown.pop_back();
            if (shown.size() < inlineMsg.size()) shown += "...";
            text(ImVec2(p0.x + PAD, my + 4.0f * z), IM_COL32(255, 236, 228, 255), shown.c_str());
            if (showAdd && VsAddComponentById && vsOwner) {
                ImGui::SetCursorScreenPos(ImVec2(p0.x + PAD, my + fs + 8.0f * z));
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 3.0f));
                if (ImGui::SmallButton(reqGap.addLabel.c_str())) VsAddComponentById(vsOwner, reqGap.addId.c_str());
                ImGui::PopStyleVar();
            }
            if (ImGui::IsMouseHoveringRect(ImVec2(p0.x, my), p1) && !showAdd) ImGui::SetTooltip("%s", inlineMsg.c_str());
            else if (showAdd && ImGui::IsMouseHoveringRect(ImVec2(p0.x, my), ImVec2(p1.x, my + fs + 6.0f)))
                ImGui::SetTooltip("%s", reqGap.message.c_str());
        }

        ImGui::PopID();
    }
    // Move the dragged node and every other selected node with it.
    if (startNodeDrag) {
        float dx = io.MouseDelta.x / (z * kLayout), dy = io.MouseDelta.y / (z * kLayout);
        if (dx != 0.0f || dy != 0.0f) {
            for (auto& n : g.nodes) if (st.sel.count(n.id)) { n.x += dx; n.y += dy; }
            g.dirty = true;
        }
    }
    if (st.wasNodeDrag && !startNodeDrag && ImGui::IsMouseReleased(ImGuiMouseButton_Left) && st.snapGrid) {
        bool moved = false;
        for (auto& n : g.nodes) if (st.sel.count(n.id)) {
            const float x = VsSnap(n.x, 20.0f), y = VsSnap(n.y, 20.0f);
            if (x != n.x || y != n.y) { n.x = x; n.y = y; moved = true; }
        }
        if (moved) g.dirty = true;
    }
    st.wasNodeDrag = startNodeDrag;
    if (pendingDup >= 0 || (winFocused && !typing && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D, false) && !st.sel.empty())) {
        std::set<int> src = st.sel;
        if (src.empty() && pendingDup >= 0) src.insert(pendingDup);
        float mx = 1e9f, my = 1e9f;
        for (const auto& n : g.nodes) if (src.count(n.id)) { mx = std::min(mx, n.x); my = std::min(my, n.y); }
        st.sel = VsPasteText(g, VsSelectionText(g, src), mx + 30.0f, my + 30.0f);
        st.selected = st.sel.empty() ? -1 : *st.sel.begin();
    }
    if (winFocused && !typing && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V, false)) {
        const char* clip = ImGui::GetClipboardText();
        if (clip && std::strstr(clip, "node ")) {
            ImVec2 w = canvasWindowHovered ? S2W(mouse) : S2W(ImVec2((canvasMin.x + canvasMax.x) * 0.5f, (canvasMin.y + canvasMax.y) * 0.5f));
            st.sel = VsPasteText(g, clip, w.x, w.y);
            st.selected = st.sel.empty() ? -1 : *st.sel.begin();
        }
    }
    if (pendingDelete >= 0) {
        if (!st.sel.count(pendingDelete)) st.sel = {pendingDelete};
        for (int id : st.sel) g.removeNode(id);
        st.sel.clear(); st.selected = -1;
    }
    if (winFocused && !typing && (ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_Backspace)) &&
        (!st.sel.empty() || st.selected >= 0)) {
        if (st.selected >= 0) st.sel.insert(st.selected);
        for (int id : st.sel) g.removeNode(id);
        st.sel.clear(); st.selected = -1;
    }

    // ---- Wires, behind the nodes. Ones touching the selection are highlighted. --
    dl->ChannelsSetCurrent(1);
    auto bezier = [&](ImVec2 a, ImVec2 b, ImU32 col, float w) {
        float dx = std::max(50.0f * z, std::fabs(b.x - a.x) * 0.5f);
        dl->AddBezierCubic(a, ImVec2(a.x + dx, a.y), ImVec2(b.x - dx, b.y), b, col, w);
    };
    auto hotNode = [&](int id) { return st.sel.count(id) > 0 || st.selected == id; };
    // Alt held over a wire: it lights up, and a click breaks that wire only (the
    // other wires on the same pin stay). Nearest wire within a few pixels wins.
    auto bezierDist = [&](ImVec2 a, ImVec2 b) {
        const float dx = std::max(50.0f * z, std::fabs(b.x - a.x) * 0.5f);
        const ImVec2 c1(a.x + dx, a.y), c2(b.x - dx, b.y);
        float best = 1.0e9f;
        for (int i = 0; i <= 24; ++i) {
            const float t = i / 24.0f, u = 1.0f - t;
            const float x = u*u*u*a.x + 3*u*u*t*c1.x + 3*u*t*t*c2.x + t*t*t*b.x;
            const float y = u*u*u*a.y + 3*u*u*t*c1.y + 3*u*t*t*c2.y + t*t*t*b.y;
            best = std::min(best, (x - io.MousePos.x) * (x - io.MousePos.x) + (y - io.MousePos.y) * (y - io.MousePos.y));
        }
        return std::sqrt(best);
    };
    int altExec = -1, altData = -1;   // index of the wire under the mouse (Alt held)
    if (io.KeyAlt && !st.dragging && ImGui::IsWindowHovered()) {
        float best = 9.0f;
        for (int i = 0; i < (int)g.execLinks.size(); ++i) {
            const auto& l = g.execLinks[(std::size_t)i];
            auto a = pinPos.find(pk(l.from, 0, 1, l.pin)), b = pinPos.find(pk(l.to, 0, 0, 0));
            if (a == pinPos.end() || b == pinPos.end()) continue;
            const float dd = bezierDist(a->second, b->second);
            if (dd < best) { best = dd; altExec = i; altData = -1; }
        }
        for (int i = 0; i < (int)g.dataLinks.size(); ++i) {
            const auto& l = g.dataLinks[(std::size_t)i];
            auto a = pinPos.find(pk(l.producer, 1, 1, l.outPin)), b = pinPos.find(pk(l.consumer, 1, 0, l.inPin));
            if (a == pinPos.end() || b == pinPos.end()) continue;
            const float dd = bezierDist(a->second, b->second);
            if (dd < best) { best = dd; altData = i; altExec = -1; }
        }
        if ((altExec >= 0 || altData >= 0) && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            if (altExec >= 0) g.execLinks.erase(g.execLinks.begin() + altExec);
            else g.dataLinks.erase(g.dataLinks.begin() + altData);
            g.dirty = true; altExec = altData = -1;
        }
    }
    int execIdx = -1;
    for (const auto& l : g.execLinks) {
        ++execIdx;
        auto a = pinPos.find(pk(l.from, 0, 1, l.pin)), b = pinPos.find(pk(l.to, 0, 0, 0));
        if (a == pinPos.end() || b == pinPos.end()) continue;
        if (execIdx == altExec) { bezier(a->second, b->second, IM_COL32(255, 120, 100, 255), 4.2f * std::max(0.7f, z)); continue; }
        const bool hot = hotNode(l.from) || hotNode(l.to);
        const bool ran = live && st.flash.count(l.to) && st.flash[l.to] > 0.0f;
        ImU32 col = ran ? IM_COL32(255, 205, 90, 255) : hot ? IM_COL32(255, 255, 255, 255) : IM_COL32(215, 216, 222, 200);
        bezier(a->second, b->second, col, (hot || ran ? 3.6f : 2.8f) * std::max(0.7f, z));
    }
    int dataIdx = -1;
    for (const auto& l : g.dataLinks) {
        ++dataIdx;
        auto a = pinPos.find(pk(l.producer, 1, 1, l.outPin)), b = pinPos.find(pk(l.consumer, 1, 0, l.inPin));
        if (a == pinPos.end() || b == pinPos.end()) continue;
        if (dataIdx == altData) { bezier(a->second, b->second, IM_COL32(255, 120, 100, 255), 4.2f * std::max(0.7f, z)); continue; }
        const bool hot = hotNode(l.producer) || hotNode(l.consumer);
        // A wire whose value can't mean what the input needs (from an older graph
        // or hand-written file) is drawn red; the checker lists it too.
        if (!VsWireOk(g, l.consumer, l.inPin, l.producer, l.outPin)) {
            bezier(a->second, b->second, IM_COL32(255, 80, 70, 240), (hot ? 3.6f : 3.0f) * std::max(0.7f, z));
            continue;
        }
        ImU32 wc = VsPinColor(VsOutPinType(g, l.producer, l.outPin));
        if (!hot) wc = (wc & 0x00FFFFFF) | (200u << 24);
        bezier(a->second, b->second, wc, (hot ? 3.4f : 2.6f) * std::max(0.7f, z));
    }
    dl->ChannelsSetCurrent(2);
    for (const VsBadge& b : badges) {
        const float r = 7.0f * std::max(0.8f, z);
        dl->AddCircleFilled(b.c, r, b.col, 16);
        const ImVec2 ts = ImGui::CalcTextSize("!");
        dl->AddText(ImVec2(b.c.x - ts.x * 0.5f, b.c.y - ts.y * 0.5f), IM_COL32(20, 20, 24, 255), "!");
        if (ImGui::IsWindowHovered() && std::fabs(io.MousePos.x - b.c.x) < r * 1.6f && std::fabs(io.MousePos.y - b.c.y) < r * 1.6f)
            ImGui::SetTooltip("%s", b.tips.c_str());
    }

    // Live drag wire + release handling.
    bool openPaletteNow = false;
    if (st.dragging) {
        auto from = pinPos.find(pk(st.dragNode, st.dragExec ? 0 : 1, st.dragFromOutput ? 1 : 0, st.dragPin));
        if (from != pinPos.end()) {
            ImVec2 m = io.MousePos;
            bezier(st.dragFromOutput ? from->second : m, st.dragFromOutput ? m : from->second,
                   IM_COL32(255, 255, 255, 170), 2.6f * std::max(0.7f, z));
        }
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            // Find a pin under the mouse of the opposite side + same kind.
            ImVec2 m = io.MousePos;
            float snap = std::max(26.0f, 30.0f * z);
            int bestNode = -1, bestIdx = -1; float bestD = snap * snap;   // forgiving snap radius
            int wantSide = st.dragFromOutput ? 0 : 1;   // connect to an input if we came from an output
            int wantKind = st.dragExec ? 0 : 1;
            for (auto& kvp : pinPos) {
                int node = (int)(kvp.first >> 20);
                int kind = (int)((kvp.first >> 18) & 3);
                int side = (int)((kvp.first >> 16) & 3);
                int idx  = (int)(kvp.first & 0xFFFF);
                if (kind != wantKind || side != wantSide || node == st.dragNode) continue;
                float dx = kvp.second.x - m.x, dy = kvp.second.y - m.y;
                float dd = dx * dx + dy * dy;
                if (dd < bestD) { bestD = dd; bestNode = node; bestIdx = idx; }
            }
            if (bestNode >= 0) {
                if (st.dragExec) {
                    const int dest = st.dragFromOutput ? bestNode : st.dragNode;
                    const VsDef* dd = nullptr;
                    if (const VsEditNode* dn = g.find(dest)) dd = FindDef(dn->type);
                    if (dd && !dd->execIn) {
                        st.refuseMsg = "Events start a chain. Wire this into a step, not into another event.";
                        st.refuseTime = 5.0f; st.offerOn = false;
                    } else if (st.dragFromOutput) g.linkExec(st.dragNode, st.dragPin, bestNode);
                    else g.linkExec(bestNode, bestIdx, st.dragNode);
                } else {
                    // Refuse a wire whose value can't work in that input (and say why).
                    const int cons = st.dragFromOutput ? bestNode : st.dragNode, inP = st.dragFromOutput ? bestIdx : st.dragPin;
                    const int prod = st.dragFromOutput ? st.dragNode : bestNode, outP = st.dragFromOutput ? st.dragPin : bestIdx;
                    std::string why;
                    std::vector<std::pair<int, int>> edges;
                    for (const auto& l : g.dataLinks) edges.push_back({l.producer, l.consumer});
                    if (VsDataWouldCycle(edges, prod, cons)) {
                        st.refuseMsg = "That would loop: this value already depends on the node you are wiring into.";
                        st.refuseTime = 5.0f; st.offerOn = false;
                    } else if (VsWireOk(g, cons, inP, prod, outP, &why)) {
                        g.linkData(cons, inP, prod, outP); st.offerOn = false;
                    } else {
                        const auto offer = VsOfferConvert((char)VsOutPinType(g, prod, outP), (char)VsInPinType(g, cons, inP));
                        st.refuseMsg = why.empty() ? offer.why : why;
                        if (!offer.why.empty() && st.refuseMsg.find(offer.why) == std::string::npos && offer.type.empty())
                            st.refuseMsg += " " + offer.why;
                        st.refuseTime = 6.0f;
                        st.offerOn = !offer.type.empty();
                        st.offerCons = cons; st.offerIn = inP; st.offerProd = prod; st.offerOut = outP;
                        st.offerType = offer.type; st.offerWhy = offer.label;
                    }
                }
            } else {
                // Dropped on the other kind of pin: say so instead of doing nothing.
                float hitDist2 = std::max(26.0f, 30.0f * z); hitDist2 *= hitDist2;
                int nk = -1, ns = -1;
                for (auto& kvp : pinPos) {
                    int node = (int)(kvp.first >> 20);
                    int kind = (int)((kvp.first >> 18) & 3);
                    int side = (int)((kvp.first >> 16) & 3);
                    if (node == st.dragNode) continue;
                    float dx = kvp.second.x - m.x, dy = kvp.second.y - m.y;
                    if (dx * dx + dy * dy < hitDist2) { nk = kind; ns = side; break; }
                }
                if (nk >= 0 && nk != (st.dragExec ? 0 : 1)) {
                    st.refuseMsg = st.dragExec
                        ? "This wire carries the flow (the white triangle). Drop it on another triangle, not on a value circle."
                        : "This wire carries a value. Drop it on a coloured circle, not on a flow triangle.";
                    st.refuseTime = 5.0f; st.offerOn = false; (void)ns;
                } else {
                // Dropped in empty space: offer the palette and wire up what's picked.
                bool overNode = false;
                for (const auto& kv : nodeRect)
                    if (m.x >= kv.second.first.x && m.x <= kv.second.second.x && m.y >= kv.second.first.y && m.y <= kv.second.second.y) { overNode = true; break; }
                if (!overNode && m.x >= canvasMin.x && m.x <= canvasMax.x && m.y >= canvasMin.y && m.y <= canvasMax.y) {
                    st.linkPending = true; st.linkExec = st.dragExec; st.linkFromOutput = st.dragFromOutput;
                    st.linkNode = st.dragNode; st.linkPin = st.dragPin;
                    openPaletteNow = true;
                }
                }
            }
            st.dragging = false;
        }
    }

    // ---- Box selection --------------------------------------------------------------
    if (bgClicked && !st.dragging && !io.KeyAlt) {
        st.boxing = true; st.boxStart = mouse;
        if (!io.KeyCtrl && !io.KeyShift) { st.sel.clear(); st.selected = -1; }
    }
    if (st.boxing) {
        ImVec2 a(std::min(st.boxStart.x, mouse.x), std::min(st.boxStart.y, mouse.y));
        ImVec2 b(std::max(st.boxStart.x, mouse.x), std::max(st.boxStart.y, mouse.y));
        dl->AddRectFilled(a, b, IM_COL32(120, 170, 255, 40));
        dl->AddRect(a, b, IM_COL32(120, 170, 255, 200), 0.0f, 0, 1.5f);
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            if (b.x - a.x > 3.0f || b.y - a.y > 3.0f)
                for (const auto& kv : nodeRect) {
                    const ImVec2& p0 = kv.second.first; const ImVec2& p1 = kv.second.second;
                    if (p1.x >= a.x && p0.x <= b.x && p1.y >= a.y && p0.y <= b.y) st.sel.insert(kv.first);
                }
            st.selected = st.sel.empty() ? -1 : *st.sel.begin();
            st.boxing = false;
        }
    }
    dl->ChannelsMerge();
    ImGui::SetWindowFontScale(1.0f);   // popups / overlay text at normal size

    // ---- Minimap (bottom-right): the whole graph + the visible area; click to jump --
    if (st.showMinimap && !g.nodes.empty()) {
        const ImVec2 mmSize(220.0f, 148.0f);
        const ImVec2 mmMax(canvasMax.x - 10.0f, canvasMax.y - 10.0f), mmMin(mmMax.x - mmSize.x, mmMax.y - mmSize.y);
        float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
        for (const auto& n : g.nodes) {
            x0 = std::min(x0, n.x * kLayout); y0 = std::min(y0, n.y * kLayout);
            x1 = std::max(x1, n.x * kLayout + 230.0f); y1 = std::max(y1, n.y * kLayout + 150.0f);
        }
        // Include the visible area so its rectangle always fits.
        ImVec2 v0 = S2W(canvasMin), v1 = S2W(canvasMax);
        x0 = std::min(x0, v0.x * kLayout); y0 = std::min(y0, v0.y * kLayout);
        x1 = std::max(x1, v1.x * kLayout); y1 = std::max(y1, v1.y * kLayout);
        float sc = std::min((mmSize.x - 8.0f) / std::max(1.0f, x1 - x0), (mmSize.y - 8.0f) / std::max(1.0f, y1 - y0));
        auto M = [&](float wx, float wy) { return ImVec2(mmMin.x + 4.0f + (wx - x0) * sc, mmMin.y + 4.0f + (wy - y0) * sc); };
        dl->AddRectFilled(mmMin, mmMax, IM_COL32(22, 26, 34, 230), 10.0f);
        dl->AddRect(mmMin, mmMax, IM_COL32(120, 136, 160, 180), 10.0f, 0, 1.5f);
        for (const auto& n : g.nodes) {
            const VsDef* d = FindDef(n.type);
            ImVec2 a = M(n.x * kLayout, n.y * kLayout), b = M(n.x * kLayout + 220.0f, n.y * kLayout + 110.0f);
            dl->AddRectFilled(a, b, d ? CatColor(d->cat) : IM_COL32(120, 120, 120, 255), 2.0f);
            if (st.sel.count(n.id)) dl->AddRect(a, b, IM_COL32(140, 190, 255, 255), 2.0f);
        }
        dl->AddRect(M(v0.x * kLayout, v0.y * kLayout), M(v1.x * kLayout, v1.y * kLayout), IM_COL32(255, 255, 255, 200), 0.0f, 0, 1.5f);
        ImGui::SetCursorScreenPos(mmMin);
        ImGui::InvisibleButton("##minimap", mmSize);
        if (ImGui::IsItemActive()) {   // centre the view on the clicked point
            float wx = (mouse.x - mmMin.x - 4.0f) / sc + x0, wy = (mouse.y - mmMin.y - 4.0f) / sc + y0;
            st.pan = ImVec2((canvasMax.x - canvasMin.x) * 0.5f - wx * z, (canvasMax.y - canvasMin.y) * 0.5f - wy * z);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Minimap: click or drag to move the view");
    }

    // "Connect to... > Search all": the wire-drop list, started from that pin of the node.
    bool pconnPlace = false;
    if (pconn.on && pconn.openAll) {
        st.linkPending = true; st.linkExec = pconn.exec; st.linkFromOutput = pconn.fromOutput;
        st.linkNode = pconn.node; st.linkPin = pconn.pin;
        openPaletteNow = true; pconnPlace = true;
    }
    // Right-click empty canvas (or Space): node palette.
    if ((canvasHovered && ImGui::IsMouseReleased(ImGuiMouseButton_Right) && io.MouseDragMaxDistanceSqr[ImGuiMouseButton_Right] < 36.0f && !st.dragging) ||
        (winFocused && !typing && canvasWindowHovered && ImGui::IsKeyPressed(ImGuiKey_Space, false)) || openPaletteNow || st.addAtCenter) {
        st.openPalette = true;
        if (!openPaletteNow) st.linkPending = false;
        st.paletteWorld = st.addAtCenter ? S2W(ImVec2((canvasMin.x + canvasMax.x) * 0.5f - 100.0f, (canvasMin.y + canvasMax.y) * 0.5f - 40.0f))
                                         : S2W(io.MousePos);
        st.addAtCenter = false;
        if (pconnPlace) st.paletteWorld = pconn.at;
        ImGui::OpenPopup("##vspalette");
    }
    // A template picked in the toolbar: drop its nodes in the middle of the view.
    if (st.pasteTemplate) {
        st.pasteTemplate = false;
        ImVec2 c = S2W(ImVec2(canvasMin.x + 60.0f, (canvasMin.y + canvasMax.y) * 0.5f - 60.0f));
        std::set<int> made = VsPasteText(g, st.templateText, c.x, c.y);
        st.sel = made; st.selected = made.empty() ? -1 : *made.begin();
        st.frameSel = !made.empty();
        st.templateText.clear();
    }
    // Add a node from the palette; wire it to the dropped pin when there is one.
    auto addFromPalette = [&](const VsDef& d, const std::string& arg = std::string()) {
        int nid = g.add(d.type, st.paletteWorld.x, st.paletteWorld.y);
        if (!arg.empty()) if (VsEditNode* nn = g.find(nid)) nn->arg = arg;
        st.selected = nid; st.sel = {nid};
        if (st.linkPending) {
            if (st.linkExec) {
                if (st.linkFromOutput) { if (d.execIn) g.linkExec(st.linkNode, st.linkPin, nid); }
                else if (!d.execOut.empty()) g.linkExec(nid, 0, st.linkNode);
            } else {
                // The first pin on the new node that can take / give this value.
                if (st.linkFromOutput) {
                    for (int pi = 0; pi < (int)d.dataIn.size(); ++pi)
                        if (VsWireOk(g, nid, pi, st.linkNode, st.linkPin)) { g.linkData(nid, pi, st.linkNode, st.linkPin); break; }
                } else {
                    for (int po = 0; po < (int)d.dataOut.size(); ++po)
                        if (VsWireOk(g, st.linkNode, st.linkPin, nid, po)) { g.linkData(st.linkNode, st.linkPin, nid, po); break; }
                }
            }
            // Drop it beside the pin rather than under the cursor tip.
            if (VsEditNode* nn = g.find(nid)) { if (!st.linkFromOutput) nn->x -= 170.0f; }
            st.linkPending = false;
        }
    };
    // "Connect to... > a node": add it already wired to the pin it was chosen for.
    if (pconn.on && !pconn.openAll && pconn.def) {
        st.linkPending = true; st.linkExec = pconn.exec; st.linkFromOutput = pconn.fromOutput;
        st.linkNode = pconn.node; st.linkPin = pconn.pin;
        st.paletteWorld = pconn.at;
        addFromPalette(*pconn.def);
        st.linkPending = false;
    }
    ImGui::SetNextWindowSize(ImVec2(1180, 760), ImGuiCond_Always);
    if (ImGui::BeginPopup("##vspalette")) {
        DrawNodePalette(st, g, [&](const VsDef& d, const std::string& arg) { addFromPalette(d, arg); });
        ImGui::EndPopup();

} else if (st.linkPending && !openPaletteNow) {
        st.linkPending = false;   // palette dismissed without a pick
    }

    if (st.missing) {
        const char* msg = "This graph file could not be opened.";
        const std::string path = g.path.empty() ? std::string("(no path)") : g.path;
        dl->AddText(ImVec2(canvasMin.x + 24.0f, canvasMin.y + 24.0f), IM_COL32(255, 170, 150, 255), msg);
        dl->AddText(ImVec2(canvasMin.x + 24.0f, canvasMin.y + 48.0f), IM_COL32(200, 206, 216, 255), path.c_str());
    } else if (g.nodes.empty()) {
        const float cardW = 480.0f, cardH = 220.0f;
        const ImVec2 c0((canvasMin.x + canvasMax.x - cardW) * 0.5f, (canvasMin.y + canvasMax.y - cardH) * 0.5f);
        const ImVec2 c1(c0.x + cardW, c0.y + cardH);
        dl->AddRectFilled(c0, c1, IM_COL32(32, 36, 46, 245), 16.0f);
        dl->AddRect(c0, c1, IM_COL32(120, 140, 168, 160), 16.0f, 0, 1.5f);
        const char* title = "This graph is empty";
        const char* hint = "Right-click the canvas, or press Space, to add a node.";
        const char* hint2 = "Start with an event such as On Start, then wire what happens next.";
        const ImVec2 ts = ImGui::CalcTextSize(title);
        dl->AddText(ImVec2(c0.x + (cardW - ts.x) * 0.5f, c0.y + 36.0f), IM_COL32(236, 240, 246, 255), title);
        const ImVec2 hs = ImGui::CalcTextSize(hint);
        dl->AddText(ImVec2(c0.x + (cardW - hs.x) * 0.5f, c0.y + 72.0f), IM_COL32(170, 180, 196, 255), hint);
        const ImVec2 h2 = ImGui::CalcTextSize(hint2);
        dl->AddText(ImVec2(c0.x + (cardW - h2.x) * 0.5f, c0.y + 96.0f), IM_COL32(150, 160, 176, 255), hint2);
        ImGui::SetCursorScreenPos(ImVec2(c0.x + (cardW - 180.0f) * 0.5f, c0.y + 148.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(16.0f, 10.0f));
        if (ImGui::Button("Add a node", ImVec2(180.0f, 0.0f))) st.addAtCenter = true;
        ImGui::PopStyleVar();
    } else if (st.needsEvent) {
        const char* msg = "Nothing runs yet. Add an event (On Start, On Update, On Key) and wire it to the first step.";
        const ImVec2 ts = ImGui::CalcTextSize(msg);
        const ImVec2 bp(canvasMin.x + 18.0f, canvasMin.y + 16.0f);
        dl->AddRectFilled(bp, ImVec2(bp.x + ts.x + 24.0f, bp.y + ts.y + 16.0f), IM_COL32(64, 48, 24, 230), 8.0f);
        dl->AddText(ImVec2(bp.x + 12.0f, bp.y + 8.0f), IM_COL32(245, 214, 150, 255), msg);
    } else if (st.showHint) {
        const char* msg = "Right-click or press Space to add a node    ·    Scroll to zoom    ·    Drag empty space to select";
        dl->AddText(ImVec2(canvasMin.x + 16.0f, canvasMax.y - 28.0f), IM_COL32(168, 178, 194, 180), msg);
    }
    // Why the last wire was refused. Wrapped, so the suggestion is not cut off.
    if (st.refuseTime > 0.0f && !st.refuseMsg.empty()) {
        st.refuseTime -= io.DeltaTime;
        const float boxW = std::min(560.0f, canvasMax.x - canvasMin.x - 32.0f);
        ImGui::SetCursorScreenPos(ImVec2(canvasMin.x + 16.0f, canvasMax.y - 120.0f));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(64, 22, 22, 245));
        ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(220, 90, 80, 255));
        ImGui::BeginChild("##vsrefuse", ImVec2(boxW, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders);
        ImGui::PushTextWrapPos(boxW - 16.0f);
        ImGui::TextUnformatted(st.refuseMsg.c_str());
        ImGui::PopTextWrapPos();
        if (st.offerOn && !st.offerType.empty() && ImGui::Button(st.offerWhy.empty() ? "Insert conversion" : st.offerWhy.c_str())) {
            const VsEditNode* prod = g.find(st.offerProd);
            const VsEditNode* cons = g.find(st.offerCons);
            const float x = prod && cons ? (prod->x + cons->x) * 0.5f : 80.0f;
            const float y = prod && cons ? (prod->y + cons->y) * 0.5f + 40.0f : 80.0f;
            const int id = g.add(st.offerType, x, y);
            g.linkData(id, 0, st.offerProd, st.offerOut);
            g.linkData(st.offerCons, st.offerIn, id, 0);
            st.offerOn = false; st.refuseMsg.clear(); st.refuseTime = 0;
        }
        ImGui::EndChild();
        ImGui::PopStyleColor(2);
    }
    // The checker's list (toolbar button): click a row to jump to its node.
    if (st.showIssues && !st.issues.empty()) {
        const float w = std::min(820.0f, canvasMax.x - canvasMin.x - 36.0f);
        ImGui::SetCursorScreenPos(ImVec2(canvasMax.x - w - 14, canvasMin.y + 40));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(28, 29, 34, 245));
        ImGui::BeginChild("##vsissuelist", ImVec2(w, std::min(260.0f, 34.0f + 26.0f * (float)st.issues.size())), ImGuiChildFlags_Borders);
        ImGui::TextDisabled("Problems the checker found");
        ImGui::Separator();
        for (std::size_t i = 0; i < st.issues.size(); ++i) {
            const auto& is = st.issues[i];
            ImGui::PushID((int)i);
            ImGui::PushStyleColor(ImGuiCol_Text, is.severity == 0 ? ImVec4(1.0f, 0.5f, 0.45f, 1) : ImVec4(0.95f, 0.78f, 0.4f, 1));
            std::string row = std::string(is.severity == 0 ? "Error: " : "Warning: ") + is.message;
            if (ImGui::Selectable(row.c_str()) && is.node >= 0) { st.sel = {is.node}; st.selected = is.node; st.focusNode = is.node; }
            if (ImGui::IsItemHovered()) {   // long messages: the whole text
                ImGui::BeginTooltip(); ImGui::PushTextWrapPos(ImGui::GetFontSize() * 36.0f);
                ImGui::TextUnformatted(row.c_str()); ImGui::PopTextWrapPos(); ImGui::EndTooltip();
            }
            ImGui::PopStyleColor();
            ImGui::PopID();
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }
    if (live) {
        std::string lv = "\xE2\x97\x8F LIVE: " + liveName;
        ImVec2 ts = ImGui::CalcTextSize(lv.c_str());
        dl->AddText(ImVec2(canvasMax.x - ts.x - 16, canvasMin.y + 12), IM_COL32(115, 230, 140, 255), lv.c_str());
        // A runtime error (runaway loop, data cycle...): say it on the canvas too,
        // not only in the Console. The node it happened at is outlined in red.
        if (!live->LastError().empty()) {
            std::string er = "\xE2\x9A\xA0 " + live->LastError();
            if (live->ErrorCount() > 1) er += "  (x" + std::to_string(live->ErrorCount()) + ")";
            const float wrap = std::max(200.0f, canvasMax.x - canvasMin.x - 36.0f);
            dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(canvasMin.x + 18, canvasMax.y - 12 - ImGui::GetTextLineHeight() * 2),
                        IM_COL32(255, 120, 110, 255), er.c_str(), nullptr, wrap);
        }
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();

    // ---- OkayScript preview panel -----------------------------------------
    // Live compile of the graph to OkayScript, side by side with the canvas, so
    // the two front-ends stay legibly equivalent. Read-only here (the graph is
    // the source of truth); Copy drops it into the clipboard to paste into the
    // text script editor.
    if (dv.stopped) {
        // ---- Debugger watch: where the flow is and what it can see ------------
        ImGui::SameLine();
        ImGui::BeginChild("vsdbgwatch", ImVec2(0, 0), true);
        auto row = [](const std::string& name, const okay::vs::VsValue& v) {
            ImGui::TextDisabled("%s", name.c_str());
            ImGui::SameLine(110.0f);
            if (v.IsColor()) {   // a swatch beside the hex
                const okay::vs::VsColor c = v.AsColor();
                ImGui::ColorButton("##dbgcol", ImVec4(c.r, c.g, c.b, c.a), ImGuiColorEditFlags_NoTooltip, ImVec2(14, 14));
                ImGui::SameLine();
            }
            ImGui::TextWrapped("%s", VsDebugText(v).c_str());
        };
        if (ImGui::CollapsingHeader("Call stack", ImGuiTreeNodeFlags_DefaultOpen)) {
            for (int i = (int)dv.frames.size() - 1; i >= 0; --i)
                ImGui::BulletText("%s  (function)", dv.frames[(std::size_t)i].name.c_str());
            ImGui::BulletText("%s  (event on '%s')", dv.event.c_str(), dv.object.c_str());
        }
        if (!dv.frames.empty()) {
            const auto& f = dv.frames.back();
            const std::string hdr = "Function " + f.name;
            if (ImGui::CollapsingHeader(hdr.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
                okay::vs::FunctionSig sig;
                bool haveSig = false;
                for (const auto& m : g.nodes)
                    if (m.type == "FunctionStart" && okay::vs::FunctionNameOf(m.arg) == f.name) { haveSig = okay::vs::ParseFunctionSignature(m.arg, sig); break; }
                for (std::size_t i = 0; i < f.args.size(); ++i)
                    row(haveSig && i < sig.params.size() ? sig.params[i].name : "arg " + std::to_string(i), f.args[i]);
                std::vector<std::pair<std::string, okay::vs::VsValue>> locals(f.locals.begin(), f.locals.end());
                std::sort(locals.begin(), locals.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
                for (const auto& kv : locals) row("local " + kv.first, kv.second);
                if (f.args.empty() && locals.empty()) ImGui::TextDisabled("(no parameters or locals)");
            }
        }
        auto section = [&](const char* title, const std::vector<std::pair<std::string, okay::vs::VsValue>>& vars, bool open) {
            if (!ImGui::CollapsingHeader(title, open ? ImGuiTreeNodeFlags_DefaultOpen : 0)) return;
            if (vars.empty()) ImGui::TextDisabled("(none)");
            for (const auto& kv : vars) row(kv.first, kv.second);
        };
        const std::string objTitle = "Variables on '" + dv.object + "'";
        section(objTitle.c_str(), dv.vars, true);
        section("Scene variables", dv.sceneVars, false);
        section("Global variables", dv.globalVars, false);
        ImGui::Separator();
        ImGui::TextDisabled("Wire values show the last value each node gave.");
        ImGui::EndChild();
    } else if (st.showScript) {
        ImGui::SameLine();
        ImGui::BeginChild("vsscript", ImVec2(0, 0), true);
        std::vector<const okay::vs::VsIssue*> blocked;
        for (const auto& is : st.issues)
            if (is.severity == 0 && is.message.rfind("Cannot export node ", 0) == 0) blocked.push_back(&is);
        if (!blocked.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.40f, 1.0f), "This graph cannot be exported.");
            ImGui::TextWrapped("These nodes have no script. The export stops instead of skipping them.");
            ImGui::Separator();
            for (const okay::vs::VsIssue* is : blocked) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.55f, 0.48f, 1.0f));
                ImGui::TextWrapped("%s", is->message.c_str());
                ImGui::PopStyleColor();
            }
            ImGui::EndChild();
        } else {
        std::string code = CompileToOkayScript(g);
        ImGui::TextDisabled("OkayScript (generated from the graph)");
        ImGui::SameLine();
        if (ImGui::SmallButton("Copy")) ImGui::SetClipboardText(code.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Save .okay")) {
            std::string p = g.path;
            if (!p.empty()) { auto d = p.rfind(".okayvs"); if (d != std::string::npos) p = p.substr(0, d); }
            else { std::string dir = ed.projectDir(); p = (dir.empty() ? std::string("Script") : dir + "/Assets/Script"); }
            p += ".okay";
            std::ofstream f(p, std::ios::binary);
            if (f) { f << code; Log("Wrote " + p); } else Log("Save failed: " + p, 2);
        }
        ImGui::Separator();
        // Read-only multiline view (a copy buffer; edits don't flow back).
        static std::vector<char> buf;
        buf.assign(code.begin(), code.end()); buf.push_back('\0');
        ImGui::InputTextMultiline("##vsscriptview", buf.data(), buf.size(),
                                  ImVec2(-FLT_MIN, -FLT_MIN), ImGuiInputTextFlags_ReadOnly);
        ImGui::EndChild();
        }
    } else if (st.showDetails) {
        ImGui::SameLine();
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 14.0f));
        ImGui::BeginChild("vsdetails", ImVec2(0, 0), true);
        const VsEditNode* sn = st.selected >= 0 ? g.find(st.selected) : nullptr;
        if (!sn && !st.sel.empty()) sn = g.find(*st.sel.begin());
        if (!sn) {
            ImGui::TextUnformatted("Node details");
            ImGui::Spacing();
            ImGui::TextWrapped("Select a node to see what it does, its pins, and any problems the checker found.");
        } else {
            const VsDef* d = VsDefFor(g, *sn);
            ImGui::TextUnformatted(d ? VsNodeTitle(*sn, *d).c_str() : sn->type.c_str());
            ImGui::Spacing();
            bool targetWired = false;
            if (d) for (int ti = 0; ti < (int)d->dataIn.size(); ++ti)
                if (d->dataIn[ti] && std::strcmp(d->dataIn[ti], "Target") == 0) {
                    for (const auto& l : g.dataLinks) if (l.consumer == sn->id && l.inPin == ti) targetWired = true;
                }
            if (const VsNodeDoc* doc = VsNodeDocFor(sn->type)) VsDrawDocBody(*doc, true, vsOwner, targetWired);
            else ImGui::TextDisabled("No description for %s.", sn->type.c_str());
            bool anyIssue = false;
            for (const auto& is : st.issues) if (is.node == sn->id) {
                if (!anyIssue) { ImGui::Spacing(); ImGui::Separator(); ImGui::TextUnformatted("Problems"); anyIssue = true; }
                ImGui::PushStyleColor(ImGuiCol_Text, is.severity == 0 ? ImVec4(1.0f, 0.55f, 0.48f, 1) : ImVec4(0.95f, 0.78f, 0.42f, 1));
                ImGui::TextWrapped("%s", is.message.c_str());
                ImGui::PopStyleColor();
            }
            ImGui::Spacing();
            if (ImGui::Button("Frame this node", ImVec2(-1, 0))) { st.focusNode = sn->id; st.sel = {sn->id}; }
        }
        ImGui::EndChild();
        ImGui::PopStyleVar();
    }

    // ---- Undo tracking: commit a change once the gesture / edit is finished ----
    {
        std::string now = g.toText();
        if (now != st.lastText) {
            if (st.pendingBase.empty()) st.pendingBase = st.lastText;
            st.lastText = std::move(now);
        }
        if (!st.pendingBase.empty() && !ImGui::IsMouseDown(ImGuiMouseButton_Left) && !ImGui::IsAnyItemActive()) {
            if (g.toText() != st.pendingBase) {
                st.undo.push_back(std::move(st.pendingBase));
                st.redo.clear();
                if (st.undo.size() > 200) st.undo.erase(st.undo.begin());
            }
            st.pendingBase.clear();
            VsRefreshDirty(st);
        }
    }

    // Save prompt. Save is the focused button. Don't Save is not.
    if (Session().ask != VsSession::Ask::None) ImGui::OpenPopup("Save this graph?");
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Save this graph?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        VsSession& ses = Session();
        auto dirtyIdx = [&]() {
            std::vector<int> ids;
            for (int i = 0; i < (int)ses.tabs.size(); ++i) {
                if (!ses.tabs[(std::size_t)i]->g.dirty) continue;
                if (ses.ask == VsSession::Ask::CloseTab && i != ses.askTab) continue;
                if (ses.ask == VsSession::Ask::CloseOthers && i == ses.askTab) continue;
                if (ses.ask == VsSession::Ask::NewGraph && i != ses.askTab) continue;
                ids.push_back(i);
            }
            return ids;
        };
        const std::vector<int> ids = dirtyIdx();
        if (ses.ask == VsSession::Ask::NewGraph)
            ImGui::TextUnformatted("Save this graph before opening a new one?");
        else if (ses.ask == VsSession::Ask::CloseWindow)
            ImGui::TextUnformatted("Close the Visual Script window?");
        else if (ids.empty())
            ImGui::TextUnformatted("Close these graphs?");
        else
            ImGui::TextUnformatted("Save these graphs before closing?");
        for (int i : ids) ImGui::BulletText("%s", VsTabTitle(*ses.tabs[(std::size_t)i]).c_str());
        if (ses.ask == VsSession::Ask::CloseWindow && ids.empty())
            ImGui::TextDisabled("Nothing unsaved. The window will hide; the tabs stay.");
        ImGui::Spacing();
        if (ImGui::Button("Save", ImVec2(120, 0))) {
            VsSaveAllTabs(ed.projectDir());
            s_closeRemember = false;
            if (ses.ask == VsSession::Ask::NewGraph) {
                const bool starter = ses.newStarter;
                ses.ask = VsSession::Ask::None;
                NewGraph(starter);
                ImGui::CloseCurrentPopup();
            } else if (ses.ask == VsSession::Ask::CloseTab) s_closeIds.push_back(ses.askTab);
            else if (ses.ask == VsSession::Ask::CloseOthers) {
                for (int i = 0; i < (int)ses.tabs.size(); ++i) if (i != ses.askTab) s_closeIds.push_back(i);
            } else if (ses.ask == VsSession::Ask::CloseAll) {
                for (int i = 0; i < (int)ses.tabs.size(); ++i) s_closeIds.push_back(i);
            } else if (ses.ask == VsSession::Ask::CloseWindow && open) *open = false;
            ses.ask = VsSession::Ask::None;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SetItemDefaultFocus();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            ses.ask = VsSession::Ask::None;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ses.ask == VsSession::Ask::CloseWindow) {
            if (ImGui::Button("Hide", ImVec2(120, 0))) {
                if (open) *open = false;
                ses.ask = VsSession::Ask::None;
                ImGui::CloseCurrentPopup();
            }
        } else if (ImGui::Button("Don't Save", ImVec2(120, 0))) {
            if (ses.ask == VsSession::Ask::NewGraph) {
                const bool starter = ses.newStarter;
                ses.ask = VsSession::Ask::None;
                NewGraph(starter);
                ImGui::CloseCurrentPopup();
            } else {
                s_closeRemember = true;
                if (ses.ask == VsSession::Ask::CloseTab) s_closeIds.push_back(ses.askTab);
                else if (ses.ask == VsSession::Ask::CloseOthers) {
                    for (int i = 0; i < (int)ses.tabs.size(); ++i) if (i != ses.askTab) s_closeIds.push_back(i);
                } else if (ses.ask == VsSession::Ask::CloseAll) {
                    for (int i = 0; i < (int)ses.tabs.size(); ++i) s_closeIds.push_back(i);
                }
                ses.ask = VsSession::Ask::None;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }

    ImGui::End();
}

} // namespace okay::editor::vsed
