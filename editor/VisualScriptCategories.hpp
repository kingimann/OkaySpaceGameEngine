#pragma once
// Where a node sits in the Add Node menu. Every node has one topic from
// kPaletteCategories. A blank subcategory means it sits directly in that topic.
// docs/visual_scripting_naming.md lists the topics for new nodes (including PR 601).
#include <cctype>
#include <cstring>
#include <string>

namespace okay::editor::vsed {

struct VsPaletteCategory {
    const char* name;
    const char* blurb;
};

// Common topics first. A node may also name a subcategory (Math / Vectors).
inline const VsPaletteCategory kPaletteCategories[] = {
    {"Events",     "Where a script starts: the frame, a key, a collision, a message."},
    {"Flow",       "Order, choices, loops, and waiting."},
    {"Variables",  "Remember a number or a yes/no on this object, the scene, or the save."},
    {"Math",       "Arithmetic, comparisons, vectors, and random numbers."},
    {"Text",       "Words, sentences, and the current language."},
    {"Lists & Maps","Several values kept under one name."},
    {"Objects",    "Find, spawn, show, hide, and name things."},
    {"Movement",   "Position, rotation, scale, and moving toward something."},
    {"Physics",    "Forces, velocity, collisions, triggers, and raycasts."},
    {"Animation",  "Clips, character actions, and inverse kinematics."},
    {"Audio",      "Sound effects and music."},
    {"Input",      "Keys, mouse, and the movement axes."},
    {"Camera",     "Where the player is looking, shake, and zoom."},
    {"UI",         "Text, buttons, and bars drawn on the screen."},
    {"Scene",      "Loading scenes, weather, story, and navigation."},
    {"Time",       "The clock, the frame, and game speed."},
    {"Online",     "Hosting, joining, messages, and Steam."},
    {"Debug",      "Messages for you while building, not for the player."},
    {"Advanced",   "Behavior trees and asking an object for a component."},
};

inline int VsPaletteCategoryCount() { return (int)(sizeof kPaletteCategories / sizeof kPaletteCategories[0]); }

inline int VsPaletteCategoryIndex(const char* name) {
    if (!name) return -1;
    for (int i = 0; i < VsPaletteCategoryCount(); ++i)
        if (std::strcmp(kPaletteCategories[i].name, name) == 0) return i;
    return -1;
}

// Vague buckets that must not appear as a category or a subcategory.
inline bool VsCatchAllCategory(const char* name) {
    if (!name || !name[0]) return false;
    static const char* k[] = {
        "Do something", "Check something", "Do", "Check", "Actions", "Data", "Engine API",
        "Misc", "Other", "General", "Basic", "Look", "World", "Stuff", "Various", "Uncategorized",
    };
    for (const char* s : k) if (std::strcmp(name, s) == 0) return true;
    return false;
}

struct VsPlace {
    const char* category = nullptr;
    const char* subcategory = "";   // never null
    bool browse = true;             // false: still found by search, not listed while browsing
};

inline VsPlace VsAt(const char* category, const char* sub = "") {
    VsPlace p; p.category = category; p.subcategory = sub ? sub : ""; return p;
}

inline bool VsIs(const char* type, const char* a) { return std::strcmp(type, a) == 0; }
inline bool VsPre(const char* type, const char* p) { return std::strncmp(type, p, std::strlen(p)) == 0; }

// Lower ranks sort first. An empty subcategory is the common part of a topic and always comes first.
inline int VsSubOrder(const char* sub) {
    if (!sub || !sub[0]) return -1;
    static const char* k[] = {
        "Keys", "Mouse", "Axes",
        "Collisions", "Queries",
        "Loops", "Timing", "Functions",
        "Compare", "Change", "Shared", "Saved", "Story",
        "Random", "Vectors", "Angles", "Curves",
        "Lists", "Maps", "Structs", "Color",
        "Items", "Appearance", "Lines", "Survival",
        "Turn", "Size", "Tweens",
        "Character", "IK", "Cutscenes",
        "Buttons", "Bars", "Fields", "Layout", "Popups", "Theme",
        "Controls",
        "Language",
        "Navigation", "Building", "Atmosphere",
        "Steam", "PlayFab",
        "Behavior",
    };
    for (int i = 0; i < (int)(sizeof k / sizeof k[0]); ++i)
        if (std::strcmp(sub, k[i]) == 0) return i;
    return 1000;
}

// A text-script builtin (Fn.print). Always a real topic; the long tail is Advanced.
inline bool VsTokenIs(const std::string& low, const char* tok) {
    const std::string t = tok;
    for (std::size_t p = 0; (p = low.find(t, p)) != std::string::npos; p += 1) {
        const bool left = p == 0 || low[p - 1] == '_' || low[p - 1] == '.';
        const std::size_t end = p + t.size();
        const bool right = end == low.size() || low[end] == '_' || low[end] == '.';
        if (left && right) return true;
    }
    return false;
}

inline VsPlace VsPlaceForBuiltin(const char* name) {
    std::string low = name ? name : "";
    for (char& c : low) c = (char)std::tolower((unsigned char)c);
    auto has = [&](const char* t) { return VsTokenIs(low, t); };
    if (has("clipboard")) return VsAt("Text");   // get_clipboard_text, set_clipboard_text
    if (has("print") || has("log") || has("warn") || has("assert") || has("debug") || has("trace"))
        return VsAt("Debug");
    if (low.rfind("game_", 0) == 0 && (has("save") || has("load") || has("slot") || has("continue") || has("autosave")))
        return VsAt("Variables", "Saved");   // game_save, game_load, game_continue ... (save slots)
    if (has("sound") || has("music") || has("audio") || has("sfx")) return VsAt("Audio");
    if (has("anim") || has("clip") || has("ik")) return VsAt("Animation");
    if (has("camera") || has("fov") || has("shake")) return VsAt("Camera");
    if (has("ui") || has("button") || has("slider") || has("toast") || has("tooltip") || has("font"))
        return VsAt("UI");
    if (has("key") || has("mouse") || has("gamepad") || has("joystick") || has("axis") || has("input") ||
        has("gesture") || has("swipe") || has("pinch") || has("twist") || has("touch"))
        return VsAt("Input");
    if (has("raycast") || has("overlap") || has("cast") || has("explosion") || has("rigid") ||
        has("force") || has("impulse") || has("velocity") || has("physics") || has("gravity"))
        return VsAt("Physics");
    if (has("net") || has("steam") || has("rpc") || has("lobby")) return VsAt("Online");
    if (has("wait") || has("delay") || has("timescale") || has("pause") || has("timer") || has("cooldown"))
        return VsAt("Time");
    if (has("tween")) return VsAt("Movement", "Tweens");
    if (has("move") || has("teleport") || has("translate") || has("rotate") || has("position") ||
        has("lookat") || has("look"))
        return VsAt("Movement");
    if (has("spawn") || has("destroy") || has("instantiate") || has("prefab") || has("find") ||
        has("tag") || has("object"))
        return VsAt("Objects");
    if (has("scene") || has("quest") || has("dialogue") || has("weather") || has("fog") || has("nav"))
        return VsAt("Scene");
    if (has("save") || has("prefs") || has("checkpoint")) return VsAt("Variables", "Saved");
    if (has("var") || has("global")) return VsAt("Variables");
    if (has("list") || has("array") || has("map") || has("dict")) return VsAt("Lists & Maps", "Lists");
    if (has("str") || has("string") || has("format") || has("concat") || has("text")) return VsAt("Text");
    if (has("random") || has("abs") || has("sin") || has("cos") || has("tan") || has("lerp") ||
        has("clamp") || has("sqrt") || has("pow") || has("min") || has("max") || has("vec") || has("math"))
        return VsAt("Math");
    return VsAt("Advanced");
}

// Gameplay nodes, escape hatches, and flow nodes added on the logic-runtime branch.
inline VsPlace VsPlaceForNewNode(const char* type) {
    if (VsIs(type, "TweenPosition") || VsIs(type, "TweenRotation") || VsIs(type, "TweenScale") ||
        VsIs(type, "TweenColor"))
        return VsAt("Movement", "Tweens");
    if (VsIs(type, "RotateTowards") || VsIs(type, "OrbitAround") || VsIs(type, "FollowPath") ||
        VsIs(type, "Bob") || VsIs(type, "Spin") || VsIs(type, "Knockback") || VsIs(type, "SmoothFollow") ||
        VsIs(type, "Patrol") || VsIs(type, "Chase") || VsIs(type, "Flee") || VsIs(type, "Wander"))
        return VsAt("Movement");
    if (VsIs(type, "CrossfadeClip") || VsIs(type, "StopAnimation") || VsIs(type, "WaitAnimEnd"))
        return VsAt("Animation");
    if (VsIs(type, "PickUp") || VsIs(type, "Drop") || VsIs(type, "Collect")) return VsAt("Objects", "Items");
    if (VsIs(type, "DoorOpen") || VsIs(type, "DoorClose") || VsIs(type, "Lever") ||
        VsIs(type, "Damage") || VsIs(type, "Heal") || VsIs(type, "IsDead") || VsIs(type, "Respawn") ||
        VsIs(type, "SpawnProjectile") || VsIs(type, "Shoot") || VsIs(type, "Ammo") ||
        VsIs(type, "SpawnEvery") || VsIs(type, "DestroyAfter") || VsIs(type, "PoolGet") ||
        VsIs(type, "PoolRelease") || VsIs(type, "FindByTag") || VsIs(type, "SendMessage") ||
        VsIs(type, "Broadcast"))
        return VsAt("Objects");
    if (VsIs(type, "OnTagEnter") || VsIs(type, "OnTagStay") || VsIs(type, "OnTagExit") ||
        VsIs(type, "Proximity"))
        return VsAt("Physics", "Collisions");
    if (VsIs(type, "CanSee")) return VsAt("Physics", "Queries");
    if (VsIs(type, "OnInteract") || VsIs(type, "OnDeath") || VsIs(type, "OnHealthChanged"))
        return VsAt("Events");
    if (VsIs(type, "OnAction") || VsIs(type, "OnActionUp") || VsIs(type, "OnActionHeld"))
        return VsAt("Input", "Controls");
    if (VsIs(type, "OnTap") || VsIs(type, "OnDoubleTap") || VsIs(type, "OnLongPress") ||
        VsIs(type, "OnSwipe") || VsIs(type, "OnPinch") || VsIs(type, "OnRotate") ||
        VsIs(type, "GestureSwipeDirection") || VsIs(type, "GesturePinchScale") || VsIs(type, "GestureTwist"))
        return VsAt("Input");
    if (VsIs(type, "GetMoveAxis")) return VsAt("Input", "Axes");
    if (VsIs(type, "ScoreAdd") || VsIs(type, "ScoreSet") || VsIs(type, "ScoreGet")) return VsAt("Variables");
    if (VsIs(type, "CheckpointSave") || VsIs(type, "CheckpointLoad")) return VsAt("Variables", "Saved");
    if (VsIs(type, "Countdown")) return VsAt("Flow", "Timing");
    if (VsIs(type, "SwitchString") || VsIs(type, "SwitchCases") || VsIs(type, "LatchGate"))
        return VsAt("Flow");
    if (VsIs(type, "ForEachMap")) return VsAt("Flow", "Loops");
    if (VsIs(type, "RunGraph")) return VsAt("Flow", "Functions");
    if (VsIs(type, "Win") || VsIs(type, "Lose") || VsIs(type, "RestartScene")) return VsAt("Scene");
    if (VsIs(type, "SetProgress")) return VsAt("UI", "Bars");
    if (VsIs(type, "ShowFloatingText") || VsIs(type, "FadeScreen")) return VsAt("UI");
    if (VsIs(type, "SetCameraTarget") || VsIs(type, "Zoom") || VsIs(type, "SwitchCamera"))
        return VsAt("Camera");
    if (VsIs(type, "PlaySoundAt") || VsIs(type, "StopSound")) return VsAt("Audio");
    if (VsIs(type, "RandomPick") || VsIs(type, "RandomPoint")) return VsAt("Math", "Random");
    if (VsIs(type, "ShuffleList") || VsIs(type, "ListMin") || VsIs(type, "ListMax"))
        return VsAt("Lists & Maps", "Lists");
    if (VsPre(type, "Map") || VsIs(type, "NewMap")) return VsAt("Lists & Maps", "Maps");
    if (VsIs(type, "StructNew") || VsIs(type, "StructGet") || VsIs(type, "StructSet")) return VsAt("Lists & Maps", "Structs");
    if (VsIs(type, "MakeColor") || VsIs(type, "SplitColor")) return VsAt("Math", "Color");
    if (VsIs(type, "IsValid")) return VsAt("Math", "Compare");
    if (VsIs(type, "NotAnd") || VsIs(type, "NotOr") || VsIs(type, "AllTrue") || VsIs(type, "AnyTrue") ||
        VsIs(type, "IsEven") || VsIs(type, "IsOdd") || VsIs(type, "PickByIndex") || VsIs(type, "FirstValid") ||
        VsIs(type, "IsInState"))
        return VsAt("Math", "Compare");
    if (VsIs(type, "DoWhile") || VsIs(type, "ForStep") || VsIs(type, "StepThrough") || VsIs(type, "CountUntil"))
        return VsAt("Flow", "Loops");
    if (VsIs(type, "WaitUntil") || VsIs(type, "WaitWhile") || VsIs(type, "WaitSteady") || VsIs(type, "WaitLimit"))
        return VsAt("Flow", "Timing");
    if (VsIs(type, "BranchInOrder") || VsIs(type, "BecameTrue") || VsIs(type, "BecameFalse") ||
        VsIs(type, "WeightedBranch") || VsIs(type, "SetState") || VsIs(type, "GetState") ||
        VsIs(type, "WhenStateBecomes") || VsIs(type, "WhenStateLeaves"))
        return VsAt("Flow");
    if (VsIs(type, "HeldLong") || VsIs(type, "HappenedNear") || VsIs(type, "EveryWhile") ||
        VsIs(type, "BlendTime") || VsIs(type, "Stopwatch") || VsIs(type, "WaitBetween"))
        return VsAt("Flow", "Timing");
    if (VsIs(type, "EaseToward")) return VsAt("Math", "Curves");
    if (VsIs(type, "IsSlightly") || VsIs(type, "IsMostly") || VsIs(type, "IsVery") || VsIs(type, "PickBest"))
        return VsAt("Math", "Compare");
    if (VsIs(type, "ShuffleBag") || VsIs(type, "ChanceEachSecond")) return VsAt("Math", "Random");
    if (VsIs(type, "TryFirst") || VsIs(type, "AllSucceed") || VsIs(type, "FlipOutcome") ||
        VsIs(type, "AlwaysSucceed") || VsIs(type, "RepeatUntilFail") || VsIs(type, "SucceedNow") ||
        VsIs(type, "FailNow") || VsIs(type, "CheckOutcome"))
        return VsAt("Advanced", "Behavior");
    if (VsIs(type, "InCone") || VsIs(type, "LineOfSight") || VsIs(type, "CountNearby") ||
        VsIs(type, "NearestObject") || VsIs(type, "FarthestObject"))
        return VsAt("Physics", "Queries");
    if (VsIs(type, "InsideArea") || VsIs(type, "SortByDistance")) return VsAt("Scene");
    if (VsIs(type, "WhenChanged") || VsIs(type, "DoUnless")) return VsAt("Flow");
    if (VsIs(type, "RememberValue") || VsIs(type, "RecallValue")) return VsAt("Variables", "Saved");
    if (VsIs(type, "HealthEmpty") || VsIs(type, "HealthFull") || VsIs(type, "AddBuff"))
        return VsAt("Objects", "Survival");
    if (VsIs(type, "AdvanceObjective")) return VsAt("Scene", "Story");
    if (VsIs(type, "AssertThis") || VsIs(type, "LogOnce") || VsIs(type, "DrawDebugLine"))
        return VsAt("Debug");
    if (VsIs(type, "GetProperty") || VsIs(type, "SetProperty") || VsIs(type, "CallMethod") ||
        VsIs(type, "GetComponent") || VsIs(type, "AddComponent") || VsIs(type, "RunSnippet") ||
        VsIs(type, "CallScript"))
        return VsAt("Advanced");
    return {};
}

// Hand-written nodes (no '.' in the type). Unknown types come back with a null category.
inline VsPlace VsPlaceForNode(const char* type) {
    if (!type || !type[0]) return {};
    if (VsPre(type, "Lb")) {
#include "generated/library_place.inc"
    }
    if (VsPre(type, "Gl") && type[2] >= 'A' && type[2] <= 'Z') {   // the gameplay library (GlGetHealth ...)
#include "generated/gplib_place.inc"
    }
    if (VsPre(type, "Fn.")) return VsPlaceForBuiltin(type + 3);
    // Events that belong with the thing they listen to.
    if (VsPre(type, "OnKey")) return VsAt("Input", "Keys");
    if (VsPre(type, "OnMouse") || VsIs(type, "OnClick")) return VsAt("Input", "Mouse");
    if (VsPre(type, "OnCollision") || VsPre(type, "OnTrigger")) return VsAt("Physics", "Collisions");
    if (VsIs(type, "OnFixedUpdate")) return VsAt("Physics");
    if (VsIs(type, "OnAnimEvent") || VsIs(type, "OnSequenceEvent")) return VsAt("Animation");
    if (VsPre(type, "OnDialogue") || VsPre(type, "OnQuest") || VsIs(type, "OnObjectiveCompleted")) return VsAt("Scene", "Story");
    if (VsPre(type, "OnVar") || VsPre(type, "OnSceneVar") || VsPre(type, "OnGlobalVar")) return VsAt("Variables");
    if (VsIs(type, "OnTimer")) return VsAt("Time");
    if (VsPre(type, "OnItem") || VsIs(type, "OnEquip") || VsIs(type, "OnCraft") || VsIs(type, "OnInventoryFull")) return VsAt("Objects", "Items");
    if (VsIs(type, "OnCheatDetected") || VsPre(type, "OnValidate") || VsIs(type, "OnNetRpc")) return VsAt("Online");
    if (VsIs(type, "OnLanguageChanged")) return VsAt("Text", "Language");
    if (VsIs(type, "OnObjectPlaced") || VsIs(type, "OnObjectRemoved") || VsIs(type, "OnLogicSignal")) return VsAt("Scene");
    if (VsIs(type, "OnStart") || VsIs(type, "OnUpdate") || VsIs(type, "OnLateUpdate") || VsIs(type, "OnEnable") ||
        VsIs(type, "OnDisable") || VsIs(type, "OnDestroy") || VsIs(type, "OnEvent")) return VsAt("Events");
    // Flow
    if (VsIs(type, "Timer") || VsIs(type, "Wait") || VsIs(type, "Delay") || VsIs(type, "Cooldown") || VsIs(type, "EveryN"))
        return VsAt("Flow", "Timing");
    if (VsIs(type, "ForLoop") || VsIs(type, "Repeat") || VsIs(type, "While") || VsIs(type, "ForEach") ||
        VsIs(type, "Break") || VsIs(type, "Continue")) return VsAt("Flow", "Loops");
    if (VsIs(type, "FunctionStart") || VsIs(type, "Return") || VsIs(type, "CallFunction")) return VsAt("Flow", "Functions");
    if (VsIs(type, "Branch") || VsIs(type, "Sequence") || VsIs(type, "Once") || VsIs(type, "FlipFlop") ||
        VsIs(type, "Gate") || VsIs(type, "DoN") || VsIs(type, "SwitchInt") || VsIs(type, "ChanceBranch") ||
        VsIs(type, "Counter") || VsIs(type, "Action") || VsIs(type, "Actions") ||
        VsIs(type, "Condition")) return VsAt("Flow");
    // Variables
    if (VsIs(type, "SaveValue") || VsIs(type, "LoadValue")) return VsAt("Variables", "Saved");
    if (VsIs(type, "GetSceneVar") || VsIs(type, "SetSceneVar") || VsIs(type, "AddSceneVar") ||
        VsIs(type, "GetGlobalVar") || VsIs(type, "SetGlobalVar") || VsIs(type, "AddGlobalVar")) return VsAt("Variables", "Shared");
    if (VsIs(type, "SetStoryVar") || VsIs(type, "GetStoryVar")) return VsAt("Variables", "Story");
    if (VsIs(type, "GetVar") || VsIs(type, "SetVar") || VsIs(type, "AddVar") || VsIs(type, "Toggle") ||
        VsIs(type, "SetObjectVar") || VsIs(type, "GetObjectVar") || VsIs(type, "SetLocal") || VsIs(type, "GetLocal") ||
        VsIs(type, "HasVar") || VsIs(type, "Const")) return VsAt("Variables");
    // Math
    if (VsIs(type, "Sin") || VsIs(type, "Cos") || VsIs(type, "Tan") || VsIs(type, "Asin") || VsIs(type, "Acos") ||
        VsIs(type, "Atan2") || VsIs(type, "Deg2Rad") || VsIs(type, "Rad2Deg") || VsIs(type, "DeltaAngle"))
        return VsAt("Math", "Angles");
    if (VsIs(type, "EaseIn") || VsIs(type, "EaseOut") || VsIs(type, "EaseInOut") || VsIs(type, "SmoothStep") ||
        VsIs(type, "EaseCurve") || VsIs(type, "SpringStep"))
        return VsAt("Math", "Curves");
    if (VsIs(type, "TweenMove") || VsIs(type, "TweenMoveSpeed") || VsIs(type, "TweenKillTag") ||
        VsIs(type, "TweenPauseTag") || VsIs(type, "TweenPlayTag"))
        return VsAt("Movement", "Tweens");
    if (VsPre(type, "Vec") || VsIs(type, "MakeVec3") || VsIs(type, "ForwardVector") || VsIs(type, "RightVector") ||
        VsIs(type, "UpVector") || VsIs(type, "Distance2D")) return VsAt("Math", "Vectors");
    if (VsIs(type, "Random") || VsIs(type, "RandomRange") || VsIs(type, "RandomInt") || VsIs(type, "Chance") ||
        VsIs(type, "Noise")) return VsAt("Math", "Random");
    if (VsIs(type, "Compare") || VsIs(type, "And") || VsIs(type, "Or") || VsIs(type, "Not") || VsIs(type, "Xor") ||
        VsIs(type, "Equals") || VsIs(type, "NotEquals") || VsIs(type, "Approx") || VsIs(type, "Between") ||
        VsIs(type, "Select")) return VsAt("Math", "Compare");
    if (VsIs(type, "Add") || VsIs(type, "Sub") || VsIs(type, "Mul") || VsIs(type, "Div") || VsIs(type, "Min") ||
        VsIs(type, "Max") || VsIs(type, "Mod") || VsIs(type, "Pow") || VsIs(type, "Abs") || VsIs(type, "Neg") ||
        VsIs(type, "Sqrt") || VsIs(type, "Floor") ||
        VsIs(type, "Round") || VsIs(type, "Ceil") || VsIs(type, "Sign") || VsIs(type, "Clamp") || VsIs(type, "Lerp") ||
        VsIs(type, "Exp") || VsIs(type, "Log") || VsIs(type, "InverseLerp") || VsIs(type, "Remap") ||
        VsIs(type, "PingPong") || VsIs(type, "Repeat01") || VsIs(type, "MoveTowardsValue") ||
        VsIs(type, "Frac") || VsIs(type, "OneMinus") || VsIs(type, "Saturate") ||
        VsIs(type, "Snap") || VsIs(type, "Wrap") || VsIs(type, "Average")) return VsAt("Math");
    // Text
    if (VsIs(type, "Localize") || VsIs(type, "GetLanguage") || VsIs(type, "SetLanguage")) return VsAt("Text", "Language");
    if (VsIs(type, "Concat") || VsIs(type, "ToString") || VsIs(type, "ToFixed") || VsIs(type, "TextLength") ||
        VsIs(type, "Upper") || VsIs(type, "Lower") || VsIs(type, "Contains") || VsIs(type, "Format") ||
        VsIs(type, "Substring") || VsIs(type, "Replace") || VsIs(type, "ToNumber") || VsIs(type, "StartsWith") ||
        VsIs(type, "EndsWith") || VsIs(type, "Trim") || VsIs(type, "IndexOf")) return VsAt("Text");
    // Lists
    if (VsIs(type, "NewList") || VsPre(type, "List")) return VsAt("Lists & Maps", "Lists");
    // Time
    if (VsIs(type, "Time") || VsIs(type, "DeltaTime") || VsIs(type, "TimeScale") || VsIs(type, "FrameCount") ||
        VsIs(type, "SetTimeScale")) return VsAt("Time");
    // Input
    if (VsPre(type, "Key")) return VsAt("Input", "Keys");
    if (VsPre(type, "Mouse") || VsIs(type, "ShowCursor")) return VsAt("Input", "Mouse");
    if (VsPre(type, "Axis")) return VsAt("Input", "Axes");
    // Physics
    if (VsIs(type, "ContactPoint") || VsIs(type, "ContactNormal") || VsIs(type, "ImpactSpeed")) return VsAt("Physics", "Collisions");
    if (VsPre(type, "Raycast") || VsPre(type, "Overlap") || VsIs(type, "SphereCast3D") ||
        VsIs(type, "BoxCast2D") || VsIs(type, "CapsuleCast2D"))
        return VsAt("Physics", "Queries");
    if (VsPre(type, "Vel") || VsIs(type, "AddForce") || VsIs(type, "AddImpulse") || VsIs(type, "SetVelocity") ||
        VsIs(type, "AddExplosionForce"))
        return VsAt("Physics");
    // Animation
    if (VsPre(type, "Ik")) return VsAt("Animation", "IK");
    if (VsIs(type, "Die") || VsIs(type, "Revive") || VsIs(type, "Dive") || VsIs(type, "Sit") || VsIs(type, "StandUp") ||
        VsIs(type, "Interact") || VsIs(type, "HitReact") || VsIs(type, "Roll") || VsIs(type, "Dodge") ||
        VsIs(type, "Vault") || VsIs(type, "Mantle") || VsIs(type, "LedgeGrab") || VsIs(type, "ClimbUp") ||
        VsIs(type, "LetGo") || VsIs(type, "Shimmy") || VsIs(type, "IsInAction") || VsIs(type, "IsSitting") ||
        VsIs(type, "IsHanging") || VsIs(type, "SeatPrompt")) return VsAt("Animation", "Character");
    if (VsPre(type, "Anim") || VsIs(type, "PlayClip") || VsIs(type, "PlayAction") ||
        VsIs(type, "SetBlendShape") || VsIs(type, "GetBlendShape") || VsIs(type, "AnimEventName")) return VsAt("Animation");
    // Audio
    if (VsIs(type, "PlaySound") || VsPre(type, "Music")) return VsAt("Audio");
    // Camera
    if (VsIs(type, "CameraShake") || VsIs(type, "SetFov") || VsIs(type, "CameraFollow")) return VsAt("Camera");
    // UI
    if (VsIs(type, "SetText") || VsIs(type, "ShowUI") || VsIs(type, "HideUI")) return VsAt("UI");
    // Debug
    if (VsIs(type, "Print") || VsIs(type, "LogWarn") || VsIs(type, "LogError")) return VsAt("Debug");
    // Online
    if (VsPre(type, "Net") || VsPre(type, "SyncVar") || VsPre(type, "Steam") || VsIs(type, "RejectRpc") ||
        VsIs(type, "RejectHit") || VsIs(type, "ServerBudget")) return VsAt("Online");
    // Scene
    if (VsIs(type, "SetWeather") || VsIs(type, "SetFog") || VsIs(type, "SetPostEffect")) return VsAt("Scene", "Atmosphere");
    if (VsIs(type, "LoadScene") || VsIs(type, "PauseGame") || VsIs(type, "QuitGame")) return VsAt("Scene");
    if (VsPre(type, "Nav") || VsIs(type, "NavArrived")) return VsAt("Scene", "Navigation");
    if (VsIs(type, "PlaySequence") || VsIs(type, "StopSequence") || VsIs(type, "StartDialogue") ||
        VsIs(type, "StartQuest") || VsIs(type, "CompleteObjective") || VsIs(type, "QuestState") ||
        VsIs(type, "EventValue") || VsIs(type, "EventName")) return VsAt("Scene", "Story");
    if (VsPre(type, "EnterBuild") || VsPre(type, "ExitBuild") || VsIs(type, "SetBuildPalette") ||
        VsIs(type, "SaveCreation") || VsIs(type, "LoadCreation") || VsIs(type, "InBuildMode")) return VsAt("Scene", "Building");
    if (VsPre(type, "Bt")) return VsAt("Advanced", "Behavior");
    // Objects and items
    if (VsIs(type, "HasItem") || VsIs(type, "ItemCount") || VsIs(type, "AddItem") || VsIs(type, "RemoveItem") ||
        VsIs(type, "EquipItem") || VsIs(type, "CraftItem") || VsIs(type, "LootRoll") || VsIs(type, "SpawnPickup"))
        return VsAt("Objects", "Items");
    if (VsIs(type, "SwingWeapon") || VsIs(type, "FireWeapon") || VsIs(type, "ReloadWeapon") ||
        VsIs(type, "WeaponAmmo") || VsIs(type, "SetWeapon"))
        return VsAt("Objects", "Combat");
    if (VsIs(type, "Spawn") || VsIs(type, "Destroy") || VsIs(type, "SetActive") || VsIs(type, "SetVisible") ||
        VsIs(type, "SetColor") || VsPre(type, "Particle") || VsIs(type, "SetLinePoints") || VsIs(type, "ClearTrail") ||
        VsIs(type, "GetSplinePoint") || VsIs(type, "SplineLength") || VsIs(type, "ObjectExists") ||
        VsIs(type, "ObjectPosition") || VsIs(type, "DistanceTo") || VsIs(type, "SelfName") || VsIs(type, "Self") ||
        VsIs(type, "Other") || VsIs(type, "OtherName") || VsIs(type, "OtherTag") || VsIs(type, "FindObject") ||
        VsIs(type, "GetName") || VsIs(type, "GetTag") || VsIs(type, "FindWithTag") || VsIs(type, "CountWithTag") ||
        VsIs(type, "NearestWithTag") || VsIs(type, "AllWithTag") || VsIs(type, "SendEvent") || VsIs(type, "BroadcastEvent"))
        return VsAt("Objects");
    // Movement (the rest of the transform nodes)
    if (VsIs(type, "GetRotation") || VsIs(type, "SetRotation") || VsIs(type, "SetRotation3") ||
        VsIs(type, "Rotate") || VsIs(type, "Rotate3") || VsIs(type, "LookAt")) return VsAt("Movement", "Turn");
    if (VsIs(type, "GetScale") || VsIs(type, "SetScale")) return VsAt("Movement", "Size");
    if (VsIs(type, "GetX") || VsIs(type, "GetY") || VsIs(type, "GetZ") || VsIs(type, "GetPosition") ||
        VsIs(type, "SetPosition") || VsIs(type, "SetPositionVec") || VsIs(type, "Translate") ||
        VsIs(type, "MoveBy") || VsIs(type, "MoveTowards") || VsIs(type, "Teleport")) return VsAt("Movement");
    if (VsPlace extra = VsPlaceForNewNode(type); extra.category) return extra;
    return {};
}

// Engine API nodes, by the script type they belong to.
inline VsPlace VsUiPlace(const char* apiType) {
    if (VsIs(apiType, "UIButton")) return VsAt("UI", "Buttons");
    if (VsIs(apiType, "UISlider") || VsIs(apiType, "UIProgressBar") || VsIs(apiType, "UIRadialProgress")) return VsAt("UI", "Bars");
    if (VsIs(apiType, "UIInputField") || VsIs(apiType, "UIDropdown")) return VsAt("UI", "Fields");
    if (VsIs(apiType, "UIScrollView") || VsIs(apiType, "UITabs") || VsIs(apiType, "UISlotGrid")) return VsAt("UI", "Layout");
    if (VsIs(apiType, "UITooltip") || VsIs(apiType, "UIToast") || VsIs(apiType, "UIModal")) return VsAt("UI", "Popups");
    if (VsIs(apiType, "UITheme") || VsIs(apiType, "UIColorPicker")) return VsAt("UI", "Theme");
    return VsAt("UI");
}

inline VsPlace VsPlaceForApi(const char* apiType, const char* member) {
    if (!apiType) return {};
    if (!member) member = "";
    if (VsIs(apiType, "GameObject")) return VsAt("Objects");
    if (VsIs(apiType, "Transform")) {
        if (VsIs(member, "eulerAngles") || VsIs(member, "localEulerAngles") || VsIs(member, "Rotate") ||
            VsIs(member, "LookAt") || VsIs(member, "forward") || VsIs(member, "right") || VsIs(member, "up"))
            return VsAt("Movement", "Turn");
        if (VsIs(member, "localScale")) return VsAt("Movement", "Size");
        return VsAt("Movement");
    }
    if (VsIs(apiType, "Vector3")) return VsAt("Math", "Vectors");
    if (VsIs(apiType, "Mathf")) return VsAt("Math");
    if (VsIs(apiType, "Rigidbody")) {
        if (VsIs(member, "Raycast") || VsPre(member, "Overlap") || VsPre(member, "Sweep")) return VsAt("Physics", "Queries");
        return VsAt("Physics");
    }
    if (VsIs(apiType, "Input")) {
        if (VsPre(member, "GetKey")) return VsAt("Input", "Keys");
        if (VsPre(member, "GetMouse") || VsIs(member, "mousePosition")) return VsAt("Input", "Mouse");
        if (VsIs(member, "GetAxis") || VsIs(member, "GetActionVector")) return VsAt("Input", "Axes");
        return VsAt("Input", "Controls");
    }
    if (VsIs(apiType, "PlayerController")) return VsAt("Input");
    if (VsIs(apiType, "PlayerView")) return VsAt("Camera");
    if (VsPre(apiType, "UI") || VsIs(apiType, "Crosshair")) return VsUiPlace(apiType);
    if (VsIs(apiType, "Time")) return VsAt("Time");
    if (VsIs(apiType, "Debug")) return VsAt("Debug");
    if (VsIs(apiType, "Audio")) return VsAt("Audio");
    if (VsIs(apiType, "SaveGame")) return VsAt("Variables", "Saved");
    if (VsIs(apiType, "Localization")) return VsAt("Text", "Language");
    if (VsIs(apiType, "Sequence")) return VsAt("Animation", "Cutscenes");
    if (VsIs(apiType, "PostProcess") || VsIs(apiType, "Fog") || VsIs(apiType, "Weather")) return VsAt("Scene", "Atmosphere");
    if (VsIs(apiType, "NavMeshAgent")) return VsAt("Scene", "Navigation");
    if (VsIs(apiType, "Dialogue") || VsIs(apiType, "Story") || VsIs(apiType, "Quest")) return VsAt("Scene", "Story");
    if (VsIs(apiType, "BehaviorTree")) return VsAt("Advanced", "Behavior");
    if (VsIs(apiType, "Animator")) return VsAt("Animation");
    if (VsIs(apiType, "IK")) return VsAt("Animation", "IK");
    if (VsIs(apiType, "Spline") || VsIs(apiType, "SplineFollower") || VsIs(apiType, "LineRenderer") ||
        VsIs(apiType, "TrailRenderer")) return VsAt("Objects", "Lines");
    return {};   // a new API type needs a line above, or the category test fails
}

// True when this script member is the same job as a hand-written node (Abs, Translate, GetPosition).
inline bool VsApiDuplicatesHandNode(const char* member, const char* handType) {
    if (!member || !handType || !member[0] || !handType[0]) return false;
    std::string m, h;
    for (const char* s = member; *s; ++s) m += (char)std::tolower((unsigned char)*s);
    for (const char* s = handType; *s; ++s) h += (char)std::tolower((unsigned char)*s);
    return h == m || h == "get" + m || h == "set" + m;
}

// True when this Actions step is the same job as a hand-written node (Wait, Play Sound, Set Position).
// It stays searchable by its old label, and it is not a second row while browsing.
inline bool VsStepSameAsNode(const char* op) {
    if (!op) return false;
    static const char* k[] = {
        "wait", "play_sound", "set_pos", "look_at", "move", "move_toward", "spawn", "spawn3", "spawn_at",
        "destroy", "set_var", "add_var", "toggle_var", "log", "load_scene", "camera_shake", "set_timescale",
        "pause", "quit", "set_text", "play_clip", "play_anim", "play_action", "set_color", "velocity",
        "velocity3", "impulse", "impulse3", "force3", "set_visible", "set_active", "set_rotation",
        "set_rotation3", "rotate", "set_scale", "die", "revive", "dive", "sit", "stand_up", "interact",
        "hit_react", "roll", "dodge", "vault", "mantle", "ledge_grab", "climb_up", "let_go", "shimmy",
    };
    for (const char* s : k) if (std::strcmp(op, s) == 0) return true;
    return false;
}

// One Actions step (the old Do / Check lists). `group` is the old bucket in main.cpp.
inline VsPlace VsPlaceForStep(const char* op, const char* group) {
    if (!op) op = "";
    if (!group) group = "";
    auto finish = [&](VsPlace p) {
        if (p.category && VsStepSameAsNode(op)) p.browse = false;
        return p;
    };
    if (VsIs(op, "log")) return finish(VsAt("Debug"));
    if (VsIs(op, "play_sound")) return finish(VsAt("Audio"));
    if (VsIs(op, "play_anim") || VsIs(op, "play_clip") || VsIs(op, "play_action")) return finish(VsAt("Animation"));
    if (VsIs(op, "set_anim") || VsIs(op, "stop_clip") || VsIs(op, "play_layer") || VsIs(op, "stop_layer") || VsIs(op, "clip_speed"))
        return finish(VsAt("Animation"));
    if (VsPre(op, "set_text") || VsIs(op, "set_bar") || VsIs(op, "str_set_text")) return finish(VsAt("UI"));
    if (VsIs(op, "set_sprite") || VsIs(op, "set_color") || VsIs(op, "set_color_var") || VsIs(op, "emit") ||
        VsIs(op, "fade") || VsIs(op, "flash") || VsIs(op, "tween_color"))
        return finish(VsAt("Objects", "Appearance"));
    if (VsIs(op, "always") || VsIs(op, "chance")) return finish(VsAt("Flow"));
    if (VsIs(op, "is_active") || VsIs(op, "has_tag")) return finish(VsAt("Objects"));
    if (VsIs(op, "cooldown")) return finish(VsAt("Flow", "Timing"));
    if (VsPre(op, "raycast")) return finish(VsAt("Physics", "Queries"));
    if (VsIs(op, "is_moving")) return finish(VsAt("Physics"));
    if (VsPre(op, "set_cam") || VsPre(op, "cam_") || VsPre(op, "camera_")) return finish(VsAt("Camera"));
    if (VsIs(op, "set_timescale") || VsIs(op, "pause") || VsIs(op, "resume") || VsIs(op, "toggle_pause")) return finish(VsAt("Time"));
    if (VsIs(op, "set_bg") || VsIs(op, "set_light") || VsIs(op, "set_ambient")) return finish(VsAt("Scene", "Atmosphere"));
    if (VsPre(op, "key")) return finish(VsAt("Input", "Keys"));
    if (VsPre(op, "mouse")) return finish(VsAt("Input", "Mouse"));
    if (VsIs(op, "has_item")) return finish(VsAt("Objects", "Items"));
    if (VsPre(op, "var_") || VsIs(op, "vars_cmp")) return finish(VsAt("Variables", "Compare"));
    if (VsPre(op, "prefs_") || VsIs(op, "get_prefs")) return finish(VsAt("Variables", "Saved"));
    if (VsIs(op, "mul_var") || VsIs(op, "div_var") || VsIs(op, "rand_var") || VsIs(op, "clamp_var") ||
        VsIs(op, "lerp_var") || VsIs(op, "floor_var") || VsIs(op, "round_var") || VsIs(op, "abs_var") ||
        VsIs(op, "negate_var") || VsIs(op, "sign_var") || VsIs(op, "sqrt_var") || VsIs(op, "pow_var") ||
        VsIs(op, "min_var") || VsIs(op, "max_var") || VsIs(op, "mod_var") || VsIs(op, "swap_var") ||
        VsIs(op, "clamp01_var") || VsIs(op, "round_to_var") || VsIs(op, "percent_var") || VsIs(op, "approach_var") ||
        VsIs(op, "wrap_var") || VsIs(op, "map_range_var") || VsIs(op, "dice_var") || VsIs(op, "random_sign_var") ||
        VsIs(op, "pick_var") || VsIs(op, "tween_var"))
        return finish(VsAt("Variables", "Change"));
    if (VsIs(op, "str_eq") || VsIs(op, "str_neq") || VsIs(op, "str_contains") || VsIs(op, "str_empty") ||
        VsIs(op, "str_starts") || VsIs(op, "str_ends"))
        return finish(VsAt("Text"));
    if (VsIs(group, "Variables")) return finish(VsAt("Variables"));
    if (VsIs(op, "wait") || VsIs(op, "wait_until")) return finish(VsAt("Flow", "Timing"));
    if (VsIs(op, "repeat") || VsIs(op, "end_repeat") || VsIs(op, "while") || VsIs(op, "end_while") ||
        VsIs(op, "for_each") || VsIs(op, "end_for") || VsIs(op, "for_each_tag") || VsIs(op, "end_for_tag") ||
        VsIs(op, "break") || VsIs(op, "continue"))
        return finish(VsAt("Flow", "Loops"));
    if (VsIs(op, "gosub") || VsIs(op, "return_sub") || VsIs(op, "call")) return finish(VsAt("Flow", "Functions"));
    if (VsIs(group, "Flow")) return finish(VsAt("Flow"));
    if (VsIs(op, "achievement") || VsIs(group, "Game")) return finish(VsAt("Online"));
    if (VsIs(group, "Character") || VsIs(group, "Jump") || VsIs(group, "Walk") || VsIs(group, "Hang") ||
        VsIs(group, "Point") || VsIs(group, "Think") || VsIs(group, "Wave Both"))
        return finish(VsAt("Animation", "Character"));
    if (VsIs(group, "Juice")) return finish(VsAt("Movement", "Tweens"));
    if (VsIs(group, "Move")) return finish(VsAt("Movement"));
    if (VsIs(group, "Steam")) return finish(VsAt("Online", "Steam"));
    if (VsIs(group, "PlayFab")) return finish(VsAt("Online", "PlayFab"));
    if (VsIs(group, "Multiplayer")) return finish(VsAt("Online"));
    if (VsIs(group, "Text")) return finish(VsAt("Text"));
    if (VsIs(group, "Survival")) return finish(VsAt("Objects", "Survival"));
    if (VsIs(group, "Object") || VsIs(group, "World")) return finish(VsAt("Objects"));
    if (VsIs(group, "Scene") || VsIs(group, "Scenes")) return finish(VsAt("Scene"));
    if (VsIs(group, "Arrays")) return finish(VsAt("Lists & Maps", "Lists"));
    if (VsIs(group, "Maps")) return finish(VsAt("Lists & Maps", "Maps"));
    if (VsIs(group, "Input")) return finish(VsAt("Input"));
    if (VsIs(group, "Physics")) return finish(VsAt("Physics"));
    if (VsIs(group, "Save")) return finish(VsAt("Variables", "Saved"));
    if (VsIs(group, "Basic") || VsIs(group, "Look")) return {};   // those ops are handled above
    return {};
}

} // namespace okay::editor::vsed
