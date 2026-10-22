#pragma once
// A node-based visual scripting editor for OkayVS graphs (.okayvs). You drag
// nodes onto a canvas, wire execution (triangles) and data (circles) pins, edit
// per-node values, and save a .okayvs file the engine's VisualScriptComponent
// runs at play time. Self-contained: it keeps its own editable model and writes
// the same text format the engine parses (node/pos/exec/data/entry lines).
#include "imgui.h"
#include "EditorState.hpp"
#include "okay/Components/VisualScriptComponent.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace okay::editor::vsed {

// ---- Node registry: describes each node type the editor can place ----------
enum VsCat { CAT_EVENT, CAT_FLOW, CAT_VAR, CAT_MATH, CAT_LOGIC, CAT_INPUT, CAT_XFORM, CAT_ACTION, CAT_DATA, CAT_NET, CAT_COUNT };

inline ImU32 CatColor(int cat) {
    switch (cat) {
        case CAT_EVENT:  return IM_COL32(120, 84, 84, 255);
        case CAT_FLOW:   return IM_COL32(96, 96, 112, 255);
        case CAT_VAR:    return IM_COL32(74, 102, 92, 255);
        case CAT_MATH:   return IM_COL32(70, 92, 110, 255);
        case CAT_LOGIC:  return IM_COL32(96, 86, 112, 255);
        case CAT_INPUT:  return IM_COL32(104, 96, 72, 255);
        case CAT_XFORM:  return IM_COL32(78, 104, 104, 255);
        case CAT_ACTION: return IM_COL32(108, 88, 72, 255);
        case CAT_NET:    return IM_COL32(72, 92, 120, 255);
        default:         return IM_COL32(88, 88, 96, 255);
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
};

// A curated, genuinely-useful set of nodes (all supported by the engine runtime).
inline const std::vector<VsDef>& Registry() {
    static const std::vector<VsDef> defs = {
        // Events
        {"OnStart",  "On Start",  CAT_EVENT, false, {""}, {}, {}, 0, nullptr},
        {"OnUpdate", "On Update", CAT_EVENT, false, {""}, {}, {}, 0, nullptr},
        // Flow
        {"Branch",   "Branch",    CAT_FLOW, true, {"True","False"}, {"Cond"}, {}, 0, nullptr},
        {"Sequence", "Sequence",  CAT_FLOW, true, {"0","1"}, {}, {}, 0, nullptr},
        {"Once",     "Once",      CAT_FLOW, true, {""}, {}, {}, 0, nullptr},
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
        {"Print",     "Print",     CAT_ACTION, true, {""}, {"Value"}, {}, 0, nullptr},
        {"PlaySound", "Play Sound",CAT_ACTION, true, {""}, {}, {}, 0, nullptr},
        {"Spawn",     "Spawn",     CAT_ACTION, true, {""}, {"X","Y","Z"}, {}, 1, "prefab"},
        {"Destroy",   "Destroy",   CAT_ACTION, true, {""}, {}, {}, 0, nullptr},
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
        {"ObjectExists",  "Object Exists",  CAT_XFORM, false, {}, {}, {"Bool"}, 1, "name"},
        {"ObjectPosition","Object Position",CAT_XFORM, false, {}, {}, {"Pos"}, 1, "name / self / other"},
        {"DistanceTo",    "Distance To",    CAT_XFORM, false, {}, {}, {"Dist"}, 1, "name / other"},
        {"SelfName",      "Self Name",      CAT_XFORM, false, {}, {}, {"Name"}, 0, nullptr},
        {"LookAt",        "Look At",        CAT_XFORM, true, {""}, {}, {}, 1, "name / other"},
        {"MoveTowards",   "Move Towards",   CAT_XFORM, true, {""}, {"Speed"}, {}, 1, "name / other"},
        {"SetActive",     "Set Active",     CAT_ACTION, true, {""}, {"On"}, {}, 1, "name / self / other"},
        {"SetText",       "Set Text",       CAT_ACTION, true, {""}, {"Text"}, {}, 1, "text object name"},
        {"LoadScene",     "Load Scene",     CAT_ACTION, true, {""}, {}, {}, 1, "scene name"},
        // --- Input / flow ---
        {"MouseButton",     "Mouse Held",    CAT_INPUT, false, {}, {}, {"Down"}, 1, "0 L / 1 R / 2 M"},
        {"MouseButtonDown", "Mouse Pressed", CAT_INPUT, false, {}, {}, {"Pressed"}, 1, "0 L / 1 R / 2 M"},
        {"Cooldown",        "Cooldown (s)",  CAT_FLOW,  true, {""}, {}, {}, 1, "seconds"},
        // --- Animation & IK ---
        {"AnimTrigger",  "Anim Trigger",   CAT_ACTION, true, {""}, {}, {}, 1, "parameter"},
        {"AnimSetFloat", "Anim Set Float", CAT_ACTION, true, {""}, {"Value"}, {}, 1, "parameter"},
        {"AnimSetBool",  "Anim Set Bool",  CAT_ACTION, true, {""}, {"Value"}, {}, 1, "parameter"},
        {"AnimState",    "Anim State",     CAT_DATA, false, {}, {}, {"State"}, 0, nullptr},
        {"IkLook",       "IK Look At",     CAT_ACTION, true, {""}, {"Point","Weight"}, {}, 0, nullptr},
        {"IkHand",       "IK Hand Reach",  CAT_ACTION, true, {""}, {"Point","Weight"}, {}, 1, "left / right"},
        // --- Flow: loops, gates, switches ---
        {"ForLoop",   "For Loop",      CAT_FLOW, true, {"Body","Done"}, {"First","Last"}, {"Index"}, 0, nullptr},
        {"Repeat",    "Repeat N",      CAT_FLOW, true, {"Body"}, {}, {}, 1, "count"},
        {"FlipFlop",  "Flip-Flop",     CAT_FLOW, true, {"A","B"}, {}, {}, 0, nullptr},
        {"Gate",      "Gate",          CAT_FLOW, true, {""}, {"Open"}, {}, 0, nullptr},
        {"DoN",       "Do N Times",    CAT_FLOW, true, {""}, {}, {"Count"}, 1, "N"},
        {"SwitchInt", "Switch (int)",  CAT_FLOW, true, {"0","1","2","Default"}, {"Value"}, {}, 0, nullptr},
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
        {"SteamName",      "Steam Name",     CAT_NET, false, {}, {}, {"Name"}, 0, nullptr},
        {"SteamIsUnlocked","Achievement Unlocked", CAT_NET, false, {}, {}, {"Bool"}, 1, "achievement id"},
        {"SteamUnlock",    "Unlock Achievement",   CAT_NET, true, {""}, {}, {}, 1, "achievement id"},
        {"SteamGetStat",   "Get Steam Stat", CAT_NET, false, {}, {}, {"Value"}, 1, "stat"},
        {"SteamSetStat",   "Set Steam Stat", CAT_NET, true, {""}, {"Value"}, {}, 1, "stat"},
        {"SteamStore",     "Store Stats",    CAT_NET, true, {""}, {}, {}, 0, nullptr},
        {"SteamLeaderboard","Submit Score",  CAT_NET, true, {""}, {"Score"}, {}, 1, "leaderboard"},
    };
    return defs;
}

// ---- Key picker for the Key Held / Key Down / Key Up nodes ------------------
// Stores the friendly name the runtime resolves (Input::KeyFromName): "space",
// "shift", "a", "7"... Typing a name into a tiny box was guesswork; this is a
// button that captures the next key press, plus a list to pick from.
inline bool IsKeyNode(const std::string& t) { return t == "Key" || t == "KeyDown" || t == "KeyUp"; }
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
inline std::string VsQuoted(std::string v) {
    for (char& c : v) if (c == '"') c = '\'';   // the file format has no escapes
    return "\"" + v + "\"";
}
inline std::string VsLiteral(const std::string& v) { return VsIsNumberOrBool(v) ? v : VsQuoted(v); }
struct VsExecLink { int from, pin, to; };
struct VsDataLink { int consumer, inPin, producer, outPin; };

struct VsEditGraph {
    std::vector<VsEditNode> nodes;
    std::vector<VsExecLink> execLinks;
    std::vector<VsDataLink> dataLinks;
    int nextId = 1;
    std::string path;      // .okayvs file (empty = unsaved)
    bool dirty = false;

    VsEditNode* find(int id) { for (auto& n : nodes) if (n.id == id) return &n; return nullptr; }
    int add(const std::string& type, float x, float y) {
        VsEditNode n; n.id = nextId++; n.type = type; n.x = x; n.y = y;
        const VsDef* d = FindDef(type);
        if (d && d->type == std::string("Compare")) n.arg = ">";
        else if (d && d->type == std::string("Timer")) n.arg = "1";
        else if (d && d->type == std::string("Const")) n.arg = "0";
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
    void linkExec(int from, int pin, int to) {
        execLinks.erase(std::remove_if(execLinks.begin(), execLinks.end(),
                    [&](const VsExecLink& l){ return l.from == from && l.pin == pin; }), execLinks.end());
        execLinks.push_back({from, pin, to}); dirty = true;
    }
    void linkData(int consumer, int inPin, int producer, int outPin) {
        dataLinks.erase(std::remove_if(dataLinks.begin(), dataLinks.end(),
                    [&](const VsDataLink& l){ return l.consumer == consumer && l.inPin == inPin; }), dataLinks.end());
        dataLinks.push_back({consumer, inPin, producer, outPin}); dirty = true;
    }

    std::string toText() const {
        std::ostringstream o;
        o << "# OkayVS graph (edited in the Visual Script editor)\n";
        for (const auto& n : nodes) {
            o << "node " << n.id << ' ' << n.type;
            if (!n.arg.empty()) o << ' ' << (n.arg.find(' ') != std::string::npos ? VsQuoted(n.arg) : n.arg);
            o << "\n";
            for (const auto& kv : n.pinDefault)
                if (!kv.second.empty()) o << "default " << n.id << ' ' << kv.first << ' ' << VsLiteral(kv.second) << "\n";
            o << "pos " << n.id << ' ' << (int)n.x << ' ' << (int)n.y << "\n";
            if (n.type == "OnStart")  o << "entry OnStart "  << n.id << "\n";
            if (n.type == "OnUpdate") o << "entry OnUpdate " << n.id << "\n";
            if (n.type == "OnCollision" || n.type == "OnTrigger" || n.type == "OnClick")
                o << "entry " << n.type << " " << n.id << "\n";
            // An On Anim Event node with a name only runs for that clip event.
            if (n.type == "OnAnimEvent")
                o << "entry OnAnimEvent" << (n.arg.empty() ? std::string() : ":" + n.arg) << " " << n.id << "\n";
        }
        for (const auto& l : execLinks) o << "exec " << l.from << ' ' << l.pin << ' ' << l.to << "\n";
        for (const auto& l : dataLinks) o << "data " << l.consumer << ' ' << l.inPin << ' ' << l.producer << ' ' << l.outPin << "\n";
        return o.str();
    }
    bool save(const std::string& p) {
        std::ofstream f(p, std::ios::binary);
        if (!f) return false;
        f << toText();
        path = p; dirty = false; return true;
    }
    void clear() { nodes.clear(); execLinks.clear(); dataLinks.clear(); nextId = 1; path.clear(); dirty = false; }
    bool load(const std::string& p) {
        std::ifstream f(p, std::ios::binary);
        if (!f) return false;
        clear(); path = p;
        std::string line;
        int maxId = 0;
        while (std::getline(f, line)) {
            if (auto h = line.find('#'); h != std::string::npos) line.erase(h);
            std::istringstream ls(line); std::string kind; ls >> kind;
            auto restOf = [&](std::istringstream& is) {   // the rest of the line, unquoted
                std::string r; std::getline(is, r);
                std::size_t a = r.find_first_not_of(" \t"); if (a == std::string::npos) return std::string();
                r = r.substr(a);
                while (!r.empty() && (r.back() == ' ' || r.back() == '\t' || r.back() == '\r')) r.pop_back();
                if (r.size() >= 2 && r.front() == '"' && r.back() == '"') r = r.substr(1, r.size() - 2);
                return r;
            };
            if (kind == "node") {
                VsEditNode n; ls >> n.id >> n.type; n.arg = restOf(ls);
                nodes.push_back(n); maxId = std::max(maxId, n.id);
            } else if (kind == "default") {
                int id = 0, pin = 0; ls >> id >> pin;
                std::string v = restOf(ls);
                if (auto* n = find(id)) n->pinDefault[pin] = v;
            } else if (kind == "pos") {
                int id; float x, y; ls >> id >> x >> y;
                if (auto* n = find(id)) { n->x = x; n->y = y; }
            } else if (kind == "exec") {
                VsExecLink l; ls >> l.from >> l.pin >> l.to; execLinks.push_back(l);
            } else if (kind == "data") {
                VsDataLink l; ls >> l.consumer >> l.inPin >> l.producer >> l.outPin; dataLinks.push_back(l);
            }
        }
        nextId = maxId + 1; dirty = false;
        return true;
    }
};

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
inline int VsExecTarget(const VsEditGraph& g, int fromNode, int pin) {
    for (const auto& l : g.execLinks) if (l.from == fromNode && l.pin == pin) return l.to;
    return -1;
}
inline std::string VsDataExpr(const VsEditGraph& g, int nodeId);
inline const VsEditNode* VsFind(const VsEditGraph& g, int id);
inline std::string VsInput(const VsEditGraph& g, int consumer, int inPin, const char* dflt = "0") {
    for (const auto& l : g.dataLinks)
        if (l.consumer == consumer && l.inPin == inPin)
            return VsDataExpr(g, l.producer);
    if (const VsEditNode* n = VsFind(g, consumer)) {
        auto it = n->pinDefault.find(inPin);
        if (it != n->pinDefault.end() && !it->second.empty()) return VsLiteral(it->second);
    }
    return dflt;
}
inline std::string VsDataExpr(const VsEditGraph& g, int nodeId) {
    const VsEditNode* n = VsFind(g, nodeId);
    if (!n) return "0";
    const std::string& t = n->type;
    auto A = [&]{ return VsInput(g, nodeId, 0); };
    auto B = [&]{ return VsInput(g, nodeId, 1); };
    auto C = [&]{ return VsInput(g, nodeId, 2); };
    auto lit = [&](const char* d){ return n->arg.empty() ? std::string(d) : n->arg; };
    auto q   = [&](const std::string& s){ return "\"" + s + "\""; };

    if (t == "Const")      return n->arg.empty() ? "0" : n->arg;
    if (t == "GetVar")     return lit("x");
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
    if (t == "Compare")    return "(" + A() + " " + lit(">") + " " + B() + ")";
    if (t == "Equals")     return "(" + A() + " == " + B() + ")";
    if (t == "NotEquals")  return "(" + A() + " != " + B() + ")";
    if (t == "And")        return "(" + A() + " && " + B() + ")";
    if (t == "Or")         return "(" + A() + " || " + B() + ")";
    if (t == "Xor")        return "(!" + A() + " != !" + B() + ")";
    if (t == "Not")        return "(!" + A() + ")";
    if (t == "Approx")     return "(abs(" + A() + " - " + B() + ") < 0.0001)";
    if (t == "Between")    return "(" + A() + " >= " + B() + " && " + A() + " <= " + C() + ")";
    if (t == "Chance")     return "(random() < " + lit("0.5") + ")";
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
    if (t == "ToFixed")    return "to_fixed(" + A() + ", " + lit("2") + ")";
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
    if (t == "MouseButton")    return "mouse_held(" + lit("0") + ")";
    if (t == "MouseButtonDown")return "mouse_down(" + lit("0") + ")";
    if (t == "AnimState")      return "anim_state()";
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
    if (t == "MouseWheel")    return "mouse_wheel()";
    if (t == "TimeScale")     return "timescale()";
    if (t == "FrameCount")    return "frame_count()";
    if (t == "NetConnected")  return "net_connected()";
    if (t == "NetIsServer")   return "net_is_server()";
    if (t == "NetIsClient")   return "net_is_client()";
    if (t == "NetPeers")      return "net_peers()";
    if (t == "NetId")         return "net_id()";
    if (t == "NetGetVar")     return "net_get(" + q(lit("x")) + ")";
    if (t == "NetMatchStarted") return "net_match_started()";
    if (t == "NetAllReady")   return "net_all_ready()";
    if (t == "SteamName")     return "steam_name()";
    if (t == "SteamIsUnlocked") return "steam_is_unlocked(" + q(lit("")) + ")";
    if (t == "SteamGetStat")  return "steam_get_stat(" + q(lit("stat")) + ")";
    return "0";
}

inline void VsEmitChain(const VsEditGraph& g, int nodeId, std::string& out,
                        const std::string& ind, std::set<int>& visited);
inline void VsEmitNode(const VsEditGraph& g, int nodeId, std::string& out,
                       const std::string& ind, std::set<int>& visited) {
    const VsEditNode* n = VsFind(g, nodeId); if (!n) return;
    const std::string& t = n->type;
    auto in  = [&](int p, const char* d = "0"){ return VsInput(g, nodeId, p, d); };
    auto arg = [&](const char* d){ return n->arg.empty() ? std::string(d) : n->arg; };
    auto q   = [&](const std::string& s){ return "\"" + s + "\""; };

    if      (t=="SetVar")      out += ind + arg("x") + " = " + in(0) + "\n";
    else if (t=="AddVar")      out += ind + arg("x") + " += " + in(0) + "\n";
    else if (t=="Print")       out += ind + "Debug.Log(" + in(0,"\"\"") + ")\n";
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
    else if (t=="AnimTrigger") out += ind + "anim_trigger(" + q(arg("")) + ")\n";
    else if (t=="AnimSetFloat")out += ind + "anim_set_float(" + q(arg("")) + ", " + in(0) + ")\n";
    else if (t=="AnimSetBool") out += ind + "anim_set_bool(" + q(arg("")) + ", " + in(0) + ")\n";
    else if (t=="IkLook")      out += ind + "ik_look(vec_x(" + in(0) + "), vec_y(" + in(0) + "), vec_z(" + in(0) + "), " + in(1, "1") + ")\n";
    else if (t=="IkHand")      out += ind + "ik_hand(" + q(arg("right")) + ", vec_x(" + in(0) + "), vec_y(" + in(0) + "), vec_z(" + in(0) + "), " + in(1, "1") + ")\n";
    else if (t=="Cooldown")    out += ind + "// Cooldown (" + arg("1") + "s): gate the actions after this with a timer\n";
    else if (t=="ForLoop") {
        const std::string v = "_i" + std::to_string(nodeId);
        out += ind + "for (var " + v + " = " + in(0, "0") + "; " + v + " <= " + in(1, "9") + "; " + v + " += 1) {\n";
        int b = VsExecTarget(g, nodeId, 0); if (b >= 0) { std::set<int> vb = visited; VsEmitChain(g, b, out, ind + "    ", vb); }
        out += ind + "}\n";
        int d = VsExecTarget(g, nodeId, 1); if (d >= 0) VsEmitChain(g, d, out, ind, visited);
    }
    else if (t=="Repeat") {
        const std::string v = "_r" + std::to_string(nodeId);
        out += ind + "for (var " + v + " = 0; " + v + " < " + arg("1") + "; " + v + " += 1) {\n";
        int b = VsExecTarget(g, nodeId, 0); if (b >= 0) VsEmitChain(g, b, out, ind + "    ", visited);
        out += ind + "}\n";
    }
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
        out += ind + "if (" + v + " < " + arg("1") + ") {\n" + ind + "    " + v + " += 1\n";
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
    else if (t=="Toggle")        out += ind + arg("x") + " = !" + arg("x") + "\n";
    else if (t=="SetPositionVec"){ std::string p = in(0, "vec3(0, 0, 0)"); out += ind + "transform.localPosition = " + p + "\n"; }
    else if (t=="MoveBy")        out += ind + "transform.localPosition = transform.localPosition + " + in(0, "vec3(0, 0, 0)") + "\n";
    else if (t=="Rotate3")       out += ind + "rotate3(" + in(0)+", "+in(1)+", "+in(2)+")\n";
    else if (t=="SetRotation3")  out += ind + "set_rot3(" + in(0)+", "+in(1)+", "+in(2)+")\n";
    else if (t=="SetColor")      out += ind + "set_color(" + in(0,"1")+", "+in(1,"1")+", "+in(2,"1")+", "+in(3,"1")+")\n";
    else if (t=="PlayClip")      out += ind + "play_clip(" + q(arg("")) + ")\n";
    else if (t=="SetTimeScale")  out += ind + "set_timescale(" + in(0, "1") + ")\n";
    else if (t=="SetBlendShape") out += ind + "blend_shape(" + q(arg("")) + ", " + in(0, "1") + ")\n";
    else if (t=="NetHost")       out += ind + "net_host(" + arg("45000") + ")\n";
    else if (t=="NetJoin") {
        std::string h = arg("127.0.0.1"), port = "45000";
        size_t cut = h.find_first_of(" :");
        if (cut != std::string::npos) { port = h.substr(cut + 1); h = h.substr(0, cut); }
        out += ind + "net_join(" + q(h) + ", " + port + ")\n";
    }
    else if (t=="NetDisconnect") out += ind + "net_disconnect()\n";
    else if (t=="NetSend")       out += ind + "net_send(" + q(arg("")) + ", str(" + in(0, "\"\"") + "))\n";
    else if (t=="NetSetVar")     out += ind + "net_set(" + q(arg("x")) + ", str(" + in(0, "\"\"") + "))\n";
    else if (t=="NetChat")       out += ind + "net_chat(" + in(0, "\"\"") + ")\n";
    else if (t=="NetRpc")        out += ind + "net_rpc(" + q(arg("")) + ", str(" + in(0, "\"\"") + "))\n";
    else if (t=="NetSpawnOwned") out += ind + "net_spawn_owned(" + q(arg("")) + ", transform.position.x, transform.position.y, transform.position.z)\n";
    else if (t=="NetDespawn")    out += ind + "net_despawn(" + in(0, "\"\"") + ")\n";
    else if (t=="NetReady")      out += ind + "net_ready(" + in(0, "true") + ")\n";
    else if (t=="NetStartMatch") out += ind + "net_start_match()\n";
    else if (t=="SteamUnlock")   out += ind + "steam_unlock(" + q(arg("")) + ")\n";
    else if (t=="SteamSetStat")  out += ind + "steam_set_stat(" + q(arg("stat")) + ", " + in(0) + ")\n";
    else if (t=="SteamStore")    out += ind + "steam_store()\n";
    else if (t=="SteamLeaderboard") out += ind + "steam_leaderboard(" + q(arg("")) + ", " + in(0) + ")\n";
    else if (t=="Timer") out += ind + "// Every (" + arg("1") + "s): wire actions to this node\n";
    else if (t=="Wait")  out += ind + "// Wait (" + arg("1") + "s): use after(t, fn) for a delay\n";
    else                 out += ind + "// unsupported node: " + t + "\n";
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
            "Branch", "Sequence", "Once", "ForLoop", "Repeat", "FlipFlop", "Gate", "DoN", "SwitchInt"};
        if (kSelfContinued.count(n->type)) break;
        cur = VsExecTarget(g, cur, 0);
    }
}
inline std::string CompileToOkayScript(const VsEditGraph& g) {
    // C#-style output: the graph becomes a component class; its events become
    // Start / Update / OnCollisionEnter / OnTriggerEnter / OnMouseDown methods.
    std::string cls = "VisualGraph";
    if (!g.path.empty()) {
        std::string stem = std::filesystem::path(g.path).stem().string(), id;
        for (char c : stem) id += (std::isalnum((unsigned char)c) || c == '_') ? c : '_';
        if (!id.empty() && !std::isdigit((unsigned char)id[0])) cls = id;
    }
    std::string out = "// Generated from the visual graph. Plain C#-style OkayScript:\n"
                      "// edit it freely, or keep wiring nodes; both drive the same runtime.\n"
                      "using UnityEngine;\n\n"
                      "public class " + cls + " : MonoBehaviour\n{\n";
    int startNode = -1, updateNode = -1;
    for (const auto& n : g.nodes) { if (n.type=="OnStart") startNode=n.id; if (n.type=="OnUpdate") updateNode=n.id; }
    std::string fields;
    for (const auto& n : g.nodes) {
        if (n.type=="Once")     fields += "    bool _once_"+std::to_string(n.id)+" = false;\n";
        if (n.type=="FlipFlop") fields += "    bool _ff_"+std::to_string(n.id)+" = false;\n";
        if (n.type=="DoN")      fields += "    int _don_"+std::to_string(n.id)+" = 0;\n";
    }
    if (!fields.empty()) out += fields + "\n";
    bool first = true;
    auto method = [&](const std::string& sig, int entry) {
        if (!first) out += "\n";
        first = false;
        out += "    " + sig + "\n    {\n";
        std::set<int> v; int s = VsExecTarget(g, entry, 0); if (s >= 0) VsEmitChain(g, s, out, "        ", v);
        out += "    }\n";
    };
    if (startNode >= 0)  method("void Start()", startNode);
    if (updateNode >= 0) method("void Update()", updateNode);
    // Collision / trigger / click events become the matching handlers.
    static const std::pair<const char*, const char*> kEvents[] = {
        {"OnCollision", "void OnCollisionEnter(Collision other)"},
        {"OnTrigger",   "void OnTriggerEnter(Collider other)"},
        {"OnClick",     "void OnMouseDown()"}};
    bool anyEvent = false;
    for (const auto& ev : kEvents)
        for (const auto& n : g.nodes)
            if (n.type == ev.first) { anyEvent = true; method(ev.second, n.id); break; }
    // Clip events: one OnAnimEvent() handler; named nodes check anim_event.
    {
        bool open = false;
        for (const auto& n : g.nodes) {
            if (n.type != "OnAnimEvent") continue;
            if (!open) { out += std::string(first ? "" : "\n") + "    void on_anim_event()\n    {\n"; open = true; anyEvent = true; first = false; }
            std::set<int> v; int s = VsExecTarget(g, n.id, 0);
            if (n.arg.empty()) { if (s >= 0) VsEmitChain(g, s, out, "        ", v); }
            else {
                out += "        if (anim_event == \"" + n.arg + "\")\n        {\n";
                if (s >= 0) VsEmitChain(g, s, out, "            ", v);
                out += "        }\n";
            }
        }
        if (open) out += "    }\n";
    }
    if (startNode < 0 && updateNode < 0 && !anyEvent)
        out += "    // Add an On Start / On Update event node to generate runnable code.\n";
    out += "}\n";
    // C# statement terminators inside method bodies (8+ spaces of indent).
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
        fin += line; if (e < out.size()) fin += '\n';
        a = e + 1;
    }
    return fin;
}

