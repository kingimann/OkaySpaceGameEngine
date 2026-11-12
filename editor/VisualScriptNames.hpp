#pragma once
// Friendly names for visual-script nodes. The file token stays in the registry;
// this table is the title, the pins, and the old words search still matches.
// Rules: docs/visual_scripting_naming.md.
#include "VisualScriptNodeDocs.hpp"   // VsLower
#include <cctype>
#include <cstring>
#include <string>
#include <vector>

namespace okay::editor::vsed {

enum class VsNameKind { Event, Action, Question, Reading, Calculation };

struct VsName {
    const char* type;
    VsNameKind  kind;
    const char* title;
    const char* oldTitle;   // previous title; also a search keyword
    const char* extra;      // more search keywords (old jargon, the token's words)
    const char* const* inPins;
    int inCount;
    const char* const* outPins;
    int outCount;
};

inline const char* const kPinValuePrevious[] = {"Value", "Previous"};
inline const char* const kPinCondition[] = {"Condition"};
inline const char* const kPinTimes[] = {"Times"};
inline const char* const kExecFirstSecond[] = {"First", "Second"};
inline const char* const kExecCases[] = {"Zero", "One", "Two", "Default"};
inline const char* const kExecNamedCases[] = {"Case One", "Case Two", "Case Three", "Case Four", "Default"};
inline const char* const kExecLever[] = {"Opened", "Closed"};
inline const char* const kPinCenterRadiusSpeed[] = {"Center", "Radius", "Speed"};
inline const char* const kPinTweenColor[] = {"Red", "Green", "Blue", "Opacity", "Seconds"};
inline const char* const kPinShoot[] = {"Distance", "Damage"};
inline const char* const kPinSmallest[] = {"Smallest"};
inline const char* const kPinLargest[] = {"Largest"};
inline const char* const kPinCall[] = {"First", "Second", "Third", "Fourth", "Method"};
inline const char* const kPinComponent[] = {"Object", "Found"};
inline const char* const kPinZoom[] = {"Amount"};
inline const char* const kPinNearby[] = {"Nearby"};
inline const char* const kPinVisible[] = {"Visible"};
inline const char* const kPinDirection[] = {"Direction"};
inline const char* const kPinRemaining[] = {"Remaining"};

// Events. oldTitle is what the palette used to say.
inline const VsName kVsNames[] = {
    {"OnStart",          VsNameKind::Event, "On Start",              "On Start",              "", nullptr, 0, nullptr, 0},
    {"OnUpdate",         VsNameKind::Event, "On Every Frame",        "On Update",             "Update frame", nullptr, 0, nullptr, 0},
    {"OnLateUpdate",     VsNameKind::Event, "On Late Update",        "On Late Update",        "On After Every Frame late", nullptr, 0, nullptr, 0},
    {"OnFixedUpdate",    VsNameKind::Event, "On Physics Update",     "On Fixed Update",       "On Steady Tick fixed timestep physics", nullptr, 0, nullptr, 0},
    {"OnEnable",         VsNameKind::Event, "On Enabled",            "On Enable",             "On Becomes Active enabled active", nullptr, 0, nullptr, 0},
    {"OnDisable",        VsNameKind::Event, "On Disabled",           "On Disable",            "On Becomes Inactive disabled inactive", nullptr, 0, nullptr, 0},
    {"OnDestroy",        VsNameKind::Event, "On Destroyed",          "On Destroy",            "destroy", nullptr, 0, nullptr, 0},
    {"OnTimer",          VsNameKind::Event, "On Every Few Seconds",  "On Timer",              "timer seconds", nullptr, 0, nullptr, 0},
    {"OnKey",            VsNameKind::Event, "On Key Pressed",        "On Key Pressed",        "", nullptr, 0, nullptr, 0},
    {"OnKeyUp",          VsNameKind::Event, "On Key Released",       "On Key Released",       "", nullptr, 0, nullptr, 0},
    {"OnClick",          VsNameKind::Event, "On Click",              "On Click",              "", nullptr, 0, nullptr, 0},
    {"OnMouseDown",      VsNameKind::Event, "On Mouse Pressed",      "On Mouse Down",         "mousedown", nullptr, 0, nullptr, 0},
    {"OnMouseUp",        VsNameKind::Event, "On Mouse Released",     "On Mouse Up",           "mouseup", nullptr, 0, nullptr, 0},
    {"OnMouseEnter",     VsNameKind::Event, "On Mouse Entered",      "On Mouse Enter",        "", nullptr, 0, nullptr, 0},
    {"OnMouseExit",      VsNameKind::Event, "On Mouse Left",         "On Mouse Exit",         "exit", nullptr, 0, nullptr, 0},
    {"OnMouseOver",      VsNameKind::Event, "On Mouse Hover",        "On Mouse Over",         "hover over", nullptr, 0, nullptr, 0},
    {"OnCollision",      VsNameKind::Event, "On Collision Start",    "On Collision",          "On Bump hit start", nullptr, 0, nullptr, 0},
    {"OnCollisionStay",  VsNameKind::Event, "On Collision Stay",     "On Collision Stay",     "On Still Bumping bump stay", nullptr, 0, nullptr, 0},
    {"OnCollisionExit",  VsNameKind::Event, "On Collision End",      "On Collision Exit",     "On Bump Ended end exit", nullptr, 0, nullptr, 0},
    {"OnTrigger",        VsNameKind::Event, "On Trigger Enter",      "On Trigger",            "On Entered Area enter", nullptr, 0, nullptr, 0},
    {"OnTriggerStay",    VsNameKind::Event, "On Trigger Stay",       "On Trigger Stay",       "On Still Inside Area stay", nullptr, 0, nullptr, 0},
    {"OnTriggerExit",    VsNameKind::Event, "On Trigger Exit",       "On Trigger Exit",       "On Left Area exit", nullptr, 0, nullptr, 0},
    {"OnAnimEvent",      VsNameKind::Event, "On Animation Marker",   "On Anim Event",         "anim animation", nullptr, 0, nullptr, 0},
    {"OnEvent",          VsNameKind::Event, "On Message",            "On Custom Event",       "custom event", nullptr, 0, nullptr, 0},
    {"OnCustomEvent",    VsNameKind::Event, "On Event Raised",       "On Typed Event",        "custom event handler typed parameters declared", nullptr, 0, nullptr, 0},
    {"OnObjectPlaced",   VsNameKind::Event, "On Object Placed",      "On Object Placed",      "", nullptr, 0, nullptr, 0},
    {"OnObjectRemoved",  VsNameKind::Event, "On Object Removed",     "On Object Removed",     "", nullptr, 0, nullptr, 0},
    {"OnLogicSignal",    VsNameKind::Event, "On Signal",             "On Logic Signal",       "logic", nullptr, 0, nullptr, 0},
    {"OnItemAdded",      VsNameKind::Event, "On Item Added",         "On Item Added",         "", nullptr, 0, nullptr, 0},
    {"OnItemRemoved",    VsNameKind::Event, "On Item Removed",       "On Item Removed",       "", nullptr, 0, nullptr, 0},
    {"OnInventoryFull",  VsNameKind::Event, "On Inventory Full",     "On Inventory Full",     "", nullptr, 0, nullptr, 0},
    {"OnItemUsed",       VsNameKind::Event, "On Item Used",          "On Item Used",          "", nullptr, 0, nullptr, 0},
    {"OnEquip",          VsNameKind::Event, "On Item Equipped",      "On Equip",              "equip", nullptr, 0, nullptr, 0},
    {"OnCraft",          VsNameKind::Event, "On Item Crafted",       "On Craft",              "craft", nullptr, 0, nullptr, 0},
    {"OnSequenceEvent",  VsNameKind::Event, "On Cutscene Marker",    "On Sequence Event",     "sequence cutscene", nullptr, 0, nullptr, 0},
    {"OnDialogueEvent",  VsNameKind::Event, "On Conversation Line",  "On Dialogue Event",     "dialogue", nullptr, 0, nullptr, 0},
    {"OnDialogueEnd",    VsNameKind::Event, "On Conversation Ended", "On Dialogue End",       "dialogue", nullptr, 0, nullptr, 0},
    {"OnQuestStarted",   VsNameKind::Event, "On Quest Started",      "On Quest Started",      "", nullptr, 0, nullptr, 0},
    {"OnQuestCompleted", VsNameKind::Event, "On Quest Completed",    "On Quest Completed",    "", nullptr, 0, nullptr, 0},
    {"OnQuestFailed",    VsNameKind::Event, "On Quest Failed",       "On Quest Failed",       "", nullptr, 0, nullptr, 0},
    {"OnObjectiveCompleted", VsNameKind::Event, "On Objective Completed", "On Objective Completed", "", nullptr, 0, nullptr, 0},
    {"OnQuestEvent",     VsNameKind::Event, "On Quest Message",      "On Quest Event",        "", nullptr, 0, nullptr, 0},
    {"OnCheatDetected",  VsNameKind::Event, "On Cheat Detected",     "On Cheat Detected",     "", nullptr, 0, nullptr, 0},
    {"OnValidateRpc",    VsNameKind::Event, "On Network Action",     "On Validate Rpc",       "rpc validate", nullptr, 0, nullptr, 0},
    {"OnValidateHit",    VsNameKind::Event, "On Hit Check",          "On Validate Hit",       "validate hit", nullptr, 0, nullptr, 0},
    {"OnLanguageChanged",VsNameKind::Event, "On Language Changed",   "On Language Changed",   "", nullptr, 0, nullptr, 0},
    {"OnVarChanged",     VsNameKind::Event, "On Value Changed",      "On Var Changed",        "var variable", nullptr, 0, kPinValuePrevious, 2},
    {"OnSceneVarChanged",VsNameKind::Event, "On Scene Value Changed","On Scene Var Changed",  "var variable scene", nullptr, 0, kPinValuePrevious, 2},
    {"OnGlobalVarChanged", VsNameKind::Event, "On Game Value Changed", "On Global Var Changed", "var variable global", nullptr, 0, kPinValuePrevious, 2},
    // Flow. oldTitle is the previous palette title, including the unit in brackets.
    {"Branch",        VsNameKind::Action, "Branch",                 "Branch",            "", kPinCondition, 1, nullptr, 0},
    {"Sequence",      VsNameKind::Action, "Sequence",               "Sequence",          "", nullptr, 0, nullptr, 0},
    {"Once",          VsNameKind::Action, "Run Once",               "Once",              "", nullptr, 0, nullptr, 0},
    {"Timer",         VsNameKind::Action, "Every Seconds",          "Every (s)",         "Timer seconds", nullptr, 0, nullptr, 0},
    {"Wait",          VsNameKind::Action, "Wait Once",              "Wait (s)",          "Wait seconds", nullptr, 0, nullptr, 0},
    {"Delay",         VsNameKind::Action, "Wait Seconds",           "Delay (s)",         "Delay pause seconds", nullptr, 0, nullptr, 0},
    {"Cooldown",      VsNameKind::Action, "Cooldown",               "Cooldown (s)",      "seconds", nullptr, 0, nullptr, 0},
    {"ForLoop",       VsNameKind::Action, "For Loop",               "For Loop",          "", nullptr, 0, nullptr, 0},
    {"Repeat",        VsNameKind::Action, "Repeat",                 "Repeat N",          "count", nullptr, 0, nullptr, 0},
    {"FlipFlop",      VsNameKind::Action, "Flip Flop",              "Flip-Flop",         "A B toggle", nullptr, 0, nullptr, 0},
    {"Gate",          VsNameKind::Action, "Gate",                   "Gate",              "", nullptr, 0, nullptr, 0},
    {"DoN",           VsNameKind::Action, "Run a Number of Times", "Do N Times",        "DoN limit", nullptr, 0, nullptr, 0},
    {"SwitchInt",     VsNameKind::Action, "Switch Number",          "Switch (int)",      "switch int case", nullptr, 0, nullptr, 0},
    {"ChanceBranch",  VsNameKind::Action, "Random Branch",          "Random Branch",     "Chance", nullptr, 0, nullptr, 0},
    {"Counter",       VsNameKind::Action, "Counter",                "Counter",           "", nullptr, 0, nullptr, 0},
    {"EveryN",        VsNameKind::Action, "Every Number of Times",  "Every N Times",     "EveryN", kPinTimes, 1, nullptr, 0},
    {"FunctionStart", VsNameKind::Action, "Define Function",        "Function",          "", nullptr, 0, nullptr, 0},
    {"Return",        VsNameKind::Action, "Return",                 "Return",            "", nullptr, 0, nullptr, 0},
    {"CallFunction",  VsNameKind::Action, "Call Function",          "Call Function",     "", nullptr, 0, nullptr, 0},
    {"RaiseEvent",    VsNameKind::Action, "Raise Event",            "Raise Event",       "send custom event typed parameters message", nullptr, 0, nullptr, 0},
    {"CallDispatcher", VsNameKind::Action, "Call Dispatcher",       "Call Dispatcher",   "event dispatcher delegate broadcast", nullptr, 0, nullptr, 0},
    {"BindEvent",     VsNameKind::Action, "Bind to Dispatcher",     "Bind Event",        "delegate subscribe listen add handler", nullptr, 0, nullptr, 0},
    {"UnbindEvent",   VsNameKind::Action, "Unbind Dispatcher", "Unbind Event",      "delegate unsubscribe stop listening remove handler", nullptr, 0, nullptr, 0},
    {"While",         VsNameKind::Action, "While",                  "While",             "", kPinCondition, 1, nullptr, 0},
    {"ForEach",       VsNameKind::Action, "For Each",               "For Each",          "", nullptr, 0, nullptr, 0},
    {"Break",         VsNameKind::Action, "Break Loop",             "Break",             "", nullptr, 0, nullptr, 0},
    {"Continue",      VsNameKind::Action, "Continue Loop",          "Continue",          "", nullptr, 0, nullptr, 0},
    // Nodes added on the logic-runtime branch. File tokens stay; these are the titles.
    {"OnInteract",       VsNameKind::Event, "On Interact",            "On Interact",            "interact", nullptr, 0, nullptr, 0},
    {"OnTagEnter",       VsNameKind::Event, "On Tag Enter",           "On Tag Enter",           "tag enter", nullptr, 0, nullptr, 0},
    {"OnTagStay",        VsNameKind::Event, "On Tag Stay",            "On Tag Stay",            "tag stay", nullptr, 0, nullptr, 0},
    {"OnTagExit",        VsNameKind::Event, "On Tag Exit",            "On Tag Exit",            "tag exit", nullptr, 0, nullptr, 0},
    {"OnDeath",          VsNameKind::Event, "On Death",               "On Death",               "death", nullptr, 0, nullptr, 0},
    {"OnHealthChanged",  VsNameKind::Event, "On Health Changed",      "On Health Changed",      "health", nullptr, 0, nullptr, 0},
    {"OnAction",         VsNameKind::Event, "On Action Pressed",      "On Action Pressed",      "action interact jump fire", nullptr, 0, nullptr, 0},
    {"OnActionUp",       VsNameKind::Event, "On Action Released",     "On Action Released",     "action", nullptr, 0, nullptr, 0},
    {"OnActionHeld",     VsNameKind::Event, "On Action Held",         "On Action Held",         "action", nullptr, 0, nullptr, 0},
    {"OnCollisionTag",   VsNameKind::Event, "On Collision With Tag",  "On Collision With Tag",  "tag collision", nullptr, 0, nullptr, 0},
    {"WaitAnimEnd",      VsNameKind::Action, "Wait for Animation End", "Wait For Animation End", "anim animation", nullptr, 0, nullptr, 0},
    {"Countdown",        VsNameKind::Action, "Count Down",            "Countdown",              "seconds", nullptr, 0, nullptr, 0},
    {"SwitchString",     VsNameKind::Action, "Switch on Name",        "Switch (name)",          "switch string case", nullptr, 0, nullptr, 0},
    {"SwitchCases",      VsNameKind::Action, "Switch on Number",      "Switch (cases)",         "switch case", nullptr, 0, nullptr, 0},
    {"LatchGate",        VsNameKind::Action, "Latch Gate",            "Latch Gate",             "", nullptr, 0, nullptr, 0},
    {"ForEachMap",       VsNameKind::Action, "For Each in Map",       "For Each Map",           "map", nullptr, 0, nullptr, 0},
    {"RunGraph",         VsNameKind::Action, "Run Another Graph",     "Run Graph",              "graph", nullptr, 0, nullptr, 0},
    {"RotateTowards",    VsNameKind::Action, "Turn Towards",          "Rotate Towards",         "rotate", nullptr, 0, nullptr, 0},
    {"OrbitAround",      VsNameKind::Action, "Orbit Around Center",   "Orbit Around",           "orbit centre center", kPinCenterRadiusSpeed, 3, nullptr, 0},
    {"FollowPath",       VsNameKind::Action, "Follow Path",           "Follow Path",            "", nullptr, 0, nullptr, 0},
    {"Bob",              VsNameKind::Action, "Move Upward and Downward", "Bob",                "bob up down", nullptr, 0, nullptr, 0},
    {"Spin",             VsNameKind::Action, "Spin",                  "Spin",                   "", nullptr, 0, nullptr, 0},
    {"Knockback",        VsNameKind::Action, "Knock Back",            "Knockback",              "", nullptr, 0, nullptr, 0},
    {"SmoothFollow",     VsNameKind::Action, "Follow Smoothly",       "Smooth Follow",          "", nullptr, 0, nullptr, 0},
    {"TweenPosition",    VsNameKind::Action, "Tween Position",        "Tween Position",         "", nullptr, 0, nullptr, 0},
    {"TweenRotation",    VsNameKind::Action, "Tween Rotation",        "Tween Rotation",         "", nullptr, 0, nullptr, 0},
    {"TweenScale",       VsNameKind::Action, "Tween Scale",           "Tween Scale",            "", nullptr, 0, nullptr, 0},
    {"TweenColor",       VsNameKind::Action, "Tween Color",           "Tween Color",            "rgba", kPinTweenColor, 5, nullptr, 0},
    {"CrossfadeClip",    VsNameKind::Action, "Crossfade Animation",   "Crossfade Animation",    "anim clip", nullptr, 0, nullptr, 0},
    {"StopAnimation",    VsNameKind::Action, "Stop Animation",        "Stop Animation",         "anim", nullptr, 0, nullptr, 0},
    {"PickUp",           VsNameKind::Action, "Pick up",               "Pick Up",                "", nullptr, 0, nullptr, 0},
    {"Drop",             VsNameKind::Action, "Drop",                  "Drop",                   "", nullptr, 0, nullptr, 0},
    {"DoorOpen",         VsNameKind::Action, "Open Door",             "Door Open",              "", nullptr, 0, nullptr, 0},
    {"DoorClose",        VsNameKind::Action, "Close Door",            "Door Close",             "", nullptr, 0, nullptr, 0},
    {"Lever",            VsNameKind::Action, "Flip Lever",            "Lever",                  "", nullptr, 0, nullptr, 0},
    {"Collect",          VsNameKind::Action, "Collect",               "Collect",                "", nullptr, 0, nullptr, 0},
    {"Damage",           VsNameKind::Action, "Apply Damage",          "Damage",                 "", nullptr, 0, nullptr, 0},
    {"Heal",             VsNameKind::Action, "Heal",                  "Heal",                   "", nullptr, 0, nullptr, 0},
    {"Respawn",          VsNameKind::Action, "Respawn",               "Respawn",                "", nullptr, 0, nullptr, 0},
    {"SpawnProjectile",  VsNameKind::Action, "Spawn Projectile",      "Spawn Projectile",       "prefab", nullptr, 0, nullptr, 0},
    {"Shoot",            VsNameKind::Action, "Shoot",                 "Shoot",                  "max dist", kPinShoot, 2, nullptr, 0},
    {"Ammo",             VsNameKind::Action, "Use Ammo",              "Ammo",                   "", nullptr, 0, kPinRemaining, 1},
    {"Patrol",           VsNameKind::Action, "Patrol Waypoints",      "Patrol Waypoints",       "", nullptr, 0, nullptr, 0},
    {"Chase",            VsNameKind::Action, "Chase Target",          "Chase Target",           "", nullptr, 0, nullptr, 0},
    {"Flee",             VsNameKind::Action, "Flee From Threat",      "Flee",                   "", nullptr, 0, nullptr, 0},
    {"Wander",           VsNameKind::Action, "Wander Around",         "Wander",                 "", nullptr, 0, nullptr, 0},
    {"SpawnEvery",       VsNameKind::Action, "Spawn Every Few Seconds", "Spawn Every",          "prefab", nullptr, 0, nullptr, 0},
    {"DestroyAfter",     VsNameKind::Action, "Destroy After Seconds", "Destroy After",          "", nullptr, 0, nullptr, 0},
    {"PoolGet",          VsNameKind::Action, "Take From Pool",        "Pool Get",               "pool", nullptr, 0, nullptr, 0},
    {"PoolRelease",      VsNameKind::Action, "Release to Pool",       "Pool Release",           "pool", nullptr, 0, nullptr, 0},
    {"ScoreAdd",         VsNameKind::Action, "Add Score",             "Score Add",              "", nullptr, 0, nullptr, 0},
    {"ScoreSet",         VsNameKind::Action, "Set Score",             "Score Set",              "", nullptr, 0, nullptr, 0},
    {"Win",              VsNameKind::Action, "Show Win",              "Win",                    "", nullptr, 0, nullptr, 0},
    {"Lose",             VsNameKind::Action, "Show Loss",             "Lose",                   "", nullptr, 0, nullptr, 0},
    {"RestartScene",     VsNameKind::Action, "Restart Scene",         "Restart Scene",          "", nullptr, 0, nullptr, 0},
    {"CheckpointSave",   VsNameKind::Action, "Save Checkpoint",       "Checkpoint Save",        "", nullptr, 0, nullptr, 0},
    {"CheckpointLoad",   VsNameKind::Action, "Load Checkpoint",       "Checkpoint Load",        "", nullptr, 0, nullptr, 0},
    {"SetProgress",      VsNameKind::Action, "Set Progress",          "Set Progress Bar",       "", nullptr, 0, nullptr, 0},
    {"ShowFloatingText", VsNameKind::Action, "Show Floating Text",     "Show Floating Text",      "", nullptr, 0, nullptr, 0},
    {"FadeScreen",       VsNameKind::Action, "Fade Screen",           "Fade Screen",            "", nullptr, 0, nullptr, 0},
    {"SetCameraTarget",  VsNameKind::Action, "Set Camera Target",     "Set Camera Target",      "", nullptr, 0, nullptr, 0},
    {"Zoom",             VsNameKind::Action, "Zoom Camera",           "Zoom",                   "", kPinZoom, 1, nullptr, 0},
    {"SwitchCamera",     VsNameKind::Action, "Switch Camera",         "Switch Camera",          "", nullptr, 0, nullptr, 0},
    {"PlaySoundAt",      VsNameKind::Action, "Play Sound Here",       "Play Sound At",          "", nullptr, 0, nullptr, 0},
    {"StopSound",        VsNameKind::Action, "Stop Sound",            "Stop Sound",             "", nullptr, 0, nullptr, 0},
    {"CallScript",       VsNameKind::Action, "Call Script",           "Call Script",            "", nullptr, 0, nullptr, 0},
    {"SetProperty",      VsNameKind::Action, "Set Property",          "Set Property",           "", nullptr, 0, nullptr, 0},
    {"CallMethod",       VsNameKind::Action, "Call Method",           "Call Method",            "", kPinCall, 5, nullptr, 0},
    {"AddComponent",     VsNameKind::Action, "Add Component",         "Add Component",          "", nullptr, 0, nullptr, 0},
    {"SendMessage",      VsNameKind::Action, "Send Message",          "Send Message",           "", nullptr, 0, nullptr, 0},
    {"Broadcast",        VsNameKind::Action, "Broadcast Message",     "Broadcast",              "", nullptr, 0, nullptr, 0},
    {"RunSnippet",       VsNameKind::Action, "Run Text Snippet",      "Run Snippet",            "", nullptr, 0, nullptr, 0},
    {"Proximity",        VsNameKind::Question, "Is Nearby",           "Proximity",              "", nullptr, 0, kPinNearby, 1},
    {"IsDead",           VsNameKind::Question, "Is Dead",             "Is Dead",                "", nullptr, 0, nullptr, 0},
    {"CanSee",           VsNameKind::Question, "Is Target Visible",   "Can See Target",         "", nullptr, 0, kPinVisible, 1},
    {"ScoreGet",         VsNameKind::Reading, "Get Score",            "Score Get",              "", nullptr, 0, nullptr, 0},
    {"GetMoveAxis",      VsNameKind::Reading, "Get Move Direction",   "Get Move Axis",          "axis", nullptr, 0, kPinDirection, 1},
    {"GetProperty",      VsNameKind::Reading, "Get Property",         "Get Property",           "", nullptr, 0, nullptr, 0},
    {"GetComponent",     VsNameKind::Reading, "Get Component",        "Get Component",          "", nullptr, 0, kPinComponent, 2},
    {"FindByTag",        VsNameKind::Reading, "Get Object by Tag",    "Find By Tag",            "find tag", nullptr, 0, nullptr, 0},
    {"RandomPick",       VsNameKind::Calculation, "Pick Random Item", "Random Pick",            "", nullptr, 0, nullptr, 0},
    {"RandomPoint",      VsNameKind::Calculation, "Pick Random Point", "Random Point In Area", "centre center", nullptr, 0, nullptr, 0},
    {"ShuffleList",      VsNameKind::Calculation, "Shuffle List",     "Shuffle",                "", nullptr, 0, nullptr, 0},
    {"ListMin",          VsNameKind::Calculation, "Find Smallest",    "Min Of List",            "min", nullptr, 0, kPinSmallest, 1},
    {"ListMax",          VsNameKind::Calculation, "Find Largest",     "Max Of List",            "max", nullptr, 0, kPinLargest, 1},
    {"NotAnd",           VsNameKind::Calculation, "Not and",           "Not and",                "nand", nullptr, 0, nullptr, 0},
    {"NotOr",            VsNameKind::Calculation, "Not or",            "Not or",                 "nor", nullptr, 0, nullptr, 0},
    {"AllTrue",          VsNameKind::Calculation, "All True",          "All True",               "", nullptr, 0, nullptr, 0},
    {"AnyTrue",          VsNameKind::Calculation, "Any True",          "Any True",               "", nullptr, 0, nullptr, 0},
    {"IsEven",           VsNameKind::Question,    "Is Even",           "Is Even",                "", nullptr, 0, nullptr, 0},
    {"IsOdd",            VsNameKind::Question,    "Is Odd",            "Is Odd",                 "", nullptr, 0, nullptr, 0},
    {"PickByIndex",      VsNameKind::Calculation, "Pick by Index",     "Pick by Index",          "", nullptr, 0, nullptr, 0},
    {"FirstValid",       VsNameKind::Calculation, "First Valid",       "First Valid",            "coalesce", nullptr, 0, nullptr, 0},
    {"BranchInOrder",    VsNameKind::Action,      "Branch in Order",   "Branch in Order",        "else if", nullptr, 0, nullptr, 0},
    {"DoWhile",          VsNameKind::Action,      "Repeat While",      "Repeat While",           "do while", nullptr, 0, nullptr, 0},
    {"ForStep",          VsNameKind::Action,      "Count With Step",   "Count With Step",        "for step reverse", nullptr, 0, nullptr, 0},
    {"WaitUntil",        VsNameKind::Action,      "Wait Until",        "Wait Until",             "", nullptr, 0, nullptr, 0},
    {"WaitWhile",        VsNameKind::Action,      "Wait While",        "Wait While",             "", nullptr, 0, nullptr, 0},
    {"BecameTrue",       VsNameKind::Action,      "Became True",       "Became True",            "rising edge", nullptr, 0, nullptr, 0},
    {"BecameFalse",      VsNameKind::Action,      "Became False",      "Became False",           "falling edge", nullptr, 0, nullptr, 0},
    {"CountUntil",       VsNameKind::Action,      "Count Until",       "Count Until",            "counter target", nullptr, 0, nullptr, 0},
    {"WeightedBranch",   VsNameKind::Action,      "Weighted Branch",   "Weighted Branch",        "random branch", nullptr, 0, nullptr, 0},
    {"WaitSteady",       VsNameKind::Action,      "Wait Until Steady", "Wait Until Steady",      "debounce", nullptr, 0, nullptr, 0},
    {"StepThrough",      VsNameKind::Action,      "Step Through",      "Step Through",           "multi gate", nullptr, 0, nullptr, 0},
    {"WaitLimit",        VsNameKind::Action,      "Wait With Limit",   "Wait With Limit",        "timeout", nullptr, 0, nullptr, 0},
    {"SetState",         VsNameKind::Action,      "Set State",         "Set State",              "", nullptr, 0, nullptr, 0},
    {"GetState",         VsNameKind::Reading,     "Get State",         "Get State",              "", nullptr, 0, nullptr, 0},
    {"IsInState",        VsNameKind::Question,    "Is in State",       "Is in State",            "enum", nullptr, 0, nullptr, 0},
    {"WhenStateBecomes", VsNameKind::Action,      "When State Becomes","When State Becomes",     "on state enter", nullptr, 0, nullptr, 0},
    {"WhenStateLeaves",  VsNameKind::Action,      "When State Leaves", "When State Leaves",      "on state exit", nullptr, 0, nullptr, 0},
    {"HeldLong",         VsNameKind::Question,    "Has Held Long Enough", "Has Held Long Enough", "held for", nullptr, 0, nullptr, 0},
    {"HappenedNear",     VsNameKind::Question,    "Has Happened Within", "Has Happened Within", "within time", nullptr, 0, nullptr, 0},
    {"EveryWhile",       VsNameKind::Action,      "Every Few Seconds", "Every Few Seconds", "every n while", nullptr, 0, nullptr, 0},
    {"EaseToward",       VsNameKind::Calculation, "Ease Toward", "Ease Toward", "", nullptr, 0, nullptr, 0},
    {"BlendTime",        VsNameKind::Action,      "Blend Over Time", "Blend Over Time", "lerp tween", nullptr, 0, nullptr, 0},
    {"Stopwatch",        VsNameKind::Action,      "Run Stopwatch", "Run Stopwatch", "", nullptr, 0, nullptr, 0},
    {"IsSlightly",       VsNameKind::Question,    "Is Slightly True", "Is Slightly True", "fuzzy", nullptr, 0, nullptr, 0},
    {"IsMostly",         VsNameKind::Question,    "Is Mostly True", "Is Mostly True", "fuzzy", nullptr, 0, nullptr, 0},
    {"IsVery",           VsNameKind::Question,    "Is Very True", "Is Very True", "fuzzy", nullptr, 0, nullptr, 0},
    {"PickBest",         VsNameKind::Calculation, "Pick Best Score", "Pick Best Score", "utility", nullptr, 0, nullptr, 0},
    {"ShuffleBag",       VsNameKind::Calculation, "Draw Without Repeating", "Draw Without Repeating", "shuffle bag", nullptr, 0, nullptr, 0},
    {"ChanceEachSecond", VsNameKind::Calculation, "Chance This Second", "Chance This Second", "probability", nullptr, 0, nullptr, 0},
    {"TryFirst",         VsNameKind::Action,      "Try First", "Try First", "selector behaviour", nullptr, 0, nullptr, 0},
    {"AllSucceed",       VsNameKind::Action,      "All Must Succeed", "All Must Succeed", "sequence behaviour", nullptr, 0, nullptr, 0},
    {"FlipOutcome",      VsNameKind::Action,      "Flip Result", "Flip Result", "inverter", nullptr, 0, nullptr, 0},
    {"AlwaysSucceed",    VsNameKind::Action,      "Always Succeed", "Always Succeed", "succeeder", nullptr, 0, nullptr, 0},
    {"RepeatUntilFail",  VsNameKind::Action,      "Repeat Until Fail", "Repeat Until Fail", "", nullptr, 0, nullptr, 0},
    {"WaitBetween",      VsNameKind::Action,      "Wait Between Tries", "Wait Between Tries", "cooldown decorator", nullptr, 0, nullptr, 0},
    {"SucceedNow",       VsNameKind::Action,      "Succeed Now", "Succeed Now", "", nullptr, 0, nullptr, 0},
    {"FailNow",          VsNameKind::Action,      "Fail Now", "Fail Now", "", nullptr, 0, nullptr, 0},
    {"CheckOutcome",     VsNameKind::Action,      "Check Condition", "Check Condition", "", nullptr, 0, nullptr, 0},
    {"InCone",           VsNameKind::Question,    "Is in Cone", "Is in Cone", "perception", nullptr, 0, nullptr, 0},
    {"LineOfSight",      VsNameKind::Question,    "Has Line of Sight", "Has Line of Sight", "", nullptr, 0, nullptr, 0},
    {"InsideArea",       VsNameKind::Question,    "Is Inside Area", "Is Inside Area", "", nullptr, 0, nullptr, 0},
    {"CountNearby",      VsNameKind::Calculation, "Count Nearby", "Count Nearby", "", nullptr, 0, nullptr, 0},
    {"NearestObject",    VsNameKind::Reading,     "Get Nearest Object", "Get Nearest Object", "", nullptr, 0, nullptr, 0},
    {"FarthestObject",   VsNameKind::Reading,     "Get Farthest Object", "Get Farthest Object", "", nullptr, 0, nullptr, 0},
    {"SortByDistance",   VsNameKind::Calculation, "Sort by Distance", "Sort by Distance", "", nullptr, 0, nullptr, 0},
    {"WhenChanged",      VsNameKind::Action,      "When Value Changes", "When Value Changes", "watch", nullptr, 0, nullptr, 0},
    {"DoUnless",         VsNameKind::Action,      "Run Unless", "Run Unless", "rule", nullptr, 0, nullptr, 0},
    {"RememberValue",    VsNameKind::Action,      "Remember Value", "Remember Value", "save flag", nullptr, 0, nullptr, 0},
    {"RecallValue",      VsNameKind::Reading,     "Get Remembered Value", "Get Remembered Value", "", nullptr, 0, nullptr, 0},
    {"HealthEmpty",      VsNameKind::Action,      "When Health Runs Out", "When Health Runs Out", "", nullptr, 0, nullptr, 0},
    {"HealthFull",       VsNameKind::Action,      "When Health is Full", "When Health is Full", "", nullptr, 0, nullptr, 0},
    {"AddBuff",          VsNameKind::Action,      "Add Timed Buff", "Add Timed Buff", "stat modifier", nullptr, 0, nullptr, 0},
    {"AdvanceObjective", VsNameKind::Action,      "Advance Objective", "Advance Objective", "quest", nullptr, 0, nullptr, 0},
    {"AssertThis",       VsNameKind::Action,      "Assert This", "Assert This", "", nullptr, 0, nullptr, 0},
    {"LogOnce",          VsNameKind::Action,      "Log Once", "Log Once", "", nullptr, 0, nullptr, 0},
    {"DrawDebugLine",    VsNameKind::Action,      "Draw Debug Line", "Draw Debug Line", "", nullptr, 0, nullptr, 0},
#include "generated/library_names.inc"
#include "generated/gplib_names.inc"
};

struct VsExecLabels { const char* type; const char* const* pins; int count; };
inline const VsExecLabels kVsExecLabels[] = {
    {"Sequence",  kExecFirstSecond, 2},
    {"FlipFlop",  kExecFirstSecond, 2},
    {"SwitchInt", kExecCases, 4},
    {"SwitchString", kExecNamedCases, 5},
    {"SwitchCases", kExecNamedCases, 5},
    {"Lever", kExecLever, 2},
};

inline const VsName* FindVsName(const char* type) {
    if (!type) return nullptr;
    for (const VsName& n : kVsNames) if (std::strcmp(n.type, type) == 0) return &n;
    return nullptr;
}

inline std::string VsSearchKeywords(const char* type) {
    const VsName* n = FindVsName(type);
    if (!n) return {};
    std::string s = n->oldTitle ? n->oldTitle : "";
    if (n->extra && n->extra[0]) { if (!s.empty()) s += ' '; s += n->extra; }
    return s;
}

// ---- The style check (docs/visual_scripting_naming.md) ----------------------
inline bool VsBannedWord(const std::string& lower) {
    static const char* k[] = {
        "anim", "api", "bool", "dt", "euler", "float", "fov", "fps", "gameobject", "id", "idx",
        "ik", "int", "max", "min", "nav", "num", "obj", "pos", "prefab",
        "old", "quat", "rad", "rigidbody", "rot", "rpc", "string", "transform",
        "ui", "var", "vec", "vec3",
    };
    for (const char* w : k) if (lower == w) return true;
    return false;
}

inline bool VsSmallWord(const std::string& lower) {
    static const char* k[] = {"a", "and", "at", "by", "for", "in", "is", "of", "on", "or", "the", "to", "up"};
    for (const char* w : k) if (lower == w) return true;
    return false;
}

inline bool VsMajorWord(const std::string& word) {
    if (word.size() < 3) return false;
    if (!std::isupper((unsigned char)word[0])) return false;
    for (std::size_t i = 1; i < word.size(); ++i)
        if (!std::islower((unsigned char)word[i])) return false;
    return true;
}

// Empty string means the title is fine. Otherwise a reason a person can read.
inline std::string LintVsTitle(const char* titleIn, VsNameKind kind) {
    if (!titleIn || !titleIn[0]) return "the name is empty";
    const std::string title = titleIn;
    if (title.front() == ' ' || title.back() == ' ') return "the name has a stray space";
    std::vector<std::string> words;
    std::string cur;
    for (char c : title) {
        if (c == ' ') {
            if (cur.empty()) return "the name has a double space";
            words.push_back(cur); cur.clear();
        } else cur += c;
    }
    if (cur.empty()) return "the name has a stray space";
    words.push_back(cur);
    for (std::size_t i = 0; i < words.size(); ++i) {
        const std::string& w = words[i];
        const std::string low = VsLower(w);
        if (VsBannedWord(low)) return "\"" + w + "\" is jargon or an abbreviation; say it in ordinary words";
        const bool smallWord = VsSmallWord(low);
        bool capSmall = smallWord && !w.empty() && std::isupper((unsigned char)w[0]);
        for (std::size_t k = 1; capSmall && k < w.size(); ++k)
            if (!std::islower((unsigned char)w[k])) capSmall = false;
        if (i == 0) {
            if (!(VsMajorWord(w) || capSmall))
                return "\"" + w + "\" should be a full word starting with a capital";
        } else if (smallWord) {
            if (w != low) return "small word \"" + w + "\" stays lowercase";
        } else if (!VsMajorWord(w)) {
            return "\"" + w + "\" should be a full word (no symbols, digits, or abbreviations)";
        }
    }
    auto starts = [&](const char* p) { return title.rfind(p, 0) == 0; };
    if (kind == VsNameKind::Event && !starts("On ")) return "an event starts with \"On \"";
    if (kind == VsNameKind::Question && !(starts("Is ") || starts("Has "))) return "a yes-or-no value starts with \"Is \" or \"Has \"";
    if (kind == VsNameKind::Reading && !starts("Get ")) return "a reading starts with \"Get \"";
    if (kind == VsNameKind::Action && (starts("On ") || starts("Get ") || starts("Is ") || starts("Has ")))
        return "an action starts with a verb (Play, Wait, Move, Set), not On, Get, Is, or Has";
    return {};
}

inline std::string LintVsPin(const char* pin) {
    if (!pin || !pin[0]) return {};   // an unlabeled flow pin
    return LintVsTitle(pin, VsNameKind::Calculation);
}

} // namespace okay::editor::vsed
