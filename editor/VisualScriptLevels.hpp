#pragma once
// How hard a node is, for the Add Node menu. Basic nodes are the ones a first
// script needs. Advanced nodes are the specialist tools. Everything else is
// Intermediate. docs/visual_scripting_naming.md is the rule for new nodes,
// including the nodes added on the logic-runtime branch (PR 601).
//
// The field on a registry row is VsDef::level. Registry() fills it from
// VsLevelForNode / VsLevelForApi, so a row can omit the field and still get
// the right level. A node on the logic-runtime branch is listed in
// kSharedLevels below; that is the list both branches share.
#include <cstring>

namespace okay::editor::vsed {

enum class VsLevel : int { Basic = 0, Intermediate = 1, Advanced = 2 };

enum class VsLevelFilter : int { All = 0, Basic = 1, Intermediate = 2, Advanced = 3 };

inline const char* VsLevelName(VsLevel level) {
    switch (level) {
        case VsLevel::Basic: return "Basic";
        case VsLevel::Intermediate: return "Intermediate";
        case VsLevel::Advanced: return "Advanced";
    }
    return "Intermediate";
}

// Short label drawn on the row. The tooltip uses the full name.
inline const char* VsLevelBadge(VsLevel level) {
    switch (level) {
        case VsLevel::Basic: return "Basic";
        case VsLevel::Intermediate: return "Inter";
        case VsLevel::Advanced: return "Adv";
    }
    return "Inter";
}

inline const char* VsLevelFilterName(VsLevelFilter filter) {
    switch (filter) {
        case VsLevelFilter::Basic: return "Basic";
        case VsLevelFilter::Intermediate: return "Intermediate";
        case VsLevelFilter::Advanced: return "Advanced";
        default: return "All";
    }
}

inline bool VsLevelPasses(VsLevel level, VsLevelFilter filter) {
    if (filter == VsLevelFilter::All) return true;
    return static_cast<int>(level) == static_cast<int>(filter) - 1;
}

// Search: the closest match first, then the easier node, then the one used most recently.
// recentIndex is 0 for the newest, and a large number when it has never been used.
inline bool VsSearchRanksBefore(int scoreA, VsLevel levelA, int recentA,
                                int scoreB, VsLevel levelB, int recentB) {
    if (scoreA != scoreB) return scoreA > scoreB;
    if (levelA != levelB) return static_cast<int>(levelA) < static_cast<int>(levelB);
    return recentA < recentB;
}

// Inside one topic: the subcategory, then Basic before Intermediate before Advanced.
inline bool VsBrowseRanksBefore(int subA, VsLevel levelA, int rankA,
                                int subB, VsLevel levelB, int rankB) {
    if (subA != subB) return subA < subB;
    if (levelA != levelB) return static_cast<int>(levelA) < static_cast<int>(levelB);
    return rankA < rankB;
}

inline bool VsLevelIs(const char* type, const char* name) {
    return type && name && std::strcmp(type, name) == 0;
}
inline bool VsLevelPre(const char* type, const char* prefix) {
    return type && prefix && std::strncmp(type, prefix, std::strlen(prefix)) == 0;
}

// Nodes the logic-runtime branch adds. Both branches read this table.
struct VsSharedLevel { const char* type; VsLevel level; };
inline const VsSharedLevel kSharedLevels[] = {
    {"OnInteract", VsLevel::Basic}, {"OnDeath", VsLevel::Basic}, {"OnAction", VsLevel::Basic},
    {"OnActionHeld", VsLevel::Basic}, {"OnActionUp", VsLevel::Basic}, {"GetMoveAxis", VsLevel::Basic},
    {"Collect", VsLevel::Basic}, {"PickUp", VsLevel::Basic}, {"Drop", VsLevel::Basic},
    {"DoorOpen", VsLevel::Basic}, {"DoorClose", VsLevel::Basic}, {"Lever", VsLevel::Basic},
    {"Heal", VsLevel::Basic}, {"Damage", VsLevel::Basic}, {"Win", VsLevel::Basic}, {"Lose", VsLevel::Basic},
    {"ScoreAdd", VsLevel::Basic}, {"ScoreGet", VsLevel::Basic}, {"ScoreSet", VsLevel::Basic},
    {"RestartScene", VsLevel::Basic}, {"ShowFloatingText", VsLevel::Basic}, {"Countdown", VsLevel::Basic},
    {"CheckpointSave", VsLevel::Basic}, {"CheckpointLoad", VsLevel::Basic}, {"IsDead", VsLevel::Basic},
    {"SpawnEvery", VsLevel::Intermediate}, {"TweenPosition", VsLevel::Intermediate},
    {"TweenRotation", VsLevel::Intermediate}, {"TweenScale", VsLevel::Intermediate},
    {"TweenColor", VsLevel::Intermediate}, {"SwitchString", VsLevel::Intermediate},
    {"SwitchCases", VsLevel::Intermediate}, {"LatchGate", VsLevel::Intermediate},
    {"FollowPath", VsLevel::Intermediate}, {"Patrol", VsLevel::Intermediate}, {"Wander", VsLevel::Intermediate},
    {"Chase", VsLevel::Intermediate}, {"Flee", VsLevel::Intermediate}, {"OrbitAround", VsLevel::Intermediate},
    {"SmoothFollow", VsLevel::Intermediate}, {"RotateTowards", VsLevel::Intermediate}, {"Spin", VsLevel::Intermediate},
    {"Bob", VsLevel::Intermediate}, {"Shoot", VsLevel::Intermediate}, {"Knockback", VsLevel::Intermediate},
    {"Ammo", VsLevel::Intermediate}, {"Respawn", VsLevel::Intermediate}, {"SpawnProjectile", VsLevel::Intermediate},
    {"DestroyAfter", VsLevel::Intermediate}, {"PlaySoundAt", VsLevel::Intermediate}, {"StopSound", VsLevel::Intermediate},
    {"StopAnimation", VsLevel::Intermediate}, {"CrossfadeClip", VsLevel::Intermediate}, {"WaitAnimEnd", VsLevel::Intermediate},
    {"PoolGet", VsLevel::Intermediate}, {"PoolRelease", VsLevel::Intermediate},
    {"NewMap", VsLevel::Intermediate},
    {"MapGet", VsLevel::Intermediate}, {"MapSet", VsLevel::Intermediate},
    {"MapHas", VsLevel::Intermediate}, {"MapRemove", VsLevel::Intermediate}, {"MapKeys", VsLevel::Intermediate},
    {"MapValues", VsLevel::Intermediate}, {"ForEachMap", VsLevel::Intermediate}, {"ShuffleList", VsLevel::Intermediate},
    {"ListMin", VsLevel::Intermediate}, {"ListMax", VsLevel::Intermediate}, {"RandomPick", VsLevel::Intermediate},
    {"RandomPoint", VsLevel::Intermediate}, {"OnCollisionTag", VsLevel::Intermediate},
    {"OnHealthChanged", VsLevel::Intermediate}, {"OnTagEnter", VsLevel::Intermediate},
    {"OnTagExit", VsLevel::Intermediate}, {"OnTagStay", VsLevel::Intermediate},
    {"CanSee", VsLevel::Intermediate}, {"Proximity", VsLevel::Intermediate}, {"IsValid", VsLevel::Intermediate},
    {"SwitchCamera", VsLevel::Intermediate}, {"SetCameraTarget", VsLevel::Intermediate}, {"Zoom", VsLevel::Intermediate},
    {"FadeScreen", VsLevel::Intermediate}, {"SetProgress", VsLevel::Intermediate},
    {"CallScript", VsLevel::Advanced}, {"RunGraph", VsLevel::Advanced},
};

inline int VsSharedLevelCount() { return (int)(sizeof kSharedLevels / sizeof kSharedLevels[0]); }

// Levels for the logic-round nodes. Kept beside kSharedLevels so that shared
// list stays the 84 types both branches already agree on.
inline const VsSharedLevel kLogicLevels[] = {
#include "generated/library_levels.inc"
#include "generated/gplib_levels.inc"
};

// The list / map / struct / colour nodes (declared variables work). Their own table so the shared one stays at 84.
inline const VsSharedLevel kCollectionLevels[] = {
    {"ListInsert", VsLevel::Intermediate}, {"ListSet", VsLevel::Intermediate}, {"ListFind", VsLevel::Intermediate},
    {"ListRemoveItem", VsLevel::Intermediate}, {"ListSort", VsLevel::Intermediate}, {"ListReverse", VsLevel::Intermediate},
    {"ListClear", VsLevel::Intermediate}, {"ListCopy", VsLevel::Intermediate}, {"MapClear", VsLevel::Intermediate},
    {"MapCount", VsLevel::Intermediate}, {"MapCopy", VsLevel::Intermediate},
    {"ListAppend", VsLevel::Intermediate}, {"ListSlice", VsLevel::Intermediate}, {"ListPop", VsLevel::Intermediate},
    {"MapGetOr", VsLevel::Intermediate}, {"MapMerge", VsLevel::Intermediate}, {"StructNew", VsLevel::Advanced},
    {"StructGet", VsLevel::Advanced}, {"StructSet", VsLevel::Advanced}, {"MakeColor", VsLevel::Intermediate},
    {"SplitColor", VsLevel::Intermediate},
};

inline int VsLogicLevelCount() { return (int)(sizeof kLogicLevels / sizeof kLogicLevels[0]); }

inline const VsLevel* VsLogicLevel(const char* type) {
    if (!type) return nullptr;
    for (const VsSharedLevel& s : kLogicLevels)
        if (std::strcmp(s.type, type) == 0) return &s.level;
    for (const VsSharedLevel& s : kCollectionLevels)
        if (std::strcmp(s.type, type) == 0) return &s.level;
    return nullptr;
}

// The everyday nodes. A first script is built from these.
inline const char* kBasicTypes[] = {
    "OnStart", "OnUpdate", "OnKey", "OnKeyUp", "OnClick", "OnCollision", "OnTrigger",
    "Branch", "Sequence", "Once", "Delay", "Timer", "Gate",
    "GetVar", "SetVar", "AddVar", "Toggle", "Const",
    "Add", "Sub", "Mul", "Div", "Min", "Max", "Abs", "Clamp", "Random", "RandomRange", "RandomInt",
    "Compare", "Equals", "And", "Or", "Not",
    "Concat", "ToString",
    "Print", "Spawn", "Destroy", "SetActive", "SetVisible", "PlaySound",
    "GetX", "GetY", "GetZ", "GetPosition", "SetPosition", "Translate", "Rotate", "LookAt", "MoveTowards",
    "Key", "KeyDown", "AxisX", "AxisY", "MouseButton",
    "SetText", "ShowUI", "HideUI",
    "Time", "DeltaTime",
    "LoadScene", "PauseGame",
    "Self", "GetName", "GetTag",
};

// The first screen of the menu: the nodes a beginner reaches for. All of these are Basic.
inline const char* kStarterNodes[] = {
    "OnStart", "OnUpdate", "OnKey",
    "Branch", "Sequence", "Delay",
    "GetVar", "SetVar", "AddVar", "Const",
    "Add", "Compare", "RandomRange",
    "Print", "Spawn", "Destroy", "SetActive",
    "GetPosition", "SetPosition", "Translate",
    "Key", "PlaySound", "SetText", "ShowUI",
    "DeltaTime", "PauseGame",
};

inline int VsStarterCount() { return (int)(sizeof kStarterNodes / sizeof kStarterNodes[0]); }

inline int VsStarterIndex(const char* type) {
    if (!type) return -1;
    for (int i = 0; i < VsStarterCount(); ++i)
        if (std::strcmp(kStarterNodes[i], type) == 0) return i;
    return -1;
}

inline bool VsIsStarter(const char* type) { return VsStarterIndex(type) >= 0; }

inline VsLevel VsLevelForNode(const char* type) {
    if (!type || !type[0]) return VsLevel::Intermediate;
    for (const VsSharedLevel& s : kSharedLevels)
        if (std::strcmp(s.type, type) == 0) return s.level;
    for (const char* b : kBasicTypes)
        if (std::strcmp(b, type) == 0) return VsLevel::Basic;
    // Specialist tools: online play, anti-cheat, behavior trees, IK, build mode, cutscenes.
    if (VsLevelPre(type, "Net") || VsLevelPre(type, "Steam") || VsLevelPre(type, "SyncVar") ||
        VsLevelPre(type, "Bt") || VsLevelPre(type, "Ik") || VsLevelPre(type, "OnNet") ||
        VsLevelPre(type, "OnCheat") || VsLevelPre(type, "OnValidate") || VsLevelPre(type, "Reject") ||
        VsLevelIs(type, "ServerBudget") || VsLevelPre(type, "EnterBuild") || VsLevelPre(type, "ExitBuild") ||
        VsLevelIs(type, "InBuildMode") || VsLevelIs(type, "SetBuildPalette") ||
        VsLevelIs(type, "SaveCreation") || VsLevelIs(type, "LoadCreation") ||
        VsLevelIs(type, "PlaySequence") || VsLevelIs(type, "StopSequence") || VsLevelIs(type, "OnSequenceEvent") ||
        VsLevelIs(type, "Vault") || VsLevelIs(type, "Mantle") || VsLevelIs(type, "LedgeGrab") ||
        VsLevelIs(type, "ClimbUp") || VsLevelIs(type, "LetGo") || VsLevelIs(type, "Shimmy") ||
        VsLevelIs(type, "GetProperty") || VsLevelIs(type, "SetProperty") ||
        VsLevelIs(type, "CallMethod") || VsLevelIs(type, "GetComponent") ||
        VsLevelIs(type, "AddComponent") || VsLevelIs(type, "RunSnippet"))
        return VsLevel::Advanced;
    if (VsLevelIs(type, "NotAnd") || VsLevelIs(type, "NotOr") || VsLevelIs(type, "AllTrue") ||
        VsLevelIs(type, "AnyTrue") || VsLevelIs(type, "IsEven") || VsLevelIs(type, "IsOdd") ||
        VsLevelIs(type, "PickByIndex") || VsLevelIs(type, "FirstValid") || VsLevelIs(type, "IsInState") ||
        VsLevelIs(type, "GetState") || VsLevelIs(type, "SetState"))
        return VsLevel::Basic;
    if (VsLevelIs(type, "IsSlightly") || VsLevelIs(type, "IsMostly") || VsLevelIs(type, "IsVery") ||
        VsLevelIs(type, "PickBest") || VsLevelIs(type, "InsideArea") || VsLevelIs(type, "CountNearby") ||
        VsLevelIs(type, "EaseToward"))
        return VsLevel::Basic;
    if (VsLevelIs(type, "TryFirst") || VsLevelIs(type, "AllSucceed") || VsLevelIs(type, "FlipOutcome") ||
        VsLevelIs(type, "AlwaysSucceed") || VsLevelIs(type, "RepeatUntilFail") || VsLevelIs(type, "WaitBetween") ||
        VsLevelIs(type, "SucceedNow") || VsLevelIs(type, "FailNow") || VsLevelIs(type, "CheckOutcome"))
        return VsLevel::Advanced;
    if (const VsLevel* logic = VsLogicLevel(type)) return *logic;
    return VsLevel::Intermediate;
}

inline VsLevel VsLevelForApi(const char* apiType, const char* member) {
    if (!apiType) apiType = "";
    if (!member) member = "";
    if (VsLevelIs(apiType, "BehaviorTree") || VsLevelIs(apiType, "IK")) return VsLevel::Advanced;
    if (VsLevelIs(apiType, "Debug")) return VsLevel::Basic;
    if (VsLevelIs(apiType, "Time") || VsLevelIs(apiType, "Audio")) return VsLevel::Basic;
    if (VsLevelIs(apiType, "GameObject")) {
        if (VsLevelIs(member, "name") || VsLevelIs(member, "tag") || VsLevelIs(member, "active") ||
            VsLevelIs(member, "Find") || VsLevelIs(member, "FindWithTag"))
            return VsLevel::Basic;
        return VsLevel::Intermediate;
    }
    if (VsLevelIs(apiType, "Transform")) {
        if (VsLevelIs(member, "position") || VsLevelIs(member, "Translate") || VsLevelIs(member, "Rotate") ||
            VsLevelIs(member, "LookAt"))
            return VsLevel::Basic;
        return VsLevel::Intermediate;
    }
    if (VsLevelIs(apiType, "Mathf")) {
        if (VsLevelIs(member, "Abs") || VsLevelIs(member, "Min") || VsLevelIs(member, "Max") ||
            VsLevelIs(member, "Clamp") || VsLevelIs(member, "Lerp"))
            return VsLevel::Basic;
        return VsLevel::Intermediate;
    }
    if (VsLevelIs(apiType, "Input")) {
        if (VsLevelPre(member, "GetKey") || VsLevelIs(member, "GetAxis")) return VsLevel::Basic;
        return VsLevel::Intermediate;
    }
    return VsLevel::Intermediate;
}

inline VsLevel VsLevelForStep(const char* op, const char* group) {
    if (!op) op = "";
    if (!group) group = "";
    if (VsLevelPre(op, "net_") || VsLevelPre(op, "steam_") || VsLevelPre(op, "playfab_") ||
        VsLevelIs(op, "achievement") || VsLevelIs(group, "Multiplayer") ||
        VsLevelIs(group, "Steam") || VsLevelIs(group, "PlayFab") || VsLevelIs(group, "Game"))
        return VsLevel::Advanced;
    static const char* basic[] = {
        "set_var", "add_var", "toggle_var", "move", "set_pos", "play_sound", "set_text", "log",
        "wait", "destroy", "set_active", "spawn", "look_at", "rotate", "key", "key_down", "always",
        "set_visible", "pause", "load_scene",
    };
    for (const char* b : basic) if (std::strcmp(op, b) == 0) return VsLevel::Basic;
    return VsLevel::Intermediate;
}

// A tiny example, only where a beginner benefits from seeing one. Empty when there is nothing to add.
inline const char* VsNodeExample(const char* type) {
    if (!type) return nullptr;
    struct Ex { const char* type; const char* text; };
    static const Ex k[] = {
        {"OnStart", "Turn a light on when the level loads."},
        {"OnUpdate", "Move forward a little every frame."},
        {"OnKey", "Press E to open a door."},
        {"Branch", "If health is under 10, play a warning."},
        {"Sequence", "Play a sound, then show a message."},
        {"Delay", "Wait 2 seconds, then spawn the next enemy."},
        {"GetVar", "Read score so a text object can show it."},
        {"SetVar", "Set score to 0 when the game starts."},
        {"AddVar", "Add 1 to score when the coin is picked up."},
        {"Const", "Type 10, and wire it into a damage node."},
        {"Add", "Add the bonus onto the score."},
        {"Compare", "True when health is below 10."},
        {"RandomRange", "A hit for somewhere between 5 and 12."},
        {"Print", "Print health, to check the script is running."},
        {"Spawn", "Create a coin at this object's position."},
        {"Destroy", "Remove the coin after it is picked up."},
        {"SetActive", "Hide the door until the key is found."},
        {"GetPosition", "Read where the player is, then aim at that point."},
        {"LbApproach", "Ease a health bar from where it is toward the new value."},
        {"LbIsPositive", "True once the score climbs above zero."},
        {"LbHasText", "True when the player typed a name."},
        {"LbIsBlinking", "A warning light that is on for half a second, then off."},
        {"LbIsQuestDone", "True when every step of the quest is finished."},
        {"LbCouldSpend", "True when the player can afford the sword."},
        {"LbRunAndCheck", "Open the door, and only then play the sound, if the door opened."},
        {"LbTimeLeft", "How many seconds are left on a ten second fuse."},
        {"SetPosition", "Put the player back at the start."},
        {"Translate", "Nudge the platform 1 unit to the right."},
        {"Key", "True while W is held, so the player keeps walking."},
        {"PlaySound", "Play the click when a button is pressed."},
        {"SetText", "Show \"Score: 10\" on the text object."},
        {"ShowUI", "Show the pause menu."},
        {"DeltaTime", "Multiply speed by this so movement stays even."},
        {"PauseGame", "Freeze the game when Escape is pressed."},
    };
    for (const Ex& e : k) if (std::strcmp(e.type, type) == 0) return e.text;
    return nullptr;
}

} // namespace okay::editor::vsed