// ---- Interaction state -----------------------------------------------------
struct VsEditState {
    VsEditGraph g;
    ImVec2 pan{40, 40};
    int    selected = -1;
    // Pending wire drag: a pin the user grabbed.
    bool   dragging = false;
    bool   dragExec = false;   // true = exec pin, false = data pin
    bool   dragFromOutput = false;
    int    dragNode = -1, dragPin = -1;
    bool   openPalette = false;
    ImVec2 paletteWorld{0, 0};
    bool   showScript = false;   // "View as OkayScript" side panel open
    float  zoom = 1.0f;          // canvas zoom (mouse wheel / toolbar), 0.4 .. 2
    bool   frameAll = false;     // fit every node in view on the next frame
};

inline VsEditState& State() { static VsEditState s; return s; }

// Optional log sink (the host wires this to its console); no hard dependency on
// the host's logging so this header can be included at global scope.
inline void (*LogFn)(const std::string&, int) = nullptr;
inline void Log(const std::string& m, int lv = 0) { if (LogFn) LogFn(m, lv); }

// Load a .okayvs into the editor's live graph (used to open one on double-click).
inline void OpenFile(const std::string& path) { State().g.load(path); State().selected = -1; }

// Pin geometry helpers (screen positions computed during draw).
struct PinKey { int node; int kind; int side; int index; };  // kind:0 exec,1 data; side:0 in,1 out

// ---- The editor window -----------------------------------------------------
// Returns nothing; manages its own state. `ed` is used for "Attach to selected".
inline void Draw(EditorState& ed, bool* open) {
    ImGui::SetNextWindowSize(ImVec2(980, 600), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Visual Script", open)) { ImGui::End(); return; }
    // A floating window remembered tiny (or squashed to its toolbar) has no room
    // for the canvas: give it a usable size.
    if (!ImGui::IsWindowDocked() && (ImGui::GetWindowSize().y < 300.0f || ImGui::GetWindowSize().x < 500.0f))
        ImGui::SetWindowSize(ImVec2(std::max(ImGui::GetWindowSize().x, 980.0f), 600.0f));
    VsEditState& st = State();
    VsEditGraph& g = st.g;

    // Toolbar.
    if (ImGui::Button("New")) { g.clear(); st.selected = -1; }
    ImGui::SameLine();
    const bool ctrlS = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::GetIO().KeyCtrl &&
                       ImGui::IsKeyPressed(ImGuiKey_S, false);
    if (ImGui::Button("Save") || ctrlS) {
        std::string p = g.path;
        if (p.empty()) {
            // default into the project Assets folder.
            std::string dir = ed.projectDir();
            p = (dir.empty() ? std::string("Script") : dir + "/Assets/Script") + ".okayvs";
        }
        if (g.save(p)) {
            // Objects already using this graph pick the change up right away.
            int n = 0;
            for (const auto& up : ed.scene().Objects())
                for (auto* vc : up->GetComponents<VisualScriptComponent>())
                    if (vc->Path() == p) { vc->Reload(); ++n; }
            Log("Saved visual script " + p + (n ? " (updated " + std::to_string(n) + " object(s))" : std::string()));
        } else Log("Save failed: " + p, 2);
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!ed.selected());
    if (ImGui::Button("Attach to Selected")) {
        if (GameObject* o = ed.selected()) {
            // Save what's on screen first: attaching loads the file, and an unsaved
            // edit would otherwise silently not be what runs.
            std::string p = g.path;
            if (p.empty()) {
                std::string dir = ed.projectDir();
                p = (dir.empty() ? std::string("Script") : dir + "/Assets/" + o->name + "_vs") + ".okayvs";
            }
            if (!g.save(p)) Log("Couldn't save " + p, 2);
            for (const auto& up : ed.scene().Objects())   // others using the file follow the edit
                for (auto* other : up->GetComponents<VisualScriptComponent>())
                    if (other->Path() == p) other->Reload();
            auto* vc = o->GetComponent<VisualScriptComponent>();
            if (!vc) vc = o->AddComponent<VisualScriptComponent>();
            vc->LoadFromFile(g.path);
            ed.dirty = true;
            Log("Attached visual script to " + o->name);
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button(st.showScript ? "Hide Script" : "View as OkayScript")) st.showScript = !st.showScript;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Compile this graph to OkayScript — author with either front-end");
    ImGui::SameLine();
    if (ImGui::Button("Open...")) ImGui::OpenPopup("##vsopen");
    if (ImGui::BeginPopup("##vsopen")) {
        // Every graph in the project's Assets.
        namespace fs = std::filesystem;
        fs::path assets = ed.projectDir().empty() ? fs::path("Assets") : fs::path(ed.projectDir()) / "Assets";
        std::error_code ec; int shown = 0;
        if (fs::exists(assets, ec))
            for (auto& e : fs::recursive_directory_iterator(assets, ec)) {
                if (!e.is_regular_file() || e.path().extension() != ".okayvs") continue;
                ++shown;
                if (ImGui::MenuItem(fs::relative(e.path(), assets, ec).string().c_str())) { g.load(e.path().string()); st.selected = -1; }
            }
        if (!shown) ImGui::TextDisabled("No .okayvs graphs in Assets yet.");
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%s%s", g.path.empty() ? "(unsaved)" : g.path.c_str(), g.dirty ? " *" : "");
    // Zoom controls (wheel over the canvas zooms too; Home frames every node).
    ImGui::SameLine(); ImGui::TextDisabled("|"); ImGui::SameLine();
    if (ImGui::Button(" - ##vszo")) st.zoom = std::max(0.4f, st.zoom / 1.15f);
    ImGui::SameLine();
    ImGui::Text("%d%%", (int)std::lround(st.zoom * 100.0f));
    if (ImGui::IsItemClicked()) st.zoom = 1.0f;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Click to reset to 100%%. Mouse wheel over the graph zooms.");
    ImGui::SameLine();
    if (ImGui::Button(" + ##vszi")) st.zoom = std::min(2.0f, st.zoom * 1.15f);
    ImGui::SameLine();
    if (ImGui::Button("Frame All")) st.frameAll = true;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Fit every node in view (Home)");
    ImGui::SameLine();
    ImGui::TextDisabled("  right-click: add node  \xC2\xB7  drag pins to wire  \xC2\xB7  middle-drag: pan  \xC2\xB7  Del: delete");
    // Lightweight validation: a graph with nodes but no event entry never runs.
    {
        bool hasEvent = false;
        for (const auto& n : g.nodes)
            if (n.type == "OnStart" || n.type == "OnUpdate" || n.type == "OnCollision" || n.type == "OnTrigger" || n.type == "OnClick" || n.type == "OnAnimEvent") { hasEvent = true; break; }
        if (!g.nodes.empty() && !hasEvent) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.95f, 0.72f, 0.40f, 1.0f), "  \xE2\x9A\xA0 add an On Start / On Update node");
        }
    }

    // Canvas (leaves room on the right for the OkayScript preview when open).
    const float scriptPanelW = 380.0f;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.105f, 0.105f, 0.112f, 1.0f));
    ImGui::BeginChild("vscanvas", ImVec2(st.showScript ? -scriptPanelW : 0.0f, 0), true,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoMove);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImVec2 canvasMin = origin, canvasMax = ImVec2(origin.x + ImGui::GetContentRegionAvail().x,
                                                  origin.y + ImGui::GetContentRegionAvail().y);
    // The full-canvas catch-all button handles panning + the right-click palette,
    // but it MUST allow overlap: without this it greedily latches the mouse-press
    // (it's submitted before the nodes), so the pin/header buttons drawn on top of
    // it never activate — that's why wiring nodes together did nothing.
    ImGui::SetNextItemAllowOverlap();
    ImGui::InvisibleButton("vscbg", ImVec2(canvasMax.x - canvasMin.x, canvasMax.y - canvasMin.y),
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
    bool canvasHovered = ImGui::IsItemHovered();
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) st.selected = -1;   // click empty space: deselect
    const bool canvasWindowHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);

    // ---- Zoom: wheel zooms about the mouse; Home / Frame All fits the graph ----
    float& z = st.zoom;
    // Node positions are stored in the original (compact) layout units; spread
    // them by the same factor the nodes grew, so saved graphs keep their gaps.
    const float kLayout = 1.4f;
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    if (canvasWindowHovered && ImGui::GetIO().MouseWheel != 0.0f && !ImGui::IsAnyItemActive() &&
        !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) {
        float wx = (mouse.x - canvasMin.x - st.pan.x) / z, wy = (mouse.y - canvasMin.y - st.pan.y) / z;
        z = std::clamp(z * (ImGui::GetIO().MouseWheel > 0 ? 1.12f : 1.0f / 1.12f), 0.4f, 2.0f);
        st.pan = ImVec2(mouse.x - canvasMin.x - wx * z, mouse.y - canvasMin.y - wy * z);   // (pan math is layout-scale free)
    }
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsKeyPressed(ImGuiKey_Home, false) &&
        !ImGui::IsAnyItemActive()) st.frameAll = true;
    if (st.frameAll) {
        st.frameAll = false;
        if (!g.nodes.empty()) {
            float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
            for (const auto& n : g.nodes) {   // rough node extents in world units
                x0 = std::min(x0, n.x * kLayout); y0 = std::min(y0, n.y * kLayout);
                x1 = std::max(x1, n.x * kLayout + 260.0f); y1 = std::max(y1, n.y * kLayout + 170.0f);
            }
            float cw = canvasMax.x - canvasMin.x - 60.0f, chh = canvasMax.y - canvasMin.y - 60.0f;
            z = std::clamp(std::min(cw / std::max(1.0f, x1 - x0), chh / std::max(1.0f, y1 - y0)), 0.4f, 1.25f);
            st.pan = ImVec2((canvasMax.x - canvasMin.x) * 0.5f - (x0 + x1) * 0.5f * z,
                            (canvasMax.y - canvasMin.y) * 0.5f - (y0 + y1) * 0.5f * z);
        }
    }
    // Text and widgets follow the zoom.
    ImGui::SetWindowFontScale(z);
    ImFont* font = ImGui::GetFont();
    const float fs = ImGui::GetFontSize();

    // Grid: minor lines + a major line every 4 cells.
    const float grid = 28.0f * z;
    const ImU32 gridMinor = IM_COL32(255, 255, 255, 9), gridMajor = IM_COL32(255, 255, 255, 22);
    {
        int i = (int)std::floor(-st.pan.x / grid);
        for (float x = std::fmod(st.pan.x, grid); x < canvasMax.x - canvasMin.x; x += grid, ++i)
            dl->AddLine(ImVec2(canvasMin.x + x, canvasMin.y), ImVec2(canvasMin.x + x, canvasMax.y), (i % 4 == 0) ? gridMajor : gridMinor);
        int j = (int)std::floor(-st.pan.y / grid);
        for (float y = std::fmod(st.pan.y, grid); y < canvasMax.y - canvasMin.y; y += grid, ++j)
            dl->AddLine(ImVec2(canvasMin.x, canvasMin.y + y), ImVec2(canvasMax.x, canvasMin.y + y), (j % 4 == 0) ? gridMajor : gridMinor);
    }

    // Pan with middle drag (anywhere on the canvas).
    if (canvasWindowHovered && ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
        st.pan.x += ImGui::GetIO().MouseDelta.x; st.pan.y += ImGui::GetIO().MouseDelta.y;
    }

    auto W2S = [&](float wx, float wy) { return ImVec2(canvasMin.x + st.pan.x + wx * kLayout * z, canvasMin.y + st.pan.y + wy * kLayout * z); };

    // Compute pin screen positions this frame.
    std::map<long long, ImVec2> pinPos;
    auto pk = [](int node, int kind, int side, int idx) -> long long {
        return ((long long)node << 20) | ((long long)kind << 18) | ((long long)side << 16) | idx;
    };

    // Roomy layout (world units, scaled by the zoom).
    const float NODE_W_BASE = 220.0f * z, HEADER_H = 32.0f * z, ROW_H = 28.0f * z, PIN_R = 6.5f * z;
    const float BOX_W = 110.0f * z;   // inline value box on an unconnected data input
    const float PAD = 12.0f * z, ROUND = 8.0f * z;
    auto inputLinked = [&](int node, int pin) {
        for (const auto& l : g.dataLinks) if (l.consumer == node && l.inPin == pin) return true;
        return false;
    };
    auto text = [&](ImVec2 p, ImU32 c, const char* t) { dl->AddText(font, fs, p, c, t); };

    // Wires go in a background channel so they run BEHIND the nodes.
    dl->ChannelsSplit(2);
    dl->ChannelsSetCurrent(1);

    // Lay out + draw each node.
    int pendingDelete = -1, pendingDup = -1;
    for (auto& n : g.nodes) {
        const VsDef* d = FindDef(n.type);
        if (!d) continue;
        int leftRows  = (d->execIn ? 1 : 0) + (int)d->dataIn.size();
        int rightRows = (int)d->execOut.size() + (int)d->dataOut.size();
        int rows = std::max(leftRows, rightRows);
        float bodyH = rows * ROW_H + (d->param ? ROW_H + 8.0f * z : 0) + PAD;
        float h = HEADER_H + bodyH;
        // Wider when it has data inputs: their value boxes sit between the input
        // labels and the output labels.
        float inLabelW = 0.0f, outLabelW = 0.0f;
        for (const char* lbl : d->dataIn) inLabelW = std::max(inLabelW, ImGui::CalcTextSize(lbl).x);
        for (const char* lbl : d->dataOut) outLabelW = std::max(outLabelW, ImGui::CalcTextSize(lbl).x);
        for (const char* lbl : d->execOut) outLabelW = std::max(outLabelW, ImGui::CalcTextSize(lbl).x);
        float titleW = ImGui::CalcTextSize(d->title).x + PAD * 2.0f;
        const float NODE_W = std::max({NODE_W_BASE, titleW,
            d->dataIn.empty() ? 0.0f : PAD + inLabelW + 8.0f * z + BOX_W + PAD + outLabelW + PAD});
        ImVec2 p0 = W2S(n.x, n.y), p1 = ImVec2(p0.x + NODE_W, p0.y + h);
        const bool hovered = ImGui::IsMouseHoveringRect(p0, p1) && canvasWindowHovered;

        // Shadow, body, header.
        dl->AddRectFilled(ImVec2(p0.x + 3 * z, p0.y + 5 * z), ImVec2(p1.x + 3 * z, p1.y + 5 * z), IM_COL32(0, 0, 0, 90), ROUND);
        dl->AddRectFilled(p0, p1, IM_COL32(38, 39, 44, 245), ROUND);
        ImU32 hc = CatColor(d->cat);
        dl->AddRectFilled(p0, ImVec2(p1.x, p0.y + HEADER_H), hc, ROUND, ImDrawFlags_RoundCornersTop);
        dl->AddLine(ImVec2(p0.x, p0.y + HEADER_H), ImVec2(p1.x, p0.y + HEADER_H), IM_COL32(0, 0, 0, 120), 1.0f);
        if (st.selected == n.id) {
            dl->AddRect(ImVec2(p0.x - 2 * z, p0.y - 2 * z), ImVec2(p1.x + 2 * z, p1.y + 2 * z), IM_COL32(120, 170, 255, 255), ROUND + 2 * z, 0, 2.5f);
        } else {
            dl->AddRect(p0, p1, hovered ? IM_COL32(150, 154, 166, 200) : IM_COL32(0, 0, 0, 170), ROUND, 0, hovered ? 1.5f : 1.0f);
        }
        text(ImVec2(p0.x + PAD, p0.y + (HEADER_H - fs) * 0.5f), IM_COL32(244, 246, 250, 255), d->title);

        // Node drag handle: an invisible button over the header.
        ImGui::SetCursorScreenPos(p0);
        ImGui::PushID(n.id);
        ImGui::InvisibleButton("##hdr", ImVec2(NODE_W, HEADER_H));
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            n.x += ImGui::GetIO().MouseDelta.x / (z * kLayout); n.y += ImGui::GetIO().MouseDelta.y / (z * kLayout); g.dirty = true;
        }
        if (ImGui::IsItemClicked()) st.selected = n.id;
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal) && d->cat != CAT_EVENT && !ImGui::IsItemActive())
            ImGui::SetTooltip("%s  (%s)", d->title, d->type);
        if (ImGui::BeginPopupContextItem("##nctx")) {
            // Applied after the loop: the node list mustn't change while it's iterated.
            if (ImGui::MenuItem("Duplicate")) pendingDup = n.id;
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
        auto dataDot = [&](ImVec2 pp, bool linked) {
            dl->AddCircleFilled(pp, PIN_R, linked ? dataCol : IM_COL32(38, 39, 44, 255), 16);
            dl->AddCircle(pp, PIN_R, dataCol, 16, 2.0f * z);
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
            dataDot(pp, linked);
            text(ImVec2(p0.x + PAD, pp.y - fs * 0.5f), labelCol, d->dataIn[i]);
            pinPos[pk(n.id, 1, 0, i)] = pp;
            // Nothing wired in: type the value right here (text, number, true/false).
            if (!linked) {
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(5 * z, 3 * z));
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4 * z);
                const float boxH = fs + 6 * z;
                ImGui::SetCursorScreenPos(ImVec2(p0.x + PAD + inLabelW + 8 * z, pp.y - boxH * 0.5f));
                ImGui::SetNextItemWidth(BOX_W);
                std::string& dv = n.pinDefault[i];
                char vb[128]; std::snprintf(vb, sizeof(vb), "%s", dv.c_str());
                char bid[16]; std::snprintf(bid, sizeof(bid), "##pd%d", i);
                if (ImGui::InputTextWithHint(bid, "value", vb, sizeof(vb))) { dv = vb; g.dirty = true; }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s: type a value (text, a number, true/false), or wire a node in", d->dataIn[i]);
                ImGui::PopStyleVar(2);
                if (dv.empty()) n.pinDefault.erase(i);
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
            bool used = false;
            for (const auto& l : g.dataLinks) if (l.producer == n.id && l.outPin == i) { used = true; break; }
            dataDot(pp, used);
            ImVec2 ts = ImGui::CalcTextSize(d->dataOut[i]);
            text(ImVec2(p1.x - PAD - ts.x, pp.y - fs * 0.5f), labelCol, d->dataOut[i]);
            pinPos[pk(n.id, 1, 1, i)] = pp;
        }

        // Param editor (inline) in the body.
        if (d->param) {
            float py = p0.y + HEADER_H + PAD * 0.5f + std::max(leftRows, rightRows) * ROW_H + 4 * z;
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6 * z, 4 * z));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4 * z);
            ImGui::SetCursorScreenPos(ImVec2(p0.x + PAD, py));
            ImGui::SetNextItemWidth(NODE_W - PAD * 2);
            char buf[96]; std::snprintf(buf, sizeof(buf), "%s", n.arg.c_str());
            if (d->param == 2) {   // compare op combo
                const char* ops[] = {">", "<", ">=", "<=", "==", "!="};
                if (ImGui::BeginCombo("##op", n.arg.c_str())) {
                    for (const char* o : ops) if (ImGui::Selectable(o, n.arg == o)) { n.arg = o; g.dirty = true; }
                    ImGui::EndCombo();
                }
            } else if (IsKeyNode(n.type)) {
                if (KeyPicker(n.arg, NODE_W - PAD * 2)) g.dirty = true;
            } else if (n.type == "MouseButton" || n.type == "MouseButtonDown") {
                const char* btns[] = {"Left button", "Right button", "Middle button"};
                int b = std::atoi(n.arg.c_str());
                if (n.arg == "right") b = 1; else if (n.arg == "middle") b = 2;
                if (b < 0 || b > 2) b = 0;
                if (ImGui::Combo("##mbtn", &b, btns, 3)) { n.arg = std::to_string(b); g.dirty = true; }
            } else {
                if (ImGui::InputTextWithHint("##arg", d->paramHint ? d->paramHint : "", buf, sizeof(buf)))
                    { n.arg = buf; g.dirty = true; }
            }
            ImGui::PopStyleVar(2);
        }

        // Start a wire from a pin: an invisible button over each pin (generous hit area).
        auto pinButton = [&](long long key, int kind, int side, int idx) {
            auto it = pinPos.find(key); if (it == pinPos.end()) return;
            ImVec2 pp = it->second;
            const float hit = std::max(18.0f, 22.0f * z);
            ImGui::SetCursorScreenPos(ImVec2(pp.x - hit * 0.5f, pp.y - hit * 0.5f));
            char id[24]; std::snprintf(id, sizeof(id), "p%d%d%d", kind, side, idx);
            ImGui::InvisibleButton(id, ImVec2(hit, hit));
            if (ImGui::IsItemActivated()) {
                st.dragging = true; st.dragExec = (kind == 0);
                st.dragFromOutput = (side == 1); st.dragNode = n.id; st.dragPin = idx;
            }
            if (ImGui::IsItemHovered())
                dl->AddCircle(pp, PIN_R + 3 * z, IM_COL32(255, 255, 255, 210), 18, 2.0f);
        };
        if (d->execIn) pinButton(pk(n.id, 0, 0, 0), 0, 0, 0);
        for (int i = 0; i < (int)d->dataIn.size(); ++i)  pinButton(pk(n.id, 1, 0, i), 1, 0, i);
        for (int i = 0; i < (int)d->execOut.size(); ++i) pinButton(pk(n.id, 0, 1, i), 0, 1, i);
        for (int i = 0; i < (int)d->dataOut.size(); ++i) pinButton(pk(n.id, 1, 1, i), 1, 1, i);

        ImGui::PopID();
    }

    if (pendingDup >= 0) {
        if (const VsEditNode* src = g.find(pendingDup)) {
            VsEditNode copy = *src;
            int c = g.add(copy.type, copy.x + 30.0f, copy.y + 30.0f);
            if (VsEditNode* cn = g.find(c)) { cn->arg = copy.arg; cn->pinDefault = copy.pinDefault; }
            st.selected = c;
        }
    }
    if (pendingDelete >= 0) { g.removeNode(pendingDelete); if (st.selected == pendingDelete) st.selected = -1; }

    // Wires, behind the nodes. Ones touching the selected node are highlighted.
    dl->ChannelsSetCurrent(0);
    auto bezier = [&](ImVec2 a, ImVec2 b, ImU32 col, float w) {
        float dx = std::max(50.0f * z, std::fabs(b.x - a.x) * 0.5f);
        dl->AddBezierCubic(a, ImVec2(a.x + dx, a.y), ImVec2(b.x - dx, b.y), b, col, w);
    };
    for (const auto& l : g.execLinks) {
        auto a = pinPos.find(pk(l.from, 0, 1, l.pin)), b = pinPos.find(pk(l.to, 0, 0, 0));
        if (a == pinPos.end() || b == pinPos.end()) continue;
        const bool hot = st.selected == l.from || st.selected == l.to;
        bezier(a->second, b->second, hot ? IM_COL32(255, 255, 255, 255) : IM_COL32(215, 216, 222, 200), (hot ? 3.6f : 2.8f) * std::max(0.7f, z));
    }
    for (const auto& l : g.dataLinks) {
        auto a = pinPos.find(pk(l.producer, 1, 1, l.outPin)), b = pinPos.find(pk(l.consumer, 1, 0, l.inPin));
        if (a == pinPos.end() || b == pinPos.end()) continue;
        const bool hot = st.selected == l.producer || st.selected == l.consumer;
        bezier(a->second, b->second, hot ? IM_COL32(150, 240, 170, 255) : IM_COL32(110, 190, 130, 210), (hot ? 3.4f : 2.6f) * std::max(0.7f, z));
    }
    dl->ChannelsSetCurrent(1);

    // Live drag wire + release handling.
    if (st.dragging) {
        auto from = pinPos.find(pk(st.dragNode, st.dragExec ? 0 : 1, st.dragFromOutput ? 1 : 0, st.dragPin));
        if (from != pinPos.end()) {
            ImVec2 m = ImGui::GetIO().MousePos;
            bezier(st.dragFromOutput ? from->second : m, st.dragFromOutput ? m : from->second,
                   IM_COL32(255, 255, 255, 170), 2.6f * std::max(0.7f, z));
        }
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            // Find a pin under the mouse of the opposite side + same kind.
            ImVec2 m = ImGui::GetIO().MousePos;
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
                    if (st.dragFromOutput) g.linkExec(st.dragNode, st.dragPin, bestNode);
                    else                   g.linkExec(bestNode, bestIdx, st.dragNode);
                } else {
                    if (st.dragFromOutput) g.linkData(bestNode, bestIdx, st.dragNode, st.dragPin);
                    else                   g.linkData(st.dragNode, st.dragPin, bestNode, bestIdx);
                }
            }
            st.dragging = false;
        }
    }
    dl->ChannelsMerge();
    ImGui::SetWindowFontScale(1.0f);   // popups / overlay text at normal size

    // Right-click empty canvas: node palette.
    if (canvasHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && !st.dragging) {
        st.openPalette = true;
        st.paletteWorld = ImVec2((ImGui::GetIO().MousePos.x - canvasMin.x - st.pan.x) / (z * kLayout),
                                 (ImGui::GetIO().MousePos.y - canvasMin.y - st.pan.y) / (z * kLayout));
        ImGui::OpenPopup("##vspalette");
    }
    if (ImGui::BeginPopup("##vspalette")) {
        static const char* catNames[CAT_COUNT] = {"Events","Flow","Variables","Math","Logic","Input","Transform","Actions","Data","Multiplayer & Steam"};
        // Type to search every node by title, type name or category; Enter adds
        // the first match.
        static char search[64] = "";
        if (ImGui::IsWindowAppearing()) { search[0] = '\0'; ImGui::SetKeyboardFocusHere(); }
        ImGui::SetNextItemWidth(240);
        const bool enter = ImGui::InputTextWithHint("##vssearch", "Search nodes...", search, sizeof search,
                                                    ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::Separator();
        auto lower = [](std::string x) { for (char& ch : x) ch = (char)std::tolower((unsigned char)ch); return x; };
        const std::string needle = lower(search);
        if (!needle.empty()) {
            int shown = 0;
            for (const auto& d : Registry()) {
                const std::string hay = lower(std::string(d.title) + " " + d.type + " " + catNames[d.cat]);
                if (hay.find(needle) == std::string::npos) continue;
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                ImGui::TextUnformatted(catNames[d.cat]);
                ImGui::PopStyleColor();
                ImGui::SameLine(150);
                if (ImGui::Selectable(d.title, shown == 0 && enter) || (shown == 0 && enter)) {
                    st.selected = g.add(d.type, st.paletteWorld.x, st.paletteWorld.y);
                    ImGui::CloseCurrentPopup();
                }
                if (++shown >= 40) break;
            }
            if (shown == 0) ImGui::TextDisabled("No node matches \"%s\"", search);
            ImGui::EndPopup();
        } else {
        for (int c = 0; c < CAT_COUNT; ++c) {
            bool any = false;
            for (const auto& d : Registry()) if (d.cat == c) { any = true; break; }
            if (!any) continue;
            if (ImGui::BeginMenu(catNames[c])) {
                for (const auto& d : Registry())
                    if (d.cat == c && ImGui::MenuItem(d.title))
                        st.selected = g.add(d.type, st.paletteWorld.x, st.paletteWorld.y);
                ImGui::EndMenu();
            }
        }
        ImGui::EndPopup();
        }
    }

    // Delete selected node with the Del key when the canvas is focused.
    if (st.selected >= 0 && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
        ImGui::IsKeyPressed(ImGuiKey_Delete)) {
        g.removeNode(st.selected); st.selected = -1;
    }

    if (g.nodes.empty())
        dl->AddText(ImVec2(canvasMin.x + 18, canvasMin.y + 16), IM_COL32(150, 152, 160, 255),
                    "Right-click to add a node. Start with an On Start or On Update event.");

    ImGui::EndChild();
    ImGui::PopStyleColor();

    // ---- OkayScript preview panel -----------------------------------------
    // Live compile of the graph to OkayScript, side by side with the canvas, so
    // the two front-ends stay legibly equivalent. Read-only here (the graph is
    // the source of truth); Copy drops it into the clipboard to paste into the
    // text script editor.
    if (st.showScript) {
        ImGui::SameLine();
        ImGui::BeginChild("vsscript", ImVec2(0, 0), true);
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

    ImGui::End();
}

} // namespace okay::editor::vsed
